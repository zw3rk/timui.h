/* ---- cell buffer ------------------------------------------------------ */
TIMUI_API TimuiResult timui_cells_init(TimuiCellBuffer *buf, int w, int h, const TimuiAllocator *alloc){
    size_t n;
    if(!buf || w <= 0 || h <= 0 || !alloc) return TIMUI_ERR_INVALID_ARGUMENT;
    n = (size_t)w * (size_t)h;
    buf->w = w;
    buf->h = h;
    buf->alloc = *alloc;
    buf->has_clip = 0;
    buf->cells = (TimuiCell *)alloc->alloc(alloc->userdata, n * sizeof(TimuiCell));
    if(!buf->cells){ buf->w = buf->h = 0; return TIMUI_ERR_OUT_OF_MEMORY; }
    timui_cells_clear(buf);
    return TIMUI_OK;
}
TIMUI_API void timui_cells_destroy(TimuiCellBuffer *buf){
    size_t n;
    if(!buf || !buf->cells) return;
    n = (size_t)buf->w * (size_t)buf->h;
    buf->alloc.free(buf->alloc.userdata, buf->cells, n * sizeof(TimuiCell));
    buf->cells = NULL;
    buf->w = buf->h = 0;
}
TIMUI_API TimuiResult timui_cells_resize(TimuiCellBuffer *buf, int w, int h, const TimuiAllocator *alloc){
    size_t n, oldn;
    TimuiCell *nc;
    if(!buf || w <= 0 || h <= 0) return TIMUI_ERR_INVALID_ARGUMENT;
    if(alloc) buf->alloc = *alloc;
    n = (size_t)w * (size_t)h;
    oldn = (size_t)buf->w * (size_t)buf->h;
    nc = (TimuiCell *)buf->alloc.realloc(buf->alloc.userdata, buf->cells,
                                         oldn * sizeof(TimuiCell), n * sizeof(TimuiCell));
    if(!nc) return TIMUI_ERR_OUT_OF_MEMORY;
    buf->cells = nc;
    buf->w = w;
    buf->h = h;
    timui_cells_clear(buf);
    return TIMUI_OK;
}
TIMUI_API void timui_cells_clear(TimuiCellBuffer *buf){
    if(!buf || !buf->cells) return;
    memset(buf->cells, 0, (size_t)buf->w * (size_t)buf->h * sizeof(TimuiCell));
}
TIMUI_API TimuiCell *timui_cells_get(TimuiCellBuffer *buf, int x, int y){
    if(!buf || !buf->cells || x < 0 || y < 0 || x >= buf->w || y >= buf->h) return NULL;
    return &buf->cells[(size_t)y * (size_t)buf->w + (size_t)x];
}
TIMUI_API int timui_cells_put(TimuiCellBuffer *buf, int x, int y, const TimuiCell *cell){
    TimuiCell *dst;
    if(!cell) return 0;
    dst = timui_cells_get(buf, x, y);
    if(!dst) return 0;
    *dst = *cell;
    return 1;
}

/* ---- utf-8 decode + width --------------------------------------------- */
TIMUI_API int timui_utf8_decode(const char *s, size_t len, uint32_t *out_cp){
    const unsigned char *p = (const unsigned char *)s;
    uint32_t cp = 0;
    int need = 0, i;
    if(!s || len == 0){ if(out_cp) *out_cp = 0; return 0; }
    if(p[0] < 0x80){ if(out_cp) *out_cp = p[0]; return 1; }
    if((p[0] & 0xE0) == 0xC0){ cp = (uint32_t)(p[0] & 0x1F); need = 1; }
    else if((p[0] & 0xF0) == 0xE0){ cp = (uint32_t)(p[0] & 0x0F); need = 2; }
    else if((p[0] & 0xF8) == 0xF0){ cp = (uint32_t)(p[0] & 0x07); need = 3; }
    else { if(out_cp) *out_cp = 0xFFFD; return 1; }            /* invalid lead */
    if((int)len < 1 + need){ return 0; }                       /* incomplete */
    for(i = 1; i <= need; i++){
        if((p[i] & 0xC0) != 0x80){ if(out_cp) *out_cp = 0xFFFD; return 1; }   /* bad continuation */
        cp = (cp << 6) | (uint32_t)(p[i] & 0x3F);
    }
    if(out_cp) *out_cp = cp;
    return 1 + need;
}
TIMUI_API int timui_utf8_width(uint32_t cp){
    if(cp < 0x20 || cp == 0x7F) return 0;                       /* control */
    if(cp == 0xFFFD) return 1;
    if((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
       (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
       (cp >= 0xFE20 && cp <= 0xFE2F)) return 0;                /* combining */
    if((cp >= 0x1100 && cp <= 0x115F) ||
       (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7A3) ||
       (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE6F) ||
       (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6) ||
       (cp >= 0x1F300 && cp <= 0x1FAFF)) return 2;              /* wide/fullwidth */
    return 1;
}

/* ---- drawing primitives ----------------------------------------------- */
static void put_glyph(TimuiCellBuffer *buf, int x, int y, uint32_t cp, TimuiStyle st){
    TimuiCell c;
    if(buf->has_clip && (x < buf->clip.x || y < buf->clip.y ||
       x >= buf->clip.x + buf->clip.w || y >= buf->clip.y + buf->clip.h)) return;
    memset(&c, 0, sizeof c);
    c.codepoint = cp;
    c.fg = st.fg;
    c.bg = st.bg;
    c.attrs = st.attrs;
    c.width = 1;
    timui_cells_put(buf, x, y, &c);
}
TIMUI_API TimuiStyle timui_style_make(uint32_t fg, uint32_t bg, uint32_t attrs){
    TimuiStyle s;
    s.fg = fg;
    s.bg = bg;
    s.attrs = attrs;
    return s;
}
TIMUI_API void timui_draw_text(TimuiCellBuffer *buf, int x, int y, TimuiStr text, TimuiStyle st){
    size_t i = 0;
    int cx = x;
    if(!buf || !text.ptr) return;
    while(i < text.len){
        uint32_t cp = 0;
        int adv = timui_utf8_decode(text.ptr + i, text.len - i, &cp);
        int w;
        if(adv <= 0) adv = 1;
        w = timui_utf8_width(cp);
        if(w > 0){
            put_glyph(buf, cx, y, cp, st);
            cx += w;
        }
        i += (size_t)adv;
    }
}
TIMUI_API void timui_draw_fill(TimuiCellBuffer *buf, TimuiRect r, TimuiStyle st){
    int xi, yi;
    if(!buf) return;
    if(r.w < 0) r.w = 0;
    if(r.h < 0) r.h = 0;
    for(yi = r.y; yi < r.y + r.h; yi++)
        for(xi = r.x; xi < r.x + r.w; xi++)
            put_glyph(buf, xi, yi, ' ', st);
}
TIMUI_API void timui_draw_hline(TimuiCellBuffer *buf, int x, int y, int w, TimuiStyle st){
    int i;
    if(!buf || w <= 0) return;
    for(i = 0; i < w; i++) put_glyph(buf, x + i, y, 0x2500, st);
}
TIMUI_API void timui_draw_vline(TimuiCellBuffer *buf, int x, int y, int h, TimuiStyle st){
    int i;
    if(!buf || h <= 0) return;
    for(i = 0; i < h; i++) put_glyph(buf, x, y + i, 0x2502, st);
}
TIMUI_API void timui_draw_box(TimuiCellBuffer *buf, TimuiRect r, uint32_t border_flags, TimuiStyle st){
    uint32_t horiz, vert, tl, tr, bl, br;
    int i;
    if(!buf || r.w < 2 || r.h < 2) return;
    if(border_flags & TIMUI_BORDER_DOUBLE){ horiz=0x2550; vert=0x2551; tl=0x2554; tr=0x2557; bl=0x255A; br=0x255D; }
    else if(border_flags & TIMUI_BORDER_ASCII){ horiz='-'; vert='|'; tl='+'; tr='+'; bl='+'; br='+'; }
    else if(border_flags & TIMUI_BORDER_ROUND){ horiz=0x2500; vert=0x2502; tl=0x256D; tr=0x256E; bl=0x2570; br=0x256F; }
    else { horiz=0x2500; vert=0x2502; tl=0x250C; tr=0x2510; bl=0x2514; br=0x2518; }  /* single */
    put_glyph(buf, r.x,               r.y,               tl, st);
    put_glyph(buf, r.x + r.w - 1,     r.y,               tr, st);
    put_glyph(buf, r.x,               r.y + r.h - 1,     bl, st);
    put_glyph(buf, r.x + r.w - 1,     r.y + r.h - 1,     br, st);
    for(i = 1; i < r.w - 1; i++){
        put_glyph(buf, r.x + i, r.y,           horiz, st);
        put_glyph(buf, r.x + i, r.y + r.h - 1, horiz, st);
    }
    for(i = 1; i < r.h - 1; i++){
        put_glyph(buf, r.x,           r.y + i, vert, st);
        put_glyph(buf, r.x + r.w - 1, r.y + i, vert, st);
    }
}

/* ---- diff renderer ---------------------------------------------------- *
 * Emits the minimal terminal update for changed cells: cursor positioning
 * (CUP), truecolour SGR (only when the style changes), and the UTF-8 glyph.
 * No stdio: integers are formatted by hand. */
static int fmt_uint(char *buf, unsigned v){
    char tmp[16];
    int n = 0, i;
    if(v == 0){ buf[0] = '0'; return 1; }
    while(v){ tmp[n++] = (char)('0' + v % 10); v /= 10; }
    for(i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    return n;
}
static int utf8_encode(uint32_t cp, char *out){
    if(cp < 0x80){ out[0] = (char)cp; return 1; }
    if(cp < 0x800){ out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if(cp < 0x10000){
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}
static void r_emit(TimuiTransport *t, const char *s, size_t n){ if(t && t->write) (void)t->write(t, s, n); }
#define R_EMIT(t, lit) r_emit((t), (lit), sizeof(lit) - 1)
static void emit_truecolor(TimuiTransport *t, int bg, uint32_t rgb){
    char buf[40];
    int n = 0;
    buf[n++] = 0x1b; buf[n++] = '[';
    buf[n++] = (char)(bg ? '4' : '3'); buf[n++] = '8'; buf[n++] = ';'; buf[n++] = '2'; buf[n++] = ';';
    n += fmt_uint(buf + n, (rgb >> 16) & 0xff); buf[n++] = ';';
    n += fmt_uint(buf + n, (rgb >> 8) & 0xff);  buf[n++] = ';';
    n += fmt_uint(buf + n, rgb & 0xff);         buf[n++] = 'm';
    r_emit(t, buf, (size_t)n);
}
static void emit_cup(TimuiTransport *t, int x, int y){
    char buf[32];
    int n = 0;
    buf[n++] = 0x1b; buf[n++] = '[';
    n += fmt_uint(buf + n, (unsigned)(y + 1)); buf[n++] = ';';
    n += fmt_uint(buf + n, (unsigned)(x + 1)); buf[n++] = 'H';
    r_emit(t, buf, (size_t)n);
}
static void emit_sgr(TimuiTransport *t, TimuiRenderer *r, const TimuiCell *c){
    if((int)c->fg == r->last_fg && (int)c->bg == r->last_bg && (int)c->attrs == r->last_attrs) return;
    R_EMIT(t, "\x1b[0m");                 /* reset, then re-apply the full style */
    if(c->fg) emit_truecolor(t, 0, c->fg);
    if(c->bg) emit_truecolor(t, 1, c->bg);
    if(c->attrs & TIMUI_ATTR_BOLD)      R_EMIT(t, "\x1b[1m");
    if(c->attrs & TIMUI_ATTR_DIM)       R_EMIT(t, "\x1b[2m");
    if(c->attrs & TIMUI_ATTR_ITALIC)    R_EMIT(t, "\x1b[3m");
    if(c->attrs & TIMUI_ATTR_UNDERLINE) R_EMIT(t, "\x1b[4m");
    if(c->attrs & TIMUI_ATTR_BLINK)     R_EMIT(t, "\x1b[5m");
    if(c->attrs & TIMUI_ATTR_REVERSE)   R_EMIT(t, "\x1b[7m");
    if(c->attrs & TIMUI_ATTR_STRIKE)    R_EMIT(t, "\x1b[9m");
    r->last_fg = (int)c->fg;
    r->last_bg = (int)c->bg;
    r->last_attrs = (int)c->attrs;
}
TIMUI_API void timui_renderer_reset(TimuiRenderer *r){
    if(!r) return;
    r->last_x = -1; r->last_y = -1;
    r->last_fg = -1; r->last_bg = -1; r->last_attrs = -1;
}
TIMUI_API void timui_render_diff(TimuiTransport *t, const TimuiCellBuffer *prev,
                                 const TimuiCellBuffer *curr, TimuiRenderer *r){
    int x, y, w, h;
    if(!t || !prev || !curr || !r) return;
    w = prev->w < curr->w ? prev->w : curr->w;
    h = prev->h < curr->h ? prev->h : curr->h;
    for(y = 0; y < h; y++){
        for(x = 0; x < w; x++){
            const TimuiCell *pc = &prev->cells[(size_t)y * prev->w + x];
            const TimuiCell *cc = &curr->cells[(size_t)y * curr->w + x];
            char gb[4];
            int gn;
            if(pc->codepoint == cc->codepoint && pc->fg == cc->fg &&
               pc->bg == cc->bg && pc->attrs == cc->attrs) continue;
            if(r->last_x != x || r->last_y != y) emit_cup(t, x, y);
            emit_sgr(t, r, cc);
            gn = utf8_encode(cc->codepoint ? cc->codepoint : ' ', gb);
            r_emit(t, gb, (size_t)gn);
            r->last_x = x + 1;
            r->last_y = y;
        }
    }
    if(t->flush) t->flush(t);
}
TIMUI_API void timui_render_cursor(TimuiTransport *t, int x, int y, int visible){
    if(!t) return;
    if(visible){
        emit_cup(t, x, y);
        R_EMIT(t, "\x1b[?25h");
    } else {
        R_EMIT(t, "\x1b[?25l");
    }
}

/* ---- style/theme system ----------------------------------------------- */
static TimuiStyle th_mk(uint32_t fg, uint32_t bg){
    TimuiStyle s; s.fg = fg; s.bg = bg; s.attrs = 0; return s;
}
TIMUI_API TimuiTheme timui_theme_builtin(TimuiBuiltinTheme t){
    TimuiTheme th;
    int i;
    /* default (MONO): white-on-black */
    for(i = 0; i < TIMUI_SLOT_COUNT; i++){ th.slots[i].fg = 0xFFFFFF; th.slots[i].bg = 0x000000; th.slots[i].attrs = 0; }
    if(t == TIMUI_THEME_DOS_BLUE){
        uint32_t blue = 0x0000AA, white = 0xFFFFFF, cyan = 0x00FFFF, gray = 0xAAAAAA;
        th.slots[TIMUI_SLOT_TEXT]          = th_mk(white, blue);
        th.slots[TIMUI_SLOT_TEXT_DIM]      = th_mk(gray,  blue);
        th.slots[TIMUI_SLOT_PANEL]         = th_mk(white, blue);
        th.slots[TIMUI_SLOT_PANEL_TITLE]   = th_mk(cyan,  blue);
        th.slots[TIMUI_SLOT_BORDER]        = th_mk(white, blue);
        th.slots[TIMUI_SLOT_BUTTON]        = th_mk(0x000000, gray);
        th.slots[TIMUI_SLOT_BUTTON_HOVERED]= th_mk(0x000000, 0xDDDDDD);
        th.slots[TIMUI_SLOT_BUTTON_FOCUSED]= th_mk(white, 0x555555);
        th.slots[TIMUI_SLOT_BUTTON_ACTIVE] = th_mk(0x000000, white);
        th.slots[TIMUI_SLOT_INPUT]         = th_mk(white, 0x000000);
        th.slots[TIMUI_SLOT_INPUT_FOCUSED] = th_mk(white, 0x333333);
        th.slots[TIMUI_SLOT_SELECTION]     = th_mk(white, 0x5555FF);
        th.slots[TIMUI_SLOT_MENU]          = th_mk(white, blue);
        th.slots[TIMUI_SLOT_MENU_ACTIVE]   = th_mk(0x000000, gray);
        th.slots[TIMUI_SLOT_STATUS]        = th_mk(white, 0x000055);
        th.slots[TIMUI_SLOT_ERROR]         = th_mk(0xFF5555, blue);
        th.slots[TIMUI_SLOT_WARNING]       = th_mk(0xFFFF55, blue);
        th.slots[TIMUI_SLOT_SUCCESS]       = th_mk(0x55FF55, blue);
    } else if(t == TIMUI_THEME_DOS_GRAY){
        uint32_t gray = 0xAAAAAA, black = 0x000000, white = 0xFFFFFF;
        th.slots[TIMUI_SLOT_TEXT]          = th_mk(black, gray);
        th.slots[TIMUI_SLOT_TEXT_DIM]      = th_mk(0x555555, gray);
        th.slots[TIMUI_SLOT_PANEL]         = th_mk(black, gray);
        th.slots[TIMUI_SLOT_PANEL_TITLE]   = th_mk(white, 0x555555);
        th.slots[TIMUI_SLOT_BORDER]        = th_mk(black, gray);
        th.slots[TIMUI_SLOT_BUTTON]        = th_mk(black, white);
        th.slots[TIMUI_SLOT_BUTTON_HOVERED]= th_mk(black, 0xDDDDDD);
        th.slots[TIMUI_SLOT_BUTTON_FOCUSED]= th_mk(white, 0x555555);
        th.slots[TIMUI_SLOT_BUTTON_ACTIVE] = th_mk(black, 0xFFFFFF);
        th.slots[TIMUI_SLOT_INPUT]         = th_mk(black, white);
        th.slots[TIMUI_SLOT_INPUT_FOCUSED] = th_mk(white, 0x555555);
        th.slots[TIMUI_SLOT_STATUS]        = th_mk(white, 0x555555);
        th.slots[TIMUI_SLOT_ERROR]         = th_mk(white, 0xAA0000);
        th.slots[TIMUI_SLOT_WARNING]       = th_mk(black, 0xAAAA00);
        th.slots[TIMUI_SLOT_SUCCESS]       = th_mk(black, 0x00AA00);
    } else if(t == TIMUI_THEME_MODERN_DARK){
        uint32_t bg = 0x1E1E2E, fg = 0xCDD6F4, accent = 0x89B4FA;
        for(i = 0; i < TIMUI_SLOT_COUNT; i++){ th.slots[i].fg = fg; th.slots[i].bg = bg; }
        th.slots[TIMUI_SLOT_PANEL_TITLE]   = th_mk(accent, bg);
        th.slots[TIMUI_SLOT_BORDER]        = th_mk(0x585B70, bg);
        th.slots[TIMUI_SLOT_BUTTON]        = th_mk(fg, 0x313244);
        th.slots[TIMUI_SLOT_BUTTON_HOVERED]= th_mk(fg, 0x45475A);
        th.slots[TIMUI_SLOT_BUTTON_FOCUSED]= th_mk(0x1E1E2E, accent);
        th.slots[TIMUI_SLOT_INPUT]         = th_mk(fg, 0x313244);
        th.slots[TIMUI_SLOT_INPUT_FOCUSED] = th_mk(fg, 0x45475A);
        th.slots[TIMUI_SLOT_SELECTION]     = th_mk(0x1E1E2E, accent);
        th.slots[TIMUI_SLOT_ERROR]         = th_mk(0xF38BA8, bg);
        th.slots[TIMUI_SLOT_WARNING]       = th_mk(0xFAB387, bg);
        th.slots[TIMUI_SLOT_SUCCESS]       = th_mk(0xA6E3A1, bg);
    }
    /* TIMUI_THEME_MONO: the white-on-black default set above */
    return th;
}
TIMUI_API TimuiStyle timui_theme_style(const TimuiTheme *th, TimuiStyleSlot slot){
    if(!th || slot < 0 || slot >= TIMUI_SLOT_COUNT){
        TimuiStyle z = {0, 0, 0};
        return z;
    }
    return th->slots[slot];
}

