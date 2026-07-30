#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "common.h"

ASTNode *optimize_ast(ASTNode *node);
ASTNode *eliminate_dead_code(ASTNode *node); // single-tree convenience (reset+mark+sweep)
void dce_reset(void);
void dce_mark(ASTNode *node);
ASTNode *dce_sweep(ASTNode *node);

#endif

