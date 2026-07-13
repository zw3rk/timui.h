/*
 * todo.c — a functional TODO / task manager (model / view / update showcase).
 *
 * The whole application state lives in main() as plain values: the task array,
 * the selection index, and the add-line buffer. timui widgets are *controlled*
 * — each frame they render the model and report intent; the model is mutated
 * only here, in the loop. That is the elm-style update discipline expressed in
 * C: begin -> read intent -> update the owned model -> view (draw) -> end.
 *
 * Widgets exercised: timui_input_field (in-line cursor editing), a manual
 * selectable list drawn row-by-row (deliberately NOT timui_listbox), a
 * timui_checkbox_mut per row, and the function bar.
 *
 * Keys:  type + Enter adds the line as a task · Up/Down move the selection ·
 * Space toggles the selected task's done flag · d/D deletes it · F10 or Esc
 * quits. Click the input to type; a task row's checkbox is also clickable.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

#include <stdio.h>
#include <string.h>

enum { TODO_MAX = 256, TODO_TEXT = 120 };
#define TODO_FILE "todo.txt"

/* The model element. Owned by main(); helpers borrow it by pointer but never
 * retain it, keeping the demo free of hidden/global state. */
struct Task { char text[TODO_TEXT]; int done; };

/* Copy `src` into a task's fixed text field, always NUL-terminated and bounded
 * (never trusts the source length). */
static void task_set_text(struct Task *t, const char *src) {
    size_t n = strlen(src);
    if (n >= sizeof t->text) n = sizeof t->text - 1;
    memcpy(t->text, src, n);
    t->text[n] = '\0';
}

/* Persistence (optional, all I/O errors are swallowed so the demo runs the same
 * with or without a data file). Line format: "<done>|<text>", e.g. "1|Buy milk".
 * A missing/unreadable file simply yields an empty list. */
static void todo_load(const char *path, struct Task *tasks, int *count) {
    char line[TODO_TEXT + 8];
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    while (*count < TODO_MAX && fgets(line, sizeof line, fp)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (n < 2 || line[1] != '|') continue;          /* skip malformed rows */
        tasks[*count].done = (line[0] == '1');
        task_set_text(&tasks[*count], line + 2);
        (*count)++;
    }
    fclose(fp);
}

static void todo_save(const char *path, const struct Task *tasks, int count) {
    int i;
    FILE *fp = fopen(path, "w");
    if (!fp) return;
    for (i = 0; i < count; i++)
        fprintf(fp, "%d|%s\n", tasks[i].done ? 1 : 0, tasks[i].text);
    fclose(fp);
}

/* Single-key list commands (Space = toggle done, d/D = delete) are not TimuiKey
 * events — timui delivers printable keystrokes as TEXT to the *focused* text
 * widget through the frame's per-frame text buffer. A focused timui_input_field
 * consumes that buffer (clearing its length), so this returns the first command
 * key ONLY when no input is focused — i.e. exactly when the user is navigating
 * the list rather than typing a task name (where a Space or 'd' is just text).
 *
 * Space / d arrive as typed text, not TimuiKey events; timui_char_pressed reads
 * them via the public API. Call AFTER the input field so a focused field has
 * already claimed its text (only then do list commands apply). */
static char todo_command_key(const TimuiFrame *f) {
    if (timui_char_pressed(f, ' ')) return ' ';
    if (timui_char_pressed(f, 'd')) return 'd';
    if (timui_char_pressed(f, 'D')) return 'D';
    return 0;
}

int main(void) {
    TimuiConfig cfg = TIMUI_CONFIG_INIT;
    Timui *ui = NULL;
    TimuiTheme theme = timui_theme_builtin(TIMUI_THEME_DOS_BLUE);

    /* ---- the model (plain values, owned here) ------------------------- */
    struct Task tasks[TODO_MAX];
    int count = 0;
    int selected = 0;
    char inbuf[TODO_TEXT] = {0};
    TimuiInputState input = { inbuf, sizeof inbuf, 0, 0 };  /* cursor + scroll persist */

    todo_load(TODO_FILE, tasks, &count);

    cfg.title     = "timui.h todo";
    cfg.input_fd  = 0;
    cfg.output_fd = 1;
    cfg.profile   = TIMUI_PROFILE_AUTO;
    cfg.flags     = TIMUI_FLAG_ALT_SCREEN | TIMUI_FLAG_MOUSE | TIMUI_FLAG_RESTORE_ON_EXIT;
    cfg.theme     = TIMUI_THEME_DOS_BLUE;

    if (timui_open(&cfg, &ui) != TIMUI_OK) return 1;

    while (!timui_should_quit(ui)) {
        TimuiFrame *f = NULL;
        TimuiCellBuffer *buf;
        TimuiRect root, title, inrow, addlbl, bar, list;
        char titlebuf[48];
        char cmd;
        int visible, first, i;

        if (!timui_begin(ui, &f)) break;
        if (timui_key_pressed(f, TIMUI_KEY_ESCAPE) || timui_key_pressed(f, TIMUI_KEY_F10))
            timui_quit(ui);

        /* self-heal the selection in case the list shrank underneath it */
        if (selected >= count) selected = count > 0 ? count - 1 : 0;
        if (selected < 0) selected = 0;

        buf  = timui_frame_buffer(f);
        root = timui_root(f);
        if (root.w <= 0 || root.h <= 0) { timui_end(f); continue; }  /* no room */

        /* backdrop, then carve the layout with the cut helpers */
        timui_draw_fill(buf, root, timui_theme_style(&theme, TIMUI_SLOT_TEXT));
        title = timui_cut_top(&root, 1);
        inrow = timui_cut_top(&root, 1);
        bar   = timui_cut_bottom(&root, 1);
        list  = root;                                  /* the middle region */

        /* ---- view: header + add line -------------------------------------- */
        snprintf(titlebuf, sizeof titlebuf, "timui.h  TODO   (%d task%s)",
                 count, count == 1 ? "" : "s");
        timui_label(f, title.x + 1, title.y, timui_str_from_cstr(titlebuf),
                    timui_theme_style(&theme, TIMUI_SLOT_PANEL_TITLE));

        addlbl = timui_cut_left(&inrow, 6);
        timui_label(f, addlbl.x + 1, addlbl.y, TIMUI_STR_LIT("Add:"),
                    timui_theme_style(&theme, TIMUI_SLOT_TEXT));
        /* update: input_field returns true on Enter; append the line as a task */
        if (timui_input_field(f, TIMUI_ID("add"), inrow, &input) && inbuf[0]) {
            if (count < TODO_MAX) {
                task_set_text(&tasks[count], inbuf);
                tasks[count].done = 0;
                selected = count;                      /* select the fresh task */
                count++;
            }
            inbuf[0] = '\0';                            /* clear + reset the widget */
            input.cursor = 0;
            input.scroll_x = 0;
        }

        /* ---- update: keyboard commands over the list ---------------------- */
        cmd = todo_command_key(f);                      /* Space / d — see helper */
        if (timui_key_pressed(f, TIMUI_KEY_UP)   && selected > 0)         selected--;
        if (timui_key_pressed(f, TIMUI_KEY_DOWN) && selected + 1 < count) selected++;
        if (count > 0 && cmd == ' ')
            tasks[selected].done = !tasks[selected].done;
        if (count > 0 && (cmd == 'd' || cmd == 'D')) {  /* delete: shift the tail down */
            int j;
            for (j = selected; j + 1 < count; j++) tasks[j] = tasks[j + 1];
            count--;
            if (selected >= count) selected = count > 0 ? count - 1 : 0;
        }

        /* ---- view: the task list, one row per task ------------------------ *
         * Scroll so the selection stays on screen (keeps it the last visible
         * row once it moves past the bottom edge). */
        visible = list.h;
        first = (visible > 0 && selected >= visible) ? selected - visible + 1 : 0;
        for (i = 0; i < visible && first + i < count; i++) {
            int idx = first + i;
            bool done = tasks[idx].done != 0;
            char cbid[16];
            TimuiRect row = { list.x, list.y + i, list.w, 1 };
            TimuiRect box = { row.x, row.y, 4, 1 };
            TimuiRect txt = { row.x + 4, row.y, row.w - 4, 1 };
            int is_sel = (idx == selected);
            TimuiStyle rowst = timui_theme_style(&theme,
                                   is_sel ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT);
            TimuiStyle txtst = is_sel ? rowst
                             : timui_theme_style(&theme,
                                   done ? TIMUI_SLOT_TEXT_DIM : TIMUI_SLOT_TEXT);

            timui_draw_fill(buf, row, rowst);           /* selection / normal bg */

            /* checkbox bound to a temp bool bridging the model's int done */
            snprintf(cbid, sizeof cbid, "cb%d", idx);
            timui_checkbox_mut(f, TIMUI_ID(cbid), box, TIMUI_STR_LIT(""), &done);
            tasks[idx].done = done;                     /* write the toggle back */

            /* task text, clipped to its cell so long entries never overflow */
            timui_push_clip(f, txt);
            timui_label(f, txt.x, txt.y, timui_str_from_cstr(tasks[idx].text), txtst);
            timui_pop_clip(f);
        }
        if (count == 0)
            timui_label(f, list.x + 1, list.y, TIMUI_STR_LIT("(no tasks — type above and press Enter)"),
                        timui_theme_style(&theme, TIMUI_SLOT_TEXT_DIM));

        timui_function_bar(f, bar,
            TIMUI_STR_LIT(" Enter Add   Space Done   D Delete   Up/Down Move   F10 Quit "));

        timui_end(f);
    }

    todo_save(TODO_FILE, tasks, count);
    timui_close(ui);
    return 0;
}
