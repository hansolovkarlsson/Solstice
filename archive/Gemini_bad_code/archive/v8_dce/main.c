#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"
#include "bytecode.h"
#include "vm.h"

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
    if (argc < 2) {
        printf("Pascal Tools Usage:\n");
        printf("  Compile code: %s -c <src.pas> <output.bin>\n", argv[0]);
        printf("  Run bytecode: %s -r <input.bin>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-c") == 0 && argc == 4) {
        char *pascal_program = read_file(argv[2]);
        if (!pascal_program) return 1;

        printf("--- Phase 1: Parsing AST ---\n");
        ASTNode *ast = parse_ast(pascal_program);

        printf("\n--- Phase 2: Type Validation Checking ---\n");
        type_check(ast);

        printf("\n--- Phase 3: Optimizing AST Tree ---\n");
        ast = optimize_ast(ast);            // Run constant folding first
        ast = eliminate_dead_code(ast);     // Run dead code elimination second

        printf("\n--- Visual AST Representation (Optimized) ---\n");
        print_ast(ast, 0);

        printf("\n--- Step 4: Generating Code ---\n");
        generate_code(ast);
        code[code_idx++] = (Instruction){OP_HALT, 0};

        printf("\n--- Step 5: Archiving Bytecode Output ---\n");
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
            printf("%s = %d\n", sym_table[i].name, vm_vars[i]);
        }
    } else {
        printf("Error: Unrecognized operational arguments flag configurations.\n");
        return 1;
    }

    return 0;
}
