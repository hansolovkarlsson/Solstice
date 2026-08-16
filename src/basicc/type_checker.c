#include <stdio.h>
#include <stdarg.h>
#include "basic.h"

static void type_error(int line, const char *fmt, ...) {
    fprintf(stderr, "%s:%d: Compile Error: ", basic_get_current_filename(), line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    fatal_abort();
}

static void check_expr(BasicASTNode *node);

// Promotes an already-checked expression to `target` where that's safe
// (integer -> real, the one implicit numeric widening this dialect
// allows - see docs/BASIC.md). Anything else (a string mixed with a
// number, or a real narrowed down to an integer-sigil variable, which
// v1 deliberately doesn't support without an explicit conversion
// builtin it doesn't have yet) is a compile error.
static BasicASTNode *coerce_numeric(BasicASTNode *expr, DataType target, const char *context) {
    if (expr->expression_type == target) return expr;
    if (expr->expression_type == TYPE_STRING || target == TYPE_STRING) {
        type_error(expr->line, "Type mismatch in %s: cannot mix a string and a number", context);
    }
    if (target == TYPE_REAL) { // expr is TYPE_INTEGER
        BasicASTNode *conv = basic_create_node(BNODE_INT_TO_REAL);
        conv->left = expr;
        conv->expression_type = TYPE_REAL;
        return conv;
    }
    // target == TYPE_INTEGER, expr is TYPE_REAL
    type_error(expr->line, "Cannot use a real value in %s where an integer ('%%') variable is expected", context);
    return NULL; // unreachable
}

static void check_binary(BasicASTNode *node) {
    check_expr(node->left);
    check_expr(node->right);
    DataType lt = node->left->expression_type;
    DataType rt = node->right->expression_type;

    switch (node->op) {
        case BTOK_PLUS:
            if (lt == TYPE_STRING || rt == TYPE_STRING) {
                if (lt != TYPE_STRING || rt != TYPE_STRING) {
                    type_error(node->line, "Cannot use '+' between a string and a number");
                }
                node->expression_type = TYPE_STRING;
            } else if (lt == TYPE_REAL || rt == TYPE_REAL) {
                node->left = coerce_numeric(node->left, TYPE_REAL, "'+'");
                node->right = coerce_numeric(node->right, TYPE_REAL, "'+'");
                node->expression_type = TYPE_REAL;
            } else {
                node->expression_type = TYPE_INTEGER;
            }
            break;

        case BTOK_MINUS:
        case BTOK_MUL:
            if (lt == TYPE_STRING || rt == TYPE_STRING) {
                type_error(node->line, "Arithmetic operators require numbers, not strings");
            }
            if (lt == TYPE_REAL || rt == TYPE_REAL) {
                node->left = coerce_numeric(node->left, TYPE_REAL, "an arithmetic expression");
                node->right = coerce_numeric(node->right, TYPE_REAL, "an arithmetic expression");
                node->expression_type = TYPE_REAL;
            } else {
                node->expression_type = TYPE_INTEGER;
            }
            break;

        case BTOK_SLASH:
            if (lt == TYPE_STRING || rt == TYPE_STRING) {
                type_error(node->line, "'/' requires numbers, not strings");
            }
            node->left = coerce_numeric(node->left, TYPE_REAL, "'/'");
            node->right = coerce_numeric(node->right, TYPE_REAL, "'/'");
            node->expression_type = TYPE_REAL;
            break;

        case BTOK_EQ:
        case BTOK_LT:
        case BTOK_GT:
        case BTOK_LTE:
        case BTOK_GTE:
        case BTOK_NEQ:
            if ((lt == TYPE_STRING) != (rt == TYPE_STRING)) {
                type_error(node->line, "Cannot compare a string with a number");
            }
            if (lt != TYPE_STRING && (lt == TYPE_REAL || rt == TYPE_REAL)) {
                node->left = coerce_numeric(node->left, TYPE_REAL, "a comparison");
                node->right = coerce_numeric(node->right, TYPE_REAL, "a comparison");
            }
            node->expression_type = TYPE_INTEGER;
            break;

        case BTOK_AND:
        case BTOK_OR:
            // Bitwise, not short-circuit-logical, on plain integers - see
            // docs/BASIC.md's note on AND/OR (the same conflation classic
            // BASIC dialects themselves relied on via their -1-for-TRUE
            // convention; this dialect's comparisons push 0/1 instead,
            // which still composes correctly under bitwise AND/OR as long
            // as operands are themselves comparison results, 0, or 1).
            if (lt != TYPE_INTEGER || rt != TYPE_INTEGER) {
                type_error(node->line, "'AND'/'OR' require integer expressions");
            }
            node->expression_type = TYPE_INTEGER;
            break;

        default:
            break; // unreachable - the parser never builds any other op here
    }
}

static void check_expr(BasicASTNode *node) {
    switch (node->type) {
        case BNODE_NUMBER:
        case BNODE_STRING:
        case BNODE_VARIABLE:
        case BNODE_INT_TO_REAL:
            break; // already typed - leaves, or synthesized by this pass itself

        case BNODE_UNARY_OP:
            check_expr(node->left);
            if (node->op == BTOK_NOT) {
                if (node->left->expression_type != TYPE_INTEGER) {
                    type_error(node->line, "'NOT' requires an integer expression");
                }
            } else { // BTOK_MINUS
                if (node->left->expression_type == TYPE_STRING) {
                    type_error(node->line, "Cannot negate a string");
                }
            }
            node->expression_type = node->left->expression_type;
            break;

        case BNODE_BINARY_OP:
            check_binary(node);
            break;

        case BNODE_LET:
        case BNODE_PRINT:
        case BNODE_INPUT:
        case BNODE_IF:
        case BNODE_GOTO:
        case BNODE_GOSUB:
        case BNODE_RETURN:
        case BNODE_FOR:
        case BNODE_NEXT:
        case BNODE_END:
            break; // statements never appear inside an expression tree
    }
}

// STEP is the one place v1 needs a compile-time-constant expression (its
// SIGN picks LTE vs GTE at codegen time - see codegen.c) - a literal,
// optionally wrapped in a single leading unary minus.
static int is_constant_step(BasicASTNode *step) {
    if (step->type == BNODE_NUMBER) return 1;
    if (step->type == BNODE_UNARY_OP && step->op == BTOK_MINUS) return step->left->type == BNODE_NUMBER;
    return 0;
}

void basic_type_check(BasicASTNode *node) {
    for (; node; node = node->next) {
        switch (node->type) {
            case BNODE_LET:
                check_expr(node->left);
                node->left = coerce_numeric(node->left, sym_table[node->data.var_idx].type, "an assignment");
                break;

            case BNODE_PRINT:
                for (BasicASTNode *item = node->left; item; item = item->next) check_expr(item);
                break;

            case BNODE_INPUT:
                if (node->left) check_expr(node->left);
                break;

            case BNODE_IF:
                check_expr(node->left);
                if (node->left->expression_type == TYPE_STRING) {
                    type_error(node->line, "'IF' condition must be numeric, not a string");
                }
                basic_type_check(node->right);
                if (node->extra) basic_type_check(node->extra);
                break;

            case BNODE_GOTO:
            case BNODE_GOSUB:
                if (basic_find_line_index(node->data.num_value) == -1) {
                    type_error(node->line, "Undefined line number %d", node->data.num_value);
                }
                break;

            case BNODE_FOR: {
                DataType var_type = sym_table[node->data.var_idx].type;
                if (var_type == TYPE_STRING) {
                    type_error(node->line, "'FOR' loop variable '%s' cannot be a string",
                               sym_table[node->data.var_idx].name);
                }
                check_expr(node->left);
                node->left = coerce_numeric(node->left, var_type, "a 'FOR' start value");
                check_expr(node->right);
                node->right = coerce_numeric(node->right, var_type, "a 'FOR' end value");
                if (node->extra) {
                    check_expr(node->extra);
                    if (!is_constant_step(node->extra)) {
                        type_error(node->line, "'FOR' loop STEP must be a constant");
                    }
                    node->extra = coerce_numeric(node->extra, var_type, "a 'FOR' STEP value");
                }
                break;
            }

            case BNODE_NEXT:
            case BNODE_RETURN:
            case BNODE_END:
                break;

            case BNODE_UNARY_OP:
            case BNODE_BINARY_OP:
            case BNODE_NUMBER:
            case BNODE_STRING:
            case BNODE_VARIABLE:
            case BNODE_INT_TO_REAL:
                break; // expression nodes never appear at statement-list level
        }
    }
}
