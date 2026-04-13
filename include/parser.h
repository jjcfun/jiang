#ifndef JIANG_PARSER_H
#define JIANG_PARSER_H

#include "ast.h"
#include "lexer.h"

typedef struct Parser {
    Lexer lexer;
    Token current;
    const char* error;
} Parser;

void parser_init(Parser* parser, const char* source);
int parser_parse_program(Parser* parser, AstProgram* out_program);

#endif
