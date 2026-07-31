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
const
    { named constants, if any - see Constants }
type
    { record type declarations and/or type aliases, if any - see Records
      and Type aliases }
var
    { declarations }
begin
    { statements }
end.
```

- The program name is required but otherwise unused (no significance
  beyond documentation).
- The `const` section is optional, and comes before `type`/`var` — see
  [Constants](#constants).
- The `type` section is optional, and declares record types and/or type
  aliases (any mix of the two, in any order) — see
  [Records](#records) and [Type aliases](#type-aliases).
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
| Real | `real` | `3.14`, `2.0`, `1.5e10` | 32-bit float — see [Real](#real) below |
| Boolean | `boolean` | `true`, `false` | `write`/`writeln` print it as `TRUE`/`FALSE` |
| String | `string` | `'hello'`, `'it''s here'` | Doubled `''` is an escaped literal quote |
| Char | `char` | `'a'`, `'!'`, `'x'` | See [Char](#char) below - a single-quoted literal of length exactly 1 |
| Array | `array[lower..upper] of T` | — | `T` is `integer`, `real`, `boolean`, `string`, or `char`; see [Arrays](#arrays) |
| Record | `type TName = record ... end;` | — | User-defined; see [Records](#records) |

There are no enumerated types or sets.

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

## Constants

```pascal
program Example;
const
    MaxScore = 100;
    Pi2 = 3.14159 * 2;
    Greeting = 'Hello';
    Enabled = true;
var
    scores: array[1..MaxScore] of integer;
begin
    writeln(Greeting, ', area factor = ', Pi2:0:2);
end.
```

- A `const` section, if present, comes right after `program Name;` and
  before `type`/`var` (see [Program structure](#program-structure)).
- Each constant's value must be a compile-time-constant expression —
  arithmetic and comparisons on integer/real literals (`+ - * / div mod
  and or xor shl shr ** = <> < > <= >=`, including unary `-`/`not`),
  `pi`, and the built-in math functions all fold at compile time, exactly
  as they do anywhere else in an expression (see
  [Operators](#operators)). A constant can also just be a single string,
  char, or boolean literal — but **string/char values cannot be built
  from `+` concatenation**, since constant folding doesn't fold string
  operations (only arithmetic/logical ones) — `const Greeting = 'Hello'
  + ', World';` is a compile-time error ("not a compile-time constant
  expression"), even though the same expression is fine as an ordinary
  runtime computation elsewhere in a program.
- A constant's expression can reference an **earlier** constant in the
  same `const` section (`const A = 10; B = A * 2;`) — it can't reference
  a variable or call a function/procedure, since nothing else has been
  declared yet at the point `const` is parsed.
- A constant has no storage of any kind — there's no `Symbol` for it, no
  `vm_vars[]` slot, and it never appears in a disassembly listing. Every
  reference to it compiles to exactly the same bytecode as if you'd
  written its value inline (a plain `PUSH`/`PUSH_STR`) — see
  [docs/ARCHITECTURE.md](ARCHITECTURE.md) for why this is the same
  "resolve entirely at compile time" approach [Records](#records) use.
- A constant can't be reassigned (`MaxScore := 5;` is a compile-time
  error), and a constant's name shares the same namespace as variables
  and procedures/functions — redeclaring it as any of those (or vice
  versa) is a compile-time error.
- **An integer constant can be used as an array bound**
  (`array[1..MaxScore] of integer`, including both dimensions of a 2D
  array) — the one place outside an ordinary expression a constant is
  accepted, since array bounds must already be compile-time-constant
  integers in this language (see [Arrays](#arrays)).

## Literals

- **Integers**: a run of digits, e.g. `123`. A leading `-` is handled by
  the unary minus operator, not the literal itself.
- **Booleans**: the keywords `true` and `false`.
- **Strings**: single-quoted, e.g. `'hello'`. A literal single quote
  inside a string is written as two consecutive quotes: `'it''s a test'`
  produces `it's a test`. String literals cannot span multiple lines.
  Maximum length is 255 characters.
- **Char codes**: `#` followed by a decimal number `1..255`, e.g. `#65`
  (the same character as `'A'`) — see [Char](#char).
- **Reals**: digits, a decimal point, and more digits (`3.14`, `2.0` -
  the decimal point is required, `2` alone is an integer), with an
  optional exponent (`1.5e10`, `2.5E-3`). See [Real](#real).

## Operators

### Arithmetic

| Operator | Meaning |
|---|---|
| `+` | Addition, or string concatenation if both operands are strings |
| `-` | Subtraction |
| `*` | Multiplication |
| `/` | **Real** division — always produces a `real`, even for two `integer` operands (`5 / 2` is `2.5`, not `2`) |
| `div` | Integer division (truncating) — `integer` operands only |
| `mod` | Modulo — `integer` operands only |
| `shl` | Bitwise shift left (integer, logical - see below) |
| `shr` | Bitwise shift right (integer, logical - see below) |

`+`, `-`, and `*` accept `integer` or `real` operands (or a mix — see
[Real](#real) for the widening rules); `div`, `mod`, `shl`, `shr` are
integer-only, with no exception for `real` operands, even via widening.
Division or modulo by a literal zero is a compile-time error; by a
runtime-zero value is a runtime error. Both are caught, never silently
produce garbage. `shl`/`shr`'s shift amount must be `0..31`; anything
else is a runtime error rather than the undefined behavior C's `<<`/`>>`
would give for an out-of-range shift.

### Comparison

| Operator | Meaning | Works on |
|---|---|---|
| `=` | Equal | integer, real, boolean, string, char |
| `<>` | Not equal | integer, real, boolean, string, char |
| `<`, `>`, `<=`, `>=` | Ordering | integer, real, boolean, string, char |

String/char equality compares the actual characters, not identity.
String/char ordering is lexicographic (character-by-character, like
`strcmp`) — e.g. `'apple' < 'banana'` is `true`. Boolean is ordinal
(`false < true`), matching standard Pascal. Comparing an `integer` and a
`real` widens the integer first, same as arithmetic (see
[Real](#real)).

### Logical / bitwise (`and`, `or`, `xor`, `not`)

| Operator | Between two `boolean`s | Between two `integer`s |
|---|---|---|
| `and` | Logical AND | Bitwise AND |
| `or` | Logical OR | Bitwise OR |
| `xor` | Logical XOR | Bitwise XOR |
| `not` | Logical NOT (unary) | Bitwise NOT (unary, ones' complement) |

Same four keywords, different meaning depending on operand type — this
matches Turbo Pascal/Delphi/Free Pascal (plain ISO Pascal doesn't define
`and`/`or`/`xor`/`not` on integers at all). Mixing a `boolean` and an
`integer` operand is a compile-time error; both operands must be the
same one of the two. There's no short-circuit evaluation — both operands
of `and`/`or` are always evaluated.

### Precedence (highest to lowest)

1. Unary `-`, `not`, parenthesized expressions
2. `**` (right-associative — `2 ** 3 ** 2` is `2 ** (3 ** 2)` = `512`)
3. `*`, `/`, `div`, `mod`, `and`, `shl`, `shr`
4. `+`, `-`, `or`, `xor`
5. `=`, `<>`, `<`, `>`, `<=`, `>=`

Assignment (`:=`) is a statement, not an expression — you can't write
`x := (y := 5)`.

## Built-in functions and procedures

| Name | Kind | Meaning |
|---|---|---|
| `abs(x)` | function | Absolute value — `integer` or `real` |
| `sqr(x)` | function | `x * x` — `integer` or `real` |
| `sqrt`, `sin`, `cos`, `arctan`, `exp`, `ln` | functions | The rest of ISO Pascal's math functions — `integer` or `real` in, always `real` out — see [Real](#real) |
| `pi` | constant | `3.14159265358979...`, usable directly as a value — see [Real](#real) |
| `power(base, exp)`, `**` | functions/operator | Exponentiation — see [Real](#real) |
| `odd(x)` | function | `true` if the integer `x` is odd |
| `succ(x)` | function | `x + 1`, for an integer |
| `pred(x)` | function | `x - 1`, for an integer |
| `inc(x)`, `inc(x, n)` | statement | Adds `1` (or `n`) to `x` in place |
| `dec(x)`, `dec(x, n)` | statement | Subtracts `1` (or `n`) from `x` in place |
| `ord(c)` | function | A `char`'s byte value, as an integer — see [Char](#char) |
| `chr(n)` | function | The `char` with byte value `n` — see [Char](#char) |
| `length(s)`, `s[i]` | function / indexing | String length and character access — see [String](#string) |
| `low(arr)`, `high(arr)`, `length(arr)` | functions | Array bounds and element count, resolved at compile time — see [Arrays](#arrays) |
| `copy`, `pos`, `mid`, `left`, `right`, `inpos` | functions | Substring extraction and searching — see [String](#string) |
| `upcase`, `uppercase`, `lowercase` | functions | Case conversion — see [String](#string) |

- `abs`/`sqr` accept `integer` or `real` (preserving whichever was
  given); `odd`/`succ`/`pred`/`inc`/`dec` work on `integer` only;
  `ord`/`chr` are the `char`/`integer` conversion pair.
- `inc`/`dec`'s target `x` must be a plain integer variable — global or
  local, but not an array element. Real Pascal's `inc`/`dec` mutate their
  argument by reference (`var` parameter); this compiler doesn't support
  by-reference *scalar* parameters yet (only array parameters are by
  reference), so `inc`/`dec` are handled as a special statement form
  rather than a general mechanism — `inc(x)` compiles to exactly what
  `x := x + 1;` would. One consequence: dead-code elimination never
  removes an `inc`/`dec` on an otherwise-unused global, since `x := x +
  1;`'s own right-hand side reads `x`, which always makes it look used —
  purely a missed optimization (the program still runs correctly), not
  a correctness issue.
- `inc`/`dec` are statements (no return value, can't be used inside an
  expression), matching real Pascal. The other five are ordinary
  functions, usable anywhere an expression is expected.

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
- The loop variable can be a global, or a parameter/local variable of a
  procedure or function:

  ```pascal
  function sumTo(n: integer): integer;
  var i, total: integer;
  begin
      total := 0;
      for i := 1 to n do
          total := total + i;
      sumTo := total;
  end;
  ```

  A local loop variable gets the same per-call isolation any other local
  does — including the end bound's cached value, so a recursive function
  with its own `for` loop works correctly no matter how deep the
  recursion goes; each call's loop is fully independent of every other
  active call's.

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

writeln('val: ', 42, ' ok=', true);   { "val: 42 ok=TRUE" }

writeln;              { just a newline }
writeln();             { same thing }
```

- Both take zero or more comma-separated arguments of any mixed type
  (`integer`, `real`, `boolean`, `string`, `char`).
- Arguments are printed back-to-back with **no separator** — include your
  own spaces in string literals if you want them.
- `writeln` appends exactly one trailing newline, after all arguments.
  `write` never appends one.
- Booleans print as `TRUE`/`FALSE`.
- Parentheses are optional when there are no arguments.

#### Field width and precision

Any argument can be followed by `:width` or `:width:precision`
(standard Pascal field-width syntax) to control its formatting:

```pascal
writeln('[', 42:5, ']');           { [   42] - right-justified to width 5 }
writeln('[', price:0:2, ']');       { [19.90] - exactly 2 decimal places }
writeln('[', price:10:2, ']');      { [     19.90] - padded to width 10 too }
```

- `width` justifies the value to *at least* that many characters, padded
  with spaces — works for every type. If the value's own text is already
  wider than `width`, nothing is truncated; the full value prints
  regardless.
  - **`width >= 0`** right-justifies (the standard, common form).
  - **`width < 0`** left-justifies instead, padding to `|width|`
    characters — some Pascal dialects' convention for the same syntax
    (plain ISO Pascal doesn't define it, but it's common enough to be
    worth supporting): `writeln('[', 42:-5, ']')` prints `[42   ]`.
- `precision` (the second, optional colon) fixes the number of digits
  after the decimal point — **`real` values only**, and requires `width`
  to already be present (`x:0:2`, not `x::2`). Using it on anything else
  is a compile-time error. Without `precision`, a `real` still uses this
  compiler's usual default format (see [Real](#real)), just padded.
- `width` and `precision` can be arbitrary `integer` expressions, not
  just literals — `writeln(x:column_width)` works.
- `precision`, when given, must be `0..20` — beyond `20` digits doesn't
  mean anything for a 32-bit float anyway. Out of that range is a
  runtime error, not silently clamped or garbled output.

### `readln`

```pascal
readln(n);      { integer }
readln(flag);   { boolean: must be exactly 0 or 1, or it's a runtime error }
readln(name);   { string: reads a full line }
```

Each call prints a `> ` prompt, then reads from standard input. Reading an
integer or boolean also consumes the rest of that input line (so a
following `readln` of any type starts cleanly on the next line).

`readln`'s target can be a global, or a parameter/local variable:

```pascal
procedure greet;
var name: string;
begin
    readln(name);
    writeln('Hello, ', name);
end;
```

`readln` into an array (global or local) isn't supported — only a plain
scalar variable.

### Compound statements

`begin ... end` groups zero or more statements (semicolon-separated, the
last one's trailing `;` is optional) into a single statement — usable
anywhere a single statement is expected (loop/if bodies, or the whole
program body).

## Real

```pascal
var
    price: real;
    quantity: integer;
    total: real;
begin
    price := 2.5;
    quantity := 3;
    total := price * quantity;   { integer widened to real automatically }
    writeln('total: ', total);

    writeln('5 / 2 = ', 5 / 2);           { 2.5 - '/' always produces real }
    writeln('trunc(total) = ', trunc(total));
    writeln('round(2.6) = ', round(2.6));
end.
```

### Representation

`real` is a 32-bit IEEE-754 float (C's `float`), not a 64-bit `double` —
this is a deliberate trade-off, not an oversight. Every storage slot in
this VM (the operand stack, variables, array elements, frame slots) is a
single 4-byte `int`; a `float` fits in that same slot with no changes
needed to any of those storage arrays. A `double` wouldn't fit in one
slot and would force every one of them to become "1 or 2 slots depending
on type" — a much larger, riskier change for comparatively little
practical benefit in a language without any of this scale. The real
consequence: roughly 7 significant decimal digits of precision, not 15-16
— there's real historical precedent for this exact choice (Turbo
Pascal's original `real` on FPU-less hardware).

### Widening and narrowing

- **Integer → real is implicit** ("widening"), anywhere Pascal allows it:
  mixed arithmetic (`2 + 3.5`), assignment (`x: real; x := 5;`), and
  passing an integer argument to a `real` parameter.
- **Real → integer is never implicit** ("narrowing") — `i: integer; i :=
  3.5;` is a compile-time error. Use `trunc`/`round` (below) to convert
  explicitly. This matches standard Pascal: silently truncating a real
  on assignment would silently lose information, so it's not automatic.
- `div`, `mod`, `shl`, `shr` are **integer-only**, with no exception for
  `real` operands even via widening — these don't have a `real` meaning
  in Pascal at all.

### `/` always produces `real`

Unlike most of this compiler's other decisions, this one is a genuine
**behavior change**: `/` used to be an alias for `div` (integer
division). Now that `real` exists, `/` matches actual Pascal semantics
— **always** floating-point division, even for two `integer` operands
(`5 / 2` is `2.5`, not `2`). `div` remains the truncating
integer-division operator, unchanged.

### `trunc` and `round`

- `trunc(x)` — `x`'s integer part, truncated toward zero (`trunc(3.7) =
  3`, `trunc(-3.7) = -3`).
- `round(x)` — `x` rounded to the nearest integer, half away from zero
  (`round(3.5) = 4`, `round(-3.5) = -4`).
- Both require a `real` argument and always succeed — there's no
  "value too large" runtime error, unlike most conversions in this VM.

### Printing

`write`/`writeln` print a `real` with `%.6g` by default — 6 significant
digits, matching a 32-bit float's actual precision, switching to
scientific notation only for very large or very small magnitudes.
Field-width/precision syntax (`writeln(x:10:2)`) is supported and is the
more common way to control this — see [`write` and
`writeln`](#write-and-writeln).

### Math functions, `pi`, and exponentiation

```pascal
var radius, area: real;
begin
    radius := 3.0;
    area := pi * sqr(radius);
    writeln('area: ', area:0:2);          { area: 28.27 }

    writeln('sqrt(2): ', sqrt(2):0:5);     { 1.41421 - integer arg widens }
    writeln('2 ** 10: ', 2 ** 10);         { 1024 }
    writeln('power(2, 0.5): ', power(2, 0.5):0:5); { 1.41421, same as sqrt(2) }
end.
```

- `sqrt`, `sin`, `cos`, `arctan`, `exp`, `ln` — the rest of ISO Pascal's
  original math function set (`abs`/`sqr` are the other two — see
  [Built-in functions and procedures](#built-in-functions-and-procedures)).
  Each accepts `integer` or `real` (an integer argument is widened
  automatically, unlike `trunc`/`round` which require `real` strictly)
  and always returns `real`.
- `pi` — the constant `3.14159265358979...`, usable directly as a value
  (`writeln(pi)`, `2 * pi`) rather than as a function call — it costs
  nothing at runtime, the same as `true`/`false`.
- `power(base, exp)` and **`**`** compute the same thing (`base` raised
  to `exp`) — pick whichever reads better. Neither is standard ISO
  Pascal (which has no exponentiation at all — `^` is Pascal's
  *pointer*-dereference operator, not exponentiation), but `power` is
  how Free Pascal's math library spells this, and `**` is a common
  extension in modern dialects, so both are provided. Both accept
  `integer` or `real` for either operand (widening as needed) and always
  return `real`, even `2 ** 10`.
- **`**`'s precedence**: tighter-binding than `*`/`/`/`div`/`mod`, and
  right-associative (`2 ** 3 ** 2` is `2 ** (3 ** 2)` = `512`, not
  `(2 ** 3) ** 2` = `64`) — see [Precedence](#precedence-highest-to-lowest).
  It binds *less* tightly than unary `-`, so `-2 ** 2` is `(-2) ** 2` =
  `4`, not `-(2 ** 2)` = `-4`. Standard Pascal has no exponentiation
  operator to match a precedent from, so this is simply this compiler's
  own chosen rule.
- **Domain errors**: rather than a separate precondition check specific
  to each function (`sqrt` needs a non-negative argument, `ln` needs a
  positive one, `power` needs a valid base/exponent combination, and so
  on), every one of these checks its *result* isn't NaN or infinite
  after computing it, erroring cleanly if so — `sqrt(-1.0)`, `ln(0.0)`,
  and `power(-4.0, 0.5)` are all runtime errors this same way, rather
  than silently producing `nan`/`inf` that only becomes visible (and
  hard to trace back) wherever it's eventually printed. If every operand
  is a constant, this is caught at compile time instead — same as
  division by zero.



- `real` array **parameters** and **local** arrays actually do work
  (they're generic over element type already) - what's *not* supported
  is 2D arrays of any element type, `real` included (see
  [Two-dimensional arrays](#two-dimensional-arrays)).

## String

```pascal
var
    name: string;
    initial: char;
begin
    name := 'Ada Lovelace';
    writeln('length: ', length(name));
    writeln('first char: ', name[1]);
    writeln('uppercase: ', uppercase(name));
    writeln('surname: ', copy(name, pos(' ', name) + 1, length(name)));
    initial := upcase(name[1]);
    writeln('initial: ', initial);
end.
```

### `length` and indexing

- `length(s)` — the number of characters, as an `integer`.
- `s[i]` — the character at 1-based position `i`, as a `char`. Only
  works on a plain `string`/`char` *variable* (global or local) — not on
  an array element, a function's result, or any other expression; this
  matches how array indexing is also restricted to a plain array name in
  this language, not general expressions.
- Unlike `copy` below, **indexing is strict**: `i` outside `1..length(s)`
  is a runtime error, not a clamped or empty result. This mirrors real
  Pascal, where indexing and `copy` genuinely have different bounds
  behavior.
- `s[i] := val` — assigns a single character (`val` must be exactly one
  character, checked at runtime the same way any other char-typed
  storage is) at 1-based position `i`, also strict like the read side
  (`i` outside `1..length(s)` is a runtime error). Works on a plain
  `string`/`char` variable, global or local — same restriction as
  reading `s[i]`.

  This compiler's strings are deduplicated in a shared pool (see
  [docs/BYTECODE.md](BYTECODE.md)), so two variables holding equal
  string values can be the *same* underlying entry — naively mutating a
  character in place would corrupt every other variable sharing it.
  `s[i] := val` avoids this by copy-on-write under the hood: it builds
  an entirely new string with that one character replaced, interns it
  (reusing an existing pool entry if one already matches), and only then
  points `s` at the result — `s` and any other variable that happened to
  share its old value are never mutated in place, only ever reassigned
  to point somewhere new. For example:

  ```pascal
  var s, t: string;
  begin
      s := 'Hello';
      t := 'Hello';   { t shares the same pooled entry as s }
      s[1] := 'J';
      writeln(s);      { Jello }
      writeln(t);      { Hello - unaffected }
  end.
  ```

### `copy`, `pos`, and the BASIC-style extras

| Function | Meaning |
|---|---|
| `copy(s, start, count)` | The substring starting at 1-based `start`, up to `count` characters |
| `pos(needle, s)` | 1-based position of `needle` in `s`, or `0` if not found |
| `mid(s, start, count)` | Same as `copy` — a more BASIC-flavored name for the same operation |
| `left(s, n)` | The first `n` characters of `s` |
| `right(s, n)` | The last `n` characters of `s` |
| `inpos(needle, s)` | Same as `pos` — finds a character (or substring) inside `s` |

- **`copy`/`mid`/`left`/`right` are lenient, never runtime errors**: an
  out-of-range `start` or a `count` that runs past the end of the string
  just yields as much of the string as actually exists (possibly an
  empty string) — this matches real Pascal's `copy`, which is
  deliberately forgiving so patterns like "give me the last 10
  characters, or the whole string if it's shorter" don't need a bounds
  check first.
- `mid`, `left`, `right`, and `inpos` aren't ISO Pascal or even standard
  Turbo Pascal/Delphi — they're BASIC-style names included as
  convenience aliases/equivalents of `copy`/`pos`. `length`, `copy`,
  `pos`, and `upcase` (below) are the ISO-standard ones.
- `pos`/`inpos` with an empty needle is defined as "not found" (`0`),
  avoiding the ambiguous question of what position an empty string
  would be "found at."

### Case conversion

- `upcase(c)` — ISO standard. Takes a single `char` (or a `string`
  that's exactly one character, via the usual `char`/`string` interop),
  returns the uppercased `char`, or the same value unchanged if it isn't
  a lowercase letter.
- `uppercase(s)` / `lowercase(s)` — whole-`string` case conversion.
  These aren't ISO standard, but are standard in Turbo Pascal/Delphi/Free
  Pascal. Only affects `a`-`z`/`A`-`Z`; anything else in the string is
  left as-is.

## Char

```pascal
var
    grade: char;
begin
    grade := 'A';
    writeln(grade);
    writeln('ordinal value: ', ord(grade));   { 65 }
    grade := chr(ord(grade) + 1);
    writeln('next letter: ', grade);           { B }
    writeln('newline via char code: ', 1, #10, 2);
```

`char` is implemented as a `string` that's constrained to hold exactly
one character; a `char` value comes from any single-quoted literal (or
string expression) that happens to be exactly one character long, or
from a dedicated `#NNN` char-code literal (see below). This means:

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

### `ord` and `chr`

- `ord(c)` — the character's byte value, as an `integer` (`0..255`).
  Accepts anything `char`/`string`-typed, but requires the actual value
  to be exactly one character, checked at runtime the same way
  assignment into a `char` is.
- `chr(n)` — the character with byte value `n`, as a `char`. `n` must be
  `1..255` — **`chr(0)` is a runtime error**, not `chr(0) = ''` or a NUL
  character: this compiler's strings are ordinary null-terminated C
  strings under the hood, so a "one-character string containing byte 0"
  isn't representable at all.
- `ord`/`chr` are exact inverses over `1..255`: `chr(ord(c)) = c`, and
  `ord(chr(n)) = n`.

### `#NNN` char-code literals

```pascal
writeln('Hello' + #10 + 'World');   { Hello, then a newline, then World }
c := #65;                            { same as c := 'A'; }
```

- `#` followed by a decimal number (`1..255`, same restriction as `chr`
  and for the same reason) is a char literal denoting the character
  with that byte value — standard Turbo Pascal/Delphi/Free Pascal syntax
  (plain ISO Pascal doesn't have it). `#0` and anything above `#255` are
  compile-time errors, since the value is always known at compile time.
- Unlike a quoted literal like `'A'` — which is `string`-typed, and only
  usable as a `char` through the general `char`/`string` interop rules
  above — `#65` is `char`-typed from the moment it's parsed. In practice
  this rarely matters (the two interoperate freely everywhere), but it's
  what makes `#NNN` a genuinely distinct literal form rather than just
  another way to write a string.
- Real Pascal allows adjacent literals to run together with no `+`
  needed (`'Hello'#13#10'World'` as a single literal). This compiler
  doesn't — join them explicitly with `+`, as in the example above.

## Arrays

```pascal
var
    scores: array[1..10] of integer;
    flags: array[0..3] of boolean;
    names: array[-5..5] of string;
```

- Bounds are compile-time-constant integers (a literal, e.g.
  `array[-5..5]`, or a named [constant](#constants), e.g.
  `array[1..MaxScore]`); they can't be variables or general expressions.
- `upper` must be `>= lower`.
- Indexing (`scores[i]`) requires the index expression to be `integer`,
  and it's bounds-checked **at runtime** — an out-of-range index is a
  runtime error, not undefined behavior.
- An array reference always needs an index; there's no whole-array
  assignment or whole-array printing.
- Total storage across every array declared in one program is capped at
  4096 elements combined (shared across every array, 1D and 2D alike).

### `low`, `high`, `length`

```pascal
var
    scores: array[-2..7] of integer;
    i: integer;
begin
    for i := low(scores) to high(scores) do
        scores[i] := i * i;
    writeln(length(scores));   { 10 }
```

- `low(arr)` / `high(arr)` — the array's declared lower/upper bound.
- `length(arr)` — the number of elements (`high - low + 1`). This is the
  same `length` used for strings (see [String](#string)) — it works out
  which one you mean from the argument.
- Takes a **plain array name**, or a `record`'s array field
  (`low(p.scores)`) — not an indexed element, and not any other
  expression — global, local, an array-reference parameter (using the
  parameter's own declared bounds, which are guaranteed to match
  whatever's actually passed), or a field of a global `record` variable:

  ```pascal
  type TStudent = record scores: array[1..5] of integer; end;
  var s: TStudent; i: integer;
  begin
      for i := low(s.scores) to high(s.scores) do
          s.scores[i] := i * i;
      writeln(length(s.scores));   { 5 }
  ```
- Because array bounds are always compile-time-constant in this
  language, all three resolve to a plain constant at compile time — a
  literal, with no runtime cost and no reference to the array's actual
  contents. `for i := low(arr) to high(arr) do` compiles exactly as if
  you'd written the bounds by hand.
- **1D arrays only for now** — calling any of these on a 2D array (or a
  2D array field) is a compile-time error, since there's no defined
  answer yet for "which dimension."

### Two-dimensional arrays

```pascal
var
    board: array[1..3, 1..3] of char;
    row, col: integer;
begin
    for row := 1 to 3 do
        for col := 1 to 3 do
            board[row, col] := '.';
    board[2, 2] := 'X';

    for row := 1 to 3 do begin
        for col := 1 to 3 do
            write(board[row, col]);
        writeln;
    end;
end.
```

- `array[lower1..upper1, lower2..upper2] of T` — each dimension has its
  own independent bounds (and its own bounds check at runtime).
- Indexed with `arr[i, j]`, matching the declaration syntax — not the
  chained `arr[i][j]` form some other languages use.
- **Exactly two dimensions** — there's no three-or-more-dimensional
  array. This isn't an oversight: a 2D array *write* needs three
  sub-expressions (two indices plus a value), which is the most this
  compiler's AST nodes can hold without growing every node in the
  compiler just for this one case. Nest a 1D array of a 2D array's rows,
  or just use a 1D array with manual index arithmetic, if you need more
  dimensions.
- **Global variables only** — no 2D array parameters and no 2D local
  arrays yet (1D arrays support both; 2D doesn't yet, matching how 1D
  arrays themselves were global-only before parameters/locals became
  their own feature).
- Stored row-major, in the same shared array-memory pool 1D arrays use.

## Type aliases

```pascal
program Example;
type
    TAge = integer;
    TYears = TAge;
var
    age: TAge;
    scores: array[1..5] of TAge;

function nextAge(a: TAge): TAge;
begin
    nextAge := a + 1;
end;

begin
    age := 30;
    writeln('nextAge(30) = ', nextAge(age));
end.
```

- `type Name = <existing scalar type>;` declares `Name` as another name
  for `integer`, `real`, `boolean`, `string`, or `char` — usable
  everywhere the aliased type is: variable declarations (plain or
  array-element), record fields, parameters, procedure/function locals,
  and function return types.
- An alias can itself alias an earlier alias (`TYears = TAge;` above) —
  chains resolve all the way down to one of the five built-in types.
- A type alias has no runtime representation of its own — it's resolved
  entirely at parse time to whichever built-in type it names, exactly
  like how [records](#records) resolve to hidden globals. Two variables
  declared through different aliases of the same underlying type (e.g.
  `age: TAge` and `x: integer`) are completely interchangeable — the
  alias is a compile-time name only, not a distinct type the type
  checker tracks separately.
- Alias names and record type names share one namespace (declared in the
  same `type` section, in any order or mix) — redeclaring either as the
  other is a compile-time error, same as redeclaring either as itself.
- A type alias can't itself name a record type (`type TWrapper =
  TPoint;` where `TPoint` is a record type is a compile-time error) —
  only the five scalar types and other aliases of them are accepted.

## Records

```pascal
type
    TPoint = record
        x, y: integer;
    end;
var
    origin, p: TPoint;
begin
    origin.x := 0;
    origin.y := 0;

    p.x := 3;
    p.y := 4;
    writeln('p = (', p.x, ', ', p.y, ')');

    p := origin;   { whole-record assignment - copies every field }
    writeln('p after p := origin: (', p.x, ', ', p.y, ')');
end.
```

### The `type` section

A `type` section, if present, comes after `program Name;` and before
`var` (standard Pascal ordering). It holds one or more record type
declarations:

```pascal
type
    TPerson = record
        age: integer;
        height: real;
        name: string;
    end;
```

- A field's type can be `integer`, `real`, `boolean`, `string`, `char`,
  or a 1D array of any of those — anything a plain variable can be,
  except another record (no nesting yet - see below).
- Multiple field names sharing one type can be comma-separated, same as
  `var` (`x, y: integer;`).
- There's no type aliasing (`type TAge = integer;`) — `type` only
  declares record types.

### How this is implemented

A record variable doesn't get one storage location of its own at all.
`var p: TPerson;` actually creates one ordinary hidden global variable
per field (internally named `p__age`, `p__height`, `p__name`, and so
on), and `p.age` just resolves, at compile time, to a reference to that
hidden variable — indistinguishable, from that point on, from any other
plain global. This is the same trick this compiler already uses for
local arrays (see [Array parameters and local arrays](#array-parameters-and-local-arrays)).

The payoff: field access, type checking, dead-code elimination, and the
`-v` diagnostic dump all already work per-variable, so they all work for
record fields too, with no new runtime mechanism at all — there isn't a
single new bytecode instruction anywhere in this feature. You can see
this directly by disassembling any program using records: field
references show up as ordinary `.var p__age integer` declarations and
`LOAD`/`STORE p__age` instructions, exactly as if you'd declared `p__age`
as a separate global yourself.

The cost of this approach is that it doesn't generalize to anything
needing a genuinely runtime-selectable record — see "What's not
supported yet" below.

A record field works as a target anywhere a plain variable does —
`inc`/`dec`, `readln`, and a `for` loop's counter all accept `c.n` the
same as a bare variable, since by the time any of these resolve their
target, `c.n` already *is* an ordinary global reference:

```pascal
c.n := 0;
inc(c.n);
for c.n := 1 to 3 do
    writeln('counting: ', c.n);
readln(c.n);
```

### Whole-record assignment

`p2 := p1;` (no `.field`, both plain record variables of the *same*
record type) copies every field individually — this compiler has no
single "copy this whole record" operation, or need for one, since a
record isn't one runtime value to begin with, just several ordinary
variables under generated names. The two sides must be declared with the
exact same record type (structurally identical but differently-named
record types are not interchangeable).

If any field is an array, whole-record assignment is a compile-time
error instead: this compiler doesn't support whole-array assignment at
all (see [Arrays](#arrays)), so there'd be no correct way to copy that
field as part of copying the whole record. Copy such fields (and any
others) individually instead.

### What's not supported yet

- **Records as array elements, or an array as a field's own bounds
  varying per record** — the whole "mangled hidden global per field"
  approach assumes exactly one, statically-known storage location per
  field. `people[i].age`, where `i` is a runtime value, doesn't fit that
  model — it would need a genuine addressing scheme, similar to how
  arrays themselves work, not just sugar over existing globals.
- **Record parameters or local records** — matching how arrays
  themselves started global-only before parameters/locals became their
  own feature.
- **Nested records** — a field can't itself be a record type.
- **Record comparison** (`p1 = p2`) — not defined. Using a record
  variable directly anywhere other than `.field` access or whole-record
  assignment (`writeln(p)`, `p1 = p2`, passing one as a procedure
  argument, and so on) is a clear compile-time error rather than a
  confusing parser failure.

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

- Three-or-more-dimensional arrays, and array parameters/locals for 2D
  arrays specifically (1D supports both already) — see
  [Two-dimensional arrays](#two-dimensional-arrays) above
- Records as array elements, record parameters/locals, nested records,
  and record comparison (plain record variables and whole-record
  assignment work — see [Records](#records) above)
- Sets, enumerated types
- Units/modules/`uses`

See the project README's Status section for the current plan.
