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
- **`const`/`type`/`var` can repeat and interleave freely**, in any
  order, as many times as needed — Delphi's more permissive style,
  rather than standard/ISO Pascal's fixed single-occurrence order:
  ```pascal
  const
      MaxScore = 100;
  type
      TScores = record
          values: array[1..MaxScore] of integer;
      end;
  var
      s: TScores;
  const
      DefaultValue = 0;
  begin
      s.values[1] := DefaultValue;
  end.
  ```
  `TScores`'s array field reaches back to `MaxScore` (an earlier
  `const`), `s`'s declaration reaches back to `TScores` (an earlier
  `type`), and a *second* `const` block — something no single-occurrence
  section could ever do — appears after `var`.
  Ordering still matters — whatever's declared **before** a given point
  in the source is visible to it, whatever comes after isn't, exactly
  like every other declare-before-use rule in this language. What's
  different from before is that visibility is no longer tied to which
  section *keyword* introduced something: a `type` can now use an
  earlier `const` (for an array bound) and a later `const`/`var` can, in
  turn, reference that `type` (or vice versa — whichever order the
  actual dependency needs), all in the same declaration part. See
  [Constants](#constants), [Records](#records),
  [Type aliases](#type-aliases), [Enumerated types](#enumerated-types),
  and [Subrange types](#subrange-types) for what each section can
  declare.
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

## Compiler directives

A `{$...}` block — a curly-brace comment whose first character is `$` —
is a compiler directive, not an ordinary comment:

```pascal
{$DEFINE DEBUG}
...
{$IFDEF DEBUG}
    writeln('debug build');
{$ELSE}
    writeln('release build');
{$ENDIF}
```

- **`{$DEFINE symbol}`** adds `symbol` to the set of defined symbols for
  the rest of the compile; **`{$UNDEF symbol}`** removes it.
  `{$DEFINE}`ing an already-defined symbol is harmless, not an error.
- **`{$IFDEF symbol} ... {$ENDIF}`** includes the enclosed source only
  if `symbol` is currently defined; **`{$IFNDEF symbol} ... {$ENDIF}`**
  is the inverse — included only if `symbol` is *not* defined. An
  optional **`{$ELSE}`** splits either into a true-branch/false-branch
  pair, exactly one of which is ever included.
- **The excluded branch is never lexed, not just skipped at run time** —
  it's as if that text weren't in the file at all. A syntax error, an
  undeclared identifier, anything at all inside a false branch causes no
  compile error:
  ```pascal
  {$IFDEF NEVER_DEFINED}
      this is not even valid Pascal +++ ;;;
  {$ENDIF}
  writeln('compiles fine - the line above was never seen');
  ```
- **`{$IFDEF}`/`{$IFNDEF}` nest** — `{$IFDEF A} ... {$IFDEF B} ...
  {$ENDIF} ... {$ENDIF}`, each `{$ENDIF}`/`{$ELSE}` referring to the
  innermost still-open one. An outer false condition suppresses
  everything inside it, including any nested `{$IFDEF}`, regardless of
  the inner condition — nesting is capped at 16 levels deep.
- **`{$DEFINE}`/`{$UNDEF}` last the whole compile, across `uses`
  boundaries** — a symbol the main program defines before its `uses`
  clause is visible to a used unit's own `{$IFDEF}`s, and a symbol a
  unit defines is visible back in the program (or another unit) after
  it, matching real Pascal. `{$IFDEF}`/`{$ENDIF}` *nesting*, in
  contrast, is per-file: each file (the main program, and each unit)
  must close every `{$IFDEF}` it opens before its own end.
- **Directive keywords and symbol names are case-insensitive**, matching
  every other keyword/identifier in this language.
- **Any other `{$...}` directive** (`{$R+}`, `{$R-}`, `{$Q-}`, etc.) is
  recognized as directive syntax and silently accepted as a no-op — see
  "What's not supported yet" below.

### What's not supported yet

- Only `{$DEFINE}`, `{$UNDEF}`, `{$IFDEF}`, `{$IFNDEF}`, `{$ELSE}`, and
  `{$ENDIF}` actually do anything. Every other directive — range
  checking (`{$R+}`/`{$R-}`), overflow checking (`{$Q+}`/`{$Q-}`), and
  so on — is accepted (so it doesn't error) but has no effect. Wiring up
  real `$R+`/`$R-` semantics would mean conditionally emitting this
  compiler's existing subrange bounds checks, a `codegen.c` change, not
  a purely lexer-level one.
- No `{$ELSEIF}`/`{$ELSIF}` chaining — nest another `{$IFDEF}` inside an
  `{$ELSE}` for the same effect.
- No `{$INCLUDE}`/`{$I file}` file inclusion.
- No command-line flag to pre-define a symbol before compilation starts
  — `{$DEFINE}` from source is the only way to define one.

## Types

| Type | Keyword | Literal examples | Notes |
|---|---|---|---|
| Integer | `integer` | `42`, `-7`, `0` | Standard C `int` range |
| Real | `real` | `3.14`, `2.0`, `1.5e10` | 32-bit float — see [Real](#real) below |
| Boolean | `boolean` | `true`, `false` | `write`/`writeln` print it as `TRUE`/`FALSE` |
| String | `string` | `'hello'`, `'it''s here'` | Doubled `''` is an escaped literal quote |
| Char | `char` | `'a'`, `'!'`, `'x'` | See [Char](#char) below - a single-quoted literal of length exactly 1 |
| Array | `array[lower..upper] of T` | — | `T` is `integer`, `real`, `boolean`, `string`, or `char`; see [Arrays](#arrays) |
| Dynamic array | `array of T` | — | Resizable via `SetLength`; `T` is a built-in primitive type only; see [Dynamic arrays](#dynamic-arrays) |
| Record | `type TName = record ... end;` | — | User-defined; see [Records](#records) |
| Enumerated | `type TName = (Val1, Val2, ...);` | — | User-defined; see [Enumerated types](#enumerated-types) |
| Subrange | `type TName = lower..upper;` | — | Bounds-checked `integer`; see [Subrange types](#subrange-types) |
| Byte | `byte` | `0`, `255` | Bounds-checked `0..255`; see [Sized integers](#sized-integers) |
| Shortint | `shortint` | `-128`, `127` | Bounds-checked `-128..127`; see [Sized integers](#sized-integers) |
| Word | `word` | `0`, `65535` | Bounds-checked `0..65535`; see [Sized integers](#sized-integers) |
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

- A `const` section can appear right after `program Name;`, or later,
  interleaved with `type`/`var` sections — see
  [Program structure](#program-structure).
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

### Typed constants (array initializers)

```pascal
const
    Fib: array[1..5] of integer = (1, 1, 2, 3, 5);
    Letters: array[1..3] of char = ('a', 'b', 'c');
```

A Turbo Pascal/Delphi extension: giving a `const` declaration a **type**
and an **aggregate initializer** creates a 1D array with real storage —
unlike the plain `const` form above (which has no storage at all and is
purely a compile-time substitution), a typed constant is genuinely an
initialized global variable.

- Only **1D arrays** are supported (`array[lower..upper] of ElementType`
  — no 2D/ND array constants). The initializer must supply exactly
  `upper - lower + 1` values, in order, comma-separated and parenthesized
  — too few or too many is a compile-time error.
- Each element's own type-matching, subrange-bounds-checking (for a
  `byte`/`shortint`/`word`/named-subrange element type), and every other
  assignment rule works exactly like an ordinary assignment into that
  array, since each initializer element compiles to an ordinary
  assignment under the hood, run once, automatically, before any of the
  program's own code — including a subrange element whose initializer
  value is out of range: that's a **runtime** error at program start
  (the same range-check ordinary subrange assignment already performs),
  not a compile-time one, since the check is the same generic mechanism
  either way.
- **The element type must be a built-in primitive scalar type**
  (`integer`, `real`, `char`, `boolean`, `byte`, `shortint`, `word`) —
  **not** a named type (a `type`-section type alias, subrange, or
  enumerated type), and **not a record**, even one already declared
  earlier in the source. This array-element restriction is unrelated to
  [record typed constants](#typed-constants-record-initializers) below —
  a bare record type is a fully supported typed-constant *type*, just
  not as an *array element* type yet.
- A typed constant **can't be reassigned** — `Fib[1] := 9;` is a
  compile-time error ("Cannot assign to constant"), checked directly at
  the point of assignment.
- **Known limitation**: this compiler has no `const`-parameter concept
  for array parameters (only scalar parameters support `const` — see
  [`const` parameters](#const-parameters)), so passing a typed constant
  array into an ordinary procedure still passes it by reference like any
  array, and the procedure *could* mutate it through its own array
  parameter. Direct assignment to the constant itself is fully blocked;
  this indirect path isn't yet.

### Typed constants (record initializers)

```pascal
type
    TPoint = record
        x, y: integer;
    end;

const
    Origin: TPoint = (x: 1; y: 2);

begin
    writeln(Origin.x, ' ', Origin.y);   { 1 2 }
end.
```

A record type used as a typed constant's own type, initialized with a
parenthesized **field** initializer (`(FieldName: value; ...)`) — a
different shape from the array case above's positional
(`(v1, v2, ...)`) one, matching real Pascal/Delphi's own typed record
constant syntax. Reuses `add_record_var()` — the exact same mechanism
`var p: TRecord;` already uses (one hidden global per field) — so a
typed constant record gets real storage and ordinary field access
(`Origin.x`), with zero new runtime machinery.

- **Every field is required, in the record type's own declared order**
  — `(y: 2; x: 1)` (wrong order) or `(x: 1)` (missing `y`) are both
  compile-time errors. This isn't a named-in-any-order struct literal;
  it must match field declaration order exactly, same as Delphi.
- **Only an all-scalar record type is supported** — a record type with
  an array-typed or nested-record-typed field is rejected up front, as
  soon as the typed constant's own type is recognized, with a clear
  message naming the offending field (not a confusing error partway
  through parsing the initializer).
- **The record type must already be declared** at the point the typed
  constant is parsed — `const`/`type` sections can interleave/repeat
  (see [Program structure](#program-structure)), so a record type
  declared *earlier* in the source works; one declared *later* is
  still an ordinary declare-before-use error (the record type name
  simply isn't recognized as anything yet at that point).
- Each field's own type-matching and subrange-bounds-checking (for a
  `byte`/`shortint`/`word`/named-subrange field) works exactly like the
  array case above — including an out-of-range field value being a
  **runtime** error at program start, not a compile-time one.
- A typed constant record's field **can't be reassigned** —
  `Origin.x := 9;` is a compile-time error, exactly like an array typed
  constant's element (each field's own hidden global is marked
  constant, and field assignment already resolves down to that same
  symbol regardless of whether it came from `var` or a typed constant).

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
| `low(arr)`, `high(arr)`, `length(arr)` | functions | Array bounds and element count, resolved at compile time for a fixed-size array, at runtime for a dynamic one — see [Arrays](#arrays)/[Dynamic arrays](#dynamic-arrays) |
| `SetLength(arr, n)` | statement | Resizes a dynamic array to `n` elements — see [Dynamic arrays](#dynamic-arrays) |
| `copy`, `pos`, `mid`, `left`, `right`, `inpos` | functions | Substring extraction and searching — see [String](#string) |
| `Copy(arr[, start[, count]])` | function | Slices/copies a dynamic array (a genuinely new array, not an alias) — see [`Copy`](#copy) |
| `arr := [e1, e2, ...]` | assignment | Builds a dynamic array directly from its elements — see [Array literals](#array-literals) |
| `Delete(var S, Index, Count)`, `Insert(Source, var S, Index)` | statements | In-place string mutation — see [`Delete` and `Insert`](#delete-and-insert) |
| `upcase`, `uppercase`, `lowercase` | functions | Case conversion — see [String](#string) |
| `IntToStr`, `FloatToStr`, `StrToInt`, `StrToFloat` | functions | Number/string conversion in memory — see [String](#string) |
| `new(p)`, `dispose(p)` | statements | Allocate/release one instance of a pointer's target type; for a class with a `destructor`, `dispose` calls it first — see [Pointers](#pointers), [Destructors](#destructors) |
| `ParamCount` | function | Number of command-line arguments passed to the running program |
| `ParamStr(i)` | function | The `i`th command-line argument as a string — `ParamStr(0)` is the running `.bin`'s own path |
| `ExceptMessage` | function | The message from the `raise` an enclosing `except` block just caught — see [`try` / `except` / `raise`](#try--except--raise) |
| `Random(n)` | function | Integer `0..n-1` — see [`Random` / `Randomize`](#random--randomize) |
| `Randomize` | statement | Seeds the random generator from the system clock — see [`Random` / `Randomize`](#random--randomize) |

- `abs`/`sqr` accept `integer` or `real` (preserving whichever was
  given); `odd`/`succ`/`pred`/`inc`/`dec` work on `integer` only;
  `ord`/`chr` are the `char`/`integer` conversion pair.
- `inc`/`dec`'s target `x` must be a plain integer variable — global,
  local, or a `var` parameter, but not an array element. Rather than a
  general by-reference mechanism, `inc`/`dec` are handled as a special
  statement form: `inc(x)` compiles to exactly what `x := x + 1;` would
  (reusing whichever of `x`'s existing read/write forms already applies
  — including the `var`-parameter one, if `x` is one). One consequence:
  dead-code elimination never removes an `inc`/`dec` on an otherwise-
  unused global, since `x := x + 1;`'s own right-hand side reads `x`,
  which always makes it look used — purely a missed optimization (the
  program still runs correctly), not
  a correctness issue.
- `inc`/`dec` are statements (no return value, can't be used inside an
  expression), matching real Pascal. The other five are ordinary
  functions, usable anywhere an expression is expected.
- `ParamCount`/`ParamStr` follow Turbo Pascal/Free Pascal's own
  convention (standard/ISO Pascal never defined command-line argument
  access at all). Arguments come from `solvm`'s own command line, not
  `pascalc`'s: `solvm program.bin arg1 arg2` — anything after the
  `.bin` path is forwarded verbatim to the running program, regardless
  of whether it looks like a flag (`solvm`'s own `-v` is only ever
  recognized *before* the `.bin` path). `ParamCount` excludes argument
  0 (matching real Pascal — it counts only the *user*-supplied
  arguments); `ParamStr(0)` is always the `.bin` path itself, even when
  `ParamCount` is `0`. An out-of-range `ParamStr(i)` (`i < 0` or
  `i > ParamCount`) returns an **empty string**, not a runtime error —
  this is the real, documented behavior of the reference
  implementations this pair is modeling itself after (Free Pascal:
  "If Index is greater than the number of arguments...an empty string
  is returned"), a deliberate departure from this compiler's more
  usual abort-on-out-of-range convention for array indexing. `ParamCount`
  may be called bare or as `ParamCount()`; `ParamStr` always needs its
  argument in parens.

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
`const` reference, or a bare enumerated value name. A label list can name
more than one value for the same branch (`2, 3:` above).

A label can also be a range, `low..high`, matching any selector value
from `low` to `high` inclusive — both bounds use the same constant forms
as a plain label, must share the selector's type, and `low` must not
exceed `high`. Ranges and plain values mix freely in the same label list:

```pascal
case grade of
    90..100: writeln('A');
    80..89: writeln('B');
    0, 60..69: writeln('D or bare pass');
else
    writeln('lower');
end;
```

Case labels — ranges and plain values alike — must not overlap anywhere
in the statement: two ranges sharing a value, or a plain value falling
inside a range (in either declaration order), is a compile-time error,
same as a repeated plain label.

`else` is optional, matching `if`/`then`. If the selector's value matches
no label and there's no `else`, it's a runtime error (this compiler
follows the common implementation choice here — a runtime error, not
undefined behavior); a range doesn't widen this — a value outside every
label (range or plain) still falls through to `else` or the runtime
error. Each branch (and the `else` branch) is a single statement, exactly
like `if`/`then` — use `begin...end` for multiple statements in a
branch.

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

### `exit` and `halt`

```pascal
function Find(target: integer): integer;
var i: integer;
begin
    for i := 1 to 10 do begin
        if data[i] = target then
            exit(i);          { found it - return i immediately }
    end;
    exit(-1);                 { not found }
end;

procedure ProcessAll;
begin
    while true do begin
        if NoMoreWork then
            halt;              { stop the whole program right here }
        DoWork;
    end;
end;
```

- **`exit;`** — early return from the current procedure/function (or,
  used at top level, from the main program itself), skipping any
  remaining statements and jumping straight to the end of the body,
  wherever the `exit` is - however deeply nested inside `if`/`while`/
  `for`/`repeat`/`case`/`with` it is.
- **`exit(value);`** — same, but first assigns `value` to the enclosing
  function's own return name, exactly like `FuncName := value;` (see
  [Functions](#functions)) followed by a bare `exit;`. **Only legal
  inside a `function`** — a plain `procedure` has no return value to
  set, and neither does the main program itself; either one is a
  compile-time error.
- `exit;`/`exit(value);` inside a `try`/`finally` still runs every
  enclosing `finally` block on the way out, innermost first, exactly as
  if execution had fallen through normally — real Pascal's own
  guarantee that a `finally` block "always runs" holds for `exit` too,
  not just for a normal fall-through or a raised exception.
- Inside a recursive function, `exit(value)` only returns from the
  *current* call - whichever call is innermost/active when it runs -
  never more than one level, no matter how deep the recursion.
- **`halt;`** — terminates the entire program immediately, from
  anywhere, unlike `exit` (which only leaves the current procedure/
  function). Equivalent to falling off the very end of the main program
  body (exit code `0`).
- **`halt(n);`** — same, but sets the program's own OS process exit
  code to `n` (a full expression, not just a literal - `halt(errorCode
  + 1)` works). Checkable from the shell via `$?` right after running
  `solvm`.
- **`halt`/`halt(n)` deliberately do NOT run `finally` blocks** —
  matches real Pascal/Delphi: `halt` is a hard stop, not a controlled
  unwind. Contrast with `exit` above.

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

### `try` / `except` / `raise`

```pascal
try
  writeln('before');
  raise 'something went wrong';
  writeln('never printed');
except
  writeln('caught: ', ExceptMessage);
end;
writeln('after');
```

- `raise <message>;` — `message` must be `string` or `char` (any
  expression, not just a literal). If a `try` is currently active
  (anywhere on the dynamic call chain, including several procedure
  calls deep — not just the current one), control jumps straight to
  that `try`'s `except` block, abandoning whatever was in progress at
  the `raise` site. If no `try` is active anywhere, `raise` behaves
  like every other fatal error: `VM Runtime Error: Unhandled exception:
  message`, then the program stops.
- `try <body> except <handler> end` — runs `body`; if it completes with
  no `raise`, `handler` is skipped entirely and execution continues
  after `end`. If anything in `body` raises, `handler` runs instead
  (from the top), and execution continues after `end` from there.
- `ExceptMessage` — a built-in, no-argument function that returns the
  message string from whichever `raise` was just caught. Only
  meaningful inside a `handler` block.
- A `try` inside another `try`'s `body` nests normally: an inner
  `raise` is caught by the *innermost* enclosing `try`, not any outer
  one. A `raise` from inside a `handler` block itself is *not* caught
  by that same `try` again (the handler that just ran is no longer
  active) — it propagates to whatever `try` encloses that one, or
  aborts the program if there isn't one.
- **`except` only catches an explicit `raise`.** The VM's own built-in
  runtime errors — division by zero, array-index-out-of-range,
  nil-pointer dereference, stack overflow, and so on — are **not**
  catchable by `except` and remain always-fatal exactly as they were
  before this feature existed. This is a deliberate scope decision, not
  an oversight: `raise`/`except` is a language-level mechanism for
  Pascal code to signal and handle its own error conditions, layered on
  top of (not a replacement for) the VM's existing "a bad runtime
  operation always aborts" guarantee. Catching built-in runtime errors
  too would be a substantially larger, separately-scoped change.
- No exception classes or types — `except` is a single blanket handler,
  there's no Delphi-style `on E: SomeExceptionType do` matching (every
  `raise` in a given `try`'s body is caught the same way, regardless of
  what its message says). No bare `raise;` re-raise shorthand —
  re-raising means writing out `raise ExceptMessage;` (or any other
  message) explicitly.

### `warning`

```pascal
writeln('before');
warning('cache is getting full');
writeln('after');
```
Output:
```
before
Warning: cache is getting full
after
```

`warning(<message>);` — `message` must be `string` or `char` (any
expression, same rule as `raise`'s message). Prints `Warning: message`
to **stderr**, not stdout, and **execution continues immediately
afterward** — unlike `assert`/an uncaught `raise`, it's never fatal, and
unlike `raise`, there's nothing for a `try`/`except` to catch (there's
no error to unwind from).

This is a distinct thing from the two other "warning"-shaped mechanisms
in this compiler, worth not confusing with `warning`:
- `assert(cond, msg)` and `raise msg;` (above) are for **errors** —
  something has gone wrong and the program should stop (uncatchably for
  `assert`, catchably for `raise`). `warning` is for the opposite case:
  something worth noting, but not wrong enough to stop for.
- The compiler's own [compile-time warnings](#warnings) (unused local
  variables, an `out` parameter never assigned, etc.) are a *static*
  check the compiler runs over your source before the program ever
  runs — they're about the code itself, not something your running
  program decides to report. `warning()` is the opposite: a runtime
  built-in your own program's logic calls, with values only known while
  it's actually executing.

Because `warning()` writes to stderr while `write`/`writeln` write to
stdout, output interleaved between them (as in the example above) stays
in source order even when both streams are captured together (a
terminal, `2>&1`, etc.) — the VM flushes stdout before every `warning()`
print for exactly this reason.

### `try` / `finally`

```pascal
try
  try
    writeln('before');
    raise 'something went wrong';
    writeln('never printed');
  finally
    writeln('cleanup runs either way');
  end;
except
  writeln('caught: ', ExceptMessage);
end;
writeln('after');
```

- `try <body> finally <cleanup> end` — `cleanup` always runs, whether
  `body` completes normally or raises. If `body` completes normally,
  `cleanup` runs and execution continues after `end`. If `body` raises,
  `cleanup` runs first, then the same exception keeps propagating
  outward exactly as if the `try`/`finally` weren't there — reaching
  the next enclosing `try`/`except` (or `try`/`finally`, whose own
  `cleanup` runs too), or the ordinary `VM Runtime Error: Unhandled
  exception` fatal path if nothing is listening anywhere.
- **A separate construct from `try`/`except`, never combined in one
  block** — there's no `try...except...finally...end`. Nest to get
  both, as in the example above: an inner `try`/`finally` for cleanup,
  wrapped in an outer `try`/`except` to actually handle the exception
  (matches Delphi, which has the same restriction).
- If `cleanup` itself raises (a nested `try`, or a bare `raise`), that
  new exception supersedes the original — the original is discarded,
  and the new one is what keeps propagating outward.
- `ExceptMessage` inside `cleanup` reflects the exception currently
  being unwound *only* when `cleanup` is running because of one. When
  `body` completed normally, `cleanup` runs like any other code — there
  is no exception in flight, so `ExceptMessage` there returns whatever
  an earlier, unrelated `raise` last left behind (or nothing) — not a
  reliable way to detect "am I cleaning up after an exception?" (Pascal
  `finally` blocks generally aren't meant to make that distinction.)
- A labeled statement can't appear anywhere inside a `finally` block's
  `cleanup` — a compile-time error. (`cleanup` is compiled twice
  internally, once for each way it can be reached; a label declared
  inside it would be ambiguous between the two copies.)

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
- **A file variable can't be assigned, compared, or used with any other
  operator** (`f := g;`, `f = g`, ...) — standard Pascal doesn't define
  any of these for files either. A file's real state doesn't live in
  its own storage slot the way every other type's does (see below), so
  copying that slot wouldn't do anything meaningful anyway.

### Typed (binary) files

```pascal
type
    TRecord = record
        id: integer;
        score: real;
    end;
var
    f: file of TRecord;
    r: TRecord;
begin
    assign(f, 'data.bin');
    rewrite(f);
    r.id := 42;
    r.score := 3.14;
    write(f, r);           { writes r's raw values, not formatted text }
    close(f);

    reset(f);
    read(f, r);             { reads one record's worth of raw values back }
    seek(f, 0);              { jump directly to record 0 - random access }
    writeln(filesize(f));   { how many records are in the file - 1 }
end.
```

`var f: file of T;` — a binary file storing a sequence of fixed-size
records of type `T` (a record type, or a bare scalar like `integer`).
Unlike `text`, `read(f, x)`/`write(f, x)` transfer `x`'s raw values
directly (one call, one whole record) rather than formatting/parsing
text — `read`/`write` on a typed file take exactly one argument beyond
`f` (not `text`'s comma-separated multi-target list).

- **`seek(f, n)`** jumps directly to record `n` (0-based) — the one
  thing a typed file can do that a text file, with no fixed record size
  to jump by, never could.
- **`filesize(f)`** returns the file's total record count.
- **`eof(f)`** works the same as for a `text` file. **`eoln`/`readln`/
  `writeln` don't apply to a typed file at all** — a compile error, since
  binary records have no line concept.
- `T`'s fields (recursively, for a record) must be `integer`/`real`/
  `boolean`/an enumerated type/a subrange/a `set` — no array-typed
  fields, and no `string`/`char`/pointer/procedural-typed fields either.
  Reason: those latter types' raw storage is a `string_pool[]` index or a
  process-local address in this compiler, neither of which means
  anything once written to a file and read back in a different run — the
  scalars above are all genuinely portable raw values (an `integer`, or
  a `real`'s own bit pattern, means the same thing regardless of which
  run wrote it).
- `read(f, x)`/`write(f, x)`'s argument must be a plain variable name
  (global or local) of the file's own element type — not `rec.field`,
  not `arr[i]`, not a general expression.

**Not implemented yet:**

- **The failure/error surface stays minimal** — reading past the end of
  a typed file is a fatal `VM Runtime Error`, not a catchable condition.
- **`read`/`write` targeting anything other than a plain variable** —
  see the restriction above.
- **`type TFileType = file of TRecord;`** — a named, reusable typed-file
  type alias. `file of T` is only legal written out inline in a `var`
  declaration, exactly like `text`.
- **A typed file variable as a parameter, local, record field, or array
  element** — global only, same restriction `text` already has.

### How this is implemented

A file variable's real state (the underlying C `FILE*`, and the
filename `assign()` bound it to) lives in a fixed-size table indexed by
the variable's own symbol index — safe only because it's always global,
so that index never changes. No dynamic allocation, matching everything
else in this VM. A typed file additionally caches its own record's
on-disk byte size in that same table, set once when `reset`/`rewrite`
opens it — `seek`/`filesize`/`eof` all read it back from there rather
than needing it re-supplied at every call site.

`read(f, rec)`/`write(f, rec)` are compiled entirely at COMPILE TIME into
one raw value transfer per leaf field of `rec` (recursing into a nested
record, exactly like whole-record assignment already does) — there is no
runtime "copy a whole record" opcode anywhere in this compiler, for
records OR for typed files; every record-shaped operation is unrolled
into N ordinary field-level operations ahead of time. A `file of integer`
(a bare scalar element type) is simply the one-leaf case of the exact
same mechanism. Every leaf transfers a full 4-byte value, **except** a
[`byte`/`shortint`/`word`](#sized-integers) leaf, which transfers 1 or 2
bytes instead — decided per leaf at compile time, so a record's actual
on-disk byte size can be smaller than `4 * (number of fields)`.

### Untyped files

```pascal
var
    f: file;
    buf: array[0..9] of integer;

begin
    assign(f, 'raw.bin');
    rewrite(f);
    buf[0] := 42;
    BlockWrite(f, buf, 1);
    close(f);

    reset(f);
    BlockRead(f, buf, 1);
    close(f);
    writeln(buf[0]);   { 42 }
end.
```

`var f: file;` (no `of Type`) — a raw binary file with no fixed record
shape, distinct from both `text` and `file of T`. `assign`/`reset`/
`rewrite`/`close`/`eof` all work exactly as they do for the other two
file kinds (`reset`/`rewrite` open in binary mode, same as a typed
file). There's no plain `read`/`write` for an untyped file at all —
transfer happens only via:

- **`BlockRead(f, arr, count)`** reads `count` elements from `f`'s
  current position into `arr`, starting at `arr`'s own declared lower
  bound (`arr[low..low+count-1]`) — matching real Pascal, which always
  fills the buffer argument's own base, never a caller-specified offset
  into it.
- **`BlockWrite(f, arr, count)`** is the write twin.
- **`arr` must be a plain 1D array** (global or local), with an element
  type that's typed-file-safe (see "Typed (binary) files" above —
  `integer`, `real`, `boolean`, an enumerated type, a subrange, or a
  `set`; no array-typed, record-typed, or string/char/pointer/
  procedural-typed elements).
- **`count` is an ordinary runtime expression**, not required to be a
  compile-time constant. A `count` larger than `arr`'s own declared size
  is a runtime "Array index out of range" error — the exact same check
  every other array access already has, not a special case.
- **`f` must already be `reset`/`rewrite`d**, exactly like the other two
  file kinds.

**Not implemented yet:**

- **`seek`/`filesize` on an untyped file** — both are tied to a *fixed*
  record size for a typed file; an untyped file has none (real Pascal's
  equivalent needs an optional record-size argument on `Reset`/
  `Rewrite`, deliberately not added here). Using either on an untyped
  file is a compile-time error.
- **A scalar (non-array) `BlockRead`/`BlockWrite` target.**
- **The optional `Result` parameter** some Pascal dialects add to
  `BlockRead`/`BlockWrite`, reporting how many elements were actually
  transferred for graceful short-read handling — reading past the end
  of an untyped file is a fatal `VM Runtime Error` instead, matching
  the exact same "failure surface stays minimal" convention typed files
  already have.
- **Narrower on-disk transfer for a `byte`/`shortint`/`word` array
  element** — every element always transfers as a full 4-byte value
  regardless of the array's own declared subrange bounds. Unlike a
  typed file's record fields (which each carry their own explicit
  on-disk width, set at declaration time), this compiler's arrays have
  no equivalent per-array width tracking to tell a literal `byte`-typed
  array apart from an ordinary hand-written `0..255` subrange array —
  a real gap, not a rounding-down default.
- **`type TFileType = file;`** — a named, reusable untyped-file type
  alias, same cut `file of T` already has.
- **An untyped file variable as a parameter, local, record field, or
  array element** — global only, same restriction the other two file
  kinds already have.

Implemented entirely by reusing typed files' own machinery: `BlockRead`/
`BlockWrite` desugar, at parse time, into an ordinary `for` loop —
`for i := 0 to count - 1 do arr[low + i] := <one raw value transfer>;`
— built from the exact same per-value read/write primitive `read(f,
rec)`/`write(f, rec)` above already compiles a typed file's own leaf
fields into. No new opcode exists anywhere for this feature; array
bounds-checking on `count` comes for free from the loop body being an
ordinary array-element access, running through this VM's existing
runtime check like any other one.

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

### `Delete` and `Insert`

```pascal
var
    s: string;
begin
    s := 'Hello, World!';
    Delete(s, 6, 7);           { s = 'Hello!' }
    Insert(' there', s, 6);    { s = 'Hello there!' }
    writeln(s);
end.
```

`Delete(var S: string; Index, Count: integer)` and `Insert(Source:
string; var S: string; Index: integer)` are Turbo Pascal/Delphi's
in-place string-mutation procedures, the missing counterpart to `copy`
(which only ever builds a *new* string, never touches its argument):

- `Delete(S, Index, Count)` removes `Count` characters from `S`,
  starting at 1-based `Index`, in place.
- `Insert(Source, S, Index)` splices `Source` into `S` right before
  1-based position `Index`, in place. Note the argument order —
  `Source` comes first, `S` (the thing actually mutated) second,
  matching real Delphi's own signature.
- **`Index`/`Count` use the same lenient clamping `copy`/`left`/`right`
  already use** — an out-of-range `Index` or `Count` never errors, just
  yields the most sensible result (`Delete` with `Index` past the end
  of `S` is a no-op; `Insert` with `Index` past the end appends;
  `Index < 1` is treated as `1`).
- **`Insert` can raise a runtime error, `Delete` never can** — `Delete`
  only ever shrinks (or leaves unchanged) a string that already fit
  within this compiler's string-length limit, so it needs no overflow
  check. `Insert` can genuinely grow a string past that limit, so
  (like `+` concatenation) it's a fatal `VM Runtime Error` if the
  result would be too long.
- **`S` must be a plain variable — global, local, or a `var`
  parameter** (not `const`/`out`, not a record field, not a `with`-block
  field, not an array element) — the same restriction `inc`/`dec`'s own
  target has, for the same reason: both procedures write back to `S`
  in place, and this compiler's write-back mechanism only covers a
  plain variable reference. `Delete`/`Insert` compile down to exactly
  the same shape `inc`/`dec` do (`x := x + 1;`, `parser.c`'s
  `parse_inc_dec()`) — read `S`, compute a new value, assign it back —
  just with a string-splicing computation standing in for `+1`.

### Case conversion

- `upcase(c)` — ISO standard. Takes a single `char` (or a `string`
  that's exactly one character, via the usual `char`/`string` interop),
  returns the uppercased `char`, or the same value unchanged if it isn't
  a lowercase letter.
- `uppercase(s)` / `lowercase(s)` — whole-`string` case conversion.
  These aren't ISO standard, but are standard in Turbo Pascal/Delphi/Free
  Pascal. Only affects `a`-`z`/`A`-`Z`; anything else in the string is
  left as-is.

### Number/string conversion

```pascal
var
    s: string;
    n: integer;
    x: real;
begin
    s := 'count: ' + IntToStr(42);        { 'count: 42' }
    n := StrToInt('  -17 ');              { -17 }
    s := 'price: ' + FloatToStr(19.9);    { 'price: 19.9' }
    x := StrToFloat('3.14');              { 3.14 }
end.
```

`write`/`writeln` can format a number into *output*, and `read`/
`readln` can parse a number *from input* — but neither helps when you
want the string itself as a value (to concatenate, store in a field,
pass to a procedure). These four functions do that conversion in
memory, standard in Turbo Pascal/Delphi (not ISO Pascal, which has
neither):

- `IntToStr(n)` — `integer` to `string`, formatted the same way `write`
  would (`%d`).
- `FloatToStr(x)` — `integer` or `real` to `string` (an integer
  argument widens to `real` first, same as `sqrt`/`sin`/etc.), using the
  same default `%.6g` formatting `write`/`writeln` use for a bare `real`
  (see [Printing](#printing) under [Real](#real)) — 6 significant
  digits, switching to scientific notation for very large/small
  magnitudes (`FloatToStr(1000000.0)` is `'1e+06'`). No optional
  precision argument — there's no way to ask for a fixed decimal count
  the way `write(x:0:2)` can; build that string via `write` if needed.
- `StrToInt(s)` / `StrToFloat(s)` — `string`/`char` to `integer`/`real`.
  **The entire string (leading/trailing whitespace aside) must be a
  single valid number** — `StrToInt('42.5')` and `StrToInt('42abc')`
  are both errors, not `42`. This is *stricter* than `read`/`readln`'s
  own number-parsing (which only needs a valid prefix, since it's
  reading from a live stream with no natural "rest of the input" to
  validate) — deliberately so, since a silently-truncated conversion
  here would be a much easier mistake to make and miss.
- **Invalid input is a fatal `VM Runtime Error`**, matching `read`/
  `readln`'s own existing behavior on unparseable input — not a
  catchable condition. Real Delphi's `StrToInt`/`StrToFloat` raise a
  catchable exception; this compiler's version doesn't, to keep the
  feature simple (no interaction with `try`/`except`'s own unwind
  machinery). Validate untrusted input yourself before calling these if
  a bad value shouldn't crash the program.

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

## Dynamic arrays

```pascal
var
    scores: array of integer;
    i: integer;
begin
    SetLength(scores, 5);
    for i := 0 to High(scores) do
        scores[i] := i * i;
    for i := 0 to High(scores) do
        write(scores[i], ' ');
    writeln;                        { 0 1 4 9 16 }
    writeln(Length(scores));        { 5 }
end.
```

`array of ElementType` (no `[lower..upper]`) declares a **resizable**
array — a genuinely different kind of value from this language's
fixed-size `array[lower..upper] of T` (see [Arrays](#arrays) above), not
just a variant spelling of it:

- **Always 0-based.** `Low(arr)` is always `0`; `High(arr)` is
  `Length(arr) - 1`. This differs from a fixed-size array, whose bounds
  are whatever you declared them as.
- **`SetLength(arr, n)`** allocates or resizes `arr` to hold exactly `n`
  elements. This is the *only* way to give a dynamic array any storage —
  indexing into one before ever calling `SetLength` on it is undefined
  (see "Uninitialized value" below). Growing preserves every existing
  element and zero-fills the new ones; shrinking preserves the retained
  prefix and discards the rest. `SetLength(arr, 0)` empties it back to
  the same state as a never-initialized variable.
- **`Length(arr)`** — the current element count, read at *runtime*
  (unlike a fixed-size array's `length`, which is always a compile-time
  constant — see [`low`, `high`, `length`](#low-high-length) above).
  `High(arr)` is `Length(arr) - 1`; both work on a dynamic array the
  exact same `low`/`high`/`length` builtins already used for fixed-size
  arrays resolve to automatically, based on the argument's own type.
- **Indexing (`arr[i]`)** requires an `integer` index and is
  bounds-checked at runtime against `[0, Length(arr) - 1]`, exactly like
  a fixed-size array's own indexing.
- **Element type**: one of the built-in primitive types only —
  `integer`, `real`, `char`, `boolean`, `string`, `byte`, `shortint`, or
  `word`. A named type (a type alias, subrange, or enumerated type), a
  `record`, a pointer, a procedural type, or another `array of ...`
  (nested dynamic arrays) aren't supported as an element type yet. A
  `byte`/`shortint`/`word` element is still bounds-checked on every
  write, exactly like a fixed-size array's own subrange-typed elements —
  it just fails at **runtime**, not compile time, since the value being
  stored isn't generally known until then.
- Declared directly wherever a type is expected — as a `var` (global or
  procedure-local) or as a **parameter** (see below) — or through a
  named alias (`type TIntArray = array of integer;`, see [Type
  aliases](#type-aliases)); this language's existing fixed-size arrays
  still can't be named this way.

### Reference semantics

A dynamic array variable's actual runtime value is a single pointer into
a shared heap block (see "Under the hood" below) — so, unlike every
other value in this language, **assignment and parameter-passing don't
copy the array's contents**:

```pascal
var
    a, b: array of integer;
begin
    SetLength(a, 3);
    a[0] := 1; a[1] := 2; a[2] := 3;
    b := a;              { b now aliases the SAME storage as a }
    b[0] := 99;
    writeln(a[0]);        { 99 - visible through a too }
end.
```

This is deliberate, not a shortcut — it's the same behavior real
Delphi's own dynamic arrays have, and it falls out naturally here from a
dynamic array being "just a pointer" under the hood:

- **`arr2 := arr1;`** copies the pointer, not the contents — `arr2` and
  `arr1` share the same storage afterward.
- **Passed as an ordinary (non-`var`) parameter**, the callee gets a copy
  of the *pointer* — element writes through it (`a[i] := x;`) are visible
  to the caller, but calling `SetLength` inside the callee only rebinds
  the callee's own local copy, leaving the caller's variable pointing at
  the original, unresized storage.
- **Passed as a `var` parameter**, `SetLength` inside the callee DOES
  rebind the caller's own variable, exactly like any other `var`
  parameter.
- **Passed as a `const` parameter**, the callee can read it and can still
  write through it element-wise (shallow, same as a `const` pointer
  parameter's own field writes) — but calling `SetLength` on it is a
  compile-time error, since that would reassign the parameter's own
  value.

### Uninitialized value

A dynamic array variable that's never had `SetLength` called on it holds
its ordinary zero-init default, exactly like every other variable in
this language — reading `Length`/`High` on one is well-defined and
reads `0` (this is actually guaranteed here, not merely a convention —
see "Under the hood" below), but indexing into one (`arr[0]`) is a
runtime "not allocated" error, since there's nothing to index into yet.
Always call `SetLength` before writing/reading an element. This mirrors
[pointers](#pointers)' own existing convention of requiring an explicit
`nil`/`new()` before a pointer is meaningfully usable.

### Under the hood

A dynamic array's runtime value is a plain int: an offset into the same
heap `new`/`dispose` already allocate from (see
[Pointers](#pointers)) — `0` means "not yet allocated" (both a fresh
variable's own zero-init default and what `SetLength(arr, 0)` itself
produces; heap offset `0` is permanently reserved, never handed out to a
real allocation, specifically so this is always safe). A real allocation
is a self-describing block: its own first slot holds the array's current
length, and the following `length` slots hold the elements.
`SetLength` always allocates a fresh block sized to the new length and
copies over what fits — it deliberately **never frees the old block**,
since (per "Reference semantics" above) another variable might still be
aliasing it. This means repeated `SetLength` calls on the same variable
(e.g. growing it one element at a time in a loop) leak the intermediate
blocks — the same accepted, documented cost as forgetting `dispose`
after `new` already is (see [Pointers](#pointers)'s own "There is no
garbage collector" note) — not a new kind of problem this feature
introduces.

### `nil`

```pascal
var
    arr: array of integer;
begin
    if arr = nil then writeln('starts nil');   { true - never SetLength'd }
    SetLength(arr, 3);
    if arr <> nil then writeln('now allocated');
    SetLength(arr, 0);
    if arr = nil then writeln('emptied back to nil');
    arr := nil;
    if arr = nil then writeln('explicitly nil-ed');
end.
```

`nil` can be assigned to a dynamic array, and compared against one with
`=`/`<>` (no other operator, same restriction pointers already have) —
in either operand order. **A never-`SetLength`'d array, one just
`SetLength(arr, 0)`'d back down, and one explicitly assigned `nil` all
compare equal to `nil`** — deliberately, matching real Delphi, where a
zero-length dynamic array and a `nil` one are the same reference. Under
the hood a dynamic array's own "not allocated" sentinel is `0`, not the
`-1` `nil` uses for pointers (see "Under the hood" above) — comparing
against `nil` doesn't compare raw values directly, precisely so this
equivalence holds regardless of which of the three ways an array ended
up "empty."

### `Copy`

```pascal
var
    a, b: array of integer;
begin
    SetLength(a, 5);
    { ... fill a ... }
    b := Copy(a);         { full copy - a NEW array, not an alias }
    b := Copy(a, 2);        { elements from index 2 to the end }
    b := Copy(a, 2, 3);      { 3 elements starting at index 2 }
end.
```

`Copy` is the escape hatch out of dynamic arrays' own reference semantics
(see above): it allocates a genuinely new backing block and copies
elements into it, so mutating the result never mutates the source. This
is the *same* `Copy`/`Mid` keyword strings already use, dispatched by the
first argument's type at compile time (a dynamic-array-typed first
argument gets this behavior; a `char`/`string` one gets the existing
substring behavior described under [Strings](#strings)) — the two share
a name but not a grammar: `start`/`count` are each optional here, where
the string form requires both.

Lenient/clamping, exactly like the string form already is — never a
runtime error:

- `start` is clamped to `[0, Length(arr)]`.
- `count`, if given, is clamped to `[0, Length(arr) - start]`; a negative
  `count`, or omitting it entirely, means "everything from `start` to the
  end."
- A source array that's never been `SetLength`'d (or `nil`) behaves as
  length `0` — `Copy` on it returns an empty (`nil`-equal) result rather
  than erroring.
- A result clamped down to zero elements is the same empty/`nil` value
  every other empty dynamic array is (see "`nil`" above) — it compares
  equal to `nil` and to a never-`SetLength`'d variable.

### Array literals

```pascal
var
    a: array of integer;
    b: array of real;
begin
    a := [1, 2, 3];
    b := [1, 2.5, 3];    { the integer literals widen to real, like any other assignment }
    a := [];               { the same empty/nil value SetLength(a, 0) produces }
end.
```

`arr := [e1, e2, ...]` — assigns a fresh dynamic array built directly from
the listed elements, without an explicit `SetLength` first. Each element
can be any expression, not just a constant, and is checked against the
array's declared element type exactly like an ordinary element write
(`arr[i] := ...`) — including widening (an `integer` literal into a
`real` array) and the runtime bounds check a `byte`/`shortint`/`word`
element type already gets.

This reuses the `[...]` bracket syntax a **set** constructor already
uses (`s := [1, 2, 3];` for `s: set of ...`) — which one a given `[...]`
means is decided by the target's own declared type, not by looking at
the brackets' contents, so there's no ambiguity in practice. Unlike a
set constructor, there's no range syntax (`[1..3]`) — real Delphi array
literals don't have one either, and it wouldn't mean anything for a
`real`/`string` element type.

Valid as an assignment's right-hand side (`arr := [...];`) and directly
as a by-value procedure/function argument:

```pascal
function Sum(a: array of integer): integer;
var i, s: integer;
begin
    s := 0;
    for i := 0 to High(a) do s := s + a[i];
    Sum := s;
end;

begin
    writeln(Sum([1, 2, 3, 4]));    { 10 - no variable needed first }
end.
```

— the callee's declared parameter type is what tells `[...]` apart from
a set constructor here, the same way an assignment target's type does.
Not yet valid as a **`var`** or **`const`** argument, though — both
share this compiler's ordinary variable-reference passing mechanism,
which needs a real variable to point at; pass a literal to a plain
by-value parameter, or assign it to a variable first if the callee needs
`var`/`const`. Also not yet valid in any other general-expression
position (a typed-constant initializer, a `write`/`writeln` argument,
etc.) — only a bare assignment RHS and a by-value call argument are
supported so far.

### Record and class fields

```pascal
type
    TBox = record
        data: array of integer;
    end;
    TFoo = class
        data: array of integer;
    end;
var
    b: TBox;
    f: TFoo;
begin
    SetLength(b.data, 3);
    b.data[0] := 1; b.data[1] := 2; b.data[2] := 3;
    b.data := [10, 20, 30];        { array-literal assignment works too }

    new(f);
    SetLength(f.data, 2);
    f.data[0] := 100;
    writeln(Length(b.data), ' ', f.data[0]);

    for x in b.data do writeln(x);   { 'for x in <record/class field> do' works too }
end.
```

A `record` field or a `class` field can be a dynamic array — declared,
`SetLength`'d, indexed, read, and written exactly like a plain variable
of the same type, including array-literal assignment, `Copy`, and
`for x in ... do` iteration. Works for a global or local record/class
variable, a record nested inside another record, and a `record`/`class`
passed as an ordinary (non-`var`) parameter (the field's pointer is
shared with the caller exactly like a plain dynamic-array value
parameter's own reference semantics — an element write is visible to
the caller, `SetLength` inside the callee isn't). Whole-record
assignment (`b2 := b1;`) copies a dynamic-array field's pointer, not its
contents — the same shallow-copy behavior a pointer-typed field already
has.

**The one remaining gap**: a dynamic array field can't be a typed
constant's own field value — there's no literal syntax for one (a typed
constant needs a genuine compile-time value; a dynamic array only ever
gets one via `SetLength`/an array literal, both runtime-only).

### What's not supported yet

- **Multi-dimensional dynamic arrays** — `array of array of integer` (or
  any other nesting) is a compile-time error; only a single dimension of
  a primitive element type is supported.
- **Record, pointer, procedural, or named (type-alias/subrange/
  enumerated) element types** — same restriction as above; only the
  built-in primitive type keywords are accepted.
- **Array-literal syntax for a fixed-size array**; a dynamic-array
  literal as a `var`/`const` call argument; and a dynamic-array literal
  in any other general-expression position (a typed-constant
  initializer, a `write`/`writeln` argument, etc.) — see "Array
  literals" above for what IS supported (an assignment RHS, and a
  by-value call argument).
- **Comparing two dynamic arrays directly** (`arr1 = arr2`) — only
  comparison against `nil` is supported; standard Pascal doesn't define
  whole-array comparison either.

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
  for `integer`, `real`, `boolean`, `string`, `char`, or a **dynamic
  array** (`array of ElementType`) — usable everywhere the aliased type
  is: variable declarations (plain or array-element), parameters,
  procedure/function locals, and (for the five plain scalar types)
  record fields and function return types.
- An alias can itself alias an earlier alias (`TYears = TAge;` above) —
  chains resolve all the way down to one of the accepted underlying
  types, dynamic arrays included:
  ```pascal
  type
      TIntArray = array of integer;
      TScores = TIntArray;
  var
      scores: TScores;
  begin
      SetLength(scores, 3);
      scores := [10, 20, 30];   { array-literal assignment works through the alias too }
  end.
  ```
- A type alias has no runtime representation of its own — it's resolved
  entirely at parse time to whichever type it names, exactly like how
  [records](#records) resolve to hidden globals. Two variables declared
  through different aliases of the same underlying type (e.g. `age: TAge`
  and `x: integer`), or through an alias and the equivalent type spelled
  out directly (`scores: TIntArray` and `direct: array of integer`), are
  completely interchangeable — the alias is a compile-time name only, not
  a distinct type the type checker tracks separately (a dynamic-array
  alias reuses the exact same structural dedup every other `array of
  integer` shape already gets — see [Dynamic
  arrays](#dynamic-arrays)).
- Alias names, record type names, enumerated type names, and subrange
  type names all share one namespace (declared in the same `type`
  section, in any order or mix) — redeclaring any of them as another is
  a compile-time error, same as redeclaring one as itself.
- A type alias can't itself name a record type (`type TWrapper =
  TPoint;` where `TPoint` is a record type is a compile-time error) or a
  **fixed-size** array (`type TFixedArr = array[1..5] of integer;` is
  also a compile-time error, with a message saying so directly — only a
  *dynamic* array, `array of ElementType`, can be named this way) — only
  the five scalar types, a dynamic array, an enumerated type, a subrange
  type, and other aliases of those, are accepted (see [Enumerated
  types](#enumerated-types) and [Subrange types](#subrange-types)
  below). Aliasing a subrange type produces another, equivalent
  subrange type under the new name (its bounds are still enforced).
- A dynamic-array alias inherits the same restrictions plain `array of
  ElementType` already has — it can't be a record field or a function
  return type yet either (see [Dynamic arrays](#dynamic-arrays)'s own
  "What's not supported yet"); aliasing doesn't lift that.

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

## Sized integers

```pascal
type
    TRec = record
        flag: byte;      { 0..255 }
        delta: shortint;  { -128..127 }
        count: word;      { 0..65535 }
        total: integer;   { unchanged, full range }
    end;
var
    f: file of TRec;
    r: TRec;
begin
    r.flag := 200; r.delta := -5; r.count := 40000; r.total := 999;
    rewrite(f);
    write(f, r);   { writes 1 + 1 + 2 + 4 = 8 bytes, not 16 }
end.
```

`byte` (`0..255`), `shortint` (`-128..127`), and `word` (`0..65535`) are
three predefined [subrange types](#subrange-types) — everything that
section says about subrange types applies to all three unchanged
(fully assignment/arithmetic-compatible with `integer`, bounds-checked
on every store, no range-checking on arithmetic itself). They exist for
one additional reason subrange types alone don't cover:

- **In a [typed (binary) file](#typed-binary-files), a `byte`/
  `shortint`/`word` field or element writes/reads its own narrower
  width on disk** — 1 byte for `byte`/`shortint`, 2 for `word` — instead
  of the 4 bytes every other field/element (including an ordinary
  hand-written subrange like `type TByte = 0..255;`) always uses. This
  matters for interop with an external fixed-width binary format (a
  file produced by another program, a documented file layout, a C
  struct written by something else) where the on-disk byte layout has
  to match exactly, not just the value's *range*.
- **This on-disk narrowing is triggered only by writing the literal
  `byte`/`shortint`/`word` keyword** — never by a subrange's bounds
  happening to match one of those ranges. A field declared
  `type TByte = 0..255; ... flag: TByte;` stays at the ordinary 4-byte
  width on disk even though its value range is identical to `byte`'s.
  This is deliberate: it means adopting `byte`/`shortint`/`word` in a
  record is an explicit, visible change to that record's on-disk
  layout, not something that could silently happen to an existing
  subrange-typed field.
- Outside a typed file, `byte`/`shortint`/`word` behave completely like
  an ordinary subrange-constrained `integer` — same in-memory
  representation, same `write`/`writeln` formatting, no special
  behavior of their own.
- `filesize(f)`/`seek(f, n)` on a typed file with narrower fields still
  count/index whole **records**, not bytes — the byte-width difference
  is entirely an on-disk packing detail; `TRec` above is still one
  record, 8 bytes, and `seek(f, 1)` still means "the second record."

**Not supported**: `int64` (a full 64-bit integer type) — this VM's
`integer` occupies one native-`int`-sized storage slot everywhere
(stack, variables, array elements), the same reason `real` is kept to
32 bits (see [Real](#real)); a genuine 64-bit type needs a wider slot or
a separate storage region, a larger change than `byte`/`shortint`/
`word` needed. Tracked as a future item in
[docs/ROADMAP.md](ROADMAP.md).

## `sizeOf`

**`sizeOf(x)` answers "how many bytes would `x` occupy as a [typed
(binary) file](#typed-binary-files) record/element" — not "how much
memory does `x` use."** These are different questions in this VM:
every scalar occupies exactly one uniform slot in memory *regardless*
of declared type (that's true for `integer` and `byte` alike), so an
honest in-memory answer would always be a uniform, uninteresting number
— the one place a byte-size answer is actually meaningful and variable
is on-disk typed-file layout, which is what `byte`/`shortint`/`word`
(above) already give real control over. This is a deliberate departure
from Delphi's `sizeOf` (which answers real in-memory layout), not an
oversight.

```pascal
type
    TRec = record
        flag: byte;
        total: integer;
    end;
var
    f: file of TRec;
    r: TRec;
begin
    writeln(sizeOf(TRec));   { 5 }
    writeln(sizeOf(r));      { 5 - same answer, from the variable's type }
    assign(f, 'data.bin');
    writeln(sizeOf(f));      { 5 - works even before reset/rewrite opens it }
end.
```

`sizeOf(x)` accepts:

- **A record type name** (`sizeOf(TRec)`) — the type must be legal as a
  typed file's element type (see
  [Typed (binary) files](#typed-binary-files) — no array, string, char,
  pointer, or procedural-typed fields, checked recursively through any
  nested record).
- **A record variable** (`sizeOf(r)`, global or local) — same answer as
  its type.
- **A typed-file variable** (`sizeOf(f)`) — the file's own per-record
  byte size, computed at its `var` declaration and available
  immediately, even before `reset`/`rewrite` ever opens the file
  (unlike `filesize(f)`, which needs an actual open file to answer a
  record *count*).
- **A scalar type name/keyword** (`sizeOf(integer)`, `sizeOf(byte)`, a
  declared type alias, enumerated type, or subrange type name) — `4`
  for `integer`/`real`/`boolean`/a set/an enumerated type, `1` for
  `byte`/`shortint`, `2` for `word`. A hand-written subrange stays `4`
  even if its bounds match `byte`'s (`sizeOf(TAge)` where
  `TAge = 0..255` is `4`, not `1`) — the same compatibility rule
  `byte`/`shortint`/`word` themselves already follow: only the literal
  keyword narrows anything.

**Not supported yet**: `sizeOf` on a plain scalar variable (`var b:
byte; sizeOf(b)`) — a documented v1 gap, not a silent one; use
`sizeOf(byte)` (or whatever the variable's declared type is) instead,
which gives the identical answer. Also not supported: arrays (types or
variables), classes/pointers, string/char types or variables, and
`int64` (doesn't exist in this compiler).

## `Random` / `Randomize`

```pascal
var
    i, roll: integer;
begin
    Randomize;
    for i := 1 to 5 do begin
        roll := Random(6) + 1;   { 1..6 }
        writeln(roll);
    end;
end.
```

- `Random(n)` — a function, returns an `integer` in `0..n-1`. `n` must
  be a positive integer (`>0`); `Random(0)`/a negative argument is a
  runtime error.
- `Randomize;` — a procedure (bare, no parens), seeds the generator
  from the system clock.
- **Without ever calling `Randomize`, `Random`'s sequence is
  deterministic — the exact same sequence every time the same compiled
  program runs** — matching real Pascal's own convention (useful for
  reproducible testing). This falls out for free from reusing the C
  standard library's own `rand()` unseeded default behavior, not
  anything this compiler tracks itself.
- **Not supported**: the parameterless Turbo Pascal form `Random`
  (no argument, returns a `real` in `[0, 1)`) — only the
  mandatory-argument, `integer`-returning `Random(n)` form exists here,
  the overwhelmingly more common one in practice (dice rolls, random
  indices, shuffling). Supporting both would mean one name whose return
  *type* changes based on whether an argument is given, which nothing
  else in this compiler does.
- Uses `rand() % n` directly — no bias correction for `n` close to the
  underlying generator's own range, an accepted simplicity trade-off
  (this isn't a cryptographic or statistically-rigorous context).

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
  [docs/CHANGELOG.md](CHANGELOG.md)). An ordinal that doesn't correspond to
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
  only (see [Arrays](#arrays)).
- **A plain `const` CAN be given an enum value** (`const Favorite =
  Green;`), as long as that enum type is declared *earlier* in the
  source — `const`/`type` sections can interleave (see
  [Program structure](#program-structure)), so this is just the same
  declare-before-use rule every other reference already follows, not
  something special-cased for enums. A **typed** constant (`const
  Favorite: TColor = Green;` — an array initializer, see [Typed
  constants](#typed-constants-array-initializers)) is a different,
  narrower feature that doesn't support an enum element type yet.

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

### `for x in ... do`

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

**Iterating a 1D array**:

```pascal
var
    scores: array[1..4] of integer;
    x: integer;
scores[1] := 5; scores[2] := 10; scores[3] := 15; scores[4] := 20;
for x in scores do
    writeln(x);   { prints 5, 10, 15, 20 }
```

`x` must match the array's declared ELEMENT type exactly (a scalar type
only — `integer`, `boolean`, `char`, an enumerated type, or a
subrange). `scores` can be a global array, a local array, or a `var`
array-reference parameter — desugars to sweeping a hidden index from
the array's own declared bounds, reading `scores[hiddenIndex]` into `x`
each iteration; no evaluate-once caching is needed here (unlike a set
or string expression) since an array is never itself an expression
value in this compiler, only ever accessed by name.

**Iterating a dynamic array**:

```pascal
var
    arr: array of integer;
    x, sum: integer;
SetLength(arr, 3);
arr[0] := 10; arr[1] := 20; arr[2] := 30;
sum := 0;
for x in arr do
    sum := sum + x;
writeln(sum);   { 60 }
```

Works the same way as a static array — `x` must match the declared
element type, `arr` can be a global, local, or `var` parameter — except
the length isn't known at compile time, so `Length(arr)` is evaluated
and cached exactly **once**, before the loop starts, same as a set or
string expression. This matters if the loop body itself calls
`SetLength` on the same array: the loop still runs exactly as many
times as `arr`'s length was *when the loop started*, never fewer or
more, regardless of how the array's own length changes underneath it
during the loop.

**Iterating a string**:

```pascal
var
    name: string;
    c: char;
name := 'Sol';
for c in name do
    writeln(c);   { prints S, o, l }
```

`c` must be `char`. The string expression (which, unlike an array, CAN
be any string-valued expression — a variable, a function call, a
concatenation) is evaluated exactly once, before the loop starts, same
as a set expression — a string-returning function called this way runs
once, not once per character. Its length is likewise captured once at
loop start; mutating the string from inside the loop body doesn't
change how many characters the loop visits.

Assigning into `x`/`c` still respects a `subrange`-constrained loop
variable — if the variable is declared `1..10` and the array holds a
wider-ranged value, or the char is subrange-constrained (`'a'..'z'`),
an out-of-range element still triggers the ordinary runtime range-check
error, exactly as an explicit assignment to that variable would.

### What's not supported yet

- **Combining two sets declared with different base types/ranges isn't
  checked** — `(set of 0..9) + (set of TColor)` is accepted, since both
  are just bitmasks under the hood; this compiler doesn't track which
  declared base type a given set value "belongs to" the way it does for
  enums. A deliberate simplification — mixing set shapes like this is a
  programmer error this compiler won't catch, not a feature.
- **2D/N-D arrays and arrays of records** as the iterated collection —
  only 1D, scalar-element arrays work.
- **A record field or `with`-field array** as the iterated collection —
  `for x in someRecord.numbers do` isn't recognized; copy/alias into a
  plain array variable first (a record-field or `with`-field SET or
  STRING works fine, this restriction is array-specific).

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
- **`with` accepts a comma-separated list of targets**,
  `with a, b do <statement>;`, equivalent to nesting
  (`with a do with b do <statement>;`) — a later target's field shadows
  an earlier target's same-named field, exactly as explicit nesting
  would. Each target in the list is checked with the same rules listed
  below (must be a plain global record variable, no nested-record
  field), independently.
- **A `with`-target's field takes priority over everything else with the
  same name** — a local variable, parameter, or global with the same
  name as a field is shadowed for the duration of the `with` body. This
  matches classic Pascal behavior (and is a well-known source of subtle
  bugs in real Pascal code — a field name accidentally colliding with an
  outer variable silently redirects to the field instead).
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

### `Pointer` and `@`/`Addr`

```pascal
type
    PInt = ^integer;
    PPoint = ^TPoint;
    TPoint = record
        x, y: integer;
    end;
var
    p: PPoint;
    g: Pointer;
    fieldPtr: PInt;
begin
    new(p);
    p^.x := 10;
    p^.y := 42;

    g := @(p^.y);          { the address of p^'s own 'y' field }
    fieldPtr := PInt(g);   { explicit cast back to a specific type }
    writeln(fieldPtr^);    { 42 }

    fieldPtr^ := 99;
    writeln(p^.y);         { 99 - same storage, not a copy }
end.
```

`Pointer` is a generic pointer type with no declared target — unlike
`^Target`, it doesn't know what's at the address it holds. Any specific
pointer type's value can be assigned to it implicitly (`g := p;`); the
reverse needs an explicit cast (`PPoint(g)`), since going from generic
back to specific can't be checked at compile time. `Pointer` is
comparable with `=`/`<>` against `nil`, another `Pointer`, or any
specific pointer type, exactly like an ordinary pointer, and — unlike
`text`/`file`/`file of T` — it's an ordinary type, usable as a
parameter, local, record field, or array element, since its runtime
state is just a plain int, not an external resource.

**Why `@`/`Addr` here means something narrower than in real Pascal.**
Real Pascal's `@x` takes the address of *any* variable. That has no
faithful representation in this VM: a pointer's runtime value is an
offset into `vm_heap_mem[]` (see "Under the hood" above) — the *only*
dynamically-sized storage region this VM has. An ordinary variable
lives in a completely separate, statically-sized region
(`vm_vars[]`/`vm_frame_stack[]` for scalars, `vm_array_mem[]` for
arrays), never in `vm_heap_mem[]` — so there's no meaningful heap
address to compute for it. What IS representable is the address of
something *already* reached through a pointer dereference — a record's
fields sit at fixed, known offsets within its own `new()`-allocated
block, so `@(p^.field)` is just `p`'s own value plus that offset, and
`@(p^)` (a scalar target) is simply `p` itself.

- **`@expr` and `Addr(expr)` are the same operation**, prefix vs.
  function-call spelling — pick whichever reads better.
- **The operand must be exactly a pointer dereference** — `p^`, or a
  chain reaching a record field (`p^.field`, or deeper). Anything else
  — a plain variable (`@x`), the pointer variable's own storage slot
  rather than what it points to (`@p`), an array element, a general
  expression — is a compile-time error naming the restriction, not a
  fallback or a silently wrong value.
- **The result is always `Pointer`-typed.**
- **`new`/`dispose` reject a `Pointer`-typed operand** — `new()` needs a
  target type to know how large an allocation to make, and `Pointer`
  has none by definition. A `Pointer` value can only ever come from an
  existing typed pointer (via implicit widening) or `@`/`Addr`, never
  allocated directly. (Real Pascal's own answer for allocating an
  untyped pointer directly, `GetMem`/`FreeMem` with an explicit byte
  count, isn't implemented here.)
- **`write`/`writeln`/`readln` reject it**, exactly like every other
  pointer type.

**Not implemented yet:**

- **`@x` for an ordinary variable** — a genuine, permanent architectural
  limitation (see above), not a "not yet." The same applies to `@p` for
  a pointer variable's own slot.
- **`GetMem`/`FreeMem`** (allocating/releasing an untyped pointer's
  target directly, with an explicit byte count) and other untyped-
  pointer conveniences some Pascal dialects add — out of scope here.

## Classes

**v1 complete** — declaration parsing, `new`/`dispose`, field
read/write, method bodies, `c.Method(args)` call syntax (all 5 "Classes
and instances" build steps, see `docs/CHANGELOG.md`'s Phase 2), single
inheritance, and virtual/dynamic dispatch all work. See "Not implemented
yet" below and `notes/classes-and-instances-scoping.md` for the full
design rationale.

Every instance also carries a hidden runtime type tag (identifying its
own allocating class) at the start of its heap block, one int larger
than just its declared fields — entirely internal bookkeeping with no
Pascal-visible syntax of its own. This tag is what every method call
dispatches through (see "Virtual dispatch" below). `new()` into a
class-typed field reached through an explicit `^` isn't supported yet —
allocate into a plain class variable first, then assign it into the
field.

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

A field can also be a **nested (plain) record type** — composition by
value, embedded directly inside the class's own heap-allocated instance:

```pascal
type
    TPoint = record
        x, y: integer;
    end;
    TCircle = class
        center: TPoint;
        radius: real;
    end;
var
    c: TCircle;
begin
    new(c);
    c.center.x := 3;
    c.center.y := 4;
    { or, inside a method body, via self-shorthand: center.x := 3; }
    ...
end.
```

Every leaf field of `center` gets its own heap slot, contiguous within
`c`'s own instance — accessed via `c.center.x`, an arbitrary-depth
`.field.field` chain if the nested type itself nests further, exactly
like a nested field on a plain record already works. Composition by
*reference* (a field whose type is another class, e.g. `next: TNode;`)
already worked before this — that's just an ordinary scalar pointer
field. **Not supported**: reading/writing/passing the nested record as
a whole (`c.center` alone, with no further `.field`) — a compile error;
you must always name a leaf field.

A field can also be an **array of scalars**, read/written by indexing
it, again via `c.data[i]` or self-shorthand `data[i]`:

```pascal
type
    TBuffer = class
        data: array[0..3] of integer;
        procedure Fill;
    end;
var
    b: TBuffer;
begin
    new(b);
    b.data[0] := 10;
    writeln(b.data[0]);
end.
```

Each element gets its own heap slot, same as a nested record's leaves.
**Not supported**: chaining a further `.field`/`^` off an array-field
element access (`self.items[i].field` or `self.items[i]^.next`) — an
array-field access, like a method call's result, is always the
*terminal* step of an access chain, a known gap. There's also a
practical size limit: every class instance's heap block must fit within
the same `MAX_RECORD_FIELDS + 1` slot budget an ordinary (all-scalar)
class already had, so a class's array fields are necessarily small —
a large buffer as a class field isn't a good fit yet.

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
  `self.field`, exactly like any other class-typed variable.
- A bare identifier that isn't a local variable/parameter, but names a
  field or method of the enclosing class, is **implicit `self.`
  shorthand** — `radius := r;` inside `TCircle.SetRadius` means exactly
  `self.radius := r;`, and a bare `DoubleRadius;` means
  `self.DoubleRadius;`. This is pure sugar: it resolves through the
  exact same field/method lookup an explicit `self.x` already goes
  through, so everything said above about `c.field`/`c.Method(args)`
  (dynamic dispatch, the parenless-call rule, etc.) applies unchanged.
  Precedence, checked in this order: a local variable or parameter of
  the same name always wins (standard shadowing, matching how a local
  already shadows a global); otherwise a class field or method of the
  same name wins over a same-named global variable/procedure. An
  inherited field/method (never redeclared by the method's own class)
  is reachable via shorthand too, since inheritance is already flattened
  into the class's own field/method list before any method body is
  parsed.
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
builds a dynamically-dispatched call (see "Virtual dispatch" below) with
`c` passed as the hidden first (`self`) argument. The parenthesized
argument list is optional when the method takes none, matching how an
ordinary parameterless function/procedure call already works
(`c.Bump;` as a bare statement, no `()`). A `function` method's call can
be used as a value anywhere an expression is expected
(`writeln(c.Area)`); a `procedure` method's call can only be a
statement, exactly like any other procedure — using one as a value is a
compile-time error, same message an ordinary procedure-used-as-a-value
already gets. A method call's result can't itself be chained into a
further `.field`/`^` step yet (a known gap, below).

### Constructors

`new`'s own syntax grows an optional second argument to allocate and
initialize a class instance in one statement:

```pascal
type
    TCircle = class
        radius: real;
        procedure Init(r: real);
    end;
var
    c: TCircle;
begin
    new(c, Init(2.0));   { same as: new(c); c.Init(2.0); }
    writeln(c.radius);
    dispose(c);
end.
```

`new(c, Init(args))` is pure sugar for `new(c); c.Init(args);` written
as one guaranteed-together statement — nothing more. **No method name
is reserved**: `Init` isn't special (this compiler has no
function/method overloading anywhere, so there's no "the" constructor
to pick out by a magic name either) — any method works here, including
one that doesn't look like an initializer at all. The parenthesized
argument list follows the same optional-when-parameterless rule as an
ordinary method call (`new(c, Init);`). Dynamic dispatch works exactly
as it does everywhere else, including from *inside* the called method —
the class's runtime type tag is written before the call runs, so a
constructor-style method calling another `self` method during its own
body dispatches correctly, and an inherited (never-redeclared)
constructor-style method is reachable the same way an ordinary
inherited method call already is.

**Nothing enforces a constructor actually gets called** — plain
`new(c);` remains completely legal on its own, and reading a field no
constructor ever set is silently allowed, exactly as it was before this
feature. `new(head^.next, Init(...))` (an allocation target reached
through an explicit `^`-chain rather than a plain variable) isn't
supported, matching the existing restriction that `new` into a
class-typed field through `^` has for a plain `new(head^.next)` too.

### Destructors

```pascal
type
    TFile = class
    public
        destructor Destroy;
    end;
    TLoggedFile = class(TFile)
    public
        destructor Destroy;
    end;
var
    lf: TLoggedFile;
    f: TFile;

destructor TFile.Destroy;
begin
    writeln('closing the file');
end;

destructor TLoggedFile.Destroy;
begin
    writeln('logging: about to close');
    inherited;
end;

begin
    new(lf);
    f := lf;
    dispose(f);   { dispatches to TLoggedFile.Destroy, which chains to
                    TFile.Destroy via 'inherited' - prints "logging:
                    about to close" then "closing the file" }
end.
```

- `destructor Name;` — an alternative to `procedure`/`function` when
  declaring a class method, marking it as the class's ONE designated
  destructor: no parameters, no return value. `dispose(c)` automatically
  calls it, dispatched dynamically exactly like an ordinary virtual
  method call, before actually freeing the instance's memory.
- **A class hierarchy has at most one destructor.** A subclass overrides
  the inherited one by re-declaring the SAME name with `destructor`
  again (call `inherited;`/`inherited Destroy;` inside it to chain
  cleanup up to the ancestor, exactly like overriding any other method —
  nothing chains automatically). Declaring an independently-named SECOND
  destructor, or overriding/introducing one with a mismatched kind
  (`procedure` instead of `destructor`, or vice versa), is a compile
  error.
- **A destructor is an ordinary method for every other purpose** —
  calling it directly (`c.Destroy;`) works outside of `dispose()` too
  (the instance stays alive; only its cleanup code runs), and a
  `private` destructor is only reachable through `dispose()` — an
  explicit external `c.Destroy;` is still rejected by the same
  visibility rule an ordinary private method already has.
- `class destructor`/`destructor ... abstract;` are both rejected — a
  destructor is inherently instance-lifecycle, and (being never
  overridable) a class method could never get an implementation.
- `dispose()` only calls the destructor for a plain variable, local, or
  `var` parameter target — `dispose(head^.next)` (a `^`-deref chain) is
  rejected with a compile error when the class has a destructor
  (unchanged, unrestricted, when it doesn't) — assign to a plain
  variable first.
- **Never `dispose()` `self` (or any alias of the same instance) from
  inside that instance's own destructor.** `dispose` has no
  already-freed check — only nil/out-of-range are guarded — so disposing
  the same instance twice (whether by hand or via a self-dispose during
  teardown) corrupts the free list, letting a later `new()` hand the
  same memory to two unrelated live pointers.

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
- The upcast compatibility above is checked **statically**, from the
  accessing expression's own declared type at compile time - but the
  method actually invoked is chosen **dynamically**, from the calling
  instance's own runtime type tag (see "Virtual dispatch" below), not
  the accessing expression's declared type.
- Multiple levels of inheritance work the same way, recursively — each
  class's own field/method lists are already fully flattened by the
  time a further subclass inherits from it.

### Sealed classes

```pascal
type
    TShape = class
        name: integer;
    end;
    TCircle = class sealed(TShape)
        radius: real;
    end;
    { TSquare = class(TCircle) end;  <- compile error: TCircle is sealed }
var
    c: TCircle;
begin
    new(c);
    c.radius := 2.0;
    dispose(c);
end.
```

`class sealed ... end;` (or `class sealed(TParent) ... end;`, combining
sealing with inheriting from a non-sealed parent) marks a class as
unable to be subclassed. Any later `class(TCircle) ... end;` is a
compile-time error naming the sealed class:

```
Cannot inherit from sealed class 'TCircle'
```

The `sealed` modifier itself is never inherited — it's checked once,
only at the point a class is used as *someone else's* parent, so a
sealed class's own ancestor (if any) is unaffected, and a sealed class
can still freely use every other class feature (fields, methods,
properties, class members, abstract methods, a destructor) exactly as
an unsealed class would. Declaring an abstract method inside a sealed
class is legal but self-defeating (the class can then be neither
subclassed nor, being abstract, instantiated) — not specially rejected,
the same way Delphi itself allows the combination.

### Virtual dispatch

```pascal
type
    TShape = class
        function Area: real;
    end;
    TCircle = class(TShape)
        radius: real;
        function Area: real;   { overrides TShape's own }
    end;
var
    s: TShape;
    c: TCircle;

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
    c.radius := 2.0;
    s := c;                    { upcast - s is statically typed TShape }
    writeln(s.Area);            { prints 12.56636 - TCircle's own Area,
                                   NOT TShape's, even though s's
                                   DECLARED type is TShape }
    dispose(c);
end.
```

Every method call dispatches through the calling instance's own hidden
runtime type tag (see above), not the accessing expression's static
type — genuine virtual/dynamic dispatch, Java-style: every method is
implicitly "virtual", with no separate `virtual`/`override` keyword
needed (there's no ambiguity to resolve, since a class can only inherit
from one ancestor and an override must match its inherited signature
exactly). This is what makes the example above print the *circle's*
area through a *shape*-typed reference: `s.Area` doesn't look at what
`s` was *declared* as, it looks at what `s` currently *holds*.

Mechanically, each class has its own **vtable** — one slot per method,
populated once at program startup (before any user code runs) with
every method's actual entry address. A method's slot number is its
position within its declaring class's own method list, which inheritance
flattening (above) already guarantees stays the same across an entire
ancestor/descendant hierarchy, so a single slot number is valid for
every class in a hierarchy without any extra bookkeeping. A call site
reads the calling instance's own tag, looks up that class's own vtable
row at the resolved slot, and calls through the address found there.

A method header declared but never given a body anywhere is still legal
on its own (see `test_class_basic.pas`) — it simply gets no vtable
entry; the existing compile-time check at any actual call site (`'X.Y'
doesn't have a body yet`) already prevents that entry from ever being
read.

### `inherited`

An overriding method can reach its ancestor's own implementation with
`inherited`, in either of two forms:

```pascal
type
    TShape = class
        name: integer;
        function Describe: integer;
        procedure SetName(n: integer);
    end;

    TCircle = class(TShape)
        radius: integer;
        function Describe: integer;
        procedure SetName(n: integer);
    end;

function TShape.Describe;
begin
    Describe := name * 10;
end;

procedure TShape.SetName;
begin
    name := n;
end;

function TCircle.Describe;
begin
    Describe := inherited Describe() + radius;   { explicit form }
end;

procedure TCircle.SetName;
begin
    inherited;                                   { bare form }
end;
```

- **`inherited MethodName(args)`** — an explicit, direct (non-virtual)
  call to whatever the enclosing method's class's *parent* provides for
  `MethodName`, with explicit arguments. `MethodName` doesn't have to
  match the enclosing method's own name. Parens are optional for a
  zero-argument call, exactly like an ordinary method call
  (`inherited SomeMethod;` and `inherited SomeMethod();` are the same
  thing) — reuses the same argument-list grammar/type-checking an
  ordinary `c.Method(args)` call already goes through.
- **Bare `inherited;`** — shorthand for "call the ancestor's version of
  the *currently executing* method, forwarding this method's own
  parameters unchanged." Always well-typed: an override is required to
  match its inherited signature exactly (see above), so the currently
  executing method's own parameters are guaranteed to match what it's
  forwarding them to.
- Usable in either statement or expression position — `inherited;` and
  `inherited MethodName(args)` both work as a plain statement
  (discarding an unused function result, like any other method-call
  statement) or as part of an expression (`x := inherited GetValue() + 1;`,
  `x := inherited;`) when the target is a function.
- Unlike an ordinary `c.Method(args)` call, `inherited` needs no vtable
  lookup at all — inheritance is already fully flattened at declaration
  time (see above), so the parent's own method list already resolves to
  whichever ancestor *actually* implements a given method, however many
  levels up that really is. A direct, compile-time-resolved call, not a
  dynamically dispatched one.
- `inherited` (either form) is only valid inside a class method body,
  and only when that class has a parent — `inherited` in a class with
  no ancestor, or outside any method body entirely, is a compile error.
  Bare `inherited;` additionally requires the enclosing method to
  actually be an override — if it doesn't correspond to anything the
  parent declares, that's a compile error too (name the target
  explicitly instead: `inherited MethodName(...)`).
- Same ordering constraint as an ordinary method call: the target
  method needs a body declared *somewhere* in the file by the time
  `inherited` referencing it is parsed (`'X.Y' doesn't have a body
  yet`) — not a new limitation, the same one ordinary method calls
  already have.

### `private`/`protected`/`public`

```pascal
type
    TCounter = class
    private
        count: integer;
        procedure Bump;
    protected
        procedure ResetToDefault;
    public
        procedure Inc3;
        function Value: integer;
    end;
    TResettableCounter = class(TCounter)
        procedure Reset;
    end;

procedure TCounter.Bump;
begin
    count := count + 1;
end;

procedure TCounter.ResetToDefault;
begin
    count := 0;              { fine - protected, declaring class itself }
end;

procedure TCounter.Inc3;
begin
    Bump; Bump; Bump;        { fine - self-shorthand, same class }
end;

function TCounter.Value;
begin
    Value := count;
end;

procedure TResettableCounter.Reset;
begin
    ResetToDefault;          { fine - protected, inherited by a descendant }
end;

var c: TResettableCounter;
begin
    new(c);
    c.Inc3;
    writeln(c.Value);        { 3 }
    c.Reset;
    writeln(c.Value);        { 0 }
    c.count := 0;            { Compile Error: 'count' is a private
                                field of class 'TCounter' and can't be
                                accessed here }
end.
```

- `private`/`protected`/`public` are section markers inside a
  `class ... end;` body — every field/method declared until the next
  marker (or the class's own `end`) takes on that visibility. Default
  (no marker at all) is `public`, so an existing class using none of
  these keywords is completely unaffected.
- **`private`**: only the *declaring* class's own methods can access
  it — not even a subclass's methods can.
- **`protected`**: the declaring class's own methods, *and* every
  (transitively) descendant class's own methods, can access it —
  everywhere else it's exactly as inaccessible as `private`. There's no
  distance limit: a member declared `protected` on a base class is
  still reachable from a grandchild, great-grandchild, and so on down
  the hierarchy (see `examples/test/protected/test_protected_grandchild.pas`).
- **Per-class, not per-instance**: any method of `TFoo` can read/write
  *any* `TFoo` instance's private/protected members, not just `self`'s
  own (`if self.x = other.x then ...` is fine from inside a `TFoo`
  method).
- A constructor (`new(c, Init(args))`) isn't a special case — a
  private/protected `Init` follows the same rule as any other
  private/protected method. A `protected` `Init`, in particular, is a
  common way to force construction only through some other, `public`
  factory method or a descendant's own constructor
  (`examples/test/protected/test_protected_inherited_init.pas` calls an
  ancestor's protected `Init` via `inherited`).
- **Section ordering**: this compiler's class grammar parses all
  fields first, then all methods (not Pascal's free interleaving of
  the two) — `private`/`protected`/`public` sections work within each
  of those two groups (all field-visibility sections, then all
  method-visibility sections), not interleaved field/method-by-
  field/method the way real Pascal allows.
- Overriding an inherited *private* or *protected* method is still
  permitted syntactically — this only checks access (reading/writing a
  field, calling a method), not override eligibility.

**Not implemented yet:**

- **Chaining off a method call's result** (`c.GetOther().field`) — a
  method call is always the terminal step of an access chain.
- **Nested procedure/function declarations inside a method body** — a
  method body doesn't support its own nested subroutines yet, unlike an
  ordinary procedure.
- **Chaining off an array-field element access** (`self.items[i].field`
  or `self.items[i]^.next`) — always the terminal step of an access
  chain, like a method call's result.
- **Reading/writing a nested-record field as a whole** (`c.center`
  alone) — always requires naming a leaf field (`c.center.x`).
- **`new()` into a class-typed field reached through an explicit `^`**
  (e.g. `new(head^.next)` where `next`'s type is a class) — allocate
  into a plain class variable first, then assign it into the field.
- **Multiple inheritance** — doesn't exist even as a plan yet beyond
  the scoping note. Delphi's `strict private`/`strict protected`
  visibility levels also don't exist — only `private`/`protected`/
  `public` (see above).

### Properties

```pascal
type
    TCircle = class
    private
        FRadius: real;
        procedure SetRadius(r: real);
    public
        function GetArea: real;
        property Radius: real read FRadius write SetRadius;
        property Area: real read GetArea;
    end;

procedure TCircle.SetRadius;
begin
    FRadius := r;
end;

function TCircle.GetArea;
begin
    GetArea := 3.14159 * FRadius * FRadius;
end;

var c: TCircle;
begin
    new(c);
    c.Radius := 5.0;             { calls SetRadius(5.0) }
    writeln(c.Radius:0:2);       { reads FRadius directly - 5.00 }
    writeln(c.Area:0:2);         { calls GetArea - 78.54 }
end.
```

- `property Name: Type read ReadTarget [write WriteTarget];` — a named
  member that reads and writes like a field at the call site, while
  actually routing through a field or a method.
- `ReadTarget` is either a field (a direct read, no call) or a
  zero-argument function (a *getter*, called with no `()` needed) whose
  return type matches the property's declared type exactly.
- `WriteTarget`, if present, is either a field (a direct write) or a
  one-argument, non-`var` procedure (a *setter*) whose parameter type
  matches the property's declared type exactly. Omitting `write`
  entirely makes the property read-only — assigning to it is a compile
  error.
- No widening between a target's own declared type and the property's
  declared type (exact match required at declaration time) — contrast
  with an ordinary assignment through the property itself, which *does*
  widen an integer literal/expression to `real` exactly like a plain
  `real` field would.
- **Property visibility governs access to the property itself** — the
  underlying field's/method's own `private`/`protected`/`public` is not
  consulted once reached through the property. A `public` property may
  front a `private` field or setter (see the worked example above:
  `FRadius` and `SetRadius` are both `private`, but `Radius` itself is
  `public`).
- **Self-shorthand** (a bare `Radius`/`Radius := x` inside another
  method of the same class) works exactly like it does for an ordinary
  field or method.
- **Inheritance**: a subclass sees every property its ancestors
  declared, exactly like fields and methods. Unlike a method, a
  property can't be overridden in a subclass — redeclaring an inherited
  property's name is a duplicate-declaration compile error.
- **Section ordering**: properties are parsed as a third group, after
  all fields and all methods (see `private`/`public`'s own "Section
  ordering" note above) — a property's read/write target must be a
  field or method already declared earlier in the same class body.
- `inherited` does not apply to a property directly — there's no
  "inherited property access" syntax. A property's read/write always
  routes through whichever method (or field) it names on *this* class;
  reaching an ancestor's own implementation of that method still means
  spelling out `inherited MethodName(...)` directly, not through the
  property.

**Not implemented yet:**

- **Indexed properties** (`property Items[i: integer]: T read GetItem
  write SetItem;`) — an array-like property backed by a parameterized
  getter/setter.
- **`default` array property** — using an object directly with `[]`
  indexing via one designated indexed property.
- **Indexed class properties** and `default` array properties — same gap
  as above, on the class-level form (see [Class members](#class-members)).
- **Property overriding** in a subclass.

### Class members

```pascal
type
    TCounter = class
    private
        class var FTotalInstances: integer;
    public
        FVal: integer;
        class function GetTotal: integer;
        procedure Bump;
        class property Total: integer read GetTotal;
    end;
var c1, c2: TCounter;

function TCounter.GetTotal;         { NOT 'class function' - see note below }
begin
    GetTotal := FTotalInstances;
end;

procedure TCounter.Bump;
begin
    FVal := FVal + 1;
    FTotalInstances := FTotalInstances + 1;   { bare self-shorthand }
end;

begin
    new(c1);
    new(c2);
    c1.Bump;
    c2.Bump;
    writeln('Total = ', TCounter.Total);      { 2 }
end.
```

- **`class var Name: Type;`** declares one shared storage location per
  class hierarchy, not per instance — every instance of `TCounter`, and
  every instance of any of its descendants, sees the same
  `FTotalInstances`. Comma-separated names and subrange types work
  exactly like an ordinary field group.
- **`class procedure/function Foo(...);`** declares a *true* class
  method (Delphi terminology) — callable as `TMyClass.Foo(...)` with no
  instance and no implicit `self`; the method body can't reference `self`
  or any instance field/method, even by bare name. **The body definition
  does not repeat `class`** — `function TCounter.GetTotal;` is correct,
  matching how an ordinary method's own body definition never repeats
  its parameter list either (the header, parsed once at declaration
  time, already recorded that this is a class method).
- **`class property Name: T read GetX [write SetX];`** mirrors an
  ordinary property, but its read/write target must itself be a class
  var or a class method — mixing kinds (a class property backed by an
  instance method, or an instance property backed by a class method) is
  a compile error in both directions.
- **Access class members through the class name**: `TCounter.Total`,
  `TCounter.GetTotal`, `TCounter.FTotalInstances` — never through an
  instance (`c.Total` is a compile error naming the correct
  `TCounter.Total` form instead). **Bare self-shorthand** (`FTotalInstances`
  with no qualifier) still works from inside any method of the class —
  instance method or class method alike, as `Bump` above shows.
- **Inherited by reference, not by copy**: a subclass doesn't get its
  own separate `class var` — it shares the exact same storage as its
  ancestor. Writing through the subclass's name and reading through the
  ancestor's name (or vice versa) sees the same value.
- **Class methods are never overridable** — no vtable slot is ever
  allocated for one, so redeclaring an inherited class method in a
  subclass (even with a matching signature) is a duplicate-declaration
  compile error, not an override. `inherited` is rejected outright
  inside a class method body, for the same reason.
- **Section ordering**: exactly like properties, class vars/methods/
  properties are folded into the same three field/method/property groups
  an ordinary class body already parses in order — a `class var` can
  appear anywhere in the field group, a class method anywhere in the
  method group, and so on.
- `new(c, SomeClassMethod(...))`'s constructor-call sugar rejects a class
  method as the constructor — it has no instance to initialize.

**Not implemented yet:**

- **Instance-qualified access to a class member** (`c.Total`) — only the
  `TCounter.Total` form works; see the bullet above.
- **Class method virtual dispatch/overriding** — see the bullet above.

### Abstract methods

```pascal
type
    TShape = class
    public
        function Area: real; abstract;
        function Describe: string;
    end;
    TCircle = class(TShape)
    private
        FRadius: real;
    public
        function Area: real;
        procedure SetRadius(r: real);
    end;
var
    c: TCircle;
    shape: TShape;

function TShape.Describe;
begin
    Describe := 'a shape';
end;

function TCircle.Area;
begin
    Area := 3.14159 * FRadius * FRadius;
end;

procedure TCircle.SetRadius;
begin
    FRadius := r;
end;

begin
    new(c);
    c.SetRadius(2.0);
    shape := c;
    writeln(shape.Describe, ', area = ', shape.Area:0:2);  { a shape, area = 12.57 }
end.
```

- `function/procedure Name(...); abstract;` — a trailing `abstract;`
  modifier after a method header declares it with NO body, ever.
  **Deliberately just `abstract;`, not Delphi's `virtual; abstract;`** —
  every instance method in this compiler is already always virtually
  dispatched (no `virtual`/`override` keyword exists at all — see
  [Classes](#classes) above), so a separate `virtual` would be pure
  noise.
- Calling an abstract method through a reference whose *static* type is
  the abstract-declaring class — exactly `shape.Area` above, where
  `shape`'s declared type is `TShape` — is allowed and dispatches
  dynamically, exactly like calling any other virtual method: at
  runtime it always resolves to whichever concrete class the variable
  actually holds (`TCircle`, in the example), never to `TShape` itself.
  This is the entire point of the feature — without it, a base class
  with no real implementation of `Area` couldn't be called through a
  base-typed reference at all.
- **A class with any unresolved abstract method (including one merely
  inherited, never overridden) can't be instantiated** — `new()` on it
  is a compile error naming the specific abstract method blocking it.
  This propagates through inheritance automatically: a subclass that
  doesn't override an inherited abstract method is *itself* still
  blocked from instantiation, and so on down the hierarchy until some
  class actually provides a concrete implementation.
- **A TRUE class method (`class procedure`/`class function`) can never
  be `abstract`** — rejected at declaration. Class methods are never
  overridable at all (no vtable slot, ever), so an abstract one could
  never get an implementation anywhere.
- **An abstract method can't have a body given in the SAME class that
  declared it abstract** — `function TShape.Area; begin ... end;` right
  after declaring `Area` abstract in `TShape` itself is a compile
  error. A subclass overriding it concretely (`function TCircle.Area;
  begin ... end;`, as above) is unaffected — that's the normal,
  expected way to resolve an abstract method.
- **`inherited AbstractMethod(...)` is rejected** — there's no
  ancestor implementation for `inherited` to reach, by definition.
- A class may freely mix abstract and ordinary concrete methods —
  only the still-abstract ones block instantiation; concrete methods
  (including ones that call an abstract method internally, expecting a
  subclass to have provided it by the time any instance actually
  exists) work completely normally.
- A subclass may re-declare an inherited abstract method as `abstract`
  again (still deferring, still no body) — useful in a multi-level
  hierarchy where an intermediate class isn't meant to be concrete
  either.

**Not implemented yet:**

- **No `class abstract`/type-level keyword** — a class's "abstract-ness"
  is entirely emergent from having at least one unresolved abstract
  method (matching real Delphi, which has no such keyword either), not
  a separate declaration.

### `is`/`as`

```pascal
type
    TShape = class
    public
        function Area: real;
    end;
    TCircle = class(TShape)
    private
        FRadius: real;
    public
        function Area: real;
        procedure SetRadius(r: real);
    end;

function TShape.Area;
begin
    Area := 0.0;
end;

function TCircle.Area;
begin
    Area := 3.14159 * FRadius * FRadius;
end;

procedure TCircle.SetRadius;
begin
    FRadius := r;
end;

var shape: TShape; c: TCircle;
begin
    new(c);
    c.SetRadius(5.0);
    shape := c;                  { upcast - shape's STATIC type is TShape }
    if shape is TCircle then     { True - shape's ACTUAL runtime class is TCircle }
        writeln('It is a circle');
    c := shape as TCircle;       { succeeds - same runtime check as 'is' }
    writeln(c.Area:0:2);         { 78.54 }
end.
```

- `obj is TFoo` tests `obj`'s *actual runtime class* (or a descendant of
  it) — not its static/declared type. `TFoo` must be a class type.
- `obj as TFoo` performs the same runtime check; on success it yields
  `obj`, now usable/assignable as `TFoo`. On failure, it raises a
  catchable exception (`"Cannot cast to 'TFoo'"`), reaching the
  innermost enclosing `try`/`except` exactly like an explicit `raise`
  would (see [`try` / `except` / `raise`](#try--except--raise)) — or an uncaught,
  fatal `VM Runtime Error: Unhandled exception: ...` if there is no
  enclosing `try`.
- `nil is TFoo` is always `False`. `nil as TFoo` always yields `nil`,
  never raises.
- Both operators reject, **at compile time**, a combination where
  neither operand's class could ever be an ancestor or descendant of the
  other — `is`/`as` between two classes with no possible relationship
  can never succeed, so it's caught before the program even runs, the
  same way an incompatible `=`/`<>` comparison between two unrelated
  class-typed operands already is.
- This is why the feature exists at all: reference-assignment lets an
  ancestor-typed variable hold a subclass's pointer value (`shape := c;`
  above) — an expression's *static* type and the object's *actual*
  runtime class can diverge, and only `is`/`as` can tell them apart.

**Not implemented yet:**

- **The failure message names only the target class**, not the actual
  runtime class being cast from — no mechanism exists to map a runtime
  class tag back to a class-name string.
- **`is`/`as` against a non-class pointer type** (`type PFoo = ^integer;`)
  — class types only.
- **Chaining** — `is`/`as` are a single, one-shot check per expression,
  the same precedence tier as `in`; there's no `obj is TFoo is TBar`.

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

### `const` parameters

```pascal
type
    TPoint = class
        x: integer;
    end;

procedure describe(const p: integer);
begin
    writeln('value: ', p);
    { p := p + 1;  <- compile error: cannot assign to 'const' parameter 'p' }
end;

procedure touch(const c: TPoint);
begin
    c.x := 77;   { legal - writes through c, not to c itself }
    { c := nil;  <- compile error: cannot assign to 'const' parameter 'c' }
end;

var
    n: integer;
    pt: TPoint;
begin
    n := 5;
    describe(n);

    new(pt);
    pt.x := 1;
    touch(pt);
    writeln(pt.x);  { 77 }
    dispose(pt);
end.
```

`const name: type` passes a parameter **by reference**, exactly like
`var` (same underlying mechanism, same restrictions on the argument -
it must be a variable, and its type must exactly match), but the
callee is never allowed to write to it: not a direct assignment, not
`inc`/`dec`, not `new()` on it, and not forwarding it as another call's
`var`/`out` argument (forwarding it as another call's own `const`
argument is fine). Each is a clear compile-time error naming the
parameter.

**Shallow, matching real Pascal**: `const` only protects the parameter
itself, not what it points to - a `const` pointer/class parameter's own
*field* can still be written through it (`c.x := 77;` above), only
reassigning `c` itself (`c := someOtherInstance;`) is rejected.

**Not supported yet**: `const` inside a procedural/functional
parameter's own inline signature, or inside a named procedural type's
signature (`type TProc = procedure(const x: integer);`) - both are a
clear compile-time error. `const` on a whole record, an array element,
or as a `for`-loop counter share the same restrictions `var` already
has, for the same reason (see above).

### `out` parameters

```pascal
procedure makeIt(out y: integer);
begin
    y := 99;
end;

var
    a: integer;
begin
    makeIt(a);
    writeln(a);  { 99 }
end.
```

`out name: type` is passed by reference, exactly like `var` - runtime-
identical, in fact, with no separate mechanism of its own. The only
difference is what it documents and one extra compile-time check: an
`out` parameter tells the reader "the callee is expected to write this,
not read whatever the caller passed in" - and if the callee's body
never assigns it before returning, on any path, a warning is printed:

```
warn.pas:5: Warning: 'out' parameter 'y' is never assigned a value in procedure 'Forgetful'
```

Same flow-insensitive limitation as every other warning this compiler
emits (see [Warnings](#warnings)) - "assigned somewhere in the body",
not "assigned on every path" - and it's only ever a warning, never a
compile error; compilation still succeeds either way. Shares every
other restriction `const`/`var` already have (addressable-variable
arguments only, no whole records/array elements, not inside a
procedural/functional signature).

### Default parameter values

```pascal
procedure foo(x: integer; y: integer = 10);
begin
    writeln(x + y);
end;

begin
    foo(5);      { y defaults to 10 -> 15 }
    foo(5, 20);  { y explicit -> 25 }
end.
```

A trailing parameter can be given a default value with `= <const-expr>`.
A call that omits trailing arguments gets the declared default spliced
in as if the caller had typed it - pure call-site sugar; nothing about
codegen or type-checking needs to know the difference.

The default must be a **compile-time constant expression** - a literal,
an arithmetic/boolean expression over literals, or a reference to an
earlier `const` (never a parameter, local variable, or function call):

```pascal
const limit = 42;
procedure foo(x: integer = limit);       { fine - references an earlier const }
procedure bar(y: integer = 3 * 10);      { fine - folds to 30 }
```

Restrictions:

- **Trailing only.** Once one parameter has a default, every parameter
  after it must also have one.
- **One name per default.** `x: integer = 5` is fine; `x, y: integer =
  5` is a compile error - ambiguous whether both get `5` or just `y`.
  Give each its own group if only one needs a default.
- **Not on `var`/`const`/`out` parameters.** All three are passed by
  reference in this compiler (an address, not a value) - a default has
  no caller-side variable to take the address of. This is stricter than
  some other Pascal dialects, which allow defaults on `const` because
  their `const` isn't always by-reference under the hood; here it always
  is, so the restriction follows directly from the implementation, not
  from a language-design choice.
- **Not on array/record parameters** - there's no array/record literal
  syntax to write a default with.
- **Not on subrange-typed parameters** (`type TRange = 1..10; procedure
  p(x: TRange = 5)`) - a documented v1 scope cut, not a silent gap.
- **Not inside a procedural/functional parameter's own inline signature
  or a named procedural type** - the same restriction `const`/`out`
  already have; only a real procedure/function/method declaration gets
  a body to compile defaults against.

**A default lives on the forward declaration when one exists** - a
completing body never re-lists parameters at all (this compiler's
existing forward-declaration convention), so there's nothing to
redeclare or contradict:

```pascal
procedure greet(name: string; times: integer = 2); forward;

procedure greet;  { no parameter list here - same as any forward completion }
var i: integer;
begin
    for i := 1 to times do
        writeln(name);
end;
```

**A class method override may declare its own, different default** from
the method it overrides. Which default applies is resolved **statically**
- against whichever type the call site's own expression is declared as
- exactly like C++/Java default arguments, even though the method body
that actually runs is still chosen dynamically (every instance method in
this compiler is always virtually dispatched - see
[Classes](#classes)):

```pascal
type
    TBase = class
        procedure greet(n: integer = 1);
    end;
    TChild = class(TBase)
        procedure greet(n: integer = 9);
    end;
var b: TBase;
    c: TChild;
begin
    new(c);
    c.greet;    { c is statically TChild -> default 9, runs TChild's body }
    b := c;
    b.greet;    { b is statically TBase -> default 1, still runs TChild's body }
end.
```

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
- **No named, storable procedural type here** — the signature is always
  written out inline at the declaration site, matching standard Pascal.
  For a `type TProc = procedure(x: integer);`-style named, reusable,
  storable type — a genuinely different, more general mechanism, not an
  extension of this one — see "Procedural types" below.

### Procedural types

```pascal
type
    TIntProc = procedure(x: integer);
    TIntFunc = function(x: integer): integer;
var
    p: TIntProc;
    f: TIntFunc;

procedure PrintDouble(x: integer);
begin
    writeln(x * 2);
end;

function Square(x: integer): integer;
begin
    Square := x * x;
end;

begin
    p := PrintDouble;
    p(21);                        { 42 }

    f := Square;
    writeln(f(6));                { 36 }

    p := nil;
    if p = nil then
        writeln('p is nil');
end.
```

`type TProc = procedure(x: integer); TFunc = function(x: integer):
real;` declares a NAMED, reusable, storable procedural type — Turbo
Pascal's non-standard extension, genuinely different from (and sharing
no storage mechanism with) an inline functional/procedural *parameter*
above. Once declared, the type name works as an ordinary scalar type
everywhere one is accepted — `var`/local declarations, `var`
parameters — since its runtime representation is a plain int (a
top-level procedure/function's entry address, or `-1` for `nil`,
exactly like a pointer).

- **Assign a top-level (non-nested) procedure/function by its bare
  name**, or `nil`, or copy another variable already holding the same
  procedural type — `p := PrintDouble;`, `p := nil;`, `p2 := p1;`. The
  actual procedure/function's signature must match the declared
  procedural type exactly (same rules as a functional/procedural
  parameter's own signature match, above): same is_function-ness, same
  return type if a function, same parameter count/types/`var`-ness.
- **`nil` is supported** — assignable, and comparable with `=`/`<>`
  against another value of the same procedural type or `nil` itself,
  exactly like a pointer.
- **Call it with `(args)`** — `p(21)`, `x := f(6)`. A `function`-typed
  value's call can be used as an expression; a `procedure`-typed one can
  only be a statement, same as any ordinary procedure.
- **A bare reference (no explicit call parentheses) means different
  things in different contexts**, to avoid an ambiguity a single fixed
  rule can't resolve: used as a whole **statement**, it's a call
  (`p;` — matching how any other zero-argument call is already allowed
  bare); used anywhere a **value** is expected instead — a comparison
  (`p = nil`), an assignment's RHS (`p2 := p1;`) — it's the stored value
  itself, never an implicit call. Calling a zero-argument procedural
  value from inside a larger expression always needs explicit `()` to
  disambiguate from a value-read.
- **A function (or class method) can return a named procedural type**
  as its result:
  ```pascal
  type
      TProc = function(x: integer): integer;
  var
      h: TProc;

  function Double(x: integer): integer;
  begin
      Double := x * 2;
  end;

  function GetHandler: TProc;
  begin
      GetHandler := Double;      { bare reference - same rule as any
                                    other procedural-type assignment }
  end;

  begin
      h := GetHandler();         { explicit '()' required - see below }
      writeln(h(5));              { 10 }
  end.
  ```
  Assigning to the function's own name inside its body follows the
  exact same bare-reference-vs-call rule as any other procedural-type
  assignment target, described above. **Calling such a function needs
  explicit `()` even with zero arguments** — `h := GetHandler;` (no
  parens) is still read as "take a bare reference to `GetHandler`
  itself" (and fails, since `GetHandler`'s own signature — no
  arguments, returns `TProc` — doesn't match `TProc`'s shape), never as
  an implicit call. This context is inherently ambiguous between the
  two readings, so `()` is a deliberate, explicit disambiguator here,
  unlike an ordinary expression context's usual "bare zero-arg call"
  convention. The call's returned value can also be passed directly as
  an argument to another procedural parameter (`Apply(GetHandler(), 5)`).
  Chaining a further call directly off the result in one expression
  (`GetHandler()(5)`) isn't supported yet — assign to a variable first.
  Calling a *class method* that returns a procedural value
  (`h := f.MakeHandler();`) works the same way.
- **A record or class field can itself have a named procedural type**:
  ```pascal
  type
      TProc = function(x: integer): integer;
      TFoo = class
          handler: TProc;
      end;
  var
      f: TFoo;

  function Double(x: integer): integer;
  begin
      Double := x * 2;
  end;

  begin
      new(f);
      f.handler := Double;    { bare reference, same rule as any other
                                 procedural-type assignment target }
      writeln(f.handler(5));  { 10 - explicit '()' to call it }
      dispose(f);
  end.
  ```
  Works the same way for a plain (non-class) record field, an array-
  typed field (`data: array[0..3] of TProc;`), a procedural field
  nested inside another record field, unqualified `self.`-shorthand
  access from inside a method, and passing a field's value directly as
  an argument to another call (a plain function's or a class method's
  own procedural parameter).

### Lambda literals

```pascal
type
    TCmp = function(a, b: integer): boolean;

var
    cmp: TCmp;

begin
    cmp := function(a, b: integer): boolean begin exit(a < b); end;
    writeln(cmp(3, 5));   { TRUE }
end.
```

An anonymous `function(...)...end` / `procedure(...)...end` expression,
usable anywhere a bare top-level procedure/function name is already
accepted as a procedural value — assigned to a procedural-typed
variable/local/record-or-class-field/array element, returned from a
function, or passed directly as an argument to a functional/procedural
parameter:

```pascal
function Apply(function f(n: integer): integer; v: integer): integer;
begin
    Apply := f(v);
end;

begin
    writeln(Apply(function(n: integer): integer begin exit(n * n); end, 5));  { 25 }
end.
```

- **Parameters are scalar only** (optionally `var`), matching the exact
  same restriction every OTHER value used as a procedural value already
  has (`proc_has_only_scalar_params`) — no arrays, records, or nested
  procedural parameters. No default parameter values.
- **Set the return value with `exit(value);`** — the same statement
  already used to return early from an ordinary function. A lambda has
  no user-writable name of its own to assign to (unlike an ordinary
  function's `FuncName := value;` form), so this is the only way.
- **No local `var` section, and no nested procedure/function
  declarations, inside a lambda body** — a lambda's own declaration part
  is just its parameter list (and return type, for a function-lambda).
- **No capture.** A lambda body can read/write its own parameters,
  any global, and (see below) an enclosing procedure's array or
  `static` local — but referencing an *ordinary* local or parameter of
  whatever procedure the lambda text happens to sit inside is a
  compile-time error, not a runtime trap:
  ```pascal
  function MakeAdder(n: integer): TF;
  begin
      { Compile error: lambda body can't reference 'n' - it's an
        ordinary parameter of the enclosing function MakeAdder. }
      MakeAdder := function(x: integer): integer begin exit(x + n); end;
  end;
  ```
  This is the one thing a lambda literal here can't do that a real
  closure could — capturing a parameter/local *by value* (the classic
  `MakeAdder(n)` factory idiom) isn't supported. A lambda literal is
  never a closure: it carries no captured environment, just a plain
  runtime code address, exactly like an ordinary top-level procedure/
  function used as a procedural value.
- **An enclosing procedure's local *array* or `static` local is still
  reachable**, and this is *not* considered a capture — both already
  compile to an ordinary hidden global reference under the hood (see
  "Nested procedures and functions" above), with nothing for a lambda's
  calling convention to depend on:
  ```pascal
  procedure Outer;
      var
          static callCount: integer;
          tick: TTick;
  begin
      tick := procedure begin
          callCount := callCount + 1;
      end;
      tick; tick; tick;    { callCount: 1, 2, 3 }
  end;
  ```
- **A lambda literal can itself appear inside another lambda's body**
  (as an argument, an assignment, etc.) — ordinary recursive parsing,
  no special support needed.
- **Not supported yet**: an immediately-invoked lambda
  (`(function(...) ... end)(5)`) or otherwise chaining a call directly
  off a lambda expression — assign it to a variable first, matching the
  same existing cut for `GetHandler()`'s own returned value above.

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
  inside its body — `factorial := ...` above — or, equivalently,
  `exit(value);` (see [`exit` and `halt`](#exit-and-halt)), which sets
  it *and* returns immediately in one step. If a function's body never
  assigns to its own name (through either form) before falling off the
  end, it returns a default value (`0` for `integer`/`boolean`/
  `char`-as-a-number, or an out-of-range value for `string`/`char` that
  will cleanly error if actually used — not silently wrong data).
- Reading the function's own name as an expression (to check the return
  value computed so far) isn't supported for a **scalar** return type —
  only assigning to it is. Inside its own body, using the bare name as
  an expression is treated as a call (usually a recursive one), not a
  read of the stored result.
- **A dynamic-array return type is the one exception** — reading (and
  indexing) the function's own name mid-body works, because it has to:
  `SetLength`/indexing/`Length`/`Copy` all need to read the array before
  (re)writing it, unlike a scalar, which never needs a read-back for the
  ordinary "compute and assign" pattern above. An explicit recursive
  call (`Build(n - 1)`, with parentheses) still means a call, exactly as
  for a scalar return type — only the bare, unparenthesized name reads
  the return value:
  ```pascal
  function Build(n: integer): array of integer;
  var sub: array of integer;
      i: integer;
  begin
      if n <= 0 then
          SetLength(Build, 0)
      else begin
          sub := Build(n - 1);               { explicit call - parens }
          SetLength(Build, Length(sub) + 1);   { read, via SetLength }
          for i := 0 to High(sub) do
              Build[i] := sub[i];               { read+write, indexed }
          Build[High(Build)] := n;
      end;
  end;
  ```
  See [Dynamic arrays](#dynamic-arrays) for everything else about the
  type itself, including a dynamic-array-typed **record/class field**
  (also supported, but a genuinely separate case from this one - see
  that section's own "Record and class fields").
- **A function can be called as a statement**, discarding its return
  value, exactly like a procedure call:
  ```pascal
  bump;             { call bump, ignore what it returns }
  x := bump;         { call bump, use what it returns }
  ```
- The return type is scalar only (`integer`, `boolean`, `string`,
  `char`) — same restriction as parameters and local variables — plus,
  as of the exception just above, a **dynamic array**.
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

## Units

A program can be split across files. A **unit** is a separate `.pas`
file declaring things another file can use:

```pascal
unit MathUtils;

interface

const
    Factor = 3;

function Square(x: integer): integer;

implementation

function Square;   { no parameter list/return type here - see below }
begin
    Square := x * x * Factor;
end;

end.
```

```pascal
program UsesIt;

uses MathUtils;

begin
    writeln(Square(4));   { 48 }
end.
```

- A unit file's own name is required to match its declared name exactly
  (`unit MathUtils;` must live in `MathUtils.pas`) — this is also how
  `uses` finds it: **same directory as the file containing the `uses`
  clause**, no separate include/search path.
- `uses Name1, Name2, ...;` is valid right after the main program's own
  heading (before `label`/`const`/`type`/`var`), or right after a unit's
  own `interface` keyword — **not** inside an `implementation` section.
  One dependency list per file.
- A unit has exactly two sections, in order: `interface` (what it
  declares) then `implementation` (procedure/function bodies, plus
  optionally more `const`/`type`/`var`/procedures of its own), each
  ending with the unit's own final `end.`.
- **A procedure/function declared in the interface needs no `forward`
  keyword** — the interface/implementation split *is* the forward
  declaration, unlike this compiler's explicit
  [`forward`](#procedures) elsewhere. Its implementation-section body
  then completes it exactly like completing any other forward
  declaration: **no parameter list or return type repeated** (see
  `function Square;` above) — this differs from standard Pascal units,
  which repeat the full signature in both places; it's a deliberate
  reuse of this compiler's one existing forward/complete mechanism
  rather than a second, parallel one.
- `const`/`type`/`var` sections work identically to the main program's
  own — including `type ... = class ... end;`: a class's fields and
  method headers go in the interface, its method bodies
  (`procedure TFoo.Method; ...`) in the implementation, same as any
  other class (see [Classes](#classes)).
- Two units that both `uses` a shared third one (a diamond dependency)
  merge that shared unit's declarations exactly once — no duplicate-
  declaration error. A cycle (`A` uses `B` uses `A`, directly or through
  more steps) is a compile error.
- **Visibility is enforced for procedures/functions and global
  variables**: something a unit declares only in its `implementation`
  section can't be referenced from outside that unit — not from the
  main program, and not from a different unit that merely `uses` it
  (even one that itself `uses` the declaring unit). The unit's own
  code, anywhere in its `implementation`, can always see its own
  private declarations. A reference from outside reads exactly like a
  genuinely undeclared identifier (`Unknown identifier`/`Undeclared
  procedure/function`), not a distinct "private" error.
  ```pascal
  unit MathUtils;
  interface
      function Square(x: integer): integer;    { public }
  implementation
      var callCount: integer;                  { private to this unit }
      procedure LogCall;                       { private to this unit }
      begin callCount := callCount + 1 end;
      function Square;
      begin
          LogCall;                             { fine - same unit }
          Square := x * x;
      end;
  end.
  ```
  A program `uses MathUtils;` can call `Square`, but referencing
  `callCount` or `LogCall` directly is a compile error. **Not yet
  enforced**: consts, types (including classes — a class declared in a
  unit's interface is fully visible everywhere regardless of anything
  declared only in that unit's implementation), enumerated
  types/values, and subrange types all stay visible everywhere
  regardless of which section declared them; so do record variables
  specifically (as opposed to plain scalar/array ones). Also unrelated:
  this doesn't give private symbols their own per-unit namespace — two
  units each privately declaring a same-named proc/var still collide as
  a duplicate declaration, exactly as before (there's no real per-unit
  name mangling, just a visibility check at reference sites) — a
  completely separate mechanism from class-level `private`/`public`
  (see [Classes](#classes)), which is unrelated and now also
  implemented.
- Not separate compilation: `uses Foo;` re-parses `Foo.pas` and merges
  its declarations into the same global tables the main program's own
  declarations use, every time it's named. There's no compiled unit
  format, `.obj`-style linking, or precompiled unit cache.
- A name a unit declares must be unique across the whole compile, same
  as any other global name — a unit and the main program (or two
  units) declaring the same name is the same "already declared" compile
  error as any other collision.

### `initialization` and `finalization`

A unit's `implementation` section can end with two optional sections
instead of going straight to `end.`:

```pascal
unit Logger;

interface

var
    LineCount: integer;

implementation

initialization
    LineCount := 0;
    writeln('Logger starting up');

finalization
    writeln('Logger shutting down, wrote ', LineCount, ' lines');

end.
```

```pascal
program UsesLogger;

uses Logger;

begin
    LineCount := LineCount + 1;
    writeln('doing work');
end.
```
Output:
```
Logger starting up
doing work
Logger shutting down, wrote 1 lines
```

- **`initialization`'s statements run once, automatically, before the
  main program's own `begin...end.` body** — no explicit call needed.
  **`finalization`'s statements run once, automatically, after** the
  main program's own body finishes normally.
- Both sections are optional and independent: a unit may have just
  `initialization`, just `finalization`, both, or neither.
- Neither section takes a `begin`/`end` wrapper — just a plain statement
  list, same as what's between `initialization`/`finalization` and the
  next keyword. No local `var` declarations are allowed here (same as
  standard Pascal) — declare any state the section needs as an ordinary
  unit-level `var` instead (like `LineCount` above).
- **With more than one unit, `initialization` sections run in
  unit-dependency order, and `finalization` sections run in the exact
  reverse order** — the same order `uses` itself resolves a diamond
  dependency in: a unit's own `initialization` always runs after every
  unit it (transitively) `uses`, and its `finalization` always runs
  before theirs.
  ```pascal
  { uses A, B;  where B itself uses A: }
  { initialization order:  A, B }
  { finalization order:    B, A }
  ```
- A unit used by more than one other unit (a diamond dependency) still
  only runs its `initialization`/`finalization` once, at the point it's
  first loaded — same one-copy guarantee `uses` already gives every
  other declaration.
- **`finalization` doesn't run if the program terminates via an
  unhandled runtime error** (an uncaught `raise`, an out-of-range index,
  etc.) — the same scope [`try`/`finally`](#try--finally) already has
  for anything outside its own handled exception; there's no
  program-wide unwind-and-clean-up mechanism.
- Only units have these sections — the main program itself doesn't;
  its own `begin...end.` body already fills that role.

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
- `uses` inside a unit's `implementation` section, and
  interface/implementation visibility enforcement for anything besides
  procedures/functions and global variables (consts, types, classes,
  enums, subranges, and record variables specifically all stay visible
  everywhere regardless of section) — see [Units](#units) above
  (`unit`/`interface`/`implementation`/`uses`, including units
  depending on units, and visibility for procs/vars, all work)

See the project README's Status section and
[docs/ROADMAP.md](ROADMAP.md) for the current plan.
