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
#include <unistd.h>    /* fork / execvp / _exit — open links, fetch remote images */
#include <sys/wait.h>  /* waitpid */
#include <fcntl.h>     /* open (/dev/null for the fetch children) */

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
/* Emoji shortcodes (:name:) — expanded at render time so `:eyes:` shows as 👀.
 * Every emoji here is in the bundled Twemoji atlas so it renders in a recording
 * without --system-emoji too. */
static const struct { const char *code; const char *emoji; } SHORTCODES[] = {
    {":eyes:", "\xF0\x9F\x91\x80"},        {":wave:", "\xF0\x9F\x91\x8B"},
    {":thumbsup:", "\xF0\x9F\x91\x8D"},    {":thumbs-up:", "\xF0\x9F\x91\x8D"},
    {":+1:", "\xF0\x9F\x91\x8D"},          {":tada:", "\xF0\x9F\x8E\x89"},
    {":fire:", "\xF0\x9F\x94\xA5"},        {":rocket:", "\xF0\x9F\x9A\x80"},
    {":heart:", "\xE2\x9D\xA4"},           {":sparkles:", "\xE2\x9C\xA8"},
    {":bulb:", "\xF0\x9F\x92\xA1"},        {":clap:", "\xF0\x9F\x91\x8F"},
    {":brain:", "\xF0\x9F\xA7\xA0"},       {":zap:", "\xE2\x9A\xA1"},
    {":star:", "\xE2\xAD\x90"},            {":joy:", "\xF0\x9F\xA4\xA3"},
    {":raised_hands:", "\xF0\x9F\x99\x8C"},{":bow:", "\xF0\x9F\x99\x87"},
    {":party:", "\xF0\x9F\xA5\xB3"},       {":bug:", "\xF0\x9F\x90\x9B"},
    {":globe:", "\xF0\x9F\x8C\x8D"},       {":boom:", "\xF0\x9F\x92\xA5"},
};
/* If a :shortcode: starts at s, return its emoji + set *clen to the code length. */
static const char *shortcode_at(const char *s, size_t *clen){
    size_t k;
    if(s[0] != ':') return NULL;
    for(k = 0; k < sizeof SHORTCODES / sizeof SHORTCODES[0]; k++){
        size_t cl = strlen(SHORTCODES[k].code);
        if(strncmp(s, SHORTCODES[k].code, cl) == 0){ *clen = cl; return SHORTCODES[k].emoji; }
    }
    return NULL;
}

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
        { size_t cl; const char *em = shortcode_at(s + i, &cl);   /* :name: -> emoji */
          if(em){
              if(x < maxx) timui_label(f, x, y, timui_str_from_cstr(em),
                                       timui_style_make(base_fg, bg, attrs));
              x += 2; i += cl; continue;
          } }
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

/* Clickable image regions recorded while drawing the transcript this frame, so a
 * click can open that image in the fullscreen viewer. Reset each frame. */
static struct { TimuiRect rect; char path[256]; char alt[128]; } g_click_img[16];
static int g_click_n;

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
/* ---- remote image fetch (http/https/ipfs) ------------------------------- *
 * A small async cache: the first time a remote image URL is seen we spawn a
 * detached thread that curls it to a temp PNG (converting via sips if needed);
 * once done the message renders it inline like a local image. ipfs:// resolves
 * through the ipfs.io gateway. Kept tiny + demo-grade (bounded cache, no retry). */
typedef struct { char url[512]; char path[256]; int state; } Fetch;   /* 0=pending 1=done 2=failed */
static Fetch g_fetch[16];
static int g_fetch_n;
static pthread_mutex_t g_fetch_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct { char url[512]; char raw[256]; char png[256]; int idx; } FetchJob;
/* exec a command with stdout+stderr silenced (they share the app's pty, so any
 * child chatter — sips echoes the output path — would corrupt the display). */
static void run_quiet(char *const argv[]){
    int devnull = open("/dev/null", O_WRONLY);
    if(devnull >= 0){ dup2(devnull, 1); dup2(devnull, 2); close(devnull); }
    execvp(argv[0], argv);
    _exit(127);
}
static void *fetch_thread(void *arg){
    FetchJob *j = (FetchJob *)arg;
    int ok = 0, st;
    pid_t pid = fork();
    if(pid == 0){   /* curl -sL to the raw temp file (no shell → no injection) */
        char *av[] = { "curl", "-sL", "--max-time", "20", "--max-filesize", "8000000", "-o", j->raw, j->url, NULL };
        run_quiet(av);
    }
    if(pid > 0 && waitpid(pid, &st, 0) > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0){
        /* normalize to PNG (Kitty f=100 needs it); sips is macOS-native. */
        pid = fork();
        if(pid == 0){ char *av[] = { "sips", "-s", "format", "png", j->raw, "--out", j->png, NULL }; run_quiet(av); }
        if(pid > 0 && waitpid(pid, &st, 0) > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0){
            FILE *fp = fopen(j->png, "rb"); if(fp){ ok = 1; fclose(fp); }
        }
    }
    pthread_mutex_lock(&g_fetch_lock);
    g_fetch[j->idx].state = ok ? 1 : 2;
    pthread_mutex_unlock(&g_fetch_lock);
    free(j);
    return NULL;
}
/* Resolve a remote URL to a local PNG path: 1 + path if fetched, else 0 (enqueues). */
static int resolve_remote(const char *url, char *out, size_t cap){
    int i, done = 0;
    pthread_mutex_lock(&g_fetch_lock);
    for(i = 0; i < g_fetch_n; i++) if(strcmp(g_fetch[i].url, url) == 0){
        if(g_fetch[i].state == 1){ snprintf(out, cap, "%s", g_fetch[i].path); done = 1; }
        pthread_mutex_unlock(&g_fetch_lock);
        return done;
    }
    if(g_fetch_n < (int)(sizeof g_fetch / sizeof g_fetch[0])){
        FetchJob *j = (FetchJob *)malloc(sizeof *j);
        i = g_fetch_n++;
        snprintf(g_fetch[i].url, sizeof g_fetch[i].url, "%s", url);
        snprintf(g_fetch[i].path, sizeof g_fetch[i].path, "/tmp/timui-fetch-%d.png", i);
        g_fetch[i].state = 0;
        if(j){
            pthread_t t;
            snprintf(j->raw, sizeof j->raw, "/tmp/timui-fetch-%d.raw", i);
            snprintf(j->png, sizeof j->png, "%s", g_fetch[i].path);
            j->idx = i;
            if(strncmp(url, "ipfs://", 7) == 0) snprintf(j->url, sizeof j->url, "https://ipfs.io/ipfs/%s", url + 7);
            else snprintf(j->url, sizeof j->url, "%s", url);
            if(pthread_create(&t, NULL, fetch_thread, j) == 0) pthread_detach(t); else { free(j); g_fetch[i].state = 2; }
        }
    }
    pthread_mutex_unlock(&g_fetch_lock);
    return 0;
}
/* ---- link preview cards (OpenGraph) ------------------------------------- *
 * Post a plain URL and we fetch the page, scrape <meta og:image>/<og:title>,
 * fetch that image, and render it as a preview — like the cards on chat apps. */
typedef struct { char url[512]; char img[256]; char title[160]; int state; } Card;
static Card g_card[8];
static int g_card_n;
static pthread_mutex_t g_card_lock = PTHREAD_MUTEX_INITIALIZER;

/* Pull the content="" of the <meta> tag carrying `prop` from an HTML buffer. */
static void find_meta(const char *html, const char *prop, char *out, size_t cap){
    const char *p = html;
    out[0] = '\0';
    while((p = strstr(p, prop)) != NULL){
        const char *lt = p, *gt = strchr(p, '>'), *c;
        while(lt > html && *lt != '<') lt--;
        if(gt && (c = strstr(lt, "content=")) != NULL && c < gt){
            const char *q = strchr(c, '"'), *q2;
            if(q && q < gt && (q2 = strchr(q + 1, '"')) != NULL){
                size_t n = (size_t)(q2 - (q + 1));
                if(n && n < cap){ memcpy(out, q + 1, n); out[n] = '\0'; return; }
            }
        }
        p += strlen(prop);
    }
}
typedef struct { char url[512]; char html[256]; char img[256]; char title[160]; int idx; } CardJob;
static void *card_thread(void *arg){
    CardJob *j = (CardJob *)arg;
    int st, ok = 0; pid_t pid; char imgurl[512] = {0};
    pid = fork();
    if(pid == 0){ char *av[] = { "curl", "-sL", "--max-time", "20", "-o", j->html, j->url, NULL }; run_quiet(av); }
    if(pid > 0 && waitpid(pid, &st, 0) > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0){
        FILE *fp = fopen(j->html, "rb");
        if(fp){                                     /* scrape og:image + og:title from the head */
            char *html = (char *)malloc(262144); size_t got = html ? fread(html, 1, 262143, fp) : 0;
            fclose(fp);
            if(html){ html[got] = '\0';
                find_meta(html, "og:image", imgurl, sizeof imgurl);
                find_meta(html, "og:title", j->title, sizeof j->title);
                free(html);
            }
        }
        if(imgurl[0] && (strncmp(imgurl, "http", 4) == 0)){   /* fetch + normalize the preview image */
            char raw[256]; snprintf(raw, sizeof raw, "%s.raw", j->img);
            pid = fork();
            if(pid == 0){ char *av[] = { "curl","-sL","--max-time","20","--max-filesize","8000000","-o",raw,imgurl,NULL }; run_quiet(av); }
            if(pid > 0 && waitpid(pid, &st, 0) > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0){
                pid = fork();
                if(pid == 0){ char *av[] = { "sips","-s","format","png",raw,"--out",j->img,NULL }; run_quiet(av); }
                if(pid > 0 && waitpid(pid, &st, 0) > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0){
                    FILE *pf = fopen(j->img, "rb"); if(pf){ ok = 1; fclose(pf); }
                }
            }
        }
    }
    pthread_mutex_lock(&g_card_lock);
    snprintf(g_card[j->idx].title, sizeof g_card[0].title, "%s", j->title);
    g_card[j->idx].state = ok ? 1 : 2;
    pthread_mutex_unlock(&g_card_lock);
    free(j);
    return NULL;
}
/* Resolve a page URL to its preview {og:image path, og:title}: 1 if ready. */
static int resolve_card(const char *url, char *img_out, size_t icap, char *title_out, size_t tcap){
    int i, done = 0;
    pthread_mutex_lock(&g_card_lock);
    for(i = 0; i < g_card_n; i++) if(strcmp(g_card[i].url, url) == 0){
        if(g_card[i].state == 1){ snprintf(img_out, icap, "%s", g_card[i].img);
                                  snprintf(title_out, tcap, "%s", g_card[i].title); done = 1; }
        pthread_mutex_unlock(&g_card_lock);
        return done;
    }
    if(g_card_n < (int)(sizeof g_card / sizeof g_card[0])){
        CardJob *j = (CardJob *)malloc(sizeof *j);
        i = g_card_n++;
        snprintf(g_card[i].url, sizeof g_card[i].url, "%s", url);
        snprintf(g_card[i].img, sizeof g_card[i].img, "/tmp/timui-card-%d.png", i);
        g_card[i].title[0] = '\0'; g_card[i].state = 0;
        if(j){ pthread_t t;
            snprintf(j->url, sizeof j->url, "%s", url);
            snprintf(j->html, sizeof j->html, "/tmp/timui-card-%d.html", i);
            snprintf(j->img, sizeof j->img, "%s", g_card[i].img);
            j->title[0] = '\0'; j->idx = i;
            if(pthread_create(&t, NULL, card_thread, j) == 0) pthread_detach(t); else { free(j); g_card[i].state = 2; }
        }
    }
    pthread_mutex_unlock(&g_card_lock);
    return 0;
}
/* First plain http(s) URL in a message (not an ![](...) image), else NULL-out. */
static int msg_link(const char *s, char *out, size_t cap){
    const char *u = s;
    while((u = strstr(u, "http")) != NULL){
        if((strncmp(u, "http://", 7) == 0 || strncmp(u, "https://", 8) == 0) &&
           !(u >= s + 2 && u[-1] == '(' && u[-2] == ']')){   /* skip ![alt](http…) */
            size_t n = 0;
            while(u[n] && u[n] != ' ' && u[n] != '\t' && u[n] != ')' && n < cap - 1){ out[n] = u[n]; n++; }
            out[n] = '\0';
            return n > 0;
        }
        u += 4;
    }
    return 0;
}
static int msg_image_path(const char *s, char *out, size_t cap){
    const char *p = strstr(s, "![");
    const char *e;
    size_t n;
    char url[512];
    if(p && (p = strchr(p, '(')) != NULL && (e = strchr(++p, ')')) != NULL){
        n = (size_t)(e - p); if(n >= sizeof url) n = sizeof url - 1;
        memcpy(url, p, n); url[n] = '\0';
        if(strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0 || strncmp(url, "ipfs://", 7) == 0)
            return resolve_remote(url, out, cap);   /* fetched temp PNG, or 0 while loading */
        snprintf(out, cap, "%s", url);              /* local path */
        return url[0] != '\0';
    }
    { char link[512], title[160];                   /* no image: a plain URL -> preview card */
      if(msg_link(s, link, sizeof link)) return resolve_card(link, out, cap, title, sizeof title);
    }
    return 0;
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
    int by0 = body.y, by1 = body.y + body.h - 1;
    /* Caption row (timestamp + rich text) — only when it lands in the pane, so a
     * partly-scrolled message doesn't draw into the header/composer. */
    if(y >= by0 && y <= by1){
        timui_label(f, body.x + 1, y, timui_str_from_cstr(ts),
                    timui_style_make(sys_fg, panel.bg, TIMUI_ATTR_DIM));
        draw_rich(f, body.x + 1 + TS_COLS, y, body.x + body.w, s, fg, panel.bg, code_fg, link_fg);
    }
    /* Inline image (rows y+1 .. y+h-1), CLIPPED to the pane so it slides off
     * smoothly instead of hiding oddly. */
    if(h > 1 && msg_image_path(s, path, sizeof path)){
        TimuiImage *img = load_image(ui, path);
        if(img){
            TimuiRect full = TIMUI_RECT(body.x + 2, y + 1, IMG_COLS, h - 1);
            int vtop = full.y > by0 ? full.y : by0;
            int vbot = (full.y + full.h - 1) < by1 ? (full.y + full.h - 1) : by1;
            if(vtop <= vbot){
                TimuiRect vis = TIMUI_RECT(full.x, vtop, IMG_COLS, vbot - vtop + 1);
                if(vis.y == full.y && vis.h == full.h) timui_image_draw(f, img, full);
                else timui_image_draw_clipped(f, img, full, vis);
            }
            /* record the clickable region (whole visible message) + its alt so a
             * click opens the fullscreen viewer. */
            { int rt = y > by0 ? y : by0, rb = (y + h - 1) < by1 ? (y + h - 1) : by1;
              if(rt <= rb && g_click_n < (int)(sizeof g_click_img / sizeof g_click_img[0])){
                  const char *ab = strstr(s, "!["), *ae;
                  g_click_img[g_click_n].rect = TIMUI_RECT(body.x, rt, body.w, rb - rt + 1);
                  snprintf(g_click_img[g_click_n].path, sizeof g_click_img[0].path, "%s", path);
                  g_click_img[g_click_n].alt[0] = '\0';
                  if(ab && (ae = strchr(ab + 2, ']')) != NULL){
                      size_t n = (size_t)(ae - (ab + 2));
                      if(n >= sizeof g_click_img[0].alt) n = sizeof g_click_img[0].alt - 1;
                      memcpy(g_click_img[g_click_n].alt, ab + 2, n);
                      g_click_img[g_click_n].alt[n] = '\0';
                  }
                  g_click_n++;
              }
            }
        }
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
    int idx, y_bottom;

    g_click_n = 0;                              /* rebuild clickable image regions */
    timui_draw_fill(buf, body, panel);
    if(body.h <= 0 || body.w <= 0 || t->count <= 0) return;

    /* Line-based scroll: stack messages newest→oldest upward from the pane
     * bottom. `scroll` is in LINES, pushing the newest line below the bottom;
     * each message draws only its rows that land in the pane (draw_message clips
     * images), so scrolling is smooth even across tall image messages. */
    y_bottom = body.y + body.h - 1 + scroll;      /* screen row of the newest content line */
    for(idx = t->count - 1; idx >= 0 && (t->count - 1 - idx) < LOG_CAP; idx--){
        int i2 = idx & (LOG_CAP - 1);
        const char *s = t->log[i2].line;
        int h = msg_rows(ui, s);
        int y_top = y_bottom - h + 1;
        if(y_bottom < body.y) break;              /* this + older are all above the pane */
        if(y_top < body.y + body.h){              /* some rows visible */
            uint32_t fg = text_fg;
            if(strncmp(s, "you:", 4) == 0)         fg = self_fg;
            else if(strncmp(s, "system:", 7) == 0) fg = sys_fg;
            draw_message(f, t->log[i2].ts, s, body, y_top, h, panel, fg, sys_fg, code_fg, link_fg, ui);
        }
        y_bottom -= h;
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

/* ---- Demo autoplay (for screen recordings) ------------------------------- *
 * `--demo <script>` self-drives the chat on a timeline so a recording is
 * hands-off and reproducible. The script is one action per line:
 *   wait <ms>     pause
 *   msg  <text>   a message arrives (e.g. "alice: hi")
 *   say  <text>   type <text> into the composer (animated) then send it
 *   img  <path>   send a local image message (![name](path))
 *   scroll <n>    scroll by n lines (+ older, - newer)
 *   open / close  open / close the fullscreen viewer for the last image
 *   quit          end the demo
 * Lines that are blank or start with '#' are ignored. */
enum { D_WAIT, D_MSG, D_SAY, D_IMG, D_SCROLL, D_OPEN, D_CLOSE, D_QUIT };
typedef struct { int kind; int arg; char text[MSG_MAX]; } DemoStep;

static long now_ms(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int demo_load(const char *path, DemoStep *out, int max){
    FILE *fp = fopen(path, "r");
    char line[MSG_MAX + 64];
    int n = 0;
    if(!fp) return -1;
    while(n < max && fgets(line, sizeof line, fp)){
        char *s = line, *rest;
        size_t L;
        while(*s == ' ' || *s == '\t') s++;
        L = strlen(s);
        while(L > 0 && (s[L-1] == '\n' || s[L-1] == '\r')) s[--L] = '\0';
        if(*s == '\0' || *s == '#') continue;
        rest = strchr(s, ' ');
        if(rest){ *rest++ = '\0'; while(*rest == ' ') rest++; } else rest = s + strlen(s);
        out[n].arg = 0; out[n].text[0] = '\0';
        if(strcmp(s, "wait")   == 0){ out[n].kind = D_WAIT;   out[n].arg = atoi(rest); }
        else if(strcmp(s, "msg")    == 0){ out[n].kind = D_MSG;   snprintf(out[n].text, MSG_MAX, "%s", rest); }
        else if(strcmp(s, "say")    == 0){ out[n].kind = D_SAY;   snprintf(out[n].text, MSG_MAX, "%s", rest); }
        else if(strcmp(s, "img")    == 0){ out[n].kind = D_IMG;   snprintf(out[n].text, MSG_MAX, "%s", rest); }
        else if(strcmp(s, "scroll") == 0){ out[n].kind = D_SCROLL; out[n].arg = atoi(rest); }
        else if(strcmp(s, "open")   == 0){ out[n].kind = D_OPEN; }
        else if(strcmp(s, "close")  == 0){ out[n].kind = D_CLOSE; }
        else if(strcmp(s, "quit")   == 0){ out[n].kind = D_QUIT; }
        else continue;   /* unknown action: skip */
        n++;
    }
    fclose(fp);
    return n;
}

/* Overlay markdown styling on the composer as you type: bold/italic/code on the
 * marked spans, KEEPING the markers (dim) so the display width — and thus the
 * input field's cursor — stay exact. Redraws over the field's plain text. */
static void draw_compose_styled(TimuiFrame *f, TimuiRect r, const char *s, int scroll_x,
                                uint32_t fg, uint32_t bg, uint32_t code_fg, uint32_t dim_fg){
    size_t i = 0, len = strlen(s);
    uint32_t attrs = 0;
    int code = 0, col = 0;
    while(s[i]){
        int x = r.x + col - scroll_x;
        if(s[i] == '*' || s[i] == '_' || s[i] == '`'){          /* marker: keep it, dim */
            if(x >= r.x && x < r.x + r.w){
                char m[2]; m[0] = s[i]; m[1] = '\0';
                timui_label(f, x, r.y, timui_str_from_cstr(m), timui_style_make(dim_fg, bg, 0));
            }
            if(s[i] == '*') attrs ^= TIMUI_ATTR_BOLD;
            else if(s[i] == '_') attrs ^= TIMUI_ATTR_ITALIC;
            else code = !code;
            col++; i++; continue;
        }
        { uint32_t cp; int adv = timui_utf8_decode(s + i, len - i, &cp);
          int w = timui_utf8_width(cp);
          if(adv <= 0) adv = 1;
          if(x >= r.x && x < r.x + r.w){
              TimuiStr ch; ch.ptr = s + i; ch.len = (size_t)adv;
              timui_label(f, x, r.y, ch, timui_style_make(code ? code_fg : fg, bg, attrs));
          }
          col += w; i += (size_t)adv; }
    }
}

/* Fullscreen image viewer: a dark backdrop, the image aspect-fit into most of the
 * screen (a cell is ~2× taller than wide), and the alt text centered below. */
static void draw_fullscreen(TimuiFrame *f, TimuiRect root, TimuiImage *img, const char *alt){
    TimuiCellBuffer *buf = timui_frame_buffer(f);
    timui_draw_fill(buf, root, timui_style_make(0xCCCCCC, 0x000000, 0));
    if(img && img->px_w > 0 && img->px_h > 0){
        int aw = root.w - 2, ah = root.h - 3;   /* side margin + 2 rows for the caption */
        int cols, rows;
        TimuiRect r;
        if(aw < 1) aw = 1;
        if(ah < 1) ah = 1;
        rows = ah;
        cols = (int)((long)ah * 2 * img->px_w / img->px_h);
        if(cols > aw){ cols = aw; rows = (int)((long)aw * img->px_h / (2 * img->px_w)); }
        if(cols < 1) cols = 1;
        if(rows < 1) rows = 1;
        r.x = root.x + (root.w - cols) / 2;
        r.y = root.y + 1 + (ah - rows) / 2;
        r.w = cols; r.h = rows;
        timui_image_draw(f, img, r);
    }
    if(alt && alt[0]){
        int w = disp_w(alt);
        timui_label(f, root.x + (root.w - w) / 2, root.y + root.h - 1,
                    timui_str_from_cstr(alt), timui_style_make(0xFFFFFF, 0x000000, TIMUI_ATTR_BOLD));
    }
    timui_label(f, root.x + 1, root.y, TIMUI_STR_LIT("Esc / click to close"),
                timui_style_make(0x888888, 0x000000, 0));
}

int main(int argc, char **argv){
    TimuiConfig cfg = {0};
    Timui *ui = NULL;
    Worker worker = {0};
    pthread_t th;
    int thread_started = 0;
    int scroll = 0;                    /* lines pinned above the newest (0 = follow) */

    /* Demo autoplay: `--demo <script>` self-drives the chat (no worker thread). */
    static DemoStep demo_steps[128];
    const char *demo_file = NULL;
    int demo_mode = 0, demo_n = 0, demo_i = 0, demo_typed = 0;
    long demo_at = 0;
    char demo_img_path[256] = {0}, demo_img_alt[128] = {0};
    { int a;
      for(a = 1; a < argc; a++)
          if(strcmp(argv[a], "--demo") == 0 && a + 1 < argc) demo_file = argv[++a]; }

    /* UI-thread-owned model: the transcript (static — ~1 MB ring, off the stack)
     * and the compose buffer. The input state persists across frames. */
    static Transcript transcript;
    char compose[MSG_MAX] = {0};
    TimuiInputState compose_state = { compose, sizeof compose, 0, 0 };

    /* Sent-message history: Up/Down recall previous inputs (shell-style).
     * hist_pos == hist_count means "editing a fresh line". */
    static char history[64][MSG_MAX];
    int hist_count = 0, hist_pos = 0;

    /* Fullscreen image viewer (opened by clicking an image message). */
    char fs_path[256] = {0}, fs_alt[128] = {0};
    int fs_active = 0;

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

    if(demo_file){
        demo_n = demo_load(demo_file, demo_steps, 128);
        if(demo_n > 0){ demo_mode = 1; demo_at = now_ms(); }
    }
    /* The worker posts async messages; in demo mode the script drives everything. */
    if(!demo_mode){
        worker.ui = ui;
        worker.stop = 0;
        if(pthread_create(&th, NULL, chat_worker, &worker) == 0) thread_started = 1;
    }

    while(!timui_should_quit(ui)){
        TimuiFrame *f = NULL;
        TimuiRect root, header, hint, input, rule, prompt;
        char recv_buf[200];
        char header_txt[80];
        uint32_t type = 0;
        size_t sz;
        int count_before, body_rows, page, maxscroll, fs_was;

        if(!timui_begin(ui, &f)) break;   /* break => still stop+join+close below */
        fs_was = fs_active;               /* modal state at frame start */

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

        /* Demo autoplay: advance the script on its timeline, driving the same
         * state a user would (transcript, composer, scroll, fullscreen). */
        if(demo_mode && demo_i < demo_n){
            long now = now_ms();
            if(now >= demo_at){
                DemoStep *st = &demo_steps[demo_i];
                switch(st->kind){
                    case D_WAIT:  demo_at = now + st->arg; demo_i++; break;
                    case D_MSG: {
                        const char *b, *e;
                        log_append(&transcript, st->text); scroll = 0;
                        /* if the message carries a local ![alt](path), remember it so a
                         * following `open` can show it fullscreen (e.g. the design post). */
                        if((b = strstr(st->text, "](")) && (e = strchr(b + 2, ')'))){
                            size_t n = (size_t)(e - (b + 2));
                            if(n && n < sizeof demo_img_path){
                                const char *base;
                                memcpy(demo_img_path, b + 2, n); demo_img_path[n] = '\0';
                                base = strrchr(demo_img_path, '/');
                                snprintf(demo_img_alt, sizeof demo_img_alt, "%s", base ? base + 1 : demo_img_path);
                            }
                        }
                        demo_at = now + 650; demo_i++;
                    } break;
                    case D_SAY: {
                        int len = (int)strlen(st->text);
                        if(demo_typed < len){                 /* reveal one char (animated typing) */
                            demo_typed++;
                            snprintf(compose, sizeof compose, "%.*s", demo_typed, st->text);
                            compose_state.cursor = strlen(compose);
                            demo_at = now + 45;
                        } else {                               /* done -> send */
                            char sent[MSG_MAX];
                            snprintf(sent, sizeof sent, "you: %s", st->text);
                            log_append(&transcript, sent);
                            compose[0] = '\0'; compose_state.cursor = 0; scroll = 0;
                            demo_typed = 0; demo_at = now + 650; demo_i++;
                        }
                    } break;
                    case D_IMG: {
                        const char *base = strrchr(st->text, '/'); char sent[MSG_MAX];
                        base = base ? base + 1 : st->text;
                        snprintf(sent, sizeof sent, "you: ![%s](%s)", base, st->text);
                        log_append(&transcript, sent); scroll = 0;
                        snprintf(demo_img_path, sizeof demo_img_path, "%s", st->text);
                        snprintf(demo_img_alt,  sizeof demo_img_alt,  "%s", base);
                        demo_at = now + 900; demo_i++;
                    } break;
                    case D_SCROLL: scroll += st->arg; if(scroll < 0) scroll = 0; demo_at = now + 450; demo_i++; break;
                    case D_OPEN:
                        if(demo_img_path[0]){
                            snprintf(fs_path, sizeof fs_path, "%s", demo_img_path);
                            snprintf(fs_alt,  sizeof fs_alt,  "%s", demo_img_alt);
                            fs_active = 1;
                        }
                        demo_at = now + 1400; demo_i++;
                        break;
                    case D_CLOSE: fs_active = 0; demo_at = now + 650; demo_i++; break;
                    case D_QUIT:  timui_quit(ui); demo_i++; break;
                }
            }
        }

        /* Keep the view anchored while scrolled up: any messages added this frame
         * (worker recv OR demo posts) push the offset so the same history stays
         * put — and the "↓ N below" counter climbs. */
        if(scroll > 0){
            int j, nl = 0;
            for(j = count_before; j < transcript.count; j++)
                nl += msg_rows(ui, transcript.log[j & (LOG_CAP - 1)].line);
            scroll += nl;
        }

        /* F10 quits from anywhere. The fullscreen image viewer is modal: while it
         * is up, draw it and dismiss on Esc / Enter / Space / click, then skip
         * the normal UI for this frame. */
        if(timui_key_pressed(f, TIMUI_KEY_F10)) timui_quit(ui);
        if(fs_active){
            draw_fullscreen(f, timui_root(f), load_image(ui, fs_path), fs_alt);
            if(fs_was && (timui_key_pressed(f, TIMUI_KEY_ESCAPE) ||
                          timui_key_pressed(f, TIMUI_KEY_ENTER)  ||
                          timui_char_pressed(f, ' ') ||
                          timui_mouse_clicked(f, NULL, NULL)))
                fs_active = 0;
            timui_end(f);
            continue;
        }
        if(timui_key_pressed(f, TIMUI_KEY_ESCAPE)) timui_quit(ui);

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
        /* clamp scroll (LINES) to the total content the ring can still show */
        { int j, total_lines = 0;
          int first = transcript.count > LOG_CAP ? transcript.count - LOG_CAP : 0;
          for(j = first; j < transcript.count; j++)
              total_lines += msg_rows(ui, transcript.log[j & (LOG_CAP - 1)].line);
          maxscroll = total_lines - body_rows;
        }
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

        /* A click on an image message opens the fullscreen viewer; otherwise, on
         * a link, open it (mouse reporting means the terminal hands us the click
         * rather than opening the OSC 8 link itself). */
        { int mx, my;
          if(timui_mouse_clicked(f, &mx, &my)){
              int hit = 0, k;
              for(k = 0; k < g_click_n; k++){
                  TimuiRect rr = g_click_img[k].rect;
                  if(mx >= rr.x && mx < rr.x + rr.w && my >= rr.y && my < rr.y + rr.h){
                      snprintf(fs_path, sizeof fs_path, "%s", g_click_img[k].path);
                      snprintf(fs_alt,  sizeof fs_alt,  "%s", g_click_img[k].alt);
                      fs_active = 1; hit = 1; break;
                  }
              }
              if(!hit) open_url(timui_hyperlink_at(f, mx, my));
          }
        }

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
        /* Live markdown styling over what you're typing (bold/italic/code). */
        draw_compose_styled(f, input, compose, compose_state.scroll_x,
                            text_fg, panel.bg, code_fg, sys_fg);

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
