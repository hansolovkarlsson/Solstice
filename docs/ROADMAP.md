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
- [x] Sets (`set of 0..9`, `set of TColor`, `set of boolean`) — a set is
      a single int at runtime, one bit per possible element (bit K set
      means value K is a member), so a set's declared base type is
      capped at 32 distinct values; the base type's bounds are only
      validated at declaration time, then discarded (`TYPE_SET` is one
      opaque `DataType`, not parameterized per declared shape, unlike
      enums). Zero new VM opcodes — set construction, union/
      intersection/difference, and `in` all reuse `SHL`/`BOR`/`BAND`/
      `BNOT`/`EQ`; subset/superset (`<=`/`>=`) reuse `DUP` plus `BAND`/
      `EQ`. Because `parse_scalar_type()` is the one centralized hook
      every scalar-type call site already goes through, sets work
      everywhere a scalar type can appear for free: variables, `var`
      parameters, array elements, record fields, and function return
      types. Known gaps: combining two sets declared with different
      base types/ranges isn't checked (both are just bitmasks), no
      `write`/`writeln`/`readln` support, and `<`/`>` are rejected
      (matching standard Pascal — use `<=`/`>=` or `=`/`<>`). Along the
      way, fixed two real bugs: dead-code elimination was silently
      dropping a set constructor/`in`'s runtime side effect (same class
      of bug the subrange range-check fix caught earlier), and `solas`
      couldn't reassemble a disassembled program with set-typed globals
      (its `.var`/`.array`/`.array2d` directive parsers didn't recognize
      `"set"` as a type string) — see
      [docs/LANGUAGE.md](LANGUAGE.md#sets).
- [ ] Pointers (`^Type`, `new`, `dispose`)

### Language — control flow

- [x] `case`/`of` statement — a multi-way branch on an ordinal value
      (integer, char, boolean, or enumerated - not real/string, neither
      of which is ordinal), with an optional `else` catch-all. Each case-
      label value (an integer literal, char/char-code literal, true/
      false, a `const` reference, or an enum value name) is parsed
      without needing to know the selector's type yet - whether it
      actually matches is deferred to type_checker.c's NODE_CASE handling,
      once the selector's own expression_type is resolved (a compound
      selector expression's type isn't known until then). No new opcodes:
      codegen caches the selector once in a hidden global (reusing
      add_temp_var(), the same trick a global `for` loop's end-bound
      already uses), compares it against each label with EQ/SEQ chained
      by OR, and falls back to OP_ASSERT with an always-false condition
      when there's no matching label and no `else` (a runtime error,
      reusing the assert mechanism instead of adding a dedicated opcode).
      Case labels must be pairwise distinct (checked at parse time).
      Known gaps: no range labels (`2..5:` - lists values individually
      instead), no `otherwise` as an alternate spelling of `else` — see
      [docs/LANGUAGE.md](LANGUAGE.md#case--of).
- [x] `goto` and `label` declarations — a block-scoped `label` section
      (an unsigned-integer list, parsed before `const`/`type`/`var`, same
      declaration-order convention as those) and `N: statement`/
      `goto N;`. Zero new opcodes: both compile straight to the
      existing `OP_JMP`, using the same emit-then-patch backpatching
      technique `break`/`continue` already use for a loop's jump
      targets, just keyed by label id instead of "current innermost
      loop" (see codegen.c's label_table/generate_block()). A label's
      scope is exactly one block (the main program or one procedure/
      function) - the label table resets per block, so a goto can never
      cross a procedure boundary, and every declared label must label
      exactly one statement in its own block (checked at parse time).
      Known gap: unlike standard Pascal, this compiler doesn't reject a
      goto that jumps into the middle of a structured statement
      (if/while/for/case/with) from outside it — see
      [docs/LANGUAGE.md](LANGUAGE.md#goto-and-labels).
- [x] `for x in s do` — iterates `x` over a [set](LANGUAGE.md#sets)'s
      members in ascending order. Not standard Wirth Pascal (a later-
      dialect extension), added after `examples/test/test_set_print.pas`
      turned out to use this syntax. Implemented entirely as a parse-
      time desugaring into AST nodes this compiler already had - zero
      new NodeType, zero new opcodes: `s` is cached once into a hidden
      set-typed temporary (exactly like an ordinary `for` loop's end-
      bound), then an ordinary `for x := 0 to 31 do` wraps an
      `if x in <cached s> then <body>`, reusing the same fixed 0..31
      sweep `in`/set construction already rely on internally. `x` must
      be plain `integer` (not `boolean`/an enum, even for a `set of
      boolean`/`set of TColor`) - a set's bit position already *is* the
      raw ordinal, and a set's declared base type is discarded after
      declaration, so there's nothing to hand back except that raw
      value. Works everywhere an ordinary `for` loop counter can be
      (global, local, parameter's record field, `static` local) - see
      [docs/LANGUAGE.md](LANGUAGE.md#iterating-a-set-for-x-in-s-do).

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
- [x] Record parameters and local records — unlike a global record's
      fields (hidden mangled globals), a local/parameter record's fields
      each get their own ordinary FRAME SLOT (via the existing `add_local`
      machinery), giving proper per-call isolation, including under
      recursion. A record parameter is always by value (this compiler has
      no by-reference mechanism for scalars at all): flattened into N
      field-value pushes at every call site, reusing the existing
      scalar-argument-passing path — no new opcodes. Required splitting
      `ProcSymbol.param_count` (syntactic parameter count, for call-site
      arg-count checks) from a new `param_slot_count` (frame slots the
      parameters actually occupy, for `ENTER`/`STORE_LOCAL`), since the
      two now diverge whenever a record parameter is present. Local and
      global records of the same type mix freely in assignment/comparison.
      Known gaps: an array-typed field in a record parameter/local is
      rejected, no `static` record locals, and `with` still only accepts
      a global record variable — see
      [docs/LANGUAGE.md](LANGUAGE.md#record-parameters-and-local-records).
- [ ] Nested records
- [ ] Variant records (`case tag: T of ...` inside a `record`) — a
      record's alternate, overlapping field layouts selected by a tag field
- [x] 2D array parameters and local 2D arrays (1D already supports both)
      — extends the existing by-reference/local-array machinery: two new
      opcodes (`LOAD_IDX2D_DYN`/`STORE_IDX2D_DYN`, mirroring the 1D
      dynamic ones exactly) for by-reference parameters, and local 2D
      arrays reuse the existing global `LOAD_IDX2D`/`STORE_IDX2D` via the
      same "hidden mangled global" trick 1D local arrays already use.
      `low`/`high`/`length` still don't support 2D (parameter or
      global) — see
      [docs/LANGUAGE.md](LANGUAGE.md#array-parameters-and-local-arrays).
- [ ] Three-or-more-dimensional arrays

### Language — I/O & error handling

- [ ] File I/O (text files: open/read/write to a file, not just stdin/stdout)
- [x] `read` (as distinct from `readln`) and multiple targets in one
      `read`/`readln` call — `readln(a, b, c)` desugars at parse time into
      `read(a); read(b); readln(c)` (only the LAST target ever flushes to
      the next line; `read(a, b, c)` is the same with none of them
      flushing), reusing `NODE_READLN`/`NODE_LOCAL_READLN`'s existing
      `->op` field to carry `TOKEN_READ`/`TOKEN_READLN` per target
      (exactly like `NODE_WRITELN` already reuses `->op` for
      `TOKEN_WRITE`/`TOKEN_WRITELN`) - a single target is returned bare,
      unwrapped, so existing single-target programs compile to identical
      bytecode; two or more are chained via `->next` and wrapped in one
      `NODE_COMPOUND` (the same trick whole-record assignment already
      uses). Needed 4 new opcodes (`READ_NOFLUSH` and the int/real/bool
      `READ_LOCAL_*_NOFLUSH` variants - string/char need no `_NOFLUSH`
      counterpart, since `fgets` already consumes the whole line either
      way). Found and fixed a real pre-existing disassembly bug in
      passing: the original `READ_LOCAL_INT/BOOL/REAL/STR/CHAR` opcodes
      were never added to `desole`'s `is_immediate()`, so their frame-slot
      operand was silently dropped from disassembly output.
- [x] `eof`/`eoln` — two new opcodes (`EOF`/`EOLN`) that peek at stdin via
      `fgetc`/`ungetc` (nothing consumed) and push a boolean; usable bare,
      with no parentheses (`while not eof do readln(x);`), matching real
      Pascal's typical style — see
      [docs/LANGUAGE.md](LANGUAGE.md#eof-and-eoln). Known gaps: no bare
      `readln;` (skip to next line, no target) - this compiler never
      supported that form even before this - and `readln`/`read` still
      silently accept a bare global array as a target without erroring
      (a pre-existing gap, confirmed present before this work too, left
      unfixed as out of scope here).
- [ ] `program` heading parameters (`program Foo(input, output);`) — pure
      syntax at the moment (`program Name;` only); lowest priority here,
      since in virtually every real implementation this list is a no-op

### Language — procedures/functions & diagnostics

- [x] General `var` parameters, for scalars (integer/real/boolean/char/
      string/enum/subrange) — a record FIELD works too (global or local,
      or a `with`-target's); a whole record or an array element as a
      `var` argument is a known gap, tracked separately below. A
      reference is a single int: >= 0 is a global's sym_table[] index
      (compile-time-constant, reusing the same "just PUSH it" trick
      NODE_ARRAY_REF already uses for array arguments), < 0 is
      `-(index + 1)`, an ABSOLUTE `vm_frame_stack[]` index of one of the
      CALLER's own local/parameter slots, computed at the call site via a
      new opcode (`PUSH_LOCAL_REF`) using the caller's own frame pointer -
      only known at runtime, unlike a global's fixed index. Two more new
      opcodes (`LOAD_REF`/`STORE_REF`) dereference either kind uniformly,
      letting one calling convention (one stack value per `var` argument,
      exactly like every other parameter kind) reach both of this VM's
      separate storage regions without widening it. Forwarding an
      already-`var` parameter through to another call needs no new
      opcode at all - its raw frame-slot value already IS a valid
      reference, so an ordinary local read passes it through unchanged.
      Found and fixed a real dead-code-elimination bug along the way: a
      global passed ONLY as a `var` argument (never read directly) was
      being wrongly treated as unused and its assignment stripped -
      exactly the same class of bug NODE_ARRAY_REF was already kept
      distinct from `NODE_NUMBER` to avoid, just missed for this new node
      type at first. Known gaps: whole records and array elements as
      `var` arguments, `readln` into a `var` parameter, and a `var`
      parameter as a `for` loop counter — see
      [docs/LANGUAGE.md](LANGUAGE.md#var-parameters).
- [ ] Nested procedure/function declarations — a procedure/function
      declared inside another one, with lexical access to the enclosing
      procedure's own locals; not supported in any form yet (only
      top-level declarations are). Distinct from Closures (Phase 2, non-
      standard): plain lexical nesting doesn't let the nested
      procedure/function escape/outlive its enclosing call, so it needs
      none of closures' capture machinery
- [ ] Functional/procedural parameters — passing a function or procedure
      as a formal parameter (`function Apply(function f(n: integer):
      integer; v: integer): integer;`), standard ISO 7185 Pascal's inline
      form. Distinct from Procedural types (Phase 2, non-standard): this
      needs no named, storable "pointer to a function" type — just the
      parameter written out inline, same as any other formal parameter
- [x] Uninitialized-variable warning pass — the first non-fatal
      diagnostic this compiler emits (`file:line: Warning: ...`, printed
      to stderr, compilation still succeeds). Deliberately
      flow-insensitive (only "ever assigned anywhere in this body", not
      "assigned on every path that reaches this read" - avoids the real
      false-positive risk a full branch/goto-aware analysis would carry)
      and scoped to one procedure/function body at a time: flags a local
      read-but-never-assigned, and a function that never assigns its own
      return value. `var` parameters, `static` locals, arrays, and -
      deliberately, for now - all global variables (including the main
      program's own top-level `var` section) aren't checked, since
      telling "initialized by an earlier procedure call" from
      "genuinely uninitialized" needs whole-program analysis this pass
      doesn't attempt. Runs at parse time, inside
      `subroutine_declaration()`, since that's the only point
      `current_locals[]` (a parser-only scratch table) still holds this
      procedure's own local metadata. Found a real, pre-existing latent
      bug on its first run: `examples/test/test_local_for_recursion.pas`
      declared `factorialViaLoop` as a `function` that never set its own
      return value (only ever called as a statement, for its side
      effects) - fixed by redeclaring it `procedure` - see
      [docs/LANGUAGE.md](LANGUAGE.md#warnings).

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

Once Phase 1 is done: grow Pascal into an object-oriented dialect, and
grow SolVM/`solas` to support OOP constructs generally rather than
Pascal-specifically, so later front ends can share the same bytecode
primitives instead of each reinventing them.

- [ ] Classes and instances (fields + methods), most likely early/static
      binding only to start
- [ ] Possibly add C-style `enum`/`union` concepts alongside Pascal's own
      enumerated types
- [ ] Units/modules and an `uses`-style include/import mechanism
- [ ] Possibly a linker for separately-compiled object-style units (`.obj`)

### Language extensions beyond standard Pascal

Moved here from Phase 1: none of these are part of Wirth/ISO 7185
Pascal, so they don't belong on the Wirth-compatibility checklist even
though a few are already implemented — Phase 1 stays scoped to standard
Pascal only, and picks up general OOP/VM growth once Phase 2 starts
anyway, so this is where they land instead.

- [x] Record comparison (`=`, `<>`) — desugars at parse time into a
      field-by-field `and`-chain of ordinary comparisons (same "a record
      isn't one runtime value" philosophy whole-record assignment
      already uses), so it needed no new opcodes either. Rejects
      different record types and records with an array field (whole-
      array comparison isn't supported) — see
      [docs/LANGUAGE.md](LANGUAGE.md#record-comparison). (Standard Pascal
      doesn't permit comparing structured types with `=`/`<>` at all.)
- [ ] Dynamic arrays (array `copy`/slicing) — standard Pascal arrays are
      always fixed-size
- [x] `assert` — `assert(cond)` / `assert(cond, message)`, a new
      NODE_ASSERT AST node compiling to a single new opcode (`OP_ASSERT`,
      pop message then condition, abort with that message if false). A
      missing message is synthesized as a literal ("Assertion failed")
      at parse time, so codegen/the VM never handle a "no message" case
      separately — see [docs/LANGUAGE.md](LANGUAGE.md#assert).
- [ ] User-level error/warning built-ins, distinct from the VM's internal
      recoverable-error mechanism (see [ARCHITECTURE.md](ARCHITECTURE.md#recoverable-errors-not-exit))
- [ ] Some form of try/except/retry that exposes that same
      recoverable-error mechanism to Pascal source itself, not just the C
      host
- [ ] Closures (a nested function capturing its enclosing scope) —
      standard Pascal allows nested procedures with lexical scoping, but
      not one that escapes/outlives its enclosing call
- [ ] Procedural types / function pointers — a variable, parameter, or
      field that holds a reference to a procedure or function, matching
      Turbo Pascal's `type TProc = procedure(x: integer);` (standard
      Pascal only allows a procedure/function as a formal parameter
      inline, not as a named, storable type - see Functional/procedural
      parameters in Phase 1 for that standard form, which needs none of
      this)
- [ ] Functions/procedures as return values — a function returning a
      reference to another function/procedure; needs procedural types
      above (standard Pascal restricts a function's return type to a
      simple ordinal/real type, so this itself is non-standard too)
- [x] Static (persistent-across-calls) local variables — `static name:
      type;` reuses the exact "hidden mangled global" trick local arrays
      already use (which are already implicitly persistent), so every
      reference resolves to an ordinary global instead of a frame slot.
      Scalars only (a local array is already persistent by default).
      Shared correctly across recursive calls; two procedures' own
      same-named static don't collide (mangled `__static_proc_name`) —
      see [docs/LANGUAGE.md](LANGUAGE.md#static-local-variables).

## Phase 3 — Additional front ends

Next up after Phase 2: a **BASIC** compiler — not aiming for
compatibility with any one dialect, but pulling in features across
BASIC's history (from early BASIC through Visual Basic/VB.NET) for
whatever's useful, with OOP support of its own.

Further out and more speculative, roughly in order of interest: **Logo**,
**Prolog**, **LISP**, **Smalltalk**, and possibly a **C** front end
(`.c`/`.h`). Prolog is the intended home for a rules engine in the
classic sense — forward/backward-chaining inference over facts and
rules, with unification — rather than bolting that machinery onto Pascal
or Phoenix.

## Phase 4 — Phoenix (an original language)

Eventually, a language of this project's own design — tentatively named
**Phoenix** — drawing on ideas from everything above. Planned built-in
features: a GUI, lightweight database handling, networking, and
token/syntax/rule parsing support built into the language itself (rather
than bolted on as a library).

Language-level features under consideration, beyond whatever Pascal
itself will already provide by this point — not part of any Pascal
dialect, so tracked here rather than against Phase 1/2:

- [ ] Tuples
- [ ] Lambda expressions (anonymous functions with closure capture)

## Ideas / not yet scheduled

Things worth remembering that aren't attached to a phase yet:

- GUI toolkit (GTK has been mentioned, for a Mac/Linux-native app)
- Sound
- Database support — lightweight, ISAM-style file access, eventually SQL
- Network protocol support
- "AI stuff maybe" — genuinely open-ended, no concrete idea yet
