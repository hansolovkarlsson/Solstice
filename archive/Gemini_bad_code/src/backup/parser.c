#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "parser.h"
#include "lexer.h"

Instruction code[MAX_CODE];
int code_idx = 0;

Symbol sym_table[MAX_SYMBOLS];
int sym_count = 0;

static const char *current_filename = "<source>";

// char string_pool[MAX_STRINGS][MAX_STR_LEN];
char *string_pool[MAX_STRINGS] = {0};
int string_pool_count = 0;

const char *get_current_filename(void) {
    return current_filename;
}

void compile_error(int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "Error [Line %d]: ", line);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

ASTNode *create_node(NodeType type) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    if (!node) { fprintf(stderr, "Memory failure\n"); exit(1); }
    node->type = type;
    node->expression_type = TYPE_UNKNOWN;
    node->line = token.line;
    return node;
}

static int find_var(const char *name) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) return i;
    }
    compile_error(token.line, "Unknown variable '%s'", name);
    return -1;
}

static void add_var(const char *name, DataType type) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) {
            compile_error(token.line, "Duplicate variable declaration '%s'", name);
        }
    }
    strcpy(sym_table[sym_count].name, name);
    sym_table[sym_count].type = type;
    sym_count++;
}

int add_string_literal(const char *str) {
    if (!str) return -1;

    // Deduplicate existing strings safely
    for (int i = 0; i < string_pool_count; i++) {
        if (string_pool[i] != NULL && strcmp(string_pool[i], str) == 0) {
            return i;
        }
    }

    if (string_pool_count >= MAX_STRINGS) {
        compile_error(0, "String pool capacity exceeded (%d max)", MAX_STRINGS);
    }

    string_pool[string_pool_count] = strdup(str);
    return string_pool_count++;
}

void free_string_pool(void) {
    for (int i = 0; i < string_pool_count; i++) {
        if (string_pool[i] != NULL) {
            free(string_pool[i]);
            string_pool[i] = NULL; // Prevent double-free issues
        }
    }
    string_pool_count = 0;
}

static void match(TokenType type) {
    if (token.type == type) next_token();
    else compile_error(token.line, "Unexpected token '%s'", token.text[0] ? token.text : "EOF");
}

static ASTNode *expression(void);

static ASTNode *factor(void) {
    if (token.type == TOKEN_STRING) {
        ASTNode *node = create_node(NODE_STRING);
        node->data.str_idx = add_string_literal(token.text);
        node->expression_type = TYPE_STRING;
        match(TOKEN_STRING);
        return node;
    } 
    if (token.type == TOKEN_MINUS || token.type == TOKEN_NOT) {
        TokenType op = token.type;
        match(op);
        ASTNode *node = create_node(NODE_UNARY_OP);
        node->op = op;
        node->left = factor();
        return node;
    } 
    if (token.type == TOKEN_LPAREN) {
        match(TOKEN_LPAREN);
        ASTNode *node = expression();
        match(TOKEN_RPAREN);
        return node;
    }
    if (token.type == TOKEN_NUMBER) {
        ASTNode *node = create_node(NODE_NUMBER);
        node->data.num_value = token.value;
        node->expression_type = TYPE_INTEGER;
        match(TOKEN_NUMBER);
        return node;
    }
    if (token.type == TOKEN_TRUE || token.type == TOKEN_FALSE) {
        ASTNode *node = create_node(NODE_BOOLEAN);
        node->data.num_value = token.value;
        node->expression_type = TYPE_BOOLEAN;
        next_token();
        return node;
    }
    if (token.type == TOKEN_IDENTIFIER) {
        ASTNode *node = create_node(NODE_VARIABLE);
        int idx = find_var(token.text);
        node->data.var_idx = idx;
        node->expression_type = sym_table[idx].type;
        match(TOKEN_IDENTIFIER);
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
    init_lexer(source);
    match(TOKEN_PROGRAM);
    match(TOKEN_IDENTIFIER);
    match(TOKEN_SEMI);

    if (token.type == TOKEN_VAR) {
        match(TOKEN_VAR);
        while (token.type == TOKEN_IDENTIFIER) {
            char temporary_names[20][MAX_NAME];
            int count = 0;
            
            strcpy(temporary_names[count++], token.text);
            match(TOKEN_IDENTIFIER);
            
            while (token.type == TOKEN_COMMA) {
                match(TOKEN_COMMA);
                strcpy(temporary_names[count++], token.text);
                match(TOKEN_IDENTIFIER);
            }
            match(TOKEN_COLON);
            
            DataType target_type = TYPE_UNKNOWN;
            if (token.type == TOKEN_INTEGER) { target_type = TYPE_INTEGER; match(TOKEN_INTEGER); }
            else if (token.type == TOKEN_BOOLEAN) { target_type = TYPE_BOOLEAN; match(TOKEN_BOOLEAN); }
            else compile_error(token.line, "Unknown primitive category");
            
            for (int i = 0; i < count; i++) {
                add_var(temporary_names[i], target_type);
            }
            match(TOKEN_SEMI);
        }
    }

    match(TOKEN_BEGIN);
    ASTNode *root = create_node(NODE_COMPOUND);
    ASTNode *current = NULL;

    // Add parsing for writeln and readln inside statement loop in parse_ast():
    while (token.type == TOKEN_IDENTIFIER || token.type == TOKEN_WRITELN || token.type == TOKEN_READLN) {
        ASTNode *stmt = NULL;

        if (token.type == TOKEN_IDENTIFIER) {
            stmt = create_node(NODE_ASSIGN);
            stmt->data.var_idx = find_var(token.text);
            match(TOKEN_IDENTIFIER);
            match(TOKEN_ASSIGN);
            stmt->left = expression();
            match(TOKEN_SEMI);
        }
        else if (token.type == TOKEN_WRITELN) {
            match(TOKEN_WRITELN);
            match(TOKEN_LPAREN);
            
            ASTNode *head = NULL;
            ASTNode *current_expr = NULL;

            while (1) {
                ASTNode *expr = expression();
                ASTNode *write_node = create_node(NODE_WRITELN);
                write_node->left = expr;

                if (!head) head = write_node;
                else current_expr->next = write_node;
                current_expr = write_node;

                if (token.type == TOKEN_COMMA) match(TOKEN_COMMA);
                else break;
            }
            match(TOKEN_RPAREN);
            match(TOKEN_SEMI);
        }
        else if (token.type == TOKEN_READLN) {
            match(TOKEN_READLN);
            match(TOKEN_LPAREN);
            stmt = create_node(NODE_READLN);
            if (token.type == TOKEN_IDENTIFIER) {
                stmt->data.var_idx = find_var(token.text);
                match(TOKEN_IDENTIFIER);
            } else {
                compile_error(token.line, "readln expects a variable identifier");
            }
            match(TOKEN_RPAREN);
            match(TOKEN_SEMI);
        }

        if (!root->left) root->left = stmt;
        else current->next = stmt;
        current = stmt;
    }

    match(TOKEN_END);
    match(TOKEN_PERIOD);
    return root;
}

void free_ast(ASTNode *node) {
    if (!node) return;
    free_ast(node->left);
    free_ast(node->right);
    free_ast(node->next);
    free(node);
}

