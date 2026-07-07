/*
 * test_draw.c — drawing primitives into the cell buffer (T3.3).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <limits.h>

TIMUI_TEST(test_draw_text){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    TimuiStyle s = timui_style_make(0xffffff, TIMUI_COLOR_DEFAULT, 0);
    TimuiCell *g;
    timui_cells_init(&b, 20, 5, &al);
    timui_draw_text(&b, 2, 1, TIMUI_STR_LIT("Hi"), s);
    g = timui_cells_get(&b, 2, 1);
    TIMUI_CHECK(g && g->codepoint == 'H' && g->fg == 0xffffff);
    g = timui_cells_get(&b, 3, 1);
    TIMUI_CHECK(g && g->codepoint == 'i');
    timui_cells_destroy(&b);
}

TIMUI_TEST(test_draw_text_truncated_utf8_replacement){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    TimuiStyle s = timui_style_make(0xffffff, TIMUI_COLOR_DEFAULT, 0);
    timui_cells_init(&b, 4, 1, &al);
    timui_draw_text(&b, 0, 0, (TimuiStr){ "\xC3", 1 }, s);
    TIMUI_CHECK(timui_cells_get(&b, 0, 0)->codepoint == 0xFFFD);
    timui_cells_destroy(&b);
}

TIMUI_TEST(test_draw_fill){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    TimuiStyle s = timui_style_make(TIMUI_COLOR_DEFAULT, 0x0000ff, 0);
    timui_cells_init(&b, 10, 10, &al);
    timui_draw_fill(&b, TIMUI_RECT(1, 1, 4, 3), s);
    TIMUI_CHECK(timui_cells_get(&b, 1, 1)->bg == 0x0000ff);
    TIMUI_CHECK(timui_cells_get(&b, 4, 3)->bg == 0x0000ff);   /* inclusive corner */
    TIMUI_CHECK(timui_cells_get(&b, 5, 3)->codepoint == 0);   /* outside the fill */
    timui_cells_destroy(&b);
}

TIMUI_TEST(test_draw_fill_extreme_negative_rect_empty){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    TimuiStyle s = timui_style_make(TIMUI_COLOR_DEFAULT, 0x0000ff, 0);
    timui_cells_init(&b, 4, 2, &al);
    timui_draw_fill(&b, TIMUI_RECT(-1, 0, INT_MIN, 1), s);
    TIMUI_CHECK(timui_cells_get(&b, 0, 0)->codepoint == 0);
    TIMUI_CHECK(timui_cells_get(&b, 0, 0)->bg == TIMUI_COLOR_DEFAULT);
    timui_cells_destroy(&b);
}

TIMUI_TEST(test_draw_box_single){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    TimuiStyle s = timui_style_make(TIMUI_COLOR_DEFAULT, TIMUI_COLOR_DEFAULT, 0);
    timui_cells_init(&b, 10, 10, &al);
    timui_draw_box(&b, TIMUI_RECT(0, 0, 4, 3), TIMUI_BORDER_SINGLE, s);
    TIMUI_CHECK(timui_cells_get(&b, 0, 0)->codepoint == 0x250C);  /* TL */
    TIMUI_CHECK(timui_cells_get(&b, 3, 0)->codepoint == 0x2510);  /* TR */
    TIMUI_CHECK(timui_cells_get(&b, 0, 2)->codepoint == 0x2514);  /* BL */
    TIMUI_CHECK(timui_cells_get(&b, 3, 2)->codepoint == 0x2518);  /* BR */
    TIMUI_CHECK(timui_cells_get(&b, 1, 0)->codepoint == 0x2500);  /* top edge */
    TIMUI_CHECK(timui_cells_get(&b, 0, 1)->codepoint == 0x2502);  /* left edge */
    timui_cells_destroy(&b);
}

TIMUI_TEST(test_draw_box_ascii){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    TimuiStyle s = timui_style_make(TIMUI_COLOR_DEFAULT, TIMUI_COLOR_DEFAULT, 0);
    timui_cells_init(&b, 10, 10, &al);
    timui_draw_box(&b, TIMUI_RECT(0, 0, 3, 3), TIMUI_BORDER_ASCII, s);
    TIMUI_CHECK(timui_cells_get(&b, 0, 0)->codepoint == '+');
    TIMUI_CHECK(timui_cells_get(&b, 1, 0)->codepoint == '-');
    TIMUI_CHECK(timui_cells_get(&b, 0, 1)->codepoint == '|');
    timui_cells_destroy(&b);
}

/* Z17: hline/vline coverage — correct glyphs, extent, and no-op guards. Neither
 * is reached via draw_box (which emits its own edges through put_glyph). */
TIMUI_TEST(test_draw_hline_vline){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    TimuiStyle s = timui_style_make(0xffffff, TIMUI_COLOR_DEFAULT, 0);
    timui_cells_init(&b, 8, 8, &al);
    timui_draw_hline(&b, 1, 0, 3, s);
    TIMUI_CHECK(timui_cells_get(&b, 1, 0)->codepoint == 0x2500);
    TIMUI_CHECK(timui_cells_get(&b, 3, 0)->codepoint == 0x2500);
    TIMUI_CHECK(timui_cells_get(&b, 4, 0)->codepoint == 0);      /* stops at x+w */
    timui_draw_vline(&b, 0, 1, 3, s);
    TIMUI_CHECK(timui_cells_get(&b, 0, 1)->codepoint == 0x2502);
    TIMUI_CHECK(timui_cells_get(&b, 0, 3)->codepoint == 0x2502);
    TIMUI_CHECK(timui_cells_get(&b, 0, 4)->codepoint == 0);
    /* zero/negative extent and NULL buffer are silent no-ops (no crash) */
    timui_draw_hline(&b, 0, 5, 0, s);
    timui_draw_vline(&b, 5, 0, -3, s);
    timui_draw_hline(NULL, 0, 0, 3, s);
    TIMUI_CHECK(timui_cells_get(&b, 0, 5)->codepoint == 0);
    timui_cells_destroy(&b);
}

/* Z9: draw_box/hline/vline must clamp extreme geometry to the buffer, mirroring
 * the Y2 draw_fill guard, so `r.x + r.w - 1` can't overflow (signed UB) and the
 * edge loops can't spin ~INT_MAX times. Observable proof: an oversized box's
 * right/bottom borders land at the buffer edge. Unclamped, (9,0) is a plain top
 * edge (not the TR corner) and the right border at column 9 is absent. */
TIMUI_TEST(test_draw_box_clamps_oversized){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    TimuiStyle s = timui_style_make(TIMUI_COLOR_DEFAULT, TIMUI_COLOR_DEFAULT, 0);
    timui_cells_init(&b, 10, 6, &al);
    timui_draw_box(&b, TIMUI_RECT(0, 0, 100000, 100000), TIMUI_BORDER_SINGLE, s);
    TIMUI_CHECK(timui_cells_get(&b, 0, 0)->codepoint == 0x250C);   /* TL */
    TIMUI_CHECK(timui_cells_get(&b, 9, 0)->codepoint == 0x2510);   /* TR clamped to right edge */
    TIMUI_CHECK(timui_cells_get(&b, 0, 5)->codepoint == 0x2514);   /* BL clamped to bottom edge */
    TIMUI_CHECK(timui_cells_get(&b, 9, 5)->codepoint == 0x2518);   /* BR clamped corner */
    TIMUI_CHECK(timui_cells_get(&b, 9, 2)->codepoint == 0x2502);   /* right border present */
    /* huge hline/vline: draw the in-bounds run and return (no hang/overflow) */
    timui_draw_hline(&b, 0, 3, 100000, s);
    TIMUI_CHECK(timui_cells_get(&b, 9, 3)->codepoint == 0x2500);
    timui_cells_destroy(&b);
}
