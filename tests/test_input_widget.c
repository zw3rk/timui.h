/*
 * test_input_widget.c — single-line input (T5.4).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

#define SETIN(fake, lit) timui_fake_set_input((fake), (lit), sizeof(lit) - 1)
#define WAVE_SKIN "\xF0\x9F\x91\x8B\xF0\x9F\x8F\xBD" /* waving hand + skin tone */
#define HEART_VS  "\xE2\x9D\xA4\xEF\xB8\x8F"         /* heavy black heart + VS16 */

TIMUI_TEST(test_input_types_and_submits){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    char buf[16] = {0};
    bool submitted;
    TimuiRect r = TIMUI_RECT(0, 0, 20, 1);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);

    /* click to focus the input */
    SETIN(&fake, "\x1b[<0;2;1M");
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("in"), r, buf, sizeof buf);
    timui_end(f);
    SETIN(&fake, "\x1b[<0;2;1m");
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("in"), r, buf, sizeof buf);
    timui_end(f);

    /* type "hi" */
    SETIN(&fake, "hi");
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("in"), r, buf, sizeof buf);
    timui_end(f);
    TIMUI_CHECK(strcmp(buf, "hi") == 0);

    /* backspace -> "h" */
    SETIN(&fake, "\x7f");
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("in"), r, buf, sizeof buf);
    timui_end(f);
    TIMUI_CHECK(strcmp(buf, "h") == 0);

    /* enter -> submit */
    SETIN(&fake, "\r");
    timui_begin(ui, &f);
    submitted = timui_input_line_buf(f, TIMUI_ID("in"), r, buf, sizeof buf);
    timui_end(f);
    TIMUI_CHECK(submitted);

    timui_close(ui);
}

/* V7: a multibyte codepoint must not be split at the cap boundary, and
 * backspace must remove a whole codepoint. */
TIMUI_TEST(test_input_line_utf8_no_split){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    char buf[4] = {0};   /* fits "ab" + NUL, but not "ab" + é (2 more bytes) */
    TimuiRect r = TIMUI_RECT(0, 0, 20, 1);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
    SETIN(&fake, "\x1b[<0;2;1M");                 /* press to focus */
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("u"), r, buf, sizeof buf);
    timui_end(f);
    SETIN(&fake, "\x1b[<0;2;1m");                 /* release */
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("u"), r, buf, sizeof buf);
    timui_end(f);

    SETIN(&fake, "ab\xC3\xA9");                   /* "ab" + é: é must not be split in */
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("u"), r, buf, sizeof buf);
    timui_end(f);
    TIMUI_CHECK(strcmp(buf, "ab") == 0);          /* é skipped whole, not half-written */
    timui_close(ui);
}

TIMUI_TEST(test_input_line_utf8_backspace){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    char buf[8] = {0};
    TimuiRect r = TIMUI_RECT(0, 0, 20, 1);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
    SETIN(&fake, "\x1b[<0;2;1M");
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("u"), r, buf, sizeof buf);
    timui_end(f);
    SETIN(&fake, "\x1b[<0;2;1m");
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("u"), r, buf, sizeof buf);
    timui_end(f);

    SETIN(&fake, "\xC3\xA9");                     /* é */
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("u"), r, buf, sizeof buf);
    timui_end(f);
    TIMUI_CHECK(strcmp(buf, "\xC3\xA9") == 0);    /* valid UTF-8 é */
    SETIN(&fake, "\x7f");                          /* backspace -> whole é removed */
    timui_begin(ui, &f);
    timui_input_line_buf(f, TIMUI_ID("u"), r, buf, sizeof buf);
    timui_end(f);
    TIMUI_CHECK(strcmp(buf, "") == 0);            /* no dangling lead byte */
    timui_close(ui);
}

TIMUI_TEST(test_input_line_grapheme_backspace){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    char buf[16] = {0};
    TimuiRect r = TIMUI_RECT(0, 0, 20, 1);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
    SETIN(&fake, "\x1b[<0;2;1M");
    timui_begin(ui, &f); timui_input_line_buf(f, TIMUI_ID("ug"), r, buf, sizeof buf); timui_end(f);
    SETIN(&fake, "\x1b[<0;2;1m");
    timui_begin(ui, &f); timui_input_line_buf(f, TIMUI_ID("ug"), r, buf, sizeof buf); timui_end(f);

    SETIN(&fake, WAVE_SKIN);
    timui_begin(ui, &f); timui_input_line_buf(f, TIMUI_ID("ug"), r, buf, sizeof buf); timui_end(f);
    TIMUI_CHECK(strcmp(buf, WAVE_SKIN) == 0);
    SETIN(&fake, "\x7f");
    timui_begin(ui, &f); timui_input_line_buf(f, TIMUI_ID("ug"), r, buf, sizeof buf); timui_end(f);
    TIMUI_CHECK(strcmp(buf, "") == 0);
    timui_close(ui);
}

TIMUI_TEST(test_input_line_buf_clipped){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiCellBuffer *cells;
    char buf[16] = "abcdef";
    TimuiRect r = TIMUI_RECT(1, 1, 4, 1);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 10, 3, &al);

    timui_begin(ui, &f);
    cells = timui_frame_buffer(f);
    (void)timui_input_line_buf(f, TIMUI_ID("short"), r, buf, sizeof buf);
    TIMUI_CHECK(timui_cells_get(cells, 5, 1)->codepoint == 0);
    timui_end(f);

    timui_close(ui);
}

TIMUI_TEST(test_input_line_buf_clamps_unterminated){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    char storage[8] = { 'a', 'b', 'c', 'd', 'E', 'F', 'G', '\0' };
    TimuiRect r = TIMUI_RECT(0, 0, 10, 1);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 5, &al);

#define ILF() do{ timui_begin(ui,&f); (void)timui_input_line_buf(f, TIMUI_ID("line"), r, storage, 4); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); ILF();
    SETIN(&fake, "\x1b[<0;2;1m"); ILF();
    SETIN(&fake, "\x7f"); ILF();
    TIMUI_CHECK(storage[0] == 'a' && storage[1] == 'b' && storage[2] == '\0');
    TIMUI_CHECK(storage[4] == 'E' && storage[5] == 'F' && storage[6] == 'G');
#undef ILF
    timui_close(ui);
}

/* F1.5: input_field — in-line cursor editing (single line). */
TIMUI_TEST(test_input_field_edit){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[16] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 10, 1);
    bool submitted = false;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
#define IF_FRAME() do{ timui_begin(ui,&f); submitted = timui_input_field(f, TIMUI_ID("if"), r, &is); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); IF_FRAME();       /* click to focus */
    SETIN(&fake, "\x1b[<0;2;1m"); IF_FRAME();
    SETIN(&fake, "abc"); IF_FRAME();
    TIMUI_CHECK(strcmp(text, "abc") == 0 && is.cursor == 3);
    SETIN(&fake, "\x1b[D"); IF_FRAME();             /* LEFT */
    TIMUI_CHECK(is.cursor == 2);
    SETIN(&fake, "Z"); IF_FRAME();                  /* insert mid-string */
    TIMUI_CHECK(strcmp(text, "abZc") == 0 && is.cursor == 3);
    SETIN(&fake, "\x1b[H"); IF_FRAME();             /* HOME */
    TIMUI_CHECK(is.cursor == 0);
    SETIN(&fake, "\x1b[3~"); IF_FRAME();            /* DELETE 'a' -> "bZc" */
    TIMUI_CHECK(strcmp(text, "bZc") == 0 && is.cursor == 0);
    SETIN(&fake, "\x1b[F"); IF_FRAME();             /* END */
    TIMUI_CHECK(is.cursor == 3);
    SETIN(&fake, "\x7f"); IF_FRAME();               /* backspace -> "bZ" */
    TIMUI_CHECK(strcmp(text, "bZ") == 0 && is.cursor == 2);
    SETIN(&fake, "\r"); IF_FRAME();                 /* Enter submits */
    TIMUI_CHECK(submitted);
#undef IF_FRAME
    timui_close(ui);
}

TIMUI_TEST(test_input_field_same_frame_edit_order){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[16] = "ab";
    TimuiInputState is = { text, sizeof text, 2, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 20, 1);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
#define IO_FRAME() do{ timui_begin(ui,&f); (void)timui_input_field(f, TIMUI_ID("io"), r, &is); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); IO_FRAME();
    SETIN(&fake, "\x1b[<0;2;1m"); IO_FRAME();
    SETIN(&fake, "\x7f""c"); IO_FRAME();
    TIMUI_CHECK(strcmp(text, "ac") == 0);
    TIMUI_CHECK(is.cursor == 2);
#undef IO_FRAME
    timui_close(ui);
}

TIMUI_TEST(test_input_field_grapheme_edit){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[32] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 20, 1);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 5, &al);
#define GF_FRAME() do{ timui_begin(ui,&f); (void)timui_input_field(f, TIMUI_ID("gf"), r, &is); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); GF_FRAME();
    SETIN(&fake, "\x1b[<0;2;1m"); GF_FRAME();
    SETIN(&fake, "a" WAVE_SKIN "b"); GF_FRAME();
    TIMUI_CHECK(strcmp(text, "a" WAVE_SKIN "b") == 0 && is.cursor == 10);
    SETIN(&fake, "\x1b[D"); GF_FRAME();             /* LEFT past b */
    TIMUI_CHECK(is.cursor == 9);
    SETIN(&fake, "\x1b[D"); GF_FRAME();             /* LEFT past whole emoji cluster */
    TIMUI_CHECK(is.cursor == 1);
    SETIN(&fake, "\x1b[3~"); GF_FRAME();            /* DELETE whole 👋🏽 */
    TIMUI_CHECK(strcmp(text, "ab") == 0 && is.cursor == 1);
    SETIN(&fake, HEART_VS); GF_FRAME();
    TIMUI_CHECK(strcmp(text, "a" HEART_VS "b") == 0 && is.cursor == 7);
    SETIN(&fake, "\x7f"); GF_FRAME();               /* backspace removes whole VS16 cluster */
    TIMUI_CHECK(strcmp(text, "ab") == 0 && is.cursor == 1);
#undef GF_FRAME
    timui_close(ui);
}

TIMUI_TEST(test_input_field_cursor_clamped_to_text){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    char text[8] = { 'a', 'b', 'c', '\0', 'X', 'Y', 'Z', '\0' };
    TimuiInputState is = { text, sizeof text, 6, 0 };

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 5, &al);

    timui_begin(ui, &f);
    (void)timui_input_field(f, TIMUI_ID("if"), TIMUI_RECT(0, 0, 10, 1), &is);
    timui_end(f);

    TIMUI_CHECK(is.cursor == 3);
    TIMUI_CHECK(strcmp(text, "abc") == 0);
    timui_close(ui);
}

TIMUI_TEST(test_input_field_clamps_unterminated){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    char storage[8] = { 'a', 'b', 'c', 'd', 'E', 'F', 'G', '\0' };
    TimuiInputState is = { storage, 4, 4, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 10, 1);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 5, &al);

#define ICU() do{ timui_begin(ui,&f); (void)timui_input_field(f, TIMUI_ID("field"), r, &is); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); ICU();
    SETIN(&fake, "\x1b[<0;2;1m"); ICU();
    SETIN(&fake, "\x7f"); ICU();
    TIMUI_CHECK(storage[0] == 'a' && storage[1] == 'b' && storage[2] == '\0');
    TIMUI_CHECK(storage[4] == 'E' && storage[5] == 'F' && storage[6] == 'G');
    TIMUI_CHECK(is.cursor == 2);
#undef ICU
    timui_close(ui);
}

/* Regression: several Enters arriving in ONE frame (a paste, or input faster
 * than the frame rate) must submit ONE segment per frame, not merge — so
 * "one\rtwo\r" yields "one" then "two", never "onetwo". The caller consumes and
 * clears on each submit (as chat.c does); the post-Enter tail is deferred. */
TIMUI_TEST(test_input_field_multi_submit){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[32] = {0};
    char got[32] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 20, 1);
    bool submitted = false;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 5, &al);
#define MS_FRAME() do{ timui_begin(ui,&f); \
        submitted = timui_input_field(f, TIMUI_ID("in"), r, &is); \
        if(submitted){ strcpy(got, text); text[0]='\0'; is.cursor=0; is.scroll_x=0; } \
        timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); MS_FRAME();          /* click to focus */
    SETIN(&fake, "\x1b[<0;2;1m"); MS_FRAME();
    SETIN(&fake, "one\rtwo\r"); MS_FRAME();            /* two submits in one frame */
    TIMUI_CHECK(submitted && strcmp(got, "one") == 0); /* first segment, NOT "onetwo" */
    SETIN(&fake, ""); MS_FRAME();                      /* no new input: deferred "two" */
    TIMUI_CHECK(submitted && strcmp(got, "two") == 0); /* deferred second segment */
    SETIN(&fake, ""); MS_FRAME();                      /* nothing left */
    TIMUI_CHECK(!submitted);
#undef MS_FRAME
    timui_close(ui);
}

/* Bracketed paste — a real paste, or a Finder drag-drop of a file path — must
 * reach the focused input. The terminal wraps it in ESC[200~ ... ESC[201~; the
 * parser turns that into a PASTE event, which timui_begin feeds to the focused
 * input (control bytes, incl. newlines, are dropped for the single-line field). */
TIMUI_TEST(test_input_field_paste){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[64] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 40, 1);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 50, 5, &al);
#define PF() do{ timui_begin(ui,&f); (void)timui_input_field(f, TIMUI_ID("in"), r, &is); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); PF();          /* click to focus */
    SETIN(&fake, "\x1b[<0;2;1m"); PF();
    SETIN(&fake, "\x1b[200~/tmp/cat.png\x1b[201~"); PF();   /* drag-drop / paste */
    TIMUI_CHECK(strcmp(text, "/tmp/cat.png") == 0);
#undef PF
    timui_close(ui);
}

/* A paste can arrive across SEVERAL reads (a slow drag-drop): the parser emits
 * the chunk at each feed boundary, so a frame — or the whole paste — spans
 * multiple PASTE events. They must ACCUMULATE into the full string, not
 * overwrite each other (the 'path truncated to the first chunk' bug). */
TIMUI_TEST(test_input_field_paste_split){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[64] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 40, 1);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 50, 5, &al);
#define PF() do{ timui_begin(ui,&f); (void)timui_input_field(f, TIMUI_ID("in"), r, &is); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); PF();
    SETIN(&fake, "\x1b[<0;2;1m"); PF();
    SETIN(&fake, "\x1b[200~/Users/angerman"); PF();          /* start + chunk, no terminator */
    SETIN(&fake, "/Documents/cat.png\x1b[201~"); PF();       /* rest + terminator */
    TIMUI_CHECK(strcmp(text, "/Users/angerman/Documents/cat.png") == 0);
#undef PF
    timui_close(ui);
}

TIMUI_TEST(test_input_field_paste_split_utf8){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[64] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 40, 1);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 50, 5, &al);
#define PFU() do{ timui_begin(ui,&f); (void)timui_input_field(f, TIMUI_ID("in"), r, &is); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); PFU();
    SETIN(&fake, "\x1b[<0;2;1m"); PFU();
    SETIN(&fake, "\x1b[200~\xC3"); PFU();
    TIMUI_CHECK(text[0] == '\0');
    SETIN(&fake, "\xA9\x1b[201~"); PFU();
    TIMUI_CHECK(strcmp(text, "\xC3\xA9") == 0);
    TIMUI_CHECK(is.cursor == 2);
#undef PFU
    timui_close(ui);
}

TIMUI_TEST(test_input_field_paste_drops_controls){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[64] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 40, 1);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 50, 5, &al);
#define PCF() do{ timui_begin(ui,&f); (void)timui_input_field(f, TIMUI_ID("in"), r, &is); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); PCF();
    SETIN(&fake, "\x1b[<0;2;1m"); PCF();
    SETIN(&fake, "\x1b[200~a\nb\rc\t\x1b[201~"); PCF();
    TIMUI_CHECK(strcmp(text, "abc") == 0);
#undef PCF
    timui_close(ui);
}

TIMUI_TEST(test_input_field_paste_invalid_utf8_replaced){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[32] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 30, 1);
    static const char expect[] = "A\xEF\xBF\xBD" "B";   /* A U+FFFD B */
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 5, &al);
#define PIF() do{ timui_begin(ui,&f); (void)timui_input_field(f, TIMUI_ID("in"), r, &is); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); PIF();
    SETIN(&fake, "\x1b[<0;2;1m"); PIF();
    timui_fake_set_input(&fake, "\x1b[200~A\xC0""B\x1b[201~", sizeof("\x1b[200~A\xC0""B\x1b[201~") - 1);
    PIF();
    TIMUI_CHECK(strcmp(text, expect) == 0);
    TIMUI_CHECK(is.cursor == sizeof(expect) - 1);
#undef PIF
    timui_close(ui);
}

TIMUI_TEST(test_paste_preserves_text_order){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiStr typed;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 3, &al);

    SETIN(&fake, "x\x1b[200~P\x1b[201~y");
    timui_begin(ui, &f);
    typed = timui_text_input(f);
    TIMUI_CHECK(typed.len == 3 && memcmp(typed.ptr, "xPy", 3) == 0);
    timui_end(f);
    timui_close(ui);
}

TIMUI_TEST(test_input_field_paste_enter_order){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[32] = {0};
    char got[32] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 20, 1);
    bool submitted = false;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 5, &al);
#define PE_FRAME() do{ timui_begin(ui,&f); \
        submitted = timui_input_field(f, TIMUI_ID("in"), r, &is); \
        if(submitted){ strcpy(got, text); text[0]='\0'; is.cursor=0; is.scroll_x=0; } \
        timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); PE_FRAME();
    SETIN(&fake, "\x1b[<0;2;1m"); PE_FRAME();
    SETIN(&fake, "\x1b[200~abc\x1b[201~\r"); PE_FRAME();
    TIMUI_CHECK(submitted && strcmp(got, "abc") == 0);
#undef PE_FRAME
    timui_close(ui);
}

/* A long burst of typed text in one read — how Ghostty inserts a drag-drop path
 * (plain text, not a paste) — must reach the input in full. The parser emits one
 * event per char, so a 73-char path is 73 events; the queue must hold them all
 * (it was 16, dropping all but the first 16 chars: the '/Users/angerman/' bug). */
TIMUI_TEST(test_input_field_text_burst){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[128] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 40, 1);
    static const char path[] = "/Users/angerman/Projects/iohk/cardano-bean/reports/bean-forge-aggregate.png";
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 90, 5, &al);
#define PF() do{ timui_begin(ui,&f); (void)timui_input_field(f, TIMUI_ID("in"), r, &is); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); PF();
    SETIN(&fake, "\x1b[<0;2;1m"); PF();
    timui_fake_set_input(&fake, path, sizeof path - 1); PF();   /* whole path, one read */
    TIMUI_CHECK(strcmp(text, path) == 0);
#undef PF
    timui_close(ui);
}

TIMUI_TEST(test_input_field_kitty_csi_u_printable){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    char text[16] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 20, 1);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
#define KCF() do{ timui_begin(ui,&f); (void)timui_input_field(f, TIMUI_ID("in"), r, &is); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); KCF();
    SETIN(&fake, "\x1b[<0;2;1m"); KCF();
    SETIN(&fake, "\x1b[97u"); KCF();
    TIMUI_CHECK(strcmp(text, "a") == 0 && is.cursor == 1);
#undef KCF
    timui_close(ui);
}

TIMUI_TEST(test_input_field_kitty_csi_u_shifted_printable){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    char text[16] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 20, 1);

    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
#define KSF() do{ timui_begin(ui,&f); (void)timui_input_field(f, TIMUI_ID("in"), r, &is); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); KSF();
    SETIN(&fake, "\x1b[<0;2;1m"); KSF();
    SETIN(&fake, "\x1b[65;2u"); KSF();
    TIMUI_CHECK(strcmp(text, "A") == 0 && is.cursor == 1);
#undef KSF
    timui_close(ui);
}

/* emacs / readline line-editing keys (ubiquitous on macOS): Ctrl-A/E move to
 * start/end, Ctrl-B/F back/forward, Ctrl-D delete, Ctrl-K/U kill to end/start,
 * Ctrl-W kill the previous word. */
TIMUI_TEST(test_input_field_emacs){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[32] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 20, 1);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 40, 5, &al);
#define PF() do{ timui_begin(ui,&f); (void)timui_input_field(f, TIMUI_ID("in"), r, &is); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); PF();
    SETIN(&fake, "\x1b[<0;2;1m"); PF();
    SETIN(&fake, "hello world"); PF();
    TIMUI_CHECK(is.cursor == 11);
    SETIN(&fake, "\x01"); PF(); TIMUI_CHECK(is.cursor == 0);     /* Ctrl-A -> start */
    SETIN(&fake, "\x05"); PF(); TIMUI_CHECK(is.cursor == 11);    /* Ctrl-E -> end   */
    SETIN(&fake, "\x02"); PF(); TIMUI_CHECK(is.cursor == 10);    /* Ctrl-B -> back  */
    SETIN(&fake, "\x06"); PF(); TIMUI_CHECK(is.cursor == 11);    /* Ctrl-F -> fwd   */
    SETIN(&fake, "\x17"); PF();                                   /* Ctrl-W kills "world" */
    TIMUI_CHECK(strcmp(text, "hello ") == 0 && is.cursor == 6);
    SETIN(&fake, "\x0b"); PF();                                   /* Ctrl-K at end: no-op */
    TIMUI_CHECK(strcmp(text, "hello ") == 0);
    SETIN(&fake, "\x15"); PF();                                   /* Ctrl-U kills to start */
    TIMUI_CHECK(strcmp(text, "") == 0 && is.cursor == 0);
    SETIN(&fake, "abc"); PF();
    SETIN(&fake, "\x01"); PF();                                   /* Ctrl-A */
    SETIN(&fake, "\x04"); PF();                                   /* Ctrl-D deletes 'a' */
    TIMUI_CHECK(strcmp(text, "bc") == 0);
    SETIN(&fake, "\x05"); PF(); SETIN(&fake, "\x0b"); PF();       /* Ctrl-E, Ctrl-K: no-op at end */
    TIMUI_CHECK(strcmp(text, "bc") == 0);
#undef PF
    timui_close(ui);
}

/* timui_input_field_styled draws with the caller's style (to blend into a panel)
 * instead of the theme's input box. */
TIMUI_TEST(test_input_field_styled){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    char text[16] = "hi";
    TimuiInputState is = { text, sizeof text, 2, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 8, 1);
    TimuiStyle custom = timui_style_make(0xFFFFFF, 0x123456, 0);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 3, &al);
    timui_begin(ui, &f);
    (void)timui_input_field_styled(f, TIMUI_ID("in"), r, &is, custom);
    buf = timui_frame_buffer(f);
    TIMUI_CHECK(timui_cells_get(buf, 5, 0)->bg == 0x123456);   /* field painted with the given bg */
    timui_end(f);
    timui_close(ui);
}

/* timui_mouse_clicked reports a click's cell; timui_hyperlink_at maps a cell to
 * its OSC 8 URL — so an app can open a clicked link even with mouse reporting on. */
TIMUI_TEST(test_mouse_click_hyperlink){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
    timui_begin(ui, &f);
    timui_label_hyperlink(f, 2, 0, TIMUI_STR_LIT("link"), "https://x.io",
                          timui_style_make(0x66CCFF, 0, TIMUI_ATTR_UNDERLINE));
    TIMUI_CHECK(timui_hyperlink_at(f, 3, 0) != NULL &&
                strcmp(timui_hyperlink_at(f, 3, 0), "https://x.io") == 0);
    TIMUI_CHECK(timui_hyperlink_at(f, 0, 0) == NULL);   /* no link there */
    timui_end(f);
    SETIN(&fake, "\x1b[<0;4;1M");                        /* left press at cell (3,0) */
    timui_begin(ui, &f);
    { int x = -1, y = -1;
      TIMUI_CHECK(timui_mouse_clicked(f, &x, &y) && x == 3 && y == 0); }
    timui_end(f);
    timui_close(ui);
}

/* F1.5: horizontal scroll keeps the cursor visible; Home scrolls back. */
TIMUI_TEST(test_input_field_scroll){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[32] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 5, 1);           /* width 5 */
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
#define IF_FRAME() do{ timui_begin(ui,&f); (void)timui_input_field(f, TIMUI_ID("if"), r, &is); timui_end(f); }while(0)
    SETIN(&fake, "\x1b[<0;2;1M"); IF_FRAME();
    SETIN(&fake, "\x1b[<0;2;1m"); IF_FRAME();
    SETIN(&fake, "abcdefg"); IF_FRAME();            /* 7 cols into a width-5 field */
    TIMUI_CHECK(is.cursor == 7);
    TIMUI_CHECK(is.scroll_x == 7 - 5 + 1);          /* cursor pinned at the right edge */
    SETIN(&fake, "\x1b[H"); IF_FRAME();             /* HOME -> scroll back to 0 */
    TIMUI_CHECK(is.cursor == 0 && is.scroll_x == 0);
#undef IF_FRAME
    timui_close(ui);
}

/* F1.5: NULL / degenerate guards. */
TIMUI_TEST(test_input_field_guards){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[4] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiInputState nul = { NULL, 4, 0, 0 };
    TIMUI_CHECK(!timui_input_field(NULL, TIMUI_ID("x"), TIMUI_RECT(0,0,4,1), &is));  /* NULL frame */
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 10, 3, &al);
    timui_begin(ui, &f);
    TIMUI_CHECK(!timui_input_field(f, TIMUI_ID("x"), TIMUI_RECT(0,0,4,1), NULL));    /* NULL state */
    TIMUI_CHECK(!timui_input_field(f, TIMUI_ID("x"), TIMUI_RECT(0,0,4,1), &nul));    /* NULL text */
    timui_end(f);
    timui_close(ui);
}

/* F1.4: a focused input requests the hardware cursor at its edit cell; a frame
 * with no focused input hides it (once). */
static int out_contains(const TimuiFakeTransport *fake, const char *needle){
    TimuiStr o = timui_fake_output(fake);
    size_t nl = strlen(needle), i;
    if(o.len < nl) return 0;
    for(i = 0; i + nl <= o.len; i++)
        if(memcmp(o.ptr + i, needle, nl) == 0) return 1;
    return 0;
}
TIMUI_TEST(test_focused_input_cursor){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[16] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 10, 1);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 30, 5, &al);
    SETIN(&fake, "\x1b[<0;2;1M");
    timui_begin(ui,&f); timui_input_field(f, TIMUI_ID("if"), r, &is); timui_end(f);
    SETIN(&fake, "\x1b[<0;2;1m");
    timui_begin(ui,&f); timui_input_field(f, TIMUI_ID("if"), r, &is); timui_end(f);
    SETIN(&fake, "ab");
    timui_fake_clear_output(&fake);
    timui_begin(ui,&f); timui_input_field(f, TIMUI_ID("if"), r, &is); timui_end(f);
    TIMUI_CHECK(is.cursor == 2);
    TIMUI_CHECK(out_contains(&fake, "\x1b[?25h"));       /* cursor shown */
    TIMUI_CHECK(out_contains(&fake, "\x1b[1;3H"));       /* CUP to screen (2,0), 1-based */
    /* a frame that renders no input -> the request is absent -> hide emitted */
    timui_fake_clear_output(&fake);
    timui_begin(ui,&f); timui_end(f);
    TIMUI_CHECK(out_contains(&fake, "\x1b[?25l"));       /* cursor hidden on focus loss */
    timui_close(ui);
}

/* Follow-up: timui_set_focus focuses a widget without a click; timui_focus reads it. */
TIMUI_TEST(test_set_focus_programmatic){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[8] = {0};
    TimuiInputState is = { text, sizeof text, 0, 0 };
    TimuiRect r = TIMUI_RECT(0, 0, 10, 1);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 3, &al);
    timui_begin(ui, &f);
    TIMUI_CHECK(timui_focus(f) == 0);                    /* nothing focused initially */
    timui_set_focus(f, TIMUI_ID("fld"));                 /* focus without a click */
    timui_input_field(f, TIMUI_ID("fld"), r, &is);
    timui_end(f);
    TIMUI_CHECK(timui_focus(f) == TIMUI_ID("fld"));       /* persists */
    SETIN(&fake, "hi");                                   /* type — the field is focused */
    timui_begin(ui, &f);
    timui_input_field(f, TIMUI_ID("fld"), r, &is);
    timui_end(f);
    TIMUI_CHECK(strcmp(text, "hi") == 0);
    timui_set_focus(NULL, 1);                             /* NULL guard: no crash */
    TIMUI_CHECK(timui_focus(NULL) == 0);
    timui_close(ui);
}

/* Follow-up: timui_text_input / timui_char_pressed expose typed chars when no
 * focused input consumes them (digits/space/letters are text, not TimuiKey). */
TIMUI_TEST(test_text_input_accessor){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiStr typed;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 3, &al);
    SETIN(&fake, "d3 ");                                  /* 'd','3',' ' — nothing consumes */
    timui_begin(ui, &f);
    typed = timui_text_input(f);
    TIMUI_CHECK(typed.len == 3);
    TIMUI_CHECK(timui_char_pressed(f, 'd'));
    TIMUI_CHECK(timui_char_pressed(f, '3'));
    TIMUI_CHECK(timui_char_pressed(f, ' '));
    TIMUI_CHECK(!timui_char_pressed(f, 'x'));
    timui_end(f);
    TIMUI_CHECK(!timui_char_pressed(NULL, 'd'));          /* NULL guards */
    TIMUI_CHECK(timui_text_input(NULL).len == 0);
    timui_close(ui);
}

/* Regression: the focused-input cursor (F1.4) is emitted AFTER render_diff, so
 * it moves the physical cursor away from where the diff renderer thinks it is.
 * If the renderer's cross-frame last_x/last_y isn't resynced, the next frame can
 * skip a needed CUP and draw a cell at the cursor position instead of its own.
 * Repro: frame 2 changes exactly the cell the stale last_x/last_y points at. */
TIMUI_TEST(test_cursor_no_diff_desync){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    char text[4] = "a";
    TimuiInputState is = { text, sizeof text, 1, 0 };   /* "a", cursor after it (col 1) */
    TimuiStyle st = timui_style_make(0xffffff, 0, 0);
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 6, 2, &al);
    /* frame 1: focused input "a" at (0,0,3,1) — draws (0,0)..(2,0); after it the
     * renderer's last_x is 3, but render_cursor then moves the cursor to (1,0). */
    timui_begin(ui, &f);
    timui_set_focus(f, TIMUI_ID("fld"));
    timui_input_field(f, TIMUI_ID("fld"), TIMUI_RECT(0,0,3,1), &is);
    timui_end(f);
    /* frame 2: same input (cells unchanged) + a NEW 'Z' at (3,0). (3,0) is the
     * only changed cell and equals the stale last_x — a correct renderer must
     * still emit a CUP to (3,0). */
    timui_fake_clear_output(&fake);
    timui_begin(ui, &f);
    timui_set_focus(f, TIMUI_ID("fld"));
    timui_input_field(f, TIMUI_ID("fld"), TIMUI_RECT(0,0,3,1), &is);
    timui_label(f, 3, 0, TIMUI_STR_LIT("Z"), st);
    timui_end(f);
    TIMUI_CHECK(out_contains(&fake, "\x1b[1;4H"));   /* 'Z' must be positioned at (3,0) */
    timui_close(ui);
}
