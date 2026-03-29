#include "llvm_gen.h"

#ifdef JIANG_HAVE_LLVM_C_API

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef struct {
    char name[128];
    LLVMTypeRef type;
} LlvmNamedType;

typedef struct {
    LLVMContextRef context;
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    LLVMTypeRef i1_type;
    LLVMTypeRef i8_type;
    LLVMTypeRef i32_type;
    LLVMTypeRef i64_type;
    LLVMTypeRef f64_type;
    LLVMTypeRef void_type;
    LLVMTypeRef ptr_type;
    LLVMTypeRef slice_u8_type;
    LLVMTypeRef slice_i64_type;
    LLVMValueRef printf_func;
    LLVMValueRef abort_func;
    LLVMValueRef fopen_func;
    LLVMValueRef fclose_func;
    LLVMValueRef fseek_func;
    LLVMValueRef ftell_func;
    LLVMValueRef rewind_func;
    LLVMValueRef fread_func;
    LLVMValueRef malloc_func;
    LLVMValueRef rt_file_exists_func;
    LLVMValueRef rt_read_file_func;
    JirModule* jir;
    CodegenModuleInfo** infos;
    size_t info_count;
    LlvmNamedType named_structs[128];
    size_t named_struct_count;
    LlvmNamedType named_unions[128];
    size_t named_union_count;
    char* error_buf;
    size_t error_buf_size;
} LlvmApiState;

typedef struct {
    JirFunction* func;
    LLVMValueRef llvm_func;
    LLVMBasicBlockRef entry_block;
    LLVMBasicBlockRef current_block;
    LLVMBasicBlockRef label_blocks[512];
    LLVMBasicBlockRef then_blocks[512];
    bool has_label_block[512];
    LLVMValueRef locals[512];
    LLVMValueRef global_array_aliases[512];
    bool has_global_array_alias[512];
    size_t local_count;
} LlvmApiFunctionState;

static void llvm_api_error(char* error_buf, size_t error_buf_size, const char* format, ...) {
    if (!error_buf || error_buf_size == 0) return;
    va_list args;
    va_start(args, format);
    vsnprintf(error_buf, error_buf_size, format, args);
    va_end(args);
}

static TypeExpr* llvm_api_unwrap_type(TypeExpr* type) {
    while (type && (type->kind == TYPE_NULLABLE || type->kind == TYPE_MUTABLE)) {
        type = (type->kind == TYPE_NULLABLE) ? type->as.nullable.element : type->as.mutable.element;
    }
    return type;
}

static bool llvm_api_is_base_type(TypeExpr* type, const char* name) {
    type = llvm_api_unwrap_type(type);
    return type &&
           type->kind == TYPE_BASE &&
           strlen(name) == type->as.base_type.length &&
           strncmp(type->as.base_type.start, name, type->as.base_type.length) == 0;
}

static long long llvm_api_static_array_length(TypeExpr* type) {
    type = llvm_api_unwrap_type(type);
    if (!type || type->kind != TYPE_ARRAY || !type->as.array.length) return -1;
    ASTNode* length = type->as.array.length;
    if (length->type != AST_LITERAL_NUMBER) return -1;
    return (long long)length->as.number.value;
}

static bool llvm_api_token_eq(Token token, const char* name) {
    return token.length == (int)strlen(name) && strncmp(token.start, name, token.length) == 0;
}

static bool llvm_api_token_name_eq(Token token, Token other) {
    return token.length == other.length && strncmp(token.start, other.start, token.length) == 0;
}

static CodegenEnumInfo* llvm_api_find_enum_info(LlvmApiState* state, Token name) {
    if (!name.start || name.length <= 0) return NULL;
    for (size_t i = 0; i < state->info_count; i++) {
        CodegenModuleInfo* info = state->infos ? state->infos[i] : NULL;
        if (!info) continue;
        for (size_t j = 0; j < info->enum_count; j++) {
            if (llvm_api_token_name_eq(info->enums[j].name, name)) return &info->enums[j];
        }
    }
    return NULL;
}

static CodegenStructInfo* llvm_api_find_struct_info(LlvmApiState* state, Token name) {
    if (!name.start || name.length <= 0) return NULL;
    for (size_t i = 0; i < state->info_count; i++) {
        CodegenModuleInfo* info = state->infos ? state->infos[i] : NULL;
        if (!info) continue;
        for (size_t j = 0; j < info->struct_count; j++) {
            if (llvm_api_token_name_eq(info->structs[j].name, name)) return &info->structs[j];
        }
    }
    return NULL;
}

static CodegenUnionInfo* llvm_api_find_union_info(LlvmApiState* state, Token name) {
    if (!name.start || name.length <= 0) return NULL;
    for (size_t i = 0; i < state->info_count; i++) {
        CodegenModuleInfo* info = state->infos ? state->infos[i] : NULL;
        if (!info) continue;
        for (size_t j = 0; j < info->union_count; j++) {
            if (llvm_api_token_name_eq(info->unions[j].name, name)) return &info->unions[j];
        }
    }
    return NULL;
}

static CodegenGlobalInfo* llvm_api_find_global_info(LlvmApiState* state, Token name) {
    if (!name.start || name.length <= 0) return NULL;
    for (size_t i = 0; i < state->info_count; i++) {
        CodegenModuleInfo* info = state->infos ? state->infos[i] : NULL;
        if (!info) continue;
        for (size_t j = 0; j < info->global_count; j++) {
            if (llvm_api_token_name_eq(info->globals[j].name, name)) return &info->globals[j];
        }
    }
    return NULL;
}

static int64_t llvm_api_find_enum_constant_value(LlvmApiState* state, Token name, bool* found) {
    *found = false;
    if (!name.start || name.length <= 0) return 0;
    for (size_t i = 0; i < state->info_count; i++) {
        CodegenModuleInfo* info = state->infos ? state->infos[i] : NULL;
        if (!info) continue;
        for (size_t j = 0; j < info->enum_count; j++) {
            CodegenEnumInfo* e = &info->enums[j];
            for (size_t k = 0; k < e->member_count; k++) {
                char full_name[256];
                snprintf(full_name, sizeof(full_name), "%.*s_%.*s",
                         (int)e->name.length, e->name.start,
                         (int)e->members[k].name.length, e->members[k].name.start);
                if ((int)strlen(full_name) == name.length &&
                    strncmp(full_name, name.start, name.length) == 0) {
                    *found = true;
                    return e->members[k].value;
                }
            }
        }
        for (size_t j = 0; j < info->union_count; j++) {
            CodegenUnionInfo* u = &info->unions[j];
            if (u->tag_enum.length != 0) continue;
            for (size_t k = 0; k < u->variant_count; k++) {
                char full_name[256];
                snprintf(full_name, sizeof(full_name), "%.*sTag_%.*s",
                         (int)u->name.length, u->name.start,
                         (int)u->variants[k].name.length, u->variants[k].name.start);
                if ((int)strlen(full_name) == name.length &&
                    strncmp(full_name, name.start, name.length) == 0) {
                    *found = true;
                    return (int64_t)k;
                }
            }
        }
    }
    return 0;
}

static LLVMTypeRef llvm_api_find_cached_named_type(LlvmNamedType* items, size_t count, Token name) {
    for (size_t i = 0; i < count; i++) {
        if ((int)strlen(items[i].name) == name.length &&
            strncmp(items[i].name, name.start, name.length) == 0) {
            return items[i].type;
        }
    }
    return NULL;
}

static void llvm_api_add_cached_named_type(LlvmNamedType* items, size_t* count, Token name, LLVMTypeRef type) {
    if (*count >= 128) return;
    snprintf(items[*count].name, sizeof(items[*count].name), "%.*s", (int)name.length, name.start);
    items[*count].type = type;
    (*count)++;
}

static bool llvm_api_is_void_type(TypeExpr* type) {
    type = llvm_api_unwrap_type(type);
    return !type ||
           (type->kind == TYPE_TUPLE && type->as.tuple.count == 0) ||
           llvm_api_is_base_type(type, "void") ||
           llvm_api_is_base_type(type, "()");
}

static bool llvm_api_is_u8_slice_type(TypeExpr* type) {
    type = llvm_api_unwrap_type(type);
    return type &&
           type->kind == TYPE_SLICE &&
           llvm_api_is_base_type(type->as.slice.element, "UInt8");
}

static bool llvm_api_is_i64_slice_type(TypeExpr* type) {
    type = llvm_api_unwrap_type(type);
    return type &&
           type->kind == TYPE_SLICE &&
           llvm_api_is_base_type(type->as.slice.element, "Int");
}

static LLVMTypeRef llvm_api_type_ref(LlvmApiState* state, TypeExpr* type) {
    type = llvm_api_unwrap_type(type);
    if (!type) return state->i64_type;
    if (llvm_api_is_void_type(type)) return state->void_type;
    if (llvm_api_is_base_type(type, "Int")) return state->i64_type;
    if (llvm_api_is_base_type(type, "Bool")) return state->i1_type;
    if (llvm_api_is_base_type(type, "UInt8")) return state->i8_type;
    if (llvm_api_is_base_type(type, "Float") || llvm_api_is_base_type(type, "Double")) return state->f64_type;
    if (type->kind == TYPE_BASE) {
        LLVMTypeRef named_struct = llvm_api_find_cached_named_type(state->named_structs, state->named_struct_count,
                                                                   type->as.base_type);
        if (named_struct) return named_struct;
        LLVMTypeRef named_union = llvm_api_find_cached_named_type(state->named_unions, state->named_union_count,
                                                                  type->as.base_type);
        if (named_union) return named_union;
        if (llvm_api_find_enum_info(state, type->as.base_type)) return state->i64_type;
    }
    if (llvm_api_is_u8_slice_type(type)) return state->slice_u8_type;
    if (llvm_api_is_i64_slice_type(type)) return state->slice_i64_type;
    if (type->kind == TYPE_POINTER) {
        LLVMTypeRef elem = llvm_api_type_ref(state, type->as.pointer.element);
        return elem ? LLVMPointerType(elem, 0) : state->ptr_type;
    }
    if (type->kind == TYPE_ARRAY) {
        LLVMTypeRef elem = llvm_api_type_ref(state, type->as.array.element);
        long long len = llvm_api_static_array_length(type);
        if (elem && len >= 0) return LLVMArrayType(elem, (unsigned)len);
    }
    if (type->kind == TYPE_TUPLE) {
        if (type->as.tuple.count == 0) return state->void_type;
        LLVMTypeRef fields[64];
        if (type->as.tuple.count > 64) return NULL;
        for (size_t i = 0; i < type->as.tuple.count; i++) {
            fields[i] = llvm_api_type_ref(state, type->as.tuple.elements[i]);
            if (!fields[i]) return NULL;
        }
        return LLVMStructTypeInContext(state->context, fields, (unsigned)type->as.tuple.count, false);
    }
    return NULL;
}

static bool llvm_api_type_is_scalar(LlvmApiState* state, TypeExpr* type) {
    type = llvm_api_unwrap_type(type);
    return llvm_api_is_base_type(type, "Int") ||
           llvm_api_is_base_type(type, "Bool") ||
           llvm_api_is_base_type(type, "UInt8") ||
           llvm_api_is_base_type(type, "Float") ||
           llvm_api_is_base_type(type, "Double") ||
           (type && type->kind == TYPE_BASE && llvm_api_find_enum_info(state, type->as.base_type) != NULL) ||
           (type && type->kind == TYPE_POINTER);
}

static int llvm_api_find_def_inst_index(JirFunction* func, int reg) {
    if (!func || reg < 0) return -1;
    for (size_t i = 0; i < func->inst_count; i++) {
        if (func->insts[i].dest == reg) return (int)i;
    }
    return -1;
}

static const char* llvm_api_const_string_payload(JirFunction* func, int reg) {
    int idx = llvm_api_find_def_inst_index(func, reg);
    if (idx < 0 || func->insts[idx].op != JIR_OP_CONST_STR) return NULL;
    return func->insts[idx].payload.str_val;
}

static size_t llvm_api_decoded_string_length(const char* s) {
    size_t len = 0;
    if (!s) return 0;
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (s[i] == '\\' && s[i + 1] != '\0' &&
            (s[i + 1] == 'n' || s[i + 1] == 't' || s[i + 1] == 'r' ||
             s[i + 1] == '"' || s[i + 1] == '\\')) {
            i++;
        }
        len++;
    }
    return len;
}

static void llvm_api_decode_string_bytes(const char* s, char* out) {
    size_t len = 0;
    if (!s) return;
    for (size_t i = 0; s[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == '\\' && s[i + 1] != '\0') {
            unsigned char next = (unsigned char)s[++i];
            switch (next) {
                case 'n': ch = '\n'; break;
                case 't': ch = '\t'; break;
                case 'r': ch = '\r'; break;
                case '"': ch = '"'; break;
                case '\\': ch = '\\'; break;
                default:
                    out[len++] = '\\';
                    ch = next;
                    break;
            }
        }
        out[len++] = (char)ch;
    }
}

static JirFunction* llvm_api_find_jir_function(JirModule* mod, Token name) {
    if (!mod) return NULL;
    for (size_t i = 0; i < mod->function_count; i++) {
        JirFunction* func = mod->functions[i];
        if ((size_t)func->name.length == name.length &&
            strncmp(func->name.start, name.start, name.length) == 0) {
            return func;
        }
    }
    return NULL;
}

static LLVMValueRef llvm_api_find_named_global(LlvmApiState* state, Token name) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%.*s", (int)name.length, name.start);
    return LLVMGetNamedGlobal(state->module, buf);
}

static LLVMValueRef llvm_api_find_llvm_function(LlvmApiState* state, Token name) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%.*s", (int)name.length, name.start);
    return LLVMGetNamedFunction(state->module, buf);
}

static LLVMTypeRef llvm_api_find_callable_return_llvm_type(LlvmApiState* state, Token name) {
    for (size_t i = 0; i < state->info_count; i++) {
        CodegenModuleInfo* info = state->infos ? state->infos[i] : NULL;
        if (!info) continue;
        for (size_t j = 0; j < info->function_count; j++) {
            if (llvm_api_token_name_eq(info->functions[j].generated_name, name)) {
                return llvm_api_type_ref(state, info->functions[j].return_type);
            }
        }
        for (size_t j = 0; j < info->struct_count; j++) {
            CodegenStructInfo* s = &info->structs[j];
            char wrapper_name[128];
            snprintf(wrapper_name, sizeof(wrapper_name), "%.*s_new", (int)s->name.length, s->name.start);
            if ((int)strlen(wrapper_name) == name.length &&
                strncmp(wrapper_name, name.start, name.length) == 0) {
                TypeExpr type = { .kind = TYPE_BASE, .as.base_type = s->name };
                return llvm_api_type_ref(state, &type);
            }
            for (size_t k = 0; k < s->method_count; k++) {
                if (llvm_api_token_name_eq(s->methods[k].generated_name, name)) {
                    return llvm_api_type_ref(state, s->methods[k].return_type);
                }
            }
        }
        for (size_t j = 0; j < info->union_count; j++) {
            CodegenUnionInfo* u = &info->unions[j];
            for (size_t k = 0; k < u->variant_count; k++) {
                char ctor_name[128];
                snprintf(ctor_name, sizeof(ctor_name), "%.*s_%.*s",
                         (int)u->name.length, u->name.start,
                         (int)u->variants[k].name.length, u->variants[k].name.start);
                if ((int)strlen(ctor_name) == name.length &&
                    strncmp(ctor_name, name.start, name.length) == 0) {
                    TypeExpr type = { .kind = TYPE_BASE, .as.base_type = u->name };
                    return llvm_api_type_ref(state, &type);
                }
            }
        }
    }
    return NULL;
}

static bool llvm_api_emit_load(LlvmApiState* state, LlvmApiFunctionState* fn_state, int reg,
                               LLVMValueRef* out_value, TypeExpr** out_type) {
    if (reg < 0 || (size_t)reg >= fn_state->local_count) {
        llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: invalid register %d", reg);
        return false;
    }
    TypeExpr* type = fn_state->func->locals[reg].type;
    LLVMTypeRef llvm_type = llvm_api_type_ref(state, type);
    if (!llvm_type || llvm_type == state->void_type) {
        llvm_api_error(state->error_buf, state->error_buf_size,
                       "LLVM C API: unsupported local type for %s", fn_state->func->locals[reg].name);
        return false;
    }
    if (fn_state->has_global_array_alias[reg]) {
        *out_value = LLVMBuildLoad2(state->builder, llvm_type, fn_state->global_array_aliases[reg], "loadtmp");
        if (out_type) *out_type = type;
        return true;
    }
    *out_value = LLVMBuildLoad2(state->builder, llvm_type, fn_state->locals[reg], "loadtmp");
    if (out_type) *out_type = type;
    return true;
}

static bool llvm_api_emit_address(LlvmApiState* state, LlvmApiFunctionState* fn_state, int reg,
                                  LLVMValueRef* out_addr, TypeExpr** out_type) {
    if (reg < 0 || (size_t)reg >= fn_state->local_count) {
        llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: invalid register %d", reg);
        return false;
    }
    if (fn_state->has_global_array_alias[reg]) {
        if (out_addr) *out_addr = fn_state->global_array_aliases[reg];
        if (out_type) *out_type = fn_state->func->locals[reg].type;
        return true;
    }
    if (out_addr) *out_addr = fn_state->locals[reg];
    if (out_type) *out_type = fn_state->func->locals[reg].type;
    return true;
}

static bool llvm_api_to_i1(LlvmApiState* state, TypeExpr* type, LLVMValueRef value, LLVMValueRef* out_value) {
    LLVMTypeRef llvm_type = llvm_api_type_ref(state, type);
    if (llvm_type == state->i1_type) {
        *out_value = value;
        return true;
    }
    if (llvm_type == state->f64_type) {
        *out_value = LLVMBuildFCmp(state->builder, LLVMRealONE, value, LLVMConstReal(state->f64_type, 0.0), "tobool");
        return true;
    }
    if (llvm_type == state->i64_type || llvm_type == state->i8_type) {
        *out_value = LLVMBuildICmp(state->builder, LLVMIntNE, value, LLVMConstNull(llvm_type), "tobool");
        return true;
    }
    llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: cannot coerce value to Bool");
    return false;
}

static LLVMValueRef llvm_api_cast_value(LlvmApiState* state, LLVMValueRef value,
                                        LLVMTypeRef from_type, LLVMTypeRef to_type) {
    if (from_type == to_type) return value;
    if ((from_type == state->i8_type || from_type == state->i1_type || from_type == state->i32_type || from_type == state->i64_type) &&
        (to_type == state->i8_type || to_type == state->i1_type || to_type == state->i32_type || to_type == state->i64_type)) {
        unsigned from_bits = LLVMGetIntTypeWidth(from_type);
        unsigned to_bits = LLVMGetIntTypeWidth(to_type);
        if (from_bits < to_bits) {
            if (from_type == state->i8_type || from_type == state->i1_type) {
                return LLVMBuildZExt(state->builder, value, to_type, "int.zext");
            }
            return LLVMBuildSExt(state->builder, value, to_type, "int.sext");
        }
        if (from_bits > to_bits) return LLVMBuildTrunc(state->builder, value, to_type, "int.trunc");
        return value;
    }
    if ((from_type == state->i8_type || from_type == state->i1_type || from_type == state->i32_type || from_type == state->i64_type) &&
        to_type == state->f64_type) {
        if (from_type == state->i8_type || from_type == state->i1_type) {
            return LLVMBuildUIToFP(state->builder, value, to_type, "uint.to.fp");
        }
        return LLVMBuildSIToFP(state->builder, value, to_type, "int.to.fp");
    }
    if (from_type == state->f64_type &&
        (to_type == state->i8_type || to_type == state->i1_type || to_type == state->i32_type || to_type == state->i64_type)) {
        if (to_type == state->i8_type || to_type == state->i1_type) {
            return LLVMBuildFPToUI(state->builder, value, to_type, "fp.to.uint");
        }
        return LLVMBuildFPToSI(state->builder, value, to_type, "fp.to.int");
    }
    return value;
}

static LLVMValueRef llvm_api_emit_slice_string_value(LlvmApiState* state, const char* text) {
    size_t len = llvm_api_decoded_string_length(text);
    char* cooked = malloc(len + 1);
    if (!cooked) return NULL;
    memset(cooked, 0, len + 1);
    llvm_api_decode_string_bytes(text, cooked);
    LLVMValueRef ptr = LLVMBuildGlobalStringPtr(state->builder, cooked, "strlit");
    free(cooked);
    LLVMValueRef slice = LLVMGetUndef(state->slice_u8_type);
    slice = LLVMBuildInsertValue(state->builder, slice, ptr, 0, "slice.ptr");
    slice = LLVMBuildInsertValue(state->builder, slice,
                                 LLVMConstInt(state->i64_type, (unsigned long long)len, false),
                                 1, "slice.len");
    return slice;
}

static bool llvm_api_emit_print(LlvmApiState* state, LlvmApiFunctionState* fn_state, JirInst* inst) {
    if (inst->call_arg_count == 0) {
        llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: print requires at least one argument");
        return false;
    }

    LLVMValueRef args[32];
    LLVMTypeRef arg_types[32];
    if (inst->call_arg_count > 32) {
        llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: too many print arguments");
        return false;
    }

    const char* fmt_const = llvm_api_const_string_payload(fn_state->func, inst->call_args[0]);
    if (fmt_const) {
        TypeExpr* fmt_type = NULL;
        LLVMValueRef fmt_value = NULL;
        if (!llvm_api_emit_load(state, fn_state, inst->call_args[0], &fmt_value, &fmt_type)) return false;
        args[0] = LLVMBuildExtractValue(state->builder, fmt_value, 0, "fmt.ptr");
        arg_types[0] = state->ptr_type;

        for (size_t i = 1; i < inst->call_arg_count; i++) {
            TypeExpr* arg_type = NULL;
            LLVMValueRef arg_value = NULL;
            if (!llvm_api_emit_load(state, fn_state, inst->call_args[i], &arg_value, &arg_type)) return false;
            if (llvm_api_is_u8_slice_type(arg_type)) {
                args[i] = LLVMBuildExtractValue(state->builder, arg_value, 0, "print.arg.ptr");
                arg_types[i] = state->ptr_type;
            } else {
                args[i] = arg_value;
                arg_types[i] = llvm_api_type_ref(state, arg_type);
            }
        }
        LLVMTypeRef printf_param_types[1] = { state->ptr_type };
        LLVMTypeRef printf_type = LLVMFunctionType(state->i32_type, printf_param_types, 1, true);
        LLVMBuildCall2(state->builder, printf_type, state->printf_func, args, (unsigned)inst->call_arg_count, "");
        return true;
    }

    if (inst->call_arg_count == 1) {
        TypeExpr* arg_type = NULL;
        LLVMValueRef arg_value = NULL;
        if (!llvm_api_emit_load(state, fn_state, inst->call_args[0], &arg_value, &arg_type)) return false;
        if (!llvm_api_is_u8_slice_type(arg_type)) {
            llvm_api_error(state->error_buf, state->error_buf_size,
                           "LLVM C API: single-arg print expects UInt8[]");
            return false;
        }
        args[0] = LLVMBuildGlobalStringPtr(state->builder, "%s", "print.fmt");
        args[1] = LLVMBuildExtractValue(state->builder, arg_value, 0, "print.ptr");
        LLVMTypeRef printf_param_types[1] = { state->ptr_type };
        LLVMTypeRef printf_type = LLVMFunctionType(state->i32_type, printf_param_types, 1, true);
        LLVMBuildCall2(state->builder, printf_type, state->printf_func, args, 2, "");
        return true;
    }

    llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: unsupported print call shape");
    return false;
}

static bool llvm_api_emit_assert(LlvmApiState* state, LlvmApiFunctionState* fn_state, JirInst* inst) {
    LLVMValueRef cond = LLVMConstInt(state->i1_type, 0, false);
    if (inst->call_arg_count > 0) {
        TypeExpr* cond_type = NULL;
        LLVMValueRef cond_value = NULL;
        if (!llvm_api_emit_load(state, fn_state, inst->call_args[0], &cond_value, &cond_type)) return false;
        if (!llvm_api_to_i1(state, cond_type, cond_value, &cond)) return false;
    }

    LLVMBasicBlockRef ok_block = LLVMAppendBasicBlockInContext(state->context, fn_state->llvm_func, "assert.ok");
    LLVMBasicBlockRef fail_block = LLVMAppendBasicBlockInContext(state->context, fn_state->llvm_func, "assert.fail");
    LLVMBuildCondBr(state->builder, cond, ok_block, fail_block);

    LLVMPositionBuilderAtEnd(state->builder, fail_block);
    LLVMTypeRef abort_type = LLVMFunctionType(state->void_type, NULL, 0, false);
    LLVMBuildCall2(state->builder, abort_type, state->abort_func, NULL, 0, "");
    LLVMBuildUnreachable(state->builder);

    LLVMPositionBuilderAtEnd(state->builder, ok_block);
    fn_state->current_block = ok_block;
    return true;
}

static void llvm_api_build_runtime_helpers(LlvmApiState* state) {
    LLVMTypeRef fopen_params[2] = { state->ptr_type, state->ptr_type };
    state->fopen_func = LLVMAddFunction(state->module, "fopen",
                                        LLVMFunctionType(state->ptr_type, fopen_params, 2, false));
    LLVMTypeRef fclose_params[1] = { state->ptr_type };
    state->fclose_func = LLVMAddFunction(state->module, "fclose",
                                         LLVMFunctionType(state->i32_type, fclose_params, 1, false));
    LLVMTypeRef fseek_params[3] = { state->ptr_type, state->i64_type, state->i32_type };
    state->fseek_func = LLVMAddFunction(state->module, "fseek",
                                        LLVMFunctionType(state->i32_type, fseek_params, 3, false));
    LLVMTypeRef ftell_params[1] = { state->ptr_type };
    state->ftell_func = LLVMAddFunction(state->module, "ftell",
                                        LLVMFunctionType(state->i64_type, ftell_params, 1, false));
    LLVMTypeRef rewind_params[1] = { state->ptr_type };
    state->rewind_func = LLVMAddFunction(state->module, "rewind",
                                         LLVMFunctionType(state->void_type, rewind_params, 1, false));
    LLVMTypeRef fread_params[4] = { state->ptr_type, state->i64_type, state->i64_type, state->ptr_type };
    state->fread_func = LLVMAddFunction(state->module, "fread",
                                        LLVMFunctionType(state->i64_type, fread_params, 4, false));
    LLVMTypeRef malloc_params[1] = { state->i64_type };
    state->malloc_func = LLVMAddFunction(state->module, "malloc",
                                         LLVMFunctionType(state->ptr_type, malloc_params, 1, false));

    LLVMTypeRef rt_exists_params[1] = { state->ptr_type };
    state->rt_file_exists_func = LLVMAddFunction(
        state->module,
        "__jiang_rt_file_exists",
        LLVMFunctionType(state->i1_type, rt_exists_params, 1, false));

    LLVMTypeRef rt_read_params[1] = { state->ptr_type };
    state->rt_read_file_func = LLVMAddFunction(
        state->module,
        "__jiang_rt_read_file",
        LLVMFunctionType(state->slice_u8_type, rt_read_params, 1, false));

    LLVMValueRef fn = state->rt_file_exists_func;
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(state->context, fn, "entry");
    LLVMBasicBlockRef missing = LLVMAppendBasicBlockInContext(state->context, fn, "missing");
    LLVMBasicBlockRef found = LLVMAppendBasicBlockInContext(state->context, fn, "found");
    LLVMPositionBuilderAtEnd(state->builder, entry);
    LLVMValueRef mode = LLVMBuildGlobalStringPtr(state->builder, "rb", "rt.exists.mode");
    LLVMValueRef open_args[2] = { LLVMGetParam(fn, 0), mode };
    LLVMValueRef file = LLVMBuildCall2(
        state->builder,
        LLVMGlobalGetValueType(state->fopen_func),
        state->fopen_func,
        open_args,
        2,
        "file");
    LLVMValueRef is_null = LLVMBuildICmp(state->builder, LLVMIntEQ, file, LLVMConstNull(state->ptr_type), "isnull");
    LLVMBuildCondBr(state->builder, is_null, missing, found);

    LLVMPositionBuilderAtEnd(state->builder, missing);
    LLVMBuildRet(state->builder, LLVMConstInt(state->i1_type, 0, false));

    LLVMPositionBuilderAtEnd(state->builder, found);
    LLVMValueRef close_args[1] = { file };
    LLVMBuildCall2(state->builder, LLVMGlobalGetValueType(state->fclose_func), state->fclose_func, close_args, 1, "");
    LLVMBuildRet(state->builder, LLVMConstInt(state->i1_type, 1, false));

    fn = state->rt_read_file_func;
    entry = LLVMAppendBasicBlockInContext(state->context, fn, "entry");
    LLVMBasicBlockRef open_failed = LLVMAppendBasicBlockInContext(state->context, fn, "open_failed");
    LLVMBasicBlockRef size_failed = LLVMAppendBasicBlockInContext(state->context, fn, "size_failed");
    LLVMBasicBlockRef alloc_failed = LLVMAppendBasicBlockInContext(state->context, fn, "alloc_failed");
    LLVMBasicBlockRef success = LLVMAppendBasicBlockInContext(state->context, fn, "success");
    LLVMPositionBuilderAtEnd(state->builder, entry);
    mode = LLVMBuildGlobalStringPtr(state->builder, "rb", "rt.read.mode");
    LLVMValueRef read_open_args[2] = { LLVMGetParam(fn, 0), mode };
    file = LLVMBuildCall2(
        state->builder,
        LLVMGlobalGetValueType(state->fopen_func),
        state->fopen_func,
        read_open_args,
        2,
        "file");
    is_null = LLVMBuildICmp(state->builder, LLVMIntEQ, file, LLVMConstNull(state->ptr_type), "isnull");
    LLVMBuildCondBr(state->builder, is_null, open_failed, size_failed);

    LLVMPositionBuilderAtEnd(state->builder, open_failed);
    LLVMBuildRet(state->builder, LLVMConstNull(state->slice_u8_type));

    LLVMPositionBuilderAtEnd(state->builder, size_failed);
    LLVMValueRef fseek_args[3] = {
        file,
        LLVMConstInt(state->i64_type, 0, false),
        LLVMConstInt(state->i32_type, 2, false),
    };
    LLVMBuildCall2(state->builder, LLVMGlobalGetValueType(state->fseek_func), state->fseek_func, fseek_args, 3, "");
    LLVMValueRef ftell_args[1] = { file };
    LLVMValueRef size = LLVMBuildCall2(
        state->builder,
        LLVMGlobalGetValueType(state->ftell_func),
        state->ftell_func,
        ftell_args,
        1,
        "size");
    LLVMValueRef size_negative =
        LLVMBuildICmp(state->builder, LLVMIntSLT, size, LLVMConstInt(state->i64_type, 0, true), "size_negative");
    LLVMBasicBlockRef rewind_block = LLVMAppendBasicBlockInContext(state->context, fn, "rewind");
    LLVMBasicBlockRef close_size_failed = LLVMAppendBasicBlockInContext(state->context, fn, "close_size_failed");
    LLVMBuildCondBr(state->builder, size_negative, close_size_failed, rewind_block);

    LLVMPositionBuilderAtEnd(state->builder, close_size_failed);
    close_args[0] = file;
    LLVMBuildCall2(state->builder, LLVMGlobalGetValueType(state->fclose_func), state->fclose_func, close_args, 1, "");
    LLVMBuildRet(state->builder, LLVMConstNull(state->slice_u8_type));

    LLVMPositionBuilderAtEnd(state->builder, rewind_block);
    LLVMValueRef rewind_args[1] = { file };
    LLVMBuildCall2(state->builder, LLVMGlobalGetValueType(state->rewind_func), state->rewind_func, rewind_args, 1, "");
    LLVMValueRef alloc_size = LLVMBuildAdd(state->builder, size, LLVMConstInt(state->i64_type, 1, false), "alloc_size");
    LLVMValueRef malloc_args[1] = { alloc_size };
    LLVMValueRef buffer = LLVMBuildCall2(
        state->builder,
        LLVMGlobalGetValueType(state->malloc_func),
        state->malloc_func,
        malloc_args,
        1,
        "buffer");
    is_null = LLVMBuildICmp(state->builder, LLVMIntEQ, buffer, LLVMConstNull(state->ptr_type), "alloc_isnull");
    LLVMBuildCondBr(state->builder, is_null, alloc_failed, success);

    LLVMPositionBuilderAtEnd(state->builder, alloc_failed);
    close_args[0] = file;
    LLVMBuildCall2(state->builder, LLVMGlobalGetValueType(state->fclose_func), state->fclose_func, close_args, 1, "");
    LLVMBuildRet(state->builder, LLVMConstNull(state->slice_u8_type));

    LLVMPositionBuilderAtEnd(state->builder, success);
    LLVMValueRef fread_args[4] = {
        buffer,
        LLVMConstInt(state->i64_type, 1, false),
        size,
        file,
    };
    LLVMValueRef read_size = LLVMBuildCall2(
        state->builder,
        LLVMGlobalGetValueType(state->fread_func),
        state->fread_func,
        fread_args,
        4,
        "read_size");
    close_args[0] = file;
    LLVMBuildCall2(state->builder, LLVMGlobalGetValueType(state->fclose_func), state->fclose_func, close_args, 1, "");
    LLVMValueRef tail_ptr =
        LLVMBuildGEP2(state->builder, state->i8_type, buffer, &read_size, 1, "tail_ptr");
    LLVMBuildStore(state->builder, LLVMConstInt(state->i8_type, 0, false), tail_ptr);
    LLVMValueRef slice = LLVMGetUndef(state->slice_u8_type);
    slice = LLVMBuildInsertValue(state->builder, slice, buffer, 0, "slice.ptr");
    slice = LLVMBuildInsertValue(state->builder, slice, read_size, 1, "slice.len");
    LLVMBuildRet(state->builder, slice);
}

static void llvm_api_declare_named_types(LlvmApiState* state) {
    for (size_t i = 0; i < state->info_count; i++) {
        CodegenModuleInfo* info = state->infos ? state->infos[i] : NULL;
        if (!info) continue;
        for (size_t j = 0; j < info->struct_count; j++) {
            if (llvm_api_find_cached_named_type(state->named_structs, state->named_struct_count,
                                                info->structs[j].name)) {
                continue;
            }
            char name_buf[128];
            snprintf(name_buf, sizeof(name_buf), "%.*s", (int)info->structs[j].name.length, info->structs[j].name.start);
            LLVMTypeRef type = LLVMStructCreateNamed(state->context, name_buf);
            llvm_api_add_cached_named_type(state->named_structs, &state->named_struct_count, info->structs[j].name, type);
        }
        for (size_t j = 0; j < info->union_count; j++) {
            if (llvm_api_find_cached_named_type(state->named_unions, state->named_union_count,
                                                info->unions[j].name)) {
                continue;
            }
            char name_buf[128];
            snprintf(name_buf, sizeof(name_buf), "%.*s", (int)info->unions[j].name.length, info->unions[j].name.start);
            LLVMTypeRef type = LLVMStructCreateNamed(state->context, name_buf);
            llvm_api_add_cached_named_type(state->named_unions, &state->named_union_count, info->unions[j].name, type);
        }
    }
}

static void llvm_api_define_named_types(LlvmApiState* state) {
    for (size_t i = 0; i < state->info_count; i++) {
        CodegenModuleInfo* info = state->infos ? state->infos[i] : NULL;
        if (!info) continue;
        for (size_t j = 0; j < info->struct_count; j++) {
            LLVMTypeRef type = llvm_api_find_cached_named_type(state->named_structs, state->named_struct_count,
                                                               info->structs[j].name);
            if (!type || LLVMCountStructElementTypes(type) > 0) continue;
            LLVMTypeRef fields[64];
            unsigned field_count = 0;
            for (size_t k = 0; k < info->structs[j].field_count && k < 64; k++) {
                fields[field_count++] = llvm_api_type_ref(state, info->structs[j].fields[k].type);
            }
            LLVMStructSetBody(type, fields, field_count, false);
        }
        for (size_t j = 0; j < info->union_count; j++) {
            LLVMTypeRef type = llvm_api_find_cached_named_type(state->named_unions, state->named_union_count,
                                                               info->unions[j].name);
            if (!type || LLVMCountStructElementTypes(type) > 0) continue;
            LLVMTypeRef fields[64];
            unsigned field_count = 0;
            fields[field_count++] = info->unions[j].tag_enum.length > 0 &&
                                    llvm_api_find_enum_info(state, info->unions[j].tag_enum)
                                        ? state->i64_type : state->i64_type;
            for (size_t k = 0; k < info->unions[j].variant_count && k < 63; k++) {
                fields[field_count++] = llvm_api_type_ref(state, info->unions[j].variants[k].type);
            }
            LLVMStructSetBody(type, fields, field_count, false);
        }
    }
}

static void llvm_api_collect_reachable(JirModule* mod, size_t func_index, bool* reachable) {
    if (!mod || func_index >= mod->function_count || reachable[func_index]) return;
    reachable[func_index] = true;
    JirFunction* func = mod->functions[func_index];
    for (size_t i = 0; i < func->inst_count; i++) {
        JirInst* inst = &func->insts[i];
        if (inst->op != JIR_OP_CALL) continue;
        for (size_t callee_i = 0; callee_i < mod->function_count; callee_i++) {
            JirFunction* callee = mod->functions[callee_i];
            if ((size_t)callee->name.length == inst->payload.name.length &&
                strncmp(callee->name.start, inst->payload.name.start, inst->payload.name.length) == 0) {
                llvm_api_collect_reachable(mod, callee_i, reachable);
                break;
            }
        }
        if (inst->payload.name.length > 4 &&
            strncmp(inst->payload.name.start + inst->payload.name.length - 4, "_new", 4) == 0) {
            char init_name[128];
            snprintf(init_name, sizeof(init_name), "%.*s_init",
                     (int)(inst->payload.name.length - 4), inst->payload.name.start);
            for (size_t callee_i = 0; callee_i < mod->function_count; callee_i++) {
                JirFunction* callee = mod->functions[callee_i];
                if (strlen(init_name) == (size_t)callee->name.length &&
                    strncmp(callee->name.start, init_name, callee->name.length) == 0) {
                    llvm_api_collect_reachable(mod, callee_i, reachable);
                    break;
                }
            }
        }
    }
}

static bool llvm_api_declare_functions(LlvmApiState* state, bool* reachable, size_t entry_index) {
    for (size_t i = 0; i < state->jir->function_count; i++) {
        if (!reachable[i]) continue;
        JirFunction* func = state->jir->functions[i];
        bool is_main = (i == entry_index);

        LLVMTypeRef ret_type = is_main ? state->i32_type : llvm_api_type_ref(state, func->return_type);
        if (!ret_type) {
            llvm_api_error(state->error_buf, state->error_buf_size,
                           "LLVM C API: unsupported return type for '%.*s'",
                           (int)func->name.length, func->name.start);
            return false;
        }

        LLVMTypeRef param_types[128];
        unsigned param_count = 0;
        if (!is_main) {
            for (size_t j = 0; j < func->local_count; j++) {
                if (!func->locals[j].is_param) continue;
                LLVMTypeRef param_type = llvm_api_type_ref(state, func->locals[j].type);
                if (!param_type || param_type == state->void_type) {
                    llvm_api_error(state->error_buf, state->error_buf_size,
                                   "LLVM C API: unsupported parameter type for '%s'", func->locals[j].name);
                    return false;
                }
                param_types[param_count++] = param_type;
            }
        }

        LLVMTypeRef fn_type = LLVMFunctionType(ret_type, param_types, param_count, false);
        char name_buf[128];
        if (is_main) {
            snprintf(name_buf, sizeof(name_buf), "main");
        } else {
            snprintf(name_buf, sizeof(name_buf), "%.*s", (int)func->name.length, func->name.start);
        }
        LLVMAddFunction(state->module, name_buf, fn_type);
    }
    return true;
}

static int llvm_api_find_struct_field_index(LlvmApiState* state, Token type_name, Token field_name) {
    CodegenStructInfo* info = llvm_api_find_struct_info(state, type_name);
    if (!info) return -1;
    for (size_t i = 0; i < info->field_count; i++) {
        if (llvm_api_token_name_eq(info->fields[i].name, field_name)) return (int)i;
    }
    return -1;
}

static TypeExpr* llvm_api_find_struct_field_type(LlvmApiState* state, Token type_name, Token field_name) {
    CodegenStructInfo* info = llvm_api_find_struct_info(state, type_name);
    if (!info) return NULL;
    for (size_t i = 0; i < info->field_count; i++) {
        if (llvm_api_token_name_eq(info->fields[i].name, field_name)) return info->fields[i].type;
    }
    return NULL;
}

static int llvm_api_find_union_variant_index(LlvmApiState* state, Token type_name, Token field_name) {
    CodegenUnionInfo* info = llvm_api_find_union_info(state, type_name);
    if (!info) return -1;
    for (size_t i = 0; i < info->variant_count; i++) {
        if (llvm_api_token_name_eq(info->variants[i].name, field_name)) return (int)i;
    }
    return -1;
}

static void llvm_api_declare_globals(LlvmApiState* state) {
    for (size_t i = 0; i < state->info_count; i++) {
        CodegenModuleInfo* info = state->infos ? state->infos[i] : NULL;
        if (!info) continue;
        for (size_t j = 0; j < info->global_count; j++) {
            CodegenGlobalInfo* g = &info->globals[j];
            char name_buf[128];
            snprintf(name_buf, sizeof(name_buf), "%.*s", (int)g->name.length, g->name.start);
            if (LLVMGetNamedGlobal(state->module, name_buf)) continue;
            LLVMTypeRef global_type = llvm_api_type_ref(state, g->type);
            if (!global_type) continue;
            LLVMValueRef global = LLVMAddGlobal(state->module, global_type, name_buf);
            LLVMSetLinkage(global, LLVMExternalLinkage);
            switch (g->init_kind) {
                case CG_GLOBAL_INIT_INT:
                    LLVMSetInitializer(global, LLVMConstInt(global_type, (unsigned long long)g->int_value, true));
                    break;
                case CG_GLOBAL_INIT_FLOAT:
                    LLVMSetInitializer(global, LLVMConstReal(global_type, g->float_value));
                    break;
                case CG_GLOBAL_INIT_ENUM: {
                    bool found = false;
                    int64_t value = llvm_api_find_enum_constant_value(
                        state,
                        g->enum_name.length > 0
                            ? (Token){ TOKEN_IDENTIFIER, "", 0, 0 }
                            : g->enum_member,
                        &found);
                    if (!found && g->enum_name.length > 0) {
                        char enum_const_buf[256];
                        snprintf(enum_const_buf, sizeof(enum_const_buf), "%.*s_%.*s",
                                 (int)g->enum_name.length, g->enum_name.start,
                                 (int)g->enum_member.length, g->enum_member.start);
                        Token combined = { TOKEN_IDENTIFIER, enum_const_buf, (int)strlen(enum_const_buf), 0 };
                        value = llvm_api_find_enum_constant_value(state, combined, &found);
                    }
                    LLVMSetInitializer(global, LLVMConstInt(global_type, (unsigned long long)value, true));
                    break;
                }
                case CG_GLOBAL_INIT_STRING: {
                    size_t len = llvm_api_decoded_string_length(g->string_value.start);
                    LLVMValueRef ptr = LLVMConstPointerNull(state->ptr_type);
                    if (g->string_value.start) {
                        char* cooked = malloc(len + 1);
                        memset(cooked, 0, len + 1);
                        llvm_api_decode_string_bytes(g->string_value.start, cooked);
                        LLVMValueRef str = LLVMConstStringInContext(state->context, cooked, (unsigned)len, true);
                        char str_name[160];
                        snprintf(str_name, sizeof(str_name), "__jiang_global_str_%s", name_buf);
                        LLVMValueRef str_global = LLVMAddGlobal(state->module, LLVMTypeOf(str), str_name);
                        LLVMSetInitializer(str_global, str);
                        LLVMSetGlobalConstant(str_global, true);
                        LLVMSetLinkage(str_global, LLVMPrivateLinkage);
                        LLVMValueRef zero = LLVMConstInt(state->i64_type, 0, false);
                        LLVMValueRef idxs[2] = { zero, zero };
                        ptr = LLVMConstGEP2(LLVMTypeOf(str), str_global, idxs, 2);
                        free(cooked);
                    }
                    LLVMValueRef slice = LLVMConstNamedStruct(global_type,
                                                              (LLVMValueRef[]){ ptr, LLVMConstInt(state->i64_type, (unsigned long long)len, false) },
                                                              2);
                    LLVMSetInitializer(global, slice);
                    break;
                }
                default:
                    LLVMSetInitializer(global, LLVMConstNull(global_type));
                    break;
            }
        }
    }
}

static void llvm_api_declare_wrapper_functions(LlvmApiState* state) {
    for (size_t i = 0; i < state->info_count; i++) {
        CodegenModuleInfo* info = state->infos ? state->infos[i] : NULL;
        if (!info) continue;
        for (size_t j = 0; j < info->struct_count; j++) {
            CodegenStructInfo* s = &info->structs[j];
            if (!s->has_init) continue;
            char wrapper_name[128];
            snprintf(wrapper_name, sizeof(wrapper_name), "%.*s_new", (int)s->name.length, s->name.start);
            if (LLVMGetNamedFunction(state->module, wrapper_name)) continue;
            LLVMTypeRef params[32];
            unsigned param_count = 0;
            for (size_t k = 0; k < s->init_param_count && k < 32; k++) {
                params[param_count++] = llvm_api_type_ref(state, s->init_params[k].type);
            }
            LLVMAddFunction(state->module, wrapper_name,
                            LLVMFunctionType(llvm_api_type_ref(state, &(TypeExpr){ .kind = TYPE_BASE, .as.base_type = s->name }),
                                             params, param_count, false));
        }
        for (size_t j = 0; j < info->union_count; j++) {
            CodegenUnionInfo* u = &info->unions[j];
            for (size_t k = 0; k < u->variant_count; k++) {
                char ctor_name[128];
                snprintf(ctor_name, sizeof(ctor_name), "%.*s_%.*s",
                         (int)u->name.length, u->name.start,
                         (int)u->variants[k].name.length, u->variants[k].name.start);
                if (LLVMGetNamedFunction(state->module, ctor_name)) continue;
                LLVMTypeRef params[32];
                unsigned param_count = 0;
                TypeExpr* variant_type = u->variants[k].type;
                variant_type = llvm_api_unwrap_type(variant_type);
                if (variant_type && variant_type->kind == TYPE_TUPLE) {
                    for (size_t m = 0; m < variant_type->as.tuple.count && m < 32; m++) {
                        params[param_count++] = llvm_api_type_ref(state, variant_type->as.tuple.elements[m]);
                    }
                } else if (!llvm_api_is_void_type(variant_type)) {
                    params[param_count++] = llvm_api_type_ref(state, variant_type);
                }
                LLVMTypeRef union_type = llvm_api_type_ref(state, &(TypeExpr){ .kind = TYPE_BASE, .as.base_type = u->name });
                LLVMAddFunction(state->module, ctor_name, LLVMFunctionType(union_type, params, param_count, false));
            }
        }
    }
}

static bool llvm_api_emit_wrapper_functions(LlvmApiState* state) {
    for (size_t i = 0; i < state->info_count; i++) {
        CodegenModuleInfo* info = state->infos ? state->infos[i] : NULL;
        if (!info) continue;
        for (size_t j = 0; j < info->struct_count; j++) {
            CodegenStructInfo* s = &info->structs[j];
            if (!s->has_init) continue;
            char wrapper_name[128];
            char init_name[128];
            snprintf(wrapper_name, sizeof(wrapper_name), "%.*s_new", (int)s->name.length, s->name.start);
            snprintf(init_name, sizeof(init_name), "%.*s_init", (int)s->name.length, s->name.start);
            LLVMValueRef wrapper = LLVMGetNamedFunction(state->module, wrapper_name);
            LLVMValueRef init_fn = LLVMGetNamedFunction(state->module, init_name);
            if (!wrapper || !init_fn || LLVMCountBasicBlocks(wrapper) > 0) continue;

            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(state->context, wrapper, "entry");
            LLVMPositionBuilderAtEnd(state->builder, entry);
            TypeExpr struct_expr = { .kind = TYPE_BASE, .as.base_type = s->name };
            LLVMTypeRef struct_type = llvm_api_type_ref(state, &struct_expr);
            LLVMValueRef self_ptr = LLVMBuildAlloca(state->builder, struct_type, "self");
            LLVMValueRef args[33];
            args[0] = self_ptr;
            for (size_t k = 0; k < s->init_param_count && k < 32; k++) {
                args[k + 1] = LLVMGetParam(wrapper, (unsigned)k);
            }
            LLVMBuildCall2(state->builder, LLVMGlobalGetValueType(init_fn), init_fn, args,
                           (unsigned)(s->init_param_count + 1), "");
            LLVMValueRef result = LLVMBuildLoad2(state->builder, struct_type, self_ptr, "result");
            LLVMBuildRet(state->builder, result);
        }

        for (size_t j = 0; j < info->union_count; j++) {
            CodegenUnionInfo* u = &info->unions[j];
            TypeExpr union_expr = { .kind = TYPE_BASE, .as.base_type = u->name };
            LLVMTypeRef union_type = llvm_api_type_ref(state, &union_expr);
            for (size_t k = 0; k < u->variant_count; k++) {
                char ctor_name[128];
                snprintf(ctor_name, sizeof(ctor_name), "%.*s_%.*s",
                         (int)u->name.length, u->name.start,
                         (int)u->variants[k].name.length, u->variants[k].name.start);
                LLVMValueRef ctor = LLVMGetNamedFunction(state->module, ctor_name);
                if (!ctor || LLVMCountBasicBlocks(ctor) > 0) continue;

                LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(state->context, ctor, "entry");
                LLVMPositionBuilderAtEnd(state->builder, entry);
                LLVMValueRef value = LLVMGetUndef(union_type);

                char enum_const_name[256];
                if (u->tag_enum.length > 0) {
                    snprintf(enum_const_name, sizeof(enum_const_name), "%.*s_%.*s",
                             (int)u->tag_enum.length, u->tag_enum.start,
                             (int)u->variants[k].name.length, u->variants[k].name.start);
                } else {
                    snprintf(enum_const_name, sizeof(enum_const_name), "%.*sTag_%.*s",
                             (int)u->name.length, u->name.start,
                             (int)u->variants[k].name.length, u->variants[k].name.start);
                }
                Token enum_tok = { TOKEN_IDENTIFIER, enum_const_name, (int)strlen(enum_const_name), 0 };
                bool found = false;
                int64_t tag_value = llvm_api_find_enum_constant_value(state, enum_tok, &found);
                value = LLVMBuildInsertValue(state->builder, value,
                                             LLVMConstInt(state->i64_type, (unsigned long long)tag_value, false),
                                             0, "union.tag");

                TypeExpr* variant_type = llvm_api_unwrap_type(u->variants[k].type);
                if (!llvm_api_is_void_type(variant_type)) {
                    LLVMValueRef payload = NULL;
                    if (variant_type && variant_type->kind == TYPE_TUPLE) {
                        LLVMTypeRef tuple_type = llvm_api_type_ref(state, variant_type);
                        payload = LLVMGetUndef(tuple_type);
                        for (size_t m = 0; m < variant_type->as.tuple.count; m++) {
                            payload = LLVMBuildInsertValue(state->builder, payload, LLVMGetParam(ctor, (unsigned)m),
                                                           (unsigned)m, "tuple.arg");
                        }
                    } else {
                        payload = LLVMGetParam(ctor, 0);
                    }
                    value = LLVMBuildInsertValue(state->builder, value, payload, (unsigned)(k + 1), "union.payload");
                }

                LLVMBuildRet(state->builder, value);
            }
        }
    }
    return true;
}

static bool llvm_api_emit_function(LlvmApiState* state, JirFunction* func, bool is_main) {
    char name_buf[128];
    snprintf(name_buf, sizeof(name_buf), "%s", is_main ? "main" : "");
    LLVMValueRef llvm_func = is_main ? LLVMGetNamedFunction(state->module, "main") : NULL;
    if (!llvm_func) {
        snprintf(name_buf, sizeof(name_buf), "%.*s", (int)func->name.length, func->name.start);
        llvm_func = LLVMGetNamedFunction(state->module, name_buf);
    }
    if (!llvm_func) {
        llvm_api_error(state->error_buf, state->error_buf_size,
                       "LLVM C API: missing declared function '%.*s'",
                       (int)func->name.length, func->name.start);
        return false;
    }

    LlvmApiFunctionState fn_state;
    memset(&fn_state, 0, sizeof(fn_state));
    fn_state.func = func;
    fn_state.llvm_func = llvm_func;
    fn_state.local_count = func->local_count;
    if (fn_state.local_count > 512) {
        llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: too many locals");
        return false;
    }

    fn_state.entry_block = LLVMAppendBasicBlockInContext(state->context, llvm_func, "entry");
    fn_state.current_block = fn_state.entry_block;
    LLVMPositionBuilderAtEnd(state->builder, fn_state.entry_block);


    for (size_t i = 0; i < func->inst_count; i++) {
        JirInst* inst = &func->insts[i];
        if (inst->op == JIR_OP_LABEL && inst->payload.label_id < 512) {
            char block_name[32];
            snprintf(block_name, sizeof(block_name), "L%u", inst->payload.label_id);
            fn_state.label_blocks[inst->payload.label_id] =
                LLVMAppendBasicBlockInContext(state->context, llvm_func, block_name);
            fn_state.has_label_block[inst->payload.label_id] = true;
        } else if (inst->op == JIR_OP_JUMP_IF_FALSE && i < 512) {
            char block_name[32];
            snprintf(block_name, sizeof(block_name), "then_%zu", i);
            fn_state.then_blocks[i] = LLVMAppendBasicBlockInContext(state->context, llvm_func, block_name);
        }
    }

    for (size_t i = 0; i < func->local_count; i++) {
        TypeExpr* local_type = llvm_api_unwrap_type(func->locals[i].type);
        if (!local_type || local_type->kind != TYPE_ARRAY) continue;
        int def_idx = llvm_api_find_def_inst_index(func, (int)i);
        if (def_idx < 0) continue;
        JirInst* def = &func->insts[def_idx];
        if (def->op != JIR_OP_LOAD_SYM || !def->payload.name.start || def->payload.name.length <= 0) continue;
        LLVMValueRef global = llvm_api_find_named_global(state, def->payload.name);
        if (!global) continue;
        fn_state.global_array_aliases[i] = global;
        fn_state.has_global_array_alias[i] = true;
    }

    for (size_t i = 0; i < func->local_count; i++) {
        if (fn_state.has_global_array_alias[i]) continue;
        LLVMTypeRef local_type = llvm_api_type_ref(state, func->locals[i].type);
        if (!local_type || local_type == state->void_type) {
            llvm_api_error(state->error_buf, state->error_buf_size,
                           "LLVM C API: unsupported local type for '%s'", func->locals[i].name);
            return false;
        }
        fn_state.locals[i] = LLVMBuildAlloca(state->builder, local_type, func->locals[i].name);
    }

    unsigned param_index = 0;
    for (size_t i = 0; i < func->local_count; i++) {
        if (!func->locals[i].is_param) continue;
        LLVMBuildStore(state->builder, LLVMGetParam(llvm_func, param_index++), fn_state.locals[i]);
    }

    for (size_t i = 0; i < func->inst_count; i++) {
        JirInst* inst = &func->insts[i];
        if (LLVMGetBasicBlockTerminator(fn_state.current_block) != NULL && inst->op != JIR_OP_LABEL) {
            continue;
        }

        switch (inst->op) {
            case JIR_OP_LABEL:
                if (inst->payload.label_id >= 512 || !fn_state.has_label_block[inst->payload.label_id]) {
                    llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: unknown label");
                    return false;
                }
                if (LLVMGetBasicBlockTerminator(fn_state.current_block) == NULL &&
                    fn_state.current_block != fn_state.label_blocks[inst->payload.label_id]) {
                    LLVMBuildBr(state->builder, fn_state.label_blocks[inst->payload.label_id]);
                }
                LLVMPositionBuilderAtEnd(state->builder, fn_state.label_blocks[inst->payload.label_id]);
                fn_state.current_block = fn_state.label_blocks[inst->payload.label_id];
                break;

            case JIR_OP_CONST_INT: {
                LLVMTypeRef dest_type = llvm_api_type_ref(state, func->locals[inst->dest].type);
                if (!dest_type) {
                    llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: unsupported const int type");
                    return false;
                }
                LLVMBuildStore(state->builder,
                               LLVMConstInt(dest_type, (unsigned long long)inst->payload.int_val, true),
                               fn_state.locals[inst->dest]);
                break;
            }

            case JIR_OP_CONST_FLOAT: {
                LLVMTypeRef dest_type = llvm_api_type_ref(state, func->locals[inst->dest].type);
                if (dest_type != state->f64_type) {
                    llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: unsupported const float type");
                    return false;
                }
                LLVMBuildStore(state->builder, LLVMConstReal(dest_type, inst->payload.float_val),
                               fn_state.locals[inst->dest]);
                break;
            }

            case JIR_OP_CONST_STR: {
                if (!llvm_api_is_u8_slice_type(func->locals[inst->dest].type)) {
                    llvm_api_error(state->error_buf, state->error_buf_size,
                                   "LLVM C API: string literals currently only support UInt8[]");
                    return false;
                }
                LLVMValueRef slice = llvm_api_emit_slice_string_value(state, inst->payload.str_val ? inst->payload.str_val : "");
                if (!slice) {
                    llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: failed to build string literal");
                    return false;
                }
                LLVMBuildStore(state->builder, slice, fn_state.locals[inst->dest]);
                break;
            }

            case JIR_OP_LOAD_SYM: {
                bool found_enum = false;
                int64_t enum_value = llvm_api_find_enum_constant_value(state, inst->payload.name, &found_enum);
                if (found_enum) {
                    LLVMTypeRef dest_type = llvm_api_type_ref(state, func->locals[inst->dest].type);
                    LLVMBuildStore(state->builder,
                                   LLVMConstInt(dest_type ? dest_type : state->i64_type,
                                                (unsigned long long)enum_value, true),
                                   fn_state.locals[inst->dest]);
                    break;
                }
                if (inst->dest >= 0 && (size_t)inst->dest < fn_state.local_count &&
                    fn_state.has_global_array_alias[inst->dest]) {
                    break;
                }
                char sym_name[128];
                snprintf(sym_name, sizeof(sym_name), "%.*s", (int)inst->payload.name.length, inst->payload.name.start);
                LLVMValueRef global = LLVMGetNamedGlobal(state->module, sym_name);
                if (!global) {
                    llvm_api_error(state->error_buf, state->error_buf_size,
                                   "LLVM C API: unresolved symbol '%.*s'",
                                   (int)inst->payload.name.length, inst->payload.name.start);
                    return false;
                }
                LLVMTypeRef global_type = LLVMGlobalGetValueType(global);
                LLVMValueRef value = LLVMBuildLoad2(state->builder, global_type, global, "loadsym");
                LLVMBuildStore(state->builder, value, fn_state.locals[inst->dest]);
                break;
            }

            case JIR_OP_STORE: {
                TypeExpr* src_type = NULL;
                LLVMValueRef src_value = NULL;
                if (!llvm_api_emit_load(state, &fn_state, inst->src1, &src_value, &src_type)) return false;
                LLVMBuildStore(state->builder, src_value, fn_state.locals[inst->dest]);
                break;
            }

            case JIR_OP_STORE_SYM: {
                char sym_name[128];
                snprintf(sym_name, sizeof(sym_name), "%.*s", (int)inst->payload.name.length, inst->payload.name.start);
                LLVMValueRef global = LLVMGetNamedGlobal(state->module, sym_name);
                TypeExpr* src_type = NULL;
                LLVMValueRef src_value = NULL;
                if (!global) {
                    llvm_api_error(state->error_buf, state->error_buf_size,
                                   "LLVM C API: unresolved symbol '%s'", sym_name);
                    return false;
                }
                if (!llvm_api_emit_load(state, &fn_state, inst->src1, &src_value, &src_type)) return false;
                LLVMTypeRef global_type = LLVMGlobalGetValueType(global);
                src_value = llvm_api_cast_value(state, src_value, llvm_api_type_ref(state, src_type), global_type);
                LLVMBuildStore(state->builder, src_value, global);
                break;
            }

            case JIR_OP_ADD:
            case JIR_OP_SUB:
            case JIR_OP_MUL:
            case JIR_OP_DIV: {
                TypeExpr* lhs_type = NULL;
                TypeExpr* rhs_type = NULL;
                LLVMValueRef lhs = NULL;
                LLVMValueRef rhs = NULL;
                if (!llvm_api_emit_load(state, &fn_state, inst->src1, &lhs, &lhs_type)) return false;
                if (!llvm_api_emit_load(state, &fn_state, inst->src2, &rhs, &rhs_type)) return false;
                bool is_int_math = llvm_api_is_base_type(lhs_type, "Int") && llvm_api_is_base_type(rhs_type, "Int");
                bool is_float_math =
                    (llvm_api_is_base_type(lhs_type, "Float") || llvm_api_is_base_type(lhs_type, "Double")) &&
                    (llvm_api_is_base_type(rhs_type, "Float") || llvm_api_is_base_type(rhs_type, "Double"));
                if (!is_int_math && !is_float_math) {
                    llvm_api_error(state->error_buf, state->error_buf_size,
                                   "LLVM C API: arithmetic currently only supports Int/Float/Double");
                    return false;
                }
                LLVMValueRef result = NULL;
                if (is_int_math) {
                    switch (inst->op) {
                        case JIR_OP_ADD: result = LLVMBuildAdd(state->builder, lhs, rhs, "addtmp"); break;
                        case JIR_OP_SUB: result = LLVMBuildSub(state->builder, lhs, rhs, "subtmp"); break;
                        case JIR_OP_MUL: result = LLVMBuildMul(state->builder, lhs, rhs, "multmp"); break;
                        default: result = LLVMBuildSDiv(state->builder, lhs, rhs, "divtmp"); break;
                    }
                } else {
                    switch (inst->op) {
                        case JIR_OP_ADD: result = LLVMBuildFAdd(state->builder, lhs, rhs, "faddtmp"); break;
                        case JIR_OP_SUB: result = LLVMBuildFSub(state->builder, lhs, rhs, "fsubtmp"); break;
                        case JIR_OP_MUL: result = LLVMBuildFMul(state->builder, lhs, rhs, "fmultmp"); break;
                        default: result = LLVMBuildFDiv(state->builder, lhs, rhs, "fdivtmp"); break;
                    }
                }
                LLVMBuildStore(state->builder, result, fn_state.locals[inst->dest]);
                break;
            }

            case JIR_OP_CMP_EQ:
            case JIR_OP_CMP_NEQ:
            case JIR_OP_CMP_LT:
            case JIR_OP_CMP_GT:
            case JIR_OP_CMP_LTE:
            case JIR_OP_CMP_GTE: {
                TypeExpr* lhs_type = NULL;
                TypeExpr* rhs_type = NULL;
                LLVMValueRef lhs = NULL;
                LLVMValueRef rhs = NULL;
                if (!llvm_api_emit_load(state, &fn_state, inst->src1, &lhs, &lhs_type)) return false;
                if (!llvm_api_emit_load(state, &fn_state, inst->src2, &rhs, &rhs_type)) return false;
                if (!llvm_api_type_is_scalar(state, lhs_type) || !llvm_api_type_is_scalar(state, rhs_type)) {
                    llvm_api_error(state->error_buf, state->error_buf_size,
                                   "LLVM C API: comparisons currently require matching scalar types");
                    return false;
                }
                LLVMTypeRef lhs_llvm_type = llvm_api_type_ref(state, lhs_type);
                LLVMTypeRef rhs_llvm_type = llvm_api_type_ref(state, rhs_type);
                if (lhs_llvm_type != rhs_llvm_type) {
                    if ((lhs_llvm_type == state->f64_type) || (rhs_llvm_type == state->f64_type)) {
                        lhs = llvm_api_cast_value(state, lhs, lhs_llvm_type, state->f64_type);
                        rhs = llvm_api_cast_value(state, rhs, rhs_llvm_type, state->f64_type);
                        lhs_llvm_type = rhs_llvm_type = state->f64_type;
                    } else {
                        LLVMTypeRef target = LLVMGetIntTypeWidth(lhs_llvm_type) >= LLVMGetIntTypeWidth(rhs_llvm_type)
                                                 ? lhs_llvm_type : rhs_llvm_type;
                        lhs = llvm_api_cast_value(state, lhs, lhs_llvm_type, target);
                        rhs = llvm_api_cast_value(state, rhs, rhs_llvm_type, target);
                        lhs_llvm_type = rhs_llvm_type = target;
                    }
                }
                LLVMValueRef result = NULL;
                if (lhs_llvm_type == state->f64_type) {
                    LLVMRealPredicate pred =
                        inst->op == JIR_OP_CMP_EQ ? LLVMRealOEQ :
                        inst->op == JIR_OP_CMP_NEQ ? LLVMRealONE :
                        inst->op == JIR_OP_CMP_LT ? LLVMRealOLT :
                        inst->op == JIR_OP_CMP_GT ? LLVMRealOGT :
                        inst->op == JIR_OP_CMP_LTE ? LLVMRealOLE : LLVMRealOGE;
                    result = LLVMBuildFCmp(state->builder, pred, lhs, rhs, "fcmp");
                } else {
                    LLVMIntPredicate pred =
                        inst->op == JIR_OP_CMP_EQ ? LLVMIntEQ :
                        inst->op == JIR_OP_CMP_NEQ ? LLVMIntNE :
                        inst->op == JIR_OP_CMP_LT ? LLVMIntSLT :
                        inst->op == JIR_OP_CMP_GT ? LLVMIntSGT :
                        inst->op == JIR_OP_CMP_LTE ? LLVMIntSLE : LLVMIntSGE;
                    result = LLVMBuildICmp(state->builder, pred, lhs, rhs, "cmptmp");
                }
                result = llvm_api_cast_value(state, result, state->i1_type,
                                             llvm_api_type_ref(state, func->locals[inst->dest].type));
                LLVMBuildStore(state->builder, result, fn_state.locals[inst->dest]);
                break;
            }

            case JIR_OP_MEMBER_ACCESS:
                if (inst->payload.name.length > 0) {
                    TypeExpr* object_type = (inst->src1 >= 0 && (size_t)inst->src1 < func->local_count)
                                                ? func->locals[inst->src1].type
                                                : NULL;
                    if (object_type && object_type->kind == TYPE_SLICE &&
                        inst->payload.name.length == 6 &&
                        strncmp(inst->payload.name.start, "length", 6) == 0) {
                        TypeExpr* loaded_type = NULL;
                        LLVMValueRef slice_value = NULL;
                        if (!llvm_api_emit_load(state, &fn_state, inst->src1, &slice_value, &loaded_type)) return false;
                        LLVMValueRef len_value = LLVMBuildExtractValue(state->builder, slice_value, 1, "slice.len");
                        LLVMBuildStore(state->builder, len_value, fn_state.locals[inst->dest]);
                        break;
                    }
                    if (object_type && object_type->kind == TYPE_ARRAY &&
                        inst->payload.name.length == 6 &&
                        strncmp(inst->payload.name.start, "length", 6) == 0) {
                        long long len = llvm_api_static_array_length(object_type);
                        if (len < 0) {
                            llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: dynamic array length unsupported");
                            return false;
                        }
                        LLVMBuildStore(state->builder,
                                       LLVMConstInt(state->i64_type, (unsigned long long)len, false),
                                       fn_state.locals[inst->dest]);
                        break;
                    }
                    if ((llvm_api_is_base_type(object_type, "Float") || llvm_api_is_base_type(object_type, "Double") ||
                         llvm_api_is_base_type(object_type, "Int") || llvm_api_is_base_type(object_type, "Bool") ||
                         llvm_api_is_base_type(object_type, "UInt8")) &&
                        inst->payload.name.length == 5 &&
                        strncmp(inst->payload.name.start, "value", 5) == 0) {
                        TypeExpr* loaded_type = NULL;
                        LLVMValueRef value = NULL;
                        if (!llvm_api_emit_load(state, &fn_state, inst->src1, &value, &loaded_type)) return false;
                        LLVMBuildStore(state->builder, value, fn_state.locals[inst->dest]);
                        break;
                    }
                    if (object_type && object_type->kind == TYPE_BASE) {
                        int struct_field_index = llvm_api_find_struct_field_index(state, object_type->as.base_type,
                                                                                  inst->payload.name);
                        if (struct_field_index >= 0) {
                            TypeExpr* loaded_type = NULL;
                            LLVMValueRef struct_value = NULL;
                            if (!llvm_api_emit_load(state, &fn_state, inst->src1, &struct_value, &loaded_type)) return false;
                            LLVMValueRef field_value = LLVMBuildExtractValue(state->builder, struct_value,
                                                                             (unsigned)struct_field_index,
                                                                             "struct.field");
                            LLVMBuildStore(state->builder, field_value, fn_state.locals[inst->dest]);
                            break;
                        }
                        int union_variant_index = llvm_api_find_union_variant_index(state, object_type->as.base_type,
                                                                                    inst->payload.name);
                        if (union_variant_index >= 0) {
                            TypeExpr* loaded_type = NULL;
                            LLVMValueRef union_value = NULL;
                            if (!llvm_api_emit_load(state, &fn_state, inst->src1, &union_value, &loaded_type)) return false;
                            LLVMValueRef field_value = LLVMBuildExtractValue(state->builder, union_value,
                                                                             (unsigned)(union_variant_index + 1),
                                                                             "union.variant");
                            LLVMBuildStore(state->builder, field_value, fn_state.locals[inst->dest]);
                            break;
                        }
                    }
                    if (object_type && object_type->kind == TYPE_POINTER) {
                        TypeExpr* pointee = llvm_api_unwrap_type(object_type->as.pointer.element);
                        TypeExpr* loaded_type = NULL;
                        LLVMValueRef ptr_value = NULL;
                        if (!llvm_api_emit_load(state, &fn_state, inst->src1, &ptr_value, &loaded_type)) return false;
                        if (pointee && pointee->kind == TYPE_BASE) {
                            int struct_field_index = llvm_api_find_struct_field_index(state, pointee->as.base_type,
                                                                                      inst->payload.name);
                            if (struct_field_index >= 0) {
                                LLVMTypeRef pointee_type = llvm_api_type_ref(state, pointee);
                                LLVMValueRef typed_ptr = LLVMBuildBitCast(state->builder, ptr_value,
                                                                          LLVMPointerType(pointee_type, 0),
                                                                          "ptr.struct");
                                LLVMValueRef field_ptr = LLVMBuildStructGEP2(state->builder, pointee_type, typed_ptr,
                                                                             (unsigned)struct_field_index, "ptr.field.ptr");
                                TypeExpr* field_type_expr =
                                    llvm_api_find_struct_field_type(state, pointee->as.base_type, inst->payload.name);
                                LLVMTypeRef field_type = llvm_api_type_ref(state, field_type_expr);
                                if (!field_type) {
                                    llvm_api_error(state->error_buf, state->error_buf_size,
                                                   "LLVM C API: unsupported struct field type '%.*s.%.*s'",
                                                   (int)pointee->as.base_type.length, pointee->as.base_type.start,
                                                   (int)inst->payload.name.length, inst->payload.name.start);
                                    return false;
                                }
                                LLVMValueRef field_value = LLVMBuildLoad2(state->builder, field_type, field_ptr, "ptr.field");
                                LLVMBuildStore(state->builder, field_value, fn_state.locals[inst->dest]);
                                break;
                            }
                        }
                    }
                } else {
                    TypeExpr* object_type = (inst->src1 >= 0 && (size_t)inst->src1 < func->local_count)
                                                ? llvm_api_unwrap_type(func->locals[inst->src1].type)
                                                : NULL;
                    TypeExpr* index_type = NULL;
                    LLVMValueRef index_value = NULL;
                    if (!llvm_api_emit_load(state, &fn_state, inst->src2, &index_value, &index_type)) return false;
                    if (object_type && object_type->kind == TYPE_ARRAY) {
                        LLVMValueRef array_base =
                            fn_state.has_global_array_alias[inst->src1]
                                ? fn_state.global_array_aliases[inst->src1]
                                : fn_state.locals[inst->src1];
                        LLVMValueRef indices[2] = {
                            LLVMConstInt(state->i64_type, 0, false),
                            index_value,
                        };
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(state->builder,
                                                              llvm_api_type_ref(state, object_type),
                                                              array_base,
                                                              indices, 2, "array.elem.ptr");
                        LLVMTypeRef elem_type = llvm_api_type_ref(state, object_type->as.array.element);
                        LLVMValueRef elem = LLVMBuildLoad2(state->builder, elem_type, elem_ptr, "array.elem");
                        LLVMBuildStore(state->builder, elem, fn_state.locals[inst->dest]);
                        break;
                    }
                    if (object_type && object_type->kind == TYPE_SLICE) {
                        TypeExpr* loaded_type = NULL;
                        LLVMValueRef slice_value = NULL;
                        if (!llvm_api_emit_load(state, &fn_state, inst->src1, &slice_value, &loaded_type)) return false;
                        LLVMValueRef ptr = LLVMBuildExtractValue(state->builder, slice_value, 0, "slice.ptr");
                        LLVMTypeRef elem_type = llvm_api_type_ref(state, object_type->as.slice.element);
                        LLVMValueRef typed_ptr = LLVMBuildBitCast(state->builder, ptr, LLVMPointerType(elem_type, 0), "slice.typed.ptr");
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(state->builder, elem_type, typed_ptr, &index_value, 1, "slice.elem.ptr");
                        LLVMValueRef elem = LLVMBuildLoad2(state->builder, elem_type, elem_ptr, "slice.elem");
                        LLVMBuildStore(state->builder, elem, fn_state.locals[inst->dest]);
                        break;
                    }
                    if (object_type && object_type->kind == TYPE_POINTER) {
                        TypeExpr* pointee = llvm_api_unwrap_type(object_type->as.pointer.element);
                        TypeExpr* ptr_loaded_type = NULL;
                        LLVMValueRef ptr_value = NULL;
                        if (!llvm_api_emit_load(state, &fn_state, inst->src1, &ptr_value, &ptr_loaded_type)) return false;
                        if (pointee && pointee->kind == TYPE_ARRAY) {
                            LLVMTypeRef elem_type = llvm_api_type_ref(state, pointee->as.array.element);
                            LLVMValueRef typed_ptr = LLVMBuildBitCast(state->builder, ptr_value,
                                                                      LLVMPointerType(elem_type, 0), "ptr.array");
                            LLVMValueRef elem_ptr = LLVMBuildGEP2(state->builder, elem_type, typed_ptr, &index_value, 1,
                                                                  "ptr.array.elem");
                            LLVMValueRef elem = LLVMBuildLoad2(state->builder, elem_type, elem_ptr, "ptr.array.load");
                            LLVMBuildStore(state->builder, elem, fn_state.locals[inst->dest]);
                            break;
                        }
                    }
                }
                llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: unsupported member access");
                return false;

            case JIR_OP_JUMP:
                if (inst->payload.label_id >= 512 || !fn_state.has_label_block[inst->payload.label_id]) {
                    llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: jump to unknown label");
                    return false;
                }
                LLVMBuildBr(state->builder, fn_state.label_blocks[inst->payload.label_id]);
                break;

            case JIR_OP_JUMP_IF_FALSE: {
                TypeExpr* cond_type = NULL;
                LLVMValueRef cond_value = NULL;
                LLVMValueRef cond_i1 = NULL;
                if (!llvm_api_emit_load(state, &fn_state, inst->src1, &cond_value, &cond_type)) return false;
                if (!llvm_api_to_i1(state, cond_type, cond_value, &cond_i1)) return false;
                if (inst->payload.label_id >= 512 || !fn_state.has_label_block[inst->payload.label_id] || i >= 512) {
                    llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: invalid conditional branch target");
                    return false;
                }
                LLVMBuildCondBr(state->builder, cond_i1, fn_state.then_blocks[i], fn_state.label_blocks[inst->payload.label_id]);
                LLVMPositionBuilderAtEnd(state->builder, fn_state.then_blocks[i]);
                fn_state.current_block = fn_state.then_blocks[i];
                break;
            }

            case JIR_OP_CALL: {
                bool is_print =
                    (inst->payload.name.length == 5 && strncmp(inst->payload.name.start, "print", 5) == 0) ||
                    (inst->payload.name.length == 17 && strncmp(inst->payload.name.start, "__intrinsic_print", 17) == 0);
                bool is_assert =
                    (inst->payload.name.length == 6 && strncmp(inst->payload.name.start, "assert", 6) == 0) ||
                    (inst->payload.name.length == 18 && strncmp(inst->payload.name.start, "__intrinsic_assert", 18) == 0);
                bool is_file_exists =
                    (inst->payload.name.length == 6 && strncmp(inst->payload.name.start, "exists", 6) == 0) ||
                    (inst->payload.name.length == 23 && strncmp(inst->payload.name.start, "__intrinsic_file_exists", 23) == 0);
                bool is_read_file =
                    (inst->payload.name.length == 8 && strncmp(inst->payload.name.start, "read_all", 8) == 0) ||
                    (inst->payload.name.length == 21 && strncmp(inst->payload.name.start, "__intrinsic_read_file", 21) == 0);
                if (is_print) {
                    if (!llvm_api_emit_print(state, &fn_state, inst)) return false;
                    break;
                }
                if (is_assert) {
                    if (!llvm_api_emit_assert(state, &fn_state, inst)) return false;
                    break;
                }
                if (is_file_exists || is_read_file) {
                    if (inst->call_arg_count != 1) {
                        llvm_api_error(state->error_buf, state->error_buf_size,
                                       "LLVM C API: fs runtime call expects one argument");
                        return false;
                    }
                    TypeExpr* path_type = NULL;
                    LLVMValueRef path_value = NULL;
                    if (!llvm_api_emit_load(state, &fn_state, inst->call_args[0], &path_value, &path_type)) return false;
                    if (!llvm_api_is_u8_slice_type(path_type)) {
                        llvm_api_error(state->error_buf, state->error_buf_size,
                                       "LLVM C API: fs runtime path expects UInt8[]");
                        return false;
                    }
                    LLVMValueRef path_ptr = LLVMBuildExtractValue(state->builder, path_value, 0, "path.ptr");
                    LLVMValueRef rt_args[1] = { path_ptr };
                    LLVMValueRef callee = is_file_exists ? state->rt_file_exists_func : state->rt_read_file_func;
                    LLVMValueRef call_value = LLVMBuildCall2(state->builder, LLVMGlobalGetValueType(callee), callee,
                                                             rt_args, 1, is_file_exists ? "exists" : "read_file");
                    if (inst->dest >= 0) {
                        LLVMBuildStore(state->builder, call_value, fn_state.locals[inst->dest]);
                    }
                    break;
                }

                JirFunction* callee_jir = llvm_api_find_jir_function(state->jir, inst->payload.name);
                LLVMValueRef callee = llvm_api_find_llvm_function(state, inst->payload.name);
                if (!callee) {
                    llvm_api_error(state->error_buf, state->error_buf_size,
                                   "LLVM C API: unresolved call target '%.*s'",
                                   (int)inst->payload.name.length, inst->payload.name.start);
                    return false;
                }
                LLVMTypeRef callee_type = LLVMGlobalGetValueType(callee);
                LLVMValueRef args[64];
                if (inst->call_arg_count > 64) {
                    llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: too many call arguments");
                    return false;
                }
                for (size_t arg_i = 0; arg_i < inst->call_arg_count; arg_i++) {
                    TypeExpr* arg_type = NULL;
                    if (!llvm_api_emit_load(state, &fn_state, inst->call_args[arg_i], &args[arg_i], &arg_type)) return false;
                }
                unsigned callee_param_count = LLVMCountParamTypes(callee_type);
                LLVMTypeRef callee_param_types[64];
                if (callee_param_count > 64) {
                    llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: too many callee parameter types");
                    return false;
                }
                LLVMGetParamTypes(callee_type, callee_param_types);
                for (size_t arg_i = 0; arg_i < inst->call_arg_count && arg_i < callee_param_count; arg_i++) {
                    TypeExpr* arg_type = (inst->call_args[arg_i] >= 0 && (size_t)inst->call_args[arg_i] < func->local_count)
                                             ? func->locals[inst->call_args[arg_i]].type
                                             : NULL;
                    LLVMTypeRef actual_type = llvm_api_type_ref(state, arg_type);
                    args[arg_i] = llvm_api_cast_value(state, args[arg_i], actual_type, callee_param_types[arg_i]);
                }
                LLVMValueRef call_value = LLVMBuildCall2(state->builder, callee_type, callee, args,
                                                         (unsigned)inst->call_arg_count,
                                                         ((callee_jir && llvm_api_is_void_type(callee_jir->return_type)) ||
                                                          LLVMGetReturnType(callee_type) == state->void_type) ? "" : "calltmp");
                LLVMTypeRef call_ret_type = callee_jir
                                                ? llvm_api_type_ref(state, callee_jir->return_type)
                                                : llvm_api_find_callable_return_llvm_type(state, inst->payload.name);
                if (call_ret_type != state->void_type && call_ret_type != NULL && inst->dest >= 0) {
                    call_value = llvm_api_cast_value(state, call_value, LLVMGetReturnType(callee_type),
                                                     llvm_api_type_ref(state, func->locals[inst->dest].type));
                    LLVMBuildStore(state->builder, call_value, fn_state.locals[inst->dest]);
                }
                break;
            }

            case JIR_OP_RETURN:
                if (is_main) {
                    LLVMBuildRet(state->builder, LLVMConstInt(state->i32_type, 0, false));
                } else if (llvm_api_is_void_type(func->return_type) || inst->src1 < 0) {
                    LLVMBuildRetVoid(state->builder);
                } else {
                    TypeExpr* ret_type = NULL;
                    LLVMValueRef ret_value = NULL;
                    if (!llvm_api_emit_load(state, &fn_state, inst->src1, &ret_value, &ret_type)) return false;
                    ret_value = llvm_api_cast_value(state, ret_value, llvm_api_type_ref(state, ret_type),
                                                    llvm_api_type_ref(state, func->return_type));
                    LLVMBuildRet(state->builder, ret_value);
                }
                break;

            case JIR_OP_ADDR_OF: {
                LLVMValueRef addr = NULL;
                if (!llvm_api_emit_address(state, &fn_state, inst->src1, &addr, NULL)) return false;
                LLVMValueRef cast_addr = LLVMBuildBitCast(state->builder, addr,
                                                          llvm_api_type_ref(state, func->locals[inst->dest].type),
                                                          "addr");
                LLVMBuildStore(state->builder, cast_addr, fn_state.locals[inst->dest]);
                break;
            }

            case JIR_OP_DEREF: {
                TypeExpr* ptr_type = NULL;
                LLVMValueRef ptr_value = NULL;
                if (!llvm_api_emit_load(state, &fn_state, inst->src1, &ptr_value, &ptr_type)) return false;
                ptr_type = llvm_api_unwrap_type(ptr_type);
                if (!ptr_type || ptr_type->kind != TYPE_POINTER) {
                    llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: deref expects pointer");
                    return false;
                }
                LLVMTypeRef elem_type = llvm_api_type_ref(state, ptr_type->as.pointer.element);
                LLVMValueRef cast_ptr = LLVMBuildBitCast(state->builder, ptr_value, LLVMPointerType(elem_type, 0), "deref.ptr");
                LLVMValueRef value = LLVMBuildLoad2(state->builder, elem_type, cast_ptr, "deref");
                LLVMBuildStore(state->builder, value, fn_state.locals[inst->dest]);
                break;
            }

            case JIR_OP_MALLOC: {
                TypeExpr* dest_type = llvm_api_unwrap_type(func->locals[inst->dest].type);
                if (!dest_type || dest_type->kind != TYPE_POINTER) {
                    llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: malloc expects pointer destination");
                    return false;
                }
                LLVMTypeRef elem_type = llvm_api_type_ref(state, dest_type->as.pointer.element);
                if (!elem_type) {
                    llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: malloc unsupported pointee type");
                    return false;
                }
                LLVMValueRef size = LLVMBuildPtrToInt(state->builder, LLVMSizeOf(elem_type), state->i64_type, "malloc.size");
                LLVMValueRef malloc_args[1] = { size };
                LLVMValueRef raw_ptr = LLVMBuildCall2(state->builder, LLVMGlobalGetValueType(state->malloc_func),
                                                      state->malloc_func, malloc_args, 1, "malloc.ptr");
                LLVMValueRef cast_ptr = LLVMBuildBitCast(state->builder, raw_ptr, llvm_api_type_ref(state, func->locals[inst->dest].type), "malloc.cast");
                LLVMBuildStore(state->builder, cast_ptr, fn_state.locals[inst->dest]);
                break;
            }

            case JIR_OP_STORE_PTR: {
                TypeExpr* ptr_type = NULL;
                TypeExpr* value_type = NULL;
                LLVMValueRef ptr_value = NULL;
                LLVMValueRef value = NULL;
                if (!llvm_api_emit_load(state, &fn_state, inst->dest, &ptr_value, &ptr_type)) return false;
                if (!llvm_api_emit_load(state, &fn_state, inst->src1, &value, &value_type)) return false;
                ptr_type = llvm_api_unwrap_type(ptr_type);
                if (!ptr_type || ptr_type->kind != TYPE_POINTER) {
                    llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: store_ptr expects pointer destination");
                    return false;
                }
                LLVMTypeRef elem_type = llvm_api_type_ref(state, ptr_type->as.pointer.element);
                LLVMValueRef cast_ptr = LLVMBuildBitCast(state->builder, ptr_value, LLVMPointerType(elem_type, 0), "store.ptr");
                LLVMBuildStore(state->builder, value, cast_ptr);
                break;
            }

            case JIR_OP_ARRAY_INIT: {
                LLVMValueRef agg = LLVMGetUndef(llvm_api_type_ref(state, func->locals[inst->dest].type));
                for (size_t j = 0; j < inst->call_arg_count; j++) {
                    TypeExpr* arg_type = NULL;
                    LLVMValueRef arg = NULL;
                    if (!llvm_api_emit_load(state, &fn_state, inst->call_args[j], &arg, &arg_type)) return false;
                    agg = LLVMBuildInsertValue(state->builder, agg, arg, (unsigned)j, "array.init");
                }
                LLVMBuildStore(state->builder, agg, fn_state.locals[inst->dest]);
                break;
            }

            case JIR_OP_MAKE_SLICE: {
                TypeExpr* obj_type = (inst->src1 >= 0 && (size_t)inst->src1 < func->local_count)
                                         ? llvm_api_unwrap_type(func->locals[inst->src1].type)
                                         : NULL;
                LLVMValueRef base_ptr = NULL;
                LLVMValueRef len_value = NULL;
                LLVMValueRef start_index = LLVMConstInt(state->i64_type, 0, false);
                if (inst->src2 >= 0) {
                    TypeExpr* start_type = NULL;
                    if (!llvm_api_emit_load(state, &fn_state, inst->src2, &start_index, &start_type)) return false;
                }
                if (obj_type && obj_type->kind == TYPE_ARRAY) {
                    LLVMValueRef array_base =
                        fn_state.has_global_array_alias[inst->src1]
                            ? fn_state.global_array_aliases[inst->src1]
                            : fn_state.locals[inst->src1];
                    LLVMValueRef indices[2] = {
                        LLVMConstInt(state->i64_type, 0, false),
                        start_index,
                    };
                    base_ptr = LLVMBuildGEP2(state->builder, llvm_api_type_ref(state, obj_type),
                                             array_base, indices, 2, "slice.from.array");
                    long long arr_len = llvm_api_static_array_length(obj_type);
                    len_value = LLVMConstInt(state->i64_type, (unsigned long long)arr_len, false);
                } else if (obj_type && obj_type->kind == TYPE_SLICE) {
                    TypeExpr* loaded_type = NULL;
                    LLVMValueRef slice_value = NULL;
                    if (!llvm_api_emit_load(state, &fn_state, inst->src1, &slice_value, &loaded_type)) return false;
                    LLVMTypeRef elem_type = llvm_api_type_ref(state, obj_type->as.slice.element);
                    LLVMValueRef ptr = LLVMBuildExtractValue(state->builder, slice_value, 0, "slice.ptr");
                    LLVMValueRef typed_ptr = LLVMBuildBitCast(state->builder, ptr, LLVMPointerType(elem_type, 0), "slice.base.ptr");
                    base_ptr = LLVMBuildGEP2(state->builder, elem_type, typed_ptr, &start_index, 1, "slice.offset.ptr");
                    len_value = LLVMBuildExtractValue(state->builder, slice_value, 1, "slice.orig.len");
                } else {
                    llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: make_slice expects array or slice");
                    return false;
                }
                if (inst->call_arg_count > 0) {
                    TypeExpr* end_type = NULL;
                    LLVMValueRef end_value = NULL;
                    if (!llvm_api_emit_load(state, &fn_state, inst->call_args[0], &end_value, &end_type)) return false;
                    len_value = LLVMBuildSub(state->builder, end_value, start_index, "slice.len");
                } else if (inst->src2 >= 0) {
                    len_value = LLVMBuildSub(state->builder, len_value, start_index, "slice.len");
                }
                LLVMValueRef slice = LLVMGetUndef(llvm_api_type_ref(state, func->locals[inst->dest].type));
                slice = LLVMBuildInsertValue(state->builder, slice, base_ptr, 0, "slice.ptr");
                slice = LLVMBuildInsertValue(state->builder, slice, len_value, 1, "slice.len");
                LLVMBuildStore(state->builder, slice, fn_state.locals[inst->dest]);
                break;
            }

            case JIR_OP_STORE_MEMBER: {
                TypeExpr* object_type = (inst->dest >= 0 && (size_t)inst->dest < func->local_count)
                                            ? llvm_api_unwrap_type(func->locals[inst->dest].type)
                                            : NULL;
                TypeExpr* value_type = NULL;
                LLVMValueRef value = NULL;
                if (!llvm_api_emit_load(state, &fn_state, inst->src1, &value, &value_type)) return false;
                if (inst->payload.name.length == 0) {
                    TypeExpr* index_type = NULL;
                    LLVMValueRef index_value = NULL;
                    if (!llvm_api_emit_load(state, &fn_state, inst->src2, &index_value, &index_type)) return false;
                    if (object_type && object_type->kind == TYPE_ARRAY) {
                        LLVMValueRef array_base =
                            fn_state.has_global_array_alias[inst->dest]
                                ? fn_state.global_array_aliases[inst->dest]
                                : fn_state.locals[inst->dest];
                        LLVMValueRef indices[2] = {
                            LLVMConstInt(state->i64_type, 0, false),
                            index_value,
                        };
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(state->builder,
                                                              llvm_api_type_ref(state, object_type),
                                                              array_base,
                                                              indices, 2, "array.elem.ptr");
                        LLVMBuildStore(state->builder, value, elem_ptr);
                        break;
                    }
                    if (object_type && object_type->kind == TYPE_SLICE) {
                        TypeExpr* loaded_type = NULL;
                        LLVMValueRef slice_value = NULL;
                        if (!llvm_api_emit_load(state, &fn_state, inst->dest, &slice_value, &loaded_type)) return false;
                        LLVMValueRef ptr = LLVMBuildExtractValue(state->builder, slice_value, 0, "slice.ptr");
                        LLVMTypeRef elem_type = llvm_api_type_ref(state, object_type->as.slice.element);
                        LLVMValueRef typed_ptr = LLVMBuildBitCast(state->builder, ptr, LLVMPointerType(elem_type, 0), "slice.typed.ptr");
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(state->builder, elem_type, typed_ptr, &index_value, 1, "slice.elem.ptr");
                        LLVMBuildStore(state->builder, value, elem_ptr);
                        break;
                    }
                }
                if (inst->payload.name.length > 0 && object_type && object_type->kind == TYPE_BASE) {
                    int struct_field_index = llvm_api_find_struct_field_index(state, object_type->as.base_type,
                                                                              inst->payload.name);
                    if (struct_field_index >= 0) {
                        TypeExpr* loaded_type = NULL;
                        LLVMValueRef struct_value = NULL;
                        if (!llvm_api_emit_load(state, &fn_state, inst->dest, &struct_value, &loaded_type)) return false;
                        struct_value = LLVMBuildInsertValue(state->builder, struct_value, value,
                                                            (unsigned)struct_field_index, "struct.set");
                        LLVMBuildStore(state->builder, struct_value, fn_state.locals[inst->dest]);
                        break;
                    }
                }
                if (inst->payload.name.length > 0 && object_type && object_type->kind == TYPE_POINTER) {
                    TypeExpr* pointee = llvm_api_unwrap_type(object_type->as.pointer.element);
                    TypeExpr* ptr_loaded_type = NULL;
                    LLVMValueRef ptr_value = NULL;
                    if (!llvm_api_emit_load(state, &fn_state, inst->dest, &ptr_value, &ptr_loaded_type)) return false;
                    if (pointee && pointee->kind == TYPE_BASE) {
                        int struct_field_index = llvm_api_find_struct_field_index(state, pointee->as.base_type,
                                                                                  inst->payload.name);
                        if (struct_field_index >= 0) {
                            LLVMTypeRef pointee_type = llvm_api_type_ref(state, pointee);
                            LLVMValueRef typed_ptr = LLVMBuildBitCast(state->builder, ptr_value,
                                                                      LLVMPointerType(pointee_type, 0),
                                                                      "ptr.struct");
                            LLVMValueRef field_ptr = LLVMBuildStructGEP2(state->builder, pointee_type, typed_ptr,
                                                                         (unsigned)struct_field_index, "ptr.field.ptr");
                            LLVMBuildStore(state->builder, value, field_ptr);
                            break;
                        }
                    }
                }
                llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: unsupported store member");
                return false;
            }

            case JIR_OP_STRUCT_INIT: {
                LLVMTypeRef struct_type = llvm_api_type_ref(state, func->locals[inst->dest].type);
                LLVMValueRef agg = LLVMGetUndef(struct_type);
                for (size_t j = 0; j < inst->call_arg_count; j++) {
                    TypeExpr* arg_type = NULL;
                    LLVMValueRef arg = NULL;
                    if (!llvm_api_emit_load(state, &fn_state, inst->call_args[j], &arg, &arg_type)) return false;
                    agg = LLVMBuildInsertValue(state->builder, agg, arg, (unsigned)j, "struct.init");
                }
                LLVMBuildStore(state->builder, agg, fn_state.locals[inst->dest]);
                break;
            }

            case JIR_OP_GET_TAG: {
                TypeExpr* object_type = (inst->src1 >= 0 && (size_t)inst->src1 < func->local_count)
                                            ? llvm_api_unwrap_type(func->locals[inst->src1].type)
                                            : NULL;
                if (!object_type || object_type->kind != TYPE_BASE ||
                    !llvm_api_find_union_info(state, object_type->as.base_type)) {
                    llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: get_tag expects union");
                    return false;
                }
                TypeExpr* loaded_type = NULL;
                LLVMValueRef union_value = NULL;
                if (!llvm_api_emit_load(state, &fn_state, inst->src1, &union_value, &loaded_type)) return false;
                LLVMValueRef tag = LLVMBuildExtractValue(state->builder, union_value, 0, "union.tag");
                LLVMBuildStore(state->builder, tag, fn_state.locals[inst->dest]);
                break;
            }

            case JIR_OP_EXTRACT_FIELD: {
                TypeExpr* object_type = (inst->src1 >= 0 && (size_t)inst->src1 < func->local_count)
                                            ? llvm_api_unwrap_type(func->locals[inst->src1].type)
                                            : NULL;
                if (object_type && object_type->kind == TYPE_TUPLE) {
                    TypeExpr* loaded_type = NULL;
                    LLVMValueRef tuple_value = NULL;
                    if (!llvm_api_emit_load(state, &fn_state, inst->src1, &tuple_value, &loaded_type)) return false;
                    LLVMValueRef field = LLVMBuildExtractValue(state->builder, tuple_value, (unsigned)inst->payload.int_val,
                                                               "tuple.field");
                    LLVMBuildStore(state->builder, field, fn_state.locals[inst->dest]);
                    break;
                }
                llvm_api_error(state->error_buf, state->error_buf_size, "LLVM C API: unsupported extract field");
                return false;
            }

            default:
                llvm_api_error(state->error_buf, state->error_buf_size,
                               "LLVM C API: unsupported JIR op %d in '%.*s'",
                               inst->op, (int)func->name.length, func->name.start);
                return false;
        }
    }

    if (LLVMGetBasicBlockTerminator(fn_state.current_block) == NULL) {
        if (is_main) LLVMBuildRet(state->builder, LLVMConstInt(state->i32_type, 0, false));
        else if (llvm_api_is_void_type(func->return_type)) LLVMBuildRetVoid(state->builder);
        else LLVMBuildRet(state->builder, LLVMConstNull(llvm_api_type_ref(state, func->return_type)));
    }

    return true;
}

static bool llvm_api_generate_ir_impl(JirModule* mod, CodegenModuleInfo** infos, size_t info_count,
                                      const char* out_path, char* error_buf, size_t error_buf_size) {
    if (error_buf && error_buf_size > 0) error_buf[0] = '\0';
    if (!mod || !out_path) {
        llvm_api_error(error_buf, error_buf_size, "LLVM C API: invalid input");
        return false;
    }

    size_t entry_index = (size_t)-1;
    for (size_t i = 0; i < mod->function_count; i++) {
        JirFunction* func = mod->functions[i];
        if (func->name.length == 11 && strncmp(func->name.start, "module_main", 11) == 0) {
            entry_index = i;
            break;
        }
    }
    if (entry_index == (size_t)-1) {
        llvm_api_error(error_buf, error_buf_size, "LLVM C API: could not find module_main");
        return false;
    }

    bool* reachable = calloc(mod->function_count, sizeof(bool));
    if (!reachable) {
        llvm_api_error(error_buf, error_buf_size, "LLVM C API: out of memory building reachability");
        return false;
    }
    llvm_api_collect_reachable(mod, entry_index, reachable);

    LlvmApiState state;
    memset(&state, 0, sizeof(state));
    state.context = LLVMContextCreate();
    state.module = LLVMModuleCreateWithNameInContext("jiang", state.context);
    state.builder = LLVMCreateBuilderInContext(state.context);
    state.i1_type = LLVMInt1TypeInContext(state.context);
    state.i8_type = LLVMInt8TypeInContext(state.context);
    state.i32_type = LLVMInt32TypeInContext(state.context);
    state.i64_type = LLVMInt64TypeInContext(state.context);
    state.f64_type = LLVMDoubleTypeInContext(state.context);
    state.void_type = LLVMVoidTypeInContext(state.context);
    state.ptr_type = LLVMPointerType(state.i8_type, 0);
    state.slice_u8_type = LLVMStructCreateNamed(state.context, "Slice_uint8_t");
    state.slice_i64_type = LLVMStructCreateNamed(state.context, "Slice_int64_t");
    LLVMTypeRef slice_u8_fields[2] = { state.ptr_type, state.i64_type };
    LLVMTypeRef slice_i64_fields[2] = { LLVMPointerType(state.i64_type, 0), state.i64_type };
    LLVMStructSetBody(state.slice_u8_type, slice_u8_fields, 2, false);
    LLVMStructSetBody(state.slice_i64_type, slice_i64_fields, 2, false);
    state.jir = mod;
    state.infos = infos;
    state.info_count = info_count;
    state.error_buf = error_buf;
    state.error_buf_size = error_buf_size;

    llvm_api_declare_named_types(&state);
    llvm_api_define_named_types(&state);

    LLVMTypeRef printf_params[1] = { state.ptr_type };
    LLVMTypeRef printf_type = LLVMFunctionType(state.i32_type, printf_params, 1, true);
    state.printf_func = LLVMAddFunction(state.module, "printf", printf_type);

    LLVMTypeRef abort_type = LLVMFunctionType(state.void_type, NULL, 0, false);
    state.abort_func = LLVMAddFunction(state.module, "abort", abort_type);

    llvm_api_build_runtime_helpers(&state);
    llvm_api_declare_globals(&state);
    llvm_api_declare_wrapper_functions(&state);

    bool ok = llvm_api_declare_functions(&state, reachable, entry_index);
    if (ok) ok = llvm_api_emit_wrapper_functions(&state);
    for (size_t i = 0; ok && i < mod->function_count; i++) {
        if (!reachable[i]) continue;
        ok = llvm_api_emit_function(&state, mod->functions[i], i == entry_index);
    }

    char* verify_error = NULL;
    if (ok && LLVMVerifyModule(state.module, LLVMReturnStatusAction, &verify_error) != 0) {
        llvm_api_error(error_buf, error_buf_size, "LLVM C API verify failed: %s",
                       verify_error ? verify_error : "unknown error");
        ok = false;
    }
    if (verify_error) LLVMDisposeMessage(verify_error);

    char* print_error = NULL;
    if (ok && LLVMPrintModuleToFile(state.module, out_path, &print_error) != 0) {
        llvm_api_error(error_buf, error_buf_size, "LLVM C API print failed: %s",
                       print_error ? print_error : "unknown error");
        ok = false;
    }
    if (print_error) LLVMDisposeMessage(print_error);

    if (!ok) {
        remove(out_path);
        LLVMDisposeBuilder(state.builder);
        LLVMDisposeModule(state.module);
        LLVMContextDispose(state.context);
    }
    free(reachable);
    return ok;
}

bool llvm_generate_ir(JirModule* mod, const char* out_path, char* error_buf, size_t error_buf_size) {
    return llvm_api_generate_ir_impl(mod, NULL, 0, out_path, error_buf, error_buf_size);
}

bool llvm_generate_ir_program(JirModule** mods, CodegenModuleInfo** infos, size_t mod_count, size_t entry_index,
                              const char* out_path, char* error_buf, size_t error_buf_size) {
    if (error_buf && error_buf_size > 0) error_buf[0] = '\0';
    if (!mods || mod_count == 0 || entry_index >= mod_count || !out_path) {
        llvm_api_error(error_buf, error_buf_size, "LLVM C API: invalid program input");
        return false;
    }

    size_t function_count = 0;
    for (size_t i = 0; i < mod_count; i++) {
        if (!mods[i]) continue;
        for (size_t j = 0; j < mods[i]->function_count; j++) {
            JirFunction* func = mods[i]->functions[j];
            if (!func) continue;
            bool is_module_main = func->name.length == 11 &&
                                  strncmp(func->name.start, "module_main", 11) == 0;
            if (is_module_main && i != entry_index) continue;
            function_count++;
        }
    }

    if (function_count == 0) {
        llvm_api_error(error_buf, error_buf_size, "LLVM C API: no functions to emit");
        return false;
    }

    JirModule combined;
    memset(&combined, 0, sizeof(combined));
    combined.function_capacity = function_count;
    combined.function_count = function_count;
    combined.functions = calloc(function_count, sizeof(JirFunction*));
    if (!combined.functions) {
        llvm_api_error(error_buf, error_buf_size, "LLVM C API: out of memory building combined module");
        return false;
    }

    size_t out_index = 0;
    for (size_t i = 0; i < mod_count; i++) {
        if (!mods[i]) continue;
        for (size_t j = 0; j < mods[i]->function_count; j++) {
            JirFunction* func = mods[i]->functions[j];
            if (!func) continue;
            bool is_module_main = func->name.length == 11 &&
                                  strncmp(func->name.start, "module_main", 11) == 0;
            if (is_module_main && i != entry_index) continue;
            combined.functions[out_index++] = func;
        }
    }

    bool ok = llvm_api_generate_ir_impl(&combined, infos, mod_count, out_path, error_buf, error_buf_size);
    free(combined.functions);
    return ok;
}

#else

static void llvm_api_error(char* error_buf, size_t error_buf_size, const char* format, ...) {
    if (!error_buf || error_buf_size == 0) return;
    va_list args;
    va_start(args, format);
    vsnprintf(error_buf, error_buf_size, format, args);
    va_end(args);
}

bool llvm_generate_ir(JirModule* mod, const char* out_path, char* error_buf, size_t error_buf_size) {
    (void)mod;
    (void)out_path;
    llvm_api_error(error_buf, error_buf_size, "LLVM C API support is not enabled in this build");
    return false;
}

bool llvm_generate_ir_program(JirModule** mods, CodegenModuleInfo** infos, size_t mod_count, size_t entry_index,
                              const char* out_path, char* error_buf, size_t error_buf_size) {
    (void)mods;
    (void)infos;
    (void)mod_count;
    (void)entry_index;
    (void)out_path;
    llvm_api_error(error_buf, error_buf_size, "LLVM C API support is not enabled in this build");
    return false;
}

#endif
