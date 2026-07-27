#include <stdio.h>
#include "bytecode.h"
#include "vm.h"
#include "error.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <input.bin>\n", argv[0]);
        return 1;
    }

    // Same recovery pattern as the compiler binary, for the load+execute
    // pipeline (bytecode loader, VM).
    if (setjmp(fatal_error_env)) {
        fprintf(stderr, "Execution failed.\n");
        return 1;
    }
    fatal_error_active = 1;

    const char *bin_path = argv[1];

    printf("\n--- Step 1: Loading Binary Executable Image ---\n");
    load_bytecode(bin_path);
    printf("[Bytecode Module] Loaded executable successfully (%d instructions, %d symbols)\n", code_idx, sym_count);

    printf("\n--- Step 2: Virtual Machine Execution ---\n");
    run_vm();
    fatal_error_active = 0;

    return 0;
}
