#include <stdio.h>
#include <stdlib.h>
#include "compiler.h"
#include "bytecode.h"
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
    if (argc != 3) {
        printf("Usage: %s <source.pas> <output.bin>\n", argv[0]);
        return 1;
    }

    // Establishes the recovery point every fatal error in the compile
    // pipeline (lexer, parser, type checker, optimizer, codegen, bytecode
    // writer) unwinds back to via fatal_abort(), instead of calling exit()
    // from deep inside library code.
    if (setjmp(fatal_error_env)) {
        fprintf(stderr, "Compilation failed.\n");
        return 1;
    }
    fatal_error_active = 1;

    const char *source_path = argv[1];
    const char *bin_path = argv[2];
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

    return 0;
}
