/*
 * pty_drive.c — run a TUI app under a pseudo-terminal, feed it scripted input,
 * and capture its raw output byte stream. This is how you drive a timui app
 * "headless": a pty makes isatty() pass so the app runs its normal render loop,
 * while we inject keystrokes (from stdin) and record everything it emits (to
 * --out) — the exact byte stream the render verifier (tests/test_render_stream.c)
 * consumes, and the basis for scripted acceptance tests.
 *
 * usage:
 *   pty_drive --out FILE [--cols N] [--rows N] [--delay-ms N]
 *             [--settle-ms N] [--run-ms N] -- CMD [ARGS...]   < input-script
 *
 *   --delay-ms   inter-byte delay while sending input (simulate typing speed)
 *   --settle-ms  after input EOF, keep capturing this long before quitting
 *   --run-ms     hard cap on the whole session (then SIGTERM the child)
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#if defined(__APPLE__) || defined(__linux__)
#include <stdlib.h>   /* posix_openpt / grantpt / unlockpt / ptsname */
#endif

static long now_ms(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static void write_all(int fd, const char *b, size_t n){
    size_t off = 0;
    while(off < n){ ssize_t w = write(fd, b + off, n - off); if(w > 0) off += (size_t)w; else if(errno != EINTR) break; }
}

int main(int argc, char **argv){
    const char *out_path = NULL;
    int cols = 80, rows = 24, delay_ms = 0, settle_ms = 600, run_ms = 8000;
    int i, cmd_start = -1;
    for(i = 1; i < argc; i++){
        if(!strcmp(argv[i], "--out") && i+1 < argc)          out_path  = argv[++i];
        else if(!strcmp(argv[i], "--cols") && i+1 < argc)    cols      = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--rows") && i+1 < argc)    rows      = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--delay-ms") && i+1 < argc)delay_ms  = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--settle-ms") && i+1<argc) settle_ms = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--run-ms") && i+1 < argc)  run_ms    = atoi(argv[++i]);
        else if(!strcmp(argv[i], "--")){ cmd_start = i + 1; break; }
    }
    if(!out_path || cmd_start < 0 || cmd_start >= argc){
        fprintf(stderr, "usage: pty_drive --out FILE [opts] -- CMD [ARGS...] < input\n");
        return 2;
    }

    /* ---- open a pty pair ---- */
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    if(master < 0 || grantpt(master) || unlockpt(master)){ perror("openpt"); return 1; }
    char *sname = ptsname(master);
    int slave = sname ? open(sname, O_RDWR | O_NOCTTY) : -1;
    if(slave < 0){ perror("open slave"); return 1; }
    { struct winsize ws; ws.ws_col = (unsigned short)cols; ws.ws_row = (unsigned short)rows;
      ws.ws_xpixel = ws.ws_ypixel = 0; ioctl(master, TIOCSWINSZ, &ws); }

    FILE *outf = fopen(out_path, "wb");
    if(!outf){ perror("fopen out"); return 1; }

    pid_t pid = fork();
    if(pid < 0){ perror("fork"); return 1; }
    if(pid == 0){                       /* child: become the session on the pty */
        setsid();
        ioctl(slave, TIOCSCTTY, 0);
        dup2(slave, 0); dup2(slave, 1); dup2(slave, 2);
        if(slave > 2) close(slave);
        close(master);
        execvp(argv[cmd_start], &argv[cmd_start]);
        perror("execvp");
        _exit(127);
    }
    close(slave);

    /* ---- parent: pump stdin -> pty, pty -> outfile ---- */
    long start = now_ms(), input_done_at = -1;
    int stdin_open = 1, quit_sent = 0, app_ready = 0;
    for(;;){
        struct pollfd pfd[2];
        int nf = 0, mi, si = -1;
        mi = nf; pfd[nf].fd = master; pfd[nf].events = POLLIN; pfd[nf].revents = 0; nf++;
        /* Only feed input once the app has emitted output — by then it has set
         * raw mode (timui does so BEFORE its first write), so keystrokes are not
         * echoed by the pty's cooked line discipline nor flushed by the app's
         * TCSAFLUSH raw-mode switch. Sending earlier both garbles the capture
         * (echo) and loses the input (flush). */
        if(stdin_open && app_ready){ si = nf; pfd[nf].fd = 0; pfd[nf].events = POLLIN; pfd[nf].revents = 0; nf++; }
        if(poll(pfd, (nfds_t)nf, 50) < 0){ if(errno == EINTR) continue; break; }

        if(pfd[mi].revents & POLLIN){   /* app output -> capture */
            char buf[4096]; ssize_t r = read(master, buf, sizeof buf);
            if(r > 0){ fwrite(buf, 1, (size_t)r, outf); app_ready = 1; }
            else break;                 /* child closed the pty (exited) */
        }
        if(pfd[mi].revents & (POLLHUP | POLLERR)) break;

        if(si >= 0 && (pfd[si].revents & POLLIN)){   /* scripted input -> app */
            char buf[1024]; ssize_t r = read(0, buf, sizeof buf);
            if(r > 0){
                if(delay_ms > 0){
                    /* Pace input, but send each escape sequence (ESC [ ... final,
                     * or ESC O x) ATOMICALLY — real terminals burst them, and a
                     * per-byte delay longer than the app's ESC timeout would
                     * split "\x1b[A" into a bare ESC (often "quit"). */
                    ssize_t k = 0;
                    while(k < r){
                        ssize_t seq = 1;
                        if(buf[k] == 0x1b && k + 1 < r){
                            if(buf[k+1] == '['){
                                seq = 2;
                                while(k + seq < r && (unsigned char)buf[k+seq] >= 0x20
                                                  && (unsigned char)buf[k+seq] < 0x40) seq++;
                                if(k + seq < r) seq++;                 /* final byte 0x40..0x7e */
                            } else if(buf[k+1] == 'O'){
                                seq = (k + 2 < r) ? 3 : 2;             /* SS3 + one byte */
                            }
                        }
                        write_all(master, buf + k, (size_t)seq);
                        k += seq;
                        { struct timespec ts = { delay_ms/1000, (long)(delay_ms%1000)*1000000L };
                          nanosleep(&ts, NULL); }
                    }
                } else write_all(master, buf, (size_t)r);
            } else { stdin_open = 0; input_done_at = now_ms(); }   /* input EOF */
        }

        if(!quit_sent && input_done_at >= 0 && now_ms() - input_done_at > settle_ms){
            write_all(master, "\x1b[21~", 5);   /* F10 -> quit */
            write_all(master, "\x1b", 1);        /* ESC fallback */
            quit_sent = 1;
        }
        if(now_ms() - start > run_ms){ kill(pid, SIGTERM); break; }
    }

    fclose(outf);
    close(master);
    { int st; waitpid(pid, &st, 0); }
    return 0;
}
