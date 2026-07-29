010_fixes.md

# Prompt

Let's fix string ordering comparisons now, and the // comments, also, change the magic number "PASC" in the bin file to be "SOLE".

# Response

Three clean, independent fixes. Let me look at the current state of the relevant files first.Let's start with `common.h` — one new opcode for string ordering (`OP_SCMP`, a three-way `strcmp` result that the existing integer `LT`/`GT`/`LTE`/`GTE` can then compare against zero — no need for four separate string-ordering opcodes):Now `type_checker.c` — remove the ordering restriction on strings:Now `codegen.c` — the ordering comparisons need to route through `OP_SCMP` for strings:Let's add a small helper right after `emit()` for string-aware ordering comparisons:Now use it for `LT`/`GT`/`LTE`/`GTE`:Now `vm.c` — implement `OP_SCMP`:Now `solas.c`/`desole.c` — add the `SCMP` mnemonic:Now task 2 — `//` line comments in `lexer.c`:Now task 3 — the magic number:Now let's build everything:Clean build. Now let's test all three features:All correct — lexicographic ordering works exactly as expected. Now `//` comments:`//` comments work correctly and don't interfere with the `/` division operator. Now the magic number change:All three fixes confirmed: new files correctly start with `SOLE`, and old-format `PASC` files are correctly rejected with a clear error rather than misbehaving. Let's round-trip test the two new opcodes and run the full regression suite:Everything passes. Since I explicitly documented these as "not implemented" in the docs I just wrote, let me update them to stay accurate — this matters given the whole point of those docs was to reflect reality precisely.Now `docs/BYTECODE.md` — the magic number and the new opcode:Now `docs/ASSEMBLER.md` — the mnemonic list:No remaining stale references. Let's rebuild once more to be sure, then package everything up:## Summary

**String ordering comparisons** — `<`/`>`/`<=`/`>=` now work on strings, lexicographically (`'apple' < 'banana'` → `true`). One new opcode, `OP_SCMP`, normalizes `strcmp`'s result to `-1`/`0`/`1`; codegen then reuses the *existing* integer `LT`/`GT`/`LTE`/`GTE` opcodes to compare that against `0`, rather than needing four separate string-ordering opcodes.

**`//` line comments** — added alongside the existing `{ }` block comments. Verified it doesn't collide with the `/` division operator.

**Magic number** — `.bin` files now start with `SOLE` instead of `PASC`. This is a genuine breaking change to the file format: **any previously-compiled `.bin` file will no longer load** (correctly rejected with a clear error, verified). Since there was never a compatibility guarantee across this project's iterations, that's expected — but worth recompiling anything you've kept around from earlier sessions.

All three round-tripped correctly through `solas`/`desole`, and the full regression suite (control flow, strings, arrays, `for`, `write`/`writeln`, error recovery) still passes.

Since I'd just written the docs claiming these were unimplemented, I went back and corrected `README.md`, `docs/LANGUAGE.md`, `docs/BYTECODE.md`, and `docs/ASSEMBLER.md` to match — the ordering-comparison and `//`-comment items are removed from the "not implemented" lists, `SCMP` is documented in both the opcode reference and the assembler's mnemonic list, and the magic number reference is updated to `SOLE`.

