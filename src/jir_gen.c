#include "jir_gen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void gen_c_type(TypeExpr* type, FILE* out, bool is_var_decl) {
    if (!type) { fprintf(out, is_var_decl ? "int64_t" : "void"); return; }
    switch (type->kind) {
        case TYPE_BASE:
            if (type->as.base_type.length == 3 && strncmp(type->as.base_type.start, "Int", 3) == 0) fprintf(out, "int64_t");
            else if (type->as.base_type.length == 5 && strncmp(type->as.base_type.start, "Float", 5) == 0) fprintf(out, "double");
            else if (type->as.base_type.length == 4 && strncmp(type->as.base_type.start, "Bool", 4) == 0) fprintf(out, "bool");
            else if (type->as.base_type.length == 6 && strncmp(type->as.base_type.start, "String", 6) == 0) fprintf(out, "const char*");
            else if ((type->as.base_type.length == 4 && strncmp(type->as.base_type.start, "void", 4) == 0) || (type->as.base_type.length == 2 && strncmp(type->as.base_type.start, "()", 2) == 0)) fprintf(out, is_var_decl ? "int64_t" : "void");
            else fprintf(out, "%.*s", (int)type->as.base_type.length, type->as.base_type.start);
            break;
        case TYPE_POINTER:
            if (!type->as.pointer.element || (type->as.pointer.element->kind == TYPE_BASE && type->as.pointer.element->as.base_type.length == 4 && strncmp(type->as.pointer.element->as.base_type.start, "void", 4) == 0)) fprintf(out, "void*");
            else { gen_c_type(type->as.pointer.element, out, false); fprintf(out, "*"); }
            break;
        case TYPE_TUPLE:
            if (type->as.tuple.count == 0) { fprintf(out, is_var_decl ? "int64_t" : "void"); return; }
            fprintf(out, "struct { ");
            for (size_t i = 0; i < type->as.tuple.count; i++) {
                gen_c_type(type->as.tuple.elements[i], out, true);
                if (type->as.tuple.names && type->as.tuple.names[i].length > 0) fprintf(out, " %.*s; ", (int)type->as.tuple.names[i].length, type->as.tuple.names[i].start);
                else fprintf(out, " _%zu; ", i);
            }
            fprintf(out, " }");
            break;
        default: fprintf(out, "void*"); break;
    }
}

static void gen_global_decls(ASTNode* node, FILE* out) {
    if (!node) return;
    switch (node->type) {
        case AST_PROGRAM: case AST_BLOCK:
            for (size_t i = 0; i < node->as.block.count; i++) gen_global_decls(node->as.block.statements[i], out);
            break;
        case AST_STRUCT_DECL:
            fprintf(out, "typedef struct %.*s %.*s;\n", (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start, (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start);
            fprintf(out, "struct %.*s {\n", (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start);
            for (size_t i = 0; i < node->as.struct_decl.member_count; i++) {
                ASTNode* member = node->as.struct_decl.members[i];
                if (member->type == AST_VAR_DECL) {
                    fprintf(out, "    "); gen_c_type(member->as.var_decl.type, out, true);
                    fprintf(out, " %.*s;\n", (int)member->as.var_decl.name.length, member->as.var_decl.name.start);
                }
            }
            fprintf(out, "};\n\n");
            break;
        case AST_ENUM_DECL:
            fprintf(out, "typedef enum {\n");
            for (size_t i = 0; i < node->as.enum_decl.member_count; i++) {
                fprintf(out, "    %.*s_%.*s", (int)node->as.enum_decl.name.length, node->as.enum_decl.name.start, (int)node->as.enum_decl.member_names[i].length, node->as.enum_decl.member_names[i].start);
                if (i < node->as.enum_decl.member_count - 1) fprintf(out, ","); fprintf(out, "\n");
            }
            fprintf(out, "} %.*s;\n\n", (int)node->as.enum_decl.name.length, node->as.enum_decl.name.start);
            break;
        case AST_UNION_DECL:
            fprintf(out, "typedef struct %.*s %.*s;\n", (int)node->as.union_decl.name.length, node->as.union_decl.name.start, (int)node->as.union_decl.name.length, node->as.union_decl.name.start);
            fprintf(out, "struct %.*s {\n", (int)node->as.union_decl.name.length, node->as.union_decl.name.start);
            if (node->as.union_decl.tag_enum.length > 0) fprintf(out, "    %.*s tag;\n", (int)node->as.union_decl.tag_enum.length, node->as.union_decl.tag_enum.start);
            else fprintf(out, "    int tag;\n");
            fprintf(out, "    union {\n");
            for (size_t i = 0; i < node->as.union_decl.member_count; i++) {
                ASTNode* variant = node->as.union_decl.members[i];
                fprintf(out, "        "); gen_c_type(variant->as.var_decl.type, out, true);
                fprintf(out, " %.*s;\n", (int)variant->as.var_decl.name.length, variant->as.var_decl.name.start);
            }
            fprintf(out, "    } as;\n"); fprintf(out, "};\n\n");
            break;
        default: break;
    }
}

static void gen_function(JirFunction* func, FILE* out, bool is_main_wrapper) {
    if (is_main_wrapper) fprintf(out, "int main() {\n");
    else {
        gen_c_type(func->return_type, out, false);
        fprintf(out, " %.*s(", (int)func->name.length, func->name.start);
        bool first = true;
        for (size_t i = 0; i < func->local_count; i++) {
            if (func->locals[i].is_param) {
                if (!first) fprintf(out, ", ");
                gen_c_type(func->locals[i].type, out, true);
                fprintf(out, " _r%zu", i); first = false;
            }
        }
        if (first) fprintf(out, "void");
        fprintf(out, ") {\n");
    }
    for (size_t i = 0; i < func->local_count; i++) {
        if (!func->locals[i].is_param) {
            TypeExpr* t = func->locals[i].type;
            if (t && t->kind == TYPE_BASE && t->as.base_type.length == 2 && strncmp(t->as.base_type.start, "()", 2) == 0) continue;
            fprintf(out, "    "); gen_c_type(t, out, true);
            fprintf(out, " _r%zu; // %s\n", i, func->locals[i].name);
        }
    }
    for (size_t i = 0; i < func->inst_count; i++) {
        JirInst* inst = &func->insts[i]; fprintf(out, "    ");
        switch (inst->op) {
            case JIR_OP_LABEL: fprintf(out, "L%u:;\n", inst->payload.label_id); continue;
            case JIR_OP_CONST_INT: if (inst->dest >= 0) fprintf(out, "_r%d = %lldLL;\n", inst->dest, (long long)inst->payload.int_val); break;
            case JIR_OP_CONST_FLOAT: if (inst->dest >= 0) fprintf(out, "_r%d = %f;\n", inst->dest, inst->payload.float_val); break;
            case JIR_OP_CONST_STR: if (inst->dest >= 0) fprintf(out, "_r%d = \"%s\";\n", inst->dest, inst->payload.str_val ? inst->payload.str_val : ""); break;
            case JIR_OP_LOAD_SYM: if (inst->dest >= 0) fprintf(out, "_r%d = %.*s;\n", inst->dest, (int)inst->payload.name.length, inst->payload.name.start); break;
            case JIR_OP_STORE: 
                if (inst->dest >= 0 && inst->src1 >= 0) {
                    // Safety cast for void* assignments
                    bool is_dest_ptr = (func->locals[inst->dest].type && func->locals[inst->dest].type->kind == TYPE_POINTER);
                    if (is_dest_ptr) fprintf(out, "_r%d = (void*)_r%d;\n", inst->dest, inst->src1);
                    else fprintf(out, "_r%d = _r%d;\n", inst->dest, inst->src1);
                } break;
            case JIR_OP_ADD: if (inst->dest >= 0) fprintf(out, "_r%d = _r%d + _r%d;\n", inst->dest, inst->src1, inst->src2); break;
            case JIR_OP_SUB: if (inst->dest >= 0) fprintf(out, "_r%d = _r%d - _r%d;\n", inst->dest, inst->src1, inst->src2); break;
            case JIR_OP_MUL: if (inst->dest >= 0) fprintf(out, "_r%d = _r%d * _r%d;\n", inst->dest, inst->src1, inst->src2); break;
            case JIR_OP_DIV: if (inst->dest >= 0) fprintf(out, "_r%d = _r%d / _r%d;\n", inst->dest, inst->src1, inst->src2); break;
            case JIR_OP_CMP_EQ:
                if (inst->dest >= 0) {
                    if (inst->src1 >= 0 && inst->src2 >= 0) fprintf(out, "_r%d = (_r%d == _r%d);\n", inst->dest, inst->src1, inst->src2);
                    else if (inst->src1 >= 0 && inst->payload.name.length > 0) fprintf(out, "_r%d = (_r%d == %.*s);\n", inst->dest, inst->src1, (int)inst->payload.name.length, inst->payload.name.start);
                } break;
            case JIR_OP_MEMBER_ACCESS:
                if (inst->dest >= 0 && inst->src1 >= 0) {
                    bool is_ptr = (func->locals[inst->src1].type && func->locals[inst->src1].type->kind == TYPE_POINTER);
                    fprintf(out, "_r%d = _r%d%s%.*s;\n", inst->dest, inst->src1, is_ptr ? "->" : ".", (int)inst->payload.name.length, inst->payload.name.start);
                } break;
            case JIR_OP_GET_TAG: if (inst->dest >= 0 && inst->src1 >= 0) fprintf(out, "_r%d = _r%d.tag;\n", inst->dest, inst->src1); break;
            case JIR_OP_EXTRACT_FIELD:
                if (inst->dest >= 0 && inst->src1 >= 0) {
                    fprintf(out, "_r%d = _r%d.as.%.*s", inst->dest, inst->src1, (int)inst->payload.name.length, inst->payload.name.start);
                    if (inst->src2 >= 0) fprintf(out, "._%d", inst->src2);
                    fprintf(out, ";\n");
                } break;
            case JIR_OP_JUMP_IF_FALSE: if (inst->src1 >= 0) fprintf(out, "if (!_r%d) goto L%u;\n", inst->src1, inst->payload.label_id); break;
            case JIR_OP_JUMP: fprintf(out, "goto L%u;\n", inst->payload.label_id); break;
            case JIR_OP_CALL: {
                if (inst->dest >= 0) {
                    TypeExpr* t = func->locals[inst->dest].type;
                    if (!(t && t->kind == TYPE_BASE && t->as.base_type.length == 2 && strncmp(t->as.base_type.start, "()", 2) == 0)) fprintf(out, "_r%d = ", inst->dest);
                }
                if (strncmp(inst->payload.name.start, "print", 5) == 0 && inst->payload.name.length == 5) {
                    fprintf(out, "printf(");
                    for (size_t k = 0; k < inst->call_arg_count; k++) {
                        TypeExpr* t = func->locals[inst->call_args[k]].type;
                        if (k == 0 && t && t->kind == TYPE_BASE && strncmp(t->as.base_type.start, "String", 6) == 0) fprintf(out, "_r%d", inst->call_args[k]);
                        else {
                            if (t && t->kind == TYPE_BASE) {
                                if (strncmp(t->as.base_type.start, "Int", 3) == 0) fprintf(out, "\"%%lld\", _r%d", inst->call_args[k]);
                                else if (strncmp(t->as.base_type.start, "Float", 5) == 0) fprintf(out, "\"%%f\", _r%d", inst->call_args[k]);
                                else fprintf(out, "\"%%p\", _r%d", inst->call_args[k]);
                            } else fprintf(out, "\"%%p\", _r%d", inst->call_args[k]);
                        }
                        if (k < inst->call_arg_count - 1) fprintf(out, ", ");
                    }
                } else if (strncmp(inst->payload.name.start, "assert", 6) == 0 && inst->payload.name.length == 6) {
                    fprintf(out, "assert(");
                    for (size_t k = 0; k < inst->call_arg_count; k++) {
                        fprintf(out, "_r%d", inst->call_args[k]);
                        if (k < inst->call_arg_count - 1) fprintf(out, ", ");
                    }
                } else {
                    fprintf(out, "%.*s(", (int)inst->payload.name.length, inst->payload.name.start);
                    for (size_t k = 0; k < inst->call_arg_count; k++) {
                        fprintf(out, "_r%d", inst->call_args[k]);
                        if (k < inst->call_arg_count - 1) fprintf(out, ", ");
                    }
                }
                fprintf(out, ");\n"); break;
            }
            case JIR_OP_RETURN: if (inst->src1 >= 0) fprintf(out, "return _r%d;\n", inst->src1); else fprintf(out, "return;\n"); break;
            default: break;
        }
    }
    if (is_main_wrapper) fprintf(out, "    return 0;\n");
    fprintf(out, "}\n\n");
}

void jir_generate_c(JirModule* mod, ASTNode* root, const char* out_path, bool is_main) {
    FILE* out = fopen(out_path, "w"); if (!out) return;
    fprintf(out, "#include <stdio.h>\n#include <stdint.h>\n#include <stdbool.h>\n#include <stdlib.h>\n#include <assert.h>\n\n");
    gen_global_decls(root, out);
    for (size_t i = 0; i < mod->function_count; i++) {
        JirFunction* func = mod->functions[i];
        if (strncmp(func->name.start, "main", 4) == 0 && func->name.length == 4) continue;
        gen_c_type(func->return_type, out, false);
        fprintf(out, " %.*s(", (int)func->name.length, func->name.start);
        bool first_param = true;
        for (size_t j = 0; j < func->local_count; j++) {
            if (func->locals[j].is_param) {
                if (!first_param) fprintf(out, ", ");
                gen_c_type(func->locals[j].type, out, true);
                fprintf(out, " _r%zu", j);
                first_param = false;
            }
        }
        if (first_param) fprintf(out, "void");
        fprintf(out, ");\n");
    }
    for (size_t i = 0; i < mod->function_count; i++) {
        JirFunction* func = mod->functions[i];
        gen_function(func, out, is_main && strncmp(func->name.start, "main", 4) == 0 && func->name.length == 4);
    }
    fclose(out);
}
