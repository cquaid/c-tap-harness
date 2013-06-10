#ifndef _H_STRICT
#define _H_STRICT

#include "types.h"

static void
handle_strict(enum pragma_state state)
{
    extern int strict;

    switch (state) {
        case PRAGMA_RESET:
        case PRAGMA_ON:
            strict = 1;
            break;
        case PRAGMA_OFF:
            strict = 0;
            break;
        default:
            break;
    };
}

#endif /* _H_STRICT */
