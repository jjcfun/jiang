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
} GenSymbol;

static GenSymbol gen_sym_table[256];
static size_t gen_sym_count = 0;

typedef struct {
    char name[64];
    char members[64][64];
    size_t member_count;
} GenEnum;

static GenEnum gen_enums[64];
static size_t gen_enum_count = 0;

static char gen_struct_names[64][64];
static size_t gen_struct_count = 0;

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

static void gen_add_sym(Token name, TypeExpr* type) {
    if (!type) return;
    snprintf(gen_sym_table[gen_sym_count].name, 64, "%.*s", (int)name.length, name.start);
    if (type->kind == TYPE_BASE) {
        snprintf(gen_sym_table[gen_sym_count].type, 64, "%.*s", (int)type->as.base_type.length, type->as.base_type.start);
    } else if (type->kind == TYPE_SLICE) {
        snprintf(gen_sym_table[gen_sym_count].type, 64, "SLICE"); // Marker for slice
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
            strncmp(current_struct_fields[i].name, name.start, name.length) == 0 && current_struct_fields[i].name[name.length] == '\0') {
            return true;
        }
    }
    return false;
}

static void generate_node(ASTNode* node, FILE* out);

static void generate_type(TypeExpr* type, FILE* out) {
    if (!type) {
        fprintf(out, "void");
        return;
    }

    switch (type->kind) {
        case TYPE_BASE:
            if (type->as.base_type.length == 3 && strncmp(type->as.base_type.start, "Int", 3) == 0) fprintf(out, "int64_t");
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
            break; // C doesn't have native nullable, handled by pointer or value
        case TYPE_MUTABLE:
            generate_type(type->as.mutable.element, out);
            break; // C is mutable by default
    }
}

static void generate_node(ASTNode* node, FILE* out) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            fprintf(out, "{\n");
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

        case AST_VAR_DECL:
            gen_add_sym(node->as.var_decl.name, node->as.var_decl.type);
            generate_type(node->as.var_decl.type, out);
            fprintf(out, " %.*s", (int)node->as.var_decl.name.length, node->as.var_decl.name.start);
            if (node->as.var_decl.type && node->as.var_decl.type->kind == TYPE_ARRAY) {
                fprintf(out, "[");
                if (node->as.var_decl.type->as.array.length) {
                    generate_node(node->as.var_decl.type->as.array.length, out);
                }
                fprintf(out, "]");
            }
            if (node->as.var_decl.initializer) {
                fprintf(out, " = ");
                generate_node(node->as.var_decl.initializer, out);
            }
            break;

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

        case AST_UNARY_EXPR:
            switch (node->as.unary.op) {
                case TOKEN_MINUS: fprintf(out, "-"); break;
                case TOKEN_BANG: fprintf(out, "!"); break;
                default: break;
            }
            generate_node(node->as.unary.right, out);
            break;

        case AST_FUNC_CALL:
            if (node->as.func_call.callee->type == AST_MEMBER_ACCESS) {
                // Method call: obj.method(args) -> Struct_method(&obj, args)
                ASTNode* obj = node->as.func_call.callee->as.member_access.object;
                Token method = node->as.func_call.callee->as.member_access.member;
                
                const char* struct_type = NULL;
                if (obj->type == AST_IDENTIFIER) {
                    struct_type = gen_get_type(obj->as.identifier.name);
                }
                
                // Namespace call support: if obj is an identifier but has no type, treat as namespace
                if (obj->type == AST_IDENTIFIER && struct_type == NULL) {
                    Token name = obj->as.identifier.name;
                    if (!(name.length == 4 && strncmp(name.start, "self", 4) == 0)) {
                        fprintf(out, "%.*s(", (int)method.length, method.start);
                        for (size_t i = 0; i < node->as.func_call.arg_count; i++) {
                            generate_node(node->as.func_call.args[i], out);
                            if (i < node->as.func_call.arg_count - 1) fprintf(out, ", ");
                        }
                        fprintf(out, ")");
                        return;
                    }
                }

                if (struct_type) {
                    fprintf(out, "%s_%.*s(&", struct_type, (int)method.length, method.start);
                    generate_node(obj, out);
                    if (node->as.func_call.arg_count > 0) fprintf(out, ", ");
                } else { // Fallback, shouldn't happen if symbols tracked
                    generate_node(obj, out);
                    fprintf(out, "->%.*s(", (int)method.length, method.start);
                }
                
                for (size_t i = 0; i < node->as.func_call.arg_count; i++) {
                    generate_node(node->as.func_call.args[i], out);
                    if (i < node->as.func_call.arg_count - 1) fprintf(out, ", ");
                }
                fprintf(out, ")");
            } else {
                bool handled = false;
                if (node->as.func_call.callee->type == AST_IDENTIFIER) {
                    Token name = node->as.func_call.callee->as.identifier.name;
                    if (gen_is_struct(name)) {
                        fprintf(out, "%.*s_new(", (int)name.length, name.start);
                        handled = true;
                    } else if (name.length == 5 && strncmp(name.start, "print", 5) == 0) {
                        fprintf(out, "printf(");
                        handled = true;
                    } else if (name.length == 6 && strncmp(name.start, "assert", 6) == 0) {
                        // Use a statement expression ({ ... }) for assert if possible, 
                        // but since it's a function call, we might be in expression context.
                        // However, in this language func calls are usually statements or used in simple ways.
                        // For a robust assert that works in expressions:
                        fprintf(out, "((void)(( ");
                        generate_node(node->as.func_call.args[0], out);
                        fprintf(out, " ) ? 0 : (fprintf(stderr, \"Assertion failed at line %%d\\n\", %d), exit(1))))", node->line);
                        handled = true;
                        return; // Special case: we already generated the whole call
                    }
                }
                
                if (!handled) {
                    generate_node(node->as.func_call.callee, out);
                    fprintf(out, "(");
                }

                for (size_t i = 0; i < node->as.func_call.arg_count; i++) {
                    generate_node(node->as.func_call.args[i], out);
                    if (i < node->as.func_call.arg_count - 1) fprintf(out, ", ");
                }
                fprintf(out, ")");
            }
            break;

        case AST_MEMBER_ACCESS: {
            ASTNode* obj = node->as.member_access.object;
            const char* obj_type = NULL;
            if (obj->type == AST_IDENTIFIER) {
                obj_type = gen_get_type(obj->as.identifier.name);
            }

            // Check for namespace: if it's an identifier with no associated type
            // and it's not 'self', we treat it as a namespace alias and just output the member.
            if (obj->type == AST_IDENTIFIER && obj_type == NULL) {
                Token name = obj->as.identifier.name;
                if (!(name.length == 4 && strncmp(name.start, "self", 4) == 0)) {
                    // It's a namespace alias (like 'm' in 'm.add').
                    // In our flat C output, we just ignore the prefix.
                    fprintf(out, "%.*s", (int)node->as.member_access.member.length, node->as.member_access.member.start);
                    return;
                }
            }

            if (node->as.member_access.member.length == 5 && memcmp(node->as.member_access.member.start, "value", 5) == 0) {
                if (obj->type == AST_ENUM_ACCESS || (obj_type && gen_is_enum_str(obj_type))) {
                    fprintf(out, "(long long)"); // Cast to long long for %lld
                    generate_node(obj, out);
                    return;
                }
            }
            
            generate_node(obj, out);
            bool is_ptr = false;
            if (obj->evaluated_type && obj->evaluated_type->kind == TYPE_POINTER) {
                is_ptr = true;
            }
            
            if (obj->type == AST_IDENTIFIER) {
                const char* type = gen_get_type(obj->as.identifier.name);
                if (type && strcmp(type, "SLICE") == 0) {
                    fprintf(out, ".%.*s", (int)node->as.member_access.member.length, node->as.member_access.member.start);
                    return;
                }
            }
            
            fprintf(out, "%s%.*s", is_ptr ? "->" : ".", (int)node->as.member_access.member.length, node->as.member_access.member.start);
            break;
        }

        case AST_NEW_EXPR: {
            ASTNode* val = node->as.new_expr.value;
            if (val->type == AST_LITERAL_NUMBER) {
                // ({ void* _p = malloc(sizeof(T)); *(T*)_p = V; (T*)_p; })
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
                // ({ void* _p = malloc(sizeof(T)*N); memcpy(_p, &((T){...}), sizeof(T)*N); (T*)_p; })
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
            } else if (val->type == AST_STRUCT_INIT_EXPR) {
                // Could be value or new. 
                if (val->as.struct_init.type && val->as.struct_init.type->kind == TYPE_BASE) {
                    fprintf(out, "({ void* _p = malloc(sizeof(");
                    generate_type(val->evaluated_type, out);
                    fprintf(out, ")); *(");
                    generate_type(val->evaluated_type, out);
                    fprintf(out, "*)_p = ");
                    generate_node(val, out);
                    fprintf(out, "; (");
                    generate_type(val->evaluated_type, out);
                    fprintf(out, "*)_p; })");
                }
            } else {
                // Fallback: malloc(sizeof(<expr>))
                fprintf(out, "malloc(sizeof(");
                generate_node(val, out);
                fprintf(out, "))");
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
            // for i in 0..10 { ... } -> for (int64_t i = 0; i < 10; i++) { ... }
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
            // Handled in Pass 1/Declarations
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

        case AST_INDEX_EXPR:
            if (node->as.index_expr.object->evaluated_type && 
                node->as.index_expr.object->evaluated_type->kind == TYPE_SLICE) {
                if (node->as.index_expr.index->type == AST_RANGE_EXPR) {
                    // Slicing: ({ Slice_T _s; _s.ptr = obj.ptr + start; _s.length = end - start; _s; })
                    fprintf(out, "({ Slice_");
                    generate_type(node->as.index_expr.object->evaluated_type->as.slice.element, out);
                    fprintf(out, " _s; _s.ptr = ");
                    generate_node(node->as.index_expr.object, out);
                    fprintf(out, ".ptr + ");
                    if (node->as.index_expr.index->as.range.start) {
                        generate_node(node->as.index_expr.index->as.range.start, out);
                    } else {
                        fprintf(out, "0");
                    }
                    fprintf(out, "; _s.length = ");
                    if (node->as.index_expr.index->as.range.end) {
                        generate_node(node->as.index_expr.index->as.range.end, out);
                    } else {
                        generate_node(node->as.index_expr.object, out);
                        fprintf(out, ".length");
                    }
                    fprintf(out, " - ");
                    if (node->as.index_expr.index->as.range.start) {
                        generate_node(node->as.index_expr.index->as.range.start, out);
                    } else {
                        fprintf(out, "0");
                    }
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
            generate_node(node->as.index_expr.index, out);
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
                // Shorthand .member. Search across all enums for matching member.
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

        case AST_BREAK_STMT:
            fprintf(out, "break");
            break;
case AST_CONTINUE_STMT:
    fprintf(out, "continue");
    break;

case AST_IMPORT: {
    // Use the pre-resolved absolute path from semantic analysis
    fprintf(out, "#include \"%s\"\n", node->as.import_decl.resolved_path);
    return; // Exit to avoid the automatic semicolon in the main loop
}

        default:
            fprintf(stderr, "Code generation error: Unknown node type %d.\n", node->type);
            break;
    }
}

static void generate_declarations(ASTNode* root, FILE* out, const char* out_path);
static void generate_implementations(ASTNode* root, FILE* out, bool is_main);

void generate_c_code(ASTNode* root, const char* out_path) {
    // Reset global state for each file generation
    gen_sym_count = 0;
    gen_enum_count = 0;
    gen_struct_count = 0;
    current_struct_context = NULL;
    current_struct_field_count = 0;

    // 1. Generate .h file
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

    // 2. Generate .c file
    FILE* c_out = fopen(out_path, "w");
    if (!c_out) {
        fprintf(stderr, "Generator could not open output file %s\n", out_path);
        return;
    }
    
    // In implementation file, include its own header
    char* last_slash = strrchr(h_path, '/');
    char* header_filename = last_slash ? last_slash + 1 : h_path;
    fprintf(c_out, "#include \"%s\"\n\n", header_filename);

    bool is_main = (strstr(out_path, "out.c") != NULL);
    generate_implementations(root, c_out, is_main);
    fclose(c_out);

    printf("Successfully generated C/H code at: %s\n", out_path);
}

static void generate_declarations(ASTNode* root, FILE* out, const char* out_path) {
    // Generate a unique guard name based on the output path
    char guard[512];
    snprintf(guard, sizeof(guard), "JIANG_GEN_");
    for (size_t i = 0; out_path[i] != '\0'; i++) {
        char c = out_path[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            guard[10 + i] = (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
        } else {
            guard[10 + i] = '_';
        }
        guard[11 + i] = '\0';
    }

    fprintf(out, "#ifndef %s\n", guard);
    fprintf(out, "#define %s\n\n", guard);
    
    // Get absolute path to runtime.h
    char runtime_path[512];
    char cwd[512];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        snprintf(runtime_path, sizeof(runtime_path), "%s/include/runtime.h", cwd);
    } else {
        strcpy(runtime_path, "runtime.h");
    }
    fprintf(out, "#include \"%s\"\n\n", runtime_path);

    // Gather structs/enums first
    for (size_t i = 0; i < root->as.block.count; i++) {
        ASTNode* node = root->as.block.statements[i];
        if (node->type == AST_STRUCT_DECL) {
            gen_add_struct(node->as.struct_decl.name);
        } else if (node->type == AST_ENUM_DECL) {
            gen_add_enum(node);
        }
    }

    // Emit prototypes
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
        } else if (node->type == AST_FUNC_DECL) {
            generate_type(node->as.func_decl.return_type, out);
            fprintf(out, " %.*s(", (int)node->as.func_decl.name.length, node->as.func_decl.name.start);
            for (size_t j = 0; j < node->as.func_decl.param_count; j++) {
                generate_node(node->as.func_decl.params[j], out);
                if (j < node->as.func_decl.param_count - 1) fprintf(out, ", ");
            }
            fprintf(out, ");\n");
        } else if (node->type == AST_VAR_DECL) {
            fprintf(out, "extern ");
            generate_type(node->as.var_decl.type, out);
            fprintf(out, " %.*s;\n", (int)node->as.var_decl.name.length, node->as.var_decl.name.start);
        } else if (node->type == AST_IMPORT) {
            // In headers, replace .c with .h for includes
            char path[256];
            strncpy(path, node->as.import_decl.resolved_path, 256);
            char* dot_c = strstr(path, ".c");
            if (dot_c) memcpy(dot_c, ".h", 2);
            fprintf(out, "#include \"%s\"\n", path);
        }
    }

    fprintf(out, "\n#endif\n");
}

static void generate_implementations(ASTNode* root, FILE* out, bool is_main) {
    for (size_t i = 0; i < root->as.block.count; i++) {
        ASTNode* node = root->as.block.statements[i];
        if (node->type == AST_FUNC_DECL) {
            // Function implementation
            generate_type(node->as.func_decl.return_type, out);
            fprintf(out, " %.*s(", (int)node->as.func_decl.name.length, node->as.func_decl.name.start);
            for (size_t j = 0; j < node->as.func_decl.param_count; j++) {
                generate_node(node->as.func_decl.params[j], out);
                if (j < node->as.func_decl.param_count - 1) fprintf(out, ", ");
            }
            fprintf(out, ") ");
            generate_node(node->as.func_decl.body, out);
            fprintf(out, "\n\n");
        } else if (node->type == AST_VAR_DECL && !is_main) {
            generate_node(node, out);
            fprintf(out, ";\n");
        }
    }

    if (is_main) {
        fprintf(out, "int main() {\n    ");
        for (size_t i = 0; i < root->as.block.count; i++) {
            ASTNode* node = root->as.block.statements[i];
            if (node->type != AST_FUNC_DECL && node->type != AST_STRUCT_DECL && 
                node->type != AST_ENUM_DECL && node->type != AST_IMPORT) {
                generate_node(node, out);
                if (node->type != AST_BLOCK) fprintf(out, ";\n    ");
            }
        }
        fprintf(out, "return 0;\n}\n");
    }
}
