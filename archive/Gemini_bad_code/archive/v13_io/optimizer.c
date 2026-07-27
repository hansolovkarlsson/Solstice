#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "optimizer.h"
#include "parser.h"

ASTNode *optimize_ast(ASTNode *node) {
    if (!node) return NULL;

    node->left = optimize_ast(node->left);
    node->right = optimize_ast(node->right);
    node->next = optimize_ast(node->next);

    if (node->type == NODE_BINARY_OP && node->left->type == NODE_NUMBER && node->right->type == NODE_NUMBER) {
        int l_val = node->left->data.num_value;
        int r_val = node->right->data.num_value;
        int folded_val = 0;
        int is_comparison = 0;

        switch (node->op) {
            case TOKEN_PLUS:  folded_val = l_val + r_val; break;
            case TOKEN_MINUS: folded_val = l_val - r_val; break;
            case TOKEN_MUL:   folded_val = l_val * r_val; break;
            case TOKEN_DIV:   
                if (r_val == 0) { 
                    fprintf(stderr, "%s:%d: Compile Error: Division by zero\n", get_current_filename(), node->line);
                    exit(1);
                }
                folded_val = l_val / r_val; 
                break;
            case TOKEN_EQ: folded_val = (l_val == r_val); is_comparison = 1; break;
            case TOKEN_LT: folded_val = (l_val < r_val);  is_comparison = 1; break;
            case TOKEN_GT: folded_val = (l_val > r_val);  is_comparison = 1; break;
            default: return node;
        }

        printf("[Optimization] Folded constants: %d and %d\n", l_val, r_val);
        free_ast(node->left);
        free_ast(node->right);
        
        node->type = is_comparison ? NODE_BOOLEAN : NODE_NUMBER;
        node->data.num_value = folded_val;
        node->left = NULL;
        node->right = NULL;
    }
    return node;
}

static int var_used_tracker[MAX_SYMBOLS];

static void mark_used_variables(ASTNode *node) {
    if (!node) return;

    if (node->type == NODE_VARIABLE) {
        var_used_tracker[node->data.var_idx] = 1;
    }

    mark_used_variables(node->left);
    mark_used_variables(node->right);
    mark_used_variables(node->next);
}

static ASTNode *sweep_dead_assignments(ASTNode *node) {
    if (!node) return NULL;

    if (node->type == NODE_COMPOUND) {
        node->left = sweep_dead_assignments(node->left);
        return node;
    }

    if (node->type == NODE_ASSIGN) {
        int var_idx = node->data.var_idx;
        node->next = sweep_dead_assignments(node->next);

        if (!var_used_tracker[var_idx]) {
            printf("[DCE Optimization] Removing dead assignment to unreferenced variable: %s\n", 
                   sym_table[var_idx].name);
            
            ASTNode *next_cached = node->next;
            node->left = optimize_ast(node->left);
            free_ast(node->left);
            node->left = NULL;
            node->next = NULL;
            free(node);
            
            return next_cached;
        }
        
        node->left = sweep_dead_assignments(node->left);
        return node;
    }

    node->left = sweep_dead_assignments(node->left);
    node->right = sweep_dead_assignments(node->right);
    return node;
}

ASTNode *eliminate_dead_code(ASTNode *node) {
    memset(var_used_tracker, 0, sizeof(var_used_tracker));
    mark_used_variables(node);
    return sweep_dead_assignments(node);
}

