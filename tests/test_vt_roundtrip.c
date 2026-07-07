/*
 * test_vt_roundtrip.c — libvterm round-trip visual tests (Tier A).
 *
 * Validates timui's diff renderer end-to-end against a real VT emulator:
 *   scene cell buffer
 *     -> timui_render_diff through a fake transport (captures the escapes)
 *     -> vterm_input_write into a fresh libvterm of the same size
 *     -> read back libvterm's screen grid
 *     -> compare it cell-by-cell against the original scene.
 * This catches SGR / CUP / OSC-8 / truecolor / wide-glyph bugs that
 * byte-substring checks cannot.
 *
 * Only compiled when WITH_VTERM=1 resolves libvterm via pkg-config; the tests
 * are registered in test_main.c under TIMUI_WITH_VTERM_TESTS.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"
#include "scenes.h"      /* scene_panel / scene_wide — shared with goldens */
#include <vterm.h>

#include <stdio.h>
#include <string.h>

/* ---- TimuiCell <-> VTermScreenCell mapping ------------------------------ *
 * Rationale (see docs/visual-tests.md):
 *  - vterm_new takes (rows, cols); timui buffers are (w, h)  ->  transpose.
 *  - timui fg/bg == TIMUI_COLOR_DEFAULT means "default" (emit_sgr emits no
 *    color SGR), which maps to libvterm's VTERM_COLOR_IS_DEFAULT_FG/BG. NB:
 *    convert_color_to_rgb RESETS the default flags, so the default check must
 *    happen BEFORE any conversion.
 *  - An empty timui cell (codepoint 0) renders as a space (' '), and a reset
 *    libvterm screen is full of spaces, so codepoint 0 and ' ' are treated as
 *    interchangeable "blank".
 *  - timui marks a wide glyph's second column with TIMUI_CELL_CONTINUATION;
 *    libvterm reports a placeholder there. We skip continuation cells (the
 *    glyph is validated at its lead cell, which carries width 2).
 *  - libvterm has no DIM/faint attribute, so TIMUI_ATTR_DIM is masked out of
 *    the comparison (it is still verified by the Tier-B golden snapshot). */

/* Decode a vterm color into a packed 0xRRGGBB timui value. Default colors
 * (fg or bg) map to TIMUI_COLOR_DEFAULT, timui's "no SGR" sentinel. */
static uint32_t vterm_color_to_timui(VTermScreen *s, const VTermColor *col){
    if(VTERM_COLOR_IS_DEFAULT_FG(col) || VTERM_COLOR_IS_DEFAULT_BG(col)) return TIMUI_COLOR_DEFAULT;
    { VTermColor c = *col;
      vterm_screen_convert_color_to_rgb(s, &c);   /* indexed/rgb -> rgb */
      return ((uint32_t)c.rgb.red << 16) | ((uint32_t)c.rgb.green << 8) | (uint32_t)c.rgb.blue;
    }
}

/* Compare one timui cell against the libvterm cell at (x,y). Returns 1 on
 * match; on mismatch, writes a one-line description to msg. */
static int cell_matches_vterm(VTermScreen *s, int x, int y,
                              const TimuiCell *tc, char *msg, size_t msgcap){
    VTermScreenCell vc;
    VTermPos pos = { y, x };
    uint32_t vfg, vbg;
    int t_under, v_under, t_blank, v_blank;
    uint32_t tm;

    if(!vterm_screen_get_cell(s, pos, &vc)){
        if(msg) snprintf(msg, msgcap, "get_cell failed at (%d,%d)", x, y);
        return 0;
    }

    /* codepoint: blank (0 or space) matches blank; else exact codepoint */
    t_blank = (tc->codepoint == 0 || tc->codepoint == ' ');
    v_blank = (vc.chars[0] == 0 || vc.chars[0] == ' ');
    if(t_blank != v_blank || (!t_blank && tc->codepoint != vc.chars[0])){
        if(msg) snprintf(msg, msgcap, "codepoint (%d,%d): timui 0x%x vterm 0x%x",
                         x, y, tc->codepoint, vc.chars[0]);
        return 0;
    }
    /* width: meaningful only for non-blank cells (blanks: timui width 0 vs vterm width 1) */
    if(!t_blank && tc->width != (uint16_t)vc.width){
        if(msg) snprintf(msg, msgcap, "width (%d,%d): timui %u vterm %d",
                         x, y, tc->codepoint, vc.width);
        return 0;
    }

    /* colors: check default flags BEFORE conversion */
    vfg = vterm_color_to_timui(s, &vc.fg);
    vbg = vterm_color_to_timui(s, &vc.bg);
    if(vfg != tc->fg){
        if(msg) snprintf(msg, msgcap, "fg (%d,%d): timui 0x%06x vterm 0x%06x", x, y, tc->fg, vfg);
        return 0;
    }
    if(vbg != tc->bg){
        if(msg) snprintf(msg, msgcap, "bg (%d,%d): timui 0x%06x vterm 0x%06x", x, y, tc->bg, vbg);
        return 0;
    }

    /* attrs: normalize both sides; drop DIM (no vterm faint); underline is a
     * 2-bit enum on the vterm side (SINGLE == 1) vs a single bit on timui. */
    tm = tc->attrs & ~(uint32_t)TIMUI_ATTR_DIM;
    t_under = (tm & TIMUI_ATTR_UNDERLINE) ? VTERM_UNDERLINE_SINGLE : VTERM_UNDERLINE_OFF;
    v_under = (int)vc.attrs.underline;
    if(((tm & TIMUI_ATTR_BOLD)      ? 1 : 0) != (int)vc.attrs.bold   ||
       ((tm & TIMUI_ATTR_ITALIC)    ? 1 : 0) != (int)vc.attrs.italic ||
       ((tm & TIMUI_ATTR_BLINK)     ? 1 : 0) != (int)vc.attrs.blink  ||
       ((tm & TIMUI_ATTR_REVERSE)   ? 1 : 0) != (int)vc.attrs.reverse||
       ((tm & TIMUI_ATTR_STRIKE)    ? 1 : 0) != (int)vc.attrs.strike ||
       t_under != v_under){
        if(msg) snprintf(msg, msgcap,
            "attrs (%d,%d): timui b%d i%d u%d k%d r%d s%d  vterm b%d i%d u%d k%d r%d s%d",
            x, y,
            (tm & TIMUI_ATTR_BOLD)?1:0, (tm & TIMUI_ATTR_ITALIC)?1:0, t_under,
            (tm & TIMUI_ATTR_BLINK)?1:0, (tm & TIMUI_ATTR_REVERSE)?1:0, (tm & TIMUI_ATTR_STRIKE)?1:0,
            (int)vc.attrs.bold, (int)vc.attrs.italic, v_under, (int)vc.attrs.blink,
            (int)vc.attrs.reverse, (int)vc.attrs.strike);
        return 0;
    }
    return 1;
}

/* Compare the whole scene grid against the libvterm screen, skipping timui
 * continuation cells (wide-glyph second columns). */
static int grid_matches_vterm(const TimuiCellBuffer *curr, VTermScreen *s,
                              char *msg, size_t msgcap){
    int x, y;
    for(y = 0; y < curr->h; y++){
        for(x = 0; x < curr->w; x++){
            const TimuiCell *tc = &curr->cells[(size_t)y * curr->w + x];
            if(tc->flags & TIMUI_CELL_CONTINUATION) continue;
            if(!cell_matches_vterm(s, x, y, tc, msg, msgcap)) return 0;
        }
    }
    return 1;
}

/* Two-frame round-trip: feed empty->prev (establishes vterm state), then the
 * diff prev->curr (the path under test), then compare the vterm screen to
 * curr. Two frames make the partial-update scene meaningful: vterm must start
 * in prev's state so a single-cell diff lands correctly. For full scenes,
 * prev is empty so frame 1 emits nothing. */
static int vt_roundtrip(const TimuiCellBuffer *prev, const TimuiCellBuffer *curr,
                        char *msg, size_t msgcap){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer empty;
    TimuiFakeTransport fake;
    TimuiTransport t;
    TimuiRenderer r;
    VTerm *vt;
    VTermScreen *s;
    int ok;

    timui_cells_init(&empty, curr->w, curr->h, &al);
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_renderer_reset(&r);

    /* frame 1: empty -> prev */
    timui_render_diff(&t, &empty, prev, &r);

    vt = vterm_new(curr->h, curr->w);              /* rows, cols — transposed */
    vterm_set_utf8(vt, 1);
    s = vterm_obtain_screen(vt);
    vterm_screen_reset(s, 1);                       /* known default pen */
    vterm_input_write(vt, (const char *)fake.out, fake.out_len);

    /* frame 2: prev -> curr (the diff under test) */
    timui_fake_clear_output(&fake);
    timui_render_diff(&t, prev, curr, &r);
    vterm_input_write(vt, (const char *)fake.out, fake.out_len);
    vterm_screen_flush_damage(s);

    ok = grid_matches_vterm(curr, s, msg, msgcap);

    vterm_free(vt);
    timui_fake_destroy(&fake);
    timui_cells_destroy(&empty);
    return ok;
}

/* Run a round-trip for (prev, curr), printing the mismatch reason on failure. */
#define VT_RUN(prev, curr) do {                                  \
        char _m[256]; int _ok = vt_roundtrip((prev), (curr), _m, sizeof _m); \
        if(!_ok) printf("  vt: %s\n", _m);                       \
        TIMUI_CHECK(_ok);                                        \
    } while(0)

/* ---- scenes ------------------------------------------------------------ */

TIMUI_TEST(test_vt_plain_text){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer prev, curr;
    timui_cells_init(&prev, 10, 3, &al);
    timui_cells_init(&curr, 10, 3, &al);
    timui_draw_text(&curr, 0, 0, TIMUI_STR_LIT("Hello"), timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, 0));
    VT_RUN(&prev, &curr);
    timui_cells_destroy(&prev); timui_cells_destroy(&curr);
}

TIMUI_TEST(test_vt_rainbow){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer prev, curr;
    timui_cells_init(&prev, 10, 1, &al);
    scene_rainbow(&curr, &al);   /* one distinct truecolor fg per cell (shared scene) */
    VT_RUN(&prev, &curr);
    timui_cells_destroy(&prev); timui_cells_destroy(&curr);
}

TIMUI_TEST(test_vt_attrs){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer prev, curr;
    timui_cells_init(&prev, 18, 2, &al);
    scene_attrs(&curr, &al);   /* per-attr run incl. BOLD|DIM + blue fill (shared scene) */
    VT_RUN(&prev, &curr);
    timui_cells_destroy(&prev); timui_cells_destroy(&curr);
}

TIMUI_TEST(test_vt_box_glyphs){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer prev, curr;
    timui_cells_init(&prev, 12, 5, &al);
    scene_panel(&curr, &al);   /* single-line box border + title (shared scene) */
    VT_RUN(&prev, &curr);
    timui_cells_destroy(&prev); timui_cells_destroy(&curr);
}

TIMUI_TEST(test_vt_fill){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer prev, curr;
    timui_cells_init(&prev, 8, 2, &al);
    timui_cells_init(&curr, 8, 2, &al);
    timui_draw_fill(&curr, TIMUI_RECT(0, 0, 8, 2), timui_style_make(0xFFFFFF, 0x0000C0, 0));
    VT_RUN(&prev, &curr);
    timui_cells_destroy(&prev); timui_cells_destroy(&curr);
}

TIMUI_TEST(test_vt_hyperlink){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer prev, curr;
    uint32_t link;
    timui_cells_init(&prev, 8, 1, &al);
    timui_cells_init(&curr, 8, 1, &al);
    /* OSC 8 must not corrupt the glyph stream — the link text must still land.
     * (libvterm parses OSC 8 but does not store it, so only the glyph is checked.) */
    link = timui_hyperlink_set(&curr, "https://example.com");
    timui_draw_text_linked(&curr, 0, 0, TIMUI_STR_LIT("link"),
                           timui_style_make(0x00FFFF, TIMUI_COLOR_DEFAULT, TIMUI_ATTR_UNDERLINE), link);
    VT_RUN(&prev, &curr);
    timui_cells_destroy(&prev); timui_cells_destroy(&curr);
}

TIMUI_TEST(test_vt_partial_update){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer prev, curr;
    TimuiStyle white = timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, 0);
    timui_cells_init(&prev, 10, 1, &al);
    timui_cells_init(&curr, 10, 1, &al);
    timui_draw_text(&prev, 0, 0, TIMUI_STR_LIT("Hello"), white);
    timui_draw_text(&curr, 0, 0, TIMUI_STR_LIT("Jello"), white);   /* differs only at col 0 */
    VT_RUN(&prev, &curr);
    timui_cells_destroy(&prev); timui_cells_destroy(&curr);
}

TIMUI_TEST(test_vt_wide_glyph){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer prev, curr;
    timui_cells_init(&prev, 6, 1, &al);
    scene_wide(&curr, &al);   /* "AあB" — width-2 glyph + continuation cell */
    VT_RUN(&prev, &curr);
    timui_cells_destroy(&prev); timui_cells_destroy(&curr);
}
