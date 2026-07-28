#include <stdio.h>
#include "codegen.h"

static void emit(Opcode op, int arg) {
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
                default: break;
            }
            break;
    }
}

