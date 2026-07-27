v10_file_and_number.md

# Prompt

Next, I’d like having it providing filename and exact line number diagnostics on syntax crashes

# Response

Here is how to integrate precise file and line number diagnostic reporting across your toolchain.
We will:
1. Store source location metadata in Token and ASTNode.
2. Track line numbers in lexer.c.
3. Add a centralized error_at() function in compiler.c that formats diagnostics as filename:line: Error message.
4. Thread the source filename into parse_ast() and type_check().
1. common.h Updates
Add line tracking to Token and ASTNode:
typedef struct {
    TokenType type;
    char text[MAX_NAME];
    int value;
    int line; // Track source line number
} Token;

typedef struct ASTNode {
    NodeType type;
    DataType expression_type; // Evaluated data type
    int line;                 // Source line associated with this node
    union {
        int num_value;        // NODE_NUMBER / NODE_BOOLEAN (0 or 1)
        int var_idx;          // NODE_VARIABLE / NODE_ASSIGN
    } data;
    TokenType op; 
    struct ASTNode *left;
    struct ASTNode *right;
    struct ASTNode *next; 
} ASTNode;

2. lexer.h & lexer.c Updates
Update lexer.c to track newlines (\n) inside normal whitespace loops and multiline { ... } comment blocks.
lexer.h:
#define LEXER_H

#include "common.h"

extern Token token;
extern int current_line;

void init_lexer(const char *source);
void next_token(void);

#endif

lexer.c:
#include <string.h>
#include <ctype.h>
#include "lexer.h"

static const char *src;
Token token;
int current_line = 1;

void init_lexer(const char *source) {
    src = source;
    current_line = 1;
    next_token();
}

void next_token(void) {
    while (1) {
        // 1. Skip standard whitespace and increment line numbers
        while (*src && isspace(*src)) {
            if (*src == '\n') {
                current_line++;
            }
            src++;
        }

        // 2. Skip Pascal standard bracket comments { ... }
        if (*src == '{') {
            src++; // Skip the opening '{'
            while (*src && *src != '}') {
                if (*src == '\n') {
                    current_line++;
                }
                src++; // Consume all comment content characters
            }
            if (*src == '}') {
                src++; // Skip the closing '}'
                continue; // Loop back up to catch any whitespace or sequential comments
            }
        } else {
            break;
        }
    }

    // Attach current line metadata to every generated token
    token.line = current_line;

    if (!*src) { token.type = TOKEN_EOF; return; }

    if (isalpha(*src)) {
        char *p = token.text;
        while (isalnum(*src)) *p++ = *src++;
        *p = '\0';

        if (strcasecmp(token.text, "program") == 0) token.type = TOKEN_PROGRAM;
        else if (strcasecmp(token.text, "var") == 0) token.type = TOKEN_VAR;
        else if (strcasecmp(token.text, "begin") == 0) token.type = TOKEN_BEGIN;
        else if (strcasecmp(token.text, "end") == 0) token.type = TOKEN_END;
        else if (strcasecmp(token.text, "integer") == 0) token.type = TOKEN_INTEGER;
        else if (strcasecmp(token.text, "boolean") == 0) token.type = TOKEN_BOOLEAN;
        else if (strcasecmp(token.text, "true") == 0) { token.type = TOKEN_TRUE; token.value = 1; }
        else if (strcasecmp(token.text, "false") == 0) { token.type = TOKEN_FALSE; token.value = 0; }
        else token.type = TOKEN_IDENTIFIER;
        return;
    }

    if (isdigit(*src)) {
        token.type = TOKEN_NUMBER;
        token.value = 0;
        while (isdigit(*src)) token.value = token.value * 10 + (*src++ - '0');
        return;
    }

    if (*src == ':' && *(src + 1) == '=') { token.type = TOKEN_ASSIGN; src += 2; return; }
    if (*src == ':') { token.type = TOKEN_COLON; src++; return; }
    if (*src == ';') { token.type = TOKEN_SEMI; src++; return; }
    if (*src == ',') { token.type = TOKEN_COMMA; src++; return; }
    if (*src == '.') { token.type = TOKEN_PERIOD; src++; return; }
    if (*src == '+') { token.type = TOKEN_PLUS; src++; return; }
    if (*src == '-') { token.type = TOKEN_MINUS; src++; return; }
    if (*src == '*') { token.type = TOKEN_MUL; src++; return; }
    if (*src == '/') { token.type = TOKEN_DIV; src++; return; }

    token.type = TOKEN_UNKNOWN;
    src++;
}

3. compiler.h & compiler.c Diagnostics Engine
We add a centralized compile_error(int line, const char *msg, ...) helper and attach line numbers when allocating AST nodes.
compiler.h:
#ifndef COMPILER_H
#define COMPILER_H

#include "common.h"

extern Instruction code[MAX_CODE];
extern int code_idx;
extern Symbol sym_table[MAX_SYMBOLS];
extern int sym_count;

ASTNode *parse_ast(const char *source, const char *filename);
ASTNode *optimize_ast(ASTNode *node);
ASTNode *eliminate_dead_code(ASTNode *node);
void type_check(ASTNode *node, const char *filename);
void generate_code(ASTNode *node);
void free_ast(ASTNode *node);
void print_ast(ASTNode *node, int indent);

#endif

Key additions to compiler.c:
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "compiler.h"
#include "lexer.h"

Instruction code[MAX_CODE];
int code_idx = 0;

Symbol sym_table[MAX_SYMBOLS];
int sym_count = 0;

static const char *current_filename = "<source>";

// Centralized error diagnostic formatting
static void compile_error(int line, const char *fmt, ...) {
    fprintf(stderr, "%s:%d: Compile Error: ", current_filename, line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    exit(1);
}

static ASTNode *create_node(NodeType type) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    if (!node) { 
        fprintf(stderr, "Memory failure\n"); 
        exit(1); 
    }
    node->type = type;
    node->expression_type = TYPE_UNKNOWN;
    node->line = token.line; // Record exact line where this node was parsed
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

static void match(TokenType type) {
    if (token.type == type) {
        next_token();
    } else {
        compile_error(token.line, "Unexpected token '%s'", token.text[0] ? token.text : "EOF");
    }
}

// Update AST Parser entry point to store filename context:
ASTNode *parse_ast(const char *source, const char *filename) {
    current_filename = filename ? filename : "<source>";
    init_lexer(source);
    // ... parse logic remains unchanged ...

Update type_check in compiler.c:
Now type_check uses the line number stored inside the AST node:
void type_check(ASTNode *node, const char *filename) {
    if (!node) return;
    if (filename) current_filename = filename;

    type_check(node->left, filename);
    type_check(node->right, filename);

    switch (node->type) {
        case NODE_COMPOUND:
            node->expression_type = TYPE_UNKNOWN;
            break;

        case NODE_ASSIGN: {
            DataType var_type = sym_table[node->data.var_idx].type;
            DataType val_type = node->left->expression_type;
            if (var_type != val_type) {
                compile_error(node->line, 
                              "Type mismatch: cannot assign value to variable '%s'", 
                              sym_table[node->data.var_idx].name);
            }
            node->expression_type = var_type;
            type_check(node->next, filename);
            break;
        }

        case NODE_BINARY_OP: {
            if (node->left->expression_type != TYPE_INTEGER || node->right->expression_type != TYPE_INTEGER) {
                compile_error(node->line, "Type mismatch: binary operations require integer operands");
            }
            node->expression_type = TYPE_INTEGER;
            break;
        }

        case NODE_NUMBER:   node->expression_type = TYPE_INTEGER; break;
        case NODE_BOOLEAN:  node->expression_type = TYPE_BOOLEAN; break;
        case NODE_VARIABLE: node->expression_type = sym_table[node->data.var_idx].type; break;
    }
}

4. main.c Updates
Pass argv[2] (the source filename) into parse_ast and type_check:
// In main.c (-c branch):
char *src_filename = argv[2];
char *pascal_program = read_file(src_filename);
if (!pascal_program) return 1;

printf("--- Phase 1: Parsing AST ---\n");
ASTNode *ast = parse_ast(pascal_program, src_filename);

printf("\n--- Phase 2: Type Validation Checking ---\n");
type_check(ast, src_filename);

💡 Example Diagnostic Output
If test.pas has a type mismatch on line 12:
test.pas:12: Compile Error: Type mismatch: cannot assign value to variable 'x'