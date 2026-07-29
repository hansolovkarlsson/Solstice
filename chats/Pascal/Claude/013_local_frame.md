013_local_frame.md

# Prompt

What's next? I like the process of adding the features to the assembler since it seems to be faster to implement, and then we can focus on the Pascal compiler later on.

# Response

Good call on the strategy — it's worked well so far (jump opcodes, then strings, then arrays all followed this same "prove it out as a raw VM primitive first" pattern), and it applies well here too: it lets us get frame/scoping *bugs* worked out with hand-written `.sasm` where we can see exactly what's happening, instead of debugging new VM behavior and new parser/type-checker code at the same time.

**Next up: local variables and parameters — i.e., stack frames.** This is the piece that makes `CALL`/`RET` actually useful for real procedures rather than a fancy `GOTO`. Right now, `n` in the recursion example is a *global* — every recursive call shares the same slot, which happened to work for that demo (each level read-then-wrote it before recursing) but is wrong in general: two live calls need their own private copies.

Here's the design I'd propose, in the same spirit as everything so far — a clearly separate, bounds-checked region, distinct from the operand stack and the call stack:

- **A new frame stack** (`vm_frame_stack[]`) holding local variable slots, plus a **frame pointer** (`fp`) marking where the *current* call's locals begin.
- **`CALL`/`RET` get extended** to save/restore `fp` alongside the return address — each call-stack entry becomes a small `{return_addr, saved_fp}` pair instead of just an address, so returning correctly restores the *caller's* frame.
- **`OP_ENTER n`** — the first instruction of a procedure body: reserves `n` local slots (zero-initialized) on the frame stack and sets `fp` to their base.
- **`OP_LOAD_LOCAL k` / `OP_STORE_LOCAL k`** — access `vm_frame_stack[fp + k]`, the frame-relative equivalent of the existing global `LOAD`/`STORE`.
- **Parameters, to start with**: passed by value. The caller pushes argument values onto the existing *operand* stack before `CALL`, exactly like it already does for anything else — no new mechanism needed there. The callee's first few instructions after `ENTER` are just `STORE_LOCAL` calls pulling those pushed values off the operand stack into local slots `0..argcount-1`. Parameters and locals end up living in the same frame, just conventionally numbered — params first.

One thing worth deciding together: some real VMs (and most CPU calling conventions) put locals *on* the operand stack itself, addressed relative to `fp`, rather than in a separate region. That's more traditional, but this project has consistently kept every kind of state in its own clearly bounded array — I'd lean toward keeping that pattern rather than breaking it now. Happy to go either way, but wanted to flag it as a real choice rather than just picking silently.

Want me to run with the separate-frame-stack design as described, or would you rather I sketch out the shared-stack alternative side by side first?

# Prompt

Keep the convention so far with clear bounded arrays. Run with the separate-frame-stack as suggested.

# Response

Good — separate, bounded frame stack it is. Let me design the frame mechanics precisely before writing code.

**Key design decision**: when `CALL` happens, it needs to save enough to fully restore the caller's context on `RET` — not just where to jump back to, but also the caller's frame pointer *and* where the frame stack's top was, so `RET` can deallocate the callee's entire frame in one step regardless of its size. So each call-stack entry becomes a small record: `{return_addr, saved_fp, saved_frame_sp}`.

- **`OP_ENTER n`** — the first instruction of a procedure body: reserves `n` zero-initialized local slots on the frame stack, sets the frame pointer to their base.
- **`OP_LOAD_LOCAL k` / `OP_STORE_LOCAL k`** — access slot `k` relative to the current frame pointer, bounds-checked against the *current* frame's actual size (not the whole frame stack).
- **Parameters and return values both ride the existing operand stack** — no new mechanism needed. Caller pushes args before `CALL`; callee's first `STORE_LOCAL`s after `ENTER` pull them off. A "function" convention (not VM-enforced, just a convention `.sasm` follows) is: leave the result on the operand stack before `RET`.

Let's build it:Now `vm.c` — the frame stack itself:Let's evolve the call stack to a struct carrying the full restore record, and add the frame stack:Now add the local-slot bounds-checking helper, right after `vm_array_offset`:Now update `run_vm()` — initialize `fp`/`frame_sp`, and rewrite `OP_CALL`/`OP_RET`, plus add `OP_ENTER`/`OP_LOAD_LOCAL`/`OP_STORE_LOCAL`:Now find and rewrite `OP_CALL`/`OP_RET`:Now `solas.c` — `ENTER`/`LOAD_LOCAL`/`STORE_LOCAL` take plain immediate integers (locals aren't named symbols, just numeric slots relative to the frame):Let's update the doc header comment:Now `desole.c`:Now let's build everything:Clean build. Now let's test — starting simple (a local variable and a parameter), then the critical test: recursive `sum(n)` proving true per-call frame isolation.21 * 2 = 42, correct. Now the critical test — recursive `sum(n)`, which only produces the right answer if each recursive call truly has its own private local storage:`15 = 1+2+3+4+5` — exactly correct, and this could only be right if each recursive call's local `n` is genuinely isolated (if locals were shared, the addition after each recursive call returns would use a corrupted, overwritten `n`). Now the error paths:All three error paths correctly caught — and the frame-stack overflow fired at the right point (frame overflow at ~205 calls with 20-slot frames, correctly triggering before the 256-deep call-stack limit would). Now the round-trip test:Clean disassembly (note `call L5` correctly self-references the procedure's own start — the recursive call), byte-identical round trip. Now the full regression suite:All regressions pass, including the earlier bare `CALL`/`RET` tests (which never touch `ENTER`/locals — confirms `fp`/`frame_sp` staying at `-1` throughout them is harmless). Let me update the docs since I'd want them accurate before wrapping up, then deliver everything:`docs/BYTECODE.md` predates `CALL`/`RET` entirely — it needs a proper update, not just a patch. Let's look at the structure:Now let's add the full explanatory section, right before "## File format":Now `docs/ASSEMBLER.md` — update the mnemonic list and operand-kinds table:Let's check `README.md` — the Pascal *language* still doesn't have procedures (only the VM primitive exists so far), so the "not yet implemented" claim there is still technically accurate, but worth a clarifying note:Let's do a final rebuild and sanity check, then package everything:## Summary

Stack frames are working, verified, and documented — SolVM can now genuinely support recursion with proper per-call local variable isolation, still entirely at the VM/assembler layer.

- **`common.h`**: `OP_ENTER`/`OP_LOAD_LOCAL`/`OP_STORE_LOCAL`, `MAX_FRAME_STACK` (4096).
- **`vm.c`**: `vm_call_stack[]` evolved from bare return addresses to a `{return_addr, saved_fp, saved_frame_sp}` record — this is the key design point, since `RET` needs to fully restore the caller's frame context, not just jump back. A new bounds-checked `vm_frame_stack[]` holds locals for every active call.
- **`solas`/`desole`**: `ENTER n`, `LOAD_LOCAL k`, `STORE_LOCAL k` (plain integers — locals are numbered slots, not named symbols), `CALL`/`RET` extended accordingly.

**Verified:**
- Basic parameter + local temp (`double(21) = 42`).
- **The critical test**: recursive `sum(n)` computing `1+2+3+4+5 = 15` — this result is *only* correct if each recursive call's local `n` is genuinely isolated (a shared/global `n` would produce garbage the moment the recursion unwinds).
- All error paths: local access with no active frame, out-of-range local index for the current frame, frame-stack overflow (deliberately triggered *before* the call-stack limit using larger frames, confirming both limits are independently enforced).
- Round-trip through `desole`/`solas` byte-identical.
- Full regression suite — completely unaffected.
- Docs (`BYTECODE.md`, `ASSEMBLER.md`, `README.md`) updated with the new memory regions, opcode reference, a full "why does `CALL` save more than a return address" explanation, and a worked trace of the recursive example.

**Where things stand:** the VM-side foundation for procedures is now complete — call/return, recursion, and local scoping all proven out with hand-written bytecode. What's left is entirely on the Pascal-language side: parsing `procedure`/`function` declarations, assigning local slot numbers during codegen, type-checking parameters, and deciding on `var` (by-reference) parameters. That's the natural next step whenever you're ready to move from the assembler back to the compiler.

# Prompt

So my understanding of "store_local 0" "load_local 0" could also be solved with adding a "dup" opcode and do "dup" then "load_local 0"?

# Response

Good question, and yes — you're onto something real, but it's more limited than a full replacement for `store_local`/`load_local`. Let me walk through exactly where it helps and where it doesn't, using the `sum(n)` example.

**Where `dup` helps:** right at the top of the function, `store_local 0` followed immediately by `load_local 0` (nothing in between) is a pure round-trip — you're popping a value off the stack just to push the same value right back. That's always replaceable with `dup` first:

```
; before
enter 1
store_local 0
load_local 0
push 0
eq

; after
enter 1
dup              ; stack: [n, n]
store_local 0    ; pops one copy into local 0; stack: [n]
push 0
eq               ; uses the remaining copy directly
```

This is a completely standard, valid peephole optimization — real compilers do exactly this. It saves one instruction in exchange for one extra stack slot briefly.

**Where it doesn't help:** look at the `recurse:` label further down — it needs `n` *twice*, separated by a `sub` and a recursive `call`, and one of those two needs to survive the recursive call intact. `dup` can't reach across that. Once the value at the top of `sum`'s function got consumed by the `eq` comparison, there's no copy left anywhere for `recurse:` to `dup` — you'd have to go back and thread duplicates through *every* subsequent branch that might need the value, including branches that don't yet exist textually near where you computed it. That's the real distinction:

- **`dup`** only works for values used again *immediately*, in a straight line, with nothing else disturbing the stack in between.
- **`LOAD_LOCAL`/`STORE_LOCAL`** let you reference a value from *anywhere* in the function body — after a branch, inside a loop, across a call — without having to manually keep it alive on the stack the whole time.

This is exactly why real machines have both a stack (or registers) *and* addressable local storage rather than just one or the other — they solve different problems. `dup` is a nice tightening for the specific "compute once, use twice in a row" pattern; it's not a substitute for locals in general.

Concretely, in this exact program, `dup` would only ever save that one instruction at the top — the two `load_local 0`s inside `recurse:` are staying regardless.

**Is it worth adding?** I think yes, eventually — it's a genuinely standard stack-machine primitive (every real stack VM has one), cheap to add, and will matter more once the Pascal compiler is actually generating expression code, where the same "compute once, use twice" pattern shows up naturally (e.g. `x := f() + f()`, or the pattern you just spotted). But I'd hold off adding it *right now*, since we don't have a compiler emitting bytecode yet to tell us where it'd actually matter — adding it speculatively risks guessing wrong about the calling/codegen conventions before they exist. Want me to add a bare `DUP` opcode now anyway (cheap, VM/assembler-only, same pattern as everything else), or hold it until we're doing procedure codegen and can see where it's genuinely earned?
