#ifndef COMMON_H
#define COMMON_H

#define MAX_NAME 32
#define MAX_SYMBOLS 100
#define MAX_CODE 500
#define MAX_STACK 100
#define MAX_STRING_LEN 256
#define MAX_STRINGS 256
#define MAX_ARRAY_MEM 4096
#define MAX_CALL_DEPTH 256
#define MAX_FRAME_STACK 4096
#define MAX_EXCEPT_DEPTH 64  // nested 'try' blocks active at once - see
                              // vm_except_stack[] in vm.c
#define MAX_PROCEDURES 50
#define MAX_UNITS 32        // total distinct units 'uses'-able in one compile
                            // - also bounds the "currently loading" stack
                            // used for circular-dependency detection, since
                            // a unit is only ever pushed onto it once
#define MAX_UNIT_PATH 256   // resolved unit source-file path buffer size
#define MAX_PROGRAM_ARGS 64 // command-line arguments a compiled program
                            // can see via ParamCount/ParamStr, INCLUDING
                            // index 0 (the .bin path itself) - see
                            // vm_set_program_args() in vm.c
#define MAX_PARAMS 8
#define MAX_CASE_LABELS 64 // per 'case' statement, across every arm combined
                           // - also bounds the number of ARMS (each arm
                           // needs at least one label), which is what
                           // codegen.c's per-arm jump-patch array sizes
                           // itself against
#define MAX_SET_BITS 32    // a 'set of <T>' base type's range (its number
                           // of distinct possible values) can't exceed
                           // this - see TYPE_SET below for why (one plain
                           // int, one bit per element)
#define MAX_ARRAY_DIMS 6   // an N-dimensional array (3 or more - 1D and 2D
                           // are their own separate, older mechanism, with
                           // their own dedicated fields/opcodes) can have
                           // at most this many dimensions. Bounds the
                           // fixed-size nd_lower[]/nd_upper[] fields below
                           // and the VM's own fixed-size per-opcode index
                           // buffer (see vm.c's OP_LOAD_IDXND etc.) - a
                           // generous, arbitrary cap, not a language rule.
#define MAX_ENUM_TYPES 20  // moved above DataType (rather than staying
                           // next to EnumTypeDef, where it used to live)
                           // because TYPE_POINTER_BASE's own definition
                           // below needs it, to reserve exactly the enum
                           // range's width and start immediately after it
                           // with zero gap or overlap.
#define MAX_ENUM_VALUES 32
#define MAX_POINTER_TYPES 20 // how many DISTINCT 'type PFoo = ^Target;'
                           // declarations a program can have - see
                           // TYPE_POINTER_BASE below.
#define MAX_PROC_TYPES 20  // how many DISTINCT NAMED procedural type
                           // ('type TProc = procedure(...);') declarations
                           // a program can have - see TYPE_PROC_BASE below.
#define MAX_RECORD_FIELDS 16 // moved here from parser.c (where
                           // RecordTypeDef/RecordField still live) because
                           // vm.c's heap allocator (see vm_heap_freelist[]
                           // in vm.c) needs the same bound: a pointer's
                           // target, if a record, needs one allocation
                           // "size class" per possible field count.
#define MAX_CLASS_METHODS 16 // moved here from parser.c (where
                           // PointerTypeDef still lives) because vm.c's
                           // vm_vtables[] needs the same bound: it's a
                           // flat array indexed class_id * MAX_CLASS_METHODS
                           // + slot, one row of MAX_CLASS_METHODS entries
                           // per class - see vm_vtables[]'s own comment.
#define MAX_HEAP_MEM 4096  // total ints reserved across every live
                           // new()'d block combined - the fixed-size pool
                           // vm_heap_mem[] (vm.c) allocates from, sized
                           // the same as MAX_ARRAY_MEM. Unlike every
                           // other SolVM memory region, how much of this
                           // is actually in use is a RUNTIME quantity
                           // (vm_heap_count, a bump cursor mutated by
                           // OP_NEW) rather than something fully known at
                           // compile time - new()/dispose() are this
                           // VM's first and only source of genuinely
                           // dynamic allocation.

typedef enum {
    TOKEN_PROGRAM, TOKEN_VAR, TOKEN_BEGIN, TOKEN_END,
    TOKEN_INTEGER, TOKEN_BOOLEAN,
    TOKEN_IDENTIFIER, TOKEN_NUMBER, TOKEN_TRUE, TOKEN_FALSE,
    TOKEN_ASSIGN, TOKEN_PLUS, TOKEN_MINUS, TOKEN_MUL, TOKEN_DIV,
    TOKEN_EQ, TOKEN_LT, TOKEN_GT,
    TOKEN_AND, TOKEN_OR, TOKEN_NOT,
    TOKEN_LTE, TOKEN_GTE, TOKEN_NEQ,
    TOKEN_DIV_KW, TOKEN_MOD, TOKEN_XOR,
    TOKEN_SEMI, TOKEN_COLON, TOKEN_COMMA, TOKEN_PERIOD,
    TOKEN_LPAREN, TOKEN_RPAREN,
    TOKEN_WRITELN, TOKEN_WRITE, TOKEN_READLN,
    TOKEN_IF, TOKEN_THEN, TOKEN_ELSE,
    TOKEN_WHILE, TOKEN_DO,
    TOKEN_REPEAT, TOKEN_UNTIL,
    TOKEN_STRING, TOKEN_STRING_TYPE,
    TOKEN_FOR, TOKEN_TO, TOKEN_DOWNTO,
    TOKEN_ARRAY, TOKEN_OF, TOKEN_DOTDOT,
    TOKEN_LBRACKET, TOKEN_RBRACKET,
    TOKEN_BREAK, TOKEN_CONTINUE,
    TOKEN_CHAR_TYPE,
    TOKEN_PROCEDURE,
    TOKEN_FORWARD,
    TOKEN_FUNCTION,
    TOKEN_SHL, TOKEN_SHR,
    TOKEN_INC, TOKEN_DEC,
    TOKEN_ABS, TOKEN_SQR, TOKEN_ODD, TOKEN_SUCC, TOKEN_PRED,
    TOKEN_ORD, TOKEN_CHR,
    TOKEN_CHARCODE, // '#NNN' - a numeric char-code literal, e.g. #13
    TOKEN_LENGTH, TOKEN_COPY, TOKEN_POS,
    TOKEN_LOW, TOKEN_HIGH,
    TOKEN_UPCASE, TOKEN_UPPERCASE, TOKEN_LOWERCASE,
    TOKEN_MID, TOKEN_LEFT, TOKEN_RIGHT, TOKEN_INPOS,
    TOKEN_REAL,       // a real literal, e.g. 3.14 - distinct from
                      // TOKEN_NUMBER (integer literals)
    TOKEN_REAL_TYPE,  // the 'real' type keyword
    TOKEN_TRUNC, TOKEN_ROUND,
    TOKEN_TYPE, TOKEN_RECORD, TOKEN_CLASS,
    TOKEN_CONST,
    TOKEN_WITH,
    TOKEN_ASSERT,
    TOKEN_STATIC,
    TOKEN_CASE,
    TOKEN_SQRT, TOKEN_SIN, TOKEN_COS, TOKEN_ARCTAN, TOKEN_EXP, TOKEN_LN,
    TOKEN_PI, TOKEN_POWER,
    TOKEN_POW, // '**'
    TOKEN_READ,   // 'read' - like 'readln', but never consumes the rest
                  // of the input line (see NODE_READLN's op field, reused
                  // to distinguish the two, exactly like NODE_WRITELN
                  // already reuses op for TOKEN_WRITE vs TOKEN_WRITELN).
    TOKEN_EOF_FN, // the 'eof' builtin function - NOT the same as TOKEN_EOF
                  // below (the lexer's own end-of-input sentinel token,
                  // an unrelated, pre-existing concept this name could
                  // otherwise be confused with).
    TOKEN_EOLN,   // the 'eoln' builtin function.
    TOKEN_SET,    // the 'set' keyword ('set of <ordinal type>').
    TOKEN_IN,     // the 'in' set-membership operator.
    TOKEN_LABEL,  // the 'label' keyword introducing a block's label
                  // declaration section ('label 1, 2, 100;').
    TOKEN_GOTO,   // the 'goto' statement keyword.
    TOKEN_TEXT_TYPE, // the 'text' file type keyword ('var f: text;').
    TOKEN_FILE_ASSIGN, // the 'assign' builtin procedure ('assign(f, name)') -
                  // a distinct token from TOKEN_ASSIGN (':=') despite the
                  // similar name; unrelated concepts that happen to share
                  // an English word.
    TOKEN_RESET, TOKEN_REWRITE, TOKEN_CLOSE, // the 'reset'/'rewrite'/
                  // 'close' builtin procedures. ('append' - opening a
                  // file for appending - is a later, non-ISO-7185 Pascal
                  // extension, deliberately out of scope for now; see
                  // docs/ROADMAP.md.)
    TOKEN_CARET,  // '^' - a pointer type ('type PFoo = ^Target;') or a
                  // dereference ('p^', 'p^.field').
    TOKEN_NEW,    // the 'new' builtin procedure ('new(p)').
    TOKEN_DISPOSE, // the 'dispose' builtin procedure ('dispose(p)').
    TOKEN_NIL,    // the 'nil' literal.
    TOKEN_UNIT,           // 'unit UnitName;' - a unit source file's own header.
    TOKEN_INTERFACE,      // a unit's 'interface' section.
    TOKEN_IMPLEMENTATION, // a unit's 'implementation' section.
    TOKEN_USES,           // 'uses UnitName, ...;' - pulls a unit's declarations
                           // into the current compile (program header or a
                           // unit's own interface section).
    TOKEN_INHERITED, // 'inherited;' or 'inherited MethodName(args);' inside
                      // a class method body - a direct (non-virtual) call to
                      // the enclosing method's own class's PARENT's
                      // implementation of a method. See NODE_INHERITED_CALL.
    TOKEN_PARAMCOUNT, // the 'ParamCount' builtin function - see OP_PARAM_COUNT.
    TOKEN_PARAMSTR,   // the 'ParamStr' builtin function - see OP_PARAM_STR.
    TOKEN_PRIVATE,    // a 'private' section inside a 'class ... end;' body -
                      // every field/method declared until the next
                      // 'private'/'public' or the class's own 'end' is only
                      // accessible from the DECLARING class's own methods,
                      // not even a subclass's - see RecordField.is_private/
                      // ProcParamHeader.is_private in parser.c.
    TOKEN_PUBLIC,     // a 'public' section - the default visibility anyway,
                      // but needed to switch back after a 'private' section.
    TOKEN_TRY,        // 'try ... except ... end' - see NODE_TRY.
    TOKEN_EXCEPT,     // the 'except' half of a 'try' statement.
    TOKEN_RAISE,      // 'raise <message>;' - see NODE_RAISE/OP_RAISE.
    TOKEN_EXCEPTMESSAGE, // the 'ExceptMessage' builtin - see OP_EXCEPT_MSG.
    TOKEN_PROPERTY,   // 'property Name: Type read ReadTarget [write WriteTarget];'
                      // inside a 'class ... end;' body - see ClassProperty in
                      // parser.c. 'read'/'write' reuse the existing,
                      // unconditionally-reserved TOKEN_READ/TOKEN_WRITE tokens
                      // (already used for read/readln/write/writeln I/O) - no
                      // new token needed for either.
    TOKEN_IS,         // 'obj is TFoo' - runtime type test. See NODE_IS_TEST/
                      // OP_IS_INSTANCE.
    TOKEN_AS,         // 'obj as TFoo' - checked downcast. See NODE_AS_CAST/
                      // OP_IS_INSTANCE (reused) + OP_RAISE.
    TOKEN_EOF
} TokenType;

typedef enum {
    TYPE_UNKNOWN,
    TYPE_INTEGER,
    TYPE_BOOLEAN,
    TYPE_STRING,
    TYPE_CHAR,  // Represented identically to TYPE_STRING at runtime (a
                // string_pool[] index) - the only difference is the VM
                // enforces a length-1 constraint whenever a value is
                // actually stored into a char variable/array element.
    TYPE_REAL,  // A 32-bit IEEE-754 float, reinterpreting the bits of the
                // same int-sized storage slot every other type uses (see
                // vm.c's bits_to_float/float_to_bits) - not a double,
                // specifically so a real value still fits in exactly one
                // slot, matching every other type's storage model.
    TYPE_SET,   // 'set of <ordinal type>' - represented as a single plain
                // int, one bit per possible element (bit K set means
                // ordinal value K is a member), so the base type's range
                // is capped at 32 distinct values (see
                // parse_scalar_type()'s TOKEN_SET branch in parser.c,
                // which enforces this at the declaration site and then
                // deliberately discards the bounds - nothing downstream
                // needs to remember them, since every set operation
                // either builds a bitmask directly from the operand
                // values (set constructors, 'in') or combines two
                // bitmasks with ordinary bitwise ops (+/-/*/=/<>/<=/>=)
                // - unlike TYPE_ENUM_BASE below, this is ONE plain type
                // for every declared set, not one per distinct base type;
                // this compiler doesn't stop you from combining a 'set of
                // 0..9' with a 'set of TColor' (both are just bitmasks at
                // that point) - a deliberate simplification, documented
                // in docs/LANGUAGE.md. No new opcodes needed anywhere:
                // NODE_SET_CONSTRUCTOR/NODE_SET_IN and the set-aware
                // branches of NODE_BINARY_OP in codegen.c build everything
                // from PUSH/DUP/SHL/BOR/BAND/BNOT/EQ/NEQ, all of which
                // already existed.
    TYPE_FILE,  // 'text' - a text file variable. GLOBAL ONLY (can't be a
                // parameter or local - a deliberate scope limitation, see
                // docs/LANGUAGE.md). Its OWN storage slot (vm_vars[idx])
                // is unused at runtime - unlike every other type, all of
                // a file variable's actual state (the FILE*, and the
                // filename assign() bound it to) lives in vm_open_files[]
                // (see vm.c), indexed by the SAME sym_table[] index a
                // file variable already has, rather than through a
                // second table-index indirection the way TYPE_STRING/
                // TYPE_CHAR values (string_pool[] indices) work - safe to
                // do only because a file variable is always global (one
                // fixed sym_table[] index for its whole lifetime), which
                // is exactly why parameter/local support was cut from
                // this feature's scope. assign() records a filename;
                // reset()/rewrite() actually fopen()s it (for reading/
                // writing respectively); close() fclose()s it. Every
                // read/write/eof/eoln builtin gained an optional leading
                // file-variable argument (defaulting to stdin/stdout when
                // omitted, exactly as before this feature existed) - see
                // NODE_READLN/NODE_LOCAL_READLN/NODE_WRITELN below, and
                // NODE_FILE_OP for assign/reset/rewrite/close themselves.
    TYPE_NIL,   // The type of the literal 'nil' - never a variable's own
                // declared type (there's no 'var x: nil;'), only ever an
                // EXPRESSION's type: assignment/comparison-compatible
                // with any pointer type (see TYPE_POINTER_BASE below),
                // checked specially wherever pointer compatibility is
                // checked (NODE_ASSIGN, NODE_BINARY_OP's '='/'<>') rather
                // than through the ordinary exact-DataType-match rule
                // every other type uses. Runtime representation: the
                // literal integer -1 (see TYPE_POINTER_BASE for why -1
                // specifically), pushed exactly like any other NODE_NUMBER
                // literal - no new opcode needed for 'nil' itself.
    TYPE_ENUM_BASE, // Not a real type by itself - a specific enumerated
                // type ('type TColor = (Red, Green, Blue);') is encoded
                // as TYPE_ENUM_BASE + its enum_types[] index, so DataType
                // stays a single plain int field everywhere (ASTNode.
                // expression_type, Symbol.type, etc.) instead of needing
                // a second "which enum" field threaded through every
                // struct that already carries a DataType. An enum
                // value's actual runtime representation is just its
                // ordinal (0, 1, 2, ...) - a plain int, identical to
                // TYPE_INTEGER as far as vm_vars[]/vm.c/the .bin format
                // are concerned; only the compiler frontend (parser.c/
                // type_checker.c/codegen.c) ever looks at values in this
                // range. Occupies TYPE_ENUM_BASE..TYPE_ENUM_BASE+
                // MAX_ENUM_TYPES-1 - every "is this an enum type" check
                // elsewhere in this codebase MUST be a bounded range
                // check (see is_enum_type()-style helpers in parser.c/
                // type_checker.c/etc.), never a bare '>= TYPE_ENUM_BASE',
                // now that TYPE_POINTER_BASE's own range immediately
                // follows this one - a bare '>=' would wrongly also
                // match every pointer type. (The one exception: desole.c
                // never distinguishes enum from pointer in the first
                // place - both degrade to printing as plain "integer",
                // their actual shared runtime representation - so its
                // own '>= TYPE_ENUM_BASE' check is deliberately still
                // unbounded.)
    TYPE_POINTER_BASE = TYPE_ENUM_BASE + MAX_ENUM_TYPES, // Also not a
                // real type by itself - a specific pointer type ('type
                // PNode = ^TNode;') is encoded as TYPE_POINTER_BASE + its
                // pointer_types[] index (parser.c-local - see
                // PointerTypeDef there), mirroring TYPE_ENUM_BASE's own
                // encoding exactly, and for the same reason: DataType
                // stays one plain int field everywhere. Explicitly placed
                // MAX_ENUM_TYPES past TYPE_ENUM_BASE (rather than letting
                // the enum auto-increment to TYPE_ENUM_BASE+1) so the two
                // ranges never overlap, however many enum types a program
                // actually declares. A pointer value's runtime
                // representation is a plain int: -1 means nil (see
                // TYPE_NIL above), and any value >= 0 is a byte-ish
                // OFFSET into vm.c's vm_heap_mem[] (an index, not a
                // pointer in the C sense - this VM has no notion of
                // machine addresses) - identical in spirit to how an enum
                // value's representation is just its raw ordinal. Only
                // the compiler frontend (parser.c/type_checker.c/
                // codegen.c) ever looks at a value in this range;
                // vm.c/solas/desole just see "some int", same as an enum.
                // pointer_types[] stays parser.c-local (unlike
                // enum_types[], which common.h/codegen.c DO need, to
                // print an enum value's name) because nothing outside
                // parser.c ever needs to look a pointer type up by this
                // index - codegen only ever needs a pointer TYPE's target
                // element size and, for a record target, its field
                // offsets, and parser.c already resolves and bakes both
                // directly into the AST as compile-time literals (see
                // NODE_HEAP_FIELD_ACCESS/ASSIGN/ALLOC below) before
                // codegen ever runs, exactly like NODE_ARRAY_RECORD_
                // FIELD_ACCESS/ASSIGN already do for array-of-records.
    TYPE_PROC_BASE = TYPE_POINTER_BASE + MAX_POINTER_TYPES, // Also not a
                // real type by itself - a specific NAMED procedural type
                // ('type TProc = procedure(x: integer);') is encoded as
                // TYPE_PROC_BASE + its proc_types[] index (parser.c-local -
                // see ProcTypeDef there), mirroring TYPE_POINTER_BASE's own
                // encoding exactly. A procedural-type value's runtime
                // representation is a plain int too: -1 means nil (assigned
                // via the 'nil' literal, exactly like a pointer), and any
                // other value is a top-level procedure/function's own
                // entry_address, pushed the same way NODE_PROC_REF already
                // does for a functional/procedural PARAMETER's actual
                // argument (see NODE_PROCVAR_CALL below) - a genuinely
                // separate, more general mechanism from that one, though:
                // a functional/procedural parameter's own inline signature
                // (is_proc_param in parser.c) can only ever live in a
                // parameter slot, fed by a literal top-level name at the
                // call site or forwarded from an enclosing one; a NAMED
                // procedural type is an ordinary DataType, usable for a
                // plain global/local variable exactly like any scalar -
                // assignable, copyable, callable through, and comparable
                // to nil.
} DataType;

// One declared enumerated type ('type TColor = (Red, Green, Blue);') -
// see the TYPE_ENUM_BASE comment above for how a specific enum type is
// referenced from a DataType field. Populated by parser.c; also read by
// codegen.c (to build the name-printing chain for 'write'/'writeln' -
// see NODE_WRITELN in codegen.c) and ast_printer.c (to print a literal
// enum value's name instead of its bare ordinal in -v output). Declared
// here (rather than staying parser.c-local like RecordTypeDef/ConstDef/
// TypeAliasDef) because, uniquely among this project's compile-time-only
// features, codegen needs it too - not just the parser. Not part of the
// .bin file format (like proc_table[], defined below): solvm/solas/
// desole never reference it, since none of them link parser.c.
typedef struct {
    char name[MAX_NAME];                       // e.g. "TColor"
    char value_names[MAX_ENUM_VALUES][MAX_NAME]; // e.g. "Red", "Green", "Blue"
    int value_str_idx[MAX_ENUM_VALUES];        // each name's string_pool[] index, for printing
    int value_count;
} EnumTypeDef;

typedef struct {
    TokenType type;
    char text[MAX_NAME];
    char string_value[MAX_STRING_LEN]; // populated only for TOKEN_STRING
    int value;
    float real_value; // populated only for TOKEN_REAL
    int line;
} Token;

typedef struct {
    char name[MAX_NAME];
    DataType type;   // element type when is_array is set, else the scalar's type
    int is_array;
    int array_lower; // inclusive. For a 2D array, the FIRST dimension's bounds.
    int array_upper; // inclusive
    int array_base;  // base offset into the shared array memory region -
                      // for a 2D array, the base of the whole flattened,
                      // row-major region (size = dim1_size * dim2_size)
    int is_2d;        // 0 for an ordinary 1D array (the existing, default
                      // case - the fields below are unused); 1 for 2D
    int array_lower2; // only meaningful if is_2d: the SECOND dimension's bounds
    int array_upper2;
    int is_nd;        // 1 if this array has 3 OR MORE dimensions - mutually
                      // exclusive with is_2d (is_array is still 1 either
                      // way). Unlike is_2d (which still uses array_lower/
                      // array_upper for its first dimension), an is_nd
                      // array stores EVERY dimension's bounds uniformly in
                      // nd_lower[]/nd_upper[] below (index 0..nd_dims-1) -
                      // array_lower/array_upper/array_lower2/array_upper2
                      // stay unused (0) when is_nd is set. array_base still
                      // means the same thing: the base offset of the whole
                      // flattened, row-major region (size = the product of
                      // every dimension's size) in the shared array memory
                      // region every array (1D, 2D, or N-D) draws from.
    int nd_dims;      // only meaningful if is_nd: the dimension count (3..MAX_ARRAY_DIMS)
    int nd_lower[MAX_ARRAY_DIMS]; // only meaningful if is_nd, indices 0..nd_dims-1
    int nd_upper[MAX_ARRAY_DIMS];
    int is_subrange;      // 1 if this symbol's VALUE (the scalar itself,
                          // or - if is_array - each element) is
                          // constrained to a declared subrange
                          // ('type TAge = 0..150;'); see NODE_RANGE_CHECK.
                          // Independent of is_array/is_2d - a subrange-
                          // element array has both set.
    int subrange_lower;   // only meaningful if is_subrange
    int subrange_upper;
    int is_record_array;      // 1 if this array's elements are records
                              // (an "array of TSomeRecord" - see parser.c's
                              // record_arrays[]/find_record_array_type()
                              // for the record TYPE this is an array of;
                              // that mapping is parser-only, since
                              // record_types[] itself never leaves
                              // parser.c - see RecordTypeDef there).
                              // Mutually exclusive with is_2d/is_nd (only
                              // 1D arrays of records are supported so
                              // far). 'type' above is unused/meaningless
                              // for a record-array Symbol (defensively set
                              // to TYPE_INTEGER) - unlike a plain array,
                              // there's no single element type; each
                              // field's own type is only ever consulted
                              // at PARSE time (via record_types[]) or
                              // carried directly on the AST node itself
                              // (NODE_ARRAY_RECORD_FIELD_ACCESS/ASSIGN's
                              // own expression_type) - never through this
                              // Symbol, which is all codegen/vm.c/solas/
                              // desole ever see.
    int record_elem_field_count; // only meaningful if is_record_array:
                              // how many ints each element occupies (the
                              // record type's own field_count) - the
                              // stride vm_record_array_offset() (vm.c)
                              // multiplies the runtime index by, since
                              // every array (1D/2D/ND/record) shares one
                              // flat vm_array_mem[] pool with an implicit
                              // stride of 1 everywhere else.
    char declaring_unit[MAX_NAME]; // empty if declared in the main
                              // program or a unit's own 'interface'
                              // section (always visible); the unit's own
                              // name if declared in a unit's
                              // 'implementation' section - see
                              // is_unit_private and symbol_visible_here()
                              // in parser.c.
    int is_unit_private;     // 1 if this var was declared in a unit's
                              // 'implementation' section, and so should
                              // only resolve for reference sites parsed
                              // from within that SAME unit's own source -
                              // see symbol_visible_here() in parser.c.
                              // 0 (visible everywhere) for anything
                              // declared in the main program or a unit's
                              // 'interface'.
} Symbol;

typedef enum {
    OP_PUSH, OP_LOAD, OP_STORE,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV,
    OP_EQ, OP_LT, OP_GT,
    OP_AND, OP_OR, OP_NOT,     // logical (boolean) - see OP_BAND etc. for the bitwise (integer) versions
    OP_LTE, OP_GTE, OP_NEQ,
    OP_NEG,
    OP_MOD, OP_XOR,
    OP_BAND, OP_BOR, OP_BXOR, OP_BNOT, // bitwise integer and/or/xor/not -
                  // 'and'/'or'/'xor'/'not' on two integers means bitwise
                  // in Pascal, distinct from the same keywords on two
                  // booleans (logical) - the underlying values (0/1 for
                  // boolean) happen to make bitwise AND/OR/XOR agree with
                  // logical AND/OR/XOR, but NOT does not (bitwise ~0 is
                  // -1, not 1), so these need to be genuinely separate
                  // opcodes, chosen by codegen based on operand type.
    OP_SHL, OP_SHR, // integer shift left/right. SHR is a logical (not
                  // arithmetic/sign-extending) shift, matching Pascal.
    OP_DUP,       // Duplicate the top of the stack (push a second copy).
                  // A generic primitive, not tied to any one feature -
                  // first real use is 'sqr(x)' (evaluate x once, DUP,
                  // multiply) rather than evaluating x's bytecode twice.
    OP_ABS,       // Pop a value; push its absolute value.
    OP_ORD,       // Pop a string_pool[] index; validate it refers to
                  // exactly one character (same check as storing into a
                  // char slot); push that character's byte value (0..255).
    OP_CHR,       // Pop an integer (1..255 - 0 can't be represented, since
                  // string_pool[] entries are null-terminated C strings);
                  // intern the single-character string for that byte
                  // value (reusing an existing pool entry if there is
                  // one) and push its index.
    OP_PRINT,     // Pop a value; print it with NO trailing newline.
    OP_PRINT_BOOL, // Pop a value; print "TRUE" (nonzero) or "FALSE" (zero)
                  // with NO trailing newline - standard Pascal prints
                  // booleans as words, not as the underlying 0/1.
    OP_READ,
    OP_HALT,
    OP_JMP,  // Unconditional jump. arg = absolute target instruction index.
    OP_JZ,   // Pop the stack; if the value is zero (false), jump to arg.
             // Otherwise fall through to the next instruction.
    OP_PUSH_STR,  // Push a string_pool[] index (arg) onto the stack.
    OP_PRINT_STR, // Pop an index; print string_pool[index] with NO
                  // trailing newline.
    OP_SEQ,       // Pop two indices; push 1 if string_pool[] contents are
                  // equal (strcmp), else 0. '<>' is OP_SEQ followed by
                  // OP_NOT - no separate string-not-equal opcode needed.
    OP_SCONCAT,   // Pop two indices; concatenate their string_pool[]
                  // contents, intern the result (possibly growing the
                  // pool at runtime), and push the new index.
    OP_NEWLINE,   // Print a newline. No operand, no stack interaction.
                  // writeln emits this once, after all its arguments;
                  // write never emits it.
    OP_LOAD_IDX,  // arg = array's symbol index. Pop a runtime index; bounds-
                  // check it against the symbol's declared [lower, upper];
                  // push the array element's value.
    OP_STORE_IDX, // arg = array's symbol index. Pop a value, then a runtime
                  // index (value was pushed after the index by codegen);
                  // bounds-check the index; store the value into the array.
    OP_SCMP,      // Pop two indices; push -1, 0, or 1 for a < b, a == b,
                  // a > b (lexicographic, via strcmp, normalized to a
                  // fixed sign). String '<'/'>'/'<='/'>=' compile as
                  // OP_SCMP followed by PUSH 0 and the matching integer
                  // LT/GT/LTE/GTE - no separate string-ordering opcodes.
    OP_CALL,      // arg = absolute target instruction index. Push the
                  // return address (the instruction after this one) onto
                  // a separate call stack, then jump to arg. Recursion-safe:
                  // each call gets its own return address on that stack,
                  // not a single shared slot.
    OP_RET,       // Pop the call stack; jump to the popped address. A
                  // runtime error (not a crash) if the call stack is
                  // empty - e.g. bytecode that reaches RET without a
                  // matching CALL. Also restores the caller's frame
                  // pointer and frame-stack top, deallocating whatever
                  // frame the returning call had (see OP_ENTER).
    OP_ENTER,     // arg = number of local slots to reserve (zero-
                  // initialized) on the frame stack. The first
                  // instruction of a procedure body - establishes its
                  // frame. Parameters arrive via the operand stack
                  // (pushed by the caller before CALL) and are typically
                  // moved into locals 0..k-1 with STORE_LOCAL right after.
    OP_LOAD_LOCAL,  // arg = local slot index, relative to the current
                    // frame pointer. Push its value.
    OP_STORE_LOCAL, // arg = local slot index, relative to the current
                    // frame pointer. Pop a value; store it there.
    OP_POP,          // Pop a value and discard it. Used when a function is
                     // called as a statement rather than as part of an
                     // expression - its return value is still pushed like
                     // any function's, but nothing consumes it, so this
                     // discards it explicitly rather than leaving the
                     // operand stack unbalanced for whatever comes next.
    OP_LOAD_IDX_DYN,  // Pop a runtime index, then a runtime array
                      // reference (a sym_table[] index - arrived via
                      // LOAD_LOCAL of an array-reference parameter).
                      // Same bounds-checked lookup as OP_LOAD_IDX, but
                      // "which array" is read from the stack instead of
                      // baked into the instruction - needed for array
                      // parameters, since different calls can pass
                      // different arrays.
    OP_STORE_IDX_DYN, // Pop a value, then a runtime index, then a runtime
                      // array reference. Same as OP_STORE_IDX, but "which
                      // array" comes from the stack.
    OP_LENGTH,    // Pop a string_pool[] index; push strlen() of it.
    OP_STR_CHAR_AT, // Pop a runtime (1-based) index, then a string_pool[]
                  // index. Bounds-check the index against the string's
                  // actual length (a runtime error if out of range,
                  // unlike OP_COPY below - this matches real Pascal,
                  // where indexing is strict but copy() is forgiving).
                  // Intern the single character at that position as a
                  // new 1-character string; push its index.
    OP_COPY,      // Pop count, then start, then a string_pool[] index
                  // (pushed in that order: string, start, count).
                  // Extracts the substring - *clamped*, not bounds-
                  // checked: an out-of-range start or a count that runs
                  // past the end just yields as much of the string as
                  // actually exists (possibly empty), matching real
                  // Pascal's copy() rather than this VM's usual strict-
                  // bounds-error convention. Interns the result; pushes
                  // its index.
    OP_POS,       // Pop a haystack string_pool[] index, then a needle
                  // string_pool[] index (pushed needle-then-haystack).
                  // Push the needle's first 1-based position in the
                  // haystack, or 0 if not found (an empty needle is
                  // defined as "not found", avoiding an ambiguous
                  // "found at every position" result).
    OP_UPCASE_CHAR,  // Pop a string_pool[] index (must be exactly one
                     // character); push the uppercased version (a new
                     // interned string) if it's a lowercase letter, else
                     // push the same index back unchanged.
    OP_UPPERCASE_STR, // Pop a string_pool[] index; push a new interned
                     // string with every lowercase letter uppercased.
    OP_LOWERCASE_STR, // Same as above, lowercasing every uppercase letter.
    OP_LEFT,      // Pop count, then a string_pool[] index. Push a new
                  // interned string of the first `count` characters,
                  // clamped to the string's actual length (never errors).
    OP_RIGHT,     // Same as OP_LEFT, but the *last* `count` characters.
    OP_LOAD_IDX2D,  // arg = array's symbol index. Pop the second runtime
                    // index, then the first (pushed first-then-second, so
                    // second ends up on top); bounds-check each against
                    // its own dimension; push the element at the row-
                    // major offset (i - lower1) * dim2_size + (j - lower2).
    OP_STORE_IDX2D, // arg = array's symbol index. Pop a value, then the
                    // second index, then the first (value pushed last by
                    // codegen, so it's on top); bounds-check; store at
                    // the same row-major offset OP_LOAD_IDX2D computes.
    OP_FADD, OP_FSUB, OP_FMUL, OP_FDIV, // float arithmetic - integer
                  // opcodes operate on the popped ints directly, which
                  // would be meaningless applied to a float's raw bit
                  // pattern (adding two floats' bit patterns doesn't give
                  // their sum's bit pattern), so these reinterpret the
                  // bits as float first (see vm.c's bits_to_float),
                  // compute, then reinterpret the float result back to
                  // an int-sized bit pattern before pushing.
    OP_FEQ, OP_FLT, OP_FGT, OP_FLTE, OP_FGTE, OP_FNEQ, // float comparison -
                  // result is a plain 0/1 boolean int, same as the
                  // integer comparison opcodes; only the comparison
                  // itself is done in float.
    OP_FNEG,      // Float unary negation.
    OP_FPRINT,    // Pop a value, reinterpret as float, print it (no
                  // trailing newline) - see vm.c for the exact format.
    OP_INT_TO_REAL, // Pop an integer; push the bit pattern of its exact
                  // float equivalent. Emitted wherever an integer-typed
                  // expression needs implicit widening to real (mixed
                  // arithmetic, or assigning an integer to a real
                  // variable) - inserted by the type checker, not chosen
                  // ad hoc by codegen.
    OP_TRUNC,     // Pop a value, reinterpret as float, push its integer
                  // part (truncated toward zero, matching Pascal's
                  // trunc() - never a runtime error, unlike most
                  // conversions in this VM, since truncation is always
                  // well-defined for any finite float).
    OP_ROUND,     // Same as OP_TRUNC, but rounds to the nearest integer
                  // (half away from zero) instead of truncating.
    OP_PRINT_PADDED,      // Pop width, then a value; format as decimal,
                  // right-justify to at least `width` characters (padded
                  // with spaces - never truncated if the content is
                  // already wider), print, no trailing newline. Kept
                  // entirely separate from plain OP_PRINT (rather than
                  // extending it to always take a width) so ordinary
                  // write/writeln arguments without ':width' compile to
                  // exactly the same bytecode as before this feature
                  // existed.
    OP_PRINT_STR_PADDED,  // Same as OP_PRINT_PADDED, but for a
                  // string_pool[] index.
    OP_PRINT_BOOL_PADDED, // Same as OP_PRINT_PADDED, but for a boolean.
    OP_FPRINT_PADDED,     // Pop width, then a value; format with the
                  // same default %.6g OP_FPRINT uses (no explicit
                  // decimal-place count given), pad, print.
    OP_FPRINT_PADDED_PRECISE, // Pop precision, then width, then a value;
                  // format with exactly `precision` digits after the
                  // decimal point (Pascal's ':width:precision' form),
                  // pad, print. A genuinely separate opcode from
                  // OP_FPRINT_PADDED, rather than one opcode with a
                  // "precision not given" sentinel value, specifically
                  // to avoid ambiguity with a real, valid user-written
                  // precision - the two forms are syntactically distinct
                  // ('x:10' vs 'x:10:2'), so codegen already knows which
                  // one applies and picks the opcode accordingly.
    OP_FABS,      // Pop a value, reinterpret as float; push its absolute
                  // value. sqr(real) needs no equivalent new opcode - it
                  // reuses the existing generic OP_DUP (duplicates
                  // whatever's on top of the stack, regardless of type)
                  // followed by OP_FMUL.
    OP_FSQRT, OP_FSIN, OP_FCOS, OP_FARCTAN, OP_FEXP, OP_FLN, // the rest
                  // of ISO Pascal's math function set (sqrt/sin/cos/
                  // arctan/exp/ln, plus abs/sqr which already existed).
                  // Each pops a value, reinterprets as float, computes
                  // via the obvious libm function, checks the result
                  // isn't NaN/infinite (a runtime error if so - domain
                  // errors like sqrt(-1) or ln(0) and overflow are all
                  // caught this one uniform way, rather than needing a
                  // separate domain precondition check per function),
                  // pushes the result.
    OP_FPOWER,    // Pop the exponent, then the base (both reinterpreted
                  // as float); push base^exponent via powf, with the
                  // same NaN/infinite result check as the functions
                  // above (e.g. a negative base with a non-integer
                  // exponent has no real result). Shared by both the
                  // power(base, exp) function and the '**' operator -
                  // they compute exactly the same thing, just with
                  // different surface syntax.
    OP_STR_CHAR_REPLACE, // Pop a new character (must be exactly one
                  // character - a runtime error otherwise, same check as
                  // any other char-typed storage), then a runtime
                  // (1-based) index, then a string_pool[] index (the
                  // string's *old* value); bounds-check the index
                  // strictly (an out-of-range index is a runtime error,
                  // matching read-side indexing, not the lenient
                  // clamping copy()/left()/right() use); build a new
                  // string with that one character replaced; intern it;
                  // push its index. Deliberately doesn't know or care
                  // whether the string being mutated lives in a global
                  // variable or a local frame slot - codegen surrounds
                  // this with an ordinary LOAD/LOAD_LOCAL beforehand and
                  // STORE/STORE_LOCAL afterward, so this one opcode
                  // covers both cases with no duplication.
    OP_READ_LOCAL_INT, OP_READ_LOCAL_BOOL, OP_READ_LOCAL_REAL,
    OP_READ_LOCAL_STR, OP_READ_LOCAL_CHAR, // arg = frame slot. Each reads
                  // one line of stdin, parses/validates it exactly like
                  // OP_READ's corresponding branch (see vm.c), and
                  // stores the result directly into the frame slot -
                  // separate opcodes per type, unlike OP_READ's single
                  // opcode with a sym_table[]-driven runtime type
                  // dispatch, because a local frame slot has no
                  // equivalent runtime type tag to dispatch on; codegen
                  // picks the right one at compile time, where the
                  // local's declared type is already known.
    OP_CHECK_LOWER, OP_CHECK_UPPER, // arg = a subrange's lower/upper bound
                  // (a compile-time constant - see NODE_RANGE_CHECK).
                  // Peeks at the value on top of the stack (does NOT pop
                  // it - leaves the stack exactly as OP_DUP would, minus
                  // the duplication) and aborts with a runtime error if
                  // it's below/above the bound; otherwise a no-op. Two
                  // separate opcodes, each with one immediate bound,
                  // rather than one opcode needing two - an opcode only
                  // carries a single int arg.
    OP_ASSERT,    // Pop a string_pool[] index (the message), then a
                  // 0/1 value (the condition, pushed first so it ends
                  // up underneath - matching codegen's natural
                  // evaluate-condition-then-message order). Aborts with
                  // a VM Runtime Error using that message if the
                  // condition is 0; otherwise a no-op, same as
                  // CHECK_LOWER/CHECK_UPPER above.
    OP_LOAD_IDX2D_DYN, OP_STORE_IDX2D_DYN, // Same as LOAD_IDX2D/
                  // STORE_IDX2D, but *which* array is also popped from
                  // the stack instead of coming from arg - needed for 2D
                  // array parameters, since different calls can pass
                  // different arrays (exactly the same reasoning
                  // LOAD_IDX_DYN/STORE_IDX_DYN already use for 1D).
                  // LOAD_IDX2D_DYN: pop j, i, then a runtime array
                  // reference (a symbol index); bounds-check each index
                  // against its own dimension; push the element.
                  // STORE_IDX2D_DYN: pop value, j, i, then a runtime
                  // array reference; bounds-check; store.
    OP_PUSH_LOCAL_REF, // Support for general 'var' (by-reference) scalar
                  // parameters - see the long comment above ProcSymbol's
                  // param_is_var field for the full design. arg = a
                  // frame-relative local slot number, of the CURRENTLY
                  // EXECUTING frame (the CALLER, about to pass one of its
                  // own locals/parameters as a 'var' argument). Resolves
                  // that slot to its absolute vm_frame_stack[] index
                  // (via vm_local_index(), using the current fp - only
                  // known at runtime, unlike a global's fixed sym_table[]
                  // index) and pushes it ENCODED as a negative int:
                  // -(absolute_index + 1). That encoding is what lets
                  // OP_LOAD_REF/OP_STORE_REF below tell "this reference
                  // is a local frame slot" (negative) apart from "this
                  // reference is a global's sym_table[] index" (zero or
                  // positive - see NODE_VAR_REF, which pushes one of
                  // those directly via a plain OP_PUSH, needing no
                  // opcode of its own). Safe without an extra liveness
                  // check: a reference is only ever created right before
                  // a CALL and consumed during that one call's execution,
                  // never stored anywhere it could outlive the frame it
                  // points into (ordinary call/return discipline keeps
                  // the caller's frame allocated for exactly that long).
    OP_LOAD_REF,  // Pop a reference (as produced by OP_PUSH_LOCAL_REF, or
                  // a plain compile-time PUSH of a global's sym_table[]
                  // index - see NODE_VAR_REF); push the value it refers
                  // to: vm_vars[ref] if ref >= 0, else
                  // vm_frame_stack[-(ref + 1)].
    OP_STORE_REF, // Pop a value, then a reference (same encoding as
                  // OP_LOAD_REF, and the same push order OP_STORE_IDX_DYN
                  // already uses: reference pushed first/deepest, value
                  // pushed last/on top); store the value through it.
    OP_READ_NOFLUSH, // Same as OP_READ (arg = a sym_table[] index), but
                  // for an integer/real/boolean target, does NOT consume
                  // the rest of the input line afterward - this is what
                  // 'read' (as opposed to 'readln') actually differs by.
                  // A string/char target reads a whole line either way
                  // (via fgets(), which already consumes through the
                  // newline as part of reading it) - identical to
                  // OP_READ for those two types, so no behavior to skip.
    OP_READ_LOCAL_INT_NOFLUSH, OP_READ_LOCAL_REAL_NOFLUSH, OP_READ_LOCAL_BOOL_NOFLUSH,
                  // Same as OP_READ_LOCAL_INT/REAL/BOOL, minus the
                  // trailing flush - the local-frame-slot equivalent of
                  // OP_READ_NOFLUSH above. No _STR/_CHAR variants needed,
                  // for the same fgets() reasoning.
    OP_EOF,       // Peek at stdin (via fgetc()+ungetc(), so nothing is
                  // actually consumed); push 1 if there's no more input,
                  // else 0.
    OP_EOLN,      // Peek at stdin the same way; push 1 if the very next
                  // character is a newline OR there's no more input
                  // (matching every real Pascal implementation's
                  // convention that end-of-file also counts as
                  // end-of-line), else 0.
    OP_LOAD_IDXND,  // N-dimensional (N=3 or more) array read. arg =
                  // array's symbol index; N itself isn't a separate
                  // operand here - the VM looks it up as
                  // sym_table[arg].nd_dims. Pops N runtime indices
                  // (pushed dimension-1-first by codegen, so dimension N
                  // ends up on top - popped first); bounds-checks each
                  // against its own dimension (sym_table[arg].nd_lower/
                  // nd_upper); pushes the element at the row-major
                  // offset (computed via nested multiplication - see
                  // vm_array_offset_nd() in vm.c - the direct
                  // generalization of OP_LOAD_IDX2D's
                  // '(i-lower1)*dim2_size+(j-lower2)' formula to however
                  // many dimensions there are).
    OP_STORE_IDXND, // arg = array's symbol index. Pops a value (pushed
                  // last by codegen, so it's on top), then N indices
                  // (same order/bounds-checking as OP_LOAD_IDXND); stores
                  // at the same offset OP_LOAD_IDXND computes.
    OP_LOAD_IDXND_DYN,  // Same as OP_LOAD_IDXND, but for an N-dimensional
                  // array-REFERENCE parameter, where *which* array is
                  // also popped from the stack instead of coming from
                  // arg - needed for the same reason OP_LOAD_IDX_DYN/
                  // OP_LOAD_IDX2D_DYN already exist for 1D/2D (different
                  // calls can pass different arrays). Unlike those,
                  // though, arg here holds N itself (the dimension
                  // count) - always a fixed, compile-time-known property
                  // of the PARAMETER's own declared shape (exactly like
                  // is_2d already is), not something that needs
                  // discovering from the runtime array reference. Pop
                  // order: N indices first (dimension N on top, same as
                  // OP_LOAD_IDXND), THEN the array reference (a
                  // sym_table[] index, pushed first/deepest by codegen -
                  // before any of the indices) - bounds/base then come
                  // from THAT symbol, not arg.
    OP_STORE_IDXND_DYN, // Same as OP_LOAD_IDXND_DYN, but pops a value
                  // (pushed last by codegen, on top of everything) before
                  // the N indices and the array reference.

    // File I/O (text files - see TYPE_FILE). Since a file variable is
    // always GLOBAL (a deliberate scope limitation - see TYPE_FILE),
    // WHICH file is always a compile-time-known sym_table[] index, baked
    // directly into arg exactly like OP_LOAD_IDX2D already bakes an
    // array's sym_table[] index into arg - never pushed/popped on the
    // stack as a runtime value, unlike an array-reference PARAMETER
    // (which needs that because different calls can pass different
    // arrays; a file variable can't, since it can't be a parameter).
    OP_FILE_ASSIGN,  // arg = file variable's sym_table index. Pop a
                  // string_pool[] index (the filename, from the
                  // expression 'assign(f, name)' evaluates); record it
                  // for the file - doesn't open anything yet, matching
                  // real Pascal (assign() only binds a name; reset()/
                  // rewrite() do the actual fopen()).
    OP_FILE_RESET,   // arg = file variable's sym_table index. fopen()s
                  // its assigned filename for reading ("r") - a Runtime
                  // Error if assign() was never called, or if the file
                  // can't be opened (doesn't exist, permissions, ...).
    OP_FILE_REWRITE, // Same as OP_FILE_RESET, but opens for writing ("w") -
                  // creates the file if it doesn't exist, truncates it
                  // if it does, matching real Pascal's rewrite().
    OP_FILE_CLOSE,   // arg = file variable's sym_table index. fclose()s
                  // it - a Runtime Error if it isn't currently open.
    OP_PRINT_FILE, OP_PRINT_STR_FILE, OP_PRINT_BOOL_FILE, OP_FPRINT_FILE,
                  // Same as OP_PRINT/OP_PRINT_STR/OP_PRINT_BOOL/
                  // OP_FPRINT, but arg = the target file variable's
                  // sym_table index and the value is fprintf()'d to that
                  // file instead of stdout - a Runtime Error if the file
                  // isn't currently open for writing.
    OP_PRINT_PADDED_FILE, OP_PRINT_STR_PADDED_FILE, OP_PRINT_BOOL_PADDED_FILE,
    OP_FPRINT_PADDED_FILE, OP_FPRINT_PADDED_PRECISE_FILE,
                  // File-writing siblings of OP_PRINT_PADDED/
                  // OP_PRINT_STR_PADDED/OP_PRINT_BOOL_PADDED/
                  // OP_FPRINT_PADDED/OP_FPRINT_PADDED_PRECISE - same
                  // arg/pop-order conventions as those (see their own
                  // comments above), just fprintf()'d to arg's file.
    OP_NEWLINE_FILE, // arg = file variable's sym_table index. Writes a
                  // newline to it - the file-writing sibling of
                  // OP_NEWLINE.
    OP_EOF_FILE, OP_EOLN_FILE, // arg = file variable's sym_table index.
                  // Same as OP_EOF/OP_EOLN, but peeking at that file
                  // instead of stdin (no stack interaction beyond
                  // pushing the boolean result) - a Runtime Error if the
                  // file isn't currently open for reading.
    OP_READ_FILE, OP_READ_FILE_NOFLUSH, // arg = the READ TARGET's
                  // sym_table index (a global, exactly like OP_READ/
                  // OP_READ_NOFLUSH) - runtime-dispatches on
                  // sym_table[arg].type exactly like those do too. Pops
                  // ONE extra value first, though, that OP_READ/
                  // OP_READ_NOFLUSH don't: the SOURCE file variable's
                  // own sym_table index, pushed via a plain OP_PUSH
                  // right before this opcode (the same "push an
                  // auxiliary reference, the main opcode pops it"
                  // pattern OP_LOAD_IDX_DYN's array-reference argument
                  // already uses, just with OP_PUSH instead of
                  // OP_LOAD_LOCAL since a file's index is always a
                  // compile-time constant, never a runtime local value).
                  // Never prints the interactive "> " prompt OP_READ
                  // does - there's no user to prompt when reading from
                  // a file.
    OP_READ_FILE_LOCAL_INT, OP_READ_FILE_LOCAL_BOOL, OP_READ_FILE_LOCAL_REAL,
    OP_READ_FILE_LOCAL_STR, OP_READ_FILE_LOCAL_CHAR,
    OP_READ_FILE_LOCAL_INT_NOFLUSH, OP_READ_FILE_LOCAL_BOOL_NOFLUSH, OP_READ_FILE_LOCAL_REAL_NOFLUSH,
                  // File-reading siblings of OP_READ_LOCAL_INT/BOOL/REAL/
                  // STR/CHAR (+ the three _NOFLUSH variants - STR/CHAR
                  // don't need one, for the same fgets()-already-
                  // consumes-the-newline reasoning those don't). arg =
                  // local frame slot (exactly like the non-file
                  // versions); pops the source file variable's
                  // sym_table index first, same convention as
                  // OP_READ_FILE above.
    OP_SKIP_PENDING_NEWLINE, // Peeks at stdin (fgetc()+ungetc(), like
                  // OP_EOF/OP_EOLN); if the next character is exactly
                  // '\n', consumes it. No stack interaction, no arg.
                  // Emitted right before a string/char read target that
                  // immediately follows a non-flushing numeric/boolean
                  // one in the same read()/readln() call - see
                  // parse_read_statement()'s comment in parser.c for the
                  // bug this works around (a leftover newline from the
                  // PRECEDING scanf-based read, which fgets() would
                  // otherwise mistake for an empty line). A no-op in
                  // every other situation (there's nothing pending to
                  // skip), so safe to emit only in that one specific
                  // case rather than unconditionally before every
                  // string/char read.
    OP_SKIP_PENDING_NEWLINE_FILE, // Same as OP_SKIP_PENDING_NEWLINE, but
                  // arg = the file variable's sym_table index - peeks at
                  // that file instead of stdin.

    // Records as array elements ('array[lo..hi] of TSomeRecord') - see
    // is_record_array/record_elem_field_count on Symbol above. arg is
    // always the ARRAY's own sym_table index (compile-time-known, exactly
    // like OP_LOAD_IDX/OP_STORE_IDX - a record array is always global, or
    // a local's hidden global, never a by-reference parameter yet). The
    // runtime index and the field's offset within one element both go on
    // the stack (the field offset is a compile-time constant too, but
    // there's only one arg slot per instruction, already used for the
    // array's own index - see codegen.c's NODE_ARRAY_RECORD_FIELD_ACCESS/
    // ASSIGN cases) - index pushed first, then field offset on top,
    // mirroring how NODE_ARRAY_ACCESS_2D pushes its first index before
    // its second.
    OP_LOAD_ARRAY_RECORD_FIELD, // Pops field offset, then runtime index;
                  // pushes vm_array_mem[array_base + (index-lower)*
                  // record_elem_field_count + field_offset].
    OP_STORE_ARRAY_RECORD_FIELD, // Pops value, then field offset, then
                  // runtime index; stores into the same address
                  // OP_LOAD_ARRAY_RECORD_FIELD computes.
    OP_STORE_ARRAY_RECORD_FIELD_CHAR, // Same as OP_STORE_ARRAY_RECORD_
                  // FIELD, but additionally vm_check_char()s the value
                  // first - chosen by codegen instead of the plain
                  // variant whenever the field being written is char-
                  // typed (baked in at compile time, via the assignment
                  // node's own expression_type - unlike OP_STORE_IDX,
                  // which reads sym_table[instr.arg].type directly, one
                  // array-of-records Symbol has no single element type
                  // to dispatch on at runtime).

    // Pointers ('type PFoo = ^Target;', 'new'/'dispose', 'p^'/'p^.field') -
    // this VM's first and only source of genuinely dynamic allocation
    // (vm_heap_mem[]/vm_heap_count/vm_heap_freelist[] in vm.c). Unlike
    // OP_LOAD_ARRAY_RECORD_FIELD's family, there's no "which array" -
    // every pointer, regardless of declared type, allocates from the
    // SAME one shared heap, so a dereference's runtime base (which block)
    // always comes off the stack, never `arg` - freeing `arg` up to carry
    // the field offset directly (a compile-time constant) instead of
    // needing its own stack push the way the array-of-records family
    // does.
    OP_NEW,       // arg = element size (ints per instance: 1 for a scalar
                  // target, or the target record type's field_count).
                  // First tries vm_heap_freelist[arg] (a size-bucketed
                  // freelist - see OP_DISPOSE); if non-empty, pops and
                  // reuses that block. Otherwise bump-allocates `arg`
                  // fresh ints from vm_heap_mem (aborting - a Runtime
                  // Error, not UB - if that would exceed MAX_HEAP_MEM).
                  // Either way, pushes the resulting block's offset.
    OP_DISPOSE,   // arg = element size (same meaning as OP_NEW's). Pops a
                  // pointer value (a Runtime Error, not silently ignored,
                  // if it's nil or otherwise not a currently-valid heap
                  // offset); links it onto the front of
                  // vm_heap_freelist[arg] for a later same-size OP_NEW to
                  // reuse (the block's own first int slot stores the
                  // previous freelist head - a classic intrusive
                  // freelist, needing no separate bookkeeping array).
                  // Does NOT push anything back, and does NOT touch
                  // whatever variable held this pointer value - matching
                  // standard Pascal, where a disposed pointer's value is
                  // left undefined, not implicitly niled (see
                  // NODE_HEAP_DISPOSE).
    OP_LOAD_HEAP_FIELD, // arg = field offset (0 for a scalar pointer
                  // target's whole '^'). Pops a pointer value (a Runtime
                  // Error - "Nil pointer dereference" - if it's nil);
                  // pushes vm_heap_mem[value + arg].
    OP_STORE_HEAP_FIELD, // Same addressing as OP_LOAD_HEAP_FIELD. Pops a
                  // value, then a pointer value (nil-checked the same
                  // way); stores into vm_heap_mem[pointer value + arg].
    OP_STORE_HEAP_FIELD_CHAR, // Same as OP_STORE_HEAP_FIELD, but
                  // additionally vm_check_char()s the value first -
                  // chosen by codegen instead of the plain variant
                  // whenever the field being written is char-typed (same
                  // reasoning as OP_STORE_ARRAY_RECORD_FIELD_CHAR - one
                  // shared heap has no single element type to dispatch a
                  // char-check on at runtime the way OP_STORE does via
                  // sym_table[]).

    OP_LOAD_VTABLE_SLOT, // Virtual/dynamic method dispatch (NODE_VIRTUAL_CALL
                  // in codegen.c). arg = the method's SLOT number - its
                  // index within the class's own PointerTypeDef.methods[]
                  // at compile time, which parser.c's inheritance
                  // flattening already guarantees is the SAME index for a
                  // given logical method across an entire ancestor/
                  // descendant hierarchy (an override replaces its
                  // inherited entry in place, never appends). Pops a
                  // runtime class_id (an instance's own hidden type tag,
                  // read via OP_LOAD_HEAP_FIELD 0 - see NODE_HEAP_ALLOC's
                  // tag-write comment); pushes
                  // vm_vtables[class_id * MAX_CLASS_METHODS + arg] - the
                  // actual implementing procedure's entry address, ready
                  // for OP_CALL_INDIRECT. No bounds check on class_id -
                  // it's always a value THIS compiler itself wrote via
                  // OP_NEW's tag-write, never user data.
    OP_STORE_VTABLE_SLOT, // Populates one vm_vtables[] slot at program
                  // startup (NODE_VTABLE_INIT_ENTRY in codegen.c, emitted
                  // once per class method, ahead of any user code - see
                  // parser.c's build_vtable_init_chain()). arg = the
                  // FULLY PRECOMPUTED flat index (class_id *
                  // MAX_CLASS_METHODS + slot) - unlike
                  // OP_LOAD_VTABLE_SLOT's arg, both halves are already
                  // known at compile time here, so there's no separate
                  // runtime class_id to pop. Pops a procedure entry
                  // address (pushed by a NODE_PROC_REF, same as any other
                  // procedure-value use); stores it into
                  // vm_vtables[arg].

    // Nested procedure/function declarations - lexical access to an
    // enclosing procedure's own locals, at arbitrary nesting depth, via a
    // static-link chain (see vm_static_link[] in vm.c). Distinct from
    // OP_CALL's return-address call stack and from OP_ENTER/OP_RET's
    // frame-pointer save/restore, which are about the DYNAMIC (caller)
    // chain - the static link is a separate, LEXICAL (declaring-scope)
    // chain, tracked per-frame in vm_static_link[fp].
    OP_PUSH_STATIC_LINK, // arg = hop count, a compile-time constant from
                  // comparing the callee's lexical parent against the
                  // procedure currently being compiled. 0 = "push my own
                  // current fp" (calling a direct lexical child). N > 0 =
                  // "walk my own static-link chain N times starting at my
                  // own fp, push the result" (calling deeper into an
                  // ancestor's other nested descendants). -1 = the
                  // sentinel "no valid link" - the flat-namespace escape
                  // hatch means a nested procedure can be called from
                  // somewhere its lexical parent isn't an active ancestor;
                  // this is legal right up until the callee actually tries
                  // to use OP_LOAD_ENCLOSING/OP_STORE_ENCLOSING/
                  // OP_PUSH_ENCLOSING_REF, at which point walking into the
                  // sentinel is a Runtime Error, not silently wrong data.
                  // Emitted right before OP_CALL, only when the callee is
                  // itself a nested procedure - never emitted for a call
                  // to a top-level (non-nested) procedure.
    OP_POP_STATIC_LINK, // No arg. Pops one value (pushed by the caller's
                  // OP_PUSH_STATIC_LINK) and records it as vm_static_link[]
                  // for the frame currently being entered. Emitted once,
                  // in the prologue of every nested procedure, right after
                  // OP_ENTER and before its ordinary parameter-unpacking
                  // OP_STORE_LOCAL sequence (the caller pushes real
                  // arguments first, the static link last/on top).
    OP_LOAD_ENCLOSING,  // arg packs (levels_up, slot) as
                  // (levels_up << 12) | slot. Walks vm_static_link[]
                  // levels_up times starting from the current fp (a
                  // Runtime Error if that walk hits the -1 sentinel),
                  // then pushes vm_frame_stack[target_fp + slot]. Only
                  // ever emitted for levels_up >= 1 - the levels_up == 0
                  // case still uses the plain OP_LOAD_LOCAL.
    OP_STORE_ENCLOSING, // Same addressing/encoding as OP_LOAD_ENCLOSING.
                  // Pops a value; stores it into
                  // vm_frame_stack[target_fp + slot].
    OP_PUSH_ENCLOSING_REF, // The levels_up-aware generalization of
                  // OP_PUSH_LOCAL_REF, same (levels_up, slot) packed arg
                  // and the same chain walk as OP_LOAD_ENCLOSING/
                  // OP_STORE_ENCLOSING. Pushes -(target_fp + slot + 1) -
                  // literally OP_PUSH_LOCAL_REF's own encoding, just
                  // computed from an ancestor frame's fp instead of the
                  // current one. Lets a nested procedure forward one of
                  // its enclosing scope's locals as a 'var' argument to a
                  // further call - OP_LOAD_REF/OP_STORE_REF downstream
                  // need no changes, since they already treat any
                  // negative value as "an absolute vm_frame_stack[]
                  // index" regardless of which opcode produced it.

    // Additional stack-manipulation opcodes, for hand-written .sasm only -
    // the compiler never emits these (OP_DUP/OP_POP already cover every
    // stack shape codegen.c needs). No arg on any of the three.
    OP_SWAP,      // Swap the top two stack values: ( a b -- b a ).
    OP_OVER,      // Copy the second-from-top value to the top:
                  // ( a b -- a b a ).
    OP_ROT,       // Rotate the top three values, bringing the third-from-
                  // top to the top: ( a b c -- b c a ).

    // VM debug built-ins, for hand-written .sasm only - the compiler never
    // emits these. Both print to stdout and leave the stack/globals
    // untouched; neither depends on -v.
    OP_DEBUG_STACK, // No arg. Prints the current evaluation stack,
                  // bottom to top.
    OP_DEBUG_SYMS,  // No arg. Prints every global variable/array's
                  // current value by name - the same listing OP_HALT
                  // prints under -v, callable mid-program instead of
                  // only at the very end of a run.

    OP_CALL_INDIRECT, // No arg. Pops a code address (a target that was
                  // only known at RUNTIME, unlike OP_CALL's compile-
                  // time-constant arg - see NODE_CALL_INDIRECT in
                  // codegen.c, the call-through-a-procedural-parameter
                  // case). Pushes {return_addr (ip as it already
                  // stands), fp, frame_sp} onto vm_call_stack[], exactly
                  // like OP_CALL; sets ip to the popped address. No
                  // static-link push/pop around it: the only value ever
                  // pushed here is a top-level procedure's own
                  // entry_address (see param_is_proc in ProcSymbol),
                  // and a top-level procedure's prologue never reads
                  // vm_static_link[] in the first place.

    OP_LOAD_HEAP_ARRAY_FIELD, // Arg = a class array field's combined
                  // compile-time offset (its own base offset within the
                  // instance, minus the array's declared lower bound -
                  // see resolve_heap_deref_step()'s NODE_HEAP_ARRAY_
                  // FIELD_ACCESS comment). Pops a runtime, already-
                  // bounds-checked index, then a heap base pointer;
                  // pushes vm_heap_mem[base + arg + index]. The
                  // scalar-field opcodes (OP_LOAD_HEAP_FIELD etc.) take
                  // only a base, since their offset is always exactly
                  // one compile-time constant - an array field element
                  // additionally needs a runtime index, which is why
                  // this is a distinct opcode rather than a reused one.
    OP_STORE_HEAP_ARRAY_FIELD, // Write counterpart - pops a value, then
                  // an index, then a base; stores into
                  // vm_heap_mem[base + arg + index].
    OP_STORE_HEAP_ARRAY_FIELD_CHAR, // Same as OP_STORE_HEAP_ARRAY_FIELD,
                  // but validates the value is a legal char first (same
                  // reasoning as OP_STORE_HEAP_FIELD_CHAR - one shared
                  // untyped heap has no per-slot type for the VM to
                  // dispatch a char-check on at runtime the way OP_STORE
                  // does via sym_table[]).
    OP_PARAM_COUNT, // Pushes the number of command-line arguments passed
                  // to the running program (excluding argument 0, the
                  // .bin path itself - see vm_set_program_args() in
                  // vm.c), matching real Pascal's own ParamCount
                  // convention. No operand - the count is VM state set
                  // once at startup, not a compile-time constant.
    OP_PARAM_STR, // Pops an integer index; pushes the corresponding
                  // command-line argument as a string_pool[] index
                  // (already interned at VM startup - see
                  // vm_set_program_args()), or an interned empty string
                  // if the index is out of range (0..ParamCount) -
                  // matching real Pascal/Free Pascal's own documented
                  // lenient behavior, not this VM's usual abort-on-out-
                  // of-range convention for array indexing. No operand.
    OP_TRY,       // arg = the except-body's start address. Pushes
                  // {sp, call_sp, fp, frame_sp, arg} onto vm_except_stack[]
                  // - a snapshot to restore to if a raise occurs anywhere
                  // in the try-body, including several call frames deep.
                  // Overflow past MAX_EXCEPT_DEPTH is a VM Runtime Error.
    OP_END_TRY,   // The try-body finished with no exception - pop
                  // vm_except_stack[] (this handler is no longer active).
                  // No operand. Popping an empty stack is a VM Runtime
                  // Error (unreachable from valid codegen).
    OP_RAISE,     // Pops a string_pool[] index (the message). If
                  // vm_except_stack[] is empty: VM Runtime Error
                  // ("Unhandled exception: <message>"), fatal. Else: pops
                  // the innermost handler (consuming it, so a raise
                  // inside the except-body itself reaches the NEXT
                  // enclosing handler, not this one again), restores
                  // sp/call_sp/fp/frame_sp from it, records the message
                  // in vm_current_exception_msg (see OP_EXCEPT_MSG), and
                  // jumps to the handler's except-body address - all
                  // without touching error.c's fatal_abort()/longjmp
                  // mechanism, since every value being restored is VM-
                  // local state already inside run_vm()'s one dispatch
                  // loop. No operand.
    OP_EXCEPT_MSG, // Pushes vm_current_exception_msg directly (a
                  // string_pool[] index - already-interned entries are
                  // immutable, so no re-interning needed, unlike
                  // OP_PARAM_STR's argv-at-startup interning). Meaningful
                  // only when executed inside an except-body. No operand.
    OP_IS_INSTANCE, // 'obj is TFoo' / one step of 'obj as TFoo' - arg =
                  // target class_id (a pointer_types[] index, same
                  // meaning as OP_LOAD_VTABLE_SLOT's runtime class_id).
                  // Pops an object pointer. If < 0 (nil): pushes 0
                  // immediately WITHOUT touching vm_heap_mem[] at all -
                  // unlike OP_LOAD_HEAP_FIELD, a nil operand here is not
                  // a fatal error, since 'nil is X' must be False, not
                  // an abort. Otherwise reads the tag directly from
                  // vm_heap_mem[obj] (offset 0 - inlined here rather than
                  // composed from a separate OP_LOAD_HEAP_FIELD 0, for
                  // exactly the reason above), then walks
                  // vm_class_parent[] upward from that tag (own class,
                  // parent, grandparent, ...) until arg is found (push 1)
                  // or the walk reaches the -1 root sentinel (push 0).
    OP_STORE_CLASS_PARENT // Populates one vm_class_parent[] slot at
                  // program startup (NODE_CLASS_PARENT_INIT_ENTRY in
                  // codegen.c, emitted once per class, ahead of any user
                  // code - see parser.c's build_class_parent_init_chain()
                  // - same population pattern as OP_STORE_VTABLE_SLOT).
                  // arg = this class's own class_id. Pops a parent
                  // class_id (or -1 for none); stores it into
                  // vm_class_parent[arg].
} Opcode;

typedef struct {
    Opcode op;
    int arg;
} Instruction;

typedef enum {
    NODE_COMPOUND,
    NODE_ASSIGN,   // Scalar: left = value expr, right unused.
                   // Array element (sym_table[data.var_idx].is_array):
                   // left = index expr, right = value expr.
    NODE_UNARY_OP,
    NODE_BINARY_OP,
    NODE_NUMBER,
    NODE_BOOLEAN,
    NODE_VARIABLE,
    NODE_WRITELN,  // Also covers 'write' - node->op is TOKEN_WRITE or
                   // TOKEN_WRITELN (only the latter appends a newline).
                   // node->left is the head of the argument list, chained
                   // via each argument's own ->next (same technique as a
                   // statement list) - NULL means zero arguments.
                   // node->extra = an optional file variable reference
                   // (a NODE_VARIABLE, expression_type TYPE_FILE) -
                   // NULL means stdout, exactly as before this field
                   // existed (see 'write(f, ...)' in docs/LANGUAGE.md).
    NODE_READLN,   // data.var_idx = target's sym_table index, op =
                   // TOKEN_READ/TOKEN_READLN (only the latter consumes
                   // the rest of the input line). extra = an optional
                   // file variable reference (a NODE_VARIABLE,
                   // expression_type TYPE_FILE) - NULL means stdin,
                   // exactly as before this field existed.
    NODE_IF,
    NODE_WHILE,
    NODE_REPEAT,
    NODE_STRING,
    NODE_FOR,
    NODE_ARRAY_ACCESS, // 'arr[i]' as an expression. data.var_idx = the
                       // array's symbol index, left = index expression.
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_CALL, // A procedure or function call. data.var_idx = proc_table
               // index. left is the head of the argument list, chained
               // via each argument's own ->next (same technique as
               // write/writeln's argument list). op is TOKEN_PROCEDURE
               // when used as a statement - if the target is a function,
               // its return value is popped and discarded (see OP_POP) -
               // or left unset (0) when used as an expression, where the
               // return value stays on the stack for the caller.
    NODE_LOCAL_VAR,    // A parameter/local read, as an expression.
                       // data.var_idx = frame-relative slot index.
    NODE_LOCAL_ASSIGN, // A parameter/local write. left = value expr.
                       // data.var_idx = slot index. expression_type is
                       // (re)used here to hold the target's declared
                       // type, set at parse time - locals aren't in
                       // sym_table, so unlike NODE_ASSIGN there's no
                       // table for the type checker to look the type up
                       // in later.
    NODE_REF_ARRAY_ACCESS, // 'arr[i]' where arr is a by-reference array
                       // parameter. data.var_idx = the PARAMETER's local
                       // slot (holding a runtime sym_table[] index - not
                       // the array's index directly, unlike
                       // NODE_ARRAY_ACCESS). left = index expression.
    NODE_REF_ARRAY_ASSIGN, // 'arr[i] := val' for a by-reference array
                       // parameter. data.var_idx = the parameter's local
                       // slot. left = index expr, right = value expr.
    NODE_REF_ARRAY_ACCESS_2D, // 'arr[i, j]' where arr is a by-reference
                       // 2D array parameter. data.var_idx = the
                       // parameter's local slot. left/right = the two
                       // index expressions.
    NODE_REF_ARRAY_ASSIGN_2D, // 'arr[i, j] := val' for a by-reference 2D
                       // array parameter. data.var_idx = the parameter's
                       // local slot. left/right = the two index
                       // expressions, extra = value expr - same field
                       // layout as NODE_ARRAY_ASSIGN_2D's global version.
    NODE_REF_ARRAY_ACCESS_ND, // 'arr[i1, ..., iN]' (N>=3) where arr is a
                       // by-reference N-dimensional array parameter.
                       // data.var_idx = the parameter's local slot
                       // (holding a runtime sym_table[] index). left =
                       // head of the index-expression chain (N nodes,
                       // each linked via its OWN ->next - NOT this
                       // node's ->next, which stays free the same way
                       // NODE_WRITELN's argument list and NODE_SET_
                       // CONSTRUCTOR's element list already chain their
                       // own children without disturbing the enclosing
                       // statement-list ->next). Exists as its own node
                       // type (rather than trying to fit into
                       // NODE_REF_ARRAY_ACCESS_2D's left/right pair)
                       // because ASTNode has only 4 child pointers and a
                       // 3+D access/assignment needs a variable-length
                       // index list - the sibling-chain technique is the
                       // established way this project already handles
                       // "a node needs an arbitrary-length list of
                       // children" without growing the struct.
    NODE_REF_ARRAY_ASSIGN_ND, // 'arr[i1, ..., iN] := val' (N>=3) for a
                       // by-reference N-dimensional array parameter.
                       // data.var_idx = the parameter's local slot.
                       // left = head of the index-expression chain,
                       // right = value expression, next = next
                       // statement (extra unused).
    NODE_ARRAY_REF,    // Pushes a global (or mangled-local, which is just
                       // a hidden global) array's sym_table[] index as a
                       // compile-time-known literal - used when passing
                       // such an array as an argument to an array-
                       // reference parameter. data.var_idx = the array's
                       // sym_table[] index. Codegen-identical to
                       // NODE_NUMBER (a plain PUSH), but kept as its own
                       // type so dead-code elimination's usage tracking
                       // recognizes this as a genuine use of that array -
                       // a NODE_NUMBER would look like an arbitrary
                       // literal to it, and the array's own assignments
                       // would then look unused and get eliminated, even
                       // though this is the only place they're ever read.
    NODE_BUILTIN_CALL, // length/copy/pos/upcase/uppercase/lowercase/left/
                       // right, distinguished by node->op (TOKEN_LENGTH,
                       // TOKEN_COPY, etc. - mid/inpos are aliased onto
                       // TOKEN_COPY/TOKEN_POS at parse time, since they're
                       // identical operations under a different name).
                       // Arguments are left/right/extra (up to 3, however
                       // many that builtin takes) - NOT chained via
                       // ->next like NODE_CALL's user-function arguments,
                       // since every builtin here has a small, fixed
                       // arity, unlike a general call. For op ==
                       // TOKEN_EOF_FN/TOKEN_EOLN specifically: left = an
                       // optional file variable reference (a
                       // NODE_VARIABLE, expression_type TYPE_FILE) - NULL
                       // means stdin, exactly as before this field existed.
    NODE_STRING_INDEX,       // 's[i]' where s is a GLOBAL string/char
                       // variable. data.var_idx = s's sym_table index,
                       // left = index expression, expression_type =
                       // TYPE_CHAR. Read-only (not a valid assignment
                       // target) - see docs for why.
    NODE_LOCAL_STRING_INDEX, // Same as above, but s is a parameter/local
                       // string/char variable. data.var_idx = s's frame
                       // slot.
    NODE_ARRAY_ACCESS_2D, // 'arr[i, j]' as an expression, for a GLOBAL 2D
                       // array. data.var_idx = the array's symbol index.
                       // left = first index, right = second index.
    NODE_ARRAY_ASSIGN_2D,  // 'arr[i, j] := val' for a GLOBAL 2D array.
                       // data.var_idx = the array's symbol index.
                       // left = first index, right = second index,
                       // extra = value expression.
    NODE_ARRAY_ACCESS_ND, // 'arr[i1, ..., iN]' (N>=3) as an expression,
                       // for a GLOBAL (or local, which is just a hidden
                       // global - see add_local_array()) N-dimensional
                       // array. data.var_idx = the array's symbol index.
                       // left = head of the index-expression chain (N
                       // nodes, each linked via its OWN ->next - see
                       // NODE_REF_ARRAY_ACCESS_ND's comment above for why
                       // this needs its own node type rather than more
                       // named pointer fields).
    NODE_ARRAY_ASSIGN_ND,  // 'arr[i1, ..., iN] := val' (N>=3) for a
                       // GLOBAL/local N-dimensional array. data.var_idx =
                       // the array's symbol index. left = head of the
                       // index-expression chain, right = value
                       // expression, next = next statement (extra
                       // unused).
    NODE_ARRAY_RECORD_FIELD_ACCESS, // 'arr[i].field' as an expression, for
                       // a GLOBAL (or local, hidden-global - see
                       // add_local_array_rec()) 1D array of records.
                       // data.var_idx = the array's symbol index. left =
                       // index expression. right = a NODE_NUMBER literal
                       // holding the field's offset within one element
                       // (0..record_elem_field_count-1) - a compile-time
                       // constant, read directly via ->data.num_value,
                       // never code-generated as its own push (mirrors
                       // NODE_RANGE_CHECK's bound literals above).
                       // expression_type = the field's own declared type,
                       // set at parse time (record_types[] - the table
                       // that field type comes from - never leaves
                       // parser.c, so nothing downstream could look it up
                       // itself).
    NODE_ARRAY_RECORD_FIELD_ASSIGN, // 'arr[i].field := val' - the write
                       // counterpart. data.var_idx = the array's symbol
                       // index. left = index expression, right = value
                       // expression, extra = a NODE_NUMBER literal
                       // holding the field's offset (same convention as
                       // the access node's ->right). expression_type =
                       // the field's declared type - used by the type
                       // checker for the usual assignment-compatibility
                       // check, AND by codegen to choose the char-
                       // checking opcode variant (OP_STORE_ARRAY_RECORD_
                       // FIELD_CHAR) when the field being written is
                       // char-typed - the VM has no per-field type table
                       // to dispatch on at runtime the way OP_STORE_IDX
                       // does via sym_table[].type, since one array-of-
                       // records Symbol covers many differently-typed
                       // fields at once.
    NODE_WRITE_ARG,    // One argument to write/writeln, wrapping its
                       // optional ':width[:precision]' field-width
                       // syntax. left = the value expression, right =
                       // width expression (NULL if not given), extra =
                       // precision expression (NULL if not given -
                       // real-typed values only). expression_type
                       // mirrors left's, set once left is type-checked.
                       // Every write/writeln argument gets wrapped in
                       // one of these, even with no ':width' at all, so
                       // codegen always sees a uniform shape.
    NODE_REAL_NUMBER,  // A real literal. data.num_value holds the exact
                       // bit pattern (via float_to_bits), not the value
                       // itself - reusing the same int union member
                       // NODE_NUMBER uses, just reinterpreted. Kept as
                       // its own node type (not folded into NODE_NUMBER)
                       // so nothing that inspects node type - constant
                       // folding, in particular - can mistake a real
                       // literal's bit pattern for a plain integer value.
    NODE_INT_TO_REAL,  // Implicit widening: left is an integer-typed
                       // expression; this node's own value is its real
                       // equivalent. Inserted by the type checker
                       // wherever Pascal's automatic int->real widening
                       // applies (mixed arithmetic, assigning an integer
                       // to a real variable) - codegen never has to
                       // decide this itself, it just sees the wrapper.
    NODE_STRING_INDEX_ASSIGN,       // 's[i] := val' where s is a GLOBAL
                       // string/char variable. data.var_idx = s's
                       // sym_table index, left = index expression,
                       // right = the new character (must be exactly one
                       // character - checked at runtime, same as any
                       // other char-typed storage in this VM).
    NODE_LOCAL_STRING_INDEX_ASSIGN, // Same as above, but s is a
                       // parameter/local string/char variable.
                       // data.var_idx = s's frame slot.
    NODE_LOCAL_FOR,    // 'for i := start to/downto end do body' where i
                       // is a parameter/local variable. data.var_idx =
                       // i's frame slot, op = TOKEN_TO/TOKEN_DOWNTO,
                       // left = start bound, extra = body - all exactly
                       // like NODE_FOR. The one structural difference:
                       // right is always a NODE_LOCAL_VAR referencing a
                       // hidden local slot that's already been assigned
                       // the end bound's value, rather than the raw end-
                       // bound expression itself. That caching is
                       // desugared entirely at parse time (an ordinary
                       // NODE_LOCAL_ASSIGN emitted just before this node,
                       // both wrapped in a NODE_COMPOUND) precisely so
                       // the hidden slot gets reserved during parsing,
                       // before the enclosing procedure's local count is
                       // finalized and ENTER is emitted - reserving it
                       // later, during codegen, would be too late. The
                       // payoff: codegen for this node needs zero special
                       // end-bound-caching logic at all, unlike NODE_FOR.
    NODE_LOCAL_READLN, // 'readln(x)' where x is a parameter/local
                       // variable. data.var_idx = x's frame slot,
                       // expression_type = x's declared type (set by the
                       // parser, since codegen needs to pick one of the
                       // OP_READ_LOCAL_* opcodes below at compile time -
                       // unlike OP_READ's runtime type dispatch via
                       // sym_table[], a local frame slot carries no
                       // runtime type information to dispatch on at all).
                       // extra = an optional file variable reference (a
                       // NODE_VARIABLE, expression_type TYPE_FILE) - NULL
                       // means stdin, exactly as before this field existed.
    NODE_RANGE_CHECK,  // Wraps a value about to be stored into a subrange-
                       // typed target ('type TAge = 0..150;'). left = the
                       // value expression; right/extra = NODE_NUMBER
                       // literals holding the lower/upper bound (always
                       // compile-time constants - see parse_type_section()
                       // in parser.c) - reusing existing child pointers
                       // for two plain int constants rather than adding
                       // new ASTNode fields. expression_type = TYPE_INTEGER
                       // (a subrange is fully assignment/arithmetic-
                       // compatible with integer - unlike an enum, it's
                       // not a distinct type the type checker tracks).
    NODE_ASSERT,       // 'assert(cond)' / 'assert(cond, msg)'. left =
                       // condition (must be boolean); right = message
                       // (must be string/char) - always present, even
                       // for the no-message form: the parser synthesizes
                       // a NODE_STRING literal ("Assertion failed") when
                       // none is given, so codegen/the VM never need to
                       // handle a "no message" case separately.
    NODE_FILE_OP,      // 'assign(f, name)' / 'reset(f)' / 'rewrite(f)' /
                       // 'close(f)' - one node type for all four,
                       // distinguished by node->op (TOKEN_FILE_ASSIGN/
                       // TOKEN_RESET/TOKEN_REWRITE/TOKEN_CLOSE), mirroring
                       // how NODE_WRITELN reuses op for TOKEN_WRITE vs
                       // TOKEN_WRITELN. data.var_idx = the file
                       // variable's sym_table index (always global - see
                       // TYPE_FILE). left = the filename expression
                       // (string/char-typed) - only meaningful, and only
                       // ever present, when op == TOKEN_FILE_ASSIGN;
                       // NULL for reset/rewrite/close, which take no
                       // second argument.
    NODE_CASE,         // 'case selector of label1: stmt1; ... [else
                       // stmtN] end'. left = selector expression, right =
                       // head of a NODE_CASE_ARM chain (each arm linked
                       // via its own ->next), extra = the else-branch
                       // statement (NULL if there isn't one). data.var_idx
                       // = string_pool index of a "no matching case
                       // label and no else clause" runtime-error message,
                       // synthesized at parse time exactly like
                       // NODE_ASSERT's default message above - used by
                       // codegen only when extra is NULL, reusing
                       // OP_ASSERT itself (an unconditional false
                       // condition) rather than needing a new opcode.
    NODE_CASE_ARM,     // One 'label1, label2: statement' arm of a
                       // NODE_CASE. left = head of that arm's case-label-
                       // value chain (leaf nodes - NODE_NUMBER/
                       // NODE_STRING/NODE_BOOLEAN - each already carrying
                       // its own expression_type, chained via ->next).
                       // right = the arm's statement.
    NODE_VAR_REF,      // A 'var' argument that resolves to a GLOBAL
                       // scalar (or a static local, or a global record's
                       // field - all of which ARE a global under the
                       // hood): pushes that global's sym_table[] index as
                       // a compile-time-known literal - the reference
                       // value itself. data.var_idx = the sym_table[]
                       // index. Codegen-identical to NODE_ARRAY_REF (a
                       // plain PUSH), but kept as its own type for the
                       // same reason NODE_ARRAY_REF is: dead-code
                       // elimination's usage tracking needs to recognize
                       // this as a genuine use of that global scalar, not
                       // an arbitrary literal (a NODE_NUMBER) or an array
                       // use (NODE_ARRAY_REF itself).
    NODE_LOCAL_VAR_REF, // A 'var' argument that resolves to one of the
                       // CALLER's own PLAIN local/parameter scalars (or a
                       // local record's field) - data.var_idx = that
                       // local's frame-relative slot number. Unlike
                       // NODE_VAR_REF, this can't be a compile-time
                       // constant: the actual vm_frame_stack[] index
                       // depends on the CALLER's frame pointer, only
                       // known at runtime - see OP_PUSH_LOCAL_REF.
                       // (Forwarding the caller's OWN 'var' parameter
                       // through to another call needs neither of these
                       // two node types - its raw local slot value IS
                       // already a valid reference, from its own caller,
                       // so an ordinary NODE_LOCAL_VAR read passes it
                       // through unchanged.)
    NODE_VAR_PARAM_READ, // Reads through a 'var' parameter, inside the
                       // procedure that declared it. data.var_idx = the
                       // parameter's own local frame slot, which holds
                       // an ENCODED REFERENCE (from one of the two node
                       // types above, or a forwarded one), not the value
                       // itself.
    NODE_VAR_PARAM_ASSIGN, // Writes through a 'var' parameter. left =
                       // value expression. data.var_idx = the parameter's
                       // own local frame slot (see NODE_VAR_PARAM_READ).
                       // expression_type = the target's declared type -
                       // needed for the same reason NODE_LOCAL_ASSIGN
                       // needs it (type_checker.c has no table to look a
                       // local's type up in later).
    NODE_SET_CONSTRUCTOR, // '[e1, e2, e3..e4, ...]' - a set literal. left
                       // = head of a chain of ordinal-valued element
                       // expressions (each already an ordinary
                       // int-producing expression - a range 'a..b' is
                       // unrolled into (b-a+1) individual NODE_NUMBER
                       // elements at parse time, since this compiler
                       // doesn't have a runtime loop primitive to build
                       // one otherwise), chained via each element's own
                       // ->next (same technique as NODE_WRITELN's
                       // argument list). expression_type = TYPE_SET.
                       // Codegen starts with an empty (0) accumulator and
                       // ORs in '1 << element' for each one - no new
                       // opcodes needed (PUSH/SHL/BOR already exist).
    NODE_SET_IN,       // 'x in s' - set membership test. left = the
                       // ordinal value (x), right = the set expression
                       // (s), expression_type = TYPE_BOOLEAN. Deliberately
                       // its own node type rather than folded into
                       // NODE_BINARY_OP: codegen needs '1 << x' evaluated
                       // BEFORE combining with s, an ordering the generic
                       // NODE_BINARY_OP codegen (which always generates
                       // left then right first) can't produce without a
                       // stack-shuffling opcode this VM doesn't have.
    NODE_LABEL,        // '<N>: statement' - a labelled statement, where N
                       // is an unsigned integer declared in the enclosing
                       // block's 'label' section. data.num_value = the
                       // label's id; left = the wrapped statement (this
                       // node itself threads ->next to whatever statement
                       // follows it in the enclosing list, exactly like
                       // any other statement node - the wrapping is
                       // transparent to that chain). Codegen (see
                       // codegen.c's label_table) records this label's
                       // code_idx the moment it's reached, patching any
                       // NODE_GOTO placeholders recorded against this id
                       // beforehand (a forward goto - one appearing
                       // earlier in the source than the label it targets).
    NODE_GOTO,         // 'goto <N>;' - data.num_value = the target
                       // label's id, already validated at parse time
                       // against the enclosing block's 'label' section
                       // (parser.c's declared_labels[]). Compiles to a
                       // single unconditional OP_JMP: straight to the
                       // label's code_idx if already known (a backward
                       // goto), or a placeholder patched once that label
                       // is reached (a forward goto) - the same emit-
                       // then-patch technique NODE_IF/NODE_WHILE/etc.
                       // already use, keyed by label id instead of a
                       // loop's break/continue targets. Restricted to
                       // targeting a label in the SAME block (procedure
                       // or main program): the label table is reset at
                       // the start of every block's own codegen (see
                       // generate_block()), so a goto can never - even
                       // accidentally - jump across a procedure boundary.
    NODE_HEAP_FIELD_ACCESS, // 'p^' or 'p^.field' as an expression - p
                       // (or a longer chain, e.g. 'p^.next^.data') is
                       // left, an already-built expression evaluating to
                       // a heap offset (or nil - see TYPE_NIL). right =
                       // a NODE_NUMBER literal holding the field's
                       // compile-time-constant offset within the pointed-
                       // to block (0 for a scalar target's whole '^',
                       // otherwise the target record type's field index -
                       // resolved at parse time via parser.c's
                       // pointer_types[]/record_types[], exactly like
                       // NODE_ARRAY_RECORD_FIELD_ACCESS's own ->right).
                       // expression_type = the field's (or scalar
                       // target's) declared type. A multi-level chain
                       // ('p^.next^.data') is built as nested
                       // NODE_HEAP_FIELD_ACCESS nodes, each one's ->left
                       // being the previous step's whole node.
    NODE_HEAP_FIELD_ASSIGN, // 'p^ := val' or 'p^.field := val' - the
                       // write counterpart. left = the base pointer
                       // expression (as above - the LAST '^' step of a
                       // longer chain; any EARLIER steps are already
                       // baked into left as nested NODE_HEAP_FIELD_ACCESS
                       // reads, same principle as NODE_ARRAY_RECORD_
                       // FIELD_ASSIGN's index expression). right = the
                       // value expression. extra = a NODE_NUMBER literal
                       // holding the field offset (same convention as the
                       // access node's ->right). expression_type = the
                       // field's/target's declared type - used by the
                       // type checker AND by codegen, to choose
                       // OP_STORE_HEAP_FIELD_CHAR over the plain variant
                       // when the field being written is char-typed
                       // (same reasoning as NODE_ARRAY_RECORD_FIELD_
                       // ASSIGN - one shared heap has no single element
                       // type for the VM to dispatch a char-check on at
                       // runtime the way OP_STORE does via sym_table[]).
    NODE_HEAP_ALLOC,   // 'new(p)''s VALUE side only - p itself is
                       // whatever ordinary assignment target p already
                       // resolves to (a plain global/local, a with-field,
                       // a record field, or a static local - NOT a heap-
                       // dereferenced target like 'p^.next', a deliberate
                       // scope cut; see docs/LANGUAGE.md). 'new(p);'
                       // desugars at parse time into 'p := <this node>;'
                       // straight through whichever existing assignment
                       // node (NODE_ASSIGN/NODE_LOCAL_ASSIGN/etc.) p's own
                       // resolution already builds - this node carries no
                       // children at all, just data.num_value = the
                       // target pointer type's element size (a compile-
                       // time constant: 1 for a scalar target, or the
                       // target record type's field_count), which codegen
                       // passes straight through as OP_NEW's arg.
    NODE_HEAP_DISPOSE, // 'dispose(p);' - unlike 'new', this only ever
                       // READS p (an ordinary expression - p can be
                       // anything pointer-typed, including a heap-
                       // dereferenced chain like 'p^.next', since no
                       // write-back into p happens - see docs/LANGUAGE.md
                       // for why this compiler deliberately does NOT nil
                       // p afterward, matching standard Pascal's own
                       // "the pointer's value is undefined after dispose"
                       // semantics rather than inventing a safer
                       // non-standard behavior). left = the pointer
                       // expression being freed. data.num_value = the
                       // pointer type's element size (same meaning as
                       // NODE_HEAP_ALLOC's), passed straight through as
                       // OP_DISPOSE's arg - the freelist is bucketed by
                       // this size, so a later new() of the SAME element
                       // size can reuse this exact block.

    NODE_PROC_REF, // A top-level (non-nested) procedure/function passed
                       // BY NAME as the actual argument for a procedural/
                       // functional parameter (e.g. 'Apply(Square, x)'),
                       // OR assigned to a NAMED procedural-type variable
                       // (e.g. 'p := Square;' where p: TProc - see
                       // parse_proc_value()) - the same node either way.
                       // data.var_idx = proc_table[] index - always a
                       // plain compile-time constant, never packed with
                       // anything, since the target is guaranteed top-
                       // level (see param_is_proc in common.h's
                       // ProcSymbol). No children. codegen.c emits a
                       // backpatched PUSH of that procedure's
                       // entry_address (mirroring record_call()'s own
                       // pending_calls[] backpatch, just patching a PUSH
                       // instead of a CALL).
    NODE_CALL_INDIRECT, // A call THROUGH an already-received procedural/
                       // functional parameter (e.g. 'f(x)' where f is
                       // itself such a parameter). op = levels_up
                       // (mirrors NODE_LOCAL_VAR's own convention - the
                       // statement/expression-context flag NODE_CALL
                       // keeps in op lives elsewhere here, see extra
                       // below, precisely because op is needed for
                       // levels_up instead). data.var_idx = f's own
                       // local frame slot (holding a runtime procedure
                       // entry address, pushed by NODE_PROC_REF or
                       // forwarded from an enclosing procedural
                       // parameter - see parse_proc_argument()). left =
                       // the argument list, chained via ->next like
                       // NODE_CALL's. expression_type = the callee's
                       // return type if it's a function, or TYPE_UNKNOWN
                       // if it's a procedure - mirrors the fact that an
                       // ordinary statement-context NODE_CALL for a
                       // plain procedure never sets expression_type
                       // either, so TYPE_UNKNOWN is already the de facto
                       // "no value" convention here, needing no new
                       // field of its own. extra = a NODE_NUMBER literal
                       // whose data.num_value is 1 if this call is used
                       // as a statement (pop an unused function result,
                       // then continue via ->next, exactly like
                       // NODE_CALL's own op == TOKEN_PROCEDURE case) or 0
                       // if used as an expression.
    NODE_PROCVAR_CALL, // A call through a NAMED procedural-type value
                       // (e.g. 'p(x)' where p: TProc - a plain global/
                       // local/'var'-parameter variable, NOT the older,
                       // narrower is_proc_param parameter mechanism
                       // NODE_CALL_INDIRECT above serves). A SEPARATE node
                       // type from NODE_CALL_INDIRECT, deliberately: that
                       // one's shape is hardcoded to "the callee's address
                       // always lives in a LOCAL frame slot" (data.var_idx
                       // + op == levels_up), which a plain GLOBAL
                       // procedural-type variable doesn't fit. Here,
                       // 'right' is instead an arbitrary already-built
                       // expression (NODE_VARIABLE/NODE_LOCAL_VAR/
                       // NODE_VAR_PARAM_READ - whichever read already
                       // applies to p) that evaluates to the callee's
                       // entry address - reusing existing read machinery
                       // rather than inventing a THIRD address-encoding
                       // scheme. left = the argument list, chained via
                       // ->next like NODE_CALL's/NODE_CALL_INDIRECT's own.
                       // expression_type/extra follow NODE_CALL_INDIRECT's
                       // exact same convention (callee's return type or
                       // TYPE_UNKNOWN; extra = a NODE_NUMBER statement-
                       // context flag).
    NODE_VIRTUAL_CALL, // A class method call ('c.Method(...)' or
                       // 'c^.Method(...)'), dispatched dynamically through
                       // c's OWN runtime type tag rather than its static
                       // type - see OP_LOAD_VTABLE_SLOT. Built by
                       // resolve_heap_deref_step() exactly where a
                       // NODE_CALL used to be built for this same syntax,
                       // back when dispatch was early/static-only. left =
                       // 'self' (the already-resolved instance expression)
                       // - kept SEPARATE from the argument list (unlike
                       // NODE_CALL, which splices self in as argument 0),
                       // because codegen needs to read self's tag WITHOUT
                       // disturbing where self itself ends up on the
                       // stack (see codegen.c's own comment on the
                       // OP_DUP/OP_SWAP shuffle this requires). right =
                       // the argument list, chained via ->next, NOT
                       // including self. data.num_value = the method's
                       // SLOT number (index within the static type's own
                       // PointerTypeDef.methods[] - stable across the
                       // whole hierarchy, see OP_LOAD_VTABLE_SLOT's own
                       // comment). op = TOKEN_PROCEDURE if used as a
                       // statement (discard an unused function result,
                       // then continue via ->next) - exactly mirrors
                       // NODE_CALL's own op convention; unlike
                       // NODE_CALL_INDIRECT, no local frame slot is
                       // involved here, so op didn't need to be freed up
                       // for anything else. expression_type = the
                       // resolved method's return type if it's a
                       // function, or TYPE_UNKNOWN if a procedure - same
                       // "no value" convention as NODE_CALL_INDIRECT's.
    NODE_VTABLE_INIT_ENTRY, // One entry of the vtable-init chain
                       // (build_vtable_init_chain() in parser.c) that
                       // runs once, before any user code, populating
                       // every class's vtable - see OP_STORE_VTABLE_SLOT.
                       // left = a NODE_PROC_REF for the method's ACTUAL
                       // implementing procedure (already resolved via
                       // find_proc() at parse time, exactly like an
                       // ordinary method call site resolves it).
                       // data.num_value = the fully precomputed flat
                       // vm_vtables[] index (class_id * MAX_CLASS_METHODS
                       // + slot). Chained via ->next, one entry per
                       // method of every class, spliced onto the front of
                       // the main program body's own statement chain so
                       // it's guaranteed to run first.
    NODE_HEAP_ARRAY_FIELD_ACCESS, // A class array field ELEMENT read
                       // ('c.data[i]' or self-shorthand 'data[i]') - the
                       // array-field counterpart of NODE_HEAP_FIELD_
                       // ACCESS. left = the base pointer expression
                       // (as NODE_HEAP_FIELD_ACCESS's own left). right =
                       // the index expression, already wrapped in a
                       // NODE_RANGE_CHECK against the field's declared
                       // array bounds (see wrap_range_check() at the
                       // call site in resolve_heap_deref_step()).
                       // data.num_value = the field's own compile-time
                       // base offset within the instance MINUS the
                       // array's declared lower bound - folding the
                       // zero-basing into the same immediate
                       // OP_LOAD_HEAP_ARRAY_FIELD already needs for the
                       // field offset, so the runtime index needs no
                       // separate adjustment: base + data.num_value +
                       // raw_index == base + field_offset + (raw_index -
                       // lower). expression_type = the array's declared
                       // element type. Always a TERMINAL access step -
                       // unlike a scalar/nested-record field, this can't
                       // be followed by a further '.field'/'^' yet (a
                       // known gap, like a method call's own result).
    NODE_HEAP_ARRAY_FIELD_ASSIGN, // Write counterpart of NODE_HEAP_ARRAY_
                       // FIELD_ACCESS above - 'c.data[i] := val' or
                       // self-shorthand 'data[i] := val'. left = the
                       // base pointer expression. right = the value
                       // expression (already wrapped in a NODE_RANGE_
                       // CHECK if the array's ELEMENT type is itself a
                       // subrange - a separate check from the index's
                       // own bounds check, same as an ordinary scalar
                       // field's value can be). extra = the index
                       // expression (range-checked against the array's
                       // bounds, same as the access node's own right).
                       // data.num_value = the same combined offset the
                       // access node uses. expression_type = the array's
                       // declared element type - used by codegen to
                       // choose OP_STORE_HEAP_ARRAY_FIELD_CHAR over the
                       // plain variant, same reasoning as NODE_HEAP_
                       // FIELD_ASSIGN's own char-field dispatch.
    NODE_INHERITED_CALL, // 'inherited MethodName(args);'/'inherited;' -
                       // a DIRECT (non-virtual) call to the enclosing
                       // method's class's PARENT's implementation of a
                       // method, resolved fully at compile time (unlike
                       // NODE_VIRTUAL_CALL's runtime vtable dispatch) -
                       // see parse_inherited_call() in parser.c. left =
                       // self (built via build_self_reference_node(),
                       // same as NODE_VIRTUAL_CALL's own left). right =
                       // the argument list, chained via each arg's own
                       // ->next - either parsed explicitly (named-target
                       // form) or synthesized by reading the enclosing
                       // method's own parameters in order (bare
                       // 'inherited;' form - always well-typed, since an
                       // override is required to match its inherited
                       // signature exactly). data.var_idx = the resolved
                       // TARGET's proc_table[] index directly (not a
                       // vtable slot - this is the one real difference
                       // from NODE_VIRTUAL_CALL's own data.num_value).
                       // op = TOKEN_PROCEDURE in statement context (same
                       // "discard an unused function result, continue via
                       // next" convention as NODE_CALL/NODE_VIRTUAL_CALL),
                       // left unset in expression context.
                       // expression_type = the target's return type if a
                       // function, else TYPE_UNKNOWN.
    NODE_TRY,          // 'try <body> except <handler> end' - left = the
                       // try-body statement list, right = the except-body
                       // statement list (both via statement_list(), the
                       // same mechanism 'repeat's body already uses - not
                       // a separate list structure). No exception-class
                       // matching - a single blanket handler catches any
                       // 'raise' from anywhere in the try-body, including
                       // several call frames deep - see OP_TRY/OP_RAISE.
    NODE_RAISE,        // 'raise <message-expr>;' - left = the message
                       // expression (must be string/char-typed, see
                       // type_checker.c). Unwinds to the innermost active
                       // OP_TRY handler if one exists, else a fatal VM
                       // Runtime Error - see OP_RAISE.
    NODE_IS_TEST,      // 'obj is TFoo' - left = the object expression
                       // (class- or TYPE_NIL-typed). data.num_value =
                       // target class's pointer_types[] index (same
                       // meaning as the runtime tag OP_NEW writes for
                       // that class). expression_type = TYPE_BOOLEAN.
                       // right/extra/op unused. See OP_IS_INSTANCE.
    NODE_AS_CAST,      // 'obj as TFoo' - left = the object expression.
                       // right = a synthesized NODE_STRING failure
                       // message ("Cannot cast to 'TFoo'"), built the
                       // same way NODE_ASSERT's default "Assertion
                       // failed" message is synthesized when none is
                       // given - so codegen never needs a separate
                       // no-message case. data.num_value = target
                       // class's pointer_types[] index. expression_type
                       // = TYPE_POINTER_BASE + that index (the cast
                       // result's new static type). extra/op unused.
                       // See OP_IS_INSTANCE/OP_RAISE.
    NODE_CLASS_PARENT_INIT_ENTRY // One entry of the startup class-
                       // parent-init chain (build_class_parent_init_
                       // chain() in parser.c) - populates vm_class_
                       // parent[], mirroring NODE_VTABLE_INIT_ENTRY's own
                       // vm_vtables[] population. left = a NODE_NUMBER
                       // holding this class's parent's own class_id (or
                       // -1 for none - no backpatching needed, unlike
                       // NODE_VTABLE_INIT_ENTRY's NODE_PROC_REF, since a
                       // parent's class_id is already a fully known
                       // compile-time integer). data.num_value = THIS
                       // class's own class_id (OP_STORE_CLASS_PARENT's
                       // arg). Chained via ->next, one entry per class
                       // (not per method, unlike NODE_VTABLE_INIT_ENTRY).
} NodeType;

typedef struct ASTNode {
    NodeType type;
    TokenType op;
    DataType expression_type;
    int line;
    union {
        int num_value;
        int var_idx;
    } data;
    struct ASTNode *left;
    struct ASTNode *right;
    struct ASTNode *next;
    struct ASTNode *extra;  // Node-specific 4th child. Currently only used
                             // by NODE_IF, for the optional else-branch.
} ASTNode;

// A procedure's own namespace, separate from Symbol (variables) - needed
// so 'foo;' (a call) and 'foo := 5;' (an assignment) can be told apart by
// which table the name resolves in. entry_address is filled in during
// codegen (the instruction index where this procedure's body starts);
// -1 until then. body is the parsed AST for the procedure's compound
// statement, set once parsing finishes it (NULL while is_forward is set).
typedef struct {
    char name[MAX_NAME];
    char unmangled_name[MAX_NAME]; // empty for an ordinary procedure/
                                    // function - only set for a class
                                    // method's body (name is the
                                    // mangled 'ClassName__Method', to
                                    // avoid colliding with another
                                    // class's own same-named method in
                                    // this compiler's one flat
                                    // proc_table[] namespace), holding
                                    // the short name the user actually
                                    // wrote inside the method body -
                                    // e.g. 'Area := ...;' to set a
                                    // function method's return value -
                                    // see parse_class_method_body().
    int entry_address;
    int param_count;                // SYNTACTIC parameter count (how many
                                    // comma-separated parameters were
                                    // declared) - used for call-site
                                    // argument-count validation. NOT
                                    // necessarily the number of frame
                                    // slots the parameters occupy - see
                                    // param_slot_count.
    int param_slot_count;           // total FRAME SLOTS the parameters
                                    // occupy (0..param_slot_count-1).
                                    // Equal to param_count UNLESS one or
                                    // more parameters is a record, which
                                    // expands to multiple slots (one per
                                    // field - see add_local_record() and
                                    // param_is_record below).
    int local_count;               // total slots ENTER reserves - params
                                    // occupy 0..param_slot_count-1,
                                    // additional locals continue from
                                    // there, and (for a function)
                                    // return_slot is the last one,
                                    // reserved automatically
    DataType param_types[MAX_PARAMS];
    char param_names[MAX_PARAMS][MAX_NAME]; // needed so a forward
                                    // declaration's later completing
                                    // definition - which doesn't re-list
                                    // parameters - can still resolve them
                                    // by name inside its body
    int param_is_array_ref[MAX_PARAMS]; // 1 if this parameter is an array
                                    // (always by reference - see parser.c)
    int param_array_lower[MAX_PARAMS];  // only meaningful if the above is
    int param_array_upper[MAX_PARAMS];  // set: the bounds any argument
                                    // passed here must exactly match
                                    // (dimension 1, if param_is_2d below)
    int param_is_2d[MAX_PARAMS];        // 1 if this array-ref parameter is
                                    // 2D - only meaningful if
                                    // param_is_array_ref is set
    int param_array_lower2[MAX_PARAMS]; // only meaningful if param_is_2d:
    int param_array_upper2[MAX_PARAMS]; // the second dimension's bounds
    int param_is_nd[MAX_PARAMS];        // 1 if this array-ref parameter has
                                    // 3 OR MORE dimensions - mutually
                                    // exclusive with param_is_2d, only
                                    // meaningful if param_is_array_ref is
                                    // set. See Symbol.is_nd in common.h
                                    // for why every dimension's bounds
                                    // live uniformly in param_nd_lower/
                                    // param_nd_upper below rather than
                                    // reusing param_array_lower/upper for
                                    // dimension 1 the way param_is_2d does.
    int param_nd_dims[MAX_PARAMS];      // only meaningful if param_is_nd
    int param_nd_lower[MAX_PARAMS][MAX_ARRAY_DIMS]; // only meaningful if
    int param_nd_upper[MAX_PARAMS][MAX_ARRAY_DIMS]; // param_is_nd, indices 0..param_nd_dims[p]-1
    int param_is_subrange[MAX_PARAMS];  // 1 if this parameter (or, if
                                    // param_is_array_ref is set, each of
                                    // its elements) is subrange-
                                    // constrained - checked at every call
                                    // site (see parse_call_arguments()).
    int param_subrange_lower[MAX_PARAMS]; // only meaningful if the above is set
    int param_subrange_upper[MAX_PARAMS];
    int param_is_record[MAX_PARAMS];    // 1 if this parameter is a record
                                    // type - always by value, flattened
                                    // into N field-value pushes at every
                                    // call site (see
                                    // parse_record_argument() in
                                    // parser.c), each landing in its own
                                    // per-call-isolated frame slot (see
                                    // add_local_record() - NOT the
                                    // "hidden global" trick local arrays
                                    // use).
    int param_record_type_idx[MAX_PARAMS]; // only meaningful if the
                                    // above is set - which parser.c-only
                                    // record_types[] entry this
                                    // parameter is
    int param_record_field_count[MAX_PARAMS]; // only meaningful if
                                    // param_is_record is set - how many
                                    // flattened argument nodes this ONE
                                    // syntactic parameter consumes at a
                                    // call site. This is the record
                                    // type's RECURSIVELY FLATTENED leaf
                                    // count (record_type_leaf_count() in
                                    // parser.c), not its top-level
                                    // declared field count, whenever a
                                    // nested-record field is present.
                                    // type_checker.c uses this to skip
                                    // over them (already correctly typed
                                    // by construction) without needing
                                    // any visibility into parser.c's
                                    // record-type tables.
    int param_is_var[MAX_PARAMS];  // 1 if this parameter is declared
                                    // 'var name: type' - general by-
                                    // reference passing for a SCALAR
                                    // (integer/real/boolean/char/string/
                                    // enum/subrange), not just arrays
                                    // (which are already always by
                                    // reference, with or without 'var' -
                                    // see parse_name_group() in parser.c).
                                    // The argument at each call site must
                                    // itself be a variable (see
                                    // parse_var_argument()), never a
                                    // general expression, and must
                                    // exactly match this parameter's
                                    // declared type (no int->real
                                    // widening, unlike a by-value
                                    // argument - real Pascal never widens
                                    // a 'var' argument either). Whole
                                    // records and array elements aren't
                                    // supported as 'var' arguments yet.
                                    // Mutually exclusive with
                                    // param_is_array_ref/param_is_record -
                                    // an array parameter is already
                                    // always by reference regardless of
                                    // 'var' (which is accepted but has no
                                    // further effect there), and a record
                                    // 'var' parameter is rejected as
                                    // unsupported at parse time.
                                    //
                                    // Implementation: the callee gets an
                                    // ordinary frame slot (via add_local(),
                                    // exactly like a ANY other parameter)
                                    // but its value is an ENCODED
                                    // REFERENCE rather than the value
                                    // itself - see OP_PUSH_LOCAL_REF/
                                    // OP_LOAD_REF/OP_STORE_REF in this
                                    // file and NODE_VAR_REF/
                                    // NODE_LOCAL_VAR_REF/
                                    // NODE_VAR_PARAM_READ/
                                    // NODE_VAR_PARAM_ASSIGN above for the
                                    // full mechanism. A reference is a
                                    // single int: >= 0 means "sym_table[]
                                    // index of a global", < 0 means
                                    // "-(index + 1), an absolute
                                    // vm_frame_stack[] index of one of
                                    // the CALLER's own local/parameter
                                    // slots" - letting one calling
                                    // convention (one value pushed per
                                    // 'var' argument, exactly like every
                                    // other parameter kind) reach either
                                    // of this VM's two separate storage
                                    // regions (vm_vars[] for globals,
                                    // vm_frame_stack[] for locals),
                                    // without needing a second,
                                    // synchronized stack value or
                                    // widening the calling convention
                                    // itself.
    int param_is_proc[MAX_PARAMS]; // 1 if this parameter is itself a
                                    // procedure/function header, written
                                    // inline ('function f(n: integer):
                                    // integer' as one formal parameter) -
                                    // standard ISO 7185 Pascal's
                                    // functional/procedural parameters.
                                    // Mutually exclusive with every other
                                    // param_is_*[] above. The actual
                                    // argument at a call site must name a
                                    // TOP-LEVEL (non-nested) procedure/
                                    // function - passing a nested one is
                                    // a compile-time error, since a
                                    // "procedure value" here is just a
                                    // single runtime int (its entry code
                                    // address, see OP_CALL_INDIRECT) with
                                    // no accompanying static link, unlike
                                    // a real closure (Phase 2). See
                                    // parse_proc_argument() in parser.c.
    int param_proc_is_function[MAX_PARAMS]; // only meaningful if the
                                    // above is set: 1 if this inline
                                    // header is 'function', 0 if
                                    // 'procedure'.
    DataType param_proc_return_type[MAX_PARAMS]; // only meaningful if
                                    // param_proc_is_function is set.
    int param_proc_param_count[MAX_PARAMS]; // only meaningful if
                                    // param_is_proc is set: how many
                                    // parameters THIS inline header
                                    // itself declares (its own,
                                    // completely separate, parameter
                                    // list - reuses MAX_PARAMS as its cap
                                    // too). Scalar-only for now (by-value
                                    // and 'var') - no array/record
                                    // parameters inside an inline
                                    // procedural header yet.
    DataType param_proc_param_types[MAX_PARAMS][MAX_PARAMS];
    int param_proc_param_is_var[MAX_PARAMS][MAX_PARAMS];
    int is_forward;                // 1 while forward-declared but not yet
                                    // completed; 0 once a real body exists
                                    // (or if it was never forward at all)
    int is_function;                // 1 if declared with 'function' (has
                                    // a return value), 0 for 'procedure'
    DataType return_type;           // only meaningful if is_function
    int return_is_subrange;         // only meaningful if is_function -
                                    // checked when the function's own
                                    // name is assigned to (its return
                                    // value), inside its own body.
    int return_subrange_lower;      // only meaningful if return_is_subrange
    int return_subrange_upper;
    int return_slot;                // only meaningful if is_function - a
                                    // hidden local slot, reserved after
                                    // every real parameter/local; assigning
                                    // to the function's own name targets
                                    // this slot (see parser.c), and its
                                    // value is pushed just before RET
    int lexical_parent_idx;         // proc_table[] index of the procedure
                                    // this one is declared directly inside
                                    // (its 'var'/'const'/nested-declaration
                                    // section), or -1 if this is a
                                    // top-level (non-nested) procedure.
                                    // Set once, at add_proc() time, from
                                    // the parser's current nesting state -
                                    // never changes afterward. Used by
                                    // codegen.c to classify every call site
                                    // (not-nested / direct child / deeper
                                    // ancestor descendant / flat-namespace
                                    // "out of lexical scope") and emit the
                                    // right OP_PUSH_STATIC_LINK, if any.
    int lexical_depth;              // 0 for a top-level procedure, 1 for
                                    // one declared directly inside a
                                    // top-level procedure, etc. Redundant
                                    // with walking lexical_parent_idx
                                    // repeatedly, but makes that
                                    // classification an O(depth) walk
                                    // instead of a chain of table lookups.
    struct ASTNode *body;
    char source_file[MAX_UNIT_PATH]; // the file this proc/function/method
                                    // was declared in - the main program's
                                    // own path, or a used unit's resolved
                                    // path. Set once, at add_proc() time.
                                    // Post-parse passes (type_checker.c,
                                    // optimizer.c, codegen.c) switch
                                    // current_filename to this while
                                    // processing this proc's own body, so
                                    // an error inside unit-declared code
                                    // reports the right file, not whichever
                                    // file parsing finished on last.
    char declaring_unit[MAX_NAME]; // same idea as Symbol's own field of
                                    // this name (a DIFFERENT purpose than
                                    // source_file above - a bare unit
                                    // NAME for visibility comparison, not
                                    // a full path for error attribution) -
                                    // empty if declared in the main
                                    // program or a unit's 'interface',
                                    // else the declaring unit's name.
    int is_unit_private;           // 1 if declared in a unit's
                                    // 'implementation' section - see
                                    // Symbol.is_unit_private and
                                    // symbol_visible_here() in parser.c.
                                    // Set (harmlessly, never consulted)
                                    // for a class method's own mangled
                                    // proc too, since add_proc() is
                                    // shared - method visibility isn't
                                    // enforced by this mechanism at all,
                                    // methods are resolved through
                                    // pointer_types[].methods[], never
                                    // through find_proc_visible().
} ProcSymbol;

// Shared Global State
extern Instruction code[MAX_CODE];
extern int code_idx;
extern Symbol sym_table[MAX_SYMBOLS];
extern int sym_count;
extern char string_pool[MAX_STRINGS][MAX_STRING_LEN];
extern int string_count;
extern int array_mem_count;
extern ProcSymbol proc_table[MAX_PROCEDURES];
extern int proc_count;
extern EnumTypeDef enum_types[MAX_ENUM_TYPES];
extern int enum_type_count;
extern Token token;

#endif

