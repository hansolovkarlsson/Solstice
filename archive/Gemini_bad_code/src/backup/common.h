#ifndef COMMON_H
#define COMMON_H

#include <stdarg.h>
#include <stdbool.h>

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
    TOKEN_AND, TOKEN_OR, TOKEN_NOT,
    TOKEN_LTE, TOKEN_GTE, TOKEN_NEQ,
    TOKEN_DIV_KW, TOKEN_MOD, TOKEN_XOR,
    TOKEN_SEMI, TOKEN_COLON, TOKEN_COMMA, TOKEN_PERIOD,
    TOKEN_LPAREN, TOKEN_RPAREN,
    TOKEN_STRING,
    TOKEN_WRITELN, TOKEN_READLN,
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
    OP_PRINT_STR,
    OP_PRINT, OP_READ,
    OP_PRINT_BOOL,
    OP_HALT
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
    NODE_STRING,
    NODE_WRITELN,
    NODE_READLN
} NodeType;


// Add to AST Node struct / Data payload:
// Store the index of the string in the string pool constant table
// Huh?

typedef struct ASTNode {
    NodeType type;
    TokenType op;
    DataType expression_type;
    int line;
    union {
        int num_value;
        bool bool_val;
        char name[64];
        int var_idx;
        int str_idx;
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

// String pool table, larger size gives LD error on Mac, over 32 KB alignment size
#define MAX_STRINGS 128
#define MAX_STR_LEN 256
// #define MAX_STRINGS 64
// #define MAX_STR_LEN 128

// extern char string_pool[MAX_STRINGS][MAX_STR_LEN];
extern char *string_pool[MAX_STRINGS]; // changed to dynamic pointer array
extern int string_pool_count;

int add_string_literal(const char *str);
void free_string_pool(void);
void compile_error(int line, const char *fmt, ...);

#endif

