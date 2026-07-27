#include <string.h>
#include <ctype.h>
#include "lexer.h"

static const char *src;
Token token;

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

    // Inside next_token() where keywords are verified via strcasecmp:
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
