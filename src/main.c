#include "llvm_emit.h"
#include "lower.h"
#include "parser.h"
#include "vec.h"

#include <llvm-c/Core.h>
#include <ctype.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define function_list_push(list, fn) VEC_PUSH((list), (fn))
#define global_list_push(list, global) VEC_PUSH((list), (global))
#define struct_list_push(list, struct_decl) VEC_PUSH((list), (struct_decl))
#define enum_list_push(list, enum_decl) VEC_PUSH((list), (enum_decl))
#define union_list_push(list, union_decl) VEC_PUSH((list), (union_decl))
#define param_list_push(list, param) VEC_PUSH((list), (param))
#define stmt_list_push(list, stmt) VEC_PUSH((list), (stmt))
#define expr_list_push(list, expr) VEC_PUSH((list), (expr))
#define type_list_push(list, type) VEC_PUSH((list), (type))
#define struct_field_list_push(list, field) VEC_PUSH((list), (field))
#define struct_field_init_list_push(list, field) VEC_PUSH((list), (field))
#define enum_member_list_push(list, member) VEC_PUSH((list), (member))
#define union_variant_list_push(list, variant) VEC_PUSH((list), (variant))
#define binding_pattern_list_push(list, pattern) VEC_PUSH((list), (pattern))
#define switch_case_list_push(list, switch_case) VEC_PUSH((list), (switch_case))
#define switch_expr_case_list_push(list, switch_case) VEC_PUSH((list), (switch_case))
#define try_catch_list_push(list, try_catch) VEC_PUSH((list), (try_catch))
#define expr_try_catch_list_push(list, try_catch) VEC_PUSH((list), (try_catch))
#define name_list_push(list, name) VEC_PUSH((list), (name))
#define concept_list_push(list, concept_decl) VEC_PUSH((list), (concept_decl))
#define where_constraint_list_push(list, constraint) VEC_PUSH((list), (constraint))
#define concept_method_list_push(list, method) VEC_PUSH((list), (method))
#define assoc_type_decl_list_push(list, assoc_type_decl) VEC_PUSH((list), (assoc_type_decl))
#define assoc_type_binding_list_push(list, assoc_type_binding) VEC_PUSH((list), (assoc_type_binding))
#define struct_init_decl_list_push(list, init_decl) VEC_PUSH((list), (init_decl))

static char* dup_text(const char* text) {
    size_t n = strlen(text);
    char* out = (char*)malloc(n + 1);
    if (!out) {
        return 0;
    }
    memcpy(out, text, n + 1);
    return out;
}

static char* dup_join3(const char* left, const char* middle, const char* right) {
    size_t left_len = strlen(left);
    size_t middle_len = strlen(middle);
    size_t right_len = strlen(right);
    char* text = (char*)malloc(left_len + middle_len + right_len + 1);
    if (!text) {
        return 0;
    }
    memcpy(text, left, left_len);
    memcpy(text + left_len, middle, middle_len);
    memcpy(text + left_len + middle_len, right, right_len);
    text[left_len + middle_len + right_len] = '\0';
    return text;
}

static char* read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    char* buffer = 0;
    long length = 0;
    size_t read_bytes = 0;

    if (!file) {
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    length = ftell(file);
    if (length < 0) {
        fclose(file);
        return 0;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    buffer = (char*)malloc((size_t)length + 1);
    if (!buffer) {
        fclose(file);
        return 0;
    }

    read_bytes = fread(buffer, 1, (size_t)length, file);
    fclose(file);
    if (read_bytes != (size_t)length) {
        free(buffer);
        return 0;
    }
    buffer[length] = '\0';
    return buffer;
}

static void print_source_excerpt(const char* path, int line, int column) {
    char* source = 0;
    const char* cursor = 0;
    const char* line_start = 0;
    const char* line_end = 0;
    int current_line = 1;
    int i = 0;
    if (!path || !*path || line <= 0) {
        return;
    }
    source = read_file(path);
    if (!source) {
        return;
    }
    cursor = source;
    line_start = source;
    while (*cursor && current_line < line) {
        if (*cursor == '\n') {
            current_line += 1;
            line_start = cursor + 1;
        }
        cursor += 1;
    }
    if (current_line != line) {
        free(source);
        return;
    }
    line_end = line_start;
    while (*line_end && *line_end != '\n' && *line_end != '\r') {
        line_end += 1;
    }
    fprintf(stderr, "  %.*s\n", (int)(line_end - line_start), line_start);
    if (column > 0) {
        fprintf(stderr, "  ");
        for (i = 1; i < column; ++i) {
            if (line_start[i - 1] == '\t') {
                fputc('\t', stderr);
            } else {
                fputc(' ', stderr);
            }
        }
        fprintf(stderr, "^\n");
    }
    free(source);
}

static int parse_embedded_diagnostic(const char* error,
                                     const char** out_path,
                                     size_t* out_path_len,
                                     int* out_line,
                                     int* out_column,
                                     const char** out_message) {
    const char* p = 0;
    const char* path_end = 0;
    const char* msg = 0;
    int line = 0;
    int column = 0;
    if (!error || !out_path || !out_path_len || !out_line || !out_column || !out_message) {
        return 0;
    }
    path_end = strchr(error, ':');
    if (!path_end || path_end == error) {
        return 0;
    }
    p = path_end + 1;
    if (!isdigit((unsigned char)*p)) {
        return 0;
    }
    while (isdigit((unsigned char)*p)) {
        line = line * 10 + (*p - '0');
        p += 1;
    }
    if (*p == ':') {
        const char* probe = p + 1;
        if (isdigit((unsigned char)*probe)) {
            p = probe;
            while (isdigit((unsigned char)*p)) {
                column = column * 10 + (*p - '0');
                p += 1;
            }
        }
    }
    if (strncmp(p, ": error: ", 9) != 0) {
        return 0;
    }
    msg = p + 9;
    *out_path = error;
    *out_path_len = (size_t)(path_end - error);
    *out_line = line;
    *out_column = column;
    *out_message = msg;
    return 1;
}

static void print_compiler_error(const char* path, int line, int column, const char* error) {
    if (error && strstr(error, ": error: ")) {
        const char* parsed_path = 0;
        const char* parsed_message = 0;
        size_t parsed_path_len = 0;
        int parsed_line = 0;
        int parsed_column = 0;
        if (parse_embedded_diagnostic(error, &parsed_path, &parsed_path_len, &parsed_line, &parsed_column, &parsed_message)) {
            char* path_copy = (char*)malloc(parsed_path_len + 1);
            if (!path_copy) {
                fprintf(stderr, "%s\n", error);
                return;
            }
            memcpy(path_copy, parsed_path, parsed_path_len);
            path_copy[parsed_path_len] = '\0';
            fprintf(stderr, "%s:%d", path_copy, parsed_line);
            if (parsed_column > 0) {
                fprintf(stderr, ":%d", parsed_column);
            }
            fprintf(stderr, ": error: %s\n", parsed_message);
            print_source_excerpt(path_copy, parsed_line, parsed_column);
            free(path_copy);
            return;
        }
        fprintf(stderr, "%s\n", error);
        return;
    }
    if (!path || !*path) {
        fprintf(stderr, "error: %s\n", error ? error : "unknown error");
        return;
    }
    if (line > 0) {
        if (column > 0) {
            fprintf(stderr, "%s:%d:%d: error: %s\n", path, line, column, error ? error : "unknown error");
        } else {
            fprintf(stderr, "%s:%d: error: %s\n", path, line, error ? error : "unknown error");
        }
        print_source_excerpt(path, line, column);
        return;
    }
    fprintf(stderr, "%s: error: %s\n", path, error ? error : "unknown error");
}

static int path_is_directory(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

static int path_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static char* basename_dup(const char* path) {
    const char* end = path + strlen(path);
    const char* base = path;
    size_t len = 0;
    char* out = 0;
    while (end > path && end[-1] == '/') {
        end -= 1;
    }
    while (base < end && base[0] != '\0') {
        const char* probe = end;
        while (probe > base) {
            if (probe[-1] == '/') {
                base = probe;
                break;
            }
            probe -= 1;
        }
        break;
    }
    len = (size_t)(end - base);
    out = (char*)malloc(len + 1);
    if (!out) {
        return 0;
    }
    memcpy(out, base, len);
    out[len] = '\0';
    return out;
}

static char* trim_ascii(char* text) {
    char* end = 0;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        text += 1;
    }
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        end -= 1;
    }
    *end = '\0';
    return text;
}

static int is_ident_name_text(const char* text) {
    const unsigned char* p = (const unsigned char*)text;
    if (!text || !*text) {
        return 0;
    }
    if (!(isalpha(*p) || *p == '_')) {
        return 0;
    }
    p += 1;
    while (*p) {
        if (!(isalnum(*p) || *p == '_')) {
            return 0;
        }
        p += 1;
    }
    return 1;
}

static char* dirname_dup(const char* path);

static char* parent_dir_dup(const char* path) {
    return dirname_dup(path);
}

static char* find_enclosing_package_dir(const char* from_path) {
    char* dir = dirname_dup(from_path);
    if (!dir) {
        return 0;
    }
    for (;;) {
        char* manifest = dup_join3(dir, "/", "package.ini");
        if (!manifest) {
            free(dir);
            return 0;
        }
        if (path_exists(manifest)) {
            free(manifest);
            return dir;
        }
        free(manifest);
        {
            char* parent = parent_dir_dup(dir);
            if (!parent || strcmp(parent, dir) == 0) {
                free(parent);
                free(dir);
                return 0;
            }
            free(dir);
            dir = parent;
        }
    }
}

static char* package_dependency_dir_path(const char* package_dir, const char* dep_name) {
    char* manifest_path = 0;
    char* manifest_text = 0;
    int in_dependencies_section = 0;
    char* result = 0;
    manifest_path = dup_join3(package_dir, "/", "package.ini");
    if (!manifest_path) {
        return 0;
    }
    manifest_text = read_file(manifest_path);
    free(manifest_path);
    if (!manifest_text) {
        return 0;
    }
    {
        char* cursor = manifest_text;
        while (*cursor != '\0') {
            char* line = cursor;
            char* eq = 0;
            while (*cursor != '\0' && *cursor != '\n') {
                cursor += 1;
            }
            if (*cursor == '\n') {
                *cursor = '\0';
                cursor += 1;
            }
            line = trim_ascii(line);
            if (*line == '\0' || *line == ';' || *line == '#') {
                continue;
            }
            if (*line == '[') {
                size_t len = strlen(line);
                in_dependencies_section = (len >= 2 && line[len - 1] == ']' && strcmp(line, "[dependencies]") == 0);
                continue;
            }
            if (!in_dependencies_section) {
                continue;
            }
            eq = strchr(line, '=');
            if (!eq) {
                continue;
            }
            *eq = '\0';
            {
                char* key = trim_ascii(line);
                char* value = trim_ascii(eq + 1);
                if (strcmp(key, dep_name) == 0 && *value != '\0') {
                    result = dup_join3(package_dir, "/", value);
                    break;
                }
            }
        }
    }
    free(manifest_text);
    return result;
}

static int load_package_root_path(const char* input_path, char** out_path, const char** error) {
    char* package_dir = 0;
    char* package_name = 0;
    char* root_name = 0;
    char* manifest_path = 0;
    char* manifest_text = 0;
    int in_package_section = 0;
    if (!path_is_directory(input_path)) {
        *out_path = dup_text(input_path);
        return *out_path != 0;
    }
    package_dir = dup_text(input_path);
    package_name = basename_dup(input_path);
    if (!package_dir || !package_name) {
        *error = "out of memory";
        goto fail;
    }
    if (!is_ident_name_text(package_name)) {
        *error = "package name must follow identifier rules";
        goto fail;
    }
    root_name = dup_join3(package_name, "", ".jiang");
    if (!root_name) {
        *error = "out of memory";
        goto fail;
    }
    manifest_path = dup_join3(input_path, "/", "package.ini");
    if (!manifest_path) {
        *error = "out of memory";
        goto fail;
    }
    manifest_text = read_file(manifest_path);
    if (manifest_text) {
        char* cursor = manifest_text;
        while (*cursor != '\0') {
            char* line = cursor;
            char* eq = 0;
            while (*cursor != '\0' && *cursor != '\n') {
                cursor += 1;
            }
            if (*cursor == '\n') {
                *cursor = '\0';
                cursor += 1;
            }
            line = trim_ascii(line);
            if (*line == '\0' || *line == ';' || *line == '#') {
                continue;
            }
            if (*line == '[') {
                size_t len = strlen(line);
                in_package_section = (len >= 2 && line[len - 1] == ']' && strncmp(line, "[package]", len) == 0);
                continue;
            }
            if (!in_package_section) {
                continue;
            }
            eq = strchr(line, '=');
            if (!eq) {
                continue;
            }
            *eq = '\0';
            {
                char* key = trim_ascii(line);
                char* value = trim_ascii(eq + 1);
                if (strcmp(key, "name") == 0 && *value != '\0') {
                    if (!is_ident_name_text(value)) {
                        *error = "package name must follow identifier rules";
                        goto fail;
                    }
                    free(package_name);
                    package_name = dup_text(value);
                    if (!package_name) {
                        *error = "out of memory";
                        goto fail;
                    }
                    free(root_name);
                    root_name = dup_join3(package_name, "", ".jiang");
                    if (!root_name) {
                        *error = "out of memory";
                        goto fail;
                    }
                } else if (strcmp(key, "root") == 0 && *value != '\0') {
                    free(root_name);
                    root_name = dup_text(value);
                    if (!root_name) {
                        *error = "out of memory";
                        goto fail;
                    }
                }
            }
        }
    }
    *out_path = dup_join3(package_dir, "/", root_name);
    if (!*out_path) {
        *error = "out of memory";
        goto fail;
    }
    free(package_dir);
    free(package_name);
    free(root_name);
    free(manifest_path);
    free(manifest_text);
    return 1;
fail:
    free(package_dir);
    free(package_name);
    free(root_name);
    free(manifest_path);
    free(manifest_text);
    return 0;
}

static void usage(const char* argv0) {
    fprintf(stderr, "usage: %s --emit-llvm <file-or-package-dir>\n", argv0);
}

static const AstFunction* find_ast_public_function(const AstProgram* program, const char* name);
static const AstGlobal* find_ast_public_global(const AstProgram* program, const char* name);
static const AstStructDecl* find_ast_public_struct(const AstProgram* program, const char* name);
static const AstEnumDecl* find_ast_public_enum(const AstProgram* program, const char* name);
static const AstUnionDecl* find_ast_public_union(const AstProgram* program, const char* name);
static const AstConceptDecl* find_ast_concept(const AstProgram* program, const char* name);
static const AstConceptDecl* find_ast_public_concept(const AstProgram* program, const char* name);
static int exported_concept_name_allowed(const AstProgram* source, int hide_private, const char* concept_name);

static int program_has_public_type(const AstProgram* program, const char* name) {
    return find_ast_public_concept(program, name) ||
           find_ast_public_struct(program, name) ||
           find_ast_public_enum(program, name) ||
           find_ast_public_union(program, name);
}

static int program_has_public_value(const AstProgram* program, const char* name) {
    return find_ast_public_function(program, name) || find_ast_public_global(program, name);
}

static int program_has_any_type(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->concepts.count; ++i) {
        if (strcmp(program->concepts.items[i].name, name) == 0) return 1;
    }
    for (i = 0; i < program->structs.count; ++i) {
        if (strcmp(program->structs.items[i].name, name) == 0) return 1;
    }
    for (i = 0; i < program->enums.count; ++i) {
        if (strcmp(program->enums.items[i].name, name) == 0) return 1;
    }
    for (i = 0; i < program->unions.count; ++i) {
        if (strcmp(program->unions.items[i].name, name) == 0) return 1;
    }
    return 0;
}

static int program_has_any_value(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->functions.count; ++i) {
        if (strcmp(program->functions.items[i].name, name) == 0) return 1;
    }
    for (i = 0; i < program->globals.count; ++i) {
        if (strcmp(program->globals.items[i].name, name) == 0) return 1;
    }
    return 0;
}

static int program_type_is_public(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->concepts.count; ++i) {
        if (strcmp(program->concepts.items[i].name, name) == 0) return program->concepts.items[i].public_flag;
    }
    for (i = 0; i < program->structs.count; ++i) {
        if (strcmp(program->structs.items[i].name, name) == 0) return program->structs.items[i].public_flag;
    }
    for (i = 0; i < program->enums.count; ++i) {
        if (strcmp(program->enums.items[i].name, name) == 0) return program->enums.items[i].public_flag;
    }
    for (i = 0; i < program->unions.count; ++i) {
        if (strcmp(program->unions.items[i].name, name) == 0) return program->unions.items[i].public_flag;
    }
    return 0;
}

static int program_value_is_public(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->functions.count; ++i) {
        if (strcmp(program->functions.items[i].name, name) == 0) return program->functions.items[i].public_flag;
    }
    for (i = 0; i < program->globals.count; ++i) {
        if (strcmp(program->globals.items[i].name, name) == 0) return program->globals.items[i].public_flag;
    }
    return 0;
}

static char* remap_value_name(const AstProgram* program, const char* prefix, int hide_private, const char* name) {
    if (!name) {
        return 0;
    }
    if (prefix && program_has_any_value(program, name)) {
        if (program_value_is_public(program, name)) {
            return dup_join3(prefix, ".", name);
        }
        if (hide_private) {
            return dup_join3(prefix, ".#", name);
        }
        return dup_text(name);
    }
    if (hide_private && program_has_any_value(program, name) && !program_value_is_public(program, name)) {
        return dup_join3("#", "", name);
    }
    return dup_text(name);
}

static char* remap_exported_name(const AstProgram* program, const char* prefix, int hide_private, const char* name) {
    if (!name) {
        return 0;
    }
    if (program_has_any_value(program, name)) {
        return remap_value_name(program, prefix, hide_private, name);
    }
    if (prefix && program_has_any_type(program, name)) {
        if (program_type_is_public(program, name)) {
            return dup_join3(prefix, ".", name);
        }
        if (hide_private) {
            return dup_join3(prefix, ".#", name);
        }
        return dup_text(name);
    }
    if (hide_private && program_has_any_type(program, name) && !program_type_is_public(program, name)) {
        return dup_join3("#", "", name);
    }
    return dup_text(name);
}

static char* remap_imported_type_decl_name(const AstProgram* program, const char* prefix, int hide_private, const char* name) {
    if (!name) {
        return 0;
    }
    if (prefix && program_has_any_type(program, name)) {
        if (program_type_is_public(program, name)) {
            return dup_join3(prefix, ".", name);
        }
        if (hide_private) {
            return dup_join3(prefix, ".#", name);
        }
        return dup_text(name);
    }
    if (hide_private && program_has_any_type(program, name) && !program_type_is_public(program, name)) {
        return dup_join3("#", "", name);
    }
    return dup_text(name);
}

static char* remap_type_name(const AstProgram* program, const char* prefix, int hide_private, const char* name) {
    if (!name) {
        return 0;
    }
    if (prefix && program_has_any_type(program, name)) {
        if (program_type_is_public(program, name)) {
            return dup_join3(prefix, ".", name);
        }
        if (hide_private) {
            return dup_join3(prefix, ".#", name);
        }
        return dup_text(name);
    }
    if (hide_private && program_has_any_type(program, name) && !program_type_is_public(program, name)) {
        return dup_join3("#", "", name);
    }
    return dup_text(name);
}

static AstType clone_type(const AstProgram* source, const char* prefix, int hide_private, const AstType* type);
static AstExpr* clone_expr(const AstProgram* source, const char* prefix, int hide_private, const AstExpr* expr);
static AstBindingPattern* clone_binding_pattern(const AstProgram* source, const char* prefix, int hide_private, const AstBindingPattern* pattern);
static int clone_block(const AstProgram* source, const char* prefix, int hide_private, AstBlock* out, const AstBlock* block);
static AstType ast_type_copy(const AstType* type);
static int ast_type_is_equal(const AstType* left, const AstType* right);
static AstType canonicalize_ast_type(AstType type);

static AstType canonicalize_ast_type(AstType type) {
    while (type.kind == AST_TYPE_OPTIONAL &&
           type.array_item &&
           type.array_item->kind == AST_TYPE_OPTIONAL) {
        AstType inner = ast_type_copy(type.array_item);
        if (type.mutable_flag) {
            inner.mutable_flag = 1;
        }
        type = inner;
    }
    return type;
}

static AstType clone_type(const AstProgram* source, const char* prefix, int hide_private, const AstType* type) {
    AstType out;
    int i = 0;
    memset(&out, 0, sizeof(out));
    out.kind = type->kind;
    out.mutable_flag = type->mutable_flag;
    out.array_length = type->array_length;
    if (type->named_name) {
        out.named_name = remap_type_name(source, prefix, hide_private, type->named_name);
    }
    for (i = 0; i < type->type_args.count; ++i) {
        type_list_push(&out.type_args, clone_type(source, prefix, hide_private, &type->type_args.items[i]));
    }
    if (type->array_item) {
        out.array_item = (AstType*)malloc(sizeof(AstType));
        *out.array_item = clone_type(source, prefix, hide_private, type->array_item);
    }
    if (type->error_type) {
        out.error_type = (AstType*)malloc(sizeof(AstType));
        *out.error_type = clone_type(source, prefix, hide_private, type->error_type);
    }
    for (i = 0; i < type->tuple_items.count; ++i) {
        AstType item = clone_type(source, prefix, hide_private, &type->tuple_items.items[i]);
        type_list_push(&out.tuple_items, item);
    }
    return canonicalize_ast_type(out);
}

static AstBindingPattern* clone_binding_pattern(const AstProgram* source, const char* prefix, int hide_private, const AstBindingPattern* pattern) {
    AstBindingPattern* out = (AstBindingPattern*)calloc(1, sizeof(AstBindingPattern));
    int i = 0;
    out->kind = pattern->kind;
    out->line = pattern->line;
    out->type = clone_type(source, prefix, hide_private, &pattern->type);
    if (pattern->name) {
        out->name = dup_text(pattern->name);
    }
    for (i = 0; i < pattern->items.count; ++i) {
        binding_pattern_list_push(&out->items, clone_binding_pattern(source, prefix, hide_private, pattern->items.items[i]));
    }
    return out;
}

static AstExpr* clone_expr(const AstProgram* source, const char* prefix, int hide_private, const AstExpr* expr) {
    AstExpr* out = 0;
    int i = 0;
    if (!expr) {
        return 0;
    }
    out = (AstExpr*)calloc(1, sizeof(AstExpr));
    out->kind = expr->kind;
    out->line = expr->line;
    switch (expr->kind) {
        case AST_EXPR_INT:
            out->as.int_value = expr->as.int_value;
            break;
        case AST_EXPR_FLOAT:
            out->as.float_value = expr->as.float_value;
            break;
        case AST_EXPR_CHAR:
            out->as.char_value = expr->as.char_value;
            break;
        case AST_EXPR_BOOL:
            out->as.bool_value = expr->as.bool_value;
            break;
        case AST_EXPR_NULL:
            break;
        case AST_EXPR_IMPLICIT:
            out->as.implicit.target_is_type = expr->as.implicit.target_is_type;
            if (expr->as.implicit.target_is_type) {
                out->as.implicit.type_target = clone_type(source, prefix, hide_private, &expr->as.implicit.type_target);
            } else {
                out->as.implicit.value_target = clone_expr(source, prefix, hide_private, expr->as.implicit.value_target);
            }
            out->as.implicit.member = dup_text(expr->as.implicit.member);
            out->as.implicit.has_type_arg = expr->as.implicit.has_type_arg;
            if (expr->as.implicit.has_type_arg) {
                out->as.implicit.type_arg = clone_type(source, prefix, hide_private, &expr->as.implicit.type_arg);
            }
            for (i = 0; i < expr->as.implicit.args.count; ++i) {
                expr_list_push(&out->as.implicit.args, clone_expr(source, prefix, hide_private, expr->as.implicit.args.items[i]));
            }
            break;
        case AST_EXPR_SIZE_OF:
            out->as.size_of_type = clone_type(source, prefix, hide_private, &expr->as.size_of_type);
            break;
        case AST_EXPR_STRING:
            out->as.string_lit.text = dup_text(expr->as.string_lit.text);
            out->as.string_lit.length = expr->as.string_lit.length;
            break;
        case AST_EXPR_NAME:
            out->as.name = remap_exported_name(source, prefix, hide_private, expr->as.name);
            break;
        case AST_EXPR_ADDR:
        case AST_EXPR_DEREF:
        case AST_EXPR_NEW:
        case AST_EXPR_FREE:
        case AST_EXPR_BIT_NOT:
            out->as.unary.value = clone_expr(source, prefix, hide_private, expr->as.unary.value);
            break;
        case AST_EXPR_BINARY:
            out->as.binary.op = expr->as.binary.op;
            out->as.binary.left = clone_expr(source, prefix, hide_private, expr->as.binary.left);
            out->as.binary.right = clone_expr(source, prefix, hide_private, expr->as.binary.right);
            break;
        case AST_EXPR_COALESCE:
            out->as.coalesce.left = clone_expr(source, prefix, hide_private, expr->as.coalesce.left);
            out->as.coalesce.right = clone_expr(source, prefix, hide_private, expr->as.coalesce.right);
            break;
        case AST_EXPR_CATCH_FALLBACK:
            out->as.catch_fallback.left = clone_expr(source, prefix, hide_private, expr->as.catch_fallback.left);
            out->as.catch_fallback.fallback = clone_expr(source, prefix, hide_private, expr->as.catch_fallback.fallback);
            break;
        case AST_EXPR_CATCH_HANDLER:
            out->as.catch_handler.left = clone_expr(source, prefix, hide_private, expr->as.catch_handler.left);
            out->as.catch_handler.binding_name = dup_text(expr->as.catch_handler.binding_name);
            out->as.catch_handler.handler = clone_expr(source, prefix, hide_private, expr->as.catch_handler.handler);
            break;
        case AST_EXPR_BLOCK:
            out->as.block_expr.body = (AstBlock*)calloc(1, sizeof(AstBlock));
            if (out->as.block_expr.body) {
                clone_block(source, prefix, hide_private, out->as.block_expr.body, expr->as.block_expr.body);
            }
            out->as.block_expr.value = clone_expr(source, prefix, hide_private, expr->as.block_expr.value);
            break;
        case AST_EXPR_IF:
            out->as.if_expr.cond = clone_expr(source, prefix, hide_private, expr->as.if_expr.cond);
            out->as.if_expr.then_expr = clone_expr(source, prefix, hide_private, expr->as.if_expr.then_expr);
            out->as.if_expr.else_expr = clone_expr(source, prefix, hide_private, expr->as.if_expr.else_expr);
            break;
        case AST_EXPR_SWITCH:
            out->as.switch_expr.value = clone_expr(source, prefix, hide_private, expr->as.switch_expr.value);
            for (i = 0; i < expr->as.switch_expr.cases.count; ++i) {
                AstSwitchExprCase item;
                memset(&item, 0, sizeof(item));
                item.pattern = clone_expr(source, prefix, hide_private, expr->as.switch_expr.cases.items[i].pattern);
                item.value = clone_expr(source, prefix, hide_private, expr->as.switch_expr.cases.items[i].value);
                item.is_else = expr->as.switch_expr.cases.items[i].is_else;
                switch_expr_case_list_push(&out->as.switch_expr.cases, item);
            }
            break;
        case AST_EXPR_TRY:
            out->as.try_expr.value = clone_expr(source, prefix, hide_private, expr->as.try_expr.value);
            for (i = 0; i < expr->as.try_expr.catches.count; ++i) {
                AstExprTryCatch item;
                memset(&item, 0, sizeof(item));
                item.error_type = clone_type(source, prefix, hide_private, &expr->as.try_expr.catches.items[i].error_type);
                item.binding_name = dup_text(expr->as.try_expr.catches.items[i].binding_name);
                item.value = clone_expr(source, prefix, hide_private, expr->as.try_expr.catches.items[i].value);
                item.line = expr->as.try_expr.catches.items[i].line;
                expr_try_catch_list_push(&out->as.try_expr.catches, item);
            }
            break;
        case AST_EXPR_COALESCE_CONTROL:
            out->as.coalesce_control.left = clone_expr(source, prefix, hide_private, expr->as.coalesce_control.left);
            out->as.coalesce_control.control = expr->as.coalesce_control.control;
            out->as.coalesce_control.return_expr = clone_expr(source, prefix, hide_private, expr->as.coalesce_control.return_expr);
            break;
        case AST_EXPR_TERNARY:
            out->as.ternary.cond = clone_expr(source, prefix, hide_private, expr->as.ternary.cond);
            out->as.ternary.then_expr = clone_expr(source, prefix, hide_private, expr->as.ternary.then_expr);
            out->as.ternary.else_expr = clone_expr(source, prefix, hide_private, expr->as.ternary.else_expr);
            break;
        case AST_EXPR_CALL:
            out->as.call.callee = remap_exported_name(source, prefix, hide_private, expr->as.call.callee);
            for (i = 0; i < expr->as.call.type_args.count; ++i) {
                type_list_push(&out->as.call.type_args, clone_type(source, prefix, hide_private, &expr->as.call.type_args.items[i]));
            }
            for (i = 0; i < expr->as.call.args.count; ++i) {
                AstStructFieldInit arg;
                memset(&arg, 0, sizeof(arg));
                arg.name = expr->as.call.args.items[i].name ? dup_text(expr->as.call.args.items[i].name) : 0;
                arg.value = clone_expr(source, prefix, hide_private, expr->as.call.args.items[i].value);
                arg.line = expr->as.call.args.items[i].line;
                struct_field_init_list_push(&out->as.call.args, arg);
            }
            break;
        case AST_EXPR_VARIANT:
            if (expr->as.variant.union_name) {
                out->as.variant.union_name = remap_type_name(source, prefix, hide_private, expr->as.variant.union_name);
            }
            out->as.variant.variant_name = dup_text(expr->as.variant.variant_name);
            out->as.variant.pattern_flag = expr->as.variant.pattern_flag;
            out->as.variant.payload = clone_expr(source, prefix, hide_private, expr->as.variant.payload);
            for (i = 0; i < expr->as.variant.bindings.count; ++i) {
                binding_pattern_list_push(&out->as.variant.bindings, clone_binding_pattern(source, prefix, hide_private, expr->as.variant.bindings.items[i]));
            }
            break;
        case AST_EXPR_FIELD:
        case AST_EXPR_OPTIONAL_FIELD:
            out->as.field.base = clone_expr(source, prefix, hide_private, expr->as.field.base);
            out->as.field.name = dup_text(expr->as.field.name);
            break;
        case AST_EXPR_STRUCT:
            out->as.struct_lit.type_name = expr->as.struct_lit.type_name
                ? remap_exported_name(source, prefix, hide_private, expr->as.struct_lit.type_name)
                : 0;
            for (i = 0; i < expr->as.struct_lit.type_args.count; ++i) {
                type_list_push(&out->as.struct_lit.type_args, clone_type(source, prefix, hide_private, &expr->as.struct_lit.type_args.items[i]));
            }
            for (i = 0; i < expr->as.struct_lit.fields.count; ++i) {
                AstStructFieldInit init;
                memset(&init, 0, sizeof(init));
                init.name = dup_text(expr->as.struct_lit.fields.items[i].name);
                init.value = clone_expr(source, prefix, hide_private, expr->as.struct_lit.fields.items[i].value);
                init.line = expr->as.struct_lit.fields.items[i].line;
                struct_field_init_list_push(&out->as.struct_lit.fields, init);
            }
            break;
        case AST_EXPR_TUPLE:
            for (i = 0; i < expr->as.tuple.items.count; ++i) {
                expr_list_push(&out->as.tuple.items, clone_expr(source, prefix, hide_private, expr->as.tuple.items.items[i]));
            }
            break;
        case AST_EXPR_ARRAY:
            for (i = 0; i < expr->as.array.items.count; ++i) {
                expr_list_push(&out->as.array.items, clone_expr(source, prefix, hide_private, expr->as.array.items.items[i]));
            }
            break;
        case AST_EXPR_INDEX:
        case AST_EXPR_OPTIONAL_INDEX:
            out->as.index.base = clone_expr(source, prefix, hide_private, expr->as.index.base);
            out->as.index.index = clone_expr(source, prefix, hide_private, expr->as.index.index);
            break;
        case AST_EXPR_SLICE_LENGTH:
            out->as.slice_length.base = clone_expr(source, prefix, hide_private, expr->as.slice_length.base);
            break;
    }
    return out;
}

static AstStmt* clone_stmt(const AstProgram* source, const char* prefix, int hide_private, const AstStmt* stmt) {
    AstStmt* out = (AstStmt*)calloc(1, sizeof(AstStmt));
    int i = 0;
    out->kind = stmt->kind;
    out->line = stmt->line;
    switch (stmt->kind) {
        case AST_STMT_RETURN:
            out->as.ret.expr = clone_expr(source, prefix, hide_private, stmt->as.ret.expr);
            break;
        case AST_STMT_VAR_DECL:
            out->as.var_decl.type = clone_type(source, prefix, hide_private, &stmt->as.var_decl.type);
            out->as.var_decl.name = dup_text(stmt->as.var_decl.name);
            out->as.var_decl.init = clone_expr(source, prefix, hide_private, stmt->as.var_decl.init);
            break;
        case AST_STMT_GROUP:
            clone_block(source, prefix, hide_private, &out->as.group_stmt, &stmt->as.group_stmt);
            break;
        case AST_STMT_ASSIGN:
            out->as.assign.target = clone_expr(source, prefix, hide_private, stmt->as.assign.target);
            out->as.assign.value = clone_expr(source, prefix, hide_private, stmt->as.assign.value);
            break;
        case AST_STMT_IF:
            out->as.if_stmt.cond = clone_expr(source, prefix, hide_private, stmt->as.if_stmt.cond);
            out->as.if_stmt.has_else = stmt->as.if_stmt.has_else;
            clone_block(source, prefix, hide_private, &out->as.if_stmt.then_block, &stmt->as.if_stmt.then_block);
            clone_block(source, prefix, hide_private, &out->as.if_stmt.else_block, &stmt->as.if_stmt.else_block);
            break;
        case AST_STMT_SWITCH:
            out->as.switch_stmt.value = clone_expr(source, prefix, hide_private, stmt->as.switch_stmt.value);
            for (i = 0; i < stmt->as.switch_stmt.cases.count; ++i) {
                AstSwitchCase item;
                memset(&item, 0, sizeof(item));
                item.pattern = clone_expr(source, prefix, hide_private, stmt->as.switch_stmt.cases.items[i].pattern);
                item.is_else = stmt->as.switch_stmt.cases.items[i].is_else;
                item.binding_name = stmt->as.switch_stmt.cases.items[i].binding_name
                    ? dup_text(stmt->as.switch_stmt.cases.items[i].binding_name)
                    : 0;
                item.result_case_kind = stmt->as.switch_stmt.cases.items[i].result_case_kind;
                clone_block(source, prefix, hide_private, &item.body, &stmt->as.switch_stmt.cases.items[i].body);
                switch_case_list_push(&out->as.switch_stmt.cases, item);
            }
            break;
        case AST_STMT_TRY:
            clone_block(source, prefix, hide_private, &out->as.try_stmt.try_body, &stmt->as.try_stmt.try_body);
            for (i = 0; i < stmt->as.try_stmt.catches.count; ++i) {
                AstTryCatch item;
                memset(&item, 0, sizeof(item));
                item.error_type = clone_type(source, prefix, hide_private, &stmt->as.try_stmt.catches.items[i].error_type);
                item.binding_name = dup_text(stmt->as.try_stmt.catches.items[i].binding_name);
                item.line = stmt->as.try_stmt.catches.items[i].line;
                clone_block(source, prefix, hide_private, &item.body, &stmt->as.try_stmt.catches.items[i].body);
                try_catch_list_push(&out->as.try_stmt.catches, item);
            }
            break;
        case AST_STMT_WHILE:
            out->as.while_stmt.cond = clone_expr(source, prefix, hide_private, stmt->as.while_stmt.cond);
            clone_block(source, prefix, hide_private, &out->as.while_stmt.body, &stmt->as.while_stmt.body);
            break;
        case AST_STMT_FOR_RANGE:
            out->as.for_range.type = clone_type(source, prefix, hide_private, &stmt->as.for_range.type);
            out->as.for_range.name = dup_text(stmt->as.for_range.name);
            out->as.for_range.start = clone_expr(source, prefix, hide_private, stmt->as.for_range.start);
            out->as.for_range.end = clone_expr(source, prefix, hide_private, stmt->as.for_range.end);
            clone_block(source, prefix, hide_private, &out->as.for_range.body, &stmt->as.for_range.body);
            break;
        case AST_STMT_FOR_EACH:
            out->as.for_each.pattern = clone_binding_pattern(source, prefix, hide_private, stmt->as.for_each.pattern);
            out->as.for_each.iterable = clone_expr(source, prefix, hide_private, stmt->as.for_each.iterable);
            out->as.for_each.indexed_flag = stmt->as.for_each.indexed_flag;
            clone_block(source, prefix, hide_private, &out->as.for_each.body, &stmt->as.for_each.body);
            break;
        case AST_STMT_BREAK:
        case AST_STMT_CONTINUE:
            break;
        case AST_STMT_DEFER:
            clone_block(source, prefix, hide_private, &out->as.defer_stmt.body, &stmt->as.defer_stmt.body);
            break;
        case AST_STMT_EXPR:
            out->as.expr_stmt.expr = clone_expr(source, prefix, hide_private, stmt->as.expr_stmt.expr);
            break;
        case AST_STMT_EXPR_CATCH:
            out->as.expr_catch_stmt.expr = clone_expr(source, prefix, hide_private, stmt->as.expr_catch_stmt.expr);
            out->as.expr_catch_stmt.binding_name = dup_text(stmt->as.expr_catch_stmt.binding_name);
            clone_block(source, prefix, hide_private, &out->as.expr_catch_stmt.body, &stmt->as.expr_catch_stmt.body);
            break;
        case AST_STMT_THROW:
            out->as.throw_stmt.expr = clone_expr(source, prefix, hide_private, stmt->as.throw_stmt.expr);
            break;
        case AST_STMT_DESTRUCTURE:
            for (i = 0; i < stmt->as.destructure.bindings.count; ++i) {
                AstParam binding;
                memset(&binding, 0, sizeof(binding));
                binding.type = clone_type(source, prefix, hide_private, &stmt->as.destructure.bindings.items[i].type);
                binding.label = stmt->as.destructure.bindings.items[i].label ? dup_text(stmt->as.destructure.bindings.items[i].label) : 0;
                binding.name = dup_text(stmt->as.destructure.bindings.items[i].name);
                binding.default_value = clone_expr(source, prefix, hide_private, stmt->as.destructure.bindings.items[i].default_value);
                binding.line = stmt->as.destructure.bindings.items[i].line;
                param_list_push(&out->as.destructure.bindings, binding);
            }
            out->as.destructure.init = clone_expr(source, prefix, hide_private, stmt->as.destructure.init);
            break;
    }
    return out;
}

static int clone_block(const AstProgram* source, const char* prefix, int hide_private, AstBlock* out, const AstBlock* block) {
    int i = 0;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < block->stmts.count; ++i) {
        stmt_list_push(&out->stmts, clone_stmt(source, prefix, hide_private, block->stmts.items[i]));
    }
    return 1;
}

static AstWhereConstraint clone_where_constraint(const AstProgram* source,
                                                 const char* prefix,
                                                 int hide_private,
                                                 const AstWhereConstraint* constraint) {
    AstWhereConstraint out;
    memset(&out, 0, sizeof(out));
    out.param_name = dup_text(constraint->param_name);
    out.concept_name = constraint->concept_name ? dup_text(constraint->concept_name) : 0;
    out.equal_type = clone_type(source, prefix, hide_private, &constraint->equal_type);
    out.kind = constraint->kind;
    out.line = constraint->line;
    return out;
}

static void clone_where_constraint_list(const AstProgram* source,
                                        const char* prefix,
                                        int hide_private,
                                        AstWhereConstraintList* out,
                                        const AstWhereConstraintList* in) {
    int i = 0;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < in->count; ++i) {
        where_constraint_list_push(out, clone_where_constraint(source, prefix, hide_private, &in->items[i]));
    }
}

static void clone_name_list(AstNameList* out, const AstNameList* in) {
    int i = 0;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < in->count; ++i) {
        name_list_push(out, dup_text(in->items[i]));
    }
}

static AstFunction clone_function(const AstProgram* source, const char* prefix, int hide_private, const AstFunction* fn, int public_flag) {
    AstFunction out;
    int i = 0;
    memset(&out, 0, sizeof(out));
    out.return_type = clone_type(source, prefix, hide_private, &fn->return_type);
    out.name = fn->method_flag ? dup_text(fn->name) : remap_exported_name(source, prefix, hide_private, fn->name);
    for (i = 0; i < fn->type_params.count; ++i) {
        name_list_push(&out.type_params, dup_text(fn->type_params.items[i]));
    }
    clone_where_constraint_list(source, prefix, hide_private, &out.where_constraints, &fn->where_constraints);
    out.public_flag = public_flag;
    out.struct_init_flag = fn->struct_init_flag;
    out.extern_flag = fn->extern_flag;
    out.method_flag = fn->method_flag;
    out.static_method_flag = fn->static_method_flag;
    out.line = fn->line;
    if (fn->owner_type_name) {
        out.owner_type_name = remap_exported_name(source, prefix, hide_private, fn->owner_type_name);
    }
    for (i = 0; i < fn->params.count; ++i) {
        AstParam param;
        memset(&param, 0, sizeof(param));
        param.type = clone_type(source, prefix, hide_private, &fn->params.items[i].type);
        param.label = fn->params.items[i].label ? dup_text(fn->params.items[i].label) : 0;
        param.name = dup_text(fn->params.items[i].name);
        param.default_value = clone_expr(source, prefix, hide_private, fn->params.items[i].default_value);
        param.line = fn->params.items[i].line;
        param_list_push(&out.params, param);
    }
    clone_block(source, prefix, hide_private, &out.body, &fn->body);
    return out;
}

static AstFunction clone_function_as(const AstProgram* source, const AstFunction* fn, const char* new_name, int public_flag) {
    AstFunction out = clone_function(source, 0, 0, fn, public_flag);
    out.name = dup_text(new_name);
    return out;
}

static AstGlobal clone_global(const AstProgram* source, const char* prefix, int hide_private, const AstGlobal* global, int public_flag) {
    AstGlobal out;
    memset(&out, 0, sizeof(out));
    out.type = clone_type(source, prefix, hide_private, &global->type);
    out.name = remap_exported_name(source, prefix, hide_private, global->name);
    out.init = clone_expr(source, prefix, hide_private, global->init);
    out.public_flag = public_flag;
    out.extern_flag = global->extern_flag;
    out.line = global->line;
    return out;
}

static AstGlobal clone_global_as(const AstProgram* source, const AstGlobal* global, const char* new_name, int public_flag) {
    AstGlobal out = clone_global(source, 0, 0, global, public_flag);
    out.name = dup_text(new_name);
    return out;
}

static AstStructDecl clone_struct_decl(const AstProgram* source, const char* prefix, int hide_private, const AstStructDecl* decl, int public_flag) {
    AstStructDecl out;
    int i = 0;
    memset(&out, 0, sizeof(out));
    out.name = remap_imported_type_decl_name(source, prefix, hide_private, decl->name);
    for (i = 0; i < decl->type_params.count; ++i) {
        name_list_push(&out.type_params, dup_text(decl->type_params.items[i]));
    }
    for (i = 0; i < decl->concept_names.count; ++i) {
        if (exported_concept_name_allowed(source, hide_private, decl->concept_names.items[i])) {
            name_list_push(&out.concept_names, remap_type_name(source, prefix, hide_private, decl->concept_names.items[i]));
        }
    }
    clone_where_constraint_list(source, prefix, hide_private, &out.where_constraints, &decl->where_constraints);
    out.public_flag = public_flag;
    out.record_flag = decl->record_flag;
    out.has_deinit = decl->has_deinit;
    out.deinit_line = decl->deinit_line;
    out.line = decl->line;
    for (i = 0; i < decl->fields.count; ++i) {
        AstStructField field;
        memset(&field, 0, sizeof(field));
        field.type = clone_type(source, prefix, hide_private, &decl->fields.items[i].type);
        field.name = dup_text(decl->fields.items[i].name);
        field.default_value = clone_expr(source, prefix, hide_private, decl->fields.items[i].default_value);
        field.line = decl->fields.items[i].line;
        struct_field_list_push(&out.fields, field);
    }
    for (i = 0; i < decl->assoc_type_bindings.count; ++i) {
        AstAssocTypeBinding binding;
        memset(&binding, 0, sizeof(binding));
        clone_name_list(&binding.context_concept_names, &decl->assoc_type_bindings.items[i].context_concept_names);
        binding.concept_name = decl->assoc_type_bindings.items[i].concept_name
            ? dup_text(decl->assoc_type_bindings.items[i].concept_name)
            : 0;
        binding.name = dup_text(decl->assoc_type_bindings.items[i].name);
        binding.value = clone_type(source, prefix, hide_private, &decl->assoc_type_bindings.items[i].value);
        binding.line = decl->assoc_type_bindings.items[i].line;
        assoc_type_binding_list_push(&out.assoc_type_bindings, binding);
    }
    for (i = 0; i < decl->init_overloads.count; ++i) {
        AstStructInitDecl init_decl;
        int j = 0;
        memset(&init_decl, 0, sizeof(init_decl));
        init_decl.line = decl->init_overloads.items[i].line;
        for (j = 0; j < decl->init_overloads.items[i].params.count; ++j) {
            AstParam param;
            memset(&param, 0, sizeof(param));
            param.type = clone_type(source, prefix, hide_private, &decl->init_overloads.items[i].params.items[j].type);
            param.label = decl->init_overloads.items[i].params.items[j].label ? dup_text(decl->init_overloads.items[i].params.items[j].label) : 0;
            param.name = dup_text(decl->init_overloads.items[i].params.items[j].name);
            param.default_value = clone_expr(source, prefix, hide_private, decl->init_overloads.items[i].params.items[j].default_value);
            param.line = decl->init_overloads.items[i].params.items[j].line;
            param_list_push(&init_decl.params, param);
        }
        clone_block(source, prefix, hide_private, &init_decl.body, &decl->init_overloads.items[i].body);
        struct_init_decl_list_push(&out.init_overloads, init_decl);
    }
    clone_block(source, prefix, hide_private, &out.deinit_body, &decl->deinit_body);
    return out;
}

static AstStructDecl clone_struct_decl_as(const AstProgram* source, const AstStructDecl* decl, const char* new_name, int public_flag) {
    AstStructDecl out = clone_struct_decl(source, 0, 0, decl, public_flag);
    out.name = dup_text(new_name);
    return out;
}

static AstEnumDecl clone_enum_decl(const AstProgram* source, const char* prefix, int hide_private, const AstEnumDecl* decl, int public_flag) {
    AstEnumDecl out;
    int i = 0;
    (void)source;
    memset(&out, 0, sizeof(out));
    out.name = remap_imported_type_decl_name(source, prefix, hide_private, decl->name);
    for (i = 0; i < decl->concept_names.count; ++i) {
        if (exported_concept_name_allowed(source, hide_private, decl->concept_names.items[i])) {
            name_list_push(&out.concept_names, remap_type_name(source, prefix, hide_private, decl->concept_names.items[i]));
        }
    }
    for (i = 0; i < decl->assoc_type_bindings.count; ++i) {
        AstAssocTypeBinding binding;
        memset(&binding, 0, sizeof(binding));
        clone_name_list(&binding.context_concept_names, &decl->assoc_type_bindings.items[i].context_concept_names);
        binding.concept_name = decl->assoc_type_bindings.items[i].concept_name
            ? dup_text(decl->assoc_type_bindings.items[i].concept_name)
            : 0;
        binding.name = dup_text(decl->assoc_type_bindings.items[i].name);
        binding.value = clone_type(source, prefix, hide_private, &decl->assoc_type_bindings.items[i].value);
        binding.line = decl->assoc_type_bindings.items[i].line;
        assoc_type_binding_list_push(&out.assoc_type_bindings, binding);
    }
    out.public_flag = public_flag;
    out.line = decl->line;
    for (i = 0; i < decl->members.count; ++i) {
        AstEnumMember member = decl->members.items[i];
        member.name = dup_text(decl->members.items[i].name);
        enum_member_list_push(&out.members, member);
    }
    return out;
}

static AstEnumDecl clone_enum_decl_as(const AstProgram* source, const AstEnumDecl* decl, const char* new_name, int public_flag) {
    AstEnumDecl out = clone_enum_decl(source, 0, 0, decl, public_flag);
    out.name = dup_text(new_name);
    return out;
}

static AstUnionDecl clone_union_decl(const AstProgram* source, const char* prefix, int hide_private, const AstUnionDecl* decl, int public_flag) {
    AstUnionDecl out;
    int i = 0;
    memset(&out, 0, sizeof(out));
    out.tag_name = decl->tag_name ? remap_exported_name(source, prefix, hide_private, decl->tag_name) : 0;
    out.name = remap_imported_type_decl_name(source, prefix, hide_private, decl->name);
    for (i = 0; i < decl->type_params.count; ++i) {
        name_list_push(&out.type_params, dup_text(decl->type_params.items[i]));
    }
    clone_where_constraint_list(source, prefix, hide_private, &out.where_constraints, &decl->where_constraints);
    for (i = 0; i < decl->concept_names.count; ++i) {
        if (exported_concept_name_allowed(source, hide_private, decl->concept_names.items[i])) {
            name_list_push(&out.concept_names, remap_type_name(source, prefix, hide_private, decl->concept_names.items[i]));
        }
    }
    for (i = 0; i < decl->assoc_type_bindings.count; ++i) {
        AstAssocTypeBinding binding;
        memset(&binding, 0, sizeof(binding));
        clone_name_list(&binding.context_concept_names, &decl->assoc_type_bindings.items[i].context_concept_names);
        binding.concept_name = decl->assoc_type_bindings.items[i].concept_name
            ? dup_text(decl->assoc_type_bindings.items[i].concept_name)
            : 0;
        binding.name = dup_text(decl->assoc_type_bindings.items[i].name);
        binding.value = clone_type(source, prefix, hide_private, &decl->assoc_type_bindings.items[i].value);
        binding.line = decl->assoc_type_bindings.items[i].line;
        assoc_type_binding_list_push(&out.assoc_type_bindings, binding);
    }
    out.public_flag = public_flag;
    out.line = decl->line;
    for (i = 0; i < decl->variants.count; ++i) {
        AstUnionVariant variant;
        memset(&variant, 0, sizeof(variant));
        variant.type = clone_type(source, prefix, hide_private, &decl->variants.items[i].type);
        variant.name = dup_text(decl->variants.items[i].name);
        variant.line = decl->variants.items[i].line;
        union_variant_list_push(&out.variants, variant);
    }
    return out;
}

static AstUnionDecl clone_union_decl_as(const AstProgram* source, const AstUnionDecl* decl, const char* new_name, int public_flag) {
    AstUnionDecl out = clone_union_decl(source, 0, 0, decl, public_flag);
    out.name = dup_text(new_name);
    return out;
}

static AstConceptDecl clone_concept_decl(const AstProgram* source, const char* prefix, int hide_private, const AstConceptDecl* decl, int public_flag) {
    AstConceptDecl out;
    int i = 0;
    memset(&out, 0, sizeof(out));
    out.name = remap_imported_type_decl_name(source, prefix, hide_private, decl->name);
    clone_where_constraint_list(source, prefix, hide_private, &out.where_constraints, &decl->where_constraints);
    for (i = 0; i < decl->concept_names.count; ++i) {
        name_list_push(&out.concept_names, remap_type_name(source, prefix, hide_private, decl->concept_names.items[i]));
    }
    for (i = 0; i < decl->assoc_types.count; ++i) {
        AstAssocTypeDecl assoc_type;
        memset(&assoc_type, 0, sizeof(assoc_type));
        assoc_type.name = dup_text(decl->assoc_types.items[i].name);
        assoc_type.line = decl->assoc_types.items[i].line;
        clone_where_constraint_list(source, prefix, hide_private, &assoc_type.where_constraints, &decl->assoc_types.items[i].where_constraints);
        assoc_type_decl_list_push(&out.assoc_types, assoc_type);
    }
    for (i = 0; i < decl->methods.count; ++i) {
        AstConceptMethod method;
        int j = 0;
        memset(&method, 0, sizeof(method));
        method.return_type = clone_type(source, prefix, hide_private, &decl->methods.items[i].return_type);
        method.name = dup_text(decl->methods.items[i].name);
        method.line = decl->methods.items[i].line;
        clone_where_constraint_list(source, prefix, hide_private, &method.where_constraints, &decl->methods.items[i].where_constraints);
        for (j = 0; j < decl->methods.items[i].params.count; ++j) {
            AstParam param;
            memset(&param, 0, sizeof(param));
            param.type = clone_type(source, prefix, hide_private, &decl->methods.items[i].params.items[j].type);
            param.label = decl->methods.items[i].params.items[j].label ? dup_text(decl->methods.items[i].params.items[j].label) : 0;
            param.name = dup_text(decl->methods.items[i].params.items[j].name);
            param.default_value = clone_expr(source, prefix, hide_private, decl->methods.items[i].params.items[j].default_value);
            param.line = decl->methods.items[i].params.items[j].line;
            param_list_push(&method.params, param);
        }
        concept_method_list_push(&out.methods, method);
    }
    out.public_flag = public_flag;
    out.line = decl->line;
    return out;
}

static const AstFunction* find_ast_function(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->functions.count; ++i) {
        if (strcmp(program->functions.items[i].name, name) == 0) return &program->functions.items[i];
    }
    return 0;
}

static const AstFunction* find_ast_public_function(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->functions.count; ++i) {
        if (program->functions.items[i].public_flag && strcmp(program->functions.items[i].name, name) == 0) {
            return &program->functions.items[i];
        }
    }
    return 0;
}

static const AstGlobal* find_ast_global(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->globals.count; ++i) {
        if (strcmp(program->globals.items[i].name, name) == 0) return &program->globals.items[i];
    }
    return 0;
}

static const AstGlobal* find_ast_public_global(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->globals.count; ++i) {
        if (program->globals.items[i].public_flag && strcmp(program->globals.items[i].name, name) == 0) {
            return &program->globals.items[i];
        }
    }
    return 0;
}

static const AstStructDecl* find_ast_struct(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->structs.count; ++i) {
        if (strcmp(program->structs.items[i].name, name) == 0) return &program->structs.items[i];
    }
    return 0;
}

static const AstStructDecl* find_ast_public_struct(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->structs.count; ++i) {
        if (program->structs.items[i].public_flag && strcmp(program->structs.items[i].name, name) == 0) {
            return &program->structs.items[i];
        }
    }
    return 0;
}

static const AstEnumDecl* find_ast_enum(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->enums.count; ++i) {
        if (strcmp(program->enums.items[i].name, name) == 0) return &program->enums.items[i];
    }
    return 0;
}

static const AstEnumDecl* find_ast_public_enum(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->enums.count; ++i) {
        if (program->enums.items[i].public_flag && strcmp(program->enums.items[i].name, name) == 0) {
            return &program->enums.items[i];
        }
    }
    return 0;
}

static const AstUnionDecl* find_ast_union(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->unions.count; ++i) {
        if (strcmp(program->unions.items[i].name, name) == 0) return &program->unions.items[i];
    }
    return 0;
}

static const AstUnionDecl* find_ast_public_union(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->unions.count; ++i) {
        if (program->unions.items[i].public_flag && strcmp(program->unions.items[i].name, name) == 0) {
            return &program->unions.items[i];
        }
    }
    return 0;
}

typedef enum AstNominalKind {
    AST_NOMINAL_NONE = 0,
    AST_NOMINAL_BUILTIN,
    AST_NOMINAL_STRUCT,
    AST_NOMINAL_ENUM,
    AST_NOMINAL_UNION,
} AstNominalKind;

typedef enum AstBuiltinNominalKind {
    AST_BUILTIN_NOMINAL_NONE = 0,
    AST_BUILTIN_NOMINAL_INT,
    AST_BUILTIN_NOMINAL_I8,
    AST_BUILTIN_NOMINAL_I16,
    AST_BUILTIN_NOMINAL_I32,
    AST_BUILTIN_NOMINAL_I64,
    AST_BUILTIN_NOMINAL_U8,
    AST_BUILTIN_NOMINAL_U16,
    AST_BUILTIN_NOMINAL_U32,
    AST_BUILTIN_NOMINAL_U64,
    AST_BUILTIN_NOMINAL_F16,
    AST_BUILTIN_NOMINAL_F32,
    AST_BUILTIN_NOMINAL_F64,
    AST_BUILTIN_NOMINAL_FLOAT,
    AST_BUILTIN_NOMINAL_DOUBLE,
    AST_BUILTIN_NOMINAL_CHARACTER,
    AST_BUILTIN_NOMINAL_UINT8,
    AST_BUILTIN_NOMINAL_BOOL,
    AST_BUILTIN_NOMINAL_VOID,
} AstBuiltinNominalKind;

typedef struct AstBuiltinNominalDecl {
    AstBuiltinNominalKind kind;
    const char* name;
} AstBuiltinNominalDecl;

typedef struct AstNominalDeclRef {
    AstNominalKind kind;
    const char* name;
    const void* decl;
} AstNominalDeclRef;

typedef enum AstTypeQueryKind {
    AST_TYPE_QUERY_NONE = 0,
    AST_TYPE_QUERY_NOMINAL,
    AST_TYPE_QUERY_TUPLE,
    AST_TYPE_QUERY_SLICE,
    AST_TYPE_QUERY_POINTER,
    AST_TYPE_QUERY_MANY_POINTER,
    AST_TYPE_QUERY_ARRAY,
    AST_TYPE_QUERY_OPTIONAL,
    AST_TYPE_QUERY_INFER,
} AstTypeQueryKind;

typedef struct AstTypeQueryRef {
    AstTypeQueryKind kind;
    const AstType* source;
    AstNominalDeclRef nominal;
    const AstType* item_type;
    int array_length;
} AstTypeQueryRef;

static const AstBuiltinNominalDecl AST_BUILTIN_INT_DECL = { AST_BUILTIN_NOMINAL_INT, "Int" };
static const AstBuiltinNominalDecl AST_BUILTIN_I8_DECL = { AST_BUILTIN_NOMINAL_I8, "Int8" };
static const AstBuiltinNominalDecl AST_BUILTIN_I16_DECL = { AST_BUILTIN_NOMINAL_I16, "Int16" };
static const AstBuiltinNominalDecl AST_BUILTIN_I32_DECL = { AST_BUILTIN_NOMINAL_I32, "Int32" };
static const AstBuiltinNominalDecl AST_BUILTIN_I64_DECL = { AST_BUILTIN_NOMINAL_I64, "Int64" };
static const AstBuiltinNominalDecl AST_BUILTIN_U8_DECL = { AST_BUILTIN_NOMINAL_U8, "UInt8" };
static const AstBuiltinNominalDecl AST_BUILTIN_U16_DECL = { AST_BUILTIN_NOMINAL_U16, "UInt16" };
static const AstBuiltinNominalDecl AST_BUILTIN_U32_DECL = { AST_BUILTIN_NOMINAL_U32, "UInt32" };
static const AstBuiltinNominalDecl AST_BUILTIN_U64_DECL = { AST_BUILTIN_NOMINAL_U64, "UInt64" };
static const AstBuiltinNominalDecl AST_BUILTIN_F16_DECL = { AST_BUILTIN_NOMINAL_F16, "Float16" };
static const AstBuiltinNominalDecl AST_BUILTIN_F32_DECL = { AST_BUILTIN_NOMINAL_F32, "Float32" };
static const AstBuiltinNominalDecl AST_BUILTIN_F64_DECL = { AST_BUILTIN_NOMINAL_F64, "Float64" };
static const AstBuiltinNominalDecl AST_BUILTIN_FLOAT_DECL = { AST_BUILTIN_NOMINAL_FLOAT, "Float" };
static const AstBuiltinNominalDecl AST_BUILTIN_DOUBLE_DECL = { AST_BUILTIN_NOMINAL_DOUBLE, "Double" };
static const AstBuiltinNominalDecl AST_BUILTIN_CHARACTER_DECL = { AST_BUILTIN_NOMINAL_CHARACTER, "Char" };
static const AstBuiltinNominalDecl AST_BUILTIN_UINT8_DECL = { AST_BUILTIN_NOMINAL_UINT8, "UInt8" };
static const AstBuiltinNominalDecl AST_BUILTIN_BOOL_DECL = { AST_BUILTIN_NOMINAL_BOOL, "Bool" };
static const AstBuiltinNominalDecl AST_BUILTIN_VOID_DECL = { AST_BUILTIN_NOMINAL_VOID, "Void" };

static const AstConceptDecl* find_ast_concept(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->concepts.count; ++i) {
        if (strcmp(program->concepts.items[i].name, name) == 0) return &program->concepts.items[i];
    }
    return 0;
}

static const AstConceptDecl* find_ast_public_concept(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->concepts.count; ++i) {
        if (program->concepts.items[i].public_flag && strcmp(program->concepts.items[i].name, name) == 0) {
            return &program->concepts.items[i];
        }
    }
    return 0;
}

static const AstBuiltinNominalDecl* find_ast_builtin_nominal(const char* name) {
    if (strcmp(name, "Int") == 0) return &AST_BUILTIN_INT_DECL;
    if (strcmp(name, "Int8") == 0) return &AST_BUILTIN_I8_DECL;
    if (strcmp(name, "Int16") == 0) return &AST_BUILTIN_I16_DECL;
    if (strcmp(name, "Int32") == 0) return &AST_BUILTIN_I32_DECL;
    if (strcmp(name, "Int64") == 0) return &AST_BUILTIN_I64_DECL;
    if (strcmp(name, "UInt16") == 0) return &AST_BUILTIN_U16_DECL;
    if (strcmp(name, "UInt32") == 0) return &AST_BUILTIN_U32_DECL;
    if (strcmp(name, "UInt64") == 0) return &AST_BUILTIN_U64_DECL;
    if (strcmp(name, "Float16") == 0) return &AST_BUILTIN_F16_DECL;
    if (strcmp(name, "Float32") == 0) return &AST_BUILTIN_F32_DECL;
    if (strcmp(name, "Float64") == 0) return &AST_BUILTIN_F64_DECL;
    if (strcmp(name, "Float") == 0) return &AST_BUILTIN_FLOAT_DECL;
    if (strcmp(name, "Double") == 0) return &AST_BUILTIN_DOUBLE_DECL;
    if (strcmp(name, "Char") == 0) return &AST_BUILTIN_CHARACTER_DECL;
    if (strcmp(name, "UInt8") == 0) return &AST_BUILTIN_UINT8_DECL;
    if (strcmp(name, "Bool") == 0) return &AST_BUILTIN_BOOL_DECL;
    if (strcmp(name, "Void") == 0) return &AST_BUILTIN_VOID_DECL;
    return 0;
}

static AstNominalKind find_ast_nominal_kind(const AstProgram* program, const char* name) {
    if (find_ast_builtin_nominal(name)) return AST_NOMINAL_BUILTIN;
    if (find_ast_struct(program, name)) return AST_NOMINAL_STRUCT;
    if (find_ast_enum(program, name)) return AST_NOMINAL_ENUM;
    if (find_ast_union(program, name)) return AST_NOMINAL_UNION;
    return AST_NOMINAL_NONE;
}

static AstNominalDeclRef find_ast_nominal_decl(const AstProgram* program, const char* name) {
    const AstBuiltinNominalDecl* builtin_decl = find_ast_builtin_nominal(name);
    const AstStructDecl* struct_decl = find_ast_struct(program, name);
    const AstEnumDecl* enum_decl = 0;
    const AstUnionDecl* union_decl = 0;
    AstNominalDeclRef out;
    memset(&out, 0, sizeof(out));
    if (builtin_decl) {
        out.kind = AST_NOMINAL_BUILTIN;
        out.name = builtin_decl->name;
        out.decl = builtin_decl;
        return out;
    }
    if (struct_decl) {
        out.kind = AST_NOMINAL_STRUCT;
        out.name = struct_decl->name;
        out.decl = struct_decl;
        return out;
    }
    enum_decl = find_ast_enum(program, name);
    if (enum_decl) {
        out.kind = AST_NOMINAL_ENUM;
        out.name = enum_decl->name;
        out.decl = enum_decl;
        return out;
    }
    union_decl = find_ast_union(program, name);
    if (union_decl) {
        out.kind = AST_NOMINAL_UNION;
        out.name = union_decl->name;
        out.decl = union_decl;
        return out;
    }
    return out;
}

static AstNominalDeclRef find_ast_public_nominal_decl(const AstProgram* program, const char* name) {
    const AstStructDecl* struct_decl = find_ast_public_struct(program, name);
    const AstEnumDecl* enum_decl = 0;
    const AstUnionDecl* union_decl = 0;
    AstNominalDeclRef out;
    memset(&out, 0, sizeof(out));
    if (find_ast_builtin_nominal(name)) {
        out = find_ast_nominal_decl(program, name);
        return out;
    }
    if (struct_decl) {
        out.kind = AST_NOMINAL_STRUCT;
        out.name = struct_decl->name;
        out.decl = struct_decl;
        return out;
    }
    enum_decl = find_ast_public_enum(program, name);
    if (enum_decl) {
        out.kind = AST_NOMINAL_ENUM;
        out.name = enum_decl->name;
        out.decl = enum_decl;
        return out;
    }
    union_decl = find_ast_public_union(program, name);
    if (union_decl) {
        out.kind = AST_NOMINAL_UNION;
        out.name = union_decl->name;
        out.decl = union_decl;
        return out;
    }
    return out;
}

static int ast_nominal_has_type_params(AstNominalDeclRef nominal) {
    switch (nominal.kind) {
        case AST_NOMINAL_BUILTIN:
            return 0;
        case AST_NOMINAL_STRUCT:
            return ((const AstStructDecl*)nominal.decl)->type_params.count > 0;
        case AST_NOMINAL_UNION:
            return ((const AstUnionDecl*)nominal.decl)->type_params.count > 0;
        case AST_NOMINAL_ENUM:
        case AST_NOMINAL_NONE:
        default:
            return 0;
    }
}

static AstTypeQueryRef describe_ast_type(const AstProgram* program, const AstType* type) {
    AstTypeQueryRef out;
    memset(&out, 0, sizeof(out));
    if (!type) {
        return out;
    }
    out.source = type;
    out.array_length = type->array_length;
    switch (type->kind) {
        case AST_TYPE_INT:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, "Int");
            return out;
        case AST_TYPE_I8:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, "Int8");
            return out;
        case AST_TYPE_I16:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, "Int16");
            return out;
        case AST_TYPE_I32:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, "Int32");
            return out;
        case AST_TYPE_I64:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, "Int64");
            return out;
        case AST_TYPE_U8:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, "UInt8");
            return out;
        case AST_TYPE_U16:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, "UInt16");
            return out;
        case AST_TYPE_U32:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, "UInt32");
            return out;
        case AST_TYPE_U64:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, "UInt64");
            return out;
        case AST_TYPE_F16:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, "Float16");
            return out;
        case AST_TYPE_F32:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, "Float32");
            return out;
        case AST_TYPE_F64:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, "Float64");
            return out;
        case AST_TYPE_FLOAT:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, "Float");
            return out;
        case AST_TYPE_DOUBLE:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, "Double");
            return out;
        case AST_TYPE_CHARACTER:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, "Char");
            return out;
        case AST_TYPE_UINT8:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, "UInt8");
            return out;
        case AST_TYPE_BOOL:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, "Bool");
            return out;
        case AST_TYPE_VOID:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, "Void");
            return out;
        case AST_TYPE_NAMED:
            out.kind = AST_TYPE_QUERY_NOMINAL;
            out.nominal = find_ast_nominal_decl(program, type->named_name);
            return out;
        case AST_TYPE_TUPLE:
            out.kind = AST_TYPE_QUERY_TUPLE;
            return out;
        case AST_TYPE_SLICE:
            out.kind = AST_TYPE_QUERY_SLICE;
            out.item_type = type->array_item;
            return out;
        case AST_TYPE_REFERENCE:
            out.kind = AST_TYPE_QUERY_POINTER;
            out.item_type = type->array_item;
            return out;
        case AST_TYPE_POINTER:
            out.kind = AST_TYPE_QUERY_POINTER;
            out.item_type = type->array_item;
            return out;
        case AST_TYPE_MANY_POINTER:
            out.kind = AST_TYPE_QUERY_MANY_POINTER;
            out.item_type = type->array_item;
            return out;
        case AST_TYPE_ARRAY:
            out.kind = AST_TYPE_QUERY_ARRAY;
            out.item_type = type->array_item;
            return out;
        case AST_TYPE_OPTIONAL:
            out.kind = AST_TYPE_QUERY_OPTIONAL;
            out.item_type = type->array_item;
            return out;
        case AST_TYPE_ERRORABLE:
            return out;
        case AST_TYPE_INFER:
            out.kind = AST_TYPE_QUERY_INFER;
            return out;
        default:
            return out;
    }
}

static int validate_module_decls(const AstProgram* own_program, const char** error) {
    int i = 0;
    int j = 0;
    for (i = 0; i < own_program->imports.count; ++i) {
        char* alias = own_program->imports.items[i].alias_name
                          ? dup_text(own_program->imports.items[i].alias_name)
                          : 0;
        if (!alias) {
            const char* path = own_program->imports.items[i].path;
            const char* slash = strrchr(path, '/');
            const char* base = slash ? slash + 1 : path;
            const char* dot = strrchr(base, '.');
            size_t len = dot && dot > base ? (size_t)(dot - base) : strlen(base);
            alias = (char*)malloc(len + 1);
            if (!alias) {
                *error = "out of memory";
                return 0;
            }
            memcpy(alias, base, len);
            alias[len] = '\0';
        }
        for (j = i + 1; j < own_program->imports.count; ++j) {
            char* other_alias = own_program->imports.items[j].alias_name
                                    ? dup_text(own_program->imports.items[j].alias_name)
                                    : 0;
            int same = 0;
            if (!other_alias) {
                const char* path = own_program->imports.items[j].path;
                const char* slash = strrchr(path, '/');
                const char* base = slash ? slash + 1 : path;
                const char* dot = strrchr(base, '.');
                size_t len = dot && dot > base ? (size_t)(dot - base) : strlen(base);
                other_alias = (char*)malloc(len + 1);
                if (!other_alias) {
                    free(alias);
                    *error = "out of memory";
                    return 0;
                }
                memcpy(other_alias, base, len);
                other_alias[len] = '\0';
            }
            same = strcmp(alias, other_alias) == 0;
            free(other_alias);
            if (same) {
                free(alias);
                *error = "duplicate import alias";
                return 0;
            }
        }
        if (program_has_any_type(own_program, alias) || program_has_any_value(own_program, alias)) {
            free(alias);
            *error = "import alias conflicts with top-level declaration";
            return 0;
        }
        for (j = 0; j < own_program->aliases.count; ++j) {
            if (strcmp(alias, own_program->aliases.items[j].name) == 0) {
                free(alias);
                *error = "import alias conflicts with alias";
                return 0;
            }
        }
        free(alias);
    }
    return 1;
}

static int apply_aliases(AstProgram* dest, const AstProgram* own_program, const char** error) {
    int i = 0;
    for (i = 0; i < own_program->aliases.count; ++i) {
        const AstAliasDecl* alias = &own_program->aliases.items[i];
        const AstFunction* fn = find_ast_function(dest, alias->target_name);
        const AstGlobal* global = find_ast_global(dest, alias->target_name);
        AstNominalDeclRef nominal = find_ast_nominal_decl(dest, alias->target_name);
        if (fn) {
            function_list_push(&dest->functions, clone_function_as(dest, fn, alias->name, alias->public_flag));
            continue;
        }
        if (global) {
            global_list_push(&dest->globals, clone_global_as(dest, global, alias->name, alias->public_flag));
            continue;
        }
        switch (nominal.kind) {
            case AST_NOMINAL_STRUCT:
                struct_list_push(&dest->structs, clone_struct_decl_as(dest, (const AstStructDecl*)nominal.decl, alias->name, alias->public_flag));
                continue;
            case AST_NOMINAL_ENUM:
                enum_list_push(&dest->enums, clone_enum_decl_as(dest, (const AstEnumDecl*)nominal.decl, alias->name, alias->public_flag));
                continue;
            case AST_NOMINAL_UNION:
                union_list_push(&dest->unions, clone_union_decl_as(dest, (const AstUnionDecl*)nominal.decl, alias->name, alias->public_flag));
                continue;
            case AST_NOMINAL_NONE:
            default:
                break;
        }
        *error = "unknown alias target";
        return 0;
    }
    return 1;
}

static int apply_public_aliases(AstProgram* dest, const AstProgram* lookup_program, const AstProgram* own_program, const char** error) {
    int i = 0;
    for (i = 0; i < own_program->aliases.count; ++i) {
        const AstAliasDecl* alias = &own_program->aliases.items[i];
        const AstFunction* fn = 0;
        const AstGlobal* global = 0;
        AstNominalDeclRef nominal;
        memset(&nominal, 0, sizeof(nominal));
        if (!alias->public_flag) {
            continue;
        }
        fn = find_ast_public_function(lookup_program, alias->target_name);
        global = find_ast_public_global(lookup_program, alias->target_name);
        nominal = find_ast_public_nominal_decl(lookup_program, alias->target_name);
        if (fn) {
            function_list_push(&dest->functions, clone_function_as(dest, fn, alias->name, 1));
            continue;
        }
        if (global) {
            global_list_push(&dest->globals, clone_global_as(dest, global, alias->name, 1));
            continue;
        }
        switch (nominal.kind) {
            case AST_NOMINAL_STRUCT:
                struct_list_push(&dest->structs, clone_struct_decl_as(dest, (const AstStructDecl*)nominal.decl, alias->name, 1));
                continue;
            case AST_NOMINAL_ENUM:
                enum_list_push(&dest->enums, clone_enum_decl_as(dest, (const AstEnumDecl*)nominal.decl, alias->name, 1));
                continue;
            case AST_NOMINAL_UNION:
                union_list_push(&dest->unions, clone_union_decl_as(dest, (const AstUnionDecl*)nominal.decl, alias->name, 1));
                continue;
            case AST_NOMINAL_NONE:
            case AST_NOMINAL_BUILTIN:
            default:
                break;
        }
        *error = "unknown alias target";
        return 0;
    }
    return 1;
}

static int concept_decl_has_method(const AstConceptDecl* concept, const char* method_name) {
    int i = 0;
    if (!concept) {
        return 0;
    }
    for (i = 0; i < concept->methods.count; ++i) {
        if (strcmp(concept->methods.items[i].name, method_name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int ast_name_list_contains(const AstNameList* list, const char* name);

static int concept_or_parent_has_method(const AstProgram* program,
                                        const AstConceptDecl* concept,
                                        const char* method_name,
                                        AstNameList* seen_concepts) {
    int i = 0;
    if (!concept) {
        return 0;
    }
    if (ast_name_list_contains(seen_concepts, concept->name)) {
        return 0;
    }
    name_list_push(seen_concepts, concept->name);
    if (concept_decl_has_method(concept, method_name)) {
        return 1;
    }
    for (i = 0; i < concept->concept_names.count; ++i) {
        const AstConceptDecl* parent = find_ast_concept(program, concept->concept_names.items[i]);
        if (concept_or_parent_has_method(program, parent, method_name, seen_concepts)) {
            return 1;
        }
    }
    return 0;
}

static int ast_name_list_contains(const AstNameList* list, const char* name) {
    int i = 0;
    for (i = 0; i < list->count; ++i) {
        if (strcmp(list->items[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

typedef struct ConceptMethodRef {
    const AstConceptMethod* method;
    const AstConceptDecl* owner;
} ConceptMethodRef;

typedef struct ConceptMethodRefList {
    ConceptMethodRef* items;
    int count;
    int capacity;
} ConceptMethodRefList;

#define concept_method_ref_list_push(list, item) VEC_PUSH((list), (item))

typedef struct ConceptAssocTypeRef {
    const char* name;
    const AstConceptDecl* declared_by;
    AstWhereConstraintList where_constraints;
} ConceptAssocTypeRef;

typedef struct ConceptAssocTypeRefList {
    ConceptAssocTypeRef* items;
    int count;
    int capacity;
} ConceptAssocTypeRefList;

#define concept_assoc_type_ref_list_push(list, item) VEC_PUSH((list), (item))

typedef struct ResolvedAssocTypeBinding {
    const char* name;
    const AstType* value;
    int line;
} ResolvedAssocTypeBinding;

typedef struct ResolvedAssocTypeBindingList {
    ResolvedAssocTypeBinding* items;
    int count;
    int capacity;
} ResolvedAssocTypeBindingList;

#define resolved_assoc_type_binding_list_push(list, item) VEC_PUSH((list), (item))

static int concept_method_signature_equal(const AstConceptMethod* a, const AstConceptMethod* b) {
    int i = 0;
    if (!ast_type_is_equal(&a->return_type, &b->return_type)) {
        return 0;
    }
    if (a->params.count != b->params.count) {
        return 0;
    }
    for (i = 0; i < a->params.count; ++i) {
        if (!ast_type_is_equal(&a->params.items[i].type, &b->params.items[i].type)) {
            return 0;
        }
        if (!!a->params.items[i].label != !!b->params.items[i].label) {
            return 0;
        }
        if (a->params.items[i].label && strcmp(a->params.items[i].label, b->params.items[i].label) != 0) {
            return 0;
        }
    }
    return 1;
}

static int where_constraint_equal(const AstWhereConstraint* a, const AstWhereConstraint* b) {
    if (a->kind != b->kind) {
        return 0;
    }
    if (strcmp(a->param_name, b->param_name) != 0) {
        return 0;
    }
    if (a->kind == AST_WHERE_CONCEPT) {
        return strcmp(a->concept_name, b->concept_name) == 0;
    }
    return ast_type_is_equal(&a->equal_type, &b->equal_type);
}

static void add_unique_where_constraint(AstWhereConstraintList* list, const AstWhereConstraint* item) {
    AstWhereConstraint copy;
    int i = 0;
    for (i = 0; i < list->count; ++i) {
        if (where_constraint_equal(&list->items[i], item)) {
            return;
        }
    }
    memset(&copy, 0, sizeof(copy));
    copy.param_name = dup_text(item->param_name);
    copy.concept_name = item->concept_name ? dup_text(item->concept_name) : 0;
    copy.equal_type = ast_type_copy(&item->equal_type);
    copy.kind = item->kind;
    copy.line = item->line;
    where_constraint_list_push(list, copy);
}

static ConceptMethodRef* find_concept_method_ref(ConceptMethodRefList* list, const char* name) {
    int i = 0;
    for (i = 0; i < list->count; ++i) {
        if (strcmp(list->items[i].method->name, name) == 0) {
            return &list->items[i];
        }
    }
    return 0;
}

static ConceptAssocTypeRef* find_concept_assoc_type_ref(ConceptAssocTypeRefList* list, const char* name) {
    int i = 0;
    for (i = 0; i < list->count; ++i) {
        if (strcmp(list->items[i].name, name) == 0) {
            return &list->items[i];
        }
    }
    return 0;
}

static const AstAssocTypeBinding* find_nominal_assoc_type_binding(AstNominalDeclRef nominal, int index) {
    switch (nominal.kind) {
        case AST_NOMINAL_STRUCT:
            return &((const AstStructDecl*)nominal.decl)->assoc_type_bindings.items[index];
        case AST_NOMINAL_ENUM:
            return &((const AstEnumDecl*)nominal.decl)->assoc_type_bindings.items[index];
        case AST_NOMINAL_UNION:
            return &((const AstUnionDecl*)nominal.decl)->assoc_type_bindings.items[index];
        default:
            return 0;
    }
}

static int nominal_assoc_type_binding_count(AstNominalDeclRef nominal) {
    switch (nominal.kind) {
        case AST_NOMINAL_STRUCT:
            return ((const AstStructDecl*)nominal.decl)->assoc_type_bindings.count;
        case AST_NOMINAL_ENUM:
            return ((const AstEnumDecl*)nominal.decl)->assoc_type_bindings.count;
        case AST_NOMINAL_UNION:
            return ((const AstUnionDecl*)nominal.decl)->assoc_type_bindings.count;
        default:
            return 0;
    }
}

static const AstType* lookup_resolved_assoc_type_binding(const ResolvedAssocTypeBindingList* bindings,
                                                         const char* assoc_name) {
    int i = 0;
    for (i = 0; i < bindings->count; ++i) {
        if (strcmp(bindings->items[i].name, assoc_name) == 0) {
            return bindings->items[i].value;
        }
    }
    return 0;
}

static int collect_concept_method_names(const AstProgram* program,
                                        const AstConceptDecl* concept,
                                        AstNameList* seen_concepts,
                                        AstNameList* active_concepts,
                                        ConceptMethodRefList* methods,
                                        const char** error,
                                        char** name_detail) {
    int i = 0;
    if (!concept) {
        return 1;
    }
    if (ast_name_list_contains(active_concepts, concept->name)) {
        *error = "cyclic trait inheritance";
        *name_detail = concept->name;
        return 0;
    }
    if (ast_name_list_contains(seen_concepts, concept->name)) {
        return 1;
    }
    name_list_push(active_concepts, concept->name);
    for (i = 0; i < concept->concept_names.count; ++i) {
        const AstConceptDecl* parent = find_ast_concept(program, concept->concept_names.items[i]);
        if (!parent) {
            *error = "unknown parent trait";
            *name_detail = concept->concept_names.items[i];
            return 0;
        }
        if (!collect_concept_method_names(program, parent, seen_concepts, active_concepts, methods, error, name_detail)) {
            return 0;
        }
    }
    name_list_push(seen_concepts, concept->name);
    for (i = 0; i < concept->methods.count; ++i) {
        ConceptMethodRef* existing = find_concept_method_ref(methods, concept->methods.items[i].name);
        if (existing) {
            if (!concept_method_signature_equal(existing->method, &concept->methods.items[i])) {
                *error = "trait requirement conflict";
                *name_detail = concept->methods.items[i].name;
                return 0;
            }
            continue;
        }
        {
            ConceptMethodRef item;
            item.method = &concept->methods.items[i];
            item.owner = concept;
            concept_method_ref_list_push(methods, item);
        }
    }
    active_concepts->count -= 1;
    return 1;
}

static int merge_assoc_where_constraints(ConceptAssocTypeRef* assoc_type,
                                         const AstWhereConstraintList* constraints,
                                         const char** error,
                                         char** name_detail) {
    int i = 0;
    for (i = 0; i < constraints->count; ++i) {
        const AstWhereConstraint* item = &constraints->items[i];
        int j = 0;
        if (strcmp(item->param_name, assoc_type->name) != 0) {
            continue;
        }
        if (item->kind == AST_WHERE_EQUAL) {
            for (j = 0; j < assoc_type->where_constraints.count; ++j) {
                if (assoc_type->where_constraints.items[j].kind == AST_WHERE_EQUAL &&
                    !ast_type_is_equal(&assoc_type->where_constraints.items[j].equal_type, &item->equal_type)) {
                    *error = "associated type conflict";
                    *name_detail = (char*)assoc_type->name;
                    return 0;
                }
            }
        }
        add_unique_where_constraint(&assoc_type->where_constraints, item);
    }
    return 1;
}

static int collect_concept_assoc_types(const AstProgram* program,
                                       const AstConceptDecl* concept,
                                       AstNameList* seen_concepts,
                                       AstNameList* active_concepts,
                                       ConceptAssocTypeRefList* assoc_types,
                                       const char** error,
                                       char** name_detail) {
    int i = 0;
    if (!concept) {
        return 1;
    }
    if (ast_name_list_contains(active_concepts, concept->name)) {
        *error = "cyclic trait inheritance";
        *name_detail = concept->name;
        return 0;
    }
    if (ast_name_list_contains(seen_concepts, concept->name)) {
        return 1;
    }
    name_list_push(active_concepts, concept->name);
    for (i = 0; i < concept->concept_names.count; ++i) {
        const AstConceptDecl* parent = find_ast_concept(program, concept->concept_names.items[i]);
        if (!parent) {
            *error = "unknown parent trait";
            *name_detail = concept->concept_names.items[i];
            return 0;
        }
        if (!collect_concept_assoc_types(program, parent, seen_concepts, active_concepts, assoc_types, error, name_detail)) {
            return 0;
        }
    }
    name_list_push(seen_concepts, concept->name);
    for (i = 0; i < concept->assoc_types.count; ++i) {
        ConceptAssocTypeRef* existing = find_concept_assoc_type_ref(assoc_types, concept->assoc_types.items[i].name);
        if (existing) {
            if (existing->declared_by == concept) {
                *error = "duplicate associated type";
                *name_detail = concept->assoc_types.items[i].name;
                return 0;
            }
            if (!merge_assoc_where_constraints(existing, &concept->assoc_types.items[i].where_constraints, error, name_detail)) {
                return 0;
            }
            continue;
        }
        {
            ConceptAssocTypeRef item;
            memset(&item, 0, sizeof(item));
            item.name = concept->assoc_types.items[i].name;
            item.declared_by = concept;
            if (!merge_assoc_where_constraints(&item, &concept->assoc_types.items[i].where_constraints, error, name_detail)) {
                return 0;
            }
            concept_assoc_type_ref_list_push(assoc_types, item);
        }
    }
    for (i = 0; i < concept->where_constraints.count; ++i) {
        ConceptAssocTypeRef* existing = find_concept_assoc_type_ref(assoc_types, concept->where_constraints.items[i].param_name);
        if (!existing) {
            *error = "unknown associated type in @where";
            *name_detail = concept->where_constraints.items[i].param_name;
            return 0;
        }
        if (!merge_assoc_where_constraints(existing, &concept->where_constraints, error, name_detail)) {
            return 0;
        }
    }
    for (i = 0; i < concept->methods.count; ++i) {
        int j = 0;
        for (j = 0; j < concept->methods.items[i].where_constraints.count; ++j) {
            ConceptAssocTypeRef* existing = find_concept_assoc_type_ref(assoc_types, concept->methods.items[i].where_constraints.items[j].param_name);
            if (!existing) {
                *error = "unknown associated type in @where";
                *name_detail = concept->methods.items[i].where_constraints.items[j].param_name;
                return 0;
            }
        }
    }
    active_concepts->count -= 1;
    return 1;
}

static int ast_type_satisfies_concept(const AstProgram* program, const AstType* type, const char* concept_name);

static int assoc_where_constraints_satisfied(const AstProgram* program,
                                             const AstWhereConstraintList* where_constraints,
                                             const ResolvedAssocTypeBindingList* assoc_bindings) {
    int i = 0;
    for (i = 0; i < where_constraints->count; ++i) {
        const AstType* actual = lookup_resolved_assoc_type_binding(assoc_bindings, where_constraints->items[i].param_name);
        if (!actual) {
            return 0;
        }
        if (where_constraints->items[i].kind == AST_WHERE_CONCEPT) {
            if (!ast_type_satisfies_concept(program, actual, where_constraints->items[i].concept_name)) {
                return 0;
            }
        } else if (!ast_type_is_equal(actual, &where_constraints->items[i].equal_type)) {
            return 0;
        }
    }
    return 1;
}

static int nominal_concepts_have_unique_method_names(const AstProgram* program,
                                                     const AstNameList* concept_names,
                                                     const char** error,
                                                     char** detail_name) {
    AstNameList seen_concepts;
    AstNameList active_concepts;
    ConceptMethodRefList methods;
    int i = 0;
    memset(&seen_concepts, 0, sizeof(seen_concepts));
    memset(&active_concepts, 0, sizeof(active_concepts));
    memset(&methods, 0, sizeof(methods));
    *error = 0;
    *detail_name = 0;
    for (i = 0; i < concept_names->count; ++i) {
        const AstConceptDecl* concept = find_ast_concept(program, concept_names->items[i]);
        if (!concept) {
            *error = "unknown trait on type";
            *detail_name = concept_names->items[i];
            return 0;
        }
        if (!collect_concept_method_names(program, concept, &seen_concepts, &active_concepts, &methods, error, detail_name)) {
            return 0;
        }
    }
    return 1;
}

static int exported_concept_name_allowed(const AstProgram* source, int hide_private, const char* concept_name) {
    const AstConceptDecl* concept = find_ast_concept(source, concept_name);
    if (!hide_private) {
        return 1;
    }
    return !concept || concept->public_flag;
}

static int method_is_exported_via_public_trait(const AstProgram* program, const AstFunction* fn) {
    AstNominalDeclRef owner;
    int i = 0;
    if (!fn || !fn->method_flag || fn->static_method_flag || !fn->owner_type_name) {
        return 0;
    }
    owner = find_ast_nominal_decl(program, fn->owner_type_name);
    switch (owner.kind) {
        case AST_NOMINAL_STRUCT: {
            const AstStructDecl* decl = (const AstStructDecl*)owner.decl;
            for (i = 0; i < decl->concept_names.count; ++i) {
                const AstConceptDecl* concept = find_ast_concept(program, decl->concept_names.items[i]);
                AstNameList seen_concepts;
                memset(&seen_concepts, 0, sizeof(seen_concepts));
                if (concept && concept->public_flag && concept_or_parent_has_method(program, concept, fn->name, &seen_concepts)) {
                    return 1;
                }
            }
            return 0;
        }
        case AST_NOMINAL_ENUM: {
            const AstEnumDecl* decl = (const AstEnumDecl*)owner.decl;
            for (i = 0; i < decl->concept_names.count; ++i) {
                const AstConceptDecl* concept = find_ast_concept(program, decl->concept_names.items[i]);
                AstNameList seen_concepts;
                memset(&seen_concepts, 0, sizeof(seen_concepts));
                if (concept && concept->public_flag && concept_or_parent_has_method(program, concept, fn->name, &seen_concepts)) {
                    return 1;
                }
            }
            return 0;
        }
        case AST_NOMINAL_UNION: {
            const AstUnionDecl* decl = (const AstUnionDecl*)owner.decl;
            for (i = 0; i < decl->concept_names.count; ++i) {
                const AstConceptDecl* concept = find_ast_concept(program, decl->concept_names.items[i]);
                AstNameList seen_concepts;
                memset(&seen_concepts, 0, sizeof(seen_concepts));
                if (concept && concept->public_flag && concept_or_parent_has_method(program, concept, fn->name, &seen_concepts)) {
                    return 1;
                }
            }
            return 0;
        }
        default:
            return 0;
    }
}

static void merge_public_import(AstProgram* dest, const AstProgram* imported, const char* prefix) {
    int i = 0;
    (void)prefix;
    for (i = 0; i < imported->concepts.count; ++i) {
        if (!find_ast_concept(dest, imported->concepts.items[i].name)) {
            concept_list_push(&dest->concepts, clone_concept_decl(imported, prefix, 1, &imported->concepts.items[i], imported->concepts.items[i].public_flag));
        }
    }
    for (i = 0; i < imported->structs.count; ++i) {
        struct_list_push(&dest->structs, clone_struct_decl(imported, prefix, 1, &imported->structs.items[i], imported->structs.items[i].public_flag));
    }
    for (i = 0; i < imported->enums.count; ++i) {
        enum_list_push(&dest->enums, clone_enum_decl(imported, prefix, 1, &imported->enums.items[i], imported->enums.items[i].public_flag));
    }
    for (i = 0; i < imported->unions.count; ++i) {
        union_list_push(&dest->unions, clone_union_decl(imported, prefix, 1, &imported->unions.items[i], imported->unions.items[i].public_flag));
    }
    for (i = 0; i < imported->globals.count; ++i) {
        if (imported->globals.items[i].public_flag) {
            global_list_push(&dest->globals, clone_global(imported, prefix, 1, &imported->globals.items[i], imported->globals.items[i].public_flag));
        }
    }
    for (i = 0; i < imported->functions.count; ++i) {
        int keep = imported->functions.items[i].public_flag;
        int exported_public_flag = imported->functions.items[i].public_flag;
        if (!keep && imported->functions.items[i].method_flag && imported->functions.items[i].owner_type_name) {
            AstNominalDeclRef owner = find_ast_nominal_decl(imported, imported->functions.items[i].owner_type_name);
            int owner_public = 0;
            switch (owner.kind) {
                case AST_NOMINAL_STRUCT:
                    owner_public = ((const AstStructDecl*)owner.decl)->public_flag;
                    break;
                case AST_NOMINAL_ENUM:
                    owner_public = ((const AstEnumDecl*)owner.decl)->public_flag;
                    break;
                case AST_NOMINAL_UNION:
                    owner_public = ((const AstUnionDecl*)owner.decl)->public_flag;
                    break;
                default:
                    break;
            }
            keep = owner_public;
            if (owner_public && method_is_exported_via_public_trait(imported, &imported->functions.items[i])) {
                exported_public_flag = 1;
            }
        }
        if (keep) {
            function_list_push(&dest->functions, clone_function(imported, prefix, 1, &imported->functions.items[i], exported_public_flag));
        }
    }
}

static void append_own_decls(AstProgram* dest, const AstProgram* own) {
    int i = 0;
    for (i = 0; i < own->concepts.count; ++i) {
        if (!find_ast_concept(dest, own->concepts.items[i].name)) {
            concept_list_push(&dest->concepts, clone_concept_decl(own, 0, 0, &own->concepts.items[i], own->concepts.items[i].public_flag));
        }
    }
    for (i = 0; i < own->structs.count; ++i) {
        struct_list_push(&dest->structs, clone_struct_decl(own, 0, 0, &own->structs.items[i], own->structs.items[i].public_flag));
    }
    for (i = 0; i < own->enums.count; ++i) {
        enum_list_push(&dest->enums, clone_enum_decl(own, 0, 0, &own->enums.items[i], own->enums.items[i].public_flag));
    }
    for (i = 0; i < own->unions.count; ++i) {
        union_list_push(&dest->unions, clone_union_decl(own, 0, 0, &own->unions.items[i], own->unions.items[i].public_flag));
    }
    for (i = 0; i < own->globals.count; ++i) {
        global_list_push(&dest->globals, clone_global(own, 0, 0, &own->globals.items[i], own->globals.items[i].public_flag));
    }
    for (i = 0; i < own->functions.count; ++i) {
        function_list_push(&dest->functions, clone_function(own, 0, 0, &own->functions.items[i], own->functions.items[i].public_flag));
    }
}

static void append_own_export_decls(AstProgram* dest, const AstProgram* own) {
    int i = 0;
    for (i = 0; i < own->concepts.count; ++i) {
        if (!find_ast_concept(dest, own->concepts.items[i].name)) {
            concept_list_push(&dest->concepts, clone_concept_decl(own, 0, 1, &own->concepts.items[i], own->concepts.items[i].public_flag));
        }
    }
    for (i = 0; i < own->structs.count; ++i) {
        struct_list_push(&dest->structs, clone_struct_decl(own, 0, 1, &own->structs.items[i], own->structs.items[i].public_flag));
    }
    for (i = 0; i < own->enums.count; ++i) {
        enum_list_push(&dest->enums, clone_enum_decl(own, 0, 1, &own->enums.items[i], own->enums.items[i].public_flag));
    }
    for (i = 0; i < own->unions.count; ++i) {
        union_list_push(&dest->unions, clone_union_decl(own, 0, 1, &own->unions.items[i], own->unions.items[i].public_flag));
    }
    for (i = 0; i < own->globals.count; ++i) {
        global_list_push(&dest->globals, clone_global(own, 0, 1, &own->globals.items[i], own->globals.items[i].public_flag));
    }
    for (i = 0; i < own->functions.count; ++i) {
        function_list_push(&dest->functions, clone_function(own, 0, 1, &own->functions.items[i], own->functions.items[i].public_flag));
    }
}

typedef struct LoadedModule LoadedModule;

typedef struct ModuleImport {
    char* name;
    int public_flag;
    LoadedModule* module;
} ModuleImport;

typedef struct ModuleImportList {
    ModuleImport* items;
    int count;
    int capacity;
} ModuleImportList;

typedef struct LoadedModuleList {
    LoadedModule** items;
    int count;
    int capacity;
} LoadedModuleList;

#define module_import_list_push(list, item) VEC_PUSH((list), (item))
#define loaded_module_list_push(list, item) VEC_PUSH((list), (item))

struct LoadedModule {
    char* path;
    AstProgram own_program;
    AstProgram full_program;
    AstProgram export_program;
    ModuleImportList imports;
    int loading_flag;
    int built_flag;
};

static char* dirname_dup(const char* path) {
    const char* slash = strrchr(path, '/');
    char* out = 0;
    size_t len = 0;
    if (!slash) {
        return dup_text(".");
    }
    len = (size_t)(slash - path);
    out = (char*)malloc(len + 1);
    if (!out) {
        return 0;
    }
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}

static char* resolve_import_path(const char* from_path, const char* import_path) {
    char* dir = dirname_dup(from_path);
    char* full = 0;
    if (!dir) {
        return 0;
    }
    if (import_path[0] == '/') {
        free(dir);
        return dup_text(import_path);
    }
    if (import_path[0] != '.') {
        if (strcmp(import_path, "std") == 0) {
            char* std_root = 0;
            const char* package_error = 0;
            int ok = load_package_root_path("std", &std_root, &package_error);
            free(dir);
            if (!ok) {
                return 0;
            }
            return std_root;
        }
        char* package_dir = find_enclosing_package_dir(from_path);
        if (package_dir) {
            char* dep_dir = package_dependency_dir_path(package_dir, import_path);
            if (dep_dir) {
                char* dep_root = 0;
                const char* package_error = 0;
                int ok = load_package_root_path(dep_dir, &dep_root, &package_error);
                free(dep_dir);
                free(package_dir);
                free(dir);
                if (!ok) {
                    return 0;
                }
                return dep_root;
            }
            free(package_dir);
        }
    }
    full = dup_join3(dir, "/", import_path);
    free(dir);
    return full;
}

static char* import_module_name(const char* import_path, const char** error) {
    const char* slash = strrchr(import_path, '/');
    const char* base = slash ? slash + 1 : import_path;
    const char* dot = strrchr(base, '.');
    size_t len = dot && dot > base ? (size_t)(dot - base) : strlen(base);
    char* out = 0;
    if (len == 0) {
        *error = "invalid import path";
        return 0;
    }
    out = (char*)malloc(len + 1);
    if (!out) {
        *error = "out of memory";
        return 0;
    }
    memcpy(out, base, len);
    out[len] = '\0';
    return out;
}

static int is_prelude_module_path(const char* path) {
    const char* suffix = "/std/prelude.jiang";
    size_t path_len = strlen(path);
    size_t suffix_len = strlen(suffix);
    if (strcmp(path, "std/prelude.jiang") == 0) {
        return 1;
    }
    return path_len >= suffix_len && strcmp(path + path_len - suffix_len, suffix) == 0;
}

static int path_in_stack(const char* path, const char** stack, int depth) {
    int i = 0;
    for (i = 0; i < depth; ++i) {
        if (strcmp(path, stack[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static LoadedModule* find_loaded_module(LoadedModuleList* cache, const char* path) {
    int i = 0;
    for (i = 0; i < cache->count; ++i) {
        if (strcmp(cache->items[i]->path, path) == 0) {
            return cache->items[i];
        }
    }
    return 0;
}

static int build_loaded_module(LoadedModule* module, int inject_prelude, LoadedModule* prelude_module, const char** error) {
    int i = 0;
    if (module->built_flag) {
        return 1;
    }
    memset(&module->full_program, 0, sizeof(module->full_program));
    memset(&module->export_program, 0, sizeof(module->export_program));
    if (inject_prelude && prelude_module) {
        merge_public_import(&module->full_program, &prelude_module->export_program, 0);
    }
    for (i = 0; i < module->imports.count; ++i) {
        merge_public_import(&module->full_program, &module->imports.items[i].module->export_program, module->imports.items[i].name);
        if (module->imports.items[i].public_flag) {
            merge_public_import(&module->export_program, &module->imports.items[i].module->export_program, module->imports.items[i].name);
        }
    }
    append_own_decls(&module->full_program, &module->own_program);
    if (!apply_aliases(&module->full_program, &module->own_program, error)) {
        return 0;
    }
    append_own_export_decls(&module->export_program, &module->own_program);
    if (!apply_public_aliases(&module->export_program, &module->full_program, &module->own_program, error)) {
        return 0;
    }
    module->built_flag = 1;
    return 1;
}

static int load_module_graph(const char* path,
                             const char** stack,
                             int depth,
                             LoadedModuleList* cache,
                             int root_flag,
                             LoadedModule** out_module,
                             const char** error) {
    LoadedModule* module = 0;
    Parser parser;
    char* source = 0;
    int i = 0;
    if (path_in_stack(path, stack, depth)) {
        *error = "import cycle";
        return 0;
    }
    module = find_loaded_module(cache, path);
    if (module) {
        if (module->loading_flag) {
            *error = "import cycle";
            return 0;
        }
        *out_module = module;
        return 1;
    }
    module = (LoadedModule*)calloc(1, sizeof(LoadedModule));
    if (!module) {
        *error = "out of memory";
        return 0;
    }
    module->path = dup_text(path);
    module->loading_flag = 1;
    loaded_module_list_push(cache, module);
    source = read_file(path);
    if (!source) {
        static char read_error[512];
        snprintf(read_error, sizeof(read_error), "failed to read import: %s", path);
        *error = read_error;
        return 0;
    }
    parser_init(&parser, source, path);
    if (!parser_parse_program(&parser, &module->own_program)) {
        static char parse_error[640];
        if (parser.error_line > 0) {
            if (parser.error_column > 0) {
                snprintf(parse_error,
                         sizeof(parse_error),
                         "%s:%d:%d: error: %s",
                         path,
                         parser.error_line,
                         parser.error_column,
                         parser.error ? parser.error : "parse error");
            } else {
                snprintf(parse_error,
                         sizeof(parse_error),
                         "%s:%d: error: %s",
                         path,
                         parser.error_line,
                         parser.error ? parser.error : "parse error");
            }
        } else {
            snprintf(parse_error, sizeof(parse_error), "%s: error: %s", path, parser.error ? parser.error : "parse error");
        }
        *error = parse_error;
        free(source);
        return 0;
    }
    if (!validate_module_decls(&module->own_program, error)) {
        free(source);
        return 0;
    }
    stack[depth] = path;
    for (i = 0; i < module->own_program.imports.count; ++i) {
        LoadedModule* imported = 0;
        ModuleImport item;
        char* import_path = resolve_import_path(path, module->own_program.imports.items[i].path);
        char* import_name = module->own_program.imports.items[i].alias_name
                                ? dup_text(module->own_program.imports.items[i].alias_name)
                                : import_module_name(module->own_program.imports.items[i].path, error);
        if (!import_path) {
            *error = "failed to resolve import";
            free(source);
            return 0;
        }
        if (!import_name) {
            free(import_path);
            free(source);
            return 0;
        }
        if (!load_module_graph(import_path, stack, depth + 1, cache, 0, &imported, error)) {
            free(import_name);
            free(import_path);
            free(source);
            return 0;
        }
        memset(&item, 0, sizeof(item));
        item.name = import_name;
        item.public_flag = module->own_program.imports.items[i].public_flag;
        item.module = imported;
        module_import_list_push(&module->imports, item);
        free(import_path);
    }
    if (!is_prelude_module_path(path)) {
        LoadedModule* prelude_module = 0;
        if (!load_module_graph("std/prelude.jiang", stack, depth + 1, cache, 0, &prelude_module, error)) {
            free(source);
            return 0;
        }
        if (!build_loaded_module(prelude_module, 0, 0, error)) {
            free(source);
            return 0;
        }
        if (!build_loaded_module(module, 1, prelude_module, error)) {
            free(source);
            return 0;
        }
    } else {
        if (!build_loaded_module(module, 0, 0, error)) {
            free(source);
            return 0;
        }
    }
    module->loading_flag = 0;
    *out_module = module;
    free(source);
    return 1;
}

static int load_effective_program(const char* path, const char** stack, int depth, AstProgram* out_program, const char** error) {
    LoadedModuleList cache;
    LoadedModule* root = 0;
    memset(&cache, 0, sizeof(cache));
    memset(out_program, 0, sizeof(*out_program));
    if (!load_module_graph(path, stack, depth, &cache, 1, &root, error)) {
        return 0;
    }
    *out_program = root->full_program;
    return 1;
}

typedef struct MonoContext {
    const AstProgram* source;
    AstProgram* out;
    const char* error;
} MonoContext;

typedef struct TypeSubst {
    const char* name;
    AstType type;
} TypeSubst;

typedef struct TypeSubstList {
    TypeSubst* items;
    int count;
    int capacity;
} TypeSubstList;

#define subst_list_push(list, item) VEC_PUSH((list), (item))

static const AstStructDecl* find_generic_struct_template(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->structs.count; ++i) {
        if (program->structs.items[i].type_params.count > 0 &&
            strcmp(program->structs.items[i].name, name) == 0) {
            return &program->structs.items[i];
        }
    }
    return 0;
}

static const AstUnionDecl* find_generic_union_template(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->unions.count; ++i) {
        if (program->unions.items[i].type_params.count > 0 &&
            strcmp(program->unions.items[i].name, name) == 0) {
            return &program->unions.items[i];
        }
    }
    return 0;
}

static const AstFunction* find_generic_function_template(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->functions.count; ++i) {
        if (program->functions.items[i].type_params.count > 0 &&
            strcmp(program->functions.items[i].name, name) == 0) {
            return &program->functions.items[i];
        }
    }
    return 0;
}

static int is_generic_owner_method(const AstProgram* program, const AstFunction* fn) {
    AstNominalDeclRef nominal;
    return fn->method_flag &&
           fn->owner_type_name &&
           ((nominal = find_ast_nominal_decl(program, fn->owner_type_name)), ast_nominal_has_type_params(nominal));
}

static const AstFunction* find_method_template(const AstProgram* program, const char* owner_name, const char* method_name) {
    int i = 0;
    for (i = 0; i < program->functions.count; ++i) {
        if (program->functions.items[i].method_flag &&
            program->functions.items[i].owner_type_name &&
            strcmp(program->functions.items[i].owner_type_name, owner_name) == 0 &&
            strcmp(program->functions.items[i].name, method_name) == 0) {
            return &program->functions.items[i];
        }
    }
    return 0;
}

static const AstFunction* find_type_method_template(const AstProgram* program, const AstType* type, const char* method_name) {
    AstTypeQueryRef query = describe_ast_type(program, type);
    if (query.kind != AST_TYPE_QUERY_NOMINAL || !query.nominal.name) {
        return 0;
    }
    return find_method_template(program, query.nominal.name, method_name);
}

static const AstType* lookup_subst(const TypeSubstList* subst, const char* name) {
    int i = 0;
    for (i = 0; i < subst->count; ++i) {
        if (strcmp(subst->items[i].name, name) == 0) {
            return &subst->items[i].type;
        }
    }
    return 0;
}

typedef enum BuiltinConceptCapability {
    BUILTIN_CONCEPT_NONE = 0,
    BUILTIN_CONCEPT_NUMBRIC = 1 << 0,
    BUILTIN_CONCEPT_EQUATABLE = 1 << 1,
    BUILTIN_CONCEPT_HASHABLE = 1 << 2,
    BUILTIN_CONCEPT_MUTABLE = 1 << 3,
    BUILTIN_CONCEPT_MAYBE_MUTABLE = 1 << 4,
} BuiltinConceptCapability;

static uint32_t ast_type_builtin_capabilities(const AstProgram* program, const AstType* type) {
    AstTypeQueryRef query = describe_ast_type(program, type);
    switch (query.kind) {
        case AST_TYPE_QUERY_NOMINAL:
            if (query.nominal.kind == AST_NOMINAL_BUILTIN) {
                const AstBuiltinNominalDecl* builtin = (const AstBuiltinNominalDecl*)query.nominal.decl;
                switch (builtin->kind) {
                    case AST_BUILTIN_NOMINAL_INT:
                    case AST_BUILTIN_NOMINAL_I8:
                    case AST_BUILTIN_NOMINAL_I16:
                    case AST_BUILTIN_NOMINAL_I32:
                    case AST_BUILTIN_NOMINAL_I64:
                    case AST_BUILTIN_NOMINAL_U8:
                    case AST_BUILTIN_NOMINAL_U16:
                    case AST_BUILTIN_NOMINAL_U32:
                    case AST_BUILTIN_NOMINAL_U64:
                    case AST_BUILTIN_NOMINAL_UINT8:
                        return BUILTIN_CONCEPT_NUMBRIC | BUILTIN_CONCEPT_EQUATABLE | BUILTIN_CONCEPT_HASHABLE;
                    case AST_BUILTIN_NOMINAL_F16:
                    case AST_BUILTIN_NOMINAL_F32:
                    case AST_BUILTIN_NOMINAL_F64:
                    case AST_BUILTIN_NOMINAL_FLOAT:
                    case AST_BUILTIN_NOMINAL_DOUBLE:
                        return BUILTIN_CONCEPT_NUMBRIC | BUILTIN_CONCEPT_EQUATABLE;
                    case AST_BUILTIN_NOMINAL_CHARACTER:
                        return BUILTIN_CONCEPT_EQUATABLE | BUILTIN_CONCEPT_HASHABLE;
                    case AST_BUILTIN_NOMINAL_BOOL:
                        return BUILTIN_CONCEPT_EQUATABLE | BUILTIN_CONCEPT_HASHABLE;
                    case AST_BUILTIN_NOMINAL_VOID:
                    case AST_BUILTIN_NOMINAL_NONE:
                    default:
                        return BUILTIN_CONCEPT_NONE;
                }
            }
            if (query.nominal.kind == AST_NOMINAL_ENUM) {
                return BUILTIN_CONCEPT_EQUATABLE;
            }
            return BUILTIN_CONCEPT_NONE;
        case AST_TYPE_QUERY_POINTER:
        case AST_TYPE_QUERY_MANY_POINTER:
            return BUILTIN_CONCEPT_EQUATABLE;
        case AST_TYPE_QUERY_OPTIONAL:
            return query.item_type ? ast_type_builtin_capabilities(program, query.item_type) : BUILTIN_CONCEPT_NONE;
        case AST_TYPE_QUERY_TUPLE:
        case AST_TYPE_QUERY_SLICE:
        case AST_TYPE_QUERY_ARRAY:
        case AST_TYPE_QUERY_INFER:
        case AST_TYPE_QUERY_NONE:
        default:
            return BUILTIN_CONCEPT_NONE;
    }
}

static uint32_t builtin_concept_flag_for_name(const char* concept_name) {
    if (strcmp(concept_name, "Numbric") == 0) return BUILTIN_CONCEPT_NUMBRIC;
    if (strcmp(concept_name, "Equatable") == 0) return BUILTIN_CONCEPT_EQUATABLE;
    if (strcmp(concept_name, "Hashable") == 0) return BUILTIN_CONCEPT_HASHABLE;
    if (strcmp(concept_name, "Mutable") == 0) return BUILTIN_CONCEPT_MUTABLE;
    if (strcmp(concept_name, "MaybeMutable") == 0) return BUILTIN_CONCEPT_MAYBE_MUTABLE;
    return BUILTIN_CONCEPT_NONE;
}

static int concept_exists(const AstProgram* program, const char* concept_name) {
    return builtin_concept_flag_for_name(concept_name) != BUILTIN_CONCEPT_NONE ||
           find_ast_concept(program, concept_name) != 0;
}

static int ast_type_has_builtin_concept(const AstProgram* program, const AstType* type, const char* concept_name) {
    uint32_t required = builtin_concept_flag_for_name(concept_name);
    if (required == BUILTIN_CONCEPT_MUTABLE) return type && type->mutable_flag;
    if (required == BUILTIN_CONCEPT_MAYBE_MUTABLE) return 1;
    if (required == BUILTIN_CONCEPT_NONE) return 0;
    return (ast_type_builtin_capabilities(program, type) & required) != 0;
}

static int concept_name_is_or_inherits(const AstProgram* program,
                                       const char* actual_name,
                                       const char* target_name,
                                       AstNameList* seen_concepts);
static int ast_type_satisfies_concept(const AstProgram* program, const AstType* type, const char* concept_name);

static AstType substitute_self_type(const AstType* type, const AstType* self_type) {
    AstType out;
    int i = 0;
    if (type->kind == AST_TYPE_NAMED && type->named_name && strcmp(type->named_name, "Self") == 0 && type->type_args.count == 0) {
        AstType copied = ast_type_copy(self_type);
        if (type->mutable_flag) {
            copied.mutable_flag = 1;
        }
        return copied;
    }
    memset(&out, 0, sizeof(out));
    out.kind = type->kind;
    out.mutable_flag = type->mutable_flag;
    out.array_length = type->array_length;
    if (type->named_name) {
        out.named_name = dup_text(type->named_name);
    }
    for (i = 0; i < type->type_args.count; ++i) {
        type_list_push(&out.type_args, substitute_self_type(&type->type_args.items[i], self_type));
    }
    if (type->array_item) {
        out.array_item = (AstType*)malloc(sizeof(AstType));
        *out.array_item = substitute_self_type(type->array_item, self_type);
    }
    for (i = 0; i < type->tuple_items.count; ++i) {
        type_list_push(&out.tuple_items, substitute_self_type(&type->tuple_items.items[i], self_type));
    }
    return out;
}

static AstType substitute_contract_type(const AstType* type,
                                        const AstType* self_type,
                                        const ResolvedAssocTypeBindingList* assoc_bindings) {
    const AstType* assoc_binding = 0;
    AstType out;
    int i = 0;
    if (type->kind == AST_TYPE_NAMED && type->named_name && strcmp(type->named_name, "Self") == 0 && type->type_args.count == 0) {
        AstType copied = ast_type_copy(self_type);
        if (type->mutable_flag) {
            copied.mutable_flag = 1;
        }
        return copied;
    }
    assoc_binding = lookup_resolved_assoc_type_binding(assoc_bindings, type->named_name ? type->named_name : "");
    if (type->kind == AST_TYPE_NAMED && assoc_binding && type->type_args.count == 0) {
        AstType copied = ast_type_copy(assoc_binding);
        if (type->mutable_flag) {
            copied.mutable_flag = 1;
        }
        return copied;
    }
    memset(&out, 0, sizeof(out));
    out.kind = type->kind;
    out.mutable_flag = type->mutable_flag;
    out.array_length = type->array_length;
    if (type->named_name) {
        out.named_name = dup_text(type->named_name);
    }
    for (i = 0; i < type->type_args.count; ++i) {
        type_list_push(&out.type_args, substitute_contract_type(&type->type_args.items[i], self_type, assoc_bindings));
    }
    if (type->array_item) {
        out.array_item = (AstType*)malloc(sizeof(AstType));
        *out.array_item = substitute_contract_type(type->array_item, self_type, assoc_bindings);
    }
    for (i = 0; i < type->tuple_items.count; ++i) {
        type_list_push(&out.tuple_items, substitute_contract_type(&type->tuple_items.items[i], self_type, assoc_bindings));
    }
    return out;
}

static int resolve_nominal_assoc_type_bindings(const AstProgram* program,
                                               AstNominalDeclRef nominal,
                                               const AstConceptDecl* target_concept,
                                               const ConceptAssocTypeRefList* assoc_types,
                                               ResolvedAssocTypeBindingList* out,
                                               const char** error,
                                               char** detail_name) {
    int i = 0;
    for (i = 0; i < nominal_assoc_type_binding_count(nominal); ++i) {
        const AstAssocTypeBinding* binding = find_nominal_assoc_type_binding(nominal, i);
        AstNameList seen_concepts;
        int j = 0;
        int match_count = 0;
        const char* matched_concept_name = 0;
        if (!find_concept_assoc_type_ref((ConceptAssocTypeRefList*)assoc_types, binding->name)) {
            continue;
        }
        memset(&seen_concepts, 0, sizeof(seen_concepts));
        if (binding->concept_name) {
            const AstConceptDecl* context_concept = find_ast_concept(program, binding->concept_name);
            ConceptAssocTypeRefList context_assoc_types;
            AstNameList seen_assoc;
            AstNameList active_assoc;
            int visible = 0;
            memset(&context_assoc_types, 0, sizeof(context_assoc_types));
            memset(&seen_assoc, 0, sizeof(seen_assoc));
            memset(&active_assoc, 0, sizeof(active_assoc));
            if (!context_concept) {
                *error = "unknown associated type binding";
                *detail_name = binding->concept_name;
                return 0;
            }
            for (j = 0; j < binding->context_concept_names.count; ++j) {
                AstNameList seen_visible;
                memset(&seen_visible, 0, sizeof(seen_visible));
                if (concept_name_is_or_inherits(program, binding->context_concept_names.items[j], binding->concept_name, &seen_visible)) {
                    visible = 1;
                    break;
                }
            }
            if (!visible) {
                *error = "unknown associated type binding";
                *detail_name = binding->concept_name;
                return 0;
            }
            if (!collect_concept_assoc_types(program, context_concept, &seen_assoc, &active_assoc, &context_assoc_types, error, detail_name)) {
                return 0;
            }
            if (!find_concept_assoc_type_ref(&context_assoc_types, binding->name)) {
                *error = "unknown associated type binding";
                *detail_name = binding->name;
                return 0;
            }
            match_count = 1;
            matched_concept_name = context_concept->name;
        } else {
        for (j = 0; j < binding->context_concept_names.count; ++j) {
            const AstConceptDecl* context_concept = find_ast_concept(program, binding->context_concept_names.items[j]);
            ConceptAssocTypeRefList context_assoc_types;
            AstNameList seen_assoc;
            AstNameList active_assoc;
            memset(&context_assoc_types, 0, sizeof(context_assoc_types));
            memset(&seen_assoc, 0, sizeof(seen_assoc));
            memset(&active_assoc, 0, sizeof(active_assoc));
            if (!context_concept) {
                continue;
            }
            if (!collect_concept_assoc_types(program, context_concept, &seen_assoc, &active_assoc, &context_assoc_types, error, detail_name)) {
                return 0;
            }
            if (find_concept_assoc_type_ref(&context_assoc_types, binding->name)) {
                match_count += 1;
                matched_concept_name = context_concept->name;
            }
        }
        }
        if (match_count == 0) {
            *error = "unknown associated type binding";
            *detail_name = binding->concept_name ? binding->concept_name : binding->name;
            return 0;
        }
        if (match_count > 1) {
            *error = "associated type binding is ambiguous";
            *detail_name = binding->name;
            return 0;
        }
        memset(&seen_concepts, 0, sizeof(seen_concepts));
        if (!concept_name_is_or_inherits(program, target_concept->name, matched_concept_name, &seen_concepts)) {
            continue;
        }
        for (j = 0; j < out->count; ++j) {
            if (strcmp(out->items[j].name, binding->name) == 0) {
                *error = "duplicate associated type binding";
                *detail_name = binding->name;
                return 0;
            }
        }
        {
            ResolvedAssocTypeBinding item;
            memset(&item, 0, sizeof(item));
            item.name = binding->name;
            item.value = &binding->value;
            item.line = binding->line;
            resolved_assoc_type_binding_list_push(out, item);
        }
    }
    return 1;
}

static int validate_nominal_assoc_type_bindings(const AstProgram* program,
                                                AstNominalDeclRef nominal,
                                                const char** error,
                                                char** detail_name) {
    int i = 0;
    for (i = 0; i < nominal_assoc_type_binding_count(nominal); ++i) {
        const AstAssocTypeBinding* binding = find_nominal_assoc_type_binding(nominal, i);
        int j = 0;
        int match_count = 0;
        if (binding->concept_name) {
            const AstConceptDecl* context_concept = find_ast_concept(program, binding->concept_name);
            ConceptAssocTypeRefList context_assoc_types;
            AstNameList seen_assoc;
            AstNameList active_assoc;
            int visible = 0;
            memset(&context_assoc_types, 0, sizeof(context_assoc_types));
            memset(&seen_assoc, 0, sizeof(seen_assoc));
            memset(&active_assoc, 0, sizeof(active_assoc));
            if (!context_concept) {
                *error = "unknown associated type binding";
                *detail_name = binding->concept_name;
                return 0;
            }
            for (j = 0; j < binding->context_concept_names.count; ++j) {
                AstNameList seen_visible;
                memset(&seen_visible, 0, sizeof(seen_visible));
                if (concept_name_is_or_inherits(program, binding->context_concept_names.items[j], binding->concept_name, &seen_visible)) {
                    visible = 1;
                    break;
                }
            }
            if (!visible) {
                *error = "unknown associated type binding";
                *detail_name = binding->concept_name;
                return 0;
            }
            if (!collect_concept_assoc_types(program, context_concept, &seen_assoc, &active_assoc, &context_assoc_types, error, detail_name)) {
                return 0;
            }
            match_count = find_concept_assoc_type_ref(&context_assoc_types, binding->name) ? 1 : 0;
        } else {
            for (j = 0; j < binding->context_concept_names.count; ++j) {
                const AstConceptDecl* context_concept = find_ast_concept(program, binding->context_concept_names.items[j]);
                ConceptAssocTypeRefList context_assoc_types;
                AstNameList seen_assoc;
                AstNameList active_assoc;
                memset(&context_assoc_types, 0, sizeof(context_assoc_types));
                memset(&seen_assoc, 0, sizeof(seen_assoc));
                memset(&active_assoc, 0, sizeof(active_assoc));
                if (!context_concept) {
                    continue;
                }
                if (!collect_concept_assoc_types(program, context_concept, &seen_assoc, &active_assoc, &context_assoc_types, error, detail_name)) {
                    return 0;
                }
                if (find_concept_assoc_type_ref(&context_assoc_types, binding->name)) {
                    match_count += 1;
                }
            }
        }
        if (match_count == 0) {
            *error = "unknown associated type binding";
            *detail_name = binding->concept_name ? binding->concept_name : binding->name;
            return 0;
        }
        if (match_count > 1) {
            *error = "associated type binding is ambiguous";
            *detail_name = binding->name;
            return 0;
        }
    }
    return 1;
}

static int type_has_concept_methods(const AstProgram* program, const AstType* type, const AstConceptDecl* concept) {
    int i = 0;
    AstTypeQueryRef query = describe_ast_type(program, type);
    AstNameList seen_concepts;
    AstNameList active_concepts;
    ConceptMethodRefList methods;
    ConceptAssocTypeRefList assoc_types;
    ResolvedAssocTypeBindingList assoc_bindings;
    const char* collect_error = 0;
    char* detail_name = 0;
    memset(&seen_concepts, 0, sizeof(seen_concepts));
    memset(&active_concepts, 0, sizeof(active_concepts));
    memset(&methods, 0, sizeof(methods));
    memset(&assoc_types, 0, sizeof(assoc_types));
    memset(&assoc_bindings, 0, sizeof(assoc_bindings));
    if (!collect_concept_assoc_types(program, concept, &seen_concepts, &active_concepts, &assoc_types, &collect_error, &detail_name)) {
        return 0;
    }
    memset(&seen_concepts, 0, sizeof(seen_concepts));
    memset(&active_concepts, 0, sizeof(active_concepts));
    if (!collect_concept_method_names(program, concept, &seen_concepts, &active_concepts, &methods, &collect_error, &detail_name)) {
        return 0;
    }
    if (query.kind != AST_TYPE_QUERY_NOMINAL) {
        return methods.count == 0 && assoc_types.count == 0;
    }
    if (query.nominal.kind == AST_NOMINAL_BUILTIN) {
        if (assoc_types.count != 0) {
            return 0;
        }
        const AstBuiltinNominalDecl* builtin = (const AstBuiltinNominalDecl*)query.nominal.decl;
        for (i = 0; i < methods.count; ++i) {
            const AstConceptMethod* method = methods.items[i].method;
            if (strcmp(method->name, "hash") == 0) {
                if (method->params.count != 0 || method->return_type.kind != AST_TYPE_INT) {
                    return 0;
                }
                if (!(builtin->kind == AST_BUILTIN_NOMINAL_INT ||
                      builtin->kind == AST_BUILTIN_NOMINAL_I8 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_I16 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_I32 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_I64 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_U8 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_U16 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_U32 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_U64 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_UINT8 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_CHARACTER ||
                      builtin->kind == AST_BUILTIN_NOMINAL_BOOL)) {
                    return 0;
                }
                continue;
            }
            if (strcmp(method->name, "equal") == 0) {
                AstType expected_param;
                if (method->params.count != 1 || method->return_type.kind != AST_TYPE_BOOL) {
                    return 0;
                }
                expected_param = substitute_self_type(&method->params.items[0].type, type);
                if (!ast_type_is_equal(&expected_param, type)) {
                    return 0;
                }
                if (!(builtin->kind == AST_BUILTIN_NOMINAL_INT ||
                      builtin->kind == AST_BUILTIN_NOMINAL_I8 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_I16 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_I32 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_I64 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_U8 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_U16 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_U32 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_U64 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_F16 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_F32 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_F64 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_FLOAT ||
                      builtin->kind == AST_BUILTIN_NOMINAL_DOUBLE ||
                      builtin->kind == AST_BUILTIN_NOMINAL_CHARACTER ||
                      builtin->kind == AST_BUILTIN_NOMINAL_UINT8 ||
                      builtin->kind == AST_BUILTIN_NOMINAL_BOOL)) {
                    return 0;
                }
                continue;
            }
            return 0;
        }
        return 1;
    }
    if (!resolve_nominal_assoc_type_bindings(program, query.nominal, concept, &assoc_types, &assoc_bindings, &collect_error, &detail_name)) {
        return 0;
    }
    for (i = 0; i < assoc_types.count; ++i) {
        const AstType* assoc_value = lookup_resolved_assoc_type_binding(&assoc_bindings, assoc_types.items[i].name);
        int j = 0;
        if (!assoc_value) {
            return 0;
        }
        for (j = 0; j < assoc_types.items[i].where_constraints.count; ++j) {
            const AstWhereConstraint* constraint = &assoc_types.items[i].where_constraints.items[j];
            if (constraint->kind == AST_WHERE_CONCEPT) {
                if (!ast_type_satisfies_concept(program, assoc_value, constraint->concept_name)) {
                    return 0;
                }
            } else if (!ast_type_is_equal(assoc_value, &constraint->equal_type)) {
                return 0;
            }
        }
    }
    for (i = 0; i < methods.count; ++i) {
        const AstConceptMethod* requirement = methods.items[i].method;
        const AstConceptDecl* owner_concept = methods.items[i].owner;
        const AstFunction* method = find_type_method_template(program, type, requirement->name);
        int j = 0;
        if (!assoc_where_constraints_satisfied(program, &requirement->where_constraints, &assoc_bindings)) {
            continue;
        }
        if (!method || method->static_method_flag) {
            return 0;
        }
        if (owner_concept && owner_concept->public_flag && !method->public_flag) {
            return 0;
        }
        {
            AstType expected_return = substitute_contract_type(&requirement->return_type, type, &assoc_bindings);
            if (!ast_type_is_equal(&method->return_type, &expected_return)) {
                return 0;
            }
        }
        if (method->params.count != requirement->params.count) {
            return 0;
        }
        for (j = 0; j < method->params.count; ++j) {
            AstType expected_param = substitute_contract_type(&requirement->params.items[j].type, type, &assoc_bindings);
            if (!ast_type_is_equal(&method->params.items[j].type, &expected_param)) {
                return 0;
            }
        }
    }
    return 1;
}

static int type_has_builtin_concept_methods(const AstProgram* program, const AstType* type, const char* concept_name) {
    AstConceptDecl synthetic;
    AstConceptMethod method;
    AstParam param;
    AstType self_type;
    memset(&synthetic, 0, sizeof(synthetic));
    memset(&method, 0, sizeof(method));
    memset(&param, 0, sizeof(param));
    memset(&self_type, 0, sizeof(self_type));

    if (strcmp(concept_name, "Hashable") == 0) {
        synthetic.name = "Hashable";
        method.name = "hash";
        method.return_type.kind = AST_TYPE_INT;
        concept_method_list_push(&synthetic.methods, method);
        return type_has_concept_methods(program, type, &synthetic);
    }

    if (strcmp(concept_name, "Equatable") == 0) {
        synthetic.name = "Equatable";
        self_type.kind = AST_TYPE_NAMED;
        self_type.named_name = "Self";
        param.name = "other";
        param.type = self_type;
        method.name = "equal";
        method.return_type.kind = AST_TYPE_BOOL;
        param_list_push(&method.params, param);
        concept_method_list_push(&synthetic.methods, method);
        return type_has_concept_methods(program, type, &synthetic);
    }

    if (strcmp(concept_name, "Mutable") == 0 || strcmp(concept_name, "MaybeMutable") == 0) {
        return 1;
    }

    return 1;
}

static int nominal_declares_concept_name(AstNominalDeclRef nominal, const char* concept_name) {
    int i = 0;
    switch (nominal.kind) {
        case AST_NOMINAL_STRUCT: {
            const AstStructDecl* decl = (const AstStructDecl*)nominal.decl;
            for (i = 0; i < decl->concept_names.count; ++i) {
                if (strcmp(decl->concept_names.items[i], concept_name) == 0) {
                    return 1;
                }
            }
            return 0;
        }
        case AST_NOMINAL_ENUM: {
            const AstEnumDecl* decl = (const AstEnumDecl*)nominal.decl;
            for (i = 0; i < decl->concept_names.count; ++i) {
                if (strcmp(decl->concept_names.items[i], concept_name) == 0) {
                    return 1;
                }
            }
            return 0;
        }
        case AST_NOMINAL_UNION: {
            const AstUnionDecl* decl = (const AstUnionDecl*)nominal.decl;
            for (i = 0; i < decl->concept_names.count; ++i) {
                if (strcmp(decl->concept_names.items[i], concept_name) == 0) {
                    return 1;
                }
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int concept_name_is_or_inherits(const AstProgram* program,
                                       const char* actual_name,
                                       const char* target_name,
                                       AstNameList* seen_concepts) {
    const AstConceptDecl* concept = 0;
    int i = 0;
    if (strcmp(actual_name, target_name) == 0) {
        return 1;
    }
    if (ast_name_list_contains(seen_concepts, actual_name)) {
        return 0;
    }
    name_list_push(seen_concepts, actual_name);
    concept = find_ast_concept(program, actual_name);
    if (!concept) {
        return 0;
    }
    for (i = 0; i < concept->concept_names.count; ++i) {
        if (concept_name_is_or_inherits(program, concept->concept_names.items[i], target_name, seen_concepts)) {
            return 1;
        }
    }
    return 0;
}

static int nominal_declares_concept_or_child(const AstProgram* program,
                                             AstNominalDeclRef nominal,
                                             const char* concept_name) {
    AstNameList seen_concepts;
    int i = 0;
    memset(&seen_concepts, 0, sizeof(seen_concepts));
    switch (nominal.kind) {
        case AST_NOMINAL_STRUCT: {
            const AstStructDecl* decl = (const AstStructDecl*)nominal.decl;
            for (i = 0; i < decl->concept_names.count; ++i) {
                if (concept_name_is_or_inherits(program, decl->concept_names.items[i], concept_name, &seen_concepts)) {
                    return 1;
                }
            }
            return 0;
        }
        case AST_NOMINAL_ENUM: {
            const AstEnumDecl* decl = (const AstEnumDecl*)nominal.decl;
            for (i = 0; i < decl->concept_names.count; ++i) {
                if (concept_name_is_or_inherits(program, decl->concept_names.items[i], concept_name, &seen_concepts)) {
                    return 1;
                }
            }
            return 0;
        }
        case AST_NOMINAL_UNION: {
            const AstUnionDecl* decl = (const AstUnionDecl*)nominal.decl;
            for (i = 0; i < decl->concept_names.count; ++i) {
                if (concept_name_is_or_inherits(program, decl->concept_names.items[i], concept_name, &seen_concepts)) {
                    return 1;
                }
            }
            return 0;
        }
        default:
            return 0;
    }
}

static int ast_type_satisfies_concept(const AstProgram* program, const AstType* type, const char* concept_name) {
    const AstConceptDecl* concept = find_ast_concept(program, concept_name);
    AstTypeQueryRef query = describe_ast_type(program, type);
    if (builtin_concept_flag_for_name(concept_name) != BUILTIN_CONCEPT_NONE) {
        if (!ast_type_has_builtin_concept(program, type, concept_name)) {
            return 0;
        }
        if (!type_has_builtin_concept_methods(program, type, concept_name)) {
            return 0;
        }
        if (query.kind == AST_TYPE_QUERY_NOMINAL && query.nominal.kind != AST_NOMINAL_BUILTIN &&
            !nominal_declares_concept_or_child(program, query.nominal, concept_name)) {
            return 0;
        }
        return !concept || type_has_concept_methods(program, type, concept);
    }
    if (!concept) {
        return 0;
    }
    if (query.kind != AST_TYPE_QUERY_NOMINAL || !nominal_declares_concept_or_child(program, query.nominal, concept_name)) {
        return 0;
    }
    return type_has_concept_methods(program, type, concept);
}

static int type_param_exists(const AstNameList* params, const char* name) {
    int i = 0;
    for (i = 0; i < params->count; ++i) {
        if (strcmp(params->items[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int concept_assoc_type_exists(const ConceptAssocTypeRefList* assoc_types, const char* name) {
    return find_concept_assoc_type_ref((ConceptAssocTypeRefList*)assoc_types, name) != 0;
}

static int function_type_param_exists(const AstProgram* program, const AstFunction* fn, const char* name) {
    if (type_param_exists(&fn->type_params, name)) {
        return 1;
    }
    if (fn->method_flag && fn->owner_type_name) {
        const AstStructDecl* owner = find_ast_struct(program, fn->owner_type_name);
        if (owner && type_param_exists(&owner->type_params, name)) {
            return 1;
        }
    }
    return 0;
}

typedef enum GenericMutabilityPolicy {
    GENERIC_MUTABILITY_IMMUTABLE = 0,
    GENERIC_MUTABILITY_MUTABLE,
    GENERIC_MUTABILITY_MAYBE_MUTABLE,
} GenericMutabilityPolicy;

static GenericMutabilityPolicy generic_mutability_policy(const AstWhereConstraintList* where_constraints, const char* param_name) {
    int i = 0;
    for (i = 0; i < where_constraints->count; ++i) {
        if (where_constraints->items[i].kind != AST_WHERE_CONCEPT) {
            continue;
        }
        if (strcmp(where_constraints->items[i].param_name, param_name) != 0) {
            continue;
        }
        if (strcmp(where_constraints->items[i].concept_name, "Mutable") == 0) {
            return GENERIC_MUTABILITY_MUTABLE;
        }
        if (strcmp(where_constraints->items[i].concept_name, "MaybeMutable") == 0) {
            return GENERIC_MUTABILITY_MAYBE_MUTABLE;
        }
    }
    return GENERIC_MUTABILITY_IMMUTABLE;
}

static int check_generic_mutability_constraints(const AstNameList* type_params,
                                                const AstWhereConstraintList* where_constraints,
                                                const TypeSubstList* subst,
                                                const char** error) {
    int i = 0;
    for (i = 0; i < type_params->count; ++i) {
        const AstType* actual = lookup_subst(subst, type_params->items[i]);
        GenericMutabilityPolicy policy = generic_mutability_policy(where_constraints, type_params->items[i]);
        if (!actual) {
            *error = "missing generic substitution for @where";
            return 0;
        }
        if (policy == GENERIC_MUTABILITY_MUTABLE && !actual->mutable_flag) {
            *error = "generic type does not satisfy trait";
            return 0;
        }
        if (policy == GENERIC_MUTABILITY_IMMUTABLE && actual->mutable_flag) {
            *error = "generic type does not satisfy trait";
            return 0;
        }
    }
    return 1;
}

static int validate_where_constraints_program(const AstProgram* program, const char** error) {
    int i = 0;
    int j = 0;
    static char where_error[256];
    AstType self_type;
    for (i = 0; i < program->concepts.count; ++i) {
        AstNameList concept_names;
        AstNameList seen_assoc_concepts;
        AstNameList active_assoc_concepts;
        ConceptAssocTypeRefList assoc_types;
        const char* collect_error = 0;
        char* detail_name = 0;
        memset(&concept_names, 0, sizeof(concept_names));
        memset(&seen_assoc_concepts, 0, sizeof(seen_assoc_concepts));
        memset(&active_assoc_concepts, 0, sizeof(active_assoc_concepts));
        memset(&assoc_types, 0, sizeof(assoc_types));
        name_list_push(&concept_names, program->concepts.items[i].name);
        if (!nominal_concepts_have_unique_method_names(program, &concept_names, &collect_error, &detail_name)) {
            if (strcmp(collect_error, "unknown parent trait") == 0) {
                snprintf(where_error, sizeof(where_error), "unknown parent trait: %s", detail_name);
            } else if (strcmp(collect_error, "cyclic trait inheritance") == 0) {
                snprintf(where_error, sizeof(where_error), "cyclic trait inheritance: %s", detail_name);
            } else {
                snprintf(where_error, sizeof(where_error), "trait requirement conflict: %s", detail_name);
            }
            *error = where_error;
            return 0;
        }
        if (!collect_concept_assoc_types(program, &program->concepts.items[i], &seen_assoc_concepts, &active_assoc_concepts, &assoc_types, &collect_error, &detail_name)) {
            if (strcmp(collect_error, "unknown parent trait") == 0) {
                snprintf(where_error, sizeof(where_error), "unknown parent trait: %s", detail_name);
            } else if (strcmp(collect_error, "cyclic trait inheritance") == 0) {
                snprintf(where_error, sizeof(where_error), "cyclic trait inheritance: %s", detail_name);
            } else if (strcmp(collect_error, "duplicate associated type") == 0) {
                snprintf(where_error, sizeof(where_error), "duplicate associated type: %s", detail_name);
            } else if (strcmp(collect_error, "unknown associated type in @where") == 0) {
                snprintf(where_error, sizeof(where_error), "unknown associated type in @where: %s", detail_name);
            } else {
                snprintf(where_error, sizeof(where_error), "associated type conflict: %s", detail_name);
            }
            *error = where_error;
            return 0;
        }
        {
            int local_assoc_index = 0;
            for (local_assoc_index = 0; local_assoc_index < program->concepts.items[i].assoc_types.count; ++local_assoc_index) {
                int parent_index = 0;
                for (parent_index = 0; parent_index < program->concepts.items[i].concept_names.count; ++parent_index) {
                    const AstConceptDecl* parent = find_ast_concept(program, program->concepts.items[i].concept_names.items[parent_index]);
                    AstNameList parent_seen;
                    AstNameList parent_active;
                    ConceptAssocTypeRefList parent_assoc_types;
                    memset(&parent_seen, 0, sizeof(parent_seen));
                    memset(&parent_active, 0, sizeof(parent_active));
                    memset(&parent_assoc_types, 0, sizeof(parent_assoc_types));
                    if (!parent) {
                        continue;
                    }
                    if (!collect_concept_assoc_types(program, parent, &parent_seen, &parent_active, &parent_assoc_types, &collect_error, &detail_name)) {
                        snprintf(where_error, sizeof(where_error), "duplicate associated type: %s", program->concepts.items[i].assoc_types.items[local_assoc_index].name);
                        *error = where_error;
                        return 0;
                    }
                    if (find_concept_assoc_type_ref(&parent_assoc_types, program->concepts.items[i].assoc_types.items[local_assoc_index].name)) {
                        snprintf(where_error, sizeof(where_error), "duplicate associated type: %s", program->concepts.items[i].assoc_types.items[local_assoc_index].name);
                        *error = where_error;
                        return 0;
                    }
                }
            }
        }
        for (j = 0; j < program->concepts.items[i].where_constraints.count; ++j) {
            AstWhereConstraint* item = &program->concepts.items[i].where_constraints.items[j];
            if (!concept_assoc_type_exists(&assoc_types, item->param_name)) {
                snprintf(where_error, sizeof(where_error), "unknown associated type in @where: %s", item->param_name);
                *error = where_error;
                return 0;
            }
            if (item->kind == AST_WHERE_CONCEPT && !concept_exists(program, item->concept_name)) {
                snprintf(where_error, sizeof(where_error), "unknown trait in @where: %s", item->concept_name);
                *error = where_error;
                return 0;
            }
        }
        for (j = 0; j < program->concepts.items[i].assoc_types.count; ++j) {
            int k = 0;
            for (k = 0; k < program->concepts.items[i].assoc_types.items[j].where_constraints.count; ++k) {
                AstWhereConstraint* item = &program->concepts.items[i].assoc_types.items[j].where_constraints.items[k];
                if (!concept_assoc_type_exists(&assoc_types, item->param_name)) {
                    snprintf(where_error, sizeof(where_error), "unknown associated type in @where: %s", item->param_name);
                    *error = where_error;
                    return 0;
                }
                if (item->kind == AST_WHERE_CONCEPT && !concept_exists(program, item->concept_name)) {
                    snprintf(where_error, sizeof(where_error), "unknown trait in @where: %s", item->concept_name);
                    *error = where_error;
                    return 0;
                }
            }
        }
        for (j = 0; j < program->concepts.items[i].methods.count; ++j) {
            int k = 0;
            for (k = 0; k < program->concepts.items[i].methods.items[j].where_constraints.count; ++k) {
                AstWhereConstraint* item = &program->concepts.items[i].methods.items[j].where_constraints.items[k];
                if (!concept_assoc_type_exists(&assoc_types, item->param_name)) {
                    snprintf(where_error, sizeof(where_error), "unknown associated type in @where: %s", item->param_name);
                    *error = where_error;
                    return 0;
                }
                if (item->kind == AST_WHERE_CONCEPT && !concept_exists(program, item->concept_name)) {
                    snprintf(where_error, sizeof(where_error), "unknown trait in @where: %s", item->concept_name);
                    *error = where_error;
                    return 0;
                }
            }
        }
    }
    for (i = 0; i < program->structs.count; ++i) {
        memset(&self_type, 0, sizeof(self_type));
        self_type.kind = AST_TYPE_NAMED;
        self_type.named_name = program->structs.items[i].name;
        for (j = 0; j < program->structs.items[i].type_params.count; ++j) {
            AstType type_arg;
            memset(&type_arg, 0, sizeof(type_arg));
            type_arg.kind = AST_TYPE_NAMED;
            type_arg.named_name = program->structs.items[i].type_params.items[j];
            type_list_push(&self_type.type_args, type_arg);
        }
        for (j = 0; j < program->structs.items[i].concept_names.count; ++j) {
            const char* concept_name = program->structs.items[i].concept_names.items[j];
            if (!concept_exists(program, concept_name)) {
                snprintf(where_error, sizeof(where_error), "unknown trait on type: %s", concept_name);
                *error = where_error;
                return 0;
            }
        }
        {
            const char* collect_error = 0;
            char* detail_name = 0;
            if (!nominal_concepts_have_unique_method_names(program, &program->structs.items[i].concept_names, &collect_error, &detail_name)) {
                snprintf(where_error, sizeof(where_error), "trait method conflict on type: %s", detail_name);
                *error = where_error;
                return 0;
            }
        }
        {
            AstNominalDeclRef nominal = find_ast_nominal_decl(program, program->structs.items[i].name);
            const char* binding_error = 0;
            char* detail_name = 0;
            if (!validate_nominal_assoc_type_bindings(program, nominal, &binding_error, &detail_name)) {
                snprintf(where_error, sizeof(where_error), "%s: %s", binding_error, detail_name);
                *error = where_error;
                return 0;
            }
        }
        for (j = 0; j < program->structs.items[i].concept_names.count; ++j) {
            const char* concept_name = program->structs.items[i].concept_names.items[j];
            if (!type_has_concept_methods(program, &self_type, find_ast_concept(program, concept_name))) {
                *error = "type does not satisfy declared trait";
                return 0;
            }
        }
        for (j = 0; j < program->structs.items[i].where_constraints.count; ++j) {
            AstWhereConstraint* item = &program->structs.items[i].where_constraints.items[j];
            if (!type_param_exists(&program->structs.items[i].type_params, item->param_name)) {
                *error = "@where parameter is not a generic parameter";
                return 0;
            }
            if (item->kind == AST_WHERE_CONCEPT && !concept_exists(program, item->concept_name)) {
                snprintf(where_error, sizeof(where_error), "unknown trait in @where: %s", item->concept_name);
                *error = where_error;
                return 0;
            }
        }
    }
    for (i = 0; i < program->enums.count; ++i) {
        memset(&self_type, 0, sizeof(self_type));
        self_type.kind = AST_TYPE_NAMED;
        self_type.named_name = program->enums.items[i].name;
        for (j = 0; j < program->enums.items[i].concept_names.count; ++j) {
            const char* concept_name = program->enums.items[i].concept_names.items[j];
            if (!concept_exists(program, concept_name)) {
                snprintf(where_error, sizeof(where_error), "unknown trait on type: %s", concept_name);
                *error = where_error;
                return 0;
            }
        }
        {
            const char* collect_error = 0;
            char* detail_name = 0;
            if (!nominal_concepts_have_unique_method_names(program, &program->enums.items[i].concept_names, &collect_error, &detail_name)) {
                snprintf(where_error, sizeof(where_error), "trait method conflict on type: %s", detail_name);
                *error = where_error;
                return 0;
            }
        }
        {
            AstNominalDeclRef nominal = find_ast_nominal_decl(program, program->enums.items[i].name);
            const char* binding_error = 0;
            char* detail_name = 0;
            if (!validate_nominal_assoc_type_bindings(program, nominal, &binding_error, &detail_name)) {
                snprintf(where_error, sizeof(where_error), "%s: %s", binding_error, detail_name);
                *error = where_error;
                return 0;
            }
        }
        for (j = 0; j < program->enums.items[i].concept_names.count; ++j) {
            const char* concept_name = program->enums.items[i].concept_names.items[j];
            if (!type_has_concept_methods(program, &self_type, find_ast_concept(program, concept_name))) {
                *error = "type does not satisfy declared trait";
                return 0;
            }
        }
    }
    for (i = 0; i < program->unions.count; ++i) {
        memset(&self_type, 0, sizeof(self_type));
        self_type.kind = AST_TYPE_NAMED;
        self_type.named_name = program->unions.items[i].name;
        for (j = 0; j < program->unions.items[i].concept_names.count; ++j) {
            const char* concept_name = program->unions.items[i].concept_names.items[j];
            if (!concept_exists(program, concept_name)) {
                snprintf(where_error, sizeof(where_error), "unknown trait on type: %s", concept_name);
                *error = where_error;
                return 0;
            }
        }
        {
            const char* collect_error = 0;
            char* detail_name = 0;
            if (!nominal_concepts_have_unique_method_names(program, &program->unions.items[i].concept_names, &collect_error, &detail_name)) {
                snprintf(where_error, sizeof(where_error), "trait method conflict on type: %s", detail_name);
                *error = where_error;
                return 0;
            }
        }
        {
            AstNominalDeclRef nominal = find_ast_nominal_decl(program, program->unions.items[i].name);
            const char* binding_error = 0;
            char* detail_name = 0;
            if (!validate_nominal_assoc_type_bindings(program, nominal, &binding_error, &detail_name)) {
                snprintf(where_error, sizeof(where_error), "%s: %s", binding_error, detail_name);
                *error = where_error;
                return 0;
            }
        }
        for (j = 0; j < program->unions.items[i].concept_names.count; ++j) {
            const char* concept_name = program->unions.items[i].concept_names.items[j];
            if (!type_has_concept_methods(program, &self_type, find_ast_concept(program, concept_name))) {
                *error = "type does not satisfy declared trait";
                return 0;
            }
        }
    }
    for (i = 0; i < program->functions.count; ++i) {
        for (j = 0; j < program->functions.items[i].where_constraints.count; ++j) {
            AstWhereConstraint* item = &program->functions.items[i].where_constraints.items[j];
            if (!function_type_param_exists(program, &program->functions.items[i], item->param_name)) {
                *error = "@where parameter is not a generic parameter";
                return 0;
            }
            if (item->kind == AST_WHERE_CONCEPT && !concept_exists(program, item->concept_name)) {
                snprintf(where_error, sizeof(where_error), "unknown trait in @where: %s", item->concept_name);
                *error = where_error;
                return 0;
            }
        }
    }
    return 1;
}

static int check_where_constraints(const AstProgram* program,
                                   const AstWhereConstraintList* where_constraints,
                                   const TypeSubstList* subst,
                                   const char** error) {
    int i = 0;
    for (i = 0; i < where_constraints->count; ++i) {
        const AstType* actual = lookup_subst(subst, where_constraints->items[i].param_name);
        if (!actual) {
            *error = "missing generic substitution for @where";
            return 0;
        }
        if (where_constraints->items[i].kind == AST_WHERE_CONCEPT) {
            if (!ast_type_satisfies_concept(program, actual, where_constraints->items[i].concept_name)) {
                *error = "generic type does not satisfy trait";
                return 0;
            }
        } else if (!ast_type_is_equal(actual, &where_constraints->items[i].equal_type)) {
            *error = "generic type does not satisfy type equality";
            return 0;
        }
    }
    return 1;
}

static char* mangle_type_name(const AstType* type);

static AstType ast_type_copy(const AstType* type) {
    AstType out;
    int i = 0;
    memset(&out, 0, sizeof(out));
    out.kind = type->kind;
    out.mutable_flag = type->mutable_flag;
    if (type->named_name) {
        out.named_name = dup_text(type->named_name);
    }
    for (i = 0; i < type->type_args.count; ++i) {
        type_list_push(&out.type_args, ast_type_copy(&type->type_args.items[i]));
    }
    if (type->array_item) {
        out.array_item = (AstType*)malloc(sizeof(AstType));
        *out.array_item = ast_type_copy(type->array_item);
    }
    if (type->error_type) {
        out.error_type = (AstType*)malloc(sizeof(AstType));
        *out.error_type = ast_type_copy(type->error_type);
    }
    for (i = 0; i < type->tuple_items.count; ++i) {
        type_list_push(&out.tuple_items, ast_type_copy(&type->tuple_items.items[i]));
    }
    out.array_length = type->array_length;
    return canonicalize_ast_type(out);
}

static char* mangle_type_name(const AstType* type) {
    char buffer[64];
    char* out = 0;
    int i = 0;
    switch (type->kind) {
        case AST_TYPE_INT:
            return dup_text("Int");
        case AST_TYPE_I8:
            return dup_text("Int8");
        case AST_TYPE_I16:
            return dup_text("Int16");
        case AST_TYPE_I32:
            return dup_text("Int32");
        case AST_TYPE_I64:
            return dup_text("Int64");
        case AST_TYPE_U8:
            return dup_text("UInt8");
        case AST_TYPE_U16:
            return dup_text("UInt16");
        case AST_TYPE_U32:
            return dup_text("UInt32");
        case AST_TYPE_U64:
            return dup_text("UInt64");
        case AST_TYPE_F16:
            return dup_text("Float16");
        case AST_TYPE_F32:
            return dup_text("Float32");
        case AST_TYPE_F64:
            return dup_text("Float64");
        case AST_TYPE_FLOAT:
            return dup_text("Float");
        case AST_TYPE_DOUBLE:
            return dup_text("Double");
        case AST_TYPE_CHARACTER:
            return dup_text("Char");
        case AST_TYPE_STRING:
            return dup_text("String");
        case AST_TYPE_UINT8:
            return dup_text("UInt8");
        case AST_TYPE_BOOL:
            return dup_text("Bool");
        case AST_TYPE_VOID:
            return dup_text("Void");
        case AST_TYPE_INFER:
            return dup_text("_");
        case AST_TYPE_REFERENCE: {
            char* item = mangle_type_name(type->array_item);
            out = dup_join3(item, "_ref", "");
            free(item);
            return out;
        }
        case AST_TYPE_POINTER: {
            char* item = mangle_type_name(type->array_item);
            out = dup_join3(item, "_ptr", "");
            free(item);
            return out;
        }
        case AST_TYPE_MANY_POINTER: {
            char* item = mangle_type_name(type->array_item);
            out = dup_join3(item, "_manyptr", "");
            free(item);
            return out;
        }
        case AST_TYPE_SLICE: {
            char* item = mangle_type_name(type->array_item);
            out = dup_join3(item, "_slice", "");
            free(item);
            return out;
        }
        case AST_TYPE_OPTIONAL: {
            char* item = mangle_type_name(type->array_item);
            out = dup_join3(item, "_opt", "");
            free(item);
            return out;
        }
        case AST_TYPE_ERRORABLE: {
            char* value = mangle_type_name(type->array_item);
            char* error = mangle_type_name(type->error_type);
            out = dup_join3(value, "_err_", error);
            free(value);
            free(error);
            return out;
        }
        case AST_TYPE_ARRAY: {
            char* item = mangle_type_name(type->array_item);
            snprintf(buffer, sizeof(buffer), "_arr%d", type->array_length);
            out = dup_join3(item, buffer, "");
            free(item);
            return out;
        }
        case AST_TYPE_TUPLE: {
            out = dup_text("Tuple");
            for (i = 0; i < type->tuple_items.count; ++i) {
                char* item = mangle_type_name(&type->tuple_items.items[i]);
                char* next = dup_join3(out, "__", item);
                free(out);
                free(item);
                out = next;
            }
            return out;
        }
        case AST_TYPE_NAMED:
            out = dup_text(type->named_name);
            for (i = 0; i < type->type_args.count; ++i) {
                char* item = mangle_type_name(&type->type_args.items[i]);
                char* next = dup_join3(out, "__", item);
                free(out);
                free(item);
                out = next;
            }
            return out;
    }
    return dup_text("Type");
}

static char* make_instantiated_name(const char* base_name, const AstTypeList* type_args) {
    char* out = dup_text(base_name);
    int i = 0;
    for (i = 0; i < type_args->count; ++i) {
        char* item = mangle_type_name(&type_args->items[i]);
        char* next = dup_join3(out, "__", item);
        free(out);
        free(item);
        out = next;
    }
    return out;
}

static AstType substitute_type(const AstType* type, const TypeSubstList* subst) {
    AstType out;
    const AstType* replaced = 0;
    int i = 0;
    if (type->kind == AST_TYPE_NAMED && type->type_args.count == 0) {
        replaced = lookup_subst(subst, type->named_name);
        if (replaced) {
            AstType copied = ast_type_copy(replaced);
            if (type->mutable_flag) {
                copied.mutable_flag = 1;
            }
            return copied;
        }
    }
    memset(&out, 0, sizeof(out));
    out.kind = type->kind;
    out.mutable_flag = type->mutable_flag;
    if (type->named_name) {
        out.named_name = dup_text(type->named_name);
    }
    for (i = 0; i < type->type_args.count; ++i) {
        type_list_push(&out.type_args, substitute_type(&type->type_args.items[i], subst));
    }
    if (type->array_item) {
        out.array_item = (AstType*)malloc(sizeof(AstType));
        *out.array_item = substitute_type(type->array_item, subst);
    }
    for (i = 0; i < type->tuple_items.count; ++i) {
        type_list_push(&out.tuple_items, substitute_type(&type->tuple_items.items[i], subst));
    }
    out.array_length = type->array_length;
    return canonicalize_ast_type(out);
}

static int ast_type_is_equal(const AstType* left, const AstType* right) {
    int i = 0;
    if (left->kind != right->kind || left->mutable_flag != right->mutable_flag) {
        return 0;
    }
    if ((left->named_name || right->named_name) &&
        (!left->named_name || !right->named_name || strcmp(left->named_name, right->named_name) != 0)) {
        return 0;
    }
    if (left->array_length != right->array_length ||
        left->type_args.count != right->type_args.count ||
        left->tuple_items.count != right->tuple_items.count) {
        return 0;
    }
    if ((left->array_item == 0) != (right->array_item == 0)) {
        return 0;
    }
    if (left->array_item && !ast_type_is_equal(left->array_item, right->array_item)) {
        return 0;
    }
    for (i = 0; i < left->type_args.count; ++i) {
        if (!ast_type_is_equal(&left->type_args.items[i], &right->type_args.items[i])) {
            return 0;
        }
    }
    for (i = 0; i < left->tuple_items.count; ++i) {
        if (!ast_type_is_equal(&left->tuple_items.items[i], &right->tuple_items.items[i])) {
            return 0;
        }
    }
    return 1;
}

static const AstFunction* find_ast_function_exact(const AstProgram* program, const char* name) {
    return find_ast_function(program, name);
}

typedef struct LocalTypeEntry {
    const char* name;
    AstType type;
} LocalTypeEntry;

typedef struct LocalTypeList {
    LocalTypeEntry* items;
    int count;
    int capacity;
} LocalTypeList;

#define local_type_list_push(list, item) VEC_PUSH((list), (item))

static const AstType* lookup_local_type(const LocalTypeList* locals, const char* name) {
    int i = 0;
    for (i = locals->count - 1; i >= 0; --i) {
        if (strcmp(locals->items[i].name, name) == 0) {
            return &locals->items[i].type;
        }
    }
    return 0;
}

static AstType infer_expr_type(const AstProgram* program, const LocalTypeList* locals, const AstExpr* expr) {
    AstType out;
    memset(&out, 0, sizeof(out));
    out.kind = AST_TYPE_VOID;
    switch (expr->kind) {
        case AST_EXPR_INT:
            out.kind = AST_TYPE_INT;
            return out;
        case AST_EXPR_FLOAT:
            out.kind = AST_TYPE_DOUBLE;
            return out;
        case AST_EXPR_CHAR:
            out.kind = AST_TYPE_CHARACTER;
            return out;
        case AST_EXPR_BOOL:
            out.kind = AST_TYPE_BOOL;
            return out;
        case AST_EXPR_STRING:
            out.kind = AST_TYPE_ARRAY;
            out.array_length = expr->as.string_lit.length;
            out.array_item = (AstType*)calloc(1, sizeof(AstType));
            if (out.array_item) {
                out.array_item->kind = AST_TYPE_UINT8;
            }
            return out;
        case AST_EXPR_NAME: {
            const AstType* local = lookup_local_type(locals, expr->as.name);
            if (local) {
                return ast_type_copy(local);
            }
            {
                const AstGlobal* global = find_ast_global(program, expr->as.name);
                if (global) {
                    return ast_type_copy(&global->type);
                }
            }
            return out;
        }
        case AST_EXPR_STRUCT:
            if (expr->as.struct_lit.type_name) {
                out.kind = AST_TYPE_NAMED;
                out.named_name = dup_text(expr->as.struct_lit.type_name);
            }
            return out;
        case AST_EXPR_CALL: {
            const AstFunction* fn = find_ast_function_exact(program, expr->as.call.callee);
            if (fn) {
                return ast_type_copy(&fn->return_type);
            }
            return out;
        }
        case AST_EXPR_FIELD: {
            AstType base = infer_expr_type(program, locals, expr->as.field.base);
            const AstStructDecl* st = 0;
            int i = 0;
            if (base.kind == AST_TYPE_NAMED) {
                st = find_ast_struct(program, base.named_name);
                if (st) {
                    for (i = 0; i < st->fields.count; ++i) {
                        if (strcmp(st->fields.items[i].name, expr->as.field.name) == 0) {
                            return ast_type_copy(&st->fields.items[i].type);
                        }
                    }
                }
            }
            return out;
        }
        default:
            return out;
    }
}

static int transform_type(MonoContext* mono, AstType* type);
static int transform_expr(MonoContext* mono, AstExpr* expr, LocalTypeList* locals);
static int instantiate_struct_template(MonoContext* mono, const AstStructDecl* templ, const AstTypeList* type_args, char** instantiated_name);
static int instantiate_union_template(MonoContext* mono, const AstUnionDecl* templ, const AstTypeList* type_args, char** instantiated_name);
static int instantiate_function_template(MonoContext* mono, const AstFunction* templ, const AstTypeList* type_args, char** instantiated_name);

static int copy_subst_from_template(TypeSubstList* subst, const AstNameList* type_params, const AstTypeList* type_args) {
    int i = 0;
    if (type_params->count != type_args->count) {
        return 0;
    }
    for (i = 0; i < type_params->count; ++i) {
        TypeSubst item;
        item.name = type_params->items[i];
        item.type = ast_type_copy(&type_args->items[i]);
        subst_list_push(subst, item);
    }
    return 1;
}

static int transform_type(MonoContext* mono, AstType* type) {
    int i = 0;
    if (type->array_item && !transform_type(mono, type->array_item)) {
        return 0;
    }
    if (type->error_type && !transform_type(mono, type->error_type)) {
        return 0;
    }
    for (i = 0; i < type->tuple_items.count; ++i) {
        if (!transform_type(mono, &type->tuple_items.items[i])) {
            return 0;
        }
    }
    for (i = 0; i < type->type_args.count; ++i) {
        if (!transform_type(mono, &type->type_args.items[i])) {
            return 0;
        }
    }
    if (type->kind == AST_TYPE_NAMED && type->type_args.count > 0) {
        const AstUnionDecl* union_templ = 0;
        if (type->named_name && strcmp(type->named_name, "Fn") == 0) {
            return 1;
        }
        char* instantiated_name = 0;
        const AstStructDecl* templ = find_generic_struct_template(mono->source, type->named_name);
        union_templ = find_generic_union_template(mono->source, type->named_name);
        if (!templ && !union_templ) {
            mono->error = "unknown generic type";
            return 0;
        }
        if (templ) {
            if (!instantiate_struct_template(mono, templ, &type->type_args, &instantiated_name)) {
                return 0;
            }
        } else {
            if (!instantiate_union_template(mono, union_templ, &type->type_args, &instantiated_name)) {
                return 0;
            }
        }
        free(type->named_name);
        type->named_name = instantiated_name;
        memset(&type->type_args, 0, sizeof(type->type_args));
    }
    return 1;
}

static AstExpr* clone_expr_subst(const AstExpr* expr, const TypeSubstList* subst);
static AstBindingPattern* clone_binding_pattern_subst(const AstBindingPattern* pattern, const TypeSubstList* subst);
static AstStmt* clone_stmt_subst(const AstStmt* stmt, const TypeSubstList* subst);

static AstType clone_type_subst(const AstType* type, const TypeSubstList* subst) {
    AstType out = substitute_type(type, subst);
    int i = 0;
    if (out.array_item) {
        AstType tmp = clone_type_subst(out.array_item, subst);
        free(out.array_item);
        out.array_item = (AstType*)malloc(sizeof(AstType));
        *out.array_item = tmp;
    }
    if (out.error_type) {
        AstType tmp = clone_type_subst(out.error_type, subst);
        free(out.error_type);
        out.error_type = (AstType*)malloc(sizeof(AstType));
        *out.error_type = tmp;
    }
    for (i = 0; i < out.type_args.count; ++i) {
        out.type_args.items[i] = clone_type_subst(&out.type_args.items[i], subst);
    }
    for (i = 0; i < out.tuple_items.count; ++i) {
        out.tuple_items.items[i] = clone_type_subst(&out.tuple_items.items[i], subst);
    }
    return out;
}

static AstExpr* clone_expr_subst(const AstExpr* expr, const TypeSubstList* subst) {
    if (!expr) {
        return 0;
    }
    AstExpr* out = (AstExpr*)calloc(1, sizeof(AstExpr));
    int i = 0;
    out->kind = expr->kind;
    out->line = expr->line;
    switch (expr->kind) {
        case AST_EXPR_INT:
            out->as.int_value = expr->as.int_value;
            break;
        case AST_EXPR_FLOAT:
            out->as.float_value = expr->as.float_value;
            break;
        case AST_EXPR_CHAR:
            out->as.char_value = expr->as.char_value;
            break;
        case AST_EXPR_BOOL:
            out->as.bool_value = expr->as.bool_value;
            break;
        case AST_EXPR_NULL:
            break;
        case AST_EXPR_IMPLICIT:
            out->as.implicit.target_is_type = expr->as.implicit.target_is_type;
            if (expr->as.implicit.target_is_type) {
                out->as.implicit.type_target = clone_type_subst(&expr->as.implicit.type_target, subst);
            } else {
                out->as.implicit.value_target = clone_expr_subst(expr->as.implicit.value_target, subst);
            }
            out->as.implicit.member = dup_text(expr->as.implicit.member);
            out->as.implicit.has_type_arg = expr->as.implicit.has_type_arg;
            if (expr->as.implicit.has_type_arg) {
                out->as.implicit.type_arg = clone_type_subst(&expr->as.implicit.type_arg, subst);
            }
            for (i = 0; i < expr->as.implicit.args.count; ++i) {
                expr_list_push(&out->as.implicit.args, clone_expr_subst(expr->as.implicit.args.items[i], subst));
            }
            break;
        case AST_EXPR_SIZE_OF:
            out->as.size_of_type = clone_type_subst(&expr->as.size_of_type, subst);
            break;
        case AST_EXPR_STRING:
            out->as.string_lit.text = dup_text(expr->as.string_lit.text);
            out->as.string_lit.length = expr->as.string_lit.length;
            break;
        case AST_EXPR_NAME:
            out->as.name = dup_text(expr->as.name);
            break;
        case AST_EXPR_ADDR:
        case AST_EXPR_DEREF:
        case AST_EXPR_NEW:
        case AST_EXPR_FREE:
        case AST_EXPR_BIT_NOT:
            out->as.unary.value = clone_expr_subst(expr->as.unary.value, subst);
            break;
        case AST_EXPR_BINARY:
            out->as.binary.op = expr->as.binary.op;
            out->as.binary.left = clone_expr_subst(expr->as.binary.left, subst);
            out->as.binary.right = clone_expr_subst(expr->as.binary.right, subst);
            break;
        case AST_EXPR_COALESCE:
            out->as.coalesce.left = clone_expr_subst(expr->as.coalesce.left, subst);
            out->as.coalesce.right = clone_expr_subst(expr->as.coalesce.right, subst);
            break;
        case AST_EXPR_CATCH_FALLBACK:
            out->as.catch_fallback.left = clone_expr_subst(expr->as.catch_fallback.left, subst);
            out->as.catch_fallback.fallback = clone_expr_subst(expr->as.catch_fallback.fallback, subst);
            break;
        case AST_EXPR_CATCH_HANDLER:
            out->as.catch_handler.left = clone_expr_subst(expr->as.catch_handler.left, subst);
            out->as.catch_handler.binding_name = dup_text(expr->as.catch_handler.binding_name);
            out->as.catch_handler.handler = clone_expr_subst(expr->as.catch_handler.handler, subst);
            break;
        case AST_EXPR_BLOCK:
            out->as.block_expr.body = (AstBlock*)calloc(1, sizeof(AstBlock));
            if (out->as.block_expr.body) {
                for (i = 0; i < expr->as.block_expr.body->stmts.count; ++i) {
                    stmt_list_push(&out->as.block_expr.body->stmts, clone_stmt_subst(expr->as.block_expr.body->stmts.items[i], subst));
                }
            }
            out->as.block_expr.value = clone_expr_subst(expr->as.block_expr.value, subst);
            break;
        case AST_EXPR_IF:
            out->as.if_expr.cond = clone_expr_subst(expr->as.if_expr.cond, subst);
            out->as.if_expr.then_expr = clone_expr_subst(expr->as.if_expr.then_expr, subst);
            out->as.if_expr.else_expr = clone_expr_subst(expr->as.if_expr.else_expr, subst);
            break;
        case AST_EXPR_SWITCH:
            out->as.switch_expr.value = clone_expr_subst(expr->as.switch_expr.value, subst);
            for (i = 0; i < expr->as.switch_expr.cases.count; ++i) {
                AstSwitchExprCase item;
                memset(&item, 0, sizeof(item));
                item.pattern = clone_expr_subst(expr->as.switch_expr.cases.items[i].pattern, subst);
                item.value = clone_expr_subst(expr->as.switch_expr.cases.items[i].value, subst);
                item.is_else = expr->as.switch_expr.cases.items[i].is_else;
                switch_expr_case_list_push(&out->as.switch_expr.cases, item);
            }
            break;
        case AST_EXPR_TRY:
            out->as.try_expr.value = clone_expr_subst(expr->as.try_expr.value, subst);
            for (i = 0; i < expr->as.try_expr.catches.count; ++i) {
                AstExprTryCatch item;
                memset(&item, 0, sizeof(item));
                item.error_type = clone_type_subst(&expr->as.try_expr.catches.items[i].error_type, subst);
                item.binding_name = dup_text(expr->as.try_expr.catches.items[i].binding_name);
                item.value = clone_expr_subst(expr->as.try_expr.catches.items[i].value, subst);
                item.line = expr->as.try_expr.catches.items[i].line;
                expr_try_catch_list_push(&out->as.try_expr.catches, item);
            }
            break;
        case AST_EXPR_COALESCE_CONTROL:
            out->as.coalesce_control.left = clone_expr_subst(expr->as.coalesce_control.left, subst);
            out->as.coalesce_control.control = expr->as.coalesce_control.control;
            out->as.coalesce_control.return_expr = clone_expr_subst(expr->as.coalesce_control.return_expr, subst);
            break;
        case AST_EXPR_TERNARY:
            out->as.ternary.cond = clone_expr_subst(expr->as.ternary.cond, subst);
            out->as.ternary.then_expr = clone_expr_subst(expr->as.ternary.then_expr, subst);
            out->as.ternary.else_expr = clone_expr_subst(expr->as.ternary.else_expr, subst);
            break;
        case AST_EXPR_CALL:
            out->as.call.callee = dup_text(expr->as.call.callee);
            for (i = 0; i < expr->as.call.type_args.count; ++i) {
                type_list_push(&out->as.call.type_args, clone_type_subst(&expr->as.call.type_args.items[i], subst));
            }
            for (i = 0; i < expr->as.call.args.count; ++i) {
                AstStructFieldInit arg;
                memset(&arg, 0, sizeof(arg));
                arg.name = expr->as.call.args.items[i].name ? dup_text(expr->as.call.args.items[i].name) : 0;
                arg.value = clone_expr_subst(expr->as.call.args.items[i].value, subst);
                arg.line = expr->as.call.args.items[i].line;
                struct_field_init_list_push(&out->as.call.args, arg);
            }
            break;
        case AST_EXPR_VARIANT:
            out->as.variant.union_name = expr->as.variant.union_name ? dup_text(expr->as.variant.union_name) : 0;
            out->as.variant.variant_name = dup_text(expr->as.variant.variant_name);
            out->as.variant.pattern_flag = expr->as.variant.pattern_flag;
            out->as.variant.payload = clone_expr_subst(expr->as.variant.payload, subst);
            break;
        case AST_EXPR_FIELD:
        case AST_EXPR_OPTIONAL_FIELD:
            out->as.field.base = clone_expr_subst(expr->as.field.base, subst);
            out->as.field.name = dup_text(expr->as.field.name);
            break;
        case AST_EXPR_STRUCT:
            out->as.struct_lit.type_name = expr->as.struct_lit.type_name ? dup_text(expr->as.struct_lit.type_name) : 0;
            for (i = 0; i < expr->as.struct_lit.type_args.count; ++i) {
                type_list_push(&out->as.struct_lit.type_args, clone_type_subst(&expr->as.struct_lit.type_args.items[i], subst));
            }
            for (i = 0; i < expr->as.struct_lit.fields.count; ++i) {
                AstStructFieldInit init = expr->as.struct_lit.fields.items[i];
                init.name = dup_text(init.name);
                init.value = clone_expr_subst(init.value, subst);
                struct_field_init_list_push(&out->as.struct_lit.fields, init);
            }
            break;
        case AST_EXPR_TUPLE:
            for (i = 0; i < expr->as.tuple.items.count; ++i) {
                expr_list_push(&out->as.tuple.items, clone_expr_subst(expr->as.tuple.items.items[i], subst));
            }
            break;
        case AST_EXPR_ARRAY:
            for (i = 0; i < expr->as.array.items.count; ++i) {
                expr_list_push(&out->as.array.items, clone_expr_subst(expr->as.array.items.items[i], subst));
            }
            break;
        case AST_EXPR_INDEX:
        case AST_EXPR_OPTIONAL_INDEX:
            out->as.index.base = clone_expr_subst(expr->as.index.base, subst);
            out->as.index.index = clone_expr_subst(expr->as.index.index, subst);
            break;
        case AST_EXPR_SLICE_LENGTH:
            out->as.slice_length.base = clone_expr_subst(expr->as.slice_length.base, subst);
            break;
    }
    return out;
}

static AstBindingPattern* clone_binding_pattern_subst(const AstBindingPattern* pattern, const TypeSubstList* subst) {
    AstBindingPattern* out = (AstBindingPattern*)calloc(1, sizeof(AstBindingPattern));
    int i = 0;
    out->kind = pattern->kind;
    out->line = pattern->line;
    out->type = clone_type_subst(&pattern->type, subst);
    if (pattern->name) {
        out->name = dup_text(pattern->name);
    }
    for (i = 0; i < pattern->items.count; ++i) {
        binding_pattern_list_push(&out->items, clone_binding_pattern_subst(pattern->items.items[i], subst));
    }
    return out;
}

static AstStmt* clone_stmt_subst(const AstStmt* stmt, const TypeSubstList* subst) {
    AstStmt* out = (AstStmt*)calloc(1, sizeof(AstStmt));
    int i = 0;
    out->kind = stmt->kind;
    out->line = stmt->line;
    switch (stmt->kind) {
        case AST_STMT_RETURN:
            out->as.ret.expr = clone_expr_subst(stmt->as.ret.expr, subst);
            break;
        case AST_STMT_VAR_DECL:
            out->as.var_decl.type = clone_type_subst(&stmt->as.var_decl.type, subst);
            out->as.var_decl.name = dup_text(stmt->as.var_decl.name);
            out->as.var_decl.init = clone_expr_subst(stmt->as.var_decl.init, subst);
            break;
        case AST_STMT_GROUP:
            for (i = 0; i < stmt->as.group_stmt.stmts.count; ++i) {
                stmt_list_push(&out->as.group_stmt.stmts, clone_stmt_subst(stmt->as.group_stmt.stmts.items[i], subst));
            }
            break;
        case AST_STMT_ASSIGN:
            out->as.assign.target = clone_expr_subst(stmt->as.assign.target, subst);
            out->as.assign.value = clone_expr_subst(stmt->as.assign.value, subst);
            break;
        case AST_STMT_IF:
            out->as.if_stmt.cond = clone_expr_subst(stmt->as.if_stmt.cond, subst);
            out->as.if_stmt.has_else = stmt->as.if_stmt.has_else;
            for (i = 0; i < stmt->as.if_stmt.then_block.stmts.count; ++i) {
                stmt_list_push(&out->as.if_stmt.then_block.stmts, clone_stmt_subst(stmt->as.if_stmt.then_block.stmts.items[i], subst));
            }
            for (i = 0; i < stmt->as.if_stmt.else_block.stmts.count; ++i) {
                stmt_list_push(&out->as.if_stmt.else_block.stmts, clone_stmt_subst(stmt->as.if_stmt.else_block.stmts.items[i], subst));
            }
            break;
        case AST_STMT_SWITCH:
            out->as.switch_stmt.value = clone_expr_subst(stmt->as.switch_stmt.value, subst);
            for (i = 0; i < stmt->as.switch_stmt.cases.count; ++i) {
                AstSwitchCase item;
                int j = 0;
                memset(&item, 0, sizeof(item));
                item.pattern = clone_expr_subst(stmt->as.switch_stmt.cases.items[i].pattern, subst);
                item.is_else = stmt->as.switch_stmt.cases.items[i].is_else;
                item.binding_name = stmt->as.switch_stmt.cases.items[i].binding_name
                    ? dup_text(stmt->as.switch_stmt.cases.items[i].binding_name)
                    : 0;
                item.result_case_kind = stmt->as.switch_stmt.cases.items[i].result_case_kind;
                for (j = 0; j < stmt->as.switch_stmt.cases.items[i].body.stmts.count; ++j) {
                    stmt_list_push(&item.body.stmts, clone_stmt_subst(stmt->as.switch_stmt.cases.items[i].body.stmts.items[j], subst));
                }
                switch_case_list_push(&out->as.switch_stmt.cases, item);
            }
            break;
        case AST_STMT_TRY:
            for (i = 0; i < stmt->as.try_stmt.try_body.stmts.count; ++i) {
                stmt_list_push(&out->as.try_stmt.try_body.stmts, clone_stmt_subst(stmt->as.try_stmt.try_body.stmts.items[i], subst));
            }
            for (i = 0; i < stmt->as.try_stmt.catches.count; ++i) {
                AstTryCatch item;
                int j = 0;
                memset(&item, 0, sizeof(item));
                item.error_type = clone_type_subst(&stmt->as.try_stmt.catches.items[i].error_type, subst);
                item.binding_name = dup_text(stmt->as.try_stmt.catches.items[i].binding_name);
                item.line = stmt->as.try_stmt.catches.items[i].line;
                for (j = 0; j < stmt->as.try_stmt.catches.items[i].body.stmts.count; ++j) {
                    stmt_list_push(&item.body.stmts, clone_stmt_subst(stmt->as.try_stmt.catches.items[i].body.stmts.items[j], subst));
                }
                try_catch_list_push(&out->as.try_stmt.catches, item);
            }
            break;
        case AST_STMT_WHILE:
            out->as.while_stmt.cond = clone_expr_subst(stmt->as.while_stmt.cond, subst);
            for (i = 0; i < stmt->as.while_stmt.body.stmts.count; ++i) {
                stmt_list_push(&out->as.while_stmt.body.stmts, clone_stmt_subst(stmt->as.while_stmt.body.stmts.items[i], subst));
            }
            break;
        case AST_STMT_FOR_RANGE:
            out->as.for_range.type = clone_type_subst(&stmt->as.for_range.type, subst);
            out->as.for_range.name = dup_text(stmt->as.for_range.name);
            out->as.for_range.start = clone_expr_subst(stmt->as.for_range.start, subst);
            out->as.for_range.end = clone_expr_subst(stmt->as.for_range.end, subst);
            for (i = 0; i < stmt->as.for_range.body.stmts.count; ++i) {
                stmt_list_push(&out->as.for_range.body.stmts, clone_stmt_subst(stmt->as.for_range.body.stmts.items[i], subst));
            }
            break;
        case AST_STMT_FOR_EACH:
            out->as.for_each.pattern = clone_binding_pattern_subst(stmt->as.for_each.pattern, subst);
            out->as.for_each.iterable = clone_expr_subst(stmt->as.for_each.iterable, subst);
            out->as.for_each.indexed_flag = stmt->as.for_each.indexed_flag;
            for (i = 0; i < stmt->as.for_each.body.stmts.count; ++i) {
                stmt_list_push(&out->as.for_each.body.stmts, clone_stmt_subst(stmt->as.for_each.body.stmts.items[i], subst));
            }
            break;
        case AST_STMT_BREAK:
        case AST_STMT_CONTINUE:
            break;
        case AST_STMT_DEFER:
            for (i = 0; i < stmt->as.defer_stmt.body.stmts.count; ++i) {
                stmt_list_push(&out->as.defer_stmt.body.stmts, clone_stmt_subst(stmt->as.defer_stmt.body.stmts.items[i], subst));
            }
            break;
        case AST_STMT_EXPR:
            out->as.expr_stmt.expr = clone_expr_subst(stmt->as.expr_stmt.expr, subst);
            break;
        case AST_STMT_EXPR_CATCH:
            out->as.expr_catch_stmt.expr = clone_expr_subst(stmt->as.expr_catch_stmt.expr, subst);
            out->as.expr_catch_stmt.binding_name = dup_text(stmt->as.expr_catch_stmt.binding_name);
            for (i = 0; i < stmt->as.expr_catch_stmt.body.stmts.count; ++i) {
                stmt_list_push(&out->as.expr_catch_stmt.body.stmts, clone_stmt_subst(stmt->as.expr_catch_stmt.body.stmts.items[i], subst));
            }
            break;
        case AST_STMT_THROW:
            out->as.throw_stmt.expr = clone_expr_subst(stmt->as.throw_stmt.expr, subst);
            break;
        case AST_STMT_DESTRUCTURE:
            for (i = 0; i < stmt->as.destructure.bindings.count; ++i) {
                AstParam binding = stmt->as.destructure.bindings.items[i];
                binding.type = clone_type_subst(&binding.type, subst);
                binding.label = binding.label ? dup_text(binding.label) : 0;
                binding.name = dup_text(binding.name);
                binding.default_value = clone_expr_subst(binding.default_value, subst);
                param_list_push(&out->as.destructure.bindings, binding);
            }
            out->as.destructure.init = clone_expr_subst(stmt->as.destructure.init, subst);
            break;
    }
    return out;
}

static int transform_block(MonoContext* mono, AstBlock* block, LocalTypeList* locals);

static int transform_stmt(MonoContext* mono, AstStmt* stmt, LocalTypeList* locals) {
    int i = 0;
    switch (stmt->kind) {
        case AST_STMT_RETURN:
            return !stmt->as.ret.expr || transform_expr(mono, stmt->as.ret.expr, locals);
        case AST_STMT_VAR_DECL: {
            LocalTypeEntry entry;
            if (!transform_type(mono, &stmt->as.var_decl.type) ||
                !transform_expr(mono, stmt->as.var_decl.init, locals)) {
                return 0;
            }
            entry.name = stmt->as.var_decl.name;
            entry.type = ast_type_copy(&stmt->as.var_decl.type);
            local_type_list_push(locals, entry);
            return 1;
        }
        case AST_STMT_GROUP:
            return transform_block(mono, &stmt->as.group_stmt, locals);
        case AST_STMT_ASSIGN:
            return transform_expr(mono, stmt->as.assign.target, locals) &&
                   transform_expr(mono, stmt->as.assign.value, locals);
        case AST_STMT_IF:
            return transform_expr(mono, stmt->as.if_stmt.cond, locals) &&
                   transform_block(mono, &stmt->as.if_stmt.then_block, locals) &&
                   (!stmt->as.if_stmt.has_else || transform_block(mono, &stmt->as.if_stmt.else_block, locals));
        case AST_STMT_SWITCH:
            if (!transform_expr(mono, stmt->as.switch_stmt.value, locals)) {
                return 0;
            }
            for (i = 0; i < stmt->as.switch_stmt.cases.count; ++i) {
                if (stmt->as.switch_stmt.cases.items[i].pattern &&
                    !transform_expr(mono, stmt->as.switch_stmt.cases.items[i].pattern, locals)) {
                    return 0;
                }
                if (!transform_block(mono, &stmt->as.switch_stmt.cases.items[i].body, locals)) {
                    return 0;
                }
            }
            return 1;
        case AST_STMT_TRY:
            if (!transform_block(mono, &stmt->as.try_stmt.try_body, locals)) {
                return 0;
            }
            for (i = 0; i < stmt->as.try_stmt.catches.count; ++i) {
                if (!transform_type(mono, &stmt->as.try_stmt.catches.items[i].error_type) ||
                    !transform_block(mono, &stmt->as.try_stmt.catches.items[i].body, locals)) {
                    return 0;
                }
            }
            return 1;
        case AST_STMT_WHILE:
            return transform_expr(mono, stmt->as.while_stmt.cond, locals) &&
                   transform_block(mono, &stmt->as.while_stmt.body, locals);
        case AST_STMT_FOR_RANGE:
            return transform_type(mono, &stmt->as.for_range.type) &&
                   transform_expr(mono, stmt->as.for_range.start, locals) &&
                   transform_expr(mono, stmt->as.for_range.end, locals) &&
                   transform_block(mono, &stmt->as.for_range.body, locals);
        case AST_STMT_FOR_EACH:
            return transform_expr(mono, stmt->as.for_each.iterable, locals) &&
                   transform_block(mono, &stmt->as.for_each.body, locals);
        case AST_STMT_DEFER:
            return transform_block(mono, &stmt->as.defer_stmt.body, locals);
        case AST_STMT_EXPR:
            return transform_expr(mono, stmt->as.expr_stmt.expr, locals);
        case AST_STMT_EXPR_CATCH:
            return transform_expr(mono, stmt->as.expr_catch_stmt.expr, locals) &&
                   transform_block(mono, &stmt->as.expr_catch_stmt.body, locals);
        case AST_STMT_THROW:
            return transform_expr(mono, stmt->as.throw_stmt.expr, locals);
        case AST_STMT_DESTRUCTURE:
            for (i = 0; i < stmt->as.destructure.bindings.count; ++i) {
                if (!transform_type(mono, &stmt->as.destructure.bindings.items[i].type)) {
                    return 0;
                }
            }
            return transform_expr(mono, stmt->as.destructure.init, locals);
        case AST_STMT_BREAK:
        case AST_STMT_CONTINUE:
            return 1;
    }
    return 1;
}

static int transform_block(MonoContext* mono, AstBlock* block, LocalTypeList* locals) {
    int i = 0;
    for (i = 0; i < block->stmts.count; ++i) {
        if (!transform_stmt(mono, block->stmts.items[i], locals)) {
            return 0;
        }
    }
    return 1;
}

static int infer_type_args_from_call(const AstProgram* program, const AstFunction* templ, const AstExpr* call, const LocalTypeList* locals, AstTypeList* out_args) {
    int i = 0;
    for (i = 0; i < templ->type_params.count; ++i) {
        int j = 0;
        int found = 0;
        for (j = 0; j < templ->params.count; ++j) {
            if (templ->params.items[j].type.kind == AST_TYPE_NAMED &&
                templ->params.items[j].type.type_args.count == 0 &&
                strcmp(templ->params.items[j].type.named_name, templ->type_params.items[i]) == 0) {
                AstType actual = infer_expr_type(program, locals, call->as.call.args.items[j].value);
                if (actual.kind == AST_TYPE_VOID) {
                    return 0;
                }
                type_list_push(out_args, actual);
                found = 1;
                break;
            }
        }
        if (!found) {
            return 0;
        }
    }
    return 1;
}

static int instantiate_function_template(MonoContext* mono, const AstFunction* templ, const AstTypeList* type_args, char** instantiated_name) {
    AstFunction fn;
    TypeSubstList subst;
    LocalTypeList locals;
    int i = 0;
    memset(&subst, 0, sizeof(subst));
    memset(&locals, 0, sizeof(locals));
    *instantiated_name = make_instantiated_name(templ->name, type_args);
    if (find_ast_function(mono->out, *instantiated_name)) {
        return 1;
    }
    if (!copy_subst_from_template(&subst, &templ->type_params, type_args)) {
        mono->error = "generic function type argument mismatch";
        return 0;
    }
    if (!check_generic_mutability_constraints(&templ->type_params, &templ->where_constraints, &subst, &mono->error)) {
        return 0;
    }
    if (!check_where_constraints(mono->source, &templ->where_constraints, &subst, &mono->error)) {
        return 0;
    }
    memset(&fn, 0, sizeof(fn));
    fn.return_type = clone_type_subst(&templ->return_type, &subst);
    fn.name = dup_text(*instantiated_name);
    fn.public_flag = templ->public_flag;
    fn.struct_init_flag = templ->struct_init_flag;
    fn.method_flag = templ->method_flag;
    fn.static_method_flag = templ->static_method_flag;
    fn.owner_type_name = templ->owner_type_name ? dup_text(templ->owner_type_name) : 0;
    fn.line = templ->line;
    for (i = 0; i < templ->params.count; ++i) {
        AstParam param = templ->params.items[i];
        LocalTypeEntry entry;
        param.type = clone_type_subst(&param.type, &subst);
        param.label = param.label ? dup_text(param.label) : 0;
        param.name = dup_text(param.name);
        param.default_value = clone_expr_subst(param.default_value, &subst);
        param_list_push(&fn.params, param);
        entry.name = param.name;
        entry.type = ast_type_copy(&param.type);
        local_type_list_push(&locals, entry);
    }
    for (i = 0; i < templ->body.stmts.count; ++i) {
        stmt_list_push(&fn.body.stmts, clone_stmt_subst(templ->body.stmts.items[i], &subst));
    }
    if (!transform_type(mono, &fn.return_type) || !transform_block(mono, &fn.body, &locals)) {
        return 0;
    }
    function_list_push(&mono->out->functions, fn);
    return 1;
}

static int instantiate_struct_template(MonoContext* mono, const AstStructDecl* templ, const AstTypeList* type_args, char** instantiated_name) {
    AstStructDecl decl;
    TypeSubstList subst;
    int i = 0;
    memset(&subst, 0, sizeof(subst));
    *instantiated_name = make_instantiated_name(templ->name, type_args);
    if (find_ast_struct(mono->out, *instantiated_name)) {
        return 1;
    }
    if (!copy_subst_from_template(&subst, &templ->type_params, type_args)) {
        mono->error = "generic struct type argument mismatch";
        return 0;
    }
    if (!check_generic_mutability_constraints(&templ->type_params, &templ->where_constraints, &subst, &mono->error)) {
        return 0;
    }
    if (!check_where_constraints(mono->source, &templ->where_constraints, &subst, &mono->error)) {
        return 0;
    }
    memset(&decl, 0, sizeof(decl));
    decl.name = dup_text(*instantiated_name);
    decl.public_flag = templ->public_flag;
    decl.record_flag = templ->record_flag;
    decl.has_deinit = templ->has_deinit;
    decl.deinit_line = templ->deinit_line;
    decl.line = templ->line;
    for (i = 0; i < templ->concept_names.count; ++i) {
        name_list_push(&decl.concept_names, dup_text(templ->concept_names.items[i]));
    }
    for (i = 0; i < templ->assoc_type_bindings.count; ++i) {
        AstAssocTypeBinding binding;
        memset(&binding, 0, sizeof(binding));
        clone_name_list(&binding.context_concept_names, &templ->assoc_type_bindings.items[i].context_concept_names);
        binding.concept_name = templ->assoc_type_bindings.items[i].concept_name
            ? dup_text(templ->assoc_type_bindings.items[i].concept_name)
            : 0;
        binding.name = dup_text(templ->assoc_type_bindings.items[i].name);
        binding.value = clone_type_subst(&templ->assoc_type_bindings.items[i].value, &subst);
        binding.line = templ->assoc_type_bindings.items[i].line;
        assoc_type_binding_list_push(&decl.assoc_type_bindings, binding);
    }
    for (i = 0; i < templ->fields.count; ++i) {
        AstStructField field = templ->fields.items[i];
        field.type = clone_type_subst(&field.type, &subst);
        field.name = dup_text(field.name);
        field.default_value = clone_expr_subst(field.default_value, &subst);
        struct_field_list_push(&decl.fields, field);
    }
    for (i = 0; i < templ->init_overloads.count; ++i) {
        AstStructInitDecl init_decl;
        int j = 0;
        memset(&init_decl, 0, sizeof(init_decl));
        init_decl.line = templ->init_overloads.items[i].line;
        for (j = 0; j < templ->init_overloads.items[i].params.count; ++j) {
            AstParam param = templ->init_overloads.items[i].params.items[j];
            param.type = clone_type_subst(&param.type, &subst);
            param.label = param.label ? dup_text(param.label) : 0;
            param.name = dup_text(param.name);
            param.default_value = clone_expr_subst(param.default_value, &subst);
            param_list_push(&init_decl.params, param);
        }
        for (j = 0; j < templ->init_overloads.items[i].body.stmts.count; ++j) {
            stmt_list_push(&init_decl.body.stmts, clone_stmt_subst(templ->init_overloads.items[i].body.stmts.items[j], &subst));
        }
        if (!transform_block(mono, &init_decl.body, &(LocalTypeList){0})) {
            return 0;
        }
        struct_init_decl_list_push(&decl.init_overloads, init_decl);
    }
    for (i = 0; i < templ->deinit_body.stmts.count; ++i) {
        stmt_list_push(&decl.deinit_body.stmts, clone_stmt_subst(templ->deinit_body.stmts.items[i], &subst));
    }
    if (!transform_block(mono, &decl.deinit_body, &(LocalTypeList){0})) {
        return 0;
    }
    struct_list_push(&mono->out->structs, decl);
    for (i = 0; i < mono->source->functions.count; ++i) {
        const AstFunction* method = &mono->source->functions.items[i];
        if (method->method_flag && method->owner_type_name && strcmp(method->owner_type_name, templ->name) == 0) {
            AstTypeList empty_args;
            AstFunction cloned = clone_function(mono->source, 0, 0, method, method->public_flag);
            const char* method_error = 0;
            if (!check_where_constraints(mono->source, &method->where_constraints, &subst, &method_error)) {
                continue;
            }
            free(cloned.owner_type_name);
            cloned.owner_type_name = dup_text(*instantiated_name);
            cloned.type_params.count = 0;
            for (int j = 0; j < cloned.params.count; ++j) {
                cloned.params.items[j].type = clone_type_subst(&cloned.params.items[j].type, &subst);
            }
            cloned.return_type = clone_type_subst(&cloned.return_type, &subst);
            memset(&empty_args, 0, sizeof(empty_args));
            for (int j = 0; j < cloned.body.stmts.count; ++j) {
                free(cloned.body.stmts.items[j]);
            }
            memset(&cloned.body, 0, sizeof(cloned.body));
            for (int j = 0; j < method->body.stmts.count; ++j) {
                stmt_list_push(&cloned.body.stmts, clone_stmt_subst(method->body.stmts.items[j], &subst));
            }
            if (!transform_block(mono, &cloned.body, &(LocalTypeList){0})) {
                return 0;
            }
            function_list_push(&mono->out->functions, cloned);
        }
    }
    return 1;
}

static int instantiate_union_template(MonoContext* mono, const AstUnionDecl* templ, const AstTypeList* type_args, char** instantiated_name) {
    AstUnionDecl decl;
    TypeSubstList subst;
    int i = 0;
    memset(&subst, 0, sizeof(subst));
    *instantiated_name = make_instantiated_name(templ->name, type_args);
    if (find_ast_union(mono->out, *instantiated_name)) {
        return 1;
    }
    if (!copy_subst_from_template(&subst, &templ->type_params, type_args)) {
        mono->error = "generic union type argument mismatch";
        return 0;
    }
    if (!check_generic_mutability_constraints(&templ->type_params, &templ->where_constraints, &subst, &mono->error)) {
        return 0;
    }
    if (!check_where_constraints(mono->source, &templ->where_constraints, &subst, &mono->error)) {
        return 0;
    }
    memset(&decl, 0, sizeof(decl));
    decl.tag_name = templ->tag_name ? dup_text(templ->tag_name) : 0;
    decl.name = dup_text(*instantiated_name);
    decl.public_flag = templ->public_flag;
    decl.line = templ->line;
    for (i = 0; i < templ->concept_names.count; ++i) {
        name_list_push(&decl.concept_names, dup_text(templ->concept_names.items[i]));
    }
    for (i = 0; i < templ->assoc_type_bindings.count; ++i) {
        AstAssocTypeBinding binding;
        memset(&binding, 0, sizeof(binding));
        clone_name_list(&binding.context_concept_names, &templ->assoc_type_bindings.items[i].context_concept_names);
        binding.concept_name = templ->assoc_type_bindings.items[i].concept_name
            ? dup_text(templ->assoc_type_bindings.items[i].concept_name)
            : 0;
        binding.name = dup_text(templ->assoc_type_bindings.items[i].name);
        binding.value = clone_type_subst(&templ->assoc_type_bindings.items[i].value, &subst);
        binding.line = templ->assoc_type_bindings.items[i].line;
        assoc_type_binding_list_push(&decl.assoc_type_bindings, binding);
    }
    for (i = 0; i < templ->variants.count; ++i) {
        AstUnionVariant variant;
        memset(&variant, 0, sizeof(variant));
        variant.type = clone_type_subst(&templ->variants.items[i].type, &subst);
        variant.name = dup_text(templ->variants.items[i].name);
        variant.line = templ->variants.items[i].line;
        union_variant_list_push(&decl.variants, variant);
    }
    union_list_push(&mono->out->unions, decl);
    for (i = 0; i < mono->source->functions.count; ++i) {
        const AstFunction* method = &mono->source->functions.items[i];
        if (method->method_flag && method->owner_type_name && strcmp(method->owner_type_name, templ->name) == 0) {
            AstFunction cloned = clone_function(mono->source, 0, 0, method, method->public_flag);
            const char* method_error = 0;
            if (!check_where_constraints(mono->source, &method->where_constraints, &subst, &method_error)) {
                continue;
            }
            free(cloned.owner_type_name);
            cloned.owner_type_name = dup_text(*instantiated_name);
            cloned.type_params.count = 0;
            for (int j = 0; j < cloned.params.count; ++j) {
                cloned.params.items[j].type = clone_type_subst(&cloned.params.items[j].type, &subst);
            }
            cloned.return_type = clone_type_subst(&cloned.return_type, &subst);
            for (int j = 0; j < cloned.body.stmts.count; ++j) {
                free(cloned.body.stmts.items[j]);
            }
            memset(&cloned.body, 0, sizeof(cloned.body));
            for (int j = 0; j < method->body.stmts.count; ++j) {
                stmt_list_push(&cloned.body.stmts, clone_stmt_subst(method->body.stmts.items[j], &subst));
            }
            if (!transform_block(mono, &cloned.body, &(LocalTypeList){0})) {
                return 0;
            }
            function_list_push(&mono->out->functions, cloned);
        }
    }
    return 1;
}

static int transform_expr(MonoContext* mono, AstExpr* expr, LocalTypeList* locals) {
    int i = 0;
    if (!expr) {
        return 1;
    }
    switch (expr->kind) {
        case AST_EXPR_IMPLICIT:
            if (expr->as.implicit.target_is_type) {
                if (!transform_type(mono, &expr->as.implicit.type_target)) {
                    return 0;
                }
            } else {
                if (!transform_expr(mono, expr->as.implicit.value_target, locals)) {
                    return 0;
                }
            }
            if (expr->as.implicit.has_type_arg &&
                !transform_type(mono, &expr->as.implicit.type_arg)) {
                return 0;
            }
            for (i = 0; i < expr->as.implicit.args.count; ++i) {
                if (!transform_expr(mono, expr->as.implicit.args.items[i], locals)) {
                    return 0;
                }
            }
            return 1;
        case AST_EXPR_SIZE_OF:
            return transform_type(mono, &expr->as.size_of_type);
        case AST_EXPR_CALL: {
            const AstFunction* templ = 0;
            AstTypeList type_args;
            memset(&type_args, 0, sizeof(type_args));
            for (i = 0; i < expr->as.call.args.count; ++i) {
                if (!transform_expr(mono, expr->as.call.args.items[i].value, locals)) {
                    return 0;
                }
            }
            templ = find_generic_function_template(mono->source, expr->as.call.callee);
            if (!templ) {
                const AstStructDecl* struct_templ = 0;
                for (i = 0; i < expr->as.call.type_args.count; ++i) {
                    if (!transform_type(mono, &expr->as.call.type_args.items[i])) {
                        return 0;
                    }
                }
                if (expr->as.call.type_args.count > 0) {
                    struct_templ = find_generic_struct_template(mono->source, expr->as.call.callee);
                    if (struct_templ) {
                        char* instantiated_name = 0;
                        if (!instantiate_struct_template(mono, struct_templ, &expr->as.call.type_args, &instantiated_name)) {
                            return 0;
                        }
                        free(expr->as.call.callee);
                        expr->as.call.callee = instantiated_name;
                        memset(&expr->as.call.type_args, 0, sizeof(expr->as.call.type_args));
                    }
                }
                return 1;
            }
            if (expr->as.call.type_args.count > 0) {
                for (i = 0; i < expr->as.call.type_args.count; ++i) {
                    AstType arg = expr->as.call.type_args.items[i];
                    if (!transform_type(mono, &arg)) {
                        return 0;
                    }
                    type_list_push(&type_args, arg);
                }
            } else if (!infer_type_args_from_call(mono->out, templ, expr, locals, &type_args)) {
                mono->error = "failed to infer generic function type arguments";
                return 0;
            }
            {
                char* instantiated_name = 0;
                if (!instantiate_function_template(mono, templ, &type_args, &instantiated_name)) {
                    return 0;
                }
                free(expr->as.call.callee);
                expr->as.call.callee = instantiated_name;
                memset(&expr->as.call.type_args, 0, sizeof(expr->as.call.type_args));
            }
            return 1;
        }
        case AST_EXPR_STRUCT:
            for (i = 0; i < expr->as.struct_lit.type_args.count; ++i) {
                if (!transform_type(mono, &expr->as.struct_lit.type_args.items[i])) {
                    return 0;
                }
            }
            if (expr->as.struct_lit.type_name && expr->as.struct_lit.type_args.count > 0) {
                const AstStructDecl* templ = find_generic_struct_template(mono->source, expr->as.struct_lit.type_name);
                char* instantiated_name = 0;
                if (!templ) {
                    mono->error = "unknown generic type";
                    return 0;
                }
                if (!instantiate_struct_template(mono, templ, &expr->as.struct_lit.type_args, &instantiated_name)) {
                    return 0;
                }
                free(expr->as.struct_lit.type_name);
                expr->as.struct_lit.type_name = instantiated_name;
                memset(&expr->as.struct_lit.type_args, 0, sizeof(expr->as.struct_lit.type_args));
            }
            for (i = 0; i < expr->as.struct_lit.fields.count; ++i) {
                if (!transform_expr(mono, expr->as.struct_lit.fields.items[i].value, locals)) {
                    return 0;
                }
            }
            return 1;
        case AST_EXPR_FIELD:
        case AST_EXPR_OPTIONAL_FIELD:
            return transform_expr(mono, expr->as.field.base, locals);
        case AST_EXPR_ADDR:
        case AST_EXPR_DEREF:
        case AST_EXPR_NEW:
        case AST_EXPR_FREE:
        case AST_EXPR_BIT_NOT:
            return transform_expr(mono, expr->as.unary.value, locals);
        case AST_EXPR_BINARY:
            return transform_expr(mono, expr->as.binary.left, locals) &&
                   transform_expr(mono, expr->as.binary.right, locals);
        case AST_EXPR_COALESCE:
            return transform_expr(mono, expr->as.coalesce.left, locals) &&
                   transform_expr(mono, expr->as.coalesce.right, locals);
        case AST_EXPR_CATCH_FALLBACK:
            return transform_expr(mono, expr->as.catch_fallback.left, locals) &&
                   transform_expr(mono, expr->as.catch_fallback.fallback, locals);
        case AST_EXPR_CATCH_HANDLER:
            return transform_expr(mono, expr->as.catch_handler.left, locals) &&
                   transform_expr(mono, expr->as.catch_handler.handler, locals);
        case AST_EXPR_BLOCK:
            return transform_block(mono, expr->as.block_expr.body, locals) &&
                   transform_expr(mono, expr->as.block_expr.value, locals);
        case AST_EXPR_IF:
            return transform_expr(mono, expr->as.if_expr.cond, locals) &&
                   transform_expr(mono, expr->as.if_expr.then_expr, locals) &&
                   transform_expr(mono, expr->as.if_expr.else_expr, locals);
        case AST_EXPR_SWITCH:
            if (!transform_expr(mono, expr->as.switch_expr.value, locals)) {
                return 0;
            }
            for (i = 0; i < expr->as.switch_expr.cases.count; ++i) {
                if ((expr->as.switch_expr.cases.items[i].pattern &&
                     !transform_expr(mono, expr->as.switch_expr.cases.items[i].pattern, locals)) ||
                    !transform_expr(mono, expr->as.switch_expr.cases.items[i].value, locals)) {
                    return 0;
                }
            }
            return 1;
        case AST_EXPR_TRY:
            if (!transform_expr(mono, expr->as.try_expr.value, locals)) {
                return 0;
            }
            for (i = 0; i < expr->as.try_expr.catches.count; ++i) {
                if (!transform_type(mono, &expr->as.try_expr.catches.items[i].error_type) ||
                    !transform_expr(mono, expr->as.try_expr.catches.items[i].value, locals)) {
                    return 0;
                }
            }
            return 1;
        case AST_EXPR_COALESCE_CONTROL:
            return transform_expr(mono, expr->as.coalesce_control.left, locals) &&
                   (!expr->as.coalesce_control.return_expr ||
                    transform_expr(mono, expr->as.coalesce_control.return_expr, locals));
        case AST_EXPR_TERNARY:
            return transform_expr(mono, expr->as.ternary.cond, locals) &&
                   transform_expr(mono, expr->as.ternary.then_expr, locals) &&
                   transform_expr(mono, expr->as.ternary.else_expr, locals);
        case AST_EXPR_VARIANT:
            return !expr->as.variant.payload || transform_expr(mono, expr->as.variant.payload, locals);
        case AST_EXPR_TUPLE:
            for (i = 0; i < expr->as.tuple.items.count; ++i) {
                if (!transform_expr(mono, expr->as.tuple.items.items[i], locals)) {
                    return 0;
                }
            }
            return 1;
        case AST_EXPR_ARRAY:
            for (i = 0; i < expr->as.array.items.count; ++i) {
                if (!transform_expr(mono, expr->as.array.items.items[i], locals)) {
                    return 0;
                }
            }
            return 1;
        case AST_EXPR_INDEX:
        case AST_EXPR_OPTIONAL_INDEX:
            return transform_expr(mono, expr->as.index.base, locals) &&
                   transform_expr(mono, expr->as.index.index, locals);
        case AST_EXPR_SLICE_LENGTH:
            return transform_expr(mono, expr->as.slice_length.base, locals);
        case AST_EXPR_INT:
        case AST_EXPR_FLOAT:
        case AST_EXPR_CHAR:
        case AST_EXPR_BOOL:
        case AST_EXPR_STRING:
        case AST_EXPR_NULL:
        case AST_EXPR_NAME:
            return 1;
    }
    return 1;
}

static int monomorphize_program(const AstProgram* source, AstProgram* out, const char** error) {
    MonoContext mono;
    int i = 0;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < source->concepts.count; ++i) {
        concept_list_push(&out->concepts, clone_concept_decl(source, 0, 0, &source->concepts.items[i], source->concepts.items[i].public_flag));
    }
    mono.source = source;
    mono.out = out;
    mono.error = 0;
    if (!validate_where_constraints_program(source, error)) {
        return 0;
    }
    for (i = 0; i < source->enums.count; ++i) {
        enum_list_push(&out->enums, clone_enum_decl(source, 0, 0, &source->enums.items[i], source->enums.items[i].public_flag));
    }
    for (i = 0; i < source->unions.count; ++i) {
        if (source->unions.items[i].type_params.count > 0) {
            continue;
        }
        union_list_push(&out->unions, clone_union_decl(source, 0, 0, &source->unions.items[i], source->unions.items[i].public_flag));
    }
    for (i = 0; i < source->structs.count; ++i) {
        AstStructDecl decl;
        int init_index = 0;
        if (source->structs.items[i].type_params.count > 0) {
            continue;
        }
        decl = clone_struct_decl(source, 0, 0, &source->structs.items[i], source->structs.items[i].public_flag);
        for (init_index = 0; init_index < decl.init_overloads.count; ++init_index) {
            if (!transform_block(&mono, &decl.init_overloads.items[init_index].body, &(LocalTypeList){0})) {
                *error = mono.error;
                return 0;
            }
        }
        struct_list_push(&out->structs, decl);
    }
    for (i = 0; i < source->globals.count; ++i) {
        AstGlobal global = clone_global(source, 0, 0, &source->globals.items[i], source->globals.items[i].public_flag);
        if (!transform_type(&mono, &global.type) || !transform_expr(&mono, global.init, &(LocalTypeList){0})) {
            *error = mono.error;
            return 0;
        }
        global_list_push(&out->globals, global);
    }
    for (i = 0; i < source->functions.count; ++i) {
        AstFunction fn;
        LocalTypeList locals;
        int j = 0;
        if (source->functions.items[i].type_params.count > 0 || is_generic_owner_method(source, &source->functions.items[i])) {
            continue;
        }
        memset(&locals, 0, sizeof(locals));
        fn = clone_function(source, 0, 0, &source->functions.items[i], source->functions.items[i].public_flag);
        if (!transform_type(&mono, &fn.return_type)) {
            *error = mono.error;
            return 0;
        }
        for (j = 0; j < fn.params.count; ++j) {
            LocalTypeEntry entry;
            if (!transform_type(&mono, &fn.params.items[j].type)) {
                *error = mono.error;
                return 0;
            }
            entry.name = fn.params.items[j].name;
            entry.type = ast_type_copy(&fn.params.items[j].type);
            local_type_list_push(&locals, entry);
        }
        if (!transform_block(&mono, &fn.body, &locals)) {
            *error = mono.error;
            return 0;
        }
        function_list_push(&out->functions, fn);
    }
    return 1;
}

int main(int argc, char** argv) {
    Parser parser;
    AstProgram ast;
    AstProgram mono_ast;
    HirProgram hir;
    JirProgram jir;
    char* source = 0;
    char* ir = 0;
    char* input_path = 0;
    const char* error = 0;

    if (argc != 3 || strcmp(argv[1], "--emit-llvm") != 0) {
        usage(argv[0]);
        return 1;
    }

    {
        const char* stack[64] = {0};
        (void)parser;
        source = 0;
        if (!load_package_root_path(argv[2], &input_path, &error)) {
            print_compiler_error(argv[2], 0, 0, error ? error : "failed to resolve package root");
            return 1;
        }
        if (!load_effective_program(input_path, stack, 0, &ast, &error)) {
            print_compiler_error(input_path, 0, 0, error ? error : "failed to load program");
            return 1;
        }
    }

    if (!monomorphize_program(&ast, &mono_ast, &error)) {
        print_compiler_error(input_path, 0, 0, error ? error : "failed to monomorphize AST");
        return 1;
    }
    {
        int error_line = 0;
        if (!lower_ast_to_hir(&mono_ast, &hir, &error, &error_line)) {
            print_compiler_error(input_path, error_line, 0, error ? error : "failed to lower AST to HIR");
            return 1;
        }
    }
    {
        int error_line = 0;
        if (!lower_hir_to_jir(&hir, &jir, &error, &error_line)) {
            print_compiler_error(input_path, error_line, 0, error ? error : "failed to lower HIR to JIR");
            return 1;
        }
    }
    if (!emit_llvm_module(&jir, &ir, &error)) {
        print_compiler_error(input_path, 0, 0, error ? error : "failed to emit LLVM IR");
        return 1;
    }

    fputs(ir, stdout);
    LLVMDisposeMessage(ir);
    return 0;
}
