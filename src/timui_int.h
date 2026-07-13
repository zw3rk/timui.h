#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <poll.h>
#include <signal.h>
#ifndef TIMUI_NO_THREADS
#include <pthread.h>
#endif

/* Internal structs (completed only in the implementing TU). */
typedef struct { int read_fd; int write_fd; } TimuiFdCtx;

enum { TIMUI_EDIT_TEXT = 1, TIMUI_EDIT_KEY = 2 };
#define TIMUI_EDIT_KEY_ENTER_ 0x80000000u
typedef struct TimuiEditOp {
    int kind;
    unsigned key;
    int start;
    int len;
    uint32_t mods;
} TimuiEditOp;

struct TimuiFrame { Timui *ui; };

struct Timui {
    TimuiConfig       cfg;
    TimuiAllocator    alloc;
    TimuiTransport    transport;
    int               have_transport;
    TimuiFdCtx        fd;
    int               input_flags_saved;
    int               input_flags;
    TimuiCaps         caps;
    TimuiScreenMode   screen;
    int               screen_active;
    TimuiTermios      termios;
    int               termios_active;
    struct sigaction  prev_sigterm;
    struct sigaction  prev_sighup;
    struct sigaction  prev_sigquit;
    int               prev_sigterm_saved;
    int               prev_sighup_saved;
    int               prev_sigquit_saved;
    TimuiCellBuffer   curr;
    TimuiCellBuffer   prev;
    int               have_buffers;
    TimuiRenderer     renderer;
    TimuiInputParser  input;
    TimuiMpsc         postq;     /* thread-safe message queue (timui_post) */
    int               have_postq;
    TimuiIdStack      ids;
    int               have_ids;
    TimuiInteract     ia;
    TimuiTheme        theme;
    const TimuiStylesheet *stylesheet;   /* borrowed; caller owns parse/free */
    char              text_in[256];
    int               text_in_len;
    char              paste_buf[256];   /* bracketed-paste accumulator (ev ptr is transient; a paste
                                         * can also span several reads -> several events per frame) */
    int               paste_len;
    char              paste_utf8_tail[4];
    int               paste_utf8_tail_len;
    int               trace_fd;         /* TIMUI_TRACE input trace fd, -1 = off */
    /* Submit segmentation for timui_input_field: byte offsets in text_in where
     * Enter fired this frame, in order. Lets the field submit ONE segment per
     * frame ("a\rb\r" -> "a" then "b") instead of merging; the post-first-Enter
     * tail is stashed in pending_* and re-injected by timui_begin next frame. */
    int               enter_at[32];
    uint32_t          enter_mods[32];
    int               enter_count;
    char              pending_in[256];
    int               pending_in_len;
    int               pending_enter_at[32];
    uint32_t          pending_enter_mods[32];
    int               pending_enter_count;
    TimuiEditOp       edit_ops[512];
    int               edit_count;
    unsigned          key_in;
    TimuiKey          key_pressed;
    uint32_t          key_mods;     /* modifiers of the last key event */
    int               mouse_wheel;  /* accumulated wheel delta this frame (+up/-down) */
    int               mouse_wheel_x, mouse_wheel_y; /* cell of the wheel event */
    int               mouse_x, mouse_y;   /* last reported cell (0-based) */
    int               mouse_clicked;      /* a button press occurred this frame */
    int               mouse_click_x, mouse_click_y; /* press cell for mouse_clicked */
    /* F1.4: hardware cursor request for the focused input. cursor_visible is a
     * per-frame request (reset in timui_begin, set by the focused input);
     * cursor_shown tracks what's on the terminal so a hide is emitted once. */
    int               cursor_x, cursor_y, cursor_visible, cursor_shown;
    int               events_dropped;
    int               w, h;
    int               cell_px_w, cell_px_h;   /* 0 == terminal did not report pixel geometry */
    int               should_quit;
    /* One feed reads up to 256 bytes and can emit one event PER byte (e.g. a
     * drag-drop path typed as text), so the queue must hold a whole read plus a
     * deferred ESC — 16 dropped all but the first 16 chars of a dropped path. */
    TimuiEvent        events[512];
    int               event_count;
    struct TimuiClipSnapshot { TimuiRect clip; int has_clip; } *clip_stack;
    int               clip_count;
    int               clip_cap;
    /* Terminal-image placements recorded this frame by timui_image_draw;
     * emitted ON TOP of the cell diff in timui_end, so they compose with the
     * renderer. Protocol-specific lifecycle state is tracked separately. */
    struct { TimuiImage *img; TimuiRect rect; TimuiRect full; } img_place[8];   /* rect=visible, full=uncropped */
    int               img_place_count;
    int               img_last_count;       /* placements emitted last frame */
    TimuiImageProtocol img_last_protocol;   /* protocol that emitted those placements */
    uint32_t          next_image_id;
    /* Z27: menu state moved out of Timui into the caller-owned TimuiMenuBar. */
    TimuiFrame        frame;
};

/* Emit any images recorded this frame, on top of the cell diff. Defined in
 * timui_images.c; called by timui_end in timui_core.c. */
void timui_images_flush_(Timui *ui);

/* Z6: the single shared UTF-8 encoder. Encodes an already-validated codepoint
 * into `out` and returns the byte count (1..4). Defined in the first-included
 * internal header so every section — the frame text buffer (core), the diff
 * renderer (render), and the title sanitizer (term) — uses one copy, rather
 * than each hand-inlining its own because the amalgamation include order put
 * the previous static out of scope. */
static int timui_utf8_encode_(uint32_t cp, char *out){
    if(cp < 0x80){ out[0] = (char)cp; return 1; }
    if(cp < 0x800){ out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if(cp < 0x10000){
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* Z28: shared list-widget scaffolding used by listbox / tree / table / command
 * palette / menu. Defined here (first-included) so every section can call them;
 * they only reference already-declared public primitives. */

static TimuiRect timui_intersect_rect_(TimuiRect a, TimuiRect b){
    TimuiRect r;
    int64_t ax2 = (int64_t)a.x + (int64_t)a.w;
    int64_t ay2 = (int64_t)a.y + (int64_t)a.h;
    int64_t bx2 = (int64_t)b.x + (int64_t)b.w;
    int64_t by2 = (int64_t)b.y + (int64_t)b.h;
    int64_t x1 = a.x > b.x ? (int64_t)a.x : (int64_t)b.x;
    int64_t y1 = a.y > b.y ? (int64_t)a.y : (int64_t)b.y;
    int64_t x2 = ax2 < bx2 ? ax2 : bx2;
    int64_t y2 = ay2 < by2 ? ay2 : by2;
    int64_t rw = x2 > x1 ? x2 - x1 : 0;
    int64_t rh = y2 > y1 ? y2 - y1 : 0;
    r.x = x1 < INT_MIN ? INT_MIN : (x1 > INT_MAX ? INT_MAX : (int)x1);
    r.y = y1 < INT_MIN ? INT_MIN : (y1 > INT_MAX ? INT_MAX : (int)y1);
    r.w = rw > INT_MAX ? INT_MAX : (int)rw;
    r.h = rh > INT_MAX ? INT_MAX : (int)rh;
    return r;
}

static int timui_rect_contains_(TimuiRect r, int x, int y){
    int64_t rx2, ry2;
    if(r.w <= 0 || r.h <= 0) return 0;
    rx2 = (int64_t)r.x + (int64_t)r.w;
    ry2 = (int64_t)r.y + (int64_t)r.h;
    return (int64_t)x >= (int64_t)r.x && (int64_t)x < rx2 &&
           (int64_t)y >= (int64_t)r.y && (int64_t)y < ry2;
}

static int timui_mouse_wheel_over_(const Timui *ui, TimuiRect r){
    return ui && ui->mouse_wheel &&
           timui_rect_contains_(r, ui->mouse_wheel_x, ui->mouse_wheel_y);
}

static void timui_draw_text_clipped_(TimuiCellBuffer *buf, TimuiRect clip, int x, int y,
                                     TimuiStr text, TimuiStyle st){
    TimuiRect old_clip, active;
    int old_has_clip;
    if(!buf) return;
    old_clip = buf->clip;
    old_has_clip = buf->has_clip;
    active = buf->has_clip ? buf->clip : TIMUI_RECT(0, 0, buf->w, buf->h);
    buf->clip = timui_intersect_rect_(active, clip);
    buf->has_clip = 1;
    timui_draw_text(buf, x, y, text, st);
    buf->clip = old_clip;
    buf->has_clip = old_has_clip;
}

/* Fill a one-row rect with style `st`, then draw `text` at column offset `xoff`
 * within it (draw_text ignores a NULL/empty str, so callers can pass either). */
static void timui_draw_row_(TimuiCellBuffer *buf, TimuiRect row, int xoff, TimuiStr text, TimuiStyle st){
    timui_draw_fill(buf, row, st);
    timui_draw_text_clipped_(buf, row, row.x + xoff, row.y, text, st);
}

/* Move a selection index by one on Up/Down (mutually exclusive), clamped to
 * [0, count-1]. Callers gate this on focus. */
static int timui_updown_nav_(TimuiFrame *f, int selected, int count){
    if(timui_key_pressed(f, TIMUI_KEY_UP) && selected > 0) selected--;
    else if(timui_key_pressed(f, TIMUI_KEY_DOWN) && selected < count - 1) selected++;
    return selected;
}
