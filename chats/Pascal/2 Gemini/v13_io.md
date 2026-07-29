v13_io.md

# Prompt

Since it's little tricky to test new functionality just from the ast-print and how the optimizer and DCE can confuse, I think it's time to add WriteLn to the project. And why to add the readln as well. It should make it easier to write test programs.

# Response

That makes total sense. Having native writeln and readln I/O procedures will let us write self-contained Pascal test programs, make assertion testing much easier, and prevent DCE from pruning variables since printing them counts as an active read/usage!
Adding I/O procedures requires updating four main areas across our modules:
1. Lexer: Add keywords writeln and readln.
2. Parser & AST: Support statement nodes for NODE_WRITELN and NODE_READLN.
3. VM Bytecode: Add instructions (OP_PRINT, OP_READ) to perform printf and scanf.
4. Code Generator & DCE: Emit the new I/O instructions and ensure DCE marks printed/read variables as used.
Here are the updates to integrate writeln and readln into your modular codebase:
1️⃣ Updated common.h
Add new tokens, opcodes, and AST node types:
// Add to TokenType enum:
TOKEN_WRITELN, TOKEN_READLN,

// Add to Opcode enum:
OP_PRINT, OP_READ,

// Add to NodeType enum:
NODE_WRITELN, NODE_READLN,

2️⃣ Updated lexer.c
Add writeln and readln keyword checks:
// In next_token(), under the identifier check:
else if (strcasecmp(token.text, "writeln") == 0) token.type = TOKEN_WRITELN;
else if (strcasecmp(token.text, "readln") == 0) token.type = TOKEN_READLN;

3️⃣ Updated parser.c
Add parsing for writeln(expr) and readln(var) statements inside statement list parsing:
// Add parsing for writeln and readln inside statement loop in parse_ast():
while (token.type == TOKEN_IDENTIFIER || token.type == TOKEN_WRITELN || token.type == TOKEN_READLN) {
    ASTNode *stmt = NULL;

    if (token.type == TOKEN_IDENTIFIER) {
        stmt = create_node(NODE_ASSIGN);
        stmt->data.var_idx = find_var(token.text);
        match(TOKEN_IDENTIFIER);
        match(TOKEN_ASSIGN);
        stmt->left = expression();
        match(TOKEN_SEMI);
    } 
    else if (token.type == TOKEN_WRITELN) {
        match(TOKEN_WRITELN);
        match(TOKEN_LPAREN); // Note: Ensure TOKEN_LPAREN '(' and TOKEN_RPAREN ')' exist in lexer
        stmt = create_node(NODE_WRITELN);
        stmt->left = expression();
        match(TOKEN_RPAREN);
        match(TOKEN_SEMI);
    } 
    else if (token.type == TOKEN_READLN) {
        match(TOKEN_READLN);
        match(TOKEN_LPAREN);
        stmt = create_node(NODE_READLN);
        if (token.type == TOKEN_IDENTIFIER) {
            stmt->data.var_idx = find_var(token.text);
            match(TOKEN_IDENTIFIER);
        } else {
            compile_error(token.line, "readln expects a variable identifier");
        }
        match(TOKEN_RPAREN);
        match(TOKEN_SEMI);
    }

    if (!root->left) root->left = stmt;
    else current->next = stmt;
    current = stmt;
}

4️⃣ Updated type_checker.c
Ensure expressions inside writeln are valid and readln targets a valid variable:
void type_check(ASTNode *node) {
    if (!node) return;

    type_check(node->left);
    type_check(node->right);
    type_check(node->next);

    switch (node->type) {
        // ... existing cases ...

        case NODE_WRITELN:
            if (node->left->expression_type == TYPE_UNKNOWN) {
                fprintf(stderr, "%s:%d: Type Error: Cannot print invalid expression\n",
                        get_current_filename(), node->line);
                exit(1);
            }
            break;

        case NODE_READLN:
            // Ensures target is a defined variable
            if (node->data.var_idx < 0 || node->data.var_idx >= sym_count) {
                fprintf(stderr, "%s:%d: Type Error: Invalid read target\n",
                        get_current_filename(), node->line);
                exit(1);
            }
            break;

        default:
            break;
    }
}

5️⃣ Updated optimizer.c (DCE Awareness)
writeln(x) references x as a NODE_VARIABLE in node->left, so mark_used_variables() will automatically mark x as used! This keeps DCE active while ensuring printed variables aren't eliminated.
6️⃣ Updated codegen.c
Emit code for OP_PRINT and OP_READ:
void generate_code(ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        // ... existing cases ...

        case NODE_WRITELN:
            generate_code(node->left); // Evaluates expression onto VM stack
            emit(OP_PRINT, 0);
            generate_code(node->next);
            break;

        case NODE_READLN:
            emit(OP_READ, node->data.var_idx); // Reads stdin into var_idx
            generate_code(node->next);
            break;
    }
}

7️⃣ Updated vm.c
Add handling for OP_PRINT and OP_READ:
case OP_PRINT: {
    int val = vm_stack[sp--];
    printf("%d\n", val);
    break;
}

case OP_READ: {
    int input_val;
    printf("> "); // Prompt user
    if (scanf("%d", &input_val) != 1) {
        fprintf(stderr, "VM Runtime Error: Invalid integer input\n");
        exit(1);
    }
    vm_vars[instr.arg] = input_val;
    break;
}

🧪 Updated test.pas Example
Now you can write test programs like this:
program TestIO;
var
    x, y, is_greater : integer;
begin
    writeln(100);          { Prints 100 directly }
    
    x := 10;
    y := 20;
    writeln(x + y);        { Prints 30 }

    is_greater := y > x;
    writeln(is_greater);   { Prints 1 (true) }
end.

How would you like to handle writeln for booleans—should the VM print 1/0 or display formatted text like true/false based on symbol data types?

# Prompt

src/parser.c:182:19: error: use of undeclared identifier
      'TOKEN_LPAREN'
  182 |             match(TOKEN_LPAREN); // Note: Ensure TOKEN_LPAREN '(' and TOKE...
      |                   ^~~~~~~~~~~~
src/parser.c:185:19: error: use of undeclared identifier
      'TOKEN_RPAREN'; did you mean 'TOKEN_READLN'?

# Response

That was a quick note left in the parser code snippet catching up to us! TOKEN_LPAREN and TOKEN_RPAREN need to be explicitly registered in common.h and handled in lexer.c.
Here are the precise updates to add parentheses support.
1️⃣ Update common.h
Add TOKEN_LPAREN and TOKEN_RPAREN to the TokenType enum:
typedef enum {
    TOKEN_PROGRAM, TOKEN_VAR, TOKEN_BEGIN, TOKEN_END,
    TOKEN_INTEGER, TOKEN_BOOLEAN,
    TOKEN_IDENTIFIER, TOKEN_NUMBER, TOKEN_TRUE, TOKEN_FALSE,
    TOKEN_ASSIGN, TOKEN_PLUS, TOKEN_MINUS, TOKEN_MUL, TOKEN_DIV,
    TOKEN_EQ, TOKEN_LT, TOKEN_GT,
    TOKEN_SEMI, TOKEN_COLON, TOKEN_COMMA, TOKEN_PERIOD,
    TOKEN_LPAREN, TOKEN_RPAREN, // <--- Add parenthesis tokens here
    TOKEN_WRITELN, TOKEN_READLN,
    TOKEN_EOF
} TokenType;

2️⃣ Update lexer.c
Add ( and ) character handlers inside the switch statement in next_token():
    token.text[0] = *src;
    token.text[1] = '\0';
    switch (*src++) {
        case '+': token.type = TOKEN_PLUS; break;
        case '-': token.type = TOKEN_MINUS; break;
        case '*': token.type = TOKEN_MUL; break;
        case '/': token.type = TOKEN_DIV; break;
        case '=': token.type = TOKEN_EQ; break;
        case '<': token.type = TOKEN_LT; break;
        case '>': token.type = TOKEN_GT; break;
        case ';': token.type = TOKEN_SEMI; break;
        case ':': token.type = TOKEN_COLON; break;
        case ',': token.type = TOKEN_COMMA; break;
        case '.': token.type = TOKEN_PERIOD; break;
        case '(': token.type = TOKEN_LPAREN; break; // <--- Add '('
        case ')': token.type = TOKEN_RPAREN; break; // <--- Add ')'
        default:  token.type = TOKEN_EOF; break;
    }

💡 Bonus: Supporting Grouped Expressions (a + b) * c
Now that we have parentheses tokens in the lexer and parser, you can also update factor() in parser.c to support nested expression grouping like (x + 5) * 2:
static ASTNode *factor(void) {
    if (token.type == TOKEN_LPAREN) {
        match(TOKEN_LPAREN);
        ASTNode *node = expression();
        match(TOKEN_RPAREN);
        return node;
    }
    
    // ... rest of factor() logic (numbers, booleans, identifiers) ...
}

Once you add TOKEN_LPAREN and TOKEN_RPAREN to common.h and lexer.c, make will build cleanly!

# Prompt

New nodes aren’t handled in ast_printer:
src/ast_printer.c:28:13: warning: enumeration values
      'NODE_WRITELN' and 'NODE_READLN' not handled in switch [-Wswitch]
   28 |     switch (node->type) {
      |             ^~~~~~~~~~

# Response

That warning happens because GCC/Clang strictly checks switch (node->type) against every enum value in NodeType. Since NODE_WRITELN and NODE_READLN were newly added, ast_printer.c needs explicit cases to render them.
Here is the update for ast_printer.c to handle both statements and recursively print their child nodes or variable bindings.
🛠️ Fix in ast_printer.c
Add NODE_WRITELN and NODE_READLN cases to print_ast():
void print_ast(ASTNode *node, int indent) {
    if (!node) return;

    print_indent(indent);

    switch (node->type) {
        case NODE_COMPOUND:
            printf("[Compound Statement]\n");
            print_ast(node->left, indent + 1);
            break;

        case NODE_ASSIGN:
            printf("[Assignment] -> Variable: %s\n", sym_table[node->data.var_idx].name);
            print_indent(indent + 1);
            printf("Value:\n");
            print_ast(node->left, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_WRITELN:
            printf("[WriteLn]\n");
            print_indent(indent + 1);
            printf("Expression:\n");
            print_ast(node->left, indent + 2);
            if (node->next) {
                print_ast(node->next, indent);
            }
            break;

        case NODE_READLN:
            printf("[ReadLn] -> Target Variable: %s\n", sym_table[node->data.var_idx].name);
            if (node->next) {
                print_ast(node->next, indent);
            }
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
            printf("[Number] %d\n", node->data.num_value);
            break;

        case NODE_BOOLEAN:
            printf("[Boolean] %s\n", node->data.num_value ? "true" : "false");
            break;

        case NODE_VARIABLE:
            printf("[Variable] %s\n", sym_table[node->data.var_idx].name);
            break;
    }
}

Once updated, the -Wswitch warnings will disappear, and your AST visualizer will clearly show [WriteLn] and [ReadLn] nodes alongside assignments and expressions!

# Prompt

Now when I build the pas file TestIO, I get this error:
--- Phase 1: Parsing AST ---

--- Phase 2: Type Checking ---
test/v13_testIO.pas:11: Type Error: Cannot assign expression to variable 'is_greater'

# Response

