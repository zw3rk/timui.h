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

TIMUI_API TimuiRect timui_border(TimuiFrame *f, TimuiRect r, TimuiBorderStyle style,
                                 TimuiStr title, TimuiStyle st){
    TimuiCellBuffer *buf;
    TimuiRect inner;
    uint32_t hz, vt, tl, tr, bl, br;
    int i;

    /* Inner content rect: r inset by the 1-cell frame, clamped non-negative.
     * Always computed so it is valid even on the no-draw paths below. */
    inner.x = r.x + 1;
    inner.y = r.y + 1;
    inner.w = r.w - 2; if(inner.w < 0) inner.w = 0;
    inner.h = r.h - 2; if(inner.h < 0) inner.h = 0;

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

    timui_box_put_(buf, r.x,           r.y,           tl, st);
    timui_box_put_(buf, r.x + r.w - 1, r.y,           tr, st);
    timui_box_put_(buf, r.x,           r.y + r.h - 1, bl, st);
    timui_box_put_(buf, r.x + r.w - 1, r.y + r.h - 1, br, st);
    for(i = 1; i < r.w - 1; i++){
        timui_box_put_(buf, r.x + i, r.y,           hz, st);
        timui_box_put_(buf, r.x + i, r.y + r.h - 1, hz, st);
    }
    for(i = 1; i < r.h - 1; i++){
        timui_box_put_(buf, r.x,           r.y + i, vt, st);
        timui_box_put_(buf, r.x + r.w - 1, r.y + i, vt, st);
    }

    /* Optional title in the top edge, one cell in. Clip to the interior span so
     * a long title truncates cleanly rather than spilling over the corners. */
    if(title.ptr && title.len && r.w > 2){
        timui_push_clip(f, TIMUI_RECT(r.x + 1, r.y, r.w - 2, 1));
        timui_draw_text(buf, r.x + 1, r.y, title, st);
        timui_pop_clip(f);
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
