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
                emit(OP_NEG, 0);
            } else if (node->op == TOKEN_NOT) {
                if (node->left->expression_type == TYPE_INTEGER) emit(OP_BNOT, 0);
                else emit(OP_NOT, 0);
            } else if (node->op == TOKEN_ABS) {
                emit(OP_ABS, 0);
            } else if (node->op == TOKEN_SQR) {
                emit(OP_DUP, 0);
                emit(OP_MUL, 0);
            } else if (node->op == TOKEN_ORD) {
                emit(OP_ORD, 0);
            } else if (node->op == TOKEN_CHR) {
                emit(OP_CHR, 0);
            }
            break;

        case NODE_BINARY_OP:
            generate_code(node->left);
            generate_code(node->right);
            switch (node->op) {
                case TOKEN_PLUS:
                    if (is_string_type(node->left->expression_type)) emit(OP_SCONCAT, 0);
                    else emit(OP_ADD, 0);
                    break;
                case TOKEN_MINUS: emit(OP_SUB, 0); break;
                case TOKEN_MUL:   emit(OP_MUL, 0); break;
                case TOKEN_DIV:   emit(OP_DIV, 0); break;
                case TOKEN_EQ:
                    if (is_string_type(node->left->expression_type)) emit(OP_SEQ, 0);
                    else emit(OP_EQ, 0);
                    break;
                case TOKEN_LT:    emit_ordering(node, OP_LT);  break;
                case TOKEN_GT:    emit_ordering(node, OP_GT);  break;
                case TOKEN_AND:
                    if (node->left->expression_type == TYPE_INTEGER) emit(OP_BAND, 0);
                    else emit(OP_AND, 0);
                    break;
                case TOKEN_OR:
                    if (node->left->expression_type == TYPE_INTEGER) emit(OP_BOR, 0);
                    else emit(OP_OR, 0);
                    break;
                case TOKEN_LTE: emit_ordering(node, OP_LTE); break;
                case TOKEN_GTE: emit_ordering(node, OP_GTE); break;
                case TOKEN_NEQ:
                    if (is_string_type(node->left->expression_type)) { emit(OP_SEQ, 0); emit(OP_NOT, 0); }
                    else emit(OP_NEQ, 0);
                    break;
                case TOKEN_DIV_KW: emit(OP_DIV, 0); break; // Reuses OP_DIV
                case TOKEN_MOD:    emit(OP_MOD, 0); break;
                case TOKEN_XOR:
                    if (node->left->expression_type == TYPE_INTEGER) emit(OP_BXOR, 0);
                    else emit(OP_XOR, 0);
                    break;
                case TOKEN_SHL: emit(OP_SHL, 0); break;
                case TOKEN_SHR: emit(OP_SHR, 0); break;
                default: break;
            }
            break;

        case NODE_WRITELN:
            for (ASTNode *arg = node->left; arg; arg = arg->next) {
                generate_code(arg);
                if (is_string_type(arg->expression_type)) emit(OP_PRINT_STR, 0);
                else if (arg->expression_type == TYPE_BOOLEAN) emit(OP_PRINT_BOOL, 0);
                else emit(OP_PRINT, 0);
            }
            if (node->op == TOKEN_WRITELN) emit(OP_NEWLINE, 0);
            generate_code(node->next);
            break;

        case NODE_READLN:
            emit(OP_READ, node->data.var_idx); // Reads stdin into var_idx
            generate_code(node->next);
            break;

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

        case NODE_BREAK:
            record_break();
            generate_code(node->next);
            break;

        case NODE_CONTINUE:
            record_continue();
            generate_code(node->next);
            break;

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
            // Caller pushed arguments left-to-right, so the last one is
            // on top of the operand stack - pop into the last parameter
            // slot first, working backwards to slot 0.
            for (int p = proc_table[i].param_count - 1; p >= 0; p--) {
                emit(OP_STORE_LOCAL, p);
            }
            generate_code(proc_table[i].body);
            if (proc_table[i].is_function) {
                emit(OP_LOAD_LOCAL, proc_table[i].return_slot);
            }
            emit(OP_RET, 0);
        }

        code[jmp_idx].arg = code_idx; // main starts here
    }
    generate_code(main_body);

    for (int i = 0; i < pending_call_count; i++) {
        code[pending_calls[i].call_instr_idx].arg = proc_table[pending_calls[i].target_proc_idx].entry_address;
    }
}

void emit_halt(void) {
    emit(OP_HALT, 0);
}

