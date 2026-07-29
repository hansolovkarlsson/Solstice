#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm.h"
#include "error.h"

static int vm_stack[MAX_STACK];
static int vm_vars[MAX_SYMBOLS];
static int vm_array_mem[MAX_ARRAY_MEM];
static int vm_frame_stack[MAX_FRAME_STACK]; // local variable slots for
                                             // every active call, stacked

// Everything RET needs to fully restore the caller's context: not just
// where to jump back to, but also its frame pointer and where the frame
// stack's top was - restoring saved_frame_sp deallocates the returning
// call's entire frame in one step, regardless of how many locals it had.
typedef struct {
    int return_addr;
    int saved_fp;
    int saved_frame_sp;
} CallRecord;

static CallRecord vm_call_stack[MAX_CALL_DEPTH]; // kept separate from
                                           // vm_stack so a procedure's
                                           // operand-stack use can never
                                           // clobber a return address (or
                                           // frame state) or vice versa.

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

static inline void vm_call_push(int *call_sp, int return_addr, int saved_fp, int saved_frame_sp) {
    if (*call_sp >= MAX_CALL_DEPTH - 1) {
        fprintf(stderr, "VM Runtime Error: Call stack overflow (limit is %d) - possible infinite recursion\n",
                MAX_CALL_DEPTH);
        fatal_abort();
    }
    (*call_sp)++;
    vm_call_stack[*call_sp].return_addr = return_addr;
    vm_call_stack[*call_sp].saved_fp = saved_fp;
    vm_call_stack[*call_sp].saved_frame_sp = saved_frame_sp;
}

static inline CallRecord vm_call_pop(int *call_sp) {
    if (*call_sp < 0) {
        fprintf(stderr, "VM Runtime Error: 'ret' with no matching 'call' (call stack empty)\n");
        fatal_abort();
    }
    return vm_call_stack[(*call_sp)--];
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

// Resolves arr[runtime_index] to an offset into vm_array_mem[], validating
// that var_idx actually names an array and that the runtime index falls
// within its declared [lower, upper] bounds - critical here specifically,
// since vm_array_mem is one shared region across every array in the
// program, and an unchecked out-of-bounds write would corrupt a
// completely different array's data.
static int vm_array_offset(int var_idx, int runtime_index) {
    vm_var_index(var_idx);
    Symbol *sym = &sym_table[var_idx];
    if (!sym->is_array) {
        fprintf(stderr, "VM Runtime Error: '%s' is not an array\n", sym->name);
        fatal_abort();
    }
    if (runtime_index < sym->array_lower || runtime_index > sym->array_upper) {
        fprintf(stderr, "VM Runtime Error: Array index %d out of range (%d..%d) for '%s'\n",
                runtime_index, sym->array_lower, sym->array_upper, sym->name);
        fatal_abort();
    }
    return sym->array_base + (runtime_index - sym->array_lower);
}

// Resolves a frame-relative local slot (k) to an absolute vm_frame_stack[]
// index, validating both that a frame is currently active (fp >= 0 - an
// OP_ENTER must have run) and that k falls within that frame's actual
// size (fp..frame_sp), not just the whole frame stack. A frame stays live
// for its whole call, including while it has a nested call in progress,
// so this is always correctly scoped to whichever call's own code is
// currently executing.
static int vm_local_index(int fp, int frame_sp, int k) {
    if (fp < 0) {
        fprintf(stderr, "VM Runtime Error: No active stack frame (local variable access outside a procedure)\n");
        fatal_abort();
    }
    if (k < 0 || fp + k > frame_sp) {
        fprintf(stderr, "VM Runtime Error: Local variable index %d out of range (0..%d for current frame)\n",
                k, frame_sp - fp);
        fatal_abort();
    }
    return fp + k;
}

// char and string share the exact same runtime representation (a
// string_pool[] index) - this is what lets every string opcode work
// unchanged for char too. The only place char is actually enforced is
// here: whenever a value is stored into a char-typed slot.
static int is_string_type(DataType t) {
    return t == TYPE_STRING || t == TYPE_CHAR;
}

// Validates that `val` is both a valid string_pool[] index and refers to
// exactly one character - called whenever a value is stored into a
// char-typed variable or array element.
static void vm_check_char(int val, const char *what) {
    int idx = vm_str_index(val);
    if (strlen(string_pool[idx]) != 1) {
        fprintf(stderr, "VM Runtime Error: %s requires a single character, got \"%s\"\n", what, string_pool[idx]);
        fatal_abort();
    }
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
    memset(vm_array_mem, 0, sizeof(vm_array_mem));
    memset(vm_frame_stack, 0, sizeof(vm_frame_stack));
    int sp = -1;
    int call_sp = -1;
    int fp = -1;         // -1 = no active frame
    int frame_sp = -1;   // top of the frame stack (empty when -1)
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
                int idx = vm_var_index(instr.arg);
                if (sym_table[idx].type == TYPE_CHAR) {
                    vm_check_char(val, sym_table[idx].name);
                }
                vm_vars[idx] = val;
                break;
            }

            case OP_LOAD_IDX: {
                int runtime_index = vm_pop(&sp);
                int offset = vm_array_offset(instr.arg, runtime_index);
                vm_push(&sp, vm_array_mem[offset]);
                break;
            }

            case OP_STORE_IDX: {
                int val = vm_pop(&sp);
                int runtime_index = vm_pop(&sp);
                int offset = vm_array_offset(instr.arg, runtime_index);
                if (sym_table[instr.arg].type == TYPE_CHAR) {
                    vm_check_char(val, sym_table[instr.arg].name);
                }
                vm_array_mem[offset] = val;
                break;
            }

            case OP_LOAD_IDX_DYN: {
                int runtime_index = vm_pop(&sp);
                int array_ref = vm_pop(&sp);
                int offset = vm_array_offset(array_ref, runtime_index);
                vm_push(&sp, vm_array_mem[offset]);
                break;
            }

            case OP_STORE_IDX_DYN: {
                int val = vm_pop(&sp);
                int runtime_index = vm_pop(&sp);
                int array_ref = vm_pop(&sp);
                int offset = vm_array_offset(array_ref, runtime_index);
                if (sym_table[array_ref].type == TYPE_CHAR) {
                    vm_check_char(val, sym_table[array_ref].name);
                }
                vm_array_mem[offset] = val;
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

            case OP_BAND: { int b = vm_pop(&sp); int a = vm_pop(&sp); vm_push(&sp, a & b); break; }
            case OP_BOR:  { int b = vm_pop(&sp); int a = vm_pop(&sp); vm_push(&sp, a | b); break; }
            case OP_BXOR: { int b = vm_pop(&sp); int a = vm_pop(&sp); vm_push(&sp, a ^ b); break; }
            case OP_BNOT: { int a = vm_pop(&sp); vm_push(&sp, ~a); break; }

            case OP_SHL: {
                int b = vm_pop(&sp);
                int a = vm_pop(&sp);
                if (b < 0 || b >= 32) {
                    fprintf(stderr, "VM Runtime Error: Shift amount %d out of range (0..31)\n", b);
                    fatal_abort();
                }
                vm_push(&sp, a << b);
                break;
            }

            case OP_SHR: {
                int b = vm_pop(&sp);
                int a = vm_pop(&sp);
                if (b < 0 || b >= 32) {
                    fprintf(stderr, "VM Runtime Error: Shift amount %d out of range (0..31)\n", b);
                    fatal_abort();
                }
                // Logical (unsigned) shift, matching Pascal's 'shr' - does
                // not sign-extend, unlike C's >> on a signed int.
                vm_push(&sp, (int)((unsigned int)a >> b));
                break;
            }

            case OP_DUP: {
                int a = vm_pop(&sp);
                vm_push(&sp, a);
                vm_push(&sp, a);
                break;
            }

            case OP_ABS: {
                int a = vm_pop(&sp);
                vm_push(&sp, a < 0 ? -a : a);
                break;
            }

            case OP_ORD: {
                int val = vm_pop(&sp);
                vm_check_char(val, "'ord' argument");
                int idx = vm_str_index(val);
                vm_push(&sp, (unsigned char)string_pool[idx][0]);
                break;
            }

            case OP_CHR: {
                int n = vm_pop(&sp);
                if (n < 1 || n > 255) {
                    fprintf(stderr, "VM Runtime Error: 'chr' argument %d out of range (1..255)\n", n);
                    fatal_abort();
                }
                char buf[2] = { (char)n, '\0' };
                vm_push(&sp, vm_intern_string(buf));
                break;
            }

            case OP_LENGTH: {
                int val = vm_pop(&sp);
                int idx = vm_str_index(val);
                vm_push(&sp, (int)strlen(string_pool[idx]));
                break;
            }

            case OP_STR_CHAR_AT: {
                int runtime_index = vm_pop(&sp);
                int val = vm_pop(&sp);
                int idx = vm_str_index(val);
                int len = (int)strlen(string_pool[idx]);
                if (runtime_index < 1 || runtime_index > len) {
                    fprintf(stderr, "VM Runtime Error: String index %d out of range (1..%d)\n", runtime_index, len);
                    fatal_abort();
                }
                char buf[2] = { string_pool[idx][runtime_index - 1], '\0' };
                vm_push(&sp, vm_intern_string(buf));
                break;
            }

            case OP_COPY: {
                int count = vm_pop(&sp);
                int start = vm_pop(&sp);
                int val = vm_pop(&sp);
                int idx = vm_str_index(val);
                int len = (int)strlen(string_pool[idx]);
                // Lenient/clamping, matching real Pascal's copy() - never
                // errors, just returns as much as actually exists.
                if (start < 1) start = 1;
                char buf[MAX_STRING_LEN];
                int buf_len = 0;
                if (start <= len && count > 0) {
                    int avail = len - start + 1;
                    int actual = count < avail ? count : avail;
                    if (actual > MAX_STRING_LEN - 1) actual = MAX_STRING_LEN - 1;
                    memcpy(buf, string_pool[idx] + (start - 1), actual);
                    buf_len = actual;
                }
                buf[buf_len] = '\0';
                vm_push(&sp, vm_intern_string(buf));
                break;
            }

            case OP_POS: {
                int haystack_val = vm_pop(&sp); // pushed second (right) - on top
                int needle_val = vm_pop(&sp);   // pushed first (left)
                int n_idx = vm_str_index(needle_val);
                int h_idx = vm_str_index(haystack_val);
                if (string_pool[n_idx][0] == '\0') {
                    vm_push(&sp, 0); // empty needle - defined as "not found"
                } else {
                    const char *found = strstr(string_pool[h_idx], string_pool[n_idx]);
                    vm_push(&sp, found ? (int)(found - string_pool[h_idx]) + 1 : 0);
                }
                break;
            }

            case OP_UPCASE_CHAR: {
                int val = vm_pop(&sp);
                vm_check_char(val, "'upcase' argument");
                int idx = vm_str_index(val);
                char c = string_pool[idx][0];
                if (c >= 'a' && c <= 'z') {
                    char buf[2] = { (char)(c - 'a' + 'A'), '\0' };
                    vm_push(&sp, vm_intern_string(buf));
                } else {
                    vm_push(&sp, val);
                }
                break;
            }

            case OP_UPPERCASE_STR: {
                int val = vm_pop(&sp);
                int idx = vm_str_index(val);
                char buf[MAX_STRING_LEN];
                int i = 0;
                for (; string_pool[idx][i] != '\0' && i < MAX_STRING_LEN - 1; i++) {
                    char c = string_pool[idx][i];
                    buf[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
                }
                buf[i] = '\0';
                vm_push(&sp, vm_intern_string(buf));
                break;
            }

            case OP_LOWERCASE_STR: {
                int val = vm_pop(&sp);
                int idx = vm_str_index(val);
                char buf[MAX_STRING_LEN];
                int i = 0;
                for (; string_pool[idx][i] != '\0' && i < MAX_STRING_LEN - 1; i++) {
                    char c = string_pool[idx][i];
                    buf[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
                }
                buf[i] = '\0';
                vm_push(&sp, vm_intern_string(buf));
                break;
            }

            case OP_LEFT: {
                int count = vm_pop(&sp);
                int val = vm_pop(&sp);
                int idx = vm_str_index(val);
                int len = (int)strlen(string_pool[idx]);
                int actual = count < 0 ? 0 : (count < len ? count : len);
                char buf[MAX_STRING_LEN];
                memcpy(buf, string_pool[idx], actual);
                buf[actual] = '\0';
                vm_push(&sp, vm_intern_string(buf));
                break;
            }

            case OP_RIGHT: {
                int count = vm_pop(&sp);
                int val = vm_pop(&sp);
                int idx = vm_str_index(val);
                int len = (int)strlen(string_pool[idx]);
                int actual = count < 0 ? 0 : (count < len ? count : len);
                char buf[MAX_STRING_LEN];
                memcpy(buf, string_pool[idx] + (len - actual), actual);
                buf[actual] = '\0';
                vm_push(&sp, vm_intern_string(buf));
                break;
            }

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

            case OP_CALL:
                // ip already points past this CALL. fp/frame_sp are saved
                // as they are right now (the caller's), before the
                // callee's own OP_ENTER (if any) establishes its frame.
                vm_call_push(&call_sp, ip, fp, frame_sp);
                ip = instr.arg;
                break;

            case OP_RET: {
                CallRecord cr = vm_call_pop(&call_sp);
                ip = cr.return_addr;
                fp = cr.saved_fp;
                frame_sp = cr.saved_frame_sp; // deallocates the whole
                                               // returning frame in one step
                break;
            }

            case OP_ENTER: {
                int n = instr.arg;
                if (frame_sp + n >= MAX_FRAME_STACK) {
                    fprintf(stderr, "VM Runtime Error: Frame stack overflow (limit is %d) - possible deep/infinite recursion\n",
                            MAX_FRAME_STACK);
                    fatal_abort();
                }
                fp = frame_sp + 1;
                for (int i = 0; i < n; i++) {
                    vm_frame_stack[fp + i] = 0;
                }
                frame_sp += n;
                break;
            }

            case OP_LOAD_LOCAL:
                vm_push(&sp, vm_frame_stack[vm_local_index(fp, frame_sp, instr.arg)]);
                break;

            case OP_STORE_LOCAL: {
                int val = vm_pop(&sp);
                vm_frame_stack[vm_local_index(fp, frame_sp, instr.arg)] = val;
                break;
            }

            case OP_POP:
                vm_pop(&sp);
                break;

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

            case OP_SCMP: {
                int b = vm_str_index(vm_pop(&sp));
                int a = vm_str_index(vm_pop(&sp));
                int cmp = strcmp(string_pool[a], string_pool[b]);
                vm_push(&sp, (cmp > 0) - (cmp < 0)); // normalize to -1/0/1
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
                        if (sym_table[i].is_array) {
                            printf("%s = [", sym_table[i].name);
                            int lower = sym_table[i].array_lower;
                            int upper = sym_table[i].array_upper;
                            int base = sym_table[i].array_base;
                            for (int j = lower; j <= upper; j++) {
                                if (j > lower) printf(", ");
                                int elem = vm_array_mem[base + (j - lower)];
                                if (is_string_type(sym_table[i].type)) {
                                    if (elem >= 0 && elem < string_count) printf("%s", string_pool[elem]);
                                    else printf("<invalid string index %d>", elem);
                                } else if (sym_table[i].type == TYPE_BOOLEAN) {
                                    printf("%s", elem ? "TRUE" : "FALSE");
                                } else {
                                    printf("%d", elem);
                                }
                            }
                            printf("]\n");
                        } else if (is_string_type(sym_table[i].type)) {
                            int idx = vm_vars[i];
                            if (idx >= 0 && idx < string_count) {
                                printf("%s = %s\n", sym_table[i].name, string_pool[idx]);
                            } else {
                                printf("%s = <invalid string index %d>\n", sym_table[i].name, idx);
                            }
                        } else if (sym_table[i].type == TYPE_BOOLEAN) {
                            printf("%s = %s\n", sym_table[i].name, vm_vars[i] ? "TRUE" : "FALSE");
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

            case OP_PRINT_BOOL: {
                int val = vm_pop(&sp);
                printf("%s", val ? "TRUE" : "FALSE");
                break;
            }

            case OP_NEWLINE:
                printf("\n");
                break;

            case OP_READ: {
                int idx = vm_var_index(instr.arg);
                printf("> "); // Prompt user

                if (is_string_type(sym_table[idx].type)) {
                    char line[MAX_STRING_LEN];
                    if (!fgets(line, sizeof(line), stdin)) {
                        fprintf(stderr, "VM Runtime Error: Invalid string input\n");
                        fatal_abort();
                    }
                    size_t len = strlen(line);
                    if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
                    if (sym_table[idx].type == TYPE_CHAR && strlen(line) != 1) {
                        fprintf(stderr, "VM Runtime Error: readln expected a single character for '%s', got \"%s\"\n",
                                sym_table[idx].name, line);
                        fatal_abort();
                    }
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

