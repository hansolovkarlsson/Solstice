#include <stdlib.h>
#include "error.h"

jmp_buf rascal_error_env;
int rascal_error_active = 0;

void rascal_abort(void) {
    if (rascal_error_active) {
        rascal_error_active = 0;
        longjmp(rascal_error_env, 1);
    }
    // No recovery point registered - preserve the old hard-exit behavior
    // rather than letting execution continue in an undefined state.
    exit(1);
}
