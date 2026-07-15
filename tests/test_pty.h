/*
 * test_pty.h -- shared pty setup for integration-style tests.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIMUI_TEST_PTY_H
#define TIMUI_TEST_PTY_H

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static inline int timui_test_force_no_pty(void){
    const char *v = getenv("TIMUI_TEST_FORCE_NO_PTY");
    return v && v[0] && strcmp(v, "0") != 0 && strcmp(v, "false") != 0 && strcmp(v, "FALSE") != 0;
}

static inline void timui_test_skip_pty(const char *name, int err){
    if(err)
        printf("  SKIP %s: pty unavailable (%s)\n", name, strerror(err));
    else
        printf("  SKIP %s: pty unavailable\n", name);
}

static inline int timui_test_open_pty_master(const char *name, int *out_master){
    int master;
    int err;

    if(!out_master) return 0;
    *out_master = -1;

    if(timui_test_force_no_pty()){
        timui_test_skip_pty(name, ENOSYS);
        return 0;
    }

    master = posix_openpt(O_RDWR | O_NOCTTY);
    if(master < 0){
        timui_test_skip_pty(name, errno);
        return 0;
    }

    if(grantpt(master) != 0 || unlockpt(master) != 0){
        err = errno;
        close(master);
        timui_test_skip_pty(name, err);
        return 0;
    }

    *out_master = master;
    return 1;
}

static inline int timui_test_open_pty_pair(const char *name, int *out_master, int *out_slave){
    int master = -1;
    int slave;
    int err;
    char *slave_name;

    if(!out_master || !out_slave) return 0;
    *out_master = -1;
    *out_slave = -1;

    if(!timui_test_open_pty_master(name, &master)) return 0;

    slave_name = ptsname(master);
    if(!slave_name){
        err = errno;
        close(master);
        timui_test_skip_pty(name, err);
        return 0;
    }

    slave = open(slave_name, O_RDWR);
    if(slave < 0){
        err = errno;
        close(master);
        timui_test_skip_pty(name, err);
        return 0;
    }

    *out_master = master;
    *out_slave = slave;
    return 1;
}

#endif /* TIMUI_TEST_PTY_H */
