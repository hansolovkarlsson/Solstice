024_next.md

# Prompt

What to focus on next?

# Response

Good moment to step back and take stock. Here's the honest landscape, roughly ranked by how much I think each one matters:

## The big one: `real` / floating-point numbers

This is the single largest remaining gap, and genuinely the most "core Pascal" feature still missing — ISO Pascal's two fundamental numeric types are `integer` and `real`, and right now this VM only has the first. It's also architecturally the biggest undertaking by far, worth being upfront about before committing to it:

Every value in this VM — `vm_stack`, `vm_vars`, `vm_array_mem`, `vm_frame_stack` — is an array of plain C `int`. Strings, chars, and booleans all cleverly ride on top of that (pool indices, 0/1) without needing a new representation. Floats can't do that trick the same way, but there's a viable path: since a C `float` and `int` are typically the same 4-byte width, a `real` value could be stored in the *same* `int`-sized slots, just reinterpreted bit-for-bit (via a union or `memcpy`) whenever a float-specific opcode touches it — a parallel set of opcodes (`FADD`, `FSUB`, `FCMP`, `FPRINT`, etc.) alongside the existing integer ones, plus `int`↔`real` conversion. That keeps every existing storage array untouched, which is the difference between "a few sessions" and "rewrite the VM." Still a genuinely large, multi-session feature touching the lexer, parser, type checker, every arithmetic/comparison opcode, and `write`/`writeln` formatting (float-to-string is its own small can of worms).

## The second core gap: records

`record ... end` is the other "fundamental Pascal" feature that's never been touched. I think this is more tractable than floats, not less important — it can likely reuse the same "flat contiguous block, compile-time-computed offset" pattern arrays already use (field access is just `base + field_offset`, always statically known, no new addressing mode needed). The type-checking side is more novel than the runtime side (heterogeneous named fields vs. arrays' homogeneous indexed ones), but nothing about it looks architecturally risky the way floats do.

## Smaller, well-scoped items sitting in the backlog

These wouldn't move the needle much individually, but they're genuine, honest gaps from earlier sessions, each probably a single focused session:
- 2D array parameters/locals (1D already has both)
- `readln` into a parameter/local, and `for`-loops with a local loop variable — both restrictions from the very first parameters session
- `s[i] := ...` string mutation via copy-on-write (build a new string, re-point the variable — same desugaring technique already used for `inc`/`dec`)

## Explicitly deferred, could revisit

Dynamic arrays / array `copy`-slicing — you asked to push this out last time, still there if you want it now that 2D arrays and array bounds functions are settled.

---

**My honest recommendation**: `real` is the more valuable long-term gap, but it's a real architectural commitment — I'd want to spend a chunk of the first session just on the design (representation, which opcodes get float twins, how mixed `int`/`real` expressions coerce) before writing any code, the same way we did for stack frames and array parameters. Records are lower-risk and would also be genuinely useful on their own. If you'd rather bank a quick, satisfying win first, any of the small items would close cleanly in one session.

Which direction sounds right to you?

# Prompt

1. Real - large change
2. Records - mid complex
3. 2D array parameters/locals, readln, for-loop, string mutation
4. Dynamic arrays
* Recommended - real
    - Do first in VM
    - Then Pascal compiler

    

