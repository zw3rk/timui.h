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
#include <stdio.h>     /* snprintf / fopen / fread */
#include <stdlib.h>    /* malloc / free */
#include <unistd.h>    /* fork / execlp / _exit — open a clicked link */
#include <sys/wait.h>  /* waitpid */

/* An inline image message reserves IMG_ROWS rows (a caption line + the picture,
 * IMG_COLS wide). Kitty-graphics terminals draw the real PNG; others show the
 * "[img]" cell placeholder from timui_image_draw. */
#define IMG_ROWS 7
#define IMG_COLS 20

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
#define MSG_MAX  1024                  /* max chars in a composed / stored message */
typedef struct { char line[MSG_MAX]; char ts[TS_LEN]; } LogLine;
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
        "carol: shipping _0.2.0_ \xF0\x9F\x9A\x80 ![logo](examples/assets/logo.png)",   /* 🚀 + inline image */
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
                    timui_label(f, x, y, TIMUI_STR_LIT("\xF0\x9F\x93\xB7 "),   /* 📷 + space (emoji-width, unambiguous) */
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

/* ---- Inline images ----------------------------------------------------- *
 * A small path-keyed cache so each referenced PNG is read + handed to timui
 * once (a resource pool, freed in main on quit). A failed load is cached as
 * NULL so we don't re-open a bad path every frame. */
#define IMG_CACHE 8
static struct { char path[256]; TimuiImage *img; } g_imgcache[IMG_CACHE];
static int g_imgcache_n;

static TimuiImage *load_image(Timui *ui, const char *path){
    int k;
    FILE *fp;
    long sz;
    unsigned char *buf;
    TimuiImage *img = NULL;
    for(k = 0; k < g_imgcache_n; k++)
        if(strcmp(g_imgcache[k].path, path) == 0) return g_imgcache[k].img;
    if(g_imgcache_n >= IMG_CACHE) return NULL;
    fp = fopen(path, "rb");
    if(fp){
        fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
        buf = (sz > 0 && sz < 4 * 1024 * 1024) ? (unsigned char *)malloc((size_t)sz) : NULL;
        if(buf && fread(buf, 1, (size_t)sz, fp) == (size_t)sz)
            img = timui_image_from_png(ui, buf, (size_t)sz);
        free(buf); fclose(fp);
    }
    snprintf(g_imgcache[g_imgcache_n].path, sizeof g_imgcache[g_imgcache_n].path, "%s", path);
    g_imgcache[g_imgcache_n].img = img;
    g_imgcache_n++;
    return img;
}
static void free_images(Timui *ui){
    int k;
    for(k = 0; k < g_imgcache_n; k++) if(g_imgcache[k].img) timui_image_free(ui, g_imgcache[k].img);
    g_imgcache_n = 0;
}

/* True if `s` ends in ".png" (case-insensitive) — the only format Kitty
 * graphics transmits here (f=100). A Finder drag-drop inserts such a path. */
static int is_png_path(const char *s){
    size_t n = strlen(s);
    const char *e;
    if(n < 4) return 0;
    e = s + n - 4;
    return e[0] == '.' && (e[1]=='p'||e[1]=='P') && (e[2]=='n'||e[2]=='N') && (e[3]=='g'||e[3]=='G');
}

/* Extract the LOCAL (non-http) path from the first ![alt](path) in `s`. */
static int msg_image_path(const char *s, char *out, size_t cap){
    const char *p = strstr(s, "![");
    const char *e;
    size_t n;
    if(!p) return 0;
    p = strchr(p, '(');
    if(!p) return 0;
    p++;
    if(strncmp(p, "http://", 7) == 0 || strncmp(p, "https://", 8) == 0) return 0;   /* remote: badge only */
    e = strchr(p, ')');
    if(!e) return 0;
    n = (size_t)(e - p); if(n >= cap) n = cap - 1;
    memcpy(out, p, n); out[n] = '\0';
    return n > 0;
}
/* Row count for a message: IMG_ROWS only when the terminal can actually draw an
 * inline image (Kitty graphics) AND the local PNG loads. Otherwise 1 row — the
 * message keeps its clickable badge (no ugly grey placeholder box). Kitty
 * graphics is stripped under tmux/screen, so there the badge is used. */
static int msg_rows(Timui *ui, const char *s){
    char path[256];
    if(timui_caps_has(timui_caps(ui), TIMUI_CAP_KITTY_GRAPHICS) &&
       msg_image_path(s, path, sizeof path) && load_image(ui, path)) return IMG_ROWS;
    return 1;
}
/* Draw one message at row `y` spanning `h` rows: a dim timestamp + rich text on
 * the first row, and (for an image message) the picture below it. */
static void draw_message(TimuiFrame *f, const char *ts, const char *s, TimuiRect body,
                         int y, int h, TimuiStyle panel, uint32_t fg, uint32_t sys_fg,
                         uint32_t code_fg, uint32_t link_fg, Timui *ui){
    char path[256];
    timui_label(f, body.x + 1, y, timui_str_from_cstr(ts),
                timui_style_make(sys_fg, panel.bg, TIMUI_ATTR_DIM));
    draw_rich(f, body.x + 1 + TS_COLS, y, body.x + body.w, s, fg, panel.bg, code_fg, link_fg);
    if(h > 1 && msg_image_path(s, path, sizeof path)){
        TimuiImage *img = load_image(ui, path);
        if(img) timui_image_draw(f, img, TIMUI_RECT(body.x + 2, y + 1, IMG_COLS, h - 1));
    }
}

/* Draw the transcript into `body`, newest at the bottom. Messages have variable
 * height (image messages take IMG_ROWS); they are stacked upward from the bottom
 * until the body fills. `scroll` pins the view above the newest message. */
static void draw_transcript(TimuiFrame *f, const Transcript *t, TimuiRect body,
                            int scroll, TimuiStyle panel, uint32_t self_fg,
                            uint32_t sys_fg, uint32_t text_fg,
                            uint32_t code_fg, uint32_t link_fg){
    TimuiCellBuffer *buf = timui_frame_buffer(f);
    Timui *ui = f->ui;
    int last, used, first, idx, y;

    timui_draw_fill(buf, body, panel);
    if(body.h <= 0 || body.w <= 0) return;

    last = t->count - 1 - scroll;                 /* newest visible message index */
    if(last < 0) return;
    used = 0; first = last;
    for(idx = last; idx >= 0 && (last - idx) < LOG_CAP; idx--){   /* stack heights upward */
        int h = msg_rows(ui, t->log[idx & (LOG_CAP - 1)].line);
        if(used + h > body.h) break;
        used += h; first = idx;
    }
    y = body.y + body.h - used;                   /* bottom-align the stacked block */
    for(idx = first; idx <= last; idx++){
        int i2 = idx & (LOG_CAP - 1);
        const char *s = t->log[i2].line;
        int h = msg_rows(ui, s);
        uint32_t fg = text_fg;
        if(strncmp(s, "you:", 4) == 0)         fg = self_fg;
        else if(strncmp(s, "system:", 7) == 0) fg = sys_fg;
        draw_message(f, t->log[i2].ts, s, body, y, h, panel, fg, sys_fg, code_fg, link_fg, ui);
        y += h;
    }
}

/* Display width (columns) of a UTF-8 string — for centering the scroll hint. */
static int disp_w(const char *s){
    size_t i = 0, len = strlen(s);
    int w = 0;
    while(i < len){
        uint32_t cp; int a = timui_utf8_decode(s + i, len - i, &cp);
        if(a <= 0) a = 1;
        w += timui_utf8_width(cp); i += (size_t)a;
    }
    return w;
}

/* Open an http(s) URL in the default browser. Double-fork so the opener is
 * reparented (no zombie); exec directly (no shell -> no injection). */
static void open_url(const char *url){
    pid_t pid;
    if(!url) return;
    if(strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) return;
    pid = fork();
    if(pid == 0){
        if(fork() == 0){
            setsid();
            execlp("open", "open", url, (char *)NULL);         /* macOS */
            execlp("xdg-open", "xdg-open", url, (char *)NULL);  /* linux */
            _exit(127);
        }
        _exit(0);
    }
    if(pid > 0){ int st; waitpid(pid, &st, 0); }
}

int main(void){
    TimuiConfig cfg = {0};
    Timui *ui = NULL;
    Worker worker = {0};
    pthread_t th;
    int thread_started = 0;
    int scroll = 0;                    /* lines pinned above the newest (0 = follow) */

    /* UI-thread-owned model: the transcript (static — ~1 MB ring, off the stack)
     * and the compose buffer. The input state persists across frames. */
    static Transcript transcript;
    char compose[MSG_MAX] = {0};
    TimuiInputState compose_state = { compose, sizeof compose, 0, 0 };

    /* Sent-message history: Up/Down recall previous inputs (shell-style).
     * hist_pos == hist_count means "editing a fresh line". */
    static char history[64][MSG_MAX];
    int hist_count = 0, hist_pos = 0;

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
        TimuiRect root, header, hint, input, rule, prompt;
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

        /* Layout (top→bottom): header · transcript · ─── rule · ❯ input · hint. */
        root   = timui_root(f);
        header = timui_cut_top(&root, 1);
        hint   = timui_cut_bottom(&root, 1);
        input  = timui_cut_bottom(&root, 1);
        rule   = timui_cut_bottom(&root, 1);
        body_rows = root.h;

        /* Scroll: Shift+↑/↓ by line, PgUp/PgDn by page, mouse wheel; Ctrl+End (or
         * sending) jumps to the newest. Plain ↑/↓ recall history (below the input).
         * Clamp to what the ring can still show. */
        page = body_rows > 1 ? body_rows - 1 : 1;
        if(timui_key_pressed_mods(f, TIMUI_KEY_UP,   TIMUI_MOD_SHIFT)) scroll += 1;
        if(timui_key_pressed_mods(f, TIMUI_KEY_DOWN, TIMUI_MOD_SHIFT)) scroll -= 1;
        if(timui_key_pressed(f, TIMUI_KEY_PAGE_UP))   scroll += page;
        if(timui_key_pressed(f, TIMUI_KEY_PAGE_DOWN)) scroll -= page;
        scroll += timui_mouse_wheel(f);                /* wheel: up = older, down = newer */
        if(timui_key_pressed_mods(f, TIMUI_KEY_END, TIMUI_MOD_CTRL)) scroll = 0;
        maxscroll = transcript.count - body_rows;
        cap = LOG_CAP - body_rows;
        if(maxscroll > cap) maxscroll = cap;
        if(maxscroll < 0)   maxscroll = 0;
        if(scroll > maxscroll) scroll = maxscroll;
        if(scroll < 0)         scroll = 0;

        /* Header. */
        timui_draw_fill(timui_frame_buffer(f), header, status);
        snprintf(header_txt, sizeof header_txt, " timui.h chat — %d message(s) ", transcript.count);
        timui_label(f, header.x, header.y, timui_str_from_cstr(header_txt), status);

        /* Transcript. */
        draw_transcript(f, &transcript, root, scroll, panel,
                        self_fg, sys_fg, text_fg, code_fg, link_fg);

        /* Click a link to open it — with mouse reporting on, the terminal sends
         * us the click instead of opening the OSC 8 link itself. */
        { int mx, my;
          if(timui_mouse_clicked(f, &mx, &my)) open_url(timui_hyperlink_at(f, mx, my)); }

        /* A ─── rule frames the composer (claude-code style); while scrolled up
         * it becomes a centered "jump to bottom" affordance. */
        {
            TimuiCellBuffer *buf = timui_frame_buffer(f);
            timui_draw_hline(buf, rule.x, rule.y, rule.w, timui_style_make(sys_fg, panel.bg, 0));
            if(scroll > 0){
                char jb[80];
                int jx;
                snprintf(jb, sizeof jb,
                         "  \xE2\x86\x93 %d below \xE2\x80\x94 Ctrl+End / Enter for latest \xE2\x86\x93  ", scroll);
                jx = rule.x + (rule.w - disp_w(jb)) / 2;
                if(jx < rule.x) jx = rule.x;
                timui_label(f, jx, rule.y, timui_str_from_cstr(jb),
                            timui_style_make(code_fg, panel.bg, TIMUI_ATTR_BOLD));
            }
        }

        /* ❯ prompt (a distinct accent) + the editable input field, focused by
         * default so you can type from the first frame. On Enter, append + snap. */
        if(timui_focus(f) == 0) timui_set_focus(f, TIMUI_ID("compose"));
        /* Plain ↑/↓ recall sent-message history into the composer (shell-style). */
        if(timui_key_pressed(f, TIMUI_KEY_UP) &&
           !timui_key_pressed_mods(f, TIMUI_KEY_UP, TIMUI_MOD_SHIFT) &&
           hist_count > 0 && hist_pos > 0){
            hist_pos--;
            snprintf(compose, sizeof compose, "%s", history[hist_pos]);
            compose_state.cursor = strlen(compose); compose_state.scroll_x = 0;
        }
        if(timui_key_pressed(f, TIMUI_KEY_DOWN) &&
           !timui_key_pressed_mods(f, TIMUI_KEY_DOWN, TIMUI_MOD_SHIFT) &&
           hist_pos < hist_count){
            hist_pos++;
            if(hist_pos == hist_count){ compose[0] = '\0'; compose_state.cursor = 0; }
            else { snprintf(compose, sizeof compose, "%s", history[hist_pos]);
                   compose_state.cursor = strlen(compose); }
            compose_state.scroll_x = 0;
        }
        prompt = timui_cut_left(&input, 2);
        timui_label(f, prompt.x, prompt.y, TIMUI_STR_LIT("\xE2\x9D\xAF "),   /* ❯ */
                    timui_style_make(link_fg, panel.bg, TIMUI_ATTR_BOLD));
        /* Styled so the composer blends into the panel (just the ❯ accent), not
         * a green input box that read as the same surface as the hint below. */
        if(timui_input_field_styled(f, TIMUI_ID("compose"), input, &compose_state,
                                    timui_style_make(text_fg, panel.bg, 0))){
            { size_t L = strlen(compose);   /* trim trailing ws a drag-drop may add */
              while(L > 0 && (compose[L-1] == ' ' || compose[L-1] == '\t')) compose[--L] = '\0'; }
            if(compose[0] != '\0'){
                char sent[MSG_MAX];
                if(hist_count < (int)(sizeof history / sizeof history[0]))
                    snprintf(history[hist_count++], MSG_MAX, "%s", compose);   /* record for ↑/↓ */
                if(is_png_path(compose)){
                    const char *base = strrchr(compose, '/');
                    base = base ? base + 1 : compose;
                    snprintf(sent, sizeof sent, "you: ![%s](%s)", base, compose);
                } else {
                    snprintf(sent, sizeof sent, "you: %s", compose);
                }
                log_append(&transcript, sent);
            }
            hist_pos = hist_count;         /* back to a fresh line */
            compose[0] = '\0';
            compose_state.cursor = 0;
            compose_state.scroll_x = 0;
            scroll = 0;
        }

        /* Dim hint line — deliberately NOT the green status bar, so the composer
         * above no longer reads as the same surface as the row below it. */
        {
            TimuiStyle dim = timui_style_make(sys_fg, panel.bg, 0);
            timui_draw_fill(timui_frame_buffer(f), hint, timui_style_make(text_fg, panel.bg, 0));
            timui_label(f, hint.x + 1, hint.y,
                TIMUI_STR_LIT("F10 Quit \xC2\xB7 \xE2\x86\x91\xE2\x86\x93 History \xC2\xB7 "
                              "Shift+\xE2\x86\x91\xE2\x86\x93 / Wheel Scroll \xC2\xB7 Ctrl+End Latest"),
                dim);
        }

        timui_end(f);
    }

    /* Shutdown ordering: stop + join the producer BEFORE destroying the queue. */
    if(thread_started){
        worker.stop = 1;
        pthread_join(th, NULL);
    }
    free_images(ui);
    timui_close(ui);
    return 0;
}
