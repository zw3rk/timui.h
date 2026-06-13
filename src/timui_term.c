/* ---- terminal transport + fake backend --------------------------------- */
static int fake_write(TimuiTransport *t, const void *data, size_t len){
    TimuiFakeTransport *f = (TimuiFakeTransport *)t->ctx;
    if(f->out_len + len > f->out_cap){
        size_t ncap = f->out_cap ? f->out_cap : 64;
        unsigned char *nb;
        while(ncap < f->out_len + len) ncap *= 2;
        nb = (unsigned char *)f->alloc.realloc(f->alloc.userdata, f->out, f->out_cap, ncap);
        if(!nb) return -1;
        f->out = nb;
        f->out_cap = ncap;
    }
    memcpy(f->out + f->out_len, data, len);
    f->out_len += len;
    return (int)len;
}
static int fake_read(TimuiTransport *t, void *buf, size_t cap){
    TimuiFakeTransport *f = (TimuiFakeTransport *)t->ctx;
    size_t avail = f->in_len - f->in_pos;
    size_t n = avail < cap ? avail : cap;
    if(n == 0) return 0;
    memcpy(buf, f->in + f->in_pos, n);
    f->in_pos += n;
    return (int)n;
}
static int fake_flush(TimuiTransport *t){ (void)t; return 0; }
static void fake_close(TimuiTransport *t){ (void)t; }

TIMUI_API TimuiResult timui_fake_init(TimuiFakeTransport *f, const TimuiAllocator *alloc){
    if(!f || !alloc) return TIMUI_ERR_INVALID_ARGUMENT;
    f->alloc = *alloc;
    f->out = NULL; f->out_cap = 0; f->out_len = 0;
    f->in = NULL;  f->in_len = 0;  f->in_pos = 0;
    return TIMUI_OK;
}
TIMUI_API void timui_fake_destroy(TimuiFakeTransport *f){
    if(!f) return;
    if(f->out) f->alloc.free(f->alloc.userdata, f->out, f->out_cap);
    f->out = NULL; f->out_cap = 0; f->out_len = 0;
    f->in = NULL;  f->in_len = 0;  f->in_pos = 0;
}
TIMUI_API void timui_fake_set_input(TimuiFakeTransport *f, const void *bytes, size_t len){
    if(!f) return;
    f->in = (const unsigned char *)bytes;
    f->in_len = len;
    f->in_pos = 0;
}
TIMUI_API TimuiStr timui_fake_output(const TimuiFakeTransport *f){
    TimuiStr s;
    if(!f){ s.ptr = NULL; s.len = 0; return s; }
    s.ptr = (const char *)f->out;
    s.len = f->out_len;
    return s;
}
TIMUI_API void timui_fake_clear_output(TimuiFakeTransport *f){
    if(f) f->out_len = 0;
}
TIMUI_API TimuiTransport timui_fake_transport(TimuiFakeTransport *f){
    TimuiTransport t;
    t.write = fake_write;
    t.read  = fake_read;
    t.flush = fake_flush;
    t.close = fake_close;
    t.ctx   = f;
    return t;
}

/* ---- screen mode setup/teardown ---------------------------------------- *
 * Emit DEC/private-mode escapes through the transport. enter() records the
 * enabled flags so exit() can emit the matching resets in reverse order. */
static void emit_lit(TimuiTransport *t, const char *s, size_t n){
    if(t && t->write) (void)t->write(t, s, n);
}
#define TIMUI_EMIT(t, lit) emit_lit((t), (lit), sizeof(lit) - 1)

TIMUI_API void timui_screen_enter(TimuiTransport *t, TimuiScreenMode *m, uint32_t flags, TimuiStr title){
    if(m) m->flags = flags;
    if(title.ptr && title.len){
        TIMUI_EMIT(t, "\x1b]0;");                       /* OSC 0 ; */
        /* Sanitize: strip BEL/ESC that would inject terminal control sequences */
        {   const char *p = title.ptr;
            size_t j;
            for(j = 0; j < title.len; j++){
                if(p[j] != 0x07 && p[j] != 0x1b){
                    char ch = p[j];
                    if(t && t->write) (void)t->write(t, &ch, 1);
                }
            }
        }
        TIMUI_EMIT(t, "\x07");                          /* BEL */
    }
    if(flags & TIMUI_FLAG_ALT_SCREEN)      TIMUI_EMIT(t, "\x1b[?1049h");
    if(flags & TIMUI_FLAG_HIDE_CURSOR)     TIMUI_EMIT(t, "\x1b[?25l");
    if(flags & TIMUI_FLAG_MOUSE){          TIMUI_EMIT(t, "\x1b[?1000h"); TIMUI_EMIT(t, "\x1b[?1006h"); }
    if(flags & TIMUI_FLAG_BRACKETED_PASTE) TIMUI_EMIT(t, "\x1b[?2004h");
    if(flags & TIMUI_FLAG_FOCUS_EVENTS)    TIMUI_EMIT(t, "\x1b[?1004h");
}
TIMUI_API void timui_screen_exit(TimuiTransport *t, TimuiScreenMode *m){
    uint32_t flags = m ? m->flags : 0;
    if(flags & TIMUI_FLAG_FOCUS_EVENTS)    TIMUI_EMIT(t, "\x1b[?1004l");
    if(flags & TIMUI_FLAG_BRACKETED_PASTE) TIMUI_EMIT(t, "\x1b[?2004l");
    if(flags & TIMUI_FLAG_MOUSE){          TIMUI_EMIT(t, "\x1b[?1006l"); TIMUI_EMIT(t, "\x1b[?1000l"); }
    if(flags & TIMUI_FLAG_HIDE_CURSOR)     TIMUI_EMIT(t, "\x1b[?25h");
    if(flags & TIMUI_FLAG_ALT_SCREEN)      TIMUI_EMIT(t, "\x1b[?1049l");
}

/* ---- terminal raw mode (POSIX) ---------------------------------------- */
TIMUI_API TimuiResult timui_termios_enter(TimuiTermios *t, int fd){
    struct termios *orig, raw;
    if(!t) return TIMUI_ERR_INVALID_ARGUMENT;
    orig = (struct termios *)malloc(sizeof(struct termios));
    if(!orig) return TIMUI_ERR_OUT_OF_MEMORY;
    if(tcgetattr(fd, orig) != 0){ free(orig); return TIMUI_ERR_OS; }
    t->fd = fd;
    t->saved = orig;
    t->have_saved = 1;
    raw = *orig;
    raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    raw.c_oflag &= ~OPOST;
    raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= ~(CSIZE | PARENB);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    if(tcsetattr(fd, TCSAFLUSH, &raw) != 0){ free(orig); t->have_saved = 0; return TIMUI_ERR_OS; }
    return TIMUI_OK;
}
TIMUI_API TimuiResult timui_termios_restore(TimuiTermios *t){
    if(!t || !t->have_saved) return TIMUI_ERR_INVALID_ARGUMENT;
    if(tcsetattr(t->fd, TCSAFLUSH, (const struct termios *)t->saved) != 0) return TIMUI_ERR_OS;
    return TIMUI_OK;
}
TIMUI_API void timui_termios_destroy(TimuiTermios *t){
    if(!t) return;
    if(t->saved){ free(t->saved); t->saved = NULL; }
    t->have_saved = 0;
}
TIMUI_API TimuiResult timui_term_size(int fd, int *out_w, int *out_h){
    struct winsize ws;
    if(ioctl(fd, TIOCGWINSZ, &ws) != 0){
        return (errno == ENOTTY) ? TIMUI_ERR_NOT_A_TTY : TIMUI_ERR_OS;
    }
    if(out_w) *out_w = (int)ws.ws_col;
    if(out_h) *out_h = (int)ws.ws_row;
    return TIMUI_OK;
}

/* ---- capability detection --------------------------------------------- */
static int caps_streq(const char *a, const char *b){
    return a && b && strcmp(a, b) == 0;
}
static int caps_is_kitty_family(const char *tp){
    return caps_streq(tp, "kitty") || caps_streq(tp, "xterm-kitty")
        || caps_streq(tp, "ghostty") || caps_streq(tp, "xterm-ghostty");
}
static int caps_is_modern(const char *tp){
    return caps_is_kitty_family(tp) || caps_streq(tp, "WezTerm")
        || caps_streq(tp, "alacritty") || caps_streq(tp, "foot")
        || caps_streq(tp, "rio");
}
static void caps_set_str(char *dst, size_t cap, const char *src){
    size_t n;
    if(!dst) return;
    dst[0] = '\0';
    if(!src) return;
    n = strlen(src);
    if(n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}
TIMUI_API void timui_caps_detect(TimuiCaps *c, const char *term, const char *term_program, const char *colorterm){
    if(!c) return;
    memset(c, 0, sizeof(*c));
    c->colors = 16;
    caps_set_str(c->term, sizeof(c->term), term);
    caps_set_str(c->term_program, sizeof(c->term_program), term_program);
    if(colorterm && (caps_streq(colorterm, "truecolor") || caps_streq(colorterm, "24bit"))){
        c->flags |= TIMUI_CAP_TRUECOLOR;
        c->colors = 16777216;
    }
    if(caps_is_modern(term_program) || caps_is_modern(term)){
        c->flags |= TIMUI_CAP_TRUECOLOR | TIMUI_CAP_256_COLOR | TIMUI_CAP_SGR_MOUSE
                  | TIMUI_CAP_BRACKETED_PASTE | TIMUI_CAP_FOCUS_EVENTS
                  | TIMUI_CAP_SYNC_OUTPUT | TIMUI_CAP_OSC8_HYPERLINKS;
        if(c->colors < 16777216) c->colors = 16777216;
        if(caps_is_kitty_family(term_program) || caps_is_kitty_family(term)){
            c->flags |= TIMUI_CAP_KITTY_KEYBOARD | TIMUI_CAP_KITTY_GRAPHICS | TIMUI_CAP_UNICODE_CORE;
        }
    } else if(term && strstr(term, "256color")){
        c->flags |= TIMUI_CAP_256_COLOR;
        c->colors = 256;
    }
    /* multiplexers reduce capabilities unless explicit passthrough is known */
    if(term && (!strncmp(term, "tmux", 4) || !strncmp(term, "screen", 6) || !strncmp(term, "zellij", 6))){
        c->flags &= ~(TIMUI_CAP_KITTY_KEYBOARD | TIMUI_CAP_KITTY_GRAPHICS | TIMUI_CAP_SYNC_OUTPUT);
        c->flags |= TIMUI_CAP_256_COLOR;
        if(c->colors < 256) c->colors = 256;
    }
}
TIMUI_API void timui_caps_apply_force(TimuiCaps *c, uint32_t force_on, uint32_t force_off){
    if(!c) return;
    c->flags |= force_on;
    c->flags &= ~force_off;
}
TIMUI_API int timui_caps_has(const TimuiCaps *c, TimuiCapFlags cap){
    return c && ((c->flags & (uint32_t)cap) != 0);
}

/* ---- synchronized output (DEC 2026) + cursor -------------------------- */
TIMUI_API void timui_sync_begin(TimuiTransport *t){ TIMUI_EMIT(t, "\x1b[?2026h"); }
TIMUI_API void timui_sync_end(TimuiTransport *t){ TIMUI_EMIT(t, "\x1b[?2026l"); }
TIMUI_API void timui_hide_cursor(TimuiTransport *t){ TIMUI_EMIT(t, "\x1b[?25l"); }
TIMUI_API void timui_show_cursor(TimuiTransport *t){ TIMUI_EMIT(t, "\x1b[?25h"); }

