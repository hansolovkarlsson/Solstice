#include <stdio.h>
#include <string.h>
#include "basic.h"

static void emit(Opcode op, int arg) {
    if (code_idx >= MAX_CODE) {
        fprintf(stderr, "%s: Compile Error: Program exceeds maximum bytecode size (limit is %d instructions)\n",
                basic_get_current_filename(), MAX_CODE);
        fatal_abort();
    }
    code[code_idx].op = op;
    code[code_idx].arg = arg;
    code_idx++;
}

static int float_to_bits_local(float f) { int bits; memcpy(&bits, &f, sizeof(bits)); return bits; }

// Ordering ('<'/'>'/'<='/'>=') on strings has no direct opcode - same
// idiom pascalc's own emit_ordering() uses: OP_SCMP leaves -1/0/1, then
// compare THAT against a literal 0 with the ordinary integer opcode.
static void emit_ordering(DataType operand_type, Opcode int_op) {
    if (operand_type == TYPE_STRING) {
        emit(OP_SCMP, 0);
        emit(OP_PUSH, 0);
        emit(int_op, 0);
    } else {
        emit(int_op, 0);
    }
}

// GOTO/GOSUB/THEN-linenum target resolution: every distinct line number
// is already known (basic_line_numbers[]/basic_line_count, from the
// parser), but a target's actual CODE ADDRESS isn't known until codegen
// reaches that line - so every jump is unconditionally backpatched, one
// final pass at the end of basic_generate_program(), mirroring pascalc's
// own pending_calls[]/pending_proc_refs[] resolution in generate_program().
typedef struct {
    int patch_idx;    // code[] index of the OP_JMP/OP_CALL to patch
    int target_line;  // the BASIC line number it should land on
} PendingLineJump;

#define MAX_PENDING_LINE_JUMPS 1000
static PendingLineJump pending_jumps[MAX_PENDING_LINE_JUMPS];
static int pending_jump_count = 0;

static void record_line_jump(int patch_idx, int target_line) {
    if (pending_jump_count >= MAX_PENDING_LINE_JUMPS) {
        fprintf(stderr, "%s: Compile Error: Too many GOTO/GOSUB/THEN line references (limit is %d)\n",
                basic_get_current_filename(), MAX_PENDING_LINE_JUMPS);
        fatal_abort();
    }
    pending_jumps[pending_jump_count].patch_idx = patch_idx;
    pending_jumps[pending_jump_count].target_line = target_line;
    pending_jump_count++;
}

// code_idx of each basic_line_numbers[] entry's first instruction, -1
// until basic_generate_code()'s line-boundary check (below) reaches it.
static int line_addr[MAX_BASIC_LINES];
static int last_line_seen = -1;

// FOR/NEXT: two independent flat statements (possibly many lines apart,
// with arbitrary code between), matched the same way parens/brackets
// are - each NEXT closes the innermost still-open FOR. Tracked here as a
// runtime (codegen-time) stack rather than in the AST itself: FOR/NEXT
// need no parent/child relationship in the tree at all, since the
// statement chain between them (node->next, walked exactly like every
// other statement) already IS the loop body - see basic_generate_code()'s
// BNODE_FOR/BNODE_NEXT cases below.
typedef struct {
    int var_idx;
    DataType var_type;
    BasicASTNode *step_expr; // NULL = default step of 1 - re-evaluated
                             // (like the end bound) each iteration rather
                             // than cached at loop entry - see docs/BASIC.md.
    int step_is_negative;
    int loop_start;
    int jz_patch_idx;
} ForFrame;

#define MAX_FOR_DEPTH 32
static ForFrame for_stack[MAX_FOR_DEPTH];
static int for_depth = 0;

// STEP is required (by the type checker) to be a literal, optionally
// wrapped in a unary minus and/or an int-to-real promotion - this just
// unwraps those to read off its compile-time sign.
static int for_step_is_negative(BasicASTNode *step) {
    if (step->type == BNODE_INT_TO_REAL) return for_step_is_negative(step->left);
    if (step->type == BNODE_UNARY_OP && step->op == BTOK_MINUS) return 1;
    return 0;
}

void basic_generate_code(BasicASTNode *node) {
    if (!node) return;

    if (node->line != last_line_seen) {
        int idx = basic_find_line_index(node->line);
        if (idx != -1) line_addr[idx] = code_idx;
        last_line_seen = node->line;
    }

    switch (node->type) {
        case BNODE_NUMBER:
            emit(OP_PUSH, node->data.num_value);
            break;

        case BNODE_STRING:
            emit(OP_PUSH_STR, node->data.num_value);
            break;

        case BNODE_VARIABLE:
            emit(OP_LOAD, node->data.var_idx);
            break;

        case BNODE_INT_TO_REAL:
            basic_generate_code(node->left);
            emit(OP_INT_TO_REAL, 0);
            break;

        case BNODE_UNARY_OP:
            basic_generate_code(node->left);
            if (node->op == BTOK_MINUS) {
                emit(node->left->expression_type == TYPE_REAL ? OP_FNEG : OP_NEG, 0);
            } else { // BTOK_NOT - the type checker guarantees an integer operand
                emit(OP_BNOT, 0);
            }
            break;

        case BNODE_BINARY_OP: {
            basic_generate_code(node->left);
            basic_generate_code(node->right);
            DataType lt = node->left->expression_type;
            int is_real = (lt == TYPE_REAL);
            switch (node->op) {
                case BTOK_PLUS:
                    if (lt == TYPE_STRING) emit(OP_SCONCAT, 0);
                    else emit(is_real ? OP_FADD : OP_ADD, 0);
                    break;
                case BTOK_MINUS: emit(is_real ? OP_FSUB : OP_SUB, 0); break;
                case BTOK_MUL:   emit(is_real ? OP_FMUL : OP_MUL, 0); break;
                case BTOK_SLASH: emit(OP_FDIV, 0); break; // both operands already real - see type_checker.c
                case BTOK_EQ:
                    if (lt == TYPE_STRING) emit(OP_SEQ, 0);
                    else emit(is_real ? OP_FEQ : OP_EQ, 0);
                    break;
                case BTOK_NEQ:
                    if (lt == TYPE_STRING) { emit(OP_SEQ, 0); emit(OP_NOT, 0); }
                    else emit(is_real ? OP_FNEQ : OP_NEQ, 0);
                    break;
                case BTOK_LT:
                    if (is_real) emit(OP_FLT, 0); else emit_ordering(lt, OP_LT);
                    break;
                case BTOK_GT:
                    if (is_real) emit(OP_FGT, 0); else emit_ordering(lt, OP_GT);
                    break;
                case BTOK_LTE:
                    if (is_real) emit(OP_FLTE, 0); else emit_ordering(lt, OP_LTE);
                    break;
                case BTOK_GTE:
                    if (is_real) emit(OP_FGTE, 0); else emit_ordering(lt, OP_GTE);
                    break;
                case BTOK_AND: emit(OP_BAND, 0); break; // both operands guaranteed integer - see type_checker.c
                case BTOK_OR:  emit(OP_BOR, 0);  break;
                default: break; // unreachable - the parser never builds any other op here
            }
            break;
        }

        case BNODE_LET:
            basic_generate_code(node->left);
            emit(OP_STORE, node->data.var_idx);
            basic_generate_code(node->next);
            break;

        case BNODE_PRINT: {
            for (BasicASTNode *item = node->left; item; item = item->next) {
                basic_generate_code(item);
                if (item->expression_type == TYPE_STRING) emit(OP_PRINT_STR, 0);
                else if (item->expression_type == TYPE_REAL) emit(OP_FPRINT, 0);
                else emit(OP_PRINT, 0);
            }
            if (!node->data.num_value) emit(OP_NEWLINE, 0);
            basic_generate_code(node->next);
            break;
        }

        case BNODE_INPUT:
            if (node->left) {
                basic_generate_code(node->left);
                emit(OP_PRINT_STR, 0);
            }
            emit(OP_READ, node->data.var_idx); // dispatches on sym_table[]'s own declared type - see vm_read_global()
            basic_generate_code(node->next);
            break;

        // if <cond> then <then> [else <else>]
        //     <cond>
        //     JZ else_or_end     ; patched below
        //     <then>
        //   [ JMP end            ; only emitted if there's an else, patched below
        //   else_or_end:
        //     <else>
        //   end: ]
        case BNODE_IF: {
            basic_generate_code(node->left);
            int jz_idx = code_idx;
            emit(OP_JZ, 0);
            basic_generate_code(node->right);
            if (node->extra) {
                int jmp_idx = code_idx;
                emit(OP_JMP, 0);
                code[jz_idx].arg = code_idx;
                basic_generate_code(node->extra);
                code[jmp_idx].arg = code_idx;
            } else {
                code[jz_idx].arg = code_idx;
            }
            basic_generate_code(node->next);
            break;
        }

        case BNODE_GOTO: {
            int idx = code_idx;
            emit(OP_JMP, 0);
            record_line_jump(idx, node->data.num_value);
            basic_generate_code(node->next);
            break;
        }

        case BNODE_GOSUB: {
            int idx = code_idx;
            emit(OP_CALL, 0);
            record_line_jump(idx, node->data.num_value);
            basic_generate_code(node->next);
            break;
        }

        case BNODE_RETURN:
            emit(OP_RET, 0);
            basic_generate_code(node->next);
            break;

        case BNODE_END:
            emit(OP_HALT, 0);
            basic_generate_code(node->next);
            break;

        // for <var> = <start> to/downto <end> [step <step>]
        //     <start>
        //     STORE var
        //   loop_start:
        //     LOAD var
        //     <end>                ; re-evaluated every iteration - see docs/BASIC.md
        //     LTE/GTE              ; direction fixed by STEP's compile-time sign
        //     JZ end               ; patched once the matching NEXT is reached
        //     <body>               ; just the rest of the flat statement chain,
        //                          ; up to and including the matching NEXT
        // (NEXT, wherever it falls in the chain, does the increment/jump-back
        // and patches the JZ above - see BNODE_NEXT below)
        case BNODE_FOR: {
            basic_generate_code(node->left);
            emit(OP_STORE, node->data.var_idx);

            if (for_depth >= MAX_FOR_DEPTH) {
                fprintf(stderr, "%s:%d: Compile Error: 'FOR' loops nested too deeply (limit is %d)\n",
                        basic_get_current_filename(), node->line, MAX_FOR_DEPTH);
                fatal_abort();
            }
            ForFrame *ff = &for_stack[for_depth++];
            ff->var_idx = node->data.var_idx;
            ff->var_type = sym_table[node->data.var_idx].type;
            ff->step_expr = node->extra;
            ff->step_is_negative = node->extra ? for_step_is_negative(node->extra) : 0;

            ff->loop_start = code_idx;
            emit(OP_LOAD, node->data.var_idx);
            basic_generate_code(node->right);
            if (ff->var_type == TYPE_REAL) {
                emit(ff->step_is_negative ? OP_FGTE : OP_FLTE, 0);
            } else {
                emit(ff->step_is_negative ? OP_GTE : OP_LTE, 0);
            }
            ff->jz_patch_idx = code_idx;
            emit(OP_JZ, 0);

            basic_generate_code(node->next);
            break;
        }

        case BNODE_NEXT: {
            if (for_depth == 0) {
                fprintf(stderr, "%s:%d: Compile Error: 'NEXT' without a matching 'FOR'\n",
                        basic_get_current_filename(), node->line);
                fatal_abort();
            }
            ForFrame *ff = &for_stack[for_depth - 1];
            if (node->data.var_idx != -1 && node->data.var_idx != ff->var_idx) {
                fprintf(stderr, "%s:%d: Compile Error: 'NEXT %s' does not match the innermost 'FOR %s'\n",
                        basic_get_current_filename(), node->line,
                        sym_table[node->data.var_idx].name, sym_table[ff->var_idx].name);
                fatal_abort();
            }

            emit(OP_LOAD, ff->var_idx);
            if (ff->step_expr) {
                basic_generate_code(ff->step_expr);
            } else {
                emit(OP_PUSH, ff->var_type == TYPE_REAL ? float_to_bits_local(1.0f) : 1);
            }
            emit(ff->var_type == TYPE_REAL ? OP_FADD : OP_ADD, 0);
            emit(OP_STORE, ff->var_idx);
            emit(OP_JMP, ff->loop_start);
            code[ff->jz_patch_idx].arg = code_idx;

            for_depth--;
            basic_generate_code(node->next);
            break;
        }
    }
}

void basic_generate_program(BasicASTNode *program) {
    for (int i = 0; i < basic_line_count; i++) line_addr[i] = -1;
    last_line_seen = -1;
    pending_jump_count = 0;
    for_depth = 0;

    basic_generate_code(program);

    // Any line whose own code never got a real address (a comment-only
    // or otherwise statement-less line) falls through to whatever comes
    // next, exactly like real BASIC - cascades correctly even for a run
    // of several such lines in a row, since this sweeps back-to-front.
    for (int i = basic_line_count - 1; i >= 0; i--) {
        if (line_addr[i] == -1) {
            line_addr[i] = (i + 1 < basic_line_count) ? line_addr[i + 1] : code_idx;
        }
    }

    for (int i = 0; i < pending_jump_count; i++) {
        int line_idx = basic_find_line_index(pending_jumps[i].target_line);
        code[pending_jumps[i].patch_idx].arg = line_addr[line_idx]; // type_check() already rejected an undefined target
    }

    if (for_depth != 0) {
        fprintf(stderr, "%s: Compile Error: 'FOR' without a matching 'NEXT'\n", basic_get_current_filename());
        fatal_abort();
    }
}

void basic_emit_halt(void) {
    emit(OP_HALT, 0);
}
