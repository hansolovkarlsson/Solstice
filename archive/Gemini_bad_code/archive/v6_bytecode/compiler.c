#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"
#include "lexer.h"

Instruction code[MAX_CODE];
int code_idx = 0;

char sym_table[MAX_SYMBOLS][MAX_NAME];
int sym_count = 0;

// Helper to instantiate raw node structs safely on heap
static ASTNode *create_node(NodeType type) {
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

// AST Optimization: Constant Folding Pass
ASTNode *optimize_ast(ASTNode *node) {
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

// Code Generation: Post-Order Traversal Strategy
void generate_code(ASTNode *node) {
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


static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        printf("    ");
    }
}

static const char* token_type_to_str(TokenType type) {
    switch (type) {
        case TOKEN_PLUS:  return "+";
        case TOKEN_MINUS: return "-";
        case TOKEN_MUL:   return "*";
        case TOKEN_DIV:   return "/";
        default:          return "?";
    }
}

void print_ast(ASTNode *node, int indent) {
    if (!node) return;

    print_indent(indent);

    switch (node->type) {
        case NODE_COMPOUND:
            printf("[Compound Statement]\n");
            print_ast(node->left, indent + 1);
            break;

        case NODE_ASSIGN:
            printf("[Assignment] -> Variable: %s\n", sym_table[node->data.var_idx]);
            print_indent(indent + 1);
            printf("Value:\n");
            print_ast(node->left, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_BINARY_OP:
            printf("[Binary Op] '%s'\n", token_type_to_str(node->op));
            print_indent(indent + 1);
            printf("Left:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Right:\n");
            print_ast(node->right, indent + 2);
            break;

        case NODE_NUMBER:
            printf("[Number] %d\n", node->data.num_value);
            break;

        case NODE_VARIABLE:
            printf("[Variable] %s\n", sym_table[node->data.var_idx]);
            break;
    }
}
