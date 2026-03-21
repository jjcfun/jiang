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
    snprintf(gen_unions[gen_union_count].tag_enum, 64, "%.*s", (int)tag_enum.length, tag_enum.start);
    gen_union_count++;
}

static const char* gen_get_union_tag_enum(const char* union_name) {
    for (size_t i = 0; i < gen_union_count; i++) {
        if (strcmp(gen_unions[i].name, union_name) == 0) return gen_unions[i].tag_enum;
    }
    return NULL;
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

static void generate_node(ASTNode* node, FILE* out);
static void generate_type(TypeExpr* type, FILE* out);

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
            else if (type->as.base_type.length == 6 && strncmp(type->as.base_type.start, "String", 6) == 0) fprintf(out, "char*");
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
            fprintf(out, "struct { ");
            for (size_t i = 0; i < type->as.tuple.count; i++) {
                generate_type(type->as.tuple.elements[i], out);
                if (type->as.tuple.names && type->as.tuple.names[i].length > 0) {
                    fprintf(out, " %.*s; ", (int)type->as.tuple.names[i].length, type->as.tuple.names[i].start);
                } else {
                    fprintf(out, " _%zu; ", i);
                }
            }
            fprintf(out, " }");
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
            fprintf(out, "%.*s", (int)node->as.string.value.length, node->as.string.value.start);
            break;

        case AST_BINARY_EXPR:
            if (node->as.binary.op == TOKEN_EQUAL_EQUAL) {
                // Special case: Union comparison Shape.circle or Shape.circle(...)
                ASTNode* right = node->as.binary.right;
                if (right->type == AST_MEMBER_ACCESS || (right->type == AST_FUNC_CALL && right->as.func_call.callee->type == AST_MEMBER_ACCESS)) {
                    ASTNode* ma = (right->type == AST_MEMBER_ACCESS) ? right : right->as.func_call.callee;
                    ASTNode* obj = ma->as.member_access.object;
                    if (obj->type == AST_IDENTIFIER && gen_is_struct(obj->as.identifier.name)) {
                        // Check if it's a pattern match call
                        bool has_patterns = false;
                        if (right->type == AST_FUNC_CALL) {
                            for (size_t i = 0; i < right->as.func_call.arg_count; i++) {
                                if (right->as.func_call.args[i]->type == AST_PATTERN) {
                                    has_patterns = true; break;
                                }
                            }
                        }

                        if (has_patterns) {
                            fprintf(out, "({ (");
                            generate_node(node->as.binary.left, out);
                            fprintf(out, ".kind == ");
                            
                            char u_name[64];
                            snprintf(u_name, 64, "%.*s", (int)obj->as.identifier.name.length, obj->as.identifier.name.start);
                            char v_name[64];
                            snprintf(v_name, 64, "%.*s", (int)ma->as.member_access.member.length, ma->as.member_access.member.start);
                            const char* tag_enum_name = gen_get_union_tag_enum(u_name);
                            if (tag_enum_name && tag_enum_name[0] != '\0') {
                                fprintf(out, "%s_%.*s", tag_enum_name, (int)ma->as.member_access.member.length, ma->as.member_access.member.start);
                            } else {
                                fprintf(out, "%.*s_%.*s", (int)obj->as.identifier.name.length, obj->as.identifier.name.start, (int)ma->as.member_access.member.length, ma->as.member_access.member.start);
                            }

                            fprintf(out, ") ? (");
                            int pattern_pos = 0;
                            for (size_t i = 0; i < right->as.func_call.arg_count; i++) {
                                ASTNode* arg = right->as.func_call.args[i];
                                if (arg->type == AST_PATTERN) {
                                    fprintf(out, "%.*s = ", (int)arg->as.pattern.name.length, arg->as.pattern.name.start);
                                    generate_node(node->as.binary.left, out);
                                    fprintf(out, ".%.*s", (int)ma->as.member_access.member.length, ma->as.member_access.member.start);
                                    if (arg->as.pattern.field_index >= 0 || pattern_pos > 0) {
                                        int field_index = pattern_pos;
                                        if (arg->as.pattern.field_name.length > 0) {
                                            fprintf(out, ".%.*s", (int)arg->as.pattern.field_name.length, arg->as.pattern.field_name.start);
                                        } else if (gen_is_tuple_variant(u_name, v_name)) {
                                            const char* field_name = gen_get_tuple_variant_field(u_name, v_name, field_index);
                                            if (field_name) fprintf(out, ".%s", field_name);
                                        } else {
                                            // Non-tuple union variant stores the value directly.
                                        }
                                    }
                                    fprintf(out, ", ");
                                    pattern_pos++;
                                }
                            }
                            fprintf(out, "1) : 0; })");
                            return;
                        } else {
                            // Standard tag comparison
                            fprintf(out, "(");
                            generate_node(node->as.binary.left, out);
                            fprintf(out, ".kind == ");
                            
                            char u_name[64];
                            snprintf(u_name, 64, "%.*s", (int)obj->as.identifier.name.length, obj->as.identifier.name.start);
                            const char* tag_enum_name = gen_get_union_tag_enum(u_name);
                            if (tag_enum_name && tag_enum_name[0] != '\0') {
                                fprintf(out, "%s_%.*s", tag_enum_name, (int)ma->as.member_access.member.length, ma->as.member_access.member.start);
                            } else {
                                fprintf(out, "%.*s_%.*s", (int)obj->as.identifier.name.length, obj->as.identifier.name.start, (int)ma->as.member_access.member.length, ma->as.member_access.member.start);
                            }
                            
                            fprintf(out, ")");
                            return;
                        }
                    }
                }
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

                if (obj->type == AST_IDENTIFIER && obj->symbol && obj->symbol->kind == SYM_NAMESPACE) {
                    fprintf(out, "%.*s(", (int)method.length, method.start);
                    for (size_t i = 0; i < node->as.func_call.arg_count; i++) {
                        generate_node(node->as.func_call.args[i], out);
                        if (i < node->as.func_call.arg_count - 1) fprintf(out, ", ");
                    }
                    fprintf(out, ")");
                    return;
                }
                
                if (obj->type == AST_IDENTIFIER && gen_is_struct(obj->as.identifier.name)) {
                    // Static Union/Struct Constructor
                    char u_name[64], m_name[64];
                    snprintf(u_name, 64, "%.*s", (int)obj->as.identifier.name.length, obj->as.identifier.name.start);
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
                        generate_node(node->as.func_call.args[i], out);
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

            if (obj->type == AST_IDENTIFIER && obj->symbol && obj->symbol->kind == SYM_NAMESPACE) {
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

        case AST_WHILE_STMT:
            fprintf(out, "while (");
            generate_node(node->as.while_stmt.condition, out);
            fprintf(out, ") ");
            generate_node(node->as.while_stmt.body, out);
            break;

        case AST_FOR_STMT:
            if (node->as.for_stmt.iterable->type == AST_RANGE_EXPR) {
                ASTNode* range = node->as.for_stmt.iterable;
                fprintf(out, "for (int64_t %.*s = ", 
                        (int)node->as.for_stmt.pattern->as.identifier.name.length, 
                        node->as.for_stmt.pattern->as.identifier.name.start);
                generate_node(range->as.range.start, out);
                fprintf(out, "; %.*s < ", 
                        (int)node->as.for_stmt.pattern->as.identifier.name.length, 
                        node->as.for_stmt.pattern->as.identifier.name.start);
                generate_node(range->as.range.end, out);
                fprintf(out, "; %.*s++) ", 
                        (int)node->as.for_stmt.pattern->as.identifier.name.length, 
                        node->as.for_stmt.pattern->as.identifier.name.start);
                generate_node(node->as.for_stmt.body, out);
            }
            break;

        case AST_RETURN_STMT:
            fprintf(out, "return");
            if (node->as.return_stmt.expression) {
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
        case AST_IMPORT: {
            const char* include_path = node->as.import_decl.resolved_path;
            const char* base = strrchr(include_path, '/');
            fprintf(out, "#include \"%s\"\n", base ? base + 1 : include_path);
            return;
        }
        default: break;
    }
}

static void generate_declarations(ASTNode* root, FILE* out, const char* out_path);
static void generate_implementations(ASTNode* root, FILE* out, bool is_main);

void generate_c_code(ASTNode* root, const char* out_path, bool is_main) {
    gen_sym_count = 0;
    gen_enum_count = 0;
    gen_struct_count = 0;
    gen_union_count = 0;
    gen_tuple_variant_count = 0;
    gen_printed_syms = false;
    gen_is_local = false;
    current_struct_context = NULL;
    current_struct_field_count = 0;

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

    generate_implementations(root, c_out, is_main);
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
                    fprintf(out, " %.*s_%.*s(%.*s* self", (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start,
                            (int)member->as.func_decl.name.length, member->as.func_decl.name.start,
                            (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start);
                    for (size_t k = 0; k < member->as.func_decl.param_count; k++) {
                        fprintf(out, ", ");
                        generate_type(member->as.func_decl.params[k]->as.var_decl.type, out);
                        fprintf(out, " %.*s", (int)member->as.func_decl.params[k]->as.var_decl.name.length,
                                member->as.func_decl.params[k]->as.var_decl.name.start);
                    }
                    fprintf(out, ");\n");
                }
            }
        } else if (node->type == AST_UNION_DECL) {
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
            gen_add_sym(node->as.pattern.name, node->evaluated_type ? node->evaluated_type : &gen_pattern_fallback_type, false);
            break;
        case AST_PROGRAM:
        case AST_BLOCK:
            for (size_t i = 0; i < node->as.block.count; i++) gen_collect_patterns(node->as.block.statements[i]);
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

static void generate_implementations(ASTNode* root, FILE* out, bool is_main) {
    for (size_t i = 0; i < root->as.block.count; i++) {
        ASTNode* node = root->as.block.statements[i];
        if (node->type == AST_FUNC_DECL) {
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
                    current_struct_context = "struct";
                    for (size_t k = 0; k < m->as.func_decl.param_count; k++) {
                        TypeExpr* t = m->as.func_decl.params[k]->as.var_decl.type;
                        if (!t || (t->kind == TYPE_BASE && t->as.base_type.length == 1 && t->as.base_type.start[0] == '_')) t = m->as.func_decl.params[k]->evaluated_type;
                        gen_add_sym(m->as.func_decl.params[k]->as.var_decl.name, t, true);
                    }
                    generate_type(m->as.func_decl.return_type, out);
                    fprintf(out, " %.*s_%.*s(%.*s* self", (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start, (int)m->as.func_decl.name.length, m->as.func_decl.name.start, (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start);
                    if (m->as.func_decl.param_count > 0) fprintf(out, ", ");
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

    if (is_main) {
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
