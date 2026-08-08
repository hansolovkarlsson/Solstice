# Scoping note: classes and instances (Phase 2)

Freeform design note, not authoritative — see `docs/ROADMAP.md`'s Phase 2
section for the actual checklist. This captures the architecture
research and design decisions behind the "Classes and instances" item
before any of it is implemented, so the reasoning isn't lost between
sessions.

## Closures and lambdas — not part of this

Neither is in the near-term plan, and neither is part of "classes and
instances":

- **Closures** are tracked separately, under Phase 2's "Language
  extensions beyond standard Pascal" (`docs/ROADMAP.md:796`), explicitly
  non-standard — real Pascal doesn't have them. This compiler already
  has nested procedures with lexical scoping (a static-link chain,
  `vm_static_link[]`), but that chain only exists while the enclosing
  call is still on the stack. A closure needs a nested function's
  captured environment to *outlive* its enclosing call — a structurally
  different, harder problem than anything nested procedures do today.
- **Lambda expressions** are further out still — listed only under
  Phase 4 (Phoenix, the eventually-planned original language),
  explicitly flagged as not part of any Pascal dialect.
- The existing "functional/procedural parameters" feature
  (`function Apply(function f(n: integer): integer; v: integer)`) is
  explicitly documented as *not* a closure — just a bare runtime code
  address with zero captured environment.
- **Relationship to classes**: none, mechanically. Classes (below) are
  built on the heap/pointer memory model — an instance is a block of
  memory with an address. A closure needs a *procedure activation* to
  survive past its own return, which is a different problem entirely
  (extending the call/frame-stack story), not something classes provide.
  In some languages closures substitute for small objects, but here
  they'd be two independent features on different machinery.

## Design decision: reference semantics

Two structurally different models fit "classes" against this codebase,
and they lead to very different amounts of work. **Decided: reference
semantics** (Delphi/Java-style) over value semantics (old Turbo Pascal
`object`-style):

- **Reference semantics (chosen)**: `class TFoo` auto-creates a
  heap-allocated record plus an implicit pointer type. `var f: TFoo;`
  holds a reference — `f2 := f1` aliases the same instance, `nil` is a
  valid value, allocation/deallocation is explicit. Builds on the
  existing pointer/heap machinery — the only genuine runtime-addressable
  record storage anywhere in the compiler (see below) — and matches what
  most people mean by "objects": identity and sharing. This is the
  bigger of the two builds.
- **Value semantics (not chosen)**: an "object" would just be a record
  with attached procedures, declared/passed by value like today's plain
  records — no heap, no `nil`, no aliasing. Much smaller build (nearly
  all reuse of the flattened-record parse-time-sugar model already in
  place), but no real object identity — copying an object copies every
  field, same as `p2 := p1;` already does for a record. Doesn't advance
  the roadmap's stated goal of growing SolVM's own OOP primitives, since
  it wouldn't need any new VM mechanism at all.

## Why reference semantics means "instance = heap-allocated record"

Plain records (even nested/variant ones) have **no real memory layout**
at all — a record variable is just N independent hidden globals created
at parse time, resolved away entirely by the time `type_checker.c`/
`codegen.c` ever see it. Only two things in this codebase have a genuine
runtime base address: arrays-of-records, and pointer targets
(`new`/`dispose`/`p^`, backed by `vm_heap_mem[]`, a real size-bucketed
free-list allocator). So an instance almost has to be a heap-allocated
block, addressed exactly the way `p^.field` already is
(`OP_LOAD_HEAP_FIELD`/`OP_STORE_HEAP_FIELD` — a compile-time field
offset against a runtime pointer value).

## Why methods need name mangling

Procedures live in one flat, whole-program namespace today — no
scoping, no overloading. `add_proc()` hard-rejects a duplicate name
anywhere in the program, nested or not. Two classes each declaring a
method called `Draw` would collide instantly unless method names are
mangled at parse time (`TCircle__Draw`) — the same trick this codebase
already uses for record fields, static locals, and nested-record
leaves. That mangling solves the whole problem for free: a method
becomes an ordinary top-level procedure with an implicit first
parameter (`self`, the instance pointer) — and passing a pointer as an
ordinary by-value argument *already works today with zero new
mechanism* (nothing in parameter-passing code special-cases pointer
types at all).

## Why static/early binding only, for now

The roadmap's own phrasing — "most likely early/static binding only to
start" — matters a lot here: static dispatch needs no vtable and no
runtime method-address storage. `obj.Method()` can resolve to one fixed
procedure at compile time, purely from the variable's declared type —
the same parse-time-resolution philosophy every record feature so far
has used. That keeps a static-binding-only v1 far smaller than "OOP"
usually implies.

## Surface syntax (v1)

```pascal
type
    TCircle = class
        radius: real;
        procedure SetRadius(r: real);
        function Area: real;
    end;

var
    c: TCircle;
begin
    new(c);                  { reuses existing pointer/heap new() unchanged }
    c.SetRadius(2.0);
    writeln('area: ', c.Area);
    dispose(c);
end.
```

## How this maps onto what already exists

1. **`class TFoo ... end;`** declares two things at once, both reusing
   existing machinery: a `RecordTypeDef` for the fields (fully reuses
   the field-group parser factored out for variant records — comma-
   separated names, scalar/array/nested types), plus an *implicit*
   `PointerTypeDef` whose target is that record — exactly as if the user
   had separately written `TFooRec = record ... end; TFoo = ^TFooRec;`,
   just automatic, and with `.` instead of requiring `^`.
2. **`c.radius`** compiles like `c^.radius` does today
   (`NODE_HEAP_FIELD_ACCESS`/`_ASSIGN`, `OP_LOAD_HEAP_FIELD`/
   `OP_STORE_HEAP_FIELD`) rather than the flattened-plain-record path —
   the parser just needs to recognize "this identifier's type is a
   class" and route field access through the heap-deref resolver
   instead.
3. **Methods** are ordinary top-level procedures under the hood,
   registered with a mangled name (`TCircle__SetRadius`) and an implicit
   first parameter `self: TCircle`. This is what makes two classes
   independently having a `Draw` method just work.
4. **`c.SetRadius(2.0)`** is genuinely new parse logic (nothing like it
   exists yet) — resolve `c` to its pointer value, look up `SetRadius`
   among `TCircle`'s methods, build an ordinary `NODE_CALL` to
   `TCircle__SetRadius` with `c`'s value spliced in as the first
   argument. Once built, this is just an ordinary call — no new codegen
   or VM work, since passing a pointer as a plain by-value argument
   already works unmodified today.
5. **`new(c)` / `dispose(c)`** need no changes at all — they already
   work on any pointer-typed target, and `c`'s type resolves to a
   pointer type per (1).

## Prerequisites / things to fix first

- **`MAX_POINTER_TYPES` / `MAX_RECORD_TYPES` = 20 each** — every class
  consumes one of each. Trivial to raise, but worth a deliberate bump
  alongside this work.
- **Array-typed fields on a pointer/heap target are already broken** —
  confirmed by tracing the actual offset math, not just the type
  checker (which accepts them): allocation size and field offsets are
  computed as field-count/field-index, not leaf-weighted, and there's no
  `c.field[i]` syntax at all. If class fields need arrays in v1, this is
  a real prerequisite fix, not reuse. Recommend scoping v1 to scalar
  fields only and fixing this separately later, same as local records
  already restrict arrays.
- **Nested-record fields are currently disallowed on any pointer
  target** — meaning a class can't have a field whose type is another
  class (composition) until that restriction is lifted, which needs its
  own leaf-offset rework (parallel to what `record_type_leaf_count()`
  did for nested plain records, redone for the heap path). Recommend
  scoping v1 to scalar-only fields and revisiting composition after.
- **`obj2 := obj1;`** (no `.`, no method call) needs *no* new work —
  under reference semantics that's just copying a pointer value, and a
  class variable's storage is already an ordinary scalar slot. This is
  free, unlike `q^ := p^;`'s field-by-field deref-copy (a different,
  still-unsupported operation for plain pointers).

## Explicit known gaps for v1

- **No inheritance** — a class can't extend another.
- **No virtual/dynamic dispatch** — `obj.Method()` always resolves to
  one fixed procedure at compile time from `obj`'s declared type.
  `OP_CALL_INDIRECT` (the opcode a vtable would need) already exists and
  is the right shape, but storing a callable address in a field is a
  separate, currently-unbuilt roadmap item ("procedural types/function
  pointers") — dynamic dispatch is a natural v2 once that lands.
- **No constructors** — just bare `new(c)`/`dispose(c)`, no
  `TFoo.Create`.
- **No visibility (`private`/`public`)** — everything's accessible, same
  as a plain record's fields today.
- **Scalar fields only** — per the prerequisite note above.

## Suggested build order

1. `class` declaration parsing → fields + implicit pointer synonym +
   method *headers* only (no bodies yet) — compiles to nothing
   runtime-visible; checkpoint.
2. `new(c)`/`dispose(c)` on a class variable — should already work from
   (1); test to confirm.
3. `c.field` read/write via the heap-deref path.
4. Method bodies: mangled-name registration + implicit `self`.
5. `c.Method(args)` call syntax.
6. Test matrix: field read/write, a method reading/writing `self`'s
   fields, two classes with a same-named method (proves the
   collision-avoidance), nil-self dereference (should inherit the
   existing nil-check for free), one method calling another on `self`
   or on a different instance passed in.

This is bigger than any single Phase 1 item — closer in scope to how
"records" as a whole grew across many roadmap entries (basic →
parameters → array-elements → nested → variant) than to one bullet.
