/*
 * test_v02_more.c — snapshot testing, text-area, ConPTY (v0.2).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <limits.h>
#include <string.h>

#define SETIN(fake, lit) timui_fake_set_input((fake), (lit), sizeof(lit) - 1)
#define WAVE_SKIN "\xF0\x9F\x91\x8B\xF0\x9F\x8F\xBD" /* waving hand + skin tone */
#define HEART_VS  "\xE2\x9D\xA4\xEF\xB8\x8F"         /* heavy black heart + VS16 */

/* ---- snapshot testing (#53) ---- */
TIMUI_TEST(test_snapshot_row_eq){
    TimuiAllocator al = timui_default_allocator();
    TimuiCellBuffer b;
    timui_cells_init(&b, 10, 3, &al);
    timui_draw_text(&b, 0, 1, TIMUI_STR_LIT("Hello"), timui_style_make(0xFFFFFF, TIMUI_COLOR_DEFAULT, 0));
    TIMUI_CHECK(timui_snapshot_row_eq(&b, 1, "Hello     "));  /* 10 cells: Hello + 5 spaces */
    TIMUI_CHECK(!timui_snapshot_row_eq(&b, 1, "World     "));
    timui_cells_destroy(&b);
}

/* ---- text-area (#49) ---- */
TIMUI_TEST(test_text_area_renders){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    char text[64] = "Hello";
    TimuiTextAreaState tas = { text, sizeof text, 5, 0 };
    TimuiCellBuffer *buf;
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_text_area(f, TIMUI_ID("ta"), TIMUI_RECT(0, 0, 20, 3), &tas);
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == 'H');
    timui_end(f);
    timui_close(ui);
}

/* V8: text_area must append/delete whole UTF-8 codepoints (same class of
 * defect as input_line_buf). */
TIMUI_TEST(test_text_area_utf8_no_split){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[4] = {0};   /* fits "ab"+NUL; not "ab"+é */
    TimuiTextAreaState tas = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 20, 3);
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    /* focus via press/release */
    SETIN(&fake, "\x1b[<0;2;1M");
    timui_begin(ui, &f); timui_text_area(f, TIMUI_ID("ta"), r, &tas); timui_end(f);
    SETIN(&fake, "\x1b[<0;2;1m");
    timui_begin(ui, &f); timui_text_area(f, TIMUI_ID("ta"), r, &tas); timui_end(f);
    timui_fake_set_input(&fake, "ab\xC3\xA9", 4);
    timui_begin(ui, &f); timui_text_area(f, TIMUI_ID("ta"), r, &tas); timui_end(f);
    TIMUI_CHECK(strcmp(text, "ab") == 0);          /* é skipped whole, not split */
    timui_close(ui);
}

TIMUI_TEST(test_text_area_utf8_backspace){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[8] = {0};
    TimuiTextAreaState tas = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 20, 3);
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
    SETIN(&fake, "\x1b[<0;2;1M");
    timui_begin(ui, &f); timui_text_area(f, TIMUI_ID("tb"), r, &tas); timui_end(f);
    SETIN(&fake, "\x1b[<0;2;1m");
    timui_begin(ui, &f); timui_text_area(f, TIMUI_ID("tb"), r, &tas); timui_end(f);
    timui_fake_set_input(&fake, "\xC3\xA9", 2);
    timui_begin(ui, &f); timui_text_area(f, TIMUI_ID("tb"), r, &tas); timui_end(f);
    TIMUI_CHECK(strcmp(text, "\xC3\xA9") == 0);
    timui_fake_set_input(&fake, "\x7f", 1);
    timui_begin(ui, &f); timui_text_area(f, TIMUI_ID("tb"), r, &tas); timui_end(f);
    TIMUI_CHECK(strcmp(text, "") == 0);            /* whole é removed */
    timui_close(ui);
}

TIMUI_TEST(test_text_area_paste_preserves_newline){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    char text[16] = {0};
    TimuiTextAreaState tas = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 20, 3);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
#define TA_PASTE_FRAME() do{ timui_begin(ui,&f); timui_text_area(f, TIMUI_ID("ta"), r, &tas); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); TA_PASTE_FRAME();
    SETIN(&fake, "\x1b[<0;2;1m"); TA_PASTE_FRAME();
    SETIN(&fake, "\x1b[200~a\nb\x1b[201~"); TA_PASTE_FRAME();
    TIMUI_CHECK(strcmp(text, "a\nb") == 0);
#undef TA_PASTE_FRAME
    timui_close(ui);
}

/* F1.3: text_area in-line cursor editing — LEFT/RIGHT/HOME/END/DELETE move and
 * edit at st->cursor (mid-string), not just append-at-end. */
TIMUI_TEST(test_text_area_cursor_edit){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[16] = {0};
    TimuiTextAreaState tas = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 20, 3);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
#define TA_FRAME() do{ timui_begin(ui,&f); timui_text_area(f, TIMUI_ID("ta"), r, &tas); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); TA_FRAME();       /* click to focus (press) */
    SETIN(&fake, "\x1b[<0;2;1m"); TA_FRAME();       /* release */
    SETIN(&fake, "abc"); TA_FRAME();
    TIMUI_CHECK(strcmp(text, "abc") == 0 && tas.cursor == 3);
    SETIN(&fake, "\x1b[D"); TA_FRAME();             /* LEFT (one move/frame) */
    TIMUI_CHECK(tas.cursor == 2);
    SETIN(&fake, "X"); TA_FRAME();                  /* insert mid-string */
    TIMUI_CHECK(strcmp(text, "abXc") == 0 && tas.cursor == 3);
    SETIN(&fake, "\x1b[H"); TA_FRAME();             /* HOME */
    TIMUI_CHECK(tas.cursor == 0);
    SETIN(&fake, "\x1b[3~"); TA_FRAME();            /* DELETE forward -> "bXc" */
    TIMUI_CHECK(strcmp(text, "bXc") == 0 && tas.cursor == 0);
    SETIN(&fake, "\x7f"); TA_FRAME();               /* backspace at 0: no-op */
    TIMUI_CHECK(strcmp(text, "bXc") == 0 && tas.cursor == 0);
    SETIN(&fake, "\x1b[F"); TA_FRAME();             /* END */
    TIMUI_CHECK(tas.cursor == 3);
    SETIN(&fake, "\x1b[C"); TA_FRAME();             /* RIGHT at end: no-op */
    TIMUI_CHECK(tas.cursor == 3);
    SETIN(&fake, "\x7f"); TA_FRAME();               /* backspace at end -> "bX" */
    TIMUI_CHECK(strcmp(text, "bX") == 0 && tas.cursor == 2);
#undef TA_FRAME
    timui_close(ui);
}

TIMUI_TEST(test_text_area_same_frame_edit_order){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[16] = "ab";
    TimuiTextAreaState tas = { text, sizeof text, 2, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 20, 3);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
#define TAO_FRAME() do{ timui_begin(ui,&f); timui_text_area(f, TIMUI_ID("tao"), r, &tas); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); TAO_FRAME();
    SETIN(&fake, "\x1b[<0;2;1m"); TAO_FRAME();
    SETIN(&fake, "\x7f""c"); TAO_FRAME();
    TIMUI_CHECK(strcmp(text, "ac") == 0);
    TIMUI_CHECK(tas.cursor == 2);
#undef TAO_FRAME
    timui_close(ui);
}

TIMUI_TEST(test_text_area_scroll_applies_before_draw){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    char text[16] = "a\nb\nc";
    TimuiTextAreaState tas = { text, sizeof text, 5, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 8, 2);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 5, &al);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_text_area(f, TIMUI_ID("tas"), r, &tas);
    TIMUI_CHECK(tas.scroll_y == 1);
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == 'b');
    TIMUI_CHECK(timui_cells_get(buf, 0, 1)->codepoint == 'c');
    timui_end(f);
    timui_close(ui);
}

/* F1.3: cursor movement and DELETE step whole UTF-8 codepoints. */
TIMUI_TEST(test_text_area_cursor_utf8){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[16] = {0};
    TimuiTextAreaState tas = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 20, 3);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 10, &al);
#define TA_FRAME() do{ timui_begin(ui,&f); timui_text_area(f, TIMUI_ID("tu"), r, &tas); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); TA_FRAME();
    SETIN(&fake, "\x1b[<0;2;1m"); TA_FRAME();
    SETIN(&fake, "a\xC3\xA9""b"); TA_FRAME();       /* "aéb" — é is C3 A9, cursor=4 */
    TIMUI_CHECK(tas.cursor == 4);
    SETIN(&fake, "\x1b[D"); TA_FRAME();             /* LEFT past 'b' -> 3 */
    TIMUI_CHECK(tas.cursor == 3);
    SETIN(&fake, "\x1b[D"); TA_FRAME();             /* LEFT past é (2 bytes) -> 1 */
    TIMUI_CHECK(tas.cursor == 1);
    SETIN(&fake, "\x1b[3~"); TA_FRAME();            /* DELETE whole é -> "ab" */
    TIMUI_CHECK(strcmp(text, "ab") == 0 && tas.cursor == 1);
#undef TA_FRAME
    timui_close(ui);
}

TIMUI_TEST(test_text_area_grapheme_edit){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[32] = {0};
    TimuiTextAreaState tas = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 20, 3);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
#define TAG_FRAME() do{ timui_begin(ui,&f); timui_text_area(f, TIMUI_ID("tg"), r, &tas); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); TAG_FRAME();
    SETIN(&fake, "\x1b[<0;2;1m"); TAG_FRAME();
    SETIN(&fake, "a" WAVE_SKIN "b"); TAG_FRAME();
    TIMUI_CHECK(strcmp(text, "a" WAVE_SKIN "b") == 0 && tas.cursor == 10);
    SETIN(&fake, "\x1b[D"); TAG_FRAME();
    TIMUI_CHECK(tas.cursor == 9);
    SETIN(&fake, "\x1b[D"); TAG_FRAME();
    TIMUI_CHECK(tas.cursor == 1);
    SETIN(&fake, "\x1b[3~"); TAG_FRAME();
    TIMUI_CHECK(strcmp(text, "ab") == 0 && tas.cursor == 1);
    SETIN(&fake, HEART_VS); TAG_FRAME();
    TIMUI_CHECK(strcmp(text, "a" HEART_VS "b") == 0 && tas.cursor == 7);
    SETIN(&fake, "\x7f"); TAG_FRAME();
    TIMUI_CHECK(strcmp(text, "ab") == 0 && tas.cursor == 1);
#undef TAG_FRAME
    timui_close(ui);
}

TIMUI_TEST(test_text_area_submit_plain_enter){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[32] = {0};
    TimuiTextAreaState tas = { text, sizeof text, 0, 0 };
    TimuiTextAreaResult res;
    TimuiRect r = TIMUI_RECT(0, 0, 20, 3);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
#define TAS_FRAME() do{ timui_begin(ui,&f); res = timui_text_area_mut(f, TIMUI_ID("ts"), r, &tas, TIMUI_TEXT_AREA_ENTER_SUBMITS); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); TAS_FRAME();
    SETIN(&fake, "\x1b[<0;2;1m"); TAS_FRAME();
    SETIN(&fake, "hello"); TAS_FRAME();
    TIMUI_CHECK(res.changed && !res.submitted && strcmp(text, "hello") == 0 && tas.cursor == 5);
    SETIN(&fake, "\r"); TAS_FRAME();
    TIMUI_CHECK(res.submitted && !res.changed && strcmp(text, "hello") == 0 && tas.cursor == 5);
#undef TAS_FRAME
    timui_close(ui);
}

TIMUI_TEST(test_text_area_ex_returns_state){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[16] = {0};
    TimuiTextAreaState tas = { text, sizeof text, 0, 0 };
    TimuiTextAreaResult res;
    TimuiRect r = TIMUI_RECT(0, 0, 20, 3);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
    SETIN(&fake, "\x1b[<0;2;1M");
    timui_begin(ui, &f); res = timui_text_area_ex(f, TIMUI_ID("tx"), r, tas, 0); timui_end(f);
    SETIN(&fake, "\x1b[<0;2;1m");
    timui_begin(ui, &f); res = timui_text_area_ex(f, TIMUI_ID("tx"), r, res.state, 0); timui_end(f);
    SETIN(&fake, "x");
    timui_begin(ui, &f); res = timui_text_area_ex(f, TIMUI_ID("tx"), r, tas, 0); timui_end(f);
    TIMUI_CHECK(res.changed && strcmp(text, "x") == 0);
    TIMUI_CHECK(tas.cursor == 0 && res.state.cursor == 1);
    timui_close(ui);
}

TIMUI_TEST(test_text_area_shift_enter_inserts_newline){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[32] = {0};
    TimuiTextAreaState tas = { text, sizeof text, 0, 0 };
    TimuiTextAreaResult res;
    TimuiRect r = TIMUI_RECT(0, 0, 20, 3);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
#define TAN_FRAME() do{ timui_begin(ui,&f); res = timui_text_area_mut(f, TIMUI_ID("tn"), r, &tas, TIMUI_TEXT_AREA_ENTER_SUBMITS); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); TAN_FRAME();
    SETIN(&fake, "\x1b[<0;2;1m"); TAN_FRAME();
    SETIN(&fake, "a\x1b[13;2u""b"); TAN_FRAME();
    TIMUI_CHECK(res.changed && !res.submitted);
    TIMUI_CHECK(strcmp(text, "a\nb") == 0 && tas.cursor == 3);
#undef TAN_FRAME
    timui_close(ui);
}

TIMUI_TEST(test_text_area_multi_enter_segments){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[32] = {0};
    char got[32] = {0};
    TimuiTextAreaState tas = { text, sizeof text, 0, 0 };
    TimuiTextAreaResult res;
    TimuiRect r = TIMUI_RECT(0, 0, 20, 3);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
#define TAM_FRAME() do{ timui_begin(ui,&f); \
        res = timui_text_area_mut(f, TIMUI_ID("tm"), r, &tas, TIMUI_TEXT_AREA_ENTER_SUBMITS); \
        if(res.submitted){ strcpy(got, text); text[0] = '\0'; tas.cursor = 0; tas.scroll_y = 0; } \
        timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); TAM_FRAME();
    SETIN(&fake, "\x1b[<0;2;1m"); TAM_FRAME();
    SETIN(&fake, "one\rtwo\r"); TAM_FRAME();
    TIMUI_CHECK(res.submitted && strcmp(got, "one") == 0);
    TAM_FRAME();
    TIMUI_CHECK(res.submitted && strcmp(got, "two") == 0);
    TAM_FRAME();
    TIMUI_CHECK(!res.submitted && strcmp(text, "") == 0);
#undef TAM_FRAME
    timui_close(ui);
}

TIMUI_TEST(test_text_area_paste_preserves_newlines){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[64] = {0};
    TimuiTextAreaState tas = { text, sizeof text, 0, 0 };
    TimuiTextAreaResult res;
    TimuiRect r = TIMUI_RECT(0, 0, 20, 3);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
#define TAP_FRAME() do{ timui_begin(ui,&f); res = timui_text_area_mut(f, TIMUI_ID("tp"), r, &tas, TIMUI_TEXT_AREA_ENTER_SUBMITS); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); TAP_FRAME();
    SETIN(&fake, "\x1b[<0;2;1m"); TAP_FRAME();
    SETIN(&fake, "\x1b[200~hello\nworld\x1b[201~"); TAP_FRAME();
    TIMUI_CHECK(res.changed && !res.submitted);
    TIMUI_CHECK(strcmp(text, "hello\nworld") == 0 && tas.cursor == 11);
#undef TAP_FRAME
    timui_close(ui);
}

TIMUI_TEST(test_text_area_submit_unfocused_noop){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[32] = "seed";
    TimuiTextAreaState tas = { text, sizeof text, 4, 0 };
    TimuiTextAreaResult res;
    TimuiRect r = TIMUI_RECT(0, 0, 20, 3);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
    SETIN(&fake, "ignored\r");
    timui_begin(ui, &f);
    res = timui_text_area_mut(f, TIMUI_ID("tu-noop"), r, &tas, TIMUI_TEXT_AREA_ENTER_SUBMITS);
    timui_end(f);
    TIMUI_CHECK(!res.focused && !res.changed && !res.submitted);
    TIMUI_CHECK(strcmp(text, "seed") == 0 && tas.cursor == 4);
    timui_close(ui);
}

TIMUI_TEST(test_text_area_shift_enter_then_plain_enter){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[32] = {0};
    TimuiTextAreaState tas = { text, sizeof text, 0, 0 };
    TimuiTextAreaResult res;
    TimuiRect r = TIMUI_RECT(0, 0, 20, 3);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
#define TASM_FRAME() do{ timui_begin(ui,&f); res = timui_text_area_mut(f, TIMUI_ID("tsm"), r, &tas, TIMUI_TEXT_AREA_ENTER_SUBMITS); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); TASM_FRAME();
    SETIN(&fake, "\x1b[<0;2;1m"); TASM_FRAME();
    SETIN(&fake, "a\x1b[13;2u""b\r"); TASM_FRAME();
    TIMUI_CHECK(res.changed && res.submitted);
    TIMUI_CHECK(strcmp(text, "a\nb") == 0 && tas.cursor == 3);
#undef TASM_FRAME
    timui_close(ui);
}

TIMUI_TEST(test_text_area_wrapper_enter_inserts_newline){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[16] = {0};
    TimuiTextAreaState tas = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 20, 3);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 10, &al);
#define TAW_FRAME() do{ timui_begin(ui,&f); timui_text_area(f, TIMUI_ID("tw"), r, &tas); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); TAW_FRAME();
    SETIN(&fake, "\x1b[<0;2;1m"); TAW_FRAME();
    SETIN(&fake, "a\rb"); TAW_FRAME();
    TIMUI_CHECK(strcmp(text, "a\nb") == 0 && tas.cursor == 3);
#undef TAW_FRAME
    timui_close(ui);
}

/* Pass-3: cap==0 with a focused text_area must not write past the buffer
 * (the guard mirrors input_line_buf's cap==0 check). */
TIMUI_TEST(test_text_area_zero_cap_safe){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char storage[2] = { 'X', '\0' };
    TimuiTextAreaState tas = { storage, 0, 0, 0 };   /* cap 0 */
    TimuiRect r = TIMUI_RECT(0, 0, 10, 3);
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 5, &al);
    SETIN(&fake, "\x1b[<0;2;1M");                                   /* focus */
    timui_begin(ui, &f); timui_text_area(f, TIMUI_ID("tz"), r, &tas); timui_end(f);
    SETIN(&fake, "\x1b[<0;2;1m");
    timui_begin(ui, &f); timui_text_area(f, TIMUI_ID("tz"), r, &tas); timui_end(f);
    TIMUI_CHECK(storage[0] == 'X');              /* cap==0 guard -> no OOB NUL write */
    timui_close(ui);
}

/* Pass-4 Y1: a caller-supplied cursor >= cap must be clamped, not written
 * past the buffer. A guard byte just past cap survives. */
TIMUI_TEST(test_text_area_cursor_overcap_safe){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char storage[11];
    TimuiTextAreaState tas = { storage, 10, 10, 0 };   /* cursor == cap (overcap) */
    TimuiRect r = TIMUI_RECT(0, 0, 10, 3);
    storage[10] = 'G';                                 /* guard byte past cap */
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 5, &al);
    SETIN(&fake, "\x1b[<0;2;1M");                                   /* focus */
    timui_begin(ui, &f); timui_text_area(f, TIMUI_ID("tc"), r, &tas); timui_end(f);
    SETIN(&fake, "\x1b[<0;2;1m");
    timui_begin(ui, &f); timui_text_area(f, TIMUI_ID("tc"), r, &tas); timui_end(f);
    TIMUI_CHECK(storage[10] == 'G');               /* cursor clamped -> no OOB write */
    timui_close(ui);
}

TIMUI_TEST(test_text_area_cursor_clamped_to_text){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    char text[8] = { 'a', 'b', 'c', '\0', 'X', 'Y', 'Z', '\0' };
    TimuiTextAreaState tas = { text, sizeof text, 6, 0 };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 5, &al);

    timui_begin(ui, &f);
    timui_text_area(f, TIMUI_ID("ta"), TIMUI_RECT(0, 0, 10, 3), &tas);
    timui_end(f);

    TIMUI_CHECK(tas.cursor == 3);
    TIMUI_CHECK(strcmp(text, "abc") == 0);
    timui_close(ui);
}

TIMUI_TEST(test_text_area_clamps_unterminated){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    char storage[8] = { 'a', 'b', 'c', 'd', 'E', 'F', 'G', '\0' };
    TimuiTextAreaState tas = { storage, 4, 4, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 20, 3);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 5, &al);

#define TACU_FRAME() do{ timui_begin(ui,&f); timui_text_area(f, TIMUI_ID("ta"), r, &tas); timui_end(f); }while(0)
    timui_fake_set_input(&fake, "\x1b[<0;2;1M", sizeof("\x1b[<0;2;1M") - 1); TACU_FRAME();
    timui_fake_set_input(&fake, "\x1b[<0;2;1m", sizeof("\x1b[<0;2;1m") - 1); TACU_FRAME();
    timui_fake_set_input(&fake, "\x7f", sizeof("\x7f") - 1); TACU_FRAME();
    TIMUI_CHECK(storage[0] == 'a' && storage[1] == 'b' && storage[2] == '\0');
    TIMUI_CHECK(storage[4] == 'E' && storage[5] == 'F' && storage[6] == 'G');
    TIMUI_CHECK(tas.cursor == 2);
#undef TACU_FRAME
    timui_close(ui);
}

/* ---- ConPTY (#55) ---- */
TIMUI_TEST(test_conpty_unsupported){
    TimuiTransport tr;
    int pid;
    memset(&tr, 0xaa, sizeof tr);
    pid = 1234;
    TIMUI_CHECK(timui_conpty_open(NULL, &pid) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(timui_conpty_open(&tr, NULL) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(timui_conpty_open(&tr, &pid) == TIMUI_ERR_UNSUPPORTED);
    TIMUI_CHECK(pid == -1);
    TIMUI_CHECK(tr.write == NULL);
    TIMUI_CHECK(tr.read == NULL);
    TIMUI_CHECK(tr.flush == NULL);
    TIMUI_CHECK(tr.close == NULL);
    TIMUI_CHECK(tr.ctx == NULL);
    TIMUI_CHECK(timui_conpty_resize(NULL, 80, 24) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(timui_conpty_resize(&tr, 0, 24) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(timui_conpty_resize(&tr, 80, 0) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(timui_conpty_resize(&tr, 80, 24) == TIMUI_ERR_UNSUPPORTED);
    timui_conpty_close(&tr, pid);
}

TIMUI_TEST(test_conpty_chunk_and_size_guards){
    size_t chunk;
    TIMUI_CHECK(timui_conpty_io_chunk_for_test(0) == 0);
    TIMUI_CHECK(timui_conpty_io_chunk_for_test(1) == 1);
    chunk = timui_conpty_io_chunk_for_test((size_t)INT_MAX + 123u);
    TIMUI_CHECK(chunk > 0);
    TIMUI_CHECK(chunk <= (size_t)INT_MAX);
    TIMUI_CHECK(chunk <= (size_t)INT_MAX + 123u);
    chunk = timui_conpty_io_chunk_for_test(SIZE_MAX);
    TIMUI_CHECK(chunk > 0);
    TIMUI_CHECK(chunk <= (size_t)INT_MAX);

    TIMUI_CHECK(timui_conpty_size_valid_for_test(80, 24));
    TIMUI_CHECK(timui_conpty_size_valid_for_test(32767, 32767));
    TIMUI_CHECK(!timui_conpty_size_valid_for_test(0, 24));
    TIMUI_CHECK(!timui_conpty_size_valid_for_test(80, 0));
    TIMUI_CHECK(!timui_conpty_size_valid_for_test(-1, 24));
    TIMUI_CHECK(!timui_conpty_size_valid_for_test(80, -1));
    TIMUI_CHECK(!timui_conpty_size_valid_for_test(32768, 24));
    TIMUI_CHECK(!timui_conpty_size_valid_for_test(80, 32768));
}
