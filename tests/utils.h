#ifndef _H_UTILS
#define _H_UTILS

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Maximum iterations for the get_line function */
#define DEFAULT_MAX_ITER (20)

/* Include the file name and line number in malloc failures. */
#define xcalloc(n, size)  x_calloc((n), (size), __FILE__, __LINE__)
#define xmalloc(size)     x_malloc((size), __FILE__, __LINE__)
#define xrealloc(p, size) x_realloc((p), (size), __FILE__, __LINE__)
#define xstrdup(p)        x_strdup((p), __FILE__, __LINE__)

/*
 * __attribute__ is available in gcc 2.5 and later, but only with gcc 2.7
 * could you use the __format__ form of the attributes, which is what we use
 * (to avoid confusion with other macros).
 */
#ifndef __attribute__
# if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 7)
#  define __attribute__(spec)   /* empty */
# endif
#endif

/*
 * We use __alloc_size__, but it was only available in fairly recent versions
 * of GCC.  Suppress warnings about the unknown attribute if GCC is too old.
 * We know that we're GCC at this point, so we can use the GCC variadic macro
 * extension, which will still work with versions of GCC too old to have C99
 * variadic macro support.
 */
#if !defined(__attribute__) && !defined(__alloc_size__)
# if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 3)
#  define __alloc_size__(spec, args...) /* empty */
# endif
#endif

/*
 * LLVM and Clang pretend to be GCC but don't support all of the __attribute__
 * settings that GCC does.  For them, suppress warnings about unknown
 * attributes on declarations.  This unfortunately will affect the entire
 * compilation context, but there's no push and pop available.
 */
#if !defined(__attribute__) && (defined(__llvm__) || defined(__clang__))
# pragma GCC diagnostic ignored "-Wattributes"
#endif

/* Declare internal functions that benefit from compiler attributes. */
static void sysdie(const char *, ...)
    __attribute__((__nonnull__, __noreturn__, __format__(printf, 1, 2)));
static void *x_calloc(size_t, size_t, const char *, int)
    __attribute__((__alloc_size__(1, 2), __malloc__, __nonnull__));
static void *x_malloc(size_t, const char *, int)
    __attribute__((__alloc_size__(1), __malloc__, __nonnull__));
static void *x_realloc(void *, size_t, const char *, int)
    __attribute__((__alloc_size__(2), __malloc__, __nonnull__(3)));
static char *x_strdup(const char *, const char *, int)
    __attribute__((__malloc__, __nonnull__));


/*
 * Report a fatal error, including the results of strerror, and exit.
 */
static void
sysdie(const char *format, ...)
{
    int oerrno;
    va_list args;

    oerrno = errno;
    fflush(stdout);
    fprintf(stderr, "runtests: ");
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, ": %s\n", strerror(oerrno));
    exit(EXIT_FAILURE);
}


/*
 * Allocate zeroed memory, reporting a fatal error and exiting on failure.
 */
static void *
x_calloc(size_t n, size_t size, const char *file, int line)
{
    void *p;

    n = (n > 0) ? n : 1;
    size = (size > 0) ? size : 1;
    p = calloc(n, size);
    if (p == NULL)
        sysdie("failed to calloc %lu bytes at %s line %d",
               (unsigned long) size, file, line);
    return p;
}


/*
 * Allocate memory, reporting a fatal error and exiting on failure.
 */
static void *
x_malloc(size_t size, const char *file, int line)
{
    void *p;

    p = malloc(size);
    if (p == NULL)
        sysdie("failed to malloc %lu bytes at %s line %d",
               (unsigned long) size, file, line);
    return p;
}


/*
 * Reallocate memory, reporting a fatal error and exiting on failure.
 */
static void *
x_realloc(void *p, size_t size, const char *file, int line)
{
    p = realloc(p, size);
    if (p == NULL)
        sysdie("failed to realloc %lu bytes at %s line %d",
               (unsigned long) size, file, line);
    return p;
}


/*
 * Copy a string, reporting a fatal error and exiting on failure.
 */
static char *
x_strdup(const char *s, const char *file, int line)
{
    char *p;
    size_t len;

    len = strlen(s) + 1;
    p = malloc(len);
    if (p == NULL)
        sysdie("failed to strdup %lu bytes at %s line %d",
               (unsigned long) len, file, line);
    memcpy(p, s, len);
    return p;
}


/*
 * Given a pointer to a string, skip any leading whitespace and return a
 * pointer to the first non-whitespace character.
 */
static const char *
skip_whitespace(const char *p)
{
    while (isspace((unsigned char)(*p)))
        p++;
    return p;
}


/*
 * This function reads a single line from a file descriptor.
 * Returns 0 on failure and 1 on success.
 * The main reason for this is fdopen can leak memory and
 * typically doing buffered reads on pipe isn't usually
 * a good idea since fdopen leaks resources on RHEL 4 variants.
 *
 * We expect fd to be opened Non-blocking so we have a count
 * controlled maximum re-trys to exit out if we're "blocking"
 * for too long.
 *
 * Returns 0 when the pipe closes/end of read
 * Returns 1 if successfully read a line and there's more
 *  to read
 * Returns -1 if there's an error
 */

/* Defined in runtests.c */
extern long blocking_time;

static int
get_line(int fd, char *buffer, int buffer_len)
{
    char cbuf[1];
    int count;
    int line_done;
    long iter;
    ssize_t ret;

    iter = 0;
    count = 0;
    line_done = 0;

    /* Leave room for a null terminator. */
    while ((count < (buffer_len - 1)) && (line_done == 0)) {
        ret = read(fd, cbuf, 1);
        switch (ret) {
            case -1:
                /* Both of these can be returned
                 * if the read would block.
                 * Depends on which posix version. */
                if (errno == EAGAIN
                    || errno == EWOULDBLOCK) {
                    if (iter < blocking_time) {
                        ++iter;
                        sleep(1);
                        continue;
                    }
                    /* If we've looped enough just return */
                    buffer[count] = '\0';
                    return 0;
                }
                /* For all other errors, return -1 */
                buffer[count] = '\0';
                return -1;
            case 0:
                /* EOF reached */
                buffer[count] = '\0';
                return 0;
            default:
                break;
        }

        buffer[count++] = cbuf[0];
        if (cbuf[0] == '\n')
            line_done = 1;

        /* Reset the counter if we manage to read something. */
        if (iter != 0)
            iter = 0;
    }

    buffer[count] = '\0';
    return 1;
}

#endif /* _H_UTILS */

/* vim: set ts=4 sw=4 sts=4 expandtab: */
