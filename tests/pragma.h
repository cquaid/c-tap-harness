#ifndef _H_PRAGMA
#define _H_PRAGMA

#include "types.h"
#include "pragma_strict.h"

typedef void(*pragma_handle_fn)(enum pragma_state);
typedef int(*pragma_check_fn)(const char*,struct testset*);
struct pragma_hook {
    const char *name;
    pragma_handle_fn handle;
    pragma_check_fn check;
};

static struct pragma_hook pragma_list[] = {
    { "strict", handle_strict, NULL },
    /* end of list */
    { NULL, NULL, NULL }
};

#endif /* _H_PRAGMA */
