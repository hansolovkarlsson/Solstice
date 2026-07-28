# Pascal → SolVM

A Wirth-style Pascal compiler targeting a custom stack-based virtual machine
(**SolVM**), plus a matching assembler (**solas**) and disassembler
(**desole**) for the VM's own bytecode.

This isn't aiming for P-Code or any existing VM compatibility — the whole
point is a bytecode format and machine designed from scratch, under your
own control, that a Pascal-like language happens to compile down to.

```
 source.pas ──(pascalc)──> program.bin ──(solvm)──> runs it
                                ▲
                 source.sasm ──(solas)──┘
                                │
                            (desole)
                                ▼
                          readable .sasm
```

## The toolchain

| Binary | Role |
|---|---|
| `pascalc` | Compiles a `.pas` source file to a `.bin` bytecode image |
| `solvm`   | Loads and runs a `.bin` bytecode image |
| `solas`   | Assembles a readable `.sasm` text file directly to `.bin` |
| `desole`  | Disassembles a `.bin` image back to readable `.sasm` |

`solas` and `desole` exist for a reason beyond convenience: they let you
inspect exactly what `pascalc`'s code generator produces, and hand-write or
hand-modify bytecode without going through the compiler at all. Every one
of `pascalc`'s new code-generation patterns in this project was cross-checked
by disassembling it and reading the result.

All four tools share the same `.bin` file format (`bytecode.c`) and the
same recoverable-error mechanism (`error.c`) — see
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how that fits together.

## Quick start

```sh
make                                   # builds all five binaries
./pascalc examples/hello.pas hello.bin
./solvm hello.bin
```

Every tool takes an optional `-v` flag for verbose/debug output (compiler
phase banners, the AST dump, VM step banners, the final variable-state
dump). Without `-v`, each tool prints only what it's actually supposed to
produce — the compiled program's own `write`/`writeln` output, the
disassembly listing, etc. — and stays silent otherwise.

```sh
./pascalc -v hello.pas hello.bin   # see the AST, phase-by-phase
./solvm -v hello.bin               # see step banners + final variable dump
```

A minimal program:

```pascal
program Hello;
var
    name: string;
begin
    name := 'World';
    writeln('Hello, ', name, '!');
end.
```

## Building

Requires a C11 compiler (Clang or GCC) and `make`. No external
dependencies.

```sh
make            # build everything
make pascalc    # build just one binary
make clean      # remove binaries and object files
```

## Documentation

- **[docs/LANGUAGE.md](docs/LANGUAGE.md)** — the Pascal dialect this compiler accepts: syntax, types, statements, operators, worked examples.
- **[docs/BYTECODE.md](docs/BYTECODE.md)** — SolVM's architecture, the full opcode reference, and the `.bin` file format.
- **[docs/ASSEMBLER.md](docs/ASSEMBLER.md)** — `solas`/`desole` syntax reference and usage guide.
- **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** — how the compiler pipeline and project are put together internally; start here if you're extending the codebase.

## Project layout

```
common.h          Shared types: tokens, opcodes, the AST node, Symbol, global state
lexer.c/.h        Source text -> tokens
parser.c/.h       Tokens -> AST (recursive descent)
type_checker.c/.h Static type checking over the AST
optimizer.c/.h    Constant folding + dead-code elimination
codegen.c/.h      AST -> bytecode
ast_printer.c/.h  AST -> human-readable tree (used by -v)
bytecode.c/.h     The .bin file format; shared code[]/sym_table[]/string_pool[] state
vm.c/.h           SolVM: the bytecode interpreter
error.c/.h        Shared recoverable-error facility + the verbose_mode flag
compiler.h        Convenience header bundling the whole compiler frontend

pascalc.c         Compiler binary entry point
solvm.c           VM binary entry point
solas.c           Assembler (source + entry point, single file)
desole.c          Disassembler (source + entry point, single file)
test_recovery.c   Demonstrates that a fatal compile error doesn't kill the process
```

## Status

Working: `if`/`while`/`repeat`/`for` (with `break`/`continue`),
`integer`/`boolean`/`string`/`char` types, string concatenation and
comparison (including ordering), one-dimensional arrays,
`write`/`writeln`/`readln`, full expression precedence, dead-code
elimination and constant folding, and procedures with by-value scalar
parameters and local variables — including correct per-call isolation
under recursion and mutual recursion via `forward` declarations (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#procedures)).

Not yet implemented: functions (procedures that return a value) and array
parameters/locals. Also missing: multi-dimensional arrays.
