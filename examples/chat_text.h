/* chat_text.h — pure text layout helpers for the timui.h chat example: emoji
 * shortcodes, a small bidi (Hebrew/Arabic), display-width metrics, and word-wrap.
 * Header-only + dependency-light (timui.h for utf8 width/decode) so it is unit-
 * tested directly by tests/test_chat_text.c and shared by examples/chat.c.
 *
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef CHAT_TEXT_H
#define CHAT_TEXT_H

/* Requires timui.h (timui_utf8_width / timui_utf8_decode / uint32_t) to be
 * included FIRST. We must NOT include it here: the chat and its test build timui
 * as a single TU with TIMUI_IMPLEMENTATION, so a second timui.h include would
 * re-emit the implementation and redefine everything. */
#ifndef TIMUI_H
#error "include timui.h before chat_text.h"
#endif
#include <string.h>
#include <stdint.h>

/* Emoji shortcodes (:name:) — expanded at render time so `:eyes:` shows as 👀.
 * Every emoji here is in the bundled Twemoji atlas so it renders in a recording
 * without --system-emoji too. */
static const struct { const char *code; const char *emoji; } SHORTCODES[] = {
    {":eyes:", "\xF0\x9F\x91\x80"},        {":wave:", "\xF0\x9F\x91\x8B"},
    {":thumbsup:", "\xF0\x9F\x91\x8D"},    {":thumbs-up:", "\xF0\x9F\x91\x8D"},
    {":+1:", "\xF0\x9F\x91\x8D"},          {":tada:", "\xF0\x9F\x8E\x89"},
    {":fire:", "\xF0\x9F\x94\xA5"},        {":rocket:", "\xF0\x9F\x9A\x80"},
    {":heart:", "\xE2\x9D\xA4"},           {":sparkles:", "\xE2\x9C\xA8"},
    {":bulb:", "\xF0\x9F\x92\xA1"},        {":clap:", "\xF0\x9F\x91\x8F"},
    {":brain:", "\xF0\x9F\xA7\xA0"},       {":zap:", "\xE2\x9A\xA1"},
    {":star:", "\xE2\xAD\x90"},            {":joy:", "\xF0\x9F\xA4\xA3"},
    {":raised_hands:", "\xF0\x9F\x99\x8C"},{":bow:", "\xF0\x9F\x99\x87"},
    {":party:", "\xF0\x9F\xA5\xB3"},       {":bug:", "\xF0\x9F\x90\x9B"},
    {":globe:", "\xF0\x9F\x8C\x8D"},       {":boom:", "\xF0\x9F\x92\xA5"},
};
/* If a :shortcode: starts at s, return its emoji + set *clen to the code length. */
static const char *shortcode_at(const char *s, size_t *clen){
    size_t k;
    if(s[0] != ':') return NULL;
    for(k = 0; k < sizeof SHORTCODES / sizeof SHORTCODES[0]; k++){
        size_t cl = strlen(SHORTCODES[k].code);
        if(strncmp(s, SHORTCODES[k].code, cl) == 0){ *clen = cl; return SHORTCODES[k].emoji; }
    }
    return NULL;
}

/* ---- RTL: Hebrew/Arabic (basic bidi + Arabic joining) -------------------- *
 * A terminal renders cells positionally (no reordering), so the app must lay RTL
 * out itself: shape Arabic base letters to their contextual presentation forms,
 * then reverse RTL runs for visual order. Simplified — no full UAX#9, no lam-alef
 * ligature — but enough to show Hebrew/Arabic messages correctly. */
static int is_rtl_cp(unsigned int cp){
    return (cp >= 0x0590 && cp <= 0x05FF) || (cp >= 0x0600 && cp <= 0x06FF) ||
           (cp >= 0x0750 && cp <= 0x077F) || (cp >= 0xFB1D && cp <= 0xFEFF);
}
/* Arabic letters: base -> isolated presentation form (FExx) + dual-joining flag.
 * For dual: final=iso+1, initial=iso+2, medial=iso+3; right-joining has iso+final only. */
static const struct { unsigned int base, iso; int dual; } ARJOIN[] = {
    {0x0621,0xFE80,0},{0x0622,0xFE81,0},{0x0623,0xFE83,0},{0x0624,0xFE85,0},
    {0x0625,0xFE87,0},{0x0626,0xFE89,1},{0x0627,0xFE8D,0},{0x0628,0xFE8F,1},
    {0x0629,0xFE93,0},{0x062A,0xFE95,1},{0x062B,0xFE99,1},{0x062C,0xFE9D,1},
    {0x062D,0xFEA1,1},{0x062E,0xFEA5,1},{0x062F,0xFEA9,0},{0x0630,0xFEAB,0},
    {0x0631,0xFEAD,0},{0x0632,0xFEAF,0},{0x0633,0xFEB1,1},{0x0634,0xFEB5,1},
    {0x0635,0xFEB9,1},{0x0636,0xFEBD,1},{0x0637,0xFEC1,1},{0x0638,0xFEC5,1},
    {0x0639,0xFEC9,1},{0x063A,0xFECD,1},{0x0641,0xFED1,1},{0x0642,0xFED5,1},
    {0x0643,0xFED9,1},{0x0644,0xFEDD,1},{0x0645,0xFEE1,1},{0x0646,0xFEE5,1},
    {0x0647,0xFEE9,1},{0x0648,0xFEED,0},{0x0649,0xFEEF,0},{0x064A,0xFEF1,1},
};
static int arjoin_type(unsigned int cp){   /* 0=not-joining 1=right-joining 2=dual */
    size_t k;
    for(k = 0; k < sizeof ARJOIN / sizeof ARJOIN[0]; k++)
        if(ARJOIN[k].base == cp) return ARJOIN[k].dual ? 2 : 1;
    return 0;
}
static int enc_utf8(unsigned int cp, char *out){
    if(cp < 0x80){ out[0] = (char)cp; return 1; }
    if(cp < 0x800){ out[0] = (char)(0xC0|(cp>>6)); out[1] = (char)(0x80|(cp&0x3F)); return 2; }
    if(cp < 0x10000){ out[0] = (char)(0xE0|(cp>>12)); out[1] = (char)(0x80|((cp>>6)&0x3F));
                      out[2] = (char)(0x80|(cp&0x3F)); return 3; }
    out[0] = (char)(0xF0|(cp>>18)); out[1] = (char)(0x80|((cp>>12)&0x3F));
    out[2] = (char)(0x80|((cp>>6)&0x3F)); out[3] = (char)(0x80|(cp&0x3F)); return 4;
}
#ifndef CHAT_SHEENBIDI                            /* only the cheap reorder needs this */
static int is_ltr_strong(unsigned int cp){        /* strong LTR: Latin letter or digit */
    return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9');
}
#endif
#ifdef CHAT_SHEENBIDI
/* Opt-in (WITH_SHEENBIDI=1): the CORRECT UAX #9 logical->visual reordering via the
 * vendored SheenBidi (tools/vendor/SheenBidi, Apache-2.0). The umbrella header is
 * on the include path only under this flag; the amalgamation is linked separately
 * so chat.c stays a single TU. */
#include <SheenBidi/SheenBidi.h>
/* Reorder `n` (already Arabic-shaped) codepoints cp[] into visual order in vis[],
 * returning the visual count. Runs the full Unicode Bidirectional Algorithm over a
 * UTF-32 view of cp[] (so SBRun offsets/lengths are codepoint indices), then walks
 * SheenBidi's already-visual-ordered runs, emitting each RTL (odd-level) run in
 * reverse (UAX #9 rule L2). `base_rtl` sets the paragraph base level (1=RTL,0=LTR);
 * the caller resolved the base direction via first_strong_rtl (UAX #9 P2/P3). */
static int bidi_reorder_sheenbidi(const unsigned int *cp, int n, int base_rtl, unsigned int *vis){
    SBUInt32 buf[512];
    SBCodepointSequence seq;
    SBAlgorithmRef algo; SBParagraphRef para; SBLineRef line;
    const SBRun *runs; SBUInteger rc, r; int i, vn = 0;
    if(n <= 0) return 0;
    for(i = 0; i < n; i++) buf[i] = (SBUInt32)cp[i];
    seq.stringEncoding = SBStringEncodingUTF32;
    seq.stringBuffer   = buf;
    seq.stringLength   = (SBUInteger)n;
    algo = SBAlgorithmCreate(&seq);
    para = SBAlgorithmCreateParagraph(algo, 0, (SBUInteger)n, base_rtl ? (SBLevel)1 : (SBLevel)0);
    line = SBParagraphCreateLine(para, 0, SBParagraphGetLength(para));
    runs = SBLineGetRunsPtr(line);
    rc   = SBLineGetRunCount(line);
    for(r = 0; r < rc; r++){
        SBUInteger off = runs[r].offset, l = runs[r].length, t;
        if(runs[r].level & 1) for(t = 0; t < l; t++) vis[vn++] = cp[off + l - 1 - t];
        else                  for(t = 0; t < l; t++) vis[vn++] = cp[off + t];
    }
    SBLineRelease(line); SBParagraphRelease(para); SBAlgorithmRelease(algo);
    return vn;
}
#endif /* CHAT_SHEENBIDI */

/* Lay a message body out in visual order for positional cell rendering. Arabic is
 * shaped to contextual presentation forms in LOGICAL order first (a pre-bidi step),
 * then the codepoints are reordered logical->visual:
 *   - DEFAULT (zero-dependency): a cheap 2-level approximation — base_rtl reverses
 *     the whole body (level 1) and un-reverses embedded Latin/number runs (level 2);
 *     base_ltr reverses maximal RTL runs in place. Good enough for chat lines, but
 *     embedded numbers-in-RTL / nesting are NOT true UAX #9.
 *   - WITH_SHEENBIDI=1 (CHAT_SHEENBIDI): the FULL Unicode Bidirectional Algorithm
 *     (UAX #9) via SheenBidi, over the same shaped codepoints. */
static void bidi_visual(const char *in, char *out, size_t cap, int base_rtl){
    unsigned int cp[512], orig[512];
    int n = 0, k, o = 0;
    size_t i = 0, len = strlen(in);
    while(in[i] && n < 512){ uint32_t c; int adv = timui_utf8_decode(in + i, len - i, &c); if(adv <= 0) adv = 1; cp[n++] = c; i += (size_t)adv; }
    memcpy(orig, cp, (size_t)n * sizeof cp[0]);
    for(k = 0; k < n; k++){                          /* Arabic contextual shaping (logical order) */
        size_t j; int found = 0; unsigned int iso = 0; int dual = 0, prevj, nextj;
        for(j = 0; j < sizeof ARJOIN / sizeof ARJOIN[0]; j++)
            if(ARJOIN[j].base == orig[k]){ found = 1; iso = ARJOIN[j].iso; dual = ARJOIN[j].dual; break; }
        if(!found) continue;
        prevj = k > 0 && arjoin_type(orig[k-1]) == 2;
        nextj = dual && k + 1 < n && arjoin_type(orig[k+1]) != 0;
        cp[k] = prevj && nextj ? iso + 3 : prevj ? iso + 1 : nextj ? iso + 2 : iso;
    }
#ifdef CHAT_SHEENBIDI
    {                                                /* full UAX #9 reorder over the shaped codepoints */
        unsigned int vis[512];
        int vn = bidi_reorder_sheenbidi(cp, n, base_rtl, vis);
        for(k = 0; k < vn && o < (int)cap - 4; k++) o += enc_utf8(vis[k], out + o);
    }
#else
    {
        int a, b;
        if(base_rtl){                                /* reverse all (level 1) … */
            for(a = 0, b = n - 1; a < b; a++, b--){ unsigned int t = cp[a]; cp[a] = cp[b]; cp[b] = t; }
            for(k = 0; k < n;){                      /* … un-reverse Latin/number runs (level 2) */
                if(is_ltr_strong(cp[k])){
                    int e = k;
                    while(e < n && (is_ltr_strong(cp[e]) || (cp[e] == ' ' && e + 1 < n && is_ltr_strong(cp[e+1])))) e++;
                    for(a = k, b = e - 1; a < b; a++, b--){ unsigned int t = cp[a]; cp[a] = cp[b]; cp[b] = t; }
                    k = e;
                } else k++;
            }
        } else {                                     /* base LTR: reverse maximal RTL runs in place */
            for(k = 0; k < n;){
                if(is_rtl_cp(cp[k])){
                    int e = k;
                    while(e < n && (is_rtl_cp(cp[e]) || (cp[e] == ' ' && e + 1 < n && is_rtl_cp(cp[e+1])))) e++;
                    for(a = k, b = e - 1; a < b; a++, b--){ unsigned int t = cp[a]; cp[a] = cp[b]; cp[b] = t; }
                    k = e;
                } else k++;
            }
        }
        for(k = 0; k < n && o < (int)cap - 4; k++) o += enc_utf8(cp[k], out + o);
    }
#endif
    out[o] = '\0';
}
/* Base direction of a body: the first strong character (UAX #9 P2/P3). */
static int first_strong_rtl(const char *s){
    size_t i = 0, len = strlen(s);
    while(s[i]){
        uint32_t cp; int a = timui_utf8_decode(s + i, len - i, &cp); if(a <= 0) a = 1;
        if(is_rtl_cp(cp)) return 1;
        if((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) return 0;
        i += (size_t)a;
    }
    return 0;
}

/* ---- display metrics + word wrap ---------------------------------------- *
 * rich_seg measures ONE draw_rich segment so wrap_rich and drawing agree on the
 * cell width of every span: markdown markers (asterisk, underscore, backtick) = 0,
 * :shortcode: = 2, an image badge ![alt](url) = 3 + alt width, an http(s) URL = its
 * byte length, else timui_utf8_width (CJK/emoji = 2). *sp = 1 for a breakable space. */
static size_t rich_seg(const char *s, size_t i, size_t len, int *w, int *sp){
    *sp = 0;
    if(s[i] == '!' && s[i+1] == '['){
        size_t a = i + 2, ae = a, u, ue;
        while(s[ae] && s[ae] != ']') ae++;
        if(s[ae] == ']' && s[ae+1] == '('){
            u = ae + 2; ue = u; while(s[ue] && s[ue] != ')') ue++;
            if(s[ue] == ')'){
                int aw = 0; size_t k = a;
                while(k < ae){ uint32_t cp; int adv = timui_utf8_decode(s + k, ae - k, &cp); if(adv <= 0) adv = 1; aw += timui_utf8_width(cp); k += (size_t)adv; }
                *w = 3 + aw; return ue + 1 - i;
            }
        }
    }
    if(strncmp(s + i, "http://", 7) == 0 || strncmp(s + i, "https://", 8) == 0){
        size_t j = i; while(s[j] && s[j] != ' ' && s[j] != '\t') j++;
        *w = (int)(j - i); return j - i;
    }
    if(s[i] == '*' || s[i] == '_' || s[i] == '`'){ *w = 0; return 1; }
    { size_t cl; if(shortcode_at(s + i, &cl)){ *w = 2; return cl; } }
    { uint32_t cp; int adv = timui_utf8_decode(s + i, len - i, &cp); if(adv <= 0) adv = 1;
      *w = timui_utf8_width(cp); if(cp == ' ') *sp = 1; return (size_t)adv; }
}
/* Greedy word-wrap of `s` to `width` display columns using rich_seg's metrics.
 * Fills starts[] with the byte offset where each visual line begins (starts[0]=0)
 * and returns the line count (>=1, capped at maxlines). Breaks at the last space;
 * a single token wider than `width` is hard-broken (and, if alone on a line, is
 * left to overflow rather than loop). */
static int wrap_rich(const char *s, int width, int *starts, int maxlines){
    size_t i = 0, len = strlen(s);
    int nlines = 0, col = 0, line_start = 0, last_brk = -1;
    if(maxlines <= 0) return 0;
    if(width < 1) width = 1;
    starts[nlines++] = 0;
    while(i < len){
        int w, sp; size_t adv = rich_seg(s, i, len, &w, &sp);
        if(col + w > width && col > 0){
            int brk = (last_brk > line_start) ? last_brk : (int)i;   /* last space, else hard-break */
            if(nlines < maxlines) starts[nlines] = brk;
            nlines++;
            line_start = brk; col = 0; last_brk = -1;
            i = (size_t)brk;                                         /* re-scan the new line */
            continue;
        }
        col += w;
        if(sp) last_brk = (int)(i + adv);
        i += adv;
    }
    return nlines;
}

/* ---- message layout: \n line breaks + fenced ```lang … ``` code blocks --- *
 * Expand a message into the visual rows to draw: text lines are word-wrapped to
 * `textw`, code lines are kept verbatim (highlighted, not wrapped), and each ```
 * fence becomes a chrome rule. Returns the total row count, writing up to `cap`. */
#define MSG_MAXLINES 128
enum { RK_TEXT, RK_CODE, RK_FENCE };
typedef struct { int off, len; unsigned char kind; char lang[12]; } MsgRow;
static int msg_visual_rows(const char *s, int textw, MsgRow *rows, int cap){
    int n = 0, in_code = 0;
    char lang[12] = "";
    size_t i = 0, len = strlen(s);
    for(;;){
        size_t e = i; while(e < len && s[e] != '\n') e++;
        int plen = (int)(e - i);
        int fence = plen >= 3 && s[i] == '`' && s[i+1] == '`' && s[i+2] == '`';
        if(fence){
            if(!in_code){ int k = 3, li = 0;                 /* opening: capture the language tag */
                while(k < plen && s[i+k] != ' ' && li < (int)sizeof lang - 1) lang[li++] = s[i+k++];
                lang[li] = '\0'; in_code = 1;
            } else in_code = 0;                              /* closing fence */
            if(n < cap){ rows[n].off = (int)i; rows[n].len = plen; rows[n].kind = RK_FENCE; rows[n].lang[0] = '\0'; }
            n++;
        } else if(in_code){
            if(n < cap){ int li = 0; rows[n].off = (int)i; rows[n].len = plen; rows[n].kind = RK_CODE;
                         while(lang[li] && li < (int)sizeof rows[n].lang - 1){ rows[n].lang[li] = lang[li]; li++; }
                         rows[n].lang[li] = '\0'; }
            n++;
        } else {                                            /* text line: word-wrap it */
            char line[1024]; int wl = plen < (int)sizeof line ? plen : (int)sizeof line - 1;
            int starts[MSG_MAXLINES], wn, w;
            memcpy(line, s + i, (size_t)wl); line[wl] = '\0';
            wn = wrap_rich(line, textw, starts, MSG_MAXLINES);
            for(w = 0; w < wn; w++){
                int roff = (int)i + starts[w];
                int rend = (w + 1 < wn) ? (int)i + starts[w+1] : (int)e;
                if(n < cap){ rows[n].off = roff; rows[n].len = rend - roff; rows[n].kind = RK_TEXT; rows[n].lang[0] = '\0'; }
                n++;
            }
        }
        if(e >= len) break;
        i = e + 1;
    }
    return n;
}

#endif /* CHAT_TEXT_H */
