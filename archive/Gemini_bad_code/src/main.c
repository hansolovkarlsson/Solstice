#include "common.h"

// Forward declaration if not already present in common.h
ASTNode *eliminate_dead_code(ASTNode *node);

static char *read_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error opening file: %s\n", filename);
        exit(1);
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = (char *)malloc(size + 1);
    if (!buffer) {
        fclose(f);
        fprintf(stderr, "Memory allocation error\n");
        exit(1);
    }

    fread(buffer, 1, size, f);
    buffer[size] = '\0';
    fclose(f);
    return buffer;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  Compile: %s -c <source.pas> <output.bin>\n", argv[0]);
        fprintf(stderr, "  Run:     %s -r <input.bin>\n", argv[0]);
        return 1;
    }

    atexit(free_string_pool);

    const char *mode = argv[1];

    // --- COMPILE MODE (-c) ---
    if (strcmp(mode, "-c") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Compile mode requires output binary filename.\n");
            fprintf(stderr, "Usage: %s -c <source.pas> <output.bin>\n", argv[0]);
            return 1;
        }

        const char *source_file = argv[2];
        const char *bin_file = argv[3];

        char *source_code = read_file(source_file);

        printf("--- Phase 1: Parsing AST ---\n");
        lexer_init(source_code);
        ASTNode *root = parse_ast();
        print_ast(root, 0);

        printf("--- Phase 2: Type Checking ---\n");
        check_types(root);

        printf("--- Phase 3: Optimizing AST (Constant Folding) ---\n");
        root = optimize_ast(root);

        printf("--- Phase 4: Dead Code Elimination ---\n");
        root = eliminate_dead_code(root);

        printf("--- Phase 5: Code Generation ---\n");
        generate_code(root);

        printf("--- Phase 6: Saving Bytecode -> %s ---\n", bin_file);
        save_bytecode(bin_file);

        free(source_code);
        printf("Compilation successful.\n");
        return 0;
    }

    // --- RUN MODE (-r) ---
    if (strcmp(mode, "-r") == 0) {
        const char *bin_file = argv[2];

        Instruction *instructions = NULL;
        int count = load_bytecode(bin_file, &instructions);

        printf("--- Virtual Machine Execution ---\n");
        execute_vm(instructions, count);
        return 0;
    }

    fprintf(stderr, "Unknown mode: %s\n", mode);
    return 1;
}