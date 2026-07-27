#ifndef COMPILER_H
#define COMPILER_H

#include "common.h"

extern Instruction code[MAX_CODE];
extern int code_idx;
extern Symbol sym_table[MAX_SYMBOLS]; // Updated structure array
extern int sym_count;

ASTNode *parse_ast(const char *source);
ASTNode *optimize_ast(ASTNode *node);
void type_check(ASTNode *node);       // New Semantic Validation Pass
void generate_code(ASTNode *node);
void free_ast(ASTNode *node);
void print_ast(ASTNode *node, int indent);

#endif
