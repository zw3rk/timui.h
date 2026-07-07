/*
 * test_mpsc.c — thread-safe MPSC queue (T1.7).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

/* V1: a wrapped size must be rejected, not allocated tiny then memcpy'd huge. */
TIMUI_TEST(test_mpsc_overflow_guard){
    TimuiAllocator al = timui_default_allocator();
    TimuiMpsc q;
    TIMUI_CHECK(timui_mpsc_init(&q, &al) == TIMUI_OK);
    TIMUI_CHECK(timui_mpsc_post(&q, 1, "x", (size_t)-1) == 0);   /* SIZE_MAX -> guard */
    timui_mpsc_destroy(&q);
}

TIMUI_TEST(test_mpsc_null_data_rejected){
    TimuiAllocator al = timui_default_allocator();
    TimuiMpsc q;
    uint32_t type = 0;
    size_t sz = 1;
    TIMUI_CHECK(timui_mpsc_init(&q, &al) == TIMUI_OK);
    TIMUI_CHECK(timui_mpsc_post(&q, 1, NULL, 1) == 0);
    TIMUI_CHECK(timui_mpsc_empty(&q));
    TIMUI_CHECK(timui_mpsc_post(&q, 2, NULL, 0) == 1);
    TIMUI_CHECK(timui_mpsc_recv(&q, &type, NULL, &sz) == 1);
    TIMUI_CHECK(type == 2 && sz == 0);
    timui_mpsc_destroy(&q);
}

TIMUI_TEST(test_mpsc_destroy_is_idempotent){
    TimuiAllocator al = timui_default_allocator();
    TimuiMpsc q;
    uint32_t type = 0;
    size_t sz = 0;

    TIMUI_CHECK(timui_mpsc_init(&q, &al) == TIMUI_OK);
    TIMUI_CHECK(timui_mpsc_post(&q, 1, "x", 1));
    timui_mpsc_destroy(&q);
    timui_mpsc_destroy(&q);
    TIMUI_CHECK(timui_mpsc_empty(&q));
    TIMUI_CHECK(!timui_mpsc_recv(&q, &type, NULL, &sz));
    TIMUI_CHECK(!timui_mpsc_post(&q, 2, NULL, 0));
}

typedef struct {
    pthread_mutex_t guard;
    int violations;
} GuardAlloc;

static void guard_enter(GuardAlloc *g){
    struct timespec ts;
    if(pthread_mutex_trylock(&g->guard) != 0){
        g->violations++;
        pthread_mutex_lock(&g->guard);
    }
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000L;
    nanosleep(&ts, NULL);
}
static void guard_leave(GuardAlloc *g){ pthread_mutex_unlock(&g->guard); }
static void *guard_alloc(void *ud, size_t sz){
    GuardAlloc *g = (GuardAlloc *)ud;
    void *p;
    guard_enter(g);
    p = malloc(sz);
    guard_leave(g);
    return p;
}
static void *guard_realloc(void *ud, void *p, size_t os, size_t ns){
    GuardAlloc *g = (GuardAlloc *)ud;
    void *np;
    (void)os;
    guard_enter(g);
    np = realloc(p, ns);
    guard_leave(g);
    return np;
}
static void guard_free(void *ud, void *p, size_t sz){
    GuardAlloc *g = (GuardAlloc *)ud;
    (void)sz;
    guard_enter(g);
    free(p);
    guard_leave(g);
}

TIMUI_TEST(test_mpsc_serializes_custom_allocator){
    GuardAlloc ga;
    TimuiAllocator al;
    TimuiMpsc q;
    pthread_t th[MPSC_WORKERS];
    MpscArg args[MPSC_WORKERS];
    int i;

    ga.violations = 0;
    pthread_mutex_init(&ga.guard, NULL);
    al.userdata = &ga; al.alloc = guard_alloc; al.realloc = guard_realloc; al.free = guard_free;
    TIMUI_CHECK(timui_mpsc_init(&q, &al) == TIMUI_OK);

    for(i = 0; i < MPSC_WORKERS; i++){
        args[i].q = &q;
        args[i].id = i;
        TIMUI_CHECK(pthread_create(&th[i], NULL, mpsc_worker, &args[i]) == 0);
    }
    for(i = 0; i < MPSC_WORKERS; i++) pthread_join(th[i], NULL);

    TIMUI_CHECK(ga.violations == 0);
    timui_mpsc_destroy(&q);
    pthread_mutex_destroy(&ga.guard);
}
