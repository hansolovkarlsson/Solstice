# Changelog

Shipped features and fixed bugs, in the order they landed, with the
design decisions and bugs found along the way for each one — kept here,
separate from [docs/ROADMAP.md](ROADMAP.md), so the roadmap stays a
short, scannable "what's left" list while this stays the durable
historical record. See docs/ROADMAP.md for what's still open.

## Phase 1 — Wirth-compatible Pascal + SolVM (complete)

The goal: bring `pascalc` up to Wirth/standard-Pascal compatibility,
expanding SolVM and `solas`/`desole` in step wherever a language feature
needed new bytecode capability. Everything below was scoped to this
phase; see [docs/ARCHITECTURE.md](ARCHITECTURE.md) for the checklist
followed for each item (new
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
      [docs/LANGUAGE.md](LANGUAGE.md#for-x-in--do).
- [x] `for x in ... do` generalized to 1D arrays and strings —
      `for x in arr do`/`for c in s do`, alongside the already-shipped
      set case, still zero new `NodeType`/`Opcode`: both desugar into
      the SAME `NODE_FOR`/`NODE_LOCAL_FOR` shape the set case already
      relies on, extending the two existing `parse_for_in_tail_global`/
      `_local` functions rather than adding new ones.

      Arrays need no evaluate-once caching at all (unusual, and worth
      calling out) - an array is never itself an expression VALUE in
      this compiler, only ever accessed by name, so there's nothing to
      cache; the sweep just walks the array's own compile-time-known
      bounds directly. Strings, by contrast, need the SAME evaluate-once
      treatment the set case already established (a string-valued
      expression, e.g. a function call, must run exactly once) plus
      their own length captured once at loop start, separately, using
      the same subrange/local-vs-global caching idiom.

      Two real defects found and fixed during design validation (a
      Plan-agent second pass), not left latent:
      - **Reusing `cache_expr_once()` for the string VALUE would have
        `fatal_abort()`'d at compile time.** That helper hardcodes its
        hidden temp as `TYPE_INTEGER` (its only prior use, record-array
        index caching, is always an int) - `type_checker.c`'s
        `NODE_ASSIGN` case validates against the temp's OWN registered
        `sym_table[]` type, not any AST node's `expression_type`, so
        caching a string this way would reject with "Cannot assign
        expression to variable" on the global path and silently
        mis-type on the local path. Fixed by hand-caching the string
        value the same way the existing SET-caching code already does
        (`add_var`/`add_local` with the real type up front) instead of
        reusing that helper - `cache_expr_once()` is still reused, but
        only for the LENGTH value, which genuinely is `TYPE_INTEGER`.
      - **A missing `wrap_range_check()`.** No generic runtime range
        check exists anywhere in `codegen.c`/`type_checker.c` - every
        one of the 20+ existing assignment-construction sites in
        `parser.c` explicitly wraps its own RHS; comparing the loop
        variable's base type against the array's element type (needed
        regardless, for an ordinary type mismatch) does nothing for
        this, since two variables can share a base type while only one
        is subrange-constrained. Every per-iteration element read
        assigned into the loop variable is now wrapped with
        `wrap_range_check()` using the LOOP VARIABLE's own subrange
        info, matching how every other assignment site in this codebase
        already treats range-checking as a property of the assignment
        TARGET.

      Scope cuts for v1: 1D arrays only (2D/N-D and arrays-of-records
      rejected with a clear error at the point the array target is
      resolved, matching the existing `low()`/`high()`/`length()`
      restriction); the iterated array/string may be a plain global, a
      "local" array/string, or (arrays only) a `var`-reference
      parameter - a record-field or `with`-field array specifically
      isn't recognized as a target and falls through to a natural,
      not purpose-written, compile error. See
      `examples/test/forin/test_forin_array_*.pas`/
      `test_forin_string_*.pas` (7 positive cases including the
      flagship evaluate-once-for-a-function-returning-a-string case and
      a runtime range-check confirmation, and 4 error cases) and
      [docs/LANGUAGE.md](LANGUAGE.md#for-x-in--do).

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
- [x] Typed (binary) files — `var f: file of TRecord;`, `read(f, rec)`/
      `write(f, rec)` transferring one record's raw values per call
      (not formatted text), plus new `seek(f, n)`/`filesize(f)` builtins
      for random access. `T`'s fields (recursively, for a record) are
      restricted to `integer`/`real`/`boolean`/enum/subrange/`set` - not
      `string`/`char`/pointer/procedural, whose raw storage (a
      `string_pool[]` index or a process-local address) isn't meaningful
      once written to a file and read back in a different run - and no
      array-typed fields, matching the SAME existing independent
      restriction whole-record assignment/comparison each already
      enforce for their own `is_array` checks.

      Extends `text`'s own `vm_open_files[]` table (one new
      `record_size` field, cached once at `reset`/`rewrite` time) rather
      than inventing a second file-state mechanism - `assign`/`close`
      are reused completely unchanged across both file kinds (their
      `TYPE_FILE`-only guard just widened to accept `TYPE_TYPED_FILE`
      too); `reset`/`rewrite` needed genuinely new opcodes
      (`OP_TYPED_FILE_RESET`/`REWRITE`, `fopen` mode `"rb"`/`"wb"`
      instead of `"r"`/`"w"`), with `arg` PACKING both the file's
      `sym_table[]` index and its record size (`record_size *
      MAX_SYMBOLS + file_sym_idx`) - both already known at compile time,
      mirroring `OP_STORE_VTABLE_SLOT`'s own precomputed-flat-index
      precedent exactly.

      `read`/`write`'s record transfer follows this compiler's own
      established "every record-shaped operation is unrolled entirely
      at COMPILE TIME into N ordinary field-level operations, never a
      runtime record-copy opcode" idiom (the same one whole-record
      assignment/argument-passing/comparison already use) - two new
      leaf-level opcodes, `OP_READ_TYPED_FILE_INT`/
      `OP_WRITE_TYPED_FILE_INT`, transfer exactly one raw int each,
      invoked once per leaf field by a parse-time walk reusing
      `record_field_read_node()`/`record_field_assign_node()` and the
      same base+offset recursion `build_record_copy()` already
      established for nested records. A `file of integer` (bare scalar
      element type) is simply the one-leaf case of the identical
      mechanism - no special-casing needed. `read`/`write`'s target must
      be a plain variable name (global or local) - not `rec.field`, not
      `arr[i]`, not multiple targets, a deliberate v1 scope cut.

      `eof(f)` on a typed file needed a genuinely SEPARATE opcode
      (`OP_TYPED_FILE_EOF`) from text mode's `OP_EOF_FILE` - the latter
      is a byte-level `fgetc`+`ungetc` peek, not meaningful for binary
      records; the typed variant instead compares the current file
      position against the end-of-file position (a `ftell`/`fseek`
      dance that also powers `OP_FILE_SIZE`, without disturbing the
      file's own read/write position). `eoln`/`readln`/`writeln` are a
      compile error on a typed file - no line concept in binary data.

      Two real bugs caught during implementation, both easy to miss:
      (1) a dead-code-elimination hazard - `read(f, someGlobalRecord)`
      with an otherwise-unread destination would have had its
      assignment, and the file read embedded inside it, silently
      deleted by `sweep_dead_assignments()`, desynchronizing every
      subsequent read from the file's actual position; fixed with a
      fifth side-effect predicate (`has_typed_file_read_side_effect()`),
      matching `has_as_cast_side_effect()`'s own exact shape, wired into
      the existing `NODE_ASSIGN` DCE guard. (2) `desole`'s `type_name()`
      has a `>= TYPE_ENUM_BASE` catch-all (degrading pascalc-frontend-
      only concepts like enum/pointer types to `"integer"`, since
      `desole` never links `parser.c`) that would have silently
      swallowed `TYPE_TYPED_FILE` too (it sits past that same boundary,
      being `TYPE_PROC_BASE + MAX_PROC_TYPES`) - fixed with an explicit
      check before the catch-all, since a typed file's runtime state
      genuinely IS VM-level (unlike enum/pointer), so `desole` CAN and
      must print it accurately for a `solas`/`desole` round-trip to
      stay correct. See `examples/test/typedfile/test_typedfile_*.pas`
      (7 positive cases including a dedicated DCE-hazard regression
      test, and 4 compile-time error cases) and
      [docs/LANGUAGE.md](LANGUAGE.md#typed-binary-files).

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

- [x] Classes and instances (v1, all 5 steps below complete) —
      reference semantics (Delphi/Java-style: an instance is a
      heap-allocated record with an implicit pointer type, `var f: TFoo`
      holds a reference, assignment aliases, `nil` is valid), chosen
      over value semantics for matching what "objects" usually means and
      for actually growing SolVM's own OOP primitives, rather than
      reusing plain records' zero-new-mechanism trick. Early/static
      binding only, as scoped (`obj.Method()` resolves to one fixed
      procedure at compile time - no vtable, no runtime method-address
      storage). Full design rationale, prerequisites, and known v1 gaps
      (no inheritance, no virtual dispatch, no constructors, no
      visibility, scalar fields only, no unqualified `self.` shorthand)
      in `notes/classes-and-instances-scoping.md` and
      [docs/LANGUAGE.md](LANGUAGE.md#classes).
- [x] Classes and instances, step 1/5: `class TFoo ... end;` declaration
      parsing — fields (reusing the record-field-group parser already
      factored out for variant records, scalar-only for now: array and
      nested-record/composition fields are a compile error) plus an
      implicit pointer-type synonym for the class name registered
      directly into `pointer_types[]` under `TFoo`'s own name (so
      `parse_scalar_type()` needed zero changes - it already resolves
      any pointer type by name), and method headers (reusing
      `parse_proc_param_header()`, the same scalar-only inline-signature
      parser functional/procedural parameters already use) parsed and
      stored per-class but not yet callable - no `.field`/`.Method(...)`
      access, no method body/dispatch, no `self`. See
      [docs/LANGUAGE.md](LANGUAGE.md#classes) and
      `notes/classes-and-instances-scoping.md`.
- [x] Classes and instances, step 2/5: `new(f)`/`dispose(f)` on a
      class-typed variable — confirmed working unmodified, exactly as
      predicted in step 1 (a class variable is an ordinary pointer
      variable under the hood). See
      `examples/test/class/test_class_new_dispose.pas`.
- [x] Classes and instances, step 3/5: `f.field` read/write, routed
      through the existing heap-dereference codegen - `f.field` on a
      class variable now compiles exactly like `f^.field` already does
      for a plain pointer, just without requiring the explicit `^`.
      Mechanically: `resolve_heap_deref_step()` (the function that
      resolves one `^` step) already expected `.field` right after the
      `^` it consumed, so a new `class_dot_deref_pending()` predicate
      (true when the base is a class and the next token is `.`, no `^`
      needed) was added alongside the existing `is_pointer_type(...) &&
      token.type == TOKEN_CARET` guard at every one of the 6 call sites
      that already handle a global/local/`var`-parameter read or write
      through a pointer - no new call sites, no codegen/VM changes.
      Since a class's fields are scalar-only (step 1's scoping
      decision), a class-dot step can never itself continue into another
      implicit-dot or `^` step, so no chain-depth logic was needed
      beyond what already existed for `p^.next^.data`-style chains.
      Along the way, fixed a rough edge this step's own tests surfaced:
      a "field not found" error on a class (`c.typo`) was naming the
      class's internal hidden backing-record ("$class0") instead of the
      class itself - `resolve_heap_deref_step()` now uses `pt->name`
      for a class specifically, matching the fix already noted as a
      known gap when step 1 landed. See
      `examples/test/class/test_class_field_access.pas`,
      `test_class_field_local_and_var.pas`.
- [x] Classes and instances, step 4/5: method bodies — `procedure
      TFoo.Method; ... end;` / `function TFoo.Method; ... end;`
      registers the body as an ordinary top-level procedure under a
      mangled name (`TFoo__Method`, the same trick record fields/static
      locals/nested-record leaves already use, needed because
      procedures share one flat whole-program namespace with no
      per-scope overloading), with an implicit `self: TFoo` parameter
      always in slot 0 - `self.field` works exactly like any other
      class-typed variable, reusing step 3 unmodified. Deliberately
      omits the parameter list/return type at the body site (matching
      this compiler's own existing `forward`-completion convention,
      not Delphi's convention of repeating it) - the header, already
      fully known from the class declaration (step 1), is looked up by
      class+method name and validated (procedure-vs-function kind must
      match). `ProcParamHeader` (shared with functional/procedural
      parameters) gained a `param_names[]` field so a method's params
      can be registered as named locals - functional/procedural
      parameters don't use it, but it costs them nothing. Also fixed
      the "assign to own function name" mechanism (needed for every
      function method to return a value at all) to match a method's
      unmangled short name via a new `ProcSymbol.unmangled_name`, not
      the mangled `proc_table[].name`. Known v1 gaps: no nested
      procedure/function declarations inside a method body, and no
      check yet that every method header declared in a class actually
      gets a body (a call to a body-less method's mangled name just
      fails with an ordinary "unknown procedure" error). Unqualified
      `self.` shorthand was later added - see the dedicated entry below.
      See
      `examples/test/class/test_class_method_basic.pas`,
      `test_class_method_samename.pas`, `test_class_method_varparam.pas`.
- [x] Classes and instances, step 5/5: `f.Method(args)` call syntax —
      `f.Method(args)` now compiles to an ordinary call to the method's
      mangled procedure, with `f` spliced in as the hidden first
      (`self`) argument. This closes out all 5 build-order steps for
      classes and instances, with static/early binding only, as scoped.
      Mechanically, `resolve_heap_deref_step()` (already shared by
      every `^`/class-dot read and write site since step 3) now checks
      a class's own methods whenever a name isn't a field, building a
      complete `NODE_CALL` (with a new, small
      `parse_class_method_call_arguments()` for the explicit argument
      list, self excluded - method parameters are guaranteed scalar, so
      this never needs the array/record/procedural-argument cases an
      ordinary call handles) instead of a field-access step. A method
      call is always the terminal step (never itself an assignment
      target, and its result can't be chained into a further `^`/`.`
      yet - a known v1 gap) - `parse_heap_deref_read()` returns it
      directly as a value (rejecting a procedure-method used as one,
      matching how any other procedure-used-as-a-value already is), and
      `parse_heap_deref_write()` signals it back as a complete statement
      via a new `HeapDerefStep.is_method_call` flag, which all 4 write/
      statement call sites (the 3 from step 3, plus `new`'s own
      `^`-chain target parsing, defensively) check before doing
      anything assignment-shaped. Along the way, fixed the last
      mangled-name leaks into user-facing errors (argument-count and
      `var`-argument mismatches now show the method's real short name).
      See `examples/test/class/test_class_call_basic.pas`,
      `test_class_call_procedure_statement.pas`,
      `test_class_call_samename.pas`.
- [x] Classes and instances: single inheritance — `class TCircle(TShape)
      ... end;` fully FLATTENS inheritance at declaration time, not a
      live relationship resolved later: every ancestor field is copied
      into the subclass's own hidden record, in order, before its own
      fields are parsed (so an ancestor's field offsets stay valid
      against a descendant's larger heap block - fields can never be
      overridden, only added), and every ancestor method header is
      likewise copied into the subclass's own method list, each
      carrying the mangled name that ACTUALLY implements it. A subclass
      redeclaring a method with the identical signature overrides it
      in place (mismatched signatures and duplicate overrides are both
      rejected); a purely-inherited method dispatches to the ancestor's
      own implementation, unchanged - both still resolved statically,
      at compile time, from the accessing expression's own declared
      type, consistent with early/static binding only. The other real
      piece: `type_checker.c`'s assignment/parameter-passing/comparison
      compatibility check (`try_widen_for_assignment()`, and the
      pointer `=`/`<>` case) now also accepts a subclass instance
      wherever an ancestor class is expected - via a new, narrowly-
      exported `class_type_is_subtype_of()` (parser.c keeps
      `pointer_types[]` itself private) - which is what makes both
      "assign/pass a `TCircle` where a `TShape` is expected" AND
      "`self` accepts a subclass instance for an inherited method call"
      work through the exact same mechanism. `var` parameters
      deliberately do NOT widen for a class upcast, matching this
      compiler's existing "a `var` argument never widens" rule.
      Along the way, fixed a real, pre-existing bug this surfaced (not
      specific to classes): `parse_name_group()` never initialized
      `nd_dims`, read as uninitialized stack garbage by
      `subroutine_declaration()`'s parameter-copy loop for any
      scalar/1D/2D parameter - harmless by chance until this work
      shifted stack layouts enough to trip it into a real crash. See
      `examples/test/class/test_class_inherit_basic.pas`,
      `test_class_inherit_method.pas`, `test_class_inherit_upcast.pas`,
      `test_class_inherit_multilevel.pas`, and
      [docs/LANGUAGE.md](LANGUAGE.md#classes).
- [x] Classes and instances: runtime type tag — every class instance now
      reserves heap offset 0 for a hidden tag (the allocating class's
      own `pointer_types[]` index), written by `new()` right after
      allocation; every ordinary field's own heap offset shifts by 1 to
      make room (`resolve_heap_deref_step()`'s one +1, gated on
      `pt->is_class` - a plain `type PFoo = ^Target;` pointer is
      unaffected). `target_elem_size` for a class is `field_count + 1`
      accordingly, so allocation size and `dispose()`'s matching
      freelist bucket both already account for it with no separate
      change needed there. Write-only for now - nothing reads the tag
      back yet; that's virtual/dynamic dispatch itself, the next step,
      now that both prerequisites (this, and procedural types just
      above) are done. Verified directly via `desole` disassembly
      (`new 2` / `push <class id>` / `store_heap_field 0` immediately
      after each `new`, every ordinary field's `store_heap_field`/
      `load_heap_field` shifted to start at 1, a subclass correctly
      tagged with ITS OWN class id rather than its parent's).
      Along the way, found and fixed a real heap-use-after-free (not
      specific to the tag): the `^`-chain branch of `new()` (`new(head^.
      next)`) tried to reuse its already-built `base` expression a
      second time to build the tag-write's read, but `free_ast()`
      visits a subtree through every parent that points at it, so a
      shared node got freed twice - confirmed with AddressSanitizer.
      Fixed by rejecting `new()` into a class-typed field reached
      through `^` with an explicit compile error (allocate into a plain
      class variable first, then assign it into the field) rather than
      leaving a silent "sometimes untagged" gap - a real utility to
      deep-copy an AST subtree would be needed to support it properly,
      and didn't exist. See
      `examples/test/class/test_class_tag_basic.pas`,
      `test_class_tag_inherit.pas`, `test_class_bad_tag_chain_new.pas`.
- [x] Virtual/dynamic dispatch — every class method is now dynamically
      dispatched by default (Java-style: no `virtual`/`override`
      keyword, since a single-inheritance override always has an
      identical, already-checked signature, leaving nothing ambiguous to
      resolve). A flat `vm_vtables[]` array in `vm.c`
      (`MAX_POINTER_TYPES * MAX_CLASS_METHODS` ints, `MAX_CLASS_METHODS`
      moved from `parser.c` to `common.h` alongside `MAX_RECORD_FIELDS`
      for the same reason - `vm.c` needs it to size the array), indexed
      `class_id * MAX_CLASS_METHODS + slot`, populated once at program
      startup by a `NODE_VTABLE_INIT_ENTRY` chain
      (`build_vtable_init_chain()` in `parser.c`) prepended to the main
      body ahead of any user code. A method's slot number is simply its
      index within its declaring class's own `PointerTypeDef.methods[]`
      - free to reuse as-is, since the existing inheritance-flattening
      (copying a parent's `methods[]` into a child in order, overriding
      IN PLACE rather than appending) already guarantees that index is
      stable across an entire ancestor/descendant hierarchy. A call site
      (`NODE_VIRTUAL_CALL`, replacing the old static `NODE_CALL` for
      this syntax) reads the calling instance's own runtime type tag
      (`OP_LOAD_HEAP_FIELD 0`), looks up that class's vtable row
      (`OP_LOAD_VTABLE_SLOT <slot>`, a new opcode - pops a class_id,
      pushes `vm_vtables[class_id * MAX_CLASS_METHODS + slot]`), and
      calls through the resolved address via the already-existing
      `OP_CALL_INDIRECT` - exactly the primitive procedural types
      already proved out, needing no changes of its own. The one new
      wrinkle `OP_CALL_INDIRECT` didn't already cover: it needs the
      callee address on TOP of the stack, but `self` must stay the
      BOTTOM-most pushed value (the callee's own reverse-order parameter
      unpacking consumes it last) - solved with a small `OP_DUP`/`OP_SWAP`
      shuffle (push self, `DUP` it to compute the target address without
      disturbing the original, then swap the target back on top after
      each argument push) rather than needing any new scratch storage.
      Along the way: found and fixed a real state-leak bug, unrelated to
      vtables but caught while working nearby - `proc_type_count`
      (procedural types, the previous step) was never reset in
      `parse_ast()`, silently accumulating across multiple compiles in
      the same host process; and updated `solas`/`desole` with the two
      new opcodes' mnemonics, confirmed via a full disassemble/
      reassemble/re-run round-trip (`CLAUDE.md`'s testing checklist
      catches real bugs here - `desole` was initially missing both from
      its `is_immediate()` operand table too, silently dropping their
      operand on disassembly). A method header declared but never given
      a body anywhere stays legal on its own (`test_class_basic.pas`)
      - it just gets no vtable entry; the pre-existing call-site check
      already prevents that entry from ever being read. Verified with a
      full `examples/` regression diff (compiled/run through both the
      pre-vtable and post-vtable binaries, output identical everywhere
      except the new vtable-specific tests) and a proactive
      AddressSanitizer/UBSan sweep (clean - surfaced two unrelated,
      pre-existing latent bugs elsewhere in the codebase instead,
      logged separately, out of scope here). See
      `examples/test/class/test_class_virtual_basic.pas` and
      [docs/LANGUAGE.md](LANGUAGE.md#classes)'s "Virtual dispatch"
      subsection.
- [x] Unqualified `self.` shorthand — a bare identifier inside a method
      body that isn't a local/parameter, but names a field or method of
      the enclosing class, now resolves as implicit `self.name`. Pure
      parser-level sugar: `resolve_heap_deref_step()` (the function
      every explicit `self.x`/`c.x` access already goes through) gained
      a third `has_dot` parameter - `1` for its two existing call sites
      (an explicit `.` was already matched before the field/method
      name), `0` for the two new shorthand call sites (the current token
      IS the name already, since there's no `.` to match). A new
      `class_has_member()` check (mirroring that function's own
      field-then-method lookup order) decides whether a bare identifier
      should be treated as shorthand at all, gated on a new
      `current_class_ptr_idx` global (parallel to, and saved/restored
      alongside, `current_proc_idx` in `parse_class_method_body()`) so
      the check only fires inside an actual method body. Inserted as a
      new resolution step in both `factor()` and `statement()`, right
      after the existing local/parameter lookup and right before the
      free-procedure/global-variable fallback - giving the precedence
      rule for free: a local/parameter of the same name still shadows a
      field/method (matches this file's own pre-existing "a local
      shadows a global" rule), while a field/method shadows a same-named
      global. Zero new `NodeType`/`Opcode` and zero changes outside
      `parser.c` - shorthand produces the exact same AST shapes explicit
      `self.x` already does, so type_checker.c/optimizer.c/
      ast_printer.c/codegen.c/vm.c/solas.c/desole.c all needed no
      changes (same "pure parse-time" shape variant records already
      established). Verified with the same full `examples/` old-vs-new
      regression diff methodology the vtable feature used - the only
      diffs were the new shorthand-specific tests themselves (two newly
      compile, one changes its resolved value on purpose to prove the
      new precedence rule, one - a same-named local parameter - was
      already unaffected since ordinary local lookup already covered
      it). See `examples/test/class/test_class_selfshorthand_*.pas` and
      [docs/LANGUAGE.md](LANGUAGE.md#classes).
- [x] Composite (nested-record) class fields — a class field can now be
      a plain `record ... end;` type (composition by value, e.g.
      `center: TPoint;`), read/written via `c.center.x` (or, inside a
      method body, shorthand `center.x`) - the nested-record half of
      classes v1's "scalar fields only" gap. Array-typed fields remain
      out of scope (need new VM opcodes for runtime-indexed heap
      addressing - a materially bigger follow-up, deliberately split
      off). Purely a parser-side offset-arithmetic fix: classes are
      heap-allocated with a single compile-time-immediate field offset
      baked into `OP_LOAD_HEAP_FIELD`/`OP_STORE_HEAP_FIELD`, which
      previously assumed every field was exactly 1 heap slot
      (`field_idx + 1`). Two new helpers, `class_field_heap_slots()` (1
      for a scalar, `record_type_leaf_count()` for a nested record -
      reusing the exact same leaf-counting plain nested records already
      use, since a nested field is already guaranteed array-field-free)
      and `class_field_base_offset()` (a prefix sum over preceding
      fields' slot costs), replace the old `field_idx + 1` in
      `resolve_heap_deref_step()`. A new `resolve_class_field_chain_offset()`
      walks any further `.subfield` steps past the first (mirroring
      `resolve_record_field_leaf()`'s own chain-walk, just accumulating
      a heap offset instead of a global sym_table index) - shared by
      both explicit `self.x`/`c.x` access and self-shorthand, since both
      already route through the same function. `parse_class_declaration()`'s
      old "reject array OR record fields" check narrowed to array-only;
      `target_elem_size` now computed via the same offset helper instead
      of a flat `field_count + 1`. Reading/writing a nested field as a
      whole (`c.center` alone, no further `.field`) is a compile error -
      an explicit, documented gap, not silently accepted.
      Found and closed along the way: `vm_heap_freelist[MAX_RECORD_FIELDS + 1]`
      (`src/solvm/vm.c`) is indexed directly by `elem_size` with no
      runtime bounds check in `OP_NEW`/`OP_DISPOSE` - safe only because
      `target_elem_size` had always been `<= MAX_RECORD_FIELDS + 1`
      before nested fields could make a class's total heap footprint
      exceed its own field count. A new compile-time check rejects any
      class whose flattened size would exceed that bound, closing a
      latent VM buffer-overflow this feature would otherwise have made
      reachable for the first time (see `test_class_composite_bad_toolarge.pas`).
      Verified with the same full `examples/` old-vs-new regression diff
      methodology used for the previous two features. See
      `examples/test/class/test_class_composite_*.pas` and
      [docs/LANGUAGE.md](LANGUAGE.md#classes).
- [x] Constructors — `new`'s own syntax grows an optional second
      argument, `new(c, Init(args))`, allocating and initializing in one
      statement. Turbo-Pascal-object-model style rather than Delphi's
      `c := TFoo.Create(args);`, deliberately: this compiler has no
      function/method overloading anywhere (`find_proc()` is a flat
      name-only lookup, `add_proc()` hard-rejects a duplicate name) and
      no precedent for a magic/reserved method name, so Delphi's model
      would need to invent both a first-ever special method name and a
      new expression-context allocation path. `new(c, Init(args))`
      avoids both - `Init` isn't reserved, any method works here, and
      it's pure sugar for `new(c); c.Init(args);` written as one
      guaranteed-together statement. Implemented entirely inside
      `parse_new_statement()` (`src/pascalc/parser.c`): the method name
      + arg list are optionally parsed right before `new`'s own closing
      `)`, resolved against the target class's `methods[]` exactly like
      `resolve_heap_deref_step()`'s own method-call branch does, and the
      resulting `NODE_VIRTUAL_CALL` is spliced onto the end of the
      existing allocate-then-tag-write statement chain (after the tag
      write, so dispatch from inside the constructor body already
      works). A third "fresh read" of the target builds the call's
      `self` argument, matching the two fresh-reads
      `parse_new_statement()` already builds for the assignment and the
      tag write - reusing either would double-free (see
      `build_class_tag_write()`'s own comment on that exact hazard).
      Zero new `NodeType`/`Opcode`; zero changes outside `parser.c` -
      `NODE_HEAP_ALLOC`/`NODE_VIRTUAL_CALL`/`NODE_COMPOUND`/
      `chain_two_statements()`/`build_class_tag_write()`/
      `parse_class_method_call_arguments()` are all reused unmodified,
      and inheritance works for free since `methods[]` already includes
      inherited entries. Explicit scope decisions: nothing enforces a
      constructor is ever called (`new(c);` alone stays completely
      legal); no `new(head^.next, Init(...))` (matches the existing
      restriction on `new` into a class field through `^`); no symmetric
      `dispose(c, Cleanup)` destructor sugar. Verified with the same
      `examples/` old-vs-new regression diff methodology used for the
      previous three features. See
      `examples/test/class/test_class_ctor_*.pas` and
      [docs/LANGUAGE.md](LANGUAGE.md#classes).
- [x] Array-typed class fields — the last classes v1 field-type gap.
      `data: array[0..3] of integer;` as a class field, read/written via
      `c.data[i]` or self-shorthand `data[i]`. Deferred when composite
      (nested-record) fields shipped, since - unlike those, pure parser-
      side offset arithmetic - this needed a genuinely new VM primitive:
      `OP_LOAD_HEAP_FIELD`/`OP_STORE_HEAP_FIELD` take the field offset
      as a single compile-time immediate, and there was no existing
      opcode for "heap address = runtime base + compile-time offset +
      runtime index." Three new opcodes (`OP_LOAD_HEAP_ARRAY_FIELD`/
      `OP_STORE_HEAP_ARRAY_FIELD`/`OP_STORE_HEAP_ARRAY_FIELD_CHAR`,
      appended at the end of the `Opcode` enum this time - CLAUDE.md's
      own "append, don't renumber" rule, learned the hard way during the
      vtable feature's mid-enum insertion) and two new AST nodes
      (`NODE_HEAP_ARRAY_FIELD_ACCESS`/`_ASSIGN`) do the addressing.
      Bounds-checking needed no new `.bin` format or metadata table:
      every existing array-indexing opcode derives bounds from a
      `sym_table[]` symbol at runtime, not applicable to a heap field
      with no per-instance symbol - instead this reuses
      `OP_CHECK_LOWER`/`OP_CHECK_UPPER` exactly as `wrap_range_check()`/
      `NODE_RANGE_CHECK` already do for subrange value checks, both
      already taking a raw compile-time bound as their own immediate.
      The array's `-lower` zero-basing is folded into the SAME immediate
      as the field's own base offset at compile time
      (`combined_offset = field_base_offset - array_lower`), so the new
      opcodes need only one immediate each, same as every other heap
      opcode. `class_field_heap_slots()` (from the composite-fields
      feature) gained the array-weighting case for free reuse by
      `class_field_base_offset()`/the `MAX_RECORD_FIELDS + 1` overflow
      guard, both already generic. An array-field access is always a
      TERMINAL step, like a method call - `self.items[i]` can't be
      followed by a further `.field`/`^` yet (real generality would need
      the read/write loops to re-enter on the array-access result,
      non-trivial for comparatively rare payoff) - a real bug from
      exactly this terminality requirement was caught and fixed before
      shipping: `parse_self_shorthand_read()` had been missed when
      wiring up the terminal-step check, silently reading element 0
      regardless of the actual index for any array-field shorthand read.
      A ~9-line "build the final write statement" block, duplicated at
      every `parse_heap_deref_write()` call site, was factored into a
      shared `build_heap_deref_write_statement()` while touching all of
      them for the new array-vs-scalar branch. Verified with the same
      `examples/` old-vs-new regression diff methodology used for the
      previous three features, a full solas/desole disassemble/
      reassemble/re-run round trip on the new opcodes, and a proactive
      AddressSanitizer/UBSan sweep (clean - the highest-risk surface of
      any feature this session, given raw `vm_heap_mem[]` pointer
      arithmetic with a runtime-computed index; the sweep's only hits
      were the same two unrelated, already-logged pre-existing bugs
      below, confirmed present on the pre-feature build too). See
      `examples/test/class/test_class_arrayfield_*.pas` and
      [docs/LANGUAGE.md](LANGUAGE.md#classes).
- [x] `inherited` method calls — an overriding method can now reach its
      ancestor's own implementation, both `inherited MethodName(args)`
      (explicit target, need not share the enclosing method's own name)
      and bare `inherited;` (same method, arguments forwarded unchanged
      from the currently executing method's own parameters). No new
      opcodes or `.bin` format changes: unlike an ordinary `c.Method()`
      call, the target is fully resolved at COMPILE time - inheritance
      is already flattened (`pointer_types[class].parent_class_ptr_idx`
      plus the parent's own `methods[]` already resolve to whichever
      ancestor actually implements a given method name, however many
      levels up the real override lives, confirmed with a three-level
      hierarchy test where the immediate parent doesn't override the
      method itself) - so `inherited` needs a plain direct call, not a
      vtable lookup. New node `NODE_INHERITED_CALL` reuses `OP_CALL`
      exactly the way `NODE_CALL` already does (`record_call()`/
      `emit_static_link_for_call()`), just pushing `self` first as an
      implicit argument the same way `NODE_VIRTUAL_CALL` already does -
      no `OP_SWAP` dance needed this time, since (unlike
      `NODE_VIRTUAL_CALL`'s dynamically-computed vtable target) there's
      no target address that needs to stay on top of the stack while
      arguments are pushed. Grammar: `inherited [Identifier]
      ['(' [args] ')']` - whether an identifier follows is the only
      branch point between the two forms; the explicit form's argument
      list reuses `parse_class_method_call_arguments()` completely
      unchanged, which already tolerates a missing `(...)` as "zero
      arguments" for an ordinary call, so `inherited NoArgMethod;` (no
      parens) falls out for free. Confirmed neither `type_checker.c` nor
      `optimizer.c` needed any change (matching `NODE_VIRTUAL_CALL`'s
      own precedent - the generic top-of-function recursion in both
      already covers a call's self/argument sub-expressions, and DCE's
      `mark_used_variables()` correctly marks a global used only inside
      an `inherited` call's argument, verified directly with a test
      rather than assumed). `ast_printer.c` needed a case (no `default:`
      there). See `examples/test/class/test_class_inherited_*.pas` and
      [docs/LANGUAGE.md](LANGUAGE.md#inherited).
- [x] Class-level `private`/`public` — `private`/`public` sections
      inside a `class ... end;` body, restricting a field or method to
      the DECLARING class's own methods - strict semantics, not
      "protected" (no `protected` level exists), matching what was
      actually asked for. A completely different, orthogonal mechanism
      from unit-level visibility (interface vs. implementation
      section, shipped earlier) - that one is file/unit-scoped, this
      one is per-class. No new AST nodes or opcodes - purely a parse-
      time access-control check on already-existing field/method
      resolution.

      Two new fields, `is_private`/`declaring_class_ptr_idx`, added to
      both `RecordField` (shared with plain records - always `0`/`-1`
      there, records have no visibility concept) and `ProcParamHeader`
      (shared with procedural parameters/types - same convention).
      `declaring_class_ptr_idx` tracks the ORIGINAL declaring class
      specifically because fields/methods are copied BY VALUE into
      every descendant's own tables (this compiler's established
      "flatten inheritance at declaration time" design) - without it, a
      descendant's own copy of an inherited private field couldn't be
      told apart from one it declared itself, which is exactly what
      strict (non-inherited) privacy needs to check. Survives an
      inheritance copy or override for free, since it's a plain struct-
      copy field alongside `mangled_name`/`is_inherited`, which already
      did.

      `private`/`public` are section markers, parsed as keywords
      recognized at the top of both of `parse_class_declaration()`'s
      two loops (fields, then methods - this compiler's class grammar
      doesn't interleave the two, unlike real Pascal), sharing ONE
      visibility state across both loops. Enforcement is
      `is_private && current_class_ptr_idx != declaring_class_ptr_idx`
      (`current_class_ptr_idx`, already tracking which class's method
      body is CURRENTLY being parsed, is the same signal self-shorthand
      already uses for "am I inside this class right now") - checked at
      three independent sites: `resolve_heap_deref_step()`'s field-
      access branch (covers every read AND write, explicit and self-
      shorthand alike, since both already share this one function),
      its method-call branch, and separately inside
      `parse_new_statement()`'s constructor-call resolution (`new(c,
      Init(args))` has its own, completely separate method lookup, not
      routed through `resolve_heap_deref_step()` at all - a private
      constructor needed its own identical check or it would have
      stayed callable from anywhere). Correctly allows a `TFoo` method
      to access *another* `TFoo` instance's private members directly
      (privacy is per-class, not per-instance, matching real Pascal)
      since the check is keyed on which class's code is executing, not
      on whether the instance expression is literally `self` - verified
      with a dedicated cross-instance test.

      Testing surfaced two real, pre-existing, unrelated limitations
      (confirmed identical on the pre-feature build) that shaped the
      test suite: a class method can't take a parameter or return value
      of its OWN class's type (the class isn't registered in
      `pointer_types[]` yet while its own method HEADERS are being
      parsed) - worked around in tests via global variables and local
      variables inside method BODIES instead, both parsed after the
      class is fully registered; and a specific forward-declared-
      function-returning-a-class pattern doesn't complete correctly
      either. Neither is in scope for this feature - logged here as
      newly-confirmed, not newly-introduced. See
      `examples/test/class/test_class_visibility_*.pas` and
      [docs/LANGUAGE.md](LANGUAGE.md#privatepublic).
- [x] Properties — `property Name: Type read ReadTarget [write
      WriteTarget];`, a named class member that reads/writes like a
      field at the call site while actually routing through a field or
      a method. `ReadTarget` is a field (direct read) or a zero-arg
      function (a getter); `WriteTarget` (optional - omitting it makes
      the property read-only) is a field (direct write) or a one-arg,
      non-`var` procedure (a setter). Both targets' types must match the
      property's own declared type exactly (no widening at declaration
      time).

      Pure compile-time sugar with zero runtime footprint - no new
      `NodeType`, no new `Opcode`, and no `type_checker.c`/
      `optimizer.c`/`codegen.c`/`vm.c`/`solas.c`/`desole.c` changes at
      all. A property resolves entirely inside the one existing shared
      choke point every class field/method access already goes through,
      `resolve_heap_deref_step()`, into the exact same node shapes a
      hand-written access would produce (`NODE_HEAP_FIELD_ACCESS`/
      `NODE_HEAP_FIELD_ASSIGN` for a field-backed side,
      `NODE_VIRTUAL_CALL` for a method-backed side) - so a property
      access is indistinguishable, post-parse, from one written by hand.

      A new per-class `ClassProperty` table (`PointerTypeDef.properties[]`,
      mirroring `RecordField`/`ProcParamHeader`'s own shape - name,
      declared type, read/write target kind + index, `is_private`/
      `declaring_class_ptr_idx`) is parsed as a THIRD group in
      `parse_class_declaration()`, after all fields and all methods (a
      property's target may be declared anywhere earlier in the same
      class body). Properties are inheritance-flattened exactly like
      fields - copied into every descendant, add-only, never overridden
      in v1 (unlike a method).

      The one genuine new mechanism: a setter-backed write's single
      argument appears syntactically AFTER `:=` (`obj.Radius := 5.0;`),
      not in a parenthesized list right after the name the way an
      ordinary method call's arguments do, so the existing
      `parse_class_method_call_arguments()` couldn't be reused as-is for
      that one case. Solved with one new `HeapDerefStep` outcome flag
      (`is_property_setter`/`setter_method_idx`) - structurally the same
      kind of terminal, write-context-only outcome `is_method_call`/
      `is_array_field` already are - and a new helper,
      `build_property_setter_call()`, that parses `:=` and the value
      expression itself (replicating the int-&gt;real widening + range-
      check treatment an ordinary field assignment already gets via
      `type_checker.c`'s `try_widen_for_assignment()`, since
      `NODE_VIRTUAL_CALL` has no `type_checker.c` case of its own to
      defer to - the same reason `parse_indirect_call()`/
      `build_procvar_call()` already duplicate that narrow check
      in-line). Wired into every write-context call site that resolves
      a heap-deref step (5 total, including `new(...)`'s own pointer-
      chain walk, which needed a defensive guard so `new()` against a
      setter-backed property fails cleanly instead of misreading a
      garbage field offset).

      Visibility is property-level, independent of the backing field's/
      method's own - a `public` property may front a `private` field or
      setter (verified with a dedicated test). No `is_property_setter`/
      new struct needed any `type_checker.c`/`optimizer.c` case; both
      confirmed to need zero changes by direct reading.

      Scope cuts for v1 (see [docs/LANGUAGE.md](LANGUAGE.md#properties)'s
      "Not implemented yet"): no indexed properties, no `default` array
      property, no class-level properties, no property overriding in a
      subclass, no `protected` visibility. See
      `examples/test/property/test_property_*.pas` (7 positive cases -
      field-backed read/write, field-backed write, getter-only read-
      only, self-shorthand, inheritance, a public property fronting
      private members, setter int-&gt;real widening - and 14 error cases
      covering every rejection path) and
      [docs/LANGUAGE.md](LANGUAGE.md#properties).
- [x] `is`/`as` operators — `obj is TFoo` (a runtime type test) and
      `obj as TFoo` (a checked downcast). Unlike Properties, this is NOT
      zero-runtime-footprint - it needs a genuine runtime check, because
      an expression's static/declared type and an object's actual
      runtime class can diverge once reference-assignment lets an
      ancestor-typed variable hold a subclass's pointer value; that
      divergence is exactly what these operators test, and it's
      unknowable at compile time (`class_type_is_subtype_of()`, the
      existing compile-time subtype check, only answers the static
      question).

      Two new opcodes: `OP_IS_INSTANCE` (arg = target class_id; pops an
      object pointer, nil-checks it FIRST so `nil is X` is `False`
      rather than a fatal nil-dereference abort, then walks a new
      `vm_class_parent[]` table upward from the object's own runtime tag
      until the target is found or the walk reaches the root) and
      `OP_STORE_CLASS_PARENT` (populates one `vm_class_parent[]` slot at
      startup). `vm_class_parent[]` is populated the exact same way
      `vm_vtables[]` already is - no new `.bin` file format, just
      ordinary compiler-emitted bytecode (`build_class_parent_init_chain()`
      in `parser.c`, mirroring `build_vtable_init_chain()`) that runs
      before any user code.

      `is` compiles to a single `OP_IS_INSTANCE` call. `as` is a small
      instruction sequence built from entirely PRE-EXISTING opcodes
      (`OP_DUP`/`OP_PUSH`/`OP_LT`/`OP_JZ`/`OP_JMP`/`OP_POP`) plus the one
      new `OP_IS_INSTANCE`: evaluate the object once, check for nil first
      (nil short-circuits straight to "yield nil", bypassing the class
      check entirely - `OP_IS_INSTANCE` alone can't distinguish "nil"
      from "wrong non-nil class"), then on a class mismatch, push a
      compile-time-interned failure message and `emit(OP_RAISE, 0)` -
      the EXACT same opcode an explicit `raise` statement uses, so a
      failed `as` is a genuinely catchable exception (Delphi's
      `EInvalidCast`), not a fatal VM error like nil-deref/bounds checks.
      No changes to `OP_RAISE`/the except-handler stack were needed at
      all.

      The right-hand operand (a bare class type NAME, not an expression)
      needed new, hand-written parsing - `factor()`/`expression()` have
      no existing hook for "identifier that names a type, not a
      variable" (mirrors how `new(x, MethodName)` already hand-parses an
      identifier against its own lookup table). A compile-time sanity
      check rejects `is`/`as` between two classes with no possible
      ancestor/descendant relationship in either direction, mirroring
      the existing `=`/`&lt;&gt;` pointer-comparison compatibility check in
      `type_checker.c`. `optimizer.c`'s dead-code elimination needed one
      new side-effect guard (`has_as_cast_side_effect()`) so a `NODE_ASSIGN`
      carrying a failed-cast-that-might-raise is never swept away just
      because its target is otherwise unread - the same class of gap
      `has_range_check()`/`has_set_side_effect()`/
      `has_heap_alloc_side_effect()` already exist to close.

      Scope cuts for v1 (see [docs/LANGUAGE.md](LANGUAGE.md#isas)'s "Not
      implemented yet"): the failure message names only the target
      class, not the actual runtime source class (no tag-to-name-string
      machinery exists); class types only, no non-class pointer types;
      one-shot precedence tier, no chaining. See
      `examples/test/isas/test_isas_*.pas` (6 positive cases including
      the ancestor-typed-variable scenario that makes the feature
      meaningful, and 5 error cases - 4 compile-time, 1 uncaught runtime
      exception) and [docs/LANGUAGE.md](LANGUAGE.md#isas).
- [x] Units/modules and an `uses`-style include/import mechanism — a
      `unit UnitName; interface ... implementation ... end.` file,
      pulled into a compile via `uses UnitName;` in a program or another
      unit. Deliberately NOT separate compilation (that stays the
      "linker" item below) - `uses Foo;` locates `Foo.pas` (same
      directory as the file containing the `uses` clause, no search
      path) and recursively re-parses its `interface`+`implementation`
      straight into the same global tables (`sym_table[]`,
      `proc_table[]`, `record_types[]`, `pointer_types[]`, etc.) the
      main program's own declarations already use, via a lexer save/
      restore point (`lexer_save_pos()`/`lexer_restore_pos()`,
      `LexerPos` in `lexer.h`) that lets the parser recurse into another
      file's source and come back exactly where a `uses` clause left
      off. By the time codegen runs, a `uses`-based program is
      indistinguishable from one big file - this needed no new AST
      nodes, no new opcodes, and no `.bin` format changes, almost
      entirely front-end (`lexer.c`/`parser.c`) work.

      Interface procedure/function headers need no `forward` keyword -
      the interface/implementation split IS the forward declaration,
      matching real Pascal unit syntax. This reuses
      `subroutine_declaration()` almost unchanged: a new `header_only`
      parameter makes its existing `is_forward = 1` early-return branch
      (originally gated on seeing a literal `forward` token) fire
      unconditionally instead, so the implementation section's
      completing bodies use this compiler's own pre-existing forward-
      declaration completion syntax (no parameter list/return type
      repeated) rather than standard Pascal's repeat-the-signature unit
      style - a deliberate, documented deviation
      (docs/LANGUAGE.md#units) in favor of reusing one existing
      mechanism instead of building a second, parallel one. A forward-
      declared interface proc never completed anywhere in the
      implementation is caught for free by the existing end-of-parse
      sweep over the whole (by-then fully merged) `proc_table[]`, run
      once after everything - main program included - has been parsed.

      Class support fell out for free: a class's method headers are
      already registered when its `type TFoo = class ... end;` is
      parsed (`parse_type_section()`, already standalone), and a method
      body is completed later via the existing `procedure TFoo.Bar;`
      branch, which returns before ever reaching the new `header_only`
      check - so a class declared in a unit's interface, with method
      bodies in its implementation, needed zero unit-specific code.
      Diamond dependencies (two units both `uses`-ing a shared third)
      and circular dependencies are handled by two small parser-local
      registries, `loaded_units[]` (fully merged already - a repeat
      `uses` is a silent no-op) and `loading_units[]` (a stack of units
      currently mid-load - naming one already on it is a circular-
      dependency compile error), both reset at the top of `parse_ast()`
      like every other global counter there - verified not to leak
      across compiles in the same process with a scratch harness
      compiling two different units-using programs back to back
      (`test_recovery.c` itself stays file-I/O-free by design, so this
      check lives outside it rather than compromising that).

      Also added: `ProcSymbol.source_file`, recorded once per proc at
      `add_proc()` time, plus `set_current_filename()` alongside the
      existing `get_current_filename()` - `pascalc.c`'s per-proc type-
      check/optimize loops and `codegen.c`'s `generate_program()` proc
      loop now switch to a proc's own recorded file while processing its
      body, so a compile error inside unit-declared code reports the
      right filename instead of whichever file parsing finished on last
      (line numbers were always correct; only the filename was wrong
      before this).

      Scope decisions, all documented in docs/LANGUAGE.md#units rather
      than silently dropped: `uses` only in a program's own header or a
      unit's `interface` (not `implementation`); no visibility
      enforcement at all yet (see the dedicated entry below - fixed
      shortly after this one shipped); same-directory-only unit
      resolution, no search path. No new opcodes were added, so (per
      CLAUDE.md's own checklist) no solas/desole changes were needed
      either. See `examples/test/units/test_units_*.pas` and
      [docs/LANGUAGE.md](LANGUAGE.md#units).
- [x] Unit-level visibility for procedures/functions and global
      variables — something a unit declares only in its
      `implementation` section can no longer be referenced from outside
      that unit (not the main program, not a different unit that merely
      `uses` it). Investigated enforcing this generically across every
      kind of declared name first: doing so correctly means auditing
      and classifying (declaration-time, which must stay visibility-
      blind or a private symbol in one unit could silently collide with
      an unrelated public one elsewhere, vs. reference-time, which
      needs the check) every call site of the relevant lookup function -
      counted roughly 92 across 9 separate lookup functions (vars,
      procs, consts, record types, classes, type aliases, enum types,
      enum values, subrange types), the single largest change surface
      in this project's history. Scoped down to procs/vars specifically
      (chosen deliberately over class-level `private`/`public` fields -
      a different mechanism entirely - as the more real, more requested
      gap now that units give it an actual boundary to enforce): the
      real number is 27 call sites (`find_var`: 11, `find_proc`: 16),
      every one individually read and classified, not sampled.

      Two new fields, `declaring_unit`/`is_unit_private`, on `Symbol`
      and `ProcSymbol` (`common.h`), stamped by `add_var()`/
      `add_array_var()`/its 2D/ND/record-array siblings, and
      `add_proc()`, from two new parser-global state variables
      (`current_unit_name`/`current_section_is_implementation`) set
      inside `load_unit()` exactly like `current_filename`/the lexer
      position already are (reset in `parse_ast()`, saved/restored
      around `load_unit()`'s own body for correct nested-`uses`
      behavior). One shared check, `symbol_visible_here()`, and two new
      wrapper functions, `find_var_soft_visible()`/`find_proc_visible()`,
      swapped in at exactly the reference-site call sites the audit
      identified - `find_var()` itself needed no call-site changes at
      all (all 8 of its own callers are reference sites, so making
      `find_var()` visibility-aware internally, by swapping its own one
      `find_var_soft()` call for `find_var_soft_visible()`, covered
      every one of them for free). Found and fixed one real gap while
      auditing: `find_file_var_soft()` (used for `read`/`write`/`eof`'s
      leading file-argument detection) was another thin wrapper directly
      around `find_var_soft()` that the original call-site count missed
      entirely, since it isn't literally named `find_var`/`find_proc` -
      caught by reading the code rather than trusting the grep count.
      A private symbol referenced from outside its unit reads exactly
      like a genuinely undeclared one (`Unknown identifier`/`Undeclared
      procedure/function`) - reuses the existing not-found error paths
      rather than a distinct "private" message, verified to be the
      identical message a truly-undeclared identifier already produces.

      Deliberately NOT covered, documented in docs/LANGUAGE.md#units
      rather than silently dropped: consts, types (including classes),
      enum types/values, and subrange types stay visible everywhere
      regardless of section (the other ~65 call sites across those 7
      lookup functions); record variables specifically, as opposed to
      plain scalar/array ones (a separate table, `record_vars[]`, never
      audited - outside what was scoped and approved); the pre-existing,
      separately-documented flat-namespace collision gap (two units each
      privately declaring the same proc/var name still collide as a
      duplicate declaration - real per-unit name mangling would be a
      materially different, larger feature). No new opcodes - purely a
      parse-time concept, so no VM/solas/desole changes either. See
      `examples/test/units/test_units_visibility_*.pas` and
      [docs/LANGUAGE.md](LANGUAGE.md#units).

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
- [x] `assert` — `assert(cond)` / `assert(cond, message)`, a new
      NODE_ASSERT AST node compiling to a single new opcode (`OP_ASSERT`,
      pop message then condition, abort with that message if false). A
      missing message is synthesized as a literal ("Assertion failed")
      at parse time, so codegen/the VM never handle a "no message" case
      separately — see [docs/LANGUAGE.md](LANGUAGE.md#assert).
- [x] `ParamCount`/`ParamStr` — command-line argument access, following
      Turbo Pascal/Free Pascal's own convention (standard/ISO Pascal
      never defined this at all). No new AST node - both reuse the
      existing `NODE_BUILTIN_CALL` mechanism `eof`/`length`/`upcase`/
      etc. already use, dispatched on `node->op` in `codegen.c`'s
      single shared case. Two new opcodes (`OP_PARAM_COUNT`/
      `OP_PARAM_STR`, both `OPERAND_NONE`), backed by a new
      `vm_set_program_args()` (`vm.h`/`vm.c`) that interns each
      argument string into `string_pool[]` via the VM's own existing
      `vm_intern_string()` (the same generic runtime string-interning
      helper `readln`/string-concat already use) - called from
      `solvm.c`'s `main()` after `load_bytecode()` (so the compiled
      program's own static strings keep their original indices) and
      before `run_vm()`. Needed a `solvm` CLI change too: `[-v]
      <input.bin>` only ever accepted one positional argument before;
      now `[-v] <input.bin> [program-args...]` forwards everything
      after the `.bin` path, verbatim (even something that looks like
      a flag), to the running program - `-v` is only ever recognized
      *before* the `.bin` path, so there's no ambiguity between
      `solvm`'s own flags and the program's. `ParamStr(0)` is the
      `.bin` path itself; `ParamCount` excludes it, matching real
      Pascal's own convention. An out-of-range `ParamStr(i)` returns an
      empty string, not a runtime error - a deliberate departure from
      this VM's usual abort-on-out-of-range convention for array
      indexing, chosen specifically to match the real, documented
      behavior of the reference implementations this feature is
      modeling itself after. Surfaced one narrow, expected side effect
      of adding any new keyword: an existing, unrelated tech exercise
      (`examples/tech/param_count.pas`, testing a wrong-argument-COUNT
      error message, coincidentally named `program ParamCount;`) had to
      be renamed (`arg_count_mismatch.pas`, `program
      ArgCountMismatch;`) since its old name is now a reserved word -
      confirmed via a full `examples/` grep that nothing else collides.
      See `examples/test/args/test_args_*.pas` and
      [docs/LANGUAGE.md](LANGUAGE.md#built-in-functions-and-procedures).
- [x] `try`/`except`/`raise` — `raise <message>;` (any string/char
      expression) unwinds straight to the innermost active `try`'s
      `except` block, however many procedure calls deep it's nested;
      `ExceptMessage` (built-in, no-arg, reuses the same
      `NODE_BUILTIN_CALL` mechanism `ParamCount`/`ParamStr` already
      share) reads the caught message back inside the handler.

      Deliberately does NOT expose the VM's own built-in runtime errors
      (division by zero, array-index-out-of-range, nil-pointer
      dereference, stack overflow, ...) to `except` — those remain
      always-fatal exactly as before this feature. Investigated the
      full "catch everything" version first: it would mean routing all
      ~90 of `vm.c`'s scattered inline `fprintf(...); fatal_abort();`
      error sites through one new choke-point function first, a much
      larger and riskier mechanical refactor of `vm.c` for comparable
      value. Chose the smaller, self-contained "only what Pascal code
      explicitly raises is catchable" scope instead (an explicit user
      decision, not a default) - it ships the full raise/catch
      mechanism now, with catching built-in VM errors left as a
      natural, separately-scoped follow-up.

      Needed genuinely new VM machinery, not just new syntax: a
      VM-internal exception-handler stack (`vm_except_stack[]` in
      `vm.c`, new `MAX_EXCEPT_DEPTH` constant in `common.h`) that a new
      `OP_TRY` opcode pushes a `{sp, call_sp, fp, frame_sp, handler_ip}`
      snapshot onto - every one of the VM's own state variables that
      together define "where execution currently is". A `raise`
      (`OP_RAISE`) many calls deep can therefore unwind straight back
      to an enclosing `try`, however many `vm_call_stack[]` frames that
      spans, by simply *overwriting* those four values from the
      snapshot rather than actually popping `vm_call_stack[]` one frame
      at a time the way `OP_RET` does (`RET` only ever pops exactly
      one) - genuinely new capability, since nothing in this VM before
      this feature ever jumped across more than one call frame at
      once. Restoring `sp` specifically is what discards any partial
      expression-evaluation or in-progress-call-argument garbage left
      on the operand stack by whatever was interrupted. Deliberately
      needed NONE of `error.c`'s existing `setjmp`/`longjmp`
      `fatal_abort()` machinery for the catching path at all - every
      value being restored is already VM-local state living inside
      `run_vm()`'s one C stack frame/dispatch loop, so a `raise` is
      just another jump-based control-flow opcode from the dispatch
      loop's own perspective, same as `JMP`/`JZ`. `fatal_abort()` is
      only reached, completely unchanged, for the *uncaught* case (no
      active handler) - preserving exactly the "print message, abort
      the whole program" behavior every other runtime error already
      has.

      `OP_END_TRY` pops the handler when the try-body completes with no
      exception (so it can't be re-entered from code the try-body falls
      through to). `OP_RAISE`, on a catch, pops (consumes) the handler
      it's jumping to *before* jumping - so a `raise` executed from
      inside the except-body itself correctly reaches the *next*
      enclosing `try`, not the one that just fired again, verified with
      a dedicated re-raise test. Four new opcodes total (`TRY` takes an
      address operand like `JMP`/`CALL`; `END_TRY`/`RAISE`/
      `EXCEPT_MSG` take none, like `ASSERT`/`RET`), appended at the end
      of the `Opcode` enum per the lesson already learned this session
      from the vtable feature's mid-enum insertion mistake.
      `type_checker.c` needed one new case (the raised message must be
      string/char-typed, mirroring `assert`'s own message-type check);
      confirmed by direct code reading (not assumed from the `assert`
      precedent alone) that `optimizer.c` needed no changes at all -
      both `mark_used_variables()` and `sweep_dead_assignments()`
      already recurse generically into any node type they don't
      specifically special-case. solas/desole round-trip verified (new
      opcodes were added). See
      `examples/test/except/test_except_*.pas` and
      [docs/LANGUAGE.md](LANGUAGE.md#try--except--raise).
- [x] `try`/`finally` — `try <body> finally <cleanup> end;`, guaranteed
      cleanup that runs whether or not `body` raised, then lets any
      in-flight exception keep propagating outward afterward. A
      separate, parallel construct from `try`/`except` (never combined
      in one block, matching Delphi's own restriction - nest to get
      both).

      Confirmed the "smaller follow-up, reuses the same `OP_TRY`-style
      snapshot mechanism" framing exactly: **zero `vm.c` changes, zero
      new opcodes, zero new `NodeType`** - `OP_TRY`/`OP_END_TRY`/
      `OP_RAISE`/`OP_EXCEPT_MSG` are reused completely unmodified, and
      `NODE_TRY` itself is reused (discriminated via its own previously
      write-only-default `op` field, `op == TOKEN_FINALLY`) rather than
      adding a new node type. The key design insight: `cleanup`'s AST
      subtree is compiled TWICE by `codegen.c` - once inline for the
      normal-completion path, once as the "handler" address `OP_TRY`
      points at for the exception-unwind path, with the second copy
      ending in `EXCEPT_MSG; RAISE` to re-raise and keep propagating
      (since by the time that copy runs, `OP_RAISE` already popped this
      `try`'s own handler entry, so the re-raise correctly searches from
      the *next* enclosing handler outward, with no new opcode needed to
      express "continue unwinding after cleanup"). An exception raised
      from inside `cleanup` itself naturally supersedes the original as
      an emergent property of the same mechanism, no special-casing
      needed.

      **One genuine hazard found and guarded against, not discovered the
      hard way**: `codegen.c`'s label table (`label_table[idx].code_idx`)
      is a single scalar, unconditionally overwritten on every
      `NODE_LABEL` visit - a labeled statement inside `cleanup` would,
      under double-compilation, get treated as a backward goto into the
      FIRST copy while the SECOND copy's own label position isn't even
      known yet, a genuine miscompile corrupting the try-vs-normal-path
      invariant. Confirmed reachable (not theoretical): this compiler's
      goto/label model is deliberately permissive about labels appearing
      inside structured statements (`docs/LANGUAGE.md`'s own documented
      simplification). Fixed with a new `finally_body_depth` counter
      (mirroring the existing `loop_depth` idiom exactly), checked at
      `NODE_LABEL`'s own creation site - catches labels nested arbitrarily
      deep inside the finally-body for free, since parsing under the
      counter naturally covers every nested statement form. A `goto`/
      `break`/`continue` referencing something OUTSIDE the finally-body
      is NOT rejected - confirmed safe, since every pending-jump
      structure a statement can register into (`label_table[idx].
      pending_jumps[]`, loop-context `continue_jumps[]`/`break_jumps[]`)
      is array-based and already tolerates multiple sources correctly.

      `goto` jumping OUT of an active `try`/`finally` body (skipping
      `END_TRY`, leaking a stale `vm_except_stack[]` entry) is an
      inherited characteristic of the already-shipped `OP_TRY`/
      `OP_END_TRY` pairing - confirmed no worse than the identical
      pre-existing gap `try`/`except` already has, not newly introduced.
      `type_checker.c`/`optimizer.c` needed zero changes (confirmed:
      `NODE_TRY` already had no case in either, relying entirely on
      their generic recursion into `left`/`right`/`next`/`extra`, and
      `optimizer.c`'s single pass completes before codegen's double-visit
      ever happens, so it only ever sees the finally-body once). See
      `examples/test/finally/test_finally_*.pas` (5 positive cases
      including a deep-call-frame unwind and cleanup-itself-raises, and 2
      error cases) and [docs/LANGUAGE.md](LANGUAGE.md#try--finally).
- [x] Procedural types / function pointers — `type TProc = procedure(x:
      integer); TFunc = function(x: integer): real;` declares a NAMED
      procedural type (Turbo Pascal's form; standard Pascal only allows
      a procedure/function as a formal parameter written inline, never
      as a named, storable type - see Functional/procedural parameters
      in Phase 1 for that narrower, older mechanism, which this reuses
      nothing from at the storage level). Encoded as `TYPE_PROC_BASE +
      its proc_types[] index`, mirroring `TYPE_POINTER_BASE`/classes
      exactly - the runtime representation is a plain int (a top-level
      procedure/function's entry address, or -1 for `nil`), so a
      variable/local/`var`-parameter of this type reuses every existing
      scalar mechanism completely unmodified; only assignment and
      calling through it needed dedicated parsing. Supports `nil`
      (assignable and `=`/`<>`-comparable, exactly like a pointer) and
      copying between two variables of the same procedural type. A new
      `NODE_PROCVAR_CALL` node (deliberately separate from the older
      `NODE_CALL_INDIRECT`, whose shape is hardcoded to "the callee
      always lives in a local frame slot" - doesn't fit a plain global)
      handles the call; a bare reference used as a whole STATEMENT
      defaults to calling it (matching how any other zero-argument
      call already works bare), but in expression/read context (`p =
      nil`, `p2 := p1`, an argument) it's just the value unless
      immediately followed by `(` - avoiding the ambiguity a
      "bare reference always calls" rule would create for comparisons
      and copies. Record/class fields of this type, passing one as an
      argument to another procedure, and function return values of
      this type are all explicit v1 gaps (the last one is its own
      separate, harder roadmap item just below, which needs this one
      first). See [docs/LANGUAGE.md](LANGUAGE.md#procedural-types).
- [x] Functions/procedures as return values — a function (or class
      method) returning a reference to another top-level function/
      procedure, as a named procedural type. Smaller than it looked:
      `function GetHandler: TProc;` as a header already parsed fine
      before this (`parse_scalar_type()` already had a procedural-type
      branch, used generically everywhere including return types) - the
      actual gaps, found by testing directly rather than guessing, were
      both in `parser.c`, no new AST nodes/opcodes/`.bin` format changes
      anywhere. Gap 1: assigning to a function's own name
      (`GetHandler := Double;`) always parsed the RHS via the generic
      `expression()`, which misparses a bare proc name as a zero-
      argument call rather than a reference - fixed by routing a
      procedural return type through `parse_proc_value()`, the same
      specialized parser every other procedural-type assignment target
      already used; this one shared branch in `statement()` covers a
      class method's own return-value assignment for free, since method
      bodies go through the identical code path. Gap 2: even once a
      function could return one, nothing could call it and use the
      result - `parse_proc_value()`/`parse_proc_argument()` both
      treated any bare proc name as "take a reference," never "call it
      and use its return value." Both gained an explicit-`(` check:
      with parens, treat it as a call whose RETURN TYPE (not its own
      callable shape) must match the target, building a plain
      `NODE_CALL` instead of a `NODE_PROC_REF`; no parens keeps the
      existing bare-reference behavior, unchanged. Deliberately NOT
      signature-based inference - even a zero-argument function needs
      explicit `()` here, since this context is inherently ambiguous
      between "reference" and "call" (documented in
      docs/LANGUAGE.md#procedural-types).

      Testing surfaced a third, adjacent gap beyond the original two:
      calling a CLASS METHOD (not just a plain function) that returns a
      procedural value - e.g. `h := f.MakeHandler();` - hit "Undeclared
      procedure/function 'f'", since `parse_proc_value()`'s single-
      bare-identifier lookup has no notion of a dotted method-call
      expression at all. Fixed with a fallback: when the identifier
      isn't a plain top-level proc but IS a real known variable (just
      not of the target type on its own), hand off to the general
      `expression()` (which already knows how to parse and type-check a
      method call, self-shorthand, etc. in full) instead of erroring -
      gated specifically on "is this name known at all" so a genuinely
      undeclared identifier still gets the original, more specific
      `Undeclared procedure/function` message, not a generic fallback
      one (caught via the full `examples/` regression sweep: an
      existing test, `test_proctype_bad_undeclared.pas`, had its error
      message silently degrade before this gating was added).
      `parse_proc_argument()`'s own version of this fallback additionally
      has to validate the signature manually, since it feeds into
      `type_checker.c`'s generic per-argument check, which deliberately
      skips validating a procedural parameter slot (trusting the parser
      to have already checked it, via a shared `TYPE_INTEGER` placeholder
      convention every branch in both functions uses for exactly this
      reason). See `examples/test/proctype/test_proctype_return_*.pas`
      and [docs/LANGUAGE.md](LANGUAGE.md#procedural-types).
- [x] Record/class field of procedural type — a `record`/`class` field
      can itself be a named procedural type. Turned out much bigger than
      expected once tested directly: a PLAIN (non-class) record field
      already worked with zero changes at all (record field assignment
      already routed through the ordinary procedural-type-assignment-
      target machinery every plain var/local already used) - but a
      CLASS field needed five distinct fixes, since class field access
      goes through a completely different, heap-based mechanism
      (`resolve_heap_deref_step()`/`build_heap_deref_write_statement()`/
      `parse_heap_deref_read()`) that had no notion of procedural types
      at all before this, none of them new AST nodes/opcodes:
      1. `build_heap_deref_write_statement()`'s two branches (ordinary
         field and array field) both needed the same procedural-type
         check `parse_proc_value()`'s own callers already use elsewhere -
         a bare proc name being assigned to a procedural-typed field was
         being misparsed as a zero-argument call to it.
      2. `parse_heap_deref_read()`'s end-of-chain return needed an
         `is_proc_type(...) && token.type == TOKEN_LPAREN` check
         (mirroring the existing NAMED-procedural-type-global case) so
         `f.handler(5)` calls THROUGH the field's stored value via
         `build_procvar_call()` - which turned out to already be fully
         generic over its `callee` argument (a `NODE_HEAP_FIELD_ACCESS`
         works exactly like a `NODE_VARIABLE` there), so no codegen.c
         changes were needed either.
      3 & 4. The SAME check again, separately, in both
         `parse_heap_deref_read()`'s and `parse_self_shorthand_read()`'s
         own array-field branches - an array field access is always a
         terminal step (returns immediately, per the existing classes
         scoping decision), so it can't just fall through to the shared
         check above - the exact same duplication that caused a real,
         shipped bug during the array-typed-fields feature itself,
         caught this time by testing the array case explicitly rather
         than assuming the scalar fix would cover it.
      5. `parse_class_method_call_arguments()` (a method call's own
         argument list) had a stale comment claiming method parameters
         are "guaranteed scalar" and only ever needed the plain-scalar/
         `var`-scalar cases - true for array/record parameters, but not
         for a NAMED procedural type, which needed the same
         `parse_proc_value()` routing as every other procedural-type
         target.

      Nested-record fields (a procedural field inside another record
      field, itself inside a class) needed no extra fix at all - a
      nested chain is already resolved to a single combined offset by
      `resolve_heap_deref_step()` before the write/read machinery ever
      sees it. Passing a class field's procedural value as an argument
      to a plain top-level function already worked via
      `parse_proc_argument()`'s own existing fallback (added for the
      return-values feature just above); passing it to a class METHOD's
      own procedural parameter needed fix 5 above, combined with that
      same existing fallback inside `parse_proc_value()`. See
      `examples/test/proctype/test_proctype_field_*.pas` and
      [docs/LANGUAGE.md](LANGUAGE.md#procedural-types).
- [x] Static (persistent-across-calls) local variables — `static name:
      type;` reuses the exact "hidden mangled global" trick local arrays
      already use (which are already implicitly persistent), so every
      reference resolves to an ordinary global instead of a frame slot.
      Scalars only (a local array is already persistent by default).
      Shared correctly across recursive calls; two procedures' own
      same-named static don't collide (mangled `__static_proc_name`) —
      see [docs/LANGUAGE.md](LANGUAGE.md#static-local-variables).
- [x] Class members — `class var Name: Type;` (one shared global per
      class hierarchy, not per instance), `class procedure/function
      Foo(...);` (a TRUE class method - Delphi terminology - callable as
      `TMyClass.Foo(...)`, no instance, no implicit `self`), and `class
      property Name: T read GetX [write SetX];` (mirrors the shipped
      instance-property mechanism, backed by a class var/class method
      instead of an instance field/method).

      Genuinely MORE zero-runtime-footprint than Properties was: a class
      var is representationally identical to any other global
      (`NODE_VARIABLE`/`NODE_ASSIGN`, completely unmodified), and a class
      method call is a plain `NODE_CALL` (no vtable, no `self` splice) -
      simpler than Properties' own getter/setter machinery, which still
      needed `NODE_VIRTUAL_CALL`'s self-splicing dance. Confirmed by
      direct reading: zero new `NodeType`s, zero new `Opcode`s, and zero
      changes needed in `type_checker.c`/`optimizer.c`/`ast_printer.c`/
      `codegen.c`/`vm.c`/`solas.c`/`desole.c` - a `parser.c`-only
      feature.

      The one genuinely new parsing capability: `TMyClass.Foo(...)`
      starts with a CLASS TYPE NAME, not a variable, at the exact
      syntactic position an ordinary variable reference already occupies
      (the start of `factor()`'s or `statement()`'s identifier
      resolution) - unlike Properties or `is`/`as`, which each had a
      syntactically unambiguous position to hook a type-name lookup
      into. Solved with a genuine one-token lookahead
      (`try_resolve_class_qualified_access()`, using the same
      `lexer_save_pos()`/`lexer_restore_pos()` primitives the field
      loop's own `class var` peek already uses) that falls through to
      the caller's completely unchanged existing resolution chain unless
      the leading identifier is a known class name immediately followed
      by `.`.

      Class methods reuse the exact same `methods[]` table, `"%s__%s"`
      mangling, `add_proc()`/`find_proc()`, and inheritance-copy
      machinery an instance method already uses - `is_class_method` is
      the only new discriminant flag, not a separate array. Two pitfalls
      caught before any code ran (both would otherwise have been silent
      bugs): the pre-existing method-loop's override-eligibility check
      needed an explicit exclusion so a class method can never be
      accepted as an "override" (no vtable slot is ever allocated for
      one - redeclaring an inherited class method is a duplicate-name
      error instead); and an instance property backed by a class method
      target needed a defensive reject guard, since
      `resolve_heap_deref_step()`'s existing getter-call path
      unconditionally splices `self` into the call, which would silently
      corrupt the stack against a class method (no `self` parameter at
      slot 0) rather than just fail to compile.

      Class vars are inherited BY REFERENCE, not by copy - a subclass's
      own `class_vars[]` entry is struct-copied from the parent
      UNCHANGED (same `sym_idx`, never re-registered), so the mangled
      global is genuinely SHARED between `TBase.X` and `TSub.X`, matching
      real Delphi class-var semantics automatically (verified with a
      dedicated test).

      Scope cuts for v1 (see
      [docs/LANGUAGE.md](LANGUAGE.md#class-members)'s "Not implemented
      yet"): no instance-qualified access to a class member (`c.Total` is
      a compile error naming the correct `TCounter.Total` form instead -
      the single biggest scope decision, avoiding a restructure of
      `resolve_heap_deref_step()`'s heavily-tested field-backed property
      logic to understand a second backing-table shape); no class method
      virtual dispatch/overriding; `inherited` and bare instance-member
      access are both rejected inside a class method body (no `self`
      exists there). See `examples/test/classmember/test_classmember_*.pas`
      (7 positive cases including inheritance-shared-storage - the one
      most likely to silently regress if a class var were accidentally
      copied instead of shared - and 14 error cases covering every
      rejection path) and [docs/LANGUAGE.md](LANGUAGE.md#class-members).
- [x] Abstract methods/classes — a trailing `abstract;` modifier after a
      method header (`function Area: real; abstract;`), giving it NO
      body, ever, plus a compile-time block on `new()`-ing any class
      with an unresolved abstract method (including one merely
      inherited, never overridden - propagates through the hierarchy
      automatically). Closes the actual known v1 gap this bullet used
      to describe: today you can declare a method with no body as long
      as nothing calls it, but calling it through a BASE-typed reference
      (the entire point of a genuine abstract-base-class pattern)
      required the base class to have some (meaningless, dummy)
      implementation anyway, since the existing "doesn't have a body
      yet" call-site check is keyed off the STATIC type. **Deliberately
      just `abstract;`, not Delphi's `virtual; abstract;`** - this
      compiler has no `virtual`/`override` keyword at all (every
      instance method is already always virtually dispatched), so a
      second, redundant keyword would be pure noise.

      **The key design insight**: rather than relaxing the ~8 existing
      "doesn't have a body yet" call-site checks for `is_abstract`
      (which would also break argument-count/type validation, since
      those need a real `proc_table[]` signature to check against),
      register a PHANTOM, permanently bodyless `proc_table[]` entry for
      an abstract method's mangled name at the moment its header is
      parsed (NOT `forward` - a real forward declaration is later
      *required* to be completed; a phantom abstract entry never is and
      never should be). This makes `find_proc()` succeed universally for
      an abstract method, so all ~8 call sites, and
      `build_vtable_init_chain()`, work completely unmodified - a call
      through a base-typed reference compiles exactly like a call to a
      concrete method, dispatching correctly via the stable vtable slot
      index at runtime. `generate_code(NULL)` (already a confirmed
      no-op) means the phantom compiles to a tiny dead `OP_RET` stub,
      provably unreachable (the declaring class can never be
      instantiated while any method stays abstract, and dispatch always
      resolves via the RUNTIME tag's own vtable row) - **zero
      `codegen.c` changes**, zero new `NodeType`s, zero new `Opcode`s.
      "Is this class safe to instantiate" checks the `is_abstract` FLAG
      directly, not `find_proc()` (the phantom makes that always
      succeed) - propagates through inheritance for free via the
      existing, unmodified struct-copy mechanism.

      **One required exception, caught during design validation, not
      left as a silent bug**: `parse_inherited_call()` builds a
      STATICALLY resolved `NODE_INHERITED_CALL` (unlike the other sites'
      dynamically-dispatched `NODE_VIRTUAL_CALL`), backpatched straight
      to the phantom's own `entry_address` - without an explicit
      `is_abstract` guard there, `inherited AbstractMethod(...)` would
      have silently compiled and, at runtime, actually executed the
      near-empty stub, returning garbage for a function. Fixed with its
      own explicit rejection, verified via a dedicated test.

      **A pre-existing, unrelated bug found and flagged, not fixed**:
      an instance method header's own parameter count is capped at
      `MAX_PARAMS` with no headroom reserved for the implicit `self`
      slot every instance method gets - a method declared with exactly
      `MAX_PARAMS` params already overflows `proc_table[].param_types[]`
      by one slot today, for ANY real method body, independent of this
      feature. The phantom-registration helper mirrors this exact
      existing pattern and reproduces the identical overflow at the
      identical boundary - flagged in a code comment at the phantom
      helper, deliberately not fixed here (out of scope: affects
      ordinary methods too, not introduced by this feature).

      Scope cuts for v1 (see
      [docs/LANGUAGE.md](LANGUAGE.md#abstract-methods)'s "Not
      implemented yet"): no `class abstract`/type-level keyword -
      abstractness is fully emergent from having ≥1 unresolved abstract
      method, matching real Delphi (which has no such keyword either);
      a TRUE class method can never be abstract (rejected at
      declaration - class methods are never overridable, so it could
      never get an implementation). See
      `examples/test/abstract/test_abstract_*.pas` (4 positive cases
      including the flagship base-typed-reference dispatch scenario and
      a 3-level-deep multi-abstract hierarchy, and 6 error cases
      covering every rejection path) and
      [docs/LANGUAGE.md](LANGUAGE.md#abstract-methods).
- [x] Virtual destructors — `destructor Destroy;` as a new alternative
      to `procedure`/`function` when declaring a class method, marking
      it as the class's ONE designated destructor: `dispose(c)` now
      calls it (dynamically dispatched, exactly like any other virtual
      method call) right before actually freeing the instance.

      **Deliberately ships only half of the bundled survey idea below**
      - no implicit `TObject` root class. The destructor mechanism works
      on ANY class, with or without an explicit parent, found by
      checking whether it (or an ancestor, via the same struct-copy
      inheritance mechanism that already propagates `is_abstract`
      automatically) has a method flagged as the destructor. A universal
      root type earns its keep in real Delphi because its whole
      ecosystem (generic containers, `TObject`-typed collections, RTTI)
      assumes one common ancestor - this compiler has none of that yet,
      so retrofitting every class's `parent_class_ptr_idx` would be
      unrequested architecture with no consumer. Zero new opcodes, zero
      new `NodeType` - reuses `NODE_VIRTUAL_CALL`/`build_vtable_init_
      chain()` completely unmodified (a destructor is an ordinary
      instance method under the hood, just carrying one new
      `is_destructor` flag).

      **A silent-corruption hazard found and guarded against before any
      code ran**: `proc_param_headers_match()` (the existing override-
      signature check) deliberately doesn't compare `is_destructor`
      (same reasoning as `is_abstract`/`is_class_method`) - without an
      explicit check, a subclass could "override" an inherited
      `destructor Destroy;` with a plain `procedure Destroy;` of the
      same signature, silently replacing the entry with one that has
      `is_destructor = 0`, breaking `dispose()`'s ability to find it in
      that subclass with no error at all. Fixed with an explicit
      uniqueness + kind-mismatch check ahead of the existing override/
      duplicate chain.

      **A missing parse path found during design validation, not left
      as a gap**: a class declaration only registers method HEADERS -
      the body is written separately at the top level via
      `subroutine_declaration()`, and every one of its four call sites
      (unit interface, unit implementation, main program, nested
      procedures) gated on `procedure`/`function` only, with no path at
      all for `destructor TFoo.Destroy; begin ... end;`. Fixed by
      threading a third `is_destructor_decl` parameter through
      `subroutine_declaration()`/`parse_class_method_body()` and all
      four call sites, with its own kind-mismatch check mirroring the
      existing `is_function` one (needed separately - a destructor and
      an ordinary procedure both have `is_function == 0`, so the
      existing check alone can't tell `destructor Destroy;` and
      `procedure Destroy;` apart).

      `dispose(c)`'s own synthesized destructor call deliberately
      bypasses `is_private` (unlike an explicit `c.Destroy;` call, which
      still goes through the ordinary, unmodified private-access check) -
      a private destructor becomes reachable only through `dispose()`,
      matching Delphi's own `Free`/`Destroy` split and the actual point
      of being able to make one private at all. Restricted to a plain
      variable/local/`var`-parameter target - a `^`-deref chain is
      rejected with a compile error when the class has a destructor
      (confirmed NOT as narrow as it first looks: this compiler
      flattens record fields, including class-pointer ones, into their
      own slots at parse time, so `dispose(myRecord.classField)`
      already resolves to a plain-variable node and isn't excluded).

      **A pre-existing, unrelated double-free hazard flagged, not
      fixed**: `OP_DISPOSE` has no already-freed guard (only nil/
      out-of-range are checked), so `dispose(self)` from inside that
      instance's own destructor corrupts the free list - not new here
      (`dispose(c); dispose(c);` already has this hazard today), but the
      destructor feature makes it much easier to trigger by accident.
      Documented with an explicit warning, matching this codebase's
      existing "the pointer's value is undefined after dispose"
      precedent for a similar footgun. See
      `examples/test/destructor/test_destructor_*.pas` (6 positive cases
      including virtual dispatch + `inherited` chaining together through
      a base-typed variable, and 10 error cases covering every rejection
      path) and [docs/LANGUAGE.md](LANGUAGE.md#destructors).

### Shipped from the OOP features survey

Pulled out of docs/ROADMAP.md's own "Object-oriented language features
under consideration" survey once shipped; the rest of that survey (still
open) stays there.

**Good fit — reuse existing machinery:**

- [x] Sealed classes — `class sealed ... end;` / `class
      sealed(TParent) ... end;` marks a class unable to be subclassed;
      `class(TSealedOne) ... end;` later is a `compile_error()` naming
      the sealed class. Confirmed by design validation to be the
      cheapest OOP feature shipped so far: one new token
      (`TOKEN_SEALED`, added exactly like `TOKEN_ABSTRACT` was), one new
      `PointerTypeDef.is_sealed` field, one parse-time check at the
      single place a `class(ParentName)` ancestor ever resolves
      (`parse_class_declaration()`) - zero new `NodeType`/`Opcode`, zero
      `codegen.c`/`type_checker.c`/`vm.c` changes.

      **A stale-slot leak confirmed real by an existing precedent in
      this same file, not a hypothetical**: `pointer_types[]` is a
      static array reused across compiles in the same process (only
      `pointer_type_count` resets to 0 between compiles - see
      "Global state, not parameters" in
      [ARCHITECTURE.md](ARCHITECTURE.md)); the plain-pointer-type path
      (`type PFoo = ^Target;`) already carries an explicit comment
      documenting this exact hazard and resets `method_count`
      accordingly. `is_sealed` follows the same discipline: assigned
      unconditionally every time a class is declared
      (`pt->is_sealed = class_is_sealed;`, never an `if`-only
      assignment with no else), so a sealed flag from an earlier
      compile's class can never leak onto an unrelated class reusing
      the same `pointer_types[]` slot in a later same-process compile.

      Scope: sealing isn't inherited - it's checked once, only at the
      point a class is used as someone else's parent, so a sealed
      class's own ancestor (if any) is unaffected. A class can be both
      sealed and (via an abstract method) abstract at once - legal but
      self-defeating (neither subclassable nor instantiable) - not
      specially rejected, matching Delphi's own behavior for the same
      combination. See `examples/test/sealed/test_sealed_*.pas` (3
      positive cases including sealed-with-parent and the sealed+
      abstract combination, 1 error case) and
      [docs/LANGUAGE.md](LANGUAGE.md#sealed-classes).

**Moderate — needs some new machinery:**

- [x] `const`/`out` parameters — `const name: type` (read-only) and
      `out name: type` (an output parameter, no read-guarantee on the
      caller's incoming value) both on ordinary procedures/functions
      and class methods. **Corrects the original survey blurb's own
      guess, based on direct code reading**: `out` isn't a design
      choice about how to implement it - it MUST be by-reference, since
      communicating a value back to the caller is impossible for a
      plain by-value local. Both reuse the existing `var`-parameter
      by-reference machinery (`is_var_param`, `PUSH_LOCAL_REF`/
      `LOAD_REF`/`STORE_REF`, `NODE_VAR_PARAM_READ`/`_ASSIGN`)
      completely unmodified - zero new opcodes, zero new `NodeType`s,
      zero `codegen.c`/`vm.c`/`type_checker.c` changes. `const` adds a
      compile-time-only write-restriction; `out` adds a compile-time-
      only "never assigned" warning - neither changes what's emitted at
      runtime.

      **`const` is SHALLOW, matching real Pascal**: blocks reassigning
      the parameter itself (`p := x;`, `new(p)`, `inc(p)`) but not
      writing through it when it's a pointer/class type (`p.field :=
      x;` on a `const p: TFoo` stays legal - `const` protects the
      reference, not what it points to). This needed guards at exactly
      4 `NODE_VAR_PARAM_ASSIGN` construction sites (`statement()`'s
      procedural-type-reassign and plain-scalar branches, `inc`/`dec`,
      `new()`'s direct-target branch) while deliberately leaving the
      pointer-deref-write branch and `new()`'s own `^`-chain branch
      unguarded - confirmed by reading that neither of those ever
      builds a `NODE_VAR_PARAM_ASSIGN` targeting the parameter's own
      slot in the first place. A 5th write vector, forwarding a
      `const`-flagged local as another call's own `var`/`out` argument,
      needed a new `callee_is_const` parameter threaded through
      `parse_var_argument()`'s forwarding branch and all 4 of its call
      sites - forwarding a `const` local into another call's own
      `const` parameter stays legal (both read-only either way).

      **Two real bugs found and fixed during design validation, not
      left latent**: (1) a third, missed caller of
      `add_local_var_param()` in the forward-declaration-completion
      replay loop (`parser.c:8123-8156`) - without the fix, `const`/
      `out` on any `forward`-declared procedure would have silently
      lost ALL enforcement the moment its body was completed, since
      that loop rebuilds each parameter's `LocalSymbol` from
      `proc_table[]`'s stored flags rather than re-parsing the header;
      confirmed fixed with a dedicated positive test
      (`test_constout_forward_const_to_const.pas`, exercising a
      `forward`-declared `const` parameter end to end) plus a smoke
      test proving the write-guard still fires post-completion. (2) the
      top-level-procedure parameter loop's `is_var_group` flag had to
      also be set by `const`/`out` (not tracked as a separate,
      independent flag) - `is_var_group` is what gates the whole-record
      rejection, the `add_local_var_param()` vs. plain `add_local()`
      dispatch, AND what gets stored into `param_is_var[]`; treating
      `const`/`out` as fully separate would have silently made them
      by-VALUE (contradicting the whole "`out` must be by-reference"
      premise) and reopened the whole-record restriction as a
      const/out-specific hole `var` itself doesn't have.

      Deliberate v1 scope cut: `const`/`out` recognized only on
      ordinary procedure/function/class-method declarations - not
      inside a procedural/functional parameter's own inline signature,
      not in a named procedural type - both threaded through a new
      `allow_const_out` gate on `parse_proc_signature_tail()`/
      `parse_proc_param_header()`, rejected with a clear message
      otherwise. Inherits every existing `var`-parameter restriction
      for free (addressable-variable arguments only, no whole records/
      array elements, no `readln` target, no `for`-loop counter). See
      `examples/test/constout/test_constout_*.pas` (5 positive cases
      including the shallow-const mutation and the forward-declaration
      regression test, 6 error cases, 1 warning case) and
      [docs/LANGUAGE.md](LANGUAGE.md#const-parameters).

- [x] Default/optional parameter values — `= <const-expr>` on a trailing
      run of parameters; a call that omits trailing arguments gets the
      default spliced into the argument list at parse time, exactly as
      the original ROADMAP blurb anticipated - pure call-site sugar,
      zero `codegen.c`/`type_checker.c` changes, since both already just
      walk whatever argument nodes are in the AST's call-argument list
      and can't tell a spliced default from one the caller wrote.

      **Not on `var`/`const`/`out` parameters** - all three are
      by-reference in this compiler (an address, not a value; see the
      `const`/`out` entry above), and a default literal has no
      caller-side lvalue to take the address of. Stricter than some
      other Pascal dialects, which allow defaults on `const` because
      their `const` isn't always by-reference under the hood - here it
      always is, so the restriction follows from the implementation,
      not a language-design choice. Also not on array/record parameters
      (no literal syntax exists for either), not on subrange-typed
      parameters (a documented v1 scope cut avoiding declaration-time
      bounds-check complexity), and not inside a procedural/functional
      parameter's own inline signature or a named procedural type -
      reusing the exact same `allow_const_out`-gated boundary `const`/
      `out` already draw for the one grammar (`parse_proc_signature_
      tail()`) that has such a flag to reuse.

      **Two real gaps found during design validation, before any code
      was written**: (1) class-method defaults would have silently
      vanished by call-site time - `register_class_method_param()` and
      `register_abstract_method_signature()` turn a class method's
      `ProcParamHeader` into its real, call-site-visible `proc_table[]`
      entry FIELD-BY-FIELD, entirely separately from the inherited-
      method flat-struct-copy path that would have carried new fields
      "for free"; both now explicitly thread `param_has_default`/
      `param_default_type`/`param_default_value` through. (2) those same
      two functions already follow an explicit defensive-zeroing
      discipline for every per-slot field they touch, since
      `proc_table[]` only resets `proc_count` between compiles in a
      long-lived host process (a stale value from an unrelated
      procedure that previously occupied the same slot could otherwise
      leak in) - the 3 new fields needed to join that same discipline,
      unconditionally written on every call regardless of whether this
      particular parameter actually has a default, not just an
      afterthought bolted on separately.

      Also confirmed, not just assumed: the ordinary top-level
      `procedure`/`function` grammar (`subroutine_declaration()`'s own
      inline parameter loop) parses independently of `parse_proc_
      signature_tail()` entirely, so it needed its own from-scratch
      implementation of every restriction above, not a shared gate;
      spliced default nodes need an explicit line number passed in
      (`make_default_value_node()`) rather than relying on
      `create_node()`'s ambient `token.line`, since by splice time
      parsing has moved on past the whole call; a class method **override
      may declare its own, different default** from the method it
      overrides - which default applies is resolved STATICALLY, against
      whichever type the call site's own expression is declared as,
      exactly like C++/Java default arguments, even though the method
      body that runs is still chosen dynamically (confirmed against the
      actual call-resolution code, not assumed by analogy); and a
      default lives on the forward declaration when one exists, needing
      zero extra code, since a completing body can never re-list
      parameters at all under this compiler's existing forward-
      declaration convention. See `examples/test/defaults/test_defaults_
      *.pas` (8 positive cases including the forward-declaration and
      override-static-resolution scenarios, 12 error cases) and
      [docs/LANGUAGE.md](LANGUAGE.md#default-parameter-values).

- [x] `with a, b do` (multiple targets in one `with`) — confirmed as
      cheap as the ROADMAP blurb guessed: `with` has no `NodeType` of
      its own at all (pure parser-time sugar - pushes a `record_vars[]`
      index onto `with_stack`, parses the body, pops), so
      `type_checker.c`/`optimizer.c`/`codegen.c`/`ast_printer.c` needed
      zero changes. `with_stack`/`with_depth`
      (`MAX_WITH_DEPTH 8`) were already a real stack scanned innermost-
      to-outermost by `find_with_field()`, so a later-listed target
      already shadows an earlier one automatically - the entire change
      is wrapping the single identifier-validate-push block in
      `statement()`'s `TOKEN_WITH` branch in a comma-separated loop,
      tracking how many targets this statement pushed so the pop
      afterward is exact regardless of list length. Each target in the
      list is validated independently (must be a non-local record
      variable, no nested-record-typed field), so a bad second-or-later
      target reports the same error, anchored at its own line, as a bad
      first target always did. See `examples/test/with/test_with_multi*.pas`
      (2 positive cases: direct nested-vs-comma-list equivalence, and a
      three-target list proving the loop isn't hardcoded to two; 1 error
      case checking the second target specifically) and
      [docs/LANGUAGE.md](LANGUAGE.md#the-with-statement).

- [x] Unit `initialization`/`finalization` sections — front-end only,
      zero new `Opcode`/`NodeType`, matching the ROADMAP blurb's
      "bounded follow-up" framing. Three existing pieces of machinery
      turned out to cover the whole feature: (1) units are pure
      source-merge with no linker and the VM has no entry-point field at
      all (`run_vm()` always starts at instruction 0) - "main runs
      first" is pure code layout ending in a trailing `emit_halt()`, so
      a unit's `finalization` code just needed to land as extra
      statements appended right before that `HALT`, no VM/bytecode
      change; (2) `loaded_units[]` (only ever appends a unit's name
      *after* everything it transitively `uses` is already merged) was
      already a valid dependency order for free - iterated forward for
      `initialization`, in reverse for `finalization`, no topo-sort
      needed; (3) `statement_list()` already stops on its own at the
      first non-statement-start token, so it parses an un-bracketed
      `initialization`/`finalization` body (no `begin`/`end`) completely
      unmodified.

      The actual change: two new tokens (`TOKEN_INITIALIZATION`/
      `TOKEN_FINALIZATION`); `load_unit()` now optionally parses each
      section right before its closing `end.` and stashes the resulting
      statement chain in two new arrays (`loaded_unit_init[]`/
      `loaded_unit_final[]`) indexed in lockstep with `loaded_units[]`;
      `parse_ast()`'s existing end-of-parse splice point (already used
      to prepend the vtable-init and class-parent-init chains onto the
      main body) gained two more passes - prepend each unit's
      `initialization` chain in reverse load order (so the
      first-loaded unit ends up first, right after the vtable/
      class-parent bookkeeping those chains need already in place), then
      append each unit's `finalization` chain, also in reverse load
      order, to the tail of the whole statement chain.

      **Scope, matching standard Pascal, not new gaps**: no local `var`
      declarations inside either section (a stray one surfaces as a
      plain `match()` "Unexpected token 'var'", no special-casing
      needed); a diamond-dependency unit's sections still run exactly
      once, at its first-load position, for free; `finalization` doesn't
      run if the program terminates via an unhandled runtime error
      (`fatal_abort()`/`longjmp`, which never reaches trailing bytecode)
      - the same scope `try`/`finally` already has outside its own
      handled exception. See `examples/test/units/test_units_initfinal_
      *.pas` (basic single-unit init+final, a diamond-dependency ordering
      test proving the exact `Base, DiamondA, DiamondB` / `DiamondB,
      DiamondA, Base` sequence, and independently-optional sections) and
      [docs/LANGUAGE.md](LANGUAGE.md#initialization-and-finalization).

- [x] Compiler directives (`{$DEFINE}`/`{$UNDEF}`/`{$IFDEF}`/
      `{$IFNDEF}`/`{$ELSE}`/`{$ENDIF}`) — the smallest feature shipped
      yet: entirely inside `lexer.c`, not even a new `TokenType`.
      Directives never produce tokens - a false `{$IFDEF}` branch is
      consumed as raw characters inside the lexer's own `{` handling
      before `next_token()` ever returns to its caller - so `parser.c`,
      `type_checker.c`, `optimizer.c`, `codegen.c`, `vm.c`, and
      `ast_printer.c` needed zero changes. `{$IFDEF FOO}` already
      silently vanished as an ordinary no-op comment before this shipped
      (nothing in `examples/` relied on that), so recognizing `{$` as
      something other than a plain comment was a safe behavior change.

      **The `LexerPos` save/restore struct already used for `uses`-based
      unit loading absorbed the new nesting state for free**: `{$IFDEF}`
      nesting (`ifdef_depth`/`ifdef_in_else[]`) is per-file - reset
      inside `init_lexer()` itself, which already runs once per file
      (main program and every unit) - and rides along inside `LexerPos`,
      so `load_unit()` needed zero edits; its existing generic
      `lexer_save_pos()`/`lexer_restore_pos()` calls around a nested
      unit's own lex pass just carry the extra fields automatically.
      `{$DEFINE}`d symbols, by contrast, are a *separate* table that
      `init_lexer()` deliberately does NOT reset - only a new
      `lexer_reset_defines()`, called once per whole compile from
      `parse_ast()` (mirroring every other per-compile reset already
      there), clears it - so a symbol `{$DEFINE}`d by the main program
      stays visible to a used unit's own `{$IFDEF}`s and vice versa,
      matching real Pascal's "lasts the rest of the compile" semantics
      for defines, while nesting itself stays correctly scoped per file.

      A false `{$IFDEF}`/dead `{$ELSE}` branch is skipped by a new
      `skip_conditional()` scanner that looks for the next literal `{$`
      marker (deliberately not string/comment-aware, matching this
      lexer's pre-existing choice not to special-case strings inside
      plain `{ }` comments either) and tracks its own local nesting
      counter to find the `{$ELSE}`/`{$ENDIF}` matching its own level -
      replicating the existing `current_line++` bump on every `\n` it
      scans over, so line numbers after a skipped block stay correct,
      same discipline the two pre-existing comment-skip loops already
      have.

      **Deliberate v1 scope cut, not a gap**: only the six directives
      above do anything; any other `{$...}` (`{$R+}`, `{$Q-}`, etc.) is
      recognized as directive syntax and silently accepted as a no-op,
      not wired to real behavior - actually implementing `$R+`/`$R-`
      range-check toggling would mean conditionally gating `codegen.c`'s
      existing subrange bounds-check emission, which would no longer be
      a purely lexer-level change, contradicting the ROADMAP's own
      framing for this feature. Also cut: `{$ELSEIF}` chaining (nest
      another `{$IFDEF}` inside `{$ELSE}` instead), `{$INCLUDE}` file
      inclusion, and a `-D` command-line flag to pre-define a symbol
      (`{$DEFINE}` from source is the only way in v1). See
      `examples/test/directives/test_directives_*.pas` (8 positive
      cases including nested conditionals, cross-`uses`-boundary define
      persistence, and proof that a dead branch's invalid syntax causes
      no error; 5 error cases: unterminated, stray `$ELSE`/`$ENDIF`,
      duplicate `$ELSE`, and nesting past the 16-level limit) and
      [docs/LANGUAGE.md](LANGUAGE.md#compiler-directives).

## Known issues (found via AddressSanitizer)

Both surfaced by a proactive ASan/UBSan sweep during the virtual
dispatch work, pre-existing and unrelated to it, and left logged here
rather than fixed in passing across several subsequent features (units,
`inherited`, `ParamCount`/`ParamStr`) since neither was in scope for
whatever feature happened to notice them next - until fixed directly,
once they'd shown up on every single sweep since:

- [x] `find_any_record_var()` (`parser.c`) read `with_stack`/
      `scope_record_var_count` at index `-1` for a plain identifier
      reference at the TOP level (main program body, outside any
      procedure, where `nesting_depth == -1` by design - see
      `parse_ast()`'s own reset comment). A global-buffer-overflow read
      one int before `with_stack`'s own storage - functionally harmless
      so far only because whatever happened to sit there in practice
      read as zero, but relied on memory layout, not on anything
      guaranteed. Fixed with the same guard `find_local()` (the
      equivalent lookup for plain, non-record locals) already had:
      `if (nesting_depth >= 0) { ... }` around the loop, skipping the
      local-scope scan entirely at the top level - mirrors
      `find_local_outward()`'s own safe `for (d = nesting_depth; d >= 0; d--)`
      pattern, just as an explicit guard rather than a naturally-empty
      loop range, since `find_any_record_var()` doesn't loop over
      nesting levels itself. While fixing this, found and fixed the
      exact same latent defect one function below it,
      `local_record_name_collides()` - not yet observed to be reachable
      in practice (`add_local()` appears to only ever run once
      `nesting_depth` has already been incremented), but the identical
      unguarded read either way if it ever were. Verified against the
      original ASan repro (any top-level `for` loop, e.g.
      `examples/test/forin/test_forin_local.pas`) and a full
      `examples/` sweep - clean.
- [x] `OP_SHL`'s implementation (`vm.c`) computed `a << b` directly on a
      signed `int`; `b == 31` triggered signed left-shift overflow
      (classic C UB) whenever the shifted-in bit would be the sign bit.
      Fixed by shifting the same bit pattern as `unsigned int` instead
      (`(int)((unsigned int)a << b)`) - always well-defined for any
      shift amount, and produces the identical two's-complement result
      this code already relied on in practice, just without the UB.
      Verified against the original ASan repro and a full `examples/`
      sweep - clean.
