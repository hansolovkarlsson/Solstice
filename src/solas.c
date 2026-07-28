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
    {"PRINT",     OP_PRINT,     OPERAND_NONE},
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
    {"LOAD_IDX_DYN",  OP_LOAD_IDX_DYN,  OPERAND_NONE},
    {"STORE_IDX_DYN", OP_STORE_IDX_DYN, OPERAND_NONE},
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
                    asm_error(line_no, "Malformed directive (expected: .var <name> <integer|boolean|string|char>)");
                }
                DataType type;
                if (strcasecmp(type_str, "integer") == 0) type = TYPE_INTEGER;
                else if (strcasecmp(type_str, "boolean") == 0) type = TYPE_BOOLEAN;
                else if (strcasecmp(type_str, "string") == 0) type = TYPE_STRING;
                else if (strcasecmp(type_str, "char") == 0) type = TYPE_CHAR;
                else { asm_error(line_no, "Unknown type '%s' (expected 'integer', 'boolean', 'string', or 'char')", type_str); return; }
                add_var(line_no, name, type);
            } else if (strcasecmp(directive, "array") == 0) {
                char name[MAX_NAME], type_str[MAX_NAME];
                int lower, upper;
                if (sscanf(line, ".%31s %31s %d %d %31s", directive, name, &lower, &upper, type_str) != 5) {
                    asm_error(line_no, "Malformed directive (expected: .array <name> <lower> <upper> <integer|boolean|string|char>)");
                }
                DataType type;
                if (strcasecmp(type_str, "integer") == 0) type = TYPE_INTEGER;
                else if (strcasecmp(type_str, "boolean") == 0) type = TYPE_BOOLEAN;
                else if (strcasecmp(type_str, "string") == 0) type = TYPE_STRING;
                else if (strcasecmp(type_str, "char") == 0) type = TYPE_CHAR;
                else { asm_error(line_no, "Unknown type '%s' (expected 'integer', 'boolean', 'string', or 'char')", type_str); return; }
                add_array_var(line_no, name, type, lower, upper);
            } else {
                asm_error(line_no, "Unknown directive '.%s' (expected .var or .array)", directive);
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
