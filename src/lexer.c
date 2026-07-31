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

    if (*src == '/' && *(src + 1) == '/') {
        while (*src && *src != '\n') src++;
        next_token();
        return;
    }

    if (!*src) {
        token.type = TOKEN_EOF;
        token.text[0] = '\0';
        return;
    }

    if (*src == '\'') {
        src++; // consume opening quote
        char *p = token.string_value;
        char *end = token.string_value + MAX_STRING_LEN - 1;
        while (1) {
            if (*src == '\'' && *(src + 1) == '\'') {
                // '' inside a string literal is an escaped literal quote
                if (p >= end) lexer_error("String literal too long (limit is %d characters)", MAX_STRING_LEN - 1);
                *p++ = '\'';
                src += 2;
            } else if (*src == '\'') {
                src++; // consume closing quote
                break;
            } else if (*src == '\0' || *src == '\n') {
                lexer_error("Unterminated string literal");
            } else {
                if (p >= end) lexer_error("String literal too long (limit is %d characters)", MAX_STRING_LEN - 1);
                *p++ = *src++;
            }
        }
        *p = '\0';
        token.type = TOKEN_STRING;
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
        else if (strcasecmp(token.text, "write") == 0) token.type = TOKEN_WRITE;
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
        else if (strcasecmp(token.text, "string") == 0) token.type = TOKEN_STRING_TYPE;
        else if (strcasecmp(token.text, "for") == 0) token.type = TOKEN_FOR;
        else if (strcasecmp(token.text, "to") == 0) token.type = TOKEN_TO;
        else if (strcasecmp(token.text, "downto") == 0) token.type = TOKEN_DOWNTO;
        else if (strcasecmp(token.text, "array") == 0) token.type = TOKEN_ARRAY;
        else if (strcasecmp(token.text, "of") == 0) token.type = TOKEN_OF;
        else if (strcasecmp(token.text, "break") == 0) token.type = TOKEN_BREAK;
        else if (strcasecmp(token.text, "continue") == 0) token.type = TOKEN_CONTINUE;
        else if (strcasecmp(token.text, "char") == 0) token.type = TOKEN_CHAR_TYPE;
        else if (strcasecmp(token.text, "real") == 0) token.type = TOKEN_REAL_TYPE;
        else if (strcasecmp(token.text, "trunc") == 0) token.type = TOKEN_TRUNC;
        else if (strcasecmp(token.text, "round") == 0) token.type = TOKEN_ROUND;
        else if (strcasecmp(token.text, "type") == 0) token.type = TOKEN_TYPE;
        else if (strcasecmp(token.text, "record") == 0) token.type = TOKEN_RECORD;
        else if (strcasecmp(token.text, "const") == 0) token.type = TOKEN_CONST;
        else if (strcasecmp(token.text, "with") == 0) token.type = TOKEN_WITH;
        else if (strcasecmp(token.text, "sqrt") == 0) token.type = TOKEN_SQRT;
        else if (strcasecmp(token.text, "sin") == 0) token.type = TOKEN_SIN;
        else if (strcasecmp(token.text, "cos") == 0) token.type = TOKEN_COS;
        else if (strcasecmp(token.text, "arctan") == 0) token.type = TOKEN_ARCTAN;
        else if (strcasecmp(token.text, "exp") == 0) token.type = TOKEN_EXP;
        else if (strcasecmp(token.text, "ln") == 0) token.type = TOKEN_LN;
        else if (strcasecmp(token.text, "pi") == 0) token.type = TOKEN_PI;
        else if (strcasecmp(token.text, "power") == 0) token.type = TOKEN_POWER;
        else if (strcasecmp(token.text, "procedure") == 0) token.type = TOKEN_PROCEDURE;
        else if (strcasecmp(token.text, "forward") == 0) token.type = TOKEN_FORWARD;
        else if (strcasecmp(token.text, "function") == 0) token.type = TOKEN_FUNCTION;
        else if (strcasecmp(token.text, "shl") == 0) token.type = TOKEN_SHL;
        else if (strcasecmp(token.text, "shr") == 0) token.type = TOKEN_SHR;
        else if (strcasecmp(token.text, "inc") == 0) token.type = TOKEN_INC;
        else if (strcasecmp(token.text, "dec") == 0) token.type = TOKEN_DEC;
        else if (strcasecmp(token.text, "abs") == 0) token.type = TOKEN_ABS;
        else if (strcasecmp(token.text, "sqr") == 0) token.type = TOKEN_SQR;
        else if (strcasecmp(token.text, "odd") == 0) token.type = TOKEN_ODD;
        else if (strcasecmp(token.text, "succ") == 0) token.type = TOKEN_SUCC;
        else if (strcasecmp(token.text, "pred") == 0) token.type = TOKEN_PRED;
        else if (strcasecmp(token.text, "ord") == 0) token.type = TOKEN_ORD;
        else if (strcasecmp(token.text, "chr") == 0) token.type = TOKEN_CHR;
        else if (strcasecmp(token.text, "length") == 0) token.type = TOKEN_LENGTH;
        else if (strcasecmp(token.text, "low") == 0) token.type = TOKEN_LOW;
        else if (strcasecmp(token.text, "high") == 0) token.type = TOKEN_HIGH;
        else if (strcasecmp(token.text, "copy") == 0) token.type = TOKEN_COPY;
        else if (strcasecmp(token.text, "pos") == 0) token.type = TOKEN_POS;
        else if (strcasecmp(token.text, "upcase") == 0) token.type = TOKEN_UPCASE;
        else if (strcasecmp(token.text, "uppercase") == 0) token.type = TOKEN_UPPERCASE;
        else if (strcasecmp(token.text, "lowercase") == 0) token.type = TOKEN_LOWERCASE;
        else if (strcasecmp(token.text, "mid") == 0) token.type = TOKEN_MID;
        else if (strcasecmp(token.text, "left") == 0) token.type = TOKEN_LEFT;
        else if (strcasecmp(token.text, "right") == 0) token.type = TOKEN_RIGHT;
        else if (strcasecmp(token.text, "inpos") == 0) token.type = TOKEN_INPOS;
        else token.type = TOKEN_IDENTIFIER;
        return;
    }

    if (*src == '#') {
        src++;
        if (!isdigit(*src)) {
            lexer_error("Expected a digit after '#' (a char-code literal, e.g. #13)");
        }
        int val = 0;
        char *p = token.text;
        *p++ = '#';
        while (isdigit(*src)) {
            val = val * 10 + (*src - '0');
            if (p < token.text + MAX_NAME - 1) *p++ = *src;
            src++;
        }
        *p = '\0';
        token.type = TOKEN_CHARCODE;
        token.value = val;
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

        // A '.' followed by a digit makes this a real literal instead -
        // '..' (the range operator, e.g. array[1..10]) and a bare '.'
        // (the period ending the program) must NOT be consumed here.
        if (*src == '.' && isdigit(*(src + 1))) {
            if (p >= end) lexer_error("Numeric literal too long (limit is %d characters)", MAX_NAME - 1);
            *p++ = *src++; // the '.'
            while (isdigit(*src)) {
                if (p >= end) lexer_error("Numeric literal too long (limit is %d characters)", MAX_NAME - 1);
                *p++ = *src++;
            }
            // Optional exponent: e/E, optional sign, one or more digits.
            if (*src == 'e' || *src == 'E') {
                const char *save_src = src;
                char *save_p = p;
                *p++ = *src++;
                if (*src == '+' || *src == '-') {
                    *p++ = *src++;
                }
                if (isdigit(*src)) {
                    while (isdigit(*src)) {
                        if (p >= end) lexer_error("Numeric literal too long (limit is %d characters)", MAX_NAME - 1);
                        *p++ = *src++;
                    }
                } else {
                    src = save_src; // not actually a valid exponent - back off
                    p = save_p;
                }
            }
            *p = '\0';
            token.type = TOKEN_REAL;
            token.real_value = strtof(token.text, NULL);
        }
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
        case '*':
            if (*src == '*') { token.type = TOKEN_POW; strcpy(token.text, "**"); src++; }
            else { token.type = TOKEN_MUL; }
            break;
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
        case '.':
            if (*src == '.') { token.type = TOKEN_DOTDOT; strcpy(token.text, ".."); src++; }
            else { token.type = TOKEN_PERIOD; }
            break;
        case '(': token.type = TOKEN_LPAREN; break; // <--- Add '('
        case ')': token.type = TOKEN_RPAREN; break; // <--- Add ')'
        case '[': token.type = TOKEN_LBRACKET; break;
        case ']': token.type = TOKEN_RBRACKET; break;
        default:  token.type = TOKEN_EOF; break;
    }

}

