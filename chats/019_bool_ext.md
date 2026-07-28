 
019_bool_ext.md

--note: also add the true false output in write

# Prompt

What operations and functions are still missing for integer, boolean, string, and char?

# Response

Let me check the actual current state of the type checker and parser rather than rely purely on memory, since precision matters here.I checked the actual source rather than relying on memory, and found one thing worth flagging that I hadn't noticed before. Here's the full picture, by type:

## Integer
**Have:** `+ - * /`, `div`, `mod`, unary `-`, all six comparisons (`= <> < > <= >=`), used as array indices and `for`-loop bounds.
**Missing:**
- No exponentiation operator
- No bitwise operations — `and`/`or`/`xor`/`not` are type-checked as boolean-only, so there's no bitwise integer variant, and no shift operators
- No `inc`/`dec`
- No overflow detection (silently wraps, matching underlying C `int` behavior — never checked or reported)
- No `abs`, `sqr`, `sqrt`, or any numeric built-in

## Boolean
**Have:** `and`, `or`, `xor`, `not`, assignment, use as `if`/`while`/`until` conditions.
**Missing — and this is worth calling out, since I hadn't verified it explicitly before:** **`=` and `<>` don't work between two booleans.** I just tested it directly:
```
Type Error: Comparisons require integer, string, or char operands
```
The relational-operator type check only accepts integer-integer or string/char-string/char pairs — boolean never got included. This is a real gap, not a deliberate restriction I'd previously decided on and forgotten to mention. `<`/`>`/`<=`/`>=` on booleans aren't supported either, though that's less surprising since Pascal doesn't typically order booleans anyway.

## String
**Have:** concatenation (`+`), all six comparisons (lexicographic), assignment, `readln`/`write`/`writeln`.
**Missing:**
- No indexing into a string to get a single character (`s[i]` doesn't work — strings aren't arrays)
- No `length`
- No substring/`copy`, no `pos` (search)
- No case conversion (`upcase`, etc.)
- No string-of-a-number / number-of-a-string conversion function (`writeln` formats implicitly, but there's no explicit `str`/`val`)
- No repetition/multiplication

## Char
**Have:** all six comparisons against `string`/`char`, concatenation with `string` (yields a `string`), assignment from a single-character string literal, runtime length-1 enforcement.
**Missing:**
- **`ord`/`chr`** (char ↔ integer conversion) — this one I'd already flagged before, still true
- No dedicated char literal syntax (it's a single-character `string` literal, not its own token)
- No `succ`/`pred`-style char arithmetic

## Cutting across all four types
There are currently **zero built-in library functions** — no `abs`, `sqr`, `sqrt`, `ord`, `chr`, `odd`, `length`, `copy`, `pos`, `upcase`, `random`, nothing. Every "function" in the language is user-defined via `function`. Now that user-defined functions exist, adding these as builtins is architecturally straightforward (they'd just be pre-registered `ProcSymbol` entries with special-cased codegen) — that's a reasonable, well-scoped next increment if you want it, and `ord`/`chr` in particular would resolve one of the standing gaps.

Want me to implement any of these — the boolean equality fix in particular seems like a real bug worth a quick fix regardless of anything else?

# Prompt

Implement the missing boolean functions and operators and comparators.

# Response

Good catch — this is genuinely a small, well-scoped fix. Let me confirm the design before touching code: since booleans are already represented as plain `0`/`1` integers at the VM level (same as `TRUE`/`FALSE` today), the *only* thing blocking `a = b` for two booleans is the type checker's relational-operator check — the existing integer comparison opcodes (`OP_EQ`, `OP_LT`, etc.) already work correctly on `0`/`1` values with zero codegen changes needed. And since real Pascal treats `boolean` as an ordinal type (`false < true`), I'll add all six comparisons (`= <> < > <= >=`), not just equality.Let's verify codegen.c genuinely needs no changes (confirming `is_string_type` returns false for boolean, so comparisons fall through to the plain integer opcodes):Confirmed — `is_string_type` correctly excludes `TYPE_BOOLEAN`, so `EQ`/`LT`/`GT`/`LTE`/`GTE`/`NEQ` codegen already falls through to the plain integer opcodes for booleans. Let's build and test:Clean build. Now let's test all six comparisons on booleans, including verifying the ordinal semantics (`false < true`):All seven cases correct, including the ordinal semantics (`false < true`). Let's also confirm a genuine type mismatch is still correctly rejected, and check constant folding works for boolean comparisons:Both correct — mismatch still rejected, and constant folding correctly computes `0` and `1`. Now the full regression suite:Round-trip test, then documentation:Now let's update `docs/LANGUAGE.md`'s comparison operators table:Final rebuild and sanity check, then package and deliver:## Summary

Genuinely small fix, as scoped: the entire change is one relational-operator check in `type_checker.c`. Booleans are already represented as plain `0`/`1` integers at the VM level (same as everything else there), so the existing integer comparison opcodes already handled the mechanics correctly — the type checker was just refusing to let two booleans reach them.

**Verified:**
- All six comparisons (`= <> < > <= >=`) work correctly on booleans, including confirming Pascal's ordinal semantics (`false < true`).
- Genuine type mismatches (boolean vs. integer) still correctly rejected.
- Constant folding correctly computes boolean comparisons at compile time (`true = false` → `0`, `false < true` → `1`).
- Byte-identical round-trip through `desole`/`solas`.
- Full regression suite across the project — nothing else affected.
- `docs/LANGUAGE.md`'s comparison table updated.

Everything else from the earlier gap analysis — bitwise integer ops, `abs`/`sqr`/`sqrt`, string indexing/`length`, `ord`/`chr`, and the rest — is still open, whenever you want to pick any of it up.

# Prompt

One more thing with boolean, my understanding is that boolean values print a false and true in standard Pascal.

# Response

Good catch — that's real, standard Pascal behavior, and this compiler doesn't do it yet. Let me confirm first, then fix it properly (including the `-v` final-state dump, which has the same gap).Confirmed — `1`, `0`, and `a = 1` should all say `TRUE`/`FALSE`. Let me fix this in both places. Standard Pascal prints these in uppercase, so I'll match that.Now `vm.c` — implement the opcode, and fix the `-v` final-state dump:Now let's fix the `-v` final-state dump, which has the same gap:Now `codegen.c` — the `writeln`/`write` argument opcode selection needs a third branch:Now `solas.c`/`desole.c`:Let's build and test:Now let's test boolean *arrays* in the `-v` dump too (the other code path I fixed), plus the full regression suite:That looks like a bug, but let me check if it's actually the pre-existing DCE pattern (assignments never read elsewhere get eliminated) rather than something new I broke:Confirmed — that's the pre-existing, documented DCE behavior (an array written but never read gets its assignments eliminated), not something my change broke. Let me fix the test properly:Both paths confirmed correct. Now round-trip test through `desole`/`solas`:Now let's check the docs for anything that needs updating:Now `docs/BYTECODE.md`'s opcode reference:Now `docs/ASSEMBLER.md`'s mnemonic list:I notice this list is also missing `LOAD_IDX_DYN`/`STORE_IDX_DYN` from an earlier session — let me fix the whole list while I'm here:Those are missing from `BYTECODE.md` too — let me add them while fixing doc drift:Final rebuild and sanity check, then package and deliver:All correct. Let's package and deliver:## Summary

Confirmed and fixed — this compiler was printing raw `0`/`1` for booleans instead of `TRUE`/`FALSE`, in **both** `write`/`writeln` and the `-v` final-state dump (including boolean array elements, which had the same gap).

**One new opcode**, `OP_PRINT_BOOL` — pops a value, prints `TRUE`/`FALSE`, no trailing newline (same convention as `OP_PRINT`). `writeln`/`write`'s argument codegen now branches three ways (string-type → `PRINT_STR`, boolean → `PRINT_BOOL`, else → `PRINT`) instead of two.

**Verified:**
- `write`/`writeln` of a plain boolean, and mixed into a larger argument list (`writeln('value is: ', a)`).
- The `-v` dump for both a scalar boolean and a boolean array — caught my own test mistake along the way (the array initially showed all `FALSE` because it was written but never read, hitting the same pre-existing DCE pattern from earlier sessions, not a new bug — confirmed via the `[DCE Optimization] Removing...` messages before fixing the test).
- Byte-identical round-trip through `desole`/`solas`, with `print_bool` correctly appearing in disassembly.
- Full regression suite — nothing else affected.
- Docs updated, and I also caught and fixed unrelated doc drift while I was in there — `LOAD_IDX_DYN`/`STORE_IDX_DYN` (from the array-parameters work) had never made it into `BYTECODE.md`'s opcode table or `ASSEMBLER.md`'s mnemonic list.

Both boolean items from your gap analysis are now closed. Let me know if you want to keep working through the rest of the list — bitwise integer operators, `abs`/`sqr`/`sqrt`, string indexing/`length`, or `ord`/`chr`.

