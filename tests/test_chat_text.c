/* test_chat_text.c — standalone unit tests for examples/chat_text.h: the display-
 * width-aware word-wrap engine (wrap_rich) plus bidi/base-direction sanity checks.
 * Builds timui as a single TU for timui_utf8_* and drives the pure helpers.
 *
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0 */
#define TIMUI_IMPLEMENTATION
#include "timui.h"
#include "chat_text.h"
#include <stdio.h>

static int failures;
#define CHECK(cond) do { if(!(cond)){ \
    printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while(0)

/* wrap and assert the line count + each line-start offset. */
static int wrap_eq(const char *s, int width, const int *want, int nwant){
    int got[64], n = wrap_rich(s, width, got, 64), i, ok = (n == nwant);
    for(i = 0; ok && i < nwant; i++) ok = (got[i] == want[i]);
    if(!ok){
        printf("  FAIL wrap \"%s\" @%d -> %d lines [", s, width, n);
        for(i = 0; i < n; i++) printf("%s%d", i ? "," : "", got[i]);
        printf("], wanted %d [", nwant);
        for(i = 0; i < nwant; i++) printf("%s%d", i ? "," : "", want[i]);
        printf("]\n"); failures++;
    }
    return ok;
}

int main(void){
    /* ---- positive: word wrapping ---- */
    { int w[] = {0, 6, 12}; wrap_eq("hello world foo", 8, w, 3); }   /* break at spaces */
    { int w[] = {0};        wrap_eq("abcdefgh", 8, w, 1); }          /* exact fit = 1 line */
    { int w[] = {0};        wrap_eq("short", 40, w, 1); }            /* fits = 1 line */
    { int w[] = {0};        wrap_eq("", 10, w, 1); }                 /* empty = 1 line */

    /* ---- display width: CJK/emoji = 2, markers = 0, shortcode = 2 ---- */
    { int w[] = {0, 6};  wrap_eq("\xE4\xB8\xAD\xE6\x96\x87""ab", 4, w, 2); }  /* 中文=4, then ab */
    { int w[] = {0, 5};  wrap_eq("\xF0\x9F\x91\x8B hi", 3, w, 2); }           /* 👋(2)+space=3, hi */
    { int w[] = {0, 7};  wrap_eq("*bold* x", 5, w, 2); }                       /* markers 0-width */
    { int w[] = {0, 7};  wrap_eq(":wave: hi", 3, w, 2); }                      /* :wave:(2)+sp=3 */

    /* URL counts its byte length (ASCII) as one unit: "http://x.io" is 11 wide,
     * so "see " wraps before it, then the URL line, then "end". */
    { int w[] = {0, 4, 16}; wrap_eq("see http://x.io end", 12, w, 3); }

    /* ---- negative / adversarial ---- */
    { int w[] = {0, 3, 6, 9}; wrap_eq("abcdefghij", 3, w, 4); }  /* over-long word hard-breaks */
    /* width <= 0 is clamped to 1 (no crash / no infinite loop) */
    { int got[8]; int n = wrap_rich("abc", 0, got, 8); CHECK(n >= 1); CHECK(got[0] == 0); }
    /* maxlines cap: count is real but starts[] is only written up to maxlines */
    { int got[2] = {-1,-1}; int n = wrap_rich("a b c d e f", 1, got, 2);
      CHECK(n > 2); CHECK(got[0] == 0); CHECK(got[1] != -1); }
    /* a single space, a single glyph, and a leading space don't misbehave */
    { int got[8]; CHECK(wrap_rich(" ", 4, got, 8) == 1); CHECK(wrap_rich("x", 4, got, 8) == 1); }

    /* ---- bidi / base direction sanity (regression for the moved helpers) ---- */
    CHECK(first_strong_rtl("avi: hi") == 0);                        /* Latin first */
    CHECK(first_strong_rtl("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D") == 1);/* שלום -> RTL */
    { char out[64]; bidi_visual("hi", out, sizeof out, 0); CHECK(strcmp(out, "hi") == 0); }

    /* ---- bidi visual order: full UAX #9 (SheenBidi) vs the cheap approximation --
     * All three vectors are HAND-COMPUTED (see tools/vendor/SheenBidi + UAX #9):
     *   Hebrew "שלום" (all-R) reverses to "םולש";
     *   Arabic "بب" shapes (pre-bidi ARJOIN: initial ﺑ=U+FE91, final ﺐ=U+FE90)
     *     then reverses to <FE90 FE91> — proving shaping is a pre-bidi step;
     *   MIXED base-RTL "ד 12 ab" is where correct UAX #9 nesting DIVERGES from the
     *     approximation: the space between the European-number run "12" and the
     *     Latin run "ab" resolves to the RTL base direction (N-rules), so "ab" and
     *     "12" are SEPARATE level-2 runs and swap under the outer reversal ->
     *     "ab 12 ד"; the cheap approximation glues "12 ab" as one LTR run -> "12 ab ד". */
    { const char *heb_log = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";      /* שלום */
      const char *ara_log = "\xD8\xA8\xD8\xA8";                     /* بب   */
      const char *mix_log = "\xD7\x93 12 ab";                       /* ד 12 ab */
#ifdef CHAT_SHEENBIDI
      char o[64];
      bidi_visual(heb_log, o, sizeof o, 1); CHECK(strcmp(o, "\xD7\x9D\xD7\x95\xD7\x9C\xD7\xA9") == 0);      /* םולש */
      bidi_visual(ara_log, o, sizeof o, 1); CHECK(strcmp(o, "\xEF\xBA\x90\xEF\xBA\x91") == 0);              /* FE90 FE91 */
      bidi_visual(mix_log, o, sizeof o, 1); CHECK(strcmp(o, "ab 12 \xD7\x93") == 0);                        /* UAX #9 nesting */
#else
      /* DEFAULT approximation build stays byte-for-byte what it always was. */
      char o[64];
      bidi_visual(heb_log, o, sizeof o, 1); CHECK(strcmp(o, "\xD7\x9D\xD7\x95\xD7\x9C\xD7\xA9") == 0);      /* pure reverse */
      bidi_visual(mix_log, o, sizeof o, 1); CHECK(strcmp(o, "12 ab \xD7\x93") == 0);                        /* space-glued */
      (void)ara_log;
#endif
    }

    /* ---- fenced code block layout (msg_visual_rows) ---- */
    { MsgRow r[16]; int n = msg_visual_rows("hi\n```c\nint x;\n```\nbye", 40, r, 16);
      CHECK(n == 5);
      CHECK(r[0].kind == RK_TEXT);
      CHECK(r[1].kind == RK_FENCE);
      CHECK(r[2].kind == RK_CODE);  CHECK(strcmp(r[2].lang, "c") == 0);
      CHECK(r[3].kind == RK_FENCE);
      CHECK(r[4].kind == RK_TEXT); }
    /* plain multi-line text = one row per \n line (no code) */
    { MsgRow r[16]; int n = msg_visual_rows("a\nb\nc", 40, r, 16);
      CHECK(n == 3); CHECK(r[0].kind == RK_TEXT && r[2].kind == RK_TEXT); }
    /* a long text line wraps into several rows */
    { MsgRow r[16]; int n = msg_visual_rows("one two three four five", 9, r, 16);
      CHECK(n >= 2); CHECK(r[0].kind == RK_TEXT); }
    /* adversarial: unterminated fence — the rest stays code, no overrun */
    { MsgRow r[16]; int n = msg_visual_rows("x\n```py\nprint(1)", 40, r, 16);
      CHECK(n == 3); CHECK(r[2].kind == RK_CODE); CHECK(strcmp(r[2].lang, "py") == 0); }
    /* adversarial: bare fence (no language), empty message, cap respected */
    { MsgRow r[16]; int n = msg_visual_rows("```\ncode\n```", 40, r, 16);
      CHECK(n == 3); CHECK(r[1].kind == RK_CODE && r[1].lang[0] == '\0'); }
    { MsgRow r[4]; int n = msg_visual_rows("", 40, r, 4); CHECK(n == 1); }
    { MsgRow r[2]; int n = msg_visual_rows("a\nb\nc\nd\ne", 40, r, 2); CHECK(n == 5); } /* count > cap */

    if(failures){ printf("chat_text: %d FAILED\n", failures); return 1; }
    printf("chat_text: all wrap + bidi tests passed\n");
    return 0;
}
