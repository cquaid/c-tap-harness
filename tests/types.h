#ifndef _H_TYPES
#define _H_TYPES

/* Test status codes. */
enum test_status {
    TEST_FAIL,
    TEST_PASS,
    TEST_SKIP,
    TEST_INVALID
};

/* Indicates the state of our plan. */
enum plan_status {
    PLAN_INIT,    /* Nothing seen yet.           */
    PLAN_FIRST,   /* Plan seen before any tests. */
    PLAN_PENDING, /* Test seen and no plan yet.  */
    PLAN_FINAL    /* Plan seen after some tests. */
};

/* Structure to hold data for a set of tests. */
struct testset {
    char *file;                /* The file name of the test.             */
    char *path;                /* The path to the test program.          */
    enum plan_status plan;     /* The status of our plan.                */
    unsigned long count;       /* Expected count of tests.               */
    unsigned long current;     /* The last seen test number.             */
    unsigned int length;       /* The length of teh last status message. */
    unsigned long passed;      /* Count of passing tests.                */
    unsigned long failed;      /* Count of failing tests.                */
    unsigned long skipped;     /* Count of skipped tests (passed).       */
    unsigned long allocated;   /* The size of the results table.         */
    enum test_status *results; /* Table of results by test number.       */
    unsigned int aborted;      /* If the set was aborted.                */
    int reported;              /* If the results were reported.          */
    int status;                /* The exit status of the test.           */
    unsigned int all_skipped;  /* If all tests were skipped.             */
    char *reason;              /* Why all tests were skipped.            */
    long tap_version;          /* Version of TAP to use.                 */
};

/* Structure to hold a linked list of test sets. */
struct testlist {
    struct testset *ts;
    struct testlist *next;
};

#endif /* _H_TYPES */

/* vim: set ts=4 sw=4 sts=4 expandtab */
