/*
 * test_size.c — terminal size query (T2.4).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "test_pty.h"
#include "timui.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

TIMUI_TEST(test_term_size_query){
    int master;
    struct winsize ws;
    int w = 0, h = 0;
    int slave;

    if(!timui_test_open_pty_pair(__func__, &master, &slave)) return;

    ws.ws_col = 100; ws.ws_row = 40; ws.ws_xpixel = 0; ws.ws_ypixel = 0;
    TIMUI_CHECK(ioctl(slave, TIOCSWINSZ, &ws) == 0);
    TIMUI_CHECK(timui_term_size(slave, &w, &h) == TIMUI_OK);
    TIMUI_CHECK(w == 100 && h == 40);

    /* zero-size is handled safely (no crash, no negative) */
    ws.ws_col = 0; ws.ws_row = 0;
    TIMUI_CHECK(ioctl(slave, TIOCSWINSZ, &ws) == 0);
    TIMUI_CHECK(timui_term_size(slave, &w, &h) == TIMUI_OK);
    TIMUI_CHECK(w == 0 && h == 0);

    close(slave);
    close(master);
}

TIMUI_TEST(test_open_falls_back_from_zero_term_size){
    int master;
    struct winsize ws;
    TimuiConfig cfg;
    Timui *ui = NULL;
    int slave, input;

    if(!timui_test_open_pty_pair(__func__, &master, &slave)) return;
    input = open("/dev/null", O_RDONLY);
    TIMUI_CHECK(input >= 0);
    if(input < 0){ close(slave); close(master); return; }

    ws.ws_col = 0; ws.ws_row = 0; ws.ws_xpixel = 0; ws.ws_ypixel = 0;
    TIMUI_CHECK(ioctl(slave, TIOCSWINSZ, &ws) == 0);

    timui_config_init(&cfg);
    cfg.input_fd = input;
    cfg.output_fd = slave;
    TIMUI_CHECK(timui_open(&cfg, &ui) == TIMUI_OK);

    timui_close(ui);
    close(input);
    close(slave);
    close(master);
}

TIMUI_TEST(test_term_size_not_a_tty){
    int p[2];
    int w, h;
    TIMUI_CHECK(pipe(p) == 0);
    TIMUI_CHECK(timui_term_size(p[0], &w, &h) == TIMUI_ERR_NOT_A_TTY);
    close(p[0]);
    close(p[1]);
}

TIMUI_TEST(test_term_size_pixels_query){
    int master;
    struct winsize ws;
    int w = 0, h = 0, px_w = 0, px_h = 0;
    int slave;

    if(!timui_test_open_pty_pair(__func__, &master, &slave)) return;

    ws.ws_col = 100; ws.ws_row = 40; ws.ws_xpixel = 800; ws.ws_ypixel = 960;
    TIMUI_CHECK(ioctl(slave, TIOCSWINSZ, &ws) == 0);
    TIMUI_CHECK(timui_term_size_pixels(slave, &w, &h, &px_w, &px_h) == TIMUI_OK);
    TIMUI_CHECK(w == 100 && h == 40);
    TIMUI_CHECK(px_w == 800 && px_h == 960);

    close(slave);
    close(master);
}

TIMUI_TEST(test_term_size_pixels_not_a_tty){
    int p[2];
    int w, h, px_w, px_h;
    TIMUI_CHECK(pipe(p) == 0);
    TIMUI_CHECK(timui_term_size_pixels(p[0], &w, &h, &px_w, &px_h) == TIMUI_ERR_NOT_A_TTY);
    close(p[0]);
    close(p[1]);
}
