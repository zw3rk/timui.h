/*
 * test_render.c — diff renderer (T3.4).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

TIMUI_TEST(test_render_diff_exact){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer prev, curr;
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiRenderer r;
    TimuiStr out;
    /* CUP(0,0) + SGR reset + fg truecolor white + "Hi"; the adjacent 'i' needs
     * no repositioning and no SGR (same style). */
    static const char expected[] = "\x1b[1;1H\x1b[0m\x1b[38;2;255;255;255mHi";

    timui_cells_init(&prev, 10, 5, &al);
    timui_cells_init(&curr, 10, 5, &al);
    timui_draw_text(&curr, 0, 0, TIMUI_STR_LIT("Hi"), timui_style_make(0xffffff, 0, 0));
    timui_fake_init(&f, &al);
    t = timui_fake_transport(&f);
    timui_renderer_reset(&r);
    timui_render_diff(&t, &prev, &curr, &r);
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len == sizeof(expected) - 1);
    TIMUI_CHECK(out.len == sizeof(expected) - 1 && memcmp(out.ptr, expected, sizeof(expected) - 1) == 0);
    timui_cells_destroy(&prev);
    timui_cells_destroy(&curr);
    timui_fake_destroy(&f);
}

TIMUI_TEST(test_render_unchanged_emits_nothing){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer prev, curr;
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiRenderer r;
    TimuiStr out;
    TimuiStyle s = timui_style_make(0xffffff, 0, 0);
    timui_cells_init(&prev, 10, 5, &al);
    timui_cells_init(&curr, 10, 5, &al);
    timui_draw_text(&prev, 0, 0, TIMUI_STR_LIT("Hi"), s);
    timui_draw_text(&curr, 0, 0, TIMUI_STR_LIT("Hi"), s);
    timui_fake_init(&f, &al);
    t = timui_fake_transport(&f);
    timui_renderer_reset(&r);
    timui_render_diff(&t, &prev, &curr, &r);
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len == 0);    /* identical frames -> no output at all */
    timui_cells_destroy(&prev);
    timui_cells_destroy(&curr);
    timui_fake_destroy(&f);
}

/* V6: narrow->wide must not emit a space that clobbers the wide glyph's 2nd
 * column. The wide glyph's UTF-8 bytes (E3 81 82) contain no 0x20, and neither
 * do CUP/SGR sequences — so a stray space appears iff the bug is present. */
TIMUI_TEST(test_render_diff_narrow_to_wide){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer prev, curr;
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiRenderer r;
    TimuiStr out;
    TimuiStyle s = timui_style_make(0xffffff, 0, 0);
    timui_cells_init(&prev, 6, 1, &al);
    timui_cells_init(&curr, 6, 1, &al);
    timui_draw_text(&prev, 0, 0, TIMUI_STR_LIT("AB"), s);          /* narrow */
    timui_draw_text(&curr, 0, 0, TIMUI_STR_LIT("\xE3\x81\x82"), s); /* あ (wide) */
    timui_fake_init(&f, &al);
    t = timui_fake_transport(&f);
    timui_renderer_reset(&r);
    timui_render_diff(&t, &prev, &curr, &r);
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len > 0);
    TIMUI_CHECK(memchr(out.ptr, ' ', out.len) == NULL);   /* no space clobber */
    timui_cells_destroy(&prev); timui_cells_destroy(&curr); timui_fake_destroy(&f);
}

/* V6: wide->narrow must clear the stale 2nd column. prev has a wide glyph at
 * (0,0) (continuation at (1,0)); curr narrows to 'A' leaving (1,0) empty. The
 * empty cell must be re-emitted as a space to erase the stale half — without
 * the width/continuation-bit in the equality predicate it was skipped. */
TIMUI_TEST(test_render_diff_wide_to_narrow){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer prev, curr;
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiRenderer r;
    TimuiStr out;
    TimuiStyle s = timui_style_make(0xffffff, 0, 0);
    timui_cells_init(&prev, 6, 1, &al);
    timui_cells_init(&curr, 6, 1, &al);
    timui_draw_text(&prev, 0, 0, TIMUI_STR_LIT("\xE3\x81\x82"), s); /* あ (wide) */
    timui_draw_text(&curr, 0, 0, TIMUI_STR_LIT("A"), s);            /* narrow; (1,0) empty */
    timui_fake_init(&f, &al);
    t = timui_fake_transport(&f);
    timui_renderer_reset(&r);
    timui_render_diff(&t, &prev, &curr, &r);
    out = timui_fake_output(&f);
    TIMUI_CHECK(out.len > 0);
    TIMUI_CHECK(memchr(out.ptr, ' ', out.len) != NULL);   /* stale half cleared */
    timui_cells_destroy(&prev); timui_cells_destroy(&curr); timui_fake_destroy(&f);
}

static int r_contains(const char *h, size_t hl, const char *needle){
    size_t nl = strlen(needle), i;
    if(nl == 0 || hl < nl) return 0;
    for(i = 0; i + nl <= hl; i++) if(memcmp(h + i, needle, nl) == 0) return 1;
    return 0;
}

/* W9: a hyperlink whose URI changes between frames (same id/glyph/style) must
 * be re-emitted with the new URI. ids are per-frame indices, so the renderer
 * must compare URIs, not ids. */
TIMUI_TEST(test_render_hyperlink_uri_change){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer empty, bufA, bufB;
    TimuiFakeTransport f;
    TimuiTransport t;
    TimuiRenderer r;
    TimuiStr out;
    TimuiStyle s = timui_style_make(0xffffff, 0, 0);
    uint32_t la, lb;
    timui_cells_init(&empty, 10, 1, &al);
    timui_cells_init(&bufA, 10, 1, &al);
    timui_cells_init(&bufB, 10, 1, &al);
    la = timui_hyperlink_set(&bufA, "https://a.example");
    timui_draw_text_linked(&bufA, 0, 0, TIMUI_STR_LIT("link"), s, la);
    lb = timui_hyperlink_set(&bufB, "https://b.example");
    timui_draw_text_linked(&bufB, 0, 0, TIMUI_STR_LIT("link"), s, lb);
    timui_fake_init(&f, &al);
    t = timui_fake_transport(&f);
    timui_renderer_reset(&r);
    timui_render_diff(&t, &empty, &bufA, &r);   /* frame 1: establish link A */
    timui_fake_clear_output(&f);
    timui_render_diff(&t, &bufA, &bufB, &r);    /* frame 2: URI A->B, same glyph */
    out = timui_fake_output(&f);
    TIMUI_CHECK(r_contains(out.ptr, out.len, "https://b.example"));  /* URI updated */
    timui_cells_destroy(&empty); timui_cells_destroy(&bufA); timui_cells_destroy(&bufB);
    timui_fake_destroy(&f);
}
