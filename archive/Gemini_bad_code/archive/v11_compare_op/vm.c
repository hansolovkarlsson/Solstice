#include "vm.h"
#include "compiler.h"
#include <stdio.h>

int vm_vars[MAX_SYMBOLS];
static int vm_stack[MAX_STACK];

void run_vm(void) {
    int sp = -1;
    int ip = 0;

    while (1) {
        if (ip < 0 || ip >= code_idx) {
            fprintf(stderr, "VM Runtime Error: Instruction pointer (ip=%d) out of bounds.\n", ip);
            exit(1);
        }

        Instruction instr = code[ip++];
        switch (instr.op) {
            case OP_PUSH:  
                if (sp >= MAX_STACK - 1) { fprintf(stderr, "VM Stack Overflow\n"); exit(1); }
                vm_stack[++sp] = instr.arg; 
                break;
            case OP_LOAD:  vm_stack[++sp] = vm_vars[instr.arg]; break;
            case OP_STORE: vm_vars[instr.arg] = vm_stack[sp--]; break;
            case OP_ADD:   vm_stack[sp - 1] += vm_stack[sp]; sp--; break;
            case OP_SUB:   vm_stack[sp - 1] -= vm_stack[sp]; sp--; break;
            case OP_MUL:   vm_stack[sp - 1] *= vm_stack[sp]; sp--; break;
            case OP_DIV:   
                if (vm_stack[sp] == 0) { fprintf(stderr, "VM Division by Zero Error\n"); exit(1); }
                vm_stack[sp - 1] /= vm_stack[sp]; sp--; break;
            case OP_EQ:    vm_stack[sp - 1] = (vm_stack[sp - 1] == vm_stack[sp]); sp--; break;
            case OP_LT:    vm_stack[sp - 1] = (vm_stack[sp - 1] <  vm_stack[sp]); sp--; break;
            case OP_GT:    vm_stack[sp - 1] = (vm_stack[sp - 1] >  vm_stack[sp]); sp--; break;
            case OP_HALT:  return;

            default:
                fprintf(stderr, "VM Runtime Error: Invalid opcode encountered (op=%d) at ip=%d\n", instr.op, ip - 1);
                exit(1);
        }
    }
}