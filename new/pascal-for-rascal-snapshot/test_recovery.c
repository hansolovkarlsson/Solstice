// Simulates an embedding host (a REPL, a GUI "Compile" button, etc.) that
// calls into the Pascal compiler repeatedly within a single
// process. Before this fix, any fatal error anywhere in the pipeline called
// exit(1) directly and would have taken the whole host process down with it.
#include <stdio.h>
#include <stdlib.h>
#include "compiler.h"
#include "error.h"

static int compile_one(const char *label, const char *source) {
    if (setjmp(fatal_error_env)) {
        printf("[%s] compile FAILED (recovered - process still alive)\n", label);
        return 0;
    }
    fatal_error_active = 1;

    ASTNode *ast = parse_ast(source, label);
    type_check(ast);
    ast = optimize_ast(ast);
    ast = eliminate_dead_code(ast);
    generate_program(ast);
    emit_halt();
    printf("[%s] compile OK (%d instructions, %d symbols)\n", label, code_idx, sym_count);
    free_ast(ast);

    fatal_error_active = 0;
    return 1;
}

int main(void) {
    const char *broken =
        "program Broken;\n"
        "var x: integer;\n"
        "begin\n"
        "    x := true;\n"  // type error: assigning boolean to integer
        "end.\n";

    const char *good1 =
        "program Good1;\n"
        "var a, b: integer;\n"
        "begin\n"
        "    a := 10;\n"
        "    b := a + 5;\n"
        "    writeln(b);\n"
        "end.\n";

    const char *good2 =
        "program Good2;\n"
        "var flag: boolean;\n"
        "begin\n"
        "    flag := (3 < 5) and true;\n"
        "    writeln(flag);\n"
        "end.\n";

    printf("=== Simulating 3 compiles in one long-lived process ===\n\n");

    int r1 = compile_one("attempt-1-broken", broken);
    int r2 = compile_one("attempt-2-good", good1);
    int r3 = compile_one("attempt-3-good", good2);

    printf("\n=== Process reached the end normally. ===\n");
    printf("Results: broken=%s, good1=%s, good2=%s\n",
           r1 ? "unexpectedly OK" : "failed as expected",
           r2 ? "OK" : "unexpectedly failed",
           r3 ? "OK" : "unexpectedly failed");

    if (r1 == 0 && r2 == 1 && r3 == 1) {
        printf("\nSUCCESS: a fatal compile error did not kill the process,\n"
               "and the compiler's global state was correctly reset between calls.\n");
        return 0;
    }
    printf("\nFAILURE: recovery did not behave as expected.\n");
    return 1;
}
