#ifndef _H_STRICT
#define _H_STRICT

static void
handle_strict(int state)
{
    extern int strict;

    strict = state;
}

#endif /* _H_STRICT */
