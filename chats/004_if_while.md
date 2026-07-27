# 004_if_while.md

# Prompt

Sounds great. Let's continue with if/while/repeat

# Response

This is a substantial addition — let me plan it out before touching code.

**What needs to change:**
1. `common.h` — new tokens (`if/then/else/while/do/repeat/until`), new AST node types, and — this is the key structural change — the `ASTNode` struct needs a **4th child pointer**. `if` needs condition + then-branch + else-branch, but the struct only has `left`/`right`/`next` (and `next` is reserved for "next statement in the enclosing list").
2. `lexer.c` — recognize the new keywords.
3. `parser.c` — this is the biggest change. Right now statement parsing is a flat inline loop inside `parse_ast()`. I need to refactor it into proper `statement()` / `statement_list()` / `compound_statement()` functions that can recurse (an `if`'s then-branch can itself contain a nested `if`, or a whole `begin...end` block).
4. `type_checker.c`, `optimizer.c`, `ast_printer.c` — extend their generic tree-walks to cover the new node type and the new 4th pointer (this is exactly the class of bug we found and fixed once already with the DCE `next`-chain issue, so I'll be careful here).
5. `codegen.c` — the real work: emit `OP_JZ`/`OP_JMP` with **backpatching** (emit a jump with a placeholder target, remember where it is, fill in the real target once we know it).

One thing I caught while planning: `codegen.c`'s `NODE_COMPOUND` case currently emits `OP_HALT` unconditionally after its body. That was fine when a compound statement only ever appeared once, as the whole program's root. But `if`/`while` bodies can now *also* be `begin...end` blocks — so without a fix, `if x then begin ... end` would halt the entire VM partway through, not just end the block. I'll separate "generate this block" from "the program is over," so only the true top-level program gets the trailing halt.

Let's start with `common.h`:Now `lexer.c` — recognize the new keywords:Now the big one — `parser.c`. Let's view it in full first:Now let's replace the statement-parsing section with the refactored `statement()`/`statement_list()`/`compound_statement()` design, and fix `free_ast()` to recurse into the new `extra` pointer:Now replace the body-parsing section of `parse_ast()` with calls into the new functions, and add the functions themselves:Structure is correct. Now fix `free_ast()` to recurse into the new `extra` pointer — this is exactly the class of bug we hardened against before:Now `type_checker.c`:Now `optimizer.c` — need to extend `optimize_ast`'s recursion, `mark_used_variables`, and `sweep_dead_assignments`'s fallback, all to cover `extra`:Now `ast_printer.c` — add print cases for the 3 new node types:Now the core work — `codegen.h` and `codegen.c`. First expose a new `emit_halt()` function:Now `codegen.c`:Now update `pascalc.c` and `test_recovery.c` to call `emit_halt()` explicitly (the automatic halt is gone from `NODE_COMPOUND`):Now let's build — with `-Wall -Wextra`, missing `switch` cases on the `NodeType`/`TokenType` enums will show up as warnings, which is exactly the safety net I want here:Zero warnings — confirms both `codegen.c`'s and `ast_printer.c`'s switches over the `NodeType` enum are exhaustive, which is exactly the safety net I wanted. Now the real tests:All correct: `1` printed for the true branch, `99` correctly skipped (condition false), and the nested `begin...end` then-block correctly printed `100, 200` while the else-block (`300`) was correctly skipped. Now `while` and `repeat`:Both correct. Let's use `desole` to inspect exactly what the new codegen produces for the `while` loop — this is the payoff of having built the disassembler first:That's the exact same bytecode we hand-assembled earlier for the countdown loop — the compiler's new codegen produces exactly what a human would write by hand. Let's test a few more things: nested control flow, dead-code elimination correctly seeing into `if` branches, and the new type checks rejecting non-boolean conditions:Let me re-check the nested test without truncation, and add one more DCE check — a variable that genuinely *is* dead even though it's inside an `if`:Both correct. Now let's run the full existing regression suite to make sure nothing from all our earlier work broke:Everything still works — no regressions in any of the earlier fixes (bounds checking, error recovery, `readln` type enforcement, the assembler/disassembler round trip). Let's finalize:## Summary

Pascal for Rascal now supports `if`/`then`/`else`, `while`/`do`, `repeat`/`until`, and arbitrarily nested `begin...end` blocks (as both program bodies and control-flow bodies).

**Structural changes:**
- `ASTNode` gained a 4th pointer (`extra`) — needed because `if` requires condition + then + else, one more than `left`/`right` alone can hold. Every generic tree-walk (`free_ast`, `type_check`, `optimize_ast`, `mark_used_variables`, `sweep_dead_assignments`) was updated to recurse into it — the same class of bug as the DCE `next`-chain issue we fixed earlier, deliberately checked carefully this time.
- `parser.c` refactored from an inline flat statement loop into proper recursive `statement()` / `statement_list()` / `compound_statement()` functions — required since statement bodies can now nest arbitrarily.
- **Caught and fixed a real bug before it shipped**: `codegen.c`'s `NODE_COMPOUND` case used to auto-emit `OP_HALT`, which only worked because a compound block used to appear exactly once (the program root). Now that `if`/`while` bodies can be `begin...end` blocks too, that would have halted the VM mid-loop. Fixed by moving the halt to an explicit `emit_halt()`, called once by `pascalc.c` after the whole program is generated.

**Verified:**
- `if`/`then`/`else` (with and without `else`, single-statement and block bodies), `while`, `repeat`/`until`, and nested `if`-inside-`while` — all correct.
- Dead-code elimination correctly sees into `if` branches both ways: doesn't remove a variable only used in a condition/else-branch, and does remove one that's genuinely dead in both branches.
- Type checker correctly rejects a non-boolean `while` condition.
- `desole` confirmed the compiler's own `while`-loop codegen produces **byte-identical** output to the loop we hand-assembled earlier — a nice validation that the whole toolchain agrees with itself.
- Full regression suite (bounds checks, error recovery, `readln` type enforcement, `solas`/`desole` round trip) — all still pass.
- Clean build, zero warnings — `-Wall -Wextra` caught nothing missing across the `NodeType` switch in both `codegen.c` and `ast_printer.c`, confirming exhaustive coverage of the 3 new node types.

Pascal for Rascal can now express real programs with loops and conditionals, compiled all the way down through a verified, jump-capable bytecode VM.

# Prompt

make a pascal program testing the if, while, repeat

# Response

Now let's compile and run it, and work out the expected output by hand so we can verify correctness rather than just eyeball it:Let me verify this by hand against the source, since that's the whole point:

- **`if n > 5`** (n=7, true) → `1`. **`if n > 100`** (false, no `else`) → correctly nothing.
- **`while i <= 10`**, printing `i` if even else `-i`: `-1, 2, -3, 4, -5, 6, -7, 8, -9, 10`, then `sum` (1+2+...+10) → `55`.
- **`repeat...until n = 5`**, printing `n*n` each pass: `1, 4, 9, 16, 25`.
- **`if sum > 50`** (55 > 50, true) → nested `while i > 0` counting down from 3: `3, 2, 1`.

Actual output: `1, -1, 2, -3, 4, -5, 6, -7, 8, -9, 10, 55, 1, 4, 9, 16, 25, 3, 2, 1` — matches exactly, line for line. Final state (`i=0, n=5, sum=55, isEven=1`) is correct too.It exercises `if`/`then`/`else`, `if` with no `else`, `while`/`do`, `repeat`/`until`, and both a nested `if`-inside-`while` and a nested `while`-inside-`if` — a decent stress test of all the codegen backpatching logic from last session, and it all checks out.

