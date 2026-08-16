#include <stdio.h>
#include <string.h>
#include "basic.h"

static float bits_to_float_local(int bits) { float f; memcpy(&f, &bits, sizeof(f)); return f; }

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) printf("    ");
}

static const char *op_str(BasicTokenType op) {
    switch (op) {
        case BTOK_PLUS:  return "+";
        case BTOK_MINUS: return "-";
        case BTOK_MUL:   return "*";
        case BTOK_SLASH: return "/";
        case BTOK_EQ:    return "=";
        case BTOK_LT:    return "<";
        case BTOK_GT:    return ">";
        case BTOK_LTE:   return "<=";
        case BTOK_GTE:   return ">=";
        case BTOK_NEQ:   return "<>";
        case BTOK_AND:   return "AND";
        case BTOK_OR:    return "OR";
        case BTOK_NOT:   return "NOT";
        default:         return "?";
    }
}

void basic_print_ast(BasicASTNode *node, int indent) {
    if (!node) return;
    print_indent(indent);

    switch (node->type) {
        case BNODE_NUMBER:
            if (node->expression_type == TYPE_REAL) printf("[Real] %g\n", bits_to_float_local(node->data.num_value));
            else printf("[Number] %d\n", node->data.num_value);
            break;
        case BNODE_STRING:
            printf("[String] \"%s\"\n", string_pool[node->data.num_value]);
            break;
        case BNODE_VARIABLE:
            printf("[Variable] %s\n", sym_table[node->data.var_idx].name);
            break;
        case BNODE_INT_TO_REAL:
            printf("[IntToReal]\n");
            basic_print_ast(node->left, indent + 1);
            break;
        case BNODE_UNARY_OP:
            printf("[UnaryOp] %s\n", op_str(node->op));
            basic_print_ast(node->left, indent + 1);
            break;
        case BNODE_BINARY_OP:
            printf("[BinaryOp] %s\n", op_str(node->op));
            basic_print_ast(node->left, indent + 1);
            basic_print_ast(node->right, indent + 1);
            break;
        case BNODE_LET:
            printf("[Let] -> %s\n", sym_table[node->data.var_idx].name);
            basic_print_ast(node->left, indent + 1);
            break;
        case BNODE_PRINT:
            // A single call, not a loop: basic_print_ast() already walks
            // the WHOLE ->next chain itself (see the bottom of this
            // function) - a manual per-item loop here would print every
            // item after the first twice.
            printf("[Print]%s\n", node->data.num_value ? " (no newline)" : "");
            basic_print_ast(node->left, indent + 1);
            break;
        case BNODE_INPUT:
            printf("[Input] -> %s\n", sym_table[node->data.var_idx].name);
            if (node->left) basic_print_ast(node->left, indent + 1);
            break;
        case BNODE_IF:
            printf("[If]\n");
            print_indent(indent + 1); printf("Condition:\n");
            basic_print_ast(node->left, indent + 2);
            print_indent(indent + 1); printf("Then:\n");
            basic_print_ast(node->right, indent + 2);
            if (node->extra) {
                print_indent(indent + 1); printf("Else:\n");
                basic_print_ast(node->extra, indent + 2);
            }
            break;
        case BNODE_GOTO:
            printf("[Goto] %d\n", node->data.num_value);
            break;
        case BNODE_GOSUB:
            printf("[Gosub] %d\n", node->data.num_value);
            break;
        case BNODE_RETURN:
            printf("[Return]\n");
            break;
        case BNODE_FOR:
            printf("[For] %s\n", sym_table[node->data.var_idx].name);
            print_indent(indent + 1); printf("From:\n");
            basic_print_ast(node->left, indent + 2);
            print_indent(indent + 1); printf("To:\n");
            basic_print_ast(node->right, indent + 2);
            if (node->extra) {
                print_indent(indent + 1); printf("Step:\n");
                basic_print_ast(node->extra, indent + 2);
            }
            break;
        case BNODE_NEXT:
            printf("[Next]%s%s\n", node->data.var_idx != -1 ? " " : "",
                   node->data.var_idx != -1 ? sym_table[node->data.var_idx].name : "");
            break;
        case BNODE_END:
            printf("[End]\n");
            break;
    }

    basic_print_ast(node->next, indent);
}
