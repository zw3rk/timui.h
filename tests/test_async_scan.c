/*
 * test_async_scan.c — pure state tests for examples/async_scan.c.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "../examples/async_scan_state.h"

#include <string.h>

TIMUI_TEST(test_async_scan_state_valid_messages){
    AsyncScanModel m = {0, 0};
    int p = 40;
    TIMUI_CHECK(async_scan_update(&m, ASYNC_SCAN_MSG_PROGRESS, &p, sizeof p));
    TIMUI_CHECK(m.progress == 40 && !m.done);
    TIMUI_CHECK(async_scan_update(&m, ASYNC_SCAN_MSG_DONE, NULL, 0));
    TIMUI_CHECK(m.progress == 100 && m.done);
}

TIMUI_TEST(test_async_scan_state_rejects_malformed_progress){
    AsyncScanModel m = {30, 0};
    unsigned char tiny[1] = { 99 };
    int p = 70;
    TIMUI_CHECK(!async_scan_update(&m, ASYNC_SCAN_MSG_PROGRESS, tiny, sizeof tiny));
    TIMUI_CHECK(m.progress == 30 && !m.done);
    TIMUI_CHECK(!async_scan_update(&m, ASYNC_SCAN_MSG_PROGRESS, &p, sizeof p - 1));
    TIMUI_CHECK(m.progress == 30 && !m.done);
    TIMUI_CHECK(!async_scan_update(&m, ASYNC_SCAN_MSG_PROGRESS, NULL, sizeof p));
    TIMUI_CHECK(m.progress == 30 && !m.done);
}

TIMUI_TEST(test_async_scan_state_clamps_progress){
    AsyncScanModel m = {30, 0};
    int p = -10;
    TIMUI_CHECK(async_scan_update(&m, ASYNC_SCAN_MSG_PROGRESS, &p, sizeof p));
    TIMUI_CHECK(m.progress == 0);
    p = 130;
    TIMUI_CHECK(async_scan_update(&m, ASYNC_SCAN_MSG_PROGRESS, &p, sizeof p));
    TIMUI_CHECK(m.progress == 100);
}
