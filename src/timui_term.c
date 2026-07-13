/* ---- terminal transport + fake backend --------------------------------- */
static int fake_write(TimuiTransport *t, const void *data, size_t len){
    TimuiFakeTransport *f = (TimuiFakeTransport *)t->ctx;
    if(len > SIZE_MAX - f->out_len) return -1;        /* out_len+len would overflow */
    if(f->out_len + len > f->out_cap){
        size_t ncap = f->out_cap ? f->out_cap : 64;
        size_t target = f->out_len + len;
        unsigned char *nb;
        while(ncap < target){ if(ncap > SIZE_MAX / 2) return -1; ncap *= 2; }
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
static void fake_close(TimuiTransport *t){
    if(t && t->ctx) timui_fake_destroy((TimuiFakeTransport *)t->ctx);
}

TIMUI_API TimuiResult timui_fake_init(TimuiFakeTransport *f, const TimuiAllocator *alloc){
    if(!f || !timui_allocator_valid_(alloc)) return TIMUI_ERR_INVALID_ARGUMENT;
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
        /* Sanitize at the codepoint level: decode UTF-8 and drop C0/DEL/C1
         * control CODEPOINTS — incl. U+009C (the C1 String Terminator, UTF-8
         * C2 9C) that closes an OSC like BEL or ESC \ — re-encoding printable
         * codepoints. A byte-level reject of 0x80-0x9f (an earlier attempt)
         * stripped VALID UTF-8 continuation bytes (Ü = C3 9C, 字 = E5 AD 97);
         * filtering by decoded codepoint keeps multibyte text intact while
         * still removing only control codepoints. Single buffered write. */
        char clean[128];
        size_t cn = 0, i = 0;
        while(i < title.len && cn + 4 < sizeof(clean)){
            uint32_t cp = 0;
            int adv = timui_utf8_decode(title.ptr + i, title.len - i, &cp);
            if(adv <= 0){ i++; continue; }                  /* incomplete lead: skip */
            if(cp >= 0x20 && cp != 0x7f && !(cp >= 0x80 && cp <= 0x9f))
                cn += (size_t)timui_utf8_encode_(cp, clean + cn);  /* keep printable cp */
            i += (size_t)adv;
        }
        if(cn > 0){   /* skip OSC entirely if all chars were stripped */
            TIMUI_EMIT(t, "\x1b]0;");
            if(t && t->write) (void)t->write(t, clean, cn);
            TIMUI_EMIT(t, "\x07");
        }
    }
    /* Disable auto-wrap (DECAWM off) unconditionally: the diff renderer positions
     * every cell with its own CUP, so a glyph written to the last column must
     * stay put — with auto-wrap on it moves the cursor to the next line (or
     * scrolls the screen when that cell is the bottom-right), desyncing the
     * renderer from the terminal. This is standard for cell-based TUIs. */
    TIMUI_EMIT(t, "\x1b[?7l");
    if(flags & TIMUI_FLAG_ALT_SCREEN)      TIMUI_EMIT(t, "\x1b[?1049h");
    if(flags & TIMUI_FLAG_KITTY_KEYBOARD)  TIMUI_EMIT(t, "\x1b[>1u");
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
    if(flags & TIMUI_FLAG_KITTY_KEYBOARD)  TIMUI_EMIT(t, "\x1b[<u");
    TIMUI_EMIT(t, "\x1b[?25h");
    if(flags & TIMUI_FLAG_ALT_SCREEN)      TIMUI_EMIT(t, "\x1b[?1049l");
    TIMUI_EMIT(t, "\x1b[?7h");             /* restore auto-wrap on exit */
}

/* ---- terminal raw mode (POSIX) ---------------------------------------- */
/* Z25 test seam: when armed, timui_termios_enter takes its tcsetattr-failure
 * branch without calling tcsetattr (no portable way to fail a real fd's
 * tcsetattr while tcgetattr succeeds). This is a documented, test-only static —
 * inert (0) in production, the only mutable static here besides the SIGTERM
 * restore carve-out. */
static int g_tcsetattr_fail_for_test = 0;
TIMUI_API void timui_termios_fail_tcsetattr_for_test(int on){ g_tcsetattr_fail_for_test = on; }
TIMUI_API TimuiResult timui_termios_enter(TimuiTermios *t, int fd){
    struct termios *orig, raw;
    if(!t) return TIMUI_ERR_INVALID_ARGUMENT;
    t->fd = fd;
    t->saved = NULL;
    t->have_saved = 0;
    orig = (struct termios *)malloc(sizeof(struct termios));
    if(!orig) return TIMUI_ERR_OUT_OF_MEMORY;
    if(tcgetattr(fd, orig) != 0){ free(orig); return TIMUI_ERR_OS; }
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
    /* short-circuit keeps the real tcsetattr un-called when the seam is armed */
    if(g_tcsetattr_fail_for_test || tcsetattr(fd, TCSAFLUSH, &raw) != 0){
        free(orig); t->saved = NULL; t->have_saved = 0; return TIMUI_ERR_OS;
    }
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
    return timui_term_size_pixels(fd, out_w, out_h, NULL, NULL);
}
TIMUI_API TimuiResult timui_term_size_pixels(int fd, int *out_w, int *out_h,
                                             int *out_px_w, int *out_px_h){
    struct winsize ws;
    if(ioctl(fd, TIOCGWINSZ, &ws) != 0){
        return (errno == ENOTTY) ? TIMUI_ERR_NOT_A_TTY : TIMUI_ERR_OS;
    }
    if(out_w) *out_w = (int)ws.ws_col;
    if(out_h) *out_h = (int)ws.ws_row;
    if(out_px_w) *out_px_w = (int)ws.ws_xpixel;
    if(out_px_h) *out_px_h = (int)ws.ws_ypixel;
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
static int caps_is_iterm2(const char *tp){
    return caps_streq(tp, "iTerm.app") || caps_streq(tp, "iTerm2");
}
static int caps_is_modern(const char *tp){
    return caps_is_kitty_family(tp) || caps_streq(tp, "WezTerm")
        || caps_streq(tp, "alacritty") || caps_streq(tp, "foot")
        || caps_streq(tp, "rio") || caps_is_iterm2(tp);
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

#define TIMUI_IMAGE_CAP_MASK_ ((uint32_t)(TIMUI_CAP_KITTY_GRAPHICS | TIMUI_CAP_SIXEL_GRAPHICS | TIMUI_CAP_ITERM2_IMAGES))

TIMUI_API void timui_caps_detect_report(TimuiCapsReport *r, const char *term,
                                        const char *term_program,
                                        const char *colorterm,
                                        const char *ssh_connection){
    TimuiCaps *c;
    uint32_t bits;
    uint32_t before;
    int modern;
    int kitty_family;
    int iterm2;
    int multiplexer;
    if(!r) return;
    memset(r, 0, sizeof(*r));
    c = &r->caps;
    modern = caps_is_modern(term_program) || caps_is_modern(term);
    kitty_family = caps_is_kitty_family(term_program) || caps_is_kitty_family(term);
    iterm2 = caps_is_iterm2(term_program) || caps_is_iterm2(term);
    multiplexer = term && (!strncmp(term, "tmux", 4) || !strncmp(term, "screen", 6) ||
                           !strncmp(term, "zellij", 6));
    if(ssh_connection && *ssh_connection) r->notes |= TIMUI_CAPS_NOTE_SSH_SESSION;
    c->colors = 16;
    caps_set_str(c->term, sizeof(c->term), term);
    caps_set_str(c->term_program, sizeof(c->term_program), term_program);
    if(colorterm && (caps_streq(colorterm, "truecolor") || caps_streq(colorterm, "24bit"))){
        c->flags |= TIMUI_CAP_TRUECOLOR;
        r->enabled_by_env |= TIMUI_CAP_TRUECOLOR;
        r->notes |= TIMUI_CAPS_NOTE_TRUECOLOR_ENV;
        c->colors = 16777216;
    }
    if(modern){
        bits = TIMUI_CAP_TRUECOLOR | TIMUI_CAP_256_COLOR | TIMUI_CAP_SGR_MOUSE
             | TIMUI_CAP_BRACKETED_PASTE | TIMUI_CAP_FOCUS_EVENTS
             | TIMUI_CAP_SYNC_OUTPUT | TIMUI_CAP_OSC8_HYPERLINKS;
        c->flags |= bits;
        r->enabled_by_env |= bits;
        r->notes |= TIMUI_CAPS_NOTE_MODERN_TERMINAL;
        if(c->colors < 16777216) c->colors = 16777216;
        if(kitty_family){
            bits = TIMUI_CAP_KITTY_KEYBOARD | TIMUI_CAP_KITTY_GRAPHICS | TIMUI_CAP_UNICODE_CORE;
            c->flags |= bits;
            r->enabled_by_env |= bits;
            r->notes |= TIMUI_CAPS_NOTE_KITTY_FAMILY;
        }
        if(iterm2){
            bits = TIMUI_CAP_ITERM2_IMAGES | TIMUI_CAP_UNICODE_CORE;
            c->flags |= bits;
            r->enabled_by_env |= bits;
            r->notes |= TIMUI_CAPS_NOTE_ITERM2;
        }
    } else if(term && strstr(term, "256color")){
        c->flags |= TIMUI_CAP_256_COLOR;
        r->enabled_by_env |= TIMUI_CAP_256_COLOR;
        r->notes |= TIMUI_CAPS_NOTE_256COLOR_TERM;
        c->colors = 256;
    } else {
        r->notes |= TIMUI_CAPS_NOTE_SAFE_FALLBACK;
    }
    /* multiplexers reduce capabilities. Image protocols are ALWAYS stripped
     * under a multiplexer: they require explicit passthrough + graphics support
     * we can't assume, and dropped image escapes leave a grey placeholder region
     * plus stray cursor moves. Keyboard and sync are only kept when the OUTER
     * terminal (TERM_PROGRAM, inherited into the session) is kitty-family;
     * otherwise stripped. timui_force_cap overrides either way (W12). */
    if(multiplexer){
        r->notes |= TIMUI_CAPS_NOTE_MULTIPLEXER;
        before = c->flags;
        c->flags &= ~TIMUI_IMAGE_CAP_MASK_;
        r->disabled_by_multiplexer |= before & TIMUI_IMAGE_CAP_MASK_;
        if(caps_is_kitty_family(term_program)){
            r->notes |= TIMUI_CAPS_NOTE_KITTY_PASSTHROUGH;
        }else{
            bits = before & (TIMUI_CAP_KITTY_KEYBOARD | TIMUI_CAP_SYNC_OUTPUT);
            c->flags &= ~(TIMUI_CAP_KITTY_KEYBOARD | TIMUI_CAP_SYNC_OUTPUT);
            r->disabled_by_multiplexer |= bits;
        }
        c->flags |= TIMUI_CAP_256_COLOR;
        if(c->colors < 256) c->colors = 256;
    }
#ifdef TIMUI_NO_IMAGES
    before = c->flags;
    c->flags &= ~TIMUI_IMAGE_CAP_MASK_;
    r->disabled_by_build |= before & TIMUI_IMAGE_CAP_MASK_;
    r->notes |= TIMUI_CAPS_NOTE_IMAGES_COMPILED_OUT;
#endif
}
TIMUI_API void timui_caps_detect(TimuiCaps *c, const char *term, const char *term_program, const char *colorterm){
    TimuiCapsReport r;
    if(!c) return;
    timui_caps_detect_report(&r, term, term_program, colorterm, NULL);
    *c = r.caps;
}
TIMUI_API void timui_caps_apply_force(TimuiCaps *c, uint32_t force_on, uint32_t force_off){
    if(!c) return;
    c->flags |= force_on;
    c->flags &= ~force_off;
#ifdef TIMUI_NO_IMAGES
    c->flags &= ~TIMUI_IMAGE_CAP_MASK_;
#endif
}
TIMUI_API int timui_caps_has(const TimuiCaps *c, TimuiCapFlags cap){
    return c && ((c->flags & (uint32_t)cap) != 0);
}
TIMUI_API void timui_force_cap(Timui *ui, TimuiCapFlags cap, int enable){
    uint32_t bits = (uint32_t)cap;
    if(!ui) return;
#ifdef TIMUI_NO_IMAGES
    bits &= ~TIMUI_IMAGE_CAP_MASK_;
    ui->caps.flags &= ~TIMUI_IMAGE_CAP_MASK_;
    if(bits == 0) return;
#endif
    if(enable) ui->caps.flags |= bits;
    else       ui->caps.flags &= ~bits;
}
TIMUI_API TimuiImageProtocol timui_caps_image_protocol(const TimuiCaps *c){
    if(!c) return TIMUI_IMAGE_PROTOCOL_NONE;
#ifdef TIMUI_NO_IMAGES
    (void)c;
    return TIMUI_IMAGE_PROTOCOL_NONE;
#else
    if(c->flags & TIMUI_CAP_KITTY_GRAPHICS) return TIMUI_IMAGE_PROTOCOL_KITTY;
    if(c->flags & TIMUI_CAP_SIXEL_GRAPHICS) return TIMUI_IMAGE_PROTOCOL_SIXEL;
    if(c->flags & TIMUI_CAP_ITERM2_IMAGES) return TIMUI_IMAGE_PROTOCOL_ITERM2;
    return TIMUI_IMAGE_PROTOCOL_NONE;
#endif
}
#undef TIMUI_IMAGE_CAP_MASK_

/* ---- synchronized output (DEC 2026) + cursor -------------------------- */
TIMUI_API void timui_sync_begin(TimuiTransport *t){ TIMUI_EMIT(t, "\x1b[?2026h"); }
TIMUI_API void timui_sync_end(TimuiTransport *t){ TIMUI_EMIT(t, "\x1b[?2026l"); }
TIMUI_API void timui_hide_cursor(TimuiTransport *t){ TIMUI_EMIT(t, "\x1b[?25l"); }
TIMUI_API void timui_show_cursor(TimuiTransport *t){ TIMUI_EMIT(t, "\x1b[?25h"); }
#undef TIMUI_EMIT   /* Z10: impl-only macro must not leak into the consumer TU */
