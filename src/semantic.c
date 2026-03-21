#include "semantic.h"
#include "parser.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <unistd.h>

typedef struct Scope {
    Symbol*       symbols;
    struct Scope* parent;
} Scope;

typedef struct Module {
    char        path[256];
    char        name[128];
    char        id[17];
    Scope*      top_level_scope;
    bool        is_analyzed;
    ASTNode*    root;
} Module;

static Scope*  current_scope = NULL;
static Module* current_module = NULL;
static bool    had_error     = false;
static int     loop_depth    = 0;
static Arena*  eval_arena    = NULL;
static Module* module_cache[128];
static int     module_cache_count = 0;

const char* module_get_name(Module* mod) { return mod->name; }
const char* module_get_id(Module* mod) { return mod->id; }
ASTNode* module_get_root(Module* mod) { return mod->root; }

int semantic_get_modules(Module** mods) {
    for (int i = 0; i < module_cache_count; i++) mods[i] = module_cache[i];
    return module_cache_count;
}

static uint64_t djb2_hash(const char* str) {
    uint64_t hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}

static void semantic_error(int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[line %d] Semantic Error: ", line);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
    had_error = true;
}

static Scope* scope_push(void) {
    Scope* scope = arena_alloc(eval_arena, sizeof(Scope));
    scope->symbols = NULL;
    scope->parent = current_scope;
    current_scope = scope;
    return scope;
}

static void scope_pop(void) {
    if (current_scope) current_scope = current_scope->parent;
}

static TypeExpr* make_base_type(const char* name) {
    TypeExpr* t = arena_alloc(eval_arena, sizeof(TypeExpr));
    t->kind = TYPE_BASE;
    t->as.base_type.start = name;
    t->as.base_type.length = strlen(name);
    return t;
}

static TypeExpr* make_named_type(Token name) {
    char* text = arena_alloc(eval_arena, name.length + 1);
    memcpy(text, name.start, name.length);
    text[name.length] = '\0';
    return make_base_type(text);
}

static TypeExpr* make_pointer_type(TypeExpr* element) {
    TypeExpr* t = arena_alloc(eval_arena, sizeof(TypeExpr));
    t->kind = TYPE_POINTER;
    t->as.pointer.element = element;
    return t;
}

static TypeExpr* make_slice_type(TypeExpr* element) {
    TypeExpr* t = arena_alloc(eval_arena, sizeof(TypeExpr));
    t->kind = TYPE_SLICE;
    t->as.slice.element = element;
    return t;
}

static TypeExpr* unwrap_type(TypeExpr* type) {
    while (type && (type->kind == TYPE_NULLABLE || type->kind == TYPE_MUTABLE)) {
        type = (type->kind == TYPE_NULLABLE) ? type->as.nullable.element : type->as.mutable.element;
    }
    return type;
}

static bool token_eq(Token a, Token b) {
    return a.length == b.length && strncmp(a.start, b.start, a.length) == 0;
}

static bool token_eq_cstr(Token tok, const char* text) {
    size_t len = strlen(text);
    return tok.length == len && strncmp(tok.start, text, len) == 0;
}

static bool same_base_name(TypeExpr* type, const char* name) {
    type = unwrap_type(type);
    return type && type->kind == TYPE_BASE && strlen(name) == type->as.base_type.length &&
           strncmp(type->as.base_type.start, name, type->as.base_type.length) == 0;
}

static bool type_compatible(TypeExpr* expected, TypeExpr* actual) {
    expected = unwrap_type(expected);
    actual = unwrap_type(actual);
    if (!expected || !actual) return true;
    if (expected->kind != actual->kind) return false;
    switch (expected->kind) {
        case TYPE_BASE:
            return expected->as.base_type.length == actual->as.base_type.length &&
                   strncmp(expected->as.base_type.start, actual->as.base_type.start, expected->as.base_type.length) == 0;
        case TYPE_POINTER:
            return type_compatible(expected->as.pointer.element, actual->as.pointer.element);
        case TYPE_ARRAY:
            return type_compatible(expected->as.array.element, actual->as.array.element);
        case TYPE_SLICE:
            return type_compatible(expected->as.slice.element, actual->as.slice.element);
        default:
            return true;
    }
}

static Symbol* symbol_define(const char* name, size_t name_len,
                             SymbolKind kind, TypeExpr* type, int line, bool is_public) {
    for (Symbol* it = current_scope->symbols; it; it = it->next) {
        if (strlen(it->name) == name_len && strncmp(it->name, name, name_len) == 0) {
            semantic_error(line, "Redefinition of '%.*s'", (int)name_len, name);
            return it;
        }
    }

    Symbol* sym = arena_alloc(eval_arena, sizeof(Symbol));
    size_t copy_len = name_len < sizeof(sym->name) - 1 ? name_len : sizeof(sym->name) - 1;
    memcpy(sym->name, name, copy_len);
    sym->name[copy_len] = '\0';
    sym->kind = kind;
    sym->type = type;
    sym->module_ptr = NULL;
    sym->module_owner = NULL;
    sym->defined_line = line;
    sym->is_public = is_public;
    sym->next = current_scope->symbols;
    current_scope->symbols = sym;
    return sym;
}

static Symbol* symbol_lookup(Token name) {
    for (Scope* scope = current_scope; scope; scope = scope->parent) {
        for (Symbol* sym = scope->symbols; sym; sym = sym->next) {
            if (strlen(sym->name) == name.length && strncmp(sym->name, name.start, name.length) == 0) return sym;
        }
    }
    return NULL;
}

static Symbol* module_lookup_export(Module* mod, Token name) {
    if (!mod || !mod->top_level_scope) return NULL;
    for (Symbol* sym = mod->top_level_scope->symbols; sym; sym = sym->next) {
        if (sym->is_public && strlen(sym->name) == name.length &&
            strncmp(sym->name, name.start, name.length) == 0) {
            return sym;
        }
    }
    return NULL;
}

static ASTNode* module_find_struct_decl(Module* mod, TypeExpr* type) {
    type = unwrap_type(type);
    if (!mod || !mod->root || !type || type->kind != TYPE_BASE) return NULL;
    for (size_t i = 0; i < mod->root->as.block.count; i++) {
        ASTNode* stmt = mod->root->as.block.statements[i];
        if (stmt->type == AST_STRUCT_DECL && token_eq(stmt->as.struct_decl.name, type->as.base_type)) return stmt;
    }
    return NULL;
}

static ASTNode* find_struct_field(ASTNode* struct_decl, Token member) {
    if (!struct_decl || struct_decl->type != AST_STRUCT_DECL) return NULL;
    for (size_t i = 0; i < struct_decl->as.struct_decl.member_count; i++) {
        ASTNode* field = struct_decl->as.struct_decl.members[i];
        if (field->type == AST_VAR_DECL && token_eq(field->as.var_decl.name, member)) return field;
    }
    return NULL;
}

static void register_builtins(void) {
    const char* builtins[] = {"Int", "Float", "Double", "UInt8", "UInt16", "Bool", "String"};
    for (size_t i = 0; i < 7; i++) {
        symbol_define(builtins[i], strlen(builtins[i]), SYM_STRUCT, make_base_type(builtins[i]), 0, false);
    }
    symbol_define("print", 5, SYM_FUNC, make_base_type("void"), 0, false);
    symbol_define("assert", 6, SYM_FUNC, make_base_type("void"), 0, false);
    symbol_define("self", 4, SYM_VAR, NULL, 0, false);
}

static void check_node(ASTNode* node);

static void check_block(ASTNode* node) {
    scope_push();
    for (size_t i = 0; i < node->as.block.count; i++) check_node(node->as.block.statements[i]);
    scope_pop();
}

static void check_binding_list(ASTNode* node) {
    if (!node || node->type != AST_BINDING_LIST) return;
    for (size_t i = 0; i < node->as.binding_list.count; i++) {
        check_node(node->as.binding_list.items[i]);
    }
}

static TypeExpr* tuple_element_type(TypeExpr* type, size_t index) {
    type = unwrap_type(type);
    if (!type) return NULL;
    if (type->kind == TYPE_TUPLE) {
        if (index < type->as.tuple.count) return type->as.tuple.elements[index];
        return NULL;
    }
    if (index == 0) return type;
    return NULL;
}

static void import_public_symbols(Module* imported_mod, int line) {
    if (!imported_mod || !imported_mod->top_level_scope) return;
    for (Symbol* sym = imported_mod->top_level_scope->symbols; sym; sym = sym->next) {
        if (!sym->is_public) continue;
        Symbol* imported = symbol_define(sym->name, strlen(sym->name), sym->kind, sym->type, line, true);
        imported->module_owner = imported_mod;
    }
}

static void check_node(ASTNode* node) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
            for (size_t i = 0; i < node->as.block.count; i++) check_node(node->as.block.statements[i]);
            break;

        case AST_BLOCK:
            check_block(node);
            break;

        case AST_BINDING_LIST:
            check_binding_list(node);
            break;

        case AST_VAR_DECL: {
            check_node(node->as.var_decl.initializer);

            TypeExpr* declared_type = node->as.var_decl.type;
            if (declared_type && same_base_name(declared_type, "_")) declared_type = NULL;

            if (declared_type && node->as.var_decl.initializer) {
                TypeExpr* init_type = node->as.var_decl.initializer->evaluated_type;
                if (unwrap_type(declared_type)->kind == TYPE_POINTER &&
                    (!init_type || unwrap_type(init_type)->kind != TYPE_POINTER)) {
                    semantic_error(node->line, "Pointer variable '%.*s' requires a pointer initializer",
                                   (int)node->as.var_decl.name.length, node->as.var_decl.name.start);
                } else if (init_type && !type_compatible(declared_type, init_type)) {
                    semantic_error(node->line, "Type mismatch in initializer for '%.*s'",
                                   (int)node->as.var_decl.name.length, node->as.var_decl.name.start);
                }
            }

            if (declared_type && node->as.var_decl.initializer &&
                node->as.var_decl.initializer->type == AST_ARRAY_LITERAL) {
                node->as.var_decl.initializer->as.array_literal.type = declared_type;
                node->as.var_decl.initializer->evaluated_type = declared_type;
            }

            node->evaluated_type = declared_type ? declared_type :
                (node->as.var_decl.initializer ? node->as.var_decl.initializer->evaluated_type : NULL);
            node->symbol = symbol_define(node->as.var_decl.name.start, node->as.var_decl.name.length,
                                         SYM_VAR, node->evaluated_type, node->line, node->is_public);
            break;
        }

        case AST_FUNC_DECL:
            node->symbol = symbol_define(node->as.func_decl.name.start, node->as.func_decl.name.length,
                                         SYM_FUNC, node->as.func_decl.return_type, node->line, node->is_public);
            scope_push();
            for (size_t i = 0; i < node->as.func_decl.param_count; i++) {
                ASTNode* p = node->as.func_decl.params[i];
                symbol_define(p->as.var_decl.name.start, p->as.var_decl.name.length,
                              SYM_PARAM, p->as.var_decl.type, p->line, false);
            }
            check_node(node->as.func_decl.body);
            scope_pop();
            break;

        case AST_STRUCT_DECL:
            node->symbol = symbol_define(node->as.struct_decl.name.start, node->as.struct_decl.name.length,
                                         SYM_STRUCT, make_named_type(node->as.struct_decl.name),
                                         node->line, node->is_public);
            scope_push();
            for (size_t i = 0; i < node->as.struct_decl.member_count; i++) check_node(node->as.struct_decl.members[i]);
            scope_pop();
            break;

        case AST_UNION_DECL:
            node->symbol = symbol_define(node->as.union_decl.name.start, node->as.union_decl.name.length,
                                         SYM_STRUCT, make_named_type(node->as.union_decl.name),
                                         node->line, node->is_public);
            break;

        case AST_IMPORT: {
            char path[PATH_MAX];
            char dir[PATH_MAX];
            strncpy(dir, current_module->path, sizeof(dir));
            char* last_slash = strrchr(dir, '/');
            if (last_slash) *last_slash = '\0';
            else strcpy(dir, ".");

            snprintf(path, sizeof(path), "%s/%.*s", dir,
                     (int)node->as.import_decl.path.length - 2, node->as.import_decl.path.start + 1);

            char abs_path[PATH_MAX];
            if (realpath(path, abs_path) == NULL) strncpy(abs_path, path, sizeof(abs_path));

            FILE* file = fopen(abs_path, "r");
            if (!file) {
                semantic_error(node->line, "Could not open import '%s'", abs_path);
                break;
            }

            fseek(file, 0, SEEK_END);
            long size = ftell(file);
            rewind(file);
            char* buf = malloc(size + 1);
            fread(buf, 1, size, file);
            buf[size] = '\0';
            fclose(file);

            ASTNode* imported_root = parse_source(eval_arena, buf);
            if (!imported_root) {
                semantic_error(node->line, "Failed to parse '%s'", abs_path);
                break;
            }

            Module* imported_mod = semantic_check(eval_arena, imported_root, abs_path);
            if (!imported_mod) break;

            snprintf(node->as.import_decl.resolved_path, sizeof(node->as.import_decl.resolved_path),
                     "build/%s_%s.c", imported_mod->name, imported_mod->id);

            if (node->as.import_decl.alias.length > 0) {
                Symbol* ns = symbol_define(node->as.import_decl.alias.start, node->as.import_decl.alias.length,
                                           SYM_NAMESPACE, NULL, node->line, false);
                ns->module_ptr = imported_mod;
                node->symbol = ns;
            } else {
                import_public_symbols(imported_mod, node->line);
            }
            break;
        }

        case AST_IDENTIFIER: {
            Symbol* sym = symbol_lookup(node->as.identifier.name);
            if (sym) {
                node->symbol = sym;
                node->evaluated_type = sym->type;
            } else if (token_eq_cstr(node->as.identifier.name, "print") ||
                       token_eq_cstr(node->as.identifier.name, "assert")) {
                node->evaluated_type = make_base_type("void");
            } else {
                semantic_error(node->line, "Undefined identifier '%.*s'",
                               (int)node->as.identifier.name.length, node->as.identifier.name.start);
            }
            break;
        }

        case AST_LITERAL_NUMBER:
            node->evaluated_type = make_base_type("Int");
            break;

        case AST_LITERAL_STRING:
            node->evaluated_type = make_base_type("String");
            break;

        case AST_TUPLE_LITERAL: {
            size_t count = node->as.tuple_literal.count;
            TypeExpr** elements = arena_alloc(eval_arena, sizeof(TypeExpr*) * count);
            for (size_t i = 0; i < count; i++) {
                check_node(node->as.tuple_literal.elements[i]);
                elements[i] = node->as.tuple_literal.elements[i]->evaluated_type;
            }
            node->evaluated_type = type_new_tuple(eval_arena, elements, NULL, count);
            break;
        }

        case AST_PATTERN:
            node->evaluated_type = node->as.pattern.type ? node->as.pattern.type : make_base_type("Double");
            node->symbol = symbol_define(node->as.pattern.name.start, node->as.pattern.name.length,
                                         SYM_VAR, node->evaluated_type, node->line, false);
            break;

        case AST_BINDING_ASSIGN: {
            check_node(node->as.binding_assign.value);
            check_binding_list(node->as.binding_assign.bindings);
            for (size_t i = 0; i < node->as.binding_assign.bindings->as.binding_list.count; i++) {
                ASTNode* binding = node->as.binding_assign.bindings->as.binding_list.items[i];
                TypeExpr* value_type = tuple_element_type(node->as.binding_assign.value->evaluated_type, i);
                if (binding->as.pattern.type && value_type &&
                    !same_base_name(binding->as.pattern.type, "_") &&
                    !type_compatible(binding->as.pattern.type, value_type)) {
                    semantic_error(binding->line, "Type mismatch in binding '%.*s'",
                                   (int)binding->as.pattern.name.length, binding->as.pattern.name.start);
                }
                if ((!binding->as.pattern.type || same_base_name(binding->as.pattern.type, "_")) && value_type) {
                    binding->evaluated_type = value_type;
                    if (binding->symbol) binding->symbol->type = value_type;
                }
            }
            break;
        }

        case AST_IF_STMT:
            check_node(node->as.if_stmt.condition);
            check_node(node->as.if_stmt.then_branch);
            check_node(node->as.if_stmt.else_branch);
            break;

        case AST_WHILE_STMT:
            loop_depth++;
            check_node(node->as.while_stmt.condition);
            check_node(node->as.while_stmt.body);
            loop_depth--;
            break;

        case AST_FOR_STMT:
            check_node(node->as.for_stmt.iterable);
            scope_push();
            if (node->as.for_stmt.pattern->type == AST_BINDING_LIST) {
                check_binding_list(node->as.for_stmt.pattern);
            } else {
                check_node(node->as.for_stmt.pattern);
            }
            loop_depth++;
            check_node(node->as.for_stmt.body);
            loop_depth--;
            scope_pop();
            break;

        case AST_RETURN_STMT:
            check_node(node->as.return_stmt.expression);
            if (node->as.return_stmt.expression) node->evaluated_type = node->as.return_stmt.expression->evaluated_type;
            break;

        case AST_BINARY_EXPR:
            check_node(node->as.binary.left);
            check_node(node->as.binary.right);
            if (node->as.binary.left) node->evaluated_type = node->as.binary.left->evaluated_type;
            break;

        case AST_FUNC_CALL:
            check_node(node->as.func_call.callee);
            for (size_t i = 0; i < node->as.func_call.arg_count; i++) check_node(node->as.func_call.args[i]);
            if (node->as.func_call.callee) node->evaluated_type = node->as.func_call.callee->evaluated_type;
            break;

        case AST_MEMBER_ACCESS: {
            check_node(node->as.member_access.object);
            ASTNode* object = node->as.member_access.object;

            if (object->type == AST_IDENTIFIER && object->symbol && object->symbol->kind == SYM_STRUCT) {
                node->evaluated_type = object->symbol->type;
                break;
            }

            if (object->type == AST_IDENTIFIER && object->symbol && object->symbol->kind == SYM_NAMESPACE) {
                Module* mod = (Module*)object->symbol->module_ptr;
                Symbol* exported = module_lookup_export(mod, node->as.member_access.member);
                if (!exported) {
                    semantic_error(node->line, "Undefined identifier '%.*s'",
                                   (int)node->as.member_access.member.length, node->as.member_access.member.start);
                } else {
                    node->symbol = exported;
                    node->evaluated_type = exported->type;
                }
                break;
            }

            if (token_eq_cstr(node->as.member_access.member, "value")) {
                node->evaluated_type = make_base_type("Int");
                break;
            }

            TypeExpr* object_type = unwrap_type(object->evaluated_type);
            TypeExpr* owner_type = object_type;
            if (owner_type && owner_type->kind == TYPE_POINTER) owner_type = unwrap_type(owner_type->as.pointer.element);

            Module* owner_mod = current_module;
            if (object->symbol && object->symbol->module_owner) owner_mod = (Module*)object->symbol->module_owner;
            else if (owner_type && owner_type->kind == TYPE_BASE) {
                Symbol* type_sym = symbol_lookup(owner_type->as.base_type);
                if (type_sym && type_sym->module_owner) owner_mod = (Module*)type_sym->module_owner;
            }

            ASTNode* struct_decl = module_find_struct_decl(owner_mod, owner_type);
            ASTNode* field = find_struct_field(struct_decl, node->as.member_access.member);
            if (field) {
                if (owner_mod != current_module && !field->is_public) {
                    semantic_error(node->line, "Field '%.*s' is private",
                                   (int)node->as.member_access.member.length, node->as.member_access.member.start);
                }
                node->evaluated_type = field->as.var_decl.type;
            } else {
                node->evaluated_type = make_base_type("Int");
            }
            break;
        }

        case AST_INDEX_EXPR: {
            check_node(node->as.index_expr.object);
            check_node(node->as.index_expr.index);
            TypeExpr* object_type = unwrap_type(node->as.index_expr.object->evaluated_type);
            if (object_type && node->as.index_expr.index && node->as.index_expr.index->type == AST_RANGE_EXPR) {
                if (object_type->kind == TYPE_ARRAY) node->evaluated_type = make_slice_type(object_type->as.array.element);
                else if (object_type->kind == TYPE_SLICE) node->evaluated_type = make_slice_type(object_type->as.slice.element);
            } else if (object_type && object_type->kind == TYPE_ARRAY) {
                node->evaluated_type = object_type->as.array.element;
            } else if (object_type && object_type->kind == TYPE_SLICE) {
                node->evaluated_type = object_type->as.slice.element;
            } else {
                node->evaluated_type = make_base_type("Int");
            }
            break;
        }

        case AST_ARRAY_LITERAL:
            if (node->as.array_literal.type) node->evaluated_type = node->as.array_literal.type;
            break;

        case AST_STRUCT_INIT_EXPR: {
            node->evaluated_type = node->as.struct_init.type;
            Module* owner_mod = current_module;
            if (node->as.struct_init.type && node->as.struct_init.type->kind == TYPE_BASE) {
                Symbol* type_sym = symbol_lookup(node->as.struct_init.type->as.base_type);
                if (type_sym && type_sym->module_owner) owner_mod = (Module*)type_sym->module_owner;
            }
            ASTNode* struct_decl = module_find_struct_decl(owner_mod, node->as.struct_init.type);
            for (size_t i = 0; i < node->as.struct_init.field_count; i++) {
                check_node(node->as.struct_init.field_values[i]);
                ASTNode* field = find_struct_field(struct_decl, node->as.struct_init.field_names[i]);
                if (field && owner_mod != current_module && !field->is_public) {
                    semantic_error(node->line, "Field '%.*s' is private",
                                   (int)node->as.struct_init.field_names[i].length, node->as.struct_init.field_names[i].start);
                }
            }
            break;
        }

        case AST_NEW_EXPR:
            check_node(node->as.new_expr.value);
            if (node->as.new_expr.value) node->evaluated_type = make_pointer_type(node->as.new_expr.value->evaluated_type);
            break;

        case AST_UNARY_EXPR:
            check_node(node->as.unary.right);
            if (node->as.unary.op == TOKEN_STAR) {
                TypeExpr* right_type = unwrap_type(node->as.unary.right->evaluated_type);
                if (!right_type || right_type->kind != TYPE_POINTER) {
                    semantic_error(node->line, "Cannot dereference non-pointer");
                } else {
                    node->evaluated_type = right_type->as.pointer.element;
                }
            } else {
                node->evaluated_type = node->as.unary.right ? node->as.unary.right->evaluated_type : NULL;
            }
            break;

        default:
            break;
    }
}

void semantic_reset(void) {
    module_cache_count = 0;
    had_error = false;
    loop_depth = 0;
}

Module* semantic_check(Arena* arena, ASTNode* root, const char* input_path) {
    char path[PATH_MAX];
    if (realpath(input_path, path) == NULL) strncpy(path, input_path, sizeof(path));

    for (int i = 0; i < module_cache_count; i++) {
        if (strcmp(module_cache[i]->path, path) == 0) return module_cache[i];
    }

    Module* prev_mod = current_module;
    Scope* prev_scope = current_scope;
    eval_arena = arena;

    Module* mod = malloc(sizeof(Module));
    strncpy(mod->path, path, sizeof(mod->path));
    mod->root = root;
    mod->is_analyzed = false;
    mod->top_level_scope = NULL;

    char* bname = strrchr(path, '/');
    const char* filename = bname ? bname + 1 : path;
    strncpy(mod->name, filename, sizeof(mod->name));
    char* dot = strrchr(mod->name, '.');
    if (dot) *dot = '\0';

    uint64_t hash = djb2_hash(path);
    snprintf(mod->id, sizeof(mod->id), "%016llx", (unsigned long long)hash);

    current_module = mod;
    module_cache[module_cache_count++] = mod;
    current_scope = NULL;
    scope_push();
    register_builtins();
    check_node(root);
    mod->top_level_scope = current_scope;
    current_scope = prev_scope;
    current_module = prev_mod;
    mod->is_analyzed = true;
    return had_error ? NULL : mod;
}
