#ifndef CODEGEN_H
#define CODEGEN_H

#include "common.h"

void generate_code(ASTNode *node);

// Emits the program-terminating OP_HALT. Call this once, after
// generate_code() on the whole program's AST - generate_code() itself no
// longer emits it automatically, since a compound (begin...end) block can
// now appear nested inside if/while/repeat, not just at the program root.
void emit_halt(void);

#endif

