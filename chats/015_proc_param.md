015_proc_param.md

# Prompt

What's next?

# Response

We're at a nice inflection point — procedures now exist as real syntax, and the VM has everything needed underneath. Here's what's actually left, roughly by size:

**The natural next increment: parameters.** This is the most impactful thing missing right now — a parameterless procedure can only work through global state, which is a real limitation on what you can actually write. The good news: the hard part (a VM that can hold per-call local storage) is already built and proven — `ENTER`/`LOAD_LOCAL`/`STORE_LOCAL` were tested with hand-written `.sasm` two sessions ago (the recursive `sum(n)` example). What's needed now is entirely compiler-side:
- Parse `procedure name(a, b: integer); ...` — parameter names and types in the declaration.
- Assign each parameter a local slot number (0, 1, 2...) during parsing, scoped to that one procedure.
- At each call site, push argument expressions in order before `CALL` (already how the calling convention was designed).
- Emit `ENTER n` + `STORE_LOCAL` for each parameter at the top of the procedure body.
- Type-check argument count and types against the declaration.
- Inside the body, references to parameter names need to resolve to `LOAD_LOCAL`/`STORE_LOCAL` instead of the global `LOAD`/`STORE` — this is the first time the compiler needs any notion of "which name means what depends on which scope I'm currently generating code for."

**Naturally bundled with parameters: procedure-local variables** (`var` declared inside a procedure body). Same mechanism — another slot in the same frame — so it's a small addition once parameter slot-tracking exists, not a separate design problem.

**After that: functions** (procedures that return a value). Also mostly free at this point — the "leave the result on the operand stack before `RET`" convention was already validated in the VM-level `sum(n)` test. Mainly needs: `function name(...): type;` syntax, treating a call as an *expression* (not just a statement) when it's a function, and type-checking the return type at each call site.

**Smaller/independent items still sitting on the list, unrelated to procedures:** `forward` declarations (unlocks mutual recursion), multi-dimensional arrays, `real`/floating-point.

**My recommendation:** parameters next, local variables right alongside them (same frame mechanism, same session makes sense), functions as a following increment once that's solid. Want to start there — designing the parameter-passing syntax and how slot-scoping works — or would you rather tackle something else off the list first?

# Prompt

Go for parameters and local variables

# Response

This is the big one — genuinely the most architecturally significant piece since the frame stack itself. Let me design the scoping carefully, since that's the new problem: the parser now needs to know "am I currently inside a procedure, and if so, does this identifier refer to a parameter/local before falling back to the global table?"

**Key design decisions, upfront:**
- Parameters and procedure-locals share one mechanism — they're both just numbered slots in the same frame (parameters get slots `0..k-1`, additional locals continue after).
- **Scalar types only this increment** (`integer`/`boolean`/`string`/`char`) — no array parameters or array locals. Keeping consistent with how every increment so far has drawn an honest line.
- **`readln` into a parameter/local, and `for`-loops over one, are explicitly rejected** with a clear error for now — both are real but separable extensions, not required to make parameters genuinely useful.
- I'll reuse the same "separate node type instead of a flag" pattern that's worked well before (`NODE_STRING` vs `NODE_VARIABLE`) — `NODE_LOCAL_VAR`/`NODE_LOCAL_ASSIGN` alongside the existing `NODE_VARIABLE`/`NODE_ASSIGN`. This turns out to matter for a subtle reason I'll get to: it keeps local-slot indices from ever accidentally colliding with global `sym_table` indices in the dead-code eliminator.
- **Ordering catch worth flagging now**: a procedure's parameter info needs to be written into `proc_table` *before* its body is parsed, not after — otherwise a recursive self-call from inside the body would see stale/zeroed parameter info when the parser checks argument count. Small thing, but exactly the kind of ordering bug this project has hit before.

Let's build it. Starting with `common.h`:Now extend `ProcSymbol` with parameter info:Now `parser.c` — the biggest piece. Let's view the current state:Let's implement. First, the local-scope tracking state and helpers:Now `find_local`/`add_local`, right after the procedure-name helpers:Now `parse_scalar_type()` and a shared `NameGroup` parser for the `name {, name} : type` pattern (used identically by parameter lists and local `var` sections):Now reset the local-scope state in `parse_ast()`, and rewrite `procedure_declaration()`:Now the big rewrite of `procedure_declaration()`:Now `factor()`'s identifier branch — check `find_local` first:Now `statement()`'s identifier branch — the full disambiguation: local assignment, then procedure call (now with real argument parsing), then global assignment:Now the `readln` and `for` restrictions — both need to explicitly reject a local/parameter with a clear error rather than silently doing the wrong thing:Now the `for`-loop variable:`parser.c` is done. Now `type_checker.c` — `NODE_LOCAL_ASSIGN` type checking and `NODE_CALL` argument type checking:Now `ast_printer.c` — add `NODE_LOCAL_VAR`/`NODE_LOCAL_ASSIGN`, and update `NODE_CALL`'s printing to show arguments (which now exist):Now `codegen.c` — the core mechanism: `NODE_LOCAL_VAR`/`NODE_LOCAL_ASSIGN`, argument-pushing in `NODE_CALL`, and the `ENTER`/parameter-unpacking prologue in `generate_program()`:Now the procedure prologue in `generate_program()` — `ENTER` plus unpacking parameters off the operand stack in reverse (last-pushed argument first):`optimizer.c` doesn't need any changes — `NODE_LOCAL_VAR`/`NODE_LOCAL_ASSIGN` are distinct node types from `NODE_VARIABLE`/`NODE_ASSIGN`, so they naturally fall through the generic recursion without ever touching `var_used_tracker` (which is sized and indexed for *global* `sym_table` indices only — exactly the collision I was trying to avoid by using separate node types). Let's build:All clean. Now the tests. First, basic parameters and locals:That's the deliberate restriction working correctly — I used a local variable as a `for`-loop counter in my own test. Let me fix it (using `while` instead, since that has no such restriction):All correct — multiple parameter types, a local variable, and a `while` loop using it all work. Now the critical test — recursive `sum(n)` compiled from real Pascal syntax, proving true per-call frame isolation:Correct — `15 = 1+2+3+4+5`, and by the same reasoning as the VM-level test, this is only correct if each recursive call's local `n` genuinely survives its own nested call untouched. Now the error cases:All four error paths correctly caught. Now let's verify shadowing (a local hiding a global of the same name — standard Pascal behavior):Perfect shadowing semantics — the local `x` is used and modified inside `test`, while the global `x` remains completely untouched (`5` before and after the call). Let's check the verbose AST dump, then round-trip and full regression:Correctly shows `[Local] slot 0` for the parameter reference, `[Local Assignment] -> slot 0` for the modification, and `[Call] -> Procedure: test` correctly shows `Arg 1: [Number] 42`. Now round-trip and full regression:Exactly the designed pattern: `enter 2` (parameter + local = 2 slots), `store_local 0` unpacking the argument, the recursive `call L1` self-referencing correctly. Byte-identical round trip. Now the full regression suite:All regressions pass. Let's update the docs since procedures now support parameters and locals — starting with `LANGUAGE.md`:Let me verify that exact example from the docs actually works as written:Matches exactly. Now update the "not implemented" list and `README.md`'s status:Now `README.md`:Final rebuild and sanity check, then package and deliver everything:## Summary of this session's work — parameters and local variables for procedures

Everything is implemented, built cleanly, and thoroughly tested. I ran out of tool budget right at the final packaging step, so let me summarize what's done and what's left.

### What's working (all verified)

- **`procedure name(a, b: integer; c: boolean); var x: integer; begin ... end;`** — parameters and local variables, both scalar-only (`integer`/`boolean`/`string`/`char`), sharing one frame mechanism (parameters get slots `0..k-1`, locals continue after).
- **By-value parameter passing** — caller pushes arguments, callee unpacks them via `STORE_LOCAL` right after `ENTER`.
- **The critical test**: recursive `sumTo(n)` compiled from real Pascal syntax computes `15 = 1+2+3+4+5` correctly — only possible if each recursive call's `n` and `partial` are genuinely isolated per-call, not shared.
- **Shadowing verified explicitly**: a parameter named `x` correctly shadows a global `x` inside the procedure, and modifying the local leaves the global completely untouched (`5` before and after the call) — standard Pascal lexical scoping, working correctly.
- **All four deliberate error paths**: argument count mismatch, argument type mismatch, array parameters rejected with a clear message, `readln` into a local rejected with a clear message.
- **Disassembly and round-trip** confirmed byte-identical, showing exactly the designed `enter 2` / `store_local 0` / recursive `call L1` pattern.
- **Full regression suite** — every earlier feature (control flow, strings, arrays, `break`/`continue`, error recovery, prior parameterless-procedure tests, prior VM-level frame tests) still passes.
- **Docs updated**: `docs/LANGUAGE.md`'s Procedures section fully rewritten with the parameter/local syntax, the shadowing rule, and current restrictions; the documented example was independently verified to actually run and produce the claimed output; `README.md`'s status line updated.

### Key design decisions made along the way

- Used **separate AST node types** (`NODE_LOCAL_VAR`/`NODE_LOCAL_ASSIGN`) rather than a flag on the existing `NODE_VARIABLE`/`NODE_ASSIGN` — this was deliberate and turned out to matter: it means local slot indices can *never* accidentally collide with global `sym_table` indices anywhere a generic tree-walk (like dead-code elimination's usage tracking) checks node type.
- **Caught an ordering bug before it shipped**: a procedure's parameter count/types must be written into `proc_table` immediately after parsing the parameter list — *before* the body is parsed — otherwise a recursive self-call from inside that body would see stale/zero parameter info.
- **Deliberately scoped out** for this increment (each with a clear compile error, not silent misbehavior): array parameters/locals, `readln` into a local, and using a local as a `for`-loop counter.

### Left to do (not started this session)

Two smaller, real gaps remain toward "full" procedures: **`forward` declarations** (unlocking mutual recursion) and **functions** (procedures that return a value — the calling convention for this was already proven at the VM level with the `sum(n)` `.sasm` example from two sessions ago, so this is mostly compiler-side wiring, similar in shape to what was just built for parameters).

# tool limit

Clean build confirmed. Let's package and deliver all the files changed this session:All files delivered. To recap where this leaves the project:

**Procedures with parameters and local variables are complete and fully verified** — by-value scalar parameters, local `var` sections sharing the same frame mechanism, correct per-call isolation under recursion (the `sumTo(5) = 15` proof), correct shadowing of outer names, all four deliberate error paths, byte-identical round-trip through `desole`/`solas`, and a full regression pass across everything built in this project so far.

Two things worth remembering for next time:

1. **The design decision that mattered most**: giving locals their own AST node types (`NODE_LOCAL_VAR`/`NODE_LOCAL_ASSIGN`) rather than reusing `NODE_VARIABLE`/`NODE_ASSIGN` with a flag — this is what kept local slot numbers from ever being able to collide with global `sym_table` indices in the dead-code eliminator, for free.
2. **The ordering bug caught before shipping**: a procedure's parameter info must be written into `proc_table` right after the parameter list is parsed, not at the end — otherwise a recursive self-call from inside the body would see stale data.

**What's left toward "full" procedures**, in rough order I'd tackle them:
- **`forward` declarations** — smaller, unlocks mutual recursion (A calls B, B calls A), mostly a parser change since the codegen/frame machinery already exists.
- **Functions** (procedures that return a value) — the calling convention was already proven at the VM level (leave the result on the operand stack before `RET`), so this is mostly `function name(...): type;` syntax, treating a call as an expression when it's a function, and type-checking the return type at call sites.
- **Array parameters/locals** — the least urgent of the three, and the most architecturally involved (arrays currently live in one flat `vm_array_mem` region addressed by compile-time-known base offsets; a parameter would need either pass-by-reference or a copy-in/copy-out convention).

Let me know which you'd like to pick up whenever you're ready to continue.

