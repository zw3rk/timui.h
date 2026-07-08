/*
 * conpty_smoke_win32.c — operator smoke for the Windows ConPTY backend.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#include "timui.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/timui_conpty.c"

#define TOKEN "TIMUI_CONPTY_SMOKE"

static long smoke_now_ms(void){
#ifdef _WIN32
    return (long)GetTickCount64();
#else
    return (long)((clock() * 1000) / CLOCKS_PER_SEC);
#endif
}

static int smoke_contains(const char *buf, size_t len, const char *needle){
    size_t nlen = strlen(needle);
    size_t i;
    if(nlen == 0 || len < nlen) return 0;
    for(i = 0; i + nlen <= len; i++)
        if(memcmp(buf + i, needle, nlen) == 0) return 1;
    return 0;
}

static const char *smoke_result_name(TimuiResult r){
    switch(r){
        case TIMUI_OK: return "TIMUI_OK";
        case TIMUI_ERR_UNSUPPORTED: return "TIMUI_ERR_UNSUPPORTED";
        case TIMUI_ERR_INVALID_ARGUMENT: return "TIMUI_ERR_INVALID_ARGUMENT";
        case TIMUI_ERR_OUT_OF_MEMORY: return "TIMUI_ERR_OUT_OF_MEMORY";
        case TIMUI_ERR_NOT_A_TTY: return "TIMUI_ERR_NOT_A_TTY";
        case TIMUI_ERR_OS: return "TIMUI_ERR_OS";
        case TIMUI_ERR_PROTOCOL: return "TIMUI_ERR_PROTOCOL";
        default: return "TIMUI_ERR_UNKNOWN";
    }
}

static void smoke_sleep_short(void){
#ifdef _WIN32
    Sleep(10);
#else
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 10 * 1000 * 1000;
    nanosleep(&ts, NULL);
#endif
}

int main(void){
    TimuiTransport t;
    TimuiResult r;
    int pid = 0, ok = 0;
    char seen[8192];
    size_t seen_len = 0;
    long deadline;
    const char script[] = "echo " TOKEN "\r\nexit\r\n";

    memset(&t, 0, sizeof t);
    memset(seen, 0, sizeof seen);
    r = timui_conpty_open(&t, &pid);
    if(r != TIMUI_OK){
        fprintf(stderr, "conpty smoke: timui_conpty_open failed: %s\n",
                smoke_result_name(r));
        return r == TIMUI_ERR_UNSUPPORTED ? 77 : 1;
    }
    r = timui_conpty_resize(&t, 80, 24);
    if(r != TIMUI_OK){
        fprintf(stderr, "conpty smoke: resize failed: %s\n", smoke_result_name(r));
        timui_conpty_close(&t, pid);
        return 1;
    }
    if(!t.write || !t.read || t.write(&t, script, sizeof script - 1) <= 0){
        fprintf(stderr, "conpty smoke: write failed\n");
        timui_conpty_close(&t, pid);
        return 1;
    }

    deadline = smoke_now_ms() + 5000;
    while(smoke_now_ms() < deadline){
        char tmp[512];
        int n = t.read(&t, tmp, sizeof tmp);
        if(n > 0){
            size_t copy = (size_t)n;
            if(copy > sizeof seen - 1 - seen_len) copy = sizeof seen - 1 - seen_len;
            if(copy > 0){
                memcpy(seen + seen_len, tmp, copy);
                seen_len += copy;
                seen[seen_len] = '\0';
            }
            if(smoke_contains(seen, seen_len, TOKEN)){
                ok = 1;
                break;
            }
        } else {
            smoke_sleep_short();
        }
    }

    timui_conpty_close(&t, pid);
    timui_conpty_close(&t, pid);
    if(!ok){
        fprintf(stderr, "conpty smoke: sentinel not observed; captured %zu bytes\n", seen_len);
        return 1;
    }
    printf("PASS conpty smoke: observed %s\n", TOKEN);
    return 0;
}
