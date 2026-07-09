/*
 * test_transport.c — transport abstraction + fake backend (T2.1).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

TIMUI_TEST(test_fake_capture_output){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiStr out;
    TIMUI_CHECK(timui_fake_init(&f, &al) == TIMUI_OK);
    t = timui_fake_transport(&f);
    TIMUI_CHECK(t.write(&t, "abc", 3) == 3);
    TIMUI_CHECK(t.write(&t, "de", 2) == 2);
    TIMUI_CHECK(t.flush(&t) == 0);
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len == 5 && memcmp(out.ptr, "abcde", 5) == 0);
    timui_fake_destroy(&f);
}

TIMUI_TEST(test_fake_inject_input){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport f;
    TimuiTransport t;
    char buf[8];
    TIMUI_CHECK(timui_fake_init(&f, &al) == TIMUI_OK);
    t = timui_fake_transport(&f);
    timui_fake_set_input(&f, "\x1b[A", 3);          /* Up-arrow escape */
    TIMUI_CHECK(t.read(&t, buf, sizeof buf) == 3);
    TIMUI_CHECK(memcmp(buf, "\x1b[A", 3) == 0);
    TIMUI_CHECK(t.read(&t, buf, sizeof buf) == 0);  /* drained */
    timui_fake_destroy(&f);
}

TIMUI_TEST(test_fake_grows){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiStr out;
    char big[256];
    int i;
    for(i = 0; i < 256; i++) big[i] = (char)('A' + (i % 26));
    TIMUI_CHECK(timui_fake_init(&f, &al) == TIMUI_OK);
    t = timui_fake_transport(&f);
    TIMUI_CHECK(t.write(&t, big, sizeof big) == 256);
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len == 256 && memcmp(out.ptr, big, 256) == 0);
    timui_fake_destroy(&f);
}

typedef struct CloseProbe {
    int closed;
} CloseProbe;

static int close_probe_write(TimuiTransport *t, const void *data, size_t len){
    (void)t; (void)data;
    return (int)len;
}
static int close_probe_read(TimuiTransport *t, void *buf, size_t cap){
    (void)t; (void)buf; (void)cap;
    return 0;
}
static int close_probe_flush(TimuiTransport *t){
    (void)t;
    return 0;
}
static void close_probe_close(TimuiTransport *t){
    CloseProbe *p = (CloseProbe *)t->ctx;
    p->closed++;
}

TIMUI_TEST(test_transport_close_hook){
    TimuiAllocator al = timui_default_allocator();
    CloseProbe p = {0};
    TimuiTransport t;
    Timui *ui = NULL;

    t.write = close_probe_write;
    t.read = close_probe_read;
    t.flush = close_probe_flush;
    t.close = close_probe_close;
    t.ctx = &p;

    TIMUI_CHECK(timui_open_for_test(&ui, t, 2, 2, &al) == TIMUI_OK);
    TIMUI_CHECK(p.closed == 0);
    timui_close(ui);
    TIMUI_CHECK(p.closed == 1);
}

TIMUI_TEST(test_open_failure_clears_output_handle){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport f;
    TimuiTransport t;
    char stale;
    Timui *ui = (Timui *)&stale;

    TIMUI_CHECK(timui_fake_init(&f, &al) == TIMUI_OK);
    t = timui_fake_transport(&f);
    TIMUI_CHECK(timui_open_for_test(&ui, t, 0, 1, &al) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(ui == NULL);

    ui = (Timui *)&stale;
    TIMUI_CHECK(timui_open(NULL, &ui) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(ui == NULL);
    timui_fake_destroy(&f);
}

typedef struct FailAllocProbe {
    TimuiAllocator base;
    int calls;
    int fail_on;
} FailAllocProbe;

static void *fail_probe_alloc(void *userdata, size_t size){
    FailAllocProbe *p = (FailAllocProbe *)userdata;
    p->calls++;
    if(p->calls == p->fail_on) return NULL;
    return p->base.alloc(p->base.userdata, size);
}
static void *fail_probe_realloc(void *userdata, void *ptr, size_t old_size, size_t new_size){
    FailAllocProbe *p = (FailAllocProbe *)userdata;
    return p->base.realloc(p->base.userdata, ptr, old_size, new_size);
}
static void fail_probe_free(void *userdata, void *ptr, size_t size){
    FailAllocProbe *p = (FailAllocProbe *)userdata;
    p->base.free(p->base.userdata, ptr, size);
}

TIMUI_TEST(test_open_for_test_failure_does_not_close_injected_transport){
    TimuiAllocator base = timui_default_allocator();
    FailAllocProbe fp;
    TimuiAllocator fail_al;
    CloseProbe cp = {0};
    TimuiTransport t;
    Timui *ui = NULL;

    fp.base = base;
    fp.calls = 0;
    fp.fail_on = 2;  /* Timui allocation succeeds; first cell-buffer allocation fails. */
    fail_al.alloc = fail_probe_alloc;
    fail_al.realloc = fail_probe_realloc;
    fail_al.free = fail_probe_free;
    fail_al.userdata = &fp;

    t.write = close_probe_write;
    t.read = close_probe_read;
    t.flush = close_probe_flush;
    t.close = close_probe_close;
    t.ctx = &cp;

    TIMUI_CHECK(timui_open_for_test(&ui, t, 4, 4, &fail_al) == TIMUI_ERR_OUT_OF_MEMORY);
    TIMUI_CHECK(ui == NULL);
    TIMUI_CHECK(cp.closed == 0);
}

/* The real-fd transport must write EVERY byte even when the fd is non-blocking
 * and its buffer is full (heavy render + fast typing) — a single write() that
 * dropped the remainder loses render bytes and garbles the screen. Drive a pipe
 * whose write end is O_NONBLOCK, drained slowly by a reader thread, and write
 * far more than the pipe buffer: timui_write_all_ must transfer all of it. */
int timui_write_all_(int fd, const void *d, size_t n);   /* internal, tested here */

#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#define WA_N (256 * 1024)
static void *wa_drain(void *arg){
    int fd = *(int *)arg;
    unsigned char buf[4096];
    size_t got = 0;
    while(got < WA_N){
        ssize_t r = read(fd, buf, sizeof buf);
        if(r > 0) got += (size_t)r;
        else if(r == 0) break;                 /* writer closed */
    }
    return NULL;
}
TIMUI_TEST(test_write_all_under_backpressure){
    int fds[2];
    int fl, ret, i;
    pthread_t th;
    static unsigned char big[WA_N];
    if(pipe(fds) != 0){ TIMUI_CHECK(0); return; }
    fl = fcntl(fds[1], F_GETFL, 0);
    TIMUI_CHECK(fl >= 0 && fcntl(fds[1], F_SETFL, fl | O_NONBLOCK) == 0);   /* non-blocking write end */
    for(i = 0; i < WA_N; i++) big[i] = (unsigned char)(i & 0xFF);
    TIMUI_CHECK(pthread_create(&th, NULL, wa_drain, &fds[0]) == 0);
    ret = timui_write_all_(fds[1], big, WA_N);                             /* must not drop bytes */
    TIMUI_CHECK(ret == WA_N);
    close(fds[1]);
    pthread_join(th, NULL);
    close(fds[0]);
    /* a naive single write() would return a short count here (< WA_N). */
}
