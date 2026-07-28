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
| Boolean | `boolean` | `true`, `false` | `write`/`writeln` print it as `TRUE`/`FALSE` |
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
| `=` | Equal | integer, boolean, string, char |
| `<>` | Not equal | integer, boolean, string, char |
| `<`, `>`, `<=`, `>=` | Ordering | integer, boolean, string, char |

String/char equality compares the actual characters, not identity.
String/char ordering is lexicographic (character-by-character, like
`strcmp`) — e.g. `'apple' < 'banana'` is `true`. Boolean is ordinal
(`false < true`), matching standard Pascal.

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
- **Scalar parameters and locals** (`integer`, `boolean`, `string`,
  `char`) are passed/stored by value — modifying one inside the procedure
  never affects the caller. **Array parameters and array locals** also
  work now, with different semantics — see
  [Array parameters and local arrays](#array-parameters-and-local-arrays)
  below.
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
- **A procedure can call any procedure declared earlier** (or itself)
  without anything special. Calling one declared *later* requires a
  `forward` declaration first — see below.
- Not yet supported, with a clear compile error if attempted:
  `readln` into a parameter or local variable, and using one as a `for`
  loop's counter. Both work fine as ordinary expressions/assignments —
  just not in those two specific positions yet.
- A procedure's name and a variable's name share one namespace — you
  can't declare a procedure with the same name as an existing global
  variable, or vice versa.
- Functions (procedures that return a value) work — see
  [Functions](#functions) below.

### Forward declarations

```pascal
procedure isOdd(n: integer); forward;   { declare the header now, define it later }

procedure isEven(n: integer);
begin
    if n = 0 then
        writeln('even')
    else
        isOdd(n - 1);   { isOdd only exists as a forward declaration so far - that's fine }
end;

procedure isOdd;   { completing the forward declaration - no parameter list here }
begin
    if n = 0 then
        writeln('odd')
    else
        isEven(n - 1);
end;
```

- `procedure name(params); forward;` declares a procedure's name and
  parameter list without a body yet — this is what lets an *earlier*
  procedure call one that's only fully defined *later* in the file,
  which is exactly what enables mutual recursion (as in the `isEven`/
  `isOdd` example above, where each one calls the other).
- The completing definition — the one with the real body — repeats just
  `procedure name;`, with **no parameter list**. The parameters were
  already declared in the forward declaration; re-specifying them here is
  a compile error. Inside the completing body, parameters are referenced
  by the same names given in the forward declaration.
- Every forward declaration must eventually be completed somewhere later
  in the same file — an unfulfilled one (`forward;` with no matching
  definition anywhere) is a compile error.
- Calling a procedure that hasn't been declared *at all yet* — not even
  with `forward` — is still a compile error, exactly as without forward
  declarations. `forward` only moves *when* a name becomes callable
  earlier; it doesn't remove the requirement that it be declared in some
  form before it's used.

## Functions

```pascal
function factorial(n: integer): integer;
begin
    if n <= 1 then
        factorial := 1
    else
        factorial := n * factorial(n - 1);
end;

begin
    writeln(factorial(5));   { 120 }
end.
```

- `function name [(params)] : returnType; ...` — otherwise declared
  exactly like a procedure (parameters, local `var` sections, `forward`
  all work the same way; a `forward`-declared function's completing
  definition omits both the parameter list *and* the return type, since
  both were already given).
- **The return value is set by assigning to the function's own name**
  inside its body — `factorial := ...` above. This is the only way to
  set it; there's no separate `return`/`exit` statement. If a function's
  body never assigns to its own name, it returns a default value (`0`
  for `integer`/`boolean`/`char`-as-a-number, or an out-of-range value
  for `string`/`char` that will cleanly error if actually used — not
  silently wrong data).
- Reading the function's own name as an expression (to check the return
  value computed so far) isn't supported — only assigning to it is.
  Inside its own body, using the bare name as an expression is treated as
  a call (usually a recursive one), not a read of the stored result.
- **A function can be called as a statement**, discarding its return
  value, exactly like a procedure call:
  ```pascal
  bump;             { call bump, ignore what it returns }
  x := bump;         { call bump, use what it returns }
  ```
- The return type is scalar only (`integer`, `boolean`, `string`,
  `char`) — same restriction as parameters and local variables.
- A function's return type is checked at every call site used as an
  expression, the same way argument types are checked.

## Array parameters and local arrays

```pascal
program Example;
var
    scores: array[1..5] of integer;
    i, total: integer;

function sumOf(arr: array[1..5] of integer): integer;
var
    k, s: integer;
begin
    s := 0;
    k := 1;
    while k <= 5 do begin
        s := s + arr[k];
        k := k + 1;
    end;
    sumOf := s;
end;

begin
    for i := 1 to 5 do
        scores[i] := i * 10;
    total := sumOf(scores);
    writeln('total = ', total);   { 150 }
end.
```

### Array parameters

- Declared the same way as a scalar parameter, just with an array type:
  `procedure foo(arr: array[1..5] of integer);`.
- **Always by reference** — there's no by-value array parameter. Reading
  or writing `arr` inside the procedure reads or writes the *same*
  underlying array the caller passed; there's no copying.
- **The argument must be a bare array name**, not an expression, an
  array element, or an array-typed local temporary computed on the fly
  — `sumOf(scores)`, not `sumOf(scores[1])` or anything else. This can be
  the caller's own global array, the caller's own local array (see
  below), or the caller's own array parameter (passed straight through,
  common for utility procedures that forward an array to a helper).
- **The argument's declared bounds and element type must exactly match**
  the parameter's — passing `array[1..10] of integer` where
  `array[1..5] of integer` is expected is a compile-time error, even
  though both are integer arrays. There's no automatic resizing or
  conversion.
- A procedure/function can have any mix of scalar and array parameters,
  in any order.

### Local arrays

- Declared in a procedure's `var` section exactly like a global array:
  `var data: array[1..5] of integer;`.
- **Not per-call isolated, unlike scalar locals.** A local array is
  allocated once for the whole program and shared across every call to
  that procedure, including recursive ones. If a recursive procedure
  needs each call to have its own independent array, this doesn't
  provide that — only scalar locals and parameters do. In practice this
  matters only for recursive procedures that use a local array; a
  non-recursive procedure's local array behaves exactly as you'd expect.
- A runtime error for a local array (like an out-of-range index) reports
  a compiler-generated internal name rather than the name used in
  source, since that mapping only exists at compile time — the error
  still correctly identifies *that* something went out of range, just
  not by your own chosen name.
- A local array can itself be passed as an array-reference argument to
  another call, exactly like a global array can.

## Errors

Every compile error reports as `file:line: Compile Error: message` (or
`Type Error:` for a type-checking failure) and stops compilation. Runtime
errors from `solvm` report as `VM Runtime Error: message` and stop
execution. Both categories are deliberate, checked failures — there's no
undefined behavior reachable from valid or invalid Pascal source; a bug in
your program produces a clear error message, not a silent wrong answer or
a crash.

## What's not implemented

- Multi-dimensional arrays
- `real`/floating-point numbers
- `ord`/`chr` (now that user-defined functions exist, these could
  plausibly be added as builtins - just not done yet)
- Records, sets, enumerated types
- Units/modules/`uses`

See the project README's Status section for the current plan.
