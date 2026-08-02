#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <errno.h>
#include "vm.h"
#include "error.h"

// Prints `text` padded to at least |width| characters: right-justified
// for width >= 0 (Pascal's normal 'x:width' field-width form),
// left-justified for width < 0 (some Pascal dialects' convention for the
// same syntax). Negating INT_MIN would be undefined behavior, so that
// one case is clamped to INT_MAX instead - no real program needs a field
// that wide anyway.
static void vm_print_padded(const char *text, int width) {
    if (width < 0) {
        int abs_width = (width == INT_MIN) ? INT_MAX : -width;
        printf("%-*s", abs_width, text);
    } else {
        printf("%*s", width, text);
    }
}

// Same as vm_print_padded(), but to a file instead of stdout - the
// file-writing sibling every OP_..._PADDED_FILE opcode needs.
static void vm_fprint_padded(FILE *f, const char *text, int width) {
    if (width < 0) {
        int abs_width = (width == INT_MIN) ? INT_MAX : -width;
        fprintf(f, "%-*s", abs_width, text);
    } else {
        fprintf(f, "%*s", width, text);
    }
}

// Reinterprets an int-sized storage slot's raw bits as a float, or vice
// versa - memcpy is the well-defined way to do this in C (a union would
// risk strict-aliasing undefined behavior). This is what lets a 'real'
// value live in exactly the same int-sized slots (vm_stack, vm_vars,
// array elements, frame slots) every other type already uses, with no
// changes needed to any of those storage arrays themselves.
static float bits_to_float(int bits) {
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

static int float_to_bits(float f) {
    int bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// Checks a math function's result isn't NaN or infinite, erroring
// cleanly if so rather than letting it silently propagate (with "nan" or
// "inf" showing up in some later, unrelated printed output, far from the
// actual cause). Catches domain errors (sqrt of a negative number, ln of
// a non-positive number, a negative power() base with a non-integer
// exponent) and overflow uniformly in one place, rather than needing a
// separate precondition check specific to each function's own domain.
static void vm_check_real_result(float val, const char *fn_name) {
    if (isnan(val) || isinf(val)) {
        fprintf(stderr, "VM Runtime Error: '%s' produced an invalid result (domain error or overflow)\n", fn_name);
        fatal_abort();
    }
}

static int vm_stack[MAX_STACK];
static int vm_vars[MAX_SYMBOLS];
static int vm_array_mem[MAX_ARRAY_MEM];
static int vm_frame_stack[MAX_FRAME_STACK]; // local variable slots for
                                             // every active call, stacked

// A file variable's real runtime state (see TYPE_FILE in common.h) -
// indexed by the SAME sym_table[] index the variable itself has (safe
// only because a file variable is always global - one fixed index for
// its whole lifetime), rather than through a second table-index
// indirection the way string_pool[] works for TYPE_STRING/TYPE_CHAR.
// vm_vars[idx] itself is never touched for a TYPE_FILE variable.
typedef struct {
    FILE *handle;   // NULL if not currently open
    char filename[MAX_STRING_LEN]; // set by assign(); read by reset()/rewrite()
    int assigned;   // 1 once assign() has been called at least once
} OpenFile;
static OpenFile vm_open_files[MAX_SYMBOLS];

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

// Same as vm_array_offset(), but for a 2D array: bounds-checks each index
// against its own dimension, then computes the row-major offset into the
// same flat vm_array_mem[] region every array (1D or 2D) shares.
static int vm_array_offset_2d(int var_idx, int i, int j) {
    vm_var_index(var_idx);
    Symbol *sym = &sym_table[var_idx];
    if (!sym->is_array || !sym->is_2d) {
        fprintf(stderr, "VM Runtime Error: '%s' is not a 2D array\n", sym->name);
        fatal_abort();
    }
    if (i < sym->array_lower || i > sym->array_upper) {
        fprintf(stderr, "VM Runtime Error: First array index %d out of range (%d..%d) for '%s'\n",
                i, sym->array_lower, sym->array_upper, sym->name);
        fatal_abort();
    }
    if (j < sym->array_lower2 || j > sym->array_upper2) {
        fprintf(stderr, "VM Runtime Error: Second array index %d out of range (%d..%d) for '%s'\n",
                j, sym->array_lower2, sym->array_upper2, sym->name);
        fatal_abort();
    }
    int dim2_size = sym->array_upper2 - sym->array_lower2 + 1;
    return sym->array_base + (i - sym->array_lower) * dim2_size + (j - sym->array_lower2);
}

// Same as vm_array_offset()/vm_array_offset_2d(), but for an array with
// 3 OR MORE dimensions: bounds-checks each index against its own
// dimension (sym->nd_lower[d]/nd_upper[d]), then computes the row-major
// offset via Horner's method - the direct generalization of
// vm_array_offset_2d()'s '(i-lower1)*dim2_size+(j-lower2)' formula to
// however many dimensions there are. 'dims' is passed separately from
// sym->nd_dims (rather than just trusting the symbol) so a mismatch
// (possible from hand-written .sasm bytecode, which isn't compiler-
// validated the way parser.c's own output is) is caught here instead of
// silently reading/writing the wrong offset.
static int vm_array_offset_nd(int var_idx, int dims, const int *indices) {
    vm_var_index(var_idx);
    Symbol *sym = &sym_table[var_idx];
    if (!sym->is_array || !sym->is_nd || sym->nd_dims != dims) {
        fprintf(stderr, "VM Runtime Error: '%s' is not a %d-dimensional array\n", sym->name, dims);
        fatal_abort();
    }
    int offset = 0;
    for (int d = 0; d < dims; d++) {
        if (indices[d] < sym->nd_lower[d] || indices[d] > sym->nd_upper[d]) {
            fprintf(stderr, "VM Runtime Error: Array index %d out of range (%d..%d) in dimension %d for '%s'\n",
                    indices[d], sym->nd_lower[d], sym->nd_upper[d], d + 1, sym->name);
            fatal_abort();
        }
        int dim_size = sym->nd_upper[d] - sym->nd_lower[d] + 1;
        offset = offset * dim_size + (indices[d] - sym->nd_lower[d]);
    }
    return sym->array_base + offset;
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

// Resolves a file variable's sym_table[] index to its currently-open
// FILE* - validates idx actually names a file variable AND that it's
// currently open, erroring clearly (mentioning `action`, e.g. "write")
// if either isn't true. Shared by every read/write/eof/eoln/close
// file opcode - assign/reset/rewrite have their own, different
// preconditions (they're what MAKE a file open in the first place), so
// they don't go through this helper.
static FILE *vm_file_handle(int idx, const char *action) {
    vm_var_index(idx);
    if (sym_table[idx].type != TYPE_FILE) {
        fprintf(stderr, "VM Runtime Error: '%s' is not a file variable\n", sym_table[idx].name);
        fatal_abort();
    }
    if (!vm_open_files[idx].handle) {
        fprintf(stderr, "VM Runtime Error: File '%s' is not open (%s)\n", sym_table[idx].name, action);
        fatal_abort();
    }
    return vm_open_files[idx].handle;
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

// Reads one value from stdin into a GLOBAL variable (idx), matching its
// declared type - shared by OP_READ ('readln' semantics: flush) and
// OP_READ_NOFLUSH ('read' semantics: leave the rest of the line alone,
// so a following read on the same line picks up where this one left
// off). 'flush' only affects INTEGER/REAL/BOOLEAN (scanf-based): a
// STRING/CHAR target always reads a whole line via fgets(), which
// already consumes through the newline as part of reading it, so there
// is nothing left to flush either way.
static void vm_read_global(int idx, int flush) {
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
    } else if (sym_table[idx].type == TYPE_REAL) {
        float input_val;
        if (scanf("%f", &input_val) != 1) {
            fprintf(stderr, "VM Runtime Error: Invalid real input\n");
            fatal_abort();
        }
        if (flush) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) { }
        }
        vm_vars[idx] = float_to_bits(input_val);
    } else {
        int input_val;
        if (scanf("%d", &input_val) != 1) {
            fprintf(stderr, "VM Runtime Error: Invalid integer input\n");
            fatal_abort();
        }
        if (flush) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) { }
        }
        if (sym_table[idx].type == TYPE_BOOLEAN && input_val != 0 && input_val != 1) {
            fprintf(stderr, "VM Runtime Error: readln expected a boolean value (0 or 1) for '%s', got %d\n",
                    sym_table[idx].name, input_val);
            fatal_abort();
        }
        vm_vars[idx] = input_val;
    }
}

// Local-frame-slot equivalents of vm_read_global()'s int/bool/real
// branches - kept as separate small functions (matching the existing
// per-type OP_READ_LOCAL_* opcode split) rather than one function
// branching on type, since a local slot carries no runtime type tag to
// branch on in the first place; codegen already picked the right
// function via the right opcode.
static void vm_read_local_int(int slot, int flush) {
    printf("> ");
    int input_val;
    if (scanf("%d", &input_val) != 1) {
        fprintf(stderr, "VM Runtime Error: Invalid integer input\n");
        fatal_abort();
    }
    if (flush) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) { }
    }
    vm_frame_stack[slot] = input_val;
}

static void vm_read_local_bool(int slot, int flush) {
    printf("> ");
    int input_val;
    if (scanf("%d", &input_val) != 1) {
        fprintf(stderr, "VM Runtime Error: Invalid integer input\n");
        fatal_abort();
    }
    if (flush) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) { }
    }
    if (input_val != 0 && input_val != 1) {
        fprintf(stderr, "VM Runtime Error: readln expected a boolean value (0 or 1), got %d\n", input_val);
        fatal_abort();
    }
    vm_frame_stack[slot] = input_val;
}

static void vm_read_local_real(int slot, int flush) {
    printf("> ");
    float input_val;
    if (scanf("%f", &input_val) != 1) {
        fprintf(stderr, "VM Runtime Error: Invalid real input\n");
        fatal_abort();
    }
    if (flush) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) { }
    }
    vm_frame_stack[slot] = float_to_bits(input_val);
}

// File-reading equivalents of vm_read_global()/vm_read_local_int/bool/
// real() above - same per-type parsing/validation, but from an
// already-open FILE* instead of stdin, and critically, WITHOUT the "> "
// interactive prompt those print: there's no user to prompt when
// reading from a file, and printing one would corrupt whatever the
// running program's own output stream is producing.
static void vm_read_global_file(int idx, FILE *f, int flush) {
    if (is_string_type(sym_table[idx].type)) {
        char line[MAX_STRING_LEN];
        if (!fgets(line, sizeof(line), f)) {
            fprintf(stderr, "VM Runtime Error: Invalid string input from file for '%s'\n", sym_table[idx].name);
            fatal_abort();
        }
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (sym_table[idx].type == TYPE_CHAR && strlen(line) != 1) {
            fprintf(stderr, "VM Runtime Error: read expected a single character for '%s', got \"%s\"\n",
                    sym_table[idx].name, line);
            fatal_abort();
        }
        vm_vars[idx] = vm_intern_string(line);
    } else if (sym_table[idx].type == TYPE_REAL) {
        float input_val;
        if (fscanf(f, "%f", &input_val) != 1) {
            fprintf(stderr, "VM Runtime Error: Invalid real input from file for '%s'\n", sym_table[idx].name);
            fatal_abort();
        }
        if (flush) {
            int c;
            while ((c = fgetc(f)) != '\n' && c != EOF) { }
        }
        vm_vars[idx] = float_to_bits(input_val);
    } else {
        int input_val;
        if (fscanf(f, "%d", &input_val) != 1) {
            fprintf(stderr, "VM Runtime Error: Invalid integer input from file for '%s'\n", sym_table[idx].name);
            fatal_abort();
        }
        if (flush) {
            int c;
            while ((c = fgetc(f)) != '\n' && c != EOF) { }
        }
        if (sym_table[idx].type == TYPE_BOOLEAN && input_val != 0 && input_val != 1) {
            fprintf(stderr, "VM Runtime Error: read expected a boolean value (0 or 1) for '%s', got %d\n",
                    sym_table[idx].name, input_val);
            fatal_abort();
        }
        vm_vars[idx] = input_val;
    }
}

static void vm_read_local_int_file(int slot, FILE *f, int flush) {
    int input_val;
    if (fscanf(f, "%d", &input_val) != 1) {
        fprintf(stderr, "VM Runtime Error: Invalid integer input from file\n");
        fatal_abort();
    }
    if (flush) {
        int c;
        while ((c = fgetc(f)) != '\n' && c != EOF) { }
    }
    vm_frame_stack[slot] = input_val;
}

static void vm_read_local_bool_file(int slot, FILE *f, int flush) {
    int input_val;
    if (fscanf(f, "%d", &input_val) != 1) {
        fprintf(stderr, "VM Runtime Error: Invalid integer input from file\n");
        fatal_abort();
    }
    if (flush) {
        int c;
        while ((c = fgetc(f)) != '\n' && c != EOF) { }
    }
    if (input_val != 0 && input_val != 1) {
        fprintf(stderr, "VM Runtime Error: read expected a boolean value (0 or 1) from file, got %d\n", input_val);
        fatal_abort();
    }
    vm_frame_stack[slot] = input_val;
}

static void vm_read_local_real_file(int slot, FILE *f, int flush) {
    float input_val;
    if (fscanf(f, "%f", &input_val) != 1) {
        fprintf(stderr, "VM Runtime Error: Invalid real input from file\n");
        fatal_abort();
    }
    if (flush) {
        int c;
        while ((c = fgetc(f)) != '\n' && c != EOF) { }
    }
    vm_frame_stack[slot] = float_to_bits(input_val);
}

// Peeks at the next character on stdin without consuming it (via
// fgetc()/ungetc() - safe for stdin, an ordinary FILE*). Used by
// OP_EOF/OP_EOLN; no persisted "current line" buffer needed anywhere
// else in this file for either of them.
static int vm_peek_stdin(void) {
    int c = fgetc(stdin);
    if (c != EOF) ungetc(c, stdin);
    return c;
}

void run_vm(void) {
    memset(vm_vars, 0, sizeof(vm_vars));
    memset(vm_array_mem, 0, sizeof(vm_array_mem));
    memset(vm_frame_stack, 0, sizeof(vm_frame_stack));
    memset(vm_open_files, 0, sizeof(vm_open_files)); // handle=NULL, filename="", assigned=0 for every slot
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

            case OP_LOAD_IDX2D: {
                int j = vm_pop(&sp); // pushed second by codegen - on top
                int i = vm_pop(&sp); // pushed first
                int offset = vm_array_offset_2d(instr.arg, i, j);
                vm_push(&sp, vm_array_mem[offset]);
                break;
            }

            case OP_STORE_IDX2D: {
                int val = vm_pop(&sp);
                int j = vm_pop(&sp);
                int i = vm_pop(&sp);
                int offset = vm_array_offset_2d(instr.arg, i, j);
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

            case OP_LOAD_IDX2D_DYN: {
                int j = vm_pop(&sp); // pushed second by codegen - on top
                int i = vm_pop(&sp); // pushed first
                int array_ref = vm_pop(&sp);
                int offset = vm_array_offset_2d(array_ref, i, j);
                vm_push(&sp, vm_array_mem[offset]);
                break;
            }

            case OP_STORE_IDX2D_DYN: {
                int val = vm_pop(&sp);
                int j = vm_pop(&sp);
                int i = vm_pop(&sp);
                int array_ref = vm_pop(&sp);
                int offset = vm_array_offset_2d(array_ref, i, j);
                if (sym_table[array_ref].type == TYPE_CHAR) {
                    vm_check_char(val, sym_table[array_ref].name);
                }
                vm_array_mem[offset] = val;
                break;
            }

            case OP_LOAD_IDXND: {
                int dims = sym_table[instr.arg].nd_dims;
                int indices[MAX_ARRAY_DIMS];
                for (int d = dims - 1; d >= 0; d--) {
                    indices[d] = vm_pop(&sp); // dimension d+1 was pushed
                                               // d-th-to-last, so it comes
                                               // off the stack in this order
                }
                int offset = vm_array_offset_nd(instr.arg, dims, indices);
                vm_push(&sp, vm_array_mem[offset]);
                break;
            }

            case OP_STORE_IDXND: {
                int val = vm_pop(&sp);
                int dims = sym_table[instr.arg].nd_dims;
                int indices[MAX_ARRAY_DIMS];
                for (int d = dims - 1; d >= 0; d--) {
                    indices[d] = vm_pop(&sp);
                }
                int offset = vm_array_offset_nd(instr.arg, dims, indices);
                if (sym_table[instr.arg].type == TYPE_CHAR) {
                    vm_check_char(val, sym_table[instr.arg].name);
                }
                vm_array_mem[offset] = val;
                break;
            }

            case OP_LOAD_IDXND_DYN: {
                int dims = instr.arg; // fixed, compile-time-known - see the opcode's comment in common.h
                int indices[MAX_ARRAY_DIMS];
                for (int d = dims - 1; d >= 0; d--) {
                    indices[d] = vm_pop(&sp);
                }
                int array_ref = vm_pop(&sp);
                int offset = vm_array_offset_nd(array_ref, dims, indices);
                vm_push(&sp, vm_array_mem[offset]);
                break;
            }

            case OP_STORE_IDXND_DYN: {
                int val = vm_pop(&sp);
                int dims = instr.arg;
                int indices[MAX_ARRAY_DIMS];
                for (int d = dims - 1; d >= 0; d--) {
                    indices[d] = vm_pop(&sp);
                }
                int array_ref = vm_pop(&sp);
                int offset = vm_array_offset_nd(array_ref, dims, indices);
                if (sym_table[array_ref].type == TYPE_CHAR) {
                    vm_check_char(val, sym_table[array_ref].name);
                }
                vm_array_mem[offset] = val;
                break;
            }

            case OP_FILE_ASSIGN: {
                int idx = vm_var_index(instr.arg);
                if (sym_table[idx].type != TYPE_FILE) {
                    fprintf(stderr, "VM Runtime Error: '%s' is not a file variable\n", sym_table[idx].name);
                    fatal_abort();
                }
                int name_idx = vm_str_index(vm_pop(&sp));
                strncpy(vm_open_files[idx].filename, string_pool[name_idx], MAX_STRING_LEN - 1);
                vm_open_files[idx].filename[MAX_STRING_LEN - 1] = '\0';
                vm_open_files[idx].assigned = 1;
                break;
            }

            case OP_FILE_RESET:
            case OP_FILE_REWRITE: {
                int idx = vm_var_index(instr.arg);
                if (sym_table[idx].type != TYPE_FILE) {
                    fprintf(stderr, "VM Runtime Error: '%s' is not a file variable\n", sym_table[idx].name);
                    fatal_abort();
                }
                if (!vm_open_files[idx].assigned) {
                    fprintf(stderr, "VM Runtime Error: File '%s' was never assign()ed a filename\n", sym_table[idx].name);
                    fatal_abort();
                }
                if (vm_open_files[idx].handle) fclose(vm_open_files[idx].handle); // reset/rewrite on an already-open file just reopens it, matching real Pascal
                const char *mode = (instr.op == OP_FILE_RESET) ? "r" : "w";
                vm_open_files[idx].handle = fopen(vm_open_files[idx].filename, mode);
                if (!vm_open_files[idx].handle) {
                    fprintf(stderr, "VM Runtime Error: Cannot open file '%s' for %s (%s)\n",
                            vm_open_files[idx].filename, (instr.op == OP_FILE_RESET) ? "reading" : "writing", strerror(errno));
                    fatal_abort();
                }
                break;
            }

            case OP_FILE_CLOSE: {
                int idx = vm_var_index(instr.arg);
                if (sym_table[idx].type != TYPE_FILE) {
                    fprintf(stderr, "VM Runtime Error: '%s' is not a file variable\n", sym_table[idx].name);
                    fatal_abort();
                }
                if (!vm_open_files[idx].handle) {
                    fprintf(stderr, "VM Runtime Error: File '%s' is not open\n", sym_table[idx].name);
                    fatal_abort();
                }
                fclose(vm_open_files[idx].handle);
                vm_open_files[idx].handle = NULL;
                break;
            }

            case OP_PRINT_FILE: {
                int val = vm_pop(&sp);
                FILE *f = vm_file_handle(instr.arg, "write");
                fprintf(f, "%d", val);
                break;
            }

            case OP_PRINT_STR_FILE: {
                int idx = vm_str_index(vm_pop(&sp));
                FILE *f = vm_file_handle(instr.arg, "write");
                fprintf(f, "%s", string_pool[idx]);
                break;
            }

            case OP_PRINT_BOOL_FILE: {
                int val = vm_pop(&sp);
                FILE *f = vm_file_handle(instr.arg, "write");
                fprintf(f, "%s", val ? "TRUE" : "FALSE");
                break;
            }

            case OP_FPRINT_FILE: {
                float val = bits_to_float(vm_pop(&sp));
                FILE *f = vm_file_handle(instr.arg, "write");
                fprintf(f, "%.6g", val);
                break;
            }

            case OP_PRINT_PADDED_FILE: {
                int width = vm_pop(&sp);
                int val = vm_pop(&sp);
                FILE *f = vm_file_handle(instr.arg, "write");
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", val);
                vm_fprint_padded(f, buf, width);
                break;
            }

            case OP_PRINT_STR_PADDED_FILE: {
                int width = vm_pop(&sp);
                int idx = vm_str_index(vm_pop(&sp));
                FILE *f = vm_file_handle(instr.arg, "write");
                vm_fprint_padded(f, string_pool[idx], width);
                break;
            }

            case OP_PRINT_BOOL_PADDED_FILE: {
                int width = vm_pop(&sp);
                int val = vm_pop(&sp);
                FILE *f = vm_file_handle(instr.arg, "write");
                vm_fprint_padded(f, val ? "TRUE" : "FALSE", width);
                break;
            }

            case OP_FPRINT_PADDED_FILE: {
                int width = vm_pop(&sp);
                float val = bits_to_float(vm_pop(&sp));
                FILE *f = vm_file_handle(instr.arg, "write");
                char buf[64];
                snprintf(buf, sizeof(buf), "%.6g", val);
                vm_fprint_padded(f, buf, width);
                break;
            }

            case OP_FPRINT_PADDED_PRECISE_FILE: {
                int precision = vm_pop(&sp);
                int width = vm_pop(&sp);
                float val = bits_to_float(vm_pop(&sp));
                FILE *f = vm_file_handle(instr.arg, "write");
                if (precision < 0 || precision > 20) {
                    fprintf(stderr, "VM Runtime Error: write field precision %d out of range (0..20)\n", precision);
                    fatal_abort();
                }
                char buf[64];
                snprintf(buf, sizeof(buf), "%.*f", precision, val);
                vm_fprint_padded(f, buf, width);
                break;
            }

            case OP_NEWLINE_FILE: {
                FILE *f = vm_file_handle(instr.arg, "write");
                fprintf(f, "\n");
                break;
            }

            case OP_EOF_FILE: {
                FILE *f = vm_file_handle(instr.arg, "eof");
                int c = fgetc(f);
                if (c != EOF) ungetc(c, f);
                vm_push(&sp, c == EOF);
                break;
            }

            case OP_EOLN_FILE: {
                FILE *f = vm_file_handle(instr.arg, "eoln");
                int c = fgetc(f);
                if (c != EOF) ungetc(c, f);
                vm_push(&sp, c == EOF || c == '\n');
                break;
            }

            case OP_READ_FILE: {
                int file_idx = vm_pop(&sp);
                FILE *f = vm_file_handle(file_idx, "read");
                vm_read_global_file(vm_var_index(instr.arg), f, 1);
                break;
            }

            case OP_READ_FILE_NOFLUSH: {
                int file_idx = vm_pop(&sp);
                FILE *f = vm_file_handle(file_idx, "read");
                vm_read_global_file(vm_var_index(instr.arg), f, 0);
                break;
            }

            case OP_READ_FILE_LOCAL_INT: {
                int file_idx = vm_pop(&sp);
                FILE *f = vm_file_handle(file_idx, "read");
                vm_read_local_int_file(vm_local_index(fp, frame_sp, instr.arg), f, 1);
                break;
            }

            case OP_READ_FILE_LOCAL_INT_NOFLUSH: {
                int file_idx = vm_pop(&sp);
                FILE *f = vm_file_handle(file_idx, "read");
                vm_read_local_int_file(vm_local_index(fp, frame_sp, instr.arg), f, 0);
                break;
            }

            case OP_READ_FILE_LOCAL_BOOL: {
                int file_idx = vm_pop(&sp);
                FILE *f = vm_file_handle(file_idx, "read");
                vm_read_local_bool_file(vm_local_index(fp, frame_sp, instr.arg), f, 1);
                break;
            }

            case OP_READ_FILE_LOCAL_BOOL_NOFLUSH: {
                int file_idx = vm_pop(&sp);
                FILE *f = vm_file_handle(file_idx, "read");
                vm_read_local_bool_file(vm_local_index(fp, frame_sp, instr.arg), f, 0);
                break;
            }

            case OP_READ_FILE_LOCAL_REAL: {
                int file_idx = vm_pop(&sp);
                FILE *f = vm_file_handle(file_idx, "read");
                vm_read_local_real_file(vm_local_index(fp, frame_sp, instr.arg), f, 1);
                break;
            }

            case OP_READ_FILE_LOCAL_REAL_NOFLUSH: {
                int file_idx = vm_pop(&sp);
                FILE *f = vm_file_handle(file_idx, "read");
                vm_read_local_real_file(vm_local_index(fp, frame_sp, instr.arg), f, 0);
                break;
            }

            case OP_READ_FILE_LOCAL_STR: {
                int file_idx = vm_pop(&sp);
                FILE *f = vm_file_handle(file_idx, "read");
                int slot = vm_local_index(fp, frame_sp, instr.arg);
                char line[MAX_STRING_LEN];
                if (!fgets(line, sizeof(line), f)) {
                    fprintf(stderr, "VM Runtime Error: Invalid string input from file\n");
                    fatal_abort();
                }
                size_t len = strlen(line);
                if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
                vm_frame_stack[slot] = vm_intern_string(line);
                break;
            }

            case OP_READ_FILE_LOCAL_CHAR: {
                int file_idx = vm_pop(&sp);
                FILE *f = vm_file_handle(file_idx, "read");
                int slot = vm_local_index(fp, frame_sp, instr.arg);
                char line[MAX_STRING_LEN];
                if (!fgets(line, sizeof(line), f)) {
                    fprintf(stderr, "VM Runtime Error: Invalid string input from file\n");
                    fatal_abort();
                }
                size_t len = strlen(line);
                if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
                if (strlen(line) != 1) {
                    fprintf(stderr, "VM Runtime Error: read expected a single character from file, got \"%s\"\n", line);
                    fatal_abort();
                }
                vm_frame_stack[slot] = vm_intern_string(line);
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

            case OP_CHECK_LOWER: {
                int a = vm_pop(&sp);
                if (a < instr.arg) {
                    fprintf(stderr, "VM Runtime Error: Value %d is below its subrange's lower bound %d\n", a, instr.arg);
                    fatal_abort();
                }
                vm_push(&sp, a);
                break;
            }

            case OP_CHECK_UPPER: {
                int a = vm_pop(&sp);
                if (a > instr.arg) {
                    fprintf(stderr, "VM Runtime Error: Value %d is above its subrange's upper bound %d\n", a, instr.arg);
                    fatal_abort();
                }
                vm_push(&sp, a);
                break;
            }

            case OP_ASSERT: {
                int msg_idx = vm_str_index(vm_pop(&sp));
                int cond = vm_pop(&sp);
                if (!cond) {
                    fprintf(stderr, "VM Runtime Error: %s\n", string_pool[msg_idx]);
                    fatal_abort();
                }
                break;
            }

            case OP_ABS: {
                int a = vm_pop(&sp);
                vm_push(&sp, a < 0 ? -a : a);
                break;
            }

            case OP_FABS: {
                float a = bits_to_float(vm_pop(&sp));
                vm_push(&sp, float_to_bits(fabsf(a)));
                break;
            }

            case OP_FSQRT: {
                float a = bits_to_float(vm_pop(&sp));
                float result = sqrtf(a);
                vm_check_real_result(result, "sqrt");
                vm_push(&sp, float_to_bits(result));
                break;
            }

            case OP_FSIN: {
                float a = bits_to_float(vm_pop(&sp));
                float result = sinf(a);
                vm_check_real_result(result, "sin");
                vm_push(&sp, float_to_bits(result));
                break;
            }

            case OP_FCOS: {
                float a = bits_to_float(vm_pop(&sp));
                float result = cosf(a);
                vm_check_real_result(result, "cos");
                vm_push(&sp, float_to_bits(result));
                break;
            }

            case OP_FARCTAN: {
                float a = bits_to_float(vm_pop(&sp));
                float result = atanf(a);
                vm_check_real_result(result, "arctan");
                vm_push(&sp, float_to_bits(result));
                break;
            }

            case OP_FEXP: {
                float a = bits_to_float(vm_pop(&sp));
                float result = expf(a);
                vm_check_real_result(result, "exp");
                vm_push(&sp, float_to_bits(result));
                break;
            }

            case OP_FLN: {
                float a = bits_to_float(vm_pop(&sp));
                float result = logf(a);
                vm_check_real_result(result, "ln");
                vm_push(&sp, float_to_bits(result));
                break;
            }

            case OP_FPOWER: {
                float exponent = bits_to_float(vm_pop(&sp)); // pushed second (last) by codegen - on top
                float base = bits_to_float(vm_pop(&sp));
                float result = powf(base, exponent);
                vm_check_real_result(result, "power");
                vm_push(&sp, float_to_bits(result));
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

            case OP_STR_CHAR_REPLACE: {
                int new_char_val = vm_pop(&sp);   // pushed last - on top
                int runtime_index = vm_pop(&sp);
                int old_str_val = vm_pop(&sp);    // pushed first (the preceding LOAD/LOAD_LOCAL)
                vm_check_char(new_char_val, "string character assignment");
                int old_idx = vm_str_index(old_str_val);
                int len = (int)strlen(string_pool[old_idx]);
                if (runtime_index < 1 || runtime_index > len) {
                    fprintf(stderr, "VM Runtime Error: String index %d out of range (1..%d)\n", runtime_index, len);
                    fatal_abort();
                }
                char buf[MAX_STRING_LEN];
                strcpy(buf, string_pool[old_idx]);
                int new_char_idx = vm_str_index(new_char_val);
                buf[runtime_index - 1] = string_pool[new_char_idx][0];
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

            case OP_FADD: { float b = bits_to_float(vm_pop(&sp)); float a = bits_to_float(vm_pop(&sp)); vm_push(&sp, float_to_bits(a + b)); break; }
            case OP_FSUB: { float b = bits_to_float(vm_pop(&sp)); float a = bits_to_float(vm_pop(&sp)); vm_push(&sp, float_to_bits(a - b)); break; }
            case OP_FMUL: { float b = bits_to_float(vm_pop(&sp)); float a = bits_to_float(vm_pop(&sp)); vm_push(&sp, float_to_bits(a * b)); break; }
            case OP_FDIV: {
                float b = bits_to_float(vm_pop(&sp));
                float a = bits_to_float(vm_pop(&sp));
                if (b == 0.0f) {
                    fprintf(stderr, "VM Runtime Error: Division by zero\n");
                    fatal_abort();
                }
                vm_push(&sp, float_to_bits(a / b));
                break;
            }

            case OP_FEQ:  { float b = bits_to_float(vm_pop(&sp)); float a = bits_to_float(vm_pop(&sp)); vm_push(&sp, a == b); break; }
            case OP_FLT:  { float b = bits_to_float(vm_pop(&sp)); float a = bits_to_float(vm_pop(&sp)); vm_push(&sp, a < b);  break; }
            case OP_FGT:  { float b = bits_to_float(vm_pop(&sp)); float a = bits_to_float(vm_pop(&sp)); vm_push(&sp, a > b);  break; }
            case OP_FLTE: { float b = bits_to_float(vm_pop(&sp)); float a = bits_to_float(vm_pop(&sp)); vm_push(&sp, a <= b); break; }
            case OP_FGTE: { float b = bits_to_float(vm_pop(&sp)); float a = bits_to_float(vm_pop(&sp)); vm_push(&sp, a >= b); break; }
            case OP_FNEQ: { float b = bits_to_float(vm_pop(&sp)); float a = bits_to_float(vm_pop(&sp)); vm_push(&sp, a != b); break; }

            case OP_FNEG: { float a = bits_to_float(vm_pop(&sp)); vm_push(&sp, float_to_bits(-a)); break; }

            case OP_INT_TO_REAL: { int a = vm_pop(&sp); vm_push(&sp, float_to_bits((float)a)); break; }

            case OP_TRUNC: { float a = bits_to_float(vm_pop(&sp)); vm_push(&sp, (int)a); break; }
            case OP_ROUND: { float a = bits_to_float(vm_pop(&sp)); vm_push(&sp, (int)roundf(a)); break; }

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

            case OP_PUSH_LOCAL_REF: {
                int abs_index = vm_local_index(fp, frame_sp, instr.arg);
                vm_push(&sp, -(abs_index + 1));
                break;
            }

            case OP_LOAD_REF: {
                int ref = vm_pop(&sp);
                if (ref >= 0) {
                    vm_push(&sp, vm_vars[vm_var_index(ref)]);
                } else {
                    // The bounds check already happened once, in
                    // OP_PUSH_LOCAL_REF, when this reference was created -
                    // and the frame it points into is guaranteed still
                    // live (see OP_PUSH_LOCAL_REF's comment in common.h),
                    // so a direct index is enough here.
                    vm_push(&sp, vm_frame_stack[-(ref + 1)]);
                }
                break;
            }

            case OP_STORE_REF: {
                int val = vm_pop(&sp);
                int ref = vm_pop(&sp);
                if (ref >= 0) {
                    vm_vars[vm_var_index(ref)] = val;
                } else {
                    vm_frame_stack[-(ref + 1)] = val;
                }
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
                            // Total flattened element count - a plain
                            // product of every dimension's size, so this
                            // correctly covers 1D, 2D, and N-D uniformly
                            // (each is just "how many dimensions", 1/2/N).
                            int total;
                            if (sym_table[i].is_nd) {
                                total = 1;
                                for (int d = 0; d < sym_table[i].nd_dims; d++) {
                                    total *= (sym_table[i].nd_upper[d] - sym_table[i].nd_lower[d] + 1);
                                }
                            } else if (sym_table[i].is_2d) {
                                total = (sym_table[i].array_upper - sym_table[i].array_lower + 1) *
                                        (sym_table[i].array_upper2 - sym_table[i].array_lower2 + 1);
                            } else {
                                total = sym_table[i].array_upper - sym_table[i].array_lower + 1;
                            }
                            int base = sym_table[i].array_base;
                            for (int j = 0; j < total; j++) {
                                if (j > 0) printf(", ");
                                int elem = vm_array_mem[base + j];
                                if (is_string_type(sym_table[i].type)) {
                                    if (elem >= 0 && elem < string_count) printf("%s", string_pool[elem]);
                                    else printf("<invalid string index %d>", elem);
                                } else if (sym_table[i].type == TYPE_BOOLEAN) {
                                    printf("%s", elem ? "TRUE" : "FALSE");
                                } else if (sym_table[i].type == TYPE_REAL) {
                                    printf("%.6g", bits_to_float(elem));
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
                        } else if (sym_table[i].type == TYPE_REAL) {
                            printf("%s = %.6g\n", sym_table[i].name, bits_to_float(vm_vars[i]));
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

            case OP_FPRINT: {
                float val = bits_to_float(vm_pop(&sp));
                printf("%.6g", val);
                break;
            }

            case OP_PRINT_PADDED: {
                int width = vm_pop(&sp);
                int val = vm_pop(&sp);
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", val);
                vm_print_padded(buf, width);
                break;
            }

            case OP_PRINT_STR_PADDED: {
                int width = vm_pop(&sp);
                int val = vm_pop(&sp);
                int idx = vm_str_index(val);
                vm_print_padded(string_pool[idx], width);
                break;
            }

            case OP_PRINT_BOOL_PADDED: {
                int width = vm_pop(&sp);
                int val = vm_pop(&sp);
                vm_print_padded(val ? "TRUE" : "FALSE", width);
                break;
            }

            case OP_FPRINT_PADDED: {
                int width = vm_pop(&sp);
                float val = bits_to_float(vm_pop(&sp));
                char buf[64];
                snprintf(buf, sizeof(buf), "%.6g", val);
                vm_print_padded(buf, width);
                break;
            }

            case OP_FPRINT_PADDED_PRECISE: {
                int precision = vm_pop(&sp);
                int width = vm_pop(&sp);
                float val = bits_to_float(vm_pop(&sp));
                if (precision < 0 || precision > 20) {
                    fprintf(stderr, "VM Runtime Error: write field precision %d out of range (0..20)\n", precision);
                    fatal_abort();
                }
                char buf[64];
                snprintf(buf, sizeof(buf), "%.*f", precision, val);
                vm_print_padded(buf, width);
                break;
            }

            case OP_NEWLINE:
                printf("\n");
                break;

            case OP_READ:
                vm_read_global(vm_var_index(instr.arg), 1);
                break;

            case OP_READ_NOFLUSH:
                vm_read_global(vm_var_index(instr.arg), 0);
                break;

            case OP_READ_LOCAL_INT:
                vm_read_local_int(vm_local_index(fp, frame_sp, instr.arg), 1);
                break;

            case OP_READ_LOCAL_INT_NOFLUSH:
                vm_read_local_int(vm_local_index(fp, frame_sp, instr.arg), 0);
                break;

            case OP_READ_LOCAL_BOOL:
                vm_read_local_bool(vm_local_index(fp, frame_sp, instr.arg), 1);
                break;

            case OP_READ_LOCAL_BOOL_NOFLUSH:
                vm_read_local_bool(vm_local_index(fp, frame_sp, instr.arg), 0);
                break;

            case OP_READ_LOCAL_REAL:
                vm_read_local_real(vm_local_index(fp, frame_sp, instr.arg), 1);
                break;

            case OP_READ_LOCAL_REAL_NOFLUSH:
                vm_read_local_real(vm_local_index(fp, frame_sp, instr.arg), 0);
                break;

            case OP_READ_LOCAL_STR: {
                int slot = vm_local_index(fp, frame_sp, instr.arg);
                printf("> ");
                char line[MAX_STRING_LEN];
                if (!fgets(line, sizeof(line), stdin)) {
                    fprintf(stderr, "VM Runtime Error: Invalid string input\n");
                    fatal_abort();
                }
                size_t len = strlen(line);
                if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
                vm_frame_stack[slot] = vm_intern_string(line);
                break;
            }

            case OP_READ_LOCAL_CHAR: {
                int slot = vm_local_index(fp, frame_sp, instr.arg);
                printf("> ");
                char line[MAX_STRING_LEN];
                if (!fgets(line, sizeof(line), stdin)) {
                    fprintf(stderr, "VM Runtime Error: Invalid string input\n");
                    fatal_abort();
                }
                size_t len = strlen(line);
                if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
                if (strlen(line) != 1) {
                    fprintf(stderr, "VM Runtime Error: readln expected a single character, got \"%s\"\n", line);
                    fatal_abort();
                }
                vm_frame_stack[slot] = vm_intern_string(line);
                break;
            }

            case OP_EOF:
                vm_push(&sp, vm_peek_stdin() == EOF);
                break;

            case OP_EOLN: {
                int c = vm_peek_stdin();
                vm_push(&sp, c == EOF || c == '\n');
                break;
            }

            default:
                fprintf(stderr, "VM Runtime Error: Invalid opcode encountered (op=%d) at ip=%d\n", instr.op, ip - 1);
                fatal_abort();
        }
    }
}

