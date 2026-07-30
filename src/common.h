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
    TOKEN_SQRT, TOKEN_SIN, TOKEN_COS, TOKEN_ARCTAN, TOKEN_EXP, TOKEN_LN,
    TOKEN_PI, TOKEN_POWER,
    TOKEN_POW, // '**'
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
    TYPE_REAL   // A 32-bit IEEE-754 float, reinterpreting the bits of the
                // same int-sized storage slot every other type uses (see
                // vm.c's bits_to_float/float_to_bits) - not a double,
                // specifically so a real value still fits in exactly one
                // slot, matching every other type's storage model.
} DataType;

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
    OP_STR_CHAR_REPLACE // Pop a new character (must be exactly one
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
    NODE_LOCAL_STRING_INDEX_ASSIGN  // Same as above, but s is a
                       // parameter/local string/char variable.
                       // data.var_idx = s's frame slot.
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
    int param_count;
    int local_count;               // total slots ENTER reserves - params
                                    // occupy 0..param_count-1, additional
                                    // locals continue from there, and (for
                                    // a function) return_slot is the last
                                    // one, reserved automatically
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
    int is_forward;                // 1 while forward-declared but not yet
                                    // completed; 0 once a real body exists
                                    // (or if it was never forward at all)
    int is_function;                // 1 if declared with 'function' (has
                                    // a return value), 0 for 'procedure'
    DataType return_type;           // only meaningful if is_function
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
extern Token token;

#endif

