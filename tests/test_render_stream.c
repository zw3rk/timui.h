/*
 * test_render_stream.c — replay the diff renderer's byte stream through a
 * minimal VT model and assert the reconstructed grid matches the intended cell
 * buffer. The unit/golden tests compare cell CONTENT; this compares the emitted
 * STREAM (CUP placement, auto-wrap, cursor interaction) — the only way to catch
 * misplacement bugs a real terminal would show.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>

/* ---- minimal VT model -------------------------------------------------- *
 * Codepoint per cell + cursor with DEFERRED auto-wrap (modern terminals like
 * Ghostty: writing the last column sets a pending-wrap flag; the NEXT glyph
 * wraps, a CUP clears it). Auto-wrap is ON unless \x1b[?7l is seen. */
#define VT_W 40
#define VT_H 12
typedef struct {
    uint32_t cp[VT_H][VT_W];
    uint32_t bg[VT_H][VT_W];         /* background color per cell */
    int cx, cy, pending, autowrap;
    uint32_t cur_bg;                 /* current SGR background */
} Vt;

static void vt_reset(Vt *v){
    int y, x;
    memset(v, 0, sizeof *v); v->autowrap = 1;
    v->cur_bg = TIMUI_COLOR_DEFAULT;
    for(y = 0; y < VT_H; y++) for(x = 0; x < VT_W; x++) v->bg[y][x] = TIMUI_COLOR_DEFAULT;
}

static void vt_scroll(Vt *v){                 /* content scrolls up when it wraps past the last row */
    int y, x;
    for(y = 0; y < VT_H - 1; y++) for(x = 0; x < VT_W; x++){ v->cp[y][x] = v->cp[y+1][x]; v->bg[y][x] = v->bg[y+1][x]; }
    for(x = 0; x < VT_W; x++){ v->cp[VT_H-1][x] = 0; v->bg[VT_H-1][x] = TIMUI_COLOR_DEFAULT; }
}
static void vt_put(Vt *v, uint32_t cp, int w){
    if(v->pending && v->autowrap){
        v->cx = 0; v->cy += 1; v->pending = 0;
        if(v->cy >= VT_H){ vt_scroll(v); v->cy = VT_H - 1; }         /* wrap past the bottom -> scroll */
    }
    if(v->cy >= 0 && v->cy < VT_H && v->cx >= 0 && v->cx < VT_W){
        v->cp[v->cy][v->cx] = cp;
        v->bg[v->cy][v->cx] = v->cur_bg;
        if(w >= 2 && v->cx + 1 < VT_W){ v->cp[v->cy][v->cx + 1] = 0; v->bg[v->cy][v->cx + 1] = v->cur_bg; }
    }
    v->cx += (w >= 2 ? 2 : 1);
    if(v->cx >= VT_W){ v->cx = VT_W; v->pending = 1; }               /* deferred wrap */
}

static void vt_feed(Vt *v, const char *s, size_t n){
    size_t i = 0;
    while(i < n){
        unsigned char c = (unsigned char)s[i];
        if(c == 0x1b && i + 1 < n && s[i+1] == '['){        /* CSI */
            size_t j = i + 2;
            int priv = 0, p[8], np, k;
            for(k = 0; k < 8; k++) p[k] = 0;
            if(j < n && s[j] == '?'){ priv = 1; j++; }
            np = 1;
            while(j < n && (((unsigned char)s[j] >= '0' && (unsigned char)s[j] <= '9') || s[j] == ';')){
                if(s[j] == ';'){ if(np < 8) np++; }
                else if(np - 1 < 8) p[np-1] = p[np-1] * 10 + (s[j] - '0');
                j++;
            }
            if(j < n){
                char fin = s[j];
                if(!priv && fin == 'H'){                     /* CUP row;col (1-based) */
                    v->cy = (np >= 1 ? p[0] : 1) - 1;
                    v->cx = (np >= 2 ? p[1] : 1) - 1;
                    if(v->cx < 0) v->cx = 0;
                    if(v->cy < 0) v->cy = 0;
                    v->pending = 0;
                } else if(priv && p[0] == 7 && (fin == 'l' || fin == 'h')){
                    v->autowrap = (fin == 'h');              /* DECAWM */
                } else if(!priv && fin == 'm'){              /* SGR */
                    if(p[0] == 0) v->cur_bg = TIMUI_COLOR_DEFAULT;   /* reset */
                    else if(p[0] == 48 && p[1] == 2)
                        v->cur_bg = ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 8) | (uint32_t)p[4];
                }
                i = j + 1;
                continue;
            }
        }
        if(c == 0x1b && i + 1 < n && s[i+1] == ']'){         /* OSC ... ST/BEL */
            size_t j = i + 2;
            while(j < n && s[j] != 0x07 && !(s[j] == 0x1b && j+1 < n && s[j+1] == '\\')) j++;
            if(j < n && s[j] == 0x1b) j++;
            i = (j < n) ? j + 1 : n;
            continue;
        }
        if(c == 0x1b){ i += 2; continue; }                   /* other ESC: skip 2 */
        if(c < 0x20){ i++; continue; }                       /* stray control */
        {   /* a glyph */
            uint32_t cp = 0;
            int adv = timui_utf8_decode(s + i, n - i, &cp);
            int w = timui_utf8_width(cp);
            if(adv <= 0) adv = 1;
            vt_put(v, cp, w);
            i += (size_t)adv;
        }
    }
}

/* A faithful mirror of chat.c's draw_transcript: fill the body, draw the last N
 * lines bottom-aligned (newest at the bottom), plus the focused input field.
 * `msgs`/`nmsg` is the growing transcript. Snapshots the intended grid. */
#define BODY_H 10
static void draw_and_snapshot(Timui *ui, char msgs[][64], int nmsg,
                              TimuiInputState *inp, uint32_t want[VT_H][VT_W],
                              uint32_t want_bg[VT_H][VT_W]){
    TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    int x, y, shown, start, i;
    TimuiRect body = TIMUI_RECT(0, 0, VT_W, BODY_H);
    timui_begin(ui, &f);
    timui_set_focus(f, TIMUI_ID("compose"));
    buf = timui_frame_buffer(f);
    timui_draw_fill(buf, body, timui_style_make(0xC0C0C0, 0x202020, 0));
    shown = nmsg < BODY_H ? nmsg : BODY_H;
    start = nmsg - shown;
    for(i = 0; i < shown; i++){
        y = body.y + body.h - shown + i;             /* bottom-align the tail */
        timui_draw_text(buf, body.x + 1, y, timui_str_from_cstr(msgs[start + i]),
                        timui_style_make(0xFFFFFF, 0x202020, 0));
    }
    timui_input_field(f, TIMUI_ID("compose"), TIMUI_RECT(0, VT_H - 2, VT_W, 1), inp);
    timui_function_bar(f, TIMUI_RECT(0, VT_H - 1, VT_W, 1), TIMUI_STR_LIT(" F10 Quit  Enter Send "));
    for(y = 0; y < VT_H; y++)
        for(x = 0; x < VT_W; x++){
            TimuiCell *cell = timui_cells_get(buf, x, y);
            want[y][x]    = (cell && cell->codepoint) ? cell->codepoint : ' ';
            want_bg[y][x] = cell ? cell->bg : TIMUI_COLOR_DEFAULT;
        }
    timui_end(f);
}

static int out_has(TimuiStr o, const char *needle){
    size_t nl = strlen(needle), i;
    if(o.len < nl) return 0;
    for(i = 0; i + nl <= o.len; i++) if(memcmp(o.ptr + i, needle, nl) == 0) return 1;
    return 0;
}
/* A frame must be wrapped in synchronized output (DEC 2026) when the terminal
 * supports it, so a partial update never reaches the screen (no tearing). */
TIMUI_TEST(test_frame_synchronized_output){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL; TimuiFrame *f = NULL;
    TimuiStr out;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 8, 2, &al);
    /* control: no sync cap (as in a test/basic terminal) -> no sync markers */
    timui_fake_clear_output(&fake);
    timui_begin(ui, &f);
    timui_draw_fill(timui_frame_buffer(f), TIMUI_RECT(0,0,8,1), timui_style_make(0xFFFFFF, 0x000000, 0));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(out.len > 0 && !out_has(out, "\x1b[?2026h"));   /* content, but not synced */
    /* enable the cap (as a modern terminal would) -> the frame is bracketed */
    timui_force_cap(ui, TIMUI_CAP_SYNC_OUTPUT, 1);
    timui_fake_clear_output(&fake);
    timui_begin(ui, &f);
    timui_draw_fill(timui_frame_buffer(f), TIMUI_RECT(0,0,8,1), timui_style_make(0xFFFFFF, 0x0000FF, 0));
    timui_end(f);
    out = timui_fake_output(&fake);
    TIMUI_CHECK(out.len >= 8 && memcmp(out.ptr, "\x1b[?2026h", 8) == 0);   /* opens with sync */
    TIMUI_CHECK(out_has(out, "\x1b[?2026l"));                              /* closes with sync */
    timui_close(ui);
}

TIMUI_TEST(test_render_stream_chat_like){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake; TimuiTransport t;
    Timui *ui = NULL;
    Vt vt;
    char compose[64] = "";
    TimuiInputState inp = { compose, sizeof compose, 0, 0 };
    uint32_t want[VT_H][VT_W];
    uint32_t want_bg[VT_H][VT_W];
    /* growing transcript of rotated senders, exactly like chat.c's worker */
    static const char *canned[] = { "alice: hi", "bob: how's the TUI?",
        "carol: shipping a much longer 0.2.0", "system: heartbeat" };
    char msgs[64][64];
    int nmsg = 0, phase, total_bad = 0;
    timui_fake_init(&fake, &al); t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, VT_W, VT_H, &al);
    vt_reset(&vt);
    for(phase = 0; phase < 40; phase++){
        TimuiStr out;
        int x, y, bad = 0;
        /* append one message (like a worker post) and occasionally a typed reply.
         * Some replies EXCEED the width, so they reach the last column and must
         * be cleared when a shorter line later replaces the row. */
        if(nmsg < 64){ snprintf(msgs[nmsg], 64, "%s", canned[phase % 4]); nmsg++; }
        if(phase % 3 == 2 && nmsg < 64){
            snprintf(msgs[nmsg], 64, "you: %s a long reply that exceeds the width #%d", compose, phase);
            nmsg++; }
        /* grow the compose text so the input scrolls horizontally */
        { size_t cl = strlen(compose);
          if(cl + 2 < sizeof compose){ compose[cl] = 'a'+(char)(phase%26); compose[cl+1]='\0'; }
          inp.cursor = strlen(compose); }
        timui_fake_clear_output(&fake);
        draw_and_snapshot(ui, msgs, nmsg, &inp, want, want_bg);
        out = timui_fake_output(&fake);
        vt_feed(&vt, out.ptr, out.len);
        for(y = 0; y < VT_H; y++)                     /* every row, incl. the input */
            for(x = 0; x < VT_W; x++){
                uint32_t got = vt.cp[y][x] ? vt.cp[y][x] : ' ';
                if(got != want[y][x]){
                    if(bad < 10) printf("    phase %d (%d,%d) glyph: want U+%04X got U+%04X\n",
                                       phase, x, y, want[y][x], got);
                    bad++;
                } else if(y != 10 && vt.bg[y][x] != want_bg[y][x]){   /* skip the undrawn row 10 */
                    if(bad < 10) printf("    phase %d (%d,%d) BG: want %06X got %06X (glyph U+%04X)\n",
                                       phase, x, y, want_bg[y][x] & 0xFFFFFF, vt.bg[y][x] & 0xFFFFFF, want[y][x]);
                    bad++;
                }
            }
        total_bad += bad;
    }
    TIMUI_CHECK(total_bad == 0);
    timui_close(ui);
}
