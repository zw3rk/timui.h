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
typedef struct Timui          Timui;            /* runtime: caps, modes, buffers    */
typedef struct TimuiFrame     TimuiFrame;       /* per-frame: valid only begin..end */
typedef struct TimuiTransport TimuiTransport;   /* forward (used by renderer) */
typedef struct TimuiCellBuffer TimuiCellBuffer; /* forward (used by lifecycle) */
typedef struct TimuiEvent     TimuiEvent;       /* forward (used by lifecycle) */

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
TIMUI_API TimuiCellBuffer *timui_frame_buffer(TimuiFrame *frame);
TIMUI_API void             timui_ui_resize(Timui *ui, int w, int h);
TIMUI_API int              timui_poll_event(Timui *ui, TimuiEvent *out_event);
TIMUI_API void             timui_quit(Timui *ui);
TIMUI_API bool             timui_should_quit(const Timui *ui);
/* Test constructor: a Timui backed by an injected transport + fixed size
 * (no tty), so the frame/render path is unit-testable without a terminal. */
TIMUI_API TimuiResult      timui_open_for_test(Timui **out_ui, TimuiTransport transport,
                                               int w, int h, const TimuiAllocator *alloc);

/* ---- Widgets (immediate-mode; controlled default + _mut convenience) -- */
typedef struct {
    bool clicked;
    bool pressed;
    bool hovered;
    bool focused;
} TimuiButtonResult;

TIMUI_API TimuiButtonResult timui_button(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr label);

typedef struct {
    bool changed;
    bool value;
    bool hovered;
    bool focused;
} TimuiBoolEdit;

TIMUI_API void       timui_label(TimuiFrame *f, int x, int y, TimuiStr text, TimuiStyle style);
TIMUI_API TimuiRect  timui_panel_begin(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr title, uint32_t border_flags);
TIMUI_API void       timui_panel_end(TimuiFrame *f);
TIMUI_API TimuiBoolEdit timui_checkbox(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr label, bool value);
TIMUI_API bool       timui_checkbox_mut(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr label, bool *value);
TIMUI_API TimuiBoolEdit timui_radio(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr label, bool selected);
TIMUI_API void       timui_function_bar(TimuiFrame *f, TimuiRect r, TimuiStr text);

/* Cursor/edit key flags accumulated per frame for the focused input. */
#define TIMUI_KEYIN_BACKSPACE 1u
#define TIMUI_KEYIN_LEFT      2u
#define TIMUI_KEYIN_RIGHT     4u
#define TIMUI_KEYIN_HOME      8u
#define TIMUI_KEYIN_END       16u
#define TIMUI_KEYIN_DELETE    32u
#define TIMUI_KEYIN_UP        64u
#define TIMUI_KEYIN_DOWN      128u

/* Mutable single-line input: click to focus, type to append (bounded by cap),
 * backspace deletes the last rune, Enter submits. Returns true on submit. */
TIMUI_API bool       timui_input_line_buf(TimuiFrame *f, TimuiId id, TimuiRect r, char *buf, size_t cap);

typedef struct { int selected; int scroll; } TimuiListState;
typedef const char *(*TimuiLabelFn)(void *userdata, int index);
typedef struct {
    int state_changed;
    int activated;
    int focused;
    TimuiListState state;
    int selected;
} TimuiListResult;

TIMUI_API TimuiListResult timui_listbox(TimuiFrame *f, TimuiId id, TimuiRect r,
                                        TimuiListState state, int count, TimuiLabelFn label, void *userdata);
TIMUI_API TimuiListResult timui_listbox_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
                                            TimuiListState *state, int count, TimuiLabelFn label, void *userdata);

typedef enum {
    TIMUI_DIALOG_NONE = 0,
    TIMUI_DIALOG_OK,
    TIMUI_DIALOG_CANCEL,
    TIMUI_DIALOG_YES,
    TIMUI_DIALOG_NO
} TimuiDialogResult;

/* Centered message box within `parent`. Returns the index of the clicked
 * button (0..count-1), or -1 if none this frame. */
TIMUI_API int timui_message_box(TimuiFrame *f, TimuiId id, TimuiRect parent,
                                TimuiStr title, TimuiStr message,
                                const TimuiStr *buttons, int count);

/* ---- Menu bar + popups (T5.7) ----------------------------------------- */
TIMUI_API void timui_menu_bar_begin(TimuiFrame *f, TimuiRect r);
TIMUI_API int  timui_menu_begin(TimuiFrame *f, TimuiId id, TimuiStr label);   /* 1 if open */
TIMUI_API int  timui_menu_item(TimuiFrame *f, TimuiId id, TimuiStr label);    /* 1 if clicked */
TIMUI_API void timui_menu_end(TimuiFrame *f);
TIMUI_API void timui_menu_bar_end(TimuiFrame *f);                             /* outside-click closes */

/* ---- Optional functional runner (T6) ---------------------------------- *
 * view() describes the frame from an immutable model; update() is the only
 * place the model changes. The runner drains UI-thread messages (timui_emit)
 * into update between frames. The manual begin/end loop stays supported. */
typedef void (*TimuiViewFn)(TimuiFrame *f, void *model);
typedef void (*TimuiUpdateFn)(void *model, uint32_t msg_type, const void *msg, size_t msg_size);
typedef struct {
    void         *model;
    TimuiViewFn   view;
    TimuiUpdateFn update;
} TimuiApp;

TIMUI_API int  timui_run(const TimuiConfig *cfg, TimuiApp *app);
TIMUI_API bool timui_emit(TimuiFrame *f, uint32_t type, const void *data, size_t size);
TIMUI_API bool timui_recv(Timui *ui, uint32_t *out_type, void *out_buf, size_t *inout_size);
TIMUI_API bool timui_post(Timui *ui, uint32_t type, const void *data, size_t size);   /* thread-safe */
TIMUI_API void timui_frame_quit(TimuiFrame *f);

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

/* ---- Cell buffer (rendering surface) ---------------------------------- */
typedef enum {
    TIMUI_CELL_EMPTY        = 0,
    TIMUI_CELL_CONTINUATION = 1u << 0,
    TIMUI_CELL_DIRTY        = 1u << 1,
    TIMUI_CELL_WIDE         = 1u << 2,
    TIMUI_CELL_IMAGE        = 1u << 3,
    TIMUI_CELL_LINK         = 1u << 4
} TimuiCellFlags;

typedef struct {
    uint32_t codepoint;
    uint32_t fg;
    uint32_t bg;
    uint32_t attrs;
    uint16_t width;
    uint16_t flags;
    uint32_t hyperlink_id;
    uint32_t image_id;
} TimuiCell;

struct TimuiCellBuffer {
    TimuiCell    *cells;
    int           w;
    int           h;
    TimuiAllocator alloc;   /* owning allocator (copied) */
    TimuiRect     clip;     /* active clip rect when has_clip */
    int           has_clip;
};
TIMUI_API void timui_push_clip(TimuiFrame *f, TimuiRect rect);
TIMUI_API void timui_pop_clip(TimuiFrame *f);

TIMUI_API TimuiResult timui_cells_init(TimuiCellBuffer *buf, int w, int h, const TimuiAllocator *alloc);
TIMUI_API void        timui_cells_destroy(TimuiCellBuffer *buf);
TIMUI_API TimuiResult timui_cells_resize(TimuiCellBuffer *buf, int w, int h, const TimuiAllocator *alloc);
TIMUI_API void        timui_cells_clear(TimuiCellBuffer *buf);
TIMUI_API TimuiCell  *timui_cells_get(TimuiCellBuffer *buf, int x, int y);
TIMUI_API int         timui_cells_put(TimuiCellBuffer *buf, int x, int y, const TimuiCell *cell);

/* ---- UTF-8 decode + display width (minimal v0.1) ---------------------- *
 * timui_utf8_decode returns the byte length of the next codepoint (1..4),
 * 0 if the input is incomplete, or 1 with *out_cp=U+FFFD on an invalid byte.
 * timui_utf8_width is a minimal wcwidth: control/combining -> 0, CJK/
 * fullwidth -> 2, box-drawing/printable -> 1. (Generated tables are v0.2.) */
TIMUI_API int timui_utf8_decode(const char *s, size_t len, uint32_t *out_cp);
TIMUI_API int timui_utf8_width(uint32_t cp);

/* ---- Drawing primitives (into the cell buffer) ------------------------ */
typedef enum {
    TIMUI_BORDER_NONE   = 0,
    TIMUI_BORDER_SINGLE = 1u << 0,
    TIMUI_BORDER_DOUBLE = 1u << 1,
    TIMUI_BORDER_ROUND  = 1u << 2,
    TIMUI_BORDER_ASCII  = 1u << 3,
    TIMUI_BORDER_SHADOW = 1u << 4
} TimuiBorderFlags;

TIMUI_API TimuiStyle timui_style_make(uint32_t fg, uint32_t bg, uint32_t attrs);
TIMUI_API void       timui_draw_text(TimuiCellBuffer *buf, int x, int y, TimuiStr text, TimuiStyle st);
TIMUI_API void       timui_draw_fill(TimuiCellBuffer *buf, TimuiRect r, TimuiStyle st);
TIMUI_API void       timui_draw_hline(TimuiCellBuffer *buf, int x, int y, int w, TimuiStyle st);
TIMUI_API void       timui_draw_vline(TimuiCellBuffer *buf, int x, int y, int h, TimuiStyle st);
TIMUI_API void       timui_draw_box(TimuiCellBuffer *buf, TimuiRect r, uint32_t border_flags, TimuiStyle st);

/* ---- Text attributes + diff renderer ---------------------------------- */
typedef enum {
    TIMUI_ATTR_NONE      = 0,
    TIMUI_ATTR_BOLD      = 1u << 0,
    TIMUI_ATTR_DIM       = 1u << 1,
    TIMUI_ATTR_ITALIC    = 1u << 2,
    TIMUI_ATTR_UNDERLINE = 1u << 3,
    TIMUI_ATTR_REVERSE   = 1u << 4,
    TIMUI_ATTR_BLINK     = 1u << 5,
    TIMUI_ATTR_STRIKE    = 1u << 6
} TimuiAttrs;

typedef struct {
    int last_x, last_y;                 /* last written cell (0-based); -1 = none */
    int last_fg, last_bg, last_attrs;   /* -1 = not yet emitted this run */
} TimuiRenderer;

TIMUI_API void timui_renderer_reset(TimuiRenderer *r);
/* Diff prev vs curr and emit the minimal terminal update (CUP + truecolor SGR
 * + glyph) through t; unchanged cells produce no output. */
TIMUI_API void timui_render_diff(TimuiTransport *t, const TimuiCellBuffer *prev,
                                 const TimuiCellBuffer *curr, TimuiRenderer *r);
/* After a frame: position the logical cursor (for text input) and show it,
 * or hide it. Call after timui_render_diff. */
TIMUI_API void timui_render_cursor(TimuiTransport *t, int x, int y, int visible);

/* ---- Style/theme system ---------------------------------------------- */
typedef enum {
    TIMUI_SLOT_TEXT = 0, TIMUI_SLOT_TEXT_DIM, TIMUI_SLOT_PANEL, TIMUI_SLOT_PANEL_TITLE,
    TIMUI_SLOT_BORDER, TIMUI_SLOT_BUTTON, TIMUI_SLOT_BUTTON_HOVERED,
    TIMUI_SLOT_BUTTON_FOCUSED, TIMUI_SLOT_BUTTON_ACTIVE, TIMUI_SLOT_INPUT,
    TIMUI_SLOT_INPUT_FOCUSED, TIMUI_SLOT_SELECTION, TIMUI_SLOT_MENU,
    TIMUI_SLOT_MENU_ACTIVE, TIMUI_SLOT_STATUS, TIMUI_SLOT_ERROR,
    TIMUI_SLOT_WARNING, TIMUI_SLOT_SUCCESS,
    TIMUI_SLOT_COUNT
} TimuiStyleSlot;

typedef struct {
    TimuiStyle slots[TIMUI_SLOT_COUNT];
} TimuiTheme;

TIMUI_API TimuiTheme timui_theme_builtin(TimuiBuiltinTheme t);
TIMUI_API TimuiStyle timui_theme_style(const TimuiTheme *th, TimuiStyleSlot slot);

/* ---- Terminal transport (backend abstraction) ------------------------- *
 * A vtable of read/write/flush/close over an opaque ctx. Real backends wrap
 * file descriptors; the fake backend captures output and replays injected
 * input so renderer/parser logic is unit-testable with no real terminal. */
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

struct TimuiEvent {
    TimuiEventKind kind;
    union {
        struct { TimuiKey key; uint32_t codepoint; uint32_t mods; TimuiKeyAction action; } key;
        struct { const char *ptr; size_t len; uint32_t codepoint; } text;
        struct { const char *ptr; size_t len; } paste;
        struct { int x; int y; int button; int wheel_y; uint32_t mods;
                 int pressed; int released; int motion; } mouse;
        struct { int focused; } focus;
    } as;
};

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
TIMUI_API int    timui_key_pressed(TimuiFrame *f, TimuiKey key);

/* ---- Interaction state (hot/active/focus) ----------------------------- *
 * Immediate-mode interaction: each frame, widgets call timui_interact_button
 * with their id + rect; the framework tracks hover, press/click edges,
 * click-to-focus, keyboard activation, and tab cycling. */
typedef struct {
    int hovered;
    int pressed;
    int active;
    int clicked;
    int focused;
} TimuiInteractResult;

typedef struct {
    TimuiId hot;
    TimuiId active;
    TimuiId focus;
    int mouse_x, mouse_y;
    int mouse_down;        /* current frame */
    int mouse_down_prev;   /* previous frame (edge detection) */
    int mouse_pressed;     /* edge: went down this frame */
    int mouse_released;    /* edge: went up this frame */
    int tab_pressed;
    int activate_pressed;
    TimuiId tab_order[64];
    int tab_count;
    int focus_advance;
    int modal_active;
    TimuiRect modal_rect;
} TimuiInteract;

TIMUI_API void                timui_interact_init(TimuiInteract *ia);
TIMUI_API void                timui_interact_set_mouse(TimuiInteract *ia, int x, int y, int down);
TIMUI_API void                timui_interact_set_keys(TimuiInteract *ia, int tab, int activate);
TIMUI_API void                timui_interact_begin(TimuiInteract *ia);
TIMUI_API TimuiInteractResult timui_interact_button(TimuiInteract *ia, TimuiId id, TimuiRect r);
TIMUI_API void                timui_interact_end(TimuiInteract *ia);

#ifdef __cplusplus
}
#endif
#endif /* TIMUI_H */

/* =========================================================================
 * Implementation -- split across src/timui_*.c for readability. These files
 * are textually #included here (one TU when TIMUI_IMPLEMENTATION is defined),
 * and tools/amalgamate.c inlines them into a self-contained release/timui.h.
 * ========================================================================= */
#ifdef TIMUI_IMPLEMENTATION
#include "../src/timui_int.h"
#include "../src/timui_core.c"
#include "../src/timui_render.c"
#include "../src/timui_term.c"
#include "../src/timui_input.c"
#include "../src/timui_widgets.c"
#include "../src/timui_clip.c"
#include "../src/timui_menus.c"
#include "../src/timui_app.c"
#endif /* TIMUI_IMPLEMENTATION */
