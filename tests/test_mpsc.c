/*
 * test_mpsc.c — thread-safe MPSC queue (T1.7).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <pthread.h>
#include <string.h>

#define MPSC_WORKERS 4
#define MPSC_PER     100

typedef struct { TimuiMpsc *q; int id; } MpscArg;

static void *mpsc_worker(void *p){
    MpscArg *a = (MpscArg *)p;
    int i;
    for(i = 0; i < MPSC_PER; i++){
        unsigned char byte = (unsigned char)(a->id * 31 + i);
        timui_mpsc_post(a->q, (uint32_t)a->id, &byte, 1);
    }
    return NULL;
}

TIMUI_TEST(test_mpsc_fifo_single){
    TimuiAllocator al = timui_default_allocator();
    TimuiMpsc q;
    uint32_t type = 0;
    char buf[8];
    size_t sz;
    TIMUI_CHECK(timui_mpsc_init(&q, &al) == TIMUI_OK);
    TIMUI_CHECK(timui_mpsc_post(&q, 1, "a", 1));
    TIMUI_CHECK(timui_mpsc_post(&q, 2, "b", 1));
    sz = sizeof buf;
    TIMUI_CHECK(timui_mpsc_recv(&q, &type, buf, &sz) && type == 1 && sz == 1 && buf[0] == 'a');
    sz = sizeof buf;
    TIMUI_CHECK(timui_mpsc_recv(&q, &type, buf, &sz) && type == 2 && sz == 1 && buf[0] == 'b');
    TIMUI_CHECK(timui_mpsc_empty(&q));
    timui_mpsc_destroy(&q);
}

TIMUI_TEST(test_mpsc_multi_producer){
    TimuiAllocator al = timui_default_allocator();
    TimuiMpsc q;
    pthread_t th[MPSC_WORKERS];
    MpscArg args[MPSC_WORKERS];
    int counts[MPSC_WORKERS] = {0};
    int i, total = 0;
    uint32_t type = 0;
    unsigned char byte = 0;
    size_t sz;

    TIMUI_CHECK(timui_mpsc_init(&q, &al) == TIMUI_OK);
    for(i = 0; i < MPSC_WORKERS; i++){
        args[i].q = &q;
        args[i].id = i;
        TIMUI_CHECK(pthread_create(&th[i], NULL, mpsc_worker, &args[i]) == 0);
    }
    for(i = 0; i < MPSC_WORKERS; i++) pthread_join(th[i], NULL);

    /* drain: all 400 messages arrive intact, each tagged with its producer */
    sz = 1;
    while(timui_mpsc_recv(&q, &type, &byte, &sz)){
        TIMUI_CHECK(type < (uint32_t)MPSC_WORKERS);
        counts[type]++;
        total++;
        sz = 1;
    }
    TIMUI_CHECK(total == MPSC_WORKERS * MPSC_PER);
    for(i = 0; i < MPSC_WORKERS; i++) TIMUI_CHECK(counts[i] == MPSC_PER);
    timui_mpsc_destroy(&q);
}
