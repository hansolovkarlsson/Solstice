#ifndef PARSER_H
#define PARSER_H

#include "common.h"

const char *get_current_filename(void);
ASTNode *create_node(NodeType type);
ASTNode *parse_ast(const char *source, const char *filename);
void free_ast(ASTNode *node);

#endif

