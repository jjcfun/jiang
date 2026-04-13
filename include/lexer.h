#ifndef JIANG_LEXER_H
#define JIANG_LEXER_H

#include "token.h"

typedef struct Lexer {
    const char* start;
    const char* current;
    int line;
} Lexer;

void lexer_init(Lexer* lexer, const char* source);
Token lexer_next(Lexer* lexer);

#endif
