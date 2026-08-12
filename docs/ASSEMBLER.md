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
desole -x <input.bin> [output.txt]        # raw hexdump instead of disassembly
```

`-v` shows a write-confirmation message (instruction/symbol/string
counts). Without it, both tools are silent on success — `solas` produces
nothing but the output file; `desole`'s actual output (the disassembly
listing) is unaffected either way, since that's its real job, not debug
narration.

`desole -x` prints a classic offset/hex/ASCII hexdump of the `.bin`
file's raw on-disk bytes instead of the structured disassembly — useful
for inspecting the exact byte layout, or for a corrupted/truncated file
that `load_bytecode()` itself refuses to open (hexdump mode reads the
file directly, without going through the normal loader at all).

## `.sasm` syntax

```
; a comment - runs to end of line
.var <name> <integer|boolean|string|char|real>     ; declare a variable
.array <name> <lower> <upper> <type>         ; declare a 1D array
.array2d <name> <lo1> <hi1> <lo2> <hi2> <type> ; declare a 2D array
.arrayrec <name> <lower> <upper> <field_count> ; declare a 1D array of records
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
| Array-of-records name | `LOAD_ARRAY_RECORD_FIELD`, `STORE_ARRAY_RECORD_FIELD`, `STORE_ARRAY_RECORD_FIELD_CHAR` | `LOAD_ARRAY_RECORD_FIELD pts` — must have a matching `.arrayrec`; the runtime index and the field's offset within one element both come off the stack (index pushed first, then field offset) |
| Integer literal (heap) | `NEW`, `DISPOSE` | `NEW 2` — element size (ints per instance), not a symbol reference |
| Integer literal (field offset) | `LOAD_HEAP_FIELD`, `STORE_HEAP_FIELD`, `STORE_HEAP_FIELD_CHAR` | `LOAD_HEAP_FIELD 1` — the pointer VALUE being dereferenced always comes off the stack (every pointer shares one heap, so there's no "which array"-style name operand here) |
| Integer literal (hop count) | `PUSH_STATIC_LINK` | `PUSH_STATIC_LINK 1` — `0` = push the current `fp`, `N` = walk the static-link chain `N` times, `-1` = the "no valid link" sentinel |
| Integer literal (packed levels_up/slot) | `LOAD_ENCLOSING`, `STORE_ENCLOSING`, `PUSH_ENCLOSING_REF` | `LOAD_ENCLOSING 4098` — compute by hand as `(levels_up << 12) \| slot`, e.g. `(1 << 12) \| 2` = `4098` for a slot 2 local one lexical level up |
| Label name | `JMP`, `JZ`, `CALL` | `JZ done`, `CALL sum` — may be a forward reference |
| Quoted string | `PUSH_STR` | `PUSH_STR "hello"` — interned into the string pool; content is everything between the first and last `"` on the line, so an embedded `"` works without escaping |
| *(none)* | everything else (including `RET`) | |

**`PRINT` and `PRINT_STR` do not print a trailing newline** — use
`NEWLINE` explicitly. This mirrors exactly what the Pascal compiler
generates for `write`/`writeln`: one `PRINT`/`PRINT_STR` per argument,
then one `NEWLINE` only for `writeln`.

### Full mnemonic list

`PUSH`, `LOAD`, `STORE`, `READ`, `READ_LOCAL_INT`, `READ_LOCAL_BOOL`,
`READ_LOCAL_REAL`, `READ_LOCAL_STR`, `READ_LOCAL_CHAR`,
`ADD`, `SUB`, `MUL`, `DIV`, `EQ`, `LT`,
`GT`, `AND`, `OR`, `NOT`, `LTE`, `GTE`, `NEQ`, `NEG`, `MOD`, `XOR`,
`BAND`, `BOR`, `BXOR`, `BNOT`, `SHL`, `SHR`, `DUP`, `ABS`, `FABS`,
`FSQRT`, `FSIN`, `FCOS`, `FARCTAN`, `FEXP`, `FLN`, `FPOWER`, `ORD`, `CHR`,
`LENGTH`, `STR_CHAR_AT`, `STR_CHAR_REPLACE`, `COPY`, `POS`, `UPCASE_CHAR`, `UPPERCASE_STR`,
`LOWERCASE_STR`, `LEFT`, `RIGHT`,
`PRINT`, `PRINT_BOOL`, `HALT`, `HALT_CODE`, `JMP`, `JZ`, `PUSH_STR`, `PRINT_STR`,
`SEQ`, `SCMP`, `SCONCAT`, `NEWLINE`, `LOAD_IDX`, `STORE_IDX`,
`LOAD_IDX_DYN`, `STORE_IDX_DYN`, `LOAD_IDX2D`, `STORE_IDX2D`,
`FADD`, `FSUB`, `FMUL`, `FDIV`, `FEQ`, `FLT`, `FGT`, `FLTE`, `FGTE`,
`FNEQ`, `FNEG`, `FPRINT`, `INT_TO_REAL`, `TRUNC`, `ROUND`,
`PRINT_PADDED`, `PRINT_STR_PADDED`, `PRINT_BOOL_PADDED`,
`FPRINT_PADDED`, `FPRINT_PADDED_PRECISE`, `CALL`, `CALL_INDIRECT`,
`RET`, `ENTER`, `LOAD_LOCAL`,
`STORE_LOCAL`, `POP`, `SWAP`, `OVER`, `ROT`,
`DEBUG_STACK`, `DEBUG_SYMS`. See
[docs/BYTECODE.md](BYTECODE.md#opcode-reference) for what each one does,
and [Procedures](BYTECODE.md#procedures-call-ret-and-stack-frames) for
the calling convention and a worked recursive example.

## Macros

```
.macro NAME [param1 param2 ...]
    <body lines>
.endmacro
```

A later line whose first token is `NAME`, followed by exactly as many
space-separated arguments as the macro has parameters, expands into the
body. Expansion is pure text substitution: every whole-word occurrence
of a parameter name in the body is replaced by the corresponding
argument text, so a parameter can stand in for a variable name, a label,
or an integer literal alike — there's no notion of a parameter's "kind".
Macro names are matched case-insensitively, like instruction mnemonics
(a macro invocation reads just like an instruction), and a macro name
can't collide with an existing mnemonic.

A label *defined* inside a macro's body (a `name:` line) is
automatically given a fresh name unique to that one expansion, so
invoking the same macro more than once doesn't produce a duplicate-label
error — a label the body only *references*, without defining (e.g. one
passed in as a parameter), is left untouched, so a macro can still jump
to a caller-supplied or genuinely global label.

```
.macro COUNTDOWN start
    push start
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
    newline
.endmacro

    countdown 3    ; prints 321
    countdown 2    ; prints 21 - loop:/done: don't collide with the call above
```

A macro's body may invoke another macro (nested expansion, up to a
fixed depth limit as a recursion guard). Expansion is a single
top-to-bottom pass over the file, so **a macro must be defined — its
`.macro`/`.endmacro` block already seen — before any line that invokes
it**, including from inside another macro's body; unlike labels, there's
no forward reference. Macro definitions don't nest (no `.macro` inside
another `.macro`'s body).

Macro invocation happens entirely before `solas`'s own two-pass
assembly, so `desole` never reconstructs `.macro` syntax — disassembling
a program that used a macro shows the fully expanded instructions, with
the mangled local-label names macro expansion generated.

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
