#include "parser.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

static void advance(Parser* parser) {
    parser->current = lexer_next(&parser->lexer);
}

static int expect(Parser* parser, TokenKind kind, const char* error) {
    if (parser->current.kind != kind) {
        parser->error = error;
        return 0;
    }
    advance(parser);
    return 1;
}

void parser_init(Parser* parser, const char* source) {
    lexer_init(&parser->lexer, source);
    parser->error = 0;
    advance(parser);
}

int parser_parse_program(Parser* parser, AstProgram* out_program) {
    char* end = 0;
    long long value = 0;

    if (!expect(parser, TOKEN_RETURN, "expected 'return'")) {
        return 0;
    }
    if (parser->current.kind != TOKEN_INT_LIT) {
        parser->error = "expected integer literal after 'return'";
        return 0;
    }

    errno = 0;
    value = strtoll(parser->current.start, &end, 10);
    if (errno != 0 || end != parser->current.start + parser->current.length || value > INT64_MAX || value < INT64_MIN) {
        parser->error = "invalid integer literal";
        return 0;
    }
    out_program->return_stmt.expr.int_value = (int64_t)value;
    advance(parser);

    if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after return expression")) {
        return 0;
    }
    if (!expect(parser, TOKEN_EOF, "expected end of file")) {
        return 0;
    }
    return 1;
}
