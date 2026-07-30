#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "optimizer.h"
#include "parser.h"
#include "error.h"

// Local copies of the bit-reinterpretation helpers (see vm.c for the
// full explanation) - needed here to fold real-literal arithmetic at
// compile time, matching this project's established pattern of
// duplicating small helpers per file rather than sharing them via a
// header.
static float bits_to_float(int bits) {
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

static int float_to_bits(float f) {
    int bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

ASTNode *optimize_ast(ASTNode *node) {
    if (!node) return NULL;

    node->left = optimize_ast(node->left);
    node->right = optimize_ast(node->right);
    node->next = optimize_ast(node->next);
    node->extra = optimize_ast(node->extra);

    if (node->type == NODE_UNARY_OP 
        && (node->left->type == NODE_NUMBER || node->left->type == NODE_BOOLEAN)
        && (node->op == TOKEN_MINUS || node->op == TOKEN_NOT ||
            node->op == TOKEN_ABS || node->op == TOKEN_SQR)) {
        int val = node->left->data.num_value;
        if (node->op == TOKEN_MINUS) {
            node->type = NODE_NUMBER;
            node->data.num_value = -val;
        } else if (node->op == TOKEN_NOT) {
            // Logical not on a boolean literal, bitwise not on an integer
            // literal - same distinction as the runtime opcodes.
            if (node->left->type == NODE_BOOLEAN) {
                node->type = NODE_BOOLEAN;
                node->data.num_value = !val;
            } else {
                node->type = NODE_NUMBER;
                node->data.num_value = ~val;
            }
        } else if (node->op == TOKEN_ABS) {
            node->type = NODE_NUMBER;
            node->data.num_value = val < 0 ? -val : val;
        } else if (node->op == TOKEN_SQR) {
            node->type = NODE_NUMBER;
            node->data.num_value = val * val;
        }
        free_ast(node->left);
        node->left = NULL;
        return node;
    }

    if (node->type == NODE_UNARY_OP && node->left->type == NODE_REAL_NUMBER
        && (node->op == TOKEN_MINUS || node->op == TOKEN_ABS || node->op == TOKEN_SQR ||
            node->op == TOKEN_TRUNC || node->op == TOKEN_ROUND || node->op == TOKEN_SQRT ||
            node->op == TOKEN_SIN || node->op == TOKEN_COS || node->op == TOKEN_ARCTAN ||
            node->op == TOKEN_EXP || node->op == TOKEN_LN)) {
        float val = bits_to_float(node->left->data.num_value);
        float real_result = 0.0f;
        int has_real_result = 0;
        if (node->op == TOKEN_MINUS) {
            node->type = NODE_REAL_NUMBER;
            node->data.num_value = float_to_bits(-val);
        } else if (node->op == TOKEN_ABS) {
            node->type = NODE_REAL_NUMBER;
            node->data.num_value = float_to_bits(fabsf(val));
        } else if (node->op == TOKEN_SQR) {
            node->type = NODE_REAL_NUMBER;
            node->data.num_value = float_to_bits(val * val);
        } else if (node->op == TOKEN_TRUNC) {
            node->type = NODE_NUMBER;
            node->data.num_value = (int)val;
        } else if (node->op == TOKEN_ROUND) {
            node->type = NODE_NUMBER;
            node->data.num_value = (int)roundf(val);
        } else if (node->op == TOKEN_SQRT) {
            real_result = sqrtf(val); has_real_result = 1;
        } else if (node->op == TOKEN_SIN) {
            real_result = sinf(val); has_real_result = 1;
        } else if (node->op == TOKEN_COS) {
            real_result = cosf(val); has_real_result = 1;
        } else if (node->op == TOKEN_ARCTAN) {
            real_result = atanf(val); has_real_result = 1;
        } else if (node->op == TOKEN_EXP) {
            real_result = expf(val); has_real_result = 1;
        } else if (node->op == TOKEN_LN) {
            real_result = logf(val); has_real_result = 1;
        }
        if (has_real_result) {
            if (isnan(real_result) || isinf(real_result)) {
                fprintf(stderr, "%s:%d: Compile Error: constant expression produced an invalid result (domain error or overflow)\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            node->type = NODE_REAL_NUMBER;
            node->data.num_value = float_to_bits(real_result);
        }
        free_ast(node->left);
        node->left = NULL;
        return node;
    }

    // Folds an implicit int->real widening (see type_checker.c's
    // widen_to_real) whose operand is itself a folded integer literal -
    // e.g. '2 + 3.14' widens the '2' to a NODE_INT_TO_REAL wrapping
    // NODE_NUMBER(2); this turns that into a plain NODE_REAL_NUMBER(2.0),
    // which is what lets the surrounding binary-op fold below see two
    // real literals and fold the whole expression down to a single
    // constant, rather than only folding operations between two
    // naturally-real literals.
    if (node->type == NODE_INT_TO_REAL && node->left->type == NODE_NUMBER) {
        float real_val = (float)node->left->data.num_value;
        free_ast(node->left);
        node->left = NULL;
        node->type = NODE_REAL_NUMBER;
        node->data.num_value = float_to_bits(real_val);
        return node;
    }

    if (node->type == NODE_BINARY_OP
        && (node->left->type == NODE_NUMBER || node->left->type == NODE_BOOLEAN)
        && (node->right->type == NODE_NUMBER || node->right->type == NODE_BOOLEAN)) {
        int l_val = node->left->data.num_value;
        int r_val = node->right->data.num_value;
        int folded_val = 0;
        int is_comparison = 0;
        int is_bool_operand = (node->left->type == NODE_BOOLEAN); // AND/OR/XOR only: by this
                                          // point type-checking already
                                          // guaranteed both operands match
                                          // (both boolean or both integer)

        switch (node->op) {
            case TOKEN_PLUS:  folded_val = l_val + r_val; break;
            case TOKEN_MINUS: folded_val = l_val - r_val; break;
            case TOKEN_MUL:   folded_val = l_val * r_val; break;
            case TOKEN_DIV:   
                if (r_val == 0) { 
                    fprintf(stderr, "%s:%d: Compile Error: Division by zero\n", get_current_filename(), node->line);
                    fatal_abort();
                }
                folded_val = l_val / r_val; 
                break;
            case TOKEN_EQ: folded_val = (l_val == r_val); is_comparison = 1; break;
            case TOKEN_LT: folded_val = (l_val < r_val);  is_comparison = 1; break;
            case TOKEN_GT: folded_val = (l_val > r_val);  is_comparison = 1; break;
            case TOKEN_AND:
                if (is_bool_operand) { folded_val = (l_val && r_val); is_comparison = 1; }
                else { folded_val = (l_val & r_val); }
                break;
            case TOKEN_OR:
                if (is_bool_operand) { folded_val = (l_val || r_val); is_comparison = 1; }
                else { folded_val = (l_val | r_val); }
                break;
            case TOKEN_LTE: folded_val = (l_val <= r_val); is_comparison = 1; break;
            case TOKEN_GTE: folded_val = (l_val >= r_val); is_comparison = 1; break;
            case TOKEN_NEQ: folded_val = (l_val != r_val); is_comparison = 1; break;
            case TOKEN_DIV_KW:
                if (r_val == 0) { fprintf(stderr, "Error: Constant division by zero\n"); fatal_abort(); }
                folded_val = l_val / r_val;
                break;
            case TOKEN_MOD:
                if (r_val == 0) { fprintf(stderr, "Error: Constant modulo by zero\n"); fatal_abort(); }
                folded_val = l_val % r_val;
                break;
            case TOKEN_XOR:
                if (is_bool_operand) { folded_val = (l_val != r_val); is_comparison = 1; }
                else { folded_val = (l_val ^ r_val); }
                break;
            case TOKEN_SHL:
                if (r_val < 0 || r_val >= 32) {
                    fprintf(stderr, "%s:%d: Compile Error: Shift amount %d out of range (0..31)\n",
                            get_current_filename(), node->line, r_val);
                    fatal_abort();
                }
                folded_val = l_val << r_val;
                break;
            case TOKEN_SHR:
                if (r_val < 0 || r_val >= 32) {
                    fprintf(stderr, "%s:%d: Compile Error: Shift amount %d out of range (0..31)\n",
                            get_current_filename(), node->line, r_val);
                    fatal_abort();
                }
                folded_val = (int)((unsigned int)l_val >> r_val);
                break;
            default: return node;
        }

        if (verbose_mode) printf("[Optimization] Folded constants: %d and %d\n", l_val, r_val);
        free_ast(node->left);
        free_ast(node->right);
        
        node->type = is_comparison ? NODE_BOOLEAN : NODE_NUMBER;
        node->data.num_value = folded_val;
        node->left = NULL;
        node->right = NULL;
    }

    if (node->type == NODE_BINARY_OP
        && node->left->type == NODE_REAL_NUMBER && node->right->type == NODE_REAL_NUMBER
        && (node->op == TOKEN_PLUS || node->op == TOKEN_MINUS || node->op == TOKEN_MUL ||
            node->op == TOKEN_DIV || node->op == TOKEN_POW || node->op == TOKEN_EQ ||
            node->op == TOKEN_LT || node->op == TOKEN_GT || node->op == TOKEN_LTE ||
            node->op == TOKEN_GTE || node->op == TOKEN_NEQ)) {
        // div/mod/shl/shr/and/or/xor don't apply to real at all (the type
        // checker already rejects them), so there's no case for those
        // here - every reachable operator is handled below.
        float l_val = bits_to_float(node->left->data.num_value);
        float r_val = bits_to_float(node->right->data.num_value);
        int is_comparison = 0;
        float folded_real = 0.0f;
        int folded_bool = 0;

        switch (node->op) {
            case TOKEN_PLUS:  folded_real = l_val + r_val; break;
            case TOKEN_MINUS: folded_real = l_val - r_val; break;
            case TOKEN_MUL:   folded_real = l_val * r_val; break;
            case TOKEN_DIV:
                if (r_val == 0.0f) {
                    fprintf(stderr, "%s:%d: Compile Error: Division by zero\n", get_current_filename(), node->line);
                    fatal_abort();
                }
                folded_real = l_val / r_val;
                break;
            case TOKEN_POW:
                folded_real = powf(l_val, r_val);
                if (isnan(folded_real) || isinf(folded_real)) {
                    fprintf(stderr, "%s:%d: Compile Error: constant expression produced an invalid result (domain error or overflow)\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
                break;
            case TOKEN_EQ:  folded_bool = (l_val == r_val); is_comparison = 1; break;
            case TOKEN_LT:  folded_bool = (l_val < r_val);  is_comparison = 1; break;
            case TOKEN_GT:  folded_bool = (l_val > r_val);  is_comparison = 1; break;
            case TOKEN_LTE: folded_bool = (l_val <= r_val); is_comparison = 1; break;
            case TOKEN_GTE: folded_bool = (l_val >= r_val); is_comparison = 1; break;
            case TOKEN_NEQ: folded_bool = (l_val != r_val); is_comparison = 1; break;
            default: break;
        }

        if (verbose_mode) printf("[Optimization] Folded real constants\n");
        free_ast(node->left);
        free_ast(node->right);
        node->left = NULL;
        node->right = NULL;
        if (is_comparison) {
            node->type = NODE_BOOLEAN;
            node->data.num_value = folded_bool;
        } else {
            node->type = NODE_REAL_NUMBER;
            node->data.num_value = float_to_bits(folded_real);
        }
    }
    return node;
}

static int var_used_tracker[MAX_SYMBOLS];

static void mark_used_variables(ASTNode *node) {
    if (!node) return;

    if (node->type == NODE_VARIABLE || node->type == NODE_ARRAY_ACCESS || node->type == NODE_ARRAY_REF
        || node->type == NODE_STRING_INDEX || node->type == NODE_ARRAY_ACCESS_2D) {
        var_used_tracker[node->data.var_idx] = 1;
    }

    mark_used_variables(node->left);
    mark_used_variables(node->right);
    mark_used_variables(node->next);
    mark_used_variables(node->extra);
}

static ASTNode *sweep_dead_assignments(ASTNode *node) {
    if (!node) return NULL;

    if (node->type == NODE_COMPOUND) {
        node->left = sweep_dead_assignments(node->left);
        node->next = sweep_dead_assignments(node->next); // a compound used
                                                           // as one statement
                                                           // in a sequence
                                                           // can have
                                                           // siblings after it
        return node;
    }

    if (node->type == NODE_ASSIGN) {
        int var_idx = node->data.var_idx;
        node->next = sweep_dead_assignments(node->next);

        if (!var_used_tracker[var_idx]) {
            if (verbose_mode) {
                printf("[DCE Optimization] Removing dead assignment to unreferenced variable: %s\n",
                       sym_table[var_idx].name);
            }
            
            ASTNode *next_cached = node->next;
            node->left = optimize_ast(node->left);
            free_ast(node->left);
            node->left = NULL;
            if (node->right) {
                node->right = optimize_ast(node->right);
                free_ast(node->right);
                node->right = NULL;
            }
            node->next = NULL;
            free(node);
            
            return next_cached;
        }
        
        node->left = sweep_dead_assignments(node->left);
        if (node->right) {
            node->right = sweep_dead_assignments(node->right);
        }
        return node;
    }

    if (node->type == NODE_ARRAY_ASSIGN_2D) {
        int var_idx = node->data.var_idx;
        node->next = sweep_dead_assignments(node->next);

        if (!var_used_tracker[var_idx]) {
            if (verbose_mode) {
                printf("[DCE Optimization] Removing dead assignment to unreferenced variable: %s\n",
                       sym_table[var_idx].name);
            }

            ASTNode *next_cached = node->next;
            node->left = optimize_ast(node->left);
            free_ast(node->left);
            node->left = NULL;
            node->right = optimize_ast(node->right);
            free_ast(node->right);
            node->right = NULL;
            node->extra = optimize_ast(node->extra);
            free_ast(node->extra);
            node->extra = NULL;
            node->next = NULL;
            free(node);

            return next_cached;
        }

        node->left = sweep_dead_assignments(node->left);
        node->right = sweep_dead_assignments(node->right);
        node->extra = sweep_dead_assignments(node->extra);
        return node;
    }

    if (node->type == NODE_STRING_INDEX_ASSIGN) {
        int var_idx = node->data.var_idx;
        node->next = sweep_dead_assignments(node->next);

        if (!var_used_tracker[var_idx]) {
            if (verbose_mode) {
                printf("[DCE Optimization] Removing dead assignment to unreferenced variable: %s\n",
                       sym_table[var_idx].name);
            }

            ASTNode *next_cached = node->next;
            node->left = optimize_ast(node->left);
            free_ast(node->left);
            node->left = NULL;
            node->right = optimize_ast(node->right);
            free_ast(node->right);
            node->right = NULL;
            node->next = NULL;
            free(node);

            return next_cached;
        }

        node->left = sweep_dead_assignments(node->left);
        node->right = sweep_dead_assignments(node->right);
        return node;
    }

    node->left = sweep_dead_assignments(node->left);
    node->right = sweep_dead_assignments(node->right);
    node->next = sweep_dead_assignments(node->next);
    node->extra = sweep_dead_assignments(node->extra);
    return node;
}

// Multi-tree-safe DCE API: reset once, mark() every tree in the program
// (main body, and now every procedure body), THEN sweep() every tree.
// This matters once a variable can be written in one tree and read in
// another - e.g. a procedure assigns a global that main later reads.
// Marking each tree in isolation (the old single-call eliminate_dead_code
// below still does exactly that) would see no read within the writing
// tree and incorrectly remove the assignment as dead.
void dce_reset(void) {
    memset(var_used_tracker, 0, sizeof(var_used_tracker));
}

void dce_mark(ASTNode *node) {
    mark_used_variables(node);
}

ASTNode *dce_sweep(ASTNode *node) {
    return sweep_dead_assignments(node);
}

// Single-tree convenience wrapper (reset + mark + sweep in one call) -
// correct as long as the caller passes the *only* tree that exists in the
// program (no procedures). test_recovery.c's test programs use this.
ASTNode *eliminate_dead_code(ASTNode *node) {
    dce_reset();
    dce_mark(node);
    return dce_sweep(node);
}

