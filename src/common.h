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
#define MAX_PROCEDURES 50
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
    TOKEN_TYPE, TOKEN_RECORD,
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
    TYPE_ENUM_BASE // Not a real type by itself - a specific enumerated
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
                // type_checker.c/codegen.c) ever looks at values >=
                // TYPE_ENUM_BASE. See EnumTypeDef below.
} DataType;

#define MAX_ENUM_TYPES 20
#define MAX_ENUM_VALUES 32

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
    NODE_READLN,
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
                       // arity, unlike a general call.
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
    NODE_GOTO          // 'goto <N>;' - data.num_value = the target
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
                                    // call site. type_checker.c uses
                                    // this to skip over them (already
                                    // correctly typed by construction)
                                    // without needing any visibility
                                    // into parser.c's record-type
                                    // tables.
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
    struct ASTNode *body;
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

