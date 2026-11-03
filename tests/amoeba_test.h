/* tests/amoeba_test.h -- amoeba test harness (C89)
 *
 * Every suite starts with:
 *
 *   #include "amoeba_test.h"
 *
 * which brings in amoeba.h with AM_IMPLEMENTATION (so tests can inspect the
 * internals: rows, refcount, symbols) plus the shared test utilities:
 *
 *   am_test_debugf()   AM_DEBUG sink: amC/amE failures land in the log
 *   am_test_allocf()   accounting allocator; aborts on OOM
 *   allmem/maxmem      live and peak bytes, checked after every test
 *   am_test_new_solver() a solver wired to the accounting allocator
 *   test_after()       runner hook: asserts no memory leaked
 *
 * The old test.c kept an OOM path alive with setjmp/longjmp because a single
 * process ran every test.  Each test now runs in its own process (see the
 * `run` recipe in tests/justfile), so an allocation failure is simply a test
 * failure: abort with a message.
 */

#ifndef AMOEBA_TEST_H
#define AMOEBA_TEST_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- logging --------------------------------------------------------- */

static void am_test_debugf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/* AM_DEBUG must be defined before amoeba.h's implementation. */
#define AM_DEBUG am_test_debugf

/* 1-byte write buffer: forces the dump writer's flush/refill path. */
#define AM_BUF_LEN 1

#define AM_IMPLEMENTATION
#include "amoeba.h"

/* ---- runner ---------------------------------------------------------- */

/* Declared before tests.h so test_run() can call it. */
static void am_test_after(const char *name);
#define test_after(name) am_test_after(name)
#include "tests.h"

/* ---- allocation accounting ------------------------------------------- */

static size_t allmem = 0;
static size_t maxmem = 0;

static void *am_test_allocf(void **pud, void *ptr, size_t ns, size_t os,
                            am_Type ty) {
    void *newptr = NULL;
    (void)pud, (void)ty;
    allmem += ns;
    allmem -= os;
    if (maxmem < allmem) maxmem = allmem;
    if (ns == 0) {
        free(ptr);
    } else {
        newptr = realloc(ptr, ns);
        if (newptr == NULL) {
            test_log("FATAL %s:%d: out of memory (%lu bytes)\n", __FILE__,
                     __LINE__, (unsigned long)ns);
            abort();
        }
    }
    return newptr;
}

static am_Solver *am_test_new_solver(void) {
    am_Solver *S = am_newsolver(am_test_allocf, NULL);
    assertok(S != NULL);
    return S;
}

/* Every test must release what it allocated: this runs after each test
 * function and turns a leak into a failure of that test. */
static void am_test_after(const char *name) {
    if (allmem != 0) {
        test_log("FAIL %s: leaked %lu bytes (peak %lu)\n", name,
                 (unsigned long)allmem, (unsigned long)maxmem);
        abort();
    }
    maxmem = 0;
}

/* ---- float helpers --------------------------------------------------- */

/* Constraint solving is iterative: compare with a tolerance, never exactly. */
#define assertnear(a, b, eps)                                                \
    do {                                                                     \
        double _a = (double)(a), _b = (double)(b), _e = (double)(eps);       \
        if (!(_a - _b <= _e && _b - _a <= _e)) {                             \
            test_log("FAIL %s:%d: %s == %g, expected %g (+-%g)\n", __FILE__, \
                     __LINE__, #a, _a, _b, _e);                              \
            abort();                                                         \
        }                                                                    \
    } while (0)

#endif /* AMOEBA_TEST_H */
