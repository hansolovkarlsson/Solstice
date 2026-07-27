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
        case OP_PRINT: return "print";
        case OP_READ:  return "read";
        case OP_HALT:  return "halt";
        case OP_JMP:   return "jmp";
        case OP_JZ:    return "jz";
        case OP_PUSH_STR:  return "push_str";
        case OP_PRINT_STR: return "print_str";
        case OP_SEQ:       return "seq";
        case OP_SCMP:      return "scmp";
        case OP_SCONCAT:   return "sconcat";
        case OP_NEWLINE:   return "newline";
        case OP_LOAD_IDX:  return "load_idx";
        case OP_STORE_IDX: return "store_idx";
        default:       return NULL;
    }
}

static const char *type_name(DataType type) {
    switch (type) {
        case TYPE_INTEGER: return "integer";
        case TYPE_BOOLEAN: return "boolean";
        case TYPE_STRING:  return "string";
        default:           return "unknown";
    }
}

// True for opcodes whose arg is an absolute instruction index (a jump
// target), as opposed to an immediate value or a variable index.
static int is_jump(Opcode op) {
    return op == OP_JMP || op == OP_JZ;
}

// True for opcodes whose arg is a variable index into sym_table[].
static int is_var_ref(Opcode op) {
    return op == OP_LOAD || op == OP_STORE || op == OP_READ || op == OP_LOAD_IDX || op == OP_STORE_IDX;
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
            if (sym_table[i].is_array) {
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

        if (instr.op == OP_PUSH) {
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
