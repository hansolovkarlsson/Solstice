023_array_ext.md

# Prompt

How about some array functionality that's part of standard pascal.

# Response

Good instinct — this project has explicitly flagged the same gap in its own docs the whole time. Let me scope this precisely.

**What's the clearest "standard Pascal, clearly missing" array gap?** Multi-dimensional arrays — `array[1..3, 1..5] of integer`, real ISO Pascal syntax, and it's been sitting in "What's not implemented" since the very first arrays session.

**Scoping decisions, upfront:**
- **Exactly 2D, not a general N-dimensional scheme.** Here's why that's not just laziness: a 2D write (`arr[i,j] := val;`) needs three sub-expressions (two indices + a value) — which fits *exactly* into this AST node's three spare child pointers (`left`/`right`/`extra`), the same slots `NODE_IF` already uses for three children. A 3D write would need four, which doesn't fit without adding a new field to every AST node in the compiler just for this one case. 2D covers the overwhelming majority of real multi-dim use (matrices, grids, boards, tables) — I'd rather ship that cleanly than add struct bloat for the rare 3D case.
- **`arr[i, j]` only** (comma-separated indices, matching the declaration syntax) — not the chained `arr[i][j]` form. ISO Pascal treats those as equivalent, but supporting `arr[i][j]` would mean treating a partially-indexed array as a first-class value in its own right (a "row" that can itself be further indexed) — a materially different, bigger feature than "multi-dimensional arrays," so I'm leaving it out.
- **Global arrays only this round** — no 2D array parameters or 2D local arrays yet, matching how 1D arrays themselves were global-only before parameters/locals became their own later increment.
- I'm keeping the *existing* 1D array representation completely untouched (`Symbol`'s existing `array_lower`/`array_upper`/`array_base` fields, unchanged) and adding 2D support *alongside* it as new fields and new AST node types — purely additive, so there's no risk of regressing anything that currently works.

Let me build it.Now let's extend `Symbol` with 2D fields, purely additive:Now the two new opcodes:Now the two new AST node types:Now `vm.c` — the 2D bounds-checking helper and the two opcodes:Now the two opcodes, near the existing 1D versions:Now `type_checker.c`:Now `codegen.c`:Now `ast_printer.c` (no `default:` — required):Now `optimizer.c` — same DCE-usage-tracking fix pattern as before:Now the parser — the biggest piece. Let's find the global `var`-section array declaration parsing:Now let's add `add_array_var_2d()`, right after the existing 1D version:Now let's update `factor()`'s global array-access branch (read) and `statement()`'s global array-assignment branch (write) to handle `arr[i,j]`:Now the write path in `statement()`:Let's view the result to verify correctness before building:This looks correct. Let's build:Clean build. Let's add `solas`/`desole` support for the two new opcodes before testing round-trips:Let me check `desole.c`'s `is_var_ref()` helper, which determines whether the operand prints as a name — needs to include the new 2D opcodes too:I need to check whether `desole`'s array-declaration printing (and `solas`'s parsing) need a `.array2d` directive for round-trip correctness:Now `solas.c` — add `.array2d` directive parsing:Let's check `solas.c`'s own `add_array_var` helper (distinct from parser.c's) to model the 2D version on it:Now wire the `.array2d` directive into the parsing dispatch:Now let's test — basic 2D array declaration, read, write:I've hit the tool-use limit for this response, so let me summarize honestly where things stand rather than call more tools.

## Summary — 2D arrays for standard Pascal

**Design implemented**, purely additive alongside the existing 1D array machinery (no existing code paths touched or at risk):
- `array[lo1..hi1, lo2..hi2] of T` declaration syntax (global variables only, matching how 1D arrays were global-only before parameters/locals became their own increment)
- `arr[i, j]` read and write, row-major layout, strict bounds-checking per dimension (matching existing array-indexing conventions)
- Two new opcodes (`OP_LOAD_IDX2D`/`OP_STORE_IDX2D`), computing the row-major offset internally from the symbol's two dimension bounds
- Extended `Symbol` with `is_2d`/`array_lower2`/`array_upper2` — the existing 1D fields are completely unchanged in meaning
- Two new AST node types whose three arguments (two indices + value, for writes) fit exactly into the existing `left`/`right`/`extra` child pointers — this is *why* I scoped this to exactly 2D rather than general N-dimensional: a 3D write would need a fourth child, which doesn't fit without adding a new field to every AST node in the compiler
- The known dead-code-elimination usage-tracking gap (same class of bug fixed twice already this project) was proactively addressed for the new 2D read node
- `solas`/`desole` round-trip support added (`.array2d` directive)

**What I actually verified before running out of budget**: one test — 3×4 grid, nested-loop fill, nested-loop read/print, and a summed total (`270`, which I checked by hand: correct). That's real evidence the core mechanism works end-to-end, but it's a single happy-path test.

**What I have NOT yet verified, and shouldn't be assumed working**:
- Bounds-checking errors (out-of-range on either dimension)
- Type errors (non-integer index, wrong value type)
- The dead-code-elimination fix I added (untested — I added the fix by reasoning from the established pattern, but didn't confirm it with a test the way I did for the equivalent 1D fixes)
- Round-trip through `desole`/`solas` for a 2D array program (the plumbing is written but never exercised)
- The full project regression suite (nothing confirmed *not* broken elsewhere)
- No documentation updates yet (`docs/LANGUAGE.md`, `docs/BYTECODE.md`, `docs/ASSEMBLER.md`, `README.md` all still describe multi-dimensional arrays as unimplemented)
- Nothing has been copied to outputs or delivered yet

Given the gap between "one test passed" and this project's established bar (error paths, round-trip, full regression, verified docs) before I'd normally call something done, I'd treat this as genuinely unfinished. Please continue in a follow-up so I can properly test the error paths, confirm the DCE fix, verify round-trip, run the full regression suite, and write the documentation before calling this delivered.

# Tool limit

Let's finish this properly. First, the error paths:All five correctly caught, including confirming 1D arrays are unaffected. Now the DCE fix verification (this is the one I hadn't confirmed with a test):Wait — the "dead" array write should have been eliminated but wasn't, and I need to understand why before concluding this is correct. Let me check `sweep_dead_assignments`:Confirmed — `sweep_dead_assignments` only recognizes `NODE_ASSIGN` (which covers 1D array writes too), but `NODE_ARRAY_ASSIGN_2D` is a different node type and falls through the generic fallback, so it's *never* eliminated regardless of usage. This isn't a correctness bug (the program still runs right, just with a few harmless extra instructions), but it's a real inconsistency with how 1D arrays behave, and worth fixing now that I've found it:Let's rebuild and re-verify the DCE test:Both directions now correctly verified — the fix works. Now round-trip test and full regression suite:All regressions pass, round-trip is byte-identical, and `.array2d grid 1 3 1 4 integer` disassembles correctly. Now the documentation. Let's check the current `## Arrays` section:Let me verify one more clean example before documenting it:Verified. Now let's write the documentation:Now let's remove "multi-dimensional arrays" from the "not implemented" list:Now `docs/BYTECODE.md`'s opcode reference:Now `docs/ASSEMBLER.md` — the mnemonic list and the `.array` directive documentation:Now the mnemonic list and the array-name operand table:Good, reads correctly. Now `README.md`'s status line:Final rebuild and sanity check, then package and deliver everything:All still correct. Let's package and deliver:## Summary — 2D arrays, now properly complete

Everything flagged as unverified last turn has now been tested and confirmed.

**Error paths** — all five correctly caught: out-of-range on each dimension independently, non-integer index rejected, wrong assignment value type rejected, and confirmed 1D arrays are completely unaffected.

**The dead-code-elimination check found a real, if minor, bug**: `sweep_dead_assignments` only recognized `NODE_ASSIGN` (which covers 1D array writes), not the new `NODE_ARRAY_ASSIGN_2D` — so a genuinely dead 2D array write was *never* eliminated, regardless of usage. Not a correctness bug (the program still ran right, just with a few harmless extra instructions), but a real inconsistency with how 1D arrays behave. Fixed by extending the sweep to handle the 2D node's three-child shape, then verified explicitly in both directions: a 2D array read only through `arr[i,j]` correctly survives DCE, and a genuinely unused 2D array write is now correctly eliminated (confirmed via the `[DCE Optimization] Removing...` message).

**Round-trip and regression**: byte-identical through `desole`/`solas` (including the new `.array2d` directive, added to both tools), and the full project regression suite passes.

**Documentation**: `docs/LANGUAGE.md` gained a full "Two-dimensional arrays" subsection (example independently verified to run and match its output) explaining the row/column semantics and, honestly, *why* it's capped at exactly two dimensions — a 2D write needs three sub-expressions, which is the most this compiler's AST nodes hold without growing every node in the compiler for one feature. `docs/BYTECODE.md` and `docs/ASSEMBLER.md` opcode/directive references updated, `README.md` status corrected, and the stale "no multi-dimensional arrays" claim replaced with the precise remaining gap (3D+, and 2D array parameters/locals).

That closes out the clearest "standard Pascal array functionality" gap. Remaining candidates if you want to keep going: 2D array parameters/locals (extending what 1D already has), or whole-array assignment (`a := b` copying matching arrays) — smaller scope, happy to take on either whenever you're ready.

# Prompt

are there standard functions and operators on the arrays from pascal in there? Like length, low, high, copy?

# Response

Let me check the actual source rather than answer from memory, since this determines exactly what's missing.I checked the actual source rather than going from memory. Here's the precise state:

**`length`** — exists, but **string/char only**. Passing an array currently gives a type error (`'length' requires a char or string argument`).

**`copy`** — exists, but **string/char only** too, same restriction.

**`low`/`high`** — **don't exist at all**, not even as keywords. Nothing calls them.

So: no, the array-specific standard functions aren't there yet — only the string versions of `length`/`copy` exist, reused from the earlier string-functions session, and they haven't been extended to accept arrays.

**Worth noting on `copy` specifically**: real Pascal's `copy` on arrays is really a *dynamic array* operation (Free Pascal/Delphi) — it slices a variable-length array into a new, smaller variable-length array. This compiler only has *static*, fixed-bounds arrays (no dynamic arrays exist at all), so "copy a subset of a fixed-size array" doesn't have a clean target type to return into the way it does for strings. I'd want to flag that as a real mismatch rather than force-fit it, unless you specifically want array slicing added as its own feature.

**`low`/`high` are the clean, valuable ones to add**, and there's a nice property worth mentioning: since this compiler's array bounds are always compile-time-constant literals (never variables), `low(arr)`/`high(arr)` can be resolved *entirely at parse time* — no new opcode needed at all, they'd just become a compile-time-constant `NODE_NUMBER`, as cheap as writing the bound literally. `length(arr)` for arrays is the same story (`high - low + 1`, also foldable to a constant).

Want me to add `low`/`high`/`length` for (1D) arrays on that basis? And separately — do you want array `copy`/slicing at all, understanding it'd need dynamic arrays as a prerequisite, or is that out of scope for now?

# Prompt

For now, only do the easiest additions, low/high/length, and push the dynamic arrays to a later date

# Response

Good, and this confirms the design is even cheaper than I described — since `low`/`high`/array-`length` all resolve to a plain compile-time constant, they need **zero** changes to the type checker, codegen, VM, disassembler, or dead-code eliminator. Only the parser changes. Let me build it.Now `parser.c`. First, let's refactor `find_var` to expose a soft variant:Now let's add `try_get_array_bounds`, right after `find_local`:Now let's restructure `factor()`'s `TOKEN_LENGTH` handling and add `TOKEN_LOW`/`TOKEN_HIGH`:Let's build:Clean build. Now the tests — the classic idiomatic use case first:All correct, including the classic `for i := low(x) to high(x)` idiom. Now let's test local arrays, array-ref parameters, and the error paths:That's the pre-existing "for-loop variable must be global" restriction from an earlier session (unrelated to this feature) — my test used a local `i`. Let me fix it:Both correct — `param low/high/length: 1 5 5` and `local low/high/length: 10 12 3`, and the final sum `15` (1+2+3+4+5). The odd line ordering is just because `sumArray`'s internal `writeln`s execute mid-evaluation of the outer `writeln`'s second argument — expected left-to-right evaluation order, not a bug. All three error paths from before were also correctly caught. Now round-trip and full regression:Let me check further into the disassembly to directly confirm the compile-time-constant claim:Confirmed exactly as designed — `low(scores)` → `push -2`, `high(scores)` → `push 7`, `length(scores)` → `push 10`, zero array references at all in the bytecode. Round-trip byte-identical, full regression suite passes. Now the documentation:Let me verify the exact documented example runs as written:That's the same "only one `var` block" restriction I hit in testing — let me fix the doc example:Let me fix the doc example accordingly:Now let's add `low`/`high` to the "Built-in functions" summary table, and update `README.md`:Now `README.md`:Final rebuild and sanity check, then package and deliver:All still correct. Let's package and deliver:## Summary

`low`, `high`, and array-`length` are implemented, tested, and documented — genuinely the cheapest feature added to this project so far, exactly as scoped.

**Only three files touched** (`common.h`, `lexer.c`, `parser.c`) — no changes needed anywhere else, because every case resolves to a plain compile-time-constant literal, confirmed directly in disassembly: `low(scores)` compiles to `push -2`, `high(scores)` to `push 7`, `length(scores)` to `push 10` — zero new opcodes, zero runtime cost, and the array itself never even appears in the generated bytecode.

**Verified:**
- The classic idiom `for i := low(arr) to high(arr) do` works correctly end-to-end, computing the right sum over a negatively-indexed array.
- Works correctly on global arrays, local arrays, *and* array-reference parameters (using the parameter's own declared bounds, which are guaranteed to match whatever's actually passed).
- String `length` remains completely unaffected — same keyword, correctly disambiguated based on whether the argument is a bare array name or a general string expression.
- All three error paths: 2D arrays explicitly rejected (no defined "which dimension" answer yet), non-array arguments rejected, indexed expressions (`arr[1]`) rejected in favor of requiring a bare array name.
- Byte-identical round-trip through `desole`/`solas`, full project regression suite passes.
- Caught and fixed two of my own test/doc mistakes along the way (multiple `var` blocks aren't valid in this language — a real, pre-existing restriction, not a bug in this feature) before finalizing.

Array `copy`/slicing is deferred as agreed, since it needs dynamic arrays as a real prerequisite rather than a good fit for this compiler's fixed-bounds arrays.

