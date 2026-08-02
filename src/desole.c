// desole - the SolVM disassembler.
//
// Reads a .bin bytecode image (the same format pascalc writes and solvm
// executes) and prints it back out as solas-syntax assembly: .var
// declarations, synthesized labels at every jump target, and one
// instruction per line. The output is valid solas input - round-tripping
// a .bin through desole then solas reproduces an equivalent program.
//
// Usage:
//   desole <input.bin>              prints to stdout
//   desole <input.bin> <output.sasm>  writes to a file
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "bytecode.h"
#include "error.h"

static const char *opcode_name(Opcode op) {
    switch (op) {
        case OP_PUSH:  return "push";
        case OP_LOAD:  return "load";
        case OP_STORE: return "store";
        case OP_ADD:   return "add";
        case OP_SUB:   return "sub";
        case OP_MUL:   return "mul";
        case OP_DIV:   return "div";
        case OP_EQ:    return "eq";
        case OP_LT:    return "lt";
        case OP_GT:    return "gt";
        case OP_AND:   return "and";
        case OP_OR:    return "or";
        case OP_NOT:   return "not";
        case OP_LTE:   return "lte";
        case OP_GTE:   return "gte";
        case OP_NEQ:   return "neq";
        case OP_NEG:   return "neg";
        case OP_MOD:   return "mod";
        case OP_XOR:   return "xor";
        case OP_BAND:  return "band";
        case OP_BOR:   return "bor";
        case OP_BXOR:  return "bxor";
        case OP_BNOT:  return "bnot";
        case OP_SHL:   return "shl";
        case OP_SHR:   return "shr";
        case OP_DUP:   return "dup";
        case OP_ABS:   return "abs";
        case OP_FABS:  return "fabs";
        case OP_FSQRT:   return "fsqrt";
        case OP_FSIN:    return "fsin";
        case OP_FCOS:    return "fcos";
        case OP_FARCTAN: return "farctan";
        case OP_FEXP:    return "fexp";
        case OP_FLN:     return "fln";
        case OP_FPOWER:  return "fpower";
        case OP_STR_CHAR_REPLACE: return "str_char_replace";
        case OP_ORD:   return "ord";
        case OP_CHR:   return "chr";
        case OP_LENGTH:        return "length";
        case OP_STR_CHAR_AT:   return "str_char_at";
        case OP_COPY:          return "copy";
        case OP_POS:           return "pos";
        case OP_UPCASE_CHAR:   return "upcase_char";
        case OP_UPPERCASE_STR: return "uppercase_str";
        case OP_LOWERCASE_STR: return "lowercase_str";
        case OP_LEFT:          return "left";
        case OP_RIGHT:         return "right";
        case OP_PRINT: return "print";
        case OP_PRINT_BOOL: return "print_bool";
        case OP_READ:  return "read";
        case OP_READ_LOCAL_INT:  return "read_local_int";
        case OP_READ_LOCAL_BOOL: return "read_local_bool";
        case OP_READ_LOCAL_REAL: return "read_local_real";
        case OP_READ_LOCAL_STR:  return "read_local_str";
        case OP_READ_LOCAL_CHAR: return "read_local_char";
        case OP_HALT:  return "halt";
        case OP_JMP:   return "jmp";
        case OP_JZ:    return "jz";
        case OP_CALL:  return "call";
        case OP_RET:   return "ret";
        case OP_ENTER:       return "enter";
        case OP_LOAD_LOCAL:  return "load_local";
        case OP_STORE_LOCAL: return "store_local";
        case OP_POP:         return "pop";
        case OP_PUSH_STR:  return "push_str";
        case OP_PRINT_STR: return "print_str";
        case OP_SEQ:       return "seq";
        case OP_SCMP:      return "scmp";
        case OP_SCONCAT:   return "sconcat";
        case OP_NEWLINE:   return "newline";
        case OP_LOAD_IDX:  return "load_idx";
        case OP_STORE_IDX: return "store_idx";
        case OP_LOAD_IDX2D:  return "load_idx2d";
        case OP_STORE_IDX2D: return "store_idx2d";
        case OP_FADD:  return "fadd";
        case OP_FSUB:  return "fsub";
        case OP_FMUL:  return "fmul";
        case OP_FDIV:  return "fdiv";
        case OP_FEQ:   return "feq";
        case OP_FLT:   return "flt";
        case OP_FGT:   return "fgt";
        case OP_FLTE:  return "flte";
        case OP_FGTE:  return "fgte";
        case OP_FNEQ:  return "fneq";
        case OP_FNEG:  return "fneg";
        case OP_FPRINT: return "fprint";
        case OP_INT_TO_REAL: return "int_to_real";
        case OP_TRUNC: return "trunc";
        case OP_ROUND: return "round";
        case OP_PRINT_PADDED:      return "print_padded";
        case OP_PRINT_STR_PADDED:  return "print_str_padded";
        case OP_PRINT_BOOL_PADDED: return "print_bool_padded";
        case OP_FPRINT_PADDED:         return "fprint_padded";
        case OP_FPRINT_PADDED_PRECISE: return "fprint_padded_precise";
        case OP_LOAD_IDX_DYN:  return "load_idx_dyn";
        case OP_STORE_IDX_DYN: return "store_idx_dyn";
        case OP_CHECK_LOWER: return "check_lower";
        case OP_CHECK_UPPER: return "check_upper";
        case OP_ASSERT: return "assert";
        case OP_LOAD_IDX2D_DYN:  return "load_idx2d_dyn";
        case OP_STORE_IDX2D_DYN: return "store_idx2d_dyn";
        case OP_PUSH_LOCAL_REF: return "push_local_ref";
        case OP_LOAD_REF:  return "load_ref";
        case OP_STORE_REF: return "store_ref";
        case OP_READ_NOFLUSH: return "read_noflush";
        case OP_READ_LOCAL_INT_NOFLUSH:  return "read_local_int_noflush";
        case OP_READ_LOCAL_BOOL_NOFLUSH: return "read_local_bool_noflush";
        case OP_READ_LOCAL_REAL_NOFLUSH: return "read_local_real_noflush";
        case OP_EOF:  return "eof";
        case OP_EOLN: return "eoln";
        case OP_LOAD_IDXND:  return "load_idxnd";
        case OP_STORE_IDXND: return "store_idxnd";
        case OP_LOAD_IDXND_DYN:  return "load_idxnd_dyn";
        case OP_STORE_IDXND_DYN: return "store_idxnd_dyn";
        case OP_FILE_ASSIGN:  return "file_assign";
        case OP_FILE_RESET:   return "file_reset";
        case OP_FILE_REWRITE: return "file_rewrite";
        case OP_FILE_CLOSE:   return "file_close";
        case OP_PRINT_FILE:      return "print_file";
        case OP_PRINT_STR_FILE:  return "print_str_file";
        case OP_PRINT_BOOL_FILE: return "print_bool_file";
        case OP_FPRINT_FILE:     return "fprint_file";
        case OP_PRINT_PADDED_FILE:      return "print_padded_file";
        case OP_PRINT_STR_PADDED_FILE:  return "print_str_padded_file";
        case OP_PRINT_BOOL_PADDED_FILE: return "print_bool_padded_file";
        case OP_FPRINT_PADDED_FILE:         return "fprint_padded_file";
        case OP_FPRINT_PADDED_PRECISE_FILE: return "fprint_padded_precise_file";
        case OP_NEWLINE_FILE: return "newline_file";
        case OP_EOF_FILE:  return "eof_file";
        case OP_EOLN_FILE: return "eoln_file";
        case OP_READ_FILE:         return "read_file";
        case OP_READ_FILE_NOFLUSH: return "read_file_noflush";
        case OP_READ_FILE_LOCAL_INT:  return "read_file_local_int";
        case OP_READ_FILE_LOCAL_BOOL: return "read_file_local_bool";
        case OP_READ_FILE_LOCAL_REAL: return "read_file_local_real";
        case OP_READ_FILE_LOCAL_STR:  return "read_file_local_str";
        case OP_READ_FILE_LOCAL_CHAR: return "read_file_local_char";
        case OP_READ_FILE_LOCAL_INT_NOFLUSH:  return "read_file_local_int_noflush";
        case OP_READ_FILE_LOCAL_BOOL_NOFLUSH: return "read_file_local_bool_noflush";
        case OP_READ_FILE_LOCAL_REAL_NOFLUSH: return "read_file_local_real_noflush";
        default:       return NULL;
    }
}

static const char *type_name(DataType type) {
    // A specific enumerated type (TYPE_ENUM_BASE + its enum_types[]
    // index - see common.h) is a pascalc-frontend-only concept: desole
    // never links parser.c, so it has no way to know which enum type
    // (or even that it WAS an enum, as opposed to a plain integer) a
    // given Symbol originally was - it prints as "integer", which is
    // its actual runtime representation, exactly as accurate as this
    // tool can be without a .bin format change.
    if (type >= TYPE_ENUM_BASE) return "integer";
    switch (type) {
        case TYPE_INTEGER: return "integer";
        case TYPE_BOOLEAN: return "boolean";
        case TYPE_STRING:  return "string";
        case TYPE_CHAR:    return "char";
        case TYPE_REAL:    return "real";
        case TYPE_SET:     return "set"; // its actual runtime representation
                           // is a plain int bitmask - see TYPE_SET's
                           // comment in common.h - but "set" is more
                           // informative in a variable-dump than "integer"
        case TYPE_FILE:    return "text";
        default:           return "unknown";
    }
}

// True for opcodes whose arg is an absolute instruction index (a jump
// target), as opposed to an immediate value or a variable index.
static int is_jump(Opcode op) {
    return op == OP_JMP || op == OP_JZ || op == OP_CALL;
}

// True for opcodes whose arg is a variable index into sym_table[]. Note
// OP_LOAD_IDXND_DYN/OP_STORE_IDXND_DYN are deliberately NOT included here -
// unlike every other opcode in this list, their arg is a fixed dimension
// count, not a symbol reference (see their comment in common.h).
static int is_var_ref(Opcode op) {
    return op == OP_LOAD || op == OP_STORE || op == OP_READ || op == OP_READ_NOFLUSH
        || op == OP_LOAD_IDX || op == OP_STORE_IDX
        || op == OP_LOAD_IDX2D || op == OP_STORE_IDX2D
        || op == OP_LOAD_IDXND || op == OP_STORE_IDXND
        || op == OP_FILE_ASSIGN || op == OP_FILE_RESET || op == OP_FILE_REWRITE || op == OP_FILE_CLOSE
        || op == OP_PRINT_FILE || op == OP_PRINT_STR_FILE || op == OP_PRINT_BOOL_FILE || op == OP_FPRINT_FILE
        || op == OP_PRINT_PADDED_FILE || op == OP_PRINT_STR_PADDED_FILE || op == OP_PRINT_BOOL_PADDED_FILE
        || op == OP_FPRINT_PADDED_FILE || op == OP_FPRINT_PADDED_PRECISE_FILE
        || op == OP_NEWLINE_FILE || op == OP_EOF_FILE || op == OP_EOLN_FILE
        || op == OP_READ_FILE || op == OP_READ_FILE_NOFLUSH; // note: OP_READ_FILE's arg is
                                                              // the READ TARGET's index, still
                                                              // a sym_table[] reference either way
}

// True for opcodes whose arg is a plain immediate value with no lookup -
// a frame-relative local slot number is just as much "a number" here as
// PUSH's literal is; neither refers to anything named.
static int is_immediate(Opcode op) {
    return op == OP_PUSH || op == OP_ENTER || op == OP_LOAD_LOCAL || op == OP_STORE_LOCAL
        || op == OP_CHECK_LOWER || op == OP_CHECK_UPPER || op == OP_PUSH_LOCAL_REF
        || op == OP_READ_LOCAL_INT_NOFLUSH || op == OP_READ_LOCAL_REAL_NOFLUSH || op == OP_READ_LOCAL_BOOL_NOFLUSH
        // Pre-existing gap fixed in passing: these five were never added
        // here, so disassembly silently dropped their frame-slot operand
        // entirely (same class of bug as the CHECK_LOWER/CHECK_UPPER one
        // this file's history already ran into once).
        || op == OP_READ_LOCAL_INT || op == OP_READ_LOCAL_BOOL || op == OP_READ_LOCAL_REAL
        || op == OP_READ_LOCAL_STR || op == OP_READ_LOCAL_CHAR
        || op == OP_READ_FILE_LOCAL_INT || op == OP_READ_FILE_LOCAL_BOOL || op == OP_READ_FILE_LOCAL_REAL
        || op == OP_READ_FILE_LOCAL_STR || op == OP_READ_FILE_LOCAL_CHAR
        || op == OP_READ_FILE_LOCAL_INT_NOFLUSH || op == OP_READ_FILE_LOCAL_BOOL_NOFLUSH || op == OP_READ_FILE_LOCAL_REAL_NOFLUSH;
}

static char *jump_targets = NULL; // one flag byte per instruction index

static void mark_jump_targets(void) {
    jump_targets = calloc((size_t)(code_idx > 0 ? code_idx : 1), 1);
    if (!jump_targets) { fprintf(stderr, "Allocation failure\n"); fatal_abort(); }
    for (int i = 0; i < code_idx; i++) {
        if (is_jump(code[i].op)) {
            int target = code[i].arg;
            if (target >= 0 && target < code_idx) {
                jump_targets[target] = 1;
            }
        }
    }
}

static void disassemble(FILE *out) {
    if (sym_count > 0) {
        for (int i = 0; i < sym_count; i++) {
            if (sym_table[i].is_array && sym_table[i].is_nd) {
                fprintf(out, ".arrayNd %s %d", sym_table[i].name, sym_table[i].nd_dims);
                for (int d = 0; d < sym_table[i].nd_dims; d++) {
                    fprintf(out, " %d %d", sym_table[i].nd_lower[d], sym_table[i].nd_upper[d]);
                }
                fprintf(out, " %s\n", type_name(sym_table[i].type));
            } else if (sym_table[i].is_array && sym_table[i].is_2d) {
                fprintf(out, ".array2d %s %d %d %d %d %s\n", sym_table[i].name,
                        sym_table[i].array_lower, sym_table[i].array_upper,
                        sym_table[i].array_lower2, sym_table[i].array_upper2, type_name(sym_table[i].type));
            } else if (sym_table[i].is_array) {
                fprintf(out, ".array %s %d %d %s\n", sym_table[i].name,
                        sym_table[i].array_lower, sym_table[i].array_upper, type_name(sym_table[i].type));
            } else {
                fprintf(out, ".var %s %s\n", sym_table[i].name, type_name(sym_table[i].type));
            }
        }
        fprintf(out, "\n");
    }

    mark_jump_targets();

    for (int i = 0; i < code_idx; i++) {
        if (jump_targets[i]) {
            fprintf(out, "L%d:\n", i);
        }

        Instruction instr = code[i];
        const char *mnemonic = opcode_name(instr.op);

        if (!mnemonic) {
            fprintf(out, "    ; <unknown opcode %d, arg=%d> ; %04d\n", instr.op, instr.arg, i);
            continue;
        }

        if (is_immediate(instr.op)) {
            fprintf(out, "    %s %d ; %04d\n", mnemonic, instr.arg, i);
        } else if (instr.op == OP_PUSH_STR) {
            if (instr.arg >= 0 && instr.arg < string_count) {
                fprintf(out, "    %s \"%s\" ; %04d\n", mnemonic, string_pool[instr.arg], i);
            } else {
                fprintf(out, "    ; <push_str with out-of-range string index %d: 0..%d> ; %04d\n",
                        instr.arg, string_count - 1, i);
            }
        } else if (is_var_ref(instr.op)) {
            if (instr.arg >= 0 && instr.arg < sym_count) {
                fprintf(out, "    %s %s ; %04d\n", mnemonic, sym_table[instr.arg].name, i);
            } else {
                fprintf(out, "    %s ?%d ; %04d  (variable index out of range: 0..%d)\n",
                        mnemonic, instr.arg, i, sym_count - 1);
            }
        } else if (is_jump(instr.op)) {
            if (instr.arg >= 0 && instr.arg < code_idx) {
                fprintf(out, "    %s L%d ; %04d\n", mnemonic, instr.arg, i);
            } else {
                fprintf(out, "    %s ?%d ; %04d  (jump target out of range: 0..%d)\n",
                        mnemonic, instr.arg, i, code_idx - 1);
            }
        } else {
            fprintf(out, "    %s ; %04d\n", mnemonic, i);
        }
    }

    free(jump_targets);
    jump_targets = NULL;
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

    if (npos != 1 && npos != 2) {
        printf("Usage: %s [-v] <input.bin> [output.sasm]\n", argv[0]);
        return 1;
    }

    if (setjmp(fatal_error_env)) {
        fprintf(stderr, "Disassembly failed.\n");
        return 1;
    }
    fatal_error_active = 1;

    const char *bin_path = positional[0];
    load_bytecode(bin_path);

    FILE *out = stdout;
    if (npos == 2) {
        out = fopen(positional[1], "w");
        if (!out) { perror("Failed to open output file"); fatal_abort(); }
    }

    disassemble(out);

    if (out != stdout) {
        fclose(out);
        if (verbose_mode) {
            fprintf(stderr, "[Disassembler] Wrote %s (%d instructions, %d symbols, %d strings)\n",
                    positional[1], code_idx, sym_count, string_count);
        }
    }

    fatal_error_active = 0;
    return 0;
}
