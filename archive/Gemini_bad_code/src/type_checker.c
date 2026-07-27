#include "common.h"

void check_types(ASTNode *node) {
    if (!node) return;

    check_types(node->left);
    check_types(node->right);

    switch (node->type) {
        case NODE_INT:
            node->expression_type = TYPE_INTEGER;
            break;

        case NODE_BOOL:
            node->expression_type = TYPE_BOOLEAN;
            break;

        case NODE_STRING:
            node->expression_type = TYPE_STRING;
            break;

        case NODE_VAR: {
            int idx = lookup_symbol(node->data.name);
            if (idx == -1) {
                compile_error(0, "Type Checker Error: Undeclared variable '%s'", node->data.name);
            }
            node->expression_type = symbol_table[idx].type;
            break;
        }

        case NODE_UNOP:
            if (node->data.op == TOKEN_NOT) {
                if (node->left->expression_type != TYPE_BOOLEAN) {
                    compile_error(0, "Operator 'not' expects boolean expression");
                }
                node->expression_type = TYPE_BOOLEAN;
            } else if (node->data.op == TOKEN_MINUS) {
                if (node->left->expression_type != TYPE_INTEGER) {
                    compile_error(0, "Unary minus expects integer expression");
                }
                node->expression_type = TYPE_INTEGER;
            }
            break;

        case NODE_BINOP:
            if (node->left->expression_type != node->right->expression_type) {
                compile_error(0, "Type mismatch in binary operation");
            }

            // Relational Operators evaluate to Boolean
            if (node->data.op >= TOKEN_EQ && node->data.op <= TOKEN_GTE) {
                node->expression_type = TYPE_BOOLEAN;
            }
            // Logical Operators evaluate to Boolean
            else if (node->data.op == TOKEN_AND || node->data.op == TOKEN_OR || node->data.op == TOKEN_XOR) {
                if (node->left->expression_type != TYPE_BOOLEAN) {
                    compile_error(0, "Logical operators require boolean operands");
                }
                node->expression_type = TYPE_BOOLEAN;
            }
            // Arithmetic Operators evaluate to Integer
            else {
                if (node->left->expression_type != TYPE_INTEGER) {
                    compile_error(0, "Arithmetic operators require integer operands");
                }
                node->expression_type = TYPE_INTEGER;
            }
            break;

        case NODE_ASSIGN: {
            int idx = lookup_symbol(node->data.name);
            if (idx == -1) {
                compile_error(0, "Assignment to undeclared variable '%s'", node->data.name);
            }
            if (symbol_table[idx].type != node->left->expression_type) {
                compile_error(0, "Type mismatch in assignment to '%s'", node->data.name);
            }
            break;
        }

        default:
            break;
    }

    check_types(node->next);
}

