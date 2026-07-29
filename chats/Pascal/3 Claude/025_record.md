025_

# Prompt

So what's next?

# Response

Good moment to take stock again. Here's the landscape:

## The other big core gap: records

`record ... end` is the last major "fundamental Pascal" feature untouched. I still think this is more tractable than `real` was — field access is just `base + field_offset`, always statically known at compile time, so it can likely reuse the same "flat contiguous block" storage pattern arrays already use rather than needing anything architecturally new like `real`'s bit-reinterpretation trick did. The type-checking side (heterogeneous named fields vs. arrays' homogeneous indexed ones) is more novel work than the runtime side. This is genuinely the last "big" language feature left.

## Now more valuable than it was last time I mentioned it: `write`/`writeln` field-width syntax

`writeln(x:10:2)` — I called this out as deferred scope in both the string-functions session and just now with `real`. It matters more today than it did before `real` existed: right now, printing a real always uses a fixed `%.6g`, with no way to control decimal places (`writeln(price:0:2)` for `"19.99"` instead of whatever 6-significant-digits happens to produce). This is a parser-grammar extension (an optional `:width` / `:width:precision` suffix on any `write`/`writeln` argument) rather than a new architectural layer, so it's a well-contained single session.

## Smaller backlog items, still genuinely open

- `s[i] := ...` string mutation (copy-on-write, same desugaring trick already used for `inc`/`dec`)
- 2D array parameters/locals (1D already has both)
- `readln` into a parameter/local, and `for`-loops with a local loop variable — both restrictions dating back to the very first parameters session
- `abs`/`sqr` extended to `real`
- Constant folding for real literals

## Explicitly deferred, still there if wanted

Dynamic arrays / array `copy`-slicing.

---

**My honest recommendation**: field-width syntax is the best next single-session win — it's contained, high-value now that `real` printing needs it, and doesn't require a new design-heavy discussion the way records or dynamic arrays would. Records is the better *long-term* choice if you want to keep closing "fundamental Pascal" gaps, but deserves the same kind of upfront design conversation `real` got before diving in.

Which direction do you want?

# Prompt

Let's close the big core gap: records. Would it be easier to solve by breaking it down and solve it how it would work in the VM first?

# Response

Yes — starting from the VM/representation side is exactly the right instinct, and it actually simplifies this more than you might expect. Let me walk through why.

## The key insight: field access is compile-time-resolved

Unlike array indexing (`arr[i]`, where `i` is a runtime value needing a bounds check), record field access (`person.age`) always uses a literal field name, known completely at compile time. There's no runtime index computation and no bounds check needed at all — `person.age` can resolve to a *fixed* location the moment the compiler sees it, the same way a plain variable reference does.

That fact opens up two genuinely different implementation strategies:

**Option A — block + offset** (what arrays do): a record variable gets a base offset into a shared memory region, and each field lives at `base + field_offset`, with new opcodes to load/store at that offset.

**Option B — mangled per-field symbols**: when the compiler sees `var p: TPerson`, it doesn't create one "record" storage location at all — it creates one *ordinary* hidden variable per field (e.g. `p__age`, `p__name`), each in the existing symbol table, each with its own correct type. `p.age` then just resolves, at parse time, to a lookup of the mangled symbol `p__age` and is treated exactly like any other variable reference from that point on.

## Why I'd pick Option B

This project already uses exactly this trick for local arrays — a local array gets a hidden *global* symbol with a mangled name, and the local declaration just remembers which mangled global it maps to. Records fit the same pattern even more cleanly, and the payoff is real: **Option B needs zero new VM opcodes.** Field read/write reuses `LOAD`/`STORE` exactly as-is. Whole-record assignment (`p2 := p1;`) becomes a compile-time-unrolled sequence of existing `LOAD`+`STORE` pairs, one per field — no new opcode needed there either. Dead-code elimination, the `-v` dump, disassembly — all of it already works per-symbol, so it all just works for record fields too, for free. If a field's own type happens to be an array, that's *also* free — it just reuses `add_array_var` directly for that field's mangled symbol.

The real cost of Option B is that it doesn't generalize to things that need a genuine runtime-selectable record: arrays of records (`people[i].age`, where *which* record depends on a runtime index) or by-reference record parameters. Given I don't have those in scope for a first version anyway, that limitation doesn't bite yet — but it's worth being explicit that if we want arrays-of-records later, that'll need Option A's addressing model, or a real rethink.

## Proposed v1 scope

- A `type` section (`type TPerson = record age: integer; name: string; end;`) — genuinely new to this compiler, since every type so far has been declared inline.
- **Global record variables only** (`var p: TPerson;`) — no record parameters, no local records, no records as function returns yet. Matches how arrays themselves started global-only before parameters/locals became their own later feature.
- Field read/write (`p.age := 30; writeln(p.age);`), any field type including arrays.
- Whole-record assignment (`p2 := p1;`).
- **Deferred**: nested records (a field whose type is itself a record), arrays of records, record parameters/locals, record comparison.

Want me to run with that scope?

# Prompt

Yes. Run with the v1 scope.

# Response

Let me start by reviewing the exact parsing structure I'll be hooking into.Let me view `factor()`'s identifier resolution and `statement()`'s assignment parsing to plan the `.field` insertion points:Let's see the rest of this block before refactoring it into a shared helper:Now I have the full picture. Let's start implementing. First, `common.h` — new tokens for `type`/`record`, and `TOKEN_PERIOD` already exists for `.`:Now `lexer.c`:Now the bulk of the work in `parser.c`. Let's find a good place for the new data structures, near existing similar state:Now let's add the record type/variable data structures right after `LocalSymbol`:Now let's add `add_record_var`, right after `add_array_var_2d` (so it can call `add_var`/`add_array_var`, both already defined by that point):Good, `find_var_soft` is already defined before this point. Let's add `add_record_var`:Now let's add `parse_type_section()`, right before `parse_ast()`:Now let's wire `parse_type_section()` into `parse_ast()`, reset the record state at the top (for `test_recovery`'s repeated-compile scenario), and extend the `var` section to recognize record type names:Now let's extend the `var` section's scalar-type branch to recognize record type names:Now let's add the record-type branch:Now let's refactor `factor()`'s global-identifier resolution into a shared helper, then add record-field resolution. First let's find the current exact location:Now let's add the shared helper right before `factor()`:Now let's replace the original block to use the shared helper and add record-field resolution:Now let's add the record-variable check right at the top, and simplify the tail using the shared helper:Now let's simplify the plain-global-variable tail to use the shared helper:Now `statement()`'s assignment parsing. Let's find the current state:This raises a real structural question — whole-record assignment desugars into *multiple* chained AST nodes, but `statement()` callers typically expect a single node. Let me check how `statement_list()` actually chains things:Good — the fix is to wrap the desugared chain in a `NODE_COMPOUND` (the existing mechanism for "a group of statements treated as one"), so `statement_list()`'s single-node assumption stays satisfied while the compound's own internal chain still generates every field assignment. Let's implement both helpers:Now let's find and update the original block in `statement()`:Now let's insert the record-variable check and simplify the tail using the shared helper:Now let's simplify the plain-global-variable assignment tail to use the shared helper:Let's build and see what happens:Clean build overall — just one harmless-but-worth-fixing warning. Let's fix it:All builds cleanly with zero warnings. Now let's test thoroughly. Basic record declaration and field access first:All field types correctly working, including arithmetic on a field. Now array fields, multiple record variables, and whole-record assignment:All correct — array field indexing works cleanly, and the whole-record assignment test confirms both correctness and independence: `b := a;` correctly copies `10, 20`, and mutating `a.x` afterward leaves `b.x` untouched at `10`, confirming this is a genuine field-by-field copy, not an alias. This is exactly the mechanism the field-by-field desugaring was designed to guarantee.

## Summary of where things stand

**Implemented and verified working so far:**
- `type` section parsing (`type TPerson = record ... end;`), with the mangled-symbol-per-field trick working exactly as designed — zero new VM opcodes, zero new bytecode instructions for any of it
- Field read and write across every field type (`integer`, `real`, `boolean`, `string`, `char`) — confirmed correct individually, and confirmed `p.age + 1`-style arithmetic on a field works correctly too
- Array fields — indexed read and write confirmed correct
- Whole-record assignment (`b := a;`) — confirmed both correctness (`b.x = 10 b.y = 20`) and correct value-copy independence (mutating `a` afterward doesn't affect `b`)
- The build is clean with zero warnings

**What I have NOT yet tested, and shouldn't be assumed working:**
- Multiple record types coexisting (I declared two in the last test — `TPoint` and `TStudent` — but haven't specifically stress-tested type-confusion scenarios)
- Every error path: unknown field name, assigning between different record types, duplicate field names within a record, duplicate type declarations, the array-field rejection in whole-record assignment, field name length overflow
- The `-v` dump showing record fields correctly (as their mangled names)
- Dead-code elimination for record fields (should work automatically since each field is an ordinary tracked symbol, but not verified)
- Round-trip through `desole`/`solas` (should be trivial since records don't exist at the bytecode level at all, but not verified)
- The full project regression suite — nothing confirmed *not* broken elsewhere
- No documentation written yet (`docs/LANGUAGE.md`, `README.md`)
- Nothing copied to outputs or delivered

Given the gap between "two tests passed" and this project's established bar before calling something done, please continue in a follow-up turn so I can finish the error-path testing, verify the `-v` dump and DCE, confirm round-trip, run the full regression suite, and write documentation before treating this as delivered.

# Limit

Let's continue with the error paths first:All five correctly caught with clear, distinct messages. Now the field-name-length overflow and multiple-record-type coexistence:Both correct. Now the `-v` dump and DCE for record fields:Exactly as designed — DCE correctly eliminates `dead`'s fields while `used`'s survive, and the `-v` dump shows the mangled names transparently. Now round-trip (should be trivial, since records don't exist at the bytecode level at all) and the full regression suite:All confirmed — records are completely invisible at the bytecode level as designed, round-trip is byte-identical, and the full regression suite passes with all previously-verified output unchanged. Now documentation. Let's check the structure and verify an example before writing:Verified. Let's check the section right before `## Procedures` to find the exact insertion point:Now let's write the full "Records" section:Now let's update the Types table and the "not implemented" list:Now the "not implemented" list:Let's also check the "Program structure" section for the overall grammar, which should mention `type`:Now `README.md`'s status line:Final rebuild and sanity check, then package and deliver:All still correct, and `test_recovery`'s repeated-compile scenario confirms the record-state reset works correctly across multiple compiles. Let's package and deliver:## Summary — records, v1 scope, genuinely complete

Records are done: implemented, thoroughly tested, and documented.

**The design paid off exactly as predicted.** Representing a record variable as N ordinary mangled global variables (`p__age`, `p__name`, ...) rather than a genuine block-and-offset structure meant this feature needed **zero new bytecode opcodes** and **zero new VM machinery** — confirmed directly via disassembly, where every field reference shows up as plain `.var p__age integer` and ordinary `LOAD`/`STORE` instructions. Field access, type checking, dead-code elimination, and the `-v` dump all worked correctly on the first try because they were never taught anything about records at all — they just see ordinary variables.

**One real structural bug caught during design, not after testing**: whole-record assignment (`p2 := p1;`) desugars into multiple chained AST nodes, but I discovered by reading `statement_list()`'s actual chaining logic that it assumes `statement()` always returns a single node and manages `->next` itself — returning a multi-node chain directly would have had its own internal links silently overwritten by the caller, losing all but the first field's assignment. Fixed by wrapping the desugared chain in `NODE_COMPOUND`, the same mechanism `begin...end` blocks already use for "a group of statements treated as one."

**Verified working**: all field types (`integer`/`real`/`boolean`/`string`/`char`) individually and in arithmetic expressions, array fields with indexed read/write, whole-record assignment with confirmed value-copy independence (mutating the source after copying doesn't affect the destination — proving it's a real field-by-field copy, not aliasing), multiple record types and multiple variables of the same type coexisting without cross-confusion, dead-code elimination correctly treating each field independently, the `-v` dump, and byte-identical round-trip through `desole`/`solas`.

**All seven error paths confirmed**: unknown field name, whole-record assignment between mismatched record types, duplicate field names, duplicate type declarations, whole-record assignment rejected when a field is an array (with a clear explanation of *why*, tied to this compiler's existing no-whole-array-assignment limitation), and mangled-name length overflow caught cleanly rather than silently truncating.

**Full project regression suite passes**, matching all previously-verified output across every prior feature.

**Documentation**: a complete new "Records" section in `docs/LANGUAGE.md` (verified example included) explaining the `type` section syntax, the mangled-symbol implementation and why it makes so much work "for free," whole-record assignment semantics, and an honest, specific list of what's deferred — plus updates to the Types table, "What's not implemented," the top-level program-structure grammar, and `README.md`.

That closes out records for v1 scope. The natural follow-ups, if you want them later, are the smaller deferred items called out in the docs: record parameters/locals, or extending the addressing model for arrays-of-records — both bigger, separate undertakings rather than quick additions.
