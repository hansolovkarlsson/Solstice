# SolVM: Bytecode & Virtual Machine Reference

SolVM is a stack-based virtual machine. Every instruction is an
`(opcode, arg)` pair; `arg` is a single `int` whose meaning depends on the
opcode (an immediate value, a variable index, a jump target, a string-pool
index — never more than one of these per instruction).

## Memory model

SolVM has six separate storage regions, each a fixed-size array sized at
compile time (`common.h`'s `MAX_*` constants) — there is no dynamic
allocation, no garbage collector, and no shared addressable memory between
regions:

| Region | What it holds | Size limit |
|---|---|---|
| `vm_stack[]` | The evaluation stack — operands for arithmetic, comparisons, etc. — and, by convention, arguments and return values crossing a `CALL`/`RET` | `MAX_STACK` = 100 |
| `vm_vars[]` | One `int` slot per scalar variable (an `integer`, a `0`/`1` `boolean`, or a string-pool index) | `MAX_SYMBOLS` = 100 |
| `vm_array_mem[]` | Every array in the program, laid out contiguously; each array's `Symbol` entry carries its own base offset | `MAX_ARRAY_MEM` = 4096 elements total |
| `string_pool[]` | Interned string contents (`char[256]` each) — populated at compile/assemble time, and can also grow at runtime (concatenation, `readln` into a string) | `MAX_STRINGS` = 256 |
| `vm_call_stack[]` | One record per active call: `{return_addr, saved_fp, saved_frame_sp}` — see [Procedures](#procedures-call-ret-and-stack-frames) | `MAX_CALL_DEPTH` = 256 |
| `vm_frame_stack[]` | Local variable slots for every active call, stacked | `MAX_FRAME_STACK` = 4096 |

**Strings and booleans are represented as integers.** A string *value*
anywhere in the VM — on the stack, in a variable slot, in an array
element — is just an `int` index into `string_pool[]`. A boolean is `0` or
`1`. This is why `LOAD`/`STORE` work identically regardless of a
variable's declared type: the VM doesn't need to know or care what a slot
"means" to move it around. Only the opcodes that actually interpret a
value — printing, comparing, concatenating — need type-specific variants
(`PRINT` vs `PRINT_STR`, `EQ` vs `SEQ`, etc.).

**`char` is exactly the same representation as `string`** — a
`string_pool[]` index, nothing more. Every string opcode works unchanged
for `char` values; there's no `OP_LOAD_CHAR` or similar. The *only* place
`char` is actually distinguished from `string` at runtime is `OP_STORE`
and `OP_STORE_IDX`, which check the target `Symbol`'s declared type and,
if it's `TYPE_CHAR`, reject a value whose `string_pool[]` entry isn't
exactly one character long. This is the same pattern `OP_READ` already
uses to enforce `boolean`'s `0`/`1` domain — a static type distinction
enforced by a runtime value check at the point of storage, rather than by
tracking it through the type system at compile time.

**Every array in a program shares one flat memory region.** Each array's
`Symbol` entry (see [File format](#file-format-bin)) carries `array_base`
(where it starts in `vm_array_mem[]`), `array_lower`/`array_upper` (its
declared index range). Element access computes
`offset = array_base + (runtime_index - array_lower)` and is
**bounds-checked against `[array_lower, array_upper]` on every single
access** — this is the one place in the VM where an unchecked out-of-range
write would silently corrupt a *different* array's data, since they all
live in the same region, so it's checked rigorously on both read
(`LOAD_IDX`) and write (`STORE_IDX`).

## Execution model

`run_vm()` is a straightforward fetch-decode-execute loop: read the
instruction at `ip`, increment `ip`, dispatch on the opcode, repeat until
`OP_HALT`. There's no call stack (no procedures/functions exist yet), so
control flow is entirely `ip` manipulation — `OP_JMP`/`OP_JZ` just set
`ip` directly, and the existing `ip` bounds check at the top of the loop
is what catches a malformed or out-of-range jump target, on the very next
cycle, with no special-casing needed.

Every array/variable/string index used at runtime is validated before
it's dereferenced. A bad index — from corrupted bytecode, a hand-written
`.sasm` bug, or an out-of-range array access in valid-looking Pascal — is
always a reported `VM Runtime Error`, never a crash or silent memory
corruption.

## Opcode reference

`arg` is `0` (unused) unless noted.

### Stack & variables

| Opcode | Effect |
|---|---|
| `PUSH` | Push the integer `arg`. |
| `LOAD` | Push `vm_vars[arg]`. |
| `STORE` | Pop a value; store into `vm_vars[arg]`. |
| `LOAD_IDX` | Pop a runtime index; bounds-check against `arg`'s (an array symbol's) declared range; push the element. |
| `STORE_IDX` | Pop a value, then a runtime index (value was pushed *after* the index by codegen); bounds-check; store the element. |
| `LOAD_IDX_DYN` | Same as `LOAD_IDX`, but *which* array is also popped from the stack instead of coming from `arg` - needed for array parameters, since different calls can pass different arrays. Pop a runtime index, then a runtime array reference (a symbol index); bounds-check; push the element. |
| `STORE_IDX_DYN` | Same as `STORE_IDX`, but *which* array is also popped from the stack. Pop a value, then a runtime index, then a runtime array reference; bounds-check; store. |
| `LOAD_IDX2D` | `arg` = a 2D array's symbol index. Pop the second runtime index, then the first (second pushed last by codegen, so it's on top); bounds-check each against its own dimension; push the element at the row-major offset `(i - lower1) * dim2_size + (j - lower2)`. |
| `STORE_IDX2D` | Same addressing as `LOAD_IDX2D`. Pop a value, then the second index, then the first (value pushed last); bounds-check; store. |

### Arithmetic (integer)

| Opcode | Effect |
|---|---|
| `ADD` / `SUB` / `MUL` / `DIV` | Pop `b`, pop `a`, push `a op b`. `DIV` aborts with a runtime error on division by zero. |
| `MOD` | Pop `b`, pop `a`, push `a % b`. Aborts on `b == 0`. |
| `NEG` | Pop `a`, push `-a`. |

### Comparison (integer) & logic (boolean)

| Opcode | Effect |
|---|---|
| `EQ` / `LT` / `GT` / `LTE` / `GTE` / `NEQ` | Pop `b`, pop `a`, push `a op b` (as `0`/`1`). |
| `AND` / `OR` | Pop `b`, pop `a`, push the logical result. No short-circuiting. |
| `NOT` | Pop `a`, push `!a`. |
| `XOR` | Pop `b`, pop `a`, push `a != b`. |
| `BAND` / `BOR` / `BXOR` | Bitwise integer AND/OR/XOR - pop `b`, pop `a`, push `a & b` / `a \| b` / `a ^ b`. Chosen by codegen instead of `AND`/`OR`/`XOR` when both operands are `integer` rather than `boolean` - the underlying `0`/`1` boolean values happen to make bitwise and logical AND/OR/XOR agree, but not NOT (see `BNOT`), so these are genuinely separate opcodes rather than a shared one. |
| `BNOT` | Pop `a`, push `~a` (bitwise NOT / ones' complement). Distinct from `NOT` because `~0` is `-1`, not `1` - reusing `NOT` for integers would be wrong. |
| `SHL` / `SHR` | Pop `b` (shift amount), pop `a`, push `a << b` / a *logical* (not sign-extending) `a >> b`. Runtime error if `b` is outside `0..31`, rather than the undefined behavior C's `<<`/`>>` give for an out-of-range shift. |
| `DUP` | Duplicate the top of the stack (push a second copy). A generic primitive - first used by `sqr(x)` (evaluate `x` once, `DUP`, `MUL`), rather than evaluating `x`'s bytecode twice. |
| `ABS` | Pop `a`, push its absolute value. |
| `ORD` | Pop a `string_pool[]` index; validate it refers to exactly one character (same check as storing into a `char` slot); push that character's byte value (`0..255`). |
| `CHR` | Pop an integer (`1..255` - `0` can't be represented, since `string_pool[]` entries are null-terminated C strings); intern the single-character string for that byte value (reusing an existing pool entry if there is one); push its index. |
| `LENGTH` | Pop a `string_pool[]` index; push `strlen()` of it. |
| `STR_CHAR_AT` | Pop a runtime (1-based) index, then a `string_pool[]` index. Bounds-checked (a runtime error if out of range) - unlike `COPY` below, string *indexing* is strict, matching real Pascal. Intern the single character at that position; push its index. |
| `COPY` | Pop `count`, then `start`, then a `string_pool[]` index. Extracts the substring - *clamped*, not bounds-checked: an out-of-range `start` or a `count` running past the end just yields as much of the string as exists (possibly empty), matching real Pascal's `copy()` rather than this VM's usual strict-bounds convention. Intern the result; push its index. |
| `POS` | Pop a haystack `string_pool[]` index, then a needle `string_pool[]` index (pushed needle-then-haystack). Push the needle's 1-based position in the haystack, or `0` if not found (an empty needle is defined as "not found"). |
| `UPCASE_CHAR` | Pop a `string_pool[]` index (must be exactly one character); push the uppercased version if it's a lowercase letter, else push the same index back unchanged. |
| `UPPERCASE_STR` / `LOWERCASE_STR` | Pop a `string_pool[]` index; push a new interned string with every letter case-converted. |
| `LEFT` / `RIGHT` | Pop `count`, then a `string_pool[]` index; push a new interned string of the first/last `count` characters, clamped to the string's actual length (never errors). |

### Strings

| Opcode | Effect |
|---|---|
| `PUSH_STR` | Push `arg`, a `string_pool[]` index. |
| `PRINT_STR` | Pop an index; print `string_pool[index]` — **no trailing newline**. |
| `SEQ` | Pop two indices; push `1` if their `string_pool[]` contents are equal (`strcmp`), else `0`. There's no separate string-inequality opcode — `<>` compiles as `SEQ` then `NOT`. |
| `SCMP` | Pop two indices (`b`, then `a`); push `-1`, `0`, or `1` for `a < b`, `a == b`, `a > b` (lexicographic, via `strcmp`, normalized to a fixed sign). `<`/`>`/`<=`/`>=` on strings compile as `SCMP` followed by `PUSH 0` and the matching integer `LT`/`GT`/`LTE`/`GTE` — no separate string-ordering opcodes needed. |
| `SCONCAT` | Pop two indices; concatenate their contents; intern the result (deduped, may grow `string_pool[]` at runtime); push the new index. Aborts if the result exceeds 255 characters or the pool is full (256 distinct strings). |

### I/O

| Opcode | Effect |
|---|---|
| `PRINT` | Pop a value; print it as an integer — **no trailing newline**. |
| `PRINT_BOOL` | Pop a value; print `TRUE` (nonzero) or `FALSE` (zero) — **no trailing newline**. |
| `NEWLINE` | Print `\n`. No stack interaction. `writeln` emits exactly one of these, after all its arguments; `write` never does. |
| `READ` | Prompt (`> `) and read from stdin into `vm_vars[arg]`. Behavior depends on the variable's declared type: integer reads with `scanf("%d")`; boolean does the same but aborts unless the value is `0` or `1`; string reads a full line via `fgets`. Either integer/boolean path flushes the rest of the input line afterward, so a following string `READ` isn't handed a stray empty line. |

### Control flow

| Opcode | Effect |
|---|---|
| `JMP` | Set `ip = arg` unconditionally. |
| `JZ` | Pop a value; if it's `0`, set `ip = arg`. Otherwise fall through. |
| `HALT` | Stop execution. With `-v`, prints the final value of every non-internal variable (see [`desole`](ASSEMBLER.md) for how `__`-prefixed names are hidden). |

### Procedures

| Opcode | Effect |
|---|---|
| `CALL` | Push `{return_addr, current fp, current frame_sp}` onto `vm_call_stack[]` (`return_addr` is `ip` as it already stands, past this instruction); set `ip = arg`. |
| `RET` | Pop `vm_call_stack[]`; restore `ip`, `fp`, and `frame_sp` from the popped record. Restoring `frame_sp` deallocates the entire returning call's frame in one step, whatever its size. A runtime error (not a crash) if the call stack is empty. |
| `ENTER` | `arg` = number of local slots to reserve. Set `fp = frame_sp + 1`; zero-initialize `arg` slots starting there; advance `frame_sp` past them. Normally the first instruction of a procedure body. |
| `LOAD_LOCAL` | `arg` = a slot index relative to `fp`. Push `vm_frame_stack[fp + arg]`. |
| `STORE_LOCAL` | `arg` = a slot index relative to `fp`. Pop a value; store it at `vm_frame_stack[fp + arg]`. |
| `POP` | Pop a value and discard it. Used when a function is called as a statement (its return value unwanted) rather than as part of an expression - the value is still pushed like any function's, but nothing consumes it, so this discards it explicitly rather than leaving the operand stack unbalanced. |

See [Procedures: CALL, RET, and stack frames](#procedures-call-ret-and-stack-frames) below for the full picture, including why `CALL` needs to save more than just a return address.

## How control flow compiles

There's no dedicated "loop" or "branch" instruction — `if`/`while`/
`repeat`/`for` all lower to `JZ`/`JMP` with backpatched targets. This is
worth understanding if you're reading `desole` output or writing `.sasm`
by hand.

**`if <cond> then <then> [else <else>]`**
```
    <cond>
    JZ else_or_end
    <then>
  [ JMP end            ; only if there's an else
  else_or_end:
    <else>
  end: ]
```

**`while <cond> do <body>`**
```
loop_start:
    <cond>
    JZ end
    <body>
    JMP loop_start
end:
```

**`repeat <body> until <cond>`**
```
loop_start:
    <body>
    <cond>
    JZ loop_start        ; loop again while cond is still false
```

**`for <var> := <start> to/downto <end> do <body>`**
```
    <start>
    STORE var
    <end>
    STORE end_tmp        ; cached ONCE - see below
loop_start:
    LOAD var
    LOAD end_tmp
    LTE/GTE              ; LTE for 'to', GTE for 'downto'
    JZ loop_end
    <body>
    LOAD var
    PUSH 1
    ADD/SUB              ; ADD for 'to', SUB for 'downto'
    STORE var
    JMP loop_start
loop_end:
```

The end bound is evaluated once into a compiler-generated hidden variable
(named `__for_tmp<N>`), not re-evaluated every iteration — Pascal
semantics require the loop's range to be fixed at the start, even if the
body later changes a variable the bound expression depended on.

### `break` and `continue`

Each loop's `JZ`/`JMP` targets above are all known at a single, fixed
point during codegen (`loop_start` before the body, or `code_idx` right
after it) — one placeholder, patched once. `break`/`continue` need a
different technique, since either can appear an arbitrary number of times
anywhere inside the loop body, and none of those occurrences know the
loop's exit/continue address yet when they're generated.

Each loop (`while`/`repeat`/`for`) pushes a small context before
generating its body: two growable lists of pending `JMP` instruction
indices, one for every `break` encountered, one for every `continue`.
Each `break`/`continue` statement just emits a `JMP 0` placeholder and
appends its own instruction index to the relevant list. Once the loop
finishes generating - at which point both the break-target (just past the
loop) and the continue-target (loop-type-specific: the condition
re-check for `while`, the until-condition for `repeat`, the increment
step for `for`) are finally known - every pending placeholder in both
lists is patched in one pass.

## Procedures: CALL, RET, and stack frames

There's no built-in notion of "a procedure" — `CALL`/`RET` plus a per-call
local-variable frame are primitives; everything about how they're used
(parameter passing, return values, when to call `ENTER`) is convention,
not something the VM enforces. This is the same philosophy as the rest of
the instruction set: a small number of primitives, composed.

### Why CALL saves more than a return address

A bare "push return address, jump; pop, jump back" would be enough for
control flow alone. But once a call has its own local variables, `RET`
also needs to **deallocate** them and **restore the caller's own frame**
- otherwise the caller's `LOAD_LOCAL 2` would silently start reading
whatever the callee left behind in that slot. So `CALL` captures a full
restore record - `{return_addr, saved_fp, saved_frame_sp}` - and `RET`
restores all three at once. Restoring `saved_frame_sp` alone deallocates
the returning call's entire frame regardless of how large it was; no
separate "how big was my frame" bookkeeping is needed.

This is also why `fp` (frame pointer) and `frame_sp` (frame-stack top)
are two different things: `frame_sp` only ever grows while a chain of
active calls is nested, tracking the *combined* depth of every live
frame; `fp` always points at the base of whichever call's own code is
*currently executing*. While call A is paused waiting on a nested call to
B, `frame_sp` reflects both A's and B's frames combined, but `fp` points
at B's - and the moment B returns, `fp` snaps back to A's, correctly
scoped again to A's own locals.

### The calling convention (by convention, not enforced)

- **Parameters**: the caller pushes argument values onto the ordinary
  `vm_stack[]` operand stack *before* `CALL` - no different from pushing
  operands for arithmetic. The callee's first instruction is normally
  `ENTER <n>` (reserving `n` local slots), followed by one `STORE_LOCAL`
  per parameter, pulling each pushed value off the operand stack into
  slots `0..k-1`.
- **Return values**: a "function" leaves its result on the operand stack
  before `RET`. The caller finds it there, on top of the stack, right
  after the `CALL` returns - exactly where any other expression's result
  would be. This is exactly the convention `pascalc` itself uses for
  Pascal `function`s: a hidden extra local slot holds the return value
  (assigning to the function's own name inside its body targets that
  slot), and `LOAD_LOCAL` on it right before `RET` pushes the result. If
  a function is called as a *statement* rather than as part of an
  expression, its still-pushed return value is popped and discarded
  (`POP`) rather than left to unbalance the operand stack for whatever
  comes next.
- **Locals**: anything beyond the parameters just gets its own slot
  number (`k..n-1`) reserved by the same `ENTER <n>`.

### Worked example: recursive sum

```
    push 5
    call sum
    print       ; 15 = 1+2+3+4+5
    newline
    halt
sum:
    enter 1          ; local 0 = n (the parameter)
    store_local 0
    load_local 0
    push 0
    eq
    jz recurse
    push 0           ; base case: sum(0) = 0
    ret
recurse:
    load_local 0     ; n - stays on the operand stack across the call
    load_local 0
    push 1
    sub              ; n - 1
    call sum         ; recursive call - its own independent frame
    add              ; n + sum(n-1)
    ret
```

This is worth tracing by hand once: `sum` calls itself five times before
hitting the base case, so at the deepest point there are five live
frames, each with its own `n` (`5, 4, 3, 2, 1, 0`). As each level
returns, the caller's `n` - loaded onto the operand stack *before* making
the recursive call, so it survives the call untouched - is still exactly
what that level pushed, letting `add` compute the right partial sum at
every level on the way back out. If `n` were a single shared slot instead
of a real per-call local, this would silently compute garbage instead of
`15`.

## File format (`.bin`)

Written/read by `save_bytecode()`/`load_bytecode()` in `bytecode.c`. All
integers are the platform's native `int` (no explicit endianness handling
— files aren't expected to move between architectures). Every count is
validated against its `MAX_*` limit on load before being used to size a
read, so a truncated or corrupted file is rejected with a clear error
rather than overflowing a buffer.

```
"SOLE"                  4 bytes, magic number

sym_count               int
Symbol[sym_count]        the symbol table

code_idx                int
Instruction[code_idx]     the program

string_count             int
char[string_count][256]  the string pool
```

`Symbol` (`common.h`):
```c
typedef struct {
    char name[32];
    DataType type;    // element type if is_array, else the scalar's type
    int is_array;
    int array_lower;  // inclusive
    int array_upper;  // inclusive
    int array_base;   // offset into vm_array_mem[]
} Symbol;
```

`Instruction`:
```c
typedef struct {
    Opcode op;
    int arg;
} Instruction;
```

Note that `array_mem_count` (how much of `vm_array_mem[]` is in use) is
**not** part of the file format — it's purely a compile/assemble-time
bookkeeping counter. The VM never needs it: each array's `base`/`lower`/
`upper` in its own `Symbol` entry is all `LOAD_IDX`/`STORE_IDX` need, and
`vm_array_mem[]` itself is a fixed-size array regardless of how much of it
any particular program actually uses.
