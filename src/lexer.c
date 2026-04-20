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

static int match_char(Lexer* lexer, char expected) {
    if (*lexer->current != expected) {
        return 0;
    }
    lexer->current += 1;
    return 1;
}

static int is_hex_digit_char(char ch) {
    return (ch >= '0' && ch <= '9') ||
           (ch >= 'a' && ch <= 'f') ||
           (ch >= 'A' && ch <= 'F');
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
        if (ch == '/' && lexer->current[1] == '/') {
            lexer->current += 2;
            while (*lexer->current != '\0' && *lexer->current != '\n') {
                lexer->current += 1;
            }
            continue;
        }
        return;
    }
}

static Token ident_or_keyword(Lexer* lexer) {
    size_t length = 0;
    lexer->current += 1;
    while (isalnum((unsigned char)*lexer->current) || *lexer->current == '_') {
        lexer->current += 1;
    }
    length = (size_t)(lexer->current - lexer->start);
    if (length == 3 && strncmp(lexer->start, "new", 3) == 0) {
        return make_token(lexer, TOKEN_KW_NEW, lexer->start, length);
    }
    if (length == 6 && strncmp(lexer->start, "import", 6) == 0) {
        return make_token(lexer, TOKEN_KW_IMPORT, lexer->start, length);
    }
    if (length == 6 && strncmp(lexer->start, "public", 6) == 0) {
        return make_token(lexer, TOKEN_KW_PUBLIC, lexer->start, length);
    }
    if (length == 5 && strncmp(lexer->start, "alias", 5) == 0) {
        return make_token(lexer, TOKEN_KW_ALIAS, lexer->start, length);
    }
    if (length == 6 && strncmp(lexer->start, "return", 6) == 0) {
        return make_token(lexer, TOKEN_KW_RETURN, lexer->start, length);
    }
    if (length == 2 && strncmp(lexer->start, "if", 2) == 0) {
        return make_token(lexer, TOKEN_KW_IF, lexer->start, length);
    }
    if (length == 4 && strncmp(lexer->start, "else", 4) == 0) {
        return make_token(lexer, TOKEN_KW_ELSE, lexer->start, length);
    }
    if (length == 5 && strncmp(lexer->start, "while", 5) == 0) {
        return make_token(lexer, TOKEN_KW_WHILE, lexer->start, length);
    }
    if (length == 3 && strncmp(lexer->start, "for", 3) == 0) {
        return make_token(lexer, TOKEN_KW_FOR, lexer->start, length);
    }
    if (length == 2 && strncmp(lexer->start, "in", 2) == 0) {
        return make_token(lexer, TOKEN_KW_IN, lexer->start, length);
    }
    if (length == 4 && strncmp(lexer->start, "enum", 4) == 0) {
        return make_token(lexer, TOKEN_KW_ENUM, lexer->start, length);
    }
    if (length == 5 && strncmp(lexer->start, "union", 5) == 0) {
        return make_token(lexer, TOKEN_KW_UNION, lexer->start, length);
    }
    if (length == 6 && strncmp(lexer->start, "struct", 6) == 0) {
        return make_token(lexer, TOKEN_KW_STRUCT, lexer->start, length);
    }
    if (length == 5 && strncmp(lexer->start, "trait", 5) == 0) {
        return make_token(lexer, TOKEN_KW_CONCEPT, lexer->start, length);
    }
    if (length == 6 && strncmp(lexer->start, "static", 6) == 0) {
        return make_token(lexer, TOKEN_KW_STATIC, lexer->start, length);
    }
    if (length == 6 && strncmp(lexer->start, "switch", 6) == 0) {
        return make_token(lexer, TOKEN_KW_SWITCH, lexer->start, length);
    }
    if (length == 5 && strncmp(lexer->start, "break", 5) == 0) {
        return make_token(lexer, TOKEN_KW_BREAK, lexer->start, length);
    }
    if (length == 8 && strncmp(lexer->start, "continue", 8) == 0) {
        return make_token(lexer, TOKEN_KW_CONTINUE, lexer->start, length);
    }
    if (length == 4 && strncmp(lexer->start, "true", 4) == 0) {
        return make_token(lexer, TOKEN_KW_TRUE, lexer->start, length);
    }
    if (length == 5 && strncmp(lexer->start, "false", 5) == 0) {
        return make_token(lexer, TOKEN_KW_FALSE, lexer->start, length);
    }
    if (length == 4 && strncmp(lexer->start, "null", 4) == 0) {
        return make_token(lexer, TOKEN_KW_NULL, lexer->start, length);
    }
    return make_token(lexer, TOKEN_IDENT, lexer->start, length);
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

    if (isdigit((unsigned char)*lexer->current)) {
        int is_float = 0;
        lexer->current += 1;
        while (isdigit((unsigned char)*lexer->current)) {
            lexer->current += 1;
        }
        if (*lexer->current == '.' && isdigit((unsigned char)lexer->current[1])) {
            is_float = 1;
            lexer->current += 1;
            while (isdigit((unsigned char)*lexer->current)) {
                lexer->current += 1;
            }
        }
        return make_token(lexer, is_float ? TOKEN_FLOAT_LIT : TOKEN_INT_LIT, lexer->start, (size_t)(lexer->current - lexer->start));
    }

    if (*lexer->current == '"') {
        lexer->current += 1;
        while (*lexer->current != '\0' && *lexer->current != '"' && *lexer->current != '\n') {
            lexer->current += 1;
        }
        if (*lexer->current != '"') {
            return make_token(lexer, TOKEN_ERROR, lexer->start, (size_t)(lexer->current - lexer->start));
        }
        lexer->current += 1;
        return make_token(lexer, TOKEN_STRING_LIT, lexer->start, (size_t)(lexer->current - lexer->start));
    }

    if (*lexer->current == '\'') {
        lexer->current += 1;
        if (*lexer->current == '\0' || *lexer->current == '\n' || *lexer->current == '\'') {
            return make_token(lexer, TOKEN_ERROR, lexer->start, (size_t)(lexer->current - lexer->start));
        }
        if (*lexer->current == '\\') {
            lexer->current += 1;
            if (*lexer->current == 'u') {
                lexer->current += 1;
                if (*lexer->current != '{') {
                    return make_token(lexer, TOKEN_ERROR, lexer->start, (size_t)(lexer->current - lexer->start));
                }
                lexer->current += 1;
                if (!is_hex_digit_char(*lexer->current)) {
                    return make_token(lexer, TOKEN_ERROR, lexer->start, (size_t)(lexer->current - lexer->start));
                }
                while (is_hex_digit_char(*lexer->current)) {
                    lexer->current += 1;
                }
                if (*lexer->current != '}') {
                    return make_token(lexer, TOKEN_ERROR, lexer->start, (size_t)(lexer->current - lexer->start));
                }
                lexer->current += 1;
            } else if (*lexer->current == 'n' || *lexer->current == 'r' || *lexer->current == 't' ||
                       *lexer->current == '0' || *lexer->current == '\\' || *lexer->current == '\'' || *lexer->current == '"') {
                lexer->current += 1;
            } else {
                return make_token(lexer, TOKEN_ERROR, lexer->start, (size_t)(lexer->current - lexer->start));
            }
        } else {
            lexer->current += 1;
        }
        if (*lexer->current != '\'') {
            return make_token(lexer, TOKEN_ERROR, lexer->start, (size_t)(lexer->current - lexer->start));
        }
        lexer->current += 1;
        return make_token(lexer, TOKEN_CHAR_LIT, lexer->start, (size_t)(lexer->current - lexer->start));
    }

    if (isalpha((unsigned char)*lexer->current) || *lexer->current == '_') {
        return ident_or_keyword(lexer);
    }

    lexer->current += 1;
    switch (lexer->start[0]) {
        case '(':
            return make_token(lexer, TOKEN_LEFT_PAREN, lexer->start, 1);
        case ')':
            return make_token(lexer, TOKEN_RIGHT_PAREN, lexer->start, 1);
        case '{':
            return make_token(lexer, TOKEN_LEFT_BRACE, lexer->start, 1);
        case '}':
            return make_token(lexer, TOKEN_RIGHT_BRACE, lexer->start, 1);
        case '[':
            return make_token(lexer, TOKEN_LEFT_BRACKET, lexer->start, 1);
        case ']':
            return make_token(lexer, TOKEN_RIGHT_BRACKET, lexer->start, 1);
        case '@':
            return make_token(lexer, TOKEN_AT, lexer->start, 1);
        case ',':
            return make_token(lexer, TOKEN_COMMA, lexer->start, 1);
        case ':':
            return make_token(lexer, TOKEN_COLON, lexer->start, 1);
        case '?':
            if (match_char(lexer, '.')) {
                return make_token(lexer, TOKEN_QUESTION_DOT, lexer->start, 2);
            }
            if (match_char(lexer, '?')) {
                return make_token(lexer, TOKEN_QUESTION_QUESTION, lexer->start, 2);
            }
            return make_token(lexer, TOKEN_QUESTION, lexer->start, 1);
        case ';':
            return make_token(lexer, TOKEN_SEMICOLON, lexer->start, 1);
        case '+':
            return make_token(lexer, TOKEN_PLUS, lexer->start, 1);
        case '-':
            return make_token(lexer, TOKEN_MINUS, lexer->start, 1);
        case '*':
            return make_token(lexer, TOKEN_STAR, lexer->start, 1);
        case '%':
            return make_token(lexer, TOKEN_PERCENT, lexer->start, 1);
        case '&':
            return make_token(lexer, TOKEN_AMP, lexer->start, 1);
        case '$':
            return make_token(lexer, TOKEN_DOLLAR, lexer->start, 1);
        case '/':
            return make_token(lexer, TOKEN_SLASH, lexer->start, 1);
        case '.':
            if (match_char(lexer, '.')) {
                return make_token(lexer, TOKEN_DOT_DOT, lexer->start, 2);
            }
            return make_token(lexer, TOKEN_DOT, lexer->start, 1);
        case '=':
            if (match_char(lexer, '=')) {
                return make_token(lexer, TOKEN_EQ_EQ, lexer->start, 2);
            }
            return make_token(lexer, TOKEN_ASSIGN, lexer->start, 1);
        case '!':
            if (match_char(lexer, '=')) {
                return make_token(lexer, TOKEN_BANG_EQ, lexer->start, 2);
            }
            return make_token(lexer, TOKEN_BANG, lexer->start, 1);
        case '<':
            if (match_char(lexer, '=')) {
                return make_token(lexer, TOKEN_LT_EQ, lexer->start, 2);
            }
            return make_token(lexer, TOKEN_LT, lexer->start, 1);
        case '>':
            if (match_char(lexer, '=')) {
                return make_token(lexer, TOKEN_GT_EQ, lexer->start, 2);
            }
            return make_token(lexer, TOKEN_GT, lexer->start, 1);
        default:
            break;
    }

    return make_token(lexer, TOKEN_ERROR, lexer->start, 1);
}
