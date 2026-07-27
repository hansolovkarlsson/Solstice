#include "common.h"

Instruction code[1024];
int code_count = 0;

static void emit(Opcode op, int arg) {
    code[code_count].op = op;
    code[code_count].arg = arg;
    code_count++;
}

void generate_code(ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case NODE_INT:
            emit(OP_PUSH_INT, node->data.int_val);
            break;

        case NODE_BOOL:
            emit(OP_PUSH_BOOL, node->data.bool_val ? 1 : 0);
            break;

        case NODE_STRING:
            // Pushes index for potential stack operations
            break;

        case NODE_VAR: {
            int idx = lookup_symbol(node->data.name);
            emit(OP_LOAD, idx);
            break;
        }

        case NODE_UNOP:
            generate_code(node->left);
            if (node->data.op == TOKEN_NOT) emit(OP_NOT, 0);
            else if (node->data.op == TOKEN_MINUS) {
                emit(OP_PUSH_INT, -1);
                emit(OP_MUL, 0);
            }
            break;

        case NODE_BINOP:
            generate_code(node->left);
            generate_code(node->right);
            switch (node->data.op) {
                case TOKEN_PLUS:  emit(OP_ADD, 0); break;
                case TOKEN_MINUS: emit(OP_SUB, 0); break;
                case TOKEN_STAR:  emit(OP_MUL, 0); break;
                case TOKEN_SLASH:
                case TOKEN_DIV:   emit(OP_DIV, 0); break;
                case TOKEN_MOD:   emit(OP_MOD, 0); break;
                case TOKEN_AND:   emit(OP_AND, 0); break;
                case TOKEN_OR:    emit(OP_OR, 0); break;
                case TOKEN_XOR:   emit(OP_XOR, 0); break;
                case TOKEN_EQ:    emit(OP_EQ, 0); break;
                case TOKEN_NEQ:   emit(OP_NEQ, 0); break;
                case TOKEN_LT:    emit(OP_LT, 0); break;
                case TOKEN_LTE:   emit(OP_LTE, 0); break;
                case TOKEN_GT:    emit(OP_GT, 0); break;
                case TOKEN_GTE:   emit(OP_GTE, 0); break;
                default: break;
            }
            break;

        case NODE_ASSIGN:
            generate_code(node->left);
            emit(OP_STORE, lookup_symbol(node->data.name));
            break;

        case NODE_WRITELN: {
            // 1. Iterate through the parameter list attached to node->left
            ASTNode *arg = node->left;
            while (arg) {
                generate_code(arg); // Generate evaluation code for this argument
                
                if (arg->expression_type == TYPE_BOOLEAN) {
                    emit(OP_WRITE_BOOL, 0);
                } else if (arg->expression_type == TYPE_STRING) {
                    emit(OP_WRITE_STR, arg->data.str_idx);
                } else {
                    emit(OP_WRITE_INT, 0);
                }
                
                arg = arg->next; // Next comma-separated argument
            }

            // 2. Output single newline for this writeln call
            emit(OP_PRINT_NEWLINE, 0);
            break; // Allow normal fall-through to generate_code(node->next) at the bottom
        }

        case NODE_READLN:
            emit(OP_READ, 0);
            emit(OP_STORE, lookup_symbol(node->data.name));
            break;

        default:
            generate_code(node->left);
            generate_code(node->right);
            break;
    }

    generate_code(node->next);
}

int get_generated_code(Instruction **out_instructions) {
    emit(OP_HALT, 0);
    *out_instructions = code;
    return code_count;
}

