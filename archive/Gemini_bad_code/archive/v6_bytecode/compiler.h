#ifndef COMPILER_H
#define COMPILER_H

#include "common.h"

extern Instruction code[MAX_CODE];
extern int code_idx;
extern char sym_table[MAX_SYMBOLS][MAX_NAME];
extern int sym_count;

ASTNode *parse_ast(const char *source);
ASTNode *optimize_ast(ASTNode *node);
void generate_code(ASTNode *node);
void free_ast(ASTNode *node);

// Add this line
void print_ast(ASTNode *node, int indent);

#endif
