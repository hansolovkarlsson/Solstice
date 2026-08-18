---
title: 2. Variables and Types
parent: Pascal Tutorial
nav_order: 2
---

# Variables and Types

## Declaring variables

Every variable in Pascal has one fixed type, declared up front in a
`var` section before the program's `begin`:

```pascal
program Ch2a;
var
    age: integer;
    price: real;
    isReady: boolean;
    name: string;
    grade: char;
begin
    age := 30;
    price := 19.99;
    isReady := true;
    name := 'Ada';
    grade := 'A';

    writeln('Name: ', name);
    writeln('Age: ', age);
    writeln('Price: ', price);
    writeln('Ready: ', isReady);
    writeln('Grade: ', grade);
end.
```

```
Name: Ada
Age: 30
Price: 19.99
Ready: TRUE
Grade: A
```

`:=` is assignment — not `=`, which you'll meet in the next chapter as
the *comparison* operator instead. Every variable must be declared
before it's used anywhere; Pascal doesn't infer a variable's type from
what you first assign to it.

Notice `isReady` printed as `TRUE`, all caps — that's just how
`write`/`writeln` render a `boolean` value.

## The five core types

| Type | Keyword | Example literal | Holds |
|---|---|---|---|
| Integer | `integer` | `42`, `-7` | A whole number |
| Real | `real` | `3.14`, `2.0` | A floating-point number |
| Boolean | `boolean` | `true`, `false` | One of two values |
| String | `string` | `'hello'` | Text, any length |
| Char | `char` | `'a'` | Exactly one character |

A `string` literal uses single quotes, not double. To put an actual
single quote inside one, double it: `'it''s here'` prints as `it's
here`.

There are more types than these five — fixed-size arrays, records,
bounds-checked integer subtypes, and more — but this chapter's five
cover everything you need to write real programs, and every later
chapter builds on them. The full list is in the [Language
Reference]({{ site.baseurl }}/LANGUAGE.html#types) whenever you want it.

## Declaring several variables at once

If more than one variable shares a type, you can list them
comma-separated instead of repeating the type line:

```pascal
var
    a, b, total: integer;
```

## Constants

A `const` section declares a NAMED value that never changes — useful
for anything you'd otherwise have to repeat, or a magic number you want
to name once:

```pascal
program Ch2b;
const
    TaxRate = 0.08;
var
    a, b, total: integer;
    subtotal, tax: real;
begin
    a := 5;
    b := 7;
    total := a + b;
    writeln('total = ', total);

    subtotal := 100;
    tax := subtotal * TaxRate;
    writeln('tax = ', tax);
end.
```

```
total = 12
tax = 8
```

Notice `subtotal := 100;` — assigning a whole-number literal to a
`real` variable works fine; Pascal widens it automatically. The
reverse (assigning a `real` value to an `integer` variable) is NOT
automatic — you'll hit a compile error if you try it directly, since
that would silently lose the fractional part. You'll meet `trunc` and
`round`, the two ways to make that conversion explicit, in the next
chapter.

A `const` can't be assigned to — `TaxRate := 0.05;` would be a compile
error, not a runtime one, since the compiler already knows `TaxRate`'s
value can never change.

## Try it yourself

Add a `weight: real;` and a `unit: char;` to `Ch2a` above (careful —
`unit` collides with nothing in this dialect, but pick a name that
doesn't shadow one of the types you just learned, like `price2`, if you
want to be safe) and print both. Then try assigning a `real` value like
`3.5` directly to an `integer` variable and see the exact compile error
Pascal gives you — recognizing that error is worth more than avoiding
it, since you'll see it again the first time you forget this rule for
real.

Next: [Expressions and Operators](03-expressions-and-operators.html),
where `total := a + b;` above gets a proper explanation.
