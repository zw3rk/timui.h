/* ---- Grapheme cluster helpers (Phase 1.5) ------------------------------ *
 * Table-driven coverage for the cluster classes that matter most in terminal
 * editing and truncation: combining marks, variation selectors, emoji
 * modifiers, regional-indicator flags, CRLF, and ZWJ emoji sequences.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd. */

static int timui_cp_between_(uint32_t cp, uint32_t lo, uint32_t hi){
    return cp >= lo && cp <= hi;
}

static int timui_grapheme_extend_(uint32_t cp){
    return
        timui_cp_between_(cp, 0x0300, 0x036F) ||      /* Combining Diacritical Marks */
        timui_cp_between_(cp, 0x1AB0, 0x1AFF) ||
        timui_cp_between_(cp, 0x1DC0, 0x1DFF) ||
        timui_cp_between_(cp, 0x20D0, 0x20FF) ||
        timui_cp_between_(cp, 0xFE20, 0xFE2F) ||
        timui_cp_between_(cp, 0xFE00, 0xFE0F) ||      /* variation selectors */
        timui_cp_between_(cp, 0xE0100, 0xE01EF) ||
        timui_cp_between_(cp, 0x1F3FB, 0x1F3FF);      /* emoji skin tones */
}

static int timui_grapheme_ri_(uint32_t cp){
    return timui_cp_between_(cp, 0x1F1E6, 0x1F1FF);
}

static int timui_grapheme_emoji_base_(uint32_t cp){
    return
        timui_cp_between_(cp, 0x1F000, 0x1FAFF) ||
        timui_cp_between_(cp, 0x2600, 0x27BF) ||
        timui_cp_between_(cp, 0x2300, 0x23FF) ||
        cp == 0x00A9 || cp == 0x00AE;
}

static size_t timui_grapheme_decode_(const char *s, size_t len, size_t off, uint32_t *cp){
    int adv;
    if(cp) *cp = 0;
    if(!s || off >= len) return off;
    adv = timui_utf8_decode(s + off, len - off, cp);
    if(adv <= 0) adv = 1;
    if(off + (size_t)adv > len) return len;
    return off + (size_t)adv;
}

TIMUI_API size_t timui_grapheme_next(const char *s, size_t len, size_t off){
    uint32_t cp = 0;
    size_t cur;
    int ri_count = 0;
    if(!s || off >= len) return len;
    cur = timui_grapheme_decode_(s, len, off, &cp);

    if(cp == '\r'){
        uint32_t ncp = 0;
        size_t n = timui_grapheme_decode_(s, len, cur, &ncp);
        if(n > cur && ncp == '\n') return n;          /* CRLF is one cluster */
        return cur;
    }
    if(cp == '\n') return cur;
    if(timui_grapheme_ri_(cp)) ri_count = 1;

    for(;;){
        uint32_t ncp = 0;
        size_t n;
        if(cur >= len) break;
        n = timui_grapheme_decode_(s, len, cur, &ncp);
        if(n <= cur) break;
        if(timui_grapheme_extend_(ncp)){
            cur = n;
            continue;
        }
        if(ncp == 0x200D){                            /* ZWJ sticks to both sides */
            cur = n;
            if(cur < len)
                cur = timui_grapheme_decode_(s, len, cur, NULL);
            continue;
        }
        if(ri_count == 1 && timui_grapheme_ri_(ncp)){
            cur = n;                                  /* RI RI flag pair */
            ri_count = 2;
            continue;
        }
        break;
    }
    return cur;
}

TIMUI_API size_t timui_grapheme_prev(const char *s, size_t len, size_t off){
    size_t prev = 0, cur = 0;
    if(!s || off == 0) return 0;
    if(off > len) off = len;
    while(cur < off){
        size_t next = timui_grapheme_next(s, len, cur);
        if(next >= off) return cur;
        if(next <= cur) break;
        prev = cur;
        cur = next;
    }
    return prev;
}

TIMUI_API int timui_grapheme_width(const char *s, size_t len){
    size_t end, i;
    int w = 0, saw_ri = 0, saw_zwj = 0, saw_vs16 = 0, saw_emoji = 0;
    if(!s || len == 0) return 0;
    end = timui_grapheme_next(s, len, 0);
    for(i = 0; i < end;){
        uint32_t cp = 0;
        size_t n = timui_grapheme_decode_(s, end, i, &cp);
        int cw;
        if(n <= i) break;
        if(cp == 0x200D){ saw_zwj = 1; i = n; continue; }
        if(cp == 0xFE0F){ saw_vs16 = 1; i = n; continue; }
        if(timui_grapheme_extend_(cp)){ i = n; continue; }
        if(timui_grapheme_ri_(cp)){ saw_ri++; saw_emoji = 1; i = n; continue; }
        if(timui_grapheme_emoji_base_(cp)) saw_emoji = 1;
        cw = timui_utf8_width(cp);
        if(cw > w) w = cw;
        i = n;
    }
    if(saw_ri >= 1) return 2;
    if(saw_zwj && saw_emoji) return 2;
    if(saw_vs16 && saw_emoji && w < 2) return 2;
    return w;
}
