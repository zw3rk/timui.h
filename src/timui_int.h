#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
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

struct TimuiFrame { Timui *ui; };

struct Timui {
    TimuiConfig       cfg;
    TimuiAllocator    alloc;
    TimuiTransport    transport;
    int               have_transport;
    TimuiFdCtx        fd;
    TimuiCaps         caps;
    TimuiScreenMode   screen;
    int               screen_active;
    TimuiTermios      termios;
    int               termios_active;
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
    char              text_in[256];
    int               text_in_len;
    char              paste_buf[256];   /* bracketed-paste accumulator (ev ptr is transient; a paste
                                         * can also span several reads -> several events per frame) */
    int               paste_len;
    int               trace_fd;         /* TIMUI_TRACE input trace fd, -1 = off */
    /* Submit segmentation for timui_input_field: byte offsets in text_in where
     * Enter fired this frame, in order. Lets the field submit ONE segment per
     * frame ("a\rb\r" -> "a" then "b") instead of merging; the post-first-Enter
     * tail is stashed in pending_* and re-injected by timui_begin next frame. */
    int               enter_at[32];
    int               enter_count;
    char              pending_in[256];
    int               pending_in_len;
    int               pending_enter_at[32];
    int               pending_enter_count;
    unsigned          key_in;
    TimuiKey          key_pressed;
    uint32_t          key_mods;     /* modifiers of the last key event */
    int               mouse_wheel;  /* accumulated wheel delta this frame (+up/-down) */
    int               mouse_x, mouse_y;   /* last reported cell (0-based) */
    int               mouse_clicked;      /* a button press occurred this frame */
    /* F1.4: hardware cursor request for the focused input. cursor_visible is a
     * per-frame request (reset in timui_begin, set by the focused input);
     * cursor_shown tracks what's on the terminal so a hide is emitted once. */
    int               cursor_x, cursor_y, cursor_visible, cursor_shown;
    int               events_dropped;
    int               w, h;
    int               should_quit;
    /* One feed reads up to 256 bytes and can emit one event PER byte (e.g. a
     * drag-drop path typed as text), so the queue must hold a whole read plus a
     * deferred ESC — 16 dropped all but the first 16 chars of a dropped path. */
    TimuiEvent        events[512];
    int               event_count;
    struct { TimuiRect clip; int has_clip; } clip_stack[8];
    int               clip_count;
    /* Kitty-graphics image placements recorded this frame by timui_image_draw;
     * transmitted (once, keyed by TimuiImage.id) and placed ON TOP of the cell
     * diff in timui_end, so they compose with the renderer. */
    struct { TimuiImage *img; TimuiRect rect; TimuiRect full; } img_place[8];   /* rect=visible, full=uncropped */
    int               img_place_count;
    int               img_last_count;   /* placements emitted last frame (for shrink-cleanup) */
    uint32_t          next_image_id;
    /* Z27: menu state moved out of Timui into the caller-owned TimuiMenuBar. */
    TimuiFrame        frame;
};

/* Transmit+place any images recorded this frame (Kitty graphics), on top of the
 * cell diff. Defined in timui_kitty.c; called by timui_end in timui_core.c. */
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

/* Fill a one-row rect with style `st`, then draw `text` at column offset `xoff`
 * within it (draw_text ignores a NULL/empty str, so callers can pass either). */
static void timui_draw_row_(TimuiCellBuffer *buf, TimuiRect row, int xoff, TimuiStr text, TimuiStyle st){
    timui_draw_fill(buf, row, st);
    timui_draw_text(buf, row.x + xoff, row.y, text, st);
}

/* Move a selection index by one on Up/Down (mutually exclusive), clamped to
 * [0, count-1]. Callers gate this on focus. */
static int timui_updown_nav_(TimuiFrame *f, int selected, int count){
    if(timui_key_pressed(f, TIMUI_KEY_UP) && selected > 0) selected--;
    else if(timui_key_pressed(f, TIMUI_KEY_DOWN) && selected < count - 1) selected++;
    return selected;
}

