# Solstice

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
- **[docs/ROADMAP.md](docs/ROADMAP.md)** — the detailed, checklist-level plan behind the summary below (what's still open).
- **[docs/CHANGELOG.md](docs/CHANGELOG.md)** — shipped features and fixed bugs, in the order they landed, with the design decisions behind each one.

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
[docs/LANGUAGE.md](docs/LANGUAGE.md#assert)); `warning(message)`, a
non-fatal runtime diagnostic that prints to stderr and keeps running
(see [docs/LANGUAGE.md](docs/LANGUAGE.md#warning)); `static` local variables,
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
`for x in ... do`, generalized beyond sets to 1D arrays and strings too
(`for x in arr do`, `for c in s do`) - still a pure parse-time
desugaring into an ordinary `for` loop plus (for sets/strings) an
evaluate-once cache, zero new opcodes (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#for-x-in--do)).
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
[docs/LANGUAGE.md](docs/LANGUAGE.md#file-io)). Also working: typed
(binary) files (`var f: file of TRecord;`, `seek`/`filesize`) — `read`/
`write` transfer a record's raw values directly rather than formatted
text, compiled entirely at compile time into one raw-int transfer per
field, the same "no runtime record-copy opcode" idiom whole-record
assignment already uses (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#typed-binary-files)). Also working:
untyped files (`var f: file;`, `BlockRead`/`BlockWrite(f, arr, count)`)
— raw binary I/O with no fixed record shape, desugaring entirely into an
ordinary `for` loop built from typed files' own per-value transfer
primitive, so it needed no opcodes of its own (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#untyped-files)). Also working:
sized integers — `byte` (`0..255`), `shortint` (`-128..127`), `word`
(`0..65535`), predefined subrange types that additionally write/read
their own narrower width (1 or 2 bytes, not 4) as a typed-file field or
element, for interop with an external fixed-width binary format
(`int64` not yet implemented — this VM's `integer` occupies one native-
`int`-sized storage slot everywhere, the same reason `real` stays 32-bit
— see [docs/LANGUAGE.md](docs/LANGUAGE.md#sized-integers)); and
`sizeOf(x)` — a pure compile-time constant answering "how many bytes
would `x` occupy as a typed-file record/element" (a record type,
record/typed-file variable, or scalar type — not "memory size," which
would always be a flat, uninteresting 4 in this VM's uniform-slot model
— see [docs/LANGUAGE.md](docs/LANGUAGE.md#sizeof)); and `IntToStr`/
`FloatToStr`/`StrToInt`/`StrToFloat`, number/string conversion in
memory (distinct from `write`/`writeln`'s formatting and `read`/
`readln`'s parsing, neither of which produces/consumes a `string`
*value*) — `StrToInt`/`StrToFloat` require the whole string to be a
valid number, a runtime error otherwise, matching `read`/`readln`'s
own fatal-on-invalid-input convention rather than a catchable exception
(see [docs/LANGUAGE.md](docs/LANGUAGE.md#numberstring-conversion)); and
`Random(n)`/`Randomize` — this compiler's first random-number
generation, `Random(n)` only (the mandatory-argument, `integer`-
returning form; Turbo Pascal's parameterless real-valued `Random` isn't
supported), deterministic across runs unless `Randomize` is called
first, matching real Pascal's own convention for free from plain C
`rand()`'s own default behavior (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#random--randomize)); and
`Delete`/`Insert`, in-place string mutation reusing `inc`/`dec`'s own
write-back trick (read the target, compute a new value, assign it back
through whichever of `NODE_ASSIGN`/`NODE_LOCAL_ASSIGN`/
`NODE_VAR_PARAM_ASSIGN` matches where it lives) — `Delete` can only
shrink a string so it never errors, `Insert` can grow one past this
compiler's string-length limit so it follows `+` concatenation's own
fatal-on-overflow convention (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#delete-and-insert)); and
`exit`/`exit(value)`/`halt`/`halt(n)` — early return from a procedure/
function (or the main program), and immediate program termination with
an OS exit code, both correctly unwinding through any enclosing
`try`/`finally` on the way out (`exit` runs every enclosing `finally`,
matching real Pascal; `halt` deliberately does not) (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#exit-and-halt)); `case` range
labels (`2..5: ...;`), mixing freely with plain-value labels and working
for any ordinal selector type (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#case--of)); typed constants
(`const arr: array[1..3] of integer = (1, 2, 3);` or `const p: TPoint =
(x: 1; y: 2);`), a genuinely initialized global array or record rather
than a plain `const`'s storage-less substitution (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#typed-constants-array-initializers));
`const`/`type`/`var` declaration sections that repeat and interleave
freely (Delphi-style) rather than each appearing once in a fixed order
(see [docs/LANGUAGE.md](docs/LANGUAGE.md#program-structure)); and
dynamic arrays (`var arr: array of integer; SetLength(arr, n);`), a
resizable array with reference semantics (assignment/value-parameter
passing shares storage, matching real Delphi) built on the same heap
`new`/`dispose` already allocate from, rather than a second allocator
(see [docs/LANGUAGE.md](docs/LANGUAGE.md#dynamic-arrays)); and lambda
literals (`cmp := function(a, b: integer): boolean begin exit(a < b);
end;`), non-capturing anonymous procedures/functions usable anywhere a
top-level procedure/function name is already accepted as a procedural
value (see [docs/LANGUAGE.md](docs/LANGUAGE.md#lambda-literals)).
Also working: records as
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
another pointer isn't supported yet. Also working: a generic `Pointer`
type and `@`/`Addr` — narrower than real Pascal's version by design, not
oversight: only the address of something already reached through a
pointer dereference (`@(p^.field)`) is computable, since this VM's
ordinary variables don't live in the same heap pointers target (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#pointer-and-addraddr)). Also working: nested procedure/
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
see [docs/CHANGELOG.md](docs/CHANGELOG.md).

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
Class-level `private`/`protected`/`public` sections restrict a field or
method's access: `private` to the declaring class's own methods only,
`protected` to the declaring class and every (transitively) descendant
class, `public` everywhere (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#privateprotectedpublic)) — a
separate, class-scoped mechanism from units' file-scoped visibility
above.
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
from outside that unit), plus optional `initialization`/`finalization`
sections that run automatically before/after the main program's own
body, in unit-dependency order — see
[docs/LANGUAGE.md](docs/LANGUAGE.md#units).

Also working, in `solas`/`desole` (hand-written `.sasm` tooling, not
Pascal-language features): `SWAP`/`OVER`/`ROT` stack-manipulation
opcodes, `DEBUG_STACK`/`DEBUG_SYMS` VM debug built-ins, a `desole -x`
raw hexdump mode, and `.macro`/`.endmacro` macro support in `solas`
(text-substitution parameters, with automatic per-expansion renaming of
any label the macro body defines itself — see
[docs/ASSEMBLER.md](docs/ASSEMBLER.md#macros)). Also working, in Pascal
source: `(* ... *)` as an alternate comment style alongside `{ }` and
`//` (see [docs/LANGUAGE.md](docs/LANGUAGE.md#comments)); compiler
directives — `{$DEFINE}`/`{$UNDEF}` and `{$IFDEF}`/`{$IFNDEF}`/`{$ELSE}`/
`{$ENDIF}` conditional compilation, entirely lexer-level (a false branch
is never even lexed) — see
[docs/LANGUAGE.md](docs/LANGUAGE.md#compiler-directives); and
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
[docs/LANGUAGE.md](docs/LANGUAGE.md#try--except--raise)). Also working:
`try`/`finally` (`try <body> finally <cleanup> end;`) — guaranteed
cleanup that runs whether or not `body` raised, then lets any in-flight
exception keep propagating; a separate construct from `try`/`except`,
nest to get both (see [docs/LANGUAGE.md](docs/LANGUAGE.md#try--finally)).
Also working: properties (`property Name: T read F write SetF;`), a
field- or method-backed named accessor resolved entirely at compile
time into an ordinary field access or method call — no new opcodes (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#properties)). Also working: `is`/`as`
(`obj is TCircle`, `obj as TCircle`) — a runtime type test and checked
downcast against an object's actual runtime class, not its static type;
a failed `as` raises a catchable exception, same as an explicit `raise`
(see [docs/LANGUAGE.md](docs/LANGUAGE.md#isas)). Also working: class
members (`class var Name: Type;`, `class procedure/function Foo(...);`,
`class property Name: T read GetX write SetX;`) — one shared storage
location per class hierarchy and true class methods/properties callable
as `TMyClass.Foo`, with no instance and no implicit `self` (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#class-members)). Also working:
abstract methods (`function Area: real; abstract;`) — a method with no
body, callable through a base-typed reference and dispatched dynamically
to whichever concrete descendant overrides it; `new()`-ing a class with
any unresolved abstract method is a compile error (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#abstract-methods)). Also working:
virtual destructors (`destructor Destroy;`) — `dispose(c)` now calls a
class's destructor, dynamically dispatched, before actually freeing the
instance (see [docs/LANGUAGE.md](docs/LANGUAGE.md#destructors)). Also
working: sealed classes (`class sealed ... end;`) — marks a class
unable to be subclassed, a compile-time-only check with no runtime cost
(see [docs/LANGUAGE.md](docs/LANGUAGE.md#sealed-classes)). Also
working: `const`/`out` parameters — `const` is a read-only by-reference
parameter (shallow: writing through a `const` pointer/class parameter's
own field is still legal, only reassigning the parameter itself is
rejected), `out` is runtime-identical to `var` with an added "never
assigned" warning (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#const-parameters)). Also working:
default parameter values (`procedure Foo(x: integer; y: integer =
10);`) — a trailing run of parameters may have a compile-time-constant
default; a call omitting trailing arguments gets the default spliced in
(see
[docs/LANGUAGE.md](docs/LANGUAGE.md#default-parameter-values)).

Also working: `Copy`/slicing for a dynamic array (`Copy(arr)`, `Copy(arr,
start)`, `Copy(arr, start, count)`) — a genuinely new array, not an
alias, lenient/clamping on out-of-range `start`/`count` rather than
erroring (see [docs/LANGUAGE.md](docs/LANGUAGE.md#copy)); and array-
literal syntax for a dynamic array (`arr := [1, 2, 3];`) as an assignment
right-hand side, sharing `[...]` with set constructors — which one a
given `[...]` means is resolved from the assignment target's own type,
not from the brackets' contents (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#array-literals)); and a named
type-alias form for a dynamic array (`type TIntArray = array of
integer;`), interchangeable with a directly-declared `array of integer`
of the same element type (see
[docs/LANGUAGE.md](docs/LANGUAGE.md#type-aliases)) — a **fixed-size**
array still can't be named this way, now with a clear compile-time
rejection rather than a confusing parser error. Not yet implemented:
`copy`/slicing, array literals, or a named alias for a fixed-size array,
an array literal as a general expression (e.g. a procedure argument) for
either array kind, variant records, and a whole record or an array
element as a `var` argument.
