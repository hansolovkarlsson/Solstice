#include "common.h"

static int vm_stack[256];
static int sp = -1;
static int memory[MAX_VARS];

void execute_vm(Instruction *instructions, int count) {
    sp = -1;
    for (int pc = 0; pc < count; pc++) {
        Instruction inst = instructions[pc];
        switch (inst.op) {
            case OP_NOP:
                break;
            case OP_PUSH_INT:
            case OP_PUSH_BOOL:
                vm_stack[++sp] = inst.arg;
                break;
            case OP_ADD: {
                int b = vm_stack[sp--];
                int a = vm_stack[sp--];
                vm_stack[++sp] = a + b;
                break;
            }
            case OP_SUB: {
                int b = vm_stack[sp--];
                int a = vm_stack[sp--];
                vm_stack[++sp] = a - b;
                break;
            }
            case OP_MUL: {
                int b = vm_stack[sp--];
                int a = vm_stack[sp--];
                vm_stack[++sp] = a * b;
                break;
            }
            case OP_DIV: {
                int b = vm_stack[sp--];
                int a = vm_stack[sp--];
                if (b == 0) {
                    fprintf(stderr, "VM Runtime Error: Division by zero\n");
                    exit(1);
                }
                vm_stack[++sp] = a / b;
                break;
            }
            case OP_MOD: {
                int b = vm_stack[sp--];
                int a = vm_stack[sp--];
                if (b == 0) {
                    fprintf(stderr, "VM Runtime Error: Division by zero\n");
                    exit(1);
                }
                vm_stack[++sp] = a % b;
                break;
            }
            case OP_AND: {
                int b = vm_stack[sp--];
                int a = vm_stack[sp--];
                vm_stack[++sp] = a && b;
                break;
            }
            case OP_OR: {
                int b = vm_stack[sp--];
                int a = vm_stack[sp--];
                vm_stack[++sp] = a || b;
                break;
            }
            case OP_XOR: {
                int b = vm_stack[sp--];
                int a = vm_stack[sp--];
                vm_stack[++sp] = (a && !b) || (!a && b);
                break;
            }
            case OP_NOT: {
                int a = vm_stack[sp--];
                vm_stack[++sp] = !a;
                break;
            }
            case OP_EQ: {
                int b = vm_stack[sp--];
                int a = vm_stack[sp--];
                vm_stack[++sp] = (a == b);
                break;
            }
            case OP_NEQ: {
                int b = vm_stack[sp--];
                int a = vm_stack[sp--];
                vm_stack[++sp] = (a != b);
                break;
            }
            case OP_LT: {
                int b = vm_stack[sp--];
                int a = vm_stack[sp--];
                vm_stack[++sp] = (a < b);
                break;
            }
            case OP_LTE: {
                int b = vm_stack[sp--];
                int a = vm_stack[sp--];
                vm_stack[++sp] = (a <= b);
                break;
            }
            case OP_GT: {
                int b = vm_stack[sp--];
                int a = vm_stack[sp--];
                vm_stack[++sp] = (a > b);
                break;
            }
            case OP_GTE: {
                int b = vm_stack[sp--];
                int a = vm_stack[sp--];
                vm_stack[++sp] = (a >= b);
                break;
            }
            case OP_LOAD:
                vm_stack[++sp] = memory[inst.arg];
                break;
            case OP_STORE:
                memory[inst.arg] = vm_stack[sp--];
                break;

            case OP_WRITE_INT:
                printf("%d", vm_stack[sp--]);
                break;

            case OP_WRITE_BOOL:
                printf("%s", vm_stack[sp--] ? "true" : "false");
                break;

            case OP_WRITE_STR:
                if (inst.arg >= 0 && inst.arg < string_pool_count && string_pool[inst.arg]) {
                    printf("%s", string_pool[inst.arg]);
                } else {
                    printf("(null string)");
                }
                break;

            case OP_PRINT_NEWLINE:
                printf("\n");
                break;
                
            case OP_READ: {
                int val;
                if (scanf("%d", &val) != 1) {
                    fprintf(stderr, "VM Runtime Error: Invalid input\n");
                    exit(1);
                }
                vm_stack[++sp] = val;
                break;
            }
            case OP_HALT:
                return;
            default:
                break;
        }
    }
}

