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
// enum_types[] index (see the comment in common.h) - a BOUNDED range
// check, not a bare '>=', since a pointer type's own encoding
// (TYPE_POINTER_BASE + its own index) immediately follows the enum
// range and would otherwise also match. Two DataType values being equal
// (left_t == right_t, used throughout this file) already means "the
// exact same enum type", so no further lookup is needed to tell two
// different enum types apart.
static int is_enum_type(DataType t) {
    return t >= TYPE_ENUM_BASE && t < TYPE_ENUM_BASE + MAX_ENUM_TYPES;
}

// Same bounded-range idea as is_enum_type() above, for a specific
// pointer type ('type PFoo = ^Target;', encoded as TYPE_POINTER_BASE +
// its pointer_types[] index in parser.c). Two DataType values being
// equal already means "the exact same pointer type" (see is_enum_type()'s
// comment) - this compiler never allows assigning/comparing two
// DIFFERENT pointer types directly, even if they happen to target the
// same thing - EXCEPT two class pointer types in an ancestor/descendant
// relationship (see class_type_is_subtype_of(), used below in
// try_widen_for_assignment() and the '='/'<>' pointer-comparison case):
// an instance of a subclass can be assigned/passed/compared wherever
// its ancestor class is expected, since a subclass's own fields always
// start with an exact copy of its ancestor's (see
// docs/LANGUAGE.md#classes).
static int is_pointer_type(DataType t) {
    return t >= TYPE_POINTER_BASE && t < TYPE_POINTER_BASE + MAX_POINTER_TYPES;
}

// Same bounded-range idea, for a specific NAMED procedural type ('type
// TProc = procedure(...);', encoded as TYPE_PROC_BASE + its
// proc_types[] index in parser.c) - a procedural-type value behaves
// like a pointer for comparison purposes too (nil-compatible, '='/'<>'
// only), so is_proc_type() joins is_pointer_type() at every one of that
// check's own call sites below.
static int is_proc_type(DataType t) {
    return t >= TYPE_PROC_BASE && t < TYPE_PROC_BASE + MAX_PROC_TYPES;
}

// True if 'value' (an expression's own type) can be nil against 'target'
// (an assignment/argument's expected type) - target is a pointer OR a
// procedural type, and value is literally TYPE_NIL. Factored out since
// this exact "is X a pointer/proc type, AND is the other side TYPE_NIL"
// pair is checked at every assignment/argument site below (NODE_ASSIGN's
// two forms, NODE_LOCAL_ASSIGN, NODE_HEAP_FIELD_ASSIGN) - same idea as
// the '='/'<>' comparison case above, just as a named helper instead of
// inlined a 5th time.
static int nil_compatible(DataType target, DataType value) {
    return (is_pointer_type(target) || is_proc_type(target)) && value == TYPE_NIL;
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
// should short-circuit past its error in that case. Also accepts a class
// instance assigned/passed where an ANCESTOR class is expected (see
// class_type_is_subtype_of(), parser.c - exported via parser.h) - no
// wrapper needed there, since a subclass instance's pointer value is
// already representationally identical to its ancestor's (see
// docs/LANGUAGE.md#classes). This is also what makes passing 'self' for
// an INHERITED method call work: self's parameter is typed as the
// method's OWN declaring (ancestor) class, and the accessing instance's
// type is a subclass of that. Returns 0 (no widening/acceptance) for
// every other combination, including real-typed value into an integer
// target - Pascal requires an explicit trunc()/round() for that
// narrowing, so this deliberately doesn't paper over it.
static int try_widen_for_assignment(ASTNode **value, DataType target) {
    if ((*value)->expression_type == TYPE_INTEGER && target == TYPE_REAL) {
        widen_to_real(value);
        return 1;
    }
    if (class_type_is_subtype_of((*value)->expression_type, target)) {
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
            if (sym->type == TYPE_FILE) {
                // Both sides being TYPE_FILE would otherwise pass the
                // ordinary type-match check below and silently compile
                // to a meaningless OP_STORE/OP_LOAD pair - a file
                // variable's real state lives in vm_open_files[],
                // indexed by its OWN sym_table index (see TYPE_FILE),
                // not in the plain storage slot this assignment would
                // actually copy.
                fprintf(stderr, "%s:%d: Type Error: A file variable can't be assigned directly - use assign/reset/rewrite/close\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            if (sym->is_array) {
                if (node->left->expression_type != TYPE_INTEGER) {
                    fprintf(stderr, "%s:%d: Type Error: Array index must be integer\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
                if (!(is_string_type(node->right->expression_type) && is_string_type(sym->type))
                    && !nil_compatible(sym->type, node->right->expression_type)
                    && node->right->expression_type != sym->type
                    && !try_widen_for_assignment(&node->right, sym->type)) {
                    fprintf(stderr, "%s:%d: Type Error: Cannot assign expression to element of array '%s'\n",
                            get_current_filename(), node->line, sym->name);
                    fatal_abort();
                }
            } else if (!(is_string_type(node->left->expression_type) && is_string_type(sym->type))
                       && !nil_compatible(sym->type, node->left->expression_type)
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
            if (left_t == TYPE_FILE || right_t == TYPE_FILE) {
                // Not defined for file variables in standard Pascal
                // either - no operator, including '='/'<>', applies to
                // them (see assign/reset/rewrite/close/read/write/eof
                // instead - the only legal ways to use one).
                fprintf(stderr, "%s:%d: Type Error: Operators aren't defined for file variables\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            if (is_pointer_type(left_t) || is_pointer_type(right_t) || is_proc_type(left_t) || is_proc_type(right_t)
                || left_t == TYPE_NIL || right_t == TYPE_NIL) {
                // Standard Pascal defines only '=' and '<>' for pointers
                // (no arithmetic, no ordering) - handled entirely here,
                // bypassing every other branch below, since none of them
                // know what to do with a pointer/procedural-type/TYPE_NIL
                // operand either. A NAMED procedural-type value behaves
                // exactly like a pointer here (see is_proc_type()'s own
                // comment). TYPE_NIL (the literal 'nil') is compatible
                // with ANY pointer or procedural type (or with itself,
                // 'nil = nil') - unlike every other type pair in this
                // compiler, which requires an EXACT DataType match (see
                // is_enum_type()'s comment: equal DataTypes already mean
                // "the same declared type").
                int compatible = (left_t == right_t)
                               || (is_pointer_type(left_t) && right_t == TYPE_NIL)
                               || (left_t == TYPE_NIL && is_pointer_type(right_t))
                               || (is_proc_type(left_t) && right_t == TYPE_NIL)
                               || (left_t == TYPE_NIL && is_proc_type(right_t))
                               || class_type_is_subtype_of(left_t, right_t)
                               || class_type_is_subtype_of(right_t, left_t);
                if (!compatible) {
                    fprintf(stderr, "%s:%d: Type Error: Cannot compare pointers/procedural values of different types\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
                if (node->op != TOKEN_EQ && node->op != TOKEN_NEQ) {
                    fprintf(stderr, "%s:%d: Type Error: Only '=' and '<>' are defined for pointers/procedural values\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
                node->expression_type = TYPE_BOOLEAN;
                break;
            }
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
                if (left_t == TYPE_SET && right_t == TYPE_SET) {
                    // + is union, - is difference, * is intersection -
                    // see codegen.c's NODE_BINARY_OP case for how each
                    // reuses existing bitwise opcodes (no new ones). A
                    // set combined with a non-set is already rejected
                    // above (the general left_t != right_t guard), so
                    // there's nothing to check here beyond "both sets".
                    node->expression_type = TYPE_SET;
                } else if (enum_ordinal_arith) {
                    // No range-checking against the enum's actual value
                    // count (e.g. succ() of its last value) - same as
                    // this compiler's ordinary integer arithmetic never
                    // overflow-checks either.
                    node->expression_type = left_t;
                } else if ((left_t != TYPE_INTEGER && left_t != TYPE_REAL) || (right_t != TYPE_INTEGER && right_t != TYPE_REAL)) {
                    fprintf(stderr, "%s:%d: Type Error: Arithmetic operations require integer or real operands%s\n",
                            get_current_filename(), node->line,
                            node->op == TOKEN_PLUS ? " (or, for '+', string/char operands, or two sets)" : "");
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
                // Two sets: '=' and '<>' are ordinary bitmask equality
                // (no codegen change needed - two ints being equal
                // already means the same bits are set); '<=' and '>='
                // are subset-or-equal / superset-or-equal (real
                // codegen support, see codegen.c). '<' and '>' aren't
                // defined for sets in standard Pascal at all - rejected
                // explicitly below, rather than silently doing a
                // meaningless raw-int comparison.
                int both_set = (left_t == TYPE_SET && right_t == TYPE_SET);
                if (both_set && (node->op == TOKEN_LT || node->op == TOKEN_GT)) {
                    fprintf(stderr, "%s:%d: Type Error: '<'/'>' aren't defined for sets - use '<='/'>=' for subset/superset, or '='/'<>' for equality\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
                if (!(is_string_type(left_t) && is_string_type(right_t)) && !both_bool && !both_numeric && !both_enum && !both_set) {
                    fprintf(stderr, "%s:%d: Type Error: Comparisons require integer, real, boolean, string, char, matching enumerated-type, or matching set operands\n", get_current_filename(), node->line);
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

        case NODE_REF_ARRAY_ACCESS_2D:
            if (node->left->expression_type != TYPE_INTEGER || node->right->expression_type != TYPE_INTEGER) {
                fprintf(stderr, "%s:%d: Type Error: Array index must be integer\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_REF_ARRAY_ASSIGN_2D:
            if (node->left->expression_type != TYPE_INTEGER || node->right->expression_type != TYPE_INTEGER) {
                fprintf(stderr, "%s:%d: Type Error: Array index must be integer\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            if (!(is_string_type(node->extra->expression_type) && is_string_type(node->expression_type))
                && node->extra->expression_type != node->expression_type
                && !try_widen_for_assignment(&node->extra, node->expression_type)) {
                fprintf(stderr, "%s:%d: Type Error: Cannot assign expression to array element\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_REF_ARRAY_ACCESS_ND:
            for (ASTNode *idx = node->left; idx; idx = idx->next) {
                if (idx->expression_type != TYPE_INTEGER) {
                    fprintf(stderr, "%s:%d: Type Error: Array index must be integer\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
            }
            break;

        case NODE_REF_ARRAY_ASSIGN_ND:
            for (ASTNode *idx = node->left; idx; idx = idx->next) {
                if (idx->expression_type != TYPE_INTEGER) {
                    fprintf(stderr, "%s:%d: Type Error: Array index must be integer\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
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
                && !nil_compatible(node->expression_type, node->left->expression_type)
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
                case TOKEN_PARAMSTR:
                    if (node->left->expression_type != TYPE_INTEGER) {
                        fprintf(stderr, "%s:%d: Type Error: 'paramstr' requires an integer argument\n",
                                get_current_filename(), node->line);
                        fatal_abort();
                    }
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

        case NODE_ARRAY_ACCESS_ND:
            for (ASTNode *idx = node->left; idx; idx = idx->next) {
                if (idx->expression_type != TYPE_INTEGER) {
                    fprintf(stderr, "%s:%d: Type Error: Array indices must be integer\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
            }
            break;

        case NODE_ARRAY_ASSIGN_ND: {
            for (ASTNode *idx = node->left; idx; idx = idx->next) {
                if (idx->expression_type != TYPE_INTEGER) {
                    fprintf(stderr, "%s:%d: Type Error: Array indices must be integer\n",
                            get_current_filename(), node->line);
                    fatal_abort();
                }
            }
            Symbol *sym = &sym_table[node->data.var_idx];
            if (!(is_string_type(node->right->expression_type) && is_string_type(sym->type))
                && node->right->expression_type != sym->type
                && !try_widen_for_assignment(&node->right, sym->type)) {
                fprintf(stderr, "%s:%d: Type Error: Cannot assign expression to element of array '%s'\n",
                        get_current_filename(), node->line, sym->name);
                fatal_abort();
            }
            break;
        }

        case NODE_ARRAY_RECORD_FIELD_ACCESS:
            if (node->left->expression_type != TYPE_INTEGER) {
                fprintf(stderr, "%s:%d: Type Error: Array index must be integer\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_ARRAY_RECORD_FIELD_ASSIGN:
            if (node->left->expression_type != TYPE_INTEGER) {
                fprintf(stderr, "%s:%d: Type Error: Array index must be integer\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            if (!(is_string_type(node->right->expression_type) && is_string_type(node->expression_type))
                && node->right->expression_type != node->expression_type
                && !try_widen_for_assignment(&node->right, node->expression_type)) {
                fprintf(stderr, "%s:%d: Type Error: Cannot assign expression to record array field\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_HEAP_FIELD_ACCESS:
            // Valid by construction - parser.c's parse_heap_deref_read()
            // only ever builds this from an already-resolved pointer-
            // typed base and an already-resolved field, exactly like
            // NODE_FILE_OP's own comment explains for its own no-op case.
            break;

        case NODE_HEAP_FIELD_ASSIGN:
            if (!(is_string_type(node->right->expression_type) && is_string_type(node->expression_type))
                && !nil_compatible(node->expression_type, node->right->expression_type)
                && node->right->expression_type != node->expression_type
                && !try_widen_for_assignment(&node->right, node->expression_type)) {
                fprintf(stderr, "%s:%d: Type Error: Cannot assign expression to pointer dereference\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_HEAP_ARRAY_FIELD_ACCESS:
            // Valid by construction, same reasoning as NODE_HEAP_FIELD_
            // ACCESS above - resolve_heap_deref_step() only ever builds
            // this from an already-resolved array field and an
            // already-range-checked index.
            break;

        case NODE_HEAP_ARRAY_FIELD_ASSIGN:
            if (!(is_string_type(node->right->expression_type) && is_string_type(node->expression_type))
                && !nil_compatible(node->expression_type, node->right->expression_type)
                && node->right->expression_type != node->expression_type
                && !try_widen_for_assignment(&node->right, node->expression_type)) {
                fprintf(stderr, "%s:%d: Type Error: Cannot assign expression to array field element\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_WRITE_ARG:
            if (node->left->expression_type == TYPE_SET) {
                // Standard Pascal defines no textual representation for a
                // set at all - rejected here rather than silently
                // printing the raw bitmask as a meaningless integer.
                fprintf(stderr, "%s:%d: Type Error: write/writeln can't print a set - there's no standard textual representation for one\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            if (is_pointer_type(node->left->expression_type)) {
                // Same reasoning as TYPE_SET above - a pointer's runtime
                // value (a vm_heap_mem[] offset) has no meaningful
                // textual representation in standard Pascal either.
                fprintf(stderr, "%s:%d: Type Error: write/writeln can't print a pointer\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
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
            // proc_table[...].param_count), so this only needs to check
            // types. Iterates PARAMETERS (param_count), not argument
            // nodes: a record parameter flattens into N field-value nodes
            // at parse time (see parse_record_argument() in parser.c), so
            // node-count and param_count diverge whenever one is present.
            ProcSymbol *proc = &proc_table[node->data.var_idx];
            ASTNode **arg_ptr = &node->left;
            for (int i = 0; i < proc->param_count; i++) {
                if (proc->param_is_record[i]) {
                    // The parser only accepts a record argument whose
                    // record type is IDENTICAL to the parameter's (see
                    // parse_record_argument()), so every flattened field
                    // is already guaranteed correctly typed - nothing to
                    // check. Just skip the N nodes this one syntactic
                    // parameter produced.
                    for (int j = 0; j < proc->param_record_field_count[i]; j++) {
                        arg_ptr = &(*arg_ptr)->next;
                    }
                    continue;
                }
                if (proc->param_is_var[i]) {
                    // parse_var_argument() already enforces an EXACT type
                    // match (no widening - real Pascal never widens a
                    // 'var' argument), so there's nothing left to check;
                    // just skip the one node it produced.
                    arg_ptr = &(*arg_ptr)->next;
                    continue;
                }
                DataType expected = proc->param_types[i];
                DataType actual = (*arg_ptr)->expression_type;
                if (!(is_string_type(expected) && is_string_type(actual)) && expected != actual
                    && !try_widen_for_assignment(arg_ptr, expected)) {
                    fprintf(stderr, "%s:%d: Type Error: Argument %d to procedure '%s' has the wrong type\n",
                            get_current_filename(), node->line, i + 1, proc->name);
                    fatal_abort();
                }
                arg_ptr = &(*arg_ptr)->next;
            }
            break;
        }

        case NODE_ASSERT:
            if (node->left->expression_type != TYPE_BOOLEAN) {
                fprintf(stderr, "%s:%d: Type Error: 'assert' requires a boolean condition\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            if (!is_string_type(node->right->expression_type)) {
                fprintf(stderr, "%s:%d: Type Error: 'assert' requires a string or char message\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        case NODE_FILE_OP:
            // reset/rewrite/close's file-variable argument was already
            // validated at parse time (find_file_var_soft() only ever
            // returns a genuine TYPE_FILE symbol) - only assign's second
            // argument (the filename, an arbitrary expression) still
            // needs a real type check here.
            if (node->op == TOKEN_FILE_ASSIGN && !is_string_type(node->left->expression_type)) {
                fprintf(stderr, "%s:%d: Type Error: 'assign' requires a string or char filename\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;

        // Each case-label leaf (NODE_NUMBER/NODE_STRING/NODE_BOOLEAN)
        // already carries its own expression_type, fixed at parse time
        // (see parse_case_label_value() in parser.c) - nothing here
        // needs the generic recursion above to have "checked" them the
        // way a NODE_BINARY_OP's operands need. What DOES need the
        // selector's expression_type is only resolved by that same
        // generic recursion (into node->left) just before this switch
        // runs, which is exactly why this check lives here and not in
        // the parser.
        case NODE_CASE: {
            DataType sel_type = node->left->expression_type;
            // Deliberately sel_type == TYPE_CHAR here, NOT is_string_type()
            // - unlike everywhere else in this file, a 'string' selector
            // must be REJECTED (it isn't ordinal, unlike char), so the two
            // can't be treated as interchangeable for this one check.
            if (sel_type != TYPE_INTEGER && sel_type != TYPE_CHAR && sel_type != TYPE_BOOLEAN && !is_enum_type(sel_type)) {
                fprintf(stderr, "%s:%d: Type Error: 'case' selector must be an ordinal type (integer, char, boolean, or enumerated), not %s\n",
                        get_current_filename(), node->line, sel_type == TYPE_REAL ? "real" : "string");
                fatal_abort();
            }
            int sel_is_char = (sel_type == TYPE_CHAR);
            for (ASTNode *arm = node->right; arm; arm = arm->next) {
                for (ASTNode *label = arm->left; label; label = label->next) {
                    // parse_case_label_value() never produces a
                    // TYPE_STRING label (only TYPE_CHAR), so is_string_type()
                    // here is just this file's usual char/string
                    // interchangeability - safe now that sel_is_char can
                    // only be true for a genuinely char selector.
                    int type_matches = sel_is_char ? is_string_type(label->expression_type) : (label->expression_type == sel_type);
                    if (!type_matches) {
                        fprintf(stderr, "%s:%d: Type Error: case label's type doesn't match the 'case' selector's type\n",
                                get_current_filename(), label->line);
                        fatal_abort();
                    }
                }
            }
            break;
        }

        case NODE_CASE_ARM: // children (labels, statement) already
                             // walked by the generic recursion above;
                             // validated from NODE_CASE's own case, once
                             // the selector's type is known
            break;

        case NODE_SET_CONSTRUCTOR:
            for (ASTNode *elem = node->left; elem; elem = elem->next) {
                DataType et = elem->expression_type;
                if (et != TYPE_INTEGER && et != TYPE_BOOLEAN && !is_enum_type(et)) {
                    fprintf(stderr, "%s:%d: Type Error: A set element must be an ordinal value (integer, boolean, or enumerated) - not real, string, char, or another set\n",
                            get_current_filename(), elem->line);
                    fatal_abort();
                }
            }
            break;

        case NODE_SET_IN: {
            DataType lt = node->left->expression_type;
            if (lt != TYPE_INTEGER && lt != TYPE_BOOLEAN && !is_enum_type(lt)) {
                fprintf(stderr, "%s:%d: Type Error: 'in' requires an ordinal value (integer, boolean, or enumerated) on the left\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            if (node->right->expression_type != TYPE_SET) {
                fprintf(stderr, "%s:%d: Type Error: 'in' requires a set on the right\n",
                        get_current_filename(), node->line);
                fatal_abort();
            }
            break;
        }

        default:
            break;
    }
}

