# To Do

Future additions:
BASIC compiler to SVM. [.bas]
C compiler to SVM. [.c .h]
Assembler. [.asm, .inc]
Disassembler
Include, import
Object oriented. Classes and objects. Early binding only probably
Linker [.obj]




*** Break up the code even further ***
expression in own file
print_ast own file
etc.

1. ~~Break up source file into multiple source files~~

2. ~~Input file <name>.pas~~

3. Expand on syntax until Google AI gives up

4. ~~Add options - can also be achieved by having 3 different main programs~~
    pascal -c <file.pas> <file.bin>
    pascal -r <file.bin>
    

* my own byte code VM
* break out compiler from VM, separate apps
* assembler, disassembler
* First milestone: Wirth compatible code, VM my own
* Next milestones: object-pascal, Turbo pascal, Delphi
* GUI support
* Sound
* Database
* BASIC compiler
* 


# From Gemini

https://share.google/aimode/4AP5PmheFvhgb7HAg

## Done
- ~~Implement an abstract syntax tree (AST) for cleaner optimization~~
- ~~Create a bytecode output module to save compiled binaries directly to a file~~
- ~~Add Dead Code Elimination (e.g., removing variables that are assigned but never used)~~
- ~~Implement an AST Printer to output a visual tree structure text representation~~
- ~~Add Global Type Check Validation for handling different variable categories~~

## Priority 1
- Providing filename and exact line number diagnostics on syntax crashes
* Implement an Uninitialized Variable Warning pass
* Implement a Bytecode Disassembler to print instructions inside binary objects

## Priority 2
* Implicit Type Coercion (e.g., handling mixed conversions if you want to add a real/float data type)

## Priority 3
* Introduce jump offsets to support IF-THEN branch structures
* Add Control Flow structural support like IF/THEN loops
* Incorporate relational conditional tokens like >, <, and =
* Type checking for control-flow expressions (e.g., ensuring that the condition inside an IF statement evaluates to a boolean) [5, 6, 7, 8] 
* Add type checking to control-flow expressions (e.g., ensuring an IF test condition evaluates to a strict boolean)
* Implement boolean-specific operators like AND, OR, or NOT structures
* Show you how to implement IF-THEN control flow structures
* Add relational boolean comparison operators like =, <, or >
* Add structural control blocks like WHILE loops or IF-THEN statements
* Incorporate Write / WriteLn IO procedures

* Supporting alternative Pascal comment styles like (* ... *)
