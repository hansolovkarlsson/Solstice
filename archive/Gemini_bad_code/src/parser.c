#include "common.h"


// Bridge lexer naming differences
#define current_tok current_token

static inline void advance(void) {
    advance_token();
}

static inline void consume(TokenType type, const char *err_msg) {
    if (current_token.type == type) {
        advance_token();
    } else {
        compile_error(current_token.line, err_msg);
    }
}

// Helper function to create standard AST nodes
ASTNode *create_ast_node(NodeType type) {
    ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
    if (!node) {
        compile_error(0, "Memory allocation failed for AST node");
    }
    node->type = type;
    node->expression_type = TYPE_UNKNOWN;
    node->data.int_val = 0;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    return node;
}

// Forward declarations for recursive descent parsing
static ASTNode *parse_expression(void);
static ASTNode *parse_statement(void);
static ASTNode *parse_block(void);

// Parse primary factor (literals, identifiers, parenthesized expressions)
static ASTNode *parse_factor(void) {
    ASTNode *node = NULL;

    if (current_tok.type == TOKEN_INT_LITERAL) {
        node = create_ast_node(NODE_INT);
        node->data.int_val = current_tok.value.int_val;
        node->expression_type = TYPE_INTEGER;
        advance();
    } else if (current_tok.type == TOKEN_BOOL_LITERAL) {
        node = create_ast_node(NODE_BOOL);
        node->data.bool_val = current_tok.value.bool_val;
        node->expression_type = TYPE_BOOLEAN;
        advance();
    } else if (current_tok.type == TOKEN_STRING) {
        node = create_ast_node(NODE_STRING);
        node->data.str_idx = current_tok.value.str_idx;
        node->expression_type = TYPE_STRING;
        advance();
    } else if (current_tok.type == TOKEN_IDENTIFIER) {
        node = create_ast_node(NODE_VAR);
        node->data.name = strdup(current_tok.lexeme);
        advance();
    } else if (current_tok.type == TOKEN_LPAREN) {
        advance();
        node = parse_expression();
        consume(TOKEN_RPAREN, "Expected ')' after expression");
    } else {
        compile_error(current_tok.line, "Expected expression factor");
    }

    return node;
}

// Parse unary expressions (NOT, Unary Minus)
static ASTNode *parse_unary(void) {
    if (current_tok.type == TOKEN_MINUS || current_tok.type == TOKEN_NOT) {
        TokenType op = current_tok.type;
        advance();
        ASTNode *node = create_ast_node(NODE_UNOP);
        node->data.op = op;
        node->left = parse_unary();
        return node;
    }
    return parse_factor();
}

// Parse multiplicative terms (*, /, DIV, MOD, AND)
static ASTNode *parse_term(void) {
    ASTNode *node = parse_unary();

    while (current_tok.type == TOKEN_STAR || current_tok.type == TOKEN_SLASH ||
           current_tok.type == TOKEN_DIV  || current_tok.type == TOKEN_MOD ||
           current_tok.type == TOKEN_AND) {
        TokenType op = current_tok.type;
        advance();
        ASTNode *right = parse_unary();
        
        ASTNode *binop = create_ast_node(NODE_BINOP);
        binop->data.op = op;
        binop->left = node;
        binop->right = right;
        node = binop;
    }
    return node;
}

// Parse additive expressions (+, -, OR)
static ASTNode *parse_simple_expression(void) {
    ASTNode *node = parse_term();

    while (current_tok.type == TOKEN_PLUS || current_tok.type == TOKEN_MINUS ||
           current_tok.type == TOKEN_OR) {
        TokenType op = current_tok.type;
        advance();
        ASTNode *right = parse_term();

        ASTNode *binop = create_ast_node(NODE_BINOP);
        binop->data.op = op;
        binop->left = node;
        binop->right = right;
        node = binop;
    }
    return node;
}

// Parse relational expressions (=, <>, <, <=, >, >=)
static ASTNode *parse_expression(void) {
    ASTNode *node = parse_simple_expression();

    if (current_tok.type == TOKEN_EQ || current_tok.type == TOKEN_NEQ ||
        current_tok.type == TOKEN_LT || current_tok.type == TOKEN_LTE ||
        current_tok.type == TOKEN_GT || current_tok.type == TOKEN_GTE) {
        TokenType op = current_tok.type;
        advance();
        ASTNode *right = parse_simple_expression();

        ASTNode *binop = create_ast_node(NODE_BINOP);
        binop->data.op = op;
        binop->left = node;
        binop->right = right;
        node = binop;
    }
    return node;
}

// Parse 'writeln' statement and attach parameters as a chain on node->left
static ASTNode *parse_writeln(void) {
    ASTNode *node = create_ast_node(NODE_WRITELN);
    advance(); // Consume TOKEN_WRITELN

    if (current_tok.type == TOKEN_LPAREN) {
        advance(); // Consume '('
        
        ASTNode *arg_head = NULL;
        ASTNode *arg_tail = NULL;

        if (current_tok.type != TOKEN_RPAREN) {
            while (1) {
                ASTNode *arg = parse_expression();
                
                if (!arg_head) {
                    arg_head = arg;
                    arg_tail = arg;
                } else {
                    arg_tail->next = arg; // Link parameters horizontally
                    arg_tail = arg;
                }

                if (current_tok.type == TOKEN_COMMA) {
                    advance();
                } else {
                    break;
                }
            }
        }
        consume(TOKEN_RPAREN, "Expected ')' after writeln parameters");
        node->left = arg_head; // Attach parameter list to left child
    }

    return node;
}

// Parse variable assignment statements (x := expr)
static ASTNode *parse_assignment(void) {
    ASTNode *node = create_ast_node(NODE_ASSIGN);
    node->data.name = strdup(current_tok.lexeme);
    advance(); // Consume identifier

    consume(TOKEN_ASSIGN, "Expected ':=' in assignment");
    node->left = parse_expression();
    return node;
}

// Parse single statement
static ASTNode *parse_statement(void) {
    if (current_tok.type == TOKEN_WRITELN) {
        return parse_writeln();
    } else if (current_tok.type == TOKEN_IDENTIFIER) {
        return parse_assignment();
    } else if (current_tok.type == TOKEN_BEGIN) {
        return parse_block();
    }

    return NULL; // Empty statement
}

// Parse BEGIN ... END compound block
static ASTNode *parse_block(void) {
    consume(TOKEN_BEGIN, "Expected 'begin'");

    ASTNode *head = NULL;
    ASTNode *tail = NULL;

    while (current_tok.type != TOKEN_END && current_tok.type != TOKEN_EOF) {
        ASTNode *stmt = parse_statement();
        if (stmt) {
            if (!head) {
                head = stmt;
                tail = stmt;
            } else {
                tail->next = stmt;
                tail = stmt;
            }
        }

        if (current_tok.type == TOKEN_SEMICOLON) {
            advance();
        } else if (current_tok.type != TOKEN_END) {
            compile_error(current_tok.line, "Expected ';' or 'end'");
        }
    }

    consume(TOKEN_END, "Expected 'end'");

    ASTNode *block = create_ast_node(NODE_BLOCK);
    block->left = head; // Statement chain hangs off block->left
    return block;
}

// Parse variable declarations section
static void parse_var_declarations(void) {
    if (current_tok.type == TOKEN_VAR) {
        advance();

        while (current_tok.type == TOKEN_IDENTIFIER) {
            while (current_tok.type == TOKEN_IDENTIFIER) {
                advance();
                if (current_tok.type == TOKEN_COMMA) {
                    advance();
                } else {
                    break;
                }
            }

            consume(TOKEN_COLON, "Expected ':' after variable names");

            if (current_tok.type == TOKEN_TYPE_INT || current_tok.type == TOKEN_TYPE_BOOL) {
                advance();
            } else {
                compile_error(current_tok.line, "Invalid variable type");
            }

            consume(TOKEN_SEMICOLON, "Expected ';' after variable declaration");
        }
    }
}

// Main parser entry point
ASTNode *parse_program(void) {
    consume(TOKEN_PROGRAM, "Expected 'program'");
    
    if (current_tok.type == TOKEN_IDENTIFIER) {
        advance();
    }
    consume(TOKEN_SEMICOLON, "Expected ';' after program name");

    parse_var_declarations();
    ASTNode *ast = parse_block();
    consume(TOKEN_DOT, "Expected '.' at end of program");

    return ast;
}