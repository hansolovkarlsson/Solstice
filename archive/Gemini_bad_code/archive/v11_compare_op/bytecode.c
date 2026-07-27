#include <stdio.h>
#include <string.h>
#include "bytecode.h"
#include "compiler.h"

int save_bytecode(const char *filename) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        fprintf(stderr, "Error: Could not open file %s for binary write\n", filename);
        return 0;
    }

    BytecodeHeader header;
    memcpy(header.magic, "PASC", 4);
    header.version = 1;
    header.sym_count = sym_count;
    header.code_idx = code_idx;

    // Write metadata tracking information
    fwrite(&header, sizeof(BytecodeHeader), 1, file);

    // Write complete Symbol structs (name + type)
    fwrite(sym_table, sizeof(Symbol), sym_count, file);

    // Write instruction code block chunks directly
    fwrite(code, sizeof(Instruction), code_idx, file);

    fclose(file);
    printf("[Bytecode Module] Binary successfully saved to %s\n", filename);
    return 1;
}

int load_bytecode(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: Could not load bytecode binary %s\n", filename);
        return 0;
    }

    BytecodeHeader header;
    if (fread(&header, sizeof(BytecodeHeader), 1, file) != 1) {
        fprintf(stderr, "Error: Corrupted bytecode header format\n");
        fclose(file);
        return 0;
    }

    if (memcmp(header.magic, "PASC", 4) != 0) {
        fprintf(stderr, "Error: Invalid file format magic signatures\n");
        fclose(file);
        return 0;
    }

    // Restore state details back inside internal module layers
    sym_count = header.sym_count;
    code_idx = header.code_idx;

    // Read complete Symbol structs back into memory
    fread(sym_table, sizeof(Symbol), sym_count, file);
    fread(code, sizeof(Instruction), code_idx, file);

    fclose(file);
    printf("[Bytecode Module] Loaded executable successfully (%d instructions, %d symbols)\n", code_idx, sym_count);
    return 1;
}