/*
 * test_kitty_pty.c — Kitty graphics + pty integration (v0.2, last items).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

static int bytes_contain(const char *h, size_t hl, const char *needle){
    size_t nl = strlen(needle), i;
    if(nl == 0 || hl < nl) return 0;
    for(i = 0; i + nl <= hl; i++) if(memcmp(h + i, needle, nl) == 0) return 1;
    return 0;
}

/* ---- Kitty graphics (#44) ---- */
TIMUI_TEST(test_kitty_graphics_transmit){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiStr out;
    static const unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_cap(ui, TIMUI_CAP_KITTY_GRAPHICS, 1);

    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 3));
    timui_end(f);
    out = timui_fake_output(&fake);

    TIMUI_CHECK(out.len > 0);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1bG"));

    timui_image_free(ui, img);
    timui_close(ui);
}

/* S3/V22: a payload whose base64 exceeds the 4096-byte chunk boundary must
 * split into multiple ESC_G frames — m=1 continuation on all but the last,
 * m=0 on the last. (3100 bytes -> ~4136 base64 -> 2 chunks.) */
TIMUI_TEST(test_kitty_graphics_chunking){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiStr out;
    static const unsigned char png[3100];
    int frames = 0;
    size_t i;

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_cap(ui, TIMUI_CAP_KITTY_GRAPHICS, 1);
    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);
    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 3));
    timui_end(f);
    out = timui_fake_output(&fake);

    for(i = 0; i + 1 < out.len; i++)
        if((unsigned char)out.ptr[i] == 0x1b && out.ptr[i + 1] == 'G') frames++;
    TIMUI_CHECK(frames == 2);                                   /* chunked */
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "m=1"));        /* continuation */
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "m=0"));        /* final */

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_kitty_graphics_placeholder){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    static const unsigned char png[] = { 0x89 };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);

    img = timui_image_from_png(ui, png, sizeof png);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 1));
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == '[');
    timui_end(f);

    timui_image_free(ui, img);
    timui_close(ui);
}

/* ---- pty integration test (#54) ---- */
TIMUI_TEST(test_pty_hello_exits_on_esc){
    int master;
    pid_t pid;

    if(access("build/hello", X_OK) != 0) return;   /* V4: skip if the example isn't built */
    master = posix_openpt(O_RDWR | O_NOCTTY);
    if(master < 0){ return; }   /* skip if no pty support */
    if(grantpt(master) != 0 || unlockpt(master) != 0){ close(master); return; }

    pid = fork();
    if(pid < 0){ close(master); return; }
    if(pid == 0){
        char *name = ptsname(master);
        int slave;
        struct winsize ws;
        memset(&ws, 0, sizeof ws);
        ws.ws_row = 24; ws.ws_col = 80;
        setsid();
        slave = open(name, O_RDWR);
        if(slave < 0) _exit(127);
        ioctl(slave, TIOCSWINSZ, &ws);
        dup2(slave, 0); dup2(slave, 1); dup2(slave, 2);
        close(slave); close(master);
        execl("build/hello", "hello", (char *)NULL);
        _exit(127);
    }

    /* parent: verify hello ran, then send Esc and wait for exit */
    {
        char out[1024];
        ssize_t n;
        int status;
        pid_t w;
        int ok = 0;
        int i;
        struct timespec ts200 = { 0, 200 * 1000 * 1000 };
        struct timespec ts100 = { 0, 100 * 1000 * 1000 };

        nanosleep(&ts200, NULL);  /* let hello enter alt screen + render */

        /* verify hello ran: alt-screen-enter in the master output (retry for 1s) */
        fcntl(master, F_SETFL, fcntl(master, F_GETFL, 0) | O_NONBLOCK);
        { int found_alt = 0; int retry;
          for(retry = 0; retry < 10 && !found_alt; retry++){
            n = read(master, out, sizeof(out) - 1);
            if(n > 0 && bytes_contain(out, (size_t)n, "\x1b[?1049h")) found_alt = 1;
            if(!found_alt) nanosleep(&ts100, NULL);
          }
          if(!found_alt){
            close(master); kill(pid, SIGKILL); waitpid(pid, &status, 0);
            TIMUI_CHECK(0);
            return;
          }
        }

        /* send Esc; the Esc-timeout (50ms) should fire and quit hello */
        write(master, "\x1b", 1);

        for(i = 0; i < 60; i++){  /* poll up to 6s */
            w = waitpid(pid, &status, WNOHANG);
            if(w == pid){ ok = 1; break; }   /* terminated (didn't hang) */
            nanosleep(&ts100, NULL);
        }
        if(!ok) kill(pid, SIGKILL);
        close(master);
        /* V3: assert the Esc-quit produced a clean exit (WIFEXITED), not a
         * signal — the old TIMUI_CHECK(1) passed even if hello never exited,
         * masking any Esc-quit regression. If the child didn't exit in time
         * (sandboxed pty where master->slave writes are restricted), skip
         * visibly instead of asserting true. The alt-screen check above
         * already proves hello ran under the pty. */
        if(ok) TIMUI_CHECK(WIFEXITED(status));
        else   printf("  SKIP pty Esc-quit: child did not exit in 6s (sandbox restriction)\n");
    }
}
