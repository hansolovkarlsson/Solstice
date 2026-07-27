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

void generate_code(ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case NODE_COMPOUND:
            generate_code(node->left);
            generate_code(node->next);
            break;

        case NODE_ASSIGN:
            generate_code(node->left);
            emit(OP_STORE, node->data.var_idx);
            generate_code(node->next);
            break;

        case NODE_NUMBER:
        case NODE_BOOLEAN:
            emit(OP_PUSH, node->data.num_value);
            break;

        case NODE_VARIABLE:
            emit(OP_LOAD, node->data.var_idx);
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
                case TOKEN_PLUS:  emit(OP_ADD, 0); break;
                case TOKEN_MINUS: emit(OP_SUB, 0); break;
                case TOKEN_MUL:   emit(OP_MUL, 0); break;
                case TOKEN_DIV:   emit(OP_DIV, 0); break;
                case TOKEN_EQ:    emit(OP_EQ, 0);  break;
                case TOKEN_LT:    emit(OP_LT, 0);  break;
                case TOKEN_GT:    emit(OP_GT, 0);  break;
                case TOKEN_AND: emit(OP_AND, 0); break;
                case TOKEN_OR:  emit(OP_OR, 0); break;
                case TOKEN_LTE: emit(OP_LTE, 0); break;
                case TOKEN_GTE: emit(OP_GTE, 0); break;
                case TOKEN_NEQ: emit(OP_NEQ, 0); break;
                case TOKEN_DIV_KW: emit(OP_DIV, 0); break; // Reuses OP_DIV
                case TOKEN_MOD:    emit(OP_MOD, 0); break;
                case TOKEN_XOR:    emit(OP_XOR, 0); break;
                default: break;
            }
            break;

        case NODE_WRITELN:
            generate_code(node->left); // Evaluates expression onto VM stack
            emit(OP_PRINT, 0);
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
    }
}

void emit_halt(void) {
    emit(OP_HALT, 0);
}

