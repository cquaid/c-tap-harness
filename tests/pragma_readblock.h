#ifndef _H_READBLOCK
#define _H_READBLOCK

#include "types.h"

/**
 * When pragma +readblock is set,
 * the read loop for data from the child
 * will be blocking (or just retries the
 * read indefinitely.
 *
 * When pragma -readblock is set,
 * the read loop becomes non-blocking.
 *
 * On reset, the original value is restored.
 */

extern int blocking_read;

static void
handle_readblock(enum pragma_state state)
{
    /* blocking_read can either be on or off
     * by default based on a command line
     * argument.  Store off the original state
     * for when PRAGMA_RESET is passed in. */
    static int orig_block = -1;
    if (orig_block < 0)
        orig_block = blocking_read;

    switch (state) {
        case PRAGMA_RESET:
            blocking_read = orig_block;
            break;
        case PRAGMA_ON:
            blocking_read = 1;
            break;
        case PRAGMA_OFF:
            blocking_read = 0;
            break;
        default:
            break;
    };
}

#endif /* _H_READBLOCK */

/* vim: set ts=4 sw=4 sts=4 expandtab: */
