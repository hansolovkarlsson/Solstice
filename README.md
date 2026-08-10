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
src/common/           Shared by every binary
  common.h              Types: tokens, opcodes, the AST node, Symbol, global state
  bytecode.c/.h         The .bin file format; shared code[]/sym_table[]/string_pool[] state
  error.c/.h            Shared recoverable-error facility + the verbose_mode flag

src/pascalc/          Compiler front end (pascalc binary)
  lexer.c/.h            Source text -> tokens
  parser.c/.h           Tokens -> AST (recursive descent)
  type_checker.c/.h     Static type checking over the AST
  optimizer.c/.h        Constant folding + dead-code elimination
  codegen.c/.h          AST -> bytecode
  ast_printer.c/.h      AST -> human-readable tree (used by -v)
  compiler.h            Convenience header bundling the whole frontend
  pascalc.c             Compiler binary entry point
  test_recovery.c       Demonstrates that a fatal compile error doesn't kill the process

src/solvm/             VM (solvm binary)
  vm.c/.h               SolVM: the bytecode interpreter
  solvm.c               VM binary entry point

src/solas/solas.c     Assembler (source + entry point, single file)
src/desole/desole.c   Disassembler (source + entry point, single file)
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
[docs/LANGUAGE.md](docs/LANGUAGE.md#constants)). Also working: `type`
aliases (`type TAge = integer;`), resolved the same way as `const` —
usable anywhere the aliased type is, including parameters, record
fields, and function return types (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#type-aliases)). Also working:
enumerated types (`type TColor = (Red, Green, Blue);`), with `ord`/
`succ`/`pred`, ordinal comparison, and `write`/`writeln` printing a
value by name (`Red`, not `0`) via a compile-time comparison chain —
no VM or `.bin` format changes at all (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#enumerated-types)). Also working:
subrange types (`type TAge = 0..150;`), fully assignment/arithmetic-
compatible with `integer` (unlike an enum) but bounds-checked at every
point a value is stored into one — variables, array elements, record
fields, parameters (checked at every call site), and return values —
via two new opcodes (`CHECK_LOWER`/`CHECK_UPPER`) that peek and validate
the value already on the stack, with no `.bin` format change (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#subrange-types)). Also working:
record comparison (`p1 = p2`, `p1 <> p2`, desugaring to a field-by-field
`and`-chain) and the `with` statement (`with p do ...`), both pure
parser-time features needing no new opcodes — a bare identifier inside a
`with` body resolves exactly as `p.field` already did (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#the-with-statement)). Also working:
`assert(cond)` / `assert(cond, message)`, aborting cleanly with that
message if `cond` is false (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#assert)); `static` local variables,
which persist across calls (including recursive ones) by reusing the
same "hidden mangled global" trick local arrays already use (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#static-local-variables)); and 2D
array parameters and local 2D arrays, extending the existing by-
reference/local-array machinery with two new opcodes
(`LOAD_IDX2D_DYN`/`STORE_IDX2D_DYN`) mirroring the 1D dynamic ones (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#array-parameters-and-local-arrays)).
Also working: record parameters and local records, always by value, with
each field in its own per-call-isolated frame slot (unlike a global
record's hidden-mangled-global fields) — no new opcodes, a record
argument just flattens into N ordinary field-value pushes at the call
site (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#record-parameters-and-local-records)).
Also working: the `case`/`of` statement, a multi-way branch on an
`integer`/`char`/`boolean`/enumerated value with an optional `else`
catch-all — no new opcodes here either, reusing the existing comparison/
jump/assert machinery (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#case--of)). Also working: general
`var` parameters (pass-by-reference for scalars — `integer`/`real`/
`boolean`/`char`/`string`/enumerated/subrange — and record fields, not
just arrays), via three new opcodes (`PUSH_LOCAL_REF`/`LOAD_REF`/
`STORE_REF`) that let one reference value transparently address either a
global variable or one of the caller's own local/parameter slots (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#var-parameters)). Also working:
`read` (distinct from `readln` — doesn't consume the rest of the input
line, so several values can be read off one line), multiple targets in
one `read`/`readln` call, and the `eof`/`eoln` predicates for stdin,
usable bare (`while not eof do readln(x);`) like real Pascal (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#read-and-readln)). Also working:
sets (`set of 0..9`, `set of TColor`, `set of boolean`), represented as
a single int (one bit per possible element, capping a set's base type
at 32 distinct values) — construction, `in`, union/intersection/
difference, and subset/superset all reuse existing bitwise opcodes with
zero new ones added, and work anywhere a scalar type can (variables,
`var` parameters, array elements, record fields, return types) (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#sets)). Also working: `goto` and
block-scoped `label` declarations, compiling straight to the VM's
existing unconditional jump with zero new opcodes, via the same
backpatching technique `break`/`continue` already use (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#goto-and-labels)). Also working:
`for x in s do`, iterating a variable over a set's members - a pure
parse-time desugaring into an ordinary `for` loop plus an `in` test,
zero new opcodes (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#iterating-a-set-for-x-in-s-do)).
Also working: an uninitialized-variable warning pass — the compiler's
first non-fatal diagnostic (compilation still succeeds), flagging a
local variable read but never assigned, or a function that never sets
its own return value, within one procedure/function body at a time (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#warnings)). Also working: arrays
with three or more dimensions (up to 6), with full parity with 1D/2D -
a global/local variable, or a by-reference parameter with exact-shape
call-site validation (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#three-or-more-dimensional-arrays)).
Also working: text file I/O (`assign`/`reset`/`rewrite`/`close`, plus an
optional leading file argument on `read`/`readln`/`write`/`writeln`/
`eof`/`eoln` — `write(f, x)` instead of a separate set of file-specific
builtin names) — a file variable is global only, not a parameter or
local, the one deliberate scope cut behind the whole feature (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#file-io)). Also working: records as
array elements (`array[1..10] of TPoint`), for both a global and a
procedure-local array — field read/write via a runtime index, plus
whole-element copy to/from another array element or a plain record
variable (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#records-as-array-elements)); 2D/N-D
arrays of records and array-of-record parameters aren't supported yet.
Also working: pointers (`type PNode = ^TNode;`, `new`/`dispose`, `p^`/
`p^.field`, `nil`) — including the self-referential linked-list/tree
pattern (a pointer type forward-referencing a record type declared later
in the same `type` section), backed by this VM's first and only
dynamically-sized memory region, a single shared heap with a size-
bucketed freelist so `dispose` actually makes space reusable rather than
just leaking it (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#pointers)); pointer to an array or to
another pointer isn't supported yet. Also working: nested procedure/
function declarations, with lexical access to any enclosing procedure's
own locals and parameters at arbitrary nesting depth — a local array or
`static` local needs zero new runtime machinery to reach from a nested
body (both are already global storage under the hood), while a plain
scalar/parameter/record-field local reaches an enclosing frame via a new
static-link chain distinct from the VM's existing call stack, five new
opcodes (`PUSH_STATIC_LINK`/`POP_STATIC_LINK`/`LOAD_ENCLOSING`/
`STORE_ENCLOSING`/`PUSH_ENCLOSING_REF`) (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#nested-procedures-and-functions)); a
nested procedure's name stays in the same flat, whole-program namespace
every procedure already shares, rather than being lexically hidden
outside its own declaring scope, so calling one from outside its
lexical parent's active scope is a runtime error, not a compile-time
one, the moment it actually touches an inaccessible enclosing local.
Also working: functional/procedural parameters (`function Apply(function
f(n: integer): integer; v: integer): integer;`), standard ISO 7185
Pascal's inline form — the actual argument must be a top-level
(non-nested) procedure/function with an exactly matching signature (a
"procedure value" is just a runtime code address, so it fits the same
one-stack-value-per-parameter convention every other parameter kind
uses, needing zero `.bin` format changes and no closure/capture
machinery), and the parameter's own inline signature is scalar-only
(by-value/`var`) for now (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#functionalprocedural-parameters)).
Also working: nested records (a field's type can be another
already-declared record type, `.field.field...` chaining as deep as
needed) — stays pure parse-time flattening, recursively, so no new
opcodes; a record type used as a nested field can't itself have an array
field, and a record type with a nested-record field can't be an array's
element type, a pointer's target type, or a `with` target (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#nested-records)). Also working:
`program` heading parameters (`program Foo(input, output);`) — pure
syntax, accepted and discarded, since this VM has no OS-level
file-parameter binding for the list to mean anything (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#program-structure)). Also working:
variant records (`case tag: T of label: (fields); ... end` as the last
part of a `record`) — flattened into ordinary, simultaneously-live
fields rather than real overlapping storage, since this compiler's
records have no memory layout of their own to overlap in the first
place (see [docs/LANGUAGE.md](docs/LANGUAGE.md#variant-records)). This
was the last item on the Phase 1 (Wirth-compatible Pascal) checklist —
see [docs/ROADMAP.md](docs/ROADMAP.md).

Phase 2 (object-oriented Pascal) has started: classes and instances
work end-to-end (`type TFoo = class ... end;`, fields, methods, `new`/
`dispose`, `c.field`, `c.Method(args)`) with reference semantics, built
entirely on the existing pointer/heap machinery rather than a new
addressing model. Single inheritance (`class TCircle(TShape) ... end;`)
also works — field/method inheritance, method overriding, upcast
compatibility (a subclass instance usable anywhere its ancestor is
expected, including as `self` for an inherited method call), and
`inherited` itself (`inherited MethodName(args);` or bare `inherited;`,
forwarding the current method's own arguments) for an override to reach
its ancestor's own implementation directly, bypassing dynamic dispatch.
Class-level `private`/`public` sections restrict a field or method to
the declaring class's own methods (strict semantics, not "protected" —
not even a subclass's methods can reach a private member, matching what
was actually asked for), a separate, class-scoped mechanism from
units' file-scoped visibility above.
Every method call is dynamically dispatched (Java-style, no `virtual`/
`override` keyword needed) through the calling instance's own hidden
runtime type tag and a per-class vtable, rather than the accessing
expression's static type — the actual polymorphism payoff: a
subclass-overridden method called through an ancestor-typed reference
correctly runs the subclass's own implementation
(see [docs/LANGUAGE.md](docs/LANGUAGE.md#classes)). Also working: NAMED
procedural types (`type TProc = procedure(x: integer);`) — a variable,
local, `var` parameter, or record/class field that holds a reference to
a top-level procedure/function (or `nil`), assignable, comparable, and
callable, including a function (or class method) returning one as its
own result (see [docs/LANGUAGE.md](docs/LANGUAGE.md#procedural-types)). Also
working: units (`unit UnitName; interface ... implementation ... end.`,
pulled into a compile via `uses UnitName;`) — a source-level `uses`
mechanism (not separate compilation) that merges a unit's declarations,
classes included, into the same compile, with unit-level visibility
enforced for procedures/functions and global variables (something a
unit declares only in its `implementation` section can't be referenced
from outside that unit) — see [docs/LANGUAGE.md](docs/LANGUAGE.md#units).

Also working, in `solas`/`desole` (hand-written `.sasm` tooling, not
Pascal-language features): `SWAP`/`OVER`/`ROT` stack-manipulation
opcodes, `DEBUG_STACK`/`DEBUG_SYMS` VM debug built-ins, a `desole -x`
raw hexdump mode, and `.macro`/`.endmacro` macro support in `solas`
(text-substitution parameters, with automatic per-expansion renaming of
any label the macro body defines itself — see
[docs/ASSEMBLER.md](docs/ASSEMBLER.md#macros)). Also working, in Pascal
source: `(* ... *)` as an alternate comment style alongside `{ }` and
`//` (see [docs/LANGUAGE.md](docs/LANGUAGE.md#comments)), and
`ParamCount`/`ParamStr` for command-line argument access (Turbo
Pascal/Free Pascal's own convention — standard Pascal never defined
this) — `solvm program.bin arg1 arg2` forwards `arg1`/`arg2` through to
the running program (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#built-in-functions-and-procedures)).
Also working: `try`/`except`/`raise` — `raise <message>;` unwinds
straight to the innermost enclosing `try`'s `except` block, however
many procedure calls deep it's nested, readable back via `ExceptMessage`;
scoped deliberately to exceptions Pascal code explicitly raises — the
VM's own built-in runtime errors (division by zero, array bounds, and
so on) stay always-fatal, not catchable, matching the "you write it, you
catch it" boundary this feature draws (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#try--except--raise)).
Also working: properties (`property Name: T read F write SetF;`), a
field- or method-backed named accessor resolved entirely at compile
time into an ordinary field access or method call — no new opcodes (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#properties)).

Not yet implemented: dynamic arrays (so no array `copy`/slicing), variant
records, and a whole record or an array element as a `var` argument.
