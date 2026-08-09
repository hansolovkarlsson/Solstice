#ifndef PARSER_H
#define PARSER_H

#include "common.h"

const char *get_current_filename(void);
void set_current_filename(const char *f);
ASTNode *create_node(NodeType type);
ASTNode *parse_ast(const char *source, const char *filename);
void free_ast(ASTNode *node);
int class_type_is_subtype_of(DataType sub, DataType target);

#endif

