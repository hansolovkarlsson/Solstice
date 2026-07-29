# 005_string.md

# Prompt

Something that would be really helpful now is string datatype

# Comment

In the future I should order the changes on the assembler and VM first, and not include the pascal compiler until the VM is done.

# Response

This is a genuinely big feature — it touches the file format, every compiler pass, the VM, and (to keep the toolchain consistent) the assembler and disassembler too. Let me scope it clearly before diving in:

**What I'll build:** string literals (`'hello'`), string variables (`s: string;`), assignment, `writeln(s)`, and equality comparison (`=`/`<>`).

**What I'm deliberately deferring** (each adds real complexity around *runtime*-created strings, versus what I'm building which is all compile-time-known): concatenation (`+`), `readln` into a string, and lexicographic ordering (`<`/`>`). I'll flag these clearly at the end as natural next steps rather than silently doing a partial job.

**Design:** the VM's stack and variable slots are plain `int`s — no room for arbitrary string data. So strings work like a constant pool: every literal gets interned once into a `string_pool[]` (part of the bytecode file, like the symbol table already is), and a "string value" is just an integer index into that pool, flowing through the existing `LOAD`/`STORE` machinery unchanged. Only `PUSH`, `PRINT`, and `EQ`/`NEQ` need string-aware variants, since those are the only places code needs to know it's holding a string rather than a raw number.

Let's start with `common.h`:Now the three new opcodes:Now `NODE_STRING` and the string pool globals:Now `bytecode.c` — define the pool globals and extend save/load:Now the load side, with the same validated-read pattern used for `sym_count`/`code_idx`:Now `lexer.c` — string literal scanning and the `string` type keyword:Now `parser.c` — string interning, the `factor()` literal branch, the `.var`-equivalent type keyword, and resetting `string_count`:Now let's add the string interning helper and wire it in:Now the `factor()` branch for string literals:Now the `var` declaration type keyword, and reset `string_count` at the start of a compile:Now `type_checker.c`:Now the `readln`-into-a-string rejection (since we're not implementing that yet, it must fail loudly rather than corrupt silently):Now `ast_printer.c`:Now `codegen.c` — the core codegen logic:Now `EQ`/`NEQ` need to branch on the operand type, and `WRITELN` needs to choose `OP_PRINT` vs `OP_PRINT_STR`:Now `vm.c`:Now add the three opcode cases:Now fix the final-results dump at `OP_HALT`, which currently prints every variable as `%d` — for a string variable, that would show the raw pool index instead of the actual string:Let's build now to catch any issues in the core compiler+VM before touching the assembler/disassembler:Clean build. Now a real test with actual Pascal source:All correct. Now the error-handling and edge cases:Let me check the actual error messages in full (buffering may have reordered stdout/stderr in the piped output):All four error messages are exactly right and correctly line-numbered. Now let's stress-test the bounds checking (too-long string, too many strings) and then a corrupted-bytecode robustness check on the string pool:All three robustness cases handled correctly, matching the established defensive pattern exactly. Now let's update `solas` and `desole` for toolchain consistency — this is the part where I need to handle a quoted-string operand, which the current whitespace-splitting parser doesn't support:Now the string interning helper and the quoted-string extractor:Now the `.var` type keyword, `string_count` reset, and the `OPERAND_STRING` case in pass 2:Now `desole.c`:Now handle `OP_PUSH_STR`'s operand specially — print it as a quoted string, matching what `solas` expects back:Here's where things stand — I got the `desole` (disassembler) side of the string support fully wired up but ran out of tool budget before I could build and verify everything end-to-end. Summary of the full session's work:

**Completed and previously verified (core compiler + VM):**
- `common.h`: `MAX_STRING_LEN`/`MAX_STRINGS` constants, `TOKEN_STRING`/`TOKEN_STRING_TYPE`, `TYPE_STRING`, `NODE_STRING`, the three new opcodes (`OP_PUSH_STR`, `OP_PRINT_STR`, `OP_SEQ`), the `string_pool[]`/`string_count` globals, and a `string_value` buffer on `Token`.
- `bytecode.c`: string pool defined here (alongside `code[]`/`sym_table[]`), and `save_bytecode`/`load_bytecode` extended to serialize it with the same validated-read pattern as everything else.
- `lexer.c`: string literal scanning (`'...'` with `''` as an escaped quote), plus the `string` type keyword.
- `parser.c`: `intern_string()` with dedup, string literals in `factor()`, `string` in `var` declarations, `string_count` reset per compile.
- `type_checker.c`: allows `=`/`<>` on strings (rejects ordering comparisons), rejects `readln` into a string var with a clear error.
- `ast_printer.c`, `codegen.c`, `vm.c`: all updated and this half was **fully built and tested** — literals, variables, assignment, `writeln`, equality, and every edge case (long/unterminated literals, corrupted bytecode, out-of-range pool indices, type errors) all verified working correctly.

**Just finished, not yet verified:**
- `solas.c`: added `OPERAND_STRING`, the three new mnemonics, quoted-string operand parsing (`PUSH_STR "text"`), `.var` string type, `string_count` reset.
- `desole.c`: added the three opcode names, string type name, and quoted-string operand printing for `OP_PUSH_STR` (round-trippable back through `solas`).

**Next step:** rebuild the full project (`make clean && make`) to confirm `solas.c`/`desole.c` compile cleanly, then re-run the round-trip test (compile a string-using `.pas` program → disassemble with `desole` → reassemble with `solas` → confirm identical VM output) the same way we validated the jump-opcode work earlier. I haven't done that pass yet — please treat the assembler/disassembler string support as unverified until that happens. Want me to pick that up now?

# Tool limit

Clean build, zero warnings. Now let's disassemble the string test program compiled earlier and check what `desole` produces:That's a clean, correct disassembly — `.var` types, string literals, `SEQ`/`NOT` for `<>`, and the `if`/`else` jump structure all look right. Now the full round-trip test:Round trip verified. Let's also test writing `.sasm` by hand directly (the actual ergonomic use case) and a couple of edge cases:Let me check the embedded-quote case without truncation — I may have cut off the actual printed output:Confirmed — even the tricky embedded-quote case works exactly as predicted (first-to-last quote extraction handles it correctly without needing escape sequences). Now a final full regression pass across everything built this whole project, to make sure nothing broke:Everything checks out — `control_flow_test.pas` output matches exactly what we verified in the previous session, all bounds-checking and error-recovery regressions still pass. Let's finalize the deliverables:## String datatype — complete

The whole toolchain is now string-aware and verified end to end:

- **`pascalc`**: `'literals'`, `s: string;` declarations, assignment, `writeln(s)`, `=`/`<>` comparison, with correct type errors for everything unsupported (ordering comparisons, `readln` into a string, wrong-type assignment).
- **`solvm`**: the string pool travels through the `.bin` format; `OP_PUSH_STR`/`OP_PRINT_STR`/`OP_SEQ` all bounds-checked against malformed bytecode the same way every other opcode is.
- **`solas`**: `PUSH_STR "text"` with direct interning, `.var s string`.
- **`desole`**: prints string operands back as quoted literals — confirmed **byte-identical VM output** on a full round trip (`strtest.bin → desole → solas → new .bin`), including a hand-check of the trickier `push_str "she said "hi" to me"` embedded-quote case, which works correctly without needing escape sequences.
- Full regression suite (bounds checks, `if`/`while`/`repeat`, jump backpatching, error recovery, `readln` boolean domain) — all still pass, nothing broke.

Deferred, as scoped up front: string concatenation (`+`), `readln` into a string, and lexicographic ordering (`<`/`>`/`<=`/`>=`). All three are natural next steps whenever you want them — concatenation and string `readln` both need the string pool to become a *runtime*-mutable structure (right now it's only populated at compile/assemble time), which is a meaningfully different problem from what we built today.

