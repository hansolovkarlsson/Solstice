#ifndef VM_H
#define VM_H

#include "common.h"

void run_vm(void);

// Interns 'program_name' (the .bin path) as command-line argument 0,
// then each of argv[0..argc-1] as arguments 1..argc, into string_pool[]
// - backing ParamCount/ParamStr. Must be called after load_bytecode()
// (so the compiled program's own static strings keep their original
// indices) and before run_vm(). argc excludes argument 0, matching
// real Pascal's own ParamCount convention.
void vm_set_program_args(const char *program_name, int argc, char **argv);

#endif

