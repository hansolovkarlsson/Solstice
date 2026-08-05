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
    DataType type;
    int is_array;
    int array_lower, array_upper; // only meaningful if is_array
    int is_subrange;      // see the Symbol comment in common.h - propagated
    int subrange_lower;   // to the field's mangled global Symbol by
    int subrange_upper;   // add_record_var()
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

// A specific pointer type is encoded as TYPE_POINTER_BASE + its
// pointer_types[] index (see the comment in common.h) - a BOUNDED range
// check, exactly like type_checker.c's own copy of this same helper
// (duplicated per this project's established "small helpers live in
// each file that needs them" convention rather than sharing one via a
// header - see e.g. bits_to_float in vm.c/optimizer.c).
static int is_pointer_type(DataType t) {
    return t >= TYPE_POINTER_BASE && t < TYPE_POINTER_BASE + MAX_POINTER_TYPES;
}

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
#define MAX_POINTER_DECLS MAX_POINTER_TYPES
typedef struct {
    char name[MAX_NAME];        // the pointer TYPE's own name, e.g. "PNode"
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
} PointerTypeDef;
static PointerTypeDef pointer_types[MAX_POINTER_DECLS];
static int pointer_type_count = 0;

static int find_pointer_type(const char *name) {
    for (int i = 0; i < pointer_type_count; i++) {
        if (strcmp(pointer_types[i].name, name) == 0) return i;
    }
    return -1;
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
    for (int i = 0; i < local_record_var_count; i++) {
        if (strcmp(local_record_vars[i].name, name) == 0) {
            *is_local = 1;
            *record_type_idx = local_record_vars[i].record_type_idx;
            *field_idx_array = local_record_vars[i].field_local_idx;
            return 1;
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
// against sym_table[]).
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

// Soft lookup for a GLOBAL file variable specifically (files are always
// global - see TYPE_FILE) - returns its sym_table[] index, or -1 if
// `name` isn't a declared file variable at all (not an error - this is
// used everywhere a leading 'read(f, ...)'/'write(f, ...)'/'eof(f)'/
// 'assign(f, ...)' file argument needs to be *detected*, not required,
// falling back to the ordinary stdin/stdout path when it's absent).
static int find_file_var_soft(const char *name) {
    int idx = find_var_soft(name);
    if (idx == -1 || sym_table[idx].type != TYPE_FILE) return -1;
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
    sym_table[sym_count].type = type;
    sym_table[sym_count].is_array = 0;
    sym_table[sym_count].array_lower = 0;
    sym_table[sym_count].array_upper = 0;
    sym_table[sym_count].array_base = 0;
    sym_table[sym_count].is_subrange = 0; // defensive reset (see comment above the Symbol struct)
    sym_table[sym_count].subrange_lower = 0;
    sym_table[sym_count].subrange_upper = 0;
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
    }
    int size = (upper - lower + 1) * rt->field_count;
    if (array_mem_count + size > MAX_ARRAY_MEM) {
        compile_error(token.line, "Array storage exhausted (limit is %d total elements across all arrays)", MAX_ARRAY_MEM);
    }
    strcpy(sym_table[sym_count].name, name);
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
        if (f->is_array) {
            add_array_var(mangled, f->type, f->array_lower, f->array_upper);
        } else {
            add_var(mangled, f->type);
        }
        sym_table[sym_count - 1].is_subrange = f->is_subrange;
        sym_table[sym_count - 1].subrange_lower = f->subrange_lower;
        sym_table[sym_count - 1].subrange_upper = f->subrange_upper;
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
    int global_idx = find_var_soft(name);
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
    return current_local_count++;
}

// Registers a general by-reference SCALAR parameter ('var name: type').
// Just an ordinary local slot (like every other parameter) - it holds an
// ENCODED REFERENCE, not the value itself, so every access must go
// through NODE_VAR_PARAM_READ/ASSIGN instead of the plain
// NODE_LOCAL_VAR/ASSIGN a by-value scalar parameter would use. See
// param_is_var's comment in common.h for the full design.
static int add_local_var_param(const char *name, DataType type, int is_subrange, int subrange_lower, int subrange_upper) {
    int idx = add_local(name, type);
    current_locals[idx].is_var_param = 1;
    current_locals[idx].is_subrange = is_subrange;
    current_locals[idx].subrange_lower = subrange_lower;
    current_locals[idx].subrange_upper = subrange_upper;
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

// Registers a record LOCAL or PARAMETER ('var p: TPoint;' inside a
// procedure body, or 'procedure foo(p: TPoint)') - see the comment
// above LocalRecordVarDef for why each field gets its own ordinary
// frame slot (add_local()) instead of a hidden global. Used for BOTH
// locals and parameters identically; a parameter's fields additionally
// get populated by copy-in code at each call site (by value) - see
// parse_call_arguments()'s record-argument handling.
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
        int idx = add_local(mangled, f->type);
        current_locals[idx].is_subrange = f->is_subrange;
        current_locals[idx].subrange_lower = f->subrange_lower;
        current_locals[idx].subrange_upper = f->subrange_upper;
        rv->field_local_idx[i] = idx;
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

// A scalar type - one of the five built-in keywords, a previously
// declared type alias, enumerated type, subrange type, or 'set of ...'
// resolving to one of them (see TypeAliasDef/EnumTypeDef/SubrangeTypeDef/
// TYPE_SET above). This is the one centralized function every scalar-
// type call site goes through (parameters, procedure-locals, record
// fields, function return types, and plain/array var declarations), so
// alias/enum/subrange/set support and any future scalar-type keyword
// only needs to be added here once.
static DataType parse_scalar_type(void) {
    scalar_type_is_subrange = 0;
    if (token.type == TOKEN_INTEGER) { match(TOKEN_INTEGER); return TYPE_INTEGER; }
    if (token.type == TOKEN_BOOLEAN) { match(TOKEN_BOOLEAN); return TYPE_BOOLEAN; }
    if (token.type == TOKEN_STRING_TYPE) { match(TOKEN_STRING_TYPE); return TYPE_STRING; }
    if (token.type == TOKEN_CHAR_TYPE) { match(TOKEN_CHAR_TYPE); return TYPE_CHAR; }
    if (token.type == TOKEN_REAL_TYPE) { match(TOKEN_REAL_TYPE); return TYPE_REAL; }
    if (token.type == TOKEN_SET) {
        match(TOKEN_SET);
        match(TOKEN_OF);
        parse_set_base_type();
        return TYPE_SET;
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
    }
    compile_error(token.line, "Unknown type (expected 'integer', 'boolean', 'string', 'char', 'real', 'set of ...', a declared type alias, an enumerated type, a subrange type, or a pointer type)");
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
    return g;
}

static ASTNode *expression(void);
static ASTNode *statement(void);
static ASTNode *statement_list(void);
static ASTNode *compound_statement(void);
static void subroutine_declaration(int is_function_decl);

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
        ASTNode *value = record_field_read_node(arg_is_local, arg_field_idx[i], arg_levels_up);
        value = wrap_range_check(value, rt->fields[i].is_subrange, rt->fields[i].subrange_lower, rt->fields[i].subrange_upper);
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
static ASTNode *parse_var_argument(int proc_idx, int param_index) {
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Parameter %d of '%s' is a 'var' parameter - expects a variable, not an expression",
                       param_index + 1, proc_table[proc_idx].name);
    }
    char name[MAX_NAME];
    int line = token.line;
    strcpy(name, token.text);

    DataType expected_type = proc_table[proc_idx].param_types[param_index];

    int with_field_idx = find_with_field(name);
    if (with_field_idx != -1) {
        match(TOKEN_IDENTIFIER);
        if (sym_table[with_field_idx].type != expected_type) {
            compile_error(line, "'var' argument '%s' has the wrong type for parameter %d of '%s'",
                           name, param_index + 1, proc_table[proc_idx].name);
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
                               param_index + 1, proc_table[proc_idx].name);
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
                               name, param_index + 1, proc_table[proc_idx].name);
            }
            ASTNode *node = create_node(NODE_VAR_REF);
            node->data.var_idx = static_idx;
            node->expression_type = expected_type;
            return node;
        }
        if (ls->type != expected_type) {
            compile_error(line, "'var' argument '%s' has the wrong type for parameter %d of '%s'",
                           name, param_index + 1, proc_table[proc_idx].name);
        }
        if (ls->is_var_param) {
            // Forwarding: this slot already holds a valid reference (from
            // this procedure's OWN caller) - pass it through unchanged.
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
                       name, param_index + 1, proc_table[proc_idx].name);
    }
    ASTNode *node = create_node(NODE_VAR_REF);
    node->data.var_idx = global_idx;
    node->expression_type = expected_type;
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
                ASTNode *arg;
                ASTNode *this_tail;
                if (arg_count < proc_table[proc_idx].param_count && proc_table[proc_idx].param_is_array_ref[arg_count]) {
                    arg = parse_array_ref_argument(proc_idx, arg_count);
                    this_tail = arg;
                } else if (arg_count < proc_table[proc_idx].param_count && proc_table[proc_idx].param_is_record[arg_count]) {
                    arg = parse_record_argument(proc_idx, arg_count, &this_tail);
                } else if (arg_count < proc_table[proc_idx].param_count && proc_table[proc_idx].param_is_var[arg_count]) {
                    arg = parse_var_argument(proc_idx, arg_count);
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
    if (arg_count != proc_table[proc_idx].param_count) {
        compile_error(token.line, "'%s' expects %d argument(s), got %d",
                       proc_table[proc_idx].name, proc_table[proc_idx].param_count, arg_count);
    }
    return arg_head;
}

// One resolved '^' step's outcome - a record field's offset/type/subrange
// info (0/scalar-target-type/not-subrange for a scalar pointer target's
// bare '^', which behaves exactly like a 1-field, unnamed record for
// this purpose).
typedef struct {
    int field_offset;
    DataType result_type;
    int is_subrange;
    int subrange_lower;
    int subrange_upper;
} HeapDerefStep;

// Resolves ONE '^' step already matched (the caller has confirmed
// is_pointer_type(base_type)) - '.field' if the target is a record,
// nothing more if it's a scalar. Shared by parse_heap_deref_read()/
// parse_heap_deref_write() below - both walk an arbitrary-depth '^'
// chain ('p^.next^.next^.data'), differing only in whether the FINAL
// step becomes a read (NODE_HEAP_FIELD_ACCESS) or is left for the caller
// to build into a write (NODE_HEAP_FIELD_ASSIGN).
static HeapDerefStep resolve_heap_deref_step(DataType base_type) {
    HeapDerefStep step;
    PointerTypeDef *pt = &pointer_types[base_type - TYPE_POINTER_BASE];
    if (pt->target_is_record) {
        if (token.type != TOKEN_PERIOD) {
            compile_error(token.line, "'...^' is a pointer to a record - access a field, e.g. '...^.field'");
        }
        match(TOKEN_PERIOD);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "Expected a field name after '^.'");
        }
        int field_idx = find_record_field(pt->target_record_type_idx, token.text);
        if (field_idx == -1) {
            compile_error(token.line, "'%s' is not a field of '%s'", token.text, record_types[pt->target_record_type_idx].name);
        }
        match(TOKEN_IDENTIFIER);
        RecordField *f = &record_types[pt->target_record_type_idx].fields[field_idx];
        step.field_offset = field_idx;
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
// confirmed token.type == TOKEN_CARET. Loops so an arbitrary-depth chain
// is handled uniformly: each '^' step wraps the previous one in a
// NODE_HEAP_FIELD_ACCESS, which becomes 'base' for the next '^' if the
// field just read is itself pointer-typed and another '^' follows -
// re-validated via is_pointer_type() on every iteration (not just the
// first), so 'x^^' where x^ isn't itself a pointer is a clean Compile
// Error, not an out-of-bounds pointer_types[] read.
static ASTNode *parse_heap_deref_read(ASTNode *base, int line) {
    while (token.type == TOKEN_CARET) {
        if (!is_pointer_type(base->expression_type)) {
            compile_error(token.line, "Cannot dereference a non-pointer value with '^'");
        }
        match(TOKEN_CARET);
        HeapDerefStep step = resolve_heap_deref_step(base->expression_type);
        base = make_heap_field_access(base, step, line);
    }
    return base;
}

// Same chain-walking as parse_heap_deref_read() above, but stops right
// before consuming the FINAL '^' - a write needs to build a
// NODE_HEAP_FIELD_ASSIGN for that last step (base + field_offset + value
// expression), not another NODE_HEAP_FIELD_ACCESS. Assumes the caller has
// already confirmed token.type == TOKEN_CARET. Returns the base
// expression the LAST step reads through, and that step's own
// HeapDerefStep (field offset/type/subrange info) via *out_step.
static ASTNode *parse_heap_deref_write(ASTNode *base, int line, HeapDerefStep *out_step) {
    for (;;) {
        if (!is_pointer_type(base->expression_type)) {
            compile_error(token.line, "Cannot dereference a non-pointer value with '^'");
        }
        match(TOKEN_CARET);
        HeapDerefStep step = resolve_heap_deref_step(base->expression_type);
        if (token.type == TOKEN_CARET) {
            base = make_heap_field_access(base, step, line);
            continue;
        }
        *out_step = step;
        return base;
    }
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
        compile_error(token.line, "'%s' is not an array, cannot be indexed", sym_table[idx].name);
    }
    ASTNode *node = create_node(NODE_VARIABLE);
    node->line = line;
    node->data.var_idx = idx;
    node->expression_type = sym_table[idx].type;
    if (is_pointer_type(node->expression_type) && token.type == TOKEN_CARET) {
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
        if (rt->fields[i].is_array) {
            compile_error(token.line, "Cannot compare record '%s': field '%s' is an array, and this compiler doesn't support whole-array comparison",
                           rec_name, rt->fields[i].name);
        }
        ASTNode *left = record_field_read_node(is_local1, field_idx1[i], levels_up1);
        ASTNode *right = record_field_read_node(is_local2, field_idx2[i], levels_up2);
        ASTNode *eq = create_node(NODE_BINARY_OP);
        eq->op = TOKEN_EQ;
        eq->left = left;
        eq->right = right;
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

static ASTNode *factor(void) {
    if (token.type == TOKEN_MINUS || token.type == TOKEN_NOT) {
        TokenType op = token.type;
        match(op);
        ASTNode *node = create_node(NODE_UNARY_OP);
        node->op = op;
        node->left = factor();
        return node;
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
                    match(TOKEN_IDENTIFIER);
                    ASTNode *file_ref = create_node(NODE_VARIABLE);
                    file_ref->data.var_idx = fidx;
                    file_ref->expression_type = TYPE_FILE;
                    node->left = file_ref;
                }
            }
            match(TOKEN_RPAREN);
        }
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
        // constant. Only supports arrays (not e.g. an ordinal type name)
        // in this increment.
        TokenType kind = token.type;
        match(kind);
        match(TOKEN_LPAREN);
        int lower, upper;
        if (!try_get_array_bounds_here(&lower, &upper)) {
            compile_error(token.line, "'%s' requires an array argument", kind == TOKEN_LOW ? "low" : "high");
        }
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
                if (token.type != TOKEN_IDENTIFIER) {
                    compile_error(token.line, "Expected a field name after '%s.'", rec_name);
                }
                int field_idx = find_record_field(rv_record_type_idx, token.text);
                if (field_idx == -1) {
                    compile_error(token.line, "'%s' is not a field of '%s'", token.text, rec_name);
                }
                match(TOKEN_IDENTIFIER);
                if (rv_is_local) {
                    ASTNode *node = record_field_read_node(1, rv_field_idx[field_idx], rv_levels_up);
                    node->line = line;
                    return node;
                }
                return parse_global_symbol_reference(rv_field_idx[field_idx], line);
            }
        }

        int levels_up;
        int local_idx = find_local_outward(token.text, &levels_up);
        if (local_idx != -1) {
            LocalSymbol *ls = local_at(local_idx, levels_up);
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
                if (is_pointer_type(node->expression_type) && token.type == TOKEN_CARET) {
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
            ASTNode *node = create_node(NODE_LOCAL_VAR);
            node->line = line;
            node->data.var_idx = local_idx;
            node->op = (TokenType)levels_up;
            node->expression_type = ls->type;
            if (is_pointer_type(node->expression_type) && token.type == TOKEN_CARET) {
                return parse_heap_deref_read(node, line);
            }
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

// Parses a 'type' section: one or more 'TypeName = record ... end;'
// declarations. Only record types exist right now - there's no type
// aliasing ('type TAge = integer;') and no nested records (a field can't
// itself be a record type).
// 'const Name1 = expr1; Name2 = expr2; ...' - see the comment above
// ConstDef for the overall approach.
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

static void parse_type_section(void) {
    match(TOKEN_TYPE);
    while (token.type == TOKEN_IDENTIFIER) {
        int line = token.line;
        char type_name[MAX_NAME];
        strcpy(type_name, token.text);
        if (find_record_type(type_name) != -1 || find_type_alias(type_name) != -1
            || find_enum_type(type_name) != -1 || find_subrange_type(type_name) != -1
            || find_pointer_type(type_name) != -1) {
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
            if (token.type == TOKEN_IDENTIFIER && find_record_type(token.text) != -1) {
                int record_idx = find_record_type(token.text);
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
            DataType field_type = parse_scalar_type();
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
                f->is_subrange = scalar_type_is_subrange;
                f->subrange_lower = scalar_type_subrange_lower;
                f->subrange_upper = scalar_type_subrange_upper;
                rt->field_count++;
            }
        }

        match(TOKEN_END);
        match(TOKEN_SEMI);
        record_type_count++;
    }

    // Resolve every pointer type left pending (forward-referencing a
    // record type by name - see the TOKEN_CARET branch above) now that
    // every type this section declares, in any order, is known.
    for (int i = 0; i < pointer_type_count; i++) {
        PointerTypeDef *pt = &pointer_types[i];
        if (!pt->is_pending) continue;
        int record_idx = find_record_type(pt->pending_target_name);
        if (record_idx == -1) {
            compile_error(pt->pending_line, "Pointer type '%s' targets undeclared type '%s'", pt->name, pt->pending_target_name);
        }
        pt->target_is_record = 1;
        pt->target_record_type_idx = record_idx;
        pt->target_elem_size = record_types[record_idx].field_count;
        pt->is_pending = 0;
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
    record_type_count = 0;
    record_var_count = 0;
    record_array_count = 0;
    pointer_type_count = 0;
    const_def_count = 0;
    type_alias_count = 0;
    enum_type_count = 0;
    subrange_type_count = 0;
    with_depth = 0;
    declared_label_count = 0;
    init_lexer(source);
    match(TOKEN_PROGRAM);
    match(TOKEN_IDENTIFIER);
    match(TOKEN_SEMI);

    if (token.type == TOKEN_LABEL) {
        parse_label_section();
    }

    if (token.type == TOKEN_CONST) {
        parse_const_section();
    }

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

    // The main program's own label declarations (if any) must survive
    // parsing every procedure/function below - each one resets and
    // reuses this same static declared_labels table for its own,
    // independent label namespace (see subroutine_declaration()).
    // Stashed here, restored just before parsing the main body itself.
    DeclaredLabel main_labels[MAX_DECLARED_LABELS];
    int main_label_count = declared_label_count;
    memcpy(main_labels, declared_labels, sizeof(DeclaredLabel) * declared_label_count);

    while (token.type == TOKEN_PROCEDURE || token.type == TOKEN_FUNCTION) {
        subroutine_declaration(token.type == TOKEN_FUNCTION);
    }

    for (int i = 0; i < proc_count; i++) {
        if (proc_table[i].is_forward) {
            compile_error(token.line, "%s '%s' was forward-declared but never defined",
                           proc_table[i].is_function ? "Function" : "Procedure", proc_table[i].name);
        }
    }

    memcpy(declared_labels, main_labels, sizeof(DeclaredLabel) * main_label_count);
    declared_label_count = main_label_count;

    ASTNode *root = compound_statement();
    check_all_labels_defined();
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
                      node->type == NODE_LOCAL_VAR_REF) ? (int)node->op : 0;

    if (node->type == NODE_LOCAL_VAR) {
        if (levels_up == 0) read_flag[node->data.var_idx] = 1;
    } else if (node->type == NODE_LOCAL_ASSIGN || node->type == NODE_LOCAL_FOR ||
               node->type == NODE_LOCAL_READLN || node->type == NODE_LOCAL_VAR_REF) {
        // NODE_LOCAL_VAR_REF (passing this local by reference to another
        // procedure as a 'var' argument) is conservatively treated as an
        // assignment too - the callee might set it through that
        // reference, and this pass would rather miss a real bug than
        // wrongly warn about a value a callee legitimately provides.
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
}

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
            } else if (proc_table[proc_idx].param_is_var[i]) {
                add_local_var_param(proc_table[proc_idx].param_names[i], proc_table[proc_idx].param_types[i],
                                     proc_table[proc_idx].param_is_subrange[i], proc_table[proc_idx].param_subrange_lower[i],
                                     proc_table[proc_idx].param_subrange_upper[i]);
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

        if (token.type == TOKEN_LPAREN) {
            match(TOKEN_LPAREN);
            if (token.type != TOKEN_RPAREN) {
                while (1) {
                    // 'var' is a per-group modifier here (inside the
                    // parameter list), unlike the 'var' KEYWORD that
                    // introduces the whole local-variable SECTION below -
                    // same token, different grammar position, so this
                    // check only fires here, once per semicolon-separated
                    // parameter group.
                    int is_var_group = 0;
                    if (token.type == TOKEN_VAR) {
                        is_var_group = 1;
                        match(TOKEN_VAR);
                    }
                    NameGroup g = parse_name_group();
                    for (int i = 0; i < g.count; i++) {
                        if (param_count >= MAX_PARAMS) {
                            compile_error(token.line, "Too many parameters (limit is %d)", MAX_PARAMS);
                        }
                        if (is_var_group && g.is_record) {
                            compile_error(token.line, "'var' doesn't support whole records yet - only a scalar 'var' parameter is supported (see docs/LANGUAGE.md)");
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
                            add_local_var_param(g.names[i], g.type, g.is_subrange, g.subrange_lower, g.subrange_upper);
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
                        param_record_field_count[param_count] = g.is_record ? record_types[g.record_type_idx].field_count : 0;
                        param_is_var[param_count] = is_var_group;
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

    if (!completing_forward && token.type == TOKEN_FORWARD) {
        match(TOKEN_FORWARD);
        match(TOKEN_SEMI);
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

    while (token.type == TOKEN_PROCEDURE || token.type == TOKEN_FUNCTION) {
        subroutine_declaration(token.type == TOKEN_FUNCTION);
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

// 'case selector of label1[, label2...]: statement1; ... [else
// statementN] end' - see the NODE_CASE/NODE_CASE_ARM comments in
// common.h for the AST shape this builds. Case-label constants must be
// pairwise distinct across the WHOLE statement (checked here, at parse
// time - comparing (type, value) pairs is all that's needed, and needs
// nothing the selector's own type resolution would add). Whether each
// label's type actually matches the selector is checked later, by
// type_checker.c, once the selector is fully resolved.
static ASTNode *parse_case_statement(void) {
    match(TOKEN_CASE);
    ASTNode *node = create_node(NODE_CASE);
    node->left = expression();
    match(TOKEN_OF);

    DataType seen_types[MAX_CASE_LABELS];
    int seen_values[MAX_CASE_LABELS];
    int seen_count = 0;

    ASTNode *arm_head = NULL;
    ASTNode *arm_tail = NULL;
    while (token.type != TOKEN_ELSE && token.type != TOKEN_END) {
        ASTNode *label_head = NULL;
        ASTNode *label_tail = NULL;
        while (1) {
            int label_line = token.line;
            ASTNode *label = parse_case_label_value();
            for (int i = 0; i < seen_count; i++) {
                if (seen_types[i] == label->expression_type && seen_values[i] == label->data.num_value) {
                    compile_error(label_line, "Duplicate case label");
                }
            }
            if (seen_count >= MAX_CASE_LABELS) {
                compile_error(label_line, "Too many case labels in one 'case' statement (limit is %d)", MAX_CASE_LABELS);
            }
            seen_types[seen_count] = label->expression_type;
            seen_values[seen_count] = label->data.num_value; // aliases data.var_idx too (same union member) - fine for a char label, which sets var_idx instead
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
            if (is_pointer_type(sym_table[with_field_idx].type)) {
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
                if (is_pointer_type(current_locals[resolved_idx].type)) {
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
            if (is_pointer_type(sym_table[resolved_idx].type)) {
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
            if (is_pointer_type(sym_table[static_idx].type)) {
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
        if (is_pointer_type(current_locals[local_idx].type)) {
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
    if (is_pointer_type(sym_table[readln_var_idx].type)) {
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
           t == TOKEN_FILE_ASSIGN || t == TOKEN_RESET || t == TOKEN_REWRITE || t == TOKEN_CLOSE ||
           t == TOKEN_NEW || t == TOKEN_DISPOSE ||
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
        if (!is_pointer_type(step.result_type)) {
            compile_error(line, "'new' expects a pointer target");
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
    match(TOKEN_RPAREN);

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
    return write_node;
}

// 'dispose(X);' - unlike 'new', this only ever READS X (an ordinary
// expression - X can be anything pointer-typed, including a heap-
// dereferenced chain like 'p^.next', since dispose never writes back
// into X - see NODE_HEAP_DISPOSE in common.h for why this deliberately
// matches standard Pascal's own "the pointer's value is undefined after
// dispose" semantics rather than auto-nilling it).
static ASTNode *parse_dispose_statement(void) {
    match(TOKEN_DISPOSE);
    match(TOKEN_LPAREN);
    int line = token.line;
    ASTNode *p_expr = expression();
    if (!is_pointer_type(p_expr->expression_type)) {
        compile_error(line, "'dispose' expects a pointer variable");
    }
    match(TOKEN_RPAREN);
    ASTNode *stmt = create_node(NODE_HEAP_DISPOSE);
    stmt->line = line;
    stmt->left = p_expr;
    stmt->data.num_value = pointer_types[p_expr->expression_type - TYPE_POINTER_BASE].target_elem_size;
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

    int src_arr_sym_idx = find_var_soft(src_name);
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
    if (is_pointer_type(sym_table[idx].type) && token.type == TOKEN_CARET) {
        int line = token.line;
        ASTNode *base = create_node(NODE_VARIABLE);
        base->line = line;
        base->data.var_idx = idx;
        base->expression_type = sym_table[idx].type;
        HeapDerefStep step;
        base = parse_heap_deref_write(base, line, &step);
        match(TOKEN_ASSIGN);
        ASTNode *stmt = create_node(NODE_HEAP_FIELD_ASSIGN);
        stmt->left = base;
        stmt->right = wrap_range_check(expression(), step.is_subrange, step.subrange_lower, step.subrange_upper);
        ASTNode *offset_lit = create_node(NODE_NUMBER);
        offset_lit->data.num_value = step.field_offset;
        offset_lit->expression_type = TYPE_INTEGER;
        stmt->extra = offset_lit;
        stmt->expression_type = step.result_type;
        return stmt;
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
static ASTNode *parse_whole_record_assignment(int dest_is_local, int dest_record_type_idx, const int *dest_field_idx, int dest_levels_up, const char *dest_name) {
    match(TOKEN_ASSIGN);
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Expected a record variable of the same type as '%s'", dest_name);
    }

    // The source may be a record-array ELEMENT ('destRec := arr[i];'),
    // not just another plain record variable - checked first via a soft
    // lookup, since the overwhelmingly common case is a plain record var.
    {
        int src_arr_sym_idx = find_var_soft(token.text);
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
    match(TOKEN_IN);
    ASTNode *set_expr = arithmetic_expression();
    match(TOKEN_DO);

    char hidden_name[MAX_NAME];
    snprintf(hidden_name, MAX_NAME, "__for_in_set%d", sym_count);
    int set_sym_idx = sym_count;
    add_var(hidden_name, TYPE_SET);

    ASTNode *cache_assign = create_node(NODE_ASSIGN);
    cache_assign->data.var_idx = set_sym_idx;
    cache_assign->expression_type = TYPE_SET;
    cache_assign->left = set_expr;

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
    match(TOKEN_IN);
    ASTNode *set_expr = arithmetic_expression();
    match(TOKEN_DO);

    char hidden_name[MAX_NAME];
    snprintf(hidden_name, MAX_NAME, "__for_in_set_local%d", current_local_count);
    int set_slot = add_local(hidden_name, TYPE_SET);

    ASTNode *cache_assign = create_node(NODE_LOCAL_ASSIGN);
    cache_assign->data.var_idx = set_slot;
    cache_assign->expression_type = TYPE_SET;
    cache_assign->left = set_expr;

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

    if (token.type == TOKEN_IDENTIFIER) {
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
                    if (token.type != TOKEN_IDENTIFIER) {
                        compile_error(token.line, "Expected a field name after '%s.'", rec_name);
                    }
                    int field_idx = find_record_field(rv_record_type_idx, token.text);
                    if (field_idx == -1) {
                        compile_error(token.line, "'%s' is not a field of '%s'", token.text, rec_name);
                    }
                    match(TOKEN_IDENTIFIER);
                    if (rv_is_local) {
                        return parse_local_assignment(rv_field_idx[field_idx], rv_levels_up);
                    }
                    return parse_global_assignment(rv_field_idx[field_idx]);
                }
                // No '.field' - this is a whole-record assignment: 'p2 := p1;'
                return parse_whole_record_assignment(rv_is_local, rv_record_type_idx, rv_field_idx, rv_levels_up, rec_name);
            }
        }

        if (current_function_idx != -1 && strcmp(token.text, proc_table[current_function_idx].name) == 0) {
            match(TOKEN_IDENTIFIER);
            if (token.type == TOKEN_ASSIGN) {
                // Assigning to the function's own name sets its return value.
                ASTNode *stmt = create_node(NODE_LOCAL_ASSIGN);
                stmt->data.var_idx = proc_table[current_function_idx].return_slot;
                stmt->expression_type = proc_table[current_function_idx].return_type;
                match(TOKEN_ASSIGN);
                stmt->left = wrap_range_check(expression(),
                    proc_table[current_function_idx].return_is_subrange,
                    proc_table[current_function_idx].return_subrange_lower,
                    proc_table[current_function_idx].return_subrange_upper);
                return stmt;
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
            if (ls->is_var_param) {
                match(TOKEN_IDENTIFIER);
                if (is_pointer_type(ls->type) && token.type == TOKEN_CARET) {
                    int line = token.line;
                    ASTNode *base = create_node(NODE_VAR_PARAM_READ);
                    base->line = line;
                    base->data.var_idx = local_idx;
                    base->op = (TokenType)levels_up;
                    base->expression_type = ls->type;
                    HeapDerefStep step;
                    base = parse_heap_deref_write(base, line, &step);
                    match(TOKEN_ASSIGN);
                    ASTNode *stmt = create_node(NODE_HEAP_FIELD_ASSIGN);
                    stmt->left = base;
                    stmt->right = wrap_range_check(expression(), step.is_subrange, step.subrange_lower, step.subrange_upper);
                    ASTNode *offset_lit = create_node(NODE_NUMBER);
                    offset_lit->data.num_value = step.field_offset;
                    offset_lit->expression_type = TYPE_INTEGER;
                    stmt->extra = offset_lit;
                    stmt->expression_type = step.result_type;
                    return stmt;
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
            if (is_pointer_type(ls->type) && token.type == TOKEN_CARET) {
                int line = token.line;
                ASTNode *base = create_node(NODE_LOCAL_VAR);
                base->line = line;
                base->data.var_idx = local_idx;
                base->op = (TokenType)levels_up;
                base->expression_type = ls->type;
                HeapDerefStep step;
                base = parse_heap_deref_write(base, line, &step);
                match(TOKEN_ASSIGN);
                ASTNode *stmt = create_node(NODE_HEAP_FIELD_ASSIGN);
                stmt->left = base;
                stmt->right = wrap_range_check(expression(), step.is_subrange, step.subrange_lower, step.subrange_upper);
                ASTNode *offset_lit = create_node(NODE_NUMBER);
                offset_lit->data.num_value = step.field_offset;
                offset_lit->expression_type = TYPE_INTEGER;
                stmt->extra = offset_lit;
                stmt->expression_type = step.result_type;
                return stmt;
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
                if (current_locals[field_local_idx].type != TYPE_INTEGER) {
                    compile_error(token.line, "'for' loop variable must be integer");
                }
                if (token.type == TOKEN_IN) {
                    return parse_for_in_tail_local(field_local_idx);
                }
                return parse_local_for_tail(field_local_idx);
            }
            if (sym_table[field_sym_idx].type != TYPE_INTEGER) {
                compile_error(token.line, "'for' loop variable must be integer");
            }
            if (token.type == TOKEN_IN) {
                return parse_for_in_tail_global(field_sym_idx);
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
            if (sym_table[static_idx].type != TYPE_INTEGER) {
                compile_error(token.line, "'for' loop variable must be integer");
            }
            match(TOKEN_IDENTIFIER);
            if (token.type == TOKEN_IN) {
                return parse_for_in_tail_global(static_idx);
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
            if (current_locals[local_idx].type != TYPE_INTEGER) {
                compile_error(token.line, "'for' loop variable must be integer");
            }
            match(TOKEN_IDENTIFIER);
            if (token.type == TOKEN_IN) {
                return parse_for_in_tail_local(local_idx);
            }
            return parse_local_for_tail(local_idx);
        }
        int global_idx = find_var(token.text);
        match(TOKEN_IDENTIFIER);
        if (token.type == TOKEN_IN) {
            if (sym_table[global_idx].type != TYPE_INTEGER) {
                compile_error(token.line, "'for' loop variable must be integer");
            }
            return parse_for_in_tail_global(global_idx);
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

    if (token.type == TOKEN_WITH) {
        // 'with recordVar do statement;' - pure parser-time sugar, no
        // AST node of its own: pushing rv_idx onto with_stack (see the
        // comment above it) makes every bare field name inside the body
        // resolve exactly as 'recordVar.field' already would, via the
        // find_with_field() checks now threaded through every
        // identifier-resolution call site. The body's own parsed AST is
        // returned completely unwrapped - this statement contributes
        // nothing to the tree beyond whatever 'statement()' below
        // produces on its own.
        match(TOKEN_WITH);
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
        }
        int rv_idx = find_record_var(token.text);
        match(TOKEN_IDENTIFIER);
        match(TOKEN_DO);
        if (with_depth >= MAX_WITH_DEPTH) {
            compile_error(token.line, "'with' statements nested too deeply (limit is %d)", MAX_WITH_DEPTH);
        }
        with_stack[with_depth++] = rv_idx;
        ASTNode *body = statement();
        with_depth--;
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

