#ifndef BYTECODE_H
#define BYTECODE_H

#include "common.h"

// Header structure for binary validation
typedef struct {
    char magic[4];     // Always "PASC"
    int version;       // Internal versioning tracker
    int sym_count;     // Total variables registered
    int code_idx;      // Size of compiled instruction array
} BytecodeHeader;

int save_bytecode(const char *filename);
int load_bytecode(const char *filename);

#endif
