# BASIC (`basicc`) — milestone 1

Classic line-numbered BASIC, compiled by `basicc` to the same `.bin`
format `pascalc` produces — it runs on the unmodified `solvm`, and needs
no `solas`/`desole` changes. Not aiming for compatibility with any one
historical dialect; see [docs/ROADMAP.md](ROADMAP.md) for what's still
open beyond this first milestone.

```sh
basicc program.bas program.bin
solvm program.bin
```

Like every tool in this project, `-v` prints compiler phase banners and
an AST dump.

## Lines and statements

Every line is `<line number> <statement>[: <statement>...]`. Line
numbers must be strictly ascending through the source file — there's no
reordering pass. Multiple statements on one line are separated by `:`.
`REM` (or a trailing `'`) starts a comment running to the end of the
line. A line number with nothing else on it (blank, or comment-only) is
valid — a `GOTO`/`GOSUB` to it falls through to whatever comes next,
exactly like real BASIC.

```basic
10 REM a comment-only line, and a blank one below, are both fine
20
30 LET X = 1: PRINT X
```

Statements: `LET` (the `LET` keyword itself is optional), `PRINT`,
`INPUT`, `IF`/`THEN`/`ELSE`, `GOTO`, `GOSUB`/`RETURN`, `FOR`/`TO`/`STEP`/
`NEXT`, `END`.

## Types and variables

No declarations — a variable's sigil fixes its type the first time it's
used anywhere (an assignment target, an expression operand, a `FOR`/
`NEXT`/`INPUT` target):

| Sigil | Type      |
|-------|-----------|
| `$`   | string    |
| `%`   | integer   |
| (none)| real      |

The sigil is part of the variable's name — `A`, `A%`, and `A$` are three
independent variables. There's no boolean type; `IF`'s condition is a
plain numeric expression tested against zero (nonzero is true), matching
classic BASIC's own numeric-truthiness convention.

An integer promotes implicitly to real wherever a real is expected
(`LET X = 5` widens the integer literal `5` to `5.0` for real-sigil
`X`). The reverse — assigning a real value to an integer-sigil variable —
is a compile error in v1: there's no `ROUND`/`TRUNC`/`INT` builtin yet to
make that conversion explicit, so it's rejected rather than silently
truncated. Use a real-sigil variable, or a real-sigil intermediate, if a
value needs to travel as real.

## Expressions

Arithmetic: `+ - * /`. `/` always produces a real result (both operands
are promoted to real first, even for two integers), matching Pascal's
own `/` in this same toolchain. String `+` concatenates; mixing a string
and a number with `+` (or any other arithmetic/comparison operator) is a
compile error. Relational: `= <> < > <= >=`, usable on numbers or on
strings (lexicographic), never mixed. `AND`/`OR`/`NOT` require integer
operands.

**`AND`/`OR` are bitwise, not short-circuit logical**, on plain integers —
the same conflation classic BASIC dialects themselves relied on (their
`-1`-for-`TRUE` convention makes bitwise AND/OR behave as logical AND/OR
for values that are actually `0` or `-1`). This dialect's own comparisons
push `0`/`1` rather than `0`/`-1`, which still composes correctly under
bitwise AND/OR as long as both operands are themselves comparison
results, `0`, or `1` — `IF (X=1) AND (Y=2) THEN` works as expected, but
`IF 4 AND 8 THEN` does NOT (bitwise `4 AND 8` is `0`, even though both
operands are individually truthy) the way a true short-circuit logical
AND would. Not yet resolved with a separate boolean type/opcode path;
tracked in docs/ROADMAP.md alongside the rest of what's still open.

## `PRINT`

Comma and semicolon both just separate items — v1 doesn't implement
classic BASIC's comma print-zone tabbing, so items print with no space
between them unless a string literal supplies one. A trailing comma or
semicolon suppresses the newline `PRINT` would otherwise print:

```basic
10 PRINT "X = "; X
20 FOR I = 1 TO 3: PRINT I;: NEXT I
30 PRINT
```

## `FOR`/`NEXT`

```basic
10 FOR I = 1 TO 10 STEP 2
20 PRINT I
30 NEXT I
```

`STEP` (default `1`) must be a compile-time constant, optionally
negative (`STEP -1`) — its sign fixes, at compile time, whether the loop
tests `<=` or `>=` against the end bound. **The end bound (and `STEP`, if
given) are re-evaluated every iteration, not cached at loop entry** —
matches real BASIC as long as the bound doesn't change during the loop
body; a loop that mutates a variable its own bound expression reads will
behave differently from a BASIC that caches the bound once at entry.
Cache it into your own variable before the `FOR` if that matters. `FOR`/
`NEXT` are matched the same way parentheses are — the innermost open
`FOR` is what an unqualified `NEXT` (or a `NEXT` naming that same
variable) closes; naming a different, non-innermost variable is a
compile error, and so is an unmatched `FOR` or `NEXT`.

## What's not supported yet

See the "Still open" list in [docs/ROADMAP.md](ROADMAP.md#phase-3--additional-front-ends):
arrays (`DIM`), string functions (`LEFT$`/`MID$`/...), user-defined `SUB`/
`FUNCTION`, `^` exponentiation, `DATA`/`READ`/`RESTORE`, file I/O, and
BASIC's own OOP support.
