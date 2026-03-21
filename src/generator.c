#include "generator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h> // For getcwd
#include <limits.h> // For PATH_MAX

// Simple symbol table for generator to know variable types for method dispatch
typedef struct {
    char name[64];
    char type[64];
    TypeExpr* type_expr;
    bool is_param;
} GenSymbol;

static GenSymbol gen_sym_table[256];
static size_t gen_sym_count = 0;
static bool gen_printed_syms = false;
static bool gen_is_local = false;
static int gen_binding_temp_id = 0;
static JirModule* gen_active_jir_module = NULL;
static TypeExpr gen_pattern_fallback_type = {
    .kind = TYPE_BASE,
    .as.base_type = { TOKEN_IDENTIFIER, "Double", 6, 0 }
};

typedef struct {
    char name[128];
    char fields[8][64];
    size_t field_count;
} GenTupleVariant;

static GenTupleVariant gen_tuple_variants[256];
static size_t gen_tuple_variant_count = 0;

typedef struct {
    TypeExpr* type;
    char name[64];
} GenTupleType;

static GenTupleType gen_tuple_types[256];
static size_t gen_tuple_type_count = 0;

static void gen_add_tuple_variant(const char* union_name, const char* variant_name) {
    snprintf(gen_tuple_variants[gen_tuple_variant_count].name, 128, "%s_%s_t", union_name, variant_name);
    gen_tuple_variants[gen_tuple_variant_count].field_count = 0;
    gen_tuple_variant_count++;
}

static bool gen_is_tuple_variant(const char* union_name, const char* variant_name) {
    char target[128];
    snprintf(target, 128, "%s_%s_t", union_name, variant_name);
    for (size_t i = 0; i < gen_tuple_variant_count; i++) {
        if (strcmp(gen_tuple_variants[i].name, target) == 0) return true;
    }
    return false;
}

static bool gen_type_equals(TypeExpr* a, TypeExpr* b) {
    if (a == b) return true;
    if (!a || !b || a->kind != b->kind) return false;
    switch (a->kind) {
        case TYPE_BASE:
            return a->as.base_type.length == b->as.base_type.length &&
                   strncmp(a->as.base_type.start, b->as.base_type.start, a->as.base_type.length) == 0;
        case TYPE_POINTER:
            return gen_type_equals(a->as.pointer.element, b->as.pointer.element);
        case TYPE_ARRAY:
            return gen_type_equals(a->as.array.element, b->as.array.element);
        case TYPE_SLICE:
            return gen_type_equals(a->as.slice.element, b->as.slice.element);
        case TYPE_NULLABLE:
            return gen_type_equals(a->as.nullable.element, b->as.nullable.element);
        case TYPE_MUTABLE:
            return gen_type_equals(a->as.mutable.element, b->as.mutable.element);
        case TYPE_TUPLE:
            if (a->as.tuple.count != b->as.tuple.count) return false;
            for (size_t i = 0; i < a->as.tuple.count; i++) {
                if (!gen_type_equals(a->as.tuple.elements[i], b->as.tuple.elements[i])) return false;
            }
            return true;
    }
    return false;
}

static const char* gen_register_tuple_type(TypeExpr* type) {
    if (!type || type->kind != TYPE_TUPLE) return NULL;
    for (size_t i = 0; i < gen_tuple_type_count; i++) {
        if (gen_type_equals(gen_tuple_types[i].type, type)) return gen_tuple_types[i].name;
    }
    snprintf(gen_tuple_types[gen_tuple_type_count].name, sizeof(gen_tuple_types[gen_tuple_type_count].name),
             "JiangTuple_%zu", gen_tuple_type_count);
    gen_tuple_types[gen_tuple_type_count].type = type;
    return gen_tuple_types[gen_tuple_type_count++].name;
}

static void gen_set_tuple_variant_fields(const char* union_name, const char* variant_name, TypeExpr* tuple_type) {
    char target[128];
    snprintf(target, sizeof(target), "%s_%s_t", union_name, variant_name);
    for (size_t i = 0; i < gen_tuple_variant_count; i++) {
        if (strcmp(gen_tuple_variants[i].name, target) != 0) continue;
        gen_tuple_variants[i].field_count = tuple_type && tuple_type->kind == TYPE_TUPLE ? tuple_type->as.tuple.count : 0;
        for (size_t j = 0; j < gen_tuple_variants[i].field_count && j < 8; j++) {
            if (tuple_type->as.tuple.names && tuple_type->as.tuple.names[j].length > 0) {
                snprintf(gen_tuple_variants[i].fields[j], 64, "%.*s",
                         (int)tuple_type->as.tuple.names[j].length, tuple_type->as.tuple.names[j].start);
            } else {
                snprintf(gen_tuple_variants[i].fields[j], 64, "_%zu", j);
            }
        }
        return;
    }
}

static const char* gen_get_tuple_variant_field(const char* union_name, const char* variant_name, int field_index) {
    char target[128];
    snprintf(target, sizeof(target), "%s_%s_t", union_name, variant_name);
    for (size_t i = 0; i < gen_tuple_variant_count; i++) {
        if (strcmp(gen_tuple_variants[i].name, target) == 0 &&
            field_index >= 0 && (size_t)field_index < gen_tuple_variants[i].field_count) {
            return gen_tuple_variants[i].fields[field_index];
        }
    }
    return NULL;
}

typedef struct {
    char name[64];
    char members[64][64];
    size_t member_count;
} GenEnum;

static GenEnum gen_enums[64];
static size_t gen_enum_count = 0;

static char gen_struct_names[64][64];
static size_t gen_struct_count = 0;

typedef struct {
    char name[64];
    char tag_enum[64];
} GenUnion;

static GenUnion gen_unions[64];
static size_t gen_union_count = 0;

static void gen_add_union(Token name, Token tag_enum) {
    snprintf(gen_unions[gen_union_count].name, 64, "%.*s", (int)name.length, name.start);
    if (tag_enum.length > 0) {
        snprintf(gen_unions[gen_union_count].tag_enum, 64, "%.*s", (int)tag_enum.length, tag_enum.start);
    } else {
        snprintf(gen_unions[gen_union_count].tag_enum, 64, "%.*sTag", (int)name.length, name.start);
    }
    gen_union_count++;
}

static const char* gen_get_union_tag_enum(const char* union_name) {
    for (size_t i = 0; i < gen_union_count; i++) {
        if (strcmp(gen_unions[i].name, union_name) == 0) return gen_unions[i].tag_enum;
    }
    return NULL;
}

static bool gen_is_u8_slice_type(TypeExpr* type) {
    return type &&
           type->kind == TYPE_SLICE &&
           type->as.slice.element &&
           type->as.slice.element->kind == TYPE_BASE &&
           type->as.slice.element->as.base_type.length == 5 &&
           strncmp(type->as.slice.element->as.base_type.start, "UInt8", 5) == 0;
}

static void gen_add_enum(ASTNode* decl) {
    Token name = decl->as.enum_decl.name;
    snprintf(gen_enums[gen_enum_count].name, 64, "%.*s", (int)name.length, name.start);
    gen_enums[gen_enum_count].member_count = decl->as.enum_decl.member_count;
    for (size_t i = 0; i < decl->as.enum_decl.member_count; i++) {
        snprintf(gen_enums[gen_enum_count].members[i], 64, "%.*s", 
                 (int)decl->as.enum_decl.member_names[i].length, 
                 decl->as.enum_decl.member_names[i].start);
    }
    gen_enum_count++;
}

static bool gen_is_enum(Token name) {
    for (size_t i = 0; i < gen_enum_count; i++) {
        if (strncmp(gen_enums[i].name, name.start, name.length) == 0 && gen_enums[i].name[name.length] == '\0') {
            return true;
        }
    }
    return false;
}

static bool gen_is_enum_str(const char* name) {
    for (size_t i = 0; i < gen_enum_count; i++) {
        if (strcmp(gen_enums[i].name, name) == 0) return true;
    }
    return false;
}

static void gen_add_struct(Token name) {
    snprintf(gen_struct_names[gen_struct_count++], 64, "%.*s", (int)name.length, name.start);
}

static bool gen_is_struct(Token name) {
    for (size_t i = 0; i < gen_struct_count; i++) {
        if (strncmp(gen_struct_names[i], name.start, name.length) == 0 && gen_struct_names[i][name.length] == '\0') {
            return true;
        }
    }
    return false;
}

static void gen_add_sym(Token name, TypeExpr* type, bool is_param) {
    if (!type) return;
    
    // Check for duplicates
    for (size_t i = 0; i < gen_sym_count; i++) {
        if (strncmp(gen_sym_table[i].name, name.start, name.length) == 0 && gen_sym_table[i].name[name.length] == '\0') {
            return;
        }
    }

    if (gen_sym_count >= 256) return;

    snprintf(gen_sym_table[gen_sym_count].name, 64, "%.*s", (int)name.length, name.start);
    gen_sym_table[gen_sym_count].type_expr = type;
    gen_sym_table[gen_sym_count].is_param = is_param;
    if (type->kind == TYPE_BASE) {
        snprintf(gen_sym_table[gen_sym_count].type, 64, "%.*s", (int)type->as.base_type.length, type->as.base_type.start);
    } else if (type->kind == TYPE_SLICE) {
        snprintf(gen_sym_table[gen_sym_count].type, 64, "SLICE"); // Marker for slice
    } else if (type->kind == TYPE_POINTER) {
        // For pointers, we store the base type but it's used differently
        if (type->as.pointer.element->kind == TYPE_BASE)
            snprintf(gen_sym_table[gen_sym_count].type, 64, "%.*s", (int)type->as.pointer.element->as.base_type.length, type->as.pointer.element->as.base_type.start);
        else
            snprintf(gen_sym_table[gen_sym_count].type, 64, "PTR");
    } else {
        snprintf(gen_sym_table[gen_sym_count].type, 64, "OTHER");
    }
    gen_sym_count++;
}

static const char* gen_get_type(Token name) {
    for (size_t i = 0; i < gen_sym_count; i++) {
        if (strncmp(gen_sym_table[i].name, name.start, name.length) == 0 && gen_sym_table[i].name[name.length] == '\0') {
            return gen_sym_table[i].type;
        }
    }
    return NULL;
}

// Track current struct context for 'self->' prefixing
static const char* current_struct_context = NULL;
typedef struct {
    char name[64];
} StructField;
static StructField current_struct_fields[64];
static size_t current_struct_field_count = 0;

static bool is_current_struct_field(Token name) {
    for (size_t i = 0; i < current_struct_field_count; i++) {
        if (name.length == strlen(current_struct_fields[i].name) &&
            strncmp(current_struct_fields[i].name, name.start, name.length) == 0) {
            return true;
        }
    }
    return false;
}

static bool gen_is_static_struct_method(ASTNode* method) {
    if (!method || method->type != AST_FUNC_DECL) return false;
    return method->as.func_decl.name.length == 4 &&
           strncmp(method->as.func_decl.name.start, "open", 4) == 0;
}

static bool gen_is_void_type(TypeExpr* type) {
    return type &&
           type->kind == TYPE_BASE &&
           type->as.base_type.length == 2 &&
           strncmp(type->as.base_type.start, "()", 2) == 0;
}

static void generate_node(ASTNode* node, FILE* out);
static void generate_type(TypeExpr* type, FILE* out);
static void generate_match_condition(ASTNode* value, ASTNode* pattern, FILE* out);
static void generate_declarations(ASTNode* root, FILE* out, const char* out_path);
static void generate_implementations(ASTNode* root, FILE* out, bool is_main, bool emit_free_functions, bool emit_main_body);
static bool gen_should_emit_jir_for_ast_function(ASTNode* node);

static void generate_decl_with_name(TypeExpr* type, const char* name, FILE* out) {
    if (!type || (type->kind == TYPE_BASE && type->as.base_type.length == 1 && type->as.base_type.start[0] == '_')) {
        fprintf(out, "double %s", name);
        return;
    }
    generate_type(type, out);
    fprintf(out, " %s", name);
    if (type && type->kind == TYPE_ARRAY) {
        fprintf(out, "[");
        if (type->as.array.length) generate_node(type->as.array.length, out);
        fprintf(out, "]");
    }
}

static void generate_tuple_field_ref(FILE* out, const char* tuple_name, TypeExpr* tuple_type, size_t index) {
    if (tuple_type && tuple_type->kind == TYPE_TUPLE &&
        tuple_type->as.tuple.names && index < tuple_type->as.tuple.count &&
        tuple_type->as.tuple.names[index].length > 0) {
        fprintf(out, "%s.%.*s", tuple_name,
                (int)tuple_type->as.tuple.names[index].length,
                tuple_type->as.tuple.names[index].start);
    } else {
        fprintf(out, "%s._%zu", tuple_name, index);
    }
}

static void generate_match_condition(ASTNode* value, ASTNode* pattern, FILE* out) {
    if (!value || !pattern) {
        fprintf(out, "0");
        return;
    }

    if (pattern->type == AST_MEMBER_ACCESS ||
        (pattern->type == AST_FUNC_CALL && pattern->as.func_call.callee->type == AST_MEMBER_ACCESS)) {
        ASTNode* ma = (pattern->type == AST_MEMBER_ACCESS) ? pattern : pattern->as.func_call.callee;
        ASTNode* obj = ma->as.member_access.object;
        if (obj->type == AST_IDENTIFIER && gen_is_struct(obj->as.identifier.name)) {
            bool has_patterns = false;
            if (pattern->type == AST_FUNC_CALL) {
                for (size_t i = 0; i < pattern->as.func_call.arg_count; i++) {
                    if (pattern->as.func_call.args[i]->type == AST_PATTERN) {
                        has_patterns = true;
                        break;
                    }
                }
            }

            if (has_patterns) {
                fprintf(out, "({ (");
                generate_node(value, out);
                fprintf(out, ".kind == ");

                char u_name[64];
                snprintf(u_name, sizeof(u_name), "%.*s", (int)obj->as.identifier.name.length, obj->as.identifier.name.start);
                char v_name[64];
                snprintf(v_name, sizeof(v_name), "%.*s", (int)ma->as.member_access.member.length, ma->as.member_access.member.start);
                const char* tag_enum_name = gen_get_union_tag_enum(u_name);
                if (tag_enum_name && tag_enum_name[0] != '\0') {
                    fprintf(out, "%s_%.*s", tag_enum_name, (int)ma->as.member_access.member.length, ma->as.member_access.member.start);
                } else {
                    fprintf(out, "%.*s_%.*s", (int)obj->as.identifier.name.length, obj->as.identifier.name.start,
                            (int)ma->as.member_access.member.length, ma->as.member_access.member.start);
                }

                fprintf(out, ") ? (");
                int pattern_pos = 0;
                for (size_t i = 0; i < pattern->as.func_call.arg_count; i++) {
                    ASTNode* arg = pattern->as.func_call.args[i];
                    if (arg->type != AST_PATTERN) continue;
                    fprintf(out, "%.*s = ", (int)arg->as.pattern.name.length, arg->as.pattern.name.start);
                    generate_node(value, out);
                    fprintf(out, ".%.*s", (int)ma->as.member_access.member.length, ma->as.member_access.member.start);
                    if (gen_is_tuple_variant(u_name, v_name) ||
                        arg->as.pattern.field_index >= 0 || pattern_pos > 0) {
                        int field_index = pattern_pos;
                        if (arg->as.pattern.field_name.length > 0) {
                            fprintf(out, ".%.*s", (int)arg->as.pattern.field_name.length, arg->as.pattern.field_name.start);
                        } else if (gen_is_tuple_variant(u_name, v_name)) {
                            const char* field_name = gen_get_tuple_variant_field(u_name, v_name, field_index);
                            if (field_name) fprintf(out, ".%s", field_name);
                        }
                    }
                    fprintf(out, ", ");
                    pattern_pos++;
                }
                fprintf(out, "1) : 0; })");
                return;
            }

            fprintf(out, "(");
            generate_node(value, out);
            fprintf(out, ".kind == ");

            char u_name[64];
            snprintf(u_name, sizeof(u_name), "%.*s", (int)obj->as.identifier.name.length, obj->as.identifier.name.start);
            const char* tag_enum_name = gen_get_union_tag_enum(u_name);
            if (tag_enum_name && tag_enum_name[0] != '\0') {
                fprintf(out, "%s_%.*s", tag_enum_name, (int)ma->as.member_access.member.length, ma->as.member_access.member.start);
            } else {
                fprintf(out, "%.*s_%.*s", (int)obj->as.identifier.name.length, obj->as.identifier.name.start,
                        (int)ma->as.member_access.member.length, ma->as.member_access.member.start);
            }
            fprintf(out, ")");
            return;
        }
    }

    fprintf(out, "(");
    generate_node(value, out);
    fprintf(out, " == ");
    generate_node(pattern, out);
    fprintf(out, ")");
}

static void gen_collect_tuple_types_from_type(TypeExpr* type) {
    if (!type) return;
    switch (type->kind) {
        case TYPE_POINTER: gen_collect_tuple_types_from_type(type->as.pointer.element); break;
        case TYPE_ARRAY: gen_collect_tuple_types_from_type(type->as.array.element); break;
        case TYPE_SLICE: gen_collect_tuple_types_from_type(type->as.slice.element); break;
        case TYPE_NULLABLE: gen_collect_tuple_types_from_type(type->as.nullable.element); break;
        case TYPE_MUTABLE: gen_collect_tuple_types_from_type(type->as.mutable.element); break;
        case TYPE_TUPLE:
            gen_register_tuple_type(type);
            for (size_t i = 0; i < type->as.tuple.count; i++) {
                gen_collect_tuple_types_from_type(type->as.tuple.elements[i]);
            }
            break;
        default: break;
    }
}

static void gen_collect_tuple_types_from_node(ASTNode* node) {
    if (!node) return;
    gen_collect_tuple_types_from_type(node->evaluated_type);
    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (size_t i = 0; i < node->as.block.count; i++) gen_collect_tuple_types_from_node(node->as.block.statements[i]);
            break;
        case AST_BINDING_LIST:
            for (size_t i = 0; i < node->as.binding_list.count; i++) gen_collect_tuple_types_from_node(node->as.binding_list.items[i]);
            break;
        case AST_BINDING_ASSIGN:
            gen_collect_tuple_types_from_node(node->as.binding_assign.bindings);
            gen_collect_tuple_types_from_node(node->as.binding_assign.value);
            break;
        case AST_VAR_DECL:
            gen_collect_tuple_types_from_type(node->as.var_decl.type);
            gen_collect_tuple_types_from_node(node->as.var_decl.initializer);
            break;
        case AST_FUNC_DECL:
            gen_collect_tuple_types_from_type(node->as.func_decl.return_type);
            for (size_t i = 0; i < node->as.func_decl.param_count; i++) gen_collect_tuple_types_from_node(node->as.func_decl.params[i]);
            gen_collect_tuple_types_from_node(node->as.func_decl.body);
            break;
        case AST_RETURN_STMT:
            gen_collect_tuple_types_from_node(node->as.return_stmt.expression);
            break;
        case AST_BINARY_EXPR:
            gen_collect_tuple_types_from_node(node->as.binary.left);
            gen_collect_tuple_types_from_node(node->as.binary.right);
            break;
        case AST_UNARY_EXPR:
            gen_collect_tuple_types_from_node(node->as.unary.right);
            break;
        case AST_FUNC_CALL:
            gen_collect_tuple_types_from_node(node->as.func_call.callee);
            for (size_t i = 0; i < node->as.func_call.arg_count; i++) gen_collect_tuple_types_from_node(node->as.func_call.args[i]);
            break;
        case AST_MEMBER_ACCESS:
            gen_collect_tuple_types_from_node(node->as.member_access.object);
            break;
        case AST_IF_STMT:
            gen_collect_tuple_types_from_node(node->as.if_stmt.condition);
            gen_collect_tuple_types_from_node(node->as.if_stmt.then_branch);
            gen_collect_tuple_types_from_node(node->as.if_stmt.else_branch);
            break;
        case AST_SWITCH_STMT:
            gen_collect_tuple_types_from_node(node->as.switch_stmt.expression);
            for (size_t i = 0; i < node->as.switch_stmt.case_count; i++) {
                gen_collect_tuple_types_from_node(node->as.switch_stmt.cases[i]);
            }
            gen_collect_tuple_types_from_node(node->as.switch_stmt.else_branch);
            break;
        case AST_SWITCH_CASE:
            gen_collect_tuple_types_from_node(node->as.switch_case.pattern);
            gen_collect_tuple_types_from_node(node->as.switch_case.body);
            break;
        case AST_WHILE_STMT:
            gen_collect_tuple_types_from_node(node->as.while_stmt.condition);
            gen_collect_tuple_types_from_node(node->as.while_stmt.body);
            break;
        case AST_FOR_STMT:
            gen_collect_tuple_types_from_node(node->as.for_stmt.pattern);
            gen_collect_tuple_types_from_node(node->as.for_stmt.iterable);
            gen_collect_tuple_types_from_node(node->as.for_stmt.body);
            break;
        case AST_STRUCT_DECL:
            for (size_t i = 0; i < node->as.struct_decl.member_count; i++) gen_collect_tuple_types_from_node(node->as.struct_decl.members[i]);
            break;
        case AST_INIT_DECL:
            for (size_t i = 0; i < node->as.init_decl.param_count; i++) gen_collect_tuple_types_from_node(node->as.init_decl.params[i]);
            gen_collect_tuple_types_from_node(node->as.init_decl.body);
            break;
        case AST_STRUCT_INIT_EXPR:
            gen_collect_tuple_types_from_type(node->as.struct_init.type);
            for (size_t i = 0; i < node->as.struct_init.field_count; i++) gen_collect_tuple_types_from_node(node->as.struct_init.field_values[i]);
            break;
        case AST_NEW_EXPR:
            gen_collect_tuple_types_from_node(node->as.new_expr.value);
            break;
        case AST_INDEX_EXPR:
            gen_collect_tuple_types_from_node(node->as.index_expr.object);
            gen_collect_tuple_types_from_node(node->as.index_expr.index);
            break;
        case AST_ARRAY_LITERAL:
            gen_collect_tuple_types_from_type(node->as.array_literal.type);
            for (size_t i = 0; i < node->as.array_literal.element_count; i++) gen_collect_tuple_types_from_node(node->as.array_literal.elements[i]);
            break;
        case AST_TUPLE_LITERAL:
            for (size_t i = 0; i < node->as.tuple_literal.count; i++) gen_collect_tuple_types_from_node(node->as.tuple_literal.elements[i]);
            break;
        case AST_UNION_DECL:
            for (size_t i = 0; i < node->as.union_decl.member_count; i++) gen_collect_tuple_types_from_node(node->as.union_decl.members[i]);
            break;
        default: break;
    }
}

static void generate_type(TypeExpr* type, FILE* out) {
    if (!type) {
        fprintf(out, "void");
        return;
    }

    switch (type->kind) {
        case TYPE_BASE:
            if (type->as.base_type.length == 2 && strncmp(type->as.base_type.start, "()", 2) == 0) fprintf(out, "void");
            else if (type->as.base_type.length == 1 && type->as.base_type.start[0] == '_') return; // Handle inferred type placeholder
            else if (type->as.base_type.length == 3 && strncmp(type->as.base_type.start, "Int", 3) == 0) fprintf(out, "int64_t");
            else if (type->as.base_type.length == 5 && strncmp(type->as.base_type.start, "Float", 5) == 0) fprintf(out, "float");
            else if (type->as.base_type.length == 6 && strncmp(type->as.base_type.start, "Double", 6) == 0) fprintf(out, "double");
            else if (type->as.base_type.length == 5 && strncmp(type->as.base_type.start, "UInt8", 5) == 0) fprintf(out, "uint8_t");
            else if (type->as.base_type.length == 6 && strncmp(type->as.base_type.start, "UInt16", 6) == 0) fprintf(out, "uint16_t");
            else if (type->as.base_type.length == 4 && strncmp(type->as.base_type.start, "Bool", 4) == 0) fprintf(out, "bool");
            else fprintf(out, "%.*s", (int)type->as.base_type.length, type->as.base_type.start);
            break;
        case TYPE_POINTER:
            generate_type(type->as.pointer.element, out);
            fprintf(out, "*");
            break;
        case TYPE_ARRAY:
            generate_type(type->as.array.element, out);
            break;
        case TYPE_SLICE:
            fprintf(out, "Slice_");
            generate_type(type->as.slice.element, out);
            break;
        case TYPE_NULLABLE:
            generate_type(type->as.nullable.element, out);
            break; 
        case TYPE_MUTABLE:
            generate_type(type->as.mutable.element, out);
            break; 
        case TYPE_TUPLE:
            fprintf(out, "%s", gen_register_tuple_type(type));
            break;
    }
}

static void generate_node(ASTNode* node, FILE* out) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
            for (size_t i = 0; i < node->as.block.count; i++) {
                generate_node(node->as.block.statements[i], out);
                if (node->as.block.statements[i]->type != AST_BLOCK && 
                    node->as.block.statements[i]->type != AST_IF_STMT &&
                    node->as.block.statements[i]->type != AST_SWITCH_STMT &&
                    node->as.block.statements[i]->type != AST_WHILE_STMT &&
                    node->as.block.statements[i]->type != AST_FOR_STMT) {
                    fprintf(out, ";\n");
                }
            }
            break;

        case AST_VAR_DECL: {
            TypeExpr* type = node->as.var_decl.type;
            if (!type || (type->kind == TYPE_BASE && type->as.base_type.length == 1 && type->as.base_type.start[0] == '_')) {
                type = node->evaluated_type;
            }
            if (!gen_is_local) {
                gen_add_sym(node->as.var_decl.name, type, false);
                generate_type(type, out);
                fprintf(out, " %.*s", (int)node->as.var_decl.name.length, node->as.var_decl.name.start);
                if (type && type->kind == TYPE_ARRAY) {
                    fprintf(out, "[");
                    if (type->as.array.length) generate_node(type->as.array.length, out);
                    fprintf(out, "]");
                }
            } else {
                if (node->as.var_decl.initializer) {
                    if (type && type->kind == TYPE_ARRAY && node->as.var_decl.initializer->type == AST_ARRAY_LITERAL) {
                        fprintf(out, "memcpy(%.*s, &", (int)node->as.var_decl.name.length, node->as.var_decl.name.start);
                        generate_node(node->as.var_decl.initializer, out);
                        fprintf(out, ", sizeof(");
                        generate_type(type->as.array.element, out);
                        fprintf(out, ") * ");
                        if (type->as.array.length) generate_node(type->as.array.length, out);
                        else fprintf(out, "%zu", node->as.var_decl.initializer->as.array_literal.element_count);
                        fprintf(out, ")");
                        return;
                    }
                    fprintf(out, "%.*s", (int)node->as.var_decl.name.length, node->as.var_decl.name.start);
                }
            }

            if (node->as.var_decl.initializer) {
                fprintf(out, " = ");
                generate_node(node->as.var_decl.initializer, out);
            }
            break;
        }

        case AST_IDENTIFIER:
            if (current_struct_context && is_current_struct_field(node->as.identifier.name)) {
                fprintf(out, "self->%.*s", (int)node->as.identifier.name.length, node->as.identifier.name.start);
            } else if (node->as.identifier.name.length == 4 && strncmp(node->as.identifier.name.start, "self", 4) == 0) {
                fprintf(out, "self");
            } else {
                fprintf(out, "%.*s", (int)node->as.identifier.name.length, node->as.identifier.name.start);
            }
            break;

        case AST_LITERAL_NUMBER:
            if (node->as.number.value == (int64_t)node->as.number.value) {
                fprintf(out, "%lld", (int64_t)node->as.number.value);
            } else {
                fprintf(out, "%g", node->as.number.value);
            }
            break;

        case AST_LITERAL_STRING:
            {
                int raw_len = (int)node->as.string.value.length;
                fprintf(out, "(Slice_uint8_t){(uint8_t*)");
                fprintf(out, "%.*s", raw_len, node->as.string.value.start);
                fprintf(out, ", %d}", raw_len >= 2 ? raw_len - 2 : 0);
            }
            break;

        case AST_TUPLE_LITERAL:
            if (node->evaluated_type &&
                node->evaluated_type->kind == TYPE_BASE &&
                node->evaluated_type->as.base_type.length == 2 &&
                strncmp(node->evaluated_type->as.base_type.start, "()", 2) == 0) {
                fprintf(out, "0");
                break;
            }
            fprintf(out, "(");
            generate_type(node->evaluated_type, out);
            fprintf(out, "){");
            for (size_t i = 0; i < node->as.tuple_literal.count; i++) {
                generate_node(node->as.tuple_literal.elements[i], out);
                if (i < node->as.tuple_literal.count - 1) fprintf(out, ", ");
            }
            fprintf(out, "}");
            break;

        case AST_BINARY_EXPR:
            if (node->as.binary.op == TOKEN_EQUAL_EQUAL) {
                generate_match_condition(node->as.binary.left, node->as.binary.right, out);
                return;
            }
            fprintf(out, "(");
            generate_node(node->as.binary.left, out);
            switch (node->as.binary.op) {
                case TOKEN_PLUS: fprintf(out, " + "); break;
                case TOKEN_MINUS: fprintf(out, " - "); break;
                case TOKEN_STAR: fprintf(out, " * "); break;
                case TOKEN_SLASH: fprintf(out, " / "); break;
                case TOKEN_EQUAL: fprintf(out, " = "); break;
                case TOKEN_EQUAL_EQUAL: fprintf(out, " == "); break;
                case TOKEN_BANG_EQUAL: fprintf(out, " != "); break;
                case TOKEN_LESS: fprintf(out, " < "); break;
                case TOKEN_GREATER: fprintf(out, " > "); break;
                case TOKEN_LESS_EQUAL: fprintf(out, " <= "); break;
                case TOKEN_GREATER_EQUAL: fprintf(out, " >= "); break;
                default: break;
            }
            generate_node(node->as.binary.right, out);
            fprintf(out, ")");
            break;

        case AST_FUNC_CALL: {
            ASTNode* callee = node->as.func_call.callee;
            if (callee->type == AST_MEMBER_ACCESS) {
                ASTNode* obj = callee->as.member_access.object;
                Token method = callee->as.member_access.member;

                if (obj->symbol && obj->symbol->kind == SYM_NAMESPACE) {
                    fprintf(out, "%.*s(", (int)method.length, method.start);
                    for (size_t i = 0; i < node->as.func_call.arg_count; i++) {
                        generate_node(node->as.func_call.args[i], out);
                        if (i < node->as.func_call.arg_count - 1) fprintf(out, ", ");
                    }
                    fprintf(out, ")");
                    return;
                }
                
                if ((obj->type == AST_IDENTIFIER && gen_is_struct(obj->as.identifier.name)) ||
                    (obj->symbol && obj->symbol->kind == SYM_STRUCT)) {
                    // Static Union/Struct Constructor
                    char u_name[64], m_name[64];
                    if (obj->type == AST_IDENTIFIER) {
                        snprintf(u_name, 64, "%.*s", (int)obj->as.identifier.name.length, obj->as.identifier.name.start);
                    } else if (obj->symbol && obj->symbol->type && obj->symbol->type->kind == TYPE_BASE) {
                        snprintf(u_name, 64, "%.*s", (int)obj->symbol->type->as.base_type.length, obj->symbol->type->as.base_type.start);
                    } else {
                        snprintf(u_name, 64, "Unknown");
                    }
                    snprintf(m_name, 64, "%.*s", (int)method.length, method.start);
                    
                    fprintf(out, "%s_%s(", u_name, m_name);
                    if (node->as.func_call.arg_count > 1 || (node->as.func_call.arg_count == 1 && gen_is_tuple_variant(u_name, m_name))) {
                        fprintf(out, "(%s_%s_t){", u_name, m_name);
                        for (size_t i = 0; i < node->as.func_call.arg_count; i++) {
                            generate_node(node->as.func_call.args[i], out);
                            if (i < node->as.func_call.arg_count - 1) fprintf(out, ", ");
                        }
                        fprintf(out, "}");
                    } else if (node->as.func_call.arg_count == 1) {
                        generate_node(node->as.func_call.args[0], out);
                    }
                    fprintf(out, ")");
                    return;
                }

                const char* obj_type = (obj->type == AST_IDENTIFIER) ? gen_get_type(obj->as.identifier.name) : NULL;
                if (obj_type && strcmp(obj_type, "SLICE") != 0 && strcmp(obj_type, "OTHER") != 0 && strcmp(obj_type, "PTR") != 0) {
                    fprintf(out, "%s_%.*s(", obj_type, (int)method.length, method.start);
                    bool is_ptr = (obj->evaluated_type && obj->evaluated_type->kind == TYPE_POINTER);
                    if (!is_ptr) fprintf(out, "&");
                    generate_node(obj, out);
                    if (node->as.func_call.arg_count > 0) fprintf(out, ", ");
                } else {
                    generate_node(obj, out);
                    fprintf(out, "%s%.*s(", (obj->evaluated_type && obj->evaluated_type->kind == TYPE_POINTER) ? "->" : ".", (int)method.length, method.start);
                }
                
                for (size_t i = 0; i < node->as.func_call.arg_count; i++) {
                    generate_node(node->as.func_call.args[i], out);
                    if (i < node->as.func_call.arg_count - 1) fprintf(out, ", ");
                }
                fprintf(out, ")");
            } else if (callee->type == AST_IDENTIFIER) {
                Token name = callee->as.identifier.name;
                if (name.length == 6 && strncmp(name.start, "assert", 6) == 0) {
                    fprintf(out, "((void)(( ");
                    generate_node(node->as.func_call.args[0], out);
                    fprintf(out, " ) ? 0 : (fprintf(stderr, \"Assertion failed at line %%d\\n\", %d), exit(1))))", node->line);
                } else if (name.length == 5 && strncmp(name.start, "print", 5) == 0) {
                    fprintf(out, "printf(");
                    for (size_t i = 0; i < node->as.func_call.arg_count; i++) {
                        ASTNode* arg = node->as.func_call.args[i];
                        TypeExpr* arg_type = arg->evaluated_type ? arg->evaluated_type : (arg->symbol ? arg->symbol->type : NULL);
                        bool is_u8_slice = gen_is_u8_slice_type(arg_type);
                        if (is_u8_slice) {
                            if (arg->type == AST_LITERAL_STRING) {
                                fprintf(out, "%.*s", (int)arg->as.string.value.length, arg->as.string.value.start);
                            } else {
                                fprintf(out, "(char*)(");
                                generate_node(arg, out);
                                fprintf(out, ").ptr");
                            }
                        } else {
                            generate_node(arg, out);
                        }
                        if (i < node->as.func_call.arg_count - 1) fprintf(out, ", ");
                    }
                    fprintf(out, ")");
                } else if (gen_is_struct(name) || (callee->symbol && callee->symbol->kind == SYM_STRUCT)) {
                    fprintf(out, "%.*s_new(", (int)name.length, name.start);
                    for (size_t i = 0; i < node->as.func_call.arg_count; i++) {
                        generate_node(node->as.func_call.args[i], out);
                        if (i < node->as.func_call.arg_count - 1) fprintf(out, ", ");
                    }
                    fprintf(out, ")");
                } else {
                    fprintf(out, "%.*s(", (int)name.length, name.start);
                    for (size_t i = 0; i < node->as.func_call.arg_count; i++) {
                        generate_node(node->as.func_call.args[i], out);
                        if (i < node->as.func_call.arg_count - 1) fprintf(out, ", ");
                    }
                    fprintf(out, ")");
                }
            }
            break;
        }

        case AST_MEMBER_ACCESS: {
            ASTNode* obj = node->as.member_access.object;
            Token member = node->as.member_access.member;

            // Support $obj.tag mapping to kind
            if (obj->type == AST_UNARY_EXPR && obj->as.unary.op == TOKEN_DOLLAR) {
                if (member.length == 3 && strncmp(member.start, "tag", 3) == 0) {
                    generate_node(obj->as.unary.right, out);
                    fprintf(out, ".kind");
                    return;
                }
            }
            
            if (obj->type == AST_IDENTIFIER && gen_is_struct(obj->as.identifier.name)) {
                fprintf(out, "%.*s_%.*s", (int)obj->as.identifier.name.length, obj->as.identifier.name.start, (int)member.length, member.start);
                return;
            }

            if (obj->symbol && obj->symbol->kind == SYM_NAMESPACE) {
                fprintf(out, "%.*s", (int)member.length, member.start);
                return;
            }
            
            const char* obj_type = (obj->type == AST_IDENTIFIER) ? gen_get_type(obj->as.identifier.name) : NULL;
            if (obj_type && gen_is_enum_str(obj_type)) {
                if (member.length == 5 && strncmp(member.start, "value", 5) == 0) {
                    generate_node(obj, out);
                    return;
                }
                generate_node(obj, out);
                fprintf(out, "_%.*s", (int)member.length, member.start);
                return;
            }

            generate_node(obj, out);
            bool is_ptr = (obj->evaluated_type && obj->evaluated_type->kind == TYPE_POINTER);
            if (obj->type == AST_IDENTIFIER && obj->as.identifier.name.length == 4 && strncmp(obj->as.identifier.name.start, "self", 4) == 0) is_ptr = true;
            fprintf(out, "%s%.*s", is_ptr ? "->" : ".", (int)member.length, member.start);
            break;
        }

        case AST_NEW_EXPR: {
            ASTNode* val = node->as.new_expr.value;
            if (val->type == AST_LITERAL_NUMBER) {
                fprintf(out, "({ void* _p = malloc(sizeof(");
                generate_type(val->evaluated_type, out);
                fprintf(out, ")); *(");
                generate_type(val->evaluated_type, out);
                fprintf(out, "*)_p = ");
                generate_node(val, out);
                fprintf(out, "; (");
                generate_type(val->evaluated_type, out);
                fprintf(out, "*)_p; })");
            } else if (val->type == AST_ARRAY_LITERAL) {
                fprintf(out, "({ void* _p = malloc(sizeof(");
                generate_type(val->as.array_literal.type->as.array.element, out);
                fprintf(out, ") * %zu); ", val->as.array_literal.element_count);
                fprintf(out, "memcpy(_p, &(");
                generate_node(val, out);
                fprintf(out, "), sizeof(");
                generate_type(val->as.array_literal.type->as.array.element, out);
                fprintf(out, ") * %zu); (", val->as.array_literal.element_count);
                generate_type(val->as.array_literal.type->as.array.element, out);
                fprintf(out, "*)_p; })");
            } else {
                TypeExpr* pointee = node->evaluated_type ? node->evaluated_type->as.pointer.element : val->evaluated_type;
                fprintf(out, "({ ");
                generate_type(pointee, out);
                fprintf(out, "* _p = malloc(sizeof(");
                generate_type(pointee, out);
                fprintf(out, ")); *_p = ");
                generate_node(val, out);
                fprintf(out, "; _p; })");
            }
            break;
        }

        case AST_IF_STMT:
            fprintf(out, "if (");
            generate_node(node->as.if_stmt.condition, out);
            fprintf(out, ") ");
            generate_node(node->as.if_stmt.then_branch, out);
            if (node->as.if_stmt.else_branch) {
                fprintf(out, " else ");
                generate_node(node->as.if_stmt.else_branch, out);
            }
            break;

        case AST_SWITCH_STMT:
            fprintf(out, "{\n");
            fprintf(out, "bool _switch_matched_%d = false;\n", gen_binding_temp_id);
            for (size_t i = 0; i < node->as.switch_stmt.case_count; i++) {
                ASTNode* case_node = node->as.switch_stmt.cases[i];
                fprintf(out, "%sif (!_switch_matched_%d && (", i == 0 ? "" : "else ", gen_binding_temp_id);
                generate_match_condition(node->as.switch_stmt.expression, case_node->as.switch_case.pattern, out);
                fprintf(out, ")) { _switch_matched_%d = true; ", gen_binding_temp_id);
                generate_node(case_node->as.switch_case.body, out);
                fprintf(out, " } ");
            }
            if (node->as.switch_stmt.else_branch) {
                fprintf(out, "else ");
                generate_node(node->as.switch_stmt.else_branch, out);
            }
            fprintf(out, "\n}");
            gen_binding_temp_id++;
            break;

        case AST_SWITCH_CASE:
            generate_node(node->as.switch_case.body, out);
            break;

        case AST_WHILE_STMT:
            fprintf(out, "while (");
            generate_node(node->as.while_stmt.condition, out);
            fprintf(out, ") ");
            generate_node(node->as.while_stmt.body, out);
            break;

        case AST_FOR_STMT:
            if (node->as.for_stmt.iterable->type == AST_RANGE_EXPR) {
                ASTNode* range = node->as.for_stmt.iterable;
                ASTNode* binding = node->as.for_stmt.pattern;
                if (binding->type == AST_BINDING_LIST) {
                    // Tuple bindings for iterators are planned, but range-based for currently
                    // only supports a single binding variable in codegen.
                    binding = (binding->as.binding_list.count > 0) ? binding->as.binding_list.items[0] : NULL;
                }
                if (!binding || binding->type != AST_PATTERN) break;
                fprintf(out, "for (int64_t %.*s = ", 
                        (int)binding->as.pattern.name.length, 
                        binding->as.pattern.name.start);
                generate_node(range->as.range.start, out);
                fprintf(out, "; %.*s < ", 
                        (int)binding->as.pattern.name.length, 
                        binding->as.pattern.name.start);
                generate_node(range->as.range.end, out);
                fprintf(out, "; %.*s++) ", 
                        (int)binding->as.pattern.name.length, 
                        binding->as.pattern.name.start);
                generate_node(node->as.for_stmt.body, out);
            }
            break;

        case AST_BINDING_ASSIGN: {
            char temp_name[64];
            snprintf(temp_name, sizeof(temp_name), "_binding_tmp_%d", gen_binding_temp_id++);
            fprintf(out, "{ ");
            generate_decl_with_name(node->as.binding_assign.value->evaluated_type, temp_name, out);
            fprintf(out, " = ");
            generate_node(node->as.binding_assign.value, out);
            fprintf(out, "; ");
            for (size_t i = 0; i < node->as.binding_assign.bindings->as.binding_list.count; i++) {
                ASTNode* binding = node->as.binding_assign.bindings->as.binding_list.items[i];
                fprintf(out, "%.*s = ", (int)binding->as.pattern.name.length, binding->as.pattern.name.start);
                generate_tuple_field_ref(out, temp_name, node->as.binding_assign.value->evaluated_type, i);
                fprintf(out, "; ");
            }
            fprintf(out, "}");
            break;
        }

        case AST_RETURN_STMT:
            fprintf(out, "return");
            if (node->as.return_stmt.expression &&
                !(node->as.return_stmt.expression->evaluated_type &&
                  node->as.return_stmt.expression->evaluated_type->kind == TYPE_BASE &&
                  node->as.return_stmt.expression->evaluated_type->as.base_type.length == 2 &&
                  strncmp(node->as.return_stmt.expression->evaluated_type->as.base_type.start, "()", 2) == 0)) {
                fprintf(out, " ");
                generate_node(node->as.return_stmt.expression, out);
            }
            break;

        case AST_STRUCT_DECL:
        case AST_UNION_DECL:
            break;

        case AST_STRUCT_INIT_EXPR:
            fprintf(out, "(%.*s){", (int)node->as.struct_init.type->as.base_type.length, node->as.struct_init.type->as.base_type.start);
            for (size_t i = 0; i < node->as.struct_init.field_count; i++) {
                fprintf(out, ".%.*s = ", (int)node->as.struct_init.field_names[i].length, node->as.struct_init.field_names[i].start);
                generate_node(node->as.struct_init.field_values[i], out);
                if (i < node->as.struct_init.field_count - 1) fprintf(out, ", ");
            }
            fprintf(out, "}");
            break;

        case AST_UNARY_EXPR:
            if (node->as.unary.op == TOKEN_STAR) {
                fprintf(out, "(*");
                generate_node(node->as.unary.right, out);
                fprintf(out, ")");
            } else if (node->as.unary.op == TOKEN_MINUS) {
                fprintf(out, "(-");
                generate_node(node->as.unary.right, out);
                fprintf(out, ")");
            } else if (node->as.unary.op == TOKEN_BANG) {
                fprintf(out, "(!");
                generate_node(node->as.unary.right, out);
                fprintf(out, ")");
            } else if (node->as.unary.op == TOKEN_DOLLAR) {
                generate_node(node->as.unary.right, out);
            }
            break;

        case AST_INDEX_EXPR:
            if (node->as.index_expr.index->type == AST_RANGE_EXPR &&
                node->as.index_expr.object->evaluated_type &&
                node->as.index_expr.object->evaluated_type->kind == TYPE_ARRAY) {
                fprintf(out, "({ Slice_");
                generate_type(node->as.index_expr.object->evaluated_type->as.array.element, out);
                fprintf(out, " _s; _s.ptr = ");
                generate_node(node->as.index_expr.object, out);
                fprintf(out, " + ");
                if (node->as.index_expr.index->as.range.start) generate_node(node->as.index_expr.index->as.range.start, out);
                else fprintf(out, "0");
                fprintf(out, "; _s.length = ");
                if (node->as.index_expr.index->as.range.end) generate_node(node->as.index_expr.index->as.range.end, out);
                else if (node->as.index_expr.object->evaluated_type->as.array.length) generate_node(node->as.index_expr.object->evaluated_type->as.array.length, out);
                else fprintf(out, "0");
                fprintf(out, " - ");
                if (node->as.index_expr.index->as.range.start) generate_node(node->as.index_expr.index->as.range.start, out);
                else fprintf(out, "0");
                fprintf(out, "; _s; })");
                return;
            }
            if (node->as.index_expr.object->evaluated_type && 
                node->as.index_expr.object->evaluated_type->kind == TYPE_SLICE) {
                if (node->as.index_expr.index->type == AST_RANGE_EXPR) {
                    fprintf(out, "({ Slice_");
                    generate_type(node->as.index_expr.object->evaluated_type->as.slice.element, out);
                    fprintf(out, " _s; _s.ptr = ");
                    generate_node(node->as.index_expr.object, out);
                    fprintf(out, ".ptr + ");
                    if (node->as.index_expr.index->as.range.start) generate_node(node->as.index_expr.index->as.range.start, out);
                    else fprintf(out, "0");
                    fprintf(out, "; _s.length = ");
                    if (node->as.index_expr.index->as.range.end) generate_node(node->as.index_expr.index->as.range.end, out);
                    else { generate_node(node->as.index_expr.object, out); fprintf(out, ".length"); }
                    fprintf(out, " - ");
                    if (node->as.index_expr.index->as.range.start) generate_node(node->as.index_expr.index->as.range.start, out);
                    else fprintf(out, "0");
                    fprintf(out, "; _s; })");
                    return;
                }
                generate_node(node->as.index_expr.object, out);
                fprintf(out, ".ptr[");
                generate_node(node->as.index_expr.index, out);
                fprintf(out, "]");
                return;
            }
            generate_node(node->as.index_expr.object, out);
            fprintf(out, "[");
            if (node->as.index_expr.index->type == AST_RANGE_EXPR) {
                ASTNode* range = node->as.index_expr.index;
                if (!range->as.range.start && !range->as.range.end) fprintf(out, "0");
                else generate_node(node->as.index_expr.index, out);
            } else {
                generate_node(node->as.index_expr.index, out);
            }
            fprintf(out, "]");
            break;

        case AST_ARRAY_LITERAL:
            if (node->as.array_literal.type) {
                fprintf(out, "(");
                generate_type(node->as.array_literal.type->as.array.element, out);
                fprintf(out, "[%zu])", node->as.array_literal.element_count);
            }
            fprintf(out, "{");
            for (size_t i = 0; i < node->as.array_literal.element_count; i++) {
                generate_node(node->as.array_literal.elements[i], out);
                if (i < node->as.array_literal.element_count - 1) fprintf(out, ", ");
            }
            fprintf(out, "}");
            break;

        case AST_ENUM_ACCESS:
            if (node->as.enum_access.enum_name.length > 0) {
                fprintf(out, "%.*s_", (int)node->as.enum_access.enum_name.length, node->as.enum_access.enum_name.start);
            } else {
                bool found = false;
                for (int i = (int)gen_enum_count - 1; i >= 0; i--) {
                    for (size_t j = 0; j < gen_enums[i].member_count; j++) {
                        if (strncmp(gen_enums[i].members[j], node->as.enum_access.member_name.start, node->as.enum_access.member_name.length) == 0 &&
                            gen_enums[i].members[j][node->as.enum_access.member_name.length] == '\0') {
                            fprintf(out, "%s_", gen_enums[i].name);
                            found = true;
                            break;
                        }
                    }
                    if (found) break;
                }
            }
            fprintf(out, "%.*s", (int)node->as.enum_access.member_name.length, node->as.enum_access.member_name.start);
            break;

        case AST_BLOCK: {
            fprintf(out, "{\n");
            if (!gen_printed_syms) {
                gen_printed_syms = true;
                for (size_t i = 0; i < gen_sym_count; i++) {
                    if (!gen_sym_table[i].is_param) {
                        fprintf(out, "    ");
                        generate_decl_with_name(gen_sym_table[i].type_expr, gen_sym_table[i].name, out);
                        fprintf(out, ";\n");
                    }
                }
            }
            for (size_t i = 0; i < node->as.block.count; i++) {
                generate_node(node->as.block.statements[i], out);
                if (node->as.block.statements[i]->type != AST_BLOCK && 
                    node->as.block.statements[i]->type != AST_IF_STMT &&
                    node->as.block.statements[i]->type != AST_SWITCH_STMT &&
                    node->as.block.statements[i]->type != AST_WHILE_STMT &&
                    node->as.block.statements[i]->type != AST_FOR_STMT) {
                    fprintf(out, ";\n");
                }
            }
            fprintf(out, "}\n");
            break;
        }

        case AST_BREAK_STMT: fprintf(out, "break"); break;
        case AST_CONTINUE_STMT: fprintf(out, "continue"); break;
        case AST_BINDING_LIST:
            break;
        case AST_IMPORT:
            break;
        default: break;
    }
}

void generate_c_code(ASTNode* root, const char* out_path, bool is_main) {
    gen_sym_count = 0;
    gen_enum_count = 0;
    gen_struct_count = 0;
    gen_union_count = 0;
    gen_tuple_variant_count = 0;
    gen_tuple_type_count = 0;
    gen_printed_syms = false;
    gen_is_local = false;
    gen_binding_temp_id = 0;
    current_struct_context = NULL;
    current_struct_field_count = 0;
    gen_collect_tuple_types_from_node(root);

    char h_path[PATH_MAX];
    strncpy(h_path, out_path, PATH_MAX);
    char* dot_c = strstr(h_path, ".c");
    if (dot_c) memcpy(dot_c, ".h", 2);
    else strncat(h_path, ".h", PATH_MAX - strlen(h_path) - 1);

    FILE* h_out = fopen(h_path, "w");
    if (h_out) { generate_declarations(root, h_out, h_path); fclose(h_out); }

    FILE* c_out = fopen(out_path, "w");
    if (!c_out) return;
    
    char* last_slash = strrchr(h_path, '/');
    char* header_filename = last_slash ? last_slash + 1 : h_path;
    fprintf(c_out, "#include \"%s\"\n\n", header_filename);

    generate_implementations(root, c_out, is_main, true, true);
    fclose(c_out);
    printf("Successfully generated C/H code at: %s\n", out_path);
}

static void generate_declarations(ASTNode* root, FILE* out, const char* out_path) {
    char guard[512];
    snprintf(guard, sizeof(guard), "JIANG_GEN_");
    for (size_t i = 0; out_path[i] != '\0'; i++) {
        char c = out_path[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) guard[10 + i] = (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
        else guard[10 + i] = '_';
        guard[11 + i] = '\0';
    }
    fprintf(out, "#ifndef %s\n#define %s\n\n", guard, guard);
    
    char runtime_path[512];
    char cwd[512];
    if (getcwd(cwd, sizeof(cwd)) != NULL) snprintf(runtime_path, sizeof(runtime_path), "%s/include/runtime.h", cwd);
    else strcpy(runtime_path, "runtime.h");
    fprintf(out, "#include \"%s\"\n\n", runtime_path);

    for (size_t i = 0; i < gen_tuple_type_count; i++) {
        TypeExpr* type = gen_tuple_types[i].type;
        fprintf(out, "typedef struct { ");
        for (size_t j = 0; j < type->as.tuple.count; j++) {
            generate_type(type->as.tuple.elements[j], out);
            if (type->as.tuple.names && type->as.tuple.names[j].length > 0) {
                fprintf(out, " %.*s; ", (int)type->as.tuple.names[j].length, type->as.tuple.names[j].start);
            } else {
                fprintf(out, " _%zu; ", j);
            }
        }
        fprintf(out, "} %s;\n", gen_tuple_types[i].name);
    }
    if (gen_tuple_type_count > 0) fprintf(out, "\n");

    for (size_t i = 0; i < root->as.block.count; i++) {
        ASTNode* node = root->as.block.statements[i];
        if (node->type == AST_STRUCT_DECL) gen_add_struct(node->as.struct_decl.name);
        else if (node->type == AST_ENUM_DECL) {
            gen_add_enum(node);
            fprintf(out, "typedef enum {\n");
            for (size_t j = 0; j < node->as.enum_decl.member_count; j++) {
                fprintf(out, "    %.*s_%.*s", (int)node->as.enum_decl.name.length, node->as.enum_decl.name.start, (int)node->as.enum_decl.member_names[j].length, node->as.enum_decl.member_names[j].start);
                if (node->as.enum_decl.member_values[j]) { fprintf(out, " = "); generate_node(node->as.enum_decl.member_values[j], out); }
                if (j < node->as.enum_decl.member_count - 1) fprintf(out, ",\n");
            }
            fprintf(out, "\n} %.*s;\n\n", (int)node->as.enum_decl.name.length, node->as.enum_decl.name.start);
        } else if (node->type == AST_UNION_DECL) {
            gen_add_struct(node->as.union_decl.name);
            gen_add_union(node->as.union_decl.name, node->as.union_decl.tag_enum);
        }
    }

    for (size_t i = 0; i < root->as.block.count; i++) {
        ASTNode* node = root->as.block.statements[i];
        if (node->type == AST_STRUCT_DECL) {
            fprintf(out, "typedef struct { ");
            for (size_t j = 0; j < node->as.struct_decl.member_count; j++) {
                if (node->as.struct_decl.members[j]->type == AST_VAR_DECL) {
                    ASTNode* m = node->as.struct_decl.members[j];
                    generate_type(m->as.var_decl.type, out);
                    fprintf(out, " %.*s; ", (int)m->as.var_decl.name.length, m->as.var_decl.name.start);
                }
            }
            fprintf(out, "} %.*s;\n", (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start);
            fprintf(out, "%.*s %.*s_new(", (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start, (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start);
            for (size_t j = 0; j < node->as.struct_decl.member_count; j++) {
                if (node->as.struct_decl.members[j]->type == AST_INIT_DECL) {
                    ASTNode* init = node->as.struct_decl.members[j];
                    for (size_t k = 0; k < init->as.init_decl.param_count; k++) {
                        generate_type(init->as.init_decl.params[k]->as.var_decl.type, out);
                        fprintf(out, " %.*s", (int)init->as.init_decl.params[k]->as.var_decl.name.length, init->as.init_decl.params[k]->as.var_decl.name.start);
                        if (k < init->as.init_decl.param_count - 1) fprintf(out, ", ");
                    }
                }
            }
            fprintf(out, ");\n");
            for (size_t j = 0; j < node->as.struct_decl.member_count; j++) {
                ASTNode* member = node->as.struct_decl.members[j];
                if (member->type == AST_FUNC_DECL && member->is_public) {
                    generate_type(member->as.func_decl.return_type, out);
                    fprintf(out, " %.*s_%.*s(", (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start,
                            (int)member->as.func_decl.name.length, member->as.func_decl.name.start);
                    if (!gen_is_static_struct_method(member)) {
                        fprintf(out, "%.*s* self", (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start);
                        if (member->as.func_decl.param_count > 0) fprintf(out, ", ");
                    }
                    for (size_t k = 0; k < member->as.func_decl.param_count; k++) {
                        generate_type(member->as.func_decl.params[k]->as.var_decl.type, out);
                        fprintf(out, " %.*s", (int)member->as.func_decl.params[k]->as.var_decl.name.length,
                                member->as.func_decl.params[k]->as.var_decl.name.start);
                        if (k < member->as.func_decl.param_count - 1) fprintf(out, ", ");
                    }
                    fprintf(out, ");\n");
                }
            }
        } else if (node->type == AST_UNION_DECL) {
            if (node->as.union_decl.tag_enum.length == 0) {
                fprintf(out, "typedef enum {\n");
                for (size_t j = 0; j < node->as.union_decl.member_count; j++) {
                    ASTNode* m = node->as.union_decl.members[j];
                    fprintf(out, "    %.*sTag_%.*s", (int)node->as.union_decl.name.length, node->as.union_decl.name.start,
                            (int)m->as.var_decl.name.length, m->as.var_decl.name.start);
                    if (j < node->as.union_decl.member_count - 1) fprintf(out, ",\n");
                }
                fprintf(out, "\n} %.*sTag;\n", (int)node->as.union_decl.name.length, node->as.union_decl.name.start);
            }
            // Forward declare types for tuple variants
            for (size_t j = 0; j < node->as.union_decl.member_count; j++) {
                ASTNode* m = node->as.union_decl.members[j];
                if (m->as.var_decl.type->kind == TYPE_TUPLE) {
                    char u_name[64], v_name[64];
                    snprintf(u_name, 64, "%.*s", (int)node->as.union_decl.name.length, node->as.union_decl.name.start);
                    snprintf(v_name, 64, "%.*s", (int)m->as.var_decl.name.length, m->as.var_decl.name.start);
                    gen_add_tuple_variant(u_name, v_name);
                    gen_set_tuple_variant_fields(u_name, v_name, m->as.var_decl.type);

                    fprintf(out, "typedef ");
                    generate_type(m->as.var_decl.type, out);
                    fprintf(out, " %s_%s_t;\n", u_name, v_name);
                }
            }

            fprintf(out, "typedef struct { ");
            if (node->as.union_decl.tag_enum.length > 0) {
                fprintf(out, "%.*s", (int)node->as.union_decl.tag_enum.length, node->as.union_decl.tag_enum.start);
            } else {
                fprintf(out, "int");
            }
            fprintf(out, " kind; union { ");
            for (size_t j = 0; j < node->as.union_decl.member_count; j++) {
                ASTNode* m = node->as.union_decl.members[j];
                if (m->as.var_decl.type->kind == TYPE_TUPLE) {
                    fprintf(out, "%.*s_%.*s_t %.*s; ", (int)node->as.union_decl.name.length, node->as.union_decl.name.start, (int)m->as.var_decl.name.length, m->as.var_decl.name.start, (int)m->as.var_decl.name.length, m->as.var_decl.name.start);
                } else {
                    TypeExpr* t = m->as.var_decl.type;
                    if (t->kind == TYPE_BASE && t->as.base_type.length == 2 && strncmp(t->as.base_type.start, "()", 2) == 0) {
                        // Tag only, no value field needed
                    } else {
                        generate_type(t, out);
                        fprintf(out, " %.*s; ", (int)m->as.var_decl.name.length, m->as.var_decl.name.start);
                    }
                }
            }
            fprintf(out, "}; } %.*s;\n", (int)node->as.union_decl.name.length, node->as.union_decl.name.start);
            for (size_t j = 0; j < node->as.union_decl.member_count; j++) {
                ASTNode* m = node->as.union_decl.members[j];
                Token tagName = {0};
                if (node->as.union_decl.tag_enum.length > 0) {
                    for (size_t k = 0; k < root->as.block.count; k++) {
                        ASTNode* maybeEnum = root->as.block.statements[k];
                        if (maybeEnum->type == AST_ENUM_DECL && maybeEnum->as.enum_decl.name.length == node->as.union_decl.tag_enum.length && strncmp(maybeEnum->as.enum_decl.name.start, node->as.union_decl.tag_enum.start, node->as.union_decl.tag_enum.length) == 0) {
                            for (size_t e = 0; e < maybeEnum->as.enum_decl.member_count; e++) {
                                if (maybeEnum->as.enum_decl.member_names[e].length == m->as.var_decl.name.length && strncmp(maybeEnum->as.enum_decl.member_names[e].start, m->as.var_decl.name.start, m->as.var_decl.name.length) == 0) { tagName = maybeEnum->as.enum_decl.member_names[e]; break; }
                            }
                            break;
                        }
                    }
                }

                fprintf(out, "static inline %.*s %.*s_%.*s(", (int)node->as.union_decl.name.length, node->as.union_decl.name.start, (int)node->as.union_decl.name.length, node->as.union_decl.name.start, (int)m->as.var_decl.name.length, m->as.var_decl.name.start);
                TypeExpr* t = m->as.var_decl.type;
                bool is_void = (t->kind == TYPE_BASE && t->as.base_type.length == 2 && strncmp(t->as.base_type.start, "()", 2) == 0);

                if (m->as.var_decl.type->kind == TYPE_TUPLE) {
                    fprintf(out, "%.*s_%.*s_t val", (int)node->as.union_decl.name.length, node->as.union_decl.name.start, (int)m->as.var_decl.name.length, m->as.var_decl.name.start);
                } else if (!is_void) {
                    generate_type(m->as.var_decl.type, out);
                    fprintf(out, " val");
                }
                fprintf(out, ") { %.*s _u; ", (int)node->as.union_decl.name.length, node->as.union_decl.name.start);
                if (tagName.length > 0) fprintf(out, "_u.kind = %.*s_%.*s; ", (int)node->as.union_decl.tag_enum.length, node->as.union_decl.tag_enum.start, (int)tagName.length, tagName.start);
                else if (node->as.union_decl.tag_enum.length == 0) fprintf(out, "_u.kind = %.*sTag_%.*s; ", (int)node->as.union_decl.name.length, node->as.union_decl.name.start, (int)m->as.var_decl.name.length, m->as.var_decl.name.start);
                else fprintf(out, "_u.kind = %zu; ", j);
                if (!is_void) {
                    fprintf(out, "_u.%.*s = val; ", (int)m->as.var_decl.name.length, m->as.var_decl.name.start);
                }
                fprintf(out, "return _u; }\n");
            }
        } else if (node->type == AST_FUNC_DECL) {
            generate_type(node->as.func_decl.return_type, out);
            fprintf(out, " %.*s(", (int)node->as.func_decl.name.length, node->as.func_decl.name.start);
            for (size_t j = 0; j < node->as.func_decl.param_count; j++) {
                generate_type(node->as.func_decl.params[j]->as.var_decl.type, out);
                fprintf(out, " %.*s", (int)node->as.func_decl.params[j]->as.var_decl.name.length, node->as.func_decl.params[j]->as.var_decl.name.start);
                if (j < node->as.func_decl.param_count - 1) fprintf(out, ", ");
            }
            fprintf(out, ");\n");
        } else if (node->type == AST_VAR_DECL) {
            fprintf(out, "extern ");
            generate_type(node->as.var_decl.type, out);
            fprintf(out, " %.*s;\n", (int)node->as.var_decl.name.length, node->as.var_decl.name.start);
        } else if (node->type == AST_IMPORT) {
            if (node->as.import_decl.resolved_path[0] == '\0') {
                continue;
            }
            char path[256];
            strncpy(path, node->as.import_decl.resolved_path, 256);
            char* dot_c = strstr(path, ".c");
            if (dot_c) memcpy(dot_c, ".h", 2);
            char* base = strrchr(path, '/');
            fprintf(out, "#include \"%s\"\n", base ? base + 1 : path);
        }
    }
    fprintf(out, "\n#endif\n");
}

static void gen_collect_patterns(ASTNode* node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_PATTERN:
            gen_add_sym(node->as.pattern.name,
                        node->evaluated_type ? node->evaluated_type :
                        (node->symbol && node->symbol->type ? node->symbol->type : &gen_pattern_fallback_type),
                        false);
            break;
        case AST_PROGRAM:
        case AST_BLOCK:
            for (size_t i = 0; i < node->as.block.count; i++) gen_collect_patterns(node->as.block.statements[i]);
            break;
        case AST_BINDING_LIST:
            for (size_t i = 0; i < node->as.binding_list.count; i++) gen_collect_patterns(node->as.binding_list.items[i]);
            break;
        case AST_BINDING_ASSIGN:
            gen_collect_patterns(node->as.binding_assign.bindings);
            gen_collect_patterns(node->as.binding_assign.value);
            break;
        case AST_TUPLE_LITERAL:
            for (size_t i = 0; i < node->as.tuple_literal.count; i++) gen_collect_patterns(node->as.tuple_literal.elements[i]);
            break;
        case AST_BINARY_EXPR:
            gen_collect_patterns(node->as.binary.left);
            gen_collect_patterns(node->as.binary.right);
            break;
        case AST_UNARY_EXPR:
            gen_collect_patterns(node->as.unary.right);
            break;
        case AST_VAR_DECL: {
            TypeExpr* type = node->as.var_decl.type;
            if (!type || (type->kind == TYPE_BASE && type->as.base_type.length == 1 && type->as.base_type.start[0] == '_')) {
                type = node->evaluated_type;
            }
            gen_add_sym(node->as.var_decl.name, type, false);
            gen_collect_patterns(node->as.var_decl.initializer);
            break;
        }
        case AST_FUNC_CALL:
            gen_collect_patterns(node->as.func_call.callee);
            for (size_t i = 0; i < node->as.func_call.arg_count; i++) gen_collect_patterns(node->as.func_call.args[i]);
            break;
        case AST_IF_STMT:
            gen_collect_patterns(node->as.if_stmt.condition);
            gen_collect_patterns(node->as.if_stmt.then_branch);
            gen_collect_patterns(node->as.if_stmt.else_branch);
            break;
        case AST_SWITCH_STMT:
            gen_collect_patterns(node->as.switch_stmt.expression);
            for (size_t i = 0; i < node->as.switch_stmt.case_count; i++) gen_collect_patterns(node->as.switch_stmt.cases[i]);
            gen_collect_patterns(node->as.switch_stmt.else_branch);
            break;
        case AST_SWITCH_CASE:
            gen_collect_patterns(node->as.switch_case.pattern);
            gen_collect_patterns(node->as.switch_case.body);
            break;
        case AST_WHILE_STMT:
            gen_collect_patterns(node->as.while_stmt.condition);
            gen_collect_patterns(node->as.while_stmt.body);
            break;
        case AST_FOR_STMT:
            gen_collect_patterns(node->as.for_stmt.pattern);
            gen_collect_patterns(node->as.for_stmt.iterable);
            gen_collect_patterns(node->as.for_stmt.body);
            break;
        case AST_RETURN_STMT:
            gen_collect_patterns(node->as.return_stmt.expression);
            break;
        case AST_STRUCT_INIT_EXPR:
            for (size_t i = 0; i < node->as.struct_init.field_count; i++) gen_collect_patterns(node->as.struct_init.field_values[i]);
            break;
        case AST_INDEX_EXPR:
            gen_collect_patterns(node->as.index_expr.object);
            gen_collect_patterns(node->as.index_expr.index);
            break;
        case AST_ARRAY_LITERAL:
            for (size_t i = 0; i < node->as.array_literal.element_count; i++) gen_collect_patterns(node->as.array_literal.elements[i]);
            break;
        default: break;
    }
}

static void generate_implementations(ASTNode* root, FILE* out, bool is_main, bool emit_free_functions, bool emit_main_body) {
    for (size_t i = 0; i < root->as.block.count; i++) {
        ASTNode* node = root->as.block.statements[i];
        if (node->type == AST_FUNC_DECL && emit_free_functions && !gen_should_emit_jir_for_ast_function(node)) {
            gen_sym_count = 0; // Local scope for function
            gen_is_local = true;
            gen_collect_patterns(node->as.func_decl.body);
            for (size_t j = 0; j < node->as.func_decl.param_count; j++) {
                TypeExpr* t = node->as.func_decl.params[j]->as.var_decl.type;
                if (!t || (t->kind == TYPE_BASE && t->as.base_type.length == 1 && t->as.base_type.start[0] == '_')) t = node->as.func_decl.params[j]->evaluated_type;
                gen_add_sym(node->as.func_decl.params[j]->as.var_decl.name, t, true);
            }
            generate_type(node->as.func_decl.return_type, out);
            fprintf(out, " %.*s(", (int)node->as.func_decl.name.length, node->as.func_decl.name.start);
            for (size_t j = 0; j < node->as.func_decl.param_count; j++) {
                TypeExpr* t = node->as.func_decl.params[j]->as.var_decl.type;
                if (!t || (t->kind == TYPE_BASE && t->as.base_type.length == 1 && t->as.base_type.start[0] == '_')) t = node->as.func_decl.params[j]->evaluated_type;
                generate_type(t, out);
                fprintf(out, " %.*s", (int)node->as.func_decl.params[j]->as.var_decl.name.length, node->as.func_decl.params[j]->as.var_decl.name.start);
                if (j < node->as.func_decl.param_count - 1) fprintf(out, ", ");
            }
            fprintf(out, ") ");
            gen_printed_syms = false;
            generate_node(node->as.func_decl.body, out);
            gen_is_local = false;
            fprintf(out, "\n\n");
        } else if (node->type == AST_STRUCT_DECL) {
            current_struct_field_count = 0;
            for (size_t j = 0; j < node->as.struct_decl.member_count; j++) {
                if (node->as.struct_decl.members[j]->type == AST_VAR_DECL) {
                    snprintf(current_struct_fields[current_struct_field_count++].name, 64, "%.*s", (int)node->as.struct_decl.members[j]->as.var_decl.name.length, node->as.struct_decl.members[j]->as.var_decl.name.start);
                }
            }
            for (size_t j = 0; j < node->as.struct_decl.member_count; j++) {
                ASTNode* m = node->as.struct_decl.members[j];
                if (m->type == AST_FUNC_DECL) {
                    gen_sym_count = 0;
                    gen_is_local = true;
                    gen_collect_patterns(m->as.func_decl.body);
                    current_struct_context = gen_is_static_struct_method(m) ? NULL : "struct";
                    for (size_t k = 0; k < m->as.func_decl.param_count; k++) {
                        TypeExpr* t = m->as.func_decl.params[k]->as.var_decl.type;
                        if (!t || (t->kind == TYPE_BASE && t->as.base_type.length == 1 && t->as.base_type.start[0] == '_')) t = m->as.func_decl.params[k]->evaluated_type;
                        gen_add_sym(m->as.func_decl.params[k]->as.var_decl.name, t, true);
                    }
                    generate_type(m->as.func_decl.return_type, out);
                    fprintf(out, " %.*s_%.*s(", (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start, (int)m->as.func_decl.name.length, m->as.func_decl.name.start);
                    if (!gen_is_static_struct_method(m)) {
                        fprintf(out, "%.*s* self", (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start);
                        if (m->as.func_decl.param_count > 0) fprintf(out, ", ");
                    }
                    for (size_t k = 0; k < m->as.func_decl.param_count; k++) {
                        TypeExpr* t = m->as.func_decl.params[k]->as.var_decl.type;
                        if (!t || (t->kind == TYPE_BASE && t->as.base_type.length == 1 && t->as.base_type.start[0] == '_')) t = m->as.func_decl.params[k]->evaluated_type;
                        generate_type(t, out);
                        fprintf(out, " %.*s", (int)m->as.func_decl.params[k]->as.var_decl.name.length, m->as.func_decl.params[k]->as.var_decl.name.start);
                        if (k < m->as.func_decl.param_count - 1) fprintf(out, ", ");
                    }
                    fprintf(out, ") ");
                    gen_printed_syms = false;
                    generate_node(m->as.func_decl.body, out);
                    current_struct_context = NULL;
                    gen_is_local = false;
                    fprintf(out, "\n\n");
                } else if (m->type == AST_INIT_DECL) {
                    gen_sym_count = 0;
                    gen_is_local = true;
                    gen_collect_patterns(m->as.init_decl.body);
                    current_struct_context = "struct";
                    for (size_t k = 0; k < m->as.init_decl.param_count; k++) {
                        TypeExpr* t = m->as.init_decl.params[k]->as.var_decl.type;
                        if (!t || (t->kind == TYPE_BASE && t->as.base_type.length == 1 && t->as.base_type.start[0] == '_')) t = m->as.init_decl.params[k]->evaluated_type;
                        gen_add_sym(m->as.init_decl.params[k]->as.var_decl.name, t, true);
                    }
                    fprintf(out, "void %.*s_init(%.*s* self", (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start, (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start);
                    if (m->as.init_decl.param_count > 0) fprintf(out, ", ");
                    for (size_t k = 0; k < m->as.init_decl.param_count; k++) {
                        TypeExpr* t = m->as.init_decl.params[k]->as.var_decl.type;
                        if (!t || (t->kind == TYPE_BASE && t->as.base_type.length == 1 && t->as.base_type.start[0] == '_')) t = m->as.init_decl.params[k]->evaluated_type;
                        generate_type(t, out);
                        fprintf(out, " %.*s", (int)m->as.init_decl.params[k]->as.var_decl.name.length, m->as.init_decl.params[k]->as.var_decl.name.start);
                        if (k < m->as.init_decl.param_count - 1) fprintf(out, ", ");
                    }
                    fprintf(out, ") ");
                    gen_printed_syms = false;
                    generate_node(m->as.init_decl.body, out);
                    fprintf(out, "\n\n");
                    fprintf(out, "%.*s %.*s_new(", (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start, (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start);
                    for (size_t k = 0; k < m->as.init_decl.param_count; k++) {
                        TypeExpr* t = m->as.init_decl.params[k]->as.var_decl.type;
                        if (!t || (t->kind == TYPE_BASE && t->as.base_type.length == 1 && t->as.base_type.start[0] == '_')) t = m->as.init_decl.params[k]->evaluated_type;
                        generate_type(t, out);
                        fprintf(out, " %.*s", (int)m->as.init_decl.params[k]->as.var_decl.name.length, m->as.init_decl.params[k]->as.var_decl.name.start);
                        if (k < m->as.init_decl.param_count - 1) fprintf(out, ", ");
                    }
                    fprintf(out, ") {\n    %.*s self;\n    %.*s_init(&self", (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start, (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start);
                    for (size_t k = 0; k < m->as.init_decl.param_count; k++) fprintf(out, ", %.*s", (int)m->as.init_decl.params[k]->as.var_decl.name.length, m->as.init_decl.params[k]->as.var_decl.name.start);
                    fprintf(out, ");\n    return self;\n}\n\n");
                    current_struct_context = NULL;
                    gen_is_local = false;
                }
            }
        } else if (node->type == AST_VAR_DECL && !is_main) {
            gen_is_local = false;
            generate_node(node, out);
            fprintf(out, ";\n");
        }
    }

    if (is_main && emit_main_body) {
        gen_sym_count = 0; // Reset for main scope
        gen_is_local = true;
        gen_collect_patterns(root);
        fprintf(out, "int main() {\n");
        for (size_t i = 0; i < gen_sym_count; i++) {
            if (!gen_sym_table[i].is_param) {
                fprintf(out, "    ");
                generate_decl_with_name(gen_sym_table[i].type_expr, gen_sym_table[i].name, out);
                fprintf(out, ";\n");
            }
        }
        gen_printed_syms = true;
        fprintf(out, "    ");
        for (size_t i = 0; i < root->as.block.count; i++) {
            ASTNode* node = root->as.block.statements[i];
            if (node->type != AST_FUNC_DECL && node->type != AST_STRUCT_DECL && node->type != AST_ENUM_DECL && node->type != AST_IMPORT && node->type != AST_UNION_DECL) {
                generate_node(node, out);
                if (node->type != AST_BLOCK && node->type != AST_IF_STMT && node->type != AST_WHILE_STMT && node->type != AST_FOR_STMT) fprintf(out, ";\n    ");
            }
        }
        gen_is_local = false;
        fprintf(out, "return 0;\n}\n");
    }
}

static JirFunction* gen_find_jir_function(JirModule* mod, const char* name) {
    if (!mod) return NULL;
    for (size_t i = 0; i < mod->function_count; i++) {
        JirFunction* func = mod->functions[i];
        if ((size_t)func->name.length == strlen(name) &&
            strncmp(func->name.start, name, func->name.length) == 0) {
            return func;
        }
    }
    return NULL;
}

static bool gen_can_emit_jir_function(JirFunction* func) {
    if (!func || gen_is_void_type(func->return_type)) return false;

    for (size_t i = 0; i < func->local_count; i++) {
        TypeExpr* type = func->locals[i].type;
        if (gen_is_void_type(type)) return false;
        if (type && type->kind == TYPE_ARRAY) return false;
        if (type && type->kind == TYPE_BASE) {
            Token base = type->as.base_type;
            bool builtin =
                (base.length == 3 && strncmp(base.start, "Int", 3) == 0) ||
                (base.length == 5 && strncmp(base.start, "Float", 5) == 0) ||
                (base.length == 6 && strncmp(base.start, "Double", 6) == 0) ||
                (base.length == 4 && strncmp(base.start, "Bool", 4) == 0) ||
                (base.length == 5 && strncmp(base.start, "UInt8", 5) == 0) ||
                (base.length == 6 && strncmp(base.start, "UInt16", 6) == 0);
            if (!builtin) return false;
        }
    }

    for (size_t i = 0; i < func->inst_count; i++) {
        JirInst* inst = &func->insts[i];
        switch (inst->op) {
            case JIR_OP_STORE_PTR:
            case JIR_OP_STORE_MEMBER:
            case JIR_OP_MALLOC:
            case JIR_OP_MEMBER_ACCESS:
            case JIR_OP_DEREF:
                return false;
            case JIR_OP_CALL:
                if ((inst->payload.name.length == 5 && strncmp(inst->payload.name.start, "print", 5) == 0) ||
                    (inst->payload.name.length == 6 && strncmp(inst->payload.name.start, "assert", 6) == 0) ||
                    (inst->payload.name.length == 9 && strncmp(inst->payload.name.start, "anonymous", 9) == 0)) {
                    return false;
                }
                break;
            case JIR_OP_LOAD_SYM:
                if (inst->payload.name.length > 0) {
                    char c = inst->payload.name.start[0];
                    if (c >= 'a' && c <= 'z') return false;
                }
                break;
            default:
                break;
        }
    }
    return true;
}

static bool gen_should_emit_jir_for_ast_function(ASTNode* node) {
    if (!gen_active_jir_module || !node || node->type != AST_FUNC_DECL) return false;
    char func_name[128];
    snprintf(func_name, sizeof(func_name), "%.*s", (int)node->as.func_decl.name.length, node->as.func_decl.name.start);
    return gen_can_emit_jir_function(gen_find_jir_function(gen_active_jir_module, func_name));
}

static void gen_emit_jir_reg(FILE* out, JirFunction* func, int reg) {
    if (reg < 0 || (size_t)reg >= func->local_count) {
        fprintf(out, "0");
        return;
    }
    fprintf(out, "%s", func->locals[reg].name);
}

static void gen_emit_jir_string(FILE* out, const char* s) {
    fprintf(out, "\"");
    for (const char* p = s; *p; p++) {
        if (*p == '\\' || *p == '"') fputc('\\', out);
        if (*p == '\n') {
            fputs("\\n", out);
        } else {
            fputc(*p, out);
        }
    }
    fprintf(out, "\"");
}

static void gen_emit_jir_aggregate(FILE* out, JirFunction* func, JirInst* inst) {
    TypeExpr* type = (inst->dest >= 0 && (size_t)inst->dest < func->local_count) ? func->locals[inst->dest].type : NULL;
    if (type && type->kind == TYPE_ARRAY) {
        fprintf(out, "(");
        generate_type(type->as.array.element, out);
        fprintf(out, "[%zu]){", inst->call_arg_count);
    } else {
        fprintf(out, "(");
        generate_type(type, out);
        fprintf(out, "){");
    }
    for (size_t i = 0; i < inst->call_arg_count; i++) {
        gen_emit_jir_reg(out, func, inst->call_args[i]);
        if (i + 1 < inst->call_arg_count) fprintf(out, ", ");
    }
    fprintf(out, "}");
}

static void gen_emit_jir_member_access(FILE* out, JirFunction* func, JirInst* inst) {
    TypeExpr* object_type = (inst->src1 >= 0 && (size_t)inst->src1 < func->local_count) ? func->locals[inst->src1].type : NULL;
    object_type = object_type ? object_type : NULL;
    if (inst->payload.name.length > 0) {
        gen_emit_jir_reg(out, func, inst->src1);
        bool is_ptr = object_type && object_type->kind == TYPE_POINTER;
        fprintf(out, "%s%.*s", is_ptr ? "->" : ".", (int)inst->payload.name.length, inst->payload.name.start);
        return;
    }
    if (object_type && object_type->kind == TYPE_SLICE) {
        gen_emit_jir_reg(out, func, inst->src1);
        fprintf(out, ".ptr[");
        gen_emit_jir_reg(out, func, inst->src2);
        fprintf(out, "]");
        return;
    }
    gen_emit_jir_reg(out, func, inst->src1);
    fprintf(out, "[");
    gen_emit_jir_reg(out, func, inst->src2);
    fprintf(out, "]");
}

static void gen_emit_jir_function(FILE* out, JirFunction* func, bool is_main) {
    if (!func) return;

    if (is_main) {
        fprintf(out, "int main() {\n");
    } else {
        generate_type(func->return_type, out);
        fprintf(out, " %.*s(", (int)func->name.length, func->name.start);
        bool first = true;
        for (size_t i = 0; i < func->local_count; i++) {
            if (!func->locals[i].is_param) continue;
            if (!first) fprintf(out, ", ");
            generate_type(func->locals[i].type, out);
            fprintf(out, " %s", func->locals[i].name);
            first = false;
        }
        fprintf(out, ") {\n");
    }

    for (size_t i = 0; i < func->local_count; i++) {
        if (func->locals[i].is_param) continue;
        fprintf(out, "    ");
        generate_decl_with_name(func->locals[i].type, func->locals[i].name, out);
        fprintf(out, ";\n");
    }

    for (size_t i = 0; i < func->inst_count; i++) {
        JirInst* inst = &func->insts[i];
        switch (inst->op) {
            case JIR_OP_LABEL:
                fprintf(out, "JIR_LABEL_%u:;\n", inst->payload.label_id);
                break;
            case JIR_OP_CONST_INT:
                fprintf(out, "    %s = %lld;\n", func->locals[inst->dest].name, (long long)inst->payload.int_val);
                break;
            case JIR_OP_CONST_STR:
                fprintf(out, "    %s = (Slice_uint8_t){(uint8_t*)", func->locals[inst->dest].name);
                gen_emit_jir_string(out, inst->payload.str_val ? inst->payload.str_val : "");
                fprintf(out, ", %zu};\n", inst->payload.str_val ? strlen(inst->payload.str_val) : 0);
                break;
            case JIR_OP_LOAD_SYM:
                fprintf(out, "    %s = ", func->locals[inst->dest].name);
                if (inst->payload.name.length > 0) fprintf(out, "%.*s", (int)inst->payload.name.length, inst->payload.name.start);
                else fprintf(out, "0");
                fprintf(out, ";\n");
                break;
            case JIR_OP_STORE:
                fprintf(out, "    %s = ", func->locals[inst->dest].name);
                gen_emit_jir_reg(out, func, inst->src1);
                fprintf(out, ";\n");
                break;
            case JIR_OP_STORE_PTR:
                fprintf(out, "    *");
                gen_emit_jir_reg(out, func, inst->dest);
                fprintf(out, " = ");
                gen_emit_jir_reg(out, func, inst->src1);
                fprintf(out, ";\n");
                break;
            case JIR_OP_STORE_MEMBER:
                fprintf(out, "    ");
                gen_emit_jir_reg(out, func, inst->dest);
                if (inst->payload.name.length > 0) {
                    fprintf(out, ".%.*s", (int)inst->payload.name.length, inst->payload.name.start);
                } else {
                    fprintf(out, "[");
                    gen_emit_jir_reg(out, func, inst->src2);
                    fprintf(out, "]");
                }
                fprintf(out, " = ");
                gen_emit_jir_reg(out, func, inst->src1);
                fprintf(out, ";\n");
                break;
            case JIR_OP_ADD:
            case JIR_OP_SUB:
            case JIR_OP_MUL:
            case JIR_OP_DIV:
            case JIR_OP_CMP_EQ:
            case JIR_OP_CMP_NEQ:
            case JIR_OP_CMP_LT:
            case JIR_OP_CMP_GT:
            case JIR_OP_CMP_LTE:
            case JIR_OP_CMP_GTE: {
                const char* op = "+";
                switch (inst->op) {
                    case JIR_OP_SUB: op = "-"; break;
                    case JIR_OP_MUL: op = "*"; break;
                    case JIR_OP_DIV: op = "/"; break;
                    case JIR_OP_CMP_EQ: op = "=="; break;
                    case JIR_OP_CMP_NEQ: op = "!="; break;
                    case JIR_OP_CMP_LT: op = "<"; break;
                    case JIR_OP_CMP_GT: op = ">"; break;
                    case JIR_OP_CMP_LTE: op = "<="; break;
                    case JIR_OP_CMP_GTE: op = ">="; break;
                    default: break;
                }
                fprintf(out, "    %s = ", func->locals[inst->dest].name);
                gen_emit_jir_reg(out, func, inst->src1);
                fprintf(out, " %s ", op);
                gen_emit_jir_reg(out, func, inst->src2);
                fprintf(out, ";\n");
                break;
            }
            case JIR_OP_JUMP:
                fprintf(out, "    goto JIR_LABEL_%u;\n", inst->payload.label_id);
                break;
            case JIR_OP_JUMP_IF_FALSE:
                fprintf(out, "    if (!");
                gen_emit_jir_reg(out, func, inst->src1);
                fprintf(out, ") goto JIR_LABEL_%u;\n", inst->payload.label_id);
                break;
            case JIR_OP_CALL:
                if (inst->dest >= 0) fprintf(out, "    %s = ", func->locals[inst->dest].name);
                else fprintf(out, "    ");
                fprintf(out, "%.*s(", (int)inst->payload.name.length, inst->payload.name.start);
                for (size_t j = 0; j < inst->call_arg_count; j++) {
                    gen_emit_jir_reg(out, func, inst->call_args[j]);
                    if (j + 1 < inst->call_arg_count) fprintf(out, ", ");
                }
                fprintf(out, ");\n");
                break;
            case JIR_OP_RETURN:
                fprintf(out, "    return");
                if (!is_main && inst->src1 >= 0) {
                    fprintf(out, " ");
                    gen_emit_jir_reg(out, func, inst->src1);
                } else if (is_main) {
                    fprintf(out, " 0");
                }
                fprintf(out, ";\n");
                break;
            case JIR_OP_MALLOC:
                fprintf(out, "    %s = malloc(sizeof(", func->locals[inst->dest].name);
                if (func->locals[inst->dest].type && func->locals[inst->dest].type->kind == TYPE_POINTER) {
                    generate_type(func->locals[inst->dest].type->as.pointer.element, out);
                } else {
                    fprintf(out, "char");
                }
                fprintf(out, "));\n");
                break;
            case JIR_OP_ARRAY_INIT:
                fprintf(out, "    %s = ", func->locals[inst->dest].name);
                gen_emit_jir_aggregate(out, func, inst);
                fprintf(out, ";\n");
                break;
            case JIR_OP_MEMBER_ACCESS:
                fprintf(out, "    %s = ", func->locals[inst->dest].name);
                gen_emit_jir_member_access(out, func, inst);
                fprintf(out, ";\n");
                break;
            case JIR_OP_DEREF:
                fprintf(out, "    %s = *", func->locals[inst->dest].name);
                gen_emit_jir_reg(out, func, inst->src1);
                fprintf(out, ";\n");
                break;
            case JIR_OP_EXTRACT_FIELD:
                fprintf(out, "    %s = ", func->locals[inst->dest].name);
                generate_tuple_field_ref(out, func->locals[inst->src1].name, func->locals[inst->src1].type, (size_t)inst->payload.int_val);
                fprintf(out, ";\n");
                break;
            default:
                fprintf(out, "    /* unsupported JIR op %d */\n", (int)inst->op);
                break;
        }
    }

    if (is_main) {
        fprintf(out, "    return 0;\n");
    }
    fprintf(out, "}\n\n");
}

void generate_c_code_from_jir(JirModule* mod, ASTNode* root, const char* out_path, bool is_main) {
    gen_sym_count = 0;
    gen_enum_count = 0;
    gen_struct_count = 0;
    gen_union_count = 0;
    gen_tuple_variant_count = 0;
    gen_tuple_type_count = 0;
    gen_printed_syms = false;
    gen_is_local = false;
    gen_binding_temp_id = 0;
    current_struct_context = NULL;
    current_struct_field_count = 0;
    gen_active_jir_module = mod;
    gen_collect_tuple_types_from_node(root);

    char h_path[PATH_MAX];
    strncpy(h_path, out_path, PATH_MAX);
    char* dot_c = strstr(h_path, ".c");
    if (dot_c) memcpy(dot_c, ".h", 2);
    else strncat(h_path, ".h", PATH_MAX - strlen(h_path) - 1);

    FILE* h_out = fopen(h_path, "w");
    if (h_out) {
        generate_declarations(root, h_out, h_path);
        fclose(h_out);
    }

    FILE* c_out = fopen(out_path, "w");
    if (!c_out) return;

    char* last_slash = strrchr(h_path, '/');
    char* header_filename = last_slash ? last_slash + 1 : h_path;
    fprintf(c_out, "#include \"%s\"\n\n", header_filename);

    generate_implementations(root, c_out, is_main, true, true);

    for (size_t i = 0; i < root->as.block.count; i++) {
        ASTNode* node = root->as.block.statements[i];
        if (node->type != AST_FUNC_DECL) continue;
        char func_name[128];
        snprintf(func_name, sizeof(func_name), "%.*s", (int)node->as.func_decl.name.length, node->as.func_decl.name.start);
        JirFunction* func = gen_find_jir_function(mod, func_name);
        if (gen_can_emit_jir_function(func)) gen_emit_jir_function(c_out, func, false);
    }

    fclose(c_out);
    gen_active_jir_module = NULL;
    printf("Successfully generated C/H code from JIR at: %s\n", out_path);
}
