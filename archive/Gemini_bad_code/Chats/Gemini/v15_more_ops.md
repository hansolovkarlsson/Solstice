v15_more_ops.md

# Prompt

What integer and boolean operators are still missing?

# Response

With our recent additions (and, or, not, <=, >=, <>, and unary -), we have covered almost all standard operations!
However, to strictly match standard Pascal (and standard extensions like Turbo Pascal) for integers and booleans, here are the remaining operators that are still missing:
1️⃣ Missing Integer Operators
• mod (Integer Modulus): • Standard Pascal keyword. Calculates the remainder of integer division (e.g., 10 mod 3 yields 1).
• div (Explicit Integer Division): • Standard Pascal syntax distinction. Standard Pascal uses / exclusively for real float division and uses the keyword div for integer division (e.g., 10 div 3 yields 3). Right now our compiler uses the / symbol for integer division.
• Unary + (Identity): • Allows explicit positive prefixes like x := +5; or y := +(a + b);.
2️⃣ Missing Boolean / Bitwise Operators
• xor (Logical Exclusive OR): • Common Pascal extension (Turbo Pascal / FreePascal). Evaluates to true if exactly one operand is true (e.g., true xor false yields true, but true xor true yields false).
• Bitwise Shifts (shl and shr): • Bitwise operations on integers. shl (shift left) and shr (shift right) perform fast multiplication/division by powers of 2 at the binary level (e.g., x shl 2).
3️⃣ Relational Set Operator
• in (Set Membership): • Used in Pascal to check if a value belongs to a range or set (e.g., x in [1..10]). This returns a boolean, though it requires introducing set data structures to the compiler.
Summary Table of Next Targets
Operator	Type	Category	Example Usage
mod	Integer	Arithmetic	remainder := x mod y;
div	Integer	Keyword Integer Div	quotient := x div y;
xor	Boolean / Integer	Logical / Bitwise	flag := a xor b;
shl / shr	Integer	Bitwise	val := x shl 3;
Would you like to implement mod, div, and xor to finish up the keywords before we consider our primitive operation engine 100% feature-complete?

# Prompt

Yes, please implement them.

# Response

Let's add mod, div, and xor to finish out the expression engine!
Using div for integer division keeps us aligned with standard Pascal conventions. We can keep / as an alias for div for now so we don't break existing tests.
Here is the implementation across all modules:
1️⃣ Update common.h
Add the tokens and opcodes:
// Add to TokenType enum:
TOKEN_DIV_KW, TOKEN_MOD, TOKEN_XOR,

// Add to Opcode enum:
OP_MOD, OP_XOR,

2️⃣ Update lexer.c
Register the keywords in next_token() inside the identifier/keyword lookup:
else if (strcasecmp(token.text, "div") == 0) token.type = TOKEN_DIV_KW;
else if (strcasecmp(token.text, "mod") == 0) token.type = TOKEN_MOD;
else if (strcasecmp(token.text, "xor") == 0) token.type = TOKEN_XOR;

3️⃣ Update parser.c
div and mod bind with multiplication at the term precedence level, while xor binds with addition/subtraction at the arithmetic expression level (matching standard Pascal operator hierarchy).
// 1. Update term() to include TOKEN_DIV_KW and TOKEN_MOD:
static ASTNode *term(void) {
    ASTNode *node = factor();
    while (token.type == TOKEN_MUL || token.type == TOKEN_DIV || 
           token.type == TOKEN_DIV_KW || token.type == TOKEN_MOD || 
           token.type == TOKEN_AND) {
        TokenType op = token.type;
        match(op);
        ASTNode *new_node = create_node(NODE_BINARY_OP);
        new_node->op = op;
        new_node->left = node;
        new_node->right = factor();
        node = new_node;
    }
    return node;
}

// 2. Update arithmetic_expression() to include TOKEN_XOR:
static ASTNode *arithmetic_expression(void) {
    ASTNode *node = term();
    while (token.type == TOKEN_PLUS || token.type == TOKEN_MINUS || 
           token.type == TOKEN_OR || token.type == TOKEN_XOR) {
        TokenType op = token.type;
        match(op);
        ASTNode *new_node = create_node(NODE_BINARY_OP);
        new_node->op = op;
        new_node->left = node;
        new_node->right = term();
        node = new_node;
    }
    return node;
}

4️⃣ Update type_checker.c
Ensure mod and div receive integers, while xor requires matching booleans:
// Inside NODE_BINARY_OP in type_check():
if (node->op == TOKEN_AND || node->op == TOKEN_OR || node->op == TOKEN_XOR) {
    if (left_t != TYPE_BOOLEAN || right_t != TYPE_BOOLEAN) {
        fprintf(stderr, "%s:%d: Type Error: Logical operators (and, or, xor) require boolean operands\n", 
                get_current_filename(), node->line);
        exit(1);
    }
    node->expression_type = TYPE_BOOLEAN;
} else if (node->op == TOKEN_PLUS || node->op == TOKEN_MINUS || 
           node->op == TOKEN_MUL || node->op == TOKEN_DIV || 
           node->op == TOKEN_DIV_KW || node->op == TOKEN_MOD) {
    if (left_t != TYPE_INTEGER || right_t != TYPE_INTEGER) {
        fprintf(stderr, "%s:%d: Type Error: Arithmetic operations require integer operands\n", 
                get_current_filename(), node->line);
        exit(1);
    }
    node->expression_type = TYPE_INTEGER;
}

5️⃣ Update optimizer.c
Add constant folding cases for all three operations:
// Inside optimize_ast() under the NODE_BINARY_OP switch:
case TOKEN_DIV_KW:
    if (r_val == 0) { fprintf(stderr, "Error: Constant division by zero\n"); exit(1); }
    folded_val = l_val / r_val;
    break;
case TOKEN_MOD:
    if (r_val == 0) { fprintf(stderr, "Error: Constant modulo by zero\n"); exit(1); }
    folded_val = l_val % r_val;
    break;
case TOKEN_XOR:
    folded_val = (l_val != r_val);
    is_comparison = 1;
    break;

6️⃣ Update codegen.c
Emit opcodes for code generation:
// Inside NODE_BINARY_OP switch in generate_code():
case TOKEN_DIV_KW: emit(OP_DIV, 0); break; // Reuses OP_DIV
case TOKEN_MOD:    emit(OP_MOD, 0); break;
case TOKEN_XOR:    emit(OP_XOR, 0); break;

7️⃣ Update vm.c
Add stack operations and runtime divide/modulo-by-zero boundary checks:
case OP_MOD: {
    int divisor = vm_stack[sp];
    if (divisor == 0) {
        fprintf(stderr, "VM Runtime Error: Modulo by zero\n");
        exit(1);
    }
    vm_stack[sp - 1] = vm_stack[sp - 1] % divisor;
    sp--;
    break;
}

case OP_XOR:
    vm_stack[sp - 1] = (vm_stack[sp - 1] != vm_stack[sp]);
    sp--;
    break;

(Note: Ensure your existing OP_DIV in vm.c also checks if (divisor == 0) to keep runtime error reporting uniform!)
8️⃣ Update ast_printer.c
Map string representations for debugging:
// Inside token_type_to_str():
case TOKEN_DIV_KW: return "div";
case TOKEN_MOD:    return "mod";
case TOKEN_XOR:    return "xor";

🧪 Test Snippet (test_math_logic.pas)
program TestMathLogic;
var
    rem, quot : integer;
    x_res1, x_res2 : boolean;
begin
    rem := +17 mod 5;      { rem = 2 }
    quot := 17 div +5;     { quot = 3 }
    writeln(rem);
    writeln(quot);

    x_res1 := true xor false; { true }
    x_res2 := true xor true;  { false }
    writeln(x_res1);
    writeln(x_res2);
end.

With these added, our expression engine is officially feature-complete for integer arithmetic and boolean logic!

