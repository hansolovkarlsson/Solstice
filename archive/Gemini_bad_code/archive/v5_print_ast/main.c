#include <stdio.h>
#include <stdlib.h>
#include "compiler.h"
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
        printf("Usage: %s <source_file.pas>\n", argv[0]);
        return 1;
    }

    char *pascal_program = read_file(argv[1]);
    if (!pascal_program) return 1;

    printf("--- Phase 1: Parsing AST ---\n");
    ASTNode *ast = parse_ast(pascal_program);
    
    printf("\n--- Visual AST Representation (Unoptimized) ---\n");
    print_ast(ast, 0);

    printf("\n--- Phase 2: Optimizing AST ---\n");
    ast = optimize_ast(ast);

    printf("\n--- Visual AST Representation (Optimized) ---\n");
    print_ast(ast, 0);

    printf("\n--- Phase 3: Emitting VM Bytecode ---\n");
    generate_code(ast);
    code[code_idx++] = (Instruction){OP_HALT, 0};
    printf("Compilation completed. Total Instructions: %d\n", code_idx);

    printf("\n--- Phase 4: Virtual Machine execution ---\n");
    run_vm();

    printf("\n--- Execution Output Results ---\n");
    for (int i = 0; i < sym_count; i++) {
        printf("%s = %d\n", sym_table[i], vm_vars[i]);
    }

    free_ast(ast);
    free(pascal_program);
    return 0;
}

