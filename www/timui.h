/* ---- timui.h -- amalgamated release header -- do not edit by hand. ------ */
/*
 * timui.h — single-header C99 immediate-mode TUI for modern terminals.
 *
 *   Primary usage (single-header / stb-style):
 *
 *       #define TIMUI_IMPLEMENTATION
 *       #include "timui.h"
 *
 *   Define TIMUI_IMPLEMENTATION in exactly one translation unit. Other
 *   translation units include this header normally for declarations only.
 *
 * Status: a working immediate-mode TUI, not a scaffold. The POSIX
 * raw-mode terminal backend, the incremental input parser (legacy + Kitty
 * keyboard, SGR mouse, bracketed paste, focus), the truecolour diff renderer,
 * and the themed widget set are all implemented and unit-tested. The only
 * lifecycle stub is the Win32 ConPTY backend, which returns
 * TIMUI_ERR_UNSUPPORTED.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
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
#define TIMUI_VERSION_MINOR 2
#define TIMUI_VERSION_PATCH 0
#define TIMUI_VERSION_STRING "0.2.0"

/* ---- Feature macros ----------------------------------------------------- *
 * TIMUI_IMPLEMENTATION   include the implementation (exactly one TU)
 * TIMUI_NO_THREADS       disable the thread-safe post API (implemented)
 * TIMUI_API              override public symbol visibility
 * TIMUI_STATIC           reserved (future static-link mode)
 *
 * Reserved (recognized by name only; no #ifdef gates them yet — defining one
 * is a no-op): TIMUI_NO_STDIO, TIMUI_NO_IMAGES, TIMUI_NO_UTF8_TABLES
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

/* Colour model (ADR 0001): colours are packed 0xRRGGBB. The out-of-range
 * sentinel TIMUI_COLOR_DEFAULT means "terminal default" (emit no colour SGR);
 * 0x000000 is literal black. Use it for fg/bg where you want the default. */
#define TIMUI_COLOR_DEFAULT 0xFFFFFFFFu

typedef struct {
    uint32_t fg;
    uint32_t bg;
    uint32_t attrs;
} TimuiStyle;

/* All three functions are REQUIRED (the library resizes buffers, so a custom
 * allocator with realloc==NULL would NULL-deref). timui_default_allocator()
 * supplies all three.
 *
 * realloc MUST follow C realloc semantics: on failure return NULL and LEAVE
 * *ptr unchanged (valid, unmoved). The library keeps the old buffer on a
 * failed resize (e.g. timui_cells_resize, timui_ui_resize's rollback) and
 * relies on this — an allocator that frees-on-failure would corrupt. */
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
    TIMUI_THEME_MONO,
    TIMUI_THEME_MODERN_LIGHT
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

/* ---- Lifecycle (POSIX terminal backend; Win32 ConPTY is a stub) -------- */
TIMUI_API TimuiResult timui_open(const TimuiConfig *cfg, Timui **out_ui);
TIMUI_API void        timui_close(Timui *ui);
/* Restore the terminal (screen exit + termios) — used by the SIGTERM/SIGHUP/
 * SIGQUIT handler timui_open installs, and callable directly (e.g. from an
 * app's own signal handler or atexit hook). */
TIMUI_API void        timui_restore_terminal(Timui *ui);
TIMUI_API const char *timui_error_string(TimuiResult result);
TIMUI_API const char *timui_version_string(void);

TIMUI_API bool      timui_begin(Timui *ui, TimuiFrame **out_frame);
TIMUI_API void      timui_end(TimuiFrame *frame);   /* exactly once per begin; a second end re-renders + re-swaps */
TIMUI_API TimuiRect timui_root(const TimuiFrame *frame);
TIMUI_API int       timui_width(const TimuiFrame *frame);
TIMUI_API int       timui_height(const TimuiFrame *frame);
TIMUI_API TimuiCellBuffer *timui_frame_buffer(TimuiFrame *frame);
/* Resize both cell buffers. Returns TIMUI_OK, TIMUI_ERR_INVALID_ARGUMENT for a
 * NULL ui / non-positive size, or TIMUI_ERR_OUT_OF_MEMORY if a buffer can't grow
 * (dimensions are left unchanged in that case — see V10 rollback). */
TIMUI_API TimuiResult      timui_ui_resize(Timui *ui, int w, int h);
TIMUI_API int              timui_poll_event(Timui *ui, TimuiEvent *out_event);
/* G7: returns the count of events dropped this frame (queue holds 16) and
 * resets the counter. Call after timui_begin to detect a burst that exceeded
 * the queue (large paste, high-rate mouse drag). */
TIMUI_API int              timui_events_dropped(Timui *ui);
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

/* Edit/navigation key flags accumulated per frame for the focused widget.
 * BACKSPACE / LEFT / RIGHT / HOME / END / DELETE drive in-line cursor editing in
 * text_area and input_field (one step per frame — a bitmask can't count repeats);
 * UP / DOWN drive listbox selection. */
#define TIMUI_KEYIN_BACKSPACE 1u
#define TIMUI_KEYIN_LEFT      2u
#define TIMUI_KEYIN_RIGHT     4u
#define TIMUI_KEYIN_HOME      8u
#define TIMUI_KEYIN_END       16u
#define TIMUI_KEYIN_DELETE    32u
#define TIMUI_KEYIN_UP        64u
#define TIMUI_KEYIN_DOWN      128u
#define TIMUI_KEYIN_KILL_EOL  256u    /* Ctrl-K: delete from the cursor to end of line */
#define TIMUI_KEYIN_KILL_BOL  512u    /* Ctrl-U: delete from start of line to the cursor */
#define TIMUI_KEYIN_KILL_WORD 1024u   /* Ctrl-W: delete the word before the cursor */

/* Mutable single-line input: click to focus, type to append (bounded by cap),
 * backspace deletes the last rune, Enter submits. Returns true on submit.
 * Append-only (no in-line cursor) — for full cursor editing use timui_input_field. */
TIMUI_API bool       timui_input_line_buf(TimuiFrame *f, TimuiId id, TimuiRect r, char *buf, size_t cap);

/* Single-line editable field with an in-line cursor (F1.5). The caller owns the
 * text buffer and the cursor/scroll state, mirroring TimuiTextAreaState. Click
 * to focus; Left/Right/Home/End move, Backspace/Delete edit at the cursor, typing
 * inserts mid-string, and the view scrolls horizontally to keep the cursor
 * visible. Returns true on Enter. `cursor`/`scroll_x` are byte-index and column. */
typedef struct { char *text; size_t cap; size_t cursor; int scroll_x; } TimuiInputState;
TIMUI_API bool       timui_input_field(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiInputState *st);
/* Single-line editor drawn with a caller-supplied style instead of the theme's
 * input slots — e.g. to blend into a surrounding panel. Same editing behaviour. */
TIMUI_API bool       timui_input_field_styled(TimuiFrame *f, TimuiId id, TimuiRect r,
                                              TimuiInputState *st, TimuiStyle style);

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

/* Reserved for a future typed-dialog helper. NOTE: timui_message_box currently
 * returns a raw 0-based button index, NOT a TimuiDialogResult. */
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

/* ---- Menu bar + popups (T5.7) ----------------------------------------- *
 * Caller-owned state (Z27): `open` — the id of the open menu (0 = none) —
 * persists across frames, so the app can observe / snapshot / drive which menu
 * is open. The remaining fields are a frame-scoped layout cursor that
 * timui_menu_bar_begin resets; callers just pass the same TimuiMenuBar through
 * begin → each menu_begin/menu_item → bar_end. */
typedef struct {
    TimuiId open;                         /* caller-owned: which menu is open */
    int bar_x, bar_y, item_x, item_y;     /* frame-scoped layout cursor */
    int clicked;                          /* internal: a press hit a header/item */
} TimuiMenuBar;
TIMUI_API void timui_menu_bar_begin(TimuiFrame *f, TimuiMenuBar *bar, TimuiRect r);
TIMUI_API int  timui_menu_begin(TimuiFrame *f, TimuiMenuBar *bar, TimuiId id, TimuiStr label); /* 1 if open */
TIMUI_API int  timui_menu_item(TimuiFrame *f, TimuiMenuBar *bar, TimuiId id, TimuiStr label);  /* 1 if clicked */
TIMUI_API void timui_menu_end(TimuiFrame *f);
TIMUI_API void timui_menu_bar_end(TimuiFrame *f, TimuiMenuBar *bar);          /* outside-click closes */

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
TIMUI_API TimuiResult timui_id_stack_push(TimuiIdStack *s, TimuiId id);        /* G6: returns OOM */
TIMUI_API TimuiResult timui_id_stack_push_cstr(TimuiIdStack *s, const char *str);
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

/* ---- Rect-split layout (clamps to non-negative) ------------------------ *
 * cut_* carve a strip off *r IN PLACE (the RectCut idiom) and return it;
 * inset/pad/split_* are pure — they take a rect by value and never mutate it. */
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
    TIMUI_CELL_CONTINUATION = 1u << 0,  /* live: wide-glyph trailing cell */
    /* Reserved (not yet used): the renderer derives width from TimuiCell.width
     * and links from hyperlink_id, so these flags are forward-looking only. */
    TIMUI_CELL_DIRTY        = 1u << 1,  /* reserved */
    TIMUI_CELL_WIDE         = 1u << 2,  /* reserved */
    TIMUI_CELL_IMAGE        = 1u << 3,  /* reserved */
    TIMUI_CELL_LINK         = 1u << 4   /* reserved */
} TimuiCellFlags;

typedef struct {
    uint32_t codepoint;
    uint32_t fg;
    uint32_t bg;
    uint32_t attrs;
    uint16_t width;
    uint16_t flags;
    uint32_t hyperlink_id;
    uint32_t image_id;    /* reserved for kitty-graphics cell placement (unused) */
} TimuiCell;

typedef struct { char uri[256]; } TimuiHyperlink;

struct TimuiCellBuffer {
    TimuiCell    *cells;
    int           w;
    int           h;
    TimuiAllocator alloc;   /* owning allocator (copied) */
    TimuiRect     clip;     /* active clip rect when has_clip */
    int           has_clip;
    TimuiHyperlink *links;  /* per-frame hyperlink table (id = index + 1) */
    int           link_count;
    int           link_cap;
};
TIMUI_API void timui_push_clip(TimuiFrame *f, TimuiRect rect);
TIMUI_API void timui_pop_clip(TimuiFrame *f);

/* ---- Scroll view (v0.2) ----------------------------------------------- */
TIMUI_API TimuiRect timui_scroll_begin(TimuiFrame *f, TimuiRect viewport, int scroll_y);
TIMUI_API void      timui_scroll_end(TimuiFrame *f);

TIMUI_API TimuiResult timui_cells_init(TimuiCellBuffer *buf, int w, int h, const TimuiAllocator *alloc);
TIMUI_API void        timui_cells_destroy(TimuiCellBuffer *buf);
TIMUI_API TimuiResult timui_cells_resize(TimuiCellBuffer *buf, int w, int h, const TimuiAllocator *alloc);
TIMUI_API void        timui_cells_clear(TimuiCellBuffer *buf);
TIMUI_API TimuiCell  *timui_cells_get(TimuiCellBuffer *buf, int x, int y);
TIMUI_API int         timui_cells_put(TimuiCellBuffer *buf, int x, int y, const TimuiCell *cell);
TIMUI_API uint32_t    timui_hyperlink_set(TimuiCellBuffer *buf, const char *uri);   /* v0.2: OSC 8 */
TIMUI_API void        timui_draw_text_linked(TimuiCellBuffer *buf, int x, int y, TimuiStr text, TimuiStyle st, uint32_t link);
TIMUI_API void        timui_label_hyperlink(TimuiFrame *f, int x, int y, TimuiStr text, const char *uri, TimuiStyle style);

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
    int last_link;                      /* current OSC 8 hyperlink id, 0 = none */
    /* OSC 8 hyperlink ids are per-frame indices into each buffer's links table,
     * so the renderer caches the last-emitted URI string (not the id) to detect
     * a same-id-different-URI change across frames (W9). */
    char last_link_uri[256];             /* URI of the currently-open OSC 8 link */
    int have_last_link;                  /* 1 = a link is currently open */
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
/* Test seam (Z25): force the tcsetattr call inside timui_termios_enter to fail,
 * so the enter-time failure/rollback branch (V11) can be exercised — there is no
 * portable way to make a real fd's tcsetattr fail while tcgetattr succeeds. Inert
 * (off) in production; pass non-zero to arm, zero to disarm. Test-only. */
TIMUI_API void        timui_termios_fail_tcsetattr_for_test(int on);

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
/* The capabilities detected for an open ui — so apps can, e.g., choose an inline
 * Kitty-graphics image vs a text fallback: timui_caps_has(timui_caps(ui), ...). */
TIMUI_API const TimuiCaps *timui_caps(const Timui *ui);

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
    int         sub_param; /* inside a ':' sub-parameter — ignore its digits (Z4) */
    int         csi_mouse; /* '<' introducer seen — SGR mouse */
    int         mparam[3]; /* mouse params: button-code, x, y */
    int         mcount;    /* mouse param index */
    int         pasting;   /* between ESC[200~ and ESC[201~ */
    const unsigned char *paste_ptr; /* start of in-feed paste content */
    int         utf8_need;
    int         utf8_len;
    uint32_t    utf8_cp;
    const char *utf8_ptr;
    uint64_t    now_ms;        /* current time, set via timui_input_set_now */
    uint64_t    esc_since_ms;  /* timestamp ESC state was entered */
    unsigned char paste_tail[6]; /* deferred partial paste terminator */
    int         paste_tail_len; /* length of deferred partial terminator */
} TimuiInputParser;

typedef void (*TimuiEventFn)(void *ctx, const TimuiEvent *ev);

TIMUI_API void   timui_input_init(TimuiInputParser *p);
TIMUI_API size_t timui_input_feed(TimuiInputParser *p, const void *bytes, size_t len,
                                  TimuiEventFn cb, void *ctx);
TIMUI_API void    timui_input_set_now(TimuiInputParser *p, uint64_t now_ms);
TIMUI_API void    timui_input_flush_esc(TimuiInputParser *p, uint64_t now_ms, TimuiEventFn cb, void *ctx);
TIMUI_API uint64_t timui_now_ms(void);   /* monotonic milliseconds */
TIMUI_API int    timui_key_pressed(TimuiFrame *f, TimuiKey key);
TIMUI_API int    timui_key_pressed_mods(TimuiFrame *f, TimuiKey key, uint32_t mods);
/* Typed text this frame not yet consumed by a focused input — digits, space, and
 * letters arrive as text, not TimuiKey events. timui_char_pressed scans for a
 * specific ASCII char; timui_text_input returns the raw run (a view into a
 * per-frame buffer, valid until the next timui_begin). */
TIMUI_API int      timui_char_pressed(const TimuiFrame *f, char ch);
TIMUI_API TimuiStr timui_text_input(const TimuiFrame *f);
/* Accumulated mouse-wheel delta this frame (+ up / - down, 0 = none). Requires
 * TIMUI_FLAG_MOUSE. Use for scrolling a view. */
TIMUI_API int      timui_mouse_wheel(const TimuiFrame *f);
/* 1 (+ 0-based cell in out_x/out_y) if a button was pressed this frame. */
TIMUI_API int      timui_mouse_clicked(const TimuiFrame *f, int *out_x, int *out_y);
/* URL of the OSC 8 hyperlink under cell (x,y) in the frame just drawn, else NULL
 * — e.g. to open a link on click when mouse reporting intercepts it. */
TIMUI_API const char *timui_hyperlink_at(const TimuiFrame *f, int x, int y);
/* Programmatic focus: focus the widget `id` (persists until a click/Tab moves
 * it); timui_focus returns the currently focused id (0 = none). */
TIMUI_API void     timui_set_focus(TimuiFrame *f, TimuiId id);
TIMUI_API TimuiId  timui_focus(const TimuiFrame *f);

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
    TimuiId *tab_order;          /* dynamically grown (V24); NULL until first push */
    int tab_count, tab_cap;
    const TimuiAllocator *alloc; /* owning allocator, for growing tab_order */
    int focus_advance;
    int modal_active;
    TimuiRect modal_rect;
} TimuiInteract;

TIMUI_API void                timui_interact_init(TimuiInteract *ia, const TimuiAllocator *alloc);
TIMUI_API void                timui_interact_destroy(TimuiInteract *ia);
TIMUI_API void                timui_interact_set_mouse(TimuiInteract *ia, int x, int y, int down);
TIMUI_API void                timui_interact_set_keys(TimuiInteract *ia, int tab, int activate);
TIMUI_API void                timui_interact_begin(TimuiInteract *ia);
TIMUI_API TimuiInteractResult timui_interact_button(TimuiInteract *ia, TimuiId id, TimuiRect r);
TIMUI_API void                timui_interact_end(TimuiInteract *ia);

/* ---- v0.2 utilities: clipboard + keymaps ------------------------------ */
TIMUI_API void timui_clipboard_set(TimuiTransport *t, TimuiStr text);

typedef struct { TimuiKey key; uint32_t mods; int action; } TimuiKeyBinding;
typedef struct { TimuiKeyBinding bindings[32]; int count; } TimuiKeymap;
TIMUI_API void timui_keymap_bind(TimuiKeymap *km, TimuiKey key, uint32_t mods, int action);
TIMUI_API int  timui_keymap_hit(TimuiFrame *f, const TimuiKeymap *km, int action);

/* ---- v0.2 widgets: table, tree, command palette ----------------------- *
 * Controlled by default (state in by value, new state out in the result), with
 * a _mut convenience twin that writes changes back through a pointer — the same
 * shape as timui_listbox / timui_listbox_mut. The plain forms never touch caller
 * memory; the _mut forms write back only on an actual change. */
typedef const char *(*TimuiCellFn)(void *ud, int row, int col);
typedef struct { int selected; int scroll; } TimuiTableState;
typedef struct { TimuiTableState state; int state_changed; int focused; } TimuiTableResult;
TIMUI_API TimuiTableResult timui_table(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *headers, int ncols, int nrows, TimuiCellFn cell_fn, void *ud,
    TimuiTableState state);
TIMUI_API TimuiTableResult timui_table_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *headers, int ncols, int nrows, TimuiCellFn cell_fn, void *ud,
    TimuiTableState *state);

typedef struct { int depth; const char *label; int has_children; int expanded; } TimuiTreeNode;
typedef struct { int selected; int state_changed; int focused; } TimuiTreeResult;
TIMUI_API TimuiTreeResult timui_tree(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTreeNode *nodes, int count, int selected);
TIMUI_API TimuiTreeResult timui_tree_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTreeNode *nodes, int count, int *selected);

typedef struct { char filter[64]; int selected; } TimuiCmdPaletteState;
typedef struct { TimuiCmdPaletteState state; int activated; int state_changed; } TimuiCmdPaletteResult;
TIMUI_API TimuiCmdPaletteResult timui_command_palette(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *commands, int count, TimuiCmdPaletteState state);
TIMUI_API TimuiCmdPaletteResult timui_command_palette_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *commands, int count, TimuiCmdPaletteState *state);

/* ---- v0.2: snapshot testing + text-area + ConPTY ---------------------- */
TIMUI_API void timui_snapshot_render(const TimuiCellBuffer *buf, int row, char *out, size_t cap);
TIMUI_API int  timui_snapshot_row_eq(const TimuiCellBuffer *buf, int row, const char *expected);
/* Full-grid serialization for golden-file visual testing (Tier B). Returns the
 * would-be length (snprintf-style). */
TIMUI_API size_t timui_snapshot_grid(const TimuiCellBuffer *buf, char *out, size_t cap);
/* Cell-by-cell grid equality (reused by the libvterm round-trip harness).
 * Writes a one-cell diff message to diff_out on the first mismatch. */
TIMUI_API int   timui_grid_eq(const TimuiCellBuffer *a, const TimuiCellBuffer *b,
                              char *diff_out, size_t diff_cap);

typedef struct { char *text; size_t cap; size_t cursor; int scroll_y; } TimuiTextAreaState;
TIMUI_API void timui_text_area(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiTextAreaState *state);

TIMUI_API TimuiResult timui_conpty_open(TimuiTransport *out_transport, int *out_pid);
TIMUI_API void timui_conpty_close(TimuiTransport *transport, int pid);

/* ---- v0.2: Kitty graphics images -------------------------------------- *
 * timui_image_draw records a placement; the image is transmitted (once, by id)
 * and placed ON TOP of the cell diff in timui_end, so it composes with the cell
 * renderer instead of being clobbered by it. `id` is assigned on first transmit
 * (0 = not yet sent). The caller reserves the region (draws its own background
 * and no text there). Kitty-graphics terminals only; a "[img]" cell placeholder
 * is drawn otherwise. */
typedef struct TimuiImage { unsigned char *data; size_t len; uint32_t id;
                            int px_w, px_h; } TimuiImage;   /* pixel size from the PNG IHDR */
TIMUI_API TimuiImage *timui_image_from_png(Timui *ui, const void *data, size_t size);
TIMUI_API void        timui_image_free(Timui *ui, TimuiImage *img);
TIMUI_API void        timui_image_draw(TimuiFrame *f, TimuiImage *img, TimuiRect r);
/* Draw only the part of `img` (which maps to cell rect `full`) that lands inside
 * `visible` — i.e. crop the image to the visible sub-rect. For smoothly clipping
 * an image as it scrolls off a pane. `visible` must be within `full`. */
TIMUI_API void        timui_image_draw_clipped(TimuiFrame *f, TimuiImage *img,
                                               TimuiRect full, TimuiRect visible);
TIMUI_API void        timui_force_cap(Timui *ui, TimuiCapFlags cap, int enable);

#ifdef __cplusplus
}
#endif
#endif /* TIMUI_H */

/* =========================================================================
 * Implementation -- enabled by defining TIMUI_IMPLEMENTATION in exactly one
 * translation unit before including this header.
 * ========================================================================= */
#ifdef TIMUI_IMPLEMENTATION
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

/* ---- lifecycle + frame ------------------------------------------------ */
/* TIMUI_TRACE: append a human-readable line of raw input bytes to the trace fd
 * (ESC -> \e, printable as-is, else \xNN). For diagnosing drag-drop / paste. */
static void trace_write_(int fd, const char *tag, const unsigned char *b, size_t n){
    static const char hex[] = "0123456789abcdef";
    char line[1200];
    size_t k, o = 0;
    if(fd < 0) return;
    while(*tag && o < sizeof line - 1) line[o++] = *tag++;
    for(k = 0; k < n && o + 4 < sizeof line; k++){
        unsigned char c = b[k];
        if(c == 0x1b){ line[o++] = '\\'; line[o++] = 'e'; }
        else if(c >= 0x20 && c < 0x7f){ line[o++] = (char)c; }
        else { line[o++] = '\\'; line[o++] = 'x'; line[o++] = hex[c >> 4]; line[o++] = hex[c & 15]; }
    }
    if(o < sizeof line) line[o++] = '\n';
    (void)write(fd, line, o);
}
static void ui_event_cb(void *ctx, const TimuiEvent *ev){
    Timui *ui = (Timui *)ctx;
    /* Bracketed paste (incl. a Finder drag-drop of a path) arrives here during
     * the parse, while its ptr into the read buffer is still valid — and a paste
     * split across reads produces SEVERAL paste events in a frame. Accumulate
     * their content into a persistent buffer (not enqueued) so nothing dangles
     * or gets overwritten; the frame appends it to the focused input's text. */
    if(ev->kind == TIMUI_EVENT_PASTE){
        size_t k;
        if(ui->trace_fd >= 0)
            trace_write_(ui->trace_fd, "PASTE ", (const unsigned char *)ev->as.paste.ptr, ev->as.paste.len);
        for(k = 0; k < ev->as.paste.len && ui->paste_len < (int)sizeof(ui->paste_buf); k++)
            ui->paste_buf[ui->paste_len++] = ev->as.paste.ptr[k];
        return;
    }
    if(ui->event_count < (int)(sizeof(ui->events) / sizeof(ui->events[0])))
        ui->events[ui->event_count++] = *ev;
    else
        ui->events_dropped++;
}
/* Write ALL n bytes to fd. The output fd typically SHARES its open file
 * description with the input fd (fd 0/1 on a tty), which we set O_NONBLOCK for
 * the frame loop's read — so writes can return a short count or EAGAIN under
 * output pressure (heavy rendering while typing fast). A single write() that
 * dropped the remainder would lose render bytes and garble the screen, so loop:
 * retry on EINTR, wait for writability on EAGAIN, and continue on a partial
 * write until the whole buffer is out. Returns bytes written (== n on success),
 * or -1 if nothing could be written. Exposed (not in the public header) so the
 * partial-write behavior is unit-testable via a pipe. */
TIMUI_API int timui_write_all_(int fd, const void *d, size_t n){
    const char *p = (const char *)d;
    size_t off = 0;
    while(off < n){
        ssize_t w = write(fd, p + off, n - off);
        if(w > 0){ off += (size_t)w; continue; }
        if(w < 0 && errno == EINTR) continue;
        if(w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)){
            struct pollfd pfd;
            pfd.fd = fd; pfd.events = POLLOUT; pfd.revents = 0;
            while(poll(&pfd, 1, -1) < 0 && errno == EINTR){ /* retry */ }
            if(pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) break;
            continue;
        }
        break;   /* genuine write error */
    }
    return (off == 0 && n > 0) ? -1 : (int)off;
}
static int fd_write(TimuiTransport *t, const void *d, size_t n){
    TimuiFdCtx *c = (TimuiFdCtx *)t->ctx;
    return timui_write_all_(c->write_fd, d, n);
}
static int fd_read(TimuiTransport *t, void *b, size_t cap){
    TimuiFdCtx *c = (TimuiFdCtx *)t->ctx;
    ssize_t r = read(c->read_fd, b, cap);
    return r <= 0 ? 0 : (int)r;
}
static int fd_flush(TimuiTransport *t){ (void)t; return 0; }
static void fd_close(TimuiTransport *t){ (void)t; }

/* Wire up the buffers/renderer/input/msgq/id-stack for a given size. */
static TimuiResult timui_setup(Timui *ui, int w, int h){
    TimuiResult r;
    ui->w = w;
    ui->h = h;
    r = timui_cells_init(&ui->curr, w, h, &ui->alloc);
    if(r != TIMUI_OK) return r;
    r = timui_cells_init(&ui->prev, w, h, &ui->alloc);
    if(r != TIMUI_OK){ timui_cells_destroy(&ui->curr); return r; }
    ui->have_buffers = 1;
    timui_renderer_reset(&ui->renderer);
    timui_input_init(&ui->input);
    r = timui_mpsc_init(&ui->postq, &ui->alloc);
    if(r != TIMUI_OK){ timui_cells_destroy(&ui->curr); timui_cells_destroy(&ui->prev); ui->have_buffers = 0; return r; }
    ui->have_postq = 1;
    r = timui_id_stack_init(&ui->ids, &ui->alloc, 32);
    if(r != TIMUI_OK){
        timui_mpsc_destroy(&ui->postq); ui->have_postq = 0;
        timui_cells_destroy(&ui->curr); timui_cells_destroy(&ui->prev); ui->have_buffers = 0;
        return r;
    }
    ui->have_ids = 1;
    timui_interact_init(&ui->ia, &ui->alloc);
    ui->theme = timui_theme_builtin(ui->cfg.theme);
    ui->should_quit = 0;
    ui->event_count = 0;
    ui->frame.ui = ui;
    return TIMUI_OK;
}
TIMUI_API TimuiResult timui_open_for_test(Timui **out_ui, TimuiTransport transport, int w, int h, const TimuiAllocator *alloc){
    Timui *ui;
    TimuiResult r;
    if(!out_ui || w <= 0 || h <= 0 || !alloc) return TIMUI_ERR_INVALID_ARGUMENT;
    *out_ui = NULL;
    ui = (Timui *)alloc->alloc(alloc->userdata, sizeof(Timui));
    if(!ui) return TIMUI_ERR_OUT_OF_MEMORY;
    memset(ui, 0, sizeof *ui);
    ui->alloc = *alloc;
    ui->transport = transport;
    ui->have_transport = 1;
    ui->fd.read_fd = -1;   /* no real fd behind a test/fake transport (W7) */
    ui->trace_fd = -1;
    timui_caps_detect(&ui->caps, NULL, NULL, NULL);
    r = timui_setup(ui, w, h);
    if(r != TIMUI_OK){ alloc->free(alloc->userdata, ui, sizeof *ui); return r; }
    *out_ui = ui;
    return TIMUI_OK;
}
/* ---- terminal restoration on signal (W6) ------------------------------ *
 * An external termination signal (SIGTERM/SIGHUP/SIGQUIT — kill, window
 * close, Ctrl-\) must not leave the terminal in raw mode. timui_open installs
 * a handler that restores the screen + termios before the process dies. This
 * needs ONE piece of global state — a static Timui* — which is a documented
 * carve-out from the "no global state" rule, justified by the safety
 * requirement (a bricked terminal is the failure mode). Single-instance
 * assumption: one controlling terminal per process.
 *
 * Async-signal-safety: the handler calls only write (screen_exit) and
 * tcsetattr (termios_restore), both async-signal-safe; the process is about
 * to die, so interleaving with in-flight I/O is acceptable. */
static Timui *g_sig_restore_ui = NULL;

TIMUI_API void timui_restore_terminal(Timui *ui){
    if(!ui) return;
    if(ui->screen_active) timui_screen_exit(&ui->transport, &ui->screen);
    if(ui->termios_active) timui_termios_restore(&ui->termios);
}
static void timui_sig_restore(int sig){
    timui_restore_terminal(g_sig_restore_ui);
    signal(sig, SIG_DFL);     /* default disposition, then re-raise to terminate */
    raise(sig);
}
static void timui_install_sig_handlers(Timui *ui){
    struct sigaction sa;
    if(!ui || (!ui->termios_active && !ui->screen_active)) return;
    g_sig_restore_ui = ui;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = timui_sig_restore;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
}
static void timui_remove_sig_handlers(void){
    struct sigaction sa;
    g_sig_restore_ui = NULL;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
}

TIMUI_API TimuiResult timui_open(const TimuiConfig *cfg, Timui **out_ui){
    Timui *ui;
    TimuiAllocator al;
    int w = 80, h = 24;
    TimuiResult r;
    if(!cfg || !out_ui) return TIMUI_ERR_INVALID_ARGUMENT;
    *out_ui = NULL;
    al = cfg->allocator.alloc ? cfg->allocator : timui_default_allocator();
    ui = (Timui *)al.alloc(al.userdata, sizeof(Timui));
    if(!ui) return TIMUI_ERR_OUT_OF_MEMORY;
    memset(ui, 0, sizeof *ui);
    ui->alloc = al;
    ui->cfg = *cfg;
    ui->fd.read_fd = cfg->input_fd;
    ui->fd.write_fd = cfg->output_fd;
    /* TIMUI_TRACE=<file>: append a raw-input trace (drag-drop / paste debugging).
     * Best-effort; a failed open leaves tracing off. */
    ui->trace_fd = -1;
    { const char *tp = getenv("TIMUI_TRACE");
      if(tp && *tp) ui->trace_fd = open(tp, O_WRONLY | O_CREAT | O_APPEND, 0644); }
    ui->transport.write = fd_write;
    ui->transport.read  = fd_read;
    ui->transport.flush = fd_flush;
    ui->transport.close = fd_close;
    ui->transport.ctx   = &ui->fd;
    ui->have_transport  = 1;
    timui_caps_detect(&ui->caps, getenv("TERM"), getenv("TERM_PROGRAM"), getenv("COLORTERM"));
    if(timui_term_size(cfg->output_fd, &w, &h) != TIMUI_OK){ w = 80; h = 24; }
    if(isatty(cfg->input_fd)){
        int flags = fcntl(cfg->input_fd, F_GETFL, 0);
        if(flags >= 0) (void)fcntl(cfg->input_fd, F_SETFL, flags | O_NONBLOCK);  /* nonblocking tty input */
        if(timui_termios_enter(&ui->termios, cfg->input_fd) == TIMUI_OK) ui->termios_active = 1;
        timui_screen_enter(&ui->transport, &ui->screen, cfg->flags, timui_str_from_cstr(cfg->title));
        ui->screen_active = 1;
    }else{
        /* non-tty real fd (piped/headless input): make it non-blocking so a read
         * with no data returns EAGAIN instead of blocking (W7 hot-spin fix). */
        int flags = fcntl(cfg->input_fd, F_GETFL, 0);
        if(flags >= 0) (void)fcntl(cfg->input_fd, F_SETFL, flags | O_NONBLOCK);
    }
    r = timui_setup(ui, w, h);
    if(r != TIMUI_OK){
        if(ui->screen_active) timui_screen_exit(&ui->transport, &ui->screen);
        if(ui->termios_active){ timui_termios_restore(&ui->termios); timui_termios_destroy(&ui->termios); }
        al.free(al.userdata, ui, sizeof *ui);
        return r;
    }
    *out_ui = ui;
    timui_install_sig_handlers(ui);   /* W6: restore the terminal on SIGTERM/SIGHUP/SIGQUIT */
    return TIMUI_OK;
}
TIMUI_API void timui_close(Timui *ui){
    TimuiAllocator al;
    if(!ui) return;
    timui_remove_sig_handlers();      /* W6: stop intercepting (close restores itself) */
    if(ui->screen_active) timui_screen_exit(&ui->transport, &ui->screen);
    if(ui->termios_active){ timui_termios_restore(&ui->termios); timui_termios_destroy(&ui->termios); }
    if(ui->have_buffers){ timui_cells_destroy(&ui->curr); timui_cells_destroy(&ui->prev); }
    if(ui->have_postq) timui_mpsc_destroy(&ui->postq);
    timui_interact_destroy(&ui->ia);   /* V24: free the dynamic tab_order */
    if(ui->have_ids) timui_id_stack_destroy(&ui->ids);
    if(ui->trace_fd >= 0) close(ui->trace_fd);
    al = ui->alloc;
    al.free(al.userdata, ui, sizeof *ui);
}
TIMUI_API uint64_t timui_now_ms(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
TIMUI_API bool timui_begin(Timui *ui, TimuiFrame **out_frame){
    if(!ui || !out_frame) return false;
    if(ui->have_transport){
        char buf[256];
        int n;
        if(ui->termios_active){   /* real terminal: poll to avoid 100% CPU hot-spin */
            struct pollfd pfd;
            pfd.fd = ui->fd.read_fd; pfd.events = POLLIN; pfd.revents = 0;
            while(poll(&pfd, 1, 16) == -1 && errno == EINTR){}  /* retry on signal */
        }
        n = ui->transport.read(&ui->transport, buf, sizeof buf);
        if(n > 0){
            if(ui->trace_fd >= 0) trace_write_(ui->trace_fd, "READ  ", (const unsigned char *)buf, (size_t)n);
            timui_input_set_now(&ui->input, timui_now_ms());
            timui_input_feed(&ui->input, buf, (size_t)n, ui_event_cb, ui);
        }else if(ui->fd.read_fd >= 0 && !ui->termios_active){
            /* non-tty real fd with no data (piped/headless input, incl. EOF):
             * the tty poll above doesn't run, so throttle explicitly to avoid a
             * 100% CPU hot-spin (W7). Test/fake transports have read_fd = -1. */
            struct timespec ts = { 0, 16 * 1000 * 1000 };
            nanosleep(&ts, NULL);
        }
        timui_input_flush_esc(&ui->input, timui_now_ms(), ui_event_cb, ui);
    }
    /* drain parsed events: mouse -> hit-testing; tab/enter -> interaction;
     * printable text + cursor keys -> the focused input's accumulator. */
    /* Re-inject any input deferred from the previous frame's multi-Enter burst
     * (post-first-Enter tail), so this frame's new events append after it and a
     * fast "a\rb\r" submits one segment per frame instead of merging. */
    if(ui->pending_in_len > 0 || ui->pending_enter_count > 0){
        int pe;
        memcpy(ui->text_in, ui->pending_in, (size_t)ui->pending_in_len);
        ui->text_in_len = ui->pending_in_len;
        for(pe = 0; pe < ui->pending_enter_count; pe++) ui->enter_at[pe] = ui->pending_enter_at[pe];
        ui->enter_count = ui->pending_enter_count;
        ui->pending_in_len = 0;
        ui->pending_enter_count = 0;
    } else {
        ui->text_in_len = 0;
        ui->enter_count = 0;
    }
    ui->key_in = 0;
    ui->key_pressed = TIMUI_KEY_UNKNOWN;
    ui->key_mods = 0;
    ui->mouse_wheel = 0;
    ui->mouse_clicked = 0;
    {
        TimuiEvent ev;
        while(timui_poll_event(ui, &ev)){
            if(ev.kind == TIMUI_EVENT_MOUSE){
                timui_interact_set_mouse(&ui->ia, ev.as.mouse.x - 1, ev.as.mouse.y - 1, ev.as.mouse.pressed);
                ui->mouse_wheel += ev.as.mouse.wheel_y;   /* expose wheel to the app */
                ui->mouse_x = ev.as.mouse.x - 1; ui->mouse_y = ev.as.mouse.y - 1;
                if(ev.as.mouse.pressed) ui->mouse_clicked = 1;
            } else if(ev.kind == TIMUI_EVENT_KEY){
                ui->key_pressed = ev.as.key.key;   /* app-level key detection */
                ui->key_mods = ev.as.key.mods;
                if(ev.as.key.key == TIMUI_KEY_TAB) timui_interact_set_keys(&ui->ia, 1, 0);
                else if(ev.as.key.key == TIMUI_KEY_ENTER){
                    timui_interact_set_keys(&ui->ia, 0, 1);
                    /* record the Enter's position in the text stream (input_field
                     * segments submits on these; excess past the cap just merges). */
                    if(ui->enter_count < (int)(sizeof(ui->enter_at)/sizeof(ui->enter_at[0])))
                        ui->enter_at[ui->enter_count++] = ui->text_in_len;
                }
                else if(ev.as.key.key == TIMUI_KEY_BACKSPACE) ui->key_in |= TIMUI_KEYIN_BACKSPACE;
                else if(ev.as.key.key == TIMUI_KEY_LEFT)   ui->key_in |= TIMUI_KEYIN_LEFT;
                else if(ev.as.key.key == TIMUI_KEY_RIGHT)  ui->key_in |= TIMUI_KEYIN_RIGHT;
                else if(ev.as.key.key == TIMUI_KEY_HOME)   ui->key_in |= TIMUI_KEYIN_HOME;
                else if(ev.as.key.key == TIMUI_KEY_END)    ui->key_in |= TIMUI_KEYIN_END;
                else if(ev.as.key.key == TIMUI_KEY_DELETE) ui->key_in |= TIMUI_KEYIN_DELETE;
                else if(ev.as.key.key == TIMUI_KEY_UP) ui->key_in |= TIMUI_KEYIN_UP;
                else if(ev.as.key.key == TIMUI_KEY_DOWN) ui->key_in |= TIMUI_KEYIN_DOWN;
                else if(ev.as.key.key == TIMUI_KEY_UNKNOWN && (ev.as.key.mods & TIMUI_MOD_CTRL)){
                    /* emacs / readline line editing (ubiquitous on macOS). Ctrl-H
                     * (backspace) already arrives as KEY_BACKSPACE from the parser. */
                    switch(ev.as.key.codepoint){
                        case 'a': ui->key_in |= TIMUI_KEYIN_HOME;      break;  /* start of line */
                        case 'e': ui->key_in |= TIMUI_KEYIN_END;       break;  /* end of line   */
                        case 'b': ui->key_in |= TIMUI_KEYIN_LEFT;      break;  /* back one char */
                        case 'f': ui->key_in |= TIMUI_KEYIN_RIGHT;     break;  /* forward       */
                        case 'd': ui->key_in |= TIMUI_KEYIN_DELETE;    break;  /* delete at cursor */
                        case 'k': ui->key_in |= TIMUI_KEYIN_KILL_EOL;  break;  /* kill to EOL    */
                        case 'u': ui->key_in |= TIMUI_KEYIN_KILL_BOL;  break;  /* kill to BOL    */
                        case 'w': ui->key_in |= TIMUI_KEYIN_KILL_WORD; break;  /* kill word back */
                        default: break;
                    }
                }
            } else if(ev.kind == TIMUI_EVENT_TEXT){
                /* UTF-8 encode the codepoint into text_in (supports international
                 * input) via the single shared encoder (Z6). */
                uint32_t cp = ev.as.text.codepoint;
                char enc[4];
                int enclen = timui_utf8_encode_(cp, enc);
                if(enclen > 0 && ui->text_in_len + enclen <= (int)sizeof(ui->text_in)){
                    int ei;
                    for(ei = 0; ei < enclen; ei++) ui->text_in[ui->text_in_len++] = enc[ei];
                }
            }
        }
        /* Append the frame's accumulated bracketed-paste content (a real paste
         * or a Finder drag-drop path) to the focused input as text. Control
         * bytes — newlines etc. — are dropped so a single-line field gets a
         * clean string and no escape sequence can be injected. */
        { int pk;
          for(pk = 0; pk < ui->paste_len && ui->text_in_len < (int)sizeof(ui->text_in); pk++){
              unsigned char pc = (unsigned char)ui->paste_buf[pk];
              if(pc >= 0x20 && pc != 0x7f) ui->text_in[ui->text_in_len++] = (char)pc;
          }
          ui->paste_len = 0;   /* consumed; reset for the next frame's feed */
        }
    }
    timui_interact_begin(&ui->ia);
    ui->cursor_visible = 0;           /* F1.4: focused input re-requests each frame */
    ui->curr.has_clip = 0;            /* fresh clip stack each frame */
    ui->clip_count = 0;
    ui->img_place_count = 0;          /* image placements are per-frame */
    timui_cells_clear(&ui->curr);
    ui->ids.count = 0;                  /* fresh id stack for this frame */
    ui->frame.ui = ui;
    *out_frame = &ui->frame;
    return true;
}
TIMUI_API void timui_end(TimuiFrame *frame){
    Timui *ui;
    TimuiCellBuffer tmp;
    int sync;
    if(!frame || !frame->ui) return;
    ui = frame->ui;
    timui_interact_end(&ui->ia);
    /* Wrap the whole frame in synchronized output (DEC 2026) when the terminal
     * supports it, so a partial update never reaches the screen — the diff
     * writes cells incrementally, and without this a fast-updating app tears
     * (a screenshot of a half-drawn frame looks like interleaved corruption).
     * Unsupported terminals lack the cap and ignore the markers anyway. */
    sync = (ui->caps.flags & TIMUI_CAP_SYNC_OUTPUT) ||
           (ui->cfg.flags  & TIMUI_FLAG_SYNC_OUTPUT);
    if(sync) timui_sync_begin(&ui->transport);
    timui_render_diff(&ui->transport, &ui->prev, &ui->curr, &ui->renderer);
    /* Kitty-graphics images drawn ON TOP of the diffed cells. Also run when the
     * count dropped to 0 (img_last_count>0) so scrolled-away placements get
     * cleared. A placement's CUP moves the physical cursor, so force the next
     * frame's diff to re-CUP whenever we emitted any. */
    if(ui->img_place_count > 0 || ui->img_last_count > 0){
        timui_images_flush_(ui);
        if(ui->img_place_count > 0){ ui->renderer.last_x = -1; ui->renderer.last_y = -1; }
    }
    /* F1.4: render_diff left the physical cursor at the last drawn cell, so
     * reposition it for the focused input every visible frame; emit a hide once
     * when focus leaves. */
    if(ui->cursor_visible){
        timui_render_cursor(&ui->transport, ui->cursor_x, ui->cursor_y, 1);
        /* render_cursor moved the physical cursor off render_diff's last cell —
         * resync the renderer (only when it actually emitted a CUP, i.e. the
         * position is on-screen), or next frame's diff skips a CUP it needs and
         * draws a cell at the cursor position instead of its own. */
        if(ui->cursor_x >= 0 && ui->cursor_y >= 0){
            ui->renderer.last_x = ui->cursor_x;
            ui->renderer.last_y = ui->cursor_y;
        }
        ui->cursor_shown = 1;
    } else if(ui->cursor_shown){
        timui_render_cursor(&ui->transport, -1, -1, 0);
        ui->cursor_shown = 0;
    }
    if(sync) timui_sync_end(&ui->transport);
    if(ui->transport.flush) ui->transport.flush(&ui->transport);   /* commit the frame */
    tmp = ui->prev; ui->prev = ui->curr; ui->curr = tmp;   /* swap for next diff */
}
TIMUI_API TimuiRect timui_root(const TimuiFrame *frame){
    TimuiRect z = {0, 0, 0, 0};
    if(!frame || !frame->ui) return z;
    z.w = frame->ui->w;
    z.h = frame->ui->h;
    return z;
}
TIMUI_API int timui_width(const TimuiFrame *frame){ return (frame && frame->ui) ? frame->ui->w : 0; }
TIMUI_API int timui_height(const TimuiFrame *frame){ return (frame && frame->ui) ? frame->ui->h : 0; }
TIMUI_API TimuiCellBuffer *timui_frame_buffer(TimuiFrame *frame){
    return (frame && frame->ui) ? &frame->ui->curr : NULL;
}
TIMUI_API TimuiResult timui_ui_resize(Timui *ui, int w, int h){
    TimuiResult r;
    int ow, oh;
    if(!ui || w <= 0 || h <= 0) return TIMUI_ERR_INVALID_ARGUMENT;
    ow = ui->w; oh = ui->h;
    /* Resize prev first; if curr then fails, roll prev back. The old order
     * (curr then prev) left curr at the new size but ui->w/h and prev at the
     * old — a divergence where layout used stale dims while the cell buffer
     * had grown. ui->w/h commit only when both buffers succeed. */
    r = timui_cells_resize(&ui->prev, w, h, &ui->alloc);
    if(r != TIMUI_OK) return r;                                         /* Z12: report OOM */
    r = timui_cells_resize(&ui->curr, w, h, &ui->alloc);
    if(r != TIMUI_OK){ (void)timui_cells_resize(&ui->prev, ow, oh, &ui->alloc); return r; }
    ui->w = w;
    ui->h = h;
    timui_renderer_reset(&ui->renderer);   /* cursor/SGR tracking invalidated */
    return TIMUI_OK;
}
TIMUI_API int timui_poll_event(Timui *ui, TimuiEvent *out_event){
    int i;
    if(!ui || !out_event || ui->event_count == 0) return 0;
    *out_event = ui->events[0];
    ui->event_count--;
    for(i = 0; i < ui->event_count; i++) ui->events[i] = ui->events[i + 1];
    return 1;
}
TIMUI_API int timui_events_dropped(Timui *ui){
    if(!ui) return 0;
    { int d = ui->events_dropped; ui->events_dropped = 0; return d; }   /* G7: read + reset */
}
TIMUI_API void timui_quit(Timui *ui){ if(ui) ui->should_quit = 1; }
TIMUI_API bool timui_should_quit(const Timui *ui){ return ui ? (bool)ui->should_quit : false; }
TIMUI_API const TimuiCaps *timui_caps(const Timui *ui){ return ui ? &ui->caps : NULL; }
TIMUI_API int timui_mouse_wheel(const TimuiFrame *f){ return (f && f->ui) ? f->ui->mouse_wheel : 0; }
TIMUI_API int timui_mouse_clicked(const TimuiFrame *f, int *out_x, int *out_y){
    if(!f || !f->ui || !f->ui->mouse_clicked) return 0;
    if(out_x) *out_x = f->ui->mouse_x;
    if(out_y) *out_y = f->ui->mouse_y;
    return 1;
}
/* URL of the OSC 8 hyperlink under cell (x,y) in the frame just drawn, or NULL.
 * Lets an app act on a link click (terminals with mouse reporting on send the
 * click to the app rather than opening the link themselves). */
TIMUI_API const char *timui_hyperlink_at(const TimuiFrame *f, int x, int y){
    Timui *ui;
    const TimuiCell *c;
    if(!f || !f->ui) return NULL;
    ui = f->ui;
    c = timui_cells_get(&ui->curr, x, y);
    if(c && c->hyperlink_id > 0 && (int)c->hyperlink_id <= ui->curr.link_count)
        return ui->curr.links[c->hyperlink_id - 1].uri;
    return NULL;
}
TIMUI_API int timui_key_pressed(TimuiFrame *f, TimuiKey key){
    return (f && f->ui && f->ui->key_pressed == key);
}
TIMUI_API int timui_key_pressed_mods(TimuiFrame *f, TimuiKey key, uint32_t mods){
    return (f && f->ui && f->ui->key_pressed == key &&
            (f->ui->key_mods & mods) == mods);
}
/* Typed characters this frame that a focused input has not consumed (digits,
 * space, and letters arrive as text, not TimuiKey events — so apps can read
 * single-key commands without reaching into internals). */
TIMUI_API TimuiStr timui_text_input(const TimuiFrame *f){
    TimuiStr s = { NULL, 0 };
    if(f && f->ui){ s.ptr = f->ui->text_in; s.len = (size_t)f->ui->text_in_len; }
    return s;
}
TIMUI_API int timui_char_pressed(const TimuiFrame *f, char ch){
    int i;
    if(!f || !f->ui) return 0;
    for(i = 0; i < f->ui->text_in_len; i++)
        if(f->ui->text_in[i] == ch) return 1;
    return 0;
}
/* Programmatic focus: focus the widget with `id` (persists until a click or Tab
 * moves it — call once, e.g. `if(!timui_focus(f)) timui_set_focus(f, id);`). */
TIMUI_API void timui_set_focus(TimuiFrame *f, TimuiId id){
    if(f && f->ui) f->ui->ia.focus = id;
}
TIMUI_API TimuiId timui_focus(const TimuiFrame *f){
    return (f && f->ui) ? f->ui->ia.focus : 0;
}

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
/* G6: push now returns TimuiResult so the caller can detect a grow-OOM and
 * skip the corresponding pop (preventing id-hierarchy corruption). */
TIMUI_API TimuiResult timui_id_stack_push(TimuiIdStack *s, TimuiId id){
    TimuiId seed;
    if(!s) return TIMUI_ERR_INVALID_ARGUMENT;
    seed = id_compose(s->count ? s->seeds[s->count - 1] : s->root, id);
    if(s->count == s->cap){                     /* grow geometrically */
        size_t ncap;
        TimuiId *ns;
        if(s->cap > SIZE_MAX / 2 / sizeof(TimuiId)) return TIMUI_ERR_OUT_OF_MEMORY;
        ncap = s->cap * 2;
        ns = (TimuiId *)s->alloc.realloc(
            s->alloc.userdata, s->seeds, s->cap * sizeof(TimuiId), ncap * sizeof(TimuiId));
        if(!ns) return TIMUI_ERR_OUT_OF_MEMORY;  /* OOM: push not applied, caller must not pop */
        s->seeds = ns;
        s->cap   = ncap;
    }
    s->seeds[s->count++] = seed;
    return TIMUI_OK;
}
TIMUI_API TimuiResult timui_id_stack_push_cstr(TimuiIdStack *s, const char *str){
    if(!s || !str) return TIMUI_ERR_INVALID_ARGUMENT;
    return timui_id_stack_push(s, timui_id_from_cstr(str));
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
    size_t need;
    unsigned char *p;
    if(!q || size > SIZE_MAX - hdr) return 0;   /* overflow guard */
    if(size > 0 && !data) return 0;             /* would record payload with no bytes */
    need = hdr + size;
    if(need > q->cap) return 0;   /* never fits */
    if(q->tail + need > q->cap){
        /* compact: move remaining data to the front to reuse freed head space */
        if(q->head > 0 && q->tail > q->head){
            memmove(q->buf, q->buf + q->head, q->tail - q->head);
            q->tail -= q->head;
            q->head = 0;
        }
        if(q->tail + need > q->cap) return 0;   /* still full after compact */
    }
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
    if(size > SIZE_MAX - sizeof(*n)) return 0;   /* overflow guard (cf. msgq_emit) */
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
    (void)ud; (void)os; if(ns == 0) ns = 1; return realloc(p, ns);
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
    if(align & (align - 1)) return NULL;     /* alignment must be a power of two */
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

/* Z10: undo the section's implementation-only macros so they can't leak into
 * the consumer's translation unit in the amalgamated single header. */
#undef TIMUI_ID_ROOT
#undef TIMUI_MPSC_LOCK
#undef TIMUI_MPSC_UNLOCK

/* ---- cell buffer ------------------------------------------------------ */
TIMUI_API TimuiResult timui_cells_init(TimuiCellBuffer *buf, int w, int h, const TimuiAllocator *alloc){
    size_t n;
    if(!buf || w <= 0 || h <= 0 || !alloc) return TIMUI_ERR_INVALID_ARGUMENT;
    if((size_t)w > SIZE_MAX / (size_t)h) return TIMUI_ERR_OUT_OF_MEMORY;     /* w*h overflow */
    n = (size_t)w * (size_t)h;
    if(n > SIZE_MAX / sizeof(TimuiCell)) return TIMUI_ERR_OUT_OF_MEMORY;     /* n*sizeof overflow */
    buf->w = w;
    buf->h = h;
    buf->alloc = *alloc;
    buf->has_clip = 0;
    buf->links = NULL;
    buf->link_count = 0;
    buf->link_cap = 0;
    buf->cells = (TimuiCell *)alloc->alloc(alloc->userdata, n * sizeof(TimuiCell));
    if(!buf->cells){ buf->w = buf->h = 0; return TIMUI_ERR_OUT_OF_MEMORY; }
    timui_cells_clear(buf);
    return TIMUI_OK;
}
TIMUI_API void timui_cells_destroy(TimuiCellBuffer *buf){
    size_t n;
    if(!buf || !buf->cells) return;
    n = (size_t)buf->w * (size_t)buf->h;
    if(buf->links){ buf->alloc.free(buf->alloc.userdata, buf->links, (size_t)buf->link_cap * sizeof(*buf->links)); buf->links = NULL; }
    buf->alloc.free(buf->alloc.userdata, buf->cells, n * sizeof(TimuiCell));
    buf->cells = NULL;
    buf->w = buf->h = 0;
}
TIMUI_API TimuiResult timui_cells_resize(TimuiCellBuffer *buf, int w, int h, const TimuiAllocator *alloc){
    size_t n, oldn;
    TimuiCell *nc;
    if(!buf || w <= 0 || h <= 0) return TIMUI_ERR_INVALID_ARGUMENT;
    if(alloc) buf->alloc = *alloc;
    if((size_t)w > SIZE_MAX / (size_t)h) return TIMUI_ERR_OUT_OF_MEMORY;     /* w*h overflow */
    n = (size_t)w * (size_t)h;
    if(n > SIZE_MAX / sizeof(TimuiCell)) return TIMUI_ERR_OUT_OF_MEMORY;     /* n*sizeof overflow */
    oldn = (size_t)buf->w * (size_t)buf->h;
    nc = (TimuiCell *)buf->alloc.realloc(buf->alloc.userdata, buf->cells,
                                         oldn * sizeof(TimuiCell), n * sizeof(TimuiCell));
    if(!nc) return TIMUI_ERR_OUT_OF_MEMORY;
    buf->cells = nc;
    buf->w = w;
    buf->h = h;
    timui_cells_clear(buf);
    return TIMUI_OK;
}
TIMUI_API void timui_cells_clear(TimuiCellBuffer *buf){
    size_t i, n;
    if(!buf || !buf->cells) return;
    n = (size_t)buf->w * (size_t)buf->h;
    memset(buf->cells, 0, n * sizeof(TimuiCell));   /* codepoint/attrs/width/flags/links = 0 */
    for(i = 0; i < n; i++){                          /* empty = DEFAULT colour (ADR 0001), not black */
        buf->cells[i].fg = TIMUI_COLOR_DEFAULT;
        buf->cells[i].bg = TIMUI_COLOR_DEFAULT;
    }
    buf->link_count = 0;   /* hyperlinks are per-frame */
}
TIMUI_API TimuiCell *timui_cells_get(TimuiCellBuffer *buf, int x, int y){
    if(!buf || !buf->cells || x < 0 || y < 0 || x >= buf->w || y >= buf->h) return NULL;
    return &buf->cells[(size_t)y * (size_t)buf->w + (size_t)x];
}
TIMUI_API int timui_cells_put(TimuiCellBuffer *buf, int x, int y, const TimuiCell *cell){
    TimuiCell *dst;
    if(!cell) return 0;
    dst = timui_cells_get(buf, x, y);
    if(!dst) return 0;
    *dst = *cell;
    return 1;
}

/* ---- utf-8 decode + width --------------------------------------------- */
TIMUI_API int timui_utf8_decode(const char *s, size_t len, uint32_t *out_cp){
    const unsigned char *p = (const unsigned char *)s;
    uint32_t cp = 0;
    int need = 0, i;
    if(!s || len == 0){ if(out_cp) *out_cp = 0; return 0; }
    if(p[0] < 0x80){ if(out_cp) *out_cp = p[0]; return 1; }
    if((p[0] & 0xE0) == 0xC0){ cp = (uint32_t)(p[0] & 0x1F); need = 1; }
    else if((p[0] & 0xF0) == 0xE0){ cp = (uint32_t)(p[0] & 0x0F); need = 2; }
    else if((p[0] & 0xF8) == 0xF0){ cp = (uint32_t)(p[0] & 0x07); need = 3; }
    else { if(out_cp) *out_cp = 0xFFFD; return 1; }            /* invalid lead */
    if((int)len < 1 + need){ return 0; }                       /* incomplete */
    for(i = 1; i <= need; i++){
        if((p[i] & 0xC0) != 0x80){ if(out_cp) *out_cp = 0xFFFD; return 1; }   /* bad continuation */
        cp = (cp << 6) | (uint32_t)(p[i] & 0x3F);
    }
    if(out_cp) *out_cp = cp;
    /* reject overlong encodings and surrogate halves */
    if(need == 1 && cp < 0x80){ if(out_cp) *out_cp = 0xFFFD; return 1; }
    if(need == 2 && cp < 0x800){ if(out_cp) *out_cp = 0xFFFD; return 1; }
    if(need == 3 && cp < 0x10000){ if(out_cp) *out_cp = 0xFFFD; return 1; }
    if(cp >= 0xD800 && cp <= 0xDFFF){ if(out_cp) *out_cp = 0xFFFD; return 1; }
    if(cp > 0x10FFFF){ if(out_cp) *out_cp = 0xFFFD; return 1; }   /* above Unicode max */
    return 1 + need;
}
TIMUI_API int timui_utf8_width(uint32_t cp){
    if(cp < 0x20 || cp == 0x7F) return 0;                       /* control */
    if(cp == 0xFFFD) return 1;
    if((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
       (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
       (cp >= 0xFE20 && cp <= 0xFE2F)) return 0;                /* combining */
    if((cp >= 0x1100 && cp <= 0x115F) ||
       (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7A3) ||
       (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE6F) ||
       (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6) ||
       (cp >= 0x1F300 && cp <= 0x1FAFF)) return 2;              /* wide/fullwidth */
    return 1;
}

/* ---- drawing primitives ----------------------------------------------- */
/* Z7: the single glyph-emit primitive. Writes cp at (x,y) with style st and an
 * optional hyperlink id, and blanks the continuation cell for a wide glyph.
 * This is the one place the subtle wide-glyph continuation logic lives (the
 * site of the prior V6/X2 bugs) — draw_text, draw_text_linked, and the box/
 * fill/line primitives all route through it. */
static void put_glyph_link(TimuiCellBuffer *buf, int x, int y, uint32_t cp, TimuiStyle st, uint32_t link){
    TimuiCell c;
    int w;
    if(buf->has_clip && (x < buf->clip.x || y < buf->clip.y ||
       x >= buf->clip.x + buf->clip.w || y >= buf->clip.y + buf->clip.h)) return;
    memset(&c, 0, sizeof c);
    w = timui_utf8_width(cp);
    c.codepoint = cp;
    c.fg = st.fg;
    c.bg = st.bg;
    c.attrs = st.attrs;
    c.width = (uint16_t)(w > 1 ? 2 : 1);
    c.hyperlink_id = link;
    timui_cells_put(buf, x, y, &c);
    /* wide glyph: blank the continuation cell so stale content isn't left behind */
    if(w > 1 && !(buf->has_clip && (x + 1 < buf->clip.x || x + 1 >= buf->clip.x + buf->clip.w))){
        memset(&c, 0, sizeof c);
        c.fg = TIMUI_COLOR_DEFAULT;   /* ADR 0001: blanked = default, not black */
        c.bg = TIMUI_COLOR_DEFAULT;
        c.flags = TIMUI_CELL_CONTINUATION;
        c.width = 0;
        timui_cells_put(buf, x + 1, y, &c);
    }
}
/* Unlinked convenience for the drawing primitives (box/fill/lines). */
static void put_glyph(TimuiCellBuffer *buf, int x, int y, uint32_t cp, TimuiStyle st){
    put_glyph_link(buf, x, y, cp, st, 0);
}
TIMUI_API TimuiStyle timui_style_make(uint32_t fg, uint32_t bg, uint32_t attrs){
    TimuiStyle s;
    s.fg = fg;
    s.bg = bg;
    s.attrs = attrs;
    return s;
}
TIMUI_API void timui_draw_text(TimuiCellBuffer *buf, int x, int y, TimuiStr text, TimuiStyle st){
    timui_draw_text_linked(buf, x, y, text, st, 0);   /* Z7: unlinked == linked with id 0 */
}
TIMUI_API uint32_t timui_hyperlink_set(TimuiCellBuffer *buf, const char *uri){
    size_t n;
    if(!buf || !uri) return 0;
    if(buf->link_count >= buf->link_cap){
        int nc = buf->link_cap ? buf->link_cap * 2 : 8;
        TimuiHyperlink *nl = (TimuiHyperlink *)buf->alloc.realloc(buf->alloc.userdata,
            buf->links, (size_t)buf->link_cap * sizeof(*nl), (size_t)nc * sizeof(*nl));
        if(!nl) return 0;
        buf->links = nl;
        buf->link_cap = nc;
    }
    n = strlen(uri);
    if(n >= sizeof(buf->links[0].uri)) n = sizeof(buf->links[0].uri) - 1;
    memcpy(buf->links[buf->link_count].uri, uri, n);
    buf->links[buf->link_count].uri[n] = '\0';
    buf->link_count++;
    return (uint32_t)buf->link_count;   /* 1-based id */
}
TIMUI_API void timui_draw_text_linked(TimuiCellBuffer *buf, int x, int y, TimuiStr text, TimuiStyle st, uint32_t link){
    size_t i = 0;
    int cx = x;
    if(!buf || !text.ptr) return;
    while(i < text.len){
        uint32_t cp = 0;
        int adv = timui_utf8_decode(text.ptr + i, text.len - i, &cp);
        int w;
        if(adv <= 0) adv = 1;
        w = timui_utf8_width(cp);
        if(w > 0){
            put_glyph_link(buf, cx, y, cp, st, link);
            cx += w;
        }
        i += (size_t)adv;
    }
}
/* Clamp a rect to the buffer bounds (and non-negative). Prevents a runaway
 * loop and signed-overflow UB (r.y+r.h) on an extreme rect; put_glyph already
 * bounds-checks each cell, so this is about loop bounds + UB, not write safety. */
static TimuiRect rect_clamp_buf(const TimuiCellBuffer *buf, TimuiRect r){
    if(r.x < 0){ r.w += r.x; r.x = 0; }
    if(r.y < 0){ r.h += r.y; r.y = 0; }
    if(r.x > buf->w) r.x = buf->w;
    if(r.y > buf->h) r.y = buf->h;
    if(r.w > buf->w - r.x) r.w = buf->w - r.x;
    if(r.h > buf->h - r.y) r.h = buf->h - r.y;
    if(r.w < 0) r.w = 0;
    if(r.h < 0) r.h = 0;
    return r;
}
TIMUI_API void timui_draw_fill(TimuiCellBuffer *buf, TimuiRect r, TimuiStyle st){
    int xi, yi;
    if(!buf) return;
    r = rect_clamp_buf(buf, r);
    for(yi = r.y; yi < r.y + r.h; yi++)
        for(xi = r.x; xi < r.x + r.w; xi++)
            put_glyph(buf, xi, yi, ' ', st);
}
TIMUI_API void timui_draw_hline(TimuiCellBuffer *buf, int x, int y, int w, TimuiStyle st){
    int i;
    if(!buf || w <= 0) return;
    /* Z9: clamp the run to the buffer so an extreme width can't overflow x+i or
     * spin the loop ~INT_MAX times (put_glyph already bounds-checks each cell,
     * so this is about loop bounds + UB, not write safety — same as draw_fill). */
    if(x < 0){ w += x; x = 0; }
    if(x >= buf->w || w <= 0) return;
    if(w > buf->w - x) w = buf->w - x;
    for(i = 0; i < w; i++) put_glyph(buf, x + i, y, 0x2500, st);
}
TIMUI_API void timui_draw_vline(TimuiCellBuffer *buf, int x, int y, int h, TimuiStyle st){
    int i;
    if(!buf || h <= 0) return;
    if(y < 0){ h += y; y = 0; }                 /* Z9: clamp — see draw_hline */
    if(y >= buf->h || h <= 0) return;
    if(h > buf->h - y) h = buf->h - y;
    for(i = 0; i < h; i++) put_glyph(buf, x, y + i, 0x2502, st);
}
TIMUI_API void timui_draw_box(TimuiCellBuffer *buf, TimuiRect r, uint32_t border_flags, TimuiStyle st){
    uint32_t horiz, vert, tl, tr, bl, br;
    int i;
    if(!buf) return;
    /* Z9: clamp the rect to the buffer first, mirroring draw_fill — kills the
     * r.x+r.w-1 signed-overflow UB and the ~INT_MAX edge loops on extreme
     * geometry; the box is clipped to the viewport (borders at the edge). */
    r = rect_clamp_buf(buf, r);
    if(r.w < 2 || r.h < 2) return;
    if(border_flags & TIMUI_BORDER_DOUBLE){ horiz=0x2550; vert=0x2551; tl=0x2554; tr=0x2557; bl=0x255A; br=0x255D; }
    else if(border_flags & TIMUI_BORDER_ASCII){ horiz='-'; vert='|'; tl='+'; tr='+'; bl='+'; br='+'; }
    else if(border_flags & TIMUI_BORDER_ROUND){ horiz=0x2500; vert=0x2502; tl=0x256D; tr=0x256E; bl=0x2570; br=0x256F; }
    else { horiz=0x2500; vert=0x2502; tl=0x250C; tr=0x2510; bl=0x2514; br=0x2518; }  /* single */
    put_glyph(buf, r.x,               r.y,               tl, st);
    put_glyph(buf, r.x + r.w - 1,     r.y,               tr, st);
    put_glyph(buf, r.x,               r.y + r.h - 1,     bl, st);
    put_glyph(buf, r.x + r.w - 1,     r.y + r.h - 1,     br, st);
    for(i = 1; i < r.w - 1; i++){
        put_glyph(buf, r.x + i, r.y,           horiz, st);
        put_glyph(buf, r.x + i, r.y + r.h - 1, horiz, st);
    }
    for(i = 1; i < r.h - 1; i++){
        put_glyph(buf, r.x,           r.y + i, vert, st);
        put_glyph(buf, r.x + r.w - 1, r.y + i, vert, st);
    }
}

/* ---- diff renderer ---------------------------------------------------- *
 * Emits the minimal terminal update for changed cells: cursor positioning
 * (CUP), truecolour SGR (only when the style changes), and the UTF-8 glyph.
 * No stdio: integers are formatted by hand. */
static int fmt_uint(char *buf, unsigned v){
    char tmp[16];
    int n = 0, i;
    if(v == 0){ buf[0] = '0'; return 1; }
    while(v){ tmp[n++] = (char)('0' + v % 10); v /= 10; }
    for(i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    return n;
}
static void r_emit(TimuiTransport *t, const char *s, size_t n){ if(t && t->write) (void)t->write(t, s, n); }
#define R_EMIT(t, lit) r_emit((t), (lit), sizeof(lit) - 1)
static void emit_truecolor(TimuiTransport *t, int bg, uint32_t rgb){
    char buf[40];
    int n = 0;
    buf[n++] = 0x1b; buf[n++] = '[';
    buf[n++] = (char)(bg ? '4' : '3'); buf[n++] = '8'; buf[n++] = ';'; buf[n++] = '2'; buf[n++] = ';';
    n += fmt_uint(buf + n, (rgb >> 16) & 0xff); buf[n++] = ';';
    n += fmt_uint(buf + n, (rgb >> 8) & 0xff);  buf[n++] = ';';
    n += fmt_uint(buf + n, rgb & 0xff);         buf[n++] = 'm';
    r_emit(t, buf, (size_t)n);
}
static void emit_cup(TimuiTransport *t, int x, int y){
    char buf[32];
    int n = 0;
    buf[n++] = 0x1b; buf[n++] = '[';
    n += fmt_uint(buf + n, (unsigned)(y + 1)); buf[n++] = ';';
    n += fmt_uint(buf + n, (unsigned)(x + 1)); buf[n++] = 'H';
    r_emit(t, buf, (size_t)n);
}
static void emit_sgr(TimuiTransport *t, TimuiRenderer *r, const TimuiCell *c){
    if((int)c->fg == r->last_fg && (int)c->bg == r->last_bg && (int)c->attrs == r->last_attrs) return;
    R_EMIT(t, "\x1b[0m");                 /* reset, then re-apply the full style */
    if(c->fg != TIMUI_COLOR_DEFAULT) emit_truecolor(t, 0, c->fg);   /* ADR 0001: 0x000000 is black */
    if(c->bg != TIMUI_COLOR_DEFAULT) emit_truecolor(t, 1, c->bg);
    if(c->attrs & TIMUI_ATTR_BOLD)      R_EMIT(t, "\x1b[1m");
    if(c->attrs & TIMUI_ATTR_DIM)       R_EMIT(t, "\x1b[2m");
    if(c->attrs & TIMUI_ATTR_ITALIC)    R_EMIT(t, "\x1b[3m");
    if(c->attrs & TIMUI_ATTR_UNDERLINE) R_EMIT(t, "\x1b[4m");
    if(c->attrs & TIMUI_ATTR_BLINK)     R_EMIT(t, "\x1b[5m");
    if(c->attrs & TIMUI_ATTR_REVERSE)   R_EMIT(t, "\x1b[7m");
    if(c->attrs & TIMUI_ATTR_STRIKE)    R_EMIT(t, "\x1b[9m");
    r->last_fg = (int)c->fg;
    r->last_bg = (int)c->bg;
    r->last_attrs = (int)c->attrs;
}
/* OSC 8 hyperlink: ESC]8;;<uri>ESC\\ to open, ESC]8;;ESC\\ to close. */
static void emit_osc8(TimuiTransport *t, const char *uri){
    R_EMIT(t, "\x1b]8;;");
    if(uri) r_emit(t, uri, strlen(uri));
    R_EMIT(t, "\x1b\\");
}
TIMUI_API void timui_renderer_reset(TimuiRenderer *r){
    if(!r) return;
    r->last_x = -1; r->last_y = -1;
    r->last_fg = -1; r->last_bg = -1; r->last_attrs = -1;
    r->last_link = 0;
    r->have_last_link = 0;
    r->last_link_uri[0] = '\0';
}
/* Resolve a cell's hyperlink id to its URI in the buffer (NULL if none / OOR).
 * ids are per-frame indices (cells_clear resets link_count each frame), so the
 * renderer compares URIs — not ids — to catch a same-id-different-URI change. */
static const char *cell_link_uri(const TimuiCellBuffer *buf, uint32_t id){
    return (id && buf->links && id <= (uint32_t)buf->link_count) ? buf->links[id - 1].uri : NULL;
}
static int uri_eq(const char *a, const char *b){
    if(a == b) return 1;
    if(!a || !b) return 0;
    return strcmp(a, b) == 0;
}
TIMUI_API void timui_render_diff(TimuiTransport *t, const TimuiCellBuffer *prev,
                                 const TimuiCellBuffer *curr, TimuiRenderer *r){
    int x, y, w, h;
    if(!t || !prev || !curr || !r) return;
    w = prev->w < curr->w ? prev->w : curr->w;
    h = prev->h < curr->h ? prev->h : curr->h;
    for(y = 0; y < h; y++){
        for(x = 0; x < w; x++){
            const TimuiCell *pc = &prev->cells[(size_t)y * prev->w + x];
            const TimuiCell *cc = &curr->cells[(size_t)y * curr->w + x];
            char gb[4];
            int gn;
            /* A wide glyph's continuation cell is owned by its lead cell
             * (which carries width 2 and advances the cursor two columns).
             * Never emit it — line 336 would render codepoint 0 as a space,
             * clobbering the right half of a just-drawn wide glyph. */
            if(cc->flags & TIMUI_CELL_CONTINUATION) continue;
            if(pc->codepoint == cc->codepoint && pc->fg == cc->fg &&
               pc->bg == cc->bg && pc->attrs == cc->attrs &&
               pc->width == cc->width &&
               (pc->flags & TIMUI_CELL_CONTINUATION) == (cc->flags & TIMUI_CELL_CONTINUATION) &&
               uri_eq(cell_link_uri(prev, pc->hyperlink_id), cell_link_uri(curr, cc->hyperlink_id))) continue;
            if(r->last_x != x || r->last_y != y) emit_cup(t, x, y);
            emit_sgr(t, r, cc);
            {   /* OSC 8: emit on a link-state change (open/close) or a URI
                 * change. ids are per-frame, so cache the open URI string and
                 * track whether a link is currently open (have_last_link). */
                const char *curi = cell_link_uri(curr, cc->hyperlink_id);
                int want = curi != NULL;
                if(want != r->have_last_link || (want && !uri_eq(curi, r->last_link_uri))){
                    emit_osc8(t, curi);
                    r->last_link = (int)cc->hyperlink_id;
                    r->have_last_link = want;
                    if(curi){
                        size_t ul = strlen(curi);
                        if(ul >= sizeof r->last_link_uri) ul = sizeof r->last_link_uri - 1;
                        memcpy(r->last_link_uri, curi, ul);
                        r->last_link_uri[ul] = '\0';
                    }else r->last_link_uri[0] = '\0';
                }
            }
            gn = timui_utf8_encode_(cc->codepoint ? cc->codepoint : ' ', gb);
            r_emit(t, gb, (size_t)gn);
            r->last_x = x + (cc->width >= 2 ? 2 : 1);   /* wide glyph advances cursor by 2 */
            r->last_y = y;
        }
    }
    if(t->flush) t->flush(t);
}
TIMUI_API void timui_render_cursor(TimuiTransport *t, int x, int y, int visible){
    if(!t) return;
    if(visible){
        if(x >= 0 && y >= 0) emit_cup(t, x, y);   /* L5: skip bogus CUP on negative coords */
        R_EMIT(t, "\x1b[?25h");
    } else {
        R_EMIT(t, "\x1b[?25l");
    }
}

/* ---- style/theme system ----------------------------------------------- */
static TimuiStyle th_mk(uint32_t fg, uint32_t bg){
    TimuiStyle s; s.fg = fg; s.bg = bg; s.attrs = 0; return s;
}
TIMUI_API TimuiTheme timui_theme_builtin(TimuiBuiltinTheme t){
    TimuiTheme th;
    int i;
    /* default (MONO): white-on-black */
    for(i = 0; i < TIMUI_SLOT_COUNT; i++){ th.slots[i].fg = 0xFFFFFF; th.slots[i].bg = 0x000000; th.slots[i].attrs = 0; }
    if(t == TIMUI_THEME_DOS_BLUE){
        uint32_t blue = 0x0000AA, white = 0xFFFFFF, cyan = 0x00FFFF, gray = 0xAAAAAA;
        th.slots[TIMUI_SLOT_TEXT]          = th_mk(white, blue);
        th.slots[TIMUI_SLOT_TEXT_DIM]      = th_mk(gray,  blue);
        th.slots[TIMUI_SLOT_PANEL]         = th_mk(white, blue);
        th.slots[TIMUI_SLOT_PANEL_TITLE]   = th_mk(cyan,  blue);
        th.slots[TIMUI_SLOT_BORDER]        = th_mk(white, blue);
        th.slots[TIMUI_SLOT_BUTTON]        = th_mk(0x000000, gray);
        th.slots[TIMUI_SLOT_BUTTON_HOVERED]= th_mk(0x000000, 0xDDDDDD);
        th.slots[TIMUI_SLOT_BUTTON_FOCUSED]= th_mk(white, 0x555555);
        th.slots[TIMUI_SLOT_BUTTON_ACTIVE] = th_mk(0x000000, white);
        th.slots[TIMUI_SLOT_INPUT]         = th_mk(white, 0x000000);
        th.slots[TIMUI_SLOT_INPUT_FOCUSED] = th_mk(white, 0x333333);
        th.slots[TIMUI_SLOT_SELECTION]     = th_mk(white, 0x5555FF);
        th.slots[TIMUI_SLOT_MENU]          = th_mk(white, blue);
        th.slots[TIMUI_SLOT_MENU_ACTIVE]   = th_mk(0x000000, gray);
        th.slots[TIMUI_SLOT_STATUS]        = th_mk(white, 0x000055);
        th.slots[TIMUI_SLOT_ERROR]         = th_mk(0xFF5555, blue);
        th.slots[TIMUI_SLOT_WARNING]       = th_mk(0xFFFF55, blue);
        th.slots[TIMUI_SLOT_SUCCESS]       = th_mk(0x55FF55, blue);
    } else if(t == TIMUI_THEME_DOS_GRAY){
        uint32_t gray = 0xAAAAAA, black = 0x000000, white = 0xFFFFFF;
        th.slots[TIMUI_SLOT_TEXT]          = th_mk(black, gray);
        th.slots[TIMUI_SLOT_TEXT_DIM]      = th_mk(0x555555, gray);
        th.slots[TIMUI_SLOT_PANEL]         = th_mk(black, gray);
        th.slots[TIMUI_SLOT_PANEL_TITLE]   = th_mk(white, 0x555555);
        th.slots[TIMUI_SLOT_BORDER]        = th_mk(black, gray);
        th.slots[TIMUI_SLOT_BUTTON]        = th_mk(black, white);
        th.slots[TIMUI_SLOT_BUTTON_HOVERED]= th_mk(black, 0xDDDDDD);
        th.slots[TIMUI_SLOT_BUTTON_FOCUSED]= th_mk(white, 0x555555);
        th.slots[TIMUI_SLOT_BUTTON_ACTIVE] = th_mk(white, black);   /* G13: pressed = inverted (was == BUTTON) */
        th.slots[TIMUI_SLOT_INPUT]         = th_mk(black, white);
        th.slots[TIMUI_SLOT_INPUT_FOCUSED] = th_mk(white, 0x555555);
        th.slots[TIMUI_SLOT_STATUS]        = th_mk(white, 0x555555);
        th.slots[TIMUI_SLOT_ERROR]         = th_mk(white, 0xAA0000);
        th.slots[TIMUI_SLOT_WARNING]       = th_mk(black, 0xAAAA00);
        th.slots[TIMUI_SLOT_SUCCESS]       = th_mk(black, 0x00AA00);
        th.slots[TIMUI_SLOT_SELECTION]     = th_mk(white, 0x555555);
        th.slots[TIMUI_SLOT_MENU]          = th_mk(black, 0xCCCCCC);
        th.slots[TIMUI_SLOT_MENU_ACTIVE]   = th_mk(black, white);
    } else if(t == TIMUI_THEME_MODERN_DARK){
        uint32_t bg = 0x1E1E2E, fg = 0xCDD6F4, accent = 0x89B4FA;
        for(i = 0; i < TIMUI_SLOT_COUNT; i++){ th.slots[i].fg = fg; th.slots[i].bg = bg; }
        th.slots[TIMUI_SLOT_TEXT_DIM]      = th_mk(0x9399B2, bg);        /* G13: dimmer than body */
        th.slots[TIMUI_SLOT_PANEL_TITLE]   = th_mk(accent, bg);
        th.slots[TIMUI_SLOT_BORDER]        = th_mk(0x585B70, bg);
        th.slots[TIMUI_SLOT_BUTTON]        = th_mk(fg, 0x313244);
        th.slots[TIMUI_SLOT_BUTTON_HOVERED]= th_mk(fg, 0x45475A);
        th.slots[TIMUI_SLOT_BUTTON_FOCUSED]= th_mk(0x1E1E2E, accent);
        th.slots[TIMUI_SLOT_BUTTON_ACTIVE] = th_mk(0x1E1E2E, 0xB4BEFE);  /* G13: brighter than focus */
        th.slots[TIMUI_SLOT_INPUT]         = th_mk(fg, 0x313244);
        th.slots[TIMUI_SLOT_INPUT_FOCUSED] = th_mk(fg, 0x45475A);
        th.slots[TIMUI_SLOT_SELECTION]     = th_mk(0x1E1E2E, accent);
        th.slots[TIMUI_SLOT_MENU]          = th_mk(fg, 0x313244);        /* G13: menu-bar surface */
        th.slots[TIMUI_SLOT_MENU_ACTIVE]   = th_mk(0x1E1E2E, accent);    /* G13: highlighted item */
        th.slots[TIMUI_SLOT_STATUS]        = th_mk(fg, 0x45475A);        /* G13: status-bar surface */
        th.slots[TIMUI_SLOT_ERROR]         = th_mk(0xF38BA8, bg);
        th.slots[TIMUI_SLOT_WARNING]       = th_mk(0xFAB387, bg);
        th.slots[TIMUI_SLOT_SUCCESS]       = th_mk(0xA6E3A1, bg);
    }
    else if(t == TIMUI_THEME_MODERN_LIGHT){
        uint32_t bg = 0xFFFFFF, fg = 0x2E2E2E, accent = 0x0066CC;
        for(i = 0; i < TIMUI_SLOT_COUNT; i++){ th.slots[i].fg = fg; th.slots[i].bg = bg; }
        th.slots[TIMUI_SLOT_TEXT_DIM]      = th_mk(0x777777, bg);        /* G13: dimmer than body */
        th.slots[TIMUI_SLOT_PANEL_TITLE]   = th_mk(accent, bg);
        th.slots[TIMUI_SLOT_BORDER]        = th_mk(0xCCCCCC, bg);
        th.slots[TIMUI_SLOT_BUTTON]        = th_mk(fg, 0xE0E0E0);
        th.slots[TIMUI_SLOT_BUTTON_HOVERED]= th_mk(fg, 0xD0D0D0);
        th.slots[TIMUI_SLOT_BUTTON_FOCUSED]= th_mk(0xFFFFFF, accent);
        th.slots[TIMUI_SLOT_BUTTON_ACTIVE] = th_mk(0xFFFFFF, 0x004C99);  /* G13: darker than focus */
        th.slots[TIMUI_SLOT_INPUT]         = th_mk(fg, 0xF0F0F0);
        th.slots[TIMUI_SLOT_INPUT_FOCUSED] = th_mk(fg, 0xE0E0E0);
        th.slots[TIMUI_SLOT_SELECTION]     = th_mk(0xFFFFFF, accent);
        th.slots[TIMUI_SLOT_MENU]          = th_mk(fg, 0xF0F0F0);        /* G13: menu-bar surface */
        th.slots[TIMUI_SLOT_MENU_ACTIVE]   = th_mk(0xFFFFFF, accent);    /* G13: highlighted item */
        th.slots[TIMUI_SLOT_STATUS]        = th_mk(fg, 0xE0E0E0);        /* G13: status-bar surface */
        th.slots[TIMUI_SLOT_ERROR]         = th_mk(0xCC0000, bg);
        th.slots[TIMUI_SLOT_WARNING]       = th_mk(0xCC6600, bg);
        th.slots[TIMUI_SLOT_SUCCESS]       = th_mk(0x008800, bg);
    }
    else if(t == TIMUI_THEME_MONO){
        /* Monochrome: no colour to spend, so interactive state is conveyed with
         * SGR attributes (dim/bold/reverse) — the idiomatic mono-terminal cue,
         * emitted by emit_sgr. G13: every state slot stays visually distinct
         * from its resting base atop the white-on-black default set above. */
        th.slots[TIMUI_SLOT_TEXT_DIM].attrs       = TIMUI_ATTR_DIM;
        th.slots[TIMUI_SLOT_PANEL_TITLE].attrs    = TIMUI_ATTR_BOLD;
        th.slots[TIMUI_SLOT_BUTTON_FOCUSED].attrs = TIMUI_ATTR_BOLD;
        th.slots[TIMUI_SLOT_BUTTON_ACTIVE].attrs  = TIMUI_ATTR_REVERSE;
        th.slots[TIMUI_SLOT_SELECTION].attrs      = TIMUI_ATTR_REVERSE;
        th.slots[TIMUI_SLOT_MENU_ACTIVE].attrs    = TIMUI_ATTR_REVERSE;
        th.slots[TIMUI_SLOT_STATUS].attrs         = TIMUI_ATTR_REVERSE;
    }
    /* An out-of-range enum falls through to the white-on-black default above. */
    return th;
}
TIMUI_API TimuiStyle timui_theme_style(const TimuiTheme *th, TimuiStyleSlot slot){
    if(!th || slot < 0 || slot >= TIMUI_SLOT_COUNT){
        TimuiStyle z = {0, 0, 0};
        return z;
    }
    return th->slots[slot];
}
#undef R_EMIT   /* Z10: impl-only macro must not leak into the consumer TU */

/* ---- terminal transport + fake backend --------------------------------- */
static int fake_write(TimuiTransport *t, const void *data, size_t len){
    TimuiFakeTransport *f = (TimuiFakeTransport *)t->ctx;
    if(len > SIZE_MAX - f->out_len) return -1;        /* out_len+len would overflow */
    if(f->out_len + len > f->out_cap){
        size_t ncap = f->out_cap ? f->out_cap : 64;
        size_t target = f->out_len + len;
        unsigned char *nb;
        while(ncap < target){ if(ncap > SIZE_MAX / 2) return -1; ncap *= 2; }
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
        /* Sanitize at the codepoint level: decode UTF-8 and drop C0/DEL/C1
         * control CODEPOINTS — incl. U+009C (the C1 String Terminator, UTF-8
         * C2 9C) that closes an OSC like BEL or ESC \ — re-encoding printable
         * codepoints. A byte-level reject of 0x80-0x9f (an earlier attempt)
         * stripped VALID UTF-8 continuation bytes (Ü = C3 9C, 字 = E5 AD 97);
         * filtering by decoded codepoint keeps multibyte text intact while
         * still removing only control codepoints. Single buffered write. */
        char clean[128];
        size_t cn = 0, i = 0;
        while(i < title.len && cn + 4 < sizeof(clean)){
            uint32_t cp = 0;
            int adv = timui_utf8_decode(title.ptr + i, title.len - i, &cp);
            if(adv <= 0){ i++; continue; }                  /* incomplete lead: skip */
            if(cp >= 0x20 && cp != 0x7f && !(cp >= 0x80 && cp <= 0x9f))
                cn += (size_t)timui_utf8_encode_(cp, clean + cn);  /* keep printable cp */
            i += (size_t)adv;
        }
        if(cn > 0){   /* skip OSC entirely if all chars were stripped */
            TIMUI_EMIT(t, "\x1b]0;");
            if(t && t->write) (void)t->write(t, clean, cn);
            TIMUI_EMIT(t, "\x07");
        }
    }
    /* Disable auto-wrap (DECAWM off) unconditionally: the diff renderer positions
     * every cell with its own CUP, so a glyph written to the last column must
     * stay put — with auto-wrap on it moves the cursor to the next line (or
     * scrolls the screen when that cell is the bottom-right), desyncing the
     * renderer from the terminal. This is standard for cell-based TUIs. */
    TIMUI_EMIT(t, "\x1b[?7l");
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
    TIMUI_EMIT(t, "\x1b[?7h");             /* restore auto-wrap on exit */
}

/* ---- terminal raw mode (POSIX) ---------------------------------------- */
/* Z25 test seam: when armed, timui_termios_enter takes its tcsetattr-failure
 * branch without calling tcsetattr (no portable way to fail a real fd's
 * tcsetattr while tcgetattr succeeds). This is a documented, test-only static —
 * inert (0) in production, the only mutable static here besides the SIGTERM
 * restore carve-out. */
static int g_tcsetattr_fail_for_test = 0;
TIMUI_API void timui_termios_fail_tcsetattr_for_test(int on){ g_tcsetattr_fail_for_test = on; }
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
    /* short-circuit keeps the real tcsetattr un-called when the seam is armed */
    if(g_tcsetattr_fail_for_test || tcsetattr(fd, TCSAFLUSH, &raw) != 0){
        free(orig); t->saved = NULL; t->have_saved = 0; return TIMUI_ERR_OS;
    }
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
    if(caps_is_modern(term_program) || caps_is_modern(term)){
        c->flags |= TIMUI_CAP_TRUECOLOR | TIMUI_CAP_256_COLOR | TIMUI_CAP_SGR_MOUSE
                  | TIMUI_CAP_BRACKETED_PASTE | TIMUI_CAP_FOCUS_EVENTS
                  | TIMUI_CAP_SYNC_OUTPUT | TIMUI_CAP_OSC8_HYPERLINKS;
        if(c->colors < 16777216) c->colors = 16777216;
        if(caps_is_kitty_family(term_program) || caps_is_kitty_family(term)){
            c->flags |= TIMUI_CAP_KITTY_KEYBOARD | TIMUI_CAP_KITTY_GRAPHICS | TIMUI_CAP_UNICODE_CORE;
        }
    } else if(term && strstr(term, "256color")){
        c->flags |= TIMUI_CAP_256_COLOR;
        c->colors = 256;
    }
    /* multiplexers reduce capabilities. Kitty GRAPHICS is ALWAYS stripped under a
     * multiplexer: it requires explicit tmux `allow-passthrough` + graphics
     * support we can't assume, and emitting APC graphics that tmux silently
     * drops leaves a grey placeholder region and stray cursor moves. Keyboard
     * and sync are only kept when the OUTER terminal (TERM_PROGRAM, inherited
     * into the session) is kitty-family; otherwise stripped. timui_force_cap
     * overrides either way (W12). */
    if(term && (!strncmp(term, "tmux", 4) || !strncmp(term, "screen", 6) || !strncmp(term, "zellij", 6))){
        c->flags &= ~TIMUI_CAP_KITTY_GRAPHICS;
        if(!caps_is_kitty_family(term_program))
            c->flags &= ~(TIMUI_CAP_KITTY_KEYBOARD | TIMUI_CAP_SYNC_OUTPUT);
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
#undef TIMUI_EMIT   /* Z10: impl-only macro must not leak into the consumer TU */

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
        /* wheel: button bits (0x03) give the direction; modifier bits
         * (Shift/Alt/Ctrl) must not erase it. Old exact-match (==64/==65)
         * dropped the delta for any modifier-tagged scroll. */
        int btn = code & 0x03;
        ev.as.mouse.wheel_y = (btn == 0) ? 1 : (btn == 1 ? -1 : 0);
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
    p->mod_param = 0; p->has_mod = 0; p->sub_param = 0;
    p->csi_mouse = 0; p->mcount = 0;
    p->mparam[0] = p->mparam[1] = p->mparam[2] = 0;
    p->pasting = 0; p->paste_ptr = NULL;
    p->utf8_need = 0; p->utf8_len = 0; p->utf8_cp = 0; p->utf8_ptr = NULL;
    p->now_ms = 0; p->esc_since_ms = 0;
    p->paste_tail_len = 0;
}
#define TIMUI_ESC_TIMEOUT_MS 50   /* lone-Esc resolution window */
TIMUI_API void timui_input_set_now(TimuiInputParser *p, uint64_t now_ms){
    if(p) p->now_ms = now_ms;
}
TIMUI_API void timui_input_flush_esc(TimuiInputParser *p, uint64_t now_ms, TimuiEventFn cb, void *ctx){
    if(!p) return;
    if(p->state == 1 &&
       now_ms - p->esc_since_ms >= TIMUI_ESC_TIMEOUT_MS){
        emit_key(cb, ctx, TIMUI_KEY_ESCAPE, 0, 0);
        p->state = 0;
        p->esc_since_ms = 0;
    }
}
TIMUI_API size_t timui_input_feed(TimuiInputParser *p, const void *data, size_t len,
                                  TimuiEventFn cb, void *ctx){
    const unsigned char *b = (const unsigned char *)data;
    size_t i, count = 0;
    if(!p || !b) return 0;
    if(p->state == 4) p->utf8_ptr = NULL;   /* crossed a feed boundary: no stable byte view */
    /* Handle deferred partial paste terminator from the previous feed */
    if(p->pasting && p->paste_tail_len > 0){
        static const unsigned char term[] = {0x1b,'[','2','0','1','~'};
        int need = 6 - p->paste_tail_len;
        int matched = 1, j;
        for(j = 0; j < need && j < (int)len; j++)
            if(b[j] != term[p->paste_tail_len + j]){ matched = 0; break; }
        if(matched && (int)len >= need){
            p->pasting = 0;             /* terminator completed across feeds */
            p->paste_tail_len = 0;
            b += need; len -= (size_t)need;
        } else if(matched){
            /* Still a prefix (terminator split across >2 feeds): every byte of
             * this feed extends the deferred terminator, so keep deferring
             * instead of flushing the tail as paste CONTENT (which would inject
             * the literal terminator bytes). Consume the whole feed; nothing
             * else to do. paste_tail has room (len < need = 6 - tail_len). */
            for(j = 0; j < (int)len; j++)
                p->paste_tail[p->paste_tail_len + j] = b[j];
            p->paste_tail_len += (int)len;
            return count;
        } else {
            /* not a terminator — emit deferred bytes as paste content */
            if(p->paste_tail_len > 0){
                emit_paste(cb, ctx, p->paste_tail, (size_t)p->paste_tail_len);
                count++;
            }
            p->paste_tail_len = 0;
        }
    }
    if(p->pasting) p->paste_ptr = (const unsigned char *)&b[0];
    for(i = 0; i < len; i++){
        unsigned char c = b[i];
        if(p->pasting){
            if(c == 0x1b){
                size_t remaining = len - i;
                if(remaining >= 6 &&
                   b[i+1] == '[' && b[i+2] == '2' && b[i+3] == '0' && b[i+4] == '1' && b[i+5] == '~'){
                    if(&b[i] > p->paste_ptr){   /* skip empty payload (back-to-back START/END) */
                        emit_paste(cb, ctx, p->paste_ptr, (size_t)(&b[i] - p->paste_ptr));
                        count++;
                    }
                    p->pasting = 0;
                    i += 5;
                } else {
                    /* Potential partial terminator — check prefix match */
                    static const unsigned char term[] = {0x1b,'[','2','0','1','~'};
                    int is_prefix = 1;
                    size_t j;
                    for(j = 0; j < remaining && j < 6; j++)
                        if(b[i+j] != term[j]){ is_prefix = 0; break; }
                    if(is_prefix && remaining < 6){
                        /* Defer: emit content up to here, save partial bytes */
                        if(&b[i] > p->paste_ptr){
                            emit_paste(cb, ctx, p->paste_ptr, (size_t)(&b[i] - p->paste_ptr));
                            count++;
                        }
                        p->paste_tail_len = (int)remaining;
                        for(j = 0; j < remaining; j++) p->paste_tail[j] = b[i+j];
                        p->paste_ptr = (const unsigned char *)&b[len];  /* prevent end-of-feed re-emit */
                        i = len;  /* exit the loop */
                        break;
                    }
                    /* Not a prefix — treat as paste content */
                }
            }
            continue;
        }
        switch(p->state){
        case 0: /* GROUND */
            if(c == 0x1b){ p->state = 1; p->esc_since_ms = p->now_ms; break; }
            if(c == '\r' || c == '\n'){ emit_key(cb, ctx, TIMUI_KEY_ENTER, 0, 0); count++; break; }
            if(c == '\t'){ emit_key(cb, ctx, TIMUI_KEY_TAB, 0, 0); count++; break; }
            if(c == 0x7f || c == 0x08){ emit_key(cb, ctx, TIMUI_KEY_BACKSPACE, 0, 0); count++; break; }
            if(c < 0x20){
                uint32_t cp;
                if(c == 0) break;   /* NUL: ignore (no phantom Ctrl-@ event) */
                cp = (c >= 1 && c <= 26) ? (uint32_t)('a' + c - 1) : (uint32_t)c;
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
                p->mod_param = 0; p->has_mod = 0; p->sub_param = 0;
                p->csi_mouse = 0; p->mcount = 0;
                p->mparam[0] = p->mparam[1] = p->mparam[2] = 0;
                break;
            }
            if(c == 'O'){ p->state = 3; break; }
            if(c == 0x1b){ emit_key(cb, ctx, TIMUI_KEY_ESCAPE, 0, 0); count++; p->esc_since_ms = p->now_ms; break; }
            if(c >= 0x20 && c < 0x80){
                emit_key(cb, ctx, TIMUI_KEY_UNKNOWN, TIMUI_MOD_ALT, (uint32_t)c);
                count++; p->state = 0; break;
            }
            emit_key(cb, ctx, TIMUI_KEY_ESCAPE, 0, 0); count++;
            p->state = 0;
            i--;   /* reprocess this byte in ground. NB: when i==0 this wraps to
                    * SIZE_MAX and the for-loop's i++ revisits b[0] — the old
                    * `if(i>0)` guard skipped the reprocess exactly at a feed
                    * boundary, silently dropping b[0]. */
            break;
        case 2: /* CSI */
            /* Z3: an ESC mid-CSI aborts the pending sequence and restarts a
             * fresh escape (ECMA-48), rather than resyncing to ground and
             * leaking the interrupted tail as text. */
            if(c == 0x1b){ p->state = 1; p->esc_since_ms = p->now_ms; break; }
            if(c == '<'){ p->csi_mouse = 1; p->mcount = 0; p->mparam[0] = p->mparam[1] = p->mparam[2] = 0; break; }
            if(c == '?' || c == '>' || c == '='){ break; }              /* private marker */
            /* Z4: ':' opens a sub-parameter (Kitty event-type / alternate-key
             * reports). timui does not use sub-parameters, so ignore their
             * digits until the next ';' or final byte — but stay in CSI state
             * so the base key is not dropped and the tail is not leaked. */
            if(c == ':'){ p->sub_param = 1; break; }
            if(c >= '0' && c <= '9'){
                if(p->sub_param){ break; }                              /* discard sub-parameter digits */
                if(p->csi_mouse){
                    if(p->mcount < 3 && p->mparam[p->mcount] < 99999)
                        p->mparam[p->mcount] = p->mparam[p->mcount] * 10 + (c - '0');
                } else if(p->has_mod){
                    if(p->mod_param < 99999) p->mod_param = p->mod_param * 10 + (c - '0');
                } else { if(p->param < 999999) p->param = p->param * 10 + (c - '0'); p->nparams = 1; }
                break;
            }
            if(c == ';'){
                p->sub_param = 0;                                       /* ';' ends any sub-parameter */
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
            /* Z3: an ESC here aborts the truncated SS3 and restarts a fresh
             * escape rather than being swallowed as a bogus final byte. */
            if(c == 0x1b){ p->state = 1; p->esc_since_ms = p->now_ms; break; }
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
                    /* Z2: reject overlong / surrogate / above-max exactly as the
                     * render decoder (timui_utf8_decode) does — otherwise an
                     * overlong C0 80 would emit codepoint 0 (a NUL injected into
                     * the app buffer, bypassing the V14 NUL guard). utf8_len is
                     * the total byte count (need+1). */
                    if((p->utf8_len == 2 && p->utf8_cp < 0x80) ||
                       (p->utf8_len == 3 && p->utf8_cp < 0x800) ||
                       (p->utf8_len == 4 && p->utf8_cp < 0x10000) ||
                       (p->utf8_cp >= 0xD800 && p->utf8_cp <= 0xDFFF) ||
                       p->utf8_cp > 0x10FFFF){
                        p->utf8_cp = 0xFFFD;
                    }
                    emit_text(cb, ctx, p->utf8_ptr, p->utf8_ptr ? (size_t)p->utf8_len : 0, p->utf8_cp);
                    count++; p->state = 0;
                }
                break;
            }
            /* invalid continuation: the partial lead sequence is ill-formed ->
             * one U+FFFD for it (NOT for b[i]); the offending byte may start
             * fresh input, so reprocess it in ground. The old code emitted
             * U+FFFD for b[i] itself and then (for i>0) reprocessed b[i],
             * double-emitting; at i==0 it dropped the reprocess entirely. */
            emit_text(cb, ctx, p->utf8_ptr,
                      p->utf8_ptr ? (size_t)(p->utf8_len - p->utf8_need) : 0, 0xFFFD);
            count++; p->state = 0;
            i--;   /* reprocess b[i] in ground (i==0 wraps; loop i++ revisits) */
            break;
        }
    }
    if(p->pasting){   /* paste ran to end of feed: emit the chunk accumulated so far */
        size_t plen = (size_t)(&b[len] - p->paste_ptr);
        if(plen > 0){ emit_paste(cb, ctx, p->paste_ptr, plen); count++; }
    }
    return count;
}
#undef TIMUI_ESC_TIMEOUT_MS   /* Z10: impl-only macro must not leak into the consumer TU */

/* ---- interaction state ------------------------------------------------ */
TIMUI_API void timui_interact_init(TimuiInteract *ia, const TimuiAllocator *alloc){
    if(!ia) return;
    ia->hot = ia->active = ia->focus = 0;
    ia->mouse_x = ia->mouse_y = 0;
    ia->mouse_down = ia->mouse_down_prev = 0;
    ia->mouse_pressed = ia->mouse_released = 0;
    ia->tab_pressed = ia->activate_pressed = 0;
    ia->tab_order = NULL;
    ia->tab_count = ia->tab_cap = 0;
    ia->alloc = alloc;          /* kept for growing tab_order on push (V24) */
    ia->focus_advance = 0;
    ia->modal_active = 0;
}
TIMUI_API void timui_interact_destroy(TimuiInteract *ia){
    if(!ia || !ia->tab_order || !ia->alloc) return;
    ia->alloc->free(ia->alloc->userdata, ia->tab_order, (size_t)ia->tab_cap * sizeof(TimuiId));
    ia->tab_order = NULL;
    ia->tab_cap = ia->tab_count = 0;
}
TIMUI_API void timui_interact_set_mouse(TimuiInteract *ia, int x, int y, int down){
    if(!ia) return;
    ia->mouse_x = x;
    ia->mouse_y = y;
    ia->mouse_down = down ? 1 : 0;
}
TIMUI_API void timui_interact_set_keys(TimuiInteract *ia, int tab, int activate){
    if(!ia) return;
    if(tab) ia->tab_pressed = 1;
    if(activate) ia->activate_pressed = 1;
}
TIMUI_API void timui_interact_begin(TimuiInteract *ia){
    if(!ia) return;
    ia->mouse_pressed  = ia->mouse_down && !ia->mouse_down_prev;
    ia->mouse_released = !ia->mouse_down && ia->mouse_down_prev;
    ia->mouse_down_prev = ia->mouse_down;
    ia->hot = 0;                 /* recomputed from this frame's submissions */
    ia->tab_count = 0;
    ia->focus_advance = ia->tab_pressed;
    ia->tab_pressed = 0;
    /* modal_active persists across frames; message_box re-asserts it each
     * frame it is called, and clears it on button click. When the caller
     * stops calling message_box, modal_active remains 1 — the caller must
     * set ui->ia.modal_active = 0 when dismissing the modal. */
}
TIMUI_API TimuiInteractResult timui_interact_button(TimuiInteract *ia, TimuiId id, TimuiRect r){
    TimuiInteractResult res = {0, 0, 0, 0, 0};
    int hover;
    if(!ia) return res;
    if(ia->modal_active){      /* modal focus trap: widgets behind the modal are inert */
        int in_m = (ia->mouse_x >= ia->modal_rect.x && ia->mouse_x < ia->modal_rect.x + ia->modal_rect.w &&
                   ia->mouse_y >= ia->modal_rect.y && ia->mouse_y < ia->modal_rect.y + ia->modal_rect.h);
        if(!in_m) return res;
    }
    hover = (ia->mouse_x >= r.x && ia->mouse_x < r.x + r.w &&
             ia->mouse_y >= r.y && ia->mouse_y < r.y + r.h);
    if(hover) ia->hot = id;
    if(hover && ia->mouse_pressed){ ia->active = id; ia->focus = id; }
    res.hovered = hover;
    res.focused = (ia->focus == id);
    res.active  = (ia->active == id);
    res.pressed = res.active && ia->mouse_down;
    if(res.active && ia->mouse_released){
        res.clicked = 1;        /* released over the active widget */
        ia->active = 0;
    }
    if(res.focused && ia->activate_pressed){
        res.clicked = 1;        /* Enter/Space activates the focused widget */
        ia->activate_pressed = 0;
    }
    /* Register in the Tab cycle (dynamically grown, V24 — no fixed cap). */
    if(ia->tab_count == ia->tab_cap){
        int ncap = ia->tab_cap ? ia->tab_cap * 2 : 16;
        TimuiId *n = NULL;
        if(ia->alloc && (size_t)ncap <= SIZE_MAX / sizeof(TimuiId))
            n = (TimuiId *)ia->alloc->realloc(ia->alloc->userdata, ia->tab_order,
                (size_t)ia->tab_cap * sizeof(TimuiId), (size_t)ncap * sizeof(TimuiId));
        if(!n) return res;             /* OOM or no allocator: skip (focus still works via click) */
        ia->tab_order = n;
        ia->tab_cap = ncap;
    }
    ia->tab_order[ia->tab_count++] = id;
    return res;
}
TIMUI_API void timui_interact_end(TimuiInteract *ia){
    int i, idx;
    if(!ia || !ia->focus_advance || ia->tab_count == 0) return;
    idx = -1;
    for(i = 0; i < ia->tab_count; i++)
        if(ia->tab_order[i] == ia->focus){ idx = i; break; }
    ia->focus = ia->tab_order[(idx + 1) % ia->tab_count];
}

/* ---- widgets ---------------------------------------------------------- */
TIMUI_API TimuiButtonResult timui_button(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr label){
    TimuiButtonResult br = {false, false, false, false};
    TimuiInteractResult ir;
    TimuiStyleSlot slot;
    TimuiStyle st;
    Timui *ui;
    if(!f || !f->ui) return br;
    ui = f->ui;
    ir = timui_interact_button(&ui->ia, id, r);
    br.clicked = ir.clicked;
    br.pressed = ir.pressed;
    br.hovered = ir.hovered;
    br.focused = ir.focused;
    slot = ir.active ? TIMUI_SLOT_BUTTON_ACTIVE
          : ir.hovered ? TIMUI_SLOT_BUTTON_HOVERED
          : ir.focused ? TIMUI_SLOT_BUTTON_FOCUSED
          : TIMUI_SLOT_BUTTON;
    st = timui_theme_style(&ui->theme, slot);
    timui_draw_fill(&ui->curr, r, st);
    timui_draw_text(&ui->curr, r.x + 1, r.y + (r.h > 1 ? (r.h - 1) / 2 : 0), label, st);
    return br;
}
TIMUI_API void timui_label(TimuiFrame *f, int x, int y, TimuiStr text, TimuiStyle style){
    Timui *ui;
    if(!f || !f->ui) return;
    ui = f->ui;
    timui_draw_text(&ui->curr, x, y, text, style);
}
TIMUI_API TimuiRect timui_panel_begin(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr title, uint32_t border_flags){
    TimuiRect body = {0, 0, 0, 0};
    Timui *ui;
    (void)id;
    if(!f || !f->ui) return body;
    ui = f->ui;
    timui_draw_box(&ui->curr, r, border_flags, timui_theme_style(&ui->theme, TIMUI_SLOT_BORDER));
    timui_push_clip(f, r);   /* W10: clip title + body content to the panel rect */
    if(title.ptr && title.len)
        timui_draw_text(&ui->curr, r.x + 1, r.y, title, timui_theme_style(&ui->theme, TIMUI_SLOT_PANEL_TITLE));
    body.x = r.x + 1; body.y = r.y + 1;
    body.w = r.w - 2; body.h = r.h - 2;
    if(body.w < 0) body.w = 0;
    if(body.h < 0) body.h = 0;
    timui_draw_fill(&ui->curr, body, timui_theme_style(&ui->theme, TIMUI_SLOT_PANEL));
    return body;
}
TIMUI_API void timui_panel_end(TimuiFrame *f){ if(f) timui_pop_clip(f); }
static TimuiBoolEdit bool_widget(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr label,
                                 bool value, int is_radio){
    TimuiBoolEdit be = {false, value, false, false};
    TimuiInteractResult ir;
    Timui *ui;
    TimuiStyle st;
    char box[4];
    if(!f || !f->ui) return be;
    ui = f->ui;
    ir = timui_interact_button(&ui->ia, id, r);
    be.hovered = ir.hovered;
    be.focused = ir.focused;
    if(ir.clicked){
        be.changed = true;
        be.value = is_radio ? true : !value;   /* radio selects; checkbox toggles */
    }
    st = timui_theme_style(&ui->theme, ir.focused ? TIMUI_SLOT_INPUT_FOCUSED : TIMUI_SLOT_INPUT);
    box[0] = is_radio ? '(' : '[';
    box[1] = value ? (is_radio ? 'o' : 'x') : ' ';
    box[2] = is_radio ? ')' : ']';
    box[3] = ' ';
    timui_draw_text(&ui->curr, r.x, r.y, (TimuiStr){ box, 4 }, st);
    timui_draw_text(&ui->curr, r.x + 4, r.y, label, timui_theme_style(&ui->theme, TIMUI_SLOT_TEXT));
    return be;
}
TIMUI_API TimuiBoolEdit timui_checkbox(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr label, bool value){
    return bool_widget(f, id, r, label, value, 0);
}
TIMUI_API bool timui_checkbox_mut(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr label, bool *value){
    TimuiBoolEdit be;
    if(!value) return false;
    be = timui_checkbox(f, id, r, label, *value);
    if(be.changed) *value = be.value;
    return *value;
}
TIMUI_API TimuiBoolEdit timui_radio(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr label, bool selected){
    return bool_widget(f, id, r, label, selected, 1);
}
TIMUI_API void timui_function_bar(TimuiFrame *f, TimuiRect r, TimuiStr text){
    Timui *ui;
    if(!f || !f->ui) return;
    ui = f->ui;
    timui_draw_fill(&ui->curr, r, timui_theme_style(&ui->theme, TIMUI_SLOT_STATUS));
    timui_draw_text(&ui->curr, r.x, r.y, text, timui_theme_style(&ui->theme, TIMUI_SLOT_STATUS));
}
/* ---- UTF-8 codepoint helpers (shared with timui_text_area) ------------- *
 * text_in carries UTF-8 (since the G8 fix), so text inputs must append and
 * delete whole codepoints — a byte-wise append splits a multibyte char at the
 * cap boundary, and a 1-byte backspace leaves a dangling lead byte. Both
 * corrupt the buffer into permanently invalid UTF-8. */

/* byte length of a well-formed UTF-8 sequence starting at lead byte b (1..4),
 * or 0 if b is not a lead. */
static int utf8_lead_len(unsigned char b){
    if(b < 0x80) return 1;
    if((b & 0xE0) == 0xC0) return 2;
    if((b & 0xF0) == 0xE0) return 3;
    if((b & 0xF8) == 0xF0) return 4;
    return 0;
}
/* New length after removing one complete UTF-8 codepoint from the end of
 * buf[0..len): walk back over trailing continuation bytes (0x80-0xBF) to the
 * lead byte, then drop the lead. */
static size_t utf8_drop_last(const char *buf, size_t len){
    size_t i = len;
    while(i > 0 && ((unsigned char)buf[i - 1] & 0xC0) == 0x80) i--;
    if(i > 0) i--;
    return i;
}
/* ---- in-line editing primitives (F1.2) --------------------------------- *
 * All operate on a NUL-terminated buffer; utf8_drop_last(buf, cursor) already
 * gives the previous codepoint boundary (Left / Backspace). */

/* Byte offset after the codepoint at `cursor`, clamped to len (Right / Delete). */
static size_t utf8_next_(const char *buf, size_t cursor, size_t len){
    size_t step;
    if(cursor >= len) return len;
    step = (size_t)utf8_lead_len((unsigned char)buf[cursor]);
    if(step == 0) step = 1;                       /* stray byte: advance one */
    return (cursor + step > len) ? len : cursor + step;
}
/* Start of the line containing `pos` (after the preceding \n/\r, or 0). */
static size_t line_start_(const char *buf, size_t pos){
    while(pos > 0 && buf[pos - 1] != '\n' && buf[pos - 1] != '\r') pos--;
    return pos;
}
/* End of the line containing `pos` (before the next \n/\r, or end). */
static size_t line_end_(const char *buf, size_t pos){
    size_t len = strlen(buf);
    while(pos < len && buf[pos] != '\n' && buf[pos] != '\r') pos++;
    return pos;
}
/* Insert `n` bytes at byte offset `at`. Returns 1 on success, 0 if it won't fit
 * (len + n + 1 > cap). The tail (incl. the NUL) is shifted right. Callers pass
 * whole codepoints so nothing is split at the cap boundary. */
static int text_insert_(char *buf, size_t cap, size_t at, const char *bytes, size_t n){
    size_t len = strlen(buf);
    if(at > len) at = len;
    if(len + n + 1 > cap) return 0;
    memmove(buf + at + n, buf + at, len - at + 1);   /* +1 also moves the NUL */
    memcpy(buf + at, bytes, n);
    return 1;
}
/* Erase byte range [from, to). Returns the new cursor (= clamped `from`). */
static size_t text_erase_(char *buf, size_t from, size_t to){
    size_t len = strlen(buf);
    if(from > len) from = len;
    if(to > len) to = len;
    if(to <= from) return from;
    memmove(buf + from, buf + to, len - to + 1);     /* +1 also moves the NUL */
    return from;
}

TIMUI_API bool timui_input_line_buf(TimuiFrame *f, TimuiId id, TimuiRect r, char *buf, size_t cap){
    Timui *ui;
    TimuiInteractResult ir;
    TimuiStyle st;
    size_t len;
    bool submitted = false;
    if(!f || !f->ui || !buf || cap == 0) return false;
    ui = f->ui;
    {
        int submit = ui->ia.activate_pressed;   /* capture before interact_button consumes it */
        ir = timui_interact_button(&ui->ia, id, r);   /* click to focus */
        len = strlen(buf);
        if(ir.focused){
            int i = 0;
            /* append whole UTF-8 codepoints; skip one that won't fit intact */
            while(i < ui->text_in_len){
                size_t m = (size_t)utf8_lead_len((unsigned char)ui->text_in[i]);
                if(m == 0) m = 1;                       /* defensive: stray byte */
                if(len + m >= cap) break;               /* no room for the codepoint + NUL */
                while(m-- > 0 && i < ui->text_in_len) buf[len++] = ui->text_in[i++];
            }
            buf[len] = '\0';
            if((ui->key_in & TIMUI_KEYIN_BACKSPACE) && len > 0){
                len = utf8_drop_last(buf, len);         /* delete a whole codepoint */
                buf[len] = '\0';
            }
            if(submit) submitted = true;
            ui->text_in_len = 0;       /* consumed by the focused input */
            ui->key_in = 0;
        }
    }
    st = timui_theme_style(&ui->theme, ir.focused ? TIMUI_SLOT_INPUT_FOCUSED : TIMUI_SLOT_INPUT);
    timui_draw_fill(&ui->curr, r, st);
    timui_draw_text(&ui->curr, r.x, r.y, timui_str_from_cstr(buf), st);
    return submitted;
}
/* Display column of the cursor: sum of glyph widths over buf[0..upto) (F1.5). */
static int display_col_(const char *buf, size_t upto){
    size_t i = 0, len = strlen(buf);
    int col = 0;
    if(upto > len) upto = len;
    while(i < upto){
        uint32_t cp = 0;
        int adv = timui_utf8_decode(buf + i, len - i, &cp);
        if(adv <= 0) adv = 1;
        col += timui_utf8_width(cp);
        i += (size_t)adv;
    }
    return col;
}
/* Row (0-based, split on \n / \r / \r\n) and display column of the cursor,
 * for placing the hardware cursor in a multi-line editor (F1.4). */
static void text_pos_(const char *buf, size_t cursor, int *out_row, int *out_col){
    size_t i = 0, len = strlen(buf), line_start = 0;
    int row = 0;
    if(cursor > len) cursor = len;
    while(i < cursor){
        if(buf[i] == '\n' || buf[i] == '\r'){
            if(buf[i] == '\r' && i + 1 < cursor && buf[i + 1] == '\n') i++;
            row++; i++; line_start = i;
        } else i++;
    }
    *out_row = row;
    *out_col = display_col_(buf + line_start, cursor - line_start);
}
static bool input_field_core(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiInputState *st,
                             const TimuiStyle *ovr){
    Timui *ui;
    TimuiInteractResult ir;
    TimuiStyle style;
    bool submitted = false;
    if(!f || !f->ui || !st || !st->text || st->cap == 0) return false;
    ui = f->ui;
    if(st->cursor >= st->cap) st->cursor = st->cap - 1;   /* Y1-style: distrust caller cursor */
    {
        ir = timui_interact_button(&ui->ia, id, r);
        if(ir.focused){
            /* Insert typed text UP TO the first Enter this frame; on an Enter,
             * submit and DEFER the post-Enter tail (and any further Enters) to
             * the next frame — one submit per frame, so a burst "a\rb\r" yields
             * "a" then "b" instead of the merged "ab". */
            int first_enter = (ui->enter_count > 0) ? ui->enter_at[0] : -1;
            int upto = (first_enter >= 0) ? first_enter : ui->text_in_len;
            int j = 0;
            size_t len;
            if(upto > ui->text_in_len) upto = ui->text_in_len;
            while(j < upto){
                int n = utf8_lead_len((unsigned char)ui->text_in[j]);
                size_t m = (size_t)(n > 0 ? n : 1);
                if(j + (int)m > upto) m = (size_t)(upto - j);
                if(!text_insert_(st->text, st->cap, st->cursor, ui->text_in + j, m)) break;
                st->cursor += m; j += (int)m;
            }
            len = strlen(st->text);
            if(ui->key_in & TIMUI_KEYIN_LEFT)  st->cursor = utf8_drop_last(st->text, st->cursor);
            if(ui->key_in & TIMUI_KEYIN_RIGHT) st->cursor = utf8_next_(st->text, st->cursor, len);
            if(ui->key_in & TIMUI_KEYIN_HOME)  st->cursor = 0;              /* single line */
            if(ui->key_in & TIMUI_KEYIN_END)   st->cursor = len;
            if((ui->key_in & TIMUI_KEYIN_BACKSPACE) && st->cursor > 0){
                size_t prev = utf8_drop_last(st->text, st->cursor);
                st->cursor = text_erase_(st->text, prev, st->cursor);
            }
            if(ui->key_in & TIMUI_KEYIN_DELETE){
                size_t nxt = utf8_next_(st->text, st->cursor, strlen(st->text));
                (void)text_erase_(st->text, st->cursor, nxt);
            }
            if(ui->key_in & TIMUI_KEYIN_KILL_EOL){          /* Ctrl-K: cursor..end */
                st->text[st->cursor] = '\0';                /* cursor is a codepoint boundary */
            }
            if(ui->key_in & TIMUI_KEYIN_KILL_BOL){          /* Ctrl-U: start..cursor */
                size_t rest = strlen(st->text + st->cursor);
                memmove(st->text, st->text + st->cursor, rest + 1);
                st->cursor = 0;
            }
            if(ui->key_in & TIMUI_KEYIN_KILL_WORD){         /* Ctrl-W: the word before the cursor */
                size_t c = st->cursor, w = c;
                while(w > 0 && st->text[w-1] == ' ') w--;    /* trailing spaces */
                while(w > 0 && st->text[w-1] != ' ') w--;    /* the word */
                memmove(st->text + w, st->text + c, strlen(st->text + c) + 1);
                st->cursor = w;
            }
            if(first_enter >= 0){
                int tail = ui->text_in_len - upto, k;
                submitted = true;
                if(tail < 0) tail = 0;
                if(tail > (int)sizeof(ui->pending_in)) tail = (int)sizeof(ui->pending_in);
                memcpy(ui->pending_in, ui->text_in + upto, (size_t)tail);
                ui->pending_in_len = tail;
                ui->pending_enter_count = ui->enter_count - 1;
                for(k = 0; k < ui->pending_enter_count; k++)
                    ui->pending_enter_at[k] = ui->enter_at[k + 1] - upto;
            }
            ui->text_in_len = 0;
            ui->enter_count = 0;
            ui->key_in = 0;
        }
    }
    /* horizontal scroll: keep the cursor column within [scroll_x, scroll_x+w) */
    { int ccol = display_col_(st->text, st->cursor);
      if(ccol < st->scroll_x) st->scroll_x = ccol;
      if(r.w > 0 && ccol >= st->scroll_x + r.w) st->scroll_x = ccol - r.w + 1;
      if(st->scroll_x < 0) st->scroll_x = 0;
      if(ir.focused){                                 /* F1.4: request the hardware cursor */
          ui->cursor_x = r.x + (ccol - st->scroll_x);
          ui->cursor_y = r.y;
          ui->cursor_visible = 1;
      }
    }
    style = ovr ? *ovr
                : timui_theme_style(&ui->theme, ir.focused ? TIMUI_SLOT_INPUT_FOCUSED : TIMUI_SLOT_INPUT);
    timui_draw_fill(&ui->curr, r, style);
    /* clip to the field and shift the text left by scroll_x so the visible
     * window tracks the cursor (put_glyph drops the clipped leading columns). */
    timui_push_clip(f, r);
    timui_draw_text(&ui->curr, r.x - st->scroll_x, r.y, timui_str_from_cstr(st->text), style);
    timui_pop_clip(f);
    return submitted;
}
/* Themed single-line editor (INPUT/INPUT_FOCUSED slots). */
TIMUI_API bool timui_input_field(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiInputState *st){
    return input_field_core(f, id, r, st, NULL);
}
/* Same, but drawn with a caller-supplied `style` (e.g. to blend the field into a
 * surrounding panel instead of the themed input box). Editing/cursor behaviour
 * is identical. */
TIMUI_API bool timui_input_field_styled(TimuiFrame *f, TimuiId id, TimuiRect r,
                                        TimuiInputState *st, TimuiStyle style){
    return input_field_core(f, id, r, st, &style);
}
TIMUI_API TimuiListResult timui_listbox(TimuiFrame *f, TimuiId id, TimuiRect r,
                                        TimuiListState state, int count, TimuiLabelFn label, void *userdata){
    TimuiListResult res;
    Timui *ui;
    TimuiInteractResult ir;
    int orig, i, visible;
    res.state_changed = 0; res.activated = 0; res.focused = 0;
    res.state = state; res.selected = state.selected;
    if(!f || !f->ui || count < 0) return res;
    ui = f->ui;
    /* Y3: clamp selection into range (siblings tree/table do this) so a stale
     * or mis-seeded selected (e.g. after the list shrinks) self-heals. */
    if(count == 0) state.selected = 0;
    else{ if(state.selected < 0) state.selected = 0; if(state.selected >= count) state.selected = count - 1; }
    orig = state.selected;
    ir = timui_interact_button(&ui->ia, id, r);
    res.focused = ir.focused;
    if(ir.focused){
        if((ui->key_in & TIMUI_KEYIN_UP) && state.selected > 0) state.selected--;
        if((ui->key_in & TIMUI_KEYIN_DOWN) && state.selected < count - 1) state.selected++;
    }
    visible = r.h > 0 ? r.h : 0;
    if(state.scroll < 0) state.scroll = 0;
    if(state.selected < state.scroll) state.scroll = state.selected;
    if(visible > 0 && state.selected >= state.scroll + visible) state.scroll = state.selected - visible + 1;
    if(state.scroll < 0) state.scroll = 0;
    /* upper-bound scroll so it can't outrun the list tail (keeps trailing
     * viewport rows filled instead of leaving an unstyled gap). */
    if(count <= visible) state.scroll = 0;
    else if(state.scroll > count - visible) state.scroll = count - visible;
    if(ir.clicked){
        int my = ui->ia.mouse_y - r.y;
        int idx = state.scroll + my;
        if(idx >= 0 && idx < count){ state.selected = idx; res.activated = 1; }
    }
    for(i = 0; i < visible; i++){
        int idx = state.scroll + i;
        TimuiStyleSlot slot;
        TimuiStyle st;
        const char *s;
        if(idx >= count) break;
        s = label ? label(userdata, idx) : "";
        slot = (idx == state.selected) ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT;
        st = timui_theme_style(&ui->theme, slot);
        timui_draw_row_(&ui->curr, TIMUI_RECT(r.x, r.y + i, r.w, 1), 0, timui_str_from_cstr(s), st);
    }
    if(state.selected != orig) res.state_changed = 1;
    res.state = state;
    res.selected = state.selected;
    return res;
}
TIMUI_API TimuiListResult timui_listbox_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
                                            TimuiListState *state, int count, TimuiLabelFn label, void *userdata){
    TimuiListResult res;
    TimuiListState empty = {0, 0};
    if(!state) return timui_listbox(f, id, r, empty, count, label, userdata);
    res = timui_listbox(f, id, r, *state, count, label, userdata);
    if(res.state_changed) *state = res.state;
    return res;
}
TIMUI_API int timui_message_box(TimuiFrame *f, TimuiId id, TimuiRect parent,
                                TimuiStr title, TimuiStr message,
                                const TimuiStr *buttons, int count){
    Timui *ui;
    int i, boxw, boxh, bx, by, btnx, clicked = -1;
    if(!f || !f->ui || count <= 0) return -1;
    ui = f->ui;
    boxw = (int)message.len + 4;
    { int btnw = 0; for(i = 0; i < count; i++) btnw += (int)buttons[i].len + 4; if(btnw > boxw) boxw = btnw; }
    if(boxw < 10) boxw = 10;
    if(boxw > parent.w - 2) boxw = parent.w - 2;   /* never exceed the parent */
    if(boxw < 2) boxw = 2;                         /* floor: never a negative/zero width */
    boxh = 5;
    if(boxh > parent.h - 2) boxh = parent.h - 2;
    if(boxh < 3) boxh = 3;
    bx = parent.x + (parent.w - boxw) / 2;
    by = parent.y + (parent.h - boxh) / 2;
    ui->ia.modal_active = 1;
    ui->ia.modal_rect = TIMUI_RECT(bx, by, boxw, boxh);
    timui_panel_begin(f, id, TIMUI_RECT(bx, by, boxw, boxh), title, TIMUI_BORDER_DOUBLE);
    timui_label(f, bx + 2, by + 1, message, timui_theme_style(&ui->theme, TIMUI_SLOT_TEXT));
    btnx = bx + 2;
    { int any_btn = 0;
      for(i = 0; i < count; i++){
        int maxw = (bx + boxw) - btnx;            /* remaining width inside the box */
        int w = (int)buttons[i].len + 2;
        TimuiRect br;
        if(maxw < 3) break;                        /* no room for even a minimal button */
        if(w > maxw) w = maxw;                      /* W3: clamp so the button fits+renders */
        br = TIMUI_RECT(btnx, by + boxh - 2, w, 1);
        if(timui_button(f, id + (TimuiId)(i + 1), br, buttons[i]).clicked){ clicked = i; ui->ia.modal_active = 0; }
        btnx += w + 1;
        any_btn = 1;
      }
      /* W3 residual: if the parent is so narrow that NO button could render,
       * don't pin modal_active — an undismissable modal would trap all input. */
      if(!any_btn) ui->ia.modal_active = 0;
    }
    timui_panel_end(f);   /* pop the clip panel_begin pushed */
    return clicked;
}
TIMUI_API void timui_label_hyperlink(TimuiFrame *f, int x, int y, TimuiStr text, const char *uri, TimuiStyle style){
    Timui *ui;
    uint32_t id;
    if(!f || !f->ui) return;
    ui = f->ui;
    id = uri ? timui_hyperlink_set(&ui->curr, uri) : 0;
    timui_draw_text_linked(&ui->curr, x, y, text, style, id);
}

/* ---- clip stack ------------------------------------------------------- *
 * push_clip intersects the active clip with rect (so nested panels shrink it);
 * pop_clip restores the previous. Drawing (put_glyph) skips cells outside the
 * active clip. Reset each frame in timui_begin. */
static TimuiRect clip_intersect(TimuiRect a, TimuiRect b){
    TimuiRect r;
    int x1 = a.x > b.x ? a.x : b.x;
    int y1 = a.y > b.y ? a.y : b.y;
    int x2 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    int y2 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    r.x = x1; r.y = y1;
    r.w = x2 > x1 ? x2 - x1 : 0;
    r.h = y2 > y1 ? y2 - y1 : 0;
    return r;
}
TIMUI_API void timui_push_clip(TimuiFrame *f, TimuiRect rect){
    Timui *ui;
    TimuiCellBuffer *b;
    TimuiRect active;
    if(!f || !f->ui) return;
    ui = f->ui;
    b = &ui->curr;
    if(ui->clip_count < 8){
        ui->clip_stack[ui->clip_count].clip = b->clip;
        ui->clip_stack[ui->clip_count].has_clip = b->has_clip;
        ui->clip_count++;
    }
    active = b->has_clip ? b->clip : TIMUI_RECT(0, 0, b->w, b->h);
    b->clip = clip_intersect(active, rect);
    b->has_clip = 1;
}
TIMUI_API void timui_pop_clip(TimuiFrame *f){
    Timui *ui;
    TimuiCellBuffer *b;
    if(!f || !f->ui) return;
    ui = f->ui;
    b = &ui->curr;
    if(ui->clip_count > 0){
        ui->clip_count--;
        b->clip = ui->clip_stack[ui->clip_count].clip;
        b->has_clip = ui->clip_stack[ui->clip_count].has_clip;
    }
}
/* ---- scroll view (v0.2) ----------------------------------------------- *
 * A clipped, scrollable viewport. scroll_begin pushes a clip to `viewport`
 * and returns a content rect shifted up by `scroll_y`; the app draws content
 * into that rect (items above/below the viewport are clipped away).
 * scroll_end pops the clip. The app adjusts scroll_y on arrow/wheel input. */
TIMUI_API TimuiRect timui_scroll_begin(TimuiFrame *f, TimuiRect viewport, int scroll_y){
    TimuiRect content = viewport;
    if(!f) return content;
    timui_push_clip(f, viewport);
    content.y -= scroll_y;
    return content;
}
TIMUI_API void timui_scroll_end(TimuiFrame *f){
    timui_pop_clip(f);
}
/* ---- clipboard (OSC 52, v0.2) ----------------------------------------- *
 * Set the terminal clipboard via OSC 52: ESC]52;c;<base64>ESC\. The base64
 * encoding is done by hand (no external dependency). */
static const char b64_tab[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static size_t b64_encode(const unsigned char *src, size_t len, char *dst, size_t cap){
    size_t i, j = 0;
    for(i = 0; i < len; i += 3){
        unsigned v = (unsigned)src[i] << 16;
        if(i + 1 < len) v |= (unsigned)src[i + 1] << 8;
        if(i + 2 < len) v |= (unsigned)src[i + 2];
        if(j + 4 > cap) return (size_t)-1;        /* cap too small */
        dst[j++] = b64_tab[(v >> 18) & 0x3F];
        dst[j++] = b64_tab[(v >> 12) & 0x3F];
        dst[j++] = (i + 1 < len) ? b64_tab[(v >> 6) & 0x3F] : '=';
        dst[j++] = (i + 2 < len) ? b64_tab[v & 0x3F] : '=';
    }
    return j;
}
TIMUI_API void timui_clipboard_set(TimuiTransport *t, TimuiStr text){
    size_t b64cap, total, b64len;
    char *buf, *full;
    if(!t || !t->write || !text.ptr || text.len == 0) return;
    if(text.len > (SIZE_MAX - 1) / 4) return;      /* base64 size would overflow size_t */
    b64cap = ((text.len + 2) / 3) * 4 + 1;
    { TimuiAllocator al = timui_default_allocator();
      buf = (char *)al.alloc(al.userdata, b64cap);
      if(!buf) return;
      b64len = b64_encode((const unsigned char *)text.ptr, text.len, buf, b64cap - 1);
      if(b64len > 0 && b64len != (size_t)-1){
          /* Build the full OSC 52 in one buffer and write in a single call */
          total = 7 + b64len + 2;
          full = (char *)al.alloc(al.userdata, total);
          if(full){
              memcpy(full, "\x1b]52;c;", 7);
              memcpy(full + 7, buf, b64len);
              memcpy(full + 7 + b64len, "\x1b\\", 2);
              (void)t->write(t, full, total);
              al.free(al.userdata, full, total);
          } else {
              /* fallback: three writes (better than nothing) */
              (void)t->write(t, "\x1b]52;c;", 7);
              (void)t->write(t, buf, b64len);
              (void)t->write(t, "\x1b\\", 2);
          }
      }
      al.free(al.userdata, buf, b64cap);
    }
}
/* ---- configurable keymaps (v0.2) -------------------------------------- *
 * A flat table of (key, mods, action) bindings. The app binds keys to its own
 * action enum values, then checks timui_keymap_hit each frame. Bindings with
 * non-zero mods use timui_key_pressed_mods (all requested mods must be
 * present); bindings with mods==0 use plain timui_key_pressed. */
TIMUI_API void timui_keymap_bind(TimuiKeymap *km, TimuiKey key, uint32_t mods, int action){
    if(!km || km->count >= (int)(sizeof(km->bindings) / sizeof(km->bindings[0]))) return;
    km->bindings[km->count].key = key;
    km->bindings[km->count].mods = mods;
    km->bindings[km->count].action = action;
    km->count++;
}
TIMUI_API int timui_keymap_hit(TimuiFrame *f, const TimuiKeymap *km, int action){
    int i;
    if(!f || !f->ui || !km) return 0;
    for(i = 0; i < km->count; i++){
        if(km->bindings[i].action == action){
            /* Y4: an action may be bound to several keys; test them all rather
             * than short-circuiting on the first binding (which made the 2nd
             * unreachable). */
            int hit = km->bindings[i].mods
                ? timui_key_pressed_mods(f, km->bindings[i].key, km->bindings[i].mods)
                : timui_key_pressed(f, km->bindings[i].key);
            if(hit) return 1;
        }
    }
    return 0;
}
/* ---- table widget (v0.2) ---------------------------------------------- *
 * A scrollable multi-column grid with a header row and row selection.
 * Controlled: takes TimuiTableState by value and returns the new state in the
 * result; timui_table_mut is the write-back convenience twin. */
TIMUI_API TimuiTableResult timui_table(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *headers, int ncols, int nrows, TimuiCellFn cell_fn, void *ud,
    TimuiTableState state){
    TimuiTableResult res;
    Timui *ui;
    int col, row, x, hdr_h = 1, cw, vis, orig;
    TimuiRect body, content;
    res.state = state; res.state_changed = 0; res.focused = 0;
    if(!f || !f->ui || ncols <= 0) return res;
    ui = f->ui;
    cw = r.w / ncols;
    vis = r.h - hdr_h;
    if(vis < 1) vis = 1;
    if(state.selected < 0) state.selected = 0;
    if(nrows > 0 && state.selected >= nrows) state.selected = nrows - 1;
    orig = state.selected;                 /* post-clamp: a pure clamp is not a change */
    if(state.scroll < 0) state.scroll = 0;
    if(state.selected < state.scroll) state.scroll = state.selected;
    if(state.selected >= state.scroll + vis) state.scroll = state.selected - vis + 1;
    /* header row */
    { TimuiStyle hs = timui_theme_style(&ui->theme, TIMUI_SLOT_PANEL_TITLE);
      x = r.x;
      for(col = 0; col < ncols; col++){
          TimuiStr h = (headers && headers[col].ptr) ? headers[col] : (TimuiStr){ NULL, 0 };
          timui_draw_row_(&ui->curr, TIMUI_RECT(x, r.y, cw, 1), 1, h, hs);   /* draw_text skips NULL */
          x += cw;
      }
    }
    /* data rows (scrollable) */
    body = TIMUI_RECT(r.x, r.y + hdr_h, r.w, r.h - hdr_h);
    content = timui_scroll_begin(f, body, state.scroll);
    for(row = 0; row < nrows; row++){
        TimuiStyle st = timui_theme_style(&ui->theme,
            row == state.selected ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT);
        x = content.x;
        for(col = 0; col < ncols; col++){
            const char *cell = cell_fn ? cell_fn(ud, row, col) : "";
            timui_draw_row_(&ui->curr, TIMUI_RECT(x, content.y + row, cw, 1), 1, timui_str_from_cstr(cell), st);
            x += cw;
        }
    }
    timui_scroll_end(f);
    /* keyboard nav only when focused — call interact_button once to avoid
     * double-registering the table id in the tab order */
    { TimuiInteractResult tir = timui_interact_button(&ui->ia, id, r);
      res.focused = tir.focused;
      if(tir.focused) state.selected = timui_updown_nav_(f, state.selected, nrows);
    }
    res.state = state;
    res.state_changed = (state.selected != orig);
    return res;
}
TIMUI_API TimuiTableResult timui_table_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *headers, int ncols, int nrows, TimuiCellFn cell_fn, void *ud,
    TimuiTableState *state){
    TimuiTableState in = state ? *state : (TimuiTableState){0, 0};
    TimuiTableResult res = timui_table(f, id, r, headers, ncols, nrows, cell_fn, ud, in);
    if(state && res.state_changed) *state = res.state;   /* write back only on a real change */
    return res;
}
/* ---- tree widget (v0.2) ----------------------------------------------- *
 * Renders a flat list of visible nodes (the app handles expand/collapse logic)
 * with depth indentation and expand markers. Selection via up/down + click.
 * Controlled: takes `selected` by value and returns the new selection in the
 * result; timui_tree_mut is the write-back convenience twin. */
TIMUI_API TimuiTreeResult timui_tree(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTreeNode *nodes, int count, int selected){
    TimuiTreeResult res;
    Timui *ui;
    int i, orig;
    TimuiRect content;
    res.selected = selected; res.state_changed = 0; res.focused = 0;
    if(!f || !f->ui || !nodes || count <= 0) return res;
    ui = f->ui;
    if(selected < 0) selected = 0;
    if(selected >= count) selected = count - 1;
    orig = selected;                       /* post-clamp: a pure clamp is not a change */
    content = timui_scroll_begin(f, r, 0);
    for(i = 0; i < count; i++){
        int y = content.y + i;
        char prefix[64];
        int pn = 0, j;
        TimuiStyle st = timui_theme_style(&ui->theme,
            i == selected ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT);
        /* indentation + expand marker. depth is app-supplied and unchecked, so
         * bound the loop to the buffer (reserve marker + space + NUL). */
        for(j = 0; j < nodes[i].depth && pn + 4 < (int)sizeof(prefix); j++){
            prefix[pn++] = ' '; prefix[pn++] = ' ';
        }
        if(nodes[i].has_children) prefix[pn++] = nodes[i].expanded ? '-' : '+';
        else prefix[pn++] = ' ';
        prefix[pn++] = ' ';
        prefix[pn] = '\0';
        timui_draw_row_(&ui->curr, TIMUI_RECT(content.x, y, r.w, 1), 0, timui_str_from_cstr(prefix), st);
        timui_draw_text(&ui->curr, content.x + pn, y, timui_str_from_cstr(nodes[i].label), st);
    }
    timui_scroll_end(f);
    /* keyboard nav only when focused */
    { TimuiInteractResult ir2 = timui_interact_button(&ui->ia, id, r);
      res.focused = ir2.focused;
      if(ir2.focused) selected = timui_updown_nav_(f, selected, count);
    }
    res.selected = selected;
    res.state_changed = (selected != orig);
    return res;
}
TIMUI_API TimuiTreeResult timui_tree_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTreeNode *nodes, int count, int *selected){
    TimuiTreeResult res = timui_tree(f, id, r, nodes, count, selected ? *selected : 0);
    if(selected && res.state_changed) *selected = res.selected;   /* write back only on a real change */
    return res;
}
/* ---- command palette (v0.2) ------------------------------------------- *
 * A popup with a filter input and a filtered command list. Controlled: takes
 * TimuiCmdPaletteState by value and returns the new state plus the activated
 * command index (or -1) in the result; timui_command_palette_mut writes back. */
static int cmd_matches(TimuiStr cmd, const char *filter){
    /* simple substring match (case-insensitive for ASCII); uses cmd.len so a
     * non-NUL-terminated TimuiStr slice is honored (not strlen). */
    size_t cl = cmd.len, fl = strlen(filter);
    size_t i, j;
    if(fl == 0) return 1;
    if(fl > cl) return 0;
    for(i = 0; i + fl <= cl; i++){
        for(j = 0; j < fl; j++){
            char a = cmd.ptr[i + j], b = filter[j];
            if(a >= 'A' && a <= 'Z') a += 32;
            if(b >= 'A' && b <= 'Z') b += 32;
            if(a != b) break;
        }
        if(j == fl) return 1;
    }
    return 0;
}
TIMUI_API TimuiCmdPaletteResult timui_command_palette(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *commands, int count, TimuiCmdPaletteState state){
    TimuiCmdPaletteResult res;
    Timui *ui;
    TimuiRect input_r, list_r;
    int i, matched_count = 0, sel_in = state.selected;
    int matched_idx[256];
    char filter_in[64];
    res.state = state; res.activated = -1; res.state_changed = 0;
    if(!f || !f->ui || !commands || count <= 0) return res;
    ui = f->ui;
    memcpy(filter_in, state.filter, sizeof filter_in);   /* snapshot to detect edits */
    timui_panel_begin(f, id, r, TIMUI_STR_LIT("Command Palette"), TIMUI_BORDER_SINGLE);
    input_r = TIMUI_RECT(r.x + 1, r.y + 1, r.w - 2, 1);
    list_r  = TIMUI_RECT(r.x + 1, r.y + 2, r.w - 2, r.h - 3);
    timui_input_line_buf(f, id + 1, input_r, state.filter, sizeof state.filter);
    /* filter */
    for(i = 0; i < count && matched_count < 256; i++){
        TimuiStr cmd = commands[i].ptr ? commands[i] : (TimuiStr){ "", 0 };
        if(cmd_matches(cmd, state.filter)) matched_idx[matched_count++] = i;
    }
    if(state.selected < 0) state.selected = 0;
    if(state.selected >= matched_count) state.selected = matched_count > 0 ? matched_count - 1 : 0;
    /* draw matched commands */
    { TimuiRect content = timui_scroll_begin(f, list_r, 0);
      for(i = 0; i < matched_count; i++){
          int orig = matched_idx[i];
          TimuiStyle st = timui_theme_style(&ui->theme,
              i == state.selected ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT);
          timui_draw_row_(&ui->curr, TIMUI_RECT(content.x, content.y + i, list_r.w, 1), 1, commands[orig], st);
      }
      timui_scroll_end(f);
    }
    /* V18: only steer the palette when its filter input is focused, so drawing
     * the palette without focusing it doesn't swallow arrow/Enter from siblings. */
    if(ui->ia.focus == id + 1){
        state.selected = timui_updown_nav_(f, state.selected, matched_count);
        if(timui_key_pressed(f, TIMUI_KEY_ENTER) && matched_count > 0){
            res.activated = matched_idx[state.selected];
            state.filter[0] = '\0';
            state.selected = 0;
        }
    }
    timui_panel_end(f);
    res.state = state;
    res.state_changed = (state.selected != sel_in ||
                         memcmp(state.filter, filter_in, sizeof filter_in) != 0);
    return res;
}
TIMUI_API TimuiCmdPaletteResult timui_command_palette_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *commands, int count, TimuiCmdPaletteState *state){
    TimuiCmdPaletteState in;
    TimuiCmdPaletteResult res;
    if(state) in = *state; else memset(&in, 0, sizeof in);
    res = timui_command_palette(f, id, r, commands, count, in);
    if(state && res.state_changed) *state = res.state;   /* write back edits/nav/activation reset */
    return res;
}
/* ---- snapshot testing (v0.2) ------------------------------------------ *
 * Render a cell-buffer row to an ASCII string for golden comparison. */
TIMUI_API void timui_snapshot_render(const TimuiCellBuffer *buf, int row, char *out, size_t cap){
    int x;
    size_t j = 0;
    if(!buf || !buf->cells || !out || cap == 0 || row < 0 || row >= buf->h){ if(out && cap) out[0] = '\0'; return; }
    for(x = 0; x < buf->w && j + 1 < cap; x++){
        uint32_t cp = buf->cells[(size_t)row * buf->w + x].codepoint;
        out[j++] = (cp && cp < 0x80) ? (char)cp : ' ';
    }
    out[j] = '\0';
}
TIMUI_API int timui_snapshot_row_eq(const TimuiCellBuffer *buf, int row, const char *expected){
    char snap[512];
    timui_snapshot_render(buf, row, snap, sizeof snap);
    return strcmp(snap, expected) == 0;
}

/* ---- full-grid snapshot + comparison (v0.2 visual-test infrastructure) -- *
 * timui_snapshot_grid serializes an entire TimuiCellBuffer to a deterministic,
 * diffable text form (one line per row) for golden-file visual testing.
 * timui_grid_eq compares two buffers cell-by-cell and is reused by the
 * libvterm round-trip harness (Tier A) so the diff message shape matches.
 *
 * Each cell is five '|'-separated fields: <codepoint>|<fg>|<bg>|<attrs>|<width>
 *   codepoint : glyph for printable ASCII (0x20-0x7e); '.' for an empty cell
 *               (cp==0); otherwise U+XXXX (uppercase hex).
 *   fg / bg   : '-' when "default" (field == 0, matching emit_sgr's "no SGR"
 *               emitted); otherwise 6-digit lowercase hex rrggbb. NOTE: pure
 *               black (0x000000) is indistinguishable from default here, but
 *               that faithfully mirrors the renderer's own model — emit_sgr
 *               treats fg==0/bg==0 as default, so a cell cannot represent
 *               black-as-set-color anyway.
 *   attrs     : '.' if none, else the sorted flag letters:
 *               b(old) d(im) i(talic) u(nderline) r(everse) k(blink) s(trike).
 *   width     : the cell's width field (1 or 2; 0 for a continuation cell).
 * Rows are prefixed "R<row>:" and separated by '\n'. The whole serialization
 * is NUL-terminated. Like snprintf, returns the length that WOULD have been
 * written and truncates if cap is too small. */

/* Minimal bounds-checked append cursor: writes a byte only when it fits
 * (reserving room for the terminating NUL) but always advances len, giving
 * snprintf-style "would-be length" semantics. */
typedef struct { char *p; size_t cap; size_t len; } TimuiSnapBuf;
static void sb_put(TimuiSnapBuf *s, char c){
    if(s->cap && s->len + 1 < s->cap) s->p[s->len] = c;
    s->len++;
}
static void sb_puts(TimuiSnapBuf *s, const char *str){ if(str) while(*str) sb_put(s, *str++); }
static void sb_putx(TimuiSnapBuf *s, uint32_t v, int upper){   /* one hex nibble */
    sb_put(s, (char)((v < 10 ? '0' + v : (upper ? 'A' : 'a') + (v - 10))));
}
static void sb_uint(TimuiSnapBuf *s, unsigned v){   /* decimal via the shared formatter */
    char nb[16]; int n = fmt_uint(nb, v), i;
    for(i = 0; i < n; i++) sb_put(s, nb[i]);
}

/* Serialize one cell's five fields into s (no trailing separator). */
static void sb_cell(TimuiSnapBuf *s, const TimuiCell *c){
    /* codepoint */
    if(c->codepoint == 0){ sb_put(s, '.'); }
    else if(c->codepoint >= 0x20 && c->codepoint < 0x7f){ sb_put(s, (char)c->codepoint); }
    else {
        uint32_t cp = c->codepoint; char tmp[8]; int n = 0, i;
        sb_puts(s, "U+");
        do { tmp[n++] = (char)((cp & 0xf) < 10 ? '0' + (cp & 0xf) : 'A' + (cp & 0xf) - 10); cp >>= 4; }
        while(cp && n < (int)sizeof(tmp));
        for(i = 0; i < n; i++) sb_put(s, tmp[n - 1 - i]);
    }
    sb_put(s, '|');
    /* fg */
    if(c->fg == TIMUI_COLOR_DEFAULT) sb_put(s, '-');
    else { sb_putx(s, (c->fg >> 20) & 0xf, 0); sb_putx(s, (c->fg >> 16) & 0xf, 0);
           sb_putx(s, (c->fg >> 12) & 0xf, 0); sb_putx(s, (c->fg >> 8) & 0xf, 0);
           sb_putx(s, (c->fg >> 4) & 0xf, 0);  sb_putx(s, c->fg & 0xf, 0); }
    sb_put(s, '|');
    /* bg */
    if(c->bg == TIMUI_COLOR_DEFAULT) sb_put(s, '-');
    else { sb_putx(s, (c->bg >> 20) & 0xf, 0); sb_putx(s, (c->bg >> 16) & 0xf, 0);
           sb_putx(s, (c->bg >> 12) & 0xf, 0); sb_putx(s, (c->bg >> 8) & 0xf, 0);
           sb_putx(s, (c->bg >> 4) & 0xf, 0);  sb_putx(s, c->bg & 0xf, 0); }
    sb_put(s, '|');
    /* attrs: sorted single-letter flags for stable, diffable output */
    if(c->attrs == 0) sb_put(s, '.');
    else {
        static const struct { uint32_t bit; char ch; } a[] = {
            { TIMUI_ATTR_BOLD, 'b' }, { TIMUI_ATTR_DIM, 'd' },
            { TIMUI_ATTR_ITALIC, 'i' }, { TIMUI_ATTR_UNDERLINE, 'u' },
            { TIMUI_ATTR_REVERSE, 'r' }, { TIMUI_ATTR_BLINK, 'k' },
            { TIMUI_ATTR_STRIKE, 's' }
        };
        int i;
        for(i = 0; i < (int)(sizeof(a)/sizeof(a[0])); i++)
            if(c->attrs & a[i].bit) sb_put(s, a[i].ch);
    }
    sb_put(s, '|');
    /* width */
    sb_uint(s, (unsigned)c->width);
}

TIMUI_API size_t timui_snapshot_grid(const TimuiCellBuffer *buf, char *out, size_t cap){
    TimuiSnapBuf s;
    int x, y;
    if(!buf || !buf->cells) return 0;
    /* The (NULL,0) size-query form still returns the would-be length: sb_put
     * writes nothing when cap==0 but advances len, so we run the loop anyway. */
    if(out && cap) out[0] = '\0';
    s.p = out; s.cap = cap; s.len = 0;
    for(y = 0; y < buf->h; y++){
        if(y > 0) sb_put(&s, '\n');
        sb_put(&s, 'R');
        sb_uint(&s, (unsigned)y);
        sb_put(&s, ':');
        for(x = 0; x < buf->w; x++){
            sb_put(&s, ' ');
            sb_cell(&s, &buf->cells[(size_t)y * buf->w + x]);
        }
    }
    if(out && cap) s.p[s.len < cap ? s.len : cap - 1] = '\0';   /* NUL within bounds */
    return s.len;                               /* would-be length (snprintf-style) */
}

/* Compare two grids field-by-field. Writes a one-cell diff message on the
 * first mismatch when diff_out != NULL. Compared fields match the renderer's
 * own notion of equality (codepoint/fg/bg/attrs/width): hyperlink_id and
 * image_id are intentionally excluded — they aren't expressible in plain text
 * and the libvterm round-trip validates hyperlinks via a dedicated scene. */
TIMUI_API int timui_grid_eq(const TimuiCellBuffer *a, const TimuiCellBuffer *b,
                            char *diff_out, size_t diff_cap){
    int x, y;
    if(!a || !b) return 0;
    if(a->w != b->w || a->h != b->h){
        if(diff_out && diff_cap){
            TimuiSnapBuf s = { diff_out, diff_cap, 0 };
            sb_puts(&s, "dimension mismatch: ");
            sb_uint(&s, (unsigned)a->w); sb_put(&s, 'x'); sb_uint(&s, (unsigned)a->h);
            sb_puts(&s, " vs ");
            sb_uint(&s, (unsigned)b->w); sb_put(&s, 'x'); sb_uint(&s, (unsigned)b->h);
            if(s.len >= diff_cap) s.len = diff_cap - 1;
            s.p[s.len] = '\0';
        }
        return 0;
    }
    for(y = 0; y < a->h; y++){
        for(x = 0; x < a->w; x++){
            const TimuiCell *ca = &a->cells[(size_t)y * a->w + x];
            const TimuiCell *cb = &b->cells[(size_t)y * b->w + x];
            if(ca->codepoint != cb->codepoint || ca->fg != cb->fg ||
               ca->bg != cb->bg || ca->attrs != cb->attrs ||
               ca->width != cb->width){
                if(diff_out && diff_cap){
                    TimuiSnapBuf s = { diff_out, diff_cap, 0 };
                    sb_puts(&s, "mismatch at (");
                    sb_uint(&s, (unsigned)x); sb_put(&s, ','); sb_uint(&s, (unsigned)y);
                    sb_puts(&s, "): expected ");
                    sb_cell(&s, ca);
                    sb_puts(&s, " got ");
                    sb_cell(&s, cb);
                    if(s.len >= diff_cap) s.len = diff_cap - 1;
                    s.p[s.len] = '\0';
                }
                return 0;
            }
        }
    }
    return 1;
}
/* ---- text-area widget (v0.2) ------------------------------------------ *
 * A multi-line text editor. Click to focus, type to insert, backspace deletes.
 * Lines are split on '\n'. (Cursor movement beyond append-at-end is future.) */
TIMUI_API void timui_text_area(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiTextAreaState *st){
    Timui *ui;
    TimuiInteractResult ir;
    TimuiRect content;
    size_t i;
    int y = 0;
    if(!f || !f->ui || !st || !st->text || st->cap == 0) return;
    if(st->cursor >= st->cap) st->cursor = st->cap - 1;   /* Y1: untrusted cursor -> OOB */
    ui = f->ui;
    ir = timui_interact_button(&ui->ia, id, r);
    if(ir.focused){
        int j = 0;
        /* insert typed codepoints AT the cursor (mid-string, F1.3), whole
         * codepoints only; stop when one won't fit (text_insert_ is cap-bounded).
         * The edit helpers live in the widgets section, in scope via the unity
         * build. */
        while(j < ui->text_in_len){
            int n = utf8_lead_len((unsigned char)ui->text_in[j]);
            size_t m = (size_t)(n > 0 ? n : 1);     /* defensive: stray byte as 1 */
            if(j + (int)m > ui->text_in_len) m = (size_t)(ui->text_in_len - j);
            if(!text_insert_(st->text, st->cap, st->cursor, ui->text_in + j, m)) break;
            st->cursor += m; j += (int)m;
        }
        /* cursor movement (one step per frame — the key_in bitmask can't count
         * repeats; key auto-repeat delivers one per frame). */
        if(ui->key_in & TIMUI_KEYIN_LEFT)  st->cursor = utf8_drop_last(st->text, st->cursor);
        if(ui->key_in & TIMUI_KEYIN_RIGHT) st->cursor = utf8_next_(st->text, st->cursor, strlen(st->text));
        if(ui->key_in & TIMUI_KEYIN_HOME)  st->cursor = line_start_(st->text, st->cursor);
        if(ui->key_in & TIMUI_KEYIN_END)   st->cursor = line_end_(st->text, st->cursor);
        /* deletion: backspace removes the codepoint before the cursor, DELETE
         * the one at the cursor. */
        if((ui->key_in & TIMUI_KEYIN_BACKSPACE) && st->cursor > 0){
            size_t prev = utf8_drop_last(st->text, st->cursor);
            st->cursor = text_erase_(st->text, prev, st->cursor);
        }
        if(ui->key_in & TIMUI_KEYIN_DELETE){
            size_t nxt = utf8_next_(st->text, st->cursor, strlen(st->text));
            (void)text_erase_(st->text, st->cursor, nxt);
        }
        ui->text_in_len = 0;
        ui->key_in = 0;
    }
    { TimuiStyle sst = timui_theme_style(&ui->theme, ir.focused ? TIMUI_SLOT_INPUT_FOCUSED : TIMUI_SLOT_INPUT);
      content = timui_scroll_begin(f, r, st->scroll_y);
      i = 0;
      while(i < st->cap && st->text[i]){
          size_t ls = i;
          while(i < st->cap && st->text[i] && st->text[i] != '\n'){
              if(st->text[i] == '\r') break;  /* \r or \r\n line break */
              i++;
          }
          timui_draw_text(&ui->curr, content.x, content.y + y,
                          (TimuiStr){ st->text + ls, i - ls }, sst);
          if(i < st->cap && (st->text[i] == '\r' || st->text[i] == '\n')){
              if(st->text[i] == '\r' && i + 1 < st->cap && st->text[i+1] == '\n') i++;
              i++;
          }
          y++;
      }
      /* auto-scroll to keep the cursor visible (computed before scroll_begin next frame) */
      {  int cursor_row = 0;
         size_t ci;
         for(ci = 0; ci < st->cursor && ci < st->cap; ci++)
             if(st->text[ci] == '\n' || st->text[ci] == '\r'){
                 cursor_row++;
                 if(st->text[ci] == '\r' && ci + 1 < st->cap && st->text[ci+1] == '\n') ci++;
             }
         if(cursor_row < st->scroll_y) st->scroll_y = cursor_row;
         if(cursor_row >= st->scroll_y + r.h) st->scroll_y = cursor_row - r.h + 1;
         if(st->scroll_y < 0) st->scroll_y = 0;
      }
      timui_scroll_end(f);
      if(ir.focused){                                 /* F1.4: request the hardware cursor */
          int crow, ccol;
          text_pos_(st->text, st->cursor, &crow, &ccol);
          if(crow >= st->scroll_y && crow < st->scroll_y + r.h && ccol < r.w){
              ui->cursor_x = r.x + ccol;                /* content.x == r.x (vertical scroll only) */
              ui->cursor_y = r.y + (crow - st->scroll_y);
              ui->cursor_visible = 1;
          }
      }
    }
}
/* ---- Windows ConPTY backend (v0.2) ------------------------------------ *
 * On Windows, ConPTY (CreatePseudoConsole) provides the equivalent of POSIX
 * forkpty. This is a minimal #ifdef _WIN32 skeleton; the real implementation
 * wraps CreatePseudoConsole + overlapped I/O in a TimuiTransport. On non-
 * Windows platforms it returns TIMUI_ERR_UNSUPPORTED. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct { HANDLE hPC; HANDLE hPipeIn; HANDLE hPipeOut; } TimuiConptyCtx;

static int conpty_write(TimuiTransport *t, const void *d, size_t n){
    TimuiConptyCtx *c = (TimuiConptyCtx *)t->ctx;
    DWORD written = 0;
    WriteFile(c->hPipeIn, d, (DWORD)n, &written, NULL);
    return (int)written;
}
static int conpty_read(TimuiTransport *t, void *b, size_t cap){
    TimuiConptyCtx *c = (TimuiConptyCtx *)t->ctx;
    DWORD got = 0;
    ReadFile(c->hPipeOut, b, (DWORD)cap, &got, NULL);
    return (int)got;
}
static int conpty_flush(TimuiTransport *t){ (void)t; return 0; }
static void conpty_close(TimuiTransport *t){ (void)t; }

TIMUI_API TimuiResult timui_conpty_open(TimuiTransport *out_transport, int *out_pid){
    /* TODO: CreatePipe x2, CreatePseudoConsole, CreateProcess, wrap handles.
     * The skeleton above provides the transport vtable; the full impl needs
     * STARTUPINFO + STARTUPINFOEX + PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE. */
    (void)out_transport; (void)out_pid;
    return TIMUI_ERR_UNSUPPORTED;
}
TIMUI_API void timui_conpty_close(TimuiTransport *transport, int pid){
    /* TODO: ClosePseudoConsole, CloseHandle, TerminateProcess. */
    (void)transport; (void)pid;
}
#else
TIMUI_API TimuiResult timui_conpty_open(TimuiTransport *out_transport, int *out_pid){
    (void)out_transport; (void)out_pid;
    return TIMUI_ERR_UNSUPPORTED;
}
TIMUI_API void timui_conpty_close(TimuiTransport *transport, int pid){
    (void)transport; (void)pid;
}
#endif
/* ---- Kitty graphics images (v0.2) ------------------------------------- *
 * Accept PNG bytes; when the terminal supports Kitty graphics, transmit via
 * ESC_G (base64-encoded PNG, f=100); otherwise draw a "[img]" placeholder.
 * Image caching/placement lifecycle is deferred (re-transmit per frame). */

/* Write all len bytes, looping past short writes. A real fd transport may
 * deliver fewer bytes than requested; without this a graphics chunk can split
 * across the header/payload/ST boundary and corrupt the image (G5 residual). */
static void kitty_write_all(TimuiTransport *t, const void *data, size_t len){
    const unsigned char *p = (const unsigned char *)data;
    size_t off = 0;
    if(!t || !t->write) return;
    while(off < len){
        int w = t->write(t, p + off, len - off);
        if(w <= 0) break;                /* error / would-block: best-effort, stop */
        off += (size_t)w;
    }
}

TIMUI_API void timui_force_cap(Timui *ui, TimuiCapFlags cap, int enable){
    if(!ui) return;
    if(enable) ui->caps.flags |= (uint32_t)cap;
    else       ui->caps.flags &= ~(uint32_t)cap;
}
TIMUI_API TimuiImage *timui_image_from_png(Timui *ui, const void *data, size_t size){
    TimuiImage *img;
    TimuiAllocator al;
    (void)ui;
    if(!data || size == 0) return NULL;
    al = timui_default_allocator();
    img = (TimuiImage *)al.alloc(al.userdata, sizeof(TimuiImage));
    if(!img) return NULL;
    img->data = (unsigned char *)al.alloc(al.userdata, size);
    if(!img->data){ al.free(al.userdata, img, sizeof *img); return NULL; }
    memcpy(img->data, data, size);
    img->len = size;
    img->id = 0;                 /* assigned on first transmit (timui_images_flush_) */
    /* pixel size from the PNG IHDR (width @16, height @20, big-endian) so a
     * placement can be cropped to a cell sub-rect (smooth scroll clipping). */
    img->px_w = img->px_h = 0;
    if(size >= 24){
        const unsigned char *d = (const unsigned char *)data;
        img->px_w = (int)(((uint32_t)d[16] << 24) | ((uint32_t)d[17] << 16) | ((uint32_t)d[18] << 8) | d[19]);
        img->px_h = (int)(((uint32_t)d[20] << 24) | ((uint32_t)d[21] << 16) | ((uint32_t)d[22] << 8) | d[23]);
    }
    return img;
}
TIMUI_API void timui_image_free(Timui *ui, TimuiImage *img){
    TimuiAllocator al;
    (void)ui;
    if(!img) return;
    al = timui_default_allocator();
    if(img->data) al.free(al.userdata, img->data, img->len);
    al.free(al.userdata, img, sizeof *img);
}
/* base64-encode + chunked transmit of the PNG bytes under `id` (a=t, one-time).
 * Uses the correct APC introducer ESC _ G (was ESC G — a real protocol bug that
 * meant no terminal ever recognised the image). */
static void kitty_transmit_(TimuiTransport *t, uint32_t id, const unsigned char *data, size_t len){
    size_t b64cap, b64len, sent;
    TimuiAllocator al = timui_default_allocator();
    char *buf;
    int first = 1;
    if(len == 0 || len > (SIZE_MAX - 1) / 4) return;
    b64cap = ((len + 2) / 3) * 4 + 1;
    buf = (char *)al.alloc(al.userdata, b64cap);
    if(!buf) return;
    b64len = b64_encode(data, len, buf, b64cap - 1);
    if(b64len > 0 && b64len != (size_t)-1){
        #define KITTY_CHUNK 4096
        sent = 0;
        while(sent < b64len){
            size_t chunk = b64len - sent;
            char hdr[48]; int hn = 0, is_last;
            const char *p;
            if(chunk > KITTY_CHUNK) chunk = KITTY_CHUNK;
            is_last = (sent + chunk >= b64len);
            hdr[hn++] = 0x1b; hdr[hn++] = '_'; hdr[hn++] = 'G';          /* APC + G */
            if(first){ /* transmit: direct(d) PNG(f=100) under id, quiet(q=2) */
                p = "a=t,t=d,f=100,q=2,i="; while(*p) hdr[hn++] = *p++;
                hn += fmt_uint(hdr + hn, id);
                hdr[hn++] = ',';
            }
            hdr[hn++] = 'm'; hdr[hn++] = '='; hdr[hn++] = is_last ? '0' : '1'; hdr[hn++] = ';';
            kitty_write_all(t, hdr, (size_t)hn);
            kitty_write_all(t, buf + sent, chunk);
            kitty_write_all(t, "\x1b\\", 2);
            sent += chunk; first = 0;
        }
        #undef KITTY_CHUNK
    }
    al.free(al.userdata, buf, b64cap);
}
/* place image `id` at the cursor, scaled to cols x rows cells, under placement
 * id `place_id` (a=p). A UNIQUE placement id per on-screen slot is essential:
 * several messages sharing one image (same id) must not all use the same
 * placement id, or each a=p replaces the previous and only one image shows. */
static void kitty_place_(TimuiTransport *t, uint32_t id, int cols, int rows, int place_id,
                         int sx, int sy, int sw, int sh){
    char b[128]; int n = 0; const char *p;
    b[n++] = 0x1b; b[n++] = '_'; b[n++] = 'G';
    p = "a=p,q=2,i="; while(*p) b[n++] = *p++;
    n += fmt_uint(b + n, id);
    p = ",p="; while(*p) b[n++] = *p++;  n += fmt_uint(b + n, (unsigned)(place_id > 0 ? place_id : 1));
    if(sw > 0){   /* source-crop rectangle (pixels) so a scrolled image clips */
        p = ",x="; while(*p) b[n++] = *p++;  n += fmt_uint(b + n, (unsigned)(sx > 0 ? sx : 0));
        p = ",y="; while(*p) b[n++] = *p++;  n += fmt_uint(b + n, (unsigned)(sy > 0 ? sy : 0));
        p = ",w="; while(*p) b[n++] = *p++;  n += fmt_uint(b + n, (unsigned)sw);
        p = ",h="; while(*p) b[n++] = *p++;  n += fmt_uint(b + n, (unsigned)(sh > 0 ? sh : 1));
    }
    p = ",c="; while(*p) b[n++] = *p++;  n += fmt_uint(b + n, (unsigned)(cols > 0 ? cols : 1));
    p = ",r="; while(*p) b[n++] = *p++;  n += fmt_uint(b + n, (unsigned)(rows > 0 ? rows : 1));
    b[n++] = 0x1b; b[n++] = '\\';
    kitty_write_all(t, b, (size_t)n);
}
/* delete every visible placement (keeps image data: lowercase d=a). */
static void kitty_delete_all_placements(TimuiTransport *t){
    kitty_write_all(t, "\x1b_Ga=d,d=a\x1b\\", 12);
}
/* Transmit (once) + place every image recorded this frame, on top of the cell
 * diff. Each on-screen slot gets a distinct placement id (i+1) and is CUP'd to
 * its rect, scaled to its cell size. When the count SHRINKS (placements scrolled
 * away, or shuffled slots), clear last frame's placements first so nothing
 * lingers above the cells (a cell redraw can't erase a Kitty image), then
 * re-place this frame's set. Under synchronized output the clear+replace is
 * atomic, so there is no flicker. Skipped on the first frame (nothing to
 * clear), which keeps a lone draw to a single transmit+place. */
void timui_images_flush_(Timui *ui){
    int i;
    if(!ui) return;
    if(ui->img_last_count > 0)
        kitty_delete_all_placements(&ui->transport);
    for(i = 0; i < ui->img_place_count; i++){
        TimuiImage *img = ui->img_place[i].img;
        TimuiRect r    = ui->img_place[i].rect;   /* visible sub-rect */
        TimuiRect full = ui->img_place[i].full;   /* uncropped rect   */
        int sx = 0, sy = 0, sw = 0, sh = 0;
        char cup[32]; int cn = 0;
        if(!img) continue;
        /* If the visible rect is a vertical sub-slice of `full`, crop the source
         * pixels to match, so the image clips smoothly at a pane edge. */
        if(img->px_w > 0 && img->px_h > 0 && full.h > 0 && (r.y != full.y || r.h != full.h)){
            sx = 0; sw = img->px_w;
            sy = (int)((long)(r.y - full.y) * img->px_h / full.h);
            sh = (int)((long)r.h * img->px_h / full.h);
            if(sh < 1) sh = 1;
        }
        if(img->id == 0){                                   /* transmit once, keyed by id */
            img->id = ++ui->next_image_id;
            kitty_transmit_(&ui->transport, img->id, img->data, img->len);
        }
        cup[cn++] = 0x1b; cup[cn++] = '[';                  /* CUP to the top-left cell */
        cn += fmt_uint(cup + cn, (unsigned)(r.y + 1)); cup[cn++] = ';';
        cn += fmt_uint(cup + cn, (unsigned)(r.x + 1)); cup[cn++] = 'H';
        kitty_write_all(&ui->transport, cup, (size_t)cn);
        kitty_place_(&ui->transport, img->id, r.w, r.h, i + 1, sx, sy, sw, sh);
    }
    ui->img_last_count = ui->img_place_count;
}
/* Record an image placement (transmit + place happen on top of the cell diff in
 * timui_end, so the renderer can't clobber it). `visible` is where it's drawn;
 * `full` is the uncropped rect (== visible when not clipping). The caller
 * reserves the region (draws its own background, no text). */
static void image_record_(Timui *ui, TimuiImage *img, TimuiRect visible, TimuiRect full){
    if(timui_caps_has(&ui->caps, TIMUI_CAP_KITTY_GRAPHICS)){
        if(ui->img_place_count < (int)(sizeof(ui->img_place) / sizeof(ui->img_place[0]))){
            ui->img_place[ui->img_place_count].img  = img;
            ui->img_place[ui->img_place_count].rect = visible;
            ui->img_place[ui->img_place_count].full = full;
            ui->img_place_count++;
        }
    } else {
        /* placeholder fallback (cells) for non-Kitty terminals */
        timui_draw_fill(&ui->curr, visible, timui_theme_style(&ui->theme, TIMUI_SLOT_INPUT));
        timui_draw_text(&ui->curr, visible.x, visible.y, TIMUI_STR_LIT("[img]"),
                        timui_theme_style(&ui->theme, TIMUI_SLOT_TEXT_DIM));
    }
}
TIMUI_API void timui_image_draw(TimuiFrame *f, TimuiImage *img, TimuiRect r){
    if(!f || !f->ui || !img) return;
    image_record_(f->ui, img, r, r);
}
TIMUI_API void timui_image_draw_clipped(TimuiFrame *f, TimuiImage *img,
                                        TimuiRect full, TimuiRect visible){
    if(!f || !f->ui || !img || visible.w <= 0 || visible.h <= 0) return;
    image_record_(f->ui, img, visible, full);
}
/* ---- menu bar + popups (T5.7) + menu focus (T4.5) -------------------- *
 * menu_bar_begin/end bracket the bar; menu_begin draws a header and toggles
 * its popup open on click (returning whether it is open); menu_item items draw
 * in the popup and report a click (which closes the menu). An outside click
 * (a press not on any header/item) closes any open menu. Z27: all state lives
 * in the caller-owned TimuiMenuBar (no hidden fields in Timui) — `open` persists
 * across frames; the layout cursor is frame-scoped, reset by menu_bar_begin. */
TIMUI_API void timui_menu_bar_begin(TimuiFrame *f, TimuiMenuBar *bar, TimuiRect r){
    Timui *ui;
    if(!f || !f->ui || !bar) return;
    ui = f->ui;
    bar->bar_x = r.x;
    bar->bar_y = r.y;
    bar->clicked = 0;
    timui_draw_fill(&ui->curr, r, timui_theme_style(&ui->theme, TIMUI_SLOT_MENU));
}
TIMUI_API int timui_menu_begin(TimuiFrame *f, TimuiMenuBar *bar, TimuiId id, TimuiStr label){
    Timui *ui;
    TimuiRect hdr;
    TimuiInteractResult ir;
    int is_open;
    if(!f || !f->ui || !bar) return 0;
    ui = f->ui;
    hdr = TIMUI_RECT(bar->bar_x, bar->bar_y, (int)label.len + 2, 1);
    ir = timui_interact_button(&ui->ia, id, hdr);
    if(ir.hovered && ui->ia.mouse_pressed) bar->clicked = 1;   /* press on header */
    if(ir.clicked) bar->open = (bar->open == id) ? 0 : id;     /* release toggles */
    is_open = (bar->open == id);
    {
        TimuiStyle st = timui_theme_style(&ui->theme, is_open ? TIMUI_SLOT_MENU_ACTIVE : TIMUI_SLOT_MENU);
        timui_draw_row_(&ui->curr, hdr, 1, label, st);
    }
    bar->bar_x += hdr.w;
    if(is_open){ bar->item_x = hdr.x; bar->item_y = bar->bar_y + 1; }
    return is_open;
}
TIMUI_API int timui_menu_item(TimuiFrame *f, TimuiMenuBar *bar, TimuiId id, TimuiStr label){
    Timui *ui;
    TimuiRect r;
    TimuiInteractResult ir;
    if(!f || !f->ui || !bar) return 0;
    ui = f->ui;
    r = TIMUI_RECT(bar->item_x, bar->item_y, (int)label.len + 4, 1);
    ir = timui_interact_button(&ui->ia, id, r);
    if(ir.hovered && ui->ia.mouse_pressed) bar->clicked = 1;   /* press on item */
    {
        TimuiStyle st = timui_theme_style(&ui->theme, ir.hovered ? TIMUI_SLOT_MENU_ACTIVE : TIMUI_SLOT_MENU);
        timui_draw_row_(&ui->curr, r, 2, label, st);
    }
    bar->item_y++;
    if(ir.clicked){ bar->open = 0; return 1; }                 /* release selects */
    return 0;
}
TIMUI_API void timui_menu_end(TimuiFrame *f){ (void)f; }
TIMUI_API void timui_menu_bar_end(TimuiFrame *f, TimuiMenuBar *bar){
    Timui *ui;
    if(!f || !f->ui || !bar) return;
    ui = f->ui;
    if(bar->open && ui->ia.mouse_pressed && !bar->clicked) bar->open = 0;
}
/* ---- optional functional runner --------------------------------------- *
 * UI-thread message queue (emit during view, recv into update) + the runner. */
/* timui_run delivers each posted message via a fixed internal buffer. Messages
 * larger than TIMUI_RUN_BUF are truncated (W5) — keep posts small, or call
 * timui_recv directly with your own buffer for large payloads. */
#define TIMUI_RUN_BUF 4096
TIMUI_API bool timui_emit(TimuiFrame *f, uint32_t type, const void *data, size_t size){
    return f && f->ui && timui_mpsc_post(&f->ui->postq, type, data, size) != 0;
}
TIMUI_API bool timui_post(Timui *ui, uint32_t type, const void *data, size_t size){
    return ui && timui_mpsc_post(&ui->postq, type, data, size) != 0;
}
TIMUI_API bool timui_recv(Timui *ui, uint32_t *out_type, void *out_buf, size_t *inout_size){
    return ui && timui_mpsc_recv(&ui->postq, out_type, out_buf, inout_size) != 0;
}
TIMUI_API void timui_frame_quit(TimuiFrame *f){
    if(f && f->ui) timui_quit(f->ui);
}
TIMUI_API int timui_run(const TimuiConfig *cfg, TimuiApp *app){
    Timui *ui = NULL;
    if(!cfg || !app || !app->view || timui_open(cfg, &ui) != TIMUI_OK) return 1;
    while(!timui_should_quit(ui)){
        TimuiFrame *f = NULL;
        uint32_t type = 0;
        unsigned char buf[TIMUI_RUN_BUF];
        size_t sz;
        if(!timui_begin(ui, &f)) break;
        app->view(f, app->model);
        sz = sizeof buf;
        while(timui_recv(ui, &type, buf, &sz)){
            if(sz > sizeof buf) sz = sizeof buf;   /* clamp to prevent stack over-read */
            if(app->update) app->update(app->model, type, buf, sz);
            sz = sizeof buf;
        }
        timui_end(f);
    }
    timui_close(ui);
    return 0;
}
#undef TIMUI_RUN_BUF   /* Z10: impl-only macro must not leak into the consumer TU */
#endif /* TIMUI_IMPLEMENTATION */
