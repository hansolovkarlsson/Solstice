#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"
#include "lexer.h"

Instruction code[MAX_CODE];
int code_idx = 0;

Symbol sym_table[MAX_SYMBOLS];
int sym_count = 0;

static ASTNode *create_node(NodeType type) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    if (!node) { printf("Memory failure\n"); exit(1); }
    node->type = type;
    node->expression_type = TYPE_UNKNOWN;
    return node;
}

static int find_var(const char *name) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) return i;
    }
    printf("Compile Error: Unknown variable '%s'\n", name);
    exit(1);
}

static void add_var(const char *name, DataType type) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i].name, name) == 0) {
            printf("Compile Error: Duplicate variable declaration '%s'\n", name);
            exit(1);
        }
    }
    strcpy(sym_table[sym_count].name, name);
    sym_table[sym_count].type = type;
    sym_count++;
}

static void match(TokenType type) {
    if (token.type == type) next_token();
    else { printf("Compile Error: Token mismatch syntax error\n"); exit(1); }
}

static ASTNode *expression(void);

static ASTNode *factor(void) {
    if (token.type == TOKEN_NUMBER) {
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
    } else if (token.type == TOKEN_IDENTIFIER) {
        ASTNode *node = create_node(NODE_VARIABLE);
        int idx = find_var(token.text);
        node->data.var_idx = idx;
        node->expression_type = sym_table[idx].type;
        match(TOKEN_IDENTIFIER);
        return node;
    }
    printf("Compile Error: Invalid factor entry\n");
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
            else { printf("Compile Error: Unknown primitive category\n"); exit(1); }
            
            for (int i = 0; i < count; i++) {
                add_var(temporary_names[i], target_type);
            }
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

        if (!root->left) root->left = assign_node;
        else current->next = assign_node;
        current = assign_node;
    }
    match(TOKEN_END);
    match(TOKEN_PERIOD);
    return root;
}

// --- GLOBAL TYPE CHECK VALIDATION PASS ---
void type_check(ASTNode *node) {
    if (!node) return;

    // Post-order traversal: check leaf child nodes first
    type_check(node->left);
    type_check(node->right);

    switch (node->type) {
        case NODE_COMPOUND:
            node->expression_type = TYPE_UNKNOWN;
            break;

        case NODE_ASSIGN: {
            DataType var_type = sym_table[node->data.var_idx].type;
            DataType val_type = node->left->expression_type;
            if (var_type != val_type) {
                printf("Type Error: Variable '%s' requires type matching, cannot assign mismatched structural payload.\n", 
                       sym_table[node->data.var_idx].name);
                exit(1);
            }
            node->expression_type = var_type;
            type_check(node->next); // Move down assignment chain sequence
            break;
        }

        case NODE_BINARY_OP: {
            // Arithmetic operators (+, -, *, /) are only valid for integers
            if (node->left->expression_type != TYPE_INTEGER || node->right->expression_type != TYPE_INTEGER) {
                printf("Type Error: Binary operations require numeric components. Math expressions reject Boolean arguments.\n");
                exit(1);
            }
            node->expression_type = TYPE_INTEGER;
            break;
        }

        case NODE_NUMBER:   node->expression_type = TYPE_INTEGER; break;
        case NODE_BOOLEAN:  node->expression_type = TYPE_BOOLEAN; break;
        case NODE_VARIABLE: node->expression_type = sym_table[node->data.var_idx].type; break;
    }
}

// Code generation and optimization passes remain mostly unchanged...


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
            
        // Add NODE_BOOLEAN here to fall through into NODE_NUMBER logic
        case NODE_BOOLEAN:
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
            // Fix: Added .name to access the string inside the Symbol struct
            printf("[Assignment] -> Variable: %s\n", sym_table[node->data.var_idx].name);
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

        case NODE_BOOLEAN:
            printf("[Boolean] %s\n", node->data.num_value ? "true" : "false");
            break;

        case NODE_VARIABLE:
            // Fix: Added .name to access the string inside the Symbol struct
            printf("[Variable] %s\n", sym_table[node->data.var_idx].name);
            break;
    }
}

// Track whether a variable index is ever read/used across expressions
static int var_used_tracker[MAX_SYMBOLS];

// Pass 1: Recursively look for read occurrences (NODE_VARIABLE) 
static void mark_used_variables(ASTNode *node) {
    if (!node) return;

    if (node->type == NODE_VARIABLE) {
        var_used_tracker[node->data.var_idx] = 1;
    }

    mark_used_variables(node->left);
    mark_used_variables(node->right);
    mark_used_variables(node->next);
}

// Pass 2: Sweep and remove assignments to variables that were never marked as used
static ASTNode *sweep_dead_assignments(ASTNode *node) {
    if (!node) return NULL;

    if (node->type == NODE_COMPOUND) {
        node->left = sweep_dead_assignments(node->left);
        return node;
    }

    if (node->type == NODE_ASSIGN) {
        int var_idx = node->data.var_idx;
        
        // Process downstream sequential assignments first
        node->next = sweep_dead_assignments(node->next);

        // If the variable being assigned to is never read anywhere, drop this assignment
        if (!var_used_tracker[var_idx]) {
            printf("[DCE Optimization] Removing dead assignment to unreferenced variable: %s\n", 
                   sym_table[var_idx].name);
            
            ASTNode *next_cached = node->next;
            
            // Isolate children so we don't accidentally free the rest of the program chain
            node->left = optimize_ast(node->left); // clear underlying mathematical expressions safely
            free_ast(node->left);
            node->left = NULL;
            node->next = NULL;
            free(node);
            
            return next_cached; // Splice out of linked list
        }
        
        node->left = sweep_dead_assignments(node->left);
        return node;
    }

    node->left = sweep_dead_assignments(node->left);
    node->right = sweep_dead_assignments(node->right);
    return node;
}

ASTNode *eliminate_dead_code(ASTNode *node) {
    // Clear out historical usage flags
    memset(var_used_tracker, 0, sizeof(var_used_tracker));

    // Step 1: Scan tree to discover which variables are genuinely read from
    mark_used_variables(node);

    // Step 2: Prune assignments writing into dead zones
    return sweep_dead_assignments(node);
}
