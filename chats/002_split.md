# 002_split.md

# Prompt

Starting to look great. Which way would be best to move forward right now? Create the assembler and separate the VM from the current code, or continue building the Pascal compiler?

# Response

Good moment to think about this, because there's a technical reason to pick one over the other, not just a preference.

**Recommendation: separate out the VM and build the assembler first.**

Here's the reasoning. Look at what's actually missing from the compiler right now — it's not more expressions or types, it's **control flow**. There's no `if`, no `while`, no `for`. And control flow needs conditional/unconditional jump instructions, which don't exist yet anywhere: `common.h`'s `Opcode` enum has no `OP_JMP` / `OP_JZ` equivalent, and the VM's dispatch loop has no branching at all — `ip` only ever increments by one instruction per cycle.

So both paths you're weighing actually share the same first prerequisite: **the VM needs jump opcodes before either the assembler or the compiler can do anything interesting with control flow.** Given that, building the assembler next is the lower-risk way to get there:

1. **Add jump opcodes to the VM** (`OP_JMP`, `OP_JZ` at minimum) — small, contained change to `vm.c` and the `Opcode` enum.
2. **Build a minimal assembler + disassembler** that can hand-write and inspect bytecode using those opcodes directly.
3. **Validate loops and branches by hand** in raw bytecode — write a small counting loop as an assembly program, run it, disassemble the compiler's own output and eyeball it. This is exactly the "have something you can test against immediately" value you get from building the VM/assembler layer first, and it's the same reasoning you had originally for wanting this tool built before the OOP work.
4. **Only then** extend the parser/codegen to emit `if`/`while`/`for` — and at that point you can cross-check the compiler's generated jumps against the disassembler output, instead of debugging new codegen and new VM behavior at the same time.

There's also a structural reason to do the VM separation now rather than later: right now `code[]`, `code_idx`, `sym_table[]`, and `sym_count` are declared as globals in `parser.c`, even though the VM only cares about them at runtime via `bytecode.c`. That's a leftover from when everything was one binary. Pulling the VM into its own SolVM binary is a good forcing function to move that runtime state to where it actually belongs (`bytecode.c`), so SolVM can genuinely stand alone and run any `.bin` file without linking against the compiler frontend at all.

If you agree, I'd start with step 1 (jump opcodes in the VM) since everything else depends on it. Want me to go ahead with that?

# Prompt

Agree. First step though is to separate Pascal compiler and SolVM into separate binaries.

# Response

Agreed — that's the right first move. Let's do it properly: move the shared runtime state to where it belongs, split `main.c` into two dedicated entry points, and update the Makefile to build two independent binaries that don't need to link against each other's code at all.

First, let's confirm exactly where `code[]`/`sym_table[]` are currently defined:Confirmed — they're defined in `parser.c` even though `bytecode.c` (save/load) and `vm.c` (execution) are the modules that actually own their lifecycle. Moving them:Good — `common.h`'s `extern` declarations for these stay unchanged; only the actual definitions moved. Now split `main.c` into two dedicated entry points. First the compiler-only one:Now remove the old combined `main.c` and update the Makefile for a proper three-way split (compiler frontend / VM / shared bytecode+error):Clean build, and notice `pascalc` links 9 objects (no `vm.o`) while `pascalvm` links only 4 objects (`pascalvm.o`, `vm.o`, `bytecode.o`, `error.o` — no lexer/parser/type_checker/optimizer/codegen at all). That's exactly the separation we wanted. Let's confirm both work correctly with the new CLI syntax, and re-run the full regression suite:Both binaries work correctly end-to-end. Now let's re-run the whole error-handling and bounds-check regression suite to make sure nothing regressed in the split:Everything checks out — all bounds-checking, error recovery, and validation from every previous fix still works identically after the split.## Summary

- **`code[]`/`code_idx`/`sym_table[]`/`sym_count`** moved from `parser.c` to `bytecode.c` — the module both binaries actually depend on, instead of the VM having to pull in parser internals just to get these declarations satisfied.
- **`main.c` → `pascalc.c` + `pascalvm.c`**: the old `-c`/`-r` flag dispatch is gone. Each binary now does one job with a simpler CLI:
  - `pascalc <source.pas> <output.bin>`
  - `pascalvm <input.bin>`
- **Makefile** rebuilt around three object groups: `SHARED_SRCS` (`bytecode.c`, `error.c` — needed by both), `FRONTEND_SRCS` (lexer through AST printer — compiler only), `VM_SRCS` (`vm.c` — VM only). `pascalvm` now links exactly 4 object files and has zero dependency on the compiler frontend.
- One naming call I made: I named the VM binary `pascalvm` for now rather than jumping straight to `solvm`, since you mentioned that name isn't fully locked in yet. Renaming it later is a one-line change in the Makefile (`PASCALVM := solvm`) plus updating `pascalvm.c`'s usage string — trivial whenever you're ready to commit to it.

Everything else — the recoverable-error handling, all the bounds checks, the `readln` type enforcement — carried through the split unchanged and re-verified.

Ready for the jump-opcode work whenever you want to move on to that.