#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "basic.h"
#include "bytecode.h"

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
    const char *positional[2];
    int npos = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose_mode = 1;
        } else if (npos < 2) {
            positional[npos++] = argv[i];
        } else {
            npos++; // trigger the usage error below
        }
    }

    if (npos != 2) {
        printf("Usage: %s [-v] <source.bas> <output.bin>\n", argv[0]);
        return 1;
    }

    // Establishes the recovery point every fatal error in the compile
    // pipeline (lexer, parser, type checker, codegen, bytecode writer)
    // unwinds back to via fatal_abort(), instead of calling exit() from
    // deep inside library code - see src/common/error.h.
    if (setjmp(fatal_error_env)) {
        fprintf(stderr, "Compilation failed.\n");
        return 1;
    }
    fatal_error_active = 1;

    const char *source_path = positional[0];
    const char *bin_path = positional[1];
    char *source = read_file(source_path);

    if (verbose_mode) printf("\n--- Phase 1: Parsing AST ---\n");
    BasicASTNode *ast = basic_parse_ast(source, source_path);

    if (verbose_mode) printf("\n--- Phase 2: Type Checking ---\n");
    basic_type_check(ast);

    if (verbose_mode) {
        printf("\n--- Abstract Syntax Tree Visualization ---\n");
        basic_print_ast(ast, 0);
    }

    if (verbose_mode) printf("\n--- Phase 3: Code Generation ---\n");
    basic_generate_program(ast);
    basic_emit_halt();

    save_bytecode(bin_path);
    if (verbose_mode) {
        printf("[Compiler] Successfully written binary payload image to %s (%d instructions, %d symbols)\n",
               bin_path, code_idx, sym_count);
    }

    basic_free_ast(ast);
    free(source);
    fatal_error_active = 0;

    return 0;
}
