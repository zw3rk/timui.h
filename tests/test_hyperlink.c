/*
 * test_hyperlink.c — OSC 8 hyperlinks (T3.6 / v0.2).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

static int bytes_contain(const char *h, size_t hl, const char *needle){
    size_t nl = strlen(needle);
    size_t i;
    if(nl == 0 || hl < nl) return 0;
    for(i = 0; i + nl <= hl; i++) if(memcmp(h + i, needle, nl) == 0) return 1;
    return 0;
}

TIMUI_TEST(test_hyperlink_renders_osc8){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer prev, curr;
    TimuiFakeTransport fake;
    TimuiTransport t;
    TimuiRenderer r;
    TimuiStr out;
    uint32_t id;

    timui_cells_init(&prev, 20, 5, &al);
    timui_cells_init(&curr, 20, 5, &al);
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);

    id = timui_hyperlink_set(&curr, "https://example.com");
    TIMUI_CHECK(id == 1);
    timui_draw_text_linked(&curr, 0, 0, TIMUI_STR_LIT("link"),
                           timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, 0), id);

    timui_renderer_reset(&r);
    timui_render_diff(&t, &prev, &curr, &r);
    out = timui_fake_output(&fake);

    TIMUI_CHECK(out.len > 0);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1b]8;;https://example.com\x1b\\"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "link"));

    timui_cells_destroy(&prev);
    timui_cells_destroy(&curr);
    timui_fake_destroy(&fake);
}

TIMUI_TEST(test_hyperlink_uri_controls_are_sanitized){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer prev, curr;
    TimuiFakeTransport fake;
    TimuiTransport t;
    TimuiRenderer r;
    TimuiStr out;
    uint32_t id;
    static const char safe_open[] = "\x1b]8;;https://ok]52;c;badend\x1b\\";

    timui_cells_init(&prev, 20, 1, &al);
    timui_cells_init(&curr, 20, 1, &al);
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);

    id = timui_hyperlink_set(&curr, "https://ok\x1b]52;c;bad\x07" "\xC2\x9C" "end");
    timui_draw_text_linked(&curr, 0, 0, TIMUI_STR_LIT("link"),
                           timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, 0), id);

    timui_renderer_reset(&r);
    timui_render_diff(&t, &prev, &curr, &r);
    out = timui_fake_output(&fake);

    TIMUI_CHECK(bytes_contain(out.ptr, out.len, safe_open));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b]52;c;bad"));
    TIMUI_CHECK(memchr(out.ptr, '\a', out.len) == NULL);

    timui_cells_destroy(&prev);
    timui_cells_destroy(&curr);
    timui_fake_destroy(&fake);
}
