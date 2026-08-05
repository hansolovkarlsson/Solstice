// solas - the SolVM assembler.
//
// Assembles a small, readable text format directly into the same .bin
// bytecode format written by pascalc and executed by solvm (via the
// shared bytecode.c reader/writer - this tool never touches the Pascal
// compiler frontend at all).
//
// Syntax:
//
//     ; comments start with a semicolon and run to end of line
//     .var <name> <integer|boolean|string>       ; declare a variable
//     .array <name> <lower> <upper> <type>       ; declare an array
//     label:                             ; a label, alone on its own line
//     MNEMONIC [operand]                 ; one instruction per line
//
// Operand kinds, by instruction:
//     PUSH <integer literal>             e.g. PUSH 5, PUSH -1
//     LOAD/STORE/READ <variable name>    must have a matching .var
//     LOAD_IDX/STORE_IDX <array name>    must have a matching .array; the
//                                        runtime index comes off the stack
//     JMP/JZ/CALL <label name>           may be a forward reference
//     PUSH_STR "<text>"                  interned into the string pool
//     ENTER <n>                          reserve n local slots (the first
//                                        instruction of a procedure body)
//     LOAD_LOCAL/STORE_LOCAL <k>         slot k, relative to the current
//                                        frame - not a name, just a number
//     everything else                    no operand
//
// Procedures: CALL pushes a return address and jumps; the callee's first
// instruction is normally ENTER <n>, which reserves n zero-initialized
// local slots. Parameters and return values both travel via the ordinary
// operand stack - the caller pushes arguments before CALL (the callee
// then STORE_LOCALs them out of slots 0..k-1), and a "function" leaves
// its result on the operand stack before RET. RET itself deallocates the
// whole frame and restores the caller's frame pointer - no separate
// "leave" instruction needed. See docs/BYTECODE.md for the full picture.
//
// .var types: integer, boolean, string
// .array bounds are integer literals (may be negative), e.g.:
//     .array nums 1 10 integer
//     .array flags -3 3 boolean
//
// Note: PRINT and PRINT_STR do NOT print a trailing newline - use NEWLINE
// explicitly (this is what write/writeln in the Pascal compiler compile
// down to: one PRINT/PRINT_STR per argument, then one NEWLINE only for
// writeln).
//
// Example (countdown loop):
//
//     .var i integer
//         push 5
//         store i
//     loop:
//         load i
//         push 0
//         gt
//         jz done
//         load i
//         print
//         load i
//         push 1
//         sub
//         store i
//         jmp loop
//     done:
//         halt
//
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "common.h"
#include "bytecode.h"
#include "error.h"

#define MAX_LABELS 512
#define MAX_LINES  4096

typedef struct {
    char name[MAX_NAME];
    int index; // instruction index the label points to
} Label;

typedef enum { OPERAND_NONE, OPERAND_IMMEDIATE, OPERAND_VAR, OPERAND_LABEL, OPERAND_STRING } OperandKind;

typedef struct {
    const char *mnemonic;
    Opcode op;
    OperandKind kind;
} OpcodeInfo;

static const OpcodeInfo OPCODE_TABLE[] = {
    {"PUSH",      OP_PUSH,      OPERAND_IMMEDIATE},
    {"LOAD",      OP_LOAD,      OPERAND_VAR},
    {"STORE",     OP_STORE,     OPERAND_VAR},
    {"READ",      OP_READ,      OPERAND_VAR},
    {"READ_LOCAL_INT",  OP_READ_LOCAL_INT,  OPERAND_IMMEDIATE},
    {"READ_LOCAL_BOOL", OP_READ_LOCAL_BOOL, OPERAND_IMMEDIATE},
    {"READ_LOCAL_REAL", OP_READ_LOCAL_REAL, OPERAND_IMMEDIATE},
    {"READ_LOCAL_STR",  OP_READ_LOCAL_STR,  OPERAND_IMMEDIATE},
    {"READ_LOCAL_CHAR", OP_READ_LOCAL_CHAR, OPERAND_IMMEDIATE},
    {"READ_NOFLUSH",      OP_READ_NOFLUSH,      OPERAND_VAR},
    {"READ_LOCAL_INT_NOFLUSH",  OP_READ_LOCAL_INT_NOFLUSH,  OPERAND_IMMEDIATE},
    {"READ_LOCAL_BOOL_NOFLUSH", OP_READ_LOCAL_BOOL_NOFLUSH, OPERAND_IMMEDIATE},
    {"READ_LOCAL_REAL_NOFLUSH", OP_READ_LOCAL_REAL_NOFLUSH, OPERAND_IMMEDIATE},
    {"EOF",  OP_EOF,  OPERAND_NONE},
    {"EOLN", OP_EOLN, OPERAND_NONE},
    {"ADD",       OP_ADD,       OPERAND_NONE},
    {"SUB",       OP_SUB,       OPERAND_NONE},
    {"MUL",       OP_MUL,       OPERAND_NONE},
    {"DIV",       OP_DIV,       OPERAND_NONE},
    {"EQ",        OP_EQ,        OPERAND_NONE},
    {"LT",        OP_LT,        OPERAND_NONE},
    {"GT",        OP_GT,        OPERAND_NONE},
    {"AND",       OP_AND,       OPERAND_NONE},
    {"OR",        OP_OR,        OPERAND_NONE},
    {"NOT",       OP_NOT,       OPERAND_NONE},
    {"LTE",       OP_LTE,       OPERAND_NONE},
    {"GTE",       OP_GTE,       OPERAND_NONE},
    {"NEQ",       OP_NEQ,       OPERAND_NONE},
    {"NEG",       OP_NEG,       OPERAND_NONE},
    {"MOD",       OP_MOD,       OPERAND_NONE},
    {"XOR",       OP_XOR,       OPERAND_NONE},
    {"BAND",      OP_BAND,      OPERAND_NONE},
    {"BOR",       OP_BOR,       OPERAND_NONE},
    {"BXOR",      OP_BXOR,      OPERAND_NONE},
    {"BNOT",      OP_BNOT,      OPERAND_NONE},
    {"SHL",       OP_SHL,       OPERAND_NONE},
    {"SHR",       OP_SHR,       OPERAND_NONE},
    {"DUP",       OP_DUP,       OPERAND_NONE},
    {"ABS",       OP_ABS,       OPERAND_NONE},
    {"FABS",      OP_FABS,      OPERAND_NONE},
    {"FSQRT",     OP_FSQRT,     OPERAND_NONE},
    {"FSIN",      OP_FSIN,      OPERAND_NONE},
    {"FCOS",      OP_FCOS,      OPERAND_NONE},
    {"FARCTAN",   OP_FARCTAN,   OPERAND_NONE},
    {"FEXP",      OP_FEXP,      OPERAND_NONE},
    {"FLN",       OP_FLN,       OPERAND_NONE},
    {"FPOWER",    OP_FPOWER,    OPERAND_NONE},
    {"STR_CHAR_REPLACE", OP_STR_CHAR_REPLACE, OPERAND_NONE},
    {"ORD",       OP_ORD,       OPERAND_NONE},
    {"CHR",       OP_CHR,       OPERAND_NONE},
    {"LENGTH",       OP_LENGTH,       OPERAND_NONE},
    {"STR_CHAR_AT",  OP_STR_CHAR_AT,  OPERAND_NONE},
    {"COPY",         OP_COPY,         OPERAND_NONE},
    {"POS",          OP_POS,          OPERAND_NONE},
    {"UPCASE_CHAR",  OP_UPCASE_CHAR,  OPERAND_NONE},
    {"UPPERCASE_STR", OP_UPPERCASE_STR, OPERAND_NONE},
    {"LOWERCASE_STR", OP_LOWERCASE_STR, OPERAND_NONE},
    {"LEFT",         OP_LEFT,         OPERAND_NONE},
    {"RIGHT",        OP_RIGHT,        OPERAND_NONE},
    {"PRINT",     OP_PRINT,     OPERAND_NONE},
    {"PRINT_BOOL", OP_PRINT_BOOL, OPERAND_NONE},
    {"HALT",      OP_HALT,      OPERAND_NONE},
    {"JMP",       OP_JMP,       OPERAND_LABEL},
    {"JZ",        OP_JZ,        OPERAND_LABEL},
    {"CALL",      OP_CALL,      OPERAND_LABEL},
    {"RET",       OP_RET,       OPERAND_NONE},
    {"ENTER",       OP_ENTER,       OPERAND_IMMEDIATE},
    {"LOAD_LOCAL",  OP_LOAD_LOCAL,  OPERAND_IMMEDIATE},
    {"STORE_LOCAL", OP_STORE_LOCAL, OPERAND_IMMEDIATE},
    {"POP",         OP_POP,         OPERAND_NONE},
    {"PUSH_STR",  OP_PUSH_STR,  OPERAND_STRING},
    {"PRINT_STR", OP_PRINT_STR, OPERAND_NONE},
    {"SEQ",       OP_SEQ,       OPERAND_NONE},
    {"SCMP",      OP_SCMP,      OPERAND_NONE},
    {"SCONCAT",   OP_SCONCAT,   OPERAND_NONE},
    {"NEWLINE",   OP_NEWLINE,   OPERAND_NONE},
    {"LOAD_IDX",  OP_LOAD_IDX,  OPERAND_VAR},
    {"STORE_IDX", OP_STORE_IDX, OPERAND_VAR},
    {"LOAD_IDX2D",  OP_LOAD_IDX2D,  OPERAND_VAR},
    {"STORE_IDX2D", OP_STORE_IDX2D, OPERAND_VAR},
    {"FADD",  OP_FADD,  OPERAND_NONE},
    {"FSUB",  OP_FSUB,  OPERAND_NONE},
    {"FMUL",  OP_FMUL,  OPERAND_NONE},
    {"FDIV",  OP_FDIV,  OPERAND_NONE},
    {"FEQ",   OP_FEQ,   OPERAND_NONE},
    {"FLT",   OP_FLT,   OPERAND_NONE},
    {"FGT",   OP_FGT,   OPERAND_NONE},
    {"FLTE",  OP_FLTE,  OPERAND_NONE},
    {"FGTE",  OP_FGTE,  OPERAND_NONE},
    {"FNEQ",  OP_FNEQ,  OPERAND_NONE},
    {"FNEG",  OP_FNEG,  OPERAND_NONE},
    {"FPRINT", OP_FPRINT, OPERAND_NONE},
    {"INT_TO_REAL", OP_INT_TO_REAL, OPERAND_NONE},
    {"TRUNC", OP_TRUNC, OPERAND_NONE},
    {"ROUND", OP_ROUND, OPERAND_NONE},
    {"PRINT_PADDED",      OP_PRINT_PADDED,      OPERAND_NONE},
    {"PRINT_STR_PADDED",  OP_PRINT_STR_PADDED,  OPERAND_NONE},
    {"PRINT_BOOL_PADDED", OP_PRINT_BOOL_PADDED, OPERAND_NONE},
    {"FPRINT_PADDED",         OP_FPRINT_PADDED,         OPERAND_NONE},
    {"FPRINT_PADDED_PRECISE", OP_FPRINT_PADDED_PRECISE, OPERAND_NONE},
    {"LOAD_IDX_DYN",  OP_LOAD_IDX_DYN,  OPERAND_NONE},
    {"STORE_IDX_DYN", OP_STORE_IDX_DYN, OPERAND_NONE},
    {"CHECK_LOWER", OP_CHECK_LOWER, OPERAND_IMMEDIATE},
    {"CHECK_UPPER", OP_CHECK_UPPER, OPERAND_IMMEDIATE},
    {"ASSERT", OP_ASSERT, OPERAND_NONE},
    {"LOAD_IDX2D_DYN",  OP_LOAD_IDX2D_DYN,  OPERAND_NONE},
    {"STORE_IDX2D_DYN", OP_STORE_IDX2D_DYN, OPERAND_NONE},
    {"PUSH_LOCAL_REF", OP_PUSH_LOCAL_REF, OPERAND_IMMEDIATE},
    {"LOAD_REF",  OP_LOAD_REF,  OPERAND_NONE},
    {"STORE_REF", OP_STORE_REF, OPERAND_NONE},
    {"LOAD_IDXND",  OP_LOAD_IDXND,  OPERAND_VAR},
    {"STORE_IDXND", OP_STORE_IDXND, OPERAND_VAR},
    {"LOAD_IDXND_DYN",  OP_LOAD_IDXND_DYN,  OPERAND_IMMEDIATE}, // operand = dimension count, not a symbol reference
    {"STORE_IDXND_DYN", OP_STORE_IDXND_DYN, OPERAND_IMMEDIATE},
    {"LOAD_ARRAY_RECORD_FIELD",       OP_LOAD_ARRAY_RECORD_FIELD,       OPERAND_VAR},
    {"STORE_ARRAY_RECORD_FIELD",      OP_STORE_ARRAY_RECORD_FIELD,      OPERAND_VAR},
    {"STORE_ARRAY_RECORD_FIELD_CHAR", OP_STORE_ARRAY_RECORD_FIELD_CHAR, OPERAND_VAR},
    {"FILE_ASSIGN",  OP_FILE_ASSIGN,  OPERAND_VAR},
    {"FILE_RESET",   OP_FILE_RESET,   OPERAND_VAR},
    {"FILE_REWRITE", OP_FILE_REWRITE, OPERAND_VAR},
    {"FILE_CLOSE",   OP_FILE_CLOSE,   OPERAND_VAR},
    {"PRINT_FILE",      OP_PRINT_FILE,      OPERAND_VAR},
    {"PRINT_STR_FILE",  OP_PRINT_STR_FILE,  OPERAND_VAR},
    {"PRINT_BOOL_FILE", OP_PRINT_BOOL_FILE, OPERAND_VAR},
    {"FPRINT_FILE",     OP_FPRINT_FILE,     OPERAND_VAR},
    {"PRINT_PADDED_FILE",      OP_PRINT_PADDED_FILE,      OPERAND_VAR},
    {"PRINT_STR_PADDED_FILE",  OP_PRINT_STR_PADDED_FILE,  OPERAND_VAR},
    {"PRINT_BOOL_PADDED_FILE", OP_PRINT_BOOL_PADDED_FILE, OPERAND_VAR},
    {"FPRINT_PADDED_FILE",         OP_FPRINT_PADDED_FILE,         OPERAND_VAR},
    {"FPRINT_PADDED_PRECISE_FILE", OP_FPRINT_PADDED_PRECISE_FILE, OPERAND_VAR},
    {"NEWLINE_FILE", OP_NEWLINE_FILE, OPERAND_VAR},
    {"EOF_FILE",  OP_EOF_FILE,  OPERAND_VAR},
    {"EOLN_FILE", OP_EOLN_FILE, OPERAND_VAR},
    {"READ_FILE",         OP_READ_FILE,         OPERAND_VAR}, // operand = READ TARGET's index - the source file's own index is popped from the stack (see common.h)
    {"READ_FILE_NOFLUSH", OP_READ_FILE_NOFLUSH, OPERAND_VAR},
    {"READ_FILE_LOCAL_INT",  OP_READ_FILE_LOCAL_INT,  OPERAND_IMMEDIATE},
    {"READ_FILE_LOCAL_BOOL", OP_READ_FILE_LOCAL_BOOL, OPERAND_IMMEDIATE},
    {"READ_FILE_LOCAL_REAL", OP_READ_FILE_LOCAL_REAL, OPERAND_IMMEDIATE},
    {"READ_FILE_LOCAL_STR",  OP_READ_FILE_LOCAL_STR,  OPERAND_IMMEDIATE},
    {"READ_FILE_LOCAL_CHAR", OP_READ_FILE_LOCAL_CHAR, OPERAND_IMMEDIATE},
    {"READ_FILE_LOCAL_INT_NOFLUSH",  OP_READ_FILE_LOCAL_INT_NOFLUSH,  OPERAND_IMMEDIATE},
    {"READ_FILE_LOCAL_BOOL_NOFLUSH", OP_READ_FILE_LOCAL_BOOL_NOFLUSH, OPERAND_IMMEDIATE},
    {"READ_FILE_LOCAL_REAL_NOFLUSH", OP_READ_FILE_LOCAL_REAL_NOFLUSH, OPERAND_IMMEDIATE},
    {"SKIP_PENDING_NEWLINE",      OP_SKIP_PENDING_NEWLINE,      OPERAND_NONE},
    {"SKIP_PENDING_NEWLINE_FILE", OP_SKIP_PENDING_NEWLINE_FILE, OPERAND_VAR},
};
#define NUM_OPCODES (sizeof(OPCODE_TABLE) / sizeof(OPCODE_TABLE[0]))

static Label labels[MAX_LABELS];
static int label_count = 0;

static char *lines[MAX_LINES];
static int line_numbers[MAX_LINES];
static int num_lines = 0;

static const char *asm_filename = "<source>";

static void asm_error(int line, const char *fmt, ...) {
    fprintf(stderr, "%s:%d: Assembler Error: ", asm_filename, line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    fatal_abort();
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

static const OpcodeInfo *find_opcode(const char *mnemonic) {
    for (size_t i = 0; i < NUM_OPCODES; i++) {
        if (strcasecmp(mnemonic, OPCODE_TABLE[i].mnemonic) == 0) return &OPCODE_TABLE[i];
    }
    return NULL;
}

static int find_var(const char *name) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) return i;
    }
    return -1;
}

static int find_label(const char *name) {
    for (int i = 0; i < label_count; i++) {
        if (strcmp(labels[i].name, name) == 0) return labels[i].index;
    }
    return -1;
}

static void add_var(int line_no, const char *name, DataType type) {
    if (strlen(name) >= MAX_NAME) {
        asm_error(line_no, "Variable name '%s' too long (limit is %d characters)", name, MAX_NAME - 1);
    }
    if (find_var(name) != -1) {
        asm_error(line_no, "Duplicate variable declaration '%s'", name);
    }
    if (sym_count >= MAX_SYMBOLS) {
        asm_error(line_no, "Too many variable declarations (limit is %d)", MAX_SYMBOLS);
    }
    strcpy(sym_table[sym_count].name, name);
    sym_table[sym_count].type = type;
    sym_table[sym_count].is_array = 0;
    sym_table[sym_count].array_lower = 0;
    sym_table[sym_count].array_upper = 0;
    sym_table[sym_count].array_base = 0;
    sym_count++;
}

static void add_array_var(int line_no, const char *name, DataType elem_type, int lower, int upper) {
    if (strlen(name) >= MAX_NAME) {
        asm_error(line_no, "Array name '%s' too long (limit is %d characters)", name, MAX_NAME - 1);
    }
    if (find_var(name) != -1) {
        asm_error(line_no, "Duplicate variable declaration '%s'", name);
    }
    if (sym_count >= MAX_SYMBOLS) {
        asm_error(line_no, "Too many variable declarations (limit is %d)", MAX_SYMBOLS);
    }
    if (upper < lower) {
        asm_error(line_no, "Invalid array bounds: upper (%d) must be >= lower (%d)", upper, lower);
    }
    int size = upper - lower + 1;
    if (array_mem_count + size > MAX_ARRAY_MEM) {
        asm_error(line_no, "Array storage exhausted (limit is %d total elements across all arrays)", MAX_ARRAY_MEM);
    }
    strcpy(sym_table[sym_count].name, name);
    sym_table[sym_count].type = elem_type;
    sym_table[sym_count].is_array = 1;
    sym_table[sym_count].array_lower = lower;
    sym_table[sym_count].array_upper = upper;
    sym_table[sym_count].array_base = array_mem_count;
    sym_table[sym_count].is_2d = 0;
    array_mem_count += size;
    sym_count++;
}

static void add_array_var_2d(int line_no, const char *name, DataType elem_type,
                              int lower, int upper, int lower2, int upper2) {
    if (strlen(name) >= MAX_NAME) {
        asm_error(line_no, "Array name '%s' too long (limit is %d characters)", name, MAX_NAME - 1);
    }
    if (find_var(name) != -1) {
        asm_error(line_no, "Duplicate variable declaration '%s'", name);
    }
    if (sym_count >= MAX_SYMBOLS) {
        asm_error(line_no, "Too many variable declarations (limit is %d)", MAX_SYMBOLS);
    }
    if (upper < lower) {
        asm_error(line_no, "Invalid array bounds: upper (%d) must be >= lower (%d)", upper, lower);
    }
    if (upper2 < lower2) {
        asm_error(line_no, "Invalid array bounds: upper2 (%d) must be >= lower2 (%d)", upper2, lower2);
    }
    int dim1_size = upper - lower + 1;
    int dim2_size = upper2 - lower2 + 1;
    int size = dim1_size * dim2_size;
    if (array_mem_count + size > MAX_ARRAY_MEM) {
        asm_error(line_no, "Array storage exhausted (limit is %d total elements across all arrays)", MAX_ARRAY_MEM);
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

static void add_array_var_nd(int line_no, const char *name, DataType elem_type,
                              int dims, const int *lower, const int *upper) {
    if (strlen(name) >= MAX_NAME) {
        asm_error(line_no, "Array name '%s' too long (limit is %d characters)", name, MAX_NAME - 1);
    }
    if (find_var(name) != -1) {
        asm_error(line_no, "Duplicate variable declaration '%s'", name);
    }
    if (sym_count >= MAX_SYMBOLS) {
        asm_error(line_no, "Too many variable declarations (limit is %d)", MAX_SYMBOLS);
    }
    int size = 1;
    for (int d = 0; d < dims; d++) {
        if (upper[d] < lower[d]) {
            asm_error(line_no, "Invalid array bounds in dimension %d: upper (%d) must be >= lower (%d)", d + 1, upper[d], lower[d]);
        }
        size *= (upper[d] - lower[d] + 1);
    }
    if (array_mem_count + size > MAX_ARRAY_MEM) {
        asm_error(line_no, "Array storage exhausted (limit is %d total elements across all arrays)", MAX_ARRAY_MEM);
    }
    strcpy(sym_table[sym_count].name, name);
    sym_table[sym_count].type = elem_type;
    sym_table[sym_count].is_array = 1;
    sym_table[sym_count].array_lower = 0;
    sym_table[sym_count].array_upper = 0;
    sym_table[sym_count].array_base = array_mem_count;
    sym_table[sym_count].is_2d = 0;
    sym_table[sym_count].is_nd = 1;
    sym_table[sym_count].nd_dims = dims;
    for (int d = 0; d < dims; d++) {
        sym_table[sym_count].nd_lower[d] = lower[d];
        sym_table[sym_count].nd_upper[d] = upper[d];
    }
    array_mem_count += size;
    sym_count++;
}

// A 1D array of records ('.arrayrec <name> <lower> <upper> <field_count>')
// - see is_record_array/record_elem_field_count in the Symbol comment in
// common.h. field_count is just an integer here (solas has no notion of
// a "record type" at all - it never links parser.c's record_types[],
// only ever sees the resulting Symbol's own stride).
static void add_array_var_rec(int line_no, const char *name, int lower, int upper, int field_count) {
    if (strlen(name) >= MAX_NAME) {
        asm_error(line_no, "Array name '%s' too long (limit is %d characters)", name, MAX_NAME - 1);
    }
    if (find_var(name) != -1) {
        asm_error(line_no, "Duplicate variable declaration '%s'", name);
    }
    if (sym_count >= MAX_SYMBOLS) {
        asm_error(line_no, "Too many variable declarations (limit is %d)", MAX_SYMBOLS);
    }
    if (upper < lower) {
        asm_error(line_no, "Invalid array bounds: upper (%d) must be >= lower (%d)", upper, lower);
    }
    if (field_count < 1) {
        asm_error(line_no, "Invalid record field count %d (must be >= 1)", field_count);
    }
    int size = (upper - lower + 1) * field_count;
    if (array_mem_count + size > MAX_ARRAY_MEM) {
        asm_error(line_no, "Array storage exhausted (limit is %d total elements across all arrays)", MAX_ARRAY_MEM);
    }
    strcpy(sym_table[sym_count].name, name);
    sym_table[sym_count].type = TYPE_INTEGER; // unused - see is_record_array in common.h
    sym_table[sym_count].is_array = 1;
    sym_table[sym_count].array_lower = lower;
    sym_table[sym_count].array_upper = upper;
    sym_table[sym_count].array_base = array_mem_count;
    sym_table[sym_count].is_2d = 0;
    sym_table[sym_count].is_nd = 0;
    sym_table[sym_count].is_record_array = 1;
    sym_table[sym_count].record_elem_field_count = field_count;
    array_mem_count += size;
    sym_count++;
}

static void add_label(int line_no, const char *name, int index) {
    if (strlen(name) >= MAX_NAME) {
        asm_error(line_no, "Label name '%s' too long (limit is %d characters)", name, MAX_NAME - 1);
    }
    if (find_label(name) != -1) {
        asm_error(line_no, "Duplicate label '%s'", name);
    }
    if (label_count >= MAX_LABELS) {
        asm_error(line_no, "Too many labels (limit is %d)", MAX_LABELS);
    }
    strcpy(labels[label_count].name, name);
    labels[label_count].index = index;
    label_count++;
}

static int intern_string(int line_no, const char *s) {
    for (int i = 0; i < string_count; i++) {
        if (strcmp(string_pool[i], s) == 0) return i;
    }
    if (string_count >= MAX_STRINGS) {
        asm_error(line_no, "Too many distinct string literals (limit is %d)", MAX_STRINGS);
    }
    strcpy(string_pool[string_count], s);
    return string_count++;
}

// Extracts the text between the first and last '"' on the line (no escape
// sequences supported yet - matches the "one instruction per line" format).
static void extract_quoted_string(int line_no, const char *line, char *out, size_t out_cap) {
    const char *start = strchr(line, '"');
    if (!start) asm_error(line_no, "Expected a quoted string operand, e.g. PUSH_STR \"hello\"");
    const char *end = strrchr(line, '"');
    if (end <= start) asm_error(line_no, "Malformed string literal (missing closing quote)");
    size_t len = (size_t)(end - start - 1);
    if (len >= out_cap) asm_error(line_no, "String literal too long (limit is %zu characters)", out_cap - 1);
    memcpy(out, start + 1, len);
    out[len] = '\0';
}

static void asm_emit(int line_no, Opcode op, int arg) {
    if (code_idx >= MAX_CODE) {
        asm_error(line_no, "Program exceeds maximum bytecode size (limit is %d instructions)", MAX_CODE);
    }
    code[code_idx].op = op;
    code[code_idx].arg = arg;
    code_idx++;
}

// A label line is a single token (no embedded whitespace) ending in ':'.
static int is_label_line(const char *line) {
    size_t len = strlen(line);
    if (len == 0 || line[len - 1] != ':') return 0;
    for (size_t i = 0; i < len - 1; i++) {
        if (isspace((unsigned char)line[i])) return 0;
    }
    return 1;
}

// Splits `source` in place into trimmed, comment-stripped, non-blank
// lines. Line numbers refer to the original file for error reporting.
static void split_lines(char *source) {
    num_lines = 0;
    int line_no = 0;
    char *cursor = source;
    while (*cursor) {
        line_no++;
        char *line_start = cursor;
        while (*cursor && *cursor != '\n') cursor++;
        if (*cursor == '\n') { *cursor = '\0'; cursor++; }

        char *semi = strchr(line_start, ';');
        if (semi) *semi = '\0';

        char *trimmed = trim(line_start);
        if (*trimmed == '\0') continue;

        if (num_lines >= MAX_LINES) {
            asm_error(line_no, "Source file too large (limit is %d non-blank lines)", MAX_LINES);
        }
        lines[num_lines] = trimmed;
        line_numbers[num_lines] = line_no;
        num_lines++;
    }
}

void assemble(char *source, const char *filename) {
    asm_filename = filename;
    sym_count = 0;
    code_idx = 0;
    string_count = 0;
    array_mem_count = 0;
    label_count = 0;

    split_lines(source);

    // Pass 1: register every .var declaration and every label's target
    // instruction index. This is what lets JMP/JZ (and, for consistency,
    // LOAD/STORE/READ) refer to something declared later in the file.
    int instr_count = 0;
    for (int i = 0; i < num_lines; i++) {
        char *line = lines[i];
        int line_no = line_numbers[i];

        if (line[0] == '.') {
            char directive[MAX_NAME];
            sscanf(line, ".%31s", directive);

            if (strcasecmp(directive, "var") == 0) {
                char name[MAX_NAME], type_str[MAX_NAME];
                if (sscanf(line, ".%31s %31s %31s", directive, name, type_str) != 3) {
                    asm_error(line_no, "Malformed directive (expected: .var <name> <integer|boolean|string|char|real>)");
                }
                DataType type;
                if (strcasecmp(type_str, "integer") == 0) type = TYPE_INTEGER;
                else if (strcasecmp(type_str, "boolean") == 0) type = TYPE_BOOLEAN;
                else if (strcasecmp(type_str, "string") == 0) type = TYPE_STRING;
                else if (strcasecmp(type_str, "char") == 0) type = TYPE_CHAR;
                else if (strcasecmp(type_str, "real") == 0) type = TYPE_REAL;
                else if (strcasecmp(type_str, "set") == 0) type = TYPE_SET;
                else if (strcasecmp(type_str, "text") == 0) type = TYPE_FILE;
                else { asm_error(line_no, "Unknown type '%s' (expected 'integer', 'boolean', 'string', 'char', 'real', 'set', or 'text')", type_str); return; }
                add_var(line_no, name, type);
            } else if (strcasecmp(directive, "array") == 0) {
                char name[MAX_NAME], type_str[MAX_NAME];
                int lower, upper;
                if (sscanf(line, ".%31s %31s %d %d %31s", directive, name, &lower, &upper, type_str) != 5) {
                    asm_error(line_no, "Malformed directive (expected: .array <name> <lower> <upper> <integer|boolean|string|char|real>)");
                }
                DataType type;
                if (strcasecmp(type_str, "integer") == 0) type = TYPE_INTEGER;
                else if (strcasecmp(type_str, "boolean") == 0) type = TYPE_BOOLEAN;
                else if (strcasecmp(type_str, "string") == 0) type = TYPE_STRING;
                else if (strcasecmp(type_str, "char") == 0) type = TYPE_CHAR;
                else if (strcasecmp(type_str, "real") == 0) type = TYPE_REAL;
                else if (strcasecmp(type_str, "set") == 0) type = TYPE_SET;
                else { asm_error(line_no, "Unknown type '%s' (expected 'integer', 'boolean', 'string', 'char', 'real', or 'set')", type_str); return; }
                add_array_var(line_no, name, type, lower, upper);
            } else if (strcasecmp(directive, "array2d") == 0) {
                char name[MAX_NAME], type_str[MAX_NAME];
                int lower, upper, lower2, upper2;
                if (sscanf(line, ".%31s %31s %d %d %d %d %31s", directive, name, &lower, &upper, &lower2, &upper2, type_str) != 7) {
                    asm_error(line_no, "Malformed directive (expected: .array2d <name> <lower1> <upper1> <lower2> <upper2> <integer|boolean|string|char|real>)");
                }
                DataType type;
                if (strcasecmp(type_str, "integer") == 0) type = TYPE_INTEGER;
                else if (strcasecmp(type_str, "boolean") == 0) type = TYPE_BOOLEAN;
                else if (strcasecmp(type_str, "string") == 0) type = TYPE_STRING;
                else if (strcasecmp(type_str, "char") == 0) type = TYPE_CHAR;
                else if (strcasecmp(type_str, "real") == 0) type = TYPE_REAL;
                else if (strcasecmp(type_str, "set") == 0) type = TYPE_SET;
                else { asm_error(line_no, "Unknown type '%s' (expected 'integer', 'boolean', 'string', 'char', 'real', or 'set')", type_str); return; }
                add_array_var_2d(line_no, name, type, lower, upper, lower2, upper2);
            } else if (strcasecmp(directive, "arraynd") == 0) {
                // Variable-arity (3+ dimensions), unlike .array/.array2d's
                // fixed-field sscanf format - tokenized by hand instead:
                // '.arrayNd <name> <dims> <lower1> <upper1> ... <type>'.
                char line_copy[512];
                strncpy(line_copy, line, sizeof(line_copy) - 1);
                line_copy[sizeof(line_copy) - 1] = '\0';
                strtok(line_copy, " \t"); // the directive itself - discarded
                char *name_tok = strtok(NULL, " \t");
                char *dims_tok = strtok(NULL, " \t");
                int dims;
                if (!name_tok || !dims_tok || sscanf(dims_tok, "%d", &dims) != 1 || dims < 3 || dims > MAX_ARRAY_DIMS) {
                    asm_error(line_no, "Malformed directive (expected: .arrayNd <name> <dims (3..%d)> <lower1> <upper1> ... <type>)", MAX_ARRAY_DIMS);
                    return;
                }
                char name[MAX_NAME];
                strncpy(name, name_tok, MAX_NAME - 1);
                name[MAX_NAME - 1] = '\0';
                int lower[MAX_ARRAY_DIMS], upper[MAX_ARRAY_DIMS];
                for (int d = 0; d < dims; d++) {
                    char *lo_tok = strtok(NULL, " \t");
                    char *hi_tok = strtok(NULL, " \t");
                    if (!lo_tok || !hi_tok || sscanf(lo_tok, "%d", &lower[d]) != 1 || sscanf(hi_tok, "%d", &upper[d]) != 1) {
                        asm_error(line_no, "Malformed directive (expected: .arrayNd <name> <dims> <lower1> <upper1> ... <type>)");
                        return;
                    }
                }
                char *type_tok = strtok(NULL, " \t");
                if (!type_tok) {
                    asm_error(line_no, "Malformed directive (missing element type)");
                    return;
                }
                char type_str[MAX_NAME];
                strncpy(type_str, type_tok, MAX_NAME - 1);
                type_str[MAX_NAME - 1] = '\0';
                DataType type;
                if (strcasecmp(type_str, "integer") == 0) type = TYPE_INTEGER;
                else if (strcasecmp(type_str, "boolean") == 0) type = TYPE_BOOLEAN;
                else if (strcasecmp(type_str, "string") == 0) type = TYPE_STRING;
                else if (strcasecmp(type_str, "char") == 0) type = TYPE_CHAR;
                else if (strcasecmp(type_str, "real") == 0) type = TYPE_REAL;
                else if (strcasecmp(type_str, "set") == 0) type = TYPE_SET;
                else { asm_error(line_no, "Unknown type '%s' (expected 'integer', 'boolean', 'string', 'char', 'real', or 'set')", type_str); return; }
                add_array_var_nd(line_no, name, type, dims, lower, upper);
            } else if (strcasecmp(directive, "arrayrec") == 0) {
                char name[MAX_NAME];
                int lower, upper, field_count;
                if (sscanf(line, ".%31s %31s %d %d %d", directive, name, &lower, &upper, &field_count) != 5) {
                    asm_error(line_no, "Malformed directive (expected: .arrayrec <name> <lower> <upper> <field_count>)");
                }
                add_array_var_rec(line_no, name, lower, upper, field_count);
            } else {
                asm_error(line_no, "Unknown directive '.%s' (expected .var, .array, .array2d, .arrayNd, or .arrayrec)", directive);
            }
        } else if (is_label_line(line)) {
            char label_name[MAX_NAME];
            size_t len = strlen(line) - 1;
            if (len >= MAX_NAME) asm_error(line_no, "Label name too long (limit is %d characters)", MAX_NAME - 1);
            memcpy(label_name, line, len);
            label_name[len] = '\0';
            add_label(line_no, label_name, instr_count);
        } else {
            char mnemonic[MAX_NAME];
            sscanf(line, "%31s", mnemonic);
            if (!find_opcode(mnemonic)) {
                asm_error(line_no, "Unknown instruction '%s'", mnemonic);
            }
            instr_count++;
        }
    }

    // Pass 2: emit real instructions, now that every label/variable is
    // known regardless of where in the file it was declared.
    for (int i = 0; i < num_lines; i++) {
        char *line = lines[i];
        int line_no = line_numbers[i];

        if (line[0] == '.' || is_label_line(line)) continue;

        char mnemonic[MAX_NAME], operand[MAX_NAME];
        int noperand = sscanf(line, "%31s %31s", mnemonic, operand);
        const OpcodeInfo *info = find_opcode(mnemonic); // already validated in pass 1

        int arg = 0;
        switch (info->kind) {
            case OPERAND_NONE:
                if (noperand > 1) {
                    asm_error(line_no, "Instruction '%s' takes no operand", info->mnemonic);
                }
                break;

            case OPERAND_IMMEDIATE: {
                if (noperand < 2) asm_error(line_no, "Instruction '%s' requires an integer operand", info->mnemonic);
                char *endptr;
                long val = strtol(operand, &endptr, 10);
                if (*endptr != '\0') asm_error(line_no, "Invalid integer operand '%s'", operand);
                arg = (int)val;
                break;
            }

            case OPERAND_VAR: {
                if (noperand < 2) asm_error(line_no, "Instruction '%s' requires a variable operand", info->mnemonic);
                int idx = find_var(operand);
                if (idx == -1) asm_error(line_no, "Undeclared variable '%s' (missing .var or .array?)", operand);
                arg = idx;
                break;
            }

            case OPERAND_LABEL: {
                if (noperand < 2) asm_error(line_no, "Instruction '%s' requires a label operand", info->mnemonic);
                int idx = find_label(operand);
                if (idx == -1) asm_error(line_no, "Undefined label '%s'", operand);
                arg = idx;
                break;
            }

            case OPERAND_STRING: {
                char str_content[MAX_STRING_LEN];
                extract_quoted_string(line_no, line, str_content, sizeof(str_content));
                arg = intern_string(line_no, str_content);
                break;
            }
        }
        asm_emit(line_no, info->op, arg);
    }
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("Failed to open source file"); fatal_abort(); }
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buffer = malloc(length + 1);
    if (!buffer) { fprintf(stderr, "Allocation failure\n"); fatal_abort(); }
    fread(buffer, 1, length, f);
    buffer[length] = '\0';
    fclose(f);
    return buffer;
}

int main(int argc, char *argv[]) {
    const char *positional[2];
    int npos = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose_mode = 1;
        } else if (npos < 2) {
            positional[npos++] = argv[i];
        } else {
            npos++; // trigger usage error below
        }
    }

    if (npos != 2) {
        printf("Usage: %s [-v] <input.sasm> <output.bin>\n", argv[0]);
        return 1;
    }

    if (setjmp(fatal_error_env)) {
        fprintf(stderr, "Assembly failed.\n");
        return 1;
    }
    fatal_error_active = 1;

    const char *source_path = positional[0];
    const char *bin_path = positional[1];
    char *source = read_file(source_path);

    assemble(source, source_path);
    save_bytecode(bin_path);
    if (verbose_mode) {
        printf("[Assembler] Successfully written binary payload image to %s (%d instructions, %d symbols, %d strings)\n",
               bin_path, code_idx, sym_count, string_count);
    }

    free(source);
    fatal_error_active = 0;
    return 0;
}
