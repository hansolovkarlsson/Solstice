#include <stdio.h>
#include <string.h>
#include "bytecode.h"
#include "vm.h"
#include "error.h"

int main(int argc, char *argv[]) {
    const char *bin_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose_mode = 1;
        } else if (!bin_path) {
            bin_path = argv[i];
        } else {
            bin_path = NULL; // trigger usage error below (too many args)
            break;
        }
    }

    if (!bin_path) {
        printf("Usage: %s [-v] <input.bin>\n", argv[0]);
        return 1;
    }

    // Same recovery pattern as the compiler binary, for the load+execute
    // pipeline (bytecode loader, VM).
    if (setjmp(fatal_error_env)) {
        fprintf(stderr, "Execution failed.\n");
        return 1;
    }
    fatal_error_active = 1;

    if (verbose_mode) printf("\n--- Step 1: Loading Binary Executable Image ---\n");
    load_bytecode(bin_path);
    if (verbose_mode) {
        printf("[Bytecode Module] Loaded executable successfully (%d instructions, %d symbols)\n", code_idx, sym_count);
    }

    if (verbose_mode) printf("\n--- Step 2: Virtual Machine Execution ---\n");
    run_vm();
    fatal_error_active = 0;

    return 0;
}
