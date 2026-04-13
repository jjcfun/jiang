#ifndef JIANG_TOKEN_H
#define JIANG_TOKEN_H

#include <stddef.h>

typedef enum TokenKind {
    TOKEN_EOF = 0,
    TOKEN_RETURN,
    TOKEN_INT_LIT,
    TOKEN_SEMICOLON,
    TOKEN_ERROR,
} TokenKind;

typedef struct Token {
    TokenKind kind;
    const char* start;
    size_t length;
    int line;
} Token;

#endif
