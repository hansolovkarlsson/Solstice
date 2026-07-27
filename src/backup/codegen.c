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
            emit(OP_HALT, 0);
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
    }
}

