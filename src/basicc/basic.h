#ifndef BASIC_H
#define BASIC_H

#include "common.h"
#include "error.h"

// Classic line-numbered BASIC, milestone 1 - see docs/BASIC.md. Deliberately
// its own small parallel vocabulary (BasicTokenType/BasicNodeType/
// BasicASTNode) rather than extending common.h's TokenType/NodeType/
// ASTNode: basicc and pascalc are separate binaries that never link
// together (see the root Makefile), so nothing is gained by sharing
// Pascal's much larger enums, and basic.h stays free to grow along BASIC's
// own lines. What IS shared, unchanged, straight from common.h: DataType,
// Symbol/sym_table[], Opcode/Instruction/code[], string_pool[] - exactly
// the surface save_bytecode() needs, which is why solvm/solas/desole need
// no changes at all to run a BASIC-compiled .bin.

#define MAX_BASIC_LINES 500 // how many distinct source line numbers one
                            // program can declare - bounds the sorted
                            // basic_line_numbers[]/basic_line_addrs[]
                            // table GOTO/GOSUB/THEN-linenum targets
                            // resolve against.

typedef enum {
    BTOK_EOF, BTOK_EOL,
    BTOK_NUMBER, BTOK_REAL, BTOK_STRING, BTOK_IDENT,

    BTOK_LET, BTOK_PRINT, BTOK_INPUT, BTOK_IF, BTOK_THEN, BTOK_ELSE,
    BTOK_GOTO, BTOK_GOSUB, BTOK_RETURN, BTOK_FOR, BTOK_TO, BTOK_STEP,
    BTOK_NEXT, BTOK_END, BTOK_AND, BTOK_OR, BTOK_NOT,

    BTOK_EQ, BTOK_PLUS, BTOK_MINUS, BTOK_MUL, BTOK_SLASH,
    BTOK_LT, BTOK_GT, BTOK_LTE, BTOK_GTE, BTOK_NEQ,
    BTOK_LPAREN, BTOK_RPAREN, BTOK_COMMA, BTOK_SEMI, BTOK_COLON,
} BasicTokenType;

typedef struct {
    BasicTokenType type;
    char text[MAX_NAME];  // identifier/keyword text, uppercased, sigil
                          // (if any) included as the LAST character - see
                          // lexer.c's identifier scanning.
    char string_value[MAX_STRING_LEN]; // populated only for BTOK_STRING -
                          // interned into string_pool[] by the PARSER (at
                          // each use site), same division of labor as
                          // pascalc's own Token/lexer.c/intern_string().
    int value;             // BTOK_NUMBER's literal value
    float real_value;      // BTOK_REAL's literal value
    int line;               // the BASIC source line number this token was
                            // read from - see BasicASTNode.line below.
} BasicToken;

extern BasicToken btoken;

void basic_init_lexer(const char *source);
void basic_next_token(void);

typedef enum {
    BNODE_NUMBER, BNODE_STRING, BNODE_VARIABLE,
    BNODE_UNARY_OP, BNODE_BINARY_OP, BNODE_INT_TO_REAL,
    BNODE_LET, BNODE_PRINT, BNODE_INPUT,
    BNODE_IF, BNODE_GOTO, BNODE_GOSUB, BNODE_RETURN,
    BNODE_FOR, BNODE_NEXT, BNODE_END,
} BasicNodeType;

// Fixed four-child-pointer shape, mirroring ASTNode's own reuse-by-
// convention discipline (see CLAUDE.md) - every new node kind added so
// far has fit without a fifth pointer. `line` doubles as both "which
// BASIC line number produced this statement" (what GOTO/GOSUB target)
// and the usual "line for an error message" - the same concept in this
// language, unlike Pascal where they differ.
typedef struct BasicASTNode {
    BasicNodeType type;
    BasicTokenType op;              // meaning depends on `type` - see codegen.c
    DataType expression_type;
    int line;
    union { int num_value; int var_idx; } data;
    struct BasicASTNode *left, *right, *next, *extra;
} BasicASTNode;

const char *basic_get_current_filename(void);
void basic_set_current_filename(const char *f);
BasicASTNode *basic_create_node(BasicNodeType type);
BasicASTNode *basic_parse_ast(const char *source, const char *filename);
void basic_free_ast(BasicASTNode *node);
int basic_intern_string(const char *s);

// Every distinct line number the program declared, in ascending source
// order (parse_ast() enforces strictly-ascending line numbers, so this
// is already sorted) - populated by parser.c, read by type_checker.c (to
// validate GOTO/GOSUB/THEN-linenum targets) and codegen.c (to backpatch
// them once every line's code address is known).
extern int basic_line_numbers[MAX_BASIC_LINES];
extern int basic_line_count;
int basic_find_line_index(int line_number); // -1 if undefined

void basic_type_check(BasicASTNode *node);

void basic_generate_code(BasicASTNode *node);
void basic_generate_program(BasicASTNode *program);
void basic_emit_halt(void);

void basic_print_ast(BasicASTNode *node, int indent);

#endif
