#include <stdio.h>
#include <string.h>
#include "bytecode.h"
#include "vm.h"
#include "error.h"

int main(int argc, char *argv[]) {
    // '-v' is only ever recognized before the '.bin' path - the moment a
    // non-'-v' argument is seen, it becomes bin_path and every remaining
    // argument, verbatim (even one that looks like a flag), is forwarded
    // to the COMPILED PROGRAM's own ParamCount/ParamStr instead of being
    // parsed as another solvm flag. Simple and unambiguous, and matches
    // this project's existing "-v always comes first" convention.
    const char *bin_path = NULL;
    int i = 1;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose_mode = 1;
        } else {
            bin_path = argv[i];
            i++;
            break;
        }
    }

    if (!bin_path) {
        printf("Usage: %s [-v] <input.bin> [program-args...]\n", argv[0]);
        return 1;
    }
    int program_argc = argc - i;
    char **program_argv = &argv[i];

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

    vm_set_program_args(bin_path, program_argc, program_argv);

    if (verbose_mode) printf("\n--- Step 2: Virtual Machine Execution ---\n");
    int exit_code = run_vm();
    fatal_error_active = 0;

    return exit_code;
}
