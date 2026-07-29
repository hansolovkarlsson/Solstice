step6_save_bytecode.md

# Prompt

Create a bytecode output module to save compiled binaries directly to a file

# Response

To save compiled bytecode directly to a binary file, we need to create a module that serializes the Instruction array and the variable state metadata, then reads it back directly into the VM. This bypasses the compilation phase entirely on subsequent runs.
Here is the implementation of the bytecode output and loading module.
------------------------------
## 1. Create bytecode.h
This file defines the format of our compiled binary files and handles file serialization and deserialization. It uses a magic number header (PASC) to prevent reading incompatible binaries. [1] 

#ifndef BYTECODE_H#define BYTECODE_H
#include "common.h"
// Header structure for binary validationtypedef struct {
    char magic[4];     // Always "PASC"
    int version;       // Internal versioning tracker
    int sym_count;     // Total variables registered
    int code_idx;      // Size of compiled instruction array
} BytecodeHeader;
int save_bytecode(const char *filename);int load_bytecode(const char *filename);
#endif

------------------------------
## 2. Create bytecode.c
This engine writes the binary image out to disk and reloads it cleanly back into global compiler memory structures.

#include <stdio.h>#include <string.h>#include "bytecode.h"#include "compiler.h"
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

    // Write symbol names to allow variable reporting on output dumps
    fwrite(sym_table, sizeof(char) * MAX_NAME, sym_count, file);

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

    fread(sym_table, sizeof(char) * MAX_NAME, sym_count, file);
    fread(code, sizeof(Instruction), code_idx, file);

    fclose(file);
    printf("[Bytecode Module] Loaded executable successfully (%d instructions, %d symbols)\n", code_idx, sym_count);
    return 1;
}

------------------------------
## 3. Update main.c
We adjust main.c to act like a real production compiler utility tool.

* Passing -c source.pas output.bin targets Compilation Mode (Outputs raw bytecode files).
* Passing -r output.bin targets Execution Mode (Runs the VM directly off the bytecode file, bypasses parsing entirely).

#include <stdio.h>#include <stdlib.h>#include <string.h>#include "compiler.h"#include "bytecode.h"#include "vm.h"
char *read_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Could not open file %s\n", filename);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = malloc(length + 1);
    if (!buffer) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        fclose(file);
        return NULL;
    }
    size_t read_bytes = fread(buffer, 1, length, file);
    buffer[read_bytes] = '\0';
    fclose(file);
    return buffer;
}
int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Pascal Tools Usage:\n");
        printf("  Compile code: %s -c <src.pas> <output.bin>\n", argv[0]);
        printf("  Run bytecode: %s -r <input.bin>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-c") == 0 && argc == 4) {
        char *pascal_program = read_file(argv[2]);
        if (!pascal_program) return 1;

        printf("--- Step 1: Parsing and Optimizing AST ---\n");
        ASTNode *ast = parse_ast(pascal_program);
        ast = optimize_ast(ast);

        printf("\n--- Step 2: Generating Code ---\n");
        generate_code(ast);
        code[code_idx++] = (Instruction){OP_HALT, 0};

        printf("\n--- Step 3: Archiving Bytecode Output ---\n");
        save_bytecode(argv[3]);

        free_ast(ast);
        free(pascal_program);
        
    } else if (strcmp(argv[1], "-r") == 0) {
        printf("--- Step 1: Loading Binary Executable Image ---\n");
        if (!load_bytecode(argv[2])) return 1;

        printf("\n--- Step 2: Virtual Machine Execution ---\n");
        run_vm();

        printf("\n--- Final Runtime Execution Output Results ---\n");
        for (int i = 0; i < sym_count; i++) {
            printf("%s = %d\n", sym_table[i], vm_vars[i]);
        }
    } else {
        printf("Error: Unrecognized operational arguments flag configurations.\n");
        return 1;
    }

    return 0;
}

------------------------------
## Compilation and Execution Guide

   1. Link and compile all files together: [2] 

gcc main.c lexer.c compiler.c bytecode.c vm.c -o pascal_tool


   1. Compile your source text file down into a target bytecode binary object: [3] 

./pascal_tool -c test.pas app.bin


   1. Run your program directly from the compiled binary payload anytime without needing the source compiler or code files present:

./pascal_tool -r app.bin

If you are ready to expand the functionality of your compiler toolchain, I can help you:

* Implement a Bytecode Disassembler to print instructions inside binary objects
* Add Global Type Check Validation for handling different variable categories


[1] [https://videlais.com](https://videlais.com/2021/02/24/using-binaryformatter-in-unity-to-save-and-load-game-data/)
[2] [https://www.linkedin.com](https://www.linkedin.com/pulse/starting-developerhacker-desk-win32-api-traces-inside-hassan-sbeyti)
[3] [https://habr.com](https://habr.com/en/articles/757998/)
