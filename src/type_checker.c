#include <stdio.h>
#include <stdlib.h>
#include "type_checker.h"
#include "parser.h"
#include "error.h"

// char and string are representationally identical at runtime (both are
// string_pool[] indices) - only the VM's runtime length-1 check actually
// distinguishes them. So for assignment/comparison/concatenation purposes,
// treat them as freely interchangeable; only variable declarations and
// runtime storage actually enforce the length-1 constraint.
static int is_string_type(DataType t) {
    return t == TYPE_STRING || t == TYPE_CHAR;
}

void type_check(ASTNode *node) {
    if (!node) return;

    type_check(node->left);
    type_check(node->right);
    type_check(node->next);
    type_check(node->extra);

    switch (node->type) {
        case NODE_ASSIGN: {
            Symbol *sym = &sym_table[node->data.var_idx];
            if (sym->is_array) {
                if (node->left->expression_type != TYPE_INTEGER) {
                    fprintf(stderr, "%s:%d: Type Error: Array index must be integer\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
                if (!(is_string_type(node->right->expression_type) && is_string_type(sym->type))
                    && node->right->expression_type != sym->type) {
                    fprintf(stderr, "%s:%d: Type Error: Cannot assign expression to element of array '%s'\n",
                            get_current_filename(), node->line, sym->name);
                    fatal_abort();
                }
            } else if (!(is_string_type(node->left->expression_type) && is_string_type(sym->type))
                       && node->left->expression_type != sym->type) {
                fprintf(stderr, "%s:%d: Type Error: Cannot assign expression to variable '%s'\n",
                        get_current_filename(), node->line, sym->name);
                fatal_abort();
            }
            break;
        }

        case NODE_UNARY_OP:
            if (node->op == TOKEN_MINUS) {
                if (node->left->expression_type != TYPE_INTEGER) {
                    fprintf(stderr, "%s:%d: Type Error: Unary minus requires integer\n", get_current_filename(), node->line);
                    fatal_abort();
                }
                node->expression_type = TYPE_INTEGER;
            } else if (node->op == TOKEN_NOT) {
                if (node->left->expression_type != TYPE_BOOLEAN) {
                    fprintf(stderr, "%s:%d: Type Error: 'not' requires boolean\n", get_current_filename(), node->line);
                    fatal_abort();
                }
                node->expression_type = TYPE_BOOLEAN;
            }
            break;

        case NODE_BINARY_OP: {
            DataType left_t = node->left->expression_type;
            DataType right_t = node->right->expression_type;

            if (!(is_string_type(left_t) && is_string_type(right_t)) && left_t != right_t) {
                fprintf(stderr, "%s:%d: Type Error: Mismatched operand types in binary operation\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }

            if (node->op == TOKEN_AND || node->op == TOKEN_OR || node->op == TOKEN_XOR) {
                if (left_t != TYPE_BOOLEAN || right_t != TYPE_BOOLEAN) {
                    fprintf(stderr, "%s:%d: Type Error: Logical operators (and, or, xor) require boolean operands\n", 
                            get_current_filename(), node->line);
                    fatal_abort();
                }
                node->expression_type = TYPE_BOOLEAN;
            } else if (node->op == TOKEN_PLUS && is_string_type(left_t) && is_string_type(right_t)) {
                node->expression_type = TYPE_STRING; // concatenation always yields a string, even from chars
            } else if (node->op == TOKEN_PLUS || node->op == TOKEN_MINUS || 
                    node->op == TOKEN_MUL || node->op == TOKEN_DIV || 
                    node->op == TOKEN_DIV_KW || node->op == TOKEN_MOD) {
                if (left_t != TYPE_INTEGER || right_t != TYPE_INTEGER) {
                    fprintf(stderr, "%s:%d: Type Error: Arithmetic operations require integer operands%s\n", 
                            get_current_filename(), node->line,
                            node->op == TOKEN_PLUS ? " (or, for '+', string/char operands)" : "");
                    fatal_abort();
                }
                node->expression_type = TYPE_INTEGER;
            } else {
                // Relational operators (=, <, >, <=, >=, <>)
                if (!(is_string_type(left_t) && is_string_type(right_t))
                    && (left_t != TYPE_INTEGER || right_t != TYPE_INTEGER)) {
                    fprintf(stderr, "%s:%d: Type Error: Comparisons require integer, string, or char operands\n", get_current_filename(), node->line);
                    fatal_abort();
                }
                node->expression_type = TYPE_BOOLEAN;
            }
            break;
        }

       case NODE_WRITELN:
            for (ASTNode *arg = node->left; arg; arg = arg->next) {
                if (arg->expression_type == TYPE_UNKNOWN) {
                    fprintf(stderr, "%s:%d: Type Error: Cannot print invalid expression\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
            }
            break;

        case NODE_READLN:
            // Ensures target is a defined variable
            if (node->data.var_idx < 0 || node->data.var_idx >= sym_count) {
                fprintf(stderr, "%s:%d: Type Error: Invalid read target\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_IF:
            if (node->left->expression_type != TYPE_BOOLEAN) {
                fprintf(stderr, "%s:%d: Type Error: 'if' condition must be boolean\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_WHILE:
            if (node->left->expression_type != TYPE_BOOLEAN) {
                fprintf(stderr, "%s:%d: Type Error: 'while' condition must be boolean\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_REPEAT:
            if (node->right->expression_type != TYPE_BOOLEAN) {
                fprintf(stderr, "%s:%d: Type Error: 'until' condition must be boolean\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_FOR:
            if (sym_table[node->data.var_idx].type != TYPE_INTEGER) {
                fprintf(stderr, "%s:%d: Type Error: 'for' loop variable must be integer\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            if (node->left->expression_type != TYPE_INTEGER || node->right->expression_type != TYPE_INTEGER) {
                fprintf(stderr, "%s:%d: Type Error: 'for' loop bounds must be integer\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_ARRAY_ACCESS:
            if (node->left->expression_type != TYPE_INTEGER) {
                fprintf(stderr, "%s:%d: Type Error: Array index must be integer\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_LOCAL_ASSIGN:
            if (!(is_string_type(node->left->expression_type) && is_string_type(node->expression_type))
                && node->left->expression_type != node->expression_type) {
                fprintf(stderr, "%s:%d: Type Error: Cannot assign expression to local variable\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_CALL: {
            // Argument count is already guaranteed correct by the parser
            // (it errors immediately at the call site if it doesn't match
            // proc_table[...].param_count), so this only needs to check types.
            ProcSymbol *proc = &proc_table[node->data.var_idx];
            int i = 0;
            for (ASTNode *arg = node->left; arg; arg = arg->next, i++) {
                DataType expected = proc->param_types[i];
                DataType actual = arg->expression_type;
                if (!(is_string_type(expected) && is_string_type(actual)) && expected != actual) {
                    fprintf(stderr, "%s:%d: Type Error: Argument %d to procedure '%s' has the wrong type\n",
                            get_current_filename(), node->line, i + 1, proc->name);
                    fatal_abort();
                }
            }
            break;
        }

        default:
            break;
    }
}

