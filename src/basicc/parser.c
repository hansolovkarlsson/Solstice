#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "basic.h"

int basic_line_numbers[MAX_BASIC_LINES];
int basic_line_count = 0;

static const char *current_filename = "<source>";
static int current_stmt_line = 0; // the BASIC line number every node
                                  // created while parsing the CURRENT
                                  // source line is stamped with - see
                                  // basic.h's own note on
                                  // BasicASTNode.line.

const char *basic_get_current_filename(void) { return current_filename; }
void basic_set_current_filename(const char *f) { current_filename = f; }

static void compile_error(int line, const char *fmt, ...) {
    fprintf(stderr, "%s:%d: Compile Error: ", current_filename, line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    fatal_abort();
}

BasicASTNode *basic_create_node(BasicNodeType type) {
    BasicASTNode *node = calloc(1, sizeof(BasicASTNode));
    if (!node) { fprintf(stderr, "Memory failure\n"); fatal_abort(); }
    node->type = type;
    node->expression_type = TYPE_UNKNOWN;
    node->line = current_stmt_line;
    return node;
}

int basic_intern_string(const char *s) {
    for (int i = 0; i < string_count; i++) {
        if (strcmp(string_pool[i], s) == 0) return i;
    }
    if (string_count >= MAX_STRINGS) {
        compile_error(current_stmt_line, "Too many distinct string literals (limit is %d)", MAX_STRINGS);
    }
    strcpy(string_pool[string_count], s);
    return string_count++;
}

int basic_find_line_index(int line_number) {
    // basic_line_numbers[] is already sorted ascending (parse_ast()
    // enforces strictly-ascending source line numbers) - binary search.
    int lo = 0, hi = basic_line_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (basic_line_numbers[mid] == line_number) return mid;
        if (basic_line_numbers[mid] < line_number) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static void match(BasicTokenType t, const char *what) {
    if (btoken.type != t) compile_error(current_stmt_line, "Expected %s", what);
    basic_next_token();
}

static int find_var(const char *name) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) return i;
    }
    return -1;
}

// Classic-BASIC sigil typing: '$' = string, '%' = integer, bare = real
// (the classic default numeric type) - see basic.h's own top comment.
static DataType type_from_sigil(const char *name) {
    char last = name[strlen(name) - 1];
    if (last == '$') return TYPE_STRING;
    if (last == '%') return TYPE_INTEGER;
    return TYPE_REAL;
}

// A variable's first use (anywhere - LET target, expression operand,
// FOR/NEXT/INPUT target) both declares and fixes its type from its own
// sigil - no prior 'DIM'/declaration required, matching classic BASIC.
static int find_or_add_var(const char *name) {
    int idx = find_var(name);
    if (idx != -1) return idx;
    if (sym_count >= MAX_SYMBOLS) {
        compile_error(current_stmt_line, "Too many variables (limit is %d)", MAX_SYMBOLS);
    }
    int i = sym_count;
    strcpy(sym_table[i].name, name);
    sym_table[i].declaring_unit[0] = '\0';
    sym_table[i].is_unit_private = 0;
    sym_table[i].type = type_from_sigil(name);
    sym_table[i].is_array = 0;
    sym_table[i].array_lower = 0;
    sym_table[i].array_upper = 0;
    sym_table[i].array_base = 0;
    sym_table[i].is_2d = 0;
    sym_table[i].array_lower2 = 0;
    sym_table[i].array_upper2 = 0;
    sym_table[i].is_nd = 0;
    sym_table[i].nd_dims = 0;
    sym_table[i].is_subrange = 0;
    sym_table[i].subrange_lower = 0;
    sym_table[i].subrange_upper = 0;
    sym_table[i].is_record_array = 0;
    sym_table[i].record_elem_field_count = 0;
    sym_table[i].is_const = 0;
    sym_count++;
    return i;
}

static int float_to_bits_local(float f) { int bits; memcpy(&bits, &f, sizeof(bits)); return bits; }

// --- Expressions (standard precedence climbing) ---
// or > and > not > relational (single level, non-chaining) > +/- > * / > unary

static BasicASTNode *parse_expression(void);

static BasicASTNode *parse_primary(void) {
    BasicASTNode *node;
    if (btoken.type == BTOK_NUMBER) {
        node = basic_create_node(BNODE_NUMBER);
        node->data.num_value = btoken.value;
        node->expression_type = TYPE_INTEGER;
        basic_next_token();
        return node;
    }
    if (btoken.type == BTOK_REAL) {
        node = basic_create_node(BNODE_NUMBER);
        node->data.num_value = float_to_bits_local(btoken.real_value);
        node->expression_type = TYPE_REAL;
        basic_next_token();
        return node;
    }
    if (btoken.type == BTOK_STRING) {
        node = basic_create_node(BNODE_STRING);
        node->data.num_value = basic_intern_string(btoken.string_value);
        node->expression_type = TYPE_STRING;
        basic_next_token();
        return node;
    }
    if (btoken.type == BTOK_IDENT) {
        int idx = find_or_add_var(btoken.text);
        node = basic_create_node(BNODE_VARIABLE);
        node->data.var_idx = idx;
        node->expression_type = sym_table[idx].type;
        basic_next_token();
        return node;
    }
    if (btoken.type == BTOK_LPAREN) {
        basic_next_token();
        node = parse_expression();
        match(BTOK_RPAREN, "')'");
        return node;
    }
    compile_error(current_stmt_line, "Expected an expression");
    return NULL; // unreachable - compile_error() never returns
}

static BasicASTNode *parse_unary(void) {
    if (btoken.type == BTOK_MINUS) {
        basic_next_token();
        BasicASTNode *node = basic_create_node(BNODE_UNARY_OP);
        node->op = BTOK_MINUS;
        node->left = parse_unary();
        return node;
    }
    if (btoken.type == BTOK_PLUS) { basic_next_token(); return parse_unary(); }
    return parse_primary();
}

static BasicASTNode *parse_mul(void) {
    BasicASTNode *node = parse_unary();
    while (btoken.type == BTOK_MUL || btoken.type == BTOK_SLASH) {
        BasicTokenType op = btoken.type;
        basic_next_token();
        BasicASTNode *bin = basic_create_node(BNODE_BINARY_OP);
        bin->op = op;
        bin->left = node;
        bin->right = parse_unary();
        node = bin;
    }
    return node;
}

static BasicASTNode *parse_add(void) {
    BasicASTNode *node = parse_mul();
    while (btoken.type == BTOK_PLUS || btoken.type == BTOK_MINUS) {
        BasicTokenType op = btoken.type;
        basic_next_token();
        BasicASTNode *bin = basic_create_node(BNODE_BINARY_OP);
        bin->op = op;
        bin->left = node;
        bin->right = parse_mul();
        node = bin;
    }
    return node;
}

static int is_rel_op(BasicTokenType t) {
    return t == BTOK_EQ || t == BTOK_LT || t == BTOK_GT
        || t == BTOK_LTE || t == BTOK_GTE || t == BTOK_NEQ;
}

static BasicASTNode *parse_rel(void) {
    BasicASTNode *node = parse_add();
    if (is_rel_op(btoken.type)) {
        BasicTokenType op = btoken.type;
        basic_next_token();
        BasicASTNode *bin = basic_create_node(BNODE_BINARY_OP);
        bin->op = op;
        bin->left = node;
        bin->right = parse_add();
        node = bin;
    }
    return node;
}

static BasicASTNode *parse_not(void) {
    if (btoken.type == BTOK_NOT) {
        basic_next_token();
        BasicASTNode *node = basic_create_node(BNODE_UNARY_OP);
        node->op = BTOK_NOT;
        node->left = parse_not();
        return node;
    }
    return parse_rel();
}

static BasicASTNode *parse_and(void) {
    BasicASTNode *node = parse_not();
    while (btoken.type == BTOK_AND) {
        basic_next_token();
        BasicASTNode *bin = basic_create_node(BNODE_BINARY_OP);
        bin->op = BTOK_AND;
        bin->left = node;
        bin->right = parse_not();
        node = bin;
    }
    return node;
}

static BasicASTNode *parse_or(void) {
    BasicASTNode *node = parse_and();
    while (btoken.type == BTOK_OR) {
        basic_next_token();
        BasicASTNode *bin = basic_create_node(BNODE_BINARY_OP);
        bin->op = BTOK_OR;
        bin->left = node;
        bin->right = parse_and();
        node = bin;
    }
    return node;
}

static BasicASTNode *parse_expression(void) { return parse_or(); }

// --- Statements ---

static int at_stmt_boundary(void) {
    return btoken.type == BTOK_EOL || btoken.type == BTOK_EOF
        || btoken.type == BTOK_COLON || btoken.type == BTOK_ELSE;
}

static BasicASTNode *parse_statement(void);

// A ':'-separated chain of statements. Naturally stops at EOL/EOF, AND
// at a bare ELSE (an IF's THEN clause can be followed directly by ELSE
// with no ':' - see parse_if()) - the while-COLON condition below is
// simply false in all three cases, no special-casing needed.
static BasicASTNode *parse_stmt_list(void) {
    BasicASTNode *head = parse_statement();
    BasicASTNode *tail = head; // parse_statement() never itself sets ->next
    while (btoken.type == BTOK_COLON) {
        basic_next_token();
        tail->next = parse_statement();
        tail = tail->next;
    }
    return head;
}

static BasicASTNode *parse_let_body(void) {
    if (btoken.type != BTOK_IDENT) compile_error(current_stmt_line, "Expected a variable name");
    int idx = find_or_add_var(btoken.text);
    basic_next_token();
    match(BTOK_EQ, "'=' in assignment");
    BasicASTNode *node = basic_create_node(BNODE_LET);
    node->data.var_idx = idx;
    node->left = parse_expression();
    return node;
}

static BasicASTNode *parse_print(void) {
    basic_next_token(); // PRINT
    BasicASTNode *node = basic_create_node(BNODE_PRINT);
    node->data.num_value = 0; // suppress-trailing-newline flag
    if (at_stmt_boundary()) return node; // bare PRINT - just a newline

    BasicASTNode *head = NULL, *tail = NULL;
    for (;;) {
        BasicASTNode *item = parse_expression();
        if (!head) head = item; else tail->next = item;
        tail = item;
        if (btoken.type == BTOK_COMMA || btoken.type == BTOK_SEMI) {
            basic_next_token();
            if (at_stmt_boundary()) { node->data.num_value = 1; break; }
            continue;
        }
        break;
    }
    node->left = head;
    return node;
}

static BasicASTNode *parse_input(void) {
    basic_next_token(); // INPUT
    BasicASTNode *node = basic_create_node(BNODE_INPUT);
    if (btoken.type == BTOK_STRING) {
        BasicASTNode *prompt = basic_create_node(BNODE_STRING);
        prompt->data.num_value = basic_intern_string(btoken.string_value);
        prompt->expression_type = TYPE_STRING;
        basic_next_token();
        if (btoken.type != BTOK_COMMA && btoken.type != BTOK_SEMI) {
            compile_error(current_stmt_line, "Expected ',' after INPUT prompt");
        }
        basic_next_token();
        node->left = prompt;
    }
    if (btoken.type != BTOK_IDENT) compile_error(current_stmt_line, "Expected a variable name after 'INPUT'");
    node->data.var_idx = find_or_add_var(btoken.text);
    basic_next_token();
    return node;
}

// An IF's THEN/ELSE target is either a bare line number (shorthand for
// 'GOTO n') or a ':'-separated statement chain running to the line's end.
static BasicASTNode *parse_then_target(void) {
    if (btoken.type == BTOK_NUMBER) {
        BasicASTNode *node = basic_create_node(BNODE_GOTO);
        node->data.num_value = btoken.value;
        basic_next_token();
        return node;
    }
    return parse_stmt_list();
}

static BasicASTNode *parse_if(void) {
    basic_next_token(); // IF
    BasicASTNode *node = basic_create_node(BNODE_IF);
    node->left = parse_expression();
    match(BTOK_THEN, "'THEN'");
    node->right = parse_then_target();
    if (btoken.type == BTOK_ELSE) {
        basic_next_token();
        node->extra = parse_then_target();
    }
    return node;
}

static BasicASTNode *parse_for(void) {
    basic_next_token(); // FOR
    if (btoken.type != BTOK_IDENT) compile_error(current_stmt_line, "Expected a loop variable after 'FOR'");
    int idx = find_or_add_var(btoken.text);
    basic_next_token();
    match(BTOK_EQ, "'=' after FOR variable");
    BasicASTNode *node = basic_create_node(BNODE_FOR);
    node->data.var_idx = idx;
    node->left = parse_expression(); // start
    match(BTOK_TO, "'TO' in FOR statement");
    node->right = parse_expression(); // end
    if (btoken.type == BTOK_STEP) {
        basic_next_token();
        node->extra = parse_expression();
    }
    return node;
}

static BasicASTNode *parse_next(void) {
    basic_next_token(); // NEXT
    BasicASTNode *node = basic_create_node(BNODE_NEXT);
    node->data.var_idx = -1;
    if (btoken.type == BTOK_IDENT) {
        node->data.var_idx = find_or_add_var(btoken.text);
        basic_next_token();
    }
    return node;
}

static BasicASTNode *parse_goto_or_gosub(BasicNodeType type, const char *keyword) {
    basic_next_token();
    if (btoken.type != BTOK_NUMBER) compile_error(current_stmt_line, "Expected a line number after '%s'", keyword);
    BasicASTNode *node = basic_create_node(type);
    node->data.num_value = btoken.value;
    basic_next_token();
    return node;
}

static BasicASTNode *parse_statement(void) {
    switch (btoken.type) {
        case BTOK_LET:
            basic_next_token();
            return parse_let_body();
        case BTOK_IDENT:
            return parse_let_body(); // 'LET' is optional, classic-BASIC style
        case BTOK_PRINT:
            return parse_print();
        case BTOK_INPUT:
            return parse_input();
        case BTOK_IF:
            return parse_if();
        case BTOK_GOTO:
            return parse_goto_or_gosub(BNODE_GOTO, "GOTO");
        case BTOK_GOSUB:
            return parse_goto_or_gosub(BNODE_GOSUB, "GOSUB");
        case BTOK_RETURN:
            basic_next_token();
            return basic_create_node(BNODE_RETURN);
        case BTOK_FOR:
            return parse_for();
        case BTOK_NEXT:
            return parse_next();
        case BTOK_END:
            basic_next_token();
            return basic_create_node(BNODE_END);
        default:
            compile_error(current_stmt_line, "Expected a statement");
            return NULL; // unreachable
    }
}

BasicASTNode *basic_parse_ast(const char *source, const char *filename) {
    current_filename = filename;
    // Reset every shared, whole-compile-scoped global state (see
    // CLAUDE.md's "Global state, not parameters" section) - basicc.c
    // only ever calls this once per process today, but resetting here
    // (rather than relying on that) keeps this function safe to embed
    // or call repeatedly later, matching the rest of this codebase's
    // convention.
    sym_count = 0;
    code_idx = 0;
    string_count = 0;
    array_mem_count = 0;
    basic_line_count = 0;

    basic_init_lexer(source);
    basic_next_token();

    BasicASTNode *head = NULL, *tail = NULL;
    int last_line_number = -1;

    while (btoken.type != BTOK_EOF) {
        while (btoken.type == BTOK_EOL) basic_next_token(); // blank/comment-only lines
        if (btoken.type == BTOK_EOF) break;

        if (btoken.type != BTOK_NUMBER) compile_error(btoken.line, "Expected a line number");
        int line_number = btoken.value;
        if (line_number <= last_line_number) {
            compile_error(btoken.line, "Line numbers must be strictly ascending (got %d after %d)",
                          line_number, last_line_number);
        }
        last_line_number = line_number;
        if (basic_line_count >= MAX_BASIC_LINES) {
            compile_error(btoken.line, "Too many source lines (limit is %d)", MAX_BASIC_LINES);
        }
        basic_line_numbers[basic_line_count++] = line_number;
        current_stmt_line = line_number;
        basic_next_token();

        // A line number with nothing after it (blank, or - since the
        // lexer consumes REM/''' comments to nothing - comment-only) is
        // valid: it declares the line number (a GOTO/GOSUB to it is
        // still legal, falling through to whatever comes next - see
        // codegen.c's line_addr backfill sweep) without adding any
        // statements.
        if (btoken.type != BTOK_EOL && btoken.type != BTOK_EOF) {
            BasicASTNode *line_stmts = parse_stmt_list();
            if (!head) head = line_stmts; else tail->next = line_stmts;
            tail = line_stmts;
            while (tail->next) tail = tail->next;
        }

        if (btoken.type != BTOK_EOL && btoken.type != BTOK_EOF) {
            compile_error(btoken.line, "Unexpected token at end of line");
        }
    }
    return head;
}

void basic_free_ast(BasicASTNode *node) {
    if (!node) return;
    basic_free_ast(node->left);
    basic_free_ast(node->right);
    basic_free_ast(node->next);
    basic_free_ast(node->extra);
    free(node);
}
