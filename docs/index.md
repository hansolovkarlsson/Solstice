---
title: Home
nav_order: 1
description: "Solstice: a Pascal and BASIC toolchain built on a custom virtual machine, from scratch."
permalink: /
---

# Solstice
{: .fs-9 }

A multi-language toolchain built around one custom stack-based virtual
machine (**SolVM**) and its own bytecode format — designed from scratch,
under its own control, rather than aiming for P-Code or any existing
VM's compatibility.
{: .fs-6 .fw-300 }

[Start the tutorial](tutorial/){: .btn .btn-primary .fs-5 .mb-4 .mb-md-0 .mr-2 }
[Language reference](LANGUAGE.html){: .btn .fs-5 .mb-4 .mb-md-0 .mr-2 }
[View on GitHub](https://github.com/hansolovkarlsson/Solstice){: .btn .fs-5 .mb-4 .mb-md-0 }

---

## What this is

The main compiler here is `pascalc`, a Wirth-style **Pascal** compiler,
developed alongside a matching assembler (`solas`) and disassembler
(`desole`) for SolVM's own bytecode. A second front end, `basicc`
(classic line-numbered **BASIC**), targets the same bytecode format and
VM unmodified.

```
 source.pas  ──(pascalc)──┐
 source.bas  ──(basicc)───┼──> program.bin ──(solvm)──> runs it
 source.sasm ──(solas)────┘         │
                                 (desole)
                                     ▼
                               readable .sasm
```

`solas` and `desole` exist for a reason beyond convenience: they let you
inspect exactly what a compiler's code generator produced, and
hand-write or hand-modify bytecode without going through a compiler at
all.

## Where to start

- **New to this dialect of Pascal?** Start with the [Pascal
  Tutorial](tutorial/) — a from-scratch walkthrough of the core language,
  with every example actually compiled and run.
- **Already know what you're looking for?** The [Pascal Language
  Reference](LANGUAGE.html) documents the full accepted dialect: syntax,
  types, statements, operators, one worked example per feature.
- **Writing BASIC instead?** See the [BASIC reference](BASIC.html).
- **Working on the VM or assembler directly?** See the [Bytecode / VM
  reference](BYTECODE.html) and the [Assembler reference](ASSEMBLER.html).
- **Extending Solstice itself?** [Project](project.html) has the
  internals writeup, the roadmap, and the full change history.

## Building it yourself

Requires a C11 compiler and `make`, no external dependencies:

```sh
git clone https://github.com/hansolovkarlsson/Solstice.git
cd Solstice
make
./bin/pascalc examples/tech/hello.pas hello.bin
./bin/solvm hello.bin
```
