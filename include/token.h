#ifndef JIANG_TOKEN_H
#define JIANG_TOKEN_H

#include <stddef.h>

typedef enum TokenKind {
    TOKEN_EOF = 0,
    TOKEN_ERROR,
    TOKEN_IDENT,
    TOKEN_INT_LIT,
    TOKEN_KW_INT,
    TOKEN_KW_BOOL,
    TOKEN_KW_RETURN,
    TOKEN_KW_IF,
    TOKEN_KW_ELSE,
    TOKEN_KW_WHILE,
    TOKEN_KW_FOR,
    TOKEN_KW_IN,
    TOKEN_KW_ENUM,
    TOKEN_KW_UNION,
    TOKEN_KW_STRUCT,
    TOKEN_KW_STATIC,
    TOKEN_KW_SWITCH,
    TOKEN_KW_BREAK,
    TOKEN_KW_CONTINUE,
    TOKEN_KW_TRUE,
    TOKEN_KW_FALSE,
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE,
    TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_BRACKET,
    TOKEN_RIGHT_BRACKET,
    TOKEN_COMMA,
    TOKEN_COLON,
    TOKEN_QUESTION,
    TOKEN_SEMICOLON,
    TOKEN_ASSIGN,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_BANG,
    TOKEN_EQ_EQ,
    TOKEN_BANG_EQ,
    TOKEN_LT,
    TOKEN_LT_EQ,
    TOKEN_GT,
    TOKEN_GT_EQ,
    TOKEN_DOT,
    TOKEN_DOT_DOT,
} TokenKind;

typedef struct Token {
    TokenKind kind;
    const char* start;
    size_t length;
    int line;
} Token;

#endif
