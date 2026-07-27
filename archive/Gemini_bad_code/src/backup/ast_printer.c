#include <stdio.h>
#include "ast_printer.h"

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        printf("    ");
    }
}

static const char* token_type_to_str(TokenType type) {
    switch (type) {
        case TOKEN_PLUS:  return "+";
        case TOKEN_MINUS: return "-";
        case TOKEN_MUL:   return "*";
        case TOKEN_DIV:   return "/";
        case TOKEN_EQ:    return "=";
        case TOKEN_LT:    return "<";
        case TOKEN_GT:    return ">";
        case TOKEN_AND:   return "and";
        case TOKEN_OR:    return "or";
        case TOKEN_NOT:   return "not";
        case TOKEN_LTE:   return "<=";
        case TOKEN_GTE:   return ">=";
        case TOKEN_NEQ:   return "<>";
        case TOKEN_DIV_KW: return "div";
        case TOKEN_MOD:    return "mod";
        case TOKEN_XOR:    return "xor";
        default:          return "?";
    }
}

void print_ast(ASTNode *node, int indent) {
    if (!node) return;

    print_indent(indent);

    switch (node->type) {
        case NODE_COMPOUND:
            printf("[Compound Statement]\n");
            print_ast(node->left, indent + 1);
            break;

        case NODE_ASSIGN:
            printf("[Assignment] -> Variable: %s\n", sym_table[node->data.var_idx].name);
            print_indent(indent + 1);
            printf("Value:\n");
            print_ast(node->left, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_WRITELN:
            printf("[WriteLn]\n");
            print_indent(indent + 1);
            printf("Expression:\n");
            print_ast(node->left, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_READLN:
            printf("[ReadLn] -> Target Variable: %s\n", sym_table[node->data.var_idx].name);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_UNARY_OP:
            printf("[Unary Op] '%s'\n", token_type_to_str(node->op));
            print_indent(indent + 1);
            printf("Operand:\n");
            print_ast(node->left, indent + 2);
            break;

        case NODE_BINARY_OP:
            printf("[Binary Op] '%s'\n", token_type_to_str(node->op));
            print_indent(indent + 1);
            printf("Left:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Right:\n");
            print_ast(node->right, indent + 2);
            break;

        case NODE_NUMBER:
            printf("[Number] %d\n", node->data.num_value);
            break;

        case NODE_BOOLEAN:
            printf("[Boolean] %s\n", node->data.num_value ? "true" : "false");
            break;

        case NODE_VARIABLE:
            printf("[Variable] %s\n", sym_table[node->data.var_idx].name);
            break;

        case NODE_STRING:
            if (node->data.str_idx >= 0 && 
                node->data.str_idx < string_pool_count && 
                string_pool[node->data.str_idx] != NULL) 
            {
                printf("[String] \"%s\"\n", string_pool[node->data.str_idx]);
            } else {
                printf("[String] (invalid/null index %d)\n", node->data.str_idx);
            }
            break;

    }
}

