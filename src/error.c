#include <stdlib.h>
#include "error.h"

jmp_buf fatal_error_env;
int fatal_error_active = 0;

void fatal_abort(void) {
    if (fatal_error_active) {
        fatal_error_active = 0;
        longjmp(fatal_error_env, 1);
    }
    // No recovery point registered - preserve the old hard-exit behavior
    // rather than letting execution continue in an undefined state.
    exit(1);
}
