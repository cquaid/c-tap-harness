/*
 * Run a set of tests, reporting results.
 *
 * Usage:
 *
 *      runtests [-b <build-dir>] [-s <source-dir>] <test-list>
 *      runtests -o [-b <build-dir>] [-s <source-dir>] <test>
 *
 * In the first case, expects a list of executables located in the given file,
 * one line per executable.  For each one, runs it as part of a test suite,
 * reporting results.  Test output should start with a line containing the
 * number of tests (numbered from 1 to this number), optionally preceded by
 * "1..", although that line may be given anywhere in the output.  Each
 * additional line should be in the following format:
 *
 *      ok <number>
 *      not ok <number>
 *      ok <number> # skip
 *      not ok <number> # todo
 *
 * where <number> is the number of the test.  An optional comment is permitted
 * after the number if preceded by whitespace.  ok indicates success, not ok
 * indicates failure.  "# skip" and "# todo" are a special cases of a comment,
 * and must start with exactly that formatting.  They indicate the test was
 * skipped for some reason (maybe because it doesn't apply to this platform)
 * or is testing something known to currently fail.  The text following either
 * "# skip" or "# todo" and whitespace is the reason.
 *
 * As a special case, the first line of the output may be in the form:
 *
 *      1..0 # skip some reason
 *
 * which indicates that this entire test case should be skipped and gives a
 * reason.
 *
 * Any other lines are ignored, although for compliance with the TAP protocol
 * all lines other than the ones in the above format should be sent to
 * standard error rather than standard output and start with #.
 *
 * This is a subset of TAP as documented in Test::Harness::TAP or
 * TAP::Parser::Grammar, which comes with Perl.
 *
 * If the -o option is given, instead run a single test and display all of its
 * output.  This is intended for use with failing tests so that the person
 * running the test suite can get more details about what failed.
 *
 * If built with the C preprocessor symbols SOURCE and BUILD defined, C TAP
 * Harness will export those values in the environment so that tests can find
 * the source and build directory and will look for tests under both
 * directories.  These paths can also be set with the -b and -s command-line
 * options, which will override anything set at build time.
 *
 * Any bug reports, bug fixes, and improvements are very much welcome and
 * should be sent to the e-mail address below.  This program is part of C TAP
 * Harness <http://www.eyrie.org/~eagle/software/c-tap-harness/>.
 *
 * Copyright 2000, 2001, 2004, 2006, 2007, 2008, 2009, 2010, 2011
 *     Russ Allbery <rra@stanford.edu>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
*/

/* Required for fdopen(), getopt(), and putenv(). */
#if defined(__STRICT_ANSI__) || defined(PEDANTIC)
# ifndef _XOPEN_SOURCE
#  define _XOPEN_SOURCE 500
# endif
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* sys/time.h must be included before sys/resource.h on some platforms. */
#include <sys/resource.h>

#include "log.h"
#include "pragma.h"
#include "types.h"
#include "utils.h"

/* AIX doesn't have WCOREDUMP. */
#ifndef WCOREDUMP
# define WCOREDUMP(status)      ((unsigned)(status) & 0x80)
#endif

/*
 * Used for iterating through arrays.  Returns the number of elements in the
 * array (useful for a < upper bound in a for loop).
 */
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

/*
 * The source and build versions of the tests directory.  This is used to set
 * the SOURCE and BUILD environment variables and find test programs, if set.
 * Normally, this should be set as part of the build process to the test
 * subdirectories of $(abs_top_srcdir) and $(abs_top_builddir) respectively.
 */
#ifndef SOURCE
# define SOURCE NULL
#endif
#ifndef BUILD
# define BUILD NULL
#endif

/* Error exit statuses for test processes. */
#define CHILDERR_DUP    100     /* Couldn't redirect stderr or stdout. */
#define CHILDERR_EXEC   101     /* Couldn't exec child process. */
#define CHILDERR_STDERR 102     /* Couldn't open stderr file. */

/*
 * Usage message.  Should be used as a printf format with two arguments: the
 * path to runtests, given twice.
 */
static const char usage_message[] = "\
Usage: %s [-b <build-dir>] [-s <source-dir>] <test> ...\n\
       %s [-b <build-dir>] [-s <source-dir>] -l <test-list>\n\
       %s -o [-b <build-dir>] [-s <source-dir>] <test>\n\
\n\
Options:\n\
    -b <build-dir>      Set the build directory to <build-dir>\n\
    -l <list>           Take the list of tests to run from <test-list>\n\
    -o                  Run a single test rather than a list of tests\n\
    -s <source-dir>     Set the source directory to <source-dir>\n\
    -L <log-path>       Log test ouput to <log-path>\n\
    -a                  If -L is specified, open <log-path> in append mode\n\
    -v                  Verbose\n\
    -e                  Capture test stderr\n\
    -p                  Pedantic (strict TAP)\n\
\n\
runtests normally runs each test listed on the command line.  With the -l\n\
option, it instead runs every test listed in a file.  With the -o option,\n\
it instead runs a single test and shows its complete output.\n";

/*
 * Header used for test output.  %s is replaced by the file name of the list
 * of tests.
 */
static const char banner[] = "\n\
Running all tests listed in %s.  If any tests fail, run the failing\n\
test program with runtests -o to see more details.\n\n";

/* Header for reports of failed tests. */
static const char header[] = "\n\
Failed Set                 Fail/Total (%) Skip Stat  Failing Tests\n\
-------------------------- -------------- ---- ----  ------------------------";

/* Verbosity level can be more than just on or off.
 * The higher the verbosity the more output.
 * Verbosity levels:
 *  1+ Show tests as they execute instead of just a count
 *  3+ Print directives to stdout */
static int verbosity = 0;

/* Although not strict TAP, sometimes it's useful
 * to parse stderr also.
 * When turned on the stderr stream is mixed with the
 * stdout stream so both will be interpreted as TAP commands. */
static int capture_stderr = 0;

/* The following non-static variables are meant to be settable
 * from pragmas */

/* When turned on, TAP specification will be enforced.
 * This is required in TAP 13 */
int strict = 0;


/*
 * Given a struct timeval, return the number of seconds it represents as a
 * double.  Use difftime() to convert a time_t to a double.
 */
static double
tv_seconds(const struct timeval *tv)
{
    return difftime(tv->tv_sec, 0) + tv->tv_usec * 1e-6;
}


/*
 * Given two struct timevals, return the difference in seconds.
 */
static double
tv_diff(const struct timeval *tv1, const struct timeval *tv0)
{
    return tv_seconds(tv1) - tv_seconds(tv0);
}


/*
 * Given two struct timevals, return the sum in seconds as a double.
 */
static double
tv_sum(const struct timeval *tv1, const struct timeval *tv2)
{
    return tv_seconds(tv1) + tv_seconds(tv2);
}


/*
 * Start a program, connecting its stdout to a pipe on our end and its stderr
 * to /dev/null, and storing the file descriptor to read from in the two
 * argument.  Returns the PID of the new process.  Errors are fatal.
 */
static pid_t
test_start(const char *path, int *fd)
{
    int fds[2], errfd;
    pid_t child;

    if (pipe(fds) == -1) {
        puts("ABORTED");
        fflush(stdout);
        sysdie("can't create pipe");
    }
    child = fork();
    if (child == (pid_t) -1) {
        puts("ABORTED");
        fflush(stdout);
        sysdie("can't fork");
    } else if (child == 0) {
        /* In child.  Set up our stdout and stderr. */
        if (capture_stderr) {
            if (dup2(fds[1], 2) == -1)
                _exit(CHILDERR_DUP);
        } else {
            errfd = open("/dev/null", O_WRONLY);
            if (errfd < 0)
                _exit(CHILDERR_STDERR);
            if (dup2(errfd, 2) == -1)
                _exit(CHILDERR_DUP);
        }
        close(fds[0]);
        if (dup2(fds[1], 1) == -1)
            _exit(CHILDERR_DUP);

        /* Now, exec our process. */
        if (execl(path, path, (char *) 0) == -1)
            _exit(CHILDERR_EXEC);
    } else {
        /* In parent.  Close the extra file descriptor. */
        close(fds[1]);
    }
    *fd = fds[0];
    return child;
}


/*
 * Back up over the output saying what test we were executing.
 */
static void
test_backspace(struct testset *ts)
{
    unsigned int i;

    if (!isatty(STDOUT_FILENO))
        return;
    for (i = 0; i < ts->length; i++)
        putchar('\b');
    for (i = 0; i < ts->length; i++)
        putchar(' ');
    for (i = 0; i < ts->length; i++)
        putchar('\b');
    ts->length = 0;
}


/*
 * Read the plan line of test output, which should contain the range of test
 * numbers.  We may initialize the testset structure here if we haven't yet
 * seen a test.  Return true if initialization succeeded and the test should
 * continue, false otherwise.
 */
static int
test_plan(const char *line, struct testset *ts)
{
    unsigned long i;
    long n;

    /* If there's no leading '1..' return false. */
    line = skip_whitespace(line);
    if (strncmp(line, "1..", 3))
        return 0;
    line += 3;

    /*
     * Get the count, check it for validity, and initialize the struct.  If we
     * have something of the form "1..0 # skip foo", the whole file was
     * skipped; record that.  If we do skip the whole file, zero out all of
     * our statistics, since they're no longer relevant.  strtol is called
     * with a second argument to advance the line pointer past the count to
     * make it simpler to detect the # skip case.
     */
    n = strtol(line, (char **) &line, 10);
    if (n == 0) {
        line = skip_whitespace(line);
        if (*line == '#') {
            line = skip_whitespace(line + 1);
            if (strncasecmp(line, "skip", 4) == 0) {
                line = skip_whitespace(line + 4);
                if (*line != '\0') {
                    ts->reason = xstrdup(line);
                    ts->reason[strlen(ts->reason) - 1] = '\0';
                }
                ts->all_skipped = 1;
                ts->aborted = 1;
                ts->count = 0;
                ts->passed = 0;
                ts->skipped = 0;
                ts->failed = 0;
                return 0;
            }
        }
    }
    if (n <= 0) {
        puts("ABORTED (invalid test count)");
        ts->aborted = 1;
        ts->reported = 1;
        return 0;
    }
    if (ts->plan == PLAN_INIT && ts->allocated == 0) {
        ts->count = n;
        ts->allocated = n;
        ts->plan = PLAN_FIRST;
        ts->results = xmalloc(ts->count * sizeof(enum test_status));
        for (i = 0; i < ts->count; i++)
            ts->results[i] = TEST_INVALID;
    } else if (ts->plan == PLAN_PENDING) {
        if ((unsigned long) n < ts->count) {
            test_backspace(ts);
            printf("ABORTED (invalid test number %lu)\n", ts->count);
            ts->aborted = 1;
            ts->reported = 1;
            return 0;
        }
        ts->count = n;
        if ((unsigned long) n > ts->allocated) {
            ts->results = xrealloc(ts->results, n * sizeof(enum test_status));
            for (i = ts->allocated; i < ts->count; i++)
                ts->results[i] = TEST_INVALID;
            ts->allocated = n;
        }
        ts->plan = PLAN_FINAL;
    }
    return 1;
}

/*
 * Read the pragma and call the associated handler
 * pragmas can also be lists of directives:
 *  pragma +foo, -bar
 * or:
 *  pragma +foo,-bar
 *
 * grammar:
 *  PRAGMA ::= 'pragma' SWITCH IDENT { ',' SWITCH IDENT }
 *           ;
 *  SWITCH ::= '+'
 *           | '-'
 *           ;
 *  IDENT  ::= [a-zA-Z0-9_]+
 *           ;
 */
static int
test_pragma(const char *line, struct testset *ts)
{
    enum pragma_state state;
    struct pragma_hook *ph;
    size_t name_len;

    line = skip_whitespace(line);
    if (strncmp(line, "pragma", 6))
        return 0;
    line = skip_whitespace(line + 6);

    while (*line != '\0') {
        /* Pragmas can either be on (+) or off (-) */
        switch (*line) {
            case '+':
                state = PRAGMA_ON;
               break;
            case '-':
                state = PRAGMA_OFF;
               break;
            default:
                goto abort;
        }

        line += 1;
        /* if there's no name, it's the end of the list */
        for (ph = pragma_list ; ph->name != NULL; ++ph) {
            name_len = strlen(ph->name);
            if (strncmp(line, ph->name, strlen(ph->name)))
                continue;
            /* if we found a match, handle */
            if (ph->handle == NULL)
                break;
            ph->handle(state);
            break;
        }

        line = skip_whitespace(line + name_len);
        if (*line != ',')
            break;

        /* there can be whitespace after the comma */
        line = skip_whitespace(line + 1);
    }

    return 0;

abort:
    test_backspace(ts);
    puts("ABORTED (invalid pragma)");
    ts->aborted = 1;
    ts->reported = 1;
    return 1;
}

/*
 * Loop through the pragmas and call their respective check functions
 */
static int
test_check_pragma(const char *line, struct testset *ts)
{
    struct pragma_hook *ph;

    for (ph = pragma_list; ph->name != NULL; ++ph) {
        if (ph->check != NULL) {
            if (ph->check(line, ts))
                return 1;
        }
    }

    return 0;
}

/*
 * Loop through the pragmas and call their handler functions
 * with PRAGMA_RESET
 */
static void
test_reset_pragma(void)
{
    struct pragma_hook *ph;

    for (ph = pragma_list; ph->name != NULL; ++ph) {
        if (ph->handle != NULL)
            ph->handle(PRAGMA_RESET);
    }
}

/*
 * Given a single line of output from a test, parse it and return the success
 * status of that test.  Anything printed to stdout not matching the form
 * /^(not )?ok \d+/ is ignored.  Sets ts->current to the test number that just
 * reported status.
 */
static void
test_checkline(const char *line, struct testset *ts)
{
    enum test_status status = TEST_PASS;
    const char *bail;
    char *end;
    char *lline;
    long number;
    unsigned long i, current;
    int outlen;

    /* Before anything, check for a test abort. */
    bail = strstr(line, "Bail out!");
    if (bail != NULL) {
        /* line is not guarnateed to be \n terminated here,
         * so writeln. Logging here so the line isn't
         * modified yet. */
        log_writeln(line);
        bail = skip_whitespace(bail + strlen("Bail out!"));
        if (*bail != '\0') {
            size_t length;

            length = strlen(bail);
            if (bail[length - 1] == '\n')
                length--;
            test_backspace(ts);
            printf("ABORTED (%.*s)\n", (int) length, bail);
            ts->reported = 1;
        }
        ts->aborted = 1;
        return;
    }

    /*
     * If the given line isn't newline-terminated, it was too big for an
     * fgets(), which means ignore it.
     */
    if (line[strlen(line) - 1] != '\n') {
        /* All output needs to be logged,
         * even if it's ignored. */
        log_writeln(line);
        return;
    }

    /* line is newline terminated, so print without appending one.
     * use format so the print is safe. */
    log_write("%s", line);

    /* Check for TAP version line.
     * Reporting TAP version < 13 is an error.
     * 13 is the current TAP specification.
     * This should only be checked as the very first line
     * (when tap_version == 0). */
    if (ts->tap_version == 0) {
        if (strncmp(line, "TAP version ", 12) == 0) {
            /* Shift past the TAP version part */
            line += 12;
            ts->tap_version = strtol(line, NULL, 10);
            /* If the TAP version is bad, abort. */
            if (ts->tap_version < 13) {
                printf("ABORTED (Invalid TAP version: %ld)\n", ts->tap_version);
                ts->reported = 1;
                ts->aborted = 1;
            }
            return;
        } else {
            /* Default to 12 if no version is given. */
            ts->tap_version = 12;
        }
    }

    /* Pragma support added in TAP 13 */
    if (ts->tap_version >= 13) {
        /* Check for pragma line */
        if (test_pragma(line, ts))
            return;
        /* Let the pragma functions check
         * the lines before anything else
         * since they may parse additional
         * instructions */
         if (test_check_pragma(line, ts))
             return;
    }

    /* If the line begins with a hash mark, ignore it. */
    if (line[0] == '#') {
        if (verbosity >= 3)
            printf("%s", line);
        return;
    }

    /* If we haven't yet seen a plan, look for one. */
    if (strncmp(line, "1..", 3) == 0) {
        switch (ts->plan) {
            case PLAN_INIT:
            case PLAN_PENDING:
                if (!test_plan(line, ts))
                    return;
                /* At this point we know this is a valid plan */
                return;
            default:
                test_backspace(ts);
                puts("ABORTED (multiple plans)");
                ts->aborted = 1;
                ts->reported = 1;
                return;
        }
    }

    /* Parse the line, ignoring something we can't parse. */
    if (strncmp(line, "not ", 4) == 0) {
        status = TEST_FAIL;
        line += 4;
    }
    if (strncmp(line, "ok", 2) != 0)
        return;
    line = skip_whitespace(line + 2);
    errno = 0;
    number = strtol(line, &end, 10);
    if (errno != 0 || end == line)
        number = ts->current + 1;
    current = number;
    if (number <= 0 || (current > ts->count && ts->plan == PLAN_FIRST)) {
        test_backspace(ts);
        printf("ABORTED (invalid test number %lu)\n", current);
        ts->aborted = 1;
        ts->reported = 1;
        return;
    }

    /* We have a valid test result.  Tweak the results array if needed. */
    if (ts->plan == PLAN_INIT || ts->plan == PLAN_PENDING) {
        ts->plan = PLAN_PENDING;
        if (current > ts->count)
            ts->count = current;
        if (current > ts->allocated) {
            unsigned long n;

            n = (ts->allocated == 0) ? 32 : ts->allocated * 2;
            if (n < current)
                n = current;
            ts->results = xrealloc(ts->results, n * sizeof(enum test_status));
            for (i = ts->allocated; i < n; i++)
                ts->results[i] = TEST_INVALID;
            ts->allocated = n;
        }
    }

    /*
     * Handle directives.  We should probably do something more interesting
     * with unexpected passes of todo tests.
     */
    while (isdigit((unsigned char)(*line)))
        line++;
    line = skip_whitespace(line);

    lline = strchr(line, '#');
    if (lline != NULL) {
        /* we have a directive */
        lline = (char *)skip_whitespace(lline + 1);
        if (strncasecmp(lline, "skip", 4) == 0)
            status = TEST_SKIP;
        else if (strncasecmp(lline, "todo", 4) == 0)
            status = (status == TEST_FAIL) ? TEST_SKIP : TEST_FAIL;
    }

    /* Make sure that the test number is in range and not a duplicate. */
    if (ts->results[current - 1] != TEST_INVALID) {
        test_backspace(ts);
        printf("ABORTED (duplicate test number %lu)\n", current);
        ts->aborted = 1;
        ts->reported = 1;
        return;
    }

    /* Good results.  Increment our various counters. */
    switch (status) {
        case TEST_PASS: ts->passed++;   break;
        case TEST_FAIL: ts->failed++;   break;
        case TEST_SKIP: ts->skipped++;  break;
        case TEST_INVALID:              break;
    }
    ts->current = current;
    ts->results[current - 1] = status;

    /* in verbose mode, print tests as they complete */
    if (verbosity >= 1) {
        char *rslt;
        size_t len;
        switch (status) {
            case TEST_PASS: rslt = "PASS"; break;
            case TEST_FAIL: rslt = "FAIL"; break;
            case TEST_SKIP: rslt = "SKIP"; break;
            case TEST_INVALID:
            default:
                rslt = "MISSING";
                break;
        }
        lline = (char *)skip_whitespace(line);
        len = strlen(lline);
        if (len > 0) {
            /* remove the \n at the end */
            lline[strlen(lline) - 1] = '\0';
            printf("  %3lu %s: %s\n", current, lline, rslt);
        } else
            printf("  %3lu %s\n", current, rslt);
        fflush(stdout);
    } else if (isatty(STDOUT_FILENO)) {
        test_backspace(ts);
        if (ts->plan == PLAN_PENDING)
            outlen = printf("%lu/?", current);
        else
            outlen = printf("%lu/%lu", current, ts->count);
        ts->length = (outlen >= 0) ? outlen : 0;
        fflush(stdout);
    }
}


/*
 * Print out a range of test numbers, returning the number of characters it
 * took up.  Takes the first number, the last number, the number of characters
 * already printed on the line, and the limit of number of characters the line
 * can hold.  Add a comma and a space before the range if chars indicates that
 * something has already been printed on the line, and print ... instead if
 * chars plus the space needed would go over the limit (use a limit of 0 to
 * disable this).
 */
static unsigned int
test_print_range(unsigned long first, unsigned long last, unsigned int chars,
                 unsigned int limit)
{
    unsigned int needed = 0;
    unsigned long n;

    for (n = first; n > 0; n /= 10)
        needed++;
    if (last > first) {
        for (n = last; n > 0; n /= 10)
            needed++;
        needed++;
    }
    if (chars > 0)
        needed += 2;
    if (limit > 0 && chars + needed > limit) {
        needed = 0;
        if (chars <= limit) {
            if (chars > 0) {
                printf(", ");
                needed += 2;
            }
            printf("...");
            needed += 3;
        }
    } else {
        if (chars > 0)
            printf(", ");
        if (last > first)
            printf("%lu-", first);
        printf("%lu", last);
    }
    return needed;
}


/*
 * Summarize a single test set.  The second argument is 0 if the set exited
 * cleanly, a positive integer representing the exit status if it exited
 * with a non-zero status, and a negative integer representing the signal
 * that terminated it if it was killed by a signal.
 */
static void
test_summarize(struct testset *ts, int status)
{
    unsigned long i;
    unsigned long missing = 0;
    unsigned long failed = 0;
    unsigned long first = 0;
    unsigned long last = 0;

    if (ts->aborted) {
        fputs("ABORTED", stdout);
        if (ts->count > 0)
            printf(" (passed %lu/%lu)", ts->passed, ts->count - ts->skipped);
    } else {
        for (i = 0; i < ts->count; i++) {
            if (ts->results[i] == TEST_INVALID) {
                if (missing == 0)
                    fputs("MISSED ", stdout);
                if (first && i == last)
                    last = i + 1;
                else {
                    if (first)
                        test_print_range(first, last, missing - 1, 0);
                    missing++;
                    first = i + 1;
                    last = i + 1;
                }
            }
        }
        if (first)
            test_print_range(first, last, missing - 1, 0);
        first = 0;
        last = 0;
        for (i = 0; i < ts->count; i++) {
            if (ts->results[i] == TEST_FAIL) {
                if (missing && !failed)
                    fputs("; ", stdout);
                if (failed == 0)
                    fputs("FAILED ", stdout);
                if (first && i == last)
                    last = i + 1;
                else {
                    if (first)
                        test_print_range(first, last, failed - 1, 0);
                    failed++;
                    first = i + 1;
                    last = i + 1;
                }
            }
        }
        if (first)
            test_print_range(first, last, failed - 1, 0);
        if (!missing && !failed) {
            fputs(!status ? "ok" : "dubious", stdout);
            if (ts->skipped > 0) {
                if (ts->skipped == 1)
                    printf(" (skipped %lu test)", ts->skipped);
                else
                    printf(" (skipped %lu tests)", ts->skipped);
            }
        }
    }
    if (status > 0)
        printf(" (exit status %d)", status);
    else if (status < 0)
        printf(" (killed by signal %d%s)", -status,
               WCOREDUMP(ts->status) ? ", core dumped" : "");
    putchar('\n');
}


/*
 * Given a test set, analyze the results, classify the exit status, handle a
 * few special error messages, and then pass it along to test_summarize() for
 * the regular output.  Returns true if the test set ran successfully and all
 * tests passed or were skipped, false otherwise.
 */
static int
test_analyze(struct testset *ts)
{
    if (ts->reported)
        return 0;
    if (ts->all_skipped) {
        if (ts->reason == NULL)
            puts("skipped");
        else
            printf("skipped (%s)\n", ts->reason);
        return 1;
    } else if (WIFEXITED(ts->status) && WEXITSTATUS(ts->status) != 0) {
        switch (WEXITSTATUS(ts->status)) {
        case CHILDERR_DUP:
            if (!ts->reported)
                puts("ABORTED (can't dup file descriptors)");
            break;
        case CHILDERR_EXEC:
            if (!ts->reported)
                puts("ABORTED (execution failed -- not found?)");
            break;
        case CHILDERR_STDERR:
            if (!ts->reported)
                puts("ABORTED (can't open /dev/null)");
            break;
        default:
            test_summarize(ts, WEXITSTATUS(ts->status));
            break;
        }
        return 0;
    } else if (WIFSIGNALED(ts->status)) {
        test_summarize(ts, -WTERMSIG(ts->status));
        return 0;
    } else if (ts->plan != PLAN_FIRST && ts->plan != PLAN_FINAL) {
        puts("ABORTED (no valid test plan)");
        ts->aborted = 1;
        return 0;
    } else {
        test_summarize(ts, 0);
        return (ts->failed == 0);
    }
}


/*
 * Runs a single test set, accumulating and then reporting the results.
 * Returns true if the test set was successfully run and all tests passed,
 * false otherwise.
 */
static int
test_run(unsigned int longest, struct testset *ts)
{
    pid_t testpid, child;
    int outfd, status;
    unsigned long i;
    char buffer[BUFSIZ];

    /* Run the test program. */
    testpid = test_start(ts->path, &outfd);

    /* Reset all Pragmas each run. */
    test_reset_pragma();

    /* Pass each line of output to test_checkline(). */
    while (!ts->aborted && get_line(outfd, buffer, sizeof(buffer)))
        test_checkline(buffer, ts);
    if (ts->plan == PLAN_INIT)
        ts->aborted = 1;
    /* If verbose, print test name and result */
    if (verbosity >= 1) {
        printf("%s", ts->file);
        for (i = strlen(ts->file); i < longest; i++)
            putchar('.');
    } else {
        /* Otherwise backspace over numeric results */
        test_backspace(ts);
    }

    /*
     * Consume the rest of the test output, close the output descriptor,
     * retrieve the exit status, and pass that information to test_analyze()
     * for eventual output.
     */
    while (get_line(outfd, buffer, sizeof(buffer)))
        ;
    close(outfd);
    child = waitpid(testpid, &ts->status, 0);
    if (child == (pid_t) -1) {
        if (!ts->reported) {
            puts("ABORTED");
            fflush(stdout);
        }
        sysdie("waitpid for %u failed", (unsigned int) testpid);
    }
    if (ts->all_skipped)
        ts->aborted = 0;
    status = test_analyze(ts);

    /* Convert missing tests to failed tests. */
    for (i = 0; i < ts->count; i++) {
        if (ts->results[i] == TEST_INVALID) {
            ts->failed++;
            ts->results[i] = TEST_FAIL;
            status = 0;
        }
    }
    return status;
}


/* Summarize a list of test failures. */
static void
test_fail_summary(const struct testlist *fails)
{
    struct testset *ts;
    unsigned int chars;
    unsigned long i, first, last, total;

    puts(header);

    /* Failed Set                 Fail/Total (%) Skip Stat  Failing (25)
       -------------------------- -------------- ---- ----  -------------- */
    for (; fails; fails = fails->next) {
        ts = fails->ts;
        total = ts->count - ts->skipped;
        printf("%-26.26s %4lu/%-4lu %3.0f%% %4lu ", ts->file, ts->failed,
               total, total ? (ts->failed * 100.0) / total : 0,
               ts->skipped);
        if (WIFEXITED(ts->status))
            printf("%4d  ", WEXITSTATUS(ts->status));
        else
            printf("  --  ");
        if (ts->aborted) {
            puts("aborted");
            continue;
        }
        chars = 0;
        first = 0;
        last = 0;
        for (i = 0; i < ts->count; i++) {
            if (ts->results[i] == TEST_FAIL) {
                if (first != 0 && i == last)
                    last = i + 1;
                else {
                    if (first != 0)
                        chars += test_print_range(first, last, chars, 19);
                    first = i + 1;
                    last = i + 1;
                }
            }
        }
        if (first != 0)
            test_print_range(first, last, chars, 19);
        putchar('\n');
    }
}


/*
 * Check whether a given file path is a valid test.  Currently, this checks
 * whether it is executable and is a regular file.  Returns true or false.
 */
static int
is_valid_test(const char *path)
{
    struct stat st;

    if (access(path, X_OK) < 0)
        return 0;
    if (stat(path, &st) < 0)
        return 0;
    if (!S_ISREG(st.st_mode))
        return 0;
    return 1;
}


/*
 * Given the name of a test, a pointer to the testset struct, and the source
 * and build directories, find the test.  We try first relative to the current
 * directory, then in the build directory (if not NULL), then in the source
 * directory.  In each of those directories, we first try a "-t" extension and
 * then a ".t" extension.  When we find an executable program, we return the
 * path to that program.  If none of those paths are executable, just fill in
 * the name of the test as is.
 *
 * The caller is responsible for freeing the path member of the testset
 * struct.
 */
static char *
find_test(const char *name, const char *source, const char *build)
{
    char *path;
    const char *bases[3], *suffix, *base;
    unsigned int i, j;
    const char *suffixes[3] = { "-t", ".t", "" };

    /* Possible base directories. */
    bases[0] = ".";
    bases[1] = build;
    bases[2] = source;

    /* Try each suffix with each base. */
    for (i = 0; i < ARRAY_SIZE(suffixes); i++) {
        suffix = suffixes[i];
        for (j = 0; j < ARRAY_SIZE(bases); j++) {
            base = bases[j];
            if (base == NULL)
                continue;
            path = xmalloc(strlen(base) + strlen(name) + strlen(suffix) + 2);
            sprintf(path, "%s/%s%s", base, name, suffix);
            if (is_valid_test(path))
                return path;
            free(path);
            path = NULL;
        }
    }
    if (path == NULL)
        path = xstrdup(name);
    return path;
}


/*
 * Read a list of tests from a file, returning the list of tests as a struct
 * testlist.  Reports an error to standard error and exits if the list of
 * tests cannot be read.
 */
static struct testlist *
read_test_list(const char *filename)
{
    FILE *file;
    unsigned int line;
    size_t length;
    char buffer[BUFSIZ];
    struct testlist *listhead, *current;

    /* Create the initial container list that will hold our results. */
    listhead = xmalloc(sizeof(struct testlist));
    listhead->ts = NULL;
    listhead->next = NULL;
    current = NULL;

    /*
     * Open our file of tests to run and read it line by line, creating a new
     * struct testlist and struct testset for each line.
     */
    file = fopen(filename, "r");
    if (file == NULL)
        sysdie("can't open %s", filename);
    line = 0;
    while (fgets(buffer, sizeof(buffer), file)) {
        line++;
        length = strlen(buffer) - 1;
        if (buffer[length] != '\n') {
            fprintf(stderr, "%s:%u: line too long\n", filename, line);
            exit(1);
        }
        buffer[length] = '\0';
        /* Skip comments. */
        if (buffer[0] == '#')
            continue;
        if (current == NULL)
            current = listhead;
        else {
            current->next = xmalloc(sizeof(struct testlist));
            current = current->next;
            current->next = NULL;
        }
        current->ts = xcalloc(1, sizeof(struct testset));
        current->ts->plan = PLAN_INIT;
        current->ts->file = xstrdup(buffer);
        current->ts->reason = NULL;
    }
    fclose(file);

    /* Return the results. */
    return listhead;
}


/*
 * Build a list of tests from command line arguments.  Takes the argv and argc
 * representing the command line arguments and returns a newly allocated test
 * list.  The caller is responsible for freeing.
 */
static struct testlist *
build_test_list(char *argv[], int argc)
{
    int i;
    struct testlist *listhead, *current;

    /* Create the initial container list that will hold our results. */
    listhead = xmalloc(sizeof(struct testlist));
    listhead->ts = NULL;
    listhead->next = NULL;
    current = NULL;

    /* Walk the list of arguments and create test sets for them. */
    for (i = 0; i < argc; i++) {
        if (current == NULL)
            current = listhead;
        else {
            current->next = xmalloc(sizeof(struct testlist));
            current = current->next;
            current->next = NULL;
        }
        current->ts = xcalloc(1, sizeof(struct testset));
        current->ts->plan = PLAN_INIT;
        current->ts->file = xstrdup(argv[i]);
        current->ts->reason = NULL;
    }

    /* Return the results. */
    return listhead;
}


/* Free a struct testset. */
static void
free_testset(struct testset *ts)
{
    free(ts->file);
    free(ts->path);
    free(ts->results);
    if (ts->reason != NULL)
        free(ts->reason);
    free(ts);
}


/*
 * Run a batch of tests.  Takes two additional parameters: the root of the
 * source directory and the root of the build directory.  Test programs will
 * be first searched for in the current directory, then the build directory,
 * then the source directory.  Returns true iff all tests passed, and always
 * frees the test list that's passed in.
 */
static int
test_batch(struct testlist *tests, const char *source, const char *build)
{
    size_t length;
    unsigned int i;
    unsigned int longest = 0;
    unsigned int count = 0;
    struct testset *ts;
    struct timeval start, end;
    struct rusage stats;
    struct testlist *failhead = NULL;
    struct testlist *failtail = NULL;
    struct testlist *current, *next;
    int succeeded;
    unsigned long total = 0;
    unsigned long passed = 0;
    unsigned long skipped = 0;
    unsigned long failed = 0;
    unsigned long aborted = 0;

    /* Walk the list of tests to find the longest name. */
    for (current = tests; current != NULL; current = current->next) {
        length = strlen(current->ts->file);
        if (length > longest)
            longest = length;
    }

    /*
     * Add two to longest and round up to the nearest tab stop.  This is how
     * wide the column for printing the current test name will be.
     */
    longest += 2;
    if (longest % 8)
        longest += 8 - (longest % 8);

    /* Start the wall clock timer. */
    gettimeofday(&start, NULL);

    /* Now, plow through our tests again, running each one. */
    for (current = tests; current != NULL; current = current->next) {
        ts = current->ts;

        /* Print out the name of the test file. */
        fputs(ts->file, stdout);
        for (i = strlen(ts->file); i < longest; i++)
            putchar('.');
        /* If in verbose mode, place a newline */
        if (verbosity >= 1)
            putchar('\n');
        if (isatty(STDOUT_FILENO))
            fflush(stdout);

        /* Run the test. */
        ts->path = find_test(ts->file, source, build);
        succeeded = test_run(longest, ts);
        fflush(stdout);

        /* Record cumulative statistics. */
        aborted += ts->aborted;
        total += ts->count + ts->all_skipped;
        passed += ts->passed;
        skipped += ts->skipped + ts->all_skipped;
        failed += ts->failed;
        count++;

        /* If the test fails, we shuffle it over to the fail list. */
        if (!succeeded) {
            if (failhead == NULL) {
                failhead = xmalloc(sizeof(struct testset));
                failtail = failhead;
            } else {
                failtail->next = xmalloc(sizeof(struct testset));
                failtail = failtail->next;
            }
            failtail->ts = ts;
            failtail->next = NULL;
        }
    }
    total -= skipped;

    /* Stop the timer and get our child resource statistics. */
    gettimeofday(&end, NULL);
    getrusage(RUSAGE_CHILDREN, &stats);

    /* Summarize the failures and free the failure list. */
    if (failhead != NULL) {
        test_fail_summary(failhead);
        while (failhead != NULL) {
            next = failhead->next;
            free(failhead);
            failhead = next;
        }
    }

    /* Free the memory used by the test lists. */
    while (tests != NULL) {
        next = tests->next;
        free_testset(tests->ts);
        free(tests);
        tests = next;
    }

    /* Print out the final test summary. */
    putchar('\n');
    if (aborted != 0) {
        if (aborted == 1)
            printf("Aborted %lu test set", aborted);
        else
            printf("Aborted %lu test sets", aborted);
        printf(", passed %lu/%lu tests", passed, total);
    }
    else if (failed == 0)
        fputs("All tests successful", stdout);
    else
        printf("Failed %lu/%lu tests, %.2f%% okay", failed, total,
               (total - failed) * 100.0 / total);
    if (skipped != 0) {
        if (skipped == 1)
            printf(", %lu test skipped", skipped);
        else
            printf(", %lu tests skipped", skipped);
    }
    puts(".");
    printf("Files=%u,  Tests=%lu", count, total);
    printf(",  %.2f seconds", tv_diff(&end, &start));
    printf(" (%.2f usr + %.2f sys = %.2f CPU)\n",
           tv_seconds(&stats.ru_utime), tv_seconds(&stats.ru_stime),
           tv_sum(&stats.ru_utime, &stats.ru_stime));
    return (failed == 0 && aborted == 0);
}


/*
 * Run a single test case.  This involves just running the test program after
 * having done the environment setup and finding the test program.
 */
static void
test_single(const char *program, const char *source, const char *build)
{
    char *path;

    path = find_test(program, source, build);
    if (execl(path, path, (char *) 0) == -1)
        sysdie("cannot exec %s", path);
}


/*
 * Main routine.  Set the SOURCE and BUILD environment variables and then,
 * given a file listing tests, run each test listed.
 */
int
main(int argc, char *argv[])
{
    int option;
    int status = 0;
    int single = 0;
    int append = 0;
    char *source_env = NULL;
    char *build_env = NULL;
    const char *shortlist;
    const char *list = NULL;
    const char *source = SOURCE;
    const char *build = BUILD;
    const char *name = NULL;
    const char *logname = NULL;
    struct testlist *tests;

    /* store off program name for usage statements */
    name = argv[0];

    while ((option = getopt(argc, argv, "b:hl:os:L:avep")) != EOF) {
        switch (option) {
        case 'b':
            build = optarg;
            break;
        case 'h':
            printf(usage_message, name, name, name);
            exit(0);
            break;
        case 'l':
            list = optarg;
            break;
        case 'o':
            single = 1;
            break;
        case 's':
            source = optarg;
            break;
        case 'L':
            logname = optarg;
            break;
        case 'a':
            append = 1;
            break;
        case 'v':
            /* Increase verbosity to support
             * multiple levels of output */
            verbosity++;
            break;
        case 'e':
            capture_stderr = 1;
            break;
        case 'p':
            strict = 1;
            break;
        default:
            fprintf(stderr, "Invalid option: %c\n", (char)(option & 0xff));
            fprintf(stderr, usage_message, name, name, name);
            exit(1);
        }
    }
    argv += optind;
    argc -= optind;
    if ((list == NULL && argc < 1) || (list != NULL && argc > 0)) {
        fprintf(stderr, usage_message, name, name, name);
        exit(1);
    }

    /* Set SOURCE and BUILD environment variables. */
    if (source != NULL) {
        source_env = xmalloc(strlen("SOURCE=") + strlen(source) + 1);
        sprintf(source_env, "SOURCE=%s", source);
        if (putenv(source_env) != 0)
            sysdie("cannot set SOURCE in the environment");
    }
    if (build != NULL) {
        build_env = xmalloc(strlen("BUILD=") + strlen(build) + 1);
        sprintf(build_env, "BUILD=%s", build);
        if (putenv(build_env) != 0)
            sysdie("cannot set BUILD in the environment");
    }

    if (logname != NULL) {
        if (log_open(logname, append) == 0)
            sysdie("cannot open log file: %s", logname);
    }

    /* Run the tests as instructed. */
    if (single)
        test_single(argv[0], source, build);
    else if (list != NULL) {
        shortlist = strrchr(list, '/');
        if (shortlist == NULL)
            shortlist = list;
        else
            shortlist++;
        printf(banner, shortlist);
        tests = read_test_list(list);
        status = test_batch(tests, source, build) ? 0 : 1;
    } else {
        tests = build_test_list(argv, argc);
        status = test_batch(tests, source, build) ? 0 : 1;
    }

    /* Clean up the log,
     * We don't need to check if we've opened a log here,
     * log_close checks for us. */
    log_close();

    /* For valgrind cleanliness, free all our memory. */
    if (source_env != NULL) {
        putenv((char *) "SOURCE=");
        free(source_env);
    }
    if (build_env != NULL) {
        putenv((char *) "BUILD=");
        free(build_env);
    }
    exit(status);
}

/* vim: set ts=4 sw=4 sts=4 expandtab */
