#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm.h"
#include "error.h"

static int vm_stack[MAX_STACK];
static int vm_vars[MAX_SYMBOLS];

static inline void vm_push(int *sp, int val) {
    if (*sp >= MAX_STACK - 1) {
        fprintf(stderr, "VM Runtime Error: Stack overflow (limit is %d)\n", MAX_STACK);
        fatal_abort();
    }
    vm_stack[++(*sp)] = val;
}

static inline int vm_pop(int *sp) {
    if (*sp < 0) {
        fprintf(stderr, "VM Runtime Error: Stack underflow\n");
        fatal_abort();
    }
    return vm_stack[(*sp)--];
}

static inline int vm_var_index(int idx) {
    if (idx < 0 || idx >= sym_count) {
        fprintf(stderr, "VM Runtime Error: Variable index %d out of range (0..%d)\n", idx, sym_count - 1);
        fatal_abort();
    }
    return idx;
}

static inline int vm_str_index(int idx) {
    if (idx < 0 || idx >= string_count) {
        fprintf(stderr, "VM Runtime Error: String index %d out of range (0..%d)\n", idx, string_count - 1);
        fatal_abort();
    }
    return idx;
}

// Adds a runtime-computed string (concatenation result, or a line read via
// readln) to the pool, growing string_count if needed. Same dedup as the
// compile-time interner in parser.c, and the same hard limit (MAX_STRINGS) -
// there's no garbage collection, so a program that concatenates in a tight
// loop will eventually exhaust the pool and report a clear runtime error
// rather than overflowing anything.
static int vm_intern_string(const char *s) {
    for (int i = 0; i < string_count; i++) {
        if (strcmp(string_pool[i], s) == 0) return i;
    }
    if (string_count >= MAX_STRINGS) {
        fprintf(stderr, "VM Runtime Error: String pool exhausted (limit is %d distinct strings)\n", MAX_STRINGS);
        fatal_abort();
    }
    strcpy(string_pool[string_count], s);
    return string_count++;
}

void run_vm(void) {
    memset(vm_vars, 0, sizeof(vm_vars));
    int sp = -1;
    int ip = 0;

    while (1) {
        if (ip < 0 || ip >= code_idx) {
            fprintf(stderr, "VM Runtime Error: Instruction pointer (ip=%d) out of bounds.\n", ip);
            fatal_abort();
        }

        Instruction instr = code[ip++];
        switch (instr.op) {
            case OP_PUSH:
                vm_push(&sp, instr.arg);
                break;

            case OP_LOAD:
                vm_push(&sp, vm_vars[vm_var_index(instr.arg)]);
                break;

            case OP_STORE: {
                int val = vm_pop(&sp);
                vm_vars[vm_var_index(instr.arg)] = val;
                break;
            }

            case OP_ADD: { int b = vm_pop(&sp); int a = vm_pop(&sp); vm_push(&sp, a + b); break; }
            case OP_SUB: { int b = vm_pop(&sp); int a = vm_pop(&sp); vm_push(&sp, a - b); break; }
            case OP_MUL: { int b = vm_pop(&sp); int a = vm_pop(&sp); vm_push(&sp, a * b); break; }

            case OP_DIV: {
                int b = vm_pop(&sp);
                int a = vm_pop(&sp);
                if (b == 0) {
                    fprintf(stderr, "VM Runtime Error: Division by zero\n");
                    fatal_abort();
                }
                vm_push(&sp, a / b);
                break;
            }

            case OP_EQ:  { int b = vm_pop(&sp); int a = vm_pop(&sp); vm_push(&sp, a == b); break; }
            case OP_LT:  { int b = vm_pop(&sp); int a = vm_pop(&sp); vm_push(&sp, a < b);  break; }
            case OP_GT:  { int b = vm_pop(&sp); int a = vm_pop(&sp); vm_push(&sp, a > b);  break; }
            case OP_LTE: { int b = vm_pop(&sp); int a = vm_pop(&sp); vm_push(&sp, a <= b); break; }
            case OP_GTE: { int b = vm_pop(&sp); int a = vm_pop(&sp); vm_push(&sp, a >= b); break; }
            case OP_NEQ: { int b = vm_pop(&sp); int a = vm_pop(&sp); vm_push(&sp, a != b); break; }
            case OP_AND: { int b = vm_pop(&sp); int a = vm_pop(&sp); vm_push(&sp, a && b); break; }
            case OP_OR:  { int b = vm_pop(&sp); int a = vm_pop(&sp); vm_push(&sp, a || b); break; }
            case OP_XOR: { int b = vm_pop(&sp); int a = vm_pop(&sp); vm_push(&sp, a != b); break; }

            case OP_NEG: { int a = vm_pop(&sp); vm_push(&sp, -a); break; }
            case OP_NOT: { int a = vm_pop(&sp); vm_push(&sp, !a); break; }

            case OP_MOD: {
                int b = vm_pop(&sp);
                int a = vm_pop(&sp);
                if (b == 0) {
                    fprintf(stderr, "VM Runtime Error: Modulo by zero\n");
                    fatal_abort();
                }
                vm_push(&sp, a % b);
                break;
            }

            case OP_JMP:
                ip = instr.arg;
                break;

            case OP_JZ: {
                int cond = vm_pop(&sp);
                if (cond == 0) {
                    ip = instr.arg;
                }
                break;
            }

            case OP_PUSH_STR:
                vm_push(&sp, instr.arg); // pool index; validated when actually dereferenced
                break;

            case OP_PRINT_STR: {
                int idx = vm_str_index(vm_pop(&sp));
                printf("%s", string_pool[idx]);
                break;
            }

            case OP_SEQ: {
                int b = vm_str_index(vm_pop(&sp));
                int a = vm_str_index(vm_pop(&sp));
                vm_push(&sp, strcmp(string_pool[a], string_pool[b]) == 0);
                break;
            }

            case OP_SCONCAT: {
                int b = vm_str_index(vm_pop(&sp));
                int a = vm_str_index(vm_pop(&sp));
                char buf[MAX_STRING_LEN];
                int written = snprintf(buf, sizeof(buf), "%s%s", string_pool[a], string_pool[b]);
                if (written < 0 || (size_t)written >= sizeof(buf)) {
                    fprintf(stderr, "VM Runtime Error: Concatenated string too long (limit is %d characters)\n",
                            MAX_STRING_LEN - 1);
                    fatal_abort();
                }
                vm_push(&sp, vm_intern_string(buf));
                break;
            }

            case OP_HALT:
                if (verbose_mode) {
                    printf("\n--- Final Runtime Execution Output Results ---\n");
                    for (int i = 0; i < sym_count; i++) {
                        if (sym_table[i].name[0] == '_' && sym_table[i].name[1] == '_') {
                            continue; // hidden compiler-internal variable (e.g. a for-loop's cached bound)
                        }
                        if (sym_table[i].type == TYPE_STRING) {
                            int idx = vm_vars[i];
                            if (idx >= 0 && idx < string_count) {
                                printf("%s = %s\n", sym_table[i].name, string_pool[idx]);
                            } else {
                                printf("%s = <invalid string index %d>\n", sym_table[i].name, idx);
                            }
                        } else {
                            printf("%s = %d\n", sym_table[i].name, vm_vars[i]);
                        }
                    }
                }
                return;

            case OP_PRINT: {
                int val = vm_pop(&sp);
                printf("%d", val);
                break;
            }

            case OP_NEWLINE:
                printf("\n");
                break;

            case OP_READ: {
                int idx = vm_var_index(instr.arg);
                printf("> "); // Prompt user

                if (sym_table[idx].type == TYPE_STRING) {
                    char line[MAX_STRING_LEN];
                    if (!fgets(line, sizeof(line), stdin)) {
                        fprintf(stderr, "VM Runtime Error: Invalid string input\n");
                        fatal_abort();
                    }
                    size_t len = strlen(line);
                    if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
                    vm_vars[idx] = vm_intern_string(line);
                } else {
                    int input_val;
                    if (scanf("%d", &input_val) != 1) {
                        fprintf(stderr, "VM Runtime Error: Invalid integer input\n");
                        fatal_abort();
                    }
                    int c;
                    while ((c = getchar()) != '\n' && c != EOF) { } // flush rest of the line
                    if (sym_table[idx].type == TYPE_BOOLEAN && input_val != 0 && input_val != 1) {
                        fprintf(stderr, "VM Runtime Error: readln expected a boolean value (0 or 1) for '%s', got %d\n",
                                sym_table[idx].name, input_val);
                        fatal_abort();
                    }
                    vm_vars[idx] = input_val;
                }
                break;
            }

            default:
                fprintf(stderr, "VM Runtime Error: Invalid opcode encountered (op=%d) at ip=%d\n", instr.op, ip - 1);
                fatal_abort();
        }
    }
}

