/*
 * test_chat_highlight.c — standalone unit tests for examples/chat_highlight.h.
 *
 * Deliberately self-contained: it does NOT include tests/test.h or the timui
 * library. Its own main() runs each test; a failing assertion prints FAIL and
 * makes the process exit non-zero. Positive tests assert exact (off,len,cls);
 * adversarial tests assert no crash, a sane classification, and that the token
 * count and every span stay in bounds (never exceeding `max`, never running
 * past the input).
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */
#include "chat_highlight.h"

#include <stdio.h>
#include <string.h>

/* ----------------------------------------------------------------------- */
/* Tiny assertion harness.                                                   */
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
static int hl_valid(int len, const HlTok *t, int n, int max)
{
    int k, prev_end = 0, ok = 1;
    if (n < 0 || n > max) return 0;
    for (k = 0; k < n; k++) {
        if (t[k].off < 0 || t[k].len <= 0) ok = 0;
        if (t[k].off + t[k].len > len) ok = 0;
        if (t[k].off < prev_end) ok = 0;
        if (t[k].cls < HL_KEYWORD || t[k].cls > HL_PUNCT) ok = 0;
        prev_end = t[k].off + t[k].len;
    }
    return ok;
}

/* Does any token cover the given class? (for negative "must NOT contain"). */
static int hl_has_class(const HlTok *t, int n, HlClass c)
{
    int k;
    for (k = 0; k < n; k++) if (t[k].cls == c) return 1;
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Positive tests — exact tokens.                                            */
/* ----------------------------------------------------------------------- */

/* C: keyword vs type vs identifier; an identifier containing a keyword must
 * NOT match it (whole-word boundaries). */
static void test_c_keyword_type_ident(void)
{
    HlTok t[16];
    int n;

    /* "return count;" -> KEYWORD, (count = text/not emitted), PUNCT */
    n = chat_highlight("return count;", 13, "c", t, 16);
    CHECK(n == 2);
    CHECK_TOK(t[0], 0, 6, HL_KEYWORD);   /* return */
    CHECK_TOK(t[1], 12, 1, HL_PUNCT);    /* ;      */

    /* "size_t n;" -> TYPE via the *_t heuristic, then PUNCT */
    n = chat_highlight("size_t n;", 9, "c", t, 16);
    CHECK(n == 2);
    CHECK_TOK(t[0], 0, 6, HL_TYPE);      /* size_t */
    CHECK_TOK(t[1], 8, 1, HL_PUNCT);     /* ;      */

    /* "int x = 42;" -> TYPE, PUNCT(=), NUMBER, PUNCT(;) */
    n = chat_highlight("int x = 42;", 11, "c", t, 16);
    CHECK(n == 4);
    CHECK_TOK(t[0], 0, 3, HL_TYPE);      /* int */
    CHECK_TOK(t[1], 6, 1, HL_PUNCT);     /* =   */
    CHECK_TOK(t[2], 8, 2, HL_NUMBER);    /* 42  */
    CHECK_TOK(t[3], 10, 1, HL_PUNCT);    /* ;   */

    /* "iffy;" -> "iffy" is NOT the keyword "if"; only the ';' is a token */
    n = chat_highlight("iffy;", 5, "c", t, 16);
    CHECK(n == 1);
    CHECK_TOK(t[0], 4, 1, HL_PUNCT);
    CHECK(!hl_has_class(t, n, HL_KEYWORD));
}

/* C: a string containing an escaped quote is one string spanning the whole
 * literal. */
static void test_c_string_escaped_quote(void)
{
    HlTok t[8];
    int n = chat_highlight("\"a\\\"b\"", 6, "c", t, 8);  /* "a\"b" */
    CHECK(n == 1);
    CHECK_TOK(t[0], 0, 6, HL_STRING);
}

/* C: line comment then block comment. */
static void test_c_comments(void)
{
    HlTok t[8];
    /* input: a<sp>// c<nl> then a block comment  (14 bytes) */
    int n = chat_highlight("a // c\n/* b */", 14, "c", t, 8);
    CHECK(n == 2);
    CHECK_TOK(t[0], 2, 4, HL_COMMENT);   /* the // c line comment  */
    CHECK_TOK(t[1], 7, 7, HL_COMMENT);   /* the 7-byte block comment */
}

/* C: a hex integer and a float with a suffix. */
static void test_c_numbers(void)
{
    HlTok t[8];
    int n = chat_highlight("0xFF 3.14f", 10, "c", t, 8);
    CHECK(n == 2);
    CHECK_TOK(t[0], 0, 4, HL_NUMBER);    /* 0xFF  */
    CHECK_TOK(t[1], 5, 5, HL_NUMBER);    /* 3.14f */
}

/* C: a whole-line preprocessor directive. */
static void test_c_preproc(void)
{
    HlTok t[8];
    int n = chat_highlight("#include <stdio.h>", 18, "c", t, 8);
    CHECK(n == 1);
    CHECK_TOK(t[0], 0, 18, HL_PREPROC);
}

/* sh: keywords if/fi around a bare word, a ';' and a trailing '#' comment. */
static void test_sh_if_fi_comment(void)
{
    HlTok t[16];
    int n = chat_highlight("if x; fi # done", 15, "sh", t, 16);
    CHECK(n == 4);
    CHECK_TOK(t[0], 0, 2, HL_KEYWORD);   /* if     */
    CHECK_TOK(t[1], 4, 1, HL_PUNCT);     /* ;      */
    CHECK_TOK(t[2], 6, 2, HL_KEYWORD);   /* fi     */
    CHECK_TOK(t[3], 9, 6, HL_COMMENT);   /* # done */
}

/* sh: $VAR expansion (reuses the HL_TYPE colour bucket). */
static void test_sh_variable(void)
{
    HlTok t[8];
    int n = chat_highlight("echo $HOME", 10, "sh", t, 8);
    CHECK(n == 1);
    CHECK_TOK(t[0], 5, 5, HL_TYPE);      /* $HOME */
}

/* python: a def keyword and a triple-quoted docstring. */
static void test_python_def_triple(void)
{
    HlTok t[16];
    /* def f():<nl>    """doc"""  (22 bytes) */
    int n = chat_highlight("def f():\n    \"\"\"doc\"\"\"", 22, "python", t, 16);
    CHECK(n == 5);
    CHECK_TOK(t[0], 0, 3, HL_KEYWORD);   /* def       */
    CHECK_TOK(t[1], 5, 1, HL_PUNCT);     /* (         */
    CHECK_TOK(t[2], 6, 1, HL_PUNCT);     /* )         */
    CHECK_TOK(t[3], 7, 1, HL_PUNCT);     /* :         */
    CHECK_TOK(t[4], 13, 9, HL_STRING);   /* """doc""" */
}

/* ----------------------------------------------------------------------- */
/* Negative / adversarial tests — no crash, sane class, bounded.             */
/* ----------------------------------------------------------------------- */

/* An unterminated string runs to end-of-input, once, without overrun. */
static void test_adv_unterminated_string(void)
{
    HlTok t[8];
    int n = chat_highlight("\"abc", 4, "c", t, 8);   /* "abc  (no closer) */
    CHECK(n == 1);
    CHECK_TOK(t[0], 0, 4, HL_STRING);
    CHECK(hl_valid(4, t, n, 8));
}

/* An unterminated block comment runs to end-of-input. */
static void test_adv_unterminated_block(void)
{
    HlTok t[8];
    int n = chat_highlight("/* abc", 6, "c", t, 8);
    CHECK(n == 1);
    CHECK_TOK(t[0], 0, 6, HL_COMMENT);
    CHECK(hl_valid(6, t, n, 8));
}

/* A lone backtick is classified as punctuation, not left dangling. */
static void test_adv_lone_backtick(void)
{
    HlTok t[8];
    int n = chat_highlight("`", 1, "c", t, 8);
    CHECK(n == 1);
    CHECK_TOK(t[0], 0, 1, HL_PUNCT);
    CHECK(hl_valid(1, t, n, 8));
}

/* '//' inside a string is part of the string, not a comment. */
static void test_adv_slashes_in_string(void)
{
    HlTok t[8];
    int n = chat_highlight("\"a//b\"", 6, "c", t, 8);
    CHECK(n == 1);
    CHECK_TOK(t[0], 0, 6, HL_STRING);
    CHECK(!hl_has_class(t, n, HL_COMMENT));
}

/* '"' inside a line comment is part of the comment, not a string. */
static void test_adv_quote_in_comment(void)
{
    HlTok t[8];
    int n = chat_highlight("// \"x", 5, "c", t, 8);
    CHECK(n == 1);
    CHECK_TOK(t[0], 0, 5, HL_COMMENT);
    CHECK(!hl_has_class(t, n, HL_STRING));
}

/* Non-ASCII bytes: inside a string they are ordinary content; outside they
 * are text (skipped). Neither crashes nor overruns. */
static void test_adv_non_ascii(void)
{
    HlTok t[16];
    int n;

    /* "é€" (é = C3 A9, € = E2 82 AC), the whole quoted run is one string. */
    n = chat_highlight("\"\xC3\xA9\xE2\x82\xAC\"", 7, "c", t, 16);
    CHECK(n == 1);
    CHECK_TOK(t[0], 0, 7, HL_STRING);
    CHECK(hl_valid(7, t, n, 16));

    /* é + 1  (bytes C3 A9 ' ' '+' ' ' '1'): non-ASCII bytes are text. */
    n = chat_highlight("\xC3\xA9 + 1", 6, "c", t, 16);
    CHECK(n == 2);
    CHECK_TOK(t[0], 3, 1, HL_PUNCT);     /* + */
    CHECK_TOK(t[1], 5, 1, HL_NUMBER);    /* 1 */
    CHECK(hl_valid(6, t, n, 16));
}

/* Empty input and len==0 both yield zero tokens. */
static void test_adv_empty_and_zero_len(void)
{
    HlTok t[8];
    CHECK(chat_highlight("", 0, "c", t, 8) == 0);
    CHECK(chat_highlight("abc", 0, "c", t, 8) == 0);   /* len == 0 */
    CHECK(chat_highlight(NULL, 5, "c", t, 8) == 0);    /* NULL code */
    CHECK(chat_highlight("abc", 3, "c", NULL, 8) == 0);/* NULL out  */
    CHECK(chat_highlight("abc", 3, "c", t, 0) == 0);   /* max == 0  */
}

/* More tokens than `max`: stop at max, never write past it. */
static void test_adv_max_bound(void)
{
    HlTok t[8];
    int n, k;
    /* 10 semicolons = 10 punctuation tokens, but max = 3. */
    t[3].off = t[3].len = -424242;       /* canary just past the limit */
    t[3].cls = HL_TEXT;
    n = chat_highlight(";;;;;;;;;;", 10, "c", t, 3);
    CHECK(n == 3);                       /* returned count capped at max */
    CHECK(hl_valid(10, t, n, 3));
    for (k = 0; k < n; k++) CHECK(t[k].cls == HL_PUNCT);
    /* the slot at index == max must be untouched */
    CHECK(t[3].off == -424242 && t[3].len == -424242 && t[3].cls == HL_TEXT);
}

/* Generic mode (no language): strings, both comment styles, numbers, and NO
 * keywords (an "if" is just text here). */
static void test_generic_no_keywords(void)
{
    HlTok t[16];
    int n = chat_highlight("if 7 # c", 8, NULL, t, 16);
    CHECK(n == 2);
    CHECK_TOK(t[0], 3, 1, HL_NUMBER);    /* 7   */
    CHECK_TOK(t[1], 5, 3, HL_COMMENT);   /* # c */
    CHECK(!hl_has_class(t, n, HL_KEYWORD));
}

int main(void)
{
    run("c_keyword_type_ident",   test_c_keyword_type_ident);
    run("c_string_escaped_quote", test_c_string_escaped_quote);
    run("c_comments",             test_c_comments);
    run("c_numbers",              test_c_numbers);
    run("c_preproc",              test_c_preproc);
    run("sh_if_fi_comment",       test_sh_if_fi_comment);
    run("sh_variable",            test_sh_variable);
    run("python_def_triple",      test_python_def_triple);
    run("adv_unterminated_string", test_adv_unterminated_string);
    run("adv_unterminated_block", test_adv_unterminated_block);
    run("adv_lone_backtick",      test_adv_lone_backtick);
    run("adv_slashes_in_string",  test_adv_slashes_in_string);
    run("adv_quote_in_comment",   test_adv_quote_in_comment);
    run("adv_non_ascii",          test_adv_non_ascii);
    run("adv_empty_and_zero_len", test_adv_empty_and_zero_len);
    run("adv_max_bound",          test_adv_max_bound);
    run("generic_no_keywords",    test_generic_no_keywords);

    if (g_fail != 0) {
        printf("\n%d/%d checks FAILED\n", g_fail, g_checks);
        return 1;
    }
    printf("\nall %d checks passed\n", g_checks);
    return 0;
}
