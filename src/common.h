#ifndef COMMON_H
#define COMMON_H

#define MAX_NAME 32
#define MAX_SYMBOLS 100
#define MAX_CODE 500
#define MAX_STACK 100
#define MAX_STRING_LEN 256
#define MAX_STRINGS 256

typedef enum {
    TOKEN_PROGRAM, TOKEN_VAR, TOKEN_BEGIN, TOKEN_END,
    TOKEN_INTEGER, TOKEN_BOOLEAN,
    TOKEN_IDENTIFIER, TOKEN_NUMBER, TOKEN_TRUE, TOKEN_FALSE,
    TOKEN_ASSIGN, TOKEN_PLUS, TOKEN_MINUS, TOKEN_MUL, TOKEN_DIV,
    TOKEN_EQ, TOKEN_LT, TOKEN_GT,
    TOKEN_AND, TOKEN_OR, TOKEN_NOT,
    TOKEN_LTE, TOKEN_GTE, TOKEN_NEQ,
    TOKEN_DIV_KW, TOKEN_MOD, TOKEN_XOR,
    TOKEN_SEMI, TOKEN_COLON, TOKEN_COMMA, TOKEN_PERIOD,
    TOKEN_LPAREN, TOKEN_RPAREN,
    TOKEN_WRITELN, TOKEN_WRITE, TOKEN_READLN,
    TOKEN_IF, TOKEN_THEN, TOKEN_ELSE,
    TOKEN_WHILE, TOKEN_DO,
    TOKEN_REPEAT, TOKEN_UNTIL,
    TOKEN_STRING, TOKEN_STRING_TYPE,
    TOKEN_FOR, TOKEN_TO, TOKEN_DOWNTO,
    TOKEN_EOF
} TokenType;

typedef enum {
    TYPE_UNKNOWN,
    TYPE_INTEGER,
    TYPE_BOOLEAN,
    TYPE_STRING
} DataType;

typedef struct {
    TokenType type;
    char text[MAX_NAME];
    char string_value[MAX_STRING_LEN]; // populated only for TOKEN_STRING
    int value;
    int line;
} Token;

typedef struct {
    char name[MAX_NAME];
    DataType type;
} Symbol;

typedef enum {
    OP_PUSH, OP_LOAD, OP_STORE,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV,
    OP_EQ, OP_LT, OP_GT,
    OP_AND, OP_OR, OP_NOT,
    OP_LTE, OP_GTE, OP_NEQ,
    OP_NEG,
    OP_MOD, OP_XOR,
    OP_PRINT,     // Pop a value; print it with NO trailing newline.
    OP_READ,
    OP_HALT,
    OP_JMP,  // Unconditional jump. arg = absolute target instruction index.
    OP_JZ,   // Pop the stack; if the value is zero (false), jump to arg.
             // Otherwise fall through to the next instruction.
    OP_PUSH_STR,  // Push a string_pool[] index (arg) onto the stack.
    OP_PRINT_STR, // Pop an index; print string_pool[index] with NO
                  // trailing newline.
    OP_SEQ,       // Pop two indices; push 1 if string_pool[] contents are
                  // equal (strcmp), else 0. '<>' is OP_SEQ followed by
                  // OP_NOT - no separate string-not-equal opcode needed.
    OP_SCONCAT,   // Pop two indices; concatenate their string_pool[]
                  // contents, intern the result (possibly growing the
                  // pool at runtime), and push the new index.
    OP_NEWLINE    // Print a newline. No operand, no stack interaction.
                  // writeln emits this once, after all its arguments;
                  // write never emits it.
} Opcode;

typedef struct {
    Opcode op;
    int arg;
} Instruction;

typedef enum {
    NODE_COMPOUND,
    NODE_ASSIGN,
    NODE_UNARY_OP,
    NODE_BINARY_OP,
    NODE_NUMBER,
    NODE_BOOLEAN,
    NODE_VARIABLE,
    NODE_WRITELN,  // Also covers 'write' - node->op is TOKEN_WRITE or
                   // TOKEN_WRITELN (only the latter appends a newline).
                   // node->left is the head of the argument list, chained
                   // via each argument's own ->next (same technique as a
                   // statement list) - NULL means zero arguments.
    NODE_READLN,
    NODE_IF,
    NODE_WHILE,
    NODE_REPEAT,
    NODE_STRING,
    NODE_FOR
} NodeType;

typedef struct ASTNode {
    NodeType type;
    TokenType op;
    DataType expression_type;
    int line;
    union {
        int num_value;
        int var_idx;
    } data;
    struct ASTNode *left;
    struct ASTNode *right;
    struct ASTNode *next;
    struct ASTNode *extra;  // Node-specific 4th child. Currently only used
                             // by NODE_IF, for the optional else-branch.
} ASTNode;

// Shared Global State
extern Instruction code[MAX_CODE];
extern int code_idx;
extern Symbol sym_table[MAX_SYMBOLS];
extern int sym_count;
extern char string_pool[MAX_STRINGS][MAX_STRING_LEN];
extern int string_count;
extern Token token;

#endif

