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
- [x] Pointers (`^Type`, `new`, `dispose`) — the VM's first and only
      source of genuinely dynamic allocation (every other memory region
      is sized entirely at compile time). One shared, fixed-size heap
      (`vm_heap_mem[]`/`MAX_HEAP_MEM`, sized like `MAX_ARRAY_MEM`) rather
      than a per-pointer-type arena: `OP_NEW`'s `arg` is the target's
      element size (1 for a scalar target, or a record target's field
      count) - a compile-time constant - so a bump cursor
      (`vm_heap_count`, this VM's first RUNTIME-variable-size state) plus
      one freelist PER SIZE (`vm_heap_freelist[1..MAX_RECORD_FIELDS]`, a
      classic intrusive linked list - a freed block's own first int
      slot stores the next-free offset) is all `OP_DISPOSE` needs to make
      a later same-size `OP_NEW` reuse freed space, with zero new
      compile-time metadata tables. A pointer's own DataType is encoded
      as `TYPE_POINTER_BASE` + its `pointer_types[]` index (parser.c-
      local - the same "codegen only ever needs facts already baked into
      the AST" reasoning `record_types[]` relies on), mirroring
      `TYPE_ENUM_BASE`'s own encoding exactly - explicitly positioned
      immediately after the enum range (`TYPE_ENUM_BASE + MAX_ENUM_TYPES`)
      so the two never overlap. This surfaced a real, previously-latent
      hazard: every existing "is this an enum" check elsewhere in the
      codebase was a bare `>= TYPE_ENUM_BASE`, which would have silently
      also matched every pointer type once one existed (miscompiling
      `ord`/`succ`/`pred`/case-selectors/etc., or reading `enum_types[]`
      out of bounds when printing) - fixed by bounding all of them (`t >=
      TYPE_ENUM_BASE && t < TYPE_ENUM_BASE + MAX_ENUM_TYPES`) before
      adding the pointer range at all.

      Two new opcodes for the heap itself (`NEW`/`DISPOSE`, `arg` =
      element size) plus three for dereferencing (`LOAD_HEAP_FIELD`/
      `STORE_HEAP_FIELD`/`_CHAR`, `arg` = field offset - 0 for a scalar
      target's whole `^`): unlike the array-of-records opcode family,
      there's no "which array" to bake into `arg` at all (every pointer
      shares the one heap), so the runtime base address always comes off
      the stack instead, freeing `arg` to carry the field offset
      directly rather than needing its own stack push. Two new
      `NodeType`s (`NODE_HEAP_FIELD_ACCESS`/`_ASSIGN`) support an
      arbitrary-depth `^` chain (`p^.next^.data`) via a small loop in
      parser.c, each step's field offset resolved at parse time exactly
      like `NODE_ARRAY_RECORD_FIELD_ACCESS`'s own. `new(p)`/`dispose(p)`
      are pure parse-time sugar needing no dedicated runtime machinery at
      all: `new(p)` desugars into `p := <fresh heap allocation>;`
      (`NODE_HEAP_ALLOC`) straight through whichever assignment node `p`'s
      own resolution already builds (reusing `inc`/`dec`'s exact target-
      resolution shape - with-field/record-field/static local/`var`
      parameter/plain local or global - PLUS a `^`-chain target,
      e.g. `new(head^.next);`); `dispose(p)` only ever READS `p` (an
      ordinary expression, so it accepts a `^`-chain target for free) and
      deliberately does NOT nil it afterward, matching standard Pascal's
      own "a disposed pointer's value is undefined" semantics rather than
      inventing a safer non-standard behavior.

      The self-referential linked-list/tree pattern (`type PNode =
      ^TNode; TNode = record ... next: PNode; end;`) needs `PNode` to
      forward-reference `TNode`, not yet declared - resolved in a second
      pass at the end of the enclosing `type` section (mirroring the
      "forward" procedure declaration/goto-label backpatching precedent
      already in this codebase), the only kind of forward reference this
      feature supports (a pointer to a scalar/alias/enum/subrange must
      already be declared, exactly like every other reference to one).
      `nil` is a new literal (`TYPE_NIL`, runtime value `-1`) compatible
      with any pointer type at assignment/`=`/`<>`, checked specially
      rather than through the exact-DataType-match rule every other type
      uses; pointers otherwise support only `=`/`<>` (no arithmetic, no
      ordering) and are rejected from `write`/`writeln`/`readln`, matching
      standard Pascal.

      Found and fixed a second real bug during testing, the same DCE
      class as the array-of-records/subrange/set fixes above but a new
      instance of it: `x := p^;` with `x` a global never read elsewhere
      silently dropped the read's own nil-pointer-dereference check once
      dead-code elimination swept the assignment away - fixed by
      extending the existing heap-allocation DCE guard to also cover a
      `NODE_HEAP_FIELD_ACCESS` anywhere in an assignment's value
      expression, not just a top-level `NODE_HEAP_ALLOC`.

      Known gaps: pointer to an array or to another pointer; a pointer-
      typed field of a local/parameter record or an array-of-records
      element, immediately dereferenced (assign to a plain pointer
      variable first); whole-record assignment/comparison through a
      dereference (`q^ := p^;`) - see
      [docs/LANGUAGE.md](LANGUAGE.md#pointers).

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
- [x] Records as array elements (runtime-indexed record storage) — 1D
      arrays only, both global and procedure-local (a local array of
      records reuses the existing "hidden global" trick a local scalar
      array already relies on). Unlike a plain (non-array) record - pure
      parse-time sugar over one hidden global per field, since there's no
      runtime "which record" to select - `people[i].age`'s `i` IS a
      runtime value, so this needed a genuinely new, runtime-addressable
      storage model: two new `Symbol` fields (`is_record_array`,
      `record_elem_field_count` - the record type's field count, i.e. the
      per-element stride) so every size/offset computation in the shared
      `vm_array_mem[]` pool scales by that stride instead of the implicit
      1 every other array element type uses. A new `vm_record_array_
      offset()` (vm.c) generalizes `vm_array_offset()` accordingly.

      Three new opcodes, following the established "runtime index is a
      compile-time-constant-adjacent value pushed onto the stack, then
      one opcode with the array's sym_table index baked into `arg`"
      pattern `LOAD_IDX2D` etc. already use: `LOAD_ARRAY_RECORD_FIELD`/
      `STORE_ARRAY_RECORD_FIELD` (+ a `_CHAR` store variant, chosen by
      codegen from the field's own declared type - since one array-of-
      records `Symbol` covers many differently-typed fields, unlike
      `STORE_IDX`, there's no single `sym_table[].type` for the VM to
      dispatch a char-check on at runtime the way it does for a plain
      array). The runtime index and the field's compile-time-constant
      offset within one element both travel via the stack (only one
      `arg` slot per instruction, already used for the array's own
      sym_table index).

      Two new `NodeType`s (`NODE_ARRAY_RECORD_FIELD_ACCESS`/`_ASSIGN`)
      carry (array symbol, index expression, field offset as a
      `NODE_NUMBER` literal child - the same "reuse an existing pointer
      field for a literal" trick `NODE_RANGE_CHECK`'s bounds already use).
      A whole-element copy (`arr[i] := arr2[j];`, or to/from a plain
      record variable) desugars at PARSE TIME into N per-field
      `NODE_ARRAY_RECORD_FIELD_ASSIGN` nodes chained via `->next` - the
      same "a record isn't one runtime value" philosophy whole-record
      assignment already uses - with the index expression(s) cached into
      a hidden temp (local frame slot inside a procedure, global
      otherwise, mirroring the existing `for`-loop-end-bound/`for x in s`
      caching split) since a multi-field copy reads the SAME index once
      per field, not just once - re-evaluating a non-trivial index
      expression that many times would both re-run side effects and be
      wasteful.

      A record type used as an array's element type must have no
      array-typed field (a fixed per-element stride can't accommodate a
      variable-size field) - checked at declaration. Known gaps: 2D/N-D
      arrays of records, array-of-record parameters (no by-reference
      mechanism built for this yet - copy into/out of a local array of
      records instead), and passing a single array-of-records element
      directly as a by-value record argument to a procedure (copy it into
      a plain record variable first). Also fixed, in passing: the `-v`
      final-state variable dump only ever computed a record array's
      element count without multiplying by its field count (a smaller
      instance of the same pre-existing 2D-array dump bug fixed during
      the three-or-more-dimensions work) - see
      [docs/LANGUAGE.md](LANGUAGE.md#records-as-array-elements).
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
- [x] Nested records — a field's type can now be another already-
      declared record type, chaining `.field.field...` as deep as
      needed. Stays pure parse-time flattening, recursively: a nested
      field's own fields become more hidden globals/frame slots, mangled
      `outer__inner__leaf`, so no new opcodes and no changes to
      `type_checker.c`/`optimizer.c`/`ast_printer.c`/`codegen.c`/`vm.c`
      were needed - every existing per-variable mechanism (DCE, `-v`
      dump, codegen) already covers a nested leaf the same way it covers
      a plain field. `RecordVarDef.field_sym_idx[i]`/
      `LocalRecordVarDef.field_local_idx[i]` were reinterpreted as "the
      first leaf's index" rather than "the one symbol/local" - unchanged
      value for a scalar/array field, and exactly the base a nested
      field's own leaves are laid out contiguously from (a new
      `record_type_leaf_count()` walks that layout to resolve a `.field`
      chain or copy/compare/pass a nested field's leaves one by one).
      Self-reference/cycles need no explicit check: a record type can
      only nest an already-fully-declared type, so a field can never
      name its own (or a mutually recursive) type. Known gaps, each an
      explicit compile-time rejection rather than a silent limitation: a
      record type used as a nested field can't itself have an array
      field (transitively, by the same declaration-order argument as
      self-reference); a record type with a nested-record field can't be
      an array's element type, a pointer's target type, or a `with`
      target - each of those addresses a record by a fixed per-field
      slot/offset that doesn't generalize to "N slots instead of 1"
      without a larger rethink - see
      [docs/LANGUAGE.md](LANGUAGE.md#nested-records).
- [x] Variant records — `case tag: T of label: (fields); ... end` now
      parses as the last part of a record type. Scoping decision: this
      compiler's records already have no memory layout of their own (a
      record variable is N independent hidden globals/locals created at
      parse time), so building genuine *overlapping* storage between
      variants would mean inventing a real addressing model from
      scratch - out of scope here. Instead, the tag field plus every
      variant's fields are flattened into ordinary, simultaneously-live
      fields (same storage plain fields already get), with the
      `case`/label syntax checked and consumed at parse time (ordinal
      tag type, label type must match, labels pairwise distinct, field
      names unique across the whole record including across variants) -
      zero changes needed to type_checker.c/optimizer.c/ast_printer.c/
      codegen.c/vm.c/solas.c/desole.c, same as nested records. Known
      gaps, each explicit: no memory overlap/type-punning between
      variants, no nested `case`, no anonymous/unnamed tag form. See
      [docs/LANGUAGE.md](LANGUAGE.md#variant-records).
- [x] 2D array parameters and local 2D arrays (1D already supports both)
      — extends the existing by-reference/local-array machinery: two new
      opcodes (`LOAD_IDX2D_DYN`/`STORE_IDX2D_DYN`, mirroring the 1D
      dynamic ones exactly) for by-reference parameters, and local 2D
      arrays reuse the existing global `LOAD_IDX2D`/`STORE_IDX2D` via the
      same "hidden mangled global" trick 1D local arrays already use.
      `low`/`high`/`length` still don't support 2D (parameter or
      global) — see
      [docs/LANGUAGE.md](LANGUAGE.md#array-parameters-and-local-arrays).
- [x] Three-or-more-dimensional arrays (`array[1..2, 1..2, 1..2] of T`,
      up to `MAX_ARRAY_DIMS` = 6) — full parity with 1D/2D: a global/
      local variable, or a by-reference parameter with exact-shape
      call-site validation. Unlike 2D (which bolted a second hardcoded
      dimension onto the 1D mechanism - named `array_lower2`/`is_2d`
      fields, dedicated opcodes), this is a genuinely general N-
      dimensional mechanism: a dimension count plus `nd_lower[]`/
      `nd_upper[]` bounds arrays on `Symbol`/`LocalSymbol`/`ProcSymbol`,
      and 4 new opcodes (`LOAD_IDXND`/`STORE_IDXND` + `_DYN` variants)
      that read the dimension count from the symbol (or, for the `_DYN`
      by-reference case, from a fixed compile-time-known operand) rather
      than having it hardcoded per-opcode. A 3+D index list is chained
      through each index's own `->next` (the same sibling-chain
      technique `NODE_WRITELN`'s argument list already uses) rather than
      adding a 5th `ASTNode` child pointer, since a 3+D assignment needs
      more sub-expressions (indices plus a value) than `ASTNode` reserves.
      1D and 2D arrays are completely untouched - separate mechanism,
      zero shared code paths, zero regression risk to either.

      Along the way, found and fixed a real, pre-existing bug (not
      specific to this feature): dead-code elimination could silently
      drop an array-element or string-index assignment's runtime bounds
      check whenever the target array/string was otherwise unread
      anywhere in the program - e.g. `arr[999] := 1;` on an array never
      read elsewhere was silently eliminated instead of aborting with an
      out-of-range error. Fixed for `NODE_ASSIGN`'s array form,
      `NODE_ARRAY_ASSIGN_2D`, the new `NODE_ARRAY_ASSIGN_ND`, and
      `NODE_STRING_INDEX_ASSIGN` - array/string-index assignments are
      simply never eliminated by DCE now, regardless of whether the
      target is otherwise read (same principle as the range-check/set-
      side-effect DCE guards added earlier for subranges/sets). Also
      fixed the `-v` verbose final-state variable dump, which only ever
      printed a 2D array's first dimension's worth of elements (a
      pre-existing, unrelated display bug) - see
      [docs/LANGUAGE.md](LANGUAGE.md#three-or-more-dimensional-arrays).

### Language — I/O & error handling

- [x] File I/O (`text` files: `assign`/`reset`/`rewrite`/`close`, plus an
      optional leading file argument on `read`/`readln`/`write`/
      `writeln`/`eof`/`eoln` - `write(f, x)` instead of `write(x)`,
      matching real Pascal's overloaded syntax exactly, not a separate
      set of file-specific builtin names). `append` (opening for
      appending) is deliberately out of scope - a later, non-ISO-7185
      Pascal extension. A file variable is GLOBAL ONLY - not a
      parameter or local - a deliberate scope cut (see
      [docs/LANGUAGE.md](LANGUAGE.md#file-io)); this is what keeps the
      feature additive rather than needing a second by-reference
      mechanism the way array parameters have.

      A file variable's real state (the `FILE*`, and the filename
      `assign()` bound it to) lives in a new `vm_open_files[]` table,
      indexed by the SAME `sym_table[]` index the variable itself has -
      safe only because it's always global, and simpler than the
      "storage slot holds a second table's index" indirection
      `TYPE_STRING`/`TYPE_CHAR` use for `string_pool[]`, since the index
      is already known at compile time. 26 new opcodes: since 1D/2D/N-D
      arrays and `break`/`continue` etc. all reused the existing
      "parallel opcode family, zero changes to the existing path"
      pattern, file I/O does too, rather than unifying stdin/stdout with
      files onto one generic "always take a handle" calling convention -
      keeps this feature purely additive, and existing stdin/stdout
      bytecode is byte-for-byte unchanged. File-writing opcodes
      (`PRINT_FILE`, its `_PADDED` siblings, `NEWLINE_FILE`, `EOF_FILE`/
      `EOLN_FILE`) bake the target file's `sym_table[]` index directly
      into `arg`, exactly like `LOAD_IDX2D` already bakes an array's -
      no runtime "which file" value needed, since a file variable is
      always global. File-reading opcodes need TWO indices (the read
      target's AND the source file's), so the file's index is pushed
      via a plain `PUSH` right before the read opcode (mirroring how
      `LOAD_IDX_DYN`'s array-reference argument is pushed via
      `LOAD_LOCAL` before it) and popped first.

      `NODE_READLN`/`NODE_LOCAL_READLN`/`NODE_WRITELN` all gained an
      `extra` field (previously unused by any of them) holding an
      optional file-variable reference - `NULL` means stdin/stdout,
      exactly as before this feature existed, so a program using no
      files compiles to identical bytecode. `NODE_BUILTIN_CALL`'s
      `left` does the same for `eof`/`eoln`. One new `NODE_FILE_OP` node
      type covers `assign`/`reset`/`rewrite`/`close` together
      (distinguished by `->op`, mirroring `NODE_WRITELN`'s own
      `TOKEN_WRITE`/`TOKEN_WRITELN` reuse).

      Found and fixed two real bugs along the way, both about a file
      variable specifically being usable where it never should be:
      assigning one file variable directly to another (`f := g;`) would
      otherwise silently pass the ordinary same-type assignment check
      and compile to a meaningless `STORE`/`LOAD` pair (a file's real
      state lives in `vm_open_files[]`, not the plain storage slot that
      would actually get copied) - now a dedicated Type Error. Likewise
      no operator (including `=`/`<>`) is defined for file variables in
      standard Pascal either, so `NODE_BINARY_OP` now rejects any file
      operand outright. See the read/readln item above for a THIRD,
      pre-existing bug (unrelated to files) this work's testing
      surfaced.
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
      unfixed as out of scope here). A third bug, surfaced while testing
      File I/O (reproduced identically via plain stdin too, confirming
      it was pre-existing and unrelated to files) and fixed in a
      dedicated follow-up pass: a multi-target `readln(a, b, ...)` where
      a non-last, non-string/char target (an integer/real/boolean, read
      via `scanf`/`fscanf` without flushing) was immediately followed by
      a string/char target lost data - the string target's `fgets`
      picked up only the leftover newline right after the non-flushed
      value, not the actual next line, reading as empty. Root cause:
      flushing and "read a whole line via `fgets`" are two different
      mechanisms that didn't coordinate. Fixed with 2 new opcodes
      (`SKIP_PENDING_NEWLINE`/`SKIP_PENDING_NEWLINE_FILE`) that peek-
      and-conditionally-consume exactly one leftover `'\n'` - emitted
      only for this ONE specific target-type transition (detected at
      parse time in `parse_read_statement()`, since it already builds
      the whole target chain with full type information), never
      unconditionally before every string/char read, since that would
      risk skipping a genuinely blank line a program legitimately meant
      to read. Confirmed the fix doesn't affect that case (a standalone
      `readln(s)` reading an intentional blank line still reads it as
      empty, not skipped).
- [x] `program` heading parameters — `program Foo(input, output);` now
      parses (previously `program Name;` only). Pure syntax: the
      parenthesized identifier list is consumed and discarded, no
      validation and nothing stored, since this VM has no OS-level
      file-parameter binding for it to mean anything - any identifiers
      are accepted, not just `input`/`output`. See
      docs/LANGUAGE.md#program-structure.

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
- [x] Nested procedure/function declarations — a procedure/function
      declared inside another one, with lexical access to the enclosing
      procedure's own locals, at arbitrary nesting depth. Distinct from
      Closures (Phase 2, non-standard): plain lexical nesting doesn't let
      the nested procedure/function escape/outlive its enclosing call, so
      it needed none of closures' capture machinery. Implementation
      notes:
      - `current_locals[]`/`local_record_vars[]` (parser.c) became a
        per-nesting-level STACK (`scope_locals[MAX_NESTING_DEPTH][...]`),
        aliased back via macros so the ~50+ existing declaration call
        sites needed zero textual changes. Only outward NAME RESOLUTION
        (`find_local_outward()`/`find_any_record_var_outward()`, new)
        needed to actually search past the innermost scope - duplicate-
        declaration checks deliberately stayed scope-local-only, so a
        nested local can legally shadow an ancestor's same-named one.
      - "How many lexical levels up" (`levels_up`) rides on the
        `ASTNode.op` field for every affected node type
        (`NODE_LOCAL_VAR`/`_ASSIGN`/`_VAR_REF`, `NODE_VAR_PARAM_READ`/
        `_ASSIGN`, `NODE_REF_ARRAY_ACCESS`/`_ASSIGN` incl. 2D/ND,
        `NODE_LOCAL_STRING_INDEX`/`_ASSIGN`) - confirmed unused by every
        one of them beforehand, so this needed **zero new `NodeType`
        values and zero `ASTNode` struct changes**, directly following
        this project's "reuse existing fields" convention. `NODE_LOCAL_FOR`/
        `NODE_LOCAL_READLN` already used `op` for something else, so a
        `for` counter/`readln` target stayed restricted to the current
        procedure's own locals - a documented gap, not a silent bug.
      - Local arrays and `static` locals needed **zero new runtime
        machinery at all** for enclosing-scope access - both were already
        implemented as hidden, mangled GLOBAL symbols, so outward name
        resolution finding one declared in an outer scope was already
        the whole fix.
      - New VM machinery: a static-LINK chain (`vm_static_link[]` in
        vm.c, indexed by `fp`) distinct from the existing DYNAMIC
        (caller) chain `vm_call_stack[]` already tracks - 5 new opcodes
        (`OP_PUSH_STATIC_LINK`/`OP_POP_STATIC_LINK`/`OP_LOAD_ENCLOSING`/
        `OP_STORE_ENCLOSING`/`OP_PUSH_ENCLOSING_REF`; the latter three
        pack `(levels_up, slot)` into one `Instruction.arg`, precedented
        by `OP_PUSH_LOCAL_REF`'s own sign-encoding trick).
      - **Deliberately non-standard**: a nested procedure's name stays in
        the same flat, whole-program namespace every procedure already
        shares (matching how `forward`/mutual recursion already work),
        rather than being lexically hidden outside its declaring scope -
        avoids a scope-stack rearchitecture of name RESOLUTION, which
        was never the hard part of this feature (reaching an enclosing
        LOCAL at runtime was). The consequence: a nested procedure can be
        called from somewhere its lexical parent isn't an active
        ancestor. This compiles fine and only traps - a genuine `VM
        Runtime Error`, not silently wrong data - the moment such a call
        actually touches an inaccessible enclosing local; see
        [docs/LANGUAGE.md](LANGUAGE.md#nested-procedures-and-functions).
      - Real bug caught during implementation, not before shipping: the
        uninitialized-variable warning pass's `scan_local_usage()` walks
        one procedure body looking for `NODE_LOCAL_VAR`/`_ASSIGN`/
        `_VAR_REF` nodes and marks `read_flag[node->data.var_idx]`/
        `assigned_flag[...]` - unguarded, a nested body's own `levels_up
        > 0` node (referencing an ANCESTOR's slot) would coincidentally
        alias one of THIS procedure's own slot indices and corrupt its
        tracking. Fixed by skipping any such node unless `levels_up == 0`.
- [x] Functional/procedural parameters — passing a function or procedure
      as a formal parameter (`function Apply(function f(n: integer):
      integer; v: integer): integer;`), standard ISO 7185 Pascal's inline
      form. Distinct from Procedural types (Phase 2, non-standard): this
      needs no named, storable "pointer to a function" type — just the
      parameter written out inline, same as any other formal parameter.
      Two deliberate scope cuts, decided up front: only a TOP-LEVEL
      (non-nested) procedure/function may be passed as the actual
      argument (a compile-time error otherwise - fully knowable
      statically), and a procedural/functional parameter's own inline
      signature is scalar parameters only (by-value/`var`) for now, no
      arrays/records in it yet.

      One new opcode (`CALL_INDIRECT` - same as `CALL`, but its jump
      target comes off the stack instead of a compile-time `arg`, since
      this is the first call in this compiler whose target is only known
      at runtime) and two new `NodeType`s: `NODE_PROC_REF` (passing a
      top-level procedure/function BY NAME as an actual argument -
      `data.var_idx` = its `proc_table[]` index, backpatched via a new
      `pending_proc_refs[]` list that's the mirror image of
      `record_call()`'s own `pending_calls[]`, just patching a `PUSH`'s
      `arg` instead of a `CALL`'s) and `NODE_CALL_INDIRECT` (a call
      THROUGH an already-received procedural parameter - `op` =
      `levels_up`, `data.var_idx` = the parameter's own local frame slot,
      reusing `NODE_LOCAL_VAR`'s exact addressing convention; the
      statement/expression-context flag `NODE_CALL` keeps in `op` lives
      on `extra` here instead, as a stashed `NODE_NUMBER` literal, since
      `op` was needed for `levels_up`).

      Because a "procedure value" is restricted to a TOP-LEVEL target, it
      fits the exact same one-stack-value-per-parameter convention every
      other parameter kind already uses (no static link needed - a
      top-level procedure's own prologue never emits
      `OP_POP_STATIC_LINK` in the first place) - zero `.bin` format
      changes, and no closure/capture machinery at all (that's most of
      what real closures, Phase 2, would need instead). Forwarding an
      already-received procedural parameter to a further call needs no
      new opcode or node type either - it's just an ordinary
      `NODE_LOCAL_VAR` read, exactly like forwarding an already-`var`
      parameter.

      `parse_var_argument()` was refactored to take an explicit expected
      type/name instead of a `proc_idx` to look them up from, so its
      existing resolution logic (with-fields, record fields, statics,
      `var`-parameter forwarding) is reusable for a `var` argument inside
      a procedural parameter's OWN inline signature too - that signature
      has no `proc_table[]` entry of its own to read an expected type
      from. By-value argument type checking (including int→real
      widening) for a call through a procedural parameter is done
      inline, at parse time, rather than deferred to `type_checker.c`
      the way an ordinary call's arguments are - `type_checker.c`'s
      `NODE_CALL` case looks expected types up via
      `proc_table[node->data.var_idx]`, which doesn't exist for an
      indirect call (the real target is only known at runtime).

      Verified two things that turned out to need NO changes, despite
      initially looking like they might: `optimizer.c`'s
      `mark_used_variables()` (DCE) only ever tracks GLOBAL
      (`sym_table[]`-indexed) variables, never local frame slots, so
      neither new node type needed anything added there; and the
      uninitialized-variable warning pass's `check_uninitialized_locals()`
      only ever scans slots AFTER every parameter's own (starting at
      `param_slot_count`), so a procedural parameter - like every other
      parameter kind - is already outside its scope by construction.
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
      bug on its first run: `examples/test/local/test_local_for_recursion.pas`
      declared `factorialViaLoop` as a `function` that never set its own
      return value (only ever called as a statement, for its side
      effects) - fixed by redeclaring it `procedure` - see
      [docs/LANGUAGE.md](LANGUAGE.md#warnings).

### VM / bytecode

- [x] Additional stack-manipulation opcodes for hand-written `.sasm`
      (`SWAP`/`OVER`/`ROT` — `DUP` and `POP`/drop already existed).
      Hand-written `.sasm` only; `pascalc` never emits any of the three,
      since it always knows an expression's evaluation order at compile
      time and never needs to rearrange the stack at runtime — see
      [docs/BYTECODE.md](BYTECODE.md#comparison-integer--logic-boolean).
- [x] VM debug built-ins: dump the current stack, dump the current symbol
      table (`DEBUG_STACK`/`DEBUG_SYMS`, hand-written `.sasm` only, no
      `-v` dependency). `DEBUG_SYMS` reuses the exact global-dump loop
      `HALT`'s own `-v` output already used, factored out into a shared
      `vm_dump_globals()` helper so both call sites (end-of-run and
      mid-run-on-demand) stay identical - see
      [docs/BYTECODE.md](BYTECODE.md#debugging).

### Tooling

- [x] `desole` hexdump output option (`-x`) — a classic offset/hex/ASCII
      dump of the `.bin` file's raw on-disk bytes, bypassing
      `load_bytecode()` entirely (so it still works on a corrupted/
      truncated file the normal loader would refuse to open) — see
      [docs/ASSEMBLER.md](ASSEMBLER.md).
- [x] Macro support in `solas` (`.macro NAME [params...]` / body /
      `.endmacro`) — a preprocessing pass between `split_lines()` and the
      existing two-pass assembler, so neither pass needed any changes:
      expansion produces a fully-expanded line list that's copied back
      over the original `lines[]`/`num_lines` before Pass 1 ever runs.
      Parameter substitution is plain whole-word text replacement (one
      mechanism covers a parameter standing in for a variable name, a
      label, or an integer literal alike). A label *defined* inside a
      macro body gets a fresh, per-expansion-unique name generated
      automatically (both the definition and any same-name reference
      within that body are renamed together), so invoking the same macro
      twice doesn't collide — a label the body only references without
      defining (e.g. one passed in as a parameter) is left alone. Nested
      macro invocations work (one macro's body calling another), guarded
      by a fixed recursion-depth limit; a macro must be defined before
      any line that invokes it (single top-to-bottom pass, no forward
      references the way labels get) — see
      [docs/ASSEMBLER.md](ASSEMBLER.md#macros).
- [x] `(* ... *)` as an alternate comment style (alongside the existing
      `{ }` and `//`) — same "scan until the closing delimiter, don't
      nest" handling `{ }` already had, added as a third case in
      `lexer.c`'s comment-skipping block. Unambiguous with the `*`
      multiplication operator since Pascal has no prefix/unary `*`, so
      `(` immediately followed by `*` can only ever be the start of a
      comment — see
      [docs/LANGUAGE.md](LANGUAGE.md#comments).

## Phase 2 — Object-oriented Pascal + general OOP support in the VM

Once Phase 1 is done: grow Pascal into an object-oriented dialect, and
grow SolVM/`solas` to support OOP constructs generally rather than
Pascal-specifically, so later front ends can share the same bytecode
primitives instead of each reinventing them.

- [ ] Classes and instances (fields + methods), most likely early/static
      binding only to start
- [ ] Possibly add a C-style `union` concept — true overlapping storage
      between fields, which variant records deliberately did NOT
      provide (see docs/LANGUAGE.md#variant-records); would need a real
      addressing/memory model for records that doesn't exist yet
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
