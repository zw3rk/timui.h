/*
 * keyboard_inspector.c — show the last key pressed (T7.5).
 *
 * Useful for comparing what Ghostty / kitty / a fallback terminal send.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

static const char *last_key(TimuiFrame *f){
    if(timui_key_pressed(f, TIMUI_KEY_UP))         return "Up";
    if(timui_key_pressed(f, TIMUI_KEY_DOWN))       return "Down";
    if(timui_key_pressed(f, TIMUI_KEY_LEFT))       return "Left";
    if(timui_key_pressed(f, TIMUI_KEY_RIGHT))      return "Right";
    if(timui_key_pressed(f, TIMUI_KEY_HOME))       return "Home";
    if(timui_key_pressed(f, TIMUI_KEY_END))        return "End";
    if(timui_key_pressed(f, TIMUI_KEY_PAGE_UP))    return "PageUp";
    if(timui_key_pressed(f, TIMUI_KEY_PAGE_DOWN))  return "PageDn";
    if(timui_key_pressed(f, TIMUI_KEY_INSERT))     return "Insert";
    if(timui_key_pressed(f, TIMUI_KEY_DELETE))     return "Delete";
    if(timui_key_pressed(f, TIMUI_KEY_BACKSPACE))  return "Backspace";
    if(timui_key_pressed(f, TIMUI_KEY_TAB))        return "Tab";
    if(timui_key_pressed(f, TIMUI_KEY_ENTER))      return "Enter";
    if(timui_key_pressed(f, TIMUI_KEY_ESCAPE))     return "Escape";
    if(timui_key_pressed(f, TIMUI_KEY_F1))         return "F1";
    if(timui_key_pressed(f, TIMUI_KEY_F2))         return "F2";
    if(timui_key_pressed(f, TIMUI_KEY_F10))        return "F10";
    if(timui_key_pressed(f, TIMUI_KEY_F12))        return "F12";
    return "(none)";
}

int main(void){
    TimuiConfig cfg = TIMUI_CONFIG_INIT;
    Timui *ui = NULL;
    cfg.title     = "timui.h keyboard inspector";
    cfg.input_fd  = 0;
    cfg.output_fd = 1;
    cfg.profile   = TIMUI_PROFILE_AUTO;
    cfg.flags     = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_KITTY_KEYBOARD | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme     = TIMUI_THEME_DOS_BLUE;
    if(timui_open(&cfg, &ui) != TIMUI_OK) return 1;

    while(!timui_should_quit(ui)){
        TimuiFrame *f = NULL;
        const char *k;
        if(!timui_begin(ui, &f)) break;
        k = last_key(f);
        timui_label(f, 2, 1, TIMUI_STR_LIT("Keyboard inspector (Esc to quit)"),
                    timui_style_make(0xFFFFFF, 0x0000AA, 0));
        timui_label(f, 2, 3, TIMUI_STR_LIT("Last key:"), timui_style_make(0xFFFFFF, 0x0000AA, 0));
        timui_label(f, 12, 3, timui_str_from_cstr(k), timui_style_make(0x00FFFF, 0x0000AA, 0));
        timui_end(f);
    }

    timui_close(ui);
    return 0;
}
