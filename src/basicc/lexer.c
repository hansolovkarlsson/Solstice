#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "basic.h"

BasicToken btoken;

static const char *src;
static int current_line = 1;

static void lexer_error(const char *msg) {
    fprintf(stderr, "%s:%d: Compile Error: %s\n", basic_get_current_filename(), current_line, msg);
    fatal_abort();
}

void basic_init_lexer(const char *source) {
    src = source;
    current_line = 1;
}

typedef struct { const char *word; BasicTokenType tok; } Keyword;
static const Keyword keywords[] = {
    {"LET", BTOK_LET}, {"PRINT", BTOK_PRINT}, {"INPUT", BTOK_INPUT},
    {"IF", BTOK_IF}, {"THEN", BTOK_THEN}, {"ELSE", BTOK_ELSE},
    {"GOTO", BTOK_GOTO}, {"GOSUB", BTOK_GOSUB}, {"RETURN", BTOK_RETURN},
    {"FOR", BTOK_FOR}, {"TO", BTOK_TO}, {"STEP", BTOK_STEP}, {"NEXT", BTOK_NEXT},
    {"END", BTOK_END}, {"AND", BTOK_AND}, {"OR", BTOK_OR}, {"NOT", BTOK_NOT},
};
#define NUM_KEYWORDS (int)(sizeof(keywords) / sizeof(keywords[0]))

void basic_next_token(void) {
    for (;;) {
        // Spaces/tabs are insignificant; newlines are NOT (they end a
        // statement list, unlike Pascal's whitespace-insensitive lexer -
        // see basic.h's own comment on BasicASTNode.line).
        while (*src == ' ' || *src == '\t' || *src == '\r') src++;

        if (*src == '\0') {
            btoken.type = BTOK_EOF;
            btoken.line = current_line;
            return;
        }

        if (*src == '\n') {
            src++;
            btoken.type = BTOK_EOL;
            btoken.line = current_line;
            current_line++;
            return;
        }

        // REM comment / trailing "'" comment: skip to (not past) the
        // newline, then loop back around so the '\n' case above fires
        // and emits the EOL token normally.
        if (*src == '\'') {
            while (*src != '\n' && *src != '\0') src++;
            continue;
        }
        if ((src[0] == 'R' || src[0] == 'r') && (src[1] == 'E' || src[1] == 'e')
            && (src[2] == 'M' || src[2] == 'm')
            && !isalnum((unsigned char)src[3]) && src[3] != '$' && src[3] != '%') {
            while (*src != '\n' && *src != '\0') src++;
            continue;
        }

        break;
    }

    btoken.line = current_line;

    // String literal - no escape sequences in v1.
    if (*src == '"') {
        src++;
        int i = 0;
        while (*src != '"') {
            if (*src == '\0' || *src == '\n') lexer_error("Unterminated string literal");
            if (i >= MAX_STRING_LEN - 1) lexer_error("String literal too long");
            btoken.string_value[i++] = *src++;
        }
        btoken.string_value[i] = '\0';
        src++; // closing quote
        btoken.type = BTOK_STRING;
        return;
    }

    // Number literal - digits, optional '.' digits. No exponent notation
    // in v1 (a deliberate, easy-to-lift-later scope cut).
    if (isdigit((unsigned char)*src)) {
        char buf[64];
        int i = 0;
        int is_real = 0;
        while (isdigit((unsigned char)*src)) {
            if (i < (int)sizeof(buf) - 1) buf[i++] = *src;
            src++;
        }
        if (*src == '.' && isdigit((unsigned char)src[1])) {
            is_real = 1;
            if (i < (int)sizeof(buf) - 1) buf[i++] = *src;
            src++;
            while (isdigit((unsigned char)*src)) {
                if (i < (int)sizeof(buf) - 1) buf[i++] = *src;
                src++;
            }
        }
        buf[i] = '\0';
        if (is_real) {
            btoken.type = BTOK_REAL;
            btoken.real_value = (float)atof(buf);
        } else {
            btoken.type = BTOK_NUMBER;
            btoken.value = atoi(buf);
        }
        return;
    }

    // Identifier or keyword. Sigils ('$'/'%') are part of a VARIABLE
    // name, never a keyword's, so they're only consumed once the
    // scanned word fails to match a keyword.
    if (isalpha((unsigned char)*src)) {
        char buf[MAX_NAME];
        int i = 0;
        while (isalnum((unsigned char)*src)) {
            if (i >= MAX_NAME - 2) lexer_error("Identifier too long");
            buf[i++] = (char)toupper((unsigned char)*src);
            src++;
        }
        buf[i] = '\0';
        for (int k = 0; k < NUM_KEYWORDS; k++) {
            if (strcmp(buf, keywords[k].word) == 0) {
                btoken.type = keywords[k].tok;
                strcpy(btoken.text, buf);
                return;
            }
        }
        if (*src == '$' || *src == '%') {
            buf[i++] = *src++;
            buf[i] = '\0';
        }
        btoken.type = BTOK_IDENT;
        strcpy(btoken.text, buf);
        return;
    }

    switch (*src) {
        case '=': src++; btoken.type = BTOK_EQ; return;
        case '+': src++; btoken.type = BTOK_PLUS; return;
        case '-': src++; btoken.type = BTOK_MINUS; return;
        case '*': src++; btoken.type = BTOK_MUL; return;
        case '/': src++; btoken.type = BTOK_SLASH; return;
        case '(': src++; btoken.type = BTOK_LPAREN; return;
        case ')': src++; btoken.type = BTOK_RPAREN; return;
        case ',': src++; btoken.type = BTOK_COMMA; return;
        case ';': src++; btoken.type = BTOK_SEMI; return;
        case ':': src++; btoken.type = BTOK_COLON; return;
        case '<':
            src++;
            if (*src == '=') { src++; btoken.type = BTOK_LTE; return; }
            if (*src == '>') { src++; btoken.type = BTOK_NEQ; return; }
            btoken.type = BTOK_LT;
            return;
        case '>':
            src++;
            if (*src == '=') { src++; btoken.type = BTOK_GTE; return; }
            btoken.type = BTOK_GT;
            return;
        default: {
            char msg[64];
            snprintf(msg, sizeof(msg), "Unexpected character '%c'", *src);
            lexer_error(msg);
        }
    }
}
