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

// Nested procedures' lexical (as opposed to vm_call_stack's DYNAMIC/
// caller) chain. vm_static_link[fp] = the fp of the ACTIVE ANCESTOR
// ACTIVATION that fp's own procedure was lexically declared inside, or
// -1 (OP_PUSH_STATIC_LINK's own sentinel) if there isn't a valid one to
// point at right now (the flat-namespace "called from outside my
// lexical scope" case - see common.h's OP_PUSH_STATIC_LINK comment).
// Set once per call, by OP_POP_STATIC_LINK, in a nested procedure's own
// prologue. Indexed by fp rather than by vm_call_stack[] position, so
// each activation of a RECURSIVE nested procedure gets its own
// independent entry for free - frame-stack space is never reused while
// a frame is still live, so two simultaneously-active calls to the same
// procedure always have two different fp values.
static int vm_static_link[MAX_FRAME_STACK];

// The heap ('new'/'dispose'/'p^') - this VM's only source of genuinely
// dynamic allocation (see MAX_HEAP_MEM's comment in common.h). Unlike
// vm_array_mem (whose layout is 100% fixed at compile time), how much of
// vm_heap_mem is actually in use is a RUNTIME quantity: vm_heap_count is
// a bump-allocation cursor mutated by OP_NEW, and vm_heap_freelist[]
// tracks blocks OP_DISPOSE has freed, bucketed by element size (1..
// MAX_RECORD_FIELDS - the only sizes OP_NEW/OP_DISPOSE ever request),
// so a later OP_NEW of the SAME size reuses one instead of growing
// vm_heap_count further. Each freelist is a classic intrusive linked
// list: a free block's own first int slot stores the NEXT free block's
// offset (or -1), needing no separate bookkeeping array.
static int vm_heap_mem[MAX_HEAP_MEM];
static int vm_heap_count;
static int vm_heap_freelist[MAX_RECORD_FIELDS + 1]; // index 0 unused (no
                                             // allocation is ever 0 ints)

// Virtual/dynamic method dispatch (OP_LOAD_VTABLE_SLOT/OP_STORE_VTABLE_SLOT
// in common.h) - one flat array, one row of MAX_CLASS_METHODS entries per
// class, indexed class_id * MAX_CLASS_METHODS + slot. Populated once, at
// program startup, by the vtable-init code parser.c's
// build_vtable_init_chain() prepends to the main body (OP_STORE_VTABLE_SLOT);
// read thereafter by every virtual call site (OP_LOAD_VTABLE_SLOT). Reset
// to -1 per slot below - never actually read at -1 in practice (every
// class's every method slot is unconditionally initialized by the
// vtable-init code before any user code runs), but matches this VM's
// general convention of leaving no region uninitialized garbage.
static int vm_vtables[MAX_POINTER_TYPES * MAX_CLASS_METHODS];

// One entry per class: its immediate parent's own class_id (or -1 for
// none/root) - populated at program startup by build_class_parent_init_
// chain()'s bytecode (OP_STORE_CLASS_PARENT), read by OP_IS_INSTANCE's
// runtime ancestor walk for 'is'/'as'. Same population pattern as
// vm_vtables[] above. Reset to -1 per slot below.
static int vm_class_parent[MAX_POINTER_TYPES];

// Command-line arguments the running program can see via ParamCount/
// ParamStr - see vm_set_program_args() below. vm_arg_str_idx[0] is
// always the .bin path; vm_arg_str_idx[1..vm_arg_count] are whatever
// extra arguments solvm's own CLI was given. Populated once, before
// run_vm() starts executing any user code.
static int vm_arg_count;
static int vm_arg_str_idx[MAX_PROGRAM_ARGS];

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
    int record_size; // TYPE_TYPED_FILE only - how many raw ints make up
                      // one record, cached from OP_TYPED_FILE_RESET/
                      // REWRITE's own packed arg (see common.h) for
                      // OP_FILE_SEEK/OP_FILE_SIZE/OP_TYPED_FILE_EOF to
                      // read later. Meaningless (0) for a TYPE_FILE
                      // (text) variable.
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

// A snapshot of every stack the VM maintains, taken when a 'try' is
// entered, so a 'raise' anywhere in its body - including several call
// frames deep - can unwind straight back to it in one step: restore
// sp/call_sp/fp/frame_sp from here (discarding whatever partial
// expression evaluation or nested-call state accumulated since), then
// jump to handler_ip. All VM-local state, so this needs no help from
// error.c's fatal_abort()/longjmp mechanism - see OP_RAISE.
typedef struct {
    int sp;
    int call_sp;
    int fp;
    int frame_sp;
    int handler_ip;
} ExceptHandler;

static ExceptHandler vm_except_stack[MAX_EXCEPT_DEPTH];
static int vm_current_exception_msg = 0; // string_pool[] index, set by the
                                          // most recent caught OP_RAISE -
                                          // see OP_EXCEPT_MSG.

static inline void vm_except_push(int *except_sp, int sp, int call_sp, int fp, int frame_sp, int handler_ip) {
    if (*except_sp >= MAX_EXCEPT_DEPTH - 1) {
        fprintf(stderr, "VM Runtime Error: Exception handler stack overflow (limit is %d) - too many nested 'try' blocks\n",
                MAX_EXCEPT_DEPTH);
        fatal_abort();
    }
    (*except_sp)++;
    vm_except_stack[*except_sp].sp = sp;
    vm_except_stack[*except_sp].call_sp = call_sp;
    vm_except_stack[*except_sp].fp = fp;
    vm_except_stack[*except_sp].frame_sp = frame_sp;
    vm_except_stack[*except_sp].handler_ip = handler_ip;
}

static inline ExceptHandler vm_except_pop(int *except_sp) {
    if (*except_sp < 0) {
        fprintf(stderr, "VM Runtime Error: 'end_try' with no matching 'try' (exception handler stack empty)\n");
        fatal_abort();
    }
    return vm_except_stack[(*except_sp)--];
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

// Same as vm_array_offset(), but for a 1D array of RECORDS: each element
// occupies record_elem_field_count contiguous ints (rather than 1), so
// the runtime index is scaled by that stride before the (also bounds-
// checked) field_offset is added - both index and field_offset are
// validated, since an out-of-range value of either would otherwise
// silently read/write a completely different array's memory (the same
// reasoning vm_array_offset()'s own comment gives for plain arrays).
static int vm_record_array_offset(int var_idx, int runtime_index, int field_offset) {
    vm_var_index(var_idx);
    Symbol *sym = &sym_table[var_idx];
    if (!sym->is_array || !sym->is_record_array) {
        fprintf(stderr, "VM Runtime Error: '%s' is not an array of records\n", sym->name);
        fatal_abort();
    }
    if (runtime_index < sym->array_lower || runtime_index > sym->array_upper) {
        fprintf(stderr, "VM Runtime Error: Array index %d out of range (%d..%d) for '%s'\n",
                runtime_index, sym->array_lower, sym->array_upper, sym->name);
        fatal_abort();
    }
    if (field_offset < 0 || field_offset >= sym->record_elem_field_count) {
        fprintf(stderr, "VM Runtime Error: Record field offset %d out of range (0..%d) for '%s'\n",
                field_offset, sym->record_elem_field_count - 1, sym->name);
        fatal_abort();
    }
    return sym->array_base + (runtime_index - sym->array_lower) * sym->record_elem_field_count + field_offset;
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

// Walks vm_static_link[] `levels_up` times starting from the CURRENT fp,
// returning the resulting ancestor activation's own fp. Aborts with a
// clear Runtime Error (rather than silently reading garbage) if fp isn't
// currently valid at all, or if the walk ever hits the -1 sentinel -
// this second case IS the concrete runtime trap for a nested procedure
// called from somewhere its lexical parent isn't an active ancestor (the
// flat-namespace escape hatch this project's nested-procedure design
// deliberately allows at compile time - see OP_PUSH_STATIC_LINK's own
// comment in common.h).
static int vm_enclosing_fp(int fp, int levels_up) {
    if (fp < 0) {
        fprintf(stderr, "VM Runtime Error: No active stack frame (enclosing variable access outside a procedure)\n");
        fatal_abort();
    }
    int target = fp;
    for (int i = 0; i < levels_up; i++) {
        if (target < 0 || target >= MAX_FRAME_STACK || vm_static_link[target] == -1) {
            fprintf(stderr, "VM Runtime Error: No active enclosing procedure activation - called from outside its lexical scope\n");
            fatal_abort();
        }
        target = vm_static_link[target];
    }
    return target;
}

// Resolves an OP_LOAD_ENCLOSING/OP_STORE_ENCLOSING/OP_PUSH_ENCLOSING_REF
// packed (levels_up, slot) arg to an absolute vm_frame_stack[] index.
// Unlike vm_local_index(), this can't bounds-check `slot` against the
// target frame's own actual size (only the CURRENTLY active frame's
// frame_sp is tracked at any given moment, not every ancestor's) - a
// plain array-bounds check is the best available defense for
// hand-written .sasm, matching this project's existing bounds-check
// style elsewhere (e.g. vm_var_index()).
static int vm_enclosing_index(int fp, int levels_up, int slot) {
    int target_fp = vm_enclosing_fp(fp, levels_up);
    if (slot < 0 || target_fp + slot < 0 || target_fp + slot >= MAX_FRAME_STACK) {
        fprintf(stderr, "VM Runtime Error: Enclosing local variable index %d out of range\n", slot);
        fatal_abort();
    }
    return target_fp + slot;
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

// Typed-file twin of vm_file_handle() above - checks TYPE_TYPED_FILE
// instead of TYPE_FILE. A separate function rather than a shared one
// with a type parameter, matching this file's own established
// "duplicate a narrow, easy-to-read helper rather than generalize it"
// convention (see e.g. is_typed_file_safe_scalar()'s own precedent in
// parser.c for the identical reasoning on the compiler side).
static FILE *vm_typed_file_handle(int idx, const char *action) {
    vm_var_index(idx);
    if (sym_table[idx].type != TYPE_TYPED_FILE) {
        fprintf(stderr, "VM Runtime Error: '%s' is not a typed file variable\n", sym_table[idx].name);
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

void vm_set_program_args(const char *program_name, int argc, char **argv) {
    if (argc > MAX_PROGRAM_ARGS - 1) {
        fprintf(stderr, "VM Runtime Error: Too many command-line arguments (limit is %d)\n", MAX_PROGRAM_ARGS - 1);
        fatal_abort();
    }
    vm_arg_str_idx[0] = vm_intern_string(program_name);
    vm_arg_count = argc;
    for (int i = 0; i < argc; i++) {
        vm_arg_str_idx[i + 1] = vm_intern_string(argv[i]);
    }
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

// Prints every global's current value by name - the same listing
// OP_HALT prints under -v, factored out so OP_DEBUG_SYMS can trigger it
// mid-execution too, not just at the very end of a run.
static void vm_dump_globals(void) {
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
            } else if (sym_table[i].is_record_array) {
                // Every field of every element, flattened -
                // this dump has no per-field type/name
                // info to show (that only ever lives in
                // parser.c's record_types[]), so it just
                // prints the raw ints (sym_table[i].type
                // is a meaningless dummy for a record
                // array - see is_record_array in
                // common.h - so every element prints as a
                // plain integer via the final 'else'
                // branch below).
                total = (sym_table[i].array_upper - sym_table[i].array_lower + 1) *
                        sym_table[i].record_elem_field_count;
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

void run_vm(void) {
    memset(vm_vars, 0, sizeof(vm_vars));
    memset(vm_array_mem, 0, sizeof(vm_array_mem));
    memset(vm_frame_stack, 0, sizeof(vm_frame_stack));
    memset(vm_open_files, 0, sizeof(vm_open_files)); // handle=NULL, filename="", assigned=0 for every slot
    memset(vm_heap_mem, 0, sizeof(vm_heap_mem));
    vm_heap_count = 0;
    for (int i = 0; i < MAX_RECORD_FIELDS + 1; i++) vm_heap_freelist[i] = -1;
    for (int i = 0; i < MAX_POINTER_TYPES * MAX_CLASS_METHODS; i++) vm_vtables[i] = -1;
    for (int i = 0; i < MAX_POINTER_TYPES; i++) vm_class_parent[i] = -1;
    for (int i = 0; i < MAX_FRAME_STACK; i++) vm_static_link[i] = -1; // -1, not 0 -
                                             // 0 is a valid fp value, so this can't
                                             // just be memset to zero like the plain
                                             // data arrays above
    int sp = -1;
    int call_sp = -1;
    int fp = -1;         // -1 = no active frame
    int frame_sp = -1;   // top of the frame stack (empty when -1)
    int except_sp = -1;  // top of vm_except_stack[] (empty when -1)
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

            case OP_LOAD_ARRAY_RECORD_FIELD: {
                int field_offset = vm_pop(&sp); // pushed second by codegen - on top
                int runtime_index = vm_pop(&sp); // pushed first
                int offset = vm_record_array_offset(instr.arg, runtime_index, field_offset);
                vm_push(&sp, vm_array_mem[offset]);
                break;
            }

            case OP_STORE_ARRAY_RECORD_FIELD: {
                int val = vm_pop(&sp);
                int field_offset = vm_pop(&sp);
                int runtime_index = vm_pop(&sp);
                int offset = vm_record_array_offset(instr.arg, runtime_index, field_offset);
                vm_array_mem[offset] = val;
                break;
            }

            case OP_STORE_ARRAY_RECORD_FIELD_CHAR: {
                int val = vm_pop(&sp);
                int field_offset = vm_pop(&sp);
                int runtime_index = vm_pop(&sp);
                int offset = vm_record_array_offset(instr.arg, runtime_index, field_offset);
                vm_check_char(val, sym_table[instr.arg].name);
                vm_array_mem[offset] = val;
                break;
            }

            case OP_NEW: {
                int elem_size = instr.arg;
                int offset;
                if (vm_heap_freelist[elem_size] != -1) {
                    offset = vm_heap_freelist[elem_size];
                    vm_heap_freelist[elem_size] = vm_heap_mem[offset]; // the freed block's own first slot stored the next-free link
                } else {
                    if (vm_heap_count + elem_size > MAX_HEAP_MEM) {
                        fprintf(stderr, "VM Runtime Error: Heap storage exhausted (limit is %d total elements)\n", MAX_HEAP_MEM);
                        fatal_abort();
                    }
                    offset = vm_heap_count;
                    vm_heap_count += elem_size;
                }
                vm_push(&sp, offset);
                break;
            }

            case OP_DISPOSE: {
                int elem_size = instr.arg;
                int ptr_val = vm_pop(&sp);
                if (ptr_val < 0) {
                    fprintf(stderr, "VM Runtime Error: Cannot dispose a nil pointer\n");
                    fatal_abort();
                }
                if (ptr_val >= vm_heap_count) {
                    fprintf(stderr, "VM Runtime Error: Invalid pointer value %d in dispose\n", ptr_val);
                    fatal_abort();
                }
                vm_heap_mem[ptr_val] = vm_heap_freelist[elem_size]; // link this block onto the front of the freelist
                vm_heap_freelist[elem_size] = ptr_val;
                break;
            }

            case OP_LOAD_HEAP_FIELD: {
                int base = vm_pop(&sp);
                if (base < 0) {
                    fprintf(stderr, "VM Runtime Error: Nil pointer dereference\n");
                    fatal_abort();
                }
                int offset = base + instr.arg;
                if (offset < 0 || offset >= vm_heap_count) {
                    fprintf(stderr, "VM Runtime Error: Invalid pointer value %d\n", base);
                    fatal_abort();
                }
                vm_push(&sp, vm_heap_mem[offset]);
                break;
            }

            case OP_STORE_HEAP_FIELD: {
                int val = vm_pop(&sp);
                int base = vm_pop(&sp);
                if (base < 0) {
                    fprintf(stderr, "VM Runtime Error: Nil pointer dereference\n");
                    fatal_abort();
                }
                int offset = base + instr.arg;
                if (offset < 0 || offset >= vm_heap_count) {
                    fprintf(stderr, "VM Runtime Error: Invalid pointer value %d\n", base);
                    fatal_abort();
                }
                vm_heap_mem[offset] = val;
                break;
            }

            case OP_STORE_HEAP_FIELD_CHAR: {
                int val = vm_pop(&sp);
                int base = vm_pop(&sp);
                if (base < 0) {
                    fprintf(stderr, "VM Runtime Error: Nil pointer dereference\n");
                    fatal_abort();
                }
                int offset = base + instr.arg;
                if (offset < 0 || offset >= vm_heap_count) {
                    fprintf(stderr, "VM Runtime Error: Invalid pointer value %d\n", base);
                    fatal_abort();
                }
                vm_check_char(val, "pointer dereference");
                vm_heap_mem[offset] = val;
                break;
            }

            case OP_LOAD_HEAP_ARRAY_FIELD: {
                int index = vm_pop(&sp);
                int base = vm_pop(&sp);
                if (base < 0) {
                    fprintf(stderr, "VM Runtime Error: Nil pointer dereference\n");
                    fatal_abort();
                }
                int offset = base + instr.arg + index;
                if (offset < 0 || offset >= vm_heap_count) {
                    fprintf(stderr, "VM Runtime Error: Invalid pointer value %d\n", base);
                    fatal_abort();
                }
                vm_push(&sp, vm_heap_mem[offset]);
                break;
            }

            case OP_STORE_HEAP_ARRAY_FIELD: {
                int val = vm_pop(&sp);
                int index = vm_pop(&sp);
                int base = vm_pop(&sp);
                if (base < 0) {
                    fprintf(stderr, "VM Runtime Error: Nil pointer dereference\n");
                    fatal_abort();
                }
                int offset = base + instr.arg + index;
                if (offset < 0 || offset >= vm_heap_count) {
                    fprintf(stderr, "VM Runtime Error: Invalid pointer value %d\n", base);
                    fatal_abort();
                }
                vm_heap_mem[offset] = val;
                break;
            }

            case OP_STORE_HEAP_ARRAY_FIELD_CHAR: {
                int val = vm_pop(&sp);
                int index = vm_pop(&sp);
                int base = vm_pop(&sp);
                if (base < 0) {
                    fprintf(stderr, "VM Runtime Error: Nil pointer dereference\n");
                    fatal_abort();
                }
                int offset = base + instr.arg + index;
                if (offset < 0 || offset >= vm_heap_count) {
                    fprintf(stderr, "VM Runtime Error: Invalid pointer value %d\n", base);
                    fatal_abort();
                }
                vm_check_char(val, "pointer dereference");
                vm_heap_mem[offset] = val;
                break;
            }

            case OP_LOAD_VTABLE_SLOT: {
                int class_id = vm_pop(&sp);
                int flat_index = class_id * MAX_CLASS_METHODS + instr.arg;
                if (flat_index < 0 || flat_index >= MAX_POINTER_TYPES * MAX_CLASS_METHODS) {
                    fprintf(stderr, "VM Runtime Error: Invalid class tag %d\n", class_id);
                    fatal_abort();
                }
                vm_push(&sp, vm_vtables[flat_index]);
                break;
            }

            case OP_STORE_VTABLE_SLOT: {
                int target = vm_pop(&sp);
                vm_vtables[instr.arg] = target;
                break;
            }

            case OP_IS_INSTANCE: {
                int obj = vm_pop(&sp);
                if (obj < 0) { // nil - not an instance of anything, but NOT
                                // a fatal error like OP_LOAD_HEAP_FIELD's own
                                // nil check: 'nil is X' must be False.
                    vm_push(&sp, 0);
                    break;
                }
                if (obj >= vm_heap_count) {
                    fprintf(stderr, "VM Runtime Error: Invalid pointer value %d\n", obj);
                    fatal_abort();
                }
                int tag = vm_heap_mem[obj]; // offset 0 - the hidden runtime type tag
                int result = 0;
                while (tag != -1) {
                    if (tag == instr.arg) { result = 1; break; }
                    if (tag < 0 || tag >= MAX_POINTER_TYPES) {
                        fprintf(stderr, "VM Runtime Error: Invalid class tag %d\n", tag);
                        fatal_abort();
                    }
                    tag = vm_class_parent[tag];
                }
                vm_push(&sp, result);
                break;
            }

            case OP_STORE_CLASS_PARENT: {
                int parent = vm_pop(&sp);
                vm_class_parent[instr.arg] = parent;
                break;
            }

            case OP_FILE_ASSIGN: {
                int idx = vm_var_index(instr.arg);
                if (sym_table[idx].type != TYPE_FILE && sym_table[idx].type != TYPE_TYPED_FILE) {
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
                if (sym_table[idx].type != TYPE_FILE && sym_table[idx].type != TYPE_TYPED_FILE) {
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

            case OP_TYPED_FILE_RESET:
            case OP_TYPED_FILE_REWRITE: {
                int file_sym_idx = instr.arg % MAX_SYMBOLS;
                int record_size = instr.arg / MAX_SYMBOLS;
                int idx = vm_var_index(file_sym_idx);
                if (sym_table[idx].type != TYPE_TYPED_FILE) {
                    fprintf(stderr, "VM Runtime Error: '%s' is not a typed file variable\n", sym_table[idx].name);
                    fatal_abort();
                }
                if (!vm_open_files[idx].assigned) {
                    fprintf(stderr, "VM Runtime Error: File '%s' was never assign()ed a filename\n", sym_table[idx].name);
                    fatal_abort();
                }
                if (vm_open_files[idx].handle) fclose(vm_open_files[idx].handle); // reset/rewrite on an already-open file just reopens it, matching real Pascal
                const char *mode = (instr.op == OP_TYPED_FILE_RESET) ? "rb" : "wb";
                vm_open_files[idx].handle = fopen(vm_open_files[idx].filename, mode);
                if (!vm_open_files[idx].handle) {
                    fprintf(stderr, "VM Runtime Error: Cannot open file '%s' for %s (%s)\n",
                            vm_open_files[idx].filename, (instr.op == OP_TYPED_FILE_RESET) ? "reading" : "writing", strerror(errno));
                    fatal_abort();
                }
                vm_open_files[idx].record_size = record_size;
                break;
            }

            case OP_READ_TYPED_FILE_INT: {
                FILE *f = vm_typed_file_handle(instr.arg, "read");
                int val;
                if (fread(&val, sizeof(int), 1, f) != 1) {
                    fprintf(stderr, "VM Runtime Error: Attempted to read past the end of typed file '%s'\n", sym_table[vm_var_index(instr.arg)].name);
                    fatal_abort();
                }
                vm_push(&sp, val);
                break;
            }

            case OP_WRITE_TYPED_FILE_INT: {
                FILE *f = vm_typed_file_handle(instr.arg, "write");
                int val = vm_pop(&sp);
                fwrite(&val, sizeof(int), 1, f);
                break;
            }

            case OP_FILE_SEEK: {
                FILE *f = vm_typed_file_handle(instr.arg, "seek");
                int n = vm_pop(&sp);
                int idx = vm_var_index(instr.arg);
                if (fseek(f, (long)n * vm_open_files[idx].record_size * (long)sizeof(int), SEEK_SET) != 0) {
                    fprintf(stderr, "VM Runtime Error: 'seek' failed on file '%s' (%s)\n", sym_table[idx].name, strerror(errno));
                    fatal_abort();
                }
                break;
            }

            case OP_FILE_SIZE: {
                FILE *f = vm_typed_file_handle(instr.arg, "filesize");
                int idx = vm_var_index(instr.arg);
                long cur = ftell(f);
                fseek(f, 0, SEEK_END);
                long end = ftell(f);
                fseek(f, cur, SEEK_SET);
                vm_push(&sp, (int)(end / (vm_open_files[idx].record_size * (long)sizeof(int))));
                break;
            }

            case OP_TYPED_FILE_EOF: {
                FILE *f = vm_typed_file_handle(instr.arg, "eof");
                long cur = ftell(f);
                fseek(f, 0, SEEK_END);
                long end = ftell(f);
                fseek(f, cur, SEEK_SET);
                vm_push(&sp, cur >= end);
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

            case OP_SKIP_PENDING_NEWLINE: {
                int c = vm_peek_stdin();
                if (c == '\n') getchar();
                break;
            }

            case OP_SKIP_PENDING_NEWLINE_FILE: {
                FILE *f = vm_file_handle(instr.arg, "read");
                int c = fgetc(f);
                if (c != '\n' && c != EOF) ungetc(c, f);
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
                // Shifting a signed int left is undefined behavior in C
                // whenever the result (or the bit shifted into the sign
                // bit) doesn't fit back in a signed int - notably b == 31
                // for any nonzero a. Shifting the same bit pattern as an
                // unsigned int instead is always well-defined and yields
                // the identical two's-complement result this code already
                // relied on in practice, just without the UB.
                vm_push(&sp, (int)((unsigned int)a << b));
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

            case OP_SWAP: {
                int b = vm_pop(&sp);
                int a = vm_pop(&sp);
                vm_push(&sp, b);
                vm_push(&sp, a);
                break;
            }

            case OP_OVER: {
                int b = vm_pop(&sp);
                int a = vm_pop(&sp);
                vm_push(&sp, a);
                vm_push(&sp, b);
                vm_push(&sp, a);
                break;
            }

            case OP_ROT: {
                int c = vm_pop(&sp);
                int b = vm_pop(&sp);
                int a = vm_pop(&sp);
                vm_push(&sp, b);
                vm_push(&sp, c);
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

            case OP_TRY:
                vm_except_push(&except_sp, sp, call_sp, fp, frame_sp, instr.arg);
                break;

            case OP_END_TRY:
                vm_except_pop(&except_sp);
                break;

            case OP_RAISE: {
                int msg_idx = vm_str_index(vm_pop(&sp));
                if (except_sp < 0) {
                    fprintf(stderr, "VM Runtime Error: Unhandled exception: %s\n", string_pool[msg_idx]);
                    fatal_abort();
                }
                ExceptHandler h = vm_except_pop(&except_sp);
                sp = h.sp;
                call_sp = h.call_sp;
                fp = h.fp;
                frame_sp = h.frame_sp;
                vm_current_exception_msg = msg_idx;
                ip = h.handler_ip;
                break;
            }

            case OP_EXCEPT_MSG:
                vm_push(&sp, vm_current_exception_msg);
                break;

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

            case OP_PARAM_COUNT:
                vm_push(&sp, vm_arg_count);
                break;

            case OP_PARAM_STR: {
                int idx = vm_pop(&sp);
                if (idx < 0 || idx > vm_arg_count) {
                    // Out-of-range - real Pascal/Free Pascal's own
                    // documented ParamStr behavior returns an empty
                    // string here, not a runtime error (unlike this
                    // VM's usual abort-on-out-of-range convention for
                    // array indexing) - see OP_PARAM_STR's own comment
                    // in common.h.
                    vm_push(&sp, vm_intern_string(""));
                } else {
                    vm_push(&sp, vm_arg_str_idx[idx]);
                }
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

            case OP_CALL_INDIRECT: {
                // Same as OP_CALL, except the jump target was only known
                // at RUNTIME (a procedure passed in as a procedural/
                // functional parameter - see NODE_CALL_INDIRECT in
                // codegen.c), so it comes off the stack instead of a
                // compile-time-constant arg. No explicit bounds check on
                // it - same convention OP_CALL itself already relies on:
                // a bad address is still caught cleanly, as a VM Runtime
                // Error, by the fetch loop's own ip bounds check on the
                // very next cycle.
                int target = vm_pop(&sp);
                vm_call_push(&call_sp, ip, fp, frame_sp);
                ip = target;
                break;
            }

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

            case OP_PUSH_STATIC_LINK:
                if (instr.arg == 0) {
                    vm_push(&sp, fp); // calling my own direct lexical child
                } else if (instr.arg == -1) {
                    vm_push(&sp, -1); // sentinel: no valid enclosing activation (see vm_enclosing_fp())
                } else {
                    vm_push(&sp, vm_enclosing_fp(fp, instr.arg));
                }
                break;

            case OP_POP_STATIC_LINK:
                // fp is already this (nested) procedure's OWN frame here -
                // OP_ENTER (always emitted for a nested procedure, even
                // with zero of its own locals - see generate_program() in
                // codegen.c) runs first and sets it, before this pops the
                // static link the caller pushed.
                vm_static_link[fp] = vm_pop(&sp);
                break;

            case OP_LOAD_ENCLOSING:
                vm_push(&sp, vm_frame_stack[vm_enclosing_index(fp, instr.arg >> 12, instr.arg & 0xFFF)]);
                break;

            case OP_STORE_ENCLOSING: {
                int val = vm_pop(&sp);
                vm_frame_stack[vm_enclosing_index(fp, instr.arg >> 12, instr.arg & 0xFFF)] = val;
                break;
            }

            case OP_PUSH_ENCLOSING_REF: {
                int abs_index = vm_enclosing_index(fp, instr.arg >> 12, instr.arg & 0xFFF);
                vm_push(&sp, -(abs_index + 1)); // literally OP_PUSH_LOCAL_REF's own encoding
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
                    vm_dump_globals();
                }
                return;

            case OP_DEBUG_STACK: {
                printf("[DEBUG] stack (%d value%s):", sp + 1, sp == 0 ? "" : "s");
                for (int i = 0; i <= sp; i++) printf(" %d", vm_stack[i]);
                printf("\n");
                break;
            }

            case OP_DEBUG_SYMS:
                printf("[DEBUG] globals:\n");
                vm_dump_globals();
                break;

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

