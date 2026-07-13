/*
 * editor.c — a nano/micro-style single-file text editor (v0.2 showcase).
 *
 * The showcase for timui_text_area's in-line cursor editing: open a file (or a
 * fresh "untitled.txt"), edit it with a real cursor (Left/Right/Home/End,
 * Backspace/Delete, mid-string insert, UTF-8 aware), save with F2, and quit
 * with F10/ESC — with a "discard changes?" confirmation when the buffer is
 * dirty.
 *
 * Focus: the editor calls timui_set_focus() each frame (except while the discard
 * dialog is up) so the text_area is focused from the first frame — typing works
 * immediately, with a hardware cursor tracking the edit position.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

#include <stdio.h>
#include <string.h>

/* Load `path` into `buf`, bounded and NUL-terminated. A missing file is not an
 * error — like nano, we just open an empty buffer under that name. Returns the
 * number of bytes read. */
static size_t load_file(const char *path, char *buf, size_t cap){
    FILE  *fp = fopen(path, "r");
    size_t n  = 0;
    if(fp){
        n = fread(buf, 1, cap - 1, fp);   /* reserve one byte for the NUL */
        fclose(fp);
    }
    buf[n] = '\0';
    return n;
}

/* Write strlen(buf) bytes back to `path`. Returns 1 on success, 0 on any I/O
 * failure (open, short write, or close). */
static int save_file(const char *path, const char *buf){
    FILE  *fp = fopen(path, "w");
    size_t len;
    if(!fp) return 0;
    len = strlen(buf);
    if(fwrite(buf, 1, len, fp) != len){ fclose(fp); return 0; }
    return fclose(fp) == 0;
}

int main(int argc, char **argv){
    /* 64 KiB of editable text. `static` keeps this large buffer in BSS rather
     * than on the stack; it is not shared mutable global state — main owns it
     * and threads it through the frame as TimuiTextAreaState.text. */
    static char buf[64 * 1024];

    TimuiConfig cfg = TIMUI_CONFIG_INIT;
    Timui      *ui  = NULL;
    TimuiTextAreaState ta = { buf, sizeof buf, 0, 0 };   /* text + cursor + scroll */
    char     filename[1024];
    size_t   saved_len;         /* strlen at the last save — the dirty baseline */
    uint64_t saved_at_ms = 0;   /* time of the last save, for the "Saved" flash  */
    int      dialog = 0;        /* 1 while the discard-confirm modal is up        */

    TimuiTheme theme;
    TimuiStyle status_st, saved_st;

    /* ---- model init: open argv[1], else a fresh untitled buffer ----------- */
    if(argc > 1 && argv[1]){
        load_file(argv[1], buf, sizeof buf);
        snprintf(filename, sizeof filename, "%s", argv[1]);
    } else {
        buf[0] = '\0';
        snprintf(filename, sizeof filename, "%s", "untitled.txt");
    }
    saved_len = strlen(buf);

    cfg.title     = "timui edit";
    cfg.input_fd  = 0;
    cfg.output_fd = 1;
    cfg.profile   = TIMUI_PROFILE_AUTO;
    cfg.flags     = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_MOUSE | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme     = TIMUI_THEME_MODERN_DARK;

    if(timui_open(&cfg, &ui) != TIMUI_OK) return 1;

    /* Bar styles, computed once: both bars use the theme's STATUS slot; the
     * transient "Saved" flash keeps that background but borrows the SUCCESS
     * foreground so it reads as confirmation, not chrome. */
    theme     = timui_theme_builtin(TIMUI_THEME_MODERN_DARK);
    status_st = timui_theme_style(&theme, TIMUI_SLOT_STATUS);
    saved_st  = timui_style_make(timui_theme_style(&theme, TIMUI_SLOT_SUCCESS).fg,
                                 status_st.bg, status_st.attrs);

    while(!timui_should_quit(ui)){
        TimuiFrame *f = NULL;
        TimuiRect   full, area, top, bot, body;
        size_t      len, row = 0, col = 0, k;
        int         dirty;
        char        title[1200], fbar[128];
        const char *hint;

        if(!timui_begin(ui, &f)) break;

        /* dirty is a coarse-but-honest signal: the buffer's length differs from
         * the last save. (Equal-length edits read as clean — acceptable for a
         * demo; text_area returns no per-edit signal to sharpen it.) */
        len   = strlen(buf);
        dirty = (len != saved_len);

        /* cursor -> line/column: row = count of '\n' before the cursor, col =
         * bytes since the last '\n'. Displayed 1-based, like nano. */
        for(k = 0; k < ta.cursor && k < sizeof buf; k++){
            if(buf[k] == '\n'){ row++; col = 0; }
            else                col++;
        }

        /* ---- quit request (suppressed while the modal owns input) --------- */
        if(dialog == 0 &&
           (timui_key_pressed(f, TIMUI_KEY_F10) || timui_key_pressed(f, TIMUI_KEY_ESCAPE))){
            if(dirty) dialog = 1;        /* ask before losing edits */
            else      timui_quit(ui);    /* clean -> leave at once   */
        }

        /* ---- save (F2) --------------------------------------------------- */
        if(dialog == 0 && timui_key_pressed(f, TIMUI_KEY_F2)){
            if(save_file(filename, buf)){
                saved_len   = len;       /* new dirty baseline        */
                saved_at_ms = timui_now_ms();
            }
        }

        /* ---- layout: title bar / editor body / function bar -------------- */
        full = timui_root(f);
        area = full;
        top  = timui_cut_top(&area, 1);
        bot  = timui_cut_bottom(&area, 1);
        body = area;                     /* everything in between */

        /* title/status bar: a filled STATUS strip carrying the filename (+ '*'
         * when dirty), with a right-aligned "Saved" flash or focus hint. */
        timui_draw_fill(timui_frame_buffer(f), top, status_st);
        snprintf(title, sizeof title, " timui edit — %s%s ", filename, dirty ? "*" : "");
        timui_label(f, top.x + 1, top.y, timui_str_from_cstr(title), status_st);

        if(saved_at_ms && timui_now_ms() - saved_at_ms < 1200u){
            hint = "Saved";
            timui_label(f, top.x + top.w - (int)strlen(hint) - 1, top.y,
                        timui_str_from_cstr(hint), saved_st);
        }

        /* the star: a multi-line editor with an in-line hardware cursor. Keep it
         * focused (it's the only input) so typing works immediately; while the
         * discard dialog is up, let its buttons hold focus instead. */
        if(dialog == 0) timui_set_focus(f, TIMUI_ID("editor"));
        timui_text_area(f, TIMUI_ID("editor"), body, &ta);

        /* function bar: shortcuts + live cursor position. */
        snprintf(fbar, sizeof fbar, " F2 Save   F10 Quit   |  Ln %zu Col %zu ",
                 (size_t)(row + 1), (size_t)(col + 1));
        timui_function_bar(f, bot, timui_str_from_cstr(fbar));

        /* ---- discard-changes modal --------------------------------------- *
         * Button-driven only: message_box clears the library's modal trap on a
         * button click, so dismissing it via OK/Cancel never strands the modal
         * (the app can't clear it — `Timui` is opaque). */
        if(dialog){
            const TimuiStr btns[2] = { TIMUI_STR_LIT("OK"), TIMUI_STR_LIT("Cancel") };
            int r = timui_message_box(f, TIMUI_ID("discard"), full,
                                      TIMUI_STR_LIT("Discard changes?"),
                                      TIMUI_STR_LIT("Unsaved changes will be lost."),
                                      btns, 2);
            if(r == 0)      timui_quit(ui);   /* OK     -> quit            */
            else if(r == 1) dialog = 0;       /* Cancel -> back to editing */
        }

        timui_end(f);
    }

    timui_close(ui);
    return 0;
}
