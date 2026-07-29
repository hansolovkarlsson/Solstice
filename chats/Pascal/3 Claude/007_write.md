007_write.md

# Prompt

One thing that needs to be fixed is writeln. In Pascal there are two versions, write and writeln, also they can take multiple arguments, writeln('val:', number); would print "val:" and then the variable without newline beetween then, and then it ends with newline after the number.

# Response

Good catch — this is a real, useful fix, and it touches the print opcodes themselves, not just the statement parsing. Let me design it carefully:

**Multiple arguments**: I'll represent `write`/`writeln`'s argument list as a linked list of expression nodes chained via each argument's own (otherwise-unused) `next` pointer — the same technique already used for statement lists, so no struct changes needed.

**`write` vs `writeln`**: same AST node type, distinguished by reusing the existing `op` field (`TOKEN_WRITE` vs `TOKEN_WRITELN`) — again no struct changes.

**The real semantic fix**: right now `OP_PRINT`/`OP_PRINT_STR` each hard-code a trailing `\n`. That's exactly wrong for `writeln('val:', number)` — printing two arguments back-to-back would currently get *two* newlines instead of zero-then-one. So I need to strip the automatic newline from both opcodes and add a separate `OP_NEWLINE` opcode that codegen emits explicitly, once, only for `writeln` (never for `write`). This is a real behavior change to the print opcodes, not just new syntax on top.

Let's start:Now `lexer.c`:Now `parser.c` — the multi-argument, optional-parens `write`/`writeln` parsing:Now replace the single-argument `writeln`-only parsing with the combined `write`/`writeln`, multi-argument, optional-parens version:Now `type_checker.c` — iterate the argument chain instead of checking only the first argument:Now `ast_printer.c`:Now `codegen.c` — the core fix:Now `vm.c`:Now `solas.c`/`desole.c` — add `OP_NEWLINE`:Let's update `solas.c`'s doc comment to reflect the changed opcode semantics:Now let's build:Clean build. Now the exact test the user described, plus edge cases:Exactly matching the intended semantics. Let me verify the "no trailing newline" claim precisely with a byte-level check, since `--- Final Runtime...` also starts with its own `\n`:Confirmed — no blank line between them, so the single newline present comes entirely from `OP_HALT`'s own diagnostic header, not from `write`. Now the round trip and full regression suite:Round trip is byte-identical, and every existing test still produces the same correct output as before. Let's quickly confirm the empty-parens form too, then finalize:`before\nafter` — `writeln()` correctly printed just a newline, `write('before')`/`write('after')` correctly had none of their own. All working as intended.## Summary

- **`write` added** alongside `writeln` — same statement, `node->op` distinguishes them.
- **Multiple comma-separated arguments** for both, of any mixed type — represented as a linked list via each argument's own `next` pointer (no struct changes needed), printed back-to-back with no separator.
- **The real fix**: `OP_PRINT`/`OP_PRINT_STR` no longer hard-code a trailing newline. A new `OP_NEWLINE` opcode handles that, emitted by codegen exactly once, only for `writeln`, after all its arguments — never for `write`, never per-argument.
- Bonus: both now support the parenthesis-free/empty forms (`writeln;`, `writeln()`), matching real Pascal.
- `solas`/`desole` updated for the new opcode; round-tripped byte-identical.

Verified your exact example — `writeln('val:', number)` now prints `val:5` with no gap and one trailing newline — plus mixed-type args, back-to-back `write` calls, bare `writeln;`, and a byte-level check confirming `write` truly leaves no trailing newline. Full regression suite still passes.

# Prompt

Change the programs so they don't print the debug text (ast tree and such) unless the program has a -v (for verbose) argument on the command line.

# Response

Good idea — and it needs to apply consistently across all four CLI tools, not just `pascalc`. There's also a wrinkle: `optimizer.c` has its own `printf`s (`[Optimization] Folded constants...`, `[DCE Optimization] Removing...`) that fire during compilation regardless of what `pascalc.c` itself prints — those need to respect the flag too, which means I need a shared "verbose" flag any module can check, not just something local to `main()`.

Let's add it alongside the existing `fatal_error_active` flag, since it's the same kind of small cross-cutting runtime state:Now `pascalc.c` — parse `-v`, gate the phase narration and AST dump:Now `optimizer.c` — gate its internal progress printfs, which fire regardless of what `pascalc.c` itself prints:Good, already included. Now `solvm.c` and `vm.c` — the VM's own step banners, plus the final variable-state dump (which is diagnostic, not the program's actual `writeln`/`write` output):Now `vm.c` — gate the final variable-state dump at `OP_HALT`:Now `solas.c` and `desole.c`, for consistency across all four CLI tools:Good, `string.h` already included. Now `desole.c`:Good, already included. Now let's build everything:Let's test each tool:Exactly right — silent on success without `-v`, full narration with it, and `solvm` now shows *only* the actual program output by default. Let's confirm `-v` restores full output and that errors always show regardless:Errors are correctly unaffected by the flag (they always print). Now `solas` and `desole`:All four tools behave correctly. Now the full regression suite to confirm functionality is unaffected — only verbosity changed:All correct — the `readln` "failure" was the expected rejection of invalid boolean input (`7`), not a regression; everything else confirms functional behavior is unchanged, just quieter by default. Let's finalize:## Summary

Added a shared `verbose_mode` flag (`error.h`/`error.c`, alongside the existing `fatal_error_active`) that any module can check — needed because `optimizer.c` prints its own progress narration (`[Optimization] Folded constants...`, `[DCE Optimization] Removing...`) independently of what `pascalc.c` prints, so gating had to happen at the source, not just in `main()`.

All four CLI tools now take an optional `-v` flag (can appear anywhere among the arguments):

| Tool | Silent by default | Shown only with `-v` |
|---|---|---|
| `pascalc` | everything on success | phase banners, AST tree, "Successfully written..." |
| `solvm` | the program's actual `write`/`writeln` output | step banners, final variable-state dump |
| `solas` | everything on success | "Successfully written..." confirmation |
| `desole` | the disassembly listing itself (that's its real output) | "Wrote ..." confirmation (only relevant when writing to a file) |

Errors are **never** gated — they print to `stderr` exactly as before, `-v` or not, verified explicitly.

One judgment call worth flagging: I also gated `solvm`'s "Final Runtime Execution Output Results" variable dump behind `-v`, since it's diagnostic info about internal VM state, not something a real Pascal program actually prints — the same category as the AST tree you mentioned. Your program's own `write`/`writeln` output is completely unaffected either way. Let me know if you'd rather that dump stay on by default.