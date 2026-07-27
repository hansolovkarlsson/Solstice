#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bytecode.h"
#include "error.h"

void save_bytecode(const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("Failed to open binary file for writing");
        fatal_abort();
    }

    char magic[4] = {'P', 'A', 'S', 'C'};
    fwrite(magic, 1, 4, f);
    fwrite(&sym_count, sizeof(int), 1, f);
    fwrite(sym_table, sizeof(Symbol), sym_count, f);
    fwrite(&code_idx, sizeof(int), 1, f);
    fwrite(code, sizeof(Instruction), code_idx, f);

    fclose(f);
}

void load_bytecode(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("Failed to open bytecode image");
        fatal_abort();
    }

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "PASC", 4) != 0) {
        fprintf(stderr, "Invalid executable header image format!\n");
        fclose(f);
        fatal_abort();
    }

    if (fread(&sym_count, sizeof(int), 1, f) != 1) {
        fprintf(stderr, "Invalid executable image: truncated symbol count\n");
        fclose(f);
        fatal_abort();
    }
    if (sym_count < 0 || sym_count > MAX_SYMBOLS) {
        fprintf(stderr, "Invalid executable image: symbol count %d out of range (max %d)\n", sym_count, MAX_SYMBOLS);
        fclose(f);
        fatal_abort();
    }
    if (fread(sym_table, sizeof(Symbol), sym_count, f) != (size_t)sym_count) {
        fprintf(stderr, "Invalid executable image: truncated symbol table\n");
        fclose(f);
        fatal_abort();
    }

    if (fread(&code_idx, sizeof(int), 1, f) != 1) {
        fprintf(stderr, "Invalid executable image: truncated instruction count\n");
        fclose(f);
        fatal_abort();
    }
    if (code_idx < 0 || code_idx > MAX_CODE) {
        fprintf(stderr, "Invalid executable image: instruction count %d out of range (max %d)\n", code_idx, MAX_CODE);
        fclose(f);
        fatal_abort();
    }
    if (fread(code, sizeof(Instruction), code_idx, f) != (size_t)code_idx) {
        fprintf(stderr, "Invalid executable image: truncated bytecode\n");
        fclose(f);
        fatal_abort();
    }

    fclose(f);
}

