#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ============================================================================
// 1. LEXER / TOKENIZER
// ============================================================================
typedef enum {
    TOKEN_PROGRAM, TOKEN_VAR, TOKEN_BEGIN, TOKEN_END, TOKEN_INTEGER,
    TOKEN_IDENTIFIER, TOKEN_NUMBER, TOKEN_ASSIGN, TOKEN_PLUS, TOKEN_MINUS,
    TOKEN_MUL, TOKEN_DIV, TOKEN_SEMI, TOKEN_COLON, TOKEN_COMMA, TOKEN_PERIOD,
    TOKEN_EOF, TOKEN_UNKNOWN
} TokenType;

typedef struct {
    TokenType type;
    char text[64];
    int value;
} Token;

const char *src;
Token token;

void next_token() {
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

// ============================================================================
// 2. COMPILER / PARSER & SYMBOL TABLE
// ============================================================================
typedef enum { OP_PUSH, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_LOAD, OP_STORE, OP_HALT } OpCode;

typedef struct {
    OpCode op;
    int arg;
} Instruction;

Instruction code[1024];
int code_idx = 0;

char sym_table[100][64];
int sym_count = 0;

void emit(OpCode op, int arg) {
    code[code_idx++] = (Instruction){op, arg};
}

int find_var(const char *name) {
    for (int i = 0; i < sym_count; i++) {
        if (strcmp(sym_table[i], name) == 0) return i;
    }
    printf("Compile Error: Unknown variable %s\n", name);
    exit(1);
}

void add_var(const char *name) {
    strcpy(sym_table[sym_count++], name);
}

void match(TokenType type) {
    if (token.type == type) next_token();
    else { printf("Compile Error: Unexpected token syntax\n"); exit(1); }
}

void expression();

void factor() {
    if (token.type == TOKEN_NUMBER) {
        emit(OP_PUSH, token.value);
        match(TOKEN_NUMBER);
    } else if (token.type == TOKEN_IDENTIFIER) {
        emit(OP_LOAD, find_var(token.text));
        match(TOKEN_IDENTIFIER);
    }
}

void term() {
    factor();
    while (token.type == TOKEN_MUL || token.type == TOKEN_DIV) {
        TokenType t = token.type;
        next_token();
        factor();
        emit(t == TOKEN_MUL ? OP_MUL : OP_DIV, 0);
    }
}

void expression() {
    term();
    while (token.type == TOKEN_PLUS || token.type == TOKEN_MINUS) {
        TokenType t = token.type;
        next_token();
        term();
        emit(t == TOKEN_PLUS ? OP_ADD : OP_SUB, 0);
    }
}

void compile(const char *source) {
    src = source;
    next_token();

    // program Header
    match(TOKEN_PROGRAM);
    match(TOKEN_IDENTIFIER);
    match(TOKEN_SEMI);

    // Variable Declarations
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

    // Statements Block
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

// ============================================================================
// 3. STACK-BASED VIRTUAL MACHINE
// ============================================================================
int vm_stack[256];
int vm_vars[100];

void run_vm() {
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

// ============================================================================
// 4. MAIN EXECUTION ENTRY
// ============================================================================
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

