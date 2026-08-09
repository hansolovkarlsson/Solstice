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

// Packs (levels_up, slot) into one Instruction.arg for OP_LOAD_ENCLOSING/
// OP_STORE_ENCLOSING/OP_PUSH_ENCLOSING_REF - see their comments in
// common.h's Opcode section. Only ever called with levels_up >= 1 (the
// levels_up == 0 case always uses the plain, non-"enclosing" opcode
// instead - see emit_load_local()/emit_store_local()/
// emit_push_local_ref() below).
static int pack_enclosing(int levels_up, int slot) {
    return (levels_up << 12) | slot;
}

// A local/parameter access node's ->op holds levels_up (0 = this
// procedure's own frame, N >= 1 = walk the static-link chain N times) -
// see the parser.c comment above find_local_outward(). These three
// helpers pick the plain LOCAL opcode or its ENCLOSING-chain-walking
// counterpart accordingly, so every affected NODE_LOCAL_VAR/
// NODE_LOCAL_ASSIGN/NODE_LOCAL_VAR_REF/NODE_VAR_PARAM_READ/
// NODE_VAR_PARAM_ASSIGN/NODE_REF_ARRAY_ACCESS(_2D/_ND)/
// NODE_REF_ARRAY_ASSIGN(_2D/_ND)/NODE_LOCAL_STRING_INDEX(_ASSIGN) case
// below can share one three-line decision instead of repeating it.
static void emit_load_local(int levels_up, int slot) {
    if (levels_up == 0) emit(OP_LOAD_LOCAL, slot);
    else emit(OP_LOAD_ENCLOSING, pack_enclosing(levels_up, slot));
}
static void emit_store_local(int levels_up, int slot) {
    if (levels_up == 0) emit(OP_STORE_LOCAL, slot);
    else emit(OP_STORE_ENCLOSING, pack_enclosing(levels_up, slot));
}
static void emit_push_local_ref(int levels_up, int slot) {
    if (levels_up == 0) emit(OP_PUSH_LOCAL_REF, slot);
    else emit(OP_PUSH_ENCLOSING_REF, pack_enclosing(levels_up, slot));
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
// ordinal, rather than nothing at all. file_idx = -1 means stdout
// (OP_PRINT_STR/OP_PRINT); otherwise the target file variable's
// sym_table index, using the OP_..._FILE siblings instead.
static void emit_enum_print_chain(DataType t, int file_idx) {
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
        emit(file_idx == -1 ? OP_PRINT_STR : OP_PRINT_STR_FILE, file_idx == -1 ? 0 : file_idx);
        done_jmp_idx[i] = code_idx;
        emit(OP_JMP, 0); // patched below: past the whole chain
        code[jz_idx].arg = code_idx; // next value's check starts here
    }
    emit(file_idx == -1 ? OP_PRINT : OP_PRINT_FILE, file_idx == -1 ? 0 : file_idx); // fallback: no match, print the raw ordinal
    for (int i = 0; i < et->value_count; i++) {
        code[done_jmp_idx[i]].arg = code_idx;
    }
}

// Emits stdout_op (arg 0) if file_idx == -1, else file_op with arg =
// file_idx - the small dispatch every write/writeln print opcode choice
// needs, now that each one has a file-writing sibling (see the "File
// I/O" section of common.h's Opcode enum). Also reused by 'eof'/'eoln'
// below, which need the exact same stdin-vs-file dispatch.
static void emit_stdio_op(Opcode stdout_op, Opcode file_op, int file_idx) {
    if (file_idx == -1) emit(stdout_op, 0);
    else emit(file_op, file_idx);
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

// proc_table[] index of whichever procedure/function generate_code() is
// currently emitting bytecode for, or -1 while generating the main
// program's own body. Set once per iteration by generate_program()'s
// per-procedure loop - see there. Used only by emit_static_link_for_call()
// below, to classify a call site's relationship to its own target.
static int codegen_current_proc_idx = -1;

// Emits whatever OP_PUSH_STATIC_LINK a call to target_proc_idx needs
// (nothing at all, if the target isn't a nested procedure), right before
// record_call()'s own OP_CALL. Four cases, matching OP_PUSH_STATIC_LINK's
// own comment in common.h:
//   1. target isn't nested (lexical_parent_idx == -1) - no static link.
//   2. target's lexical parent IS the procedure currently being
//      compiled - arg 0 ("push my own current fp").
//   3. target's lexical parent is a proper ancestor of the procedure
//      currently being compiled - arg N, the number of hops up MY OWN
//      lexical_parent_idx chain to reach it (which mirrors, at compile
//      time, exactly the runtime hop count OP_PUSH_STATIC_LINK's own
//      chain walk performs over vm_static_link[] at the call site).
//   4. neither of the above - the flat-namespace escape hatch (nested
//      procedure names stay callable from anywhere, not just their own
//      lexical scope) means there's no live ancestor activation to point
//      at here. arg -1, the sentinel: compiles fine, only traps at
//      runtime if the callee actually touches an enclosing local.
static void emit_static_link_for_call(int target_proc_idx) {
    int target_parent = proc_table[target_proc_idx].lexical_parent_idx;
    if (target_parent == -1) {
        return; // case 1
    }
    int walk = codegen_current_proc_idx;
    int hops = 0;
    while (walk != -1) {
        if (walk == target_parent) {
            emit(OP_PUSH_STATIC_LINK, hops); // case 2 (hops == 0) or case 3 (hops >= 1)
            return;
        }
        walk = proc_table[walk].lexical_parent_idx;
        hops++;
    }
    emit(OP_PUSH_STATIC_LINK, -1); // case 4
}

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

// Same backpatch idea as pending_calls[] above, for NODE_PROC_REF
// (passing a top-level procedure/function BY NAME as an actual argument
// for a procedural/functional parameter - see common.h) - the only
// difference is WHICH instruction gets patched: a PUSH's arg (the
// procedure's runtime "value" - its entry address, pushed as ordinary
// data) instead of a CALL's own jump target.
#define MAX_PENDING_PROC_REFS 100

typedef struct {
    int push_instr_idx;  // index into code[] of the PUSH instruction
    int target_proc_idx; // which procedure's entry_address it needs
} PendingProcRef;

static PendingProcRef pending_proc_refs[MAX_PENDING_PROC_REFS];
static int pending_proc_ref_count = 0;

static void record_proc_ref(int target_proc_idx) {
    if (pending_proc_ref_count >= MAX_PENDING_PROC_REFS) {
        fprintf(stderr, "%s: Compile Error: Too many procedural/functional parameter arguments (limit is %d)\n",
                get_current_filename(), MAX_PENDING_PROC_REFS);
        fatal_abort();
    }
    pending_proc_refs[pending_proc_ref_count].push_instr_idx = code_idx;
    pending_proc_refs[pending_proc_ref_count].target_proc_idx = target_proc_idx;
    pending_proc_ref_count++;
    emit(OP_PUSH, 0); // placeholder, patched once generate_program() finishes
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

        case NODE_WRITELN: {
            int file_idx = node->extra ? node->extra->data.var_idx : -1; // -1 = stdout
            for (ASTNode *arg = node->left; arg; arg = arg->next) {
                generate_code(arg->left);
                DataType t = arg->left->expression_type;
                if (arg->right) {
                    generate_code(arg->right); // width
                    if (arg->extra) {
                        generate_code(arg->extra); // precision (real only, guaranteed by the type checker)
                        emit_stdio_op(OP_FPRINT_PADDED_PRECISE, OP_FPRINT_PADDED_PRECISE_FILE, file_idx);
                    } else if (is_string_type(t)) {
                        emit_stdio_op(OP_PRINT_STR_PADDED, OP_PRINT_STR_PADDED_FILE, file_idx);
                    } else if (t == TYPE_BOOLEAN) {
                        emit_stdio_op(OP_PRINT_BOOL_PADDED, OP_PRINT_BOOL_PADDED_FILE, file_idx);
                    } else if (t == TYPE_REAL) {
                        emit_stdio_op(OP_FPRINT_PADDED, OP_FPRINT_PADDED_FILE, file_idx);
                    } else {
                        emit_stdio_op(OP_PRINT_PADDED, OP_PRINT_PADDED_FILE, file_idx);
                    }
                } else {
                    if (is_string_type(t)) emit_stdio_op(OP_PRINT_STR, OP_PRINT_STR_FILE, file_idx);
                    else if (t == TYPE_BOOLEAN) emit_stdio_op(OP_PRINT_BOOL, OP_PRINT_BOOL_FILE, file_idx);
                    else if (t == TYPE_REAL) emit_stdio_op(OP_FPRINT, OP_FPRINT_FILE, file_idx);
                    else if (t >= TYPE_ENUM_BASE && t < TYPE_POINTER_BASE) emit_enum_print_chain(t, file_idx);
                    else emit_stdio_op(OP_PRINT, OP_PRINT_FILE, file_idx);
                }
            }
            if (node->op == TOKEN_WRITELN) emit_stdio_op(OP_NEWLINE, OP_NEWLINE_FILE, file_idx);
            generate_code(node->next);
            break;
        }

        // node->op is TOKEN_READLN (flush the rest of the input line
        // afterward) or TOKEN_READ (don't) - see parse_read_statement()
        // in parser.c. A string/char target's opcode reads a whole line
        // via fgets() either way, so there's nothing to pick between for
        // those two types. node->extra (if set) is the source file - see
        // OP_READ_FILE's comment in common.h for why its sym_table index
        // is pushed via a plain OP_PUSH first, rather than baked into
        // arg (arg is already the READ TARGET's own index here).
        case NODE_READLN:
            if (node->left) { // see parse_read_statement()'s comment in parser.c
                emit_stdio_op(OP_SKIP_PENDING_NEWLINE, OP_SKIP_PENDING_NEWLINE_FILE, node->extra ? node->extra->data.var_idx : -1);
            }
            if (node->extra) {
                emit(OP_PUSH, node->extra->data.var_idx);
                emit(node->op == TOKEN_READ ? OP_READ_FILE_NOFLUSH : OP_READ_FILE, node->data.var_idx);
            } else {
                emit(node->op == TOKEN_READ ? OP_READ_NOFLUSH : OP_READ, node->data.var_idx);
            }
            generate_code(node->next);
            break;

        case NODE_LOCAL_READLN: {
            int noflush = (node->op == TOKEN_READ);
            if (node->left) { // see parse_read_statement()'s comment in parser.c
                emit_stdio_op(OP_SKIP_PENDING_NEWLINE, OP_SKIP_PENDING_NEWLINE_FILE, node->extra ? node->extra->data.var_idx : -1);
            }
            Opcode op;
            if (node->extra) {
                emit(OP_PUSH, node->extra->data.var_idx);
                if (node->expression_type == TYPE_CHAR) op = OP_READ_FILE_LOCAL_CHAR;
                else if (is_string_type(node->expression_type)) op = OP_READ_FILE_LOCAL_STR;
                else if (node->expression_type == TYPE_BOOLEAN) op = noflush ? OP_READ_FILE_LOCAL_BOOL_NOFLUSH : OP_READ_FILE_LOCAL_BOOL;
                else if (node->expression_type == TYPE_REAL) op = noflush ? OP_READ_FILE_LOCAL_REAL_NOFLUSH : OP_READ_FILE_LOCAL_REAL;
                else op = noflush ? OP_READ_FILE_LOCAL_INT_NOFLUSH : OP_READ_FILE_LOCAL_INT;
            } else {
                if (node->expression_type == TYPE_CHAR) op = OP_READ_LOCAL_CHAR;
                else if (is_string_type(node->expression_type)) op = OP_READ_LOCAL_STR;
                else if (node->expression_type == TYPE_BOOLEAN) op = noflush ? OP_READ_LOCAL_BOOL_NOFLUSH : OP_READ_LOCAL_BOOL;
                else if (node->expression_type == TYPE_REAL) op = noflush ? OP_READ_LOCAL_REAL_NOFLUSH : OP_READ_LOCAL_REAL;
                else op = noflush ? OP_READ_LOCAL_INT_NOFLUSH : OP_READ_LOCAL_INT;
            }
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
            if (node->op == TOKEN_EOF_FN || node->op == TOKEN_EOLN) {
                // Unlike every other builtin here, an eof/eoln file
                // argument (if any) isn't a VALUE to push onto the
                // stack via the generic generate_code(node->left) below -
                // it's a compile-time-known file variable index, baked
                // directly into arg (files are always global - see
                // TYPE_FILE), exactly like every other file opcode.
                // Running generate_code() on it would wrongly push
                // vm_vars[] for that variable (meaningless - a file
                // variable's real state lives in vm_open_files[]) and
                // leave it unbalanced on the stack, so this case returns
                // early instead of falling into the shared code below.
                int file_idx = node->left ? node->left->data.var_idx : -1;
                if (node->op == TOKEN_EOF_FN) emit_stdio_op(OP_EOF, OP_EOF_FILE, file_idx);
                else emit_stdio_op(OP_EOLN, OP_EOLN_FILE, file_idx);
                break;
            }
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
            }
            break;

        case NODE_STRING_INDEX:
            emit(OP_LOAD, node->data.var_idx);
            generate_code(node->left);
            emit(OP_STR_CHAR_AT, 0);
            break;

        case NODE_LOCAL_STRING_INDEX:
            emit_load_local((int)node->op, node->data.var_idx);
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
            emit_load_local((int)node->op, node->data.var_idx);
            generate_code(node->left);
            generate_code(node->right);
            emit(OP_STR_CHAR_REPLACE, 0);
            emit_store_local((int)node->op, node->data.var_idx);
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

        case NODE_ARRAY_RECORD_FIELD_ACCESS:
            generate_code(node->left);                     // index
            emit(OP_PUSH, node->right->data.num_value);    // field offset (compile-time constant)
            emit(OP_LOAD_ARRAY_RECORD_FIELD, node->data.var_idx);
            break;

        case NODE_ARRAY_RECORD_FIELD_ASSIGN:
            generate_code(node->left);                     // index
            emit(OP_PUSH, node->extra->data.num_value);    // field offset (compile-time constant)
            generate_code(node->right);                    // value
            if (node->expression_type == TYPE_CHAR) {
                emit(OP_STORE_ARRAY_RECORD_FIELD_CHAR, node->data.var_idx);
            } else {
                emit(OP_STORE_ARRAY_RECORD_FIELD, node->data.var_idx);
            }
            generate_code(node->next);
            break;

        case NODE_HEAP_FIELD_ACCESS:
            generate_code(node->left);                  // the pointer value
            emit(OP_LOAD_HEAP_FIELD, node->right->data.num_value); // field offset (compile-time constant)
            break;

        case NODE_HEAP_FIELD_ASSIGN:
            generate_code(node->left);                  // the pointer value
            generate_code(node->right);                 // value
            if (node->expression_type == TYPE_CHAR) {
                emit(OP_STORE_HEAP_FIELD_CHAR, node->extra->data.num_value);
            } else {
                emit(OP_STORE_HEAP_FIELD, node->extra->data.num_value);
            }
            generate_code(node->next);
            break;

        case NODE_HEAP_ARRAY_FIELD_ACCESS:
            generate_code(node->left);                  // base pointer
            generate_code(node->right);                 // index (already range-checked by the parser)
            emit(OP_LOAD_HEAP_ARRAY_FIELD, node->data.num_value); // combined offset (field base offset - array lower bound)
            break;

        case NODE_HEAP_ARRAY_FIELD_ASSIGN:
            generate_code(node->left);                  // base pointer
            generate_code(node->extra);                 // index (range-checked)
            generate_code(node->right);                 // value
            if (node->expression_type == TYPE_CHAR) {
                emit(OP_STORE_HEAP_ARRAY_FIELD_CHAR, node->data.num_value);
            } else {
                emit(OP_STORE_HEAP_ARRAY_FIELD, node->data.num_value);
            }
            generate_code(node->next);
            break;

        case NODE_HEAP_ALLOC:
            emit(OP_NEW, node->data.num_value);
            break;

        case NODE_HEAP_DISPOSE:
            generate_code(node->left);
            emit(OP_DISPOSE, node->data.num_value);
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
            emit_static_link_for_call(node->data.var_idx);
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

        case NODE_PROC_REF:
            // Pushes the target procedure's entry address as ordinary
            // data - a value, not a jump - so a further OP_CALL_INDIRECT
            // (inside whichever procedure receives it) can jump there
            // later. Backpatched exactly like record_call()'s own
            // pending_calls[], since entry_address isn't known yet if
            // the target is forward-declared or simply not yet generated.
            record_proc_ref(node->data.var_idx);
            break;

        case NODE_CALL_INDIRECT:
            for (ASTNode *arg = node->left; arg; arg = arg->next) {
                generate_code(arg);
            }
            emit_load_local((int)node->op, node->data.var_idx); // push the target address, on top of the args
            emit(OP_CALL_INDIRECT, 0);
            if (node->extra->data.num_value) {
                // Statement context (mirrors NODE_CALL's own op ==
                // TOKEN_PROCEDURE case above) - discard an unused
                // function result, then continue the enclosing statement
                // chain. expression_type is TYPE_UNKNOWN for a procedure-
                // parameter call, exactly like an ordinary statement-
                // context NODE_CALL to a plain procedure never sets it
                // either - see NODE_CALL_INDIRECT's comment in common.h.
                if (node->expression_type != TYPE_UNKNOWN) {
                    emit(OP_POP, 0);
                }
                generate_code(node->next);
            }
            break;

        case NODE_PROCVAR_CALL:
            // Same shape as NODE_CALL_INDIRECT just above, except the
            // callee's address comes from an arbitrary already-built
            // expression (node->right - a NODE_VARIABLE/NODE_LOCAL_VAR/
            // NODE_VAR_PARAM_READ reading a NAMED procedural-type
            // value) instead of always a local frame slot - see
            // NODE_PROCVAR_CALL's own comment in common.h.
            for (ASTNode *arg = node->left; arg; arg = arg->next) {
                generate_code(arg);
            }
            generate_code(node->right); // push the target address, on top of the args
            emit(OP_CALL_INDIRECT, 0);
            if (node->extra->data.num_value) {
                if (node->expression_type != TYPE_UNKNOWN) {
                    emit(OP_POP, 0);
                }
                generate_code(node->next);
            }
            break;

        case NODE_VIRTUAL_CALL:
            // 'self' must end up as the BOTTOM-most pushed value (the
            // callee's own reverse-order STORE_LOCAL parameter-unpacking
            // consumes it last), but OP_CALL_INDIRECT needs the target
            // address on TOP. Resolved by computing the target right
            // after pushing self (leaving [self, target]), then, for each
            // arg, pushing it and swapping it past the target - which
            // keeps the target on top after every arg without ever
            // disturbing self underneath. E.g. two args:
            // [self, target] -(push arg1)-> [self, target, arg1]
            //                 -(SWAP)------> [self, arg1, target]
            //                 -(push arg2)-> [self, arg1, target, arg2]
            //                 -(SWAP)------> [self, arg1, arg2, target]
            generate_code(node->left);       // [self]
            emit(OP_DUP, 0);                 // [self, self]
            emit(OP_LOAD_HEAP_FIELD, 0);     // [self, tag] - offset 0 is always the hidden runtime type tag
            emit(OP_LOAD_VTABLE_SLOT, node->data.num_value); // [self, target]
            for (ASTNode *arg = node->right; arg; arg = arg->next) {
                generate_code(arg);
                emit(OP_SWAP, 0);
            }
            emit(OP_CALL_INDIRECT, 0);
            if (node->op == TOKEN_PROCEDURE) {
                // Statement context: continue the enclosing statement
                // chain, discarding an unused function result first -
                // mirrors NODE_CALL's own op == TOKEN_PROCEDURE case.
                if (node->expression_type != TYPE_UNKNOWN) {
                    emit(OP_POP, 0);
                }
                generate_code(node->next);
            }
            break;

        case NODE_INHERITED_CALL:
            // Unlike NODE_VIRTUAL_CALL, the target is already fully
            // resolved at compile time (see parse_inherited_call() in
            // parser.c) - no vtable slot, no dynamically-computed
            // target address to keep on top of the stack, so self and
            // the args just push in the callee's own expected order
            // directly (self first/bottom-most, same requirement as
            // NODE_VIRTUAL_CALL's, just without needing the SWAP dance
            // that's only there to keep an indirect target on top).
            generate_code(node->left); // [self]
            for (ASTNode *arg = node->right; arg; arg = arg->next) {
                generate_code(arg);
            }
            emit_static_link_for_call(node->data.var_idx); // no-op in
                                     // practice - class methods are
                                     // always top-level - but mirrors
                                     // every other direct-OP_CALL site
            record_call(node->data.var_idx); // emits OP_CALL, backpatched
                                     // exactly like NODE_CALL's own
            if (node->op == TOKEN_PROCEDURE) {
                if (proc_table[node->data.var_idx].is_function) {
                    emit(OP_POP, 0); // statement-context call to a function: discard the unused result
                }
                generate_code(node->next);
            }
            break;

        case NODE_VTABLE_INIT_ENTRY:
            generate_code(node->left); // a NODE_PROC_REF: pushes the implementing procedure's entry address
            emit(OP_STORE_VTABLE_SLOT, node->data.num_value);
            generate_code(node->next);
            break;

        case NODE_LOCAL_VAR:
            emit_load_local((int)node->op, node->data.var_idx);
            break;

        case NODE_LOCAL_ASSIGN:
            generate_code(node->left);
            emit_store_local((int)node->op, node->data.var_idx);
            generate_code(node->next);
            break;

        case NODE_REF_ARRAY_ACCESS:
            emit_load_local((int)node->op, node->data.var_idx); // the runtime array reference (sym_table index)
            generate_code(node->left);               // the runtime index
            emit(OP_LOAD_IDX_DYN, 0);
            break;

        case NODE_REF_ARRAY_ASSIGN:
            emit_load_local((int)node->op, node->data.var_idx); // the runtime array reference
            generate_code(node->left);               // the runtime index
            generate_code(node->right);               // the value
            emit(OP_STORE_IDX_DYN, 0);
            generate_code(node->next);
            break;

        case NODE_REF_ARRAY_ACCESS_2D:
            emit_load_local((int)node->op, node->data.var_idx); // the runtime array reference (sym_table index)
            generate_code(node->left);  // first index
            generate_code(node->right); // second index
            emit(OP_LOAD_IDX2D_DYN, 0);
            break;

        case NODE_REF_ARRAY_ASSIGN_2D:
            emit_load_local((int)node->op, node->data.var_idx); // the runtime array reference
            generate_code(node->left);  // first index
            generate_code(node->right); // second index
            generate_code(node->extra); // the value
            emit(OP_STORE_IDX2D_DYN, 0);
            generate_code(node->next);
            break;

        case NODE_REF_ARRAY_ACCESS_ND: {
            emit_load_local((int)node->op, node->data.var_idx); // the runtime array reference (sym_table index)
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
            emit_load_local((int)node->op, node->data.var_idx); // the runtime array reference
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

        case NODE_FILE_OP:
            if (node->op == TOKEN_FILE_ASSIGN) {
                generate_code(node->left); // filename
                emit(OP_FILE_ASSIGN, node->data.var_idx);
            } else if (node->op == TOKEN_RESET) {
                emit(OP_FILE_RESET, node->data.var_idx);
            } else if (node->op == TOKEN_REWRITE) {
                emit(OP_FILE_REWRITE, node->data.var_idx);
            } else { // TOKEN_CLOSE
                emit(OP_FILE_CLOSE, node->data.var_idx);
            }
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
            emit_push_local_ref((int)node->op, node->data.var_idx);
            break;

        case NODE_VAR_PARAM_READ:
            emit_load_local((int)node->op, node->data.var_idx); // the reference
            emit(OP_LOAD_REF, 0);
            break;

        case NODE_VAR_PARAM_ASSIGN:
            emit_load_local((int)node->op, node->data.var_idx); // the reference
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
    pending_proc_ref_count = 0;
    // Whatever file is "current" when codegen starts is the main
    // program's own path (pascalc.c resets it there right before calling
    // this) - saved so a unit-declared proc's own source_file (see the
    // loop below) can be restored back to it once done with that proc.
    const char *main_filename = get_current_filename();

    if (proc_count > 0) {
        int jmp_idx = code_idx;
        emit(OP_JMP, 0); // placeholder, patched below

        for (int i = 0; i < proc_count; i++) {
            set_current_filename(proc_table[i].source_file);
            codegen_current_proc_idx = i;
            proc_table[i].entry_address = code_idx;
            // A nested procedure needs its own distinct fp (established
            // only by OP_ENTER - see vm.c's OP_CALL, which deliberately
            // leaves fp pointing at the CALLER's frame until OP_ENTER
            // runs) so OP_POP_STATIC_LINK below has somewhere correct to
            // record its static link, even if it happens to declare zero
            // of its own params/locals (a plain "procedure Bump; begin
            // outerX := outerX + 1; end;" is exactly this case). Without
            // this, OP_POP_STATIC_LINK would write into the CALLER's own
            // fp slot instead, corrupting the caller's static link.
            if (proc_table[i].local_count > 0 || proc_table[i].lexical_parent_idx != -1) {
                emit(OP_ENTER, proc_table[i].local_count);
            }
            if (proc_table[i].lexical_parent_idx != -1) {
                emit(OP_POP_STATIC_LINK, 0);
            }
            // Caller pushed arguments left-to-right (a record argument as
            // N flattened field values - see parse_record_argument() in
            // parser.c), so the last one is on top of the operand stack -
            // pop into the last parameter SLOT first, working backwards to
            // slot 0. param_slot_count, not param_count: a record
            // parameter occupies multiple slots (one per field) but is
            // still just one syntactic parameter. The static link, if any,
            // was pushed by the CALLER LAST - after every real argument,
            // right before OP_CALL (see emit_static_link_for_call()'s own
            // call site) - so it's the first thing popped here, via
            // OP_POP_STATIC_LINK just above, before this loop even starts.
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
    set_current_filename(main_filename);
    codegen_current_proc_idx = -1;
    generate_block(main_body);

    for (int i = 0; i < pending_call_count; i++) {
        code[pending_calls[i].call_instr_idx].arg = proc_table[pending_calls[i].target_proc_idx].entry_address;
    }
    for (int i = 0; i < pending_proc_ref_count; i++) {
        code[pending_proc_refs[i].push_instr_idx].arg = proc_table[pending_proc_refs[i].target_proc_idx].entry_address;
    }
}

void emit_halt(void) {
    emit(OP_HALT, 0);
}

