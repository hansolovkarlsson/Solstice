#ifndef LEXER_H
#define LEXER_H

#include "common.h"

extern Token token;
extern int current_line;

void init_lexer(const char *source);
void next_token(void);

#endif
