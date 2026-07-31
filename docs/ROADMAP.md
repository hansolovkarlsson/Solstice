# Roadmap

Where Ouroboros is headed, and — more usefully — what's actually left to
do to get there. The [README](../README.md)'s Roadmap section is the
one-paragraph version of this; this is the working version, with the
current phase broken down into concrete, checkable tasks. Update this
file as work lands or plans change — it's meant to stay current, not to
be a one-time snapshot.

## Origin

Ouroboros started from a long-standing interest in stack-based
postfix VMs (Forth, HP calculators) and in how Pascal implementations are
often built on one. The goal isn't P-Code or any other VM's
compatibility — SolVM is designed from scratch, under this project's own
control, specifically so that more than one front-end language can
eventually target it. The long-run ambition is something in the spirit
of d:Base or SQLWindows: a self-contained tool for building personal
applications, with its own compiler(s), VM, and eventually GUI/database/
networking support, developed first on macOS and later ported to Linux.

## Phase 1 — Wirth-compatible Pascal + SolVM (current, active)

The active goal: bring `pascalc` up to Wirth/standard-Pascal
compatibility, expanding SolVM and `solas`/`desole` in step wherever a
language feature needs new bytecode capability. Everything below is
scoped to this phase; see [docs/ARCHITECTURE.md](ARCHITECTURE.md) for the
checklist to follow when implementing any single item (new
`TokenType`/`NodeType`/`Opcode` → lexer → parser → type checker →
optimizer → `ast_printer` → codegen → `vm.c` → `solas`/`desole`).

### Language — type system

- [x] `const` declarations (named constants) — resolved entirely at
      parse time (no `Symbol`/runtime storage); usable anywhere an
      expression is expected, and as an integer array bound. Known gap:
      folding only covers arithmetic/logical expressions, not string
      concatenation (`const S = 'a' + 'b';` isn't accepted yet, since
      `optimizer.c` doesn't fold string ops) — see
      [docs/LANGUAGE.md](LANGUAGE.md#constants).
- [x] Enumerated types (`type TColor = (Red, Green, Blue);`) — encoded
      as `TYPE_ENUM_BASE + enum_types[] index` so `DataType` stays a
      single plain field everywhere, with zero VM/`.bin` format changes
      (an enum value is just an int at runtime, exactly like `integer`).
      `ord`/`succ`/`pred`, ordinal comparison, and full scope (arrays,
      records, parameters, locals, return types, type aliases) all work.
      `write`/`writeln` print by name via a compile-time comparison
      chain. Known gaps: `inc`/`dec` don't work on enums (integer-only;
      use `succ`/`pred`), `succ`/`pred` don't range-check past an enum's
      first/last value, field-width syntax on an enum falls back to
      printing the raw ordinal instead of the name, array bounds can't
      reference an enum value, and a `const` can never reference an enum
      value (`const` is always parsed before `type`, so no enum value
      exists yet at that point) — see
      [docs/LANGUAGE.md](LANGUAGE.md#enumerated-types).
- [x] Subrange types (`type TAge = 0..150;`, integer only) —
      assignment/arithmetic-compatible with plain `integer` (unlike an
      enum, no distinct `DataType` encoding), bounds-checked at every
      point a value is stored via a new `NODE_RANGE_CHECK` AST wrapper
      compiling to two new opcodes (`CHECK_LOWER`/`CHECK_UPPER`, peek-
      and-validate, no `.bin` format change). Full scope: variables,
      array elements, record fields, parameters (checked at call sites),
      return values, `inc`/`dec`, and type aliases of a subrange. Named
      declaration only (`array[1..MaxSize] of TAge`, not an inline
      anonymous `var a: 0..150;`). Along the way, fixed a real bug this
      surfaced: dead-code elimination was silently dropping an
      assignment's runtime side effect (the range check) whenever its
      target variable was otherwise unread — see
      [docs/LANGUAGE.md](LANGUAGE.md#subrange-types).
- [x] Type aliases (`type TAge = integer;`) — resolved at parse time via
      the same centralized `parse_scalar_type()` every scalar-type call
      site already went through, so this also consolidated 3 duplicated
      inline type-keyword chains into that one function. Usable in var
      declarations (plain/array), record fields, parameters, locals, and
      function return types. Chains (`TYears = TAge;`) work; aliasing a
      record type doesn't (only the 5 scalar types) — see
      [docs/LANGUAGE.md](LANGUAGE.md#type-aliases).
- [ ] Sets
- [ ] Pointers (`^Type`, `new`, `dispose`)

### Language — records & arrays

- [x] `with` statement — pure parser-time sugar, no AST node/codegen/VM
      changes: a stack of active `with`-targets (`with_stack`) makes a
      bare field name resolve exactly like `record.field` already did,
      threaded through every identifier-resolution call site (`factor()`,
      assignment, `inc`/`dec`, `readln`, `for`-loop counter, `low`/
      `high`/`length`). Nests; a `with`-field shadows same-named locals/
      globals (classic Pascal behavior). Single record only (no `with a,
      b do`) — see [docs/LANGUAGE.md](LANGUAGE.md#the-with-statement).
- [ ] Records as array elements (runtime-indexed record storage)
- [ ] Record parameters and local records
- [ ] Nested records
- [x] Record comparison (`=`, `<>`) — desugars at parse time into a
      field-by-field `and`-chain of ordinary comparisons (same "a record
      isn't one runtime value" philosophy whole-record assignment
      already uses), so it needed no new opcodes either. Rejects
      different record types and records with an array field (whole-
      array comparison isn't supported) — see
      [docs/LANGUAGE.md](LANGUAGE.md#record-comparison).
- [ ] 2D array parameters and local 2D arrays (1D already supports both)
- [ ] Three-or-more-dimensional arrays
- [ ] Dynamic arrays (array `copy`/slicing)

### Language — I/O & error handling

- [ ] File I/O (text files: open/read/write to a file, not just stdin/stdout)
- [ ] `assert`
- [ ] User-level error/warning built-ins, distinct from the VM's internal
      recoverable-error mechanism (see [ARCHITECTURE.md](ARCHITECTURE.md#recoverable-errors-not-exit))
- [ ] Some form of try/except/retry that exposes that same
      recoverable-error mechanism to Pascal source itself, not just the C
      host

### Language — procedures/functions & diagnostics

- [ ] Closures (a nested function capturing its enclosing scope)
- [ ] Static (persistent-across-calls) local variables
- [ ] Uninitialized-variable warning pass

### VM / bytecode

- [ ] Additional stack-manipulation opcodes for hand-written `.sasm`
      (`over`, `rot`, `swap` — `DUP` and `POP`/drop already exist, see
      [docs/BYTECODE.md](BYTECODE.md))

### Tooling

- [ ] `desole` hexdump output option
- [ ] Macro support in `solas`
- [ ] `(* ... *)` as an alternate comment style (currently only `{ }` and `//`)
- [ ] VM debug built-ins: dump the current stack, dump the current symbol table

## Phase 2 — Object-oriented Pascal + general OOP support in the VM

Once Phase 1 is done: grow Pascal into an object-oriented dialect
(classes/objects, most likely early binding only to start), and grow
SolVM/`solas` to support OOP constructs generally rather than
Pascal-specifically, so later front ends can share the same bytecode
primitives instead of each reinventing them. Also under this phase:

- Possibly add C-style `enum`/`union` concepts alongside Pascal's own
  enumerated types
- Units/modules and an `uses`-style include/import mechanism
- Possibly a linker for separately-compiled object-style units (`.obj`)

## Phase 3 — Additional front ends

Next up after Phase 2: a **BASIC** compiler — not aiming for
compatibility with any one dialect, but pulling in features across
BASIC's history (from early BASIC through Visual Basic/VB.NET) for
whatever's useful, with OOP support of its own.

Further out and more speculative, roughly in order of interest: **Logo**,
**Prolog**, **LISP**, **Smalltalk**, and possibly a **C** front end
(`.c`/`.h`).

## Phase 4 — Phoenix (an original language)

Eventually, a language of this project's own design — tentatively named
**Phoenix** — drawing on ideas from everything above. Planned built-in
features: a GUI, lightweight database handling, networking, and
token/syntax/rule parsing support built into the language itself (rather
than bolted on as a library).

## Ideas / not yet scheduled

Things worth remembering that aren't attached to a phase yet:

- GUI toolkit (GTK has been mentioned, for a Mac/Linux-native app)
- Sound
- Database support — lightweight, ISAM-style file access, eventually SQL
- Network protocol support
- "AI stuff maybe" — genuinely open-ended, no concrete idea yet
