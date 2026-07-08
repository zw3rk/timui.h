/*
 * image_smoke.c — live terminal smoke for timui image protocols.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static const unsigned char smoke_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04,
    0x08, 0x06, 0x00, 0x00, 0x00, 0xa9, 0xf1, 0x9e, 0x7e, 0x00, 0x00, 0x00,
    0x25, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x88, 0x7c, 0x67, 0xff,
    0x9f, 0x95, 0x9d, 0xe3, 0x3f, 0x08, 0x84, 0x6d, 0x3b, 0xf4, 0x9f, 0x01,
    0x99, 0x03, 0x92, 0x64, 0x40, 0xe6, 0x80, 0x24, 0x19, 0x90, 0x39, 0x20,
    0x00, 0x00, 0x77, 0xa5, 0x29, 0x85, 0xb5, 0x28, 0x31, 0x1a, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

static const unsigned char smoke_rgba[4 * 4 * 4] = {
    0x59,0xee,0x3f,0xff, 0x05,0x07,0x08,0xff, 0xff,0xff,0xff,0xff, 0x56,0xb6,0xc2,0xff,
    0x05,0x07,0x08,0xff, 0xff,0xff,0xff,0xff, 0x56,0xb6,0xc2,0xff, 0x59,0xee,0x3f,0xff,
    0xff,0xff,0xff,0xff, 0x56,0xb6,0xc2,0xff, 0x59,0xee,0x3f,0xff, 0x05,0x07,0x08,0xff,
    0x56,0xb6,0xc2,0xff, 0x59,0xee,0x3f,0xff, 0x05,0x07,0x08,0xff, 0xff,0xff,0xff,0xff,
};

static TimuiImageProtocol parse_protocol(const char *s, int *forced){
    if(forced) *forced = 1;
    if(!s || strcmp(s, "auto") == 0){
        if(forced) *forced = 0;
        return TIMUI_IMAGE_PROTOCOL_NONE;
    }
    if(strcmp(s, "kitty") == 0) return TIMUI_IMAGE_PROTOCOL_KITTY;
    if(strcmp(s, "sixel") == 0) return TIMUI_IMAGE_PROTOCOL_SIXEL;
    if(strcmp(s, "iterm2") == 0) return TIMUI_IMAGE_PROTOCOL_ITERM2;
    if(strcmp(s, "none") == 0) return TIMUI_IMAGE_PROTOCOL_NONE;
    if(forced) *forced = -1;
    return TIMUI_IMAGE_PROTOCOL_NONE;
}

static const char *protocol_name(TimuiImageProtocol p){
    switch(p){
        case TIMUI_IMAGE_PROTOCOL_KITTY: return "kitty";
        case TIMUI_IMAGE_PROTOCOL_SIXEL: return "sixel";
        case TIMUI_IMAGE_PROTOCOL_ITERM2: return "iterm2";
        default: return "none";
    }
}

static int parse_int(const char *s, int fallback){
    char *end = NULL;
    long v;
    if(!s) return fallback;
    v = strtol(s, &end, 10);
    if(!end || *end != '\0' || v < 0 || v > 1000000) return fallback;
    return (int)v;
}

static void draw_row(TimuiFrame *f, int x, int y, const char *label,
                     TimuiImage *img, TimuiRect r){
    timui_label(f, x, y, timui_str_from_cstr(label),
                timui_style_make(0x59ee3f, TIMUI_COLOR_DEFAULT, 0));
    timui_image_draw(f, img, r);
}

int main(int argc, char **argv){
    TimuiConfig cfg = {0};
    Timui *ui = NULL;
    TimuiImage *png = NULL, *rgba = NULL, *both = NULL;
    TimuiImageProtocol want = TIMUI_IMAGE_PROTOCOL_NONE;
    int forced = 0, frames = -1, frame = 0, i;
    int rc = 1;

    for(i = 1; i < argc; i++){
        if(strcmp(argv[i], "--protocol") == 0 && i + 1 < argc){
            want = parse_protocol(argv[++i], &forced);
            if(forced < 0) return 2;
        } else if(strcmp(argv[i], "--frames") == 0 && i + 1 < argc){
            frames = parse_int(argv[++i], -1);
        } else {
            return 2;
        }
    }

    cfg.title = "timui image smoke";
    cfg.input_fd = STDIN_FILENO;
    cfg.output_fd = STDOUT_FILENO;
    cfg.profile = TIMUI_PROFILE_AUTO;
    cfg.flags = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme = TIMUI_THEME_DOS_BLUE;
    if(timui_open(&cfg, &ui) != TIMUI_OK) return 1;
    if(forced > 0) timui_force_image_protocol(ui, want);

    png = timui_image_from_png(ui, smoke_png, sizeof smoke_png);
    rgba = timui_image_from_rgba(ui, smoke_rgba, 4, 4, 4 * 4);
    both = timui_image_from_png_rgba(ui, smoke_png, sizeof smoke_png,
                                     smoke_rgba, 4, 4, 4 * 4);
    if(!png || !rgba || !both) goto done;

    while(!timui_should_quit(ui)){
        TimuiFrame *f = NULL;
        TimuiRect root;
        char line[96];
        TimuiImageProtocol active;
        if(!timui_begin(ui, &f)) break;
        root = timui_root(f);
        active = timui_image_protocol(ui);

        timui_label(f, 2, 1, TIMUI_STR_LIT("image protocol smoke"),
                    timui_style_make(0xffffff, TIMUI_COLOR_DEFAULT, 0));
        snprintf(line, sizeof line, "active: %s%s", protocol_name(active),
                 forced > 0 ? " (forced)" : " (detected)");
        timui_label(f, 2, 2, timui_str_from_cstr(line),
                    timui_style_make(0x98a1a6, TIMUI_COLOR_DEFAULT, 0));
        timui_label(f, 2, 3, TIMUI_STR_LIT("Esc exits. Use PROTOCOL=kitty|sixel|iterm2|none."),
                    timui_style_make(0x98a1a6, TIMUI_COLOR_DEFAULT, 0));

        if(root.w >= 70 && root.h >= 18){
            draw_row(f, 4, 6,  "plain png",      png,  TIMUI_RECT(4,  8, 16, 7));
            draw_row(f, 28, 6, "raw rgba",       rgba, TIMUI_RECT(28, 8, 16, 7));
            draw_row(f, 52, 6, "png+rgba sidecar", both, TIMUI_RECT(52, 8, 16, 7));
        } else {
            draw_row(f, 2, 5,  "plain png",      png,  TIMUI_RECT(2,  7, 14, 5));
            draw_row(f, 2, 13, "raw rgba",       rgba, TIMUI_RECT(2, 15, 14, 5));
            draw_row(f, 2, 21, "png+rgba sidecar", both, TIMUI_RECT(2, 23, 14, 5));
        }

        if(timui_key_pressed(f, TIMUI_KEY_ESCAPE) ||
           timui_key_pressed(f, TIMUI_KEY_F10)) timui_quit(ui);
        timui_end(f);
        frame++;
        if(frames >= 0 && frame >= frames) timui_quit(ui);
    }

    rc = 0;
done:
    timui_image_free(ui, png);
    timui_image_free(ui, rgba);
    timui_image_free(ui, both);
    timui_close(ui);
    return rc;
}
