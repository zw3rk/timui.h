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

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../tools/vendor/stb_image_write.h"

enum { SMOKE_W = 64, SMOKE_H = 24 };

static void make_smoke_rgba(unsigned char *rgba, int w, int h){
    static const unsigned char colors[][3] = {
        { 0x59, 0xee, 0x3f },
        { 0x56, 0xb6, 0xc2 },
        { 0xff, 0xff, 0xff },
        { 0x05, 0x07, 0x08 }
    };
    int x, y;
    if(!rgba || w <= 0 || h <= 0) return;
    for(y = 0; y < h; y++){
        for(x = 0; x < w; x++){
            int band = ((x / 8) + (y / 6)) & 3;
            unsigned char *p = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4u;
            if(x == 0 || y == 0 || x == w - 1 || y == h - 1) band = 0;
            p[0] = colors[band][0];
            p[1] = colors[band][1];
            p[2] = colors[band][2];
            p[3] = 0xff;
        }
    }
}

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

static void draw_unsupported(TimuiFrame *f, int x, int y, const char *label,
                             const char *note, TimuiRect r){
    TimuiStyle box = timui_style_make(0x98a1a6, 0x050708, 0);
    timui_label(f, x, y, timui_str_from_cstr(label),
                timui_style_make(0x98a1a6, TIMUI_COLOR_DEFAULT, 0));
    timui_draw_fill(timui_frame_buffer(f), r, box);
    timui_label(f, r.x, r.y + r.h / 2, timui_str_from_cstr(note), box);
}

int main(int argc, char **argv){
    TimuiConfig cfg = TIMUI_CONFIG_INIT;
    Timui *ui = NULL;
    TimuiImage *png = NULL, *rgba = NULL, *both = NULL;
    TimuiImageProtocol want = TIMUI_IMAGE_PROTOCOL_NONE;
    unsigned char smoke_rgba[SMOKE_W * SMOKE_H * 4];
    unsigned char *smoke_png = NULL;
    int smoke_png_len = 0;
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

    make_smoke_rgba(smoke_rgba, SMOKE_W, SMOKE_H);
    smoke_png = stbi_write_png_to_mem(smoke_rgba, SMOKE_W * 4,
                                      SMOKE_W, SMOKE_H, 4, &smoke_png_len);
    if(!smoke_png || smoke_png_len <= 0) goto done;
    png = timui_image_from_png(ui, smoke_png, (size_t)smoke_png_len);
    rgba = timui_image_from_rgba(ui, smoke_rgba, SMOKE_W, SMOKE_H, SMOKE_W * 4);
    both = timui_image_from_png_rgba(ui, smoke_png, (size_t)smoke_png_len,
                                     smoke_rgba, SMOKE_W, SMOKE_H, SMOKE_W * 4);
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
            if(active == TIMUI_IMAGE_PROTOCOL_ITERM2){
                draw_unsupported(f, 28, 6, "raw rgba", "iTerm2 needs PNG",
                                 TIMUI_RECT(28, 8, 16, 7));
            } else {
                draw_row(f, 28, 6, "raw rgba", rgba, TIMUI_RECT(28, 8, 16, 7));
            }
            draw_row(f, 52, 6, "png+rgba sidecar", both, TIMUI_RECT(52, 8, 16, 7));
        } else {
            draw_row(f, 2, 5,  "plain png",      png,  TIMUI_RECT(2,  7, 14, 5));
            if(active == TIMUI_IMAGE_PROTOCOL_ITERM2){
                draw_unsupported(f, 2, 13, "raw rgba", "PNG needed",
                                 TIMUI_RECT(2, 15, 14, 5));
            } else {
                draw_row(f, 2, 13, "raw rgba", rgba, TIMUI_RECT(2, 15, 14, 5));
            }
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
    free(smoke_png);
    timui_close(ui);
    return rc;
}
