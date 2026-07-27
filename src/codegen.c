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
            if (node->op == TOKEN_MINUS) emit(OP_NEG, 0);
            else if (node->op == TOKEN_NOT) emit(OP_NOT, 0);
            break;

        case NODE_BINARY_OP:
            generate_code(node->left);
            generate_code(node->right);
            switch (node->op) {
                case TOKEN_PLUS:
                    if (node->left->expression_type == TYPE_STRING) emit(OP_SCONCAT, 0);
                    else emit(OP_ADD, 0);
                    break;
                case TOKEN_MINUS: emit(OP_SUB, 0); break;
                case TOKEN_MUL:   emit(OP_MUL, 0); break;
                case TOKEN_DIV:   emit(OP_DIV, 0); break;
                case TOKEN_EQ:
                    if (node->left->expression_type == TYPE_STRING) emit(OP_SEQ, 0);
                    else emit(OP_EQ, 0);
                    break;
                case TOKEN_LT:    emit(OP_LT, 0);  break;
                case TOKEN_GT:    emit(OP_GT, 0);  break;
                case TOKEN_AND: emit(OP_AND, 0); break;
                case TOKEN_OR:  emit(OP_OR, 0); break;
                case TOKEN_LTE: emit(OP_LTE, 0); break;
                case TOKEN_GTE: emit(OP_GTE, 0); break;
                case TOKEN_NEQ:
                    if (node->left->expression_type == TYPE_STRING) { emit(OP_SEQ, 0); emit(OP_NOT, 0); }
                    else emit(OP_NEQ, 0);
                    break;
                case TOKEN_DIV_KW: emit(OP_DIV, 0); break; // Reuses OP_DIV
                case TOKEN_MOD:    emit(OP_MOD, 0); break;
                case TOKEN_XOR:    emit(OP_XOR, 0); break;
                default: break;
            }
            break;

        case NODE_WRITELN:
            for (ASTNode *arg = node->left; arg; arg = arg->next) {
                generate_code(arg);
                if (arg->expression_type == TYPE_STRING) emit(OP_PRINT_STR, 0);
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
        //     <body>
        //     JMP loop_start
        //   end:
        case NODE_WHILE: {
            int loop_start = code_idx;
            generate_code(node->left);        // condition
            int jz_idx = code_idx;
            emit(OP_JZ, 0);                   // placeholder, patched below
            generate_code(node->right);       // body
            emit(OP_JMP, loop_start);
            code[jz_idx].arg = code_idx;      // JZ lands here: past the loop
            generate_code(node->next);
            break;
        }

        // repeat <body> until <cond>
        //   loop_start:
        //     <body>
        //     <cond>
        //     JZ loop_start      ; loop again while cond is still false
        case NODE_REPEAT: {
            int loop_start = code_idx;
            generate_code(node->left);        // body (statement chain)
            generate_code(node->right);       // until-condition
            emit(OP_JZ, loop_start);
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
        //     <body>
        //     LOAD var
        //     PUSH 1
        //     ADD/SUB              ; ADD for 'to', SUB for 'downto'
        //     STORE var
        //     JMP loop_start
        //   end:
        case NODE_FOR: {
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

            emit(OP_LOAD, loop_var);
            emit(OP_PUSH, 1);
            emit(descending ? OP_SUB : OP_ADD, 0);
            emit(OP_STORE, loop_var);
            emit(OP_JMP, loop_start);

            code[jz_idx].arg = code_idx;       // JZ lands here: past the loop
            generate_code(node->next);
            break;
        }
    }
}

void emit_halt(void) {
    emit(OP_HALT, 0);
}

