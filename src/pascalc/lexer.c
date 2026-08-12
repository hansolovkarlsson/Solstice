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

// Symbols defined via {$DEFINE}/{$UNDEF}, checked by {$IFDEF}/
// {$IFNDEF}. NOT reset by init_lexer() - see lexer_reset_defines()'s
// own doc comment in lexer.h for why.
#define MAX_DEFINED_SYMBOLS 64
static char defined_symbols[MAX_DEFINED_SYMBOLS][MAX_NAME];
static int defined_symbol_count = 0;

// {$IFDEF}/{$IFNDEF} nesting - one entry per currently-open, currently-
// ACTIVE conditional. A conditional whose branch is being skipped is
// fully consumed by skip_conditional() below before control ever
// returns to next_token(), so it never needs its own entry here - only
// branches actually being lexed do. ifdef_in_else[i] is set once that
// level's {$ELSE} has been taken, to catch a duplicate {$ELSE}. Reset
// inside init_lexer() itself (unlike defined_symbols[] above) since
// nesting is per-file - see lexer.h's LexerPos doc comment.
static int ifdef_in_else[MAX_IFDEF_DEPTH];
static int ifdef_depth = 0;

static void lexer_error(const char *fmt, ...) {
    fprintf(stderr, "%s:%d: Compile Error: ", get_current_filename(), current_line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    fatal_abort();
}

void lexer_reset_defines(void) {
    defined_symbol_count = 0;
}

static int is_symbol_defined(const char *name) {
    for (int i = 0; i < defined_symbol_count; i++) {
        if (strcasecmp(defined_symbols[i], name) == 0) return 1;
    }
    return 0;
}

static void define_symbol(const char *name) {
    if (is_symbol_defined(name)) return; // idempotent, matches real Pascal
    if (defined_symbol_count >= MAX_DEFINED_SYMBOLS) {
        lexer_error("Too many '$DEFINE'd symbols in this compile (limit is %d)", MAX_DEFINED_SYMBOLS);
    }
    strcpy(defined_symbols[defined_symbol_count++], name);
}

static void undef_symbol(const char *name) {
    for (int i = 0; i < defined_symbol_count; i++) {
        if (strcasecmp(defined_symbols[i], name) == 0) {
            for (int j = i; j < defined_symbol_count - 1; j++) {
                strcpy(defined_symbols[j], defined_symbols[j + 1]);
            }
            defined_symbol_count--;
            return;
        }
    }
}

// Skips spaces/tabs/newlines between a directive keyword and its
// argument (e.g. the space in '{$IFDEF FOO}'), bumping current_line for
// any embedded newline, same as every other whitespace/comment skip in
// this file.
static void skip_directive_ws(void) {
    while (*src == ' ' || *src == '\t' || *src == '\n' || *src == '\r') {
        if (*src == '\n') current_line++;
        src++;
    }
}

// Reads a directive keyword or {$DEFINE}/{$IFDEF}-style symbol name
// into 'out' (bounded exactly like the ordinary identifier scanner
// below). Leaves out[0] == '\0' if the next character isn't
// identifier-like, for the caller's own "expected a symbol name" check.
static void read_directive_word(char *out, size_t outsz) {
    char *p = out;
    char *end = out + outsz - 1;
    while (isalnum((unsigned char)*src) || *src == '_') {
        if (p >= end) lexer_error("Directive name too long (limit is %d characters)", (int)outsz - 1);
        *p++ = *src++;
    }
    *p = '\0';
}

// The ordinary '{ ... }' comment scan, factored out since it's also
// needed after every recognized directive's argument and inside
// skip_conditional() below.
static void skip_to_closing_brace(void) {
    while (*src && *src != '}') {
        if (*src == '\n') current_line++;
        src++;
    }
    if (*src == '}') src++;
}

// Scans raw source for the next literal '{$' marker (deliberately not
// string/comment-aware - the same simplicity this lexer already accepts
// for plain '{ }' comments, which don't understand strings inside them
// either), tracking a LOCAL nesting counter for any {$IFDEF}/{$IFNDEF}
// it passes over so it finds the marker matching its own level, not a
// nested one. Any {$DEFINE}/{$UNDEF} passed over while skipping is
// deliberately NOT applied - side effects inside a dead branch don't
// happen, matching real Pascal.
//
// Returns 1 if it stopped at a level-0 {$ELSE} (only possible when
// stop_at_else is true), 0 if it stopped at a level-0 {$ENDIF}. EOF, or
// a level-0 {$ELSE} when stop_at_else is false (a duplicate {$ELSE}),
// is a lexer_error().
static int skip_conditional(int stop_at_else) {
    int nesting = 0;
    for (;;) {
        if (!*src) {
            lexer_error("Unterminated '$IFDEF'/'$IFNDEF' (missing '$ENDIF')");
        }
        if (src[0] == '{' && src[1] == '$') {
            src += 2;
            skip_directive_ws();
            char word[MAX_NAME];
            read_directive_word(word, sizeof(word));
            if (strcasecmp(word, "IFDEF") == 0 || strcasecmp(word, "IFNDEF") == 0) {
                nesting++;
                skip_to_closing_brace();
            } else if (strcasecmp(word, "ELSE") == 0) {
                skip_to_closing_brace();
                if (nesting == 0) {
                    if (!stop_at_else) {
                        lexer_error("Duplicate '$ELSE' for the same '$IFDEF'/'$IFNDEF'");
                    }
                    return 1;
                }
            } else if (strcasecmp(word, "ENDIF") == 0) {
                skip_to_closing_brace();
                if (nesting == 0) return 0;
                nesting--;
            } else {
                skip_to_closing_brace();
            }
            continue;
        }
        if (*src == '\n') current_line++;
        src++;
    }
}

void init_lexer(const char *source) {
    src = source;
    current_line = 1;
    ifdef_depth = 0;
    next_token();
}

LexerPos lexer_save_pos(void) {
    LexerPos pos;
    pos.src = src;
    pos.current_line = current_line;
    pos.ifdef_depth = ifdef_depth;
    memcpy(pos.ifdef_in_else, ifdef_in_else, sizeof(int) * ifdef_depth);
    return pos;
}

void lexer_restore_pos(LexerPos pos) {
    src = pos.src;
    current_line = pos.current_line;
    ifdef_depth = pos.ifdef_depth;
    memcpy(ifdef_in_else, pos.ifdef_in_else, sizeof(int) * ifdef_depth);
}

void next_token(void) {
    while (*src && isspace(*src)) {
        if (*src == '\n') current_line++;
        src++;
    }

    token.line = current_line;

    if (*src == '{') {
        if (*(src + 1) == '$') {
            src += 2;
            skip_directive_ws();
            char word[MAX_NAME];
            read_directive_word(word, sizeof(word));

            if (strcasecmp(word, "DEFINE") == 0) {
                skip_directive_ws();
                char sym[MAX_NAME];
                read_directive_word(sym, sizeof(sym));
                if (!sym[0]) lexer_error("Expected a symbol name after '$DEFINE'");
                skip_to_closing_brace();
                define_symbol(sym);
            } else if (strcasecmp(word, "UNDEF") == 0) {
                skip_directive_ws();
                char sym[MAX_NAME];
                read_directive_word(sym, sizeof(sym));
                if (!sym[0]) lexer_error("Expected a symbol name after '$UNDEF'");
                skip_to_closing_brace();
                undef_symbol(sym);
            } else if (strcasecmp(word, "IFDEF") == 0 || strcasecmp(word, "IFNDEF") == 0) {
                int negate = (strcasecmp(word, "IFNDEF") == 0);
                skip_directive_ws();
                char sym[MAX_NAME];
                read_directive_word(sym, sizeof(sym));
                if (!sym[0]) lexer_error("Expected a symbol name after '$IFDEF'/'$IFNDEF'");
                skip_to_closing_brace();
                int cond = is_symbol_defined(sym);
                if (negate) cond = !cond;
                if (ifdef_depth >= MAX_IFDEF_DEPTH) {
                    lexer_error("'$IFDEF'/'$IFNDEF' nested too deeply (limit is %d)", MAX_IFDEF_DEPTH);
                }
                ifdef_in_else[ifdef_depth++] = 0;
                if (!cond) {
                    if (skip_conditional(1)) ifdef_in_else[ifdef_depth - 1] = 1;
                    else ifdef_depth--;
                }
            } else if (strcasecmp(word, "ELSE") == 0) {
                skip_to_closing_brace();
                if (ifdef_depth == 0) lexer_error("'$ELSE' without a matching '$IFDEF'/'$IFNDEF'");
                if (ifdef_in_else[ifdef_depth - 1]) lexer_error("Duplicate '$ELSE' for the same '$IFDEF'/'$IFNDEF'");
                // The 'if' branch was just lexed actively, so the
                // 'else' branch is always dead - skip straight to this
                // level's own $ENDIF.
                skip_conditional(0);
                ifdef_depth--;
            } else if (strcasecmp(word, "ENDIF") == 0) {
                skip_to_closing_brace();
                if (ifdef_depth == 0) lexer_error("'$ENDIF' without a matching '$IFDEF'/'$IFNDEF'");
                ifdef_depth--;
            } else {
                // Any other {$...} directive ({$R+}, {$Q-}, etc.) -
                // accepted, silently ignored, exactly like a plain
                // comment. Deliberate v1 scope cut - see
                // docs/LANGUAGE.md#compiler-directives.
                skip_to_closing_brace();
            }
            next_token();
            return;
        }
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

    if (*src == '(' && *(src + 1) == '*') {
        src += 2;
        while (*src && !(*src == '*' && *(src + 1) == ')')) {
            if (*src == '\n') current_line++;
            src++;
        }
        if (*src) src += 2;
        next_token();
        return;
    }

    if (!*src) {
        if (ifdef_depth > 0) {
            lexer_error("Unterminated '$IFDEF'/'$IFNDEF' (missing '$ENDIF')");
        }
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
        else if (strcasecmp(token.text, "read") == 0) token.type = TOKEN_READ;
        else if (strcasecmp(token.text, "eoln") == 0) token.type = TOKEN_EOLN;
        else if (strcasecmp(token.text, "eof") == 0) token.type = TOKEN_EOF_FN;
        else if (strcasecmp(token.text, "set") == 0) token.type = TOKEN_SET;
        else if (strcasecmp(token.text, "in") == 0) token.type = TOKEN_IN;
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
        else if (strcasecmp(token.text, "label") == 0) token.type = TOKEN_LABEL;
        else if (strcasecmp(token.text, "goto") == 0) token.type = TOKEN_GOTO;
        else if (strcasecmp(token.text, "text") == 0) token.type = TOKEN_TEXT_TYPE;
        else if (strcasecmp(token.text, "assign") == 0) token.type = TOKEN_FILE_ASSIGN;
        else if (strcasecmp(token.text, "reset") == 0) token.type = TOKEN_RESET;
        else if (strcasecmp(token.text, "rewrite") == 0) token.type = TOKEN_REWRITE;
        else if (strcasecmp(token.text, "close") == 0) token.type = TOKEN_CLOSE;
        else if (strcasecmp(token.text, "new") == 0) token.type = TOKEN_NEW;
        else if (strcasecmp(token.text, "dispose") == 0) token.type = TOKEN_DISPOSE;
        else if (strcasecmp(token.text, "nil") == 0) token.type = TOKEN_NIL;
        else if (strcasecmp(token.text, "char") == 0) token.type = TOKEN_CHAR_TYPE;
        else if (strcasecmp(token.text, "real") == 0) token.type = TOKEN_REAL_TYPE;
        else if (strcasecmp(token.text, "byte") == 0) token.type = TOKEN_BYTE;
        else if (strcasecmp(token.text, "shortint") == 0) token.type = TOKEN_SHORTINT;
        else if (strcasecmp(token.text, "word") == 0) token.type = TOKEN_WORD;
        else if (strcasecmp(token.text, "trunc") == 0) token.type = TOKEN_TRUNC;
        else if (strcasecmp(token.text, "round") == 0) token.type = TOKEN_ROUND;
        else if (strcasecmp(token.text, "type") == 0) token.type = TOKEN_TYPE;
        else if (strcasecmp(token.text, "record") == 0) token.type = TOKEN_RECORD;
        else if (strcasecmp(token.text, "class") == 0) token.type = TOKEN_CLASS;
        else if (strcasecmp(token.text, "const") == 0) token.type = TOKEN_CONST;
        else if (strcasecmp(token.text, "with") == 0) token.type = TOKEN_WITH;
        else if (strcasecmp(token.text, "assert") == 0) token.type = TOKEN_ASSERT;
        else if (strcasecmp(token.text, "static") == 0) token.type = TOKEN_STATIC;
        else if (strcasecmp(token.text, "case") == 0) token.type = TOKEN_CASE;
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
        else if (strcasecmp(token.text, "unit") == 0) token.type = TOKEN_UNIT;
        else if (strcasecmp(token.text, "interface") == 0) token.type = TOKEN_INTERFACE;
        else if (strcasecmp(token.text, "implementation") == 0) token.type = TOKEN_IMPLEMENTATION;
        else if (strcasecmp(token.text, "uses") == 0) token.type = TOKEN_USES;
        else if (strcasecmp(token.text, "initialization") == 0) token.type = TOKEN_INITIALIZATION;
        else if (strcasecmp(token.text, "finalization") == 0) token.type = TOKEN_FINALIZATION;
        else if (strcasecmp(token.text, "inherited") == 0) token.type = TOKEN_INHERITED;
        else if (strcasecmp(token.text, "paramcount") == 0) token.type = TOKEN_PARAMCOUNT;
        else if (strcasecmp(token.text, "paramstr") == 0) token.type = TOKEN_PARAMSTR;
        else if (strcasecmp(token.text, "private") == 0) token.type = TOKEN_PRIVATE;
        else if (strcasecmp(token.text, "public") == 0) token.type = TOKEN_PUBLIC;
        else if (strcasecmp(token.text, "try") == 0) token.type = TOKEN_TRY;
        else if (strcasecmp(token.text, "except") == 0) token.type = TOKEN_EXCEPT;
        else if (strcasecmp(token.text, "finally") == 0) token.type = TOKEN_FINALLY;
        else if (strcasecmp(token.text, "destructor") == 0) token.type = TOKEN_DESTRUCTOR;
        else if (strcasecmp(token.text, "raise") == 0) token.type = TOKEN_RAISE;
        else if (strcasecmp(token.text, "warning") == 0) token.type = TOKEN_WARNING;
        else if (strcasecmp(token.text, "exceptmessage") == 0) token.type = TOKEN_EXCEPTMESSAGE;
        else if (strcasecmp(token.text, "property") == 0) token.type = TOKEN_PROPERTY;
        else if (strcasecmp(token.text, "is") == 0) token.type = TOKEN_IS;
        else if (strcasecmp(token.text, "as") == 0) token.type = TOKEN_AS;
        else if (strcasecmp(token.text, "file") == 0) token.type = TOKEN_FILE_TYPE;
        else if (strcasecmp(token.text, "seek") == 0) token.type = TOKEN_SEEK;
        else if (strcasecmp(token.text, "filesize") == 0) token.type = TOKEN_FILESIZE;
        else if (strcasecmp(token.text, "abstract") == 0) token.type = TOKEN_ABSTRACT;
        else if (strcasecmp(token.text, "sealed") == 0) token.type = TOKEN_SEALED;
        else if (strcasecmp(token.text, "out") == 0) token.type = TOKEN_OUT;
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
        case '^': token.type = TOKEN_CARET; break;
        default:  token.type = TOKEN_EOF; break;
    }

}

