# Ouroboros Session Handoff

## Core Goal

Ouroboros is a multi-language toolchain built around a custom stack-based
VM (`solvm`) and its own bytecode format. The only front end under active
development is `pascalc`, a Wirth-style Pascal compiler, built alongside
the assembler (`solas`) and disassembler (`desole`) for the VM's own
bytecode. Current phase (Phase 1 of `docs/ROADMAP.md`): bring `pascalc`
up to Wirth/standard-Pascal compatibility, expanding `solvm`/`solas`/
`desole` in step wherever a language feature needs new bytecode
capability. This session implemented two Phase 1 roadmap items in full:
**records as array elements** and **pointers**.

## Current Status & Progress

### 1. Records as array elements — DONE, committed, pushed

Commit `584a061` ("Add records as array elements to pascalc"), on top of
the prior commit `d0e238f`. Pushed to `origin/main`. Nothing left to do
on this item.

- 1D arrays of records (`array[1..N] of TSomeRecord`), for both global
  and procedure-local arrays (local reuses the existing "hidden global"
  trick 1D scalar-array locals already use).
- Field read/write via a runtime index (`arr[i].field`), plus
  whole-element copy in all three directions: array↔array,
  array↔plain-record-variable.
- **Known gaps** (documented in `docs/LANGUAGE.md`): 2D/N-D arrays of
  records, array-of-record parameters, passing an array-of-records
  element directly as a by-value record argument to a procedure
  (workaround: copy through a temp record variable first).

### 2. Pointers — DONE, fully implemented and tested, **NOT committed**

All 18 implementation tasks complete; full regression sweep clean (345
`.pas` files + `test_recovery`); zero `-Wall -Wextra` warnings; `bin/`
rebuilt via `scripts/make.sh`. **This work is sitting uncommitted in the
working tree** — the standing project rule is to never commit/push
without the user's explicit instruction, and that instruction was not
given before this session ended.

- `type PFoo = ^Target;` where `Target` is any scalar type or a record
  type, including **forward-referencing** a record type declared later
  in the same `type` section (the self-referential linked-list/tree
  pattern: `PNode = ^TNode; TNode = record ... next: PNode; end;`).
- `new(p)` / `dispose(p)` — accept a with-field, record field, `var`
  parameter, plain local/global, **or** a `^`-dereference-chain target
  (`new(head^.next);`).
- `p^` / `p^.field` dereferencing to arbitrary depth
  (`p^.next^.next^.data`).
- `nil` literal, compatible with any pointer type at assignment/`=`/`<>`.
- Pointers support only `=`/`<>` (no arithmetic/ordering); rejected from
  `write`/`writeln`/`readln`.
- Backed by the VM's **first genuinely dynamic memory region**: one
  shared fixed-size heap (`vm_heap_mem[]`, `MAX_HEAP_MEM` = 4096 ints)
  with a size-bucketed freelist (`vm_heap_freelist[1..MAX_RECORD_FIELDS]`)
  so `dispose` actually makes space reusable, not just leaked.
- **Known gaps** (documented in `docs/LANGUAGE.md`): pointer to an array,
  pointer to another pointer (`^^T`), a pointer-typed field of a
  local/parameter record or an array-of-records element when immediately
  dereferenced (workaround: assign to a plain pointer variable first),
  whole-record assignment/comparison through a dereference (`q^ := p^;`
  — assign/compare field by field instead).

## Key Decisions Made

- **Scope for records-as-array-elements**, chosen via `AskUserQuestion`:
  1D only, global + local, no by-reference parameters — matches the
  project's repeated pattern of shipping the narrower, tractable slice
  first and documenting the rest as gaps.
- **Scope for pointers**, chosen via `AskUserQuestion`: pointer to
  scalar-or-record (not array, not pointer-to-pointer), self-referential
  record support **included** (judged essential — without it the feature
  can't build a linked list, arguably the entire point of pointers in
  Pascal), heap with a real freelist (not bump-only/leak) since a fixed
  4096-int heap would be exhausted almost immediately by any nontrivial
  program that didn't reuse freed space.
- **`dispose` does NOT nil the pointer afterward.** Initially considered
  auto-nilling as a "safer than standard Pascal" default, but rejected
  in favor of matching real Pascal's actual semantics (a disposed
  pointer's value is left undefined) — this also happens to simplify the
  implementation, since `dispose` then only ever needs to *read* its
  target, never write it back, so it can accept any pointer-valued
  expression (including a `^`-chain) for free via the ordinary
  expression parser.
- **One shared heap, not one arena per declared pointer type.** A
  per-pointer-type arena was considered (cleaner size accounting) but
  rejected: it would require a new compile-time metadata table visible
  to `codegen.c`/`vm.c` (and serialized into the `.bin` format, like
  `sym_table[]`), whereas a single shared heap keyed by element size
  needs **zero** new shared tables — `OP_NEW`'s `arg` (the element size)
  is already a compile-time constant baked in by `codegen.c`, so the
  freelist can be indexed by that size directly.
- **`DataType` range bug caught before it shipped.** Every existing
  "is this an enum" check in the codebase (`parser.c`, `type_checker.c`,
  `codegen.c`, `ast_printer.c`) was a bare `>= TYPE_ENUM_BASE`. Once
  `TYPE_POINTER_BASE` was added immediately after the enum range, those
  checks would have silently also matched pointer types (miscompiling
  `ord`/`succ`/`pred`/case-selectors, or reading `enum_types[]`
  out-of-bounds when printing). Fixed by bounding every such check
  (`t >= TYPE_ENUM_BASE && t < TYPE_ENUM_BASE + MAX_ENUM_TYPES`) in a
  dedicated pass **before** adding the pointer feature itself.
  `desole.c`'s own check was deliberately left unbounded — both enums
  and pointers legitimately degrade to "integer" there, since neither
  concept exists once `desole` links only `bytecode.c`.
- **`new`/`dispose` desugar entirely into ordinary assignment/read
  infrastructure at parse time** — `new(p)` becomes `p := <fresh heap
  allocation>;` reusing whichever assignment node `p`'s own resolution
  already builds (mirrors `inc`/`dec`'s exact target-resolution shape).
  This meant zero new "resolve an arbitrary lvalue" machinery was needed.
- **Real bug caught during testing, not before shipping**: dead-code
  elimination could drop `x := p^;`'s own nil-pointer-dereference check
  when `x` (a global) was otherwise unread — the DCE sweep only guarded
  `NODE_ASSIGN` targets, not side effects hiding in the *value*
  expression. Fixed by extending the existing heap-allocation DCE guard
  (originally written only for `NODE_HEAP_ALLOC`) to also recognize
  `NODE_HEAP_FIELD_ACCESS` anywhere in the value expression — same class
  of fix as three earlier DCE bugs this project has hit (subrange range
  checks, set side effects, array bounds checks).

## Files & Paths Touched

**Core compiler/VM (both features combined):**
- `src/common.h` — `DataType` reordering (`TYPE_NIL`, `TYPE_POINTER_BASE`
  bounded against `TYPE_ENUM_BASE`), `is_record_array`/
  `record_elem_field_count` on `Symbol`, new `NodeType`s
  (`NODE_ARRAY_RECORD_FIELD_ACCESS/ASSIGN`, `NODE_HEAP_FIELD_ACCESS/
  ASSIGN`, `NODE_HEAP_ALLOC`, `NODE_HEAP_DISPOSE`), new `TokenType`s
  (`TOKEN_CARET`, `TOKEN_NEW`, `TOKEN_DISPOSE`, `TOKEN_NIL`), 8 new
  opcodes, `MAX_POINTER_TYPES`/`MAX_HEAP_MEM`/`MAX_RECORD_FIELDS` (moved
  here from `parser.c`).
- `src/lexer.c` — `^`, `new`, `dispose`, `nil`.
- `src/parser.c` — the bulk of both features: `record_arrays[]`/
  `pointer_types[]` side tables, `add_array_var_rec()`/
  `add_local_array_rec()`, pointer-type declaration parsing incl. the
  end-of-`type`-section forward-reference resolution pass, `nil` literal,
  `parse_heap_deref_read()`/`parse_heap_deref_write()` chain helpers,
  `parse_new_statement()`/`parse_dispose_statement()`, and the
  `is_pointer_type`/bounded-`is_enum_type`-style fixes threaded through
  ~10 existing call sites (readln rejection, `ord()`, etc.).
- `src/type_checker.c` — pointer/nil assignment and comparison
  compatibility rules, `write`/`writeln`/`readln` rejection.
- `src/codegen.c` — cases for all 6 new node types.
- `src/vm.c` — `vm_heap_mem[]`/`vm_heap_count`/`vm_heap_freelist[]`,
  `vm_record_array_offset()`, all 8 new opcode implementations.
- `src/ast_printer.c`, `src/optimizer.c` — print cases;
  `has_heap_alloc_side_effect()` DCE guard (covers both `NODE_HEAP_ALLOC`
  and `NODE_HEAP_FIELD_ACCESS`).
- `src/solas.c`, `src/desole.c` — `.arrayrec` directive, 8 new opcode
  mnemonics.

**Tests** (`examples/test/`): 12 `test_recarr_*.pas` files (records as
array elements) + 16 `test_ptr_*.pas` files (pointers) — positive and
negative cases for both features.

**Docs**: `docs/LANGUAGE.md` (new "Records as array elements" and
"Pointers" sections + gaps lists), `README.md` (Status paragraph),
`docs/ROADMAP.md` (both items checked off with implementation notes),
`docs/BYTECODE.md` (opcode reference), `docs/ASSEMBLER.md` (`.sasm`
operand-kind table).

**Build output**: `bin/{pascalc,solvm,solas,desole,test_recovery}`
rebuilt via `source config.sh && bash scripts/make.sh`.

## Failed/Rejected Approaches

- **Per-pointer-type heap arenas** (a dedicated region + freelist per
  declared `^Target` type) — rejected in favor of one shared heap keyed
  by element size; would have needed a new compile-time table visible to
  `vm.c`/serialized into `.bin`, for no real benefit over size-bucketing.
- **Bump-only heap with `dispose` as a no-op/leak** — considered as the
  "simplest possible" implementation, rejected because a fixed 4096-int
  heap would be exhausted almost immediately by any realistic
  build-and-tear-down-a-list workload; the freelist wasn't much harder to
  build once the design was worked out.
- **Auto-nilling a pointer after `dispose`** — considered as a
  "safer than standard Pascal" default, rejected in favor of matching
  real Pascal semantics (see Key Decisions above).
- **Restricting `new(X)` to a plain-variable target only** (no
  `^`-chain, i.e. requiring `new(temp); head^.next := temp;` as a
  workaround) — this was the *original* plan to keep scope tractable,
  but it directly contradicted a preview the user had already approved
  (`new(head^.next);`). Reconsidered mid-implementation once the
  `parse_heap_deref_write()` helper existed anyway (needed regardless
  for ordinary `p^.field := value;` statements) — extending `new()` to
  accept a `^`-chain target turned out to be a small addition on top of
  already-built machinery, so the fuller scope was implemented instead
  of quietly shipping the narrower one.
- **`test_ptr_local.pas` "Unexpected token 'var'" — investigated as a
  suspected parser bug, turned out to be an invalid test file.** The
  first draft declared a `var` section *after* a `procedure` declaration
  in the main program, which is not valid Pascal (declaration order is
  fixed: `const`, `type`, `var`, then procedures/functions). No compiler
  bug — the test file was rewritten with the `var` section moved before
  the `procedure` declarations, per every other test file's structure.
- **Bare `>= TYPE_ENUM_BASE` checks in `desole.c`** — initially assumed
  this would need bounding like every other instance; on inspection it's
  correct *unbounded*, since both enums and pointers legitimately print
  as `"integer"` there and `desole` has no way to distinguish them
  anyway (it never links `parser.c`). Left as-is, with a comment
  explaining why.

## Immediate Next Step

**Ask the user whether to commit and push the pointers work.** It is
fully implemented, tested (16 new test files, full regression sweep
clean, round-tripped through `solas`/`desole`), and documented, but sits
uncommitted per the standing "never commit without explicit instruction"
rule. The natural opening line for the next session is to state that
pointers are complete and ask: *"commit and push?"*

After that, the next roadmap item to consider (not yet scoped or
discussed in depth) is likely **nested procedure/function declarations**
— flagged earlier in this session as the smaller, lower-risk item
compared to pointers (no new allocation/runtime-representation story
needed, "just" lexical scoping into an enclosing call's frame). The
alternative remaining Phase 1 items are: functional/procedural
parameters, `program` heading parameters (explicitly noted as
lowest-priority/no-op), and a handful of smaller VM/tooling items
(`over`/`rot`/`swap` opcodes, `desole` hexdump, `solas` macros, `(* *)`
comment style, VM debug built-ins) — see `docs/ROADMAP.md` for the full,
current list.
