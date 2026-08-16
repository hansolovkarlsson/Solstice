# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

**Solstice**: a multi-language toolchain built around one custom
stack-based virtual machine (`solvm`) and its bytecode format. Not aiming
for P-Code or any existing VM compatibility — the bytecode format and
machine are designed from scratch, under project control, so that
multiple front-end languages can eventually target it.

The main compiler being worked on is `pascalc`, a Wirth-style Pascal
compiler, developed in parallel with the assembler (`solas`) and
disassembler (`desole`) for the VM's own bytecode. Current phase: get
`pascalc` compatible with Wirth/standard Pascal, and expand `solvm` and
`solas`/`desole` alongside it as new language features demand new
bytecode capability. A second front end, `basicc` (classic line-numbered
BASIC — see [docs/BASIC.md](docs/BASIC.md)), has its first milestone
shipped, targeting the same bytecode format and VM unmodified.

```
 source.pas  ──(pascalc)──┐
 source.bas  ──(basicc)───┼──> program.bin ──(solvm)──> runs it
 source.sasm ──(solas)────┘         │
                                 (desole)
                                     ▼
                               readable .sasm
```

### Longer-term direction

Worth keeping in mind when making design decisions in `common.h`,
`vm.c`, `solas.c`/`desole.c` — bias toward choices that don't foreclose
these, but don't build for them speculatively either (see the "don't
design for hypothetical future requirements" rule below — none of this
is scoped work yet):

- **Pascal** is the main, most advanced front end. After
  Wirth-compatibility, the plan is to grow it toward object-oriented
  Pascal, then possibly add C-style `enum`/`union` concepts.
- **The VM and assembler are meant to grow into general OOP support**
  (and other advanced features) so later languages can rely on the same
  bytecode primitives rather than each language inventing its own.
- **BASIC** (`basicc`, classic line-numbered, not aiming for
  compatibility with any one dialect — pulls features across BASIC's
  history, from early BASIC through Visual Basic/VB.NET, for whatever's
  useful) has its first milestone shipped — see
  [docs/BASIC.md](docs/BASIC.md) and [docs/ROADMAP.md](docs/ROADMAP.md)
  for what's still open. Further out and more speculative, roughly in
  order of interest: **Logo**, **Prolog**, **LISP**, and **Smalltalk**.
- Eventually, an original language of the author's own design, tentatively
  named **Phoenix**, drawing on ideas from the above, intended to have
  built-in GUI, lightweight database handling, networking, and
  token/syntax parsing support.

None of the above is in scope until the Pascal compiler and VM reach the
Wirth-compatible milestone — treat it as orientation for *why* the VM/
bytecode layer should stay language-agnostic, not as a backlog to
implement against.

## Build

Source lives under `src/`, split into one directory per binary plus a
shared `src/common/` (see "Why six binaries" below); a single
non-recursive Makefile at the repo root builds all of them straight into
`bin/`.

```sh
make            # builds bin/pascalc, bin/solvm, bin/solas, bin/desole, bin/test_recovery, bin/basicc
make pascalc    # build just one binary
make clean      # remove binaries and object files
```

Requires a C11 compiler (`cc`) and `make`. No external dependencies.
Build flags: `-Wall -Wextra -std=c11 -g -fno-common`. Several switches
over `NodeType`/`Opcode` deliberately omit a `default:` case so that a
missing case is a compiler warning, not a silent gap — **keep the build
warning-clean**; a new warning after adding a node/opcode means a case
was missed, not a nuisance to suppress.

`scripts/make.sh` (run from repo root, needs `config.sh` sourced first —
see below) does the same build.

### Running a compiled program

```sh
pascalc examples/hello.pas hello.bin   # bin/ is on PATH once config.sh is sourced
solvm hello.bin
```

Every tool accepts an optional `-v` for verbose/debug output (compiler
phase banners, AST dump, VM step banners, final variable-state dump).
Without `-v` each tool prints only what it's actually supposed to produce
(the compiled program's own `write`/`writeln` output, the disassembly
listing, etc.) and stays silent otherwise.

### Shell helpers

`source config.sh` from repo root puts `bin/` and `scripts/` on `PATH`.
Then:

```sh
pascal.sh foo.pas     # pascalc foo.pas foo.bin && solvm foo.bin (pass -v as $1 to verbose both)
solas.sh foo.sasm     # solas foo.sasm foo.bin && solvm foo.bin
desole.sh foo.bin     # desole foo.bin foo.disasm && cat foo.disasm
solvm.sh foo.bin      # solvm foo.bin
```

## Testing

There is no formal test framework or test runner script. `examples/Pascal/`
holds one small `.pas` file per `pascalc` feature/regression, named
`test_<feature>_<variant>.pas`, grouped into one subdirectory per feature
prefix (`examples/Pascal/<feature>/test_<feature>_<variant>.pas` —
e.g. `examples/Pascal/ptr/test_ptr_basic.pas`, `examples/Pascal/goto/
test_goto_badscope.pas`); a new test for an existing feature goes in its
existing subdirectory, a genuinely new feature gets a new one named after
its prefix. `examples/BASIC/` holds `basicc`'s own `.bas` regression
tests the same way, flat for now (small enough not to need per-feature
subdirectories yet — see [docs/BASIC.md](docs/BASIC.md)).
Assembler/VM-opcode-level regression tests (hand-written `.sasm`, no
Pascal or BASIC source involved) live in `examples/asm/` instead,
grouped by feature prefix the same way as `examples/Pascal/`. Verify a
change by compiling and running the relevant file(s) and hand-checking
output. The approach used throughout this project, in order:

1. **Positive case**: write/find a small `.pas` program exercising the
   feature, compile and run it, hand-verify the output (work it out, not
   just eyeball it).
2. **Error case**: for every new compile-time or runtime error path,
   confirm the exact message and a clean exit — not a crash.
3. **Round-trip through `solas`/`desole`** whenever new opcodes are
   involved: disassemble the compiler's output, reassemble it, diff the
   VM's output on both. This has caught real bugs (e.g. an opcode's
   semantics changing without `desole`'s printer being updated).
4. **Full regression pass** before considering a change done: re-run a
   representative program from each earlier feature (control flow,
   strings, `for`, arrays, error recovery).
5. **`-Wall -Wextra` clean build, always.**

`test_recovery` (in `src/pascalc/`, built by `make`) demonstrates that a fatal
compile error doesn't kill the host process — it compiles a broken
program then two good ones in the same process and confirms the process
survives and both good compiles still succeed.

## Architecture

Full detail in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — read it
before making structural changes. Key points:

### Pipeline

```
source.pas
    │  lexer.c        source text -> Token stream
    ▼
parser.c              tokens -> AST (recursive descent, one ASTNode tree)
    ▼
type_checker.c         walks the AST, fills in ASTNode.expression_type
    ▼
optimizer.c             constant folding + dead-code elimination
    ▼
codegen.c               AST -> Instruction[] (code[]) + string pool/symbol table
    ▼
bytecode.c (save)       code[] + sym_table[] + string_pool[] -> .bin file
```

`pascalc.c` just drives these five stages in order. Everything after the
lexer operates on the same in-memory `ASTNode` tree — there's no separate
IR between AST and bytecode. `solvm` only links `bytecode.c` (load) +
`vm.c`, never the frontend. `solas`/`desole` only link `bytecode.c`, and
read/write `code[]`/`sym_table[]`/`string_pool[]` directly without ever
building an AST. The directory split (`src/pascalc/`, `src/solvm/`,
`src/solas/`, `src/desole/`, `src/common/`) plus the root Makefile's
`FRONTEND_OBJS`/`VM_OBJS`/`COMMON_OBJS` variables enforces this
separation at the link level.

### Global state, not parameters

`code[]`, `code_idx`, `sym_table[]`, `sym_count`, `string_pool[]`,
`string_count`, `array_mem_count`, `token` (declared in `common.h`) are
plain globals, used directly by any module that includes `common.h`
rather than threaded through function parameters. Deliberate, kept
throughout for small function signatures. **The cost**: anything that
populates this state at compile/assemble time must explicitly reset it
first (`parse_ast()` and `assemble()` both zero `sym_count`, `code_idx`,
`string_count`, `array_mem_count` at the top) or state leaks across
compiles in the same process — this is exactly what `test_recovery`
guards against.

### Recoverable errors, never `exit()`

Every fatal error path calls `fatal_abort()` (`error.c`), not `exit(1)`.
If a recovery point is registered (`fatal_error_active = 1` after
`setjmp(fatal_error_env)`), it `longjmp`s back instead of killing the
process — this is what makes the compiler/VM embeddable in a long-lived
host. Every CLI tool's `main()` follows:

```c
if (setjmp(fatal_error_env)) {
    fprintf(stderr, "Compilation failed.\n");
    return 1;
}
fatal_error_active = 1;
... call into the compiler ...
fatal_error_active = 0;
```

**If you add a new fatal error site, call `fatal_abort()`, never
`exit()`** — an `exit()` anywhere outside `main()` breaks this silently.

`verbose_mode` (also in `error.h`) is the other cross-cutting flag, set
from each tool's `-v`. Convention: gate debug/progress narration behind
`if (verbose_mode)`; never gate an error message or a compiled program's
own `write`/`writeln` output.

### The `ASTNode` struct: field reuse by convention

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

This struct hasn't grown since `if`/`while`/`repeat`. Every new node type
has found a way to reuse the existing four pointers, `op`, and the `data`
union — **check whether a new node type's needs fit the existing fields
before adding a fifth pointer or a new union member**; so far every case
has fit. Two recurring techniques: sibling lists threaded through each
node's own `next` (statement lists, `write`/`writeln` argument lists —
not a separate list structure), and reusing `op` as a discriminator that
isn't literally an operator (`NODE_FOR` stores `TOKEN_TO`/`TOKEN_DOWNTO`
there; `NODE_WRITELN` stores `TOKEN_WRITE`/`TOKEN_WRITELN`). See the
field-meaning table in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for
the current per-node-type convention.

### Adding a new AST-level feature: the checklist

Every feature added so far (`if`/`while`/`repeat`, `for`, strings,
arrays, records, `real`, procedures/functions) touched roughly this same
set of files in this order — skipping one is exactly how real bugs got
introduced (see the "Real bugs this pattern has caught" section of
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for three concrete examples
worth reading before touching this path):

1. **`common.h`** — new `TokenType`/`NodeType`/`Opcode` values (append,
   don't renumber).
2. **`lexer.c`** — new keyword(s)/token(s), if any.
3. **`parser.c`** — build the new AST node(s); add to
   `is_statement_start()` if it's a statement.
4. **`type_checker.c`** — a `case` if it needs type-specific validation.
   Check first whether the generic top-of-function recursion into
   `left`/`right`/`next`/`extra` already covers the new node's children.
5. **`optimizer.c`** — same generic-recursion check for `optimize_ast()`
   and `mark_used_variables()`. A node type that introduces a variable
   *reference* (like `NODE_ARRAY_ACCESS`) must be added explicitly to
   `mark_used_variables()`, or dead-code elimination will wrongly treat
   that variable as unused.
6. **`ast_printer.c`** — a `case` for the new node type (no `default:` —
   a missing case is a build warning, treat it as a hard stop).
7. **`codegen.c`** — emit instructions; add new opcodes to `common.h`'s
   `Opcode` enum first if needed.
8. **`vm.c`** — implement any new opcode(s); bounds-check every array/
   variable/string index it touches (see
   [docs/BYTECODE.md](docs/BYTECODE.md)).
9. **`solas.c` / `desole.c`** — add new mnemonics, *only if* new opcodes
   were added. Purely-syntactic features lowering to existing opcodes
   need no assembler/disassembler changes.

### Why six binaries

```
pascalc  = src/pascalc/{pascalc,lexer,parser,type_checker,optimizer,codegen,ast_printer}.c + src/common/{bytecode,error}.c
solvm    = src/solvm/{solvm,vm}.c                                                          + src/common/{bytecode,error}.c
solas    = src/solas/solas.c                                                               + src/common/{bytecode,error}.c
desole   = src/desole/desole.c                                                             + src/common/{bytecode,error}.c
basicc   = src/basicc/{basicc,lexer,parser,type_checker,codegen,ast_printer}.c             + src/common/{bytecode,error}.c
```

`src/common/bytecode.c` (the `.bin` format + shared `code[]`/
`sym_table[]`/`string_pool[]` state) and `src/common/error.c`
(recoverable-error facility + `verbose_mode`) are the only things every
binary shares — hence the shared `src/common/` directory, separate from
each binary's own subdirectory. `basicc` deliberately does NOT extend
`common.h`'s `TokenType`/`NodeType`/`ASTNode` with BASIC's own vocabulary
— `pascalc` and `basicc` are separate binaries that never link together,
so nothing is gained by sharing Pascal's much larger enums; BASIC's own
`BasicTokenType`/`BasicNodeType`/`BasicASTNode` live in
`src/basicc/basic.h` instead. See [docs/BASIC.md](docs/BASIC.md).

### SolVM memory model

Six fixed-size storage regions (`common.h`'s `MAX_*` constants) — no
dynamic allocation, no GC, no shared addressable memory between regions:
`vm_stack[]` (evaluation stack), `vm_vars[]` (scalar variable slots),
`vm_array_mem[]` (all arrays, contiguous, each `Symbol` carries its own
base offset), `string_pool[]` (interned strings, can grow at runtime),
`vm_call_stack[]` (one `{return_addr, saved_fp, saved_frame_sp}` per
active call), `vm_frame_stack[]` (local variable slots per active call).
Strings and `char` are both just `int` indices into `string_pool[]`;
booleans are `0`/`1` ints — this is why `LOAD`/`STORE` work identically
regardless of declared type, and only type-*interpreting* opcodes
(`PRINT` vs `PRINT_STR`, `EQ` vs `SEQ`) need type-specific variants. Full
opcode reference and `.bin` file format: [docs/BYTECODE.md](docs/BYTECODE.md).

## Documentation map

- [docs/LANGUAGE.md](docs/LANGUAGE.md) — the accepted Pascal dialect: syntax, types, statements, operators, worked examples per feature.
- [docs/BYTECODE.md](docs/BYTECODE.md) — SolVM architecture, full opcode reference, `.bin` file format.
- [docs/ASSEMBLER.md](docs/ASSEMBLER.md) — `solas`/`desole` `.sasm` syntax and usage.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — internals and extension checklist; start here before structural changes.
- [docs/ROADMAP.md](docs/ROADMAP.md) — the authoritative, checklist-level project plan: current-phase tasks plus later phases (OOP Pascal, further front ends, Phoenix). Scoped to what's still open — a shipped item moves out to `docs/CHANGELOG.md` once it lands. Keep it updated as work lands or plans change — this is the one place tracking what's actually left to do, superseding the old `notes/todo.md`.
- [docs/CHANGELOG.md](docs/CHANGELOG.md) — the chronological record of shipped features and fixed bugs, each with the design decisions and bugs found along the way. Move a `docs/ROADMAP.md` item here (as a `[x]` entry, full detail intact) once it ships, rather than leaving it to accumulate in the roadmap.
- Root [README.md](README.md) — current feature status (working / not yet implemented), project layout table, and a one-paragraph roadmap summary that links to docs/ROADMAP.md.

## Repo layout beyond `src/`

- `examples/Pascal/`, `examples/BASIC/`, `examples/asm/`, `examples/audit/`, `examples/doc/`, `examples/tech/` — `.pas`/`.bas`/`.sasm` sample and test programs, grouped by purpose: `Pascal/` = `pascalc` regression tests, split into one subdirectory per feature prefix since it's by far the largest (see [Testing](#testing) above); `BASIC/` = `basicc` regression tests, flat for now (small enough not to need per-feature subdirectories yet); `asm/` = hand-written `solas`/`desole`/VM-opcode example AND regression-test `.sasm` programs (the latter grouped the same per-feature-prefix way as `Pascal/`); `doc/` = examples referenced from `docs/`; `tech/` = misc technical exercises; `audit/` = audit-driven test programs.
- `notes/` — the author's own freeform design notes (naming ideas, musings); not authoritative — the actual plan lives in [docs/ROADMAP.md](docs/ROADMAP.md).
- `chats/` — saved transcripts from other AI assistants used during design.
- `new/` — a staging inbox: `scripts/unpack.sh` unzips incoming work into `new/files/`, `scripts/mvnew.sh` distributes it into the right home (`README.md` → root, other `*.md` → `docs/`, `Makefile` → root, each `.c`/`.h` → its own `src/{common,pascalc,solvm,solas,desole,basicc}/` by filename, `doc_*.pas` → `examples/doc/`, `test_<feature>_<variant>.pas` → `examples/Pascal/<feature>/`, `test_<feature>_<variant>.sasm` → `examples/asm/<feature>/`, `test_<variant>.bas` → `examples/BASIC/`, any other `*.sasm` → `examples/asm/`). Not part of the build.
