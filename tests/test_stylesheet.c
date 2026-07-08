/*
 * test_stylesheet.c -- TCSS-like stylesheet parser/resolver.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <stdlib.h>
#include <string.h>

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
