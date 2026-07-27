#ifndef ERROR_H
#define ERROR_H

#include <setjmp.h>

// Shared recovery point. A host (main(), a REPL, a GUI event handler, ...)
// establishes a recovery point with:
//
//     if (setjmp(rascal_error_env)) {
//         // landed here via rascal_abort() - the failed operation has been
//         // unwound; rascal_error_active is already reset to 0.
//         return 1;
//     }
//     rascal_error_active = 1;
//     ... call into the compiler/VM ...
//     rascal_error_active = 0;
//
extern jmp_buf rascal_error_env;
extern int rascal_error_active;

// Called by every fatal-error site instead of exit(1). If a recovery point
// has been registered (rascal_error_active), unwinds back to it so the host
// process keeps running. If not (e.g. a host forgot to call setjmp first,
// or this is being used exactly like the old CLI), falls back to exit(1)
// so behavior is never "silently do nothing".
void rascal_abort(void);

#endif
