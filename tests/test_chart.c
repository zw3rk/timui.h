/* test_chart.c — standalone unit tests for src/timui_chart.c: the indicator
 * widgets (bar chart, sparkline, gauge/meter/progress, spinner) and their pure
 * helpers (colour lerp, filled-cell counting, peak-hold envelope, spinner
 * frame selection). Builds timui as a single TU and drives both the pure
 * helpers (no frame) and the widgets (through a fake-transport test frame),
 * asserting hand-computed cell contents — synthetic ground truth, no oracle.
 *
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"
#include <stdio.h>
#include <math.h>

static int failures;
#define CHECK(cond) do { if(!(cond)){ \
    printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while(0)
/* float equality with a generous epsilon (the widgets round to whole cells). */
#define FEQ(a, b) (fabsf((float)(a) - (float)(b)) < 1e-4f)
#define CELL(buf, x, y) timui_cells_get((buf), (x), (y))

/* Open a test ui backed by a fake transport at a fixed size. */
static TimuiAllocator g_al;
static Timui *open_ui(TimuiFakeTransport *fake, int w, int h){
    TimuiTransport t;
    Timui *ui = NULL;
    g_al = timui_default_allocator();
    timui_fake_init(fake, &g_al);
    t = timui_fake_transport(fake);
    timui_open_for_test(&ui, t, w, h, &g_al);
    return ui;
}

int main(void){
    /* ================================================================= *
     * Pure helpers (no frame) — TDD ground truth.
     * ================================================================= */

    /* ---- timui_lerp_rgb: endpoints, midpoint, clamp ---- */
    CHECK(timui_lerp_rgb(0xFF0000u, 0x0000FFu, 0.0f) == 0xFF0000u);        /* t=0 -> a */
    CHECK(timui_lerp_rgb(0xFF0000u, 0x0000FFu, 1.0f) == 0x0000FFu);        /* t=1 -> b */
    /* midpoint ROUNDS each channel to nearest (the shared timui_box.c lerp):
     * R 255->0 at 0.5 = round(127.5) = 0x80; B 0->255 = round(127.5) = 0x80. */
    CHECK(timui_lerp_rgb(0xFF0000u, 0x0000FFu, 0.5f) == 0x800080u);
    CHECK(timui_lerp_rgb(0x000000u, 0xFFFFFFu, 0.5f) == 0x808080u);        /* grey midpoint */
    CHECK(timui_lerp_rgb(0xFF0000u, 0x0000FFu, 2.0f) == 0x0000FFu);        /* t>1 clamps to b */
    CHECK(timui_lerp_rgb(0xFF0000u, 0x0000FFu, -1.0f) == 0xFF0000u);       /* t<0 clamps to a */

    /* ---- timui_bar_cells: round, clamp 0..1, value>max = full, size<=0 ---- */
    CHECK(timui_bar_cells(0.0f, 1.0f, 10) == 0);
    CHECK(timui_bar_cells(1.0f, 1.0f, 10) == 10);
    CHECK(timui_bar_cells(0.5f, 1.0f, 10) == 5);        /* 5.0 -> 5 */
    CHECK(timui_bar_cells(0.55f, 1.0f, 10) == 6);       /* 5.5 rounds up */
    CHECK(timui_bar_cells(2.0f, 1.0f, 10) == 10);       /* value>max clamps to full */
    CHECK(timui_bar_cells(-1.0f, 1.0f, 10) == 0);       /* value<0 clamps to empty */
    CHECK(timui_bar_cells(50.0f, 100.0f, 10) == 5);     /* scaled against max */
    CHECK(timui_bar_cells(0.5f, 0.0f, 10) == 5);        /* max<=0 => value already 0..1 */
    CHECK(timui_bar_cells(0.5f, -3.0f, 10) == 5);
    CHECK(timui_bar_cells(1.0f, 1.0f, 0) == 0);         /* size<=0 => 0 cells */

    /* gauge fraction -> filled cells over a 20-wide track (0, 0.5, 1, clamp) */
    CHECK(timui_bar_cells(0.0f, 1.0f, 20) == 0);
    CHECK(timui_bar_cells(0.5f, 1.0f, 20) == 10);
    CHECK(timui_bar_cells(1.0f, 1.0f, 20) == 20);
    CHECK(timui_bar_cells(1.5f, 1.0f, 20) == 20);       /* out-of-range clamps to full */

    /* ---- timui_peak_hold: instant attack, linear release, floor at value ---- */
    CHECK(FEQ(timui_peak_hold(0.2f, 0.8f, 0.05f), 0.8f));   /* value>cap => instant rise */
    CHECK(FEQ(timui_peak_hold(0.8f, 0.0f, 0.05f), 0.75f));  /* release by decay */
    CHECK(FEQ(timui_peak_hold(0.8f, 0.78f, 0.05f), 0.78f)); /* never below the live value */
    CHECK(FEQ(timui_peak_hold(0.02f, 0.0f, 0.05f), 0.0f));  /* never below 0 */
    /* a one-shot: rise to 1.0 instantly, then decay to exactly 0 over 4 frames
     * (0.25 is exact in float, so the arithmetic lands cleanly). */
    { float cap = timui_peak_hold(0.0f, 1.0f, 0.25f);       /* instant -> 1.0 */
      CHECK(FEQ(cap, 1.0f));
      cap = timui_peak_hold(cap, 0.0f, 0.25f); CHECK(FEQ(cap, 0.75f));
      cap = timui_peak_hold(cap, 0.0f, 0.25f); CHECK(FEQ(cap, 0.5f));
      cap = timui_peak_hold(cap, 0.0f, 0.25f); CHECK(FEQ(cap, 0.25f));
      cap = timui_peak_hold(cap, 0.0f, 0.25f); CHECK(FEQ(cap, 0.0f)); }

    /* ---- timui_spinner_glyph: cycle of 10, wraps forward and backward ---- */
    CHECK(timui_spinner_glyph(0)  == 0x280Bu);
    CHECK(timui_spinner_glyph(1)  == 0x2819u);
    CHECK(timui_spinner_glyph(10) == 0x280Bu);          /* wraps to frame 0 */
    CHECK(timui_spinner_glyph(23) == 0x2838u);          /* 23 % 10 == 3 -> FRAMES[3] */
    CHECK(timui_spinner_glyph(-1) == 0x280Fu);          /* negative wraps to frame 9 */

    /* ================================================================= *
     * Widgets (through a test frame) — assert hand-computed cells.
     * ================================================================= */

    /* ---- barchart: single full bar shows the vertical lo->hi gradient ---- */
    { TimuiFakeTransport fake; TimuiFrame *f = NULL; TimuiCellBuffer *buf;
      Timui *ui = open_ui(&fake, 40, 12);
      float v[1] = { 1.0f };
      TimuiBarState st = {{0}, 0};
      TimuiBarOpts o;
      memset(&o, 0, sizeof o);
      o.max = 1.0f; o.lo = 0xFF0000u; o.hi = 0x0000FFu; o.track = 0x101010u;
      timui_begin(ui, &f); buf = timui_frame_buffer(f);
      timui_barchart(f, TIMUI_RECT(0, 0, 8, 6), v, 1, o, &st);
      CHECK(CELL(buf, 0, 5)->bg == 0xFF0000u);          /* bottom row = lo (t=0) */
      CHECK(CELL(buf, 0, 0)->bg == 0x0000FFu);          /* top row = hi (t=1) */
      timui_end(f); timui_close(ui); timui_fake_destroy(&fake); }

    /* ---- barchart: all-zero values -> every cell is the track colour ---- */
    { TimuiFakeTransport fake; TimuiFrame *f = NULL; TimuiCellBuffer *buf;
      Timui *ui = open_ui(&fake, 40, 12);
      float v[3] = { 0.0f, 0.0f, 0.0f };
      TimuiBarState st = {{0}, 0};
      TimuiBarOpts o;
      memset(&o, 0, sizeof o);
      o.max = 1.0f; o.lo = 0xFF0000u; o.hi = 0x0000FFu; o.track = 0x101010u;
      timui_begin(ui, &f); buf = timui_frame_buffer(f);
      timui_barchart(f, TIMUI_RECT(0, 0, 9, 4), v, 3, o, &st);
      CHECK(CELL(buf, 0, 3)->bg == 0x101010u);          /* empty -> track */
      CHECK(CELL(buf, 0, 0)->bg == 0x101010u);
      timui_end(f); timui_close(ui); timui_fake_destroy(&fake); }

    /* ---- barchart: n == 0 draws nothing and does not crash ---- */
    { TimuiFakeTransport fake; TimuiFrame *f = NULL; TimuiCellBuffer *buf;
      Timui *ui = open_ui(&fake, 40, 12);
      float v[1] = { 1.0f };
      TimuiBarState st = {{0}, 0};
      TimuiBarOpts o;
      memset(&o, 0, sizeof o);
      o.max = 1.0f; o.lo = 0xFF0000u; o.hi = 0x0000FFu; o.track = 0x101010u;
      timui_begin(ui, &f); buf = timui_frame_buffer(f);
      timui_barchart(f, TIMUI_RECT(0, 0, 8, 6), v, 0, o, &st);
      CHECK(CELL(buf, 0, 0)->codepoint == 0);           /* nothing was drawn */
      timui_end(f); timui_close(ui); timui_fake_destroy(&fake); }

    /* ---- barchart: peak-hold cap floats ABOVE the fill after a drop ---- */
    { TimuiFakeTransport fake; TimuiFrame *f = NULL; TimuiCellBuffer *buf;
      Timui *ui = open_ui(&fake, 12, 10);
      float hi[1] = { 1.0f }, lo[1] = { 0.1f };
      TimuiBarState st = {{0}, 0};
      TimuiBarOpts o;
      memset(&o, 0, sizeof o);
      o.max = 1.0f; o.lo = 0x000000u; o.hi = 0x00FF00u; o.track = 0x101010u;
      o.peak_decay = 0.1f; o.cap_light = 0.5f;
      /* frame 1: value 1.0 fills the bar and pins the cap at the top */
      timui_begin(ui, &f);
      timui_barchart(f, TIMUI_RECT(0, 0, 4, 8), hi, 1, o, &st);
      timui_end(f);
      /* frame 2: value drops to 0.1 (filled=1); cap decays 1.0 -> 0.9 (row 6) */
      timui_begin(ui, &f); buf = timui_frame_buffer(f);
      timui_barchart(f, TIMUI_RECT(0, 0, 4, 8), lo, 1, o, &st);
      CHECK(CELL(buf, 0, 7)->bg == 0x000000u);          /* fill base (row 0, t=0) */
      CHECK(CELL(buf, 0, 6)->bg == 0x101010u);          /* empty track just above */
      CHECK(CELL(buf, 0, 1)->bg != 0x101010u);          /* floating cap here (row 6) */
      CHECK(CELL(buf, 0, 0)->bg == 0x101010u);          /* above the cap -> track */
      timui_end(f); timui_close(ui); timui_fake_destroy(&fake); }

    /* ---- sparkline: block glyphs for full / empty / half levels ---- */
    { TimuiFakeTransport fake; TimuiFrame *f = NULL; TimuiCellBuffer *buf;
      Timui *ui = open_ui(&fake, 40, 4);
      float full[8], zero[8], half[8]; int i;
      for(i = 0; i < 8; i++){ full[i] = 1.0f; zero[i] = 0.0f; half[i] = 0.5f; }
      timui_begin(ui, &f); buf = timui_frame_buffer(f);
      timui_sparkline(f, TIMUI_RECT(0, 0, 8, 1), full, 8, timui_style_make(0xFFFFFFu, 0, 0));
      CHECK(CELL(buf, 0, 0)->codepoint == 0x2588u);     /* level 8 -> full block */
      CHECK(CELL(buf, 7, 0)->codepoint == 0x2588u);
      timui_sparkline(f, TIMUI_RECT(0, 1, 8, 1), zero, 8, timui_style_make(0xFFFFFFu, 0, 0));
      CHECK(CELL(buf, 0, 1)->codepoint == (uint32_t)' ');/* level 0 -> blank */
      timui_sparkline(f, TIMUI_RECT(0, 2, 8, 1), half, 8, timui_style_make(0xFFFFFFu, 0, 0));
      CHECK(CELL(buf, 0, 2)->codepoint == 0x2584u);     /* level 4 -> ▄ */
      timui_end(f); timui_close(ui); timui_fake_destroy(&fake); }

    /* ---- progress: fill + track split and the "NN%" readout ---- */
    { TimuiFakeTransport fake; TimuiFrame *f = NULL; TimuiCellBuffer *buf;
      Timui *ui = open_ui(&fake, 40, 4);
      TimuiStyle s = timui_style_make(0x00FF00u, 0x000010u, 0);
      timui_begin(ui, &f); buf = timui_frame_buffer(f);
      timui_progress(f, TIMUI_RECT(0, 0, 10, 1), 0.5f, s);   /* track_w=6, filled=3 */
      CHECK(CELL(buf, 2, 0)->bg == 0x00FF00u);          /* filled */
      CHECK(CELL(buf, 3, 0)->bg == 0x000010u);          /* track */
      CHECK(CELL(buf, 7, 0)->codepoint == (uint32_t)'5');/* readout "50%" */
      CHECK(CELL(buf, 9, 0)->codepoint == (uint32_t)'%');
      timui_end(f); timui_close(ui); timui_fake_destroy(&fake); }

    /* ---- gauge: fill + track split and the "N.NN" readout ---- */
    { TimuiFakeTransport fake; TimuiFrame *f = NULL; TimuiCellBuffer *buf;
      Timui *ui = open_ui(&fake, 40, 4);
      TimuiStyle s = timui_style_make(0xABCDEFu, 0x111111u, 0);
      timui_begin(ui, &f); buf = timui_frame_buffer(f);
      timui_gauge(f, TIMUI_RECT(0, 0, 12, 1), 0.5f, s);      /* track_w=7, filled=4 */
      CHECK(CELL(buf, 0, 0)->bg == 0xABCDEFu);          /* filled */
      CHECK(CELL(buf, 4, 0)->bg == 0x111111u);          /* track */
      CHECK(CELL(buf, 8, 0)->codepoint == (uint32_t)'0');/* readout "0.50" */
      CHECK(CELL(buf, 9, 0)->codepoint == (uint32_t)'.');
      CHECK(CELL(buf, 11, 0)->codepoint == (uint32_t)'0');
      timui_end(f); timui_close(ui); timui_fake_destroy(&fake); }

    /* ---- meter: fill, bright peak-hold cap tick, and the numeric readout ---- */
    { TimuiFakeTransport fake; TimuiFrame *f = NULL; TimuiCellBuffer *buf;
      Timui *ui = open_ui(&fake, 40, 4);
      TimuiStyle s = timui_style_make(0x00FF00u, 0x101010u, 0);
      timui_begin(ui, &f); buf = timui_frame_buffer(f);
      timui_meter(f, TIMUI_RECT(0, 0, 20, 1), 0.5f, 0.8f, s); /* track_w=15, filled=8, cap@12 */
      CHECK(CELL(buf, 0, 0)->bg == 0x00FF00u);          /* filled */
      CHECK(CELL(buf, 13, 0)->bg == 0x101010u);         /* track (above fill, below cap-less) */
      CHECK(CELL(buf, 12, 0)->bg == 0x80FF80u);         /* peak cap tick = fg lightened 50% (rounded lerp) */
      CHECK(CELL(buf, 16, 0)->codepoint == (uint32_t)'0');/* readout "0.50" */
      CHECK(CELL(buf, 19, 0)->codepoint == (uint32_t)'0');
      timui_end(f); timui_close(ui); timui_fake_destroy(&fake); }

    /* ---- spinner: draws the tick's frame glyph at (x,y) ---- */
    { TimuiFakeTransport fake; TimuiFrame *f = NULL; TimuiCellBuffer *buf;
      Timui *ui = open_ui(&fake, 40, 4);
      TimuiStyle s = timui_style_make(0xFFFFFFu, 0, 0);
      timui_begin(ui, &f); buf = timui_frame_buffer(f);
      timui_spinner(f, 2, 1, 0, s);
      CHECK(CELL(buf, 2, 1)->codepoint == 0x280Bu);     /* frame 0 */
      timui_spinner(f, 5, 1, 13, s);
      CHECK(CELL(buf, 5, 1)->codepoint == 0x2838u);     /* 13 % 10 == 3 -> frame 3 */
      timui_end(f); timui_close(ui); timui_fake_destroy(&fake); }

    /* ---- negative / adversarial: NULL frame and degenerate rects survive ---- */
    timui_barchart(NULL, TIMUI_RECT(0, 0, 8, 6), NULL, 1, (TimuiBarOpts){0}, NULL);
    timui_sparkline(NULL, TIMUI_RECT(0, 0, 8, 1), NULL, 1, timui_style_make(0, 0, 0));
    timui_progress(NULL, TIMUI_RECT(0, 0, 8, 1), 0.5f, timui_style_make(0, 0, 0));
    timui_gauge(NULL, TIMUI_RECT(0, 0, 8, 1), 0.5f, timui_style_make(0, 0, 0));
    timui_meter(NULL, TIMUI_RECT(0, 0, 8, 1), 0.5f, 0.5f, timui_style_make(0, 0, 0));
    timui_spinner(NULL, 0, 0, 0, timui_style_make(0, 0, 0));

    if(failures){ printf("chart: %d FAILED\n", failures); return 1; }
    printf("chart: all indicator-widget tests passed\n");
    return 0;
}
