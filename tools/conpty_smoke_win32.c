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

static unsigned long long smoke_now_ms(void){
#ifdef _WIN32
    return (unsigned long long)GetTickCount64();
#else
    return (unsigned long long)((clock() * 1000) / CLOCKS_PER_SEC);
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

static void smoke_append(char *dst, size_t *dst_len, size_t dst_cap,
                         const char *src, size_t src_len){
    size_t copy;
    if(!dst || !dst_len || dst_cap == 0) return;
    if(*dst_len >= dst_cap - 1) return;
    copy = src_len;
    if(copy > dst_cap - 1 - *dst_len) copy = dst_cap - 1 - *dst_len;
    if(copy > 0){
        memcpy(dst + *dst_len, src, copy);
        *dst_len += copy;
        dst[*dst_len] = '\0';
    }
}

static const char *smoke_echo_script_for_attempt(int attempt){
    switch(attempt){
        case 0:
            /* Microsoft ConPTY examples serialize Enter as LF. */
            return "echo " TOKEN "\n";
        case 1:
            return "echo " TOKEN "\r\n";
        default:
            return NULL;
    }
}

static const char *smoke_exit_script_for_attempt(int attempt){
    switch(attempt){
        case 0:
            return "exit\n";
        case 1:
            return "exit\r\n";
        default:
            return NULL;
    }
}

static int smoke_write_exact(TimuiTransport *t, const char *script, size_t len,
                             int *out_last_write){
    int n;
    if(out_last_write) *out_last_write = 0;
    if(!t || !t->write || !script || len > (size_t)INT_MAX) return 0;
    n = t->write(t, script, len);
    if(out_last_write) *out_last_write = n;
    return n == (int)len;
}

static void smoke_dump_excerpt(FILE *f, const char *buf, size_t len){
    size_t i, limit;
    if(!f || !buf) return;
    limit = len < 512u ? len : 512u;
    fprintf(f, "conpty smoke: captured excerpt (%zu/%zu bytes): ", limit, len);
    for(i = 0; i < limit; i++){
        unsigned char c = (unsigned char)buf[i];
        if(c == '\r') fputs("\\r", f);
        else if(c == '\n') fputs("\\n", f);
        else if(c == '\t') fputs("\\t", f);
        else if(c == 0x1b) fputs("\\x1b", f);
        else if(c >= 0x20 && c < 0x7f) fputc((int)c, f);
        else fprintf(f, "\\x%02x", (unsigned)c);
    }
    if(limit < len) fputs("...", f);
    fputc('\n', f);
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
    int ok = 0, attempt = 0, attempts_run = 0, last_write = 0;
    char seen[8192];
    size_t seen_len = 0;

    memset(seen, 0, sizeof seen);

    for(attempt = 0; !ok && smoke_echo_script_for_attempt(attempt); attempt++){
        TimuiTransport t;
        TimuiResult r;
        int pid = 0;
        unsigned long long deadline;
        const char *echo_script = smoke_echo_script_for_attempt(attempt);
        const char *exit_script = smoke_exit_script_for_attempt(attempt);

        attempts_run++;
        memset(&t, 0, sizeof t);
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
        if(!t.write || !t.read){
            fprintf(stderr, "conpty smoke: transport callbacks missing\n");
            timui_conpty_close(&t, pid);
            return 1;
        }

        deadline = smoke_now_ms() + 2000;
        while(smoke_now_ms() < deadline){
            char tmp[512];
            int n = t.read(&t, tmp, sizeof tmp);
            if(n > 0){
                smoke_append(seen, &seen_len, sizeof seen, tmp, (size_t)n);
                if(memchr(tmp, '>', (size_t)n) || memchr(tmp, '$', (size_t)n))
                    break;
            } else {
                smoke_sleep_short();
            }
        }

        if(!smoke_write_exact(&t, echo_script, strlen(echo_script), &last_write)){
            fprintf(stderr,
                    "conpty smoke: write failed or short on attempt %d; last_write=%d\n",
                    attempt, last_write);
            timui_conpty_close(&t, pid);
            return 1;
        }

        deadline = smoke_now_ms() + 5000;
        while(smoke_now_ms() < deadline){
            char tmp[512];
            int n = t.read(&t, tmp, sizeof tmp);
            if(n > 0){
                smoke_append(seen, &seen_len, sizeof seen, tmp, (size_t)n);
                if(smoke_contains(seen, seen_len, TOKEN)){
                    ok = 1;
                    break;
                }
            } else {
                smoke_sleep_short();
            }
        }

        if(exit_script){
            int exit_write = 0;
            (void)smoke_write_exact(&t, exit_script, strlen(exit_script), &exit_write);
        }
        timui_conpty_close(&t, pid);
        timui_conpty_close(&t, pid);
    }
    if(!ok){
        fprintf(stderr,
                "conpty smoke: sentinel not observed; captured %zu bytes; attempts=%d; last_write=%d\n",
                seen_len, attempts_run, last_write);
        smoke_dump_excerpt(stderr, seen, seen_len);
        return 1;
    }
    printf("PASS conpty smoke: observed %s\n", TOKEN);
    return 0;
}
