#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
    Arena* arena;
    Token current;
    Token previous;
    bool had_error;
    bool panic_mode;
} Parser;

static Parser parser;

static char parser_enum_names[64][64];
static size_t parser_enum_count = 0;

static void parser_add_enum(Token name) {
    if (parser_enum_count >= 64) return;
    snprintf(parser_enum_names[parser_enum_count++], 64, "%.*s", (int)name.length, name.start);
}

static bool parser_is_enum(Token name) {
    for (size_t i = 0; i < parser_enum_count; i++) {
        if (strncmp(parser_enum_names[i], name.start, name.length) == 0 && parser_enum_names[i][name.length] == '\0') {
            return true;
        }
    }
    return false;
}



static void error_at(Token* token, const char* message) {
    if (parser.panic_mode) return;
    parser.panic_mode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        fprintf(stderr, " at '%.*s'", (int)token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.had_error = true;
}

static void advance() {
    parser.previous = parser.current;
    
    for (;;) {
        parser.current = lexer_next_token();
        if (parser.current.type != TOKEN_ERROR) break;
        error_at(&parser.current, parser.current.start);
    }
}

static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }
    error_at(&parser.current, message);
}

static bool check(TokenType type) {
    return parser.current.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

// Pratt Parser Definitions
typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_RANGE,       // ..
    PREC_COMPARISON,  // == != < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * /
    PREC_PRIMARY
} Precedence;

static ASTNode* expression();
static ASTNode* statement();
static TypeExpr* parse_type();
static ASTNode* parse_precedence(Precedence precedence);
static ASTNode* struct_declaration();
static ASTNode* enum_declaration();
static ASTNode* block_statement();
static ASTNode* function_declaration(TypeExpr* return_type, Token name);

static void synchronize() {
    parser.panic_mode = false;
    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;
        switch (parser.current.type) {
            case TOKEN_STRUCT:
            case TOKEN_ENUM:
            case TOKEN_UNION:
            case TOKEN_IF:
            case TOKEN_ELSE:
            case TOKEN_SWITCH:
            case TOKEN_RETURN:
            case TOKEN_SUDO:
            case TOKEN_UNDERSCORE:
                return;
            default: ; // Do nothing
        }
        advance();
    }
}

static TypeExpr* parse_type() {
    if (match(TOKEN_LEFT_PAREN)) {
        if (match(TOKEN_RIGHT_PAREN)) {
            // Empty tuple ()
            Token void_tok = {TOKEN_EOF, "()", 2, parser.previous.line};
            return type_new_base(parser.arena, void_tok);
        }
        
        TypeExpr* elements[64];
        Token names[64];
        size_t count = 0;
        bool has_names = false;

        do {
            elements[count] = parse_type();
            names[count] = (Token){0};
            if (check(TOKEN_IDENTIFIER)) {
                advance();
                names[count] = parser.previous;
                has_names = true;
            }
            count++;
        } while (match(TOKEN_COMMA));

        consume(TOKEN_RIGHT_PAREN, "Expect ')' after tuple type.");
        
        TypeExpr* type = NULL;
        if (count == 1 && !has_names) {
            // Grouping (Type)
            type = elements[0];
        } else {
            TypeExpr** elements_copy = arena_alloc(parser.arena, sizeof(TypeExpr*) * count);
            memcpy(elements_copy, elements, sizeof(TypeExpr*) * count);
            Token* names_copy = NULL;
            if (has_names) {
                names_copy = arena_alloc(parser.arena, sizeof(Token) * count);
                memcpy(names_copy, names, sizeof(Token) * count);
            }
            type = type_new_tuple(parser.arena, elements_copy, names_copy, count);
        }

        // Handle array modifier after (Type) or Tuple
        while (match(TOKEN_LEFT_BRACKET)) {
            ASTNode* length = NULL;
            if (!check(TOKEN_RIGHT_BRACKET)) {
                length = expression();
            }
            consume(TOKEN_RIGHT_BRACKET, "Expect ']' after array length.");
            type = type_new_modifier(parser.arena, TYPE_ARRAY, type);
            type->as.array.length = length;
        }
        return type;
    }
    
    if (match(TOKEN_UNDERSCORE) || match(TOKEN_IDENTIFIER)) {
        
        TypeExpr* type = type_new_base(parser.arena, parser.previous);
        
        // Handle modifiers: *, ?, !, []
        for (;;) {
            if (match(TOKEN_STAR)) {
                type = type_new_modifier(parser.arena, TYPE_POINTER, type);
            } else if (match(TOKEN_QUESTION)) {
                type = type_new_modifier(parser.arena, TYPE_NULLABLE, type);
            } else if (match(TOKEN_BANG)) {
                type = type_new_modifier(parser.arena, TYPE_MUTABLE, type);
            } else if (match(TOKEN_LEFT_BRACKET)) {
                ASTNode* length = NULL;
                bool inferred = false;
                if (match(TOKEN_UNDERSCORE)) {
                    inferred = true;
                } else if (!check(TOKEN_RIGHT_BRACKET)) {
                    length = expression();
                }
                consume(TOKEN_RIGHT_BRACKET, "Expect ']' after array length.");
                
                if (length == NULL && !inferred) {
                    // It's a Slice Type[]
                    type = type_new_modifier(parser.arena, TYPE_SLICE, type);
                } else {
                    // It's an Array Type[N] or Type[_]
                    type = type_new_modifier(parser.arena, TYPE_ARRAY, type);
                    type->as.array.length = length; // NULL means inferred '_'
                }
            } else {
                break;
            }
        }
        return type;
    }
    error_at(&parser.current, "Expect type name.");
    return NULL;
}

static ASTNode* number() {
    double value = strtod(parser.previous.start, NULL);
    ASTNode* node = ast_new_node(parser.arena, AST_LITERAL_NUMBER, parser.previous.line);
    node->as.number.value = value;
    return node;
}

static ASTNode* grouping() {
    ASTNode* expr = expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
    return expr;
}

static ASTNode* binary(ASTNode* left) {
    TokenType operatorType = parser.previous.type;
    
    Precedence prec = PREC_NONE;
    switch (operatorType) {
        case TOKEN_EQUAL: prec = PREC_ASSIGNMENT; break;
        case TOKEN_DOT_DOT: prec = PREC_RANGE; break;
        case TOKEN_EQUAL_EQUAL:
        case TOKEN_BANG_EQUAL:
        case TOKEN_LESS:
        case TOKEN_LESS_EQUAL:
        case TOKEN_GREATER:
        case TOKEN_GREATER_EQUAL: prec = PREC_COMPARISON; break;
        case TOKEN_PLUS:
        case TOKEN_MINUS: prec = PREC_TERM; break;
        case TOKEN_STAR:
        case TOKEN_SLASH: prec = PREC_FACTOR; break;
        default: break;
    }

    ASTNode* right = NULL;
    // For range operator, right side is optional if inside indexing (though EBNF says required)
    // To support [start..], we check if we should parse right
    if (operatorType == TOKEN_DOT_DOT) {
        if (check(TOKEN_RIGHT_BRACKET) || check(TOKEN_COMMA) || check(TOKEN_RIGHT_PAREN)) {
            // Optional right operand in range
            right = NULL;
        } else {
            right = parse_precedence((Precedence)(prec + 1));
        }
    } else {
        right = parse_precedence((Precedence)(prec + 1));
    }

    ASTNode* node = NULL;
    
    if (operatorType == TOKEN_DOT_DOT) {
        node = ast_new_node(parser.arena, AST_RANGE_EXPR, parser.previous.line);
        node->as.range.start = left;
        node->as.range.end = right;
    } else {
        node = ast_new_node(parser.arena, AST_BINARY_EXPR, parser.previous.line);
        node->as.binary.left = left;
        node->as.binary.right = right;
        node->as.binary.op = operatorType;
    }
    return node;
}

static ASTNode* literal_string() {
    ASTNode* node = ast_new_node(parser.arena, AST_LITERAL_STRING, parser.previous.line);
    node->as.string.value = parser.previous;
    return node;
}

static ASTNode* identifier() {
    ASTNode* node = ast_new_node(parser.arena, AST_IDENTIFIER, parser.previous.line);
    node->as.identifier.name = parser.previous;
    return node;
}

static ASTNode* call(ASTNode* callee) {
    ASTNode* node = ast_new_node(parser.arena, AST_FUNC_CALL, parser.previous.line);
    node->as.func_call.callee = callee;
    
    ASTNode* args[64]; // Fixed size for simplicity in MVP
    size_t count = 0;
    
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            args[count++] = expression();
        } while (match(TOKEN_COMMA));
    }
    
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    
    node->as.func_call.args = arena_alloc(parser.arena, sizeof(ASTNode*) * count);
    memcpy(node->as.func_call.args, args, sizeof(ASTNode*) * count);
    node->as.func_call.arg_count = count;
    
    return node;
}

static ASTNode* dot(ASTNode* object) {
    consume(TOKEN_IDENTIFIER, "Expect member name after '.'.");
    Token member = parser.previous;

    if (object->type == AST_IDENTIFIER && parser_is_enum(object->as.identifier.name)) {
        ASTNode* node = ast_new_node(parser.arena, AST_ENUM_ACCESS, member.line);
        node->as.enum_access.enum_name = object->as.identifier.name;
        node->as.enum_access.member_name = member;
        return node;
    }

    ASTNode* node = ast_new_node(parser.arena, AST_MEMBER_ACCESS, member.line);
    node->as.member_access.object = object;
    node->as.member_access.member = member;
    return node;
}

static ASTNode* struct_init_expr(TypeExpr* type) {
    // Current is already after '{' or the caller matched it.
    // If 'type' is NOT NULL, the caller might NOT have consumed '{' yet (e.g. Type {).
    // Let's check how it's called.
    // 1. identifier() -> struct_init_expr(type) [TOKEN_LEFT_BRACE matches]
    // 2. parse_precedence -> struct_init_expr(NULL) [TOKEN_LEFT_BRACE matches]
    
    // In both cases in the current parser, '{' is already consumed if we are here.
    // Wait, let's check parse_precedence again.
    // parse_precedence: } else if (parser.previous.type == TOKEN_LEFT_BRACE) { node = struct_init_expr(NULL); }
    // identifier: if (check(TOKEN_LEFT_BRACE)) { TypeExpr* type = ...; node = struct_init_expr(type); }
    // Ah, identifier() DOES NOT consume it yet.
    
    if (type) {
        consume(TOKEN_LEFT_BRACE, "Expect '{' before initialization.");
    }
    
    // Peer ahead to see if it's a struct init (ID :) or array init (EXPR)
    // For simplicity, we can just try parsing an expression and see if it's followed by a colon.
    // However, Jiang seems to allow omitting keys if the list is used.
    
    // Let's check for "ID :" pattern
    if (check(TOKEN_IDENTIFIER)) {
        // We need a way to lookahead more than 1 token. Or we can just parse the first item and decide.
        // Actually, let's use a simpler heuristic: if first item is `ID :`, it's a struct init.
        // But tokens are consumed. Let's use `lexer_peek` if available or just assume.
        // Wait, I don't have a reliable peek(2). 
        // Let's just try to parse an expression, but if it was an identifier, we might need to backtrack or handle it.
        
        // Actually, let's just parse the first element as an expression and then check for ':'
        // But if it's an assignment or something else, it's messy.
        
        // Use a better approach: if current is IDENTIFIER, peek next.
        if (peek_token().type == TOKEN_COLON) {
            // Struct init
            Token field_names[64];
            ASTNode* field_values[64];
            size_t count = 0;
            do {
                consume(TOKEN_IDENTIFIER, "Expect field name in struct initialization.");
                field_names[count] = parser.previous;
                consume(TOKEN_COLON, "Expect ':' after field name.");
                field_values[count] = expression();
                count++;
            } while (match(TOKEN_COMMA));
            consume(TOKEN_RIGHT_BRACE, "Expect '}' after struct initialization.");
            
            ASTNode* node = ast_new_node(parser.arena, AST_STRUCT_INIT_EXPR, parser.previous.line);
            node->as.struct_init.type = type;
            node->as.struct_init.field_count = count;
            node->as.struct_init.field_names = arena_alloc(parser.arena, sizeof(Token) * count);
            memcpy(node->as.struct_init.field_names, field_names, sizeof(Token) * count);
            node->as.struct_init.field_values = arena_alloc(parser.arena, sizeof(ASTNode*) * count);
            memcpy(node->as.struct_init.field_values, field_values, sizeof(ASTNode*) * count);
            return node;
        }
    }

    // Array literal
    ASTNode* elements[128];
    size_t count = 0;
    if (!check(TOKEN_RIGHT_BRACE)) {
        do {
            elements[count++] = expression();
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after array literal.");
    
    ASTNode* node = ast_new_node(parser.arena, AST_ARRAY_LITERAL, parser.previous.line);
    node->as.array_literal.type = type;
    node->as.array_literal.element_count = count;
    node->as.array_literal.elements = arena_alloc(parser.arena, sizeof(ASTNode*) * count);
    memcpy(node->as.array_literal.elements, elements, sizeof(ASTNode*) * count);
    return node;
}

static TypeExpr* ast_to_type(ASTNode* node) {
    if (!node) return NULL;
    switch (node->type) {
        case AST_IDENTIFIER: {
            TypeExpr* type = type_new_base(parser.arena, node->as.identifier.name);
            return type;
        }
        case AST_INDEX_EXPR: {
            TypeExpr* element = ast_to_type(node->as.index_expr.object);
            if (!element) return NULL;
            TypeExpr* type = type_new_modifier(parser.arena, TYPE_ARRAY, element);
            type->as.array.length = node->as.index_expr.index;
            return type;
        }
        case AST_UNARY_EXPR: {
            if (node->as.unary.op == TOKEN_STAR) {
                TypeExpr* element = ast_to_type(node->as.unary.right);
                if (!element) return NULL;
                return type_new_modifier(parser.arena, TYPE_POINTER, element);
            }
            // TODO: handle ? and ! if they are prefix in type but postfix/something else in expr
            break;
        }
        default: break;
    }
    return NULL;
}

static ASTNode* parse_precedence(Precedence precedence) {
    advance();

    ASTNode* node = NULL;

    // Prefix rules
    if (parser.previous.type == TOKEN_NUMBER) {
        node = number();
    } else if (parser.previous.type == TOKEN_STRING) {
        node = literal_string();
    } else if (parser.previous.type == TOKEN_LEFT_PAREN) {
        node = grouping();
    } else if (parser.previous.type == TOKEN_LEFT_BRACE) {
        node = struct_init_expr(NULL);
    } else if (parser.previous.type == TOKEN_IDENTIFIER || parser.previous.type == TOKEN_UNDERSCORE) {
        // Check for pattern: '_ name' or 'Type name'
        if (parser.previous.type == TOKEN_UNDERSCORE && check(TOKEN_IDENTIFIER)) {
            advance();
            Token name = parser.previous;
            node = ast_new_node(parser.arena, AST_PATTERN, name.line);
            node->as.pattern.type = NULL;
            node->as.pattern.name = name;
        } else if (parser.previous.type == TOKEN_IDENTIFIER && check(TOKEN_IDENTIFIER)) {
            Token type_tok = parser.previous;
            advance();
            Token name = parser.previous;
            node = ast_new_node(parser.arena, AST_PATTERN, name.line);
            node->as.pattern.type = type_new_base(parser.arena, type_tok);
            node->as.pattern.name = name;
        } else {
            // Simple identifier or keyword-like identifier (Int, Float, etc.)
            node = ast_new_node(parser.arena, AST_IDENTIFIER, parser.previous.line);
            node->as.identifier.name = parser.previous;
        }
    } else if (parser.previous.type == TOKEN_DOT_DOT) {
        // Handle [..end] or [..]
        ASTNode* end = NULL;
        if (!check(TOKEN_RIGHT_BRACKET) && !check(TOKEN_COMMA) && !check(TOKEN_RIGHT_PAREN)) {
            end = parse_precedence(PREC_RANGE);
        }
        node = ast_new_node(parser.arena, AST_RANGE_EXPR, parser.previous.line);
        node->as.range.start = NULL;
        node->as.range.end = end;
    } else if (parser.previous.type == TOKEN_NEW) {
        node = ast_new_node(parser.arena, AST_NEW_EXPR, parser.previous.line);
        // Use PREC_NONE so that the full expression after `new` is parsed,
        // including function calls like new Point(x, y) and
        // struct literals like new Point { x: 1, y: 2 }.
        node->as.new_expr.value = parse_precedence(PREC_NONE);
    } else if (parser.previous.type == TOKEN_DOLLAR) {
        // Handle $ identifier or $ digit
        advance();
        node = identifier();
        node->line = parser.previous.line; // Fix line ref
        node->type = AST_UNARY_EXPR;
        node->as.unary.op = TOKEN_DOLLAR;
        node->as.unary.right = ast_new_node(parser.arena, AST_IDENTIFIER, parser.previous.line);
        node->as.unary.right->as.identifier.name = parser.previous;
    } else if (parser.previous.type == TOKEN_DOT) {
        // Shorthand enum access: .member
        consume(TOKEN_IDENTIFIER, "Expect member name after '.'.");
        ASTNode* node = ast_new_node(parser.arena, AST_ENUM_ACCESS, parser.previous.line);
        node->as.enum_access.enum_name = (Token){0}; // Inferred
        node->as.enum_access.member_name = parser.previous;
        return node;
    } else if (parser.previous.type == TOKEN_STAR || parser.previous.type == TOKEN_MINUS || parser.previous.type == TOKEN_BANG) {
        Token op = parser.previous;
        ASTNode* right = parse_precedence(PREC_PRIMARY); // Unary binds tightly
        node = ast_new_node(parser.arena, AST_UNARY_EXPR, op.line);
        node->as.unary.op = op.type;
        node->as.unary.right = right;
    } else {
        error_at(&parser.previous, "Expect expression.");
        return NULL;
    }

    // Infix & Postfix rules
    for (;;) {
        // Postfix kicks in first
        if (precedence <= PREC_PRIMARY) {
            if (match(TOKEN_LEFT_PAREN)) {
                node = call(node);
                continue;
            } else if (match(TOKEN_DOT)) {
                node = dot(node);
                continue;
            } else if (match(TOKEN_LEFT_BRACKET)) {
                if (match(TOKEN_RIGHT_BRACKET)) {
                    ASTNode* range = ast_new_node(parser.arena, AST_RANGE_EXPR, parser.previous.line);
                    range->as.range.start = NULL;
                    range->as.range.end = NULL;
                    ASTNode* index_node = ast_new_node(parser.arena, AST_INDEX_EXPR, parser.previous.line);
                    index_node->as.index_expr.object = node;
                    index_node->as.index_expr.index = range;
                    node = index_node;
                } else {
                    ASTNode* index = expression();
                    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after array index.");
                    ASTNode* index_node = ast_new_node(parser.arena, AST_INDEX_EXPR, parser.previous.line);
                    index_node->as.index_expr.object = node;
                    index_node->as.index_expr.index = index;
                    node = index_node;
                }
                continue;
            } else if (check(TOKEN_LEFT_BRACE)) {
                TypeExpr* type = ast_to_type(node);
                if (!type) {
                    // It might be something else? No, if it's node followed by {, it must be type init in this context.
                    // But wait, what if it's a closure? Jiang closures use { ... } but usually after -> or as standalone.
                    break; 
                }
                node = struct_init_expr(type);
                continue;
            }
        }

        // Binary
        Precedence next_prec = PREC_NONE;
        switch (parser.current.type) {
            case TOKEN_EQUAL: next_prec = PREC_ASSIGNMENT; break;
            case TOKEN_DOT_DOT: next_prec = PREC_RANGE; break;
            case TOKEN_EQUAL_EQUAL:
            case TOKEN_BANG_EQUAL:
            case TOKEN_LESS:
            case TOKEN_LESS_EQUAL:
            case TOKEN_GREATER:
            case TOKEN_GREATER_EQUAL: next_prec = PREC_COMPARISON; break;
            case TOKEN_PLUS:
            case TOKEN_MINUS: next_prec = PREC_TERM; break;
            case TOKEN_STAR:
            case TOKEN_SLASH: next_prec = PREC_FACTOR; break;
            default: break;
        }

        if (precedence <= next_prec && next_prec != PREC_NONE) {
            advance();
            node = binary(node);
        } else {
            break;
        }
    }

    return node;
}

static ASTNode* expression() {
    return parse_precedence(PREC_ASSIGNMENT);
}



static ASTNode* return_statement() {
    Token return_token = parser.previous;
    ASTNode* expr = NULL;
    if (!check(TOKEN_SEMICOLON)) {
        expr = expression();
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
    
    ASTNode* node = ast_new_node(parser.arena, AST_RETURN_STMT, return_token.line);
    node->as.return_stmt.expression = expr;
    return node;
}

static ASTNode* function_declaration(TypeExpr* return_type, Token name) {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    
    ASTNode* params[64];
    size_t count = 0;
    
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            TypeExpr* p_type = parse_type();
            consume(TOKEN_IDENTIFIER, "Expect parameter name.");
            Token p_name = parser.previous;
            
            ASTNode* param = ast_new_node(parser.arena, AST_VAR_DECL, p_name.line);
            param->as.var_decl.type = p_type;
            param->as.var_decl.name = p_name;
            param->as.var_decl.initializer = NULL;
            params[count++] = param;
        } while (match(TOKEN_COMMA));
    }
    
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    
    ASTNode* body = statement(); // Usually a block
    
    ASTNode* node = ast_new_node(parser.arena, AST_FUNC_DECL, name.line);
    node->as.func_decl.return_type = return_type;
    node->as.func_decl.name = name;
    node->as.func_decl.param_count = count;
    node->as.func_decl.params = arena_alloc(parser.arena, sizeof(ASTNode*) * count);
    memcpy(node->as.func_decl.params, params, sizeof(ASTNode*) * count);
    node->as.func_decl.body = body;
    
    return node;
}

static ASTNode* enum_declaration() {
    TypeExpr* base_type = NULL;
    if (match(TOKEN_LEFT_PAREN)) {
        base_type = parse_type();
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after enum base type.");
    }
    
    consume(TOKEN_IDENTIFIER, "Expect enum name.");
    Token name = parser.previous;
    consume(TOKEN_LEFT_BRACE, "Expect '{' before enum body.");
    
    Token member_names[256];
    ASTNode* member_values[256];
    size_t count = 0;
    
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        consume(TOKEN_IDENTIFIER, "Expect enum member name.");
        member_names[count] = parser.previous;
        member_values[count] = NULL;
        
        if (match(TOKEN_EQUAL)) {
            member_values[count] = expression();
        }
        
        count++;
        if (!match(TOKEN_COMMA)) break;
    }
    
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after enum body.");
    
    ASTNode* node = ast_new_node(parser.arena, AST_ENUM_DECL, name.line);
    node->as.enum_decl.name = name;
    parser_add_enum(name);
    node->as.enum_decl.base_type = base_type;
    node->as.enum_decl.member_count = count;
    node->as.enum_decl.member_names = (Token*)arena_alloc(parser.arena, sizeof(Token) * count);
    node->as.enum_decl.member_values = (ASTNode**)arena_alloc(parser.arena, sizeof(ASTNode*) * count);
    memcpy(node->as.enum_decl.member_names, member_names, sizeof(Token) * count);
    memcpy(node->as.enum_decl.member_values, member_values, sizeof(ASTNode*) * count);
    
    return node;
}

static ASTNode* union_declaration() {
    Token union_token = parser.previous;
    Token tag_enum = {0};
    
    // Optional tag enum: union(TagEnum) UnionName { ... }
    if (match(TOKEN_LEFT_PAREN)) {
        consume(TOKEN_IDENTIFIER, "Expect tag enum name.");
        tag_enum = parser.previous;
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after tag enum.");
    }
    
    consume(TOKEN_IDENTIFIER, "Expect union name.");
    Token name = parser.previous;
    consume(TOKEN_LEFT_BRACE, "Expect '{' before union body.");
    
    ASTNode* members[128];
    size_t count = 0;
    
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        TypeExpr* type = parse_type();
        consume(TOKEN_IDENTIFIER, "Expect variant name.");
        Token mem_name = parser.previous;
        
        consume(TOKEN_SEMICOLON, "Expect ';' after variant declaration.");
        
        ASTNode* field = ast_new_node(parser.arena, AST_VAR_DECL, mem_name.line);
        field->as.var_decl.type = type;
        field->as.var_decl.name = mem_name;
        field->as.var_decl.initializer = NULL;
        members[count++] = field;
    }
    
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after union body.");
    
    ASTNode* node = ast_new_node(parser.arena, AST_UNION_DECL, union_token.line);
    node->as.union_decl.name = name;
    node->as.union_decl.tag_enum = tag_enum;
    node->as.union_decl.member_count = count;
    node->as.union_decl.members = (ASTNode**)arena_alloc(parser.arena, sizeof(ASTNode*) * count);
    memcpy(node->as.union_decl.members, members, sizeof(ASTNode*) * count);
    
    return node;
}

static ASTNode* struct_declaration() {
    Token struct_token = parser.previous;
    consume(TOKEN_IDENTIFIER, "Expect struct name.");
    Token name = parser.previous;
    
    consume(TOKEN_LEFT_BRACE, "Expect '{' before struct body.");
    
    ASTNode* members[128];
    size_t count = 0;
    
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        bool is_public = false;
        if (match(TOKEN_PUBLIC)) {
            is_public = true;
        }

        if (match(TOKEN_INIT)) {
            // Constructor
            consume(TOKEN_LEFT_PAREN, "Expect '(' after 'init'.");
            ASTNode* params[64];
            size_t param_count = 0;
            if (!check(TOKEN_RIGHT_PAREN)) {
                do {
                    TypeExpr* p_type = parse_type();
                    consume(TOKEN_IDENTIFIER, "Expect parameter name.");
                    Token p_name = parser.previous;
                    ASTNode* param = ast_new_node(parser.arena, AST_VAR_DECL, p_name.line);
                    param->as.var_decl.type = p_type;
                    param->as.var_decl.name = p_name;
                    param->as.var_decl.initializer = NULL;
                    params[param_count++] = param;
                } while (match(TOKEN_COMMA));
            }
            consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
            
            ASTNode* body = statement();
            ASTNode* init_node = ast_new_node(parser.arena, AST_INIT_DECL, parser.previous.line);
            init_node->is_public = is_public;
            init_node->as.init_decl.param_count = param_count;
            if (param_count > 0) {
                init_node->as.init_decl.params = arena_alloc(parser.arena, sizeof(ASTNode*) * param_count);
                memcpy(init_node->as.init_decl.params, params, sizeof(ASTNode*) * param_count);
            }
            init_node->as.init_decl.body = body;
            members[count++] = init_node;
        } else {
            // Either field or method. Both start with a type.
            TypeExpr* type = parse_type();
            consume(TOKEN_IDENTIFIER, "Expect member name.");
            Token mem_name = parser.previous;
            
            if (check(TOKEN_LEFT_PAREN)) {
                // Method
                ASTNode* method = function_declaration(type, mem_name);
                if (method) method->is_public = is_public;
                members[count++] = method;
            } else {
                // Fields (could be multiple separated by comma)
                do {
                    if (parser.previous.type == TOKEN_COMMA) {
                        consume(TOKEN_IDENTIFIER, "Expect field name.");
                        mem_name = parser.previous;
                    }
                    ASTNode* field = ast_new_node(parser.arena, AST_VAR_DECL, mem_name.line);
                    field->as.var_decl.type = type;
                    field->as.var_decl.name = mem_name;
                    field->as.var_decl.initializer = NULL;
                    field->is_public = is_public;
                    members[count++] = field;
                } while (match(TOKEN_COMMA));
                
                consume(TOKEN_SEMICOLON, "Expect ';' after field declaration.");
            }
        }
    }
    
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after struct body.");
    
    ASTNode* node = ast_new_node(parser.arena, AST_STRUCT_DECL, struct_token.line);
    node->as.struct_decl.name = name;
    node->as.struct_decl.member_count = count;
    if (count > 0) {
        node->as.struct_decl.members = arena_alloc(parser.arena, sizeof(ASTNode*) * count);
        memcpy(node->as.struct_decl.members, members, sizeof(ASTNode*) * count);
    }
    return node;
}

static ASTNode* block_statement() {
    ASTNode* node = ast_new_node(parser.arena, AST_BLOCK, parser.previous.line);
    ASTNode* statements[1024];
    size_t count = 0;

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        ASTNode* stmt = statement();
        if (stmt) {
            statements[count++] = stmt;
        } else {
            synchronize();
        }
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");

    node->as.block.statements = (ASTNode**)arena_alloc(parser.arena, sizeof(ASTNode*) * count);
    memcpy(node->as.block.statements, statements, sizeof(ASTNode*) * count);
    node->as.block.count = count;

    return node;
}

static ASTNode* if_statement() {
    Token if_token = parser.previous;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    ASTNode* condition = expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    ASTNode* then_branch = statement();
    ASTNode* else_branch = NULL;

    if (match(TOKEN_ELSE)) {
        else_branch = statement();
    }

    ASTNode* node = ast_new_node(parser.arena, AST_IF_STMT, if_token.line);
    node->as.if_stmt.condition = condition;
    node->as.if_stmt.then_branch = then_branch;
    node->as.if_stmt.else_branch = else_branch;
    return node;
}

static ASTNode* while_statement() {
    Token while_token = parser.previous;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    ASTNode* condition = expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    ASTNode* body = statement();

    ASTNode* node = ast_new_node(parser.arena, AST_WHILE_STMT, while_token.line);
    node->as.while_stmt.condition = condition;
    node->as.while_stmt.body = body;
    return node;
}

static ASTNode* for_pattern() {
    if (match(TOKEN_LEFT_PAREN)) {
        ASTNode* node = ast_new_node(parser.arena, AST_BLOCK, parser.previous.line); // Re-using block structure for patterns
        ASTNode* patterns[64];
        size_t count = 0;
        
        do {
            patterns[count++] = for_pattern();
        } while (match(TOKEN_COMMA));
        
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after pattern list.");
        
        node->as.block.statements = (ASTNode**)arena_alloc(parser.arena, sizeof(ASTNode*) * count);
        memcpy(node->as.block.statements, patterns, sizeof(ASTNode*) * count);
        node->as.block.count = count;
        return node;
    }
    
    consume(TOKEN_IDENTIFIER, "Expect identifier in for pattern.");
    ASTNode* node = ast_new_node(parser.arena, AST_IDENTIFIER, parser.previous.line);
    node->as.identifier.name = parser.previous;
    return node;
}

static ASTNode* for_statement() {
    Token for_token = parser.previous;
    
    ASTNode* pattern = for_pattern();
    consume(TOKEN_IN, "Expect 'in' after for pattern.");
    ASTNode* iterable = expression();
    
    ASTNode* body = statement();
    
    ASTNode* node = ast_new_node(parser.arena, AST_FOR_STMT, for_token.line);
    node->as.for_stmt.pattern = pattern;
    node->as.for_stmt.iterable = iterable;
    node->as.for_stmt.body = body;
    return node;
}
static ASTNode* expression_statement() {
    ASTNode* expr = expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    return expr;
}

static ASTNode* import_statement(bool is_public) {
    Token import_tok = parser.previous;
    Token alias = {0};
    Token path = {0};

    // Case 1: import Alias "path";
    if (check(TOKEN_IDENTIFIER)) {
        advance();
        alias = parser.previous;
        consume(TOKEN_STRING, "Expect string literal for import path.");
        path = parser.previous;
    } else {
        // Case 2: import "path";
        consume(TOKEN_STRING, "Expect string literal for import path.");
        path = parser.previous;
    }

    consume(TOKEN_SEMICOLON, "Expect ';' after import statement.");

    ASTNode* node = ast_new_node(parser.arena, AST_IMPORT, import_tok.line);
    node->as.import_decl.is_public = is_public;
    node->as.import_decl.alias = alias;
    node->as.import_decl.path = path;
    return node;
}

static ASTNode* statement() {
    bool is_public = false;
    if (match(TOKEN_PUBLIC)) {
        is_public = true;
    }
    
    if (match(TOKEN_IMPORT)) {
        return import_statement(is_public);
    }
    if (match(TOKEN_IF)) {
        return if_statement();
    }
    if (match(TOKEN_WHILE)) {
        return while_statement();
    }
    if (match(TOKEN_FOR)) {
        return for_statement();
    }
    if (match(TOKEN_RETURN)) {
        return return_statement();
    }
    if (match(TOKEN_BREAK)) {
        Token tok = parser.previous;
        consume(TOKEN_SEMICOLON, "Expect ';' after 'break'.");
        ASTNode* node = ast_new_node(parser.arena, AST_BREAK_STMT, tok.line);
        return node;
    }
    if (match(TOKEN_CONTINUE)) {
        Token tok = parser.previous;
        consume(TOKEN_SEMICOLON, "Expect ';' after 'continue'.");
        ASTNode* node = ast_new_node(parser.arena, AST_CONTINUE_STMT, tok.line);
        return node;
    }
    if (match(TOKEN_STRUCT)) {
        ASTNode* node = struct_declaration();
        if (node) node->is_public = is_public;
        return node;
    }
    if (match(TOKEN_UNION)) {
        ASTNode* node = union_declaration();
        if (node) node->is_public = is_public;
        return node;
    }
    if (match(TOKEN_ENUM)) {
        ASTNode* node = enum_declaration();
        if (node) node->is_public = is_public;
        return node;
    }
    if (match(TOKEN_LEFT_BRACE)) {
        return block_statement();
    }
    
    // Lookahead for struct initialization statement (e.g. `Point { x: 1, y: 2 };`)
    // Actually, primary expression handles `Type {` nicely if it's part of an expression.
    // However, if the line starts with `Type {` it might be parsed as an expression statement.
    // Expression statements are fine, they just need to parse the initialization.
    // But wait, what if it's just a block `{ ... }`? `TOKEN_LEFT_BRACE` handles it.
    
    // Lookahead for var vs func decl
    bool is_decl = false;
    if (check(TOKEN_LEFT_PAREN)) {
        Token next = peek_token();
        if (next.type == TOKEN_RIGHT_PAREN) {
            // '()' then likely 'ID ('
            is_decl = true;
        } else if (next.type == TOKEN_IDENTIFIER) {
            // Check for '(Type) ID' or '(Type)[Size] ID'
            // We'll peek further to see if there's a ')'
            Token next2 = peek_next_token();
            if (next2.type == TOKEN_RIGHT_PAREN) {
                is_decl = true;
            }
        }
    } else if (check(TOKEN_UNDERSCORE) || 
               (check(TOKEN_IDENTIFIER) && (
                peek_token().type == TOKEN_IDENTIFIER || 
                peek_token().type == TOKEN_STAR || 
                peek_token().type == TOKEN_LEFT_BRACKET || 
                peek_token().type == TOKEN_BANG || 
                peek_token().type == TOKEN_QUESTION))) {
        is_decl = true;
    }

    if (is_decl) {
        TypeExpr* type = parse_type();
        if (type && match(TOKEN_IDENTIFIER)) {
            Token name = parser.previous;
            if (check(TOKEN_LEFT_PAREN)) {
                ASTNode* node = function_declaration(type, name);
                if (node) node->is_public = is_public;
                return node;
            } else {
                // Variable declaration
                ASTNode* initializer = NULL;
                if (match(TOKEN_EQUAL)) {
                    initializer = expression();
                }
                consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
                ASTNode* decl = ast_new_node(parser.arena, AST_VAR_DECL, name.line);
                decl->as.var_decl.type = type;
                decl->as.var_decl.name = name;
                decl->as.var_decl.initializer = initializer;
                decl->is_public = is_public;
                return decl;
            }
        }
    }
    
    return expression_statement();
}

ASTNode* parse_source(Arena* arena, const char* source) {
    parser.arena = arena;
    parser.had_error = false;
    parser.panic_mode = false;
    
    lexer_init(source);
    advance(); 

    ASTNode* program = ast_new_node(arena, AST_PROGRAM, 1);
    ASTNode* statements[1024];
    size_t count = 0;

    while (!match(TOKEN_EOF)) {
        ASTNode* stmt = statement();
        if (stmt) {
            statements[count++] = stmt;
        } else {
            synchronize();
        }
    }

    program->as.block.statements = (ASTNode**)arena_alloc(arena, sizeof(ASTNode*) * count);
    memcpy(program->as.block.statements, statements, sizeof(ASTNode*) * count);
    program->as.block.count = count;

    return program;
}
