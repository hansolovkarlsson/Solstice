# Ouroboros

A multi-language toolchain built around one custom stack-based virtual
machine (**SolVM**) and its bytecode format — designed from scratch, under
its own control, so that multiple front-end languages can eventually
target it, rather than aiming for P-Code or any existing VM's
compatibility.

The active work right now is a Wirth-style **Pascal** compiler
(`pascalc`), developed in parallel with a matching assembler (**solas**)
and disassembler (**desole**) for SolVM's own bytecode. See
[Roadmap](#roadmap) below for where this is headed.

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
- **[docs/ROADMAP.md](docs/ROADMAP.md)** — the detailed, checklist-level plan behind the summary below.

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

## Roadmap

See [docs/ROADMAP.md](docs/ROADMAP.md) for the full, checklist-level
breakdown. In short: the first goal is to finish `pascalc` to the point
of Wirth/standard Pascal compatibility, while expanding SolVM and
`solas`/`desole` in parallel wherever new language features demand new
bytecode capability. From there, the plan is:

- Grow Pascal into an object-oriented dialect, and possibly add C-style
  `enum`/`union` concepts.
- Grow SolVM and the assembler into general object-oriented support (and
  other advanced features), so later languages share the same bytecode
  primitives instead of each reinventing them.
- Add further front ends over time — a **BASIC** compiler next (not
  compatible with any one dialect, but drawing features across BASIC's
  history, from early BASIC through Visual Basic/VB.NET), and further
  out, more speculatively, **Prolog**, **LISP**, and **Smalltalk**.
- Eventually, an original language of its own design, tentatively named
  **Phoenix**, drawing on ideas from the above, with built-in GUI,
  lightweight database handling, networking, and token/syntax parsing
  support.

## Status

Working: `if`/`while`/`repeat`/`for` (with `break`/`continue`),
`integer`/`real`/`boolean`/`string`/`char` types, string concatenation and
comparison (including ordering), one-dimensional arrays,
`write`/`writeln`/`readln`, full expression precedence, dead-code
elimination and constant folding, and procedures and functions with
by-value scalar parameters, local variables, by-reference array
parameters, and local arrays — including correct per-call isolation for
scalars under recursion and mutual recursion via `forward` declarations
(see [docs/LANGUAGE.md](docs/LANGUAGE.md#procedures),
[docs/LANGUAGE.md](docs/LANGUAGE.md#functions), and
[docs/LANGUAGE.md](docs/LANGUAGE.md#array-parameters-and-local-arrays)).
Also working: bitwise `and`/`or`/`xor`/`not` and `shl`/`shr` on integers,
the built-ins `abs`/`sqr`/`odd`/`succ`/`pred`/`inc`/`dec`/`ord`/`chr`,
`#NNN` char-code literals, and string handling — `length`, `s[i]`
indexing, `copy`/`pos`/`mid`/`left`/`right`/`inpos`, and
`upcase`/`uppercase`/`lowercase` (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#built-in-functions-and-procedures),
[docs/LANGUAGE.md](docs/LANGUAGE.md#char), and
[docs/LANGUAGE.md](docs/LANGUAGE.md#string)). Also working:
two-dimensional arrays and `low`/`high`/`length` for arrays, resolved
entirely at compile time (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#two-dimensional-arrays) and
[docs/LANGUAGE.md](docs/LANGUAGE.md#low-high-length)). Also working: the
`real` type (a 32-bit float), with automatic integer↔real widening,
`trunc`/`round`, `/` now real Pascal's actual always-real division,
`abs`/`sqr` extended to `real`, and constant folding of `real`-literal
expressions at compile time
(see [docs/LANGUAGE.md](docs/LANGUAGE.md#real)). Also working: records
(`type TPoint = record x, y: integer; end;`), with whole-record
assignment, implemented as pure compile-time sugar over ordinary global
variables — zero new bytecode instructions anywhere in the feature (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#records)). Also working:
`write`/`writeln` field-width and precision syntax (`writeln(x:10:2)`),
with width/precision allowed as arbitrary integer expressions, not just
literals (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#write-and-writeln)). Also working:
the rest of ISO Pascal's math functions (`sqrt`/`sin`/`cos`/`arctan`/
`exp`/`ln`), the `pi` constant, and exponentiation via `power(base, exp)`
and `**` (right-associative, tighter-binding than `*`/`/`) — domain
errors (`sqrt(-1)`, `ln(0)`, etc.) are caught uniformly at runtime, or at
compile time when foldable (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#math-functions-pi-and-exponentiation)).
Also working: `s[i] := val` string character mutation, via copy-on-write
under the hood so it's safe even when the string pool's deduplication
means another variable shares the same underlying entry (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#string)). Also working: `for` loop
counters and `readln` targets can now be a parameter/local variable, not
just global — including full per-call recursion safety for a local loop
counter's cached end bound (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#for-loops) and
[docs/LANGUAGE.md](docs/LANGUAGE.md#readln)). Also working: `const`
declarations, resolved entirely at compile time (no `Symbol` or runtime
storage at all — a reference compiles to exactly the same bytecode as
the literal value would), including as an array bound (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#constants)).

Not yet implemented: three-or-more-dimensional arrays, array
parameters/locals for 2D arrays specifically, dynamic arrays (so no
array `copy`/slicing), and records as array elements, record
parameters/locals, nested records, or record comparison.
