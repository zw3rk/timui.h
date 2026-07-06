/* test_tabs.c — standalone unit tests for the tab-bar widget (W2).
 *
 * Two layers, per the TDD gate:
 *   1. The PURE geometry helpers (timui_tabs_layout / timui_tabs_scroll /
 *      timui_tab_visible) — hand-computed vectors, no frame, no terminal.
 *   2. The interactive timui_tabs widget driven through a fake transport
 *      (click to select, Left/Right to move), like tests/test_listbox.c.
 *
 * Builds timui as a single TU (TIMUI_IMPLEMENTATION) so the whole library —
 * including src/timui_tabs.c — is compiled in and the public helpers resolve.
 *
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if(!(cond)){ \
    printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while(0)

/* Assert a span equals {x,w}. */
#define SPAN_EQ(s, ex, ew) do { CHECK((s).x == (ex)); CHECK((s).w == (ew)); } while(0)

#define SETIN(fake, lit) timui_fake_set_input((fake), (lit), sizeof(lit) - 1)

/* ---- pure layout: [x, x+w) spans from labels + widths + separators ------ */
static void test_layout(void){
    TimuiTabSpan sp[8];
    /* ASCII: each tab = width(label)+2 (the ' LABEL ' box), sep=1 between.
     *   "A"(1)   -> {0,3}
     *   "BB"(2)  -> {4,4}   (0+3 + sep 1)
     *   "CCC"(3) -> {9,5}   (4+4 + sep 1)      total = 9+5 = 14 */
    { const char *L[] = { "A", "BB", "CCC" };
      int total = timui_tabs_layout(L, 3, 1, sp, 8);
      CHECK(total == 14);
      SPAN_EQ(sp[0], 0, 3); SPAN_EQ(sp[1], 4, 4); SPAN_EQ(sp[2], 9, 5); }

    /* CJK is width-aware: 中文 = 4 columns -> tab width 6.
     *   "中文"(4) -> {0,6}; "x"(1) -> {7,3}   total = 10 */
    { const char *L[] = { "\xE4\xB8\xAD\xE6\x96\x87", "x" };
      int total = timui_tabs_layout(L, 2, 1, sp, 8);
      CHECK(total == 10);
      SPAN_EQ(sp[0], 0, 6); SPAN_EQ(sp[1], 7, 3); }

    /* sep = 0 packs tabs adjacently. */
    { const char *L[] = { "A", "BB" };
      int total = timui_tabs_layout(L, 2, 0, sp, 8);
      CHECK(total == 7);              /* 3 + 4 */
      SPAN_EQ(sp[0], 0, 3); SPAN_EQ(sp[1], 3, 4); }

    /* ---- adversarial ---- */
    /* empty label -> the box is still 2 columns (both pads), still clickable. */
    { const char *L[] = { "", "" };
      int total = timui_tabs_layout(L, 2, 1, sp, 8);
      CHECK(total == 5);             /* 2 + sep 1 + 2 */
      SPAN_EQ(sp[0], 0, 2); SPAN_EQ(sp[1], 3, 2); }

    /* n = 0 -> width 0, nothing written (out untouched). */
    { const char *L[] = { "x" };
      sp[0].x = 111; sp[0].w = 222;
      CHECK(timui_tabs_layout(L, 0, 1, sp, 8) == 0);
      CHECK(sp[0].x == 111 && sp[0].w == 222); }

    /* n < 0 is treated as 0 (defensive). */
    CHECK(timui_tabs_layout(NULL, -3, 1, NULL, 0) == 0);

    /* out == NULL only measures (no write, no crash). */
    { const char *L[] = { "A", "BB", "CCC" };
      CHECK(timui_tabs_layout(L, 3, 1, NULL, 0) == 14); }

    /* max caps writes but the returned total still covers all tabs. */
    { const char *L[] = { "A", "BB", "CCC" };
      sp[2].x = 777;
      CHECK(timui_tabs_layout(L, 3, 1, sp, 2) == 14);   /* total still 14 */
      SPAN_EQ(sp[0], 0, 3); SPAN_EQ(sp[1], 4, 4);
      CHECK(sp[2].x == 777); }                          /* span[2] not written */
}

/* ---- pure scroll: keep `selected` visible, minimal move ----------------- */
static void test_scroll(void){
    /* spans {0,3},{4,4},{9,5}; total content width = 14. */
    TimuiTabSpan sp[3] = { {0,3}, {4,4}, {9,5} };

    /* everything fits -> no scroll regardless of selection. */
    CHECK(timui_tabs_scroll(sp, 3, 0, 14, 0) == 0);
    CHECK(timui_tabs_scroll(sp, 3, 2, 14, 0) == 0);

    /* narrow (8): selecting the last tab (index n-1) right-aligns it.
     *   sel end = 14, 14 > 0+8 -> scroll = 14-8 = 6 (clamped to max 6). */
    CHECK(timui_tabs_scroll(sp, 3, 2, 8, 0) == 6);
    /* index 0 is always fully visible at scroll 0. */
    CHECK(timui_tabs_scroll(sp, 3, 0, 8, 0) == 0);
    /* from a scrolled state, moving back to tab 0 brings the view home. */
    CHECK(timui_tabs_scroll(sp, 3, 0, 8, 6) == 0);
    /* middle tab already visible from cur=0 -> no move (end 8 not > 8). */
    CHECK(timui_tabs_scroll(sp, 3, 1, 8, 0) == 0);

    /* ---- adversarial ---- */
    /* a single tab WIDER than the viewport pins its LEFT edge (label start). */
    { TimuiTabSpan one[1] = { {0,10} };
      CHECK(timui_tabs_scroll(one, 1, 0, 4, 0) == 0); }
    /* a wide middle tab, viewport smaller than it: left edge pinned to its x. */
    { TimuiTabSpan w[2] = { {0,3}, {4,10} };   /* total 14 */
      CHECK(timui_tabs_scroll(w, 2, 1, 6, 0) == 4); }
    /* scroll never goes negative and never exceeds total-width. */
    CHECK(timui_tabs_scroll(sp, 3, 2, 8, -5) == 6);
    CHECK(timui_tabs_scroll(sp, 3, 2, 8, 999) == 6);
    /* degenerate viewport width (<=0) does not crash; clamps to >= 0. */
    CHECK(timui_tabs_scroll(sp, 3, 0, 0, 0) >= 0);
}

/* ---- pure visibility: overlap with [scroll, scroll+width) --------------- */
static void test_visible(void){
    TimuiTabSpan s = { 9, 5 };                 /* [9,14) */
    CHECK(!timui_tab_visible(s, 0, 8));        /* [0,8) — no overlap */
    CHECK( timui_tab_visible(s, 6, 8));        /* [6,14) — overlaps */
    CHECK( timui_tab_visible(s, 10, 2));       /* [10,12) — inside */
    /* touching edges: [0,3) vs viewport starting exactly at 3 -> not visible. */
    { TimuiTabSpan e = { 0, 3 };
      CHECK(!timui_tab_visible(e, 3, 5));
      CHECK( timui_tab_visible(e, 2, 5)); }    /* [2,7) overlaps [0,3) */
}

/* ---- interactive widget: click + arrows through a fake transport --------- */
static void test_widget(void){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    const char *L[] = { "one", "two", "three" };   /* spans {0,5},{6,5},{13,7}; total 20 */
    int sel = 0, got;
    TimuiRect r = TIMUI_RECT(0, 0, 20, 1);         /* fits all three tabs */

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 6, &al);

    /* click "two": its box is screen cols [6,11); click cell x=7 (SGR col 8). */
    SETIN(&fake, "\x1b[<0;8;1M");
    timui_begin(ui, &f); timui_tabs(f, TIMUI_ID("T"), r, L, 3, &sel); timui_end(f);
    SETIN(&fake, "\x1b[<0;8;1m");
    timui_begin(ui, &f); got = timui_tabs(f, TIMUI_ID("T"), r, L, 3, &sel); timui_end(f);
    CHECK(sel == 1); CHECK(got == 1);

    /* Right arrow -> 2 (focus is on the bar after the click). */
    SETIN(&fake, "\x1b[C");
    timui_begin(ui, &f); timui_tabs(f, TIMUI_ID("T"), r, L, 3, &sel); timui_end(f);
    CHECK(sel == 2);

    /* Right at the last tab clamps (no wrap past n-1). */
    SETIN(&fake, "\x1b[C");
    timui_begin(ui, &f); timui_tabs(f, TIMUI_ID("T"), r, L, 3, &sel); timui_end(f);
    CHECK(sel == 2);

    /* Left arrow -> 1. */
    SETIN(&fake, "\x1b[D");
    timui_begin(ui, &f); timui_tabs(f, TIMUI_ID("T"), r, L, 3, &sel); timui_end(f);
    CHECK(sel == 1);

    timui_close(ui);
}

/* Overflow + degenerate: a narrow bar with wide content must not crash and must
 * keep the selection in range as the arrows walk to the far tab. */
static void test_widget_overflow(void){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    const char *L[] = { "alpha", "beta", "gamma", "delta" };
    int sel = 3, empty_sel = 5;
    TimuiRect r = TIMUI_RECT(0, 0, 8, 1);          /* far narrower than the tabs */

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 6, &al);

    /* selected at n-1 with heavy overflow: renders without crashing, stays 3. */
    timui_begin(ui, &f);
    CHECK(timui_tabs(f, TIMUI_ID("O"), r, L, 4, &sel) == 3);
    timui_end(f);
    CHECK(sel == 3);

    /* n = 0: no tabs -> selection normalizes to 0, no crash. */
    timui_begin(ui, &f);
    CHECK(timui_tabs(f, TIMUI_ID("O2"), r, L, 0, &empty_sel) == 0);
    timui_end(f);
    CHECK(empty_sel == 0);

    /* NULL selected is tolerated (returns 0, no deref). */
    timui_begin(ui, &f);
    CHECK(timui_tabs(f, TIMUI_ID("O3"), r, L, 4, NULL) == 0);
    timui_end(f);

    timui_close(ui);
}

int main(void){
    test_layout();
    test_scroll();
    test_visible();
    test_widget();
    test_widget_overflow();

    if(failures){ printf("tabs: %d FAILED\n", failures); return 1; }
    printf("tabs: all layout + scroll + widget tests passed\n");
    return 0;
}
