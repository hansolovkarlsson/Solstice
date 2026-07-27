008_arrays.md

# Prompt

What's next?

# Response

Nice checkpoint to pause at. Here's the landscape as it actually stands now:

**Small, bounded items still on the list**
- String ordering comparisons (`<`, `>`, `<=`, `>=`) — deferred when we did strings, since `=`/`<>` covered the common case and ordering needs a bit more design (lexicographic via `strcmp`, straightforward but untouched so far).
- `//` single-line comments, alongside the existing `{ }` style — trivial lexer addition, quality-of-life.

**The two big remaining gaps**

1. **Arrays.** There's currently no way to store a list of values — no `arr: array[1..10] of integer`. This is arguably the more *immediately* limiting gap of the two: you can't write a sort, a search, a histogram, anything that needs indexed storage, without it. Technically it's a contained change — a new memory region indexed at runtime (`LOAD_IDX`/`STORE_IDX`-style opcodes with bounds checking, matching the paranoid style we've kept everywhere else), plus array-type declarations and indexing syntax in the parser. No call stack needed.

2. **Procedures and functions.** Still the most architecturally significant piece — every program is one flat block, no reusable subroutines. This needs an actual call stack in SolVM (`CALL`/`RET`, stack frames, parameters, locals scoped to a call), which is a step up in complexity from anything built so far. It's also the prerequisite for OOP down the line, per your original vision — methods are just procedures with an implicit `self`.

**My honest take:** I'd do **arrays next, procedures/functions after**. Reasoning: arrays are self-contained (they don't ripple into the call-stack work at all), they unlock a huge class of programs on their own, and they're a good "medium" step before taking on something call-stack-sized. Procedures still needs to happen — it's just a bigger lift, and doing arrays first doesn't cost you anything toward it later.

That said, if you'd rather tackle the big one now while it's fresh in your head from planning it out last time, I'm ready for that too. Which sounds right?

# Prompt

Ok. Let's do arrays.

# Response

This is another substantial one — it needs a new memory region in the VM (arrays don't fit in the existing scalar `vm_vars[]`), new syntax (`array[1..10] of integer`, indexing `arr[i]`), and — this is the part I need to be careful about — a real memory-safety concern: array bounds must be checked at *runtime* the same rigorous way everything else in this VM is, since an off-by-one here would silently corrupt a neighboring array's data in shared memory.

**Design:**
- One flat `vm_array_mem[]` region shared by every array in a program. Each array variable gets a compile-time-assigned base offset into it (`sym_table` grows to carry `is_array`/`lower`/`upper`/`base` — this is a whole-struct `fwrite`/`fread` already, so the bytecode format picks it up for free).
- Array bounds must be compile-time integer literals (`array[1..10] of integer`) — no dynamic sizing, keeping this consistent with the rest of the language.
- Two new opcodes: `OP_LOAD_IDX`/`OP_STORE_IDX`, both bounds-checked against the declared range at runtime.
- `arr[i] := expr` reuses the existing `NODE_ASSIGN` node rather than adding a new type — index goes in the (previously scalar-only) `left`, and I add a `right` for the value, distinguished by checking `is_array` on the symbol at codegen/type-check time. I caught one real bug while designing this: the dead-code eliminator's "remove this assignment" path only frees `node->left` today — for an array assignment it would leak `node->right` (the value expression). Fixing that as part of this change.

Let's start:Let's make all the `common.h` additions:Now add the `array_mem_count` global:Now `bytecode.c` — define `array_mem_count`:Now `lexer.c`:Now the `.`/`..` distinction and `[`/`]`:Now `parser.c` — the biggest piece:Now the `parse_int_literal` helper, right before `match`:Now update `parse_ast()`'s `var` declaration parsing to handle array types, and reset `array_mem_count`:Now `factor()` — array-read expressions:Let me fix a small line-number regression I introduced — the node should capture the identifier's own line, not whatever token follows it:Now `statement()`'s `TOKEN_IDENTIFIER` branch — array-element assignment:Now `type_checker.c`:Now add the `NODE_ARRAY_ACCESS` case:Now `optimizer.c` — the two fixes: marking array accesses as "used", and the memory-leak fix in dead-code removal:Now the memory-leak fix — `node->right` (an array assignment's value expression) wasn't being freed when the assignment gets removed as dead code:Now `ast_printer.c`:Now add `NODE_ARRAY_ACCESS`:Now `codegen.c`:I noticed `add_temp_var` has the same stale-array-metadata bug I just fixed in `add_var` — let me fix that too:Now the array codegen — `NODE_ASSIGN` branching, and `NODE_ARRAY_ACCESS`:Now `vm.c` — the array memory region and the two new opcodes:Add the array memory region:Now zero `vm_array_mem` at start of `run_vm()`, and implement the two opcodes:Let's also extend the final variable dump to show array contents (still gated behind `-v`), since arrays don't have a meaningful `vm_vars[i]` entry at all:Let's build the core compiler and VM now, before tackling `solas`/`desole`:Clean build, zero warnings — confirms exhaustive switch coverage for `NODE_ARRAY_ACCESS`. Now real tests:AST looks correct. Now let's actually run it:All correct — squares 1,4,9,16,25, sum=55, boolean array, and string array all working. Let's check the verbose final-state dump for arrays:Array dump formatting is correct. Now the error handling — the critical part given the memory-safety design:The negative-bounds and compile-time error tests all look correct, but the runtime out-of-bounds test shows `exit=0` with no error — that's wrong, let me investigate:That's not a bug — the optimizer correctly eliminated the dead assignment, since `arr` is written but never read anywhere in that program (exactly the DCE behavior we want). Let me fix the test to actually use the array afterward:Both correctly caught — bounds checking works on both read and write. Let's verify two arrays don't corrupt each other's memory, and check for leaks with valgrind if available:`valgrind` isn't available in this sandbox, but the two-array test confirms base offsets are correctly non-overlapping. Let's specifically exercise the DCE memory-leak fix (dead array assignment):Let's also verify the established "surface compile errors even in dead code" behavior still works for array assignments — e.g., a constant division-by-zero in a dead array-assignment's value expression:Correctly caught — the constant division-by-zero in the dead array assignment's value expression still surfaces as a compile error, confirming the `optimize_ast`-before-free fix in the DCE removal path works correctly for arrays too, not just scalars.

## Where things stand

Arrays are implemented and thoroughly verified for the core compiler and VM (`pascalc` + `solvm`):

**Working and tested:**
- `arr: array[lower..upper] of integer/boolean/string;` declarations, including negative bounds
- Reading and writing (`arr[i]`, `arr[i] := expr`)
- Runtime bounds checking on **both** read (`OP_LOAD_IDX`) and write (`OP_STORE_IDX`) — verified out-of-range access is rejected cleanly with a clear error, not a crash
- Multiple arrays coexist correctly in the shared `vm_array_mem[]` region with no cross-contamination (verified with two adjacent arrays)
- Compile-time errors: invalid bounds (`upper < lower`), indexing a non-array, a bare array reference with no index, non-integer index
- Two real bugs I caught and fixed while building this:
  1. **Memory leak** in the dead-code eliminator — removing a dead array assignment only freed the index expression, not the value expression. Fixed and verified (both the no-crash case and the "still surfaces compile errors like div-by-zero in dead code" case).
  2. **Stale metadata leak** — `add_var()` and `add_temp_var()` didn't zero the new array fields on a `Symbol`, so a previous compile's array metadata could leak into a new compile reusing the same `sym_table` slot (relevant given the recoverable/reusable-process work from earlier). Fixed in both places.
- Dead-code elimination correctly treats an array as "unused" only when nothing reads any element of it, and correctly marks it used via `NODE_ARRAY_ACCESS`.
- Clean build, zero warnings, across all changes.

**Not yet done — this is where I'd continue next:**
- `solas`/`desole` (the assembler/disassembler) haven't been touched yet for array support (`.array` directive, `LOAD_IDX`/`STORE_IDX` mnemonics, dumping array contents in the disassembly listing). Skipping these means the toolchain is temporarily inconsistent — a program using arrays can't currently be round-tripped through `desole`/`solas` the way everything else in this project has been.
- Full regression suite (the whole battery of earlier tests — strings, `for`, control flow, error recovery, `-v` flag) hasn't been re-run since these changes landed.
- Haven't presented the changed files to you yet.

Want me to pick up right where this left off — `solas`/`desole` array support, then the full regression pass and file delivery?

# Tool limit

