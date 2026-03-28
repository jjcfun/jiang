#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include "hir.h"
#include "jir.h"
#include "lower.h"
#include "jir_gen.h"
#include "llvm_gen.h"
#include "arena.h"

typedef struct BuildManifest {
    char manifest_path[PATH_MAX];
    char project_dir[PATH_MAX];
    char name[128];
    char root[PATH_MAX];
    char stdlib_dir[PATH_MAX];
    char test_dir[PATH_MAX];
    char module_names[64][128];
    char module_paths[64][PATH_MAX];
    int module_count;
    char target_names[32][128];
    char target_roots[32][PATH_MAX];
    char target_test_dirs[32][PATH_MAX];
    int target_count;
} BuildManifest;

typedef enum {
    BACKEND_C,
    BACKEND_LLVM,
} BackendKind;

static char compiler_include_dir[PATH_MAX] = "include";

static char* read_file_text(const char* path) {
    // Source and manifest files are read as raw bytes. Stage1 UTF-8 support is
    // defined at the byte level: the compiler preserves UTF-8 bytes in strings
    // and comments, but does not yet implement Unicode identifier semantics.
    FILE* file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char* text = malloc(size + 1);
    if (!text) { fclose(file); return NULL; }
    size_t rb = fread(text, 1, size, file);
    text[rb] = '\0';
    fclose(file);
    return text;
}

static void print_usage(const char* argv0) {
    printf("Usage:\n");
    printf("  %s [--stdlib-dir <path>] [--dump-hir] [--emit-llvm] [--backend <c|llvm>] <input_file>\n", argv0);
    printf("  %s build [--manifest <path>] [--target <name>] [--backend <c|llvm>]\n", argv0);
    printf("  %s run [--manifest <path>] [--target <name>] [--backend <c|llvm>]\n", argv0);
    printf("  %s test [--manifest <path>] [--target <name>] [--backend <c|llvm>]\n", argv0);
}

static bool parse_backend_name(const char* text, BackendKind* out_backend) {
    if (strcmp(text, "c") == 0) {
        *out_backend = BACKEND_C;
        return true;
    }
    if (strcmp(text, "llvm") == 0) {
        *out_backend = BACKEND_LLVM;
        return true;
    }
    return false;
}

static bool ensure_dir(const char* path) {
    if (mkdir(path, 0777) == 0) return true;
    return errno == EEXIST;
}

static void path_dirname(const char* path, char* out, size_t out_size) {
    char temp[PATH_MAX];
    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    char* dir = dirname(temp);
    strncpy(out, dir, out_size - 1);
    out[out_size - 1] = '\0';
}

static void path_join(const char* base, const char* tail, char* out, size_t out_size) {
    if (!tail || tail[0] == '\0') {
        strncpy(out, base, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    if (tail[0] == '/') {
        strncpy(out, tail, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    snprintf(out, out_size, "%s/%s", base, tail);
}

static void init_compiler_paths(const char* argv0) {
    char exe_path[PATH_MAX];
    char exe_dir[PATH_MAX];
    char project_dir[PATH_MAX];

    if (realpath(argv0, exe_path) == NULL) {
        strncpy(compiler_include_dir, "include", sizeof(compiler_include_dir) - 1);
        compiler_include_dir[sizeof(compiler_include_dir) - 1] = '\0';
        return;
    }

    path_dirname(exe_path, exe_dir, sizeof(exe_dir));
    path_dirname(exe_dir, project_dir, sizeof(project_dir));
    path_join(project_dir, "include", compiler_include_dir, sizeof(compiler_include_dir));
}

static void file_stem(const char* path, char* out, size_t out_size) {
    char temp[PATH_MAX];
    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    char* base = basename(temp);
    strncpy(out, base, out_size - 1);
    out[out_size - 1] = '\0';
    char* dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

static char* trim_in_place(char* text) {
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') text++;
    size_t len = strlen(text);
    while (len > 0) {
        char ch = text[len - 1];
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') break;
        text[--len] = '\0';
    }
    return text;
}

static void strip_optional_quotes(char* text) {
    size_t len = strlen(text);
    if (len >= 2 && text[0] == '"' && text[len - 1] == '"') {
        memmove(text, text + 1, len - 2);
        text[len - 2] = '\0';
    }
}

static bool manifest_set_value(BuildManifest* manifest, const char* key, const char* value_text) {
    char value[PATH_MAX];
    strncpy(value, value_text, sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';
    char* trimmed = trim_in_place(value);
    strip_optional_quotes(trimmed);

    if (strcmp(key, "name") == 0) {
        strncpy(manifest->name, trimmed, sizeof(manifest->name) - 1);
        manifest->name[sizeof(manifest->name) - 1] = '\0';
        return true;
    }
    if (strcmp(key, "root") == 0) {
        path_join(manifest->project_dir, trimmed, manifest->root, sizeof(manifest->root));
        return true;
    }
    if (strcmp(key, "stdlib_dir") == 0) {
        path_join(manifest->project_dir, trimmed, manifest->stdlib_dir, sizeof(manifest->stdlib_dir));
        return true;
    }
    if (strcmp(key, "test_dir") == 0) {
        path_join(manifest->project_dir, trimmed, manifest->test_dir, sizeof(manifest->test_dir));
        return true;
    }
    if (strncmp(key, "module.", 7) == 0) {
        if (manifest->module_count >= 64) return false;
        const char* module_name = key + 7;
        if (module_name[0] == '\0') return false;
        strncpy(manifest->module_names[manifest->module_count], module_name,
                sizeof(manifest->module_names[0]) - 1);
        manifest->module_names[manifest->module_count][sizeof(manifest->module_names[0]) - 1] = '\0';
        path_join(manifest->project_dir, trimmed, manifest->module_paths[manifest->module_count],
                  sizeof(manifest->module_paths[0]));
        manifest->module_count++;
        return true;
    }
    if (strncmp(key, "target.", 7) == 0) {
        const char* target_name = key + 7;
        const char* suffix = strrchr(target_name, '.');
        if (!suffix) return false;
        size_t name_len = (size_t)(suffix - target_name);
        if (name_len == 0 || name_len >= sizeof(manifest->target_names[0])) return false;
        int target_index = -1;
        for (int i = 0; i < manifest->target_count; i++) {
            if (strncmp(manifest->target_names[i], target_name, name_len) == 0 &&
                manifest->target_names[i][name_len] == '\0') {
                target_index = i;
                break;
            }
        }
        if (target_index < 0) {
            if (manifest->target_count >= 32) return false;
            target_index = manifest->target_count++;
            memcpy(manifest->target_names[target_index], target_name, name_len);
            manifest->target_names[target_index][name_len] = '\0';
        }
        if (strcmp(suffix, ".root") == 0) {
            path_join(manifest->project_dir, trimmed, manifest->target_roots[target_index],
                      sizeof(manifest->target_roots[0]));
            return true;
        }
        if (strcmp(suffix, ".test_dir") == 0) {
            path_join(manifest->project_dir, trimmed, manifest->target_test_dirs[target_index],
                      sizeof(manifest->target_test_dirs[0]));
            return true;
        }
        return false;
    }
    return false;
}

static bool manifest_target_root(const BuildManifest* manifest, const char* target_name,
                                 char* out_root, size_t out_root_size) {
    if (!target_name || target_name[0] == '\0') {
        strncpy(out_root, manifest->root, out_root_size - 1);
        out_root[out_root_size - 1] = '\0';
        return manifest->root[0] != '\0';
    }

    for (int i = 0; i < manifest->target_count; i++) {
        if (strcmp(manifest->target_names[i], target_name) == 0) {
            strncpy(out_root, manifest->target_roots[i], out_root_size - 1);
            out_root[out_root_size - 1] = '\0';
            return true;
        }
    }
    return false;
}

static bool manifest_target_test_dir(const BuildManifest* manifest, const char* target_name,
                                     char* out_dir, size_t out_dir_size) {
    if (!target_name || target_name[0] == '\0') {
        strncpy(out_dir, manifest->test_dir, out_dir_size - 1);
        out_dir[out_dir_size - 1] = '\0';
        return manifest->test_dir[0] != '\0';
    }

    for (int i = 0; i < manifest->target_count; i++) {
        if (strcmp(manifest->target_names[i], target_name) == 0) {
            if (manifest->target_test_dirs[i][0] == '\0') return false;
            strncpy(out_dir, manifest->target_test_dirs[i], out_dir_size - 1);
            out_dir[out_dir_size - 1] = '\0';
            return true;
        }
    }
    return false;
}

static bool load_manifest(const char* manifest_path, BuildManifest* manifest) {
    memset(manifest, 0, sizeof(*manifest));
    strncpy(manifest->manifest_path, manifest_path, sizeof(manifest->manifest_path) - 1);
    path_dirname(manifest_path, manifest->project_dir, sizeof(manifest->project_dir));

    char* text = read_file_text(manifest_path);
    if (!text) {
        fprintf(stderr, "Could not read manifest %s\n", manifest_path);
        return false;
    }

    char* cursor = text;
    int line_no = 1;
    while (cursor && *cursor) {
        char* line = cursor;
        char* newline = strchr(cursor, '\n');
        if (newline) {
            *newline = '\0';
            cursor = newline + 1;
        } else {
            cursor = NULL;
        }

        char* trimmed = trim_in_place(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#') {
            line_no++;
            continue;
        }

        char* equals = strchr(trimmed, '=');
        if (!equals) {
            fprintf(stderr, "Manifest %s line %d missing '='\n", manifest_path, line_no);
            free(text);
            return false;
        }

        *equals = '\0';
        char* key = trim_in_place(trimmed);
        char* value = trim_in_place(equals + 1);
        if (!manifest_set_value(manifest, key, value)) {
            fprintf(stderr, "Manifest %s line %d has unknown key '%s'\n", manifest_path, line_no, key);
            free(text);
            return false;
        }
        line_no++;
    }

    if (manifest->stdlib_dir[0] == '\0') {
        path_join(manifest->project_dir, "std", manifest->stdlib_dir, sizeof(manifest->stdlib_dir));
    }
    if (manifest->test_dir[0] == '\0') {
        path_join(manifest->project_dir, "tests", manifest->test_dir, sizeof(manifest->test_dir));
    }
    if (manifest->name[0] == '\0') {
        fprintf(stderr, "Manifest %s missing required key 'name'\n", manifest_path);
        free(text);
        return false;
    }
    if (manifest->root[0] == '\0') {
        fprintf(stderr, "Manifest %s missing required key 'root'\n", manifest_path);
        free(text);
        return false;
    }

    free(text);
    return true;
}

static bool link_llvm_ir(const char* ir_path, const char* output_path) {
    char cmd[32768];
    int status = 0;
    snprintf(cmd, sizeof(cmd), "xcrun clang -Wno-override-module -x ir \"%s\" -o \"%s\"", ir_path, output_path);
    status = system(cmd);
    if (status != 0) {
        snprintf(cmd, sizeof(cmd), "clang -Wno-override-module -x ir \"%s\" -o \"%s\"", ir_path, output_path);
        status = system(cmd);
    }
    if (status != 0) {
        fprintf(stderr, "LLVM linkage failed.\n");
        return false;
    }
    return true;
}

static int compile_input_file(const char* input_path, const char* stdlib_path,
                              const BuildManifest* manifest,
                              const char* build_dir, const char* output_path,
                              bool dump_hir_only, bool emit_llvm_only,
                              BackendKind backend) {
    Arena* arena = arena_create();
    char* source = read_file_text(input_path);
    if (!source) {
        fprintf(stderr, "Could not read file %s\n", input_path);
        arena_destroy(arena);
        return 1;
    }

    if (!ensure_dir(build_dir)) {
        fprintf(stderr, "Could not create build directory %s\n", build_dir);
        arena_destroy(arena);
        free(source);
        return 1;
    }

    ASTNode* root = parse_source(arena, source);
    if (!root) {
        arena_destroy(arena);
        free(source);
        return 1;
    }

    semantic_reset();
    semantic_set_stdlib_dir(stdlib_path);
    semantic_clear_module_map();
    if (manifest) {
        for (int i = 0; i < manifest->module_count; i++) {
            semantic_add_named_module(manifest->module_names[i], manifest->module_paths[i]);
        }
    }
    Module* main_mod = semantic_check(arena, root, input_path);
    if (!main_mod) {
        arena_destroy(arena);
        free(source);
        return 1;
    }

    if (dump_hir_only) {
        HIRModule* hir = module_get_hir(main_mod);
        hir_dump(stdout, hir);
        arena_destroy(arena);
        free(source);
        return 0;
    }

    Module* all_mods[128];
    int mod_count = semantic_get_modules(all_mods);
    JirModule* jir_mods[128];
    CodegenModuleInfo* codegen_infos[128];
    int main_mod_index = -1;
    for (int i = 0; i < mod_count; i++) {
        Module* mod = all_mods[i];
        HIRModule* hir = module_get_hir(mod);
        jir_mods[i] = lower_hir_to_jir(hir, arena);
        codegen_infos[i] = module_get_codegen_info(mod);
        if (mod == main_mod) main_mod_index = i;
    }

    if (emit_llvm_only || backend == BACKEND_LLVM) {
        char llvm_output_path[PATH_MAX];
        if (emit_llvm_only) {
            strncpy(llvm_output_path, output_path, sizeof(llvm_output_path) - 1);
            llvm_output_path[sizeof(llvm_output_path) - 1] = '\0';
        } else {
            snprintf(llvm_output_path, sizeof(llvm_output_path), "%s.ll", output_path);
        }
        char llvm_error[512];
        if (main_mod_index < 0 ||
            !llvm_generate_ir_program(jir_mods, codegen_infos, (size_t)mod_count, (size_t)main_mod_index,
                                      llvm_output_path, llvm_error, sizeof(llvm_error))) {
            fprintf(stderr, "%s\n", llvm_error[0] ? llvm_error : "LLVM emission failed.");
            arena_destroy(arena);
            free(source);
            return 1;
        }
        if (!emit_llvm_only && !link_llvm_ir(llvm_output_path, output_path)) {
            arena_destroy(arena);
            free(source);
            return 1;
        }
        // LLVM codegen currently keeps a few semantic/lowering-owned references
        // alive until process exit. Avoid tearing down the arena on the success
        // path here so aggregate-heavy cases like anonymous unions don't crash
        // after the final binary has already been produced.
        return 0;
    }

    char c_files[128][PATH_MAX];
    int c_count = 0;

    for (int i = 0; i < mod_count; i++) {
        Module* mod = all_mods[i];
        JirModule* jir = jir_mods[i];
        if (!jir) continue;

        char out_path[PATH_MAX];
        snprintf(out_path, sizeof(out_path), "%s/%s_%s.c", build_dir, module_get_name(mod), module_get_id(mod));
        jir_generate_c(jir, module_get_codegen_info(mod), out_path, (mod == main_mod),
                       module_get_name(mod), module_get_id(mod));
        strncpy(c_files[c_count], out_path, PATH_MAX - 1);
        c_files[c_count][PATH_MAX - 1] = '\0';
        c_count++;
    }

    char cmd[32768];
    int offset = snprintf(cmd, sizeof(cmd), "gcc -I\"%s\" ", compiler_include_dir);
    for (int i = 0; i < c_count; i++) {
        offset += snprintf(cmd + offset, sizeof(cmd) - offset, "\"%s\" ", c_files[i]);
    }
    snprintf(cmd + offset, sizeof(cmd) - offset, "-o \"%s\" -lm", output_path);

    if (system(cmd) != 0) {
        fprintf(stderr, "Linkage failed.\n");
        arena_destroy(arena);
        free(source);
        return 1;
    }

    arena_destroy(arena);
    free(source);
    return 0;
}

static int build_project(const BuildManifest* manifest, const char* target_name,
                         char* out_bin, size_t out_bin_size, BackendKind backend) {
    char build_dir[PATH_MAX];
    char root_path[PATH_MAX];
    char output_name[256];
    path_join(manifest->project_dir, "build", build_dir, sizeof(build_dir));
    if (!manifest_target_root(manifest, target_name, root_path, sizeof(root_path))) {
        fprintf(stderr, "Unknown target '%s'\n", target_name ? target_name : "");
        return 1;
    }

    if (target_name && target_name[0] != '\0') {
        snprintf(output_name, sizeof(output_name), "%s_%s", manifest->name, target_name);
    } else {
        snprintf(output_name, sizeof(output_name), "%s", manifest->name);
    }
    path_join(build_dir, output_name, out_bin, out_bin_size);
    return compile_input_file(root_path, manifest->stdlib_dir, manifest, build_dir, out_bin, false, false, backend);
}

static int run_project(const BuildManifest* manifest, const char* target_name, BackendKind backend) {
    char out_bin[PATH_MAX];
    int status = build_project(manifest, target_name, out_bin, sizeof(out_bin), backend);
    if (status != 0) return status;

    char cmd[PATH_MAX + 4];
    snprintf(cmd, sizeof(cmd), "\"%s\"", out_bin);
    return system(cmd);
}

static int test_project(const BuildManifest* manifest, const char* target_name, BackendKind backend) {
    char test_dir[PATH_MAX];
    if (!manifest_target_test_dir(manifest, target_name, test_dir, sizeof(test_dir))) {
        fprintf(stderr, "Unknown test target '%s'\n", target_name ? target_name : "");
        return 1;
    }

    DIR* dir = opendir(test_dir);
    if (!dir) {
        fprintf(stderr, "Could not open test directory %s\n", test_dir);
        return 1;
    }

    char build_dir[PATH_MAX];
    path_join(manifest->project_dir, "build", build_dir, sizeof(build_dir));
    if (!ensure_dir(build_dir)) {
        fprintf(stderr, "Could not create build directory %s\n", build_dir);
        closedir(dir);
        return 1;
    }

    int success_count = 0;
    int fail_count = 0;
    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        const char* name = entry->d_name;
        size_t len = strlen(name);
        if (len < 7 || strcmp(name + len - 6, ".jiang") != 0) continue;

        char test_path[PATH_MAX];
        char output_bin[PATH_MAX];
        char test_name[256];
        path_join(test_dir, name, test_path, sizeof(test_path));
        file_stem(name, test_name, sizeof(test_name));
        path_join(build_dir, test_name, output_bin, sizeof(output_bin));

        printf("正在测试 %s ... ", name);
        fflush(stdout);

        int compile_status = compile_input_file(test_path, manifest->stdlib_dir, manifest, build_dir, output_bin, false, false, backend);
        bool expect_fail = strncmp(name, "fail_", 5) == 0;

        if (expect_fail) {
            if (compile_status != 0) {
                printf("通过 (成功拦截预期内的错误)\n");
                success_count++;
            } else {
                printf("失败 (预期会编译报错，但编译器成功退出了)\n");
                fail_count++;
            }
            continue;
        }

        if (compile_status != 0) {
            printf("失败 (编译过程中出错)\n");
            fail_count++;
            continue;
        }

        char cmd[PATH_MAX + 4];
        snprintf(cmd, sizeof(cmd), "\"%s\"", output_bin);
        int run_status = system(cmd);
        if (run_status == 0) {
            printf("通过\n");
            success_count++;
        } else if (strcmp(test_name, "assert_fail") == 0) {
            printf("通过 (成功触发预期内的断言失败)\n");
            success_count++;
        } else {
            printf("失败 (二进制文件运行崩溃)\n");
            fail_count++;
        }
    }

    closedir(dir);

    printf("\n--- 测试总结 ---\n");
    printf("总计通过: %d\n", success_count);
    printf("总计失败: %d\n", fail_count);
    return fail_count == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    init_compiler_paths(argv[0]);

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "build") == 0 || strcmp(argv[1], "run") == 0 || strcmp(argv[1], "test") == 0) {
        const char* manifest_path = "jiang.build";
        const char* target_name = NULL;
        BackendKind backend = BACKEND_C;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--manifest") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Missing path after --manifest\n");
                    return 1;
                }
                manifest_path = argv[++i];
            } else if (strcmp(argv[i], "--target") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Missing name after --target\n");
                    return 1;
                }
                target_name = argv[++i];
            } else if (strcmp(argv[i], "--backend") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Missing name after --backend\n");
                    return 1;
                }
                if (!parse_backend_name(argv[++i], &backend)) {
                    fprintf(stderr, "Unknown backend '%s'\n", argv[i]);
                    return 1;
                }
            } else {
                fprintf(stderr, "Unknown argument: %s\n", argv[i]);
                return 1;
            }
        }

        BuildManifest manifest;
        if (!load_manifest(manifest_path, &manifest)) return 1;

        if (strcmp(argv[1], "build") == 0) {
            char out_bin[PATH_MAX];
            int status = build_project(&manifest, target_name, out_bin, sizeof(out_bin), backend);
            if (status == 0) printf("Built %s\n", out_bin);
            return status;
        }
        if (strcmp(argv[1], "run") == 0) return run_project(&manifest, target_name, backend);
        return test_project(&manifest, target_name, backend);
    }

    const char* stdlib_path = "std";
    const char* input_path = NULL;
    bool dump_hir_only = false;
    bool emit_llvm_only = false;
    BackendKind backend = BACKEND_C;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--stdlib-dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing path after --stdlib-dir\n");
                return 1;
            }
            stdlib_path = argv[++i];
        } else if (strcmp(argv[i], "--dump-hir") == 0) {
            dump_hir_only = true;
        } else if (strcmp(argv[i], "--emit-llvm") == 0) {
            emit_llvm_only = true;
        } else if (strcmp(argv[i], "--backend") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing name after --backend\n");
                return 1;
            }
            if (!parse_backend_name(argv[++i], &backend)) {
                fprintf(stderr, "Unknown backend '%s'\n", argv[i]);
                return 1;
            }
        } else {
            input_path = argv[i];
        }
    }

    if (!input_path) {
        fprintf(stderr, "Missing input file\n");
        return 1;
    }

    char output_path[PATH_MAX];
    char build_dir[PATH_MAX];
    file_stem(input_path, output_path, sizeof(output_path));
    snprintf(build_dir, sizeof(build_dir), "build");

    char final_output_path[PATH_MAX];
    snprintf(final_output_path, sizeof(final_output_path), "%s/%s%s", build_dir, output_path,
             emit_llvm_only ? ".ll" : "");
    int status = compile_input_file(input_path, stdlib_path, NULL, build_dir, final_output_path,
                                    dump_hir_only, emit_llvm_only, backend);
    if (status == 0 && (emit_llvm_only || backend == BACKEND_LLVM)) {
        _Exit(0);
    }
    return status;
}
