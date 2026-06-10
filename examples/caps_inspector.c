/*
 * caps_inspector.c — show detected terminal capabilities (T7.6).
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

#include <stdlib.h>   /* getenv */

static void cap_line(TimuiFrame *f, int y, const char *name, int on){
    char line[64];
    int i = 0;
    const char *n;
    line[i++] = on ? '[' : ' ';
    line[i++] = on ? 'x' : ' ';
    line[i++] = on ? ']' : ' ';
    line[i++] = ' ';
    n = name;
    while(*n && i < (int)sizeof(line) - 1) line[i++] = *n++;
    line[i] = '\0';
    timui_label(f, 2, y, timui_str_from_cstr(line), timui_style_make(0xFFFFFF, 0x0000AA, 0));
}

int main(void){
    TimuiConfig cfg = {0};
    Timui *ui = NULL;
    TimuiCaps caps;
    timui_caps_detect(&caps, getenv("TERM"), getenv("TERM_PROGRAM"), getenv("COLORTERM"));

    cfg.title     = "timui.h capability inspector";
    cfg.input_fd  = 0;
    cfg.output_fd = 1;
    cfg.profile   = TIMUI_PROFILE_AUTO;
    cfg.flags     = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme     = TIMUI_THEME_DOS_BLUE;
    if(timui_open(&cfg, &ui) != TIMUI_OK) return 1;

    while(!timui_should_quit(ui)){
        TimuiFrame *f = NULL;
        if(!timui_begin(ui, &f)) break;
        if(timui_key_pressed(f, TIMUI_KEY_ESCAPE) || timui_key_pressed(f, TIMUI_KEY_F10)) timui_quit(ui);
        timui_label(f, 2, 1, TIMUI_STR_LIT("Capability inspector (Esc/F10 to quit)"),
                    timui_style_make(0xFFFFFF, 0x0000AA, 0));
        cap_line(f, 3, "truecolor",          timui_caps_has(&caps, TIMUI_CAP_TRUECOLOR));
        cap_line(f, 4, "256-color",          timui_caps_has(&caps, TIMUI_CAP_256_COLOR));
        cap_line(f, 5, "kitty keyboard",     timui_caps_has(&caps, TIMUI_CAP_KITTY_KEYBOARD));
        cap_line(f, 6, "SGR mouse",          timui_caps_has(&caps, TIMUI_CAP_SGR_MOUSE));
        cap_line(f, 7, "bracketed paste",    timui_caps_has(&caps, TIMUI_CAP_BRACKETED_PASTE));
        cap_line(f, 8, "synchronized output", timui_caps_has(&caps, TIMUI_CAP_SYNC_OUTPUT));
        cap_line(f, 9, "OSC 8 hyperlinks",   timui_caps_has(&caps, TIMUI_CAP_OSC8_HYPERLINKS));
        timui_end(f);
    }

    timui_close(ui);
    return 0;
}
