/*
 * test_msgq.c — UI-thread message queue (T1.6).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

TIMUI_TEST(test_msgq_order_and_copy){
    TimuiAllocator al = timui_default_allocator();
    TimuiMsgQueue q;
    uint32_t type = 0;
    char buf[32];
    size_t sz;
    TIMUI_CHECK(timui_msgq_init(&q, &al, 256) == TIMUI_OK);

    TIMUI_CHECK(timui_msgq_emit(&q, 1, "hello",  5));
    TIMUI_CHECK(timui_msgq_emit(&q, 2, "world!", 6));

    sz = sizeof buf;
    TIMUI_CHECK(timui_msgq_recv(&q, &type, buf, &sz));
    TIMUI_CHECK(type == 1 && sz == 5 && memcmp(buf, "hello", 5) == 0);

    sz = sizeof buf;
    TIMUI_CHECK(timui_msgq_recv(&q, &type, buf, &sz));
    TIMUI_CHECK(type == 2 && sz == 6 && memcmp(buf, "world!", 6) == 0);

    /* drained */
    sz = sizeof buf;
    TIMUI_CHECK(!timui_msgq_recv(&q, &type, buf, &sz));
    TIMUI_CHECK(timui_msgq_empty(&q));
    timui_msgq_destroy(&q);
}

TIMUI_TEST(test_msgq_full_predictable){
    TimuiAllocator al = timui_default_allocator();
    TimuiMsgQueue q;
    /* cap 24: one 6-byte payload (header 12 + 6 = 18) fits; a second must not */
    TIMUI_CHECK(timui_msgq_init(&q, &al, 24) == TIMUI_OK);
    TIMUI_CHECK(timui_msgq_emit(&q, 7, "abcdef", 6));
    TIMUI_CHECK(!timui_msgq_emit(&q, 8, "ghijkl", 6));   /* over cap -> rejected, not corrupted */
    timui_msgq_destroy(&q);
}

TIMUI_TEST(test_msgq_variable_sizes){
    TimuiAllocator al = timui_default_allocator();
    TimuiMsgQueue q;
    uint32_t type = 0;
    char buf[64];
    size_t sz;
    TIMUI_CHECK(timui_msgq_init(&q, &al, 256) == TIMUI_OK);

    TIMUI_CHECK(timui_msgq_emit(&q, 10, "", 0));                       /* zero-length */
    TIMUI_CHECK(timui_msgq_emit(&q, 11, "x", 1));
    TIMUI_CHECK(timui_msgq_emit(&q, 12, "abcdefghijklmnop", 16));

    sz = sizeof buf; TIMUI_CHECK(timui_msgq_recv(&q, &type, buf, &sz));
    TIMUI_CHECK(type == 10 && sz == 0);

    sz = sizeof buf; TIMUI_CHECK(timui_msgq_recv(&q, &type, buf, &sz));
    TIMUI_CHECK(type == 11 && sz == 1 && buf[0] == 'x');

    sz = sizeof buf; TIMUI_CHECK(timui_msgq_recv(&q, &type, buf, &sz));
    TIMUI_CHECK(type == 12 && sz == 16 && memcmp(buf, "abcdefghijklmnop", 16) == 0);

    timui_msgq_destroy(&q);
}

/* V21: size>0 with NULL data would record a payload of uninitialised slab
 * bytes; reject it. */
TIMUI_TEST(test_msgq_null_data_rejected){
    TimuiAllocator al = timui_default_allocator();
    TimuiMsgQueue q;
    TIMUI_CHECK(timui_msgq_init(&q, &al, 256) == TIMUI_OK);
    TIMUI_CHECK(timui_msgq_emit(&q, 1, NULL, 10) == 0);
    timui_msgq_destroy(&q);
}
