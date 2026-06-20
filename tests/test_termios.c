/*
 * test_termios.c — POSIX raw-mode enter/restore round-trip on a real pty (T2.2).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/* Exercises the real termios path through a posix_openpt pty pair (no -lutil
 * needed): raw mode clears ICANON/ECHO; restore reproduces the original c_lflag. */
TIMUI_TEST(test_termios_raw_and_restore){
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    struct termios orig, after_raw, after_restore;
    TimuiTermios t;
    char *name;
    int slave;

    TIMUI_CHECK(master >= 0);
    if(master < 0) return;
    if(grantpt(master) != 0 || unlockpt(master) != 0){ close(master); TIMUI_CHECK(0); return; }

    name = ptsname(master);
    TIMUI_CHECK(name != NULL);
    if(!name){ close(master); return; }
    slave = open(name, O_RDWR);
    TIMUI_CHECK(slave >= 0);
    if(slave < 0){ close(master); return; }

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
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    TimuiTermios t;
    char *name;
    int slave;

    TIMUI_CHECK(master >= 0);
    if(master < 0) return;
    if(grantpt(master) != 0 || unlockpt(master) != 0){ close(master); TIMUI_CHECK(0); return; }
    name = ptsname(master);
    if(!name){ close(master); TIMUI_CHECK(0); return; }
    slave = open(name, O_RDWR);
    if(slave < 0){ close(master); TIMUI_CHECK(0); return; }

    timui_termios_fail_tcsetattr_for_test(1);            /* arm the seam */
    TIMUI_CHECK(timui_termios_enter(&t, slave) == TIMUI_ERR_OS);
    timui_termios_fail_tcsetattr_for_test(0);            /* disarm immediately */

    TIMUI_CHECK(t.have_saved == 0 && t.saved == NULL);   /* V11: cleared, not dangling */
    TIMUI_CHECK(timui_termios_restore(&t) == TIMUI_ERR_INVALID_ARGUMENT);  /* nothing to restore */
    timui_termios_destroy(&t);                           /* safe: no double-free (ASAN) */

    close(slave);
    close(master);
}
