#ifndef LEXER_H
#define LEXER_H

#include "common.h"

void init_lexer(const char *source);
void next_token(void);

// Saves/restores the lexer's position in its current source buffer (and
// line counter, and {$IFDEF}/{$IFNDEF} nesting state) - not the source
// buffer itself, and not 'token' (already a plain global the caller can
// save/restore directly). Lets the parser recurse into a 'uses'-d
// unit's own source via init_lexer(), then come back to exactly where
// the referencing file's 'uses' clause left off. {$IFDEF} nesting is
// per-file (init_lexer() resets it fresh for the unit's own pass), so
// it rides along in this same struct; {$DEFINE}d symbols are NOT part
// of this - see lexer_reset_defines() below for why.
#define MAX_IFDEF_DEPTH 16
typedef struct {
    const char *src;
    int current_line;
    int ifdef_depth;
    int ifdef_in_else[MAX_IFDEF_DEPTH];
} LexerPos;
LexerPos lexer_save_pos(void);
void lexer_restore_pos(LexerPos pos);

// Clears the {$DEFINE}d-symbol table. Called exactly once per whole
// compile, from parse_ast() - NOT from init_lexer(), which runs once
// per FILE (main program AND every nested unit) - so a {$DEFINE} made
// by one file in a compile stays visible to every other file 'uses'-d
// in that same compile, matching real Pascal's "$DEFINE lasts the rest
// of the compile" semantics, while still never leaking into an
// unrelated later compile in the same long-lived host process.
void lexer_reset_defines(void);

#endif

