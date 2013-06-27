#ifndef _H_STRICT
#define _H_STRICT

#include "types.h"

static void
handle_strict(enum pragma_state state)
{
    extern int strict;
    /* Strict can either be on or off
     * by default based on a command line
     * argument.  Store off the original state
     * for when PRAGMA_RESET is passed in. */
    static int orig_strict = -1;
    if (orig_strict < 0)
        orig_strict = strict;

    switch (state) {
        case PRAGMA_RESET:
            strict = orig_strict;
            break;
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

/* vim: set ts=4 sw=4 sts=4 expandtab: */
