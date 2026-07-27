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
            if (sym_table[node->data.var_idx].is_array) {
                printf("[Array Assignment] -> Array: %s\n", sym_table[node->data.var_idx].name);
                print_indent(indent + 1);
                printf("Index:\n");
                print_ast(node->left, indent + 2);
                print_indent(indent + 1);
                printf("Value:\n");
                print_ast(node->right, indent + 2);
            } else {
                printf("[Assignment] -> Variable: %s\n", sym_table[node->data.var_idx].name);
                print_indent(indent + 1);
                printf("Value:\n");
                print_ast(node->left, indent + 2);
            }
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_WRITELN: {
            printf("[%s]\n", node->op == TOKEN_WRITE ? "Write" : "WriteLn");
            int arg_num = 1;
            for (ASTNode *arg = node->left; arg; arg = arg->next) {
                print_indent(indent + 1);
                printf("Arg %d:\n", arg_num++);
                print_ast(arg, indent + 2);
            }
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;
        }
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
            printf("[String] \"%s\"\n", string_pool[node->data.var_idx]);
            break;

        case NODE_IF:
            printf("[If]\n");
            print_indent(indent + 1);
            printf("Condition:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Then:\n");
            print_ast(node->right, indent + 2);
            if (node->extra) {
                print_indent(indent + 1);
                printf("Else:\n");
                print_ast(node->extra, indent + 2);
            }
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_WHILE:
            printf("[While]\n");
            print_indent(indent + 1);
            printf("Condition:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Body:\n");
            print_ast(node->right, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_REPEAT:
            printf("[Repeat]\n");
            print_indent(indent + 1);
            printf("Body:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Until:\n");
            print_ast(node->right, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_FOR:
            printf("[For] -> Variable: %s (%s)\n", sym_table[node->data.var_idx].name,
                   node->op == TOKEN_DOWNTO ? "downto" : "to");
            print_indent(indent + 1);
            printf("From:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("To:\n");
            print_ast(node->right, indent + 2);
            print_indent(indent + 1);
            printf("Do:\n");
            print_ast(node->extra, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_ARRAY_ACCESS:
            printf("[Array Access] -> Array: %s\n", sym_table[node->data.var_idx].name);
            print_indent(indent + 1);
            printf("Index:\n");
            print_ast(node->left, indent + 2);
            break;

        case NODE_BREAK:
            printf("[Break]\n");
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_CONTINUE:
            printf("[Continue]\n");
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;
    }
}

