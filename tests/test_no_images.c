/* test_no_images.c -- TIMUI_NO_IMAGES build-mode contract.
 *
 * TIMUI_NO_IMAGES is API-preserving: applications can keep compiling their
 * image-aware code, but protocol detection and forced image caps resolve to
 * TIMUI_IMAGE_PROTOCOL_NONE and draws use the text fallback.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

#include <stdio.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(cond) do {                                                     \
    checks++;                                                                \
    if(!(cond)){                                                             \
        failures++;                                                          \
        printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
    }                                                                        \
} while(0)

static int bytes_contain(const char *h, size_t hl, const char *needle){
    size_t nl = strlen(needle), i;
    if(!h || nl == 0 || hl < nl) return 0;
    for(i = 0; i + nl <= hl; i++) if(memcmp(h + i, needle, nl) == 0) return 1;
    return 0;
}

static void test_caps_resolve_to_none(void){
    TimuiCaps c;
    memset(&c, 0, sizeof c);
    c.flags = TIMUI_CAP_KITTY_GRAPHICS | TIMUI_CAP_SIXEL_GRAPHICS | TIMUI_CAP_ITERM2_IMAGES;

    CHECK(timui_caps_image_protocol(NULL) == TIMUI_IMAGE_PROTOCOL_NONE);
    CHECK(timui_caps_image_protocol(&c) == TIMUI_IMAGE_PROTOCOL_NONE);

    timui_caps_apply_force(&c,
        TIMUI_CAP_KITTY_GRAPHICS | TIMUI_CAP_SIXEL_GRAPHICS | TIMUI_CAP_ITERM2_IMAGES,
        0);
    CHECK(timui_caps_image_protocol(&c) == TIMUI_IMAGE_PROTOCOL_NONE);
    CHECK(!timui_caps_has(&c, TIMUI_CAP_KITTY_GRAPHICS));
    CHECK(!timui_caps_has(&c, TIMUI_CAP_SIXEL_GRAPHICS));
    CHECK(!timui_caps_has(&c, TIMUI_CAP_ITERM2_IMAGES));
}

static void test_forced_protocols_stay_none(void){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;

    CHECK(timui_fake_init(&fake, &al) == TIMUI_OK);
    t = timui_fake_transport(&fake);
    CHECK(timui_open_for_test(&ui, t, 20, 4, &al) == TIMUI_OK);

    timui_force_cap(ui, TIMUI_CAP_KITTY_GRAPHICS, 1);
    CHECK(timui_image_protocol(ui) == TIMUI_IMAGE_PROTOCOL_NONE);
    CHECK(!timui_caps_has(timui_caps(ui), TIMUI_CAP_KITTY_GRAPHICS));

    timui_force_cap(ui, (TimuiCapFlags)(TIMUI_CAP_TRUECOLOR | TIMUI_CAP_SIXEL_GRAPHICS), 1);
    CHECK(timui_caps_has(timui_caps(ui), TIMUI_CAP_TRUECOLOR));
    CHECK(!timui_caps_has(timui_caps(ui), TIMUI_CAP_SIXEL_GRAPHICS));

    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_KITTY);
    CHECK(timui_image_protocol(ui) == TIMUI_IMAGE_PROTOCOL_NONE);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_SIXEL);
    CHECK(timui_image_protocol(ui) == TIMUI_IMAGE_PROTOCOL_NONE);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_ITERM2);
    CHECK(timui_image_protocol(ui) == TIMUI_IMAGE_PROTOCOL_NONE);

    timui_close(ui);
}

static void test_draw_falls_back_without_image_escapes(void){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    TimuiImage *img;
    TimuiStr out;
    static const unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47 };

    CHECK(timui_fake_init(&fake, &al) == TIMUI_OK);
    t = timui_fake_transport(&fake);
    CHECK(timui_open_for_test(&ui, t, 30, 6, &al) == TIMUI_OK);

    img = timui_image_from_png(ui, png, sizeof png);
    CHECK(img != NULL);

    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_KITTY);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 1));
    CHECK(timui_cells_get(buf, 0, 0)->codepoint == '[');
    CHECK(timui_cells_get(buf, 1, 0)->codepoint == 'i');
    CHECK(timui_cells_get(buf, 2, 0)->codepoint == 'm');
    CHECK(timui_cells_get(buf, 3, 0)->codepoint == 'g');
    CHECK(timui_cells_get(buf, 4, 0)->codepoint == ']');
    timui_end(f);

    out = timui_fake_output(&fake);
    CHECK(!bytes_contain(out.ptr, out.len, "\x1b_G"));
    CHECK(!bytes_contain(out.ptr, out.len, "\x1b]1337;File="));
    CHECK(!bytes_contain(out.ptr, out.len, "\x1bP"));

    timui_image_free(ui, img);
    timui_close(ui);
}

int main(void){
    test_caps_resolve_to_none();
    test_forced_protocols_stay_none();
    test_draw_falls_back_without_image_escapes();

    if(failures){
        printf("%d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf("PASS TIMUI_NO_IMAGES (%d checks)\n", checks);
    return 0;
}
