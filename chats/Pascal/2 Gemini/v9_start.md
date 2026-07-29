
Thanks for providing the complete codebase! The architecture is extremely clean and easy to follow.

A quick note on your current implementation: notice in bytecode.c [source: 11] that fwrite and fread write/read sizeof(char) * MAX_NAME bytes for the symbol table [source: 11], but sym_table is an array of Symbol structs (sizeof(Symbol)) [source: 3, 10]. Since Symbol contains char name[MAX_NAME] and DataType type [source: 10], serializing only MAX_NAME bytes truncates the struct and loses the DataType field when loading bytecodes [source: 10, 11]. Changing sizeof(char) * MAX_NAME to sizeof(Symbol) in bytecode.c will keep your saved binary headers completely accurate [source: 10, 11].

Now, to tackle the Immediate Next Steps, here is the roadmap breakdown along with the necessary additions for each file.

🛠️ Step 1: Relational Operators (=, <, >)
1. common.h Updates
Add token types, opcodes, and a node type for binary comparisons [source: 10]:
// Add to TokenType enum:
TOKEN_EQ, TOKEN_LT, TOKEN_GT,

// Add to OpCode enum:
OP_EQ, OP_LT, OP_GT,

2. lexer.c Updates
Update next_token() to recognize comparison operators [source: 2]:
if (*src == '=') { token.type = TOKEN_EQ; src++; return; }
if (*src == '<') { token.type = TOKEN_LT; src++; return; }
if (*src == '>') { token.type = TOKEN_GT; src++; return; }

3. compiler.c Updates
• Parser (expression): Extend grammar hierarchy so relational operations have lower precedence than arithmetic ops (+, -, *, /) [source: 3]:
static ASTNode *relational_expression(void) {
    ASTNode *node = expression();
    while (token.type == TOKEN_EQ || token.type == TOKEN_LT || token.type == TOKEN_GT) {
        ASTNode *op_node = create_node(NODE_BINARY_OP);
        op_node->op = token.type;
        op_node->left = node;
        next_token();
        op_node->right = expression();
        node = op_node;
    }
    return node;
}

• Type Checker (type_check): Enforce that relational operators require TYPE_INTEGER operands and output a TYPE_BOOLEAN expression type [source: 3, 10]:
case NODE_BINARY_OP: {
    if (node->op == TOKEN_EQ || node->op == TOKEN_LT || node->op == TOKEN_GT) {
        if (node->left->expression_type != TYPE_INTEGER || node->right->expression_type != TYPE_INTEGER) {
            printf("Type Error: Relational comparison operators require integer targets.\n");
            exit(1);
        }
        node->expression_type = TYPE_BOOLEAN;
    } else {
        // Standard arithmetic operations (+, -, *, /)
        if (node->left->expression_type != TYPE_INTEGER || node->right->expression_type != TYPE_INTEGER) {
            printf("Type Error: Arithmetic operators require integer targets.\n");
            exit(1);
        }
        node->expression_type = TYPE_INTEGER;
    }
    break;
}

• Code Generator (generate_code): Emit binary conditional evaluation opcodes [source: 3]:
if (node->op == TOKEN_EQ) code[code_idx++] = (Instruction){OP_EQ, 0};
if (node->op == TOKEN_LT) code[code_idx++] = (Instruction){OP_LT, 0};
if (node->op == TOKEN_GT) code[code_idx++] = (Instruction){OP_GT, 0};

4. vm.c Execution
Simulate comparison evaluations and push boolean outputs (1 or 0) back to the stack [source: 8]:
case OP_EQ: vm_stack[sp - 1] = (vm_stack[sp - 1] == vm_stack[sp]); sp--; break;
case OP_LT: vm_stack[sp - 1] = (vm_stack[sp - 1] <  vm_stack[sp]); sp--; break;
case OP_GT: vm_stack[sp - 1] = (vm_stack[sp - 1] >  vm_stack[sp]); sp--; break;

🔀 Step 2: Control Flow (IF-THEN-ELSE, WHILE)
1. common.h Updates
Add tokens, AST node types, and jump opcodes [source: 10]:
// TokenType:
TOKEN_IF, TOKEN_THEN, TOKEN_ELSE, TOKEN_WHILE, TOKEN_DO,

// NodeType:
NODE_IF, NODE_WHILE,

// OpCode:
OP_JMP,          // Unconditional jump to target index (instr.arg)
OP_JMP_IF_FALSE, // Pops condition; jumps to target index if condition == 0

2. lexer.c Keyword Recognition
Map keywords inside next_token() [source: 2]:
else if (strcasecmp(token.text, "if") == 0) token.type = TOKEN_IF;
else if (strcasecmp(token.text, "then") == 0) token.type = TOKEN_THEN;
else if (strcasecmp(token.text, "else") == 0) token.type = TOKEN_ELSE;
else if (strcasecmp(token.text, "while") == 0) token.type = TOKEN_WHILE;
else if (strcasecmp(token.text, "do") == 0) token.type = TOKEN_DO;

3. compiler.c Control Flow Node Generation & Backpatching
Add parsing methods for control flow structures [source: 3]:
static ASTNode *statement(void);

static ASTNode *if_statement(void) {
    match(TOKEN_IF);
    ASTNode *node = create_node(NODE_IF);
    node->left = relational_expression(); // Condition
    match(TOKEN_THEN);
    node->right = statement();             // 'Then' body branch

    if (token.type == TOKEN_ELSE) {
        match(TOKEN_ELSE);
        node->next = statement();          // 'Else' body branch attached to next pointer
    }
    return node;
}

static ASTNode *while_statement(void) {
    match(TOKEN_WHILE);
    ASTNode *node = create_node(NODE_WHILE);
    node->left = relational_expression(); // Condition
    match(TOKEN_DO);
    node->right = statement();             // Loop body
    return node;
}

Backpatching in generate_code:
Control flow requires calculating destination offsets for OP_JMP and OP_JMP_IF_FALSE instructions [source: 3]:
case NODE_IF: {
    generate_code(node->left); // Emit condition evaluation
    int jump_false_idx = code_idx;
    code[code_idx++] = (Instruction){OP_JMP_IF_FALSE, 0}; // Placeholder target

    generate_code(node->right); // 'Then' block execution code

    if (node->next) { // Optional 'Else' block present
        int jump_exit_idx = code_idx;
        code[code_idx++] = (Instruction){OP_JMP, 0}; // Skip 'Else' after 'Then' finishes
        
        // Backpatch: update conditional jump location to point to 'Else' start
        code[jump_false_idx].arg = code_idx;

        generate_code(node->next); // 'Else' block body
        code[jump_exit_idx].arg = code_idx; // Backpatch exit jump destination
    } else {
        // Backpatch: jump to instruction immediately following 'Then' block
        code[jump_false_idx].arg = code_idx;
    }
    break;
}

case NODE_WHILE: {
    int loop_start_idx = code_idx;
    generate_code(node->left); // Evaluate condition
    
    int jump_false_idx = code_idx;
    code[code_idx++] = (Instruction){OP_JMP_IF_FALSE, 0}; // Jump past loop body if condition fails
    
    generate_code(node->right); // Loop body code execution
    
    code[code_idx++] = (Instruction){OP_JMP, loop_start_idx}; // Jump back to evaluate loop condition
    code[jump_false_idx].arg = code_idx; // Backpatch loop exit destination
    break;
}

4. vm.c Execution Jumps
Update the VM loop with IP jumping mechanisms [source: 8]:
case OP_JMP:
    ip = instr.arg;
    break;

case OP_JMP_IF_FALSE: {
    int condition = vm_stack[sp--];
    if (!condition) {
        ip = instr.arg;
    }
    break;
}

📍 Step 3: Precise Lexer Line Tracking & Error Context
1. common.h Metadata Additions
Include positional metadata in the Token and ASTNode structures [source: 10]:
typedef struct {
    TokenType type;
    char text[MAX_NAME];
    int value;
    int line; // Track source line number
} Token;

typedef struct ASTNode {
    NodeType type;
    DataType expression_type;
    int line; // Preserve context for error passes (e.g., type check pass)
    // ... rest of fields
} ASTNode;

2. lexer.c Positional Line Increments
Initialize line counters and increment on newlines [source: 2]:
static int current_line = 1;

void init_lexer(const char *source) {
    src = source;
    current_line = 1;
    next_token();
}

// In next_token():
while (*src) {
    if (*src == '\n') {
        current_line++;
        src++;
        continue;
    }
    if (isspace(*src)) { src++; continue; }
    // ... comment handling ...
}

// Store current line location inside token
token.line = current_line;

Which feature would you like to build out and test first?


# Prompt

First, let’s fix the problem about the bytecode.c and write/read sym_table serialization. I suspect it’s because sym_table was only a char* before but now has a type as well.

# Response

You nailed it—that is exactly what happened.
When sym_table held raw strings, writing sizeof(char) * MAX_NAME per symbol entry was sufficient. When Symbol was upgraded to a struct containing both char name[MAX_NAME] and DataType type in common.h, the size of each entry expanded to sizeof(Symbol). Writing only sizeof(char) * MAX_NAME truncated the struct, stripping the type field and misaligning array offsets when reading back the binary image.
Here is the updated bytecode.c file using sizeof(Symbol) for accurate binary serialization:
#include <stdio.h>
#include <string.h>
#include "bytecode.h"
#include "compiler.h"

int save_bytecode(const char *filename) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        fprintf(stderr, "Error: Could not open file %s for binary write\n", filename);
        return 0;
    }

    BytecodeHeader header;
    memcpy(header.magic, "PASC", 4);
    header.version = 1;
    header.sym_count = sym_count;
    header.code_idx = code_idx;

    // Write metadata tracking information
    fwrite(&header, sizeof(BytecodeHeader), 1, file);

    // Write complete Symbol structs (name + type)
    fwrite(sym_table, sizeof(Symbol), sym_count, file);

    // Write instruction code block chunks directly
    fwrite(code, sizeof(Instruction), code_idx, file);

    fclose(file);
    printf("[Bytecode Module] Binary successfully saved to %s\n", filename);
    return 1;
}

int load_bytecode(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: Could not load bytecode binary %s\n", filename);
        return 0;
    }

    BytecodeHeader header;
    if (fread(&header, sizeof(BytecodeHeader), 1, file) != 1) {
        fprintf(stderr, "Error: Corrupted bytecode header format\n");
        fclose(file);
        return 0;
    }

    if (memcmp(header.magic, "PASC", 4) != 0) {
        fprintf(stderr, "Error: Invalid file format magic signatures\n");
        fclose(file);
        return 0;
    }

    // Restore state details back inside internal module layers
    sym_count = header.sym_count;
    code_idx = header.code_idx;

    // Read complete Symbol structs back into memory
    fread(sym_table, sizeof(Symbol), sym_count, file);
    fread(code, sizeof(Instruction), code_idx, file);

    fclose(file);
    printf("[Bytecode Module] Loaded executable successfully (%d instructions, %d symbols)\n", code_idx, sym_count);
    return 1;
}


