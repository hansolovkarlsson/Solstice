020_int_ext.md

# Prompt

Lets do all the missing functions/operators for integers

# Response

Let me first check the actual VM implementation of `AND`/`OR`/`XOR`/`NOT`, since whether they're bitwise or logical under the hood determines what I can safely reuse for integers.Let me scope this carefully before diving in, since "missing for integers" spans a few genuinely different things.

**What I'll implement, and why:**
- **Bitwise `and`/`or`/`xor`/`not` on integers** — real gap. I checked the VM: `AND`/`OR`/`XOR`/`NOT` are currently implemented as C's logical `&&`/`||`/`!=`/`!`, which happens to coincidentally work for booleans (0/1) but is *wrong* for integers (`6 and 3` should be `2`, not `1`). Needs genuinely new bitwise opcodes, reusing the same `and`/`or`/`xor`/`not` keywords (this is standard in Turbo Pascal/Delphi/Free Pascal — plain ISO Pascal doesn't have it at all).
- **`shl`/`shr`** — standard companion to the above in those same dialects.
- **`abs`, `sqr`, `odd`, `succ`, `pred`** — real ISO Pascal built-ins. I'll implement `odd`/`succ`/`pred` as pure AST rewrites (`odd(x)` → `x mod 2 <> 0`, etc.) reusing existing machinery with zero new opcodes; `sqr` gets a new generic `DUP` opcode (worth having as a primitive, and this is exactly the "real use case" I said I'd wait for); `abs` gets one small dedicated opcode since it needs a conditional that doesn't decompose cleanly into existing pieces.
- **`inc`/`dec`** — in real Pascal these mutate their argument by reference, which this compiler doesn't support for scalars at all yet (only arrays are by-reference). Rather than building general var-parameters just for this, I'll treat them as syntactic sugar — `inc(x)` desugars directly to the same AST as `x := x + 1;`. I'll restrict targets to plain integer variables (global or local, not array elements) and explicitly exclude `char`/`boolean` targets, since neither has a representation where "increment" is meaningful here (`char` is a string-pool index, not a character code).

**Explicitly not doing:** exponentiation (`^` isn't part of real Pascal — it's the pointer-dereference operator there) and `sqrt` (needs a `real` type this compiler doesn't have; an integer-only `sqrt` would be non-standard and I'd rather not invent that).

Let me check the parser structure first:Let's check `optimizer.c`'s constant-folding switch to see if I need to extend it or if it's safe to leave as-is (just meaning these new ops don't get folded, not a correctness issue):That reveals something important — let me check whether this fold path is even reachable for booleans, since the guard only checks `NODE_NUMBER`, not `NODE_BOOLEAN`:Good — and that check just surfaced a real pre-existing bug I should fix while I'm in this exact code: the constant-folding guard only matches `NODE_NUMBER` operands, never `NODE_BOOLEAN`, so `true and false` has silently never been folded (confirmed above — the AST dump shows it reaching codegen unfolded). Since I'm about to add bitwise-vs-logical branching to this exact switch, I'll fix that guard too rather than build new folding logic next to a known-dead path.

Let's implement everything, starting with `common.h`:Now the opcodes:Now `lexer.c`:Now `type_checker.c`:Now let's update both cases:Now `codegen.c`:Now `NODE_BINARY_OP`'s opcode dispatch:Now `vm.c` — implement all eight new opcodes:Now `parser.c`. First, `term()` — add `shl`/`shr` at the same precedence as `and`/`*`/`div`/`mod`:Now `factor()` — add `abs`/`sqr`/`odd`/`succ`/`pred`:Now `is_statement_start()` and the `inc`/`dec` statement parsing:Now let's add the `parse_inc_dec` helper and wire it into `statement()`:Now let's wire `parse_inc_dec` into `statement()`:Now `optimizer.c` — fix the boolean-fold guard bug and extend folding for the new operators:Now `solas.c`/`desole.c` — mnemonics and opcode names for all eight new opcodes:Let's build:Clean build. Now the tests — starting with bitwise operators:All correct, including the logical (not arithmetic) shift semantics on `-1 shr 28 = 15`. Now `abs`/`sqr`/`odd`/`succ`/`pred` and `inc`/`dec`:All correct. Let's test `inc`/`dec` on a *local* variable inside a procedure (exercising the local-vs-global branch), and specifically confirm the boolean constant-folding bug fix:Confirmed — `[Optimization] Folded constants: 1 and 0` now fires (previously silently dead), and the AST correctly shows the folded `[Boolean] false` leaf. Now the error cases:Let's also verify the specific new error message fires for a same-typed-but-invalid case (both strings, which passes the earlier mismatch check but should still be rejected by the new AND/OR/XOR logic):Correctly caught. Now the round-trip test and full regression suite:All regressions pass. Now documentation. Let's check the current operator tables in `LANGUAGE.md`:## Summary — integer operators/functions, complete

Everything from the plan was implemented and verified this session, plus one real pre-existing bug found and fixed along the way. I got cut off mid-documentation-write (had just finished rewriting the Operators section of `docs/LANGUAGE.md`), so here's the full state.

### What's done and verified

**Bitwise `and`/`or`/`xor`/`not` on integers** — new opcodes (`OP_BAND`/`OP_BOR`/`OP_BXOR`/`OP_BNOT`), since the existing `AND`/`OR`/`XOR`/`NOT` are implemented as C's logical operators, which is correct for booleans but wrong for integers. Verified: `6 and 3 = 2`, `6 or 3 = 7`, `6 xor 3 = 5`, `not 5 = -6`, mixed boolean/integer operands correctly rejected.

**`shl`/`shr`** — new opcodes, bounds-checked (`0..31`, clean runtime error otherwise rather than C's undefined behavior), `shr` implemented as a *logical* shift matching Pascal (verified `-1 shr 28 = 15`, not `-1`).

**`abs`, `sqr`, `odd`, `succ`, `pred`** — `odd`/`succ`/`pred` are pure AST rewrites (e.g. `odd(x)` → `x mod 2 <> 0`) reusing all existing machinery with zero new opcodes. `sqr` uses a new generic `OP_DUP` (the primitive I'd said I'd add once a real use case appeared). `abs` gets one small dedicated opcode. All verified correct, including negative inputs.

**`inc`/`dec`** — desugars directly to the same AST as `x := x + n;`, restricted to plain integer variables (global or local, not array elements, not char/boolean). Verified on both globals and locals, including with an explicit delta, and all three restriction errors (string target, array target, non-integer target) correctly rejected.

**Real bug found and fixed**: while touching the constant-folding code, I discovered `true and false` had never actually been constant-folded — the fold guard only matched `NODE_NUMBER` operands, never `NODE_BOOLEAN`, silently making the existing boolean-folding cases dead code since the feature was first built. Fixed the guard and verified folding now genuinely fires (`[Optimization] Folded constants: 1 and 0` appears, and the AST shows the collapsed `[Boolean] false` leaf).

**Verified further**: byte-identical round-trip through `desole`/`solas`, and a full regression suite across every existing feature in the project — nothing else affected.

### What's left
`docs/LANGUAGE.md`'s Operators section has been rewritten (bitwise/logical dual-meaning table, `shl`/`shr`, precedence, and a new "Built-in functions and procedures" table for `abs`/`sqr`/`odd`/`succ`/`pred`/`inc`/`dec`) — that edit succeeded. Still needed: the "not implemented" list and `README.md`'s status line haven't been updated yet, and nothing's been copied to outputs or delivered via `present_files` this turn. Want me to finish those two doc updates and deliver everything?

# tool limit

Good, no stale claims to fix there. Now `README.md`'s status line:Let's also check `docs/BYTECODE.md`'s opcode reference table — it should include the 8 new opcodes too, matching the pattern I already fixed once before:And `docs/ASSEMBLER.md`'s mnemonic list, keeping it consistent with the pattern established last time:Final rebuild and sanity check, then package and deliver everything:All still correct. Let's package and deliver:## Summary

All missing integer operators and functions are implemented, tested, and now fully documented.

**Delivered this session:**
- Bitwise `and`/`or`/`xor`/`not` on integers (new opcodes, since the existing ones are logical-only)
- `shl`/`shr` with bounds-checking and correct logical-shift semantics
- `abs`, `sqr`, `odd`, `succ`, `pred` (mostly via AST rewriting, `sqr` introducing a new generic `DUP` primitive)
- `inc`/`dec` (desugared to plain assignment, since by-reference scalars don't exist here)
- A genuine pre-existing bug fixed along the way: boolean constant folding had been silently dead code since it was first written
- All of it round-trips correctly through the assembler/disassembler and passes the full project regression suite

**Docs now fully in sync:**
- `docs/LANGUAGE.md` — rewrote the Operators section (bitwise/logical dual-meaning table, `shl`/`shr`, updated precedence) and added a new "Built-in functions and procedures" table
- `docs/BYTECODE.md` — all eight new opcodes added to the opcode reference
- `docs/ASSEMBLER.md` — mnemonic list updated
- `README.md` — status line updated

That closes out the integer gap analysis completely. From the original four-type breakdown, remaining open items are: string (`length`, indexing, `copy`/`pos`, case conversion), char (`ord`/`chr`, dedicated literal syntax), and boolean is now fully closed. Let me know which you'd like to tackle next.

