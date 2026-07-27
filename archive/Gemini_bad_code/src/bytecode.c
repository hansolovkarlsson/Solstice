#include "common.h"

// Expose internal code buffer from codegen.c if needed, 
// or accept instructions passed in.
extern Instruction code[1024];
extern int code_count;

void save_bytecode(const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Error opening binary file for writing: %s\n", filename);
        exit(1);
    }

    // 1. Write Symbol Table
    fwrite(&symbol_count, sizeof(int), 1, f);
    fwrite(symbol_table, sizeof(Symbol), symbol_count, f);

    // 2. Write String Pool
    fwrite(&string_pool_count, sizeof(int), 1, f);
    for (int i = 0; i < string_pool_count; i++) {
        int len = string_pool[i] ? (int)strlen(string_pool[i]) : 0;
        fwrite(&len, sizeof(int), 1, f);
        if (len > 0) {
            fwrite(string_pool[i], sizeof(char), len, f);
        }
    }

    // 3. Write Compiled Instructions
    fwrite(&code_count, sizeof(int), 1, f);
    fwrite(code, sizeof(Instruction), code_count, f);

    fclose(f);
}

int load_bytecode(const char *filename, Instruction **out_instructions) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error opening binary file for reading: %s\n", filename);
        exit(1);
    }

    // 1. Read Symbol Table
    fread(&symbol_count, sizeof(int), 1, f);
    fread(symbol_table, sizeof(Symbol), symbol_count, f);

    // 2. Read String Pool
    free_string_pool(); // Clear existing pool before loading
    fread(&string_pool_count, sizeof(int), 1, f);
    for (int i = 0; i < string_pool_count; i++) {
        int len = 0;
        fread(&len, sizeof(int), 1, f);
        if (len > 0) {
            string_pool[i] = (char *)malloc(len + 1);
            fread(string_pool[i], sizeof(char), len, f);
            string_pool[i][len] = '\0';
        } else {
            string_pool[i] = NULL;
        }
    }

    // 3. Read Instructions
    fread(&code_count, sizeof(int), 1, f);
    fread(code, sizeof(Instruction), code_count, f);

    fclose(f);

    *out_instructions = code;
    return code_count;
}

