#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "parser.h"
#include "lexer.h"
#include "error.h"

static const char *current_filename = "<source>";

// Tracks how many while/for/repeat bodies we're currently parsing inside
// of (nested ifs don't count). Used to reject break/continue outside a
// loop right where they're written, rather than only failing at codegen.
static int loop_depth = 0;

// The parameters and local variables of whichever procedure is currently
// being parsed - a fresh, tiny scope, reset at the start of each
// procedure_declaration() and cleared again once it's done. Empty (and
// in_procedure == 0) while parsing the main program body, so every
// identifier there resolves against sym_table exactly as before
// procedures existed.
#define MAX_LOCALS 32
typedef struct {
    char name[MAX_NAME];
    DataType type;
} LocalSymbol;
static LocalSymbol current_locals[MAX_LOCALS];
static int current_local_count = 0;
static int in_procedure = 0;

const char *get_current_filename(void) {
    return current_filename;
}

static void compile_error(int line, const char *fmt, ...) {
    fprintf(stderr, "%s:%d: Compile Error: ", current_filename, line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    fatal_abort();
}

ASTNode *create_node(NodeType type) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    if (!node) { fprintf(stderr, "Memory failure\n"); fatal_abort(); }
    node->type = type;
    node->expression_type = TYPE_UNKNOWN;
    node->line = token.line;
    return node;
}

static int find_var(const char *name) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) return i;
    }
    compile_error(token.line, "Unknown identifier '%s'", name);
    return -1;
}

static void add_var(const char *name, DataType type) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) {
            compile_error(token.line, "Duplicate variable declaration '%s'", name);
        }
    }
    if (sym_count >= MAX_SYMBOLS) {
        compile_error(token.line, "Too many variable declarations (limit is %d)", MAX_SYMBOLS);
    }
    strcpy(sym_table[sym_count].name, name);
    sym_table[sym_count].type = type;
    sym_table[sym_count].is_array = 0;
    sym_table[sym_count].array_lower = 0;
    sym_table[sym_count].array_upper = 0;
    sym_table[sym_count].array_base = 0;
    sym_count++;
}

// Same as add_var(), but for 'name: array[lower..upper] of type'. Bounds
// must already be validated (lower <= upper) by the caller.
static void add_array_var(const char *name, DataType elem_type, int lower, int upper) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) {
            compile_error(token.line, "Duplicate variable declaration '%s'", name);
        }
    }
    if (sym_count >= MAX_SYMBOLS) {
        compile_error(token.line, "Too many variable declarations (limit is %d)", MAX_SYMBOLS);
    }
    int size = upper - lower + 1;
    if (array_mem_count + size > MAX_ARRAY_MEM) {
        compile_error(token.line, "Array storage exhausted (limit is %d total elements across all arrays)", MAX_ARRAY_MEM);
    }
    strcpy(sym_table[sym_count].name, name);
    sym_table[sym_count].type = elem_type;
    sym_table[sym_count].is_array = 1;
    sym_table[sym_count].array_lower = lower;
    sym_table[sym_count].array_upper = upper;
    sym_table[sym_count].array_base = array_mem_count;
    array_mem_count += size;
    sym_count++;
}

// Soft lookup - returns -1 rather than erroring, since the caller needs
// to decide "is this name a procedure or a variable?" before knowing
// which error (if any) is appropriate.
static int find_proc(const char *name) {
    for (int i = 0; i < proc_count; i++) {
        if (strcmp(proc_table[i].name, name) == 0) return i;
    }
    return -1;
}

// Registers a procedure's name immediately (before its body is parsed) -
// required so a procedure can call itself (recursion): by the time the
// parser is inside the body, find_proc() already sees this entry.
// Procedures and variables are stored in separate tables but share one
// namespace from the user's perspective, so this checks against both.
static int add_proc(const char *name) {
    if (find_proc(name) != -1) {
        compile_error(token.line, "Duplicate procedure declaration '%s'", name);
    }
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) {
            compile_error(token.line, "'%s' is already declared as a variable", name);
        }
    }
    if (proc_count >= MAX_PROCEDURES) {
        compile_error(token.line, "Too many procedure declarations (limit is %d)", MAX_PROCEDURES);
    }
    strcpy(proc_table[proc_count].name, name);
    proc_table[proc_count].entry_address = -1; // resolved during codegen
    proc_table[proc_count].body = NULL;        // set once the body is parsed
    return proc_count++;
}

// Soft lookup, like find_proc() - returns -1 if `name` isn't a
// parameter/local of the procedure currently being parsed (or if we're
// not currently parsing a procedure body at all). A local shadows a
// procedure name or a global variable of the same name, matching
// standard Pascal lexical scoping - which is exactly why every call site
// below checks this first.
static int find_local(const char *name) {
    if (!in_procedure) return -1;
    for (int i = 0; i < current_local_count; i++) {
        if (strcmp(current_locals[i].name, name) == 0) return i;
    }
    return -1;
}

static int add_local(const char *name, DataType type) {
    if (find_local(name) != -1) {
        compile_error(token.line, "Duplicate parameter or local variable '%s'", name);
    }
    if (current_local_count >= MAX_LOCALS) {
        compile_error(token.line, "Too many parameters/local variables (limit is %d)", MAX_LOCALS);
    }
    strcpy(current_locals[current_local_count].name, name);
    current_locals[current_local_count].type = type;
    return current_local_count++;
}

// Adds a string literal to the pool, reusing an existing slot if the exact
// same text was already interned (this is a space-saving dedup, not a
// correctness requirement - string equality is checked via strcmp at
// runtime, not by comparing pool indices).
static int intern_string(const char *s) {
    for (int i = 0; i < string_count; i++) {
        if (strcmp(string_pool[i], s) == 0) return i;
    }
    if (string_count >= MAX_STRINGS) {
        compile_error(token.line, "Too many distinct string literals (limit is %d)", MAX_STRINGS);
    }
    strcpy(string_pool[string_count], s);
    return string_count++;
}

static void match(TokenType type) {
    if (token.type == type) next_token();
    else compile_error(token.line, "Unexpected token '%s'", token.text[0] ? token.text : "EOF");
}

// Parses a compile-time-constant integer literal, e.g. for array bounds
// (array[1..10], array[-5..5]). Not a general expression - array sizes
// must be known at compile time in this language.
static int parse_int_literal(void) {
    int sign = 1;
    if (token.type == TOKEN_MINUS) {
        sign = -1;
        match(TOKEN_MINUS);
    }
    if (token.type != TOKEN_NUMBER) {
        compile_error(token.line, "Expected an integer literal");
    }
    int val = token.value * sign;
    match(TOKEN_NUMBER);
    return val;
}

// A scalar type only - used for parameters and procedure-local variables,
// neither of which support arrays in this increment.
static DataType parse_scalar_type(void) {
    if (token.type == TOKEN_INTEGER) { match(TOKEN_INTEGER); return TYPE_INTEGER; }
    if (token.type == TOKEN_BOOLEAN) { match(TOKEN_BOOLEAN); return TYPE_BOOLEAN; }
    if (token.type == TOKEN_STRING_TYPE) { match(TOKEN_STRING_TYPE); return TYPE_STRING; }
    if (token.type == TOKEN_CHAR_TYPE) { match(TOKEN_CHAR_TYPE); return TYPE_CHAR; }
    compile_error(token.line, "Unknown type (arrays are not supported for parameters or local variables)");
    return TYPE_UNKNOWN;
}

// Parses 'name {, name} : scalar-type' - the shared inner syntax of a
// parameter group and a procedure-local var declaration line.
#define MAX_GROUP_NAMES 16
typedef struct {
    char names[MAX_GROUP_NAMES][MAX_NAME];
    int count;
    DataType type;
} NameGroup;

static NameGroup parse_name_group(void) {
    NameGroup g;
    g.count = 0;
    if (token.type != TOKEN_IDENTIFIER) compile_error(token.line, "Expected an identifier");
    strcpy(g.names[g.count++], token.text);
    match(TOKEN_IDENTIFIER);
    while (token.type == TOKEN_COMMA) {
        match(TOKEN_COMMA);
        if (g.count >= MAX_GROUP_NAMES) {
            compile_error(token.line, "Too many names in one group (limit is %d)", MAX_GROUP_NAMES);
        }
        if (token.type != TOKEN_IDENTIFIER) compile_error(token.line, "Expected an identifier");
        strcpy(g.names[g.count++], token.text);
        match(TOKEN_IDENTIFIER);
    }
    match(TOKEN_COLON);
    g.type = parse_scalar_type();
    return g;
}

static ASTNode *expression(void);
static ASTNode *statement(void);
static ASTNode *statement_list(void);
static ASTNode *compound_statement(void);
static void procedure_declaration(void);

static ASTNode *factor(void) {
    if (token.type == TOKEN_MINUS || token.type == TOKEN_NOT) {
        TokenType op = token.type;
        match(op);
        ASTNode *node = create_node(NODE_UNARY_OP);
        node->op = op;
        node->left = factor();
        return node;
    } else if (token.type == TOKEN_LPAREN) {
        match(TOKEN_LPAREN);
        ASTNode *node = expression();
        match(TOKEN_RPAREN);
        return node;
    } else if (token.type == TOKEN_NUMBER) {
        ASTNode *node = create_node(NODE_NUMBER);
        node->data.num_value = token.value;
        node->expression_type = TYPE_INTEGER;
        match(TOKEN_NUMBER);
        return node;
    } else if (token.type == TOKEN_TRUE || token.type == TOKEN_FALSE) {
        ASTNode *node = create_node(NODE_BOOLEAN);
        node->data.num_value = token.value;
        node->expression_type = TYPE_BOOLEAN;
        next_token();
        return node;
    } else if (token.type == TOKEN_STRING) {
        ASTNode *node = create_node(NODE_STRING);
        node->data.var_idx = intern_string(token.string_value); // pool index
        node->expression_type = TYPE_STRING;
        match(TOKEN_STRING);
        return node;
    } else if (token.type == TOKEN_IDENTIFIER) {
        int line = token.line;

        int local_idx = find_local(token.text);
        if (local_idx != -1) {
            match(TOKEN_IDENTIFIER);
            ASTNode *node = create_node(NODE_LOCAL_VAR);
            node->line = line;
            node->data.var_idx = local_idx;
            node->expression_type = current_locals[local_idx].type;
            return node;
        }

        int idx = find_var(token.text);
        match(TOKEN_IDENTIFIER);
        if (sym_table[idx].is_array) {
            if (token.type != TOKEN_LBRACKET) {
                compile_error(token.line, "Array '%s' must be indexed", sym_table[idx].name);
            }
            match(TOKEN_LBRACKET);
            ASTNode *node = create_node(NODE_ARRAY_ACCESS);
            node->line = line;
            node->data.var_idx = idx;
            node->left = expression(); // index
            node->expression_type = sym_table[idx].type;
            match(TOKEN_RBRACKET);
            return node;
        }
        if (token.type == TOKEN_LBRACKET) {
            compile_error(token.line, "'%s' is not an array, cannot be indexed", sym_table[idx].name);
        }
        ASTNode *node = create_node(NODE_VARIABLE);
        node->line = line;
        node->data.var_idx = idx;
        node->expression_type = sym_table[idx].type;
        return node;
    }
    compile_error(token.line, "Invalid factor entry");
    return NULL;
}

static ASTNode *term(void) {
    ASTNode *node = factor();
    while (token.type == TOKEN_MUL || token.type == TOKEN_DIV || 
           token.type == TOKEN_DIV_KW || token.type == TOKEN_MOD || 
           token.type == TOKEN_AND) {
        TokenType op = token.type;
        match(op);
        ASTNode *new_node = create_node(NODE_BINARY_OP);
        new_node->op = op;
        new_node->left = node;
        new_node->right = factor();
        node = new_node;
    }
    return node;
}

static ASTNode *arithmetic_expression(void) {
    ASTNode *node = term();
    while (token.type == TOKEN_PLUS || token.type == TOKEN_MINUS || 
           token.type == TOKEN_OR || token.type == TOKEN_XOR) {
        TokenType op = token.type;
        match(op);
        ASTNode *new_node = create_node(NODE_BINARY_OP);
        new_node->op = op;
        new_node->left = node;
        new_node->right = term();
        node = new_node;
    }
    return node;
}


static ASTNode *expression(void) {
    ASTNode *node = arithmetic_expression();
    while (token.type == TOKEN_EQ || token.type == TOKEN_LT || token.type == TOKEN_GT
        || token.type == TOKEN_LTE || token.type == TOKEN_GTE || token.type == TOKEN_NEQ ) {
        ASTNode *op_node = create_node(NODE_BINARY_OP);
        op_node->op = token.type;
        op_node->left = node;
        next_token();
        op_node->right = arithmetic_expression();
        node = op_node;
    }
    return node;
}

ASTNode *parse_ast(const char *source, const char *filename) {
    current_filename = filename ? filename : "<source>";
    sym_count = 0;
    code_idx = 0;
    string_count = 0;
    array_mem_count = 0;
    loop_depth = 0;
    proc_count = 0;
    current_local_count = 0;
    in_procedure = 0;
    init_lexer(source);
    match(TOKEN_PROGRAM);
    match(TOKEN_IDENTIFIER);
    match(TOKEN_SEMI);

    if (token.type == TOKEN_VAR) {
        match(TOKEN_VAR);
        while (token.type == TOKEN_IDENTIFIER) {
            #define MAX_VAR_NAMES_PER_LINE 20
            char temporary_names[MAX_VAR_NAMES_PER_LINE][MAX_NAME];
            int count = 0;

            strcpy(temporary_names[count++], token.text);
            match(TOKEN_IDENTIFIER);
            
            while (token.type == TOKEN_COMMA) {
                match(TOKEN_COMMA);
                if (count >= MAX_VAR_NAMES_PER_LINE) {
                    compile_error(token.line, "Too many identifiers in one 'var' line (limit is %d)", MAX_VAR_NAMES_PER_LINE);
                }
                strcpy(temporary_names[count++], token.text);
                match(TOKEN_IDENTIFIER);
            }
            match(TOKEN_COLON);

            if (token.type == TOKEN_ARRAY) {
                match(TOKEN_ARRAY);
                match(TOKEN_LBRACKET);
                int lower = parse_int_literal();
                match(TOKEN_DOTDOT);
                int upper = parse_int_literal();
                match(TOKEN_RBRACKET);
                if (upper < lower) {
                    compile_error(token.line, "Invalid array bounds: upper (%d) must be >= lower (%d)", upper, lower);
                }
                match(TOKEN_OF);

                DataType elem_type = TYPE_UNKNOWN;
                if (token.type == TOKEN_INTEGER) { elem_type = TYPE_INTEGER; match(TOKEN_INTEGER); }
                else if (token.type == TOKEN_BOOLEAN) { elem_type = TYPE_BOOLEAN; match(TOKEN_BOOLEAN); }
                else if (token.type == TOKEN_STRING_TYPE) { elem_type = TYPE_STRING; match(TOKEN_STRING_TYPE); }
                else if (token.type == TOKEN_CHAR_TYPE) { elem_type = TYPE_CHAR; match(TOKEN_CHAR_TYPE); }
                else compile_error(token.line, "Unknown array element type");

                for (int i = 0; i < count; i++) {
                    add_array_var(temporary_names[i], elem_type, lower, upper);
                }
            } else {
                DataType target_type = TYPE_UNKNOWN;
                if (token.type == TOKEN_INTEGER) { target_type = TYPE_INTEGER; match(TOKEN_INTEGER); }
                else if (token.type == TOKEN_BOOLEAN) { target_type = TYPE_BOOLEAN; match(TOKEN_BOOLEAN); }
                else if (token.type == TOKEN_STRING_TYPE) { target_type = TYPE_STRING; match(TOKEN_STRING_TYPE); }
                else if (token.type == TOKEN_CHAR_TYPE) { target_type = TYPE_CHAR; match(TOKEN_CHAR_TYPE); }
                else compile_error(token.line, "Unknown primitive category");

                for (int i = 0; i < count; i++) {
                    add_var(temporary_names[i], target_type);
                }
            }
            match(TOKEN_SEMI);
        }
    }

    while (token.type == TOKEN_PROCEDURE) {
        procedure_declaration();
    }

    ASTNode *root = compound_statement();
    match(TOKEN_PERIOD);
    return root;
}

// 'procedure name [(params)] ; [var locals ;] <compound-statement>;'.
// Registers the name (via add_proc) before parsing anything else, so a
// call to this procedure's own name inside its body - recursion -
// resolves correctly. Parameter info is written back to proc_table right
// after the parameter list is parsed, before the body: a recursive call
// site inside the body needs the real param_count/param_types already in
// place, not the placeholders add_proc() set.
static void procedure_declaration(void) {
    match(TOKEN_PROCEDURE);
    if (token.type != TOKEN_IDENTIFIER) {
        compile_error(token.line, "Expected a procedure name");
    }
    char name[MAX_NAME];
    strcpy(name, token.text);
    match(TOKEN_IDENTIFIER);

    int proc_idx = add_proc(name);

    in_procedure = 1;
    current_local_count = 0;

    int param_count = 0;
    DataType param_types[MAX_PARAMS];

    if (token.type == TOKEN_LPAREN) {
        match(TOKEN_LPAREN);
        if (token.type != TOKEN_RPAREN) {
            while (1) {
                NameGroup g = parse_name_group();
                for (int i = 0; i < g.count; i++) {
                    if (param_count >= MAX_PARAMS) {
                        compile_error(token.line, "Too many parameters (limit is %d)", MAX_PARAMS);
                    }
                    add_local(g.names[i], g.type);
                    param_types[param_count++] = g.type;
                }
                if (token.type == TOKEN_SEMI) { match(TOKEN_SEMI); continue; }
                break;
            }
        }
        match(TOKEN_RPAREN);
    }
    match(TOKEN_SEMI);

    // Write parameter info back now - before the body is parsed, so a
    // recursive call from inside the body sees the real values.
    proc_table[proc_idx].param_count = param_count;
    for (int i = 0; i < param_count; i++) {
        proc_table[proc_idx].param_types[i] = param_types[i];
    }

    if (token.type == TOKEN_VAR) {
        match(TOKEN_VAR);
        while (token.type == TOKEN_IDENTIFIER) {
            NameGroup g = parse_name_group();
            for (int i = 0; i < g.count; i++) {
                add_local(g.names[i], g.type);
            }
            match(TOKEN_SEMI);
        }
    }

    ASTNode *body = compound_statement();
    match(TOKEN_SEMI);

    proc_table[proc_idx].body = body;
    proc_table[proc_idx].local_count = current_local_count; // params + locals

    in_procedure = 0;
    current_local_count = 0;
}

// True for every token that can legally start a statement. Used by
// statement_list() to know when to stop (hitting END/ELSE/UNTIL, or EOF
// on a malformed file, all correctly fail this check).
static int is_statement_start(TokenType t) {
    return t == TOKEN_IDENTIFIER || t == TOKEN_WRITELN || t == TOKEN_WRITE || t == TOKEN_READLN ||
           t == TOKEN_IF || t == TOKEN_WHILE || t == TOKEN_REPEAT || t == TOKEN_FOR || t == TOKEN_BEGIN ||
           t == TOKEN_BREAK || t == TOKEN_CONTINUE;
}

// Parses exactly one statement - an assignment, writeln/readln call,
// if/while/repeat, or a nested begin...end block. Never touches a
// separating semicolon or the node's ->next; that's statement_list()'s job.
static ASTNode *statement(void) {
    if (token.type == TOKEN_BEGIN) {
        return compound_statement();
    }

    if (token.type == TOKEN_IDENTIFIER) {
        int local_idx = find_local(token.text);
        if (local_idx != -1) {
            ASTNode *stmt = create_node(NODE_LOCAL_ASSIGN);
            stmt->data.var_idx = local_idx;
            stmt->expression_type = current_locals[local_idx].type; // target type, for the type checker
            match(TOKEN_IDENTIFIER);
            match(TOKEN_ASSIGN);
            stmt->left = expression();
            return stmt;
        }

        int proc_idx = find_proc(token.text);
        if (proc_idx != -1) {
            ASTNode *stmt = create_node(NODE_CALL);
            stmt->data.var_idx = proc_idx;
            match(TOKEN_IDENTIFIER);

            int arg_count = 0;
            if (token.type == TOKEN_LPAREN) {
                match(TOKEN_LPAREN);
                if (token.type != TOKEN_RPAREN) {
                    ASTNode *arg_head = expression();
                    arg_count++;
                    ASTNode *arg_tail = arg_head;
                    while (token.type == TOKEN_COMMA) {
                        match(TOKEN_COMMA);
                        ASTNode *next_arg = expression();
                        arg_count++;
                        arg_tail->next = next_arg;
                        arg_tail = next_arg;
                    }
                    stmt->left = arg_head;
                }
                match(TOKEN_RPAREN);
            }
            if (arg_count != proc_table[proc_idx].param_count) {
                compile_error(token.line, "Procedure '%s' expects %d argument(s), got %d",
                               proc_table[proc_idx].name, proc_table[proc_idx].param_count, arg_count);
            }
            return stmt;
        }

        ASTNode *stmt = create_node(NODE_ASSIGN);
        int idx = find_var(token.text);
        stmt->data.var_idx = idx;
        match(TOKEN_IDENTIFIER);
        if (sym_table[idx].is_array) {
            if (token.type != TOKEN_LBRACKET) {
                compile_error(token.line, "Array '%s' must be indexed for assignment", sym_table[idx].name);
            }
            match(TOKEN_LBRACKET);
            stmt->left = expression();  // index
            match(TOKEN_RBRACKET);
            match(TOKEN_ASSIGN);
            stmt->right = expression(); // value
        } else {
            match(TOKEN_ASSIGN);
            stmt->left = expression();  // value
        }
        return stmt;
    }

    if (token.type == TOKEN_WRITELN || token.type == TOKEN_WRITE) {
        TokenType kind = token.type;
        match(kind);
        ASTNode *stmt = create_node(NODE_WRITELN);
        stmt->op = kind; // TOKEN_WRITE (no trailing newline) or TOKEN_WRITELN
        stmt->left = NULL;
        if (token.type == TOKEN_LPAREN) {
            match(TOKEN_LPAREN);
            if (token.type != TOKEN_RPAREN) {
                ASTNode *arg_head = expression();
                ASTNode *arg_tail = arg_head;
                while (token.type == TOKEN_COMMA) {
                    match(TOKEN_COMMA);
                    ASTNode *next_arg = expression();
                    arg_tail->next = next_arg;
                    arg_tail = next_arg;
                }
                stmt->left = arg_head;
            }
            match(TOKEN_RPAREN);
        }
        return stmt;
    }

    if (token.type == TOKEN_READLN) {
        match(TOKEN_READLN);
        match(TOKEN_LPAREN);
        ASTNode *stmt = create_node(NODE_READLN);
        if (token.type == TOKEN_IDENTIFIER) {
            if (find_local(token.text) != -1) {
                compile_error(token.line, "readln into a parameter or local variable is not yet supported");
            }
            stmt->data.var_idx = find_var(token.text);
            match(TOKEN_IDENTIFIER);
        } else {
            compile_error(token.line, "readln expects a variable identifier");
        }
        match(TOKEN_RPAREN);
        return stmt;
    }

    if (token.type == TOKEN_IF) {
        ASTNode *stmt = create_node(NODE_IF);
        match(TOKEN_IF);
        stmt->left = expression();       // condition
        match(TOKEN_THEN);
        stmt->right = statement();       // then-branch
        if (token.type == TOKEN_ELSE) {
            match(TOKEN_ELSE);
            stmt->extra = statement();   // else-branch (optional)
        }
        return stmt;
    }

    if (token.type == TOKEN_WHILE) {
        ASTNode *stmt = create_node(NODE_WHILE);
        match(TOKEN_WHILE);
        stmt->left = expression();       // condition
        match(TOKEN_DO);
        loop_depth++;
        stmt->right = statement();       // body
        loop_depth--;
        return stmt;
    }

    if (token.type == TOKEN_FOR) {
        ASTNode *stmt = create_node(NODE_FOR);
        match(TOKEN_FOR);
        if (token.type != TOKEN_IDENTIFIER) {
            compile_error(token.line, "'for' expects a variable identifier");
        }
        if (find_local(token.text) != -1) {
            compile_error(token.line, "'for' loop variable must be a global variable (parameters/locals not yet supported)");
        }
        stmt->data.var_idx = find_var(token.text);
        match(TOKEN_IDENTIFIER);
        match(TOKEN_ASSIGN);
        stmt->left = expression();       // start bound
        if (token.type == TOKEN_TO) {
            match(TOKEN_TO);
            stmt->op = TOKEN_TO;
        } else if (token.type == TOKEN_DOWNTO) {
            match(TOKEN_DOWNTO);
            stmt->op = TOKEN_DOWNTO;
        } else {
            compile_error(token.line, "'for' expects 'to' or 'downto'");
        }
        stmt->right = expression();      // end bound
        match(TOKEN_DO);
        loop_depth++;
        stmt->extra = statement();       // body
        loop_depth--;
        return stmt;
    }

    if (token.type == TOKEN_REPEAT) {
        ASTNode *stmt = create_node(NODE_REPEAT);
        match(TOKEN_REPEAT);
        loop_depth++;
        stmt->left = statement_list();   // body (chained via ->next, no wrapping compound needed)
        loop_depth--;
        match(TOKEN_UNTIL);
        stmt->right = expression();      // until-condition
        return stmt;
    }

    if (token.type == TOKEN_BREAK) {
        if (loop_depth == 0) {
            compile_error(token.line, "'break' used outside of a loop");
        }
        ASTNode *stmt = create_node(NODE_BREAK);
        match(TOKEN_BREAK);
        return stmt;
    }

    if (token.type == TOKEN_CONTINUE) {
        if (loop_depth == 0) {
            compile_error(token.line, "'continue' used outside of a loop");
        }
        ASTNode *stmt = create_node(NODE_CONTINUE);
        match(TOKEN_CONTINUE);
        return stmt;
    }

    compile_error(token.line, "Unexpected token '%s' at start of statement", token.text[0] ? token.text : "EOF");
    return NULL;
}

// Parses statements separated by ';' until a non-statement token is hit
// (END, ELSE, UNTIL, or EOF). A trailing ';' before that terminator is
// optional, matching normal Pascal statement-list syntax.
static ASTNode *statement_list(void) {
    ASTNode *head = NULL;
    ASTNode *tail = NULL;
    while (is_statement_start(token.type)) {
        ASTNode *stmt = statement();
        if (!head) head = stmt;
        else tail->next = stmt;
        tail = stmt;

        if (token.type == TOKEN_SEMI) {
            match(TOKEN_SEMI);
        } else {
            break;
        }
    }
    return head;
}

static ASTNode *compound_statement(void) {
    match(TOKEN_BEGIN);
    ASTNode *root = create_node(NODE_COMPOUND);
    root->left = statement_list();
    match(TOKEN_END);
    return root;
}

void free_ast(ASTNode *node) {
    if (!node) return;
    free_ast(node->left);
    free_ast(node->right);
    free_ast(node->next);
    free_ast(node->extra);
    free(node);
}

