#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include "lexer.h"

static const char *src;
Token token;
static int current_line = 1;

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
        while (isalnum(*src) || *src == '_') *p++ = *src++;
        *p = '\0';

        if (strcasecmp(token.text, "program") == 0) token.type = TOKEN_PROGRAM;
        else if (strcasecmp(token.text, "var") == 0) token.type = TOKEN_VAR;
        else if (strcasecmp(token.text, "begin") == 0) token.type = TOKEN_BEGIN;
        else if (strcasecmp(token.text, "end") == 0) token.type = TOKEN_END;
        else if (strcasecmp(token.text, "integer") == 0) token.type = TOKEN_INTEGER;
        else if (strcasecmp(token.text, "boolean") == 0) token.type = TOKEN_BOOLEAN;
        else if (strcasecmp(token.text, "true") == 0) { token.type = TOKEN_TRUE; token.value = 1; }
        else if (strcasecmp(token.text, "false") == 0) { token.type = TOKEN_FALSE; token.value = 0; }
        else token.type = TOKEN_IDENTIFIER;
        return;
    }

    if (isdigit(*src)) {
        token.type = TOKEN_NUMBER;
        token.value = 0;
        char *p = token.text;
        while (isdigit(*src)) {
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
        case '<': token.type = TOKEN_LT; break;
        case '>': token.type = TOKEN_GT; break;
        case ';': token.type = TOKEN_SEMI; break;
        case ':': token.type = TOKEN_COLON; break;
        case ',': token.type = TOKEN_COMMA; break;
        case '.': token.type = TOKEN_PERIOD; break;
        default:  token.type = TOKEN_EOF; break;
    }
}

