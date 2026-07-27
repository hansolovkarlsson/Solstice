#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include "lexer.h"
#include "parser.h"
#include "error.h"

static const char *src;
Token token;
static int current_line = 1;

static void lexer_error(const char *fmt, ...) {
    fprintf(stderr, "%s:%d: Compile Error: ", get_current_filename(), current_line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    fatal_abort();
}

void init_lexer(const char *source) {
    src = source;
    current_line = 1;
    next_token();
}

void next_token(void) {
    while (*src && isspace(*src)) {
        if (*src == '\n') current_line++;
        src++;
    }

    token.line = current_line;

    if (*src == '{') {
        while (*src && *src != '}') {
            if (*src == '\n') current_line++;
            src++;
        }
        if (*src == '}') src++;
        next_token();
        return;
    }

    if (!*src) {
        token.type = TOKEN_EOF;
        token.text[0] = '\0';
        return;
    }

    if (isalpha(*src) || *src == '_') {
        char *p = token.text;
        char *end = token.text + MAX_NAME - 1;
        while (isalnum(*src) || *src == '_') {
            if (p >= end) lexer_error("Identifier too long (limit is %d characters)", MAX_NAME - 1);
            *p++ = *src++;
        }
        *p = '\0';

        if (strcasecmp(token.text, "program") == 0) token.type = TOKEN_PROGRAM;
        else if (strcasecmp(token.text, "var") == 0) token.type = TOKEN_VAR;
        else if (strcasecmp(token.text, "begin") == 0) token.type = TOKEN_BEGIN;
        else if (strcasecmp(token.text, "end") == 0) token.type = TOKEN_END;
        else if (strcasecmp(token.text, "integer") == 0) token.type = TOKEN_INTEGER;
        else if (strcasecmp(token.text, "boolean") == 0) token.type = TOKEN_BOOLEAN;
        else if (strcasecmp(token.text, "true") == 0) { token.type = TOKEN_TRUE; token.value = 1; }
        else if (strcasecmp(token.text, "false") == 0) { token.type = TOKEN_FALSE; token.value = 0; }
        else if (strcasecmp(token.text, "writeln") == 0) token.type = TOKEN_WRITELN;
        else if (strcasecmp(token.text, "readln") == 0) token.type = TOKEN_READLN;
        else if (strcasecmp(token.text, "and") == 0) token.type = TOKEN_AND;
        else if (strcasecmp(token.text, "or") == 0) token.type = TOKEN_OR;
        else if (strcasecmp(token.text, "not") == 0) token.type = TOKEN_NOT;
        else if (strcasecmp(token.text, "div") == 0) token.type = TOKEN_DIV_KW;
        else if (strcasecmp(token.text, "mod") == 0) token.type = TOKEN_MOD;
        else if (strcasecmp(token.text, "xor") == 0) token.type = TOKEN_XOR;
        else if (strcasecmp(token.text, "if") == 0) token.type = TOKEN_IF;
        else if (strcasecmp(token.text, "then") == 0) token.type = TOKEN_THEN;
        else if (strcasecmp(token.text, "else") == 0) token.type = TOKEN_ELSE;
        else if (strcasecmp(token.text, "while") == 0) token.type = TOKEN_WHILE;
        else if (strcasecmp(token.text, "do") == 0) token.type = TOKEN_DO;
        else if (strcasecmp(token.text, "repeat") == 0) token.type = TOKEN_REPEAT;
        else if (strcasecmp(token.text, "until") == 0) token.type = TOKEN_UNTIL;
        else token.type = TOKEN_IDENTIFIER;
        return;
    }

    if (isdigit(*src)) {
        token.type = TOKEN_NUMBER;
        token.value = 0;
        char *p = token.text;
        char *end = token.text + MAX_NAME - 1;
        while (isdigit(*src)) {
            if (p >= end) lexer_error("Numeric literal too long (limit is %d digits)", MAX_NAME - 1);
            *p++ = *src;
            token.value = token.value * 10 + (*src - '0');
            src++;
        }
        *p = '\0';
        return;
    }

    if (*src == ':' && *(src + 1) == '=') {
        token.type = TOKEN_ASSIGN;
        strcpy(token.text, ":=");
        src += 2;
        return;
    }

    token.text[0] = *src;
    token.text[1] = '\0';
    switch (*src++) {
        case '+': token.type = TOKEN_PLUS; break;
        case '-': token.type = TOKEN_MINUS; break;
        case '*': token.type = TOKEN_MUL; break;
        case '/': token.type = TOKEN_DIV; break;
        case '=': token.type = TOKEN_EQ; break;
        case '<': 
            if (*src == '=') { token.type = TOKEN_LTE; strcpy(token.text, "<="); src++; }
            else if (*src == '>') { token.type = TOKEN_NEQ; strcpy(token.text, "<>"); src++; }
            else { token.type = TOKEN_LT; }
            break;
        case '>': 
            if (*src == '=') { token.type = TOKEN_GTE; strcpy(token.text, ">="); src++; }
            else { token.type = TOKEN_GT; }
            break;
        case ';': token.type = TOKEN_SEMI; break;
        case ':': token.type = TOKEN_COLON; break;
        case ',': token.type = TOKEN_COMMA; break;
        case '.': token.type = TOKEN_PERIOD; break;
        case '(': token.type = TOKEN_LPAREN; break; // <--- Add '('
        case ')': token.type = TOKEN_RPAREN; break; // <--- Add ')'
        default:  token.type = TOKEN_EOF; break;
    }

}

