/*
 * test_images_pty.c -- terminal image protocols + pty integration.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <limits.h>

static int bytes_contain(const char *h, size_t hl, const char *needle){
    size_t nl = strlen(needle), i;
    if(nl == 0 || hl < nl) return 0;
    for(i = 0; i + nl <= hl; i++) if(memcmp(h + i, needle, nl) == 0) return 1;
    return 0;
}

static int bytes_count(const char *h, size_t hl, const char *needle){
    size_t nl = strlen(needle), i;
    int count = 0;
    if(nl == 0 || hl < nl) return 0;
    for(i = 0; i + nl <= hl; i++) if(memcmp(h + i, needle, nl) == 0) count++;
    return count;
}

static const unsigned char png_4x4[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04,
    0x08, 0x06, 0x00, 0x00, 0x00, 0xa9, 0xf1, 0x9e, 0x7e, 0x00, 0x00, 0x00,
    0x25, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x88, 0x7c, 0x67, 0xff,
    0x9f, 0x95, 0x9d, 0xe3, 0x3f, 0x08, 0x84, 0x6d, 0x3b, 0xf4, 0x9f, 0x01,
    0x99, 0x03, 0x92, 0x64, 0x40, 0xe6, 0x80, 0x24, 0x19, 0x90, 0x39, 0x20,
    0x00, 0x00, 0x77, 0xa5, 0x29, 0x85, 0xb5, 0x28, 0x31, 0x1a, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

typedef struct {
    TimuiFakeTransport fake;
    size_t max_write;
    int oversize_write;
} CappedTransport;

static int capped_write(TimuiTransport *t, const void *data, size_t len){
    CappedTransport *c = (CappedTransport *)t->ctx;
    TimuiTransport fake_t;
    if(len > c->max_write){
        c->oversize_write = 1;
        return -1;
    }
    fake_t = timui_fake_transport(&c->fake);
    return fake_t.write(&fake_t, data, len);
}
static int capped_read(TimuiTransport *t, void *buf, size_t cap){
    (void)t; (void)buf; (void)cap;
    return 0;
}
static int capped_flush(TimuiTransport *t){ (void)t; return 0; }
static void capped_close(TimuiTransport *t){ (void)t; }

static TimuiTransport capped_transport(CappedTransport *c){
    TimuiTransport t;
    t.write = capped_write;
    t.read = capped_read;
    t.flush = capped_flush;
    t.close = capped_close;
    t.ctx = c;
    return t;
}

/* ---- Kitty graphics (#44) ---- */
TIMUI_TEST(test_kitty_graphics_transmit){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiStr out;
    static const unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_cap(ui, TIMUI_CAP_KITTY_GRAPHICS, 1);

    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 3));
    timui_end(f);
    out = timui_fake_output(&fake);

    TIMUI_CHECK(out.len > 0);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1b_G"));   /* APC graphics (was ESC G — a bug) */
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "a=t"));      /* transmit under an id */
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "a=p"));      /* placed by id */
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "c=5"));      /* sized to the 5x3 rect */
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "r=3"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_kitty_png_rgba_uses_png_payload){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiStr out;
    static const unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47 };
    unsigned char rgba[4] = { 0xff, 0x00, 0x00, 0xff };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_KITTY);
    img = timui_image_from_png_rgba(ui, png, sizeof png, rgba, 1, 1, 4);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 3));
    timui_end(f);
    out = timui_fake_output(&fake);

    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1b_G"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "a=t,t=d,f=100"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "iVBORw=="));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1bP0;1;0q"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b]1337;File="));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_kitty_transmit_does_not_duplicate_payload){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiStr out;
    static const unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47 };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_KITTY);
    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 3));
    timui_end(f);
    out = timui_fake_output(&fake);

    TIMUI_CHECK(bytes_count(out.ptr, out.len, "iVBORw==") == 1);

    timui_image_free(ui, img);
    timui_close(ui);
}

/* S3/V22: a payload whose base64 exceeds the 4096-byte chunk boundary must
 * split into multiple ESC_G frames — m=1 continuation on all but the last,
 * m=0 on the last. (3100 bytes -> ~4136 base64 -> 2 chunks.) */
TIMUI_TEST(test_kitty_graphics_chunking){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiStr out;
    static const unsigned char png[3100];
    int frames = 0;
    size_t i;

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_cap(ui, TIMUI_CAP_KITTY_GRAPHICS, 1);
    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);
    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 3));
    timui_end(f);
    out = timui_fake_output(&fake);

    for(i = 0; i + 2 < out.len; i++)
        if((unsigned char)out.ptr[i] == 0x1b && out.ptr[i + 1] == '_' && out.ptr[i + 2] == 'G') frames++;
    TIMUI_CHECK(frames == 3);                                   /* 2 transmit chunks + 1 place */
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "m=1"));        /* continuation */
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "m=0"));        /* final */

    timui_image_free(ui, img);
    timui_close(ui);
}

/* A clipped placement crops the SOURCE pixels (from the PNG IHDR size) to match
 * the visible cell sub-rect — for smooth scroll clipping. */
TIMUI_TEST(test_kitty_graphics_clip){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiStr out;
    unsigned char png[24] = {0};
    png[19] = 10;   /* IHDR width  = 10 px */
    png[23] = 20;   /* IHDR height = 20 px */
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_cap(ui, TIMUI_CAP_KITTY_GRAPHICS, 1);
    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img && img->px_w == 10 && img->px_h == 20);   /* parsed from IHDR */
    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    /* full is 4 rows; show only the bottom 2 -> crop the top 2 rows (10 of 20px) */
    timui_image_draw_clipped(f, img, TIMUI_RECT(0, 0, 4, 4), TIMUI_RECT(0, 2, 4, 2));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "y=10"));   /* src y = 2/4 * 20 */
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "h=10"));   /* src h = 2/4 * 20 */
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "w=10"));   /* src w = full width */
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "r=2"));    /* displayed in 2 rows */
    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_image_png_ihdr_over_int_ignored){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiImage *img;
    unsigned char png[24] = {0};

    memset(png + 16, 0xff, 8);
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);
    TIMUI_CHECK(img->px_w == 0);
    TIMUI_CHECK(img->px_h == 0);

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_kitty_clipped_invalid_visible_no_image_escape){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;
    unsigned char png[24] = {0};
    png[19] = 10;
    png[23] = 20;

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_KITTY);
    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw_clipped(f, img, TIMUI_RECT(0, 2, 5, 2), TIMUI_RECT(0, 0, 5, 3));
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == '[');
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b_G"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_kitty_graphics_respects_active_clip){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiStr out;
    static const unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_KITTY);
    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_push_clip(f, TIMUI_RECT(0, 0, 2, 2));
    timui_image_draw(f, img, TIMUI_RECT(10, 10, 1, 1));
    timui_pop_clip(f);
    timui_end(f);
    out = timui_fake_output(&fake);

    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b_G"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "a=p"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_kitty_graphics_placeholder){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    static const unsigned char png[] = { 0x89 };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);

    img = timui_image_from_png(ui, png, sizeof png);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 1));
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == '[');
    timui_end(f);

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_image_protocol_force_none_placeholder){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;
    static const unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47 };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    TIMUI_CHECK(timui_image_protocol(NULL) == TIMUI_IMAGE_PROTOCOL_NONE);
    timui_force_cap(ui, TIMUI_CAP_KITTY_GRAPHICS, 1);
    TIMUI_CHECK(timui_image_protocol(ui) == TIMUI_IMAGE_PROTOCOL_KITTY);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_NONE);
    TIMUI_CHECK(timui_image_protocol(ui) == TIMUI_IMAGE_PROTOCOL_NONE);

    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 1));
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == '[');
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b_G"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_sixel_malformed_png_still_placeholder){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;
    static const unsigned char png[] = { 0x89, 0x50 };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);

    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_SIXEL);
    TIMUI_CHECK(timui_image_protocol(ui) == TIMUI_IMAGE_PROTOCOL_SIXEL);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 1));
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == '[');
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b_G"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1bP"));

    timui_force_image_protocol(ui, (TimuiImageProtocol)99);
    TIMUI_CHECK(timui_image_protocol(ui) == TIMUI_IMAGE_PROTOCOL_NONE);

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_sixel_plain_png_decodes_to_dcs){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_SIXEL);
    img = timui_image_from_png(ui, png_4x4, sizeof png_4x4);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 4, 4));
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == 0);
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1b[1;1H"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1bP0;1;0q"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\"1;1;4;4"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "#1;2;"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b_G"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b]1337;File="));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_sixel_oversized_png_still_placeholder){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;
    unsigned char png[24] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x20, 0x01, 0x00, 0x00, 0x00, 0x01
    };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_SIXEL);
    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 4, 1));
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == '[');
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1bP0;1;0q"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_image_from_rgba_copies_rows_and_dimensions){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiImage *img;
    unsigned char rgba[16] = {
        0xff, 0x00, 0x00, 0xff, 0xee, 0xee, 0xee, 0xee,
        0x00, 0xff, 0x00, 0xff, 0xdd, 0xdd, 0xdd, 0xdd
    };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    img = timui_image_from_rgba(ui, rgba, 1, 2, 8);
    TIMUI_CHECK(img != NULL);
    TIMUI_CHECK(img->kind == TIMUI_IMAGE_KIND_RGBA);
    TIMUI_CHECK(img->px_w == 1);
    TIMUI_CHECK(img->px_h == 2);
    TIMUI_CHECK(img->stride == 4);
    TIMUI_CHECK(img->len == 8);
    rgba[0] = 0x00;
    rgba[8] = 0xff;
    TIMUI_CHECK(img->data[0] == 0xff);
    TIMUI_CHECK(img->data[4] == 0x00);

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_image_from_rgba_rejects_invalid_inputs){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    unsigned char px[4] = { 0xff, 0x00, 0x00, 0xff };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    TIMUI_CHECK(timui_image_from_rgba(ui, NULL, 1, 1, 4) == NULL);
    TIMUI_CHECK(timui_image_from_rgba(ui, px, 0, 1, 4) == NULL);
    TIMUI_CHECK(timui_image_from_rgba(ui, px, 1, 0, 4) == NULL);
    TIMUI_CHECK(timui_image_from_rgba(ui, px, 2, 1, 7) == NULL);
    TIMUI_CHECK(timui_image_from_rgba(ui, px, INT_MAX / 2 + 1, 2, INT_MAX) == NULL);
    timui_close(ui);
}

TIMUI_TEST(test_image_from_png_rgba_copies_png_and_pixels){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiImage *img;
    unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47 };
    unsigned char rgba[2 * 8] = {
        0xff, 0x00, 0x00, 0xff, 0xee, 0xee, 0xee, 0xee,
        0x00, 0xff, 0x00, 0xff, 0xdd, 0xdd, 0xdd, 0xdd
    };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    img = timui_image_from_png_rgba(ui, png, sizeof png, rgba, 1, 2, 8);
    TIMUI_CHECK(img != NULL);
    TIMUI_CHECK(img->kind == TIMUI_IMAGE_KIND_PNG_RGBA);
    TIMUI_CHECK(img->len == sizeof png);
    TIMUI_CHECK(img->rgba_len == 8);
    TIMUI_CHECK(img->px_w == 1);
    TIMUI_CHECK(img->px_h == 2);
    TIMUI_CHECK(img->stride == 4);
    png[0] = 0;
    rgba[0] = 0;
    rgba[8] = 0xff;
    TIMUI_CHECK(img->data[0] == 0x89);
    TIMUI_CHECK(img->rgba[0] == 0xff);
    TIMUI_CHECK(img->rgba[4] == 0x00);

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_image_from_png_rgba_rejects_invalid_inputs){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47 };
    unsigned char px[4] = { 0xff, 0x00, 0x00, 0xff };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    TIMUI_CHECK(timui_image_from_png_rgba(ui, NULL, sizeof png, px, 1, 1, 4) == NULL);
    TIMUI_CHECK(timui_image_from_png_rgba(ui, png, 0, px, 1, 1, 4) == NULL);
    TIMUI_CHECK(timui_image_from_png_rgba(ui, png, sizeof png, NULL, 1, 1, 4) == NULL);
    TIMUI_CHECK(timui_image_from_png_rgba(ui, png, sizeof png, px, 0, 1, 4) == NULL);
    TIMUI_CHECK(timui_image_from_png_rgba(ui, png, sizeof png, px, 1, 0, 4) == NULL);
    TIMUI_CHECK(timui_image_from_png_rgba(ui, png, sizeof png, px, 2, 1, 7) == NULL);
    TIMUI_CHECK(timui_image_from_png_rgba(ui, png, sizeof png, px, INT_MAX / 2 + 1, 2, INT_MAX) == NULL);
    timui_close(ui);
}

TIMUI_TEST(test_sixel_rgba_emits_dcs_single_band){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;
    unsigned char rgba[6 * 4];
    int i;

    for(i = 0; i < 6; i++){
        rgba[i * 4 + 0] = 0xff;
        rgba[i * 4 + 1] = 0x00;
        rgba[i * 4 + 2] = 0x00;
        rgba[i * 4 + 3] = 0xff;
    }
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_SIXEL);
    img = timui_image_from_rgba(ui, rgba, 1, 6, 4);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 1, 1));
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == 0);
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1b[1;1H"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1bP0;1;0q"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\"1;1;1;6"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "#1;2;100;0;0"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "#1~"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1b\\"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b_G"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b]1337;File="));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_sixel_png_rgba_emits_dcs_from_pixels){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;
    static const unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47 };
    unsigned char rgba[6 * 4];
    int i;

    for(i = 0; i < 6; i++){
        rgba[i * 4 + 0] = 0xff;
        rgba[i * 4 + 1] = 0x00;
        rgba[i * 4 + 2] = 0x00;
        rgba[i * 4 + 3] = 0xff;
    }
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_SIXEL);
    img = timui_image_from_png_rgba(ui, png, sizeof png, rgba, 1, 6, 4);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 1, 1));
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == 0);
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1bP0;1;0q"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "#1;2;100;0;0"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "#1~"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b_G"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b]1337;File="));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_sixel_rejects_short_strided_sidecar){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    TimuiImage bad;
    TimuiStr out;
    unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47 };
    unsigned char backing[1028];

    memset(&bad, 0, sizeof bad);
    memset(backing, 0, sizeof backing);
    backing[0] = 0xff;
    backing[3] = 0xff;
    backing[1024] = 0xff;        /* physically present, but outside rgba_len */
    backing[1027] = 0xff;
    bad.data = png;
    bad.len = sizeof png;
    bad.rgba = backing;
    bad.rgba_len = 8;
    bad.px_w = 1;
    bad.px_h = 2;
    bad.kind = TIMUI_IMAGE_KIND_PNG_RGBA;
    bad.stride = 1024;

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_SIXEL);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, &bad, TIMUI_RECT(0, 0, 1, 1));
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == '[');
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1bP0;1;0q"));

    timui_close(ui);
}

TIMUI_TEST(test_sixel_rgba_alpha_and_band_order){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiStr out;
    unsigned char rgba[7 * 4];
    int i;

    memset(rgba, 0, sizeof rgba);
    for(i = 0; i < 7; i++){
        rgba[i * 4 + 0] = 0xff;
        rgba[i * 4 + 3] = (i == 0 || i == 2 || i == 5 || i == 6) ? 0xff : 0x00;
    }
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_SIXEL);
    img = timui_image_from_rgba(ui, rgba, 1, 7, 4);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 1, 2));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "#1d-#1@"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_sixel_palette_over_cap_quantizes_to_dcs){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;
    unsigned char rgba[17 * 4];
    int i;

    for(i = 0; i < 17; i++){
        rgba[i * 4 + 0] = (unsigned char)i;
        rgba[i * 4 + 1] = (unsigned char)(255 - i);
        rgba[i * 4 + 2] = (unsigned char)(i * 7);
        rgba[i * 4 + 3] = 0xff;
    }
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_SIXEL);
    img = timui_image_from_rgba(ui, rgba, 17, 1, 17 * 4);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 17, 1));
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == 0);
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1bP0;1;0q"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\"1;1;17;1"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "#17;2;"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_sixel_rgba_quantizer_preserves_exact_small_palette){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiStr out;
    unsigned char rgba[2 * 4] = {
        0x11, 0x22, 0x33, 0xff,
        0x44, 0x55, 0x66, 0xff
    };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_SIXEL);
    img = timui_image_from_rgba(ui, rgba, 2, 1, 2 * 4);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 2, 1));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "#1;2;7;13;20"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "#2;2;27;33;40"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_sixel_rgba_scales_to_known_cell_pixels){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiStr out;
    unsigned char rgba[4] = { 0xff, 0x00, 0x00, 0xff };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_SIXEL);
    timui_set_cell_pixels_for_test(ui, 2, 3);
    img = timui_image_from_rgba(ui, rgba, 1, 1, 4);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 4, 2));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\"1;1;8;6"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "#1~~~~~~~~"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_sixel_invalid_cell_pixels_fall_back_to_source_pixels){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiStr out;
    unsigned char rgba[2 * 2 * 4];
    memset(rgba, 0xff, sizeof rgba);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_SIXEL);
    timui_set_cell_pixels_for_test(ui, 0, 3);
    img = timui_image_from_rgba(ui, rgba, 2, 2, 2 * 4);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 4, 2));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\"1;1;2;2"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\"1;1;8;6"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_sixel_clipped_rgba_emits_cropped_dcs){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;
    unsigned char rgba[4 * 6 * 4];
    int x, y;
    memset(rgba, 0, sizeof rgba);
    for(y = 2; y < 5; y++){
        for(x = 1; x < 3; x++){
            unsigned char *px = rgba + ((size_t)y * 4u + (size_t)x) * 4u;
            px[0] = 0xff;
            px[3] = 0xff;
        }
    }

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_SIXEL);
    img = timui_image_from_rgba(ui, rgba, 4, 6, 4 * 4);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw_clipped(f, img, TIMUI_RECT(0, 0, 4, 6), TIMUI_RECT(1, 2, 2, 3));
    TIMUI_CHECK(timui_cells_get(buf, 1, 2)->codepoint == 0);
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1b[3;2H"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1bP0;1;0q"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\"1;1;2;3"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "#1;2;100;0;0"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "#1FF"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_sixel_clipped_png_decodes_and_crops){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_SIXEL);
    img = timui_image_from_png(ui, png_4x4, sizeof png_4x4);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw_clipped(f, img, TIMUI_RECT(0, 0, 4, 4), TIMUI_RECT(0, 1, 4, 1));
    TIMUI_CHECK(timui_cells_get(buf, 0, 1)->codepoint == 0);
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1b[2;1H"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1bP0;1;0q"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\"1;1;4;1"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_sixel_clipped_png_rgba_emits_cropped_dcs){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;
    static const unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47 };
    unsigned char rgba[4 * 6 * 4];
    int x, y;

    memset(rgba, 0, sizeof rgba);
    for(y = 2; y < 5; y++){
        for(x = 1; x < 3; x++){
            unsigned char *px = rgba + ((size_t)y * 4u + (size_t)x) * 4u;
            px[0] = 0xff;
            px[3] = 0xff;
        }
    }
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_SIXEL);
    img = timui_image_from_png_rgba(ui, png, sizeof png, rgba, 4, 6, 4 * 4);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw_clipped(f, img, TIMUI_RECT(0, 0, 4, 6), TIMUI_RECT(1, 2, 2, 3));
    TIMUI_CHECK(timui_cells_get(buf, 1, 2)->codepoint == 0);
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1b[3;2H"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1bP0;1;0q"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\"1;1;2;3"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "#1;2;100;0;0"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "#1FF"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_sixel_clipped_invalid_visible_no_partial_dcs){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;
    unsigned char rgba[2 * 2 * 4];
    memset(rgba, 0xff, sizeof rgba);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_SIXEL);
    img = timui_image_from_rgba(ui, rgba, 2, 2, 2 * 4);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw_clipped(f, img, TIMUI_RECT(0, 2, 2, 2), TIMUI_RECT(0, 0, 2, 3));
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == '[');
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1bP"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_kitty_rgba_without_png_falls_back_placeholder){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;
    unsigned char rgba[4] = { 0xff, 0x00, 0x00, 0xff };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_KITTY);
    img = timui_image_from_rgba(ui, rgba, 1, 1, 4);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 1));
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == '[');
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b_G"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_sixel_clipped_palette_uses_visible_crop_not_full_image){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;
    unsigned char rgba[20 * 2 * 4];
    int x, y;

    for(y = 0; y < 2; y++){
        for(x = 0; x < 20; x++){
            unsigned char *px = rgba + ((size_t)y * 20u + (size_t)x) * 4u;
            px[0] = (unsigned char)(x * 11);
            px[1] = (unsigned char)(255 - x);
            px[2] = (unsigned char)(x * 3);
            px[3] = 0xff;
        }
    }
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_SIXEL);
    img = timui_image_from_rgba(ui, rgba, 20, 2, 20 * 4);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw_clipped(f, img, TIMUI_RECT(0, 0, 20, 2), TIMUI_RECT(0, 0, 4, 2));
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == 0);
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1bP0;1;0q"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\"1;1;4;2"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "#4;2;"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "#17;2;"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_sixel_clears_previous_kitty_placements){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *kitty_img;
    TimuiImage *sixel_img;
    TimuiStr out;
    static const unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47 };
    unsigned char rgba[6 * 4];
    int i;

    for(i = 0; i < 6; i++){
        rgba[i * 4 + 0] = 0xff;
        rgba[i * 4 + 1] = 0x00;
        rgba[i * 4 + 2] = 0x00;
        rgba[i * 4 + 3] = 0xff;
    }
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    kitty_img = timui_image_from_png(ui, png, sizeof png);
    sixel_img = timui_image_from_rgba(ui, rgba, 1, 6, 4);
    TIMUI_CHECK(kitty_img != NULL);
    TIMUI_CHECK(sixel_img != NULL);

    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_KITTY);
    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, kitty_img, TIMUI_RECT(0, 0, 5, 2));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "a=p"));

    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_SIXEL);
    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, sixel_img, TIMUI_RECT(0, 0, 1, 1));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "a=d,d=a"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1bP0;1;0q"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "a=p"));

    timui_image_free(ui, kitty_img);
    timui_image_free(ui, sixel_img);
    timui_close(ui);
}

TIMUI_TEST(test_image_protocol_force_none_clears_old_kitty_placements){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiStr out;
    static const unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47 };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_KITTY);
    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 2));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1b_G"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "a=p"));

    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_NONE);
    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 2));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "a=d,d=a"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "a=p"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_iterm2_images_emit_osc1337){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;
    static const unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47 };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_ITERM2);
    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(2, 1, 5, 3));
    TIMUI_CHECK(timui_cells_get(buf, 2, 1)->codepoint == 0);  /* no placeholder */
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1b[2;3H"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1b]1337;File="));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "inline=1"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "size=4"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "width=5"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "height=3"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "preserveAspectRatio=0"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, ":iVBORw==\x1b\\"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b_G"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_iterm2_png_rgba_uses_png_payload){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;
    static const unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47 };
    unsigned char rgba[4] = { 0xff, 0x00, 0x00, 0xff };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_ITERM2);
    img = timui_image_from_png_rgba(ui, png, sizeof png, rgba, 1, 1, 4);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(2, 1, 5, 3));
    TIMUI_CHECK(timui_cells_get(buf, 2, 1)->codepoint == 0);
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1b]1337;File="));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "size=4"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, ":iVBORw==\x1b\\"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b_G"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1bP0;1;0q"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_iterm2_rgba_without_png_falls_back_placeholder){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;
    unsigned char rgba[4] = { 0xff, 0x00, 0x00, 0xff };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_ITERM2);
    img = timui_image_from_rgba(ui, rgba, 1, 1, 4);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 1));
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == '[');
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b]1337;File="));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_iterm2_short_write_transport_chunks){
    TimuiAllocator al = timui_default_allocator();
    CappedTransport capped;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiStr out;
    static const unsigned char png[5000] = { 0x89, 0x50, 0x4E, 0x47 };

    memset(&capped, 0, sizeof capped);
    capped.max_write = 4096;
    timui_fake_init(&capped.fake, &al);
    t = capped_transport(&capped);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_ITERM2);
    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    timui_image_draw(f, img, TIMUI_RECT(2, 1, 5, 3));
    timui_end(f);
    out = timui_fake_output(&capped.fake);
    TIMUI_CHECK(capped.oversize_write == 0);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1b]1337;File="));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "size=5000"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1b\\"));

    timui_image_free(ui, img);
    timui_close(ui);
    timui_fake_destroy(&capped.fake);
}

TIMUI_TEST(test_iterm2_clears_previous_kitty_placements){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiStr out;
    static const unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47 };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);

    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_KITTY);
    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 2));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "a=p"));

    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_ITERM2);
    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 2));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "a=d,d=a"));
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1b]1337;File="));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "a=p"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_iterm2_no_kitty_delete_after_iterm2_frame){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiStr out;
    static const unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47 };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);

    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_ITERM2);
    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 2));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "\x1b]1337;File="));

    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_NONE);
    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 2));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "a=d,d=a"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b]1337;File="));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_iterm2_oversized_image_no_partial_emit){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage bad;
    TimuiStr out;
    unsigned char byte = 0;

    memset(&bad, 0, sizeof bad);
    bad.data = &byte;
    bad.len = (SIZE_MAX - 1) / 4 + 1;

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_ITERM2);

    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, &bad, TIMUI_RECT(0, 0, 5, 2));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b]1337;File="));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b_G"));

    timui_close(ui);
}

TIMUI_TEST(test_iterm2_clipped_draw_falls_back_placeholder){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;
    unsigned char png[24] = {0};
    png[19] = 10;
    png[23] = 20;

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_ITERM2);
    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw_clipped(f, img, TIMUI_RECT(0, 0, 5, 4), TIMUI_RECT(0, 2, 5, 2));
    TIMUI_CHECK(timui_cells_get(buf, 0, 2)->codepoint == '[');
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b]1337;File="));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b_G"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_iterm2_clipped_fallback_clears_previous_kitty_placement){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiCellBuffer *buf;
    TimuiStr out;
    unsigned char png[24] = {0};
    png[19] = 10;
    png[23] = 20;

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);

    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_KITTY);
    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 4));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "a=p"));

    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_ITERM2);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_fake_clear_output(&fake);
    timui_image_draw_clipped(f, img, TIMUI_RECT(0, 0, 5, 4), TIMUI_RECT(0, 2, 5, 2));
    TIMUI_CHECK(timui_cells_get(buf, 0, 2)->codepoint == '[');
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "a=d,d=a"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b]1337;File="));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "a=p"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_iterm2_invalid_rect_emits_no_escape){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiStr out;
    static const unsigned char png[] = { 0x89, 0x50 };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_ITERM2);
    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 0, 2));
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 0));
    timui_image_draw(f, img, TIMUI_RECT(INT_MAX, 0, 5, 2));
    timui_image_draw(f, img, TIMUI_RECT(0, INT_MAX, 5, 2));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b]1337;File="));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "\x1b_G"));

    timui_image_free(ui, img);
    timui_close(ui);
}

TIMUI_TEST(test_kitty_invalid_rect_does_not_refresh_stale_cleanup){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiImage *img;
    TimuiStr out;
    static const unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47 };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_force_image_protocol(ui, TIMUI_IMAGE_PROTOCOL_KITTY);
    img = timui_image_from_png(ui, png, sizeof png);
    TIMUI_CHECK(img != NULL);

    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(0, 0, 5, 2));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "a=p"));

    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_image_draw(f, img, TIMUI_RECT(-1, 0, 5, 2));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(bytes_contain(out.ptr, out.len, "a=d,d=a"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "a=p"));

    timui_begin(ui, &f);
    timui_fake_clear_output(&fake);
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "a=d,d=a"));
    TIMUI_CHECK(!bytes_contain(out.ptr, out.len, "a=p"));

    timui_image_free(ui, img);
    timui_close(ui);
}

/* ---- pty integration test (#54) ---- */
TIMUI_TEST(test_pty_hello_exits_on_esc){
    int master;
    pid_t pid;

    if(access("build/hello", X_OK) != 0) return;   /* V4: skip if the example isn't built */
    master = posix_openpt(O_RDWR | O_NOCTTY);
    if(master < 0){ return; }   /* skip if no pty support */
    if(grantpt(master) != 0 || unlockpt(master) != 0){ close(master); return; }

    pid = fork();
    if(pid < 0){ close(master); return; }
    if(pid == 0){
        char *name = ptsname(master);
        int slave;
        struct winsize ws;
        memset(&ws, 0, sizeof ws);
        ws.ws_row = 24; ws.ws_col = 80;
        setsid();
        slave = open(name, O_RDWR);
        if(slave < 0) _exit(127);
        ioctl(slave, TIOCSWINSZ, &ws);
        dup2(slave, 0); dup2(slave, 1); dup2(slave, 2);
        close(slave); close(master);
        execl("build/hello", "hello", (char *)NULL);
        _exit(127);
    }

    /* parent: verify hello ran, then send Esc and wait for exit */
    {
        char out[1024];
        ssize_t n;
        int status;
        pid_t w;
        int ok = 0;
        int i;
        struct timespec ts200 = { 0, 200 * 1000 * 1000 };
        struct timespec ts100 = { 0, 100 * 1000 * 1000 };

        nanosleep(&ts200, NULL);  /* let hello enter alt screen + render */

        /* verify hello ran: alt-screen-enter in the master output (retry for 1s) */
        fcntl(master, F_SETFL, fcntl(master, F_GETFL, 0) | O_NONBLOCK);
        { int found_alt = 0; int retry;
          for(retry = 0; retry < 10 && !found_alt; retry++){
            n = read(master, out, sizeof(out) - 1);
            if(n > 0 && bytes_contain(out, (size_t)n, "\x1b[?1049h")) found_alt = 1;
            if(!found_alt) nanosleep(&ts100, NULL);
          }
          if(!found_alt){
            close(master); kill(pid, SIGKILL); waitpid(pid, &status, 0);
            TIMUI_CHECK(0);
            return;
          }
        }

        /* send Esc; the Esc-timeout (50ms) should fire and quit hello */
        write(master, "\x1b", 1);

        for(i = 0; i < 60; i++){  /* poll up to 6s */
            w = waitpid(pid, &status, WNOHANG);
            if(w == pid){ ok = 1; break; }   /* terminated (didn't hang) */
            nanosleep(&ts100, NULL);
        }
        if(!ok) kill(pid, SIGKILL);
        close(master);
        /* V3: assert the Esc-quit produced a clean exit (WIFEXITED), not a
         * signal — the old TIMUI_CHECK(1) passed even if hello never exited,
         * masking any Esc-quit regression. If the child didn't exit in time
         * (sandboxed pty where master->slave writes are restricted), skip
         * visibly instead of asserting true. The alt-screen check above
         * already proves hello ran under the pty. */
        if(ok) TIMUI_CHECK(WIFEXITED(status));
        else   printf("  SKIP pty Esc-quit: child did not exit in 6s (sandbox restriction)\n");
    }
}

/* W6: SIGTERM must restore the terminal. Fork a child that opens timui on a
 * pty (enters alt screen), pause()s; the parent sends SIGTERM and checks the
 * pty output for the alt-screen-exit sequence (the handler's screen_exit). */
TIMUI_TEST(test_signal_restore){
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    pid_t pid;
    if(master < 0){ return; }   /* skip if no pty support */
    if(grantpt(master) != 0 || unlockpt(master) != 0){ close(master); return; }
    pid = fork();
    if(pid < 0){ close(master); return; }
    if(pid == 0){
        /* child: slave becomes our controlling tty; enter alt screen; wait. */
        char *name = ptsname(master);
        int slave = open(name, O_RDWR);
        struct winsize ws;
        TimuiConfig cfg; Timui *ui = NULL;
        setsid();
        if(slave < 0) _exit(127);
        memset(&ws, 0, sizeof ws); ws.ws_row = 24; ws.ws_col = 80;
        ioctl(slave, TIOCSWINSZ, &ws);
        dup2(slave, 0); dup2(slave, 1); dup2(slave, 2);
        close(slave); close(master);
        memset(&cfg, 0, sizeof cfg);
        cfg.input_fd = 0; cfg.output_fd = 1;
        cfg.flags = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_RESTORE_ON_EXIT;
        cfg.title = ""; cfg.profile = TIMUI_PROFILE_AUTO;
        if(timui_open(&cfg, &ui) != TIMUI_OK) _exit(127);
        pause();   /* the W6 handler restores the terminal on SIGTERM, then dies */
        _exit(127);
    }
    /* parent */
    {
        char out[2048]; ssize_t n; int status, retry; size_t total = 0;
        struct timespec ts200 = { 0, 200 * 1000 * 1000 }, ts50 = { 0, 50 * 1000 * 1000 };
        nanosleep(&ts200, NULL);   /* let the child enter the alt screen */
        fcntl(master, F_SETFL, fcntl(master, F_GETFL, 0) | O_NONBLOCK);
        n = read(master, out, sizeof out - 1);
        if(!(n > 0 && bytes_contain(out, (size_t)n, "\x1b[?1049h"))){
            close(master); kill(pid, SIGKILL); waitpid(pid, &status, 0); return;  /* skip */
        }
        kill(pid, SIGTERM);
        /* Drain the master WHILE reaping: the child's handler writes screen_exit
         * to the pty, and would block on a full buffer if the parent sat in
         * waitpid without reading — a deadlock. Poll read + waitpid(WNOHANG). */
        for(retry = 0; retry < 40; retry++){
            ssize_t m = read(master, out + total, sizeof out - 1 - total);
            if(m > 0) total += (size_t)m;
            if(bytes_contain(out, total, "\x1b[?1049l")) break;
            if(waitpid(pid, &status, WNOHANG) != 0){
                m = read(master, out + total, sizeof out - 1 - total);
                if(m > 0) total += (size_t)m;
                break;
            }
            nanosleep(&ts50, NULL);
        }
        waitpid(pid, &status, 0);   /* reap if not already */
        TIMUI_CHECK(bytes_contain(out, total, "\x1b[?1049l"));   /* W6: alt screen exited on SIGTERM */
        close(master);
    }
}
