#include "semantic.h"
#include "parser.h"
#include "generator.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <unistd.h>

// ─────────────────────────────────────────────────────────────────────────────
// Path Helpers
// ─────────────────────────────────────────────────────────────────────────────

static void get_absolute_path(const char* relative_path, char* buffer) {
    if (realpath(relative_path, buffer) == NULL) {
        // If realpath fails (e.g. file doesn't exist yet), 
        // fall back to the relative path but this is risky.
        strncpy(buffer, relative_path, PATH_MAX);
    }
}

static void ensure_dir_exists(const char* path) {
    char dir[PATH_MAX];
    strncpy(dir, path, PATH_MAX);
    char* last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        char command[PATH_MAX + 10];
        snprintf(command, sizeof(command), "mkdir -p %s", dir);
        system(command);
    }
}

static void resolve_import_path(const char* current_file_path, const char* import_path, char* buffer) {
    // 1. If import_path is absolute, just normalize it
    if (import_path[0] == '/') {
        get_absolute_path(import_path, buffer);
        return;
    }

    // 2. Make it relative to current_file_path's directory
    char dir[PATH_MAX];
    strncpy(dir, current_file_path, PATH_MAX);
    char* last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0'; // Get directory part
    } else {
        strcpy(dir, "."); // Current directory
    }

    char joined[PATH_MAX];
    snprintf(joined, PATH_MAX, "%s/%s", dir, import_path);
    
    // Use realpath to resolve .. and .
    if (realpath(joined, buffer) == NULL) {
        // Fallback
        strncpy(buffer, joined, PATH_MAX);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Symbol Table
// ─────────────────────────────────────────────────────────────────────────────

typedef enum {
    SYM_VAR,
    SYM_FUNC,
    SYM_STRUCT,
    SYM_ENUM,
    SYM_VARIANT, // For Union variants
    SYM_PARAM,
    SYM_BUILTIN,
    SYM_NAMESPACE, // <--- Added: represents an imported module
} SymbolKind;

typedef struct Symbol {
    char        name[128];
    SymbolKind  kind;
    TypeExpr*   type;        // May be NULL for structs/enums
    struct Module* module_ptr; // For SYM_NAMESPACE or owner of the symbol
    struct Module* module_owner; // <--- Added: the module where this symbol is defined
    int         defined_line;
    bool        is_public;   // visibility flag
    struct Symbol* next;     // Linked list within one scope
} Symbol;

typedef struct Scope {
    Symbol*       symbols;   // Linked list of symbols in this scope
    struct Scope* parent;
} Scope;

typedef struct Module {
    char        path[256];
    Scope*      top_level_scope;
    bool        is_analyzed;
} Module;

// ── Global state ─────────────────────────────────────────────────────────────

static Scope*  current_scope = NULL;
static Module* current_module = NULL;
static bool    had_error     = false;
static int     loop_depth    = 0;  // Track nesting of while/for loops for break/continue validation
static Arena*  eval_arena    = NULL;

static Module* module_cache[128];
static int     module_count = 0;

static char    generated_c_files[128][PATH_MAX];
static int     generated_c_count = 0;

static Symbol* symbol_define(const char* name, size_t name_len,
                          SymbolKind kind, TypeExpr* type, int line, bool is_public);

static void register_builtins(void) {
    // Register basic types as SYM_STRUCT so they can be used in type initialization
    const char* builtins[] = {"Int", "Float", "Double", "UInt8", "UInt16", "Bool", "String"};
    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        symbol_define(builtins[i], strlen(builtins[i]), SYM_STRUCT, NULL, 0, false);
    }

    // Register built-in functions
    symbol_define("print", 5, SYM_BUILTIN, NULL, 0, false);
    symbol_define("assert", 6, SYM_BUILTIN, NULL, 0, false);
}

static void semantic_error(int line, const char* format, ...) {
    had_error = true;
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[Semantic Error] Line %d: ", line);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

// ── Scope helpers ─────────────────────────────────────────────────────────────

static Scope* scope_push(void) {
    Scope* s = (Scope*)calloc(1, sizeof(Scope));
    s->parent  = current_scope;
    current_scope = s;
    return s;
}

static void scope_pop(void) {
    if (!current_scope) return;
    Scope* old = current_scope;
    current_scope = old->parent;

    // Free symbol nodes (not their TypeExprs – those live in the arena)
    Symbol* sym = old->symbols;
    while (sym) {
        Symbol* next = sym->next;
        free(sym);
        sym = next;
    }
    free(old);
}

// ── Symbol helpers ────────────────────────────────────────────────────────────

// Define a symbol in the CURRENT scope.
// Returns NULL (and emits an error) if the name is already defined in this scope.
static Symbol* symbol_define(const char* name, size_t name_len,
                          SymbolKind kind, TypeExpr* type, int line, bool is_public) {
    if (!current_scope) return NULL; // Safety

    if (name_len == 0) {
        // This shouldn't happen
        return NULL; 
    }

    // Check for duplicate in current scope only
    for (Symbol* s = current_scope->symbols; s; s = s->next) {
        if (strncmp(s->name, name, name_len) == 0 && s->name[name_len] == '\0') {
            if (line > 0) { // Only error for non-builtins
                semantic_error(line,
                    "Identifier '%.*s' is already declared in this scope (first declared at line %d).",
                    (int)name_len, name, s->defined_line);
            }
            return NULL;
        }
    }

    Symbol* sym = (Symbol*)calloc(1, sizeof(Symbol));
    snprintf(sym->name, sizeof(sym->name), "%.*s", (int)name_len, name);
    sym->kind         = kind;
    sym->type         = type;
    sym->module_ptr   = NULL;
    sym->module_owner = current_module; // <--- Set owner
    sym->defined_line = line;
    sym->is_public    = is_public;
    sym->next         = current_scope->symbols;
    current_scope->symbols = sym;
    return sym;
}

// Look up a symbol starting from the current scope, walking up to global.
static Symbol* symbol_lookup(const char* name, size_t name_len) {
    for (Scope* sc = current_scope; sc; sc = sc->parent) {
        for (Symbol* s = sc->symbols; s; s = s->next) {
            if (strncmp(s->name, name, name_len) == 0 && s->name[name_len] == '\0') {
                return s;
            }
        }
    }
    return NULL;
}

// Convenience wrapper using a Token
static Symbol* define_token(Token tok, SymbolKind kind, TypeExpr* type, bool is_public) {
    return symbol_define(tok.start, tok.length, kind, type, tok.line, is_public);
}

static Symbol* lookup_token(Token tok) {
    return symbol_lookup(tok.start, tok.length);
}

// ─────────────────────────────────────────────────────────────────────────────
// Forward declaration
// ─────────────────────────────────────────────────────────────────────────────

static bool check_node(ASTNode* node);

// ─────────────────────────────────────────────────────────────────────────────
// Struct init checker (unchanged logic, just adapted to new file layout)
// ─────────────────────────────────────────────────────────────────────────────

static bool check_init_assign(ASTNode* node, Token* req, bool* ass,
                               size_t count, ASTNode* struct_decl);

static bool check_init_assign(ASTNode* node, Token* req, bool* ass,
                               size_t count, ASTNode* struct_decl) {
    if (!node) return true;

    if (node->type == AST_BINARY_EXPR && node->as.binary.op == TOKEN_EQUAL) {
        if (node->as.binary.left->type == AST_IDENTIFIER) {
            Token name = node->as.binary.left->as.identifier.name;
            for (size_t i = 0; i < count; i++) {
                if (req[i].length == name.length &&
                    strncmp(req[i].start, name.start, name.length) == 0) {
                    ass[i] = true;
                }
            }
        } else if (node->as.binary.left->type == AST_MEMBER_ACCESS) {
            ASTNode* obj = node->as.binary.left->as.member_access.object;
            if (obj->type == AST_IDENTIFIER && 
                obj->as.identifier.name.length == 4 && 
                strncmp(obj->as.identifier.name.start, "self", 4) == 0) {
                Token name = node->as.binary.left->as.member_access.member;
                for (size_t i = 0; i < count; i++) {
                    if (req[i].length == name.length &&
                        strncmp(req[i].start, name.start, name.length) == 0) {
                        ass[i] = true;
                    }
                }
            }
        }
        if (!check_init_assign(node->as.binary.right, req, ass, count, struct_decl)) return false;
        return true;
    }

    if (node->type == AST_FUNC_CALL) {
        Token callee_name;
        bool is_method = false;
        if (node->as.func_call.callee->type == AST_IDENTIFIER) {
            callee_name = node->as.func_call.callee->as.identifier.name;
            for (size_t i = 0; i < struct_decl->as.struct_decl.member_count; i++) {
                ASTNode* m = struct_decl->as.struct_decl.members[i];
                if (m->type == AST_FUNC_DECL &&
                    m->as.func_decl.name.length == callee_name.length &&
                    strncmp(m->as.func_decl.name.start, callee_name.start, callee_name.length) == 0) {
                    is_method = true;
                    break;
                }
            }
        }

        if (is_method) {
            for (size_t i = 0; i < count; i++) {
                if (!ass[i]) {
                    semantic_error(node->line,
                        "Cannot call struct method '%.*s' inside init before all non-optional "
                        "properties are initialized (missing '%.*s').",
                        (int)callee_name.length, callee_name.start,
                        (int)req[i].length, req[i].start);
                    return false;
                }
            }
        }

        for (size_t i = 0; i < node->as.func_call.arg_count; i++) {
            if (!check_init_assign(node->as.func_call.args[i], req, ass, count, struct_decl))
                return false;
        }
        return true;
    }

    switch (node->type) {
        case AST_BLOCK:
            for (size_t i = 0; i < node->as.block.count; i++) {
                if (!check_init_assign(node->as.block.statements[i], req, ass, count, struct_decl))
                    return false;
            }
            break;
        case AST_IF_STMT:
            if (!check_init_assign(node->as.if_stmt.condition, req, ass, count, struct_decl)) return false;
            if (!check_init_assign(node->as.if_stmt.then_branch, req, ass, count, struct_decl)) return false;
            if (node->as.if_stmt.else_branch)
                if (!check_init_assign(node->as.if_stmt.else_branch, req, ass, count, struct_decl)) return false;
            break;
        case AST_WHILE_STMT:
            if (!check_init_assign(node->as.while_stmt.condition, req, ass, count, struct_decl)) return false;
            if (!check_init_assign(node->as.while_stmt.body, req, ass, count, struct_decl)) return false;
            break;
        case AST_FOR_STMT:
            if (!check_init_assign(node->as.for_stmt.iterable, req, ass, count, struct_decl)) return false;
            if (!check_init_assign(node->as.for_stmt.body, req, ass, count, struct_decl)) return false;
            break;
        case AST_RETURN_STMT:
            if (node->as.return_stmt.expression)
                if (!check_init_assign(node->as.return_stmt.expression, req, ass, count, struct_decl)) return false;
            break;
        case AST_MEMBER_ACCESS:
            if (!check_init_assign(node->as.member_access.object, req, ass, count, struct_decl)) return false;
            break;
        case AST_UNARY_EXPR:
            if (!check_init_assign(node->as.unary.right, req, ass, count, struct_decl)) return false;
            break;
        case AST_NEW_EXPR:
            if (!check_init_assign(node->as.new_expr.value, req, ass, count, struct_decl)) return false;
            break;
        case AST_VAR_DECL:
            if (!check_init_assign(node->as.var_decl.initializer, req, ass, count, struct_decl)) return false;
            break;
        case AST_INDEX_EXPR:
            if (!check_init_assign(node->as.index_expr.object, req, ass, count, struct_decl)) return false;
            if (!check_init_assign(node->as.index_expr.index, req, ass, count, struct_decl)) return false;
            break;
        case AST_ARRAY_LITERAL:
            for (size_t i = 0; i < node->as.array_literal.element_count; i++) {
                if (!check_init_assign(node->as.array_literal.elements[i], req, ass, count, struct_decl))
                    return false;
            }
            break;
        case AST_RANGE_EXPR:
            if (node->as.range.start)
                if (!check_init_assign(node->as.range.start, req, ass, count, struct_decl)) return false;
            if (node->as.range.end)
                if (!check_init_assign(node->as.range.end, req, ass, count, struct_decl)) return false;
            break;
        case AST_ENUM_DECL:
            for (size_t i = 0; i < node->as.enum_decl.member_count; i++) {
                if (node->as.enum_decl.member_values[i])
                    if (!check_init_assign(node->as.enum_decl.member_values[i], req, ass, count, struct_decl))
                        return false;
            }
            break;
        case AST_ENUM_ACCESS:
            break;
        default:
            break;
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main AST checker with symbol table
// ─────────────────────────────────────────────────────────────────────────────

static bool check_node(ASTNode* node) {
    if (!node) return true;

    switch (node->type) {

        // ── Top-level program: register all global declarations first ──────
        case AST_PROGRAM: {
            // Pre-pass: register all top-level names so that forward references work
            // (e.g. a function body referencing another function declared later)
            for (size_t i = 0; i < node->as.block.count; i++) {
                ASTNode* decl = node->as.block.statements[i];
                if (decl->type == AST_FUNC_DECL) {
                    define_token(decl->as.func_decl.name, SYM_FUNC,
                                 decl->as.func_decl.return_type, decl->is_public);
                } else if (decl->type == AST_STRUCT_DECL) {
                    define_token(decl->as.struct_decl.name, SYM_STRUCT, NULL, decl->is_public);
                } else if (decl->type == AST_ENUM_DECL) {
                    define_token(decl->as.enum_decl.name, SYM_ENUM, NULL, decl->is_public);
                } else if (decl->type == AST_UNION_DECL) {
                    define_token(decl->as.union_decl.name, SYM_STRUCT, NULL, decl->is_public);
                }
            }

            // Now check each statement
            for (size_t i = 0; i < node->as.block.count; i++) {
                check_node(node->as.block.statements[i]);
            }
            break;
        }

        // ── Block: push/pop scope ─────────────────────────────────────────
        case AST_BLOCK: {
            scope_push();
            for (size_t i = 0; i < node->as.block.count; i++) {
                check_node(node->as.block.statements[i]);
            }
            scope_pop();
            break;
        }

        // ── Variable declaration ──────────────────────────────────────────
        case AST_VAR_DECL: {
            // Pointer safety: cannot initialize a pointer with a numeric literal
            if (node->as.var_decl.type &&
                node->as.var_decl.type->kind == TYPE_POINTER) {
                ASTNode* init = node->as.var_decl.initializer;
                if (init && init->type == AST_LITERAL_NUMBER) {
                    semantic_error(node->line,
                        "Pointer '%.*s' cannot be initialized with a numeric literal. "
                        "Use 'new' or a pointer move.",
                        (int)node->as.var_decl.name.length, node->as.var_decl.name.start);
                }
            }

            // Check initializer before defining the variable itself
            check_node(node->as.var_decl.initializer);

            // Simple type inference
            if (node->as.var_decl.type == NULL && node->as.var_decl.initializer != NULL) {
                node->as.var_decl.type = node->as.var_decl.initializer->evaluated_type;
            }

            // Define in current scope
            define_token(node->as.var_decl.name, SYM_VAR, node->as.var_decl.type, node->is_public);
            node->evaluated_type = node->as.var_decl.type;
            break;
        }

        // ── Identifier reference ──────────────────────────────────────────
        case AST_IDENTIFIER: {
            Token name = node->as.identifier.name;
            // Skip built-ins: 'self' (handled specially in generator)
            if (name.length == 4 && strncmp(name.start, "self", 4)  == 0) {
                break;
            }
            Symbol* sym = lookup_token(name);
            if (!sym) {
                semantic_error(node->line,
                    "Undefined identifier '%.*s'.",
                    (int)name.length, name.start);
            } else {
                if (sym->kind == SYM_STRUCT || sym->kind == SYM_ENUM) {
                    node->evaluated_type = type_new_base(eval_arena, name);
                } else if (sym->kind == SYM_BUILTIN) {
                    node->evaluated_type = NULL;
                } else {
                    node->evaluated_type = sym->type;
                }
            }
            break;
        }

        // ── Pattern ───────────────────────────────────────────────────────
        case AST_PATTERN: {
            if (node->as.pattern.type) {
                node->evaluated_type = node->as.pattern.type;
            }
            // Note: type inference happens in the caller (e.g. AST_BINARY_EXPR)
            break;
        }

        // ── Function declaration ──────────────────────────────────────────
        case AST_FUNC_DECL: {
            // The function name was already registered in the program pre-pass.
            // Open a new scope for parameters + body.
            scope_push();
            for (size_t i = 0; i < node->as.func_decl.param_count; i++) {
                ASTNode* p = node->as.func_decl.params[i];
                define_token(p->as.var_decl.name, SYM_PARAM, p->as.var_decl.type, false);
            }
            // Check body without the outer scope_push from AST_BLOCK
            // (we push the scope ourselves so we can include params in scope)
            if (node->as.func_decl.body) {
                ASTNode* body = node->as.func_decl.body;
                // body is AST_BLOCK; emit its statements without double-pushing scope
                for (size_t i = 0; i < body->as.block.count; i++) {
                    check_node(body->as.block.statements[i]);
                }
            }
            scope_pop();
            break;
        }

        // ── Return statement ──────────────────────────────────────────────
        case AST_RETURN_STMT:
            check_node(node->as.return_stmt.expression);
            break;

        // ── Function call ─────────────────────────────────────────────────
        case AST_FUNC_CALL: {
            // Validate callee
            ASTNode* callee = node->as.func_call.callee;
            Symbol* variant_sym = NULL;

            if (callee->type == AST_IDENTIFIER) {
                Token name = callee->as.identifier.name;
                Symbol* sym = lookup_token(name);
                if (!sym) {
                    semantic_error(node->line,
                        "Call to undefined function or type '%.*s'.",
                        (int)name.length, name.start);
                } else {
                    if (sym->kind == SYM_STRUCT) {
                        // Constructor returns the struct type
                        node->evaluated_type = type_new_base(eval_arena, name);
                    } else if (sym->kind == SYM_VARIANT) {
                        // Union variant constructor
                        variant_sym = sym;
                        // Find the parent Union type. 
                        // For simplicity, we can try to find the Union name from the scope.
                        // However, variants are defined in the Union's scope.
                        // So the parent scope should have the Union name.
                        if (current_scope->parent) {
                             // This is a bit complex if it's a direct call like `circle(10.5)`.
                             // But usually it's `Shape.circle(10.5)`.
                        }
                    } else if (sym->kind == SYM_BUILTIN) {
                        node->evaluated_type = NULL;
                    } else {
                        node->evaluated_type = sym->type;
                    }
                }
            } else if (callee->type == AST_MEMBER_ACCESS) {
                check_node(callee);
                // If it's a member access, we need to check if the member is a variant.
                // AST_MEMBER_ACCESS logic should have set the type, but for variants,
                // we want the return type of the call to be the Union type.
                
                // Let's re-lookup the variant symbol to be sure.
                if (callee->as.member_access.object->evaluated_type && 
                    callee->as.member_access.object->evaluated_type->kind == TYPE_BASE) {
                    Token union_name = callee->as.member_access.object->evaluated_type->as.base_type;
                    Symbol* union_sym = lookup_token(union_name);
                    if (union_sym && union_sym->kind == SYM_STRUCT) {
                         Scope* union_scope = (Scope*)union_sym->module_ptr;
                         if (union_scope) {
                             Token m_name = callee->as.member_access.member;
                             for (Symbol* s = union_scope->symbols; s; s = s->next) {
                                 if (s->name[m_name.length] == '\0' && 
                                     strncmp(s->name, m_name.start, m_name.length) == 0) {
                                     if (s->kind == SYM_VARIANT) {
                                         variant_sym = s;
                                         node->evaluated_type = type_new_base(eval_arena, union_name);
                                     }
                                     break;
                                 }
                             }
                         }
                    }
                }
            } else {
                check_node(callee);
                node->evaluated_type = callee->evaluated_type;
            }

            for (size_t i = 0; i < node->as.func_call.arg_count; i++) {
                check_node(node->as.func_call.args[i]);
            }
            break;
        }

        // ── Struct declaration ────────────────────────────────────────────
        case AST_STRUCT_DECL: {
            // Find the struct symbol we registered in pre-pass
            Symbol* struct_sym = lookup_token(node->as.struct_decl.name);

            // Struct name already registered in program pre-pass.
            // Open a new scope for struct members.
            scope_push();
            if (struct_sym) struct_sym->module_ptr = (Module*)current_scope; // Hack: reuse module_ptr to hold Scope* for members

            // Register methods and fields into the struct scope
            for (size_t i = 0; i < node->as.struct_decl.member_count; i++) {
                ASTNode* member = node->as.struct_decl.members[i];
                if (member->type == AST_VAR_DECL) {
                    define_token(member->as.var_decl.name, SYM_VAR,
                                 member->as.var_decl.type, member->is_public);
                } else if (member->type == AST_FUNC_DECL) {
                    define_token(member->as.func_decl.name, SYM_FUNC,
                                 member->as.func_decl.return_type, member->is_public);
                }
            }

            // ... rest of init checking ...
            for (size_t i = 0; i < node->as.struct_decl.member_count; i++) {
                ASTNode* member = node->as.struct_decl.members[i];
                if (member->type == AST_INIT_DECL) {
                    Token required_fields[64];
                    bool  assigned_fields[64] = {false};
                    size_t req_count = 0;

                    for (size_t j = 0; j < node->as.struct_decl.member_count; j++) {
                        ASTNode* field = node->as.struct_decl.members[j];
                        if (field->type == AST_VAR_DECL &&
                            field->as.var_decl.type->kind != TYPE_NULLABLE) {
                            required_fields[req_count++] = field->as.var_decl.name;
                        }
                    }

                    if (!check_init_assign(member->as.init_decl.body,
                                           required_fields, assigned_fields, req_count, node)) {
                        scope_pop();
                        return false;
                    }

                    for (size_t j = 0; j < req_count; j++) {
                        if (!assigned_fields[j]) {
                            semantic_error(member->line,
                                "Struct '%.*s' init must initialize non-optional property '%.*s'.",
                                (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start,
                                (int)required_fields[j].length, required_fields[j].start);
                            scope_pop();
                            return false;
                        }
                    }
                }
            }

            // Note: We don't pop and free the scope if we want to preserve it!
            // But currently scope_pop frees symbols. We need to detach it.
            current_scope = current_scope->parent;
            break;
        }

        // ── Enum declaration ──────────────────────────────────────────────
        case AST_ENUM_DECL: {
            // Enum name already registered. Check value expressions.
            for (size_t i = 0; i < node->as.enum_decl.member_count; i++) {
                if (node->as.enum_decl.member_values[i]) {
                    check_node(node->as.enum_decl.member_values[i]);
                }
            }
            break;
        }

        case AST_UNION_DECL: {
            Symbol* tag_enum_sym = NULL;
            if (node->as.union_decl.tag_enum.length > 0) {
                tag_enum_sym = lookup_token(node->as.union_decl.tag_enum);
                if (!tag_enum_sym || tag_enum_sym->kind != SYM_ENUM) {
                    semantic_error(node->line, "Undefined or invalid tag enum '%.*s' for union.",
                        (int)node->as.union_decl.tag_enum.length, node->as.union_decl.tag_enum.start);
                }
            }

            scope_push();
            Symbol* union_sym = lookup_token(node->as.union_decl.name);
            if (union_sym) union_sym->module_ptr = (Module*)current_scope;

            for (size_t i = 0; i < node->as.union_decl.member_count; i++) {
                ASTNode* member = node->as.union_decl.members[i];
                Token m_name = member->as.var_decl.name;

                // Register as variant, keeping its field type. 
                // The parent union type is available via the union name.
                Symbol* variant_sym = define_token(m_name, SYM_VARIANT, member->as.var_decl.type, false);
                if (variant_sym) {
                    variant_sym->module_owner = current_module;
                    // We can store the parent union's name/type in a special field if needed,
                    // but for now, the scope relationship should be enough.
                }
            }
            current_scope = current_scope->parent;
            break;
        }

        // ── Enum access (.member or Enum.member) ──────────────────────────
        case AST_ENUM_ACCESS:
            // If a fully qualified enum name is given, verify the enum type exists.
            if (node->as.enum_access.enum_name.length > 0) {
                Token name = node->as.enum_access.enum_name;
                if (!lookup_token(name)) {
                    semantic_error(node->line,
                        "Undefined enum type '%.*s'.",
                        (int)name.length, name.start);
                }
            }
            break;

        // ── Binary expression ─────────────────────────────────────────────
        case AST_BINARY_EXPR: {
            check_node(node->as.binary.left);

            // Special handling for Union Pattern Matching: union_val == Variant(patterns...)
            if (node->as.binary.op == TOKEN_EQUAL_EQUAL && 
                node->as.binary.left && node->as.binary.left->evaluated_type &&
                node->as.binary.left->evaluated_type->kind == TYPE_BASE &&
                node->as.binary.right && node->as.binary.right->type == AST_FUNC_CALL) {
                
                Token union_name = node->as.binary.left->evaluated_type->as.base_type;
                Symbol* union_sym = lookup_token(union_name);
                
                if (union_sym && union_sym->kind == SYM_STRUCT) {
                    ASTNode* call = node->as.binary.right;
                    Symbol* variant_sym = NULL;

                    // Find variant symbol
                    if (call->as.func_call.callee->type == AST_MEMBER_ACCESS) {
                        Token m_name = call->as.func_call.callee->as.member_access.member;
                        Scope* union_scope = (Scope*)union_sym->module_ptr;
                        if (union_scope) {
                            for (Symbol* s = union_scope->symbols; s; s = s->next) {
                                if (s->name[m_name.length] == '\0' && 
                                    strncmp(s->name, m_name.start, m_name.length) == 0 &&
                                    s->kind == SYM_VARIANT) {
                                    variant_sym = s;
                                    break;
                                }
                            }
                        }
                    }

                    if (variant_sym) {
                        // Pattern matching!
                        TypeExpr* v_type = variant_sym->type;
                        
                        if (v_type && v_type->kind == TYPE_TUPLE) {
                            if (call->as.func_call.arg_count != v_type->as.tuple.count) {
                                semantic_error(node->line, "Pattern match for variant '%.*s' expects %zu fields, got %zu.",
                                    (int)strlen(variant_sym->name), variant_sym->name,
                                    v_type->as.tuple.count, call->as.func_call.arg_count);
                            } else {
                                for (size_t i = 0; i < call->as.func_call.arg_count; i++) {
                                    ASTNode* arg = call->as.func_call.args[i];
                                    TypeExpr* field_type = v_type->as.tuple.elements[i];
                                    if (arg->type == AST_PATTERN) {
                                        arg->as.pattern.field_index = (int)i;
                                        if (v_type->as.tuple.names) {
                                            arg->as.pattern.field_name = v_type->as.tuple.names[i];
                                        } else {
                                            arg->as.pattern.field_name = (Token){0};
                                        }
                                        
                                        if (arg->as.pattern.type == NULL || (arg->as.pattern.type->kind == TYPE_BASE && arg->as.pattern.type->as.base_type.length == 1 && arg->as.pattern.type->as.base_type.start[0] == '_')) {
                                            arg->as.pattern.type = field_type;
                                        }
                                        arg->evaluated_type = arg->as.pattern.type;
                                        define_token(arg->as.pattern.name, SYM_VAR, arg->evaluated_type, false);
                                    } else {
                                        check_node(arg);
                                    }
                                }
                            }
                        } else if (v_type) {
                            // Single field variant (effectively a 1-element tuple for generation purposes)
                            if (call->as.func_call.arg_count != 1) {
                                semantic_error(node->line, "Pattern match for variant '%.*s' expects 1 field, got %zu.",
                                    (int)strlen(variant_sym->name), variant_sym->name,
                                    call->as.func_call.arg_count);
                            } else {
                                ASTNode* arg = call->as.func_call.args[0];
                                if (arg->type == AST_PATTERN) {
                                    arg->as.pattern.field_index = 0;
                                    // Single field variants in the parser currently don't always use tuples
                                    // but we can assume index 0 if it's not a tuple but has a type.
                                    // Wait, if it's NOT a tuple, the generator will generate it as a direct field.
                                    // Let's mark it index -1 to indicate direct field access.
                                    arg->as.pattern.field_index = -1; 
                                    
                                    if (arg->as.pattern.type == NULL || (arg->as.pattern.type->kind == TYPE_BASE && arg->as.pattern.type->as.base_type.length == 1 && arg->as.pattern.type->as.base_type.start[0] == '_')) {
                                        arg->as.pattern.type = v_type;
                                    }
                                    arg->evaluated_type = arg->as.pattern.type;
                                    define_token(arg->as.pattern.name, SYM_VAR, arg->evaluated_type, false);
                                } else {
                                    check_node(arg);
                                }
                            }
                        } else {
                            // Variant with no fields (tag only)
                            if (call->as.func_call.arg_count != 0) {
                                semantic_error(node->line, "Pattern match for variant '%.*s' expects 0 fields, got %zu.",
                                    (int)strlen(variant_sym->name), variant_sym->name,
                                    call->as.func_call.arg_count);
                            }
                        }
                        
                        Token bool_tok = {TOKEN_IDENTIFIER, "Bool", 4, node->line};
                        node->evaluated_type = type_new_base(eval_arena, bool_tok);
                        return true;
                    }
                }
            }

            check_node(node->as.binary.right);
            // Result type of binary expr usually same as left (simplification for MVP)
            if (node->as.binary.left && node->as.binary.left->evaluated_type)
                node->evaluated_type = node->as.binary.left->evaluated_type;
            
            // If it's a comparison, return Bool
            if (node->as.binary.op == TOKEN_EQUAL_EQUAL || node->as.binary.op == TOKEN_BANG_EQUAL ||
                node->as.binary.op == TOKEN_LESS || node->as.binary.op == TOKEN_LESS_EQUAL ||
                node->as.binary.op == TOKEN_GREATER || node->as.binary.op == TOKEN_GREATER_EQUAL) {
                Token bool_tok = {TOKEN_IDENTIFIER, "Bool", 4, node->line};
                node->evaluated_type = type_new_base(eval_arena, bool_tok);
            }
            break;
        }

        // ── Unary expression ──────────────────────────────────────────────
        case AST_UNARY_EXPR:
            check_node(node->as.unary.right);
            if (node->as.unary.right && node->as.unary.right->evaluated_type)
                node->evaluated_type = node->as.unary.right->evaluated_type;
            break;

        // ── Member access ─────────────────────────────────────────────────
        case AST_MEMBER_ACCESS: {
            check_node(node->as.member_access.object);
            
            // Check for namespace access: alias.member
            if (node->as.member_access.object->type == AST_IDENTIFIER) {
                Token obj_name = node->as.member_access.object->as.identifier.name;
                Symbol* sym = lookup_token(obj_name);
                if (sym && sym->kind == SYM_NAMESPACE) {
                    Module* mod = sym->module_ptr;
                    Token member_tok = node->as.member_access.member;
                    Symbol* member_sym = NULL;
                    if (mod->top_level_scope) {
                        for (Symbol* s = mod->top_level_scope->symbols; s; s = s->next) {
                            if (s->name[member_tok.length] == '\0' &&
                                strncmp(s->name, member_tok.start, member_tok.length) == 0) {
                                if (s->is_public) {
                                    member_sym = s;
                                    break;
                                }
                            }
                        }
                    }
                    if (!member_sym) {
                        semantic_error(node->line, "Module '%s' does not export a member named '%.*s'.",
                                       mod->path, (int)member_tok.length, member_tok.start);
                    } else {
                        node->evaluated_type = member_sym->type;
                    }
                    return true;
                }
            }

            // Type lookup for Struct, Union, and Tuple
            if (node->as.member_access.object->evaluated_type) {
                TypeExpr* obj_type = node->as.member_access.object->evaluated_type;
                if (obj_type->kind == TYPE_BASE) {
                    Token type_name = obj_type->as.base_type;
                    Symbol* sym = lookup_token(type_name);
                    if (sym && (sym->kind == SYM_STRUCT || sym->kind == SYM_ENUM)) {
                        Scope* member_scope = (Scope*)sym->module_ptr;
                        if (member_scope) {
                            Token field_name = node->as.member_access.member;
                            Symbol* field_sym = NULL;
                            for (Symbol* s = member_scope->symbols; s; s = s->next) {
                                if (s->name[field_name.length] == '\0' &&
                                    strncmp(s->name, field_name.start, field_name.length) == 0) {
                                    field_sym = s;
                                    break;
                                }
                            }
                            if (field_sym) {
                                node->evaluated_type = field_sym->type;
                                // Visibility check
                                if (!field_sym->is_public && sym->module_owner != current_module) {
                                    semantic_error(node->line, 
                                        "Cannot access private member '%.*s' of '%.*s' from outside its module.",
                                        (int)field_name.length, field_name.start,
                                        (int)type_name.length, type_name.start);
                                }
                                return true;
                            }
                        }
                    }
                } else if (obj_type->kind == TYPE_TUPLE) {
                    Token field_name = node->as.member_access.member;
                    for (size_t i = 0; i < obj_type->as.tuple.count; i++) {
                        if (obj_type->as.tuple.names &&
                            obj_type->as.tuple.names[i].length == field_name.length &&
                            strncmp(obj_type->as.tuple.names[i].start, field_name.start, field_name.length) == 0) {
                            node->evaluated_type = obj_type->as.tuple.elements[i];
                            return true;
                        }
                    }
                    semantic_error(node->line, "Tuple has no field named '%.*s'.", (int)field_name.length, field_name.start);
                }
            }

            if (node->as.member_access.object)
                node->evaluated_type = node->as.member_access.object->evaluated_type;
            break;
        }

        // ── new expression ────────────────────────────────────────────────
        case AST_NEW_EXPR:
            if (node->as.new_expr.value) {
                check_node(node->as.new_expr.value);
                if (node->as.new_expr.value->evaluated_type) {
                    node->evaluated_type = type_new_modifier(eval_arena, TYPE_POINTER, node->as.new_expr.value->evaluated_type);
                }
            }
            break;

        // ── If statement ──────────────────────────────────────────────────
        case AST_IF_STMT:
            check_node(node->as.if_stmt.condition);
            check_node(node->as.if_stmt.then_branch);
            if (node->as.if_stmt.else_branch)
                check_node(node->as.if_stmt.else_branch);
            break;

        // ── While statement ───────────────────────────────────────────────
        case AST_WHILE_STMT:
            check_node(node->as.while_stmt.condition);
            loop_depth++;
            check_node(node->as.while_stmt.body);
            loop_depth--;
            break;

        // ── For statement ─────────────────────────────────────────────────
        case AST_FOR_STMT: {
            check_node(node->as.for_stmt.iterable);
            // The pattern variable is implicitly declared – define it in a new scope
            scope_push();
            if (node->as.for_stmt.pattern->type == AST_IDENTIFIER) {
                Token pat = node->as.for_stmt.pattern->as.identifier.name;
                symbol_define(pat.start, pat.length, SYM_VAR, NULL, node->line, false);
            }
            // body is AST_BLOCK; check its contents in the same scope
            loop_depth++;
            ASTNode* body = node->as.for_stmt.body;
            if (body->type == AST_BLOCK) {
                for (size_t i = 0; i < body->as.block.count; i++) {
                    check_node(body->as.block.statements[i]);
                }
            } else {
                check_node(body);
            }
            loop_depth--;
            scope_pop();
            break;
        }

        // ── Break / Continue ──────────────────────────────────────────────
        case AST_BREAK_STMT:
            if (loop_depth == 0) {
                semantic_error(node->line, "'break' used outside of a loop.");
            }
            break;

        case AST_CONTINUE_STMT:
            if (loop_depth == 0) {
                semantic_error(node->line, "'continue' used outside of a loop.");
            }
            break;

        case AST_IMPORT: {
            Token path_tok = node->as.import_decl.path;
            // The path is a string literal including quotes, e.g., "utils/math.jiang"
            char raw_path[PATH_MAX];
            snprintf(raw_path, sizeof(raw_path), "%.*s", (int)path_tok.length - 2, path_tok.start + 1);

            char path[PATH_MAX];
            resolve_import_path(current_module->path, raw_path, path);
            strncpy(node->as.import_decl.resolved_path, path, 256);

            // 1. Load and parse the imported file
            FILE* file = fopen(path, "rb");
            if (!file) {
                semantic_error(node->line, "Could not open imported file '%s'.", path);
                break;
            }
            fseek(file, 0L, SEEK_END);
            size_t fileSize = ftell(file);
            rewind(file);
            char* buffer = (char*)malloc(fileSize + 1);
            fread(buffer, sizeof(char), fileSize, file);
            buffer[fileSize] = '\0';
            fclose(file);

            // We need to keep the buffer alive because the AST tokens point to it.
            // Ideally, the buffer would be managed by the Arena or a Module.
            ASTNode* imported_root = parse_source(eval_arena, buffer);
            // free(buffer); // DO NOT FREE THIS!
            
            if (!imported_root) {
                semantic_error(node->line, "Failed to parse imported file '%s'.", path);
                break;
            }

            // 2. Recursively check the imported module
            if (!semantic_check(eval_arena, imported_root, path, NULL)) {
                had_error = true;
            }

            // 3. Generate C code for the imported module in the 'build' directory
            char c_path[PATH_MAX];
            
            char cwd[PATH_MAX];
            getcwd(cwd, sizeof(cwd));
            const char* relative_to_root = path;
            if (strncmp(path, cwd, strlen(cwd)) == 0) {
                relative_to_root = path + strlen(cwd);
                if (relative_to_root[0] == '/') relative_to_root++;
            }

            snprintf(c_path, sizeof(c_path), "build/%s", relative_to_root);
            
            // Store the ABSOLUTE path where the C code is generated
            char abs_buffer[PATH_MAX];
            getcwd(abs_buffer, sizeof(abs_buffer));
            snprintf(node->as.import_decl.resolved_path, 256, "%s/%s", abs_buffer, c_path);
            
            // Now fix suffixes in both paths
            char* dot_jiang_c = strstr(c_path, ".jiang");
            if (dot_jiang_c) { memcpy(dot_jiang_c, ".c", 2); dot_jiang_c[2] = '\0'; }
            
            char* dot_jiang_res = strstr(node->as.import_decl.resolved_path, ".jiang");
            if (dot_jiang_res) { memcpy(dot_jiang_res, ".c", 2); dot_jiang_res[2] = '\0'; }

            ensure_dir_exists(c_path);
            generate_c_code(imported_root, c_path, false);
            
            // Record this generated C file for later linking
            bool already_added = false;
            for (int i = 0; i < generated_c_count; i++) {
                if (strcmp(generated_c_files[i], c_path) == 0) {
                    already_added = true;
                    break;
                }
            }
            if (!already_added) {
                strncpy(generated_c_files[generated_c_count++], c_path, PATH_MAX);
            }

            // 4. Import the public symbols from the analyzed module
            Module* imported_mod = NULL;
            for (int i = 0; i < module_count; i++) {
                if (strcmp(module_cache[i]->path, path) == 0) {
                    imported_mod = module_cache[i];
                    break;
                }
            }

            if (imported_mod && imported_mod->top_level_scope) {
                Token alias = node->as.import_decl.alias;
                if (alias.length > 0) {
                    // Create a namespace symbol
                    Symbol* ns_sym = (Symbol*)malloc(sizeof(Symbol));
                    snprintf(ns_sym->name, sizeof(ns_sym->name), "%.*s", (int)alias.length, alias.start);
                    ns_sym->kind = SYM_NAMESPACE;
                    ns_sym->module_ptr = imported_mod;
                    ns_sym->defined_line = node->line;
                    ns_sym->is_public = false;
                    ns_sym->next = current_scope->symbols;
                    current_scope->symbols = ns_sym;
                } else {
                    // Direct import: Copy public symbols from imported_mod to current_scope
                    Symbol* sym = imported_mod->top_level_scope->symbols;
                    while (sym) {
                        if (sym->is_public) {
                            // Check for conflict
                            bool conflict = false;
                            for (Symbol* s = current_scope->symbols; s; s = s->next) {
                                if (strcmp(s->name, sym->name) == 0) {
                                    conflict = true;
                                    break;
                                }
                            }
                            if (!conflict) {
                                // Create a new symbol in current scope but copy metadata
                                Symbol* new_sym = (Symbol*)calloc(1, sizeof(Symbol));
                                snprintf(new_sym->name, sizeof(new_sym->name), "%s", sym->name);
                                new_sym->kind = sym->kind;
                                new_sym->type = sym->type;
                                new_sym->module_ptr = sym->module_ptr;
                                new_sym->module_owner = sym->module_owner;
                                new_sym->defined_line = node->line;
                                new_sym->is_public = false; // The imported symbol itself is private to this module unless re-exported
                                new_sym->next = current_scope->symbols;
                                current_scope->symbols = new_sym;
                            }
                        }
                        sym = sym->next;
                    }
                }
            }
            break;
        }

        // ── Range expression ──────────────────────────────────────────────
        case AST_RANGE_EXPR:
            if (node->as.range.start) check_node(node->as.range.start);
            if (node->as.range.end)   check_node(node->as.range.end);
            break;

        // ── Index expression ──────────────────────────────────────────────
        case AST_INDEX_EXPR:
            check_node(node->as.index_expr.object);
            check_node(node->as.index_expr.index);
            break;

        // ── Array literal ─────────────────────────────────────────────────
        case AST_ARRAY_LITERAL:
            for (size_t i = 0; i < node->as.array_literal.element_count; i++) {
                check_node(node->as.array_literal.elements[i]);
            }
            node->evaluated_type = node->as.array_literal.type;
            break;

        // ── Struct init expression ────────────────────────────────────────
        case AST_STRUCT_INIT_EXPR: {
            for (size_t i = 0; i < node->as.struct_init.field_count; i++) {
                check_node(node->as.struct_init.field_values[i]);
            }
            node->evaluated_type = node->as.struct_init.type;

            // Visibility check for struct init fields
            if (node->as.struct_init.type && node->as.struct_init.type->kind == TYPE_BASE) {
                Token type_name = node->as.struct_init.type->as.base_type;
                Symbol* struct_sym = lookup_token(type_name);
                if (struct_sym && struct_sym->kind == SYM_STRUCT) {
                    Scope* member_scope = (Scope*)struct_sym->module_ptr;
                    if (member_scope && struct_sym->module_owner != current_module) {
                        // We are outside the defining module. 
                        // Check if any field being initialized is private.
                        for (size_t i = 0; i < node->as.struct_init.field_count; i++) {
                            Token field_name = node->as.struct_init.field_names[i];
                            Symbol* field_sym = NULL;
                            for (Symbol* s = member_scope->symbols; s; s = s->next) {
                                if (s->name[field_name.length] == '\0' &&
                                    strncmp(s->name, field_name.start, field_name.length) == 0) {
                                    field_sym = s;
                                    break;
                                }
                            }
                            if (field_sym && !field_sym->is_public) {
                                semantic_error(node->line, 
                                    "Cannot initialize private field '%.*s' of struct '%.*s' from outside its defining module. Use a public init method instead.",
                                    (int)field_name.length, field_name.start,
                                    (int)type_name.length, type_name.start);
                            }
                        }
                    }
                }
            }
            break;
        }

        // ── Init declaration (handled inside STRUCT_DECL) ─────────────────
        case AST_INIT_DECL:
            for (size_t i = 0; i < node->as.init_decl.param_count; i++) {
                check_node(node->as.init_decl.params[i]);
            }
            check_node(node->as.init_decl.body);
            break;

        // ── Literals ──────────────────────────────────────────────────────
        case AST_LITERAL_NUMBER: {
            // Implicitly Int for now
            Token int_token = (Token){TOKEN_IDENTIFIER, "Int", 3, node->line};
            node->evaluated_type = type_new_base(eval_arena, int_token);
            break;
        }

        case AST_LITERAL_STRING:
            break;

        default:
            break;
    }

    return !had_error;
}

int semantic_get_generated_files(char files[128][4096]) {
    for (int i = 0; i < generated_c_count; i++) {
        strncpy(files[i], generated_c_files[i], 4096);
    }
    return generated_c_count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public entry point
// ─────────────────────────────────────────────────────────────────────────────

void semantic_reset(void) {
    module_count = 0;
    generated_c_count = 0;
    had_error = false;
    loop_depth = 0;
}

bool semantic_check(Arena* arena, ASTNode* root, const char* input_path, const char* out_path) {
    char path[PATH_MAX];
    get_absolute_path(input_path, path);

    printf("Starting semantic analysis for %s...\n", path);
    
    // Check if already in cache
    for (int i = 0; i < module_count; i++) {
        if (strcmp(module_cache[i]->path, path) == 0) {
            if (module_cache[i]->is_analyzed) return true;
            return true;
        }
    }

    Module* prev_module = current_module;
    Scope*  prev_scope  = current_scope;

    if (prev_module == NULL) {
        generated_c_count = 0;
        if (out_path) {
            strncpy(generated_c_files[generated_c_count++], out_path, PATH_MAX);
        } else {
            strncpy(generated_c_files[generated_c_count++], "build/out.c", PATH_MAX);
        }
    }

    eval_arena = arena;
    had_error  = false;
    loop_depth = 0;

    Module* mod = (Module*)malloc(sizeof(Module));
    strncpy(mod->path, path, 256);
    mod->is_analyzed = false;
    current_module = mod;

    // Create the global scope for this module
    mod->top_level_scope = scope_push();
    
    register_builtins();

    module_cache[module_count++] = mod;

    check_node(root);

    // Keep the top-level scope but restore previous context
    current_scope = prev_scope;
    current_module = prev_module;
    mod->is_analyzed = true;

    if (!had_error) {
        printf("Semantic analysis passed for %s.\n", path);
    }
    return !had_error;
}
