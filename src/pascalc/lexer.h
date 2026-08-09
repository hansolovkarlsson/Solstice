#ifndef LEXER_H
#define LEXER_H

#include "common.h"

void init_lexer(const char *source);
void next_token(void);

// Saves/restores the lexer's position in its current source buffer (and
// line counter) - not the source buffer itself, and not 'token' (already
// a plain global the caller can save/restore directly). Lets the parser
// recurse into a 'uses'-d unit's own source via init_lexer(), then come
// back to exactly where the referencing file's 'uses' clause left off.
typedef struct {
    const char *src;
    int current_line;
} LexerPos;
LexerPos lexer_save_pos(void);
void lexer_restore_pos(LexerPos pos);

#endif

