028_

# Prompt

What's next

# Response

Good moment to take stock — this project has grown substantially. Here's the landscape:

## Smaller, contained items still open

- **`s[i] := ...` string mutation** — `s[i]` is still read-only. Same copy-on-write desugaring trick already used for `inc`/`dec` would work (`s := copy(s,1,i-1) + newchar + copy(s,i+1,length(s))`).
- **`for`-loops with a local loop variable, and `readln` into a parameter/local** — two restrictions dating back to the very first parameters session. Worth revisiting now that the compiler has far more type/mechanism maturity than it did then.
- **2D array parameters/locals** — 1D arrays support both; 2D was scoped global-only.

## Bigger items needing their own design conversation

- **Record parameters/locals, or arrays of records** — needs a real addressing model (like arrays use), not more sugar over globals.
- **Dynamic arrays / array `copy`-slicing** — explicitly deferred a while back.
- **Sets, enumerated types** — never scoped at all. Sets in particular are a genuinely distinctive ISO Pascal feature (`[1, 3, 5]`, `in`, set union/intersection) that would meaningfully move the needle on "full-featured Pascal."
- **Units/modules (`uses`)** — a much bigger structural undertaking (multi-file compilation), probably out of scope for this project's shape.

## Something different: correctness/quality passes, not new features

Given how much surface area this compiler now has (integers, reals, booleans, strings, chars, 1D/2D arrays, records, procedures/functions with recursion, forward declarations), it might be worth a session that isn't about adding anything new at all — auditing for gaps between features that were each individually tested but never tested *together*. Concrete example: does a `real` field in a record work correctly? Does `sqrt` work on a 2D array element? Does `power()` work as a function argument? These interaction points are exactly where bugs like the two caught this conversation (the `widen_to_real` double-widening bug, the `NODE_ARRAY_ASSIGN_2D` DCE gap) tend to hide.

---

**My honest take**: if you want to keep pushing toward "full-featured Pascal," **sets** are the most distinctive remaining ISO feature and would be a genuinely good next target — Pascal without sets feels noticeably incomplete to anyone who's used the language before. If you'd rather consolidate what's already built, an interaction/regression audit across feature combinations would be time well spent given the codebase's current size. And if you want something quick and contained, `s[i] :=` is sitting right there.

Which direction?

# Prompt

let's go with string mutation

# Response

Let me check the current state precisely before designing this — how `s[i]` reads work, how local-slot addressing works, and how `statement()`'s array-write path handles the "not an array" fallback that currently rejects `s[i] := ...`.This "load old value, compute new value on the stack, store back" pattern lets me reuse the *existing* `LOAD`/`STORE`/`LOAD_LOCAL`/`STORE_LOCAL` opcodes entirely, needing only **one** new, fully generic opcode in between (no duplication for global vs. local). Let's check `common.h`'s `MAX_STRING_LEN` and the statement()'s current local/global assignment paths to plan the exact insertion:Now I have everything I need. Let me implement this. First, `common.h`:Good, syntax is correct. Now the opcode:Now `vm.c` — implement `OP_STR_CHAR_REPLACE`:Now `type_checker.c`:Now `codegen.c`:Now `ast_printer.c` (no `default:` — required):Now `optimizer.c` — let's add DCE support for `NODE_STRING_INDEX_ASSIGN`, matching how `NODE_ARRAY_ASSIGN_2D` was handled:Let's confirm `mark_used_variables`'s generic recursion already covers this node's children (so uses of *other* variables within the index/value expressions are still correctly tracked):Confirmed, no changes needed there. Now the biggest remaining piece — `parser.c`. First, `parse_global_assignment`:Now the local variant in `statement()`'s local-variable assignment path:Wait — I need to check the token consumption order carefully, since `match(TOKEN_IDENTIFIER)` was previously *inside* the block I inserted before. Let me view the actual result:Confirmed a real bug — `match(TOKEN_IDENTIFIER)` was still inside the final fallback block, so my new check ran while `token` still pointed at the identifier itself, never at `[`. My new branch was completely unreachable. Let me fix the ordering:Now let's double-check `parse_global_assignment`'s call site — is the identifier already consumed before it's called?Good, `parse_global_assignment` was correctly ordered from the start — only the local case needed the fix, which is now applied. Let's build:Clean build. Now let's test thoroughly, starting with the basic case on both global and local strings:All correct, including local string mutation and the loop-based full-string uppercasing (confirming repeated mutations work correctly). Now the error paths:Both showed `exit=0` unexpectedly — this smells like the same DCE mistake I've made a few times before in this project (forgetting to actually *read* the variable, so my own new DCE support eliminates the mutation before it ever runs). Let me fix the tests:Both correctly caught once I actually read the variable afterward. Now let's explicitly verify the DCE behavior itself (both directions), matching the established verification pattern from earlier sessions:Confirmed correctly in both directions. Now round-trip and the full regression suite:Found a real round-trip bug — I forgot to add `solas.c`/`desole.c` mnemonic support for `OP_STR_CHAR_REPLACE` entirely. Let me verify and fix:Let's rebuild and re-verify the round-trip:Confirmed fixed — round-trip is now byte-identical. This was a real bug that would have silently corrupted any string-mutation program on disassembly/reassembly, caught before declaring success. Let's re-run the full regression suite to confirm nothing else is affected:All still passing. Now the documentation — let's fix the stale "read-only" claim in the String section:I've hit the tool-use limit for this response, so let me give you an honest status report rather than call more tools.

## What's implemented and thoroughly verified

**Full `s[i] := ...` string mutation**, via a copy-on-write approach: one new, fully generic opcode (`OP_STR_CHAR_REPLACE`) that reuses the *existing* `LOAD`/`STORE`/`LOAD_LOCAL`/`STORE_LOCAL` opcodes rather than needing separate global/local variants — codegen just wraps the existing load/store around it. Works for both global and local string variables.

**A real bug caught mid-parser-implementation, not after testing**: when I first wired the local-variable write path, I inserted the new `[` check *before* the existing `match(TOKEN_IDENTIFIER)` call that used to live further down — meaning the check ran while `token` still pointed at the identifier itself, never at `[`, making the new branch completely unreachable. Caught by re-reading the exact code I'd just written before building, not by trial and error.

**A second real bug, this one caught by testing**: I forgot to register `OP_STR_CHAR_REPLACE`'s mnemonic in `solas.c`/`desole.c` entirely. This didn't show up in ordinary compile/run testing — it only surfaced during the round-trip check, where the reassembled program silently truncated every mutated string to one character. This is exactly the kind of bug that's invisible unless you specifically test disassembly/reassembly, which is why that check exists as a standing practice in this project. Found and fixed before moving on.

**Verified working**: mutation on global and local strings, repeated mutations in a loop (uppercasing an entire string character-by-character), read-after-write consistency, out-of-range index correctly rejected (strict, matching read-side `s[i]` behavior — unlike `copy`'s intentional leniency), wrong value type rejected at compile time, multi-character value rejected at runtime, dead-code elimination correctly handling both directions (confirmed via the same "forgot to actually read the variable" test mistake I've made a few times before in this project — caught and fixed before treating it as a real finding), byte-identical round-trip (after the fix above), and the full project regression suite passing.

## What's NOT done yet

- `docs/LANGUAGE.md`'s String section still has the stale "`s[i]` is read-only" claim — I was mid-edit on this exact line when I ran out of room
- No other documentation updates: `docs/BYTECODE.md` and `docs/ASSEMBLER.md` don't list the new opcode/mnemonic yet, `README.md`'s status line is stale
- Nothing has been copied to outputs or delivered via `present_files`

Please continue in a follow-up turn so I can finish the documentation and deliver everything properly.

# Limit

Let's finish this properly. First, let's view the exact context around the stale claim:Let me verify a comprehensive example before writing the corrected documentation:Verified — this confirms the copy-on-write approach correctly preserves `t`'s value even though `s` and `t` initially shared the same pooled entry. Now let's rewrite the documentation:Let's check for any other lingering mentions of this limitation elsewhere:Good, no other mentions. Now let's update `docs/BYTECODE.md`'s opcode reference:Now `docs/ASSEMBLER.md`'s mnemonic list:Now `README.md`'s status line:Final rebuild and sanity check, then package and deliver:All still correct. Let's package and deliver:## Summary — `s[i] := ...` string mutation, complete

Everything is now implemented, thoroughly tested (including two real bugs found and fixed along the way), and fully documented.

**Design**: `s[i] := val` uses copy-on-write under the hood — one new, fully generic opcode (`OP_STR_CHAR_REPLACE`) that builds an entirely new string with the one character replaced and interns it, rather than mutating the pooled entry in place. This matters because this compiler's strings are deduplicated in a shared pool, so two variables holding equal values can point at the *same* entry — verified directly with a test where `s` and `t` both hold `'Hello'` and share that pooled entry, then `s[1] := 'J'` correctly leaves `t` untouched at `'Hello'`.

**Two real bugs caught, both before I called anything finished:**
1. A parser ordering bug — I inserted the local-variable write-path check *before* the point where the identifier token was actually consumed, making the new branch structurally unreachable. Caught by re-reading the code, not by trial and error.
2. A round-trip bug — I forgot to register the new opcode's mnemonic in `solas.c`/`desole.c` at all, which silently truncated every mutated string to one character on disassembly/reassembly. This didn't show up in ordinary compile-and-run testing at all — only the standing round-trip check caught it, which is exactly why that check exists as a habit in this project.

**Fully verified**: mutation on both global and local strings, repeated mutations in a loop, read-after-write consistency, strict out-of-range rejection (matching read-side `s[i]`, unlike `copy`'s intentional leniency), wrong-type and multi-character rejection, dead-code elimination correctly handling both directions, byte-identical round-trip after the fix, and the full project regression suite.

**Documentation**: the stale "`s[i]` is read-only" claim in `docs/LANGUAGE.md`'s String section is replaced with an accurate description of the copy-on-write mechanism, including the verified shared-pool-safety example. `docs/BYTECODE.md` and `docs/ASSEMBLER.md` updated with the new opcode, and `README.md`'s status line updated.

From the earlier list: `for`-loop/`readln`-local restrictions and 2D array parameters/locals are the remaining contained items; records-in-arrays, dynamic arrays, and sets are the bigger design-first candidates whenever you're ready for one of those.

