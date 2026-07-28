#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "common.h"

ASTNode *optimize_ast(ASTNode *node);
ASTNode *eliminate_dead_code(ASTNode *node);

#endif

