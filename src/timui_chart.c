/* ---- chart / indicator widgets (W3) ----------------------------------- *
 * Pure UI over caller-supplied values — there is NO DSP here (spectra, FFTs,
 * metering all live off the UI thread; see examples/radio.c). This section
 * promotes the reusable *look and feel* from that example into first-class
 * widgets: the vertical gradient bar with a floating peak-hold cap, plus
 * compact sparklines, horizontal gauge/meter/progress bars, and a spinner.
 *
 * Everything routes through the existing drawing primitives (timui_draw_fill /
 * timui_draw_text) so clipping, wide-glyph handling, and the diff renderer all
 * apply unchanged. The pure helpers (lerp / bar_cells / peak_hold /
 * spinner_glyph) are side-effect-free and unit-tested without a frame.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd. */

/* ============================ pure helpers ============================== */

/* timui_lerp_rgb is the shared colour-interpolation helper defined in
 * src/timui_box.c (included just before this section); the barchart gradient
 * uses it. It ROUNDS each channel (nearest), so a midpoint may differ by 1/255
 * from the radio example's original truncating gradient — visually identical. */

/* fraction of `value` relative to `max` (or the value itself when max<=0),
 * clamped to [0,1]. Shared by bar_cells and the widgets. */
static float timui_frac_(float value, float max){
    float frac = (max > 0.0f) ? value / max : value;
    if(frac < 0.0f) frac = 0.0f;
    if(frac > 1.0f) frac = 1.0f;
    return frac;
}

/* Number of filled cells for `value` over a `size`-cell track: the clamped
 * fraction rounded to the nearest whole cell. value>max fills the whole track;
 * value<0 fills none; size<=0 yields 0. */
TIMUI_API int timui_bar_cells(float value, float max, int size){
    int n;
    if(size <= 0) return 0;
    n = (int)(timui_frac_(value, max) * (float)size + 0.5f);   /* round to nearest */
    if(n < 0) n = 0;
    if(n > size) n = size;
    return n;
}

/* Peak-hold envelope for one cap: rises INSTANTLY to a higher `value`, else
 * releases LINEARLY by `decay` per call, never falling below the live `value`
 * (and, since levels are non-negative, never below 0). Feeding it once per
 * frame makes a floating cap chase peaks up and ease back down. */
TIMUI_API float timui_peak_hold(float cap, float value, float decay){
    float c;
    if(value >= cap) return value;          /* instant attack */
    c = cap - decay;                        /* linear release */
    return c < value ? value : c;
}

/* Codepoint of the braille "dots" throbber frame for `tick`. Ten frames; a
 * negative tick wraps (so a monotonically increasing OR decreasing counter both
 * animate). Cycle: ⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏. */
TIMUI_API uint32_t timui_spinner_glyph(int tick){
    static const uint32_t FRAMES[10] = {
        0x280Bu, 0x2819u, 0x2839u, 0x2838u, 0x283Cu,
        0x2834u, 0x2826u, 0x2827u, 0x2807u, 0x280Fu
    };
    int i = tick % 10;
    if(i < 0) i += 10;
    return FRAMES[i];
}

/* ============================ draw helpers ============================== */

/* Fill a single cell's background with `col` (a space glyph whose fg==bg reads
 * as a solid block). The one place the widgets paint a coloured cell. */
static void timui_cell_bg_(TimuiCellBuffer *buf, int x, int y, uint32_t col){
    timui_draw_fill(buf, TIMUI_RECT(x, y, 1, 1), timui_style_make(col, col, 0));
}

/* Format an unsigned integer into `out` (no stdio in the library). Returns the
 * digit count. */
static int timui_fmt_uint_(char *out, unsigned v){
    char tmp[12];
    int n = 0, i;
    if(v == 0){ out[0] = '0'; return 1; }
    while(v){ tmp[n++] = (char)('0' + (int)(v % 10)); v /= 10; }
    for(i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    return n;
}

/* "NN%" for a 0..1 fraction. Returns the byte count. */
static int timui_fmt_pct_(char *out, float frac){
    int n;
    if(frac < 0.0f) frac = 0.0f;
    if(frac > 1.0f) frac = 1.0f;
    n = timui_fmt_uint_(out, (unsigned)(int)(frac * 100.0f + 0.5f));
    out[n++] = '%';
    return n;
}

/* "W.FF" for a level (clamped to [0, 9.99]). Returns the byte count. */
static int timui_fmt_frac2_(char *out, float v){
    int whole, frac2, n;
    if(v < 0.0f) v = 0.0f;
    if(v > 9.99f) v = 9.99f;
    whole = (int)v;
    frac2 = (int)((v - (float)whole) * 100.0f + 0.5f);
    if(frac2 > 99) frac2 = 99;
    n = timui_fmt_uint_(out, (unsigned)whole);
    out[n++] = '.';
    out[n++] = (char)('0' + frac2 / 10);
    out[n++] = (char)('0' + frac2 % 10);
    return n;
}

/* ============================== widgets ================================= */

TIMUI_API void timui_barchart(TimuiFrame *f, TimuiRect r, const float *vals, int n,
                              TimuiBarOpts opts, TimuiBarState *st){
    TimuiCellBuffer *buf;
    int gap = opts.gap > 0 ? opts.gap : 1;
    int barH, bw, x0, b, use_labels;
    if(!f || !vals || n <= 0 || r.w <= 0 || r.h <= 0) return;
    buf = timui_frame_buffer(f);
    if(n > TIMUI_BAR_MAX) n = TIMUI_BAR_MAX;             /* cap to the state array */
    use_labels = (opts.labels != NULL) && (r.h >= 2);
    barH = use_labels ? r.h - 1 : r.h;                  /* reserve a row for labels */
    if(barH < 1) barH = 1;
    bw = (r.w - (n - 1) * gap) / n;                     /* even split, gaps between */
    if(bw < 1){ bw = 1; gap = 0; }                      /* too tight: drop gaps */
    x0 = r.x + (r.w - (bw * n + gap * (n - 1))) / 2;    /* centre the group */
    if(x0 < r.x) x0 = r.x;
    if(st) st->n = n;
    for(b = 0; b < n; b++){
        int bx = x0 + b * (bw + gap), row;
        float frac = timui_frac_(vals[b], opts.max);
        int filled = timui_bar_cells(vals[b], opts.max, barH), capr = -1;
        /* peak-hold cap (caller-owned state): rises instantly, decays per call */
        if(st && opts.peak_decay > 0.0f){
            st->caps[b] = timui_peak_hold(st->caps[b], frac, opts.peak_decay);
            capr = (int)(st->caps[b] * (float)barH + 0.5f);
            if(capr > barH) capr = barH;
        }
        for(row = 0; row < barH; row++){                /* row 0 == bottom cell */
            int y = r.y + barH - 1 - row;
            float t = (float)row / (float)(barH > 1 ? barH - 1 : 1);
            uint32_t base = timui_lerp_rgb(opts.lo, opts.hi, t), col;
            if(row < filled)          col = base;                         /* fill */
            else if(row + 1 == capr)  col = timui_lerp_rgb(base, 0xFFFFFFu, opts.cap_light);
            else                      col = opts.track;                   /* empty */
            timui_draw_fill(buf, TIMUI_RECT(bx, y, bw, 1), timui_style_make(col, col, 0));
        }
        if(use_labels && opts.labels[b]){               /* centred label on the last row */
            TimuiStr lbl = timui_str_from_cstr(opts.labels[b]);
            int lx = bx + (bw - (int)lbl.len) / 2;
            if(lx < r.x) lx = bx;
            timui_draw_text(buf, lx, r.y + r.h - 1, lbl, timui_style_make(opts.hi, opts.track, 0));
        }
    }
}

TIMUI_API void timui_sparkline(TimuiFrame *f, TimuiRect r, const float *history, int n,
                               TimuiStyle style){
    TimuiCellBuffer *buf;
    int cols, off, i;
    if(!f || !history || n <= 0 || r.w <= 0 || r.h <= 0) return;
    buf = timui_frame_buffer(f);
    cols = n < r.w ? n : r.w;               /* at most r.w most-recent samples */
    off  = r.w - cols;                      /* right-align within the rect */
    for(i = 0; i < cols; i++){
        /* map to one of eight sub-cell levels; level 0 stays blank */
        int level = timui_bar_cells(history[n - cols + i], 1.0f, 8);
        uint32_t cp = level <= 0 ? (uint32_t)' ' : (0x2580u + (uint32_t)level);
        char bytes[4];
        TimuiStr s;
        s.ptr = bytes;
        s.len = (size_t)timui_utf8_encode_(cp, bytes);   /* Z6 shared encoder */
        timui_draw_text(buf, r.x + off + i, r.y, s, style);
    }
}

/* Shared horizontal fill for gauge/meter/progress: a `r.w`-cell track filled to
 * `frac` with style.fg over style.bg, an optional bright peak `cap` tick
 * (cap < 0 disables it), and an optional readout right-aligned in a reserved
 * field. Draws on the first row (r.y). */
static void timui_hbar_(TimuiFrame *f, TimuiRect r, float frac, float cap,
                        TimuiStyle style, const char *readout, int rn){
    TimuiCellBuffer *buf = timui_frame_buffer(f);
    int reserve = (readout && rn > 0) ? rn + 1 : 0;     /* field + one-column gap */
    int track_w = r.w - reserve, filled;
    if(track_w < 1){ track_w = r.w; reserve = 0; }      /* no room: drop the readout */
    filled = timui_bar_cells(frac, 1.0f, track_w);
    if(filled > 0)
        timui_draw_fill(buf, TIMUI_RECT(r.x, r.y, filled, 1),
                        timui_style_make(style.fg, style.fg, 0));
    if(track_w - filled > 0)
        timui_draw_fill(buf, TIMUI_RECT(r.x + filled, r.y, track_w - filled, 1),
                        timui_style_make(style.bg, style.bg, 0));
    if(cap >= 0.0f){                                     /* floating peak-hold tick */
        int capx = timui_bar_cells(cap, 1.0f, track_w);
        if(capx >= track_w) capx = track_w - 1;
        if(capx >= filled && capx >= 0)
            timui_cell_bg_(buf, r.x + capx, r.y, timui_lerp_rgb(style.fg, 0xFFFFFFu, 0.5f));
    }
    if(reserve){
        TimuiStr s;
        s.ptr = readout;
        s.len = (size_t)rn;
        timui_draw_text(buf, r.x + track_w + 1, r.y, s,
                        timui_style_make(style.fg, style.bg, style.attrs));
    }
}

TIMUI_API void timui_progress(TimuiFrame *f, TimuiRect r, float frac, TimuiStyle style){
    char out[8];
    if(!f || r.w <= 0 || r.h <= 0) return;
    timui_hbar_(f, r, frac, -1.0f, style, out, timui_fmt_pct_(out, frac));
}

TIMUI_API void timui_gauge(TimuiFrame *f, TimuiRect r, float frac, TimuiStyle style){
    char out[8];
    if(!f || r.w <= 0 || r.h <= 0) return;
    timui_hbar_(f, r, frac, -1.0f, style, out, timui_fmt_frac2_(out, frac));
}

TIMUI_API void timui_meter(TimuiFrame *f, TimuiRect r, float level, float cap, TimuiStyle style){
    char out[8];
    if(!f || r.w <= 0 || r.h <= 0) return;
    timui_hbar_(f, r, level, cap, style, out, timui_fmt_frac2_(out, level));
}

TIMUI_API void timui_spinner(TimuiFrame *f, int x, int y, int tick, TimuiStyle style){
    TimuiCellBuffer *buf;
    char bytes[4];
    TimuiStr s;
    if(!f) return;
    buf = timui_frame_buffer(f);
    s.ptr = bytes;
    s.len = (size_t)timui_utf8_encode_(timui_spinner_glyph(tick), bytes);
    timui_draw_text(buf, x, y, s, style);
}
