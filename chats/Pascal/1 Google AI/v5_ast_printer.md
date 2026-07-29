step5_ast_printer.md

# Prompt

Implement an AST Printer to output a visual tree structure text representation

# Response

Here is the implementation of an AST Printer that visually outputs the structure of your Abstract Syntax Tree using text indents. [1] 
## Update compiler.h
Add the print function prototype to your header file:

#ifndef COMPILER_H#define COMPILER_H
#include "common.h"
extern Instruction code[MAX_CODE];extern int code_idx;extern char sym_table[MAX_SYMBOLS][MAX_NAME];extern int sym_count;
ASTNode *parse_ast(const char *source);ASTNode *optimize_ast(ASTNode *node);void generate_code(ASTNode *node);void free_ast(ASTNode *node);
// Add this linevoid print_ast(ASTNode *node, int indent);
#endif

## Add print_ast to compiler.c
Append this function to the bottom of your compiler.c file. It uses recursive spacing and box-drawing symbols to trace your execution flows. [2] 

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

## Update main.c
Hook the print function into your primary application pipeline right between the parsing and optimization phases:

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <source_file.pas>\n", argv);
        return 1;
    }

    char *pascal_program = read_file(argv);
    if (!pascal_program) return 1;

    printf("--- Phase 1: Parsing AST ---\n");
    ASTNode *ast = parse_ast(pascal_program);
    
    printf("\n--- Visual AST Representation (Unoptimized) ---\n");
    print_ast(ast, 0);

    printf("\n--- Phase 2: Optimizing AST ---\n");
    ast = optimize_ast(ast);

    printf("\n--- Visual AST Representation (Optimized) ---\n");
    print_ast(ast, 0);

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

## Sample Visual Output
If you compile and run a script containing result := x * 2 + 5;, your terminal will now visually print the parsed tree branches:

--- Visual AST Representation (Unoptimized) ---
[Compound Statement]
    [Assignment] -> Variable: result
        Value:
            [Binary Op] '+'
                Left:
                    [Binary Op] '*'
                        Left:
                            [Variable] x
                        Right:
                            [Number] 2
                Right:
                    [Number] 5

If you are ready to continue adding elements to your language layout, I can show you how to:

* Incorporate relational conditional tokens like >, <, and =
* Introduce jump offsets to support IF-THEN branch structures


[1] [https://hypefortypes.github.io](https://hypefortypes.github.io/S18/slides/parsing.pdf)
[2] [https://www.rose-hulman.edu](https://www.rose-hulman.edu/class/csse/csse120/202010/Sessions/Session06-in-class/BoxAndPointerDiagrams-Exercise.pdf)
