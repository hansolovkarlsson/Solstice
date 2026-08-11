# Roadmap

Where Ouroboros is headed, and — more usefully — what's actually left to
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

Ouroboros started from a long-standing interest in stack-based
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

- [ ] Dynamic arrays (array `copy`/slicing) — standard Pascal arrays are
      always fixed-size
- [ ] `sizeOf` — a Turbo Pascal/Delphi built-in, not ISO Pascal.
      Complicated by this VM's memory model: every scalar (`integer`,
      `real`, `boolean`, `char`, `string`) is a uniform 4-byte slot in
      `vm_vars[]`/`vm_stack[]` (strings/chars are `string_pool[]`
      indices, not inline bytes - see the memory-model section in
      `CLAUDE.md`), so a literal byte-size answer wouldn't reflect
      anything real the way it does in Delphi. Would need a defined
      convention first (e.g. "slots occupied" rather than bytes) before
      this means anything meaningful for records/arrays/classes; no
      design yet.

      **A concrete motivating use case, not just parity with Delphi**:
      typed (binary) files (see `#typed-binary-files` below) - a `file
      of TRecord` variable's `filesize(f)` returns the record COUNT, not
      a byte count, so computing the file's actual byte size needs
      `TRecord`'s own on-disk size multiplied in by hand. Today every
      leaf field is written as one raw 4-byte int/float regardless of
      its declared type (see typed files' "How this is implemented"),
      so a `sizeOf` answer would currently just be `4 * (leaf field
      count)` - genuinely meaningful arithmetic, but only as informative
      as that uniform-width assumption stays true. Once/if the sized-
      integers entry below ships and typed-file writes start emitting
      actual variable-width values on disk, `sizeOf` would need to track
      real per-field width instead of a flat per-leaf constant - the two
      ideas are linked, and `sizeOf` is more useful once sized integers
      exist, not really useful before they do.
- [ ] Closures (a nested function capturing its enclosing scope) —
      standard Pascal allows nested procedures with lexical scoping, but
      not one that escapes/outlives its enclosing call

### Object-oriented language features under consideration (Delphi-inspired)

A survey of Delphi/Object Pascal features not yet tracked anywhere on
this roadmap, compared against what classes-and-instances/units already
built. Ordered by fit with the existing architecture (the shared
`resolve_heap_deref_step()` field/method resolution, the runtime class
tag, the vtable) rather than by Delphi-completeness — these are
unscoped ideas, not committed work, and none has a design/plan yet.

**Moderate — needs some new machinery:**

- [ ] `TObject` implicit root class — the virtual-destructor half of
      this originally-bundled idea has shipped (see the destructor entry
      above); this is what's left. Retrofitting every class's
      `parent_class_ptr_idx` to an implicit universal ancestor earns its
      keep once something actually needs one common type to hang off of
      - generic containers, a `TObject`-typed collection, RTTI - none of
      which exist in this compiler yet. Revisit when one of those does.
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
none of that fits Ouroboros's own trajectory (a from-scratch VM, not
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

- [ ] Dynamic arrays — already flagged as missing (no array
      `copy`/slicing — see the root README's feature-status line). Runs
      into the same wall generics does (see the OOP survey's own
      Generics entry above): every array today has a compile-time-fixed
      size baked into the single fixed `vm_array_mem[]` region, and this
      VM has zero dynamic allocation anywhere. Doing this properly means
      either a real allocator for arrays or at least a heap-style
      freelist like classes already use for fixed-size blocks - a
      genuine design decision, not a quick add.
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
- [ ] Sized integers (`byte`/`word`/`shortint`/`int64`) — moved up from
      "low value" (an earlier framing that only weighed this against
      subrange types' existing bounds-checking, e.g. `type TByte =
      0..255;` already rejecting an out-of-range value at runtime -
      genuinely redundant for THAT purpose alone). The real motivating
      case is typed (binary) file interop: `write(f, rec)` currently
      writes every leaf field as one raw 4-byte int/float unconditionally
      (see [docs/LANGUAGE.md](LANGUAGE.md#typed-binary-files)'s "How
      this is implemented"), regardless of the field's declared type -
      so a record meant to match an external fixed-width binary format
      (interop with another program, a documented file format, a C
      struct written by something else) can't actually get the right
      per-field byte widths on disk today, no matter how the Pascal
      type is declared; a subrange only bounds-checks the VALUE, it
      doesn't change the WIDTH `write(f, rec)` emits. Doing this
      properly means teaching the typed-file read/write path to
      actually pack/unpack narrower values per field (a `byte` field
      writing 1 byte, not 4) - real new machinery in the file-I/O layer,
      not just new type keywords with the same runtime representation
      as `integer`. Also feeds `sizeOf` (above): a meaningful byte-size
      answer needs real per-field width to report.

**Large, already deprioritized for the same reasons as the OOP survey's
own equivalent entries:** a `Variant` dynamically-typed type (cuts
against this compiler's whole "resolve every type at compile time"
design — same conclusion as the OOP survey's own `Variant` entry above,
since it's the same type either way); generics/parameterized types (tied
to the identical fixed-size-region tension as dynamic arrays above, but
bigger — see the OOP survey's own Generics entry).

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
