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
    int               events_dropped;
    int               w, h;
    int               should_quit;
    TimuiEvent        events[16];
    int               event_count;
    struct { TimuiRect clip; int has_clip; } clip_stack[8];
    int               clip_count;
    TimuiId           open_menu;
    int               menu_bar_x, menu_bar_y, menu_item_x, menu_item_y, menu_clicked;
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

