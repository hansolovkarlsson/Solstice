021_char_ext.md

# Prompt

close the gap of char (`ord`/`chr`, dedicated literal syntax)

# Response

Good, well-scoped gap to close. Let me think through what "dedicated char literal syntax" should concretely mean here, since real Pascal char literals normally just reuse string-literal syntax (`'A'`) — which this compiler already supports for char via type coercion.

**What I'll build:**
- **`ord`/`chr`** — new keyword-based built-ins (like `abs`/`sqr` from last session), reusing `NODE_UNARY_OP`. `ord(c)` returns a char's ordinal value as `integer`; `chr(n)` returns the character with that ordinal value.
- **`#NNN` numeric char-code literals** — this is real, standard Turbo Pascal/Delphi/Free Pascal syntax (`#13` for carriage return, etc.), and it's genuinely new syntax rather than reusing string-literal quoting — which is what makes it a real "dedicated char literal," distinct from `'A'`. `#65` will parse directly to `TYPE_CHAR` (not `TYPE_STRING`), unlike ordinary string literals.

**One representational limit I need to handle carefully**: this compiler's strings are null-terminated C strings, which can't hold a `\0` byte — so character code `0` can't be represented. I'll restrict both `chr()` and `#NNN` to `1..255` with a clear error otherwise, rather than let it silently corrupt into an empty string.

**Explicitly not doing**: implicit concatenation of adjacent literals without `+` (real Pascal allows `'Hello'#13#10'World'` as one literal with no `+` needed) — that needs lexer-level literal-fusing I'd rather not add for this scope; `#13 + #10` will work fine via existing concatenation.

Let me check the lexer to confirm `#` is unclaimed, then build it.`#` is confirmed unused. Let's view the full lexer to find the right insertion point:Let's check `vm.c`'s existing string helpers I'll want to reuse for `chr`/`ord`:Now let's implement. First `common.h`:Now `lexer.c` — the keywords, plus `#NNN` tokenization:Now the `#NNN` tokenization, right before the digit-literal handling:Now `parser.c` — `factor()` additions for `ord`/`chr` and `#NNN`:Now let's find the `TOKEN_STRING` literal handling to add `TOKEN_CHARCODE` right after, using the same `intern_string` mechanism:Now `type_checker.c`:Now `codegen.c`:Now `vm.c`:Now `solas.c`/`desole.c`:Let's build:Clean build. Now the tests — `ord`/`chr` and `#NNN` literals:A crash — let's debug via disassembly:Found it — a real bug in the constant-folding logic, not something in `chr`/`ord` codegen itself. Let me check `optimizer.c`'s unary-fold block:This is a serious bug — the fold logic unconditionally deletes `node->left` even when none of the known operators matched (`ORD`/`CHR` fall through every `if`/`else if` with no action, but the unconditional `free_ast(node->left); node->left = NULL;` still runs, corrupting the node into a unary op with a missing operand). This is a real correctness bug, not a `chr`/`ord` codegen issue — and it would silently corrupt *any* future unary op I forget to add explicit fold-handling for. Let me fix the root cause: gate entry to the whole block on the operator being one we actually know how to fold.Let me also double-check the binary-op fold switch doesn't have the same class of bug (it has a `default:` case — need to confirm it returns *before* the unconditional frees, not after):Confirmed safe — the binary-op path's `default: return node;` exits before reaching the frees, so only the unary-op path had this bug. Let's rebuild and re-test:All correct, including the round-trip through byte 200 and concatenation with `#33`. Now the error cases:All six correctly caught with clear, distinct messages. Now round-trip test and full regression suite:All regressions pass. Now let's update the documentation. Let's check the current `## Char` section:Let me verify a clean example before writing the docs:Now let's add `ord`/`chr` to the "Built-in functions and procedures" table from last session, and remove the stale `ord`/`chr` mention from "not implemented":Now the "not implemented" list — remove the stale `ord`/`chr` line:Let's also check the "Literals" section, which lists literal forms — `#NNN` should be listed there too:Now `docs/BYTECODE.md`'s opcode reference and `docs/ASSEMBLER.md`'s mnemonic list — same pattern as before:Now `README.md`'s status line:Final rebuild and sanity check, then package and deliver:## Summary — `ord`/`chr` and dedicated char literal syntax, complete

I hit the tool-use limit right after the final sanity check, which passed cleanly, so here's the full state of the work.

### What's done and verified

**`ord`/`chr`** — new keyword-based built-ins (`TOKEN_ORD`/`TOKEN_CHR`), reusing `NODE_UNARY_OP` like `abs`/`sqr`. `ord(c)` validates its argument is exactly one character at runtime (reusing the existing `vm_check_char` helper) and returns its byte value; `chr(n)` validates `1..255` and interns the resulting single-character string (reusing `vm_intern_string`). Verified round-trip correctness (`ord(chr(200)) = 200`) and all type/range errors.

**`#NNN` char-code literals** — genuinely new syntax (`#` was previously unclaimed), parsed directly to `TYPE_CHAR` at parse time — unlike a quoted literal like `'A'`, which is `TYPE_STRING` and only usable as a char via the existing interop rules. This is what makes it a real "dedicated" char literal rather than just another way to spell a string. Restricted to `1..255`, both `chr()` and `#NNN`, since this compiler's strings are null-terminated C strings under the hood and can't represent a `\0` byte.

**A real bug found and fixed**: while adding `chr`/`ord`, discovered that `optimizer.c`'s unary constant-folding block would unconditionally free and null out a unary op's operand *even for operators it didn't know how to fold* — silently corrupting the node (leaving `chr(65)` with no operand at all) and crashing at runtime with a stack underflow. This wasn't a `chr`/`ord`-specific bug; it was a structural flaw that would have corrupted *any* future unary operator I forgot to add explicit fold-handling for. Fixed by gating entry to the whole block on the operator actually being one of the four known ones, rather than gating only on the operand being a literal. Verified the sibling binary-op fold path didn't have the same issue (its `default: return node;` already exits safely before the frees).

**Fully verified**: all six error paths (`chr(0)`, `chr(256)`, `#0`, `#256`, `ord` on a multi-character string, `ord`/`chr` given the wrong argument type), byte-identical round-trip through `desole`/`solas`, and a full regression suite across the entire project — nothing else affected.

### Documentation — complete
`docs/LANGUAGE.md`'s `## Char` section rewritten with `ord`/`chr` and `#NNN` subsections (example independently verified to run and match its documented output), the built-ins table extended, the "Literals" section updated, and the stale "no `ord`/`chr`" line removed from "What's not implemented." `docs/BYTECODE.md` and `docs/ASSEMBLER.md` opcode/mnemonic references updated, and `README.md`'s status line updated.

### What's left
Nothing has been copied to `/mnt/user-data/outputs` or delivered via `present_files` yet this turn — that's the one remaining step. Want me to do that now?

# Tool limit

All files delivered. To recap where this leaves the char gap: `ord`/`chr` and `#NNN` char-code literals are fully implemented, tested (including all six deliberate error paths), documented, and byte-clean through the full regression suite. The one bug found along the way — the unary constant-folding corruption issue — was a structural fix, not a one-off patch, so it protects any future unary operator I add later too.

That closes out the char gap from the original four-type analysis. Between this session and the last two, integer and char are now both fully closed, and boolean was closed a few sessions back. The one remaining item from that original breakdown is **string**: `length`, indexing individual characters, `copy`/`pos`, and case conversion. Let me know if you'd like to pick that up next, or if there's something else you'd rather work on.

