#include <string.h>
#include <ctype.h>
#include "lexer.h"

static const char *src;
Token token;
int current_line = 1;

void init_lexer(const char *source) {
    src = source;
    current_line = 1;
    next_token();
}

void next_token(void) {
    while (1) {
        // 1. Skip standard whitespace and increment line numbers
        while (*src && isspace(*src)) {
            if (*src == '\n') {
                current_line++;
            }
            src++;
        }

        // 2. Skip Pascal standard bracket comments { ... }
        if (*src == '{') {
            src++; // Skip the opening '{'
            while (*src && *src != '}') {
                if (*src == '\n') {
                    current_line++;
                }
                src++; // Consume all comment content characters
            }
            if (*src == '}') {
                src++; // Skip the closing '}'
                continue; // Loop back up to catch any whitespace or sequential comments
            }
        } else {
            break;
        }
    }

    // Attach current line metadata to every generated token
    token.line = current_line;

    if (!*src) { token.type = TOKEN_EOF; return; }

    // Allow letters or underscores at the start of identifiers
    if (isalpha(*src) || *src == '_') {
        char *p = token.text;
        // Allow letters, digits, or underscores inside identifiers
        while (isalnum(*src) || *src == '_') *p++ = *src++;
        *p = '\0';

        // Keywords check
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
    if (*src == '=') { token.type = TOKEN_EQ; src++; return; }
    if (*src == '<') { token.type = TOKEN_LT; src++; return; }
    if (*src == '>') { token.type = TOKEN_GT; src++; return; }


    token.type = TOKEN_UNKNOWN;
    src++;
}

