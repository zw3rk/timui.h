/*
 * test.h — tiny unit-test harness for timui.h (test-only).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIMUI_TEST_H
#define TIMUI_TEST_H

#include <stddef.h>
#include <stdio.h>

/* Global failure counter (test-only mutable state — not production code). */
extern int timui_test_failures;

/* A test is a void function; failures bump the global counter. */
#define TIMUI_TEST(name) void name(void)
#define TIMUI_CHECK(cond)                                                   \
    do {                                                                    \
        if(!(cond)) {                                                       \
            ++timui_test_failures;                                          \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                   \
    } while(0)

/* Registry of unit tests — add one line per new test function. */
void test_rect_cuts(void);
void test_rect_split(void);
void test_rect_inset_clamp(void);
void test_ids_stable(void);

#endif /* TIMUI_TEST_H */
