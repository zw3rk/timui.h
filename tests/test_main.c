/*
 * test_main.c — registers and runs all timui.h unit tests.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>

#include "test.h"

int timui_test_failures = 0;

int main(void){
    typedef void (*test_fn)(void);
    test_fn tests[] = {
        test_rect_cuts,
        test_rect_split,
        test_rect_inset_clamp,
        test_ids_stable,
    };
    size_t i;
    size_t n = sizeof(tests) / sizeof(tests[0]);

    for(i = 0; i < n; ++i) tests[i]();

    if(timui_test_failures == 0)
        printf("all tests passed (%zu)\n", n);
    else
        printf("%d CHECK failure(s)\n", timui_test_failures);

    return timui_test_failures == 0 ? 0 : 1;
}
