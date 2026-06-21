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
