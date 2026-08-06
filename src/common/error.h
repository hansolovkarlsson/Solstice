#ifndef ERROR_H
#define ERROR_H

#include <setjmp.h>

// Shared recovery point. A host (main(), a REPL, a GUI event handler, ...)
// establishes a recovery point with:
//
//     if (setjmp(fatal_error_env)) {
//         // landed here via fatal_abort() - the failed operation has been
//         // unwound; fatal_error_active is already reset to 0.
//         return 1;
//     }
//     fatal_error_active = 1;
//     ... call into the compiler/VM ...
//     fatal_error_active = 0;
//
extern jmp_buf fatal_error_env;
extern int fatal_error_active;

// Set by each CLI tool's main() from a -v flag. Checked anywhere debug or
// progress narration might be printed (compiler phase banners, the AST
// dump, optimizer pass logging, the VM's step banners and final variable
// dump, assembler/disassembler write confirmations) - a program's actual
// output (its own writeln/write calls) is never gated by this.
extern int verbose_mode;

// Called by every fatal-error site instead of exit(1). If a recovery point
// has been registered (fatal_error_active), unwinds back to it so the host
// process keeps running. If not (e.g. a host forgot to call setjmp first,
// or this is being used exactly like the old CLI), falls back to exit(1)
// so behavior is never "silently do nothing".
void fatal_abort(void);

#endif
