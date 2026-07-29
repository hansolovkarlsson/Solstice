#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "parser.h"
#include "lexer.h"
#include "error.h"

// Reinterprets a float's bits as an int-sized bit pattern - see vm.c for
// the full explanation (this is how a 'real' value shares the same
// int-sized storage slots every other type uses). Only float_to_bits is
// needed here (to encode a real literal's value at parse time); the
// reverse isn't needed until runtime, so it stays vm.c-only.
static int float_to_bits(float f) {
    int bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

static const char *current_filename = "<source>";

// Tracks how many while/for/repeat bodies we're currently parsing inside
// of (nested ifs don't count). Used to reject break/continue outside a
// loop right where they're written, rather than only failing at codegen.
static int loop_depth = 0;

// The parameters and local variables of whichever procedure is currently
// being parsed - a fresh, tiny scope, reset at the start of each
// procedure_declaration() and cleared again once it's done. Empty (and
// in_procedure == 0) while parsing the main program body, so every
// identifier there resolves against sym_table exactly as before
// procedures existed.
#define MAX_LOCALS 32
typedef struct {
    char name[MAX_NAME];
    DataType type;      // scalar type, or array ELEMENT type if is_array/is_array_ref
    int is_array;
    int array_sym_idx;  // only meaningful if is_array: the mangled,
                        // hidden sym_table[] index this local array's
                        // data actually lives at (see add_local_array)
    int is_array_ref;   // a by-reference array PARAMETER - the local
                        // slot holds a runtime sym_table[] index, not
                        // the array's data directly
    int array_lower;    // only meaningful if is_array_ref: the declared
    int array_upper;    // bounds an argument passed here must match
} LocalSymbol;
static LocalSymbol current_locals[MAX_LOCALS];
static int current_local_count = 0;
static int in_procedure = 0;

// Records are implemented as pure syntactic sugar over ordinary global
// variables - a record variable doesn't get one storage location of its
// own at all. Instead, declaring 'var p: TPerson' creates one ordinary
// hidden global per field (mangled "p__fieldname", via add_var()/
// add_array_var() exactly as if the user had declared each field
// separately), and 'p.field' just resolves, at parse time, to a
// reference to that mangled symbol. This is the same trick this project
// already uses for local arrays (see add_local_array) - and the payoff
// is the same: field access, whole-field type checking, codegen, dead-
// code elimination, and the -v dump all already work per-symbol, so they
// all work for record fields too, with zero new runtime machinery. The
// real cost is that this doesn't generalize to anything needing a
// genuinely runtime-selectable record (arrays of records, by-reference
// record parameters) - not needed for this feature's current scope, but
// would need a real rethink (an addressing model, like arrays use) if
// ever added.
#define MAX_RECORD_TYPES 20
#define MAX_RECORD_FIELDS 16
#define MAX_RECORD_VARS 50

typedef struct {
    char name[MAX_NAME];
    DataType type;
    int is_array;
    int array_lower, array_upper; // only meaningful if is_array
} RecordField;

typedef struct {
    char name[MAX_NAME];
    RecordField fields[MAX_RECORD_FIELDS];
    int field_count;
} RecordTypeDef;
static RecordTypeDef record_types[MAX_RECORD_TYPES];
static int record_type_count = 0;

typedef struct {
    char name[MAX_NAME];       // the record VARIABLE's own name, e.g. "p"
    int record_type_idx;       // which record_types[] entry it's an instance of
    int field_sym_idx[MAX_RECORD_FIELDS]; // sym_table[] index of each
                                // field's mangled global, in the record
                                // type's declared field order
} RecordVarDef;
static RecordVarDef record_vars[MAX_RECORD_VARS];
static int record_var_count = 0;

static int find_record_type(const char *name) {
    for (int i = 0; i < record_type_count; i++) {
        if (strcmp(record_types[i].name, name) == 0) return i;
    }
    return -1;
}

static int find_record_var(const char *name) {
    for (int i = 0; i < record_var_count; i++) {
        if (strcmp(record_vars[i].name, name) == 0) return i;
    }
    return -1;
}

// -1 if record_types[record_type_idx] has no field with this name.
static int find_record_field(int record_type_idx, const char *field_name) {
    RecordTypeDef *rt = &record_types[record_type_idx];
    for (int i = 0; i < rt->field_count; i++) {
        if (strcmp(rt->fields[i].name, field_name) == 0) return i;
    }
    return -1;
}

// The proc_table index of the function whose body is currently being
// parsed, or -1 if we're not inside a function body (either not inside
// any procedure/function at all, or inside a plain procedure). Used to
// recognize 'FunctionName := expr;' inside that function's own body as
// setting its return value, rather than an ordinary assignment.
static int current_function_idx = -1;

const char *get_current_filename(void) {
    return current_filename;
}

static void compile_error(int line, const char *fmt, ...) {
    fprintf(stderr, "%s:%d: Compile Error: ", current_filename, line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    fatal_abort();
}

ASTNode *create_node(NodeType type) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    if (!node) { fprintf(stderr, "Memory failure\n"); fatal_abort(); }
    node->type = type;
    node->expression_type = TYPE_UNKNOWN;
    node->line = token.line;
    return node;
}

// Soft lookup - returns -1 if `name` isn't a declared global variable,
// rather than erroring. Used where the caller needs to check "is this a
// global X" before deciding how to proceed (see try_get_array_bounds
// below), as opposed to find_var()'s "this MUST be declared, error
// immediately if not" contract.
static int find_var_soft(const char *name) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) return i;
    }
    return -1;
}

static int find_var(const char *name) {
    int idx = find_var_soft(name);
    if (idx == -1) {
        compile_error(token.line, "Unknown identifier '%s'", name);
    }
    return idx;
}

static void add_var(const char *name, DataType type) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) {
            compile_error(token.line, "Duplicate variable declaration '%s'", name);
        }
    }
    if (sym_count >= MAX_SYMBOLS) {
        compile_error(token.line, "Too many variable declarations (limit is %d)", MAX_SYMBOLS);
    }
    strcpy(sym_table[sym_count].name, name);
    sym_table[sym_count].type = type;
    sym_table[sym_count].is_array = 0;
    sym_table[sym_count].array_lower = 0;
    sym_table[sym_count].array_upper = 0;
    sym_table[sym_count].array_base = 0;
    sym_count++;
}

// Same as add_var(), but for 'name: array[lower..upper] of type'. Bounds
// must already be validated (lower <= upper) by the caller.
static void add_array_var(const char *name, DataType elem_type, int lower, int upper) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) {
            compile_error(token.line, "Duplicate variable declaration '%s'", name);
        }
    }
    if (sym_count >= MAX_SYMBOLS) {
        compile_error(token.line, "Too many variable declarations (limit is %d)", MAX_SYMBOLS);
    }
    int size = upper - lower + 1;
    if (array_mem_count + size > MAX_ARRAY_MEM) {
        compile_error(token.line, "Array storage exhausted (limit is %d total elements across all arrays)", MAX_ARRAY_MEM);
    }
    strcpy(sym_table[sym_count].name, name);
    sym_table[sym_count].type = elem_type;
    sym_table[sym_count].is_array = 1;
    sym_table[sym_count].array_lower = lower;
    sym_table[sym_count].array_upper = upper;
    sym_table[sym_count].array_base = array_mem_count;
    sym_table[sym_count].is_2d = 0; // defensive reset (see comment above the Symbol struct)
    array_mem_count += size;
    sym_count++;
}

// Same as add_array_var(), but for a 2D array ('name: array[lo1..hi1,
// lo2..hi2] of type'). Bounds must already be validated (lower <= upper,
// each dimension) by the caller. Reserves dim1_size * dim2_size elements
// - the whole flattened, row-major region - from the same shared
// array-memory pool every array (1D or 2D) draws from.
static void add_array_var_2d(const char *name, DataType elem_type, int lower, int upper, int lower2, int upper2) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) {
            compile_error(token.line, "Duplicate variable declaration '%s'", name);
        }
    }
    if (sym_count >= MAX_SYMBOLS) {
        compile_error(token.line, "Too many variable declarations (limit is %d)", MAX_SYMBOLS);
    }
    int dim1_size = upper - lower + 1;
    int dim2_size = upper2 - lower2 + 1;
    int size = dim1_size * dim2_size;
    if (array_mem_count + size > MAX_ARRAY_MEM) {
        compile_error(token.line, "Array storage exhausted (limit is %d total elements across all arrays)", MAX_ARRAY_MEM);
    }
    strcpy(sym_table[sym_count].name, name);
    sym_table[sym_count].type = elem_type;
    sym_table[sym_count].is_array = 1;
    sym_table[sym_count].array_lower = lower;
    sym_table[sym_count].array_upper = upper;
    sym_table[sym_count].array_base = array_mem_count;
    sym_table[sym_count].is_2d = 1;
    sym_table[sym_count].array_lower2 = lower2;
    sym_table[sym_count].array_upper2 = upper2;
    array_mem_count += size;
    sym_count++;
}

// Declares a GLOBAL record variable of the given record type: creates one
// ordinary hidden global symbol per field (mangled "name__fieldname"),
// via add_var()/add_array_var() exactly as if the user had declared each
// field as its own separate global variable. See the comment above
// RecordTypeDef for why this makes field access, type checking, codegen,
// and DCE all work for free, with zero new runtime machinery.
static void add_record_var(const char *name, int record_type_idx) {
    if (find_var_soft(name) != -1 || find_record_var(name) != -1) {
        compile_error(token.line, "Duplicate variable declaration '%s'", name);
    }
    if (record_var_count >= MAX_RECORD_VARS) {
        compile_error(token.line, "Too many record variables (limit is %d)", MAX_RECORD_VARS);
    }
    RecordTypeDef *rt = &record_types[record_type_idx];
    RecordVarDef *rv = &record_vars[record_var_count];
    strcpy(rv->name, name);
    rv->record_type_idx = record_type_idx;
    for (int i = 0; i < rt->field_count; i++) {
        RecordField *f = &rt->fields[i];
        // "name__fieldname" - checked explicitly rather than letting
        // snprintf silently truncate, which could make two different
        // field manglings collide.
        if (strlen(name) + 2 + strlen(f->name) >= MAX_NAME) {
            compile_error(token.line, "Record variable/field name '%s.%s' too long (limit is %d characters combined)",
                          name, f->name, MAX_NAME - 3);
        }
        char mangled[2 * MAX_NAME];
        snprintf(mangled, sizeof(mangled), "%s__%s", name, f->name);
        if (f->is_array) {
            add_array_var(mangled, f->type, f->array_lower, f->array_upper);
        } else {
            add_var(mangled, f->type);
        }
        rv->field_sym_idx[i] = sym_count - 1; // add_var/add_array_var just incremented sym_count
    }
    record_var_count++;
}

// Soft lookup - returns -1 rather than erroring, since the caller needs
// to decide "is this name a procedure or a variable?" before knowing
// which error (if any) is appropriate.
static int find_proc(const char *name) {
    for (int i = 0; i < proc_count; i++) {
        if (strcmp(proc_table[i].name, name) == 0) return i;
    }
    return -1;
}

// Registers a procedure's name immediately (before its body is parsed) -
// required so a procedure can call itself (recursion): by the time the
// parser is inside the body, find_proc() already sees this entry.
// Procedures and variables are stored in separate tables but share one
// namespace from the user's perspective, so this checks against both.
static int add_proc(const char *name) {
    if (find_proc(name) != -1) {
        compile_error(token.line, "Duplicate procedure declaration '%s'", name);
    }
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) {
            compile_error(token.line, "'%s' is already declared as a variable", name);
        }
    }
    if (proc_count >= MAX_PROCEDURES) {
        compile_error(token.line, "Too many procedure declarations (limit is %d)", MAX_PROCEDURES);
    }
    strcpy(proc_table[proc_count].name, name);
    proc_table[proc_count].entry_address = -1; // resolved during codegen
    proc_table[proc_count].body = NULL;        // set once the body is parsed
    proc_table[proc_count].is_forward = 0;     // may be set to 1 right after, if this is a forward declaration
    proc_table[proc_count].param_count = 0;    // defensive reset (see comment above the struct)
    proc_table[proc_count].is_function = 0;    // may be set to 1 right after, if this is a function
    return proc_count++;
}

// Soft lookup, like find_proc() - returns -1 if `name` isn't a
// parameter/local of the procedure currently being parsed (or if we're
// not currently parsing a procedure body at all). A local shadows a
// procedure name or a global variable of the same name, matching
// standard Pascal lexical scoping - which is exactly why every call site
// below checks this first.
static int find_local(const char *name) {
    if (!in_procedure) return -1;
    for (int i = 0; i < current_local_count; i++) {
        if (strcmp(current_locals[i].name, name) == 0) return i;
    }
    return -1;
}

// Checks whether `name` refers to a 1D array - global, local (which
// reuses the mangled-global mechanism, so its bounds live in sym_table[]
// too), or a by-reference array parameter (whose OWN declared bounds are
// used - guaranteed to match whatever array is actually passed, since
// this compiler requires an array argument to exactly match the
// parameter's declared bounds at every call site). If so, writes its
// bounds to *lower/*upper and returns 1; otherwise returns 0 without
// touching them. Doesn't consume any tokens - this is a pure name
// lookup, used to peek whether low()/high()/length()'s argument is a
// bare array name before deciding how to parse the rest of the call.
// 2D arrays are explicitly rejected with a compile error rather than
// silently returning one dimension's bounds - there's no defined answer
// yet for "which dimension" here.
static int try_get_array_bounds(const char *name, int *lower, int *upper) {
    int local_idx = find_local(name);
    if (local_idx != -1) {
        if (current_locals[local_idx].is_array) {
            int sym_idx = current_locals[local_idx].array_sym_idx;
            if (sym_table[sym_idx].is_2d) {
                compile_error(token.line, "'%s' is a 2D array - low/high/length don't support 2D arrays yet", name);
            }
            *lower = sym_table[sym_idx].array_lower;
            *upper = sym_table[sym_idx].array_upper;
            return 1;
        }
        if (current_locals[local_idx].is_array_ref) {
            *lower = current_locals[local_idx].array_lower;
            *upper = current_locals[local_idx].array_upper;
            return 1;
        }
        return 0; // a local, but not an array
    }
    int global_idx = find_var_soft(name);
    if (global_idx != -1 && sym_table[global_idx].is_array) {
        if (sym_table[global_idx].is_2d) {
            compile_error(token.line, "'%s' is a 2D array - low/high/length don't support 2D arrays yet", name);
        }
        *lower = sym_table[global_idx].array_lower;
        *upper = sym_table[global_idx].array_upper;
        return 1;
    }
    return 0;
}

static int add_local(const char *name, DataType type) {
    if (find_local(name) != -1) {
        compile_error(token.line, "Duplicate parameter or local variable '%s'", name);
    }
    if (current_local_count >= MAX_LOCALS) {
        compile_error(token.line, "Too many parameters/local variables (limit is %d)", MAX_LOCALS);
    }
    strcpy(current_locals[current_local_count].name, name);
    current_locals[current_local_count].type = type;
    current_locals[current_local_count].is_array = 0;
    current_locals[current_local_count].array_sym_idx = 0;
    current_locals[current_local_count].is_array_ref = 0;
    current_locals[current_local_count].array_lower = 0;
    current_locals[current_local_count].array_upper = 0;
    return current_local_count++;
}

// Registers a by-reference array parameter. Just an ordinary local slot
// (it holds a runtime sym_table[] index - see parse_array_ref_argument
// and generate_code's NODE_REF_ARRAY_ACCESS/ASSIGN cases), plus the
// declared bounds every call site's argument must exactly match.
static int add_local_array_ref(const char *name, DataType elem_type, int lower, int upper) {
    int idx = add_local(name, elem_type);
    current_locals[idx].is_array_ref = 1;
    current_locals[idx].array_lower = lower;
    current_locals[idx].array_upper = upper;
    return idx;
}

// Registers a procedure-local array ('var arr: array[lo..hi] of T;'
// inside a procedure body). Reuses the exact same global sym_table[] /
// vm_array_mem[] machinery an ordinary top-level array uses - not the
// per-call frame stack scalar locals use - under a hidden, mangled name
// (so two different procedures can each have their own 'temp' array
// without colliding). The practical consequence: unlike a scalar local,
// this array is allocated ONCE for the whole program and SHARED across
// every call to this procedure, including recursive ones - it does not
// get fresh, isolated storage per call.
static int add_local_array(const char *name, DataType elem_type, int lower, int upper) {
    if (find_local(name) != -1) {
        compile_error(token.line, "Duplicate parameter or local variable '%s'", name);
    }
    if (current_local_count >= MAX_LOCALS) {
        compile_error(token.line, "Too many parameters/local variables (limit is %d)", MAX_LOCALS);
    }
    char mangled[MAX_NAME];
    snprintf(mangled, MAX_NAME, "__local_arr%d", sym_count);
    int array_sym_idx = sym_count; // add_array_var() is about to append here
    add_array_var(mangled, elem_type, lower, upper);

    strcpy(current_locals[current_local_count].name, name);
    current_locals[current_local_count].type = elem_type;
    current_locals[current_local_count].is_array = 1;
    current_locals[current_local_count].array_sym_idx = array_sym_idx;
    return current_local_count++;
}

// Adds a string literal to the pool, reusing an existing slot if the exact
// same text was already interned (this is a space-saving dedup, not a
// correctness requirement - string equality is checked via strcmp at
// runtime, not by comparing pool indices).
static int intern_string(const char *s) {
    for (int i = 0; i < string_count; i++) {
        if (strcmp(string_pool[i], s) == 0) return i;
    }
    if (string_count >= MAX_STRINGS) {
        compile_error(token.line, "Too many distinct string literals (limit is %d)", MAX_STRINGS);
    }
    strcpy(string_pool[string_count], s);
    return string_count++;
}

static void match(TokenType type) {
    if (token.type == type) next_token();
    else compile_error(token.line, "Unexpected token '%s'", token.text[0] ? token.text : "EOF");
}

// Parses a compile-time-constant integer literal, e.g. for array bounds
// (array[1..10], array[-5..5]). Not a general expression - array sizes
// must be known at compile time in this language.
static int parse_int_literal(void) {
    int sign = 1;
    if (token.type == TOKEN_MINUS) {
        sign = -1;
        match(TOKEN_MINUS);
    }
    if (token.type != TOKEN_NUMBER) {
        compile_error(token.line, "Expected an integer literal");
    }
    int val = token.value * sign;
    match(TOKEN_NUMBER);
    return val;
}

// A scalar type only - used for parameters and procedure-local variables,
// neither of which support arrays in this increment.
static DataType parse_scalar_type(void) {
    if (token.type == TOKEN_INTEGER) { match(TOKEN_INTEGER); return TYPE_INTEGER; }
    if (token.type == TOKEN_BOOLEAN) { match(TOKEN_BOOLEAN); return TYPE_BOOLEAN; }
    if (token.type == TOKEN_STRING_TYPE) { match(TOKEN_STRING_TYPE); return TYPE_STRING; }
    if (token.type == TOKEN_CHAR_TYPE) { match(TOKEN_CHAR_TYPE); return TYPE_CHAR; }
    if (token.type == TOKEN_REAL_TYPE) { match(TOKEN_REAL_TYPE); return TYPE_REAL; }
    compile_error(token.line, "Unknown type (expected 'integer', 'boolean', 'string', 'char', or 'real')");
    return TYPE_UNKNOWN;
}

// Parses 'name {, name} : scalar-type' or 'name {, name} : array[lo..hi]
// of scalar-type' - the shared inner syntax of a parameter group and a
// procedure-local var declaration line.
#define MAX_GROUP_NAMES 16
typedef struct {
    char names[MAX_GROUP_NAMES][MAX_NAME];
    int count;
    DataType type;   // scalar type, or array ELEMENT type if is_array
    int is_array;
    int array_lower;
    int array_upper;
} NameGroup;

static NameGroup parse_name_group(void) {
    NameGroup g;
    g.count = 0;
    g.is_array = 0;
    if (token.type != TOKEN_IDENTIFIER) compile_error(token.line, "Expected an identifier");
    strcpy(g.names[g.count++], token.text);
    match(TOKEN_IDENTIFIER);
    while (token.type == TOKEN_COMMA) {
        match(TOKEN_COMMA);
        if (g.count >= MAX_GROUP_NAMES) {
            compile_error(token.line, "Too many names in one group (limit is %d)", MAX_GROUP_NAMES);
        }
        if (token.type != TOKEN_IDENTIFIER) compile_error(token.line, "Expected an identifier");
        strcpy(g.names[g.count++], token.text);
        match(TOKEN_IDENTIFIER);
    }
    match(TOKEN_COLON);
    if (token.type == TOKEN_ARRAY) {
        match(TOKEN_ARRAY);
        match(TOKEN_LBRACKET);
        g.array_lower = parse_int_literal();
        match(TOKEN_DOTDOT);
        g.array_upper = parse_int_literal();
        match(TOKEN_RBRACKET);
        if (g.array_upper < g.array_lower) {
            compile_error(token.line, "Invalid array bounds: upper (%d) must be >= lower (%d)", g.array_upper, g.array_lower);
        }
        match(TOKEN_OF);
        g.type = parse_scalar_type();
        g.is_array = 1;
    } else {
        g.type = parse_scalar_type();
    }
    return g;
}

static ASTNode *expression(void);
static ASTNode *statement(void);
static ASTNode *statement_list(void);
static ASTNode *compound_statement(void);
static void subroutine_declaration(int is_function_decl);

// Parses '(' [expr {',' expr}] ')' as a call's argument list, or nothing
// if there are no parens at all - both are accepted for a zero-argument
// call, matching how write/writeln also tolerate the parenless form.
// Checks the parsed count against proc_table[proc_idx]'s declared
// param_count, erroring immediately (with proc_idx's own name in the
// message) on a mismatch. Returns the head of the argument list (chained
// via each argument's own ->next), or NULL if there are none.
// Parses a bare array name as an argument for an array-reference
// parameter (param_index into proc_idx's declared parameter list).
// Validates the array's bounds and element type exactly match the
// parameter's declared ones, then builds a node supplying that array's
// sym_table[] index as the argument's runtime value - either a compile-
// time constant (a global array, or the caller's own var-declared local
// array both have one fixed sym_table[] slot for their whole lifetime)
// or the caller's own array-reference parameter's current value (which
// DOES vary per call, exactly like any other parameter - the caller may
// itself just be forwarding an array it received).
static ASTNode *parse_array_ref_argument(int proc_idx, int param_index) {
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Expected an array name for parameter %d of '%s'",
                       param_index + 1, proc_table[proc_idx].name);
    }
    char arg_name[MAX_NAME];
    int arg_line = token.line;
    strcpy(arg_name, token.text);
    match(TOKEN_IDENTIFIER);

    DataType expected_elem = proc_table[proc_idx].param_types[param_index];
    int expected_lower = proc_table[proc_idx].param_array_lower[param_index];
    int expected_upper = proc_table[proc_idx].param_array_upper[param_index];

    int local_idx = find_local(arg_name);
    if (local_idx != -1 && (current_locals[local_idx].is_array || current_locals[local_idx].is_array_ref)) {
        DataType actual_elem;
        int actual_lower, actual_upper;
        if (current_locals[local_idx].is_array) {
            int s = current_locals[local_idx].array_sym_idx;
            actual_elem = sym_table[s].type;
            actual_lower = sym_table[s].array_lower;
            actual_upper = sym_table[s].array_upper;
        } else {
            actual_elem = current_locals[local_idx].type;
            actual_lower = current_locals[local_idx].array_lower;
            actual_upper = current_locals[local_idx].array_upper;
        }
        if (actual_elem != expected_elem || actual_lower != expected_lower || actual_upper != expected_upper) {
            compile_error(arg_line, "Array argument '%s' does not match parameter %d of '%s' (declared bounds/type)",
                           arg_name, param_index + 1, proc_table[proc_idx].name);
        }
        ASTNode *node;
        if (current_locals[local_idx].is_array) {
            node = create_node(NODE_ARRAY_REF); // compile-time-known sym_table index
            node->data.var_idx = current_locals[local_idx].array_sym_idx;
        } else {
            node = create_node(NODE_LOCAL_VAR); // the caller's own array-ref param's runtime value
            node->data.var_idx = local_idx;
        }
        node->expression_type = expected_elem;
        return node;
    }

    int global_idx = find_var(arg_name);
    if (!sym_table[global_idx].is_array) {
        compile_error(arg_line, "'%s' is not an array", arg_name);
    }
    if (sym_table[global_idx].type != expected_elem ||
        sym_table[global_idx].array_lower != expected_lower ||
        sym_table[global_idx].array_upper != expected_upper) {
        compile_error(arg_line, "Array argument '%s' does not match parameter %d of '%s' (declared bounds/type)",
                       arg_name, param_index + 1, proc_table[proc_idx].name);
    }
    ASTNode *node = create_node(NODE_ARRAY_REF);
    node->data.var_idx = global_idx;
    node->expression_type = expected_elem;
    return node;
}

static ASTNode *parse_call_arguments(int proc_idx) {
    ASTNode *arg_head = NULL;
    ASTNode *arg_tail = NULL;
    int arg_count = 0;
    if (token.type == TOKEN_LPAREN) {
        match(TOKEN_LPAREN);
        if (token.type != TOKEN_RPAREN) {
            while (1) {
                ASTNode *arg = (arg_count < proc_table[proc_idx].param_count && proc_table[proc_idx].param_is_array_ref[arg_count])
                    ? parse_array_ref_argument(proc_idx, arg_count)
                    : expression();
                arg_count++;
                if (!arg_head) arg_head = arg; else arg_tail->next = arg;
                arg_tail = arg;
                if (token.type == TOKEN_COMMA) { match(TOKEN_COMMA); continue; }
                break;
            }
        }
        match(TOKEN_RPAREN);
    }
    if (arg_count != proc_table[proc_idx].param_count) {
        compile_error(token.line, "'%s' expects %d argument(s), got %d",
                       proc_table[proc_idx].name, proc_table[proc_idx].param_count, arg_count);
    }
    return arg_head;
}

// Given an already-resolved GLOBAL symbol index, parses whatever follows
// (array indexing, 2D array indexing, string indexing) and builds the
// appropriate expression node. Shared between plain global-variable
// resolution and record field access - a record field is just a global
// symbol under a mangled name, so once its index is resolved, everything
// past that point (is it an array? a string that can be indexed? a plain
// scalar?) is identical to resolving any other global variable.
static ASTNode *parse_global_symbol_reference(int idx, int line) {
    if (sym_table[idx].is_array) {
        if (token.type != TOKEN_LBRACKET) {
            compile_error(token.line, "Array '%s' must be indexed", sym_table[idx].name);
        }
        match(TOKEN_LBRACKET);
        if (sym_table[idx].is_2d) {
            ASTNode *node = create_node(NODE_ARRAY_ACCESS_2D);
            node->line = line;
            node->data.var_idx = idx;
            node->left = expression(); // first index
            match(TOKEN_COMMA);
            node->right = expression(); // second index
            node->expression_type = sym_table[idx].type;
            match(TOKEN_RBRACKET);
            return node;
        }
        ASTNode *node = create_node(NODE_ARRAY_ACCESS);
        node->line = line;
        node->data.var_idx = idx;
        node->left = expression(); // index
        node->expression_type = sym_table[idx].type;
        match(TOKEN_RBRACKET);
        return node;
    }
    if (token.type == TOKEN_LBRACKET) {
        if (sym_table[idx].type == TYPE_STRING || sym_table[idx].type == TYPE_CHAR) {
            match(TOKEN_LBRACKET);
            ASTNode *node = create_node(NODE_STRING_INDEX);
            node->line = line;
            node->data.var_idx = idx;
            node->left = expression(); // index
            node->expression_type = TYPE_CHAR;
            match(TOKEN_RBRACKET);
            return node;
        }
        compile_error(token.line, "'%s' is not an array, cannot be indexed", sym_table[idx].name);
    }
    ASTNode *node = create_node(NODE_VARIABLE);
    node->line = line;
    node->data.var_idx = idx;
    node->expression_type = sym_table[idx].type;
    return node;
}

static ASTNode *factor(void) {
    if (token.type == TOKEN_MINUS || token.type == TOKEN_NOT) {
        TokenType op = token.type;
        match(op);
        ASTNode *node = create_node(NODE_UNARY_OP);
        node->op = op;
        node->left = factor();
        return node;
    } else if (token.type == TOKEN_ABS || token.type == TOKEN_SQR ||
               token.type == TOKEN_ORD || token.type == TOKEN_CHR ||
               token.type == TOKEN_TRUNC || token.type == TOKEN_ROUND ||
               token.type == TOKEN_SQRT || token.type == TOKEN_SIN ||
               token.type == TOKEN_COS || token.type == TOKEN_ARCTAN ||
               token.type == TOKEN_EXP || token.type == TOKEN_LN) {
        TokenType op = token.type;
        match(op);
        match(TOKEN_LPAREN);
        ASTNode *node = create_node(NODE_UNARY_OP);
        node->op = op;
        node->left = expression();
        match(TOKEN_RPAREN);
        return node;
    } else if (token.type == TOKEN_PI) {
        // A zero-cost literal, exactly like true/false - not a function
        // call, so it needs no runtime opcode and is automatically
        // constant-foldable in any expression that uses it.
        match(TOKEN_PI);
        ASTNode *node = create_node(NODE_REAL_NUMBER);
        node->data.num_value = float_to_bits(3.14159265358979323846f);
        node->expression_type = TYPE_REAL;
        return node;
    } else if (token.type == TOKEN_POWER) {
        match(TOKEN_POWER);
        match(TOKEN_LPAREN);
        ASTNode *node = create_node(NODE_BUILTIN_CALL);
        node->op = TOKEN_POWER;
        node->left = expression();  // base
        match(TOKEN_COMMA);
        node->right = expression(); // exponent
        match(TOKEN_RPAREN);
        return node;
    } else if (token.type == TOKEN_ODD) {
        // odd(x) desugars to '(x mod 2) <> 0' - reuses the existing
        // mod/comparison machinery entirely, so type checking (x must be
        // integer) and codegen need no awareness of 'odd' at all.
        match(TOKEN_ODD);
        match(TOKEN_LPAREN);
        ASTNode *x = expression();
        match(TOKEN_RPAREN);
        ASTNode *two = create_node(NODE_NUMBER);
        two->data.num_value = 2;
        two->expression_type = TYPE_INTEGER;
        ASTNode *rem = create_node(NODE_BINARY_OP);
        rem->op = TOKEN_MOD;
        rem->left = x;
        rem->right = two;
        ASTNode *zero = create_node(NODE_NUMBER);
        zero->data.num_value = 0;
        zero->expression_type = TYPE_INTEGER;
        ASTNode *node = create_node(NODE_BINARY_OP);
        node->op = TOKEN_NEQ;
        node->left = rem;
        node->right = zero;
        return node;
    } else if (token.type == TOKEN_SUCC || token.type == TOKEN_PRED) {
        // succ(x)/pred(x) desugar to 'x + 1'/'x - 1' - same reasoning as odd().
        TokenType kind = token.type;
        match(kind);
        match(TOKEN_LPAREN);
        ASTNode *x = expression();
        match(TOKEN_RPAREN);
        ASTNode *one = create_node(NODE_NUMBER);
        one->data.num_value = 1;
        one->expression_type = TYPE_INTEGER;
        ASTNode *node = create_node(NODE_BINARY_OP);
        node->op = (kind == TOKEN_SUCC) ? TOKEN_PLUS : TOKEN_MINUS;
        node->left = x;
        node->right = one;
        return node;
    } else if (token.type == TOKEN_LENGTH) {
        // length(arr) - a plain array name resolves to a compile-time
        // constant (arr's declared bounds are always known at compile
        // time), same as low()/high() below. length(s) for a string/char
        // expression still needs the existing runtime OP_LENGTH, since a
        // string's actual length varies at runtime.
        match(TOKEN_LENGTH);
        match(TOKEN_LPAREN);
        int lower, upper;
        if (token.type == TOKEN_IDENTIFIER && try_get_array_bounds(token.text, &lower, &upper)) {
            match(TOKEN_IDENTIFIER);
            match(TOKEN_RPAREN);
            ASTNode *node = create_node(NODE_NUMBER);
            node->data.num_value = upper - lower + 1;
            node->expression_type = TYPE_INTEGER;
            return node;
        }
        ASTNode *node = create_node(NODE_BUILTIN_CALL);
        node->op = TOKEN_LENGTH;
        node->left = expression();
        match(TOKEN_RPAREN);
        return node;
    } else if (token.type == TOKEN_LOW || token.type == TOKEN_HIGH) {
        // low(arr)/high(arr) - arr's declared bounds, as a compile-time
        // constant. Only supports arrays (not e.g. an ordinal type name)
        // in this increment.
        TokenType kind = token.type;
        match(kind);
        match(TOKEN_LPAREN);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "'%s' requires a plain array name", kind == TOKEN_LOW ? "low" : "high");
        }
        int lower, upper;
        if (!try_get_array_bounds(token.text, &lower, &upper)) {
            compile_error(token.line, "'%s' requires an array argument", kind == TOKEN_LOW ? "low" : "high");
        }
        match(TOKEN_IDENTIFIER);
        match(TOKEN_RPAREN);
        ASTNode *node = create_node(NODE_NUMBER);
        node->data.num_value = (kind == TOKEN_LOW) ? lower : upper;
        node->expression_type = TYPE_INTEGER;
        return node;
    } else if (token.type == TOKEN_UPCASE || token.type == TOKEN_UPPERCASE || token.type == TOKEN_LOWERCASE) {
        // Unary string builtins: upcase(x), uppercase(x), lowercase(x)
        TokenType op = token.type;
        match(op);
        match(TOKEN_LPAREN);
        ASTNode *node = create_node(NODE_BUILTIN_CALL);
        node->op = op;
        node->left = expression();
        match(TOKEN_RPAREN);
        return node;
    } else if (token.type == TOKEN_POS || token.type == TOKEN_INPOS) {
        // pos(needle, haystack) / inpos(needle, haystack) - inpos is just
        // pos under a different name (the two are semantically identical
        // since char and string share the same representation).
        match(token.type);
        match(TOKEN_LPAREN);
        ASTNode *node = create_node(NODE_BUILTIN_CALL);
        node->op = TOKEN_POS;
        node->left = expression();
        match(TOKEN_COMMA);
        node->right = expression();
        match(TOKEN_RPAREN);
        return node;
    } else if (token.type == TOKEN_COPY || token.type == TOKEN_MID) {
        // copy(s, start, count) / mid(s, start, count) - mid is an alias
        // for copy (same operation, different conventional name).
        match(token.type);
        match(TOKEN_LPAREN);
        ASTNode *node = create_node(NODE_BUILTIN_CALL);
        node->op = TOKEN_COPY;
        node->left = expression();
        match(TOKEN_COMMA);
        node->right = expression();
        match(TOKEN_COMMA);
        node->extra = expression();
        match(TOKEN_RPAREN);
        return node;
    } else if (token.type == TOKEN_LEFT || token.type == TOKEN_RIGHT) {
        // left(s, n) / right(s, n) - NOT desugared to copy(): right(s, n)
        // would need both s and n each twice ('copy(s, length(s)-n+1,
        // n)'), and this AST has no safe way to share a subtree (reusing
        // the same node pointer twice would double-evaluate it, and later
        // double-free it) - so these get their own dedicated opcodes
        // instead, computed by the VM from a single evaluation of each
        // argument.
        TokenType op = token.type;
        match(op);
        match(TOKEN_LPAREN);
        ASTNode *node = create_node(NODE_BUILTIN_CALL);
        node->op = op;
        node->left = expression();
        match(TOKEN_COMMA);
        node->right = expression();
        match(TOKEN_RPAREN);
        return node;
    } else if (token.type == TOKEN_LPAREN) {
        match(TOKEN_LPAREN);
        ASTNode *node = expression();
        match(TOKEN_RPAREN);
        return node;
    } else if (token.type == TOKEN_NUMBER) {
        ASTNode *node = create_node(NODE_NUMBER);
        node->data.num_value = token.value;
        node->expression_type = TYPE_INTEGER;
        match(TOKEN_NUMBER);
        return node;
    } else if (token.type == TOKEN_REAL) {
        ASTNode *node = create_node(NODE_REAL_NUMBER);
        node->data.num_value = float_to_bits(token.real_value);
        node->expression_type = TYPE_REAL;
        match(TOKEN_REAL);
        return node;
    } else if (token.type == TOKEN_TRUE || token.type == TOKEN_FALSE) {
        ASTNode *node = create_node(NODE_BOOLEAN);
        node->data.num_value = token.value;
        node->expression_type = TYPE_BOOLEAN;
        next_token();
        return node;
    } else if (token.type == TOKEN_STRING) {
        ASTNode *node = create_node(NODE_STRING);
        node->data.var_idx = intern_string(token.string_value); // pool index
        node->expression_type = TYPE_STRING;
        match(TOKEN_STRING);
        return node;
    } else if (token.type == TOKEN_CHARCODE) {
        // #NNN - a dedicated char literal, distinct from a quoted string
        // literal: it's TYPE_CHAR from the moment it's parsed (an
        // ordinary 'x' string literal is TYPE_STRING, only usable as a
        // char via the general char/string interop rules). 0 can't be
        // represented - string_pool[] entries are null-terminated C
        // strings, and a "one-character string" containing a NUL byte
        // would actually be an empty string.
        if (token.value < 1 || token.value > 255) {
            compile_error(token.line, "Character code %d out of range (1..255)", token.value);
        }
        char buf[2] = { (char)token.value, '\0' };
        ASTNode *node = create_node(NODE_STRING);
        node->data.var_idx = intern_string(buf);
        node->expression_type = TYPE_CHAR;
        match(TOKEN_CHARCODE);
        return node;
    } else if (token.type == TOKEN_IDENTIFIER) {
        int line = token.line;

        int rv_idx = find_record_var(token.text);
        if (rv_idx != -1) {
            char rec_name[MAX_NAME];
            strcpy(rec_name, token.text);
            match(TOKEN_IDENTIFIER);
            match(TOKEN_PERIOD);
            if (token.type != TOKEN_IDENTIFIER) {
                compile_error(token.line, "Expected a field name after '%s.'", rec_name);
            }
            RecordVarDef *rv = &record_vars[rv_idx];
            int field_idx = find_record_field(rv->record_type_idx, token.text);
            if (field_idx == -1) {
                compile_error(token.line, "'%s' is not a field of '%s'", token.text, rec_name);
            }
            match(TOKEN_IDENTIFIER);
            return parse_global_symbol_reference(rv->field_sym_idx[field_idx], line);
        }

        int local_idx = find_local(token.text);
        if (local_idx != -1) {
            if (current_locals[local_idx].is_array) {
                match(TOKEN_IDENTIFIER);
                int arr_sym_idx = current_locals[local_idx].array_sym_idx;
                if (token.type != TOKEN_LBRACKET) {
                    compile_error(token.line, "Array '%s' must be indexed", current_locals[local_idx].name);
                }
                match(TOKEN_LBRACKET);
                ASTNode *node = create_node(NODE_ARRAY_ACCESS);
                node->line = line;
                node->data.var_idx = arr_sym_idx;
                node->left = expression(); // index
                node->expression_type = sym_table[arr_sym_idx].type;
                match(TOKEN_RBRACKET);
                return node;
            }
            if (current_locals[local_idx].is_array_ref) {
                match(TOKEN_IDENTIFIER);
                if (token.type != TOKEN_LBRACKET) {
                    compile_error(token.line, "Array '%s' must be indexed", current_locals[local_idx].name);
                }
                match(TOKEN_LBRACKET);
                ASTNode *node = create_node(NODE_REF_ARRAY_ACCESS);
                node->line = line;
                node->data.var_idx = local_idx; // the parameter's OWN slot, holding a runtime sym_table index
                node->left = expression(); // index
                node->expression_type = current_locals[local_idx].type;
                match(TOKEN_RBRACKET);
                return node;
            }
            match(TOKEN_IDENTIFIER);
            if ((current_locals[local_idx].type == TYPE_STRING || current_locals[local_idx].type == TYPE_CHAR)
                && token.type == TOKEN_LBRACKET) {
                match(TOKEN_LBRACKET);
                ASTNode *node = create_node(NODE_LOCAL_STRING_INDEX);
                node->line = line;
                node->data.var_idx = local_idx;
                node->left = expression(); // index
                node->expression_type = TYPE_CHAR;
                match(TOKEN_RBRACKET);
                return node;
            }
            ASTNode *node = create_node(NODE_LOCAL_VAR);
            node->line = line;
            node->data.var_idx = local_idx;
            node->expression_type = current_locals[local_idx].type;
            return node;
        }

        int call_proc_idx = find_proc(token.text);
        if (call_proc_idx != -1) {
            if (!proc_table[call_proc_idx].is_function) {
                compile_error(token.line, "'%s' is a procedure and does not return a value; it cannot be used in an expression",
                               proc_table[call_proc_idx].name);
            }
            match(TOKEN_IDENTIFIER);
            ASTNode *node = create_node(NODE_CALL);
            node->line = line;
            node->data.var_idx = call_proc_idx;
            node->expression_type = proc_table[call_proc_idx].return_type;
            node->left = parse_call_arguments(call_proc_idx);
            return node;
        }

        int idx = find_var(token.text);
        match(TOKEN_IDENTIFIER);
        return parse_global_symbol_reference(idx, line);
    }
    compile_error(token.line, "Invalid factor entry");
    return NULL;
}

// '**' binds tighter than */div/mod/and/shl/shr, and is right-
// associative (2 ** 3 ** 2 is 2 ** (3 ** 2) = 512, not (2**3)**2 = 64) -
// achieved by recursing into power_expr() itself for the right operand,
// rather than the level below it. Note this binds *tighter* than unary
// minus, since factor() (which handles unary minus) is called first, so
// '-2 ** 2' parses as '(-2) ** 2' = 4, not '-(2 ** 2)' = -4. Standard
// Pascal has no exponentiation operator at all to match a precedent
// from, so this is simply this compiler's own chosen rule, documented
// here rather than derived from any existing Pascal convention.
static ASTNode *power_expr(void) {
    ASTNode *node = factor();
    if (token.type == TOKEN_POW) {
        match(TOKEN_POW);
        ASTNode *new_node = create_node(NODE_BINARY_OP);
        new_node->op = TOKEN_POW;
        new_node->left = node;
        new_node->right = power_expr();
        node = new_node;
    }
    return node;
}

static ASTNode *term(void) {
    ASTNode *node = power_expr();
    while (token.type == TOKEN_MUL || token.type == TOKEN_DIV || 
           token.type == TOKEN_DIV_KW || token.type == TOKEN_MOD || 
           token.type == TOKEN_AND || token.type == TOKEN_SHL || token.type == TOKEN_SHR) {
        TokenType op = token.type;
        match(op);
        ASTNode *new_node = create_node(NODE_BINARY_OP);
        new_node->op = op;
        new_node->left = node;
        new_node->right = power_expr();
        node = new_node;
    }
    return node;
}

static ASTNode *arithmetic_expression(void) {
    ASTNode *node = term();
    while (token.type == TOKEN_PLUS || token.type == TOKEN_MINUS || 
           token.type == TOKEN_OR || token.type == TOKEN_XOR) {
        TokenType op = token.type;
        match(op);
        ASTNode *new_node = create_node(NODE_BINARY_OP);
        new_node->op = op;
        new_node->left = node;
        new_node->right = term();
        node = new_node;
    }
    return node;
}


static ASTNode *expression(void) {
    ASTNode *node = arithmetic_expression();
    while (token.type == TOKEN_EQ || token.type == TOKEN_LT || token.type == TOKEN_GT
        || token.type == TOKEN_LTE || token.type == TOKEN_GTE || token.type == TOKEN_NEQ ) {
        ASTNode *op_node = create_node(NODE_BINARY_OP);
        op_node->op = token.type;
        op_node->left = node;
        next_token();
        op_node->right = arithmetic_expression();
        node = op_node;
    }
    return node;
}

// Parses a 'type' section: one or more 'TypeName = record ... end;'
// declarations. Only record types exist right now - there's no type
// aliasing ('type TAge = integer;') and no nested records (a field can't
// itself be a record type).
static void parse_type_section(void) {
    match(TOKEN_TYPE);
    while (token.type == TOKEN_IDENTIFIER) {
        if (record_type_count >= MAX_RECORD_TYPES) {
            compile_error(token.line, "Too many record types (limit is %d)", MAX_RECORD_TYPES);
        }
        char type_name[MAX_NAME];
        strcpy(type_name, token.text);
        if (find_record_type(type_name) != -1) {
            compile_error(token.line, "Duplicate type declaration '%s'", type_name);
        }
        match(TOKEN_IDENTIFIER);
        match(TOKEN_EQ);
        match(TOKEN_RECORD);

        RecordTypeDef *rt = &record_types[record_type_count];
        strcpy(rt->name, type_name);
        rt->field_count = 0;

        while (token.type == TOKEN_IDENTIFIER) {
            #define MAX_FIELD_NAMES_PER_LINE 10
            char field_names[MAX_FIELD_NAMES_PER_LINE][MAX_NAME];
            int fcount = 0;
            strcpy(field_names[fcount++], token.text);
            match(TOKEN_IDENTIFIER);
            while (token.type == TOKEN_COMMA) {
                match(TOKEN_COMMA);
                if (fcount >= MAX_FIELD_NAMES_PER_LINE) {
                    compile_error(token.line, "Too many field names on one line (limit is %d)", MAX_FIELD_NAMES_PER_LINE);
                }
                strcpy(field_names[fcount++], token.text);
                match(TOKEN_IDENTIFIER);
            }
            match(TOKEN_COLON);

            int is_array = 0;
            int lower = 0, upper = 0;
            if (token.type == TOKEN_ARRAY) {
                match(TOKEN_ARRAY);
                match(TOKEN_LBRACKET);
                lower = parse_int_literal();
                match(TOKEN_DOTDOT);
                upper = parse_int_literal();
                match(TOKEN_RBRACKET);
                if (upper < lower) {
                    compile_error(token.line, "Invalid array bounds: upper (%d) must be >= lower (%d)", upper, lower);
                }
                match(TOKEN_OF);
                is_array = 1;
            }
            DataType field_type = TYPE_UNKNOWN;
            if (token.type == TOKEN_INTEGER) { field_type = TYPE_INTEGER; match(TOKEN_INTEGER); }
            else if (token.type == TOKEN_BOOLEAN) { field_type = TYPE_BOOLEAN; match(TOKEN_BOOLEAN); }
            else if (token.type == TOKEN_STRING_TYPE) { field_type = TYPE_STRING; match(TOKEN_STRING_TYPE); }
            else if (token.type == TOKEN_CHAR_TYPE) { field_type = TYPE_CHAR; match(TOKEN_CHAR_TYPE); }
            else if (token.type == TOKEN_REAL_TYPE) { field_type = TYPE_REAL; match(TOKEN_REAL_TYPE); }
            else compile_error(token.line, "Unknown field type (records can't contain other records yet)");
            match(TOKEN_SEMI);

            for (int i = 0; i < fcount; i++) {
                if (rt->field_count >= MAX_RECORD_FIELDS) {
                    compile_error(token.line, "Too many fields in record '%s' (limit is %d)", rt->name, MAX_RECORD_FIELDS);
                }
                if (find_record_field(record_type_count, field_names[i]) != -1) {
                    compile_error(token.line, "Duplicate field '%s' in record '%s'", field_names[i], rt->name);
                }
                RecordField *f = &rt->fields[rt->field_count];
                strcpy(f->name, field_names[i]);
                f->type = field_type;
                f->is_array = is_array;
                f->array_lower = lower;
                f->array_upper = upper;
                rt->field_count++;
            }
        }

        match(TOKEN_END);
        match(TOKEN_SEMI);
        record_type_count++;
    }
}

ASTNode *parse_ast(const char *source, const char *filename) {
    current_filename = filename ? filename : "<source>";
    sym_count = 0;
    code_idx = 0;
    string_count = 0;
    array_mem_count = 0;
    loop_depth = 0;
    proc_count = 0;
    current_local_count = 0;
    in_procedure = 0;
    current_function_idx = -1;
    record_type_count = 0;
    record_var_count = 0;
    init_lexer(source);
    match(TOKEN_PROGRAM);
    match(TOKEN_IDENTIFIER);
    match(TOKEN_SEMI);

    if (token.type == TOKEN_TYPE) {
        parse_type_section();
    }

    if (token.type == TOKEN_VAR) {
        match(TOKEN_VAR);
        while (token.type == TOKEN_IDENTIFIER) {
            #define MAX_VAR_NAMES_PER_LINE 20
            char temporary_names[MAX_VAR_NAMES_PER_LINE][MAX_NAME];
            int count = 0;

            strcpy(temporary_names[count++], token.text);
            match(TOKEN_IDENTIFIER);
            
            while (token.type == TOKEN_COMMA) {
                match(TOKEN_COMMA);
                if (count >= MAX_VAR_NAMES_PER_LINE) {
                    compile_error(token.line, "Too many identifiers in one 'var' line (limit is %d)", MAX_VAR_NAMES_PER_LINE);
                }
                strcpy(temporary_names[count++], token.text);
                match(TOKEN_IDENTIFIER);
            }
            match(TOKEN_COLON);

            if (token.type == TOKEN_IDENTIFIER && find_record_type(token.text) != -1) {
                int record_type_idx = find_record_type(token.text);
                match(TOKEN_IDENTIFIER);
                for (int i = 0; i < count; i++) {
                    add_record_var(temporary_names[i], record_type_idx);
                }
            } else if (token.type == TOKEN_ARRAY) {
                match(TOKEN_ARRAY);
                match(TOKEN_LBRACKET);
                int lower = parse_int_literal();
                match(TOKEN_DOTDOT);
                int upper = parse_int_literal();
                int is_2d = 0;
                int lower2 = 0, upper2 = 0;
                if (token.type == TOKEN_COMMA) {
                    match(TOKEN_COMMA);
                    lower2 = parse_int_literal();
                    match(TOKEN_DOTDOT);
                    upper2 = parse_int_literal();
                    if (upper2 < lower2) {
                        compile_error(token.line, "Invalid array bounds: upper (%d) must be >= lower (%d)", upper2, lower2);
                    }
                    is_2d = 1;
                }
                match(TOKEN_RBRACKET);
                if (upper < lower) {
                    compile_error(token.line, "Invalid array bounds: upper (%d) must be >= lower (%d)", upper, lower);
                }
                match(TOKEN_OF);

                DataType elem_type = TYPE_UNKNOWN;
                if (token.type == TOKEN_INTEGER) { elem_type = TYPE_INTEGER; match(TOKEN_INTEGER); }
                else if (token.type == TOKEN_BOOLEAN) { elem_type = TYPE_BOOLEAN; match(TOKEN_BOOLEAN); }
                else if (token.type == TOKEN_STRING_TYPE) { elem_type = TYPE_STRING; match(TOKEN_STRING_TYPE); }
                else if (token.type == TOKEN_CHAR_TYPE) { elem_type = TYPE_CHAR; match(TOKEN_CHAR_TYPE); }
                else if (token.type == TOKEN_REAL_TYPE) { elem_type = TYPE_REAL; match(TOKEN_REAL_TYPE); }
                else compile_error(token.line, "Unknown array element type");

                for (int i = 0; i < count; i++) {
                    if (is_2d) {
                        add_array_var_2d(temporary_names[i], elem_type, lower, upper, lower2, upper2);
                    } else {
                        add_array_var(temporary_names[i], elem_type, lower, upper);
                    }
                }
            } else {
                DataType target_type = TYPE_UNKNOWN;
                if (token.type == TOKEN_INTEGER) { target_type = TYPE_INTEGER; match(TOKEN_INTEGER); }
                else if (token.type == TOKEN_BOOLEAN) { target_type = TYPE_BOOLEAN; match(TOKEN_BOOLEAN); }
                else if (token.type == TOKEN_STRING_TYPE) { target_type = TYPE_STRING; match(TOKEN_STRING_TYPE); }
                else if (token.type == TOKEN_CHAR_TYPE) { target_type = TYPE_CHAR; match(TOKEN_CHAR_TYPE); }
                else if (token.type == TOKEN_REAL_TYPE) { target_type = TYPE_REAL; match(TOKEN_REAL_TYPE); }
                else compile_error(token.line, "Unknown primitive category");

                for (int i = 0; i < count; i++) {
                    add_var(temporary_names[i], target_type);
                }
            }
            match(TOKEN_SEMI);
        }
    }

    while (token.type == TOKEN_PROCEDURE || token.type == TOKEN_FUNCTION) {
        subroutine_declaration(token.type == TOKEN_FUNCTION);
    }

    for (int i = 0; i < proc_count; i++) {
        if (proc_table[i].is_forward) {
            compile_error(token.line, "%s '%s' was forward-declared but never defined",
                           proc_table[i].is_function ? "Function" : "Procedure", proc_table[i].name);
        }
    }

    ASTNode *root = compound_statement();
    match(TOKEN_PERIOD);
    return root;
}

// 'procedure name [(params)] ; forward;'  -- a forward declaration, or
// 'procedure name [(params)] ; [var locals ;] <compound-statement>;'  -- a
// full declaration, or
// 'procedure name ; [var locals ;] <compound-statement>;'  -- completing a
// previous forward declaration (no parameter list here - it was already
// given).
//
// Registers the name (via add_proc) before parsing anything else, so a
// call to this procedure's own name inside its body - recursion -
// resolves correctly. Parameter info is written back to proc_table right
// after the parameter list is parsed, before the body: a recursive call
// site inside the body needs the real param_count/param_types already in
// place, not the placeholders add_proc() set.
// 'procedure name [(params)] ; forward;' or
// 'function name [(params)] : returnType ; forward;'  -- a forward
// declaration, or
// 'procedure name [(params)] ; [var locals ;] <compound-statement>;' or
// 'function name [(params)] : returnType ; [var locals ;] <compound-statement>;'
// -- a full declaration, or
// 'procedure name ; ...' / 'function name ; ...'  -- completing a
// previous forward declaration (no parameter list or return type here -
// both were already given).
//
// Registers the name (via add_proc) before parsing anything else, so a
// call to this procedure's own name inside its body - recursion -
// resolves correctly. Parameter info is written back to proc_table right
// after the parameter list is parsed, before the body: a recursive call
// site inside the body needs the real param_count/param_types already in
// place, not the placeholders add_proc() set.
static void subroutine_declaration(int is_function_decl) {
    match(is_function_decl ? TOKEN_FUNCTION : TOKEN_PROCEDURE);
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Expected a %s name", is_function_decl ? "function" : "procedure");
    }
    char name[MAX_NAME];
    int decl_line = token.line;
    strcpy(name, token.text);
    match(TOKEN_IDENTIFIER);

    int existing_idx = find_proc(name);
    int completing_forward = (existing_idx != -1 && proc_table[existing_idx].is_forward);

    if (completing_forward && proc_table[existing_idx].is_function != is_function_decl) {
        compile_error(decl_line, "'%s' was forward-declared as a %s, but completed as a %s", name,
                       proc_table[existing_idx].is_function ? "function" : "procedure",
                       is_function_decl ? "function" : "procedure");
    }

    int proc_idx = completing_forward ? existing_idx : add_proc(name);

    in_procedure = 1;
    current_local_count = 0;

    if (completing_forward) {
        if (token.type == TOKEN_LPAREN) {
            compile_error(decl_line, "'%s' was already forward-declared with its parameter list - omit parameters here", name);
        }
        // Replay the forward declaration's parameters as locals, so this
        // completing body can reference them by name even though they
        // aren't re-listed here.
        for (int i = 0; i < proc_table[proc_idx].param_count; i++) {
            if (proc_table[proc_idx].param_is_array_ref[i]) {
                add_local_array_ref(proc_table[proc_idx].param_names[i], proc_table[proc_idx].param_types[i],
                                     proc_table[proc_idx].param_array_lower[i], proc_table[proc_idx].param_array_upper[i]);
            } else {
                add_local(proc_table[proc_idx].param_names[i], proc_table[proc_idx].param_types[i]);
            }
        }
        if (is_function_decl && token.type == TOKEN_COLON) {
            compile_error(decl_line, "'%s' was already forward-declared with its return type - omit it here", name);
        }
    } else {
        int param_count = 0;
        DataType param_types[MAX_PARAMS];
        char param_names[MAX_PARAMS][MAX_NAME];
        int param_is_array_ref[MAX_PARAMS];
        int param_array_lower[MAX_PARAMS];
        int param_array_upper[MAX_PARAMS];

        if (token.type == TOKEN_LPAREN) {
            match(TOKEN_LPAREN);
            if (token.type != TOKEN_RPAREN) {
                while (1) {
                    NameGroup g = parse_name_group();
                    for (int i = 0; i < g.count; i++) {
                        if (param_count >= MAX_PARAMS) {
                            compile_error(token.line, "Too many parameters (limit is %d)", MAX_PARAMS);
                        }
                        if (g.is_array) {
                            add_local_array_ref(g.names[i], g.type, g.array_lower, g.array_upper);
                        } else {
                            add_local(g.names[i], g.type);
                        }
                        strcpy(param_names[param_count], g.names[i]);
                        param_types[param_count] = g.type;
                        param_is_array_ref[param_count] = g.is_array;
                        param_array_lower[param_count] = g.array_lower;
                        param_array_upper[param_count] = g.array_upper;
                        param_count++;
                    }
                    if (token.type == TOKEN_SEMI) { match(TOKEN_SEMI); continue; }
                    break;
                }
            }
            match(TOKEN_RPAREN);
        }

        // Write parameter info back now - before a 'forward;' marker or a
        // body is parsed, so a recursive call from inside the body (or a
        // mutually-recursive call from another procedure) sees the real
        // values right away.
        proc_table[proc_idx].param_count = param_count;
        for (int i = 0; i < param_count; i++) {
            proc_table[proc_idx].param_types[i] = param_types[i];
            strcpy(proc_table[proc_idx].param_names[i], param_names[i]);
            proc_table[proc_idx].param_is_array_ref[i] = param_is_array_ref[i];
            proc_table[proc_idx].param_array_lower[i] = param_array_lower[i];
            proc_table[proc_idx].param_array_upper[i] = param_array_upper[i];
        }

        proc_table[proc_idx].is_function = is_function_decl;
        if (is_function_decl) {
            match(TOKEN_COLON);
            proc_table[proc_idx].return_type = parse_scalar_type();
        }
    }

    match(TOKEN_SEMI);

    if (!completing_forward && token.type == TOKEN_FORWARD) {
        match(TOKEN_FORWARD);
        match(TOKEN_SEMI);
        proc_table[proc_idx].is_forward = 1;
        in_procedure = 0;
        current_local_count = 0;
        return; // the real body comes later, in a completing declaration
    }

    if (token.type == TOKEN_VAR) {
        match(TOKEN_VAR);
        while (token.type == TOKEN_IDENTIFIER) {
            NameGroup g = parse_name_group();
            for (int i = 0; i < g.count; i++) {
                if (g.is_array) {
                    add_local_array(g.names[i], g.type, g.array_lower, g.array_upper);
                } else {
                    add_local(g.names[i], g.type);
                }
            }
            match(TOKEN_SEMI);
        }
    }

    // A function gets one more hidden local slot, reserved last (after
    // every real parameter/local), to hold its return value. Assigning to
    // the function's own name inside its body (see statement()) targets
    // this slot via the ordinary NODE_LOCAL_ASSIGN mechanism - no new AST
    // node type needed. Must be reserved (and current_function_idx set)
    // before the body is parsed, so those assignments resolve correctly.
    if (proc_table[proc_idx].is_function) {
        proc_table[proc_idx].return_slot = current_local_count++;
        current_function_idx = proc_idx;
    } else {
        current_function_idx = -1;
    }

    ASTNode *body = compound_statement();
    match(TOKEN_SEMI);

    proc_table[proc_idx].body = body;
    proc_table[proc_idx].local_count = current_local_count; // params + locals (+ return_slot, for a function)
    proc_table[proc_idx].is_forward = 0; // completed now (harmless if it wasn't forward to begin with)

    in_procedure = 0;
    current_local_count = 0;
    current_function_idx = -1;
}

// True for every token that can legally start a statement. Used by
// statement_list() to know when to stop (hitting END/ELSE/UNTIL, or EOF
// on a malformed file, all correctly fail this check).
static int is_statement_start(TokenType t) {
    return t == TOKEN_IDENTIFIER || t == TOKEN_WRITELN || t == TOKEN_WRITE || t == TOKEN_READLN ||
           t == TOKEN_IF || t == TOKEN_WHILE || t == TOKEN_REPEAT || t == TOKEN_FOR || t == TOKEN_BEGIN ||
           t == TOKEN_BREAK || t == TOKEN_CONTINUE || t == TOKEN_INC || t == TOKEN_DEC;
}

// Parses exactly one statement - an assignment, writeln/readln call,
// if/while/repeat, or a nested begin...end block. Never touches a
// separating semicolon or the node's ->next; that's statement_list()'s job.
// Parses 'inc(target)', 'inc(target, delta)', and the 'dec' equivalents.
// Desugars directly to the same AST 'target := target + delta;' (or '-'
// for dec) would produce - reuses every bit of existing assignment type-
// checking and codegen, no new opcodes or node types needed. The target
// must be a plain integer variable (global or local) - not an array
// element, and not boolean/string/char: char is a string-pool index at
// runtime, not a character code, so "incrementing" it wouldn't mean
// "next character" the way it does in real Pascal.
static ASTNode *parse_inc_dec(TokenType kind) {
    const char *name_str = (kind == TOKEN_INC) ? "inc" : "dec";
    match(kind);
    match(TOKEN_LPAREN);
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "'%s' expects a variable", name_str);
    }
    char name[MAX_NAME];
    int line = token.line;
    strcpy(name, token.text);
    match(TOKEN_IDENTIFIER);

    int local_idx = find_local(name);
    int is_local = (local_idx != -1 && !current_locals[local_idx].is_array && !current_locals[local_idx].is_array_ref);
    int global_idx = -1;
    DataType target_type;
    if (local_idx != -1 && !is_local) {
        compile_error(line, "'%s' expects a plain integer variable, not an array", name_str);
    }
    if (is_local) {
        target_type = current_locals[local_idx].type;
    } else {
        global_idx = find_var(name);
        if (sym_table[global_idx].is_array) {
            compile_error(line, "'%s' expects a plain integer variable, not an array", name_str);
        }
        target_type = sym_table[global_idx].type;
    }
    if (target_type != TYPE_INTEGER) {
        compile_error(line, "'%s' only supports integer variables", name_str);
    }

    ASTNode *delta;
    if (token.type == TOKEN_COMMA) {
        match(TOKEN_COMMA);
        delta = expression();
    } else {
        delta = create_node(NODE_NUMBER);
        delta->data.num_value = 1;
        delta->expression_type = TYPE_INTEGER;
    }
    match(TOKEN_RPAREN);

    ASTNode *read_node;
    if (is_local) {
        read_node = create_node(NODE_LOCAL_VAR);
        read_node->data.var_idx = local_idx;
        read_node->expression_type = TYPE_INTEGER;
    } else {
        read_node = create_node(NODE_VARIABLE);
        read_node->data.var_idx = global_idx;
        read_node->expression_type = TYPE_INTEGER;
    }

    ASTNode *value_node = create_node(NODE_BINARY_OP);
    value_node->op = (kind == TOKEN_INC) ? TOKEN_PLUS : TOKEN_MINUS;
    value_node->left = read_node;
    value_node->right = delta;

    ASTNode *write_node;
    if (is_local) {
        write_node = create_node(NODE_LOCAL_ASSIGN);
        write_node->data.var_idx = local_idx;
        write_node->expression_type = TYPE_INTEGER;
    } else {
        write_node = create_node(NODE_ASSIGN);
        write_node->data.var_idx = global_idx;
    }
    write_node->left = value_node;
    return write_node;
}

// Given an already-resolved GLOBAL symbol index, parses whatever follows
// (array indexing, 2D array indexing) plus ':=' and the value expression,
// and builds the appropriate assignment node. Shared between plain
// global-variable assignment and record field assignment - mirrors
// parse_global_symbol_reference on the read side.
static ASTNode *parse_global_assignment(int idx) {
    if (sym_table[idx].is_array) {
        if (token.type != TOKEN_LBRACKET) {
            compile_error(token.line, "Array '%s' must be indexed for assignment", sym_table[idx].name);
        }
        match(TOKEN_LBRACKET);
        if (sym_table[idx].is_2d) {
            ASTNode *stmt = create_node(NODE_ARRAY_ASSIGN_2D);
            stmt->data.var_idx = idx;
            stmt->left = expression();  // first index
            match(TOKEN_COMMA);
            stmt->right = expression(); // second index
            match(TOKEN_RBRACKET);
            match(TOKEN_ASSIGN);
            stmt->extra = expression(); // value
            return stmt;
        }
        ASTNode *stmt = create_node(NODE_ASSIGN);
        stmt->data.var_idx = idx;
        stmt->left = expression();  // index
        match(TOKEN_RBRACKET);
        match(TOKEN_ASSIGN);
        stmt->right = expression(); // value
        return stmt;
    }
    ASTNode *stmt = create_node(NODE_ASSIGN);
    stmt->data.var_idx = idx;
    match(TOKEN_ASSIGN);
    stmt->left = expression();  // value
    return stmt;
}

// 'p2 := p1;' where p2 (already resolved) and p1 must be record variables
// of the same record type. Desugars into N ordinary field assignments
// (p2.f1 := p1.f1; p2.f2 := p1.f2; ...) chained via ->next, wrapped in a
// NODE_COMPOUND - this compiler has no single "copy this whole record"
// opcode, or need for one, since a record isn't one runtime value at all,
// just N ordinary variables under mangled names. The NODE_COMPOUND
// wrapper matters structurally: statement_list() treats whatever
// statement() returns as a single node and manages its ->next itself, so
// returning a multi-node chain directly would have its own internal
// links silently overwritten - wrapping keeps the chain intact via the
// compound's ->left while still presenting a single well-behaved node.
static ASTNode *parse_whole_record_assignment(int dest_rv_idx) {
    RecordVarDef *dest_rv = &record_vars[dest_rv_idx];
    match(TOKEN_ASSIGN);
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Expected a record variable of the same type as '%s'", dest_rv->name);
    }
    int src_rv_idx = find_record_var(token.text);
    if (src_rv_idx == -1) {
        compile_error(token.line, "'%s' is not a record variable", token.text);
    }
    RecordVarDef *src_rv = &record_vars[src_rv_idx];
    if (src_rv->record_type_idx != dest_rv->record_type_idx) {
        compile_error(token.line, "Cannot assign '%s' (type '%s') to '%s' (type '%s') - different record types",
                      src_rv->name, record_types[src_rv->record_type_idx].name,
                      dest_rv->name, record_types[dest_rv->record_type_idx].name);
    }
    match(TOKEN_IDENTIFIER);

    RecordTypeDef *rt = &record_types[dest_rv->record_type_idx];
    for (int i = 0; i < rt->field_count; i++) {
        if (rt->fields[i].is_array) {
            compile_error(token.line, "Cannot assign whole record '%s': field '%s' is an array, and this compiler doesn't support whole-array assignment",
                          rt->name, rt->fields[i].name);
        }
    }

    ASTNode *head = NULL;
    ASTNode *tail = NULL;
    for (int i = 0; i < rt->field_count; i++) {
        ASTNode *assign = create_node(NODE_ASSIGN);
        assign->data.var_idx = dest_rv->field_sym_idx[i];
        ASTNode *value = create_node(NODE_VARIABLE);
        value->data.var_idx = src_rv->field_sym_idx[i];
        value->expression_type = sym_table[src_rv->field_sym_idx[i]].type;
        assign->left = value;
        if (!head) head = assign; else tail->next = assign;
        tail = assign;
    }
    ASTNode *compound = create_node(NODE_COMPOUND);
    compound->left = head; // NULL for a (degenerate) empty record - generate_code(NULL) safely no-ops
    return compound;
}

// Parses one write/writeln argument: an expression, optionally followed
// by ':width' or ':width:precision' (Pascal's field-width syntax).
// Always wraps the result in NODE_WRITE_ARG, even when no ':width' is
// present, so codegen sees a uniform shape for every argument.
static ASTNode *parse_write_arg(void) {
    ASTNode *value = expression();
    ASTNode *arg = create_node(NODE_WRITE_ARG);
    arg->line = value->line;
    arg->left = value;
    if (token.type == TOKEN_COLON) {
        match(TOKEN_COLON);
        arg->right = expression(); // width
        if (token.type == TOKEN_COLON) {
            match(TOKEN_COLON);
            arg->extra = expression(); // precision
        }
    }
    return arg;
}

static ASTNode *statement(void) {
    if (token.type == TOKEN_BEGIN) {
        return compound_statement();
    }

    if (token.type == TOKEN_IDENTIFIER) {
        int rv_idx = find_record_var(token.text);
        if (rv_idx != -1) {
            char rec_name[MAX_NAME];
            strcpy(rec_name, token.text);
            match(TOKEN_IDENTIFIER);
            if (token.type == TOKEN_PERIOD) {
                match(TOKEN_PERIOD);
                if (token.type != TOKEN_IDENTIFIER) {
                    compile_error(token.line, "Expected a field name after '%s.'", rec_name);
                }
                RecordVarDef *rv = &record_vars[rv_idx];
                int field_idx = find_record_field(rv->record_type_idx, token.text);
                if (field_idx == -1) {
                    compile_error(token.line, "'%s' is not a field of '%s'", token.text, rec_name);
                }
                match(TOKEN_IDENTIFIER);
                return parse_global_assignment(rv->field_sym_idx[field_idx]);
            }
            // No '.field' - this is a whole-record assignment: 'p2 := p1;'
            return parse_whole_record_assignment(rv_idx);
        }

        if (current_function_idx != -1 && strcmp(token.text, proc_table[current_function_idx].name) == 0) {
            // Assigning to the function's own name sets its return value.
            ASTNode *stmt = create_node(NODE_LOCAL_ASSIGN);
            stmt->data.var_idx = proc_table[current_function_idx].return_slot;
            stmt->expression_type = proc_table[current_function_idx].return_type;
            match(TOKEN_IDENTIFIER);
            match(TOKEN_ASSIGN);
            stmt->left = expression();
            return stmt;
        }

        int local_idx = find_local(token.text);
        if (local_idx != -1) {
            if (current_locals[local_idx].is_array) {
                int arr_sym_idx = current_locals[local_idx].array_sym_idx;
                match(TOKEN_IDENTIFIER);
                if (token.type != TOKEN_LBRACKET) {
                    compile_error(token.line, "Array '%s' must be indexed for assignment", current_locals[local_idx].name);
                }
                ASTNode *stmt = create_node(NODE_ASSIGN);
                stmt->data.var_idx = arr_sym_idx;
                match(TOKEN_LBRACKET);
                stmt->left = expression();  // index
                match(TOKEN_RBRACKET);
                match(TOKEN_ASSIGN);
                stmt->right = expression(); // value
                return stmt;
            }
            if (current_locals[local_idx].is_array_ref) {
                match(TOKEN_IDENTIFIER);
                if (token.type != TOKEN_LBRACKET) {
                    compile_error(token.line, "Array '%s' must be indexed for assignment", current_locals[local_idx].name);
                }
                ASTNode *stmt = create_node(NODE_REF_ARRAY_ASSIGN);
                stmt->data.var_idx = local_idx; // the parameter's OWN slot, holding a runtime sym_table index
                stmt->expression_type = current_locals[local_idx].type; // element type, for the type checker
                match(TOKEN_LBRACKET);
                stmt->left = expression();  // index
                match(TOKEN_RBRACKET);
                match(TOKEN_ASSIGN);
                stmt->right = expression(); // value
                return stmt;
            }
            ASTNode *stmt = create_node(NODE_LOCAL_ASSIGN);
            stmt->data.var_idx = local_idx;
            stmt->expression_type = current_locals[local_idx].type; // target type, for the type checker
            match(TOKEN_IDENTIFIER);
            match(TOKEN_ASSIGN);
            stmt->left = expression();
            return stmt;
        }

        int proc_idx = find_proc(token.text);
        if (proc_idx != -1) {
            ASTNode *stmt = create_node(NODE_CALL);
            stmt->data.var_idx = proc_idx;
            stmt->op = TOKEN_PROCEDURE; // marks statement context: discard an unused function result
            match(TOKEN_IDENTIFIER);
            stmt->left = parse_call_arguments(proc_idx);
            return stmt;
        }

        int idx = find_var(token.text);
        match(TOKEN_IDENTIFIER);
        return parse_global_assignment(idx);
    }

    if (token.type == TOKEN_INC || token.type == TOKEN_DEC) {
        return parse_inc_dec(token.type);
    }

    if (token.type == TOKEN_WRITELN || token.type == TOKEN_WRITE) {
        TokenType kind = token.type;
        match(kind);
        ASTNode *stmt = create_node(NODE_WRITELN);
        stmt->op = kind; // TOKEN_WRITE (no trailing newline) or TOKEN_WRITELN
        stmt->left = NULL;
        if (token.type == TOKEN_LPAREN) {
            match(TOKEN_LPAREN);
            if (token.type != TOKEN_RPAREN) {
                ASTNode *arg_head = parse_write_arg();
                ASTNode *arg_tail = arg_head;
                while (token.type == TOKEN_COMMA) {
                    match(TOKEN_COMMA);
                    ASTNode *next_arg = parse_write_arg();
                    arg_tail->next = next_arg;
                    arg_tail = next_arg;
                }
                stmt->left = arg_head;
            }
            match(TOKEN_RPAREN);
        }
        return stmt;
    }

    if (token.type == TOKEN_READLN) {
        match(TOKEN_READLN);
        match(TOKEN_LPAREN);
        ASTNode *stmt = create_node(NODE_READLN);
        if (token.type == TOKEN_IDENTIFIER) {
            if (find_local(token.text) != -1) {
                compile_error(token.line, "readln into a parameter or local variable is not yet supported");
            }
            stmt->data.var_idx = find_var(token.text);
            match(TOKEN_IDENTIFIER);
        } else {
            compile_error(token.line, "readln expects a variable identifier");
        }
        match(TOKEN_RPAREN);
        return stmt;
    }

    if (token.type == TOKEN_IF) {
        ASTNode *stmt = create_node(NODE_IF);
        match(TOKEN_IF);
        stmt->left = expression();       // condition
        match(TOKEN_THEN);
        stmt->right = statement();       // then-branch
        if (token.type == TOKEN_ELSE) {
            match(TOKEN_ELSE);
            stmt->extra = statement();   // else-branch (optional)
        }
        return stmt;
    }

    if (token.type == TOKEN_WHILE) {
        ASTNode *stmt = create_node(NODE_WHILE);
        match(TOKEN_WHILE);
        stmt->left = expression();       // condition
        match(TOKEN_DO);
        loop_depth++;
        stmt->right = statement();       // body
        loop_depth--;
        return stmt;
    }

    if (token.type == TOKEN_FOR) {
        ASTNode *stmt = create_node(NODE_FOR);
        match(TOKEN_FOR);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "'for' expects a variable identifier");
        }
        if (find_local(token.text) != -1) {
            compile_error(token.line, "'for' loop variable must be a global variable (parameters/locals not yet supported)");
        }
        stmt->data.var_idx = find_var(token.text);
        match(TOKEN_IDENTIFIER);
        match(TOKEN_ASSIGN);
        stmt->left = expression();       // start bound
        if (token.type == TOKEN_TO) {
            match(TOKEN_TO);
            stmt->op = TOKEN_TO;
        } else if (token.type == TOKEN_DOWNTO) {
            match(TOKEN_DOWNTO);
            stmt->op = TOKEN_DOWNTO;
        } else {
            compile_error(token.line, "'for' expects 'to' or 'downto'");
        }
        stmt->right = expression();      // end bound
        match(TOKEN_DO);
        loop_depth++;
        stmt->extra = statement();       // body
        loop_depth--;
        return stmt;
    }

    if (token.type == TOKEN_REPEAT) {
        ASTNode *stmt = create_node(NODE_REPEAT);
        match(TOKEN_REPEAT);
        loop_depth++;
        stmt->left = statement_list();   // body (chained via ->next, no wrapping compound needed)
        loop_depth--;
        match(TOKEN_UNTIL);
        stmt->right = expression();      // until-condition
        return stmt;
    }

    if (token.type == TOKEN_BREAK) {
        if (loop_depth == 0) {
            compile_error(token.line, "'break' used outside of a loop");
        }
        ASTNode *stmt = create_node(NODE_BREAK);
        match(TOKEN_BREAK);
        return stmt;
    }

    if (token.type == TOKEN_CONTINUE) {
        if (loop_depth == 0) {
            compile_error(token.line, "'continue' used outside of a loop");
        }
        ASTNode *stmt = create_node(NODE_CONTINUE);
        match(TOKEN_CONTINUE);
        return stmt;
    }

    compile_error(token.line, "Unexpected token '%s' at start of statement", token.text[0] ? token.text : "EOF");
    return NULL;
}

// Parses statements separated by ';' until a non-statement token is hit
// (END, ELSE, UNTIL, or EOF). A trailing ';' before that terminator is
// optional, matching normal Pascal statement-list syntax.
static ASTNode *statement_list(void) {
    ASTNode *head = NULL;
    ASTNode *tail = NULL;
    while (is_statement_start(token.type)) {
        ASTNode *stmt = statement();
        if (!head) head = stmt;
        else tail->next = stmt;
        tail = stmt;

        if (token.type == TOKEN_SEMI) {
            match(TOKEN_SEMI);
        } else {
            break;
        }
    }
    return head;
}

static ASTNode *compound_statement(void) {
    match(TOKEN_BEGIN);
    ASTNode *root = create_node(NODE_COMPOUND);
    root->left = statement_list();
    match(TOKEN_END);
    return root;
}

void free_ast(ASTNode *node) {
    if (!node) return;
    free_ast(node->left);
    free_ast(node->right);
    free_ast(node->next);
    free_ast(node->extra);
    free(node);
}

