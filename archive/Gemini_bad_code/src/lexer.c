#include "common.h"
#include <ctype.h>

static const char *src;
static int line_num = 1;

void lexer_init(const char *source_code) {
    src = source_code;
    line_num = 1;
}

void next_token(Token *token) {
    while (*src != '\0') {
        if (*src == ' ' || *src == '\t' || *src == '\r') {
            src++;
            continue;
        }
        if (*src == '\n') {
            line_num++;
            src++;
            continue;
        }

        token->line = line_num;

        // Pascal comments: { ... }
        if (*src == '{') {
            while (*src != '}' && *src != '\0') {
                if (*src == '\n') line_num++;
                src++;
            }
            if (*src == '}') src++;
            continue;
        }

        // Single-quoted String Literals
        if (*src == '\'') {
            src++; // Skip opening quote
            int i = 0;
            while (*src != '\'' && *src != '\0' && *src != '\n') {
                if (i < MAX_STR_LEN - 1) {
                    token->text[i++] = *src;
                }
                src++;
            }
            token->text[i] = '\0';

            if (*src == '\'') {
                src++; // Skip closing quote
            } else {
                compile_error(line_num, "Unterminated string literal");
            }
            token->type = TOKEN_STRING;
            return;
        }

        // Identifiers & Keywords
        if (isalpha(*src) || *src == '_') {
            int i = 0;
            while (isalnum(*src) || *src == '_') {
                if (i < MAX_STR_LEN - 1) token->text[i++] = *src;
                src++;
            }
            token->text[i] = '\0';

            if (strcasecmp(token->text, "program") == 0) token->type = TOKEN_PROGRAM;
            else if (strcasecmp(token->text, "var") == 0) token->type = TOKEN_VAR;
            else if (strcasecmp(token->text, "begin") == 0) token->type = TOKEN_BEGIN;
            else if (strcasecmp(token->text, "end") == 0) token->type = TOKEN_END;
            else if (strcasecmp(token->text, "integer") == 0) token->type = TOKEN_INTEGER_TYPE;
            else if (strcasecmp(token->text, "boolean") == 0) token->type = TOKEN_BOOLEAN_TYPE;
            else if (strcasecmp(token->text, "string") == 0) token->type = TOKEN_STRING_TYPE;
            else if (strcasecmp(token->text, "true") == 0) token->type = TOKEN_BOOL_LITERAL;
            else if (strcasecmp(token->text, "false") == 0) token->type = TOKEN_BOOL_LITERAL;
            else if (strcasecmp(token->text, "div") == 0) token->type = TOKEN_DIV;
            else if (strcasecmp(token->text, "mod") == 0) token->type = TOKEN_MOD;
            else if (strcasecmp(token->text, "and") == 0) token->type = TOKEN_AND;
            else if (strcasecmp(token->text, "or") == 0) token->type = TOKEN_OR;
            else if (strcasecmp(token->text, "xor") == 0) token->type = TOKEN_XOR;
            else if (strcasecmp(token->text, "not") == 0) token->type = TOKEN_NOT;
            else if (strcasecmp(token->text, "writeln") == 0) token->type = TOKEN_WRITELN;
            else if (strcasecmp(token->text, "readln") == 0) token->type = TOKEN_READLN;
            else token->type = TOKEN_IDENTIFIER;
            return;
        }

        // Integer Literals
        if (isdigit(*src)) {
            int i = 0;
            while (isdigit(*src)) {
                if (i < MAX_STR_LEN - 1) token->text[i++] = *src;
                src++;
            }
            token->text[i] = '\0';
            token->type = TOKEN_INT_LITERAL;
            return;
        }

        // Two-character operators
        if (*src == ':' && *(src + 1) == '=') {
            src += 2;
            strcpy(token->text, ":=");
            token->type = TOKEN_ASSIGN;
            return;
        }
        if (*src == '<' && *(src + 1) == '>') {
            src += 2;
            strcpy(token->text, "<>");
            token->type = TOKEN_NEQ;
            return;
        }
        if (*src == '<' && *(src + 1) == '=') {
            src += 2;
            strcpy(token->text, "<=");
            token->type = TOKEN_LTE;
            return;
        }
        if (*src == '>' && *(src + 1) == '=') {
            src += 2;
            strcpy(token->text, ">=");
            token->type = TOKEN_GTE;
            return;
        }

        // Single-character tokens
        char c = *src++;
        token->text[0] = c;
        token->text[1] = '\0';

        switch (c) {
            case '+': token->type = TOKEN_PLUS; return;
            case '-': token->type = TOKEN_MINUS; return;
            case '*': token->type = TOKEN_STAR; return;
            case '/': token->type = TOKEN_SLASH; return;
            case '=': token->type = TOKEN_EQ; return;
            case '<': token->type = TOKEN_LT; return;
            case '>': token->type = TOKEN_GT; return;
            case '(': token->type = TOKEN_LPAREN; return;
            case ')': token->type = TOKEN_RPAREN; return;
            case ':': token->type = TOKEN_COLON; return;
            case ';': token->type = TOKEN_SEMI; return;
            case ',': token->type = TOKEN_COMMA; return;
            case '.': token->type = TOKEN_EOF; return;
            default:
                compile_error(line_num, "Unknown character '%c'", c);
        }
    }

    token->type = TOKEN_EOF;
    token->text[0] = '\0';
}

