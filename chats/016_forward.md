016_forward.md

# Prompt

Let's implement forward declaration.

# Response

Good — but implementing this properly reveals a real complication I need to handle carefully, not just add syntax for.

**The problem:** my current codegen relies on an invariant — "a procedure can only call itself or an earlier-declared procedure" — specifically so I never need to backpatch `CALL` targets (each procedure's entry address is already known by the time anything calls it). Forward declarations exist *precisely* to break that invariant: they let procedure A (which appears first) call procedure B (forward-declared before A, but not actually *defined* until after A). Once that's allowed, A's body might get generated *before* B's entry address is known.

**The fix:** switch `CALL` target resolution from "always immediately known" to backpatched — the same pending-jump-list technique already used for `break`/`continue`, just applied to calls, patched once in a final pass after every procedure *and* main have been generated. This is strictly more general (and no more complex) than what's there now, and it doesn't change behavior for any existing non-forward case.

**Also discovered while designing this:** the completing definition (`procedure B; begin ... end;`, no parameter list — parameters aren't re-specified, matching classic Pascal) needs its body to reference parameters by name. That means `ProcSymbol` needs to store parameter *names*, not just types, which I hadn't needed before.

Let's build it. Starting with `common.h`:Now extend `ProcSymbol` with parameter names and the `is_forward` flag:Now `lexer.c` — the `forward` keyword:Now `codegen.c` — the backpatching mechanism for `CALL` targets. Let's view the current state:Now let's add the pending-call backpatch mechanism and update `NODE_CALL` and `generate_program()`:Now update `generate_program()`: reset `pending_call_count`, and patch every pending call at the end:Now `parser.c` — the biggest piece. Let's view the current `procedure_declaration()` and surrounding helpers:Now let's rewrite `add_proc` to defensively reset the new fields (stale data from a previous compile reusing the same `proc_table` slot is exactly the class of bug this project has hit before), then rewrite `procedure_declaration()` entirely:Now the validation after the procedure-declaration loop in `parse_ast()`:Now the full rewrite of `procedure_declaration()`:Let's build:Now the real test — mutual recursion via forward declaration (the classic `isEven`/`isOdd` example):That's the restriction working correctly — the completing definition shouldn't re-list parameters (classic Pascal rule). Let me fix the test:That's exactly the right proof case — and it genuinely exercises the backpatch mechanism, not just coincidentally works: `isOdd` occupies `proc_table[0]` (registered first via its forward declaration), `isEven` is `proc_table[1]`. So when `isOdd`'s body is code-generated (first, since procedures generate in table order), its call to `isEven` targets an address that doesn't exist yet. Let's confirm this directly in the disassembly:Confirmed — `isOdd` (at `L1`) contains `call L15`, a genuine forward jump to `isEven`'s body which comes *after* it in the code. That's the backpatch mechanism proven in real compiled output, not just working by coincidence. Now the error cases:All three correctly caught. Let's test a parameterless forward declaration too, and then round-trip and full regression:Perfect alternation — `ping 1, pong 2, ping 3, pong 4, ping 5, pong 6` — confirming parameterless mutual recursion works too. Byte-identical round trip. Now the full regression suite:All regressions pass — the switch to universal backpatching correctly preserved every existing non-forward case. Let's update the docs, then package and deliver:Now `README.md`:Let's check `docs/BYTECODE.md` — it may still claim `CALL` targets never needed backpatching, which is now false:No stale claim there — that's about `JZ`/`JMP` generally, still accurate. Let's do a final rebuild and package everything:All correct. Let's package and deliver:## Summary

`forward` declarations and mutual recursion are complete and thoroughly verified.

**The real work wasn't the syntax** — it was recognizing that forward declarations break a load-bearing assumption in the existing codegen: "a procedure's entry address is always known by the time anything calls it." Mutual recursion means procedure A can call procedure B before B's real body has been generated. I switched `CALL` target resolution from immediate to backpatched (same pending-list technique as `break`/`continue`), and confirmed via disassembly that the mechanism is genuinely exercised — in the `isEven`/`isOdd` test, `isOdd` ends up at `proc_table[0]` (registered first via its forward declaration) and its body contains `call L15`, a real forward jump to `isEven`'s not-yet-generated code at the time.

**Also required**: `ProcSymbol` needed to start storing parameter *names*, not just types — the completing definition (`procedure isOdd; begin ... end;`) doesn't re-list parameters, so the parser replays the forward declaration's recorded names through `add_local()` to make them referenceable in the body.

**Verified:**
- Mutual recursion with parameters (`isEven`/`isOdd`, computing correct true/false results) and without (`ping`/`pong`, alternating correctly across 6 calls).
- All three deliberate error paths: unfulfilled forward declaration, calling a procedure before even its forward declaration exists, and re-specifying parameters on the completing definition.
- Byte-identical round trip through `desole`/`solas`.
- Full regression suite — every earlier feature, including the pre-forward-declaration procedure/parameter tests, still passes unchanged, confirming the switch to universal backpatching didn't alter behavior for any existing case.
- Docs updated: `LANGUAGE.md` gained a full "Forward declarations" subsection with a worked example (independently verified to run and match its documented output), and the "not implemented" list and `README.md` status corrected.

**What's left toward "full" procedures**: functions (procedures that return a value — the calling convention was already proven at the VM level) and array parameters/locals. Both are the natural next steps whenever you want to continue.

# Prompt

I'm getting this warning when I compile. And have had it for a while. Is it a concern?
ld: warning: reducing alignment of section __DATA,__common from 0x8000 to 0x4000 because it exceeds segment maximum alignment

# Response

Good question to check on — let me confirm exactly what's causing it before reassuring you, since I want to give you a precise answer rather than a hand-wavy one.That's the answer — `proc_table` is 17.2 KB, just over the 16 KB boundary, which is exactly why the linker's alignment heuristic jumps to requesting 32 KB (`0x8000`) for it, and then has to cap it back down to the 16 KB (`0x4000`) page-alignment limit that macOS's Mach-O segments allow. (`string_pool` at 64 KB is even bigger and would trigger the same thing, possibly on its own line if you have multiple such warnings.)

**Short answer: not a concern.** This is purely a linker alignment-preference notice, not an error, and it has zero effect on program correctness. Nothing about how these arrays are accessed requires more than a few bytes of alignment (an `int` needs 4-byte alignment, a `struct` needs the alignment of its largest member) — 16 KB is already vastly more generous than what's actually needed. The linker is just saying "I'd have preferred to page-align this a bit more aggressively for cache/mapping efficiency, but I'm capped, so I'm using the cap instead" — which is exactly as correct, just marginally less optimal in a way that doesn't matter for data of this size.

**Why it's showing up now, specifically**: `proc_table` and `string_pool` are both recent additions (procedures and strings), and both are declared `extern`-visible (non-`static`) global arrays without initializers — which puts them in Mach-O's `__DATA,__common` section, the specific section this alignment heuristic applies to.

If you'd like a fully silent build anyway (purely cosmetic, your call), the standard one-line fix is adding `-fno-common` to the Makefile's `CFLAGS`, which tells the compiler to emit these as ordinary zero-initialized data instead of tentative "common" definitions, sidestepping this particular linker heuristic entirely. I can add that for you if you want — but I'd only frame it as tidiness, not a fix for anything actually broken. Want me to add it?

# Prompt

Sure, add it.

# Response

Let's rebuild everything to confirm this doesn't break anything (it shouldn't — it only affects where the linker places these symbols, not program behavior):Clean build. Since this warning is specific to Apple's `ld` on macOS, I can't directly reproduce it in this Linux sandbox — but I can confirm the flag applies cleanly and doesn't change program behavior at all:Everything behaves identically, as expected. Let's deliver the updated Makefile:One line changed — `CFLAGS` now includes `-fno-common`. On your Mac, `proc_table`/`string_pool`/etc. should now land in ordinary zero-initialized data instead of Mach-O's `__DATA,__common` section, which should quiet that specific linker warning. Rebuild with `make clean && make` and you should get a silent build; let me know if the warning somehow persists (unlikely, but if Apple's linker is doing something slightly different than the standard mechanism I described, worth knowing).