#include <stdio.h>
#include <stdlib.h>
#include "codegen.h"
#include "parser.h"
#include "error.h"

static void emit(Opcode op, int arg) {
    if (code_idx >= MAX_CODE) {
        fprintf(stderr, "%s: Compile Error: Program exceeds maximum bytecode size (limit is %d instructions)\n",
                get_current_filename(), MAX_CODE);
        fatal_abort();
    }
    code[code_idx].op = op;
    code[code_idx].arg = arg;
    code_idx++;
}

// char and string share the exact same runtime representation (a
// string_pool[] index), so every opcode-selection decision that currently
// checks "is this a string" needs to treat char the same way.
static int is_string_type(DataType t) {
    return t == TYPE_STRING || t == TYPE_CHAR;
}

// Emits an ordering comparison (<, >, <=, >=). For integer operands this
// is just int_op directly. For strings, OP_SCMP first reduces the pair to
// a -1/0/1 result, which int_op then compares against a literal 0 -
// avoids needing four separate string-ordering opcodes.
static void emit_ordering(ASTNode *node, Opcode int_op) {
    if (is_string_type(node->left->expression_type)) {
        emit(OP_SCMP, 0);
        emit(OP_PUSH, 0);
        emit(int_op, 0);
    } else {
        emit(int_op, 0);
    }
}

// Prints an enum value by name ('writeln(Red)' -> "Red"), not its bare
// ordinal - the VM only ever sees an enum value as a plain int (see the
// TYPE_ENUM_BASE comment in common.h), so there's no opcode that knows
// "this int is enum value N of enum type T" at runtime. Instead this
// emits a compile-time-built chain of comparisons: pop nothing yet, DUP
// the value, compare against each possible ordinal in turn, and print
// the matching name via the ordinary string-printing opcodes - no new
// opcode or .bin format change needed, just more bytecode per call site.
// Expects the value already on top of the stack (from generate_code());
// consumes it, leaving the stack exactly as balanced as a plain
// OP_PRINT would. An ordinal that matches no value (only reachable via
// unchecked succ()/pred() arithmetic past an enum's first/last value -
// see NODE_BINARY_OP in type_checker.c) falls back to printing the raw
// ordinal, rather than nothing at all.
static void emit_enum_print_chain(DataType t) {
    EnumTypeDef *et = &enum_types[t - TYPE_ENUM_BASE];
    int done_jmp_idx[MAX_ENUM_VALUES];
    for (int i = 0; i < et->value_count; i++) {
        emit(OP_DUP, 0);
        emit(OP_PUSH, i);
        emit(OP_EQ, 0);
        int jz_idx = code_idx;
        emit(OP_JZ, 0); // patched below: to the next value's check
        emit(OP_POP, 0); // matched - discard the now-unneeded duplicated value
        emit(OP_PUSH_STR, et->value_str_idx[i]);
        emit(OP_PRINT_STR, 0);
        done_jmp_idx[i] = code_idx;
        emit(OP_JMP, 0); // patched below: past the whole chain
        code[jz_idx].arg = code_idx; // next value's check starts here
    }
    emit(OP_PRINT, 0); // fallback: no match, print the raw ordinal
    for (int i = 0; i < et->value_count; i++) {
        code[done_jmp_idx[i]].arg = code_idx;
    }
}

// Allocates a hidden, compiler-generated variable slot (not reachable from
// user code). Used to cache a for-loop's end bound: Pascal evaluates that
// bound once, at loop start, not on every iteration - so if the loop body
// modifies a variable the bound expression depends on, the loop must not
// be affected. Re-emitting the end-expression's code inside the loop
// condition every iteration would get this wrong; caching it here doesn't.
static int add_temp_var(DataType type) {
    if (sym_count >= MAX_SYMBOLS) {
        fprintf(stderr, "%s: Compile Error: Too many variables (limit is %d, including internal loop temporaries)\n",
                get_current_filename(), MAX_SYMBOLS);
        fatal_abort();
    }
    snprintf(sym_table[sym_count].name, MAX_NAME, "__for_tmp%d", sym_count);
    sym_table[sym_count].type = type;
    sym_table[sym_count].is_array = 0;
    sym_table[sym_count].array_lower = 0;
    sym_table[sym_count].array_upper = 0;
    sym_table[sym_count].array_base = 0;
    return sym_count++;
}

// break/continue support: each loop (while/repeat/for) pushes a context
// before generating its body. A break/continue statement inside that body
// emits a JMP placeholder and records its instruction index here, since
// the real target isn't known until the whole loop has been generated
// (continue's target, in particular, is the loop's increment/condition
// step, which comes right *after* the body). Once the loop finishes
// generating, patch_loop() fills in every pending placeholder at once.
#define MAX_LOOP_DEPTH 32
#define MAX_LOOP_JUMPS 64

typedef struct {
    int break_jumps[MAX_LOOP_JUMPS];
    int break_count;
    int continue_jumps[MAX_LOOP_JUMPS];
    int continue_count;
} LoopContext;

static LoopContext loop_stack[MAX_LOOP_DEPTH];
static int loop_depth = 0;

static void push_loop(void) {
    if (loop_depth >= MAX_LOOP_DEPTH) {
        fprintf(stderr, "%s: Compile Error: Loops nested too deeply (limit is %d)\n",
                get_current_filename(), MAX_LOOP_DEPTH);
        fatal_abort();
    }
    loop_stack[loop_depth].break_count = 0;
    loop_stack[loop_depth].continue_count = 0;
    loop_depth++;
}

static void pop_loop(void) {
    loop_depth--;
}

static void record_break(void) {
    LoopContext *lc = &loop_stack[loop_depth - 1];
    if (lc->break_count >= MAX_LOOP_JUMPS) {
        fprintf(stderr, "%s: Compile Error: Too many 'break' statements in one loop (limit is %d)\n",
                get_current_filename(), MAX_LOOP_JUMPS);
        fatal_abort();
    }
    lc->break_jumps[lc->break_count++] = code_idx;
    emit(OP_JMP, 0); // placeholder, patched by patch_loop()
}

static void record_continue(void) {
    LoopContext *lc = &loop_stack[loop_depth - 1];
    if (lc->continue_count >= MAX_LOOP_JUMPS) {
        fprintf(stderr, "%s: Compile Error: Too many 'continue' statements in one loop (limit is %d)\n",
                get_current_filename(), MAX_LOOP_JUMPS);
        fatal_abort();
    }
    lc->continue_jumps[lc->continue_count++] = code_idx;
    emit(OP_JMP, 0); // placeholder, patched by patch_loop()
}

static void patch_loop(int continue_target, int break_target) {
    LoopContext *lc = &loop_stack[loop_depth - 1];
    for (int i = 0; i < lc->continue_count; i++) code[lc->continue_jumps[i]].arg = continue_target;
    for (int i = 0; i < lc->break_count; i++) code[lc->break_jumps[i]].arg = break_target;
}

// goto/label support: one entry per label id seen so far in the CURRENT
// block (main program or procedure/function body - see generate_block()
// below, which resets this table at the start of each one). A NODE_GOTO
// reached before its target NODE_LABEL has been generated (a forward
// goto) can't know the real jump address yet, so it emits a placeholder
// and records its instruction index here; once NODE_LABEL is reached,
// every pending placeholder recorded against that id gets patched to the
// label's actual code_idx - same emit-then-patch idea as break/continue
// above, just keyed by label id instead of "current innermost loop". A
// backward goto (label already generated) skips the placeholder entirely
// and emits the real jump immediately, exactly like NODE_WHILE's jump
// back to its own condition.
#define MAX_LABELS_PER_BLOCK 64
#define MAX_LABEL_PENDING_JUMPS 32

typedef struct {
    int id;
    int code_idx;       // -1 until this label has actually been generated
    int pending_jumps[MAX_LABEL_PENDING_JUMPS];
    int pending_count;
} LabelEntry;

static LabelEntry label_table[MAX_LABELS_PER_BLOCK];
static int label_table_count = 0;

// Finds this label's entry in the current block's table, creating one
// (code_idx == -1, meaning "not generated yet") on first sight - a
// forward goto will always see its target for the first time this way,
// since the label itself hasn't been generated yet.
static int find_or_add_label(int id) {
    for (int i = 0; i < label_table_count; i++) {
        if (label_table[i].id == id) return i;
    }
    if (label_table_count >= MAX_LABELS_PER_BLOCK) {
        fprintf(stderr, "%s: Compile Error: Too many labels in one block (limit is %d)\n",
                get_current_filename(), MAX_LABELS_PER_BLOCK);
        fatal_abort();
    }
    LabelEntry *le = &label_table[label_table_count];
    le->id = id;
    le->code_idx = -1;
    le->pending_count = 0;
    return label_table_count++;
}

// Procedure-call target resolution: forward declarations mean a CALL's
// target entry_address isn't always known yet when the CALL itself is
// generated (procedure A can call forward-declared procedure B before B's
// real body - and therefore its entry_address - has been generated). So
// every CALL is backpatched, the same technique as break/continue above:
// emit a placeholder now, record which procedure it's really calling, and
// fill in the real address in one final pass once every procedure (and
// main) has been generated and every entry_address is therefore known.
#define MAX_PENDING_CALLS 200

typedef struct {
    int call_instr_idx;  // index into code[] of the CALL instruction
    int target_proc_idx; // which procedure it's calling
} PendingCall;

static PendingCall pending_calls[MAX_PENDING_CALLS];
static int pending_call_count = 0;

static void record_call(int target_proc_idx) {
    if (pending_call_count >= MAX_PENDING_CALLS) {
        fprintf(stderr, "%s: Compile Error: Too many procedure calls (limit is %d)\n",
                get_current_filename(), MAX_PENDING_CALLS);
        fatal_abort();
    }
    pending_calls[pending_call_count].call_instr_idx = code_idx;
    pending_calls[pending_call_count].target_proc_idx = target_proc_idx;
    pending_call_count++;
    emit(OP_CALL, 0); // placeholder, patched once generate_program() finishes
}

void generate_code(ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case NODE_COMPOUND:
            generate_code(node->left);
            generate_code(node->next);
            break;

        case NODE_ASSIGN:
            if (sym_table[node->data.var_idx].is_array) {
                generate_code(node->left);   // index
                generate_code(node->right);  // value
                emit(OP_STORE_IDX, node->data.var_idx);
            } else {
                generate_code(node->left);   // value
                emit(OP_STORE, node->data.var_idx);
            }
            generate_code(node->next);
            break;

        case NODE_NUMBER:
        case NODE_BOOLEAN:
            emit(OP_PUSH, node->data.num_value);
            break;

        case NODE_REAL_NUMBER:
            emit(OP_PUSH, node->data.num_value); // the bit pattern, already computed at parse time
            break;

        case NODE_INT_TO_REAL:
            generate_code(node->left);
            emit(OP_INT_TO_REAL, 0);
            break;

        case NODE_ARRAY_REF:
            emit(OP_PUSH, node->data.var_idx);
            break;

        case NODE_VARIABLE:
            emit(OP_LOAD, node->data.var_idx);
            break;

        case NODE_STRING:
            emit(OP_PUSH_STR, node->data.var_idx);
            break;

        case NODE_ARRAY_ACCESS:
            generate_code(node->left);   // index
            emit(OP_LOAD_IDX, node->data.var_idx);
            break;

        case NODE_UNARY_OP:
            generate_code(node->left);
            if (node->op == TOKEN_MINUS) {
                if (node->left->expression_type == TYPE_REAL) emit(OP_FNEG, 0);
                else emit(OP_NEG, 0);
            } else if (node->op == TOKEN_NOT) {
                if (node->left->expression_type == TYPE_INTEGER) emit(OP_BNOT, 0);
                else emit(OP_NOT, 0);
            } else if (node->op == TOKEN_ABS) {
                if (node->left->expression_type == TYPE_REAL) emit(OP_FABS, 0);
                else emit(OP_ABS, 0);
            } else if (node->op == TOKEN_SQR) {
                emit(OP_DUP, 0);
                if (node->left->expression_type == TYPE_REAL) emit(OP_FMUL, 0);
                else emit(OP_MUL, 0);
            } else if (node->op == TOKEN_ORD) {
                emit(OP_ORD, 0);
            } else if (node->op == TOKEN_CHR) {
                emit(OP_CHR, 0);
            } else if (node->op == TOKEN_TRUNC) {
                emit(OP_TRUNC, 0);
            } else if (node->op == TOKEN_ROUND) {
                emit(OP_ROUND, 0);
            } else if (node->op == TOKEN_SQRT) {
                emit(OP_FSQRT, 0);
            } else if (node->op == TOKEN_SIN) {
                emit(OP_FSIN, 0);
            } else if (node->op == TOKEN_COS) {
                emit(OP_FCOS, 0);
            } else if (node->op == TOKEN_ARCTAN) {
                emit(OP_FARCTAN, 0);
            } else if (node->op == TOKEN_EXP) {
                emit(OP_FEXP, 0);
            } else if (node->op == TOKEN_LN) {
                emit(OP_FLN, 0);
            }
            break;

        case NODE_BINARY_OP: {
            // Set subset ('<=')/superset ('>=') tests need a DUP between
            // generating the two operands (A <= B is (A AND B) == A,
            // which needs A available twice - once for the AND, once for
            // the final comparison - and this VM has no stack-shuffling
            // opcode that could produce that from the ordinary
            // [left, right] the generic preamble below leaves on the
            // stack). Handled as a special case, before that preamble
            // runs, rather than restructuring it for every operator.
            // '>=' is the same test with the operands swapped (A >= B is
            // B <= A), so it generates right before left.
            if (node->left->expression_type == TYPE_SET && (node->op == TOKEN_LTE || node->op == TOKEN_GTE)) {
                if (node->op == TOKEN_LTE) {
                    generate_code(node->left);
                    emit(OP_DUP, 0);
                    generate_code(node->right);
                } else {
                    generate_code(node->right);
                    emit(OP_DUP, 0);
                    generate_code(node->left);
                }
                emit(OP_BAND, 0);
                emit(OP_EQ, 0);
                break;
            }
            generate_code(node->left);
            generate_code(node->right);
            int operand_is_real = (node->left->expression_type == TYPE_REAL);
            int operand_is_set = (node->left->expression_type == TYPE_SET);
            switch (node->op) {
                case TOKEN_PLUS:
                    if (is_string_type(node->left->expression_type)) emit(OP_SCONCAT, 0);
                    else if (operand_is_real) emit(OP_FADD, 0);
                    else if (operand_is_set) emit(OP_BOR, 0); // union
                    else emit(OP_ADD, 0);
                    break;
                case TOKEN_MINUS:
                    if (operand_is_real) emit(OP_FSUB, 0);
                    else if (operand_is_set) { emit(OP_BNOT, 0); emit(OP_BAND, 0); } // difference: left AND (NOT right)
                    else emit(OP_SUB, 0);
                    break;
                case TOKEN_MUL:
                    if (operand_is_real) emit(OP_FMUL, 0);
                    else if (operand_is_set) emit(OP_BAND, 0); // intersection
                    else emit(OP_MUL, 0);
                    break;
                case TOKEN_DIV:
                    // '/' - the type checker guarantees both operands are
                    // already real by the time codegen sees this node.
                    emit(OP_FDIV, 0);
                    break;
                case TOKEN_POW:
                    // '**' - same guarantee as '/' above.
                    emit(OP_FPOWER, 0);
                    break;
                case TOKEN_EQ:
                    if (is_string_type(node->left->expression_type)) emit(OP_SEQ, 0);
                    else if (operand_is_real) emit(OP_FEQ, 0);
                    else emit(OP_EQ, 0);
                    break;
                case TOKEN_LT:
                    if (operand_is_real) emit(OP_FLT, 0);
                    else emit_ordering(node, OP_LT);
                    break;
                case TOKEN_GT:
                    if (operand_is_real) emit(OP_FGT, 0);
                    else emit_ordering(node, OP_GT);
                    break;
                case TOKEN_AND:
                    if (node->left->expression_type == TYPE_INTEGER) emit(OP_BAND, 0);
                    else emit(OP_AND, 0);
                    break;
                case TOKEN_OR:
                    if (node->left->expression_type == TYPE_INTEGER) emit(OP_BOR, 0);
                    else emit(OP_OR, 0);
                    break;
                case TOKEN_LTE:
                    if (operand_is_real) emit(OP_FLTE, 0);
                    else emit_ordering(node, OP_LTE);
                    break;
                case TOKEN_GTE:
                    if (operand_is_real) emit(OP_FGTE, 0);
                    else emit_ordering(node, OP_GTE);
                    break;
                case TOKEN_NEQ:
                    if (is_string_type(node->left->expression_type)) { emit(OP_SEQ, 0); emit(OP_NOT, 0); }
                    else if (operand_is_real) emit(OP_FNEQ, 0);
                    else emit(OP_NEQ, 0);
                    break;
                case TOKEN_DIV_KW: emit(OP_DIV, 0); break; // integer-only, guaranteed by the type checker
                case TOKEN_MOD:    emit(OP_MOD, 0); break; // integer-only
                case TOKEN_XOR:
                    if (node->left->expression_type == TYPE_INTEGER) emit(OP_BXOR, 0);
                    else emit(OP_XOR, 0);
                    break;
                case TOKEN_SHL: emit(OP_SHL, 0); break;
                case TOKEN_SHR: emit(OP_SHR, 0); break;
                default: break;
            }
            break;
        }

        case NODE_WRITELN:
            for (ASTNode *arg = node->left; arg; arg = arg->next) {
                generate_code(arg->left);
                DataType t = arg->left->expression_type;
                if (arg->right) {
                    generate_code(arg->right); // width
                    if (arg->extra) {
                        generate_code(arg->extra); // precision (real only, guaranteed by the type checker)
                        emit(OP_FPRINT_PADDED_PRECISE, 0);
                    } else if (is_string_type(t)) {
                        emit(OP_PRINT_STR_PADDED, 0);
                    } else if (t == TYPE_BOOLEAN) {
                        emit(OP_PRINT_BOOL_PADDED, 0);
                    } else if (t == TYPE_REAL) {
                        emit(OP_FPRINT_PADDED, 0);
                    } else {
                        emit(OP_PRINT_PADDED, 0);
                    }
                } else {
                    if (is_string_type(t)) emit(OP_PRINT_STR, 0);
                    else if (t == TYPE_BOOLEAN) emit(OP_PRINT_BOOL, 0);
                    else if (t == TYPE_REAL) emit(OP_FPRINT, 0);
                    else if (t >= TYPE_ENUM_BASE) emit_enum_print_chain(t);
                    else emit(OP_PRINT, 0);
                }
            }
            if (node->op == TOKEN_WRITELN) emit(OP_NEWLINE, 0);
            generate_code(node->next);
            break;

        // node->op is TOKEN_READLN (flush the rest of the input line
        // afterward) or TOKEN_READ (don't) - see parse_read_statement()
        // in parser.c. A string/char target's opcode reads a whole line
        // via fgets() either way, so there's nothing to pick between for
        // those two types.
        case NODE_READLN:
            emit(node->op == TOKEN_READ ? OP_READ_NOFLUSH : OP_READ, node->data.var_idx);
            generate_code(node->next);
            break;

        case NODE_LOCAL_READLN: {
            int noflush = (node->op == TOKEN_READ);
            Opcode op;
            if (node->expression_type == TYPE_CHAR) op = OP_READ_LOCAL_CHAR;
            else if (is_string_type(node->expression_type)) op = OP_READ_LOCAL_STR;
            else if (node->expression_type == TYPE_BOOLEAN) op = noflush ? OP_READ_LOCAL_BOOL_NOFLUSH : OP_READ_LOCAL_BOOL;
            else if (node->expression_type == TYPE_REAL) op = noflush ? OP_READ_LOCAL_REAL_NOFLUSH : OP_READ_LOCAL_REAL;
            else op = noflush ? OP_READ_LOCAL_INT_NOFLUSH : OP_READ_LOCAL_INT;
            emit(op, node->data.var_idx);
            generate_code(node->next);
            break;
        }

        // if <cond> then <then> [else <else>]
        //     <cond>
        //     JZ else_or_end     ; patched below
        //     <then>
        //   [ JMP end            ; only emitted if there's an else, patched below
        //   else_or_end:
        //     <else>
        //   end: ]
        case NODE_IF: {
            generate_code(node->left);        // condition
            int jz_idx = code_idx;
            emit(OP_JZ, 0);                   // placeholder, patched below
            generate_code(node->right);       // then-branch
            if (node->extra) {
                int jmp_idx = code_idx;
                emit(OP_JMP, 0);              // placeholder, patched below
                code[jz_idx].arg = code_idx;  // JZ lands here: start of else
                generate_code(node->extra);   // else-branch
                code[jmp_idx].arg = code_idx; // JMP lands here: past the else
            } else {
                code[jz_idx].arg = code_idx;  // JZ lands here: past the then
            }
            generate_code(node->next);
            break;
        }

        // while <cond> do <body>
        //   loop_start:
        //     <cond>
        //     JZ end             ; patched below
        //     <body>             ; continue -> loop_start, break -> end
        //     JMP loop_start
        //   end:
        case NODE_WHILE: {
            push_loop();
            int loop_start = code_idx;
            generate_code(node->left);        // condition
            int jz_idx = code_idx;
            emit(OP_JZ, 0);                   // placeholder, patched below
            generate_code(node->right);       // body
            emit(OP_JMP, loop_start);
            code[jz_idx].arg = code_idx;      // JZ lands here: past the loop
            patch_loop(loop_start, code_idx); // continue -> re-check cond, break -> past the loop
            pop_loop();
            generate_code(node->next);
            break;
        }

        // repeat <body> until <cond>
        //   loop_start:
        //     <body>             ; continue -> just below (the until-cond)
        //     <cond>
        //     JZ loop_start      ; loop again while cond is still false
        //   end:                ; break -> here
        case NODE_REPEAT: {
            push_loop();
            int loop_start = code_idx;
            generate_code(node->left);        // body (statement chain)
            int continue_target = code_idx;   // the until-condition starts here
            generate_code(node->right);       // until-condition
            emit(OP_JZ, loop_start);
            patch_loop(continue_target, code_idx); // break -> past the loop
            pop_loop();
            generate_code(node->next);
            break;
        }

        // for <var> := <start> to/downto <end> do <body>
        //     <start>
        //     STORE var
        //     <end>
        //     STORE end_tmp        ; cached once - not re-evaluated per iteration
        //   loop_start:
        //     LOAD var
        //     LOAD end_tmp
        //     LTE/GTE              ; var <= end_tmp (to) / var >= end_tmp (downto)
        //     JZ end               ; patched below
        //     <body>               ; continue -> just below (the increment step)
        //     LOAD var
        //     PUSH 1
        //     ADD/SUB              ; ADD for 'to', SUB for 'downto'
        //     STORE var
        //     JMP loop_start
        //   end:                   ; break -> here
        case NODE_FOR: {
            push_loop();
            int loop_var = node->data.var_idx;
            int descending = (node->op == TOKEN_DOWNTO);

            generate_code(node->left);         // start bound
            emit(OP_STORE, loop_var);

            int end_var = add_temp_var(TYPE_INTEGER);
            generate_code(node->right);        // end bound, evaluated once
            emit(OP_STORE, end_var);

            int loop_start = code_idx;
            emit(OP_LOAD, loop_var);
            emit(OP_LOAD, end_var);
            emit(descending ? OP_GTE : OP_LTE, 0);
            int jz_idx = code_idx;
            emit(OP_JZ, 0);                    // placeholder, patched below

            generate_code(node->extra);        // body

            int continue_target = code_idx;    // the increment step starts here

            emit(OP_LOAD, loop_var);
            emit(OP_PUSH, 1);
            emit(descending ? OP_SUB : OP_ADD, 0);
            emit(OP_STORE, loop_var);
            emit(OP_JMP, loop_start);

            code[jz_idx].arg = code_idx;       // JZ lands here: past the loop
            patch_loop(continue_target, code_idx);
            pop_loop();
            generate_code(node->next);
            break;
        }

        case NODE_LOCAL_FOR: {
            push_loop();
            int loop_var = node->data.var_idx;
            int descending = (node->op == TOKEN_DOWNTO);

            generate_code(node->left);         // start bound
            emit(OP_STORE_LOCAL, loop_var);

            // node->right is already a NODE_LOCAL_VAR referencing the
            // hidden end-bound slot - the caching itself was desugared
            // entirely at parse time (see parser.c's NODE_FOR handling),
            // so generating it here just loads the already-cached value;
            // no add_temp_var/extra STORE needed the way NODE_FOR needs
            // above for the global case.
            int loop_start = code_idx;
            emit(OP_LOAD_LOCAL, loop_var);
            generate_code(node->right);
            emit(descending ? OP_GTE : OP_LTE, 0);
            int jz_idx = code_idx;
            emit(OP_JZ, 0);                    // placeholder, patched below

            generate_code(node->extra);        // body

            int continue_target = code_idx;    // the increment step starts here

            emit(OP_LOAD_LOCAL, loop_var);
            emit(OP_PUSH, 1);
            emit(descending ? OP_SUB : OP_ADD, 0);
            emit(OP_STORE_LOCAL, loop_var);
            emit(OP_JMP, loop_start);

            code[jz_idx].arg = code_idx;       // JZ lands here: past the loop
            patch_loop(continue_target, code_idx);
            pop_loop();
            generate_code(node->next);
            break;
        }

        case NODE_BREAK:
            record_break();
            generate_code(node->next);
            break;

        case NODE_CONTINUE:
            record_continue();
            generate_code(node->next);
            break;

        case NODE_LABEL: {
            int idx = find_or_add_label(node->data.num_value);
            label_table[idx].code_idx = code_idx;
            for (int i = 0; i < label_table[idx].pending_count; i++) {
                code[label_table[idx].pending_jumps[i]].arg = code_idx;
            }
            label_table[idx].pending_count = 0;
            generate_code(node->left);
            generate_code(node->next);
            break;
        }

        case NODE_GOTO: {
            int idx = find_or_add_label(node->data.num_value);
            if (label_table[idx].code_idx != -1) {
                emit(OP_JMP, label_table[idx].code_idx); // backward goto - target already known
            } else {
                if (label_table[idx].pending_count >= MAX_LABEL_PENDING_JUMPS) {
                    fprintf(stderr, "%s: Compile Error: Too many 'goto' statements targeting one label (limit is %d)\n",
                            get_current_filename(), MAX_LABEL_PENDING_JUMPS);
                    fatal_abort();
                }
                label_table[idx].pending_jumps[label_table[idx].pending_count++] = code_idx;
                emit(OP_JMP, 0); // placeholder - patched once NODE_LABEL is reached
            }
            generate_code(node->next);
            break;
        }

        case NODE_BUILTIN_CALL:
            generate_code(node->left);
            if (node->op == TOKEN_LENGTH) {
                emit(OP_LENGTH, 0);
            } else if (node->op == TOKEN_UPCASE) {
                emit(OP_UPCASE_CHAR, 0);
            } else if (node->op == TOKEN_UPPERCASE) {
                emit(OP_UPPERCASE_STR, 0);
            } else if (node->op == TOKEN_LOWERCASE) {
                emit(OP_LOWERCASE_STR, 0);
            } else if (node->op == TOKEN_POS) {
                generate_code(node->right);
                emit(OP_POS, 0);
            } else if (node->op == TOKEN_LEFT) {
                generate_code(node->right);
                emit(OP_LEFT, 0);
            } else if (node->op == TOKEN_RIGHT) {
                generate_code(node->right);
                emit(OP_RIGHT, 0);
            } else if (node->op == TOKEN_COPY) {
                generate_code(node->right);
                generate_code(node->extra);
                emit(OP_COPY, 0);
            } else if (node->op == TOKEN_POWER) {
                generate_code(node->right); // exponent
                emit(OP_FPOWER, 0);
            } else if (node->op == TOKEN_EOF_FN) {
                emit(OP_EOF, 0);
            } else if (node->op == TOKEN_EOLN) {
                emit(OP_EOLN, 0);
            }
            break;

        case NODE_STRING_INDEX:
            emit(OP_LOAD, node->data.var_idx);
            generate_code(node->left);
            emit(OP_STR_CHAR_AT, 0);
            break;

        case NODE_LOCAL_STRING_INDEX:
            emit(OP_LOAD_LOCAL, node->data.var_idx);
            generate_code(node->left);
            emit(OP_STR_CHAR_AT, 0);
            break;

        case NODE_STRING_INDEX_ASSIGN:
            emit(OP_LOAD, node->data.var_idx);   // old value
            generate_code(node->left);            // index
            generate_code(node->right);           // new character
            emit(OP_STR_CHAR_REPLACE, 0);
            emit(OP_STORE, node->data.var_idx);
            generate_code(node->next);
            break;

        case NODE_LOCAL_STRING_INDEX_ASSIGN:
            emit(OP_LOAD_LOCAL, node->data.var_idx);
            generate_code(node->left);
            generate_code(node->right);
            emit(OP_STR_CHAR_REPLACE, 0);
            emit(OP_STORE_LOCAL, node->data.var_idx);
            generate_code(node->next);
            break;

        case NODE_ARRAY_ACCESS_2D:
            generate_code(node->left);  // first index
            generate_code(node->right); // second index
            emit(OP_LOAD_IDX2D, node->data.var_idx);
            break;

        case NODE_ARRAY_ASSIGN_2D:
            generate_code(node->left);  // first index
            generate_code(node->right); // second index
            generate_code(node->extra); // value
            emit(OP_STORE_IDX2D, node->data.var_idx);
            generate_code(node->next);
            break;

        case NODE_ARRAY_ACCESS_ND:
            for (ASTNode *idx = node->left; idx; idx = idx->next) {
                generate_code(idx);
            }
            emit(OP_LOAD_IDXND, node->data.var_idx);
            break;

        case NODE_ARRAY_ASSIGN_ND:
            for (ASTNode *idx = node->left; idx; idx = idx->next) {
                generate_code(idx);
            }
            generate_code(node->right); // value
            emit(OP_STORE_IDXND, node->data.var_idx);
            generate_code(node->next);
            break;

        case NODE_WRITE_ARG:
            // Structurally unreachable: NODE_WRITELN's own case unwraps
            // a NODE_WRITE_ARG's left/right/extra directly and never
            // calls generate_code() on the wrapper node itself. Listed
            // explicitly anyway, matching this switch's "every node type
            // is handled" convention.
            break;

        case NODE_CALL:
            for (ASTNode *arg = node->left; arg; arg = arg->next) {
                generate_code(arg);
            }
            record_call(node->data.var_idx);
            if (node->op == TOKEN_PROCEDURE) {
                // Statement context: continue the enclosing statement
                // chain, discarding an unused function result first.
                if (proc_table[node->data.var_idx].is_function) {
                    emit(OP_POP, 0); // statement-context call to a function: discard the unused result
                }
                generate_code(node->next);
            }
            // Expression context (node->op left unset): node->next, if
            // set at all, belongs to an enclosing argument list (write/
            // writeln's, or another call's) - that list's own loop above
            // already walks it, so this call must not walk it again.
            break;

        case NODE_LOCAL_VAR:
            emit(OP_LOAD_LOCAL, node->data.var_idx);
            break;

        case NODE_LOCAL_ASSIGN:
            generate_code(node->left);
            emit(OP_STORE_LOCAL, node->data.var_idx);
            generate_code(node->next);
            break;

        case NODE_REF_ARRAY_ACCESS:
            emit(OP_LOAD_LOCAL, node->data.var_idx); // the runtime array reference (sym_table index)
            generate_code(node->left);               // the runtime index
            emit(OP_LOAD_IDX_DYN, 0);
            break;

        case NODE_REF_ARRAY_ASSIGN:
            emit(OP_LOAD_LOCAL, node->data.var_idx); // the runtime array reference
            generate_code(node->left);               // the runtime index
            generate_code(node->right);               // the value
            emit(OP_STORE_IDX_DYN, 0);
            generate_code(node->next);
            break;

        case NODE_REF_ARRAY_ACCESS_2D:
            emit(OP_LOAD_LOCAL, node->data.var_idx); // the runtime array reference (sym_table index)
            generate_code(node->left);  // first index
            generate_code(node->right); // second index
            emit(OP_LOAD_IDX2D_DYN, 0);
            break;

        case NODE_REF_ARRAY_ASSIGN_2D:
            emit(OP_LOAD_LOCAL, node->data.var_idx); // the runtime array reference
            generate_code(node->left);  // first index
            generate_code(node->right); // second index
            generate_code(node->extra); // the value
            emit(OP_STORE_IDX2D_DYN, 0);
            generate_code(node->next);
            break;

        case NODE_REF_ARRAY_ACCESS_ND: {
            emit(OP_LOAD_LOCAL, node->data.var_idx); // the runtime array reference (sym_table index)
            int dims = 0;
            for (ASTNode *idx = node->left; idx; idx = idx->next) {
                generate_code(idx);
                dims++;
            }
            // arg = dims, NOT the array reference - see OP_LOAD_IDXND_DYN's
            // comment in common.h: unlike the array reference itself,
            // dimension count is always a fixed, compile-time-known
            // property of this PARAMETER's declared shape.
            emit(OP_LOAD_IDXND_DYN, dims);
            break;
        }

        case NODE_REF_ARRAY_ASSIGN_ND: {
            emit(OP_LOAD_LOCAL, node->data.var_idx); // the runtime array reference
            int dims = 0;
            for (ASTNode *idx = node->left; idx; idx = idx->next) {
                generate_code(idx);
                dims++;
            }
            generate_code(node->right); // the value
            emit(OP_STORE_IDXND_DYN, dims);
            generate_code(node->next);
            break;
        }

        case NODE_RANGE_CHECK:
            generate_code(node->left); // the value - left on the stack afterward
            emit(OP_CHECK_LOWER, node->right->data.num_value);
            emit(OP_CHECK_UPPER, node->extra->data.num_value);
            break;

        case NODE_ASSERT:
            generate_code(node->left);  // condition
            generate_code(node->right); // message
            emit(OP_ASSERT, 0);
            generate_code(node->next);
            break;

        // case <selector> of
        //     label1[, label2...]: <stmt1>;
        //     ...
        //   [else <stmtN>]
        //   end
        //     <selector>
        //     STORE sel_var          ; cached once - see add_temp_var()
        //   arm1:
        //     LOAD sel_var
        //     PUSH/PUSH_STR label1
        //     EQ/SEQ
        //   [ LOAD sel_var
        //     PUSH/PUSH_STR label2
        //     EQ/SEQ
        //     OR                     ; once per extra label sharing this arm ]
        //     JZ arm2                ; patched below
        //     <stmt1>
        //     JMP end                ; patched below
        //   arm2:
        //     ...
        //   end:                     ; falls straight through to here if no
        //                            ; arm matched and there's no else (see
        //                            ; the OP_ASSERT fallback below) - or
        //                            ; lands here via the else/last arm's JMP
        case NODE_CASE: {
            generate_code(node->left); // selector
            int sel_var = add_temp_var(node->left->expression_type);
            emit(OP_STORE, sel_var);
            // == TYPE_CHAR, not is_string_type(): a 'string' selector is
            // already rejected by type_checker.c before codegen ever
            // runs, but this stays explicit rather than relying on that.
            int sel_is_char = (node->left->expression_type == TYPE_CHAR);

            int end_jmp_idx[MAX_CASE_LABELS];
            int end_jmp_count = 0;

            for (ASTNode *arm = node->right; arm; arm = arm->next) {
                int label_count = 0;
                for (ASTNode *label = arm->left; label; label = label->next) {
                    emit(OP_LOAD, sel_var);
                    generate_code(label);
                    emit(sel_is_char ? OP_SEQ : OP_EQ, 0);
                    if (label_count > 0) emit(OP_OR, 0);
                    label_count++;
                }
                int jz_idx = code_idx;
                emit(OP_JZ, 0); // placeholder, patched below: next arm's checks

                generate_code(arm->right); // this arm's statement

                end_jmp_idx[end_jmp_count++] = code_idx;
                emit(OP_JMP, 0); // placeholder, patched below: past the whole case

                code[jz_idx].arg = code_idx; // JZ lands here: next arm's checks
            }

            if (node->extra) {
                generate_code(node->extra); // else-branch
            } else {
                // No matching label and no else clause: an unconditional
                // runtime error, reusing OP_ASSERT (an always-false
                // condition) rather than adding a dedicated opcode.
                emit(OP_PUSH, 0);
                emit(OP_PUSH_STR, node->data.var_idx);
                emit(OP_ASSERT, 0);
            }

            for (int i = 0; i < end_jmp_count; i++) {
                code[end_jmp_idx[i]].arg = code_idx; // every arm's JMP lands here
            }

            generate_code(node->next);
            break;
        }

        case NODE_CASE_ARM:
            // Never reached via generate_code()'s ordinary dispatch -
            // NODE_CASE's own case above walks each arm directly (label
            // comparisons need custom per-arm codegen, not the single
            // generate_code(child) call every other node type gets), so
            // this case exists only to satisfy -Wswitch's exhaustiveness
            // check.
            break;

        case NODE_VAR_REF:
            emit(OP_PUSH, node->data.var_idx);
            break;

        case NODE_LOCAL_VAR_REF:
            emit(OP_PUSH_LOCAL_REF, node->data.var_idx);
            break;

        case NODE_VAR_PARAM_READ:
            emit(OP_LOAD_LOCAL, node->data.var_idx); // the reference
            emit(OP_LOAD_REF, 0);
            break;

        case NODE_VAR_PARAM_ASSIGN:
            emit(OP_LOAD_LOCAL, node->data.var_idx); // the reference
            generate_code(node->left);                // the value
            emit(OP_STORE_REF, 0);
            generate_code(node->next);
            break;

        case NODE_SET_CONSTRUCTOR:
            emit(OP_PUSH, 0); // empty-set accumulator
            for (ASTNode *elem = node->left; elem; elem = elem->next) {
                emit(OP_PUSH, 1);
                generate_code(elem);
                emit(OP_SHL, 0);  // 1 << element
                emit(OP_BOR, 0);  // fold into the accumulator
            }
            break;

        case NODE_SET_IN:
            emit(OP_PUSH, 1);
            generate_code(node->left); // the element
            emit(OP_SHL, 0);           // 1 << element - OP_SHL's own
                                       // existing bounds check (0..31)
                                       // is what catches an out-of-range
                                       // element at runtime; no separate
                                       // check needed here
            generate_code(node->right); // the set
            emit(OP_BAND, 0);
            emit(OP_PUSH, 0);
            emit(OP_NEQ, 0);           // != 0 -> boolean
            break;
    }
}

// Emits every procedure body first (each preceded by one shared JMP that
// skips straight to the main program), then the main program itself.
//
//     JMP main_start        ; only emitted if there's at least one procedure
//   proc0:
//     ENTER local_count     ; only if proc0 has any locals/parameters
//     STORE_LOCAL k, ..., 0 ; unpack parameters off the operand stack
//     <proc0 body>
//     RET
//   proc1:
//     ...
//   main_start:
//     <main body>
//
// A procedure only becomes callable (findable by find_proc()) once its
// name has been registered - as soon as parsing starts on it (self-
// recursion), or, with 'forward', as soon as the forward declaration is
// parsed (calls before either point are correctly rejected as unknown
// identifiers). But its entry_address isn't necessarily known yet at the
// point some other procedure calls it - forward declarations specifically
// exist to let procedure A call procedure B before B's real body appears
// in the source. So every CALL target is backpatched (record_call()
// above), resolved in the final pass below once every procedure's
// entry_address is known.
// Generates one whole block's code (a procedure/function body, or the
// main program body) - resetting the goto/label table first, since each
// block has its own independent label namespace and generate_code()
// itself is called recursively for every node, not just at block
// boundaries, so it can't do this reset itself.
static void generate_block(ASTNode *body) {
    label_table_count = 0;
    generate_code(body);
}

void generate_program(ASTNode *main_body) {
    pending_call_count = 0;

    if (proc_count > 0) {
        int jmp_idx = code_idx;
        emit(OP_JMP, 0); // placeholder, patched below

        for (int i = 0; i < proc_count; i++) {
            proc_table[i].entry_address = code_idx;
            if (proc_table[i].local_count > 0) {
                emit(OP_ENTER, proc_table[i].local_count);
            }
            // Caller pushed arguments left-to-right (a record argument as
            // N flattened field values - see parse_record_argument() in
            // parser.c), so the last one is on top of the operand stack -
            // pop into the last parameter SLOT first, working backwards to
            // slot 0. param_slot_count, not param_count: a record
            // parameter occupies multiple slots (one per field) but is
            // still just one syntactic parameter.
            for (int p = proc_table[i].param_slot_count - 1; p >= 0; p--) {
                emit(OP_STORE_LOCAL, p);
            }
            generate_block(proc_table[i].body);
            if (proc_table[i].is_function) {
                emit(OP_LOAD_LOCAL, proc_table[i].return_slot);
            }
            emit(OP_RET, 0);
        }

        code[jmp_idx].arg = code_idx; // main starts here
    }
    generate_block(main_body);

    for (int i = 0; i < pending_call_count; i++) {
        code[pending_calls[i].call_instr_idx].arg = proc_table[pending_calls[i].target_proc_idx].entry_address;
    }
}

void emit_halt(void) {
    emit(OP_HALT, 0);
}

