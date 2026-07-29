# To Do



See if I can get LISP, Smalltalk, Prolog working on the SolVM as well
Should make separate projects first, because I'm learning Pascal making this compiler
Logo as well

I could name the project Ouroboros, seems like there are very vague use of it in programming environment


Array built-in functions as per standard Pascal


Would like some built-in functions to:
dump current stack
dump current list of symbols

constants



More stack operations, like dup, drop, over, rot, swap, etc

Later also add macros and other cool things for assembler

Manual for Pascal, SolVM, Solas, Desole etc
All data types
Functions
Type creation
Arrays

Write true/false for boolean type
Write(a,b,c); comma parameters
WriteLn(a,b,c);
read string

Hexdump of bytecode from disasm as an option
CLI argument for compiler Vm etc for no printing debug text
CLI argment to print AST, variables, etc

Also Closure constructs in functions would be cool
Need static variables

Compile time datatype check, like char_test.pas, shouldn't compile

Could add Token, Rule, Syntax, etc as functionality to make it built-in compiler support

Then the name change to Phoenix instead.

Assert functions
Error, Warning, built-in functions as well
Try-Catch-Retry or something similar


BASIC compiler to SVM. [.bas]
C compiler to SVM. [.c .h]
Assembler. [.asm, .inc]
Include, import
Object oriented. Classes and objects. Early binding only probably
Linker [.obj]




    

* First milestone: Wirth compatible code, VM my own
* Next milestones: object-pascal, Turbo pascal, Delphi
* GUI support
* Sound
* Database, SQL
* BASIC compiler
* Network protocol
* AI stuff maybe? 


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
