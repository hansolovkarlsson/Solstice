#ifndef CODEGEN_H
#define CODEGEN_H

#include "common.h"

void generate_code(ASTNode *node);

// The top-level entry point: emits every declared procedure's body first
// (each preceded by a JMP that skips straight to the main program, so
// falling off the end of the code never accidentally runs into a
// procedure body), then the main program itself. Call this instead of
// generate_code() directly for a whole program - generate_code() remains
// the internal recursive workhorse, and is what procedure/main bodies are
// each generated with.
void generate_program(ASTNode *main_body);

// Emits the program-terminating OP_HALT. Call this once, after
// generate_program() - generate_code() itself no longer emits it
// automatically, since a compound (begin...end) block can appear nested
// inside if/while/repeat/a procedure body, not just at the program root.
void emit_halt(void);

#endif

