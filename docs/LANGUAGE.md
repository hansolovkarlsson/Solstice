# Language Reference

The Pascal dialect accepted by `pascalc`. It follows Wirth-style Pascal
where it exists here, but doesn't claim compatibility with any particular
standard — features are added as they're built, and this document only
describes what's actually implemented.

Keywords are case-insensitive (`WHILE`, `while`, and `While` are the same
token). Identifiers are case-sensitive (`x` and `X` are different
variables).

## Program structure

```pascal
program Name;
var
    { declarations }
begin
    { statements }
end.
```

- The program name is required but otherwise unused (no significance
  beyond documentation).
- The `var` section is optional — omit it entirely if the program declares
  no variables.
- The final `.` after `end` is required.

## Comments

Two forms: block comments delimited by curly braces (may span multiple
lines), and `//` line comments (run to end of line):

```pascal
{ this is a comment }
x := 1; { so is this }
{
  and this
}
y := 2; // this too, to end of line
```

## Types

| Type | Keyword | Literal examples | Notes |
|---|---|---|---|
| Integer | `integer` | `42`, `-7`, `0` | Standard C `int` range |
| Boolean | `boolean` | `true`, `false` | |
| String | `string` | `'hello'`, `'it''s here'` | Doubled `''` is an escaped literal quote |
| Char | `char` | `'a'`, `'!'`, `'x'` | See [Char](#char) below - a single-quoted literal of length exactly 1 |
| Array | `array[lower..upper] of T` | — | `T` is `integer`, `boolean`, `string`, or `char`; see [Arrays](#arrays) |

There is no `real`/floating-point type, and no user-defined
(`record`/`enum`) types.

## Variable declarations

```pascal
var
    a, b, c: integer;
    flag: boolean;
    name: string;
    scores: array[1..10] of integer;
    grid: array[0..9] of boolean;
```

- Multiple names sharing one type can be comma-separated on one line
  (limit: 20 names per line).
- Every variable must be declared before use; there's no forward
  reference for variables (unlike labels/procedures in real Pascal, which
  this dialect doesn't have anyway).
- Total variable count (including compiler-internal temporaries — see
  [`for`](#for-loops)) is capped at 100 in one program.

## Literals

- **Integers**: a run of digits, e.g. `123`. A leading `-` is handled by
  the unary minus operator, not the literal itself.
- **Booleans**: the keywords `true` and `false`.
- **Strings**: single-quoted, e.g. `'hello'`. A literal single quote
  inside a string is written as two consecutive quotes: `'it''s a test'`
  produces `it's a test`. String literals cannot span multiple lines.
  Maximum length is 255 characters.

## Operators

### Arithmetic (integer operands, except `+` which also works on strings)

| Operator | Meaning |
|---|---|
| `+` | Addition, or string concatenation if both operands are strings |
| `-` | Subtraction |
| `*` | Multiplication |
| `/` | Integer division (alias for `div`) |
| `div` | Integer division |
| `mod` | Modulo |

Division or modulo by a literal zero is a compile-time error; by a
runtime-zero value is a runtime error. Both are caught, never silently
produce garbage.

### Comparison

| Operator | Meaning | Works on |
|---|---|---|
| `=` | Equal | integer, string, char |
| `<>` | Not equal | integer, string, char |
| `<`, `>`, `<=`, `>=` | Ordering | integer, string, char |

String/char equality compares the actual characters, not identity.
String/char ordering is lexicographic (character-by-character, like
`strcmp`) — e.g. `'apple' < 'banana'` is `true`.

### Logical (boolean operands)

| Operator | Meaning |
|---|---|
| `and` | Logical AND |
| `or` | Logical OR |
| `xor` | Logical XOR |
| `not` | Logical NOT (unary) |

There's no short-circuit evaluation — both operands of `and`/`or` are
always evaluated.

### Precedence (highest to lowest)

1. Unary `-`, `not`, parenthesized expressions
2. `*`, `/`, `div`, `mod`, `and`
3. `+`, `-`, `or`, `xor`
4. `=`, `<>`, `<`, `>`, `<=`, `>=`

Assignment (`:=`) is a statement, not an expression — you can't write
`x := (y := 5)`.

## Statements

### Assignment

```pascal
x := 5;
name := 'Alice';
scores[i] := scores[i] + 1;
```

The right-hand side's type must exactly match the variable's (or, for an
array element, the array's element type).

### `if` / `then` / `else`

```pascal
if x > 0 then
    writeln('positive')
else if x < 0 then
    writeln('negative')
else
    writeln('zero');
```

The condition must be `boolean`. `else` is optional. Both branches are a
single statement — use `begin...end` for multiple statements in a branch:

```pascal
if x > 0 then begin
    writeln('positive');
    count := count + 1;
end;
```

### `while` / `do`

```pascal
while i <= 10 do begin
    writeln(i);
    i := i + 1;
end;
```

Condition checked before each iteration (zero iterations if false
initially).

### `repeat` / `until`

```pascal
repeat
    n := n + 1;
until n = 5;
```

Body runs at least once; loops while the `until` condition is **false**
(i.e. stops once it becomes true). No `begin`/`end` needed — `repeat` and
`until` already bracket the body, which can hold multiple
semicolon-separated statements directly.

### `for` loops

```pascal
for i := 1 to 10 do
    writeln(i);

for i := 10 downto 1 do
    writeln(i);
```

- The loop variable and both bounds must be `integer`.
- **The end bound is evaluated once**, at loop start — not on every
  iteration. If the loop body modifies a variable the bound expression
  depends on, the loop's iteration count is unaffected:

  ```pascal
  n := 5;
  for i := 1 to n do begin
      writeln(i);
      n := 0;   { does NOT shorten the loop - it still runs 5 times }
  end;
  ```
- After the loop, the loop variable holds one past the last value used
  (`n + 1` for `to`, `n - 1` for `downto`) — don't rely on its exact value
  after the loop without checking.

### `break` and `continue`

```pascal
while true do begin
    i := i + 1;
    if i = 5 then break;      { exit the loop immediately }
end;

for i := 1 to 10 do begin
    if i mod 2 = 0 then continue;   { skip to the next iteration }
    writeln(i);
end;
```

- Valid inside `while`, `for`, and `repeat` — using either outside a loop
  is a compile-time error, checked at the exact point it's written (not
  just "somewhere in this function"), including when nested inside an
  `if` that isn't itself inside a loop.
- In nested loops, `break`/`continue` always apply to the **innermost**
  enclosing loop only.
- `continue` in a `for` loop still runs the increment step before
  re-checking the loop condition — it doesn't skip incrementing the loop
  variable.

### `write` and `writeln`

```pascal
write('a');
write('b');
writeln('c');        { prints "abc" then a newline }

writeln('val: ', 42, ' ok=', true);   { "val: 42 ok=1" }

writeln;              { just a newline }
writeln();             { same thing }
```

- Both take zero or more comma-separated arguments of any mixed type
  (`integer`, `boolean`, `string`).
- Arguments are printed back-to-back with **no separator** — include your
  own spaces in string literals if you want them.
- `writeln` appends exactly one trailing newline, after all arguments.
  `write` never appends one.
- Booleans print as `1` (true) or `0` (false); there's no `true`/`false`
  text output.
- Parentheses are optional when there are no arguments.

### `readln`

```pascal
readln(n);      { integer }
readln(flag);   { boolean: must be exactly 0 or 1, or it's a runtime error }
readln(name);   { string: reads a full line }
```

Each call prints a `> ` prompt, then reads from standard input. Reading an
integer or boolean also consumes the rest of that input line (so a
following `readln` of any type starts cleanly on the next line).

### Compound statements

`begin ... end` groups zero or more statements (semicolon-separated, the
last one's trailing `;` is optional) into a single statement — usable
anywhere a single statement is expected (loop/if bodies, or the whole
program body).

## Char

```pascal
var
    grade: char;
begin
    grade := 'A';
    writeln(grade);
```

`char` is implemented as a `string` that's constrained to hold exactly
one character — there's no separate literal syntax; a `char` value comes
from any single-quoted literal (or string expression) that happens to be
exactly one character long. This means:

- `char` and `string` freely mix in assignment, comparison (`=`, `<>`,
  `<`, `>`, `<=`, `>=`, all lexicographic), and `+` concatenation (which
  always produces a `string`, even from two `char`s).
- The length-1 constraint is enforced **at runtime**, not compile time —
  assigning a longer string-typed expression into a `char` variable
  compiles fine but fails when it actually runs:

  ```pascal
  var c: char;
  var s: string;
  begin
      s := 'hi';
      c := s;   { runtime error: "c requires a single character, got "hi"" }
  end.
  ```

  This mirrors how `readln` into a `boolean` is checked (must be `0` or
  `1`) — the type is enforced by validating the actual value the moment
  it's stored, not by tracking string lengths through arbitrary
  expressions at compile time.
- `readln` into a `char` variable works the same way: reads a line, then
  requires it to be exactly one character.
- There's no `ord`/`chr` (converting between a character and its integer
  code) — those are conventionally built-in *functions*, and this
  language doesn't have user-callable functions yet.

## Arrays

```pascal
var
    scores: array[1..10] of integer;
    flags: array[0..3] of boolean;
    names: array[-5..5] of string;
```

- Bounds are compile-time-constant integer literals (may be negative,
  e.g. `array[-5..5]`); they can't be variables or general expressions.
- `upper` must be `>= lower`.
- Indexing (`scores[i]`) requires the index expression to be `integer`,
  and it's bounds-checked **at runtime** — an out-of-range index is a
  runtime error, not undefined behavior.
- An array reference always needs an index; there's no whole-array
  assignment or whole-array printing.
- Total storage across every array declared in one program is capped at
  4096 elements combined.
- No multi-dimensional arrays (`array[1..3, 1..3] of integer` isn't
  supported — nest a loop instead, or wait for that feature).

## Procedures

```pascal
program Example;
var
    total: integer;

procedure sumTo(n: integer);
var
    partial: integer;
begin
    if n = 0 then
        partial := 0
    else begin
        sumTo(n - 1);
        partial := n + total;
    end;
    total := partial;
end;

begin
    sumTo(5);
    writeln('sum 1..5 = ', total);   { 15 }
end.
```

- Procedures are declared after the `var` section and before the main
  `begin...end.` body:
  `procedure name [(params)] ; [var locals;] <compound-statement>;`
- **Parameters** are declared like `var`, but parenthesized and
  semicolon-separated between groups:
  `procedure foo(a, b: integer; flag: boolean);`. Passed by value —
  modifying a parameter inside the procedure never affects the caller's
  argument.
- **Local variables** use an ordinary `var` section, placed after the
  parameter list and before `begin`:
  `procedure foo(x: integer); var temp: integer; begin ... end;`
- **Parameters and locals are scalar only this increment** — `integer`,
  `boolean`, `string`, `char`. No array parameters or array locals yet.
- **Recursion works, with correct per-call isolation.** Each call gets
  its own private copy of every parameter and local — the worked example
  above only computes the right sum because of this (each recursive
  call's `n` and `partial` survive the nested call underneath it
  untouched).
- **A local shadows an outer name of the same type** — a parameter or
  local variable with the same name as a global variable (or even a
  procedure) takes priority inside that procedure's body, standard Pascal
  lexical scoping. The global is simply inaccessible by that name from
  inside the procedure; nothing about the global itself changes.
- **A procedure can call any procedure declared earlier** (or itself),
  but not one declared later in the file — there's no `forward`
  declaration yet, so mutual recursion (A calls B, B calls A) isn't
  possible.
- Not yet supported, with a clear compile error if attempted:
  `readln` into a parameter or local variable, and using one as a `for`
  loop's counter. Both work fine as ordinary expressions/assignments —
  just not in those two specific positions yet.
- A procedure's name and a variable's name share one namespace — you
  can't declare a procedure with the same name as an existing global
  variable, or vice versa.
- Functions (procedures that return a value) aren't implemented yet — a
  procedure "returns" a result only by writing to a global variable (as
  in the example above) or, less commonly, by leaving a value on the
  operand stack for the caller before returning (possible today by
  hand-writing `.sasm`, see [docs/BYTECODE.md](BYTECODE.md), but the
  Pascal compiler doesn't expose this as `function`/`result` syntax yet).

## Errors

Every compile error reports as `file:line: Compile Error: message` (or
`Type Error:` for a type-checking failure) and stops compilation. Runtime
errors from `solvm` report as `VM Runtime Error: message` and stop
execution. Both categories are deliberate, checked failures — there's no
undefined behavior reachable from valid or invalid Pascal source; a bug in
your program produces a clear error message, not a silent wrong answer or
a crash.

## What's not implemented

- Functions (procedures that return a value), array parameters/locals,
  `forward` declarations (needed for mutual recursion) — see
  [Procedures](#procedures) above for what does exist today
- Multi-dimensional arrays
- `real`/floating-point numbers
- `ord`/`chr` (needs functions, above)
- Records, sets, enumerated types
- Units/modules/`uses`

See the project README's Status section for the current plan.
