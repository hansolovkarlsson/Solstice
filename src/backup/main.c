#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"
#include "bytecode.h"
#include "vm.h"
#include "error.h"

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("Failed to open source file"); fatal_abort(); }
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = malloc(length + 1);
    if (!buffer) { fprintf(stderr, "Allocation failure\n"); fatal_abort(); }
    fread(buffer, 1, length, f);
    buffer[length] = '\0';
    fclose(f);
    return buffer;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage:\n");
        printf("  Compile: %s -c <source.pas> <output.bin>\n", argv[0]);
        printf("  Execute: %s -r <input.bin>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-c") == 0) {
        // Establishes the recovery point every fatal error in the compile
        // pipeline (lexer, parser, type checker, optimizer, codegen,
        // bytecode writer) unwinds back to via fatal_abort(), instead of
        // calling exit() from deep inside library code.
        if (setjmp(fatal_error_env)) {
            fprintf(stderr, "Compilation failed.\n");
            return 1;
        }
        fatal_error_active = 1;

        const char *source_path = argv[2];
        const char *bin_path = argv[3];
        char *source = read_file(source_path);

        printf("\n--- Phase 1: Parsing AST ---\n");
        ASTNode *ast = parse_ast(source, source_path);

        printf("\n--- Phase 2: Type Checking ---\n");
        type_check(ast);

        printf("\n--- Phase 3: Optimizing AST ---\n");
        ast = optimize_ast(ast);
        ast = eliminate_dead_code(ast);

        printf("\n--- Abstract Syntax Tree Visualization ---\n");
        print_ast(ast, 0);

        printf("\n--- Phase 4: Code Generation ---\n");
        generate_code(ast);

        save_bytecode(bin_path);
        printf("[Compiler] Successfully written binary payload image to %s (%d instructions, %d symbols)\n", 
               bin_path, code_idx, sym_count);

        free_ast(ast);
        free(source);
        fatal_error_active = 0;
    } else if (strcmp(argv[1], "-r") == 0) {
        // Same pattern for the load+execute pipeline (bytecode loader, VM).
        if (setjmp(fatal_error_env)) {
            fprintf(stderr, "Execution failed.\n");
            return 1;
        }
        fatal_error_active = 1;

        const char *bin_path = argv[2];

        printf("\n--- Step 1: Loading Binary Executable Image ---\n");
        load_bytecode(bin_path);
        printf("[Bytecode Module] Loaded executable successfully (%d instructions, %d symbols)\n", code_idx, sym_count);

        printf("\n--- Step 2: Virtual Machine Execution ---\n");
        run_vm();
        fatal_error_active = 0;
    } else {
        fprintf(stderr, "Unknown flag '%s'\n", argv[1]);
        return 1;
    }

    return 0;
}

