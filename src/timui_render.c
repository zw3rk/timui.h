/* ---- cell buffer ------------------------------------------------------ */
TIMUI_API TimuiResult timui_cells_init(TimuiCellBuffer *buf, int w, int h, const TimuiAllocator *alloc){
    size_t n;
    if(!buf) return TIMUI_ERR_INVALID_ARGUMENT;
    memset(buf, 0, sizeof *buf);
    if(w <= 0 || h <= 0 || !timui_allocator_valid_(alloc)) return TIMUI_ERR_INVALID_ARGUMENT;
    if((size_t)w > SIZE_MAX / (size_t)h) return TIMUI_ERR_OUT_OF_MEMORY;     /* w*h overflow */
    n = (size_t)w * (size_t)h;
    if(n > SIZE_MAX / sizeof(TimuiCell)) return TIMUI_ERR_OUT_OF_MEMORY;     /* n*sizeof overflow */
    buf->w = w;
    buf->h = h;
    buf->alloc = *alloc;
    buf->has_clip = 0;
    buf->links = NULL;
    buf->link_count = 0;
    buf->link_cap = 0;
    buf->cells = (TimuiCell *)alloc->alloc(alloc->userdata, n * sizeof(TimuiCell));
    if(!buf->cells){ buf->w = buf->h = 0; return TIMUI_ERR_OUT_OF_MEMORY; }
    timui_cells_clear(buf);
    return TIMUI_OK;
}
TIMUI_API void timui_cells_destroy(TimuiCellBuffer *buf){
    size_t n;
    if(!buf || !buf->cells) return;
    n = (size_t)buf->w * (size_t)buf->h;
    if(buf->links){ buf->alloc.free(buf->alloc.userdata, buf->links, (size_t)buf->link_cap * sizeof(*buf->links)); buf->links = NULL; }
    buf->alloc.free(buf->alloc.userdata, buf->cells, n * sizeof(TimuiCell));
    buf->cells = NULL;
    buf->w = buf->h = 0;
}
TIMUI_API TimuiResult timui_cells_resize(TimuiCellBuffer *buf, int w, int h, const TimuiAllocator *alloc){
    size_t n, oldn;
    TimuiCell *nc;
    if(!buf || w <= 0 || h <= 0) return TIMUI_ERR_INVALID_ARGUMENT;
    if(!buf->cells){
        if(!timui_allocator_valid_(alloc)) return TIMUI_ERR_INVALID_ARGUMENT;
        buf->alloc = *alloc;
    }
    if((size_t)w > SIZE_MAX / (size_t)h) return TIMUI_ERR_OUT_OF_MEMORY;     /* w*h overflow */
    n = (size_t)w * (size_t)h;
    if(n > SIZE_MAX / sizeof(TimuiCell)) return TIMUI_ERR_OUT_OF_MEMORY;     /* n*sizeof overflow */
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
    size_t i, n;
    if(!buf || !buf->cells) return;
    n = (size_t)buf->w * (size_t)buf->h;
    memset(buf->cells, 0, n * sizeof(TimuiCell));   /* codepoint/attrs/width/flags/links = 0 */
    for(i = 0; i < n; i++){                          /* empty = DEFAULT colour (ADR 0001), not black */
        buf->cells[i].fg = TIMUI_COLOR_DEFAULT;
        buf->cells[i].bg = TIMUI_COLOR_DEFAULT;
    }
    buf->link_count = 0;   /* hyperlinks are per-frame */
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
    if(p[0] >= 0xC2 && (p[0] & 0xE0) == 0xC0){ cp = (uint32_t)(p[0] & 0x1F); need = 1; }
    else if((p[0] & 0xF0) == 0xE0){ cp = (uint32_t)(p[0] & 0x0F); need = 2; }
    else if(p[0] <= 0xF4 && (p[0] & 0xF8) == 0xF0){ cp = (uint32_t)(p[0] & 0x07); need = 3; }
    else { if(out_cp) *out_cp = 0xFFFD; return 1; }            /* invalid lead */
    if((int)len < 1 + need){ return 0; }                       /* incomplete */
    for(i = 1; i <= need; i++){
        if((p[i] & 0xC0) != 0x80){ if(out_cp) *out_cp = 0xFFFD; return 1; }   /* bad continuation */
        cp = (cp << 6) | (uint32_t)(p[i] & 0x3F);
    }
    if(out_cp) *out_cp = cp;
    /* reject overlong encodings and surrogate halves */
    if(need == 1 && cp < 0x80){ if(out_cp) *out_cp = 0xFFFD; return 1; }
    if(need == 2 && cp < 0x800){ if(out_cp) *out_cp = 0xFFFD; return 1; }
    if(need == 3 && cp < 0x10000){ if(out_cp) *out_cp = 0xFFFD; return 1; }
    if(cp >= 0xD800 && cp <= 0xDFFF){ if(out_cp) *out_cp = 0xFFFD; return 1; }
    if(cp > 0x10FFFF){ if(out_cp) *out_cp = 0xFFFD; return 1; }   /* above Unicode max */
    return 1 + need;
}
TIMUI_API int timui_utf8_width(uint32_t cp){
    if(cp < 0x20 || cp == 0x7F) return 0;                       /* control */
    if(cp >= 0x80 && cp <= 0x9F) return 0;                       /* C1 control */
    if(cp == 0xFFFD) return 1;
    if((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
       (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
       (cp >= 0xFE20 && cp <= 0xFE2F)) return 0;                /* combining */
    if(cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F) ||
       (cp >= 0xE0100 && cp <= 0xE01EF) ||
       (cp >= 0x1F3FB && cp <= 0x1F3FF)) return 0;              /* joiner / variation / emoji modifier */
    if((cp >= 0x1100 && cp <= 0x115F) ||
       (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7A3) ||
       (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE6F) ||
       (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6) ||
       (cp >= 0x1F300 && cp <= 0x1FAFF)) return 2;              /* wide/fullwidth */
    return 1;
}

/* ---- drawing primitives ----------------------------------------------- */
static void clear_cell_default_(TimuiCellBuffer *buf, int x, int y){
    TimuiCell c;
    if(!timui_cells_get(buf, x, y)) return;
    memset(&c, 0, sizeof c);
    c.fg = TIMUI_COLOR_DEFAULT;
    c.bg = TIMUI_COLOR_DEFAULT;
    timui_cells_put(buf, x, y, &c);
}

static void clear_wide_pair_touching_(TimuiCellBuffer *buf, int x, int y){
    TimuiCell *c = timui_cells_get(buf, x, y);
    if(!c) return;
    if(c->flags & TIMUI_CELL_CONTINUATION){
        if(x > 0){
            TimuiCell *lead = timui_cells_get(buf, x - 1, y);
            if(lead && lead->width >= 2) clear_cell_default_(buf, x - 1, y);
        }
        clear_cell_default_(buf, x, y);
    } else if(c->width >= 2){
        clear_cell_default_(buf, x, y);
        clear_cell_default_(buf, x + 1, y);
    }
}

/* Z7: the single glyph-emit primitive. Writes cp at (x,y) with style st and an
 * optional hyperlink id, and blanks the continuation cell for a wide glyph.
 * This is the one place the subtle wide-glyph continuation logic lives (the
 * site of the prior V6/X2 bugs) — draw_text, draw_text_linked, and the box/
 * fill/line primitives all route through it. */
static void put_glyph_link(TimuiCellBuffer *buf, int x, int y, uint32_t cp, TimuiStyle st, uint32_t link){
    TimuiCell c;
    int w;
    if(!buf || !buf->cells || x < 0 || y < 0 || x >= buf->w || y >= buf->h) return;
    if(buf->has_clip && (x < buf->clip.x || y < buf->clip.y ||
       x >= buf->clip.x + buf->clip.w || y >= buf->clip.y + buf->clip.h)) return;
    memset(&c, 0, sizeof c);
    w = timui_utf8_width(cp);
    if(w > 1 && (x + 1 >= buf->w ||
       (buf->has_clip && (x + 1 < buf->clip.x || x + 1 >= buf->clip.x + buf->clip.w)))) return;
    clear_wide_pair_touching_(buf, x, y);
    if(w > 1) clear_wide_pair_touching_(buf, x + 1, y);
    c.codepoint = cp;
    c.fg = st.fg;
    c.bg = st.bg;
    c.attrs = st.attrs;
    c.width = (uint16_t)(w > 1 ? 2 : 1);
    c.hyperlink_id = link;
    timui_cells_put(buf, x, y, &c);
    /* wide glyph: blank the continuation cell so stale content isn't left behind */
    if(w > 1){
        memset(&c, 0, sizeof c);
        c.fg = TIMUI_COLOR_DEFAULT;   /* ADR 0001: blanked = default, not black */
        c.bg = TIMUI_COLOR_DEFAULT;
        c.flags = TIMUI_CELL_CONTINUATION;
        c.width = 0;
        timui_cells_put(buf, x + 1, y, &c);
    }
}
/* Unlinked convenience for the drawing primitives (box/fill/lines). */
static void put_glyph(TimuiCellBuffer *buf, int x, int y, uint32_t cp, TimuiStyle st){
    put_glyph_link(buf, x, y, cp, st, 0);
}
TIMUI_API TimuiStyle timui_style_make(uint32_t fg, uint32_t bg, uint32_t attrs){
    TimuiStyle s;
    s.fg = fg;
    s.bg = bg;
    s.attrs = attrs;
    return s;
}
TIMUI_API void timui_draw_text(TimuiCellBuffer *buf, int x, int y, TimuiStr text, TimuiStyle st){
    timui_draw_text_linked(buf, x, y, text, st, 0);   /* Z7: unlinked == linked with id 0 */
}
TIMUI_API uint32_t timui_hyperlink_set(TimuiCellBuffer *buf, const char *uri){
    size_t n;
    if(!buf || !uri) return 0;
    if(buf->link_count >= buf->link_cap){
        int nc = buf->link_cap ? buf->link_cap * 2 : 8;
        TimuiHyperlink *nl = (TimuiHyperlink *)buf->alloc.realloc(buf->alloc.userdata,
            buf->links, (size_t)buf->link_cap * sizeof(*nl), (size_t)nc * sizeof(*nl));
        if(!nl) return 0;
        buf->links = nl;
        buf->link_cap = nc;
    }
    n = strlen(uri);
    if(n >= sizeof(buf->links[0].uri)) n = sizeof(buf->links[0].uri) - 1;
    memcpy(buf->links[buf->link_count].uri, uri, n);
    buf->links[buf->link_count].uri[n] = '\0';
    buf->link_count++;
    return (uint32_t)buf->link_count;   /* 1-based id */
}
TIMUI_API void timui_draw_text_linked(TimuiCellBuffer *buf, int x, int y, TimuiStr text, TimuiStyle st, uint32_t link){
    size_t i = 0;
    int cx = x;
    if(!buf || !text.ptr) return;
    while(i < text.len){
        uint32_t cp = 0;
        int adv = timui_utf8_decode(text.ptr + i, text.len - i, &cp);
        int w;
        if(adv <= 0){ cp = 0xFFFD; adv = 1; }
        w = timui_utf8_width(cp);
        if(w > 0){
            put_glyph_link(buf, cx, y, cp, st, link);
            cx += w;
        }
        i += (size_t)adv;
    }
}
/* Clamp a rect to the buffer bounds (and non-negative). Prevents a runaway
 * loop and signed-overflow UB (r.y+r.h) on an extreme rect; put_glyph already
 * bounds-checks each cell, so this is about loop bounds + UB, not write safety. */
static TimuiRect rect_clamp_buf(const TimuiCellBuffer *buf, TimuiRect r){
    TimuiRect out = {0, 0, 0, 0};
    int64_t x1, y1, x2, y2;
    if(!buf || r.w <= 0 || r.h <= 0) return out;
    x1 = r.x; y1 = r.y;
    x2 = (int64_t)r.x + (int64_t)r.w;
    y2 = (int64_t)r.y + (int64_t)r.h;
    if(x2 <= 0 || y2 <= 0 || x1 >= buf->w || y1 >= buf->h) return out;
    if(x1 < 0) x1 = 0;
    if(y1 < 0) y1 = 0;
    if(x2 > buf->w) x2 = buf->w;
    if(y2 > buf->h) y2 = buf->h;
    if(x2 <= x1 || y2 <= y1) return out;
    out.x = (int)x1;
    out.y = (int)y1;
    out.w = (int)(x2 - x1);
    out.h = (int)(y2 - y1);
    return out;
}
TIMUI_API void timui_draw_fill(TimuiCellBuffer *buf, TimuiRect r, TimuiStyle st){
    int xi, yi;
    if(!buf) return;
    r = rect_clamp_buf(buf, r);
    for(yi = r.y; yi < r.y + r.h; yi++)
        for(xi = r.x; xi < r.x + r.w; xi++)
            put_glyph(buf, xi, yi, ' ', st);
}
TIMUI_API void timui_draw_hline(TimuiCellBuffer *buf, int x, int y, int w, TimuiStyle st){
    int i;
    if(!buf || w <= 0) return;
    /* Z9: clamp the run to the buffer so an extreme width can't overflow x+i or
     * spin the loop ~INT_MAX times (put_glyph already bounds-checks each cell,
     * so this is about loop bounds + UB, not write safety — same as draw_fill). */
    if(x < 0){ w += x; x = 0; }
    if(x >= buf->w || w <= 0) return;
    if(w > buf->w - x) w = buf->w - x;
    for(i = 0; i < w; i++) put_glyph(buf, x + i, y, 0x2500, st);
}
TIMUI_API void timui_draw_vline(TimuiCellBuffer *buf, int x, int y, int h, TimuiStyle st){
    int i;
    if(!buf || h <= 0) return;
    if(y < 0){ h += y; y = 0; }                 /* Z9: clamp — see draw_hline */
    if(y >= buf->h || h <= 0) return;
    if(h > buf->h - y) h = buf->h - y;
    for(i = 0; i < h; i++) put_glyph(buf, x, y + i, 0x2502, st);
}
TIMUI_API void timui_draw_box(TimuiCellBuffer *buf, TimuiRect r, uint32_t border_flags, TimuiStyle st){
    uint32_t horiz, vert, tl, tr, bl, br;
    int i;
    if(!buf) return;
    /* Z9: clamp the rect to the buffer first, mirroring draw_fill — kills the
     * r.x+r.w-1 signed-overflow UB and the ~INT_MAX edge loops on extreme
     * geometry; the box is clipped to the viewport (borders at the edge). */
    r = rect_clamp_buf(buf, r);
    if(r.w < 2 || r.h < 2) return;
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
static void r_emit(TimuiTransport *t, const char *s, size_t n){ if(t && t->write) (void)t->write(t, s, n); }
#define R_EMIT(t, lit) r_emit((t), (lit), sizeof(lit) - 1)
static void emit_osc_string_sanitized(TimuiTransport *t, const char *s){
    size_t i = 0, len;
    if(!s) return;
    len = strlen(s);
    while(i < len){
        uint32_t cp = 0;
        int adv = timui_utf8_decode(s + i, len - i, &cp);
        char out[4];
        int n;
        if(adv <= 0){ cp = 0xFFFDu; adv = 1; }
        if(cp < 0x20u || cp == 0x7Fu || (cp >= 0x80u && cp <= 0x9Fu)){
            i += (size_t)adv;
            continue;
        }
        n = timui_utf8_encode_(cp, out);
        if(n > 0) r_emit(t, out, (size_t)n);
        i += (size_t)adv;
    }
}
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
    if(c->fg != TIMUI_COLOR_DEFAULT) emit_truecolor(t, 0, c->fg);   /* ADR 0001: 0x000000 is black */
    if(c->bg != TIMUI_COLOR_DEFAULT) emit_truecolor(t, 1, c->bg);
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
/* OSC 8 hyperlink: ESC]8;;<uri>ESC\\ to open, ESC]8;;ESC\\ to close. */
static void emit_osc8(TimuiTransport *t, const char *uri){
    R_EMIT(t, "\x1b]8;;");
    if(uri) emit_osc_string_sanitized(t, uri);
    R_EMIT(t, "\x1b\\");
}
TIMUI_API void timui_renderer_reset(TimuiRenderer *r){
    if(!r) return;
    r->last_x = -1; r->last_y = -1;
    r->last_fg = -1; r->last_bg = -1; r->last_attrs = -1;
    r->last_link = 0;
    r->have_last_link = 0;
    r->last_link_uri[0] = '\0';
}
/* Resolve a cell's hyperlink id to its URI in the buffer (NULL if none / OOR).
 * ids are per-frame indices (cells_clear resets link_count each frame), so the
 * renderer compares URIs — not ids — to catch a same-id-different-URI change. */
static const char *cell_link_uri(const TimuiCellBuffer *buf, uint32_t id){
    return (id && buf->links && id <= (uint32_t)buf->link_count) ? buf->links[id - 1].uri : NULL;
}
static int uri_eq(const char *a, const char *b){
    if(a == b) return 1;
    if(!a || !b) return 0;
    return strcmp(a, b) == 0;
}
static uint32_t render_safe_cp(uint32_t cp){
    if(cp == 0) return ' ';
    if(cp < 0x20u || cp == 0x7Fu || (cp >= 0x80u && cp <= 0x9Fu)) return ' ';
    if(cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) return 0xFFFDu;
    return cp;
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
            /* A wide glyph's continuation cell is owned by its lead cell
             * (which carries width 2 and advances the cursor two columns).
             * Never emit it — line 336 would render codepoint 0 as a space,
             * clobbering the right half of a just-drawn wide glyph. */
            if(cc->flags & TIMUI_CELL_CONTINUATION) continue;
            if(pc->codepoint == cc->codepoint && pc->fg == cc->fg &&
               pc->bg == cc->bg && pc->attrs == cc->attrs &&
               pc->width == cc->width &&
               (pc->flags & TIMUI_CELL_CONTINUATION) == (cc->flags & TIMUI_CELL_CONTINUATION) &&
               uri_eq(cell_link_uri(prev, pc->hyperlink_id), cell_link_uri(curr, cc->hyperlink_id))) continue;
            if(r->last_x != x || r->last_y != y) emit_cup(t, x, y);
            emit_sgr(t, r, cc);
            {   /* OSC 8: emit on a link-state change (open/close) or a URI
                 * change. ids are per-frame, so cache the open URI string and
                 * track whether a link is currently open (have_last_link). */
                const char *curi = cell_link_uri(curr, cc->hyperlink_id);
                int want = curi != NULL;
                if(want != r->have_last_link || (want && !uri_eq(curi, r->last_link_uri))){
                    emit_osc8(t, curi);
                    r->last_link = (int)cc->hyperlink_id;
                    r->have_last_link = want;
                    if(curi){
                        size_t ul = strlen(curi);
                        if(ul >= sizeof r->last_link_uri) ul = sizeof r->last_link_uri - 1;
                        memcpy(r->last_link_uri, curi, ul);
                        r->last_link_uri[ul] = '\0';
                    }else r->last_link_uri[0] = '\0';
                }
            }
            gn = timui_utf8_encode_(render_safe_cp(cc->codepoint), gb);
            r_emit(t, gb, (size_t)gn);
            r->last_x = x + (cc->width >= 2 ? 2 : 1);   /* wide glyph advances cursor by 2 */
            r->last_y = y;
        }
    }
    if(r->have_last_link){
        emit_osc8(t, NULL);
        r->last_link = 0;
        r->have_last_link = 0;
        r->last_link_uri[0] = '\0';
    }
    if(t->flush) t->flush(t);
}
TIMUI_API void timui_render_cursor(TimuiTransport *t, int x, int y, int visible){
    if(!t) return;
    if(visible){
        if(x >= 0 && y >= 0 && x < INT_MAX && y < INT_MAX) emit_cup(t, x, y);
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
        th.slots[TIMUI_SLOT_BUTTON_ACTIVE] = th_mk(white, black);   /* G13: pressed = inverted (was == BUTTON) */
        th.slots[TIMUI_SLOT_INPUT]         = th_mk(black, white);
        th.slots[TIMUI_SLOT_INPUT_FOCUSED] = th_mk(white, 0x555555);
        th.slots[TIMUI_SLOT_STATUS]        = th_mk(white, 0x555555);
        th.slots[TIMUI_SLOT_ERROR]         = th_mk(white, 0xAA0000);
        th.slots[TIMUI_SLOT_WARNING]       = th_mk(black, 0xAAAA00);
        th.slots[TIMUI_SLOT_SUCCESS]       = th_mk(black, 0x00AA00);
        th.slots[TIMUI_SLOT_SELECTION]     = th_mk(white, 0x555555);
        th.slots[TIMUI_SLOT_MENU]          = th_mk(black, 0xCCCCCC);
        th.slots[TIMUI_SLOT_MENU_ACTIVE]   = th_mk(black, white);
    } else if(t == TIMUI_THEME_MODERN_DARK){
        uint32_t bg = 0x1E1E2E, fg = 0xCDD6F4, accent = 0x89B4FA;
        for(i = 0; i < TIMUI_SLOT_COUNT; i++){ th.slots[i].fg = fg; th.slots[i].bg = bg; }
        th.slots[TIMUI_SLOT_TEXT_DIM]      = th_mk(0x9399B2, bg);        /* G13: dimmer than body */
        th.slots[TIMUI_SLOT_PANEL_TITLE]   = th_mk(accent, bg);
        th.slots[TIMUI_SLOT_BORDER]        = th_mk(0x585B70, bg);
        th.slots[TIMUI_SLOT_BUTTON]        = th_mk(fg, 0x313244);
        th.slots[TIMUI_SLOT_BUTTON_HOVERED]= th_mk(fg, 0x45475A);
        th.slots[TIMUI_SLOT_BUTTON_FOCUSED]= th_mk(0x1E1E2E, accent);
        th.slots[TIMUI_SLOT_BUTTON_ACTIVE] = th_mk(0x1E1E2E, 0xB4BEFE);  /* G13: brighter than focus */
        th.slots[TIMUI_SLOT_INPUT]         = th_mk(fg, 0x313244);
        th.slots[TIMUI_SLOT_INPUT_FOCUSED] = th_mk(fg, 0x45475A);
        th.slots[TIMUI_SLOT_SELECTION]     = th_mk(0x1E1E2E, accent);
        th.slots[TIMUI_SLOT_MENU]          = th_mk(fg, 0x313244);        /* G13: menu-bar surface */
        th.slots[TIMUI_SLOT_MENU_ACTIVE]   = th_mk(0x1E1E2E, accent);    /* G13: highlighted item */
        th.slots[TIMUI_SLOT_STATUS]        = th_mk(fg, 0x45475A);        /* G13: status-bar surface */
        th.slots[TIMUI_SLOT_ERROR]         = th_mk(0xF38BA8, bg);
        th.slots[TIMUI_SLOT_WARNING]       = th_mk(0xFAB387, bg);
        th.slots[TIMUI_SLOT_SUCCESS]       = th_mk(0xA6E3A1, bg);
    }
    else if(t == TIMUI_THEME_MODERN_LIGHT){
        uint32_t bg = 0xFFFFFF, fg = 0x2E2E2E, accent = 0x0066CC;
        for(i = 0; i < TIMUI_SLOT_COUNT; i++){ th.slots[i].fg = fg; th.slots[i].bg = bg; }
        th.slots[TIMUI_SLOT_TEXT_DIM]      = th_mk(0x777777, bg);        /* G13: dimmer than body */
        th.slots[TIMUI_SLOT_PANEL_TITLE]   = th_mk(accent, bg);
        th.slots[TIMUI_SLOT_BORDER]        = th_mk(0xCCCCCC, bg);
        th.slots[TIMUI_SLOT_BUTTON]        = th_mk(fg, 0xE0E0E0);
        th.slots[TIMUI_SLOT_BUTTON_HOVERED]= th_mk(fg, 0xD0D0D0);
        th.slots[TIMUI_SLOT_BUTTON_FOCUSED]= th_mk(0xFFFFFF, accent);
        th.slots[TIMUI_SLOT_BUTTON_ACTIVE] = th_mk(0xFFFFFF, 0x004C99);  /* G13: darker than focus */
        th.slots[TIMUI_SLOT_INPUT]         = th_mk(fg, 0xF0F0F0);
        th.slots[TIMUI_SLOT_INPUT_FOCUSED] = th_mk(fg, 0xE0E0E0);
        th.slots[TIMUI_SLOT_SELECTION]     = th_mk(0xFFFFFF, accent);
        th.slots[TIMUI_SLOT_MENU]          = th_mk(fg, 0xF0F0F0);        /* G13: menu-bar surface */
        th.slots[TIMUI_SLOT_MENU_ACTIVE]   = th_mk(0xFFFFFF, accent);    /* G13: highlighted item */
        th.slots[TIMUI_SLOT_STATUS]        = th_mk(fg, 0xE0E0E0);        /* G13: status-bar surface */
        th.slots[TIMUI_SLOT_ERROR]         = th_mk(0xCC0000, bg);
        th.slots[TIMUI_SLOT_WARNING]       = th_mk(0xCC6600, bg);
        th.slots[TIMUI_SLOT_SUCCESS]       = th_mk(0x008800, bg);
    }
    else if(t == TIMUI_THEME_MONO){
        /* Monochrome: no colour to spend, so interactive state is conveyed with
         * SGR attributes (dim/bold/reverse) — the idiomatic mono-terminal cue,
         * emitted by emit_sgr. G13: every state slot stays visually distinct
         * from its resting base atop the white-on-black default set above. */
        th.slots[TIMUI_SLOT_TEXT_DIM].attrs       = TIMUI_ATTR_DIM;
        th.slots[TIMUI_SLOT_PANEL_TITLE].attrs    = TIMUI_ATTR_BOLD;
        th.slots[TIMUI_SLOT_BUTTON_FOCUSED].attrs = TIMUI_ATTR_BOLD;
        th.slots[TIMUI_SLOT_BUTTON_ACTIVE].attrs  = TIMUI_ATTR_REVERSE;
        th.slots[TIMUI_SLOT_SELECTION].attrs      = TIMUI_ATTR_REVERSE;
        th.slots[TIMUI_SLOT_MENU_ACTIVE].attrs    = TIMUI_ATTR_REVERSE;
        th.slots[TIMUI_SLOT_STATUS].attrs         = TIMUI_ATTR_REVERSE;
    }
    /* An out-of-range enum falls through to the white-on-black default above. */
    return th;
}
TIMUI_API TimuiStyle timui_theme_style(const TimuiTheme *th, TimuiStyleSlot slot){
    if(!th || slot < 0 || slot >= TIMUI_SLOT_COUNT){
        TimuiStyle z = {0, 0, 0};
        return z;
    }
    return th->slots[slot];
}
#undef R_EMIT   /* Z10: impl-only macro must not leak into the consumer TU */
