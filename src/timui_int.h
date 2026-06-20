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
    unsigned          key_in;
    TimuiKey          key_pressed;
    uint32_t          key_mods;     /* modifiers of the last key event */
    /* F1.4: hardware cursor request for the focused input. cursor_visible is a
     * per-frame request (reset in timui_begin, set by the focused input);
     * cursor_shown tracks what's on the terminal so a hide is emitted once. */
    int               cursor_x, cursor_y, cursor_visible, cursor_shown;
    int               events_dropped;
    int               w, h;
    int               should_quit;
    TimuiEvent        events[16];
    int               event_count;
    struct { TimuiRect clip; int has_clip; } clip_stack[8];
    int               clip_count;
    /* Z27: menu state moved out of Timui into the caller-owned TimuiMenuBar. */
    TimuiFrame        frame;
};

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

