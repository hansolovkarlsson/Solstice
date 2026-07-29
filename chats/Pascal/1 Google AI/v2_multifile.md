step2_multifile.md

# Prompt

Break up into multiple source files

# Response

Here is the compiler refactored into a clean, modular multi-file structure.
## common.h
Defines shared tokens, instructions, and limits.

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
#endif

## lexer.h

#ifndef LEXER_H#define LEXER_H
#include "common.h"
extern Token token;
void init_lexer(const char *source);void next_token(void);
#endif

## lexer.c

#include <string.h>#include <ctype.h>#include "lexer.h"
static const char *src;Token token;
void init_lexer(const char *source) {
    src = source;
    next_token();
}
void next_token(void) {
    while (*src && isspace(*src)) src++;
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

## compiler.h

#ifndef COMPILER_H#define COMPILER_H
#include "common.h"
extern Instruction code[MAX_CODE];extern int code_idx;extern char sym_table[MAX_SYMBOLS][MAX_NAME];extern int sym_count;
void compile(const char *source);
#endif

## compiler.c

#include <stdio.h>#include <stdlib.h>#include <string.h>#include "compiler.h"#include "lexer.h"
Instruction code[MAX_CODE];int code_idx = 0;
char sym_table[MAX_SYMBOLS][MAX_NAME];int sym_count = 0;
static void emit(OpCode op, int arg) {
    code[code_idx++] = (Instruction){op, arg};
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
static void expression(void);
static void factor(void) {
    if (token.type == TOKEN_NUMBER) {
        emit(OP_PUSH, token.value);
        match(TOKEN_NUMBER);
    } else if (token.type == TOKEN_IDENTIFIER) {
        emit(OP_LOAD, find_var(token.text));
        match(TOKEN_IDENTIFIER);
    }
}
static void term(void) {
    factor();
    while (token.type == TOKEN_MUL || token.type == TOKEN_DIV) {
        TokenType t = token.type;
        next_token();
        factor();
        emit(t == TOKEN_MUL ? OP_MUL : OP_DIV, 0);
    }
}
static void expression(void) {
    term();
    while (token.type == TOKEN_PLUS || token.type == TOKEN_MINUS) {
        TokenType t = token.type;
        next_token();
        term();
        emit(t == TOKEN_PLUS ? OP_ADD : OP_SUB, 0);
    }
}
void compile(const char *source) {
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
    while (token.type == TOKEN_IDENTIFIER) {
        int var_idx = find_var(token.text);
        match(TOKEN_IDENTIFIER);
        match(TOKEN_ASSIGN);
        expression();
        emit(OP_STORE, var_idx);
        match(TOKEN_SEMI);
    }
    match(TOKEN_END);
    match(TOKEN_PERIOD);
    emit(OP_HALT, 0);
}

## vm.h

#ifndef VM_H#define VM_H
extern int vm_vars[MAX_SYMBOLS];
void run_vm(void);
#endif

## vm.c

#include "vm.h"#include "compiler.h"
int vm_vars[MAX_SYMBOLS];static int vm_stack[MAX_STACK];
void run_vm(void) {
    int sp = -1;
    int ip = 0;

    while (1) {
        Instruction instr = code[ip++];
        switch (instr.op) {
            case OP_PUSH:  vm_stack[++sp] = instr.arg; break;
            case OP_LOAD:  vm_stack[++sp] = vm_vars[instr.arg]; break;
            case OP_STORE: vm_vars[instr.arg] = vm_stack[sp--]; break;
            case OP_ADD:   vm_stack[sp - 1] += vm_stack[sp]; sp--; break;
            case OP_SUB:   vm_stack[sp - 1] -= vm_stack[sp]; sp--; break;
            case OP_MUL:   vm_stack[sp - 1] *= vm_stack[sp]; sp--; break;
            case OP_DIV:   vm_stack[sp - 1] /= vm_stack[sp]; sp--; break;
            case OP_HALT:  return;
        }
    }
}

## main.c

#include <stdio.h>#include "compiler.h"#include "vm.h"
int main() {
    const char *pascal_program = 
        "program MathTest;\n"
        "var x, y, result : integer;\n"
        "begin\n"
        "  x := 15;\n"
        "  y := 5;\n"
        "  result := x * 2 + y - 3;\n"
        "end.";

    printf("Compiling Pascal Program...\n");
    compile(pascal_program);
    printf("Compilation successful. Generated %d instructions.\n", code_idx);

    printf("\nRunning Virtual Machine...\n");
    run_vm();

    printf("\nVM Execution Finished. Final State:\n");
    for (int i = 0; i < sym_count; i++) {
        printf("%s = %d\n", sym_table[i], vm_vars[i]);
    }

    return 0;
}

## How to Compile
You can compile all files directly using GCC:

gcc main.c lexer.c compiler.c vm.c -o pascal_compiler

If you are interested, I can:

* Provide a Makefile to automate builds
* Add support for reading source code from a file instead of a hardcoded string


