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
    HIRModule*  hir;
    CodegenModuleInfo* codegen;
} Module;

static Scope*  current_scope = NULL;
static Module* current_module = NULL;
static bool    had_error     = false;
static int     loop_depth    = 0;
static Arena*  eval_arena    = NULL;
static Module* module_cache[128];
static int     module_cache_count = 0;
static char    stdlib_dir[PATH_MAX] = "std";

const char* module_get_name(Module* mod) { return mod->name; }
const char* module_get_id(Module* mod) { return mod->id; }
ASTNode* module_get_root(Module* mod) { return mod->root; }
HIRModule* module_get_hir(Module* mod) { return mod->hir; }
CodegenModuleInfo* module_get_codegen_info(Module* mod) { return mod->codegen; }

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
            if (same_base_name(expected, "Double")) {
                return same_base_name(actual, "Double") ||
                       same_base_name(actual, "Float") ||
                       same_base_name(actual, "Int");
            }
            if (same_base_name(expected, "Float")) {
                return same_base_name(actual, "Float") ||
                       same_base_name(actual, "Int");
            }
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

static Symbol* current_scope_lookup(Token name) {
    if (!current_scope) return NULL;
    for (Symbol* sym = current_scope->symbols; sym; sym = sym->next) {
        if (strlen(sym->name) == name.length && strncmp(sym->name, name.start, name.length) == 0) return sym;
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

static ASTNode* find_struct_method(ASTNode* struct_decl, Token member) {
    if (!struct_decl || struct_decl->type != AST_STRUCT_DECL) return NULL;
    for (size_t i = 0; i < struct_decl->as.struct_decl.member_count; i++) {
        ASTNode* method = struct_decl->as.struct_decl.members[i];
        if (method->type == AST_FUNC_DECL && token_eq(method->as.func_decl.name, member)) return method;
    }
    return NULL;
}

static ASTNode* module_find_enum_decl(Module* mod, Token name) {
    if (!mod || !mod->root) return NULL;
    for (size_t i = 0; i < mod->root->as.block.count; i++) {
        ASTNode* stmt = mod->root->as.block.statements[i];
        if (stmt->type == AST_ENUM_DECL && token_eq(stmt->as.enum_decl.name, name)) return stmt;
    }
    return NULL;
}

static bool enum_has_member(ASTNode* enum_decl, Token member) {
    if (!enum_decl || enum_decl->type != AST_ENUM_DECL) return false;
    for (size_t i = 0; i < enum_decl->as.enum_decl.member_count; i++) {
        if (token_eq(enum_decl->as.enum_decl.member_names[i], member)) return true;
    }
    return false;
}

static Token semantic_make_generated_name(const char* prefix, Token suffix) {
    size_t prefix_len = strlen(prefix);
    size_t len = prefix_len + suffix.length;
    char* text = arena_alloc(eval_arena, len + 1);
    memcpy(text, prefix, prefix_len);
    memcpy(text + prefix_len, suffix.start, suffix.length);
    text[len] = '\0';
    Token token = { TOKEN_IDENTIFIER, text, (int)len, suffix.line };
    return token;
}

static Token semantic_make_joined_name(Token left, const char* sep, Token right) {
    size_t sep_len = strlen(sep);
    size_t len = left.length + sep_len + right.length;
    char* text = arena_alloc(eval_arena, len + 1);
    memcpy(text, left.start, left.length);
    memcpy(text + left.length, sep, sep_len);
    memcpy(text + left.length + sep_len, right.start, right.length);
    text[len] = '\0';
    Token token = { TOKEN_IDENTIFIER, text, (int)len, left.line };
    return token;
}

static bool semantic_is_static_struct_method(ASTNode* method) {
    return method && method->type == AST_FUNC_DECL &&
           method->as.func_decl.name.length == 4 &&
           strncmp(method->as.func_decl.name.start, "open", 4) == 0;
}

static TypeExpr* codegen_effective_type(TypeExpr* declared_type, TypeExpr* evaluated_type) {
    if (!declared_type) return evaluated_type;
    if (declared_type->kind == TYPE_BASE &&
        declared_type->as.base_type.length == 1 &&
        declared_type->as.base_type.start[0] == '_') {
        return evaluated_type;
    }
    return declared_type;
}

static void build_codegen_info(Module* mod) {
    ASTNode* root = mod->root;
    CodegenModuleInfo* info = arena_alloc(eval_arena, sizeof(CodegenModuleInfo));
    memset(info, 0, sizeof(CodegenModuleInfo));

    size_t import_count = 0;
    size_t enum_count = 0;
    size_t struct_count = 0;
    size_t union_count = 0;
    size_t function_count = 0;
    size_t global_count = 0;
    for (size_t i = 0; i < root->as.block.count; i++) {
        ASTNode* node = root->as.block.statements[i];
        switch (node->type) {
            case AST_IMPORT: import_count++; break;
            case AST_ENUM_DECL: enum_count++; break;
            case AST_STRUCT_DECL: struct_count++; break;
            case AST_UNION_DECL: union_count++; break;
            case AST_FUNC_DECL: function_count++; break;
            case AST_VAR_DECL: global_count++; break;
            default: break;
        }
    }

    info->imports = import_count ? arena_alloc(eval_arena, sizeof(CodegenImportInfo) * import_count) : NULL;
    info->enums = enum_count ? arena_alloc(eval_arena, sizeof(CodegenEnumInfo) * enum_count) : NULL;
    info->structs = struct_count ? arena_alloc(eval_arena, sizeof(CodegenStructInfo) * struct_count) : NULL;
    info->unions = union_count ? arena_alloc(eval_arena, sizeof(CodegenUnionInfo) * union_count) : NULL;
    info->functions = function_count ? arena_alloc(eval_arena, sizeof(CodegenFunctionInfo) * function_count) : NULL;
    info->globals = global_count ? arena_alloc(eval_arena, sizeof(CodegenGlobalInfo) * global_count) : NULL;

    size_t import_index = 0;
    size_t enum_index = 0;
    size_t struct_index = 0;
    size_t union_index = 0;
    size_t function_index = 0;
    size_t global_index = 0;

    for (size_t i = 0; i < root->as.block.count; i++) {
        ASTNode* node = root->as.block.statements[i];
        if (node->type == AST_IMPORT) {
            if (node->as.import_decl.resolved_path[0] == '\0') continue;
            char path[256];
            strncpy(path, node->as.import_decl.resolved_path, sizeof(path));
            char* dot_c = strstr(path, ".c");
            if (dot_c) memcpy(dot_c, ".h", 2);
            char* base = strrchr(path, '/');
            snprintf(info->imports[import_index++].header_path, 256, "%s", base ? base + 1 : path);
            continue;
        }

        if (node->type == AST_ENUM_DECL) {
            CodegenEnumInfo* e = &info->enums[enum_index++];
            e->name = node->as.enum_decl.name;
            e->base_type = node->as.enum_decl.base_type;
            e->member_count = node->as.enum_decl.member_count;
            e->members = arena_alloc(eval_arena, sizeof(CodegenEnumMemberInfo) * e->member_count);
            int64_t next_value = 0;
            for (size_t j = 0; j < e->member_count; j++) {
                e->members[j].name = node->as.enum_decl.member_names[j];
                ASTNode* value = node->as.enum_decl.member_values[j];
                if (value && value->type == AST_LITERAL_NUMBER) {
                    next_value = (int64_t)value->as.number.value;
                }
                e->members[j].value = next_value++;
            }
            continue;
        }

        if (node->type == AST_STRUCT_DECL) {
            CodegenStructInfo* s = &info->structs[struct_index++];
            memset(s, 0, sizeof(*s));
            s->name = node->as.struct_decl.name;
            size_t field_count = 0;
            size_t method_count = 0;
            for (size_t j = 0; j < node->as.struct_decl.member_count; j++) {
                ASTNode* member = node->as.struct_decl.members[j];
                if (member->type == AST_VAR_DECL) field_count++;
                else if (member->type == AST_FUNC_DECL) method_count++;
                else if (member->type == AST_INIT_DECL) s->has_init = true;
            }
            s->field_count = field_count;
            s->fields = field_count ? arena_alloc(eval_arena, sizeof(CodegenFieldInfo) * field_count) : NULL;
            s->method_count = method_count;
            s->methods = method_count ? arena_alloc(eval_arena, sizeof(CodegenFunctionInfo) * method_count) : NULL;

            size_t field_index = 0;
            size_t method_index = 0;
            for (size_t j = 0; j < node->as.struct_decl.member_count; j++) {
                ASTNode* member = node->as.struct_decl.members[j];
                if (member->type == AST_VAR_DECL) {
                    s->fields[field_index].name = member->as.var_decl.name;
                    s->fields[field_index].type = codegen_effective_type(member->as.var_decl.type, member->evaluated_type);
                    s->fields[field_index].is_public = member->is_public;
                    field_index++;
                } else if (member->type == AST_FUNC_DECL) {
                    CodegenFunctionInfo* f = &s->methods[method_index++];
                    memset(f, 0, sizeof(*f));
                    f->name = member->as.func_decl.name;
                    f->generated_name = semantic_make_joined_name(s->name, "_", member->as.func_decl.name);
                    f->return_type = member->as.func_decl.return_type;
                    f->is_public = member->is_public;
                    f->is_static_method = semantic_is_static_struct_method(member);
                    f->is_method = true;
                    f->param_count = member->as.func_decl.param_count;
                    f->params = f->param_count ? arena_alloc(eval_arena, sizeof(CodegenParamInfo) * f->param_count) : NULL;
                    for (size_t k = 0; k < f->param_count; k++) {
                        f->params[k].name = member->as.func_decl.params[k]->as.var_decl.name;
                        f->params[k].type = codegen_effective_type(member->as.func_decl.params[k]->as.var_decl.type,
                                                                   member->as.func_decl.params[k]->evaluated_type);
                    }
                } else if (member->type == AST_INIT_DECL) {
                    s->init_param_count = member->as.init_decl.param_count;
                    s->init_params = s->init_param_count ? arena_alloc(eval_arena, sizeof(CodegenParamInfo) * s->init_param_count) : NULL;
                    for (size_t k = 0; k < s->init_param_count; k++) {
                        s->init_params[k].name = member->as.init_decl.params[k]->as.var_decl.name;
                        s->init_params[k].type = codegen_effective_type(member->as.init_decl.params[k]->as.var_decl.type,
                                                                        member->as.init_decl.params[k]->evaluated_type);
                    }
                }
            }
            continue;
        }

        if (node->type == AST_UNION_DECL) {
            CodegenUnionInfo* u = &info->unions[union_index++];
            memset(u, 0, sizeof(*u));
            u->name = node->as.union_decl.name;
            u->tag_enum = node->as.union_decl.tag_enum;
            u->variant_count = node->as.union_decl.member_count;
            u->variants = arena_alloc(eval_arena, sizeof(CodegenUnionVariantInfo) * u->variant_count);
            for (size_t j = 0; j < u->variant_count; j++) {
                ASTNode* member = node->as.union_decl.members[j];
                u->variants[j].name = member->as.var_decl.name;
                u->variants[j].type = codegen_effective_type(member->as.var_decl.type, member->evaluated_type);
            }
            continue;
        }

        if (node->type == AST_FUNC_DECL) {
            CodegenFunctionInfo* f = &info->functions[function_index++];
            memset(f, 0, sizeof(*f));
            f->name = node->as.func_decl.name;
            f->generated_name = node->as.func_decl.name;
            f->return_type = node->as.func_decl.return_type;
            f->is_public = node->is_public;
            f->param_count = node->as.func_decl.param_count;
            f->params = f->param_count ? arena_alloc(eval_arena, sizeof(CodegenParamInfo) * f->param_count) : NULL;
            for (size_t j = 0; j < f->param_count; j++) {
                f->params[j].name = node->as.func_decl.params[j]->as.var_decl.name;
                f->params[j].type = codegen_effective_type(node->as.func_decl.params[j]->as.var_decl.type,
                                                           node->as.func_decl.params[j]->evaluated_type);
            }
            continue;
        }

        if (node->type == AST_VAR_DECL) {
            CodegenGlobalInfo* g = &info->globals[global_index++];
            memset(g, 0, sizeof(*g));
            g->name = node->as.var_decl.name;
            g->type = codegen_effective_type(node->as.var_decl.type, node->evaluated_type);
            g->is_public = node->is_public;
            ASTNode* init = node->as.var_decl.initializer;
            if (init && init->type == AST_LITERAL_NUMBER) {
                if (init->evaluated_type && init->evaluated_type->kind == TYPE_BASE &&
                    init->evaluated_type->as.base_type.length == 3 &&
                    strncmp(init->evaluated_type->as.base_type.start, "Int", 3) == 0) {
                    g->init_kind = CG_GLOBAL_INIT_INT;
                    g->int_value = (int64_t)init->as.number.value;
                } else {
                    g->init_kind = CG_GLOBAL_INIT_FLOAT;
                    g->float_value = init->as.number.value;
                }
            } else if (init && init->type == AST_LITERAL_STRING) {
                g->init_kind = CG_GLOBAL_INIT_STRING;
                g->string_value = init->as.string.value;
            } else if (init && init->type == AST_ENUM_ACCESS) {
                g->init_kind = CG_GLOBAL_INIT_ENUM;
                g->enum_name = init->as.enum_access.enum_name;
                g->enum_member = init->as.enum_access.member_name;
            }
            continue;
        }
    }

    info->import_count = import_index;
    info->enum_count = enum_index;
    info->struct_count = struct_index;
    info->union_count = union_index;
    info->function_count = function_index;
    info->global_count = global_index;
    mod->codegen = info;
}

static void register_builtins(void) {
    const char* builtins[] = {"Int", "Float", "Double", "UInt8", "UInt16", "Bool"};
    for (size_t i = 0; i < 6; i++) {
        symbol_define(builtins[i], strlen(builtins[i]), SYM_STRUCT, make_base_type(builtins[i]), 0, false);
    }
    symbol_define("print", 5, SYM_FUNC, make_base_type("void"), 0, false);
    symbol_define("assert", 6, SYM_FUNC, make_base_type("void"), 0, false);
    symbol_define("__intrinsic_print", 17, SYM_FUNC, make_base_type("void"), 0, false);
    symbol_define("__intrinsic_assert", 18, SYM_FUNC, make_base_type("void"), 0, false);
    symbol_define("__intrinsic_read_file", 21, SYM_FUNC,
                  make_slice_type(make_base_type("UInt8")), 0, false);
    symbol_define("__intrinsic_file_exists", 23, SYM_FUNC,
                  make_base_type("Bool"), 0, false);
    symbol_define("__intrinsic_alloc_ints", 22, SYM_FUNC,
                  make_slice_type(make_base_type("Int")), 0, false);
    symbol_define("__intrinsic_alloc_bytes", 23, SYM_FUNC,
                  make_slice_type(make_base_type("UInt8")), 0, false);
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

static bool file_exists(const char* path) {
    return access(path, F_OK) == 0;
}


static bool resolve_import_path(const char* importer_path, Token path_token, char* out, size_t out_size) {
    char import_path[PATH_MAX];
    size_t import_len = path_token.length >= 2 ? (size_t)path_token.length - 2 : 0;
    if (import_len >= sizeof(import_path)) import_len = sizeof(import_path) - 1;
    memcpy(import_path, path_token.start + 1, import_len);
    import_path[import_len] = '\0';

    if (import_path[0] == '/') {
        if (realpath(import_path, out) != NULL) return true;
        if (file_exists(import_path)) {
            strncpy(out, import_path, out_size);
            return true;
        }
    }

    if (strncmp(import_path, "std/", 4) == 0) {
        char candidate[PATH_MAX];
        snprintf(candidate, sizeof(candidate), "%s/%s", stdlib_dir, import_path + 4);
        if (realpath(candidate, out) != NULL) return true;
        if (file_exists(candidate)) {
            strncpy(out, candidate, out_size);
            return true;
        }
    }

    char importer_dir[PATH_MAX];
    strncpy(importer_dir, importer_path, sizeof(importer_dir));
    char* last_slash = strrchr(importer_dir, '/');
    if (last_slash) *last_slash = '\0';
    else strcpy(importer_dir, ".");

    char candidate[PATH_MAX];
    snprintf(candidate, sizeof(candidate), "%s/%s", importer_dir, import_path);
    if (realpath(candidate, out) != NULL) return true;
    if (file_exists(candidate)) {
        strncpy(out, candidate, out_size);
        return true;
    }

    snprintf(candidate, sizeof(candidate), "%s", import_path);
    if (realpath(candidate, out) != NULL) return true;
    if (file_exists(candidate)) {
        strncpy(out, candidate, out_size);
        return true;
    }

    return false;
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

        case AST_ENUM_DECL:
            node->symbol = symbol_define(node->as.enum_decl.name.start, node->as.enum_decl.name.length,
                                         SYM_ENUM, make_named_type(node->as.enum_decl.name),
                                         node->line, node->is_public);
            node->evaluated_type = node->symbol ? node->symbol->type : make_named_type(node->as.enum_decl.name);
            break;

        case AST_UNION_DECL:
            node->symbol = symbol_define(node->as.union_decl.name.start, node->as.union_decl.name.length,
                                         SYM_STRUCT, make_named_type(node->as.union_decl.name),
                                         node->line, node->is_public);
            break;

        case AST_IMPORT: {
            char import_text[PATH_MAX];
            size_t import_len = node->as.import_decl.path.length >= 2 ? (size_t)node->as.import_decl.path.length - 2 : 0;
            if (import_len >= sizeof(import_text)) import_len = sizeof(import_text) - 1;
            memcpy(import_text, node->as.import_decl.path.start + 1, import_len);
            import_text[import_len] = '\0';

            char abs_path[PATH_MAX];
            if (!resolve_import_path(current_module->path, node->as.import_decl.path, abs_path, sizeof(abs_path))) {
                semantic_error(node->line, "Could not resolve import '%s'", import_text);
                break;
            }

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
                Symbol* ns = current_scope_lookup(node->as.import_decl.alias);
                if (ns &&
                    (token_eq_cstr(node->as.import_decl.alias, "assert") ||
                     token_eq_cstr(node->as.import_decl.alias, "print"))) {
                    ns->kind = SYM_NAMESPACE;
                    ns->type = NULL;
                    ns->is_public = node->as.import_decl.is_public;
                } else {
                    ns = symbol_define(node->as.import_decl.alias.start, node->as.import_decl.alias.length,
                                       SYM_NAMESPACE, NULL, node->line, node->as.import_decl.is_public);
                }
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
                       token_eq_cstr(node->as.identifier.name, "assert") ||
                       token_eq_cstr(node->as.identifier.name, "__intrinsic_print") ||
                       token_eq_cstr(node->as.identifier.name, "__intrinsic_assert")) {
                node->evaluated_type = make_base_type("void");
            } else {
                semantic_error(node->line, "Undefined identifier '%.*s'",
                               (int)node->as.identifier.name.length, node->as.identifier.name.start);
            }
            break;
        }

        case AST_LITERAL_NUMBER:
            if (node->as.number.value == (int64_t)node->as.number.value) {
                node->evaluated_type = make_base_type("Int");
            } else {
                node->evaluated_type = make_base_type("Double");
            }
            break;

        case AST_LITERAL_STRING:
            node->evaluated_type = make_slice_type(make_base_type("UInt8"));
            break;

        case AST_TUPLE_LITERAL: {
            size_t count = node->as.tuple_literal.count;
            if (count == 0) {
                node->evaluated_type = make_base_type("()");
                break;
            }
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

        case AST_SWITCH_STMT:
            check_node(node->as.switch_stmt.expression);
            for (size_t i = 0; i < node->as.switch_stmt.case_count; i++) {
                ASTNode* case_node = node->as.switch_stmt.cases[i];
                scope_push();
                check_node(case_node->as.switch_case.pattern);
                if (case_node->as.switch_case.pattern) {
                    ASTNode* switch_value = node->as.switch_stmt.expression;
                    ASTNode* pattern = case_node->as.switch_case.pattern;
                    ASTNode* left = pattern;
                    if (pattern->type == AST_FUNC_CALL) {
                        for (size_t j = 0; j < pattern->as.func_call.arg_count; j++) {
                            ASTNode* binding = pattern->as.func_call.args[j];
                            if (binding->type != AST_PATTERN) continue;
                            TypeExpr* value_type = NULL;
                            if (pattern->as.func_call.callee &&
                                pattern->as.func_call.callee->type == AST_MEMBER_ACCESS &&
                                switch_value->evaluated_type &&
                                switch_value->evaluated_type->kind == TYPE_BASE) {
                                TypeExpr* union_type = unwrap_type(switch_value->evaluated_type);
                                Symbol* union_sym = symbol_lookup(union_type->as.base_type);
                                if (union_sym) {
                                    Module* owner_mod = union_sym->module_owner ? (Module*)union_sym->module_owner : current_module;
                                    if (owner_mod && owner_mod->root) {
                                        for (size_t k = 0; k < owner_mod->root->as.block.count; k++) {
                                            ASTNode* stmt = owner_mod->root->as.block.statements[k];
                                            if (stmt->type != AST_UNION_DECL) continue;
                                            if (!token_eq(stmt->as.union_decl.name, union_type->as.base_type)) continue;
                                            Token variant_name = pattern->as.func_call.callee->as.member_access.member;
                                            for (size_t m = 0; m < stmt->as.union_decl.member_count; m++) {
                                                ASTNode* member = stmt->as.union_decl.members[m];
                                                if (token_eq(member->as.var_decl.name, variant_name)) {
                                                    value_type = member->as.var_decl.type;
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            if ((!binding->as.pattern.type || same_base_name(binding->as.pattern.type, "_")) && value_type) {
                                binding->evaluated_type = value_type;
                                if (binding->symbol) binding->symbol->type = value_type;
                            } else if (binding->as.pattern.type && value_type &&
                                       !same_base_name(binding->as.pattern.type, "_") &&
                                       !type_compatible(binding->as.pattern.type, value_type)) {
                                semantic_error(binding->line, "Type mismatch in switch binding '%.*s'",
                                               (int)binding->as.pattern.name.length, binding->as.pattern.name.start);
                            }
                        }
                    } else {
                        left = switch_value;
                    }
                    (void)left;
                }
                check_node(case_node->as.switch_case.body);
                scope_pop();
            }
            if (node->as.switch_stmt.else_branch) {
                scope_push();
                check_node(node->as.switch_stmt.else_branch);
                scope_pop();
            }
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

            if (object->symbol && object->symbol->kind == SYM_NAMESPACE) {
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

            if (object->symbol && object->symbol->kind == SYM_ENUM) {
                Token enum_name = object->symbol->type ? object->symbol->type->as.base_type : (Token){0};
                Token member_name = node->as.member_access.member;
                Module* owner_mod = current_module;
                if (object->type == AST_MEMBER_ACCESS &&
                    object->as.member_access.object &&
                    object->as.member_access.object->symbol &&
                    object->as.member_access.object->symbol->kind == SYM_NAMESPACE) {
                    owner_mod = (Module*)object->as.member_access.object->symbol->module_ptr;
                } else if (object->symbol->module_owner) {
                    owner_mod = (Module*)object->symbol->module_owner;
                }

                ASTNode* enum_decl = module_find_enum_decl(owner_mod, enum_name);
                if (!enum_has_member(enum_decl, member_name)) {
                    semantic_error(node->line, "Enum '%.*s' has no member '%.*s'",
                                   (int)enum_name.length, enum_name.start,
                                   (int)member_name.length, member_name.start);
                    break;
                }

                node->type = AST_ENUM_ACCESS;
                node->as.enum_access.enum_name = enum_name;
                node->as.enum_access.member_name = member_name;
                node->evaluated_type = object->symbol->type;
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
            if (object->symbol && object->symbol->kind == SYM_STRUCT &&
                object->type == AST_MEMBER_ACCESS &&
                object->as.member_access.object &&
                object->as.member_access.object->symbol &&
                object->as.member_access.object->symbol->kind == SYM_NAMESPACE) {
                owner_mod = (Module*)object->as.member_access.object->symbol->module_ptr;
            }
            if (object->symbol && object->symbol->module_owner) owner_mod = (Module*)object->symbol->module_owner;
            else if (owner_type && owner_type->kind == TYPE_BASE) {
                Symbol* type_sym = symbol_lookup(owner_type->as.base_type);
                if (type_sym && type_sym->module_owner) owner_mod = (Module*)type_sym->module_owner;
            }

            ASTNode* struct_decl = module_find_struct_decl(owner_mod, owner_type);
            if (!struct_decl && object->symbol && object->symbol->kind == SYM_STRUCT) {
                node->evaluated_type = object->symbol->type;
                break;
            }
            ASTNode* method = find_struct_method(struct_decl, node->as.member_access.member);
            if (method) {
                node->evaluated_type = method->as.func_decl.return_type;
                node->symbol = method->symbol;
                break;
            }
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

void semantic_set_stdlib_dir(const char* path) {
    if (!path || !path[0]) {
        strncpy(stdlib_dir, "std", sizeof(stdlib_dir));
        return;
    }
    strncpy(stdlib_dir, path, sizeof(stdlib_dir));
    stdlib_dir[sizeof(stdlib_dir) - 1] = '\0';
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
    mod->hir = NULL;
    mod->codegen = NULL;
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
    if (!had_error) {
        mod->hir = hir_build_module(root, arena);
        build_codegen_info(mod);
    }
    mod->top_level_scope = current_scope;
    current_scope = prev_scope;
    current_module = prev_mod;
    mod->is_analyzed = true;
    return had_error ? NULL : mod;
}
