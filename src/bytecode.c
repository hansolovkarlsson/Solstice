#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bytecode.h"
#include "error.h"

// The in-memory bytecode program and symbol table. Populated either by the
// compiler front end (parser.c fills sym_table[]/sym_count as it parses
// declarations, codegen.c fills code[]/code_idx) and then written out by
// save_bytecode(), or read directly from disk by load_bytecode() for the
// VM to execute. Living here rather than in parser.c means the VM binary
// can link bytecode.c + vm.c without pulling in any compiler-frontend code.
Instruction code[MAX_CODE];
int code_idx = 0;

Symbol sym_table[MAX_SYMBOLS];
int sym_count = 0;

char string_pool[MAX_STRINGS][MAX_STRING_LEN];
int string_count = 0;

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

    fwrite(&string_count, sizeof(int), 1, f);
    fwrite(string_pool, sizeof(string_pool[0]), string_count, f);

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

    if (fread(&string_count, sizeof(int), 1, f) != 1) {
        fprintf(stderr, "Invalid executable image: truncated string count\n");
        fclose(f);
        fatal_abort();
    }
    if (string_count < 0 || string_count > MAX_STRINGS) {
        fprintf(stderr, "Invalid executable image: string count %d out of range (max %d)\n", string_count, MAX_STRINGS);
        fclose(f);
        fatal_abort();
    }
    if (fread(string_pool, sizeof(string_pool[0]), string_count, f) != (size_t)string_count) {
        fprintf(stderr, "Invalid executable image: truncated string pool\n");
        fclose(f);
        fatal_abort();
    }

    fclose(f);
}

