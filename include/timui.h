/*
 * timui.h — single-header C99 immediate-mode TUI for modern terminals.
 *
 *   Primary usage (single-header / stb-style):
 *
 *       #define TIMUI_IMPLEMENTATION
 *       #include "timui.h"
 *
 *   Split build: src/timui_core.c defines TIMUI_IMPLEMENTATION and includes
 *   this header; everything else compiles against the declarations only.
 *
 * Phase 0 scaffold: the foundational types and the pure leaf helpers (rect
 * layout, ids, strings) are implemented and unit-tested. The terminal /
 * render / widget stack lands in later phases, so the lifecycle functions
 * below currently report TIMUI_ERR_UNSUPPORTED.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 *
 * See docs/PRD.md (product spec) and docs/DECISIONS.md (locked decisions).
 */
#ifndef TIMUI_H
#define TIMUI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Version ------------------------------------------------------------ */
#define TIMUI_VERSION_MAJOR 0
#define TIMUI_VERSION_MINOR 1
#define TIMUI_VERSION_PATCH 0
#define TIMUI_VERSION_STRING "0.1.0"

/* ---- Feature macros ----------------------------------------------------- *
 * TIMUI_IMPLEMENTATION   include the implementation (exactly one TU)
 * TIMUI_NO_STDIO         omit helpers that require <stdio.h>
 * TIMUI_NO_THREADS       disable the thread-safe post/wakeup API
 * TIMUI_NO_IMAGES        omit the (future) image protocol helpers
 * TIMUI_NO_UTF8_TABLES   ASCII-only / minimal Unicode build
 * TIMUI_API              override public symbol visibility
 * TIMUI_STATIC           reserved (future static-link mode)
 */
#ifndef TIMUI_API
#  define TIMUI_API
#endif

/* ---- Forward declarations ---------------------------------------------- */
typedef struct Timui      Timui;       /* runtime: caps, modes, buffers    */
typedef struct TimuiFrame TimuiFrame;  /* per-frame: valid only begin..end */

typedef uint64_t TimuiId;

/* ---- Core value types -------------------------------------------------- */
typedef struct {
    const char *ptr;
    size_t      len;
} TimuiStr;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} TimuiRect;

typedef struct {
    uint32_t fg;
    uint32_t bg;
    uint32_t attrs;
} TimuiStyle;

typedef struct {
    void  *userdata;
    void *(*alloc)(void *userdata, size_t size);
    void *(*realloc)(void *userdata, void *ptr, size_t old_size, size_t new_size);
    void  (*free)(void *userdata, void *ptr, size_t size);
} TimuiAllocator;

/* ---- Result / errors --------------------------------------------------- */
typedef enum {
    TIMUI_OK = 0,
    TIMUI_ERR_INVALID_ARGUMENT,
    TIMUI_ERR_OUT_OF_MEMORY,
    TIMUI_ERR_NOT_A_TTY,
    TIMUI_ERR_OS,
    TIMUI_ERR_UNSUPPORTED,
    TIMUI_ERR_PROTOCOL
} TimuiResult;

/* ---- Arena (low-level bump allocator; frames use it internally) ------- *
 * `alloc` is borrowed and must outlive the arena. `align` must be a power
 * of two; timui_arena_alloc returns NULL on overflow / out-of-memory. */
typedef struct {
    const TimuiAllocator *alloc;
    unsigned char        *base;
    size_t                cap;
    size_t                off;
} TimuiArena;

TIMUI_API TimuiAllocator timui_default_allocator(void);
TIMUI_API TimuiResult    timui_arena_init(TimuiArena *a, const TimuiAllocator *alloc, size_t cap);
TIMUI_API void          *timui_arena_alloc(TimuiArena *a, size_t size, size_t align);
TIMUI_API void           timui_arena_reset(TimuiArena *a);
TIMUI_API void           timui_arena_free(TimuiArena *a);

/* ---- Profiles, flags, builtin themes (capability tiers) ---------------- */
typedef enum {
    TIMUI_PROFILE_SAFE = 0,
    TIMUI_PROFILE_MODERN,
    TIMUI_PROFILE_KITTY_FAMILY,
    TIMUI_PROFILE_AUTO
} TimuiProfile;

typedef enum {
    TIMUI_THEME_DOS_BLUE = 0,
    TIMUI_THEME_DOS_GRAY,
    TIMUI_THEME_MODERN_DARK,
    TIMUI_THEME_MONO
} TimuiBuiltinTheme;

typedef enum {
    TIMUI_FLAG_ALT_SCREEN      = 1u << 0,
    TIMUI_FLAG_MOUSE           = 1u << 1,
    TIMUI_FLAG_BRACKETED_PASTE = 1u << 2,
    TIMUI_FLAG_TRUECOLOR       = 1u << 3,
    TIMUI_FLAG_SYNC_OUTPUT     = 1u << 4,
    TIMUI_FLAG_KITTY_KEYBOARD  = 1u << 5,
    TIMUI_FLAG_FOCUS_EVENTS    = 1u << 6,
    TIMUI_FLAG_HIDE_CURSOR     = 1u << 7,
    TIMUI_FLAG_RESTORE_ON_EXIT = 1u << 8
} TimuiFlags;

typedef struct {
    const char       *title;
    int               input_fd;
    int               output_fd;
    TimuiProfile      profile;
    uint32_t          flags;
    TimuiBuiltinTheme theme;
    TimuiAllocator    allocator;
    size_t            frame_arena_bytes;
    size_t            persistent_state_bytes;
    size_t            message_queue_bytes;
    void             *userdata;
} TimuiConfig;

/* ---- Convenience macros ------------------------------------------------ */
#define TIMUI_STR_LIT(s)     ((TimuiStr){ (s), sizeof(s) - 1 })
#define TIMUI_RECT(x, y, w, h) ((TimuiRect){ (x), (y), (w), (h) })
#define TIMUI_ID(s)          timui_id_from_cstr(s)

/* ---- Lifecycle (terminal backend lands in Phase 2) --------------------- */
TIMUI_API TimuiResult timui_open(const TimuiConfig *cfg, Timui **out_ui);
TIMUI_API void        timui_close(Timui *ui);
TIMUI_API const char *timui_error_string(TimuiResult result);
TIMUI_API const char *timui_version_string(void);

TIMUI_API bool      timui_begin(Timui *ui, TimuiFrame **out_frame);
TIMUI_API void      timui_end(TimuiFrame *frame);
TIMUI_API TimuiRect timui_root(const TimuiFrame *frame);
TIMUI_API int       timui_width(const TimuiFrame *frame);
TIMUI_API int       timui_height(const TimuiFrame *frame);

/* ---- IDs --------------------------------------------------------------- */
TIMUI_API TimuiId timui_id_from_bytes(const void *data, size_t len);
TIMUI_API TimuiId timui_id_from_cstr(const char *str);

/* ---- ID stack (widget identity hierarchy) ------------------------------ *
 * A stack of composed seeds: push mixes the id with the current seed so
 * nested widget paths get distinct, stable ids. Exposed fields (count, root)
 * aid collision debugging. */
typedef struct {
    TimuiId       *seeds;   /* seeds[count-1] is the current id */
    size_t         count;
    size_t         cap;
    TimuiId        root;    /* seed when the stack is empty */
    TimuiAllocator alloc;   /* owning allocator (copied) */
} TimuiIdStack;

TIMUI_API TimuiResult timui_id_stack_init(TimuiIdStack *s, const TimuiAllocator *alloc, size_t cap);
TIMUI_API void        timui_id_stack_push(TimuiIdStack *s, TimuiId id);
TIMUI_API void        timui_id_stack_push_cstr(TimuiIdStack *s, const char *str);
TIMUI_API void        timui_id_stack_pop(TimuiIdStack *s);
TIMUI_API TimuiId     timui_id_stack_current(const TimuiIdStack *s);
TIMUI_API void        timui_id_stack_destroy(TimuiIdStack *s);

/* ---- Message queue (UI-thread; variable-size, copy-in) ----------------- *
 * Framed records [type|size|data] in a contiguous slab. emit copies bytes
 * in and rejects when full (no overwrite); recv dequeues in FIFO order. */
typedef struct {
    TimuiAllocator alloc;
    unsigned char *buf;
    size_t cap;
    size_t head;
    size_t tail;
} TimuiMsgQueue;

TIMUI_API TimuiResult timui_msgq_init(TimuiMsgQueue *q, const TimuiAllocator *alloc, size_t cap);
TIMUI_API void        timui_msgq_destroy(TimuiMsgQueue *q);
TIMUI_API int         timui_msgq_emit(TimuiMsgQueue *q, uint32_t type, const void *data, size_t size);
TIMUI_API int         timui_msgq_recv(TimuiMsgQueue *q, uint32_t *out_type, void *out_buf, size_t *inout_size);
TIMUI_API int         timui_msgq_empty(const TimuiMsgQueue *q);

/* ---- MPSC queue (thread-safe post; UI-thread recv) -------------------- *
 * Many producers, one consumer. post() is thread-safe and copies the payload
 * into a node; recv() dequeues FIFO on the UI thread. Define TIMUI_NO_THREADS
 * for a single-threaded build (no pthread, unlocked). */
typedef struct TimuiMpscNode {
    struct TimuiMpscNode *next;
    uint32_t type;
    size_t size;
    unsigned char data[];   /* flexible array member — payload follows */
} TimuiMpscNode;

typedef struct {
    TimuiAllocator  alloc;
    TimuiMpscNode  *head;   /* oldest — recv pops here */
    TimuiMpscNode  *tail;   /* newest — post appends here */
    size_t          pending;
#ifndef TIMUI_NO_THREADS
    void           *lock;   /* pthread_mutex_t* (allocated in init, keeps pthread out of the header) */
#endif
} TimuiMpsc;

TIMUI_API TimuiResult timui_mpsc_init(TimuiMpsc *q, const TimuiAllocator *alloc);
TIMUI_API void        timui_mpsc_destroy(TimuiMpsc *q);
TIMUI_API int         timui_mpsc_post(TimuiMpsc *q, uint32_t type, const void *data, size_t size);
TIMUI_API int         timui_mpsc_recv(TimuiMpsc *q, uint32_t *out_type, void *out_buf, size_t *inout_size);
TIMUI_API int         timui_mpsc_empty(TimuiMpsc *q);

/* ---- Strings ----------------------------------------------------------- */
TIMUI_API size_t timui_str_len(TimuiStr s);
TIMUI_API int    timui_str_empty(TimuiStr s);
TIMUI_API int    timui_str_eq(TimuiStr a, TimuiStr b);
TIMUI_API TimuiStr timui_str_from_cstr(const char *s);
TIMUI_API size_t   timui_str_copy(char *dst, size_t cap, TimuiStr src);
TIMUI_API TimuiStr timui_str_slice(TimuiStr s, size_t start, size_t len);
TIMUI_API int      timui_str_eq_cstr(TimuiStr a, const char *b);

/* ---- Rect-split layout (pure; clamps to non-negative) ------------------ */
TIMUI_API TimuiRect timui_cut_top(TimuiRect *r, int h);
TIMUI_API TimuiRect timui_cut_bottom(TimuiRect *r, int h);
TIMUI_API TimuiRect timui_cut_left(TimuiRect *r, int w);
TIMUI_API TimuiRect timui_cut_right(TimuiRect *r, int w);
TIMUI_API TimuiRect timui_inset(TimuiRect r, int n);
TIMUI_API TimuiRect timui_pad(TimuiRect r, int l, int t, int rr, int b);
TIMUI_API void      timui_split_cols(TimuiRect r, float ratio, TimuiRect *a, TimuiRect *b);
TIMUI_API void      timui_split_rows(TimuiRect r, float ratio, TimuiRect *a, TimuiRect *b);

/* ---- Terminal transport (backend abstraction) ------------------------- *
 * A vtable of read/write/flush/close over an opaque ctx. Real backends wrap
 * file descriptors; the fake backend captures output and replays injected
 * input so renderer/parser logic is unit-testable with no real terminal. */
typedef struct TimuiTransport TimuiTransport;
typedef int  (*TimuiTransportWrite)(TimuiTransport *t, const void *data, size_t len);
typedef int  (*TimuiTransportRead)(TimuiTransport *t, void *buf, size_t cap);
typedef int  (*TimuiTransportFlush)(TimuiTransport *t);
typedef void (*TimuiTransportClose)(TimuiTransport *t);

struct TimuiTransport {
    TimuiTransportWrite  write;
    TimuiTransportRead   read;
    TimuiTransportFlush  flush;
    TimuiTransportClose  close;
    void                *ctx;
};

/* Fake transport (tests / no-real-terminal rendering). */
typedef struct {
    TimuiAllocator       alloc;
    unsigned char       *out;
    size_t               out_cap;
    size_t               out_len;
    const unsigned char *in;
    size_t               in_len;
    size_t               in_pos;
} TimuiFakeTransport;

TIMUI_API TimuiResult    timui_fake_init(TimuiFakeTransport *f, const TimuiAllocator *alloc);
TIMUI_API void           timui_fake_destroy(TimuiFakeTransport *f);
TIMUI_API void           timui_fake_set_input(TimuiFakeTransport *f, const void *bytes, size_t len);
TIMUI_API TimuiStr       timui_fake_output(const TimuiFakeTransport *f);
TIMUI_API void           timui_fake_clear_output(TimuiFakeTransport *f);
TIMUI_API TimuiTransport timui_fake_transport(TimuiFakeTransport *f);

/* ---- Screen mode setup/teardown (ANSI escape emission) --------------- *
 * enter() emits the private-mode escapes for the requested flags plus an
 * OSC title; exit() emits the matching 'l' resets in reverse order. */
typedef struct {
    uint32_t flags;   /* modes enabled by enter(); exit() reverses these */
} TimuiScreenMode;

TIMUI_API void timui_screen_enter(TimuiTransport *t, TimuiScreenMode *m, uint32_t flags, TimuiStr title);
TIMUI_API void timui_screen_exit(TimuiTransport *t, TimuiScreenMode *m);

/* ---- Terminal raw mode (POSIX) ---------------------------------------- *
 * Save the fd's termios, switch to raw (no canonical/echo/signals, 8-bit
 * clean, VMIN=1/VTIME=0), and restore exactly on close. struct termios is
 * stored opaquely so <termios.h> stays out of the public header. */
typedef struct {
    int  fd;
    void *saved;       /* struct termios* (heap-allocated in enter) */
    int  have_saved;
} TimuiTermios;

TIMUI_API TimuiResult timui_termios_enter(TimuiTermios *t, int fd);
TIMUI_API TimuiResult timui_termios_restore(TimuiTermios *t);
TIMUI_API void        timui_termios_destroy(TimuiTermios *t);

/* Query the terminal size (cols x rows) via TIOCGWINSZ. Resize is detected
 * by polling (the frame re-queries each tick), avoiding signal-handler state.
 * Returns TIMUI_ERR_NOT_A_TTY if fd is not a terminal. */
TIMUI_API TimuiResult timui_term_size(int fd, int *out_w, int *out_h);

/* ---- Capability detection --------------------------------------------- */
typedef enum {
    TIMUI_CAP_ALT_SCREEN      = 1u << 0,
    TIMUI_CAP_TRUECOLOR       = 1u << 1,
    TIMUI_CAP_256_COLOR       = 1u << 2,
    TIMUI_CAP_SGR_MOUSE       = 1u << 3,
    TIMUI_CAP_BRACKETED_PASTE = 1u << 4,
    TIMUI_CAP_FOCUS_EVENTS    = 1u << 5,
    TIMUI_CAP_SYNC_OUTPUT     = 1u << 6,
    TIMUI_CAP_KITTY_KEYBOARD  = 1u << 7,
    TIMUI_CAP_OSC8_HYPERLINKS = 1u << 8,
    TIMUI_CAP_KITTY_GRAPHICS  = 1u << 9,
    TIMUI_CAP_UNICODE_CORE    = 1u << 10
} TimuiCapFlags;

typedef struct {
    uint32_t flags;
    int      colors;
    int      width;
    int      height;
    char     term[64];
    char     term_program[64];
    char     term_program_version[64];
} TimuiCaps;

/* Pure, deterministic detection from environment strings (no I/O, no live
 * queries): known modern terminals get the modern cap set; multiplexers
 * (tmux/screen/zellij) reduce it; unknown terminals fall back to a safe
 * minimum. force_on / force_off override the result. */
TIMUI_API void timui_caps_detect(TimuiCaps *caps, const char *term, const char *term_program, const char *colorterm);
TIMUI_API void timui_caps_apply_force(TimuiCaps *caps, uint32_t force_on, uint32_t force_off);
TIMUI_API int  timui_caps_has(const TimuiCaps *caps, TimuiCapFlags cap);

/* ---- Synchronized output (DEC 2026) + cursor -------------------------- *
 * Wrap a frame's terminal writes so the terminal repaints atomically. The
 * caller gates sync on TIMUI_CAP_SYNC_OUTPUT; hide/show cursor are the safe
 * fallback when synchronized output is unavailable. */
TIMUI_API void timui_sync_begin(TimuiTransport *t);
TIMUI_API void timui_sync_end(TimuiTransport *t);
TIMUI_API void timui_hide_cursor(TimuiTransport *t);
TIMUI_API void timui_show_cursor(TimuiTransport *t);

/* ---- Events ----------------------------------------------------------- */
typedef enum {
    TIMUI_EVENT_NONE = 0, TIMUI_EVENT_KEY, TIMUI_EVENT_TEXT, TIMUI_EVENT_MOUSE,
    TIMUI_EVENT_PASTE, TIMUI_EVENT_RESIZE, TIMUI_EVENT_FOCUS,
    TIMUI_EVENT_TIMER, TIMUI_EVENT_USER
} TimuiEventKind;

typedef enum {
    TIMUI_KEY_UNKNOWN = 0, TIMUI_KEY_ESCAPE, TIMUI_KEY_ENTER, TIMUI_KEY_TAB,
    TIMUI_KEY_BACKSPACE, TIMUI_KEY_DELETE, TIMUI_KEY_INSERT,
    TIMUI_KEY_UP, TIMUI_KEY_DOWN, TIMUI_KEY_LEFT, TIMUI_KEY_RIGHT,
    TIMUI_KEY_HOME, TIMUI_KEY_END, TIMUI_KEY_PAGE_UP, TIMUI_KEY_PAGE_DOWN,
    TIMUI_KEY_F1, TIMUI_KEY_F2, TIMUI_KEY_F3, TIMUI_KEY_F4, TIMUI_KEY_F5,
    TIMUI_KEY_F6, TIMUI_KEY_F7, TIMUI_KEY_F8, TIMUI_KEY_F9, TIMUI_KEY_F10,
    TIMUI_KEY_F11, TIMUI_KEY_F12
} TimuiKey;

typedef enum {
    TIMUI_MOD_NONE = 0, TIMUI_MOD_SHIFT = 1u << 0, TIMUI_MOD_ALT = 1u << 1,
    TIMUI_MOD_CTRL = 1u << 2, TIMUI_MOD_SUPER = 1u << 3,
    TIMUI_MOD_HYPER = 1u << 4, TIMUI_MOD_META = 1u << 5
} TimuiMods;

typedef enum { TIMUI_KEY_PRESS = 0, TIMUI_KEY_REPEAT, TIMUI_KEY_RELEASE } TimuiKeyAction;

typedef struct {
    TimuiEventKind kind;
    union {
        struct { TimuiKey key; uint32_t codepoint; uint32_t mods; TimuiKeyAction action; } key;
        struct { const char *ptr; size_t len; uint32_t codepoint; } text;
        struct { const char *ptr; size_t len; } paste;
        struct { int x; int y; int button; int wheel_y; uint32_t mods;
                 int pressed; int released; int motion; } mouse;
        struct { int focused; } focus;
    } as;
} TimuiEvent;

/* ---- Input parser (legacy + CSI; incremental, callback-based) -------- *
 * Feed raw input bytes; complete key/text events are delivered to cb. The
 * parser holds state, so a sequence split across feeds still completes. */
typedef struct {
    int         state;     /* 0 ground, 1 esc, 2 csi, 3 ss3, 4 utf8 */
    int         param;     /* current CSI numeric parameter (~ keys) */
    int         nparams;   /* any parameter seen */
    int         mod_param; /* second CSI parameter (kitty modifiers) */
    int         has_mod;   /* a second parameter was given */
    int         csi_mouse; /* '<' introducer seen — SGR mouse */
    int         mparam[3]; /* mouse params: button-code, x, y */
    int         mcount;    /* mouse param index */
    int         pasting;   /* between ESC[200~ and ESC[201~ */
    const unsigned char *paste_ptr; /* start of in-feed paste content */
    int         utf8_need;
    int         utf8_len;
    uint32_t    utf8_cp;
    const char *utf8_ptr;
} TimuiInputParser;

typedef void (*TimuiEventFn)(void *ctx, const TimuiEvent *ev);

TIMUI_API void   timui_input_init(TimuiInputParser *p);
TIMUI_API size_t timui_input_feed(TimuiInputParser *p, const void *bytes, size_t len,
                                  TimuiEventFn cb, void *ctx);

#ifdef __cplusplus
}
#endif
#endif /* TIMUI_H */

/* =========================================================================
 * Implementation
 * ========================================================================= */
#ifdef TIMUI_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <termios.h>
#ifndef TIMUI_NO_THREADS
#include <pthread.h>
#endif

/* Internal structs — completed only in the implementing TU (Phase 0 stubs). */
struct Timui      { int _phase0_unused; };
struct TimuiFrame { int _phase0_unused; };

/* ---- version ----------------------------------------------------------- */
TIMUI_API const char *timui_version_string(void){
    return TIMUI_VERSION_STRING;
}

/* ---- errors ------------------------------------------------------------ */
TIMUI_API const char *timui_error_string(TimuiResult result){
    switch(result){
        case TIMUI_OK:                   return "ok";
        case TIMUI_ERR_INVALID_ARGUMENT: return "invalid argument";
        case TIMUI_ERR_OUT_OF_MEMORY:    return "out of memory";
        case TIMUI_ERR_NOT_A_TTY:        return "not a tty";
        case TIMUI_ERR_OS:               return "os error";
        case TIMUI_ERR_UNSUPPORTED:      return "unsupported";
        case TIMUI_ERR_PROTOCOL:         return "protocol error";
    }
    return "unknown";
}

/* ---- lifecycle (Phase 2 backend not yet implemented) ------------------- */
TIMUI_API TimuiResult timui_open(const TimuiConfig *cfg, Timui **out_ui){
    (void)cfg;
    (void)out_ui;
    return TIMUI_ERR_UNSUPPORTED;
}
TIMUI_API void timui_close(Timui *ui){ (void)ui; }
TIMUI_API bool timui_begin(Timui *ui, TimuiFrame **out_frame){
    (void)ui; (void)out_frame; return false;
}
TIMUI_API void timui_end(TimuiFrame *frame){ (void)frame; }
TIMUI_API TimuiRect timui_root(const TimuiFrame *frame){
    TimuiRect z = {0, 0, 0, 0};
    (void)frame;
    return z;
}
TIMUI_API int timui_width(const TimuiFrame *frame){ (void)frame; return 0; }
TIMUI_API int timui_height(const TimuiFrame *frame){ (void)frame; return 0; }

/* ---- ids (FNV-1a 64; non-cryptographic widget identity) ---------------- */
TIMUI_API TimuiId timui_id_from_bytes(const void *data, size_t len){
    const unsigned char *p = (const unsigned char *)data;
    TimuiId h = (TimuiId)1469598103934665603ull;   /* FNV-1a offset basis */
    size_t i;
    if(!p) return 0;
    for(i = 0; i < len; i++){
        h ^= (TimuiId)p[i];
        h *= (TimuiId)1099511628211ull;            /* FNV prime */
    }
    return h;
}
TIMUI_API TimuiId timui_id_from_cstr(const char *str){
    return str ? timui_id_from_bytes(str, strlen(str)) : (TimuiId)0;
}

/* Compose parent||child through FNV-1a so nested id paths are order-dependent
 * (a/b != b/a) yet stable across frames. */
#define TIMUI_ID_ROOT ((TimuiId)1469598103934665603ull)
static TimuiId id_compose(TimuiId parent, TimuiId child){
    unsigned char buf[16];
    TimuiId h = (TimuiId)1469598103934665603ull;
    size_t i;
    for(i = 0; i < 8; i++){
        buf[i]     = (unsigned char)(parent >> (8 * (7 - i)));
        buf[8 + i] = (unsigned char)(child  >> (8 * (7 - i)));
    }
    for(i = 0; i < 16; i++){ h ^= (TimuiId)buf[i]; h *= (TimuiId)1099511628211ull; }
    return h;
}
TIMUI_API TimuiResult timui_id_stack_init(TimuiIdStack *s, const TimuiAllocator *alloc, size_t cap){
    if(!s || !alloc || cap == 0) return TIMUI_ERR_INVALID_ARGUMENT;
    s->alloc = *alloc;
    s->root  = TIMUI_ID_ROOT;
    s->count = 0;
    s->cap   = cap;
    s->seeds = (TimuiId *)alloc->alloc(alloc->userdata, cap * sizeof(TimuiId));
    if(!s->seeds){ s->cap = 0; return TIMUI_ERR_OUT_OF_MEMORY; }
    return TIMUI_OK;
}
TIMUI_API void timui_id_stack_push(TimuiIdStack *s, TimuiId id){
    TimuiId seed;
    if(!s) return;
    seed = id_compose(s->count ? s->seeds[s->count - 1] : s->root, id);
    if(s->count == s->cap){                     /* grow geometrically */
        size_t ncap = s->cap * 2;
        TimuiId *ns = (TimuiId *)s->alloc.realloc(
            s->alloc.userdata, s->seeds, s->cap * sizeof(TimuiId), ncap * sizeof(TimuiId));
        if(!ns) return;                         /* OOM: drop push, id unchanged */
        s->seeds = ns;
        s->cap   = ncap;
    }
    s->seeds[s->count++] = seed;
}
TIMUI_API void timui_id_stack_push_cstr(TimuiIdStack *s, const char *str){
    if(s && str) timui_id_stack_push(s, timui_id_from_cstr(str));
}
TIMUI_API void timui_id_stack_pop(TimuiIdStack *s){
    if(s && s->count > 0) s->count--;
}
TIMUI_API TimuiId timui_id_stack_current(const TimuiIdStack *s){
    if(!s || s->count == 0) return s ? s->root : (TimuiId)0;
    return s->seeds[s->count - 1];
}
TIMUI_API void timui_id_stack_destroy(TimuiIdStack *s){
    if(!s || !s->seeds) return;
    s->alloc.free(s->alloc.userdata, s->seeds, s->cap * sizeof(TimuiId));
    s->seeds = NULL; s->cap = 0; s->count = 0;
}

/* ---- strings ----------------------------------------------------------- */
TIMUI_API size_t timui_str_len(TimuiStr s){ return s.len; }
TIMUI_API int    timui_str_empty(TimuiStr s){ return s.len == 0; }
TIMUI_API int    timui_str_eq(TimuiStr a, TimuiStr b){
    if(a.len != b.len) return 0;
    if(a.len == 0) return 1;
    return memcmp(a.ptr, b.ptr, a.len) == 0;
}
TIMUI_API TimuiStr timui_str_from_cstr(const char *s){
    TimuiStr r;
    r.ptr = s;
    r.len = s ? strlen(s) : 0;
    return r;
}
TIMUI_API size_t timui_str_copy(char *dst, size_t cap, TimuiStr src){
    size_t n = src.len;
    if(dst == NULL || cap == 0) return n;   /* report needed; write nothing */
    if(n > cap - 1) n = cap - 1;            /* truncate to cap-1 */
    if(n > 0) memcpy(dst, src.ptr, n);
    dst[n] = '\0';
    return src.len;                          /* full needed length (snprintf-style) */
}
TIMUI_API TimuiStr timui_str_slice(TimuiStr s, size_t start, size_t len){
    TimuiStr r = {NULL, 0};
    if(start >= s.len) return r;            /* past end -> empty */
    r.ptr = s.ptr + start;
    r.len = s.len - start;
    if(len < r.len) r.len = len;
    return r;
}
TIMUI_API int timui_str_eq_cstr(TimuiStr a, const char *b){
    if(b == NULL) return 0;
    return timui_str_eq(a, timui_str_from_cstr(b));
}

/* ---- message queue ----------------------------------------------------- *
 * Framed records [uint32 type][size_t size][data] laid out in a slab. emit
 * copies the payload in and returns 0 when the record won't fit (predictable
 * full — no overwrite); recv dequeues FIFO, copies up to *inout_size bytes
 * and reports the real payload size, reclaiming the slab once drained. */
TIMUI_API TimuiResult timui_msgq_init(TimuiMsgQueue *q, const TimuiAllocator *alloc, size_t cap){
    if(!q || !alloc || cap == 0) return TIMUI_ERR_INVALID_ARGUMENT;
    q->alloc = *alloc;
    q->cap   = cap;
    q->head  = 0;
    q->tail  = 0;
    q->buf   = (unsigned char *)alloc->alloc(alloc->userdata, cap);
    if(!q->buf){ q->cap = 0; return TIMUI_ERR_OUT_OF_MEMORY; }
    return TIMUI_OK;
}
TIMUI_API void timui_msgq_destroy(TimuiMsgQueue *q){
    if(!q || !q->buf) return;
    q->alloc.free(q->alloc.userdata, q->buf, q->cap);
    q->buf = NULL; q->cap = 0; q->head = 0; q->tail = 0;
}
TIMUI_API int timui_msgq_emit(TimuiMsgQueue *q, uint32_t type, const void *data, size_t size){
    const size_t hdr = sizeof(uint32_t) + sizeof(size_t);
    size_t need = hdr + size;
    unsigned char *p;
    if(!q || need > q->cap || q->tail + need > q->cap) return 0;   /* full / no fit */
    p = q->buf + q->tail;
    memcpy(p, &type, sizeof(uint32_t));
    memcpy(p + sizeof(uint32_t), &size, sizeof(size_t));
    if(size > 0 && data) memcpy(p + hdr, data, size);
    q->tail += need;
    return 1;
}
TIMUI_API int timui_msgq_recv(TimuiMsgQueue *q, uint32_t *out_type, void *out_buf, size_t *inout_size){
    const size_t hdr = sizeof(uint32_t) + sizeof(size_t);
    uint32_t type;
    size_t size, copy;
    unsigned char *p;
    if(!q || q->head >= q->tail) return 0;               /* empty */
    p = q->buf + q->head;
    memcpy(&type, p, sizeof(uint32_t));
    memcpy(&size, p + sizeof(uint32_t), sizeof(size_t));
    if(out_type) *out_type = type;
    if(inout_size){
        copy = *inout_size;
        if(copy > size) copy = size;                     /* copy at most the payload */
        if(out_buf && copy > 0) memcpy(out_buf, p + hdr, copy);
        *inout_size = size;                              /* report the real size */
    }
    q->head += hdr + size;
    if(q->head >= q->tail){ q->head = 0; q->tail = 0; }  /* reclaim when drained */
    return 1;
}
TIMUI_API int timui_msgq_empty(const TimuiMsgQueue *q){
    return !q || q->head >= q->tail;
}

/* ---- MPSC queue (thread-safe post; UI-thread recv) -------------------- */
#ifndef TIMUI_NO_THREADS
#define TIMUI_MPSC_LOCK(q)   pthread_mutex_lock((pthread_mutex_t *)(q)->lock)
#define TIMUI_MPSC_UNLOCK(q) pthread_mutex_unlock((pthread_mutex_t *)(q)->lock)
#else
#define TIMUI_MPSC_LOCK(q)   ((void)0)
#define TIMUI_MPSC_UNLOCK(q) ((void)0)
#endif
TIMUI_API TimuiResult timui_mpsc_init(TimuiMpsc *q, const TimuiAllocator *alloc){
    if(!q || !alloc) return TIMUI_ERR_INVALID_ARGUMENT;
    q->alloc = *alloc;
    q->head = NULL; q->tail = NULL; q->pending = 0;
#ifndef TIMUI_NO_THREADS
    q->lock = alloc->alloc(alloc->userdata, sizeof(pthread_mutex_t));
    if(!q->lock) return TIMUI_ERR_OUT_OF_MEMORY;
    if(pthread_mutex_init((pthread_mutex_t *)q->lock, NULL) != 0){
        alloc->free(alloc->userdata, q->lock, sizeof(pthread_mutex_t));
        q->lock = NULL;
        return TIMUI_ERR_OS;
    }
#endif
    return TIMUI_OK;
}
TIMUI_API void timui_mpsc_destroy(TimuiMpsc *q){
    uint32_t t;
    size_t s = 0;
    if(!q) return;
    while(timui_mpsc_recv(q, &t, NULL, &s)){ }       /* drain remaining nodes */
#ifndef TIMUI_NO_THREADS
    if(q->lock){
        pthread_mutex_destroy((pthread_mutex_t *)q->lock);
        q->alloc.free(q->alloc.userdata, q->lock, sizeof(pthread_mutex_t));
        q->lock = NULL;
    }
#endif
}
TIMUI_API int timui_mpsc_post(TimuiMpsc *q, uint32_t type, const void *data, size_t size){
    TimuiMpscNode *n;
    if(!q) return 0;
    n = (TimuiMpscNode *)q->alloc.alloc(q->alloc.userdata, sizeof(*n) + size);
    if(!n) return 0;
    n->next = NULL; n->type = type; n->size = size;
    if(size > 0 && data) memcpy(n->data, data, size);
    TIMUI_MPSC_LOCK(q);
    if(q->tail) q->tail->next = n; else q->head = n;
    q->tail = n;
    q->pending++;
    TIMUI_MPSC_UNLOCK(q);
    return 1;
}
TIMUI_API int timui_mpsc_recv(TimuiMpsc *q, uint32_t *out_type, void *out_buf, size_t *inout_size){
    TimuiMpscNode *n;
    size_t copy;
    if(!q) return 0;
    TIMUI_MPSC_LOCK(q);
    n = q->head;
    if(n){
        q->head = n->next;
        if(!q->head) q->tail = NULL;
        q->pending--;
    }
    TIMUI_MPSC_UNLOCK(q);
    if(!n) return 0;
    if(out_type) *out_type = n->type;
    if(inout_size){
        copy = *inout_size;
        if(copy > n->size) copy = n->size;
        if(out_buf && copy > 0) memcpy(out_buf, n->data, copy);
        *inout_size = n->size;
    }
    q->alloc.free(q->alloc.userdata, n, sizeof(*n) + n->size);
    return 1;
}
TIMUI_API int timui_mpsc_empty(TimuiMpsc *q){
    int e;
    if(!q) return 1;
    TIMUI_MPSC_LOCK(q);
    e = (q->pending == 0);
    TIMUI_MPSC_UNLOCK(q);
    return e;
}

/* ---- allocator + arena ------------------------------------------------- *
 * The default allocator wraps malloc/realloc/free. The arena is a bump
 * allocator: init reserves `cap` bytes, alloc hands out aligned slices and
 * returns NULL past the end, reset rewinds for the next frame, free returns
 * the backing buffer to the allocator. */
static void *def_alloc(void *ud, size_t sz){ (void)ud; return malloc(sz); }
static void *def_realloc(void *ud, void *p, size_t os, size_t ns){
    (void)ud; (void)os; return realloc(p, ns);
}
static void def_free(void *ud, void *p, size_t sz){ (void)ud; (void)sz; free(p); }

TIMUI_API TimuiAllocator timui_default_allocator(void){
    TimuiAllocator a;
    a.userdata = NULL;
    a.alloc    = def_alloc;
    a.realloc  = def_realloc;
    a.free     = def_free;
    return a;
}
TIMUI_API TimuiResult timui_arena_init(TimuiArena *a, const TimuiAllocator *alloc, size_t cap){
    if(!a || !alloc || cap == 0) return TIMUI_ERR_INVALID_ARGUMENT;
    a->alloc = alloc;
    a->cap   = cap;
    a->off   = 0;
    a->base  = (unsigned char *)alloc->alloc(alloc->userdata, cap);
    if(!a->base){ a->cap = 0; return TIMUI_ERR_OUT_OF_MEMORY; }
    return TIMUI_OK;
}
TIMUI_API void *timui_arena_alloc(TimuiArena *a, size_t size, size_t align){
    size_t mask, aligned;
    if(!a || align == 0) return NULL;
    if(size == 0) size = 1;
    mask    = align - 1;
    aligned = (a->off + mask) & ~mask;
    if(aligned < a->off) return NULL;             /* wraparound guard */
    if(aligned + size < aligned) return NULL;     /* wraparound guard */
    if(aligned + size > a->cap) return NULL;      /* out of memory */
    a->off = aligned + size;
    return a->base + aligned;
}
TIMUI_API void timui_arena_reset(TimuiArena *a){ if(a) a->off = 0; }
TIMUI_API void timui_arena_free(TimuiArena *a){
    if(!a || !a->base) return;
    a->alloc->free(a->alloc->userdata, a->base, a->cap);
    a->base = NULL; a->cap = 0; a->off = 0; a->alloc = NULL;
}

/* ---- rect layout (clamps to non-negative; never overflows the parent) -- */
TIMUI_API TimuiRect timui_cut_top(TimuiRect *r, int h){
    TimuiRect out;
    if(h < 0) h = 0;
    if(h > r->h) h = r->h;
    out.x = r->x; out.y = r->y; out.w = r->w; out.h = h;
    r->y += h; r->h -= h;
    return out;
}
TIMUI_API TimuiRect timui_cut_bottom(TimuiRect *r, int h){
    TimuiRect out;
    if(h < 0) h = 0;
    if(h > r->h) h = r->h;
    r->h -= h;
    out.x = r->x; out.y = r->y + r->h; out.w = r->w; out.h = h;
    return out;
}
TIMUI_API TimuiRect timui_cut_left(TimuiRect *r, int w){
    TimuiRect out;
    if(w < 0) w = 0;
    if(w > r->w) w = r->w;
    out.x = r->x; out.y = r->y; out.w = w; out.h = r->h;
    r->x += w; r->w -= w;
    return out;
}
TIMUI_API TimuiRect timui_cut_right(TimuiRect *r, int w){
    TimuiRect out;
    if(w < 0) w = 0;
    if(w > r->w) w = r->w;
    r->w -= w;
    out.x = r->x + r->w; out.y = r->y; out.w = w; out.h = r->h;
    return out;
}
TIMUI_API TimuiRect timui_inset(TimuiRect r, int n){
    if(n < 0) n = 0;
    r.x += n; r.y += n;
    r.w -= 2 * n; r.h -= 2 * n;
    if(r.w < 0) r.w = 0;
    if(r.h < 0) r.h = 0;
    return r;
}
TIMUI_API TimuiRect timui_pad(TimuiRect r, int l, int t, int rr, int b){
    if(l < 0) l = 0; if(t < 0) t = 0; if(rr < 0) rr = 0; if(b < 0) b = 0;
    r.x += l; r.y += t;
    r.w -= (l + rr); r.h -= (t + b);
    if(r.w < 0) r.w = 0;
    if(r.h < 0) r.h = 0;
    return r;
}
TIMUI_API void timui_split_cols(TimuiRect r, float ratio, TimuiRect *a, TimuiRect *b){
    int aw;
    if(ratio < 0.0f) ratio = 0.0f;
    if(ratio > 1.0f) ratio = 1.0f;
    aw = (int)(r.w * ratio);
    if(a){ a->x = r.x;      a->y = r.y; a->w = aw;       a->h = r.h; }
    if(b){ b->x = r.x + aw; b->y = r.y; b->w = r.w - aw; b->h = r.h; }
}
TIMUI_API void timui_split_rows(TimuiRect r, float ratio, TimuiRect *a, TimuiRect *b){
    int ah;
    if(ratio < 0.0f) ratio = 0.0f;
    if(ratio > 1.0f) ratio = 1.0f;
    ah = (int)(r.h * ratio);
    if(a){ a->x = r.x; a->y = r.y;      a->w = r.w; a->h = ah; }
    if(b){ b->x = r.x; b->y = r.y + ah; b->w = r.w; b->h = r.h - ah; }
}

/* ---- terminal transport + fake backend --------------------------------- */
static int fake_write(TimuiTransport *t, const void *data, size_t len){
    TimuiFakeTransport *f = (TimuiFakeTransport *)t->ctx;
    if(f->out_len + len > f->out_cap){
        size_t ncap = f->out_cap ? f->out_cap : 64;
        unsigned char *nb;
        while(ncap < f->out_len + len) ncap *= 2;
        nb = (unsigned char *)f->alloc.realloc(f->alloc.userdata, f->out, f->out_cap, ncap);
        if(!nb) return -1;
        f->out = nb;
        f->out_cap = ncap;
    }
    memcpy(f->out + f->out_len, data, len);
    f->out_len += len;
    return (int)len;
}
static int fake_read(TimuiTransport *t, void *buf, size_t cap){
    TimuiFakeTransport *f = (TimuiFakeTransport *)t->ctx;
    size_t avail = f->in_len - f->in_pos;
    size_t n = avail < cap ? avail : cap;
    if(n == 0) return 0;
    memcpy(buf, f->in + f->in_pos, n);
    f->in_pos += n;
    return (int)n;
}
static int fake_flush(TimuiTransport *t){ (void)t; return 0; }
static void fake_close(TimuiTransport *t){ (void)t; }

TIMUI_API TimuiResult timui_fake_init(TimuiFakeTransport *f, const TimuiAllocator *alloc){
    if(!f || !alloc) return TIMUI_ERR_INVALID_ARGUMENT;
    f->alloc = *alloc;
    f->out = NULL; f->out_cap = 0; f->out_len = 0;
    f->in = NULL;  f->in_len = 0;  f->in_pos = 0;
    return TIMUI_OK;
}
TIMUI_API void timui_fake_destroy(TimuiFakeTransport *f){
    if(!f) return;
    if(f->out) f->alloc.free(f->alloc.userdata, f->out, f->out_cap);
    f->out = NULL; f->out_cap = 0; f->out_len = 0;
    f->in = NULL;  f->in_len = 0;  f->in_pos = 0;
}
TIMUI_API void timui_fake_set_input(TimuiFakeTransport *f, const void *bytes, size_t len){
    if(!f) return;
    f->in = (const unsigned char *)bytes;
    f->in_len = len;
    f->in_pos = 0;
}
TIMUI_API TimuiStr timui_fake_output(const TimuiFakeTransport *f){
    TimuiStr s;
    if(!f){ s.ptr = NULL; s.len = 0; return s; }
    s.ptr = (const char *)f->out;
    s.len = f->out_len;
    return s;
}
TIMUI_API void timui_fake_clear_output(TimuiFakeTransport *f){
    if(f) f->out_len = 0;
}
TIMUI_API TimuiTransport timui_fake_transport(TimuiFakeTransport *f){
    TimuiTransport t;
    t.write = fake_write;
    t.read  = fake_read;
    t.flush = fake_flush;
    t.close = fake_close;
    t.ctx   = f;
    return t;
}

/* ---- screen mode setup/teardown ---------------------------------------- *
 * Emit DEC/private-mode escapes through the transport. enter() records the
 * enabled flags so exit() can emit the matching resets in reverse order. */
static void emit_lit(TimuiTransport *t, const char *s, size_t n){
    if(t && t->write) (void)t->write(t, s, n);
}
#define TIMUI_EMIT(t, lit) emit_lit((t), (lit), sizeof(lit) - 1)

TIMUI_API void timui_screen_enter(TimuiTransport *t, TimuiScreenMode *m, uint32_t flags, TimuiStr title){
    if(m) m->flags = flags;
    if(title.ptr && title.len){
        TIMUI_EMIT(t, "\x1b]0;");                       /* OSC 0 ; */
        if(t && t->write) (void)t->write(t, title.ptr, title.len);
        TIMUI_EMIT(t, "\x07");                          /* BEL */
    }
    if(flags & TIMUI_FLAG_ALT_SCREEN)      TIMUI_EMIT(t, "\x1b[?1049h");
    if(flags & TIMUI_FLAG_HIDE_CURSOR)     TIMUI_EMIT(t, "\x1b[?25l");
    if(flags & TIMUI_FLAG_MOUSE){          TIMUI_EMIT(t, "\x1b[?1000h"); TIMUI_EMIT(t, "\x1b[?1006h"); }
    if(flags & TIMUI_FLAG_BRACKETED_PASTE) TIMUI_EMIT(t, "\x1b[?2004h");
    if(flags & TIMUI_FLAG_FOCUS_EVENTS)    TIMUI_EMIT(t, "\x1b[?1004h");
}
TIMUI_API void timui_screen_exit(TimuiTransport *t, TimuiScreenMode *m){
    uint32_t flags = m ? m->flags : 0;
    if(flags & TIMUI_FLAG_FOCUS_EVENTS)    TIMUI_EMIT(t, "\x1b[?1004l");
    if(flags & TIMUI_FLAG_BRACKETED_PASTE) TIMUI_EMIT(t, "\x1b[?2004l");
    if(flags & TIMUI_FLAG_MOUSE){          TIMUI_EMIT(t, "\x1b[?1006l"); TIMUI_EMIT(t, "\x1b[?1000l"); }
    if(flags & TIMUI_FLAG_HIDE_CURSOR)     TIMUI_EMIT(t, "\x1b[?25h");
    if(flags & TIMUI_FLAG_ALT_SCREEN)      TIMUI_EMIT(t, "\x1b[?1049l");
}

/* ---- terminal raw mode (POSIX) ---------------------------------------- */
TIMUI_API TimuiResult timui_termios_enter(TimuiTermios *t, int fd){
    struct termios *orig, raw;
    if(!t) return TIMUI_ERR_INVALID_ARGUMENT;
    orig = (struct termios *)malloc(sizeof(struct termios));
    if(!orig) return TIMUI_ERR_OUT_OF_MEMORY;
    if(tcgetattr(fd, orig) != 0){ free(orig); return TIMUI_ERR_OS; }
    t->fd = fd;
    t->saved = orig;
    t->have_saved = 1;
    raw = *orig;
    raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    raw.c_oflag &= ~OPOST;
    raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= ~(CSIZE | PARENB);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    if(tcsetattr(fd, TCSAFLUSH, &raw) != 0) return TIMUI_ERR_OS;
    return TIMUI_OK;
}
TIMUI_API TimuiResult timui_termios_restore(TimuiTermios *t){
    if(!t || !t->have_saved) return TIMUI_ERR_INVALID_ARGUMENT;
    if(tcsetattr(t->fd, TCSAFLUSH, (const struct termios *)t->saved) != 0) return TIMUI_ERR_OS;
    return TIMUI_OK;
}
TIMUI_API void timui_termios_destroy(TimuiTermios *t){
    if(!t) return;
    if(t->saved){ free(t->saved); t->saved = NULL; }
    t->have_saved = 0;
}
TIMUI_API TimuiResult timui_term_size(int fd, int *out_w, int *out_h){
    struct winsize ws;
    if(ioctl(fd, TIOCGWINSZ, &ws) != 0){
        return (errno == ENOTTY) ? TIMUI_ERR_NOT_A_TTY : TIMUI_ERR_OS;
    }
    if(out_w) *out_w = (int)ws.ws_col;
    if(out_h) *out_h = (int)ws.ws_row;
    return TIMUI_OK;
}

/* ---- capability detection --------------------------------------------- */
static int caps_streq(const char *a, const char *b){
    return a && b && strcmp(a, b) == 0;
}
static int caps_is_kitty_family(const char *tp){
    return caps_streq(tp, "kitty") || caps_streq(tp, "xterm-kitty")
        || caps_streq(tp, "ghostty") || caps_streq(tp, "xterm-ghostty");
}
static int caps_is_modern(const char *tp){
    return caps_is_kitty_family(tp) || caps_streq(tp, "WezTerm")
        || caps_streq(tp, "alacritty") || caps_streq(tp, "foot")
        || caps_streq(tp, "rio");
}
static void caps_set_str(char *dst, size_t cap, const char *src){
    size_t n;
    if(!dst) return;
    dst[0] = '\0';
    if(!src) return;
    n = strlen(src);
    if(n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}
TIMUI_API void timui_caps_detect(TimuiCaps *c, const char *term, const char *term_program, const char *colorterm){
    if(!c) return;
    memset(c, 0, sizeof(*c));
    c->colors = 16;
    caps_set_str(c->term, sizeof(c->term), term);
    caps_set_str(c->term_program, sizeof(c->term_program), term_program);
    if(colorterm && (caps_streq(colorterm, "truecolor") || caps_streq(colorterm, "24bit"))){
        c->flags |= TIMUI_CAP_TRUECOLOR;
        c->colors = 16777216;
    }
    if(caps_is_modern(term_program)){
        c->flags |= TIMUI_CAP_TRUECOLOR | TIMUI_CAP_256_COLOR | TIMUI_CAP_SGR_MOUSE
                  | TIMUI_CAP_BRACKETED_PASTE | TIMUI_CAP_FOCUS_EVENTS
                  | TIMUI_CAP_SYNC_OUTPUT | TIMUI_CAP_OSC8_HYPERLINKS;
        if(c->colors < 16777216) c->colors = 16777216;
        if(caps_is_kitty_family(term_program)){
            c->flags |= TIMUI_CAP_KITTY_KEYBOARD | TIMUI_CAP_KITTY_GRAPHICS | TIMUI_CAP_UNICODE_CORE;
        }
    } else if(term && strstr(term, "256color")){
        c->flags |= TIMUI_CAP_256_COLOR;
        c->colors = 256;
    }
    /* multiplexers reduce capabilities unless explicit passthrough is known */
    if(term && (strstr(term, "tmux") || strstr(term, "screen") || strstr(term, "zellij"))){
        c->flags &= ~(TIMUI_CAP_KITTY_KEYBOARD | TIMUI_CAP_KITTY_GRAPHICS | TIMUI_CAP_SYNC_OUTPUT);
        c->flags |= TIMUI_CAP_256_COLOR;
        if(c->colors < 256) c->colors = 256;
    }
}
TIMUI_API void timui_caps_apply_force(TimuiCaps *c, uint32_t force_on, uint32_t force_off){
    if(!c) return;
    c->flags |= force_on;
    c->flags &= ~force_off;
}
TIMUI_API int timui_caps_has(const TimuiCaps *c, TimuiCapFlags cap){
    return c && ((c->flags & (uint32_t)cap) != 0);
}

/* ---- synchronized output (DEC 2026) + cursor -------------------------- */
TIMUI_API void timui_sync_begin(TimuiTransport *t){ TIMUI_EMIT(t, "\x1b[?2026h"); }
TIMUI_API void timui_sync_end(TimuiTransport *t){ TIMUI_EMIT(t, "\x1b[?2026l"); }
TIMUI_API void timui_hide_cursor(TimuiTransport *t){ TIMUI_EMIT(t, "\x1b[?25l"); }
TIMUI_API void timui_show_cursor(TimuiTransport *t){ TIMUI_EMIT(t, "\x1b[?25h"); }

/* ---- input parser ------------------------------------------------------ *
 * Incremental byte->event state machine: ground/esc/csi/ss3/utf8. Emits a
 * TimuiEvent through cb for each complete key or text rune; invalid bytes
 * become U+FFFD rather than crashing. */
static void emit_key(TimuiEventFn cb, void *ctx, TimuiKey k, uint32_t mods, uint32_t cp){
    TimuiEvent ev;
    ev.kind = TIMUI_EVENT_KEY;
    ev.as.key.key = k;
    ev.as.key.codepoint = cp;
    ev.as.key.mods = mods;
    ev.as.key.action = TIMUI_KEY_PRESS;
    if(cb) cb(ctx, &ev);
}
static void emit_text(TimuiEventFn cb, void *ctx, const char *ptr, size_t len, uint32_t cp){
    TimuiEvent ev;
    ev.kind = TIMUI_EVENT_TEXT;
    ev.as.text.ptr = ptr;
    ev.as.text.len = len;
    ev.as.text.codepoint = cp;
    if(cb) cb(ctx, &ev);
}
static void emit_paste(TimuiEventFn cb, void *ctx, const unsigned char *ptr, size_t len){
    TimuiEvent ev;
    ev.kind = TIMUI_EVENT_PASTE;
    ev.as.paste.ptr = (const char *)ptr;
    ev.as.paste.len = len;
    if(cb) cb(ctx, &ev);
}
static void emit_focus(TimuiEventFn cb, void *ctx, int focused){
    TimuiEvent ev;
    ev.kind = TIMUI_EVENT_FOCUS;
    ev.as.focus.focused = focused;
    if(cb) cb(ctx, &ev);
}
/* SGR mouse: mp = {Cb, x, y}; final 'M' press, 'm' release. Cb encodes
 * button, modifiers, motion (0x20) and wheel (0x40). */
static void emit_mouse(TimuiEventFn cb, void *ctx, const int *mp, unsigned char final){
    TimuiEvent ev;
    int code = mp[0];
    ev.kind = TIMUI_EVENT_MOUSE;
    ev.as.mouse.x = mp[1];
    ev.as.mouse.y = mp[2];
    ev.as.mouse.button = -1;
    ev.as.mouse.wheel_y = 0;
    ev.as.mouse.mods = 0;
    ev.as.mouse.pressed  = (final == 'M');
    ev.as.mouse.released = (final == 'm');
    ev.as.mouse.motion = 0;
    if(code & 0x40){
        ev.as.mouse.wheel_y = (code == 64) ? 1 : (code == 65 ? -1 : 0);
    } else {
        ev.as.mouse.button = code & 0x03;
        ev.as.mouse.motion = (code & 0x20) ? 1 : 0;
    }
    if(code & 0x04) ev.as.mouse.mods |= TIMUI_MOD_SHIFT;
    if(code & 0x08) ev.as.mouse.mods |= TIMUI_MOD_ALT;
    if(code & 0x10) ev.as.mouse.mods |= TIMUI_MOD_CTRL;
    if(cb) cb(ctx, &ev);
}
static TimuiKey csi_letter(unsigned char f){
    switch(f){
        case 'A': return TIMUI_KEY_UP;
        case 'B': return TIMUI_KEY_DOWN;
        case 'C': return TIMUI_KEY_RIGHT;
        case 'D': return TIMUI_KEY_LEFT;
        case 'H': return TIMUI_KEY_HOME;
        case 'F': return TIMUI_KEY_END;
        default:  return TIMUI_KEY_UNKNOWN;
    }
}
static TimuiKey csi_tilde(int n){
    switch(n){
        case 1: case 7: return TIMUI_KEY_HOME;
        case 4: case 8: return TIMUI_KEY_END;
        case 2:  return TIMUI_KEY_INSERT;
        case 3:  return TIMUI_KEY_DELETE;
        case 5:  return TIMUI_KEY_PAGE_UP;
        case 6:  return TIMUI_KEY_PAGE_DOWN;
        case 15: return TIMUI_KEY_F5;
        case 17: return TIMUI_KEY_F6;
        case 18: return TIMUI_KEY_F7;
        case 19: return TIMUI_KEY_F8;
        case 20: return TIMUI_KEY_F9;
        case 21: return TIMUI_KEY_F10;
        case 23: return TIMUI_KEY_F11;
        case 24: return TIMUI_KEY_F12;
        default: return TIMUI_KEY_UNKNOWN;
    }
}
static TimuiKey ss3_final(unsigned char f){
    switch(f){
        case 'P': return TIMUI_KEY_F1;
        case 'Q': return TIMUI_KEY_F2;
        case 'R': return TIMUI_KEY_F3;
        case 'S': return TIMUI_KEY_F4;
        case 'H': return TIMUI_KEY_HOME;
        case 'F': return TIMUI_KEY_END;
        default:  return TIMUI_KEY_UNKNOWN;
    }
}
/* Kitty keyboard: CSI <code>;<mods>u -- map well-known codes, decode mods
 * (value = 1 + bitmask: shift/alt/ctrl/super/hyper/meta). */
static TimuiKey kitty_code_key(int code){
    switch(code){
        case 9:   return TIMUI_KEY_TAB;
        case 13:  return TIMUI_KEY_ENTER;
        case 27:  return TIMUI_KEY_ESCAPE;
        case 127: return TIMUI_KEY_BACKSPACE;
        default:  return TIMUI_KEY_UNKNOWN;
    }
}
static uint32_t decode_kitty_mods(int param){
    uint32_t mods = TIMUI_MOD_NONE;
    int m = param > 0 ? param - 1 : 0;
    if(m & 1)  mods |= TIMUI_MOD_SHIFT;
    if(m & 2)  mods |= TIMUI_MOD_ALT;
    if(m & 4)  mods |= TIMUI_MOD_CTRL;
    if(m & 8)  mods |= TIMUI_MOD_SUPER;
    if(m & 16) mods |= TIMUI_MOD_HYPER;
    if(m & 32) mods |= TIMUI_MOD_META;
    return mods;
}
/* UTF-8 lead byte: continuation count (1..3), or -1 if not a valid lead. */
static int utf8_lead(unsigned char b, uint32_t *cp){
    if((b & 0xE0) == 0xC0){ *cp = (uint32_t)(b & 0x1F); return 1; }
    if((b & 0xF0) == 0xE0){ *cp = (uint32_t)(b & 0x0F); return 2; }
    if((b & 0xF8) == 0xF0){ *cp = (uint32_t)(b & 0x07); return 3; }
    return -1;
}
TIMUI_API void timui_input_init(TimuiInputParser *p){
    if(!p) return;
    p->state = 0; p->param = 0; p->nparams = 0;
    p->mod_param = 0; p->has_mod = 0;
    p->csi_mouse = 0; p->mcount = 0;
    p->mparam[0] = p->mparam[1] = p->mparam[2] = 0;
    p->pasting = 0; p->paste_ptr = NULL;
    p->utf8_need = 0; p->utf8_len = 0; p->utf8_cp = 0; p->utf8_ptr = NULL;
}
TIMUI_API size_t timui_input_feed(TimuiInputParser *p, const void *data, size_t len,
                                  TimuiEventFn cb, void *ctx){
    const unsigned char *b = (const unsigned char *)data;
    size_t i, count = 0;
    if(!p || !b) return 0;
    if(p->state == 4) p->utf8_ptr = NULL;   /* crossed a feed boundary: no stable byte view */
    if(p->pasting) p->paste_ptr = (const unsigned char *)&b[0];   /* paste continues into this feed */
    for(i = 0; i < len; i++){
        unsigned char c = b[i];
        if(p->pasting){
            /* scan for the ESC[201~ terminator; anything else is paste content */
            if(c == 0x1b && i + 5 < len &&
               b[i+1] == '[' && b[i+2] == '2' && b[i+3] == '0' && b[i+4] == '1' && b[i+5] == '~'){
                emit_paste(cb, ctx, p->paste_ptr, (size_t)(&b[i] - p->paste_ptr));
                count++;
                p->pasting = 0;
                i += 5;                 /* consume the 6-byte terminator */
            }
            continue;
        }
        switch(p->state){
        case 0: /* GROUND */
            if(c == 0x1b){ p->state = 1; break; }
            if(c == '\r' || c == '\n'){ emit_key(cb, ctx, TIMUI_KEY_ENTER, 0, 0); count++; break; }
            if(c == '\t'){ emit_key(cb, ctx, TIMUI_KEY_TAB, 0, 0); count++; break; }
            if(c == 0x7f || c == 0x08){ emit_key(cb, ctx, TIMUI_KEY_BACKSPACE, 0, 0); count++; break; }
            if(c < 0x20){
                uint32_t cp = (c >= 1 && c <= 26) ? (uint32_t)('a' + c - 1) : (uint32_t)c;
                emit_key(cb, ctx, TIMUI_KEY_UNKNOWN, TIMUI_MOD_CTRL, cp);
                count++; break;
            }
            if(c < 0x80){
                emit_text(cb, ctx, (const char *)&b[i], 1, (uint32_t)c);
                count++; break;
            }
            {   /* UTF-8 multibyte lead (c >= 0x80) */
                uint32_t cp = 0;
                int need = utf8_lead(c, &cp);
                if(need < 0){ emit_text(cb, ctx, (const char *)&b[i], 1, 0xFFFD); count++; break; }
                p->utf8_cp = cp; p->utf8_need = need; p->utf8_len = need + 1;
                p->utf8_ptr = (const char *)&b[i];
                p->state = 4;
            }
            break;
        case 1: /* ESC */
            if(c == '['){
                p->state = 2; p->param = 0; p->nparams = 0;
                p->mod_param = 0; p->has_mod = 0;
                p->csi_mouse = 0; p->mcount = 0;
                p->mparam[0] = p->mparam[1] = p->mparam[2] = 0;
                break;
            }
            if(c == 'O'){ p->state = 3; break; }
            if(c == 0x1b){ emit_key(cb, ctx, TIMUI_KEY_ESCAPE, 0, 0); count++; break; } /* stay ESC */
            if(c >= 0x20 && c < 0x80){
                emit_key(cb, ctx, TIMUI_KEY_UNKNOWN, TIMUI_MOD_ALT, (uint32_t)c);
                count++; p->state = 0; break;
            }
            emit_key(cb, ctx, TIMUI_KEY_ESCAPE, 0, 0); count++;
            p->state = 0;
            if(i > 0) i--;        /* reprocess the byte in ground */
            break;
        case 2: /* CSI */
            if(c == '<'){ p->csi_mouse = 1; p->mcount = 0; p->mparam[0] = p->mparam[1] = p->mparam[2] = 0; break; }
            if(c == '?' || c == '>' || c == '='){ break; }              /* private marker */
            if(c >= '0' && c <= '9'){
                if(p->csi_mouse){
                    if(p->mcount < 3) p->mparam[p->mcount] = p->mparam[p->mcount] * 10 + (c - '0');
                } else if(p->has_mod){
                    p->mod_param = p->mod_param * 10 + (c - '0');
                } else { p->param = p->param * 10 + (c - '0'); p->nparams = 1; }
                break;
            }
            if(c == ';'){
                if(p->csi_mouse){ if(p->mcount < 2) p->mcount++; }
                else { p->has_mod = 1; p->mod_param = 0; }
                break;
            }
            if(c >= 0x40 && c <= 0x7e){
                uint32_t mods = p->has_mod ? decode_kitty_mods(p->mod_param) : 0;
                if(p->csi_mouse){
                    if(c == 'M' || c == 'm'){ emit_mouse(cb, ctx, p->mparam, c); count++; }
                    p->csi_mouse = 0;
                } else if(c == '~'){
                    int n = p->nparams ? p->param : 0;
                    if(n == 200){ p->pasting = 1; p->paste_ptr = (const unsigned char *)&b[i+1]; }
                    else if(n == 201){ p->pasting = 0; }
                    else { TimuiKey k = csi_tilde(n); if(k != TIMUI_KEY_UNKNOWN){ emit_key(cb, ctx, k, mods, 0); count++; } }
                } else if(c == 'u'){
                    /* Kitty keyboard: CSI <code>;<mods>u */
                    int code = p->nparams ? p->param : 0;
                    emit_key(cb, ctx, kitty_code_key(code), mods, (uint32_t)code);
                    count++;
                } else if(c == 'I'){ emit_focus(cb, ctx, 1); count++; }
                else if(c == 'O'){ emit_focus(cb, ctx, 0); count++; }
                else { TimuiKey k = csi_letter(c); if(k != TIMUI_KEY_UNKNOWN){ emit_key(cb, ctx, k, mods, 0); count++; } }
                p->state = 0;
                break;
            }
            p->state = 0;          /* unexpected: resync */
            break;
        case 3: /* SS3 (ESC O X) */
            {
                TimuiKey k = ss3_final(c);
                if(k != TIMUI_KEY_UNKNOWN){ emit_key(cb, ctx, k, 0, 0); count++; }
                p->state = 0;
            }
            break;
        case 4: /* UTF-8 continuation */
            if((c & 0xC0) == 0x80){
                p->utf8_cp = (p->utf8_cp << 6) | (uint32_t)(c & 0x3F);
                p->utf8_need--;
                if(p->utf8_need == 0){
                    emit_text(cb, ctx, p->utf8_ptr, p->utf8_ptr ? (size_t)p->utf8_len : 0, p->utf8_cp);
                    count++; p->state = 0;
                }
                break;
            }
            emit_text(cb, ctx, (const char *)&b[i], 1, 0xFFFD);   /* invalid continuation */
            count++; p->state = 0;
            if(i > 0) i--;
            break;
        }
    }
    if(p->pasting){   /* paste ran to end of feed: emit the chunk accumulated so far */
        size_t plen = (size_t)(&b[len] - p->paste_ptr);
        if(plen > 0){ emit_paste(cb, ctx, p->paste_ptr, plen); count++; }
    }
    return count;
}

#endif /* TIMUI_IMPLEMENTATION */
