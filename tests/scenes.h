/*
 * scenes.h — shared golden scenes for the visual-test infrastructure.
 *
 * Each builder initializes the buffer then draws a fixed, deterministic scene.
 * They are built identically by tools/gen_golden.c (which writes
 * tests/golden/<scene>.txt) and by the golden-backed unit test, so a rendering
 * regression surfaces as a golden mismatch rather than silent drift between
 * the two producers.
 *
 * Include AFTER <timui.h> (needs TimuiCellBuffer / TimuiAllocator + the draw
 * API, which are declared there). The builders are `static` so each
 * translation unit that includes this header gets its own copy.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIMUI_SCENES_H
#define TIMUI_SCENES_H

/* 12x5 panel: single-line box border (cyan), "Files" title on the top edge
 * (yellow bold), body text "Edit  Quit" (white). Exercises box-drawing
 * glyphs plus truecolor foregrounds and the BOLD attribute. */
static void scene_panel(TimuiCellBuffer *b, const TimuiAllocator *al){
    TimuiStyle border = timui_style_make(0x00C0FF, TIMUI_COLOR_DEFAULT, 0);
    TimuiStyle title  = timui_style_make(0xFFFF00, TIMUI_COLOR_DEFAULT, TIMUI_ATTR_BOLD);
    TimuiStyle body   = timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, 0);
    timui_cells_init(b, 12, 5, al);
    timui_draw_box(b, TIMUI_RECT(0, 0, 12, 5), TIMUI_BORDER_SINGLE, border);
    timui_draw_text(b, 2, 0, TIMUI_STR_LIT("Files"), title);
    timui_draw_text(b, 2, 2, TIMUI_STR_LIT("Edit  Quit"), body);
}

/* 10x1 rainbow: one glyph per column, each a distinct foreground. Exercises
 * truecolor SGR across many cells in a row. */
static void scene_rainbow(TimuiCellBuffer *b, const TimuiAllocator *al){
    static const uint32_t cols[10] = {
        0xFF0000, 0xFF7F00, 0xFFFF00, 0x00FF00, 0x00FFFF,
        0x0000FF, 0x7F00FF, 0xFF00FF, 0xFFFFFF, 0x808080
    };
    const char text[10] = { 'A','B','C','D','E','F','G','H','I','J' };
    int i;
    timui_cells_init(b, 10, 1, al);
    for(i = 0; i < 10; i++)
        timui_draw_text(b, i, 0, (TimuiStr){ &text[i], 1 },
                        timui_style_make(cols[i], TIMUI_COLOR_DEFAULT, 0));
}

/* 18x2 styled: row 0 is a run of single glyphs each carrying one attribute
 * (B/I/U/R/S), followed by "BD" (BOLD|DIM); row 1 is a blue background fill.
 * Exercises attribute serialization (incl. compound attrs) and backgrounds. */
static void scene_attrs(TimuiCellBuffer *b, const TimuiAllocator *al){
    timui_cells_init(b, 18, 2, al);
    timui_draw_text(b, 0, 0, TIMUI_STR_LIT("B"),  timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, TIMUI_ATTR_BOLD));
    timui_draw_text(b, 1, 0, TIMUI_STR_LIT("I"),  timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, TIMUI_ATTR_ITALIC));
    timui_draw_text(b, 2, 0, TIMUI_STR_LIT("U"),  timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, TIMUI_ATTR_UNDERLINE));
    timui_draw_text(b, 3, 0, TIMUI_STR_LIT("R"),  timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, TIMUI_ATTR_REVERSE));
    timui_draw_text(b, 4, 0, TIMUI_STR_LIT("S"),  timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, TIMUI_ATTR_STRIKE));
    timui_draw_text(b, 5, 0, TIMUI_STR_LIT("BD"), timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, TIMUI_ATTR_BOLD | TIMUI_ATTR_DIM));
    timui_draw_fill(b, TIMUI_RECT(0, 1, 18, 1), timui_style_make(0xFFFFFF, 0x0000A0, 0));
}

/* 6x1 wide-glyph: "AあB" — the hiragana U+3042 occupies two columns (a lead
 * cell of width 2 plus a blanked continuation cell). Exercises wide-glyph
 * width handling and continuation cells in the cell buffer. */
static void scene_wide(TimuiCellBuffer *b, const TimuiAllocator *al){
    timui_cells_init(b, 6, 1, al);
    timui_draw_text(b, 0, 0, TIMUI_STR_LIT("A\xE3\x81\x82" "B"),
                    timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, 0));
}

#endif /* TIMUI_SCENES_H */
