/*
 * timui_box.c — line-drawing box frame + RGB colour interpolation.
 *
 * timui_border strokes a 1-cell frame around a rect using one of four Unicode
 * line styles (SINGLE / ROUNDED / DOUBLE / THICK), embeds an optional title in
 * the top edge, and returns the inner content rect (r inset by the frame). The
 * inner rect is computed and returned even when the frame is too small to draw
 * or the frame pointer is NULL, so callers can always lay out inside it.
 *
 * timui_lerp_rgb linearly interpolates two packed 0xRRGGBB colours. Both are
 * side-effect-free apart from the cell writes timui_border makes into the
 * frame's buffer (via the public text primitive, which handles clipping and
 * wide-glyph continuation).
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */

/* Encode one already-validated codepoint and stamp it at (x,y) through the
 * public text primitive (timui_utf8_encode_ is the shared encoder from
 * timui_int.h). Keeps the box logic on the public drawing API rather than
 * reaching into the renderer's private put_glyph. */
static void timui_box_put_(TimuiCellBuffer *buf, int x, int y, uint32_t cp, TimuiStyle st){
    char tmp[4];
    TimuiStr s;
    s.ptr = tmp;
    s.len = (size_t)timui_utf8_encode_(cp, tmp);
    timui_draw_text(buf, x, y, s, st);
}

static int timui_box_clamp_i64_(int64_t v){
    if(v < (int64_t)INT_MIN) return INT_MIN;
    if(v > (int64_t)INT_MAX) return INT_MAX;
    return (int)v;
}

TIMUI_API TimuiRect timui_border(TimuiFrame *f, TimuiRect r, TimuiBorderStyle style,
                                 TimuiStr title, TimuiStyle st){
    TimuiCellBuffer *buf;
    TimuiRect inner;
    uint32_t hz, vt, tl, tr, bl, br;
    int64_t x0, y0, x1, y1;
    int64_t xs, xe, ys, ye, p;
    int64_t iw, ih;

    /* Inner content rect: r inset by the 1-cell frame, clamped non-negative.
     * Always computed so it is valid even on the no-draw paths below. */
    inner.x = timui_box_clamp_i64_((int64_t)r.x + 1);
    inner.y = timui_box_clamp_i64_((int64_t)r.y + 1);
    iw = (int64_t)r.w - 2;
    ih = (int64_t)r.h - 2;
    inner.w = iw > 0 ? timui_box_clamp_i64_(iw) : 0;
    inner.h = ih > 0 ? timui_box_clamp_i64_(ih) : 0;

    if(!f) return inner;
    buf = timui_frame_buffer(f);
    if(!buf) return inner;

    /* Glyph set per style: horizontal, vertical, and the four corners. */
    switch(style){
        case TIMUI_BOX_ROUNDED: hz=0x2500; vt=0x2502; tl=0x256D; tr=0x256E; bl=0x2570; br=0x256F; break;
        case TIMUI_BOX_DOUBLE:  hz=0x2550; vt=0x2551; tl=0x2554; tr=0x2557; bl=0x255A; br=0x255D; break;
        case TIMUI_BOX_THICK:   hz=0x2501; vt=0x2503; tl=0x250F; tr=0x2513; bl=0x2517; br=0x251B; break;
        case TIMUI_BOX_SINGLE:
        default:                hz=0x2500; vt=0x2502; tl=0x250C; tr=0x2510; bl=0x2514; br=0x2518; break;
    }

    /* Need a 2x2 rect to stroke a frame with distinct corners; otherwise the
     * inner rect is still returned for layout. */
    if(r.w < 2 || r.h < 2) return inner;

    x0 = (int64_t)r.x; y0 = (int64_t)r.y;
    x1 = x0 + (int64_t)r.w - 1;
    y1 = y0 + (int64_t)r.h - 1;

    if(y0 >= 0 && y0 < buf->h){
        if(x0 >= 0 && x0 < buf->w) timui_box_put_(buf, (int)x0, (int)y0, tl, st);
        if(x1 >= 0 && x1 < buf->w) timui_box_put_(buf, (int)x1, (int)y0, tr, st);
        xs = x0 + 1; xe = x1 - 1;
        if(xs < 0) xs = 0;
        if(xe > (int64_t)buf->w - 1) xe = (int64_t)buf->w - 1;
        for(p = xs; p <= xe; p++) timui_box_put_(buf, (int)p, (int)y0, hz, st);
    }
    if(y1 >= 0 && y1 < buf->h){
        if(x0 >= 0 && x0 < buf->w) timui_box_put_(buf, (int)x0, (int)y1, bl, st);
        if(x1 >= 0 && x1 < buf->w) timui_box_put_(buf, (int)x1, (int)y1, br, st);
        xs = x0 + 1; xe = x1 - 1;
        if(xs < 0) xs = 0;
        if(xe > (int64_t)buf->w - 1) xe = (int64_t)buf->w - 1;
        for(p = xs; p <= xe; p++) timui_box_put_(buf, (int)p, (int)y1, hz, st);
    }
    ys = y0 + 1; ye = y1 - 1;
    if(ys < 0) ys = 0;
    if(ye > (int64_t)buf->h - 1) ye = (int64_t)buf->h - 1;
    for(p = ys; p <= ye; p++){
        if(x0 >= 0 && x0 < buf->w) timui_box_put_(buf, (int)x0, (int)p, vt, st);
        if(x1 >= 0 && x1 < buf->w) timui_box_put_(buf, (int)x1, (int)p, vt, st);
    }

    /* Optional title in the top edge, one cell in. Clip to the interior span so
     * a long title truncates cleanly rather than spilling over the corners. */
    if(title.ptr && title.len && r.w > 2 && y0 >= 0 && y0 < buf->h){
        int64_t tx = x0 + 1;
        xs = tx; xe = x1 - 1;
        if(xs < 0) xs = 0;
        if(xe > (int64_t)buf->w - 1) xe = (int64_t)buf->w - 1;
        if(xs <= xe && tx >= (int64_t)INT_MIN && tx <= (int64_t)INT_MAX){
            timui_push_clip(f, TIMUI_RECT((int)xs, (int)y0, (int)(xe - xs + 1), 1));
            timui_draw_text(buf, (int)tx, (int)y0, title, st);
            timui_pop_clip(f);
        }
    }

    return inner;
}

TIMUI_API uint32_t timui_lerp_rgb(uint32_t a, uint32_t b, float t){
    float ar = (float)((a >> 16) & 0xFF), ag = (float)((a >> 8) & 0xFF), ab = (float)(a & 0xFF);
    float br = (float)((b >> 16) & 0xFF), bg = (float)((b >> 8) & 0xFF), bb = (float)(b & 0xFF);
    unsigned rr, rg, rb;
    if(t < 0.0f) t = 0.0f;
    if(t > 1.0f) t = 1.0f;
    /* Each interpolated channel stays within [0,255], so rounding a
     * non-negative value with +0.5 is correct in both directions. */
    rr = (unsigned)(ar + (br - ar) * t + 0.5f);
    rg = (unsigned)(ag + (bg - ag) * t + 0.5f);
    rb = (unsigned)(ab + (bb - ab) * t + 0.5f);
    return (rr << 16) | (rg << 8) | rb;
}
