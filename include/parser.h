#ifndef JIANG_PARSER_H
#define JIANG_PARSER_H

#include "ast.h"
#include "hashmap.h"
#include "lexer.h"

typedef struct Parser {
    Lexer lexer;
    Token current;
    Token next;
    const char* source;
    const char* filename;
    const char* error;
    int error_line;
    int error_column;
    HashMap known_types;
    HashMap static_fields;
    AstNameList scoped_type_names;
} Parser;

void parser_init(Parser* parser, const char* source, const char* filename);
int parser_parse_program(Parser* parser, AstProgram* out_program);

#endif
