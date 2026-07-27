#include "common.h"

void print_ast(ASTNode *node, int indent) {
    if (!node) return;

    for (int i = 0; i < indent; i++) printf("  ");

    switch (node->type) {
        case NODE_PROGRAM:
            printf("Program\n");
            break;
        case NODE_BLOCK:
            printf("Block\n");
            break;
        case NODE_ASSIGN:
            printf("Assign [%s]\n", node->data.name);
            break;
        case NODE_BINOP:
            printf("BinOp [Op Token: %d]\n", node->data.op);
            break;
        case NODE_UNOP:
            printf("UnOp [Op Token: %d]\n", node->data.op);
            break;
        case NODE_INT:
            printf("Int Literal [%d]\n", node->data.int_val);
            break;
        case NODE_BOOL:
            printf("Bool Literal [%s]\n", node->data.bool_val ? "true" : "false");
            break;
        case NODE_STRING:
            if (node->data.str_idx >= 0 && node->data.str_idx < string_pool_count && string_pool[node->data.str_idx]) {
                printf("String Literal [\"%s\"]\n", string_pool[node->data.str_idx]);
            } else {
                printf("String Literal [invalid index %d]\n", node->data.str_idx);
            }
            break;
        case NODE_VAR:
            printf("Var [%s]\n", node->data.name);
            break;
        case NODE_WRITELN:
            printf("Writeln\n");
            break;
        case NODE_READLN:
            printf("Readln [%s]\n", node->data.name);
            break;
        default:
            printf("Unknown AST Node\n");
            break;
    }

    print_ast(node->left, indent + 1);
    print_ast(node->right, indent + 1);
    print_ast(node->next, indent);
}

