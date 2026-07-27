#ifndef COMMON_H
#define COMMON_H

#define MAX_NAME 32
#define MAX_SYMBOLS 100
#define MAX_CODE 500
#define MAX_STACK 100

typedef enum {
    TOKEN_PROGRAM, TOKEN_VAR, TOKEN_BEGIN, TOKEN_END,
    TOKEN_INTEGER, TOKEN_BOOLEAN,
    TOKEN_IDENTIFIER, TOKEN_NUMBER, TOKEN_TRUE, TOKEN_FALSE,
    TOKEN_ASSIGN, TOKEN_PLUS, TOKEN_MINUS, TOKEN_MUL, TOKEN_DIV,
    TOKEN_EQ, TOKEN_LT, TOKEN_GT,
    TOKEN_SEMI, TOKEN_COLON, TOKEN_COMMA, TOKEN_PERIOD,
    TOKEN_EOF
} TokenType;

typedef enum {
    TYPE_UNKNOWN,
    TYPE_INTEGER,
    TYPE_BOOLEAN
} DataType;

typedef struct {
    TokenType type;
    char text[MAX_NAME];
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
    OP_HALT
} Opcode;

typedef struct {
    Opcode op;
    int arg;
} Instruction;

typedef enum {
    NODE_COMPOUND,
    NODE_ASSIGN,
    NODE_BINARY_OP,
    NODE_NUMBER,
    NODE_BOOLEAN,
    NODE_VARIABLE
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
} ASTNode;

// Shared Global State
extern Instruction code[MAX_CODE];
extern int code_idx;
extern Symbol sym_table[MAX_SYMBOLS];
extern int sym_count;
extern Token token;

#endif

