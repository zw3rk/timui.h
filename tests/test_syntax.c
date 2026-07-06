/*
 * test_syntax.c — unit tests for src/timui_syntax.c: the promoted table-driven
 * syntax highlighter (timui_highlight) + the read-only code viewer (timui_code)
 * and its pure helpers (timui_hl_color, timui_code_scroll_clamp).
 *
 * Standalone (own main, own tiny CHECK harness — no tests/test.h). It builds the
 * timui library as a single TU (TIMUI_IMPLEMENTATION) so it can reach both the
 * pure highlighter and the render path through the public API. Positive tests
 * assert exact (off,len,cls) on hand-computed spans; adversarial tests assert no
 * crash, a sane classification, and that every span stays in bounds.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"

#include <stdio.h>
#include <string.h>

/* ----------------------------------------------------------------------- */
/* Tiny assertion harness (mirrors test_chat_highlight.c).                   */
/* ----------------------------------------------------------------------- */

static int g_fail = 0;    /* total failed checks */
static int g_checks = 0;  /* total checks run    */

#define CHECK(cond)                                                         \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(cond)) {                                                      \
            ++g_fail;                                                       \
            printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                   \
    } while (0)

typedef void (*hl_test_fn)(void);

static void run(const char *name, hl_test_fn fn)
{
    int before = g_fail;
    fn();
    printf("%s %s\n", (g_fail == before) ? "PASS" : "FAIL", name);
}

/* Assert one token equals the expected (off,len,cls). */
#define CHECK_TOK(t, o, l, k)                                               \
    do {                                                                    \
        CHECK((t).off == (o));                                             \
        CHECK((t).len == (l));                                             \
        CHECK((t).cls == (k));                                             \
    } while (0)

/* Structural validity every token stream must satisfy: in-bounds, positive
 * length, source-ordered / non-overlapping, and a real (non-default) class. */
static int hl_valid(int len, const TimuiHlTok *t, int n, int max)
{
    int k, prev_end = 0, ok = 1;
    if (n < 0 || n > max) return 0;
    for (k = 0; k < n; k++) {
        if (t[k].off < 0 || t[k].len <= 0) ok = 0;
        if (t[k].off + t[k].len > len) ok = 0;
        if (t[k].off < prev_end) ok = 0;
        if (t[k].cls < TIMUI_HL_KEYWORD || t[k].cls > TIMUI_HL_PUNCT) ok = 0;
        prev_end = t[k].off + t[k].len;
    }
    return ok;
}

/* Does any token carry the given class? (for negative "must NOT contain"). */
static int hl_has_class(const TimuiHlTok *t, int n, TimuiHlClass c)
{
    int k;
    for (k = 0; k < n; k++) if (t[k].cls == c) return 1;
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Positive highlighter tests — exact tokens, hand-computed offsets.         */
/* ----------------------------------------------------------------------- */

/* C: keyword vs type vs identifier (whole-word), a number, punctuation. */
static void test_c_keyword_type_ident(void)
{
    TimuiHlTok t[16];
    int n;

    /* "return count;" -> KEYWORD, (count text), PUNCT(;) */
    n = timui_highlight("return count;", 13, "c", t, 16);
    CHECK(n == 2);
    CHECK_TOK(t[0], 0, 6, TIMUI_HL_KEYWORD);   /* return */
    CHECK_TOK(t[1], 12, 1, TIMUI_HL_PUNCT);    /* ;      */

    /* "size_t n;" -> TYPE via the *_t heuristic, then PUNCT */
    n = timui_highlight("size_t n;", 9, "c", t, 16);
    CHECK(n == 2);
    CHECK_TOK(t[0], 0, 6, TIMUI_HL_TYPE);      /* size_t */

    /* "int x = 42;" -> TYPE, PUNCT(=), NUMBER, PUNCT(;) */
    n = timui_highlight("int x = 42;", 11, "c", t, 16);
    CHECK(n == 4);
    CHECK_TOK(t[0], 0, 3, TIMUI_HL_TYPE);      /* int */
    CHECK_TOK(t[1], 6, 1, TIMUI_HL_PUNCT);     /* =   */
    CHECK_TOK(t[2], 8, 2, TIMUI_HL_NUMBER);    /* 42  */
    CHECK_TOK(t[3], 10, 1, TIMUI_HL_PUNCT);    /* ;   */

    /* "iffy;" -> "iffy" is NOT the keyword "if" (whole-word); only ';' emits */
    n = timui_highlight("iffy;", 5, "c", t, 16);
    CHECK(n == 1);
    CHECK_TOK(t[0], 4, 1, TIMUI_HL_PUNCT);
    CHECK(!hl_has_class(t, n, TIMUI_HL_KEYWORD));
}

/* C: string, char literal, line + block comments, hex/float numbers, cpp. */
static void test_c_strings_comments_numbers(void)
{
    TimuiHlTok t[16];
    int n;

    /* escaped quote: "a\"b" is one STRING spanning the whole literal */
    n = timui_highlight("\"a\\\"b\"", 6, "c", t, 16);
    CHECK(n == 1);
    CHECK_TOK(t[0], 0, 6, TIMUI_HL_STRING);

    /* a char literal 'x' */
    n = timui_highlight("'x'", 3, "c", t, 16);
    CHECK(n == 1);
    CHECK_TOK(t[0], 0, 3, TIMUI_HL_CHAR);

    /* line comment then block comment */
    n = timui_highlight("a // c\n/* b */", 14, "c", t, 16);
    CHECK(n == 2);
    CHECK_TOK(t[0], 2, 4, TIMUI_HL_COMMENT);   /* // c            */
    CHECK_TOK(t[1], 7, 7, TIMUI_HL_COMMENT);   /* 7-byte block    */

    /* a hex integer and a float with a suffix */
    n = timui_highlight("0xFF 3.14f", 10, "c", t, 16);
    CHECK(n == 2);
    CHECK_TOK(t[0], 0, 4, TIMUI_HL_NUMBER);    /* 0xFF  */
    CHECK_TOK(t[1], 5, 5, TIMUI_HL_NUMBER);    /* 3.14f */

    /* a whole-line preprocessor directive */
    n = timui_highlight("#include <stdio.h>", 18, "c", t, 16);
    CHECK(n == 1);
    CHECK_TOK(t[0], 0, 18, TIMUI_HL_PREPROC);
}

/* sh: if/fi keywords, a ';' punct, a trailing '#' comment, and $VAR (TYPE). */
static void test_sh_keywords_var_comment(void)
{
    TimuiHlTok t[16];
    int n;

    n = timui_highlight("if x; fi # done", 15, "sh", t, 16);
    CHECK(n == 4);
    CHECK_TOK(t[0], 0, 2, TIMUI_HL_KEYWORD);   /* if     */
    CHECK_TOK(t[1], 4, 1, TIMUI_HL_PUNCT);     /* ;      */
    CHECK_TOK(t[2], 6, 2, TIMUI_HL_KEYWORD);   /* fi     */
    CHECK_TOK(t[3], 9, 6, TIMUI_HL_COMMENT);   /* # done */

    /* $HOME reuses the TYPE colour bucket. */
    n = timui_highlight("echo $HOME", 10, "sh", t, 16);
    CHECK(n == 1);
    CHECK_TOK(t[0], 5, 5, TIMUI_HL_TYPE);      /* $HOME */
}

/* python: a def keyword and a triple-quoted docstring. */
static void test_python_def_triple(void)
{
    TimuiHlTok t[16];
    /* def f():<nl>    """doc"""  (22 bytes) */
    int n = timui_highlight("def f():\n    \"\"\"doc\"\"\"", 22, "python", t, 16);
    CHECK(n == 5);
    CHECK_TOK(t[0], 0, 3, TIMUI_HL_KEYWORD);   /* def       */
    CHECK_TOK(t[1], 5, 1, TIMUI_HL_PUNCT);     /* (         */
    CHECK_TOK(t[2], 6, 1, TIMUI_HL_PUNCT);     /* )         */
    CHECK_TOK(t[3], 7, 1, TIMUI_HL_PUNCT);     /* :         */
    CHECK_TOK(t[4], 13, 9, TIMUI_HL_STRING);   /* """doc""" */
}

/* sql: case-insensitive keywords + type, a number, a '-quoted string, and a
 * -- line comment; a lone '-' is punctuation (not a comment). */
static void test_sql_keywords_string_comment(void)
{
    TimuiHlTok t[32];
    int n;

    /* uppercase keywords match */
    n = timui_highlight("SELECT id FROM t", 16, "sql", t, 32);
    CHECK(n == 2);
    CHECK_TOK(t[0], 0, 6, TIMUI_HL_KEYWORD);   /* SELECT */
    CHECK_TOK(t[1], 10, 4, TIMUI_HL_KEYWORD);  /* FROM   */

    /* lowercase keywords match too; the 1 is a number */
    n = timui_highlight("select 1 from t", 15, "sql", t, 32);
    CHECK(n == 3);
    CHECK_TOK(t[0], 0, 6, TIMUI_HL_KEYWORD);   /* select */
    CHECK_TOK(t[1], 7, 1, TIMUI_HL_NUMBER);    /* 1      */
    CHECK_TOK(t[2], 9, 4, TIMUI_HL_KEYWORD);   /* from   */

    /* a type name (INTEGER), case-insensitive */
    n = timui_highlight("x integer", 9, "sql", t, 32);
    CHECK(n == 1);
    CHECK_TOK(t[0], 2, 7, TIMUI_HL_TYPE);      /* integer */

    /* a '-quoted string then a -- comment to EOL */
    n = timui_highlight("'hi' -- note", 12, "sql", t, 32);
    CHECK(n == 2);
    CHECK_TOK(t[0], 0, 4, TIMUI_HL_STRING);    /* 'hi'    */
    CHECK_TOK(t[1], 5, 7, TIMUI_HL_COMMENT);   /* -- note */

    /* a lone '-' is punctuation, not a comment */
    n = timui_highlight("a - b", 5, "sql", t, 32);
    CHECK(n == 1);
    CHECK_TOK(t[0], 2, 1, TIMUI_HL_PUNCT);
    CHECK(!hl_has_class(t, n, TIMUI_HL_COMMENT));
}

/* Generic mode (NULL/""/unknown): strings, both comment styles, numbers, and
 * NO keywords (an "if" is just text here). */
static void test_generic_fallback(void)
{
    TimuiHlTok t[16];
    int n = timui_highlight("if 7 # c", 8, NULL, t, 16);
    CHECK(n == 2);
    CHECK_TOK(t[0], 3, 1, TIMUI_HL_NUMBER);    /* 7   */
    CHECK_TOK(t[1], 5, 3, TIMUI_HL_COMMENT);   /* # c */
    CHECK(!hl_has_class(t, n, TIMUI_HL_KEYWORD));

    /* an unknown language name also falls back to generic */
    n = timui_highlight("if 7 # c", 8, "brainfuck", t, 16);
    CHECK(n == 2);
    CHECK(!hl_has_class(t, n, TIMUI_HL_KEYWORD));
}

/* ----------------------------------------------------------------------- */
/* Negative / adversarial highlighter tests.                                 */
/* ----------------------------------------------------------------------- */

/* Empty / degenerate arguments yield zero tokens, never a crash. */
static void test_adv_empty_and_null(void)
{
    TimuiHlTok t[8];
    CHECK(timui_highlight("", 0, "c", t, 8) == 0);
    CHECK(timui_highlight("abc", 0, "c", t, 8) == 0);     /* len == 0 */
    CHECK(timui_highlight(NULL, 5, "c", t, 8) == 0);      /* NULL src */
    CHECK(timui_highlight("abc", 3, "c", NULL, 8) == 0);  /* NULL out */
    CHECK(timui_highlight("abc", 3, "c", t, 0) == 0);     /* max == 0 */
}

/* An unterminated string runs to end-of-input, once, without overrun. */
static void test_adv_unterminated_string(void)
{
    TimuiHlTok t[8];
    int n = timui_highlight("\"abc", 4, "c", t, 8);   /* "abc — no closer */
    CHECK(n == 1);
    CHECK_TOK(t[0], 0, 4, TIMUI_HL_STRING);
    CHECK(hl_valid(4, t, n, 8));
}

/* An unterminated block comment runs to end-of-input, once, without overrun. */
static void test_adv_unterminated_block(void)
{
    TimuiHlTok t[8];
    int n = timui_highlight("/* abc", 6, "c", t, 8);
    CHECK(n == 1);
    CHECK_TOK(t[0], 0, 6, TIMUI_HL_COMMENT);
    CHECK(hl_valid(6, t, n, 8));
}

/* Non-ASCII bytes never crash or overrun; inside a string they are content. */
static void test_adv_non_ascii(void)
{
    TimuiHlTok t[16];
    /* "é€" (é = C3 A9, € = E2 82 AC) — one quoted STRING */
    int n = timui_highlight("\"\xC3\xA9\xE2\x82\xAC\"", 7, "c", t, 16);
    CHECK(n == 1);
    CHECK_TOK(t[0], 0, 7, TIMUI_HL_STRING);
    CHECK(hl_valid(7, t, n, 16));
}

/* More tokens than `max`: stop at max, never write past it. */
static void test_adv_token_cap(void)
{
    TimuiHlTok t[8];
    int n, k;
    t[3].off = t[3].len = -424242;       /* canary just past the limit */
    t[3].cls = TIMUI_HL_TEXT;
    n = timui_highlight(";;;;;;;;;;", 10, "c", t, 3);   /* 10 puncts, max 3 */
    CHECK(n == 3);
    CHECK(hl_valid(10, t, n, 3));
    for (k = 0; k < n; k++) CHECK(t[k].cls == TIMUI_HL_PUNCT);
    /* the slot at index == max must be untouched */
    CHECK(t[3].off == -424242 && t[3].len == -424242 && t[3].cls == TIMUI_HL_TEXT);
}

/* ----------------------------------------------------------------------- */
/* Colour helper.                                                            */
/* ----------------------------------------------------------------------- */

/* Every non-default class maps to a distinct, non-default colour; TEXT is the
 * default code foreground. String and char share a bucket (both green). */
static void test_hl_color_mapping(void)
{
    uint32_t kw = timui_hl_color(TIMUI_HL_KEYWORD);
    uint32_t ty = timui_hl_color(TIMUI_HL_TYPE);
    uint32_t st = timui_hl_color(TIMUI_HL_STRING);
    uint32_t ch = timui_hl_color(TIMUI_HL_CHAR);
    uint32_t co = timui_hl_color(TIMUI_HL_COMMENT);
    uint32_t nu = timui_hl_color(TIMUI_HL_NUMBER);
    uint32_t pp = timui_hl_color(TIMUI_HL_PREPROC);
    uint32_t pu = timui_hl_color(TIMUI_HL_PUNCT);
    uint32_t tx = timui_hl_color(TIMUI_HL_TEXT);

    CHECK(st == ch);                 /* string and char share the green bucket */
    CHECK(kw != ty && kw != co && kw != nu && kw != pp && kw != pu);
    CHECK(ty != co && ty != nu && ty != pp && ty != pu);
    CHECK(co != nu && co != pp && co != pu);
    CHECK(kw != tx && ty != tx && co != tx);  /* real classes != plain text */
    /* an out-of-range class falls back to the default (no crash) */
    CHECK(timui_hl_color((TimuiHlClass)999) == tx);
}

/* ----------------------------------------------------------------------- */
/* Code-view scroll clamp — pure, hand-computable.                           */
/* ----------------------------------------------------------------------- */

static void test_scroll_clamp(void)
{
    /* content taller than the viewport: valid range [0, nlines - visible] */
    CHECK(timui_code_scroll_clamp(5, 10, 3) == 5);    /* in range        */
    CHECK(timui_code_scroll_clamp(-4, 10, 3) == 0);   /* clamp at top    */
    CHECK(timui_code_scroll_clamp(100, 10, 3) == 7);  /* clamp at bottom */
    CHECK(timui_code_scroll_clamp(7, 10, 3) == 7);    /* exact bottom    */

    /* content shorter than the viewport: only 0 is valid */
    CHECK(timui_code_scroll_clamp(2, 3, 10) == 0);
    CHECK(timui_code_scroll_clamp(0, 3, 10) == 0);

    /* degenerate: no lines / no viewport clamp to 0, never negative */
    CHECK(timui_code_scroll_clamp(5, 0, 5) == 0);
    CHECK(timui_code_scroll_clamp(5, 4, 0) == 4);     /* visible 0 => max=nlines */
    CHECK(timui_code_scroll_clamp(-1, 0, 0) == 0);
}

/* ----------------------------------------------------------------------- */
/* Code viewer render smoke — drives the production path with a fake TTY.    */
/* ----------------------------------------------------------------------- */

/* Find the x of the first cell in row `y` whose codepoint == cp, else -1. */
static int find_cp_in_row(TimuiCellBuffer *buf, int y, uint32_t cp, int w)
{
    int x;
    for (x = 0; x < w; x++)
        if (timui_cells_get(buf, x, y)->codepoint == cp) return x;
    return -1;
}

static void test_code_render_smoke(void)
{
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport tr;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    const char *src = "int x = 42;\nreturn x;";
    int scroll = 100;   /* deliberately out of range — timui_code must clamp it */
    int ix;

    timui_fake_init(&fake, &al);
    tr = timui_fake_transport(&fake);
    timui_open_for_test(&ui, tr, 40, 10, &al);
    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);

    timui_code(f, TIMUI_RECT(0, 0, 40, 10), src, (int)strlen(src), "c", &scroll);

    /* out-of-range scroll clamped in place: 2 lines, 10 rows => top = 0 */
    CHECK(scroll == 0);

    /* the subtle code background filled an empty area of the rect */
    CHECK(timui_cells_get(buf, 38, 9)->bg == 0x1B1E2Bu);

    /* the keyword-coloured source landed on the rows (gutter-independent scan):
     * 'i' of "int" on row 0 carries the TYPE colour, "return" the KEYWORD one */
    ix = find_cp_in_row(buf, 0, 'i', 40);
    CHECK(ix >= 0);
    if (ix >= 0) CHECK(timui_cells_get(buf, ix, 0)->fg == timui_hl_color(TIMUI_HL_TYPE));
    ix = find_cp_in_row(buf, 1, 'r', 40);
    CHECK(ix >= 0);
    if (ix >= 0) CHECK(timui_cells_get(buf, ix, 1)->fg == timui_hl_color(TIMUI_HL_KEYWORD));

    timui_end(f);
    timui_close(ui);

    /* a NULL frame / NULL src must be a no-op, not a crash */
    timui_code(NULL, TIMUI_RECT(0, 0, 10, 10), src, 4, "c", &scroll);
    timui_fake_destroy(&fake);
}

int main(void)
{
    run("c_keyword_type_ident",         test_c_keyword_type_ident);
    run("c_strings_comments_numbers",   test_c_strings_comments_numbers);
    run("sh_keywords_var_comment",      test_sh_keywords_var_comment);
    run("python_def_triple",            test_python_def_triple);
    run("sql_keywords_string_comment",  test_sql_keywords_string_comment);
    run("generic_fallback",             test_generic_fallback);
    run("adv_empty_and_null",           test_adv_empty_and_null);
    run("adv_unterminated_string",      test_adv_unterminated_string);
    run("adv_unterminated_block",       test_adv_unterminated_block);
    run("adv_non_ascii",                test_adv_non_ascii);
    run("adv_token_cap",                test_adv_token_cap);
    run("hl_color_mapping",             test_hl_color_mapping);
    run("scroll_clamp",                 test_scroll_clamp);
    run("code_render_smoke",            test_code_render_smoke);

    if (g_fail != 0) {
        printf("\n%d/%d checks FAILED\n", g_fail, g_checks);
        return 1;
    }
    printf("\nall %d checks passed\n", g_checks);
    return 0;
}
