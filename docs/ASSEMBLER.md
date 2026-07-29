# `solas` / `desole`: Assembler & Disassembler Guide

`solas` assembles a readable text format directly to SolVM's `.bin`
bytecode format — the same format `pascalc` produces and `solvm`
executes. `desole` does the reverse. Together they let you inspect
exactly what the compiler generated, hand-write bytecode without going
through the compiler, or hand-patch a compiled program.

Both are self-contained: they never link against the Pascal compiler
frontend or the VM, only the shared `bytecode.c` file-format code (see
[docs/ARCHITECTURE.md](ARCHITECTURE.md)).

## Usage

```sh
solas [-v] <input.sasm> <output.bin>
desole [-v] <input.bin> [output.sasm]     # omit output to print to stdout
```

`-v` shows a write-confirmation message (instruction/symbol/string
counts). Without it, both tools are silent on success — `solas` produces
nothing but the output file; `desole`'s actual output (the disassembly
listing) is unaffected either way, since that's its real job, not debug
narration.

## `.sasm` syntax

```
; a comment - runs to end of line
.var <name> <integer|boolean|string|char|real>     ; declare a variable
.array <name> <lower> <upper> <type>         ; declare a 1D array
.array2d <name> <lo1> <hi1> <lo2> <hi2> <type> ; declare a 2D array
label:                                        ; a label, alone on its own line
MNEMONIC [operand]                            ; one instruction per line
```

- Mnemonics and type keywords are case-insensitive (`push`, `PUSH`,
  `Push` all work). Variable, array, and label names are case-sensitive —
  matching how the Pascal compiler treats identifiers.
- `.var`/`.array`/`.array2d` can appear anywhere in the file, not just at
  the top — and so can a label used before it's declared (`jz done`
  followed later by `done:`). Assembly is two-pass specifically to make
  this work.
- A label must be alone on its own line (no instruction on the same
  line).
- `.array`/`.array2d` bounds are integer literals, may be negative:
  `.array flags -3 3 boolean`.

### Operand kinds, by instruction

| Kind | Instructions | Example |
|---|---|---|
| Integer literal | `PUSH`, `ENTER`, `LOAD_LOCAL`, `STORE_LOCAL` | `PUSH 5`, `ENTER 2`, `LOAD_LOCAL 0` — a local slot is just a number, not a named symbol |
| Variable name | `LOAD`, `STORE`, `READ` | `LOAD x` — must have a matching `.var` |
| Array name | `LOAD_IDX`, `STORE_IDX`, `LOAD_IDX2D`, `STORE_IDX2D` | `LOAD_IDX nums` — must have a matching `.array`/`.array2d`; the runtime index (indices, for the 2D ops) come off the stack, not the operand |
| Label name | `JMP`, `JZ`, `CALL` | `JZ done`, `CALL sum` — may be a forward reference |
| Quoted string | `PUSH_STR` | `PUSH_STR "hello"` — interned into the string pool; content is everything between the first and last `"` on the line, so an embedded `"` works without escaping |
| *(none)* | everything else (including `RET`) | |

**`PRINT` and `PRINT_STR` do not print a trailing newline** — use
`NEWLINE` explicitly. This mirrors exactly what the Pascal compiler
generates for `write`/`writeln`: one `PRINT`/`PRINT_STR` per argument,
then one `NEWLINE` only for `writeln`.

### Full mnemonic list

`PUSH`, `LOAD`, `STORE`, `READ`, `ADD`, `SUB`, `MUL`, `DIV`, `EQ`, `LT`,
`GT`, `AND`, `OR`, `NOT`, `LTE`, `GTE`, `NEQ`, `NEG`, `MOD`, `XOR`,
`BAND`, `BOR`, `BXOR`, `BNOT`, `SHL`, `SHR`, `DUP`, `ABS`, `FABS`,
`FSQRT`, `FSIN`, `FCOS`, `FARCTAN`, `FEXP`, `FLN`, `FPOWER`, `ORD`, `CHR`,
`LENGTH`, `STR_CHAR_AT`, `COPY`, `POS`, `UPCASE_CHAR`, `UPPERCASE_STR`,
`LOWERCASE_STR`, `LEFT`, `RIGHT`,
`PRINT`, `PRINT_BOOL`, `HALT`, `JMP`, `JZ`, `PUSH_STR`, `PRINT_STR`,
`SEQ`, `SCMP`, `SCONCAT`, `NEWLINE`, `LOAD_IDX`, `STORE_IDX`,
`LOAD_IDX_DYN`, `STORE_IDX_DYN`, `LOAD_IDX2D`, `STORE_IDX2D`,
`FADD`, `FSUB`, `FMUL`, `FDIV`, `FEQ`, `FLT`, `FGT`, `FLTE`, `FGTE`,
`FNEQ`, `FNEG`, `FPRINT`, `INT_TO_REAL`, `TRUNC`, `ROUND`,
`PRINT_PADDED`, `PRINT_STR_PADDED`, `PRINT_BOOL_PADDED`,
`FPRINT_PADDED`, `FPRINT_PADDED_PRECISE`, `CALL`,
`RET`, `ENTER`, `LOAD_LOCAL`,
`STORE_LOCAL`, `POP`. See
[docs/BYTECODE.md](BYTECODE.md#opcode-reference) for what each one does,
and [Procedures](BYTECODE.md#procedures-call-ret-and-stack-frames) for
the calling convention and a worked recursive example.

## Example: countdown loop

```
.var i integer
    push 5
    store i
loop:
    load i
    push 0
    gt
    jz done
    load i
    print
    load i
    push 1
    sub
    store i
    jmp loop
done:
    halt
```

```sh
solas countdown.sasm countdown.bin
solvm countdown.bin
```

## Example: array access

```
.array nums 1 3 integer

    push 1
    push 10
    store_idx nums     ; nums[1] := 10
    push 2
    push 20
    store_idx nums     ; nums[2] := 20

    push 1
    load_idx nums       ; push nums[1]
    print
    newline
    halt
```

## Reading `desole` output

`desole` reconstructs `.var`/`.array` declarations from the file's symbol
table, and synthesizes a label (`L<address>`) at every instruction that's
actually a jump target — labels only appear where something jumps to
them, computed with a pre-pass over the code. Every instruction line
carries an address comment (`; 0004`), which is safe for `solas` to
re-assemble since `;` starts a comment.

```sh
desole program.bin
```

```
.var i integer

    push 5 ; 0000
    store i ; 0001
L2:
    load i ; 0002
    push 0 ; 0003
    gt ; 0004
    jz L13 ; 0005
    ...
L13:
    halt ; 0013
```

Variable/array references print by name (looked up from the symbol
table) rather than raw index, and jump targets print as `L<n>` rather
than a bare number. If a `.bin` file is corrupted or hand-crafted with an
invalid operand — an out-of-range variable index, an out-of-range jump
target — `desole` doesn't crash; it prints an inline comment flagging the
problem instead:

```
    store ?99 ; 0000  (variable index out of range: 0..-1)
    jz ?9999 ; 0001  (jump target out of range: 0..2)
```

## Round-tripping

Disassembling and reassembling reproduces byte-identical VM behavior:

```sh
desole program.bin program.sasm
solas program.sasm program2.bin
# solvm program.bin and solvm program2.bin now behave identically
```

This is a genuinely useful workflow, not just a correctness demo: it lets
you diff two versions of the compiler's codegen output as readable text,
or hand-patch a single instruction in a compiled program without
recompiling from source.
