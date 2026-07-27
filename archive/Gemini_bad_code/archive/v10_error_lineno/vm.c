#include "vm.h"
#include "compiler.h"

int vm_vars[MAX_SYMBOLS];
static int vm_stack[MAX_STACK];

void run_vm(void) {
    int sp = -1;
    int ip = 0;

    while (1) {
        Instruction instr = code[ip++];
        switch (instr.op) {
            case OP_PUSH:  vm_stack[++sp] = instr.arg; break;
            case OP_LOAD:  vm_stack[++sp] = vm_vars[instr.arg]; break;
            case OP_STORE: vm_vars[instr.arg] = vm_stack[sp--]; break;
            case OP_ADD:   vm_stack[sp - 1] += vm_stack[sp]; sp--; break;
            case OP_SUB:   vm_stack[sp - 1] -= vm_stack[sp]; sp--; break;
            case OP_MUL:   vm_stack[sp - 1] *= vm_stack[sp]; sp--; break;
            case OP_DIV:   vm_stack[sp - 1] /= vm_stack[sp]; sp--; break;
            case OP_HALT:  return;
        }
    }
}
