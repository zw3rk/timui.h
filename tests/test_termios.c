/*
 * test_termios.c — POSIX raw-mode enter/restore round-trip on a real pty (T2.2).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "test_pty.h"
#include "timui.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static void test_sigterm_handler(int sig){ (void)sig; }

static int termios_contains(const char *haystack, size_t haystack_len, const char *needle){
    size_t needle_len = strlen(needle);
    size_t i;
    if(needle_len == 0) return 1;
    if(haystack_len < needle_len) return 0;
    for(i = 0; i <= haystack_len - needle_len; i++){
        if(memcmp(haystack + i, needle, needle_len) == 0) return 1;
    }
    return 0;
}

static int pty_output_contains(int fd, const char *needle){
    char buf[512];
    size_t used = 0;
    int flags;
    int read_errno = 0;
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if(poll(&pfd, 1, 250) <= 0) return 0;
    flags = fcntl(fd, F_GETFL, 0);
    if(flags >= 0) (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    while(used < sizeof buf){
        ssize_t n = read(fd, buf + used, sizeof buf - used);
        if(n <= 0){ if(n < 0) read_errno = errno; break; }
        used += (size_t)n;
    }
    if(flags >= 0) (void)fcntl(fd, F_SETFL, flags);
    if(read_errno != 0 && read_errno != EAGAIN && read_errno != EWOULDBLOCK) return 0;
    return termios_contains(buf, used, needle);
}

/* Exercises the real termios path through a posix_openpt pty pair (no -lutil
 * needed): raw mode clears ICANON/ECHO; restore reproduces the original c_lflag. */
TIMUI_TEST(test_termios_raw_and_restore){
    int master;
    struct termios orig, after_raw, after_restore;
    TimuiTermios t;
    int slave;

    if(!timui_test_open_pty_pair(__func__, &master, &slave)) return;

    TIMUI_CHECK(tcgetattr(slave, &orig) == 0);

    TIMUI_CHECK(timui_termios_enter(&t, slave) == TIMUI_OK);
    TIMUI_CHECK(tcgetattr(slave, &after_raw) == 0);
    TIMUI_CHECK(!(after_raw.c_lflag & ICANON));   /* raw: canonical off */
    TIMUI_CHECK(!(after_raw.c_lflag & ECHO));     /* raw: echo off */

    TIMUI_CHECK(timui_termios_restore(&t) == TIMUI_OK);
    TIMUI_CHECK(tcgetattr(slave, &after_restore) == 0);
    TIMUI_CHECK(after_restore.c_lflag == orig.c_lflag);   /* exactly restored */

    timui_termios_destroy(&t);
    close(slave);
    close(master);
}

/* Z25 (guards V11): if tcsetattr fails during enter, the saved termios must be
 * freed AND have_saved/saved cleared, so a subsequent destroy does not double-
 * free and restore refuses (no stale pointer use). Driven via the tcsetattr
 * failure seam on a real pty slave (tcgetattr succeeds, then the seam forces the
 * tcsetattr branch). */
TIMUI_TEST(test_termios_setattr_failure){
    int master;
    TimuiTermios t;
    int slave;

    if(!timui_test_open_pty_pair(__func__, &master, &slave)) return;

    timui_termios_fail_tcsetattr_for_test(1);            /* arm the seam */
    TIMUI_CHECK(timui_termios_enter(&t, slave) == TIMUI_ERR_OS);
    timui_termios_fail_tcsetattr_for_test(0);            /* disarm immediately */

    TIMUI_CHECK(t.have_saved == 0 && t.saved == NULL);   /* V11: cleared, not dangling */
    TIMUI_CHECK(timui_termios_restore(&t) == TIMUI_ERR_INVALID_ARGUMENT);  /* nothing to restore */
    timui_termios_destroy(&t);                           /* safe: no double-free (ASAN) */

    close(slave);
    close(master);
}

TIMUI_TEST(test_termios_enter_failure_clears_state){
    TimuiTermios t;
    t.fd = 123;
    t.saved = (void *)1;
    t.have_saved = 1;

    TIMUI_CHECK(timui_termios_enter(&t, -1) == TIMUI_ERR_OS);
    TIMUI_CHECK(t.saved == NULL && t.have_saved == 0);
}

/* Regression: timui_open makes input non-blocking for frame polling, but the
 * caller owns the fd and must get its original status flags back on close. */
TIMUI_TEST(test_open_restores_input_fd_flags){
    int p[2];
    int orig, during, after;
    TimuiConfig cfg;
    Timui *ui = NULL;
    int ok;

    ok = pipe(p);
    TIMUI_CHECK(ok == 0);
    if(ok != 0) return;

    orig = fcntl(p[0], F_GETFL, 0);
    TIMUI_CHECK(orig >= 0);

    timui_config_init(&cfg);
    cfg.input_fd = p[0];
    cfg.output_fd = p[1];
    TIMUI_CHECK(timui_open(&cfg, &ui) == TIMUI_OK);
    during = fcntl(p[0], F_GETFL, 0);
    TIMUI_CHECK(during >= 0 && (during & O_NONBLOCK));

    timui_close(ui);
    after = fcntl(p[0], F_GETFL, 0);
    TIMUI_CHECK(after == orig);

    close(p[0]);
    close(p[1]);
}

TIMUI_TEST(test_open_fails_when_nonblock_set_fails){
    int p[2];
    int orig, after;
    TimuiConfig cfg;
    Timui *ui = NULL;
    TimuiResult r;

    TIMUI_CHECK(pipe(p) == 0);
    orig = fcntl(p[0], F_GETFL, 0);
    TIMUI_CHECK(orig >= 0);

    timui_config_init(&cfg);
    cfg.input_fd = p[0];
    cfg.output_fd = p[1];

    timui_open_fail_fsetfl_for_test(1);
    r = timui_open(&cfg, &ui);
    timui_open_fail_fsetfl_for_test(0);

    TIMUI_CHECK(r == TIMUI_ERR_OS);
    TIMUI_CHECK(ui == NULL);
    after = fcntl(p[0], F_GETFL, 0);
    TIMUI_CHECK(after == orig);

    close(p[0]);
    close(p[1]);
}

TIMUI_TEST(test_restore_terminal_restores_input_fd_flags){
    int p[2];
    int orig, during, after;
    TimuiConfig cfg;
    Timui *ui = NULL;
    int ok;

    ok = pipe(p);
    TIMUI_CHECK(ok == 0);
    if(ok != 0) return;

    orig = fcntl(p[0], F_GETFL, 0);
    timui_config_init(&cfg);
    cfg.input_fd = p[0];
    cfg.output_fd = p[1];
    TIMUI_CHECK(timui_open(&cfg, &ui) == TIMUI_OK);

    during = fcntl(p[0], F_GETFL, 0);
    TIMUI_CHECK(during >= 0 && (during & O_NONBLOCK));

    timui_restore_terminal(ui);
    after = fcntl(p[0], F_GETFL, 0);
    TIMUI_CHECK(after == orig);

    timui_close(ui);
    close(p[0]);
    close(p[1]);
}

TIMUI_TEST(test_open_restores_previous_signal_handler){
    int master;
    int slave, nullfd;
    struct sigaction orig, custom, after;
    TimuiConfig cfg;
    Timui *ui = NULL;

    if(!timui_test_open_pty_pair(__func__, &master, &slave)) return;
    nullfd = open("/dev/null", O_WRONLY);
    if(nullfd < 0){ close(slave); close(master); TIMUI_CHECK(0); return; }

    TIMUI_CHECK(sigaction(SIGTERM, NULL, &orig) == 0);
    memset(&custom, 0, sizeof custom);
    custom.sa_handler = test_sigterm_handler;
    sigemptyset(&custom.sa_mask);
    TIMUI_CHECK(sigaction(SIGTERM, &custom, NULL) == 0);

    timui_config_init(&cfg);
    cfg.input_fd = slave;
    cfg.output_fd = nullfd;
    cfg.flags = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_RESTORE_ON_EXIT;
    TIMUI_CHECK(timui_open(&cfg, &ui) == TIMUI_OK);
    timui_close(ui);

    TIMUI_CHECK(sigaction(SIGTERM, NULL, &after) == 0);
    TIMUI_CHECK(after.sa_handler == test_sigterm_handler);

    (void)sigaction(SIGTERM, &orig, NULL);
    close(nullfd);
    close(slave);
    close(master);
}

TIMUI_TEST(test_open_enters_screen_when_only_output_is_tty){
    int master;
    int slave, input;
    TimuiConfig cfg;
    Timui *ui = NULL;
    struct winsize ws;

    if(!timui_test_open_pty_pair(__func__, &master, &slave)) return;
    input = open("/dev/null", O_RDONLY);
    if(input < 0){ close(slave); close(master); TIMUI_CHECK(0); return; }
    memset(&ws, 0, sizeof ws);
    ws.ws_col = 80;
    ws.ws_row = 24;
    TIMUI_CHECK(ioctl(slave, TIOCSWINSZ, &ws) == 0);

    timui_config_init(&cfg);
    cfg.input_fd = input;
    cfg.output_fd = slave;
    cfg.flags = TIMUI_FLAG_ALT_SCREEN;

    TIMUI_CHECK(timui_open(&cfg, &ui) == TIMUI_OK);
    TIMUI_CHECK(pty_output_contains(master, "\x1b[?1049h"));

    timui_close(ui);
    close(input);
    close(slave);
    close(master);
}

TIMUI_TEST(test_open_fails_when_raw_mode_fails){
    int master;
    int slave, nullfd, orig_flags, after_flags;
    TimuiConfig cfg;
    Timui *ui = NULL;
    TimuiResult r;

    if(!timui_test_open_pty_pair(__func__, &master, &slave)) return;
    nullfd = open("/dev/null", O_WRONLY);
    if(nullfd < 0){ close(slave); close(master); TIMUI_CHECK(0); return; }

    orig_flags = fcntl(slave, F_GETFL, 0);
    timui_config_init(&cfg);
    cfg.input_fd = slave;
    cfg.output_fd = nullfd;

    timui_termios_fail_tcsetattr_for_test(1);
    r = timui_open(&cfg, &ui);
    timui_termios_fail_tcsetattr_for_test(0);

    TIMUI_CHECK(r == TIMUI_ERR_OS);
    TIMUI_CHECK(ui == NULL);
    after_flags = fcntl(slave, F_GETFL, 0);
    TIMUI_CHECK(orig_flags >= 0 && after_flags == orig_flags);

    close(nullfd);
    close(slave);
    close(master);
}

TIMUI_TEST(test_open_rejects_invalid_fds){
    TimuiConfig cfg;
    Timui *ui = NULL;
    TimuiResult r;
    int p[2];
    int ok;

    timui_config_init(&cfg);
    cfg.input_fd = -1;
    cfg.output_fd = 1;
    r = timui_open(&cfg, &ui);
    TIMUI_CHECK(r == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(ui == NULL);
    if(ui){ timui_close(ui); ui = NULL; }

    cfg.input_fd = 0;
    cfg.output_fd = -1;
    r = timui_open(&cfg, &ui);
    TIMUI_CHECK(r == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(ui == NULL);
    if(ui){ timui_close(ui); ui = NULL; }

    ok = pipe(p);
    TIMUI_CHECK(ok == 0);
    if(ok != 0) return;
    close(p[0]);
    cfg.input_fd = p[0];
    cfg.output_fd = p[1];
    r = timui_open(&cfg, &ui);
    TIMUI_CHECK(r == TIMUI_ERR_OS);
    TIMUI_CHECK(ui == NULL);
    if(ui){ timui_close(ui); ui = NULL; }
    close(p[1]);
}
