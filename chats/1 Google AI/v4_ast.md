step4_ast.md

# Prompt

implement an abstract syntax tree (AST) for cleaner optimization

# Response

To transition your compiler from a single-pass code generator to an AST-driven design, we must decouple parsing from bytecode generation.
By introducing an Abstract Syntax Tree (AST), the parser builds a tree structure in memory that mirrors the grammar of your code. This architectural change allows you to run pass-based optimizations (like Constant Folding) directly on the tree before emitting any VM instructions. [1, 2, 3] 
Here is the fully upgraded compiler system, containing updated header and source structures.
------------------------------
## common.h
We introduce an explicit NodeType enum and structural configurations for individual AST node types. [4] 

#ifndef COMMON_H#define COMMON_H
#define MAX_NAME 32#define MAX_SYMBOLS 100#define MAX_CODE 1000#define MAX_STACK 256
typedef enum {
    TOKEN_PROGRAM, TOKEN_VAR, TOKEN_BEGIN, TOKEN_END, TOKEN_INTEGER,
    TOKEN_IDENTIFIER, TOKEN_NUMBER, TOKEN_ASSIGN, TOKEN_PLUS, TOKEN_MINUS,
    TOKEN_MUL, TOKEN_DIV, TOKEN_SEMI, TOKEN_COLON, TOKEN_COMMA, TOKEN_PERIOD,
    TOKEN_EOF, TOKEN_UNKNOWN
} TokenType;
typedef struct {
    TokenType type;
    char text[MAX_NAME];
    int value;
} Token;
typedef enum { OP_PUSH, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_LOAD, OP_STORE, OP_HALT } OpCode;
typedef struct {
    OpCode op;
    int arg;
} Instruction;
// AST Nodestypedef enum {
    NODE_NUMBER,
    NODE_VARIABLE,
    NODE_BINARY_OP,
    NODE_ASSIGN,
    NODE_COMPOUND
} NodeType;
typedef struct ASTNode {
    NodeType type;
    union {
        // NODE_NUMBER
        int num_value;
        // NODE_VARIABLE / NODE_ASSIGN
        int var_idx;
    } data;
    TokenType op; // Used for NODE_BINARY_OP (+, -, *, /)
    struct ASTNode *left;
    struct ASTNode *right;
    struct ASTNode *next; // Linked list for sequential statements
} ASTNode;
#endif

------------------------------
## compiler.h
The compiler contract is updated to handle AST creation, optimization passes, and cleanup.

#ifndef COMPILER_H#define COMPILER_H
#include "common.h"
extern Instruction code[MAX_CODE];extern int code_idx;extern char sym_table[MAX_SYMBOLS][MAX_NAME];extern int sym_count;
ASTNode *parse_ast(const char *source);ASTNode *optimize_ast(ASTNode *node);void generate_code(ASTNode *node);void free_ast(ASTNode *node);
#endif

------------------------------
## compiler.c
This engine parses raw tokens into node expressions, optimizes branches by executing matching operations early (constant folding), and recursively walks the trees to emit VM opcodes.

#include <stdio.h>#include <stdlib.h>#include <string.h>#include "compiler.h"#include "lexer.h"
Instruction code[MAX_CODE];int code_idx = 0;
char sym_table[MAX_SYMBOLS][MAX_NAME];int sym_count = 0;
// Helper to instantiate raw node structs safely on heapstatic ASTNode *create_node(NodeType type) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error: AST memory allocation failed\n");
        exit(1);
    }
    node->type = type;
    return node;
}
static int find_var(const char *name) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i], name) == 0) return i;
    }
    printf("Compile Error: Unknown variable %s\n", name);
    exit(1);
}
static void add_var(const char *name) {
    strcpy(sym_table[sym_count++], name);
}
static void match(TokenType type) {
    if (token.type == type) next_token();
    else { printf("Compile Error: Unexpected token syntax\n"); exit(1); }
}
static ASTNode *expression(void);
static ASTNode *factor(void) {
    if (token.type == TOKEN_NUMBER) {
        ASTNode *node = create_node(NODE_NUMBER);
        node->data.num_value = token.value;
        match(TOKEN_NUMBER);
        return node;
    } else if (token.type == TOKEN_IDENTIFIER) {
        ASTNode *node = create_node(NODE_VARIABLE);
        node->data.var_idx = find_var(token.text);
        match(TOKEN_IDENTIFIER);
        return node;
    }
    printf("Compile Error: Expected factor\n");
    exit(1);
}
static ASTNode *term(void) {
    ASTNode *node = factor();
    while (token.type == TOKEN_MUL || token.type == TOKEN_DIV) {
        ASTNode *op_node = create_node(NODE_BINARY_OP);
        op_node->op = token.type;
        op_node->left = node;
        next_token();
        op_node->right = factor();
        node = op_node;
    }
    return node;
}
static ASTNode *expression(void) {
    ASTNode *node = term();
    while (token.type == TOKEN_PLUS || token.type == TOKEN_MINUS) {
        ASTNode *op_node = create_node(NODE_BINARY_OP);
        op_node->op = token.type;
        op_node->left = node;
        next_token();
        op_node->right = term();
        node = op_node;
    }
    return node;
}
ASTNode *parse_ast(const char *source) {
    init_lexer(source);

    match(TOKEN_PROGRAM);
    match(TOKEN_IDENTIFIER);
    match(TOKEN_SEMI);

    if (token.type == TOKEN_VAR) {
        match(TOKEN_VAR);
        while (token.type == TOKEN_IDENTIFIER) {
            add_var(token.text);
            match(TOKEN_IDENTIFIER);
            while (token.type == TOKEN_COMMA) {
                match(TOKEN_COMMA);
                add_var(token.text);
                match(TOKEN_IDENTIFIER);
            }
            match(TOKEN_COLON);
            match(TOKEN_INTEGER);
            match(TOKEN_SEMI);
        }
    }

    match(TOKEN_BEGIN);
    ASTNode *root = create_node(NODE_COMPOUND);
    ASTNode *current = NULL;

    while (token.type == TOKEN_IDENTIFIER) {
        ASTNode *assign_node = create_node(NODE_ASSIGN);
        assign_node->data.var_idx = find_var(token.text);
        match(TOKEN_IDENTIFIER);
        match(TOKEN_ASSIGN);
        assign_node->left = expression();
        match(TOKEN_SEMI);

        if (!root->left) {
            root->left = assign_node;
        } else {
            current->next = assign_node;
        }
        current = assign_node;
    }
    match(TOKEN_END);
    match(TOKEN_PERIOD);
    return root;
}
// AST Optimization: Constant Folding PassASTNode *optimize_ast(ASTNode *node) {
    if (!node) return NULL;

    // Recursively optimize children branches first
    node->left = optimize_ast(node->left);
    node->right = optimize_ast(node->right);
    node->next = optimize_ast(node->next);

    // If both operands are raw numbers, evaluate mathematical transformations at compile-time
    if (node->type == NODE_BINARY_OP && node->left->type == NODE_NUMBER && node->right->type == NODE_NUMBER) {
        int l_val = node->left->data.num_value;
        int r_val = node->right->data.num_value;
        int folded_val = 0;

        switch (node->op) {
            case TOKEN_PLUS:  folded_val = l_val + r_val; break;
            case TOKEN_MINUS: folded_val = l_val - r_val; break;
            case TOKEN_MUL:   folded_val = l_val * r_val; break;
            case TOKEN_DIV:   
                if (r_val == 0) { printf("Compile Error: Division by zero\n"); exit(1); }
                folded_val = l_val / r_val; 
                break;
            default: return node;
        }

        printf("[Optimization] Folded constants: %d and %d\n", l_val, r_val);
        free_ast(node->left);
        free_ast(node->right);
        
        node->type = NODE_NUMBER;
        node->data.num_value = folded_val;
        node->left = NULL;
        node->right = NULL;
    }
    return node;
}
// Code Generation: Post-Order Traversal Strategyvoid generate_code(ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case NODE_COMPOUND:
            generate_code(node->left);
            break;
        case NODE_ASSIGN:
            generate_code(node->left);
            code[code_idx++] = (Instruction){OP_STORE, node->data.var_idx};
            generate_code(node->next);
            break;
        case NODE_NUMBER:
            code[code_idx++] = (Instruction){OP_PUSH, node->data.num_value};
            break;
        case NODE_VARIABLE:
            code[code_idx++] = (Instruction){OP_LOAD, node->data.var_idx};
            break;
        case NODE_BINARY_OP:
            generate_code(node->left);
            generate_code(node->right);
            if (node->op == TOKEN_PLUS)  code[code_idx++] = (Instruction){OP_ADD, 0};
            if (node->op == TOKEN_MINUS) code[code_idx++] = (Instruction){OP_SUB, 0};
            if (node->op == TOKEN_MUL)   code[code_idx++] = (Instruction){OP_MUL, 0};
            if (node->op == TOKEN_DIV)   code[code_idx++] = (Instruction){OP_DIV, 0};
            break;
    }
}
void free_ast(ASTNode *node) {
    if (!node) return;
    free_ast(node->left);
    free_ast(node->right);
    free_ast(node->next);
    free(node);
}

------------------------------
## main.c
main.c orchestrates the pipeline: parsing to AST, optimizing the tree, and walking the structured AST to emit bytecode. [5] 

#include <stdio.h>#include <stdlib.h>#include "compiler.h"#include "vm.h"
char *read_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Could not open file %s\n", filename);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = malloc(length + 1);
    if (!buffer) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        fclose(file);
        return NULL;
    }
    size_t read_bytes = fread(buffer, 1, length, file);
    buffer[read_bytes] = '\0';
    fclose(file);
    return buffer;
}
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <source_file.pas>\n", argv[0]);
        return 1;
    }

    char *pascal_program = read_file(argv[1]);
    if (!pascal_program) return 1;

    printf("--- Phase 1: Parsing AST ---\n");
    ASTNode *ast = parse_ast(pascal_program);

    printf("\n--- Phase 2: Optimizing AST ---\n");
    ast = optimize_ast(ast);

    printf("\n--- Phase 3: Emitting VM Bytecode ---\n");
    generate_code(ast);
    code[code_idx++] = (Instruction){OP_HALT, 0};
    printf("Compilation completed. Total Instructions: %d\n", code_idx);

    printf("\n--- Phase 4: Virtual Machine execution ---\n");
    run_vm();

    printf("\n--- Execution Output Results ---\n");
    for (int i = 0; i < sym_count; i++) {
        printf("%s = %d\n", sym_table[i], vm_vars[i]);
    }

    free_ast(ast);
    free(pascal_program);
    return 0;
}

(Note: lexer.c, lexer.h, vm.c, and vm.h remain identical to your previous multi-file codebase structure).
------------------------------
## How to Test Optimization
If you pass a Pascal file (test.pas) containing a static calculation expression like this:

program OptimizeTest;
var result : integer;
begin
  result := 10 + 20 * 3;
end.

The optimizer detects operations on literal numbers and computes them entirely during compilation.
Instead of generating separate runtime calculation codes (PUSH 10, PUSH 20, PUSH 3, MUL, ADD), it collapses the entire branch into a single execution step:

--- Phase 1: Parsing AST ---

--- Phase 2: Optimizing AST ---
[Optimization] Folded constants: 20 and 3
[Optimization] Folded constants: 10 and 60

--- Phase 3: Emitting VM Bytecode ---
Compilation completed. Total Instructions: 3

Now, the VM only needs to execute PUSH 70 and STORE.
If you would like to keep growing this layout, I can show you how to:

* Add Dead Code Elimination (e.g., removing variables that are assigned but never used)
* Implement an AST Printer to output a visual tree structure text representation


[1] [https://ruslanspivak.com](https://ruslanspivak.com/lsbasi-part7/)
[2] [https://swastik-mukherjee.medium.com](https://swastik-mukherjee.medium.com/the-technical-foundations-of-static-application-security-testing-sast-0ef7c9ca9a24)
[3] [https://www.sciencedirect.com](https://www.sciencedirect.com/science/article/pii/S0167404824003870)
[4] [https://steemit.com](https://steemit.com/utopianio/@drifter1/writing-a-simple-compiler-on-my-own-abstract-syntax-tree-structure-c-flex-bison)
[5] [https://hackernoon.com](https://hackernoon.com/dumping-a-c-programs-ast-with-the-psyche-c-compiler-frontend)
