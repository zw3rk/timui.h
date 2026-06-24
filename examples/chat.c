/*
 * chat.c — thread-safe message API demo: a live chat / log feed.
 *
 * A background pthread synthesizes "incoming" chat lines and hands them to the
 * UI thread through the ONLY thread-safe entry point, timui_post (an MPSC
 * queue). The main loop drains them with timui_recv into a bounded transcript
 * ring (the scrollback) and renders the tail (newest at the bottom); an input
 * field below composes a reply appended locally on Enter.
 *
 * Feature tour: timestamps per line; PgUp/PgDn + Up/Down scrollback; a tiny
 * markdown subset in messages (*bold*, _italic_, `code`) plus http(s):// URLs
 * rendered as OSC 8 hyperlinks; and wide glyphs / emoji (via timui_utf8_width).
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
#include <time.h>      /* nanosleep / time / localtime / strftime */
#include <string.h>    /* strlen / strncmp / memcpy */
#include <stdio.h>     /* snprintf */

/* Message type carried over the MPSC queue. */
enum { MSG_LINE = 1 };

/* ---- Transcript ring buffer (the scrollback) --------------------------- *
 * Fixed LOG_CAP-slot ring: this IS the scrollback limit. `count` is the total
 * ever appended; the live slot is `count & (LOG_CAP-1)` and the last N lines
 * are the tail. Bounded memory, O(1) append, no allocation. Each line carries
 * an HH:MM:SS timestamp captured at append time. */
#define LOG_CAP  1024                  /* scrollback depth (power of two) */
#define TS_LEN   9                     /* "HH:MM:SS" + NUL */
#define TS_COLS  9                     /* timestamp render width incl. one space */
typedef struct { char line[200]; char ts[TS_LEN]; } LogLine;
typedef struct { LogLine log[LOG_CAP]; int count; } Transcript;

/* Append one line (truncated to fit) with a timestamp; overwrites the oldest
 * slot once full. UI-thread only, so plain localtime() is fine. */
static void log_append(Transcript *t, const char *s){
    LogLine *slot = &t->log[t->count & (LOG_CAP - 1)];
    size_t i = 0;
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    if(lt) strftime(slot->ts, sizeof slot->ts, "%H:%M:%S", lt);
    else   slot->ts[0] = '\0';
    for(; s[i] != '\0' && i < sizeof slot->line - 1; i++) slot->line[i] = s[i];
    slot->line[i] = '\0';
    t->count++;
}

/* ---- Background worker -------------------------------------------------- *
 * `stop` is written by the UI thread and polled by the worker. C99 has no
 * <stdatomic.h>, so we use a documented `volatile int`: volatile forces the
 * worker to re-read it each iteration and cache coherency makes the store
 * visible promptly. We sleep in small slices and re-check, so a quit joins in
 * well under one tick rather than waiting a full ~700ms. */
typedef struct { Timui *ui; volatile int stop; } Worker;

static void *chat_worker(void *arg){
    Worker *w = (Worker *)arg;
    /* Canned senders rotated in order; every 4th line is a synthesized counter.
     * They double as a feature demo: emoji, *bold*, _italic_, `code`, a URL. */
    static const char *canned[] = {
        "alice: hi \xF0\x9F\x91\x8B",                                  /* 👋 */
        "bob: how's the *TUI*? run `make run-chat` \xE2\x80\x94 https://github.com",
        "carol: shipping _0.2.0_ \xF0\x9F\x9A\x80 ![screenshot](https://github.com)",  /* 🚀 + image */
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

/* ---- Rich text: a tiny markdown subset + URL autolink ------------------ *
 * Renders one message from `x`, clipped to `maxx`. Supports *bold*, _italic_,
 * `code`, and http(s):// links (drawn as OSC 8 hyperlinks). Markers toggle
 * their attribute (an unmatched marker simply runs to end of line). Draws one
 * glyph at a time so wide chars (CJK, emoji) advance by two cells. */
static void draw_rich(TimuiFrame *f, int x, int y, int maxx, const char *s,
                      uint32_t base_fg, uint32_t bg,
                      uint32_t code_fg, uint32_t link_fg){
    size_t i = 0, len = strlen(s);
    uint32_t attrs = 0;
    int code = 0;
    while(s[i] && x < maxx){
        /* markdown image ![alt](url): a "🖼 alt" badge, alt hyperlinked to the
         * url (clickable in terminals with OSC 8). True inline Kitty-graphics
         * rendering needs image-subsystem work (see timui_image_draw). */
        if(s[i] == '!' && s[i+1] == '['){
            size_t a = i + 2, ae = a, u, ue, k;
            while(s[ae] && s[ae] != ']') ae++;
            if(s[ae] == ']' && s[ae+1] == '('){
                u = ae + 2; ue = u;
                while(s[ue] && s[ue] != ')') ue++;
                if(s[ue] == ')'){
                    char uri[512];
                    size_t ul = ue - u;
                    TimuiStr alt;
                    if(ul > sizeof uri - 1) ul = sizeof uri - 1;
                    memcpy(uri, s + u, ul); uri[ul] = '\0';
                    timui_label(f, x, y, TIMUI_STR_LIT("\xF0\x9F\x96\xBC "),   /* 🖼 + space */
                                timui_style_make(code_fg, bg, 0));
                    x += 3;                                    /* emoji width 2 + space */
                    alt.ptr = s + a; alt.len = ae - a;
                    timui_label_hyperlink(f, x, y, alt, uri,
                                          timui_style_make(link_fg, bg, TIMUI_ATTR_UNDERLINE));
                    for(k = a; k < ae;){                       /* advance x by alt's width */
                        uint32_t cp; int adv = timui_utf8_decode(s + k, ae - k, &cp);
                        if(adv <= 0) adv = 1;
                        x += timui_utf8_width(cp); k += (size_t)adv;
                    }
                    i = ue + 1;
                    continue;
                }
            }
        }
        if(strncmp(s + i, "http://", 7) == 0 || strncmp(s + i, "https://", 8) == 0){
            size_t j = i, ul;
            char uri[512];
            TimuiStr span;
            while(s[j] && s[j] != ' ' && s[j] != '\t') j++;   /* URL ends at whitespace */
            ul = j - i; if(ul > sizeof uri - 1) ul = sizeof uri - 1;
            memcpy(uri, s + i, ul); uri[ul] = '\0';
            span.ptr = s + i; span.len = j - i;
            timui_label_hyperlink(f, x, y, span, uri,
                                  timui_style_make(link_fg, bg, TIMUI_ATTR_UNDERLINE));
            x += (int)(j - i);                                 /* URLs are ASCII: width == len */
            i = j;
            continue;
        }
        if(s[i] == '*'){ attrs ^= TIMUI_ATTR_BOLD;   i++; continue; }
        if(s[i] == '_'){ attrs ^= TIMUI_ATTR_ITALIC; i++; continue; }
        if(s[i] == '`'){ code = !code;               i++; continue; }
        {
            uint32_t cp;
            int adv = timui_utf8_decode(s + i, len - i, &cp);
            int glyph_w;
            TimuiStr ch;
            if(adv <= 0) adv = 1;
            glyph_w = timui_utf8_width(cp);
            ch.ptr = s + i; ch.len = (size_t)adv;
            timui_label(f, x, y, ch,
                        timui_style_make(code ? code_fg : base_fg, bg,
                                         attrs | (code ? TIMUI_ATTR_DIM : 0u)));
            x += glyph_w > 0 ? glyph_w : 0;
            i += (size_t)adv;
        }
    }
}

/* Draw the transcript into `body`, newest at the bottom. `scroll` is the number
 * of lines the view is pinned ABOVE the newest (0 = following the tail). Each
 * line is a dim timestamp then the rich-rendered message, tinted by sender. */
static void draw_transcript(TimuiFrame *f, const Transcript *t, TimuiRect body,
                            int scroll, TimuiStyle panel, uint32_t self_fg,
                            uint32_t sys_fg, uint32_t text_fg,
                            uint32_t code_fg, uint32_t link_fg){
    TimuiCellBuffer *buf = timui_frame_buffer(f);
    int rows, last, shown, first, i;

    timui_draw_fill(buf, body, panel);
    if(body.h <= 0 || body.w <= 0) return;

    rows  = body.h;
    last  = t->count - 1 - scroll;               /* index of the bottom visible line */
    if(last < 0) return;
    shown = last + 1 < rows ? last + 1 : rows;    /* how many lines fit / exist */
    first = last - shown + 1;

    for(i = 0; i < shown; i++){
        int idx = (first + i) & (LOG_CAP - 1);
        const char *s  = t->log[idx].line;
        const char *ts = t->log[idx].ts;
        int y = body.y + body.h - shown + i;      /* bottom-align the shown block */
        uint32_t fg = text_fg;
        if(strncmp(s, "you:", 4) == 0)         fg = self_fg;
        else if(strncmp(s, "system:", 7) == 0) fg = sys_fg;
        timui_label(f, body.x + 1, y, timui_str_from_cstr(ts),
                    timui_style_make(sys_fg, panel.bg, TIMUI_ATTR_DIM));
        draw_rich(f, body.x + 1 + TS_COLS, y, body.x + body.w, s,
                  fg, panel.bg, code_fg, link_fg);
    }
}

int main(void){
    TimuiConfig cfg = {0};
    Timui *ui = NULL;
    Worker worker = {0};
    pthread_t th;
    int thread_started = 0;
    int scroll = 0;                    /* lines pinned above the newest (0 = follow) */

    /* UI-thread-owned model: the transcript and the compose buffer. The input
     * state persists across frames (cursor + horizontal scroll live here). */
    Transcript transcript = {0};
    char compose[200] = {0};
    TimuiInputState compose_state = { compose, sizeof compose, 0, 0 };

    /* Theme-derived styles (MODERN_DARK) so the hand-drawn feed matches the
     * themed widgets (input field, function bar). */
    TimuiTheme  theme   = timui_theme_builtin(TIMUI_THEME_MODERN_DARK);
    TimuiStyle  panel   = timui_theme_style(&theme, TIMUI_SLOT_PANEL);
    TimuiStyle  status  = timui_theme_style(&theme, TIMUI_SLOT_STATUS);
    uint32_t    self_fg = timui_theme_style(&theme, TIMUI_SLOT_SUCCESS).fg;
    uint32_t    sys_fg  = timui_theme_style(&theme, TIMUI_SLOT_TEXT_DIM).fg;
    uint32_t    text_fg = timui_theme_style(&theme, TIMUI_SLOT_TEXT).fg;
    uint32_t    code_fg = timui_theme_style(&theme, TIMUI_SLOT_WARNING).fg;
    uint32_t    link_fg = 0x6CB6FFu;   /* light blue for hyperlinks */

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
        char header_txt[80];
        uint32_t type = 0;
        size_t sz;
        int count_before, body_rows, page, maxscroll, cap;

        if(!timui_begin(ui, &f)) break;   /* break => still stop+join+close below */

        /* Drain EVERY queued post into the transcript (UI thread only). Reset
         * the buffer size before each recv; recv reports the real payload size,
         * and copied bytes = min(cap-1, real), so clamp before NUL-terminating. */
        count_before = transcript.count;
        sz = sizeof recv_buf - 1;
        while(timui_recv(ui, &type, recv_buf, &sz)){
            if(type == MSG_LINE){
                size_t n = sz < sizeof recv_buf - 1 ? sz : sizeof recv_buf - 1;
                recv_buf[n] = '\0';
                log_append(&transcript, recv_buf);
            }
            sz = sizeof recv_buf - 1;
        }
        /* Keep the view anchored while scrolled up: new lines push the offset so
         * the same history stays put (0 = following the tail, which follows). */
        if(scroll > 0) scroll += transcript.count - count_before;

        /* ESC or F10 quit. */
        if(timui_key_pressed(f, TIMUI_KEY_ESCAPE) || timui_key_pressed(f, TIMUI_KEY_F10))
            timui_quit(ui);

        /* Layout: header (top) · footer (bottom) · input (above footer) · the
         * rest is the scrolling transcript body. */
        root   = timui_root(f);
        header = timui_cut_top(&root, 1);
        footer = timui_cut_bottom(&root, 1);
        input  = timui_cut_bottom(&root, 1);
        body_rows = root.h;

        /* Scrollback navigation. Up/Down by a line, PgUp/PgDn by a page. The
         * single-line input ignores these keys, so they are ours. Clamp to the
         * range the ring can still show (older lines are overwritten). */
        page = body_rows > 1 ? body_rows - 1 : 1;
        if(timui_key_pressed(f, TIMUI_KEY_UP))        scroll += 1;
        if(timui_key_pressed(f, TIMUI_KEY_DOWN))      scroll -= 1;
        if(timui_key_pressed(f, TIMUI_KEY_PAGE_UP))   scroll += page;
        if(timui_key_pressed(f, TIMUI_KEY_PAGE_DOWN)) scroll -= page;
        maxscroll = transcript.count - body_rows;
        cap = LOG_CAP - body_rows;
        if(maxscroll > cap) maxscroll = cap;
        if(maxscroll < 0)   maxscroll = 0;
        if(scroll > maxscroll) scroll = maxscroll;
        if(scroll < 0)         scroll = 0;

        /* Header status bar: message counter, plus scroll state when pinned. */
        timui_draw_fill(timui_frame_buffer(f), header, status);
        if(scroll > 0)
            snprintf(header_txt, sizeof header_txt,
                     " timui.h chat — %d msg   \xE2\x86\x91%d scrolled (PgDn: latest) ",
                     transcript.count, scroll);
        else
            snprintf(header_txt, sizeof header_txt,
                     " timui.h chat — %d message(s) ", transcript.count);
        timui_label(f, header.x, header.y, timui_str_from_cstr(header_txt), status);

        /* Transcript body. */
        draw_transcript(f, &transcript, root, scroll, panel,
                        self_fg, sys_fg, text_fg, code_fg, link_fg);

        /* Compose line: a "> " prompt then the editable input field. Focus it by
         * default (unless the user clicked elsewhere) so you can type from the
         * first frame without pressing Tab. On Enter, append and jump to newest. */
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
            scroll = 0;                    /* snap back to the newest line */
        }

        /* Footer hint bar. */
        timui_function_bar(f, footer,
            TIMUI_STR_LIT(" F10/ESC Quit   Enter Send   PgUp/PgDn Scroll "));

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
