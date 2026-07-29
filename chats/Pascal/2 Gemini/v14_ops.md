v14_ops.md

# Prompt

What's next

# Response

Now that writeln and readln are fully integrated, we have a complete end-to-end pipeline with standard I/O! We can write real, interactive Pascal programs and verify both compile-time optimizations and runtime behavior seamlessly.
Here are the logical next steps to advance the compiler:
🟢 Option 1: Control Flow Constructs (if/then/else and while loops)
This is the single biggest milestone for making our custom VM Turing-complete.
• Lexer/Parser: Add keywords if, then, else, while, do.
• VM Opcodes: Introduce unconditional and conditional branch instructions (OP_JUMP, OP_JUMP_IF_FALSE).
• Codegen: Implement target label backpatching to skip code blocks based on condition evaluation.
🟡 Option 2: Extended Operators & Logic Functions
Build out the primitive expression engine to make conditions and calculations more flexible.
• Boolean Logic: and, or, not.
• Comparison Operators: <= (Less or Equal), >= (Greater or Equal), <> (Not Equal).
• Unary Minus: Support negative numbers (e.g., x := -5).
🔵 Option 3: Standard Output Formatting
Enhance writeln and runtime display:
• Boolean Formatting: Have writeln(is_greater) print "true" / "false" in the VM instead of raw 1 / 0 integer flags by checking symbol table metadata.
• Multiple Arguments: Allow writeln(x, y, z); or printing string literals like writeln('Value: ', x).
Which direction would you like to tackle first?

# Prompt

I’d like to exhaust all the functionality relating to operations of integers and booleans first before expanding into more control flow and such. So let’s focus on option 2: boolean logic, all comparison operators, and unary minus for numbers.

# Response

That is a fantastic plan. Fleshing out the primitive types and operators completely ensures we have a rock-solid expression evaluation engine before we introduce the complexity of jumps and control flow.
To support full boolean logic (and, or, not), the remaining comparison operators (<=, >=, <>), and unary minus (-), we need to update several layers of our compiler pipeline.
Here are the precise modifications to weave these into your existing files.
1️⃣ Update common.h
We need to expand our tokens, opcodes, and AST nodes.
// 1. Add to TokenType enum (before TOKEN_SEMI):
    TOKEN_AND, TOKEN_OR, TOKEN_NOT,
    TOKEN_LTE, TOKEN_GTE, TOKEN_NEQ,

// 2. Add to Opcode enum (before OP_HALT):
    OP_AND, OP_OR, OP_NOT,
    OP_LTE, OP_GTE, OP_NEQ,
    OP_NEG,

// 3. Add to NodeType enum:
    NODE_UNARY_OP,

2️⃣ Update lexer.c
We must recognize the new keywords and parse the multi-character relational operators.
// 1. In next_token(), inside the isalpha() block, add these keywords:
        else if (strcasecmp(token.text, "and") == 0) token.type = TOKEN_AND;
        else if (strcasecmp(token.text, "or") == 0) token.type = TOKEN_OR;
        else if (strcasecmp(token.text, "not") == 0) token.type = TOKEN_NOT;

// 2. In next_token(), replace the '<' and '>' cases in the switch statement with:
        case '<': 
            if (*src == '=') { token.type = TOKEN_LTE; strcpy(token.text, "<="); src++; }
            else if (*src == '>') { token.type = TOKEN_NEQ; strcpy(token.text, "<>"); src++; }
            else { token.type = TOKEN_LT; }
            break;
        case '>': 
            if (*src == '=') { token.type = TOKEN_GTE; strcpy(token.text, ">="); src++; }
            else { token.type = TOKEN_GT; }
            break;

3️⃣ Update parser.c
Pascal has strict precedence. not is a unary operator (highest), and groups with multiplication, or groups with addition, and relational operators remain at the bottom.
// 1. Update factor() to handle unary minus and not:
static ASTNode *factor(void) {
    if (token.type == TOKEN_MINUS || token.type == TOKEN_NOT) {
        TokenType op = token.type;
        match(op);
        ASTNode *node = create_node(NODE_UNARY_OP);
        node->op = op;
        node->left = factor();
        return node;
    }
    if (token.type == TOKEN_LPAREN) {
        match(TOKEN_LPAREN);
        ASTNode *node = expression();
        match(TOKEN_RPAREN);
        return node;
    }
    // ... rest of factor() (numbers, booleans, variables) remains exactly the same
}

// 2. Update term() to include TOKEN_AND:
static ASTNode *term(void) {
    ASTNode *node = factor();
    while (token.type == TOKEN_MUL || token.type == TOKEN_DIV || token.type == TOKEN_AND) {
        // ... same block as before ...
    }
    return node;
}

// 3. Update arithmetic_expression() to include TOKEN_OR:
static ASTNode *arithmetic_expression(void) {
    ASTNode *node = term();
    while (token.type == TOKEN_PLUS || token.type == TOKEN_MINUS || token.type == TOKEN_OR) {
        // ... same block as before ...
    }
    return node;
}

// 4. Update expression() to include LTE, GTE, and NEQ:
static ASTNode *expression(void) {
    ASTNode *node = arithmetic_expression();
    while (token.type == TOKEN_EQ || token.type == TOKEN_LT || token.type == TOKEN_GT ||
           token.type == TOKEN_LTE || token.type == TOKEN_GTE || token.type == TOKEN_NEQ) {
        // ... same block as before ...
    }
    return node;
}

4️⃣ Update type_checker.c
Enforce that logical operators only accept booleans, and relational/arithmetic operators only accept integers.
// Add inside the switch (node->type) block:
        case NODE_UNARY_OP:
            if (node->op == TOKEN_MINUS) {
                if (node->left->expression_type != TYPE_INTEGER) {
                    fprintf(stderr, "%s:%d: Type Error: Unary minus requires integer\n", get_current_filename(), node->line);
                    exit(1);
                }
                node->expression_type = TYPE_INTEGER;
            } else if (node->op == TOKEN_NOT) {
                if (node->left->expression_type != TYPE_BOOLEAN) {
                    fprintf(stderr, "%s:%d: Type Error: 'not' requires boolean\n", get_current_filename(), node->line);
                    exit(1);
                }
                node->expression_type = TYPE_BOOLEAN;
            }
            break;

// Inside the NODE_BINARY_OP case, update the type validations:
            if (node->op == TOKEN_AND || node->op == TOKEN_OR) {
                if (left_t != TYPE_BOOLEAN || right_t != TYPE_BOOLEAN) {
                    fprintf(stderr, "%s:%d: Type Error: Logical operators require boolean operands\n", get_current_filename(), node->line);
                    exit(1);
                }
                node->expression_type = TYPE_BOOLEAN;
            } else if (node->op == TOKEN_PLUS || node->op == TOKEN_MINUS || node->op == TOKEN_MUL || node->op == TOKEN_DIV) {
                if (left_t != TYPE_INTEGER || right_t != TYPE_INTEGER) {
                    fprintf(stderr, "%s:%d: Type Error: Arithmetic operations require integer operands\n", get_current_filename(), node->line);
                    exit(1);
                }
                node->expression_type = TYPE_INTEGER;
            } else {
                // Relational operators (=, <, >, <=, >=, <>)
                if (left_t != TYPE_INTEGER || right_t != TYPE_INTEGER) {
                    fprintf(stderr, "%s:%d: Type Error: Comparisons require integer operands\n", get_current_filename(), node->line);
                    exit(1);
                }
                node->expression_type = TYPE_BOOLEAN;
            }

5️⃣ Update optimizer.c
Enable constant folding for all the new operators to trim down runtime overhead.
// Inside optimize_ast(), add Unary folding before Binary folding:
    if (node->type == NODE_UNARY_OP && (node->left->type == NODE_NUMBER || node->left->type == NODE_BOOLEAN)) {
        int val = node->left->data.num_value;
        if (node->op == TOKEN_MINUS) {
            node->type = NODE_NUMBER;
            node->data.num_value = -val;
        } else if (node->op == TOKEN_NOT) {
            node->type = NODE_BOOLEAN;
            node->data.num_value = !val;
        }
        free_ast(node->left);
        node->left = NULL;
        return node;
    }

// In the NODE_BINARY_OP folding switch statement, add the new cases:
            case TOKEN_AND: folded_val = (l_val && r_val); is_comparison = 1; break;
            case TOKEN_OR:  folded_val = (l_val || r_val); is_comparison = 1; break;
            case TOKEN_LTE: folded_val = (l_val <= r_val); is_comparison = 1; break;
            case TOKEN_GTE: folded_val = (l_val >= r_val); is_comparison = 1; break;
            case TOKEN_NEQ: folded_val = (l_val != r_val); is_comparison = 1; break;

6️⃣ Update codegen.c
Translate the new AST nodes directly to their VM opcodes.
// Add inside the switch (node->type) block:
        case NODE_UNARY_OP:
            generate_code(node->left);
            if (node->op == TOKEN_MINUS) emit(OP_NEG, 0);
            else if (node->op == TOKEN_NOT) emit(OP_NOT, 0);
            break;

// In the NODE_BINARY_OP switch, append the new cases:
                case TOKEN_AND: emit(OP_AND, 0); break;
                case TOKEN_OR:  emit(OP_OR, 0); break;
                case TOKEN_LTE: emit(OP_LTE, 0); break;
                case TOKEN_GTE: emit(OP_GTE, 0); break;
                case TOKEN_NEQ: emit(OP_NEQ, 0); break;

7️⃣ Update vm.c
Teach the virtual machine how to execute the new math and logic instructions.
// Add inside the run_vm() switch block:
            case OP_NEG:   vm_stack[sp] = -vm_stack[sp]; break;
            case OP_NOT:   vm_stack[sp] = !vm_stack[sp]; break;
            
            case OP_AND:   vm_stack[sp - 1] = (vm_stack[sp - 1] && vm_stack[sp]); sp--; break;
            case OP_OR:    vm_stack[sp - 1] = (vm_stack[sp - 1] || vm_stack[sp]); sp--; break;
            
            case OP_LTE:   vm_stack[sp - 1] = (vm_stack[sp - 1] <= vm_stack[sp]); sp--; break;
            case OP_GTE:   vm_stack[sp - 1] = (vm_stack[sp - 1] >= vm_stack[sp]); sp--; break;
            case OP_NEQ:   vm_stack[sp - 1] = (vm_stack[sp - 1] != vm_stack[sp]); sp--; break;

8️⃣ Update ast_printer.c
Finally, map the new tokens so the -c compilation flag visualizes them correctly.
// Inside token_type_to_str():
        case TOKEN_AND:   return "and";
        case TOKEN_OR:    return "or";
        case TOKEN_NOT:   return "not";
        case TOKEN_LTE:   return "<=";
        case TOKEN_GTE:   return ">=";
        case TOKEN_NEQ:   return "<>";

// Inside print_ast() switch block, handle NODE_UNARY_OP:
        case NODE_UNARY_OP:
            printf("[Unary Op] '%s'\n", token_type_to_str(node->op));
            print_indent(indent + 1);
            printf("Operand:\n");
            print_ast(node->left, indent + 2);
            break;


# Prompt

Make a test pascal file for all new operators

# Response

Here is a comprehensive test program (test_operators.pas) that exercises every single new operator—unary minus, not, logical and/or, and all comparisons (<=, >=, <>)—along with expressions testing Pascal's precedence rules and constant folding/runtime evaluation:
program TestOperators;
var
    a, b, neg_val : integer;
    res_lte, res_gte, res_neq : boolean;
    bool1, bool2, bool_res : boolean;
begin
    { --- 1. Unary Operations --- }
    a := 15;
    neg_val := -a;          { Unary minus: neg_val = -15 }
    writeln(neg_val);

    bool1 := true;
    bool2 := not bool1;     { Unary not: bool2 = false }
    writeln(bool2);

    { --- 2. Relational Operators (<=, >=, <>) --- }
    b := 20;
    res_lte := a <= b;      { 15 <= 20 -> true }
    res_gte := a >= 20;     { 15 >= 20 -> false }
    res_neq := a <> b;      { 15 <> 20 -> true }

    writeln(res_lte);
    writeln(res_gte);
    writeln(res_neq);

    { --- 3. Boolean Logic (and, or) --- }
    bool_res := bool1 and (a < b);   { true and true -> true }
    writeln(bool_res);

    bool_res := bool2 or (a > b);    { false or false -> false }
    writeln(bool_res);

    { --- 4. Precedence & Complex Expressions --- }
    { Demonstrates that 'not' and comparisons bind correctly in combined logic }
    bool_res := not (a + neg_val <> 0) and (b >= 20); { not false and true -> true }
    writeln(bool_res);
end.

Expected Execution Output
When you compile and run this with your VM, you should get:
-15
0
1
0
1
1
0
1

(Note: 1 represents true and 0 represents false at the VM runtime stack level unless custom string formatting is added to writeln!)


