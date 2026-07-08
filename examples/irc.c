/*
 * irc.c — a minimal RFC 1459 / 2812 IRC client over timui.h.
 *
 * The natural next step after chat.c: a real wire protocol over a plaintext TCP
 * socket, multiple buffers (server + channels + queries) shown as tabs, a
 * per-buffer scrollback ring with timestamps + rich text, the active channel's
 * nick list, and a slash-command composer.
 *
 * Architecture (see docs/THREADING.md):
 *   - The PURE parser lives in examples/irc_proto.h (allocation-free, unit-tested
 *     by tests/test_irc.c via `make check-irc`).
 *   - A background WORKER thread owns the socket: it connects, reads bytes,
 *     splits on CRLF, auto-replies PING->PONG, and hands each RAW line to the UI
 *     thread through the ONLY thread-safe entry point, timui_post. The UI never
 *     touches the socket; the worker never touches the frame.
 *   - The MESSAGE HANDLER (irc_feed) is SEPARABLE from the socket: it takes a raw
 *     line, parses it, and mutates the client model (scrollback, nick list,
 *     topic, active buffer). Both the socket path (via timui_recv) and the
 *     offline --demo/--replay path drive the SAME handler, so the headless smoke
 *     exercises the real code with canned lines and no network.
 *   - Shutdown ordering (W14): signal + pthread_join the worker BEFORE
 *     timui_close, so the queue the worker posts to is never freed under it.
 *
 * NO vendored dependencies, NO crypto — plaintext IRC only (host:6667).
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"
#include "irc_proto.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* ---- model ------------------------------------------------------------- */

#define IRC_MSG_MAX      512     /* max chars stored per scrollback line       */
#define IRC_SCROLL_CAP   512     /* per-buffer scrollback ring (power of two)  */
#define IRC_MAX_BUFFERS  16      /* server + channels + queries                */
#define IRC_MAX_NICKS    256     /* nicks tracked per channel                  */
#define IRC_NICK_LEN     32
#define TS_LEN           9       /* "HH:MM:SS" + NUL                            */

/* Line-kind selects a colour + tiny bit of chrome in the renderer. */
enum { LK_MSG = 0, LK_SELF, LK_SYSTEM, LK_ACTION, LK_NOTICE, LK_ERROR };

typedef struct { char text[IRC_MSG_MAX]; char ts[TS_LEN]; int kind; } IrcLine;

typedef struct {
    char    name[64];                       /* "server" / "#chan" / "nick"     */
    int     is_channel;
    char    topic[256];
    IrcLine log[IRC_SCROLL_CAP];
    int     count;                          /* total ever appended (ring)      */
    int     scroll;                         /* lines pinned above newest (0=follow) */
    char    nicks[IRC_MAX_NICKS][IRC_NICK_LEN];
    int     nnicks;
} IrcBuffer;

typedef struct {
    char      nick[IRC_NICK_LEN];           /* our current nick                */
    char      server[128];                  /* host we connected to (display)  */
    IrcBuffer bufs[IRC_MAX_BUFFERS];
    int       nbufs;
    int       active;                       /* active buffer index             */
} IrcClient;

/* ---- timestamps + buffer helpers --------------------------------------- */

/* Capture the local wall-clock as "HH:MM:SS" (UI thread only -> localtime ok). */
static void ts_now(char out[TS_LEN]){
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    if(lt) strftime(out, TS_LEN, "%H:%M:%S", lt);
    else   out[0] = '\0';
}

/* Find a buffer by (case-insensitive) name, or -1. */
static int buf_find(IrcClient *cl, const char *name){
    int i;
    for(i = 0; i < cl->nbufs; i++)
        if(irc_ieq_(cl->bufs[i].name, name)) return i;
    return -1;
}

/* Get (creating if needed) a buffer by name; returns its index. The server
 * buffer is index 0, seeded by irc_client_init. Over the cap -> route to 0. */
static int buf_get(IrcClient *cl, const char *name, int is_channel){
    int i = buf_find(cl, name);
    if(i >= 0) return i;
    if(cl->nbufs >= IRC_MAX_BUFFERS) return 0;
    i = cl->nbufs++;
    memset(&cl->bufs[i], 0, sizeof cl->bufs[i]);
    snprintf(cl->bufs[i].name, sizeof cl->bufs[i].name, "%s", name);
    cl->bufs[i].is_channel = is_channel;
    return i;
}

/* Append a formatted line (timestamped) to a buffer's scrollback ring. While the
 * reader is scrolled up, keep the view anchored (grow the offset by one line). */
static void buf_logf(IrcBuffer *b, int kind, const char *fmt, ...){
    IrcLine *slot = &b->log[b->count & (IRC_SCROLL_CAP - 1)];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(slot->text, sizeof slot->text, fmt, ap);
    va_end(ap);
    ts_now(slot->ts);
    slot->kind = kind;
    b->count++;
    if(b->scroll > 0) b->scroll++;          /* stay put as history grows */
}

/* ---- nick list (per channel) ------------------------------------------- */

/* Strip a leading run of membership sigils (@ op, + voice, ~ & % founder/…). */
static const char *nick_strip(const char *n){
    while(*n == '@' || *n == '+' || *n == '~' || *n == '&' || *n == '%') n++;
    return n;
}
static int nick_index(IrcBuffer *b, const char *nick){
    int i;
    for(i = 0; i < b->nnicks; i++)
        if(irc_ieq_(b->nicks[i], nick)) return i;
    return -1;
}
static void nick_add(IrcBuffer *b, const char *nick){
    nick = nick_strip(nick);
    if(!*nick || nick_index(b, nick) >= 0 || b->nnicks >= IRC_MAX_NICKS) return;
    snprintf(b->nicks[b->nnicks++], IRC_NICK_LEN, "%s", nick);
}
static void nick_del(IrcBuffer *b, const char *nick){
    int i = nick_index(b, nick_strip(nick));
    if(i < 0) return;
    b->nicks[i][0] = '\0';                              /* shift the tail down */
    for(; i < b->nnicks - 1; i++) memcpy(b->nicks[i], b->nicks[i + 1], IRC_NICK_LEN);
    b->nnicks--;
}
static void nick_rename(IrcBuffer *b, const char *from, const char *to){
    int i = nick_index(b, nick_strip(from));
    if(i < 0) return;
    snprintf(b->nicks[i], IRC_NICK_LEN, "%s", nick_strip(to));
}

/* ---- the message handler (separable from the socket) ------------------- *
 * Feed ONE raw server line into the model. Pure w.r.t. I/O: it only reads the
 * line and mutates *cl. This is what both the socket path and the offline
 * demo/replay path call, so the smoke drives the real handler. */
static void irc_feed(IrcClient *cl, const char *rawline){
    IrcMessage m;
    IrcCmd cmd;
    int self;
    if(!irc_parse(rawline, strlen(rawline), &m)) return;
    cmd = irc_classify(&m);
    self = m.nick[0] && irc_ieq_(m.nick, cl->nick);     /* did WE originate it? */

    switch(cmd){
    case IRC_CMD_PING:                                   /* worker auto-replies; no UI noise */
        break;

    case IRC_CMD_PRIVMSG: case IRC_CMD_NOTICE: {
        const char *target = irc_param(&m, 0);
        const char *body   = irc_param(&m, 1);
        int notice = (cmd == IRC_CMD_NOTICE);
        int bi;
        /* a channel target routes to that channel; a message TO US routes to a
         * query buffer named after the sender. */
        if(irc_is_channel(target)) bi = buf_get(cl, target, 1);
        else if(m.nick[0])         bi = buf_get(cl, m.nick, 0);
        else                       bi = 0;               /* server notices */
        /* CTCP ACTION (/me) arrives as \x01ACTION text\x01 */
        if(!notice && body[0] == '\x01' && strncmp(body + 1, "ACTION ", 7) == 0){
            char act[IRC_MSG_MAX];
            size_t n = strlen(body + 8);
            if(n && body[8 + n - 1] == '\x01') n--;      /* drop trailing \x01 */
            snprintf(act, sizeof act, "%.*s", (int)n, body + 8);
            buf_logf(&cl->bufs[bi], LK_ACTION, "* %s %s", m.nick, act);
        } else {
            buf_logf(&cl->bufs[bi], notice ? LK_NOTICE : LK_MSG,
                     "<%s> %s", m.nick[0] ? m.nick : "*", body);
        }
        break;
    }

    case IRC_CMD_JOIN: {
        const char *chan = irc_param(&m, 0);             /* middle OR trailing */
        int bi = buf_get(cl, chan, 1);
        nick_add(&cl->bufs[bi], m.nick);
        buf_logf(&cl->bufs[bi], LK_SYSTEM, "-> %s joined %s", m.nick, chan);
        if(self) cl->active = bi;                        /* follow our own JOIN */
        break;
    }

    case IRC_CMD_PART: {
        const char *chan = irc_param(&m, 0);
        int bi = buf_find(cl, chan);
        if(bi >= 0){
            nick_del(&cl->bufs[bi], m.nick);
            buf_logf(&cl->bufs[bi], LK_SYSTEM, "<- %s left %s", m.nick, chan);
        }
        break;
    }

    case IRC_CMD_QUIT: {
        int i;                                            /* remove from every channel */
        for(i = 0; i < cl->nbufs; i++)
            if(cl->bufs[i].is_channel && nick_index(&cl->bufs[i], m.nick) >= 0){
                nick_del(&cl->bufs[i], m.nick);
                buf_logf(&cl->bufs[i], LK_SYSTEM, "<- %s quit (%s)", m.nick, irc_param(&m, 0));
            }
        break;
    }

    case IRC_CMD_NICK: {
        const char *to = irc_param(&m, 0);
        int i;
        for(i = 0; i < cl->nbufs; i++)
            if(cl->bufs[i].is_channel && nick_index(&cl->bufs[i], m.nick) >= 0){
                nick_rename(&cl->bufs[i], m.nick, to);
                buf_logf(&cl->bufs[i], LK_SYSTEM, "%s is now known as %s", m.nick, to);
            }
        if(self) snprintf(cl->nick, sizeof cl->nick, "%s", to);
        break;
    }

    case IRC_CMD_TOPIC: {
        const char *chan = irc_param(&m, 0);
        int bi = buf_get(cl, chan, 1);
        snprintf(cl->bufs[bi].topic, sizeof cl->bufs[bi].topic, "%s", irc_param(&m, 1));
        buf_logf(&cl->bufs[bi], LK_SYSTEM, "%s set topic: %s", m.nick, irc_param(&m, 1));
        break;
    }

    case IRC_CMD_NUMERIC:
        switch(m.numeric){
        case IRC_RPL_WELCOME:                            /* 001: registration done */
            snprintf(cl->nick, sizeof cl->nick, "%s", irc_param(&m, 0));
            buf_logf(&cl->bufs[0], LK_SYSTEM, "%s", irc_param(&m, 1));
            break;
        case IRC_RPL_TOPIC: {                            /* 332: channel topic */
            int bi = buf_get(cl, irc_param(&m, 1), 1);
            snprintf(cl->bufs[bi].topic, sizeof cl->bufs[bi].topic, "%s", irc_param(&m, 2));
            break;
        }
        case IRC_RPL_NAMREPLY: {                         /* 353: me = #chan :n1 n2 … */
            const char *chan = irc_param(&m, 2);         /* (param 1 is '='/'*'/'@') */
            const char *names = irc_param(&m, 3);
            int bi = buf_get(cl, chan, 1);
            char tok[IRC_NICK_LEN];
            int k = 0;
            const char *p = names;
            for(;; p++){                                 /* split the space list */
                if(*p == ' ' || *p == '\0'){
                    if(k){ tok[k] = '\0'; nick_add(&cl->bufs[bi], tok); k = 0; }
                    if(*p == '\0') break;
                } else if(k < IRC_NICK_LEN - 1) tok[k++] = *p;
            }
            break;
        }
        case IRC_RPL_ENDOFNAMES:                          /* 366: nothing to render */
            break;
        default:                                          /* other numerics -> server log */
            buf_logf(&cl->bufs[0], m.numeric >= 400 ? LK_ERROR : LK_SYSTEM,
                     "[%d] %s", m.numeric,
                     m.nparams > 1 ? irc_param(&m, m.nparams - 1) : m.command);
            break;
        }
        break;

    case IRC_CMD_ERROR:
        buf_logf(&cl->bufs[0], LK_ERROR, "ERROR %s", irc_param(&m, 0));
        break;

    default:                                              /* unhandled verb -> server log */
        buf_logf(&cl->bufs[0], LK_SYSTEM, "%s %s", m.command, irc_param(&m, 0));
        break;
    }
}

/* ---- network worker ---------------------------------------------------- *
 * The worker owns the socket. UI -> worker traffic goes through a mutex-guarded
 * outbound byte buffer (net_send appends, the worker drains + writes), so ALL
 * socket writes stay on the worker thread. Worker -> UI traffic is raw lines via
 * timui_post. `state`/`stop` are documented volatile ints (see chat.c). */
enum { NET_CONNECTING = 0, NET_REGISTERED, NET_DISCONNECTED };
enum { MSG_IRC_LINE = 1 };

typedef struct {
    Timui          *ui;
    volatile int    stop;
    volatile int    state;
    char            host[128];
    int             port;
    char            nick[IRC_NICK_LEN];
    char            channel[64];            /* auto-join after 001 ("" = none)  */
    pthread_mutex_t out_lock;
    char            out[8192];              /* pending outbound bytes           */
    size_t          out_len;
} IrcNet;

/* Thread-safe: enqueue one line (CRLF appended) for the worker to write. Called
 * from the UI thread; safe because the buffer is mutex-guarded. */
static void net_send(IrcNet *n, const char *fmt, ...){
    char line[IRC_MSG_MAX];
    va_list ap;
    int k;
    if(!n) return;
    va_start(ap, fmt);
    k = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if(k < 0) return;
    if(k > (int)sizeof line - 1) k = (int)sizeof line - 1;
    pthread_mutex_lock(&n->out_lock);
    if(n->out_len + (size_t)k + 2 <= sizeof n->out){
        memcpy(n->out + n->out_len, line, (size_t)k);
        n->out_len += (size_t)k;
        n->out[n->out_len++] = '\r';
        n->out[n->out_len++] = '\n';
    }
    pthread_mutex_unlock(&n->out_lock);
}

/* Write a whole buffer to fd (worker thread only); 0 on success. */
static int net_write_all(int fd, const char *buf, size_t len){
    size_t off = 0;
    while(off < len){
        ssize_t w = write(fd, buf + off, len - off);
        if(w < 0){ if(errno == EINTR) continue; return -1; }
        off += (size_t)w;
    }
    return 0;
}
/* Convenience: format + CRLF + write immediately (registration / PONG). */
static void net_write_line(int fd, const char *fmt, ...){
    char line[IRC_MSG_MAX + 2];
    va_list ap;
    int k;
    va_start(ap, fmt);
    k = vsnprintf(line, sizeof line - 2, fmt, ap);
    va_end(ap);
    if(k < 0) return;
    line[k++] = '\r'; line[k++] = '\n';
    (void)net_write_all(fd, line, (size_t)k);
}

/* Resolve host:port and connect a stream socket; -1 on failure. */
static int net_dial(const char *host, int port){
    struct addrinfo hints, *res = NULL, *rp;
    char portstr[16];
    int fd = -1;
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof portstr, "%d", port);
    if(getaddrinfo(host, portstr, &hints, &res) != 0) return -1;
    for(rp = res; rp; rp = rp->ai_next){
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if(fd < 0) continue;
        if(connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* Sleep `ms`, but wake early (in 100ms slices) if asked to stop. */
static void net_nap(IrcNet *n, int ms){
    int slept = 0;
    while(slept < ms && !n->stop){
        struct timespec ts = { 0, 100 * 1000 * 1000 };
        nanosleep(&ts, NULL);
        slept += 100;
    }
}

/* Post one raw line to the UI queue (drops silently if the queue is full). */
static void net_post_line(IrcNet *n, const char *line, size_t len){
    (void)timui_post(n->ui, MSG_IRC_LINE, line, len);
}

static void *irc_worker(void *arg){
    IrcNet *n = (IrcNet *)arg;
    int backoff = 1000;                                  /* reconnect backoff (ms) */

    while(!n->stop){
        int fd;
        char acc[IRC_MSG_MAX * 4];
        size_t acc_len = 0;

        n->state = NET_CONNECTING;
        fd = net_dial(n->host, n->port);
        if(fd < 0){                                       /* dial failed -> backoff */
            n->state = NET_DISCONNECTED;
            net_nap(n, backoff);
            if(backoff < 30000) backoff *= 2;
            continue;
        }
        backoff = 1000;                                   /* reset on a good connect */
        /* register: NICK + USER (RFC 2812 §3.1). */
        net_write_line(fd, "NICK %s", n->nick);
        net_write_line(fd, "USER %s 0 * :%s", n->nick, n->nick);

        while(!n->stop){
            struct pollfd pfd;
            int pr;
            /* flush any UI-queued outbound first */
            pthread_mutex_lock(&n->out_lock);
            if(n->out_len){ (void)net_write_all(fd, n->out, n->out_len); n->out_len = 0; }
            pthread_mutex_unlock(&n->out_lock);

            pfd.fd = fd; pfd.events = POLLIN; pfd.revents = 0;
            pr = poll(&pfd, 1, 100);
            if(pr < 0){ if(errno == EINTR) continue; break; }
            if(pr == 0) continue;                         /* timeout -> re-check stop/outbound */
            if(pfd.revents & (POLLERR | POLLHUP)) break;
            if(pfd.revents & POLLIN){
                char tmp[2048];
                ssize_t r = read(fd, tmp, sizeof tmp);
                ssize_t i;
                if(r <= 0) break;                          /* EOF / error -> reconnect */
                for(i = 0; i < r; i++){
                    char c = tmp[i];
                    if(c == '\n' || c == '\r'){
                        if(acc_len){                       /* a complete line */
                            IrcMessage m;
                            acc[acc_len] = '\0';
                            /* minimal worker-side protocol: PING->PONG, 001->JOIN */
                            if(irc_parse(acc, acc_len, &m)){
                                IrcCmd cc = irc_classify(&m);
                                if(cc == IRC_CMD_PING)
                                    net_write_line(fd, "PONG :%s", irc_param(&m, 0));
                                else if(cc == IRC_CMD_NUMERIC && m.numeric == IRC_RPL_WELCOME){
                                    n->state = NET_REGISTERED;
                                    if(n->channel[0]) net_write_line(fd, "JOIN %s", n->channel);
                                }
                            }
                            net_post_line(n, acc, acc_len);
                        }
                        acc_len = 0;
                    } else if(acc_len < sizeof acc - 1){
                        acc[acc_len++] = c;
                    } /* else: over-long line — drop the overflow, keep scanning */
                }
            }
        }
        close(fd);
        n->state = NET_DISCONNECTED;
        if(!n->stop) net_nap(n, backoff);                 /* brief pause before reconnect */
    }
    return NULL;
}

/* ---- composer: slash commands -> protocol + local echo ----------------- *
 * `net` may be NULL (offline demo): commands still manipulate local buffers so
 * the UI is demonstrable without a server. */
static void irc_submit(IrcClient *cl, IrcNet *net, const char *input){
    IrcBuffer *act = &cl->bufs[cl->active];
    if(!input[0]) return;

    if(input[0] == '/'){                                  /* a command */
        char verb[16];
        const char *rest = input + 1;
        int vl = 0;
        while(*rest && *rest != ' ' && vl < (int)sizeof verb - 1) verb[vl++] = *rest++;
        verb[vl] = '\0';
        while(*rest == ' ') rest++;                        /* skip to the argument */

        if(irc_ieq_(verb, "join") && irc_is_channel(rest)){
            char chan[64];
            snprintf(chan, sizeof chan, "%s", rest);
            cl->active = buf_get(cl, chan, 1);
            net_send(net, "JOIN %s", chan);
            if(!net) nick_add(&cl->bufs[cl->active], cl->nick);   /* offline echo */
        } else if(irc_ieq_(verb, "part")){
            const char *chan = *rest ? rest : act->name;
            int bi = buf_find(cl, chan);
            net_send(net, "PART %s", chan);
            if(bi > 0){                                     /* keep buffer 0 (server) */
                int j;
                for(j = bi; j < cl->nbufs - 1; j++) cl->bufs[j] = cl->bufs[j + 1];
                cl->nbufs--;
                if(cl->active >= cl->nbufs) cl->active = cl->nbufs - 1;
            }
        } else if(irc_ieq_(verb, "msg")){                   /* /msg nick text */
            char who[IRC_NICK_LEN];
            int wl = 0;
            while(*rest && *rest != ' ' && wl < (int)sizeof who - 1) who[wl++] = *rest++;
            who[wl] = '\0';
            while(*rest == ' ') rest++;
            if(who[0] && *rest){
                int bi = buf_get(cl, who, 0);
                net_send(net, "PRIVMSG %s :%s", who, rest);
                buf_logf(&cl->bufs[bi], LK_SELF, "<%s> %s", cl->nick, rest);
                cl->active = bi;
            }
        } else if(irc_ieq_(verb, "nick") && *rest){
            net_send(net, "NICK %s", rest);
            if(!net) snprintf(cl->nick, sizeof cl->nick, "%s", rest);
        } else if(irc_ieq_(verb, "me") && *rest){           /* CTCP ACTION */
            net_send(net, "PRIVMSG %s :\x01" "ACTION %s\x01", act->name, rest);
            buf_logf(act, LK_ACTION, "* %s %s", cl->nick, rest);
        } else if(irc_ieq_(verb, "quit")){
            net_send(net, "QUIT :%s", *rest ? rest : "timui.h irc");
        } else {
            buf_logf(&cl->bufs[cl->active], LK_ERROR, "unknown command: /%s", verb);
        }
        return;
    }

    /* plain text -> PRIVMSG to the active buffer + local echo (servers never
     * echo your own PRIVMSG back). Only meaningful in a channel/query. */
    if(cl->active == 0){
        buf_logf(act, LK_ERROR, "not in a channel — /join #chan first");
        return;
    }
    net_send(net, "PRIVMSG %s :%s", act->name, input);
    buf_logf(act, LK_SELF, "<%s> %s", cl->nick, input);
}

/* ---- rich text (a subset of chat.c's draw_rich, LTR only) -------------- *
 * *bold* _italic_ `code`, plus http(s):// autolinked as OSC 8. One glyph at a
 * time so wide runes (CJK/emoji) advance two cells. */
static void draw_rich(TimuiFrame *f, int x, int y, int maxx, const char *s,
                      uint32_t fg, uint32_t bg, uint32_t code_fg, uint32_t link_fg){
    size_t i = 0, len = strlen(s);
    uint32_t attrs = 0;
    int code = 0;
    while(s[i] && x < maxx){
        if(strncmp(s + i, "http://", 7) == 0 || strncmp(s + i, "https://", 8) == 0){
            size_t j = i, ul;
            char uri[512];
            TimuiStr span;
            while(s[j] && s[j] != ' ' && s[j] != '\t') j++;
            ul = j - i; if(ul > sizeof uri - 1) ul = sizeof uri - 1;
            memcpy(uri, s + i, ul); uri[ul] = '\0';
            span.ptr = s + i; span.len = j - i;
            timui_label_hyperlink(f, x, y, span, uri,
                                  timui_style_make(link_fg, bg, TIMUI_ATTR_UNDERLINE));
            x += (int)(j - i);
            i = j;
            continue;
        }
        if(s[i] == '*'){ attrs ^= TIMUI_ATTR_BOLD;   i++; continue; }
        if(s[i] == '_'){ attrs ^= TIMUI_ATTR_ITALIC; i++; continue; }
        if(s[i] == '`'){ code = !code;               i++; continue; }
        {
            uint32_t cp;
            int adv = timui_utf8_decode(s + i, len - i, &cp);
            int gw;
            TimuiStr ch;
            if(adv <= 0) adv = 1;
            gw = timui_utf8_width(cp);
            ch.ptr = s + i; ch.len = (size_t)adv;
            timui_label(f, x, y, ch, timui_style_make(code ? code_fg : fg, bg,
                        attrs | (code ? TIMUI_ATTR_DIM : 0u)));
            x += gw > 0 ? gw : 0;
            i += (size_t)adv;
        }
    }
}

/* Draw a buffer's scrollback into `body`, newest at the bottom, honouring the
 * per-buffer `scroll` (lines pinned above the newest). */
static void draw_scrollback(TimuiFrame *f, IrcBuffer *b, TimuiRect body,
                            TimuiStyle panel, uint32_t text_fg, uint32_t self_fg,
                            uint32_t sys_fg, uint32_t code_fg, uint32_t link_fg){
    int y = body.y + body.h - 1 + b->scroll;             /* screen row of newest line */
    int idx, lx = body.x + 1 + TS_LEN, maxx = body.x + body.w;
    TimuiStyle tss = timui_style_make(sys_fg, panel.bg, TIMUI_ATTR_DIM);
    timui_draw_fill(timui_frame_buffer(f), body, panel);
    for(idx = b->count - 1; idx >= 0 && (b->count - 1 - idx) < IRC_SCROLL_CAP; idx--){
        IrcLine *ln = &b->log[idx & (IRC_SCROLL_CAP - 1)];
        uint32_t fg = text_fg;
        if(y < body.y) break;                             /* above the pane */
        if(y <= body.y + body.h - 1){                     /* visible */
            switch(ln->kind){
                case LK_SELF:   fg = self_fg; break;
                case LK_SYSTEM: fg = sys_fg;  break;
                case LK_ACTION: fg = 0xC792EAu; break;    /* purple */
                case LK_NOTICE: fg = 0x82AAFFu; break;    /* blue   */
                case LK_ERROR:  fg = 0xF78C6Cu; break;    /* orange */
                default: break;
            }
            timui_label(f, body.x + 1, y, timui_str_from_cstr(ln->ts), tss);
            draw_rich(f, lx, y, maxx, ln->text, fg, panel.bg, code_fg, link_fg);
        }
        y--;
    }
}

/* Nick-list table cell accessor: one column of the active channel's nicks. */
static const char *nick_cell(void *ud, int row, int col){
    IrcBuffer *b = (IrcBuffer *)ud;
    (void)col;
    if(row < 0 || row >= b->nnicks) return "";
    return b->nicks[row];
}

/* ---- client bootstrap -------------------------------------------------- */
static void irc_client_init(IrcClient *cl, const char *nick, const char *server){
    memset(cl, 0, sizeof *cl);
    snprintf(cl->nick, sizeof cl->nick, "%s", nick);
    snprintf(cl->server, sizeof cl->server, "%s", server);
    cl->nbufs = 1;                                        /* buffer 0 = server console */
    snprintf(cl->bufs[0].name, sizeof cl->bufs[0].name, "server");
    cl->bufs[0].is_channel = 0;
    cl->active = 0;
}

/* Canned server transcript for --demo (no network). Exercises 001/JOIN/353/366/
 * 332 + PRIVMSG (with *bold* / `code` / a URL / emoji) so the smoke can assert
 * the channel tab, a message, and a nick all render. */
static const char *const DEMO_LINES[] = {
    ":irc.timui.net 001 me :Welcome to the timui IRC Network me",
    ":me!me@localhost JOIN #timui",
    ":irc.timui.net 353 me = #timui :me @alice bob carol",
    ":irc.timui.net 366 me #timui :End of /NAMES list.",
    ":irc.timui.net 332 me #timui :timui.h \xE2\x80\x94 a single-header C99 TUI",
    ":alice!a@host PRIVMSG #timui :hi everyone \xF0\x9F\x91\x8B",
    ":bob!b@host PRIVMSG #timui :morning \xE2\x80\x94 *bold* and `code` render",
    ":carol!c@host PRIVMSG #timui :ship it: https://timui.dev"
};

/* Feed a file of raw IRC lines (one per line) through the handler (--replay). */
static void irc_replay_file(IrcClient *cl, const char *path){
    FILE *fp = fopen(path, "r");
    char line[IRC_MSG_MAX];
    if(!fp) return;
    while(fgets(line, sizeof line, fp)){
        size_t L = strlen(line);
        while(L > 0 && (line[L-1] == '\n' || line[L-1] == '\r')) line[--L] = '\0';
        if(L) irc_feed(cl, line);
    }
    fclose(fp);
}

int main(int argc, char **argv){
    TimuiConfig cfg = {0};
    Timui *ui = NULL;
    /* The client model holds per-buffer scrollback rings (several MB); keep it
     * OFF the stack as a function-local static (as chat.c does for its ring)
     * rather than a huge automatic that would overflow the stack. Single-
     * threaded ownership: only the UI thread ever touches it. */
    static IrcClient client;
    IrcNet net = {0};
    pthread_t th;
    int worker_started = 0;

    /* modes: default offline demo; --connect goes live; --replay feeds a file. */
    const char *host = NULL, *nick = "timui", *channel = "#timui", *replay = NULL;
    int port = 6667, demo = 0, max_frames = 0, frames = 0, a;
    char compose[IRC_MSG_MAX] = {0};
    TimuiTextAreaState compose_state = { compose, sizeof compose, 0, 0 };
    static char history[64][IRC_MSG_MAX];      /* sent lines, for ↑/↓ recall (static: off-stack) */
    int hist_count = 0, hist_pos = 0;

    for(a = 1; a < argc; a++){
        if(!strcmp(argv[a], "--connect") && a + 1 < argc) host = argv[++a];
        else if(!strcmp(argv[a], "--port") && a + 1 < argc) port = atoi(argv[++a]);
        else if(!strcmp(argv[a], "--nick") && a + 1 < argc) nick = argv[++a];
        else if(!strcmp(argv[a], "--channel") && a + 1 < argc) channel = argv[++a];
        else if(!strcmp(argv[a], "--replay") && a + 1 < argc) replay = argv[++a];
        else if(!strcmp(argv[a], "--demo")) demo = 1;
        else if(!strcmp(argv[a], "--frames") && a + 1 < argc) max_frames = atoi(argv[++a]);
    }
    if(!host && !replay) demo = 1;                         /* nothing to connect to -> demo */

    irc_client_init(&client, nick, host ? host : "offline");

    cfg.title     = "timui.h irc";
    cfg.input_fd  = 0;
    cfg.output_fd = 1;
    cfg.profile   = TIMUI_PROFILE_AUTO;
    cfg.flags     = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_MOUSE | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme     = TIMUI_THEME_MODERN_DARK;
    if(timui_open(&cfg, &ui) != TIMUI_OK) return 1;

    /* Theme-derived styles (match the themed widgets). */
    { TimuiTheme theme = timui_theme_builtin(TIMUI_THEME_MODERN_DARK);
      TimuiStyle panel  = timui_theme_style(&theme, TIMUI_SLOT_PANEL);
      TimuiStyle status = timui_theme_style(&theme, TIMUI_SLOT_STATUS);
      uint32_t self_fg  = timui_theme_style(&theme, TIMUI_SLOT_SUCCESS).fg;
      uint32_t sys_fg   = timui_theme_style(&theme, TIMUI_SLOT_TEXT_DIM).fg;
      uint32_t text_fg  = timui_theme_style(&theme, TIMUI_SLOT_TEXT).fg;
      uint32_t code_fg  = timui_theme_style(&theme, TIMUI_SLOT_WARNING).fg;
      uint32_t link_fg  = 0x6CB6FFu;
      TimuiStyle border_st = timui_style_make(sys_fg, panel.bg, 0);
      TimuiStyle title_st  = timui_style_make(link_fg, panel.bg, TIMUI_ATTR_BOLD);

      /* Offline: play the canned transcript through the SAME handler the socket
       * uses. Live: replay a file first (if any), then start the worker. */
      if(demo){ size_t i; for(i = 0; i < sizeof DEMO_LINES / sizeof DEMO_LINES[0]; i++)
                    irc_feed(&client, DEMO_LINES[i]); }
      if(replay) irc_replay_file(&client, replay);

      if(host){
          net.ui = ui; net.stop = 0; net.state = NET_CONNECTING; net.port = port;
          snprintf(net.host, sizeof net.host, "%s", host);
          snprintf(net.nick, sizeof net.nick, "%s", nick);
          snprintf(net.channel, sizeof net.channel, "%s", channel);
          pthread_mutex_init(&net.out_lock, NULL);
          if(pthread_create(&th, NULL, irc_worker, &net) == 0) worker_started = 1;
          else pthread_mutex_destroy(&net.out_lock);
      }

      while(!timui_should_quit(ui)){
          TimuiFrame *f = NULL;
          TimuiRect root, rows[4], mid[2];
          const TimuiConstraint vmain[] = { TIMUI_LEN(1), TIMUI_LEN(1), TIMUI_FLEX(1), TIMUI_LEN(3) };
          const TimuiConstraint hmid[]  = { TIMUI_FLEX(1), TIMUI_LEN(18) };
          const char *labels[IRC_MAX_BUFFERS];
          char recv[IRC_MSG_MAX];
          uint32_t type = 0;
          size_t sz;
          int i;

          if(!timui_begin(ui, &f)) break;

          /* Drain queued raw lines from the worker into the model (UI thread). */
          sz = sizeof recv - 1;
          while(timui_recv(ui, &type, recv, &sz)){
              if(type == MSG_IRC_LINE){
                  size_t n = sz < sizeof recv - 1 ? sz : sizeof recv - 1;
                  recv[n] = '\0';
                  irc_feed(&client, recv);
              }
              sz = sizeof recv - 1;
          }

          if(timui_key_pressed(f, TIMUI_KEY_F10)) timui_quit(ui);

          root = timui_root(f);
          timui_split_v(root, vmain, 4, rows);            /* header · tabs · body · composer */

          /* header: nick @ server + active channel + connection state. */
          { char hdr[160];
            IrcBuffer *ab = &client.bufs[client.active];
            const char *st = worker_started ? (net.state == NET_REGISTERED ? "online" :
                                     net.state == NET_CONNECTING ? "connecting" : "offline")
                                  : (demo ? "demo" : "replay");
            timui_draw_fill(timui_frame_buffer(f), rows[0], status);
            snprintf(hdr, sizeof hdr, " %s@%s \xC2\xB7 %s \xC2\xB7 %s",
                     client.nick, client.server, ab->name, st);
            timui_label(f, rows[0].x, rows[0].y, timui_str_from_cstr(hdr), status);
          }

          /* tabs: one per buffer. */
          for(i = 0; i < client.nbufs; i++) labels[i] = client.bufs[i].name;
          (void)timui_tabs(f, TIMUI_ID("bufs"), rows[1], labels, client.nbufs, &client.active);

          /* body: scrollback (left) + nick list (right). */
          timui_split_h(rows[2], hmid, 2, mid);
          { IrcBuffer *ab = &client.bufs[client.active];
            char title[80];
            TimuiRect in;
            /* scrollback panel — title carries the topic for a channel. */
            snprintf(title, sizeof title, " %s%s%s ", ab->name,
                     ab->topic[0] ? " — " : "", ab->topic);
            in = timui_border(f, mid[0], TIMUI_BOX_ROUNDED, timui_str_from_cstr(title), border_st);
            /* wheel / PgUp / PgDn scroll the active buffer. */
            ab->scroll += timui_mouse_wheel(f);
            if(timui_key_pressed(f, TIMUI_KEY_PAGE_UP))   ab->scroll += in.h > 1 ? in.h - 1 : 1;
            if(timui_key_pressed(f, TIMUI_KEY_PAGE_DOWN)) ab->scroll -= in.h > 1 ? in.h - 1 : 1;
            { int maxs = ab->count - in.h; if(maxs < 0) maxs = 0;
              if(ab->scroll > maxs) ab->scroll = maxs;
              if(ab->scroll < 0)    ab->scroll = 0; }
            draw_scrollback(f, ab, in, panel, text_fg, self_fg, sys_fg, code_fg, link_fg);

            /* nick-list panel (channels only). */
            { char nt[24];
              TimuiRect nin;
              snprintf(nt, sizeof nt, " %d nicks ", ab->nnicks);
              nin = timui_border(f, mid[1], TIMUI_BOX_ROUNDED,
                                 ab->is_channel ? timui_str_from_cstr(nt) : TIMUI_STR_LIT(" nicks "),
                                 border_st);
              if(ab->is_channel){
                  static TimuiTableState nst;            /* view state persists across frames */
                  TimuiStr nh = TIMUI_STR_LIT("nick");
                  TimuiTableModel nm = { &nh, 1, ab->nnicks, nick_cell, ab, 3, 16, 128 };
                  (void)timui_table_ex_mut(f, TIMUI_ID("nicks"), nin, &nm, &nst);
              }
            }
          }

          /* composer: a one-row textarea (plain Enter submits; Shift+Enter inserts
           * a newline where the terminal reports modifiers). Shift+←/→ switch
           * channels; ↑/↓ recall sent lines; /connect starts the worker on demand;
           * other /commands go through irc_submit. */
          { TimuiRect in = timui_border(f, rows[3], TIMUI_BOX_ROUNDED,
                TIMUI_STR_LIT(" /connect /join /part /msg /nick /me /quit \xC2\xB7 \xE2\x86\x91\xE2\x86\x93 history \xC2\xB7 Shift+\xE2\x86\x90\xE2\x86\x92 channel "),
                border_st);
            TimuiRect line = in, fld; line.h = 1;
            fld = line; fld.x += 2; fld.w -= 2;
            timui_label(f, in.x, in.y, TIMUI_STR_LIT("\xE2\x9D\xAF "),
                        timui_style_make(link_fg, panel.bg, TIMUI_ATTR_BOLD));

            /* Keep the composer focused so you can always type — clicking a tab
             * still switches channel (the tab consumes the click) but focus
             * returns here, and the nick list / tabs are mouse-driven. */
            if(timui_focus(f) != TIMUI_ID("compose")) timui_set_focus(f, TIMUI_ID("compose"));

            /* Shift+←/→ switch the active buffer (channel), like clicking a tab. */
            if(client.nbufs > 0){
                if(timui_key_pressed_mods(f, TIMUI_KEY_LEFT,  TIMUI_MOD_SHIFT))
                    client.active = (client.active - 1 + client.nbufs) % client.nbufs;
                if(timui_key_pressed_mods(f, TIMUI_KEY_RIGHT, TIMUI_MOD_SHIFT))
                    client.active = (client.active + 1) % client.nbufs;
            }

            /* ↑/↓ recall previously-sent lines (shell-style). */
            if(timui_key_pressed(f, TIMUI_KEY_UP) &&
               !timui_key_pressed_mods(f, TIMUI_KEY_UP, TIMUI_MOD_SHIFT) &&
               hist_count > 0 && hist_pos > 0){
                hist_pos--;
                snprintf(compose, sizeof compose, "%s", history[hist_pos]);
                compose_state.cursor = strlen(compose); compose_state.scroll_y = 0;
            }
            if(timui_key_pressed(f, TIMUI_KEY_DOWN) &&
               !timui_key_pressed_mods(f, TIMUI_KEY_DOWN, TIMUI_MOD_SHIFT) &&
               hist_pos < hist_count){
                hist_pos++;
                if(hist_pos == hist_count){ compose[0] = '\0'; compose_state.cursor = 0; }
                else { snprintf(compose, sizeof compose, "%s", history[hist_pos]);
                       compose_state.cursor = strlen(compose); }
                compose_state.scroll_y = 0;
            }

            { TimuiTextAreaResult compose_res =
                  timui_text_area_mut(f, TIMUI_ID("compose"), fld, &compose_state,
                                      TIMUI_TEXT_AREA_ENTER_SUBMITS);
              if(compose_res.submitted){
                if(compose[0] && hist_count < (int)(sizeof history / sizeof history[0]))
                    snprintf(history[hist_count++], IRC_MSG_MAX, "%s", compose);   /* record for ↑/↓ */
                hist_pos = hist_count;
                /* /connect <host> [port]: start the worker on demand (needs the
                 * thread + net in this scope, unlike the other slash commands). */
                if(!strncmp(compose, "/connect", 8) && (compose[8] == ' ' || compose[8] == '\0')){
                    if(worker_started)
                        buf_logf(&client.bufs[0], LK_ERROR, "already connected to %s", net.host);
                    else {
                        char h[128]; int p = 6667, hi = 0; const char *r = compose + 8;
                        while(*r == ' ') r++;
                        while(*r && *r != ' ' && hi < (int)sizeof h - 1) h[hi++] = *r++;
                        h[hi] = '\0';
                        while(*r == ' ') r++;
                        if(*r) p = atoi(r);
                        if(!h[0]) buf_logf(&client.bufs[0], LK_ERROR, "usage: /connect <host> [port]");
                        else {
                            net.ui = ui; net.stop = 0; net.state = NET_CONNECTING; net.port = p;
                            snprintf(net.host, sizeof net.host, "%s", h);
                            snprintf(net.nick, sizeof net.nick, "%s", client.nick);
                            snprintf(net.channel, sizeof net.channel, "%s", channel);
                            snprintf(client.server, sizeof client.server, "%s", h);
                            pthread_mutex_init(&net.out_lock, NULL);
                            if(pthread_create(&th, NULL, irc_worker, &net) == 0){
                                worker_started = 1;
                                buf_logf(&client.bufs[0], LK_SYSTEM, "connecting to %s:%d\xE2\x80\xA6", h, p);
                            } else { pthread_mutex_destroy(&net.out_lock);
                                buf_logf(&client.bufs[0], LK_ERROR, "could not start network worker"); }
                        }
                    }
                } else {
                    irc_submit(&client, worker_started ? &net : NULL, compose);
                    if(irc_ieq_(compose, "/quit")) timui_quit(ui);
                }
                compose[0] = '\0'; compose_state.cursor = 0; compose_state.scroll_y = 0;
                client.bufs[client.active].scroll = 0;        /* snap to newest on send */
              }
              timui_draw_fill(timui_frame_buffer(f), fld, panel);
              timui_push_clip(f, fld);
              timui_label(f, fld.x, fld.y, timui_str_from_cstr(compose),
                          timui_style_make(text_fg, panel.bg, 0));
              timui_pop_clip(f);
            }
            (void)title_st; (void)host;
          }

          timui_end(f);
          if(max_frames > 0 && ++frames >= max_frames) timui_quit(ui);
      }
    }

    /* W14 shutdown ordering: stop + join the worker BEFORE timui_close. */
    if(worker_started){
        net.stop = 1;
        pthread_join(th, NULL);
        pthread_mutex_destroy(&net.out_lock);
    }
    timui_close(ui);
    return 0;
}
