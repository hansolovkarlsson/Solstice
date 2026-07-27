#include <stdio.h>
#include <stdlib.h>
#include "type_checker.h"
#include "parser.h"

void type_check(ASTNode *node) {
    if (!node) return;

    type_check(node->left);
    type_check(node->right);
    type_check(node->next);

    switch (node->type) {
        case NODE_ASSIGN: {
            DataType target_type = sym_table[node->data.var_idx].type;
            if (node->left->expression_type != target_type) {
                fprintf(stderr, "%s:%d: Type Error: Cannot assign expression to variable '%s'\n",
                        get_current_filename(), node->line, sym_table[node->data.var_idx].name);
                exit(1);
            }
            break;
        }

        case NODE_BINARY_OP: {
            DataType left_t = node->left->expression_type;
            DataType right_t = node->right->expression_type;

            if (left_t != right_t) {
                fprintf(stderr, "%s:%d: Type Error: Mismatched operand types in binary operation\n",
                        get_current_filename(), node->line);
                exit(1);
            }

            if (node->op == TOKEN_PLUS || node->op == TOKEN_MINUS ||
                node->op == TOKEN_MUL  || node->op == TOKEN_DIV) {
                if (left_t != TYPE_INTEGER) {
                    fprintf(stderr, "%s:%d: Type Error: Arithmetic operations require integer operands\n",
                            get_current_filename(), node->line);
                    exit(1);
                }
                node->expression_type = TYPE_INTEGER;
            } else if (node->op == TOKEN_EQ || node->op == TOKEN_LT || node->op == TOKEN_GT) {
                node->expression_type = TYPE_BOOLEAN;
            }
            break;
        }

        default:
            break;
    }
}

