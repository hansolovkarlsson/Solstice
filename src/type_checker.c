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

// A specific enumerated type is encoded as TYPE_ENUM_BASE + its
// enum_types[] index (see the comment in common.h) - any DataType value
// at or above that threshold is some enum type. Two DataType values
// being equal (left_t == right_t, used throughout this file) already
// means "the exact same enum type", so no further lookup is needed to
// tell two different enum types apart.
static int is_enum_type(DataType t) {
    return t >= TYPE_ENUM_BASE;
}

// Wraps *child in an implicit int->real widening conversion (Pascal's
// automatic integer-to-real promotion) and updates *child to point at the
// wrapper. Preserves *child's own ->next (if it has one - e.g. it's a
// member of an argument list or statement list) by moving that link onto
// the new wrapper instead, so this is safe to call on any child pointer,
// not just binary-op operands. No-op guard is the caller's job (only call
// this when you've already confirmed the child is TYPE_INTEGER).
static void widen_to_real(ASTNode **child) {
    if ((*child)->expression_type != TYPE_INTEGER) return; // already real - nothing to do
    ASTNode *original = *child;
    ASTNode *wrapper = create_node(NODE_INT_TO_REAL);
    wrapper->left = original;
    wrapper->expression_type = TYPE_REAL;
    wrapper->line = original->line;
    wrapper->next = original->next;
    original->next = NULL;
    *child = wrapper;
}

// Assignment-compatibility check with implicit widening: if *value is
// integer-typed and target is real, widens *value (mutating it through
// the pointer) and returns 1 - the caller's own type-mismatch condition
// should short-circuit past its error in that case. Returns 0 (no
// widening performed) for every other combination, including real-typed
// value into an integer target - Pascal requires an explicit trunc()/
// round() for that narrowing, so this deliberately doesn't paper over it.
static int try_widen_for_assignment(ASTNode **value, DataType target) {
    if ((*value)->expression_type == TYPE_INTEGER && target == TYPE_REAL) {
        widen_to_real(value);
        return 1;
    }
    return 0;
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
                    && node->right->expression_type != sym->type
                    && !try_widen_for_assignment(&node->right, sym->type)) {
                    fprintf(stderr, "%s:%d: Type Error: Cannot assign expression to element of array '%s'\n",
                            get_current_filename(), node->line, sym->name);
                    fatal_abort();
                }
            } else if (!(is_string_type(node->left->expression_type) && is_string_type(sym->type))
                       && node->left->expression_type != sym->type
                       && !try_widen_for_assignment(&node->left, sym->type)) {
                fprintf(stderr, "%s:%d: Type Error: Cannot assign expression to variable '%s'\n",
                        get_current_filename(), node->line, sym->name);
                fatal_abort();
            }
            break;
        }

        case NODE_UNARY_OP:
            if (node->op == TOKEN_MINUS) {
                if (node->left->expression_type != TYPE_INTEGER && node->left->expression_type != TYPE_REAL) {
                    fprintf(stderr, "%s:%d: Type Error: Unary minus requires integer or real\n", get_current_filename(), node->line);
                    fatal_abort();
                }
                node->expression_type = node->left->expression_type; // preserves int or real
            } else if (node->op == TOKEN_NOT) {
                // 'not' on boolean is logical; on integer it's bitwise
                // (Pascal convention, e.g. Turbo Pascal/Delphi/Free Pascal).
                if (node->left->expression_type == TYPE_BOOLEAN) {
                    node->expression_type = TYPE_BOOLEAN;
                } else if (node->left->expression_type == TYPE_INTEGER) {
                    node->expression_type = TYPE_INTEGER;
                } else {
                    fprintf(stderr, "%s:%d: Type Error: 'not' requires boolean or integer\n", get_current_filename(), node->line);
                    fatal_abort();
                }
            } else if (node->op == TOKEN_ABS || node->op == TOKEN_SQR) {
                if (node->left->expression_type != TYPE_INTEGER && node->left->expression_type != TYPE_REAL) {
                    fprintf(stderr, "%s:%d: Type Error: '%s' requires an integer or real argument\n",
                            get_current_filename(), node->line, node->op == TOKEN_ABS ? "abs" : "sqr");
                    fatal_abort();
                }
                node->expression_type = node->left->expression_type; // preserves int or real
            } else if (node->op == TOKEN_ORD) {
                if (!is_string_type(node->left->expression_type)) {
                    fprintf(stderr, "%s:%d: Type Error: 'ord' requires a char or string argument\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
                node->expression_type = TYPE_INTEGER;
            } else if (node->op == TOKEN_CHR) {
                if (node->left->expression_type != TYPE_INTEGER) {
                    fprintf(stderr, "%s:%d: Type Error: 'chr' requires an integer argument\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
                node->expression_type = TYPE_CHAR;
            } else if (node->op == TOKEN_TRUNC || node->op == TOKEN_ROUND) {
                if (node->left->expression_type != TYPE_REAL) {
                    fprintf(stderr, "%s:%d: Type Error: '%s' requires a real argument\n",
                            get_current_filename(), node->line, node->op == TOKEN_TRUNC ? "trunc" : "round");
                    fatal_abort();
                }
                node->expression_type = TYPE_INTEGER;
            } else if (node->op == TOKEN_SQRT || node->op == TOKEN_SIN || node->op == TOKEN_COS ||
                       node->op == TOKEN_ARCTAN || node->op == TOKEN_EXP || node->op == TOKEN_LN) {
                if (node->left->expression_type != TYPE_INTEGER && node->left->expression_type != TYPE_REAL) {
                    fprintf(stderr, "%s:%d: Type Error: '%s' requires an integer or real argument\n",
                            get_current_filename(), node->line,
                            node->op == TOKEN_SQRT ? "sqrt" : node->op == TOKEN_SIN ? "sin" :
                            node->op == TOKEN_COS ? "cos" : node->op == TOKEN_ARCTAN ? "arctan" :
                            node->op == TOKEN_EXP ? "exp" : "ln");
                    fatal_abort();
                }
                widen_to_real(&node->left); // no-op if already real
                node->expression_type = TYPE_REAL;
            }
            break;

        case NODE_BINARY_OP: {
            DataType left_t = node->left->expression_type;
            DataType right_t = node->right->expression_type;
            int mixed_numeric = (left_t == TYPE_INTEGER && right_t == TYPE_REAL)
                              || (left_t == TYPE_REAL && right_t == TYPE_INTEGER);
            // Ordinal arithmetic on an enum ('Red + 1' / 'Red - 1') -
            // this is how succ()/pred() are implemented for enums (see
            // their desugaring to '+1'/'-1' in parser.c's factor()), and
            // is also accepted written directly. The result stays the
            // SAME enum type - see the branch below.
            int enum_ordinal_arith = is_enum_type(left_t) && right_t == TYPE_INTEGER
                                   && (node->op == TOKEN_PLUS || node->op == TOKEN_MINUS);

            if (!(is_string_type(left_t) && is_string_type(right_t)) && !mixed_numeric
                && !enum_ordinal_arith && left_t != right_t) {
                fprintf(stderr, "%s:%d: Type Error: Mismatched operand types in binary operation\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }

            if (node->op == TOKEN_AND || node->op == TOKEN_OR || node->op == TOKEN_XOR) {
                // and/or/xor are logical between two booleans, bitwise
                // between two integers (Pascal convention) - real never
                // participates, so a mixed int/real pair correctly falls
                // through to the error below (matching neither case).
                if (left_t == TYPE_BOOLEAN && right_t == TYPE_BOOLEAN) {
                    node->expression_type = TYPE_BOOLEAN;
                } else if (left_t == TYPE_INTEGER && right_t == TYPE_INTEGER) {
                    node->expression_type = TYPE_INTEGER;
                } else {
                    fprintf(stderr, "%s:%d: Type Error: 'and'/'or'/'xor' require both operands boolean, or both integer\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
            } else if (node->op == TOKEN_PLUS && is_string_type(left_t) && is_string_type(right_t)) {
                node->expression_type = TYPE_STRING; // concatenation always yields a string, even from chars
            } else if (node->op == TOKEN_DIV_KW || node->op == TOKEN_MOD ||
                       node->op == TOKEN_SHL || node->op == TOKEN_SHR) {
                // div/mod/shl/shr are integer-only in Pascal - real
                // doesn't participate at all, not even via widening.
                if (left_t != TYPE_INTEGER || right_t != TYPE_INTEGER) {
                    fprintf(stderr, "%s:%d: Type Error: 'div'/'mod'/'shl'/'shr' require integer operands\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
                node->expression_type = TYPE_INTEGER;
            } else if (node->op == TOKEN_DIV) {
                // '/' always produces a real result in Pascal, even for
                // two integer operands (5 / 2 = 2.5, not 2) - 'div' is
                // the integer-division operator, handled above.
                if ((left_t != TYPE_INTEGER && left_t != TYPE_REAL) || (right_t != TYPE_INTEGER && right_t != TYPE_REAL)) {
                    fprintf(stderr, "%s:%d: Type Error: '/' requires integer or real operands\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
                widen_to_real(&node->left);
                widen_to_real(&node->right);
                node->expression_type = TYPE_REAL;
            } else if (node->op == TOKEN_POW) {
                // '**' always produces real, same reasoning as '/' above.
                if ((left_t != TYPE_INTEGER && left_t != TYPE_REAL) || (right_t != TYPE_INTEGER && right_t != TYPE_REAL)) {
                    fprintf(stderr, "%s:%d: Type Error: '**' requires integer or real operands\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
                widen_to_real(&node->left);
                widen_to_real(&node->right);
                node->expression_type = TYPE_REAL;
            } else if (node->op == TOKEN_PLUS || node->op == TOKEN_MINUS || node->op == TOKEN_MUL) {
                if (enum_ordinal_arith) {
                    // No range-checking against the enum's actual value
                    // count (e.g. succ() of its last value) - same as
                    // this compiler's ordinary integer arithmetic never
                    // overflow-checks either.
                    node->expression_type = left_t;
                } else if ((left_t != TYPE_INTEGER && left_t != TYPE_REAL) || (right_t != TYPE_INTEGER && right_t != TYPE_REAL)) {
                    fprintf(stderr, "%s:%d: Type Error: Arithmetic operations require integer or real operands%s\n",
                            get_current_filename(), node->line,
                            node->op == TOKEN_PLUS ? " (or, for '+', string/char operands)" : "");
                    fatal_abort();
                } else if (left_t == TYPE_REAL || right_t == TYPE_REAL) {
                    widen_to_real(&node->left);
                    widen_to_real(&node->right);
                    node->expression_type = TYPE_REAL;
                } else {
                    node->expression_type = TYPE_INTEGER;
                }
            } else {
                // Relational operators (=, <, >, <=, >=, <>). Boolean is
                // ordinal in Pascal (false < true), so all six are valid
                // between two booleans too - represented as a plain 0/1
                // int at the VM level, exactly like integer, so the
                // existing integer comparison opcodes already handle this
                // correctly; no codegen changes needed for that case.
                int both_bool = (left_t == TYPE_BOOLEAN && right_t == TYPE_BOOLEAN);
                int both_numeric = (left_t == TYPE_INTEGER || left_t == TYPE_REAL)
                                 && (right_t == TYPE_INTEGER || right_t == TYPE_REAL);
                // Two values of the SAME enum type compare the same way
                // integers do (ordinal ordering) - the early mismatched-
                // operand-types guard above already rejects two
                // DIFFERENT enum types here, since left_t != right_t in
                // that case.
                int both_enum = (left_t == right_t && is_enum_type(left_t));
                if (!(is_string_type(left_t) && is_string_type(right_t)) && !both_bool && !both_numeric && !both_enum) {
                    fprintf(stderr, "%s:%d: Type Error: Comparisons require integer, real, boolean, string, char, or matching enumerated-type operands\n", get_current_filename(), node->line);
                    fatal_abort();
                }
                if (both_numeric && (left_t == TYPE_REAL || right_t == TYPE_REAL)) {
                    widen_to_real(&node->left);
                    widen_to_real(&node->right);
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

        case NODE_LOCAL_FOR:
            // The loop variable's own type was already validated at
            // parse time (current_locals[] - a parser-only structure -
            // isn't accessible from this pass). 'right' is always a
            // NODE_LOCAL_VAR reference to an integer hidden slot by
            // construction, so only 'left' (the start bound, an
            // arbitrary user expression) genuinely needs checking here.
            if (node->left->expression_type != TYPE_INTEGER || node->right->expression_type != TYPE_INTEGER) {
                fprintf(stderr, "%s:%d: Type Error: 'for' loop bounds must be integer\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_LOCAL_READLN:
            // Valid by construction - the parser only builds this node
            // for an already-resolved local variable of a known type.
            break;

        case NODE_ARRAY_ACCESS:
            if (node->left->expression_type != TYPE_INTEGER) {
                fprintf(stderr, "%s:%d: Type Error: Array index must be integer\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_REF_ARRAY_ACCESS:
            if (node->left->expression_type != TYPE_INTEGER) {
                fprintf(stderr, "%s:%d: Type Error: Array index must be integer\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_REF_ARRAY_ASSIGN:
            if (node->left->expression_type != TYPE_INTEGER) {
                fprintf(stderr, "%s:%d: Type Error: Array index must be integer\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            if (!(is_string_type(node->right->expression_type) && is_string_type(node->expression_type))
                && node->right->expression_type != node->expression_type
                && !try_widen_for_assignment(&node->right, node->expression_type)) {
                fprintf(stderr, "%s:%d: Type Error: Cannot assign expression to array element\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_LOCAL_ASSIGN:
            if (!(is_string_type(node->left->expression_type) && is_string_type(node->expression_type))
                && node->left->expression_type != node->expression_type
                && !try_widen_for_assignment(&node->left, node->expression_type)) {
                fprintf(stderr, "%s:%d: Type Error: Cannot assign expression to local variable\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_BUILTIN_CALL:
            switch (node->op) {
                case TOKEN_LENGTH:
                    if (!is_string_type(node->left->expression_type)) {
                        fprintf(stderr, "%s:%d: Type Error: 'length' requires a char or string argument\n",
                                get_current_filename(), node->line);
                        fatal_abort();
                    }
                    node->expression_type = TYPE_INTEGER;
                    break;
                case TOKEN_COPY:
                    if (!is_string_type(node->left->expression_type)) {
                        fprintf(stderr, "%s:%d: Type Error: 'copy'/'mid' requires a char or string first argument\n",
                                get_current_filename(), node->line);
                        fatal_abort();
                    }
                    if (node->right->expression_type != TYPE_INTEGER || node->extra->expression_type != TYPE_INTEGER) {
                        fprintf(stderr, "%s:%d: Type Error: 'copy'/'mid' requires integer start/count arguments\n",
                                get_current_filename(), node->line);
                        fatal_abort();
                    }
                    node->expression_type = TYPE_STRING;
                    break;
                case TOKEN_POS:
                    if (!is_string_type(node->left->expression_type) || !is_string_type(node->right->expression_type)) {
                        fprintf(stderr, "%s:%d: Type Error: 'pos'/'inpos' requires char or string arguments\n",
                                get_current_filename(), node->line);
                        fatal_abort();
                    }
                    node->expression_type = TYPE_INTEGER;
                    break;
                case TOKEN_UPCASE:
                    if (!is_string_type(node->left->expression_type)) {
                        fprintf(stderr, "%s:%d: Type Error: 'upcase' requires a char or string argument\n",
                                get_current_filename(), node->line);
                        fatal_abort();
                    }
                    node->expression_type = TYPE_CHAR;
                    break;
                case TOKEN_UPPERCASE:
                case TOKEN_LOWERCASE:
                    if (!is_string_type(node->left->expression_type)) {
                        fprintf(stderr, "%s:%d: Type Error: 'uppercase'/'lowercase' requires a char or string argument\n",
                                get_current_filename(), node->line);
                        fatal_abort();
                    }
                    node->expression_type = TYPE_STRING;
                    break;
                case TOKEN_LEFT:
                case TOKEN_RIGHT:
                    if (!is_string_type(node->left->expression_type)) {
                        fprintf(stderr, "%s:%d: Type Error: 'left'/'right' requires a char or string first argument\n",
                                get_current_filename(), node->line);
                        fatal_abort();
                    }
                    if (node->right->expression_type != TYPE_INTEGER) {
                        fprintf(stderr, "%s:%d: Type Error: 'left'/'right' requires an integer count argument\n",
                                get_current_filename(), node->line);
                        fatal_abort();
                    }
                    node->expression_type = TYPE_STRING;
                    break;
                case TOKEN_POWER: {
                    DataType bt = node->left->expression_type;
                    DataType et = node->right->expression_type;
                    if ((bt != TYPE_INTEGER && bt != TYPE_REAL) || (et != TYPE_INTEGER && et != TYPE_REAL)) {
                        fprintf(stderr, "%s:%d: Type Error: 'power' requires integer or real arguments\n",
                                get_current_filename(), node->line);
                        fatal_abort();
                    }
                    widen_to_real(&node->left);
                    widen_to_real(&node->right);
                    node->expression_type = TYPE_REAL;
                    break;
                }
                default:
                    break;
            }
            break;

        case NODE_STRING_INDEX:
        case NODE_LOCAL_STRING_INDEX:
            if (node->left->expression_type != TYPE_INTEGER) {
                fprintf(stderr, "%s:%d: Type Error: String index must be integer\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_STRING_INDEX_ASSIGN:
        case NODE_LOCAL_STRING_INDEX_ASSIGN:
            if (node->left->expression_type != TYPE_INTEGER) {
                fprintf(stderr, "%s:%d: Type Error: String index must be integer\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            if (!is_string_type(node->right->expression_type)) {
                fprintf(stderr, "%s:%d: Type Error: Cannot assign a non-char/string value to a string character\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_ARRAY_ACCESS_2D:
            if (node->left->expression_type != TYPE_INTEGER || node->right->expression_type != TYPE_INTEGER) {
                fprintf(stderr, "%s:%d: Type Error: Array indices must be integer\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_ARRAY_ASSIGN_2D: {
            if (node->left->expression_type != TYPE_INTEGER || node->right->expression_type != TYPE_INTEGER) {
                fprintf(stderr, "%s:%d: Type Error: Array indices must be integer\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            Symbol *sym = &sym_table[node->data.var_idx];
            if (!(is_string_type(node->extra->expression_type) && is_string_type(sym->type))
                && node->extra->expression_type != sym->type
                && !try_widen_for_assignment(&node->extra, sym->type)) {
                fprintf(stderr, "%s:%d: Type Error: Cannot assign expression to element of array '%s'\n",
                        get_current_filename(), node->line, sym->name);
                fatal_abort();
            }
            break;
        }

        case NODE_WRITE_ARG:
            if (node->right && node->right->expression_type != TYPE_INTEGER) {
                fprintf(stderr, "%s:%d: Type Error: write/writeln field width must be integer\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            if (node->extra) {
                if (node->extra->expression_type != TYPE_INTEGER) {
                    fprintf(stderr, "%s:%d: Type Error: write/writeln field precision must be integer\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
                if (node->left->expression_type != TYPE_REAL) {
                    fprintf(stderr, "%s:%d: Type Error: write/writeln field precision (':width:precision') is only valid for real values\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
            }
            node->expression_type = node->left->expression_type;
            break;

        case NODE_CALL: {
            // Argument count is already guaranteed correct by the parser
            // (it errors immediately at the call site if it doesn't match
            // proc_table[...].param_count), so this only needs to check types.
            ProcSymbol *proc = &proc_table[node->data.var_idx];
            int i = 0;
            ASTNode **arg_ptr = &node->left;
            while (*arg_ptr) {
                DataType expected = proc->param_types[i];
                DataType actual = (*arg_ptr)->expression_type;
                if (!(is_string_type(expected) && is_string_type(actual)) && expected != actual
                    && !try_widen_for_assignment(arg_ptr, expected)) {
                    fprintf(stderr, "%s:%d: Type Error: Argument %d to procedure '%s' has the wrong type\n",
                            get_current_filename(), node->line, i + 1, proc->name);
                    fatal_abort();
                }
                arg_ptr = &(*arg_ptr)->next;
                i++;
            }
            break;
        }

        default:
            break;
    }
}

