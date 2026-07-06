/* test_layout.c — standalone unit tests for the constraint layout solver
 * (src/timui_layout.c) and the box-frame / colour-lerp helpers (src/timui_box.c).
 *
 * Builds timui as a single TU (TIMUI_IMPLEMENTATION) so the pure solver, the
 * frame-backed timui_border, and timui_lerp_rgb are all exercisable here. Every
 * expected value is hand-computed in the comment beside it (per the TDD spec):
 * fixed sizes first, the leftover split across the flexible children by weight
 * with the rounding remainder handed to the LAST flexible child, MIN/MAX bounds
 * honoured, and children tiling the area EXACTLY with no gaps/overlaps.
 *
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"
#include <stdio.h>

static int failures;
#define CHECK(cond) do { if(!(cond)){ \
    printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while(0)

static int rect_eq(TimuiRect r, int x, int y, int w, int h){
    return r.x == x && r.y == y && r.w == w && r.h == h;
}
/* NB: parameters are ex/ey/ew/eh (not x/y/w/h) so they do not get substituted
 * inside the struct member accesses _r.x / _r.y / _r.w / _r.h below. */
#define CHECK_RECT(rc, ex, ey, ew, eh) do { TimuiRect _r = (rc); \
    if(!rect_eq(_r, (ex), (ey), (ew), (eh))){ \
        printf("  FAIL %s:%d  rect {%d,%d,%d,%d} != {%d,%d,%d,%d}\n", \
               __FILE__, __LINE__, _r.x, _r.y, _r.w, _r.h, (ex), (ey), (ew), (eh)); \
        failures++; } } while(0)

/* ---- timui_split: the FLEX solver -------------------------------------- */
static void test_split_flex(void){
    /* Canonical: w=100 [LEN20, FLEX1, FLEX2]. LEN20 fixed; leftover 80 split
     * 1:2 with the remainder to the last flex -> round(80*1/3)=27, 80-27=53.
     * -> {x0 w20, x20 w27, x47 w53}; they tile [0,100) exactly. */
    { TimuiConstraint c[] = { TIMUI_LEN(20), TIMUI_FLEX(1), TIMUI_FLEX(2) };
      TimuiRect o[3];
      int n = timui_split(TIMUI_RECT(0, 0, 100, 10), TIMUI_AXIS_H, c, 3, o);
      CHECK(n == 3);
      CHECK_RECT(o[0], 0,  0, 20, 10);
      CHECK_RECT(o[1], 20, 0, 27, 10);
      CHECK_RECT(o[2], 47, 0, 53, 10);
      /* contiguity: no gaps / no overlaps, last ends exactly at the edge */
      CHECK(o[0].x + o[0].w == o[1].x);
      CHECK(o[1].x + o[1].w == o[2].x);
      CHECK(o[2].x + o[2].w == 100); }

    /* Single flex takes all the leftover. */
    { TimuiConstraint c[] = { TIMUI_LEN(20), TIMUI_FLEX(1) };
      TimuiRect o[2];
      timui_split(TIMUI_RECT(0, 0, 100, 5), TIMUI_AXIS_H, c, 2, o);
      CHECK_RECT(o[0], 0,  0, 20, 5);
      CHECK_RECT(o[1], 20, 0, 80, 5); }

    /* Three equal flexes: 100/3 -> 33,33 then remainder 34 to the last. */
    { TimuiConstraint c[] = { TIMUI_FLEX(1), TIMUI_FLEX(1), TIMUI_FLEX(1) };
      TimuiRect o[3];
      timui_split(TIMUI_RECT(0, 0, 100, 1), TIMUI_AXIS_H, c, 3, o);
      CHECK_RECT(o[0], 0,  0, 33, 1);
      CHECK_RECT(o[1], 33, 0, 33, 1);
      CHECK_RECT(o[2], 66, 0, 34, 1); }
}

/* ---- timui_split: PCT + rounding --------------------------------------- */
static void test_split_pct(void){
    /* PCT(33) of 10 = round(3.3) = 3; flex absorbs the rest -> exact tiling. */
    { TimuiConstraint c[] = { TIMUI_PCT(33), TIMUI_FLEX(1) };
      TimuiRect o[2];
      timui_split(TIMUI_RECT(0, 0, 10, 4), TIMUI_AXIS_H, c, 2, o);
      CHECK_RECT(o[0], 0, 0, 3, 4);
      CHECK_RECT(o[1], 3, 0, 7, 4); }

    /* PCT(50) of 7 = round(3.5) = 4 (round-half-up). */
    { TimuiConstraint c[] = { TIMUI_PCT(50), TIMUI_FLEX(1) };
      TimuiRect o[2];
      timui_split(TIMUI_RECT(0, 0, 7, 1), TIMUI_AXIS_H, c, 2, o);
      CHECK_RECT(o[0], 0, 0, 4, 1);
      CHECK_RECT(o[1], 4, 0, 3, 1); }

    /* Percentages that sum to 100 tile the whole area with no flex. */
    { TimuiConstraint c[] = { TIMUI_PCT(30), TIMUI_PCT(30), TIMUI_PCT(40) };
      TimuiRect o[3];
      timui_split(TIMUI_RECT(0, 0, 100, 1), TIMUI_AXIS_H, c, 3, o);
      CHECK_RECT(o[0], 0,  0, 30, 1);
      CHECK_RECT(o[1], 30, 0, 30, 1);
      CHECK_RECT(o[2], 60, 0, 40, 1); }
}

/* ---- timui_split: MIN / MAX bounds ------------------------------------- */
static void test_split_minmax(void){
    /* MIN(80) would get 50 by weight but is floored to 80; the flex shrinks to
     * the residual 20. */
    { TimuiConstraint c[] = { TIMUI_MIN(80), TIMUI_FLEX(1) };
      TimuiRect o[2];
      timui_split(TIMUI_RECT(0, 0, 100, 1), TIMUI_AXIS_H, c, 2, o);
      CHECK_RECT(o[0], 0,  0, 80, 1);
      CHECK_RECT(o[1], 80, 0, 20, 1); }

    /* MAX(30) would get 50 by weight but is capped to 30; the flex grows to 70. */
    { TimuiConstraint c[] = { TIMUI_MAX(30), TIMUI_FLEX(1) };
      TimuiRect o[2];
      timui_split(TIMUI_RECT(0, 0, 100, 1), TIMUI_AXIS_H, c, 2, o);
      CHECK_RECT(o[0], 0,  0, 30, 1);
      CHECK_RECT(o[1], 30, 0, 70, 1); }

    /* A slack MIN never triggers: 50 >= 30 stays 50. */
    { TimuiConstraint c[] = { TIMUI_MIN(30), TIMUI_FLEX(1) };
      TimuiRect o[2];
      timui_split(TIMUI_RECT(0, 0, 100, 1), TIMUI_AXIS_H, c, 2, o);
      CHECK_RECT(o[0], 0,  0, 50, 1);
      CHECK_RECT(o[1], 50, 0, 50, 1); }
}

/* ---- timui_split: over-constrained + degenerate ------------------------ */
static void test_split_edges(void){
    /* sum(LEN) > area: clamp to the boundary, never negative, never overflow. */
    { TimuiConstraint c[] = { TIMUI_LEN(40), TIMUI_LEN(40) };
      TimuiRect o[2];
      timui_split(TIMUI_RECT(0, 0, 50, 3), TIMUI_AXIS_H, c, 2, o);
      CHECK_RECT(o[0], 0,  0, 40, 3);
      CHECK_RECT(o[1], 40, 0, 10, 3);        /* clamped from 40 to the remaining 10 */
      CHECK(o[1].w >= 0); }

    /* n == 1 */
    { TimuiConstraint c[] = { TIMUI_FLEX(1) };
      TimuiRect o[1];
      CHECK(timui_split(TIMUI_RECT(0, 0, 100, 5), TIMUI_AXIS_H, c, 1, o) == 1);
      CHECK_RECT(o[0], 0, 0, 100, 5); }
    { TimuiConstraint c[] = { TIMUI_LEN(20) };
      TimuiRect o[1];
      timui_split(TIMUI_RECT(0, 0, 100, 5), TIMUI_AXIS_H, c, 1, o);
      CHECK_RECT(o[0], 0, 0, 20, 5); }       /* fixed underfill: trailing space unused */

    /* n == 0 and bad pointers return 0 and write nothing. */
    { TimuiConstraint c[] = { TIMUI_FLEX(1) };
      TimuiRect o[1] = { { -1, -1, -1, -1 } };
      CHECK(timui_split(TIMUI_RECT(0, 0, 100, 5), TIMUI_AXIS_H, c, 0, o) == 0);
      CHECK(rect_eq(o[0], -1, -1, -1, -1));   /* untouched */
      CHECK(timui_split(TIMUI_RECT(0, 0, 100, 5), TIMUI_AXIS_H, NULL, 1, o) == 0);
      CHECK(timui_split(TIMUI_RECT(0, 0, 100, 5), TIMUI_AXIS_H, c, 1, NULL) == 0); }
}

/* ---- timui_split: gap + margin + vertical axis ------------------------- */
static void test_split_opts_axis(void){
    /* gap=2 between two equal flexes: avail = 100 - 2 = 98, split 49/49, with a
     * 2-cell gap so they occupy [0,49) and [51,100). */
    { TimuiConstraint c[] = { TIMUI_FLEX(1), TIMUI_FLEX(1) };
      TimuiRect o[2];
      TimuiLayoutOpts opts; opts.gap = 2; opts.margin = 0;
      timui_split_ex(TIMUI_RECT(0, 0, 100, 1), TIMUI_AXIS_H, c, 2, opts, o);
      CHECK_RECT(o[0], 0,  0, 49, 1);
      CHECK_RECT(o[1], 51, 0, 49, 1); }

    /* margin=1 insets the area on every side before the split. */
    { TimuiConstraint c[] = { TIMUI_FLEX(1) };
      TimuiRect o[1];
      TimuiLayoutOpts opts; opts.gap = 0; opts.margin = 1;
      timui_split_ex(TIMUI_RECT(0, 0, 100, 10), TIMUI_AXIS_H, c, 1, opts, o);
      CHECK_RECT(o[0], 1, 1, 98, 8); }

    /* Vertical axis: y/h vary, x/w span the full (cross) width. */
    { TimuiConstraint c[] = { TIMUI_LEN(20), TIMUI_FLEX(1) };
      TimuiRect o[2];
      CHECK(timui_split_v(TIMUI_RECT(0, 0, 10, 100), c, 2, o) == 2);
      CHECK_RECT(o[0], 0, 0,  10, 20);
      CHECK_RECT(o[1], 0, 20, 10, 80); }

    /* split_h wrapper matches the explicit H axis. */
    { TimuiConstraint c[] = { TIMUI_FLEX(1), TIMUI_FLEX(1) };
      TimuiRect a[2], b[2];
      timui_split_h(TIMUI_RECT(3, 4, 20, 6), c, 2, a);
      timui_split(TIMUI_RECT(3, 4, 20, 6), TIMUI_AXIS_H, c, 2, b);
      CHECK(rect_eq(a[0], b[0].x, b[0].y, b[0].w, b[0].h));
      CHECK(rect_eq(a[1], b[1].x, b[1].y, b[1].w, b[1].h)); }
}

/* ---- timui_grid: 2x2 composition --------------------------------------- */
static void test_grid(void){
    TimuiConstraint rows[] = { TIMUI_FLEX(1), TIMUI_FLEX(1) };
    TimuiConstraint cols[] = { TIMUI_FLEX(1), TIMUI_FLEX(1) };
    TimuiRect o[4];
    int n = timui_grid(TIMUI_RECT(0, 0, 100, 20), rows, 2, cols, 2, o);
    CHECK(n == 4);
    CHECK_RECT(o[0], 0,  0,  50, 10);   /* row0 col0 */
    CHECK_RECT(o[1], 50, 0,  50, 10);   /* row0 col1 */
    CHECK_RECT(o[2], 0,  10, 50, 10);   /* row1 col0 */
    CHECK_RECT(o[3], 50, 10, 50, 10);   /* row1 col1 */

    /* uneven grid: 3 rows by heights, 2 cols by widths. */
    { TimuiConstraint r2[] = { TIMUI_LEN(2), TIMUI_FLEX(1), TIMUI_LEN(3) };
      TimuiConstraint c2[] = { TIMUI_PCT(50), TIMUI_FLEX(1) };
      TimuiRect g[6];
      int m = timui_grid(TIMUI_RECT(0, 0, 40, 10), r2, 3, c2, 2, g);
      CHECK(m == 6);
      CHECK_RECT(g[0], 0,  0, 20, 2);   /* row0 (h2) col0 (50% of 40 = 20) */
      CHECK_RECT(g[1], 20, 0, 20, 2);   /* row0 col1 (flex 20) */
      CHECK_RECT(g[2], 0,  2, 20, 5);   /* row1 (flex h = 10-2-3 = 5) col0 */
      CHECK_RECT(g[5], 20, 7, 20, 3); } /* row2 (h3, y=7) col1 */
}

/* ---- timui_border: inner rect + drawn glyphs --------------------------- */
static void test_border(void){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    TimuiStyle st = timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, 0);
    TimuiStr empty = { NULL, 0 };

    /* Pure inner-rect geometry — testable without a frame (NULL is guarded). */
    CHECK_RECT(timui_border(NULL, TIMUI_RECT(2, 3, 10, 5), TIMUI_BOX_SINGLE, empty, st),
               3, 4, 8, 3);                                  /* x+1,y+1,w-2,h-2 */
    CHECK_RECT(timui_border(NULL, TIMUI_RECT(0, 0, 1, 1), TIMUI_BOX_SINGLE, empty, st),
               1, 1, 0, 0);                                  /* degenerate 1x1: clamp >=0 */
    CHECK_RECT(timui_border(NULL, TIMUI_RECT(0, 0, 2, 2), TIMUI_BOX_SINGLE, empty, st),
               1, 1, 0, 0);                                  /* degenerate 2x2 */

    /* Frame-backed draw: corners per style + a title in the top edge. */
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 48, 6, &al);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);

    /* Four styles side by side, 12 cells apart; check each TL corner glyph. */
    timui_border(f, TIMUI_RECT(0,  0, 10, 5), TIMUI_BOX_SINGLE,  empty, st);
    timui_border(f, TIMUI_RECT(12, 0, 10, 5), TIMUI_BOX_ROUNDED, empty, st);
    timui_border(f, TIMUI_RECT(24, 0, 10, 5), TIMUI_BOX_DOUBLE,  empty, st);
    { TimuiRect inner = timui_border(f, TIMUI_RECT(36, 0, 10, 5), TIMUI_BOX_THICK,
                                     TIMUI_STR_LIT("Hi"), st);
      CHECK_RECT(inner, 37, 1, 8, 3); }

    CHECK(timui_cells_get(buf, 0,  0)->codepoint == 0x250C);  /* SINGLE  TL */
    CHECK(timui_cells_get(buf, 12, 0)->codepoint == 0x256D);  /* ROUNDED TL */
    CHECK(timui_cells_get(buf, 24, 0)->codepoint == 0x2554);  /* DOUBLE  TL */
    CHECK(timui_cells_get(buf, 36, 0)->codepoint == 0x250F);  /* THICK   TL */

    /* THICK edges + corners around the last box (x 36..45). */
    CHECK(timui_cells_get(buf, 45, 0)->codepoint == 0x2513);  /* THICK TR */
    CHECK(timui_cells_get(buf, 36, 4)->codepoint == 0x2517);  /* THICK BL */
    CHECK(timui_cells_get(buf, 45, 4)->codepoint == 0x251B);  /* THICK BR */
    CHECK(timui_cells_get(buf, 36, 1)->codepoint == 0x2503);  /* THICK left edge */
    CHECK(timui_cells_get(buf, 40, 4)->codepoint == 0x2501);  /* THICK bottom edge */

    /* Title sits in the top edge, one cell in; the rest of the edge is drawn. */
    CHECK(timui_cells_get(buf, 37, 0)->codepoint == 'H');
    CHECK(timui_cells_get(buf, 38, 0)->codepoint == 'i');
    CHECK(timui_cells_get(buf, 39, 0)->codepoint == 0x2501); /* edge resumes after title */

    /* DOUBLE box corners (x 24..33). */
    CHECK(timui_cells_get(buf, 33, 0)->codepoint == 0x2557);  /* DOUBLE TR */
    CHECK(timui_cells_get(buf, 24, 4)->codepoint == 0x255A);  /* DOUBLE BL */
    CHECK(timui_cells_get(buf, 33, 4)->codepoint == 0x255D);  /* DOUBLE BR */

    timui_end(f);
    timui_close(ui);
}

/* ---- timui_lerp_rgb ---------------------------------------------------- */
static void test_lerp(void){
    CHECK(timui_lerp_rgb(0x000000, 0xFFFFFF, 0.0f) == 0x000000u);  /* t=0 -> a */
    CHECK(timui_lerp_rgb(0x000000, 0xFFFFFF, 1.0f) == 0xFFFFFFu);  /* t=1 -> b */
    CHECK(timui_lerp_rgb(0x000000, 0xFFFFFF, 0.5f) == 0x808080u);  /* mid = round(127.5) */
    CHECK(timui_lerp_rgb(0xFF0000, 0x0000FF, 0.5f) == 0x800080u);  /* R:255->0, B:0->255 */
    CHECK(timui_lerp_rgb(0x102030, 0x102030, 0.5f) == 0x102030u);  /* same colour, any t */
    /* out-of-range t clamps to [0,1]. */
    CHECK(timui_lerp_rgb(0x000000, 0xFFFFFF, 2.0f)  == 0xFFFFFFu);
    CHECK(timui_lerp_rgb(0x000000, 0xFFFFFF, -1.0f) == 0x000000u);
}

int main(void){
    test_split_flex();
    test_split_pct();
    test_split_minmax();
    test_split_edges();
    test_split_opts_axis();
    test_grid();
    test_border();
    test_lerp();
    if(failures){ printf("layout: %d FAILED\n", failures); return 1; }
    printf("layout: all split/grid/border/lerp tests passed\n");
    return 0;
}
