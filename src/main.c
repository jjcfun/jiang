#include "llvm_emit.h"
#include "lower.h"
#include "parser.h"

#include <llvm-c/Core.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void usage(const char* argv0) {
    fprintf(stderr, "usage: %s --emit-llvm <file>\n", argv0);
}

int main(int argc, char** argv) {
    Parser parser;
    AstProgram ast;
    HirProgram hir;
    JirProgram jir;
    char* source = 0;
    char* ir = 0;
    const char* error = 0;

    if (argc != 3 || strcmp(argv[1], "--emit-llvm") != 0) {
        usage(argv[0]);
        return 1;
    }

    source = read_file(argv[2]);
    if (!source) {
        fprintf(stderr, "error: failed to read %s\n", argv[2]);
        return 1;
    }

    parser_init(&parser, source);
    if (!parser_parse_program(&parser, &ast)) {
        fprintf(stderr, "error: %s at line %d\n", parser.error, parser.error_line);
        free(source);
        return 1;
    }

    if (!lower_ast_to_hir(&ast, &hir, &error)) {
        fprintf(stderr, "error: %s\n", error ? error : "failed to lower AST to HIR");
        free(source);
        return 1;
    }
    if (!lower_hir_to_jir(&hir, &jir, &error)) {
        fprintf(stderr, "error: %s\n", error ? error : "failed to lower HIR to JIR");
        free(source);
        return 1;
    }
    if (!emit_llvm_module(&jir, &ir, &error)) {
        fprintf(stderr, "error: %s\n", error ? error : "failed to emit LLVM IR");
        free(source);
        return 1;
    }

    fputs(ir, stdout);
    LLVMDisposeMessage(ir);
    free(source);
    return 0;
}
