#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "log.h"

static FILE* logfile = NULL;


/* Attempts to open
 * a logfile.
 * returns true on success,
 * false otherwise. */
int
log_open(const char *name)
{
    if (logfile != NULL)
        log_close();

    if (strcmp(name, "stdout") == 0) {
        logfile = stdout;
        return 1;
    }

    if (strcmp(name, "stderr") == 0) {
        logfile = stderr;
        return 1;
    }

    /* Appending in case something else
     * creates the log.
     * This might be better as an option
     * and set with an argument to this
     * function. */
    logfile = fopen(name, "a");
    return (logfile != NULL);
}

void
log_close(void)
{
    if (logfile == NULL)
        return;

    if (logfile == stdout)
        return;

    if (logfile == stderr)
        return;

    fclose(logfile);
    logfile = NULL;
}

void
log_write(const char *fmt, ...)
{
    va_list vargs;

    if (logfile == NULL)
        return;

    va_start(vargs, fmt);
    vfprintf(logfile, fmt, vargs);
    va_end(vargs);

    fflush(logfile);
}

void
log_writeln(const char *str)
{
    if (logfile == NULL)
        return;

    fprintf(logfile, "%s\n", str);
    fflush(logfile);
}

/* vim: set ts=4 sw=4 sts=4 expandtab */
