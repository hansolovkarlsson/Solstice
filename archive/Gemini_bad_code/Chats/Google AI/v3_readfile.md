step3_readfile.md

# Prompt

Add support for reading source code from a file instead of hardcoded string

# Response

Here is the updated main.c file. It replaces the hardcoded string with dynamic file loading, validates file existence, and ensures safe heap allocation.
## Updated main.c

#include <stdio.h>#include <stdlib.h>#include "compiler.h"#include "vm.h"
char *read_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Could not open file %s\n", filename);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = malloc(length + 1);
    if (!buffer) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        fclose(file);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, length, file);
    buffer[read_bytes] = '\0';

    fclose(file);
    return buffer;
}
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <source_file.pas>\n", argv[0]);
        return 1;
    }

    printf("Reading source file: %s...\n", argv[1]);
    char *pascal_program = read_file(argv[1]);
    if (!pascal_program) return 1;

    printf("Compiling Pascal Program...\n");
    compile(pascal_program);
    printf("Compilation successful. Generated %d instructions.\n", code_idx);

    printf("\nRunning Virtual Machine...\n");
    run_vm();

    printf("\nVM Execution Finished. Final State:\n");
    for (int i = 0; i < sym_count; i++) {
        printf("%s = %d\n", sym_table[i], vm_vars[i]);
    }

    free(pascal_program);
    return 0;
}

## Execution Example

   1. Save your Pascal program text into a file named test.pas:

program MathTest;
var x, y, result : integer;
begin
  x := 15;
  y := 5;
  result := x * 2 + y - 3;
end.


   1. Compile the source code files: [1] 

gcc main.c lexer.c compiler.c vm.c -o pascal_compiler


   1. Run the executable by passing the source file path as an argument:

./pascal_compiler test.pas

If you want to keep expanding this project, I can help you:

* Implement an abstract syntax tree (AST) for cleaner optimization
* Create a bytecode output module to save compiled binaries directly to a file [2] 


[1] [https://www.reddit.com](https://www.reddit.com/r/AskProgramming/comments/mwm94a/how_to_compile_a_bunch_of_source_code_cpp_h_etc/)
[2] [https://codequiry.com](https://codequiry.com/blog/how-cross-language-code-plagiarism-detection-actually-works)
