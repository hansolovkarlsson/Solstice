#include "common.h"

// --- Constant Folding ---
ASTNode *optimize_ast(ASTNode *node) {
    if (!node) return NULL;

    // Recursively optimize children first
    node->left = optimize_ast(node->left);
    node->right = optimize_ast(node->right);

    // Constant Folding: Unary Operations
    if (node->type == NODE_UNOP && node->left && 
       (node->left->type == NODE_INT || node->left->type == NODE_BOOL)) {
        
        if (node->data.op == TOKEN_MINUS && node->left->type == NODE_INT) {
            int val = node->left->data.int_val;
            node->type = NODE_INT;
            node->data.int_val = -val;
            node->expression_type = TYPE_INTEGER;
            free(node->left);
            node->left = NULL;
        } else if (node->data.op == TOKEN_NOT && node->left->type == NODE_BOOL) {
            bool val = node->left->data.bool_val;
            node->type = NODE_BOOL;
            node->data.bool_val = !val;
            node->expression_type = TYPE_BOOLEAN;
            free(node->left);
            node->left = NULL;
        }
    }

    // Constant Folding: Binary Arithmetic & Logical Operations
    if (node->type == NODE_BINOP && node->left && node->right &&
        node->left->type == NODE_INT && node->right->type == NODE_INT) {
        
        int l_val = node->left->data.int_val;
        int r_val = node->right->data.int_val;
        int result = 0;
        bool can_fold = true;

        switch (node->data.op) {
            case TOKEN_PLUS:  result = l_val + r_val; break;
            case TOKEN_MINUS: result = l_val - r_val; break;
            case TOKEN_STAR:  result = l_val * r_val; break;
            case TOKEN_SLASH:
            case TOKEN_DIV:
                if (r_val == 0) {
                    compile_error(0, "Optimizer Error: Division by zero at compile time");
                }
                result = l_val / r_val;
                break;
            case TOKEN_MOD:
                if (r_val == 0) {
                    compile_error(0, "Optimizer Error: Division by zero at compile time");
                }
                result = l_val % r_val;
                break;
            default:
                can_fold = false;
                break;
        }

        if (can_fold) {
            node->type = NODE_INT;
            node->data.int_val = result;
            node->expression_type = TYPE_INTEGER;
            free(node->left);
            free(node->right);
            node->left = NULL;
            node->right = NULL;
        }
    }

    // Process statement chains
    node->next = optimize_ast(node->next);
    return node;
}

// --- Helper: Deep Free Subtree Memory ---
static void free_ast_node(ASTNode *node) {
    if (!node) return;
    free_ast_node(node->left);
    free_ast_node(node->right);
    // Do not recursively free node->next here to avoid stack overflow on long statement chains
    free(node);
}

// --- Dead Code Elimination (DCE) ---
ASTNode *eliminate_dead_code(ASTNode *node) {
    if (!node) return NULL;

    // Process children first
    node->left = eliminate_dead_code(node->left);
    node->right = eliminate_dead_code(node->right);

    // Eliminate dead branch if a conditional block is guarded by a constant 'false'
    if (node->type == NODE_BLOCK) {
        if (node->left && node->left->type == NODE_BOOL && !node->left->data.bool_val) {
            free_ast_node(node->right);
            node->right = NULL;
        }
    }

    // Process and stitch statement chains
    node->next = eliminate_dead_code(node->next);

    return node;
}

