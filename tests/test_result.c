/*
 * test_result.c — TimuiResult stringification + version (T1.1 close-out).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

TIMUI_TEST(test_error_string){
    /* every enumerator stringifies to its exact human string */
    TIMUI_CHECK(strcmp(timui_error_string(TIMUI_OK), "ok") == 0);
    TIMUI_CHECK(strcmp(timui_error_string(TIMUI_ERR_INVALID_ARGUMENT), "invalid argument") == 0);
    TIMUI_CHECK(strcmp(timui_error_string(TIMUI_ERR_NOT_A_TTY), "not a tty") == 0);
    TIMUI_CHECK(strcmp(timui_error_string(TIMUI_ERR_UNSUPPORTED), "unsupported") == 0);

    /* out-of-range values still return a valid, non-NULL string */
    TIMUI_CHECK(timui_error_string((TimuiResult)9999) != NULL);
}

TIMUI_TEST(test_version){
    TIMUI_CHECK(strcmp(timui_version_string(), "0.1.0") == 0);
}
