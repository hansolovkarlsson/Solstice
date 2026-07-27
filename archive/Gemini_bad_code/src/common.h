#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

#define MAX_STRINGS 128
#define MAX_STR_LEN 256
#define MAX_VARS 128
#define MAX_NAME_LEN 64

// --- Token Types ---
typedef enum {
    TOKEN_EOF,
    TOKEN_PROGRAM, TOKEN_VAR, TOKEN_BEGIN, TOKEN_END,
    TOKEN_INTEGER_TYPE, TOKEN_BOOLEAN_TYPE, TOKEN_STRING_TYPE,
    TOKEN_IDENTIFIER, TOKEN_INT_LITERAL, TOKEN_BOOL_LITERAL, TOKEN_STRING,
    TOKEN_ASSIGN, TOKEN_PLUS, TOKEN_MINUS, TOKEN_STAR, TOKEN_SLASH,
    TOKEN_DIV, TOKEN_MOD,
    TOKEN_AND, TOKEN_OR, TOKEN_XOR, TOKEN_NOT,
    TOKEN_EQ, TOKEN_NEQ, TOKEN_LT, TOKEN_LTE, TOKEN_GT, TOKEN_GTE,
    TOKEN_LPAREN, TOKEN_RPAREN, TOKEN_COLON, TOKEN_SEMI, TOKEN_COMMA,
    TOKEN_WRITELN, TOKEN_READLN
} TokenType;

typedef struct {
    TokenType type;
    char text[MAX_STR_LEN];
    int line;
} Token;

// --- Data Types ---
typedef enum {
    TYPE_UNKNOWN,
    TYPE_INTEGER,
    TYPE_BOOLEAN,
    TYPE_STRING
} DataType;

// --- AST Node Types ---
typedef enum {
    NODE_PROGRAM,
    NODE_VAR_DECL,
    NODE_BLOCK,
    NODE_ASSIGN,
    NODE_BINOP,
    NODE_UNOP,
    NODE_INT,
    NODE_BOOL,
    NODE_STRING,
    NODE_VAR,
    NODE_WRITELN,
    NODE_READLN
} NodeType;

typedef struct ASTNode {
    NodeType type;
    DataType expression_type;
    union {
        int int_val;
        bool bool_val;
        char name[MAX_NAME_LEN];
        int str_idx;
        TokenType op;
    } data;
    struct ASTNode *left;
    struct ASTNode *right;
    struct ASTNode *next;
} ASTNode;

// --- Virtual Machine Opcodes ---
typedef enum {
    OP_NOP,
    OP_PUSH_INT, OP_PUSH_BOOL,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_AND, OP_OR, OP_XOR, OP_NOT,
    OP_EQ, OP_NEQ, OP_LT, OP_LTE, OP_GT, OP_GTE,
    OP_LOAD, OP_STORE,
    OP_WRITE_INT, OP_WRITE_BOOL, OP_WRITE_STR, // New inline write opcodes
    OP_PRINT_NEWLINE,                           // Appends \n at the end
    OP_READ,
    OP_HALT
} Opcode;

typedef struct {
    Opcode op;
    int arg;
} Instruction;

// --- Symbol Table Structure ---
typedef struct {
    char name[MAX_NAME_LEN];
    DataType type;
    int index;
} Symbol;

extern Symbol symbol_table[MAX_VARS];
extern int symbol_count;

// --- String Pool Management ---
extern char *string_pool[MAX_STRINGS];
extern int string_pool_count;

// --- Function Prototypes ---
void compile_error(int line, const char *fmt, ...);
int add_string_literal(const char *str);
void free_string_pool(void);
int lookup_symbol(const char *name);
int add_symbol(const char *name, DataType type);

void lexer_init(const char *source_code);
void next_token(Token *token);

ASTNode *parse_ast(void);
void print_ast(ASTNode *node, int indent);
void check_types(ASTNode *node);

void generate_code(ASTNode *node);
int get_generated_code(Instruction **out_instructions);

void execute_vm(Instruction *instructions, int count);

void save_bytecode(const char *filename);
int load_bytecode(const char *filename, Instruction **out_instructions);

ASTNode *optimize_ast(ASTNode *node);

#endif

