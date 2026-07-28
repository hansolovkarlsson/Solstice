#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bytecode.h"

void save_bytecode(const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("Failed to open binary file for writing");
        exit(1);
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
        exit(1);
    }

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "PASC", 4) != 0) {
        fprintf(stderr, "Invalid executable header image format!\n");
        fclose(f);
        exit(1);
    }

    fread(&sym_count, sizeof(int), 1, f);
    fread(sym_table, sizeof(Symbol), sym_count, f);
    fread(&code_idx, sizeof(int), 1, f);
    fread(code, sizeof(Instruction), code_idx, f);

    fclose(f);
}

