#ifndef JIANG_LEXER_H
#define JIANG_LEXER_H

#include <stdint.h>
#include <stddef.h>

// Token Types for Jiang
typedef enum {
    TOKEN_EOF = 0,
    TOKEN_ERROR,

    // Single-character tokens
    TOKEN_LEFT_PAREN,    // (
    TOKEN_RIGHT_PAREN,   // )
    TOKEN_LEFT_BRACE,    // {
    TOKEN_RIGHT_BRACE,   // }
    TOKEN_LEFT_BRACKET,  // [
    TOKEN_RIGHT_BRACKET, // ]
    TOKEN_COMMA,         // ,
    TOKEN_DOT,           // .
    TOKEN_MINUS,         // -
    TOKEN_PLUS,          // +
    TOKEN_SEMICOLON,     // ;
    TOKEN_SLASH,         // /
    TOKEN_STAR,          // *
    TOKEN_DOT_DOT,       // ..
    TOKEN_DOLLAR,        // $
    TOKEN_QUESTION,      // ?
    TOKEN_BANG,          // !
    TOKEN_AT,            // @
    TOKEN_COLON,         // :
    TOKEN_AMPERSAND,     // &
    TOKEN_UNDERSCORE,    // _

    // One or two character tokens
    TOKEN_BANG_EQUAL,    // !=
    TOKEN_EQUAL,         // =
    TOKEN_EQUAL_EQUAL,   // ==
    TOKEN_GREATER,       // >
    TOKEN_GREATER_EQUAL, // >=
    TOKEN_LESS,          // <
    TOKEN_LESS_EQUAL,    // <=
    TOKEN_PLUS_EQUAL,    // +=
    TOKEN_MINUS_EQUAL,   // -=
    TOKEN_STAR_EQUAL,    // *=
    TOKEN_SLASH_EQUAL,   // /=

    // Literals
    TOKEN_IDENTIFIER,
    TOKEN_STRING,
    TOKEN_NUMBER,
    
    // Keywords
    TOKEN_STRUCT,
    TOKEN_INIT,
    TOKEN_ENUM,
    TOKEN_UNION,
    TOKEN_IMPORT,
    TOKEN_PUBLIC,
    TOKEN_EXTERN,
    TOKEN_NEW,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_NULL,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_FOR,
    TOKEN_IN,
    TOKEN_SWITCH,
    TOKEN_RETURN,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_ASYNC,
    TOKEN_FN,
    TOKEN_SUDO
} TokenType;

typedef struct {
    TokenType type;
    const char* start;
    size_t length;
    int line;
} Token;

// Initialize the lexer with source code text
void lexer_init(const char* source);

// Return the next token from the source code
Token lexer_next_token(void);
Token peek_token(void);
Token peek_next_token(void);

#endif // JIANG_LEXER_H
