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
    { record type declarations, type aliases, enumerated types, and/or
      subrange types, if any - see Records, Type aliases, Enumerated
      types, and Subrange types }
var
    { declarations }
begin
    { statements }
end.
```

- The program name is required but otherwise unused (no significance
  beyond documentation).
- The heading may optionally include a parenthesized list of
  identifiers, e.g. `program Name(input, output);` — standard Pascal's
  program-parameter list, traditionally naming the files/devices the
  program uses. This VM has no OS-level file-parameter binding, so the
  list is accepted purely as syntax and has no effect: any identifiers
  are allowed (not just `input`/`output`), and they're neither
  validated nor stored anywhere.
- The `const` section is optional, and comes before `type`/`var` — see
  [Constants](#constants).
- The `type` section is optional, and declares record types, type
  aliases, enumerated types, and/or subrange types (any mix, in any
  order) — see [Records](#records), [Type aliases](#type-aliases),
  [Enumerated types](#enumerated-types), and
  [Subrange types](#subrange-types).
- The `var` section is optional — omit it entirely if the program declares
  no variables.
- The final `.` after `end` is required.

## Comments

Three forms: block comments delimited by curly braces or by `(* *)`
(either may span multiple lines - neither nests), and `//` line
comments (run to end of line):

```pascal
{ this is a comment }
x := 1; { so is this }
{
  and this
}
(* this is also a comment, an alternate spelling of { } *)
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
| Enumerated | `type TName = (Val1, Val2, ...);` | — | User-defined; see [Enumerated types](#enumerated-types) |
| Subrange | `type TName = lower..upper;` | — | Bounds-checked `integer`; see [Subrange types](#subrange-types) |
| Set | `set of T` | `[1, 2, 3]`, `[]` | `T` is `integer`/`char`/`boolean`/an enumerated type, capped at 32 values; see [Sets](#sets) |
| Pointer | `type TName = ^Target;` | `nil` | `Target` is a scalar or record type; see [Pointers](#pointers) |

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
| `succ(x)` | function | `x + 1` — an integer, or an enumerated value (see [Enumerated types](#enumerated-types)) |
| `pred(x)` | function | `x - 1` — an integer, or an enumerated value |
| `inc(x)`, `inc(x, n)` | statement | Adds `1` (or `n`) to `x` in place — integer only, even for an enum (use `x := succ(x);` instead) |
| `dec(x)`, `dec(x, n)` | statement | Subtracts `1` (or `n`) from `x` in place — integer only, even for an enum (use `x := pred(x);` instead) |
| `ord(c)` | function | A `char`'s byte value, or an enumerated value's ordinal, as an integer — see [Char](#char) and [Enumerated types](#enumerated-types) |
| `chr(n)` | function | The `char` with byte value `n` — see [Char](#char) |
| `length(s)`, `s[i]` | function / indexing | String length and character access — see [String](#string) |
| `low(arr)`, `high(arr)`, `length(arr)` | functions | Array bounds and element count, resolved at compile time — see [Arrays](#arrays) |
| `copy`, `pos`, `mid`, `left`, `right`, `inpos` | functions | Substring extraction and searching — see [String](#string) |
| `upcase`, `uppercase`, `lowercase` | functions | Case conversion — see [String](#string) |
| `new(p)`, `dispose(p)` | statements | Allocate/release one instance of a pointer's target type — see [Pointers](#pointers) |

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

### `case` / `of`

```pascal
case x of
    1: writeln('one');
    2, 3: writeln('two or three');
else
    writeln('other: ', x);
end;
```

A multi-way branch on an ordinal value — `integer`, `char`, `boolean`, or
an enumerated type (not `real` or `string`, neither of which is
ordinal). The selector can be any expression of one of those types, not
just a bare variable. Each case label is a compile-time constant of the
same type as the selector: an (optionally negative) integer literal, a
one-character string or `#NNN` char-code literal, `true`/`false`, a
`const` reference, or a bare enumerated value name — not a general
expression, and not a range (`2..5:` isn't accepted; list the values
individually). A label list can name more than one value for the same
branch (`2, 3:` above). Case labels must be pairwise distinct across the
whole statement — a repeated label is a compile-time error.

`else` is optional, matching `if`/`then`. If the selector's value matches
no label and there's no `else`, it's a runtime error (this compiler
follows the common implementation choice here — a runtime error, not
undefined behavior). Each branch (and the `else` branch) is a single
statement, exactly like `if`/`then` — use `begin...end` for multiple
statements in a branch.

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

### `goto` and labels

```pascal
program GotoDemo;
label 1, 2;
var
    i: integer;
begin
    i := 1;
    1: writeln(i);        { a labelled statement - '1:' can be jumped to }
    i := i + 1;
    if i <= 3 then goto 1;    { jump back - prints 1, 2, 3 }
    goto 2;
    writeln('skipped');
    2: writeln('done');
end.
```

- Every label a block uses must be declared up front in a `label`
  section — a comma-separated list of unsigned integers, e.g.
  `label 1, 2, 100;`. Like `const`/`type`/`var`, at most one `label`
  section per block, and it must come first (before `const`/`type`/`var`
  and, in a procedure/function, before its own `var` section).
- `N: statement` attaches label `N` to a statement. Every declared label
  must label **exactly one** statement somewhere in its block — a
  declared-but-never-used label, or a label attached to two different
  statements, is a compile-time error.
- `goto N;` jumps to whichever statement label `N` marks — forward
  (a label appearing later in the source) or backward (earlier), either
  way with no restriction, since `label`/`goto` don't build a runtime
  loop at all - it's just an unconditional jump.
- **A label's scope is exactly one block** (the main program, or one
  procedure/function) — a `goto` can only target a label declared in the
  *same* block it appears in. A procedure's own `label`/`goto` are
  completely independent of the main program's and of every other
  procedure's; the same label number can be reused freely across blocks
  without conflict, and a `goto` can never jump into or out of a
  procedure.

#### What's not supported yet

- **This compiler doesn't check that a `goto` stays outside every
  structured statement (`if`/`while`/`for`/`case`/`with`) it doesn't
  already enclose** — standard Pascal forbids jumping directly into the
  middle of one of these from outside it; this compiler allows it (the
  jump just lands wherever the label's bytecode address is, skipping
  whatever came before it in that same body). A deliberate simplification
  — this is a programmer error this compiler won't catch, not a feature.
- **No empty statement** (`1: ;` as a do-nothing target) — this compiler
  doesn't support an empty statement anywhere, not just after a label, so
  a label meant to mark a fall-through/exit point needs a real trailing
  statement to attach to (e.g. a harmless `writeln` or assignment).

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
- An optional leading [file variable](#file-io) writes to that file
  instead of standard output: `write(f, x, y)`, `writeln(f)` (just a
  newline, to `f`).

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

### `read` and `readln`

```pascal
readln(n);      { integer }
readln(flag);   { boolean: must be exactly 0 or 1, or it's a runtime error }
readln(name);   { string: reads a full line }
readln(a, b, c); { multiple targets - reads three whitespace-separated
                   values, even across several lines if needed }
```

Each call prints a `> ` prompt, then reads from standard input. Reading
an `integer`, `real`, or `boolean` value doesn't itself consume a whole
line (`scanf`-style whitespace-delimited parsing) — it's `readln`,
specifically, that afterward also consumes the rest of that input line,
so a following `readln` of any type starts cleanly on the next line.
`read` is the same in every other respect, but *never* does that: after
`read(x)`, the rest of the current line — including any further
whitespace-separated values still on it — is left alone for a
subsequent `read` or `readln` to pick up.

```pascal
read(a);
read(b);
readln(c);     { reads a, b, c from the SAME line if they're on it -
                 only c's read flushes to the next line afterward }
```

**Multiple targets** in one call — `read(a, b, c)` / `readln(a, b,
c)` — work like calling `read`/`readln` on each target in turn, except
only the *last* one ever flushes to the next line: `readln(a, b, c)` is
exactly `read(a); read(b); readln(c);`, and `read(a, b, c)` is `read(a);
read(b); read(c);` (none of them flush). A single target behaves exactly
as before.

A non-last, non-string/char target (an `integer`/`real`/`boolean`,
which doesn't flush on its own) immediately followed by a `string`/
`char` target correctly continues on the next line, even though the
first target's own read left the line position sitting right before its
trailing newline rather than at the start of the next line — the
compiler detects this exact situation at compile time and skips that
one leftover newline first, so `readln(intVar, stringVar)` on two
separate lines works as expected. This is the only target-type
transition that needs it: every other pairing already lines up
correctly on its own (a numeric/boolean read's underlying `scanf` skips
leading whitespace on its own before parsing the next value; a
string/char read's `fgets` always consumes through its own line's
newline, so whatever follows it starts a fresh line naturally either
way).

An optional leading [file variable](#file-io) reads from that file
instead of standard input: `read(f, a, b)`, `readln(f, a)`.

A `read`/`readln` target can be a global, a parameter/local variable, a
`static` local, a record field (global or local, or a `with`-target's),
or the loop variable's own field — anything a plain assignment target
can be, except an array, a `var` parameter, or an array element (none of
those are supported yet):

```pascal
procedure greet;
var name: string;
begin
    readln(name);
    writeln('Hello, ', name);
end;
```

### `eof` and `eoln`

```pascal
while not eof do begin
    readln(x);
    writeln('got ', x);
end;
```

`eof` and `eoln` are boolean functions — usually written bare, with no
parentheses at all (`eof()`/`eoln()` also work, taking no argument,
meaning standard input — or `eof(f)`/`eoln(f)`, naming a [file
variable](#file-io) instead). Neither one consumes any input — they
only peek.

- **`eof`** is `true` once there's no more input left to read at all.
- **`eoln`** is `true` once the next character is the end of the current
  line (or there's no more input at all — matching every real Pascal
  implementation's convention that end-of-file also counts as
  end-of-line).

```pascal
read(a);
if eoln then writeln('nothing else on this line')
else writeln('more values follow on this line');
```

### `assert`

```pascal
assert(x > 0);
assert(x > 0, 'x must be positive');
```

- `assert(condition)` / `assert(condition, message)` — `condition` must
  be `boolean`; if it evaluates to `false` at runtime, execution stops
  immediately with `VM Runtime Error: message` (or `VM Runtime Error:
  Assertion failed` if no message was given). If `condition` is `true`,
  `assert` has no effect at all — it's not evaluated again or cached,
  just skipped.
- `message`, if given, must be `string` or `char`, and can be any
  expression (not just a literal) — it's evaluated every time `assert`
  runs, same as `condition`.
- Unlike a normal runtime error (array index out of range, division by
  zero, and so on), `assert` is a check *you* write — a way to state an
  invariant explicitly and have the compiler check it at exactly the
  point you expect it to hold, with a message describing what actually
  went wrong.

### Compound statements

`begin ... end` groups zero or more statements (semicolon-separated, the
last one's trailing `;` is optional) into a single statement — usable
anywhere a single statement is expected (loop/if bodies, or the whole
program body).

## File I/O

```pascal
var
    f: text;
    line: string;
begin
    assign(f, 'output.txt');
    rewrite(f);            { create/truncate for writing }
    writeln(f, 'Hello, file!');
    writeln(f, 42);
    close(f);

    assign(f, 'output.txt');
    reset(f);               { open for reading }
    while not eof(f) do begin
        readln(f, line);
        writeln('> ', line);
    end;
    close(f);
end.
```

A `text` file variable, declared with `var f: text;`, works with the
same `read`/`readln`/`write`/`writeln`/`eof`/`eoln` you already know —
just add it as the first argument (`write(f, x)` instead of `write(x)`)
to target that file instead of standard input/output. There's no
separate set of file-specific builtin names to learn.

- **`assign(f, name)`** binds a filename (a `string`/`char` expression)
  to `f`. Doesn't open anything yet — it just remembers the name for the
  next `reset`/`rewrite`.
- **`reset(f)`** opens the file `f` was last `assign`ed to, for
  *reading*. A Runtime Error if `f` was never `assign`ed, or if the file
  can't be opened (doesn't exist, no permission, ...).
- **`rewrite(f)`** opens it for *writing* — creates the file if it
  doesn't exist, **truncates it if it does** (matching real Pascal).
- **`close(f)`** closes it. A Runtime Error if `f` isn't currently open.
  Calling `reset`/`rewrite` again (on the same or a re-`assign`ed
  filename) doesn't require closing first — either one reopens `f`
  automatically, closing whatever was open before.
- **`read(f, ...)`/`readln(f, ...)`** — same syntax and semantics as
  plain `read`/`readln` (including multiple targets), just reading from
  `f` instead of standard input. No `"> "` prompt is printed — that's
  only for an interactive terminal.
- **`write(f, ...)`/`writeln(f, ...)`** — same as plain `write`/
  `writeln` (including field-width/precision, and printing an
  enumerated value by name), writing to `f` instead of standard output.
- **`eof(f)`/`eoln(f)`** — same as bare `eof`/`eoln`, checking `f`
  instead of standard input.

### What's not supported yet

- **A file variable can only be a GLOBAL variable** — not a parameter,
  local, record field, or array element. This is the one deliberate
  scope limitation the whole feature is built around (see below) —
  `procedure Foo(var f: text);` doesn't work; every procedure that
  touches a given file has to reference the same global variable
  directly.
- **`append`** (opening a file for appending rather than overwriting) —
  a later, non-ISO-7185 Pascal extension, out of scope for now.
  `rewrite` is the only way to open a file for writing, and it always
  truncates.
- **Only text files** — no `file of T` (typed/binary files), no
  `seek`/`filesize`/random access. Everything reads/writes as text,
  exactly like `read`/`write` on standard input/output already do.
- **A file variable can't be assigned, compared, or used with any other
  operator** (`f := g;`, `f = g`, ...) — standard Pascal doesn't define
  any of these for files either. A file's real state doesn't live in
  its own storage slot the way every other type's does (see below), so
  copying that slot wouldn't do anything meaningful anyway.

### How this is implemented

A file variable's real state (the underlying C `FILE*`, and the
filename `assign()` bound it to) lives in a fixed-size table indexed by
the variable's own symbol index — safe only because it's always global,
so that index never changes. No dynamic allocation, matching everything
else in this VM.

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
- A 2D array can also be a **parameter** (always by reference, exactly
  like a 1D array parameter) or a **local variable** — see [Array
  parameters and local arrays](#array-parameters-and-local-arrays),
  which covers 1D and 2D together.
- Stored row-major, in the same shared array-memory pool 1D arrays use.

### Three-or-more-dimensional arrays

```pascal
var
    cube: array[1..2, 1..2, 1..2] of integer;
    x, y, z, n: integer;
begin
    n := 0;
    for x := 1 to 2 do
        for y := 1 to 2 do
            for z := 1 to 2 do begin
                n := n + 1;
                cube[x, y, z] := n;
            end;
    writeln(cube[2, 2, 2]);   { 8 }
end.
```

- Same syntax as 2D, just with more comma-separated dimensions —
  `array[lo1..hi1, lo2..hi2, lo3..hi3, ...] of T`, indexed with
  `arr[i, j, k, ...]`. Up to 6 dimensions total.
- Works everywhere a 1D/2D array does: a global/local variable, or a
  **parameter** (always by reference — see [Array parameters and local
  arrays](#array-parameters-and-local-arrays)). A parameter's declared
  shape (element type, dimension count, and every dimension's bounds)
  must exactly match whatever array is passed at each call site, same
  as 1D/2D.
- Stored row-major in the same shared array-memory pool every array
  (1D, 2D, or more) draws from — a 3D array's element count is simply
  the product of all three dimensions' sizes.
- `low`/`high`/`length` don't support a multi-dimensional array (2D or
  more) — same pre-existing limitation 2D arrays already have.

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
- Alias names, record type names, enumerated type names, and subrange
  type names all share one namespace (declared in the same `type`
  section, in any order or mix) — redeclaring any of them as another is
  a compile-time error, same as redeclaring one as itself.
- A type alias can't itself name a record type (`type TWrapper =
  TPoint;` where `TPoint` is a record type is a compile-time error) —
  only the five scalar types, an enumerated type, a subrange type, and
  other aliases of those, are accepted (see [Enumerated
  types](#enumerated-types) and [Subrange types](#subrange-types)
  below). Aliasing a subrange type produces another, equivalent
  subrange type under the new name (its bounds are still enforced).

## Subrange types

```pascal
program Example;
type
    TAge = 0..150;
var
    age: TAge;
    scores: array[1..5] of TAge;
begin
    age := 36;
    writeln('age = ', age);
    age := 200;              { runtime error: out of range }
end.
```

- `type Name = <lower> .. <upper>;` declares a bounds-checked integer
  type: `lower`/`upper` are compile-time-constant integers (a literal,
  or a named [constant](#constants), same as an array bound), and
  `upper` must be `>= lower`.
- **Unlike an enumerated type, a subrange is fully assignment- and
  arithmetic-compatible with plain `integer`** — the type checker never
  distinguishes them, so `age: TAge` and `x: integer` mix freely in
  expressions, comparisons, and assignment in either direction with no
  widening step needed. The *only* thing a subrange type adds is a
  **runtime bounds check performed every time a value is stored** into
  a variable declared with it — assigning a value outside `lower..upper`
  is a runtime error (`VM Runtime Error`), not a silent wraparound or a
  compile-time rejection (the value very often isn't known until
  runtime).
- The bounds check applies everywhere a subrange type is used: plain
  variables, array elements (`scores[i] := 200;` above would also
  error), record fields, `inc`/`dec`, function parameters (checked at
  every call site) and return values (checked when the function's own
  name is assigned to, inside its body), and by-reference array
  parameters.
- **No range-checking on arithmetic itself** — `age + 1` (including via
  `succ`/`pred`) can produce an out-of-range *value*; the error only
  fires at the point that value is actually *stored* somewhere
  subrange-typed. An intermediate out-of-range value that's only ever
  read (e.g. printed) without being stored anywhere subrange-typed never
  errors.
- Implemented as a compile-time-only wrapper around the assigned value
  (`NODE_RANGE_CHECK` — see [docs/ARCHITECTURE.md](ARCHITECTURE.md)),
  compiling to two small new opcodes (`CHECK_LOWER`/`CHECK_UPPER`, see
  [docs/BYTECODE.md](BYTECODE.md)) — no `.bin` format change, since a
  subrange-typed variable's `Symbol` entry is otherwise indistinguishable
  from a plain `integer`'s (disassembling one shows `.var age integer`).

## Enumerated types

```pascal
program Example;
type
    TColor = (Red, Green, Blue);
var
    c: TColor;
begin
    c := Red;
    writeln('c = ', c);           { c = Red - printed by name, not ordinal }
    c := succ(c);
    writeln('c = ', c);           { c = Green }
    writeln('ord(c) = ', ord(c)); { ord(c) = 1 }
end.
```

- `type Name = (Val1, Val2, ...);` declares an ordered, named set of
  values — each value's ordinal is simply its position in the list,
  starting at `0`.
- Every value name is a usable expression in its own right (`Red`,
  `Green`, `Blue` above), typed as the enclosing enumerated type. Value
  names share one flat namespace across *every* enumerated type declared
  in the program (`type TColor = (Red, Green); TSize = (Small, Red);` is
  a compile-time error — `Red` can't be declared twice) — same as how
  it's a compile-time error for a value name to collide with an existing
  `const` name, and vice versa.
- **`ord`, `succ`, `pred`** all work on an enum value exactly as they do
  on an integer (see
  [Built-in functions and procedures](#built-in-functions-and-procedures)):
  `ord(c)` gives its ordinal, `succ(c)`/`pred(c)` give the next/previous
  value. `c + 1` / `c - 1` work directly too (this is exactly what
  `succ`/`pred` desugar to) — but no other arithmetic (`c * 2`, `c + c`,
  etc.) is accepted. **`inc`/`dec` don't work on an enum** (they're
  restricted to plain `integer` variables) — write `c := succ(c);`
  instead of `inc(c);`. **Neither `succ` nor `pred` range-checks** — calling
  `succ` on an enumerated type's last value (or `pred` on its first)
  doesn't raise an error; it just produces an ordinal that isn't any
  named value (see printing, below).
- **Comparison** (`=`, `<>`, `<`, `>`, `<=`, `>=`) works between two
  values of the *same* enumerated type, ordinally (`Red < Green < Blue`
  above). Comparing two *different* enumerated types (or an enum against
  a plain integer) is a compile-time error — unlike `integer`/`real`,
  there's no implicit widening between two different enum types, or
  between an enum and `integer`.
- **Assignment** requires an exact type match: `c := Red;` where `c: TColor`
  works; `c := 5;` (a plain integer) or assigning a value from a
  *different* declared enumerated type is a compile-time error. A value
  name itself can never be assigned to (`Red := Green;` is a compile-time
  error — the same restriction a `const` has).
- **`write`/`writeln` print an enum value by name**, not its bare ordinal
  (`writeln(Red)` prints `Red`) — a deliberate convenience beyond strict
  ISO Pascal (which doesn't define printing an enumerated type at all),
  matching Free Pascal/Delphi. Under the hood this compiles to a chain of
  compile-time-generated comparisons (no new opcode, no `.bin` format
  change — see [docs/ARCHITECTURE.md](ARCHITECTURE.md)), so bytecode size
  for a `writeln` of an enum grows with how many values its type has.
  **Field-width syntax on an enum argument (`writeln(c:10)`) falls back
  to printing the raw ordinal**, padded, rather than the name — a known,
  deliberately-scoped-out gap (see
  [docs/ROADMAP.md](ROADMAP.md)). An ordinal that doesn't correspond to
  any named value (only reachable via unchecked `succ`/`pred` past an
  enum's first/last value) also falls back to printing the raw ordinal,
  in both the padded and unpadded cases.
- Usable everywhere a scalar type is: variable declarations (plain or
  array element), record fields, parameters, procedure/function locals,
  function return types, and as the target of a [type
  alias](#type-aliases) (`type TShade = TColor;`).
- **`readln` into an enum-typed variable isn't supported** — there's no
  syntax for reading one back in by name, and reading a raw ordinal
  in unchecked would risk an out-of-range value with no corresponding
  name.
- **Array bounds can't reference an enum value** (`array[Red..Blue] of
  ...` doesn't work) — bounds stay integer-literal-or-integer-`const`
  only (see [Arrays](#arrays)); a `const` also can't be given an enum
  value from a type declared *after* it, since `const` is always parsed
  before `type` (see [Program structure](#program-structure)) — a
  `const` can only ever reference an earlier `const`.

## Sets

```pascal
var
    s: set of 0..9;
begin
    s := [1, 3, 5];        { a set constructor - literal elements }
    writeln(1 in s);       { TRUE }
    writeln(2 in s);       { FALSE }

    s := s + [7];          { union }
    s := s - [3];          { difference }
    s := [1, 2] * [2, 3];  { intersection - just {2} }

    s := [1..5];           { a range inside a constructor }
    s := [];                { the empty set }
end.
```

A set holds a collection of ordinal values from a small, fixed range —
represented internally as a single int, one bit per possible element (bit
K set means value K is a member), so **a set's base type is capped at 32
distinct values**. The base type, after `set of`, can be:

- An inline range: `set of 0..9`
- `boolean` (2 values)
- A previously-declared enumerated type: `set of TColor`
- A previously-declared subrange type (named, or a [type alias](#type-aliases) of one)

`char`, `string`, `real`, and a bare `integer` (unbounded) can't be a
set's base type — declaring one is a compile-time error, same as a base
range wider than 32 values.

### Set constructors

`[e1, e2, e3..e4, ...]` builds a set value. Each element can be any
expression of an ordinal type (`integer`, `boolean`, or an enumerated
value) — not just a literal:

```pascal
s := [x, y, 10];
```

A range (`a..b`) inside a constructor must have **compile-time-constant**
integer bounds (a literal, or a `const` reference) — this compiler
doesn't have a runtime loop primitive to build one from a variable range,
so it unrolls a constant range into its individual elements at compile
time instead. `[]` is the empty set.

An out-of-range element (negative, or 32 or higher) is a **runtime**
error, not a compile-time one, when it isn't a literal — the same check
that would reject an out-of-range `shl`/`shr` amount.

### Operators

| Operator | Meaning |
|---|---|
| `+` | Union |
| `-` | Difference |
| `*` | Intersection |
| `=` / `<>` | Equality / inequality |
| `<=` / `>=` | Subset-or-equal / superset-or-equal |
| `in` | Membership: `x in s` |

`<` and `>` are **not** defined for sets (matching standard Pascal) — use
`<=`/`>=` for subset/superset, or `=`/`<>` for equality. `x in s`
requires an ordinal `x` (`integer`, `boolean`, or an enumerated value)
and a set `s`.

A set can be a `var`/`const`/local/global variable, a by-value parameter,
a record field, or a function's return type. `write`/`writeln` can't
print a set directly — standard Pascal defines no textual representation
for one — and `readln` into a set isn't supported either.

### Iterating a set: `for x in s do`

```pascal
s := [1, 3, 5, 9];
for x in s do
    writeln(x);   { prints 1, 3, 5, 9 - ascending order, each member once }
```

`x` must be a plain `integer` variable (a global, local, parameter,
record field, or `static` local - anywhere an ordinary `for` loop
counter can be, except a `var` parameter, which no `for` loop counter
can be yet) - not `boolean` or an enumerated type, even when `s` was
declared as `set of boolean` or `set of TColor`. This isn't a
restriction chosen for its own sake: a set's bit position already *is*
the member's raw ordinal value (see above), and a set's declared base
type is discarded after declaration, so there's no record of "this
value came from `boolean`" or "this value came from `TColor`" to hand
back — a set of an enumerated type still yields plain integer ordinals
when iterated, matching whatever `ord()` of each member would give.

`s` is evaluated exactly **once**, before the loop starts (like a
`for` loop's own end-bound) — a set expression with a side effect (an
unusual thing to write, but possible if it calls a function) only runs
once, not once per candidate member.

No new bytecode is involved: this desugars entirely at compile time into
an ordinary `for x := 0 to 31 do` wrapping an `if x in s then ...` — the
same fixed 0..31 sweep `in`/set construction already use internally,
since 32 is the hard cap on a set's size (see above). This means a
`break`/`continue` inside the loop body works exactly as it would in any
other `for` loop.

### What's not supported yet

- **Combining two sets declared with different base types/ranges isn't
  checked** — `(set of 0..9) + (set of TColor)` is accepted, since both
  are just bitmasks under the hood; this compiler doesn't track which
  declared base type a given set value "belongs to" the way it does for
  enums. A deliberate simplification — mixing set shapes like this is a
  programmer error this compiler won't catch, not a feature.

A set works as a `var` parameter (mutated correctly through the
reference), and as an array element type (`array[1..3] of set of 0..9`),
in every position an ordinary scalar type can appear — there's nothing
set-specific to call out there.

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
  a 1D array of any of those, or another already-declared record type
  (see "Nested records" below) — anything a plain variable can be,
  except `array of RecordType`.
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

### Record comparison

```pascal
if p1 = p2 then
    writeln('same point');
if p1 <> p2 then
    writeln('different point');
```

`p1 = p2` / `p1 <> p2` (both plain record variables of the exact same
record type) compare every field pairwise, combined with `and` — `p1 =
p2` desugars to `(p1.f1 = p2.f1) and (p1.f2 = p2.f2) and ...`, and `<>`
is just that negated. Comparing two records of *different* record types
is a compile-time error (matching whole-record assignment's own
restriction), and — also matching whole-record assignment — a record
with an array field can't be compared at all (whole-array comparison
isn't supported, so there'd be no correct way to compare that field).

### The `with` statement

```pascal
with p do begin
    x := 1;
    y := 2;
end;
writeln(p.x, ' ', p.y);   { 1 2 }
```

Inside a `with recordVar do <statement>;` body, a bare identifier that
names one of `recordVar`'s fields resolves exactly as `recordVar.field`
would — usable for reading, assignment, `inc`/`dec`, `readln`, a `for`
loop counter, and `low`/`high`/`length` on an array field. Purely a
parser-time convenience — `with` doesn't change *how* a field is
resolved, only lets you skip writing `recordVar.` in front of it, so
there's no restriction here beyond what already applies to
`recordVar.field` written out explicitly.

- **`with` statements nest** (`with a do with b do ...`), and an inner
  `with`'s field shadows an outer one of the same name.
- **A `with`-target's field takes priority over everything else with the
  same name** — a local variable, parameter, or global with the same
  name as a field is shadowed for the duration of the `with` body. This
  matches classic Pascal behavior (and is a well-known source of subtle
  bugs in real Pascal code — a field name accidentally colliding with an
  outer variable silently redirects to the field instead).
- Only one record per `with` (`with a, b do ...`, real Pascal's
  multi-record form, isn't accepted) — nest two `with` statements
  instead (`with a do with b do ...`) for the same effect.
- The `with`-target must be a plain record variable; it can't be an
  expression.
- The `with`-target's record type can't have a nested-record field (see
  [Nested records](#nested-records)) — a bare identifier binds straight
  to a field's own storage, which isn't meaningful for a field that's a
  whole nested record; access it with `recordVar.field.subfield`
  instead.

### Record parameters and local records

```pascal
type
    TPoint = record
        x, y: integer;
    end;

function SumPlusOne(p: TPoint): integer;
var
    local: TPoint;
begin
    local.x := p.x + 1;
    local.y := p.y + 1;
    SumPlusOne := local.x + local.y;
end;
```

A record can be a parameter type or a local variable's type inside a
procedure/function, not just a global. Unlike a global record — whose
fields are hidden mangled globals — a local or parameter record's fields
each get their own ordinary frame slot, exactly as if you'd declared them
as separate scalar locals/parameters. That gives a local/parameter record
proper per-call isolation, including under recursion: two active calls
each get their own independent copy of the record, the same guarantee
every other local/parameter already has.

A record parameter is always **by value** — this compiler has no
by-reference mechanism for scalars at all (unlike array parameters, which
are always by reference), so passing a record copies every field at the
call site, and mutating a parameter's fields inside the procedure never
affects the caller's own record.

A local/parameter record works exactly like a global one everywhere else
— field access, whole-record assignment (`:=`), comparison (`=`/`<>`), a
field as a `for` loop counter, and `readln` into a field — and local and
global records of the same type can be freely mixed on either side of an
assignment or comparison (`localRec := globalRec;`, `if localRec =
globalRec then ...`).

### Records as array elements

```pascal
type
    TPoint = record
        x, y: integer;
    end;
var
    pts: array[1..10] of TPoint;
    p: TPoint;
begin
    pts[1].x := 5;
    pts[1].y := 10;
    pts[2] := pts[1];      { whole-element copy: array element <- array element }
    p := pts[2];            { plain record <- array element }
    pts[3] := p;             { array element <- plain record }
    writeln(pts[2].x);      { 5 }
end.
```

An array's element type can be a record type, for both a global array and
a procedure-local one (a local array of records reuses the same "hidden
global" storage trick a local scalar array already does — shared/
persistent across every call, including recursive ones). Read or write a
single field via a runtime index (`pts[i].x := 5;`), or copy a whole
element at once — the source (or destination) can be another array-of-
records element (same array or a different one, as long as it's the same
record type) or a plain record variable; whole-element copy desugars,
same as ordinary whole-record assignment, into one field-by-field
assignment per field, with the index evaluated exactly once regardless of
how many fields the record has.

Under the hood, each array element occupies as many contiguous storage
slots as the record type has fields (rather than the usual one slot per
element) — a genuinely runtime-addressable layout, unlike a plain
(non-array) record, which is pure parse-time sugar over one hidden global
per field. This is why a plain record and an array of records need two
different implementations even though they look similar on the surface:
a plain record's `p.field` is resolved entirely at parse time (there's no
runtime "which record" to select), but `people[i].age`'s `i` is a runtime
value, so the compiler can no longer resolve which storage location to
touch until the program actually runs.

### Nested records

```pascal
type
    TPoint = record
        x, y: integer;
    end;
    TRect = record
        topleft, bottomright: TPoint;
    end;
var
    r: TRect;
begin
    r.topleft.x := 0;
    r.topleft.y := 0;
    r.bottomright.x := 3;
    r.bottomright.y := 4;
    writeln(r.topleft.x, ' ', r.bottomright.y);   { 0 4 }
end.
```

A field's type can be another already-declared record type, and `.field`
chains drill in as far as needed (`r.topleft.x`, or three or more levels
deep). Whole-record assignment (`:=`), comparison (`=`/`<>`), and
by-value parameters/local variables all work the same way they do for a
plain record — a nested field is just more fields, copied/compared/
passed leaf by leaf.

Under the hood this stays pure parse-time flattening, recursively: a
nested field's own fields become more hidden globals (or frame slots),
mangled `outer__inner__leaf`, exactly as if every leaf had been declared
as its own separate field of the outer record — no new runtime
mechanism, no new bytecode. A record type can only nest an
already-declared type (record types can't forward-reference each other),
which is also what rules out self-reference (`TNode = record next:
TNode; end;` is a compile error — `TNode` isn't declared yet at the
point its own body would need to name it) and mutual recursion.

**What's not supported for a nested field:**

- **A record type with an array-typed field can't be used as a nested
  field's type** — and, transitively, nothing nested inside *that* type
  can have an array field either (since it could itself only be declared
  by nesting an already-array-field-free type). A record type with an
  array field still works as a plain variable, a parameter's argument,
  etc. — just not as another record's nested field.
- **`array of RecordType` isn't a valid field type** — a field can be a
  single nested record, or a 1D array of scalars, but not a 1D array of
  records.
- **A record type with a nested-record field can't be an array's element
  type** (see "Records as array elements" above), **a pointer's target
  type** (see "Pointers" below), or a `with` target (see "The `with`
  statement" above) — each of those mechanisms addresses a record's
  fields by a fixed per-field slot/offset, which doesn't generalize to a
  nested field's "N slots instead of 1" without a larger rethink.

### Variant records

```pascal
type
    TKind = (Circle, Rectangle);
    TShape = record
        case kind: TKind of
            Circle: (radius: real);
            Rectangle: (width, height: real);
    end;
var
    s: TShape;
begin
    s.kind := Circle;
    s.radius := 2.5;
    writeln('radius: ', s.radius);

    s.kind := Rectangle;
    s.width := 3.0;
    s.height := 4.0;
    writeln('area: ', s.width * s.height);
end.
```

A record's `case` — placed last, after any ordinary fields — declares a
tag field (`kind`, above) and one or more variants, each headed by a
label list (compile-time ordinal constants — literals, `const`s, or enum
values, exactly like a `case` *statement*'s labels) and a parenthesized
field list using the same field syntax ordinary fields use (scalar,
array, or nested-record). The tag field's type must be ordinal
(`integer`, `char`, `boolean`, or an already-declared enum type — not a
subrange), and every label's type must match it exactly. Labels must be
pairwise distinct across the whole `case`, and field names — the tag's,
and every variant's — must be unique across the *entire* record, the
same "no duplicate field name" rule plain fields already follow.

**How this is implemented — and what that means:** unlike real Pascal,
where a record's variants *overlap* in memory (only one variant's fields
are "live" per tag value, and the record's total size is capped at the
largest variant), this compiler's records already have no memory layout
of their own to overlap — see "How this is implemented" above: a record
variable is just N independent hidden globals/locals created at parse
time. Building genuine overlapping storage would mean inventing a real
addressing model for records from scratch, so variant records here stay
consistent with that existing design instead: the tag field and *every*
variant's fields are all ordinary, simultaneously-live fields (as if
every variant's fields had simply been declared as plain fields of the
same record) — the `case`/label syntax is checked and consumed at
compile time, but it doesn't restrict which fields you can read/write
when, and it doesn't reduce the record's total storage. This still
compiles and correctly runs the common use of variant records (modeling
"one of several shapes" of data, switching on the tag), which doesn't
rely on the underlying storage actually overlapping.

**What's not supported:**

- **No memory overlap or type-punning between variants** — see above;
  writing one variant's field then reading a *different* variant's field
  as a reinterpretation of the same bytes (a trick real Pascal
  implementations sometimes allow, though it's not portable there
  either) doesn't work here, since each field has its own independent
  storage.
- **No nested `case`** — a variant's field list can't itself contain
  another variant part. Standard Pascal allows nesting; out of scope
  here.
- **No anonymous/unnamed tag** (`case TKind of ...` without a field
  name) — only the named-tag form (`case kind: TKind of ...`) is
  supported.

### What's not supported yet

- **2D or N-D arrays of records** — only a 1D array of records is
  supported so far; `array[1..2, 1..2] of TPoint` is a compile error.
- **A record type with an array-typed field, used as an array's element
  type** — a record with an array field still works as a plain (non-array)
  variable, but using it as an array's element type is a compile error:
  each element would need a variable amount of storage per field, which
  this feature's fixed-size-per-element layout doesn't support.
- **Array-of-record parameters** — passing an array of records to a
  procedure/function is a compile error; array parameters need a
  by-reference mechanism this feature doesn't build yet. Work around it
  by copying into/out of a local array of records instead.
- **Passing a single array-of-records element directly as a by-value
  record argument** (`Foo(pts[i])` where `Foo(p: TPoint)`) — copy it into
  a plain record variable first (`p := pts[i]; Foo(p);`).
- **An array-typed field in a record parameter or local record** — a
  compile error for now (a global record's field CAN be an array). Only
  affects local/parameter records; a global record with an array field
  still works.
- **A `static` record local** — persisting a record's fields across calls
  the way a `static` scalar local does isn't supported yet.
- **`with` on a local or parameter record** — `with` still only accepts a
  global record variable; access a local/parameter record's fields with
  `recordVar.field` directly.
- **Nested records inside an array element type, a pointer target, or a
  `with` target** — see "Nested records" above for the full list of
  nesting-specific restrictions.

## Pointers

```pascal
type
    PNode = ^TNode;
    TNode = record
        data: integer;
        next: PNode;
    end;
var
    head, p: PNode;
begin
    head := nil;
    new(head);
    head^.data := 1;
    new(head^.next);
    head^.next^.data := 2;
    head^.next^.next := nil;

    p := head;
    while p <> nil do begin
        writeln(p^.data);
        p := p^.next;
    end;

    dispose(head^.next);
    dispose(head);
end.
```

A pointer type is declared with `^Target`, where `Target` is any scalar
type (`integer`, `boolean`, `char`, `real`, `string`, an enumerated
type, a subrange, or a type alias) or a record type. `Target` may be a
record type declared *later* in the same `type` section — the classic
self-referential pattern above (`PNode` targets `TNode`, which itself
has a field of type `PNode`) needs exactly this forward reference, and
it's resolved once the whole `type` section finishes parsing.

`new(p)` allocates one instance of `p`'s target type and points `p` at
it; `dispose(p)` releases it, making that storage available for a later
`new()` of the same target type to reuse. Both accept the same variety
of target `new`/`dispose` on a pointer field also work on
(`with`-target, record field, `var` parameter, plain local/global), and
`new` additionally accepts a pointer field reached through one or more
`^` dereferences (`new(head^.next);`), matching the worked example
above.

`p^` dereferences a pointer — `p^` alone if it targets a scalar type
(readable and writable directly, like `p^ := 5;`), or `p^.field` if it
targets a record. A chain of any length works: `p^.next^.next^.data`.
Dereferencing a nil pointer, or disposing one, is a runtime error, not
undefined behavior. Standard Pascal leaves a pointer's value undefined
immediately after `dispose` (not reliably nil) — this compiler matches
that: `dispose` never modifies the pointer variable itself.

`nil` is a literal usable anywhere a pointer value is expected: assigned
to a pointer variable, or compared with `=`/`<>` against a pointer of
any type. Two pointer variables (or a pointer and `nil`) can only be
compared with `=`/`<>` — no other operator is defined for pointers, and
comparing two *different* declared pointer types (even if they happen to
target the same thing) is a compile-time error, matching Pascal's usual
name-equivalence rule for pointer types. A pointer's value has no
textual representation — `write`/`writeln` and `readln` both reject it.

Under the hood, a pointer's runtime value is a plain int: `-1` for
`nil`, or an offset into a single fixed-size heap shared by every
pointer in the program (`vm_heap_mem[]` in `vm.c`) — this VM's only
source of genuinely dynamic allocation; every other memory region is
sized entirely at compile time. `dispose` doesn't just leak a freed
block — it links it onto a freelist bucketed by allocation size, so a
later `new()` targeting the same size reuses it instead of growing the
heap further.

**There is no garbage collector.** Losing every reference to an
allocated block — assigning its pointer `nil`, overwriting it with
another `new()`, or just letting the local variable that held it go out
of scope at `RET` — without first calling `dispose` on it does not
reclaim that memory. Nothing walks live pointers looking for orphaned
blocks; the heap only ever grows via `new` and only ever gets storage
back via a matching `dispose`, exactly like C's `malloc`/`free`. This
matches standard Pascal's own contract (`dispose` is always manual
there too) - it's mentioned here because the *consequence* is worth
knowing: a long-running program that leaks allocations in a loop will
eventually exhaust the heap and abort with a clean `VM Runtime Error`
(never a crash or silent corruption - see
[docs/BYTECODE.md](BYTECODE.md#memory-model)), but it genuinely can run
out. Always pair `new` with a `dispose` once
you're done with a block, the same discipline `malloc`/`free` demands.

### What's not supported yet

- **Pointer to an array** — `^array[1..10] of integer` is a compile
  error; a pointer may target a scalar or record type only.
- **Pointer to a pointer** (`^^integer`) — one level of indirection only.
- **A pointer-typed field of a *local or parameter* record, immediately
  dereferenced** (`myLocalRec.next^`) — works fine for a *global*
  record's field, or for a plain pointer variable (`p^`, `p^.next^`);
  assign the field to a plain pointer variable first as a workaround.
- **A pointer-typed field of an array-of-records element, immediately
  dereferenced** (`arr[i].next^`) — same workaround as above.
- **Whole-record assignment/comparison through a dereference**
  (`q^ := p^;`, `p^ = q^`) — assign/compare field by field instead.
- **A pointer targeting a record type that has a nested-record field**
  (see [Nested records](#nested-records)) — a compile error; pointers to
  a record work only when every field is a scalar or array.

## Classes

**v1 complete** — declaration parsing, `new`/`dispose`, field
read/write, method bodies, `c.Method(args)` call syntax (all 5 "Classes
and instances" build steps in `docs/ROADMAP.md`'s Phase 2), and single
inheritance all work. Early/static binding only, as scoped — see "Not
implemented yet" below and `notes/classes-and-instances-scoping.md` for
the full design rationale.

```pascal
type
    TCircle = class
        radius: real;
        procedure SetRadius(r: real);
        function Area: real;
    end;
var
    c: TCircle;

procedure TCircle.SetRadius;
begin
    self.radius := r;
end;

function TCircle.Area;
begin
    Area := 3.14159 * self.radius * self.radius;
end;

begin
    new(c);
    c.SetRadius(2.0);
    writeln('area: ', c.Area);
    dispose(c);
end.
```

A class declaration is reference-semantics, like Delphi/Java, not the
value-semantics `object` of old Turbo Pascal: `class TFoo ... end;`
declares fields (the same syntax and rules an ordinary record's fields
already use) and method headers (`procedure`/`function`, parsed the same
way a functional/procedural parameter's own inline signature already
is — scalar parameters only, by-value or `var`, no return-type/parameter
subranges), then registers `TFoo` itself directly as an implicit pointer
type targeting those fields. This means **every existing pointer
mechanism already works on a class variable unmodified** — `var c:
TCircle;`, passing `c` as an ordinary or `var` parameter, and
`new(c)`/`dispose(c)` all work today, exactly as they would for a
hand-written `type PCircle = ^TCircleRecord;`.

`c.field` compiles exactly like `p^.field` already does for a plain
pointer — the same field-offset resolution and the same
`OP_LOAD_HEAP_FIELD`/`OP_STORE_HEAP_FIELD` opcodes — just without
requiring the explicit `^` a plain pointer still needs. This is real
dereferencing, not sugar over a copy: `c.radius := 2.0;` writes through
`c`'s heap-allocated instance, `Bump(c)` (a `var TCircle` parameter)
mutates the caller's own instance, and dereferencing a `nil` or never-`new`'d
class variable is the same clean `VM Runtime Error` a plain pointer's
nil-dereference already is.

A method's **body** is declared separately from its header, after the
`var` section, using `procedure ClassName.MethodName; ... end;` /
`function ClassName.MethodName; ... end;` — note the parameter list and
return type are **not** repeated here (unlike Delphi): they're already
fully known from the header declared inside `class ... end;`, and
omitting them here matches this compiler's own existing convention for
completing a `forward`-declared procedure, rather than importing
Delphi's own convention. Inside the body:

- **`self`** is an ordinary parameter (always the method's first,
  hidden one) of the class's own type — read/write its fields via
  `self.field`, exactly like any other class-typed variable. There's no
  unqualified `field` shorthand yet (see "Not implemented yet" below).
- The method's own declared parameters (`r` above) are ordinary named
  locals, usable directly.
- A `function` method sets its return value the same way an ordinary
  function does — assign to the method's own (short, unqualified) name,
  e.g. `Area := ...;`.
- A method body may have its own `label`/`var` sections and local
  variables, exactly like an ordinary procedure/function body.

Under the hood, a method's body is registered as an ordinary top-level
procedure under a **mangled name** (`TCircle__SetRadius`) — the same
trick record fields/static locals/nested-record leaves already use,
needed because every procedure in this compiler shares one flat,
whole-program namespace with no per-class scoping or overloading. Two
different classes can each declare a method with the same name (e.g.
both `TCircle` and `TSquare` declaring their own `Area`) with no
collision. You can still call a method's real, mangled name directly,
passing `self` explicitly (`TCircle__SetRadius(c, 2.0)`) — exactly what
`c.SetRadius(2.0)` itself desugars into.

**`c.Method(args)`** resolves `c`, checks the name against the class's
declared methods (checked after fields — a name can't be both), and
builds an ordinary call to the mangled procedure with `c` spliced in as
the hidden first (`self`) argument. The parenthesized argument list is
optional when the method takes none, matching how an ordinary
parameterless function/procedure call already works
(`c.Bump;` as a bare statement, no `()`). A `function` method's call can
be used as a value anywhere an expression is expected
(`writeln(c.Area)`); a `procedure` method's call can only be a
statement, exactly like any other procedure — using one as a value is a
compile-time error, same message an ordinary procedure-used-as-a-value
already gets. A method call's result can't itself be chained into a
further `.field`/`^` step yet (a known gap, below).

### Inheritance

```pascal
type
    TShape = class
        name: integer;
        function Area: real;
    end;
    TCircle = class(TShape)
        radius: real;
        function Area: real;   { overrides TShape's own }
    end;
var
    c: TCircle;
    s: TShape;

function TShape.Area;
begin
    Area := 0.0;
end;

function TCircle.Area;
begin
    Area := 3.14159 * self.radius * self.radius;
end;

begin
    new(c);
    c.name := 1;             { an inherited field }
    c.radius := 2.0;
    writeln(c.Area);         { TCircle's own override }

    s := c;                  { upcast: a subclass instance assigned to
                                an ancestor-typed variable }
    writeln(s.name);
    dispose(c);
end.
```

`class TCircle(TShape) ... end;` declares `TCircle` as a subclass of
`TShape`. Inheritance is fully **flattened at declaration time**, not a
live relationship resolved later:

- Every field `TShape` has (and, transitively, everything **it**
  inherits) is copied into `TCircle`'s own field list, in order, before
  `TCircle`'s own new fields are added — a descendant's fields always
  start with an exact copy of its ancestor's own layout. A field name
  can never be overridden, only added; colliding with an inherited name
  is the same duplicate-field error as colliding with any other field.
- Every method header `TShape` has is likewise copied into `TCircle`'s
  own method list. `TCircle` can then either leave it alone (a plain
  **inherited** method — calling it through a `TCircle` instance
  dispatches to `TShape`'s own implementation, unchanged) or redeclare
  it with the identical signature inside its own `class ... end;` body
  (an **override** — a mismatched signature, or overriding the same
  method twice, is a compile-time error). A method's body can only be
  given for a header the class declares itself — writing
  `function TCircle.Area;` without first redeclaring `Area` in
  `TCircle`'s own header list (to override it) is rejected, even though
  `TCircle` inherits `Area` from `TShape`.
- **A subclass instance can be used anywhere its ancestor is expected**
  — assigned to an ancestor-typed variable, passed as an ordinary
  by-value parameter, compared with `=`/`<>` against an ancestor-typed
  value, or (this is what makes calling an *inherited* method work at
  all) passed as `self` to a method whose own declared type is an
  ancestor class. The one exception: a `var` parameter never widens for
  a class upcast either, matching this compiler's existing "a `var`
  argument is never implicitly widened" rule for every other type.
- Both inherited-vs-overridden method dispatch and the upcast
  compatibility above are resolved **statically**, from the accessing
  expression's own declared type at compile time — there's no vtable
  and no runtime type tag, consistent with early/static binding only
  (see "Not implemented yet" below).
- Multiple levels of inheritance work the same way, recursively — each
  class's own field/method lists are already fully flattened by the
  time a further subclass inherits from it.

**Not implemented yet:**

- **Unqualified field access inside a method body** (`radius := r;`
  instead of `self.radius := r;`) — always requires `self.` for now.
- **Chaining off a method call's result** (`c.GetOther().field`) — a
  method call is always the terminal step of an access chain.
- **Nested procedure/function declarations inside a method body** — a
  method body doesn't support its own nested subroutines yet, unlike an
  ordinary procedure.
- **No check that every declared method header actually gets a body** —
  calling a body-less method just fails with an ordinary "unknown
  procedure" error; there's no earlier, clearer check yet (unlike a
  genuine `forward`-declared procedure, which is checked).
- **Array or nested-record (composition) fields** — a class's fields
  must be scalar for now, the same restriction a local/parameter record
  has today.
- **Virtual/dynamic dispatch, constructors, multiple inheritance, and
  visibility (`private`/`public`)** — none of these exist even as a plan
  yet beyond the scoping note; single inheritance with early/static
  binding only, as scoped.

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
  argument — unless it's declared `var`, which passes it by reference
  instead; see [`var` parameters](#var-parameters) below.
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

### Nested procedures and functions

```pascal
procedure outer;
var
    total: integer;

    procedure addUp(n: integer);
    procedure step(k: integer);
    begin
        total := total + k;   { outer's own local, not step's/addUp's }
    end;
    begin
        if n > 0 then begin
            step(n);
            addUp(n - 1);
        end;
    end;

begin
    total := 0;
    addUp(3);
    writeln(total);   { 6 }
end;

begin
    outer;
end.
```

- A procedure or function may be declared **inside** another
  procedure/function's own declaration section (after its `var` section,
  before its `begin`), at any nesting depth. The nested body can read
  and write **any enclosing procedure's own locals and parameters** —
  not just its immediate parent's, as `step` reaching all the way up
  through `addUp` to `outer`'s own `total` above demonstrates — matching
  standard Pascal lexical scoping.
- A nested local **shadows** a same-named local in an enclosing
  procedure, exactly like a procedure's own local already shadows a
  global — the enclosing one is simply inaccessible by that name from
  inside the nested body, standard Pascal scoping. A duplicate name
  *within the same* declaration section is still rejected, as always.
- Unlike standard Pascal, a nested procedure/function's name is **not**
  restricted to being called only from inside its lexically enclosing
  procedure — it shares the same single, whole-program namespace every
  top-level procedure already does (this is also how `forward` and
  mutual recursion between procedures already work). Calling a nested
  procedure from somewhere its lexical parent isn't currently active
  compiles fine; it's only a **runtime error** if that call actually
  goes on to touch one of the (inaccessible) enclosing locals — see
  [Errors](#errors) below.
- A local **array** or **`static` local** declared in an enclosing
  procedure works the same way from a nested body, at zero extra cost —
  both are already implemented as global storage under the hood (see
  [Local arrays](#local-arrays) and [Static local
  variables](#static-local-variables) below), so nothing new is needed
  to reach one from a nested procedure.
- Recursion works the same for a nested procedure as a top-level one,
  including a nested procedure calling *itself* while still reaching an
  enclosing local correctly on every recursive call.

#### What's not supported yet

- **A `for` loop counter, or a `readln` target, that's an enclosing
  procedure's local** — both must be one of the current procedure's own
  locals. Use one of this procedure's own locals as the counter/target
  instead, and (for `readln`) assign it to the outer one afterward.
  (Standard Pascal requires a `for` loop counter be local to the
  enclosing block anyway, so this is a narrow, defensible restriction.)
- **Whole-record `var`-parameter forwarding, or an array element as a
  `var` argument, through an enclosing scope** — already-existing gaps
  (see [`var` parameters](#var-parameters) below) that nesting doesn't
  lift; a nested procedure inherits whatever restrictions its enclosing
  one already has.
- Nesting deeper than 16 levels is a compile-time error (`'name' is
  nested too deeply`) — a generous, arbitrary limit, not a language rule.

### Static local variables

```pascal
function counter: integer;
var
    static n: integer;
begin
    inc(n);
    counter := n;
end;

begin
    writeln(counter);   { 1 }
    writeln(counter);   { 2 }
    writeln(counter);   { 3 }
end.
```

- `static name: type;` in a procedure/function's `var` section declares
  a local that **persists across calls**, unlike an ordinary local
  (which starts fresh — effectively zeroed — on every call, including
  recursive ones). A `static` local is initialized to `0` (or the
  equivalent for its type) once, the first time the program runs, then
  simply keeps whatever value it was last assigned.
- **Recursive calls share the same static local** — unlike an ordinary
  local, which gets its own independent copy per call (see
  [Recursion](#procedures) above), a `static` local behaves like a
  single, unqualified global that only this procedure can see by that
  name.
- Two different procedures can each have their own `static` local with
  the same name (e.g. two counters both named `n`) without colliding —
  each is a distinct piece of storage.
- **Doesn't apply to arrays** — a local array is already shared across
  every call by default (see [Local arrays](#local-arrays)), so `static`
  on an array declaration is a compile-time error; only a plain scalar
  local can be `static`.
- Works everywhere an ordinary scalar local does: `inc`/`dec`, `readln`,
  a `for` loop counter, and a [subrange](#subrange-types)-typed static
  is bounds-checked exactly like any other subrange-typed storage.

### `var` parameters

```pascal
procedure inc10(var x: integer);
begin
    x := x + 10;
end;

procedure swap(var a, b: integer);
var
    temp: integer;
begin
    temp := a;
    a := b;
    b := temp;
end;

var
    n: integer;
    p, q: integer;
begin
    n := 5;
    inc10(n);
    writeln(n);      { 15 }

    p := 1;
    q := 2;
    swap(p, q);
    writeln(p, ' ', q);  { 2 1 }
end.
```

`var name: type` in a parameter list passes that parameter **by
reference** — assigning to it inside the procedure writes straight
through to the caller's own variable, instead of a private copy. This is
the general mechanism real Pascal's `inc`/`dec` are built on; in this
compiler `inc`/`dec` still desugar directly to `x := x + 1` rather than
routing through it (see
[Built-in functions and procedures](#built-in-functions-and-procedures)),
but a `var` parameter now works everywhere else a real Pascal one would.

- Works for every scalar type: `integer`, `real`, `boolean`, `char`,
  `string`, an enumerated type, or a subrange (bounds-checked on every
  write, using the *parameter's* own declared bounds — see
  [Subrange types](#subrange-types)).
- **The argument must be a variable** — a global, a local/parameter of
  the caller, a `static` local, a record field (global or local, or a
  `with`-target's field), or the caller's own `var` parameter (forwarded
  straight through, unchanged) — never a general expression like `x +
  1`, matching real Pascal.
- **The argument's type must exactly match** the parameter's declared
  type — unlike a by-value argument, a `var` argument is never
  automatically widened (an `integer` variable can't be passed to a `var
  x: real` parameter).
- `var` mixes freely with ordinary by-value parameters and array
  parameters in the same parameter list
  (`procedure foo(a: integer; var b: integer; c: array[1..3] of integer)`).
- Writing `var` before an **array** parameter is accepted but redundant —
  an array parameter is already always by reference (see
  [Array parameters and local arrays](#array-parameters-and-local-arrays)),
  with or without it.
- **Not supported yet**: a whole **record** as a `var` parameter (a
  record *field* works fine, though — see the example above), an
  **array element** as a `var` argument (`swap(arr[1], arr[2])`), `readln`
  into a `var` parameter, and using a `var` parameter as a `for` loop's
  counter. Each is a clear compile-time error rather than a silent wrong
  answer.

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

### Functional/procedural parameters

```pascal
function Square(n: integer): integer;
begin
    Square := n * n;
end;

function Cube(n: integer): integer;
begin
    Cube := n * n * n;
end;

function Apply(function f(n: integer): integer; v: integer): integer;
begin
    Apply := f(v);
end;

begin
    writeln(Apply(Square, 5));  { 25 }
    writeln(Apply(Cube, 3));    { 27 }
end.
```

A procedure or function may itself be a formal parameter, written out
inline exactly like a real declaration's own header — standard ISO 7185
Pascal's functional/procedural parameters. The actual argument at a call
site is a procedure/function's bare name (`Apply(Square, 5)`, not
`Apply(Square(5))`); it's called from inside the receiving procedure
just like any other procedure/function (`f(v)` above).

- **The actual argument must be a top-level (non-nested) procedure/
  function.** Passing a nested one by name is a compile-time error. A
  procedural/functional parameter is *not* a closure — it carries no
  captured environment of its own, just a plain runtime code address
  (see [docs/BYTECODE.md](BYTECODE.md#memory-model)), which is exactly
  why only a top-level procedure/function (never needing one) qualifies.
  An already-received procedural parameter *can* be forwarded straight
  through to a further call taking the same signature — it's still
  provably top-level, by construction.
- **The actual argument's signature must match exactly**: same
  is_function-ness (a `function` parameter needs a function argument, a
  `procedure` parameter needs a procedure), same return type if a
  function, and the same parameter count/types/`var`-ness in order — no
  implicit widening, matching this compiler's existing `var`-argument
  rule.
- **A procedural/functional parameter's own inline signature is scalar
  parameters only for now** — by-value and `var`, no arrays or records
  (a documented gap, not a silent one; the same restriction the actual
  argument's own parameter list must then also satisfy).
- **No other use is supported**: a procedural/functional parameter can
  only be called, or forwarded as another procedural/functional
  argument — no `=`/`<>` comparison, no assignment to a plain variable,
  no storing in an array/record field, no `write`/`writeln`.
- **No named, storable procedural type** — the signature is always
  written out inline at the declaration site, matching standard Pascal.
  A `type TProc = procedure(x: integer);`-style named type is a
  possible later, non-standard extension (see
  [docs/ROADMAP.md](../docs/ROADMAP.md)'s Procedural types item), not
  this feature.

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
- **2D (and 3-or-more-dimensional) array parameters work too**
  (`procedure foo(arr: array[1..3, 1..3] of integer);`), with the same
  by-reference semantics and exact-bounds-match requirement — including
  every dimension. `low`/`high`/`length` still don't support a multi-
  dimensional array (parameter or global), matching [Two-dimensional
  arrays](#two-dimensional-arrays).

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
- **2D (and 3-or-more-dimensional) local arrays work too** (`var grid:
  array[1..3, 1..3] of integer;`), with the same "shared across every
  call, not per-call isolated" behavior as a 1D local array.

## Errors

Every compile error reports as `file:line: Compile Error: message` (or
`Type Error:` for a type-checking failure) and stops compilation. Runtime
errors from `solvm` report as `VM Runtime Error: message` and stop
execution. Both categories are deliberate, checked failures — there's no
undefined behavior reachable from valid or invalid Pascal source; a bug in
your program produces a clear error message, not a silent wrong answer or
a crash.

## Warnings

Unlike an error, a `file:line: Warning: message` **doesn't** stop
compilation — it's a heuristic diagnostic about code that compiles and
runs fine, but might be a mistake. Currently the only warning is an
uninitialized-variable check, run once per procedure/function:

```pascal
function Average(a, b: integer): integer;
begin
    { forgot: Average := (a + b) div 2; }
end;
```
```
file.pas:1: Warning: function 'Average' never assigns a value to its own name - it will always return an undefined value
```

- Flags a local variable that's **read but never assigned a value
  anywhere** in its own procedure/function body, and a function that
  **never assigns to its own name** anywhere in its body (so it can
  never return a meaningful value).
- Deliberately **flow-insensitive**: it only asks "is this ever
  assigned, anywhere in this body" — not "is it assigned on every path
  that reaches this read". `if cond then x := 1; writeln(x);` is **not**
  flagged, even though `x` is only actually assigned when `cond` is
  true. A precise, path-aware version would need to correctly model
  every statement kind's control flow (`if`/`while`/`for`/`repeat`/`case`
  merge points, `break`/`continue`, and this compiler's unrestricted
  `goto`) — real complexity, with real risk of false-warning noise on
  correct code. This simpler check only ever *under*-warns; it never
  incorrectly warns about valid code.
- Scope is deliberately narrow — only plain scalar locals of one
  procedure/function body:
  - Not parameters (always initialized by the caller) or `var`
    parameters (a valid reference regardless of what it points to).
  - Not `static` locals — they persist across calls, so reading the
    implicit zero on the first call is often exactly the point.
  - Not arrays — per-element initialization isn't tracked.
  - **Not global variables at all, including the main program's own
    top-level `var` section.** Telling "already initialized by an
    earlier procedure call" from "genuinely never initialized" needs
    whole-program analysis this pass doesn't attempt — so, unlike most
    other diagnostics in this compiler, an uninitialized *global* simply
    isn't caught at all yet.

## What's not implemented

- Pointer to an array or to another pointer, and a few narrower pointer-
  dereference gaps — see [Pointers](#pointers) above (pointer to a
  scalar or record, including the self-referential linked-list/tree
  pattern, `new`/`dispose`/`nil`, all work)
- 2D/N-D arrays of records, array-of-record parameters, an array-typed
  field in a record parameter/local record, and a nested-record field
  used as an array's element type or a pointer's target type — see
  [Records as array elements](#records-as-array-elements),
  [Nested records](#nested-records), and [Records](#records) above (1D
  arrays of records, plain record variables (including nested-record
  fields), record parameters/locals, and record comparison all work)
- Units/modules/`uses`

See the project README's Status section and
[docs/ROADMAP.md](ROADMAP.md) for the current plan.
