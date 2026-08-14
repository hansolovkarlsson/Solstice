# Roadmap

Where Solstice is headed, and — more usefully — what's actually left to
do to get there. The [README](../README.md)'s Roadmap section is the
one-paragraph version of this; this is the working version, with the
current phase broken down into concrete, checkable tasks. Update this
file as work lands or plans change — it's meant to stay current, not to
be a one-time snapshot.

Shipped work moves to [docs/CHANGELOG.md](CHANGELOG.md) once it lands —
that file holds the full chronological record, including the design
decisions and bugs found along the way for each feature. This file stays
scoped to what's still open.

## Origin

Solstice started from a long-standing interest in stack-based
postfix VMs (Forth, HP calculators) and in how Pascal implementations are
often built on one. The goal isn't P-Code or any other VM's
compatibility — SolVM is designed from scratch, under this project's own
control, specifically so that more than one front-end language can
eventually target it. The long-run ambition is something in the spirit
of d:Base or SQLWindows: a self-contained tool for building personal
applications, with its own compiler(s), VM, and eventually GUI/database/
networking support, developed first on macOS and later ported to Linux.

## Phase 1 — Wirth-compatible Pascal + SolVM (complete)

Every task in this phase has shipped. See
[docs/CHANGELOG.md](CHANGELOG.md) for the full record and the design
rationale behind each item.

## Phase 2 — Object-oriented Pascal + general OOP support in the VM

Once Phase 1 is done: grow Pascal into an object-oriented dialect, and
grow SolVM/`solas` to support OOP constructs generally rather than
Pascal-specifically, so later front ends can share the same bytecode
primitives instead of each reinventing them.

- [ ] Possibly add a C-style `union` concept — true overlapping storage
      between fields, which variant records deliberately did NOT
      provide (see docs/LANGUAGE.md#variant-records); would need a real
      addressing/memory model for records that doesn't exist yet
- [ ] Possibly a linker for separately-compiled object-style units (`.obj`)

### Language extensions beyond standard Pascal

Moved here from Phase 1: none of these are part of Wirth/ISO 7185
Pascal, so they don't belong on the Wirth-compatibility checklist even
though a few are already implemented — Phase 1 stays scoped to standard
Pascal only, and picks up general OOP/VM growth once Phase 2 starts
anyway, so this is where they land instead.

- [ ] Dynamic array follow-ups — 1D dynamic arrays with primitive
      element types (`array of integer`, `SetLength`/`Length`/`High`/
      `Low`, reference semantics), `for x in arr do`, the `nil` literal,
      `Copy`/slicing, and array-literal syntax (`arr := [1, 2, 3];`) have
      shipped (see [docs/LANGUAGE.md](LANGUAGE.md#dynamic-arrays)). A
      named type-alias form (`type TIntArray = array of integer;`) turned
      out to already work as a side effect of how dynamic array types are
      represented internally - confirmed by testing, not newly built - so
      it's shipped too (see [docs/LANGUAGE.md](LANGUAGE.md#type-aliases));
      the one real gap that surfaced, a fixed-size array attempting the
      same syntax getting a confusing parser error, now gets a clear
      rejection instead of real support. A dynamic-array-typed function
      return type has shipped too (`function MakeArr: array of
      integer;`) - the read/index support this actually needed
      (`SetLength`/indexing/`Length`/`Copy` all read-before-write, so a
      function's own name had to become readable mid-body, not just
      assignable - see [docs/LANGUAGE.md](LANGUAGE.md#functions))
      is deliberately scoped to a dynamic-array return type only; a
      scalar return type keeps its existing, documented "reading the
      function's own name is a recursive call" behavior unchanged. A
      dynamic-array literal as a by-value call argument (`Foo([1, 2,
      3])`) has shipped too, same dispatch-on-known-type idea applied to
      `parse_call_arguments()` - a `var`/`const` argument still can't
      take a literal (both share this compiler's variable-reference
      passing mechanism, which needs a real variable). A dynamic-array-
      typed record/class field has shipped too (`data: array of integer;`
      inside a `record`/`class`) - turned out to need real work despite
      an initial "should be free" assessment: plain record fields (each
      one an ordinary hidden global/local, same storage a pointer field
      already gets) mostly *were* free once the field-type parser
      restriction was lifted, but a CLASS field's heap-offset-based
      storage (see `resolve_heap_deref_step()`) needed its own parallel
      set of fixes - read/write/indexing support in the heap-deref chain,
      plus a genuine bug caught along the way (`build_heap_deref_write_
      statement()`'s procedural-type check, `>= TYPE_PROC_BASE`, would
      have silently misrouted a dynamic-array field's own assignment,
      since `TYPE_DYNARRAY_BASE` sits numerically above `TYPE_PROC_BASE`
      - same class of bug as the return-type work's own `TYPE_PROC_BASE`
      finding). `for x in rec.field do`/`for x in c.field do` has shipped
      too - a plain record field reuses `for x in arr do`'s existing bare-
      variable machinery unchanged (a field is just another ordinary
      hidden global/local slot); a class/pointer field needed its own
      lookahead branch (heap-offset storage, not a slot), deliberately
      reimplemented as a narrow, terminal-only field lookup rather than
      calling the general `resolve_heap_deref_step()`, so the speculative
      "is this a dynamic-array field at all" check can never accidentally
      trigger a method call's own argument-parsing side effects before
      deciding whether to commit. Caught one real bug in review before it
      shipped: the FIRST version's local-variable lookahead branch
      returned 0 (the whole function's own "not a match" signal) the
      instant a local name resolved to something other than a bare
      dynamic array - which incorrectly also cut off the new record/
      class-field checks below it whenever the base variable itself was a
      *local* (a LOCAL class instance's field silently stayed
      unrecognized, while the identical GLOBAL case worked, since the
      global branch had never had an equivalent unconditional early
      return). Fixed by restructuring so a local-but-not-dynarray match
      falls through instead of returning, while still gating the
      global-variable check on `local_idx == -1` so shadowing isn't
      broken. Still
      open: multi-dimensional dynamic arrays; record/named-type/nested-
      array element types (this is different from the array's own type
      being aliased, which now works - an alias still can't stand in for
      a record/pointer/procedural/named-type ELEMENT type); array
      literals for a fixed-size array, as a `var`/`const` call argument,
      or in any other general-expression position; a named alias for a
      FIXED-size array; and comparing two dynamic arrays directly (`arr1
      = arr2`, as opposed to comparison against `nil`, which now works).

      A dynamic-array field's own value inside a **typed constant** has
      shipped too (`Bob: TScores = (name: 'Bob'; values: [10, 20, 30]);`)
      - reuses `typed_const_init_head`/`tail` (the existing chain of
      runtime-init statements a *fixed-size* array field's typed-constant
      initializer already relied on) rather than `parse_typed_const_
      value()`'s compile-time-literal-folding path, since a dynamic array
      has no compile-time literal form at all; the field's initializer is
      instead an ordinary `parse_dynarray_literal()` call, generating the
      same `NODE_DYNARRAY_LITERAL` construction any other array-literal
      assignment lowers to, run once at program start like every other
      typed-constant field's own init statement. Found and fixed one
      newly-reachable gap along the way: `SetLength` on a const record
      field never checked `is_const` at all (only the plain bare-variable
      fallback did) - dormant until now, since no dynarray record field
      could previously ever BE `is_const`; fixed by adding the same
      check to that branch of `parse_dynarray_writeback_target()`.

      A **bare** dynamic-array typed constant (`const X: array of
      integer = [1, 2, 3];`, no record involved) has shipped too - one
      more branch in `parse_typed_const_declaration()`'s own dispatch
      (`array` followed by `of` rather than `[`, previously an
      "Unexpected token 'of'" error), reusing every piece already proven
      by the record-field version above (`parse_dynarray_of()` for the
      element type, `parse_dynarray_literal()` for the value,
      `typed_const_init_head`/`tail` for the runtime init) - genuinely
      small, since nothing new had to be built for it.

      **A second, more consequential newly-reachable gap found this
      time, in the OPTIMIZER rather than the parser**: an unused dynamic-
      array-typed global assigned an out-of-range subrange-element
      literal (`var wasted: array of byte; wasted := [300];`, `wasted`
      never read afterward) silently compiled and ran to completion
      instead of aborting with the documented runtime range-check error -
      dead-code elimination's existing `has_range_check()` guard (added
      specifically to stop exactly this class of bug for an ordinary
      scalar assignment) only does a SHALLOW check on the assignment's
      immediate child, and a dynamic-array literal's own per-element
      range checks sit one level deeper, inside `NODE_DYNARRAY_LITERAL`'s
      own element list - invisible to that shallow check. Not new to this
      session (a plain `var`, unrelated to typed constants at all,
      reproduces it identically), just never noticed before, since
      nothing had previously exercised an unused dynamic-array-literal
      assignment with an out-of-range element. Fixed in
      `has_heap_alloc_side_effect()` instead of `has_range_check()`:
      recognizing a bare `NODE_DYNARRAY_LITERAL` node covers BOTH its own
      `OP_NEW` heap-allocation side effect (the same reasoning that
      function already applies to `NODE_HEAP_ALLOC`) AND every nested
      element range check uniformly, with no need to separately walk the
      element list - the literal's mere presence is already sufficient
      reason to never eliminate the assignment carrying it.

**Considered, explicitly out of scope: inline assembly** (`asm ... end;`
embedding raw `.sasm` directly inside a Pascal source file, letting a
procedure body drop to hand-written bytecode the way real Delphi drops
to hand-written x86). Scoped in detail, not just floated: `solas`'s own
`assemble()` (`solas.c`) can't be called as-is to implement this - it
unconditionally resets `sym_count`/`code_idx`/`string_count`/
`array_mem_count`/`label_count` to zero at its own top, since it's built
as a one-shot whole-program assembler, not something that can append
into a program `pascalc` has already partly generated. Making this work
needs real surgery on that tested, working file first (parameterizing
`assemble()` to skip those resets and start `code_idx`/label numbering
from wherever the enclosing Pascal compile already stands), before any
of the Pascal-side lexer/parser/codegen work even starts - a bigger,
more structurally invasive change than anything shipped this session,
for a feature the project's own original framing already called "not
itself a priority." Global Pascal variables would likely be reachable
from an asm block almost for free once that refactor exists (`solas`'s
own `OPERAND_VAR` resolution already searches the shared `sym_table[]`);
locals/parameters would need genuinely new machinery on top (they live
in `vm_frame_stack[]`/`current_locals[]`, which `solas` has no concept
of at all). Logged here, with the actual blocker named, rather than left
an open checklist item that reads as smaller than it is.

**Considered, explicitly out of scope: closures** (a nested function
capturing its enclosing scope and escaping/outliving its enclosing
call). Standard Pascal's own nested procedures with lexical scoping stay
supported and unaffected - this is specifically about a nested function
*outliving* the call that declared it, which they can't do today (every
nested procedure's prologue unconditionally expects a static link from
its caller, so calling one after its enclosing call has returned would
read a dangling `vm_static_link[fp]` slot). Not an unexamined gap: a
**safe-escape** variant (a nested procedure/function eligible to be used
as a procedural value if and only if it, and everything nested inside
it, never reaches outside itself for an ordinary enclosing local/
parameter - no captured *values*, just a static-link classification
problem, solvable with zero new opcodes) was designed to completion and
explicitly shelved by user decision in favor of the narrower, simpler
alternative that actually shipped instead: non-capturing **lambda
literals** (see [docs/CHANGELOG.md](CHANGELOG.md)'s own entry for them,
which names this tradeoff directly). **Full value-capturing closures**
(the classic `MakeAdder(n)` factory idiom) were never designed past that
same decision point - they'd need a heap-allocated capture block and a
representation migration through every existing procedural-value use
(record/class fields, arrays, parameters, `nil` comparisons), since a
procedural value today is uniformly just a plain int (a code address).
Logged here, rather than left an open checklist item, so none of this
gets mistaken for an unexamined gap later.

**Considered, explicitly out of scope: `TObject` implicit root class**
(the virtual-destructor half of this originally-bundled idea has
shipped — see the destructor entry above; this is what's left). Scoped
in detail, not just floated: every consumer of `parent_class_ptr_idx`
(`class_type_is_subtype_of()`, the `protected`-visibility ancestry
check, `OP_IS_INSTANCE`'s runtime `vm_class_parent[]` walk) already
terminates on `-1` rather than special-casing "no parent," so
retrofitting a real ancestor above every class — pre-registering an
empty `TObject` in `parse_ast()` and defaulting `parse_class_
declaration()`'s parent index to it instead of `-1` — would be
mechanical, not architectural; a field-free class already allocates
correctly today (heap offset 0 is always the hidden runtime type tag),
so even `new`/`dispose` on a bare `TObject` needs no new code path.
With zero fields and zero methods, though, the entire payoff is
narrow: a `TObject`-typed variable/parameter/array element could accept
*any* class instance via the existing widening/`is`/`as` machinery — no
`ClassName`/`Free`/RTTI, which need machinery that doesn't exist yet.
No generic containers, `TObject`-typed collections, or RTTI exist in
this compiler to actually consume that, and `MAX_POINTER_TYPES` is only
20 — every compile would unconditionally spend one slot on it. Revisit
if a concrete need for a heterogeneous "any class instance" container
or parameter shows up; not worth carrying as pure infrastructure ahead
of one.

### Object-oriented language features under consideration (Delphi-inspired)

A survey of Delphi/Object Pascal features not yet tracked anywhere on
this roadmap, compared against what classes-and-instances/units already
built. Ordered by fit with the existing architecture (the shared
`resolve_heap_deref_step()` field/method resolution, the runtime class
tag, the vtable) rather than by Delphi-completeness — these are
unscoped ideas, not committed work, and none has a design/plan yet.

**Moderate — needs some new machinery:**

- [ ] Typed exception handlers (`on E: SomeExceptionType do`) — needs an
      actual exception-class hierarchy, which drags in most of classes'
      own machinery a second time; explicitly cut when try/except
      shipped.
- [ ] Class references/metaclasses (`TClass = class of TObject;`,
      virtual constructors via `AClass.Create`) — a new "class-typed
      value" distinct from an instance.

**Large — architecture-changing, lower priority:**

- [ ] Method overloading (`overload`) — this compiler's whole design
      leans on a flat, non-overloaded namespace (`find_proc()`/
      `add_proc()` hard-reject duplicates); overloading would touch
      call resolution everywhere. It's exactly why constructors went
      with `new(c, Init(args))` instead of Delphi's `Create`.
- [ ] Operator overloading (`class operator Add(...)`) — really
      overloading again, plus new codegen hooks at every binary-op site.
- [ ] Generics (`TList<T>`) — the biggest lift on this list. No
      templates/monomorphization machinery exists anywhere in this
      pipeline, and the fixed-size-region memory model is in tension
      with a general container type. Realistically a phase of its own.
- [ ] Advanced records (methods/properties/operator overloads/visibility
      on a `record`) — mostly falls out once properties/class-members/
      operator-overloading land; not much new beyond those.
- [ ] Class helpers / record helpers — niche; mostly a workaround for
      Delphi's lack of free functions feeling OOP-y, which Pascal
      doesn't need (it already has free procedures).
- [ ] Open array / `array of const` parameters (variadic-style, what
      powers `Format`) — mostly useful *because of*
      generics/overloading; low standalone value.
- [ ] `Variant` type (dynamically-typed value with implicit
      conversions) — cuts against this compiler's whole "resolve type
      at compile time" design. Low fit, low priority.

**Considered, explicitly out of scope:** COM/interfaces with reference
counting, `OleVariant`, packages/DLLs, RTTI attributes, and the
VCL/component/message model (`TComponent`, published properties,
message handlers) all exist in Delphi specifically to support
Windows/COM interop and the VCL's design-time component architecture —
none of that fits Solstice's own trajectory (a from-scratch VM, not
Windows-hosted). Threading (`TThread`) is out for a different reason:
SolVM's whole design (`run_vm()`'s single dispatch loop, global VM
state) has no concept of concurrency — that would be a VM redesign, not
a language feature. Logged here so none of these get mistaken for an
oversight later.

### Data types under consideration

A survey of data types not yet in `pascalc`, alongside what's already
implemented (see the Types table in
[docs/LANGUAGE.md](LANGUAGE.md#types)): `integer`, `real` (32-bit float
only), `boolean`, `string`, `char`, arrays (1D/2D/N-D), records
(including variant records — non-overlapping, see
[docs/LANGUAGE.md](LANGUAGE.md#variant-records)), enumerated types,
subrange types, sets (capped at 32 elements), pointers, classes,
procedural types, and `text` file I/O. Ordered by cost, same spirit as
the OOP survey above — unscoped ideas, not committed work, and none has
a design/plan yet.

**Moderate — real architectural decisions:**

- [ ] `double`/extended-precision `real` — `real` is deliberately kept
      to 32 bits specifically because it has to fit the same 4-byte-
      `int`-sized slot every other scalar uses in `vm_vars[]`/
      `vm_stack[]` (see [docs/LANGUAGE.md](LANGUAGE.md#real)); those
      arrays are homogeneous ints. A `double` needs either a wider slot
      for every variable (wasteful) or a separate real-only storage
      region — a real architectural fork, similar in spirit to the
      dynamic-arrays problem above.
- [ ] C-style `union` — already logged as an idea further up this
      document (Language extensions section); explicitly blocked on the
      same thing variant records worked around instead of solving: this
      compiler's records have no real memory layout at all (each field
      is an independent hidden global/local, not contiguous storage —
      see [docs/LANGUAGE.md](LANGUAGE.md#variant-records)'s "How this is
      implemented"), so true overlapping storage needs that addressing
      model built first.
- [ ] `int64` (a full 64-bit integer type) — split off from the sized-
      integers entry once `byte`/`shortint`/`word` shipped (see
      docs/CHANGELOG.md): those three all fit within this VM's existing
      one-`int`-sized storage slot (they're narrower than `integer`, not
      wider), so they needed no storage-model change. `int64` doesn't
      fit at all - `vm_vars[]`/`vm_stack[]`/`vm_array_mem[]`/
      `vm_frame_stack[]` are homogeneous native-`int` arrays, one slot
      per scalar, the same constraint that already keeps `real` at 32
      bits (see the `double` entry above). A genuine 64-bit type needs
      either a wider slot for every variable (wasteful for the common
      case) or a separate wide-int storage region - a real architectural
      fork, the same class of problem as the `double` entry, not a
      quick add.

**Large, already deprioritized for the same reasons as the OOP survey's
own equivalent entries:** a `Variant` dynamically-typed type (cuts
against this compiler's whole "resolve every type at compile time"
design — same conclusion as the OOP survey's own `Variant` entry above,
since it's the same type either way); generics/parameterized types (see
the OOP survey's own Generics entry — no templates/monomorphization
machinery exists anywhere in this pipeline).

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
