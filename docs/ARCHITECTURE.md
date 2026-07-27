# Architecture

How this codebase is put together, and — more usefully — what to touch
when you extend it. Written for future-you picking this project back up,
not as a tour for its own sake.

## The compiler pipeline

```
source.pas
    │  lexer.c        source text -> Token stream
    ▼
parser.c              tokens -> AST (recursive descent, one ASTNode tree)
    ▼
type_checker.c         walks the AST, fills in ASTNode.expression_type,
                        rejects anything ill-typed
    ▼
optimizer.c             constant folding + dead-code elimination
    ▼
codegen.c               AST -> Instruction[] (code[]) + resolves the
                         string pool / symbol table it needs
    ▼
bytecode.c (save)       code[] + sym_table[] + string_pool[] -> .bin file
```

`pascalc.c` is the thin driver that calls these five in order. Every
stage after the lexer operates on the same in-memory `ASTNode` tree built
by the parser — there's no intermediate representation between AST and
bytecode.

`solvm` only needs `bytecode.c` (load) + `vm.c` — it never touches any of
the above. `solas`/`desole` only need `bytecode.c` — they read/write the
same `code[]`/`sym_table[]`/`string_pool[]` globals directly, without
going through the AST at all.

## Global state, not parameters

`code[]`, `code_idx`, `sym_table[]`, `sym_count`, `string_pool[]`,
`string_count`, `array_mem_count`, and `token` (declared in `common.h`,
defined in `bytecode.c`/`lexer.c`) are plain global arrays/counters, not
threaded through function parameters. This was a deliberate choice
inherited from the original codebase and kept throughout: it keeps every
function signature small, and every module that needs this state
(`parser.c`, `codegen.c`, `vm.c`, `solas.c`, `desole.c`) just includes
`common.h` and uses it directly.

**The cost of this choice**: anything that populates this state at
compile/assemble time must explicitly reset it at the start, or state
leaks from a previous run in the same process. `parse_ast()` and
`assemble()` both do this:

```c
sym_count = 0;
code_idx = 0;
string_count = 0;
array_mem_count = 0;
```

This matters more than it might look like, because of the next section.

## Recoverable errors, not `exit()`

Every fatal error path — a parse error, a type error, a runtime VM error,
a malformed bytecode file — calls `fatal_abort()` (`error.c`) instead of
`exit(1)`. If a recovery point has been registered
(`fatal_error_active = 1` after a `setjmp(fatal_error_env)`), it
`longjmp`s back there instead of killing the process. Every CLI tool's
`main()` follows this pattern:

```c
if (setjmp(fatal_error_env)) {
    fprintf(stderr, "Compilation failed.\n");
    return 1;
}
fatal_error_active = 1;
... call into the compiler ...
fatal_error_active = 0;
```

**Why this exists**: it's what makes the compiler/VM embeddable — a
future REPL, GUI, or Cowork-style host can call `parse_ast()` /
`generate_code()` / `run_vm()` repeatedly in one long-lived process, and
one bad program won't take the whole host down. `test_recovery.c`
demonstrates this directly: it compiles a broken program, a good program,
and another good program in sequence in one process, and confirms the
process survives the failure and produces correct results afterward
(this is also *why* the global-state reset in the previous section
matters — without it, the second compile in that sequence would silently
inherit stale symbol-table entries from the first).

If you add a new fatal error site, call `fatal_abort()`, never `exit()`
directly — an `exit()` anywhere in library code (not `main()`) breaks
this property silently.

## `verbose_mode`: the other cross-cutting flag

Set from each tool's `-v` argument, declared in `error.h` alongside
`fatal_error_active` because it's the same kind of thing — small global
state that needs to be visible from deep inside library code (notably
`optimizer.c`'s own progress printfs, which fire independently of
whatever `pascalc.c` itself prints). Convention: gate anything that's
debug/progress narration behind `if (verbose_mode)`; never gate an
error message, and never gate a program's own actual output (a compiled
Pascal program's `write`/`writeln` calls execute unconditionally).

## The `ASTNode` struct: field reuse by convention

```c
typedef struct ASTNode {
    NodeType type;
    TokenType op;                // meaning depends on `type`
    DataType expression_type;
    int line;
    union { int num_value; int var_idx; } data;   // meaning depends on `type`
    struct ASTNode *left, *right, *next, *extra;   // meaning depends on `type`
} ASTNode;
```

This struct has stayed exactly this shape since `if`/`while`/`repeat`
were added, despite everything built on top of it since (`for`, strings,
arrays, multi-argument `write`). Every new statement/expression kind
found a way to reuse the existing four pointers, the `op` field, and the
`data` union rather than growing the struct further. This is intentional
— **check whether a new node type's needs fit the existing fields before
adding a fifth pointer or a new union member.** So far, every case has
fit. Current conventions, for reference:

| Node type | `left` | `right` | `next` | `extra` | `op` | `data.var_idx` |
|---|---|---|---|---|---|---|
| `NODE_ASSIGN` (scalar) | value expr | — | next stmt | — | — | target var |
| `NODE_ASSIGN` (array elem) | index expr | value expr | next stmt | — | — | array var |
| `NODE_IF` | condition | then-branch | next stmt | else-branch | — | — |
| `NODE_FOR` | start bound | end bound | next stmt | body | `TOKEN_TO`/`TOKEN_DOWNTO` | loop var |
| `NODE_WRITELN` | head of arg list (chained via each arg's own `next`) | — | next stmt | — | `TOKEN_WRITE`/`TOKEN_WRITELN` | — |
| `NODE_ARRAY_ACCESS` | index expr | — | — | — | — | array var |

Two techniques worth naming explicitly, since they're reused constantly:

- **Sibling chains via `next`.** A statement list, and `write`/`writeln`'s
  argument list, are both just linked lists threaded through each
  element's own `next` pointer — not a separate list/array structure.
  `statement_list()` and the `write`/`writeln` argument-parsing loop in
  `statement()` both do this the same way.
- **Reusing `op` for a discriminator that isn't really "the operator".**
  `NODE_FOR` stores `TOKEN_TO`/`TOKEN_DOWNTO` there; `NODE_WRITELN`
  stores `TOKEN_WRITE`/`TOKEN_WRITELN`. It's not literally an operator in
  either case, just a convenient pre-existing `TokenType` slot.

## Adding a new node type: the checklist

Every AST-level feature added to this project (`if`/`while`/`repeat`,
`for`, strings, arrays, multi-arg `write`) touched roughly the same set of
files, in roughly this order. Skipping one of these is exactly how the
real bugs mentioned below got introduced.

1. **`common.h`** — new `TokenType`/`NodeType`/`Opcode` values as needed
   (append, don't renumber — nothing depends on specific values, but
   renumbering makes diffs harder to review for no benefit).
2. **`lexer.c`** — new keyword(s)/token(s), if any.
3. **`parser.c`** — build the new AST node(s). If the new construct is a
   statement, add it to `is_statement_start()` too.
4. **`type_checker.c`** — a `case` for the new node type if it needs
   type-specific validation. **Also check whether the top-of-function
   generic recursion** (`type_check(node->left); ...->right; ...->next;
   ...->extra;`) already covers your new node's children — it usually
   does, since it recurses into all four unconditionally regardless of
   node type.
5. **`optimizer.c`** — same generic-recursion check for `optimize_ast()`
   and `mark_used_variables()`. If your node type introduces a variable
   *reference* (like `NODE_ARRAY_ACCESS`), it must be added explicitly to
   `mark_used_variables()`'s `if (node->type == ...)` check, or
   dead-code elimination will incorrectly treat that variable as unused.
6. **`ast_printer.c`** — a `case` for the new node type. **This switch has
   no `default:`, so the compiler (with `-Wall -Wextra`) will refuse to
   compile silently — a missing case is a build warning, not a silent
   gap.** `codegen.c`'s switch has the same property. Treat any such
   warning as a hard stop, not a nuisance.
7. **`codegen.c`** — emit the actual instructions. If this needs new
   opcodes, add them to `common.h`'s `Opcode` enum first.
8. **`vm.c`** — implement any new opcode(s). Bounds-check every array/
   variable/string index a new opcode touches — see
   [docs/BYTECODE.md](BYTECODE.md) for the standard the rest of the VM
   holds to.
9. **`solas.c` / `desole.c`** — add the new mnemonic(s) to keep the
   toolchain consistent, *if* new opcodes were added. Purely-syntactic
   features that lower entirely to existing opcodes (this was true for
   `for` loops) need no assembler/disassembler changes at all.

## Real bugs this pattern has caught (and how)

Worth internalizing these, since they're exactly the mistakes this
checklist exists to prevent:

- **Forgetting to recurse into `extra`.** When `NODE_IF`'s else-branch
  (`extra`) was added, the dead-code sweep's generic fallback only
  recursed into `left`/`right`/`next` — an unreferenced variable
  assigned only inside an `else` branch wouldn't have been correctly
  swept. Caught before shipping by explicitly checking every generic
  recursion site against the new field.
- **A memory leak in dead-code removal.** When array-element assignment
  started using `NODE_ASSIGN.right` for the value expression (previously
  unused for that node type), the "remove this dead assignment" path in
  `sweep_dead_assignments()` still only freed `left`. Fixed by freeing
  `right` too wherever `left` is freed in that function, and running
  `optimize_ast()` on it first for the same reason `left` already was:
  constant-folding a **dead** subtree can still surface a real compile
  error, like a literal division by zero, that the user would want to
  know about even though the code never runs.
- **Stale `Symbol` metadata across compiles.** When `Symbol` grew
  `is_array`/`array_lower`/`array_upper`/`array_base` fields, both
  `add_var()` (compiler and assembler) and `add_temp_var()` (the `for`
  loop's hidden bound variable) needed to explicitly zero those new
  fields — `sym_table[]` is a persistent global array; only `sym_count`
  resets between compiles in the same process, so a slot reused by a
  later compile could otherwise inherit `is_array = 1` from an earlier
  compile that happened to use the same index for an array.
- **Conflating "generate this block" with "the program is over."**
  `NODE_COMPOUND`'s codegen used to emit `OP_HALT` unconditionally,
  which only worked because a compound block appeared exactly once, at
  the program root. The moment `if`/`while` bodies could *also* be
  `begin...end` blocks, that would have halted the VM mid-loop. Fixed by
  moving the halt to an explicit `emit_halt()`, called once by
  `pascalc.c` after the whole program is generated — `NODE_COMPOUND`
  itself just generates its body and continues the enclosing chain.

## Why five binaries

```
pascalc  = pascalc.c   + lexer/parser/type_checker/optimizer/codegen/ast_printer.c + bytecode.c + error.c
solvm    = solvm.c     + vm.c                                                     + bytecode.c + error.c
solas    = solas.c                                                                + bytecode.c + error.c
desole   = desole.c                                                               + bytecode.c + error.c
```

The Makefile's `FRONTEND_OBJS`/`VM_OBJS`/`SHARED_OBJS` split enforces
this at the link level, not just by convention: `solvm` cannot
accidentally end up linking any compiler-frontend code, and `pascalc`
cannot end up linking the VM. `bytecode.c` (the `.bin` format + the
shared `code[]`/`sym_table[]`/`string_pool[]` state) and `error.c` (the
recoverable-error facility + `verbose_mode`) are the only things every
binary shares.

## Testing approach used throughout this project

There's no formal test framework — every feature added so far was
verified the same way, worth continuing:

1. **Positive cases**: write a small `.pas` program exercising the new
   feature, compile and run it, and hand-verify the output against what
   the feature should do (worked through by hand, not just eyeballed —
   e.g. summing 1..10 and checking the loop actually produces 55).
2. **Error cases**: for every new compile-time or runtime error path
   added, write a program that should trigger it and confirm the exact
   message and that the process exits cleanly (not a crash).
3. **Round-trip through `solas`/`desole`** whenever new opcodes are
   involved: disassemble the compiler's output, reassemble it, and diff
   the VM's output on both — this has caught real issues (e.g. an
   opcode's semantics changing without updating what `desole` prints)
   before they became invisible bugs.
4. **Full regression pass** before considering a change done: re-run a
   representative program from each earlier feature (control flow,
   strings, `for`, arrays, error recovery) to confirm nothing broke.
5. **`-Wall -Wextra` clean build, always** — several of the switches
   over `NodeType`/`Opcode` deliberately have no `default:` case so a
   missing case becomes a compiler warning, not a silent gap.
