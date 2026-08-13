#include <stdio.h>
#include <string.h>
#include "ast_printer.h"

// Local copy of the bit-reinterpretation helper (see vm.c for the full
// explanation) - needed here just to print a real literal's actual value
// rather than its raw bit pattern.
static float bits_to_float(int bits) {
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        printf("    ");
    }
}

static const char* token_type_to_str(TokenType type) {
    switch (type) {
        case TOKEN_PLUS:  return "+";
        case TOKEN_MINUS: return "-";
        case TOKEN_MUL:   return "*";
        case TOKEN_DIV:   return "/";
        case TOKEN_EQ:    return "=";
        case TOKEN_LT:    return "<";
        case TOKEN_GT:    return ">";
        case TOKEN_AND:   return "and";
        case TOKEN_OR:    return "or";
        case TOKEN_NOT:   return "not";
        case TOKEN_LTE:   return "<=";
        case TOKEN_GTE:   return ">=";
        case TOKEN_NEQ:   return "<>";
        case TOKEN_DIV_KW: return "div";
        case TOKEN_MOD:    return "mod";
        case TOKEN_XOR:    return "xor";
        default:          return "?";
    }
}

void print_ast(ASTNode *node, int indent) {
    if (!node) return;

    print_indent(indent);

    switch (node->type) {
        case NODE_COMPOUND:
            printf("[Compound Statement]\n");
            print_ast(node->left, indent + 1);
            break;

        case NODE_ASSIGN:
            if (sym_table[node->data.var_idx].is_array) {
                printf("[Array Assignment] -> Array: %s\n", sym_table[node->data.var_idx].name);
                print_indent(indent + 1);
                printf("Index:\n");
                print_ast(node->left, indent + 2);
                print_indent(indent + 1);
                printf("Value:\n");
                print_ast(node->right, indent + 2);
            } else {
                printf("[Assignment] -> Variable: %s\n", sym_table[node->data.var_idx].name);
                print_indent(indent + 1);
                printf("Value:\n");
                print_ast(node->left, indent + 2);
            }
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_WRITELN: {
            printf("[%s]\n", node->op == TOKEN_WRITE ? "Write" : "WriteLn");
            if (node->extra) {
                print_indent(indent + 1);
                printf("File: %s\n", sym_table[node->extra->data.var_idx].name);
            }
            int arg_num = 1;
            for (ASTNode *arg = node->left; arg; arg = arg->next) {
                print_indent(indent + 1);
                printf("Arg %d:\n", arg_num++);
                print_ast(arg, indent + 2);
            }
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;
        }

        case NODE_WRITE_ARG:
            print_ast(node->left, indent);
            if (node->right) {
                print_indent(indent);
                printf("Width:\n");
                print_ast(node->right, indent + 1);
            }
            if (node->extra) {
                print_indent(indent);
                printf("Precision:\n");
                print_ast(node->extra, indent + 1);
            }
            break;
        case NODE_READLN:
            printf("[%s] -> Target Variable: %s\n", node->op == TOKEN_READ ? "Read" : "ReadLn", sym_table[node->data.var_idx].name);
            if (node->extra) {
                print_indent(indent + 1);
                printf("File: %s\n", sym_table[node->extra->data.var_idx].name);
            }
            if (node->left) {
                print_indent(indent + 1);
                printf("(skips a leftover newline first)\n");
            }
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_UNARY_OP:
            printf("[Unary Op] '%s'\n", token_type_to_str(node->op));
            print_indent(indent + 1);
            printf("Operand:\n");
            print_ast(node->left, indent + 2);
            break;

        case NODE_BINARY_OP:
            printf("[Binary Op] '%s'\n", token_type_to_str(node->op));
            print_indent(indent + 1);
            printf("Left:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Right:\n");
            print_ast(node->right, indent + 2);
            break;

        case NODE_NUMBER:
            if (node->expression_type >= TYPE_ENUM_BASE && node->expression_type < TYPE_POINTER_BASE) {
                EnumTypeDef *et = &enum_types[node->expression_type - TYPE_ENUM_BASE];
                printf("[Enum] %s (%s)\n",
                       node->data.num_value >= 0 && node->data.num_value < et->value_count
                           ? et->value_names[node->data.num_value] : "?",
                       et->name);
            } else {
                printf("[Number] %d\n", node->data.num_value);
            }
            break;

        case NODE_REAL_NUMBER:
            printf("[Real] %g\n", bits_to_float(node->data.num_value));
            break;

        case NODE_INT_TO_REAL:
            printf("[Int->Real Widen]\n");
            print_ast(node->left, indent + 1);
            break;

        case NODE_BOOLEAN:
            printf("[Boolean] %s\n", node->data.num_value ? "true" : "false");
            break;

        case NODE_VARIABLE:
            printf("[Variable] %s\n", sym_table[node->data.var_idx].name);
            break;

        case NODE_STRING:
            printf("[String] \"%s\"\n", string_pool[node->data.var_idx]);
            break;

        case NODE_IF:
            printf("[If]\n");
            print_indent(indent + 1);
            printf("Condition:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Then:\n");
            print_ast(node->right, indent + 2);
            if (node->extra) {
                print_indent(indent + 1);
                printf("Else:\n");
                print_ast(node->extra, indent + 2);
            }
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_WHILE:
            printf("[While]\n");
            print_indent(indent + 1);
            printf("Condition:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Body:\n");
            print_ast(node->right, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_REPEAT:
            printf("[Repeat]\n");
            print_indent(indent + 1);
            printf("Body:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Until:\n");
            print_ast(node->right, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_FOR:
            printf("[For] -> Variable: %s (%s)\n", sym_table[node->data.var_idx].name,
                   node->op == TOKEN_DOWNTO ? "downto" : "to");
            print_indent(indent + 1);
            printf("From:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("To:\n");
            print_ast(node->right, indent + 2);
            print_indent(indent + 1);
            printf("Do:\n");
            print_ast(node->extra, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_LOCAL_FOR:
            printf("[Local For] -> slot %d (%s)\n", node->data.var_idx,
                   node->op == TOKEN_DOWNTO ? "downto" : "to");
            print_indent(indent + 1);
            printf("From:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("To:\n");
            print_ast(node->right, indent + 2);
            print_indent(indent + 1);
            printf("Do:\n");
            print_ast(node->extra, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_LOCAL_READLN:
            printf("[Local %s] -> slot %d\n", node->op == TOKEN_READ ? "Read" : "ReadLn", node->data.var_idx);
            if (node->extra) {
                print_indent(indent + 1);
                printf("File: %s\n", sym_table[node->extra->data.var_idx].name);
            }
            if (node->left) {
                print_indent(indent + 1);
                printf("(skips a leftover newline first)\n");
            }
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_ARRAY_ACCESS:
            printf("[Array Access] -> Array: %s\n", sym_table[node->data.var_idx].name);
            print_indent(indent + 1);
            printf("Index:\n");
            print_ast(node->left, indent + 2);
            break;

        case NODE_RANGE_CHECK:
            printf("[Range Check %d..%d]\n", node->right->data.num_value, node->extra->data.num_value);
            print_ast(node->left, indent + 1);
            break;

        case NODE_ASSERT:
            printf("[Assert]\n");
            print_indent(indent + 1);
            printf("Condition:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Message:\n");
            print_ast(node->right, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_TRY:
            printf("[Try]\n");
            print_indent(indent + 1);
            printf("Body:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("%s\n", node->op == TOKEN_FINALLY ? "Finally:" : "Except:");
            print_ast(node->right, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_RAISE:
            printf("[Raise]\n");
            print_indent(indent + 1);
            printf("Message:\n");
            print_ast(node->left, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_WARNING:
            printf("[Warning]\n");
            print_indent(indent + 1);
            printf("Message:\n");
            print_ast(node->left, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_IS_TEST:
            printf("[Is Test] -> Class ID: %d\n", node->data.num_value);
            print_ast(node->left, indent + 1);
            break;

        case NODE_AS_CAST:
            printf("[As Cast] -> Class ID: %d\n", node->data.num_value);
            print_ast(node->left, indent + 1);
            break;

        case NODE_CLASS_PARENT_INIT_ENTRY:
            printf("[Class Parent Init] -> Class: %d, Parent: %d\n", node->data.num_value, node->left->data.num_value);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_FILE_OP: {
            const char *op_name = node->op == TOKEN_FILE_ASSIGN ? "Assign"
                                 : node->op == TOKEN_RESET ? "Reset"
                                 : node->op == TOKEN_REWRITE ? "Rewrite"
                                 : node->op == TOKEN_SEEK ? "Seek" : "Close";
            printf("[%s] -> File: %s\n", op_name, sym_table[node->data.var_idx].name);
            if (node->left) {
                print_indent(indent + 1);
                printf(node->op == TOKEN_SEEK ? "Record index:\n" : "Filename:\n");
                print_ast(node->left, indent + 2);
            }
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;
        }

        case NODE_TYPED_FILE_READ_LEAF:
            printf("[Typed File Read Leaf] -> File: %s%s\n", sym_table[node->data.var_idx].name,
                   node->op == TOKEN_BYTE ? " (byte)" : node->op == TOKEN_SHORTINT ? " (shortint)" : node->op == TOKEN_WORD ? " (word)" : "");
            break;

        case NODE_TYPED_FILE_WRITE_LEAF:
            printf("[Typed File Write Leaf] -> File: %s%s\n", sym_table[node->data.var_idx].name,
                   node->op == TOKEN_BYTE ? " (byte)" : node->op == TOKEN_SHORTINT ? " (shortint)" : node->op == TOKEN_WORD ? " (word)" : "");
            print_ast(node->left, indent + 1);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_CASE:
            printf("[Case]\n");
            print_indent(indent + 1);
            printf("Selector:\n");
            print_ast(node->left, indent + 2);
            print_ast(node->right, indent + 1); // the NODE_CASE_ARM chain
            if (node->extra) {
                print_indent(indent + 1);
                printf("Else:\n");
                print_ast(node->extra, indent + 2);
            }
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_CASE_ARM: {
            printf("[Case Arm]\n");
            print_indent(indent + 1);
            printf("Labels:\n");
            for (ASTNode *label = node->left; label; label = label->next) {
                print_ast(label, indent + 2);
            }
            print_indent(indent + 1);
            printf("Statement:\n");
            print_ast(node->right, indent + 2);
            if (node->next) {
                print_ast(node->next, indent); // the next arm, same indent
            }
            break;
        }

        case NODE_CASE_RANGE:
            printf("[Case Range]\n");
            print_indent(indent + 1);
            printf("Low:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("High:\n");
            print_ast(node->right, indent + 2);
            break;

        case NODE_VAR_REF:
            printf("[Var Ref] -> Variable: %s\n", sym_table[node->data.var_idx].name);
            break;

        case NODE_LOCAL_VAR_REF:
            printf("[Local Var Ref] slot %d\n", node->data.var_idx);
            break;

        case NODE_VAR_PARAM_READ:
            printf("[Var Param Read] slot %d\n", node->data.var_idx);
            break;

        case NODE_VAR_PARAM_ASSIGN:
            printf("[Var Param Assignment] -> slot %d\n", node->data.var_idx);
            print_indent(indent + 1);
            printf("Value:\n");
            print_ast(node->left, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_SET_CONSTRUCTOR: {
            printf("[Set Constructor]\n");
            int elem_num = 1;
            for (ASTNode *elem = node->left; elem; elem = elem->next) {
                print_indent(indent + 1);
                printf("Elem %d:\n", elem_num++);
                print_ast(elem, indent + 2);
            }
            break;
        }

        case NODE_SET_IN:
            printf("[Set In]\n");
            print_indent(indent + 1);
            printf("Element:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Set:\n");
            print_ast(node->right, indent + 2);
            break;

        case NODE_BREAK:
            printf("[Break]\n");
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_CONTINUE:
            printf("[Continue]\n");
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_RANDOMIZE:
            printf("[Randomize]\n");
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_EXIT:
            printf("[Exit]\n");
            if (node->left) {
                print_indent(indent + 1);
                printf("Value:\n");
                print_ast(node->left, indent + 2);
            }
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_HALT:
            printf("[Halt]\n");
            if (node->left) {
                print_indent(indent + 1);
                printf("Code:\n");
                print_ast(node->left, indent + 2);
            }
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_LABEL:
            printf("[Label] %d:\n", node->data.num_value);
            print_ast(node->left, indent + 1);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_GOTO:
            printf("[Goto] -> %d\n", node->data.num_value);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_CALL: {
            ProcSymbol *proc = &proc_table[node->data.var_idx];
            printf("[Call] -> %s: %s\n", proc->is_function ? "Function" : "Procedure", proc->name);
            int arg_num = 1;
            for (ASTNode *arg = node->left; arg; arg = arg->next) {
                print_indent(indent + 1);
                printf("Arg %d:\n", arg_num++);
                print_ast(arg, indent + 2);
            }
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;
        }

        case NODE_LOCAL_VAR:
            printf("[Local] slot %d\n", node->data.var_idx);
            break;

        case NODE_LOCAL_ASSIGN:
            printf("[Local Assignment] -> slot %d\n", node->data.var_idx);
            print_indent(indent + 1);
            printf("Value:\n");
            print_ast(node->left, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_REF_ARRAY_ACCESS:
            printf("[Array-Ref Access] -> param slot %d\n", node->data.var_idx);
            print_indent(indent + 1);
            printf("Index:\n");
            print_ast(node->left, indent + 2);
            break;

        case NODE_REF_ARRAY_ASSIGN:
            printf("[Array-Ref Assignment] -> param slot %d\n", node->data.var_idx);
            print_indent(indent + 1);
            printf("Index:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Value:\n");
            print_ast(node->right, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_REF_ARRAY_ACCESS_2D:
            printf("[Array-Ref Access 2D] -> param slot %d\n", node->data.var_idx);
            print_indent(indent + 1);
            printf("Index 1:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Index 2:\n");
            print_ast(node->right, indent + 2);
            break;

        case NODE_REF_ARRAY_ASSIGN_2D:
            printf("[Array-Ref Assignment 2D] -> param slot %d\n", node->data.var_idx);
            print_indent(indent + 1);
            printf("Index 1:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Index 2:\n");
            print_ast(node->right, indent + 2);
            print_indent(indent + 1);
            printf("Value:\n");
            print_ast(node->extra, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_REF_ARRAY_ACCESS_ND: {
            printf("[Array-Ref Access ND] -> param slot %d\n", node->data.var_idx);
            int idx_num = 1;
            for (ASTNode *idx = node->left; idx; idx = idx->next) {
                print_indent(indent + 1);
                printf("Index %d:\n", idx_num++);
                print_ast(idx, indent + 2);
            }
            break;
        }

        case NODE_REF_ARRAY_ASSIGN_ND: {
            printf("[Array-Ref Assignment ND] -> param slot %d\n", node->data.var_idx);
            int idx_num = 1;
            for (ASTNode *idx = node->left; idx; idx = idx->next) {
                print_indent(indent + 1);
                printf("Index %d:\n", idx_num++);
                print_ast(idx, indent + 2);
            }
            print_indent(indent + 1);
            printf("Value:\n");
            print_ast(node->right, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;
        }

        case NODE_ARRAY_REF:
            printf("[Array Ref] -> sym_table index %d\n", node->data.var_idx);
            break;

        case NODE_BUILTIN_CALL: {
            printf("[Builtin Call] (op=%d)\n", node->op);
            if (node->left) {
                print_indent(indent + 1);
                printf("Arg 1:\n");
                print_ast(node->left, indent + 2);
            }
            if (node->right) {
                print_indent(indent + 1);
                printf("Arg 2:\n");
                print_ast(node->right, indent + 2);
            }
            if (node->extra) {
                print_indent(indent + 1);
                printf("Arg 3:\n");
                print_ast(node->extra, indent + 2);
            }
            break;
        }

        case NODE_STRING_INDEX:
            printf("[String Index] -> sym_table index %d\n", node->data.var_idx);
            print_indent(indent + 1);
            printf("Index:\n");
            print_ast(node->left, indent + 2);
            break;

        case NODE_LOCAL_STRING_INDEX:
            printf("[Local String Index] -> slot %d\n", node->data.var_idx);
            print_indent(indent + 1);
            printf("Index:\n");
            print_ast(node->left, indent + 2);
            break;

        case NODE_STRING_INDEX_ASSIGN:
            printf("[String Index Assignment] -> sym_table index %d\n", node->data.var_idx);
            print_indent(indent + 1);
            printf("Index:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Value:\n");
            print_ast(node->right, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_LOCAL_STRING_INDEX_ASSIGN:
            printf("[Local String Index Assignment] -> slot %d\n", node->data.var_idx);
            print_indent(indent + 1);
            printf("Index:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Value:\n");
            print_ast(node->right, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_ARRAY_ACCESS_2D:
            printf("[2D Array Access] -> Array: %s\n", sym_table[node->data.var_idx].name);
            print_indent(indent + 1);
            printf("Index 1:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Index 2:\n");
            print_ast(node->right, indent + 2);
            break;

        case NODE_ARRAY_ASSIGN_2D:
            printf("[2D Array Assignment] -> Array: %s\n", sym_table[node->data.var_idx].name);
            print_indent(indent + 1);
            printf("Index 1:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Index 2:\n");
            print_ast(node->right, indent + 2);
            print_indent(indent + 1);
            printf("Value:\n");
            print_ast(node->extra, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_ARRAY_ACCESS_ND: {
            printf("[ND Array Access] -> Array: %s\n", sym_table[node->data.var_idx].name);
            int idx_num = 1;
            for (ASTNode *idx = node->left; idx; idx = idx->next) {
                print_indent(indent + 1);
                printf("Index %d:\n", idx_num++);
                print_ast(idx, indent + 2);
            }
            break;
        }

        case NODE_ARRAY_ASSIGN_ND: {
            printf("[ND Array Assignment] -> Array: %s\n", sym_table[node->data.var_idx].name);
            int idx_num = 1;
            for (ASTNode *idx = node->left; idx; idx = idx->next) {
                print_indent(indent + 1);
                printf("Index %d:\n", idx_num++);
                print_ast(idx, indent + 2);
            }
            print_indent(indent + 1);
            printf("Value:\n");
            print_ast(node->right, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;
        }

        case NODE_ARRAY_RECORD_FIELD_ACCESS:
            printf("[Array-of-Record Field Access] -> Array: %s, Field offset: %d\n",
                   sym_table[node->data.var_idx].name, node->right->data.num_value);
            print_indent(indent + 1);
            printf("Index:\n");
            print_ast(node->left, indent + 2);
            break;

        case NODE_ARRAY_RECORD_FIELD_ASSIGN:
            printf("[Array-of-Record Field Assignment] -> Array: %s, Field offset: %d\n",
                   sym_table[node->data.var_idx].name, node->extra->data.num_value);
            print_indent(indent + 1);
            printf("Index:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Value:\n");
            print_ast(node->right, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_HEAP_FIELD_ACCESS:
            printf("[Heap Field Access] -> Field offset: %d\n", node->right->data.num_value);
            print_indent(indent + 1);
            printf("Pointer:\n");
            print_ast(node->left, indent + 2);
            break;

        case NODE_HEAP_FIELD_ASSIGN:
            printf("[Heap Field Assignment] -> Field offset: %d\n", node->extra->data.num_value);
            print_indent(indent + 1);
            printf("Pointer:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Value:\n");
            print_ast(node->right, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_HEAP_ARRAY_FIELD_ACCESS:
            printf("[Heap Array Field Access] -> Combined offset: %d\n", node->data.num_value);
            print_indent(indent + 1);
            printf("Pointer:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Index:\n");
            print_ast(node->right, indent + 2);
            break;

        case NODE_HEAP_ARRAY_FIELD_ASSIGN:
            printf("[Heap Array Field Assignment] -> Combined offset: %d\n", node->data.num_value);
            print_indent(indent + 1);
            printf("Pointer:\n");
            print_ast(node->left, indent + 2);
            print_indent(indent + 1);
            printf("Index:\n");
            print_ast(node->extra, indent + 2);
            print_indent(indent + 1);
            printf("Value:\n");
            print_ast(node->right, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_HEAP_ALLOC:
            printf("[Heap Alloc] -> Element size: %d\n", node->data.num_value);
            break;

        case NODE_HEAP_DISPOSE:
            printf("[Heap Dispose] -> Element size: %d\n", node->data.num_value);
            print_indent(indent + 1);
            printf("Pointer:\n");
            print_ast(node->left, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_PROC_REF: {
            ProcSymbol *proc = &proc_table[node->data.var_idx];
            printf("[Proc Ref] -> %s: %s\n", proc->is_function ? "Function" : "Procedure", proc->name);
            break;
        }

        case NODE_CALL_INDIRECT: {
            printf("[Call Indirect] -> slot %d (levels_up %d)\n", node->data.var_idx, (int)node->op);
            int arg_num = 1;
            for (ASTNode *arg = node->left; arg; arg = arg->next) {
                print_indent(indent + 1);
                printf("Arg %d:\n", arg_num++);
                print_ast(arg, indent + 2);
            }
            if (node->extra->data.num_value && node->next) {
                print_ast(node->next, indent);
            }
            break;
        }

        case NODE_VIRTUAL_CALL: {
            printf("[Virtual Call] -> slot %d\n", node->data.num_value);
            print_indent(indent + 1);
            printf("Self:\n");
            print_ast(node->left, indent + 2);
            int arg_num = 1;
            for (ASTNode *arg = node->right; arg; arg = arg->next) {
                print_indent(indent + 1);
                printf("Arg %d:\n", arg_num++);
                print_ast(arg, indent + 2);
            }
            if (node->op == TOKEN_PROCEDURE && node->next) {
                print_ast(node->next, indent);
            }
            break;
        }

        case NODE_INHERITED_CALL: {
            ProcSymbol *proc = &proc_table[node->data.var_idx];
            printf("[Inherited Call] -> %s: %s\n", proc->is_function ? "Function" : "Procedure", proc->name);
            print_indent(indent + 1);
            printf("Self:\n");
            print_ast(node->left, indent + 2);
            int arg_num = 1;
            for (ASTNode *arg = node->right; arg; arg = arg->next) {
                print_indent(indent + 1);
                printf("Arg %d:\n", arg_num++);
                print_ast(arg, indent + 2);
            }
            if (node->op == TOKEN_PROCEDURE && node->next) {
                print_ast(node->next, indent);
            }
            break;
        }

        case NODE_VTABLE_INIT_ENTRY: {
            printf("[Vtable Init Entry] -> vm_vtables[%d]\n", node->data.num_value);
            print_ast(node->left, indent + 1);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;
        }

        case NODE_PROCVAR_CALL: {
            printf("[Procvar Call]\n");
            print_indent(indent + 1);
            printf("Callee:\n");
            print_ast(node->right, indent + 2);
            int arg_num = 1;
            for (ASTNode *arg = node->left; arg; arg = arg->next) {
                print_indent(indent + 1);
                printf("Arg %d:\n", arg_num++);
                print_ast(arg, indent + 2);
            }
            if (node->extra->data.num_value && node->next) {
                print_ast(node->next, indent);
            }
            break;
        }
    }
}

