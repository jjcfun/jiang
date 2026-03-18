#ifndef JIANG_PARSER_H
#define JIANG_PARSER_H

#include "lexer.h"
#include "ast.h"
#include "arena.h"

// Parse the source text using the given arena and return the root ASTNode
ASTNode* parse_source(Arena* arena, const char* source);

#endif // JIANG_PARSER_H
