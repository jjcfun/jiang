#include "lexer.h"

#include <ctype.h>
#include <string.h>

static Token make_token(Lexer* lexer, TokenKind kind, const char* start, size_t length) {
    Token token;
    token.kind = kind;
    token.start = start;
    token.length = length;
    token.line = lexer->line;
    return token;
}

static void skip_ws(Lexer* lexer) {
    for (;;) {
        char ch = *lexer->current;
        if (ch == ' ' || ch == '\r' || ch == '\t') {
            lexer->current += 1;
            continue;
        }
        if (ch == '\n') {
            lexer->line += 1;
            lexer->current += 1;
            continue;
        }
        return;
    }
}

void lexer_init(Lexer* lexer, const char* source) {
    lexer->start = source;
    lexer->current = source;
    lexer->line = 1;
}

Token lexer_next(Lexer* lexer) {
    skip_ws(lexer);
    lexer->start = lexer->current;

    if (*lexer->current == '\0') {
        return make_token(lexer, TOKEN_EOF, lexer->current, 0);
    }

    if (*lexer->current == ';') {
        lexer->current += 1;
        return make_token(lexer, TOKEN_SEMICOLON, lexer->start, 1);
    }

    if (isdigit((unsigned char)*lexer->current)) {
        while (isdigit((unsigned char)*lexer->current)) {
            lexer->current += 1;
        }
        return make_token(lexer, TOKEN_INT_LIT, lexer->start, (size_t)(lexer->current - lexer->start));
    }

    if (isalpha((unsigned char)*lexer->current) || *lexer->current == '_') {
        while (isalnum((unsigned char)*lexer->current) || *lexer->current == '_') {
            lexer->current += 1;
        }
        size_t length = (size_t)(lexer->current - lexer->start);
        if (length == 6 && strncmp(lexer->start, "return", 6) == 0) {
            return make_token(lexer, TOKEN_RETURN, lexer->start, length);
        }
        return make_token(lexer, TOKEN_ERROR, lexer->start, length);
    }

    lexer->current += 1;
    return make_token(lexer, TOKEN_ERROR, lexer->start, 1);
}
