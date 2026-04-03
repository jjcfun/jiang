#include "lexer.h"
#include <string.h>
#include <stdbool.h>

typedef struct {
    const char* start;
    const char* current;
    int line;
} Lexer;

static Lexer lexer;

void lexer_init(const char* source) {
    lexer.start = source;
    lexer.current = source;
    lexer.line = 1;
}

static bool is_at_end() {
    return *lexer.current == '\0';
}

static char advance() {
    lexer.current++;
    return lexer.current[-1];
}

static char peek() {
    return *lexer.current;
}

static char peek_next() {
    if (is_at_end()) return '\0';
    return lexer.current[1];
}

static bool match(char expected) {
    if (is_at_end()) return false;
    if (*lexer.current != expected) return false;
    lexer.current++;
    return true;
}

static Token make_token(TokenType type) {
    Token token;
    token.type = type;
    token.start = lexer.start;
    token.length = (size_t)(lexer.current - lexer.start);
    token.line = lexer.line;
    return token;
}

static Token error_token(const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = strlen(message);
    token.line = lexer.line;
    return token;
}

static void skip_whitespace() {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                lexer.line++;
                advance();
                break;
            case '/':
                if (peek_next() == '/') {
                    // Stage1 treats source as a UTF-8 byte stream. Comments are
                    // skipped byte-by-byte until '\n', so non-ASCII UTF-8 bytes
                    // are accepted here without introducing Unicode identifier rules.
                    while (peek() != '\n' && !is_at_end()) advance();
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static TokenType check_keyword(int start, int length, const char* rest, TokenType type) {
    if (lexer.current - lexer.start == start + length && memcmp(lexer.start + start, rest, length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

static TokenType identifier_type() {
    // Simple trie for keyword recognition
    switch (lexer.start[0]) {
        case 'D': return check_keyword(1, 5, "efer", TOKEN_IDENTIFIER); // Fallback to identifier if no defer keyword
        case 'F': 
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'a': return check_keyword(2, 3, "lse", TOKEN_FALSE);
                    case 'n': return check_keyword(2, 0, "", TOKEN_FN);
                }
            }
            break;
        case 'I': break;
        case 'U': break;
        case 'a': return check_keyword(1, 4, "sync", TOKEN_ASYNC);
        case 'b': return check_keyword(1, 4, "reak", TOKEN_BREAK);
        case 'c': return check_keyword(1, 7, "ontinue", TOKEN_CONTINUE);
        case 'e':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'l': return check_keyword(2, 2, "se", TOKEN_ELSE);
                    case 'n': return check_keyword(2, 2, "um", TOKEN_ENUM);
                    case 'x': return check_keyword(2, 4, "tern", TOKEN_EXTERN);
                }
            }
            break;
        case 'f':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'a': return check_keyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o': return check_keyword(2, 1, "r", TOKEN_FOR);
                }
            }
            break;
        case 'i':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'f': return check_keyword(2, 0, "", TOKEN_IF);
                    case 'm': return check_keyword(2, 4, "port", TOKEN_IMPORT);
                    case 'n': 
                        if (lexer.current - lexer.start == 2) return TOKEN_IN;
                        return check_keyword(2, 2, "it", TOKEN_INIT);
                }
            }
            break;
        case 'n':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 'e': return check_keyword(2, 1, "w", TOKEN_NEW);
                    case 'u': return check_keyword(2, 2, "ll", TOKEN_NULL);
                }
            }
            break;
        case 'p': return check_keyword(1, 5, "ublic", TOKEN_PUBLIC);
        case 'r': return check_keyword(1, 5, "eturn", TOKEN_RETURN);
        case 's':
            if (lexer.current - lexer.start > 1) {
                switch (lexer.start[1]) {
                    case 't': return check_keyword(2, 4, "ruct", TOKEN_STRUCT);
                    case 'w': return check_keyword(2, 4, "itch", TOKEN_SWITCH);
                    case 'u': return check_keyword(2, 2, "do", TOKEN_SUDO);
                }
            }
            break;
        case 't': return check_keyword(1, 3, "rue", TOKEN_TRUE);
        case 'u': return check_keyword(1, 4, "nion", TOKEN_UNION);
        case 'w': return check_keyword(1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static Token identifier() {
    while (is_alpha(peek()) || is_digit(peek())) advance();
    return make_token(identifier_type());
}

static Token number() {
    while (is_digit(peek())) advance();

    // Look for a fractional part
    if (peek() == '.' && is_digit(peek_next())) {
        advance(); // Consume the "."
        while (is_digit(peek())) advance();
    }

    return make_token(TOKEN_NUMBER);
}

static Token string() {
    // Stage1 keeps string literals as raw UTF-8 bytes. The lexer does not try
    // to decode Unicode here; it only preserves the byte range inside quotes.
    while (peek() != '"' && !is_at_end()) {
        if (peek() == '\n') lexer.line++;
        advance();
    }

    if (is_at_end()) return error_token("Unterminated string.");

    // The closing quote
    advance();
    return make_token(TOKEN_STRING);
}

Token lexer_next_token() {
    skip_whitespace();
    lexer.start = lexer.current;

    if (is_at_end()) return make_token(TOKEN_EOF);

    char c = advance();

    if (is_alpha(c)) return identifier();
    if (is_digit(c)) return number();

    switch (c) {
        case '(': return make_token(TOKEN_LEFT_PAREN);
        case ')': return make_token(TOKEN_RIGHT_PAREN);
        case '{': return make_token(TOKEN_LEFT_BRACE);
        case '}': return make_token(TOKEN_RIGHT_BRACE);
        case '[': return make_token(TOKEN_LEFT_BRACKET);
        case ']': return make_token(TOKEN_RIGHT_BRACKET);
        case ';': return make_token(TOKEN_SEMICOLON);
        case ',': return make_token(TOKEN_COMMA);
        case '.': 
            if (match('.')) return make_token(TOKEN_DOT_DOT);
            return make_token(TOKEN_DOT);
        case '-': return make_token(match('=') ? TOKEN_MINUS_EQUAL : TOKEN_MINUS);
        case '+': return make_token(match('=') ? TOKEN_PLUS_EQUAL  : TOKEN_PLUS);
        case '/': return make_token(match('=') ? TOKEN_SLASH_EQUAL : TOKEN_SLASH);
        case '*': return make_token(match('=') ? TOKEN_STAR_EQUAL  : TOKEN_STAR);
        case '$': return make_token(TOKEN_DOLLAR);
        case '?': return make_token(TOKEN_QUESTION);
        case '@': return make_token(TOKEN_AT);
        case ':': return make_token(TOKEN_COLON);
        case '&': return make_token(TOKEN_AMPERSAND);
        
        case '!': return make_token(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=': return make_token(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<': return make_token(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>': return make_token(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);

        case '"': return string();
    }

    // Outside strings/comments, Stage1 still keeps identifier rules ASCII-only.
    // Non-ASCII UTF-8 bytes therefore remain invalid here and intentionally
    // surface as a lexer error instead of becoming Unicode identifiers.
    return error_token("Unexpected character.");
}

Token peek_token() {
    const char* old_start = lexer.start;
    const char* old_current = lexer.current;
    int old_line = lexer.line;
    Token token = lexer_next_token();
    lexer.start = old_start;
    lexer.current = old_current;
    lexer.line = old_line;
    return token;
}

Token peek_next_token() {
    const char* old_start = lexer.start;
    const char* old_current = lexer.current;
    int old_line = lexer.line;
    lexer_next_token();
    Token token = lexer_next_token();
    lexer.start = old_start;
    lexer.current = old_current;
    lexer.line = old_line;
    return token;
}
