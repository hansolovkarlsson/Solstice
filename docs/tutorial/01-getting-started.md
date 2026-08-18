---
title: 1. Getting Started
parent: Pascal Tutorial
nav_order: 1
---

# Getting Started

## What you'll need

Just a C11 compiler (Clang or GCC) and `make`. Nothing else — Solstice
has no external dependencies.

## Building the toolchain

```sh
git clone https://github.com/hansolovkarlsson/Solstice.git
cd Solstice
make
```

This builds everything into `bin/`, including the two tools this
tutorial uses:

- **`pascalc`** — compiles a `.pas` source file into a `.bin` bytecode
  image
- **`solvm`** — loads and runs a `.bin` bytecode image

Nothing else in the toolchain (`solas`, `desole`, `basicc`) matters for
this tutorial — they're covered in their own reference docs.

## Your first program

Create a file called `greet.pas`:

```pascal
program Greet;
begin
    writeln('Hello, Solstice!');
end.
```

A few things worth noticing even in this tiny program, because they're
true of every Pascal program you'll write:

- Every program starts with `program <Name>;` and ends with `end.` — a
  period, not a semicolon, on the very last line.
- `begin`/`end` bracket the program's main body — you'll see this same
  pairing again for procedures, `if` blocks, loops, and more.
- `writeln` prints its argument followed by a newline. Text in single
  quotes is a string literal.
- Statements end with `;` — except the last statement before an `end`,
  where it's optional (Pascal treats `;` as a statement *separator*,
  not a terminator — though writing one there anyway is harmless and
  common style).

## Compiling and running it

```sh
./bin/pascalc greet.pas greet.bin
./bin/solvm greet.bin
```

```
Hello, Solstice!
```

`pascalc` reads `greet.pas` and writes `greet.bin` — a compiled
bytecode image, not a native executable. `solvm` is what actually runs
it. This two-step split is why Solstice can add new source languages
(like `basicc`, its BASIC compiler) without touching the VM at all:
every front end just needs to produce the same `.bin` format.

## Seeing what's happening under the hood

Every tool in this project accepts an optional `-v` flag for verbose
output. Try it on both steps:

```sh
./bin/pascalc -v greet.pas greet.bin
```

This prints the compiler's internal phases as it runs — parsing,
type-checking, optimizing, code generation — along with a dump of the
program's abstract syntax tree. You don't need to understand this dump
to use Pascal, but it's there if you're curious what the compiler
actually built from your source.

```sh
./bin/solvm -v greet.bin
```

This prints a step-by-step trace of the virtual machine executing your
program's bytecode, plus a final dump of every variable's value when
the program halts. Handy for genuinely confusing bugs later on, once
your programs get bigger than this chapter's examples.

Without `-v`, both tools stay quiet except for what your program itself
prints — no extra noise.

## A shortcut for later

Once your programs get less trivial, typing the compile-then-run pair
every time gets old. If you `source config.sh` from the repo root, you
get a helper script:

```sh
source config.sh
pascal.sh greet.pas
```

That's the compile-and-run pair in one command (pass `-v` as its first
argument to see both tools' verbose output together). The rest of this
tutorial shows the explicit `pascalc`/`solvm` pair, since that's what's
actually happening either way — use whichever you prefer.

## Try it yourself

Change `greet.pas` to print a second line with your own name on it, and
re-run it. If you get stuck on the syntax for printing more than one
value on one line, try `writeln('Hello, ', 'Solstice', '!');` and see
what happens — [Input and Output](04-input-and-output.html) covers this
properly later.

Next: [Variables and Types](02-variables-and-types.html), where you'll
give your programs somewhere to put data instead of only ever printing
fixed text.
