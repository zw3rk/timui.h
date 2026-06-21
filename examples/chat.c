/*
 * chat.c — thread-safe message API demo: a live chat / log feed.
 *
 * A background pthread synthesizes "incoming" chat lines and hands them to the
 * UI thread through the ONLY thread-safe entry point, timui_post (an MPSC
 * queue). The main loop drains them with timui_recv into an in-memory
 * transcript ring buffer and renders the tail (newest at the bottom); an input
 * field below lets you compose a reply that is appended locally on Enter.
 *
 * Threading contract (see docs/THREADING.md): only timui_post may be called off
 * the UI thread. timui_begin/end/recv and every widget/drawing call are
 * UI-thread only. On quit we signal the worker, pthread_join it, and ONLY THEN
 * call timui_close — the worker must be stopped before the queue it posts to is
 * destroyed (W14 shutdown ordering).
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

#include <pthread.h>   /* pthread_create / pthread_join */
#include <time.h>      /* nanosleep */
#include <string.h>    /* strlen / strncmp */
#include <stdio.h>     /* snprintf */

/* Message type carried over the MPSC queue. */
enum { MSG_LINE = 1 };

/* ---- Transcript ring buffer -------------------------------------------- *
 * Fixed 256-slot ring holding the newest lines. `count` is the monotonically
 * increasing total appended; the live slot is `count & (LOG_CAP-1)` and the
 * last N lines are the tail. Bounded memory, O(1) append, no allocation. */
#define LOG_CAP 256
typedef struct { char line[200]; } LogLine;
typedef struct { LogLine log[LOG_CAP]; int count; } Transcript;

/* Append one line (truncated to fit); overwrites the oldest slot once full. */
static void log_append(Transcript *t, const char *s){
    LogLine *slot = &t->log[t->count & (LOG_CAP - 1)];
    size_t i = 0;
    for(; s[i] != '\0' && i < sizeof slot->line - 1; i++) slot->line[i] = s[i];
    slot->line[i] = '\0';
    t->count++;
}

/* ---- Background worker -------------------------------------------------- *
 * `stop` is written by the UI thread and polled by the worker. C99 has no
 * <stdatomic.h>, so we use a documented `volatile int`: volatile forces the
 * worker to re-read it from memory each iteration and cache coherency makes the
 * store visible promptly. We sleep in small slices and re-check it, so a quit
 * joins in well under one tick rather than waiting a full ~700ms. */
typedef struct { Timui *ui; volatile int stop; } Worker;

static void *chat_worker(void *arg){
    Worker *w = (Worker *)arg;
    /* Canned senders rotated in order; every 4th line is a synthesized counter. */
    static const char *canned[] = {
        "alice: hi",
        "bob: how's the TUI?",
        "carol: shipping 0.2.0",
    };
    int turn = 0;
    unsigned counter = 1;

    while(!w->stop){
        char line[200];
        int which = turn % 4;
        int slice;
        if(which < 3) snprintf(line, sizeof line, "%s", canned[which]);
        else          snprintf(line, sizeof line, "system: heartbeat #%u", counter++);

        /* THE thread-safe call. A bounded queue may reject the post (returns
         * false) — dropping an "incoming" line is fine; never crash on it. */
        (void)timui_post(w->ui, MSG_LINE, line, strlen(line));
        turn++;

        /* ~700ms total, checked in 50ms slices for a responsive shutdown. */
        for(slice = 0; slice < 14 && !w->stop; slice++){
            struct timespec ts = { 0, 50 * 1000 * 1000 };
            nanosleep(&ts, NULL);
        }
    }
    return NULL;
}

/* ---- Rendering --------------------------------------------------------- *
 * Draw the transcript tail into `body`, newest at the bottom (auto-scroll to
 * tail). Self lines ("you:") and system lines are tinted differently, all on
 * the panel background so the feed reads as one surface. */
static void draw_transcript(TimuiFrame *f, const Transcript *t, TimuiRect body,
                            TimuiStyle panel, uint32_t self_fg,
                            uint32_t sys_fg, uint32_t text_fg){
    TimuiCellBuffer *buf = timui_frame_buffer(f);
    int rows, shown, start, i;

    timui_draw_fill(buf, body, panel);
    if(body.h <= 0 || body.w <= 0) return;

    rows  = body.h;
    shown = t->count < rows ? t->count : rows;   /* how many tail lines fit */
    start = t->count - shown;                    /* first tail line index   */

    for(i = 0; i < shown; i++){
        int idx = (start + i) & (LOG_CAP - 1);
        const char *s = t->log[idx].line;
        int y = body.y + body.h - shown + i;     /* bottom-align the tail   */
        uint32_t fg = text_fg;
        if(strncmp(s, "you:", 4) == 0)         fg = self_fg;
        else if(strncmp(s, "system:", 7) == 0) fg = sys_fg;
        timui_label(f, body.x + 1, y, timui_str_from_cstr(s),
                    timui_style_make(fg, panel.bg, 0));
    }
}

int main(void){
    TimuiConfig cfg = {0};
    Timui *ui = NULL;
    Worker worker = {0};
    pthread_t th;
    int thread_started = 0;

    /* UI-thread-owned model: the transcript and the compose buffer. The input
     * state persists across frames (cursor + horizontal scroll live here). */
    Transcript transcript = {0};
    char compose[200] = {0};
    TimuiInputState compose_state = { compose, sizeof compose, 0, 0 };

    /* Theme-derived styles (MODERN_DARK) so the hand-drawn feed matches the
     * themed widgets (input field, function bar). */
    TimuiTheme  theme  = timui_theme_builtin(TIMUI_THEME_MODERN_DARK);
    TimuiStyle  panel  = timui_theme_style(&theme, TIMUI_SLOT_PANEL);
    TimuiStyle  status = timui_theme_style(&theme, TIMUI_SLOT_STATUS);
    uint32_t    self_fg = timui_theme_style(&theme, TIMUI_SLOT_SUCCESS).fg;
    uint32_t    sys_fg  = timui_theme_style(&theme, TIMUI_SLOT_TEXT_DIM).fg;
    uint32_t    text_fg = timui_theme_style(&theme, TIMUI_SLOT_TEXT).fg;

    cfg.title     = "timui.h chat";
    cfg.input_fd  = 0;
    cfg.output_fd = 1;
    cfg.profile   = TIMUI_PROFILE_AUTO;
    cfg.flags     = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_MOUSE | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme     = TIMUI_THEME_MODERN_DARK;

    /* If open fails (e.g. non-tty), return WITHOUT starting the worker so there
     * is nothing to join and the process exits immediately. */
    if(timui_open(&cfg, &ui) != TIMUI_OK) return 1;

    worker.ui = ui;
    worker.stop = 0;
    if(pthread_create(&th, NULL, chat_worker, &worker) == 0) thread_started = 1;

    while(!timui_should_quit(ui)){
        TimuiFrame *f = NULL;
        TimuiRect root, header, footer, input, prompt;
        char recv_buf[200];
        char header_txt[64];
        uint32_t type = 0;
        size_t sz;

        if(!timui_begin(ui, &f)) break;   /* break => still stop+join+close below */

        /* Drain EVERY queued post into the transcript (UI thread only). Reset
         * the buffer size before each recv; recv reports the real payload size,
         * and copied bytes = min(cap-1, real), so clamp before NUL-terminating. */
        sz = sizeof recv_buf - 1;
        while(timui_recv(ui, &type, recv_buf, &sz)){
            if(type == MSG_LINE){
                size_t n = sz < sizeof recv_buf - 1 ? sz : sizeof recv_buf - 1;
                recv_buf[n] = '\0';
                log_append(&transcript, recv_buf);
            }
            sz = sizeof recv_buf - 1;
        }

        /* ESC or F10 quit. */
        if(timui_key_pressed(f, TIMUI_KEY_ESCAPE) || timui_key_pressed(f, TIMUI_KEY_F10))
            timui_quit(ui);

        /* Layout: header (top) · footer (bottom) · input (above footer) · the
         * rest is the scrolling transcript body. */
        root   = timui_root(f);
        header = timui_cut_top(&root, 1);
        footer = timui_cut_bottom(&root, 1);
        input  = timui_cut_bottom(&root, 1);

        /* Header status bar with a live message counter. */
        timui_draw_fill(timui_frame_buffer(f), header, status);
        snprintf(header_txt, sizeof header_txt,
                 " timui.h chat — %d message(s) ", transcript.count);
        timui_label(f, header.x, header.y, timui_str_from_cstr(header_txt), status);

        /* Transcript body (auto-scrolled to the newest line). */
        draw_transcript(f, &transcript, root, panel, self_fg, sys_fg, text_fg);

        /* Compose line: a "> " prompt then the editable input field. Focus it by
         * default (unless the user clicked elsewhere) so you can type from the
         * first frame without pressing Tab. On Enter, append and clear. */
        if(timui_focus(f) == 0) timui_set_focus(f, TIMUI_ID("compose"));
        prompt = timui_cut_left(&input, 2);
        timui_label(f, prompt.x, prompt.y, TIMUI_STR_LIT("> "),
                    timui_style_make(text_fg, panel.bg, 0));
        if(timui_input_field(f, TIMUI_ID("compose"), input, &compose_state)){
            if(compose[0] != '\0'){
                char sent[200];
                snprintf(sent, sizeof sent, "you: %s", compose);
                log_append(&transcript, sent);
            }
            compose[0] = '\0';
            compose_state.cursor = 0;
            compose_state.scroll_x = 0;
        }

        /* Footer hint bar. */
        timui_function_bar(f, footer, TIMUI_STR_LIT(" F10/ESC Quit   Enter Send "));

        timui_end(f);
    }

    /* Shutdown ordering: stop + join the producer BEFORE destroying the queue. */
    if(thread_started){
        worker.stop = 1;
        pthread_join(th, NULL);
    }
    timui_close(ui);
    return 0;
}
