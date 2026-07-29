012_call_ret.md

# Prompt

What's next?

# Response

Good moment to check in — the "small stuff" list is now empty, which actually simplifies this answer a lot.

**What's left, and it's genuinely just one item of real size:**

- **Procedures and functions.** Every other gap on the original list (`ord`/`chr`, in particular) is now *blocked on* this rather than being independently small — you can't have builtin functions without having functions. Multi-dimensional arrays and `real`/floating-point are the only truly independent remaining items, and both are smaller, more mechanical extensions of things that already exist (arrays generalize via row-major offset math; `real` is a new but bounded data representation, similar in shape to how `char` slotted into the existing `string` machinery).

**Why procedures/functions is the one worth doing next:** it's the last thing standing between this being "a compiler with a lot of features" and "a real language you could write real programs in." Every test program in this whole project so far has been one flat block — no code reuse, no recursion, nothing you'd call a "library" in even the smallest sense. It's also the direct prerequisite for everything on your original vision (OOP methods are just procedures with an implicit `self`; precompiled library modules need callable entry points to link against).

**What it actually requires**, concretely:
- A real call stack in SolVM — `CALL`/`RET` opcodes, stack frames
- Parameter passing (by value at minimum; by reference if you want `var` parameters, which real Pascal has)
- **Local variable scoping** — this is the part that's architecturally new, not just bigger. Right now every variable in a program lives in one flat global `sym_table`/`vm_vars`. A function's locals need to exist only for the duration of that call, correctly shadow outer names, and not collide with a *different* call to the *same* function (recursion is really the thing that forces this — without recursion you could almost cheat by keeping everything flat, but recursion means the same function's locals need multiple simultaneous live copies)

This is a bigger jump than anything built so far — closer in shape to when we did the jump-opcode work, but for calls instead of branches, plus that scoping problem, which is genuinely new territory for this codebase.

Given the size, I'd suggest we start with a design discussion before touching code — nail down the calling convention (how args and locals sit relative to the stack, how `CALL`/`RET` interact with the existing flat `code[]`, whether `var` parameters are in scope for v1 or a later pass) — rather than diving straight into implementation the way we could with the smaller features. Want to start there?

# Prompt

Yes, let's start there, with call/ret. And perhaps if we start with an implementation on the assembler and VM first would break down the complexity a little?

# Response

Good instinct — isolating this at the VM/assembler layer means we can fully prove out recursion and call-stack correctness with hand-written `.sasm` before touching the parser at all, where any bugs would otherwise be tangled up with new syntax and type-checking too.

**Scope for this session: bare `CALL`/`RET` only** — no parameters, no local variable scoping yet. Just "jump to a subroutine, remember where to come back to, come back." That's the real foundational primitive — recursion in particular only works correctly if return addresses live on a proper stack (not some single global "return address" variable, which would break the instant a procedure calls itself). Parameters and local scoping are a separate, harder problem (right now every variable in this VM is global) that I'd rather tackle once this primitive is solid.

**Design:**
- A **separate call stack** from the existing operand stack (`vm_stack`) — mixing return addresses with operand values on one stack is exactly how stack-smashing-style bugs happen, and this project has consistently kept different kinds of state in separate, bounds-checked regions (`vm_stack`, `vm_vars`, `vm_array_mem`).
- `OP_CALL <target>` — push the return address (the instruction right after this `CALL`) onto the call stack, jump to `target`.
- `OP_RET` — pop the call stack, jump there.
- Both bounds-checked: call-stack overflow catches runaway/infinite recursion cleanly instead of crashing; `RET` with an empty call stack (malformed bytecode calling `RET` without a matching `CALL`) is also caught.
- `solas`/`desole` get `CALL <label>` (same two-pass forward-reference-capable label resolution as `JMP`/`JZ`) and `RET`.

Let's build it:Now `vm.c` — the call stack itself:Now let's find `run_vm()` and add the `call_sp` local, plus the `OP_CALL`/`OP_RET` handling:Now let's add the `call_sp` local and the `OP_CALL`/`OP_RET` cases:Now `solas.c`:Now `desole.c`:Now let's build everything:Now let's test. First, a simple two-call-site test:Correct — the same procedure body called from two different sites, both returning to the right place. Now the real test — **recursion**, which is the whole reason a proper call stack matters:Exactly right — and since `countdown` recurses 5 levels deep before unwinding, and each unwind correctly returns to the right call site, this proves the call stack is genuinely stack-based, not a single shared "last return address" slot (which would have broken immediately here). Let's test nested (non-recursive) calls too, then the error paths:Both error cases caught correctly, and the nested-call trace (`1, 2, 99, 3, 4`) confirms proper LIFO unwind order — `c` returns into `b`, `b` returns into `a`, `a` returns into `main`. Now the round-trip test:Clean disassembly — `call L4` synthesizes the label exactly like `JMP`/`JZ` do. Round trip byte-identical. Let's finish with the full regression suite to confirm the compiler frontend (untouched this session) and everything else still works:All existing behavior is completely unaffected — exactly as expected, since `pascalc` doesn't emit `CALL`/`RET` yet. Let's finalize:## Summary

`CALL`/`RET` are working, verified, and isolated entirely at the VM/assembler layer — the Pascal compiler frontend hasn't been touched, so this is a clean addition on top of everything else.

- **`common.h`**: `OP_CALL`/`OP_RET`, `MAX_CALL_DEPTH` (256).
- **`vm.c`**: a separate `vm_call_stack[]` with its own bounds-checked push/pop, kept entirely distinct from the operand stack. `CALL` pushes the post-instruction `ip` and jumps; `RET` pops and jumps back.
- **`solas`/`desole`**: `CALL <label>` (same two-pass forward-reference resolution as `JMP`/`JZ`) and `RET`; disassembly synthesizes labels at `CALL` targets the same way it already does for jumps.

**Verified:**
- Multiple call sites correctly reuse one procedure body and each returns to its own call site.
- Nested (non-recursive) calls unwind in correct LIFO order (`a → b → c`, then `c → b → a`).
- **Recursion** — the real test — a procedure calling itself 5 levels deep, unwinding correctly through all 5 return addresses. This is the case that would break immediately with a naive "one shared return-address variable" implementation and only works because it's a proper stack.
- Both error paths: call-stack overflow (infinite recursion caught cleanly at depth 256, not a crash) and `RET` with no matching `CALL` (malformed bytecode caught immediately).
- Round-trip through `desole`/`solas` byte-identical.
- Full regression suite — completely unaffected, as expected.

This is a solid foundation. The next piece — and it's a meaningfully different problem, not just "more of this" — is **local variable scoping and parameter passing**: right now anything a "procedure" touches (like `n` in the recursion example) is still a global variable, which is fine for a bare demo but means two concurrent recursive calls can't each have their own private copy of a local. That's the real reason procedures/functions is architecturally hard, and it's what I'd want to design next before wiring any of this into Pascal syntax.

# Prompt

Just to confirm, the return stack is separate from the data/parameter stack?

# Response

Yes — confirmed, they're two completely separate arrays:

```c
static int vm_stack[MAX_STACK];           // the operand/data stack
static int vm_call_stack[MAX_CALL_DEPTH]; // return addresses only
```

Each has its own stack pointer (`sp` for `vm_stack`, `call_sp` for `vm_call_stack`) and its own bounds-checked push/pop helpers (`vm_push`/`vm_pop` vs `vm_call_push`/`vm_call_pop`), so there's no way for one to spill into or corrupt the other.

That was a deliberate choice, not an accident — it follows the same pattern this VM already uses everywhere else (`vm_stack`, `vm_vars`, `vm_array_mem` are all separate, bounds-checked regions rather than one shared blob). The specific reason it matters for `CALL`/`RET`: if return addresses lived on the same stack as operands, a procedure that pushes values for arithmetic (or leaves the stack slightly unbalanced — easy to do by accident once you're hand-writing `.sasm`) could overwrite or misalign a return address sitting right underneath it, and `RET` would jump to garbage. Keeping them separate means a bug in a procedure's operand-stack usage can *never* corrupt control flow, and vice versa.

It also matters for what comes next: once local variables and parameters exist, they'll most likely live in yet another separate region tied to the call stack (a per-call "frame"), rather than getting mixed into either of these two. Keeping the return-address stack pure right now — nothing on it but return addresses — is exactly what makes that extension straightforward later instead of requiring a redesign.

