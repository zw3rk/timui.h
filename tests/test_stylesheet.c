/*
 * test_stylesheet.c -- TCSS-like stylesheet parser/resolver.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <stdlib.h>
#include <string.h>

#define SETIN(fake, lit) timui_fake_set_input((fake), (lit), sizeof(lit) - 1)

TIMUI_TEST(test_stylesheet_parse_specificity_states){
    TimuiAllocator al = timui_default_allocator();
    TimuiStylesheet ss;
    TimuiStyle base = timui_style_make(0xAAAAAA, 0x000000, 0);
    TimuiStyleQuery q;
    TimuiResolvedStyle r;
    const char *src =
        "button { fg: #111111; bg: #222222; bold: true; padding: 1; }\n"
        ".primary { fg: #333333; dim: true; }\n"
        "button.primary:focused { fg: #444444; reverse: true; border: round; gap: 2; gradient-lo: #010203; gradient-hi: #040506; }\n"
        "#send { bg: #ABCDEF; }\n";

    memset(&ss, 0, sizeof ss);
    TIMUI_CHECK(timui_stylesheet_parse(&ss, src, strlen(src), &al) == TIMUI_OK);
    q.kind = TIMUI_WIDGET_BUTTON;
    q.id = "send";
    q.classes = "primary danger";
    q.states = TIMUI_STYLE_STATE_FOCUSED;
    q.base = base;
    r = timui_stylesheet_resolve(&ss, q);

    TIMUI_CHECK(r.style.fg == 0x444444);
    TIMUI_CHECK(r.style.bg == 0xABCDEF);
    TIMUI_CHECK((r.style.attrs & (TIMUI_ATTR_BOLD | TIMUI_ATTR_DIM | TIMUI_ATTR_REVERSE)) ==
                (TIMUI_ATTR_BOLD | TIMUI_ATTR_DIM | TIMUI_ATTR_REVERSE));
    TIMUI_CHECK((r.mask & (TIMUI_STYLE_PROP_FG | TIMUI_STYLE_PROP_BG |
                           TIMUI_STYLE_PROP_BORDER | TIMUI_STYLE_PROP_PADDING |
                           TIMUI_STYLE_PROP_GAP | TIMUI_STYLE_PROP_GRADIENT_LO |
                           TIMUI_STYLE_PROP_GRADIENT_HI)) != 0);
    TIMUI_CHECK(r.border == TIMUI_BORDER_ROUND);
    TIMUI_CHECK(r.padding == 1 && r.gap == 2);
    TIMUI_CHECK(r.gradient_lo == 0x010203 && r.gradient_hi == 0x040506);
    timui_stylesheet_free(&ss);
}

TIMUI_TEST(test_stylesheet_source_order_tiebreak){
    TimuiAllocator al = timui_default_allocator();
    TimuiStylesheet ss;
    TimuiStyleQuery q;
    TimuiResolvedStyle r;
    const char *src = "input { fg: #111111; } input { fg: #222222; }";
    memset(&ss, 0, sizeof ss);
    TIMUI_CHECK(timui_stylesheet_parse(&ss, src, strlen(src), &al) == TIMUI_OK);
    q.kind = TIMUI_WIDGET_INPUT;
    q.id = NULL;
    q.classes = NULL;
    q.states = 0;
    q.base = timui_style_make(0xAAAAAA, 0xBBBBBB, 0);
    r = timui_stylesheet_resolve(&ss, q);
    TIMUI_CHECK(r.style.fg == 0x222222);
    TIMUI_CHECK(r.style.bg == 0xBBBBBB);
    timui_stylesheet_free(&ss);
}

TIMUI_TEST(test_stylesheet_state_matching){
    TimuiAllocator al = timui_default_allocator();
    TimuiStylesheet ss;
    TimuiStyleQuery q;
    TimuiResolvedStyle r;
    TimuiStyle base = timui_style_make(0xAAAAAA, 0xBBBBBB, 0);
    const char *src =
        "button { fg: #111111; bg: #222222; }\n"
        "button:hovered { fg: #333333; }\n"
        "button:focused { bg: #444444; }\n";
    memset(&ss, 0, sizeof ss);
    TIMUI_CHECK(timui_stylesheet_parse(&ss, src, strlen(src), &al) == TIMUI_OK);
    q.kind = TIMUI_WIDGET_BUTTON;
    q.id = NULL;
    q.classes = NULL;
    q.states = 0;
    q.base = base;
    r = timui_stylesheet_resolve(&ss, q);
    TIMUI_CHECK(r.style.fg == 0x111111 && r.style.bg == 0x222222);
    q.states = TIMUI_STYLE_STATE_HOVERED;
    r = timui_stylesheet_resolve(&ss, q);
    TIMUI_CHECK(r.style.fg == 0x333333 && r.style.bg == 0x222222);
    q.states = TIMUI_STYLE_STATE_FOCUSED;
    r = timui_stylesheet_resolve(&ss, q);
    TIMUI_CHECK(r.style.fg == 0x111111 && r.style.bg == 0x444444);
    timui_stylesheet_free(&ss);
}

TIMUI_TEST(test_stylesheet_malformed_input){
    TimuiAllocator al = timui_default_allocator();
    TimuiStylesheet ss;
    const char *bad_color = "button { fg: #12; }";
    const char *bad_prop = "button { sparkle: true; }";
    const char *bad_state = "button:levitating { fg: #FFFFFF; }";
    const char *missing_semicolon = "button { fg: #FFFFFF }";
    const char *missing_brace = "button { fg: #FFFFFF;";
    memset(&ss, 0, sizeof ss);
    TIMUI_CHECK(timui_stylesheet_parse(&ss, bad_color, strlen(bad_color), &al) == TIMUI_ERR_PROTOCOL);
    TIMUI_CHECK(timui_stylesheet_parse(&ss, bad_prop, strlen(bad_prop), &al) == TIMUI_ERR_PROTOCOL);
    TIMUI_CHECK(timui_stylesheet_parse(&ss, bad_state, strlen(bad_state), &al) == TIMUI_ERR_PROTOCOL);
    TIMUI_CHECK(timui_stylesheet_parse(&ss, missing_semicolon, strlen(missing_semicolon), &al) == TIMUI_ERR_PROTOCOL);
    TIMUI_CHECK(timui_stylesheet_parse(&ss, missing_brace, strlen(missing_brace), &al) == TIMUI_ERR_PROTOCOL);
}

typedef struct { int fail_alloc; } StyleFailAlloc;
static void *style_fail_alloc(void *ud, size_t sz){
    StyleFailAlloc *fa = (StyleFailAlloc *)ud;
    if(fa->fail_alloc) return NULL;
    return malloc(sz);
}
static void *style_fail_realloc(void *ud, void *p, size_t os, size_t ns){
    (void)ud; (void)os;
    return realloc(p, ns);
}
static void style_fail_free(void *ud, void *p, size_t sz){
    (void)ud; (void)sz;
    free(p);
}

TIMUI_TEST(test_stylesheet_oom){
    StyleFailAlloc fa = { 1 };
    TimuiAllocator al = { &fa, style_fail_alloc, style_fail_realloc, style_fail_free };
    TimuiStylesheet ss;
    const char *src = "button { fg: #FFFFFF; }";
    memset(&ss, 0, sizeof ss);
    TIMUI_CHECK(timui_stylesheet_parse(&ss, src, strlen(src), &al) == TIMUI_ERR_OUT_OF_MEMORY);
    TIMUI_CHECK(ss.rules == NULL && ss.count == 0);
}

TIMUI_TEST(test_stylesheet_applies_button_states){
    TimuiAllocator al = timui_default_allocator();
    TimuiStylesheet ss;
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    const char *src =
        "button { fg: #010203; bg: #111213; }\n"
        "button:hovered { bg: #212223; }\n";
    memset(&ss, 0, sizeof ss);
    TIMUI_CHECK(timui_stylesheet_parse(&ss, src, strlen(src), &al) == TIMUI_OK);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
    timui_set_stylesheet(ui, &ss);

    timui_begin(ui, &f);
    (void)timui_button(f, TIMUI_ID("b"), TIMUI_RECT(0, 2, 12, 1), TIMUI_STR_LIT("Go"));
    buf = timui_frame_buffer(f);
    TIMUI_CHECK(timui_cells_get(buf, 0, 2)->bg == 0x111213);
    TIMUI_CHECK(timui_cells_get(buf, 1, 2)->fg == 0x010203);
    timui_end(f);

    SETIN(&fake, "\x1b[<32;2;3M");  /* motion/hover over the button */
    timui_begin(ui, &f);
    (void)timui_button(f, TIMUI_ID("b"), TIMUI_RECT(0, 2, 12, 1), TIMUI_STR_LIT("Go"));
    buf = timui_frame_buffer(f);
    TIMUI_CHECK(timui_cells_get(buf, 0, 2)->bg == 0x212223);
    timui_end(f);

    timui_close(ui);
    timui_stylesheet_free(&ss);
}

static const char *style_label(void *ud, int idx){
    (void)ud;
    return idx == 0 ? "first" : "second";
}

TIMUI_TEST(test_stylesheet_applies_list_selection){
    TimuiAllocator al = timui_default_allocator();
    TimuiStylesheet ss;
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    TimuiListState st = {1, 0};
    const char *src =
        "listbox { fg: #101010; bg: #202020; }\n"
        "listbox:selected { fg: #303030; bg: #404040; reverse: true; }\n";
    memset(&ss, 0, sizeof ss);
    TIMUI_CHECK(timui_stylesheet_parse(&ss, src, strlen(src), &al) == TIMUI_OK);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
    timui_set_stylesheet(ui, &ss);

    timui_begin(ui, &f);
    (void)timui_listbox(f, TIMUI_ID("list"), TIMUI_RECT(0, 0, 20, 3), st, 2, style_label, NULL);
    buf = timui_frame_buffer(f);
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->fg == 0x101010);
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->bg == 0x202020);
    TIMUI_CHECK(timui_cells_get(buf, 0, 1)->fg == 0x303030);
    TIMUI_CHECK(timui_cells_get(buf, 0, 1)->bg == 0x404040);
    TIMUI_CHECK((timui_cells_get(buf, 0, 1)->attrs & TIMUI_ATTR_REVERSE) != 0);
    timui_end(f);

    timui_close(ui);
    timui_stylesheet_free(&ss);
}

TIMUI_TEST(test_stylesheet_explicit_label_ignores_sheet){
    TimuiAllocator al = timui_default_allocator();
    TimuiStylesheet ss;
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    TimuiStyle explicit_style = timui_style_make(0xABCDEF, 0x123456, 0);
    const char *src = "label { fg: #000000; bg: #FFFFFF; reverse: true; }";
    memset(&ss, 0, sizeof ss);
    TIMUI_CHECK(timui_stylesheet_parse(&ss, src, strlen(src), &al) == TIMUI_OK);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
    timui_set_stylesheet(ui, &ss);

    timui_begin(ui, &f);
    timui_label(f, 0, 0, TIMUI_STR_LIT("Label"), explicit_style);
    buf = timui_frame_buffer(f);
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->fg == 0xABCDEF);
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->bg == 0x123456);
    TIMUI_CHECK((timui_cells_get(buf, 0, 0)->attrs & TIMUI_ATTR_REVERSE) == 0);
    timui_end(f);

    timui_close(ui);
    timui_stylesheet_free(&ss);
}

TIMUI_TEST(test_stylesheet_clear_and_explicit_input_style){
    TimuiAllocator al = timui_default_allocator();
    TimuiStylesheet ss;
    TimuiTheme th = timui_theme_builtin(TIMUI_THEME_DOS_BLUE);
    TimuiStyle theme_button = timui_theme_style(&th, TIMUI_SLOT_BUTTON);
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    char text[16] = "hi";
    TimuiInputState is = { text, sizeof text, 2, 0 };
    TimuiStyle explicit_style = timui_style_make(0xABCDEF, 0x123456, 0);
    const char *src = "button { bg: #010203; } input { bg: #FF0000; }";
    memset(&ss, 0, sizeof ss);
    TIMUI_CHECK(timui_stylesheet_parse(&ss, src, strlen(src), &al) == TIMUI_OK);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
    timui_set_stylesheet(ui, &ss);

    timui_begin(ui, &f);
    (void)timui_input_field_styled(f, TIMUI_ID("in"), TIMUI_RECT(0, 1, 8, 1), &is, explicit_style);
    (void)timui_button(f, TIMUI_ID("b"), TIMUI_RECT(0, 3, 8, 1), TIMUI_STR_LIT("Go"));
    buf = timui_frame_buffer(f);
    TIMUI_CHECK(timui_cells_get(buf, 0, 1)->bg == 0x123456);
    TIMUI_CHECK(timui_cells_get(buf, 0, 3)->bg == 0x010203);
    timui_end(f);

    timui_set_stylesheet(ui, NULL);
    timui_begin(ui, &f);
    (void)timui_button(f, TIMUI_ID("b"), TIMUI_RECT(0, 3, 8, 1), TIMUI_STR_LIT("Go"));
    buf = timui_frame_buffer(f);
    TIMUI_CHECK(timui_cells_get(buf, 0, 3)->bg == theme_button.bg);
    timui_end(f);

    timui_close(ui);
    timui_stylesheet_free(&ss);
}
