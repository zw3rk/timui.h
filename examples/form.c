/*
 * form.c — checkbox / radio / input / submit + message box (T7.3).
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

int main(void){
    TimuiConfig cfg = {0};
    Timui *ui = NULL;
    bool enable = true;
    int choice = 0, show_msg = 0;
    char name[64] = {0};

    cfg.title     = "timui.h form";
    cfg.input_fd  = 0;
    cfg.output_fd = 1;
    cfg.profile   = TIMUI_PROFILE_AUTO;
    cfg.flags     = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_MOUSE | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme     = TIMUI_THEME_DOS_BLUE;

    if(timui_open(&cfg, &ui) != TIMUI_OK) return 1;

    while(!timui_should_quit(ui)){
        TimuiFrame *f = NULL;
        TimuiRect root;
        const TimuiStr ok[1] = { TIMUI_STR_LIT("OK") };
        if(!timui_begin(ui, &f)) break;
        if(timui_key_pressed(f, TIMUI_KEY_ESCAPE)) timui_quit(ui);

        root = timui_root(f);
        timui_label(f, root.x + 2, root.y, TIMUI_STR_LIT("Form"),
                    timui_style_make(0xFFFFFF, 0x0000AA, 0));

        timui_checkbox_mut(f, TIMUI_ID("en"),   TIMUI_RECT(2, 2, 24, 1), TIMUI_STR_LIT("Enable feature"), &enable);
        if(timui_radio(f, TIMUI_ID("ra"), TIMUI_RECT(2, 3, 24, 1), TIMUI_STR_LIT("Option A"), choice == 0).changed) choice = 0;
        if(timui_radio(f, TIMUI_ID("rb"), TIMUI_RECT(2, 4, 24, 1), TIMUI_STR_LIT("Option B"), choice == 1).changed) choice = 1;
        timui_label(f, 2, 6, TIMUI_STR_LIT("Name:"), timui_style_make(0xFFFFFF, 0x0000AA, 0));
        timui_input_line_buf(f, TIMUI_ID("name"), TIMUI_RECT(9, 6, 30, 1), name, sizeof name);

        if(timui_button(f, TIMUI_ID("submit"), TIMUI_RECT(2, 8, 10, 1), TIMUI_STR_LIT("Submit")).clicked) show_msg = 1;
        if(show_msg){
            if(timui_message_box(f, TIMUI_ID("msg"), root, TIMUI_STR_LIT("Submitted"),
                                 TIMUI_STR_LIT("Form submitted!"), ok, 1) == 0) show_msg = 0;
        }

        timui_end(f);
    }

    timui_close(ui);
    return 0;
}
