#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "parser.h"
#include "lexer.h"
#include "error.h"
#include "type_checker.h"
#include "optimizer.h"

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

// Tracks how many 'try...finally...end' cleanup bodies we're currently
// parsing inside of (mirrors loop_depth's own shape/purpose). Used to
// reject a labeled statement anywhere inside a finally-body, right where
// it's written - codegen.c compiles a finally-body's AST subtree TWICE
// (once for normal completion, once for exception-unwind - see NODE_TRY's
// codegen case), and label_table[]'s single-scalar code_idx can't
// tolerate the same label being generated twice; the second occurrence
// would be misread as a backward goto into the FIRST copy, corrupting
// the try-vs-normal-path invariant. See the NODE_LABEL creation site.
static int finally_body_depth = 0;

// Labels declared in the CURRENT block's 'label' section (the main
// program, or whichever procedure/function is being parsed right now) -
// reset at the start of each block (parse_ast() for the main program,
// subroutine_declaration() for a procedure/function), since each block
// has its own independent label namespace: a goto can only target a
// label declared in that same block, never across a procedure boundary.
// 'defined' tracks whether the label has already been used as a
// 'N: statement' prefix somewhere in this block - needed to catch both a
// duplicate definition (two statements claiming the same label) and, at
// the end of the block, a label that was declared but never used to
// label any statement (a standard Pascal requirement, not just a stray
// warning - see check_all_labels_defined()). Because procedure bodies
// are parsed (via subroutine_declaration(), which resets this same
// table) in between the main program's own declarations and its body,
// parse_ast() stashes and restores its own declared_labels/count around
// that procedure-parsing loop - see the comment there.
#define MAX_DECLARED_LABELS 64
typedef struct {
    int id;
    int decl_line;
    int defined;
} DeclaredLabel;
static DeclaredLabel declared_labels[MAX_DECLARED_LABELS];
static int declared_label_count = 0;

static int find_declared_label(int id) {
    for (int i = 0; i < declared_label_count; i++) {
        if (declared_labels[i].id == id) return i;
    }
    return -1;
}

// The parameters and local variables of whichever procedure is currently
// being parsed - a fresh, tiny scope, reset at the start of each
// procedure_declaration() and cleared again once it's done. Empty (and
// nesting_depth == -1) while parsing the main program body, so every
// identifier there resolves against sym_table exactly as before
// procedures existed.
//
// Nested procedures (a procedure/function declared inside another one's
// declaration section) turn this into a STACK of scopes, one per active
// nesting level, rather than one flat table - see nesting_depth/
// scope_locals[]/current_locals below. MAX_LOCALS was bumped from 32 to
// 64 for this: a nesting chain's cumulative locals (every enclosing
// scope's own locals, all simultaneously in scope from the innermost
// procedure's point of view) now share this per-level budget, where
// before it only ever bounded one procedure's own locals at a time.
#define MAX_LOCALS 64
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
                        // (dimension 1, if is_2d below)
    int is_2d;           // only meaningful if is_array_ref: 1 if this
                        // by-reference array parameter is 2D
    int array_lower2;    // only meaningful if is_2d: the second
    int array_upper2;    // dimension's bounds
    int is_nd;            // only meaningful if is_array_ref: 1 if this
                        // by-reference array parameter has 3 OR MORE
                        // dimensions - mutually exclusive with is_2d.
                        // See Symbol.is_nd in common.h for why every
                        // dimension's bounds live uniformly in
                        // nd_lower/nd_upper below instead of reusing
                        // array_lower/array_upper for dimension 1.
    int nd_dims;          // only meaningful if is_nd
    int nd_lower[MAX_ARRAY_DIMS]; // only meaningful if is_nd, indices 0..nd_dims-1
    int nd_upper[MAX_ARRAY_DIMS];
    int is_subrange;     // this SCALAR local/parameter (or, if
                        // is_array_ref, each element the referenced
                        // array holds) is subrange-constrained - see
                        // the Symbol comment in common.h. Not consulted
                        // for a plain is_array local (that case's
                        // constraint lives on the mangled global's own
                        // Symbol - see add_local_array()).
    int subrange_lower;  // only meaningful if is_subrange
    int subrange_upper;
    int is_static;        // 'static count: integer;' - a SCALAR local
                        // that persists across calls, unlike an
                        // ordinary local (which ENTER zero-initializes
                        // fresh every call). Reuses the exact same
                        // "hidden mangled global" trick add_local_array()
                        // already uses for arrays (which are already
                        // implicitly persistent) - see add_static_local().
                        // Mutually exclusive with is_array/is_array_ref
                        // (only a plain scalar local can be static).
    int static_sym_idx;   // only meaningful if is_static: the mangled
                        // global's sym_table[] index - every reference
                        // resolves here instead of a frame slot.
    int is_var_param;     // 'var name: type' - a general by-reference
                        // SCALAR parameter (see param_is_var in
                        // common.h's ProcSymbol for the full design).
                        // This slot holds an ENCODED REFERENCE, not the
                        // value itself - every read/write of it must go
                        // through NODE_VAR_PARAM_READ/ASSIGN (never the
                        // plain NODE_LOCAL_VAR/ASSIGN every other scalar
                        // local uses). Mutually exclusive with
                        // is_array/is_array_ref/is_static (only a plain
                        // scalar parameter can be a 'var' parameter).
                        // ALSO set (alongside is_const_param/is_out_param
                        // below) for a 'const'/'out' parameter - both
                        // reuse this exact by-reference mechanism.
    int is_const_param;    // 'const name: type' - see param_is_const in
                        // common.h's ProcSymbol. Only meaningful when
                        // is_var_param is also set. Blocks every write-
                        // guard site from assigning to this parameter
                        // directly (shallow - writing THROUGH it, e.g.
                        // a pointer/class parameter's own field, is
                        // still legal). Mutually exclusive with
                        // is_out_param.
    int is_out_param;      // 'out name: type' - see param_is_out in
                        // common.h's ProcSymbol. Only meaningful when
                        // is_var_param is also set. No behavioral
                        // difference from a plain 'var' parameter at
                        // this level - only consulted by the
                        // uninitialized-variable warning pass. Mutually
                        // exclusive with is_const_param.
    int is_proc_param;    // an inline procedural/functional parameter
                        // (see param_is_proc in common.h's ProcSymbol) -
                        // this slot holds a runtime procedure entry
                        // address, not an ordinary scalar value. Mutually
                        // exclusive with is_array/is_array_ref/is_static/
                        // is_var_param. Every read of it must go through
                        // NODE_CALL_INDIRECT (a call) or be forwarded
                        // whole, via a plain NODE_LOCAL_VAR read, as
                        // another procedural argument (see
                        // parse_proc_argument()) - never any other
                        // expression context.
    int proc_param_is_function;   // only meaningful if is_proc_param:
                        // 1 if this inline signature is 'function', 0
                        // if 'procedure'.
    DataType proc_param_return_type; // only meaningful if
                        // proc_param_is_function.
    int proc_param_param_count;   // only meaningful if is_proc_param:
                        // this signature's OWN parameter count (a
                        // completely separate parameter list from the
                        // enclosing procedure's).
    DataType proc_param_param_types[MAX_PARAMS];
    int proc_param_param_is_var[MAX_PARAMS];
} LocalSymbol;

// How many procedure/function declarations deep we're currently parsing
// INTO (a nested procedure's own declaration section, which may itself
// declare further nested procedures). -1 = not currently inside any
// procedure at all (parsing the main program's own declarations/body).
// 0 = directly inside a top-level procedure/function. 1 = inside a
// procedure/function declared directly inside THAT one, etc. This one
// counter is the single source of truth for "how many local scopes are
// currently live" - it replaces what used to be a separate in_procedure
// boolean, since nesting_depth >= 0 means exactly the same thing
// in_procedure == 1 used to.
#define MAX_NESTING_DEPTH 16
static int nesting_depth = -1;

// scope_locals[d]/scope_local_count[d] hold nesting level d's own
// parameters/locals - scope_locals[nesting_depth] is always "whichever
// procedure is innermost right now", matching what current_locals used
// to be back when only one level could ever be active. current_locals/
// current_local_count are kept as macros aliasing the active level, so
// every existing call site that declares into them (add_local() and
// friends) needs no changes at all: it already only ever touches "the
// currently active scope", which is exactly what these macros now mean.
// A name declared in an OUTER scope is reached differently - see
// find_local_outward()/local_at() below - current_locals[] itself only
// ever represents the innermost level.
static LocalSymbol scope_locals[MAX_NESTING_DEPTH][MAX_LOCALS];
static int scope_local_count[MAX_NESTING_DEPTH];
#define current_locals       (scope_locals[nesting_depth])
#define current_local_count  (scope_local_count[nesting_depth])

// Reaches the LocalSymbol a find_local_outward() (or otherwise known
// levels_up) match actually lives in - current_locals[idx] alone is only
// ever correct for levels_up == 0 (the innermost scope); every other
// level needs this instead. Declared here (rather than next to
// find_local_outward() itself, much later in this file) so the record-
// variable machinery just below - which needs it too - can see it.
static LocalSymbol *local_at(int idx, int levels_up) {
    return &scope_locals[nesting_depth - levels_up][idx];
}

// Constants are the simplest possible compile-time-only feature: a
// 'const Name = expr;' declaration never gets a Symbol/sym_table[] entry
// or any runtime storage at all - the expression is fully resolved once,
// right here during parsing (reusing the SAME type_check()/optimize_ast()
// machinery the rest of the pipeline runs later, just invoked immediately
// on this one small subtree), and the resulting literal value is stashed
// in this parser-only table. From that point on, factor() resolves any
// reference to a const name directly into a fresh literal AST node - the
// same trick 'pi' already uses below - exactly as if the user had typed
// the literal value inline. This is also why a const expression can only
// reference other, earlier consts, never a variable or a function call:
// nothing else has been declared yet at the point 'const' is parsed (it
// comes before 'type'/'var'/procedures in program structure), so any
// other identifier simply doesn't exist yet as far as the parser is
// concerned - no separate check is needed to enforce this.
#define MAX_CONSTS 50
typedef struct {
    char name[MAX_NAME];
    DataType type;
    int value; // raw int (TYPE_INTEGER/TYPE_BOOLEAN), a float bit pattern
               // (TYPE_REAL), or a string_pool[] index (TYPE_STRING/TYPE_CHAR)
} ConstDef;
static ConstDef const_defs[MAX_CONSTS];
static int const_def_count = 0;

static int find_const(const char *name) {
    for (int i = 0; i < const_def_count; i++) {
        if (strcmp(const_defs[i].name, name) == 0) return i;
    }
    return -1;
}

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
// MAX_RECORD_FIELDS now lives in common.h - vm.c's heap allocator (see
// vm_heap_freelist[]) needs the same bound, since a pointer's target, if
// a record, needs one allocation "size class" per possible field count.
#define MAX_RECORD_VARS 50

typedef struct {
    char name[MAX_NAME];
    DataType type;         // meaningless if is_record
    int is_array;
    int array_lower, array_upper; // only meaningful if is_array
    int is_subrange;      // see the Symbol comment in common.h - propagated
    int subrange_lower;   // to the field's mangled global Symbol by
    int subrange_upper;   // add_record_var()
    TokenType disk_width; // TOKEN_BYTE/TOKEN_SHORTINT/TOKEN_WORD if this
                           // field's type was literally that keyword, else
                           // 0 (ordinary 4-byte width) - including for an
                           // ordinary hand-written subrange whose bounds
                           // happen to match one of those three ranges
                           // (see scalar_type_disk_width's own comment for
                           // why this is NOT inferred from is_subrange/
                           // bounds). Only meaningful for a typed-file
                           // leaf's on-disk width - see NODE_TYPED_FILE_
                           // WRITE_LEAF/READ_LEAF and record_type_byte_size().
    int is_record;        // field's type is itself a record type (nested
                           // records) - mutually exclusive with is_array.
                           // The nested type is guaranteed to have no
                           // array-typed field anywhere in it (enforced
                           // where a nested field is declared - see
                           // parse_type_section()), so a nested field's
                           // own leaves are always scalar.
    int record_type_idx;  // record_types[] index; only meaningful if is_record
    int is_private;       // only meaningful for a CLASS field (always 0 for
                           // a plain record field - records have no
                           // visibility concept) - 1 if declared in a
                           // 'private' section; see resolve_heap_deref_step()'s
                           // enforcement. Survives an inheritance copy
                           // unchanged (a plain struct-copy field, same as
                           // declaring_class_ptr_idx below), since a
                           // descendant class doesn't re-declare an
                           // inherited field.
    int is_protected;     // same convention as is_private, mutually
                           // exclusive with it - 1 if declared in a
                           // 'protected' section. See
                           // class_ptr_idx_is_or_descends_from()'s
                           // enforcement in resolve_heap_deref_step().
    int declaring_class_ptr_idx; // only meaningful for a CLASS field (-1
                           // for a plain record field) - the pointer_types[]
                           // index of whichever class's OWN 'class ... end;'
                           // first declared this field, needed because
                           // inheritance flattens a copy of every ancestor
                           // field into a descendant's own rt->fields[] -
                           // without this, a descendant's own copy of an
                           // inherited PRIVATE field couldn't be told apart
                           // from one it declared itself.
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

// A record LOCAL or PARAMETER (as opposed to RecordVarDef above, which
// is always a GLOBAL). Unlike a global record's fields (hidden globals,
// shared/not per-call-isolated - same as an ordinary global), each
// field here is an ordinary FRAME SLOT (a current_locals[] index, added
// via add_local() exactly like N separate scalar locals/parameters
// would be) - giving a local/parameter record proper per-call
// isolation, matching every OTHER local/parameter in this language. A
// record's fields don't need contiguous memory the way an array's
// elements do, so there's no reason for a local record to inherit
// local ARRAYS' "shared across every call" limitation - see
// add_local_record() below. Reset (like current_locals[]) at the start
// of each procedure's parsing; a local record shadows a global of the
// same name, checked first everywhere - see find_any_record_var().
#define MAX_LOCAL_RECORD_VARS 20
typedef struct {
    char name[MAX_NAME];
    int record_type_idx;
    int field_local_idx[MAX_RECORD_FIELDS]; // current_locals[] index of
                                // each field (in the SAME nesting level
                                // as this LocalRecordVarDef itself, per
                                // scope_record_vars[] below), in declared
                                // order
} LocalRecordVarDef;

// Same per-nesting-level stack treatment as scope_locals[]/current_locals
// above, and for the same reason - local_record_vars/local_record_var_count
// are kept as macros aliasing the innermost active level, so every
// existing call site needs no changes.
static LocalRecordVarDef scope_record_vars[MAX_NESTING_DEPTH][MAX_LOCAL_RECORD_VARS];
static int scope_record_var_count[MAX_NESTING_DEPTH];
#define local_record_vars       (scope_record_vars[nesting_depth])
#define local_record_var_count  (scope_record_var_count[nesting_depth])

// Maps an array-of-records Symbol (by its OWN sym_table[] index - whether
// a true global or a local's hidden mangled global, see
// add_local_array_rec()) to which record_types[] entry it's an array OF.
// A separate side table, rather than a field on Symbol itself, because
// record_type_idx is only ever meaningful as an index into record_types[]
// - a table that (like every other record-related table in this file)
// never leaves parser.c, unlike is_record_array/record_elem_field_count
// on Symbol, which ARE meaningful (a stride, a boolean) without it.
#define MAX_RECORD_ARRAYS 30
typedef struct {
    int sym_idx;
    int record_type_idx;
} RecordArrayDef;
static RecordArrayDef record_arrays[MAX_RECORD_ARRAYS];
static int record_array_count = 0;

// register_record_array() is defined further down (right before its
// first use, add_array_var_rec()) - it needs compile_error(), declared
// later in this file.

// -1 if sym_idx doesn't name an array-of-records (shouldn't happen at any
// call site that already checked sym_table[sym_idx].is_record_array).
static int find_record_array_type(int sym_idx) {
    for (int i = 0; i < record_array_count; i++) {
        if (record_arrays[i].sym_idx == sym_idx) return record_arrays[i].record_type_idx;
    }
    return -1;
}

// Maps a typed-file (TYPE_TYPED_FILE) Symbol - by its own sym_table[]
// index, always global (see TYPE_TYPED_FILE's own comment in common.h) -
// to which record_types[] entry (or bare scalar DataType) it holds, and
// the record's total leaf count (how many raw ints make up one record -
// record_type_leaf_count(record_type_idx), or 1 for a bare scalar). A
// separate side table, mirroring record_arrays[] just above exactly,
// rather than fields on Symbol itself, for the identical reason: this
// state is only ever meaningful to parser.c (codegen bakes leaf_count
// into OP_TYPED_FILE_RESET/REWRITE's own arg at compile time - see
// codegen.c - so nothing downstream needs this table at all).
#define MAX_TYPED_FILE_VARS 20
typedef struct {
    int sym_idx;
    int is_record;
    int record_type_idx;  // only meaningful if is_record
    DataType scalar_type;  // only meaningful if !is_record
    int leaf_count;
    TokenType disk_width;  // only meaningful if !is_record - see
                            // RecordField.disk_width's own comment; the
                            // record case's per-FIELD widths live on
                            // record_types[record_type_idx].fields[]
                            // instead, since a record can mix widths
                            // across its own fields.
    int byte_size;          // actual on-disk bytes per record - either
                            // record_type_byte_size(record_type_idx) or
                            // this scalar's own disk_width-derived size
                            // (1/2/4) for the bare-scalar case. What
                            // actually gets baked into reset/rewrite's
                            // packed arg now (see codegen.c) - NOT
                            // leaf_count * sizeof(int), which stops
                            // being correct the moment any field/element
                            // has a narrower-than-4-byte disk_width.
} TypedFileVarDef;
static TypedFileVarDef typed_file_vars[MAX_TYPED_FILE_VARS];
static int typed_file_var_count = 0;

// -1 if sym_idx doesn't name a typed-file variable.
static int find_typed_file_var(int sym_idx) {
    for (int i = 0; i < typed_file_var_count; i++) {
        if (typed_file_vars[i].sym_idx == sym_idx) return i;
    }
    return -1;
}

// Whether t is a type whose raw int IS its actual portable value -
// integer (also covers a subrange field: subrange-ness is a separate
// is_subrange flag on RecordField, not a distinct DataType, so plain
// TYPE_INTEGER already covers it), real, boolean, a specific enumerated
// type, or set - as opposed to a string_pool[] index (string/char) or a
// process-local address (pointer/procedural), neither of which means
// anything once written to a file and read back in a different run.
static int is_typed_file_safe_scalar(DataType t) {
    return t == TYPE_INTEGER || t == TYPE_REAL || t == TYPE_BOOLEAN || t == TYPE_SET
        || (t >= TYPE_ENUM_BASE && t < TYPE_ENUM_BASE + MAX_ENUM_TYPES);
}

// Whether every leaf of record_type_idx (recursively) is safe per
// is_typed_file_safe_scalar() above, AND the record has no array-typed
// field anywhere - matching the EXISTING independent restriction
// parse_whole_record_assignment()/build_record_compare() each already
// enforce for their own is_array checks (this compiler's records have
// no memory layout of their own to serialize an array field's variable
// element count into, the same underlying reason those two features cut
// array fields).
static int record_type_is_typed_file_safe(int record_type_idx) {
    RecordTypeDef *rt = &record_types[record_type_idx];
    for (int i = 0; i < rt->field_count; i++) {
        RecordField *f = &rt->fields[i];
        if (f->is_array) return 0;
        if (f->is_record) {
            if (!record_type_is_typed_file_safe(f->record_type_idx)) return 0;
        } else if (!is_typed_file_safe_scalar(f->type)) {
            return 0;
        }
    }
    return 1;
}

// A specific pointer type is encoded as TYPE_POINTER_BASE + its
// pointer_types[] index (see the comment in common.h) - a BOUNDED range
// check, exactly like type_checker.c's own copy of this same helper
// (duplicated per this project's established "small helpers live in
// each file that needs them" convention rather than sharing one via a
// header - see e.g. bits_to_float in vm.c/optimizer.c).
static int is_pointer_type(DataType t) {
    return t >= TYPE_POINTER_BASE && t < TYPE_POINTER_BASE + MAX_POINTER_TYPES;
}

// Same bounded-range idea, for a specific NAMED procedural type ('type
// TProc = procedure(...);', encoded as TYPE_PROC_BASE + its
// proc_types[] index - see the comment in common.h).
static int is_proc_type(DataType t) {
    return t >= TYPE_PROC_BASE && t < TYPE_PROC_BASE + MAX_PROC_TYPES;
}

// Forward-declared (defined much later in this file) purely so
// find_or_add_dynarray_type() below can report "too many distinct
// element types" the same way every other table-overflow error in this
// file does - mirrors the same forward-declaration this file's typed-
// constants support needed (and later removed) for the identical reason.
static void compile_error(int line, const char *fmt, ...);

// Same bounded-range idea, for a specific dynamic-array shape ('array of
// ElementType', encoded as TYPE_DYNARRAY_BASE + its dynarray_types[]
// index - see the comment in common.h).
static int is_dynarray_type(DataType t) {
    return t >= TYPE_DYNARRAY_BASE && t < TYPE_DYNARRAY_BASE + MAX_DYNARRAY_TYPES;
}

// One declared dynamic-array SHAPE - just its element type. Unlike
// pointer_types[]/proc_types[]/etc., 'array of T' has no name of its own
// (it's always written out inline at each declaration site, never given a
// 'type TFoo = array of T;' alias - see docs/LANGUAGE.md#dynamic-arrays
// for why that's out of scope for now), so there's no name-equivalence
// rule to encode here the way pointer types have. Instead this table is
// deduped STRUCTURALLY by find_or_add_dynarray_type() below: every
// 'array of integer' anywhere in a program resolves to the SAME
// dynarray_types[] index, so two independently-declared dynamic arrays of
// the same element type share one DataType value and are freely
// assignment-compatible via the ordinary exact-type-match rule every
// other DataType already uses - no dynamic-array-specific case needed in
// type_checker.c's NODE_ASSIGN handling at all.
// elem_is_subrange/elem_subrange_lower/elem_subrange_upper mirror
// Symbol.is_subrange for a STATIC array's element type (byte/shortint/
// word - see parse_scalar_type()'s own scalar_type_is_subrange output) -
// needed so an element WRITE can still be wrapped in the same
// wrap_range_check() every other subrange-typed target already gets. Not
// used for a plain (non-subrange) element type.
typedef struct {
    DataType elem_type;
    int elem_is_subrange;
    int elem_subrange_lower;
    int elem_subrange_upper;
} DynArrayTypeDef;
static DynArrayTypeDef dynarray_types[MAX_DYNARRAY_TYPES];
static int dynarray_type_count = 0;

static int find_or_add_dynarray_type(DataType elem_type, int is_subrange, int lower, int upper) {
    for (int i = 0; i < dynarray_type_count; i++) {
        DynArrayTypeDef *d = &dynarray_types[i];
        if (d->elem_type == elem_type && d->elem_is_subrange == is_subrange
            && (!is_subrange || (d->elem_subrange_lower == lower && d->elem_subrange_upper == upper))) {
            return i;
        }
    }
    if (dynarray_type_count >= MAX_DYNARRAY_TYPES) {
        compile_error(token.line, "Too many distinct dynamic-array element types (limit is %d)", MAX_DYNARRAY_TYPES);
    }
    DynArrayTypeDef *d = &dynarray_types[dynarray_type_count];
    d->elem_type = elem_type;
    d->elem_is_subrange = is_subrange;
    d->elem_subrange_lower = lower;
    d->elem_subrange_upper = upper;
    return dynarray_type_count++;
}

// Parses the 'of ElementType' tail of a dynamic-array declaration,
// assuming 'array' has ALREADY been matched and the current token is
// (or should be) 'of' - shared by parse_scalar_type() (which matches
// 'array' itself, for a parameter/local/nested type reference) and the
// two call sites that special-case 'array' BEFORE parse_scalar_type() is
// ever reached (parse_var_section()/the param-local shared group-parser -
// each must peek for '[' first to tell a STATIC array declaration apart
// from this one, so neither can just delegate to parse_scalar_type()
// directly without first consuming 'array' themselves). Defined after
// parse_scalar_type() itself (this is just a forward declaration) since
// its body needs to call that function, which in turn calls this one -
// genuinely mutually recursive, unlike most of this file's top-to-bottom
// definition order.
static DataType parse_dynarray_of(void);

// One declared pointer type ('type PFoo = ^Target;'). target_type is any
// DataType parse_scalar_type() can already resolve (a scalar, an alias,
// an enum, a subrange) - OR the pointer targets a RECORD type instead,
// which parse_scalar_type() has no notion of at all, so that case is
// tracked separately (target_is_record/target_record_type_idx) rather
// than trying to force a record type into a DataType slot.
//
// A record target specifically may be a FORWARD reference - 'PNode =
// ^TNode;' declared before 'TNode' itself, the classic self-referential
// linked-list/tree pattern standard Pascal requires - so is_pending
// stays 1 (with pending_target_name/pending_line recording what to
// resolve and where to blame an error) until resolve_pending_pointer_
// types() runs at the end of the ENCLOSING 'type' section (see
// parse_type_section()) - by which point every type name declared in
// that section, in any order, is known. A pointer targeting a scalar/
// alias/enum/subrange is never deferred this way: parse_scalar_type()
// already requires those to be declared before use, exactly like every
// other reference to one, so there's nothing to defer.
// One inline procedural/functional parameter header - standard ISO 7185
// Pascal's functional/procedural parameters ('function f(n: integer):
// integer' or 'procedure f(...)' written out as ONE formal parameter,
// unlike a NameGroup which can list several names sharing one type).
// Declared up here (rather than next to parse_proc_param_header(),
// which actually parses one) so a class's method headers - see
// PointerTypeDef.methods below - can reuse this exact shape too: a
// class method header is parsed by the very same
// parse_proc_param_header(), since "one procedure/function signature,
// no body, scalar params only" is exactly what both need.
typedef struct {
    char name[MAX_NAME];
    int is_function;
    DataType return_type;      // only meaningful if is_function
    int param_count;           // this header's OWN parameter count - a
                               // completely separate parameter list from
                               // the enclosing procedure's
    DataType param_types[MAX_PARAMS];
    int param_is_var[MAX_PARAMS];
    int param_is_const[MAX_PARAMS]; // 'const' - only meaningful when
                               // param_is_var is also set (both share
                               // the same by-reference mechanism);
                               // mutually exclusive with param_is_out.
                               // Only ever set when this header is a
                               // class method's (see the allow_const_out
                               // parameter on parse_proc_signature_tail()/
                               // parse_proc_param_header()) - rejected
                               // as a compile error everywhere else this
                               // struct is used (a procedural/functional
                               // parameter's own inline signature, a
                               // named procedural type).
    int param_is_out[MAX_PARAMS];  // 'out' - same scope restriction as
                               // param_is_const above.
    int param_has_default[MAX_PARAMS]; // 1 if this parameter has a
                               // default value ('= <const-expr>'), only
                               // ever set on a TRAILING run of
                               // parameters, never alongside
                               // param_is_var/param_is_const/
                               // param_is_out (no caller-side lvalue to
                               // splice a default's address into for a
                               // by-reference parameter), and never on
                               // an array/record or subrange-typed
                               // parameter. Same allow_const_out-gated
                               // scope restriction as param_is_const/
                               // param_is_out above.
    DataType param_default_type[MAX_PARAMS]; // the default's own
                               // resolved literal type (e.g. TYPE_INTEGER
                               // for '= 5' even on a 'real' parameter -
                               // int-to-real widening happens for free
                               // at each call site's ordinary
                               // try_widen_for_assignment check, exactly
                               // like a real caller-supplied int
                               // argument would).
    int param_default_value[MAX_PARAMS]; // only meaningful when
                               // param_has_default is set; same encoding
                               // as ConstDef.value - a raw int
                               // (INTEGER/BOOLEAN), a float bit pattern
                               // (REAL, via float_to_bits), or a
                               // string_pool[] index (STRING/CHAR).
    char param_names[MAX_PARAMS][MAX_NAME]; // unused by a functional/
                               // procedural parameter itself (only the
                               // structural signature - count/types/
                               // var-ness - matters for call-site
                               // matching there), but populated
                               // regardless: a class method header (see
                               // PointerTypeDef.methods) reuses this
                               // exact struct, and ITS parameter names
                               // matter - the method's later BODY
                               // declaration needs them to register as
                               // named locals (see
                               // parse_class_method_body()).
    char mangled_name[MAX_NAME]; // unused by a functional/procedural
                               // parameter (also unused for the DECLARING
                               // class - always 'DeclaringClass__Name');
                               // for a class method, this is the mangled
                               // proc_table[] name that ACTUALLY
                               // implements it - which, for an INHERITED,
                               // not-overridden method, is the ANCESTOR
                               // class's own mangled name, not the
                               // accessing class's (see
                               // parse_class_declaration()'s inheritance
                               // comment). Computed once, at whichever
                               // class's own 'class ... end;' first
                               // declares or overrides this method, and
                               // copied unchanged into every descendant
                               // that inherits it without overriding.
    int is_inherited;          // unused by a functional/procedural
                               // parameter; for a class method, 1 if
                               // this entry was copied from a parent
                               // class and NOT subsequently overridden
                               // by the class it currently lives on -
                               // parse_class_method_body() rejects
                               // giving a body to a purely-inherited
                               // entry (redeclare it to override first).
    int is_private;            // unused by a functional/procedural
                               // parameter; for a class method, 1 if
                               // declared in a 'private' section - see
                               // RecordField.is_private's own comment
                               // (same convention, same reasoning).
    int is_protected;          // unused by a functional/procedural
                               // parameter; for a class method, 1 if
                               // declared in a 'protected' section - see
                               // RecordField.is_protected's own comment.
    int declaring_class_ptr_idx; // unused by a functional/procedural
                               // parameter; for a class method, the
                               // pointer_types[] index of whichever class
                               // ORIGINALLY declared it - see
                               // RecordField.declaring_class_ptr_idx.
    int is_class_method;      // unused by a functional/procedural
                               // parameter; 1 if declared 'class
                               // procedure'/'class function' - a TRUE
                               // class method (Delphi terminology; NOT
                               // the loose "class method" = "method of a
                               // class" sense the REST of this codebase's
                               // own comments otherwise use for an
                               // ordinary INSTANCE method - see
                               // build_class_member_access()'s own
                               // comment). Called as 'ClassName.Name(...)',
                               // no instance, no implicit 'self' parameter
                               // at slot 0 (see parse_class_method_body()),
                               // never virtually dispatched (see
                               // build_vtable_init_chain()) and never
                               // overridable (see the method-loop's own
                               // override-eligibility check).
    int is_abstract;          // unused by a functional/procedural
                               // parameter, and never 1 alongside
                               // is_class_method (rejected at parse time -
                               // a class method is never overridden, so it
                               // could never get an implementation); 1 if
                               // declared with a trailing 'abstract;' after
                               // the header (see the method-loop's own
                               // parsing of it). An abstract method gets a
                               // PHANTOM proc_table[] entry instead of a
                               // real body - see
                               // register_abstract_method_signature() -
                               // so find_proc(mangled_name) succeeds for
                               // it exactly like a real method, letting a
                               // call through a base-typed reference
                               // compile and dispatch dynamically to
                               // whichever concrete descendant actually
                               // overrides it. Propagates through
                               // inheritance by ordinary struct-copy
                               // (parse_class_declaration()'s copy loop)
                               // until some class's own override replaces
                               // the whole header with one that has
                               // is_abstract = 0 (or re-declares abstract
                               // again, deferring further). See
                               // class_first_unresolved_abstract_method()
                               // for how this blocks 'new()'.
    int is_destructor;        // unused by a functional/procedural
                               // parameter; 1 if declared with the
                               // 'destructor' keyword instead of
                               // 'procedure'/'function' (always implies
                               // is_function = 0, no params). At most one
                               // per class hierarchy - see
                               // parse_class_declaration()'s own
                               // uniqueness/kind-mismatch checks.
                               // Consulted ONLY by dispose() (see
                               // class_find_destructor()) and those two
                               // structural checks - an ordinary direct
                               // call ('c.Destroy;') and 'inherited' both
                               // treat a destructor as a completely
                               // ordinary instance method, needing no
                               // special-casing anywhere else.
} ProcParamHeader;

#define MAX_CLASS_PROPERTIES 16 // no vm.c coupling needed (unlike
                                 // MAX_CLASS_METHODS/MAX_RECORD_FIELDS) - a
                                 // property has no runtime representation of
                                 // its own at all; it always resolves, at
                                 // PARSE time (see resolve_heap_deref_step()),
                                 // into an already-existing field offset or
                                 // an already-existing method's own vtable
                                 // slot. Stays local to parser.c.

// One 'property Name: Type read ReadTarget [write WriteTarget];' entry.
// Like RecordField/ProcParamHeader, this is pure compile-time metadata -
// resolve_heap_deref_step() is the only place that ever reads it, and it
// always resolves a property access into an ALREADY EXISTING field offset
// or method-call node shape - see that function's own comment. read_idx/
// write_idx stay valid across inheritance for the same reason
// ProcParamHeader.mangled_name/method_idx do (see parse_class_declaration()'s
// inheritance comment): a subclass's rt->fields[]/pt->methods[] always
// starts with an exact, order-preserving copy of its parent's, so an index
// recorded while parsing an ancestor class stays correct, unchanged, in
// every descendant's own tables too - this struct is simply struct-copied
// into a descendant's pt->properties[], needing no per-copy index fixup.
typedef struct {
    char name[MAX_NAME];
    DataType type;          // the property's own declared type - required to
                             // match its read target's type (the field's own
                             // type, or the getter's return type) and, if
                             // present, its write target's type (the field's
                             // own type, or the setter's sole parameter's
                             // type) EXACTLY (no widening) - checked once, at
                             // property-declaration time.
    int read_is_field;      // 1 = ReadTarget names a field (rt->fields[
                             // read_idx], a direct read, no call); 0 =
                             // ReadTarget names a getter function
                             // (pt->methods[read_idx], called with zero
                             // arguments).
    int read_idx;            // rt->fields[]/pt->methods[] index, per
                             // read_is_field.
    int has_write;           // 0 for a read-only property (no 'write' clause
                             // at all, e.g. 'property Area: real read
                             // GetArea;') - assigning to one is a compile
                             // error, checked in resolve_heap_deref_step().
    int write_is_field;      // only meaningful if has_write; 1 = WriteTarget
                             // names a field (direct write); 0 = WriteTarget
                             // names a setter procedure (called with the
                             // assigned value as its one argument).
    int write_idx;           // only meaningful if has_write; rt->fields[]/
                             // pt->methods[] index, per write_is_field.
    int is_private;          // the PROPERTY's OWN visibility - gates access
                             // to the property itself. The underlying
                             // field's/method's own is_private is
                             // DELIBERATELY not consulted once accessed
                             // through the property (standard Delphi
                             // convention: a public property can front a
                             // private field/method).
    int is_protected;        // the PROPERTY's OWN 'protected' visibility,
                             // mutually exclusive with is_private - same
                             // "gates the property itself, not whatever's
                             // underneath" reasoning as is_private above.
    int declaring_class_ptr_idx; // pointer_types[] index of whichever
                             // class's own 'class ... end;' first declared
                             // this property - same convention as
                             // RecordField.declaring_class_ptr_idx/
                             // ProcParamHeader.declaring_class_ptr_idx,
                             // needed because inheritance flattens a copy of
                             // every ancestor property into a descendant's
                             // own pt->properties[]. Properties can't be
                             // overridden in v1 (see the property-parsing
                             // loop's duplicate-name check) - unlike a
                             // method, there's no is_inherited flag, since
                             // "copied, not yet overridden" never needs
                             // distinguishing from "declared fresh" for a
                             // property.
    int is_class_property;   // 1 if declared 'class property' - a TRUE
                             // class property, backed by a class var
                             // (read_is_field/write_is_field) or a TRUE
                             // class method (see ProcParamHeader.
                             // is_class_method) instead of an instance
                             // field/method. read_idx/write_idx then index
                             // pt->class_vars[]/pt->methods[] (still
                             // filtered to is_class_method entries)
                             // instead of rt->fields[]/pt->methods[]
                             // unfiltered. Accessed only as
                             // 'ClassName.Name', never through an
                             // instance - see build_class_member_access().
} ClassProperty;

#define MAX_CLASS_VARS 16 // no vm.c coupling needed - a class var is an
                           // ordinary sym_table[]/vm_vars[] global,
                           // reusing that pre-existing MAX_SYMBOLS-bounded
                           // capacity; this only bounds the per-class
                           // PARSE-TIME lookup table, same reasoning as
                           // MAX_CLASS_PROPERTIES above.

// One 'class var Name: Type;' entry - a variable shared across ALL
// instances of a class (and every descendant that inherits it), not
// per-instance. Unlike an instance field, this has no heap-offset
// concept at all - it's an ORDINARY GLOBAL (sym_table[]/vm_vars[] slot),
// mangled "ClassName__VarName" exactly like a class method's own mangled
// name (see ProcParamHeader.mangled_name's comment), registered via the
// same add_var() every plain global uses. Struct-copied UNCHANGED (same
// sym_idx, never re-registered) into a descendant's own class_vars[] on
// inheritance - this is what makes TBase.X and TSub.X genuinely share
// ONE storage location, matching real Delphi class-var semantics for
// free, simply by not re-running add_var() for an inherited entry.
typedef struct {
    char name[MAX_NAME];
    int sym_idx;              // sym_table[]/vm_vars[] index - see comment above
    DataType type;
    int is_subrange;
    int subrange_lower;
    int subrange_upper;
    int is_private;           // same strict-private convention as every
                               // other class member - gates access to
                               // THIS class var, checked against
                               // current_class_ptr_idx.
    int is_protected;         // same convention as is_private, mutually
                               // exclusive with it.
    int declaring_class_ptr_idx; // pointer_types[] index of whichever
                               // class's own 'class ... end;' first
                               // declared this class var - same
                               // convention as every other class member.
} ClassVar;

#define MAX_POINTER_DECLS MAX_POINTER_TYPES
// MAX_CLASS_METHODS now lives in common.h - vm.c's vm_vtables[] needs it
// too (see that array's own comment). Headers only, per the classes-and-
// instances scoping note (notes/classes-and-instances-scoping.md): a
// method's BODY is a separate, later build step, not part of this
// struct at all yet.
typedef struct {
    char name[MAX_NAME];        // the pointer TYPE's own name, e.g. "PNode" -
                                 // or, for a class, the class's own name
                                 // (see is_class below): 'class TFoo ... end;'
                                 // registers TFoo directly as a pointer type,
                                 // so every existing pointer-typed variable/
                                 // parameter/field mechanism already works
                                 // for a class variable unmodified.
    int target_is_record;
    int target_record_type_idx; // only meaningful if target_is_record
    DataType target_type;       // only meaningful if !target_is_record
    int target_elem_size;       // ints per allocated instance: 1 for a
                                 // scalar target, else the target record
                                 // type's field_count - resolved together
                                 // with target_is_record/target_type
                                 // (immediately, or once is_pending
                                 // clears)
    int is_pending;
    char pending_target_name[MAX_NAME];
    int pending_line;
    int is_class;                // 1 if this pointer type is a class's
                                  // own implicit pointer type (see
                                  // parse_class_declaration()) - always
                                  // has target_is_record = 1 and
                                  // is_pending = 0; never forward-referenced,
                                  // since a class is only ever registered
                                  // once its own 'end;' is fully parsed.
    int method_count;            // only meaningful if is_class
    ProcParamHeader methods[MAX_CLASS_METHODS]; // only meaningful if is_class -
                                  // headers only; see the comment above
                                  // MAX_CLASS_METHODS
    int property_count;          // only meaningful if is_class
    ClassProperty properties[MAX_CLASS_PROPERTIES]; // only meaningful if
                                  // is_class - see ClassProperty's own
                                  // comment; flattened across inheritance
                                  // exactly like 'methods' above, but never
                                  // overridden (add-only, like fields).
    int class_var_count;         // only meaningful if is_class
    ClassVar class_vars[MAX_CLASS_VARS]; // only meaningful if is_class -
                                  // see ClassVar's own comment. Inherited
                                  // BY REFERENCE (struct-copied UNCHANGED,
                                  // same sym_idx, never re-added), not by
                                  // value like fields/properties are -
                                  // this is what makes class-var storage
                                  // genuinely SHARED across a hierarchy.
    int parent_class_ptr_idx;    // only meaningful if is_class - this
                                  // class's OWN parent's pointer_types[]
                                  // index ('class TFoo(TBase) ... end;'),
                                  // or -1 for no parent. Inheritance is
                                  // fully FLATTENED at declaration time
                                  // (see parse_class_declaration()): a
                                  // subclass's hidden record already
                                  // contains a copy of every ancestor
                                  // field, in the same order/offsets, and
                                  // 'methods' already contains a copy of
                                  // every ancestor method header (see
                                  // ProcParamHeader.mangled_name/
                                  // is_inherited) - so nothing downstream
                                  // ever needs to walk this chain at
                                  // runtime, or even at compile time,
                                  // EXCEPT class_type_is_subtype_of()
                                  // (type_checker.c's assignment/
                                  // parameter-passing compatibility
                                  // check - see docs/LANGUAGE.md#classes).
    int is_sealed;                // only meaningful if is_class - 1 if
                                  // this class was declared 'class sealed
                                  // ... end;' (see parse_class_declaration()).
                                  // Blocks any LATER 'class(ThisClass)'
                                  // from resolving - checked once, at the
                                  // single place a parent class name is
                                  // resolved. Assigned UNCONDITIONALLY
                                  // every time a class is declared (never
                                  // just 'if sealed'), matching every
                                  // other is_class-only field here -
                                  // pointer_types[] contents persist
                                  // across compiles in the same process
                                  // (only pointer_type_count resets), so a
                                  // skipped else-branch would leak a
                                  // stale sealed flag from an earlier
                                  // compile onto an unrelated class reusing
                                  // the same table slot.
} PointerTypeDef;
static PointerTypeDef pointer_types[MAX_POINTER_DECLS];
static int pointer_type_count = 0;

static int find_pointer_type(const char *name) {
    for (int i = 0; i < pointer_type_count; i++) {
        if (strcmp(pointer_types[i].name, name) == 0) return i;
    }
    return -1;
}

// Exported (see parser.h) for type_checker.c's assignment/parameter-
// passing compatibility check (try_widen_for_assignment()): true if
// 'sub' is 'target' itself, or a (transitively) inherited subclass of
// it - both must be class pointer types (see PointerTypeDef.is_class);
// anything else, including two unrelated plain 'type PFoo = ^Target;'
// pointer types, returns 0. pointer_types[] itself stays parser.c-local
// (see its own comment) - this is the one narrow query exposed instead
// of the whole array/struct. A subclass instance's pointer value is
// representationally identical to its ancestor's (same int, and the
// ancestor's own fields are always a prefix of the subclass's own
// layout - see parse_class_declaration()'s inheritance comment), so
// nothing needs converting when this returns true; the caller can just
// accept the value as-is.
int class_type_is_subtype_of(DataType sub, DataType target) {
    if (!is_pointer_type(sub) || !is_pointer_type(target)) return 0;
    int idx = sub - TYPE_POINTER_BASE;
    int target_idx = target - TYPE_POINTER_BASE;
    while (idx != -1) {
        if (idx == target_idx) return 1;
        idx = pointer_types[idx].is_class ? pointer_types[idx].parent_class_ptr_idx : -1;
    }
    return 0;
}

// 'protected' visibility's own ancestry check: whether sub_idx's class
// IS target_idx's class, or a (transitively) inherited descendant of
// it - the exact same parent_class_ptr_idx walk class_type_is_subtype_of()
// does above, adapted for the raw pointer_types[] indices
// current_class_ptr_idx/declaring_class_ptr_idx already use (a
// deliberately separate, narrower helper rather than reusing that
// DataType-typed one directly - the TYPE_POINTER_BASE conversion it
// needs doesn't fit a bare index cleanly, and this codebase's own
// established habit is a small duplicated helper over a forced-generic
// one - see e.g. vm_typed_file_handle()'s own precedent in vm.c).
// Correctly returns false when sub_idx is -1 (current_class_ptr_idx
// outside any method body - never "inside" any class) with no separate
// guard needed, since the loop condition alone already excludes it.
static int class_ptr_idx_is_or_descends_from(int sub_idx, int target_idx) {
    int idx = sub_idx;
    while (idx != -1) {
        if (idx == target_idx) return 1;
        idx = pointer_types[idx].is_class ? pointer_types[idx].parent_class_ptr_idx : -1;
    }
    return 0;
}

// One declared NAMED procedural type ('type TProc = procedure(x:
// integer); TFunc = function: real;' - see parse_type_section()'s own
// TOKEN_PROCEDURE/TOKEN_FUNCTION branch). Reuses ProcParamHeader's exact
// shape (defined above, alongside PointerTypeDef.methods for the same
// reason) purely for its signature fields (is_function/return_type/
// param_count/param_types[]/param_is_var[]) - .name/.param_names[]/
// .mangled_name/.is_inherited are all meaningless here and left unset.
typedef struct {
    char name[MAX_NAME]; // the procedural TYPE's own name, e.g. "TProc"
    ProcParamHeader sig;
} ProcTypeDef;
static ProcTypeDef proc_types[MAX_PROC_TYPES];
static int proc_type_count = 0;

static int find_proc_type(const char *name) {
    for (int i = 0; i < proc_type_count; i++) {
        if (strcmp(proc_types[i].name, name) == 0) return i;
    }
    return -1;
}

// True right when a CLASS-typed expression ('t' is a class's own
// implicit pointer type - see PointerTypeDef.is_class) is immediately
// followed by '.field' with no explicit '^' - the Delphi/Java-style
// implicit dereference reference-semantics classes use (see
// docs/LANGUAGE.md#classes and notes/classes-and-instances-scoping.md),
// as opposed to a plain 'type PFoo = ^Target;' pointer, which always
// needs an explicit 'p^.field'. Every call site that already checks
// "is_pointer_type(t) && token.type == TOKEN_CARET" to decide whether a
// '^'-dereference chain follows also checks this, to decide whether an
// IMPLICIT one follows instead.
static int class_dot_deref_pending(DataType t) {
    return is_pointer_type(t) && pointer_types[t - TYPE_POINTER_BASE].is_class && token.type == TOKEN_PERIOD;
}

static int find_record_type(const char *name) {
    for (int i = 0; i < record_type_count; i++) {
        if (strcmp(record_types[i].name, name) == 0) return i;
    }
    return -1;
}

// A type alias ('type TAge = integer;') is exactly as compile-time-only
// as a const: it never becomes a Symbol or a distinct DataType of its
// own, and has no runtime representation whatsoever - it's purely a
// second name for one of the existing scalar DataTypes, substituted the
// moment it's referenced (see parse_scalar_type() below). An alias can
// itself alias an earlier alias ('type TAge = integer; TYears = TAge;'),
// the same way one const's expression can reference an earlier const.
#define MAX_TYPE_ALIASES 20
typedef struct {
    char name[MAX_NAME];
    DataType type;
} TypeAliasDef;
static TypeAliasDef type_aliases[MAX_TYPE_ALIASES];
static int type_alias_count = 0;

static int find_type_alias(const char *name) {
    for (int i = 0; i < type_alias_count; i++) {
        if (strcmp(type_aliases[i].name, name) == 0) return i;
    }
    return -1;
}

// A subrange type ('type TAge = 0..150;') is, unlike a type alias, NOT
// just another name for TYPE_INTEGER with zero further consequence - a
// value of this type is bounds-checked at the point it's stored (see
// NODE_RANGE_CHECK and wrap_range_check() below). But it's also unlike
// an enum: it's fully assignment/arithmetic-compatible with a plain
// integer (this compiler's type checker never distinguishes them), so -
// unlike EnumTypeDef - this table never needs to be consulted by
// type_checker.c or codegen.c at all. It's purely a parser-time-only
// lookup: parse_scalar_type() resolves a subrange type NAME into plain
// TYPE_INTEGER (via its side-channel below), and the bounds are copied
// directly into whatever Symbol/ProcSymbol/RecordField/NameGroup field
// is being populated at that declaration site - this table itself is
// never touched again after that.
#define MAX_SUBRANGE_TYPES 20
typedef struct {
    char name[MAX_NAME];
    int lower;
    int upper;
} SubrangeTypeDef;
static SubrangeTypeDef subrange_types[MAX_SUBRANGE_TYPES];
static int subrange_type_count = 0;

static int find_subrange_type(const char *name) {
    for (int i = 0; i < subrange_type_count; i++) {
        if (strcmp(subrange_types[i].name, name) == 0) return i;
    }
    return -1;
}

// enum_types[]/enum_type_count (declared in common.h, defined in
// bytecode.c - see the EnumTypeDef comment there) hold every declared
// enumerated type ('type TColor = (Red, Green, Blue);'). A specific
// enum type's DataType is TYPE_ENUM_BASE + its index here.
static int find_enum_type(const char *name) {
    for (int i = 0; i < enum_type_count; i++) {
        if (strcmp(enum_types[i].name, name) == 0) return i;
    }
    return -1;
}

// Looks up a bare value name (e.g. 'Red') across every declared enum
// type's value list (they all share one flat namespace, matching real
// Pascal). On a match, fills *enum_type_idx and *ordinal and returns 1;
// returns 0 (leaving both untouched) if not found.
static int find_enum_value(const char *name, int *enum_type_idx, int *ordinal) {
    for (int i = 0; i < enum_type_count; i++) {
        for (int j = 0; j < enum_types[i].value_count; j++) {
            if (strcmp(enum_types[i].value_names[j], name) == 0) {
                *enum_type_idx = i;
                *ordinal = j;
                return 1;
            }
        }
    }
    return 0;
}

static int find_record_var(const char *name) {
    for (int i = 0; i < record_var_count; i++) {
        if (strcmp(record_vars[i].name, name) == 0) return i;
    }
    return -1;
}

// Unified record-variable lookup: checks local_record_vars[] (a
// local/parameter record - only meaningful while a procedure is
// currently being parsed) FIRST, then the global record_vars[] -
// matching how a local shadows a global of the same name everywhere
// else in this file. On a match, fills *is_local, *record_type_idx,
// and *field_idx_array (the found entry's own array of per-field
// indices - current_locals[] indices if *is_local, else sym_table[]
// indices) and returns 1; returns 0 (touching nothing) if not found in
// either table.
static int find_any_record_var(const char *name, int *is_local, int *record_type_idx, const int **field_idx_array) {
    // nesting_depth == -1 (top-level, not currently parsing a procedure/
    // method body) means local_record_var_count/local_record_vars alias
    // scope_record_var_count[-1]/scope_record_vars[-1] - out of bounds.
    // Matches find_local()'s own identical guard.
    if (nesting_depth >= 0) {
        for (int i = 0; i < local_record_var_count; i++) {
            if (strcmp(local_record_vars[i].name, name) == 0) {
                *is_local = 1;
                *record_type_idx = local_record_vars[i].record_type_idx;
                *field_idx_array = local_record_vars[i].field_local_idx;
                return 1;
            }
        }
    }
    int gi = find_record_var(name);
    if (gi != -1) {
        *is_local = 0;
        *record_type_idx = record_vars[gi].record_type_idx;
        *field_idx_array = record_vars[gi].field_sym_idx;
        return 1;
    }
    return 0;
}

// The find_local_outward() of record variables: searches every active
// scope from innermost (nesting_depth) outward to the outermost (0)
// before falling back to the flat global record_vars[] table, and (via
// *out_levels_up, only meaningful when *is_local ends up 1) reports how
// many lexical levels up a LOCAL match was found - needed by
// record_field_read_node()/record_field_assign_node() below to reach the
// right scope's own field_local_idx[] entries, and to tag the levels_up
// this field access must carry on whatever AST node it builds.
static int find_any_record_var_outward(const char *name, int *out_levels_up, int *is_local, int *record_type_idx, const int **field_idx_array) {
    for (int d = nesting_depth; d >= 0; d--) {
        for (int i = 0; i < scope_record_var_count[d]; i++) {
            if (strcmp(scope_record_vars[d][i].name, name) == 0) {
                *out_levels_up = nesting_depth - d;
                *is_local = 1;
                *record_type_idx = scope_record_vars[d][i].record_type_idx;
                *field_idx_array = scope_record_vars[d][i].field_local_idx;
                return 1;
            }
        }
    }
    int gi = find_record_var(name);
    if (gi != -1) {
        *out_levels_up = 0; // meaningless when *is_local is 0 - a global has no "level"
        *is_local = 0;
        *record_type_idx = record_vars[gi].record_type_idx;
        *field_idx_array = record_vars[gi].field_sym_idx;
        return 1;
    }
    return 0;
}

// Builds an expression node reading one already-resolved record field,
// given find_any_record_var()'s *is_local output and the field's index
// into its field_idx_array (a current_locals[] index if is_local, else a
// sym_table[] index) - the two possible storage locations a record
// variable's field can live in (see the LocalRecordVarDef/RecordVarDef
// comments above). 'levels_up' (only meaningful when is_local) is
// find_any_record_var_outward()'s own *out_levels_up - a plain
// find_any_record_var() (current-scope-only) match is always levels_up 0.
// Mirrors parse_global_symbol_reference() for the "plain scalar, no
// further indexing" case, which is the only case a record field can ever
// need: an array-typed field is rejected by add_local_record() for a
// local/parameter record, and whole-array GLOBAL record fields aren't
// read through this helper (they still go through
// parse_global_symbol_reference(), which knows how to index them).
static ASTNode *record_field_read_node(int is_local, int field_or_sym_idx, int levels_up) {
    if (is_local) {
        ASTNode *node = create_node(NODE_LOCAL_VAR);
        node->data.var_idx = field_or_sym_idx;
        node->op = (TokenType)levels_up;
        node->expression_type = local_at(field_or_sym_idx, levels_up)->type;
        return node;
    }
    ASTNode *node = create_node(NODE_VARIABLE);
    node->data.var_idx = field_or_sym_idx;
    node->expression_type = sym_table[field_or_sym_idx].type;
    return node;
}

// Builds an assignment node writing 'value' into one already-resolved
// record field - the write-side mirror of record_field_read_node() above.
// NODE_LOCAL_ASSIGN needs its own ->expression_type set explicitly (type_
// checker.c has no visibility into parser.c's current_locals[] to look it
// up by index, unlike NODE_ASSIGN, which type_checker.c checks straight
// against sym_table[]). No is_const check here (unlike
// parse_global_assignment() below): a typed constant is always a plain
// array symbol (record typed constants aren't supported - see
// parse_typed_const_declaration()), and an array symbol never reaches
// this function, only ever record_field_assign_node()'s record-specific
// callers - so field_or_sym_idx here is never a typed constant's own
// storage.
static ASTNode *record_field_assign_node(int is_local, int field_or_sym_idx, int levels_up, ASTNode *value) {
    ASTNode *node = create_node(is_local ? NODE_LOCAL_ASSIGN : NODE_ASSIGN);
    node->data.var_idx = field_or_sym_idx;
    node->left = value;
    if (is_local) {
        node->op = (TokenType)levels_up;
        node->expression_type = local_at(field_or_sym_idx, levels_up)->type;
    }
    return node;
}

// -1 if record_types[record_type_idx] has no field with this name.
static int find_record_field(int record_type_idx, const char *field_name) {
    RecordTypeDef *rt = &record_types[record_type_idx];
    for (int i = 0; i < rt->field_count; i++) {
        if (strcmp(rt->fields[i].name, field_name) == 0) return i;
    }
    return -1;
}

// How many leaf (scalar/array) storage slots record_type_idx flattens
// into - 1 per ordinary field, or the nested type's own leaf count for a
// record-typed field (recursively). A nested field's own type is
// guaranteed array-field-free (see record_type_has_array_field() and
// where it's enforced in parse_type_section()), so this never needs to
// special-case arrays below the top level - every leaf a nested field
// contributes is a plain scalar.
static int record_type_leaf_count(int record_type_idx) {
    RecordTypeDef *rt = &record_types[record_type_idx];
    int total = 0;
    for (int i = 0; i < rt->field_count; i++) {
        total += rt->fields[i].is_record
            ? record_type_leaf_count(rt->fields[i].record_type_idx)
            : 1;
    }
    return total;
}

// Typed-file counterpart of record_type_leaf_count() above: total ON-
// DISK BYTES record_type_idx flattens into, summing each leaf's actual
// disk_width (1 for TOKEN_BYTE/TOKEN_SHORTINT, 2 for TOKEN_WORD, 4 -
// sizeof(int) - for everything else, including an ordinary hand-written
// subrange field) instead of counting 1 per leaf. For an all-ordinary
// record this returns exactly leaf_count * sizeof(int) - identical to
// what typed-file I/O already did before byte/shortint/word existed, so
// a record with no byte/shortint/word field keeps its exact original
// on-disk layout (see the compatibility note on scalar_type_disk_width).
static int record_type_byte_size(int record_type_idx) {
    RecordTypeDef *rt = &record_types[record_type_idx];
    int total = 0;
    for (int i = 0; i < rt->field_count; i++) {
        RecordField *f = &rt->fields[i];
        if (f->is_record) {
            total += record_type_byte_size(f->record_type_idx);
        } else if (f->disk_width == TOKEN_BYTE || f->disk_width == TOKEN_SHORTINT) {
            total += 1;
        } else if (f->disk_width == TOKEN_WORD) {
            total += 2;
        } else {
            total += (int)sizeof(int);
        }
    }
    return total;
}

// True if record_type_idx has an array-typed field at its own top
// level. A type can only ever nest an already-fully-declared type (see
// the self-reference/cycle note above record_type_count), so any type
// that itself passed this check when IT nested something is guaranteed
// array-field-free all the way down - callers never need to recurse
// through nested fields to check transitively, only the immediate
// target type.
static int record_type_has_array_field(int record_type_idx) {
    RecordTypeDef *rt = &record_types[record_type_idx];
    for (int i = 0; i < rt->field_count; i++) {
        if (rt->fields[i].is_array) return 1;
    }
    return 0;
}

// True if record_type_idx has a nested-record field at its own top
// level - same declaration-order induction argument as
// record_type_has_array_field() above. Used by the array-element-type,
// pointer-target, and with-target restriction checks, none of which
// generalize to a nested field's "N slots instead of 1" shape.
static int record_type_has_nested_field(int record_type_idx) {
    RecordTypeDef *rt = &record_types[record_type_idx];
    for (int i = 0; i < rt->field_count; i++) {
        if (rt->fields[i].is_record) return 1;
    }
    return 0;
}

// 'with recordVar do statement;' - a stack (nesting: 'with a do with b
// do ...') of record_vars[] indices for whichever with-statement(s) are
// currently being parsed. Purely a parser-time convenience: since a
// record field already resolves to an ordinary mangled global by the
// time anything downstream sees it (see the RecordTypeDef comment
// above), 'with' needs no AST node, codegen, or type-checker changes at
// all - it just lets a bare field name resolve the same way 'p.field'
// already does, for as long as the stack is non-empty. Search order is
// innermost-to-outermost, so a nested 'with's field shadows an outer
// one of the same name - and, matching classic Pascal behavior, a
// with-field takes priority over a local/global variable of the same
// name too (checked at the same priority as "is this identifier itself
// a record variable name" in every identifier-resolution call site).
#define MAX_WITH_DEPTH 8
static int with_stack[MAX_WITH_DEPTH];
static int with_depth = 0;

// Returns the mangled global sym_table[] index for 'name' if it names a
// field of some currently-active 'with' target, else -1.
static int find_with_field(const char *name) {
    for (int i = with_depth - 1; i >= 0; i--) {
        RecordVarDef *rv = &record_vars[with_stack[i]];
        int field_idx = find_record_field(rv->record_type_idx, name);
        if (field_idx != -1) return rv->field_sym_idx[field_idx];
    }
    return -1;
}

// The proc_table index of the function whose body is currently being
// parsed, or -1 if we're not inside a function body (either not inside
// any procedure/function at all, or inside a plain procedure). Used to
// recognize 'FunctionName := expr;' inside that function's own body as
// setting its return value, rather than an ordinary assignment.
static int current_function_idx = -1;

// The proc_table index of the procedure/function whose declaration
// section is currently being parsed, or -1 while parsing the main
// program's own declarations. Distinct from current_function_idx (which
// is function-only and return-value-assignment-specific): this tracks
// EVERY currently-open procedure/function, so add_proc() can stamp a
// newly-declared nested procedure's lexical_parent_idx from it. Both are
// saved/restored as plain C locals around subroutine_declaration()'s own
// recursive call, exactly like the C call stack already threads any
// other per-invocation state - see subroutine_declaration() itself.
static int current_proc_idx = -1;

// The pointer_types[] index of the class whose method body is currently
// being parsed, or -1 outside any method body. Lets a bare identifier in
// a method body's expressions/statements be checked against the class's
// own fields/methods (see class_has_member()) for the unqualified
// 'self.' shorthand - saved/restored in parse_class_method_body() the
// same way current_proc_idx is, just one more variable in that existing
// pattern.
static int current_class_ptr_idx = -1;

// Whether the method body CURRENTLY being parsed is a TRUE class method
// (ProcParamHeader.is_class_method) - meaningless (0) whenever
// current_class_ptr_idx is -1. No 'self' local exists in a class
// method's body at all (see parse_class_method_body()'s own skip), so
// this is what lets self-shorthand resolution (parse_self_shorthand_
// read()/write()) tell "no self exists here, only class members are
// reachable" apart from the ordinary instance-method case - saved/
// restored the same way current_class_ptr_idx already is.
static int current_method_is_class_method = 0;

const char *get_current_filename(void) {
    return current_filename;
}

// Lets a post-parse pass (type_checker.c, optimizer.c, codegen.c) point
// error messages at whichever file a specific proc/function actually
// came from (see ProcSymbol.source_file in common.h), rather than
// whichever file parsing itself finished on last.
void set_current_filename(const char *f) {
    current_filename = f;
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

// Builds a fresh literal AST leaf node from a folded default parameter
// value (type/value pair stored the same way as parser.c's own ConstDef)
// - used to splice a default in for an omitted trailing call argument.
// Mirrors the identifier-resolves-to-a-const substitution in
// primary_expression(), except 'line' must be passed explicitly: by the
// time a call's argument list has been fully parsed, token.line (which
// create_node() would otherwise stamp) points well past the call itself.
static ASTNode *make_default_value_node(DataType type, int value, int line) {
    ASTNode *node;
    if (type == TYPE_REAL) {
        node = create_node(NODE_REAL_NUMBER);
        node->data.num_value = value;
    } else if (type == TYPE_BOOLEAN) {
        node = create_node(NODE_BOOLEAN);
        node->data.num_value = value;
    } else if (type == TYPE_STRING || type == TYPE_CHAR) {
        node = create_node(NODE_STRING);
        node->data.var_idx = value;
    } else { // TYPE_INTEGER
        node = create_node(NODE_NUMBER);
        node->data.num_value = value;
    }
    node->expression_type = type;
    node->line = line;
    return node;
}

// Wraps 'value' in a NODE_RANGE_CHECK if is_subrange is set; otherwise
// returns 'value' unchanged. Called at every point a value is about to
// be stored into a subrange-typed target (see SubrangeTypeDef above).
static ASTNode *wrap_range_check(ASTNode *value, int is_subrange, int lower, int upper) {
    if (!is_subrange) return value;
    ASTNode *node = create_node(NODE_RANGE_CHECK);
    node->expression_type = TYPE_INTEGER;
    node->left = value;
    ASTNode *lo = create_node(NODE_NUMBER);
    lo->data.num_value = lower;
    lo->expression_type = TYPE_INTEGER;
    ASTNode *hi = create_node(NODE_NUMBER);
    hi->data.num_value = upper;
    hi->expression_type = TYPE_INTEGER;
    node->right = lo;
    node->extra = hi;
    return node;
}

// Which unit's own source is currently being parsed (empty = the main
// program, or between unit loads - see load_unit()), and whether that's
// currently its 'interface' (0) or 'implementation' (1) section - what
// add_var()/add_proc() stamp a new declaration's own declaring_unit/
// is_unit_private with (see Symbol/ProcSymbol in common.h). Reset at
// the top of parse_ast() like every other global counter there;
// saved/restored around load_unit()'s own body exactly like
// current_filename/the lexer position already are, so a nested 'uses'
// (one unit loading another) can't leak its own section into the
// resuming outer unit's.
static char current_unit_name[MAX_NAME];
static int current_section_is_implementation = 0;

// Visibility check shared by find_var_soft_visible()/find_proc_visible()
// below - a symbol declared in a unit's interface (or the main program)
// is always visible; one declared in a unit's implementation is only
// visible while THAT SAME unit's own source is what's currently being
// parsed (its own implementation referencing its own private
// declarations, including from an earlier point in that same
// implementation section).
static int symbol_visible_here(const char *declaring_unit, int is_unit_private) {
    if (!is_unit_private) return 1;
    return strcmp(declaring_unit, current_unit_name) == 0;
}

// Soft lookup - returns -1 if `name` isn't a declared global variable,
// rather than erroring. Used where the caller needs to check "is this a
// global X" before deciding how to proceed (see try_get_array_bounds
// below), as opposed to find_var()'s "this MUST be declared, error
// immediately if not" contract. Deliberately visibility-BLIND (unlike
// find_var_soft_visible() just below) - used directly at declaration-
// time duplicate-detection sites, which must keep seeing every existing
// var regardless of which unit (if any) declared it, or a private var
// in one unit could silently collide with an unrelated one elsewhere.
static int find_var_soft(const char *name) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) return i;
    }
    return -1;
}

// The visibility-aware counterpart of find_var_soft() above - used at
// every REFERENCE site (resolving a bare identifier the user actually
// typed against the global var table), as opposed to a declaration-time
// duplicate check.
static int find_var_soft_visible(const char *name) {
    int idx = find_var_soft(name);
    if (idx != -1 && !symbol_visible_here(sym_table[idx].declaring_unit, sym_table[idx].is_unit_private)) {
        return -1;
    }
    return idx;
}

static int find_var(const char *name) {
    int idx = find_var_soft_visible(name);
    if (idx == -1) {
        compile_error(token.line, "Unknown identifier '%s'", name);
    }
    return idx;
}

// Soft lookup for a GLOBAL file variable specifically (files are always
// global - see TYPE_FILE/TYPE_TYPED_FILE/TYPE_UNTYPED_FILE) - returns
// its sym_table[] index, or -1 if `name` isn't a declared file variable
// AT ALL (any of the three kinds - not an error - this is used
// everywhere a leading 'read(f, ...)'/'write(f, ...)'/'eof(f)'/
// 'assign(f, ...)' file argument needs to be *detected*, not required,
// falling back to the ordinary stdin/stdout path when it's absent).
// Callers that need to tell the three kinds apart check
// sym_table[idx].type == TYPE_TYPED_FILE/TYPE_UNTYPED_FILE themselves
// afterward.
static int find_file_var_soft(const char *name) {
    int idx = find_var_soft_visible(name);
    if (idx == -1 || (sym_table[idx].type != TYPE_FILE && sym_table[idx].type != TYPE_TYPED_FILE
                       && sym_table[idx].type != TYPE_UNTYPED_FILE)) return -1;
    return idx;
}

static void add_var(const char *name, DataType type) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) {
            compile_error(token.line, "Duplicate variable declaration '%s'", name);
        }
    }
    if (find_const(name) != -1) {
        compile_error(token.line, "'%s' is already declared as a constant", name);
    }
    {
        int existing_enum_type_idx, existing_ordinal;
        if (find_enum_value(name, &existing_enum_type_idx, &existing_ordinal)) {
            compile_error(token.line, "'%s' is already declared as an enumerated value", name);
        }
    }
    if (sym_count >= MAX_SYMBOLS) {
        compile_error(token.line, "Too many variable declarations (limit is %d)", MAX_SYMBOLS);
    }
    strcpy(sym_table[sym_count].name, name);
    strcpy(sym_table[sym_count].declaring_unit, current_unit_name);
    sym_table[sym_count].is_unit_private = current_section_is_implementation;
    sym_table[sym_count].type = type;
    sym_table[sym_count].is_array = 0;
    sym_table[sym_count].array_lower = 0;
    sym_table[sym_count].array_upper = 0;
    sym_table[sym_count].array_base = 0;
    sym_table[sym_count].is_subrange = 0; // defensive reset (see comment above the Symbol struct)
    sym_table[sym_count].subrange_lower = 0;
    sym_table[sym_count].subrange_upper = 0;
    sym_table[sym_count].is_const = 0; // defensive reset
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
    if (find_const(name) != -1) {
        compile_error(token.line, "'%s' is already declared as a constant", name);
    }
    {
        int existing_enum_type_idx, existing_ordinal;
        if (find_enum_value(name, &existing_enum_type_idx, &existing_ordinal)) {
            compile_error(token.line, "'%s' is already declared as an enumerated value", name);
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
    strcpy(sym_table[sym_count].declaring_unit, current_unit_name);
    sym_table[sym_count].is_unit_private = current_section_is_implementation;
    sym_table[sym_count].type = elem_type;
    sym_table[sym_count].is_array = 1;
    sym_table[sym_count].array_lower = lower;
    sym_table[sym_count].array_upper = upper;
    sym_table[sym_count].array_base = array_mem_count;
    sym_table[sym_count].is_2d = 0; // defensive reset (see comment above the Symbol struct)
    sym_table[sym_count].is_nd = 0; // defensive reset
    sym_table[sym_count].is_subrange = 0;
    sym_table[sym_count].subrange_lower = 0;
    sym_table[sym_count].subrange_upper = 0;
    sym_table[sym_count].is_record_array = 0; // defensive reset
    sym_table[sym_count].record_elem_field_count = 0;
    sym_table[sym_count].is_const = 0; // defensive reset
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
    if (find_const(name) != -1) {
        compile_error(token.line, "'%s' is already declared as a constant", name);
    }
    {
        int existing_enum_type_idx, existing_ordinal;
        if (find_enum_value(name, &existing_enum_type_idx, &existing_ordinal)) {
            compile_error(token.line, "'%s' is already declared as an enumerated value", name);
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
    strcpy(sym_table[sym_count].declaring_unit, current_unit_name);
    sym_table[sym_count].is_unit_private = current_section_is_implementation;
    sym_table[sym_count].type = elem_type;
    sym_table[sym_count].is_array = 1;
    sym_table[sym_count].array_lower = lower;
    sym_table[sym_count].array_upper = upper;
    sym_table[sym_count].array_base = array_mem_count;
    sym_table[sym_count].is_2d = 1;
    sym_table[sym_count].array_lower2 = lower2;
    sym_table[sym_count].array_upper2 = upper2;
    sym_table[sym_count].is_nd = 0; // defensive reset
    sym_table[sym_count].is_subrange = 0; // defensive reset (see comment above the Symbol struct)
    sym_table[sym_count].subrange_lower = 0;
    sym_table[sym_count].subrange_upper = 0;
    sym_table[sym_count].is_record_array = 0; // defensive reset
    sym_table[sym_count].record_elem_field_count = 0;
    sym_table[sym_count].is_const = 0; // defensive reset
    array_mem_count += size;
    sym_count++;
}

// Same as add_array_var()/add_array_var_2d(), but for an array with 3 OR
// MORE dimensions ('name: array[lo1..hi1, lo2..hi2, lo3..hi3, ...] of
// type'). Unlike add_array_var_2d() (which still uses array_lower/
// array_upper for its first dimension), this stores EVERY dimension's
// bounds uniformly in nd_lower[]/nd_upper[] (index 0..dims-1) -
// array_lower/array_upper/array_lower2/array_upper2 stay unused
// (defensively zeroed) - simpler to reason about than mixing "dimension
// 0 lives in the old fields, the rest live in the new ones". Bounds
// must already be validated (lower <= upper, each dimension) by the
// caller. Reserves the full product of every dimension's size - the
// whole flattened, row-major region - from the same shared array-memory
// pool every array (1D, 2D, or N-D) draws from.
static void add_array_var_nd(const char *name, DataType elem_type, int dims, const int *lower, const int *upper) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) {
            compile_error(token.line, "Duplicate variable declaration '%s'", name);
        }
    }
    if (find_const(name) != -1) {
        compile_error(token.line, "'%s' is already declared as a constant", name);
    }
    {
        int existing_enum_type_idx, existing_ordinal;
        if (find_enum_value(name, &existing_enum_type_idx, &existing_ordinal)) {
            compile_error(token.line, "'%s' is already declared as an enumerated value", name);
        }
    }
    if (sym_count >= MAX_SYMBOLS) {
        compile_error(token.line, "Too many variable declarations (limit is %d)", MAX_SYMBOLS);
    }
    int size = 1;
    for (int d = 0; d < dims; d++) {
        size *= (upper[d] - lower[d] + 1);
    }
    if (array_mem_count + size > MAX_ARRAY_MEM) {
        compile_error(token.line, "Array storage exhausted (limit is %d total elements across all arrays)", MAX_ARRAY_MEM);
    }
    strcpy(sym_table[sym_count].name, name);
    strcpy(sym_table[sym_count].declaring_unit, current_unit_name);
    sym_table[sym_count].is_unit_private = current_section_is_implementation;
    sym_table[sym_count].type = elem_type;
    sym_table[sym_count].is_array = 1;
    sym_table[sym_count].array_lower = 0;
    sym_table[sym_count].array_upper = 0;
    sym_table[sym_count].array_base = array_mem_count;
    sym_table[sym_count].is_2d = 0;
    sym_table[sym_count].array_lower2 = 0;
    sym_table[sym_count].array_upper2 = 0;
    sym_table[sym_count].is_nd = 1;
    sym_table[sym_count].nd_dims = dims;
    for (int d = 0; d < dims; d++) {
        sym_table[sym_count].nd_lower[d] = lower[d];
        sym_table[sym_count].nd_upper[d] = upper[d];
    }
    sym_table[sym_count].is_subrange = 0; // defensive reset (see comment above the Symbol struct)
    sym_table[sym_count].subrange_lower = 0;
    sym_table[sym_count].subrange_upper = 0;
    sym_table[sym_count].is_record_array = 0; // defensive reset
    sym_table[sym_count].record_elem_field_count = 0;
    sym_table[sym_count].is_const = 0; // defensive reset
    array_mem_count += size;
    sym_count++;
}

// A 1D array of records ('array[lo..hi] of TSomeRecord') - each element
// occupies record_types[record_type_idx].field_count contiguous ints in
// vm_array_mem[] (rather than 1, like every other array element type) -
// see is_record_array/record_elem_field_count in the Symbol comment in
// common.h. Only 1D is supported so far (2D/ND arrays of records are a
// known gap - see docs/LANGUAGE.md); the record type itself must have no
// array-typed field (an array of records with an array field would need
// a variable stride per field, not a fixed one - out of scope for now).
// Does NOT register the sym_table[]<->record_type_idx mapping itself -
// see register_record_array() below, called separately by every call
// site (global var section, add_local_array_rec()) right after this.
static void register_record_array(int sym_idx, int record_type_idx) {
    if (record_array_count >= MAX_RECORD_ARRAYS) {
        compile_error(token.line, "Too many array-of-record declarations (limit is %d)", MAX_RECORD_ARRAYS);
    }
    record_arrays[record_array_count].sym_idx = sym_idx;
    record_arrays[record_array_count].record_type_idx = record_type_idx;
    record_array_count++;
}

static void add_array_var_rec(const char *name, int record_type_idx, int lower, int upper) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) {
            compile_error(token.line, "Duplicate variable declaration '%s'", name);
        }
    }
    if (find_const(name) != -1) {
        compile_error(token.line, "'%s' is already declared as a constant", name);
    }
    {
        int existing_enum_type_idx, existing_ordinal;
        if (find_enum_value(name, &existing_enum_type_idx, &existing_ordinal)) {
            compile_error(token.line, "'%s' is already declared as an enumerated value", name);
        }
    }
    if (sym_count >= MAX_SYMBOLS) {
        compile_error(token.line, "Too many variable declarations (limit is %d)", MAX_SYMBOLS);
    }
    RecordTypeDef *rt = &record_types[record_type_idx];
    for (int i = 0; i < rt->field_count; i++) {
        if (rt->fields[i].is_array) {
            compile_error(token.line, "Array-of-record element type '%s' has an array field '%s' - arrays of records with an array field aren't supported yet",
                          rt->name, rt->fields[i].name);
        }
        if (rt->fields[i].is_record) {
            compile_error(token.line, "Array-of-record element type '%s' has a nested-record field '%s' - arrays of records with a nested-record field aren't supported yet",
                          rt->name, rt->fields[i].name);
        }
    }
    int size = (upper - lower + 1) * rt->field_count;
    if (array_mem_count + size > MAX_ARRAY_MEM) {
        compile_error(token.line, "Array storage exhausted (limit is %d total elements across all arrays)", MAX_ARRAY_MEM);
    }
    strcpy(sym_table[sym_count].name, name);
    strcpy(sym_table[sym_count].declaring_unit, current_unit_name);
    sym_table[sym_count].is_unit_private = current_section_is_implementation;
    sym_table[sym_count].type = TYPE_INTEGER; // unused - see is_record_array in common.h
    sym_table[sym_count].is_array = 1;
    sym_table[sym_count].array_lower = lower;
    sym_table[sym_count].array_upper = upper;
    sym_table[sym_count].array_base = array_mem_count;
    sym_table[sym_count].is_2d = 0;
    sym_table[sym_count].is_nd = 0;
    sym_table[sym_count].is_subrange = 0;
    sym_table[sym_count].subrange_lower = 0;
    sym_table[sym_count].subrange_upper = 0;
    sym_table[sym_count].is_record_array = 1;
    sym_table[sym_count].record_elem_field_count = rt->field_count;
    sym_table[sym_count].is_const = 0; // defensive reset
    array_mem_count += size;
    sym_count++;
}

// Recursively creates one hidden global symbol per LEAF field of
// record_type_idx, mangled under "prefix__" - used by add_record_var()
// below for a nested-record field's own subtree. The nested type is
// guaranteed to have no array-typed field anywhere in it (enforced when
// the nested field was declared - see parse_type_section()), so only
// the is_record/scalar branches can ever recurse further; an is_array
// field can still appear at THIS level (an ordinary array field of the
// nested type's own top level is fine - only a type used AS a nested
// field is restricted, not what a global record's own fields may be).
static void add_record_var_fields(const char *prefix, int record_type_idx) {
    RecordTypeDef *rt = &record_types[record_type_idx];
    for (int i = 0; i < rt->field_count; i++) {
        RecordField *f = &rt->fields[i];
        if (strlen(prefix) + 2 + strlen(f->name) >= MAX_NAME) {
            compile_error(token.line, "Record variable/field name '%s.%s' too long (limit is %d characters combined)",
                          prefix, f->name, MAX_NAME - 3);
        }
        char mangled[2 * MAX_NAME];
        snprintf(mangled, sizeof(mangled), "%s__%s", prefix, f->name);
        if (f->is_record) {
            add_record_var_fields(mangled, f->record_type_idx);
            continue;
        }
        if (f->is_array) {
            add_array_var(mangled, f->type, f->array_lower, f->array_upper);
        } else {
            add_var(mangled, f->type);
        }
        sym_table[sym_count - 1].is_subrange = f->is_subrange;
        sym_table[sym_count - 1].subrange_lower = f->subrange_lower;
        sym_table[sym_count - 1].subrange_upper = f->subrange_upper;
    }
}

// Declares a GLOBAL record variable of the given record type: creates one
// ordinary hidden global symbol per field (mangled "name__fieldname"),
// via add_var()/add_array_var() exactly as if the user had declared each
// field as its own separate global variable. See the comment above
// RecordTypeDef for why this makes field access, type checking, codegen,
// and DCE all work for free, with zero new runtime machinery. A nested-
// record field instead expands, via add_record_var_fields() above, into
// N contiguous leaf symbols - field_sym_idx[i] holds the FIRST of those
// (or the field's own single symbol, for a scalar/array field - same
// convention, unified), and every other leaf lives at a
// record_type_leaf_count()-computed offset from it (see
// resolve_record_field_leaf()).
static void add_record_var(const char *name, int record_type_idx) {
    if (find_var_soft(name) != -1 || find_record_var(name) != -1) {
        compile_error(token.line, "Duplicate variable declaration '%s'", name);
    }
    if (find_const(name) != -1) {
        compile_error(token.line, "'%s' is already declared as a constant", name);
    }
    {
        int existing_enum_type_idx, existing_ordinal;
        if (find_enum_value(name, &existing_enum_type_idx, &existing_ordinal)) {
            compile_error(token.line, "'%s' is already declared as an enumerated value", name);
        }
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
        int base = sym_count; // first leaf symbol about to be created for field i
        if (f->is_record) {
            add_record_var_fields(mangled, f->record_type_idx);
        } else {
            if (f->is_array) {
                add_array_var(mangled, f->type, f->array_lower, f->array_upper);
            } else {
                add_var(mangled, f->type);
            }
            sym_table[sym_count - 1].is_subrange = f->is_subrange;
            sym_table[sym_count - 1].subrange_lower = f->subrange_lower;
            sym_table[sym_count - 1].subrange_upper = f->subrange_upper;
        }
        rv->field_sym_idx[i] = base;
    }
    record_var_count++;
}

// Soft lookup - returns -1 rather than erroring, since the caller needs
// to decide "is this name a procedure or a variable?" before knowing
// which error (if any) is appropriate.
// Deliberately visibility-BLIND, same reasoning as find_var_soft() above -
// used both at declaration-time duplicate-detection sites AND at every
// class-method mangled-name lookup (h->mangled_name, never a raw user-
// typed identifier - methods are resolved through
// pointer_types[].methods[], an entirely separate mechanism this
// compiler's unit-visibility feature doesn't touch). Only the handful of
// genuine "resolve a bare identifier the user typed" reference sites use
// find_proc_visible() below instead.
static int find_proc(const char *name) {
    for (int i = 0; i < proc_count; i++) {
        if (strcmp(proc_table[i].name, name) == 0) return i;
    }
    return -1;
}

// The visibility-aware counterpart of find_proc() above - see
// find_var_soft_visible()'s own comment for the reasoning.
static int find_proc_visible(const char *name) {
    int idx = find_proc(name);
    if (idx != -1 && !symbol_visible_here(proc_table[idx].declaring_unit, proc_table[idx].is_unit_private)) {
        return -1;
    }
    return idx;
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
    if (find_const(name) != -1) {
        compile_error(token.line, "'%s' is already declared as a constant", name);
    }
    {
        int existing_enum_type_idx, existing_ordinal;
        if (find_enum_value(name, &existing_enum_type_idx, &existing_ordinal)) {
            compile_error(token.line, "'%s' is already declared as an enumerated value", name);
        }
    }
    if (proc_count >= MAX_PROCEDURES) {
        compile_error(token.line, "Too many procedure declarations (limit is %d)", MAX_PROCEDURES);
    }
    strcpy(proc_table[proc_count].name, name);
    strncpy(proc_table[proc_count].source_file, current_filename, MAX_UNIT_PATH - 1);
    proc_table[proc_count].source_file[MAX_UNIT_PATH - 1] = '\0';
    strcpy(proc_table[proc_count].declaring_unit, current_unit_name);
    proc_table[proc_count].is_unit_private = current_section_is_implementation;
    proc_table[proc_count].unmangled_name[0] = '\0'; // defensive reset (see comment above ProcSymbol) - overwritten by parse_class_method_body() for a method
    proc_table[proc_count].entry_address = -1; // resolved during codegen
    proc_table[proc_count].body = NULL;        // set once the body is parsed
    proc_table[proc_count].is_forward = 0;     // may be set to 1 right after, if this is a forward declaration
    proc_table[proc_count].param_count = 0;    // defensive reset (see comment above the struct)
    proc_table[proc_count].is_function = 0;    // may be set to 1 right after, if this is a function
    // Lexical nesting: current_proc_idx is whichever procedure/function's
    // declaration section is currently being parsed (-1 for the main
    // program), set/restored by subroutine_declaration() around its own
    // recursive call - see there. Fixed at registration time and never
    // touched again.
    proc_table[proc_count].lexical_parent_idx = current_proc_idx;
    proc_table[proc_count].lexical_depth = nesting_depth + 1;
    return proc_count++;
}

// Soft lookup, like find_proc() - returns -1 if `name` isn't a
// parameter/local of the procedure currently being parsed (or if we're
// not currently parsing a procedure body at all). A local shadows a
// procedure name or a global variable of the same name, matching
// standard Pascal lexical scoping - which is exactly why every call site
// below checks this first.
static int find_local(const char *name) {
    if (nesting_depth < 0) return -1;
    for (int i = 0; i < current_local_count; i++) {
        if (strcmp(current_locals[i].name, name) == 0) return i;
    }
    return -1;
}

// Scope-local only, by design - do NOT use this for ordinary name
// resolution (see find_local_outward() below for that). Every
// add_local()/add_local_array()/add_local_array_rec()/add_static_local()
// duplicate-declaration check needs exactly this "does this name already
// exist in MY OWN scope" question, not "is this name visible from here" -
// searching outward here would wrongly reject a nested procedure's local
// legitimately shadowing an ancestor's same-named local, which standard
// Pascal allows.
static int find_local_in_current_scope(const char *name) {
    return find_local(name);
}

// The general-purpose local lookup: searches from the innermost active
// scope (nesting_depth) outward to the outermost (0), returning the
// first match - standard lexical shadowing, an inner declaration hiding
// an outer one of the same name. On a match, *out_levels_up receives how
// many lexical levels up it was found (0 = the innermost/currently
// active scope, identical to what find_local() alone already means) -
// callers that go on to build a runtime-access AST node (NODE_LOCAL_VAR/
// NODE_LOCAL_ASSIGN/NODE_LOCAL_VAR_REF/NODE_VAR_PARAM_READ/
// NODE_VAR_PARAM_ASSIGN/NODE_REF_ARRAY_ACCESS or _ASSIGN, in any
// dimensionality) must tag it onto that node (node->op = (TokenType)
// levels_up - see the ASTNode field-reuse comment in common.h's Opcode
// section) so codegen knows whether to address the current frame
// (OP_LOAD_LOCAL/OP_STORE_LOCAL/OP_PUSH_LOCAL_REF, levels_up == 0) or an
// ancestor's, via the static-link chain (OP_LOAD_ENCLOSING/
// OP_STORE_ENCLOSING/OP_PUSH_ENCLOSING_REF, levels_up >= 1).
//
// NODE_LOCAL_FOR and NODE_LOCAL_READLN can NOT carry a levels_up tag this
// way (their own ->op already holds TOKEN_TO/TOKEN_DOWNTO and
// TOKEN_READ/TOKEN_READLN respectively) - a 'for' loop counter and a
// readln target must resolve to the CURRENT procedure's own scope only;
// see their call sites for the explicit "found outward" rejection this
// implies (a documented known gap, not a silent wrong answer).
static int find_local_outward(const char *name, int *out_levels_up) {
    for (int d = nesting_depth; d >= 0; d--) {
        for (int i = 0; i < scope_local_count[d]; i++) {
            if (strcmp(scope_locals[d][i].name, name) == 0) {
                *out_levels_up = nesting_depth - d;
                return i;
            }
        }
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
    int levels_up;
    int local_idx = find_local_outward(name, &levels_up);
    if (local_idx != -1) {
        LocalSymbol *ls = local_at(local_idx, levels_up);
        if (ls->is_array) {
            int sym_idx = ls->array_sym_idx;
            if (sym_table[sym_idx].is_2d || sym_table[sym_idx].is_nd) {
                compile_error(token.line, "'%s' is a multi-dimensional array - low/high/length don't support 2D/N-D arrays yet", name);
            }
            *lower = sym_table[sym_idx].array_lower;
            *upper = sym_table[sym_idx].array_upper;
            return 1;
        }
        if (ls->is_array_ref) {
            if (ls->is_2d || ls->is_nd) {
                compile_error(token.line, "'%s' is a multi-dimensional array - low/high/length don't support 2D/N-D arrays yet", name);
            }
            *lower = ls->array_lower;
            *upper = ls->array_upper;
            return 1;
        }
        return 0; // a local, but not an array
    }
    int global_idx = find_var_soft_visible(name);
    if (global_idx != -1 && sym_table[global_idx].is_array) {
        if (sym_table[global_idx].is_2d || sym_table[global_idx].is_nd) {
            compile_error(token.line, "'%s' is a multi-dimensional array - low/high/length don't support 2D/N-D arrays yet", name);
        }
        *lower = sym_table[global_idx].array_lower;
        *upper = sym_table[global_idx].array_upper;
        return 1;
    }
    return 0;
}


// True if 'name' is already a local/parameter RECORD variable's own name
// (as opposed to one of its mangled per-field frame slots, which
// find_local() wouldn't recognize as belonging to 'name' at all - a
// record variable's own name never becomes a current_locals[] entry by
// itself). Every OTHER local-registering function (add_local(),
// add_local_array(), add_static_local()) needs this check too, alongside
// find_local(), to catch a plain local/array/static colliding with an
// already-declared record parameter/local of the same name - the mirror
// image of the check add_local_record() itself already does via
// find_any_record_var().
static int local_record_name_collides(const char *name) {
    // Same nesting_depth == -1 guard as find_any_record_var() above -
    // not yet observed to be reachable in practice (add_local() appears
    // to only ever run once nesting_depth has already been incremented),
    // but the same latent out-of-bounds read either way if it ever is.
    if (nesting_depth < 0) return 0;
    for (int i = 0; i < local_record_var_count; i++) {
        if (strcmp(local_record_vars[i].name, name) == 0) return 1;
    }
    return 0;
}

static int add_local(const char *name, DataType type) {
    if (find_local_in_current_scope(name) != -1 || local_record_name_collides(name)) {
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
    current_locals[current_local_count].is_subrange = 0; // defensive reset (see comment above LocalSymbol)
    current_locals[current_local_count].subrange_lower = 0;
    current_locals[current_local_count].subrange_upper = 0;
    current_locals[current_local_count].is_static = 0;
    current_locals[current_local_count].static_sym_idx = 0;
    current_locals[current_local_count].is_var_param = 0; // defensive reset (see comment above LocalSymbol)
    current_locals[current_local_count].is_const_param = 0; // defensive reset (see comment above LocalSymbol)
    current_locals[current_local_count].is_out_param = 0; // defensive reset (see comment above LocalSymbol)
    current_locals[current_local_count].is_proc_param = 0; // defensive reset (see comment above LocalSymbol)
    return current_local_count++;
}

// Registers a general by-reference SCALAR parameter ('var name: type',
// 'const name: type', or 'out name: type' - is_const/is_out select
// which, both mutually exclusive, plain 'var' when both are 0). Just an
// ordinary local slot (like every other parameter) - it holds an
// ENCODED REFERENCE, not the value itself, so every access must go
// through NODE_VAR_PARAM_READ/ASSIGN instead of the plain
// NODE_LOCAL_VAR/ASSIGN a by-value scalar parameter would use. See
// param_is_var/param_is_const/param_is_out's comments in common.h for
// the full design.
static int add_local_var_param(const char *name, DataType type, int is_subrange, int subrange_lower, int subrange_upper, int is_const, int is_out) {
    int idx = add_local(name, type);
    current_locals[idx].is_var_param = 1;
    current_locals[idx].is_const_param = is_const;
    current_locals[idx].is_out_param = is_out;
    current_locals[idx].is_subrange = is_subrange;
    current_locals[idx].subrange_lower = subrange_lower;
    current_locals[idx].subrange_upper = subrange_upper;
    return idx;
}

// Registers an inline procedural/functional parameter ('function f(n:
// integer): integer' or 'procedure f(...)' as one formal parameter).
// Just an ordinary local slot (like every other parameter) - it holds a
// runtime procedure entry address, not an ordinary scalar value, so
// every access must go through NODE_CALL_INDIRECT (a call) or a plain
// NODE_LOCAL_VAR read (forwarding to a further call) instead of any
// other expression form. type is TYPE_INTEGER - meaningless beyond
// "this slot holds one int", mirroring add_local_array_rec()'s own
// dummy-type comment.
static int add_local_proc_param(const char *name, int is_function, DataType return_type,
                                 int param_count, const DataType *param_types, const int *param_is_var) {
    int idx = add_local(name, TYPE_INTEGER);
    current_locals[idx].is_proc_param = 1;
    current_locals[idx].proc_param_is_function = is_function;
    current_locals[idx].proc_param_return_type = return_type;
    current_locals[idx].proc_param_param_count = param_count;
    for (int i = 0; i < param_count; i++) {
        current_locals[idx].proc_param_param_types[i] = param_types[i];
        current_locals[idx].proc_param_param_is_var[i] = param_is_var[i];
    }
    return idx;
}

// Registers a by-reference array parameter. Just an ordinary local slot
// (it holds a runtime sym_table[] index - see parse_array_ref_argument
// and generate_code's NODE_REF_ARRAY_ACCESS/ASSIGN cases), plus the
// declared bounds every call site's argument must exactly match.
static int add_local_array_ref(const char *name, DataType elem_type, int lower, int upper,
                                int is_2d, int lower2, int upper2,
                                int is_nd, int nd_dims, const int *nd_lower, const int *nd_upper,
                                int is_subrange, int subrange_lower, int subrange_upper) {
    int idx = add_local(name, elem_type);
    current_locals[idx].is_array_ref = 1;
    current_locals[idx].array_lower = lower;
    current_locals[idx].array_upper = upper;
    current_locals[idx].is_2d = is_2d;
    current_locals[idx].array_lower2 = lower2;
    current_locals[idx].array_upper2 = upper2;
    current_locals[idx].is_nd = is_nd;
    current_locals[idx].nd_dims = nd_dims;
    for (int d = 0; d < nd_dims; d++) {
        current_locals[idx].nd_lower[d] = nd_lower[d];
        current_locals[idx].nd_upper[d] = nd_upper[d];
    }
    current_locals[idx].is_subrange = is_subrange;
    current_locals[idx].subrange_lower = subrange_lower;
    current_locals[idx].subrange_upper = subrange_upper;
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
static int add_local_array(const char *name, DataType elem_type, int lower, int upper,
                            int is_2d, int lower2, int upper2,
                            int is_nd, int nd_dims, const int *nd_lower, const int *nd_upper,
                            int is_subrange, int subrange_lower, int subrange_upper) {
    if (find_local_in_current_scope(name) != -1 || local_record_name_collides(name)) {
        compile_error(token.line, "Duplicate parameter or local variable '%s'", name);
    }
    if (current_local_count >= MAX_LOCALS) {
        compile_error(token.line, "Too many parameters/local variables (limit is %d)", MAX_LOCALS);
    }
    char mangled[MAX_NAME];
    snprintf(mangled, MAX_NAME, "__local_arr%d", sym_count);
    int array_sym_idx = sym_count; // add_array_var()/add_array_var_2d()/add_array_var_nd() is about to append here
    if (is_nd) {
        add_array_var_nd(mangled, elem_type, nd_dims, nd_lower, nd_upper);
    } else if (is_2d) {
        add_array_var_2d(mangled, elem_type, lower, upper, lower2, upper2);
    } else {
        add_array_var(mangled, elem_type, lower, upper);
    }
    sym_table[array_sym_idx].is_subrange = is_subrange;
    sym_table[array_sym_idx].subrange_lower = subrange_lower;
    sym_table[array_sym_idx].subrange_upper = subrange_upper;

    strcpy(current_locals[current_local_count].name, name);
    current_locals[current_local_count].type = elem_type;
    current_locals[current_local_count].is_array = 1;
    current_locals[current_local_count].array_sym_idx = array_sym_idx;
    current_locals[current_local_count].is_array_ref = 0; // defensive reset (see comment above LocalSymbol) -
    current_locals[current_local_count].array_lower = 0;  // this slot may have held an array-ref PARAMETER
    current_locals[current_local_count].array_upper = 0;  // (or anything else) in a previously-parsed procedure
    current_locals[current_local_count].is_subrange = 0;  // meaningless for is_array - lives on the mangled
    current_locals[current_local_count].subrange_lower = 0; // global's own Symbol (sym_table[array_sym_idx]) instead
    current_locals[current_local_count].subrange_upper = 0;
    current_locals[current_local_count].is_static = 0;
    current_locals[current_local_count].static_sym_idx = 0;
    current_locals[current_local_count].is_var_param = 0; // defensive reset (see comment above LocalSymbol)
    current_locals[current_local_count].is_const_param = 0; // defensive reset (see comment above LocalSymbol)
    current_locals[current_local_count].is_out_param = 0; // defensive reset (see comment above LocalSymbol)
    current_locals[current_local_count].is_proc_param = 0; // defensive reset (see comment above LocalSymbol)
    return current_local_count++;
}

// Same trick as add_local_array() above - a procedure-local array of
// records is just another hidden, mangled GLOBAL array-of-records (see
// add_array_var_rec()), shared/persistent across every call exactly like
// a local array already is. Registers the sym_table[]<->record_type_idx
// mapping too, so factor()/statement()'s local-array read/write paths
// (which resolve to this same array_sym_idx) can look up field names via
// find_record_array_type() exactly as they would for a true global.
static int add_local_array_rec(const char *name, int record_type_idx, int lower, int upper) {
    if (find_local_in_current_scope(name) != -1 || local_record_name_collides(name)) {
        compile_error(token.line, "Duplicate parameter or local variable '%s'", name);
    }
    if (current_local_count >= MAX_LOCALS) {
        compile_error(token.line, "Too many parameters/local variables (limit is %d)", MAX_LOCALS);
    }
    char mangled[MAX_NAME];
    snprintf(mangled, MAX_NAME, "__local_arr%d", sym_count);
    int array_sym_idx = sym_count; // add_array_var_rec() is about to append here
    add_array_var_rec(mangled, record_type_idx, lower, upper);
    register_record_array(array_sym_idx, record_type_idx);

    strcpy(current_locals[current_local_count].name, name);
    current_locals[current_local_count].type = TYPE_INTEGER; // unused - see add_array_var_rec()'s own comment
    current_locals[current_local_count].is_array = 1;
    current_locals[current_local_count].array_sym_idx = array_sym_idx;
    current_locals[current_local_count].is_array_ref = 0; // defensive reset (see comment above LocalSymbol)
    current_locals[current_local_count].array_lower = 0;
    current_locals[current_local_count].array_upper = 0;
    current_locals[current_local_count].is_subrange = 0;
    current_locals[current_local_count].subrange_lower = 0;
    current_locals[current_local_count].subrange_upper = 0;
    current_locals[current_local_count].is_static = 0;
    current_locals[current_local_count].static_sym_idx = 0;
    current_locals[current_local_count].is_var_param = 0; // defensive reset (see comment above LocalSymbol)
    current_locals[current_local_count].is_const_param = 0; // defensive reset (see comment above LocalSymbol)
    current_locals[current_local_count].is_out_param = 0; // defensive reset (see comment above LocalSymbol)
    current_locals[current_local_count].is_proc_param = 0; // defensive reset (see comment above LocalSymbol)
    return current_local_count++;
}

// Registers a procedure-local STATIC scalar variable ('static count:
// integer;' inside a procedure body) - persists across calls, unlike an
// ordinary local (which ENTER zero-initializes fresh every call).
// Reuses the exact same trick add_local_array() above already uses for
// arrays (which are already implicitly persistent - see its own
// comment): a hidden, mangled GLOBAL, so two different procedures' own
// "count" don't collide, with every reference inside the procedure
// resolving to it exactly as if it were an ordinary global (see the
// is_static checks throughout this file). Array locals don't need this
// function at all - they're already global-backed by nature.
static int add_static_local(const char *proc_name, const char *name, DataType type,
                             int is_subrange, int subrange_lower, int subrange_upper) {
    if (find_local_in_current_scope(name) != -1 || local_record_name_collides(name)) {
        compile_error(token.line, "Duplicate parameter or local variable '%s'", name);
    }
    if (current_local_count >= MAX_LOCALS) {
        compile_error(token.line, "Too many parameters/local variables (limit is %d)", MAX_LOCALS);
    }
    // "__static_procname_name" - checked explicitly rather than letting
    // snprintf silently truncate, which could make two different
    // manglings collide (same reasoning as add_record_var()'s own check).
    if (strlen(proc_name) + 10 + strlen(name) >= MAX_NAME) {
        compile_error(token.line, "Static local variable name '%s' in procedure '%s' too long", name, proc_name);
    }
    char mangled[2 * MAX_NAME];
    snprintf(mangled, sizeof(mangled), "__static_%s_%s", proc_name, name);
    int sym_idx = sym_count; // add_var() is about to append here
    add_var(mangled, type);
    sym_table[sym_idx].is_subrange = is_subrange;
    sym_table[sym_idx].subrange_lower = subrange_lower;
    sym_table[sym_idx].subrange_upper = subrange_upper;

    strcpy(current_locals[current_local_count].name, name);
    current_locals[current_local_count].type = type;
    current_locals[current_local_count].is_static = 1;
    current_locals[current_local_count].static_sym_idx = sym_idx;
    return current_local_count++;
}

// Recursively creates one frame slot per LEAF field of record_type_idx,
// mangled under "prefix__" - used by add_local_record() below for a
// nested-record field's own subtree. The nested type is guaranteed
// array-field-free (enforced where the nested field was declared - see
// parse_type_section()), so this never encounters an is_array field and
// never needs add_local_record()'s own array rejection here.
static void add_local_record_fields(const char *prefix, int record_type_idx) {
    RecordTypeDef *rt = &record_types[record_type_idx];
    for (int i = 0; i < rt->field_count; i++) {
        RecordField *f = &rt->fields[i];
        char mangled[2 * MAX_NAME];
        if (strlen(prefix) + 2 + strlen(f->name) >= sizeof(mangled)) {
            compile_error(token.line, "Record variable/field name '%s.%s' too long", prefix, f->name);
        }
        snprintf(mangled, sizeof(mangled), "%s__%s", prefix, f->name);
        if (f->is_record) {
            add_local_record_fields(mangled, f->record_type_idx);
            continue;
        }
        int idx = add_local(mangled, f->type);
        current_locals[idx].is_subrange = f->is_subrange;
        current_locals[idx].subrange_lower = f->subrange_lower;
        current_locals[idx].subrange_upper = f->subrange_upper;
    }
}

// Registers a record LOCAL or PARAMETER ('var p: TPoint;' inside a
// procedure body, or 'procedure foo(p: TPoint)') - see the comment
// above LocalRecordVarDef for why each field gets its own ordinary
// frame slot (add_local()) instead of a hidden global. Used for BOTH
// locals and parameters identically; a parameter's fields additionally
// get populated by copy-in code at each call site (by value) - see
// parse_call_arguments()'s record-argument handling. A nested-record
// field instead expands, via add_local_record_fields() above, into N
// contiguous frame slots - field_local_idx[i] holds the FIRST of those
// (or the field's own single slot, for a scalar field - same
// convention, unified).
static void add_local_record(const char *name, int record_type_idx) {
    int existing_is_local, existing_record_type_idx;
    const int *existing_field_idx;
    if (find_local_in_current_scope(name) != -1 || find_any_record_var(name, &existing_is_local, &existing_record_type_idx, &existing_field_idx)) {
        compile_error(token.line, "Duplicate parameter or local variable '%s'", name);
    }
    if (local_record_var_count >= MAX_LOCAL_RECORD_VARS) {
        compile_error(token.line, "Too many local/parameter record variables (limit is %d)", MAX_LOCAL_RECORD_VARS);
    }
    RecordTypeDef *rt = &record_types[record_type_idx];
    LocalRecordVarDef *rv = &local_record_vars[local_record_var_count];
    strcpy(rv->name, name);
    rv->record_type_idx = record_type_idx;
    for (int i = 0; i < rt->field_count; i++) {
        RecordField *f = &rt->fields[i];
        if (f->is_array) {
            compile_error(token.line, "Record local/parameter '%s' has an array field '%s' - this compiler doesn't support that yet (see docs/LANGUAGE.md)",
                          name, f->name);
        }
        // "name__fieldname", purely for -v/ast_printer readability - see
        // add_local()/current_locals[]; unlike a global record's mangled
        // field, this name is never looked up again (field resolution
        // goes through LocalRecordVarDef.field_local_idx instead).
        char mangled[2 * MAX_NAME];
        if (strlen(name) + 2 + strlen(f->name) >= sizeof(mangled)) {
            compile_error(token.line, "Record variable/field name '%s.%s' too long", name, f->name);
        }
        snprintf(mangled, sizeof(mangled), "%s__%s", name, f->name);
        int base = current_local_count; // first frame slot about to be created for field i
        if (f->is_record) {
            add_local_record_fields(mangled, f->record_type_idx);
        } else {
            int idx = add_local(mangled, f->type);
            current_locals[idx].is_subrange = f->is_subrange;
            current_locals[idx].subrange_lower = f->subrange_lower;
            current_locals[idx].subrange_upper = f->subrange_upper;
        }
        rv->field_local_idx[i] = base;
    }
    local_record_var_count++;
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

// Called right after a record variable's name and the FIRST '.' are
// already consumed, with 'field_idx_array' the caller's
// RecordVarDef.field_sym_idx/LocalRecordVarDef.field_local_idx (whose
// values are "first leaf" bases - see add_record_var()/
// add_local_record()). Resolves the rest of a possibly-multi-level
// '.field.field...' chain down to a scalar leaf, following into
// nested-record fields as needed, and returns the leaf's final storage
// index (a sym_table[] index, or current_locals[] index - same
// convention field_idx_array itself uses). Below the top level there's
// no field_idx array to consult directly (only the top-level record
// variable has one), so descending adds the leaf-counts of every
// preceding sibling field at that level to the running index instead -
// valid because add_record_var_fields()/add_local_record_fields() lay
// out a nested field's leaves contiguously, in declaration order.
static int resolve_record_field_leaf(int record_type_idx, const int *field_idx_array, const char *rec_name) {
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Expected a field name after '%s.'", rec_name);
    }
    int field_idx = find_record_field(record_type_idx, token.text);
    if (field_idx == -1) {
        compile_error(token.line, "'%s' is not a field of '%s'", token.text, rec_name);
    }
    // Sized for the deepest possible nesting chain (bounded by
    // MAX_RECORD_TYPES - a type can only nest an already-declared type,
    // so a chain can never revisit a type and never exceeds that many
    // segments) - this buffer is only ever used to compose an error
    // message, snprintf'd defensively regardless.
    char field_path[MAX_RECORD_TYPES * (MAX_NAME + 1)];
    snprintf(field_path, sizeof(field_path), "%s", token.text);
    match(TOKEN_IDENTIFIER);

    int idx = field_idx_array[field_idx];
    RecordTypeDef *rt = &record_types[record_type_idx];
    RecordField *f = &rt->fields[field_idx];
    while (f->is_record) {
        if (token.type != TOKEN_PERIOD) {
            compile_error(token.line, "'%s.%s' names a whole record - specify a further field ('%s.%s.fieldname'), compare it with '=' or '<>', or use whole-record assignment",
                          rec_name, field_path, rec_name, field_path);
        }
        match(TOKEN_PERIOD);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "Expected a field name after '%s.%s.'", rec_name, field_path);
        }
        RecordTypeDef *srt = &record_types[f->record_type_idx];
        int sub_field_idx = find_record_field(f->record_type_idx, token.text);
        if (sub_field_idx == -1) {
            compile_error(token.line, "'%s' is not a field of '%s'", token.text, srt->name);
        }
        for (int k = 0; k < sub_field_idx; k++) {
            idx += srt->fields[k].is_record
                ? record_type_leaf_count(srt->fields[k].record_type_idx)
                : 1;
        }
        size_t used = strlen(field_path);
        snprintf(field_path + used, sizeof(field_path) - used, ".%s", token.text);
        match(TOKEN_IDENTIFIER);
        rt = srt;
        f = &srt->fields[sub_field_idx];
    }
    return idx;
}

// Extends try_get_array_bounds() to also accept 'record.arrayField'
// syntax at the current token - a record field is just an ordinary
// mangled global array by the time it's resolved (see the RecordTypeDef
// comment), so once the field is found, its bounds come from sym_table[]
// exactly like any other array. Consumes the identifier (and '.field',
// if that path is taken) only when committing to one of these two
// interpretations; returns 0 without consuming anything if the name is
// neither an array nor a record variable, so callers needing that
// (length()'s fallback to a general expression, for length(s) on a
// string) still work correctly.
static int try_get_array_bounds_here(int *lower, int *upper) {
    if (token.type != TOKEN_IDENTIFIER) return 0;
    if (try_get_array_bounds(token.text, lower, upper)) {
        match(TOKEN_IDENTIFIER);
        return 1;
    }
    {
        int with_field_idx = find_with_field(token.text);
        if (with_field_idx != -1) {
            if (!sym_table[with_field_idx].is_array) return 0; // not an array - let the caller fall back to a general expression
            if (sym_table[with_field_idx].is_2d) {
                compile_error(token.line, "'%s' is a 2D array - low/high/length don't support 2D arrays yet", token.text);
            }
            match(TOKEN_IDENTIFIER);
            *lower = sym_table[with_field_idx].array_lower;
            *upper = sym_table[with_field_idx].array_upper;
            return 1;
        }
    }
    int rv_idx = find_record_var(token.text);
    if (rv_idx == -1) return 0;
    char rec_name[MAX_NAME];
    strcpy(rec_name, token.text);
    match(TOKEN_IDENTIFIER);
    if (token.type != TOKEN_PERIOD) {
        compile_error(token.line, "'%s' is a record variable and can't be used directly here - access a field with '%s.fieldname'", rec_name, rec_name);
    }
    match(TOKEN_PERIOD);
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Expected a field name after '%s.'", rec_name);
    }
    char field_name[MAX_NAME];
    strcpy(field_name, token.text);
    RecordVarDef *rv = &record_vars[rv_idx];
    int field_idx = find_record_field(rv->record_type_idx, field_name);
    if (field_idx == -1) {
        compile_error(token.line, "'%s' is not a field of '%s'", field_name, rec_name);
    }
    int field_sym = rv->field_sym_idx[field_idx];
    match(TOKEN_IDENTIFIER);
    if (!sym_table[field_sym].is_array) {
        compile_error(token.line, "'%s.%s' is not an array", rec_name, field_name);
    }
    if (sym_table[field_sym].is_2d) {
        compile_error(token.line, "'%s.%s' is a 2D array - low/high/length don't support 2D arrays yet", rec_name, field_name);
    }
    *lower = sym_table[field_sym].array_lower;
    *upper = sym_table[field_sym].array_upper;
    return 1;
}

// Parses a compile-time-constant integer literal, e.g. for array bounds
// (array[1..10], array[-5..5], array[1..MaxSize]). Not a general
// expression - array sizes must be known at compile time in this
// language - but a reference to an integer 'const' is accepted here too
// (it's just as compile-time-known as a literal), since this is the one
// centralized function every array-bound call site already goes through.
static int parse_int_literal(void) {
    int sign = 1;
    if (token.type == TOKEN_MINUS) {
        sign = -1;
        match(TOKEN_MINUS);
    }
    if (token.type == TOKEN_IDENTIFIER) {
        int const_idx = find_const(token.text);
        if (const_idx == -1 || const_defs[const_idx].type != TYPE_INTEGER) {
            compile_error(token.line, "Expected an integer literal or an integer constant");
        }
        int val = const_defs[const_idx].value * sign;
        match(TOKEN_IDENTIFIER);
        return val;
    }
    if (token.type != TOKEN_NUMBER) {
        compile_error(token.line, "Expected an integer literal or an integer constant");
    }
    int val = token.value * sign;
    match(TOKEN_NUMBER);
    return val;
}

// Parses 'lo1..hi1 {, lo2..hi2}' - an array declaration's bound list, any
// dimension count (1, 2, or more) - shared by every site that declares
// array bounds (the global var section, and parse_name_group() for
// parameters/locals). Does NOT consume the surrounding '[' ']' - callers
// still do that themselves, exactly as before this helper existed; this
// only replaces each site's own inner bound-parsing loop. Returns the
// dimension count and fills lower[]/upper[] (each sized MAX_ARRAY_DIMS)
// with the parsed, already-validated (lower <= upper) bounds, indices
// 0..dims-1. Callers decide what to DO with a given dimension count (1
// and 2 still go through add_array_var()/add_array_var_2d() and their
// own dedicated Symbol fields, completely unchanged; 3+ goes through the
// new add_array_var_nd() and nd_lower[]/nd_upper[]) - this function only
// parses, it doesn't know or care which mechanism the result feeds.
static int parse_array_bounds(int *lower, int *upper) {
    int dims = 0;
    while (1) {
        if (dims >= MAX_ARRAY_DIMS) {
            compile_error(token.line, "Array has too many dimensions (limit is %d)", MAX_ARRAY_DIMS);
        }
        lower[dims] = parse_int_literal();
        match(TOKEN_DOTDOT);
        upper[dims] = parse_int_literal();
        if (upper[dims] < lower[dims]) {
            compile_error(token.line, "Invalid array bounds: upper (%d) must be >= lower (%d)", upper[dims], lower[dims]);
        }
        dims++;
        if (token.type == TOKEN_COMMA) {
            match(TOKEN_COMMA);
            continue;
        }
        break;
    }
    return dims;
}

// Side-channel output of parse_scalar_type() below: whether the type it
// JUST resolved was a subrange type, and if so, its bounds. A subrange
// resolves to plain TYPE_INTEGER as parse_scalar_type()'s actual return
// value (see SubrangeTypeDef above for why), so this is the only way a
// caller that cares (var/array-element/record-field/parameter/local/
// return-type declarations) can find out to also apply the bounds
// constraint - callers that don't care about subranges can simply
// ignore it. Reset at the top of every parse_scalar_type() call, so it
// always reflects only the most recent call.
static int scalar_type_is_subrange = 0;
static int scalar_type_subrange_lower = 0;
static int scalar_type_subrange_upper = 0;

// A second, independent side-channel output of parse_scalar_type():
// TOKEN_BYTE/TOKEN_SHORTINT/TOKEN_WORD if the type just resolved was
// literally one of those three keywords, else 0 (ordinary 4-byte
// width). Deliberately NOT derived from scalar_type_is_subrange/bounds -
// an ordinary hand-written 'type TByte = 0..255;' subrange must NOT be
// treated as narrower on disk just because its bounds happen to match
// 'byte''s own range, or an already-working typed-file program using
// such a subrange would silently change its on-disk format the moment
// this feature shipped. Only a literal 'byte'/'shortint'/'word' token
// sets this - see build_typed_file_write_chain()/build_typed_file_read_
// chain() and TypedFileVarDef.disk_width, the only things that read it.
static TokenType scalar_type_disk_width = 0;

// Parses the '<base-type>' after 'set of' and validates it, WITHOUT
// keeping the bounds around afterward - nothing downstream needs them
// (see TYPE_SET's comment in common.h for why). Accepts an inline
// subrange ('0..9'), 'boolean' (2 values), a previously-declared
// enumerated type name, or a previously-declared subrange type name/
// alias - and rejects anything whose range exceeds MAX_SET_BITS distinct
// values (integer/char/string/real, or an enum/subrange too wide to fit
// in one bitmask).
static void parse_set_base_type(void) {
    int lower, upper;
    if (token.type == TOKEN_NUMBER || token.type == TOKEN_MINUS) {
        lower = parse_int_literal();
        match(TOKEN_DOTDOT);
        upper = parse_int_literal();
        if (upper < lower) {
            compile_error(token.line, "Invalid set base range: upper (%d) must be >= lower (%d)", upper, lower);
        }
    } else if (token.type == TOKEN_BOOLEAN) {
        match(TOKEN_BOOLEAN);
        lower = 0;
        upper = 1;
    } else if (token.type == TOKEN_IDENTIFIER && find_enum_type(token.text) != -1) {
        int enum_type_idx = find_enum_type(token.text);
        match(TOKEN_IDENTIFIER);
        lower = 0;
        upper = enum_types[enum_type_idx].value_count - 1;
    } else if (token.type == TOKEN_IDENTIFIER && find_subrange_type(token.text) != -1) {
        int subrange_idx = find_subrange_type(token.text);
        match(TOKEN_IDENTIFIER);
        lower = subrange_types[subrange_idx].lower;
        upper = subrange_types[subrange_idx].upper;
    } else {
        compile_error(token.line, "Expected a set base type: an inline range ('0..9'), 'boolean', an enumerated type, or a subrange type (integer/char/string/real have too many, or too few, defined values to be one)");
        return; // unreachable
    }
    if (lower < 0) {
        compile_error(token.line, "A set base type's range can't include a negative value (%d)", lower);
    }
    if (upper - lower + 1 > MAX_SET_BITS) {
        compile_error(token.line, "Set base range is too large (%d distinct values; limit is %d)", upper - lower + 1, MAX_SET_BITS);
    }
}

// True if the CURRENT token would be accepted by parse_scalar_type()
// below as a scalar type name, WITHOUT consuming it or erroring -
// mirrors that function's own resolution order as a pure boolean
// pre-check (TOKEN_SET/TOKEN_TEXT_TYPE/TOKEN_FILE_TYPE excluded: none
// makes sense as a bare type-name argument on its own - 'set' alone
// isn't a complete type without 'of ...', and the file keywords are
// things parse_scalar_type() itself rejects with a dedicated error).
// Used by sizeOf(x) to decide "is x a type name" before falling back
// to resolving it as a variable instead - factor()/expression() have
// no other existing hook for "identifier that names a type."
static int token_is_scalar_type_name(void) {
    if (token.type == TOKEN_INTEGER || token.type == TOKEN_BOOLEAN || token.type == TOKEN_STRING_TYPE
        || token.type == TOKEN_CHAR_TYPE || token.type == TOKEN_REAL_TYPE
        || token.type == TOKEN_BYTE || token.type == TOKEN_SHORTINT || token.type == TOKEN_WORD) {
        return 1;
    }
    if (token.type == TOKEN_IDENTIFIER) {
        return find_type_alias(token.text) != -1
            || find_enum_type(token.text) != -1
            || find_subrange_type(token.text) != -1
            || find_pointer_type(token.text) != -1
            || find_proc_type(token.text) != -1;
    }
    return 0;
}

// A scalar type - one of the eight built-in keywords (integer, boolean,
// string, char, real, byte, shortint, word), a previously declared type
// alias, enumerated type, subrange type, or 'set of ...' resolving to
// one of them (see TypeAliasDef/EnumTypeDef/SubrangeTypeDef/TYPE_SET
// above). This is the one centralized function every scalar-type call
// site goes through (parameters, procedure-locals, record fields,
// function return types, and plain/array var declarations), so alias/
// enum/subrange/set support and any future scalar-type keyword only
// needs to be added here once. byte/shortint/word resolve to plain
// TYPE_INTEGER, exactly like a named subrange type alias does (see
// scalar_type_is_subrange above) - fully interchangeable with integer
// everywhere except on-disk width in a typed file (see
// scalar_type_disk_width above and RecordField.disk_width/
// TypedFileVarDef.disk_width).
static DataType parse_scalar_type(void) {
    scalar_type_is_subrange = 0;
    scalar_type_disk_width = 0;
    if (token.type == TOKEN_INTEGER) { match(TOKEN_INTEGER); return TYPE_INTEGER; }
    if (token.type == TOKEN_BOOLEAN) { match(TOKEN_BOOLEAN); return TYPE_BOOLEAN; }
    if (token.type == TOKEN_STRING_TYPE) { match(TOKEN_STRING_TYPE); return TYPE_STRING; }
    if (token.type == TOKEN_CHAR_TYPE) { match(TOKEN_CHAR_TYPE); return TYPE_CHAR; }
    if (token.type == TOKEN_REAL_TYPE) { match(TOKEN_REAL_TYPE); return TYPE_REAL; }
    if (token.type == TOKEN_BYTE) {
        match(TOKEN_BYTE);
        scalar_type_is_subrange = 1;
        scalar_type_subrange_lower = 0;
        scalar_type_subrange_upper = 255;
        scalar_type_disk_width = TOKEN_BYTE;
        return TYPE_INTEGER;
    }
    if (token.type == TOKEN_SHORTINT) {
        match(TOKEN_SHORTINT);
        scalar_type_is_subrange = 1;
        scalar_type_subrange_lower = -128;
        scalar_type_subrange_upper = 127;
        scalar_type_disk_width = TOKEN_SHORTINT;
        return TYPE_INTEGER;
    }
    if (token.type == TOKEN_WORD) {
        match(TOKEN_WORD);
        scalar_type_is_subrange = 1;
        scalar_type_subrange_lower = 0;
        scalar_type_subrange_upper = 65535;
        scalar_type_disk_width = TOKEN_WORD;
        return TYPE_INTEGER;
    }
    if (token.type == TOKEN_SET) {
        match(TOKEN_SET);
        match(TOKEN_OF);
        parse_set_base_type();
        return TYPE_SET;
    }
    if (token.type == TOKEN_ARRAY) {
        // 'array of ElementType' (no '[lo..hi]') - a DYNAMIC array (see
        // TYPE_DYNARRAY_BASE in common.h). The two call sites that parse
        // 'array[...] of T' directly (parse_var_section() and the
        // param/local shared group-parser) each peek for '[' BEFORE
        // reaching this function at all, so this branch only ever fires
        // once 'of' is confirmed to follow 'array' with no bracket in
        // between.
        match(TOKEN_ARRAY);
        return parse_dynarray_of();
    }
    if (token.type == TOKEN_TEXT_TYPE) {
        // Deliberately NOT '{ match(TOKEN_TEXT_TYPE); return TYPE_FILE; }'
        // like every other branch here - a file variable can only be a
        // GLOBAL, declared directly in the main program's own 'var'
        // section (see its own dedicated parsing there), never a
        // parameter/local/record-field/array-element type, precisely
        // because this is the one centralized function every one of
        // those OTHER contexts goes through. Recognizing the keyword
        // here (rather than just letting it fall through to the generic
        // "Unknown type" error below) exists purely for a clearer,
        // specific error message.
        compile_error(token.line, "'text' can only declare a global file variable in the main program's own 'var' section - not as a parameter, local, record field, or array element type (see docs/LANGUAGE.md)");
        return TYPE_UNKNOWN; // unreachable
    }
    if (token.type == TOKEN_FILE_TYPE) {
        // Same restriction, same reasoning, as TOKEN_TEXT_TYPE just
        // above - a typed OR untyped file variable can only be global
        // too (this fires before ever peeking for 'of', so it covers
        // both 'file of T' and bare 'file' uniformly).
        compile_error(token.line, "'file'/'file of ...' can only declare a global file variable in the main program's own 'var' section - not as a parameter, local, record field, or array element type (see docs/LANGUAGE.md)");
        return TYPE_UNKNOWN; // unreachable
    }
    if (token.type == TOKEN_POINTER_TYPE) {
        // Unlike TOKEN_TEXT_TYPE/TOKEN_FILE_TYPE above, 'Pointer' has NO
        // global-only restriction - its runtime state is just a plain
        // int (see TYPE_UNTYPED_POINTER in common.h), not an external
        // resource, so it's an ordinary type usable as a parameter/
        // local/field/array-element, exactly like a named '^Target'
        // pointer type already is.
        match(TOKEN_POINTER_TYPE);
        return TYPE_UNTYPED_POINTER;
    }
    if (token.type == TOKEN_IDENTIFIER) {
        int alias_idx = find_type_alias(token.text);
        if (alias_idx != -1) {
            DataType t = type_aliases[alias_idx].type;
            match(TOKEN_IDENTIFIER);
            return t;
        }
        int enum_type_idx = find_enum_type(token.text);
        if (enum_type_idx != -1) {
            match(TOKEN_IDENTIFIER);
            return (DataType)(TYPE_ENUM_BASE + enum_type_idx);
        }
        int subrange_idx = find_subrange_type(token.text);
        if (subrange_idx != -1) {
            scalar_type_is_subrange = 1;
            scalar_type_subrange_lower = subrange_types[subrange_idx].lower;
            scalar_type_subrange_upper = subrange_types[subrange_idx].upper;
            match(TOKEN_IDENTIFIER);
            return TYPE_INTEGER;
        }
        int pointer_type_idx = find_pointer_type(token.text);
        if (pointer_type_idx != -1) {
            match(TOKEN_IDENTIFIER);
            return (DataType)(TYPE_POINTER_BASE + pointer_type_idx);
        }
        int proc_type_idx = find_proc_type(token.text);
        if (proc_type_idx != -1) {
            match(TOKEN_IDENTIFIER);
            return (DataType)(TYPE_PROC_BASE + proc_type_idx);
        }
    }
    compile_error(token.line, "Unknown type (expected 'integer', 'boolean', 'string', 'char', 'real', 'set of ...', a declared type alias, an enumerated type, a subrange type, a pointer type, or a procedural type)");
    return TYPE_UNKNOWN;
}

static DataType parse_dynarray_of(void) {
    match(TOKEN_OF);
    // Restricted to the built-in primitive keywords only - no named type
    // (alias/subrange/enum/pointer/procedural), record, or nested
    // 'array of ...'/'array[...] of ...' element type yet (see
    // docs/LANGUAGE.md#dynamic-arrays). byte/shortint/word ARE included
    // (unlike typed constants' own primitive-only cut) since
    // parse_scalar_type() already resolves their subrange bounds as a
    // side effect below - reusing that costs nothing extra here.
    if (token.type != TOKEN_INTEGER && token.type != TOKEN_REAL_TYPE
        && token.type != TOKEN_CHAR_TYPE && token.type != TOKEN_BOOLEAN
        && token.type != TOKEN_STRING_TYPE && token.type != TOKEN_BYTE
        && token.type != TOKEN_SHORTINT && token.type != TOKEN_WORD) {
        compile_error(token.line, "Dynamic array element type must be 'integer', 'real', 'char', 'boolean', 'string', 'byte', 'shortint', or 'word' - a named type, record, or nested array isn't supported yet (see docs/LANGUAGE.md)");
    }
    DataType elem_type = parse_scalar_type();
    int idx = find_or_add_dynarray_type(elem_type, scalar_type_is_subrange, scalar_type_subrange_lower, scalar_type_subrange_upper);
    // This function's own scalar_type_is_subrange/etc. globals must NOT
    // leak the ELEMENT's subrange-ness out to whatever CALLER asked for
    // this dynamic array's own type - a dynamic array variable itself is
    // never subrange-checked (only its individual elements are, via
    // NODE_DYNARRAY_ASSIGN - see codegen.c).
    scalar_type_is_subrange = 0;
    return (DataType)(TYPE_DYNARRAY_BASE + idx);
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
    int is_2d;             // 1 if this is a 2D array (a second
                          // 'lo2..hi2' dimension was given)
    int array_lower2;
    int array_upper2;
    int is_nd;             // 1 if this array has 3 OR MORE dimensions -
                          // mutually exclusive with is_2d. See Symbol.
                          // is_nd in common.h for why every dimension's
                          // bounds live uniformly in nd_lower/nd_upper
                          // below instead of reusing array_lower/
                          // array_upper for dimension 1.
    int nd_dims;           // only meaningful if is_nd
    int nd_lower[MAX_ARRAY_DIMS]; // only meaningful if is_nd, indices 0..nd_dims-1
    int nd_upper[MAX_ARRAY_DIMS];
    int is_subrange;      // see the Symbol comment in common.h
    int subrange_lower;
    int subrange_upper;
    TokenType disk_width; // see RecordField.disk_width's own comment -
                          // only meaningful for a record field's on-disk
                          // typed-file width; harmless/unused for every
                          // other NameGroup consumer (var/parameter/local
                          // declarations).
    int is_record;         // 1 if this group's type is a record type
                          // name (mutually exclusive with is_array -
                          // records as array elements aren't supported
                          // yet, so parse_scalar_type() below still
                          // rejects a record type name in that position)
    int record_type_idx;  // only meaningful if is_record
    int is_array_of_record; // 1 if this is an array whose ELEMENT type is
                          // a record type name - mutually exclusive with
                          // is_record, but is_array is ALSO set (see
                          // below): this is still an array, in every
                          // sense parameter/local declaration dispatch
                          // (statement()/subroutine_declaration()) needs
                          // to tell apart from a whole-record group.
    int array_record_type_idx; // only meaningful if is_array_of_record
} NameGroup;

static NameGroup parse_name_group(void) {
    NameGroup g;
    g.count = 0;
    g.type = TYPE_INTEGER; // defensive default (see the Symbol comment in common.h) - only meaningful if !is_record
    g.is_array = 0;
    g.is_2d = 0;
    g.is_nd = 0;
    g.nd_dims = 0; // defensive reset - only the is_nd (3+D array) branch
                   // below ever sets this otherwise, but subroutine_
                   // declaration()'s param-copy loop reads g.nd_dims
                   // UNCONDITIONALLY (not gated on is_nd), so it must
                   // never be left as uninitialized stack garbage for a
                   // scalar/1D/2D parameter - a real, pre-existing bug
                   // this surfaced, not specific to any one feature.
    g.is_record = 0;
    g.record_type_idx = 0;
    g.is_array_of_record = 0;
    g.array_record_type_idx = 0;
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
        if (token.type != TOKEN_LBRACKET) {
            // No '[lo..hi]' - a DYNAMIC array parameter or local (see
            // parse_dynarray_of()). Needs NO new machinery here at all:
            // it's just an ordinary (or 'var') SCALAR, since its whole
            // runtime value is one int (a heap pointer - see
            // TYPE_DYNARRAY_BASE) - g.is_array stays 0 (its own struct
            // default), so this group falls through to the exact same
            // plain-scalar path every other non-array type already takes.
            g.type = parse_dynarray_of();
            g.is_subrange = scalar_type_is_subrange;
            g.subrange_lower = scalar_type_subrange_lower;
            g.subrange_upper = scalar_type_subrange_upper;
            g.disk_width = scalar_type_disk_width;
            return g;
        }
        match(TOKEN_LBRACKET);
        int lower[MAX_ARRAY_DIMS], upper[MAX_ARRAY_DIMS];
        int dims = parse_array_bounds(lower, upper);
        match(TOKEN_RBRACKET);
        match(TOKEN_OF);
        if (dims >= 3) {
            // is_nd arrays keep array_lower/array_upper at 0 (unused) -
            // every dimension's bounds live uniformly in nd_lower/
            // nd_upper instead. Must match add_array_var_nd()'s exact
            // same zeroing, since array_shapes_match() below compares
            // array_lower/array_upper unconditionally (not just when
            // !is_nd) when validating a call-site argument.
            g.array_lower = 0;
            g.array_upper = 0;
            g.is_nd = 1;
            g.nd_dims = dims;
            for (int d = 0; d < dims; d++) {
                g.nd_lower[d] = lower[d];
                g.nd_upper[d] = upper[d];
            }
        } else {
            g.array_lower = lower[0];
            g.array_upper = upper[0];
            if (dims == 2) {
                g.is_2d = 1;
                g.array_lower2 = lower[1];
                g.array_upper2 = upper[1];
            }
        }
        if (token.type == TOKEN_IDENTIFIER && find_record_type(token.text) != -1) {
            if (dims != 1) {
                compile_error(token.line, "Arrays of records are only supported for 1D arrays right now (see docs/LANGUAGE.md)");
            }
            g.array_record_type_idx = find_record_type(token.text);
            g.is_array_of_record = 1;
            match(TOKEN_IDENTIFIER);
        } else {
            g.type = parse_scalar_type();
        }
        g.is_array = 1;
    } else if (token.type == TOKEN_IDENTIFIER && find_record_type(token.text) != -1) {
        g.record_type_idx = find_record_type(token.text);
        g.is_record = 1;
        match(TOKEN_IDENTIFIER);
    } else {
        g.type = parse_scalar_type();
    }
    g.is_subrange = scalar_type_is_subrange;
    g.subrange_lower = scalar_type_subrange_lower;
    g.subrange_upper = scalar_type_subrange_upper;
    g.disk_width = scalar_type_disk_width;
    return g;
}

// Parses the '(params) [: returntype]' TAIL of a procedure/function
// signature - h->is_function must already be set (by whichever caller
// consumed the leading 'function'/'procedure' keyword and, if it has
// one of its own, a name); h->name is left untouched here. Shared by
// parse_proc_param_header() below (a functional/procedural PARAMETER's
// own inline header, e.g. 'function f(n: integer): integer' as ONE
// formal parameter - has a name of its own, 'f') and
// parse_type_section()'s TOKEN_PROCEDURE/TOKEN_FUNCTION branch (a NAMED
// procedural TYPE, e.g. 'type TProc = procedure(x: integer);' - has no
// name of its own; the type's own name, already consumed before '=',
// is unrelated to any parameter name here). Scalar-only signature for
// now (by-value, 'var', and - when allow_const_out permits, see below -
// 'const'/'out') - no array/record parameters (parse_scalar_
// type() already rejects both with a clear "Unknown type" error,
// needing no extra guard here), and no subrange types either (there's
// nowhere to stash per-parameter subrange bounds in the ProcSymbol/
// LocalSymbol arrays this feature added - a documented, narrow v1 gap,
// not a silent one).
//
// allow_const_out: 'const'/'out' are only meaningful on a genuine
// procedure/function/class-method declaration, where a real body gets
// compiled against them (write-guards, the out-unassigned warning) -
// not inside a procedural/functional parameter's own inline signature
// or a named procedural type, which never have a body of their own and
// would need their own param_is_const[]/param_is_out[] arrays threaded
// through entirely separate structs for comparatively little value (a
// deliberate, documented v1 scope cut - see docs/ROADMAP.md's
// const/out entry). Callers pass 1 only for a class method header.
static ASTNode *expression(void); // forward-declared early - needed
                                   // right below to parse a default
                                   // parameter value's expression
                                   // ('= <const-expr>'); the ordinary
                                   // forward declaration further down
                                   // this file is unreachable from here.
static void parse_proc_signature_tail(ProcParamHeader *h, int allow_const_out) {
    h->param_count = 0;
    int seen_default = 0; // once any parameter has a default, every
                           // parameter after it (across all remaining
                           // groups) must also have one - see
                           // docs/LANGUAGE.md.
    if (token.type == TOKEN_LPAREN) {
        match(TOKEN_LPAREN);
        if (token.type != TOKEN_RPAREN) {
            while (1) {
                int is_var = 0, is_const = 0, is_out = 0;
                if (token.type == TOKEN_VAR) { is_var = 1; match(TOKEN_VAR); }
                else if (token.type == TOKEN_CONST) {
                    if (!allow_const_out) {
                        compile_error(token.line, "'const' parameters aren't supported here yet - only on procedure/function/method declarations (see docs/LANGUAGE.md)");
                    }
                    is_var = 1; is_const = 1; match(TOKEN_CONST);
                } else if (token.type == TOKEN_OUT) {
                    if (!allow_const_out) {
                        compile_error(token.line, "'out' parameters aren't supported here yet - only on procedure/function/method declarations (see docs/LANGUAGE.md)");
                    }
                    is_var = 1; is_out = 1; match(TOKEN_OUT);
                }
                char names[MAX_GROUP_NAMES][MAX_NAME];
                int name_count = 0;
                while (1) {
                    if (token.type != TOKEN_IDENTIFIER) {
                        compile_error(token.line, "Expected a parameter name");
                    }
                    if (name_count >= MAX_GROUP_NAMES) {
                        compile_error(token.line, "Too many parameter names in one group (limit is %d)", MAX_GROUP_NAMES);
                    }
                    strcpy(names[name_count++], token.text);
                    match(TOKEN_IDENTIFIER);
                    if (token.type == TOKEN_COMMA) { match(TOKEN_COMMA); continue; }
                    break;
                }
                match(TOKEN_COLON);
                DataType t = parse_scalar_type();
                if (scalar_type_is_subrange) {
                    compile_error(token.line, "A subrange type isn't supported inside a procedure/function signature yet - use its base type 'integer' (see docs/LANGUAGE.md)");
                }
                int has_default = 0, default_value = 0;
                DataType default_type = TYPE_UNKNOWN;
                if (token.type == TOKEN_EQ) {
                    if (!allow_const_out) {
                        compile_error(token.line, "default parameter values aren't supported here yet - only on procedure/function/method declarations (see docs/LANGUAGE.md)");
                    }
                    if (is_var) {
                        compile_error(token.line, "'var'/'const'/'out' parameters cannot have default values (see docs/LANGUAGE.md)");
                    }
                    if (name_count != 1) {
                        compile_error(token.line, "a default value can only be given for a single parameter, not a shared 'name, name: type' group (see docs/LANGUAGE.md)");
                    }
                    match(TOKEN_EQ);
                    int default_line = token.line;
                    ASTNode *value = expression();
                    type_check(value);
                    value = optimize_ast(value);
                    if (value->type != NODE_NUMBER && value->type != NODE_REAL_NUMBER
                        && value->type != NODE_BOOLEAN && value->type != NODE_STRING) {
                        compile_error(default_line, "default value for parameter '%s' is not a compile-time constant expression", names[0]);
                    }
                    if (t == TYPE_REAL && value->expression_type == TYPE_INTEGER) {
                        value->expression_type = TYPE_REAL;
                        value->data.num_value = float_to_bits((float)value->data.num_value);
                    } else if (!((t == TYPE_STRING || t == TYPE_CHAR) && (value->expression_type == TYPE_STRING || value->expression_type == TYPE_CHAR))
                               && value->expression_type != t) {
                        compile_error(default_line, "default value for parameter '%s' doesn't match its declared type", names[0]);
                    }
                    has_default = 1;
                    default_type = value->expression_type;
                    default_value = (value->type == NODE_STRING) ? value->data.var_idx : value->data.num_value;
                    seen_default = 1;
                } else if (seen_default) {
                    compile_error(token.line, "parameter '%s' must have a default value - once one parameter has a default, every parameter after it must too (see docs/LANGUAGE.md)", names[0]);
                }
                for (int i = 0; i < name_count; i++) {
                    if (h->param_count >= MAX_PARAMS) {
                        compile_error(token.line, "Too many parameters (limit is %d)", MAX_PARAMS);
                    }
                    h->param_types[h->param_count] = t;
                    h->param_is_var[h->param_count] = is_var;
                    h->param_is_const[h->param_count] = is_const;
                    h->param_is_out[h->param_count] = is_out;
                    h->param_has_default[h->param_count] = has_default;
                    h->param_default_type[h->param_count] = default_type;
                    h->param_default_value[h->param_count] = default_value;
                    strcpy(h->param_names[h->param_count], names[i]);
                    h->param_count++;
                }
                if (token.type == TOKEN_SEMI) { match(TOKEN_SEMI); continue; }
                break;
            }
        }
        match(TOKEN_RPAREN);
    }

    if (h->is_function) {
        match(TOKEN_COLON);
        h->return_type = parse_scalar_type();
        if (scalar_type_is_subrange) {
            compile_error(token.line, "A subrange return type isn't supported here yet - use its base type 'integer' (see docs/LANGUAGE.md)");
        }
    } else {
        h->return_type = TYPE_UNKNOWN;
    }
}

// Parses one inline procedural/functional parameter header, starting at
// the 'function'/'procedure' keyword (e.g. 'function f(n: integer):
// integer' as ONE formal parameter, unlike a NameGroup which can list
// several names sharing one type) - see parse_proc_signature_tail()
// above for everything after the header's own name (including
// allow_const_out's meaning).
static ProcParamHeader parse_proc_param_header(int allow_const_out) {
    ProcParamHeader h;
    h.is_function = (token.type == TOKEN_FUNCTION);
    match(h.is_function ? TOKEN_FUNCTION : TOKEN_PROCEDURE);
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Expected a parameter name after '%s'", h.is_function ? "function" : "procedure");
    }
    strcpy(h.name, token.text);
    match(TOKEN_IDENTIFIER);
    parse_proc_signature_tail(&h, allow_const_out);
    return h;
}

static ASTNode *expression(void);
static ASTNode *statement(void);
static ASTNode *statement_list(void);
static ASTNode *compound_statement(void);
static ASTNode *parse_call_arguments(int proc_idx);
static void subroutine_declaration(int is_function_decl, int header_only, int is_destructor_decl);
static ASTNode *parse_case_label_value(void);
static void parse_record_field_group(RecordTypeDef *rt, int record_type_idx, int is_private, int is_protected, int declaring_class_ptr_idx);
static void parse_record_variant_part(RecordTypeDef *rt, int record_type_idx);
static void parse_class_declaration(const char *class_name, int line);
static void parse_class_method_body(int is_function_decl, const char *class_name, int decl_line, int is_destructor_decl);
static int register_abstract_method_signature(ProcParamHeader *h, int class_ptr_idx);

// Parses 'idx1, idx2, ..., idxN]' (already past the opening '[') for an
// N-dimensional (N=dims, always 3 or more - 1D/2D have their own
// dedicated index parsing) array access/assignment. Chains each index
// expression via its own ->next - NOT the caller's access/assignment
// node's ->next, which stays free for whatever that node's own context
// needs (statement-list chaining, for an assignment) - the same
// sibling-chain technique NODE_WRITELN's argument list and NODE_SET_
// CONSTRUCTOR's element list already use, needed here because ASTNode
// has only 4 child pointers and an N-dimensional access already needs
// its symbol/local index (in data.var_idx) plus, for an assignment, a
// value expression too - no room left for N more named index fields.
// Consumes the closing ']' itself, so the caller doesn't need to.
static ASTNode *parse_nd_index_list(int dims) {
    ASTNode *head = NULL;
    ASTNode *tail = NULL;
    for (int d = 0; d < dims; d++) {
        ASTNode *idx_node = expression();
        if (!head) head = idx_node; else tail->next = idx_node;
        tail = idx_node;
        if (d < dims - 1) {
            match(TOKEN_COMMA);
        }
    }
    match(TOKEN_RBRACKET);
    return head;
}

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
// A snapshot of an array's declared shape (element type + every
// dimension's bounds) - used by parse_array_ref_argument() below to
// compare a call-site argument's actual shape against a parameter's
// expected one, for 1D, 2D, and N-D arrays uniformly. is_2d and is_nd
// are mutually exclusive; lower2/upper2 are only meaningful if is_2d,
// nd_dims/nd_lower/nd_upper only if is_nd.
typedef struct {
    DataType elem;
    int lower, upper;
    int is_2d;
    int lower2, upper2;
    int is_nd;
    int nd_dims;
    int nd_lower[MAX_ARRAY_DIMS];
    int nd_upper[MAX_ARRAY_DIMS];
} ArrayShape;

static int array_shapes_match(const ArrayShape *a, const ArrayShape *b) {
    if (a->elem != b->elem || a->lower != b->lower || a->upper != b->upper
        || a->is_2d != b->is_2d || a->is_nd != b->is_nd) {
        return 0;
    }
    if (a->is_2d && (a->lower2 != b->lower2 || a->upper2 != b->upper2)) {
        return 0;
    }
    if (a->is_nd) {
        if (a->nd_dims != b->nd_dims) return 0;
        for (int d = 0; d < a->nd_dims; d++) {
            if (a->nd_lower[d] != b->nd_lower[d] || a->nd_upper[d] != b->nd_upper[d]) return 0;
        }
    }
    return 1;
}

static ASTNode *parse_array_ref_argument(int proc_idx, int param_index) {
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Expected an array name for parameter %d of '%s'",
                       param_index + 1, proc_table[proc_idx].name);
    }
    char arg_name[MAX_NAME];
    int arg_line = token.line;
    strcpy(arg_name, token.text);
    match(TOKEN_IDENTIFIER);

    ArrayShape expected = {0};
    expected.elem = proc_table[proc_idx].param_types[param_index];
    expected.lower = proc_table[proc_idx].param_array_lower[param_index];
    expected.upper = proc_table[proc_idx].param_array_upper[param_index];
    expected.is_2d = proc_table[proc_idx].param_is_2d[param_index];
    expected.lower2 = proc_table[proc_idx].param_array_lower2[param_index];
    expected.upper2 = proc_table[proc_idx].param_array_upper2[param_index];
    expected.is_nd = proc_table[proc_idx].param_is_nd[param_index];
    expected.nd_dims = proc_table[proc_idx].param_nd_dims[param_index];
    for (int d = 0; d < expected.nd_dims; d++) {
        expected.nd_lower[d] = proc_table[proc_idx].param_nd_lower[param_index][d];
        expected.nd_upper[d] = proc_table[proc_idx].param_nd_upper[param_index][d];
    }

    int levels_up;
    int local_idx = find_local_outward(arg_name, &levels_up);
    if (local_idx != -1) {
        LocalSymbol *ls = local_at(local_idx, levels_up);
        if (ls->is_array || ls->is_array_ref) {
        ArrayShape actual = {0};
        if (ls->is_array) {
            int s = ls->array_sym_idx;
            actual.elem = sym_table[s].type;
            actual.lower = sym_table[s].array_lower;
            actual.upper = sym_table[s].array_upper;
            actual.is_2d = sym_table[s].is_2d;
            actual.lower2 = sym_table[s].array_lower2;
            actual.upper2 = sym_table[s].array_upper2;
            actual.is_nd = sym_table[s].is_nd;
            actual.nd_dims = sym_table[s].nd_dims;
            for (int d = 0; d < actual.nd_dims; d++) {
                actual.nd_lower[d] = sym_table[s].nd_lower[d];
                actual.nd_upper[d] = sym_table[s].nd_upper[d];
            }
        } else {
            actual.elem = ls->type;
            actual.lower = ls->array_lower;
            actual.upper = ls->array_upper;
            actual.is_2d = ls->is_2d;
            actual.lower2 = ls->array_lower2;
            actual.upper2 = ls->array_upper2;
            actual.is_nd = ls->is_nd;
            actual.nd_dims = ls->nd_dims;
            for (int d = 0; d < actual.nd_dims; d++) {
                actual.nd_lower[d] = ls->nd_lower[d];
                actual.nd_upper[d] = ls->nd_upper[d];
            }
        }
        if (!array_shapes_match(&actual, &expected)) {
            compile_error(arg_line, "Array argument '%s' does not match parameter %d of '%s' (declared bounds/type)",
                           arg_name, param_index + 1, proc_table[proc_idx].name);
        }
        ASTNode *node;
        if (ls->is_array) {
            node = create_node(NODE_ARRAY_REF); // compile-time-known sym_table index - level-independent
            node->data.var_idx = ls->array_sym_idx;
        } else {
            node = create_node(NODE_LOCAL_VAR); // the caller's own array-ref param's runtime value
            node->data.var_idx = local_idx;
            node->op = (TokenType)levels_up;
        }
        node->expression_type = expected.elem;
        return node;
        }
    }

    int global_idx = find_var(arg_name);
    if (!sym_table[global_idx].is_array) {
        compile_error(arg_line, "'%s' is not an array", arg_name);
    }
    ArrayShape actual = {0};
    actual.elem = sym_table[global_idx].type;
    actual.lower = sym_table[global_idx].array_lower;
    actual.upper = sym_table[global_idx].array_upper;
    actual.is_2d = sym_table[global_idx].is_2d;
    actual.lower2 = sym_table[global_idx].array_lower2;
    actual.upper2 = sym_table[global_idx].array_upper2;
    actual.is_nd = sym_table[global_idx].is_nd;
    actual.nd_dims = sym_table[global_idx].nd_dims;
    for (int d = 0; d < actual.nd_dims; d++) {
        actual.nd_lower[d] = sym_table[global_idx].nd_lower[d];
        actual.nd_upper[d] = sym_table[global_idx].nd_upper[d];
    }
    if (!array_shapes_match(&actual, &expected)) {
        compile_error(arg_line, "Array argument '%s' does not match parameter %d of '%s' (declared bounds/type)",
                       arg_name, param_index + 1, proc_table[proc_idx].name);
    }
    ASTNode *node = create_node(NODE_ARRAY_REF);
    node->data.var_idx = global_idx;
    node->expression_type = expected.elem;
    return node;
}

// Parses a bare record-variable name as an argument for a record
// parameter (param_index into proc_idx's declared parameter list) - see
// param_is_record[] in ProcSymbol. Record parameters are always BY
// VALUE, and this compiler has no by-reference mechanism for scalars at
// all, so unlike parse_array_ref_argument() there's no "pass a runtime
// reference" option: this flattens the argument into N field-value read
// nodes (its own fields, wherever they resolve to - local frame slot or
// global mangled symbol), one per field of the record TYPE, in declared
// order, chained via ->next exactly like N separate scalar arguments
// would be. Each field is individually range-checked against the
// PARAMETER record type's own field bounds - safe to use the type's
// declared bounds (rather than re-deriving them some other way) because
// the caller's record is required to be of the exact same record type,
// not just a structurally compatible one.
// Sets *out_tail to the chain's last node (NULL for a zero-field record
// type), so the caller can splice the whole chain into its own argument
// list the same way it splices in a single-node argument.
//
// Below the top level of the loop below: walks record_type_idx's own
// fields via base+offset arithmetic (no field_idx array exists below
// the top level - only the argument's own top-level record variable has
// one), recursing into further nested-record fields, appending one
// range-checked read node per LEAF field to the head/tail chain exactly
// as the top-level loop does per field.
static void build_record_arg_values(int record_type_idx, int is_local, int base, int levels_up, ASTNode **head, ASTNode **tail) {
    RecordTypeDef *rt = &record_types[record_type_idx];
    int offset = 0;
    for (int i = 0; i < rt->field_count; i++) {
        RecordField *f = &rt->fields[i];
        if (f->is_record) {
            build_record_arg_values(f->record_type_idx, is_local, base + offset, levels_up, head, tail);
        } else {
            ASTNode *value = record_field_read_node(is_local, base + offset, levels_up);
            value = wrap_range_check(value, f->is_subrange, f->subrange_lower, f->subrange_upper);
            if (!*head) *head = value; else (*tail)->next = value;
            *tail = value;
        }
        offset += f->is_record ? record_type_leaf_count(f->record_type_idx) : 1;
    }
}

static ASTNode *parse_record_argument(int proc_idx, int param_index, ASTNode **out_tail) {
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Parameter %d of '%s' expects a record variable", param_index + 1, proc_table[proc_idx].name);
    }
    char arg_name[MAX_NAME];
    int arg_line = token.line;
    strcpy(arg_name, token.text);
    match(TOKEN_IDENTIFIER);

    int arg_levels_up, arg_is_local, arg_record_type_idx;
    const int *arg_field_idx;
    if (!find_any_record_var_outward(arg_name, &arg_levels_up, &arg_is_local, &arg_record_type_idx, &arg_field_idx)) {
        compile_error(arg_line, "'%s' is not a record variable", arg_name);
    }
    int expected_type_idx = proc_table[proc_idx].param_record_type_idx[param_index];
    if (arg_record_type_idx != expected_type_idx) {
        compile_error(arg_line, "Record argument '%s' (type '%s') does not match parameter %d of '%s' (expects '%s')",
                       arg_name, record_types[arg_record_type_idx].name, param_index + 1, proc_table[proc_idx].name,
                       record_types[expected_type_idx].name);
    }

    RecordTypeDef *rt = &record_types[expected_type_idx];
    ASTNode *head = NULL;
    ASTNode *tail = NULL;
    for (int i = 0; i < rt->field_count; i++) {
        RecordField *f = &rt->fields[i];
        if (f->is_record) {
            build_record_arg_values(f->record_type_idx, arg_is_local, arg_field_idx[i], arg_levels_up, &head, &tail);
            continue;
        }
        ASTNode *value = record_field_read_node(arg_is_local, arg_field_idx[i], arg_levels_up);
        value = wrap_range_check(value, f->is_subrange, f->subrange_lower, f->subrange_upper);
        if (!head) head = value; else tail->next = value;
        tail = value;
    }
    *out_tail = tail;
    return head; // NULL only for a (degenerate) empty record type
}

// Parses an argument for a general 'var' (by-reference SCALAR) parameter
// (param_index into proc_idx's declared parameter list) - see
// param_is_var in common.h's ProcSymbol for the full design. Real Pascal
// restricts a 'var' argument to a VARIABLE, never a general expression,
// so - like parse_record_argument() and parse_array_ref_argument() - this
// resolves a bare identifier (plus an optional '.field') directly, rather
// than calling expression(). Requires an EXACT type match with the
// parameter's declared type (no int->real widening - real Pascal never
// widens a 'var' argument either, unlike a by-value one).
//
// Builds whichever reference-producing node fits:
//   - a with-target's field, a global variable, a static local (itself a
//     hidden global), or a global record's field -> NODE_VAR_REF (a
//     compile-time-constant sym_table[] index)
//   - the caller's own PLAIN local/parameter, or a local record's field
//     -> NODE_LOCAL_VAR_REF (computed at runtime from the CALLER's own
//     frame pointer - see OP_PUSH_LOCAL_REF in vm.c)
//   - the caller's own 'var' parameter, forwarded through unchanged -> an
//     ordinary NODE_LOCAL_VAR read (its raw value IS already a valid
//     reference, from its own caller - nothing to re-encode)
// Whole records and array elements aren't accepted yet (see
// param_is_var's comment in common.h) - both rejected here with a clear
// error, as known gaps rather than a silent wrong answer.
// expected_type/proc_name/param_index are passed explicitly (rather
// than a proc_idx to look them up from) so this same resolution logic
// (with-fields, record fields, statics, forwarding an already-'var'
// slot, plain locals/globals) is reusable for a call THROUGH a
// procedural/functional parameter too, which has no proc_table[] entry
// of its own to look an expected type up from - see
// parse_indirect_call().
// callee_is_const: whether the parameter THIS argument is satisfying
// is itself 'const' (read-only, so forwarding an already-'const' local
// into it is fine) as opposed to 'var'/'out' (writable, so forwarding
// a 'const' local into it must be rejected - see the forwarding branch
// below). Always 0 when called through a procedural/functional
// parameter or a procedural-type variable, since 'const'/'out' aren't
// supported inside those signatures (see parse_proc_signature_tail()'s
// own allow_const_out parameter) - correctly still rejects forwarding
// a 'const' source there too, since such a target is by construction
// never const.
static ASTNode *parse_var_argument(DataType expected_type, const char *proc_name, int param_index, int callee_is_const) {
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Parameter %d of '%s' is a 'var' parameter - expects a variable, not an expression",
                       param_index + 1, proc_name);
    }
    char name[MAX_NAME];
    int line = token.line;
    strcpy(name, token.text);

    int with_field_idx = find_with_field(name);
    if (with_field_idx != -1) {
        match(TOKEN_IDENTIFIER);
        if (sym_table[with_field_idx].type != expected_type) {
            compile_error(line, "'var' argument '%s' has the wrong type for parameter %d of '%s'",
                           name, param_index + 1, proc_name);
        }
        ASTNode *node = create_node(NODE_VAR_REF);
        node->data.var_idx = with_field_idx;
        node->expression_type = expected_type;
        return node;
    }

    {
        int rv_levels_up, rv_is_local, rv_record_type_idx;
        const int *rv_field_idx;
        if (find_any_record_var_outward(name, &rv_levels_up, &rv_is_local, &rv_record_type_idx, &rv_field_idx)) {
            match(TOKEN_IDENTIFIER);
            if (token.type != TOKEN_PERIOD) {
                compile_error(token.line, "'%s' is a record - 'var' expects a field, e.g. '%s.field' (whole records aren't supported as 'var' arguments yet)",
                               name, name);
            }
            match(TOKEN_PERIOD);
            if (token.type != TOKEN_IDENTIFIER) {
                compile_error(token.line, "Expected a field name after '%s.'", name);
            }
            int field_idx = find_record_field(rv_record_type_idx, token.text);
            if (field_idx == -1) {
                compile_error(token.line, "'%s' is not a field of '%s'", token.text, name);
            }
            int resolved_idx = rv_field_idx[field_idx];
            match(TOKEN_IDENTIFIER);
            DataType field_type = rv_is_local ? local_at(resolved_idx, rv_levels_up)->type : sym_table[resolved_idx].type;
            if (field_type != expected_type) {
                compile_error(line, "'var' argument does not match parameter %d of '%s' (wrong type)",
                               param_index + 1, proc_name);
            }
            ASTNode *node = create_node(rv_is_local ? NODE_LOCAL_VAR_REF : NODE_VAR_REF);
            node->data.var_idx = resolved_idx;
            if (rv_is_local) node->op = (TokenType)rv_levels_up;
            node->expression_type = expected_type;
            return node;
        }
    }

    int levels_up;
    int local_idx = find_local_outward(name, &levels_up);
    if (local_idx != -1) {
        LocalSymbol *ls = local_at(local_idx, levels_up);
        if (ls->is_array || ls->is_array_ref) {
            compile_error(line, "'%s' is an array - array elements aren't supported as 'var' arguments yet (an array itself is already always by reference, without needing 'var')", name);
        }
        match(TOKEN_IDENTIFIER);
        if (ls->is_static) {
            int static_idx = ls->static_sym_idx;
            if (sym_table[static_idx].type != expected_type) {
                compile_error(line, "'var' argument '%s' has the wrong type for parameter %d of '%s'",
                               name, param_index + 1, proc_name);
            }
            ASTNode *node = create_node(NODE_VAR_REF);
            node->data.var_idx = static_idx;
            node->expression_type = expected_type;
            return node;
        }
        if (ls->type != expected_type) {
            compile_error(line, "'var' argument '%s' has the wrong type for parameter %d of '%s'",
                           name, param_index + 1, proc_name);
        }
        if (ls->is_var_param) {
            // Forwarding: this slot already holds a valid reference (from
            // this procedure's OWN caller) - pass it through unchanged.
            if (ls->is_const_param && !callee_is_const) {
                compile_error(line, "cannot pass 'const' parameter '%s' where a writable ('var'/'out') parameter is expected", name);
            }
            ASTNode *node = create_node(NODE_LOCAL_VAR);
            node->data.var_idx = local_idx;
            node->op = (TokenType)levels_up;
            node->expression_type = expected_type;
            return node;
        }
        ASTNode *node = create_node(NODE_LOCAL_VAR_REF);
        node->data.var_idx = local_idx;
        node->op = (TokenType)levels_up;
        node->expression_type = expected_type;
        return node;
    }

    int global_idx = find_var(name);
    match(TOKEN_IDENTIFIER);
    if (sym_table[global_idx].is_array) {
        compile_error(line, "'%s' is an array - array elements aren't supported as 'var' arguments yet (an array itself is already always by reference, without needing 'var')", name);
    }
    if (sym_table[global_idx].type != expected_type) {
        compile_error(line, "'var' argument '%s' has the wrong type for parameter %d of '%s'",
                       name, param_index + 1, proc_name);
    }
    ASTNode *node = create_node(NODE_VAR_REF);
    node->data.var_idx = global_idx;
    node->expression_type = expected_type;
    return node;
}

// Compares two procedural signatures for exact structural compatibility
// (same is_function-ness, same return type if a function, same
// parameter count, and same type/var-ness per position, in order - no
// implicit widening, matching this codebase's existing 'var' argument
// rule) - the check every actual argument for a procedural/functional
// parameter must pass, whether it names a top-level procedure/function
// directly or forwards an already-received procedural parameter.
static int proc_signatures_match(int is_function_a, DataType return_type_a, int count_a,
                                  const DataType *types_a, const int *is_var_a,
                                  int is_function_b, DataType return_type_b, int count_b,
                                  const DataType *types_b, const int *is_var_b) {
    if (is_function_a != is_function_b) return 0;
    if (is_function_a && return_type_a != return_type_b) return 0;
    if (count_a != count_b) return 0;
    for (int i = 0; i < count_a; i++) {
        if (types_a[i] != types_b[i] || is_var_a[i] != is_var_b[i]) return 0;
    }
    return 1;
}

// A procedural/functional parameter's own inline signature is always
// scalar-only (see parse_proc_param_header()) - so a top-level
// procedure/function passed as the actual argument must ALSO have only
// plain scalar (or 'var' scalar) parameters, never an array/record/
// procedural one of its own. Without this check, comparing param_types[]
// directly could wrongly accept e.g. an array parameter whose ELEMENT
// type happens to equal the expected scalar type.
static int proc_has_only_scalar_params(int proc_idx) {
    for (int i = 0; i < proc_table[proc_idx].param_count; i++) {
        if (proc_table[proc_idx].param_is_array_ref[i] || proc_table[proc_idx].param_is_record[i] ||
            proc_table[proc_idx].param_is_proc[i]) {
            return 0;
        }
    }
    return 1;
}

// Walks a just-parsed lambda literal's body looking for any reference
// that reaches OUTSIDE the lambda's own parameters/locals into an
// ordinary local or parameter of whatever procedure the lambda text
// happens to sit inside - lambda literals can't capture (see
// docs/LANGUAGE.md#procedural-types). The closed set of node types
// below is exactly the set find_local_outward()'s own callers tag with
// a 'levels_up' value in node->op (see that function's comment in
// common.h's Opcode-adjacent field-reuse notes) - an enclosing local
// ARRAY or 'static' local deliberately isn't in this set, since both
// already resolve to a plain sym_table[] global reference with no
// levels_up tag at all, so they're left alone here on purpose (a lambda
// CAN read/write an enclosing array or static local - see the plan).
// Same generic-recursion idiom as mark_used_variables() in optimizer.c.
static void reject_lambda_captures(ASTNode *node) {
    if (!node) return;
    if ((node->type == NODE_LOCAL_VAR || node->type == NODE_LOCAL_ASSIGN || node->type == NODE_LOCAL_VAR_REF ||
         node->type == NODE_VAR_PARAM_READ || node->type == NODE_VAR_PARAM_ASSIGN ||
         node->type == NODE_REF_ARRAY_ACCESS || node->type == NODE_REF_ARRAY_ASSIGN ||
         node->type == NODE_REF_ARRAY_ACCESS_2D || node->type == NODE_REF_ARRAY_ASSIGN_2D ||
         node->type == NODE_REF_ARRAY_ACCESS_ND || node->type == NODE_REF_ARRAY_ASSIGN_ND ||
         node->type == NODE_LOCAL_STRING_INDEX || node->type == NODE_LOCAL_STRING_INDEX_ASSIGN)
        && (int)node->op > 0) {
        compile_error(node->line, "Lambda body can't reference an enclosing procedure's local variable or parameter "
                                   "- lambda literals can't capture (an enclosing array or 'static' local is still reachable)");
    }
    reject_lambda_captures(node->left);
    reject_lambda_captures(node->right);
    reject_lambda_captures(node->next);
    reject_lambda_captures(node->extra);
}

// Parses an anonymous 'function(...)...end' / 'procedure(...)...end'
// literal, appearing as an expression wherever a bare top-level
// procedure/function name is already accepted as a procedural value
// (see parse_proc_value()/parse_proc_argument()'s own new leading
// branches, the only two callers). Registers a synthetic top-level-
// equivalent proc_table[] entry (a fresh, compiler-generated name, never
// user-visible) and returns its index.
//
// Deliberately NOT a reuse of subroutine_declaration()'s own (much
// larger) parameter-group parser, which also handles array-ref/record/
// procedural/default-value parameters and forward declarations that a
// lambda can never have - see the plan for why a small, self-contained
// duplicate is preferred here over refactoring that function.
//
// Only scalar (optionally 'var') parameters are accepted - matches
// proc_has_only_scalar_params()'s own existing requirement for ANY
// value used as a procedural value, named or anonymous, so this isn't a
// new restriction. No local 'var' section, no nested procedure/function
// declarations inside the body - see the plan's explicit v1 cuts. A
// function-lambda sets its return value via the already-existing
// 'exit(value);' statement (see build_return_assign_node(), keyed off
// current_function_idx alone) - there's no user-writable name to assign
// to via the ordinary 'FuncName := value;' form.
static int parse_lambda_literal(void) {
    int is_function = (token.type == TOKEN_FUNCTION);
    int decl_line = token.line;
    match(is_function ? TOKEN_FUNCTION : TOKEN_PROCEDURE);

    char name[MAX_NAME];
    snprintf(name, MAX_NAME, "__lambda%d", proc_count);
    int proc_idx = add_proc(name);
    // Forced to -1 (top-level-equivalent) immediately, long before the
    // body is parsed - safe to do this early because nothing at PARSE
    // time consults lexical_parent_idx (identifier resolution runs off
    // nesting_depth/scope_locals[] instead, untouched by this), and the
    // capture check below guarantees, before this function returns, that
    // the body never actually needed a static link in the first place.
    // See the plan's "The mechanism" section for the full chain of
    // reasoning (codegen.c:1794/1797 gate OP_ENTER/OP_POP_STATIC_LINK
    // purely on this field; emit_static_link_for_call() and the two
    // existing "is this nested?" rejections in this file do too).
    proc_table[proc_idx].lexical_parent_idx = -1;
    proc_table[proc_idx].lexical_depth = 0;

    int saved_function_idx = current_function_idx;
    int saved_proc_idx = current_proc_idx;
    current_proc_idx = proc_idx;
    nesting_depth++;
    if (nesting_depth >= MAX_NESTING_DEPTH) {
        compile_error(decl_line, "Lambda literal is nested too deeply (limit is %d levels)", MAX_NESTING_DEPTH);
    }
    current_local_count = 0;

    int param_count = 0;
    DataType param_types[MAX_PARAMS];
    char param_names[MAX_PARAMS][MAX_NAME];
    int param_is_var[MAX_PARAMS];
    int param_is_subrange[MAX_PARAMS];
    int param_subrange_lower[MAX_PARAMS];
    int param_subrange_upper[MAX_PARAMS];

    // '(' itself is optional for a zero-parameter lambda - matches the
    // same convention a named procedural type's own signature already
    // uses (parse_proc_signature_tail(): 'procedure;' needs no parens).
    if (token.type == TOKEN_LPAREN) {
    match(TOKEN_LPAREN);
    if (token.type != TOKEN_RPAREN) {
        while (1) {
            int is_var = 0;
            if (token.type == TOKEN_VAR) {
                is_var = 1;
                match(TOKEN_VAR);
            }
            int group_start = param_count;
            while (1) {
                if (token.type != TOKEN_IDENTIFIER) {
                    compile_error(token.line, "Expected a lambda parameter name");
                }
                if (param_count >= MAX_PARAMS) {
                    compile_error(token.line, "Too many lambda parameters (limit is %d)", MAX_PARAMS);
                }
                strcpy(param_names[param_count], token.text);
                match(TOKEN_IDENTIFIER);
                param_count++;
                if (token.type == TOKEN_COMMA) { match(TOKEN_COMMA); continue; }
                break;
            }
            match(TOKEN_COLON);
            DataType type = parse_scalar_type();
            for (int i = group_start; i < param_count; i++) {
                param_types[i] = type;
                param_is_var[i] = is_var;
                param_is_subrange[i] = scalar_type_is_subrange;
                param_subrange_lower[i] = scalar_type_subrange_lower;
                param_subrange_upper[i] = scalar_type_subrange_upper;
                if (is_var) {
                    add_local_var_param(param_names[i], type, scalar_type_is_subrange, scalar_type_subrange_lower,
                                         scalar_type_subrange_upper, 0, 0);
                } else {
                    int idx = add_local(param_names[i], type);
                    current_locals[idx].is_subrange = scalar_type_is_subrange;
                    current_locals[idx].subrange_lower = scalar_type_subrange_lower;
                    current_locals[idx].subrange_upper = scalar_type_subrange_upper;
                }
            }
            if (token.type == TOKEN_SEMI) { match(TOKEN_SEMI); continue; }
            break;
        }
    }
    match(TOKEN_RPAREN);
    }

    proc_table[proc_idx].param_count = param_count;
    proc_table[proc_idx].param_slot_count = current_local_count;
    for (int i = 0; i < param_count; i++) {
        proc_table[proc_idx].param_types[i] = param_types[i];
        strcpy(proc_table[proc_idx].param_names[i], param_names[i]);
        proc_table[proc_idx].param_is_array_ref[i] = 0;
        proc_table[proc_idx].param_is_2d[i] = 0;
        proc_table[proc_idx].param_is_nd[i] = 0;
        proc_table[proc_idx].param_is_subrange[i] = param_is_subrange[i];
        proc_table[proc_idx].param_subrange_lower[i] = param_subrange_lower[i];
        proc_table[proc_idx].param_subrange_upper[i] = param_subrange_upper[i];
        proc_table[proc_idx].param_is_record[i] = 0;
        proc_table[proc_idx].param_is_var[i] = param_is_var[i];
        proc_table[proc_idx].param_is_const[i] = 0;
        proc_table[proc_idx].param_is_out[i] = 0;
        proc_table[proc_idx].param_has_default[i] = 0;
        proc_table[proc_idx].param_default_type[i] = TYPE_UNKNOWN;
        proc_table[proc_idx].param_is_proc[i] = 0;
    }

    proc_table[proc_idx].is_function = is_function;
    if (is_function) {
        match(TOKEN_COLON);
        proc_table[proc_idx].return_type = parse_scalar_type();
        proc_table[proc_idx].return_is_subrange = scalar_type_is_subrange;
        proc_table[proc_idx].return_subrange_lower = scalar_type_subrange_lower;
        proc_table[proc_idx].return_subrange_upper = scalar_type_subrange_upper;
        proc_table[proc_idx].return_slot = current_local_count++;
        current_function_idx = proc_idx;
    } else {
        current_function_idx = -1;
    }

    ASTNode *body = compound_statement();

    proc_table[proc_idx].body = body;
    proc_table[proc_idx].local_count = current_local_count;
    proc_table[proc_idx].is_forward = 0;

    reject_lambda_captures(body);

    current_local_count = 0;
    nesting_depth--;
    current_proc_idx = saved_proc_idx;
    current_function_idx = saved_function_idx;
    return proc_idx;
}

static ASTNode *parse_proc_argument(int proc_idx, int param_index) {
    int expected_is_function = proc_table[proc_idx].param_proc_is_function[param_index];
    DataType expected_return_type = proc_table[proc_idx].param_proc_return_type[param_index];
    int expected_param_count = proc_table[proc_idx].param_proc_param_count[param_index];
    DataType *expected_param_types = proc_table[proc_idx].param_proc_param_types[param_index];
    int *expected_param_is_var = proc_table[proc_idx].param_proc_param_is_var[param_index];

    if (token.type == TOKEN_FUNCTION || token.type == TOKEN_PROCEDURE) {
        int lambda_line = token.line;
        int target_proc_idx = parse_lambda_literal();
        // lexical_parent_idx is already -1 (forced by parse_lambda_literal())
        // - matches a top-level procedure/function exactly, so the "is this
        // nested?" rejection every OTHER path through this function still
        // has (further down, unreached here) simply never applies.
        if (!proc_has_only_scalar_params(target_proc_idx) ||
            !proc_signatures_match(proc_table[target_proc_idx].is_function, proc_table[target_proc_idx].return_type,
                                    proc_table[target_proc_idx].param_count, proc_table[target_proc_idx].param_types,
                                    proc_table[target_proc_idx].param_is_var,
                                    expected_is_function, expected_return_type, expected_param_count,
                                    expected_param_types, expected_param_is_var)) {
            compile_error(lambda_line, "Lambda literal does not match the required signature for parameter %d of '%s'",
                           param_index + 1, proc_table[proc_idx].name);
        }
        ASTNode *node = create_node(NODE_PROC_REF);
        node->line = lambda_line;
        node->data.var_idx = target_proc_idx;
        node->expression_type = TYPE_INTEGER; // meaningless beyond "one int" - see is_proc_param's comment
        return node;
    }

    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Parameter %d of '%s' is a procedural/functional parameter - expects a procedure/function name, not an expression",
                       param_index + 1, proc_table[proc_idx].name);
    }
    char name[MAX_NAME];
    int line = token.line;
    strcpy(name, token.text);

    int levels_up;
    int local_idx = find_local_outward(name, &levels_up);
    if (local_idx != -1) {
        LocalSymbol *ls = local_at(local_idx, levels_up);
        if (!ls->is_proc_param) {
            compile_error(line, "'%s' is not a procedure/function - parameter %d of '%s' expects one",
                           name, param_index + 1, proc_table[proc_idx].name);
        }
        if (!proc_signatures_match(ls->proc_param_is_function, ls->proc_param_return_type, ls->proc_param_param_count,
                                    ls->proc_param_param_types, ls->proc_param_param_is_var,
                                    expected_is_function, expected_return_type, expected_param_count,
                                    expected_param_types, expected_param_is_var)) {
            compile_error(line, "'%s' does not match the required signature for parameter %d of '%s'",
                           name, param_index + 1, proc_table[proc_idx].name);
        }
        match(TOKEN_IDENTIFIER);
        // Forwarding: this local already holds a valid top-level entry
        // address, received from THIS procedure's own caller - pass it
        // through unchanged, exactly like 'var'-parameter forwarding
        // (see NODE_LOCAL_VAR's own reuse for that case above).
        ASTNode *node = create_node(NODE_LOCAL_VAR);
        node->line = line;
        node->data.var_idx = local_idx;
        node->op = (TokenType)levels_up;
        node->expression_type = TYPE_INTEGER; // meaningless beyond "one int" - see is_proc_param's comment
        return node;
    }

    int target_proc_idx = find_proc_visible(name);
    if (target_proc_idx == -1 && find_var_soft_visible(name) == -1) {
        // Genuinely unknown by any mechanism - the original, more
        // specific error (not the fallback's own generic one below).
        compile_error(line, "Undeclared procedure/function '%s'", name);
    }
    if (target_proc_idx == -1) {
        // A real global variable, just not (on its own) of this
        // procedural type - could be a class instance expression whose
        // method call returns a matching procedural value (e.g.
        // 'f.MakeHandler()'), which this function has no special syntax
        // of its own for. Falls back to a general expression() (already
        // knows how to parse and type-check a method call, self-
        // shorthand, etc. in full) - but unlike parse_proc_value()'s OWN
        // fallback below, this result feeds into type_checker.c's
        // generic per-argument check, which deliberately SKIPS
        // validating a param_is_proc slot (trusting the parser to
        // already have checked it, via the TYPE_INTEGER placeholder
        // convention every other branch here uses) - so this validates
        // the signature manually before applying that same placeholder.
        ASTNode *node = expression();
        int matches = node->expression_type >= TYPE_PROC_BASE;
        if (matches) {
            ProcParamHeader *ret_sig = &proc_types[node->expression_type - TYPE_PROC_BASE].sig;
            matches = expected_is_function && proc_signatures_match(ret_sig->is_function, ret_sig->return_type, ret_sig->param_count,
                                             ret_sig->param_types, ret_sig->param_is_var,
                                             expected_is_function, expected_return_type, expected_param_count,
                                             expected_param_types, expected_param_is_var);
        }
        if (!matches) {
            compile_error(line, "Expression does not match the required signature for parameter %d of '%s'",
                           param_index + 1, proc_table[proc_idx].name);
        }
        node->expression_type = TYPE_INTEGER; // meaningless beyond "one int" - see is_proc_param's comment
        return node;
    }
    match(TOKEN_IDENTIFIER);

    // Same explicit-'(' disambiguation as parse_proc_value() - see its
    // own comment. Here "the required procedural type" is this
    // parameter's own inline signature rather than a named proc_types[]
    // entry, so - unlike parse_proc_value()'s exact-named-type equality
    // (nominal typing, matching how every other procedural-type context
    // already compares) - matching against an unnamed inline signature
    // has nothing to compare BY NAME, so this checks the called
    // function's return type's own underlying signature structurally,
    // via the same proc_signatures_match() the non-call fallback below
    // already uses against these exact expected_* fields.
    if (token.type == TOKEN_LPAREN) {
        int matches = expected_is_function && proc_table[target_proc_idx].is_function &&
            proc_table[target_proc_idx].return_type >= TYPE_PROC_BASE;
        if (matches) {
            ProcParamHeader *ret_sig = &proc_types[proc_table[target_proc_idx].return_type - TYPE_PROC_BASE].sig;
            matches = proc_signatures_match(ret_sig->is_function, ret_sig->return_type, ret_sig->param_count,
                                             ret_sig->param_types, ret_sig->param_is_var,
                                             expected_is_function, expected_return_type, expected_param_count,
                                             expected_param_types, expected_param_is_var);
        }
        if (!matches) {
            compile_error(line, "'%s(...)' does not return the required procedural type for parameter %d of '%s'",
                           name, param_index + 1, proc_table[proc_idx].name);
        }
        ASTNode *node = create_node(NODE_CALL);
        node->line = line;
        node->data.var_idx = target_proc_idx;
        node->left = parse_call_arguments(target_proc_idx);
        // Matches NODE_PROC_REF's own placeholder just below (see its
        // comment) - type_checker.c's generic per-argument check for a
        // param_is_proc slot compares against param_types[i]'s own
        // identical placeholder, deliberately never validating a
        // procedural argument that way (real validation already happened
        // above, at parse time).
        node->expression_type = TYPE_INTEGER;
        return node;
    }

    if (proc_table[target_proc_idx].lexical_parent_idx != -1) {
        compile_error(line, "'%s' is a nested procedure/function - only a top-level one can be passed as a procedural/functional parameter (see docs/LANGUAGE.md)", name);
    }
    if (!proc_has_only_scalar_params(target_proc_idx) ||
        !proc_signatures_match(proc_table[target_proc_idx].is_function, proc_table[target_proc_idx].return_type,
                                proc_table[target_proc_idx].param_count, proc_table[target_proc_idx].param_types,
                                proc_table[target_proc_idx].param_is_var,
                                expected_is_function, expected_return_type, expected_param_count,
                                expected_param_types, expected_param_is_var)) {
        compile_error(line, "'%s' does not match the required signature for parameter %d of '%s'",
                       name, param_index + 1, proc_table[proc_idx].name);
    }
    ASTNode *node = create_node(NODE_PROC_REF);
    node->line = line;
    node->data.var_idx = target_proc_idx;
    node->expression_type = TYPE_INTEGER; // meaningless beyond "one int" - see is_proc_param's comment
    return node;
}

// Parses the RHS of an assignment INTO a NAMED procedural-type target
// (proc_type_idx into proc_types[]) - 'nil', a top-level (non-nested)
// procedure/function name matching the target's exact signature, or an
// existing variable/local/'var'-parameter ALREADY of the same
// procedural type (a plain copy of its already-held address, never a
// call - see docs/LANGUAGE.md#classes: a procedural-type value can only
// ever be read bare in exactly this one context, or forwarded as a
// matching call argument in a future step; everywhere else, a bare
// reference to one calls it). Mirrors parse_proc_argument()'s exact
// same two non-nil cases - duplicated rather than shared, since that
// one is keyed by a PARAMETER's own inline signature storage
// (proc_table[].param_proc_*), not proc_types[].
static ASTNode *parse_proc_value(int proc_type_idx, int line) {
    ProcParamHeader *sig = &proc_types[proc_type_idx].sig;
    if (token.type == TOKEN_NIL) {
        match(TOKEN_NIL);
        ASTNode *node = create_node(NODE_NUMBER);
        node->line = line;
        node->data.num_value = -1;
        node->expression_type = TYPE_NIL;
        return node;
    }
    if (token.type == TOKEN_FUNCTION || token.type == TOKEN_PROCEDURE) {
        int lambda_line = token.line;
        int target_proc_idx = parse_lambda_literal();
        if (!proc_has_only_scalar_params(target_proc_idx) ||
            !proc_signatures_match(proc_table[target_proc_idx].is_function, proc_table[target_proc_idx].return_type,
                                    proc_table[target_proc_idx].param_count, proc_table[target_proc_idx].param_types,
                                    proc_table[target_proc_idx].param_is_var,
                                    sig->is_function, sig->return_type, sig->param_count,
                                    sig->param_types, sig->param_is_var)) {
            compile_error(lambda_line, "Lambda literal does not match the required signature for procedural type '%s'",
                           proc_types[proc_type_idx].name);
        }
        ASTNode *node = create_node(NODE_PROC_REF);
        node->line = lambda_line;
        node->data.var_idx = target_proc_idx;
        node->expression_type = (DataType)(TYPE_PROC_BASE + proc_type_idx);
        return node;
    }
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Expected 'nil', a procedure/function name, or a variable of the same procedural type");
    }
    char name[MAX_NAME];
    int name_line = token.line;
    strcpy(name, token.text);

    int levels_up;
    int local_idx = find_local_outward(name, &levels_up);
    if (local_idx != -1) {
        LocalSymbol *ls = local_at(local_idx, levels_up);
        if (ls->type != (DataType)(TYPE_PROC_BASE + proc_type_idx)) {
            compile_error(name_line, "'%s' is not a procedure/function or a variable of this procedural type", name);
        }
        match(TOKEN_IDENTIFIER);
        ASTNode *node = create_node(ls->is_var_param ? NODE_VAR_PARAM_READ : NODE_LOCAL_VAR);
        node->line = name_line;
        node->data.var_idx = local_idx;
        node->op = (TokenType)levels_up;
        node->expression_type = ls->type;
        return node;
    }

    int global_idx = find_var_soft_visible(name);
    if (global_idx != -1 && sym_table[global_idx].type == (DataType)(TYPE_PROC_BASE + proc_type_idx)) {
        match(TOKEN_IDENTIFIER);
        ASTNode *node = create_node(NODE_VARIABLE);
        node->line = name_line;
        node->data.var_idx = global_idx;
        node->expression_type = sym_table[global_idx].type;
        return node;
    }

    int target_proc_idx = find_proc_visible(name);
    if (target_proc_idx == -1 && find_var_soft_visible(name) == -1) {
        // Genuinely unknown by any mechanism - the original, more
        // specific error (not the fallback's own generic one below).
        compile_error(name_line, "Undeclared procedure/function '%s'", name);
    }
    if (target_proc_idx == -1) {
        // A real global variable, just not (on its own) of this
        // procedural type - could be a class instance expression whose
        // method call returns this procedural type (e.g.
        // 'f.MakeHandler()'), which this function has no special syntax
        // of its own for. Falls back to a general expression()
        // (already knows how to parse and type-check a method call,
        // self-shorthand, etc. in full) - the assignment's own generic
        // type check (used for every OTHER procedural-type target
        // already, e.g. an existing variable of this type) validates the
        // result correctly, so nothing extra is needed here (unlike
        // parse_proc_argument()'s own fallback, which has to validate
        // manually - see its comment).
        return expression();
    }
    match(TOKEN_IDENTIFIER);

    // An explicit '(' here means "call this function and use its RETURN
    // VALUE" (which must itself already be this exact procedural type),
    // as opposed to the bare-name case below, "take a reference to this
    // proc's own address" (whose own signature, not its return type,
    // must match) - needed for a function that itself returns a
    // procedural value. This context is inherently ambiguous between the
    // two readings, so '(' is a deliberate, explicit disambiguator, not
    // an inferred one - even a zero-argument function needs '()' here,
    // unlike an ordinary expression context (see docs/LANGUAGE.md#procedural-types).
    if (token.type == TOKEN_LPAREN) {
        if (!proc_table[target_proc_idx].is_function ||
            proc_table[target_proc_idx].return_type != (DataType)(TYPE_PROC_BASE + proc_type_idx)) {
            compile_error(name_line, "'%s(...)' does not return the required procedural type '%s'", name, proc_types[proc_type_idx].name);
        }
        ASTNode *node = create_node(NODE_CALL);
        node->line = name_line;
        node->data.var_idx = target_proc_idx;
        node->left = parse_call_arguments(target_proc_idx);
        node->expression_type = proc_table[target_proc_idx].return_type;
        return node;
    }

    if (proc_table[target_proc_idx].lexical_parent_idx != -1) {
        compile_error(name_line, "'%s' is a nested procedure/function - only a top-level one can be assigned to a procedural type (see docs/LANGUAGE.md)", name);
    }
    if (!proc_has_only_scalar_params(target_proc_idx) ||
        !proc_signatures_match(proc_table[target_proc_idx].is_function, proc_table[target_proc_idx].return_type,
                                proc_table[target_proc_idx].param_count, proc_table[target_proc_idx].param_types,
                                proc_table[target_proc_idx].param_is_var,
                                sig->is_function, sig->return_type, sig->param_count,
                                sig->param_types, sig->param_is_var)) {
        compile_error(name_line, "'%s' does not match the required signature for procedural type '%s'", name, proc_types[proc_type_idx].name);
    }
    ASTNode *node = create_node(NODE_PROC_REF);
    node->line = name_line;
    node->data.var_idx = target_proc_idx;
    node->expression_type = (DataType)(TYPE_PROC_BASE + proc_type_idx);
    return node;
}

static ASTNode *parse_call_arguments(int proc_idx) {
    ASTNode *arg_head = NULL;
    ASTNode *arg_tail = NULL;
    int arg_count = 0;
    int call_line = token.line; // captured before any argument parsing -
                                 // by the time a default needs splicing
                                 // in below, token.line has moved well
                                 // past the whole call.
    if (token.type == TOKEN_LPAREN) {
        match(TOKEN_LPAREN);
        if (token.type != TOKEN_RPAREN) {
            while (1) {
                ASTNode *arg;
                ASTNode *this_tail;
                if (arg_count < proc_table[proc_idx].param_count && proc_table[proc_idx].param_is_array_ref[arg_count]) {
                    arg = parse_array_ref_argument(proc_idx, arg_count);
                    this_tail = arg;
                } else if (arg_count < proc_table[proc_idx].param_count && proc_table[proc_idx].param_is_record[arg_count]) {
                    arg = parse_record_argument(proc_idx, arg_count, &this_tail);
                } else if (arg_count < proc_table[proc_idx].param_count && proc_table[proc_idx].param_is_var[arg_count]) {
                    arg = parse_var_argument(proc_table[proc_idx].param_types[arg_count], proc_table[proc_idx].name, arg_count, proc_table[proc_idx].param_is_const[arg_count]);
                    this_tail = arg;
                } else if (arg_count < proc_table[proc_idx].param_count && proc_table[proc_idx].param_is_proc[arg_count]) {
                    arg = parse_proc_argument(proc_idx, arg_count);
                    this_tail = arg;
                } else if (arg_count < proc_table[proc_idx].param_count) {
                    arg = wrap_range_check(expression(), proc_table[proc_idx].param_is_subrange[arg_count],
                        proc_table[proc_idx].param_subrange_lower[arg_count], proc_table[proc_idx].param_subrange_upper[arg_count]);
                    this_tail = arg;
                } else {
                    arg = expression(); // too many arguments - the count mismatch error below still fires
                    this_tail = arg;
                }
                arg_count++;
                if (arg) { // NULL only for a zero-field record argument
                    if (!arg_head) arg_head = arg; else arg_tail->next = arg;
                    arg_tail = this_tail;
                }
                if (token.type == TOKEN_COMMA) { match(TOKEN_COMMA); continue; }
                break;
            }
        }
        match(TOKEN_RPAREN);
    }
    int param_count = proc_table[proc_idx].param_count;
    int min_required = param_count;
    for (int i = 0; i < param_count; i++) {
        if (proc_table[proc_idx].param_has_default[i]) { min_required = i; break; }
    }
    if (arg_count < min_required || arg_count > param_count) {
        if (min_required == param_count) {
            compile_error(token.line, "'%s' expects %d argument(s), got %d",
                           proc_table[proc_idx].name, param_count, arg_count);
        } else {
            compile_error(token.line, "'%s' expects between %d and %d argument(s), got %d",
                           proc_table[proc_idx].name, min_required, param_count, arg_count);
        }
    }
    for (int i = arg_count; i < param_count; i++) {
        ASTNode *def = make_default_value_node(proc_table[proc_idx].param_default_type[i],
                                                proc_table[proc_idx].param_default_value[i], call_line);
        if (!arg_head) arg_head = def; else arg_tail->next = def;
        arg_tail = def;
    }
    return arg_head;
}

// A call THROUGH an already-received procedural/functional parameter
// (e.g. 'f(x)' where f is itself such a parameter) - a small sibling of
// parse_call_arguments()/NODE_CALL, reading ls's own stored inline
// signature instead of a proc_table[] entry (there isn't one - the
// actual callee is only known at runtime). Only two argument kinds are
// possible here (by-value scalar, 'var' scalar), matching this
// signature's own scalar-only restriction (see parse_proc_param_header).
// ls/local_idx/levels_up identify the already-resolved local slot
// holding the runtime entry address; is_statement mirrors NODE_CALL's
// own op == TOKEN_PROCEDURE flag (statement context: discard an unused
// function result, then continue the enclosing statement chain via
// ->next) - stored on NODE_CALL_INDIRECT's extra instead of op, since op
// is needed here for levels_up (see NODE_CALL_INDIRECT's comment in
// common.h).
static ASTNode *parse_indirect_call(LocalSymbol *ls, int local_idx, int levels_up, int line, int is_statement) {
    if (!is_statement && !ls->proc_param_is_function) {
        compile_error(line, "'%s' is a procedure and does not return a value; it cannot be used in an expression", ls->name);
    }
    match(TOKEN_IDENTIFIER);

    ASTNode *arg_head = NULL;
    ASTNode *arg_tail = NULL;
    int arg_count = 0;
    if (token.type == TOKEN_LPAREN) {
        match(TOKEN_LPAREN);
        if (token.type != TOKEN_RPAREN) {
            while (1) {
                ASTNode *arg;
                if (arg_count < ls->proc_param_param_count && ls->proc_param_param_is_var[arg_count]) {
                    arg = parse_var_argument(ls->proc_param_param_types[arg_count], ls->name, arg_count, 0); // 0: const/out never valid inside a procedural/functional parameter's own signature (see docs/LANGUAGE.md)
                } else if (arg_count < ls->proc_param_param_count) {
                    arg = expression();
                    DataType expected = ls->proc_param_param_types[arg_count];
                    DataType actual = arg->expression_type;
                    int expected_stringy = (expected == TYPE_STRING || expected == TYPE_CHAR);
                    int actual_stringy = (actual == TYPE_STRING || actual == TYPE_CHAR);
                    if (!(expected_stringy && actual_stringy) && expected != actual) {
                        if (actual == TYPE_INTEGER && expected == TYPE_REAL) {
                            // Implicit int->real widening, matching an
                            // ordinary by-value call argument's own rule
                            // (see try_widen_for_assignment() in
                            // type_checker.c) - duplicated narrowly here,
                            // rather than deferred to type_checker.c,
                            // because NODE_CALL_INDIRECT has no
                            // proc_table[] entry for it to look expected
                            // argument types up from at that later stage.
                            ASTNode *wrapper = create_node(NODE_INT_TO_REAL);
                            wrapper->left = arg;
                            wrapper->expression_type = TYPE_REAL;
                            wrapper->line = arg->line;
                            arg = wrapper;
                        } else {
                            compile_error(token.line, "Argument %d to '%s' has the wrong type", arg_count + 1, ls->name);
                        }
                    }
                } else {
                    arg = expression(); // too many arguments - the count mismatch error below still fires
                }
                arg_count++;
                if (!arg_head) arg_head = arg; else arg_tail->next = arg;
                arg_tail = arg;
                if (token.type == TOKEN_COMMA) { match(TOKEN_COMMA); continue; }
                break;
            }
        }
        match(TOKEN_RPAREN);
    }
    if (arg_count != ls->proc_param_param_count) {
        compile_error(line, "'%s' expects %d argument(s), got %d", ls->name, ls->proc_param_param_count, arg_count);
    }

    ASTNode *node = create_node(NODE_CALL_INDIRECT);
    node->line = line;
    node->data.var_idx = local_idx;
    node->op = (TokenType)levels_up;
    node->left = arg_head;
    node->expression_type = ls->proc_param_is_function ? ls->proc_param_return_type : TYPE_UNKNOWN;
    ASTNode *stmt_flag = create_node(NODE_NUMBER);
    stmt_flag->data.num_value = is_statement;
    stmt_flag->expression_type = TYPE_INTEGER;
    node->extra = stmt_flag;
    return node;
}

// Parses a call through an already-resolved NAMED procedural-type value
// ('callee', already built by whichever ordinary read the caller used -
// NODE_VARIABLE/NODE_LOCAL_VAR/NODE_VAR_PARAM_READ) - '(args)', or
// nothing for a zero-argument call, matching how an ordinary
// parameterless function/procedure call already works. Builds
// NODE_PROCVAR_CALL, NOT NODE_CALL_INDIRECT - see NODE_PROCVAR_CALL's
// own comment in common.h for why (NODE_CALL_INDIRECT's shape is
// hardcoded to "the callee's address always lives in a LOCAL frame
// slot", which a plain GLOBAL procedural-type variable doesn't fit).
// Mirrors parse_indirect_call()'s own inline argument-parsing exactly,
// including the int->real widening duplicated here for the same reason
// it's duplicated there: there's no proc_table[] entry for a
// procedural-type value's own signature to look up later.
static ASTNode *build_procvar_call(ASTNode *callee, int proc_type_idx, int line, int is_statement) {
    ProcParamHeader *sig = &proc_types[proc_type_idx].sig;
    if (!is_statement && !sig->is_function) {
        compile_error(line, "'%s' is a procedure and does not return a value; it cannot be used in an expression", proc_types[proc_type_idx].name);
    }
    ASTNode *arg_head = NULL;
    ASTNode *arg_tail = NULL;
    int arg_count = 0;
    if (token.type == TOKEN_LPAREN) {
        match(TOKEN_LPAREN);
        if (token.type != TOKEN_RPAREN) {
            while (1) {
                ASTNode *arg;
                if (arg_count < sig->param_count && sig->param_is_var[arg_count]) {
                    arg = parse_var_argument(sig->param_types[arg_count], proc_types[proc_type_idx].name, arg_count, 0); // 0: const/out never valid inside a named procedural type's own signature (see docs/LANGUAGE.md)
                } else if (arg_count < sig->param_count) {
                    arg = expression();
                    DataType expected = sig->param_types[arg_count];
                    DataType actual = arg->expression_type;
                    int expected_stringy = (expected == TYPE_STRING || expected == TYPE_CHAR);
                    int actual_stringy = (actual == TYPE_STRING || actual == TYPE_CHAR);
                    if (!(expected_stringy && actual_stringy) && expected != actual) {
                        if (actual == TYPE_INTEGER && expected == TYPE_REAL) {
                            ASTNode *wrapper = create_node(NODE_INT_TO_REAL);
                            wrapper->left = arg;
                            wrapper->expression_type = TYPE_REAL;
                            wrapper->line = arg->line;
                            arg = wrapper;
                        } else {
                            compile_error(token.line, "Argument %d to '%s' has the wrong type", arg_count + 1, proc_types[proc_type_idx].name);
                        }
                    }
                } else {
                    arg = expression(); // too many arguments - the count mismatch error below still fires
                }
                arg_count++;
                if (!arg_head) arg_head = arg; else arg_tail->next = arg;
                arg_tail = arg;
                if (token.type == TOKEN_COMMA) { match(TOKEN_COMMA); continue; }
                break;
            }
        }
        match(TOKEN_RPAREN);
    }
    if (arg_count != sig->param_count) {
        compile_error(line, "'%s' expects %d argument(s), got %d", proc_types[proc_type_idx].name, sig->param_count, arg_count);
    }

    ASTNode *node = create_node(NODE_PROCVAR_CALL);
    node->line = line;
    node->left = arg_head;
    node->right = callee;
    node->expression_type = sig->is_function ? sig->return_type : TYPE_UNKNOWN;
    ASTNode *stmt_flag = create_node(NODE_NUMBER);
    stmt_flag->data.num_value = is_statement;
    stmt_flag->expression_type = TYPE_INTEGER;
    node->extra = stmt_flag;
    return node;
}

// Parses a class method call's explicit argument list at the call site
// ('(args)', or nothing for a zero-argument call - matching how an
// ordinary parameterless function call already works). 'self' is NOT
// written here - the caller already has it (the already-resolved
// instance expression) and splices it in as argument 0 itself, before
// this function's own returned list. mangled_proc_idx's OWN
// param_count includes self at slot 0, so every check/lookup here is
// offset by 1. Method parameters are guaranteed scalar - never array/
// record arguments - but DO include named procedural types (see
// ProcParamHeader/parse_proc_param_header()), which need the same
// specialized parse_proc_value() every other procedural-type target
// already uses, alongside the plain-scalar/'var'-scalar cases
// parse_call_arguments() itself handles.
static ASTNode *parse_class_method_call_arguments(int mangled_proc_idx) {
    ASTNode *arg_head = NULL;
    ASTNode *arg_tail = NULL;
    int user_param_count = proc_table[mangled_proc_idx].param_count - 1; // excluding self
    int arg_count = 0;
    int call_line = token.line; // captured before any argument parsing -
                                 // by the time a default needs splicing
                                 // in below, token.line has moved well
                                 // past the whole call.
    if (token.type == TOKEN_LPAREN) {
        match(TOKEN_LPAREN);
        if (token.type != TOKEN_RPAREN) {
            while (1) {
                int slot = arg_count + 1; // +1 to skip self at slot 0
                ASTNode *arg;
                if (arg_count < user_param_count && proc_table[mangled_proc_idx].param_is_var[slot]) {
                    arg = parse_var_argument(proc_table[mangled_proc_idx].param_types[slot], proc_table[mangled_proc_idx].unmangled_name, slot, proc_table[mangled_proc_idx].param_is_const[slot]);
                } else if (arg_count < user_param_count && proc_table[mangled_proc_idx].param_types[slot] >= TYPE_PROC_BASE) {
                    arg = parse_proc_value(proc_table[mangled_proc_idx].param_types[slot] - TYPE_PROC_BASE, token.line);
                } else if (arg_count < user_param_count) {
                    arg = wrap_range_check(expression(), proc_table[mangled_proc_idx].param_is_subrange[slot],
                        proc_table[mangled_proc_idx].param_subrange_lower[slot], proc_table[mangled_proc_idx].param_subrange_upper[slot]);
                } else {
                    arg = expression(); // too many arguments - the count mismatch error below still fires
                }
                if (!arg_head) arg_head = arg; else arg_tail->next = arg;
                arg_tail = arg;
                arg_count++;
                if (token.type == TOKEN_COMMA) { match(TOKEN_COMMA); continue; }
                break;
            }
        }
        match(TOKEN_RPAREN);
    }
    int min_required = user_param_count;
    for (int i = 0; i < user_param_count; i++) {
        if (proc_table[mangled_proc_idx].param_has_default[i + 1]) { min_required = i; break; } // +1 to skip self at slot 0
    }
    if (arg_count < min_required || arg_count > user_param_count) {
        if (min_required == user_param_count) {
            compile_error(token.line, "'%s' expects %d argument(s), got %d",
                           proc_table[mangled_proc_idx].unmangled_name, user_param_count, arg_count);
        } else {
            compile_error(token.line, "'%s' expects between %d and %d argument(s), got %d",
                           proc_table[mangled_proc_idx].unmangled_name, min_required, user_param_count, arg_count);
        }
    }
    for (int i = arg_count; i < user_param_count; i++) {
        int slot = i + 1; // +1 to skip self at slot 0
        ASTNode *def = make_default_value_node(proc_table[mangled_proc_idx].param_default_type[slot],
                                                proc_table[mangled_proc_idx].param_default_value[slot], call_line);
        if (!arg_head) arg_head = def; else arg_tail->next = def;
        arg_tail = def;
    }
    return arg_head;
}

// One resolved '^' step's outcome - a record field's offset/type/subrange
// info (0/scalar-target-type/not-subrange for a scalar pointer target's
// bare '^', which behaves exactly like a 1-field, unnamed record for
// this purpose) - OR, for a class specifically, a method CALL instead
// (is_method_call/call_node; the other fields are meaningless then).
typedef struct {
    int field_offset;
    DataType result_type;
    int is_subrange;
    int subrange_lower;
    int subrange_upper;
    int is_method_call;
    ASTNode *call_node; // only meaningful if is_method_call - a
                        // complete NODE_CALL, 'self' already spliced
                        // in as its first argument. op is left unset -
                        // the caller sets it to TOKEN_PROCEDURE when
                        // used as a statement (discard an unused
                        // function result), matching every other
                        // call-statement site's own convention.
    int is_array_field; // true if this step is a class array field
                        // element access ('c.data[i]') rather than an
                        // ordinary scalar/nested-record field - a THIRD
                        // possible outcome alongside is_method_call,
                        // also always terminal (see NODE_HEAP_ARRAY_
                        // FIELD_ACCESS in common.h). field_offset is the
                        // COMBINED offset (field base offset - array
                        // lower bound) when this is set, not directly
                        // usable as an ordinary field offset.
    ASTNode *array_index; // only meaningful if is_array_field - the
                        // index expression, already range-checked
                        // against the field's declared array bounds.
    int is_property_setter; // true if this step is a property whose WRITE
                        // target is a setter PROCEDURE (as opposed to a
                        // field) - a FOURTH possible outcome, but unlike
                        // the three above it's never itself complete: the
                        // setter's one argument is the value expression
                        // that appears AFTER ':=', which this function
                        // hasn't parsed yet (it doesn't even know ':=' is
                        // coming - that's the write-context CALLER's job,
                        // same as for an ordinary field write). Only ever
                        // set when is_statement_context was 1 - a property
                        // read (expression context) always resolves the
                        // getter into a complete call itself, see
                        // is_method_call. result_type is the PROPERTY's
                        // own declared type (identical to the setter's
                        // param type by construction - checked once, at
                        // property-declaration time). See
                        // build_property_setter_call().
    int setter_method_idx; // only meaningful if is_property_setter -
                        // pt->methods[] index of the setter procedure
                        // (also its vtable slot, exactly like an ordinary
                        // method call's method_idx/NODE_VIRTUAL_CALL).
} HeapDerefStep;

// Whether class_ptr_idx is safe to new() - i.e. whether it (including
// every method inherited from an ancestor) has any method still marked
// abstract. Deliberately checks the is_abstract FLAG directly, NOT
// find_proc(mangled_name) == -1 - register_abstract_method_signature()
// gives every abstract method a phantom proc_table[] entry, so
// find_proc() ALWAYS succeeds for one; is_abstract is the only signal
// left that still means "no real implementation exists". Works
// correctly across multi-level inheritance for free: an unoverridden
// abstract method's is_abstract=1 flows through the ordinary struct-
// copy inheritance mechanism (parse_class_declaration()'s copy loop)
// completely unchanged; a concrete override replaces the WHOLE header
// in place, defaulting is_abstract back to 0 unless the override is
// itself also declared abstract (allowed - deferring further down the
// hierarchy).
static const char *class_first_unresolved_abstract_method(int class_ptr_idx) {
    PointerTypeDef *pt = &pointer_types[class_ptr_idx];
    for (int i = 0; i < pt->method_count; i++) {
        if (pt->methods[i].is_abstract) return pt->methods[i].name;
    }
    return NULL;
}

// class_ptr_idx's destructor slot (stable vtable index, following
// inheritance - a class that doesn't declare/override one still finds
// an ancestor's, via the same struct-copy inheritance mechanism), or -1
// if no destructor exists anywhere in the hierarchy. Used by
// parse_dispose_statement() to build the virtual call dispose() makes
// before actually freeing the block - see there for why this returns
// the SLOT index (NODE_VIRTUAL_CALL.data.num_value), not a proc_table[]
// index.
static int class_find_destructor(int class_ptr_idx) {
    PointerTypeDef *pt = &pointer_types[class_ptr_idx];
    for (int i = 0; i < pt->method_count; i++) {
        if (pt->methods[i].is_destructor) return i;
    }
    return -1;
}

// Whether 'name' is a field or method of class_ptr_idx - the check that
// decides whether a bare identifier inside a method body should be
// treated as implicit 'self.name' shorthand (see
// parse_self_shorthand_read()/parse_self_shorthand_write() below).
// Mirrors resolve_heap_deref_step()'s own field-then-method check order,
// since that's the function that actually resolves the access once this
// says yes.
static int class_has_member(int class_ptr_idx, const char *name) {
    PointerTypeDef *pt = &pointer_types[class_ptr_idx];
    if (find_record_field(pt->target_record_type_idx, name) != -1) {
        return 1;
    }
    for (int i = 0; i < pt->property_count; i++) {
        if (strcmp(pt->properties[i].name, name) == 0) return 1;
    }
    for (int i = 0; i < pt->method_count; i++) {
        if (strcmp(pt->methods[i].name, name) == 0) return 1;
    }
    // Covers TRUE class methods (is_class_method) / TRUE class
    // properties (is_class_property) automatically too, unmodified -
    // they're already just flagged entries in the two loops above.
    // class_vars[] is the one genuinely new table needing its own scan.
    for (int i = 0; i < pt->class_var_count; i++) {
        if (strcmp(pt->class_vars[i].name, name) == 0) return 1;
    }
    return 0;
}

// How many contiguous heap ints a single top-level class field
// occupies - its element count for an array field (is_array/is_record
// are mutually exclusive, so no further weighting needed - an array
// field's element type is always scalar), record_type_leaf_count() for
// a nested record (guaranteed array-field-free by
// parse_record_field_group()'s own existing check, so its leaf count IS
// its heap slot count), or 1 for an ordinary scalar.
static int class_field_heap_slots(RecordField *f) {
    if (f->is_array) return f->array_upper - f->array_lower + 1;
    return f->is_record ? record_type_leaf_count(f->record_type_idx) : 1;
}

// The heap offset of field field_idx within rt - 1 (the hidden runtime-
// tag slot every class instance carries at offset 0) plus the summed
// heap-slot cost of every preceding field. Passing field_idx ==
// rt->field_count gives the record's TOTAL heap slot count (1 + tag) -
// exactly target_elem_size - so parse_class_declaration() reuses this
// directly instead of a separate "total slots" helper.
static int class_field_base_offset(RecordTypeDef *rt, int field_idx) {
    int offset = 1;
    for (int i = 0; i < field_idx; i++) {
        offset += class_field_heap_slots(&rt->fields[i]);
    }
    return offset;
}

// Continues an already-resolved TOP-LEVEL class field access through
// further nested-record '.subfield' steps - same per-sibling weighting
// resolve_record_field_leaf() uses for a plain record variable's own
// chain (both units mean the same thing: one leaf is always one storage
// slot, whichever kind of storage it is). Updates *f_ptr to the chain's
// final (guaranteed scalar) leaf field. A chain that stops on a still-
// is_record field with no further '.' is a compile error - reading/
// writing/passing a "whole nested record" heap access isn't supported
// yet (a known gap, like array fields).
static int resolve_class_field_chain_offset(RecordField **f_ptr) {
    int extra = 0;
    RecordField *f = *f_ptr;
    while (f->is_record) {
        if (token.type != TOKEN_PERIOD) {
            compile_error(token.line, "'%s' names a whole record - specify a further field, e.g. '%s.fieldname'", f->name, f->name);
        }
        match(TOKEN_PERIOD);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "Expected a field name after '%s.'", f->name);
        }
        RecordTypeDef *srt = &record_types[f->record_type_idx];
        int sub_field_idx = find_record_field(f->record_type_idx, token.text);
        if (sub_field_idx == -1) {
            compile_error(token.line, "'%s' is not a field of '%s'", token.text, srt->name);
        }
        for (int k = 0; k < sub_field_idx; k++) {
            extra += srt->fields[k].is_record ? record_type_leaf_count(srt->fields[k].record_type_idx) : 1;
        }
        match(TOKEN_IDENTIFIER);
        f = &srt->fields[sub_field_idx];
    }
    *f_ptr = f;
    return extra;
}

// Resolves ONE '^' step already matched (the caller has confirmed
// is_pointer_type(base's type)) - '.field' if the target is a record,
// nothing more if it's a scalar. Shared by parse_heap_deref_read()/
// parse_heap_deref_write() below - both walk an arbitrary-depth '^'
// chain ('p^.next^.next^.data'), differing only in whether the FINAL
// step becomes a read (NODE_HEAP_FIELD_ACCESS) or is left for the caller
// to build into a write (NODE_HEAP_FIELD_ASSIGN).
//
// For a CLASS specifically (pt->is_class), a name that isn't a field is
// also checked against the class's own methods before giving up - see
// docs/LANGUAGE.md#classes. A method call is always the step's LAST
// possible outcome: it's never itself an assignment target, and its
// result can't be chained into a further '^'/'.' step in v1 (a known
// gap) - both parse_heap_deref_read()/parse_heap_deref_write() below
// return as soon as is_method_call comes back set, never looping again.
// is_statement_context distinguishes the two callers for exactly one
// purpose: a method call used as an EXPRESSION (parse_heap_deref_read)
// must be a function, exactly like any other procedure-used-as-a-value
// rejection elsewhere in this file - a statement-context call
// (parse_heap_deref_write) has no such restriction (calling a function
// and discarding its result is fine, same as an ordinary NODE_CALL).
// has_dot distinguishes an explicit '^.field'/'c.field' access (1 - the
// current token is '.', which this function itself matches before
// reading the field/method name) from the unqualified self-shorthand
// path (0 - see parse_self_shorthand_read()/parse_self_shorthand_write()
// below): there the current token IS the field/method name already,
// with no '.' to match, since the caller confirmed via
// class_has_member() that the bare identifier names a member before
// ever reaching here.
static HeapDerefStep resolve_heap_deref_step(ASTNode *base, int is_statement_context, int has_dot) {
    HeapDerefStep step;
    step.is_method_call = 0;
    step.call_node = NULL;
    step.is_array_field = 0;
    step.array_index = NULL;
    step.is_property_setter = 0;
    step.setter_method_idx = -1;
    DataType base_type = base->expression_type;
    PointerTypeDef *pt = &pointer_types[base_type - TYPE_POINTER_BASE];
    if (pt->target_is_record) {
        if (has_dot) {
            if (token.type != TOKEN_PERIOD) {
                compile_error(token.line, "'...^' is a pointer to a record - access a field, e.g. '...^.field'");
            }
            match(TOKEN_PERIOD);
        }
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "Expected a field name after '^.'");
        }
        int field_idx = find_record_field(pt->target_record_type_idx, token.text);
        if (field_idx == -1 && pt->is_class) {
            char name[MAX_NAME];
            strcpy(name, token.text);
            int name_line = token.line;

            // Property lookup, before the ordinary method lookup below -
            // property names are guaranteed disjoint from field/method
            // names (enforced at property-declaration time in
            // parse_class_declaration()), so the order between this and
            // the method-lookup block below doesn't matter for
            // correctness, only readability.
            int prop_idx = -1;
            for (int i = 0; i < pt->property_count; i++) {
                if (strcmp(pt->properties[i].name, name) == 0) { prop_idx = i; break; }
            }
            if (prop_idx != -1) {
                match(TOKEN_IDENTIFIER);
                ClassProperty *prop = &pt->properties[prop_idx];
                if (prop->is_class_property) {
                    // v1 scope cut: no instance-qualified access to a
                    // TRUE class property - only 'ClassName.Name' (see
                    // build_class_member_access()). Avoids teaching this
                    // function's field-backed read/write branches above
                    // to also understand a second backing-table shape.
                    compile_error(name_line, "'%s' is a class property of '%s' - access it via '%s.%s', not through an instance", name, pt->name, pt->name, name);
                }
                if (prop->is_private && current_class_ptr_idx != prop->declaring_class_ptr_idx) {
                    // The PROPERTY's own visibility gates access here - the
                    // underlying field's/method's own is_private is
                    // deliberately NOT consulted once reached through the
                    // property (a public property may front a private
                    // field/method - standard Delphi convention).
                    compile_error(name_line, "'%s' is a private property of class '%s' and can't be accessed here", name, pointer_types[prop->declaring_class_ptr_idx].name);
                }
                if (prop->is_protected && !class_ptr_idx_is_or_descends_from(current_class_ptr_idx, prop->declaring_class_ptr_idx)) {
                    compile_error(name_line, "'%s' is a protected property of class '%s' and can't be accessed here", name, pointer_types[prop->declaring_class_ptr_idx].name);
                }
                RecordTypeDef *target_rt = &record_types[pt->target_record_type_idx];

                if (is_statement_context) {
                    // A property reached in statement/write context is
                    // always headed for ':=' - there's no "call a property
                    // like a bare procedure" concept, matching how an
                    // ordinary field reached in statement context always
                    // heads for ':=' too (see build_heap_deref_write_statement()).
                    if (!prop->has_write) {
                        compile_error(name_line, "'%s' is a read-only property of class '%s' and can't be assigned to", name, pt->name);
                    }
                    if (prop->write_is_field) {
                        RecordField *f = &target_rt->fields[prop->write_idx];
                        step.field_offset = class_field_base_offset(target_rt, prop->write_idx);
                        step.result_type = f->type;
                        step.is_subrange = f->is_subrange;
                        step.subrange_lower = f->subrange_lower;
                        step.subrange_upper = f->subrange_upper;
                        return step;
                    }
                    step.is_property_setter = 1;
                    step.setter_method_idx = prop->write_idx;
                    step.result_type = prop->type;
                    return step;
                }

                // Read/expression context.
                if (prop->read_is_field) {
                    RecordField *f = &target_rt->fields[prop->read_idx];
                    step.field_offset = class_field_base_offset(target_rt, prop->read_idx);
                    step.result_type = f->type;
                    step.is_subrange = f->is_subrange;
                    step.subrange_lower = f->subrange_lower;
                    step.subrange_upper = f->subrange_upper;
                    return step;
                }
                // Getter - build the call exactly like the ordinary
                // method-call block below does, just keyed by the
                // property's own read_idx. Arity/return-type were already
                // validated to be zero-arg/exact-match at property-
                // declaration time, so parse_class_method_call_arguments()
                // (which tolerates a missing '(' for a zero-arg call) needs
                // no special-casing here.
                ProcParamHeader *rh = &pt->methods[prop->read_idx];
                int mangled_idx = find_proc(rh->mangled_name);
                if (mangled_idx == -1) {
                    compile_error(name_line, "'%s.%s' doesn't have a body yet", pt->name, rh->name);
                }
                ASTNode *call = create_node(NODE_VIRTUAL_CALL);
                call->line = name_line;
                call->data.num_value = prop->read_idx;
                call->expression_type = rh->return_type;
                call->left = base;
                call->right = parse_class_method_call_arguments(mangled_idx);
                step.is_method_call = 1;
                step.call_node = call;
                step.result_type = call->expression_type;
                return step;
            }

            int method_idx = -1;
            for (int i = 0; i < pt->method_count; i++) {
                if (strcmp(pt->methods[i].name, name) == 0) { method_idx = i; break; }
            }
            if (method_idx == -1) {
                compile_error(name_line, "'%s' is not a field or method of class '%s'", name, pt->name);
            }
            match(TOKEN_IDENTIFIER);
            ProcParamHeader *h = &pt->methods[method_idx];
            if (h->is_class_method) {
                // Same v1 scope cut as the class-property guard above -
                // no instance-qualified access to a TRUE class method.
                compile_error(name_line, "'%s' is a class method of '%s' - call it via '%s.%s', not through an instance", name, pt->name, pt->name, name);
            }
            if (h->is_private && current_class_ptr_idx != h->declaring_class_ptr_idx) {
                // Strict private: only the DECLARING class's own methods
                // can call this, not even a subclass's -
                // current_class_ptr_idx is which class's method body is
                // CURRENTLY being parsed (-1 outside any method),
                // matching self-shorthand's own use of it for the same
                // "am I inside this class right now" question.
                compile_error(name_line, "'%s' is a private method of class '%s' and can't be called here", name, pointer_types[h->declaring_class_ptr_idx].name);
            }
            if (h->is_protected && !class_ptr_idx_is_or_descends_from(current_class_ptr_idx, h->declaring_class_ptr_idx)) {
                // 'protected': the declaring class's own methods AND any
                // (transitively) descendant class's methods can call
                // this - class_ptr_idx_is_or_descends_from() walks
                // current_class_ptr_idx's own parent_class_ptr_idx chain
                // looking for h->declaring_class_ptr_idx.
                compile_error(name_line, "'%s' is a protected method of class '%s' and can't be called here", name, pointer_types[h->declaring_class_ptr_idx].name);
            }
            // h->mangled_name - NOT "pt->name__name" - is what actually
            // implements this call STATICALLY, for a class matching
            // base's own declared type exactly - but base's RUNTIME
            // class may be a DESCENDANT that overrides this method, so
            // the actual call is dispatched dynamically instead (see
            // NODE_VIRTUAL_CALL in common.h) - h->mangled_name is used
            // here only to confirm a body exists SOMEWHERE in the
            // hierarchy (a class declaring a method header but never
            // giving ANY class a body for it is still a compile error,
            // just like an ordinary forward-declared procedure).
            // method_idx itself - NOT anything derived from
            // h->mangled_name - is the vtable slot: parse_class_declaration()
            // copies an ancestor's methods[] into every descendant IN
            // ORDER before appending/overriding, so a given logical
            // method's index is already the same across the whole
            // hierarchy, exactly what a stable slot number needs.
            int mangled_idx = find_proc(h->mangled_name);
            if (mangled_idx == -1) {
                compile_error(name_line, "'%s.%s' doesn't have a body yet", pt->name, name);
            }
            if (!is_statement_context && !h->is_function) {
                compile_error(name_line, "'%s' is a procedure and does not return a value; it cannot be used in an expression", name);
            }
            ASTNode *call = create_node(NODE_VIRTUAL_CALL);
            call->line = name_line;
            call->data.num_value = method_idx;
            call->expression_type = h->is_function ? h->return_type : TYPE_UNKNOWN;
            call->left = base; // 'self' - kept separate from the argument list, see NODE_VIRTUAL_CALL's own comment
            call->right = parse_class_method_call_arguments(mangled_idx);
            step.is_method_call = 1;
            step.call_node = call;
            step.result_type = call->expression_type;
            return step;
        }
        if (field_idx == -1) {
            // A class's own name (pt->name), not its hidden backing
            // record's internal "$classN" name - see
            // parse_class_declaration()'s comment on that mangling.
            compile_error(token.line, "'%s' is not a field of '%s'", token.text, pt->is_class ? pt->name : record_types[pt->target_record_type_idx].name);
        }
        int field_line = token.line;
        match(TOKEN_IDENTIFIER);
        RecordTypeDef *target_rt = &record_types[pt->target_record_type_idx];
        RecordField *f = &target_rt->fields[field_idx];
        if (f->is_private && current_class_ptr_idx != f->declaring_class_ptr_idx) {
            // Same strict-private reasoning as the method-call check
            // above - covers every field read AND write, explicit
            // ('c.field') and self-shorthand alike, since both already
            // route through this one shared function.
            compile_error(field_line, "'%s' is a private field of class '%s' and can't be accessed here", f->name, pointer_types[f->declaring_class_ptr_idx].name);
        }
        if (f->is_protected && !class_ptr_idx_is_or_descends_from(current_class_ptr_idx, f->declaring_class_ptr_idx)) {
            // Same reasoning as the protected method-call check above -
            // covers every field read AND write, explicit ('c.field')
            // and self-shorthand alike.
            compile_error(field_line, "'%s' is a protected field of class '%s' and can't be accessed here", f->name, pointer_types[f->declaring_class_ptr_idx].name);
        }
        // For a class: heap offset 0 is the hidden runtime type tag (see
        // parse_class_declaration()'s target_elem_size comment and
        // new()'s tag-write), and a field past the first may itself
        // occupy more than 1 heap slot if it's a nested record -
        // class_field_base_offset() accounts for both. A plain 'type
        // PFoo = ^Target;' pointer has no tag slot and stays scalar-only
        // (nested-record fields are rejected elsewhere for it), so
        // field_idx IS the heap offset there, unchanged.
        int offset;
        if (pt->is_class) {
            offset = class_field_base_offset(target_rt, field_idx);
            if (f->is_record) {
                offset += resolve_class_field_chain_offset(&f); // f becomes the final scalar leaf
            } else if (f->is_array) {
                // An array field ELEMENT access ('c.data[i]') - always
                // terminal (see NODE_HEAP_ARRAY_FIELD_ACCESS in
                // common.h), so this returns immediately rather than
                // falling through to the ordinary scalar/nested-record-
                // leaf step below. The '-array_lower' zero-basing is
                // folded into the SAME immediate as the field's own base
                // offset - see OP_LOAD_HEAP_ARRAY_FIELD's comment.
                if (token.type != TOKEN_LBRACKET) {
                    compile_error(token.line, "Array field '%s' must be indexed, e.g. '%s[i]'", f->name, f->name);
                }
                match(TOKEN_LBRACKET);
                step.array_index = wrap_range_check(expression(), 1, f->array_lower, f->array_upper);
                match(TOKEN_RBRACKET);
                step.is_array_field = 1;
                step.field_offset = offset - f->array_lower;
                step.result_type = f->type;
                step.is_subrange = f->is_subrange;
                step.subrange_lower = f->subrange_lower;
                step.subrange_upper = f->subrange_upper;
                return step;
            }
        } else {
            offset = field_idx;
        }
        step.field_offset = offset;
        step.result_type = f->type;
        step.is_subrange = f->is_subrange;
        step.subrange_lower = f->subrange_lower;
        step.subrange_upper = f->subrange_upper;
    } else {
        step.field_offset = 0;
        step.result_type = pt->target_type;
        step.is_subrange = 0;
        step.subrange_lower = 0;
        step.subrange_upper = 0;
    }
    return step;
}

static ASTNode *make_heap_field_access(ASTNode *base, HeapDerefStep step, int line) {
    ASTNode *node = create_node(NODE_HEAP_FIELD_ACCESS);
    node->line = line;
    node->left = base;
    ASTNode *offset_lit = create_node(NODE_NUMBER);
    offset_lit->data.num_value = step.field_offset;
    offset_lit->expression_type = TYPE_INTEGER;
    node->right = offset_lit;
    node->expression_type = step.result_type;
    return node;
}

// Parses a '^' dereference chain following an already-built pointer-
// typed expression 'base' ('p^', 'p^.field', or a longer chain like
// 'p^.next^.data') for use as an r-value - the caller has already
// confirmed token.type == TOKEN_CARET OR class_dot_deref_pending(base's
// type) (a class variable's IMPLICIT '.field'/'.Method(...)', no '^' -
// see that function's comment). Loops so an arbitrary-depth chain is
// handled uniformly: each step wraps the previous one in a
// NODE_HEAP_FIELD_ACCESS, which becomes 'base' for the next step if the
// field just read is itself pointer-typed and another '^' follows -
// re-validated via is_pointer_type() on every iteration (not just the
// first), so 'x^^' where x^ isn't itself a pointer is a clean Compile
// Error, not an out-of-bounds pointer_types[] read. A class FIELD is
// always scalar in v1 (see docs/LANGUAGE.md#classes), so a field step
// can never itself be followed by another implicit-dot step - only an
// explicit '^' can ever continue a chain past the first step. A method
// CALL is always the terminal step, full stop (its result can't be
// chained into a further '^'/'.' at all yet, even an explicit one) -
// checked here via a function-method returning a value; a procedure-
// method used in an expression is rejected, matching how any other
// procedure-used-as-a-value already is.
static ASTNode *parse_heap_deref_read(ASTNode *base, int line) {
    while (token.type == TOKEN_CARET || class_dot_deref_pending(base->expression_type)) {
        if (!is_pointer_type(base->expression_type)) {
            compile_error(token.line, "Cannot dereference a non-pointer value with '^'");
        }
        if (token.type == TOKEN_CARET) match(TOKEN_CARET);
        HeapDerefStep step = resolve_heap_deref_step(base, 0, 1); // expression context, explicit dot/caret already positioned - resolve_heap_deref_step() itself rejects a procedure-method here
        if (step.is_method_call) {
            return step.call_node;
        }
        if (step.is_array_field) {
            // Terminal, like a method call - must return here rather
            // than fall through to make_heap_field_access() below, since
            // step.field_offset here is the COMBINED offset (field base
            // offset - array lower bound), not a plain field offset.
            ASTNode *node = create_node(NODE_HEAP_ARRAY_FIELD_ACCESS);
            node->line = line;
            node->left = base;
            node->right = step.array_index;
            node->data.num_value = step.field_offset;
            node->expression_type = step.result_type;
            // Same procedural-typed-field-followed-by-'(' call check the
            // bottom of this function does for a plain field - an array
            // field is terminal either way, so this can't just fall
            // through to that shared check below.
            if (is_proc_type(node->expression_type) && token.type == TOKEN_LPAREN) {
                return build_procvar_call(node, node->expression_type - TYPE_PROC_BASE, line, 0);
            }
            return node;
        }
        base = make_heap_field_access(base, step, line);
    }
    // A procedural-typed field followed by '(' is a CALL through the
    // stored value - same "bare reference vs. call" convention every
    // other procedural-type read already follows (see the NAMED-
    // procedural-type-global case this mirrors), just reached via a
    // heap field instead of a plain variable. build_procvar_call() only
    // needs an already-built expression that reads the value (here,
    // 'base' itself - a NODE_HEAP_FIELD_ACCESS, or the original pointer
    // expression if the loop above never ran), same as it already
    // accepts for a plain global/local/var-param read.
    if (is_proc_type(base->expression_type) && token.type == TOKEN_LPAREN) {
        return build_procvar_call(base, base->expression_type - TYPE_PROC_BASE, line, 0);
    }
    return base;
}

// Same chain-walking as parse_heap_deref_read() above, but stops right
// before consuming the FINAL field step - a write needs to build a
// NODE_HEAP_FIELD_ASSIGN for that last step (base + field_offset + value
// expression), not another NODE_HEAP_FIELD_ACCESS. Assumes the caller has
// already confirmed the same entry condition parse_heap_deref_read()
// does. Returns the base expression the LAST step reads through, and
// that step's own HeapDerefStep (field offset/type/subrange info) via
// *out_step - UNLESS the step turns out to be a method CALL rather than
// a field: a call is never an assignment target ('c.Method() := x'
// makes no sense), so it's necessarily a complete STATEMENT on its own
// (e.g. 'c.SetRadius(2.0);', no ':=' anywhere) - out_step->is_method_call
// is set, out_step->call_node holds that complete statement (op already
// set to TOKEN_PROCEDURE, discarding an unused function result, exactly
// like any other call-statement), and the return value is NULL and
// must not be used. Every caller of this function checks
// out_step->is_method_call first, before doing anything else with the
// return value.
static ASTNode *parse_heap_deref_write(ASTNode *base, int line, HeapDerefStep *out_step) {
    for (;;) {
        if (!is_pointer_type(base->expression_type)) {
            compile_error(token.line, "Cannot dereference a non-pointer value with '^'");
        }
        if (token.type == TOKEN_CARET) match(TOKEN_CARET);
        HeapDerefStep step = resolve_heap_deref_step(base, 1, 1); // statement context, explicit dot/caret already positioned
        if (step.is_method_call) {
            step.call_node->op = TOKEN_PROCEDURE; // statement context: discard an unused function result
            *out_step = step;
            return NULL;
        }
        if (step.is_property_setter) {
            // Terminal, like a method call - build_property_setter_call()
            // (the caller's job, see its own comment) still needs 'base'
            // itself (unlike is_method_call, where the call node already
            // has it spliced in), so return it rather than NULL.
            *out_step = step;
            return base;
        }
        if (step.is_array_field) {
            // Terminal, like a method call - must return here, BEFORE
            // the '^'-continuation check below, since step.field_offset
            // here is the COMBINED offset (field base offset - array
            // lower bound), not a plain field offset make_heap_field_
            // access() could safely use.
            *out_step = step;
            return base;
        }
        if (token.type == TOKEN_CARET) {
            base = make_heap_field_access(base, step, line);
            continue;
        }
        *out_step = step;
        return base;
    }
}

// Builds the final write statement for an already-resolved heap-deref
// chain - shared by every '^'/'.'-access assignment site (a global,
// local, var-param, or self-shorthand pointer variable). Parses ':='
// and the value expression, then returns either a NODE_HEAP_FIELD_ASSIGN
// (an ordinary scalar/nested-record leaf) or a NODE_HEAP_ARRAY_FIELD_ASSIGN
// (step.is_array_field set), depending on what resolve_heap_deref_step()
// resolved. Every call site already checked step.is_method_call itself
// first - a method call is a complete statement on its own, never
// reaching here.
static ASTNode *build_heap_deref_write_statement(ASTNode *base, HeapDerefStep step) {
    match(TOKEN_ASSIGN);
    if (step.is_array_field) {
        ASTNode *stmt = create_node(NODE_HEAP_ARRAY_FIELD_ASSIGN);
        stmt->left = base;
        stmt->extra = step.array_index;
        // A procedural-typed field needs the same specialized parser
        // every other procedural-type assignment target already uses -
        // the generic expression() below would misparse a bare proc name
        // as a zero-argument CALL to it, not a reference (see
        // docs/LANGUAGE.md#procedural-types).
        stmt->right = step.result_type >= TYPE_PROC_BASE
            ? parse_proc_value(step.result_type - TYPE_PROC_BASE, token.line)
            : wrap_range_check(expression(), step.is_subrange, step.subrange_lower, step.subrange_upper);
        stmt->data.num_value = step.field_offset;
        stmt->expression_type = step.result_type;
        return stmt;
    }
    ASTNode *stmt = create_node(NODE_HEAP_FIELD_ASSIGN);
    stmt->left = base;
    stmt->right = step.result_type >= TYPE_PROC_BASE
        ? parse_proc_value(step.result_type - TYPE_PROC_BASE, token.line)
        : wrap_range_check(expression(), step.is_subrange, step.subrange_lower, step.subrange_upper);
    ASTNode *offset_lit = create_node(NODE_NUMBER);
    offset_lit->data.num_value = step.field_offset;
    offset_lit->expression_type = TYPE_INTEGER;
    stmt->extra = offset_lit;
    stmt->expression_type = step.result_type;
    return stmt;
}

// Builds the NODE_VIRTUAL_CALL for a property write whose write target is a
// setter PROCEDURE (step.is_property_setter) - the write-context twin of
// build_heap_deref_write_statement() for the ordinary-field-terminal case.
// Parses ':=' and the value expression itself (the setter's one argument),
// exactly like build_heap_deref_write_statement() does for a plain field -
// this can't reuse parse_class_method_call_arguments() (it expects
// '(args)' immediately after the name, not a ':=' and a bare expression
// appearing afterward).
static ASTNode *build_property_setter_call(ASTNode *base, HeapDerefStep step, int line) {
    PointerTypeDef *pt = &pointer_types[base->expression_type - TYPE_POINTER_BASE];
    ProcParamHeader *h = &pt->methods[step.setter_method_idx];
    int mangled_idx = find_proc(h->mangled_name);
    if (mangled_idx == -1) {
        compile_error(line, "'%s.%s' doesn't have a body yet", pt->name, h->name);
    }
    match(TOKEN_ASSIGN);
    // slot 1, not 0: the mangled setter's own param_count includes 'self'
    // at slot 0 (see parse_class_method_call_arguments()'s own comment) -
    // the setter's single user-visible parameter is always slot 1, since
    // property declaration time already validated it takes exactly one.
    ASTNode *value = step.result_type >= TYPE_PROC_BASE
        ? parse_proc_value(step.result_type - TYPE_PROC_BASE, token.line)
        : expression();
    if (step.result_type < TYPE_PROC_BASE) {
        DataType expected = step.result_type;
        DataType actual = value->expression_type;
        int expected_stringy = (expected == TYPE_STRING || expected == TYPE_CHAR);
        int actual_stringy = (actual == TYPE_STRING || actual == TYPE_CHAR);
        if (!(expected_stringy && actual_stringy) && expected != actual) {
            if (actual == TYPE_INTEGER && expected == TYPE_REAL) {
                // Implicit int->real widening, matching an ordinary
                // assignment's own rule (try_widen_for_assignment() in
                // type_checker.c) - duplicated narrowly here, the same way
                // parse_indirect_call()/build_procvar_call() already do,
                // since NODE_VIRTUAL_CALL has no type_checker.c case for
                // this to defer to (see docs/ROADMAP.md's Properties entry).
                ASTNode *wrapper = create_node(NODE_INT_TO_REAL);
                wrapper->left = value;
                wrapper->expression_type = TYPE_REAL;
                wrapper->line = value->line;
                value = wrapper;
            } else {
                compile_error(line, "Cannot assign this expression to property '%s' - wrong type", h->name);
            }
        }
        value = wrap_range_check(value, proc_table[mangled_idx].param_is_subrange[1],
            proc_table[mangled_idx].param_subrange_lower[1], proc_table[mangled_idx].param_subrange_upper[1]);
    }

    ASTNode *call = create_node(NODE_VIRTUAL_CALL);
    call->line = line;
    call->data.num_value = step.setter_method_idx;
    call->expression_type = TYPE_UNKNOWN; // a setter is always a procedure
    call->left = base;
    call->right = value;
    call->op = TOKEN_PROCEDURE; // statement context - matches every other call-statement's own convention
    return call;
}

// Shared widen+range-check helper for build_class_member_access() below -
// factors out the int->real widening / exact-type-match check
// build_property_setter_call() above already established as this
// codebase's own pattern for "a value about to be stored where
// type_checker.c has no case to defer to" (NODE_CALL/NODE_ASSIGN
// targeting a mangled class-member global/proc have no dedicated
// type_checker.c case either - see docs/ROADMAP.md's Properties entry
// for why). Kept local to this one new function (not exported/reused
// Resolves a bare class-member name (a class var, a TRUE class method,
// or a TRUE class property - Delphi terminology; see ProcParamHeader.
// is_class_method's own comment for why "class method" needs saying
// explicitly here) of class_ptr_idx into a complete, SELF-FREE AST node.
// Shared by both callers that need this: try_resolve_class_qualified_
// access() (after consuming 'ClassName.', for 'TMyClass.Foo' access) and
// the self-shorthand path in parse_self_shorthand_read()/write() (after
// consuming nothing extra, for bare 'Foo' access from inside a method
// body) - mirrors resolve_heap_deref_step()'s own single-function-two-
// calling-contexts shape. Assumes the CURRENT token is already the bare
// member-name identifier (not yet matched). Resolves against
// class_vars[] -> is_class_method-flagged methods[] -> is_class_property-
// flagged properties[], in that order (mirrors class_has_member()'s own
// field->property->method order) - the three tables/flags are already
// guaranteed to share one flat, collision-checked namespace (see
// parse_class_declaration()'s own duplicate-name checks), so this
// function never needs to worry about a name matching more than one.
static ASTNode *build_class_member_access(int class_ptr_idx, int is_statement_context) {
    PointerTypeDef *pt = &pointer_types[class_ptr_idx];
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Expected a class member name");
    }
    char name[MAX_NAME];
    int name_line = token.line;
    strcpy(name, token.text);

    int cvi = -1;
    for (int i = 0; i < pt->class_var_count; i++) {
        if (strcmp(pt->class_vars[i].name, name) == 0) { cvi = i; break; }
    }
    if (cvi != -1) {
        match(TOKEN_IDENTIFIER);
        ClassVar *cv = &pt->class_vars[cvi];
        if (cv->is_private && current_class_ptr_idx != cv->declaring_class_ptr_idx) {
            compile_error(name_line, "'%s' is a private class variable of class '%s' and can't be accessed here", name, pointer_types[cv->declaring_class_ptr_idx].name);
        }
        if (cv->is_protected && !class_ptr_idx_is_or_descends_from(current_class_ptr_idx, cv->declaring_class_ptr_idx)) {
            compile_error(name_line, "'%s' is a protected class variable of class '%s' and can't be accessed here", name, pointer_types[cv->declaring_class_ptr_idx].name);
        }
        if (is_statement_context) {
            match(TOKEN_ASSIGN);
            ASTNode *value = wrap_range_check(expression(), cv->is_subrange, cv->subrange_lower, cv->subrange_upper);
            ASTNode *assign = create_node(NODE_ASSIGN);
            assign->line = name_line;
            assign->data.var_idx = cv->sym_idx;
            assign->left = value;
            return assign;
        }
        ASTNode *node = create_node(NODE_VARIABLE);
        node->line = name_line;
        node->data.var_idx = cv->sym_idx;
        node->expression_type = cv->type;
        return node;
    }

    int mi = -1;
    for (int i = 0; i < pt->method_count; i++) {
        if (pt->methods[i].is_class_method && strcmp(pt->methods[i].name, name) == 0) { mi = i; break; }
    }
    if (mi != -1) {
        match(TOKEN_IDENTIFIER);
        ProcParamHeader *h = &pt->methods[mi];
        if (h->is_private && current_class_ptr_idx != h->declaring_class_ptr_idx) {
            compile_error(name_line, "'%s' is a private class method of class '%s' and can't be called here", name, pointer_types[h->declaring_class_ptr_idx].name);
        }
        if (h->is_protected && !class_ptr_idx_is_or_descends_from(current_class_ptr_idx, h->declaring_class_ptr_idx)) {
            compile_error(name_line, "'%s' is a protected class method of class '%s' and can't be called here", name, pointer_types[h->declaring_class_ptr_idx].name);
        }
        int mangled_idx = find_proc(h->mangled_name);
        if (mangled_idx == -1) {
            compile_error(name_line, "'%s.%s' doesn't have a body yet", pt->name, name);
        }
        if (!is_statement_context && !h->is_function) {
            compile_error(name_line, "'%s' is a procedure and does not return a value; it cannot be used in an expression", name);
        }
        ASTNode *call = create_node(NODE_CALL);
        call->line = name_line;
        call->data.var_idx = mangled_idx;
        call->expression_type = h->is_function ? h->return_type : TYPE_UNKNOWN;
        call->left = parse_call_arguments(mangled_idx);
        if (is_statement_context) call->op = TOKEN_PROCEDURE;
        return call;
    }

    int pi = -1;
    for (int i = 0; i < pt->property_count; i++) {
        if (pt->properties[i].is_class_property && strcmp(pt->properties[i].name, name) == 0) { pi = i; break; }
    }
    if (pi != -1) {
        match(TOKEN_IDENTIFIER);
        ClassProperty *prop = &pt->properties[pi];
        if (prop->is_private && current_class_ptr_idx != prop->declaring_class_ptr_idx) {
            compile_error(name_line, "'%s' is a private class property of class '%s' and can't be accessed here", name, pointer_types[prop->declaring_class_ptr_idx].name);
        }
        if (prop->is_protected && !class_ptr_idx_is_or_descends_from(current_class_ptr_idx, prop->declaring_class_ptr_idx)) {
            compile_error(name_line, "'%s' is a protected class property of class '%s' and can't be accessed here", name, pointer_types[prop->declaring_class_ptr_idx].name);
        }
        if (is_statement_context) {
            if (!prop->has_write) {
                compile_error(name_line, "'%s' is a read-only class property of class '%s' and can't be assigned to", name, pt->name);
            }
            if (prop->write_is_field) {
                ClassVar *cv = &pt->class_vars[prop->write_idx];
                match(TOKEN_ASSIGN);
                ASTNode *value = wrap_range_check(expression(), cv->is_subrange, cv->subrange_lower, cv->subrange_upper);
                ASTNode *assign = create_node(NODE_ASSIGN);
                assign->line = name_line;
                assign->data.var_idx = cv->sym_idx;
                assign->left = value;
                return assign;
            }
            // Setter class method - its mangled proc has NO 'self' at
            // slot 0 (see parse_class_method_body()'s own skip), so the
            // one user-visible parameter is slot 0, not slot 1 the way
            // build_property_setter_call()'s instance-method setter
            // needs above.
            ProcParamHeader *h = &pt->methods[prop->write_idx];
            int mangled_idx = find_proc(h->mangled_name);
            if (mangled_idx == -1) {
                compile_error(name_line, "'%s.%s' doesn't have a body yet", pt->name, h->name);
            }
            match(TOKEN_ASSIGN);
            ASTNode *value = wrap_range_check(expression(), proc_table[mangled_idx].param_is_subrange[0],
                proc_table[mangled_idx].param_subrange_lower[0], proc_table[mangled_idx].param_subrange_upper[0]);
            ASTNode *call = create_node(NODE_CALL);
            call->line = name_line;
            call->data.var_idx = mangled_idx;
            call->expression_type = TYPE_UNKNOWN;
            call->left = value; // parse_call_arguments() builds a ->next-chained arg list; a single node is already a valid one-element list
            call->op = TOKEN_PROCEDURE;
            return call;
        }
        // Read/expression context.
        if (prop->read_is_field) {
            ClassVar *cv = &pt->class_vars[prop->read_idx];
            ASTNode *node = create_node(NODE_VARIABLE);
            node->line = name_line;
            node->data.var_idx = cv->sym_idx;
            node->expression_type = cv->type;
            return node;
        }
        // Getter class method - arity/return-type were already validated
        // to be zero-arg/exact-match at property-declaration time, so
        // parse_call_arguments() (which tolerates a missing '(' for a
        // zero-arg call) needs no special-casing here.
        ProcParamHeader *h = &pt->methods[prop->read_idx];
        int mangled_idx = find_proc(h->mangled_name);
        if (mangled_idx == -1) {
            compile_error(name_line, "'%s.%s' doesn't have a body yet", pt->name, h->name);
        }
        ASTNode *call = create_node(NODE_CALL);
        call->line = name_line;
        call->data.var_idx = mangled_idx;
        call->expression_type = h->return_type;
        call->left = parse_call_arguments(mangled_idx);
        return call;
    }

    compile_error(name_line, "'%s' is not a class member of '%s'", name, pt->name);
    return NULL; // unreachable
}

// Disambiguates a bare leading identifier that STARTS an expression
// (factor()) or a statement (statement()) between "an ordinary
// variable/const/proc reference" (the overwhelmingly common case, left
// completely untouched) and "TMyClass.Foo" - a CLASS TYPE NAME used as a
// qualifier, at the exact same syntactic position an ordinary variable
// reference already occupies. Unlike is/as's own class-name parsing
// (which sits in an unambiguous position right after 'is'/'as' has
// already been consumed, on a LATER token factor() never even sees - see
// expression()'s own TOKEN_IS/TOKEN_AS handling), the parser does NOT
// yet know, at the moment it sees THIS bare identifier, which
// interpretation is coming - class names and variable names live in
// separate, non-cross-checked namespaces in this compiler (nothing stops
// a variable sharing a name with a class), so a genuine one-token
// lookahead (save/restore the lexer position, same primitives the field
// loop's own TOKEN_CLASS branch already uses) is required, not just a
// name match. Returns NULL (falls through to the caller's own,
// completely unchanged existing resolution chain) unless the identifier
// is a known class type name IMMEDIATELY followed by '.' -
// class-qualified interpretation wins whenever both hold, a deliberate
// precedence rule documented in docs/LANGUAGE.md.
static ASTNode *try_resolve_class_qualified_access(int is_statement_context) {
    int class_idx = find_pointer_type(token.text);
    if (class_idx == -1 || !pointer_types[class_idx].is_class) {
        return NULL; // cheap, non-disruptive check for the overwhelmingly common case
    }
    Token saved_token = token;
    LexerPos saved_pos = lexer_save_pos();
    next_token(); // peek past the class name
    if (token.type != TOKEN_PERIOD) {
        token = saved_token;
        lexer_restore_pos(saved_pos);
        return NULL;
    }
    match(TOKEN_PERIOD);
    return build_class_member_access(class_idx, is_statement_context);
}

// Same lookahead idea as try_resolve_class_qualified_access() just
// above (a bare identifier is ambiguous between "a pointer type name"
// and "a variable/function name" until the token AFTER it is known),
// but for '(' instead of '.' - 'PFoo(genericPtr)', an explicit, always-
// unchecked pointer-type cast, needed to make a Pointer value usable
// again after @/Addr or an implicit widening FROM a specific pointer
// type (see try_widen_for_assignment()'s own comment in type_checker.c
// for why the reverse direction isn't implicit). Deliberately NOT built
// on 'is'/'as' (TOKEN_IS/TOKEN_AS elsewhere in this file) - those
// perform a genuine runtime check against a class instance's own type
// tag, which is meaningless here: a Pointer carries no runtime type
// information to check against at all, so this is a pure, always-
// succeeding, compile-time relabeling of an already-identical runtime
// int (closer to a C-style '(PFoo)ptr' cast than a checked downcast) -
// applies to ANY pointer type, not just classes, unlike is/as.
static ASTNode *try_resolve_pointer_cast(void) {
    int ptr_idx = find_pointer_type(token.text);
    if (ptr_idx == -1) {
        return NULL; // cheap, non-disruptive check for the overwhelmingly common case
    }
    Token saved_token = token;
    LexerPos saved_pos = lexer_save_pos();
    int line = token.line;
    next_token(); // peek past the pointer type name
    if (token.type != TOKEN_LPAREN) {
        token = saved_token;
        lexer_restore_pos(saved_pos);
        return NULL;
    }
    match(TOKEN_LPAREN);
    ASTNode *arg = expression();
    match(TOKEN_RPAREN);
    if (arg->expression_type != TYPE_UNTYPED_POINTER && !is_pointer_type(arg->expression_type)) {
        compile_error(line, "'%s(...)' expects a pointer or Pointer-typed argument", pointer_types[ptr_idx].name);
    }
    // A pass-through, not a new node - the underlying runtime int is
    // identical either way (every pointer type shares the exact same
    // representation - see TYPE_POINTER_BASE's own comment in
    // common.h), so retyping the already-built expression is the entire
    // cast.
    arg->expression_type = (DataType)(TYPE_POINTER_BASE + ptr_idx);
    // 'PFoo(generic)^' / 'PFoo(generic)^.field' - a '^'/'.' chain may
    // immediately follow the cast, exactly like it can follow any other
    // already-resolved pointer-typed expression (see the same check
    // repeated at every other pointer-typed factor() resolution site in
    // this file, e.g. the plain-variable case just below).
    if (token.type == TOKEN_CARET || class_dot_deref_pending(arg->expression_type)) {
        return parse_heap_deref_read(arg, line);
    }
    return arg;
}

// Builds the synthetic 'self'-read node any unqualified self-shorthand
// access starts from - the same NODE_LOCAL_VAR shape an explicit 'self'
// reference resolves to via the ordinary local lookup, just built
// directly since the caller already knows (via class_has_member()) that
// the bare identifier it's about to resolve names a member, not 'self'
// itself. Looked up via find_local_outward() rather than assumed to be
// local slot 0, so this stays correct regardless of exactly which slot
// register_class_method_param() gave 'self'.
static ASTNode *build_self_reference_node(int line) {
    int levels_up;
    int local_idx = find_local_outward("self", &levels_up);
    ASTNode *node = create_node(NODE_LOCAL_VAR);
    node->line = line;
    node->data.var_idx = local_idx;
    node->op = (TokenType)levels_up;
    node->expression_type = (DataType)(TYPE_POINTER_BASE + current_class_ptr_idx);
    return node;
}

// Builds a read of the enclosing method's own already-declared parameter
// at proc_table[current_proc_idx].param_names[slot] (slot 1..param_count-1,
// slot 0 being self) - the same 5-line "look up a local by name, branch
// on is_var_param" shape used at every other local-read site (e.g. the
// procedural-parameter-by-name lookup a few hundred lines up, and
// build_self_reference_node() itself for slot 0). Used only by
// parse_inherited_call()'s bare 'inherited;' form, to forward the
// current method's own arguments unchanged.
static ASTNode *build_local_param_read(const char *name, int line) {
    int levels_up;
    int local_idx = find_local_outward(name, &levels_up);
    LocalSymbol *ls = local_at(local_idx, levels_up);
    ASTNode *node = create_node(ls->is_var_param ? NODE_VAR_PARAM_READ : NODE_LOCAL_VAR);
    node->line = line;
    node->data.var_idx = local_idx;
    node->op = (TokenType)levels_up;
    node->expression_type = ls->type;
    return node;
}

// 'inherited;' / 'inherited MethodName;' / 'inherited MethodName(args);' -
// a DIRECT (non-virtual) call to the enclosing method's class's PARENT's
// implementation of a method - see NODE_INHERITED_CALL in common.h.
// Unlike an ordinary 'c.Method(args)' call, the target is fully resolved
// at COMPILE time: inheritance is already flattened (parent's methods[]
// already reflects whichever ancestor actually implements a given name,
// however many levels up), so no vtable slot/dynamic dispatch is needed
// at all - see the codegen.c case.
//
// No identifier after 'inherited' means "the same method currently
// executing, with its own parameters forwarded unchanged" - always
// well-typed, since an override is required to match its inherited
// signature exactly (docs/LANGUAGE.md#classes), so the currently
// executing method's own parameter list is guaranteed identical to
// whatever it's calling into on the parent.
static ASTNode *parse_inherited_call(int is_statement_context) {
    int line = token.line;
    match(TOKEN_INHERITED);

    if (current_class_ptr_idx == -1) {
        compile_error(line, "'inherited' can only be used inside a class method body");
    }
    if (current_method_is_class_method) {
        // TRUE class methods (Delphi terminology - see ProcParamHeader.
        // is_class_method) are never overridden (no vtable slot, ever -
        // see build_vtable_init_chain()'s skip and the method-loop's own
        // override-eligibility check), so there's no ancestor
        // implementation for 'inherited' to reach.
        compile_error(line, "'inherited' can't be used inside a class method body - class methods are never overridden");
    }
    PointerTypeDef *cls = &pointer_types[current_class_ptr_idx];
    if (cls->parent_class_ptr_idx == -1) {
        compile_error(line, "class '%s' has no parent class - 'inherited' can't be used here", cls->name);
    }
    PointerTypeDef *parent = &pointer_types[cls->parent_class_ptr_idx];

    char method_name[MAX_NAME];
    int forwarding = (token.type != TOKEN_IDENTIFIER);
    if (forwarding) {
        strcpy(method_name, proc_table[current_proc_idx].unmangled_name);
    } else {
        strcpy(method_name, token.text);
        match(TOKEN_IDENTIFIER);
    }

    int method_idx = -1;
    for (int i = 0; i < parent->method_count; i++) {
        if (strcmp(parent->methods[i].name, method_name) == 0) { method_idx = i; break; }
    }
    if (method_idx == -1) {
        if (forwarding) {
            compile_error(line, "'%s' doesn't override an inherited method - '%s' has no method '%s' (use 'inherited MethodName(...)' to call a differently-named one)",
                           proc_table[current_proc_idx].unmangled_name, parent->name, method_name);
        }
        compile_error(line, "'%s' is not a declared method of '%s' (or any of its ancestors)", method_name, parent->name);
    }
    ProcParamHeader *h = &parent->methods[method_idx];
    if (h->is_class_method) {
        compile_error(line, "'inherited' can only reach an instance method - '%s' is a class method; call '%s.%s' directly instead", method_name, parent->name, method_name);
    }
    if (h->is_abstract) {
        // MUST be checked explicitly, before find_proc() below -
        // register_abstract_method_signature() gave this method a
        // phantom proc_table[] entry, so find_proc() would otherwise
        // succeed here too, letting 'inherited AbstractMethod' compile
        // into a real, STATICALLY resolved NODE_INHERITED_CALL
        // (unlike an ordinary method call's NODE_VIRTUAL_CALL, this is
        // a direct OP_CALL backpatched straight to the phantom's own
        // entry_address - see codegen.c) that would actually run the
        // near-empty stub at runtime and return garbage for a
        // function, instead of being caught here at compile time.
        compile_error(line, "'%s.%s' is abstract and has no implementation to call via 'inherited'", parent->name, method_name);
    }
    int mangled_idx = find_proc(h->mangled_name);
    if (mangled_idx == -1) {
        compile_error(line, "'%s.%s' doesn't have a body yet", parent->name, method_name);
    }
    if (!is_statement_context && !h->is_function) {
        compile_error(line, "'%s' is a procedure and does not return a value; it cannot be used in an expression", method_name);
    }

    ASTNode *call = create_node(NODE_INHERITED_CALL);
    call->line = line;
    call->data.var_idx = mangled_idx;
    call->expression_type = h->is_function ? h->return_type : TYPE_UNKNOWN;
    call->left = build_self_reference_node(line);

    if (forwarding) {
        ASTNode *arg_head = NULL;
        ASTNode *arg_tail = NULL;
        for (int i = 1; i < proc_table[current_proc_idx].param_count; i++) {
            ASTNode *arg = build_local_param_read(proc_table[current_proc_idx].param_names[i], line);
            if (!arg_head) arg_head = arg; else arg_tail->next = arg;
            arg_tail = arg;
        }
        call->right = arg_head;
    } else {
        call->right = parse_class_method_call_arguments(mangled_idx);
    }
    return call;
}

// Whether 'name' names a CLASS-shaped member (a class var, a TRUE class
// method, or a TRUE class property) of class_ptr_idx - as opposed to an
// ordinary instance field/method/property. Used by the self-shorthand
// read/write functions below to decide, BEFORE ever building a 'self'
// reference, whether a bare name should route to build_class_member_
// access() (self-free) instead - critical inside a class method body,
// which has no 'self' local at all (see parse_class_method_body()'s own
// skip) to build in the first place.
static int class_ptr_has_class_shaped_member(int class_ptr_idx, const char *name) {
    PointerTypeDef *pt = &pointer_types[class_ptr_idx];
    for (int i = 0; i < pt->class_var_count; i++) {
        if (strcmp(pt->class_vars[i].name, name) == 0) return 1;
    }
    for (int i = 0; i < pt->method_count; i++) {
        if (pt->methods[i].is_class_method && strcmp(pt->methods[i].name, name) == 0) return 1;
    }
    for (int i = 0; i < pt->property_count; i++) {
        if (pt->properties[i].is_class_property && strcmp(pt->properties[i].name, name) == 0) return 1;
    }
    return 0;
}

// Read-context (expression) self-shorthand: the current token is a bare
// identifier already confirmed, by the caller via class_has_member(), to
// name a field or method of the enclosing method's class - resolves it
// as if 'self.' had been written, through the same resolve_heap_deref_step()
// every explicit 'self.x'/'c.x' access already goes through, just with
// has_dot=0 since there's no '.' here to consume. A field result is
// handed off to the unmodified parse_heap_deref_read() for any further
// explicit '^' chain (e.g. a shorthand pointer field followed by
// '^.field') - shorthand only changes how the FIRST step is found, never
// how a chain continues past it.
static ASTNode *parse_self_shorthand_read(int line) {
    if (class_ptr_has_class_shaped_member(current_class_ptr_idx, token.text)) {
        return build_class_member_access(current_class_ptr_idx, 0);
    }
    if (current_method_is_class_method) {
        compile_error(token.line, "cannot access instance member '%s' from a class method - no instance available", token.text);
    }
    ASTNode *base = build_self_reference_node(line);
    HeapDerefStep step = resolve_heap_deref_step(base, 0, 0); // expression context, no explicit dot to consume
    if (step.is_method_call) {
        return step.call_node;
    }
    if (step.is_array_field) {
        // Terminal, like a method call - must return here rather than
        // fall through to make_heap_field_access() below, since
        // step.field_offset here is the COMBINED offset (field base
        // offset - array lower bound), not a plain field offset.
        ASTNode *node = create_node(NODE_HEAP_ARRAY_FIELD_ACCESS);
        node->line = line;
        node->left = base;
        node->right = step.array_index;
        node->data.num_value = step.field_offset;
        node->expression_type = step.result_type;
        // Same procedural-typed-field-followed-by-'(' call check
        // parse_heap_deref_read() makes for its own array-field branch -
        // see that comment.
        if (is_proc_type(node->expression_type) && token.type == TOKEN_LPAREN) {
            return build_procvar_call(node, node->expression_type - TYPE_PROC_BASE, line, 0);
        }
        return node;
    }
    return parse_heap_deref_read(make_heap_field_access(base, step, line), line);
}

// Write/statement-context twin of parse_self_shorthand_read() above -
// mirrors the inline pattern every explicit pointer-typed local/var-param
// write site already uses (resolve one step; either a terminal
// method-call statement, or a NODE_HEAP_FIELD_ASSIGN target). If a '^'
// follows the shorthand field (further chaining), delegates to the
// unmodified parse_heap_deref_write() for the rest, exactly like
// parse_self_shorthand_read() delegates to parse_heap_deref_read().
static ASTNode *parse_self_shorthand_write(void) {
    int line = token.line;
    if (class_ptr_has_class_shaped_member(current_class_ptr_idx, token.text)) {
        return build_class_member_access(current_class_ptr_idx, 1);
    }
    if (current_method_is_class_method) {
        compile_error(token.line, "cannot access instance member '%s' from a class method - no instance available", token.text);
    }
    ASTNode *base = build_self_reference_node(line);
    HeapDerefStep step = resolve_heap_deref_step(base, 1, 0); // statement context, no explicit dot to consume
    if (step.is_method_call) {
        step.call_node->op = TOKEN_PROCEDURE; // statement context: discard an unused function result
        return step.call_node;
    }
    if (step.is_property_setter) {
        return build_property_setter_call(base, step, line);
    }
    if (!step.is_array_field && token.type == TOKEN_CARET) {
        base = parse_heap_deref_write(make_heap_field_access(base, step, line), line, &step);
        if (step.is_method_call) {
            return step.call_node;
        }
        if (step.is_property_setter) {
            // Defensive - a setter step is terminal, so this second check
            // should never actually fire (mirrors the same double-check
            // pattern for is_method_call just above), but keeps this
            // function correct if that invariant ever changes.
            return build_property_setter_call(base, step, line);
        }
    }
    return build_heap_deref_write_statement(base, step);
}

// Given an already-resolved GLOBAL symbol index, parses whatever follows
// (array indexing, 2D array indexing, string indexing) and builds the
// appropriate expression node. Shared between plain global-variable
// resolution and record field access - a record field is just a global
// symbol under a mangled name, so once its index is resolved, everything
// past that point (is it an array? a string that can be indexed? a plain
// scalar?) is identical to resolving any other global variable.
// Parses '[index].field' for an array-of-records already resolved to
// arr_sym_idx (whether a true global or a local's hidden mangled global -
// see is_record_array in common.h) - '[' has already been matched by the
// caller. Shared between parse_global_symbol_reference() below and the
// local-array read path in factor(), the two places a plain array
// identifier's '[' gets matched on the read side.
static ASTNode *parse_record_array_field_read(int arr_sym_idx, int line) {
    ASTNode *index_expr = expression();
    match(TOKEN_RBRACKET);
    if (token.type != TOKEN_PERIOD) {
        compile_error(token.line, "'%s' is an array of records - index it, then access a field, e.g. '%s[i].field'",
                      sym_table[arr_sym_idx].name, sym_table[arr_sym_idx].name);
    }
    match(TOKEN_PERIOD);
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Expected a field name after '%s[...].'", sym_table[arr_sym_idx].name);
    }
    int record_type_idx = find_record_array_type(arr_sym_idx);
    int field_idx = find_record_field(record_type_idx, token.text);
    if (field_idx == -1) {
        compile_error(token.line, "'%s' is not a field of '%s'", token.text, record_types[record_type_idx].name);
    }
    match(TOKEN_IDENTIFIER);
    RecordField *f = &record_types[record_type_idx].fields[field_idx];
    ASTNode *node = create_node(NODE_ARRAY_RECORD_FIELD_ACCESS);
    node->line = line;
    node->data.var_idx = arr_sym_idx;
    node->left = index_expr;
    ASTNode *offset_lit = create_node(NODE_NUMBER);
    offset_lit->data.num_value = field_idx;
    offset_lit->expression_type = TYPE_INTEGER;
    node->right = offset_lit;
    node->expression_type = f->type;
    return node;
}

static ASTNode *parse_global_symbol_reference(int idx, int line) {
    if (sym_table[idx].is_array) {
        if (token.type != TOKEN_LBRACKET) {
            compile_error(token.line, "Array '%s' must be indexed", sym_table[idx].name);
        }
        match(TOKEN_LBRACKET);
        if (sym_table[idx].is_record_array) {
            return parse_record_array_field_read(idx, line);
        }
        if (sym_table[idx].is_nd) {
            ASTNode *node = create_node(NODE_ARRAY_ACCESS_ND);
            node->line = line;
            node->data.var_idx = idx;
            node->left = parse_nd_index_list(sym_table[idx].nd_dims); // consumes ']' itself
            node->expression_type = sym_table[idx].type;
            return node;
        }
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
        if (is_dynarray_type(sym_table[idx].type)) {
            match(TOKEN_LBRACKET);
            ASTNode *base = create_node(NODE_VARIABLE);
            base->line = line;
            base->data.var_idx = idx;
            base->expression_type = sym_table[idx].type;
            ASTNode *node = create_node(NODE_DYNARRAY_ACCESS);
            node->line = line;
            node->left = base;
            node->right = expression(); // index
            node->expression_type = dynarray_types[sym_table[idx].type - TYPE_DYNARRAY_BASE].elem_type;
            match(TOKEN_RBRACKET);
            return node;
        }
        compile_error(token.line, "'%s' is not an array, cannot be indexed", sym_table[idx].name);
    }
    ASTNode *node = create_node(NODE_VARIABLE);
    node->line = line;
    node->data.var_idx = idx;
    node->expression_type = sym_table[idx].type;
    if (is_proc_type(node->expression_type) && token.type == TOKEN_LPAREN) {
        // A NAMED procedural-type global followed by '(' is a CALL - see
        // build_procvar_call()'s own comment. In expression context
        // (unlike a bare statement - see parse_global_assignment()),
        // NOT followed by '(' just means "the value itself" (e.g.
        // 'p1 = nil', 'p2 := p1', passing p1 through as an argument) -
        // calling a zero-argument procedural-type value in expression
        // context always needs explicit '()' to disambiguate from that.
        return build_procvar_call(node, node->expression_type - TYPE_PROC_BASE, line, 0);
    }
    if (is_pointer_type(node->expression_type) && (token.type == TOKEN_CARET || class_dot_deref_pending(node->expression_type))) {
        return parse_heap_deref_read(node, line);
    }
    return node;
}

// 'p1 = p2' / 'p1 <> p2' where both are record variables of the SAME
// record type - called from factor() the moment it sees '='/'<>' right
// after a record variable name (i.e. this consumes the operator itself,
// instead of leaving it for expression()'s ordinary comparison-operator
// loop, since a "whole record value" isn't one ASTNode/DataType this
// compiler can represent - there's no single opcode that could ever
// compare two records as a unit). Desugars into an AND-chain comparing
// each field pair (NOT-wrapped for '<>'), field by field - the same
// "a record isn't one runtime value, just several ordinary globals
// under mangled names" philosophy parse_whole_record_assignment() above
// already uses for ':='. Reuses the ordinary NODE_BINARY_OP(EQ)/
// NODE_BINARY_OP(AND)/NODE_UNARY_OP(NOT) machinery - ONLY the leaf
// NODE_VARIABLE nodes need expression_type set explicitly (matching
// parse_global_symbol_reference() above); type_check()'s existing
// generic recursion fills in every wrapper node normally once parsing
// finishes, exactly as if a user had written the field-by-field chain
// by hand.
// Below the top level of parse_record_comparison()'s loop below: same
// base+offset walk as build_record_arg_values() uses, but folding an
// AND-chain of per-leaf equality comparisons instead of appending to a
// node chain - recurses for a nested-record field, folding its own
// (recursively built) AND-chain into the running result exactly like a
// single leaf comparison gets folded in below.
static ASTNode *build_record_compare(int record_type_idx, int is_local1, int base1, int levels_up1, int is_local2, int base2, int levels_up2) {
    RecordTypeDef *rt = &record_types[record_type_idx];
    ASTNode *result = NULL;
    int offset = 0;
    for (int i = 0; i < rt->field_count; i++) {
        RecordField *f = &rt->fields[i];
        ASTNode *piece;
        if (f->is_record) {
            piece = build_record_compare(f->record_type_idx, is_local1, base1 + offset, levels_up1, is_local2, base2 + offset, levels_up2);
        } else {
            ASTNode *left = record_field_read_node(is_local1, base1 + offset, levels_up1);
            ASTNode *right = record_field_read_node(is_local2, base2 + offset, levels_up2);
            piece = create_node(NODE_BINARY_OP);
            piece->op = TOKEN_EQ;
            piece->left = left;
            piece->right = right;
        }
        if (!result) {
            result = piece;
        } else {
            ASTNode *and_node = create_node(NODE_BINARY_OP);
            and_node->op = TOKEN_AND;
            and_node->left = result;
            and_node->right = piece;
            result = and_node;
        }
        offset += f->is_record ? record_type_leaf_count(f->record_type_idx) : 1;
    }
    return result;
}

static ASTNode *parse_record_comparison(int is_local1, int record_type_idx1, const int *field_idx1, int levels_up1, const char *rec_name) {
    TokenType op = token.type; // TOKEN_EQ or TOKEN_NEQ
    match(op);
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Expected a record variable of the same type as '%s'", rec_name);
    }
    int levels_up2, is_local2, record_type_idx2;
    const int *field_idx2;
    if (!find_any_record_var_outward(token.text, &levels_up2, &is_local2, &record_type_idx2, &field_idx2)) {
        compile_error(token.line, "'%s' is not a record variable", token.text);
    }
    if (record_type_idx1 != record_type_idx2) {
        compile_error(token.line, "Cannot compare '%s' (type '%s') to '%s' (type '%s') - different record types",
                       rec_name, record_types[record_type_idx1].name,
                       token.text, record_types[record_type_idx2].name);
    }
    match(TOKEN_IDENTIFIER);

    RecordTypeDef *rt = &record_types[record_type_idx1];
    ASTNode *result = NULL;
    for (int i = 0; i < rt->field_count; i++) {
        RecordField *f = &rt->fields[i];
        if (f->is_array) {
            compile_error(token.line, "Cannot compare record '%s': field '%s' is an array, and this compiler doesn't support whole-array comparison",
                           rec_name, f->name);
        }
        ASTNode *eq;
        if (f->is_record) {
            eq = build_record_compare(f->record_type_idx, is_local1, field_idx1[i], levels_up1, is_local2, field_idx2[i], levels_up2);
        } else {
            ASTNode *left = record_field_read_node(is_local1, field_idx1[i], levels_up1);
            ASTNode *right = record_field_read_node(is_local2, field_idx2[i], levels_up2);
            eq = create_node(NODE_BINARY_OP);
            eq->op = TOKEN_EQ;
            eq->left = left;
            eq->right = right;
        }
        if (!result) {
            result = eq;
        } else {
            ASTNode *and_node = create_node(NODE_BINARY_OP);
            and_node->op = TOKEN_AND;
            and_node->left = result;
            and_node->right = eq;
            result = and_node;
        }
    }
    if (!result) {
        // A zero-field record type (an empty 'record ... end;') - two
        // instances of it are vacuously always equal.
        result = create_node(NODE_BOOLEAN);
        result->data.num_value = 1;
        result->expression_type = TYPE_BOOLEAN;
    }
    if (op == TOKEN_NEQ) {
        ASTNode *not_node = create_node(NODE_UNARY_OP);
        not_node->op = TOKEN_NOT;
        not_node->left = result;
        result = not_node;
    }
    return result;
}

static ASTNode *factor(void);

// Parses the operand of '@'/'Addr(...)' - must be exactly a pointer
// dereference (p^, or a chain reaching a record field, p^.field or
// deeper) - see NODE_HEAP_FIELD_ACCESS's own comment in common.h for
// its left/right shape, reused directly here: left is already the
// pointer's own value expression, right is already the field's
// compile-time-constant offset (0 for a scalar target's bare '^').
// Repackaged as left + offset (or bare left when offset is 0, avoiding
// a wasted '+0'), retyped TYPE_UNTYPED_POINTER - no new opcode needed,
// since the field offset is ALREADY a compile-time constant today (it's
// baked directly into OP_LOAD_HEAP_FIELD's own arg, never pushed at
// runtime) - this just adds it explicitly instead of letting
// OP_LOAD_HEAP_FIELD consume it internally. A multi-level chain
// (p^.next^.data) works with no special handling: 'left' is simply
// whatever (possibly itself a NODE_HEAP_FIELD_ACCESS) expression the
// existing dereference-chain parser already built for everything before
// the LAST '^' step.
//
// Anything else (a plain variable, an array element, a general
// expression) is explicitly rejected - this VM's ordinary variables
// don't live in the same addressable heap pointers do (see
// docs/LANGUAGE.md#pointers), so there's no meaningful address to
// compute for them; @x/@p (the pointer variable's OWN storage slot, as
// opposed to what it points to) are the two most likely such attempts.
static ASTNode *parse_addr_expression(int line) {
    ASTNode *operand = factor();
    if (operand->type != NODE_HEAP_FIELD_ACCESS) {
        compile_error(line, "'@'/'Addr' only applies to a pointer dereference ('p^' or 'p^.field') - not a plain "
                       "variable, array element, or general expression (this VM's ordinary variables aren't "
                       "heap-addressable - see docs/LANGUAGE.md)");
    }
    // NODE_ADDR_OF, not a NODE_BINARY_OP with a repurposed '+' tag - see
    // that node's own comment in common.h for why (type_checker.c's
    // NODE_BINARY_OP case would wrongly reject a pointer-typed operand).
    ASTNode *result = create_node(NODE_ADDR_OF);
    result->line = line;
    result->left = operand->left;
    result->right = operand->right;
    result->expression_type = TYPE_UNTYPED_POINTER;
    return result;
}

static ASTNode *factor(void) {
    if (token.type == TOKEN_MINUS || token.type == TOKEN_NOT) {
        TokenType op = token.type;
        match(op);
        ASTNode *node = create_node(NODE_UNARY_OP);
        node->op = op;
        node->left = factor();
        return node;
    } else if (token.type == TOKEN_AT) {
        int line = token.line;
        match(TOKEN_AT);
        return parse_addr_expression(line);
    } else if (token.type == TOKEN_ADDR) {
        int line = token.line;
        match(TOKEN_ADDR);
        match(TOKEN_LPAREN);
        ASTNode *result = parse_addr_expression(line);
        match(TOKEN_RPAREN);
        return result;
    } else if (token.type == TOKEN_INHERITED) {
        return parse_inherited_call(0);
    } else if (token.type == TOKEN_ORD) {
        match(TOKEN_ORD);
        match(TOKEN_LPAREN);
        ASTNode *arg = expression();
        match(TOKEN_RPAREN);
        if (arg->expression_type >= TYPE_ENUM_BASE && arg->expression_type < TYPE_POINTER_BASE) {
            // An enum value's ordinal IS its runtime representation
            // already - ord() on an enum is a compile-time no-op,
            // unlike ord() on a char/string (which needs the OP_ORD
            // opcode to pull a byte value out of a string_pool[]
            // entry). Reuse 'arg' directly rather than wrapping it in a
            // NODE_UNARY_OP, which would emit a real (and wrong) OP_ORD.
            arg->expression_type = TYPE_INTEGER;
            return arg;
        }
        ASTNode *node = create_node(NODE_UNARY_OP);
        node->op = TOKEN_ORD;
        node->left = arg;
        return node;
    } else if (token.type == TOKEN_ABS || token.type == TOKEN_SQR ||
               token.type == TOKEN_CHR ||
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
    } else if (token.type == TOKEN_EOF_FN || token.type == TOKEN_EOLN) {
        // 'eof' / 'eoln' - real Pascal usage is almost always bare (no
        // parens at all, e.g. 'while not eof do ...'), but '()' is
        // accepted too, now optionally naming a file variable inside it
        // ('eof(f)') to check that file instead of stdin. Unlike every
        // other builtin here, these need a genuine runtime check
        // (peeking at stdin or a file), so - unlike 'pi' above - this
        // does need a real opcode: NODE_BUILTIN_CALL, dispatched on
        // node->op in codegen. left = an optional file variable
        // reference (a NODE_VARIABLE, expression_type TYPE_FILE) - NULL
        // means stdin, exactly as before this field existed.
        TokenType kind = token.type;
        match(kind);
        ASTNode *node = create_node(NODE_BUILTIN_CALL);
        node->op = kind;
        node->expression_type = TYPE_BOOLEAN;
        if (token.type == TOKEN_LPAREN) {
            match(TOKEN_LPAREN);
            if (token.type == TOKEN_IDENTIFIER) {
                int fidx = find_file_var_soft(token.text);
                if (fidx != -1) {
                    if (kind == TOKEN_EOLN && (sym_table[fidx].type == TYPE_TYPED_FILE
                                                || sym_table[fidx].type == TYPE_UNTYPED_FILE)) {
                        // No line concept in binary data - see the v1
                        // scope cut in docs/LANGUAGE.md.
                        compile_error(token.line, "'eoln' doesn't apply to a typed or untyped file - use 'eof' instead");
                    }
                    match(TOKEN_IDENTIFIER);
                    ASTNode *file_ref = create_node(NODE_VARIABLE);
                    file_ref->data.var_idx = fidx;
                    file_ref->expression_type = sym_table[fidx].type; // TYPE_FILE, TYPE_TYPED_FILE, or TYPE_UNTYPED_FILE
                    node->left = file_ref;
                }
            }
            match(TOKEN_RPAREN);
        }
        return node;
    } else if (token.type == TOKEN_FILESIZE) {
        // 'filesize(f)', a typed file only - unlike eof/eoln above, the
        // file argument is MANDATORY (there's no stdin/stdout fallback
        // that makes sense for "how many records"), so this needs no
        // ->left indirection - the file's own sym_table[] index goes
        // straight into data.var_idx.
        match(TOKEN_FILESIZE);
        match(TOKEN_LPAREN);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "Expected a typed file variable after 'filesize('");
        }
        int fidx = find_file_var_soft(token.text);
        if (fidx == -1 || sym_table[fidx].type != TYPE_TYPED_FILE) {
            compile_error(token.line, "'%s' is not a typed file variable", token.text);
        }
        match(TOKEN_IDENTIFIER);
        match(TOKEN_RPAREN);
        ASTNode *node = create_node(NODE_BUILTIN_CALL);
        node->op = TOKEN_FILESIZE;
        node->data.var_idx = fidx;
        node->expression_type = TYPE_INTEGER;
        return node;
    } else if (token.type == TOKEN_SIZEOF) {
        // 'sizeOf(x)' - the number of bytes x would occupy as a typed
        // (binary) file record/element (NOT "memory size" - every
        // scalar in this VM occupies one uniform slot regardless of
        // declared type, so that answer would always just be 4 and
        // reflect nothing real; see docs/LANGUAGE.md#sizeof). Always a
        // compile-time constant - x is resolved to a byte count right
        // here and spliced in as a plain NODE_NUMBER, exactly like a
        // named constant substitutes - no opcode, no type_checker.c/
        // optimizer.c/codegen.c/vm.c involvement at all.
        match(TOKEN_SIZEOF);
        match(TOKEN_LPAREN);
        int line = token.line;
        int size;
        // Set (and left for the shared typed-file-safety check just
        // below) whenever x resolved to a record type, whether named
        // directly or via a record variable.
        int rt_idx_for_check = -1;
        char bad_record_name[MAX_NAME] = "";
        int rv_is_local, rv_record_type_idx;
        const int *rv_field_idx;
        int local_levels_up;
        if (token.type == TOKEN_IDENTIFIER && find_record_type(token.text) != -1) {
            // A record TYPE name.
            rt_idx_for_check = find_record_type(token.text);
            strcpy(bad_record_name, token.text);
            size = record_type_byte_size(rt_idx_for_check);
            match(TOKEN_IDENTIFIER);
        } else if (token.type == TOKEN_IDENTIFIER && find_file_var_soft(token.text) != -1
                   && sym_table[find_file_var_soft(token.text)].type == TYPE_TYPED_FILE) {
            // A typed-file VARIABLE - reuse its already-cached byte_size,
            // no recomputation, and meaningful even before reset/rewrite
            // ever opens the file (a pure declaration-time answer).
            int fidx = find_file_var_soft(token.text);
            size = typed_file_vars[find_typed_file_var(fidx)].byte_size;
            match(TOKEN_IDENTIFIER);
        } else if (token.type == TOKEN_IDENTIFIER
                   && find_any_record_var(token.text, &rv_is_local, &rv_record_type_idx, &rv_field_idx)) {
            // A record VARIABLE (global or local) - resolve to its
            // record type, same answer/check as the type-name case.
            rt_idx_for_check = rv_record_type_idx;
            strcpy(bad_record_name, token.text);
            size = record_type_byte_size(rt_idx_for_check);
            match(TOKEN_IDENTIFIER);
        } else if (token_is_scalar_type_name()) {
            // A scalar TYPE name/keyword - reuse parse_scalar_type()
            // wholesale, including its scalar_type_disk_width side
            // channel (exactly the same source of truth byte/shortint/
            // word fields already use).
            DataType t = parse_scalar_type();
            if (!is_typed_file_safe_scalar(t)) {
                compile_error(line, "This type can't be used with 'sizeOf' - only integer, real, boolean, byte, shortint, word, an enumerated type, or a set are allowed");
            }
            size = (scalar_type_disk_width == TOKEN_BYTE || scalar_type_disk_width == TOKEN_SHORTINT) ? 1
                 : (scalar_type_disk_width == TOKEN_WORD) ? 2 : (int)sizeof(int);
        } else if (token.type == TOKEN_IDENTIFIER && find_local_outward(token.text, &local_levels_up) != -1) {
            if (local_at(find_local_outward(token.text, &local_levels_up), local_levels_up)->is_array) {
                compile_error(line, "'sizeOf' doesn't support arrays");
            }
            // A plain scalar variable - explicit v1 scope cut (see
            // docs/LANGUAGE.md#sizeof), not silently returning a wrong
            // answer for a byte/shortint/word-declared one.
            compile_error(line, "'sizeOf' on a scalar variable isn't supported yet - use 'sizeOf' with the type name instead");
            size = 0; // unreachable
        } else if (token.type == TOKEN_IDENTIFIER && find_var_soft_visible(token.text) != -1) {
            if (sym_table[find_var_soft_visible(token.text)].is_array) {
                compile_error(line, "'sizeOf' doesn't support arrays");
            }
            compile_error(line, "'sizeOf' on a scalar variable isn't supported yet - use 'sizeOf' with the type name instead");
            size = 0; // unreachable
        } else {
            compile_error(line, "'sizeOf' expects a type name, a record variable, or a typed file variable (see docs/LANGUAGE.md)");
            size = 0; // unreachable
        }
        if (rt_idx_for_check != -1 && !record_type_is_typed_file_safe(rt_idx_for_check)) {
            compile_error(line, "'%s' can't be used with 'sizeOf' - no array, string, char, pointer, or procedural-typed fields allowed (same restriction as a typed file's element type)", bad_record_name);
        }
        match(TOKEN_RPAREN);
        ASTNode *node = create_node(NODE_NUMBER);
        node->line = line;
        node->data.num_value = size;
        node->expression_type = TYPE_INTEGER;
        return node;
    } else if (token.type == TOKEN_PARAMCOUNT) {
        // 'ParamCount' - like eof/eoln above, real usage is almost
        // always bare, but '()' (always empty - there's no file-like
        // argument here) is accepted too. Only known at VM startup
        // (see vm_set_program_args() in vm.c), not compile time, so
        // this needs a real opcode.
        match(TOKEN_PARAMCOUNT);
        if (token.type == TOKEN_LPAREN) {
            match(TOKEN_LPAREN);
            match(TOKEN_RPAREN);
        }
        ASTNode *node = create_node(NODE_BUILTIN_CALL);
        node->op = TOKEN_PARAMCOUNT;
        node->expression_type = TYPE_INTEGER;
        return node;
    } else if (token.type == TOKEN_EXCEPTMESSAGE) {
        // 'ExceptMessage' - meaningful only inside an except-body, same
        // bare-or-empty-parens shape as ParamCount above. Only known at
        // runtime (whatever the innermost active OP_RAISE last recorded),
        // so this needs a real opcode too.
        match(TOKEN_EXCEPTMESSAGE);
        if (token.type == TOKEN_LPAREN) {
            match(TOKEN_LPAREN);
            match(TOKEN_RPAREN);
        }
        ASTNode *node = create_node(NODE_BUILTIN_CALL);
        node->op = TOKEN_EXCEPTMESSAGE;
        node->expression_type = TYPE_STRING;
        return node;
    } else if (token.type == TOKEN_PARAMSTR) {
        // 'ParamStr(i)' - same one-required-argument shape as length(x)
        // below. Out-of-range i is a runtime concern (empty string, not
        // an error - see OP_PARAM_STR), not a parse/type-time one.
        match(TOKEN_PARAMSTR);
        match(TOKEN_LPAREN);
        ASTNode *node = create_node(NODE_BUILTIN_CALL);
        node->op = TOKEN_PARAMSTR;
        node->left = expression();
        node->expression_type = TYPE_STRING;
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
        if (try_get_array_bounds_here(&lower, &upper)) {
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
        // constant for a STATIC array. A dynamic array's bounds are only
        // known at runtime (see TYPE_DYNARRAY_BASE) and are always
        // 0-based (Delphi convention, unlike this compiler's existing
        // lo..hi static arrays) - low() is still a compile-time constant
        // 0 there, but high() desugars to 'length(arr) - 1', reusing
        // TOKEN_LENGTH's own dynamic-array runtime path (see codegen.c)
        // the same way succ()/pred() desugar to 'x+1'/'x-1' above.
        TokenType kind = token.type;
        match(kind);
        match(TOKEN_LPAREN);
        int lower, upper;
        if (try_get_array_bounds_here(&lower, &upper)) {
            match(TOKEN_RPAREN);
            ASTNode *node = create_node(NODE_NUMBER);
            node->data.num_value = (kind == TOKEN_LOW) ? lower : upper;
            node->expression_type = TYPE_INTEGER;
            return node;
        }
        ASTNode *arg = expression();
        match(TOKEN_RPAREN);
        if (!is_dynarray_type(arg->expression_type)) {
            compile_error(token.line, "'%s' requires an array argument", kind == TOKEN_LOW ? "low" : "high");
        }
        if (kind == TOKEN_LOW) {
            ASTNode *node = create_node(NODE_NUMBER);
            node->data.num_value = 0;
            node->expression_type = TYPE_INTEGER;
            return node;
        }
        ASTNode *len_call = create_node(NODE_BUILTIN_CALL);
        len_call->op = TOKEN_LENGTH;
        len_call->left = arg;
        len_call->expression_type = TYPE_INTEGER;
        ASTNode *one = create_node(NODE_NUMBER);
        one->data.num_value = 1;
        one->expression_type = TYPE_INTEGER;
        ASTNode *node = create_node(NODE_BINARY_OP);
        node->op = TOKEN_MINUS;
        node->left = len_call;
        node->right = one;
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
    } else if (token.type == TOKEN_INTTOSTR || token.type == TOKEN_STRTOINT
               || token.type == TOKEN_FLOATTOSTR || token.type == TOKEN_STRTOFLOAT
               || token.type == TOKEN_RANDOM) {
        // Unary number<->string builtins: IntToStr(n), StrToInt(s),
        // FloatToStr(x), StrToFloat(s) - same shape as upcase/uppercase/
        // lowercase just above. Random(n) reuses the identical shape -
        // one integer argument, one integer result.
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
    } else if (token.type == TOKEN_LBRACKET) {
        // '[e1, e2, e3..e4, ...]' - a set constructor ('[]' is the empty
        // set). A range's bounds ('a..b') must be compile-time-constant
        // integers - unrolled into (b-a+1) individual elements right
        // here, since this compiler has no runtime loop primitive to
        // build one otherwise; a single (non-range) element can be any
        // ordinal expression, constant or not (see NODE_SET_CONSTRUCTOR's
        // codegen, which just ORs '1 << element' into an accumulator for
        // each one - a literal and a runtime-computed element compile
        // identically).
        match(TOKEN_LBRACKET);
        ASTNode *node = create_node(NODE_SET_CONSTRUCTOR);
        node->expression_type = TYPE_SET;
        ASTNode *head = NULL;
        ASTNode *tail = NULL;
        if (token.type != TOKEN_RBRACKET) {
            while (1) {
                ASTNode *elem = expression();
                if (token.type == TOKEN_DOTDOT) {
                    if (elem->type != NODE_NUMBER) {
                        compile_error(token.line, "A set range's bounds must be constant integers");
                    }
                    int lo = elem->data.num_value;
                    free_ast(elem);
                    match(TOKEN_DOTDOT);
                    ASTNode *upper_node = expression();
                    if (upper_node->type != NODE_NUMBER) {
                        compile_error(token.line, "A set range's bounds must be constant integers");
                    }
                    int hi = upper_node->data.num_value;
                    free_ast(upper_node);
                    if (hi < lo) {
                        compile_error(token.line, "Invalid set range: upper (%d) must be >= lower (%d)", hi, lo);
                    }
                    for (int v = lo; v <= hi; v++) {
                        ASTNode *n = create_node(NODE_NUMBER);
                        n->data.num_value = v;
                        n->expression_type = TYPE_INTEGER;
                        if (!head) head = n; else tail->next = n;
                        tail = n;
                    }
                } else {
                    if (!head) head = elem; else tail->next = elem;
                    tail = elem;
                }
                if (token.type == TOKEN_COMMA) { match(TOKEN_COMMA); continue; }
                break;
            }
        }
        match(TOKEN_RBRACKET);
        node->left = head;
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
    } else if (token.type == TOKEN_NIL) {
        // 'nil' - a plain NODE_NUMBER literal, exactly like an enum value
        // name already is (see TOKEN_IDENTIFIER's enum-value branch
        // below) - its runtime representation (the int -1) IS its value,
        // no dedicated opcode needed. expression_type = TYPE_NIL, not any
        // specific pointer type - see TYPE_NIL in common.h for how
        // assignment/comparison compatibility with a REAL pointer type is
        // checked specially in type_checker.c, rather than through the
        // ordinary exact-DataType-match rule every other type uses.
        ASTNode *node = create_node(NODE_NUMBER);
        node->data.num_value = -1;
        node->expression_type = TYPE_NIL;
        match(TOKEN_NIL);
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

        ASTNode *class_qualified = try_resolve_class_qualified_access(0);
        if (class_qualified) return class_qualified;

        ASTNode *pointer_cast = try_resolve_pointer_cast();
        if (pointer_cast) return pointer_cast;

        int const_idx = find_const(token.text);
        if (const_idx != -1) {
            ConstDef *c = &const_defs[const_idx];
            match(TOKEN_IDENTIFIER);
            ASTNode *node;
            if (c->type == TYPE_REAL) {
                node = create_node(NODE_REAL_NUMBER);
                node->data.num_value = c->value;
            } else if (c->type == TYPE_BOOLEAN) {
                node = create_node(NODE_BOOLEAN);
                node->data.num_value = c->value;
            } else if (c->type == TYPE_STRING || c->type == TYPE_CHAR) {
                node = create_node(NODE_STRING);
                node->data.var_idx = c->value;
            } else { // TYPE_INTEGER
                node = create_node(NODE_NUMBER);
                node->data.num_value = c->value;
            }
            node->expression_type = c->type;
            return node;
        }

        int enum_type_idx, ordinal;
        if (find_enum_value(token.text, &enum_type_idx, &ordinal)) {
            // A bare enum value name ('Red') resolves directly to its
            // ordinal as a plain NODE_NUMBER literal - exactly like a
            // const - with expression_type carrying which enum type it
            // is (see the TYPE_ENUM_BASE comment in common.h). No new
            // NodeType needed.
            match(TOKEN_IDENTIFIER);
            ASTNode *node = create_node(NODE_NUMBER);
            node->data.num_value = ordinal;
            node->expression_type = (DataType)(TYPE_ENUM_BASE + enum_type_idx);
            return node;
        }

        int with_field_idx = find_with_field(token.text);
        if (with_field_idx != -1) {
            match(TOKEN_IDENTIFIER);
            return parse_global_symbol_reference(with_field_idx, line);
        }

        {
            int rv_levels_up, rv_is_local, rv_record_type_idx;
            const int *rv_field_idx;
            if (find_any_record_var_outward(token.text, &rv_levels_up, &rv_is_local, &rv_record_type_idx, &rv_field_idx)) {
                char rec_name[MAX_NAME];
                strcpy(rec_name, token.text);
                match(TOKEN_IDENTIFIER);
                if (token.type == TOKEN_EQ || token.type == TOKEN_NEQ) {
                    return parse_record_comparison(rv_is_local, rv_record_type_idx, rv_field_idx, rv_levels_up, rec_name);
                }
                if (token.type != TOKEN_PERIOD) {
                    compile_error(token.line, "'%s' is a record variable and can't be used directly here - access a field with '%s.fieldname', compare it with '=' or '<>', or use whole-record assignment ('%s := otherRecord;')",
                                  rec_name, rec_name, rec_name);
                }
                match(TOKEN_PERIOD);
                int leaf_idx = resolve_record_field_leaf(rv_record_type_idx, rv_field_idx, rec_name);
                if (rv_is_local) {
                    ASTNode *node = record_field_read_node(1, leaf_idx, rv_levels_up);
                    node->line = line;
                    return node;
                }
                return parse_global_symbol_reference(leaf_idx, line);
            }
        }

        int levels_up;
        int local_idx = find_local_outward(token.text, &levels_up);
        if (local_idx != -1) {
            LocalSymbol *ls = local_at(local_idx, levels_up);
            if (ls->is_proc_param) {
                return parse_indirect_call(ls, local_idx, levels_up, line, 0);
            }
            if (ls->is_var_param) {
                match(TOKEN_IDENTIFIER);
                if ((ls->type == TYPE_STRING || ls->type == TYPE_CHAR)
                    && token.type == TOKEN_LBRACKET) {
                    compile_error(token.line, "Indexing a 'var' parameter's string/char value ('%s[i]') is not supported yet - only the whole value can be read/written through it",
                                   ls->name);
                }
                ASTNode *node = create_node(NODE_VAR_PARAM_READ);
                node->line = line;
                node->data.var_idx = local_idx;
                node->op = (TokenType)levels_up;
                node->expression_type = ls->type;
                if (is_dynarray_type(node->expression_type) && token.type == TOKEN_LBRACKET) {
                    match(TOKEN_LBRACKET);
                    ASTNode *access = create_node(NODE_DYNARRAY_ACCESS);
                    access->line = line;
                    access->left = node;
                    access->right = expression(); // index
                    access->expression_type = dynarray_types[node->expression_type - TYPE_DYNARRAY_BASE].elem_type;
                    match(TOKEN_RBRACKET);
                    return access;
                }
                if (is_proc_type(node->expression_type) && token.type == TOKEN_LPAREN) {
                    return build_procvar_call(node, node->expression_type - TYPE_PROC_BASE, line, 0);
                }
                if (is_pointer_type(node->expression_type) && (token.type == TOKEN_CARET || class_dot_deref_pending(node->expression_type))) {
                    return parse_heap_deref_read(node, line);
                }
                return node;
            }
            if (ls->is_static) {
                match(TOKEN_IDENTIFIER);
                return parse_global_symbol_reference(ls->static_sym_idx, line);
            }
            if (ls->is_array) {
                match(TOKEN_IDENTIFIER);
                int arr_sym_idx = ls->array_sym_idx;
                if (token.type != TOKEN_LBRACKET) {
                    compile_error(token.line, "Array '%s' must be indexed", ls->name);
                }
                match(TOKEN_LBRACKET);
                if (sym_table[arr_sym_idx].is_record_array) {
                    return parse_record_array_field_read(arr_sym_idx, line);
                }
                if (sym_table[arr_sym_idx].is_nd) {
                    ASTNode *node = create_node(NODE_ARRAY_ACCESS_ND);
                    node->line = line;
                    node->data.var_idx = arr_sym_idx;
                    node->left = parse_nd_index_list(sym_table[arr_sym_idx].nd_dims); // consumes ']' itself
                    node->expression_type = sym_table[arr_sym_idx].type;
                    return node;
                }
                if (sym_table[arr_sym_idx].is_2d) {
                    ASTNode *node = create_node(NODE_ARRAY_ACCESS_2D);
                    node->line = line;
                    node->data.var_idx = arr_sym_idx;
                    node->left = expression();  // first index
                    match(TOKEN_COMMA);
                    node->right = expression(); // second index
                    node->expression_type = sym_table[arr_sym_idx].type;
                    match(TOKEN_RBRACKET);
                    return node;
                }
                ASTNode *node = create_node(NODE_ARRAY_ACCESS);
                node->line = line;
                node->data.var_idx = arr_sym_idx;
                node->left = expression(); // index
                node->expression_type = sym_table[arr_sym_idx].type;
                match(TOKEN_RBRACKET);
                return node;
            }
            if (ls->is_array_ref) {
                match(TOKEN_IDENTIFIER);
                if (token.type != TOKEN_LBRACKET) {
                    compile_error(token.line, "Array '%s' must be indexed", ls->name);
                }
                match(TOKEN_LBRACKET);
                if (ls->is_nd) {
                    ASTNode *node = create_node(NODE_REF_ARRAY_ACCESS_ND);
                    node->line = line;
                    node->data.var_idx = local_idx;
                    node->op = (TokenType)levels_up;
                    node->left = parse_nd_index_list(ls->nd_dims); // consumes ']' itself
                    node->expression_type = ls->type;
                    return node;
                }
                if (ls->is_2d) {
                    ASTNode *node = create_node(NODE_REF_ARRAY_ACCESS_2D);
                    node->line = line;
                    node->data.var_idx = local_idx;
                    node->op = (TokenType)levels_up;
                    node->left = expression();  // first index
                    match(TOKEN_COMMA);
                    node->right = expression(); // second index
                    node->expression_type = ls->type;
                    match(TOKEN_RBRACKET);
                    return node;
                }
                ASTNode *node = create_node(NODE_REF_ARRAY_ACCESS);
                node->line = line;
                node->data.var_idx = local_idx; // the parameter's OWN slot, holding a runtime sym_table index
                node->op = (TokenType)levels_up;
                node->left = expression(); // index
                node->expression_type = ls->type;
                match(TOKEN_RBRACKET);
                return node;
            }
            match(TOKEN_IDENTIFIER);
            if ((ls->type == TYPE_STRING || ls->type == TYPE_CHAR)
                && token.type == TOKEN_LBRACKET) {
                match(TOKEN_LBRACKET);
                ASTNode *node = create_node(NODE_LOCAL_STRING_INDEX);
                node->line = line;
                node->data.var_idx = local_idx;
                node->op = (TokenType)levels_up;
                node->left = expression(); // index
                node->expression_type = TYPE_CHAR;
                match(TOKEN_RBRACKET);
                return node;
            }
            if (is_dynarray_type(ls->type) && token.type == TOKEN_LBRACKET) {
                match(TOKEN_LBRACKET);
                ASTNode *base = create_node(NODE_LOCAL_VAR);
                base->line = line;
                base->data.var_idx = local_idx;
                base->op = (TokenType)levels_up;
                base->expression_type = ls->type;
                ASTNode *node = create_node(NODE_DYNARRAY_ACCESS);
                node->line = line;
                node->left = base;
                node->right = expression(); // index
                node->expression_type = dynarray_types[ls->type - TYPE_DYNARRAY_BASE].elem_type;
                match(TOKEN_RBRACKET);
                return node;
            }
            ASTNode *node = create_node(NODE_LOCAL_VAR);
            node->line = line;
            node->data.var_idx = local_idx;
            node->op = (TokenType)levels_up;
            node->expression_type = ls->type;
            if (is_proc_type(node->expression_type) && token.type == TOKEN_LPAREN) {
                return build_procvar_call(node, node->expression_type - TYPE_PROC_BASE, line, 0);
            }
            if (is_pointer_type(node->expression_type) && (token.type == TOKEN_CARET || class_dot_deref_pending(node->expression_type))) {
                return parse_heap_deref_read(node, line);
            }
            return node;
        }

        if (current_class_ptr_idx != -1 && class_has_member(current_class_ptr_idx, token.text)) {
            return parse_self_shorthand_read(line);
        }

        int call_proc_idx = find_proc_visible(token.text);
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
    // 'x in s' - same precedence level as the relational operators above,
    // but its own node type (not NODE_BINARY_OP) - see NODE_SET_IN's
    // comment in common.h for why. Not part of the while loop above:
    // chaining 'in' with itself or with =/<>/etc. isn't meaningful, so a
    // single optional check here is enough.
    if (token.type == TOKEN_IN) {
        match(TOKEN_IN);
        ASTNode *in_node = create_node(NODE_SET_IN);
        in_node->left = node;
        in_node->right = arithmetic_expression();
        in_node->expression_type = TYPE_BOOLEAN;
        node = in_node;
    }
    // 'obj is TFoo' / 'obj as TFoo' - same precedence tier as 'in' above,
    // and the same reasoning for being a single optional check rather
    // than part of the while loop: chaining is/as with itself or with
    // the relational operators isn't meaningful. The right-hand side is
    // a bare class TYPE NAME, not an expression - factor()/expression()
    // have no hook anywhere for "identifier that names a type, not a
    // variable" (a bare class name used as an operand today falls
    // through to find_var()'s "Unknown identifier" error), so this is
    // hand-parsed here directly against find_pointer_type(), mirroring
    // how new(x, MethodName) hand-parses an identifier against its own
    // lookup table instead of going through expression().
    if (token.type == TOKEN_IS || token.type == TOKEN_AS) {
        TokenType op = token.type;
        int line = token.line;
        match(op);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "Expected a class type name after '%s'", op == TOKEN_IS ? "is" : "as");
        }
        int class_idx = find_pointer_type(token.text);
        if (class_idx == -1 || !pointer_types[class_idx].is_class) {
            compile_error(token.line, "'%s' is not a known class type", token.text);
        }
        match(TOKEN_IDENTIFIER);

        DataType left_t = node->expression_type;
        int left_is_class = is_pointer_type(left_t) && pointer_types[left_t - TYPE_POINTER_BASE].is_class;
        if (left_t != TYPE_NIL && !left_is_class) {
            compile_error(line, "'%s' requires a class-typed (or nil) operand", op == TOKEN_IS ? "is" : "as");
        }
        DataType target_t = (DataType)(TYPE_POINTER_BASE + class_idx);
        // TYPE_NIL bypasses the ancestor/descendant relationship check
        // entirely - nil has no runtime class of its own, so 'nil is X'/
        // 'nil as X' are legal for ANY class X. class_type_is_subtype_of()
        // requires is_pointer_type() on both operands and would otherwise
        // wrongly reject this (TYPE_NIL isn't in the pointer-type range
        // at all - see is_pointer_type()'s bounded-range check).
        if (left_t != TYPE_NIL
            && !class_type_is_subtype_of(left_t, target_t)
            && !class_type_is_subtype_of(target_t, left_t)) {
            compile_error(line, "'%s' can never succeed: '%s' and '%s' are unrelated classes",
                           op == TOKEN_IS ? "is" : "as",
                           pointer_types[left_t - TYPE_POINTER_BASE].name, pointer_types[class_idx].name);
        }

        if (op == TOKEN_IS) {
            ASTNode *test = create_node(NODE_IS_TEST);
            test->line = line;
            test->left = node;
            test->data.num_value = class_idx;
            test->expression_type = TYPE_BOOLEAN;
            node = test;
        } else {
            ASTNode *cast = create_node(NODE_AS_CAST);
            cast->line = line;
            cast->left = node;
            cast->data.num_value = class_idx;
            cast->expression_type = target_t;
            char msg[MAX_NAME + 32];
            snprintf(msg, sizeof(msg), "Cannot cast to '%s'", pointer_types[class_idx].name);
            ASTNode *msg_node = create_node(NODE_STRING);
            msg_node->line = line;
            msg_node->data.var_idx = intern_string(msg);
            msg_node->expression_type = TYPE_STRING;
            cast->right = msg_node;
            node = cast;
        }
    }
    return node;
}

// Parses a 'label' section: 'label 1, 2, 100;' - one or more comma-
// separated unsigned integer labels (no sign, no const/enum reference -
// ISO Pascal defines a label as literally an unsigned-integer, unlike
// case labels or subrange bounds which accept more).
static void parse_label_section(void) {
    match(TOKEN_LABEL);
    while (1) {
        int line = token.line;
        if (token.type != TOKEN_NUMBER) {
            compile_error(line, "Expected an unsigned integer label");
        }
        int id = token.value;
        match(TOKEN_NUMBER);
        if (find_declared_label(id) != -1) {
            compile_error(line, "Duplicate label declaration '%d'", id);
        }
        if (declared_label_count >= MAX_DECLARED_LABELS) {
            compile_error(line, "Too many label declarations (limit is %d)", MAX_DECLARED_LABELS);
        }
        declared_labels[declared_label_count].id = id;
        declared_labels[declared_label_count].decl_line = line;
        declared_labels[declared_label_count].defined = 0;
        declared_label_count++;
        if (token.type == TOKEN_COMMA) {
            match(TOKEN_COMMA);
            continue;
        }
        break;
    }
    match(TOKEN_SEMI);
}

// Every label declared in the current block's 'label' section must label
// exactly one statement somewhere in that block (standard Pascal
// requirement) - called once the block's whole statement list has been
// parsed, so a label that's declared but never used is caught rather
// than silently accepted as dead documentation.
static void check_all_labels_defined(void) {
    for (int i = 0; i < declared_label_count; i++) {
        if (!declared_labels[i].defined) {
            compile_error(declared_labels[i].decl_line, "Label %d was declared but never labels a statement", declared_labels[i].id);
        }
    }
}

// Typed-constant module state: the flat chain of synthesized NODE_ASSIGN
// nodes that initialize every typed constant's storage (see
// parse_typed_const_declaration() below) - real storage/runtime
// machinery, unlike the plain ConstDef mechanism above, since a typed
// constant is an array or record, which (like every array/record in
// this compiler) needs an actual sym_table[] entry, not just a parser-
// side substitution table. Spliced onto the very front of the main
// program's body in parse_ast(), using the exact same "walk to tail,
// prepend" pattern vtable_init/class_parent_init already use there.
// Reset at the top of parse_ast() alongside const_def_count and
// friends, so a second compile in the same long-lived process (see
// test_recovery) doesn't leak the previous compile's chain.
static ASTNode *typed_const_init_head = NULL;
static ASTNode *typed_const_init_tail = NULL;

static void append_typed_const_init(ASTNode *stmt) {
    if (!typed_const_init_head) typed_const_init_head = stmt;
    else typed_const_init_tail->next = stmt;
    typed_const_init_tail = stmt;
}

// Parses and fully resolves ONE typed-constant initializer value (an
// array element, or a record field's value) - the same "type_check() +
// optimize_ast() right now, at parse time" trick the scalar-const path
// above already uses, requiring the result to fold down to a literal (a
// genuine compile-time constant, not just any expression - 'what' names
// the value's role, e.g. "Array element"/"Field", for the error
// message). Unlike the scalar path, the resulting literal node is kept
// (not extracted into a raw value and discarded) - it gets embedded
// directly as an ordinary NODE_ASSIGN's right-hand side by this
// function's callers below, so it's walked by type_check()/
// optimize_ast() a second time, harmlessly, once more as part of the
// whole-program pass (every literal node type falls through both
// passes' generic/default case - a no-op) - which is also what performs
// the actual value-vs-declared-type check (matching how any ordinary
// assignment's own right-hand side is validated), so this function
// doesn't need to duplicate that logic.
static ASTNode *parse_typed_const_value(const char *const_name, const char *what) {
    int line = token.line;
    ASTNode *value = expression();
    type_check(value);
    value = optimize_ast(value);
    switch (value->type) {
        case NODE_NUMBER:
        case NODE_REAL_NUMBER:
        case NODE_BOOLEAN:
        case NODE_STRING:
            break;
        default:
            compile_error(line, "%s of typed constant '%s' is not a compile-time constant expression", what, const_name);
    }
    return value;
}

// Parses the typed-constant form of a 'const' declaration - 'Name :
// array[lower..upper] of ScalarType = (v1, v2, ...);' - called right
// after parse_const_section() has matched the identifier and found ':'
// instead of '=' (see there). v1 scope, documented in
// docs/LANGUAGE.md: 1D arrays only (no 2D/ND), and the element type
// must be a built-in PRIMITIVE scalar type (integer/real/char/boolean/
// byte/shortint/word) - not a named type-alias/subrange/enum type, and
// not a record - both enforced by explicit checks below (a named-type-
// alias/enum/subrange element type, and a record itself as the typed
// constant's own type - see the TOKEN_ARRAY check at the top of this
// function). 'const'/'type' sections can repeat and interleave
// (Delphi-style - see parse_ast()/load_unit()), so a type declared
// earlier in the source IS visible by the time a typed constant here
// gets parsed - these checks can no longer rely on strict const-before-
// type ordering to keep them out the way they used to.
// Declares real storage (add_array_var() - the exact same helper the
// 'var' section uses, which already gives this feature duplicate-name
// protection against every other kind of global for free) and appends
// one synthesized NODE_ASSIGN per element onto typed_const_init_head/
// tail - see parse_typed_const_value()'s own comment above for why the
// initializer values are safe to walk through type_check()/
// optimize_ast() a second time later.
// Parses the typed-constant form of a 'const' declaration whose type is
// a RECORD - 'Name : RecordType = (Field1: v1; Field2: v2; ...);' -
// called from parse_typed_const_declaration() below once it's peeked a
// record type name after ':'. v1 scope, mirroring the array case's own:
// every field must be a scalar (no array-typed or nested-record field
// anywhere in the type - rejected up front, clearly, rather than
// producing a confusing error partway through the initializer), fields
// must be given in the record type's own declared order (matching real
// Delphi's own rule for typed record constants - this isn't a struct
// literal with named-in-any-order fields), and every field is required
// (no partial initialization, no defaults).
//
// Reuses add_record_var() completely unmodified - the exact same
// mechanism 'var p: TRecord;' already uses to create one hidden global
// per field - so a typed constant record gets real storage, correct
// field-access codegen, and dead-code elimination for free, exactly
// like the array case reuses add_array_var(). Each field's initializer
// becomes one plain-scalar NODE_ASSIGN (left = value, right unused -
// see NODE_ASSIGN's own comment in common.h; this is NOT the array-
// element overload the array case above uses, since a field's own leaf
// symbol is an ordinary scalar, never itself is_array here).
static void parse_typed_const_record_declaration(const char *name, int decl_line, int record_type_idx) {
    RecordTypeDef *rt = &record_types[record_type_idx];
    for (int i = 0; i < rt->field_count; i++) {
        if (rt->fields[i].is_array || rt->fields[i].is_record) {
            compile_error(decl_line, "Typed constant '%s': record type '%s' has an array or nested-record field '%s' - "
                           "only all-scalar record types are supported as a typed constant's type yet",
                           name, rt->name, rt->fields[i].name);
        }
    }
    match(TOKEN_IDENTIFIER); // the record type name, already confirmed by the caller
    match(TOKEN_EQ);

    add_record_var(name, record_type_idx); // one hidden global leaf per field, exactly like 'var name: RecordType;'
    RecordVarDef *rv = &record_vars[record_var_count - 1]; // the one add_record_var() just appended

    match(TOKEN_LPAREN);
    for (int i = 0; i < rt->field_count; i++) {
        RecordField *f = &rt->fields[i];
        if (token.type != TOKEN_IDENTIFIER || strcmp(token.text, f->name) != 0) {
            compile_error(token.line, "Typed constant '%s': expected field '%s' next (fields must be given in the "
                           "record type's own declared order, and every field is required)", name, f->name);
        }
        match(TOKEN_IDENTIFIER);
        match(TOKEN_COLON);
        ASTNode *value = parse_typed_const_value(name, "Field");
        ASTNode *stmt = create_node(NODE_ASSIGN);
        stmt->data.var_idx = rv->field_sym_idx[i];
        stmt->left = wrap_range_check(value, f->is_subrange, f->subrange_lower, f->subrange_upper);
        append_typed_const_init(stmt);
        sym_table[rv->field_sym_idx[i]].is_const = 1;
        if (i < rt->field_count - 1) {
            if (token.type != TOKEN_SEMI) {
                compile_error(token.line, "Typed constant '%s': expected ';' between fields", name);
            }
            match(TOKEN_SEMI);
        }
    }
    if (token.type == TOKEN_SEMI) {
        compile_error(decl_line, "Typed constant '%s': too many fields (expected %d)", name, rt->field_count);
    }
    match(TOKEN_RPAREN);
    match(TOKEN_SEMI);
}

static void parse_typed_const_declaration(const char *name, int decl_line) {
    match(TOKEN_COLON);

    if (token.type == TOKEN_IDENTIFIER && find_record_type(token.text) != -1) {
        parse_typed_const_record_declaration(name, decl_line, find_record_type(token.text));
        return;
    }

    if (token.type != TOKEN_ARRAY) {
        compile_error(token.line, "Typed constant '%s': expected 'array' or a record type name after ':'", name);
    }
    match(TOKEN_ARRAY);
    match(TOKEN_LBRACKET);
    int lower[MAX_ARRAY_DIMS], upper[MAX_ARRAY_DIMS];
    int dims = parse_array_bounds(lower, upper);
    if (dims != 1) {
        compile_error(decl_line, "Typed constant '%s': multi-dimensional array constants aren't supported yet (only 1D)", name);
    }
    match(TOKEN_RBRACKET);
    match(TOKEN_OF);
    if (token.type == TOKEN_IDENTIFIER && find_record_type(token.text) != -1) {
        compile_error(decl_line, "Typed constant '%s': array-of-record constants aren't supported yet", name);
    }
    // v1 scope: a built-in PRIMITIVE element type only - explicit check,
    // not left to fall out of declaration order the way it used to
    // (before 'const'/'type' interleaving, a named type-alias/enum/
    // subrange type could never resolve here anyway, since 'type' always
    // parsed after 'const' - now that a 'type' declared earlier in the
    // source IS visible here, this must be enforced directly instead of
    // relying on that accident of ordering).
    if (token.type == TOKEN_IDENTIFIER && (find_type_alias(token.text) != -1 ||
        find_enum_type(token.text) != -1 || find_subrange_type(token.text) != -1)) {
        compile_error(decl_line, "Typed constant '%s': array element type must be a built-in primitive type "
                       "(integer/real/char/boolean/byte/shortint/word) - a named type-alias/enum/subrange type "
                       "isn't supported here yet", name);
    }
    DataType elem_type = parse_scalar_type();
    int elem_is_subrange = scalar_type_is_subrange;
    int elem_subrange_lower = scalar_type_subrange_lower;
    int elem_subrange_upper = scalar_type_subrange_upper;
    match(TOKEN_EQ);

    int array_sym_idx = sym_count; // add_array_var() is about to append here
    add_array_var(name, elem_type, lower[0], upper[0]);
    sym_table[array_sym_idx].is_subrange = elem_is_subrange;
    sym_table[array_sym_idx].subrange_lower = elem_subrange_lower;
    sym_table[array_sym_idx].subrange_upper = elem_subrange_upper;

    int count = upper[0] - lower[0] + 1;
    match(TOKEN_LPAREN);
    for (int i = 0; i < count; i++) {
        ASTNode *value = parse_typed_const_value(name, "Array element");
        ASTNode *stmt = create_node(NODE_ASSIGN);
        stmt->data.var_idx = array_sym_idx;
        stmt->left = make_default_value_node(TYPE_INTEGER, lower[0] + i, decl_line);
        stmt->right = wrap_range_check(value, elem_is_subrange, elem_subrange_lower, elem_subrange_upper);
        append_typed_const_init(stmt);
        if (i < count - 1) {
            if (token.type != TOKEN_COMMA) {
                compile_error(token.line, "Typed constant '%s': expected %d elements, got fewer", name, count);
            }
            match(TOKEN_COMMA);
        }
    }
    if (token.type == TOKEN_COMMA) {
        compile_error(token.line, "Typed constant '%s': expected %d elements, got more", name, count);
    }
    match(TOKEN_RPAREN);
    match(TOKEN_SEMI);
    sym_table[array_sym_idx].is_const = 1;
}

// Parses a 'type' section: one or more 'TypeName = record ... end;'
// declarations. Only record types exist right now - there's no type
// aliasing ('type TAge = integer;') and no nested records (a field can't
// itself be a record type).
// 'const Name1 = expr1; Name2 = expr2; ...' (or, for an array/record,
// 'Name3 : Type = (initializer);' - see parse_typed_const_declaration()
// above) - see the comment above ConstDef for the overall approach.
static void parse_const_section(void) {
    match(TOKEN_CONST);
    while (token.type == TOKEN_IDENTIFIER) {
        int line = token.line;
        char name[MAX_NAME];
        strcpy(name, token.text);
        if (find_const(name) != -1) {
            compile_error(line, "Duplicate constant declaration '%s'", name);
        }
        match(TOKEN_IDENTIFIER);

        if (token.type == TOKEN_COLON) {
            parse_typed_const_declaration(name, line);
            continue;
        }

        match(TOKEN_EQ);
        ASTNode *value = expression();
        match(TOKEN_SEMI);

        // Fully resolve the expression right now, via the same
        // type-checking and constant-folding passes the rest of the
        // pipeline runs later on the whole program - just run
        // immediately, on this one small subtree.
        type_check(value);
        value = optimize_ast(value);

        if (const_def_count >= MAX_CONSTS) {
            compile_error(line, "Too many constant declarations (limit is %d)", MAX_CONSTS);
        }
        ConstDef *c = &const_defs[const_def_count];
        strcpy(c->name, name);
        switch (value->type) {
            case NODE_NUMBER:      c->type = value->expression_type; c->value = value->data.num_value; break; // TYPE_INTEGER, or an enum type if 'value' is a bare enum value name
            case NODE_REAL_NUMBER: c->type = TYPE_REAL;    c->value = value->data.num_value; break;
            case NODE_BOOLEAN:     c->type = TYPE_BOOLEAN; c->value = value->data.num_value; break;
            case NODE_STRING:      c->type = value->expression_type; c->value = value->data.var_idx; break;
            default:
                compile_error(line, "'%s' is not a compile-time constant expression", name);
        }
        const_def_count++;
        free_ast(value);
    }
}

// Parses one 'name[, name...] : type' field group (array, nested-record,
// or scalar) and appends each resulting name as a RecordField to rt.
// Shared by a record's fixed field list and every variant's own
// parenthesized field list (parse_record_variant_part(), below) -
// callers handle their own trailing separator, since the two contexts
// use different separator rules (a fixed field always needs a trailing
// ';', a variant's last field group before ')' doesn't).
static void parse_record_field_group(RecordTypeDef *rt, int record_type_idx, int is_private, int is_protected, int declaring_class_ptr_idx) {
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
    int is_nested_record = 0;
    int nested_record_type_idx = -1;
    DataType field_type = TYPE_UNKNOWN;
    if (token.type == TOKEN_IDENTIFIER && find_record_type(token.text) != -1) {
        // A nested-record field ('topleft: TPoint;'). Only
        // already-declared record types are ever visible here
        // (record_type_count isn't incremented until THIS type's
        // own 'end;'), so a field can never name its own
        // type, directly or via a cycle through another type -
        // no explicit self-reference/cycle check is needed.
        if (is_array) {
            compile_error(token.line, "Array field element type must be scalar, not a record type - 'array of %s' isn't supported as a field type", token.text);
        }
        nested_record_type_idx = find_record_type(token.text);
        if (record_type_has_array_field(nested_record_type_idx)) {
            compile_error(token.line, "'%s' can't be used as a nested field because it has an array field - a record type used as a nested field can't contain array fields (see docs/LANGUAGE.md)", token.text);
        }
        is_nested_record = 1;
        match(TOKEN_IDENTIFIER);
    } else {
        field_type = parse_scalar_type();
    }

    for (int i = 0; i < fcount; i++) {
        if (rt->field_count >= MAX_RECORD_FIELDS) {
            compile_error(token.line, "Too many fields in record '%s' (limit is %d)", rt->name, MAX_RECORD_FIELDS);
        }
        if (find_record_field(record_type_idx, field_names[i]) != -1) {
            compile_error(token.line, "Duplicate field '%s' in record '%s'", field_names[i], rt->name);
        }
        RecordField *f = &rt->fields[rt->field_count];
        strcpy(f->name, field_names[i]);
        f->is_record = is_nested_record;
        f->record_type_idx = nested_record_type_idx;
        f->type = field_type;
        f->is_array = is_array;
        f->array_lower = lower;
        f->array_upper = upper;
        f->is_subrange = is_nested_record ? 0 : scalar_type_is_subrange;
        f->subrange_lower = is_nested_record ? 0 : scalar_type_subrange_lower;
        f->subrange_upper = is_nested_record ? 0 : scalar_type_subrange_upper;
        f->disk_width = is_nested_record ? 0 : scalar_type_disk_width;
        f->is_private = is_private;
        f->is_protected = is_protected;
        f->declaring_class_ptr_idx = declaring_class_ptr_idx;
        rt->field_count++;
    }
}

// Parses a record's optional variant part: 'case tagname: tagtype of
// label[, label...]: (fieldgroup; ...); ... end' - shares the record's
// own closing 'end' (there's no separate 'end' for the case).
//
// Scoping decision (see docs/LANGUAGE.md#variant-records): this
// compiler's records already have no real memory layout of their own -
// a record variable is just N independent hidden globals/locals created
// at parse time (see the "How this is implemented" note above plain
// records). Building genuine overlapping storage for variants would
// mean inventing a real addressing model for records from scratch, well
// beyond this feature's scope. So here, the tag field plus EVERY
// variant's fields are appended to rt->fields[] as ordinary
// non-overlapping fields, exactly like plain fields - only the label
// list is used for anything case-like (parse-time type/distinctness
// validation against the tag type). There is no runtime tag dispatch
// and no storage sharing between variants.
static void parse_record_variant_part(RecordTypeDef *rt, int record_type_idx) {
    match(TOKEN_CASE);
    char tag_name[MAX_NAME];
    strcpy(tag_name, token.text);
    int tag_line = token.line;
    match(TOKEN_IDENTIFIER);
    match(TOKEN_COLON);
    DataType tag_type = parse_scalar_type();
    if (tag_type != TYPE_INTEGER && tag_type != TYPE_CHAR && tag_type != TYPE_BOOLEAN
        && !(tag_type >= TYPE_ENUM_BASE && tag_type < TYPE_POINTER_BASE)) {
        compile_error(tag_line, "Variant record tag field must be an ordinal type (integer, char, boolean, or enumerated)");
    }
    if (scalar_type_is_subrange) {
        compile_error(tag_line, "Variant record tag field can't be a subrange type");
    }
    match(TOKEN_OF);

    // The tag itself becomes an ordinary field, like any other.
    if (rt->field_count >= MAX_RECORD_FIELDS) {
        compile_error(tag_line, "Too many fields in record '%s' (limit is %d)", rt->name, MAX_RECORD_FIELDS);
    }
    if (find_record_field(record_type_idx, tag_name) != -1) {
        compile_error(tag_line, "Duplicate field '%s' in record '%s'", tag_name, rt->name);
    }
    RecordField *tagf = &rt->fields[rt->field_count];
    strcpy(tagf->name, tag_name);
    tagf->type = tag_type;
    tagf->is_record = 0;
    tagf->is_array = 0;
    tagf->is_subrange = 0;
    tagf->disk_width = 0;
    rt->field_count++;

    DataType seen_types[MAX_CASE_LABELS];
    int seen_values[MAX_CASE_LABELS];
    int seen_count = 0;
    int variant_count = 0;

    while (token.type != TOKEN_END) {
        while (1) {
            int label_line = token.line;
            ASTNode *label = parse_case_label_value();
            if (label->expression_type != tag_type) {
                compile_error(label_line, "Variant label's type doesn't match the tag field '%s''s type", tag_name);
            }
            for (int i = 0; i < seen_count; i++) {
                if (seen_types[i] == label->expression_type && seen_values[i] == label->data.num_value) {
                    compile_error(label_line, "Duplicate variant label");
                }
            }
            if (seen_count >= MAX_CASE_LABELS) {
                compile_error(label_line, "Too many variant labels (limit is %d)", MAX_CASE_LABELS);
            }
            seen_types[seen_count] = label->expression_type;
            seen_values[seen_count] = label->data.num_value; // aliases data.var_idx too (same union member) - fine for a char label, which sets var_idx instead
            seen_count++;

            if (token.type == TOKEN_COMMA) { match(TOKEN_COMMA); continue; }
            break;
        }
        match(TOKEN_COLON);
        match(TOKEN_LPAREN);
        while (token.type == TOKEN_IDENTIFIER) {
            parse_record_field_group(rt, record_type_idx, 0, 0, -1); // plain record - no visibility concept
            if (token.type == TOKEN_SEMI) { match(TOKEN_SEMI); continue; }
            break;
        }
        match(TOKEN_RPAREN);
        variant_count++;

        if (token.type == TOKEN_SEMI) { match(TOKEN_SEMI); continue; }
        break;
    }
    if (variant_count == 0) {
        compile_error(token.line, "'case' in a variant record must have at least one variant");
    }
}

// True if two class method headers have the same signature (is_function/
// return_type/param_count/param_types[i]/param_is_var[i]) - used to
// require an override to actually be signature-compatible with what it
// overrides (see parse_class_declaration()'s inheritance handling).
// Deliberately ignores .name/.param_names[]/.mangled_name/.is_inherited -
// none of those are part of a signature.
static int proc_param_headers_match(ProcParamHeader *a, ProcParamHeader *b) {
    if (a->is_function != b->is_function) return 0;
    if (a->is_function && a->return_type != b->return_type) return 0;
    if (a->param_count != b->param_count) return 0;
    for (int i = 0; i < a->param_count; i++) {
        if (a->param_types[i] != b->param_types[i]) return 0;
        if (a->param_is_var[i] != b->param_is_var[i]) return 0;
    }
    return 1;
}

// Parses a class type declaration: 'class [(ParentName)] <fields>
// <method headers> end;' - already past 'type TFoo =', with token
// positioned at the 'class' keyword itself. Registers TWO things, per
// the design decision in notes/classes-and-instances-scoping.md
// (reference semantics): a hidden RecordTypeDef for the fields, and
// class_name itself as pointer_types[]'s own entry (is_class = 1,
// target_is_record = 1, targeting that hidden record type) - so every
// existing pointer-typed variable/parameter/field/new/dispose mechanism
// already works for a class variable completely unmodified (see
// PointerTypeDef.name's comment). The hidden record type is deliberately
// named "$classN" - '$' can't start (or appear in) a legal Pascal
// identifier, so this can never collide with a user-written name, and
// deliberately ISN'T class_name itself, so find_record_type(class_name)
// still returns -1 and a class can't be accidentally (mis)used as if it
// were an ordinary nested-record field naming it directly (composition
// isn't supported yet - see the scoping note). Known small rough edge:
// a field-list error inside a class (duplicate/too-many field name)
// will name this internal "$classN" record, not the class itself, since
// parse_record_field_group()'s own error messages reference rt->name
// directly - harmless (still a clean, correct compile error) but worth
// polishing later.
//
// Inheritance ('class TCircle(TShape) ... end;') is fully FLATTENED
// here, at declaration time - not resolved later, and not tracked as a
// live relationship anywhere except parent_class_ptr_idx (kept only for
// class_type_is_subtype_of()'s assignment/parameter-passing
// compatibility check, in type_checker.c):
//   - EVERY ancestor field is copied into this class's OWN rt->fields[],
//     in order, BEFORE this class's own fields are parsed - so a
//     subclass's fields always start with an exact copy of its parent's
//     own layout (which itself already includes ITS parent's, and so
//     on), keeping every ancestor's own field offsets valid against a
//     descendant's larger heap block. A field name colliding with an
//     inherited one is rejected by the ordinary, unchanged
//     find_record_field() duplicate check inside parse_record_field_group() -
//     fields can never be overridden, only added.
//   - EVERY ancestor method header is likewise copied into this class's
//     OWN pt->methods[], marked is_inherited = 1. If this class then
//     declares a header with the SAME name, that's an OVERRIDE: it must
//     have the identical signature (proc_param_headers_match()), and it
//     REPLACES the inherited entry in place (own mangled_name,
//     is_inherited = 0) rather than being rejected as a duplicate -
//     tracked via inherited_method_count, the boundary between "copied
//     from the parent" and "declared fresh by this class" entries.
//   - h.mangled_name is stored on EVERY header (not just recomputed at
//     each call site) precisely so a call through a subclass reaches
//     the right implementation: resolve_heap_deref_step() just calls
//     find_proc(h->mangled_name) - for an inherited, not-overridden
//     method that's the ANCESTOR's own mangled name, copied through
//     unchanged; for an overridden or newly-declared one, it's this
//     class's own.
//
// v1 scope: fields (scalar only - no array or nested-record fields yet,
// a known gap) and method HEADERS only, parsed via the exact same
// parse_proc_param_header() functional/procedural parameters already
// use, and stored on the class's PointerTypeDef entry for
// parse_class_method_body() to consume later. Since class_name isn't
// registered as a pointer type until this whole declaration finishes, a
// method can't reference its own class in its signature yet (e.g. a
// linked-list-style 'SetNext(n: TFoo)') - the same declare-before-use
// restriction every other type in this compiler already has (a parent
// class, being a SEPARATE, already-fully-declared type, has no such
// restriction).
static void parse_class_declaration(const char *class_name, int line) {
    match(TOKEN_CLASS);

    int class_is_sealed = 0;
    if (token.type == TOKEN_SEALED) {
        match(TOKEN_SEALED);
        class_is_sealed = 1;
    }

    int parent_ptr_idx = -1;
    if (token.type == TOKEN_LPAREN) {
        match(TOKEN_LPAREN);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "Expected a parent class name after '('");
        }
        parent_ptr_idx = find_pointer_type(token.text);
        if (parent_ptr_idx == -1 || !pointer_types[parent_ptr_idx].is_class) {
            compile_error(token.line, "'%s' is not a declared class", token.text);
        }
        if (pointer_types[parent_ptr_idx].is_sealed) {
            compile_error(token.line, "Cannot inherit from sealed class '%s'", token.text);
        }
        match(TOKEN_IDENTIFIER);
        match(TOKEN_RPAREN);
    }
    PointerTypeDef *parent = parent_ptr_idx == -1 ? NULL : &pointer_types[parent_ptr_idx];

    if (record_type_count >= MAX_RECORD_TYPES) {
        compile_error(line, "Too many record types (limit is %d)", MAX_RECORD_TYPES);
    }
    int rt_idx = record_type_count;
    RecordTypeDef *rt = &record_types[rt_idx];
    snprintf(rt->name, MAX_NAME, "$class%d", rt_idx);
    rt->field_count = 0;

    if (parent != NULL) {
        // No overflow check needed here: parent_rt->field_count is
        // already <= MAX_RECORD_FIELDS by construction (checked when
        // the parent itself was declared) - if copying it plus this
        // class's own fields overflows, parse_record_field_group()'s
        // own existing MAX_RECORD_FIELDS check (using the running
        // rt->field_count, already seeded with the inherited baseline
        // by the time it runs) catches it correctly.
        RecordTypeDef *parent_rt = &record_types[parent->target_record_type_idx];
        for (int i = 0; i < parent_rt->field_count; i++) {
            rt->fields[rt->field_count] = parent_rt->fields[i];
            rt->field_count++;
        }
    }

    // pt setup (and every inheritance-copy loop) moved HERE, ahead of the
    // field loop below - the field loop now also parses 'class var'
    // groups (see its own TOKEN_CLASS branch), which need pt->class_vars[]
    // to already exist. Harmless to hoist: pt->method_count/property_count/
    // class_var_count are each explicitly zeroed right where they're used
    // either way, just slightly earlier now.
    if (pointer_type_count >= MAX_POINTER_TYPES) {
        compile_error(line, "Too many pointer type declarations (limit is %d)", MAX_POINTER_TYPES);
    }
    PointerTypeDef *pt = &pointer_types[pointer_type_count];
    strcpy(pt->name, class_name);
    pt->is_class = 1;
    pt->target_is_record = 1;
    pt->target_record_type_idx = rt_idx;
    pt->is_pending = 0;
    pt->parent_class_ptr_idx = parent_ptr_idx;
    pt->is_sealed = class_is_sealed;
    pt->method_count = 0;

    if (parent != NULL) {
        // Same reasoning as the field copy above: parent->method_count
        // is already <= MAX_CLASS_METHODS by construction, and the
        // method-parsing loop below's own existing MAX_CLASS_METHODS
        // check (against the running, inherited-seeded pt->method_count)
        // catches any overflow from adding this class's own methods.
        // Covers TRUE class methods (ProcParamHeader.is_class_method)
        // too, unchanged - they're plain entries in this same table.
        for (int i = 0; i < parent->method_count; i++) {
            pt->methods[pt->method_count] = parent->methods[i];
            pt->methods[pt->method_count].is_inherited = 1;
            pt->method_count++;
        }
    }
    pt->property_count = 0;
    if (parent != NULL) {
        // Properties follow FIELDS' inheritance rule, not methods' - add-
        // only, never overridden in v1 (see the property-parsing loop's own
        // duplicate-name check below) - so, unlike the method copy above,
        // no is_inherited marking is needed here. Same overflow reasoning
        // as the field/method copies: parent->property_count is already
        // <= MAX_CLASS_PROPERTIES by construction. Covers TRUE class
        // properties (ClassProperty.is_class_property) too, unchanged.
        for (int i = 0; i < parent->property_count; i++) {
            pt->properties[pt->property_count] = parent->properties[i];
            pt->property_count++;
        }
    }
    pt->class_var_count = 0;
    if (parent != NULL) {
        // UNCHANGED struct-copy (same sym_idx, never re-added via
        // add_var()) - this is what makes a class var's storage
        // genuinely SHARED between the declaring class and every
        // descendant, rather than an independent per-subclass copy the
        // way an ordinary field gets (see ClassVar's own comment).
        for (int i = 0; i < parent->class_var_count; i++) {
            pt->class_vars[pt->class_var_count] = parent->class_vars[i];
            pt->class_var_count++;
        }
    }

    // Tracks the currently-active 'private'/'public' section - persists
    // across ALL THREE of this field loop, the method loop, and the
    // property loop below (one shared state, not reset in between),
    // since this compiler's class grammar parses all fields first, then
    // all methods, then all properties, unlike real Pascal's free
    // interleaving of the two - so writing a 'private'/'public' section
    // among the methods too works, just not interleaved with fields
    // within one section. Defaults to public, so an existing class using
    // neither keyword is completely unaffected. pointer_type_count
    // itself (not yet incremented - pt above already points at
    // &pointer_types[pointer_type_count]) is already this class's own
    // future pointer_types[] index by construction, needed as
    // declaring_class_ptr_idx for every field/method/property/class var
    // this class declares itself (as opposed to inherits).
    int current_is_private = 0;
    int current_is_protected = 0; // mutually exclusive with current_is_private -
                                   // see is_protected's own comment on
                                   // RecordField/ProcParamHeader/ClassProperty/
                                   // ClassVar for the enforcement side.
    while (token.type == TOKEN_IDENTIFIER || token.type == TOKEN_PRIVATE || token.type == TOKEN_PUBLIC || token.type == TOKEN_PROTECTED || token.type == TOKEN_CLASS) {
        if (token.type == TOKEN_PRIVATE) { match(TOKEN_PRIVATE); current_is_private = 1; current_is_protected = 0; continue; }
        if (token.type == TOKEN_PUBLIC) { match(TOKEN_PUBLIC); current_is_private = 0; current_is_protected = 0; continue; }
        if (token.type == TOKEN_PROTECTED) { match(TOKEN_PROTECTED); current_is_private = 0; current_is_protected = 1; continue; }
        if (token.type == TOKEN_CLASS) {
            // 'class var Name: Type;' (a class var, handled here since
            // it shares the field loop's own name-group/type syntax) vs.
            // 'class procedure'/'class function' (method-loop territory)
            // vs. 'class property' (property-loop territory) - all start
            // with the same TOKEN_CLASS, so a one-token peek is required
            // to tell them apart before committing. Uses the lexer's
            // existing save/restore primitives (already used for a much
            // bigger backtrack, a full unit re-parse - see load_unit()).
            Token saved_token = token;
            LexerPos saved_pos = lexer_save_pos();
            next_token(); // peek past 'class'
            if (token.type == TOKEN_VAR) {
                match(TOKEN_VAR);
                int group_line = token.line; // capture BEFORE parse_name_group() - NameGroup has no line field, and token has moved past the whole group by the time it returns
                NameGroup g = parse_name_group();
                if (g.is_array || g.is_record || g.is_array_of_record) {
                    compile_error(group_line, "A class var must be a scalar type - array and record class vars aren't supported yet");
                }
                for (int i = 0; i < g.count; i++) {
                    if (find_record_field(rt_idx, g.names[i]) != -1) {
                        compile_error(group_line, "'%s' is already a field of class '%s'", g.names[i], class_name);
                    }
                    int dup = 0;
                    for (int j = 0; j < pt->class_var_count; j++) {
                        if (strcmp(pt->class_vars[j].name, g.names[i]) == 0) { dup = 1; break; }
                    }
                    if (dup) {
                        compile_error(group_line, "Duplicate class variable '%s' in class '%s'", g.names[i], class_name);
                    }
                    if (pt->class_var_count >= MAX_CLASS_VARS) {
                        compile_error(group_line, "Class '%s' has too many class variables (limit is %d)", class_name, MAX_CLASS_VARS);
                    }
                    char mangled[2 * MAX_NAME];
                    snprintf(mangled, sizeof(mangled), "%s__%s", class_name, g.names[i]);
                    int sym_idx = sym_count;
                    add_var(mangled, g.type);
                    sym_table[sym_idx].is_subrange = g.is_subrange;
                    sym_table[sym_idx].subrange_lower = g.subrange_lower;
                    sym_table[sym_idx].subrange_upper = g.subrange_upper;
                    ClassVar *cv = &pt->class_vars[pt->class_var_count++];
                    strcpy(cv->name, g.names[i]);
                    cv->sym_idx = sym_idx;
                    cv->type = g.type;
                    cv->is_subrange = g.is_subrange;
                    cv->subrange_lower = g.subrange_lower;
                    cv->subrange_upper = g.subrange_upper;
                    cv->is_private = current_is_private;
                    cv->is_protected = current_is_protected;
                    cv->declaring_class_ptr_idx = pointer_type_count;
                }
                match(TOKEN_SEMI);
                continue;
            }
            // Not 'class var' - restore the peek and hand off to the
            // method loop below, which re-peeks this same TOKEN_CLASS
            // for 'class procedure'/'class function'.
            token = saved_token;
            lexer_restore_pos(saved_pos);
            break;
        }
        // Array-typed and nested-record fields are both accepted here -
        // parse_record_field_group() already enforces every restriction
        // that still applies (a nested field's own type must be array-
        // field-free). class_field_heap_slots()/class_field_base_offset()
        // account for either field kind's real heap-slot cost, and the
        // MAX_RECORD_FIELDS + 1 overflow guard below (this function's own
        // target_elem_size check) catches an oversized combination of
        // either kind at compile time.
        parse_record_field_group(rt, rt_idx, current_is_private, current_is_protected, pointer_type_count);
        match(TOKEN_SEMI);
    }
    record_type_count++;

    int inherited_method_count = pt->method_count;
    // Tracks which INHERITED slots this class has already overridden -
    // needed so overriding the SAME inherited method twice in one class
    // body is still caught as a duplicate, not silently accepted as
    // "overriding the override" (existing_idx alone can't tell those
    // apart, since replacing an inherited entry in place doesn't move
    // it past inherited_method_count).
    int already_overridden[MAX_CLASS_METHODS] = {0};

    while (token.type == TOKEN_PROCEDURE || token.type == TOKEN_FUNCTION || token.type == TOKEN_DESTRUCTOR || token.type == TOKEN_PRIVATE || token.type == TOKEN_PUBLIC || token.type == TOKEN_PROTECTED || token.type == TOKEN_CLASS) {
        if (token.type == TOKEN_PRIVATE) { match(TOKEN_PRIVATE); current_is_private = 1; current_is_protected = 0; continue; }
        if (token.type == TOKEN_PUBLIC) { match(TOKEN_PUBLIC); current_is_private = 0; current_is_protected = 0; continue; }
        if (token.type == TOKEN_PROTECTED) { match(TOKEN_PROTECTED); current_is_private = 0; current_is_protected = 1; continue; }
        int is_class_method_decl = 0;
        if (token.type == TOKEN_CLASS) {
            // 'class procedure'/'class function' (a TRUE class method) vs.
            // 'class property' (property-loop territory) - one-token
            // peek, same primitive the field loop's own TOKEN_CLASS
            // branch already uses.
            Token saved_token = token;
            LexerPos saved_pos = lexer_save_pos();
            next_token(); // peek past 'class'
            if (token.type == TOKEN_PROCEDURE || token.type == TOKEN_FUNCTION) {
                is_class_method_decl = 1;
                // token is now PROCEDURE/FUNCTION - fall through below,
                // parse_proc_param_header() picks up from here.
            } else if (token.type == TOKEN_PROPERTY) {
                token = saved_token;
                lexer_restore_pos(saved_pos);
                break;
            } else {
                compile_error(token.line, "Expected 'procedure', 'function', or 'property' after 'class' here");
            }
        }
        int method_line = token.line;
        ProcParamHeader h;
        int is_destructor_decl = 0;
        if (token.type == TOKEN_DESTRUCTOR) {
            // 'destructor Name;' - an alternative to procedure/function,
            // parsed by hand since parse_proc_param_header() hard-codes
            // matching TOKEN_FUNCTION/TOKEN_PROCEDURE as its own literal
            // first token. Reuses parse_proc_signature_tail() directly -
            // already separate from parse_proc_param_header(), with no
            // leading-keyword assumption of its own - for the optional
            // '(params)' (rejected below if any are given; a trailing
            // ': ReturnType' is simply never looked for since is_function
            // stays 0, so 'destructor Foo: integer;' falls through to a
            // generic token-mismatch error at the match(TOKEN_SEMI)
            // below rather than a dedicated message - an accepted, minor
            // rough edge, not gold-plated).
            match(TOKEN_DESTRUCTOR);
            h.is_function = 0;
            if (token.type != TOKEN_IDENTIFIER) {
                compile_error(token.line, "Expected a destructor name after 'destructor'");
            }
            strcpy(h.name, token.text);
            match(TOKEN_IDENTIFIER);
            parse_proc_signature_tail(&h, 0); // moot - rejected just below if any params were given
            if (h.param_count > 0) {
                compile_error(method_line, "'%s' is a destructor and can't take parameters", h.name);
            }
            is_destructor_decl = 1;
        } else {
            h = parse_proc_param_header(1); // class method header - const/out allowed
        }
        h.is_class_method = is_class_method_decl;
        h.is_destructor = is_destructor_decl;
        match(TOKEN_SEMI);
        h.is_abstract = 0;
        if (token.type == TOKEN_ABSTRACT) {
            // No separate 'virtual' keyword exists in this compiler -
            // every instance method is already always virtually
            // dispatched (see docs/LANGUAGE.md#classes) - so 'abstract'
            // alone is the complete modifier, unlike Delphi's 'virtual;
            // abstract;' pair.
            if (h.is_class_method) {
                compile_error(token.line, "'%s' can't be abstract - a class method is never overridden, so it would never have an implementation", h.name);
            }
            if (h.is_destructor) {
                compile_error(token.line, "'%s' is a destructor and can't be abstract", h.name);
            }
            match(TOKEN_ABSTRACT);
            match(TOKEN_SEMI);
            h.is_abstract = 1;
        }
        if (find_record_field(rt_idx, h.name) != -1) {
            compile_error(method_line, "'%s' is already a field of class '%s'", h.name, class_name);
        }
        for (int i = 0; i < pt->class_var_count; i++) {
            if (strcmp(pt->class_vars[i].name, h.name) == 0) {
                compile_error(method_line, "'%s' is already a class variable of class '%s'", h.name, class_name);
            }
        }
        snprintf(h.mangled_name, MAX_NAME, "%s__%s", class_name, h.name);
        h.is_inherited = 0;
        h.is_private = current_is_private;
        h.is_protected = current_is_protected;
        h.declaring_class_ptr_idx = pointer_type_count;

        int existing_idx = -1;
        for (int i = 0; i < pt->method_count; i++) {
            if (strcmp(pt->methods[i].name, h.name) == 0) { existing_idx = i; break; }
        }
        // A class hierarchy has at most ONE destructor, found by flag
        // (is_destructor) not by name - proc_param_headers_match() below
        // deliberately doesn't compare is_destructor (same reasoning as
        // is_abstract/is_class_method), so without this check a subclass
        // could silently "override" an inherited destructor with a
        // plain procedure of the same signature, replacing the entry
        // with one that has is_destructor = 0 - dispose() would then
        // silently stop finding it in that subclass, with no error at
        // all. Checked BEFORE the override/duplicate chain below, since
        // that chain has no way to express "same name, different kind."
        if (existing_idx == -1) {
            if (h.is_destructor) {
                for (int i = 0; i < pt->method_count; i++) {
                    if (pt->methods[i].is_destructor) {
                        compile_error(method_line, "class '%s' already has a destructor '%s' (declared in '%s') - a class can have at most one; override it instead of declaring a new one",
                                       class_name, pt->methods[i].name, pointer_types[pt->methods[i].declaring_class_ptr_idx].name);
                    }
                }
            }
        } else if (h.is_destructor != pt->methods[existing_idx].is_destructor) {
            if (h.is_destructor) {
                compile_error(method_line, "'%s' is inherited as an ordinary method - it can't be redeclared as a destructor", h.name);
            } else {
                compile_error(method_line, "'%s' is the class's destructor and must be redeclared with 'destructor', not '%s'", h.name, h.is_function ? "function" : "procedure");
            }
        }
        if (existing_idx == -1) {
            if (pt->method_count >= MAX_CLASS_METHODS) {
                compile_error(method_line, "Class '%s' has too many methods (limit is %d)", class_name, MAX_CLASS_METHODS);
            }
            pt->methods[pt->method_count] = h;
            pt->method_count++;
        } else if (existing_idx < inherited_method_count && !already_overridden[existing_idx]
                   && !h.is_class_method && !pt->methods[existing_idx].is_class_method) {
            // An override - must match the inherited signature exactly.
            // A TRUE class method is never overridable (no vtable slot,
            // ever - see build_vtable_init_chain()'s own skip) - any
            // collision involving one (new-vs-inherited-instance, new-
            // vs-inherited-class, new-instance-vs-inherited-class) falls
            // through to the plain "Duplicate method" error below instead.
            if (!proc_param_headers_match(&pt->methods[existing_idx], &h)) {
                compile_error(method_line, "'%s.%s' overrides an inherited method with a different signature - an override's parameter/return types must match exactly", class_name, h.name);
            }
            pt->methods[existing_idx] = h;
            already_overridden[existing_idx] = 1;
        } else {
            compile_error(method_line, "Duplicate method '%s' in class '%s'", h.name, class_name);
        }
        if (h.is_abstract) {
            register_abstract_method_signature(&h, pointer_type_count);
        }
    }

    // Properties come AFTER fields and methods above - a property's
    // read/write target may be a field or method declared anywhere in this
    // class body, and by this point rt->fields[]/pt->methods[] are both
    // guaranteed fully populated (including any inherited entries copied in
    // above), so lookup order within THIS loop doesn't matter.
    while (token.type == TOKEN_PROPERTY || token.type == TOKEN_PRIVATE || token.type == TOKEN_PUBLIC || token.type == TOKEN_PROTECTED || token.type == TOKEN_CLASS) {
        if (token.type == TOKEN_PRIVATE) { match(TOKEN_PRIVATE); current_is_private = 1; current_is_protected = 0; continue; }
        if (token.type == TOKEN_PUBLIC) { match(TOKEN_PUBLIC); current_is_private = 0; current_is_protected = 0; continue; }
        if (token.type == TOKEN_PROTECTED) { match(TOKEN_PROTECTED); current_is_private = 0; current_is_protected = 1; continue; }
        int is_class_property_decl = 0;
        if (token.type == TOKEN_CLASS) {
            // This is the LAST of the three declaration loops, so unlike
            // the field/method loops' own TOKEN_CLASS branches, there's
            // no further loop to hand off to if what follows isn't
            // 'property' - a plain compile_error() is correct here.
            match(TOKEN_CLASS);
            if (token.type != TOKEN_PROPERTY) {
                compile_error(token.line, "Expected 'property' after 'class' here");
            }
            is_class_property_decl = 1;
        }

        int prop_line = token.line;
        match(TOKEN_PROPERTY);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "Expected a property name after 'property'");
        }
        char prop_name[MAX_NAME];
        strcpy(prop_name, token.text);
        match(TOKEN_IDENTIFIER);

        // Properties share one flat namespace with fields, methods, and
        // class vars, exactly like methods already do with fields (see
        // the method loop's own find_record_field() check above).
        if (find_record_field(rt_idx, prop_name) != -1) {
            compile_error(prop_line, "'%s' is already a field of class '%s'", prop_name, class_name);
        }
        for (int i = 0; i < pt->method_count; i++) {
            if (strcmp(pt->methods[i].name, prop_name) == 0) {
                compile_error(prop_line, "'%s' is already a method of class '%s'", prop_name, class_name);
            }
        }
        for (int i = 0; i < pt->property_count; i++) {
            if (strcmp(pt->properties[i].name, prop_name) == 0) {
                compile_error(prop_line, "Duplicate property '%s' in class '%s'", prop_name, class_name);
            }
        }
        for (int i = 0; i < pt->class_var_count; i++) {
            if (strcmp(pt->class_vars[i].name, prop_name) == 0) {
                compile_error(prop_line, "'%s' is already a class variable of class '%s'", prop_name, class_name);
            }
        }

        match(TOKEN_COLON);
        DataType prop_type = parse_scalar_type();

        if (token.type != TOKEN_READ) {
            compile_error(token.line, "Expected 'read' in property '%s' declaration", prop_name);
        }
        match(TOKEN_READ);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "Expected a field or function name after 'read'");
        }
        char read_name[MAX_NAME];
        int read_line = token.line;
        strcpy(read_name, token.text);
        match(TOKEN_IDENTIFIER);

        ClassProperty prop;
        strcpy(prop.name, prop_name);
        prop.type = prop_type;
        prop.is_private = current_is_private;
        prop.is_protected = current_is_protected;
        prop.declaring_class_ptr_idx = pointer_type_count;
        prop.has_write = 0;
        prop.is_class_property = is_class_property_decl;

        if (is_class_property_decl) {
            int cvi = -1;
            for (int i = 0; i < pt->class_var_count; i++) {
                if (strcmp(pt->class_vars[i].name, read_name) == 0) { cvi = i; break; }
            }
            if (cvi != -1) {
                if (pt->class_vars[cvi].type != prop_type) {
                    compile_error(read_line, "Property '%s' read target class var '%s' has a different type than the property itself", prop_name, read_name);
                }
                prop.read_is_field = 1;
                prop.read_idx = cvi;
            } else {
                int mi = -1;
                for (int i = 0; i < pt->method_count; i++) {
                    if (strcmp(pt->methods[i].name, read_name) == 0) { mi = i; break; }
                }
                if (mi == -1) {
                    compile_error(read_line, "'%s' is not a class variable or class method of class '%s' (property '%s' read target)", read_name, class_name, prop_name);
                }
                ProcParamHeader *rh = &pt->methods[mi];
                if (!rh->is_class_method) {
                    compile_error(read_line, "Property '%s' read target '%s' must be a class method, not an instance method (or declare '%s' as an instance property instead)", prop_name, read_name, prop_name);
                }
                if (!rh->is_function) {
                    compile_error(read_line, "Property '%s' read target '%s' must be a function (it's a procedure)", prop_name, read_name);
                }
                if (rh->param_count != 0) {
                    compile_error(read_line, "Property '%s' read target '%s' must take no arguments (it takes %d)", prop_name, read_name, rh->param_count);
                }
                if (rh->return_type != prop_type) {
                    compile_error(read_line, "Property '%s' read target function '%s' returns a different type than the property itself", prop_name, read_name);
                }
                prop.read_is_field = 0;
                prop.read_idx = mi;
            }
        } else {
            int rfi = find_record_field(rt_idx, read_name);
            if (rfi != -1) {
                RecordField *rf = &rt->fields[rfi];
                if (rf->is_array || rf->is_record) {
                    compile_error(read_line, "Property '%s' read target '%s' must be a scalar field (array/nested-record fields aren't supported as a property target)", prop_name, read_name);
                }
                if (rf->type != prop_type) {
                    compile_error(read_line, "Property '%s' read target field '%s' has a different type than the property itself", prop_name, read_name);
                }
                prop.read_is_field = 1;
                prop.read_idx = rfi;
            } else {
                int mi = -1;
                for (int i = 0; i < pt->method_count; i++) {
                    if (strcmp(pt->methods[i].name, read_name) == 0) { mi = i; break; }
                }
                if (mi == -1) {
                    compile_error(read_line, "'%s' is not a field or method of class '%s' (property '%s' read target)", read_name, class_name, prop_name);
                }
                ProcParamHeader *rh = &pt->methods[mi];
                if (rh->is_class_method) {
                    // Symmetric to the class-property guard above -
                    // resolve_heap_deref_step()'s property-read branch
                    // unconditionally splices 'self' into the getter
                    // call, so an unchecked instance property backed by
                    // a class method (no 'self' at slot 0) would corrupt
                    // the stack at runtime, not just fail to compile.
                    compile_error(read_line, "Property '%s' read target '%s' is a class method - declare '%s' as a 'class property' instead", prop_name, read_name, prop_name);
                }
                if (!rh->is_function) {
                    compile_error(read_line, "Property '%s' read target '%s' must be a function (it's a procedure)", prop_name, read_name);
                }
                if (rh->param_count != 0) {
                    compile_error(read_line, "Property '%s' read target '%s' must take no arguments (it takes %d)", prop_name, read_name, rh->param_count);
                }
                if (rh->return_type != prop_type) {
                    compile_error(read_line, "Property '%s' read target function '%s' returns a different type than the property itself", prop_name, read_name);
                }
                prop.read_is_field = 0;
                prop.read_idx = mi;
            }
        }

        if (token.type == TOKEN_WRITE) {
            match(TOKEN_WRITE);
            if (token.type != TOKEN_IDENTIFIER) {
                compile_error(token.line, "Expected a field or procedure name after 'write'");
            }
            char write_name[MAX_NAME];
            int write_line = token.line;
            strcpy(write_name, token.text);
            match(TOKEN_IDENTIFIER);

            if (is_class_property_decl) {
                int cvi = -1;
                for (int i = 0; i < pt->class_var_count; i++) {
                    if (strcmp(pt->class_vars[i].name, write_name) == 0) { cvi = i; break; }
                }
                if (cvi != -1) {
                    if (pt->class_vars[cvi].type != prop_type) {
                        compile_error(write_line, "Property '%s' write target class var '%s' has a different type than the property itself", prop_name, write_name);
                    }
                    prop.write_is_field = 1;
                    prop.write_idx = cvi;
                } else {
                    int mi = -1;
                    for (int i = 0; i < pt->method_count; i++) {
                        if (strcmp(pt->methods[i].name, write_name) == 0) { mi = i; break; }
                    }
                    if (mi == -1) {
                        compile_error(write_line, "'%s' is not a class variable or class method of class '%s' (property '%s' write target)", write_name, class_name, prop_name);
                    }
                    ProcParamHeader *wh = &pt->methods[mi];
                    if (!wh->is_class_method) {
                        compile_error(write_line, "Property '%s' write target '%s' must be a class method, not an instance method", prop_name, write_name);
                    }
                    if (wh->is_function) {
                        compile_error(write_line, "Property '%s' write target '%s' must be a procedure (it's a function)", prop_name, write_name);
                    }
                    if (wh->param_count != 1) {
                        compile_error(write_line, "Property '%s' write target '%s' must take exactly one argument (it takes %d)", prop_name, write_name, wh->param_count);
                    }
                    if (wh->param_is_var[0]) {
                        compile_error(write_line, "Property '%s' write target '%s' can't take its argument by 'var'", prop_name, write_name);
                    }
                    if (wh->param_types[0] != prop_type) {
                        compile_error(write_line, "Property '%s' write target '%s' takes a different type than the property itself", prop_name, write_name);
                    }
                    prop.write_is_field = 0;
                    prop.write_idx = mi;
                }
            } else {
                int wfi = find_record_field(rt_idx, write_name);
                if (wfi != -1) {
                    RecordField *wf = &rt->fields[wfi];
                    if (wf->is_array || wf->is_record) {
                        compile_error(write_line, "Property '%s' write target '%s' must be a scalar field", prop_name, write_name);
                    }
                    if (wf->type != prop_type) {
                        compile_error(write_line, "Property '%s' write target field '%s' has a different type than the property itself", prop_name, write_name);
                    }
                    prop.write_is_field = 1;
                    prop.write_idx = wfi;
                } else {
                    int mi = -1;
                    for (int i = 0; i < pt->method_count; i++) {
                        if (strcmp(pt->methods[i].name, write_name) == 0) { mi = i; break; }
                    }
                    if (mi == -1) {
                        compile_error(write_line, "'%s' is not a field or method of class '%s' (property '%s' write target)", write_name, class_name, prop_name);
                    }
                    ProcParamHeader *wh = &pt->methods[mi];
                    if (wh->is_class_method) {
                        compile_error(write_line, "Property '%s' write target '%s' is a class method - declare '%s' as a 'class property' instead", prop_name, write_name, prop_name);
                    }
                    if (wh->is_function) {
                        compile_error(write_line, "Property '%s' write target '%s' must be a procedure (it's a function)", prop_name, write_name);
                    }
                    if (wh->param_count != 1) {
                        compile_error(write_line, "Property '%s' write target '%s' must take exactly one argument (it takes %d)", prop_name, write_name, wh->param_count);
                    }
                    if (wh->param_is_var[0]) {
                        compile_error(write_line, "Property '%s' write target '%s' can't take its argument by 'var'", prop_name, write_name);
                    }
                    if (wh->param_types[0] != prop_type) {
                        compile_error(write_line, "Property '%s' write target '%s' takes a different type than the property itself", prop_name, write_name);
                    }
                    prop.write_is_field = 0;
                    prop.write_idx = mi;
                }
            }
            prop.has_write = 1;
        }

        match(TOKEN_SEMI);

        if (pt->property_count >= MAX_CLASS_PROPERTIES) {
            compile_error(prop_line, "Class '%s' has too many properties (limit is %d)", class_name, MAX_CLASS_PROPERTIES);
        }
        pt->properties[pt->property_count] = prop;
        pt->property_count++;
    }

    // class_field_base_offset(rt, rt->field_count) is 1 (the hidden
    // runtime type tag every class instance carries at heap offset 0 -
    // see resolve_heap_deref_step()'s own comment, and new()'s tag-
    // write) plus every field's own heap-slot cost (1 for a scalar,
    // more for a nested-record field) - exactly the record's total heap
    // footprint. Not a user-visible field, not part of
    // rt->fields[]/find_record_field() at all, so nothing about field
    // lookup/inheritance/duplicate checking above needed to change for
    // it. Only a class gets this slot - an ordinary 'type PFoo =
    // ^Target;' pointer's own target_elem_size (set elsewhere in this
    // file) is unaffected.
    pt->target_elem_size = class_field_base_offset(rt, rt->field_count);
    // vm_heap_freelist[MAX_RECORD_FIELDS + 1] (src/solvm/vm.c) is
    // indexed directly by elem_size with no runtime bounds check in
    // OP_NEW/OP_DISPOSE - safe only because target_elem_size has always
    // been <= MAX_RECORD_FIELDS + 1 before nested-record/array fields
    // could make a class's total heap footprint exceed its own field
    // count. Reject here so that invariant can never be broken by a
    // compiled program, rather than risking an out-of-bounds VM write.
    if (pt->target_elem_size > MAX_RECORD_FIELDS + 1) {
        compile_error(line, "Class '%s' is too large once its nested-record/array fields are flattened (needs %d heap slots, limit is %d) - reduce its field count, nesting depth, or array sizes", class_name, pt->target_elem_size, MAX_RECORD_FIELDS + 1);
    }
    match(TOKEN_END);
    match(TOKEN_SEMI);
    pointer_type_count++;
}

static void parse_type_section(void) {
    match(TOKEN_TYPE);
    while (token.type == TOKEN_IDENTIFIER) {
        int line = token.line;
        char type_name[MAX_NAME];
        strcpy(type_name, token.text);
        if (find_record_type(type_name) != -1 || find_type_alias(type_name) != -1
            || find_enum_type(type_name) != -1 || find_subrange_type(type_name) != -1
            || find_pointer_type(type_name) != -1 || find_proc_type(type_name) != -1) {
            compile_error(line, "Duplicate type declaration '%s'", type_name);
        }
        match(TOKEN_IDENTIFIER);
        match(TOKEN_EQ);

        if (token.type == TOKEN_CARET) {
            // A pointer type ('type PFoo = ^Target;'). Target can be:
            //  - a record type name, possibly not declared YET (a
            //    forward reference - the classic self-referential
            //    linked-list/tree pattern, 'PNode = ^TNode; TNode =
            //    record ... next: PNode; end;') - deferred to
            //    resolve_pending_pointer_types() below, once every type
            //    name in this section is known;
            //  - a record type name that's ALREADY declared - resolved
            //    immediately;
            //  - anything else parse_scalar_type() already knows how to
            //    resolve (a built-in keyword, an alias, an enum, a
            //    subrange, or even an earlier pointer type) - also
            //    resolved immediately, since none of those can be
            //    forward-referenced anyway (parse_scalar_type() already
            //    requires them declared first, exactly like every other
            //    reference to one).
            match(TOKEN_CARET);
            if (pointer_type_count >= MAX_POINTER_TYPES) {
                compile_error(line, "Too many pointer type declarations (limit is %d)", MAX_POINTER_TYPES);
            }
            PointerTypeDef *pt = &pointer_types[pointer_type_count];
            strcpy(pt->name, type_name);
            pt->is_class = 0;      // an ordinary 'type PFoo = ^Target;' -
            pt->method_count = 0;  // explicit reset: pointer_types[] is a
                                    // static array reused across compiles
                                    // in the same process (see
                                    // ARCHITECTURE.md's "global state, not
                                    // parameters" note), so a slot a PRIOR
                                    // compile used for a class must not
                                    // leak is_class/method_count here.
            if (token.type == TOKEN_IDENTIFIER && find_record_type(token.text) != -1) {
                int record_idx = find_record_type(token.text);
                if (record_type_has_nested_field(record_idx)) {
                    compile_error(token.line, "Pointer target record type '%s' has a nested-record field - pointers to a record with a nested-record field aren't supported yet (see docs/LANGUAGE.md)", token.text);
                }
                match(TOKEN_IDENTIFIER);
                pt->target_is_record = 1;
                pt->target_record_type_idx = record_idx;
                pt->target_elem_size = record_types[record_idx].field_count;
                pt->is_pending = 0;
            } else if (token.type == TOKEN_IDENTIFIER
                       && find_type_alias(token.text) == -1 && find_enum_type(token.text) == -1
                       && find_subrange_type(token.text) == -1 && find_pointer_type(token.text) == -1) {
                // Not found ANYWHERE yet - a forward reference to a
                // record type this SAME 'type' section will declare
                // later (or a genuine typo/unknown name, which won't be
                // distinguishable from a legitimate forward reference
                // until the whole section finishes parsing).
                strcpy(pt->pending_target_name, token.text);
                pt->pending_line = token.line;
                pt->is_pending = 1;
                match(TOKEN_IDENTIFIER);
            } else {
                pt->target_is_record = 0;
                pt->target_type = parse_scalar_type();
                pt->target_elem_size = 1;
                pt->is_pending = 0;
            }
            match(TOKEN_SEMI);
            pointer_type_count++;
            continue;
        }

        if (token.type == TOKEN_CLASS) {
            // 'type TFoo = class ... end;' - see parse_class_declaration()'s
            // own comment for the full design.
            parse_class_declaration(type_name, line);
            continue;
        }

        if (token.type == TOKEN_PROCEDURE || token.type == TOKEN_FUNCTION) {
            // A NAMED procedural type ('type TProc = procedure(x:
            // integer); TFunc = function: real;') - see
            // parse_proc_signature_tail()'s own comment for why this
            // reuses that shared helper instead of
            // parse_proc_param_header() (which expects a PARAMETER's own
            // name right after the keyword; a procedural type has none
            // of its own - type_name, already consumed before '=', is
            // the type's name). See docs/LANGUAGE.md#classes (Procedural
            // types) for the full design: the runtime representation is
            // a plain int (a top-level procedure/function's entry
            // address, or -1 for nil), exactly like a pointer, so a
            // variable/parameter/`var`-parameter of this type reuses
            // every existing scalar mechanism unmodified; only
            // assignment (parse_proc_value()) and calling through it
            // (build_procvar_call()) need dedicated parsing.
            if (proc_type_count >= MAX_PROC_TYPES) {
                compile_error(line, "Too many procedural type declarations (limit is %d)", MAX_PROC_TYPES);
            }
            ProcTypeDef *pd = &proc_types[proc_type_count];
            strcpy(pd->name, type_name);
            pd->sig.is_function = (token.type == TOKEN_FUNCTION);
            match(pd->sig.is_function ? TOKEN_FUNCTION : TOKEN_PROCEDURE);
            parse_proc_signature_tail(&pd->sig, 0); // named procedural type - const/out scope cut, see docs/LANGUAGE.md
            match(TOKEN_SEMI);
            proc_type_count++;
            continue;
        }

        if (token.type == TOKEN_LPAREN) {
            // An enumerated type: 'type TColor = (Red, Green, Blue);'.
            // Each value's ordinal is just its position in this list
            // (0, 1, 2, ...) - the same integer that ends up as its
            // actual runtime representation (see the TYPE_ENUM_BASE
            // comment in common.h). Every value name shares one flat
            // namespace across every enum type, matching real Pascal.
            if (enum_type_count >= MAX_ENUM_TYPES) {
                compile_error(line, "Too many enumerated type declarations (limit is %d)", MAX_ENUM_TYPES);
            }
            match(TOKEN_LPAREN);
            EnumTypeDef *et = &enum_types[enum_type_count];
            strcpy(et->name, type_name);
            et->value_count = 0;
            while (1) {
                if (token.type != TOKEN_IDENTIFIER) {
                    compile_error(token.line, "Expected an enumerated value name");
                }
                char value_name[MAX_NAME];
                strcpy(value_name, token.text);
                int existing_type_idx, existing_ordinal;
                for (int i = 0; i < et->value_count; i++) {
                    if (strcmp(et->value_names[i], value_name) == 0) {
                        compile_error(token.line, "Duplicate value '%s' in enumerated type '%s'", value_name, type_name);
                    }
                }
                if (find_enum_value(value_name, &existing_type_idx, &existing_ordinal) || find_const(value_name) != -1) {
                    compile_error(token.line, "'%s' is already declared", value_name);
                }
                if (et->value_count >= MAX_ENUM_VALUES) {
                    compile_error(token.line, "Too many values in enumerated type '%s' (limit is %d)", type_name, MAX_ENUM_VALUES);
                }
                strcpy(et->value_names[et->value_count], value_name);
                et->value_str_idx[et->value_count] = intern_string(value_name);
                et->value_count++;
                match(TOKEN_IDENTIFIER);
                if (token.type == TOKEN_COMMA) { match(TOKEN_COMMA); continue; }
                break;
            }
            match(TOKEN_RPAREN);
            match(TOKEN_SEMI);
            enum_type_count++;
            continue;
        }

        // A subrange type ('type TAge = 0..150;') - see the comment
        // above SubrangeTypeDef. Unambiguous the moment the bound starts
        // with a number/minus sign (a type alias/enum-type name can
        // never start with those); when it starts with an identifier,
        // it's only ambiguous with a type alias/enum-type reference if
        // that identifier ALSO happens to be an integer const's name -
        // which a type alias/enum-type reference could never validly be
        // anyway, so treating it as a subrange bound here is correct.
        int could_be_subrange = (token.type == TOKEN_NUMBER || token.type == TOKEN_MINUS);
        if (!could_be_subrange && token.type == TOKEN_IDENTIFIER) {
            int ci = find_const(token.text);
            could_be_subrange = (ci != -1 && const_defs[ci].type == TYPE_INTEGER);
        }
        if (could_be_subrange) {
            if (subrange_type_count >= MAX_SUBRANGE_TYPES) {
                compile_error(line, "Too many subrange type declarations (limit is %d)", MAX_SUBRANGE_TYPES);
            }
            int lower = parse_int_literal();
            match(TOKEN_DOTDOT);
            int upper = parse_int_literal();
            match(TOKEN_SEMI);
            if (upper < lower) {
                compile_error(line, "Invalid subrange bounds: upper (%d) must be >= lower (%d)", upper, lower);
            }
            SubrangeTypeDef *st = &subrange_types[subrange_type_count];
            strcpy(st->name, type_name);
            st->lower = lower;
            st->upper = upper;
            subrange_type_count++;
            continue;
        }

        if (token.type != TOKEN_RECORD) {
            // A plain type alias ('type TAge = integer;', or 'type TB =
            // TA;' where TA is itself an enum/subrange type - see the
            // comment above TypeAliasDef). parse_scalar_type() already
            // resolves a reference to an earlier alias by name too, so
            // aliases can chain ('type TAge = integer; TYears = TAge;').
            DataType aliased = parse_scalar_type();
            match(TOKEN_SEMI);
            if (scalar_type_is_subrange) {
                // Aliasing a subrange type produces another subrange
                // type under the new name, not a plain TypeAliasDef -
                // TypeAliasDef has no bounds fields of its own.
                if (subrange_type_count >= MAX_SUBRANGE_TYPES) {
                    compile_error(line, "Too many subrange type declarations (limit is %d)", MAX_SUBRANGE_TYPES);
                }
                SubrangeTypeDef *st = &subrange_types[subrange_type_count];
                strcpy(st->name, type_name);
                st->lower = scalar_type_subrange_lower;
                st->upper = scalar_type_subrange_upper;
                subrange_type_count++;
                continue;
            }
            if (type_alias_count >= MAX_TYPE_ALIASES) {
                compile_error(line, "Too many type declarations (limit is %d)", MAX_TYPE_ALIASES);
            }
            TypeAliasDef *a = &type_aliases[type_alias_count];
            strcpy(a->name, type_name);
            a->type = aliased;
            type_alias_count++;
            continue;
        }
        match(TOKEN_RECORD);

        if (record_type_count >= MAX_RECORD_TYPES) {
            compile_error(line, "Too many record types (limit is %d)", MAX_RECORD_TYPES);
        }
        RecordTypeDef *rt = &record_types[record_type_count];
        strcpy(rt->name, type_name);
        rt->field_count = 0;

        while (token.type == TOKEN_IDENTIFIER) {
            parse_record_field_group(rt, record_type_count, 0, 0, -1); // plain record - no visibility concept
            match(TOKEN_SEMI);
        }
        if (token.type == TOKEN_CASE) {
            // Variant part - see parse_record_variant_part()'s comment
            // for the flatten-not-overlap scoping decision.
            parse_record_variant_part(rt, record_type_count);
        }

        match(TOKEN_END);
        match(TOKEN_SEMI);
        record_type_count++;
    }
}

// Resolves every pointer type left pending (forward-referencing a
// record type by name - see parse_type_section()'s own TOKEN_CARET
// branch) - callers run this once their whole declaration part's
// repeated/interleaved 'const'/'type'/'var' sections are ALL done, not
// once per individual 'type' keyword block, so a self-referential pair
// split across two separate 'type' blocks (with a 'const'/'var' in
// between) still resolves correctly - see the plan for why this used to
// be inlined at the end of parse_type_section() itself, which broke
// exactly that case. Scans the ENTIRE pointer_types[] array (not just
// whatever the most recent parse_type_section() call added) and skips
// anything already resolved, so calling this once per independent
// declaration-part scope (main program; a unit's interface; that same
// unit's implementation, separately) is correct and sufficient.
static void resolve_pending_pointer_types(void) {
    for (int i = 0; i < pointer_type_count; i++) {
        PointerTypeDef *pt = &pointer_types[i];
        if (!pt->is_pending) continue;
        int record_idx = find_record_type(pt->pending_target_name);
        if (record_idx == -1) {
            compile_error(pt->pending_line, "Pointer type '%s' targets undeclared type '%s'", pt->name, pt->pending_target_name);
        }
        if (record_type_has_nested_field(record_idx)) {
            compile_error(pt->pending_line, "Pointer target record type '%s' has a nested-record field - pointers to a record with a nested-record field aren't supported yet (see docs/LANGUAGE.md)", pt->pending_target_name);
        }
        pt->target_is_record = 1;
        pt->target_record_type_idx = record_idx;
        pt->target_elem_size = record_types[record_idx].field_count;
        pt->is_pending = 0;
    }
}

// 'var name, name2, ... : type; ...'  -- a top-level 'var' section.
// Extracted out of parse_ast() (which used to inline this) so a unit's
// interface/implementation sections (see load_unit() below) can reuse
// it too - parse_const_section()/parse_type_section() were already
// standalone for the same reason.
static void parse_var_section(void) {
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
            if (token.type != TOKEN_LBRACKET) {
                // No '[lo..hi]' - a DYNAMIC array global (see
                // parse_dynarray_of()). Needs no dedicated storage helper
                // the way add_array_var() etc. are - it's just an
                // ordinary add_var() (a plain scalar slot: one int, a
                // heap pointer - see TYPE_DYNARRAY_BASE), exactly like a
                // pointer-typed global already is.
                DataType target_type = parse_dynarray_of();
                for (int i = 0; i < count; i++) {
                    add_var(temporary_names[i], target_type);
                }
                match(TOKEN_SEMI);
                continue;
            }
            match(TOKEN_LBRACKET);
            int lower[MAX_ARRAY_DIMS], upper[MAX_ARRAY_DIMS];
            int dims = parse_array_bounds(lower, upper);
            match(TOKEN_RBRACKET);
            match(TOKEN_OF);

            if (token.type == TOKEN_IDENTIFIER && find_record_type(token.text) != -1) {
                if (dims != 1) {
                    compile_error(token.line, "Arrays of records are only supported for 1D arrays right now (see docs/LANGUAGE.md)");
                }
                int elem_record_type_idx = find_record_type(token.text);
                match(TOKEN_IDENTIFIER);
                for (int i = 0; i < count; i++) {
                    int array_sym_idx = sym_count; // add_array_var_rec() is about to append here
                    add_array_var_rec(temporary_names[i], elem_record_type_idx, lower[0], upper[0]);
                    register_record_array(array_sym_idx, elem_record_type_idx);
                }
            } else {
                DataType elem_type = parse_scalar_type();
                int is_subrange = scalar_type_is_subrange;
                int subrange_lower = scalar_type_subrange_lower;
                int subrange_upper = scalar_type_subrange_upper;

                for (int i = 0; i < count; i++) {
                    if (dims == 1) {
                        add_array_var(temporary_names[i], elem_type, lower[0], upper[0]);
                    } else if (dims == 2) {
                        add_array_var_2d(temporary_names[i], elem_type, lower[0], upper[0], lower[1], upper[1]);
                    } else {
                        add_array_var_nd(temporary_names[i], elem_type, dims, lower, upper);
                    }
                    sym_table[sym_count - 1].is_subrange = is_subrange;
                    sym_table[sym_count - 1].subrange_lower = subrange_lower;
                    sym_table[sym_count - 1].subrange_upper = subrange_upper;
                }
            }
        } else if (token.type == TOKEN_TEXT_TYPE) {
            // A file variable - see TYPE_FILE in common.h for why
            // this is its own branch here (the ONLY place 'text' is
            // legal) rather than going through parse_scalar_type()
            // like every other type does. add_var() needs nothing
            // file-specific - TYPE_FILE is a plain scalar as far as
            // sym_table[] itself is concerned; all the real file
            // STATE lives in vm.c's vm_open_files[], indexed by this
            // same symbol index.
            match(TOKEN_TEXT_TYPE);
            for (int i = 0; i < count; i++) {
                add_var(temporary_names[i], TYPE_FILE);
            }
        } else if (token.type == TOKEN_FILE_TYPE) {
            // 'file of TRecord'/'file of integer' etc. - a typed
            // (binary) file. Same "own branch here, own global-only
            // restriction" reasoning as TOKEN_TEXT_TYPE just above -
            // this is the ONLY place 'file of ...' is legal. Peeks for
            // 'of' BEFORE matching it - bare 'file' (no 'of') is an
            // UNTYPED file instead (see below), mirroring exactly how
            // 'array'/'array of T' (dynamic arrays) are disambiguated
            // by peeking for '[' before committing to either path.
            match(TOKEN_FILE_TYPE);
            if (token.type != TOKEN_OF) {
                // 'file;' (no 'of Type') - an untyped file: no fixed
                // element type or on-disk record size, read/written in
                // caller-specified chunks via BlockRead/BlockWrite (see
                // parse_block_read()/parse_block_write() below). No side
                // table needed (unlike TypedFileVarDef) - there's no
                // element type or byte size to remember at all. Falls
                // through to the shared trailing match(TOKEN_SEMI) below,
                // same as the TOKEN_TEXT_TYPE branch above.
                for (int i = 0; i < count; i++) {
                    add_var(temporary_names[i], TYPE_UNTYPED_FILE);
                }
            } else {
                match(TOKEN_OF);
                int type_line = token.line;
                int rt_idx = (token.type == TOKEN_IDENTIFIER) ? find_record_type(token.text) : -1;
                int is_record = (rt_idx != -1);
                DataType scalar_type = TYPE_UNKNOWN;
                if (is_record) {
                    match(TOKEN_IDENTIFIER);
                    if (!record_type_is_typed_file_safe(rt_idx)) {
                        compile_error(type_line, "Record type '%s' can't be used as a typed file's element type - no array, string, char, pointer, or procedural-typed fields allowed (their raw storage isn't meaningful once written to a file)", record_types[rt_idx].name);
                    }
                } else {
                    // Reuses parse_scalar_type() directly - it already
                    // resolves every non-record scalar/enum/subrange/alias/
                    // pointer/procedural type name (and rejects 'text'/
                    // 'file' with its own clear error), exactly the same
                    // resolution a 'file of X' element type needs.
                    scalar_type = parse_scalar_type();
                    if (!is_typed_file_safe_scalar(scalar_type)) {
                        compile_error(type_line, "This type can't be used as a typed file's element type - only integer, real, boolean, an enumerated type, a subrange, or a set are allowed");
                    }
                }
                int leaf_count = is_record ? record_type_leaf_count(rt_idx) : 1;
                TokenType scalar_disk_width = is_record ? 0 : scalar_type_disk_width;
                int byte_size = is_record ? record_type_byte_size(rt_idx)
                    : (scalar_disk_width == TOKEN_BYTE || scalar_disk_width == TOKEN_SHORTINT) ? 1
                    : (scalar_disk_width == TOKEN_WORD) ? 2
                    : (int)sizeof(int);
                for (int i = 0; i < count; i++) {
                    add_var(temporary_names[i], TYPE_TYPED_FILE);
                    int sym_idx = sym_count - 1;
                    if (typed_file_var_count >= MAX_TYPED_FILE_VARS) {
                        compile_error(type_line, "Too many typed file variables (limit is %d)", MAX_TYPED_FILE_VARS);
                    }
                    TypedFileVarDef *tf = &typed_file_vars[typed_file_var_count++];
                    tf->sym_idx = sym_idx;
                    tf->is_record = is_record;
                    tf->record_type_idx = rt_idx;
                    tf->scalar_type = scalar_type;
                    tf->leaf_count = leaf_count;
                    tf->disk_width = scalar_disk_width;
                    tf->byte_size = byte_size;
                }
            }
        } else {
            DataType target_type = parse_scalar_type();
            int is_subrange = scalar_type_is_subrange;
            int subrange_lower = scalar_type_subrange_lower;
            int subrange_upper = scalar_type_subrange_upper;

            for (int i = 0; i < count; i++) {
                add_var(temporary_names[i], target_type);
                sym_table[sym_count - 1].is_subrange = is_subrange;
                sym_table[sym_count - 1].subrange_lower = subrange_lower;
                sym_table[sym_count - 1].subrange_upper = subrange_upper;
            }
        }
        match(TOKEN_SEMI);
    }
}

// Units 'uses'-d so far this compile (fully merged into the global
// tables already) and units currently mid-load (a stack, for circular-
// dependency detection) - see load_unit(). Both reset to 0 at the top of
// parse_ast(), alongside every other global counter reset there, or
// state would leak across compiles in the same process exactly like
// test_recovery guards against for everything else.
static char loaded_units[MAX_UNITS][MAX_NAME];
static int loaded_unit_count = 0;
static char loading_units[MAX_UNITS][MAX_NAME];
static int loading_unit_count = 0;

// Each loaded unit's own 'initialization'/'finalization' statement chain
// (NULL if the unit has none), indexed in lockstep with loaded_units[] -
// written by load_unit() right before it appends to loaded_units[], so
// every slot below loaded_unit_count is always fresh from THIS compile;
// no separate reset needed beyond loaded_unit_count itself resetting to
// 0 in parse_ast(). See parse_ast()'s end-of-parse splice for how these
// become part of the main program's own statement chain.
static ASTNode *loaded_unit_init[MAX_UNITS];
static ASTNode *loaded_unit_final[MAX_UNITS];

// Reads an entire unit source file into a malloc'd, NUL-terminated
// buffer. Mirrors pascalc.c's own read_file() (fopen/fseek/fread), but
// reports a failure via compile_error() (recoverable, points at the
// 'uses' clause that named this unit) rather than perror()/a raw abort -
// this file never calls exit()/fatal_abort() without going through
// compile_error() first, and 'uses'-ing a nonexistent unit is an
// ordinary, expected compile error, not a host-process-ending one.
static char *read_unit_file(const char *path, int use_line, const char *unit_name) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        compile_error(use_line, "Cannot find unit '%s' (expected file '%s')", unit_name, path);
    }
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buffer = malloc(length + 1);
    if (!buffer) {
        fclose(f);
        compile_error(use_line, "Allocation failure reading unit '%s'", unit_name);
    }
    size_t read_count = fread(buffer, 1, length, f);
    buffer[read_count] = '\0';
    fclose(f);
    return buffer;
}

// A unit is searched for in the same directory as the file containing
// the 'uses' clause that named it (current_filename, at the point this
// is called - before load_unit() below switches it to the unit's own
// path) - no separate include/search-path concept, matching this
// project's habit of not building for a requirement it hasn't hit yet.
static void build_unit_path(char *out, size_t outsz, const char *unit_name) {
    const char *slash = strrchr(current_filename, '/');
    if (slash) {
        int dir_len = (int)(slash - current_filename);
        snprintf(out, outsz, "%.*s/%s.pas", dir_len, current_filename, unit_name);
    } else {
        snprintf(out, outsz, "%s.pas", unit_name);
    }
}

static void load_unit(const char *name, int use_line);

// 'uses UnitName, UnitName2, ...;' - a comma-separated list of unit
// names, each pulled in via load_unit(). Valid right after the main
// program's own header, or right after a unit's own 'interface' keyword
// (see load_unit()) - not inside an 'implementation' section, one
// dependency list per file, simpler than Turbo Pascal's two-place
// 'uses'.
static void parse_uses_clause(void) {
    match(TOKEN_USES);
    while (1) {
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "Expected a unit name after 'uses'");
        }
        char name[MAX_NAME];
        int line = token.line;
        strcpy(name, token.text);
        match(TOKEN_IDENTIFIER);
        load_unit(name, line);
        if (token.type != TOKEN_COMMA) break;
        match(TOKEN_COMMA);
    }
    match(TOKEN_SEMI);
}

// Loads and merges 'name.pas' (a 'unit name; interface ... implementation
// ... end.' file) into the current compile - a no-op if already fully
// loaded (a diamond dependency: two units both 'uses'-ing a shared third
// one), a circular-dependency compile error if 'name' is already mid-
// load higher up the same 'uses' chain.
//
// Not separate compilation: everything the unit declares is parsed
// straight into the SAME global tables (sym_table[], proc_table[],
// record_types[], pointer_types[], etc.) the main program's own
// declarations use, via a nested, save-and-restore re-entry of the
// lexer/parser on the unit's own source - by the time codegen runs, a
// 'uses'-based program is indistinguishable from one big file. See
// docs/LANGUAGE.md#units for the full syntax and the (documented, not
// silently dropped) gaps this leaves: no interface/implementation
// visibility enforcement yet (same gap as classes' private/public), and
// same-directory-only unit resolution.
//
// Interface procedure/function headers need no 'forward;' keyword - see
// subroutine_declaration()'s header_only parameter. A forward-declared
// interface proc never completed anywhere in the implementation is
// already caught for free by parse_ast()'s own end-of-parse sweep over
// the whole (by-then fully merged) proc_table[], run once after
// everything - main program included - has been parsed.
static void load_unit(const char *name, int use_line) {
    for (int i = 0; i < loaded_unit_count; i++) {
        if (strcmp(loaded_units[i], name) == 0) return; // already merged
    }
    for (int i = 0; i < loading_unit_count; i++) {
        if (strcmp(loading_units[i], name) == 0) {
            compile_error(use_line, "Circular unit dependency: '%s' (directly or indirectly) uses itself", name);
        }
    }
    if (loading_unit_count >= MAX_UNITS || loaded_unit_count >= MAX_UNITS) {
        compile_error(use_line, "Too many units in this compile (limit is %d)", MAX_UNITS);
    }

    char path[MAX_UNIT_PATH];
    build_unit_path(path, sizeof(path), name);
    char *unit_source = read_unit_file(path, use_line, name);

    strcpy(loading_units[loading_unit_count++], name);

    // owned_path lives on this call's own stack frame for as long as
    // this function is on the call stack - including every nested
    // load_unit() this unit's own 'uses' clause triggers below - and is
    // never referenced again once current_filename is restored at the
    // end, so (unlike unit_source) it doesn't need a heap allocation.
    char owned_path[MAX_UNIT_PATH];
    strcpy(owned_path, path);
    const char *saved_filename = current_filename;
    LexerPos saved_pos = lexer_save_pos();
    Token saved_token = token;
    char saved_unit_name[MAX_NAME];
    strcpy(saved_unit_name, current_unit_name);
    int saved_section_is_implementation = current_section_is_implementation;

    current_filename = owned_path;
    init_lexer(unit_source);

    match(TOKEN_UNIT);
    if (token.type != TOKEN_IDENTIFIER || strcmp(token.text, name) != 0) {
        compile_error(token.line, "File '%s' must declare 'unit %s;' to match 'uses %s' (found '%s')", path, name, name, token.text);
    }
    match(TOKEN_IDENTIFIER);
    match(TOKEN_SEMI);
    strcpy(current_unit_name, name);
    current_section_is_implementation = 0;
    match(TOKEN_INTERFACE);

    if (token.type == TOKEN_USES) {
        parse_uses_clause();
    }
    // 'const'/'type'/'var' repeat and interleave freely (Delphi-style,
    // not standard Pascal's fixed single-occurrence order) - whatever
    // was declared textually before a given point is visible to it,
    // regardless of which of the three keywords introduced it. See
    // resolve_pending_pointer_types()'s own comment for why it runs
    // once here, after the whole loop, rather than inside
    // parse_type_section() itself.
    while (token.type == TOKEN_CONST || token.type == TOKEN_TYPE || token.type == TOKEN_VAR) {
        if (token.type == TOKEN_CONST) parse_const_section();
        else if (token.type == TOKEN_TYPE) parse_type_section();
        else parse_var_section();
    }
    resolve_pending_pointer_types();
    while (token.type == TOKEN_PROCEDURE || token.type == TOKEN_FUNCTION || token.type == TOKEN_DESTRUCTOR) {
        subroutine_declaration(token.type == TOKEN_FUNCTION, 1, token.type == TOKEN_DESTRUCTOR); // header_only
    }

    match(TOKEN_IMPLEMENTATION);
    current_section_is_implementation = 1;
    // A separate, independent interleaving scope from the interface
    // section above - a pending pointer declared in the interface must
    // NOT resolve against a type only declared here, and vice versa
    // (matches the existing is_unit_private visibility split between
    // the two sections).
    while (token.type == TOKEN_CONST || token.type == TOKEN_TYPE || token.type == TOKEN_VAR) {
        if (token.type == TOKEN_CONST) parse_const_section();
        else if (token.type == TOKEN_TYPE) parse_type_section();
        else parse_var_section();
    }
    resolve_pending_pointer_types();
    while (token.type == TOKEN_PROCEDURE || token.type == TOKEN_FUNCTION || token.type == TOKEN_DESTRUCTOR) {
        subroutine_declaration(token.type == TOKEN_FUNCTION, 0, token.type == TOKEN_DESTRUCTOR);
    }

    // Both sections are optional and independent - a unit may have
    // 'initialization' only, 'finalization' only, both, or neither.
    // statement_list() (used everywhere else only inside a begin...end)
    // works unmodified here too: real Pascal's initialization/
    // finalization sections are themselves un-bracketed statement lists,
    // and statement_list() already stops on its own at the first
    // non-statement-start token (here, TOKEN_FINALIZATION or TOKEN_END).
    ASTNode *unit_init = NULL;
    if (token.type == TOKEN_INITIALIZATION) {
        match(TOKEN_INITIALIZATION);
        unit_init = statement_list();
    }
    ASTNode *unit_final = NULL;
    if (token.type == TOKEN_FINALIZATION) {
        match(TOKEN_FINALIZATION);
        unit_final = statement_list();
    }

    match(TOKEN_END);
    match(TOKEN_PERIOD);

    free(unit_source);
    loading_unit_count--;
    loaded_unit_init[loaded_unit_count] = unit_init;
    loaded_unit_final[loaded_unit_count] = unit_final;
    strcpy(loaded_units[loaded_unit_count++], name);

    current_filename = saved_filename;
    lexer_restore_pos(saved_pos);
    token = saved_token;
    strcpy(current_unit_name, saved_unit_name);
    current_section_is_implementation = saved_section_is_implementation;
}

// Builds the vtable-init statement chain (see NODE_VTABLE_INIT_ENTRY in
// common.h): one entry per method of every class that actually has a
// body, populating vm_vtables[] before any user code runs. Called once,
// near the very end of parse_ast(), by which point every class's method
// headers AND every procedure/function body (including every class
// method's own) has already been parsed.
//
// A method header declared but never given a body ANYWHERE is a known,
// pre-existing, intentionally lazy gap (see test_class_basic.pas/
// test_class_samename_methods.pas - a class declaration is valid on its
// own even if some method is never implemented, as long as nothing ever
// calls it) - resolve_heap_deref_step()'s own find_proc() check already
// rejects any call site that WOULD need such a method, so a bodyless
// header simply gets no vtable slot populated here: its vm_vtables[]
// entry stays at run_vm()'s -1 reset value, never consulted, because no
// program that compiles can ever reach an OP_LOAD_VTABLE_SLOT for it.
//
// An ABSTRACT method (ProcParamHeader.is_abstract) is a DIFFERENT case
// that looks similar but isn't: register_abstract_method_signature()
// gives it a phantom proc_table[] entry (no real body, but a real,
// resolvable mangled_name), so find_proc() below DOES succeed for it,
// and a vtable slot IS populated here - pointing at the phantom's tiny
// dead OP_RET stub. That slot is still never reached, but for a
// different reason than the lazy-gap case above: the declaring class
// can never be new()'d while any of its methods stays is_abstract (see
// class_first_unresolved_abstract_method()), and any variable whose
// STATIC type is that class must, if non-nil, hold a RUNTIME tag
// belonging to some concrete descendant - dispatch always resolves via
// the runtime tag's own vtable row, never the abstract-declaring
// class's own row.
static ASTNode *build_vtable_init_chain(void) {
    ASTNode *head = NULL;
    ASTNode *tail = NULL;
    for (int c = 0; c < pointer_type_count; c++) {
        PointerTypeDef *pt = &pointer_types[c];
        if (!pt->is_class) continue;
        for (int slot = 0; slot < pt->method_count; slot++) {
            ProcParamHeader *h = &pt->methods[slot];
            if (h->is_class_method) continue; // never virtually dispatched -
                                               // called via plain NODE_CALL,
                                               // no vtable slot needed; skip
                                               // so it never wastes a scarce
                                               // MAX_CLASS_METHODS-bounded
                                               // vm_vtables[] row.
            int mangled_idx = find_proc(h->mangled_name);
            if (mangled_idx == -1) continue; // no body anywhere - see comment above
            ASTNode *ref = create_node(NODE_PROC_REF);
            ref->data.var_idx = mangled_idx;
            ref->expression_type = TYPE_INTEGER; // meaningless beyond "one int" - see NODE_PROC_REF's own comment
            ASTNode *entry = create_node(NODE_VTABLE_INIT_ENTRY);
            entry->left = ref;
            entry->data.num_value = c * MAX_CLASS_METHODS + slot;
            if (!head) head = entry; else tail->next = entry;
            tail = entry;
        }
    }
    return head;
}

// Companion to build_vtable_init_chain() above - one entry per class
// (not per method), storing its immediate parent's own class_id (or -1
// for none) into vm_class_parent[] at program startup, for OP_IS_
// INSTANCE's runtime ancestor walk (is/as). No backpatching needed here
// - unlike a method's entry_address, a parent's class_id is already a
// fully known compile-time integer.
static ASTNode *build_class_parent_init_chain(void) {
    ASTNode *head = NULL;
    ASTNode *tail = NULL;
    for (int c = 0; c < pointer_type_count; c++) {
        PointerTypeDef *pt = &pointer_types[c];
        if (!pt->is_class) continue;
        ASTNode *parent_lit = create_node(NODE_NUMBER);
        parent_lit->data.num_value = pt->parent_class_ptr_idx; // -1 if none
        parent_lit->expression_type = TYPE_INTEGER;
        ASTNode *entry = create_node(NODE_CLASS_PARENT_INIT_ENTRY);
        entry->left = parent_lit;
        entry->data.num_value = c;
        if (!head) head = entry; else tail->next = entry;
        tail = entry;
    }
    return head;
}

ASTNode *parse_ast(const char *source, const char *filename) {
    current_filename = filename ? filename : "<source>";
    sym_count = 0;
    code_idx = 0;
    string_count = 0;
    array_mem_count = 0;
    loop_depth = 0;
    proc_count = 0;
    nesting_depth = -1; // must be reset before current_local_count/
                        // local_record_var_count below - both are macros
                        // aliasing scope_locals[nesting_depth]/
                        // scope_record_vars[nesting_depth], meaningless
                        // (and out of bounds) until this is set first.
                        // Each level's own count is zeroed when
                        // subroutine_declaration() actually enters it,
                        // not needed here.
    current_proc_idx = -1;
    current_function_idx = -1;
    current_class_ptr_idx = -1;
    record_type_count = 0;
    record_var_count = 0;
    record_array_count = 0;
    typed_file_var_count = 0;
    pointer_type_count = 0;
    proc_type_count = 0;
    dynarray_type_count = 0;
    const_def_count = 0;
    typed_const_init_head = NULL;
    typed_const_init_tail = NULL;
    type_alias_count = 0;
    enum_type_count = 0;
    subrange_type_count = 0;
    with_depth = 0;
    declared_label_count = 0;
    loaded_unit_count = 0;
    loading_unit_count = 0;
    current_unit_name[0] = '\0';
    current_section_is_implementation = 0;
    lexer_reset_defines();
    init_lexer(source);
    match(TOKEN_PROGRAM);
    match(TOKEN_IDENTIFIER);
    if (token.type == TOKEN_LPAREN) {
        // Standard Pascal's optional program-parameter list (traditionally
        // file/device names like input/output). No file-parameter binding
        // exists in this VM, so the list is accepted and discarded - pure
        // syntax, any identifier accepted, nothing stored or validated.
        match(TOKEN_LPAREN);
        match(TOKEN_IDENTIFIER);
        while (token.type == TOKEN_COMMA) {
            match(TOKEN_COMMA);
            match(TOKEN_IDENTIFIER);
        }
        match(TOKEN_RPAREN);
    }
    match(TOKEN_SEMI);

    if (token.type == TOKEN_USES) {
        parse_uses_clause();
    }

    if (token.type == TOKEN_LABEL) {
        parse_label_section();
    }

    // 'const'/'type'/'var' repeat and interleave freely (Delphi-style) -
    // see the matching comment in load_unit() for the full rationale and
    // why resolve_pending_pointer_types() runs once here, after the
    // whole loop.
    while (token.type == TOKEN_CONST || token.type == TOKEN_TYPE || token.type == TOKEN_VAR) {
        if (token.type == TOKEN_CONST) parse_const_section();
        else if (token.type == TOKEN_TYPE) parse_type_section();
        else parse_var_section();
    }
    resolve_pending_pointer_types();

    // The main program's own label declarations (if any) must survive
    // parsing every procedure/function below - each one resets and
    // reuses this same static declared_labels table for its own,
    // independent label namespace (see subroutine_declaration()).
    // Stashed here, restored just before parsing the main body itself.
    DeclaredLabel main_labels[MAX_DECLARED_LABELS];
    int main_label_count = declared_label_count;
    memcpy(main_labels, declared_labels, sizeof(DeclaredLabel) * declared_label_count);

    while (token.type == TOKEN_PROCEDURE || token.type == TOKEN_FUNCTION || token.type == TOKEN_DESTRUCTOR) {
        subroutine_declaration(token.type == TOKEN_FUNCTION, 0, token.type == TOKEN_DESTRUCTOR);
    }

    for (int i = 0; i < proc_count; i++) {
        if (proc_table[i].is_forward) {
            compile_error(token.line, "%s '%s' was forward-declared but never defined",
                           proc_table[i].is_function ? "Function" : "Procedure", proc_table[i].name);
        }
    }

    memcpy(declared_labels, main_labels, sizeof(DeclaredLabel) * main_label_count);
    declared_label_count = main_label_count;

    ASTNode *vtable_init = build_vtable_init_chain();
    ASTNode *class_parent_init = build_class_parent_init_chain();
    ASTNode *root = compound_statement();
    check_all_labels_defined();
    match(TOKEN_PERIOD);
    if (vtable_init) {
        // Prepend to the main body's own statement chain, guaranteeing
        // it runs before any user code - generate_program() (codegen.c)
        // emits every procedure/method BEFORE the main body, so every
        // entry_address this chain's NODE_PROC_REFs need is already
        // resolved (or backpatchable) regardless.
        ASTNode *tail = vtable_init;
        while (tail->next) tail = tail->next;
        tail->next = root->left;
        root->left = vtable_init;
    }
    if (class_parent_init) {
        // Independent second prepend - order relative to vtable_init
        // doesn't matter, they populate disjoint tables
        // (vm_class_parent[] vs. vm_vtables[]).
        ASTNode *tail = class_parent_init;
        while (tail->next) tail = tail->next;
        tail->next = root->left;
        root->left = class_parent_init;
    }

    // Unit initialization sections run next, in unit-load (dependency)
    // order - prepend in REVERSE load order so that after every prepend,
    // the first-loaded unit's init chain ends up first, right after the
    // vtable/class-parent bookkeeping above (a unit's own initialization
    // code may construct objects or call virtual methods, which needs
    // vtables already populated).
    for (int i = loaded_unit_count - 1; i >= 0; i--) {
        if (!loaded_unit_init[i]) continue;
        ASTNode *tail = loaded_unit_init[i];
        while (tail->next) tail = tail->next;
        tail->next = root->left;
        root->left = loaded_unit_init[i];
    }

    if (typed_const_init_head) {
        // Prepended LAST among this whole block's prepends, so typed
        // constants - the closest thing this compiler has to a static
        // data segment - are established before anything else runs,
        // including unit initialization sections (which may legitimately
        // want to read a typed constant). Same "walk to tail, prepend"
        // pattern as vtable_init/class_parent_init/unit-init above.
        ASTNode *tail = typed_const_init_tail;
        tail->next = root->left;
        root->left = typed_const_init_head;
    }

    // Unit finalization sections run last, after the main program's own
    // body, in REVERSE unit-load order (last-loaded unit finalizes
    // first) - appended to the tail of the whole statement chain built
    // so far, right before pascalc.c's own trailing emit_halt().
    {
        ASTNode *tail = root->left;
        if (tail) { while (tail->next) tail = tail->next; }
        for (int i = loaded_unit_count - 1; i >= 0; i--) {
            if (!loaded_unit_final[i]) continue;
            if (tail) tail->next = loaded_unit_final[i];
            else root->left = loaded_unit_final[i];
            tail = loaded_unit_final[i];
            while (tail->next) tail = tail->next;
        }
    }

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
// Uninitialized-local-variable warning pass, run once per procedure/
// function right after its body is fully parsed (see the call site in
// subroutine_declaration() below) - this is the only point current_locals[]
// (a parser-only, per-procedure scratch table) still holds this
// procedure's own local metadata (name, is_array/is_static/is_var_param),
// which is why this lives here rather than as a separate pass over
// proc_table[] later: nothing after parsing retains that per-local
// detail. Non-fatal: prints to stderr and returns, never calls
// fatal_abort() - this is a heuristic diagnostic, not a hard guarantee.
//
// Deliberately FLOW-INSENSITIVE: this only asks "is this local ever
// read, and is it ever assigned, ANYWHERE in this body" - it does NOT
// try to determine whether every code path assigns a variable before
// every read of it (e.g. 'if cond then x := 1; writeln(x);' is not
// flagged, even though x might be unassigned when cond is false). A
// precise, branch-aware analysis would need to correctly model every
// statement kind's control flow (if/while/for/repeat/case merge
// points, break/continue, and this compiler's unrestricted goto) -
// real complexity with real risk of false positives on totally valid
// code. This simpler pass only ever under-warns (misses some real
// bugs), never over-warns on correct code - the right tradeoff for a
// warning nobody's forced to act on. See docs/LANGUAGE.md for the exact
// documented scope.
//
// Also deliberately scoped to PLAIN scalar locals of THIS ONE body:
//  - Not parameters (always initialized by the caller) or 'var'
//    parameters (a valid reference regardless of what it points to).
//  - Not 'static' locals - they persist across calls, so "never
//    assigned in THIS body" doesn't mean "never assigned", and relying
//    on the implicit zero from a prior (or the very first) call is a
//    common, intentional reason to reach for 'static' in the first
//    place.
//  - Not arrays - this pass doesn't track per-element initialization.
//  - Not global variables AT ALL, including the main program's own
//    top-level 'var' section - correctly telling "already initialized
//    by an earlier procedure call" from "genuinely never initialized"
//    needs whole-program interprocedural analysis this pass doesn't
//    attempt; checking only what a single call graph edge could prove
//    isn't worth the false-positive risk on the (very common) pattern
//    of a helper procedure initializing a global before main uses it.
static void scan_local_usage(ASTNode *node, char *read_flag, char *assigned_flag) {
    if (!node) return;

    // A nonzero ->op on any of these node types means "this slot index is
    // in an ENCLOSING procedure's own scope, not this body's own" (see
    // the levels_up-via-->op comment in common.h's Opcode section) - this
    // whole pass is scoped to ONE procedure's own locals (see the comment
    // above), so such a node must be skipped here: node->data.var_idx
    // would otherwise coincidentally alias one of THIS procedure's own
    // slot indices and corrupt its read/assigned tracking. NODE_LOCAL_FOR
    // and NODE_LOCAL_READLN can never have a nonzero ->op here (their own
    // ->op is TOKEN_TO/TOKEN_DOWNTO/TOKEN_READ/TOKEN_READLN instead - see
    // their own parse sites, which restrict both to this procedure's own
    // locals for exactly this reason) so they need no such check.
    int levels_up = (node->type == NODE_LOCAL_VAR || node->type == NODE_LOCAL_ASSIGN ||
                      node->type == NODE_LOCAL_VAR_REF || node->type == NODE_VAR_PARAM_ASSIGN) ? (int)node->op : 0;

    if (node->type == NODE_LOCAL_VAR) {
        if (levels_up == 0) read_flag[node->data.var_idx] = 1;
    } else if (node->type == NODE_LOCAL_ASSIGN || node->type == NODE_LOCAL_FOR ||
               node->type == NODE_LOCAL_READLN || node->type == NODE_LOCAL_VAR_REF ||
               node->type == NODE_VAR_PARAM_ASSIGN) {
        // NODE_LOCAL_VAR_REF (passing this local by reference to another
        // procedure as a 'var' argument) is conservatively treated as an
        // assignment too - the callee might set it through that
        // reference, and this pass would rather miss a real bug than
        // wrongly warn about a value a callee legitimately provides.
        // NODE_VAR_PARAM_ASSIGN (assigning to THIS procedure's own 'var'/
        // 'const'/'out' parameter, e.g. an 'out' parameter's own body
        // writing it) only matters here for is_out_param slots - see
        // check_uninitialized_locals()'s own new loop below; harmless to
        // set assigned_flag for a plain 'var' slot too, since that loop
        // never reads it there.
        if (levels_up == 0) assigned_flag[node->data.var_idx] = 1;
    }

    scan_local_usage(node->left, read_flag, assigned_flag);
    scan_local_usage(node->right, read_flag, assigned_flag);
    scan_local_usage(node->next, read_flag, assigned_flag);
    scan_local_usage(node->extra, read_flag, assigned_flag);
}

static void check_uninitialized_locals(int proc_idx, ASTNode *body, int decl_line) {
    char read_flag[MAX_LOCALS] = {0};
    char assigned_flag[MAX_LOCALS] = {0};
    scan_local_usage(body, read_flag, assigned_flag);

    int param_slot_count = proc_table[proc_idx].param_slot_count;
    int is_function = proc_table[proc_idx].is_function;
    int local_end = current_local_count - (is_function ? 1 : 0); // exclude the hidden return_slot - checked separately below, never via current_locals[] (its entry there is stale/unpopulated - see the return_slot comment in subroutine_declaration())

    for (int i = param_slot_count; i < local_end; i++) {
        if (current_locals[i].is_array || current_locals[i].is_array_ref ||
            current_locals[i].is_static || current_locals[i].is_var_param) {
            continue;
        }
        if (read_flag[i] && !assigned_flag[i]) {
            fprintf(stderr, "%s:%d: Warning: local variable '%s' in %s '%s' is read but never assigned a value\n",
                    current_filename, decl_line, current_locals[i].name,
                    is_function ? "function" : "procedure", proc_table[proc_idx].name);
        }
    }

    if (is_function && !assigned_flag[proc_table[proc_idx].return_slot]) {
        fprintf(stderr, "%s:%d: Warning: function '%s' never assigns a value to its own name - it will always return an undefined value\n",
                current_filename, decl_line, proc_table[proc_idx].name);
    }

    // 'out' parameters: the one case where a parameter itself IS checked
    // by this pass (every other parameter kind is always skipped above,
    // by construction, since the loop starts at param_slot_count) - an
    // 'out' parameter promises the caller a value back, so never
    // assigning it anywhere in the body is worth flagging, mirroring the
    // function-return-value check just above. Deliberately does NOT also
    // check read_flag here (unlike the loop above) - reading an 'out'
    // parameter before assigning it is legal, just unusual, not worth a
    // dedicated warning.
    for (int i = 0; i < param_slot_count; i++) {
        if (current_locals[i].is_out_param && !assigned_flag[i]) {
            fprintf(stderr, "%s:%d: Warning: 'out' parameter '%s' is never assigned a value in %s '%s'\n",
                    current_filename, decl_line, current_locals[i].name,
                    is_function ? "function" : "procedure", proc_table[proc_idx].name);
        }
    }
}

// Registers one SCALAR parameter (self, or one of a method's own
// already-header-declared params - see parse_class_method_body() below)
// as both a local (add_local()/add_local_var_param()) and a
// proc_table[proc_idx] entry at 'slot'. A method's own parameters are
// guaranteed scalar (parse_proc_param_header() already enforces this
// when the header itself is parsed - see ProcParamHeader), so this
// never needs the array/record/procedural-parameter branches
// subroutine_declaration()'s own parameter loop has to handle.
static void register_class_method_param(int proc_idx, int slot, const char *name, DataType type, int is_var, int is_const, int is_out, int has_default, DataType default_type, int default_value) {
    if (is_var) {
        add_local_var_param(name, type, 0, 0, 0, is_const, is_out);
    } else {
        add_local(name, type);
    }
    proc_table[proc_idx].param_types[slot] = type;
    strcpy(proc_table[proc_idx].param_names[slot], name);
    proc_table[proc_idx].param_is_array_ref[slot] = 0;
    proc_table[proc_idx].param_is_2d[slot] = 0;
    proc_table[proc_idx].param_is_nd[slot] = 0;
    proc_table[proc_idx].param_nd_dims[slot] = 0;
    proc_table[proc_idx].param_is_subrange[slot] = 0;
    proc_table[proc_idx].param_subrange_lower[slot] = 0;
    proc_table[proc_idx].param_subrange_upper[slot] = 0;
    proc_table[proc_idx].param_is_record[slot] = 0;
    proc_table[proc_idx].param_record_type_idx[slot] = 0;
    proc_table[proc_idx].param_record_field_count[slot] = 0;
    proc_table[proc_idx].param_is_var[slot] = is_var;
    proc_table[proc_idx].param_is_const[slot] = is_const;
    proc_table[proc_idx].param_is_out[slot] = is_out;
    // Explicitly written every call, even when has_default is 0 - like
    // every other field above, proc_table[] only resets param_count
    // between compiles in a long-lived host process (see add_proc()'s
    // comment), so leaving these untouched on the "no default" path
    // would let a stale param_has_default/param_default_* value leak in
    // from an unrelated procedure that previously occupied this same
    // slot in an earlier compile.
    proc_table[proc_idx].param_has_default[slot] = has_default;
    proc_table[proc_idx].param_default_type[slot] = default_type;
    proc_table[proc_idx].param_default_value[slot] = default_value;
    proc_table[proc_idx].param_is_proc[slot] = 0;
    proc_table[proc_idx].param_proc_is_function[slot] = 0;
    proc_table[proc_idx].param_proc_return_type[slot] = TYPE_UNKNOWN;
    proc_table[proc_idx].param_proc_param_count[slot] = 0;
}

// Registers a PHANTOM proc_table[] entry for an abstract method - just
// enough of the call-site-visible signature (param types/count/var-ness,
// is_function, return_type, unmangled_name) for parse_class_method_call_
// arguments()/the ~8 existing 'doesn't have a body yet' sites to treat it
// exactly like any other resolvable method, without ever giving it a
// real body. Deliberately does NOT call register_class_method_param()
// (which calls add_local()/add_local_var_param(), mutating the AMBIENT
// current_locals[]/current_local_count meant for whichever procedure
// body is CURRENTLY being parsed) - this runs at class-HEADER-parse
// time, not inside any body-parse, so touching that ambient state would
// corrupt an unrelated context. proc_table[proc_idx].body is left NULL
// forever (add_proc()'s own default) - generate_code(NULL) is already a
// confirmed no-op, so this compiles to a tiny dead OP_RET stub, never
// reached (see class_first_unresolved_abstract_method() for why: the
// declaring class can never be new()'d while this entry's is_abstract
// stays 1, and dispatch always resolves via the RUNTIME tag's own
// vtable row, never this declaring class's). is_forward is also left at
// its default 0 - never set to 1 - so the end-of-compile "forward-
// declared but never defined" sweep correctly, silently ignores it
// forever.
static int register_abstract_method_signature(ProcParamHeader *h, int class_ptr_idx) {
    int proc_idx = add_proc(h->mangled_name);
    strcpy(proc_table[proc_idx].unmangled_name, h->name);
    proc_table[proc_idx].is_function = h->is_function;
    proc_table[proc_idx].return_type = h->return_type;
    proc_table[proc_idx].return_is_subrange = 0;
    proc_table[proc_idx].return_subrange_lower = 0;
    proc_table[proc_idx].return_subrange_upper = 0;
    // Defensive zeroing, not "never consulted": generate_program()
    // (codegen.c) unconditionally emits a code block for EVERY
    // proc_table[] entry, phantom or real - a NULL body makes that
    // block a bare OP_RET (generate_block(NULL) is a no-op), but
    // param_slot_count/local_count/return_slot still feed that dead
    // block's own OP_ENTER/param-store emission, and proc_table[] is a
    // fixed global array where only proc_count resets between compiles
    // in a long-lived host process (see test_recovery) - a stale value
    // from an EARLIER compile's higher-indexed entry could otherwise
    // leak into this freshly-claimed slot.
    proc_table[proc_idx].param_slot_count = 0;
    proc_table[proc_idx].local_count = 0;
    proc_table[proc_idx].return_slot = 0;
    // Self at slot 0 - mirrors register_class_method_param()'s own real
    // registration for defensiveness, though nothing currently reads
    // it (self is passed as NODE_VIRTUAL_CALL's own ->left, never
    // validated against param_types[0]; NODE_VIRTUAL_CALL has no
    // type_checker.c case at all).
    proc_table[proc_idx].param_types[0] = (DataType)(TYPE_POINTER_BASE + class_ptr_idx);
    strcpy(proc_table[proc_idx].param_names[0], "self");
    proc_table[proc_idx].param_is_var[0] = 0;
    proc_table[proc_idx].param_is_const[0] = 0;
    proc_table[proc_idx].param_is_out[0] = 0;
    proc_table[proc_idx].param_has_default[0] = 0; // 'self' can never have a default
    proc_table[proc_idx].param_default_type[0] = TYPE_UNKNOWN;
    proc_table[proc_idx].param_default_value[0] = 0;
    proc_table[proc_idx].param_is_array_ref[0] = 0;
    proc_table[proc_idx].param_is_2d[0] = 0;
    proc_table[proc_idx].param_is_nd[0] = 0;
    proc_table[proc_idx].param_is_subrange[0] = 0;
    proc_table[proc_idx].param_subrange_lower[0] = 0;
    proc_table[proc_idx].param_subrange_upper[0] = 0;
    proc_table[proc_idx].param_is_record[0] = 0;
    proc_table[proc_idx].param_record_type_idx[0] = 0;
    proc_table[proc_idx].param_record_field_count[0] = 0;
    proc_table[proc_idx].param_is_proc[0] = 0;
    proc_table[proc_idx].param_proc_is_function[0] = 0;
    proc_table[proc_idx].param_proc_return_type[0] = TYPE_UNKNOWN;
    proc_table[proc_idx].param_proc_param_count[0] = 0;
    // h->param_count is capped at MAX_PARAMS by parse_proc_signature_
    // tail() with no headroom reserved for self - a method declared
    // with exactly MAX_PARAMS params already overflows
    // param_types[MAX_PARAMS] by one slot in register_class_method_
    // param()'s own real-body path too (no bounds check there either) -
    // a pre-existing gap affecting any instance method, abstract or
    // not, NOT introduced or fixed here.
    proc_table[proc_idx].param_count = h->param_count + 1;
    for (int i = 0; i < h->param_count; i++) {
        int slot = i + 1;
        proc_table[proc_idx].param_types[slot] = h->param_types[i];
        strcpy(proc_table[proc_idx].param_names[slot], h->param_names[i]);
        proc_table[proc_idx].param_is_var[slot] = h->param_is_var[i];
        proc_table[proc_idx].param_is_const[slot] = h->param_is_const[i];
        proc_table[proc_idx].param_is_out[slot] = h->param_is_out[i];
        proc_table[proc_idx].param_has_default[slot] = h->param_has_default[i];
        proc_table[proc_idx].param_default_type[slot] = h->param_default_type[i];
        proc_table[proc_idx].param_default_value[slot] = h->param_default_value[i];
        proc_table[proc_idx].param_is_subrange[slot] = 0;
        proc_table[proc_idx].param_subrange_lower[slot] = 0;
        proc_table[proc_idx].param_subrange_upper[slot] = 0;
        proc_table[proc_idx].param_is_array_ref[slot] = 0;
        proc_table[proc_idx].param_is_2d[slot] = 0;
        proc_table[proc_idx].param_is_nd[slot] = 0;
        proc_table[proc_idx].param_is_record[slot] = 0;
        proc_table[proc_idx].param_record_type_idx[slot] = 0;
        proc_table[proc_idx].param_record_field_count[slot] = 0;
        proc_table[proc_idx].param_is_proc[slot] = 0;
        proc_table[proc_idx].param_proc_is_function[slot] = 0;
        proc_table[proc_idx].param_proc_return_type[slot] = TYPE_UNKNOWN;
        proc_table[proc_idx].param_proc_param_count[slot] = 0;
    }
    return proc_idx;
}

// 'procedure ClassName.MethodName; begin ... end;' / 'function
// ClassName.MethodName; begin ... end;' - a class method's BODY.
// Already past 'procedure'/'function ClassName.', with token positioned
// at the method name. The header (name, params, return type) was
// already fully parsed and validated when the enclosing 'class ... end;'
// was declared (see parse_class_declaration()) - deliberately NOT
// repeated here, exactly like completing a 'forward'-declared procedure
// in this compiler's own existing convention (rather than Delphi's
// convention of repeating the parameter list) - see
// docs/LANGUAGE.md#classes.
//
// Registers the body as an ORDINARY top-level procedure under a
// mangled name ('TCircle__SetRadius', the same trick record fields/
// static locals/nested-record leaves already use - needed because
// procedures share one flat whole-program namespace with no per-scope
// overloading), with an implicit 'self: ClassName' parameter (always
// slot 0) prepended before the method's own declared parameters.
// 'self' is an ORDINARY parameter, read via 'self.field' exactly like
// any other class-typed parameter - there's no unqualified 'field'
// shorthand yet, a known v1 gap. A method body also doesn't support its
// own nested procedure/function declarations yet (unlike an ordinary
// procedure) - also a known, narrow v1 gap.
static void parse_class_method_body(int is_function_decl, const char *class_name, int decl_line, int is_destructor_decl) {
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Expected a method name after '%s.'", class_name);
    }
    char method_name[MAX_NAME];
    strcpy(method_name, token.text);
    match(TOKEN_IDENTIFIER);

    int class_ptr_idx = find_pointer_type(class_name);
    if (class_ptr_idx == -1 || !pointer_types[class_ptr_idx].is_class) {
        compile_error(decl_line, "'%s' is not a declared class", class_name);
    }
    PointerTypeDef *cls = &pointer_types[class_ptr_idx];
    int method_idx = -1;
    for (int i = 0; i < cls->method_count; i++) {
        if (strcmp(cls->methods[i].name, method_name) == 0) { method_idx = i; break; }
    }
    if (method_idx == -1) {
        compile_error(decl_line, "'%s' is not a declared method of class '%s'", method_name, class_name);
    }
    ProcParamHeader *h = &cls->methods[method_idx];
    if (h->is_abstract) {
        // Must run before the find_proc(h->mangled_name) != -1 check
        // below - register_abstract_method_signature() already gave
        // this method a phantom entry, so that check would otherwise
        // fire with the misleading generic "already has a body"
        // message instead of this specific one. A subclass overriding
        // concretely has its OWN h->is_abstract == 0 after the
        // override-replace (see parse_class_declaration()'s method
        // loop), so this only blocks a body in the SAME class that
        // declared the method abstract, never a legitimate override.
        compile_error(decl_line, "'%s.%s' is abstract and can't have a body - remove 'abstract' or implement it in a subclass instead", class_name, method_name);
    }
    if (h->is_inherited) {
        compile_error(decl_line, "'%s' is inherited by '%s' and hasn't been overridden - redeclare its header inside 'class %s(...) ... end;' first (see docs/LANGUAGE.md#classes)",
                       method_name, class_name, class_name);
    }
    if (h->is_function != is_function_decl) {
        compile_error(decl_line, "'%s.%s' was declared as a %s, but its body is written as a %s",
                       class_name, method_name, h->is_function ? "function" : "procedure",
                       is_function_decl ? "function" : "procedure");
    }
    if (h->is_destructor != is_destructor_decl) {
        // A SEPARATE check from is_function above - both a destructor
        // and an ordinary procedure have is_function == 0, so that check
        // alone can't tell 'destructor Destroy;' and 'procedure Destroy;'
        // apart.
        compile_error(decl_line, "'%s.%s' was declared as a %s, but its body is written as a %s",
                       class_name, method_name,
                       h->is_destructor ? "destructor" : (h->is_function ? "function" : "procedure"),
                       is_destructor_decl ? "destructor" : (is_function_decl ? "function" : "procedure"));
    }
    match(TOKEN_SEMI);

    if (find_proc(h->mangled_name) != -1) {
        compile_error(decl_line, "'%s.%s' already has a body", class_name, method_name);
    }
    int proc_idx = add_proc(h->mangled_name);
    strcpy(proc_table[proc_idx].unmangled_name, method_name);
    proc_table[proc_idx].is_function = is_function_decl;
    proc_table[proc_idx].return_type = h->return_type;
    proc_table[proc_idx].return_is_subrange = 0; // method return types are never subrange - see ProcParamHeader's own comment
    proc_table[proc_idx].return_subrange_lower = 0;
    proc_table[proc_idx].return_subrange_upper = 0;

    int saved_function_idx = current_function_idx;
    int saved_proc_idx = current_proc_idx;
    int saved_class_ptr_idx = current_class_ptr_idx;
    int saved_method_is_class_method = current_method_is_class_method;
    current_proc_idx = proc_idx;
    current_class_ptr_idx = class_ptr_idx;
    current_method_is_class_method = h->is_class_method;
    nesting_depth++;
    if (nesting_depth >= MAX_NESTING_DEPTH) {
        compile_error(decl_line, "'%s.%s' is nested too deeply (limit is %d levels)", class_name, method_name, MAX_NESTING_DEPTH);
    }
    current_local_count = 0;
    local_record_var_count = 0;
    declared_label_count = 0;

    if (h->is_class_method) {
        // A TRUE class method has no instance, so no 'self' parameter at
        // slot 0 at all - real parameters start at slot 0 directly (see
        // build_class_member_access()/try_resolve_class_qualified_
        // access() - a class method call is a plain NODE_CALL through
        // parse_call_arguments(), which has no self-offset logic of its
        // own to match).
        for (int i = 0; i < h->param_count; i++) {
            register_class_method_param(proc_idx, i, h->param_names[i], h->param_types[i], h->param_is_var[i], h->param_is_const[i], h->param_is_out[i], h->param_has_default[i], h->param_default_type[i], h->param_default_value[i]);
        }
        proc_table[proc_idx].param_count = h->param_count;
    } else {
        register_class_method_param(proc_idx, 0, "self", (DataType)(TYPE_POINTER_BASE + class_ptr_idx), 0, 0, 0, 0, TYPE_UNKNOWN, 0);
        for (int i = 0; i < h->param_count; i++) {
            register_class_method_param(proc_idx, i + 1, h->param_names[i], h->param_types[i], h->param_is_var[i], h->param_is_const[i], h->param_is_out[i], h->param_has_default[i], h->param_default_type[i], h->param_default_value[i]);
        }
        proc_table[proc_idx].param_count = h->param_count + 1;
    }
    proc_table[proc_idx].param_slot_count = current_local_count; // one frame slot per param here - always true, since method params are scalar-only (see this function's own comment)

    if (token.type == TOKEN_LABEL) {
        parse_label_section();
    }

    if (token.type == TOKEN_VAR) {
        match(TOKEN_VAR);
        while (token.type == TOKEN_IDENTIFIER || token.type == TOKEN_STATIC) {
            int is_static = 0;
            if (token.type == TOKEN_STATIC) {
                is_static = 1;
                match(TOKEN_STATIC);
            }
            NameGroup g = parse_name_group();
            for (int i = 0; i < g.count; i++) {
                if (is_static) {
                    if (g.is_array) {
                        compile_error(token.line, "'static' doesn't apply to arrays - a local array is already shared across every call, unlike a scalar local (see docs/LANGUAGE.md)");
                    }
                    if (g.is_record) {
                        compile_error(token.line, "'static' doesn't apply to records yet - a persistent record local isn't supported (see docs/LANGUAGE.md)");
                    }
                    add_static_local(proc_table[proc_idx].name, g.names[i], g.type,
                                      g.is_subrange, g.subrange_lower, g.subrange_upper);
                } else if (g.is_array_of_record) {
                    add_local_array_rec(g.names[i], g.array_record_type_idx, g.array_lower, g.array_upper);
                } else if (g.is_array) {
                    add_local_array(g.names[i], g.type, g.array_lower, g.array_upper,
                                     g.is_2d, g.array_lower2, g.array_upper2,
                                     g.is_nd, g.nd_dims, g.nd_lower, g.nd_upper,
                                     g.is_subrange, g.subrange_lower, g.subrange_upper);
                } else if (g.is_record) {
                    add_local_record(g.names[i], g.record_type_idx);
                } else {
                    int idx = add_local(g.names[i], g.type);
                    current_locals[idx].is_subrange = g.is_subrange;
                    current_locals[idx].subrange_lower = g.subrange_lower;
                    current_locals[idx].subrange_upper = g.subrange_upper;
                }
            }
            match(TOKEN_SEMI);
        }
    }

    if (proc_table[proc_idx].is_function) {
        proc_table[proc_idx].return_slot = current_local_count++;
        current_function_idx = proc_idx;
    } else {
        current_function_idx = -1;
    }

    ASTNode *body = compound_statement();
    check_all_labels_defined();
    match(TOKEN_SEMI);

    proc_table[proc_idx].body = body;
    proc_table[proc_idx].local_count = current_local_count;
    proc_table[proc_idx].is_forward = 0;

    check_uninitialized_locals(proc_idx, body, decl_line);

    current_local_count = 0;
    local_record_var_count = 0;
    nesting_depth--;
    current_proc_idx = saved_proc_idx;
    current_function_idx = saved_function_idx;
    current_class_ptr_idx = saved_class_ptr_idx;
    current_method_is_class_method = saved_method_is_class_method;
}

// Registers the name (via add_proc) before parsing anything else, so a
// call to this procedure's own name inside its body - recursion -
// resolves correctly. Parameter info is written back to proc_table right
// after the parameter list is parsed, before the body: a recursive call
// site inside the body needs the real param_count/param_types already in
// place, not the placeholders add_proc() set.
static void subroutine_declaration(int is_function_decl, int header_only, int is_destructor_decl) {
    if (is_destructor_decl) {
        match(TOKEN_DESTRUCTOR);
    } else {
        match(is_function_decl ? TOKEN_FUNCTION : TOKEN_PROCEDURE);
    }
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Expected a %s name", is_destructor_decl ? "destructor" : (is_function_decl ? "function" : "procedure"));
    }
    char name[MAX_NAME];
    int decl_line = token.line;
    strcpy(name, token.text);
    match(TOKEN_IDENTIFIER);

    if (token.type == TOKEN_PERIOD) {
        // 'procedure ClassName.MethodName; ... ' - a class method's
        // BODY, not an ordinary procedure/function named 'ClassName'.
        // See parse_class_method_body()'s own comment. Only valid at
        // the true top level (nesting_depth is still -1 here, before
        // THIS declaration's own nesting_depth++ below - a class method
        // body isn't itself nestable inside another procedure, the
        // same way a class declaration itself only ever appears in the
        // main program's own 'type' section).
        if (nesting_depth != -1) {
            compile_error(decl_line, "A class method body ('%s.Method') must be declared at the top level, not nested inside another procedure", name);
        }
        match(TOKEN_PERIOD);
        parse_class_method_body(is_function_decl, name, decl_line, is_destructor_decl);
        return;
    }

    if (is_destructor_decl) {
        compile_error(decl_line, "'destructor' can only be used for a class method body ('destructor ClassName.MethodName; ...') - '%s' isn't followed by '.'", name);
    }

    int existing_idx = find_proc(name);
    int completing_forward = (existing_idx != -1 && proc_table[existing_idx].is_forward);

    if (completing_forward && proc_table[existing_idx].is_function != is_function_decl) {
        compile_error(decl_line, "'%s' was forward-declared as a %s, but completed as a %s", name,
                       proc_table[existing_idx].is_function ? "function" : "procedure",
                       is_function_decl ? "function" : "procedure");
    }

    int proc_idx = completing_forward ? existing_idx : add_proc(name);

    // Saved/restored as plain C locals - the C call stack itself threads
    // the enclosing procedure's own state back correctly once a nested
    // declaration (parsed recursively, below) finishes, without needing
    // any array/stack of its own. An unconditional clear at the end of
    // this function (as it used to do, back when only one level could
    // ever be active) would otherwise wrongly clobber an ENCLOSING
    // procedure's own current_function_idx/current_proc_idx once a
    // nested child finishes parsing and control returns here.
    int saved_function_idx = current_function_idx;
    int saved_proc_idx = current_proc_idx;
    current_proc_idx = proc_idx;
    nesting_depth++;
    if (nesting_depth >= MAX_NESTING_DEPTH) {
        compile_error(decl_line, "'%s' is nested too deeply (limit is %d levels)", name, MAX_NESTING_DEPTH);
    }
    current_local_count = 0;
    local_record_var_count = 0;
    declared_label_count = 0;

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
                                     proc_table[proc_idx].param_array_lower[i], proc_table[proc_idx].param_array_upper[i],
                                     proc_table[proc_idx].param_is_2d[i], proc_table[proc_idx].param_array_lower2[i],
                                     proc_table[proc_idx].param_array_upper2[i],
                                     proc_table[proc_idx].param_is_nd[i], proc_table[proc_idx].param_nd_dims[i],
                                     proc_table[proc_idx].param_nd_lower[i], proc_table[proc_idx].param_nd_upper[i],
                                     proc_table[proc_idx].param_is_subrange[i], proc_table[proc_idx].param_subrange_lower[i],
                                     proc_table[proc_idx].param_subrange_upper[i]);
            } else if (proc_table[proc_idx].param_is_record[i]) {
                add_local_record(proc_table[proc_idx].param_names[i], proc_table[proc_idx].param_record_type_idx[i]);
            } else if (proc_table[proc_idx].param_is_proc[i]) {
                add_local_proc_param(proc_table[proc_idx].param_names[i], proc_table[proc_idx].param_proc_is_function[i],
                                      proc_table[proc_idx].param_proc_return_type[i], proc_table[proc_idx].param_proc_param_count[i],
                                      proc_table[proc_idx].param_proc_param_types[i], proc_table[proc_idx].param_proc_param_is_var[i]);
            } else if (proc_table[proc_idx].param_is_var[i]) {
                add_local_var_param(proc_table[proc_idx].param_names[i], proc_table[proc_idx].param_types[i],
                                     proc_table[proc_idx].param_is_subrange[i], proc_table[proc_idx].param_subrange_lower[i],
                                     proc_table[proc_idx].param_subrange_upper[i],
                                     proc_table[proc_idx].param_is_const[i], proc_table[proc_idx].param_is_out[i]);
            } else {
                int idx = add_local(proc_table[proc_idx].param_names[i], proc_table[proc_idx].param_types[i]);
                current_locals[idx].is_subrange = proc_table[proc_idx].param_is_subrange[i];
                current_locals[idx].subrange_lower = proc_table[proc_idx].param_subrange_lower[i];
                current_locals[idx].subrange_upper = proc_table[proc_idx].param_subrange_upper[i];
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
        int param_is_2d[MAX_PARAMS];
        int param_array_lower2[MAX_PARAMS];
        int param_array_upper2[MAX_PARAMS];
        int param_is_nd[MAX_PARAMS];
        int param_nd_dims[MAX_PARAMS];
        int param_nd_lower[MAX_PARAMS][MAX_ARRAY_DIMS];
        int param_nd_upper[MAX_PARAMS][MAX_ARRAY_DIMS];
        int param_is_subrange[MAX_PARAMS];
        int param_subrange_lower[MAX_PARAMS];
        int param_subrange_upper[MAX_PARAMS];
        int param_is_record[MAX_PARAMS];
        int param_record_type_idx[MAX_PARAMS];
        int param_record_field_count[MAX_PARAMS];
        int param_is_var[MAX_PARAMS];
        int param_is_const[MAX_PARAMS];
        int param_is_out[MAX_PARAMS];
        int param_is_proc[MAX_PARAMS];
        int param_proc_is_function[MAX_PARAMS];
        DataType param_proc_return_type[MAX_PARAMS];
        int param_proc_param_count[MAX_PARAMS];
        DataType param_proc_param_types[MAX_PARAMS][MAX_PARAMS];
        int param_proc_param_is_var[MAX_PARAMS][MAX_PARAMS];
        int param_has_default[MAX_PARAMS];
        DataType param_default_type[MAX_PARAMS];
        int param_default_value[MAX_PARAMS];
        int seen_default = 0; // once any parameter has a default, every
                               // parameter after it (across all
                               // remaining groups) must also have one -
                               // see docs/LANGUAGE.md.

        if (token.type == TOKEN_LPAREN) {
            match(TOKEN_LPAREN);
            if (token.type != TOKEN_RPAREN) {
                while (1) {
                    // An inline procedural/functional parameter ('function
                    // f(...): T' or 'procedure f(...)') is its OWN group,
                    // parsed entirely differently from a NameGroup (it
                    // always declares exactly one name, and its own inner
                    // parameter list is unrelated grammar) - handled here,
                    // before falling through to the ordinary
                    // var/NameGroup path below.
                    if (token.type == TOKEN_FUNCTION || token.type == TOKEN_PROCEDURE) {
                        if (param_count >= MAX_PARAMS) {
                            compile_error(token.line, "Too many parameters (limit is %d)", MAX_PARAMS);
                        }
                        ProcParamHeader h = parse_proc_param_header(0); // inline functional/procedural parameter - const/out scope cut, see docs/LANGUAGE.md
                        add_local_proc_param(h.name, h.is_function, h.return_type, h.param_count, h.param_types, h.param_is_var);
                        strcpy(param_names[param_count], h.name);
                        param_types[param_count] = TYPE_INTEGER; // unused - see is_proc_param's comment
                        param_is_array_ref[param_count] = 0;
                        param_is_2d[param_count] = 0;
                        param_is_nd[param_count] = 0;
                        param_nd_dims[param_count] = 0;
                        param_is_subrange[param_count] = 0;
                        param_is_record[param_count] = 0;
                        param_record_type_idx[param_count] = 0;
                        param_record_field_count[param_count] = 0;
                        param_is_var[param_count] = 0;
                        param_is_const[param_count] = 0;
                        param_is_out[param_count] = 0;
                        param_is_proc[param_count] = 1;
                        param_proc_is_function[param_count] = h.is_function;
                        param_proc_return_type[param_count] = h.return_type;
                        param_proc_param_count[param_count] = h.param_count;
                        for (int i = 0; i < h.param_count; i++) {
                            param_proc_param_types[param_count][i] = h.param_types[i];
                            param_proc_param_is_var[param_count][i] = h.param_is_var[i];
                        }
                        param_has_default[param_count] = 0;
                        param_default_type[param_count] = TYPE_UNKNOWN;
                        param_default_value[param_count] = 0;
                        param_count++;
                        if (token.type == TOKEN_EQ) {
                            compile_error(token.line, "default parameter values aren't supported for procedural/functional parameters yet (see docs/LANGUAGE.md)");
                        }
                        if (seen_default) {
                            compile_error(token.line, "parameter '%s' must have a default value - once one parameter has a default, every parameter after it must too (see docs/LANGUAGE.md)", h.name);
                        }
                        if (token.type == TOKEN_SEMI) { match(TOKEN_SEMI); continue; }
                        break;
                    }

                    // 'var'/'const'/'out' are per-group modifiers here
                    // (inside the parameter list), unlike the 'var'/
                    // 'const' KEYWORDS that introduce a whole
                    // local-variable/named-constant SECTION below - same
                    // tokens, different grammar position, so this check
                    // only fires here, once per semicolon-separated
                    // parameter group. Mutually exclusive with each
                    // other. is_var_group means "by reference at all" -
                    // 'const'/'out' both set it too (they share the
                    // exact same by-reference mechanism as plain 'var'),
                    // not just their own is_const_group/is_out_group -
                    // this is what makes the whole-record rejection just
                    // below, and the add_local_var_param() dispatch a
                    // few lines down, already cover 'const'/'out' for
                    // free.
                    int is_var_group = 0, is_const_group = 0, is_out_group = 0;
                    if (token.type == TOKEN_VAR) {
                        is_var_group = 1;
                        match(TOKEN_VAR);
                    } else if (token.type == TOKEN_CONST) {
                        is_var_group = 1;
                        is_const_group = 1;
                        match(TOKEN_CONST);
                    } else if (token.type == TOKEN_OUT) {
                        is_var_group = 1;
                        is_out_group = 1;
                        match(TOKEN_OUT);
                    }
                    NameGroup g = parse_name_group();
                    int has_default = 0, default_value = 0;
                    DataType default_type = TYPE_UNKNOWN;
                    if (token.type == TOKEN_EQ) {
                        if (is_var_group) {
                            compile_error(token.line, "'var'/'const'/'out' parameters cannot have default values (see docs/LANGUAGE.md)");
                        }
                        if (g.is_array || g.is_record || g.is_array_of_record) {
                            compile_error(token.line, "default values aren't supported for array/record parameters yet (see docs/LANGUAGE.md)");
                        }
                        if (g.is_subrange) {
                            compile_error(token.line, "default values aren't supported for subrange-typed parameters yet - use the base type instead (see docs/LANGUAGE.md)");
                        }
                        if (g.count != 1) {
                            compile_error(token.line, "a default value can only be given for a single parameter, not a shared 'name, name: type' group (see docs/LANGUAGE.md)");
                        }
                        match(TOKEN_EQ);
                        int default_line = token.line;
                        ASTNode *value = expression();
                        type_check(value);
                        value = optimize_ast(value);
                        if (value->type != NODE_NUMBER && value->type != NODE_REAL_NUMBER
                            && value->type != NODE_BOOLEAN && value->type != NODE_STRING) {
                            compile_error(default_line, "default value for parameter '%s' is not a compile-time constant expression", g.names[0]);
                        }
                        if (g.type == TYPE_REAL && value->expression_type == TYPE_INTEGER) {
                            value->expression_type = TYPE_REAL;
                            value->data.num_value = float_to_bits((float)value->data.num_value);
                        } else if (!((g.type == TYPE_STRING || g.type == TYPE_CHAR) && (value->expression_type == TYPE_STRING || value->expression_type == TYPE_CHAR))
                                   && value->expression_type != g.type) {
                            compile_error(default_line, "default value for parameter '%s' doesn't match its declared type", g.names[0]);
                        }
                        has_default = 1;
                        default_type = value->expression_type;
                        default_value = (value->type == NODE_STRING) ? value->data.var_idx : value->data.num_value;
                        seen_default = 1;
                    } else if (seen_default) {
                        compile_error(token.line, "parameter '%s' must have a default value - once one parameter has a default, every parameter after it must too (see docs/LANGUAGE.md)", g.count > 0 ? g.names[0] : "?");
                    }
                    for (int i = 0; i < g.count; i++) {
                        if (param_count >= MAX_PARAMS) {
                            compile_error(token.line, "Too many parameters (limit is %d)", MAX_PARAMS);
                        }
                        if (is_var_group && g.is_record) {
                            compile_error(token.line, "'var'/'const'/'out' don't support whole records yet - only a scalar by-reference parameter is supported (see docs/LANGUAGE.md)");
                        }
                        if (g.is_array_of_record) {
                            compile_error(token.line, "Array-of-record parameters aren't supported yet - copy into/out of a local array of records instead (see docs/LANGUAGE.md)");
                        }
                        if (g.is_array) {
                            // 'var' on an array parameter is accepted but
                            // redundant - an array parameter is already
                            // always by reference, with or without it.
                            add_local_array_ref(g.names[i], g.type, g.array_lower, g.array_upper,
                                                 g.is_2d, g.array_lower2, g.array_upper2,
                                                 g.is_nd, g.nd_dims, g.nd_lower, g.nd_upper,
                                                 g.is_subrange, g.subrange_lower, g.subrange_upper);
                        } else if (g.is_record) {
                            add_local_record(g.names[i], g.record_type_idx);
                        } else if (is_var_group) {
                            add_local_var_param(g.names[i], g.type, g.is_subrange, g.subrange_lower, g.subrange_upper, is_const_group, is_out_group);
                        } else {
                            int idx = add_local(g.names[i], g.type);
                            current_locals[idx].is_subrange = g.is_subrange;
                            current_locals[idx].subrange_lower = g.subrange_lower;
                            current_locals[idx].subrange_upper = g.subrange_upper;
                        }
                        strcpy(param_names[param_count], g.names[i]);
                        param_types[param_count] = g.type;
                        param_is_array_ref[param_count] = g.is_array;
                        param_array_lower[param_count] = g.array_lower;
                        param_array_upper[param_count] = g.array_upper;
                        param_is_2d[param_count] = g.is_2d;
                        param_array_lower2[param_count] = g.array_lower2;
                        param_array_upper2[param_count] = g.array_upper2;
                        param_is_nd[param_count] = g.is_nd;
                        param_nd_dims[param_count] = g.nd_dims;
                        for (int d = 0; d < g.nd_dims; d++) {
                            param_nd_lower[param_count][d] = g.nd_lower[d];
                            param_nd_upper[param_count][d] = g.nd_upper[d];
                        }
                        param_is_subrange[param_count] = g.is_subrange;
                        param_subrange_lower[param_count] = g.subrange_lower;
                        param_subrange_upper[param_count] = g.subrange_upper;
                        param_is_record[param_count] = g.is_record;
                        param_record_type_idx[param_count] = g.record_type_idx;
                        param_record_field_count[param_count] = g.is_record ? record_type_leaf_count(g.record_type_idx) : 0;
                        param_is_var[param_count] = is_var_group;
                        param_is_const[param_count] = is_const_group;
                        param_is_out[param_count] = is_out_group;
                        param_is_proc[param_count] = 0;
                        param_proc_is_function[param_count] = 0;
                        param_proc_param_count[param_count] = 0;
                        param_has_default[param_count] = has_default;
                        param_default_type[param_count] = default_type;
                        param_default_value[param_count] = default_value;
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
        // Every add_local()/add_local_array_ref()/add_local_record() call
        // above (one call per parameter, or per field for a record
        // parameter) has already run, so current_local_count is exactly
        // the number of frame slots the parameters ended up occupying -
        // see param_slot_count's comment in common.h.
        proc_table[proc_idx].param_slot_count = current_local_count;
        for (int i = 0; i < param_count; i++) {
            proc_table[proc_idx].param_types[i] = param_types[i];
            strcpy(proc_table[proc_idx].param_names[i], param_names[i]);
            proc_table[proc_idx].param_is_array_ref[i] = param_is_array_ref[i];
            proc_table[proc_idx].param_array_lower[i] = param_array_lower[i];
            proc_table[proc_idx].param_array_upper[i] = param_array_upper[i];
            proc_table[proc_idx].param_is_2d[i] = param_is_2d[i];
            proc_table[proc_idx].param_array_lower2[i] = param_array_lower2[i];
            proc_table[proc_idx].param_array_upper2[i] = param_array_upper2[i];
            proc_table[proc_idx].param_is_nd[i] = param_is_nd[i];
            proc_table[proc_idx].param_nd_dims[i] = param_nd_dims[i];
            for (int d = 0; d < param_nd_dims[i]; d++) {
                proc_table[proc_idx].param_nd_lower[i][d] = param_nd_lower[i][d];
                proc_table[proc_idx].param_nd_upper[i][d] = param_nd_upper[i][d];
            }
            proc_table[proc_idx].param_is_subrange[i] = param_is_subrange[i];
            proc_table[proc_idx].param_subrange_lower[i] = param_subrange_lower[i];
            proc_table[proc_idx].param_subrange_upper[i] = param_subrange_upper[i];
            proc_table[proc_idx].param_is_record[i] = param_is_record[i];
            proc_table[proc_idx].param_record_type_idx[i] = param_record_type_idx[i];
            proc_table[proc_idx].param_record_field_count[i] = param_record_field_count[i];
            proc_table[proc_idx].param_is_var[i] = param_is_var[i];
            proc_table[proc_idx].param_is_const[i] = param_is_const[i];
            proc_table[proc_idx].param_is_out[i] = param_is_out[i];
            proc_table[proc_idx].param_has_default[i] = param_has_default[i];
            proc_table[proc_idx].param_default_type[i] = param_default_type[i];
            proc_table[proc_idx].param_default_value[i] = param_default_value[i];
            proc_table[proc_idx].param_is_proc[i] = param_is_proc[i];
            proc_table[proc_idx].param_proc_is_function[i] = param_proc_is_function[i];
            proc_table[proc_idx].param_proc_return_type[i] = param_proc_return_type[i];
            proc_table[proc_idx].param_proc_param_count[i] = param_proc_param_count[i];
            for (int j = 0; j < param_proc_param_count[i]; j++) {
                proc_table[proc_idx].param_proc_param_types[i][j] = param_proc_param_types[i][j];
                proc_table[proc_idx].param_proc_param_is_var[i][j] = param_proc_param_is_var[i][j];
            }
        }

        proc_table[proc_idx].is_function = is_function_decl;
        if (is_function_decl) {
            match(TOKEN_COLON);
            proc_table[proc_idx].return_type = parse_scalar_type();
            proc_table[proc_idx].return_is_subrange = scalar_type_is_subrange;
            proc_table[proc_idx].return_subrange_lower = scalar_type_subrange_lower;
            proc_table[proc_idx].return_subrange_upper = scalar_type_subrange_upper;
        }
    }

    match(TOKEN_SEMI);

    // header_only (a unit's interface section - see load_unit()) needs no
    // literal 'forward' keyword: the interface/implementation split
    // itself IS the forward declaration, same as real Pascal units. Takes
    // the exact same early-return path as an explicit 'forward;' though,
    // just without consuming a keyword that was never there.
    if (!completing_forward && (header_only || token.type == TOKEN_FORWARD)) {
        if (!header_only) {
            match(TOKEN_FORWARD);
            match(TOKEN_SEMI);
        }
        proc_table[proc_idx].is_forward = 1;
        current_local_count = 0;
        local_record_var_count = 0;
        nesting_depth--;
        current_proc_idx = saved_proc_idx;
        current_function_idx = saved_function_idx;
        return; // the real body comes later, in a completing declaration
    }

    if (token.type == TOKEN_LABEL) {
        parse_label_section();
    }

    if (token.type == TOKEN_VAR) {
        match(TOKEN_VAR);
        while (token.type == TOKEN_IDENTIFIER || token.type == TOKEN_STATIC) {
            int is_static = 0;
            if (token.type == TOKEN_STATIC) {
                is_static = 1;
                match(TOKEN_STATIC);
            }
            NameGroup g = parse_name_group();
            for (int i = 0; i < g.count; i++) {
                if (is_static) {
                    if (g.is_array) {
                        compile_error(token.line, "'static' doesn't apply to arrays - a local array is already shared across every call, unlike a scalar local (see docs/LANGUAGE.md)");
                    }
                    if (g.is_record) {
                        compile_error(token.line, "'static' doesn't apply to records yet - a persistent record local isn't supported (see docs/LANGUAGE.md)");
                    }
                    add_static_local(proc_table[proc_idx].name, g.names[i], g.type,
                                      g.is_subrange, g.subrange_lower, g.subrange_upper);
                } else if (g.is_array_of_record) {
                    add_local_array_rec(g.names[i], g.array_record_type_idx, g.array_lower, g.array_upper);
                } else if (g.is_array) {
                    add_local_array(g.names[i], g.type, g.array_lower, g.array_upper,
                                     g.is_2d, g.array_lower2, g.array_upper2,
                                     g.is_nd, g.nd_dims, g.nd_lower, g.nd_upper,
                                     g.is_subrange, g.subrange_lower, g.subrange_upper);
                } else if (g.is_record) {
                    add_local_record(g.names[i], g.record_type_idx);
                } else {
                    int idx = add_local(g.names[i], g.type);
                    current_locals[idx].is_subrange = g.is_subrange;
                    current_locals[idx].subrange_lower = g.subrange_lower;
                    current_locals[idx].subrange_upper = g.subrange_upper;
                }
            }
            match(TOKEN_SEMI);
        }
    }

    // Nested procedure/function declarations - one or more procedure/
    // function declarations INSIDE this one's own declaration section,
    // recursing back into this same function. Mirrors parse_ast()'s own
    // top-level procedure-parsing loop exactly (same loop condition, same
    // label-table stash/restore around it, for the same reason: each
    // nested declaration resets and reuses this same static
    // declared_labels table for its own independent label namespace, so
    // THIS procedure's own label section - already parsed above, if any -
    // must survive parsing every nested child below).
    DeclaredLabel saved_labels[MAX_DECLARED_LABELS];
    int saved_label_count = declared_label_count;
    memcpy(saved_labels, declared_labels, sizeof(DeclaredLabel) * declared_label_count);

    while (token.type == TOKEN_PROCEDURE || token.type == TOKEN_FUNCTION || token.type == TOKEN_DESTRUCTOR) {
        subroutine_declaration(token.type == TOKEN_FUNCTION, 0, token.type == TOKEN_DESTRUCTOR);
    }

    memcpy(declared_labels, saved_labels, sizeof(DeclaredLabel) * saved_label_count);
    declared_label_count = saved_label_count;

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
    check_all_labels_defined();
    match(TOKEN_SEMI);

    proc_table[proc_idx].body = body;
    proc_table[proc_idx].local_count = current_local_count; // params + locals (+ return_slot, for a function)
    proc_table[proc_idx].is_forward = 0; // completed now (harmless if it wasn't forward to begin with)

    check_uninitialized_locals(proc_idx, body, decl_line);

    current_local_count = 0;
    local_record_var_count = 0;
    nesting_depth--;
    current_proc_idx = saved_proc_idx;
    current_function_idx = saved_function_idx;
}

// Parses one compile-time-constant case-label value: an (optionally
// negative) integer literal, a char literal ('a' or #NNN), true/false, a
// 'const' reference, or a bare enumerated value name - the only forms
// standard Pascal allows as a case-label constant. Deliberately doesn't
// know (or need to know) the enclosing case statement's selector type:
// whether this label's type actually matches the selector is checked
// later, by type_checker.c's NODE_CASE handling, once the selector's own
// expression_type is resolved - which, for anything beyond a bare
// variable, only happens during that later pass (see the NODE_BINARY_OP
// construction in term()/arithmetic_expression()/expression() above,
// none of which set expression_type at parse time the way a leaf node
// does). Returns a leaf node (NODE_NUMBER/NODE_STRING/NODE_BOOLEAN) with
// expression_type already set, mirroring the equivalent literal-handling
// branches in factor().
static ASTNode *parse_case_label_value(void) {
    int sign = 1;
    if (token.type == TOKEN_MINUS) {
        sign = -1;
        match(TOKEN_MINUS);
    }
    if (token.type == TOKEN_NUMBER) {
        ASTNode *node = create_node(NODE_NUMBER);
        node->data.num_value = token.value * sign;
        node->expression_type = TYPE_INTEGER;
        match(TOKEN_NUMBER);
        return node;
    }
    if (sign == -1) {
        compile_error(token.line, "Expected an integer literal after '-'");
    }
    if (token.type == TOKEN_STRING) {
        if (strlen(token.string_value) != 1) {
            compile_error(token.line, "A char case label must be exactly one character, got '%s'", token.string_value);
        }
        ASTNode *node = create_node(NODE_STRING);
        node->data.var_idx = intern_string(token.string_value);
        node->expression_type = TYPE_CHAR;
        match(TOKEN_STRING);
        return node;
    }
    if (token.type == TOKEN_CHARCODE) {
        if (token.value < 1 || token.value > 255) {
            compile_error(token.line, "Character code %d out of range (1..255)", token.value);
        }
        char buf[2] = { (char)token.value, '\0' };
        ASTNode *node = create_node(NODE_STRING);
        node->data.var_idx = intern_string(buf);
        node->expression_type = TYPE_CHAR;
        match(TOKEN_CHARCODE);
        return node;
    }
    if (token.type == TOKEN_TRUE || token.type == TOKEN_FALSE) {
        ASTNode *node = create_node(NODE_BOOLEAN);
        node->data.num_value = token.value;
        node->expression_type = TYPE_BOOLEAN;
        next_token();
        return node;
    }
    if (token.type == TOKEN_IDENTIFIER) {
        int const_idx = find_const(token.text);
        if (const_idx != -1) {
            ConstDef *c = &const_defs[const_idx];
            if (c->type == TYPE_REAL) {
                compile_error(token.line, "'%s' is a real constant - case labels must be an ordinal type (integer, char, boolean, or enumerated)", token.text);
            }
            ASTNode *node;
            if (c->type == TYPE_STRING || c->type == TYPE_CHAR) {
                node = create_node(NODE_STRING);
                node->data.var_idx = c->value;
            } else if (c->type == TYPE_BOOLEAN) {
                node = create_node(NODE_BOOLEAN);
                node->data.num_value = c->value;
            } else { // TYPE_INTEGER
                node = create_node(NODE_NUMBER);
                node->data.num_value = c->value;
            }
            node->expression_type = c->type;
            match(TOKEN_IDENTIFIER);
            return node;
        }
        int enum_type_idx, ordinal;
        if (find_enum_value(token.text, &enum_type_idx, &ordinal)) {
            ASTNode *node = create_node(NODE_NUMBER);
            node->data.num_value = ordinal;
            node->expression_type = (DataType)(TYPE_ENUM_BASE + enum_type_idx);
            match(TOKEN_IDENTIFIER);
            return node;
        }
        compile_error(token.line, "'%s' is not a constant or enumerated value", token.text);
    }
    compile_error(token.line, "Expected a case label (a literal, constant, or enumerated value)");
    return NULL; // unreachable - compile_error() never returns
}

// Returns a case-label leaf's ordinal value for comparison/ordering
// purposes: data.num_value directly for NODE_NUMBER/NODE_BOOLEAN (already
// the ordinal), or the character code of a NODE_STRING (char) label,
// whose data.var_idx is a string_pool[] index rather than an ordinal
// itself. Used only for parse-time bookkeeping (range-bound validation,
// overlap detection below) - codegen keeps using the leaf node itself
// (var_idx + OP_SEQ) for actual char equality/ordering.
static int case_label_ordinal(ASTNode *label) {
    if (label->type == NODE_STRING) {
        return (unsigned char)string_pool[label->data.var_idx][0];
    }
    return label->data.num_value;
}

// Parses one case label: a single value (parse_case_label_value()), or,
// if '..' follows, a 'low..high' range. Range bounds must share a type
// and low must not exceed high - both checked here since case_label_ordinal()
// already gives an ordinal to compare regardless of the underlying type
// (int/char/bool/enum all resolve to a plain int this way).
static ASTNode *parse_case_label(void) {
    int low_line = token.line;
    ASTNode *low = parse_case_label_value();
    if (token.type != TOKEN_DOTDOT) {
        return low;
    }
    match(TOKEN_DOTDOT);
    int high_line = token.line;
    ASTNode *high = parse_case_label_value();
    if (low->expression_type != high->expression_type) {
        compile_error(high_line, "Case range bounds must have the same type");
    }
    if (case_label_ordinal(low) > case_label_ordinal(high)) {
        compile_error(low_line, "Case range's low bound must not exceed its high bound");
    }
    ASTNode *range = create_node(NODE_CASE_RANGE);
    range->left = low;
    range->right = high;
    range->expression_type = low->expression_type;
    return range;
}

// 'case selector of label1[, label2...]: statement1; ... [else
// statementN] end' - see the NODE_CASE/NODE_CASE_ARM/NODE_CASE_RANGE
// comments in common.h for the AST shape this builds. Case labels
// (values and ranges alike) must not overlap across the WHOLE statement
// (checked here, at parse time, via an interval-overlap test - a plain
// value is tracked as the degenerate range [ordinal, ordinal] - which
// needs nothing the selector's own type resolution would add). Whether
// each label's type actually matches the selector is checked later, by
// type_checker.c, once the selector is fully resolved.
static ASTNode *parse_case_statement(void) {
    match(TOKEN_CASE);
    ASTNode *node = create_node(NODE_CASE);
    node->left = expression();
    match(TOKEN_OF);

    DataType seen_types[MAX_CASE_LABELS];
    int seen_lo[MAX_CASE_LABELS];
    int seen_hi[MAX_CASE_LABELS];
    int seen_count = 0;

    ASTNode *arm_head = NULL;
    ASTNode *arm_tail = NULL;
    while (token.type != TOKEN_ELSE && token.type != TOKEN_END) {
        ASTNode *label_head = NULL;
        ASTNode *label_tail = NULL;
        while (1) {
            int label_line = token.line;
            ASTNode *label = parse_case_label();
            int lo, hi;
            if (label->type == NODE_CASE_RANGE) {
                lo = case_label_ordinal(label->left);
                hi = case_label_ordinal(label->right);
            } else {
                lo = hi = case_label_ordinal(label);
            }
            for (int i = 0; i < seen_count; i++) {
                if (seen_types[i] == label->expression_type && lo <= seen_hi[i] && seen_lo[i] <= hi) {
                    compile_error(label_line, "Duplicate or overlapping case label");
                }
            }
            if (seen_count >= MAX_CASE_LABELS) {
                compile_error(label_line, "Too many case labels in one 'case' statement (limit is %d)", MAX_CASE_LABELS);
            }
            seen_types[seen_count] = label->expression_type;
            seen_lo[seen_count] = lo;
            seen_hi[seen_count] = hi;
            seen_count++;

            if (!label_head) label_head = label; else label_tail->next = label;
            label_tail = label;
            if (token.type == TOKEN_COMMA) { match(TOKEN_COMMA); continue; }
            break;
        }
        match(TOKEN_COLON);

        ASTNode *arm = create_node(NODE_CASE_ARM);
        arm->left = label_head;
        arm->right = statement();
        if (!arm_head) arm_head = arm; else arm_tail->next = arm;
        arm_tail = arm;

        if (token.type == TOKEN_SEMI) { match(TOKEN_SEMI); continue; }
        break;
    }
    if (!arm_head) {
        compile_error(token.line, "'case' must have at least one label");
    }
    node->right = arm_head;

    if (token.type == TOKEN_ELSE) {
        match(TOKEN_ELSE);
        node->extra = statement();
        if (token.type == TOKEN_SEMI) { match(TOKEN_SEMI); }
    }
    match(TOKEN_END);

    // Synthesized even when there IS an else clause - simpler than
    // conditionally interning it, and it costs nothing (one string_pool
    // slot) when unused. Mirrors how NODE_ASSERT's default "Assertion
    // failed" message is always synthesized too, regardless of whether
    // the user supplied their own.
    node->data.var_idx = intern_string("No matching case label and no else clause");
    return node;
}

// Parses ONE read/readln target (a variable - a with-target's field, a
// record field (global or local), a plain local/parameter, a static
// local, or a plain global) and returns the appropriate node
// (NODE_READLN or NODE_LOCAL_READLN). Deliberately does NOT set the
// returned node's ->op (the caller - parse_read_statement() below -
// decides 'read' vs 'readln' semantics once it knows whether this is the
// LAST target in the list) and does NOT consume ')' (more targets may
// follow, comma-separated).
static ASTNode *parse_read_target(void) {
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "readln expects a variable identifier");
    }
    {
        int with_field_idx = find_with_field(token.text);
        if (with_field_idx != -1) {
            match(TOKEN_IDENTIFIER);
            if (sym_table[with_field_idx].is_array) {
                compile_error(token.line, "readln into an array is not supported");
            }
            if (sym_table[with_field_idx].type >= TYPE_ENUM_BASE && sym_table[with_field_idx].type < TYPE_POINTER_BASE) {
                compile_error(token.line, "readln into an enumerated value is not supported");
            }
            if (is_pointer_type(sym_table[with_field_idx].type) || (sym_table[with_field_idx].type) == TYPE_UNTYPED_POINTER) {
                compile_error(token.line, "readln into a pointer is not supported");
            }
            if (sym_table[with_field_idx].type == TYPE_SET) {
                compile_error(token.line, "readln into a set is not supported");
            }
            ASTNode *stmt = create_node(NODE_READLN);
            stmt->data.var_idx = with_field_idx;
            return stmt;
        }
    }

    {
        int rv_is_local, rv_record_type_idx;
        const int *rv_field_idx;
        if (find_any_record_var(token.text, &rv_is_local, &rv_record_type_idx, &rv_field_idx)) {
            char rec_name[MAX_NAME];
            strcpy(rec_name, token.text);
            match(TOKEN_IDENTIFIER);
            if (token.type != TOKEN_PERIOD) {
                compile_error(token.line, "'%s' is a record - readln expects a field, e.g. '%s.field'", rec_name, rec_name);
            }
            match(TOKEN_PERIOD);
            if (token.type != TOKEN_IDENTIFIER) {
                compile_error(token.line, "Expected a field name after '%s.'", rec_name);
            }
            int field_idx = find_record_field(rv_record_type_idx, token.text);
            if (field_idx == -1) {
                compile_error(token.line, "'%s' is not a field of '%s'", token.text, rec_name);
            }
            int resolved_idx = rv_field_idx[field_idx];
            match(TOKEN_IDENTIFIER);
            if (rv_is_local) {
                // Never an array (add_local_record() rejects an array
                // field) - only the enum check applies.
                if (current_locals[resolved_idx].type >= TYPE_ENUM_BASE && current_locals[resolved_idx].type < TYPE_POINTER_BASE) {
                    compile_error(token.line, "readln into an enumerated value is not supported");
                }
                if (is_pointer_type(current_locals[resolved_idx].type) || (current_locals[resolved_idx].type) == TYPE_UNTYPED_POINTER) {
                    compile_error(token.line, "readln into a pointer is not supported");
                }
                if (current_locals[resolved_idx].type == TYPE_SET) {
                    compile_error(token.line, "readln into a set is not supported");
                }
                ASTNode *stmt = create_node(NODE_LOCAL_READLN);
                stmt->data.var_idx = resolved_idx;
                stmt->expression_type = current_locals[resolved_idx].type;
                return stmt;
            }
            if (sym_table[resolved_idx].is_array) {
                compile_error(token.line, "readln into an array is not supported");
            }
            if (sym_table[resolved_idx].type >= TYPE_ENUM_BASE && sym_table[resolved_idx].type < TYPE_POINTER_BASE) {
                compile_error(token.line, "readln into an enumerated value is not supported");
            }
            if (is_pointer_type(sym_table[resolved_idx].type) || (sym_table[resolved_idx].type) == TYPE_UNTYPED_POINTER) {
                compile_error(token.line, "readln into a pointer is not supported");
            }
            if (sym_table[resolved_idx].type == TYPE_SET) {
                compile_error(token.line, "readln into a set is not supported");
            }
            ASTNode *stmt = create_node(NODE_READLN);
            stmt->data.var_idx = resolved_idx;
            return stmt;
        }
    }

    // NODE_LOCAL_READLN's own ->op already carries TOKEN_READ/TOKEN_READLN
    // (see parse_read_statement()), so it has nowhere to also carry a
    // levels_up tag - a readln target must be the CURRENT procedure's own
    // local, never an enclosing scope's (a documented known gap: read
    // into a local of the current procedure and assign it to the outer
    // one afterward instead). find_local_outward() (rather than plain
    // find_local()) is used only so this can be detected and reported
    // clearly, rather than silently falling through to "undefined
    // variable" below.
    {
        int outer_levels_up;
        if (find_local_outward(token.text, &outer_levels_up) != -1 && outer_levels_up > 0) {
            compile_error(token.line, "readln into an enclosing procedure's local '%s' is not supported yet - readln into one of this procedure's own locals instead, then assign it to '%s'", token.text, token.text);
        }
    }
    int local_idx = find_local(token.text);
    if (local_idx != -1) {
        if (current_locals[local_idx].is_var_param) {
            compile_error(token.line, "readln into a 'var' parameter is not supported yet - readln into a plain local/global instead, then assign it through the parameter");
        }
        if (current_locals[local_idx].is_static) {
            match(TOKEN_IDENTIFIER);
            int static_idx = current_locals[local_idx].static_sym_idx;
            if (sym_table[static_idx].type >= TYPE_ENUM_BASE && sym_table[static_idx].type < TYPE_POINTER_BASE) {
                compile_error(token.line, "readln into an enumerated value is not supported");
            }
            if (is_pointer_type(sym_table[static_idx].type) || (sym_table[static_idx].type) == TYPE_UNTYPED_POINTER) {
                compile_error(token.line, "readln into a pointer is not supported");
            }
            if (sym_table[static_idx].type == TYPE_SET) {
                compile_error(token.line, "readln into a set is not supported");
            }
            ASTNode *stmt = create_node(NODE_READLN);
            stmt->data.var_idx = static_idx;
            return stmt;
        }
        if (current_locals[local_idx].is_array || current_locals[local_idx].is_array_ref) {
            compile_error(token.line, "readln into an array is not supported");
        }
        if (current_locals[local_idx].type >= TYPE_ENUM_BASE && current_locals[local_idx].type < TYPE_POINTER_BASE) {
            compile_error(token.line, "readln into an enumerated value is not supported");
        }
        if (is_pointer_type(current_locals[local_idx].type) || (current_locals[local_idx].type) == TYPE_UNTYPED_POINTER) {
            compile_error(token.line, "readln into a pointer is not supported");
        }
        if (current_locals[local_idx].type == TYPE_SET) {
            compile_error(token.line, "readln into a set is not supported");
        }
        ASTNode *stmt = create_node(NODE_LOCAL_READLN);
        stmt->data.var_idx = local_idx;
        stmt->expression_type = current_locals[local_idx].type;
        match(TOKEN_IDENTIFIER);
        return stmt;
    }

    int readln_var_idx = find_var(token.text);
    if (sym_table[readln_var_idx].type >= TYPE_ENUM_BASE && sym_table[readln_var_idx].type < TYPE_POINTER_BASE) {
        compile_error(token.line, "readln into an enumerated value is not supported");
    }
    if (is_pointer_type(sym_table[readln_var_idx].type) || (sym_table[readln_var_idx].type) == TYPE_UNTYPED_POINTER) {
        compile_error(token.line, "readln into a pointer is not supported");
    }
    if (sym_table[readln_var_idx].type == TYPE_SET) {
        compile_error(token.line, "readln into a set is not supported");
    }
    ASTNode *stmt = create_node(NODE_READLN);
    stmt->data.var_idx = readln_var_idx;
    match(TOKEN_IDENTIFIER);
    return stmt;
}

// A read target's declared type - NODE_LOCAL_READLN always carries it
// directly (expression_type), but NODE_READLN (a global-backed target)
// doesn't: codegen looks it up via sym_table[] at codegen time instead,
// since that case dispatches on the symbol's RUNTIME type tag rather
// than anything baked in at parse time. Needed here (parse time) too,
// to detect the read-target-ordering bug parse_read_statement() works
// around below.
static DataType read_target_type(ASTNode *target) {
    if (target->type == NODE_LOCAL_READLN) return target->expression_type;
    return sym_table[target->data.var_idx].type;
}

// Recursively builds the chain of NODE_ASSIGN/NODE_LOCAL_ASSIGN nodes
// for a NESTED-record field of a typed-file read(f, X) - mirrors
// build_record_copy()'s own base+offset recursive walk exactly (same
// running 'offset' counter, same reasoning: a nested record's own
// fields are laid out contiguously right after dest_base, so no second
// field_idx_array is needed once recursing past the top level). The
// "source" side is a NODE_TYPED_FILE_READ_LEAF (one raw int read from
// the file) instead of another record's own field.
static void build_typed_file_read_chain(int record_type_idx, int file_sym_idx, int dest_is_local, int dest_base, int dest_levels_up, ASTNode **head, ASTNode **tail) {
    RecordTypeDef *rt = &record_types[record_type_idx];
    int offset = 0;
    for (int i = 0; i < rt->field_count; i++) {
        RecordField *f = &rt->fields[i];
        if (f->is_record) {
            build_typed_file_read_chain(f->record_type_idx, file_sym_idx, dest_is_local, dest_base + offset, dest_levels_up, head, tail);
        } else {
            ASTNode *leaf = create_node(NODE_TYPED_FILE_READ_LEAF);
            leaf->data.var_idx = file_sym_idx;
            leaf->expression_type = f->type;
            leaf->op = f->disk_width;
            // A typed-file read ingests untrusted external bytes -
            // unlike an ordinary same-type record copy (build_record_
            // copy(), whose source is already known-valid), a subrange-
            // typed destination leaf needs a genuine range check here.
            ASTNode *value = f->is_subrange ? wrap_range_check(leaf, 1, f->subrange_lower, f->subrange_upper) : leaf;
            ASTNode *assign = record_field_assign_node(dest_is_local, dest_base + offset, dest_levels_up, value);
            if (!*head) *head = assign; else (*tail)->next = assign;
            *tail = assign;
        }
        offset += f->is_record ? record_type_leaf_count(f->record_type_idx) : 1;
    }
}

// Write twin of build_typed_file_read_chain() above - one
// NODE_TYPED_FILE_WRITE_LEAF per leaf field, source value read via
// record_field_read_node() (an ordinary field read).
static void build_typed_file_write_chain(int record_type_idx, int file_sym_idx, int src_is_local, int src_base, int src_levels_up, ASTNode **head, ASTNode **tail) {
    RecordTypeDef *rt = &record_types[record_type_idx];
    int offset = 0;
    for (int i = 0; i < rt->field_count; i++) {
        RecordField *f = &rt->fields[i];
        if (f->is_record) {
            build_typed_file_write_chain(f->record_type_idx, file_sym_idx, src_is_local, src_base + offset, src_levels_up, head, tail);
        } else {
            ASTNode *value = record_field_read_node(src_is_local, src_base + offset, src_levels_up);
            ASTNode *leaf = create_node(NODE_TYPED_FILE_WRITE_LEAF);
            leaf->data.var_idx = file_sym_idx;
            leaf->left = value;
            leaf->expression_type = f->type;
            leaf->op = f->disk_width;
            if (!*head) *head = leaf; else (*tail)->next = leaf;
            *tail = leaf;
        }
        offset += f->is_record ? record_type_leaf_count(f->record_type_idx) : 1;
    }
}

// Resolves the single target of a typed-file read(f, X)/write(f, X) - X
// is either a whole record variable of the file's own record type, or
// (for a bare-scalar-element file) a plain scalar variable of the
// file's own scalar type - global or local, but never rec.field/arr[i]/
// a general expression (the v1 scope cut - see docs/LANGUAGE.md).
// Fills *is_local/*levels_up and either (*is_record=1) *record_type_idx
// plus *field_idx_array, or (*is_record=0) *scalar_idx - matching
// find_any_record_var_outward()'s own output shape for the record case,
// and find_local_outward()/find_var_soft_visible()'s for the scalar
// case. compile_error()s on any mismatch (not found, wrong kind, wrong
// type).
static void resolve_typed_file_target(TypedFileVarDef *tf, const char *stmt_name, int *is_local, int *levels_up, int *is_record, int *record_type_idx, const int **field_idx_array, int *scalar_idx) {
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "'%s' expects a plain variable name", stmt_name);
    }
    int rt_idx_found;
    if (find_any_record_var_outward(token.text, levels_up, is_local, &rt_idx_found, field_idx_array)) {
        if (!tf->is_record || rt_idx_found != tf->record_type_idx) {
            compile_error(token.line, "'%s' is the wrong type for this typed file", token.text);
        }
        *is_record = 1;
        *record_type_idx = rt_idx_found;
        match(TOKEN_IDENTIFIER);
        return;
    }
    int local_idx = find_local_outward(token.text, levels_up);
    if (local_idx != -1) {
        if (tf->is_record || local_at(local_idx, *levels_up)->type != tf->scalar_type) {
            compile_error(token.line, "'%s' is the wrong type for this typed file", token.text);
        }
        *is_local = 1;
        *is_record = 0;
        *scalar_idx = local_idx;
        match(TOKEN_IDENTIFIER);
        return;
    }
    int sym_idx = find_var_soft_visible(token.text);
    if (sym_idx == -1) {
        compile_error(token.line, "'%s' is not a declared variable", token.text);
    }
    if (tf->is_record || sym_table[sym_idx].type != tf->scalar_type) {
        compile_error(token.line, "'%s' is the wrong type for this typed file", token.text);
    }
    *is_local = 0;
    *is_record = 0;
    *scalar_idx = sym_idx;
    match(TOKEN_IDENTIFIER);
}

// 'read(f, X)', a typed file only - see resolve_typed_file_target()
// above for X's own restrictions. Builds one NODE_ASSIGN/NODE_LOCAL_
// ASSIGN per leaf field (or a single one, for a bare-scalar-element
// file), wrapped in a NODE_COMPOUND (matching whole-record assignment's
// own multi-node desugaring convention) only when there's more than one
// - a single target is returned bare, matching parse_read_statement()'s
// own convention just below.
static ASTNode *parse_typed_file_read(TypedFileVarDef *tf) {
    int is_local, levels_up, is_record, record_type_idx, scalar_idx = -1;
    const int *field_idx_array = NULL;
    resolve_typed_file_target(tf, "read", &is_local, &levels_up, &is_record, &record_type_idx, &field_idx_array, &scalar_idx);
    if (!is_record) {
        ASTNode *leaf = create_node(NODE_TYPED_FILE_READ_LEAF);
        leaf->data.var_idx = tf->sym_idx;
        leaf->expression_type = tf->scalar_type;
        leaf->op = tf->disk_width;
        return record_field_assign_node(is_local, scalar_idx, levels_up, leaf);
    }
    RecordTypeDef *rt = &record_types[record_type_idx];
    ASTNode *head = NULL, *tail = NULL;
    for (int i = 0; i < rt->field_count; i++) {
        RecordField *f = &rt->fields[i];
        if (f->is_record) {
            build_typed_file_read_chain(f->record_type_idx, tf->sym_idx, is_local, field_idx_array[i], levels_up, &head, &tail);
            continue;
        }
        ASTNode *leaf = create_node(NODE_TYPED_FILE_READ_LEAF);
        leaf->data.var_idx = tf->sym_idx;
        leaf->expression_type = f->type;
        leaf->op = f->disk_width;
        ASTNode *value = f->is_subrange ? wrap_range_check(leaf, 1, f->subrange_lower, f->subrange_upper) : leaf;
        ASTNode *assign = record_field_assign_node(is_local, field_idx_array[i], levels_up, value);
        if (!head) head = assign; else tail->next = assign;
        tail = assign;
    }
    ASTNode *compound = create_node(NODE_COMPOUND);
    compound->left = head; // NULL for a (degenerate) empty record
    return compound;
}

// Write twin of parse_typed_file_read() above.
static ASTNode *parse_typed_file_write(TypedFileVarDef *tf) {
    int is_local, levels_up, is_record, record_type_idx, scalar_idx = -1;
    const int *field_idx_array = NULL;
    resolve_typed_file_target(tf, "write", &is_local, &levels_up, &is_record, &record_type_idx, &field_idx_array, &scalar_idx);
    if (!is_record) {
        ASTNode *value = record_field_read_node(is_local, scalar_idx, levels_up);
        ASTNode *leaf = create_node(NODE_TYPED_FILE_WRITE_LEAF);
        leaf->data.var_idx = tf->sym_idx;
        leaf->left = value;
        leaf->expression_type = tf->scalar_type;
        leaf->op = tf->disk_width;
        return leaf;
    }
    RecordTypeDef *rt = &record_types[record_type_idx];
    ASTNode *head = NULL, *tail = NULL;
    for (int i = 0; i < rt->field_count; i++) {
        RecordField *f = &rt->fields[i];
        if (f->is_record) {
            build_typed_file_write_chain(f->record_type_idx, tf->sym_idx, is_local, field_idx_array[i], levels_up, &head, &tail);
            continue;
        }
        ASTNode *value = record_field_read_node(is_local, field_idx_array[i], levels_up);
        ASTNode *leaf = create_node(NODE_TYPED_FILE_WRITE_LEAF);
        leaf->data.var_idx = tf->sym_idx;
        leaf->left = value;
        leaf->expression_type = f->type;
        leaf->op = f->disk_width;
        if (!head) head = leaf; else tail->next = leaf;
        tail = leaf;
    }
    ASTNode *compound = create_node(NODE_COMPOUND);
    compound->left = head;
    return compound;
}

// Resolves 'name' to a 1D array's underlying GLOBAL sym_table[] index,
// element type, and declared bounds - for BlockRead/BlockWrite's own
// array-argument resolution below. Checks a LOCAL array first (a local
// array is already a hidden mangled GLOBAL under the hood - see
// LocalSymbol.array_sym_idx's own comment - so both cases end up
// resolving to a plain global index either way, with no levels_up
// indirection ever needed), falling back to an ordinary global. Returns
// 0 (and leaves every out-param untouched) if 'name' isn't a declared
// 1D array at all - the caller reports its own specific error message.
static int resolve_blockio_array(const char *name, int *out_sym_idx, DataType *out_elem_type,
                                  int *out_lower, int *out_upper) {
    int levels_up;
    int local_idx = find_local_outward(name, &levels_up);
    if (local_idx != -1) {
        LocalSymbol *ls = local_at(local_idx, levels_up);
        if (!ls->is_array || ls->is_2d || ls->is_nd) return 0;
        *out_sym_idx = ls->array_sym_idx;
        *out_elem_type = ls->type;
        *out_lower = ls->array_lower;
        *out_upper = ls->array_upper;
        return 1;
    }
    int gidx = find_var_soft_visible(name);
    if (gidx == -1 || !sym_table[gidx].is_array || sym_table[gidx].is_2d || sym_table[gidx].is_nd) return 0;
    *out_sym_idx = gidx;
    *out_elem_type = sym_table[gidx].type;
    *out_lower = sym_table[gidx].array_lower;
    *out_upper = sym_table[gidx].array_upper;
    return 1;
}

// 'BlockRead(f, arr, count)' / 'BlockWrite(f, arr, count)' - an untyped
// file only (TYPE_UNTYPED_FILE). Desugars entirely into a synthesized
// NODE_FOR loop this compiler already knows how to generate - no new
// NodeType, no new opcode - mirroring parse_for_in_tail_global()'s own
// array-loop desugaring exactly:
//
//     __blockread_idxN := 0;
//     for __blockread_idxN := 0 to count - 1 do
//         arr[arr_lower + __blockread_idxN] := <one raw value read from f>;
//
// (BlockWrite's body is the write-leaf twin, reading arr[...] instead of
// assigning to it.) 'count' is a general runtime expression - unlike a
// record's always-static field count, this can't be unrolled at compile
// time - but array bounds-checking comes entirely free this way: the
// loop body is an ORDINARY array-element assignment/read, running
// through this VM's existing runtime bounds check like any other one,
// so a 'count' too large for arr's own declared bounds already produces
// this VM's standard "Array index out of range" error with no extra
// code needed here.
//
// v1 scope: every element always transfers as a full 4-byte value,
// regardless of the array's own declared subrange bounds - this
// compiler has no per-ARRAY equivalent of RecordField.disk_width/
// TypedFileVarDef.disk_width to distinguish an array of literal 'byte'
// from an ordinary hand-written '0..255' subrange (Symbol only tracks
// is_subrange/subrange_lower/subrange_upper, not which keyword produced
// them), so narrower on-disk transfer for a byte/shortint/word array
// element isn't supported yet (see docs/LANGUAGE.md) - a documented
// gap, not a silent one.
static ASTNode *parse_block_read_write(int is_read) {
    int line = token.line;
    match(is_read ? TOKEN_BLOCKREAD : TOKEN_BLOCKWRITE);
    match(TOKEN_LPAREN);

    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "'%s' expects an untyped file variable", is_read ? "BlockRead" : "BlockWrite");
    }
    int fidx = find_file_var_soft(token.text);
    if (fidx == -1 || sym_table[fidx].type != TYPE_UNTYPED_FILE) {
        compile_error(token.line, "'%s' is not an untyped file variable", token.text);
    }
    match(TOKEN_IDENTIFIER);
    match(TOKEN_COMMA);

    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "'%s' expects an array variable", is_read ? "BlockRead" : "BlockWrite");
    }
    char arr_name[MAX_NAME];
    strcpy(arr_name, token.text);
    int arr_sym_idx, arr_lower, arr_upper;
    DataType elem_type;
    if (!resolve_blockio_array(arr_name, &arr_sym_idx, &elem_type, &arr_lower, &arr_upper)) {
        compile_error(token.line, "'%s' must be a declared 1D array", arr_name);
    }
    if (sym_table[arr_sym_idx].is_record_array) {
        // resolve_blockio_array() doesn't reject this itself - a record-
        // element array still has is_array set, just with a meaningless
        // placeholder 'type' (TYPE_INTEGER - see is_record_array's own
        // comment in common.h), which is_typed_file_safe_scalar() below
        // would otherwise wrongly accept as "array of integer".
        compile_error(token.line, "'%s' array element type must be integer, real, boolean, an enumerated type, "
                       "a subrange, or a set - not a record", is_read ? "BlockRead" : "BlockWrite");
    }
    if (!is_typed_file_safe_scalar(elem_type)) {
        compile_error(token.line, "'%s' array element type must be integer, real, boolean, an enumerated type, "
                       "a subrange, or a set", is_read ? "BlockRead" : "BlockWrite");
    }
    int elem_is_subrange = sym_table[arr_sym_idx].is_subrange;
    int elem_subrange_lower = sym_table[arr_sym_idx].subrange_lower;
    int elem_subrange_upper = sym_table[arr_sym_idx].subrange_upper;
    match(TOKEN_IDENTIFIER);
    match(TOKEN_COMMA);
    ASTNode *count_expr = expression();
    match(TOKEN_RPAREN);

    char idx_name[MAX_NAME];
    snprintf(idx_name, MAX_NAME, "__blockio_idx%d", sym_count);
    int idx_sym_idx = sym_count;
    add_var(idx_name, TYPE_INTEGER);

    ASTNode *for_node = create_node(NODE_FOR);
    for_node->line = line;
    for_node->data.var_idx = idx_sym_idx;
    for_node->op = TOKEN_TO;
    ASTNode *lo = create_node(NODE_NUMBER);
    lo->data.num_value = 0;
    lo->expression_type = TYPE_INTEGER;
    for_node->left = lo;
    ASTNode *one = create_node(NODE_NUMBER);
    one->data.num_value = 1;
    one->expression_type = TYPE_INTEGER;
    ASTNode *hi = create_node(NODE_BINARY_OP);
    hi->op = TOKEN_MINUS;
    hi->left = count_expr;
    hi->right = one;
    hi->expression_type = TYPE_INTEGER;
    for_node->right = hi;

    ASTNode *idx_read = create_node(NODE_VARIABLE);
    idx_read->data.var_idx = idx_sym_idx;
    idx_read->expression_type = TYPE_INTEGER;

    ASTNode *index_expr = idx_read;
    if (arr_lower != 0) {
        ASTNode *lower_lit = create_node(NODE_NUMBER);
        lower_lit->data.num_value = arr_lower;
        lower_lit->expression_type = TYPE_INTEGER;
        index_expr = create_node(NODE_BINARY_OP);
        index_expr->op = TOKEN_PLUS;
        index_expr->left = idx_read;
        index_expr->right = lower_lit;
        index_expr->expression_type = TYPE_INTEGER;
    }
    (void)arr_upper; // bounds enforcement comes from the array's own runtime check, not a compile-time comparison here

    ASTNode *body_stmt;
    if (is_read) {
        ASTNode *leaf = create_node(NODE_TYPED_FILE_READ_LEAF);
        leaf->line = line;
        leaf->data.var_idx = fidx;
        leaf->expression_type = elem_type;
        leaf->op = (TokenType)0; // always full-width - see this function's own v1 scope comment above
        ASTNode *assign = create_node(NODE_ASSIGN);
        assign->line = line;
        assign->data.var_idx = arr_sym_idx;
        assign->left = index_expr;
        assign->right = wrap_range_check(leaf, elem_is_subrange, elem_subrange_lower, elem_subrange_upper);
        assign->expression_type = elem_type;
        body_stmt = assign;
    } else {
        ASTNode *elem_read = create_node(NODE_ARRAY_ACCESS);
        elem_read->line = line;
        elem_read->data.var_idx = arr_sym_idx;
        elem_read->left = index_expr;
        elem_read->expression_type = elem_type;
        ASTNode *leaf = create_node(NODE_TYPED_FILE_WRITE_LEAF);
        leaf->line = line;
        leaf->data.var_idx = fidx;
        leaf->left = elem_read;
        leaf->expression_type = elem_type;
        leaf->op = (TokenType)0;
        body_stmt = leaf;
    }

    ASTNode *body = create_node(NODE_COMPOUND);
    body->left = body_stmt;
    for_node->extra = body;

    return for_node;
}

// 'read(a[, b, c...])' / 'readln(a[, b, c...])' - a comma-separated list
// of read targets (see parse_read_target() above). 'read' and 'readln'
// differ only in whether the LAST target consumes the rest of the input
// line afterward: 'read(a, b, c)' desugars to 'read(a); read(b);
// read(c)' (none of them flush), while 'readln(a, b, c)' desugars to
// 'read(a); read(b); readln(c)' (only the last one flushes) - matching
// real Pascal's readln semantics exactly. Each target node's ->op is set
// to TOKEN_READ or TOKEN_READLN accordingly (reused exactly like
// NODE_WRITELN already reuses ->op for TOKEN_WRITE vs TOKEN_WRITELN) -
// that's what codegen dispatches on to pick the flushing or non-flushing
// opcode variant.
// A single target - the overwhelmingly common case, and the only form
// this compiler supported before multi-target reads existed - is
// returned bare, unwrapped, exactly as before: existing single-target
// programs compile to identical bytecode. Two or more targets are
// chained via each node's own ->next and wrapped in one NODE_COMPOUND
// (the same trick whole-record assignment already uses for its own
// multi-node desugaring), since statement_list() manages ->next itself
// for the ENCLOSING statement sequence and would otherwise silently
// overwrite this chain's own links.
static ASTNode *parse_read_statement(int is_readln) {
    match(is_readln ? TOKEN_READLN : TOKEN_READ);
    match(TOKEN_LPAREN);

    // An optional leading file variable - 'read(f, a, b)' reads from f
    // instead of stdin. Detected via a soft lookup (not every
    // identifier here is one - the overwhelmingly common case is a
    // bare read target, so this must never mistake an ordinary first
    // target for a file), consumed together with the comma that must
    // follow it, before the ordinary target-parsing loop below even
    // starts - every target built by that loop gets the same file
    // reference attached (see the loop body).
    int file_sym_idx = -1;
    if (token.type == TOKEN_IDENTIFIER) {
        int fidx = find_file_var_soft(token.text);
        if (fidx != -1) {
            if (sym_table[fidx].type == TYPE_TYPED_FILE) {
                // A typed file read is a GENUINELY separate mechanism
                // (raw binary transfer, single target only - see
                // parse_typed_file_read()) - branch off entirely rather
                // than falling into the multi-target text-read loop
                // below, which doesn't apply to it at all.
                if (is_readln) {
                    compile_error(token.line, "'readln' doesn't apply to a typed file - use 'read' instead");
                }
                match(TOKEN_IDENTIFIER);
                match(TOKEN_COMMA);
                ASTNode *result = parse_typed_file_read(&typed_file_vars[find_typed_file_var(fidx)]);
                match(TOKEN_RPAREN);
                return result;
            }
            if (sym_table[fidx].type == TYPE_UNTYPED_FILE) {
                // Plain 'read'/'readln' don't apply to an untyped file at
                // all (no fixed record shape to transfer, no text to
                // parse) - use 'BlockRead' instead. Rejected explicitly
                // here rather than silently falling into the text-read
                // loop below, which would otherwise misinterpret this as
                // an ordinary text file target.
                compile_error(token.line, "'%s' doesn't apply to an untyped file - use 'BlockRead' instead",
                               is_readln ? "readln" : "read");
            }
            file_sym_idx = fidx;
            match(TOKEN_IDENTIFIER);
            if (token.type != TOKEN_COMMA) {
                compile_error(token.line, "'%s(%s, ...)' expects at least one target after the file variable",
                               is_readln ? "readln" : "read", sym_table[fidx].name);
            }
            match(TOKEN_COMMA);
        }
    }

    ASTNode *head = NULL;
    ASTNode *tail = NULL;
    while (1) {
        ASTNode *target = parse_read_target();
        target->op = TOKEN_READ; // provisional - the last one is fixed up below if this is a 'readln'
        if (file_sym_idx != -1) {
            ASTNode *file_ref = create_node(NODE_VARIABLE);
            file_ref->data.var_idx = file_sym_idx;
            file_ref->expression_type = TYPE_FILE;
            target->extra = file_ref;
        }
        // Bug workaround: a non-flushing numeric/boolean read (scanf/
        // fscanf-based, which only skips LEADING whitespace, not
        // trailing) leaves the read position sitting right before that
        // value's own trailing newline - not at the start of the next
        // line. A string/char target immediately after one (in this
        // same target list - 'tail' here is always non-last by
        // construction, since we're about to chain another target onto
        // it) reads via fgets() instead, which does NOT skip leading
        // whitespace/newlines the way scanf does, so it would
        // immediately hit that leftover newline and read an empty
        // "line" rather than the intended next one. Mark the target
        // (via ->left, otherwise unused on NODE_READLN/NODE_LOCAL_READLN -
        // see codegen.c) to skip exactly that one leftover newline
        // first. Every OTHER target-type transition is unaffected:
        // scanf/fscanf already skips leading whitespace/newlines on its
        // own for the next numeric/boolean read, and a string/char
        // target's own fgets() always consumes through its line's
        // newline, so the target right after IT never has this problem
        // either way.
        if (tail) {
            DataType prev_t = read_target_type(tail);
            DataType cur_t = read_target_type(target);
            int prev_numeric = (prev_t == TYPE_INTEGER || prev_t == TYPE_REAL || prev_t == TYPE_BOOLEAN);
            int cur_stringy = (cur_t == TYPE_STRING || cur_t == TYPE_CHAR);
            if (prev_numeric && cur_stringy) {
                target->left = create_node(NODE_NUMBER); // dummy marker - see comment above
            }
        }
        if (!head) head = target; else tail->next = target;
        tail = target;
        if (token.type == TOKEN_COMMA) { match(TOKEN_COMMA); continue; }
        break;
    }
    match(TOKEN_RPAREN);

    if (is_readln) {
        tail->op = TOKEN_READLN; // only the last target flushes
    }

    if (head == tail) {
        return head;
    }
    ASTNode *compound = create_node(NODE_COMPOUND);
    compound->left = head;
    return compound;
}

// True for every token that can legally start a statement. Used by
// statement_list() to know when to stop (hitting END/ELSE/UNTIL, or EOF
// on a malformed file, all correctly fail this check).
static int is_statement_start(TokenType t) {
    return t == TOKEN_IDENTIFIER || t == TOKEN_WRITELN || t == TOKEN_WRITE || t == TOKEN_READLN || t == TOKEN_READ ||
           t == TOKEN_IF || t == TOKEN_WHILE || t == TOKEN_REPEAT || t == TOKEN_FOR || t == TOKEN_BEGIN ||
           t == TOKEN_BREAK || t == TOKEN_CONTINUE || t == TOKEN_INC || t == TOKEN_DEC || t == TOKEN_WITH ||
           t == TOKEN_ASSERT || t == TOKEN_CASE || t == TOKEN_GOTO ||
           t == TOKEN_FILE_ASSIGN || t == TOKEN_RESET || t == TOKEN_REWRITE || t == TOKEN_CLOSE || t == TOKEN_SEEK ||
           t == TOKEN_BLOCKREAD || t == TOKEN_BLOCKWRITE ||
           t == TOKEN_NEW || t == TOKEN_DISPOSE || t == TOKEN_INHERITED ||
           t == TOKEN_TRY || t == TOKEN_RAISE || t == TOKEN_WARNING || t == TOKEN_RANDOMIZE ||
           t == TOKEN_DELETE || t == TOKEN_INSERT || t == TOKEN_SETLENGTH || t == TOKEN_EXIT || t == TOKEN_HALT ||
           t == TOKEN_NUMBER; // a bare integer literal never starts any OTHER
                              // statement - it can only be a 'N: statement'
                              // label prefix (see statement()) - so this is
                              // unambiguous.
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

    int local_idx = -1;
    int levels_up = 0;
    int is_local = 0;
    int is_var_param = 0;
    int global_idx = -1;
    DataType target_type;
    int target_is_subrange = 0, target_subrange_lower = 0, target_subrange_upper = 0;

    int with_field_idx = find_with_field(name);
    int rv_levels_up = 0, rv_is_local = 0, rv_record_type_idx = 0;
    const int *rv_field_idx_arr = NULL;
    int rv_found = (with_field_idx == -1) && find_any_record_var_outward(name, &rv_levels_up, &rv_is_local, &rv_record_type_idx, &rv_field_idx_arr);
    int static_levels_up = 0;
    int static_local_idx = (with_field_idx == -1 && !rv_found) ? find_local_outward(name, &static_levels_up) : -1;
    if (static_local_idx != -1 && !local_at(static_local_idx, static_levels_up)->is_static) static_local_idx = -1;
    if (with_field_idx != -1) {
        global_idx = with_field_idx;
        if (sym_table[global_idx].is_array) {
            compile_error(line, "'%s' expects a plain integer variable, not an array", name_str);
        }
        target_type = sym_table[global_idx].type;
        target_is_subrange = sym_table[global_idx].is_subrange;
        target_subrange_lower = sym_table[global_idx].subrange_lower;
        target_subrange_upper = sym_table[global_idx].subrange_upper;
    } else if (rv_found) {
        if (token.type != TOKEN_PERIOD) {
            compile_error(token.line, "'%s' is a record - '%s' expects a field, e.g. '%s.field'", name, name_str, name);
        }
        match(TOKEN_PERIOD);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "Expected a field name after '%s.'", name);
        }
        int field_idx = find_record_field(rv_record_type_idx, token.text);
        if (field_idx == -1) {
            compile_error(token.line, "'%s' is not a field of '%s'", token.text, name);
        }
        int resolved_idx = rv_field_idx_arr[field_idx];
        match(TOKEN_IDENTIFIER);
        if (rv_is_local) {
            is_local = 1;
            local_idx = resolved_idx;
            levels_up = rv_levels_up;
            LocalSymbol *ls = local_at(local_idx, levels_up);
            target_type = ls->type;
            target_is_subrange = ls->is_subrange;
            target_subrange_lower = ls->subrange_lower;
            target_subrange_upper = ls->subrange_upper;
        } else {
            global_idx = resolved_idx;
            if (sym_table[global_idx].is_array) {
                compile_error(line, "'%s' expects a plain integer variable, not an array", name_str);
            }
            target_type = sym_table[global_idx].type;
            target_is_subrange = sym_table[global_idx].is_subrange;
            target_subrange_lower = sym_table[global_idx].subrange_lower;
            target_subrange_upper = sym_table[global_idx].subrange_upper;
        }
    } else if (static_local_idx != -1) {
        global_idx = local_at(static_local_idx, static_levels_up)->static_sym_idx;
        if (sym_table[global_idx].is_array) {
            compile_error(line, "'%s' expects a plain integer variable, not an array", name_str);
        }
        target_type = sym_table[global_idx].type;
        target_is_subrange = sym_table[global_idx].is_subrange;
        target_subrange_lower = sym_table[global_idx].subrange_lower;
        target_subrange_upper = sym_table[global_idx].subrange_upper;
    } else {
        local_idx = find_local_outward(name, &levels_up);
        is_local = (local_idx != -1 && !local_at(local_idx, levels_up)->is_array && !local_at(local_idx, levels_up)->is_array_ref);
        if (local_idx != -1 && !is_local) {
            compile_error(line, "'%s' expects a plain integer variable, not an array", name_str);
        }
        if (is_local) {
            LocalSymbol *ls = local_at(local_idx, levels_up);
            target_type = ls->type;
            target_is_subrange = ls->is_subrange;
            target_subrange_lower = ls->subrange_lower;
            target_subrange_upper = ls->subrange_upper;
            // is_var_param is set below, after target_type's own integer
            // check - a 'var' parameter's read/write node kind is decided
            // separately from is_local (see the read_node/write_node
            // construction below), but the type/subrange info above is
            // identical either way.
        } else {
            global_idx = find_var(name);
            if (sym_table[global_idx].is_array) {
                compile_error(line, "'%s' expects a plain integer variable, not an array", name_str);
            }
            target_type = sym_table[global_idx].type;
            target_is_subrange = sym_table[global_idx].is_subrange;
            target_subrange_lower = sym_table[global_idx].subrange_lower;
            target_subrange_upper = sym_table[global_idx].subrange_upper;
        }
    }
    if (target_type != TYPE_INTEGER) {
        compile_error(line, "'%s' only supports integer variables", name_str);
    }
    if (is_local && local_at(local_idx, levels_up)->is_var_param) {
        is_var_param = 1;
        if (local_at(local_idx, levels_up)->is_const_param) {
            compile_error(line, "'%s' cannot be used on 'const' parameter '%s'", name_str, name);
        }
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
    if (is_var_param) {
        read_node = create_node(NODE_VAR_PARAM_READ);
        read_node->data.var_idx = local_idx;
        read_node->op = (TokenType)levels_up;
        read_node->expression_type = TYPE_INTEGER;
    } else if (is_local) {
        read_node = create_node(NODE_LOCAL_VAR);
        read_node->data.var_idx = local_idx;
        read_node->op = (TokenType)levels_up;
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
    if (is_var_param) {
        write_node = create_node(NODE_VAR_PARAM_ASSIGN);
        write_node->data.var_idx = local_idx;
        write_node->op = (TokenType)levels_up;
        write_node->expression_type = TYPE_INTEGER;
    } else if (is_local) {
        write_node = create_node(NODE_LOCAL_ASSIGN);
        write_node->data.var_idx = local_idx;
        write_node->op = (TokenType)levels_up;
        write_node->expression_type = TYPE_INTEGER;
    } else {
        write_node = create_node(NODE_ASSIGN);
        write_node->data.var_idx = global_idx;
    }
    write_node->left = wrap_range_check(value_node, target_is_subrange, target_subrange_lower, target_subrange_upper);
    return write_node;
}

// Resolves a plain 'string' variable as Delete/Insert's write-back
// target - global, local/parameter, or 'var' parameter (not 'const'/
// 'out', not a with-field/record-field/static-local - a deliberate v1
// scope cut narrower than parse_inc_dec()'s own resolution chain above,
// documented in docs/LANGUAGE.md, not a silent gap: the overwhelming
// majority of real use is a plain string variable, and skipping the
// record/with-field branches keeps this proportionate to the feature
// rather than a near-duplicate of parse_inc_dec()'s full chain).
// Returns the correct READ node kind (NODE_VAR_PARAM_READ/
// NODE_LOCAL_VAR/NODE_VARIABLE) - see string_writeback_assign_node()
// below for the matching write side.
static ASTNode *parse_string_writeback_target(const char *builtin_name) {
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "'%s' expects a string variable", builtin_name);
    }
    char name[MAX_NAME];
    strcpy(name, token.text);
    int line = token.line;
    int levels_up;
    int local_idx = find_local_outward(name, &levels_up);
    if (local_idx != -1) {
        LocalSymbol *ls = local_at(local_idx, levels_up);
        if (ls->is_array || ls->is_array_ref) {
            compile_error(line, "'%s' expects a plain string variable, not an array", builtin_name);
        }
        if (ls->is_const_param) {
            compile_error(line, "'%s' cannot be used on 'const' parameter '%s'", builtin_name, name);
        }
        if (ls->type != TYPE_STRING) {
            compile_error(line, "'%s' expects a string variable", builtin_name);
        }
        match(TOKEN_IDENTIFIER);
        ASTNode *node = create_node(ls->is_var_param ? NODE_VAR_PARAM_READ : NODE_LOCAL_VAR);
        node->data.var_idx = local_idx;
        node->op = (TokenType)levels_up;
        node->expression_type = TYPE_STRING;
        return node;
    }
    int sym_idx = find_var(name);
    if (sym_table[sym_idx].is_array) {
        compile_error(line, "'%s' expects a plain string variable, not an array", builtin_name);
    }
    if (sym_table[sym_idx].type != TYPE_STRING) {
        compile_error(line, "'%s' expects a string variable", builtin_name);
    }
    match(TOKEN_IDENTIFIER);
    ASTNode *node = create_node(NODE_VARIABLE);
    node->data.var_idx = sym_idx;
    node->expression_type = TYPE_STRING;
    return node;
}

// Builds the matching write-back node for whatever
// parse_string_writeback_target() returned as a read - mirrors
// parse_inc_dec()'s own 3-way write_node dispatch above. Safe to call
// with read_node reused as-is elsewhere too (e.g. spliced into a
// NODE_BUILTIN_CALL's own left/right as the "read" operand) - this
// function only reads read_node's type/data.var_idx/op FIELDS to build
// a brand new, separate write node; it never attaches read_node itself
// as a second parent's child, so there's no shared-subtree hazard.
static ASTNode *string_writeback_assign_node(ASTNode *read_node, ASTNode *value) {
    NodeType write_type = read_node->type == NODE_VAR_PARAM_READ ? NODE_VAR_PARAM_ASSIGN
                         : read_node->type == NODE_LOCAL_VAR ? NODE_LOCAL_ASSIGN
                         : NODE_ASSIGN;
    ASTNode *node = create_node(write_type);
    node->data.var_idx = read_node->data.var_idx;
    if (write_type != NODE_ASSIGN) {
        node->op = read_node->op; // levels_up
        node->expression_type = TYPE_STRING;
    }
    node->left = value;
    return node;
}

// SetLength(arr, n)'s own target resolution - mirrors
// parse_string_writeback_target() exactly, for a dynamic array instead
// of a string.
static ASTNode *parse_dynarray_writeback_target(void) {
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "'SetLength' expects a dynamic array variable");
    }
    char name[MAX_NAME];
    strcpy(name, token.text);
    int line = token.line;
    int levels_up;
    int local_idx = find_local_outward(name, &levels_up);
    if (local_idx != -1) {
        LocalSymbol *ls = local_at(local_idx, levels_up);
        if (ls->is_const_param) {
            compile_error(line, "'SetLength' cannot be used on 'const' parameter '%s'", name);
        }
        if (!is_dynarray_type(ls->type)) {
            compile_error(line, "'SetLength' expects a dynamic array variable, not '%s'", name);
        }
        match(TOKEN_IDENTIFIER);
        ASTNode *node = create_node(ls->is_var_param ? NODE_VAR_PARAM_READ : NODE_LOCAL_VAR);
        node->data.var_idx = local_idx;
        node->op = (TokenType)levels_up;
        node->expression_type = ls->type;
        return node;
    }
    int sym_idx = find_var(name);
    if (!is_dynarray_type(sym_table[sym_idx].type)) {
        compile_error(line, "'SetLength' expects a dynamic array variable, not '%s'", name);
    }
    if (sym_table[sym_idx].is_const) {
        compile_error(line, "'SetLength' cannot be used on constant '%s'", name);
    }
    match(TOKEN_IDENTIFIER);
    ASTNode *node = create_node(NODE_VARIABLE);
    node->data.var_idx = sym_idx;
    node->expression_type = sym_table[sym_idx].type;
    return node;
}

// Builds the matching write-back node for whatever
// parse_dynarray_writeback_target() returned as a read - mirrors
// string_writeback_assign_node() exactly, except expression_type comes
// from read_node itself (already the dynamic array's own encoded type)
// rather than a hardcoded TYPE_STRING.
static ASTNode *dynarray_writeback_assign_node(ASTNode *read_node, ASTNode *value) {
    NodeType write_type = read_node->type == NODE_VAR_PARAM_READ ? NODE_VAR_PARAM_ASSIGN
                         : read_node->type == NODE_LOCAL_VAR ? NODE_LOCAL_ASSIGN
                         : NODE_ASSIGN;
    ASTNode *node = create_node(write_type);
    node->data.var_idx = read_node->data.var_idx;
    if (write_type != NODE_ASSIGN) {
        node->op = read_node->op; // levels_up
        node->expression_type = read_node->expression_type;
    }
    node->left = value;
    return node;
}

// Builds a '<target>^[0] := class_ptr_idx;' statement
// (NODE_HEAP_FIELD_ASSIGN) writing a freshly-allocated class instance's
// hidden runtime type tag at heap offset 0 - see
// parse_class_declaration()'s target_elem_size comment and
// resolve_heap_deref_step()'s +1 field-offset shift. This is pure
// write-only infrastructure for now: nothing reads it back yet (that's
// a later step - virtual/dynamic dispatch). 'target_read' must be a
// FRESH read expression (a new node, not one already spliced in
// elsewhere in the AST) evaluating to the just-allocated pointer value;
// this function takes ownership of it as the new statement's own left
// child.
static ASTNode *build_class_tag_write(ASTNode *target_read, int class_ptr_idx, int line) {
    ASTNode *stmt = create_node(NODE_HEAP_FIELD_ASSIGN);
    stmt->line = line;
    stmt->left = target_read;
    ASTNode *tag_value = create_node(NODE_NUMBER);
    tag_value->data.num_value = class_ptr_idx;
    tag_value->expression_type = TYPE_INTEGER;
    stmt->right = tag_value;
    ASTNode *offset_lit = create_node(NODE_NUMBER);
    offset_lit->data.num_value = 0;
    offset_lit->expression_type = TYPE_INTEGER;
    stmt->extra = offset_lit;
    stmt->expression_type = TYPE_INTEGER;
    return stmt;
}

// Wraps 'first' and 'second' (first->next left NULL) in a NODE_COMPOUND
// so a caller that must return exactly ONE ASTNode for what's
// syntactically one statement (see NODE_COMPOUND's own comment in
// common.h) can chain two statements together - used below when new()
// on a class-typed target needs both the ordinary allocation-assignment
// AND the tag-write from build_class_tag_write() above.
static ASTNode *chain_two_statements(ASTNode *first, ASTNode *second) {
    first->next = second;
    ASTNode *compound = create_node(NODE_COMPOUND);
    compound->left = first;
    return compound;
}

// 'new(X)' - X resolves exactly the same variety parse_inc_dec() above
// already resolves for a plain integer target (a with-field, a record
// field - global or local, a static local, or a plain local/parameter/
// global), OPTIONALLY followed by a '^' dereference chain ('new(head^.
// next);' - the pointer field of whatever record that chain reaches).
// Desugars at parse time into 'X := <fresh heap allocation>;' either
// way - straight through whichever assignment node kind X's own
// resolution already builds (NODE_ASSIGN/NODE_LOCAL_ASSIGN/NODE_VAR_
// PARAM_ASSIGN for the plain case, NODE_HEAP_FIELD_ASSIGN for the '^'
// case, via parse_heap_deref_write() - the same helper ordinary 'p^.
// field := value;' assignment statements already use) - reusing 100% of
// existing assignment infrastructure, the same way inc/dec above reuses
// it for '+'/'-'.
static ASTNode *parse_new_statement(void) {
    match(TOKEN_NEW);
    match(TOKEN_LPAREN);
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "'new' expects a pointer variable");
    }
    char name[MAX_NAME];
    int line = token.line;
    strcpy(name, token.text);
    match(TOKEN_IDENTIFIER);

    int local_idx = -1;
    int levels_up = 0;
    int is_local = 0;
    int is_var_param = 0;
    int global_idx = -1;
    DataType target_type;

    int with_field_idx = find_with_field(name);
    int rv_levels_up = 0, rv_is_local = 0, rv_record_type_idx = 0;
    const int *rv_field_idx_arr = NULL;
    int rv_found = (with_field_idx == -1) && find_any_record_var_outward(name, &rv_levels_up, &rv_is_local, &rv_record_type_idx, &rv_field_idx_arr);
    int static_levels_up = 0;
    int static_local_idx = (with_field_idx == -1 && !rv_found) ? find_local_outward(name, &static_levels_up) : -1;
    if (static_local_idx != -1 && !local_at(static_local_idx, static_levels_up)->is_static) static_local_idx = -1;
    if (with_field_idx != -1) {
        global_idx = with_field_idx;
        target_type = sym_table[global_idx].type;
    } else if (rv_found) {
        if (token.type != TOKEN_PERIOD) {
            compile_error(token.line, "'%s' is a record - 'new' expects a field, e.g. '%s.field'", name, name);
        }
        match(TOKEN_PERIOD);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "Expected a field name after '%s.'", name);
        }
        int field_idx = find_record_field(rv_record_type_idx, token.text);
        if (field_idx == -1) {
            compile_error(token.line, "'%s' is not a field of '%s'", token.text, name);
        }
        int resolved_idx = rv_field_idx_arr[field_idx];
        match(TOKEN_IDENTIFIER);
        if (rv_is_local) {
            is_local = 1;
            local_idx = resolved_idx;
            levels_up = rv_levels_up;
            target_type = local_at(local_idx, levels_up)->type;
        } else {
            global_idx = resolved_idx;
            target_type = sym_table[global_idx].type;
        }
    } else if (static_local_idx != -1) {
        global_idx = local_at(static_local_idx, static_levels_up)->static_sym_idx;
        target_type = sym_table[global_idx].type;
    } else {
        local_idx = find_local_outward(name, &levels_up);
        is_local = (local_idx != -1 && !local_at(local_idx, levels_up)->is_array && !local_at(local_idx, levels_up)->is_array_ref);
        if (local_idx != -1 && !is_local) {
            compile_error(line, "'new' expects a plain pointer variable, not an array");
        }
        if (is_local) {
            target_type = local_at(local_idx, levels_up)->type;
        } else {
            global_idx = find_var(name);
            target_type = sym_table[global_idx].type;
        }
    }
    if (!is_pointer_type(target_type)) {
        compile_error(line, "'new' expects a pointer variable");
    }
    if (is_local && local_at(local_idx, levels_up)->is_var_param) {
        is_var_param = 1;
    }

    if (token.type == TOKEN_CARET) {
        // 'new(head^.next);' - X itself is a plain pointer variable, but
        // a '^' chain follows: allocate into whatever field that chain
        // finally reaches, not into X itself. Build a READ node for X
        // (parse_heap_deref_write() walks the chain by reading its way
        // down to the last step - see its own comment), then let it
        // resolve the rest exactly like an ordinary 'X^...^.field :=
        // value;' assignment statement would.
        ASTNode *base;
        if (is_var_param) {
            base = create_node(NODE_VAR_PARAM_READ);
            base->data.var_idx = local_idx;
            base->op = (TokenType)levels_up;
            base->expression_type = target_type;
        } else if (is_local) {
            base = create_node(NODE_LOCAL_VAR);
            base->data.var_idx = local_idx;
            base->op = (TokenType)levels_up;
            base->expression_type = target_type;
        } else {
            base = create_node(NODE_VARIABLE);
            base->data.var_idx = global_idx;
            base->expression_type = target_type;
        }
        HeapDerefStep step;
        base = parse_heap_deref_write(base, line, &step);
        if (step.is_method_call || step.is_property_setter || !is_pointer_type(step.result_type)) {
            compile_error(line, "'new' expects a pointer target");
        }
        if (pointer_types[step.result_type - TYPE_POINTER_BASE].is_class) {
            // Tagging a class instance needs a FRESH read of wherever it
            // just landed (see build_class_tag_write()'s own comment) -
            // for the plain-variable case below, that's cheap (build a
            // brand new NODE_VARIABLE/NODE_LOCAL_VAR/NODE_VAR_PARAM_READ),
            // but here 'base' can be an arbitrary '^'-chain expression
            // (e.g. 'head^.next'), and reusing that SAME already-built
            // node a second time - rather than deep-copying it, which
            // this compiler has no utility for - caused a real
            // heap-use-after-free (free_ast() visits a shared subtree
            // through both parents, freeing it twice) found while
            // building this. Rejected explicitly rather than left as a
            // silent "sometimes untagged" gap, since an untagged
            // instance would be a real, hard-to-diagnose bug once
            // virtual dispatch consumes the tag later.
            compile_error(line, "'new' into a class-typed field reached through '^' isn't supported yet - allocate into a plain class variable first, then assign it into the field");
        }
        match(TOKEN_RPAREN);
        ASTNode *value_node = create_node(NODE_HEAP_ALLOC);
        value_node->expression_type = step.result_type;
        value_node->data.num_value = pointer_types[step.result_type - TYPE_POINTER_BASE].target_elem_size;
        ASTNode *stmt = create_node(NODE_HEAP_FIELD_ASSIGN);
        stmt->left = base;
        stmt->right = value_node;
        ASTNode *offset_lit = create_node(NODE_NUMBER);
        offset_lit->data.num_value = step.field_offset;
        offset_lit->expression_type = TYPE_INTEGER;
        stmt->extra = offset_lit;
        stmt->expression_type = step.result_type;
        return stmt;
    }

    // Optional constructor-call sugar: 'new(c, Init(args));' allocates
    // c exactly as plain 'new(c)' does, then immediately calls
    // c.Init(args) on it - equivalent to writing 'new(c); c.Init(args);'
    // as two statements, just guaranteed together. No method name is
    // reserved - Init isn't special, any method works here; nothing
    // stops 'new(c, Bump(5))'. Parsed (and the method resolved) here,
    // before match(TOKEN_RPAREN) below, since the method's own
    // parenthesized argument list needs to be consumed first - 'new's
    // own closing ')' comes after it.
    ASTNode *ctor_call = NULL;
    if (token.type == TOKEN_COMMA) {
        if (!pointer_types[target_type - TYPE_POINTER_BASE].is_class) {
            compile_error(token.line, "'new(x, Method(...))' is only valid when 'x' is a class-typed variable");
        }
        match(TOKEN_COMMA);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "Expected a method name after ','");
        }
        PointerTypeDef *ctor_pt = &pointer_types[target_type - TYPE_POINTER_BASE];
        char method_name[MAX_NAME];
        strcpy(method_name, token.text);
        int method_line = token.line;
        int method_idx = -1;
        for (int i = 0; i < ctor_pt->method_count; i++) {
            if (strcmp(ctor_pt->methods[i].name, method_name) == 0) { method_idx = i; break; }
        }
        if (method_idx == -1) {
            compile_error(method_line, "'%s' is not a method of class '%s'", method_name, ctor_pt->name);
        }
        match(TOKEN_IDENTIFIER);
        ProcParamHeader *ctor_h = &ctor_pt->methods[method_idx];
        if (ctor_h->is_class_method) {
            compile_error(method_line, "'%s' is a class method and can't be used as a constructor - it has no instance to initialize", method_name);
        }
        if (ctor_h->is_destructor) {
            compile_error(method_line, "'%s' is the destructor and can't be used as a constructor", method_name);
        }
        if (ctor_h->is_private && current_class_ptr_idx != ctor_h->declaring_class_ptr_idx) {
            // Same strict-private check as an ordinary method call - a
            // constructor isn't a special case (resolve_heap_deref_step()
            // never sees this call at all, since 'new(c, Init(args))' has
            // its own, separate method lookup here).
            compile_error(method_line, "'%s' is a private method of class '%s' and can't be called here", method_name, pointer_types[ctor_h->declaring_class_ptr_idx].name);
        }
        if (ctor_h->is_protected && !class_ptr_idx_is_or_descends_from(current_class_ptr_idx, ctor_h->declaring_class_ptr_idx)) {
            // Same protected check as an ordinary method call - see the
            // 'is_private' branch just above for why a constructor isn't
            // a special case here.
            compile_error(method_line, "'%s' is a protected method of class '%s' and can't be called here", method_name, pointer_types[ctor_h->declaring_class_ptr_idx].name);
        }
        int ctor_mangled_idx = find_proc(ctor_h->mangled_name);
        if (ctor_mangled_idx == -1) {
            compile_error(method_line, "'%s.%s' doesn't have a body yet", ctor_pt->name, method_name);
        }
        // A THIRD fresh read of the same target - self is a separate
        // subtree from write_node's own target-ref below and the
        // tag-write's fresh_read further down; reusing either would
        // double-free (see build_class_tag_write()'s own comment on
        // exactly this hazard). Same is_var_param/is_local/global
        // 3-way branch this function already uses twice elsewhere.
        ASTNode *ctor_self;
        if (is_var_param) {
            ctor_self = create_node(NODE_VAR_PARAM_READ);
            ctor_self->data.var_idx = local_idx;
            ctor_self->op = (TokenType)levels_up;
        } else if (is_local) {
            ctor_self = create_node(NODE_LOCAL_VAR);
            ctor_self->data.var_idx = local_idx;
            ctor_self->op = (TokenType)levels_up;
        } else {
            ctor_self = create_node(NODE_VARIABLE);
            ctor_self->data.var_idx = global_idx;
        }
        ctor_self->line = method_line;
        ctor_self->expression_type = target_type;
        ctor_call = create_node(NODE_VIRTUAL_CALL);
        ctor_call->line = method_line;
        ctor_call->data.num_value = method_idx;
        ctor_call->expression_type = ctor_h->is_function ? ctor_h->return_type : TYPE_UNKNOWN;
        ctor_call->op = TOKEN_PROCEDURE; // statement context: discard an unused function result, same as any other call-statement
        ctor_call->left = ctor_self;
        ctor_call->right = parse_class_method_call_arguments(ctor_mangled_idx);
    }
    match(TOKEN_RPAREN);

    if (is_var_param && local_at(local_idx, levels_up)->is_const_param) {
        compile_error(line, "cannot 'new' a 'const' parameter '%s'", name);
    }

    ASTNode *value_node = create_node(NODE_HEAP_ALLOC);
    value_node->expression_type = target_type;
    value_node->data.num_value = pointer_types[target_type - TYPE_POINTER_BASE].target_elem_size;

    ASTNode *write_node;
    if (is_var_param) {
        write_node = create_node(NODE_VAR_PARAM_ASSIGN);
        write_node->data.var_idx = local_idx;
        write_node->op = (TokenType)levels_up;
        write_node->expression_type = target_type;
    } else if (is_local) {
        write_node = create_node(NODE_LOCAL_ASSIGN);
        write_node->data.var_idx = local_idx;
        write_node->op = (TokenType)levels_up;
        write_node->expression_type = target_type;
    } else {
        write_node = create_node(NODE_ASSIGN);
        write_node->data.var_idx = global_idx;
    }
    write_node->left = value_node;
    if (pointer_types[target_type - TYPE_POINTER_BASE].is_class) {
        const char *unresolved_abstract = class_first_unresolved_abstract_method(target_type - TYPE_POINTER_BASE);
        if (unresolved_abstract != NULL) {
            compile_error(line, "Cannot instantiate '%s' - abstract method '%s' has no implementation", pointer_types[target_type - TYPE_POINTER_BASE].name, unresolved_abstract);
        }
        // A fresh read of the SAME target, to get the just-allocated
        // offset back for tagging - see build_class_tag_write()'s own
        // comment. Mirrors the '^'-chain branch's own base-building
        // above exactly, just for a plain named target instead.
        ASTNode *fresh_read;
        if (is_var_param) {
            fresh_read = create_node(NODE_VAR_PARAM_READ);
            fresh_read->data.var_idx = local_idx;
            fresh_read->op = (TokenType)levels_up;
            fresh_read->expression_type = target_type;
        } else if (is_local) {
            fresh_read = create_node(NODE_LOCAL_VAR);
            fresh_read->data.var_idx = local_idx;
            fresh_read->op = (TokenType)levels_up;
            fresh_read->expression_type = target_type;
        } else {
            fresh_read = create_node(NODE_VARIABLE);
            fresh_read->data.var_idx = global_idx;
            fresh_read->expression_type = target_type;
        }
        ASTNode *tag_write = build_class_tag_write(fresh_read, target_type - TYPE_POINTER_BASE, line);
        if (ctor_call != NULL) {
            tag_write->next = ctor_call; // runs after the tag is written, so a constructor calling other self.methods() dispatches correctly
        }
        return chain_two_statements(write_node, tag_write);
    }
    return write_node;
}

// 'dispose(X);' - unlike 'new', this only ever READS X (an ordinary
// expression - X can be anything pointer-typed, including a heap-
// dereferenced chain like 'p^.next', since dispose never writes back
// into X - see NODE_HEAP_DISPOSE in common.h for why this deliberately
// matches standard Pascal's own "the pointer's value is undefined after
// dispose" semantics rather than auto-nilling it).
//
// If X's target is a class with a destructor anywhere in its hierarchy
// (class_find_destructor()), a virtual call to it is spliced in right
// before the actual free, chained via chain_two_statements() (a raw
// 'dtor_call->next = stmt; return dtor_call;' would be WRONG - the
// caller/statement_list() would overwrite dtor_call's own ->next when
// linking the NEXT top-level statement, losing the link to stmt entirely;
// chain_two_statements() wraps both in a NODE_COMPOUND instead, whose
// OWN ->next is the only externally-visible splice point - same
// mechanism new(c, Init(args))'s own tag-write+constructor-call chain
// already uses).
//
// WARNING: the destructor's own body must never dispose() 'self' (or
// any alias of the same instance) - OP_DISPOSE (vm.c) has no "already
// freed" guard, only nil/out-of-range checks, so a self-dispose during
// teardown links the same block onto the freelist twice, corrupting it
// so a later new() can hand the same memory to two live, unrelated
// pointers. Not new here - dispose(c); dispose(c); already has this
// hazard today - but self-dispose from inside teardown code is an easy
// accident to make.
static ASTNode *parse_dispose_statement(void) {
    match(TOKEN_DISPOSE);
    match(TOKEN_LPAREN);
    int line = token.line;
    ASTNode *p_expr = expression();
    if (!is_pointer_type(p_expr->expression_type)) {
        compile_error(line, "'dispose' expects a pointer variable");
    }
    match(TOKEN_RPAREN);
    DataType target_type = p_expr->expression_type;
    ASTNode *stmt = create_node(NODE_HEAP_DISPOSE);
    stmt->line = line;
    stmt->left = p_expr;
    stmt->data.num_value = pointer_types[target_type - TYPE_POINTER_BASE].target_elem_size;

    if (pointer_types[target_type - TYPE_POINTER_BASE].is_class) {
        int class_ptr_idx = target_type - TYPE_POINTER_BASE;
        int dtor_slot = class_find_destructor(class_ptr_idx);
        if (dtor_slot != -1) {
            if (p_expr->type != NODE_VARIABLE && p_expr->type != NODE_LOCAL_VAR && p_expr->type != NODE_VAR_PARAM_READ) {
                compile_error(line, "'dispose' can't call '%s's destructor on this expression - dispose a plain variable instead", pointer_types[class_ptr_idx].name);
            }
            ProcParamHeader *dtor_h = &pointer_types[class_ptr_idx].methods[dtor_slot];
            if (find_proc(dtor_h->mangled_name) == -1) {
                compile_error(line, "'%s.%s' (the destructor) doesn't have a body yet", pointer_types[class_ptr_idx].name, dtor_h->name);
            }
            // A FRESH second read of the same target - p_expr itself is
            // already consumed as stmt->left above; reusing that same
            // node instance a second time here would make free_ast()
            // visit it through two parents and double-free it (the same
            // hazard parse_new_statement()'s own fresh-read comments
            // document already having hit once).
            ASTNode *self_read = create_node(p_expr->type);
            self_read->data.var_idx = p_expr->data.var_idx;
            self_read->op = p_expr->op;
            self_read->expression_type = target_type;
            self_read->line = line;
            ASTNode *dtor_call = create_node(NODE_VIRTUAL_CALL);
            dtor_call->line = line;
            dtor_call->data.num_value = dtor_slot;
            dtor_call->expression_type = TYPE_UNKNOWN;
            dtor_call->op = TOKEN_PROCEDURE;
            dtor_call->left = self_read;
            dtor_call->right = NULL;
            return chain_two_statements(dtor_call, stmt);
        }
    }
    return stmt;
}

// Caches 'expr' (an already-parsed expression, evaluated exactly once)
// into a fresh hidden temp - a local frame slot if currently inside a
// procedure/function body, a global otherwise - matching the existing
// local/global split parse_local_for_tail()/parse_for_in_tail_global()
// already use to cache a 'for' loop's end bound / a 'for x in s do's set
// expression. Needed here because a whole-element record-array copy
// ('arr[i] := someRecord;' or the reverse) reads/writes the SAME index
// value once per field, not just once - re-evaluating the raw index
// expression that many times would be both wrong (re-running any side
// effects) and wasteful. Returns the caching NODE_ASSIGN/NODE_LOCAL_ASSIGN
// statement (link it in via ->next before whatever uses the cached
// value) and, via *out_slot/*out_is_local, enough to build fresh
// NODE_VARIABLE/NODE_LOCAL_VAR references to it afterward - see
// make_cached_ref() below.
static ASTNode *cache_expr_once(ASTNode *expr, const char *name_prefix, int *out_slot, int *out_is_local) {
    if (nesting_depth >= 0) {
        char hidden_name[MAX_NAME];
        snprintf(hidden_name, MAX_NAME, "__%s_local%d", name_prefix, current_local_count);
        int slot = add_local(hidden_name, TYPE_INTEGER);
        ASTNode *cache_assign = create_node(NODE_LOCAL_ASSIGN);
        cache_assign->data.var_idx = slot;
        cache_assign->expression_type = TYPE_INTEGER;
        cache_assign->left = expr;
        *out_slot = slot;
        *out_is_local = 1;
        return cache_assign;
    }
    char hidden_name[MAX_NAME];
    snprintf(hidden_name, MAX_NAME, "__%s%d", name_prefix, sym_count);
    int slot = sym_count;
    add_var(hidden_name, TYPE_INTEGER);
    ASTNode *cache_assign = create_node(NODE_ASSIGN);
    cache_assign->data.var_idx = slot;
    cache_assign->left = expr;
    *out_slot = slot;
    *out_is_local = 0;
    return cache_assign;
}

// Builds a fresh reference to a value already cached via cache_expr_once()
// above - needed once per field a whole-element record-array copy
// touches, since the same cached temp can't be read by reusing the same
// ASTNode pointer in more than one place (ASTNode children form a tree,
// not a DAG - a node linked in twice would have its ->next silently
// double-managed).
static ASTNode *make_cached_ref(int slot, int is_local) {
    ASTNode *n = create_node(is_local ? NODE_LOCAL_VAR : NODE_VARIABLE);
    n->data.var_idx = slot;
    n->expression_type = TYPE_INTEGER;
    return n;
}

// 'arr[i] := <record source>;' where arr (dest_sym_idx) is a 1D array of
// records and index_expr is the already-parsed (not yet cached) index -
// ':=' has already been matched by the caller. The source may be either
// another record-array element ('arr[i] := other[j];', same or different
// array, same record type) or a plain record variable, global or local
// ('arr[i] := someRecord;'). Desugars into N field-by-field
// NODE_ARRAY_RECORD_FIELD_ASSIGN nodes (one per field of the record
// type), chained via ->next and wrapped in a NODE_COMPOUND - the same
// "a record isn't one runtime value" philosophy parse_whole_record_
// assignment() below already uses, generalized to a destination that's
// an array ELEMENT rather than a whole record variable. The destination
// index is cached once (see cache_expr_once()) since it's read once per
// field, not just once; when the source is ALSO a record-array element,
// its own index is cached too, for the same reason.
static ASTNode *parse_record_array_dest_whole_assignment(int dest_sym_idx, ASTNode *index_expr) {
    int dest_record_type_idx = find_record_array_type(dest_sym_idx);
    RecordTypeDef *rt = &record_types[dest_record_type_idx];

    int dest_slot, dest_is_local;
    ASTNode *dest_cache = cache_expr_once(index_expr, "recarr_idx", &dest_slot, &dest_is_local);

    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Expected a record value of the same type as '%s's elements", sym_table[dest_sym_idx].name);
    }
    char src_name[MAX_NAME];
    strcpy(src_name, token.text);
    int src_line = token.line;

    int src_arr_sym_idx = find_var_soft_visible(src_name);
    if (src_arr_sym_idx != -1 && sym_table[src_arr_sym_idx].is_record_array) {
        int src_record_type_idx = find_record_array_type(src_arr_sym_idx);
        if (src_record_type_idx != dest_record_type_idx) {
            compile_error(src_line, "Cannot assign '%s' (array of '%s') to an element of '%s' (array of '%s') - different record types",
                          src_name, record_types[src_record_type_idx].name, sym_table[dest_sym_idx].name, rt->name);
        }
        match(TOKEN_IDENTIFIER);
        if (token.type != TOKEN_LBRACKET) {
            compile_error(token.line, "Array '%s' must be indexed", src_name);
        }
        match(TOKEN_LBRACKET);
        ASTNode *src_index_expr = expression();
        match(TOKEN_RBRACKET);

        int src_slot, src_is_local;
        ASTNode *src_cache = cache_expr_once(src_index_expr, "recarr_idx", &src_slot, &src_is_local);
        dest_cache->next = src_cache;

        ASTNode *head = NULL, *tail = NULL;
        for (int i = 0; i < rt->field_count; i++) {
            ASTNode *offset_lit_r = create_node(NODE_NUMBER);
            offset_lit_r->data.num_value = i;
            offset_lit_r->expression_type = TYPE_INTEGER;
            ASTNode *value = create_node(NODE_ARRAY_RECORD_FIELD_ACCESS);
            value->data.var_idx = src_arr_sym_idx;
            value->left = make_cached_ref(src_slot, src_is_local);
            value->right = offset_lit_r;
            value->expression_type = rt->fields[i].type;

            ASTNode *offset_lit_w = create_node(NODE_NUMBER);
            offset_lit_w->data.num_value = i;
            offset_lit_w->expression_type = TYPE_INTEGER;
            ASTNode *assign = create_node(NODE_ARRAY_RECORD_FIELD_ASSIGN);
            assign->data.var_idx = dest_sym_idx;
            assign->left = make_cached_ref(dest_slot, dest_is_local);
            assign->right = value;
            assign->extra = offset_lit_w;
            assign->expression_type = rt->fields[i].type;

            if (!head) head = assign; else tail->next = assign;
            tail = assign;
        }
        src_cache->next = head;
        ASTNode *compound = create_node(NODE_COMPOUND);
        compound->left = dest_cache;
        return compound;
    }

    int src_levels_up2, src_is_local2, src_record_type_idx2;
    const int *src_field_idx;
    if (!find_any_record_var_outward(src_name, &src_levels_up2, &src_is_local2, &src_record_type_idx2, &src_field_idx)) {
        compile_error(src_line, "'%s' is not a record variable or an array of records", src_name);
    }
    if (src_record_type_idx2 != dest_record_type_idx) {
        compile_error(src_line, "Cannot assign '%s' (type '%s') to an element of '%s' (array of '%s') - different record types",
                      src_name, record_types[src_record_type_idx2].name, sym_table[dest_sym_idx].name, rt->name);
    }
    match(TOKEN_IDENTIFIER);

    ASTNode *head = NULL, *tail = NULL;
    for (int i = 0; i < rt->field_count; i++) {
        ASTNode *value = record_field_read_node(src_is_local2, src_field_idx[i], src_levels_up2);
        ASTNode *offset_lit = create_node(NODE_NUMBER);
        offset_lit->data.num_value = i;
        offset_lit->expression_type = TYPE_INTEGER;
        ASTNode *assign = create_node(NODE_ARRAY_RECORD_FIELD_ASSIGN);
        assign->data.var_idx = dest_sym_idx;
        assign->left = make_cached_ref(dest_slot, dest_is_local);
        assign->right = value;
        assign->extra = offset_lit;
        assign->expression_type = rt->fields[i].type;
        if (!head) head = assign; else tail->next = assign;
        tail = assign;
    }
    dest_cache->next = head;
    ASTNode *compound = create_node(NODE_COMPOUND);
    compound->left = dest_cache;
    return compound;
}

// Parses '[index].field := value' OR '[index] := <record source>' for an
// array-of-records already resolved to arr_sym_idx (whether a true global
// or a local's hidden mangled global) - '[' has already been matched by
// the caller. The write-side mirror of parse_record_array_field_read()
// above, shared between parse_global_assignment() below and the
// local-array write path in statement().
static ASTNode *parse_record_array_write(int arr_sym_idx) {
    ASTNode *index_expr = expression();
    match(TOKEN_RBRACKET);
    if (token.type == TOKEN_PERIOD) {
        match(TOKEN_PERIOD);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "Expected a field name after '%s[...].'", sym_table[arr_sym_idx].name);
        }
        int record_type_idx = find_record_array_type(arr_sym_idx);
        int field_idx = find_record_field(record_type_idx, token.text);
        if (field_idx == -1) {
            compile_error(token.line, "'%s' is not a field of '%s'", token.text, record_types[record_type_idx].name);
        }
        match(TOKEN_IDENTIFIER);
        RecordField *f = &record_types[record_type_idx].fields[field_idx];
        match(TOKEN_ASSIGN);
        ASTNode *stmt = create_node(NODE_ARRAY_RECORD_FIELD_ASSIGN);
        stmt->data.var_idx = arr_sym_idx;
        stmt->left = index_expr;
        stmt->right = wrap_range_check(expression(), f->is_subrange, f->subrange_lower, f->subrange_upper);
        ASTNode *offset_lit = create_node(NODE_NUMBER);
        offset_lit->data.num_value = field_idx;
        offset_lit->expression_type = TYPE_INTEGER;
        stmt->extra = offset_lit;
        stmt->expression_type = f->type;
        return stmt;
    }
    match(TOKEN_ASSIGN);
    return parse_record_array_dest_whole_assignment(arr_sym_idx, index_expr);
}

// Given an already-resolved GLOBAL symbol index, parses whatever follows
// (array indexing, 2D array indexing) plus ':=' and the value expression,
// and builds the appropriate assignment node. Shared between plain
// global-variable assignment and record field assignment - mirrors
// parse_global_symbol_reference on the read side.
static ASTNode *parse_global_assignment(int idx) {
    if (sym_table[idx].is_const) {
        compile_error(token.line, "Cannot assign to constant '%s'", sym_table[idx].name);
    }
    if (sym_table[idx].is_array) {
        if (token.type != TOKEN_LBRACKET) {
            compile_error(token.line, "Array '%s' must be indexed for assignment", sym_table[idx].name);
        }
        match(TOKEN_LBRACKET);
        if (sym_table[idx].is_record_array) {
            return parse_record_array_write(idx);
        }
        if (sym_table[idx].is_nd) {
            ASTNode *stmt = create_node(NODE_ARRAY_ASSIGN_ND);
            stmt->data.var_idx = idx;
            stmt->left = parse_nd_index_list(sym_table[idx].nd_dims); // consumes ']' itself
            match(TOKEN_ASSIGN);
            stmt->right = wrap_range_check(expression(), sym_table[idx].is_subrange,
                                            sym_table[idx].subrange_lower, sym_table[idx].subrange_upper); // value
            return stmt;
        }
        if (sym_table[idx].is_2d) {
            ASTNode *stmt = create_node(NODE_ARRAY_ASSIGN_2D);
            stmt->data.var_idx = idx;
            stmt->left = expression();  // first index
            match(TOKEN_COMMA);
            stmt->right = expression(); // second index
            match(TOKEN_RBRACKET);
            match(TOKEN_ASSIGN);
            stmt->extra = wrap_range_check(expression(), sym_table[idx].is_subrange,
                                            sym_table[idx].subrange_lower, sym_table[idx].subrange_upper); // value
            return stmt;
        }
        ASTNode *stmt = create_node(NODE_ASSIGN);
        stmt->data.var_idx = idx;
        stmt->left = expression();  // index
        match(TOKEN_RBRACKET);
        match(TOKEN_ASSIGN);
        stmt->right = wrap_range_check(expression(), sym_table[idx].is_subrange,
                                        sym_table[idx].subrange_lower, sym_table[idx].subrange_upper); // value
        return stmt;
    }
    if (token.type == TOKEN_LBRACKET && (sym_table[idx].type == TYPE_STRING || sym_table[idx].type == TYPE_CHAR)) {
        match(TOKEN_LBRACKET);
        ASTNode *stmt = create_node(NODE_STRING_INDEX_ASSIGN);
        stmt->data.var_idx = idx;
        stmt->left = expression();  // index
        match(TOKEN_RBRACKET);
        match(TOKEN_ASSIGN);
        stmt->right = expression(); // new character
        return stmt;
    }
    if (token.type == TOKEN_LBRACKET && is_dynarray_type(sym_table[idx].type)) {
        int line = token.line;
        match(TOKEN_LBRACKET);
        ASTNode *base = create_node(NODE_VARIABLE);
        base->line = line;
        base->data.var_idx = idx;
        base->expression_type = sym_table[idx].type;
        DynArrayTypeDef *d = &dynarray_types[sym_table[idx].type - TYPE_DYNARRAY_BASE];
        ASTNode *stmt = create_node(NODE_DYNARRAY_ASSIGN);
        stmt->line = line;
        stmt->left = base;
        stmt->right = expression(); // index
        match(TOKEN_RBRACKET);
        match(TOKEN_ASSIGN);
        stmt->extra = wrap_range_check(expression(), d->elem_is_subrange, d->elem_subrange_lower, d->elem_subrange_upper); // value
        stmt->expression_type = d->elem_type;
        return stmt;
    }
    if (is_pointer_type(sym_table[idx].type) && (token.type == TOKEN_CARET || class_dot_deref_pending(sym_table[idx].type))) {
        int line = token.line;
        ASTNode *base = create_node(NODE_VARIABLE);
        base->line = line;
        base->data.var_idx = idx;
        base->expression_type = sym_table[idx].type;
        HeapDerefStep step;
        base = parse_heap_deref_write(base, line, &step);
        if (step.is_method_call) return step.call_node;
        if (step.is_property_setter) return build_property_setter_call(base, step, line);
        return build_heap_deref_write_statement(base, step);
    }
    if (is_proc_type(sym_table[idx].type)) {
        // A NAMED procedural-type global. token is already positioned
        // right after the identifier (see this function's callers) -
        // ':=' means an assignment (parse_proc_value() handles the
        // RHS); anything else means this bare reference is a CALL,
        // exactly like any other zero-arg procedure/function used bare
        // already is - see build_procvar_call()'s own comment.
        if (token.type == TOKEN_ASSIGN) {
            ASTNode *stmt = create_node(NODE_ASSIGN);
            stmt->data.var_idx = idx;
            match(TOKEN_ASSIGN);
            stmt->left = parse_proc_value(sym_table[idx].type - TYPE_PROC_BASE, token.line);
            return stmt;
        }
        ASTNode *base = create_node(NODE_VARIABLE);
        base->line = token.line;
        base->data.var_idx = idx;
        base->expression_type = sym_table[idx].type;
        return build_procvar_call(base, sym_table[idx].type - TYPE_PROC_BASE, base->line, 1);
    }
    ASTNode *stmt = create_node(NODE_ASSIGN);
    stmt->data.var_idx = idx;
    match(TOKEN_ASSIGN);
    stmt->left = wrap_range_check(expression(), sym_table[idx].is_subrange,
                                   sym_table[idx].subrange_lower, sym_table[idx].subrange_upper); // value
    return stmt;
}

// Given an already-resolved LOCAL record field (a current_locals[] index),
// parses ':=' and the value expression. A record field local is always a
// plain scalar (add_local_record() rejects an array field), so unlike
// parse_global_assignment() there's no indexing to handle - this is the
// simple tail end of NODE_LOCAL_ASSIGN construction, reused wherever a
// local record field is a write target.
static ASTNode *parse_local_assignment(int local_idx, int levels_up) {
    match(TOKEN_ASSIGN);
    LocalSymbol *ls = local_at(local_idx, levels_up);
    return record_field_assign_node(1, local_idx, levels_up,
        wrap_range_check(expression(), ls->is_subrange,
                          ls->subrange_lower, ls->subrange_upper));
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
// Below the top level of parse_whole_record_assignment()'s copy loop
// below: same base+offset walk as build_record_arg_values()/
// build_record_compare() use, but appending one field-to-field assign
// node per LEAF field to the head/tail chain, recursing for a further
// nested-record field. src and dest are guaranteed the same record
// type throughout (checked once, at the top level, before any of this
// runs), so a single recursive walk drives both sides' offsets in
// lockstep.
static void build_record_copy(int record_type_idx, int dest_is_local, int dest_base, int dest_levels_up, int src_is_local, int src_base, int src_levels_up, ASTNode **head, ASTNode **tail) {
    RecordTypeDef *rt = &record_types[record_type_idx];
    int offset = 0;
    for (int i = 0; i < rt->field_count; i++) {
        RecordField *f = &rt->fields[i];
        if (f->is_record) {
            build_record_copy(f->record_type_idx, dest_is_local, dest_base + offset, dest_levels_up, src_is_local, src_base + offset, src_levels_up, head, tail);
        } else {
            ASTNode *value = record_field_read_node(src_is_local, src_base + offset, src_levels_up);
            ASTNode *assign = record_field_assign_node(dest_is_local, dest_base + offset, dest_levels_up, value);
            if (!*head) *head = assign; else (*tail)->next = assign;
            *tail = assign;
        }
        offset += f->is_record ? record_type_leaf_count(f->record_type_idx) : 1;
    }
}

static ASTNode *parse_whole_record_assignment(int dest_is_local, int dest_record_type_idx, const int *dest_field_idx, int dest_levels_up, const char *dest_name) {
    match(TOKEN_ASSIGN);
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Expected a record variable of the same type as '%s'", dest_name);
    }

    // The source may be a record-array ELEMENT ('destRec := arr[i];'),
    // not just another plain record variable - checked first via a soft
    // lookup, since the overwhelmingly common case is a plain record var.
    {
        int src_arr_sym_idx = find_var_soft_visible(token.text);
        if (src_arr_sym_idx != -1 && sym_table[src_arr_sym_idx].is_record_array) {
            int src_record_type_idx = find_record_array_type(src_arr_sym_idx);
            if (src_record_type_idx != dest_record_type_idx) {
                compile_error(token.line, "Cannot assign '%s' (array of '%s') to '%s' (type '%s') - different record types",
                              token.text, record_types[src_record_type_idx].name,
                              dest_name, record_types[dest_record_type_idx].name);
            }
            match(TOKEN_IDENTIFIER);
            if (token.type != TOKEN_LBRACKET) {
                compile_error(token.line, "Array '%s' must be indexed", sym_table[src_arr_sym_idx].name);
            }
            match(TOKEN_LBRACKET);
            ASTNode *index_expr = expression();
            match(TOKEN_RBRACKET);

            int idx_slot, idx_is_local;
            ASTNode *idx_cache = cache_expr_once(index_expr, "recarr_idx", &idx_slot, &idx_is_local);

            RecordTypeDef *rt = &record_types[dest_record_type_idx];
            ASTNode *head = NULL, *tail = NULL;
            for (int i = 0; i < rt->field_count; i++) {
                ASTNode *offset_lit = create_node(NODE_NUMBER);
                offset_lit->data.num_value = i;
                offset_lit->expression_type = TYPE_INTEGER;
                ASTNode *value = create_node(NODE_ARRAY_RECORD_FIELD_ACCESS);
                value->data.var_idx = src_arr_sym_idx;
                value->left = make_cached_ref(idx_slot, idx_is_local);
                value->right = offset_lit;
                value->expression_type = rt->fields[i].type;
                ASTNode *assign = record_field_assign_node(dest_is_local, dest_field_idx[i], dest_levels_up, value);
                if (!head) head = assign; else tail->next = assign;
                tail = assign;
            }
            idx_cache->next = head;
            ASTNode *compound = create_node(NODE_COMPOUND);
            compound->left = idx_cache;
            return compound;
        }
    }

    int src_levels_up, src_is_local, src_record_type_idx;
    const int *src_field_idx;
    if (!find_any_record_var_outward(token.text, &src_levels_up, &src_is_local, &src_record_type_idx, &src_field_idx)) {
        compile_error(token.line, "'%s' is not a record variable", token.text);
    }
    if (src_record_type_idx != dest_record_type_idx) {
        compile_error(token.line, "Cannot assign '%s' (type '%s') to '%s' (type '%s') - different record types",
                      token.text, record_types[src_record_type_idx].name,
                      dest_name, record_types[dest_record_type_idx].name);
    }
    match(TOKEN_IDENTIFIER);

    RecordTypeDef *rt = &record_types[dest_record_type_idx];
    for (int i = 0; i < rt->field_count; i++) {
        if (rt->fields[i].is_array) {
            compile_error(token.line, "Cannot assign whole record '%s': field '%s' is an array, and this compiler doesn't support whole-array assignment",
                          rt->name, rt->fields[i].name);
        }
    }

    ASTNode *head = NULL;
    ASTNode *tail = NULL;
    for (int i = 0; i < rt->field_count; i++) {
        RecordField *f = &rt->fields[i];
        if (f->is_record) {
            build_record_copy(f->record_type_idx, dest_is_local, dest_field_idx[i], dest_levels_up, src_is_local, src_field_idx[i], src_levels_up, &head, &tail);
            continue;
        }
        ASTNode *value = record_field_read_node(src_is_local, src_field_idx[i], src_levels_up);
        ASTNode *assign = record_field_assign_node(dest_is_local, dest_field_idx[i], dest_levels_up, value);
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
// Parses the '<var> := start (to|downto) end do body' tail of a 'for'
// statement, given the loop counter already resolved to a LOCAL frame
// slot (field_local_idx - either a plain local, or one field of a local/
// parameter record). Shared between the plain-local case and the local-
// record-field case in statement()'s TOKEN_FOR handling below: a local
// counter's end bound has to be cached in its own hidden local first
// (unlike the global/with-field/global-record-field case, which reuses
// the ordinary NODE_FOR shape directly) since NODE_LOCAL_FOR's codegen
// re-evaluates its ->right every loop iteration, and re-evaluating an
// arbitrary end-bound expression on every iteration would be both wrong
// (re-running any side effects) and wasteful.
static ASTNode *parse_local_for_tail(int field_local_idx) {
    match(TOKEN_ASSIGN);
    ASTNode *start_bound = expression();
    TokenType dir;
    if (token.type == TOKEN_TO) { match(TOKEN_TO); dir = TOKEN_TO; }
    else if (token.type == TOKEN_DOWNTO) { match(TOKEN_DOWNTO); dir = TOKEN_DOWNTO; }
    else { compile_error(token.line, "'for' expects 'to' or 'downto'"); dir = TOKEN_TO; }
    ASTNode *end_bound = expression();
    match(TOKEN_DO);

    // Cache the end bound in a hidden local, reserved now (during
    // parsing) rather than later - the enclosing procedure's local count
    // must be finalized before ENTER is emitted, so a new slot can't be
    // added once codegen has started.
    char hidden_name[MAX_NAME];
    snprintf(hidden_name, MAX_NAME, "__for_tmp_local%d", current_local_count);
    int end_slot = add_local(hidden_name, TYPE_INTEGER);

    ASTNode *cache_assign = create_node(NODE_LOCAL_ASSIGN);
    cache_assign->data.var_idx = end_slot;
    cache_assign->expression_type = TYPE_INTEGER;
    cache_assign->left = end_bound;

    ASTNode *for_node = create_node(NODE_LOCAL_FOR);
    for_node->data.var_idx = field_local_idx;
    for_node->op = dir;
    for_node->left = start_bound;
    ASTNode *end_ref = create_node(NODE_LOCAL_VAR);
    end_ref->data.var_idx = end_slot;
    end_ref->expression_type = TYPE_INTEGER;
    for_node->right = end_ref;

    loop_depth++;
    for_node->extra = statement();   // body
    loop_depth--;

    cache_assign->next = for_node;
    ASTNode *compound = create_node(NODE_COMPOUND);
    compound->left = cache_assign;
    return compound;
}

// Identity of a resolved 'for x in arr do' array target - enough to
// build repeated element reads of it (build_forin_array_element_read()
// below), without needing to re-resolve the name each time.
typedef struct {
    int is_ref;       // 1 = array-ref ('var') parameter (NODE_REF_ARRAY_ACCESS) - "which array" is a runtime value; 0 = plain/mangled-global array (NODE_ARRAY_ACCESS)
    int sym_idx;       // valid when !is_ref: the array's sym_table[] index
    int local_idx;      // valid when is_ref: the parameter's own local slot
    int levels_up;       // valid when is_ref: same convention find_local_outward() itself uses
    DataType elem_type;
    int lower, upper;
} ForInArrayTarget;

// Checks whether the CURRENT token names a supported 'for x in arr do'
// array target (a plain global array, a "local" array - which
// add_local_array() registers as a mangled GLOBAL, so it's resolved
// identically to a true global once found, see is_array below - or a
// by-reference array parameter), NOT immediately followed by '[' (that
// shape is ordinary indexed access, an entirely different, already-
// scalar-valued expression - e.g. 'for x in someSetArray[i] do' iterates
// the SET stored at that element, unaffected by this function at all).
// Mirrors try_get_array_bounds_here()'s own contract (peek, consume
// only on an actual match, return 0 without consuming anything
// otherwise so the caller can fall back to arithmetic_expression() for
// the set/string cases) but resolves fuller identity than that function
// needs for low()/high()/length(). Deliberately uses find_var_soft_
// visible() (NOT find_var(), which unconditionally fatal_abort()s on an
// unknown identifier) for the global fallback - 'for c in
// SomeStringFunc() do' must fall through gracefully here, not crash the
// compiler because a procedure name isn't a variable.
//
// Scope cut: doesn't check with-fields or record fields - 'for x in
// someRecord.numbers do' isn't recognized here and falls through to
// arithmetic_expression(), which will surface some natural (not
// purpose-written) "array must be indexed" rejection - a real but
// low-priority UX gap, not a correctness one, left undecorated for v1.
static int try_resolve_forin_array_here(ForInArrayTarget *out) {
    if (token.type != TOKEN_IDENTIFIER) return 0;
    char name[MAX_NAME];
    strcpy(name, token.text);
    Token saved_token = token;
    LexerPos saved_pos = lexer_save_pos();
    next_token(); // peek past the identifier
    int followed_by_bracket = (token.type == TOKEN_LBRACKET);
    token = saved_token;
    lexer_restore_pos(saved_pos);
    if (followed_by_bracket) return 0;

    int levels_up;
    int local_idx = find_local_outward(name, &levels_up);
    if (local_idx != -1) {
        LocalSymbol *ls = local_at(local_idx, levels_up);
        if (ls->is_array) {
            if (sym_table[ls->array_sym_idx].is_2d || sym_table[ls->array_sym_idx].is_nd
                || sym_table[ls->array_sym_idx].is_record_array) {
                compile_error(token.line, "'for x in %s do' doesn't support 2D/N-D arrays or arrays of records yet", name);
            }
            match(TOKEN_IDENTIFIER);
            out->is_ref = 0;
            out->sym_idx = ls->array_sym_idx;
            out->elem_type = sym_table[ls->array_sym_idx].type;
            out->lower = sym_table[ls->array_sym_idx].array_lower;
            out->upper = sym_table[ls->array_sym_idx].array_upper;
            return 1;
        }
        if (ls->is_array_ref) {
            if (ls->is_2d || ls->is_nd) {
                compile_error(token.line, "'for x in %s do' doesn't support 2D/N-D arrays yet", name);
            }
            match(TOKEN_IDENTIFIER);
            out->is_ref = 1;
            out->local_idx = local_idx;
            out->levels_up = levels_up;
            out->elem_type = ls->type;
            out->lower = ls->array_lower;
            out->upper = ls->array_upper;
            return 1;
        }
        return 0; // a local, but not an array - fall through
    }
    int global_idx = find_var_soft_visible(name);
    if (global_idx != -1 && sym_table[global_idx].is_array) {
        if (sym_table[global_idx].is_2d || sym_table[global_idx].is_nd
            || sym_table[global_idx].is_record_array) {
            compile_error(token.line, "'for x in %s do' doesn't support 2D/N-D arrays or arrays of records yet", name);
        }
        match(TOKEN_IDENTIFIER);
        out->is_ref = 0;
        out->sym_idx = global_idx;
        out->elem_type = sym_table[global_idx].type;
        out->lower = sym_table[global_idx].array_lower;
        out->upper = sym_table[global_idx].array_upper;
        return 1;
    }
    return 0; // not an array at all - fall through to a general expression
}

// Builds ONE element read of a resolved for-in array target, indexed by
// index_ref (a fresh read of the hidden sweep variable - never reused
// across iterations/callers, matching the AST-double-free hazard
// documented and avoided elsewhere in this file).
static ASTNode *build_forin_array_element_read(ForInArrayTarget *t, ASTNode *index_ref) {
    if (t->is_ref) {
        ASTNode *node = create_node(NODE_REF_ARRAY_ACCESS);
        node->data.var_idx = t->local_idx;
        node->op = (TokenType)t->levels_up;
        node->left = index_ref;
        node->expression_type = t->elem_type;
        return node;
    }
    ASTNode *node = create_node(NODE_ARRAY_ACCESS);
    node->data.var_idx = t->sym_idx;
    node->left = index_ref;
    node->expression_type = t->elem_type;
    return node;
}

// Identity of a resolved 'for x in arr do' DYNAMIC array target - a
// genuinely separate resolution path from ForInArrayTarget above, not a
// branch inside it: a dynamic array's representation is fundamentally
// different (a single scalar int naming a heap block - see
// TYPE_DYNARRAY_BASE - not per-index storage), so it needs none of
// is_ref/sym_idx/local_idx's "which of 3 storage shapes" split. Resolved
// exactly like any other dynamic-array-typed variable's own value is
// read elsewhere in this file (e.g. 'arr[i]'s own base - see
// NODE_DYNARRAY_ASSIGN's construction above) - is_local + idx +
// levels_up is enough to rebuild that same read as many times as
// needed (see build_forin_dynarray_read() below).
typedef struct {
    int is_local;      // 0 = global (idx is a sym_table[] index), 1 =
                        // local/'var'-parameter (idx is a local slot,
                        // levels_up meaningful - see find_local_outward())
    int idx;
    int levels_up;
    DataType dynarray_type; // the variable's OWN type (TYPE_DYNARRAY_BASE
                        // + its dynarray_types[] index) - needed on the
                        // read node itself, for Length()'s own dispatch
                        // and NODE_DYNARRAY_ACCESS's base.
    DataType elem_type;
    int elem_is_subrange, elem_subrange_lower, elem_subrange_upper;
} ForInDynArrayTarget;

// Same lookahead contract as try_resolve_forin_array_here() just above
// (peek, consume only on an actual match, return 0 without consuming
// anything otherwise) - but for a DYNAMIC array. Scope matches the
// static case exactly: a bare variable only (global, local, or 'var'-
// parameter - a dynamic array parameter is already "just an ordinary
// (or 'var') scalar parameter", no special-casing needed - see
// docs/CHANGELOG.md's own dynamic-arrays entry), not a general
// expression - 'for x in SomeFuncReturningDynArray() do' falls through
// to arithmetic_expression() below exactly like the static case already
// does for its own not-a-bare-variable inputs.
static int try_resolve_forin_dynarray_here(ForInDynArrayTarget *out) {
    if (token.type != TOKEN_IDENTIFIER) return 0;
    char name[MAX_NAME];
    strcpy(name, token.text);

    int levels_up;
    int local_idx = find_local_outward(name, &levels_up);
    if (local_idx != -1) {
        LocalSymbol *ls = local_at(local_idx, levels_up);
        if (!is_dynarray_type(ls->type)) return 0;
        match(TOKEN_IDENTIFIER);
        out->is_local = 1;
        out->idx = local_idx;
        out->levels_up = levels_up;
        out->dynarray_type = ls->type;
        DynArrayTypeDef *d = &dynarray_types[ls->type - TYPE_DYNARRAY_BASE];
        out->elem_type = d->elem_type;
        out->elem_is_subrange = d->elem_is_subrange;
        out->elem_subrange_lower = d->elem_subrange_lower;
        out->elem_subrange_upper = d->elem_subrange_upper;
        return 1;
    }
    int global_idx = find_var_soft_visible(name);
    if (global_idx != -1 && is_dynarray_type(sym_table[global_idx].type)) {
        match(TOKEN_IDENTIFIER);
        out->is_local = 0;
        out->idx = global_idx;
        out->dynarray_type = sym_table[global_idx].type;
        DynArrayTypeDef *d = &dynarray_types[sym_table[global_idx].type - TYPE_DYNARRAY_BASE];
        out->elem_type = d->elem_type;
        out->elem_is_subrange = d->elem_is_subrange;
        out->elem_subrange_lower = d->elem_subrange_lower;
        out->elem_subrange_upper = d->elem_subrange_upper;
        return 1;
    }
    return 0;
}

// Builds ONE fresh read of a resolved for-in DYNAMIC array target's own
// value (its heap offset - the same node shape 'arr[i] := x;'s own base
// already uses, e.g. in the NODE_DYNARRAY_ASSIGN construction above).
// Called TWICE per match (once for Length()'s argument, once for
// NODE_DYNARRAY_ACCESS's base) - never the same node reused in two
// places, matching this file's own documented AST-double-free hazard
// (see build_forin_array_element_read()'s own comment above).
static ASTNode *build_forin_dynarray_read(ForInDynArrayTarget *t) {
    ASTNode *node;
    if (t->is_local) {
        LocalSymbol *ls = local_at(t->idx, t->levels_up);
        node = create_node(ls->is_var_param ? NODE_VAR_PARAM_READ : NODE_LOCAL_VAR);
        node->op = (TokenType)t->levels_up;
    } else {
        node = create_node(NODE_VARIABLE);
    }
    node->data.var_idx = t->idx;
    node->expression_type = t->dynarray_type;
    return node;
}

// Builds 'Length(<a fresh read of t>) - 1' - the exact same shape
// high(arr) already desugars to for a dynamic array (see factor()'s own
// TOKEN_HIGH branch) - reused here as 'for x in arr do's own runtime
// upper bound. Ordinary integer arithmetic (both operands are genuinely
// TYPE_INTEGER), NOT the kind of pointer-in-a-binary-op trap '@'/'Addr'
// hit - type_checker.c's NODE_BINARY_OP case handles this exactly
// right, no dedicated node needed. NODE_FOR/NODE_LOCAL_FOR's own
// codegen already evaluates 'right' exactly once and caches it in a
// temp var (see codegen.c) regardless of whether it's a compile-time
// literal (the static-array case) or, as here, a computed expression -
// no codegen change needed for this to work.
static ASTNode *build_forin_dynarray_upper_bound(ForInDynArrayTarget *t) {
    ASTNode *len_call = create_node(NODE_BUILTIN_CALL);
    len_call->op = TOKEN_LENGTH;
    len_call->left = build_forin_dynarray_read(t);
    len_call->expression_type = TYPE_INTEGER;
    ASTNode *one = create_node(NODE_NUMBER);
    one->data.num_value = 1;
    one->expression_type = TYPE_INTEGER;
    ASTNode *node = create_node(NODE_BINARY_OP);
    node->op = TOKEN_MINUS;
    node->left = len_call;
    node->right = one;
    node->expression_type = TYPE_INTEGER;
    return node;
}

// Builds ONE element read of a resolved for-in DYNAMIC array target,
// indexed by index_ref (a fresh read of the hidden sweep variable, same
// convention as build_forin_array_element_read() above).
static ASTNode *build_forin_dynarray_element_read(ForInDynArrayTarget *t, ASTNode *index_ref) {
    ASTNode *node = create_node(NODE_DYNARRAY_ACCESS);
    node->left = build_forin_dynarray_read(t);
    node->right = index_ref;
    node->expression_type = t->elem_type;
    return node;
}

// Parses the 'in <set expr> do <body>' tail of 'for x in s do stmt',
// given the loop variable already resolved to a GLOBAL scalar
// (sym_idx - a plain global, a with-field, a global record field, or a
// static local's mangled global). Desugars entirely into AST nodes this
// compiler already knows how to generate - no new NodeType, no new
// opcode:
//
//     __for_in_setN := s;              { the set expr, evaluated once }
//     for x := 0 to MAX_SET_BITS - 1 do
//         if x in __for_in_setN then stmt
//
// A set's bit position IS its member's ordinal value directly (see
// TYPE_SET in common.h) - a set's declared base type's bounds are only
// ever checked at declaration time, then discarded - so sweeping the
// full, fixed 0..MAX_SET_BITS-1 range and testing membership each time
// is always correct, regardless of what the set was originally declared
// over. x itself must be plain integer (checked by the caller, exactly
// like an ordinary 'for' loop variable) - since a set's element "flavor"
// (integer/enum/boolean) isn't tracked past declaration either, there's
// no way to hand back anything but a raw ordinal.
static ASTNode *parse_for_in_tail_global(int sym_idx) {
    int line = token.line; // captured before further tokens are consumed,
                            // so a type-mismatch error below still points
                            // at the 'for' statement itself, not wherever
                            // parsing happens to have reached by the time
                            // the mismatch is discovered (e.g. the loop
                            // body's own first line, once the collection
                            // expression and 'do' are behind us)
    match(TOKEN_IN);

    ForInArrayTarget target;
    if (try_resolve_forin_array_here(&target)) {
        if (sym_table[sym_idx].type != target.elem_type) {
            compile_error(line, "'for' loop variable's type doesn't match the array's element type");
        }
        match(TOKEN_DO);

        char idx_name[MAX_NAME];
        snprintf(idx_name, MAX_NAME, "__for_in_arr_idx%d", sym_count);
        int idx_sym_idx = sym_count;
        add_var(idx_name, TYPE_INTEGER);

        ASTNode *for_node = create_node(NODE_FOR);
        for_node->data.var_idx = idx_sym_idx;
        for_node->op = TOKEN_TO;
        ASTNode *lo = create_node(NODE_NUMBER);
        lo->data.num_value = target.lower;
        lo->expression_type = TYPE_INTEGER;
        for_node->left = lo;
        ASTNode *hi = create_node(NODE_NUMBER);
        hi->data.num_value = target.upper;
        hi->expression_type = TYPE_INTEGER;
        for_node->right = hi;

        ASTNode *idx_read = create_node(NODE_VARIABLE);
        idx_read->data.var_idx = idx_sym_idx;
        idx_read->expression_type = TYPE_INTEGER;
        ASTNode *element_read = build_forin_array_element_read(&target, idx_read);

        ASTNode *assign_x = create_node(NODE_ASSIGN);
        assign_x->data.var_idx = sym_idx;
        assign_x->expression_type = sym_table[sym_idx].type;
        assign_x->left = wrap_range_check(element_read, sym_table[sym_idx].is_subrange,
            sym_table[sym_idx].subrange_lower, sym_table[sym_idx].subrange_upper);

        loop_depth++;
        assign_x->next = statement();   // body
        loop_depth--;
        ASTNode *body_compound = create_node(NODE_COMPOUND);
        body_compound->left = assign_x;
        for_node->extra = body_compound;

        return for_node;
    }

    ForInDynArrayTarget dyn_target;
    if (try_resolve_forin_dynarray_here(&dyn_target)) {
        if (sym_table[sym_idx].type != dyn_target.elem_type) {
            compile_error(line, "'for' loop variable's type doesn't match the array's element type");
        }
        match(TOKEN_DO);

        char idx_name[MAX_NAME];
        snprintf(idx_name, MAX_NAME, "__for_in_arr_idx%d", sym_count);
        int idx_sym_idx = sym_count;
        add_var(idx_name, TYPE_INTEGER);

        // Cached ONCE, before the loop starts - NODE_FOR's own codegen
        // would cache a runtime 'right' bound automatically (see
        // build_forin_dynarray_upper_bound()'s own comment), but this
        // function's LOCAL twin (parse_for_in_tail_local(), using
        // NODE_LOCAL_FOR) does NOT: its 'right' is re-evaluated every
        // single loop condition check unless the caller pre-caches it
        // (see NODE_LOCAL_FOR's own comment in codegen.c) - which would
        // both waste work and, worse, silently re-read a SHRUNK/GROWN
        // length if the loop body itself calls SetLength on the array.
        // Caching explicitly here, uniformly for both this function and
        // its local twin, sidesteps that difference entirely rather than
        // relying on it.
        int len_slot, len_is_local;
        ASTNode *len_cache_assign = cache_expr_once(build_forin_dynarray_upper_bound(&dyn_target),
                                                      "for_in_dynarr_len", &len_slot, &len_is_local);

        ASTNode *for_node = create_node(NODE_FOR);
        for_node->data.var_idx = idx_sym_idx;
        for_node->op = TOKEN_TO;
        ASTNode *lo = create_node(NODE_NUMBER);
        lo->data.num_value = 0;
        lo->expression_type = TYPE_INTEGER;
        for_node->left = lo;
        for_node->right = make_cached_ref(len_slot, len_is_local);

        ASTNode *idx_read = create_node(NODE_VARIABLE);
        idx_read->data.var_idx = idx_sym_idx;
        idx_read->expression_type = TYPE_INTEGER;
        ASTNode *element_read = build_forin_dynarray_element_read(&dyn_target, idx_read);

        ASTNode *assign_x = create_node(NODE_ASSIGN);
        assign_x->data.var_idx = sym_idx;
        assign_x->expression_type = sym_table[sym_idx].type;
        assign_x->left = wrap_range_check(element_read, sym_table[sym_idx].is_subrange,
            sym_table[sym_idx].subrange_lower, sym_table[sym_idx].subrange_upper);

        loop_depth++;
        assign_x->next = statement();   // body
        loop_depth--;
        ASTNode *body_compound2 = create_node(NODE_COMPOUND);
        body_compound2->left = assign_x;
        for_node->extra = body_compound2;

        len_cache_assign->next = for_node;
        ASTNode *compound = create_node(NODE_COMPOUND);
        compound->left = len_cache_assign;
        return compound;
    }

    ASTNode *collection_expr = arithmetic_expression();
    match(TOKEN_DO);

    if (collection_expr->expression_type == TYPE_STRING) {
        if (sym_table[sym_idx].type != TYPE_CHAR) {
            compile_error(line, "'for' loop variable must be char to iterate a string");
        }
        char str_name[MAX_NAME];
        snprintf(str_name, MAX_NAME, "__for_in_str%d", sym_count);
        int str_sym_idx = sym_count;
        add_var(str_name, TYPE_STRING);
        ASTNode *str_cache_assign = create_node(NODE_ASSIGN);
        str_cache_assign->data.var_idx = str_sym_idx;
        str_cache_assign->expression_type = TYPE_STRING;
        str_cache_assign->left = collection_expr;

        ASTNode *str_read_for_len = create_node(NODE_VARIABLE);
        str_read_for_len->data.var_idx = str_sym_idx;
        str_read_for_len->expression_type = TYPE_STRING;
        ASTNode *len_call = create_node(NODE_BUILTIN_CALL);
        len_call->op = TOKEN_LENGTH;
        len_call->left = str_read_for_len;
        len_call->expression_type = TYPE_INTEGER;

        int len_slot, len_is_local;
        ASTNode *len_cache_assign = cache_expr_once(len_call, "for_in_strlen", &len_slot, &len_is_local);

        char idx_name[MAX_NAME];
        snprintf(idx_name, MAX_NAME, "__for_in_str_idx%d", sym_count);
        int idx_sym_idx = sym_count;
        add_var(idx_name, TYPE_INTEGER);

        ASTNode *for_node = create_node(NODE_FOR);
        for_node->data.var_idx = idx_sym_idx;
        for_node->op = TOKEN_TO;
        ASTNode *lo = create_node(NODE_NUMBER);
        lo->data.num_value = 1;
        lo->expression_type = TYPE_INTEGER;
        for_node->left = lo;
        for_node->right = make_cached_ref(len_slot, len_is_local);

        ASTNode *idx_read = create_node(NODE_VARIABLE);
        idx_read->data.var_idx = idx_sym_idx;
        idx_read->expression_type = TYPE_INTEGER;
        ASTNode *char_read = create_node(NODE_STRING_INDEX);
        char_read->data.var_idx = str_sym_idx;
        char_read->left = idx_read;
        char_read->expression_type = TYPE_CHAR;

        ASTNode *assign_c = create_node(NODE_ASSIGN);
        assign_c->data.var_idx = sym_idx;
        assign_c->expression_type = TYPE_CHAR;
        assign_c->left = wrap_range_check(char_read, sym_table[sym_idx].is_subrange,
            sym_table[sym_idx].subrange_lower, sym_table[sym_idx].subrange_upper);

        loop_depth++;
        assign_c->next = statement();   // body
        loop_depth--;
        ASTNode *body_compound = create_node(NODE_COMPOUND);
        body_compound->left = assign_c;
        for_node->extra = body_compound;

        str_cache_assign->next = len_cache_assign;
        len_cache_assign->next = for_node;
        ASTNode *compound = create_node(NODE_COMPOUND);
        compound->left = str_cache_assign;
        return compound;
    }

    if (collection_expr->expression_type != TYPE_SET) {
        compile_error(line, "'for x in ...' expects a set, array, or string");
    }
    if (sym_table[sym_idx].type != TYPE_INTEGER) {
        compile_error(line, "'for' loop variable must be integer");
    }

    char hidden_name[MAX_NAME];
    snprintf(hidden_name, MAX_NAME, "__for_in_set%d", sym_count);
    int set_sym_idx = sym_count;
    add_var(hidden_name, TYPE_SET);

    ASTNode *cache_assign = create_node(NODE_ASSIGN);
    cache_assign->data.var_idx = set_sym_idx;
    cache_assign->expression_type = TYPE_SET;
    cache_assign->left = collection_expr;

    ASTNode *for_node = create_node(NODE_FOR);
    for_node->data.var_idx = sym_idx;
    for_node->op = TOKEN_TO;
    ASTNode *lo = create_node(NODE_NUMBER);
    lo->data.num_value = 0;
    lo->expression_type = TYPE_INTEGER;
    for_node->left = lo;
    ASTNode *hi = create_node(NODE_NUMBER);
    hi->data.num_value = MAX_SET_BITS - 1;
    hi->expression_type = TYPE_INTEGER;
    for_node->right = hi;

    ASTNode *x_read = create_node(NODE_VARIABLE);
    x_read->data.var_idx = sym_idx;
    x_read->expression_type = TYPE_INTEGER;
    ASTNode *set_read = create_node(NODE_VARIABLE);
    set_read->data.var_idx = set_sym_idx;
    set_read->expression_type = TYPE_SET;
    ASTNode *in_test = create_node(NODE_SET_IN);
    in_test->left = x_read;
    in_test->right = set_read;
    in_test->expression_type = TYPE_BOOLEAN;

    ASTNode *if_node = create_node(NODE_IF);
    if_node->left = in_test;

    loop_depth++;
    if_node->right = statement();   // body
    loop_depth--;
    for_node->extra = if_node;

    cache_assign->next = for_node;
    ASTNode *compound = create_node(NODE_COMPOUND);
    compound->left = cache_assign;
    return compound;
}

// Same as parse_for_in_tail_global() above, but for a loop variable that
// resolved to a LOCAL frame slot (a plain local, or one field of a
// local/parameter record) - mirrors parse_local_for_tail()'s use of a
// hidden local (rather than a hidden global) to cache the (here, set-
// typed) value that must only be evaluated once.
static ASTNode *parse_for_in_tail_local(int local_idx) {
    int line = token.line; // captured before further tokens are consumed -
                            // see parse_for_in_tail_global()'s identical
                            // comment for why
    match(TOKEN_IN);

    ForInArrayTarget target;
    if (try_resolve_forin_array_here(&target)) {
        if (current_locals[local_idx].type != target.elem_type) {
            compile_error(line, "'for' loop variable's type doesn't match the array's element type");
        }
        match(TOKEN_DO);

        char idx_name[MAX_NAME];
        snprintf(idx_name, MAX_NAME, "__for_in_arr_idx_local%d", current_local_count);
        int idx_slot = add_local(idx_name, TYPE_INTEGER);

        ASTNode *for_node = create_node(NODE_LOCAL_FOR);
        for_node->data.var_idx = idx_slot;
        for_node->op = TOKEN_TO;
        ASTNode *lo = create_node(NODE_NUMBER);
        lo->data.num_value = target.lower;
        lo->expression_type = TYPE_INTEGER;
        for_node->left = lo;
        ASTNode *hi = create_node(NODE_NUMBER);
        hi->data.num_value = target.upper;
        hi->expression_type = TYPE_INTEGER;
        for_node->right = hi;

        ASTNode *idx_read = create_node(NODE_LOCAL_VAR);
        idx_read->data.var_idx = idx_slot;
        idx_read->expression_type = TYPE_INTEGER;
        ASTNode *element_read = build_forin_array_element_read(&target, idx_read);

        ASTNode *assign_x = create_node(NODE_LOCAL_ASSIGN);
        assign_x->data.var_idx = local_idx;
        assign_x->expression_type = current_locals[local_idx].type;
        assign_x->left = wrap_range_check(element_read, current_locals[local_idx].is_subrange,
            current_locals[local_idx].subrange_lower, current_locals[local_idx].subrange_upper);

        loop_depth++;
        assign_x->next = statement();   // body
        loop_depth--;
        ASTNode *body_compound = create_node(NODE_COMPOUND);
        body_compound->left = assign_x;
        for_node->extra = body_compound;

        return for_node;
    }

    ForInDynArrayTarget dyn_target;
    if (try_resolve_forin_dynarray_here(&dyn_target)) {
        if (current_locals[local_idx].type != dyn_target.elem_type) {
            compile_error(line, "'for' loop variable's type doesn't match the array's element type");
        }
        match(TOKEN_DO);

        char idx_name[MAX_NAME];
        snprintf(idx_name, MAX_NAME, "__for_in_arr_idx_local%d", current_local_count);
        int idx_slot = add_local(idx_name, TYPE_INTEGER);

        // Cached ONCE, before the loop starts - see the identical
        // comment in parse_for_in_tail_global()'s own dynamic-array
        // branch for why this is required (NODE_LOCAL_FOR, unlike
        // NODE_FOR, never caches a runtime 'right' bound itself).
        int len_slot, len_is_local;
        ASTNode *len_cache_assign = cache_expr_once(build_forin_dynarray_upper_bound(&dyn_target),
                                                      "for_in_dynarr_len", &len_slot, &len_is_local);

        ASTNode *for_node = create_node(NODE_LOCAL_FOR);
        for_node->data.var_idx = idx_slot;
        for_node->op = TOKEN_TO;
        ASTNode *lo = create_node(NODE_NUMBER);
        lo->data.num_value = 0;
        lo->expression_type = TYPE_INTEGER;
        for_node->left = lo;
        for_node->right = make_cached_ref(len_slot, len_is_local);

        ASTNode *idx_read = create_node(NODE_LOCAL_VAR);
        idx_read->data.var_idx = idx_slot;
        idx_read->expression_type = TYPE_INTEGER;
        ASTNode *element_read = build_forin_dynarray_element_read(&dyn_target, idx_read);

        ASTNode *assign_x = create_node(NODE_LOCAL_ASSIGN);
        assign_x->data.var_idx = local_idx;
        assign_x->expression_type = current_locals[local_idx].type;
        assign_x->left = wrap_range_check(element_read, current_locals[local_idx].is_subrange,
            current_locals[local_idx].subrange_lower, current_locals[local_idx].subrange_upper);

        loop_depth++;
        assign_x->next = statement();   // body
        loop_depth--;
        ASTNode *body_compound2 = create_node(NODE_COMPOUND);
        body_compound2->left = assign_x;
        for_node->extra = body_compound2;

        len_cache_assign->next = for_node;
        ASTNode *compound = create_node(NODE_COMPOUND);
        compound->left = len_cache_assign;
        return compound;
    }

    ASTNode *collection_expr = arithmetic_expression();
    match(TOKEN_DO);

    if (collection_expr->expression_type == TYPE_STRING) {
        if (current_locals[local_idx].type != TYPE_CHAR) {
            compile_error(line, "'for' loop variable must be char to iterate a string");
        }
        char str_name[MAX_NAME];
        snprintf(str_name, MAX_NAME, "__for_in_str_local%d", current_local_count);
        int str_slot = add_local(str_name, TYPE_STRING);
        ASTNode *str_cache_assign = create_node(NODE_LOCAL_ASSIGN);
        str_cache_assign->data.var_idx = str_slot;
        str_cache_assign->expression_type = TYPE_STRING;
        str_cache_assign->left = collection_expr;

        ASTNode *str_read_for_len = create_node(NODE_LOCAL_VAR);
        str_read_for_len->data.var_idx = str_slot;
        str_read_for_len->expression_type = TYPE_STRING;
        ASTNode *len_call = create_node(NODE_BUILTIN_CALL);
        len_call->op = TOKEN_LENGTH;
        len_call->left = str_read_for_len;
        len_call->expression_type = TYPE_INTEGER;

        int len_slot, len_is_local;
        ASTNode *len_cache_assign = cache_expr_once(len_call, "for_in_strlen", &len_slot, &len_is_local);

        char idx_name[MAX_NAME];
        snprintf(idx_name, MAX_NAME, "__for_in_str_idx_local%d", current_local_count);
        int idx_slot = add_local(idx_name, TYPE_INTEGER);

        ASTNode *for_node = create_node(NODE_LOCAL_FOR);
        for_node->data.var_idx = idx_slot;
        for_node->op = TOKEN_TO;
        ASTNode *lo = create_node(NODE_NUMBER);
        lo->data.num_value = 1;
        lo->expression_type = TYPE_INTEGER;
        for_node->left = lo;
        for_node->right = make_cached_ref(len_slot, len_is_local);

        ASTNode *idx_read = create_node(NODE_LOCAL_VAR);
        idx_read->data.var_idx = idx_slot;
        idx_read->expression_type = TYPE_INTEGER;
        ASTNode *char_read = create_node(NODE_LOCAL_STRING_INDEX);
        char_read->data.var_idx = str_slot;
        char_read->op = (TokenType)0;
        char_read->left = idx_read;
        char_read->expression_type = TYPE_CHAR;

        ASTNode *assign_c = create_node(NODE_LOCAL_ASSIGN);
        assign_c->data.var_idx = local_idx;
        assign_c->expression_type = TYPE_CHAR;
        assign_c->left = wrap_range_check(char_read, current_locals[local_idx].is_subrange,
            current_locals[local_idx].subrange_lower, current_locals[local_idx].subrange_upper);

        loop_depth++;
        assign_c->next = statement();   // body
        loop_depth--;
        ASTNode *body_compound = create_node(NODE_COMPOUND);
        body_compound->left = assign_c;
        for_node->extra = body_compound;

        str_cache_assign->next = len_cache_assign;
        len_cache_assign->next = for_node;
        ASTNode *compound = create_node(NODE_COMPOUND);
        compound->left = str_cache_assign;
        return compound;
    }

    if (collection_expr->expression_type != TYPE_SET) {
        compile_error(line, "'for x in ...' expects a set, array, or string");
    }
    if (current_locals[local_idx].type != TYPE_INTEGER) {
        compile_error(line, "'for' loop variable must be integer");
    }

    char hidden_name[MAX_NAME];
    snprintf(hidden_name, MAX_NAME, "__for_in_set_local%d", current_local_count);
    int set_slot = add_local(hidden_name, TYPE_SET);

    ASTNode *cache_assign = create_node(NODE_LOCAL_ASSIGN);
    cache_assign->data.var_idx = set_slot;
    cache_assign->expression_type = TYPE_SET;
    cache_assign->left = collection_expr;

    ASTNode *for_node = create_node(NODE_LOCAL_FOR);
    for_node->data.var_idx = local_idx;
    for_node->op = TOKEN_TO;
    ASTNode *lo = create_node(NODE_NUMBER);
    lo->data.num_value = 0;
    lo->expression_type = TYPE_INTEGER;
    for_node->left = lo;
    ASTNode *hi = create_node(NODE_NUMBER);
    hi->data.num_value = MAX_SET_BITS - 1;
    hi->expression_type = TYPE_INTEGER;
    for_node->right = hi;

    ASTNode *x_read = create_node(NODE_LOCAL_VAR);
    x_read->data.var_idx = local_idx;
    x_read->expression_type = TYPE_INTEGER;
    ASTNode *set_read = create_node(NODE_LOCAL_VAR);
    set_read->data.var_idx = set_slot;
    set_read->expression_type = TYPE_SET;
    ASTNode *in_test = create_node(NODE_SET_IN);
    in_test->left = x_read;
    in_test->right = set_read;
    in_test->expression_type = TYPE_BOOLEAN;

    ASTNode *if_node = create_node(NODE_IF);
    if_node->left = in_test;

    loop_depth++;
    if_node->right = statement();   // body
    loop_depth--;
    for_node->extra = if_node;

    cache_assign->next = for_node;
    ASTNode *compound = create_node(NODE_COMPOUND);
    compound->left = cache_assign;
    return compound;
}

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

// Builds the NODE_LOCAL_ASSIGN that stores a value into function_idx's
// own return_slot - shared by the 'FuncName := expr' assignment (below,
// in statement()'s TOKEN_IDENTIFIER branch) and 'exit(value)' (also in
// statement()), so both go through the exact same procedural-return-type
// and subrange-return-type handling. Assumes 'token' is positioned right
// after whichever prefix ('FuncName :=' or 'exit(') already matched, at
// the start of the value expression itself.
static ASTNode *build_return_assign_node(int function_idx) {
    ASTNode *stmt = create_node(NODE_LOCAL_ASSIGN);
    stmt->data.var_idx = proc_table[function_idx].return_slot;
    stmt->expression_type = proc_table[function_idx].return_type;
    if (proc_table[function_idx].return_type >= TYPE_PROC_BASE) {
        // A procedural return type needs the same specialized parser
        // every other procedural-type assignment target already uses -
        // the generic expression() below would misparse a bare proc
        // name as a zero-argument CALL to it, not a reference (see
        // docs/LANGUAGE.md#procedural-types).
        stmt->left = parse_proc_value(proc_table[function_idx].return_type - TYPE_PROC_BASE, token.line);
    } else {
        // return_is_subrange is never set for a procedural return type
        // (mirrors ProcParamHeader's own "method return types are never
        // subrange" precedent), so this wrap is meaningless there
        // anyway - only reached for an ordinary scalar return type.
        stmt->left = wrap_range_check(expression(),
            proc_table[function_idx].return_is_subrange,
            proc_table[function_idx].return_subrange_lower,
            proc_table[function_idx].return_subrange_upper);
    }
    return stmt;
}

static ASTNode *statement(void) {
    if (token.type == TOKEN_NUMBER) {
        // A bare integer literal can only ever appear here as a
        // 'N: statement' label prefix - no other Pascal statement starts
        // with one, so there's nothing to disambiguate (see
        // is_statement_start()'s comment).
        int line = token.line;
        ASTNode *stmt = create_node(NODE_LABEL);
        int id = token.value;
        match(TOKEN_NUMBER);
        match(TOKEN_COLON);
        int idx = find_declared_label(id);
        if (idx == -1) {
            compile_error(line, "Label %d wasn't declared in this block's 'label' section", id);
        }
        if (declared_labels[idx].defined) {
            compile_error(line, "Label %d already labels another statement", id);
        }
        if (finally_body_depth > 0) {
            compile_error(line, "Label %d can't appear inside a 'finally' block - its cleanup code is compiled twice internally (once for normal completion, once for exception unwinding), which would create an ambiguous jump target", id);
        }
        declared_labels[idx].defined = 1;
        stmt->data.num_value = id;
        stmt->left = statement();
        return stmt;
    }

    if (token.type == TOKEN_GOTO) {
        int line = token.line;
        ASTNode *stmt = create_node(NODE_GOTO);
        match(TOKEN_GOTO);
        if (token.type != TOKEN_NUMBER) {
            compile_error(token.line, "Expected a label number after 'goto'");
        }
        int id = token.value;
        match(TOKEN_NUMBER);
        if (find_declared_label(id) == -1) {
            compile_error(line, "'goto %d' references a label not declared in this block's 'label' section", id);
        }
        stmt->data.num_value = id;
        return stmt;
    }

    if (token.type == TOKEN_BEGIN) {
        return compound_statement();
    }

    if (token.type == TOKEN_INHERITED) {
        ASTNode *call = parse_inherited_call(1);
        call->op = TOKEN_PROCEDURE; // statement context: discard an unused function result
        return call;
    }

    if (token.type == TOKEN_IDENTIFIER) {
        ASTNode *class_qualified = try_resolve_class_qualified_access(1);
        if (class_qualified) return class_qualified;

        if (find_const(token.text) != -1) {
            compile_error(token.line, "'%s' is a constant and cannot be assigned to", token.text);
        }
        {
            int enum_type_idx, ordinal;
            if (find_enum_value(token.text, &enum_type_idx, &ordinal)) {
                compile_error(token.line, "'%s' is an enumerated value and cannot be assigned to", token.text);
            }
        }

        {
            int with_field_idx = find_with_field(token.text);
            if (with_field_idx != -1) {
                match(TOKEN_IDENTIFIER);
                return parse_global_assignment(with_field_idx);
            }
        }

        {
            int rv_levels_up, rv_is_local, rv_record_type_idx;
            const int *rv_field_idx;
            if (find_any_record_var_outward(token.text, &rv_levels_up, &rv_is_local, &rv_record_type_idx, &rv_field_idx)) {
                char rec_name[MAX_NAME];
                strcpy(rec_name, token.text);
                match(TOKEN_IDENTIFIER);
                if (token.type == TOKEN_PERIOD) {
                    match(TOKEN_PERIOD);
                    int leaf_idx = resolve_record_field_leaf(rv_record_type_idx, rv_field_idx, rec_name);
                    if (rv_is_local) {
                        return parse_local_assignment(leaf_idx, rv_levels_up);
                    }
                    return parse_global_assignment(leaf_idx);
                }
                // No '.field' - this is a whole-record assignment: 'p2 := p1;'
                return parse_whole_record_assignment(rv_is_local, rv_record_type_idx, rv_field_idx, rv_levels_up, rec_name);
            }
        }

        // A class method function's OWN name, for this "assign to set
        // the return value" check, is the short name the user wrote in
        // the method body's own header line ('function TFoo.Bar;') -
        // proc_table[]'s own .name is the MANGLED 'TFoo__Bar' (needed
        // for call-site lookups elsewhere), never what a method body's
        // source text itself uses - see ProcSymbol.unmangled_name's
        // comment.
        const char *own_function_name = current_function_idx != -1 && proc_table[current_function_idx].unmangled_name[0]
            ? proc_table[current_function_idx].unmangled_name
            : (current_function_idx != -1 ? proc_table[current_function_idx].name : "");
        if (current_function_idx != -1 && strcmp(token.text, own_function_name) == 0) {
            match(TOKEN_IDENTIFIER);
            if (token.type == TOKEN_ASSIGN) {
                // Assigning to the function's own name sets its return value.
                match(TOKEN_ASSIGN);
                return build_return_assign_node(current_function_idx);
            }
            // Otherwise this is a recursive self-call used as a statement
            // (discarding the return value) - same shape as calling any
            // other procedure/function by name.
            ASTNode *stmt = create_node(NODE_CALL);
            stmt->data.var_idx = current_function_idx;
            stmt->op = TOKEN_PROCEDURE; // marks statement context: discard an unused function result
            stmt->left = parse_call_arguments(current_function_idx);
            return stmt;
        }

        int levels_up;
        int local_idx = find_local_outward(token.text, &levels_up);
        if (local_idx != -1) {
            LocalSymbol *ls = local_at(local_idx, levels_up);
            if (ls->is_proc_param) {
                return parse_indirect_call(ls, local_idx, levels_up, token.line, 1);
            }
            if (ls->is_var_param) {
                match(TOKEN_IDENTIFIER);
                if (is_proc_type(ls->type)) {
                    // A NAMED procedural-type 'var' parameter - see the
                    // matching global case in parse_global_assignment()
                    // for why this branches on token.type == TOKEN_ASSIGN.
                    if (token.type == TOKEN_ASSIGN) {
                        if (ls->is_const_param) {
                            compile_error(token.line, "cannot assign to 'const' parameter '%s'", ls->name);
                        }
                        ASTNode *stmt = create_node(NODE_VAR_PARAM_ASSIGN);
                        stmt->data.var_idx = local_idx;
                        stmt->op = (TokenType)levels_up;
                        stmt->expression_type = ls->type;
                        match(TOKEN_ASSIGN);
                        stmt->left = parse_proc_value(ls->type - TYPE_PROC_BASE, token.line);
                        return stmt;
                    }
                    ASTNode *base = create_node(NODE_VAR_PARAM_READ);
                    base->line = token.line;
                    base->data.var_idx = local_idx;
                    base->op = (TokenType)levels_up;
                    base->expression_type = ls->type;
                    return build_procvar_call(base, ls->type - TYPE_PROC_BASE, base->line, 1);
                }
                if (is_pointer_type(ls->type) && (token.type == TOKEN_CARET || class_dot_deref_pending(ls->type))) {
                    int line = token.line;
                    ASTNode *base = create_node(NODE_VAR_PARAM_READ);
                    base->line = line;
                    base->data.var_idx = local_idx;
                    base->op = (TokenType)levels_up;
                    base->expression_type = ls->type;
                    HeapDerefStep step;
                    base = parse_heap_deref_write(base, line, &step);
                    if (step.is_method_call) return step.call_node;
                    if (step.is_property_setter) return build_property_setter_call(base, step, line);
                    return build_heap_deref_write_statement(base, step);
                }
                if (is_dynarray_type(ls->type) && token.type == TOKEN_LBRACKET) {
                    // Writing THROUGH the parameter to one of its
                    // elements - shallow, same as a pointer/class
                    // parameter's own field write above, so this is
                    // allowed even for a 'const' parameter (only
                    // reassigning the parameter's OWN pointer value,
                    // below, is blocked for one - see is_const_param's
                    // own comment).
                    int line = token.line;
                    match(TOKEN_LBRACKET);
                    ASTNode *base = create_node(NODE_VAR_PARAM_READ);
                    base->line = line;
                    base->data.var_idx = local_idx;
                    base->op = (TokenType)levels_up;
                    base->expression_type = ls->type;
                    DynArrayTypeDef *d = &dynarray_types[ls->type - TYPE_DYNARRAY_BASE];
                    ASTNode *stmt = create_node(NODE_DYNARRAY_ASSIGN);
                    stmt->line = line;
                    stmt->left = base;
                    stmt->right = expression(); // index
                    match(TOKEN_RBRACKET);
                    match(TOKEN_ASSIGN);
                    stmt->extra = wrap_range_check(expression(), d->elem_is_subrange, d->elem_subrange_lower, d->elem_subrange_upper); // value
                    stmt->expression_type = d->elem_type;
                    return stmt;
                }
                if (ls->is_const_param) {
                    compile_error(token.line, "cannot assign to 'const' parameter '%s'", ls->name);
                }
                match(TOKEN_ASSIGN);
                ASTNode *stmt = create_node(NODE_VAR_PARAM_ASSIGN);
                stmt->data.var_idx = local_idx;
                stmt->op = (TokenType)levels_up;
                stmt->expression_type = ls->type;
                stmt->left = wrap_range_check(expression(), ls->is_subrange,
                    ls->subrange_lower, ls->subrange_upper);
                return stmt;
            }
            if (ls->is_static) {
                match(TOKEN_IDENTIFIER);
                return parse_global_assignment(ls->static_sym_idx);
            }
            if (ls->is_array) {
                int arr_sym_idx = ls->array_sym_idx;
                match(TOKEN_IDENTIFIER);
                if (token.type != TOKEN_LBRACKET) {
                    compile_error(token.line, "Array '%s' must be indexed for assignment", ls->name);
                }
                if (sym_table[arr_sym_idx].is_record_array) {
                    match(TOKEN_LBRACKET);
                    return parse_record_array_write(arr_sym_idx);
                }
                if (sym_table[arr_sym_idx].is_nd) {
                    ASTNode *stmt = create_node(NODE_ARRAY_ASSIGN_ND);
                    stmt->data.var_idx = arr_sym_idx;
                    match(TOKEN_LBRACKET);
                    stmt->left = parse_nd_index_list(sym_table[arr_sym_idx].nd_dims); // consumes ']' itself
                    match(TOKEN_ASSIGN);
                    stmt->right = wrap_range_check(expression(), sym_table[arr_sym_idx].is_subrange,
                        sym_table[arr_sym_idx].subrange_lower, sym_table[arr_sym_idx].subrange_upper); // value
                    return stmt;
                }
                if (sym_table[arr_sym_idx].is_2d) {
                    ASTNode *stmt = create_node(NODE_ARRAY_ASSIGN_2D);
                    stmt->data.var_idx = arr_sym_idx;
                    match(TOKEN_LBRACKET);
                    stmt->left = expression();  // first index
                    match(TOKEN_COMMA);
                    stmt->right = expression(); // second index
                    match(TOKEN_RBRACKET);
                    match(TOKEN_ASSIGN);
                    stmt->extra = wrap_range_check(expression(), sym_table[arr_sym_idx].is_subrange,
                        sym_table[arr_sym_idx].subrange_lower, sym_table[arr_sym_idx].subrange_upper); // value
                    return stmt;
                }
                ASTNode *stmt = create_node(NODE_ASSIGN);
                stmt->data.var_idx = arr_sym_idx;
                match(TOKEN_LBRACKET);
                stmt->left = expression();  // index
                match(TOKEN_RBRACKET);
                match(TOKEN_ASSIGN);
                stmt->right = wrap_range_check(expression(), sym_table[arr_sym_idx].is_subrange,
                    sym_table[arr_sym_idx].subrange_lower, sym_table[arr_sym_idx].subrange_upper); // value
                return stmt;
            }
            if (ls->is_array_ref) {
                match(TOKEN_IDENTIFIER);
                if (token.type != TOKEN_LBRACKET) {
                    compile_error(token.line, "Array '%s' must be indexed for assignment", ls->name);
                }
                if (ls->is_nd) {
                    ASTNode *stmt = create_node(NODE_REF_ARRAY_ASSIGN_ND);
                    stmt->data.var_idx = local_idx;
                    stmt->op = (TokenType)levels_up;
                    stmt->expression_type = ls->type;
                    match(TOKEN_LBRACKET);
                    stmt->left = parse_nd_index_list(ls->nd_dims); // consumes ']' itself
                    match(TOKEN_ASSIGN);
                    stmt->right = wrap_range_check(expression(), ls->is_subrange,
                        ls->subrange_lower, ls->subrange_upper); // value
                    return stmt;
                }
                if (ls->is_2d) {
                    ASTNode *stmt = create_node(NODE_REF_ARRAY_ASSIGN_2D);
                    stmt->data.var_idx = local_idx;
                    stmt->op = (TokenType)levels_up;
                    stmt->expression_type = ls->type;
                    match(TOKEN_LBRACKET);
                    stmt->left = expression();  // first index
                    match(TOKEN_COMMA);
                    stmt->right = expression(); // second index
                    match(TOKEN_RBRACKET);
                    match(TOKEN_ASSIGN);
                    stmt->extra = wrap_range_check(expression(), ls->is_subrange,
                        ls->subrange_lower, ls->subrange_upper); // value
                    return stmt;
                }
                ASTNode *stmt = create_node(NODE_REF_ARRAY_ASSIGN);
                stmt->data.var_idx = local_idx; // the parameter's OWN slot, holding a runtime sym_table index
                stmt->op = (TokenType)levels_up;
                stmt->expression_type = ls->type; // element type, for the type checker
                match(TOKEN_LBRACKET);
                stmt->left = expression();  // index
                match(TOKEN_RBRACKET);
                match(TOKEN_ASSIGN);
                stmt->right = wrap_range_check(expression(), ls->is_subrange,
                    ls->subrange_lower, ls->subrange_upper); // value
                return stmt;
            }
            match(TOKEN_IDENTIFIER);
            if (token.type == TOKEN_LBRACKET && (ls->type == TYPE_STRING || ls->type == TYPE_CHAR)) {
                match(TOKEN_LBRACKET);
                ASTNode *stmt = create_node(NODE_LOCAL_STRING_INDEX_ASSIGN);
                stmt->data.var_idx = local_idx;
                stmt->op = (TokenType)levels_up;
                stmt->left = expression();  // index
                match(TOKEN_RBRACKET);
                match(TOKEN_ASSIGN);
                stmt->right = expression(); // new character
                return stmt;
            }
            if (is_dynarray_type(ls->type) && token.type == TOKEN_LBRACKET) {
                int line = token.line;
                match(TOKEN_LBRACKET);
                ASTNode *base = create_node(NODE_LOCAL_VAR);
                base->line = line;
                base->data.var_idx = local_idx;
                base->op = (TokenType)levels_up;
                base->expression_type = ls->type;
                DynArrayTypeDef *d = &dynarray_types[ls->type - TYPE_DYNARRAY_BASE];
                ASTNode *stmt = create_node(NODE_DYNARRAY_ASSIGN);
                stmt->line = line;
                stmt->left = base;
                stmt->right = expression(); // index
                match(TOKEN_RBRACKET);
                match(TOKEN_ASSIGN);
                stmt->extra = wrap_range_check(expression(), d->elem_is_subrange, d->elem_subrange_lower, d->elem_subrange_upper); // value
                stmt->expression_type = d->elem_type;
                return stmt;
            }
            if (is_proc_type(ls->type)) {
                // A NAMED procedural-type local - see the matching
                // global case in parse_global_assignment() for why this
                // branches on token.type == TOKEN_ASSIGN.
                if (token.type == TOKEN_ASSIGN) {
                    ASTNode *stmt = create_node(NODE_LOCAL_ASSIGN);
                    stmt->data.var_idx = local_idx;
                    stmt->op = (TokenType)levels_up;
                    stmt->expression_type = ls->type;
                    match(TOKEN_ASSIGN);
                    stmt->left = parse_proc_value(ls->type - TYPE_PROC_BASE, token.line);
                    return stmt;
                }
                ASTNode *base = create_node(NODE_LOCAL_VAR);
                base->line = token.line;
                base->data.var_idx = local_idx;
                base->op = (TokenType)levels_up;
                base->expression_type = ls->type;
                return build_procvar_call(base, ls->type - TYPE_PROC_BASE, base->line, 1);
            }
            if (is_pointer_type(ls->type) && (token.type == TOKEN_CARET || class_dot_deref_pending(ls->type))) {
                int line = token.line;
                ASTNode *base = create_node(NODE_LOCAL_VAR);
                base->line = line;
                base->data.var_idx = local_idx;
                base->op = (TokenType)levels_up;
                base->expression_type = ls->type;
                HeapDerefStep step;
                base = parse_heap_deref_write(base, line, &step);
                if (step.is_method_call) return step.call_node;
                if (step.is_property_setter) return build_property_setter_call(base, step, line);
                return build_heap_deref_write_statement(base, step);
            }
            ASTNode *stmt = create_node(NODE_LOCAL_ASSIGN);
            stmt->data.var_idx = local_idx;
            stmt->op = (TokenType)levels_up;
            stmt->expression_type = ls->type; // target type, for the type checker
            match(TOKEN_ASSIGN);
            stmt->left = wrap_range_check(expression(), ls->is_subrange,
                ls->subrange_lower, ls->subrange_upper);
            return stmt;
        }

        if (current_class_ptr_idx != -1 && class_has_member(current_class_ptr_idx, token.text)) {
            return parse_self_shorthand_write();
        }

        int proc_idx = find_proc_visible(token.text);
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

    if (token.type == TOKEN_DELETE) {
        match(TOKEN_DELETE);
        match(TOKEN_LPAREN);
        ASTNode *read_node = parse_string_writeback_target("Delete");
        match(TOKEN_COMMA);
        ASTNode *index_expr = expression();
        match(TOKEN_COMMA);
        ASTNode *count_expr = expression();
        match(TOKEN_RPAREN);
        ASTNode *value_node = create_node(NODE_BUILTIN_CALL);
        value_node->op = TOKEN_DELETE;
        value_node->left = read_node;
        value_node->right = index_expr;
        value_node->extra = count_expr;
        value_node->expression_type = TYPE_STRING;
        return string_writeback_assign_node(read_node, value_node);
    }

    if (token.type == TOKEN_INSERT) {
        match(TOKEN_INSERT);
        match(TOKEN_LPAREN);
        ASTNode *source_expr = expression();
        match(TOKEN_COMMA);
        ASTNode *read_node = parse_string_writeback_target("Insert");
        match(TOKEN_COMMA);
        ASTNode *index_expr = expression();
        match(TOKEN_RPAREN);
        ASTNode *value_node = create_node(NODE_BUILTIN_CALL);
        value_node->op = TOKEN_INSERT;
        value_node->left = source_expr;
        value_node->right = read_node;
        value_node->extra = index_expr;
        value_node->expression_type = TYPE_STRING;
        return string_writeback_assign_node(read_node, value_node);
    }

    if (token.type == TOKEN_SETLENGTH) {
        match(TOKEN_SETLENGTH);
        match(TOKEN_LPAREN);
        ASTNode *read_node = parse_dynarray_writeback_target();
        match(TOKEN_COMMA);
        ASTNode *n_expr = expression();
        match(TOKEN_RPAREN);
        ASTNode *value_node = create_node(NODE_BUILTIN_CALL);
        value_node->op = TOKEN_SETLENGTH;
        value_node->left = read_node;
        value_node->right = n_expr;
        value_node->expression_type = read_node->expression_type;
        return dynarray_writeback_assign_node(read_node, value_node);
    }

    if (token.type == TOKEN_BLOCKREAD || token.type == TOKEN_BLOCKWRITE) {
        return parse_block_read_write(token.type == TOKEN_BLOCKREAD);
    }

    if (token.type == TOKEN_FILE_ASSIGN) {
        // 'assign(f, name)' - binds a filename to f. Doesn't open
        // anything yet (matching real Pascal - reset()/rewrite() do
        // that); see NODE_FILE_OP/OP_FILE_ASSIGN.
        match(TOKEN_FILE_ASSIGN);
        match(TOKEN_LPAREN);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "'assign' expects a file variable");
        }
        int fidx = find_file_var_soft(token.text);
        if (fidx == -1) {
            compile_error(token.line, "'%s' is not a declared file variable", token.text);
        }
        match(TOKEN_IDENTIFIER);
        match(TOKEN_COMMA);
        ASTNode *stmt = create_node(NODE_FILE_OP);
        stmt->op = TOKEN_FILE_ASSIGN;
        stmt->data.var_idx = fidx;
        stmt->left = expression(); // filename - must be string/char, checked in type_checker.c
        match(TOKEN_RPAREN);
        return stmt;
    }

    if (token.type == TOKEN_RESET || token.type == TOKEN_REWRITE || token.type == TOKEN_CLOSE) {
        // 'reset(f)' (open for reading) / 'rewrite(f)' (open for
        // writing) / 'close(f)' - each takes exactly one, required,
        // file-variable argument. See NODE_FILE_OP/OP_FILE_RESET/
        // OP_FILE_REWRITE/OP_FILE_CLOSE.
        TokenType kind = token.type;
        const char *name = kind == TOKEN_RESET ? "reset" : kind == TOKEN_REWRITE ? "rewrite" : "close";
        match(kind);
        match(TOKEN_LPAREN);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "'%s' expects a file variable", name);
        }
        int fidx = find_file_var_soft(token.text);
        if (fidx == -1) {
            compile_error(token.line, "'%s' is not a declared file variable", token.text);
        }
        match(TOKEN_IDENTIFIER);
        match(TOKEN_RPAREN);
        ASTNode *stmt = create_node(NODE_FILE_OP);
        stmt->op = kind;
        stmt->data.var_idx = fidx;
        if ((kind == TOKEN_RESET || kind == TOKEN_REWRITE) &&
            (sym_table[fidx].type == TYPE_TYPED_FILE || sym_table[fidx].type == TYPE_UNTYPED_FILE)) {
            // Bakes the file's own record BYTE size in at PARSE time
            // (right is otherwise unused on NODE_FILE_OP) rather than
            // having codegen.c look it up itself - typed_file_vars[] is
            // parser.c-local, the same reason pointer_types[]/
            // record_types[] stay parser.c-local too (see their own
            // comments) - nothing outside this file ever needs to look
            // a typed file up by this index. See codegen.c's
            // TOKEN_RESET/TOKEN_REWRITE branch. byte_size, NOT
            // leaf_count - a byte/shortint/word field's disk width is
            // narrower than sizeof(int), so "leaf count" and "byte
            // count" have diverged since typed_file_vars[].byte_size
            // was introduced; see record_type_byte_size().
            //
            // An UNTYPED file has no fixed record size at all - packs 0,
            // reusing this exact same opcode purely for its "open in
            // binary mode" behavior (see OP_TYPED_FILE_RESET/REWRITE's
            // own comment in common.h). The cached 0 is never read back:
            // OP_FILE_SEEK/OP_FILE_SIZE/OP_TYPED_FILE_EOF (the only
            // consumers of vm_open_files[idx].record_byte_size) aren't
            // supported for untyped files yet (see 'seek'/'filesize'
            // parsing below, which explicitly rejects TYPE_UNTYPED_FILE).
            ASTNode *record_size_lit = create_node(NODE_NUMBER);
            record_size_lit->data.num_value = sym_table[fidx].type == TYPE_TYPED_FILE
                ? typed_file_vars[find_typed_file_var(fidx)].byte_size : 0;
            record_size_lit->expression_type = TYPE_INTEGER;
            stmt->right = record_size_lit;
        }
        return stmt;
    }

    if (token.type == TOKEN_SEEK) {
        // 'seek(f, n)', a typed file only - jumps to record n (0-based).
        // See NODE_FILE_OP/OP_FILE_SEEK.
        match(TOKEN_SEEK);
        match(TOKEN_LPAREN);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "'seek' expects a typed file variable");
        }
        int fidx = find_file_var_soft(token.text);
        if (fidx == -1 || sym_table[fidx].type != TYPE_TYPED_FILE) {
            compile_error(token.line, "'%s' is not a typed file variable", token.text);
        }
        match(TOKEN_IDENTIFIER);
        match(TOKEN_COMMA);
        ASTNode *stmt = create_node(NODE_FILE_OP);
        stmt->op = TOKEN_SEEK;
        stmt->data.var_idx = fidx;
        stmt->left = expression(); // record index - must be integer, checked in type_checker.c
        match(TOKEN_RPAREN);
        return stmt;
    }

    if (token.type == TOKEN_NEW) {
        return parse_new_statement();
    }

    if (token.type == TOKEN_DISPOSE) {
        return parse_dispose_statement();
    }

    if (token.type == TOKEN_ASSERT) {
        match(TOKEN_ASSERT);
        match(TOKEN_LPAREN);
        ASTNode *stmt = create_node(NODE_ASSERT);
        stmt->left = expression(); // condition
        if (token.type == TOKEN_COMMA) {
            match(TOKEN_COMMA);
            stmt->right = expression(); // message
        } else {
            // No message given - synthesize a default literal, exactly
            // like a user-written string, so codegen never needs to
            // handle a "no message" case separately.
            ASTNode *msg = create_node(NODE_STRING);
            msg->data.var_idx = intern_string("Assertion failed");
            msg->expression_type = TYPE_STRING;
            stmt->right = msg;
        }
        match(TOKEN_RPAREN);
        return stmt;
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
                // An optional leading file variable - 'write(f, a, b)'
                // writes to f instead of stdout. Detected via a soft
                // lookup (not every identifier here is one). 'write(f)'
                // (a file with nothing else) is valid too - the comma is
                // only required when more arguments follow, matching
                // real Pascal's 'writeln(f);' (just a newline to f).
                if (token.type == TOKEN_IDENTIFIER) {
                    int fidx = find_file_var_soft(token.text);
                    if (fidx != -1) {
                        if (sym_table[fidx].type == TYPE_TYPED_FILE) {
                            // A typed file write is a GENUINELY separate
                            // mechanism (raw binary transfer, single
                            // target only) - branch off entirely, same
                            // reasoning as parse_read_statement()'s own
                            // typed-file branch.
                            if (kind == TOKEN_WRITELN) {
                                compile_error(token.line, "'writeln' doesn't apply to a typed file - use 'write' instead");
                            }
                            match(TOKEN_IDENTIFIER);
                            match(TOKEN_COMMA);
                            ASTNode *result = parse_typed_file_write(&typed_file_vars[find_typed_file_var(fidx)]);
                            match(TOKEN_RPAREN);
                            return result;
                        }
                        if (sym_table[fidx].type == TYPE_UNTYPED_FILE) {
                            // Plain 'write'/'writeln' don't apply to an
                            // untyped file at all - use 'BlockWrite'
                            // instead. Same reasoning as
                            // parse_read_statement()'s own untyped-file
                            // rejection.
                            compile_error(token.line, "'%s' doesn't apply to an untyped file - use 'BlockWrite' instead",
                                           kind == TOKEN_WRITELN ? "writeln" : "write");
                        }
                        match(TOKEN_IDENTIFIER);
                        ASTNode *file_ref = create_node(NODE_VARIABLE);
                        file_ref->data.var_idx = fidx;
                        file_ref->expression_type = TYPE_FILE;
                        stmt->extra = file_ref;
                        if (token.type == TOKEN_COMMA) {
                            match(TOKEN_COMMA);
                        }
                    }
                }
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
            }
            match(TOKEN_RPAREN);
        }
        return stmt;
    }

    if (token.type == TOKEN_READLN) {
        return parse_read_statement(1);
    }

    if (token.type == TOKEN_READ) {
        return parse_read_statement(0);
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
        match(TOKEN_FOR);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "'for' expects a variable identifier");
        }
        int with_field_idx = find_with_field(token.text);
        int rv_is_local = 0, rv_record_type_idx = 0;
        const int *rv_field_idx_arr = NULL;
        int rv_found = (with_field_idx == -1) && find_any_record_var(token.text, &rv_is_local, &rv_record_type_idx, &rv_field_idx_arr);
        if (with_field_idx != -1 || rv_found) {
            int field_sym_idx = -1;   // valid if with-field or global record field
            int field_local_idx = -1; // valid if local record field
            int is_local_target = 0;
            if (with_field_idx != -1) {
                field_sym_idx = with_field_idx;
                match(TOKEN_IDENTIFIER);
            } else {
                char rec_name[MAX_NAME];
                strcpy(rec_name, token.text);
                match(TOKEN_IDENTIFIER);
                if (token.type != TOKEN_PERIOD) {
                    compile_error(token.line, "'%s' is a record - 'for' expects a field, e.g. '%s.field'", rec_name, rec_name);
                }
                match(TOKEN_PERIOD);
                if (token.type != TOKEN_IDENTIFIER) {
                    compile_error(token.line, "Expected a field name after '%s.'", rec_name);
                }
                int field_idx = find_record_field(rv_record_type_idx, token.text);
                if (field_idx == -1) {
                    compile_error(token.line, "'%s' is not a field of '%s'", token.text, rec_name);
                }
                match(TOKEN_IDENTIFIER);
                if (rv_is_local) {
                    is_local_target = 1;
                    field_local_idx = rv_field_idx_arr[field_idx];
                } else {
                    field_sym_idx = rv_field_idx_arr[field_idx];
                }
            }
            if (is_local_target) {
                if (token.type == TOKEN_IN) {
                    return parse_for_in_tail_local(field_local_idx);
                }
                if (current_locals[field_local_idx].type != TYPE_INTEGER) {
                    compile_error(token.line, "'for' loop variable must be integer");
                }
                return parse_local_for_tail(field_local_idx);
            }
            if (token.type == TOKEN_IN) {
                return parse_for_in_tail_global(field_sym_idx);
            }
            if (sym_table[field_sym_idx].type != TYPE_INTEGER) {
                compile_error(token.line, "'for' loop variable must be integer");
            }
            ASTNode *stmt = create_node(NODE_FOR);
            stmt->data.var_idx = field_sym_idx;
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
        // NODE_LOCAL_FOR's own ->op already carries TOKEN_TO/TOKEN_DOWNTO
        // (see parse_local_for_tail()/parse_for_in_tail_local()), so it
        // has nowhere to also carry a levels_up tag - a 'for' loop
        // counter must be the CURRENT procedure's own local, never an
        // enclosing scope's (a documented known gap - standard Pascal
        // requires the counter be local to the enclosing block anyway).
        // find_local_outward() is used only so this can be detected and
        // reported clearly, rather than silently falling through to
        // "undefined variable" below.
        {
            int outer_levels_up;
            if (find_local_outward(token.text, &outer_levels_up) != -1 && outer_levels_up > 0) {
                compile_error(token.line, "'for' loop variable can't be an enclosing procedure's local '%s' yet - use one of this procedure's own locals as the loop counter instead", token.text);
            }
        }
        int local_idx = find_local(token.text);
        if (local_idx != -1 && current_locals[local_idx].is_var_param) {
            compile_error(token.line, "'for' loop variable can't be a 'var' parameter yet - assign it to a plain local first");
        }
        if (local_idx != -1 && current_locals[local_idx].is_static) {
            // A static local behaves exactly like a global here - plain
            // storage, no per-call frame to isolate - so this reuses the
            // ordinary NODE_FOR shape (the global fallback further
            // below), just targeting the mangled global's index instead
            // of find_var()'s.
            int static_idx = current_locals[local_idx].static_sym_idx;
            match(TOKEN_IDENTIFIER);
            if (token.type == TOKEN_IN) {
                return parse_for_in_tail_global(static_idx);
            }
            if (sym_table[static_idx].type != TYPE_INTEGER) {
                compile_error(token.line, "'for' loop variable must be integer");
            }
            ASTNode *stmt = create_node(NODE_FOR);
            stmt->data.var_idx = static_idx;
            match(TOKEN_ASSIGN);
            stmt->left = expression(); // start bound
            if (token.type == TOKEN_TO) {
                match(TOKEN_TO);
                stmt->op = TOKEN_TO;
            } else if (token.type == TOKEN_DOWNTO) {
                match(TOKEN_DOWNTO);
                stmt->op = TOKEN_DOWNTO;
            } else {
                compile_error(token.line, "'for' expects 'to' or 'downto'");
            }
            stmt->right = expression(); // end bound
            match(TOKEN_DO);
            loop_depth++;
            stmt->extra = statement();  // body
            loop_depth--;
            return stmt;
        }
        if (local_idx != -1) {
            match(TOKEN_IDENTIFIER);
            if (token.type == TOKEN_IN) {
                return parse_for_in_tail_local(local_idx);
            }
            if (current_locals[local_idx].type != TYPE_INTEGER) {
                compile_error(token.line, "'for' loop variable must be integer");
            }
            return parse_local_for_tail(local_idx);
        }
        int global_idx = find_var(token.text);
        match(TOKEN_IDENTIFIER);
        if (token.type == TOKEN_IN) {
            return parse_for_in_tail_global(global_idx);
        }
        if (sym_table[global_idx].type != TYPE_INTEGER) {
            compile_error(token.line, "'for' loop variable must be integer");
        }
        ASTNode *stmt = create_node(NODE_FOR);
        stmt->data.var_idx = global_idx;
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

    if (token.type == TOKEN_CASE) {
        return parse_case_statement();
    }

    if (token.type == TOKEN_TRY) {
        ASTNode *stmt = create_node(NODE_TRY);
        match(TOKEN_TRY);
        stmt->left = statement_list();   // try-body
        if (token.type == TOKEN_FINALLY) {
            // 'try <body> finally <cleanup> end' - a SEPARATE construct
            // from 'try <body> except <handler> end', never combined in
            // one block (matches Delphi - nest to get both). Discriminated
            // from an ordinary try/except purely via 'op', which NODE_TRY
            // never otherwise sets/reads - see codegen.c's own NODE_TRY
            // case for why the cleanup body gets parsed once but compiled
            // twice.
            match(TOKEN_FINALLY);
            stmt->op = TOKEN_FINALLY;
            finally_body_depth++;
            stmt->right = statement_list();  // cleanup body
            finally_body_depth--;
        } else {
            match(TOKEN_EXCEPT);
            stmt->right = statement_list();  // except-body
        }
        match(TOKEN_END);
        return stmt;
    }

    if (token.type == TOKEN_RAISE) {
        ASTNode *stmt = create_node(NODE_RAISE);
        match(TOKEN_RAISE);
        stmt->left = expression();       // message
        return stmt;
    }

    if (token.type == TOKEN_WARNING) {
        match(TOKEN_WARNING);
        match(TOKEN_LPAREN);
        ASTNode *stmt = create_node(NODE_WARNING);
        stmt->left = expression(); // message
        match(TOKEN_RPAREN);
        return stmt;
    }

    if (token.type == TOKEN_WITH) {
        // 'with recordVar[, recordVar...] do statement;' - pure
        // parser-time sugar, no AST node of its own: pushing each
        // target's rv_idx onto with_stack (see the comment above it)
        // makes every bare field name inside the body resolve exactly
        // as 'recordVar.field' already would, via the find_with_field()
        // checks now threaded through every identifier-resolution call
        // site. The body's own parsed AST is returned completely
        // unwrapped - this statement contributes nothing to the tree
        // beyond whatever 'statement()' below produces on its own.
        //
        // A comma-separated target list pushes one entry per target, in
        // order, so a later target's field shadows an earlier target's
        // same-named field - find_with_field() already scans innermost-
        // to-outermost, so this matches 'with a do with b do ...'
        // exactly without any change to the stack or its lookup.
        match(TOKEN_WITH);
        int pushed = 0;
        for (;;) {
            if (token.type != TOKEN_IDENTIFIER) {
                compile_error(token.line, "'with' expects a record variable");
            }
            {
                int rv_is_local, rv_record_type_idx;
                const int *rv_field_idx;
                if (!find_any_record_var(token.text, &rv_is_local, &rv_record_type_idx, &rv_field_idx)) {
                    compile_error(token.line, "'%s' is not a record variable", token.text);
                }
                if (rv_is_local) {
                    compile_error(token.line, "'with' doesn't support a local record variable or parameter yet - access its fields directly (e.g. '%s.field')", token.text);
                }
                if (record_type_has_nested_field(rv_record_type_idx)) {
                    // find_with_field() binds a bare name straight to
                    // field_sym_idx[field_idx] as if it were always a leaf -
                    // a nested-record field's base isn't a valid scalar/
                    // array reference on its own, so 'with' can't accept a
                    // record type that has one yet.
                    compile_error(token.line, "'with' doesn't support a record with a nested-record field yet - access its fields directly (e.g. '%s.field.subfield')", token.text);
                }
            }
            int rv_idx = find_record_var(token.text);
            match(TOKEN_IDENTIFIER);
            if (with_depth >= MAX_WITH_DEPTH) {
                compile_error(token.line, "'with' statements nested too deeply (limit is %d)", MAX_WITH_DEPTH);
            }
            with_stack[with_depth++] = rv_idx;
            pushed++;
            if (token.type != TOKEN_COMMA) break;
            match(TOKEN_COMMA);
        }
        match(TOKEN_DO);
        ASTNode *body = statement();
        with_depth -= pushed;
        return body;
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

    if (token.type == TOKEN_RANDOMIZE) {
        ASTNode *stmt = create_node(NODE_RANDOMIZE);
        match(TOKEN_RANDOMIZE);
        return stmt;
    }

    if (token.type == TOKEN_EXIT) {
        match(TOKEN_EXIT);
        ASTNode *stmt = create_node(NODE_EXIT);
        if (token.type == TOKEN_LPAREN) {
            if (current_function_idx == -1 || !proc_table[current_function_idx].is_function) {
                compile_error(token.line, "'exit' with a value is only allowed inside a function");
            }
            match(TOKEN_LPAREN);
            stmt->left = build_return_assign_node(current_function_idx);
            match(TOKEN_RPAREN);
        }
        return stmt;
    }

    if (token.type == TOKEN_HALT) {
        match(TOKEN_HALT);
        ASTNode *stmt = create_node(NODE_HALT);
        if (token.type == TOKEN_LPAREN) {
            match(TOKEN_LPAREN);
            stmt->left = expression();
            match(TOKEN_RPAREN);
        }
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

