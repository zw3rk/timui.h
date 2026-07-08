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
 * and the themed widget set are all implemented and unit-tested. The Win32
 * ConPTY transport is implemented behind _WIN32, runtime-probed, and
 * compile-checked; live Windows Terminal smoke evidence is still required
 * before claiming supported Windows operation.
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
 * TIMUI_NO_IMAGES        keep the image API but disable terminal image escapes
 * TIMUI_API              override public symbol visibility
 * TIMUI_STATIC           reserved (future static-link mode)
 *
 * Reserved (recognized by name only; no #ifdef gates them yet — defining one
 * is a no-op): TIMUI_NO_STDIO, TIMUI_NO_UTF8_TABLES
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

/* ---- Lifecycle (POSIX terminal backend; Win32 ConPTY transport) -------- */
TIMUI_API TimuiResult timui_open(const TimuiConfig *cfg, Timui **out_ui);
TIMUI_API void        timui_close(Timui *ui);
/* Restore the terminal (screen exit + termios). Call from normal control flow
 * or an atexit hook; do not call this public transport path from a signal
 * handler. timui_open's optional signal handler uses an internal best-effort
 * restoration path instead. */
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
/* Advanced raw-event polling. timui_begin consumes key/text/mouse/paste into
 * frame aggregators (timui_text_input, key flags, mouse helpers). Events left
 * after begin are for out-of-band cases such as focus changes. */
TIMUI_API int              timui_poll_event(Timui *ui, TimuiEvent *out_event);
/* G7: returns the count of events dropped this frame and resets the counter.
 * The raw queue holds 512 slots; paste/text frame buffers can still truncate
 * very large bursts. Call after timui_begin to detect loss. */
TIMUI_API int              timui_events_dropped(Timui *ui);
TIMUI_API void             timui_quit(Timui *ui);
TIMUI_API bool             timui_should_quit(const Timui *ui);
/* Test constructor: a Timui backed by an injected transport + fixed size
 * (no tty), so the frame/render path is unit-testable without a terminal. */
TIMUI_API TimuiResult      timui_open_for_test(Timui **out_ui, TimuiTransport transport,
                                               int w, int h, const TimuiAllocator *alloc);
/* Test seam: override cell pixel geometry for protocol-emission tests. Invalid
 * dimensions clear the override and restore source-pixel Sixel emission. */
TIMUI_API void             timui_set_cell_pixels_for_test(Timui *ui, int cell_w_px, int cell_h_px);

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

/* ---- Constraint layout solver (ratatui-style) ------------------------- *
 * A TimuiConstraint is a tagged sizing rule for one child of a split:
 *   LEN(n)  fixed n cells
 *   PCT(p)  p% of the axis length (rounded to the nearest cell)
 *   FLEX(w) weighted share of the leftover space
 *   MIN(n)  flexible, but never smaller than n cells (grows to fill)
 *   MAX(n)  flexible, but never larger than n cells (grows to fill, capped)
 * timui_split divides `area` along `axis` into n contiguous child rects that
 * tile the area: LEN/PCT are allocated first, the remainder is shared across the
 * flexible children by weight — the LAST flexible child absorbs the rounding
 * remainder so children tile EXACTLY — and MIN/MAX bounds are honoured. Sizes
 * are clamped non-negative and never overflow the area (over-constrained fixed
 * sizes clamp to the boundary). Returns the number of child rects written (n),
 * or 0 on bad arguments (NULL cons/out, n<=0, or n greater than the internal
 * per-split cap). */
typedef enum {
    TIMUI_CON_LEN = 0,   /* fixed n cells                        */
    TIMUI_CON_PCT,       /* p% of the axis length               */
    TIMUI_CON_FLEX,      /* weighted share of the leftover space */
    TIMUI_CON_MIN,       /* flexible, at least n cells           */
    TIMUI_CON_MAX        /* flexible, at most n cells            */
} TimuiConstraintKind;

typedef struct {
    TimuiConstraintKind kind;
    int                 value;   /* LEN/MIN/MAX: cells · PCT: percent · FLEX: weight */
} TimuiConstraint;

/* Split axis: H arranges children side-by-side (columns; x/w vary), V stacks
 * them top-to-bottom (rows; y/h vary). */
typedef enum { TIMUI_AXIS_H = 0, TIMUI_AXIS_V = 1 } TimuiAxis;

/* Gap between adjacent children + a uniform outer margin inside `area`. */
typedef struct { int gap; int margin; } TimuiLayoutOpts;

/* Constructor macros (C99 compound literals). */
#define TIMUI_LEN(n)  ((TimuiConstraint){ TIMUI_CON_LEN,  (n) })
#define TIMUI_PCT(p)  ((TimuiConstraint){ TIMUI_CON_PCT,  (p) })
#define TIMUI_FLEX(w) ((TimuiConstraint){ TIMUI_CON_FLEX, (w) })
#define TIMUI_MIN(n)  ((TimuiConstraint){ TIMUI_CON_MIN,  (n) })
#define TIMUI_MAX(n)  ((TimuiConstraint){ TIMUI_CON_MAX,  (n) })

TIMUI_API int timui_split(TimuiRect area, TimuiAxis axis, const TimuiConstraint *cons, int n, TimuiRect *out);
TIMUI_API int timui_split_ex(TimuiRect area, TimuiAxis axis, const TimuiConstraint *cons, int n,
                             TimuiLayoutOpts opts, TimuiRect *out);
TIMUI_API int timui_split_h(TimuiRect area, const TimuiConstraint *cons, int n, TimuiRect *out);
TIMUI_API int timui_split_v(TimuiRect area, const TimuiConstraint *cons, int n, TimuiRect *out);
/* 2D grid: split `area` into nr row-bands by `rows` (heights), then each band
 * into nc cells by `cols` (widths). `out` is row-major (out[r*nc + c]); returns
 * nr*nc, or 0 on bad arguments. */
TIMUI_API int timui_grid(TimuiRect area, const TimuiConstraint *rows, int nr,
                         const TimuiConstraint *cols, int nc, TimuiRect *out);
TIMUI_API int timui_grid_ex(TimuiRect area, const TimuiConstraint *rows, int nr,
                            const TimuiConstraint *cols, int nc, TimuiLayoutOpts opts, TimuiRect *out);

/* Caller-owned two-pane splitter: TIMUI_AXIS_H gives left/divider/right,
 * TIMUI_AXIS_V gives top/divider/bottom. The controlled form returns updated
 * state without writing caller memory; the _mut form writes back only while the
 * divider is dragged. */
typedef struct {
    float ratio;        /* first pane share of available space, clamped 0..1 */
    int min_first;      /* minimum cells for the first pane */
    int min_second;     /* minimum cells for the second pane */
} TimuiSplitPaneState;
typedef struct {
    TimuiSplitPaneState state;
    TimuiRect first;
    TimuiRect divider;
    TimuiRect second;
    bool changed;
    bool hovered;
    bool dragging;
} TimuiSplitPaneResult;
TIMUI_API TimuiSplitPaneResult timui_split_pane(TimuiFrame *f, TimuiId id, TimuiRect r,
                                                TimuiAxis axis, TimuiSplitPaneState state);
TIMUI_API TimuiSplitPaneResult timui_split_pane_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
                                                    TimuiAxis axis, TimuiSplitPaneState *state);

/* ---- Box frame (line-drawing border) + colour lerp -------------------- *
 * timui_border strokes a 1-cell frame around `r` in the chosen line style with
 * an optional `title` embedded in the top edge, and returns the inner content
 * rect (r inset by one cell on every side, clamped non-negative). The inner rect
 * is returned even when the frame is too small to draw (r.w<2 or r.h<2) or f is
 * NULL, so callers can always lay out inside it. timui_lerp_rgb linearly
 * interpolates two packed 0xRRGGBB colours (t clamped to [0,1]). */
typedef enum {
    TIMUI_BOX_SINGLE = 0,   /* ─ │ ┌ ┐ └ ┘ */
    TIMUI_BOX_ROUNDED,      /* ─ │ ╭ ╮ ╰ ╯ */
    TIMUI_BOX_DOUBLE,       /* ═ ║ ╔ ╗ ╚ ╝ */
    TIMUI_BOX_THICK         /* ━ ┃ ┏ ┓ ┗ ┛ */
} TimuiBorderStyle;

TIMUI_API TimuiRect timui_border(TimuiFrame *f, TimuiRect r, TimuiBorderStyle style, TimuiStr title, TimuiStyle st);
TIMUI_API uint32_t  timui_lerp_rgb(uint32_t a, uint32_t b, float t);

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

/* ---- UTF-8 decode + display width ------------------------------------- *
 * timui_utf8_decode returns the byte length of the next codepoint (1..4),
 * 0 if the input is incomplete, or 1 with *out_cp=U+FFFD on an invalid byte.
 * timui_utf8_width is a minimal wcwidth: control/combining/format modifiers ->
 * 0, CJK/fullwidth/emoji bases -> 2, box-drawing/printable -> 1.
 *
 * Grapheme helpers walk extended user-visible clusters for the common TUI
 * cases timui must not split: CRLF, combining marks, variation selectors,
 * emoji skin-tone modifiers, regional-indicator flags, and ZWJ emoji runs.
 * `next` / `prev` take byte offsets into s[0..len] and return byte offsets;
 * width measures the first cluster in s[0..len]. */
TIMUI_API int timui_utf8_decode(const char *s, size_t len, uint32_t *out_cp);
TIMUI_API int timui_utf8_width(uint32_t cp);
TIMUI_API size_t timui_grapheme_next(const char *s, size_t len, size_t off);
TIMUI_API size_t timui_grapheme_prev(const char *s, size_t len, size_t off);
TIMUI_API int    timui_grapheme_width(const char *s, size_t len);

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

/* ---- Stylesheets (small TCSS-like parser/resolver) -------------------- */
typedef enum {
    TIMUI_WIDGET_ANY = 0,
    TIMUI_WIDGET_LABEL,
    TIMUI_WIDGET_PANEL,
    TIMUI_WIDGET_BUTTON,
    TIMUI_WIDGET_INPUT,
    TIMUI_WIDGET_TEXT_AREA,
    TIMUI_WIDGET_LISTBOX,
    TIMUI_WIDGET_TABLE,
    TIMUI_WIDGET_TREE,
    TIMUI_WIDGET_MENU,
    TIMUI_WIDGET_TOAST,
    TIMUI_WIDGET_SPLIT
} TimuiWidgetKind;

typedef enum {
    TIMUI_STYLE_STATE_FOCUSED  = 1u << 0,
    TIMUI_STYLE_STATE_HOVERED  = 1u << 1,
    TIMUI_STYLE_STATE_ACTIVE   = 1u << 2,
    TIMUI_STYLE_STATE_DISABLED = 1u << 3,
    TIMUI_STYLE_STATE_SELECTED = 1u << 4
} TimuiStyleState;

typedef enum {
    TIMUI_STYLE_PROP_FG          = 1u << 0,
    TIMUI_STYLE_PROP_BG          = 1u << 1,
    TIMUI_STYLE_PROP_ATTRS       = 1u << 2,
    TIMUI_STYLE_PROP_BORDER      = 1u << 3,
    TIMUI_STYLE_PROP_PADDING     = 1u << 4,
    TIMUI_STYLE_PROP_GAP         = 1u << 5,
    TIMUI_STYLE_PROP_GRADIENT_LO = 1u << 6,
    TIMUI_STYLE_PROP_GRADIENT_HI = 1u << 7
} TimuiStyleProp;

typedef struct TimuiStyleRule TimuiStyleRule;
typedef struct {
    TimuiStyleRule *rules;
    int count;
    int cap;
    TimuiAllocator alloc;
} TimuiStylesheet;

typedef struct {
    TimuiWidgetKind kind;
    const char *id;
    const char *classes;       /* whitespace-separated class names */
    uint32_t states;
    TimuiStyle base;
} TimuiStyleQuery;

typedef struct {
    TimuiStyle style;
    uint32_t mask;
    uint32_t border;
    int padding;
    int gap;
    uint32_t gradient_lo;
    uint32_t gradient_hi;
} TimuiResolvedStyle;

TIMUI_API TimuiResult timui_stylesheet_parse(TimuiStylesheet *out, const char *src,
                                             size_t len, const TimuiAllocator *alloc);
TIMUI_API void timui_stylesheet_free(TimuiStylesheet *ss);
TIMUI_API TimuiResolvedStyle timui_stylesheet_resolve(const TimuiStylesheet *ss,
                                                      TimuiStyleQuery query);
/* Borrow a parsed stylesheet for subsequent frames; ownership stays with the
 * caller. Pass NULL to return to the builtin theme only. */
TIMUI_API void timui_set_stylesheet(Timui *ui, const TimuiStylesheet *ss);

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
TIMUI_API void        timui_open_fail_fsetfl_for_test(int on);

/* Query the terminal size (cols x rows) via TIOCGWINSZ. Applications that need
 * live resize handling should call this on the output fd and then call
 * timui_ui_resize(ui, w, h) when the size changes. Returns
 * TIMUI_ERR_NOT_A_TTY if fd is not a terminal. The _pixels variant also returns
 * the terminal's total pixel dimensions when the platform reports them. */
TIMUI_API TimuiResult timui_term_size(int fd, int *out_w, int *out_h);
TIMUI_API TimuiResult timui_term_size_pixels(int fd, int *out_w, int *out_h,
                                             int *out_px_w, int *out_px_h);

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
    TIMUI_CAP_UNICODE_CORE    = 1u << 10,
    TIMUI_CAP_SIXEL_GRAPHICS  = 1u << 11,
    TIMUI_CAP_ITERM2_IMAGES   = 1u << 12
} TimuiCapFlags;

typedef enum {
    TIMUI_IMAGE_PROTOCOL_NONE = 0,
    TIMUI_IMAGE_PROTOCOL_KITTY,
    TIMUI_IMAGE_PROTOCOL_SIXEL,
    TIMUI_IMAGE_PROTOCOL_ITERM2
} TimuiImageProtocol;

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
/* Select the preferred image protocol from explicit capability flags. Kitty is
 * preferred when present because it is the richest path in this release;
 * otherwise Sixel wins over iTerm2 for broader terminal utility. Defining
 * TIMUI_NO_IMAGES makes this return TIMUI_IMAGE_PROTOCOL_NONE. */
TIMUI_API TimuiImageProtocol timui_caps_image_protocol(const TimuiCaps *caps);
/* The capabilities detected for an open ui — so apps can, e.g., choose an inline
 * image vs a text fallback: timui_image_protocol(ui) != TIMUI_IMAGE_PROTOCOL_NONE. */
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
    TIMUI_EVENT_PASTE, TIMUI_EVENT_RESIZE, TIMUI_EVENT_FOCUS, /* RESIZE reserved */
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
/* ---- pure data-grid math (display-width aware, no frame/TUI) ----------- *
 * The deterministic layout kernel shared by the enhanced table + scrollable
 * tree. Lifted from examples/sqlite_table.h so widgets and callers drive the
 * same tested code. All are side-effect-free and unit-tested (tests/test_grid.c). */
typedef struct { int first; int count; } TimuiSlice;   /* a visible [first,first+count) window */
/* Display width (terminal columns) of a NUL-terminated UTF-8 string: sums
 * timui_utf8_width over each code point (CJK/emoji=2, control/combining=0).
 * NULL -> 0. */
TIMUI_API int  timui_display_width(const char *s);
/* A column's fitted width from per-cell display widths: the widest cell, floored
 * at `minw` (room for an ellipsis) and capped at `maxw`. `minw` is a hard floor
 * (wins even when maxw<minw). Negative widths ignored; n==0/NULL -> minw. */
TIMUI_API int  timui_col_fit_width(const int *cellw, int n, int maxw, int minw);
/* Truncate `s` to at most `width` display columns into `out` (cap bytes, NUL-
 * terminated), never splitting a wide (2-col) glyph; on overflow a 1-column "…"
 * occupies the final column and *ellipsis is set. Returns the columns used
 * (<= width). width<=0 -> empty (ellipsis flagged iff content was dropped). */
TIMUI_API int  timui_fit_cell(const char *s, int width, char *out, size_t cap, int *ellipsis);
/* Visible [first,first+count) slice of `total` items in a `viewport`-sized
 * window scrolled to `offset`, clamped to [0, max(0,total-viewport)] so it never
 * scrolls past either end. Reused for VERTICAL rows AND HORIZONTAL cells.
 * Degenerate (total<=0 or viewport<=0) -> a zero-length slice. */
TIMUI_API TimuiSlice timui_page_slice(int total, int viewport, int offset);
/* Minimal scroll offset that keeps item `sel` inside a `viewport` window at
 * `offset`: scroll up to it if above, down just enough if below, else unchanged.
 * Clamped >= 0. `total` bounds the range; viewport<=0 is a no-op. */
TIMUI_API int  timui_scroll_to(int sel, int offset, int viewport, int total);

typedef const char *(*TimuiCellFn)(void *ud, int row, int col);
/* Additive field: hscroll (horizontal cell offset) — existing {selected,scroll}
 * callers keep compiling (aggregate inits zero-fill it; the plain timui_table
 * ignores it). Read/written only by the timui_table_ex family. */
typedef struct { int selected; int scroll; int hscroll; } TimuiTableState;
typedef struct { TimuiTableState state; int state_changed; int focused; } TimuiTableResult;
TIMUI_API TimuiTableResult timui_table(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *headers, int ncols, int nrows, TimuiCellFn cell_fn, void *ud,
    TimuiTableState state);
TIMUI_API TimuiTableResult timui_table_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *headers, int ncols, int nrows, TimuiCellFn cell_fn, void *ud,
    TimuiTableState *state);

/* A virtual multi-column grid model: header labels + a row COUNT + a cell
 * accessor (rows fetched on demand, so large sets are never materialized).
 * col_min/col_max/sample <= 0 fall back to sensible defaults. Consumed by
 * timui_table_ex, which fits each column to its content (headers + a bounded row
 * sample), draws a STICKY header, and scrolls both axes. */
typedef struct {
    const TimuiStr *headers;   /* ncols labels (NULL / NULL-ptr entries allowed) */
    int             ncols;
    int             nrows;     /* virtual row count */
    TimuiCellFn     cell_fn;   /* const char *(*)(void *ud, int row, int col) */
    void           *ud;
    int             col_min;   /* per-column width floor (<=0 => default 3)   */
    int             col_max;   /* per-column width cap   (<=0 => default 24)  */
    int             sample;    /* rows sampled for width fit (<=0 => default 128) */
} TimuiTableModel;
TIMUI_API TimuiTableResult timui_table_ex(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTableModel *model, TimuiTableState state);
TIMUI_API TimuiTableResult timui_table_ex_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTableModel *model, TimuiTableState *state);

typedef struct { int depth; const char *label; int has_children; int expanded; } TimuiTreeNode;
typedef struct { int selected; int state_changed; int focused; } TimuiTreeResult;
TIMUI_API TimuiTreeResult timui_tree(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTreeNode *nodes, int count, int selected);
TIMUI_API TimuiTreeResult timui_tree_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTreeNode *nodes, int count, int *selected);

/* Flatten a DFS-ordered node list to the indices of the VISIBLE nodes (every
 * ancestor expanded): a collapsed node (has_children && !expanded) hides its
 * whole deeper-depth subtree until the depth returns to <= the collapsed node's
 * depth. Writes up to `cap` indices to out[] and returns the TOTAL visible count
 * (which may exceed cap). NULL/empty -> 0. Pure; unit-tested (tests/test_grid.c). */
TIMUI_API int timui_tree_flatten(const TimuiTreeNode *nodes, int count, int *out, int cap);

/* Scrollable tree for LARGE trees: pass the FULL node list; the widget flattens
 * to the visible nodes and WINDOWS them to the viewport (r.h rows). `selected`
 * and `scroll` are positions in the VISIBLE list (0-based). Backward-compatible
 * addition — the plain timui_tree above is unchanged. Controlled + _mut twin. */
typedef struct { int selected; int scroll; } TimuiTreeState;
typedef struct { TimuiTreeState state; int state_changed; int focused; } TimuiTreeScrollResult;
TIMUI_API TimuiTreeScrollResult timui_tree_scroll(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTreeNode *nodes, int count, TimuiTreeState state);
TIMUI_API TimuiTreeScrollResult timui_tree_scroll_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTreeNode *nodes, int count, TimuiTreeState *state);

typedef struct { char filter[64]; int selected; } TimuiCmdPaletteState;
typedef struct { TimuiCmdPaletteState state; int activated; int state_changed; } TimuiCmdPaletteResult;
TIMUI_API TimuiCmdPaletteResult timui_command_palette(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *commands, int count, TimuiCmdPaletteState state);
TIMUI_API TimuiCmdPaletteResult timui_command_palette_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *commands, int count, TimuiCmdPaletteState *state);

/* Field-attached autocomplete/combobox. `query` is caller-owned storage; the
 * widget edits it in place, filters `options`, and when an option is activated
 * copies that option back into `query` (bounded by `cap`). `selected`/`scroll`
 * are positions in the filtered list; result `selected`/`activated` are original
 * option indices, or -1 when no option matches / activates. */
typedef struct {
    char *query;
    size_t cap;
    size_t cursor;
    int scroll_x;
    int open;
    int selected;
    int scroll;
} TimuiComboboxState;
typedef struct {
    TimuiComboboxState state;
    int state_changed;
    int query_changed;
    int activated;
    int selected;
    int match_count;
    int focused;
} TimuiComboboxResult;
TIMUI_API TimuiComboboxResult timui_combobox(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *options, int count, TimuiComboboxState state);
TIMUI_API TimuiComboboxResult timui_combobox_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *options, int count, TimuiComboboxState *state);

typedef enum {
    TIMUI_TOAST_INFO = 0,
    TIMUI_TOAST_SUCCESS,
    TIMUI_TOAST_WARNING,
    TIMUI_TOAST_ERROR
} TimuiToastSeverity;
typedef struct {
    TimuiStr title;
    TimuiStr message;
    TimuiToastSeverity severity;
    uint64_t created_ms;
    uint64_t ttl_ms;      /* 0 = sticky until caller dismisses */
    int dismissed;
} TimuiToast;
typedef struct {
    int dismissed;        /* original toast index, or -1 */
    int visible_count;    /* number drawn inside the supplied rect */
} TimuiToastResult;
TIMUI_API TimuiToastResult timui_toasts(TimuiFrame *f, TimuiId id, TimuiRect r,
                                        const TimuiToast *toasts, int count,
                                        uint64_t now_ms);

/* ---- Tab bar (W2) ------------------------------------------------------ *
 * A single-row bar of labeled tabs. timui_tabs highlights *selected as a boxed,
 * radio.c-style active tab, moves the selection on Left/Right (when focused) and
 * on mouse clicks, and horizontally overflow-scrolls to keep the selected tab
 * visible when the tabs are wider than the bar. The (possibly updated) index is
 * written back through *selected and also returned.
 *
 * The geometry is factored into pure, I/O-free helpers (no frame / terminal),
 * so an app can hit-test or lay tabs out itself and the math is unit-testable in
 * isolation. Each tab occupies display_width(label)+2 columns — a one-space pad
 * each side, the ' LABEL ' box — width-aware via timui_utf8_width. */
typedef struct { int x; int w; } TimuiTabSpan;   /* a tab's [x, x+w) columns in the unscrolled bar */

/* Lay out n tabs left-to-right with `sep` columns between consecutive tabs.
 * Writes up to `max` spans into `out` (out may be NULL to only measure) and
 * returns the total content width (0 for n <= 0). */
TIMUI_API int  timui_tabs_layout(const char *const *labels, int n, int sep,
                                 TimuiTabSpan *out, int max);
/* Choose a scroll offset (columns) that keeps tab `selected` visible in a
 * `width`-column viewport, adjusting minimally from `cur_scroll`. Clamped to
 * [0, max(0, total-width)]; a tab wider than the viewport pins its left edge. */
TIMUI_API int  timui_tabs_scroll(const TimuiTabSpan *spans, int n, int selected,
                                 int width, int cur_scroll);
/* 1 if `span` overlaps the viewport [scroll, scroll+width), else 0. */
TIMUI_API int  timui_tab_visible(TimuiTabSpan span, int scroll, int width);

TIMUI_API int  timui_tabs(TimuiFrame *f, TimuiId id, TimuiRect r,
                          const char *const *labels, int n, int *selected);

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
typedef enum {
    TIMUI_TEXT_AREA_DEFAULT = 0,
    TIMUI_TEXT_AREA_ENTER_SUBMITS = 1u << 0
} TimuiTextAreaFlags;
typedef struct {
    TimuiTextAreaState state;
    int changed;
    int submitted;
    int focused;
} TimuiTextAreaResult;
TIMUI_API TimuiTextAreaResult timui_text_area_ex(TimuiFrame *f, TimuiId id, TimuiRect r,
                                                 TimuiTextAreaState state, uint32_t flags);
TIMUI_API TimuiTextAreaResult timui_text_area_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
                                                  TimuiTextAreaState *state, uint32_t flags);
TIMUI_API void timui_text_area(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiTextAreaState *state);

TIMUI_API TimuiResult timui_conpty_open(TimuiTransport *out_transport, int *out_pid);
TIMUI_API TimuiResult timui_conpty_resize(TimuiTransport *transport, int cols, int rows);
TIMUI_API void timui_conpty_close(TimuiTransport *transport, int pid);
/* Test seams for the Win32 ConPTY backend's platform-neutral guards. */
TIMUI_API size_t timui_conpty_io_chunk_for_test(size_t remaining);
TIMUI_API int    timui_conpty_size_valid_for_test(int cols, int rows);

/* ---- v0.2: terminal images -------------------------------------------- *
 * timui_image_draw records a placement emitted ON TOP of the cell diff in
 * timui_end, so it composes with the cell renderer instead of being clobbered
 * by it. Kitty transmits once by `id` and places by rect; iTerm2 emits an
 * inline File payload per draw. The caller reserves the region (draws its own
 * background and no text there). This release emits Kitty graphics and iTerm2
 * inline PNG-backed images, plus Sixel for raw RGBA pixels, PNG+RGBA sidecars,
 * and lazily decoded plain PNGs with exact palettes or bounded 16-colour
 * quantization and clipped Sixel draws. Unsupported protocols/data pairs and
 * unsupported clipped draws render a "[img]" cell placeholder. With
 * TIMUI_NO_IMAGES, the same API stays available but always uses that
 * placeholder path and emits no terminal image escape sequences. */
typedef enum {
    TIMUI_IMAGE_KIND_PNG = 0,
    TIMUI_IMAGE_KIND_RGBA,
    TIMUI_IMAGE_KIND_PNG_RGBA
} TimuiImageKind;

typedef struct TimuiImage { unsigned char *data; size_t len;
                            unsigned char *rgba; size_t rgba_len;
                            uint32_t id;
                            int px_w, px_h; TimuiImageKind kind;
                            int stride; } TimuiImage;   /* tight RGBA stride, or 0 without pixels */
TIMUI_API TimuiImage *timui_image_from_png(Timui *ui, const void *data, size_t size);
TIMUI_API TimuiImage *timui_image_from_rgba(Timui *ui, const void *rgba,
                                            int w, int h, int stride);
/* Original PNG bytes plus caller-supplied decoded RGBA pixels. This lets apps
 * avoid the lazy PNG decode path while preserving PNG passthrough for
 * Kitty/iTerm2 and giving Sixel exact pixels for emission and clipping. The
 * supplied RGBA dimensions are expected to match the PNG and drive source
 * cropping. */
TIMUI_API TimuiImage *timui_image_from_png_rgba(Timui *ui, const void *png,
                                                size_t png_size,
                                                const void *rgba,
                                                int w, int h, int stride);
TIMUI_API void        timui_image_free(Timui *ui, TimuiImage *img);
TIMUI_API void        timui_image_draw(TimuiFrame *f, TimuiImage *img, TimuiRect r);
/* Draw only the part of `img` (which maps to cell rect `full`) that lands inside
 * `visible` — i.e. crop the image to the visible sub-rect. For smoothly clipping
 * an image as it scrolls off a pane. `visible` must be within `full`. */
TIMUI_API void        timui_image_draw_clipped(TimuiFrame *f, TimuiImage *img,
                                               TimuiRect full, TimuiRect visible);
TIMUI_API TimuiImageProtocol timui_image_protocol(const Timui *ui);
TIMUI_API void        timui_force_cap(Timui *ui, TimuiCapFlags cap, int enable);
/* Override the active image cap set. Unknown protocol values clear all image
 * caps and therefore select TIMUI_IMAGE_PROTOCOL_NONE. With TIMUI_NO_IMAGES,
 * every value selects NONE. */
TIMUI_API void        timui_force_image_protocol(Timui *ui, TimuiImageProtocol protocol);

/* ---- Chart / indicator widgets (W3) ----------------------------------- *
 * Pure UI over caller-supplied values (NO DSP here): vertical bar charts with
 * a two-colour vertical gradient and floating peak-hold caps, compact
 * block-glyph sparklines, horizontal gauge/meter/progress bars over a 0..1
 * fraction, and a tick-advanced braille spinner. The peak-hold envelope and
 * gradient/peak-cap bar look are promoted from the radio example
 * (examples/radio.c + examples/radio_dsp.h). */

/* (timui_lerp_rgb is declared with the box helpers above — the barchart uses it.) */

/* Filled-cell count for `value` over a track of `size` cells: the fraction
 * clamp(value/max, 0, 1) rounded to the nearest cell. `max <= 0` treats the
 * value as already normalized (0..1). Shared by the bar chart (bar height) and
 * the gauge/meter/progress bars (fill width). Returns 0..size. */
TIMUI_API int timui_bar_cells(float value, float max, int size);

/* Peak-hold envelope for one cap: rises INSTANTLY to a higher `value`,
 * otherwise decays LINEARLY by `decay` per call, never below `value`. Fed once
 * per frame it makes a floating cap chase peaks and ease back over ~cap/decay
 * frames (promoted from radio_dsp.h radio_peak_hold). */
TIMUI_API float timui_peak_hold(float cap, float value, float decay);

/* Codepoint of the braille throbber frame for `tick` (10-frame cycle; negative
 * ticks wrap). Advance `tick` once per frame for an animated spinner. */
TIMUI_API uint32_t timui_spinner_glyph(int tick);

/* Bar-chart options. `vals` are drawn as vertical bars scaled to the rect
 * height against `max` (<= 0 => values are already 0..1), each a vertical
 * gradient from `lo` (base) to `hi` (top). A lighter peak-hold cap (the bar
 * colour lightened toward white by `cap_light`) rises instantly and decays by
 * `peak_decay` per call; `peak_decay <= 0` or a NULL state disables it.
 * `labels` (optional) draws one centred label per bar on the bottom row. */
typedef struct {
    float       max;         /* full-scale value; <= 0 => values are 0..1        */
    uint32_t    lo;          /* gradient colour at the bar base (0xRRGGBB)       */
    uint32_t    hi;          /* gradient colour at the bar top                   */
    uint32_t    track;       /* empty-cell colour behind the bars                */
    float       peak_decay;  /* per-frame cap decay in fraction units (0 = off)  */
    float       cap_light;   /* lighten the cap toward white by this (0..1)      */
    int         gap;         /* blank columns between bars (< 1 => 1)            */
    const char *const *labels; /* optional per-bar labels (NULL => none)         */
} TimuiBarOpts;

/* Caller-owned peak-hold state: one held cap (0..1 fraction) per bar. */
#define TIMUI_BAR_MAX 64
typedef struct { float caps[TIMUI_BAR_MAX]; int n; } TimuiBarState;

TIMUI_API void timui_barchart(TimuiFrame *f, TimuiRect r, const float *vals, int n,
                              TimuiBarOpts opts, TimuiBarState *st);

/* Compact one-row trend of the last min(n, r.w) samples (right-aligned) drawn
 * with the eight partial-block glyphs ▁..█; each sample maps to 0..1 (clamped). */
TIMUI_API void timui_sparkline(TimuiFrame *f, TimuiRect r, const float *history, int n,
                               TimuiStyle style);

/* Horizontal indicators over a 0..1 fraction, all thin wrappers over one fill
 * helper (style.fg = bar colour, style.bg = track colour). `progress` is a
 * determinate task bar with an "NN%" readout; `gauge` shows a live "N.NN"
 * reading; `meter` is a live level with a bright peak-hold cap tick + "N.NN"
 * readout (pass cap < 0 to hide the tick). Fractions clamp to [0,1]; the
 * readout is dropped when the rect is too narrow to fit it. */
TIMUI_API void timui_progress(TimuiFrame *f, TimuiRect r, float frac, TimuiStyle style);
TIMUI_API void timui_gauge(TimuiFrame *f, TimuiRect r, float frac, TimuiStyle style);
TIMUI_API void timui_meter(TimuiFrame *f, TimuiRect r, float level, float cap, TimuiStyle style);

/* Braille/ascii throbber advanced by `tick`, drawn as one glyph at (x,y). */
TIMUI_API void timui_spinner(TimuiFrame *f, int x, int y, int tick, TimuiStyle style);

/* ---- W4: syntax highlighting + read-only code viewer ------------------ *
 * A tiny, table-driven, allocation-free lexer (promoted from the chat/sqlite
 * examples) that emits coloured token spans for C / sh / python / sql, with a
 * generic fallback, plus a read-only scrolling code viewer built on top. The
 * scanner emits only NON-default spans in source order; the gaps between them
 * are implicitly TIMUI_HL_TEXT (never emitted), so a renderer walks
 * "gap, token, gap, token, …" trivially. Every scan is bounded: it advances by
 * at least one byte, reads only within [0,len), and treats bytes as unsigned so
 * non-ASCII input can never be misclassified or overrun. */
typedef enum {
    TIMUI_HL_TEXT = 0, TIMUI_HL_KEYWORD, TIMUI_HL_TYPE, TIMUI_HL_STRING,
    TIMUI_HL_CHAR, TIMUI_HL_COMMENT, TIMUI_HL_NUMBER, TIMUI_HL_PREPROC,
    TIMUI_HL_PUNCT
} TimuiHlClass;

/* A highlighted span: byte offset + length into the source, and its class. */
typedef struct { int off; int len; TimuiHlClass cls; } TimuiHlTok;

/* Tokenize `src` (length `len`) in language `lang` ("c", "sh"/"bash",
 * "python"/"py", "sql"; NULL/""/unknown = generic). Writes up to `max` NON-default
 * spans to `out` in source order (gaps are TIMUI_HL_TEXT); returns the token count
 * (always <= max — the scan stops the moment the buffer is full). Pure. */
TIMUI_API int      timui_highlight(const char *src, int len, const char *lang,
                                   TimuiHlTok *out, int max);

/* Default 0xRRGGBB colour for a token class (a Night-Owl-ish palette matching
 * the chat example); TIMUI_HL_TEXT and any out-of-range class map to the code
 * foreground. */
TIMUI_API uint32_t timui_hl_color(TimuiHlClass cls);

/* Clamp a code-view top-line scroll offset to [0, max(0, nlines - visible)] so
 * neither end scrolls past content. Pure; used by timui_code. */
TIMUI_API int      timui_code_scroll_clamp(int scroll, int nlines, int visible);

/* Read-only syntax-highlighted code viewer: fills `r` with a subtle code
 * background, then draws the source (split on '\n') with a line-number gutter and
 * per-token syntax colours, scrolled vertically by *scroll — clamped in place to
 * a valid range. Lines are NOT wrapped; each clips at the right edge of `r`. A
 * NULL `scroll` shows the top; a NULL frame/src is a no-op. */
TIMUI_API void     timui_code(TimuiFrame *f, TimuiRect r, const char *src, int len,
                              const char *lang, int *scroll);

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
    int               cell_px_w, cell_px_h;   /* 0 == terminal did not report pixel geometry */
    int               should_quit;
    /* One feed reads up to 256 bytes and can emit one event PER byte (e.g. a
     * drag-drop path typed as text), so the queue must hold a whole read plus a
     * deferred ESC — 16 dropped all but the first 16 chars of a dropped path. */
    TimuiEvent        events[512];
    int               event_count;
    struct { TimuiRect clip; int has_clip; } clip_stack[8];
    int               clip_count;
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

static int timui_allocator_valid_(const TimuiAllocator *alloc){
    return alloc && alloc->alloc && alloc->realloc && alloc->free;
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
    TimuiEvent queued;
    if(ev->kind == TIMUI_EVENT_PASTE){
        size_t start, room, copy, orig_len, k;
        if(ui->trace_fd >= 0)
            trace_write_(ui->trace_fd, "PASTE ", (const unsigned char *)ev->as.paste.ptr, ev->as.paste.len);
        orig_len = ev->as.paste.len;
        start = (size_t)ui->paste_len;
        room = sizeof(ui->paste_buf) - start;
        copy = orig_len < room ? orig_len : room;
        if(copy == 0){ ui->events_dropped++; return; }
        for(k = 0; k < copy; k++) ui->paste_buf[ui->paste_len++] = ev->as.paste.ptr[k];
        queued = *ev;
        queued.as.paste.ptr = ui->paste_buf + start;
        queued.as.paste.len = copy;
        ev = &queued;
        if(copy < orig_len) ui->events_dropped++;
    }
    if(ui->event_count < (int)(sizeof(ui->events) / sizeof(ui->events[0])))
        ui->events[ui->event_count++] = *ev;
    else
        ui->events_dropped++;
}
static void timui_append_text_cp_(Timui *ui, uint32_t cp){
    char enc[4];
    int enclen;
    if(!ui) return;
    if(cp < 0x20 || cp == 0x7f || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return;
    enclen = timui_utf8_encode_(cp, enc);
    if(enclen > 0 && ui->text_in_len + enclen <= (int)sizeof(ui->text_in)){
        int ei;
        for(ei = 0; ei < enclen; ei++) ui->text_in[ui->text_in_len++] = enc[ei];
    }
}
static void timui_append_paste_bytes_(Timui *ui, const char *ptr, size_t len){
    size_t pk = 0;
    while(ui && pk < len && ui->text_in_len < (int)sizeof(ui->text_in)){
        unsigned char pc = (unsigned char)ptr[pk];
        uint32_t cp = 0;
        int adv;
        if(pc == 0 || pc == 0x7f){ pk++; continue; }
        if(pc < 0x20){
            if(pc != '\n' && pc != '\r' && pc != '\t'){ pk++; continue; }
            if(ui->text_in_len < (int)sizeof(ui->text_in)) ui->text_in[ui->text_in_len++] = (char)pc;
            pk++;
            continue;
        }
        adv = timui_utf8_decode(ptr + pk, len - pk, &cp);
        if(adv == 0){ cp = 0xFFFD; adv = (int)(len - pk); }
        if(adv < 0) adv = 1;
        timui_append_text_cp_(ui, cp);
        pk += (size_t)adv;
    }
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

static void timui_set_cell_pixels_(Timui *ui, int cell_w_px, int cell_h_px){
    if(!ui) return;
    if(cell_w_px > 0 && cell_h_px > 0){
        ui->cell_px_w = cell_w_px;
        ui->cell_px_h = cell_h_px;
    }else{
        ui->cell_px_w = 0;
        ui->cell_px_h = 0;
    }
}

static void timui_set_terminal_pixels_(Timui *ui, int cols, int rows, int px_w, int px_h){
    if(!ui || cols <= 0 || rows <= 0 || px_w <= 0 || px_h <= 0){
        timui_set_cell_pixels_(ui, 0, 0);
        return;
    }
    timui_set_cell_pixels_(ui, px_w / cols, px_h / rows);
}

TIMUI_API TimuiResult timui_open_for_test(Timui **out_ui, TimuiTransport transport, int w, int h, const TimuiAllocator *alloc){
    Timui *ui;
    TimuiResult r;
    if(!out_ui || w <= 0 || h <= 0 || !timui_allocator_valid_(alloc)) return TIMUI_ERR_INVALID_ARGUMENT;
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
    if(r != TIMUI_OK){
        if(transport.close) transport.close(&transport);
        alloc->free(alloc->userdata, ui, sizeof *ui);
        return r;
    }
    *out_ui = ui;
    return TIMUI_OK;
}
TIMUI_API void timui_set_cell_pixels_for_test(Timui *ui, int cell_w_px, int cell_h_px){
    timui_set_cell_pixels_(ui, cell_w_px, cell_h_px);
}

static int g_fsetfl_fail_for_test = 0;
TIMUI_API void timui_open_fail_fsetfl_for_test(int on){ g_fsetfl_fail_for_test = on; }

/* ---- terminal restoration on signal (W6) ------------------------------ *
 * An external termination signal (SIGTERM/SIGHUP/SIGQUIT — kill, window
 * close, Ctrl-\) must not leave the terminal in raw mode. timui_open installs
 * a handler that restores the screen + termios before the process dies. This
 * needs ONE piece of global state — a static Timui* — which is a documented
 * carve-out from the "no global state" rule, justified by the safety
 * requirement (a bricked terminal is the failure mode). Single-instance
 * assumption: one controlling terminal per process.
 *
 * The handler uses a bounded best-effort path: restore input fd flags and
 * termios first, then write teardown escapes directly with single write() calls
 * (no transport abstraction, no poll/retry loop that can hang in a handler). */
static Timui *g_sig_restore_ui = NULL;

static void timui_restore_input_flags(Timui *ui){
    if(!ui || !ui->input_flags_saved) return;
    (void)fcntl(ui->fd.read_fd, F_SETFL, ui->input_flags);
    ui->input_flags_saved = 0;
}

TIMUI_API void timui_restore_terminal(Timui *ui){
    if(!ui) return;
    timui_restore_input_flags(ui);
    if(ui->termios_active) timui_termios_restore(&ui->termios);
    if(ui->screen_active) timui_screen_exit(&ui->transport, &ui->screen);
}
static void timui_signal_write_(int fd, const char *s, size_t n){
    if(fd >= 0) (void)write(fd, s, n);
}
#define TIMUI_SIG_EMIT(ui, lit) timui_signal_write_((ui)->fd.write_fd, (lit), sizeof(lit) - 1)
static void timui_signal_screen_exit_(Timui *ui){
    uint32_t flags;
    if(!ui || !ui->screen_active) return;
    flags = ui->screen.flags;
    if(flags & TIMUI_FLAG_FOCUS_EVENTS)    TIMUI_SIG_EMIT(ui, "\x1b[?1004l");
    if(flags & TIMUI_FLAG_BRACKETED_PASTE) TIMUI_SIG_EMIT(ui, "\x1b[?2004l");
    if(flags & TIMUI_FLAG_MOUSE){          TIMUI_SIG_EMIT(ui, "\x1b[?1006l"); TIMUI_SIG_EMIT(ui, "\x1b[?1000l"); }
    if(flags & TIMUI_FLAG_KITTY_KEYBOARD)  TIMUI_SIG_EMIT(ui, "\x1b[<u");
    TIMUI_SIG_EMIT(ui, "\x1b[?25h");
    if(flags & TIMUI_FLAG_ALT_SCREEN)      TIMUI_SIG_EMIT(ui, "\x1b[?1049l");
    TIMUI_SIG_EMIT(ui, "\x1b[?7h");
}
#undef TIMUI_SIG_EMIT
static void timui_signal_restore_terminal_(Timui *ui){
    if(!ui) return;
    timui_restore_input_flags(ui);
    if(ui->termios_active) (void)timui_termios_restore(&ui->termios);
    timui_signal_screen_exit_(ui);
}
static void timui_restore_previous_signal(Timui *ui, int sig){
    if(!ui){ signal(sig, SIG_DFL); return; }
    if(sig == SIGTERM && ui->prev_sigterm_saved){
        sigaction(SIGTERM, &ui->prev_sigterm, NULL); ui->prev_sigterm_saved = 0; return;
    }
    if(sig == SIGHUP && ui->prev_sighup_saved){
        sigaction(SIGHUP, &ui->prev_sighup, NULL); ui->prev_sighup_saved = 0; return;
    }
    if(sig == SIGQUIT && ui->prev_sigquit_saved){
        sigaction(SIGQUIT, &ui->prev_sigquit, NULL); ui->prev_sigquit_saved = 0; return;
    }
    signal(sig, SIG_DFL);
}
static void timui_sig_restore(int sig){
    Timui *ui = g_sig_restore_ui;
    timui_signal_restore_terminal_(ui);
    if(g_sig_restore_ui == ui) g_sig_restore_ui = NULL;
    timui_restore_previous_signal(ui, sig);
    raise(sig);
}
static void timui_install_sig_handlers(Timui *ui){
    struct sigaction sa;
    if(!ui || !(ui->cfg.flags & TIMUI_FLAG_RESTORE_ON_EXIT) || (!ui->termios_active && !ui->screen_active)) return;
    g_sig_restore_ui = ui;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = timui_sig_restore;
    sigemptyset(&sa.sa_mask);
#ifdef SA_RESTART
    sa.sa_flags = SA_RESTART;
#endif
    ui->prev_sigterm_saved = (sigaction(SIGTERM, &sa, &ui->prev_sigterm) == 0);
    ui->prev_sighup_saved  = (sigaction(SIGHUP,  &sa, &ui->prev_sighup)  == 0);
    ui->prev_sigquit_saved = (sigaction(SIGQUIT, &sa, &ui->prev_sigquit) == 0);
}
static void timui_remove_sig_handlers(Timui *ui){
    if(g_sig_restore_ui == ui) g_sig_restore_ui = NULL;
    if(!ui) return;
    if(ui->prev_sigterm_saved){ sigaction(SIGTERM, &ui->prev_sigterm, NULL); ui->prev_sigterm_saved = 0; }
    if(ui->prev_sighup_saved){  sigaction(SIGHUP,  &ui->prev_sighup,  NULL); ui->prev_sighup_saved = 0; }
    if(ui->prev_sigquit_saved){ sigaction(SIGQUIT, &ui->prev_sigquit, NULL); ui->prev_sigquit_saved = 0; }
}
static void timui_open_cleanup_failed(Timui *ui){
    if(!ui) return;
    timui_restore_terminal(ui);
    if(ui->termios_active){ timui_termios_destroy(&ui->termios); ui->termios_active = 0; }
    if(ui->trace_fd >= 0){ close(ui->trace_fd); ui->trace_fd = -1; }
}

TIMUI_API TimuiResult timui_open(const TimuiConfig *cfg, Timui **out_ui){
    Timui *ui;
    TimuiAllocator al;
    int input_flags;
    int input_is_tty, output_is_tty;
    int w = 80, h = 24;
    int px_w = 0, px_h = 0;
    TimuiResult r;
    if(!cfg || !out_ui) return TIMUI_ERR_INVALID_ARGUMENT;
    *out_ui = NULL;
    if(cfg->input_fd < 0 || cfg->output_fd < 0) return TIMUI_ERR_INVALID_ARGUMENT;
    input_flags = fcntl(cfg->input_fd, F_GETFL, 0);
    if(input_flags < 0) return TIMUI_ERR_OS;
    if(fcntl(cfg->output_fd, F_GETFL, 0) < 0) return TIMUI_ERR_OS;
    input_is_tty = isatty(cfg->input_fd);
    output_is_tty = isatty(cfg->output_fd);
    if(cfg->allocator.alloc || cfg->allocator.realloc || cfg->allocator.free){
        if(!timui_allocator_valid_(&cfg->allocator)) return TIMUI_ERR_INVALID_ARGUMENT;
        al = cfg->allocator;
    }else{
        al = timui_default_allocator();
    }
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
    if(timui_term_size_pixels(cfg->output_fd, &w, &h, &px_w, &px_h) != TIMUI_OK){
        w = 80; h = 24; px_w = 0; px_h = 0;
    }
    if(w <= 0 || h <= 0){ w = 80; h = 24; px_w = 0; px_h = 0; }
    ui->input_flags = input_flags;
    ui->input_flags_saved = 1;
    if(g_fsetfl_fail_for_test || fcntl(cfg->input_fd, F_SETFL, input_flags | O_NONBLOCK) < 0){
        timui_open_cleanup_failed(ui);
        al.free(al.userdata, ui, sizeof *ui);
        return TIMUI_ERR_OS;
    }
    if(input_is_tty){
        r = timui_termios_enter(&ui->termios, cfg->input_fd);
        if(r != TIMUI_OK){
            timui_open_cleanup_failed(ui);
            al.free(al.userdata, ui, sizeof *ui);
            return r;
        }
        ui->termios_active = 1;
    }
    if(output_is_tty){
        timui_screen_enter(&ui->transport, &ui->screen, cfg->flags, timui_str_from_cstr(cfg->title));
        ui->screen_active = 1;
    }
    r = timui_setup(ui, w, h);
    if(r != TIMUI_OK){
        timui_open_cleanup_failed(ui);
        al.free(al.userdata, ui, sizeof *ui);
        return r;
    }
    timui_set_terminal_pixels_(ui, w, h, px_w, px_h);
    *out_ui = ui;
    timui_install_sig_handlers(ui);   /* W6: restore the terminal on SIGTERM/SIGHUP/SIGQUIT */
    return TIMUI_OK;
}
TIMUI_API void timui_close(Timui *ui){
    TimuiAllocator al;
    if(!ui) return;
    timui_restore_terminal(ui);
    timui_remove_sig_handlers(ui);    /* W6: stop intercepting after the terminal is restored */
    if(ui->termios_active) timui_termios_destroy(&ui->termios);
    if(ui->have_buffers){ timui_cells_destroy(&ui->curr); timui_cells_destroy(&ui->prev); }
    if(ui->have_postq) timui_mpsc_destroy(&ui->postq);
    timui_interact_destroy(&ui->ia);   /* V24: free the dynamic tab_order */
    if(ui->have_ids) timui_id_stack_destroy(&ui->ids);
    if(ui->trace_fd >= 0) close(ui->trace_fd);
    if(ui->have_transport && ui->transport.close) ui->transport.close(&ui->transport);
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
        for(pe = 0; pe < ui->pending_enter_count; pe++){
            ui->enter_at[pe] = ui->pending_enter_at[pe];
            ui->enter_mods[pe] = ui->pending_enter_mods[pe];
        }
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
        TimuiEvent focus_events[sizeof(ui->events) / sizeof(ui->events[0])];
        int focus_count = 0;
        int saw_mouse_press = 0, saw_mouse_release = 0;
        int press_x = 0, press_y = 0;
        while(timui_poll_event(ui, &ev)){
            if(ev.kind == TIMUI_EVENT_MOUSE){
                int mx = ev.as.mouse.x - 1;
                int my = ev.as.mouse.y - 1;
                ui->mouse_wheel += ev.as.mouse.wheel_y;   /* expose wheel to the app */
                ui->mouse_x = mx; ui->mouse_y = my;
                if(ev.as.mouse.wheel_y == 0 &&
                   (ev.as.mouse.motion || ev.as.mouse.button == 0 || ev.as.mouse.released)){
                    int down = ev.as.mouse.motion ? (ev.as.mouse.button == 0)
                                                  : (ev.as.mouse.button == 0 && ev.as.mouse.pressed);
                    timui_interact_set_mouse(&ui->ia, mx, my, down);
                    if(!ev.as.mouse.motion && ev.as.mouse.button == 0 && ev.as.mouse.pressed){
                        saw_mouse_press = 1; press_x = mx; press_y = my; ui->mouse_clicked = 1;
                    }
                    if(!ev.as.mouse.motion && ev.as.mouse.released) saw_mouse_release = 1;
                }
            } else if(ev.kind == TIMUI_EVENT_KEY){
                ui->key_pressed = ev.as.key.key;   /* app-level key detection */
                ui->key_mods = ev.as.key.mods;
                if(ev.as.key.key == TIMUI_KEY_TAB) timui_interact_set_keys(&ui->ia, 1, 0);
                else if(ev.as.key.key == TIMUI_KEY_ENTER){
                    timui_interact_set_keys(&ui->ia, 0, 1);
                    /* record the Enter's position in the text stream (input_field
                     * segments submits on these; excess past the cap just merges). */
                    if(ui->enter_count < (int)(sizeof(ui->enter_at)/sizeof(ui->enter_at[0]))){
                        ui->enter_at[ui->enter_count] = ui->text_in_len;
                        ui->enter_mods[ui->enter_count] = ev.as.key.mods;
                        ui->enter_count++;
                    }
                }
                else if(ev.as.key.key == TIMUI_KEY_BACKSPACE) ui->key_in |= TIMUI_KEYIN_BACKSPACE;
                else if(ev.as.key.key == TIMUI_KEY_LEFT)   ui->key_in |= TIMUI_KEYIN_LEFT;
                else if(ev.as.key.key == TIMUI_KEY_RIGHT)  ui->key_in |= TIMUI_KEYIN_RIGHT;
                else if(ev.as.key.key == TIMUI_KEY_HOME)   ui->key_in |= TIMUI_KEYIN_HOME;
                else if(ev.as.key.key == TIMUI_KEY_END)    ui->key_in |= TIMUI_KEYIN_END;
                else if(ev.as.key.key == TIMUI_KEY_DELETE) ui->key_in |= TIMUI_KEYIN_DELETE;
                else if(ev.as.key.key == TIMUI_KEY_UP) ui->key_in |= TIMUI_KEYIN_UP;
                else if(ev.as.key.key == TIMUI_KEY_DOWN) ui->key_in |= TIMUI_KEYIN_DOWN;
                else if(ev.as.key.key == TIMUI_KEY_UNKNOWN &&
                        (ev.as.key.mods & ~TIMUI_MOD_SHIFT) == TIMUI_MOD_NONE){
                    uint32_t cp = ev.as.key.codepoint;
                    timui_append_text_cp_(ui, cp);
                }
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
                timui_append_text_cp_(ui, cp);
            } else if(ev.kind == TIMUI_EVENT_PASTE){
                timui_append_paste_bytes_(ui, ev.as.paste.ptr, ev.as.paste.len);
            } else if(ev.kind == TIMUI_EVENT_FOCUS){
                if(focus_count < (int)(sizeof(focus_events) / sizeof(focus_events[0])))
                    focus_events[focus_count++] = ev;
            }
        }
        if(focus_count > 0){
            int fi;
            ui->event_count = 0;
            for(fi = 0; fi < focus_count; fi++) ui->events[ui->event_count++] = focus_events[fi];
        }
        if(saw_mouse_press && saw_mouse_release)
            timui_interact_set_mouse(&ui->ia, press_x, press_y, ui->ia.mouse_down);
        if(saw_mouse_press){
            ui->mouse_x = press_x;
            ui->mouse_y = press_y;
        }
        timui_interact_begin(&ui->ia);
        if(saw_mouse_press) ui->ia.mouse_pressed = 1;
        if(saw_mouse_release) ui->ia.mouse_released = 1;
        ui->paste_len = 0;   /* queued paste slices have been consumed */
    }
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
    TimuiCellBuffer next_prev, next_curr;
    if(!ui || w <= 0 || h <= 0) return TIMUI_ERR_INVALID_ARGUMENT;
    memset(&next_prev, 0, sizeof next_prev);
    memset(&next_curr, 0, sizeof next_curr);
    /* Allocate the replacement buffers before touching the live pair. A failed
     * resize then leaves curr/prev/ui dimensions identical, with no rollback
     * allocation needed. */
    r = timui_cells_init(&next_prev, w, h, &ui->alloc);
    if(r != TIMUI_OK) return r;
    r = timui_cells_init(&next_curr, w, h, &ui->alloc);
    if(r != TIMUI_OK){ timui_cells_destroy(&next_prev); return r; }
    timui_cells_destroy(&ui->prev);
    timui_cells_destroy(&ui->curr);
    ui->prev = next_prev;
    ui->curr = next_curr;
    ui->have_buffers = 1;
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
TIMUI_API TimuiImageProtocol timui_image_protocol(const Timui *ui){
    return ui ? timui_caps_image_protocol(&ui->caps) : TIMUI_IMAGE_PROTOCOL_NONE;
}
TIMUI_API void timui_force_image_protocol(Timui *ui, TimuiImageProtocol protocol){
    const uint32_t mask = (uint32_t)(TIMUI_CAP_KITTY_GRAPHICS |
                                    TIMUI_CAP_SIXEL_GRAPHICS |
                                    TIMUI_CAP_ITERM2_IMAGES);
    if(!ui) return;
    ui->caps.flags &= ~mask;
#ifdef TIMUI_NO_IMAGES
    (void)protocol;
    return;
#endif
    switch(protocol){
    case TIMUI_IMAGE_PROTOCOL_KITTY:
        ui->caps.flags |= TIMUI_CAP_KITTY_GRAPHICS;
        break;
    case TIMUI_IMAGE_PROTOCOL_SIXEL:
        ui->caps.flags |= TIMUI_CAP_SIXEL_GRAPHICS;
        break;
    case TIMUI_IMAGE_PROTOCOL_ITERM2:
        ui->caps.flags |= TIMUI_CAP_ITERM2_IMAGES;
        break;
    case TIMUI_IMAGE_PROTOCOL_NONE:
    default:
        break;
    }
}
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
    if(!s || !timui_allocator_valid_(alloc) || cap == 0) return TIMUI_ERR_INVALID_ARGUMENT;
    memset(s, 0, sizeof *s);
    if(cap > SIZE_MAX / sizeof(TimuiId)) return TIMUI_ERR_OUT_OF_MEMORY;
    s->alloc = *alloc;
    s->root  = TIMUI_ID_ROOT;
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
    if(!q || !timui_allocator_valid_(alloc) || cap == 0) return TIMUI_ERR_INVALID_ARGUMENT;
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
    if(!q || !timui_allocator_valid_(alloc)) return TIMUI_ERR_INVALID_ARGUMENT;
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
#ifndef TIMUI_NO_THREADS
    if(!q->lock){
        q->head = q->tail = NULL;
        q->pending = 0;
        memset(&q->alloc, 0, sizeof q->alloc);
        return;
    }
#endif
    while(timui_mpsc_recv(q, &t, NULL, &s)){ }       /* drain remaining nodes */
#ifndef TIMUI_NO_THREADS
    if(q->lock){
        pthread_mutex_destroy((pthread_mutex_t *)q->lock);
        q->alloc.free(q->alloc.userdata, q->lock, sizeof(pthread_mutex_t));
        q->lock = NULL;
    }
#endif
    q->head = q->tail = NULL;
    q->pending = 0;
    memset(&q->alloc, 0, sizeof q->alloc);
}
TIMUI_API int timui_mpsc_post(TimuiMpsc *q, uint32_t type, const void *data, size_t size){
    TimuiMpscNode *n;
    if(!q) return 0;
    if(!timui_allocator_valid_(&q->alloc)) return 0;
#ifndef TIMUI_NO_THREADS
    if(!q->lock) return 0;
#endif
    if(size > 0 && !data) return 0;
    if(size > SIZE_MAX - sizeof(*n)) return 0;   /* overflow guard (cf. msgq_emit) */
    TIMUI_MPSC_LOCK(q);
    n = (TimuiMpscNode *)q->alloc.alloc(q->alloc.userdata, sizeof(*n) + size);
    if(!n){ TIMUI_MPSC_UNLOCK(q); return 0; }
    n->next = NULL; n->type = type; n->size = size;
    if(size > 0 && data) memcpy(n->data, data, size);
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
#ifndef TIMUI_NO_THREADS
    if(!q->lock) return 0;
#endif
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
    TIMUI_MPSC_LOCK(q);
    q->alloc.free(q->alloc.userdata, n, sizeof(*n) + n->size);
    TIMUI_MPSC_UNLOCK(q);
    return 1;
}
TIMUI_API int timui_mpsc_empty(TimuiMpsc *q){
    int e;
    if(!q) return 1;
#ifndef TIMUI_NO_THREADS
    if(!q->lock) return 1;
#endif
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
    if(!a || !timui_allocator_valid_(alloc) || cap == 0) return TIMUI_ERR_INVALID_ARGUMENT;
    a->alloc = alloc;
    a->cap   = cap;
    a->off   = 0;
    a->base  = (unsigned char *)alloc->alloc(alloc->userdata, cap);
    if(!a->base){ a->cap = 0; return TIMUI_ERR_OUT_OF_MEMORY; }
    return TIMUI_OK;
}
TIMUI_API void *timui_arena_alloc(TimuiArena *a, size_t size, size_t align){
    uintptr_t base, addr, aligned_addr, delta, mask;
    size_t aligned;
    if(!a || align == 0) return NULL;
    if(align & (align - 1)) return NULL;     /* alignment must be a power of two */
    if(size == 0) size = 1;
    base = (uintptr_t)a->base;
    if((uintptr_t)a->off > UINTPTR_MAX - base) return NULL;
    addr = base + (uintptr_t)a->off;
    mask = (uintptr_t)align - 1u;
    if(addr + mask < addr) return NULL;
    aligned_addr = (addr + mask) & ~mask;
    if(aligned_addr < base) return NULL;
    delta = aligned_addr - base;
    if(delta > (uintptr_t)SIZE_MAX) return NULL;
    aligned = (size_t)delta;
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
    if(!buf) return TIMUI_ERR_INVALID_ARGUMENT;
    memset(buf, 0, sizeof *buf);
    if(w <= 0 || h <= 0 || !timui_allocator_valid_(alloc)) return TIMUI_ERR_INVALID_ARGUMENT;
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
    if(!buf->cells){
        if(!timui_allocator_valid_(alloc)) return TIMUI_ERR_INVALID_ARGUMENT;
        buf->alloc = *alloc;
    }
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
    if(cp >= 0x80 && cp <= 0x9F) return 0;                       /* C1 control */
    if(cp == 0xFFFD) return 1;
    if((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
       (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
       (cp >= 0xFE20 && cp <= 0xFE2F)) return 0;                /* combining */
    if(cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F) ||
       (cp >= 0xE0100 && cp <= 0xE01EF) ||
       (cp >= 0x1F3FB && cp <= 0x1F3FF)) return 0;              /* joiner / variation / emoji modifier */
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
    if(!buf || !buf->cells || x < 0 || y < 0 || x >= buf->w || y >= buf->h) return;
    if(buf->has_clip && (x < buf->clip.x || y < buf->clip.y ||
       x >= buf->clip.x + buf->clip.w || y >= buf->clip.y + buf->clip.h)) return;
    memset(&c, 0, sizeof c);
    w = timui_utf8_width(cp);
    if(w > 1 && (x + 1 >= buf->w ||
       (buf->has_clip && (x + 1 < buf->clip.x || x + 1 >= buf->clip.x + buf->clip.w)))) return;
    c.codepoint = cp;
    c.fg = st.fg;
    c.bg = st.bg;
    c.attrs = st.attrs;
    c.width = (uint16_t)(w > 1 ? 2 : 1);
    c.hyperlink_id = link;
    timui_cells_put(buf, x, y, &c);
    /* wide glyph: blank the continuation cell so stale content isn't left behind */
    if(w > 1){
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
        if(adv <= 0){ cp = 0xFFFD; adv = 1; }
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
    TimuiRect out = {0, 0, 0, 0};
    int64_t x1, y1, x2, y2;
    if(!buf || r.w <= 0 || r.h <= 0) return out;
    x1 = r.x; y1 = r.y;
    x2 = (int64_t)r.x + (int64_t)r.w;
    y2 = (int64_t)r.y + (int64_t)r.h;
    if(x2 <= 0 || y2 <= 0 || x1 >= buf->w || y1 >= buf->h) return out;
    if(x1 < 0) x1 = 0;
    if(y1 < 0) y1 = 0;
    if(x2 > buf->w) x2 = buf->w;
    if(y2 > buf->h) y2 = buf->h;
    if(x2 <= x1 || y2 <= y1) return out;
    out.x = (int)x1;
    out.y = (int)y1;
    out.w = (int)(x2 - x1);
    out.h = (int)(y2 - y1);
    return out;
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
static void emit_osc_string_sanitized(TimuiTransport *t, const char *s){
    size_t i = 0, len;
    if(!s) return;
    len = strlen(s);
    while(i < len){
        uint32_t cp = 0;
        int adv = timui_utf8_decode(s + i, len - i, &cp);
        char out[4];
        int n;
        if(adv <= 0){ cp = 0xFFFDu; adv = 1; }
        if(cp < 0x20u || cp == 0x7Fu || (cp >= 0x80u && cp <= 0x9Fu)){
            i += (size_t)adv;
            continue;
        }
        n = timui_utf8_encode_(cp, out);
        if(n > 0) r_emit(t, out, (size_t)n);
        i += (size_t)adv;
    }
}
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
    if(uri) emit_osc_string_sanitized(t, uri);
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
static uint32_t render_safe_cp(uint32_t cp){
    if(cp == 0) return ' ';
    if(cp < 0x20u || cp == 0x7Fu || (cp >= 0x80u && cp <= 0x9Fu)) return ' ';
    if(cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) return 0xFFFDu;
    return cp;
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
            gn = timui_utf8_encode_(render_safe_cp(cc->codepoint), gb);
            r_emit(t, gb, (size_t)gn);
            r->last_x = x + (cc->width >= 2 ? 2 : 1);   /* wide glyph advances cursor by 2 */
            r->last_y = y;
        }
    }
    if(r->have_last_link){
        emit_osc8(t, NULL);
        r->last_link = 0;
        r->have_last_link = 0;
        r->last_link_uri[0] = '\0';
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
/* ---- stylesheet parser/resolver --------------------------------------- *
 * A deliberately small TCSS-like layer over TimuiStyle. It parses one selector
 * per rule and resolves by simple specificity + source order. */
#define TIMUI_SS_NAME_MAX 63

struct TimuiStyleRule {
    TimuiWidgetKind kind;
    char id[TIMUI_SS_NAME_MAX + 1];
    char klass[TIMUI_SS_NAME_MAX + 1];
    uint32_t states;
    int specificity;
    int order;
    uint32_t props;
    TimuiStyle style;
    uint32_t attr_props;
    uint32_t attr_values;
    uint32_t border;
    int padding;
    int gap;
    uint32_t gradient_lo;
    uint32_t gradient_hi;
};

typedef struct {
    const char *s;
    size_t len;
    size_t pos;
} TimuiStyleParser;

static int ss_alloc_valid_(const TimuiAllocator *a){
    return a && a->alloc && a->realloc && a->free;
}
static int ss_is_space_(char c){
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}
static int ss_is_alpha_(char c){
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int ss_is_name_(char c){
    return ss_is_alpha_(c) || (c >= '0' && c <= '9') || c == '-';
}
static void ss_skip_ws_(TimuiStyleParser *p){
    while(p->pos < p->len && ss_is_space_(p->s[p->pos])) p->pos++;
}
static int ss_at_(TimuiStyleParser *p, char c){
    ss_skip_ws_(p);
    return p->pos < p->len && p->s[p->pos] == c;
}
static int ss_take_(TimuiStyleParser *p, char c){
    if(!ss_at_(p, c)) return 0;
    p->pos++;
    return 1;
}
static int ss_ident_(TimuiStyleParser *p, char *out, size_t cap){
    size_t n = 0;
    ss_skip_ws_(p);
    if(p->pos >= p->len || !ss_is_alpha_(p->s[p->pos])) return 0;
    while(p->pos < p->len && ss_is_name_(p->s[p->pos])){
        if(n + 1 < cap) out[n++] = p->s[p->pos];
        else return 0;
        p->pos++;
    }
    out[n] = '\0';
    return 1;
}
static int ss_streq_(const char *a, const char *b){
    return strcmp(a ? a : "", b ? b : "") == 0;
}
static int ss_widget_kind_(const char *name, TimuiWidgetKind *out){
    if(ss_streq_(name, "label")) *out = TIMUI_WIDGET_LABEL;
    else if(ss_streq_(name, "panel")) *out = TIMUI_WIDGET_PANEL;
    else if(ss_streq_(name, "button")) *out = TIMUI_WIDGET_BUTTON;
    else if(ss_streq_(name, "input")) *out = TIMUI_WIDGET_INPUT;
    else if(ss_streq_(name, "textarea") || ss_streq_(name, "text-area")) *out = TIMUI_WIDGET_TEXT_AREA;
    else if(ss_streq_(name, "listbox")) *out = TIMUI_WIDGET_LISTBOX;
    else if(ss_streq_(name, "table")) *out = TIMUI_WIDGET_TABLE;
    else if(ss_streq_(name, "tree")) *out = TIMUI_WIDGET_TREE;
    else if(ss_streq_(name, "menu")) *out = TIMUI_WIDGET_MENU;
    else if(ss_streq_(name, "toast")) *out = TIMUI_WIDGET_TOAST;
    else if(ss_streq_(name, "split")) *out = TIMUI_WIDGET_SPLIT;
    else return 0;
    return 1;
}
static int ss_state_(const char *name, uint32_t *out){
    if(ss_streq_(name, "focused")) *out = TIMUI_STYLE_STATE_FOCUSED;
    else if(ss_streq_(name, "hovered") || ss_streq_(name, "hover")) *out = TIMUI_STYLE_STATE_HOVERED;
    else if(ss_streq_(name, "active") || ss_streq_(name, "pressed")) *out = TIMUI_STYLE_STATE_ACTIVE;
    else if(ss_streq_(name, "disabled")) *out = TIMUI_STYLE_STATE_DISABLED;
    else if(ss_streq_(name, "selected")) *out = TIMUI_STYLE_STATE_SELECTED;
    else return 0;
    return 1;
}
static int ss_hex_(char c){
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return 10 + c - 'a';
    if(c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}
static int ss_color_(TimuiStyleParser *p, uint32_t *out){
    uint32_t v = 0;
    int i;
    char word[TIMUI_SS_NAME_MAX + 1];
    ss_skip_ws_(p);
    if(p->pos < p->len && p->s[p->pos] == '#'){
        p->pos++;
        for(i = 0; i < 6; i++){
            int h;
            if(p->pos >= p->len) return 0;
            h = ss_hex_(p->s[p->pos++]);
            if(h < 0) return 0;
            v = (v << 4) | (uint32_t)h;
        }
        if(p->pos < p->len && ss_is_name_(p->s[p->pos])) return 0;
        *out = v;
        return 1;
    }
    if(!ss_ident_(p, word, sizeof word)) return 0;
    if(!ss_streq_(word, "default")) return 0;
    *out = TIMUI_COLOR_DEFAULT;
    return 1;
}
static int ss_bool_(TimuiStyleParser *p, int *out){
    char word[TIMUI_SS_NAME_MAX + 1];
    if(!ss_ident_(p, word, sizeof word)) return 0;
    if(ss_streq_(word, "true") || ss_streq_(word, "on") || ss_streq_(word, "yes")){
        *out = 1; return 1;
    }
    if(ss_streq_(word, "false") || ss_streq_(word, "off") || ss_streq_(word, "no")){
        *out = 0; return 1;
    }
    return 0;
}
static int ss_int_(TimuiStyleParser *p, int *out){
    long v = 0;
    int neg = 0, any = 0;
    ss_skip_ws_(p);
    if(p->pos < p->len && p->s[p->pos] == '-'){ neg = 1; p->pos++; }
    while(p->pos < p->len && p->s[p->pos] >= '0' && p->s[p->pos] <= '9'){
        any = 1;
        v = v * 10 + (p->s[p->pos] - '0');
        if(v > 1000000L) return 0;
        p->pos++;
    }
    if(!any || neg) return 0;
    *out = (int)v;
    return 1;
}
static int ss_border_(TimuiStyleParser *p, uint32_t *out){
    char word[TIMUI_SS_NAME_MAX + 1];
    if(!ss_ident_(p, word, sizeof word)) return 0;
    if(ss_streq_(word, "none")) *out = TIMUI_BORDER_NONE;
    else if(ss_streq_(word, "single")) *out = TIMUI_BORDER_SINGLE;
    else if(ss_streq_(word, "double")) *out = TIMUI_BORDER_DOUBLE;
    else if(ss_streq_(word, "round") || ss_streq_(word, "rounded")) *out = TIMUI_BORDER_ROUND;
    else if(ss_streq_(word, "ascii")) *out = TIMUI_BORDER_ASCII;
    else if(ss_streq_(word, "shadow")) *out = TIMUI_BORDER_SHADOW;
    else return 0;
    return 1;
}
static int ss_selector_(TimuiStyleParser *p, TimuiStyleRule *r){
    char name[TIMUI_SS_NAME_MAX + 1];
    int have = 0;
    ss_skip_ws_(p);
    r->kind = TIMUI_WIDGET_ANY;
    if(p->pos < p->len && p->s[p->pos] == '*'){
        p->pos++;
        have = 1;
    } else if(p->pos < p->len && ss_is_alpha_(p->s[p->pos])){
        if(!ss_ident_(p, name, sizeof name)) return 0;
        if(!ss_widget_kind_(name, &r->kind)) return 0;
        r->specificity += 1;
        have = 1;
    }
    for(;;){
        uint32_t st;
        ss_skip_ws_(p);
        if(p->pos >= p->len) return 0;
        if(p->s[p->pos] == '#'){
            p->pos++;
            if(r->id[0] || !ss_ident_(p, r->id, sizeof r->id)) return 0;
            r->specificity += 100; have = 1;
        } else if(p->s[p->pos] == '.'){
            p->pos++;
            if(r->klass[0] || !ss_ident_(p, r->klass, sizeof r->klass)) return 0;
            r->specificity += 10; have = 1;
        } else if(p->s[p->pos] == ':'){
            p->pos++;
            if(!ss_ident_(p, name, sizeof name) || !ss_state_(name, &st)) return 0;
            r->states |= st;
            r->specificity += 10; have = 1;
        } else break;
    }
    return have;
}
static int ss_class_matches_(const char *classes, const char *klass){
    size_t klen, i = 0;
    if(!klass || !klass[0]) return 1;
    if(!classes) return 0;
    klen = strlen(klass);
    while(classes[i]){
        while(classes[i] && ss_is_space_(classes[i])) i++;
        if(!classes[i]) break;
        { size_t start = i;
          while(classes[i] && !ss_is_space_(classes[i])) i++;
          if(i - start == klen && memcmp(classes + start, klass, klen) == 0) return 1; }
    }
    return 0;
}
static int ss_rule_matches_(const TimuiStyleRule *r, TimuiStyleQuery q){
    if(r->kind != TIMUI_WIDGET_ANY && r->kind != q.kind) return 0;
    if(r->id[0] && (!q.id || strcmp(r->id, q.id) != 0)) return 0;
    if(!ss_class_matches_(q.classes, r->klass)) return 0;
    if((q.states & r->states) != r->states) return 0;
    return 1;
}
static TimuiResult ss_push_rule_(TimuiStylesheet *ss, const TimuiStyleRule *r){
    if(ss->count == ss->cap){
        int ncap = ss->cap ? ss->cap * 2 : 8;
        TimuiStyleRule *nr;
        if(ncap < ss->cap) return TIMUI_ERR_OUT_OF_MEMORY;
        if(ss->rules)
            nr = (TimuiStyleRule *)ss->alloc.realloc(ss->alloc.userdata, ss->rules,
                                                     (size_t)ss->cap * sizeof *ss->rules,
                                                     (size_t)ncap * sizeof *ss->rules);
        else
            nr = (TimuiStyleRule *)ss->alloc.alloc(ss->alloc.userdata,
                                                   (size_t)ncap * sizeof *ss->rules);
        if(!nr) return TIMUI_ERR_OUT_OF_MEMORY;
        ss->rules = nr;
        ss->cap = ncap;
    }
    ss->rules[ss->count++] = *r;
    return TIMUI_OK;
}
static int ss_decl_(TimuiStyleParser *p, TimuiStyleRule *r){
    char prop[TIMUI_SS_NAME_MAX + 1];
    if(!ss_ident_(p, prop, sizeof prop)) return 0;
    if(!ss_take_(p, ':')) return 0;
    if(ss_streq_(prop, "fg")){
        if(!ss_color_(p, &r->style.fg)) return 0;
        r->props |= TIMUI_STYLE_PROP_FG;
    } else if(ss_streq_(prop, "bg")){
        if(!ss_color_(p, &r->style.bg)) return 0;
        r->props |= TIMUI_STYLE_PROP_BG;
    } else if(ss_streq_(prop, "bold") || ss_streq_(prop, "dim") || ss_streq_(prop, "reverse")){
        uint32_t bit = ss_streq_(prop, "bold") ? TIMUI_ATTR_BOLD :
                       ss_streq_(prop, "dim") ? TIMUI_ATTR_DIM : TIMUI_ATTR_REVERSE;
        int on;
        if(!ss_bool_(p, &on)) return 0;
        r->attr_props |= bit;
        if(on) r->attr_values |= bit;
        else r->attr_values &= ~bit;
        r->props |= TIMUI_STYLE_PROP_ATTRS;
    } else if(ss_streq_(prop, "border")){
        if(!ss_border_(p, &r->border)) return 0;
        r->props |= TIMUI_STYLE_PROP_BORDER;
    } else if(ss_streq_(prop, "padding")){
        if(!ss_int_(p, &r->padding)) return 0;
        r->props |= TIMUI_STYLE_PROP_PADDING;
    } else if(ss_streq_(prop, "gap")){
        if(!ss_int_(p, &r->gap)) return 0;
        r->props |= TIMUI_STYLE_PROP_GAP;
    } else if(ss_streq_(prop, "gradient-lo")){
        if(!ss_color_(p, &r->gradient_lo)) return 0;
        r->props |= TIMUI_STYLE_PROP_GRADIENT_LO;
    } else if(ss_streq_(prop, "gradient-hi")){
        if(!ss_color_(p, &r->gradient_hi)) return 0;
        r->props |= TIMUI_STYLE_PROP_GRADIENT_HI;
    } else return 0;
    return ss_take_(p, ';');
}
TIMUI_API void timui_stylesheet_free(TimuiStylesheet *ss){
    if(!ss) return;
    if(ss->rules && ss_alloc_valid_(&ss->alloc))
        ss->alloc.free(ss->alloc.userdata, ss->rules, (size_t)ss->cap * sizeof *ss->rules);
    ss->rules = NULL;
    ss->count = 0;
    ss->cap = 0;
    memset(&ss->alloc, 0, sizeof ss->alloc);
}
TIMUI_API TimuiResult timui_stylesheet_parse(TimuiStylesheet *out, const char *src,
                                             size_t len, const TimuiAllocator *alloc){
    TimuiStyleParser p;
    TimuiResult gr;
    TimuiAllocator al;
    if(!out || (!src && len > 0) || !ss_alloc_valid_(alloc)) return TIMUI_ERR_INVALID_ARGUMENT;
    al = *alloc;
    memset(out, 0, sizeof *out);
    out->alloc = al;
    p.s = src ? src : "";
    p.len = len;
    p.pos = 0;
    while(1){
        TimuiStyleRule r;
        ss_skip_ws_(&p);
        if(p.pos >= p.len) return TIMUI_OK;
        memset(&r, 0, sizeof r);
        r.order = out->count;
        if(!ss_selector_(&p, &r) || !ss_take_(&p, '{')) goto protocol;
        while(!ss_at_(&p, '}')){
            if(p.pos >= p.len) goto protocol;
            if(!ss_decl_(&p, &r)) goto protocol;
        }
        p.pos++;
        gr = ss_push_rule_(out, &r);
        if(gr != TIMUI_OK){ timui_stylesheet_free(out); return gr; }
    }
protocol:
    timui_stylesheet_free(out);
    return TIMUI_ERR_PROTOCOL;
}
static void ss_apply_style_(TimuiResolvedStyle *res, uint32_t prop, int spec, int *best,
                            const TimuiStyleRule *r){
    if(spec < *best) return;
    *best = spec;
    res->mask |= prop;
    if(prop == TIMUI_STYLE_PROP_FG) res->style.fg = r->style.fg;
    else if(prop == TIMUI_STYLE_PROP_BG) res->style.bg = r->style.bg;
    else if(prop == TIMUI_STYLE_PROP_BORDER) res->border = r->border;
    else if(prop == TIMUI_STYLE_PROP_PADDING) res->padding = r->padding;
    else if(prop == TIMUI_STYLE_PROP_GAP) res->gap = r->gap;
    else if(prop == TIMUI_STYLE_PROP_GRADIENT_LO) res->gradient_lo = r->gradient_lo;
    else if(prop == TIMUI_STYLE_PROP_GRADIENT_HI) res->gradient_hi = r->gradient_hi;
}
TIMUI_API TimuiResolvedStyle timui_stylesheet_resolve(const TimuiStylesheet *ss,
                                                      TimuiStyleQuery query){
    enum { P_FG, P_BG, P_BOLD, P_DIM, P_REV, P_BORDER, P_PADDING, P_GAP, P_GLO, P_GHI, P_COUNT };
    int best[P_COUNT];
    TimuiResolvedStyle res;
    int i;
    res.style = query.base;
    res.mask = 0;
    res.border = TIMUI_BORDER_NONE;
    res.padding = 0;
    res.gap = 0;
    res.gradient_lo = 0;
    res.gradient_hi = 0;
    for(i = 0; i < P_COUNT; i++) best[i] = -1;
    if(!ss || !ss->rules) return res;
    for(i = 0; i < ss->count; i++){
        const TimuiStyleRule *r = &ss->rules[i];
        int spec = r->specificity;
        (void)r->order;
        if(!ss_rule_matches_(r, query)) continue;
        if(r->props & TIMUI_STYLE_PROP_FG) ss_apply_style_(&res, TIMUI_STYLE_PROP_FG, spec, &best[P_FG], r);
        if(r->props & TIMUI_STYLE_PROP_BG) ss_apply_style_(&res, TIMUI_STYLE_PROP_BG, spec, &best[P_BG], r);
        if((r->attr_props & TIMUI_ATTR_BOLD) && spec >= best[P_BOLD]){
            best[P_BOLD] = spec; res.mask |= TIMUI_STYLE_PROP_ATTRS;
            if(r->attr_values & TIMUI_ATTR_BOLD) res.style.attrs |= TIMUI_ATTR_BOLD;
            else res.style.attrs &= ~TIMUI_ATTR_BOLD;
        }
        if((r->attr_props & TIMUI_ATTR_DIM) && spec >= best[P_DIM]){
            best[P_DIM] = spec; res.mask |= TIMUI_STYLE_PROP_ATTRS;
            if(r->attr_values & TIMUI_ATTR_DIM) res.style.attrs |= TIMUI_ATTR_DIM;
            else res.style.attrs &= ~TIMUI_ATTR_DIM;
        }
        if((r->attr_props & TIMUI_ATTR_REVERSE) && spec >= best[P_REV]){
            best[P_REV] = spec; res.mask |= TIMUI_STYLE_PROP_ATTRS;
            if(r->attr_values & TIMUI_ATTR_REVERSE) res.style.attrs |= TIMUI_ATTR_REVERSE;
            else res.style.attrs &= ~TIMUI_ATTR_REVERSE;
        }
        if(r->props & TIMUI_STYLE_PROP_BORDER) ss_apply_style_(&res, TIMUI_STYLE_PROP_BORDER, spec, &best[P_BORDER], r);
        if(r->props & TIMUI_STYLE_PROP_PADDING) ss_apply_style_(&res, TIMUI_STYLE_PROP_PADDING, spec, &best[P_PADDING], r);
        if(r->props & TIMUI_STYLE_PROP_GAP) ss_apply_style_(&res, TIMUI_STYLE_PROP_GAP, spec, &best[P_GAP], r);
        if(r->props & TIMUI_STYLE_PROP_GRADIENT_LO) ss_apply_style_(&res, TIMUI_STYLE_PROP_GRADIENT_LO, spec, &best[P_GLO], r);
        if(r->props & TIMUI_STYLE_PROP_GRADIENT_HI) ss_apply_style_(&res, TIMUI_STYLE_PROP_GRADIENT_HI, spec, &best[P_GHI], r);
    }
    return res;
}

TIMUI_API void timui_set_stylesheet(Timui *ui, const TimuiStylesheet *ss){
    if(ui) ui->stylesheet = ss;
}

static TimuiStyle timui_widget_style_(Timui *ui, TimuiWidgetKind kind,
                                      TimuiStyleSlot slot, uint32_t states){
    TimuiStyle base;
    TimuiStyleQuery q;
    if(!ui) return timui_style_make(0, 0, 0);
    base = timui_theme_style(&ui->theme, slot);
    if(!ui->stylesheet) return base;
    q.kind = kind;
    q.id = NULL;
    q.classes = NULL;
    q.states = states;
    q.base = base;
    return timui_stylesheet_resolve(ui->stylesheet, q).style;
}

#undef TIMUI_SS_NAME_MAX
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
static void fake_close(TimuiTransport *t){
    if(t && t->ctx) timui_fake_destroy((TimuiFakeTransport *)t->ctx);
}

TIMUI_API TimuiResult timui_fake_init(TimuiFakeTransport *f, const TimuiAllocator *alloc){
    if(!f || !timui_allocator_valid_(alloc)) return TIMUI_ERR_INVALID_ARGUMENT;
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
    if(flags & TIMUI_FLAG_KITTY_KEYBOARD)  TIMUI_EMIT(t, "\x1b[>1u");
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
    if(flags & TIMUI_FLAG_KITTY_KEYBOARD)  TIMUI_EMIT(t, "\x1b[<u");
    TIMUI_EMIT(t, "\x1b[?25h");
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
    t->fd = fd;
    t->saved = NULL;
    t->have_saved = 0;
    orig = (struct termios *)malloc(sizeof(struct termios));
    if(!orig) return TIMUI_ERR_OUT_OF_MEMORY;
    if(tcgetattr(fd, orig) != 0){ free(orig); return TIMUI_ERR_OS; }
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
    return timui_term_size_pixels(fd, out_w, out_h, NULL, NULL);
}
TIMUI_API TimuiResult timui_term_size_pixels(int fd, int *out_w, int *out_h,
                                             int *out_px_w, int *out_px_h){
    struct winsize ws;
    if(ioctl(fd, TIOCGWINSZ, &ws) != 0){
        return (errno == ENOTTY) ? TIMUI_ERR_NOT_A_TTY : TIMUI_ERR_OS;
    }
    if(out_w) *out_w = (int)ws.ws_col;
    if(out_h) *out_h = (int)ws.ws_row;
    if(out_px_w) *out_px_w = (int)ws.ws_xpixel;
    if(out_px_h) *out_px_h = (int)ws.ws_ypixel;
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

#define TIMUI_IMAGE_CAP_MASK_ ((uint32_t)(TIMUI_CAP_KITTY_GRAPHICS | TIMUI_CAP_SIXEL_GRAPHICS | TIMUI_CAP_ITERM2_IMAGES))

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
    /* multiplexers reduce capabilities. Image protocols are ALWAYS stripped
     * under a multiplexer: they require explicit passthrough + graphics support
     * we can't assume, and dropped image escapes leave a grey placeholder region
     * plus stray cursor moves. Keyboard and sync are only kept when the OUTER
     * terminal (TERM_PROGRAM, inherited into the session) is kitty-family;
     * otherwise stripped. timui_force_cap overrides either way (W12). */
    if(term && (!strncmp(term, "tmux", 4) || !strncmp(term, "screen", 6) || !strncmp(term, "zellij", 6))){
        c->flags &= ~TIMUI_IMAGE_CAP_MASK_;
        if(!caps_is_kitty_family(term_program))
            c->flags &= ~(TIMUI_CAP_KITTY_KEYBOARD | TIMUI_CAP_SYNC_OUTPUT);
        c->flags |= TIMUI_CAP_256_COLOR;
        if(c->colors < 256) c->colors = 256;
    }
#ifdef TIMUI_NO_IMAGES
    c->flags &= ~TIMUI_IMAGE_CAP_MASK_;
#endif
}
TIMUI_API void timui_caps_apply_force(TimuiCaps *c, uint32_t force_on, uint32_t force_off){
    if(!c) return;
    c->flags |= force_on;
    c->flags &= ~force_off;
#ifdef TIMUI_NO_IMAGES
    c->flags &= ~TIMUI_IMAGE_CAP_MASK_;
#endif
}
TIMUI_API int timui_caps_has(const TimuiCaps *c, TimuiCapFlags cap){
    return c && ((c->flags & (uint32_t)cap) != 0);
}
TIMUI_API void timui_force_cap(Timui *ui, TimuiCapFlags cap, int enable){
    uint32_t bits = (uint32_t)cap;
    if(!ui) return;
#ifdef TIMUI_NO_IMAGES
    bits &= ~TIMUI_IMAGE_CAP_MASK_;
    ui->caps.flags &= ~TIMUI_IMAGE_CAP_MASK_;
    if(bits == 0) return;
#endif
    if(enable) ui->caps.flags |= bits;
    else       ui->caps.flags &= ~bits;
}
TIMUI_API TimuiImageProtocol timui_caps_image_protocol(const TimuiCaps *c){
    if(!c) return TIMUI_IMAGE_PROTOCOL_NONE;
#ifdef TIMUI_NO_IMAGES
    (void)c;
    return TIMUI_IMAGE_PROTOCOL_NONE;
#else
    if(c->flags & TIMUI_CAP_KITTY_GRAPHICS) return TIMUI_IMAGE_PROTOCOL_KITTY;
    if(c->flags & TIMUI_CAP_SIXEL_GRAPHICS) return TIMUI_IMAGE_PROTOCOL_SIXEL;
    if(c->flags & TIMUI_CAP_ITERM2_IMAGES) return TIMUI_IMAGE_PROTOCOL_ITERM2;
    return TIMUI_IMAGE_PROTOCOL_NONE;
#endif
}
#undef TIMUI_IMAGE_CAP_MASK_

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
        ev.as.mouse.pressed = 0;
        ev.as.mouse.released = 0;
    } else {
        int btn = code & 0x03;
        ev.as.mouse.motion = (code & 0x20) ? 1 : 0;
        ev.as.mouse.button = (btn == 3) ? -1 : btn;
        if(ev.as.mouse.motion || btn == 3){
            ev.as.mouse.pressed = 0;
            ev.as.mouse.released = 0;
        }
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
    if(b >= 0xC2 && (b & 0xE0) == 0xC0){ *cp = (uint32_t)(b & 0x1F); return 1; }
    if((b & 0xF0) == 0xE0){ *cp = (uint32_t)(b & 0x0F); return 2; }
    if(b <= 0xF4 && (b & 0xF8) == 0xF0){ *cp = (uint32_t)(b & 0x07); return 3; }
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
static int timui_input_flush_esc_(TimuiInputParser *p, uint64_t now_ms, TimuiEventFn cb, void *ctx){
    int emitted = 0;
    if(!p) return 0;
    if((p->state == 1 || p->state == 2 || p->state == 3) &&
       now_ms - p->esc_since_ms >= TIMUI_ESC_TIMEOUT_MS){
        if(p->state == 1){
            emit_key(cb, ctx, TIMUI_KEY_ESCAPE, 0, 0);
            emitted = 1;
        }
        p->state = 0;
        p->param = 0; p->nparams = 0;
        p->mod_param = 0; p->has_mod = 0; p->sub_param = 0;
        p->csi_mouse = 0; p->mcount = 0;
        p->mparam[0] = p->mparam[1] = p->mparam[2] = 0;
        p->esc_since_ms = 0;
    }
    return emitted;
}
TIMUI_API void timui_input_flush_esc(TimuiInputParser *p, uint64_t now_ms, TimuiEventFn cb, void *ctx){
    (void)timui_input_flush_esc_(p, now_ms, cb, ctx);
}
TIMUI_API size_t timui_input_feed(TimuiInputParser *p, const void *data, size_t len,
                                  TimuiEventFn cb, void *ctx){
    const unsigned char *b = (const unsigned char *)data;
    size_t i, count = 0;
    if(!p || !b) return 0;
    count += (size_t)timui_input_flush_esc_(p, p->now_ms, cb, ctx);
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
                if(need < 0){
                    size_t bad_len = 1;
                    if(c >= 0xC0 && c <= 0xC1 && i + 1 < len && (b[i + 1] & 0xC0) == 0x80){
                        bad_len = 2;
                    } else if(c >= 0xF5 && c <= 0xF7){
                        size_t j;
                        for(j = 1; j < 4 && i + j < len && (b[i + j] & 0xC0) == 0x80; j++){}
                        bad_len = j;
                    }
                    emit_text(cb, ctx, (const char *)&b[i], bad_len, 0xFFFD);
                    count++;
                    i += bad_len - 1;
                    break;
                }
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
        int in_m = timui_rect_contains_(ia->modal_rect, ia->mouse_x, ia->mouse_y);
        if(!in_m) return res;
    }
    hover = timui_rect_contains_(r, ia->mouse_x, ia->mouse_y);
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
static void widget_draw_text_clipped(TimuiFrame *f, TimuiRect clip, int x, int y,
                                     TimuiStr text, TimuiStyle style){
    Timui *ui;
    if(!f || !f->ui) return;
    ui = f->ui;
    timui_push_clip(f, clip);
    timui_draw_text(&ui->curr, x, y, text, style);
    timui_pop_clip(f);
}

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
    st = timui_widget_style_(ui, TIMUI_WIDGET_BUTTON, slot,
                             (ir.active ? TIMUI_STYLE_STATE_ACTIVE : 0) |
                             (ir.hovered ? TIMUI_STYLE_STATE_HOVERED : 0) |
                             (ir.focused ? TIMUI_STYLE_STATE_FOCUSED : 0));
    timui_draw_fill(&ui->curr, r, st);
    widget_draw_text_clipped(f, r, r.x + 1, r.y + (r.h > 1 ? (r.h - 1) / 2 : 0), label, st);
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
    timui_draw_box(&ui->curr, r, border_flags,
                   timui_widget_style_(ui, TIMUI_WIDGET_PANEL, TIMUI_SLOT_BORDER, 0));
    timui_push_clip(f, r);   /* W10: clip title + body content to the panel rect */
    if(title.ptr && title.len)
        timui_draw_text(&ui->curr, r.x + 1, r.y, title,
                        timui_widget_style_(ui, TIMUI_WIDGET_PANEL, TIMUI_SLOT_PANEL_TITLE, 0));
    body.x = r.x + 1; body.y = r.y + 1;
    body.w = r.w - 2; body.h = r.h - 2;
    if(body.w < 0) body.w = 0;
    if(body.h < 0) body.h = 0;
    timui_draw_fill(&ui->curr, body,
                    timui_widget_style_(ui, TIMUI_WIDGET_PANEL, TIMUI_SLOT_PANEL, 0));
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
    st = timui_widget_style_(ui, TIMUI_WIDGET_INPUT,
                             ir.focused ? TIMUI_SLOT_INPUT_FOCUSED : TIMUI_SLOT_INPUT,
                             ir.focused ? TIMUI_STYLE_STATE_FOCUSED : 0);
    box[0] = is_radio ? '(' : '[';
    box[1] = value ? (is_radio ? 'o' : 'x') : ' ';
    box[2] = is_radio ? ')' : ']';
    box[3] = ' ';
    timui_push_clip(f, r);
    timui_draw_text(&ui->curr, r.x, r.y, (TimuiStr){ box, 4 }, st);
    timui_draw_text(&ui->curr, r.x + 4, r.y, label,
                    timui_widget_style_(ui, TIMUI_WIDGET_INPUT, TIMUI_SLOT_TEXT, 0));
    timui_pop_clip(f);
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
    TimuiStyle st;
    if(!f || !f->ui) return;
    ui = f->ui;
    st = timui_widget_style_(ui, TIMUI_WIDGET_PANEL, TIMUI_SLOT_STATUS, 0);
    timui_draw_row_(&ui->curr, r, 0, text, st);
}
/* ---- UTF-8 / grapheme edit helpers (shared with timui_text_area) -------- *
 * text_in carries UTF-8 (since the G8 fix), so text inputs must append and
 * delete whole clusters — a byte-wise append can split a multibyte char at the
 * cap boundary, and a codepoint-wise backspace can leave a dangling skin-tone
 * modifier, variation selector, or joiner sequence. */

/* byte length of a well-formed UTF-8 sequence starting at lead byte b (1..4),
 * or 0 if b is not a lead. */
static int utf8_lead_len(unsigned char b){
    if(b < 0x80) return 1;
    if((b & 0xE0) == 0xC0) return 2;
    if((b & 0xF0) == 0xE0) return 3;
    if((b & 0xF8) == 0xF0) return 4;
    return 0;
}
static int single_line_text_byte_(unsigned char b){
    return b >= 0x20 && b != 0x7f;
}
/* New length after removing one complete grapheme cluster from the end of
 * buf[0..len). The state structs keep byte cursors, so callers still pass and
 * receive byte offsets. */
static size_t utf8_drop_last(const char *buf, size_t len){
    return timui_grapheme_prev(buf, len, len);
}
/* ---- in-line editing primitives (F1.2) --------------------------------- *
 * All operate on a NUL-terminated buffer; utf8_drop_last(buf, cursor) already
 * gives the previous grapheme boundary (Left / Backspace). */

/* Byte offset after the grapheme at `cursor`, clamped to len (Right / Delete). */
static size_t utf8_next_(const char *buf, size_t cursor, size_t len){
    if(cursor >= len) return len;
    return timui_grapheme_next(buf, len, cursor);
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
 * whole codepoints so nothing is split at the UTF-8 byte boundary. */
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
static size_t text_len_bounded_(const char *buf, size_t cap){
    size_t len = 0;
    while(len < cap && buf[len]) len++;
    return len;
}

TIMUI_API bool timui_input_line_buf(TimuiFrame *f, TimuiId id, TimuiRect r, char *buf, size_t cap){
    Timui *ui;
    TimuiInteractResult ir;
    TimuiStyle st;
    size_t len;
    bool submitted = false;
    if(!f || !f->ui || !buf || cap == 0) return false;
    ui = f->ui;
    len = text_len_bounded_(buf, cap);
    if(len >= cap){
        len = cap - 1;
        buf[len] = '\0';
    }
    {
        int submit = ui->ia.activate_pressed;   /* capture before interact_button consumes it */
        ir = timui_interact_button(&ui->ia, id, r);   /* click to focus */
        if(ir.focused){
            int i = 0;
            /* append whole UTF-8 codepoints; skip one that won't fit intact */
            while(i < ui->text_in_len){
                size_t m = (size_t)utf8_lead_len((unsigned char)ui->text_in[i]);
                if(!single_line_text_byte_((unsigned char)ui->text_in[i])){ i++; continue; }
                if(m == 0) m = 1;                       /* defensive: stray byte */
                if(len + m >= cap) break;               /* no room for the codepoint + NUL */
                while(m-- > 0 && i < ui->text_in_len) buf[len++] = ui->text_in[i++];
            }
            buf[len] = '\0';
            if((ui->key_in & TIMUI_KEYIN_BACKSPACE) && len > 0){
                len = utf8_drop_last(buf, len);         /* delete a whole cluster */
                buf[len] = '\0';
            }
            if(submit) submitted = true;
            ui->text_in_len = 0;       /* consumed by the focused input */
            ui->key_in = 0;
        }
    }
    st = timui_widget_style_(ui, TIMUI_WIDGET_INPUT,
                             ir.focused ? TIMUI_SLOT_INPUT_FOCUSED : TIMUI_SLOT_INPUT,
                             ir.focused ? TIMUI_STYLE_STATE_FOCUSED : 0);
    timui_draw_fill(&ui->curr, r, st);
    widget_draw_text_clipped(f, r, r.x, r.y, (TimuiStr){ buf, len }, st);
    return submitted;
}
/* Display column of the cursor: sum of grapheme widths over buf[0..upto) (F1.5). */
static int display_col_(const char *buf, size_t upto){
    size_t i = 0, len = strlen(buf);
    int col = 0;
    if(upto > len) upto = len;
    while(i < upto){
        size_t n = timui_grapheme_next(buf, len, i);
        if(n <= i) n = i + 1;
        if(n > upto) n = upto;
        col += timui_grapheme_width(buf + i, n - i);
        i = n;
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
    { size_t text_len = text_len_bounded_(st->text, st->cap);
      if(text_len >= st->cap){ text_len = st->cap - 1; st->text[text_len] = '\0'; }
      if(st->cursor > text_len) st->cursor = text_len; }   /* Y1-style: distrust caller cursor */
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
                if(!single_line_text_byte_((unsigned char)ui->text_in[j])){ j++; continue; }
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
                st->text[st->cursor] = '\0';                /* cursor is a cluster boundary */
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
                for(k = 0; k < ui->pending_enter_count; k++){
                    ui->pending_enter_at[k] = ui->enter_at[k + 1] - upto;
                    ui->pending_enter_mods[k] = ui->enter_mods[k + 1];
                }
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
                : timui_widget_style_(ui, TIMUI_WIDGET_INPUT,
                                      ir.focused ? TIMUI_SLOT_INPUT_FOCUSED : TIMUI_SLOT_INPUT,
                                      ir.focused ? TIMUI_STYLE_STATE_FOCUSED : 0);
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
        st = timui_widget_style_(ui, TIMUI_WIDGET_LISTBOX, slot,
                                 idx == state.selected ? TIMUI_STYLE_STATE_SELECTED : 0);
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
    timui_label(f, bx + 2, by + 1, message,
                timui_widget_style_(ui, TIMUI_WIDGET_PANEL, TIMUI_SLOT_TEXT, 0));
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
/* timui_tabs.c — tab-bar widget (W2).
 *
 * A single-row bar of labeled tabs with a boxed/highlighted active tab (the
 * radio.c look), Left/Right + mouse-click selection, and horizontal overflow
 * scrolling that keeps the selected tab in view. The geometry is split into
 * three pure, I/O-free helpers so the math is unit-testable in isolation and an
 * app can reuse it for its own hit-testing:
 *
 *   timui_tabs_layout   — each tab's [x, x+w) span from labels + widths + sep
 *   timui_tabs_scroll   — a scroll offset that keeps `selected` visible
 *   timui_tab_visible   — does a span overlap the viewport
 *
 * This file is a SECTION of the unity build: it is textually #included from
 * include/timui.h under TIMUI_IMPLEMENTATION, so it carries no includes/guards
 * and may call any already-declared public primitive (timui_utf8_*, the theme,
 * the draw primitives, the interaction state) plus the internal Timui fields.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd. */

/* Cap on tabs a single bar lays out/draws in one frame (spans live on the
 * stack — a bar with more than this is pathological; extra tabs are ignored). */
#define TIMUI_TABS_MAX 64

/* Display width (terminal columns) of a NUL-terminated UTF-8 label, summing
 * per-codepoint widths so CJK/emoji count as 2 and combining marks as 0. */
static int timui_tab_text_width_(const char *s){
    int w = 0;
    size_t i = 0, len;
    if(!s) return 0;
    len = strlen(s);
    while(i < len){
        uint32_t cp = 0;
        int adv = timui_utf8_decode(s + i, len - i, &cp);
        if(adv <= 0) adv = 1;                 /* never stall on a bad byte */
        w += timui_utf8_width(cp);
        i += (size_t)adv;
    }
    return w;
}

/* Pure layout: place n tabs left-to-right. Each tab is drawn as " LABEL " — one
 * pad column on each side of its display width — and consecutive tabs are
 * separated by `sep` columns. Writes up to `max` spans into `out` (out may be
 * NULL to only measure) and returns the total content width. */
TIMUI_API int timui_tabs_layout(const char *const *labels, int n, int sep,
                                TimuiTabSpan *out, int max){
    int i, x = 0;
    if(n < 0) n = 0;
    if(sep < 0) sep = 0;
    for(i = 0; i < n; i++){
        int lw = (labels && labels[i]) ? timui_tab_text_width_(labels[i]) : 0;
        int tw = lw + 2;                      /* the ' LABEL ' box (pad each side) */
        if(out && i < max){ out[i].x = x; out[i].w = tw; }
        x += tw;
        if(i + 1 < n) x += sep;               /* sep goes BETWEEN tabs, not after */
    }
    return x;
}

/* Pure scroll: pick a column offset that keeps tab `selected` visible in a
 * `width`-column viewport, moving as little as possible from `cur_scroll`.
 * Reveals the selected tab's left edge if it is off to the left, or its right
 * edge if off to the right; a tab wider than the viewport pins its left edge so
 * the label start stays readable. Result is clamped to [0, max(0,total-width)]. */
TIMUI_API int timui_tabs_scroll(const TimuiTabSpan *spans, int n, int selected,
                                int width, int cur_scroll){
    int scroll, total, maxscroll, end;
    if(!spans || n <= 0) return 0;
    if(selected < 0) selected = 0;
    if(selected >= n) selected = n - 1;
    total = spans[n - 1].x + spans[n - 1].w;          /* end of the last tab */
    scroll = cur_scroll < 0 ? 0 : cur_scroll;
    end = spans[selected].x + spans[selected].w;
    if(spans[selected].x < scroll)                    /* left of the view */
        scroll = spans[selected].x;
    else if(width > 0 && end > scroll + width)         /* right of the view */
        scroll = end - width;
    if(width > 0 && spans[selected].w > width)          /* oversize: show label start */
        scroll = spans[selected].x;
    maxscroll = total - width;
    if(maxscroll < 0) maxscroll = 0;
    if(scroll > maxscroll) scroll = maxscroll;
    if(scroll < 0) scroll = 0;
    return scroll;
}

/* Pure visibility: does `span` overlap the viewport [scroll, scroll+width)? */
TIMUI_API int timui_tab_visible(TimuiTabSpan span, int scroll, int width){
    return (span.x < scroll + width && span.x + span.w > scroll) ? 1 : 0;
}

/* The interactive widget. Draws the bar, highlights *selected, applies Left/
 * Right (when focused) and mouse clicks, overflow-scrolls to keep the selection
 * visible, writes the resulting index back through *selected, and returns it. */
TIMUI_API int timui_tabs(TimuiFrame *f, TimuiId id, TimuiRect r,
                         const char *const *labels, int n, int *selected){
    Timui *ui;
    TimuiInteractResult ir;
    TimuiTabSpan spans[TIMUI_TABS_MAX];
    TimuiStyle bar_st, sel_st, txt_st;
    int i, sel, y, scroll;

    if(!f || !f->ui) return selected ? *selected : 0;
    ui = f->ui;
    if(!selected) return 0;
    sel = *selected;
    bar_st = timui_widget_style_(ui, TIMUI_WIDGET_MENU, TIMUI_SLOT_PANEL, 0);

    /* No tabs: clear the bar, normalize the selection, and bail out. */
    if(n <= 0){
        timui_draw_fill(&ui->curr, r, bar_st);
        *selected = 0;
        return 0;
    }
    if(n > TIMUI_TABS_MAX) n = TIMUI_TABS_MAX;
    if(sel < 0) sel = 0;
    if(sel >= n) sel = n - 1;                 /* self-heal a stale selection (Y3) */

    /* Register the whole bar so it joins the focus + Tab cycle and we can see
     * click / keyboard activation, exactly as the other widgets do. */
    ir = timui_interact_button(&ui->ia, id, r);

    /* Keyboard: Left/Right step the selection when the bar is focused (mirrors
     * the listbox's Up/Down over the accumulated key bitmask). */
    if(ir.focused){
        if((ui->key_in & TIMUI_KEYIN_LEFT)  && sel > 0)     sel--;
        if((ui->key_in & TIMUI_KEYIN_RIGHT) && sel < n - 1) sel++;
    }

    /* Lay the tabs out, then choose a scroll that keeps `sel` visible. cur=0 is
     * the canonical offset: tab 0 sits flush-left and later tabs only scroll in
     * once they would otherwise overflow the bar. */
    timui_tabs_layout(labels, n, 1, spans, n);
    scroll = timui_tabs_scroll(spans, n, sel, r.w, 0);

    /* Mouse: a click landing on a visible tab selects it. Hit-test in bar-local,
     * scroll-adjusted columns and require the pointer inside the bar (so a
     * keyboard activation with the mouse elsewhere can't grab a tab). */
    if(ir.clicked && timui_rect_contains_(r, ui->ia.mouse_x, ui->ia.mouse_y)){
        int lx = ui->ia.mouse_x - r.x + scroll;       /* column in the unscrolled bar */
        for(i = 0; i < n; i++){
            if(lx >= spans[i].x && lx < spans[i].x + spans[i].w){ sel = i; break; }
        }
        scroll = timui_tabs_scroll(spans, n, sel, r.w, scroll);   /* keep the pick in view */
    }

    /* ---- draw: boxed/highlighted active tab, dim inactive labels ---- */
    y = r.y;
    sel_st = timui_widget_style_(ui, TIMUI_WIDGET_MENU, TIMUI_SLOT_SELECTION,
                                 TIMUI_STYLE_STATE_SELECTED);
    txt_st = timui_widget_style_(ui, TIMUI_WIDGET_MENU, TIMUI_SLOT_TEXT, 0);
    txt_st.bg = bar_st.bg;                    /* inactive labels sit on the bar bg */
    timui_draw_fill(&ui->curr, r, bar_st);    /* clear the bar */
    timui_push_clip(f, r);                    /* clip any overflow to the bar */
    for(i = 0; i < n; i++){
        int sx;
        TimuiStr lab;
        if(!timui_tab_visible(spans[i], scroll, r.w)) continue;
        sx = r.x + spans[i].x - scroll;       /* screen column of this tab's box */
        lab = timui_str_from_cstr((labels && labels[i]) ? labels[i] : "");
        if(i == sel){
            TimuiStyle hi = sel_st;
            hi.attrs |= TIMUI_ATTR_BOLD;
            timui_draw_fill(&ui->curr, TIMUI_RECT(sx, y, spans[i].w, 1), hi);  /* highlight box */
            timui_draw_text(&ui->curr, sx + 1, y, lab, hi);                    /* padded label */
        } else {
            timui_draw_text(&ui->curr, sx + 1, y, lab, txt_st);
        }
    }
    timui_pop_clip(f);

    *selected = sel;
    return sel;
}
/* ---- clip stack ------------------------------------------------------- *
 * push_clip intersects the active clip with rect (so nested panels shrink it);
 * pop_clip restores the previous. Drawing (put_glyph) skips cells outside the
 * active clip. Reset each frame in timui_begin. */
static TimuiRect clip_intersect(TimuiRect a, TimuiRect b){
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
/* ---- table widget (v0.2 + virtual grid) ------------------------------- *
 * Two families:
 *   - timui_table / timui_table_mut : the original fixed-width grid (unchanged;
 *     backward-compatible for examples/file_manager.c + procmon.c).
 *   - timui_table_ex / timui_table_ex_mut : a VIRTUAL multi-column grid over a
 *     TimuiTableModel (row count + cell accessor, so large sets aren't
 *     materialized) with a STICKY header, per-column content-fit widths, and
 *     both vertical + horizontal scroll.
 *
 * The layout kernel (display width, column fit, cell truncation, paging/scroll)
 * is pure + side-effect-free and unit-tested in tests/test_grid.c — lifted from
 * examples/sqlite_table.h so the widget and standalone callers share one path.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd. */

/* ----------------------------------------------------------------------- */
/* Pure data-grid math (declared TIMUI_API in include/timui.h).              */
/* ----------------------------------------------------------------------- */

/* Display width of the first `len` bytes of a UTF-8 buffer (internal: also
 * serves TimuiStr headers, which need not be NUL-terminated). */
static int timui_disp_width_n_(const char *s, size_t len){
    size_t i;
    int w = 0;
    if(!s) return 0;
    for(i = 0; i < len;){
        size_t n = timui_grapheme_next(s, len, i);
        if(n <= i) n = i + 1;                  /* never stall on malformed input */
        w += timui_grapheme_width(s + i, n - i);
        i = n;
    }
    return w;
}

TIMUI_API int timui_display_width(const char *s){
    return s ? timui_disp_width_n_(s, strlen(s)) : 0;
}

TIMUI_API int timui_col_fit_width(const int *cellw, int n, int maxw, int minw){
    int i, m = 0, w;
    if(cellw)
        for(i = 0; i < n; i++)
            if(cellw[i] > m) m = cellw[i];
    w = m;
    if(w > maxw) w = maxw;                      /* cap  */
    if(w < minw) w = minw;                      /* floor (also handles maxw<minw) */
    return w;
}

/* U+2026 HORIZONTAL ELLIPSIS — one display column (3 UTF-8 bytes). */
#define TIMUI_ELLIPSIS_ "\xE2\x80\xA6"

TIMUI_API int timui_fit_cell(const char *s, int width, char *out, size_t cap, int *ellipsis){
    size_t i, len, o = 0;
    int used = 0, full, budget;
    if(ellipsis) *ellipsis = 0;
    if(!out || cap == 0) return 0;
    out[0] = '\0';
    if(!s) s = "";
    len = strlen(s);
    full = timui_disp_width_n_(s, len);
    if(width <= 0){                            /* no room at all */
        if(ellipsis) *ellipsis = (full > 0);
        return 0;
    }
    if(full <= width){                         /* fits whole — copy verbatim */
        size_t n = len < cap - 1 ? len : cap - 1;
        memcpy(out, s, n);
        out[n] = '\0';
        return full;
    }
    if(cap < sizeof(TIMUI_ELLIPSIS_)){         /* no room for ellipsis + NUL */
        if(ellipsis) *ellipsis = 1;
        return 1;
    }
    /* Truncate: reserve the last column for the ellipsis; never split a wide
     * glyph (when one straddles the budget the ellipsis lands early and the
     * caller pads the slack). */
    budget = width - 1;
    for(i = 0; i < len;){
        size_t n = timui_grapheme_next(s, len, i);
        int gw;
        if(n <= i) n = i + 1;
        gw = timui_grapheme_width(s + i, n - i);
        if(used + gw > budget) break;
        if(o + (n - i) + sizeof(TIMUI_ELLIPSIS_) > cap) break;  /* keep room for "…" + NUL */
        memcpy(out + o, s + i, n - i);
        o += n - i;
        used += gw;
        i = n;
    }
    if(o + 3 < cap){ memcpy(out + o, TIMUI_ELLIPSIS_, 3); o += 3; }
    out[o] = '\0';
    if(ellipsis) *ellipsis = 1;
    return used + 1;                           /* content columns + ellipsis */
}

TIMUI_API TimuiSlice timui_page_slice(int total, int viewport, int offset){
    TimuiSlice s;
    int maxoff;
    s.first = 0; s.count = 0;
    if(total <= 0) return s;
    maxoff = total - viewport;
    if(maxoff < 0) maxoff = 0;
    if(offset < 0) offset = 0;
    if(offset > maxoff) offset = maxoff;
    s.first = offset;
    if(viewport <= 0){ s.count = 0; return s; }
    s.count = total - offset;
    if(s.count > viewport) s.count = viewport;
    return s;
}

TIMUI_API int timui_scroll_to(int sel, int offset, int viewport, int total){
    (void)total;
    if(viewport <= 0) return offset;
    if(sel < offset) offset = sel;
    else if(sel >= offset + viewport) offset = sel - viewport + 1;
    if(offset < 0) offset = 0;
    return offset;
}

/* ----------------------------------------------------------------------- */
/* timui_table / timui_table_mut — original fixed-width grid (UNCHANGED).    */
/* Controlled: takes TimuiTableState by value and returns the new state in    */
/* the result; timui_table_mut is the write-back convenience twin.           */
/* ----------------------------------------------------------------------- */
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
    { TimuiStyle hs = timui_widget_style_(ui, TIMUI_WIDGET_TABLE, TIMUI_SLOT_PANEL_TITLE, 0);
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
        TimuiStyle st = timui_widget_style_(ui, TIMUI_WIDGET_TABLE,
            row == state.selected ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT,
            row == state.selected ? TIMUI_STYLE_STATE_SELECTED : 0);
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
    TimuiTableState in = state ? *state : (TimuiTableState){0, 0, 0};
    TimuiTableResult res = timui_table(f, id, r, headers, ncols, nrows, cell_fn, ud, in);
    if(state && res.state_changed) *state = res.state;   /* write back only on a real change */
    return res;
}

/* ----------------------------------------------------------------------- */
/* timui_table_ex — virtual multi-column grid (sticky header, h+v scroll).   */
/* ----------------------------------------------------------------------- */
#define TIMUI_GRID_MAX_COLS 64      /* columns fitted per frame (stack budget) */
#define TIMUI_GRID_COL_MIN  3       /* default per-column width floor          */
#define TIMUI_GRID_COL_MAX  24      /* default per-column width cap            */
#define TIMUI_GRID_SAMPLE   128     /* default rows sampled for width fitting  */
#define TIMUI_GRID_COL_GAP  1       /* blank cells between columns             */
#define TIMUI_GRID_HSTEP    4       /* cells scrolled per Left/Right key       */
#define TIMUI_GRID_CELLBUF  256     /* per-cell fit scratch                    */

/* Fit each of the first `ncols` (<= TIMUI_GRID_MAX_COLS) columns to its content:
 * the widest of the header + a bounded row sample, capped/floored. Returns the
 * number of columns actually fitted (clamped to the cap). */
static int timui_grid_fit_cols_(const TimuiTableModel *m, int *colw){
    int c, r, ncols, srows, maxw, minw, sample;
    ncols  = m->ncols < TIMUI_GRID_MAX_COLS ? m->ncols : TIMUI_GRID_MAX_COLS;
    minw   = m->col_min > 0 ? m->col_min : TIMUI_GRID_COL_MIN;
    maxw   = m->col_max > 0 ? m->col_max : TIMUI_GRID_COL_MAX;
    sample = m->sample  > 0 ? m->sample  : TIMUI_GRID_SAMPLE;
    srows  = m->nrows < sample ? m->nrows : sample;
    for(c = 0; c < ncols; c++){
        int widest = 0;
        if(m->headers && m->headers[c].ptr){
            int hw = timui_disp_width_n_(m->headers[c].ptr, m->headers[c].len);
            if(hw > widest) widest = hw;
        }
        for(r = 0; r < srows; r++){
            const char *txt = m->cell_fn ? m->cell_fn(m->ud, r, c) : "";
            int cw = timui_display_width(txt);
            if(cw > widest) widest = cw;
        }
        colw[c] = timui_col_fit_width(&widest, 1, maxw, minw);
    }
    return ncols;
}

/* Fit a (possibly non-NUL-terminated) TimuiStr into `width` columns -> out. */
static void timui_grid_fit_str_(TimuiStr s, int width, char *out, size_t cap){
    char tmp[TIMUI_GRID_CELLBUF];
    size_t n = s.ptr ? (s.len < sizeof(tmp) - 1 ? s.len : sizeof(tmp) - 1) : 0;
    int ell;
    if(n) memcpy(tmp, s.ptr, n);
    tmp[n] = '\0';
    timui_fit_cell(tmp, width, out, cap, &ell);
}

TIMUI_API TimuiTableResult timui_table_ex(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTableModel *model, TimuiTableState state){
    TimuiTableResult res;
    Timui *ui;
    TimuiInteractResult ir;
    int colw[TIMUI_GRID_MAX_COLS];
    int ncols, nrows, vis, total_w, sel, scroll, hscroll, orig_sel, c, cx, y, wh;
    res.state = state; res.state_changed = 0; res.focused = 0;
    if(!f || !f->ui || !model || model->ncols <= 0) return res;
    ui = f->ui;
    nrows = model->nrows;
    ncols = timui_grid_fit_cols_(model, colw);

    /* Total grid width (cells): sum of fitted widths + gaps between columns. */
    total_w = 0;
    for(c = 0; c < ncols; c++) total_w += colw[c] + (c ? TIMUI_GRID_COL_GAP : 0);

    /* Vertical layout: one sticky header row, the rest is the scroll body. */
    vis = r.h - 1;
    if(vis < 1) vis = 1;

    /* Selection clamp (self-heals a stale selected, like listbox/tree). */
    sel = state.selected;
    if(nrows <= 0) sel = 0;
    else { if(sel < 0) sel = 0; if(sel >= nrows) sel = nrows - 1; }
    orig_sel = sel;

    ir = timui_interact_button(&ui->ia, id, r);
    res.focused = ir.focused;

    scroll  = state.scroll  < 0 ? 0 : state.scroll;
    hscroll = state.hscroll < 0 ? 0 : state.hscroll;

    if(ir.focused){
        /* Up/Down move the selection; Left/Right pan horizontally. */
        sel = timui_updown_nav_(f, sel, nrows);
        if(timui_key_pressed(f, TIMUI_KEY_LEFT))  hscroll -= TIMUI_GRID_HSTEP;
        if(timui_key_pressed(f, TIMUI_KEY_RIGHT)) hscroll += TIMUI_GRID_HSTEP;
        if(timui_key_pressed(f, TIMUI_KEY_HOME))  hscroll = 0;
    }

    /* Mouse wheel scrolls the body and drags the selection into the new window. */
    wh = timui_mouse_wheel(f);
    if(wh){
        scroll -= wh;
        scroll = timui_page_slice(nrows, vis, scroll).first;
        if(sel < scroll) sel = scroll;
        if(sel >= scroll + vis) sel = scroll + vis - 1;
        if(nrows > 0){ if(sel >= nrows) sel = nrows - 1; } else sel = 0;
        if(sel < 0) sel = 0;
    }

    /* Click selects a body row (ignoring clicks on the sticky header). */
    if(ir.clicked){
        int my = ui->ia.mouse_y - (r.y + 1);
        int idx = scroll + my;
        if(my >= 0 && idx >= 0 && idx < nrows) sel = idx;
    }

    /* Keep the selection visible, then clamp both axes to a valid window. */
    scroll  = timui_scroll_to(sel, scroll, vis, nrows);
    scroll  = timui_page_slice(nrows, vis, scroll).first;
    hscroll = timui_page_slice(total_w, r.w, hscroll).first;

    /* --- draw (clipped to r so h-scroll overflow and header/body stay inside). */
    if(r.h >= 1 && r.w >= 1){
        timui_push_clip(f, r);
        /* sticky header: h-scrolled only, never v-scrolled. */
        { TimuiStyle hs = timui_widget_style_(ui, TIMUI_WIDGET_TABLE, TIMUI_SLOT_PANEL_TITLE, 0);
          timui_draw_fill(&ui->curr, TIMUI_RECT(r.x, r.y, r.w, 1), hs);
          cx = r.x - hscroll;
          for(c = 0; c < ncols; c++){
              char buf[TIMUI_GRID_CELLBUF];
              TimuiStr h = model->headers ? model->headers[c] : (TimuiStr){ NULL, 0 };
              timui_grid_fit_str_(h, colw[c], buf, sizeof buf);
              timui_draw_text(&ui->curr, cx, r.y, timui_str_from_cstr(buf), hs);
              cx += colw[c] + TIMUI_GRID_COL_GAP;
          }
        }
        /* body rows: the visible vertical slice, each h-scrolled. */
        { TimuiSlice rows = timui_page_slice(nrows, vis, scroll);
          int i;
          for(i = 0; i < rows.count; i++){
              int row = rows.first + i;
              TimuiStyle st = timui_widget_style_(ui, TIMUI_WIDGET_TABLE,
                  row == sel ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT,
                  row == sel ? TIMUI_STYLE_STATE_SELECTED : 0);
              y = r.y + 1 + i;
              /* fill the whole row first for a continuous selection highlight */
              timui_draw_fill(&ui->curr, TIMUI_RECT(r.x, y, r.w, 1), st);
              cx = r.x - hscroll;
              for(c = 0; c < ncols; c++){
                  char buf[TIMUI_GRID_CELLBUF];
                  int ell;
                  const char *txt = model->cell_fn ? model->cell_fn(model->ud, row, c) : "";
                  timui_fit_cell(txt, colw[c], buf, sizeof buf, &ell);
                  timui_draw_text(&ui->curr, cx, y, timui_str_from_cstr(buf), st);
                  cx += colw[c] + TIMUI_GRID_COL_GAP;
              }
          }
        }
        timui_pop_clip(f);
    }

    state.selected = sel;
    state.scroll   = scroll;
    state.hscroll  = hscroll;
    res.state = state;
    /* A pure clamp is not a change; only a real selection move counts (matches
     * timui_table). Scroll is derived state — the _mut twin persists it, but it
     * alone does not flag state_changed. */
    res.state_changed = (sel != orig_sel);
    return res;
}

TIMUI_API TimuiTableResult timui_table_ex_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTableModel *model, TimuiTableState *state){
    TimuiTableState in = state ? *state : (TimuiTableState){0, 0, 0};
    TimuiTableResult res = timui_table_ex(f, id, r, model, in);
    /* Write back the full derived state (selection + both scroll offsets), so
     * scrolling persists across frames — but only when something actually moved
     * (selection changed, or a scroll offset was adjusted). */
    if(state && (res.state_changed ||
                 res.state.scroll  != in.scroll ||
                 res.state.hscroll != in.hscroll))
        *state = res.state;
    return res;
}
/* ---- tree widget (v0.2 + scroll) -------------------------------------- *
 * Two families:
 *   - timui_tree / timui_tree_mut : renders a flat list of visible nodes the app
 *     already flattened (unchanged; backward-compatible).
 *   - timui_tree_flatten + timui_tree_scroll / _mut : for LARGE trees — pass the
 *     FULL DFS node list (with expanded flags); the pure flattener yields the
 *     VISIBLE indices, and the widget windows them to the viewport.
 *
 * The visibility rule (a collapsed node hides its deeper subtree) lives in ONE
 * place — timui_tree_step_ — shared by the pure flattener (unit-tested in
 * tests/test_grid.c) and the scrollable widget, so the two can never drift.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd. */

/* Sentinel "nothing hidden" depth watermark: any real node depth is below it. */
#define TIMUI_TREE_NOHIDE (1 << 30)

/* Step the visibility watermark past node `n` and report whether it is visible.
 * `hidden` holds the depth of the nearest enclosing COLLAPSED node: any node
 * deeper than that is a hidden descendant. A visible collapsed node re-arms the
 * watermark to its own depth; a visible expanded/leaf node clears it. */
static int timui_tree_step_(const TimuiTreeNode *n, int *hidden){
    int d = n->depth;
    if(d > *hidden) return 0;                                  /* hidden descendant */
    *hidden = (n->has_children && !n->expanded) ? d : TIMUI_TREE_NOHIDE;
    return 1;                                                  /* visible */
}

TIMUI_API int timui_tree_flatten(const TimuiTreeNode *nodes, int count, int *out, int cap){
    int i, hidden = TIMUI_TREE_NOHIDE, n = 0;
    if(!nodes || count <= 0) return 0;
    for(i = 0; i < count; i++){
        if(timui_tree_step_(&nodes[i], &hidden)){
            if(out && n < cap) out[n] = i;
            n++;                                               /* count even past cap */
        }
    }
    return n;
}

/* Draw one visible node (indent + expand marker + label) into row `y`. Shared by
 * the scroll widget; mirrors the prefix logic of the plain timui_tree below. */
static void timui_tree_draw_node_(Timui *ui, const TimuiTreeNode *node,
                                  TimuiRect r, int y, TimuiStyle st){
    char prefix[64];
    int pn = 0, j;
    /* indentation + expand marker. depth is app-supplied and unchecked, so bound
     * the loop to the buffer (reserve marker + space + NUL). */
    for(j = 0; j < node->depth && pn + 4 < (int)sizeof(prefix); j++){
        prefix[pn++] = ' '; prefix[pn++] = ' ';
    }
    if(node->has_children) prefix[pn++] = node->expanded ? '-' : '+';
    else                   prefix[pn++] = ' ';
    prefix[pn++] = ' ';
    prefix[pn] = '\0';
    timui_draw_row_(&ui->curr, TIMUI_RECT(r.x, y, r.w, 1), 0, timui_str_from_cstr(prefix), st);
    timui_draw_text(&ui->curr, r.x + pn, y, timui_str_from_cstr(node->label), st);
}

/* ----------------------------------------------------------------------- */
/* timui_tree / timui_tree_mut — flat visible list (UNCHANGED).              */
/* ----------------------------------------------------------------------- */
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
        TimuiStyle st = timui_widget_style_(ui, TIMUI_WIDGET_TREE,
            i == selected ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT,
            i == selected ? TIMUI_STYLE_STATE_SELECTED : 0);
        timui_tree_draw_node_(ui, &nodes[i], TIMUI_RECT(content.x, y, r.w, 1), y, st);
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

/* ----------------------------------------------------------------------- */
/* timui_tree_scroll — full tree, windowed to the viewport.                  */
/* `selected`/`scroll` are positions in the VISIBLE list (0-based).          */
/* ----------------------------------------------------------------------- */
TIMUI_API TimuiTreeScrollResult timui_tree_scroll(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTreeNode *nodes, int count, TimuiTreeState state){
    TimuiTreeScrollResult res;
    Timui *ui;
    TimuiInteractResult ir;
    int hidden, nvis, vis, sel, scroll, orig_sel, i, vp, wh;
    res.state = state; res.state_changed = 0; res.focused = 0;
    if(!f || !f->ui || !nodes || count <= 0) return res;
    ui = f->ui;

    /* Pass 1: count the visible nodes (so scroll/selection can be clamped). */
    hidden = TIMUI_TREE_NOHIDE; nvis = 0;
    for(i = 0; i < count; i++) if(timui_tree_step_(&nodes[i], &hidden)) nvis++;

    vis = r.h > 0 ? r.h : 1;
    sel = state.selected;
    if(nvis <= 0) sel = 0;
    else { if(sel < 0) sel = 0; if(sel >= nvis) sel = nvis - 1; }
    orig_sel = sel;

    ir = timui_interact_button(&ui->ia, id, r);
    res.focused = ir.focused;
    if(ir.focused) sel = timui_updown_nav_(f, sel, nvis);

    scroll = state.scroll < 0 ? 0 : state.scroll;
    wh = timui_mouse_wheel(f);
    if(wh){
        scroll -= wh;
        scroll = timui_page_slice(nvis, vis, scroll).first;
        if(sel < scroll) sel = scroll;
        if(sel >= scroll + vis) sel = scroll + vis - 1;
        if(nvis > 0){ if(sel >= nvis) sel = nvis - 1; } else sel = 0;
        if(sel < 0) sel = 0;
    }
    if(ir.clicked){
        int my  = ui->ia.mouse_y - r.y;
        int idx = scroll + my;
        if(my >= 0 && idx >= 0 && idx < nvis) sel = idx;
    }

    /* Keep the selection visible, then clamp the window to a valid range. */
    scroll = timui_scroll_to(sel, scroll, vis, nvis);
    scroll = timui_page_slice(nvis, vis, scroll).first;

    /* Pass 2: draw the visible nodes that land inside [scroll, scroll+vis). */
    timui_push_clip(f, r);
    hidden = TIMUI_TREE_NOHIDE; vp = 0;
    for(i = 0; i < count; i++){
        int row;
        if(!timui_tree_step_(&nodes[i], &hidden)) continue;
        row = vp - scroll;
        if(row >= 0 && row < vis){
            TimuiStyle st = timui_widget_style_(ui, TIMUI_WIDGET_TREE,
                vp == sel ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT,
                vp == sel ? TIMUI_STYLE_STATE_SELECTED : 0);
            timui_tree_draw_node_(ui, &nodes[i], r, r.y + row, st);
        }
        vp++;
    }
    timui_pop_clip(f);

    state.selected = sel;
    state.scroll   = scroll;
    res.state = state;
    res.state_changed = (sel != orig_sel);   /* pure clamp / scroll is not a change */
    return res;
}
TIMUI_API TimuiTreeScrollResult timui_tree_scroll_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiTreeNode *nodes, int count, TimuiTreeState *state){
    TimuiTreeState in = state ? *state : (TimuiTreeState){0, 0};
    TimuiTreeScrollResult res = timui_tree_scroll(f, id, r, nodes, count, in);
    /* Persist the derived state (selection + scroll) whenever either moved. */
    if(state && (res.state_changed || res.state.scroll != in.scroll))
        *state = res.state;
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
          TimuiStyle st = timui_widget_style_(ui, TIMUI_WIDGET_LISTBOX,
              i == state.selected ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT,
              i == state.selected ? TIMUI_STYLE_STATE_SELECTED : 0);
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
/* ---- combobox / autocomplete ------------------------------------------ *
 * A single-focus editable field with an attached filtered popup. The caller
 * owns query storage and selection state; options are immutable TimuiStrs. */
static int cb_match_(TimuiStr opt, const char *query){
    size_t ol = opt.len, ql = strlen(query ? query : "");
    size_t i, j;
    if(ql == 0) return 1;
    if(!opt.ptr || ql > ol) return 0;
    for(i = 0; i + ql <= ol; i++){
        for(j = 0; j < ql; j++){
            char a = opt.ptr[i + j], b = query[j];
            if(a >= 'A' && a <= 'Z') a += 32;
            if(b >= 'A' && b <= 'Z') b += 32;
            if(a != b) break;
        }
        if(j == ql) return 1;
    }
    return 0;
}
static int cb_text_byte_(unsigned char b){
    return b >= 0x20 && b != 0x7f;
}
static int cb_filter_(const TimuiStr *options, int count, const char *query, int *matches, int max){
    int i, n = 0;
    if(!options || count <= 0 || !matches || max <= 0) return 0;
    for(i = 0; i < count && n < max; i++){
        TimuiStr opt = options[i].ptr ? options[i] : (TimuiStr){ "", 0 };
        if(cb_match_(opt, query)) matches[n++] = i;
    }
    return n;
}
static int cb_insert_text_(TimuiComboboxState *st, const char *src, int nbytes){
    int j = 0, changed = 0;
    while(j < nbytes){
        int n = utf8_lead_len((unsigned char)src[j]);
        size_t m = (size_t)(n > 0 ? n : 1);
        if(!cb_text_byte_((unsigned char)src[j])){ j++; continue; }
        if(j + (int)m > nbytes) m = (size_t)(nbytes - j);
        if(!text_insert_(st->query, st->cap, st->cursor, src + j, m)) break;
        st->cursor += m;
        j += (int)m;
        changed = 1;
    }
    return changed;
}
static void cb_copy_option_(TimuiComboboxState *st, TimuiStr opt){
    size_t out = 0, i = 0;
    if(!st || !st->query || st->cap == 0) return;
    while(i < opt.len && out + 1 < st->cap){
        size_t n = timui_grapheme_next(opt.ptr, opt.len, i);
        if(n <= i) n = i + 1;
        if(out + (n - i) + 1 > st->cap) break;
        memcpy(st->query + out, opt.ptr + i, n - i);
        out += n - i;
        i = n;
    }
    st->query[out] = '\0';
    st->cursor = out;
    st->scroll_x = 0;
}
static void cb_scroll_to_selected_(TimuiComboboxState *st, int visible, int count){
    if(st->selected < 0) st->selected = 0;
    if(count <= 0){ st->selected = 0; st->scroll = 0; return; }
    if(st->selected >= count) st->selected = count - 1;
    if(st->scroll < 0) st->scroll = 0;
    if(visible <= 0){ st->scroll = 0; return; }
    if(st->selected < st->scroll) st->scroll = st->selected;
    if(st->selected >= st->scroll + visible) st->scroll = st->selected - visible + 1;
    if(count <= visible) st->scroll = 0;
    else if(st->scroll > count - visible) st->scroll = count - visible;
}
TIMUI_API TimuiComboboxResult timui_combobox(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *options, int count, TimuiComboboxState state){
    TimuiComboboxResult res;
    Timui *ui;
    TimuiInteractResult ir;
    TimuiRect field, popup;
    int matches[256], match_count = 0, visible, i;
    int user_changed = 0, query_changed = 0;
    int accept_filtered = -1;
    res.state = state;
    res.state_changed = 0;
    res.query_changed = 0;
    res.activated = -1;
    res.selected = -1;
    res.match_count = 0;
    res.focused = 0;
    if(!f || !f->ui || !state.query || state.cap == 0 || count < 0) return res;
    ui = f->ui;
    if(state.cursor >= state.cap) state.cursor = state.cap - 1;
    field = TIMUI_RECT(r.x, r.y, r.w, r.h > 0 ? 1 : 0);
    popup = TIMUI_RECT(r.x, r.y + 1, r.w, r.h > 1 ? r.h - 1 : 0);
    ir = timui_interact_button(&ui->ia, id, r);
    res.focused = ir.focused;
    if(ir.focused){
        size_t len;
        size_t cursor_before;
        if(ui->text_in_len > 0){
            query_changed = cb_insert_text_(&state, ui->text_in, ui->text_in_len);
            if(query_changed){ state.open = 1; state.selected = 0; state.scroll = 0; user_changed = 1; }
        }
        len = strlen(state.query);
        cursor_before = state.cursor;
        if(ui->key_in & TIMUI_KEYIN_LEFT)  state.cursor = utf8_drop_last(state.query, state.cursor);
        if(ui->key_in & TIMUI_KEYIN_RIGHT) state.cursor = utf8_next_(state.query, state.cursor, len);
        if(ui->key_in & TIMUI_KEYIN_HOME)  state.cursor = 0;
        if(ui->key_in & TIMUI_KEYIN_END)   state.cursor = len;
        if(state.cursor != cursor_before) user_changed = 1;
        if((ui->key_in & TIMUI_KEYIN_BACKSPACE) && state.cursor > 0){
            size_t prev = utf8_drop_last(state.query, state.cursor);
            state.cursor = text_erase_(state.query, prev, state.cursor);
            query_changed = 1; state.open = 1; state.selected = 0; state.scroll = 0; user_changed = 1;
        }
        if(ui->key_in & TIMUI_KEYIN_DELETE){
            size_t nxt = utf8_next_(state.query, state.cursor, strlen(state.query));
            if(nxt > state.cursor){
                (void)text_erase_(state.query, state.cursor, nxt);
                query_changed = 1; state.open = 1; state.selected = 0; state.scroll = 0; user_changed = 1;
            }
        }
        if(ui->key_pressed == TIMUI_KEY_ESCAPE && state.open){
            state.open = 0;
            user_changed = 1;
        }
        ui->text_in_len = 0;
        ui->enter_count = 0;
        ui->key_in = 0;
    }
    match_count = cb_filter_(options, count, state.query, matches, (int)(sizeof matches / sizeof matches[0]));
    visible = popup.h > 0 ? popup.h : 0;
    cb_scroll_to_selected_(&state, visible, match_count);
    if(match_count > 0) res.selected = matches[state.selected];
    if(ir.focused && state.open && match_count > 0){
        if(timui_key_pressed(f, TIMUI_KEY_UP) && state.selected > 0){
            state.selected--; user_changed = 1;
        }
        if(timui_key_pressed(f, TIMUI_KEY_DOWN) && state.selected < match_count - 1){
            state.selected++; user_changed = 1;
        }
        cb_scroll_to_selected_(&state, visible, match_count);
    }
    if(ir.clicked && ui->ia.mouse_released){
        int my = ui->ia.mouse_y - r.y;
        if(my == 0){ state.open = 1; user_changed = 1; }
        else if(state.open && my > 0 && my <= visible){
            int row = state.scroll + my - 1;
            if(row >= 0 && row < match_count) accept_filtered = row;
        }
    }
    if(ir.focused && state.open && timui_key_pressed(f, TIMUI_KEY_ENTER) && match_count > 0)
        accept_filtered = state.selected;
    if(accept_filtered >= 0 && accept_filtered < match_count){
        int orig = matches[accept_filtered];
        cb_copy_option_(&state, options[orig]);
        res.activated = orig;
        res.selected = orig;
        query_changed = 1;
        user_changed = 1;
        state.open = 0;
        state.selected = accept_filtered;
    } else if(match_count > 0) {
        res.selected = matches[state.selected];
    }
    { int ccol = display_col_(state.query, state.cursor);
      int scroll_x = state.scroll_x;
      if(ccol < scroll_x) scroll_x = ccol;
      if(field.w > 0 && ccol >= scroll_x + field.w) scroll_x = ccol - field.w + 1;
      if(scroll_x < 0) scroll_x = 0;
      state.scroll_x = scroll_x;
      if(ir.focused && field.h > 0){
          ui->cursor_x = field.x + (ccol - state.scroll_x);
          ui->cursor_y = field.y;
          ui->cursor_visible = 1;
      }
    }
    { TimuiStyle fst = timui_widget_style_(ui, TIMUI_WIDGET_INPUT,
          ir.focused ? TIMUI_SLOT_INPUT_FOCUSED : TIMUI_SLOT_INPUT,
          ir.focused ? TIMUI_STYLE_STATE_FOCUSED : 0);
      timui_draw_fill(&ui->curr, field, fst);
      timui_push_clip(f, field);
      timui_draw_text(&ui->curr, field.x - state.scroll_x, field.y, timui_str_from_cstr(state.query), fst);
      timui_pop_clip(f); }
    if(state.open && popup.h > 0){
        timui_push_clip(f, popup);
        for(i = 0; i < popup.h; i++){
            int mi = state.scroll + i;
            TimuiStyle st;
            TimuiStr label;
            if(mi >= match_count) break;
            st = timui_widget_style_(ui, TIMUI_WIDGET_LISTBOX,
                mi == state.selected ? TIMUI_SLOT_SELECTION : TIMUI_SLOT_TEXT,
                mi == state.selected ? TIMUI_STYLE_STATE_SELECTED : 0);
            label = options[matches[mi]].ptr ? options[matches[mi]] : TIMUI_STR_LIT("");
            timui_draw_row_(&ui->curr, TIMUI_RECT(popup.x, popup.y + i, popup.w, 1), 0, label, st);
        }
        timui_pop_clip(f);
    }
    res.state = state;
    res.state_changed = user_changed;
    res.query_changed = query_changed;
    res.match_count = match_count;
    res.focused = ir.focused;
    return res;
}
TIMUI_API TimuiComboboxResult timui_combobox_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
    const TimuiStr *options, int count, TimuiComboboxState *state){
    TimuiComboboxState empty;
    TimuiComboboxResult res;
    memset(&empty, 0, sizeof empty);
    res = timui_combobox(f, id, r, options, count, state ? *state : empty);
    if(state && res.state_changed) *state = res.state;
    return res;
}
/* ---- toast / notification renderer ------------------------------------ *
 * Immediate-mode, caller-owned notifications. The widget only reports which
 * original item was clicked for dismissal; callers decide whether to remove it. */
static int toast_alive_(const TimuiToast *t, uint64_t now_ms){
    if(!t || t->dismissed) return 0;
    if(t->ttl_ms == 0) return 1;
    if(now_ms < t->created_ms) return 1;
    return now_ms - t->created_ms < t->ttl_ms;
}
static TimuiStyle toast_style_(Timui *ui, TimuiToastSeverity severity){
    TimuiStyleSlot slot = TIMUI_SLOT_STATUS;
    if(severity == TIMUI_TOAST_SUCCESS) slot = TIMUI_SLOT_SUCCESS;
    else if(severity == TIMUI_TOAST_WARNING) slot = TIMUI_SLOT_WARNING;
    else if(severity == TIMUI_TOAST_ERROR) slot = TIMUI_SLOT_ERROR;
    return timui_widget_style_(ui, TIMUI_WIDGET_TOAST, slot, 0);
}
TIMUI_API TimuiToastResult timui_toasts(TimuiFrame *f, TimuiId id, TimuiRect r,
                                        const TimuiToast *toasts, int count,
                                        uint64_t now_ms){
    TimuiToastResult res;
    Timui *ui;
    int i, y;
    res.dismissed = -1;
    res.visible_count = 0;
    if(!f || !f->ui || !toasts || count <= 0 || r.w <= 0 || r.h <= 0) return res;
    ui = f->ui;
    y = r.y;
    timui_push_clip(f, r);
    for(i = 0; i < count; i++){
        TimuiRect tr;
        TimuiStyle st;
        TimuiInteractResult ir;
        if(!toast_alive_(&toasts[i], now_ms)) continue;
        if(y + 3 > r.y + r.h) break;
        tr = TIMUI_RECT(r.x, y, r.w, 3);
        st = toast_style_(ui, toasts[i].severity);
        ir = timui_interact_button(&ui->ia, id + (TimuiId)(i + 1), tr);
        if(ir.clicked && ui->ia.mouse_released) res.dismissed = i;
        timui_draw_fill(&ui->curr, tr,
                        timui_widget_style_(ui, TIMUI_WIDGET_TOAST, TIMUI_SLOT_PANEL, 0));
        timui_draw_box(&ui->curr, tr, TIMUI_BORDER_ROUND, st);
        widget_draw_text_clipped(f, tr, tr.x + 2, tr.y, toasts[i].title, st);
        widget_draw_text_clipped(f, tr, tr.x + 2, tr.y + 1, toasts[i].message,
                                 timui_widget_style_(ui, TIMUI_WIDGET_TOAST, TIMUI_SLOT_TEXT, 0));
        if(tr.w >= 5)
            timui_draw_text(&ui->curr, tr.x + tr.w - 4, tr.y, TIMUI_STR_LIT("[x]"), st);
        res.visible_count++;
        y += 3;
    }
    timui_pop_clip(f);
    return res;
}
/* ---- split / resizable pane widget ------------------------------------ *
 * Caller-owned two-pane splitter. This intentionally implements only local
 * divider drag; richer pointer capture and hover routing belong to Phase 2. */

static float split_pane_ratio_(float ratio){
    if(!(ratio == ratio)) return 0.5f;   /* NaN */
    if(ratio < 0.0f) return 0.0f;
    if(ratio > 1.0f) return 1.0f;
    return ratio;
}

static int split_pane_axis_len_(TimuiRect r, TimuiAxis axis){
    int n = axis == TIMUI_AXIS_V ? r.h : r.w;
    return n > 0 ? n : 0;
}

static int split_pane_cross_len_(TimuiRect r, TimuiAxis axis){
    int n = axis == TIMUI_AXIS_V ? r.w : r.h;
    return n > 0 ? n : 0;
}

static int split_pane_first_size_(int desired, int avail, int min_first, int min_second){
    int lo, hi;
    if(avail <= 0) return 0;
    if(min_first < 0) min_first = 0;
    if(min_second < 0) min_second = 0;
    if(min_first + min_second > avail){
        int total = min_first + min_second;
        if(total <= 0) return avail / 2;
        return (int)(((long)avail * min_first + total / 2) / total);
    }
    lo = min_first;
    hi = avail - min_second;
    if(desired < lo) desired = lo;
    if(desired > hi) desired = hi;
    return desired;
}

static TimuiSplitPaneResult split_pane_layout_(TimuiRect r, TimuiAxis axis,
                                               TimuiSplitPaneState state){
    TimuiSplitPaneResult res;
    int axis_len = split_pane_axis_len_(r, axis);
    int cross_len = split_pane_cross_len_(r, axis);
    int div = axis_len > 0 ? 1 : 0;
    int avail = axis_len - div;
    int first;
    if(avail < 0) avail = 0;
    state.ratio = split_pane_ratio_(state.ratio);
    if(state.min_first < 0) state.min_first = 0;
    if(state.min_second < 0) state.min_second = 0;
    first = split_pane_first_size_((int)((float)avail * state.ratio + 0.5f),
                                   avail, state.min_first, state.min_second);
    state.ratio = avail > 0 ? (float)first / (float)avail : 0.0f;

    res.state = state;
    res.changed = false;
    res.hovered = false;
    res.dragging = false;
    if(axis == TIMUI_AXIS_V){
        res.first = TIMUI_RECT(r.x, r.y, cross_len, first);
        res.divider = TIMUI_RECT(r.x, r.y + first, cross_len, div);
        res.second = TIMUI_RECT(r.x, r.y + first + div, cross_len, avail - first);
    } else {
        res.first = TIMUI_RECT(r.x, r.y, first, cross_len);
        res.divider = TIMUI_RECT(r.x + first, r.y, div, cross_len);
        res.second = TIMUI_RECT(r.x + first + div, r.y, avail - first, cross_len);
    }
    return res;
}

static int split_pane_same_layout_(TimuiSplitPaneResult a, TimuiSplitPaneResult b){
    return a.first.x == b.first.x && a.first.y == b.first.y &&
           a.first.w == b.first.w && a.first.h == b.first.h &&
           a.divider.x == b.divider.x && a.divider.y == b.divider.y &&
           a.divider.w == b.divider.w && a.divider.h == b.divider.h &&
           a.second.x == b.second.x && a.second.y == b.second.y &&
           a.second.w == b.second.w && a.second.h == b.second.h;
}

static TimuiSplitPaneResult split_pane_drag_(TimuiSplitPaneResult base, TimuiRect r,
                                             TimuiAxis axis, int mouse_x, int mouse_y){
    TimuiSplitPaneState state = base.state;
    int axis_len = split_pane_axis_len_(r, axis);
    int avail = axis_len > 0 ? axis_len - 1 : 0;
    int first = axis == TIMUI_AXIS_V ? mouse_y - r.y : mouse_x - r.x;
    TimuiSplitPaneResult next;
    if(avail < 0) avail = 0;
    first = split_pane_first_size_(first, avail, state.min_first, state.min_second);
    state.ratio = avail > 0 ? (float)first / (float)avail : 0.0f;
    next = split_pane_layout_(r, axis, state);
    next.changed = !split_pane_same_layout_(base, next);
    next.dragging = true;
    next.hovered = base.hovered;
    return next;
}

static void split_pane_draw_(Timui *ui, TimuiSplitPaneResult res, TimuiAxis axis){
    TimuiStyle st;
    if(!ui || res.divider.w <= 0 || res.divider.h <= 0) return;
    st = timui_widget_style_(ui, TIMUI_WIDGET_SPLIT,
                             res.dragging ? TIMUI_SLOT_SELECTION :
                             res.hovered ? TIMUI_SLOT_BUTTON_HOVERED : TIMUI_SLOT_BORDER,
                             (res.dragging ? TIMUI_STYLE_STATE_ACTIVE : 0) |
                             (res.hovered ? TIMUI_STYLE_STATE_HOVERED : 0));
    timui_draw_fill(&ui->curr, res.divider, st);
    if(axis == TIMUI_AXIS_V)
        timui_draw_hline(&ui->curr, res.divider.x, res.divider.y, res.divider.w, st);
    else
        timui_draw_vline(&ui->curr, res.divider.x, res.divider.y, res.divider.h, st);
}

TIMUI_API TimuiSplitPaneResult timui_split_pane(TimuiFrame *f, TimuiId id, TimuiRect r,
                                                TimuiAxis axis, TimuiSplitPaneState state){
    TimuiSplitPaneResult res;
    Timui *ui;
    TimuiInteractResult ir;
    if(axis != TIMUI_AXIS_V) axis = TIMUI_AXIS_H;
    res = split_pane_layout_(r, axis, state);
    if(!f || !f->ui) return res;
    ui = f->ui;
    ir = timui_interact_button(&ui->ia, id, res.divider);
    res.hovered = ir.hovered;
    res.dragging = ir.pressed;
    if(ir.pressed)
        res = split_pane_drag_(res, r, axis, ui->ia.mouse_x, ui->ia.mouse_y);
    split_pane_draw_(ui, res, axis);
    return res;
}

TIMUI_API TimuiSplitPaneResult timui_split_pane_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
                                                    TimuiAxis axis, TimuiSplitPaneState *state){
    TimuiSplitPaneState in = {0.5f, 0, 0};
    TimuiSplitPaneResult res;
    if(state) in = *state;
    res = timui_split_pane(f, id, r, axis, in);
    if(state && res.changed) *state = res.state;
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
 *   fg / bg   : '-' when "default" (field == TIMUI_COLOR_DEFAULT, matching
 *               emit_sgr's "no SGR" emitted); otherwise 6-digit lowercase
 *               hex rrggbb. Pure black (0x000000) is literal black.
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
 * A multi-line text editor. Click to focus, type to insert, Backspace/Delete
 * remove whole grapheme clusters. Lines are split on '\n'. */
static int text_area_insert_span_(TimuiTextAreaState *st, const char *src, int nbytes){
    int j = 0;
    int changed = 0;
    while(j < nbytes){
        int n = utf8_lead_len((unsigned char)src[j]);
        size_t m = (size_t)(n > 0 ? n : 1);     /* defensive: stray byte as 1 */
        if(j + (int)m > nbytes) m = (size_t)(nbytes - j);
        if(!text_insert_(st->text, st->cap, st->cursor, src + j, m)) break;
        st->cursor += m;
        j += (int)m;
        changed = 1;
    }
    return changed;
}
static int text_area_insert_newline_(TimuiTextAreaState *st){
    if(!text_insert_(st->text, st->cap, st->cursor, "\n", 1)) return 0;
    st->cursor++;
    return 1;
}
static int text_area_enter_submits_(const Timui *ui, int enter_index, uint32_t flags){
    if(!(flags & TIMUI_TEXT_AREA_ENTER_SUBMITS)) return 0;
    return !(ui->enter_mods[enter_index] & TIMUI_MOD_SHIFT);
}
static void text_area_defer_after_enter_(Timui *ui, int at, int enter_index){
    int tail = ui->text_in_len - at;
    int k;
    if(tail < 0) tail = 0;
    if(tail > (int)sizeof(ui->pending_in)) tail = (int)sizeof(ui->pending_in);
    memcpy(ui->pending_in, ui->text_in + at, (size_t)tail);
    ui->pending_in_len = tail;
    ui->pending_enter_count = ui->enter_count - enter_index - 1;
    if(ui->pending_enter_count < 0) ui->pending_enter_count = 0;
    for(k = 0; k < ui->pending_enter_count; k++){
        ui->pending_enter_at[k] = ui->enter_at[enter_index + 1 + k] - at;
        ui->pending_enter_mods[k] = ui->enter_mods[enter_index + 1 + k];
    }
}
static void text_area_process_text_(Timui *ui, TimuiTextAreaState *st,
                                    uint32_t flags, TimuiTextAreaResult *res){
    int j = 0;
    int e;
    for(e = 0; e < ui->enter_count; e++){
        int at = ui->enter_at[e];
        if(at < j) at = j;
        if(at > ui->text_in_len) at = ui->text_in_len;
        if(text_area_insert_span_(st, ui->text_in + j, at - j)) res->changed = 1;
        if(text_area_enter_submits_(ui, e, flags)){
            res->submitted = 1;
            text_area_defer_after_enter_(ui, at, e);
            return;
        }
        if(text_area_insert_newline_(st)) res->changed = 1;
        j = at;
    }
    if(text_area_insert_span_(st, ui->text_in + j, ui->text_in_len - j)) res->changed = 1;
}
TIMUI_API TimuiTextAreaResult timui_text_area_ex(TimuiFrame *f, TimuiId id, TimuiRect r,
                                                 TimuiTextAreaState st, uint32_t flags){
    Timui *ui;
    TimuiInteractResult ir;
    TimuiRect content;
    TimuiTextAreaResult res;
    size_t i;
    int y = 0;
    res.state = st;
    res.changed = 0;
    res.submitted = 0;
    res.focused = 0;
    if(!f || !f->ui || !st.text || st.cap == 0) return res;
    { size_t text_len = text_len_bounded_(st.text, st.cap);
      if(text_len >= st.cap) text_len = st.cap - 1;
      if(st.cursor > text_len) st.cursor = text_len; }   /* Y1: untrusted cursor -> OOB */
    ui = f->ui;
    ir = timui_interact_button(&ui->ia, id, r);
    res.focused = ir.focused;
    if(ir.focused){
        /* Insert typed codepoints at the cursor. Deletion/movement below is
         * grapheme-aware; insertion remains codepoint-by-codepoint and
         * cap-bounded, so invalid partial UTF-8 is not created.
         * The edit helpers live in the widgets section, in scope via the unity
         * build. */
        text_area_process_text_(ui, &st, flags, &res);
        /* cursor movement (one step per frame — the key_in bitmask can't count
         * repeats; key auto-repeat delivers one per frame). */
        if(ui->key_in & TIMUI_KEYIN_LEFT)  st.cursor = utf8_drop_last(st.text, st.cursor);
        if(ui->key_in & TIMUI_KEYIN_RIGHT) st.cursor = utf8_next_(st.text, st.cursor, strlen(st.text));
        if(ui->key_in & TIMUI_KEYIN_HOME)  st.cursor = line_start_(st.text, st.cursor);
        if(ui->key_in & TIMUI_KEYIN_END)   st.cursor = line_end_(st.text, st.cursor);
        /* deletion: backspace removes the cluster before the cursor, DELETE
         * the one at the cursor. */
        if((ui->key_in & TIMUI_KEYIN_BACKSPACE) && st.cursor > 0){
            size_t prev = utf8_drop_last(st.text, st.cursor);
            st.cursor = text_erase_(st.text, prev, st.cursor);
            res.changed = 1;
        }
        if(ui->key_in & TIMUI_KEYIN_DELETE){
            size_t nxt = utf8_next_(st.text, st.cursor, strlen(st.text));
            if(nxt > st.cursor){
                (void)text_erase_(st.text, st.cursor, nxt);
                res.changed = 1;
            }
        }
        ui->text_in_len = 0;
        ui->enter_count = 0;
        ui->key_in = 0;
    }
    { TimuiStyle sst = timui_widget_style_(ui, TIMUI_WIDGET_TEXT_AREA,
          ir.focused ? TIMUI_SLOT_INPUT_FOCUSED : TIMUI_SLOT_INPUT,
          ir.focused ? TIMUI_STYLE_STATE_FOCUSED : 0);
      content = timui_scroll_begin(f, r, st.scroll_y);
      i = 0;
      while(i < st.cap && st.text[i]){
          size_t ls = i;
          while(i < st.cap && st.text[i] && st.text[i] != '\n'){
              if(st.text[i] == '\r') break;  /* \r or \r\n line break */
              i++;
          }
          timui_draw_text(&ui->curr, content.x, content.y + y,
                          (TimuiStr){ st.text + ls, i - ls }, sst);
          if(i < st.cap && (st.text[i] == '\r' || st.text[i] == '\n')){
              if(st.text[i] == '\r' && i + 1 < st.cap && st.text[i+1] == '\n') i++;
              i++;
          }
          y++;
      }
      /* auto-scroll to keep the cursor visible (computed before scroll_begin next frame) */
      {  int cursor_row = 0;
         size_t ci;
         for(ci = 0; ci < st.cursor && ci < st.cap; ci++)
             if(st.text[ci] == '\n' || st.text[ci] == '\r'){
                 cursor_row++;
                 if(st.text[ci] == '\r' && ci + 1 < st.cap && st.text[ci+1] == '\n') ci++;
             }
         if(cursor_row < st.scroll_y) st.scroll_y = cursor_row;
         if(cursor_row >= st.scroll_y + r.h) st.scroll_y = cursor_row - r.h + 1;
         if(st.scroll_y < 0) st.scroll_y = 0;
      }
      timui_scroll_end(f);
      if(ir.focused){                                 /* F1.4: request the hardware cursor */
          int crow, ccol;
          text_pos_(st.text, st.cursor, &crow, &ccol);
          if(crow >= st.scroll_y && crow < st.scroll_y + r.h && ccol < r.w){
              ui->cursor_x = r.x + ccol;                /* content.x == r.x (vertical scroll only) */
              ui->cursor_y = r.y + (crow - st.scroll_y);
              ui->cursor_visible = 1;
          }
      }
    }
    res.state = st;
    return res;
}
TIMUI_API TimuiTextAreaResult timui_text_area_mut(TimuiFrame *f, TimuiId id, TimuiRect r,
                                                  TimuiTextAreaState *state, uint32_t flags){
    TimuiTextAreaResult res;
    if(!state){
        TimuiTextAreaState empty = {0};
        return timui_text_area_ex(f, id, r, empty, flags);
    }
    res = timui_text_area_ex(f, id, r, *state, flags);
    *state = res.state;
    return res;
}
TIMUI_API void timui_text_area(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiTextAreaState *state){
    (void)timui_text_area_mut(f, id, r, state, TIMUI_TEXT_AREA_DEFAULT);
}
/* ---- Windows ConPTY backend (v0.2) ------------------------------------ *
 * On Windows, ConPTY (CreatePseudoConsole) provides a VT byte-stream transport
 * to a child console application. Non-Windows builds keep returning
 * TIMUI_ERR_UNSUPPORTED. */

#define TIMUI_CONPTY_COORD_MAX 32767
#define TIMUI_CONPTY_IO_CHUNK_MAX ((size_t)(1024u * 1024u))

TIMUI_API size_t timui_conpty_io_chunk_for_test(size_t remaining){
    size_t max = TIMUI_CONPTY_IO_CHUNK_MAX;
    if(max > (size_t)INT_MAX) max = (size_t)INT_MAX;
    if(remaining < max) return remaining;
    return max;
}

TIMUI_API int timui_conpty_size_valid_for_test(int cols, int rows){
    return cols > 0 && rows > 0 &&
           cols <= TIMUI_CONPTY_COORD_MAX && rows <= TIMUI_CONPTY_COORD_MAX;
}

static void conpty_zero_transport_(TimuiTransport *t){
    if(!t) return;
    t->write = NULL;
    t->read = NULL;
    t->flush = NULL;
    t->close = NULL;
    t->ctx = NULL;
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

typedef HRESULT (WINAPI *TimuiCreatePseudoConsoleFn)(COORD, HANDLE, HANDLE, DWORD, HPCON *);
typedef HRESULT (WINAPI *TimuiResizePseudoConsoleFn)(HPCON, COORD);
typedef VOID    (WINAPI *TimuiClosePseudoConsoleFn)(HPCON);

typedef struct {
    TimuiCreatePseudoConsoleFn create_pseudo_console;
    TimuiResizePseudoConsoleFn resize_pseudo_console;
    TimuiClosePseudoConsoleFn  close_pseudo_console;
} TimuiConptyApi;

typedef struct {
    HPCON  hpc;
    HANDLE hPipeIn;       /* host writes here -> ConPTY input */
    HANDLE hPipeOut;      /* host reads here  <- ConPTY output */
    HANDLE hProcess;
    HANDLE hThread;
    DWORD  pid;
    TimuiResizePseudoConsoleFn resize_pseudo_console;
    TimuiClosePseudoConsoleFn  close_pseudo_console;
} TimuiConptyCtx;

static int conpty_load_api_(TimuiConptyApi *api){
    HMODULE kernel32;
    union { FARPROC p; TimuiCreatePseudoConsoleFn f; } create_pc;
    union { FARPROC p; TimuiResizePseudoConsoleFn f; } resize_pc;
    union { FARPROC p; TimuiClosePseudoConsoleFn f; } close_pc;
    if(!api) return 0;
    memset(api, 0, sizeof *api);
    kernel32 = GetModuleHandleW(L"kernel32.dll");
    if(!kernel32) kernel32 = LoadLibraryW(L"kernel32.dll");
    if(!kernel32) return 0;
    create_pc.p = GetProcAddress(kernel32, "CreatePseudoConsole");
    resize_pc.p = GetProcAddress(kernel32, "ResizePseudoConsole");
    close_pc.p = GetProcAddress(kernel32, "ClosePseudoConsole");
    api->create_pseudo_console = create_pc.f;
    api->resize_pseudo_console = resize_pc.f;
    api->close_pseudo_console = close_pc.f;
    return api->create_pseudo_console &&
           api->resize_pseudo_console &&
           api->close_pseudo_console;
}

static void conpty_close_handle_(HANDLE *h){
    if(h && *h && *h != INVALID_HANDLE_VALUE){
        CloseHandle(*h);
        *h = NULL;
    }
}

static void conpty_cleanup_ctx_(TimuiConptyCtx *c){
    DWORD exit_code = 0;
    if(!c) return;
    conpty_close_handle_(&c->hPipeIn);
    conpty_close_handle_(&c->hPipeOut);
    if(c->hpc){
        c->close_pseudo_console(c->hpc);
        c->hpc = NULL;
    }
    if(c->hProcess){
        if(WaitForSingleObject(c->hProcess, 1000) == WAIT_TIMEOUT &&
           GetExitCodeProcess(c->hProcess, &exit_code) && exit_code == STILL_ACTIVE){
            (void)TerminateProcess(c->hProcess, 1);
            (void)WaitForSingleObject(c->hProcess, 1000);
        }
    }
    conpty_close_handle_(&c->hThread);
    conpty_close_handle_(&c->hProcess);
    free(c);
}

static int conpty_write(TimuiTransport *t, const void *d, size_t n){
    TimuiConptyCtx *c = t ? (TimuiConptyCtx *)t->ctx : NULL;
    const unsigned char *p = (const unsigned char *)d;
    size_t limit, off = 0;
    if(!c || !c->hPipeIn || (!d && n > 0)) return -1;
    limit = n < (size_t)INT_MAX ? n : (size_t)INT_MAX;
    while(off < limit){
        size_t chunk = timui_conpty_io_chunk_for_test(limit - off);
        DWORD written = 0;
        if(chunk == 0) break;
        if(!WriteFile(c->hPipeIn, p + off, (DWORD)chunk, &written, NULL))
            break;
        if(written == 0) break;
        off += (size_t)written;
    }
    return (off == 0 && n > 0) ? -1 : (int)off;
}

static int conpty_read(TimuiTransport *t, void *b, size_t cap){
    TimuiConptyCtx *c = t ? (TimuiConptyCtx *)t->ctx : NULL;
    DWORD avail = 0, got = 0;
    size_t chunk;
    if(!c || !c->hPipeOut || !b || cap == 0) return 0;
    if(!PeekNamedPipe(c->hPipeOut, NULL, 0, NULL, &avail, NULL) || avail == 0)
        return 0;
    chunk = timui_conpty_io_chunk_for_test(cap);
    if(chunk > (size_t)avail) chunk = (size_t)avail;
    if(chunk == 0) return 0;
    if(!ReadFile(c->hPipeOut, b, (DWORD)chunk, &got, NULL)) return 0;
    return (int)got;
}

static int conpty_flush(TimuiTransport *t){ (void)t; return 0; }
static void conpty_close_transport(TimuiTransport *t){
    if(!t || !t->ctx) return;
    conpty_cleanup_ctx_((TimuiConptyCtx *)t->ctx);
    conpty_zero_transport_(t);
}

static void conpty_default_command_(wchar_t *buf, DWORD cap){
    DWORD n;
    static const wchar_t fallback[] = L"cmd.exe";
    if(!buf || cap == 0) return;
    buf[0] = 0;
    n = GetEnvironmentVariableW(L"COMSPEC", buf, cap);
    if(n == 0 || n >= cap){
        DWORD i;
        for(i = 0; i + 1 < cap && fallback[i]; i++) buf[i] = fallback[i];
        buf[i] = 0;
    }
}

TIMUI_API TimuiResult timui_conpty_open(TimuiTransport *out_transport, int *out_pid){
    TimuiConptyCtx *ctx = NULL;
    HANDLE in_read = NULL, in_write = NULL, out_read = NULL, out_write = NULL;
    SIZE_T attr_bytes = 0;
    STARTUPINFOEXW si;
    PROCESS_INFORMATION pi;
    COORD size;
    wchar_t cmd[32768];
    TimuiConptyApi api;
    TimuiResult result = TIMUI_ERR_OS;
    int attr_ready = 0;

    if(!out_transport || !out_pid) return TIMUI_ERR_INVALID_ARGUMENT;
    conpty_zero_transport_(out_transport);
    *out_pid = -1;
    if(!conpty_load_api_(&api)) return TIMUI_ERR_UNSUPPORTED;

    ctx = (TimuiConptyCtx *)calloc(1, sizeof *ctx);
    if(!ctx) return TIMUI_ERR_OUT_OF_MEMORY;
    ctx->resize_pseudo_console = api.resize_pseudo_console;
    ctx->close_pseudo_console = api.close_pseudo_console;
    if(!CreatePipe(&in_read, &in_write, NULL, 0)) goto fail;
    if(!CreatePipe(&out_read, &out_write, NULL, 0)) goto fail;

    size.X = 80;
    size.Y = 24;
    if(FAILED(api.create_pseudo_console(size, in_read, out_write, 0, &ctx->hpc))) goto fail;
    conpty_close_handle_(&in_read);
    conpty_close_handle_(&out_write);

    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_bytes);
    if(attr_bytes == 0) goto fail;
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)calloc(1, attr_bytes);
    if(!si.lpAttributeList){ result = TIMUI_ERR_OUT_OF_MEMORY; goto fail; }
    if(!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_bytes)) goto fail_attr;
    attr_ready = 1;
    if(!UpdateProcThreadAttribute(si.lpAttributeList, 0,
                                  PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                  ctx->hpc, sizeof ctx->hpc, NULL, NULL)) goto fail_attr;

    conpty_default_command_(cmd, (DWORD)(sizeof cmd / sizeof cmd[0]));
    if(!cmd[0]) goto fail_attr;
    if(!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT,
                       NULL, NULL, &si.StartupInfo, &pi)) goto fail_attr;
    DeleteProcThreadAttributeList(si.lpAttributeList);
    free(si.lpAttributeList);
    si.lpAttributeList = NULL;

    if(pi.dwProcessId > (DWORD)INT_MAX){
        ctx->hProcess = pi.hProcess;
        ctx->hThread = pi.hThread;
        goto fail;
    }
    ctx->hPipeIn = in_write;
    ctx->hPipeOut = out_read;
    in_write = NULL;
    out_read = NULL;
    ctx->hProcess = pi.hProcess;
    ctx->hThread = pi.hThread;
    ctx->pid = pi.dwProcessId;

    out_transport->write = conpty_write;
    out_transport->read = conpty_read;
    out_transport->flush = conpty_flush;
    out_transport->close = conpty_close_transport;
    out_transport->ctx = ctx;
    *out_pid = (int)ctx->pid;
    conpty_close_handle_(&in_read);
    conpty_close_handle_(&out_write);
    return TIMUI_OK;

fail_attr:
    if(attr_ready)
        DeleteProcThreadAttributeList(si.lpAttributeList);
    if(si.lpAttributeList)
        free(si.lpAttributeList);
fail:
    conpty_close_handle_(&in_read);
    conpty_close_handle_(&in_write);
    conpty_close_handle_(&out_read);
    conpty_close_handle_(&out_write);
    conpty_cleanup_ctx_(ctx);
    conpty_zero_transport_(out_transport);
    *out_pid = -1;
    return result;
}

TIMUI_API TimuiResult timui_conpty_resize(TimuiTransport *transport, int cols, int rows){
    TimuiConptyCtx *c;
    COORD size;
    if(!transport || !timui_conpty_size_valid_for_test(cols, rows))
        return TIMUI_ERR_INVALID_ARGUMENT;
    c = (TimuiConptyCtx *)transport->ctx;
    if(!c || !c->hpc || !c->resize_pseudo_console) return TIMUI_ERR_INVALID_ARGUMENT;
    size.X = (SHORT)cols;
    size.Y = (SHORT)rows;
    return SUCCEEDED(c->resize_pseudo_console(c->hpc, size)) ? TIMUI_OK : TIMUI_ERR_OS;
}

TIMUI_API void timui_conpty_close(TimuiTransport *transport, int pid){
    (void)pid;
    conpty_close_transport(transport);
}
#else
TIMUI_API TimuiResult timui_conpty_open(TimuiTransport *out_transport, int *out_pid){
    if(!out_transport || !out_pid) return TIMUI_ERR_INVALID_ARGUMENT;
    conpty_zero_transport_(out_transport);
    *out_pid = -1;
    return TIMUI_ERR_UNSUPPORTED;
}
TIMUI_API TimuiResult timui_conpty_resize(TimuiTransport *transport, int cols, int rows){
    if(!transport || !timui_conpty_size_valid_for_test(cols, rows))
        return TIMUI_ERR_INVALID_ARGUMENT;
    return TIMUI_ERR_UNSUPPORTED;
}
TIMUI_API void timui_conpty_close(TimuiTransport *transport, int pid){
    (void)pid;
    conpty_zero_transport_(transport);
}
#endif
/* ---- Terminal images (v0.2) ------------------------------------------- *
 * Accept PNG bytes and raw RGBA pixels; unsupported protocol/data pairs draw
 * a "[img]" placeholder instead of guessing. */

#ifndef TIMUI_NO_IMAGES
#ifndef TIMUI_IMAGE_PNG_MAX_DIMENSION
#define TIMUI_IMAGE_PNG_MAX_DIMENSION 4096
#endif
#ifndef TIMUI_IMAGE_PNG_MAX_PIXELS
#define TIMUI_IMAGE_PNG_MAX_PIXELS 16777216u
#endif
#ifndef STBI_MAX_DIMENSIONS
#define TIMUI_UNDEF_STBI_MAX_DIMENSIONS
#define STBI_MAX_DIMENSIONS TIMUI_IMAGE_PNG_MAX_DIMENSION
#endif
#ifndef STB_IMAGE_STATIC
#define TIMUI_UNDEF_STB_IMAGE_STATIC
#define STB_IMAGE_STATIC
#endif
#ifndef STB_IMAGE_IMPLEMENTATION
#define TIMUI_UNDEF_STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif
#ifndef STBI_ONLY_PNG
#define TIMUI_UNDEF_STBI_ONLY_PNG
#define STBI_ONLY_PNG
#endif
#ifndef STBI_NO_STDIO
#define TIMUI_UNDEF_STBI_NO_STDIO
#define STBI_NO_STDIO
#endif
#ifndef STBI_NO_LINEAR
#define TIMUI_UNDEF_STBI_NO_LINEAR
#define STBI_NO_LINEAR
#endif
#ifndef STBI_NO_HDR
#define TIMUI_UNDEF_STBI_NO_HDR
#define STBI_NO_HDR
#endif
#ifndef STBI_NO_THREAD_LOCALS
#define TIMUI_UNDEF_STBI_NO_THREAD_LOCALS
#define STBI_NO_THREAD_LOCALS
#endif
#ifndef STBI_NO_FAILURE_STRINGS
#define TIMUI_UNDEF_STBI_NO_FAILURE_STRINGS
#define STBI_NO_FAILURE_STRINGS
#endif
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
/* stb_image - v2.30 - public domain image loader - http://nothings.org/stb
                                  no warranty implied; use at your own risk

   Do this:
      #define STB_IMAGE_IMPLEMENTATION
   before you include this file in *one* C or C++ file to create the implementation.

   // i.e. it should look like this:
   #include ...
   #include ...
   #include ...
   #define STB_IMAGE_IMPLEMENTATION
   #include "stb_image.h"

   You can #define STBI_ASSERT(x) before the #include to avoid using assert.h.
   And #define STBI_MALLOC, STBI_REALLOC, and STBI_FREE to avoid using malloc,realloc,free


   QUICK NOTES:
      Primarily of interest to game developers and other people who can
          avoid problematic images and only need the trivial interface

      JPEG baseline & progressive (12 bpc/arithmetic not supported, same as stock IJG lib)
      PNG 1/2/4/8/16-bit-per-channel

      TGA (not sure what subset, if a subset)
      BMP non-1bpp, non-RLE
      PSD (composited view only, no extra channels, 8/16 bit-per-channel)

      GIF (*comp always reports as 4-channel)
      HDR (radiance rgbE format)
      PIC (Softimage PIC)
      PNM (PPM and PGM binary only)

      Animated GIF still needs a proper API, but here's one way to do it:
          http://gist.github.com/urraka/685d9a6340b26b830d49

      - decode from memory or through FILE (define STBI_NO_STDIO to remove code)
      - decode from arbitrary I/O callbacks
      - SIMD acceleration on x86/x64 (SSE2) and ARM (NEON)

   Full documentation under "DOCUMENTATION" below.


LICENSE

  See end of file for license information.

RECENT REVISION HISTORY:

      2.30  (2024-05-31) avoid erroneous gcc warning
      2.29  (2023-05-xx) optimizations
      2.28  (2023-01-29) many error fixes, security errors, just tons of stuff
      2.27  (2021-07-11) document stbi_info better, 16-bit PNM support, bug fixes
      2.26  (2020-07-13) many minor fixes
      2.25  (2020-02-02) fix warnings
      2.24  (2020-02-02) fix warnings; thread-local failure_reason and flip_vertically
      2.23  (2019-08-11) fix clang static analysis warning
      2.22  (2019-03-04) gif fixes, fix warnings
      2.21  (2019-02-25) fix typo in comment
      2.20  (2019-02-07) support utf8 filenames in Windows; fix warnings and platform ifdefs
      2.19  (2018-02-11) fix warning
      2.18  (2018-01-30) fix warnings
      2.17  (2018-01-29) bugfix, 1-bit BMP, 16-bitness query, fix warnings
      2.16  (2017-07-23) all functions have 16-bit variants; optimizations; bugfixes
      2.15  (2017-03-18) fix png-1,2,4; all Imagenet JPGs; no runtime SSE detection on GCC
      2.14  (2017-03-03) remove deprecated STBI_JPEG_OLD; fixes for Imagenet JPGs
      2.13  (2016-12-04) experimental 16-bit API, only for PNG so far; fixes
      2.12  (2016-04-02) fix typo in 2.11 PSD fix that caused crashes
      2.11  (2016-04-02) 16-bit PNGS; enable SSE2 in non-gcc x64
                         RGB-format JPEG; remove white matting in PSD;
                         allocate large structures on the stack;
                         correct channel count for PNG & BMP
      2.10  (2016-01-22) avoid warning introduced in 2.09
      2.09  (2016-01-16) 16-bit TGA; comments in PNM files; STBI_REALLOC_SIZED

   See end of file for full revision history.


 ============================    Contributors    =========================

 Image formats                          Extensions, features
    Sean Barrett (jpeg, png, bmp)          Jetro Lauha (stbi_info)
    Nicolas Schulz (hdr, psd)              Martin "SpartanJ" Golini (stbi_info)
    Jonathan Dummer (tga)                  James "moose2000" Brown (iPhone PNG)
    Jean-Marc Lienher (gif)                Ben "Disch" Wenger (io callbacks)
    Tom Seddon (pic)                       Omar Cornut (1/2/4-bit PNG)
    Thatcher Ulrich (psd)                  Nicolas Guillemot (vertical flip)
    Ken Miller (pgm, ppm)                  Richard Mitton (16-bit PSD)
    github:urraka (animated gif)           Junggon Kim (PNM comments)
    Christopher Forseth (animated gif)     Daniel Gibson (16-bit TGA)
                                           socks-the-fox (16-bit PNG)
                                           Jeremy Sawicki (handle all ImageNet JPGs)
 Optimizations & bugfixes                  Mikhail Morozov (1-bit BMP)
    Fabian "ryg" Giesen                    Anael Seghezzi (is-16-bit query)
    Arseny Kapoulkine                      Simon Breuss (16-bit PNM)
    John-Mark Allen
    Carmelo J Fdez-Aguera

 Bug & warning fixes
    Marc LeBlanc            David Woo          Guillaume George     Martins Mozeiko
    Christpher Lloyd        Jerry Jansson      Joseph Thomson       Blazej Dariusz Roszkowski
    Phil Jordan                                Dave Moore           Roy Eltham
    Hayaki Saito            Nathan Reed        Won Chun
    Luke Graham             Johan Duparc       Nick Verigakis       the Horde3D community
    Thomas Ruf              Ronny Chevalier                         github:rlyeh
    Janez Zemva             John Bartholomew   Michal Cichon        github:romigrou
    Jonathan Blow           Ken Hamada         Tero Hanninen        github:svdijk
    Eugene Golushkov        Laurent Gomila     Cort Stratton        github:snagar
    Aruelien Pocheville     Sergio Gonzalez    Thibault Reuille     github:Zelex
    Cass Everitt            Ryamond Barbiero                        github:grim210
    Paul Du Bois            Engin Manap        Aldo Culquicondor    github:sammyhw
    Philipp Wiesemann       Dale Weiler        Oriol Ferrer Mesia   github:phprus
    Josh Tobin              Neil Bickford      Matthew Gregan       github:poppolopoppo
    Julian Raschke          Gregory Mullen     Christian Floisand   github:darealshinji
    Baldur Karlsson         Kevin Schmidt      JR Smith             github:Michaelangel007
                            Brad Weinberger    Matvey Cherevko      github:mosra
    Luca Sas                Alexander Veselov  Zack Middleton       [reserved]
    Ryan C. Gordon          [reserved]                              [reserved]
                     DO NOT ADD YOUR NAME HERE

                     Jacko Dirks

  To add your name to the credits, pick a random blank space in the middle and fill it.
  80% of merge conflicts on stb PRs are due to people adding their name at the end
  of the credits.
*/

#ifndef STBI_INCLUDE_STB_IMAGE_H
#define STBI_INCLUDE_STB_IMAGE_H

// DOCUMENTATION
//
// Limitations:
//    - no 12-bit-per-channel JPEG
//    - no JPEGs with arithmetic coding
//    - GIF always returns *comp=4
//
// Basic usage (see HDR discussion below for HDR usage):
//    int x,y,n;
//    unsigned char *data = stbi_load(filename, &x, &y, &n, 0);
//    // ... process data if not NULL ...
//    // ... x = width, y = height, n = # 8-bit components per pixel ...
//    // ... replace '0' with '1'..'4' to force that many components per pixel
//    // ... but 'n' will always be the number that it would have been if you said 0
//    stbi_image_free(data);
//
// Standard parameters:
//    int *x                 -- outputs image width in pixels
//    int *y                 -- outputs image height in pixels
//    int *channels_in_file  -- outputs # of image components in image file
//    int desired_channels   -- if non-zero, # of image components requested in result
//
// The return value from an image loader is an 'unsigned char *' which points
// to the pixel data, or NULL on an allocation failure or if the image is
// corrupt or invalid. The pixel data consists of *y scanlines of *x pixels,
// with each pixel consisting of N interleaved 8-bit components; the first
// pixel pointed to is top-left-most in the image. There is no padding between
// image scanlines or between pixels, regardless of format. The number of
// components N is 'desired_channels' if desired_channels is non-zero, or
// *channels_in_file otherwise. If desired_channels is non-zero,
// *channels_in_file has the number of components that _would_ have been
// output otherwise. E.g. if you set desired_channels to 4, you will always
// get RGBA output, but you can check *channels_in_file to see if it's trivially
// opaque because e.g. there were only 3 channels in the source image.
//
// An output image with N components has the following components interleaved
// in this order in each pixel:
//
//     N=#comp     components
//       1           grey
//       2           grey, alpha
//       3           red, green, blue
//       4           red, green, blue, alpha
//
// If image loading fails for any reason, the return value will be NULL,
// and *x, *y, *channels_in_file will be unchanged. The function
// stbi_failure_reason() can be queried for an extremely brief, end-user
// unfriendly explanation of why the load failed. Define STBI_NO_FAILURE_STRINGS
// to avoid compiling these strings at all, and STBI_FAILURE_USERMSG to get slightly
// more user-friendly ones.
//
// Paletted PNG, BMP, GIF, and PIC images are automatically depalettized.
//
// To query the width, height and component count of an image without having to
// decode the full file, you can use the stbi_info family of functions:
//
//   int x,y,n,ok;
//   ok = stbi_info(filename, &x, &y, &n);
//   // returns ok=1 and sets x, y, n if image is a supported format,
//   // 0 otherwise.
//
// Note that stb_image pervasively uses ints in its public API for sizes,
// including sizes of memory buffers. This is now part of the API and thus
// hard to change without causing breakage. As a result, the various image
// loaders all have certain limits on image size; these differ somewhat
// by format but generally boil down to either just under 2GB or just under
// 1GB. When the decoded image would be larger than this, stb_image decoding
// will fail.
//
// Additionally, stb_image will reject image files that have any of their
// dimensions set to a larger value than the configurable STBI_MAX_DIMENSIONS,
// which defaults to 2**24 = 16777216 pixels. Due to the above memory limit,
// the only way to have an image with such dimensions load correctly
// is for it to have a rather extreme aspect ratio. Either way, the
// assumption here is that such larger images are likely to be malformed
// or malicious. If you do need to load an image with individual dimensions
// larger than that, and it still fits in the overall size limit, you can
// #define STBI_MAX_DIMENSIONS on your own to be something larger.
//
// ===========================================================================
//
// UNICODE:
//
//   If compiling for Windows and you wish to use Unicode filenames, compile
//   with
//       #define STBI_WINDOWS_UTF8
//   and pass utf8-encoded filenames. Call stbi_convert_wchar_to_utf8 to convert
//   Windows wchar_t filenames to utf8.
//
// ===========================================================================
//
// Philosophy
//
// stb libraries are designed with the following priorities:
//
//    1. easy to use
//    2. easy to maintain
//    3. good performance
//
// Sometimes I let "good performance" creep up in priority over "easy to maintain",
// and for best performance I may provide less-easy-to-use APIs that give higher
// performance, in addition to the easy-to-use ones. Nevertheless, it's important
// to keep in mind that from the standpoint of you, a client of this library,
// all you care about is #1 and #3, and stb libraries DO NOT emphasize #3 above all.
//
// Some secondary priorities arise directly from the first two, some of which
// provide more explicit reasons why performance can't be emphasized.
//
//    - Portable ("ease of use")
//    - Small source code footprint ("easy to maintain")
//    - No dependencies ("ease of use")
//
// ===========================================================================
//
// I/O callbacks
//
// I/O callbacks allow you to read from arbitrary sources, like packaged
// files or some other source. Data read from callbacks are processed
// through a small internal buffer (currently 128 bytes) to try to reduce
// overhead.
//
// The three functions you must define are "read" (reads some bytes of data),
// "skip" (skips some bytes of data), "eof" (reports if the stream is at the end).
//
// ===========================================================================
//
// SIMD support
//
// The JPEG decoder will try to automatically use SIMD kernels on x86 when
// supported by the compiler. For ARM Neon support, you must explicitly
// request it.
//
// (The old do-it-yourself SIMD API is no longer supported in the current
// code.)
//
// On x86, SSE2 will automatically be used when available based on a run-time
// test; if not, the generic C versions are used as a fall-back. On ARM targets,
// the typical path is to have separate builds for NEON and non-NEON devices
// (at least this is true for iOS and Android). Therefore, the NEON support is
// toggled by a build flag: define STBI_NEON to get NEON loops.
//
// If for some reason you do not want to use any of SIMD code, or if
// you have issues compiling it, you can disable it entirely by
// defining STBI_NO_SIMD.
//
// ===========================================================================
//
// HDR image support   (disable by defining STBI_NO_HDR)
//
// stb_image supports loading HDR images in general, and currently the Radiance
// .HDR file format specifically. You can still load any file through the existing
// interface; if you attempt to load an HDR file, it will be automatically remapped
// to LDR, assuming gamma 2.2 and an arbitrary scale factor defaulting to 1;
// both of these constants can be reconfigured through this interface:
//
//     stbi_hdr_to_ldr_gamma(2.2f);
//     stbi_hdr_to_ldr_scale(1.0f);
//
// (note, do not use _inverse_ constants; stbi_image will invert them
// appropriately).
//
// Additionally, there is a new, parallel interface for loading files as
// (linear) floats to preserve the full dynamic range:
//
//    float *data = stbi_loadf(filename, &x, &y, &n, 0);
//
// If you load LDR images through this interface, those images will
// be promoted to floating point values, run through the inverse of
// constants corresponding to the above:
//
//     stbi_ldr_to_hdr_scale(1.0f);
//     stbi_ldr_to_hdr_gamma(2.2f);
//
// Finally, given a filename (or an open file or memory block--see header
// file for details) containing image data, you can query for the "most
// appropriate" interface to use (that is, whether the image is HDR or
// not), using:
//
//     stbi_is_hdr(char *filename);
//
// ===========================================================================
//
// iPhone PNG support:
//
// We optionally support converting iPhone-formatted PNGs (which store
// premultiplied BGRA) back to RGB, even though they're internally encoded
// differently. To enable this conversion, call
// stbi_convert_iphone_png_to_rgb(1).
//
// Call stbi_set_unpremultiply_on_load(1) as well to force a divide per
// pixel to remove any premultiplied alpha *only* if the image file explicitly
// says there's premultiplied data (currently only happens in iPhone images,
// and only if iPhone convert-to-rgb processing is on).
//
// ===========================================================================
//
// ADDITIONAL CONFIGURATION
//
//  - You can suppress implementation of any of the decoders to reduce
//    your code footprint by #defining one or more of the following
//    symbols before creating the implementation.
//
//        STBI_NO_JPEG
//        STBI_NO_PNG
//        STBI_NO_BMP
//        STBI_NO_PSD
//        STBI_NO_TGA
//        STBI_NO_GIF
//        STBI_NO_HDR
//        STBI_NO_PIC
//        STBI_NO_PNM   (.ppm and .pgm)
//
//  - You can request *only* certain decoders and suppress all other ones
//    (this will be more forward-compatible, as addition of new decoders
//    doesn't require you to disable them explicitly):
//
//        STBI_ONLY_JPEG
//        STBI_ONLY_PNG
//        STBI_ONLY_BMP
//        STBI_ONLY_PSD
//        STBI_ONLY_TGA
//        STBI_ONLY_GIF
//        STBI_ONLY_HDR
//        STBI_ONLY_PIC
//        STBI_ONLY_PNM   (.ppm and .pgm)
//
//   - If you use STBI_NO_PNG (or _ONLY_ without PNG), and you still
//     want the zlib decoder to be available, #define STBI_SUPPORT_ZLIB
//
//  - If you define STBI_MAX_DIMENSIONS, stb_image will reject images greater
//    than that size (in either width or height) without further processing.
//    This is to let programs in the wild set an upper bound to prevent
//    denial-of-service attacks on untrusted data, as one could generate a
//    valid image of gigantic dimensions and force stb_image to allocate a
//    huge block of memory and spend disproportionate time decoding it. By
//    default this is set to (1 << 24), which is 16777216, but that's still
//    very big.

#ifndef STBI_NO_STDIO
#include <stdio.h>
#endif // STBI_NO_STDIO

#define STBI_VERSION 1

enum
{
   STBI_default = 0, // only used for desired_channels

   STBI_grey       = 1,
   STBI_grey_alpha = 2,
   STBI_rgb        = 3,
   STBI_rgb_alpha  = 4
};

#include <stdlib.h>
typedef unsigned char stbi_uc;
typedef unsigned short stbi_us;

#ifdef __cplusplus
extern "C" {
#endif

#ifndef STBIDEF
#ifdef STB_IMAGE_STATIC
#define STBIDEF static
#else
#define STBIDEF extern
#endif
#endif

//////////////////////////////////////////////////////////////////////////////
//
// PRIMARY API - works on images of any type
//

//
// load image by filename, open file, or memory buffer
//

typedef struct
{
   int      (*read)  (void *user,char *data,int size);   // fill 'data' with 'size' bytes.  return number of bytes actually read
   void     (*skip)  (void *user,int n);                 // skip the next 'n' bytes, or 'unget' the last -n bytes if negative
   int      (*eof)   (void *user);                       // returns nonzero if we are at end of file/data
} stbi_io_callbacks;

////////////////////////////////////
//
// 8-bits-per-channel interface
//

STBIDEF stbi_uc *stbi_load_from_memory   (stbi_uc           const *buffer, int len   , int *x, int *y, int *channels_in_file, int desired_channels);
STBIDEF stbi_uc *stbi_load_from_callbacks(stbi_io_callbacks const *clbk  , void *user, int *x, int *y, int *channels_in_file, int desired_channels);

#ifndef STBI_NO_STDIO
STBIDEF stbi_uc *stbi_load            (char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);
STBIDEF stbi_uc *stbi_load_from_file  (FILE *f, int *x, int *y, int *channels_in_file, int desired_channels);
// for stbi_load_from_file, file pointer is left pointing immediately after image
#endif

#ifndef STBI_NO_GIF
STBIDEF stbi_uc *stbi_load_gif_from_memory(stbi_uc const *buffer, int len, int **delays, int *x, int *y, int *z, int *comp, int req_comp);
#endif

#ifdef STBI_WINDOWS_UTF8
STBIDEF int stbi_convert_wchar_to_utf8(char *buffer, size_t bufferlen, const wchar_t* input);
#endif

////////////////////////////////////
//
// 16-bits-per-channel interface
//

STBIDEF stbi_us *stbi_load_16_from_memory   (stbi_uc const *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels);
STBIDEF stbi_us *stbi_load_16_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *channels_in_file, int desired_channels);

#ifndef STBI_NO_STDIO
STBIDEF stbi_us *stbi_load_16          (char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);
STBIDEF stbi_us *stbi_load_from_file_16(FILE *f, int *x, int *y, int *channels_in_file, int desired_channels);
#endif

////////////////////////////////////
//
// float-per-channel interface
//
#ifndef STBI_NO_LINEAR
   STBIDEF float *stbi_loadf_from_memory     (stbi_uc const *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels);
   STBIDEF float *stbi_loadf_from_callbacks  (stbi_io_callbacks const *clbk, void *user, int *x, int *y,  int *channels_in_file, int desired_channels);

   #ifndef STBI_NO_STDIO
   STBIDEF float *stbi_loadf            (char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);
   STBIDEF float *stbi_loadf_from_file  (FILE *f, int *x, int *y, int *channels_in_file, int desired_channels);
   #endif
#endif

#ifndef STBI_NO_HDR
   STBIDEF void   stbi_hdr_to_ldr_gamma(float gamma);
   STBIDEF void   stbi_hdr_to_ldr_scale(float scale);
#endif // STBI_NO_HDR

#ifndef STBI_NO_LINEAR
   STBIDEF void   stbi_ldr_to_hdr_gamma(float gamma);
   STBIDEF void   stbi_ldr_to_hdr_scale(float scale);
#endif // STBI_NO_LINEAR

// stbi_is_hdr is always defined, but always returns false if STBI_NO_HDR
STBIDEF int    stbi_is_hdr_from_callbacks(stbi_io_callbacks const *clbk, void *user);
STBIDEF int    stbi_is_hdr_from_memory(stbi_uc const *buffer, int len);
#ifndef STBI_NO_STDIO
STBIDEF int      stbi_is_hdr          (char const *filename);
STBIDEF int      stbi_is_hdr_from_file(FILE *f);
#endif // STBI_NO_STDIO


// get a VERY brief reason for failure
// on most compilers (and ALL modern mainstream compilers) this is threadsafe
STBIDEF const char *stbi_failure_reason  (void);

// free the loaded image -- this is just free()
STBIDEF void     stbi_image_free      (void *retval_from_stbi_load);

// get image dimensions & components without fully decoding
STBIDEF int      stbi_info_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *comp);
STBIDEF int      stbi_info_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *comp);
STBIDEF int      stbi_is_16_bit_from_memory(stbi_uc const *buffer, int len);
STBIDEF int      stbi_is_16_bit_from_callbacks(stbi_io_callbacks const *clbk, void *user);

#ifndef STBI_NO_STDIO
STBIDEF int      stbi_info               (char const *filename,     int *x, int *y, int *comp);
STBIDEF int      stbi_info_from_file     (FILE *f,                  int *x, int *y, int *comp);
STBIDEF int      stbi_is_16_bit          (char const *filename);
STBIDEF int      stbi_is_16_bit_from_file(FILE *f);
#endif



// for image formats that explicitly notate that they have premultiplied alpha,
// we just return the colors as stored in the file. set this flag to force
// unpremultiplication. results are undefined if the unpremultiply overflow.
STBIDEF void stbi_set_unpremultiply_on_load(int flag_true_if_should_unpremultiply);

// indicate whether we should process iphone images back to canonical format,
// or just pass them through "as-is"
STBIDEF void stbi_convert_iphone_png_to_rgb(int flag_true_if_should_convert);

// flip the image vertically, so the first pixel in the output array is the bottom left
STBIDEF void stbi_set_flip_vertically_on_load(int flag_true_if_should_flip);

// as above, but only applies to images loaded on the thread that calls the function
// this function is only available if your compiler supports thread-local variables;
// calling it will fail to link if your compiler doesn't
STBIDEF void stbi_set_unpremultiply_on_load_thread(int flag_true_if_should_unpremultiply);
STBIDEF void stbi_convert_iphone_png_to_rgb_thread(int flag_true_if_should_convert);
STBIDEF void stbi_set_flip_vertically_on_load_thread(int flag_true_if_should_flip);

// ZLIB client - used by PNG, available for other purposes

STBIDEF char *stbi_zlib_decode_malloc_guesssize(const char *buffer, int len, int initial_size, int *outlen);
STBIDEF char *stbi_zlib_decode_malloc_guesssize_headerflag(const char *buffer, int len, int initial_size, int *outlen, int parse_header);
STBIDEF char *stbi_zlib_decode_malloc(const char *buffer, int len, int *outlen);
STBIDEF int   stbi_zlib_decode_buffer(char *obuffer, int olen, const char *ibuffer, int ilen);

STBIDEF char *stbi_zlib_decode_noheader_malloc(const char *buffer, int len, int *outlen);
STBIDEF int   stbi_zlib_decode_noheader_buffer(char *obuffer, int olen, const char *ibuffer, int ilen);


#ifdef __cplusplus
}
#endif

//
//
////   end header file   /////////////////////////////////////////////////////
#endif // STBI_INCLUDE_STB_IMAGE_H

#ifdef STB_IMAGE_IMPLEMENTATION

#if defined(STBI_ONLY_JPEG) || defined(STBI_ONLY_PNG) || defined(STBI_ONLY_BMP) \
  || defined(STBI_ONLY_TGA) || defined(STBI_ONLY_GIF) || defined(STBI_ONLY_PSD) \
  || defined(STBI_ONLY_HDR) || defined(STBI_ONLY_PIC) || defined(STBI_ONLY_PNM) \
  || defined(STBI_ONLY_ZLIB)
   #ifndef STBI_ONLY_JPEG
   #define STBI_NO_JPEG
   #endif
   #ifndef STBI_ONLY_PNG
   #define STBI_NO_PNG
   #endif
   #ifndef STBI_ONLY_BMP
   #define STBI_NO_BMP
   #endif
   #ifndef STBI_ONLY_PSD
   #define STBI_NO_PSD
   #endif
   #ifndef STBI_ONLY_TGA
   #define STBI_NO_TGA
   #endif
   #ifndef STBI_ONLY_GIF
   #define STBI_NO_GIF
   #endif
   #ifndef STBI_ONLY_HDR
   #define STBI_NO_HDR
   #endif
   #ifndef STBI_ONLY_PIC
   #define STBI_NO_PIC
   #endif
   #ifndef STBI_ONLY_PNM
   #define STBI_NO_PNM
   #endif
#endif

#if defined(STBI_NO_PNG) && !defined(STBI_SUPPORT_ZLIB) && !defined(STBI_NO_ZLIB)
#define STBI_NO_ZLIB
#endif


#include <stdarg.h>
#include <stddef.h> // ptrdiff_t on osx
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#if !defined(STBI_NO_LINEAR) || !defined(STBI_NO_HDR)
#include <math.h>  // ldexp, pow
#endif

#ifndef STBI_NO_STDIO
#include <stdio.h>
#endif

#ifndef STBI_ASSERT
#include <assert.h>
#define STBI_ASSERT(x) assert(x)
#endif

#ifdef __cplusplus
#define STBI_EXTERN extern "C"
#else
#define STBI_EXTERN extern
#endif


#ifndef _MSC_VER
   #ifdef __cplusplus
   #define stbi_inline inline
   #else
   #define stbi_inline
   #endif
#else
   #define stbi_inline __forceinline
#endif

#ifndef STBI_NO_THREAD_LOCALS
   #if defined(__cplusplus) &&  __cplusplus >= 201103L
      #define STBI_THREAD_LOCAL       thread_local
   #elif defined(__GNUC__) && __GNUC__ < 5
      #define STBI_THREAD_LOCAL       __thread
   #elif defined(_MSC_VER)
      #define STBI_THREAD_LOCAL       __declspec(thread)
   #elif defined (__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
      #define STBI_THREAD_LOCAL       _Thread_local
   #endif

   #ifndef STBI_THREAD_LOCAL
      #if defined(__GNUC__)
        #define STBI_THREAD_LOCAL       __thread
      #endif
   #endif
#endif

#if defined(_MSC_VER) || defined(__SYMBIAN32__)
typedef unsigned short stbi__uint16;
typedef   signed short stbi__int16;
typedef unsigned int   stbi__uint32;
typedef   signed int   stbi__int32;
#else
#include <stdint.h>
typedef uint16_t stbi__uint16;
typedef int16_t  stbi__int16;
typedef uint32_t stbi__uint32;
typedef int32_t  stbi__int32;
#endif

// should produce compiler error if size is wrong
typedef unsigned char validate_uint32[sizeof(stbi__uint32)==4 ? 1 : -1];

#ifdef _MSC_VER
#define STBI_NOTUSED(v)  (void)(v)
#else
#define STBI_NOTUSED(v)  (void)sizeof(v)
#endif

#ifdef _MSC_VER
#define STBI_HAS_LROTL
#endif

#ifdef STBI_HAS_LROTL
   #define stbi_lrot(x,y)  _lrotl(x,y)
#else
   #define stbi_lrot(x,y)  (((x) << (y)) | ((x) >> (-(y) & 31)))
#endif

#if defined(STBI_MALLOC) && defined(STBI_FREE) && (defined(STBI_REALLOC) || defined(STBI_REALLOC_SIZED))
// ok
#elif !defined(STBI_MALLOC) && !defined(STBI_FREE) && !defined(STBI_REALLOC) && !defined(STBI_REALLOC_SIZED)
// ok
#else
#error "Must define all or none of STBI_MALLOC, STBI_FREE, and STBI_REALLOC (or STBI_REALLOC_SIZED)."
#endif

#ifndef STBI_MALLOC
#define STBI_MALLOC(sz)           malloc(sz)
#define STBI_REALLOC(p,newsz)     realloc(p,newsz)
#define STBI_FREE(p)              free(p)
#endif

#ifndef STBI_REALLOC_SIZED
#define STBI_REALLOC_SIZED(p,oldsz,newsz) STBI_REALLOC(p,newsz)
#endif

// x86/x64 detection
#if defined(__x86_64__) || defined(_M_X64)
#define STBI__X64_TARGET
#elif defined(__i386) || defined(_M_IX86)
#define STBI__X86_TARGET
#endif

#if defined(__GNUC__) && defined(STBI__X86_TARGET) && !defined(__SSE2__) && !defined(STBI_NO_SIMD)
// gcc doesn't support sse2 intrinsics unless you compile with -msse2,
// which in turn means it gets to use SSE2 everywhere. This is unfortunate,
// but previous attempts to provide the SSE2 functions with runtime
// detection caused numerous issues. The way architecture extensions are
// exposed in GCC/Clang is, sadly, not really suited for one-file libs.
// New behavior: if compiled with -msse2, we use SSE2 without any
// detection; if not, we don't use it at all.
#define STBI_NO_SIMD
#endif

#if defined(__MINGW32__) && defined(STBI__X86_TARGET) && !defined(STBI_MINGW_ENABLE_SSE2) && !defined(STBI_NO_SIMD)
// Note that __MINGW32__ doesn't actually mean 32-bit, so we have to avoid STBI__X64_TARGET
//
// 32-bit MinGW wants ESP to be 16-byte aligned, but this is not in the
// Windows ABI and VC++ as well as Windows DLLs don't maintain that invariant.
// As a result, enabling SSE2 on 32-bit MinGW is dangerous when not
// simultaneously enabling "-mstackrealign".
//
// See https://github.com/nothings/stb/issues/81 for more information.
//
// So default to no SSE2 on 32-bit MinGW. If you've read this far and added
// -mstackrealign to your build settings, feel free to #define STBI_MINGW_ENABLE_SSE2.
#define STBI_NO_SIMD
#endif

#if !defined(STBI_NO_SIMD) && (defined(STBI__X86_TARGET) || defined(STBI__X64_TARGET))
#define STBI_SSE2
#include <emmintrin.h>

#ifdef _MSC_VER

#if _MSC_VER >= 1400  // not VC6
#include <intrin.h> // __cpuid
static int stbi__cpuid3(void)
{
   int info[4];
   __cpuid(info,1);
   return info[3];
}
#else
static int stbi__cpuid3(void)
{
   int res;
   __asm {
      mov  eax,1
      cpuid
      mov  res,edx
   }
   return res;
}
#endif

#define STBI_SIMD_ALIGN(type, name) __declspec(align(16)) type name

#if !defined(STBI_NO_JPEG) && defined(STBI_SSE2)
static int stbi__sse2_available(void)
{
   int info3 = stbi__cpuid3();
   return ((info3 >> 26) & 1) != 0;
}
#endif

#else // assume GCC-style if not VC++
#define STBI_SIMD_ALIGN(type, name) type name __attribute__((aligned(16)))

#if !defined(STBI_NO_JPEG) && defined(STBI_SSE2)
static int stbi__sse2_available(void)
{
   // If we're even attempting to compile this on GCC/Clang, that means
   // -msse2 is on, which means the compiler is allowed to use SSE2
   // instructions at will, and so are we.
   return 1;
}
#endif

#endif
#endif

// ARM NEON
#if defined(STBI_NO_SIMD) && defined(STBI_NEON)
#undef STBI_NEON
#endif

#ifdef STBI_NEON
#include <arm_neon.h>
#ifdef _MSC_VER
#define STBI_SIMD_ALIGN(type, name) __declspec(align(16)) type name
#else
#define STBI_SIMD_ALIGN(type, name) type name __attribute__((aligned(16)))
#endif
#endif

#ifndef STBI_SIMD_ALIGN
#define STBI_SIMD_ALIGN(type, name) type name
#endif

#ifndef STBI_MAX_DIMENSIONS
#define STBI_MAX_DIMENSIONS (1 << 24)
#endif

///////////////////////////////////////////////
//
//  stbi__context struct and start_xxx functions

// stbi__context structure is our basic context used by all images, so it
// contains all the IO context, plus some basic image information
typedef struct
{
   stbi__uint32 img_x, img_y;
   int img_n, img_out_n;

   stbi_io_callbacks io;
   void *io_user_data;

   int read_from_callbacks;
   int buflen;
   stbi_uc buffer_start[128];
   int callback_already_read;

   stbi_uc *img_buffer, *img_buffer_end;
   stbi_uc *img_buffer_original, *img_buffer_original_end;
} stbi__context;


static void stbi__refill_buffer(stbi__context *s);

// initialize a memory-decode context
static void stbi__start_mem(stbi__context *s, stbi_uc const *buffer, int len)
{
   s->io.read = NULL;
   s->read_from_callbacks = 0;
   s->callback_already_read = 0;
   s->img_buffer = s->img_buffer_original = (stbi_uc *) buffer;
   s->img_buffer_end = s->img_buffer_original_end = (stbi_uc *) buffer+len;
}

// initialize a callback-based context
static void stbi__start_callbacks(stbi__context *s, stbi_io_callbacks *c, void *user)
{
   s->io = *c;
   s->io_user_data = user;
   s->buflen = sizeof(s->buffer_start);
   s->read_from_callbacks = 1;
   s->callback_already_read = 0;
   s->img_buffer = s->img_buffer_original = s->buffer_start;
   stbi__refill_buffer(s);
   s->img_buffer_original_end = s->img_buffer_end;
}

#ifndef STBI_NO_STDIO

static int stbi__stdio_read(void *user, char *data, int size)
{
   return (int) fread(data,1,size,(FILE*) user);
}

static void stbi__stdio_skip(void *user, int n)
{
   int ch;
   fseek((FILE*) user, n, SEEK_CUR);
   ch = fgetc((FILE*) user);  /* have to read a byte to reset feof()'s flag */
   if (ch != EOF) {
      ungetc(ch, (FILE *) user);  /* push byte back onto stream if valid. */
   }
}

static int stbi__stdio_eof(void *user)
{
   return feof((FILE*) user) || ferror((FILE *) user);
}

static stbi_io_callbacks stbi__stdio_callbacks =
{
   stbi__stdio_read,
   stbi__stdio_skip,
   stbi__stdio_eof,
};

static void stbi__start_file(stbi__context *s, FILE *f)
{
   stbi__start_callbacks(s, &stbi__stdio_callbacks, (void *) f);
}

//static void stop_file(stbi__context *s) { }

#endif // !STBI_NO_STDIO

static void stbi__rewind(stbi__context *s)
{
   // conceptually rewind SHOULD rewind to the beginning of the stream,
   // but we just rewind to the beginning of the initial buffer, because
   // we only use it after doing 'test', which only ever looks at at most 92 bytes
   s->img_buffer = s->img_buffer_original;
   s->img_buffer_end = s->img_buffer_original_end;
}

enum
{
   STBI_ORDER_RGB,
   STBI_ORDER_BGR
};

typedef struct
{
   int bits_per_channel;
   int num_channels;
   int channel_order;
} stbi__result_info;

#ifndef STBI_NO_JPEG
static int      stbi__jpeg_test(stbi__context *s);
static void    *stbi__jpeg_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);
static int      stbi__jpeg_info(stbi__context *s, int *x, int *y, int *comp);
#endif

#ifndef STBI_NO_PNG
static int      stbi__png_test(stbi__context *s);
static void    *stbi__png_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);
static int      stbi__png_info(stbi__context *s, int *x, int *y, int *comp);
static int      stbi__png_is16(stbi__context *s);
#endif

#ifndef STBI_NO_BMP
static int      stbi__bmp_test(stbi__context *s);
static void    *stbi__bmp_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);
static int      stbi__bmp_info(stbi__context *s, int *x, int *y, int *comp);
#endif

#ifndef STBI_NO_TGA
static int      stbi__tga_test(stbi__context *s);
static void    *stbi__tga_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);
static int      stbi__tga_info(stbi__context *s, int *x, int *y, int *comp);
#endif

#ifndef STBI_NO_PSD
static int      stbi__psd_test(stbi__context *s);
static void    *stbi__psd_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri, int bpc);
static int      stbi__psd_info(stbi__context *s, int *x, int *y, int *comp);
static int      stbi__psd_is16(stbi__context *s);
#endif

#ifndef STBI_NO_HDR
static int      stbi__hdr_test(stbi__context *s);
static float   *stbi__hdr_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);
static int      stbi__hdr_info(stbi__context *s, int *x, int *y, int *comp);
#endif

#ifndef STBI_NO_PIC
static int      stbi__pic_test(stbi__context *s);
static void    *stbi__pic_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);
static int      stbi__pic_info(stbi__context *s, int *x, int *y, int *comp);
#endif

#ifndef STBI_NO_GIF
static int      stbi__gif_test(stbi__context *s);
static void    *stbi__gif_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);
static void    *stbi__load_gif_main(stbi__context *s, int **delays, int *x, int *y, int *z, int *comp, int req_comp);
static int      stbi__gif_info(stbi__context *s, int *x, int *y, int *comp);
#endif

#ifndef STBI_NO_PNM
static int      stbi__pnm_test(stbi__context *s);
static void    *stbi__pnm_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri);
static int      stbi__pnm_info(stbi__context *s, int *x, int *y, int *comp);
static int      stbi__pnm_is16(stbi__context *s);
#endif

static
#ifdef STBI_THREAD_LOCAL
STBI_THREAD_LOCAL
#endif
const char *stbi__g_failure_reason;

STBIDEF const char *stbi_failure_reason(void)
{
   return stbi__g_failure_reason;
}

#ifndef STBI_NO_FAILURE_STRINGS
static int stbi__err(const char *str)
{
   stbi__g_failure_reason = str;
   return 0;
}
#endif

static void *stbi__malloc(size_t size)
{
    return STBI_MALLOC(size);
}

// stb_image uses ints pervasively, including for offset calculations.
// therefore the largest decoded image size we can support with the
// current code, even on 64-bit targets, is INT_MAX. this is not a
// significant limitation for the intended use case.
//
// we do, however, need to make sure our size calculations don't
// overflow. hence a few helper functions for size calculations that
// multiply integers together, making sure that they're non-negative
// and no overflow occurs.

// return 1 if the sum is valid, 0 on overflow.
// negative terms are considered invalid.
static int stbi__addsizes_valid(int a, int b)
{
   if (b < 0) return 0;
   // now 0 <= b <= INT_MAX, hence also
   // 0 <= INT_MAX - b <= INTMAX.
   // And "a + b <= INT_MAX" (which might overflow) is the
   // same as a <= INT_MAX - b (no overflow)
   return a <= INT_MAX - b;
}

// returns 1 if the product is valid, 0 on overflow.
// negative factors are considered invalid.
static int stbi__mul2sizes_valid(int a, int b)
{
   if (a < 0 || b < 0) return 0;
   if (b == 0) return 1; // mul-by-0 is always safe
   // portable way to check for no overflows in a*b
   return a <= INT_MAX/b;
}

#if !defined(STBI_NO_JPEG) || !defined(STBI_NO_PNG) || !defined(STBI_NO_TGA) || !defined(STBI_NO_HDR)
// returns 1 if "a*b + add" has no negative terms/factors and doesn't overflow
static int stbi__mad2sizes_valid(int a, int b, int add)
{
   return stbi__mul2sizes_valid(a, b) && stbi__addsizes_valid(a*b, add);
}
#endif

// returns 1 if "a*b*c + add" has no negative terms/factors and doesn't overflow
static int stbi__mad3sizes_valid(int a, int b, int c, int add)
{
   return stbi__mul2sizes_valid(a, b) && stbi__mul2sizes_valid(a*b, c) &&
      stbi__addsizes_valid(a*b*c, add);
}

// returns 1 if "a*b*c*d + add" has no negative terms/factors and doesn't overflow
#if !defined(STBI_NO_LINEAR) || !defined(STBI_NO_HDR) || !defined(STBI_NO_PNM)
static int stbi__mad4sizes_valid(int a, int b, int c, int d, int add)
{
   return stbi__mul2sizes_valid(a, b) && stbi__mul2sizes_valid(a*b, c) &&
      stbi__mul2sizes_valid(a*b*c, d) && stbi__addsizes_valid(a*b*c*d, add);
}
#endif

#if !defined(STBI_NO_JPEG) || !defined(STBI_NO_PNG) || !defined(STBI_NO_TGA) || !defined(STBI_NO_HDR)
// mallocs with size overflow checking
static void *stbi__malloc_mad2(int a, int b, int add)
{
   if (!stbi__mad2sizes_valid(a, b, add)) return NULL;
   return stbi__malloc(a*b + add);
}
#endif

static void *stbi__malloc_mad3(int a, int b, int c, int add)
{
   if (!stbi__mad3sizes_valid(a, b, c, add)) return NULL;
   return stbi__malloc(a*b*c + add);
}

#if !defined(STBI_NO_LINEAR) || !defined(STBI_NO_HDR) || !defined(STBI_NO_PNM)
static void *stbi__malloc_mad4(int a, int b, int c, int d, int add)
{
   if (!stbi__mad4sizes_valid(a, b, c, d, add)) return NULL;
   return stbi__malloc(a*b*c*d + add);
}
#endif

// returns 1 if the sum of two signed ints is valid (between -2^31 and 2^31-1 inclusive), 0 on overflow.
static int stbi__addints_valid(int a, int b)
{
   if ((a >= 0) != (b >= 0)) return 1; // a and b have different signs, so no overflow
   if (a < 0 && b < 0) return a >= INT_MIN - b; // same as a + b >= INT_MIN; INT_MIN - b cannot overflow since b < 0.
   return a <= INT_MAX - b;
}

// returns 1 if the product of two ints fits in a signed short, 0 on overflow.
static int stbi__mul2shorts_valid(int a, int b)
{
   if (b == 0 || b == -1) return 1; // multiplication by 0 is always 0; check for -1 so SHRT_MIN/b doesn't overflow
   if ((a >= 0) == (b >= 0)) return a <= SHRT_MAX/b; // product is positive, so similar to mul2sizes_valid
   if (b < 0) return a <= SHRT_MIN / b; // same as a * b >= SHRT_MIN
   return a >= SHRT_MIN / b;
}

// stbi__err - error
// stbi__errpf - error returning pointer to float
// stbi__errpuc - error returning pointer to unsigned char

#ifdef STBI_NO_FAILURE_STRINGS
   #define stbi__err(x,y)  0
#elif defined(STBI_FAILURE_USERMSG)
   #define stbi__err(x,y)  stbi__err(y)
#else
   #define stbi__err(x,y)  stbi__err(x)
#endif

#define stbi__errpf(x,y)   ((float *)(size_t) (stbi__err(x,y)?NULL:NULL))
#define stbi__errpuc(x,y)  ((unsigned char *)(size_t) (stbi__err(x,y)?NULL:NULL))

STBIDEF void stbi_image_free(void *retval_from_stbi_load)
{
   STBI_FREE(retval_from_stbi_load);
}

#ifndef STBI_NO_LINEAR
static float   *stbi__ldr_to_hdr(stbi_uc *data, int x, int y, int comp);
#endif

#ifndef STBI_NO_HDR
static stbi_uc *stbi__hdr_to_ldr(float   *data, int x, int y, int comp);
#endif

static int stbi__vertically_flip_on_load_global = 0;

STBIDEF void stbi_set_flip_vertically_on_load(int flag_true_if_should_flip)
{
   stbi__vertically_flip_on_load_global = flag_true_if_should_flip;
}

#ifndef STBI_THREAD_LOCAL
#define stbi__vertically_flip_on_load  stbi__vertically_flip_on_load_global
#else
static STBI_THREAD_LOCAL int stbi__vertically_flip_on_load_local, stbi__vertically_flip_on_load_set;

STBIDEF void stbi_set_flip_vertically_on_load_thread(int flag_true_if_should_flip)
{
   stbi__vertically_flip_on_load_local = flag_true_if_should_flip;
   stbi__vertically_flip_on_load_set = 1;
}

#define stbi__vertically_flip_on_load  (stbi__vertically_flip_on_load_set       \
                                         ? stbi__vertically_flip_on_load_local  \
                                         : stbi__vertically_flip_on_load_global)
#endif // STBI_THREAD_LOCAL

static void *stbi__load_main(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri, int bpc)
{
   memset(ri, 0, sizeof(*ri)); // make sure it's initialized if we add new fields
   ri->bits_per_channel = 8; // default is 8 so most paths don't have to be changed
   ri->channel_order = STBI_ORDER_RGB; // all current input & output are this, but this is here so we can add BGR order
   ri->num_channels = 0;

   // test the formats with a very explicit header first (at least a FOURCC
   // or distinctive magic number first)
   #ifndef STBI_NO_PNG
   if (stbi__png_test(s))  return stbi__png_load(s,x,y,comp,req_comp, ri);
   #endif
   #ifndef STBI_NO_BMP
   if (stbi__bmp_test(s))  return stbi__bmp_load(s,x,y,comp,req_comp, ri);
   #endif
   #ifndef STBI_NO_GIF
   if (stbi__gif_test(s))  return stbi__gif_load(s,x,y,comp,req_comp, ri);
   #endif
   #ifndef STBI_NO_PSD
   if (stbi__psd_test(s))  return stbi__psd_load(s,x,y,comp,req_comp, ri, bpc);
   #else
   STBI_NOTUSED(bpc);
   #endif
   #ifndef STBI_NO_PIC
   if (stbi__pic_test(s))  return stbi__pic_load(s,x,y,comp,req_comp, ri);
   #endif

   // then the formats that can end up attempting to load with just 1 or 2
   // bytes matching expectations; these are prone to false positives, so
   // try them later
   #ifndef STBI_NO_JPEG
   if (stbi__jpeg_test(s)) return stbi__jpeg_load(s,x,y,comp,req_comp, ri);
   #endif
   #ifndef STBI_NO_PNM
   if (stbi__pnm_test(s))  return stbi__pnm_load(s,x,y,comp,req_comp, ri);
   #endif

   #ifndef STBI_NO_HDR
   if (stbi__hdr_test(s)) {
      float *hdr = stbi__hdr_load(s, x,y,comp,req_comp, ri);
      return stbi__hdr_to_ldr(hdr, *x, *y, req_comp ? req_comp : *comp);
   }
   #endif

   #ifndef STBI_NO_TGA
   // test tga last because it's a crappy test!
   if (stbi__tga_test(s))
      return stbi__tga_load(s,x,y,comp,req_comp, ri);
   #endif

   return stbi__errpuc("unknown image type", "Image not of any known type, or corrupt");
}

static stbi_uc *stbi__convert_16_to_8(stbi__uint16 *orig, int w, int h, int channels)
{
   int i;
   int img_len = w * h * channels;
   stbi_uc *reduced;

   reduced = (stbi_uc *) stbi__malloc(img_len);
   if (reduced == NULL) return stbi__errpuc("outofmem", "Out of memory");

   for (i = 0; i < img_len; ++i)
      reduced[i] = (stbi_uc)((orig[i] >> 8) & 0xFF); // top half of each byte is sufficient approx of 16->8 bit scaling

   STBI_FREE(orig);
   return reduced;
}

static stbi__uint16 *stbi__convert_8_to_16(stbi_uc *orig, int w, int h, int channels)
{
   int i;
   int img_len = w * h * channels;
   stbi__uint16 *enlarged;

   enlarged = (stbi__uint16 *) stbi__malloc(img_len*2);
   if (enlarged == NULL) return (stbi__uint16 *) stbi__errpuc("outofmem", "Out of memory");

   for (i = 0; i < img_len; ++i)
      enlarged[i] = (stbi__uint16)((orig[i] << 8) + orig[i]); // replicate to high and low byte, maps 0->0, 255->0xffff

   STBI_FREE(orig);
   return enlarged;
}

static void stbi__vertical_flip(void *image, int w, int h, int bytes_per_pixel)
{
   int row;
   size_t bytes_per_row = (size_t)w * bytes_per_pixel;
   stbi_uc temp[2048];
   stbi_uc *bytes = (stbi_uc *)image;

   for (row = 0; row < (h>>1); row++) {
      stbi_uc *row0 = bytes + row*bytes_per_row;
      stbi_uc *row1 = bytes + (h - row - 1)*bytes_per_row;
      // swap row0 with row1
      size_t bytes_left = bytes_per_row;
      while (bytes_left) {
         size_t bytes_copy = (bytes_left < sizeof(temp)) ? bytes_left : sizeof(temp);
         memcpy(temp, row0, bytes_copy);
         memcpy(row0, row1, bytes_copy);
         memcpy(row1, temp, bytes_copy);
         row0 += bytes_copy;
         row1 += bytes_copy;
         bytes_left -= bytes_copy;
      }
   }
}

#ifndef STBI_NO_GIF
static void stbi__vertical_flip_slices(void *image, int w, int h, int z, int bytes_per_pixel)
{
   int slice;
   int slice_size = w * h * bytes_per_pixel;

   stbi_uc *bytes = (stbi_uc *)image;
   for (slice = 0; slice < z; ++slice) {
      stbi__vertical_flip(bytes, w, h, bytes_per_pixel);
      bytes += slice_size;
   }
}
#endif

static unsigned char *stbi__load_and_postprocess_8bit(stbi__context *s, int *x, int *y, int *comp, int req_comp)
{
   stbi__result_info ri;
   void *result = stbi__load_main(s, x, y, comp, req_comp, &ri, 8);

   if (result == NULL)
      return NULL;

   // it is the responsibility of the loaders to make sure we get either 8 or 16 bit.
   STBI_ASSERT(ri.bits_per_channel == 8 || ri.bits_per_channel == 16);

   if (ri.bits_per_channel != 8) {
      result = stbi__convert_16_to_8((stbi__uint16 *) result, *x, *y, req_comp == 0 ? *comp : req_comp);
      ri.bits_per_channel = 8;
   }

   // @TODO: move stbi__convert_format to here

   if (stbi__vertically_flip_on_load) {
      int channels = req_comp ? req_comp : *comp;
      stbi__vertical_flip(result, *x, *y, channels * sizeof(stbi_uc));
   }

   return (unsigned char *) result;
}

static stbi__uint16 *stbi__load_and_postprocess_16bit(stbi__context *s, int *x, int *y, int *comp, int req_comp)
{
   stbi__result_info ri;
   void *result = stbi__load_main(s, x, y, comp, req_comp, &ri, 16);

   if (result == NULL)
      return NULL;

   // it is the responsibility of the loaders to make sure we get either 8 or 16 bit.
   STBI_ASSERT(ri.bits_per_channel == 8 || ri.bits_per_channel == 16);

   if (ri.bits_per_channel != 16) {
      result = stbi__convert_8_to_16((stbi_uc *) result, *x, *y, req_comp == 0 ? *comp : req_comp);
      ri.bits_per_channel = 16;
   }

   // @TODO: move stbi__convert_format16 to here
   // @TODO: special case RGB-to-Y (and RGBA-to-YA) for 8-bit-to-16-bit case to keep more precision

   if (stbi__vertically_flip_on_load) {
      int channels = req_comp ? req_comp : *comp;
      stbi__vertical_flip(result, *x, *y, channels * sizeof(stbi__uint16));
   }

   return (stbi__uint16 *) result;
}

#if !defined(STBI_NO_HDR) && !defined(STBI_NO_LINEAR)
static void stbi__float_postprocess(float *result, int *x, int *y, int *comp, int req_comp)
{
   if (stbi__vertically_flip_on_load && result != NULL) {
      int channels = req_comp ? req_comp : *comp;
      stbi__vertical_flip(result, *x, *y, channels * sizeof(float));
   }
}
#endif

#ifndef STBI_NO_STDIO

#if defined(_WIN32) && defined(STBI_WINDOWS_UTF8)
STBI_EXTERN __declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int cp, unsigned long flags, const char *str, int cbmb, wchar_t *widestr, int cchwide);
STBI_EXTERN __declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned int cp, unsigned long flags, const wchar_t *widestr, int cchwide, char *str, int cbmb, const char *defchar, int *used_default);
#endif

#if defined(_WIN32) && defined(STBI_WINDOWS_UTF8)
STBIDEF int stbi_convert_wchar_to_utf8(char *buffer, size_t bufferlen, const wchar_t* input)
{
	return WideCharToMultiByte(65001 /* UTF8 */, 0, input, -1, buffer, (int) bufferlen, NULL, NULL);
}
#endif

static FILE *stbi__fopen(char const *filename, char const *mode)
{
   FILE *f;
#if defined(_WIN32) && defined(STBI_WINDOWS_UTF8)
   wchar_t wMode[64];
   wchar_t wFilename[1024];
	if (0 == MultiByteToWideChar(65001 /* UTF8 */, 0, filename, -1, wFilename, sizeof(wFilename)/sizeof(*wFilename)))
      return 0;

	if (0 == MultiByteToWideChar(65001 /* UTF8 */, 0, mode, -1, wMode, sizeof(wMode)/sizeof(*wMode)))
      return 0;

#if defined(_MSC_VER) && _MSC_VER >= 1400
	if (0 != _wfopen_s(&f, wFilename, wMode))
		f = 0;
#else
   f = _wfopen(wFilename, wMode);
#endif

#elif defined(_MSC_VER) && _MSC_VER >= 1400
   if (0 != fopen_s(&f, filename, mode))
      f=0;
#else
   f = fopen(filename, mode);
#endif
   return f;
}


STBIDEF stbi_uc *stbi_load(char const *filename, int *x, int *y, int *comp, int req_comp)
{
   FILE *f = stbi__fopen(filename, "rb");
   unsigned char *result;
   if (!f) return stbi__errpuc("can't fopen", "Unable to open file");
   result = stbi_load_from_file(f,x,y,comp,req_comp);
   fclose(f);
   return result;
}

STBIDEF stbi_uc *stbi_load_from_file(FILE *f, int *x, int *y, int *comp, int req_comp)
{
   unsigned char *result;
   stbi__context s;
   stbi__start_file(&s,f);
   result = stbi__load_and_postprocess_8bit(&s,x,y,comp,req_comp);
   if (result) {
      // need to 'unget' all the characters in the IO buffer
      fseek(f, - (int) (s.img_buffer_end - s.img_buffer), SEEK_CUR);
   }
   return result;
}

STBIDEF stbi__uint16 *stbi_load_from_file_16(FILE *f, int *x, int *y, int *comp, int req_comp)
{
   stbi__uint16 *result;
   stbi__context s;
   stbi__start_file(&s,f);
   result = stbi__load_and_postprocess_16bit(&s,x,y,comp,req_comp);
   if (result) {
      // need to 'unget' all the characters in the IO buffer
      fseek(f, - (int) (s.img_buffer_end - s.img_buffer), SEEK_CUR);
   }
   return result;
}

STBIDEF stbi_us *stbi_load_16(char const *filename, int *x, int *y, int *comp, int req_comp)
{
   FILE *f = stbi__fopen(filename, "rb");
   stbi__uint16 *result;
   if (!f) return (stbi_us *) stbi__errpuc("can't fopen", "Unable to open file");
   result = stbi_load_from_file_16(f,x,y,comp,req_comp);
   fclose(f);
   return result;
}


#endif //!STBI_NO_STDIO

STBIDEF stbi_us *stbi_load_16_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels)
{
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__load_and_postprocess_16bit(&s,x,y,channels_in_file,desired_channels);
}

STBIDEF stbi_us *stbi_load_16_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *channels_in_file, int desired_channels)
{
   stbi__context s;
   stbi__start_callbacks(&s, (stbi_io_callbacks *)clbk, user);
   return stbi__load_and_postprocess_16bit(&s,x,y,channels_in_file,desired_channels);
}

STBIDEF stbi_uc *stbi_load_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *comp, int req_comp)
{
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__load_and_postprocess_8bit(&s,x,y,comp,req_comp);
}

STBIDEF stbi_uc *stbi_load_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *comp, int req_comp)
{
   stbi__context s;
   stbi__start_callbacks(&s, (stbi_io_callbacks *) clbk, user);
   return stbi__load_and_postprocess_8bit(&s,x,y,comp,req_comp);
}

#ifndef STBI_NO_GIF
STBIDEF stbi_uc *stbi_load_gif_from_memory(stbi_uc const *buffer, int len, int **delays, int *x, int *y, int *z, int *comp, int req_comp)
{
   unsigned char *result;
   stbi__context s;
   stbi__start_mem(&s,buffer,len);

   result = (unsigned char*) stbi__load_gif_main(&s, delays, x, y, z, comp, req_comp);
   if (stbi__vertically_flip_on_load) {
      stbi__vertical_flip_slices( result, *x, *y, *z, *comp );
   }

   return result;
}
#endif

#ifndef STBI_NO_LINEAR
static float *stbi__loadf_main(stbi__context *s, int *x, int *y, int *comp, int req_comp)
{
   unsigned char *data;
   #ifndef STBI_NO_HDR
   if (stbi__hdr_test(s)) {
      stbi__result_info ri;
      float *hdr_data = stbi__hdr_load(s,x,y,comp,req_comp, &ri);
      if (hdr_data)
         stbi__float_postprocess(hdr_data,x,y,comp,req_comp);
      return hdr_data;
   }
   #endif
   data = stbi__load_and_postprocess_8bit(s, x, y, comp, req_comp);
   if (data)
      return stbi__ldr_to_hdr(data, *x, *y, req_comp ? req_comp : *comp);
   return stbi__errpf("unknown image type", "Image not of any known type, or corrupt");
}

STBIDEF float *stbi_loadf_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *comp, int req_comp)
{
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__loadf_main(&s,x,y,comp,req_comp);
}

STBIDEF float *stbi_loadf_from_callbacks(stbi_io_callbacks const *clbk, void *user, int *x, int *y, int *comp, int req_comp)
{
   stbi__context s;
   stbi__start_callbacks(&s, (stbi_io_callbacks *) clbk, user);
   return stbi__loadf_main(&s,x,y,comp,req_comp);
}

#ifndef STBI_NO_STDIO
STBIDEF float *stbi_loadf(char const *filename, int *x, int *y, int *comp, int req_comp)
{
   float *result;
   FILE *f = stbi__fopen(filename, "rb");
   if (!f) return stbi__errpf("can't fopen", "Unable to open file");
   result = stbi_loadf_from_file(f,x,y,comp,req_comp);
   fclose(f);
   return result;
}

STBIDEF float *stbi_loadf_from_file(FILE *f, int *x, int *y, int *comp, int req_comp)
{
   stbi__context s;
   stbi__start_file(&s,f);
   return stbi__loadf_main(&s,x,y,comp,req_comp);
}
#endif // !STBI_NO_STDIO

#endif // !STBI_NO_LINEAR

// these is-hdr-or-not is defined independent of whether STBI_NO_LINEAR is
// defined, for API simplicity; if STBI_NO_LINEAR is defined, it always
// reports false!

STBIDEF int stbi_is_hdr_from_memory(stbi_uc const *buffer, int len)
{
   #ifndef STBI_NO_HDR
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__hdr_test(&s);
   #else
   STBI_NOTUSED(buffer);
   STBI_NOTUSED(len);
   return 0;
   #endif
}

#ifndef STBI_NO_STDIO
STBIDEF int      stbi_is_hdr          (char const *filename)
{
   FILE *f = stbi__fopen(filename, "rb");
   int result=0;
   if (f) {
      result = stbi_is_hdr_from_file(f);
      fclose(f);
   }
   return result;
}

STBIDEF int stbi_is_hdr_from_file(FILE *f)
{
   #ifndef STBI_NO_HDR
   long pos = ftell(f);
   int res;
   stbi__context s;
   stbi__start_file(&s,f);
   res = stbi__hdr_test(&s);
   fseek(f, pos, SEEK_SET);
   return res;
   #else
   STBI_NOTUSED(f);
   return 0;
   #endif
}
#endif // !STBI_NO_STDIO

STBIDEF int      stbi_is_hdr_from_callbacks(stbi_io_callbacks const *clbk, void *user)
{
   #ifndef STBI_NO_HDR
   stbi__context s;
   stbi__start_callbacks(&s, (stbi_io_callbacks *) clbk, user);
   return stbi__hdr_test(&s);
   #else
   STBI_NOTUSED(clbk);
   STBI_NOTUSED(user);
   return 0;
   #endif
}

#ifndef STBI_NO_LINEAR
static float stbi__l2h_gamma=2.2f, stbi__l2h_scale=1.0f;

STBIDEF void   stbi_ldr_to_hdr_gamma(float gamma) { stbi__l2h_gamma = gamma; }
STBIDEF void   stbi_ldr_to_hdr_scale(float scale) { stbi__l2h_scale = scale; }
#endif

static float stbi__h2l_gamma_i=1.0f/2.2f, stbi__h2l_scale_i=1.0f;

STBIDEF void   stbi_hdr_to_ldr_gamma(float gamma) { stbi__h2l_gamma_i = 1/gamma; }
STBIDEF void   stbi_hdr_to_ldr_scale(float scale) { stbi__h2l_scale_i = 1/scale; }


//////////////////////////////////////////////////////////////////////////////
//
// Common code used by all image loaders
//

enum
{
   STBI__SCAN_load=0,
   STBI__SCAN_type,
   STBI__SCAN_header
};

static void stbi__refill_buffer(stbi__context *s)
{
   int n = (s->io.read)(s->io_user_data,(char*)s->buffer_start,s->buflen);
   s->callback_already_read += (int) (s->img_buffer - s->img_buffer_original);
   if (n == 0) {
      // at end of file, treat same as if from memory, but need to handle case
      // where s->img_buffer isn't pointing to safe memory, e.g. 0-byte file
      s->read_from_callbacks = 0;
      s->img_buffer = s->buffer_start;
      s->img_buffer_end = s->buffer_start+1;
      *s->img_buffer = 0;
   } else {
      s->img_buffer = s->buffer_start;
      s->img_buffer_end = s->buffer_start + n;
   }
}

stbi_inline static stbi_uc stbi__get8(stbi__context *s)
{
   if (s->img_buffer < s->img_buffer_end)
      return *s->img_buffer++;
   if (s->read_from_callbacks) {
      stbi__refill_buffer(s);
      return *s->img_buffer++;
   }
   return 0;
}

#if defined(STBI_NO_JPEG) && defined(STBI_NO_HDR) && defined(STBI_NO_PIC) && defined(STBI_NO_PNM)
// nothing
#else
stbi_inline static int stbi__at_eof(stbi__context *s)
{
   if (s->io.read) {
      if (!(s->io.eof)(s->io_user_data)) return 0;
      // if feof() is true, check if buffer = end
      // special case: we've only got the special 0 character at the end
      if (s->read_from_callbacks == 0) return 1;
   }

   return s->img_buffer >= s->img_buffer_end;
}
#endif

#if defined(STBI_NO_JPEG) && defined(STBI_NO_PNG) && defined(STBI_NO_BMP) && defined(STBI_NO_PSD) && defined(STBI_NO_TGA) && defined(STBI_NO_GIF) && defined(STBI_NO_PIC)
// nothing
#else
static void stbi__skip(stbi__context *s, int n)
{
   if (n == 0) return;  // already there!
   if (n < 0) {
      s->img_buffer = s->img_buffer_end;
      return;
   }
   if (s->io.read) {
      int blen = (int) (s->img_buffer_end - s->img_buffer);
      if (blen < n) {
         s->img_buffer = s->img_buffer_end;
         (s->io.skip)(s->io_user_data, n - blen);
         return;
      }
   }
   s->img_buffer += n;
}
#endif

#if defined(STBI_NO_PNG) && defined(STBI_NO_TGA) && defined(STBI_NO_HDR) && defined(STBI_NO_PNM)
// nothing
#else
static int stbi__getn(stbi__context *s, stbi_uc *buffer, int n)
{
   if (s->io.read) {
      int blen = (int) (s->img_buffer_end - s->img_buffer);
      if (blen < n) {
         int res, count;

         memcpy(buffer, s->img_buffer, blen);

         count = (s->io.read)(s->io_user_data, (char*) buffer + blen, n - blen);
         res = (count == (n-blen));
         s->img_buffer = s->img_buffer_end;
         return res;
      }
   }

   if (s->img_buffer+n <= s->img_buffer_end) {
      memcpy(buffer, s->img_buffer, n);
      s->img_buffer += n;
      return 1;
   } else
      return 0;
}
#endif

#if defined(STBI_NO_JPEG) && defined(STBI_NO_PNG) && defined(STBI_NO_PSD) && defined(STBI_NO_PIC)
// nothing
#else
static int stbi__get16be(stbi__context *s)
{
   int z = stbi__get8(s);
   return (z << 8) + stbi__get8(s);
}
#endif

#if defined(STBI_NO_PNG) && defined(STBI_NO_PSD) && defined(STBI_NO_PIC)
// nothing
#else
static stbi__uint32 stbi__get32be(stbi__context *s)
{
   stbi__uint32 z = stbi__get16be(s);
   return (z << 16) + stbi__get16be(s);
}
#endif

#if defined(STBI_NO_BMP) && defined(STBI_NO_TGA) && defined(STBI_NO_GIF)
// nothing
#else
static int stbi__get16le(stbi__context *s)
{
   int z = stbi__get8(s);
   return z + (stbi__get8(s) << 8);
}
#endif

#ifndef STBI_NO_BMP
static stbi__uint32 stbi__get32le(stbi__context *s)
{
   stbi__uint32 z = stbi__get16le(s);
   z += (stbi__uint32)stbi__get16le(s) << 16;
   return z;
}
#endif

#define STBI__BYTECAST(x)  ((stbi_uc) ((x) & 255))  // truncate int to byte without warnings

#if defined(STBI_NO_JPEG) && defined(STBI_NO_PNG) && defined(STBI_NO_BMP) && defined(STBI_NO_PSD) && defined(STBI_NO_TGA) && defined(STBI_NO_GIF) && defined(STBI_NO_PIC) && defined(STBI_NO_PNM)
// nothing
#else
//////////////////////////////////////////////////////////////////////////////
//
//  generic converter from built-in img_n to req_comp
//    individual types do this automatically as much as possible (e.g. jpeg
//    does all cases internally since it needs to colorspace convert anyway,
//    and it never has alpha, so very few cases ). png can automatically
//    interleave an alpha=255 channel, but falls back to this for other cases
//
//  assume data buffer is malloced, so malloc a new one and free that one
//  only failure mode is malloc failing

static stbi_uc stbi__compute_y(int r, int g, int b)
{
   return (stbi_uc) (((r*77) + (g*150) +  (29*b)) >> 8);
}
#endif

#if defined(STBI_NO_PNG) && defined(STBI_NO_BMP) && defined(STBI_NO_PSD) && defined(STBI_NO_TGA) && defined(STBI_NO_GIF) && defined(STBI_NO_PIC) && defined(STBI_NO_PNM)
// nothing
#else
static unsigned char *stbi__convert_format(unsigned char *data, int img_n, int req_comp, unsigned int x, unsigned int y)
{
   int i,j;
   unsigned char *good;

   if (req_comp == img_n) return data;
   STBI_ASSERT(req_comp >= 1 && req_comp <= 4);

   good = (unsigned char *) stbi__malloc_mad3(req_comp, x, y, 0);
   if (good == NULL) {
      STBI_FREE(data);
      return stbi__errpuc("outofmem", "Out of memory");
   }

   for (j=0; j < (int) y; ++j) {
      unsigned char *src  = data + j * x * img_n   ;
      unsigned char *dest = good + j * x * req_comp;

      #define STBI__COMBO(a,b)  ((a)*8+(b))
      #define STBI__CASE(a,b)   case STBI__COMBO(a,b): for(i=x-1; i >= 0; --i, src += a, dest += b)
      // convert source image with img_n components to one with req_comp components;
      // avoid switch per pixel, so use switch per scanline and massive macros
      switch (STBI__COMBO(img_n, req_comp)) {
         STBI__CASE(1,2) { dest[0]=src[0]; dest[1]=255;                                     } break;
         STBI__CASE(1,3) { dest[0]=dest[1]=dest[2]=src[0];                                  } break;
         STBI__CASE(1,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=255;                     } break;
         STBI__CASE(2,1) { dest[0]=src[0];                                                  } break;
         STBI__CASE(2,3) { dest[0]=dest[1]=dest[2]=src[0];                                  } break;
         STBI__CASE(2,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=src[1];                  } break;
         STBI__CASE(3,4) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];dest[3]=255;        } break;
         STBI__CASE(3,1) { dest[0]=stbi__compute_y(src[0],src[1],src[2]);                   } break;
         STBI__CASE(3,2) { dest[0]=stbi__compute_y(src[0],src[1],src[2]); dest[1] = 255;    } break;
         STBI__CASE(4,1) { dest[0]=stbi__compute_y(src[0],src[1],src[2]);                   } break;
         STBI__CASE(4,2) { dest[0]=stbi__compute_y(src[0],src[1],src[2]); dest[1] = src[3]; } break;
         STBI__CASE(4,3) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];                    } break;
         default: STBI_ASSERT(0); STBI_FREE(data); STBI_FREE(good); return stbi__errpuc("unsupported", "Unsupported format conversion");
      }
      #undef STBI__CASE
   }

   STBI_FREE(data);
   return good;
}
#endif

#if defined(STBI_NO_PNG) && defined(STBI_NO_PSD)
// nothing
#else
static stbi__uint16 stbi__compute_y_16(int r, int g, int b)
{
   return (stbi__uint16) (((r*77) + (g*150) +  (29*b)) >> 8);
}
#endif

#if defined(STBI_NO_PNG) && defined(STBI_NO_PSD)
// nothing
#else
static stbi__uint16 *stbi__convert_format16(stbi__uint16 *data, int img_n, int req_comp, unsigned int x, unsigned int y)
{
   int i,j;
   stbi__uint16 *good;

   if (req_comp == img_n) return data;
   STBI_ASSERT(req_comp >= 1 && req_comp <= 4);

   good = (stbi__uint16 *) stbi__malloc(req_comp * x * y * 2);
   if (good == NULL) {
      STBI_FREE(data);
      return (stbi__uint16 *) stbi__errpuc("outofmem", "Out of memory");
   }

   for (j=0; j < (int) y; ++j) {
      stbi__uint16 *src  = data + j * x * img_n   ;
      stbi__uint16 *dest = good + j * x * req_comp;

      #define STBI__COMBO(a,b)  ((a)*8+(b))
      #define STBI__CASE(a,b)   case STBI__COMBO(a,b): for(i=x-1; i >= 0; --i, src += a, dest += b)
      // convert source image with img_n components to one with req_comp components;
      // avoid switch per pixel, so use switch per scanline and massive macros
      switch (STBI__COMBO(img_n, req_comp)) {
         STBI__CASE(1,2) { dest[0]=src[0]; dest[1]=0xffff;                                     } break;
         STBI__CASE(1,3) { dest[0]=dest[1]=dest[2]=src[0];                                     } break;
         STBI__CASE(1,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=0xffff;                     } break;
         STBI__CASE(2,1) { dest[0]=src[0];                                                     } break;
         STBI__CASE(2,3) { dest[0]=dest[1]=dest[2]=src[0];                                     } break;
         STBI__CASE(2,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=src[1];                     } break;
         STBI__CASE(3,4) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];dest[3]=0xffff;        } break;
         STBI__CASE(3,1) { dest[0]=stbi__compute_y_16(src[0],src[1],src[2]);                   } break;
         STBI__CASE(3,2) { dest[0]=stbi__compute_y_16(src[0],src[1],src[2]); dest[1] = 0xffff; } break;
         STBI__CASE(4,1) { dest[0]=stbi__compute_y_16(src[0],src[1],src[2]);                   } break;
         STBI__CASE(4,2) { dest[0]=stbi__compute_y_16(src[0],src[1],src[2]); dest[1] = src[3]; } break;
         STBI__CASE(4,3) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];                       } break;
         default: STBI_ASSERT(0); STBI_FREE(data); STBI_FREE(good); return (stbi__uint16*) stbi__errpuc("unsupported", "Unsupported format conversion");
      }
      #undef STBI__CASE
   }

   STBI_FREE(data);
   return good;
}
#endif

#ifndef STBI_NO_LINEAR
static float   *stbi__ldr_to_hdr(stbi_uc *data, int x, int y, int comp)
{
   int i,k,n;
   float *output;
   if (!data) return NULL;
   output = (float *) stbi__malloc_mad4(x, y, comp, sizeof(float), 0);
   if (output == NULL) { STBI_FREE(data); return stbi__errpf("outofmem", "Out of memory"); }
   // compute number of non-alpha components
   if (comp & 1) n = comp; else n = comp-1;
   for (i=0; i < x*y; ++i) {
      for (k=0; k < n; ++k) {
         output[i*comp + k] = (float) (pow(data[i*comp+k]/255.0f, stbi__l2h_gamma) * stbi__l2h_scale);
      }
   }
   if (n < comp) {
      for (i=0; i < x*y; ++i) {
         output[i*comp + n] = data[i*comp + n]/255.0f;
      }
   }
   STBI_FREE(data);
   return output;
}
#endif

#ifndef STBI_NO_HDR
#define stbi__float2int(x)   ((int) (x))
static stbi_uc *stbi__hdr_to_ldr(float   *data, int x, int y, int comp)
{
   int i,k,n;
   stbi_uc *output;
   if (!data) return NULL;
   output = (stbi_uc *) stbi__malloc_mad3(x, y, comp, 0);
   if (output == NULL) { STBI_FREE(data); return stbi__errpuc("outofmem", "Out of memory"); }
   // compute number of non-alpha components
   if (comp & 1) n = comp; else n = comp-1;
   for (i=0; i < x*y; ++i) {
      for (k=0; k < n; ++k) {
         float z = (float) pow(data[i*comp+k]*stbi__h2l_scale_i, stbi__h2l_gamma_i) * 255 + 0.5f;
         if (z < 0) z = 0;
         if (z > 255) z = 255;
         output[i*comp + k] = (stbi_uc) stbi__float2int(z);
      }
      if (k < comp) {
         float z = data[i*comp+k] * 255 + 0.5f;
         if (z < 0) z = 0;
         if (z > 255) z = 255;
         output[i*comp + k] = (stbi_uc) stbi__float2int(z);
      }
   }
   STBI_FREE(data);
   return output;
}
#endif

//////////////////////////////////////////////////////////////////////////////
//
//  "baseline" JPEG/JFIF decoder
//
//    simple implementation
//      - doesn't support delayed output of y-dimension
//      - simple interface (only one output format: 8-bit interleaved RGB)
//      - doesn't try to recover corrupt jpegs
//      - doesn't allow partial loading, loading multiple at once
//      - still fast on x86 (copying globals into locals doesn't help x86)
//      - allocates lots of intermediate memory (full size of all components)
//        - non-interleaved case requires this anyway
//        - allows good upsampling (see next)
//    high-quality
//      - upsampled channels are bilinearly interpolated, even across blocks
//      - quality integer IDCT derived from IJG's 'slow'
//    performance
//      - fast huffman; reasonable integer IDCT
//      - some SIMD kernels for common paths on targets with SSE2/NEON
//      - uses a lot of intermediate memory, could cache poorly

#ifndef STBI_NO_JPEG

// huffman decoding acceleration
#define FAST_BITS   9  // larger handles more cases; smaller stomps less cache

typedef struct
{
   stbi_uc  fast[1 << FAST_BITS];
   // weirdly, repacking this into AoS is a 10% speed loss, instead of a win
   stbi__uint16 code[256];
   stbi_uc  values[256];
   stbi_uc  size[257];
   unsigned int maxcode[18];
   int    delta[17];   // old 'firstsymbol' - old 'firstcode'
} stbi__huffman;

typedef struct
{
   stbi__context *s;
   stbi__huffman huff_dc[4];
   stbi__huffman huff_ac[4];
   stbi__uint16 dequant[4][64];
   stbi__int16 fast_ac[4][1 << FAST_BITS];

// sizes for components, interleaved MCUs
   int img_h_max, img_v_max;
   int img_mcu_x, img_mcu_y;
   int img_mcu_w, img_mcu_h;

// definition of jpeg image component
   struct
   {
      int id;
      int h,v;
      int tq;
      int hd,ha;
      int dc_pred;

      int x,y,w2,h2;
      stbi_uc *data;
      void *raw_data, *raw_coeff;
      stbi_uc *linebuf;
      short   *coeff;   // progressive only
      int      coeff_w, coeff_h; // number of 8x8 coefficient blocks
   } img_comp[4];

   stbi__uint32   code_buffer; // jpeg entropy-coded buffer
   int            code_bits;   // number of valid bits
   unsigned char  marker;      // marker seen while filling entropy buffer
   int            nomore;      // flag if we saw a marker so must stop

   int            progressive;
   int            spec_start;
   int            spec_end;
   int            succ_high;
   int            succ_low;
   int            eob_run;
   int            jfif;
   int            app14_color_transform; // Adobe APP14 tag
   int            rgb;

   int scan_n, order[4];
   int restart_interval, todo;

// kernels
   void (*idct_block_kernel)(stbi_uc *out, int out_stride, short data[64]);
   void (*YCbCr_to_RGB_kernel)(stbi_uc *out, const stbi_uc *y, const stbi_uc *pcb, const stbi_uc *pcr, int count, int step);
   stbi_uc *(*resample_row_hv_2_kernel)(stbi_uc *out, stbi_uc *in_near, stbi_uc *in_far, int w, int hs);
} stbi__jpeg;

static int stbi__build_huffman(stbi__huffman *h, int *count)
{
   int i,j,k=0;
   unsigned int code;
   // build size list for each symbol (from JPEG spec)
   for (i=0; i < 16; ++i) {
      for (j=0; j < count[i]; ++j) {
         h->size[k++] = (stbi_uc) (i+1);
         if(k >= 257) return stbi__err("bad size list","Corrupt JPEG");
      }
   }
   h->size[k] = 0;

   // compute actual symbols (from jpeg spec)
   code = 0;
   k = 0;
   for(j=1; j <= 16; ++j) {
      // compute delta to add to code to compute symbol id
      h->delta[j] = k - code;
      if (h->size[k] == j) {
         while (h->size[k] == j)
            h->code[k++] = (stbi__uint16) (code++);
         if (code-1 >= (1u << j)) return stbi__err("bad code lengths","Corrupt JPEG");
      }
      // compute largest code + 1 for this size, preshifted as needed later
      h->maxcode[j] = code << (16-j);
      code <<= 1;
   }
   h->maxcode[j] = 0xffffffff;

   // build non-spec acceleration table; 255 is flag for not-accelerated
   memset(h->fast, 255, 1 << FAST_BITS);
   for (i=0; i < k; ++i) {
      int s = h->size[i];
      if (s <= FAST_BITS) {
         int c = h->code[i] << (FAST_BITS-s);
         int m = 1 << (FAST_BITS-s);
         for (j=0; j < m; ++j) {
            h->fast[c+j] = (stbi_uc) i;
         }
      }
   }
   return 1;
}

// build a table that decodes both magnitude and value of small ACs in
// one go.
static void stbi__build_fast_ac(stbi__int16 *fast_ac, stbi__huffman *h)
{
   int i;
   for (i=0; i < (1 << FAST_BITS); ++i) {
      stbi_uc fast = h->fast[i];
      fast_ac[i] = 0;
      if (fast < 255) {
         int rs = h->values[fast];
         int run = (rs >> 4) & 15;
         int magbits = rs & 15;
         int len = h->size[fast];

         if (magbits && len + magbits <= FAST_BITS) {
            // magnitude code followed by receive_extend code
            int k = ((i << len) & ((1 << FAST_BITS) - 1)) >> (FAST_BITS - magbits);
            int m = 1 << (magbits - 1);
            if (k < m) k += (~0U << magbits) + 1;
            // if the result is small enough, we can fit it in fast_ac table
            if (k >= -128 && k <= 127)
               fast_ac[i] = (stbi__int16) ((k * 256) + (run * 16) + (len + magbits));
         }
      }
   }
}

static void stbi__grow_buffer_unsafe(stbi__jpeg *j)
{
   do {
      unsigned int b = j->nomore ? 0 : stbi__get8(j->s);
      if (b == 0xff) {
         int c = stbi__get8(j->s);
         while (c == 0xff) c = stbi__get8(j->s); // consume fill bytes
         if (c != 0) {
            j->marker = (unsigned char) c;
            j->nomore = 1;
            return;
         }
      }
      j->code_buffer |= b << (24 - j->code_bits);
      j->code_bits += 8;
   } while (j->code_bits <= 24);
}

// (1 << n) - 1
static const stbi__uint32 stbi__bmask[17]={0,1,3,7,15,31,63,127,255,511,1023,2047,4095,8191,16383,32767,65535};

// decode a jpeg huffman value from the bitstream
stbi_inline static int stbi__jpeg_huff_decode(stbi__jpeg *j, stbi__huffman *h)
{
   unsigned int temp;
   int c,k;

   if (j->code_bits < 16) stbi__grow_buffer_unsafe(j);

   // look at the top FAST_BITS and determine what symbol ID it is,
   // if the code is <= FAST_BITS
   c = (j->code_buffer >> (32 - FAST_BITS)) & ((1 << FAST_BITS)-1);
   k = h->fast[c];
   if (k < 255) {
      int s = h->size[k];
      if (s > j->code_bits)
         return -1;
      j->code_buffer <<= s;
      j->code_bits -= s;
      return h->values[k];
   }

   // naive test is to shift the code_buffer down so k bits are
   // valid, then test against maxcode. To speed this up, we've
   // preshifted maxcode left so that it has (16-k) 0s at the
   // end; in other words, regardless of the number of bits, it
   // wants to be compared against something shifted to have 16;
   // that way we don't need to shift inside the loop.
   temp = j->code_buffer >> 16;
   for (k=FAST_BITS+1 ; ; ++k)
      if (temp < h->maxcode[k])
         break;
   if (k == 17) {
      // error! code not found
      j->code_bits -= 16;
      return -1;
   }

   if (k > j->code_bits)
      return -1;

   // convert the huffman code to the symbol id
   c = ((j->code_buffer >> (32 - k)) & stbi__bmask[k]) + h->delta[k];
   if(c < 0 || c >= 256) // symbol id out of bounds!
       return -1;
   STBI_ASSERT((((j->code_buffer) >> (32 - h->size[c])) & stbi__bmask[h->size[c]]) == h->code[c]);

   // convert the id to a symbol
   j->code_bits -= k;
   j->code_buffer <<= k;
   return h->values[c];
}

// bias[n] = (-1<<n) + 1
static const int stbi__jbias[16] = {0,-1,-3,-7,-15,-31,-63,-127,-255,-511,-1023,-2047,-4095,-8191,-16383,-32767};

// combined JPEG 'receive' and JPEG 'extend', since baseline
// always extends everything it receives.
stbi_inline static int stbi__extend_receive(stbi__jpeg *j, int n)
{
   unsigned int k;
   int sgn;
   if (j->code_bits < n) stbi__grow_buffer_unsafe(j);
   if (j->code_bits < n) return 0; // ran out of bits from stream, return 0s intead of continuing

   sgn = j->code_buffer >> 31; // sign bit always in MSB; 0 if MSB clear (positive), 1 if MSB set (negative)
   k = stbi_lrot(j->code_buffer, n);
   j->code_buffer = k & ~stbi__bmask[n];
   k &= stbi__bmask[n];
   j->code_bits -= n;
   return k + (stbi__jbias[n] & (sgn - 1));
}

// get some unsigned bits
stbi_inline static int stbi__jpeg_get_bits(stbi__jpeg *j, int n)
{
   unsigned int k;
   if (j->code_bits < n) stbi__grow_buffer_unsafe(j);
   if (j->code_bits < n) return 0; // ran out of bits from stream, return 0s intead of continuing
   k = stbi_lrot(j->code_buffer, n);
   j->code_buffer = k & ~stbi__bmask[n];
   k &= stbi__bmask[n];
   j->code_bits -= n;
   return k;
}

stbi_inline static int stbi__jpeg_get_bit(stbi__jpeg *j)
{
   unsigned int k;
   if (j->code_bits < 1) stbi__grow_buffer_unsafe(j);
   if (j->code_bits < 1) return 0; // ran out of bits from stream, return 0s intead of continuing
   k = j->code_buffer;
   j->code_buffer <<= 1;
   --j->code_bits;
   return k & 0x80000000;
}

// given a value that's at position X in the zigzag stream,
// where does it appear in the 8x8 matrix coded as row-major?
static const stbi_uc stbi__jpeg_dezigzag[64+15] =
{
    0,  1,  8, 16,  9,  2,  3, 10,
   17, 24, 32, 25, 18, 11,  4,  5,
   12, 19, 26, 33, 40, 48, 41, 34,
   27, 20, 13,  6,  7, 14, 21, 28,
   35, 42, 49, 56, 57, 50, 43, 36,
   29, 22, 15, 23, 30, 37, 44, 51,
   58, 59, 52, 45, 38, 31, 39, 46,
   53, 60, 61, 54, 47, 55, 62, 63,
   // let corrupt input sample past end
   63, 63, 63, 63, 63, 63, 63, 63,
   63, 63, 63, 63, 63, 63, 63
};

// decode one 64-entry block--
static int stbi__jpeg_decode_block(stbi__jpeg *j, short data[64], stbi__huffman *hdc, stbi__huffman *hac, stbi__int16 *fac, int b, stbi__uint16 *dequant)
{
   int diff,dc,k;
   int t;

   if (j->code_bits < 16) stbi__grow_buffer_unsafe(j);
   t = stbi__jpeg_huff_decode(j, hdc);
   if (t < 0 || t > 15) return stbi__err("bad huffman code","Corrupt JPEG");

   // 0 all the ac values now so we can do it 32-bits at a time
   memset(data,0,64*sizeof(data[0]));

   diff = t ? stbi__extend_receive(j, t) : 0;
   if (!stbi__addints_valid(j->img_comp[b].dc_pred, diff)) return stbi__err("bad delta","Corrupt JPEG");
   dc = j->img_comp[b].dc_pred + diff;
   j->img_comp[b].dc_pred = dc;
   if (!stbi__mul2shorts_valid(dc, dequant[0])) return stbi__err("can't merge dc and ac", "Corrupt JPEG");
   data[0] = (short) (dc * dequant[0]);

   // decode AC components, see JPEG spec
   k = 1;
   do {
      unsigned int zig;
      int c,r,s;
      if (j->code_bits < 16) stbi__grow_buffer_unsafe(j);
      c = (j->code_buffer >> (32 - FAST_BITS)) & ((1 << FAST_BITS)-1);
      r = fac[c];
      if (r) { // fast-AC path
         k += (r >> 4) & 15; // run
         s = r & 15; // combined length
         if (s > j->code_bits) return stbi__err("bad huffman code", "Combined length longer than code bits available");
         j->code_buffer <<= s;
         j->code_bits -= s;
         // decode into unzigzag'd location
         zig = stbi__jpeg_dezigzag[k++];
         data[zig] = (short) ((r >> 8) * dequant[zig]);
      } else {
         int rs = stbi__jpeg_huff_decode(j, hac);
         if (rs < 0) return stbi__err("bad huffman code","Corrupt JPEG");
         s = rs & 15;
         r = rs >> 4;
         if (s == 0) {
            if (rs != 0xf0) break; // end block
            k += 16;
         } else {
            k += r;
            // decode into unzigzag'd location
            zig = stbi__jpeg_dezigzag[k++];
            data[zig] = (short) (stbi__extend_receive(j,s) * dequant[zig]);
         }
      }
   } while (k < 64);
   return 1;
}

static int stbi__jpeg_decode_block_prog_dc(stbi__jpeg *j, short data[64], stbi__huffman *hdc, int b)
{
   int diff,dc;
   int t;
   if (j->spec_end != 0) return stbi__err("can't merge dc and ac", "Corrupt JPEG");

   if (j->code_bits < 16) stbi__grow_buffer_unsafe(j);

   if (j->succ_high == 0) {
      // first scan for DC coefficient, must be first
      memset(data,0,64*sizeof(data[0])); // 0 all the ac values now
      t = stbi__jpeg_huff_decode(j, hdc);
      if (t < 0 || t > 15) return stbi__err("can't merge dc and ac", "Corrupt JPEG");
      diff = t ? stbi__extend_receive(j, t) : 0;

      if (!stbi__addints_valid(j->img_comp[b].dc_pred, diff)) return stbi__err("bad delta", "Corrupt JPEG");
      dc = j->img_comp[b].dc_pred + diff;
      j->img_comp[b].dc_pred = dc;
      if (!stbi__mul2shorts_valid(dc, 1 << j->succ_low)) return stbi__err("can't merge dc and ac", "Corrupt JPEG");
      data[0] = (short) (dc * (1 << j->succ_low));
   } else {
      // refinement scan for DC coefficient
      if (stbi__jpeg_get_bit(j))
         data[0] += (short) (1 << j->succ_low);
   }
   return 1;
}

// @OPTIMIZE: store non-zigzagged during the decode passes,
// and only de-zigzag when dequantizing
static int stbi__jpeg_decode_block_prog_ac(stbi__jpeg *j, short data[64], stbi__huffman *hac, stbi__int16 *fac)
{
   int k;
   if (j->spec_start == 0) return stbi__err("can't merge dc and ac", "Corrupt JPEG");

   if (j->succ_high == 0) {
      int shift = j->succ_low;

      if (j->eob_run) {
         --j->eob_run;
         return 1;
      }

      k = j->spec_start;
      do {
         unsigned int zig;
         int c,r,s;
         if (j->code_bits < 16) stbi__grow_buffer_unsafe(j);
         c = (j->code_buffer >> (32 - FAST_BITS)) & ((1 << FAST_BITS)-1);
         r = fac[c];
         if (r) { // fast-AC path
            k += (r >> 4) & 15; // run
            s = r & 15; // combined length
            if (s > j->code_bits) return stbi__err("bad huffman code", "Combined length longer than code bits available");
            j->code_buffer <<= s;
            j->code_bits -= s;
            zig = stbi__jpeg_dezigzag[k++];
            data[zig] = (short) ((r >> 8) * (1 << shift));
         } else {
            int rs = stbi__jpeg_huff_decode(j, hac);
            if (rs < 0) return stbi__err("bad huffman code","Corrupt JPEG");
            s = rs & 15;
            r = rs >> 4;
            if (s == 0) {
               if (r < 15) {
                  j->eob_run = (1 << r);
                  if (r)
                     j->eob_run += stbi__jpeg_get_bits(j, r);
                  --j->eob_run;
                  break;
               }
               k += 16;
            } else {
               k += r;
               zig = stbi__jpeg_dezigzag[k++];
               data[zig] = (short) (stbi__extend_receive(j,s) * (1 << shift));
            }
         }
      } while (k <= j->spec_end);
   } else {
      // refinement scan for these AC coefficients

      short bit = (short) (1 << j->succ_low);

      if (j->eob_run) {
         --j->eob_run;
         for (k = j->spec_start; k <= j->spec_end; ++k) {
            short *p = &data[stbi__jpeg_dezigzag[k]];
            if (*p != 0)
               if (stbi__jpeg_get_bit(j))
                  if ((*p & bit)==0) {
                     if (*p > 0)
                        *p += bit;
                     else
                        *p -= bit;
                  }
         }
      } else {
         k = j->spec_start;
         do {
            int r,s;
            int rs = stbi__jpeg_huff_decode(j, hac); // @OPTIMIZE see if we can use the fast path here, advance-by-r is so slow, eh
            if (rs < 0) return stbi__err("bad huffman code","Corrupt JPEG");
            s = rs & 15;
            r = rs >> 4;
            if (s == 0) {
               if (r < 15) {
                  j->eob_run = (1 << r) - 1;
                  if (r)
                     j->eob_run += stbi__jpeg_get_bits(j, r);
                  r = 64; // force end of block
               } else {
                  // r=15 s=0 should write 16 0s, so we just do
                  // a run of 15 0s and then write s (which is 0),
                  // so we don't have to do anything special here
               }
            } else {
               if (s != 1) return stbi__err("bad huffman code", "Corrupt JPEG");
               // sign bit
               if (stbi__jpeg_get_bit(j))
                  s = bit;
               else
                  s = -bit;
            }

            // advance by r
            while (k <= j->spec_end) {
               short *p = &data[stbi__jpeg_dezigzag[k++]];
               if (*p != 0) {
                  if (stbi__jpeg_get_bit(j))
                     if ((*p & bit)==0) {
                        if (*p > 0)
                           *p += bit;
                        else
                           *p -= bit;
                     }
               } else {
                  if (r == 0) {
                     *p = (short) s;
                     break;
                  }
                  --r;
               }
            }
         } while (k <= j->spec_end);
      }
   }
   return 1;
}

// take a -128..127 value and stbi__clamp it and convert to 0..255
stbi_inline static stbi_uc stbi__clamp(int x)
{
   // trick to use a single test to catch both cases
   if ((unsigned int) x > 255) {
      if (x < 0) return 0;
      if (x > 255) return 255;
   }
   return (stbi_uc) x;
}

#define stbi__f2f(x)  ((int) (((x) * 4096 + 0.5)))
#define stbi__fsh(x)  ((x) * 4096)

// derived from jidctint -- DCT_ISLOW
#define STBI__IDCT_1D(s0,s1,s2,s3,s4,s5,s6,s7) \
   int t0,t1,t2,t3,p1,p2,p3,p4,p5,x0,x1,x2,x3; \
   p2 = s2;                                    \
   p3 = s6;                                    \
   p1 = (p2+p3) * stbi__f2f(0.5411961f);       \
   t2 = p1 + p3*stbi__f2f(-1.847759065f);      \
   t3 = p1 + p2*stbi__f2f( 0.765366865f);      \
   p2 = s0;                                    \
   p3 = s4;                                    \
   t0 = stbi__fsh(p2+p3);                      \
   t1 = stbi__fsh(p2-p3);                      \
   x0 = t0+t3;                                 \
   x3 = t0-t3;                                 \
   x1 = t1+t2;                                 \
   x2 = t1-t2;                                 \
   t0 = s7;                                    \
   t1 = s5;                                    \
   t2 = s3;                                    \
   t3 = s1;                                    \
   p3 = t0+t2;                                 \
   p4 = t1+t3;                                 \
   p1 = t0+t3;                                 \
   p2 = t1+t2;                                 \
   p5 = (p3+p4)*stbi__f2f( 1.175875602f);      \
   t0 = t0*stbi__f2f( 0.298631336f);           \
   t1 = t1*stbi__f2f( 2.053119869f);           \
   t2 = t2*stbi__f2f( 3.072711026f);           \
   t3 = t3*stbi__f2f( 1.501321110f);           \
   p1 = p5 + p1*stbi__f2f(-0.899976223f);      \
   p2 = p5 + p2*stbi__f2f(-2.562915447f);      \
   p3 = p3*stbi__f2f(-1.961570560f);           \
   p4 = p4*stbi__f2f(-0.390180644f);           \
   t3 += p1+p4;                                \
   t2 += p2+p3;                                \
   t1 += p2+p4;                                \
   t0 += p1+p3;

static void stbi__idct_block(stbi_uc *out, int out_stride, short data[64])
{
   int i,val[64],*v=val;
   stbi_uc *o;
   short *d = data;

   // columns
   for (i=0; i < 8; ++i,++d, ++v) {
      // if all zeroes, shortcut -- this avoids dequantizing 0s and IDCTing
      if (d[ 8]==0 && d[16]==0 && d[24]==0 && d[32]==0
           && d[40]==0 && d[48]==0 && d[56]==0) {
         //    no shortcut                 0     seconds
         //    (1|2|3|4|5|6|7)==0          0     seconds
         //    all separate               -0.047 seconds
         //    1 && 2|3 && 4|5 && 6|7:    -0.047 seconds
         int dcterm = d[0]*4;
         v[0] = v[8] = v[16] = v[24] = v[32] = v[40] = v[48] = v[56] = dcterm;
      } else {
         STBI__IDCT_1D(d[ 0],d[ 8],d[16],d[24],d[32],d[40],d[48],d[56])
         // constants scaled things up by 1<<12; let's bring them back
         // down, but keep 2 extra bits of precision
         x0 += 512; x1 += 512; x2 += 512; x3 += 512;
         v[ 0] = (x0+t3) >> 10;
         v[56] = (x0-t3) >> 10;
         v[ 8] = (x1+t2) >> 10;
         v[48] = (x1-t2) >> 10;
         v[16] = (x2+t1) >> 10;
         v[40] = (x2-t1) >> 10;
         v[24] = (x3+t0) >> 10;
         v[32] = (x3-t0) >> 10;
      }
   }

   for (i=0, v=val, o=out; i < 8; ++i,v+=8,o+=out_stride) {
      // no fast case since the first 1D IDCT spread components out
      STBI__IDCT_1D(v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7])
      // constants scaled things up by 1<<12, plus we had 1<<2 from first
      // loop, plus horizontal and vertical each scale by sqrt(8) so together
      // we've got an extra 1<<3, so 1<<17 total we need to remove.
      // so we want to round that, which means adding 0.5 * 1<<17,
      // aka 65536. Also, we'll end up with -128 to 127 that we want
      // to encode as 0..255 by adding 128, so we'll add that before the shift
      x0 += 65536 + (128<<17);
      x1 += 65536 + (128<<17);
      x2 += 65536 + (128<<17);
      x3 += 65536 + (128<<17);
      // tried computing the shifts into temps, or'ing the temps to see
      // if any were out of range, but that was slower
      o[0] = stbi__clamp((x0+t3) >> 17);
      o[7] = stbi__clamp((x0-t3) >> 17);
      o[1] = stbi__clamp((x1+t2) >> 17);
      o[6] = stbi__clamp((x1-t2) >> 17);
      o[2] = stbi__clamp((x2+t1) >> 17);
      o[5] = stbi__clamp((x2-t1) >> 17);
      o[3] = stbi__clamp((x3+t0) >> 17);
      o[4] = stbi__clamp((x3-t0) >> 17);
   }
}

#ifdef STBI_SSE2
// sse2 integer IDCT. not the fastest possible implementation but it
// produces bit-identical results to the generic C version so it's
// fully "transparent".
static void stbi__idct_simd(stbi_uc *out, int out_stride, short data[64])
{
   // This is constructed to match our regular (generic) integer IDCT exactly.
   __m128i row0, row1, row2, row3, row4, row5, row6, row7;
   __m128i tmp;

   // dot product constant: even elems=x, odd elems=y
   #define dct_const(x,y)  _mm_setr_epi16((x),(y),(x),(y),(x),(y),(x),(y))

   // out(0) = c0[even]*x + c0[odd]*y   (c0, x, y 16-bit, out 32-bit)
   // out(1) = c1[even]*x + c1[odd]*y
   #define dct_rot(out0,out1, x,y,c0,c1) \
      __m128i c0##lo = _mm_unpacklo_epi16((x),(y)); \
      __m128i c0##hi = _mm_unpackhi_epi16((x),(y)); \
      __m128i out0##_l = _mm_madd_epi16(c0##lo, c0); \
      __m128i out0##_h = _mm_madd_epi16(c0##hi, c0); \
      __m128i out1##_l = _mm_madd_epi16(c0##lo, c1); \
      __m128i out1##_h = _mm_madd_epi16(c0##hi, c1)

   // out = in << 12  (in 16-bit, out 32-bit)
   #define dct_widen(out, in) \
      __m128i out##_l = _mm_srai_epi32(_mm_unpacklo_epi16(_mm_setzero_si128(), (in)), 4); \
      __m128i out##_h = _mm_srai_epi32(_mm_unpackhi_epi16(_mm_setzero_si128(), (in)), 4)

   // wide add
   #define dct_wadd(out, a, b) \
      __m128i out##_l = _mm_add_epi32(a##_l, b##_l); \
      __m128i out##_h = _mm_add_epi32(a##_h, b##_h)

   // wide sub
   #define dct_wsub(out, a, b) \
      __m128i out##_l = _mm_sub_epi32(a##_l, b##_l); \
      __m128i out##_h = _mm_sub_epi32(a##_h, b##_h)

   // butterfly a/b, add bias, then shift by "s" and pack
   #define dct_bfly32o(out0, out1, a,b,bias,s) \
      { \
         __m128i abiased_l = _mm_add_epi32(a##_l, bias); \
         __m128i abiased_h = _mm_add_epi32(a##_h, bias); \
         dct_wadd(sum, abiased, b); \
         dct_wsub(dif, abiased, b); \
         out0 = _mm_packs_epi32(_mm_srai_epi32(sum_l, s), _mm_srai_epi32(sum_h, s)); \
         out1 = _mm_packs_epi32(_mm_srai_epi32(dif_l, s), _mm_srai_epi32(dif_h, s)); \
      }

   // 8-bit interleave step (for transposes)
   #define dct_interleave8(a, b) \
      tmp = a; \
      a = _mm_unpacklo_epi8(a, b); \
      b = _mm_unpackhi_epi8(tmp, b)

   // 16-bit interleave step (for transposes)
   #define dct_interleave16(a, b) \
      tmp = a; \
      a = _mm_unpacklo_epi16(a, b); \
      b = _mm_unpackhi_epi16(tmp, b)

   #define dct_pass(bias,shift) \
      { \
         /* even part */ \
         dct_rot(t2e,t3e, row2,row6, rot0_0,rot0_1); \
         __m128i sum04 = _mm_add_epi16(row0, row4); \
         __m128i dif04 = _mm_sub_epi16(row0, row4); \
         dct_widen(t0e, sum04); \
         dct_widen(t1e, dif04); \
         dct_wadd(x0, t0e, t3e); \
         dct_wsub(x3, t0e, t3e); \
         dct_wadd(x1, t1e, t2e); \
         dct_wsub(x2, t1e, t2e); \
         /* odd part */ \
         dct_rot(y0o,y2o, row7,row3, rot2_0,rot2_1); \
         dct_rot(y1o,y3o, row5,row1, rot3_0,rot3_1); \
         __m128i sum17 = _mm_add_epi16(row1, row7); \
         __m128i sum35 = _mm_add_epi16(row3, row5); \
         dct_rot(y4o,y5o, sum17,sum35, rot1_0,rot1_1); \
         dct_wadd(x4, y0o, y4o); \
         dct_wadd(x5, y1o, y5o); \
         dct_wadd(x6, y2o, y5o); \
         dct_wadd(x7, y3o, y4o); \
         dct_bfly32o(row0,row7, x0,x7,bias,shift); \
         dct_bfly32o(row1,row6, x1,x6,bias,shift); \
         dct_bfly32o(row2,row5, x2,x5,bias,shift); \
         dct_bfly32o(row3,row4, x3,x4,bias,shift); \
      }

   __m128i rot0_0 = dct_const(stbi__f2f(0.5411961f), stbi__f2f(0.5411961f) + stbi__f2f(-1.847759065f));
   __m128i rot0_1 = dct_const(stbi__f2f(0.5411961f) + stbi__f2f( 0.765366865f), stbi__f2f(0.5411961f));
   __m128i rot1_0 = dct_const(stbi__f2f(1.175875602f) + stbi__f2f(-0.899976223f), stbi__f2f(1.175875602f));
   __m128i rot1_1 = dct_const(stbi__f2f(1.175875602f), stbi__f2f(1.175875602f) + stbi__f2f(-2.562915447f));
   __m128i rot2_0 = dct_const(stbi__f2f(-1.961570560f) + stbi__f2f( 0.298631336f), stbi__f2f(-1.961570560f));
   __m128i rot2_1 = dct_const(stbi__f2f(-1.961570560f), stbi__f2f(-1.961570560f) + stbi__f2f( 3.072711026f));
   __m128i rot3_0 = dct_const(stbi__f2f(-0.390180644f) + stbi__f2f( 2.053119869f), stbi__f2f(-0.390180644f));
   __m128i rot3_1 = dct_const(stbi__f2f(-0.390180644f), stbi__f2f(-0.390180644f) + stbi__f2f( 1.501321110f));

   // rounding biases in column/row passes, see stbi__idct_block for explanation.
   __m128i bias_0 = _mm_set1_epi32(512);
   __m128i bias_1 = _mm_set1_epi32(65536 + (128<<17));

   // load
   row0 = _mm_load_si128((const __m128i *) (data + 0*8));
   row1 = _mm_load_si128((const __m128i *) (data + 1*8));
   row2 = _mm_load_si128((const __m128i *) (data + 2*8));
   row3 = _mm_load_si128((const __m128i *) (data + 3*8));
   row4 = _mm_load_si128((const __m128i *) (data + 4*8));
   row5 = _mm_load_si128((const __m128i *) (data + 5*8));
   row6 = _mm_load_si128((const __m128i *) (data + 6*8));
   row7 = _mm_load_si128((const __m128i *) (data + 7*8));

   // column pass
   dct_pass(bias_0, 10);

   {
      // 16bit 8x8 transpose pass 1
      dct_interleave16(row0, row4);
      dct_interleave16(row1, row5);
      dct_interleave16(row2, row6);
      dct_interleave16(row3, row7);

      // transpose pass 2
      dct_interleave16(row0, row2);
      dct_interleave16(row1, row3);
      dct_interleave16(row4, row6);
      dct_interleave16(row5, row7);

      // transpose pass 3
      dct_interleave16(row0, row1);
      dct_interleave16(row2, row3);
      dct_interleave16(row4, row5);
      dct_interleave16(row6, row7);
   }

   // row pass
   dct_pass(bias_1, 17);

   {
      // pack
      __m128i p0 = _mm_packus_epi16(row0, row1); // a0a1a2a3...a7b0b1b2b3...b7
      __m128i p1 = _mm_packus_epi16(row2, row3);
      __m128i p2 = _mm_packus_epi16(row4, row5);
      __m128i p3 = _mm_packus_epi16(row6, row7);

      // 8bit 8x8 transpose pass 1
      dct_interleave8(p0, p2); // a0e0a1e1...
      dct_interleave8(p1, p3); // c0g0c1g1...

      // transpose pass 2
      dct_interleave8(p0, p1); // a0c0e0g0...
      dct_interleave8(p2, p3); // b0d0f0h0...

      // transpose pass 3
      dct_interleave8(p0, p2); // a0b0c0d0...
      dct_interleave8(p1, p3); // a4b4c4d4...

      // store
      _mm_storel_epi64((__m128i *) out, p0); out += out_stride;
      _mm_storel_epi64((__m128i *) out, _mm_shuffle_epi32(p0, 0x4e)); out += out_stride;
      _mm_storel_epi64((__m128i *) out, p2); out += out_stride;
      _mm_storel_epi64((__m128i *) out, _mm_shuffle_epi32(p2, 0x4e)); out += out_stride;
      _mm_storel_epi64((__m128i *) out, p1); out += out_stride;
      _mm_storel_epi64((__m128i *) out, _mm_shuffle_epi32(p1, 0x4e)); out += out_stride;
      _mm_storel_epi64((__m128i *) out, p3); out += out_stride;
      _mm_storel_epi64((__m128i *) out, _mm_shuffle_epi32(p3, 0x4e));
   }

#undef dct_const
#undef dct_rot
#undef dct_widen
#undef dct_wadd
#undef dct_wsub
#undef dct_bfly32o
#undef dct_interleave8
#undef dct_interleave16
#undef dct_pass
}

#endif // STBI_SSE2

#ifdef STBI_NEON

// NEON integer IDCT. should produce bit-identical
// results to the generic C version.
static void stbi__idct_simd(stbi_uc *out, int out_stride, short data[64])
{
   int16x8_t row0, row1, row2, row3, row4, row5, row6, row7;

   int16x4_t rot0_0 = vdup_n_s16(stbi__f2f(0.5411961f));
   int16x4_t rot0_1 = vdup_n_s16(stbi__f2f(-1.847759065f));
   int16x4_t rot0_2 = vdup_n_s16(stbi__f2f( 0.765366865f));
   int16x4_t rot1_0 = vdup_n_s16(stbi__f2f( 1.175875602f));
   int16x4_t rot1_1 = vdup_n_s16(stbi__f2f(-0.899976223f));
   int16x4_t rot1_2 = vdup_n_s16(stbi__f2f(-2.562915447f));
   int16x4_t rot2_0 = vdup_n_s16(stbi__f2f(-1.961570560f));
   int16x4_t rot2_1 = vdup_n_s16(stbi__f2f(-0.390180644f));
   int16x4_t rot3_0 = vdup_n_s16(stbi__f2f( 0.298631336f));
   int16x4_t rot3_1 = vdup_n_s16(stbi__f2f( 2.053119869f));
   int16x4_t rot3_2 = vdup_n_s16(stbi__f2f( 3.072711026f));
   int16x4_t rot3_3 = vdup_n_s16(stbi__f2f( 1.501321110f));

#define dct_long_mul(out, inq, coeff) \
   int32x4_t out##_l = vmull_s16(vget_low_s16(inq), coeff); \
   int32x4_t out##_h = vmull_s16(vget_high_s16(inq), coeff)

#define dct_long_mac(out, acc, inq, coeff) \
   int32x4_t out##_l = vmlal_s16(acc##_l, vget_low_s16(inq), coeff); \
   int32x4_t out##_h = vmlal_s16(acc##_h, vget_high_s16(inq), coeff)

#define dct_widen(out, inq) \
   int32x4_t out##_l = vshll_n_s16(vget_low_s16(inq), 12); \
   int32x4_t out##_h = vshll_n_s16(vget_high_s16(inq), 12)

// wide add
#define dct_wadd(out, a, b) \
   int32x4_t out##_l = vaddq_s32(a##_l, b##_l); \
   int32x4_t out##_h = vaddq_s32(a##_h, b##_h)

// wide sub
#define dct_wsub(out, a, b) \
   int32x4_t out##_l = vsubq_s32(a##_l, b##_l); \
   int32x4_t out##_h = vsubq_s32(a##_h, b##_h)

// butterfly a/b, then shift using "shiftop" by "s" and pack
#define dct_bfly32o(out0,out1, a,b,shiftop,s) \
   { \
      dct_wadd(sum, a, b); \
      dct_wsub(dif, a, b); \
      out0 = vcombine_s16(shiftop(sum_l, s), shiftop(sum_h, s)); \
      out1 = vcombine_s16(shiftop(dif_l, s), shiftop(dif_h, s)); \
   }

#define dct_pass(shiftop, shift) \
   { \
      /* even part */ \
      int16x8_t sum26 = vaddq_s16(row2, row6); \
      dct_long_mul(p1e, sum26, rot0_0); \
      dct_long_mac(t2e, p1e, row6, rot0_1); \
      dct_long_mac(t3e, p1e, row2, rot0_2); \
      int16x8_t sum04 = vaddq_s16(row0, row4); \
      int16x8_t dif04 = vsubq_s16(row0, row4); \
      dct_widen(t0e, sum04); \
      dct_widen(t1e, dif04); \
      dct_wadd(x0, t0e, t3e); \
      dct_wsub(x3, t0e, t3e); \
      dct_wadd(x1, t1e, t2e); \
      dct_wsub(x2, t1e, t2e); \
      /* odd part */ \
      int16x8_t sum15 = vaddq_s16(row1, row5); \
      int16x8_t sum17 = vaddq_s16(row1, row7); \
      int16x8_t sum35 = vaddq_s16(row3, row5); \
      int16x8_t sum37 = vaddq_s16(row3, row7); \
      int16x8_t sumodd = vaddq_s16(sum17, sum35); \
      dct_long_mul(p5o, sumodd, rot1_0); \
      dct_long_mac(p1o, p5o, sum17, rot1_1); \
      dct_long_mac(p2o, p5o, sum35, rot1_2); \
      dct_long_mul(p3o, sum37, rot2_0); \
      dct_long_mul(p4o, sum15, rot2_1); \
      dct_wadd(sump13o, p1o, p3o); \
      dct_wadd(sump24o, p2o, p4o); \
      dct_wadd(sump23o, p2o, p3o); \
      dct_wadd(sump14o, p1o, p4o); \
      dct_long_mac(x4, sump13o, row7, rot3_0); \
      dct_long_mac(x5, sump24o, row5, rot3_1); \
      dct_long_mac(x6, sump23o, row3, rot3_2); \
      dct_long_mac(x7, sump14o, row1, rot3_3); \
      dct_bfly32o(row0,row7, x0,x7,shiftop,shift); \
      dct_bfly32o(row1,row6, x1,x6,shiftop,shift); \
      dct_bfly32o(row2,row5, x2,x5,shiftop,shift); \
      dct_bfly32o(row3,row4, x3,x4,shiftop,shift); \
   }

   // load
   row0 = vld1q_s16(data + 0*8);
   row1 = vld1q_s16(data + 1*8);
   row2 = vld1q_s16(data + 2*8);
   row3 = vld1q_s16(data + 3*8);
   row4 = vld1q_s16(data + 4*8);
   row5 = vld1q_s16(data + 5*8);
   row6 = vld1q_s16(data + 6*8);
   row7 = vld1q_s16(data + 7*8);

   // add DC bias
   row0 = vaddq_s16(row0, vsetq_lane_s16(1024, vdupq_n_s16(0), 0));

   // column pass
   dct_pass(vrshrn_n_s32, 10);

   // 16bit 8x8 transpose
   {
// these three map to a single VTRN.16, VTRN.32, and VSWP, respectively.
// whether compilers actually get this is another story, sadly.
#define dct_trn16(x, y) { int16x8x2_t t = vtrnq_s16(x, y); x = t.val[0]; y = t.val[1]; }
#define dct_trn32(x, y) { int32x4x2_t t = vtrnq_s32(vreinterpretq_s32_s16(x), vreinterpretq_s32_s16(y)); x = vreinterpretq_s16_s32(t.val[0]); y = vreinterpretq_s16_s32(t.val[1]); }
#define dct_trn64(x, y) { int16x8_t x0 = x; int16x8_t y0 = y; x = vcombine_s16(vget_low_s16(x0), vget_low_s16(y0)); y = vcombine_s16(vget_high_s16(x0), vget_high_s16(y0)); }

      // pass 1
      dct_trn16(row0, row1); // a0b0a2b2a4b4a6b6
      dct_trn16(row2, row3);
      dct_trn16(row4, row5);
      dct_trn16(row6, row7);

      // pass 2
      dct_trn32(row0, row2); // a0b0c0d0a4b4c4d4
      dct_trn32(row1, row3);
      dct_trn32(row4, row6);
      dct_trn32(row5, row7);

      // pass 3
      dct_trn64(row0, row4); // a0b0c0d0e0f0g0h0
      dct_trn64(row1, row5);
      dct_trn64(row2, row6);
      dct_trn64(row3, row7);

#undef dct_trn16
#undef dct_trn32
#undef dct_trn64
   }

   // row pass
   // vrshrn_n_s32 only supports shifts up to 16, we need
   // 17. so do a non-rounding shift of 16 first then follow
   // up with a rounding shift by 1.
   dct_pass(vshrn_n_s32, 16);

   {
      // pack and round
      uint8x8_t p0 = vqrshrun_n_s16(row0, 1);
      uint8x8_t p1 = vqrshrun_n_s16(row1, 1);
      uint8x8_t p2 = vqrshrun_n_s16(row2, 1);
      uint8x8_t p3 = vqrshrun_n_s16(row3, 1);
      uint8x8_t p4 = vqrshrun_n_s16(row4, 1);
      uint8x8_t p5 = vqrshrun_n_s16(row5, 1);
      uint8x8_t p6 = vqrshrun_n_s16(row6, 1);
      uint8x8_t p7 = vqrshrun_n_s16(row7, 1);

      // again, these can translate into one instruction, but often don't.
#define dct_trn8_8(x, y) { uint8x8x2_t t = vtrn_u8(x, y); x = t.val[0]; y = t.val[1]; }
#define dct_trn8_16(x, y) { uint16x4x2_t t = vtrn_u16(vreinterpret_u16_u8(x), vreinterpret_u16_u8(y)); x = vreinterpret_u8_u16(t.val[0]); y = vreinterpret_u8_u16(t.val[1]); }
#define dct_trn8_32(x, y) { uint32x2x2_t t = vtrn_u32(vreinterpret_u32_u8(x), vreinterpret_u32_u8(y)); x = vreinterpret_u8_u32(t.val[0]); y = vreinterpret_u8_u32(t.val[1]); }

      // sadly can't use interleaved stores here since we only write
      // 8 bytes to each scan line!

      // 8x8 8-bit transpose pass 1
      dct_trn8_8(p0, p1);
      dct_trn8_8(p2, p3);
      dct_trn8_8(p4, p5);
      dct_trn8_8(p6, p7);

      // pass 2
      dct_trn8_16(p0, p2);
      dct_trn8_16(p1, p3);
      dct_trn8_16(p4, p6);
      dct_trn8_16(p5, p7);

      // pass 3
      dct_trn8_32(p0, p4);
      dct_trn8_32(p1, p5);
      dct_trn8_32(p2, p6);
      dct_trn8_32(p3, p7);

      // store
      vst1_u8(out, p0); out += out_stride;
      vst1_u8(out, p1); out += out_stride;
      vst1_u8(out, p2); out += out_stride;
      vst1_u8(out, p3); out += out_stride;
      vst1_u8(out, p4); out += out_stride;
      vst1_u8(out, p5); out += out_stride;
      vst1_u8(out, p6); out += out_stride;
      vst1_u8(out, p7);

#undef dct_trn8_8
#undef dct_trn8_16
#undef dct_trn8_32
   }

#undef dct_long_mul
#undef dct_long_mac
#undef dct_widen
#undef dct_wadd
#undef dct_wsub
#undef dct_bfly32o
#undef dct_pass
}

#endif // STBI_NEON

#define STBI__MARKER_none  0xff
// if there's a pending marker from the entropy stream, return that
// otherwise, fetch from the stream and get a marker. if there's no
// marker, return 0xff, which is never a valid marker value
static stbi_uc stbi__get_marker(stbi__jpeg *j)
{
   stbi_uc x;
   if (j->marker != STBI__MARKER_none) { x = j->marker; j->marker = STBI__MARKER_none; return x; }
   x = stbi__get8(j->s);
   if (x != 0xff) return STBI__MARKER_none;
   while (x == 0xff)
      x = stbi__get8(j->s); // consume repeated 0xff fill bytes
   return x;
}

// in each scan, we'll have scan_n components, and the order
// of the components is specified by order[]
#define STBI__RESTART(x)     ((x) >= 0xd0 && (x) <= 0xd7)

// after a restart interval, stbi__jpeg_reset the entropy decoder and
// the dc prediction
static void stbi__jpeg_reset(stbi__jpeg *j)
{
   j->code_bits = 0;
   j->code_buffer = 0;
   j->nomore = 0;
   j->img_comp[0].dc_pred = j->img_comp[1].dc_pred = j->img_comp[2].dc_pred = j->img_comp[3].dc_pred = 0;
   j->marker = STBI__MARKER_none;
   j->todo = j->restart_interval ? j->restart_interval : 0x7fffffff;
   j->eob_run = 0;
   // no more than 1<<31 MCUs if no restart_interal? that's plenty safe,
   // since we don't even allow 1<<30 pixels
}

static int stbi__parse_entropy_coded_data(stbi__jpeg *z)
{
   stbi__jpeg_reset(z);
   if (!z->progressive) {
      if (z->scan_n == 1) {
         int i,j;
         STBI_SIMD_ALIGN(short, data[64]);
         int n = z->order[0];
         // non-interleaved data, we just need to process one block at a time,
         // in trivial scanline order
         // number of blocks to do just depends on how many actual "pixels" this
         // component has, independent of interleaved MCU blocking and such
         int w = (z->img_comp[n].x+7) >> 3;
         int h = (z->img_comp[n].y+7) >> 3;
         for (j=0; j < h; ++j) {
            for (i=0; i < w; ++i) {
               int ha = z->img_comp[n].ha;
               if (!stbi__jpeg_decode_block(z, data, z->huff_dc+z->img_comp[n].hd, z->huff_ac+ha, z->fast_ac[ha], n, z->dequant[z->img_comp[n].tq])) return 0;
               z->idct_block_kernel(z->img_comp[n].data+z->img_comp[n].w2*j*8+i*8, z->img_comp[n].w2, data);
               // every data block is an MCU, so countdown the restart interval
               if (--z->todo <= 0) {
                  if (z->code_bits < 24) stbi__grow_buffer_unsafe(z);
                  // if it's NOT a restart, then just bail, so we get corrupt data
                  // rather than no data
                  if (!STBI__RESTART(z->marker)) return 1;
                  stbi__jpeg_reset(z);
               }
            }
         }
         return 1;
      } else { // interleaved
         int i,j,k,x,y;
         STBI_SIMD_ALIGN(short, data[64]);
         for (j=0; j < z->img_mcu_y; ++j) {
            for (i=0; i < z->img_mcu_x; ++i) {
               // scan an interleaved mcu... process scan_n components in order
               for (k=0; k < z->scan_n; ++k) {
                  int n = z->order[k];
                  // scan out an mcu's worth of this component; that's just determined
                  // by the basic H and V specified for the component
                  for (y=0; y < z->img_comp[n].v; ++y) {
                     for (x=0; x < z->img_comp[n].h; ++x) {
                        int x2 = (i*z->img_comp[n].h + x)*8;
                        int y2 = (j*z->img_comp[n].v + y)*8;
                        int ha = z->img_comp[n].ha;
                        if (!stbi__jpeg_decode_block(z, data, z->huff_dc+z->img_comp[n].hd, z->huff_ac+ha, z->fast_ac[ha], n, z->dequant[z->img_comp[n].tq])) return 0;
                        z->idct_block_kernel(z->img_comp[n].data+z->img_comp[n].w2*y2+x2, z->img_comp[n].w2, data);
                     }
                  }
               }
               // after all interleaved components, that's an interleaved MCU,
               // so now count down the restart interval
               if (--z->todo <= 0) {
                  if (z->code_bits < 24) stbi__grow_buffer_unsafe(z);
                  if (!STBI__RESTART(z->marker)) return 1;
                  stbi__jpeg_reset(z);
               }
            }
         }
         return 1;
      }
   } else {
      if (z->scan_n == 1) {
         int i,j;
         int n = z->order[0];
         // non-interleaved data, we just need to process one block at a time,
         // in trivial scanline order
         // number of blocks to do just depends on how many actual "pixels" this
         // component has, independent of interleaved MCU blocking and such
         int w = (z->img_comp[n].x+7) >> 3;
         int h = (z->img_comp[n].y+7) >> 3;
         for (j=0; j < h; ++j) {
            for (i=0; i < w; ++i) {
               short *data = z->img_comp[n].coeff + 64 * (i + j * z->img_comp[n].coeff_w);
               if (z->spec_start == 0) {
                  if (!stbi__jpeg_decode_block_prog_dc(z, data, &z->huff_dc[z->img_comp[n].hd], n))
                     return 0;
               } else {
                  int ha = z->img_comp[n].ha;
                  if (!stbi__jpeg_decode_block_prog_ac(z, data, &z->huff_ac[ha], z->fast_ac[ha]))
                     return 0;
               }
               // every data block is an MCU, so countdown the restart interval
               if (--z->todo <= 0) {
                  if (z->code_bits < 24) stbi__grow_buffer_unsafe(z);
                  if (!STBI__RESTART(z->marker)) return 1;
                  stbi__jpeg_reset(z);
               }
            }
         }
         return 1;
      } else { // interleaved
         int i,j,k,x,y;
         for (j=0; j < z->img_mcu_y; ++j) {
            for (i=0; i < z->img_mcu_x; ++i) {
               // scan an interleaved mcu... process scan_n components in order
               for (k=0; k < z->scan_n; ++k) {
                  int n = z->order[k];
                  // scan out an mcu's worth of this component; that's just determined
                  // by the basic H and V specified for the component
                  for (y=0; y < z->img_comp[n].v; ++y) {
                     for (x=0; x < z->img_comp[n].h; ++x) {
                        int x2 = (i*z->img_comp[n].h + x);
                        int y2 = (j*z->img_comp[n].v + y);
                        short *data = z->img_comp[n].coeff + 64 * (x2 + y2 * z->img_comp[n].coeff_w);
                        if (!stbi__jpeg_decode_block_prog_dc(z, data, &z->huff_dc[z->img_comp[n].hd], n))
                           return 0;
                     }
                  }
               }
               // after all interleaved components, that's an interleaved MCU,
               // so now count down the restart interval
               if (--z->todo <= 0) {
                  if (z->code_bits < 24) stbi__grow_buffer_unsafe(z);
                  if (!STBI__RESTART(z->marker)) return 1;
                  stbi__jpeg_reset(z);
               }
            }
         }
         return 1;
      }
   }
}

static void stbi__jpeg_dequantize(short *data, stbi__uint16 *dequant)
{
   int i;
   for (i=0; i < 64; ++i)
      data[i] *= dequant[i];
}

static void stbi__jpeg_finish(stbi__jpeg *z)
{
   if (z->progressive) {
      // dequantize and idct the data
      int i,j,n;
      for (n=0; n < z->s->img_n; ++n) {
         int w = (z->img_comp[n].x+7) >> 3;
         int h = (z->img_comp[n].y+7) >> 3;
         for (j=0; j < h; ++j) {
            for (i=0; i < w; ++i) {
               short *data = z->img_comp[n].coeff + 64 * (i + j * z->img_comp[n].coeff_w);
               stbi__jpeg_dequantize(data, z->dequant[z->img_comp[n].tq]);
               z->idct_block_kernel(z->img_comp[n].data+z->img_comp[n].w2*j*8+i*8, z->img_comp[n].w2, data);
            }
         }
      }
   }
}

static int stbi__process_marker(stbi__jpeg *z, int m)
{
   int L;
   switch (m) {
      case STBI__MARKER_none: // no marker found
         return stbi__err("expected marker","Corrupt JPEG");

      case 0xDD: // DRI - specify restart interval
         if (stbi__get16be(z->s) != 4) return stbi__err("bad DRI len","Corrupt JPEG");
         z->restart_interval = stbi__get16be(z->s);
         return 1;

      case 0xDB: // DQT - define quantization table
         L = stbi__get16be(z->s)-2;
         while (L > 0) {
            int q = stbi__get8(z->s);
            int p = q >> 4, sixteen = (p != 0);
            int t = q & 15,i;
            if (p != 0 && p != 1) return stbi__err("bad DQT type","Corrupt JPEG");
            if (t > 3) return stbi__err("bad DQT table","Corrupt JPEG");

            for (i=0; i < 64; ++i)
               z->dequant[t][stbi__jpeg_dezigzag[i]] = (stbi__uint16)(sixteen ? stbi__get16be(z->s) : stbi__get8(z->s));
            L -= (sixteen ? 129 : 65);
         }
         return L==0;

      case 0xC4: // DHT - define huffman table
         L = stbi__get16be(z->s)-2;
         while (L > 0) {
            stbi_uc *v;
            int sizes[16],i,n=0;
            int q = stbi__get8(z->s);
            int tc = q >> 4;
            int th = q & 15;
            if (tc > 1 || th > 3) return stbi__err("bad DHT header","Corrupt JPEG");
            for (i=0; i < 16; ++i) {
               sizes[i] = stbi__get8(z->s);
               n += sizes[i];
            }
            if(n > 256) return stbi__err("bad DHT header","Corrupt JPEG"); // Loop over i < n would write past end of values!
            L -= 17;
            if (tc == 0) {
               if (!stbi__build_huffman(z->huff_dc+th, sizes)) return 0;
               v = z->huff_dc[th].values;
            } else {
               if (!stbi__build_huffman(z->huff_ac+th, sizes)) return 0;
               v = z->huff_ac[th].values;
            }
            for (i=0; i < n; ++i)
               v[i] = stbi__get8(z->s);
            if (tc != 0)
               stbi__build_fast_ac(z->fast_ac[th], z->huff_ac + th);
            L -= n;
         }
         return L==0;
   }

   // check for comment block or APP blocks
   if ((m >= 0xE0 && m <= 0xEF) || m == 0xFE) {
      L = stbi__get16be(z->s);
      if (L < 2) {
         if (m == 0xFE)
            return stbi__err("bad COM len","Corrupt JPEG");
         else
            return stbi__err("bad APP len","Corrupt JPEG");
      }
      L -= 2;

      if (m == 0xE0 && L >= 5) { // JFIF APP0 segment
         static const unsigned char tag[5] = {'J','F','I','F','\0'};
         int ok = 1;
         int i;
         for (i=0; i < 5; ++i)
            if (stbi__get8(z->s) != tag[i])
               ok = 0;
         L -= 5;
         if (ok)
            z->jfif = 1;
      } else if (m == 0xEE && L >= 12) { // Adobe APP14 segment
         static const unsigned char tag[6] = {'A','d','o','b','e','\0'};
         int ok = 1;
         int i;
         for (i=0; i < 6; ++i)
            if (stbi__get8(z->s) != tag[i])
               ok = 0;
         L -= 6;
         if (ok) {
            stbi__get8(z->s); // version
            stbi__get16be(z->s); // flags0
            stbi__get16be(z->s); // flags1
            z->app14_color_transform = stbi__get8(z->s); // color transform
            L -= 6;
         }
      }

      stbi__skip(z->s, L);
      return 1;
   }

   return stbi__err("unknown marker","Corrupt JPEG");
}

// after we see SOS
static int stbi__process_scan_header(stbi__jpeg *z)
{
   int i;
   int Ls = stbi__get16be(z->s);
   z->scan_n = stbi__get8(z->s);
   if (z->scan_n < 1 || z->scan_n > 4 || z->scan_n > (int) z->s->img_n) return stbi__err("bad SOS component count","Corrupt JPEG");
   if (Ls != 6+2*z->scan_n) return stbi__err("bad SOS len","Corrupt JPEG");
   for (i=0; i < z->scan_n; ++i) {
      int id = stbi__get8(z->s), which;
      int q = stbi__get8(z->s);
      for (which = 0; which < z->s->img_n; ++which)
         if (z->img_comp[which].id == id)
            break;
      if (which == z->s->img_n) return 0; // no match
      z->img_comp[which].hd = q >> 4;   if (z->img_comp[which].hd > 3) return stbi__err("bad DC huff","Corrupt JPEG");
      z->img_comp[which].ha = q & 15;   if (z->img_comp[which].ha > 3) return stbi__err("bad AC huff","Corrupt JPEG");
      z->order[i] = which;
   }

   {
      int aa;
      z->spec_start = stbi__get8(z->s);
      z->spec_end   = stbi__get8(z->s); // should be 63, but might be 0
      aa = stbi__get8(z->s);
      z->succ_high = (aa >> 4);
      z->succ_low  = (aa & 15);
      if (z->progressive) {
         if (z->spec_start > 63 || z->spec_end > 63  || z->spec_start > z->spec_end || z->succ_high > 13 || z->succ_low > 13)
            return stbi__err("bad SOS", "Corrupt JPEG");
      } else {
         if (z->spec_start != 0) return stbi__err("bad SOS","Corrupt JPEG");
         if (z->succ_high != 0 || z->succ_low != 0) return stbi__err("bad SOS","Corrupt JPEG");
         z->spec_end = 63;
      }
   }

   return 1;
}

static int stbi__free_jpeg_components(stbi__jpeg *z, int ncomp, int why)
{
   int i;
   for (i=0; i < ncomp; ++i) {
      if (z->img_comp[i].raw_data) {
         STBI_FREE(z->img_comp[i].raw_data);
         z->img_comp[i].raw_data = NULL;
         z->img_comp[i].data = NULL;
      }
      if (z->img_comp[i].raw_coeff) {
         STBI_FREE(z->img_comp[i].raw_coeff);
         z->img_comp[i].raw_coeff = 0;
         z->img_comp[i].coeff = 0;
      }
      if (z->img_comp[i].linebuf) {
         STBI_FREE(z->img_comp[i].linebuf);
         z->img_comp[i].linebuf = NULL;
      }
   }
   return why;
}

static int stbi__process_frame_header(stbi__jpeg *z, int scan)
{
   stbi__context *s = z->s;
   int Lf,p,i,q, h_max=1,v_max=1,c;
   Lf = stbi__get16be(s);         if (Lf < 11) return stbi__err("bad SOF len","Corrupt JPEG"); // JPEG
   p  = stbi__get8(s);            if (p != 8) return stbi__err("only 8-bit","JPEG format not supported: 8-bit only"); // JPEG baseline
   s->img_y = stbi__get16be(s);   if (s->img_y == 0) return stbi__err("no header height", "JPEG format not supported: delayed height"); // Legal, but we don't handle it--but neither does IJG
   s->img_x = stbi__get16be(s);   if (s->img_x == 0) return stbi__err("0 width","Corrupt JPEG"); // JPEG requires
   if (s->img_y > STBI_MAX_DIMENSIONS) return stbi__err("too large","Very large image (corrupt?)");
   if (s->img_x > STBI_MAX_DIMENSIONS) return stbi__err("too large","Very large image (corrupt?)");
   c = stbi__get8(s);
   if (c != 3 && c != 1 && c != 4) return stbi__err("bad component count","Corrupt JPEG");
   s->img_n = c;
   for (i=0; i < c; ++i) {
      z->img_comp[i].data = NULL;
      z->img_comp[i].linebuf = NULL;
   }

   if (Lf != 8+3*s->img_n) return stbi__err("bad SOF len","Corrupt JPEG");

   z->rgb = 0;
   for (i=0; i < s->img_n; ++i) {
      static const unsigned char rgb[3] = { 'R', 'G', 'B' };
      z->img_comp[i].id = stbi__get8(s);
      if (s->img_n == 3 && z->img_comp[i].id == rgb[i])
         ++z->rgb;
      q = stbi__get8(s);
      z->img_comp[i].h = (q >> 4);  if (!z->img_comp[i].h || z->img_comp[i].h > 4) return stbi__err("bad H","Corrupt JPEG");
      z->img_comp[i].v = q & 15;    if (!z->img_comp[i].v || z->img_comp[i].v > 4) return stbi__err("bad V","Corrupt JPEG");
      z->img_comp[i].tq = stbi__get8(s);  if (z->img_comp[i].tq > 3) return stbi__err("bad TQ","Corrupt JPEG");
   }

   if (scan != STBI__SCAN_load) return 1;

   if (!stbi__mad3sizes_valid(s->img_x, s->img_y, s->img_n, 0)) return stbi__err("too large", "Image too large to decode");

   for (i=0; i < s->img_n; ++i) {
      if (z->img_comp[i].h > h_max) h_max = z->img_comp[i].h;
      if (z->img_comp[i].v > v_max) v_max = z->img_comp[i].v;
   }

   // check that plane subsampling factors are integer ratios; our resamplers can't deal with fractional ratios
   // and I've never seen a non-corrupted JPEG file actually use them
   for (i=0; i < s->img_n; ++i) {
      if (h_max % z->img_comp[i].h != 0) return stbi__err("bad H","Corrupt JPEG");
      if (v_max % z->img_comp[i].v != 0) return stbi__err("bad V","Corrupt JPEG");
   }

   // compute interleaved mcu info
   z->img_h_max = h_max;
   z->img_v_max = v_max;
   z->img_mcu_w = h_max * 8;
   z->img_mcu_h = v_max * 8;
   // these sizes can't be more than 17 bits
   z->img_mcu_x = (s->img_x + z->img_mcu_w-1) / z->img_mcu_w;
   z->img_mcu_y = (s->img_y + z->img_mcu_h-1) / z->img_mcu_h;

   for (i=0; i < s->img_n; ++i) {
      // number of effective pixels (e.g. for non-interleaved MCU)
      z->img_comp[i].x = (s->img_x * z->img_comp[i].h + h_max-1) / h_max;
      z->img_comp[i].y = (s->img_y * z->img_comp[i].v + v_max-1) / v_max;
      // to simplify generation, we'll allocate enough memory to decode
      // the bogus oversized data from using interleaved MCUs and their
      // big blocks (e.g. a 16x16 iMCU on an image of width 33); we won't
      // discard the extra data until colorspace conversion
      //
      // img_mcu_x, img_mcu_y: <=17 bits; comp[i].h and .v are <=4 (checked earlier)
      // so these muls can't overflow with 32-bit ints (which we require)
      z->img_comp[i].w2 = z->img_mcu_x * z->img_comp[i].h * 8;
      z->img_comp[i].h2 = z->img_mcu_y * z->img_comp[i].v * 8;
      z->img_comp[i].coeff = 0;
      z->img_comp[i].raw_coeff = 0;
      z->img_comp[i].linebuf = NULL;
      z->img_comp[i].raw_data = stbi__malloc_mad2(z->img_comp[i].w2, z->img_comp[i].h2, 15);
      if (z->img_comp[i].raw_data == NULL)
         return stbi__free_jpeg_components(z, i+1, stbi__err("outofmem", "Out of memory"));
      // align blocks for idct using mmx/sse
      z->img_comp[i].data = (stbi_uc*) (((size_t) z->img_comp[i].raw_data + 15) & ~15);
      if (z->progressive) {
         // w2, h2 are multiples of 8 (see above)
         z->img_comp[i].coeff_w = z->img_comp[i].w2 / 8;
         z->img_comp[i].coeff_h = z->img_comp[i].h2 / 8;
         z->img_comp[i].raw_coeff = stbi__malloc_mad3(z->img_comp[i].w2, z->img_comp[i].h2, sizeof(short), 15);
         if (z->img_comp[i].raw_coeff == NULL)
            return stbi__free_jpeg_components(z, i+1, stbi__err("outofmem", "Out of memory"));
         z->img_comp[i].coeff = (short*) (((size_t) z->img_comp[i].raw_coeff + 15) & ~15);
      }
   }

   return 1;
}

// use comparisons since in some cases we handle more than one case (e.g. SOF)
#define stbi__DNL(x)         ((x) == 0xdc)
#define stbi__SOI(x)         ((x) == 0xd8)
#define stbi__EOI(x)         ((x) == 0xd9)
#define stbi__SOF(x)         ((x) == 0xc0 || (x) == 0xc1 || (x) == 0xc2)
#define stbi__SOS(x)         ((x) == 0xda)

#define stbi__SOF_progressive(x)   ((x) == 0xc2)

static int stbi__decode_jpeg_header(stbi__jpeg *z, int scan)
{
   int m;
   z->jfif = 0;
   z->app14_color_transform = -1; // valid values are 0,1,2
   z->marker = STBI__MARKER_none; // initialize cached marker to empty
   m = stbi__get_marker(z);
   if (!stbi__SOI(m)) return stbi__err("no SOI","Corrupt JPEG");
   if (scan == STBI__SCAN_type) return 1;
   m = stbi__get_marker(z);
   while (!stbi__SOF(m)) {
      if (!stbi__process_marker(z,m)) return 0;
      m = stbi__get_marker(z);
      while (m == STBI__MARKER_none) {
         // some files have extra padding after their blocks, so ok, we'll scan
         if (stbi__at_eof(z->s)) return stbi__err("no SOF", "Corrupt JPEG");
         m = stbi__get_marker(z);
      }
   }
   z->progressive = stbi__SOF_progressive(m);
   if (!stbi__process_frame_header(z, scan)) return 0;
   return 1;
}

static stbi_uc stbi__skip_jpeg_junk_at_end(stbi__jpeg *j)
{
   // some JPEGs have junk at end, skip over it but if we find what looks
   // like a valid marker, resume there
   while (!stbi__at_eof(j->s)) {
      stbi_uc x = stbi__get8(j->s);
      while (x == 0xff) { // might be a marker
         if (stbi__at_eof(j->s)) return STBI__MARKER_none;
         x = stbi__get8(j->s);
         if (x != 0x00 && x != 0xff) {
            // not a stuffed zero or lead-in to another marker, looks
            // like an actual marker, return it
            return x;
         }
         // stuffed zero has x=0 now which ends the loop, meaning we go
         // back to regular scan loop.
         // repeated 0xff keeps trying to read the next byte of the marker.
      }
   }
   return STBI__MARKER_none;
}

// decode image to YCbCr format
static int stbi__decode_jpeg_image(stbi__jpeg *j)
{
   int m;
   for (m = 0; m < 4; m++) {
      j->img_comp[m].raw_data = NULL;
      j->img_comp[m].raw_coeff = NULL;
   }
   j->restart_interval = 0;
   if (!stbi__decode_jpeg_header(j, STBI__SCAN_load)) return 0;
   m = stbi__get_marker(j);
   while (!stbi__EOI(m)) {
      if (stbi__SOS(m)) {
         if (!stbi__process_scan_header(j)) return 0;
         if (!stbi__parse_entropy_coded_data(j)) return 0;
         if (j->marker == STBI__MARKER_none ) {
         j->marker = stbi__skip_jpeg_junk_at_end(j);
            // if we reach eof without hitting a marker, stbi__get_marker() below will fail and we'll eventually return 0
         }
         m = stbi__get_marker(j);
         if (STBI__RESTART(m))
            m = stbi__get_marker(j);
      } else if (stbi__DNL(m)) {
         int Ld = stbi__get16be(j->s);
         stbi__uint32 NL = stbi__get16be(j->s);
         if (Ld != 4) return stbi__err("bad DNL len", "Corrupt JPEG");
         if (NL != j->s->img_y) return stbi__err("bad DNL height", "Corrupt JPEG");
         m = stbi__get_marker(j);
      } else {
         if (!stbi__process_marker(j, m)) return 1;
         m = stbi__get_marker(j);
      }
   }
   if (j->progressive)
      stbi__jpeg_finish(j);
   return 1;
}

// static jfif-centered resampling (across block boundaries)

typedef stbi_uc *(*resample_row_func)(stbi_uc *out, stbi_uc *in0, stbi_uc *in1,
                                    int w, int hs);

#define stbi__div4(x) ((stbi_uc) ((x) >> 2))

static stbi_uc *resample_row_1(stbi_uc *out, stbi_uc *in_near, stbi_uc *in_far, int w, int hs)
{
   STBI_NOTUSED(out);
   STBI_NOTUSED(in_far);
   STBI_NOTUSED(w);
   STBI_NOTUSED(hs);
   return in_near;
}

static stbi_uc* stbi__resample_row_v_2(stbi_uc *out, stbi_uc *in_near, stbi_uc *in_far, int w, int hs)
{
   // need to generate two samples vertically for every one in input
   int i;
   STBI_NOTUSED(hs);
   for (i=0; i < w; ++i)
      out[i] = stbi__div4(3*in_near[i] + in_far[i] + 2);
   return out;
}

static stbi_uc*  stbi__resample_row_h_2(stbi_uc *out, stbi_uc *in_near, stbi_uc *in_far, int w, int hs)
{
   // need to generate two samples horizontally for every one in input
   int i;
   stbi_uc *input = in_near;

   if (w == 1) {
      // if only one sample, can't do any interpolation
      out[0] = out[1] = input[0];
      return out;
   }

   out[0] = input[0];
   out[1] = stbi__div4(input[0]*3 + input[1] + 2);
   for (i=1; i < w-1; ++i) {
      int n = 3*input[i]+2;
      out[i*2+0] = stbi__div4(n+input[i-1]);
      out[i*2+1] = stbi__div4(n+input[i+1]);
   }
   out[i*2+0] = stbi__div4(input[w-2]*3 + input[w-1] + 2);
   out[i*2+1] = input[w-1];

   STBI_NOTUSED(in_far);
   STBI_NOTUSED(hs);

   return out;
}

#define stbi__div16(x) ((stbi_uc) ((x) >> 4))

static stbi_uc *stbi__resample_row_hv_2(stbi_uc *out, stbi_uc *in_near, stbi_uc *in_far, int w, int hs)
{
   // need to generate 2x2 samples for every one in input
   int i,t0,t1;
   if (w == 1) {
      out[0] = out[1] = stbi__div4(3*in_near[0] + in_far[0] + 2);
      return out;
   }

   t1 = 3*in_near[0] + in_far[0];
   out[0] = stbi__div4(t1+2);
   for (i=1; i < w; ++i) {
      t0 = t1;
      t1 = 3*in_near[i]+in_far[i];
      out[i*2-1] = stbi__div16(3*t0 + t1 + 8);
      out[i*2  ] = stbi__div16(3*t1 + t0 + 8);
   }
   out[w*2-1] = stbi__div4(t1+2);

   STBI_NOTUSED(hs);

   return out;
}

#if defined(STBI_SSE2) || defined(STBI_NEON)
static stbi_uc *stbi__resample_row_hv_2_simd(stbi_uc *out, stbi_uc *in_near, stbi_uc *in_far, int w, int hs)
{
   // need to generate 2x2 samples for every one in input
   int i=0,t0,t1;

   if (w == 1) {
      out[0] = out[1] = stbi__div4(3*in_near[0] + in_far[0] + 2);
      return out;
   }

   t1 = 3*in_near[0] + in_far[0];
   // process groups of 8 pixels for as long as we can.
   // note we can't handle the last pixel in a row in this loop
   // because we need to handle the filter boundary conditions.
   for (; i < ((w-1) & ~7); i += 8) {
#if defined(STBI_SSE2)
      // load and perform the vertical filtering pass
      // this uses 3*x + y = 4*x + (y - x)
      __m128i zero  = _mm_setzero_si128();
      __m128i farb  = _mm_loadl_epi64((__m128i *) (in_far + i));
      __m128i nearb = _mm_loadl_epi64((__m128i *) (in_near + i));
      __m128i farw  = _mm_unpacklo_epi8(farb, zero);
      __m128i nearw = _mm_unpacklo_epi8(nearb, zero);
      __m128i diff  = _mm_sub_epi16(farw, nearw);
      __m128i nears = _mm_slli_epi16(nearw, 2);
      __m128i curr  = _mm_add_epi16(nears, diff); // current row

      // horizontal filter works the same based on shifted vers of current
      // row. "prev" is current row shifted right by 1 pixel; we need to
      // insert the previous pixel value (from t1).
      // "next" is current row shifted left by 1 pixel, with first pixel
      // of next block of 8 pixels added in.
      __m128i prv0 = _mm_slli_si128(curr, 2);
      __m128i nxt0 = _mm_srli_si128(curr, 2);
      __m128i prev = _mm_insert_epi16(prv0, t1, 0);
      __m128i next = _mm_insert_epi16(nxt0, 3*in_near[i+8] + in_far[i+8], 7);

      // horizontal filter, polyphase implementation since it's convenient:
      // even pixels = 3*cur + prev = cur*4 + (prev - cur)
      // odd  pixels = 3*cur + next = cur*4 + (next - cur)
      // note the shared term.
      __m128i bias  = _mm_set1_epi16(8);
      __m128i curs = _mm_slli_epi16(curr, 2);
      __m128i prvd = _mm_sub_epi16(prev, curr);
      __m128i nxtd = _mm_sub_epi16(next, curr);
      __m128i curb = _mm_add_epi16(curs, bias);
      __m128i even = _mm_add_epi16(prvd, curb);
      __m128i odd  = _mm_add_epi16(nxtd, curb);

      // interleave even and odd pixels, then undo scaling.
      __m128i int0 = _mm_unpacklo_epi16(even, odd);
      __m128i int1 = _mm_unpackhi_epi16(even, odd);
      __m128i de0  = _mm_srli_epi16(int0, 4);
      __m128i de1  = _mm_srli_epi16(int1, 4);

      // pack and write output
      __m128i outv = _mm_packus_epi16(de0, de1);
      _mm_storeu_si128((__m128i *) (out + i*2), outv);
#elif defined(STBI_NEON)
      // load and perform the vertical filtering pass
      // this uses 3*x + y = 4*x + (y - x)
      uint8x8_t farb  = vld1_u8(in_far + i);
      uint8x8_t nearb = vld1_u8(in_near + i);
      int16x8_t diff  = vreinterpretq_s16_u16(vsubl_u8(farb, nearb));
      int16x8_t nears = vreinterpretq_s16_u16(vshll_n_u8(nearb, 2));
      int16x8_t curr  = vaddq_s16(nears, diff); // current row

      // horizontal filter works the same based on shifted vers of current
      // row. "prev" is current row shifted right by 1 pixel; we need to
      // insert the previous pixel value (from t1).
      // "next" is current row shifted left by 1 pixel, with first pixel
      // of next block of 8 pixels added in.
      int16x8_t prv0 = vextq_s16(curr, curr, 7);
      int16x8_t nxt0 = vextq_s16(curr, curr, 1);
      int16x8_t prev = vsetq_lane_s16(t1, prv0, 0);
      int16x8_t next = vsetq_lane_s16(3*in_near[i+8] + in_far[i+8], nxt0, 7);

      // horizontal filter, polyphase implementation since it's convenient:
      // even pixels = 3*cur + prev = cur*4 + (prev - cur)
      // odd  pixels = 3*cur + next = cur*4 + (next - cur)
      // note the shared term.
      int16x8_t curs = vshlq_n_s16(curr, 2);
      int16x8_t prvd = vsubq_s16(prev, curr);
      int16x8_t nxtd = vsubq_s16(next, curr);
      int16x8_t even = vaddq_s16(curs, prvd);
      int16x8_t odd  = vaddq_s16(curs, nxtd);

      // undo scaling and round, then store with even/odd phases interleaved
      uint8x8x2_t o;
      o.val[0] = vqrshrun_n_s16(even, 4);
      o.val[1] = vqrshrun_n_s16(odd,  4);
      vst2_u8(out + i*2, o);
#endif

      // "previous" value for next iter
      t1 = 3*in_near[i+7] + in_far[i+7];
   }

   t0 = t1;
   t1 = 3*in_near[i] + in_far[i];
   out[i*2] = stbi__div16(3*t1 + t0 + 8);

   for (++i; i < w; ++i) {
      t0 = t1;
      t1 = 3*in_near[i]+in_far[i];
      out[i*2-1] = stbi__div16(3*t0 + t1 + 8);
      out[i*2  ] = stbi__div16(3*t1 + t0 + 8);
   }
   out[w*2-1] = stbi__div4(t1+2);

   STBI_NOTUSED(hs);

   return out;
}
#endif

static stbi_uc *stbi__resample_row_generic(stbi_uc *out, stbi_uc *in_near, stbi_uc *in_far, int w, int hs)
{
   // resample with nearest-neighbor
   int i,j;
   STBI_NOTUSED(in_far);
   for (i=0; i < w; ++i)
      for (j=0; j < hs; ++j)
         out[i*hs+j] = in_near[i];
   return out;
}

// this is a reduced-precision calculation of YCbCr-to-RGB introduced
// to make sure the code produces the same results in both SIMD and scalar
#define stbi__float2fixed(x)  (((int) ((x) * 4096.0f + 0.5f)) << 8)
static void stbi__YCbCr_to_RGB_row(stbi_uc *out, const stbi_uc *y, const stbi_uc *pcb, const stbi_uc *pcr, int count, int step)
{
   int i;
   for (i=0; i < count; ++i) {
      int y_fixed = (y[i] << 20) + (1<<19); // rounding
      int r,g,b;
      int cr = pcr[i] - 128;
      int cb = pcb[i] - 128;
      r = y_fixed +  cr* stbi__float2fixed(1.40200f);
      g = y_fixed + (cr*-stbi__float2fixed(0.71414f)) + ((cb*-stbi__float2fixed(0.34414f)) & 0xffff0000);
      b = y_fixed                                     +   cb* stbi__float2fixed(1.77200f);
      r >>= 20;
      g >>= 20;
      b >>= 20;
      if ((unsigned) r > 255) { if (r < 0) r = 0; else r = 255; }
      if ((unsigned) g > 255) { if (g < 0) g = 0; else g = 255; }
      if ((unsigned) b > 255) { if (b < 0) b = 0; else b = 255; }
      out[0] = (stbi_uc)r;
      out[1] = (stbi_uc)g;
      out[2] = (stbi_uc)b;
      out[3] = 255;
      out += step;
   }
}

#if defined(STBI_SSE2) || defined(STBI_NEON)
static void stbi__YCbCr_to_RGB_simd(stbi_uc *out, stbi_uc const *y, stbi_uc const *pcb, stbi_uc const *pcr, int count, int step)
{
   int i = 0;

#ifdef STBI_SSE2
   // step == 3 is pretty ugly on the final interleave, and i'm not convinced
   // it's useful in practice (you wouldn't use it for textures, for example).
   // so just accelerate step == 4 case.
   if (step == 4) {
      // this is a fairly straightforward implementation and not super-optimized.
      __m128i signflip  = _mm_set1_epi8(-0x80);
      __m128i cr_const0 = _mm_set1_epi16(   (short) ( 1.40200f*4096.0f+0.5f));
      __m128i cr_const1 = _mm_set1_epi16( - (short) ( 0.71414f*4096.0f+0.5f));
      __m128i cb_const0 = _mm_set1_epi16( - (short) ( 0.34414f*4096.0f+0.5f));
      __m128i cb_const1 = _mm_set1_epi16(   (short) ( 1.77200f*4096.0f+0.5f));
      __m128i y_bias = _mm_set1_epi8((char) (unsigned char) 128);
      __m128i xw = _mm_set1_epi16(255); // alpha channel

      for (; i+7 < count; i += 8) {
         // load
         __m128i y_bytes = _mm_loadl_epi64((__m128i *) (y+i));
         __m128i cr_bytes = _mm_loadl_epi64((__m128i *) (pcr+i));
         __m128i cb_bytes = _mm_loadl_epi64((__m128i *) (pcb+i));
         __m128i cr_biased = _mm_xor_si128(cr_bytes, signflip); // -128
         __m128i cb_biased = _mm_xor_si128(cb_bytes, signflip); // -128

         // unpack to short (and left-shift cr, cb by 8)
         __m128i yw  = _mm_unpacklo_epi8(y_bias, y_bytes);
         __m128i crw = _mm_unpacklo_epi8(_mm_setzero_si128(), cr_biased);
         __m128i cbw = _mm_unpacklo_epi8(_mm_setzero_si128(), cb_biased);

         // color transform
         __m128i yws = _mm_srli_epi16(yw, 4);
         __m128i cr0 = _mm_mulhi_epi16(cr_const0, crw);
         __m128i cb0 = _mm_mulhi_epi16(cb_const0, cbw);
         __m128i cb1 = _mm_mulhi_epi16(cbw, cb_const1);
         __m128i cr1 = _mm_mulhi_epi16(crw, cr_const1);
         __m128i rws = _mm_add_epi16(cr0, yws);
         __m128i gwt = _mm_add_epi16(cb0, yws);
         __m128i bws = _mm_add_epi16(yws, cb1);
         __m128i gws = _mm_add_epi16(gwt, cr1);

         // descale
         __m128i rw = _mm_srai_epi16(rws, 4);
         __m128i bw = _mm_srai_epi16(bws, 4);
         __m128i gw = _mm_srai_epi16(gws, 4);

         // back to byte, set up for transpose
         __m128i brb = _mm_packus_epi16(rw, bw);
         __m128i gxb = _mm_packus_epi16(gw, xw);

         // transpose to interleave channels
         __m128i t0 = _mm_unpacklo_epi8(brb, gxb);
         __m128i t1 = _mm_unpackhi_epi8(brb, gxb);
         __m128i o0 = _mm_unpacklo_epi16(t0, t1);
         __m128i o1 = _mm_unpackhi_epi16(t0, t1);

         // store
         _mm_storeu_si128((__m128i *) (out + 0), o0);
         _mm_storeu_si128((__m128i *) (out + 16), o1);
         out += 32;
      }
   }
#endif

#ifdef STBI_NEON
   // in this version, step=3 support would be easy to add. but is there demand?
   if (step == 4) {
      // this is a fairly straightforward implementation and not super-optimized.
      uint8x8_t signflip = vdup_n_u8(0x80);
      int16x8_t cr_const0 = vdupq_n_s16(   (short) ( 1.40200f*4096.0f+0.5f));
      int16x8_t cr_const1 = vdupq_n_s16( - (short) ( 0.71414f*4096.0f+0.5f));
      int16x8_t cb_const0 = vdupq_n_s16( - (short) ( 0.34414f*4096.0f+0.5f));
      int16x8_t cb_const1 = vdupq_n_s16(   (short) ( 1.77200f*4096.0f+0.5f));

      for (; i+7 < count; i += 8) {
         // load
         uint8x8_t y_bytes  = vld1_u8(y + i);
         uint8x8_t cr_bytes = vld1_u8(pcr + i);
         uint8x8_t cb_bytes = vld1_u8(pcb + i);
         int8x8_t cr_biased = vreinterpret_s8_u8(vsub_u8(cr_bytes, signflip));
         int8x8_t cb_biased = vreinterpret_s8_u8(vsub_u8(cb_bytes, signflip));

         // expand to s16
         int16x8_t yws = vreinterpretq_s16_u16(vshll_n_u8(y_bytes, 4));
         int16x8_t crw = vshll_n_s8(cr_biased, 7);
         int16x8_t cbw = vshll_n_s8(cb_biased, 7);

         // color transform
         int16x8_t cr0 = vqdmulhq_s16(crw, cr_const0);
         int16x8_t cb0 = vqdmulhq_s16(cbw, cb_const0);
         int16x8_t cr1 = vqdmulhq_s16(crw, cr_const1);
         int16x8_t cb1 = vqdmulhq_s16(cbw, cb_const1);
         int16x8_t rws = vaddq_s16(yws, cr0);
         int16x8_t gws = vaddq_s16(vaddq_s16(yws, cb0), cr1);
         int16x8_t bws = vaddq_s16(yws, cb1);

         // undo scaling, round, convert to byte
         uint8x8x4_t o;
         o.val[0] = vqrshrun_n_s16(rws, 4);
         o.val[1] = vqrshrun_n_s16(gws, 4);
         o.val[2] = vqrshrun_n_s16(bws, 4);
         o.val[3] = vdup_n_u8(255);

         // store, interleaving r/g/b/a
         vst4_u8(out, o);
         out += 8*4;
      }
   }
#endif

   for (; i < count; ++i) {
      int y_fixed = (y[i] << 20) + (1<<19); // rounding
      int r,g,b;
      int cr = pcr[i] - 128;
      int cb = pcb[i] - 128;
      r = y_fixed + cr* stbi__float2fixed(1.40200f);
      g = y_fixed + cr*-stbi__float2fixed(0.71414f) + ((cb*-stbi__float2fixed(0.34414f)) & 0xffff0000);
      b = y_fixed                                   +   cb* stbi__float2fixed(1.77200f);
      r >>= 20;
      g >>= 20;
      b >>= 20;
      if ((unsigned) r > 255) { if (r < 0) r = 0; else r = 255; }
      if ((unsigned) g > 255) { if (g < 0) g = 0; else g = 255; }
      if ((unsigned) b > 255) { if (b < 0) b = 0; else b = 255; }
      out[0] = (stbi_uc)r;
      out[1] = (stbi_uc)g;
      out[2] = (stbi_uc)b;
      out[3] = 255;
      out += step;
   }
}
#endif

// set up the kernels
static void stbi__setup_jpeg(stbi__jpeg *j)
{
   j->idct_block_kernel = stbi__idct_block;
   j->YCbCr_to_RGB_kernel = stbi__YCbCr_to_RGB_row;
   j->resample_row_hv_2_kernel = stbi__resample_row_hv_2;

#ifdef STBI_SSE2
   if (stbi__sse2_available()) {
      j->idct_block_kernel = stbi__idct_simd;
      j->YCbCr_to_RGB_kernel = stbi__YCbCr_to_RGB_simd;
      j->resample_row_hv_2_kernel = stbi__resample_row_hv_2_simd;
   }
#endif

#ifdef STBI_NEON
   j->idct_block_kernel = stbi__idct_simd;
   j->YCbCr_to_RGB_kernel = stbi__YCbCr_to_RGB_simd;
   j->resample_row_hv_2_kernel = stbi__resample_row_hv_2_simd;
#endif
}

// clean up the temporary component buffers
static void stbi__cleanup_jpeg(stbi__jpeg *j)
{
   stbi__free_jpeg_components(j, j->s->img_n, 0);
}

typedef struct
{
   resample_row_func resample;
   stbi_uc *line0,*line1;
   int hs,vs;   // expansion factor in each axis
   int w_lores; // horizontal pixels pre-expansion
   int ystep;   // how far through vertical expansion we are
   int ypos;    // which pre-expansion row we're on
} stbi__resample;

// fast 0..255 * 0..255 => 0..255 rounded multiplication
static stbi_uc stbi__blinn_8x8(stbi_uc x, stbi_uc y)
{
   unsigned int t = x*y + 128;
   return (stbi_uc) ((t + (t >>8)) >> 8);
}

static stbi_uc *load_jpeg_image(stbi__jpeg *z, int *out_x, int *out_y, int *comp, int req_comp)
{
   int n, decode_n, is_rgb;
   z->s->img_n = 0; // make stbi__cleanup_jpeg safe

   // validate req_comp
   if (req_comp < 0 || req_comp > 4) return stbi__errpuc("bad req_comp", "Internal error");

   // load a jpeg image from whichever source, but leave in YCbCr format
   if (!stbi__decode_jpeg_image(z)) { stbi__cleanup_jpeg(z); return NULL; }

   // determine actual number of components to generate
   n = req_comp ? req_comp : z->s->img_n >= 3 ? 3 : 1;

   is_rgb = z->s->img_n == 3 && (z->rgb == 3 || (z->app14_color_transform == 0 && !z->jfif));

   if (z->s->img_n == 3 && n < 3 && !is_rgb)
      decode_n = 1;
   else
      decode_n = z->s->img_n;

   // nothing to do if no components requested; check this now to avoid
   // accessing uninitialized coutput[0] later
   if (decode_n <= 0) { stbi__cleanup_jpeg(z); return NULL; }

   // resample and color-convert
   {
      int k;
      unsigned int i,j;
      stbi_uc *output;
      stbi_uc *coutput[4] = { NULL, NULL, NULL, NULL };

      stbi__resample res_comp[4];

      for (k=0; k < decode_n; ++k) {
         stbi__resample *r = &res_comp[k];

         // allocate line buffer big enough for upsampling off the edges
         // with upsample factor of 4
         z->img_comp[k].linebuf = (stbi_uc *) stbi__malloc(z->s->img_x + 3);
         if (!z->img_comp[k].linebuf) { stbi__cleanup_jpeg(z); return stbi__errpuc("outofmem", "Out of memory"); }

         r->hs      = z->img_h_max / z->img_comp[k].h;
         r->vs      = z->img_v_max / z->img_comp[k].v;
         r->ystep   = r->vs >> 1;
         r->w_lores = (z->s->img_x + r->hs-1) / r->hs;
         r->ypos    = 0;
         r->line0   = r->line1 = z->img_comp[k].data;

         if      (r->hs == 1 && r->vs == 1) r->resample = resample_row_1;
         else if (r->hs == 1 && r->vs == 2) r->resample = stbi__resample_row_v_2;
         else if (r->hs == 2 && r->vs == 1) r->resample = stbi__resample_row_h_2;
         else if (r->hs == 2 && r->vs == 2) r->resample = z->resample_row_hv_2_kernel;
         else                               r->resample = stbi__resample_row_generic;
      }

      // can't error after this so, this is safe
      output = (stbi_uc *) stbi__malloc_mad3(n, z->s->img_x, z->s->img_y, 1);
      if (!output) { stbi__cleanup_jpeg(z); return stbi__errpuc("outofmem", "Out of memory"); }

      // now go ahead and resample
      for (j=0; j < z->s->img_y; ++j) {
         stbi_uc *out = output + n * z->s->img_x * j;
         for (k=0; k < decode_n; ++k) {
            stbi__resample *r = &res_comp[k];
            int y_bot = r->ystep >= (r->vs >> 1);
            coutput[k] = r->resample(z->img_comp[k].linebuf,
                                     y_bot ? r->line1 : r->line0,
                                     y_bot ? r->line0 : r->line1,
                                     r->w_lores, r->hs);
            if (++r->ystep >= r->vs) {
               r->ystep = 0;
               r->line0 = r->line1;
               if (++r->ypos < z->img_comp[k].y)
                  r->line1 += z->img_comp[k].w2;
            }
         }
         if (n >= 3) {
            stbi_uc *y = coutput[0];
            if (z->s->img_n == 3) {
               if (is_rgb) {
                  for (i=0; i < z->s->img_x; ++i) {
                     out[0] = y[i];
                     out[1] = coutput[1][i];
                     out[2] = coutput[2][i];
                     out[3] = 255;
                     out += n;
                  }
               } else {
                  z->YCbCr_to_RGB_kernel(out, y, coutput[1], coutput[2], z->s->img_x, n);
               }
            } else if (z->s->img_n == 4) {
               if (z->app14_color_transform == 0) { // CMYK
                  for (i=0; i < z->s->img_x; ++i) {
                     stbi_uc m = coutput[3][i];
                     out[0] = stbi__blinn_8x8(coutput[0][i], m);
                     out[1] = stbi__blinn_8x8(coutput[1][i], m);
                     out[2] = stbi__blinn_8x8(coutput[2][i], m);
                     out[3] = 255;
                     out += n;
                  }
               } else if (z->app14_color_transform == 2) { // YCCK
                  z->YCbCr_to_RGB_kernel(out, y, coutput[1], coutput[2], z->s->img_x, n);
                  for (i=0; i < z->s->img_x; ++i) {
                     stbi_uc m = coutput[3][i];
                     out[0] = stbi__blinn_8x8(255 - out[0], m);
                     out[1] = stbi__blinn_8x8(255 - out[1], m);
                     out[2] = stbi__blinn_8x8(255 - out[2], m);
                     out += n;
                  }
               } else { // YCbCr + alpha?  Ignore the fourth channel for now
                  z->YCbCr_to_RGB_kernel(out, y, coutput[1], coutput[2], z->s->img_x, n);
               }
            } else
               for (i=0; i < z->s->img_x; ++i) {
                  out[0] = out[1] = out[2] = y[i];
                  out[3] = 255; // not used if n==3
                  out += n;
               }
         } else {
            if (is_rgb) {
               if (n == 1)
                  for (i=0; i < z->s->img_x; ++i)
                     *out++ = stbi__compute_y(coutput[0][i], coutput[1][i], coutput[2][i]);
               else {
                  for (i=0; i < z->s->img_x; ++i, out += 2) {
                     out[0] = stbi__compute_y(coutput[0][i], coutput[1][i], coutput[2][i]);
                     out[1] = 255;
                  }
               }
            } else if (z->s->img_n == 4 && z->app14_color_transform == 0) {
               for (i=0; i < z->s->img_x; ++i) {
                  stbi_uc m = coutput[3][i];
                  stbi_uc r = stbi__blinn_8x8(coutput[0][i], m);
                  stbi_uc g = stbi__blinn_8x8(coutput[1][i], m);
                  stbi_uc b = stbi__blinn_8x8(coutput[2][i], m);
                  out[0] = stbi__compute_y(r, g, b);
                  out[1] = 255;
                  out += n;
               }
            } else if (z->s->img_n == 4 && z->app14_color_transform == 2) {
               for (i=0; i < z->s->img_x; ++i) {
                  out[0] = stbi__blinn_8x8(255 - coutput[0][i], coutput[3][i]);
                  out[1] = 255;
                  out += n;
               }
            } else {
               stbi_uc *y = coutput[0];
               if (n == 1)
                  for (i=0; i < z->s->img_x; ++i) out[i] = y[i];
               else
                  for (i=0; i < z->s->img_x; ++i) { *out++ = y[i]; *out++ = 255; }
            }
         }
      }
      stbi__cleanup_jpeg(z);
      *out_x = z->s->img_x;
      *out_y = z->s->img_y;
      if (comp) *comp = z->s->img_n >= 3 ? 3 : 1; // report original components, not output
      return output;
   }
}

static void *stbi__jpeg_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri)
{
   unsigned char* result;
   stbi__jpeg* j = (stbi__jpeg*) stbi__malloc(sizeof(stbi__jpeg));
   if (!j) return stbi__errpuc("outofmem", "Out of memory");
   memset(j, 0, sizeof(stbi__jpeg));
   STBI_NOTUSED(ri);
   j->s = s;
   stbi__setup_jpeg(j);
   result = load_jpeg_image(j, x,y,comp,req_comp);
   STBI_FREE(j);
   return result;
}

static int stbi__jpeg_test(stbi__context *s)
{
   int r;
   stbi__jpeg* j = (stbi__jpeg*)stbi__malloc(sizeof(stbi__jpeg));
   if (!j) return stbi__err("outofmem", "Out of memory");
   memset(j, 0, sizeof(stbi__jpeg));
   j->s = s;
   stbi__setup_jpeg(j);
   r = stbi__decode_jpeg_header(j, STBI__SCAN_type);
   stbi__rewind(s);
   STBI_FREE(j);
   return r;
}

static int stbi__jpeg_info_raw(stbi__jpeg *j, int *x, int *y, int *comp)
{
   if (!stbi__decode_jpeg_header(j, STBI__SCAN_header)) {
      stbi__rewind( j->s );
      return 0;
   }
   if (x) *x = j->s->img_x;
   if (y) *y = j->s->img_y;
   if (comp) *comp = j->s->img_n >= 3 ? 3 : 1;
   return 1;
}

static int stbi__jpeg_info(stbi__context *s, int *x, int *y, int *comp)
{
   int result;
   stbi__jpeg* j = (stbi__jpeg*) (stbi__malloc(sizeof(stbi__jpeg)));
   if (!j) return stbi__err("outofmem", "Out of memory");
   memset(j, 0, sizeof(stbi__jpeg));
   j->s = s;
   result = stbi__jpeg_info_raw(j, x, y, comp);
   STBI_FREE(j);
   return result;
}
#endif

// public domain zlib decode    v0.2  Sean Barrett 2006-11-18
//    simple implementation
//      - all input must be provided in an upfront buffer
//      - all output is written to a single output buffer (can malloc/realloc)
//    performance
//      - fast huffman

#ifndef STBI_NO_ZLIB

// fast-way is faster to check than jpeg huffman, but slow way is slower
#define STBI__ZFAST_BITS  9 // accelerate all cases in default tables
#define STBI__ZFAST_MASK  ((1 << STBI__ZFAST_BITS) - 1)
#define STBI__ZNSYMS 288 // number of symbols in literal/length alphabet

// zlib-style huffman encoding
// (jpegs packs from left, zlib from right, so can't share code)
typedef struct
{
   stbi__uint16 fast[1 << STBI__ZFAST_BITS];
   stbi__uint16 firstcode[16];
   int maxcode[17];
   stbi__uint16 firstsymbol[16];
   stbi_uc  size[STBI__ZNSYMS];
   stbi__uint16 value[STBI__ZNSYMS];
} stbi__zhuffman;

stbi_inline static int stbi__bitreverse16(int n)
{
  n = ((n & 0xAAAA) >>  1) | ((n & 0x5555) << 1);
  n = ((n & 0xCCCC) >>  2) | ((n & 0x3333) << 2);
  n = ((n & 0xF0F0) >>  4) | ((n & 0x0F0F) << 4);
  n = ((n & 0xFF00) >>  8) | ((n & 0x00FF) << 8);
  return n;
}

stbi_inline static int stbi__bit_reverse(int v, int bits)
{
   STBI_ASSERT(bits <= 16);
   // to bit reverse n bits, reverse 16 and shift
   // e.g. 11 bits, bit reverse and shift away 5
   return stbi__bitreverse16(v) >> (16-bits);
}

static int stbi__zbuild_huffman(stbi__zhuffman *z, const stbi_uc *sizelist, int num)
{
   int i,k=0;
   int code, next_code[16], sizes[17];

   // DEFLATE spec for generating codes
   memset(sizes, 0, sizeof(sizes));
   memset(z->fast, 0, sizeof(z->fast));
   for (i=0; i < num; ++i)
      ++sizes[sizelist[i]];
   sizes[0] = 0;
   for (i=1; i < 16; ++i)
      if (sizes[i] > (1 << i))
         return stbi__err("bad sizes", "Corrupt PNG");
   code = 0;
   for (i=1; i < 16; ++i) {
      next_code[i] = code;
      z->firstcode[i] = (stbi__uint16) code;
      z->firstsymbol[i] = (stbi__uint16) k;
      code = (code + sizes[i]);
      if (sizes[i])
         if (code-1 >= (1 << i)) return stbi__err("bad codelengths","Corrupt PNG");
      z->maxcode[i] = code << (16-i); // preshift for inner loop
      code <<= 1;
      k += sizes[i];
   }
   z->maxcode[16] = 0x10000; // sentinel
   for (i=0; i < num; ++i) {
      int s = sizelist[i];
      if (s) {
         int c = next_code[s] - z->firstcode[s] + z->firstsymbol[s];
         stbi__uint16 fastv = (stbi__uint16) ((s << 9) | i);
         z->size [c] = (stbi_uc     ) s;
         z->value[c] = (stbi__uint16) i;
         if (s <= STBI__ZFAST_BITS) {
            int j = stbi__bit_reverse(next_code[s],s);
            while (j < (1 << STBI__ZFAST_BITS)) {
               z->fast[j] = fastv;
               j += (1 << s);
            }
         }
         ++next_code[s];
      }
   }
   return 1;
}

// zlib-from-memory implementation for PNG reading
//    because PNG allows splitting the zlib stream arbitrarily,
//    and it's annoying structurally to have PNG call ZLIB call PNG,
//    we require PNG read all the IDATs and combine them into a single
//    memory buffer

typedef struct
{
   stbi_uc *zbuffer, *zbuffer_end;
   int num_bits;
   int hit_zeof_once;
   stbi__uint32 code_buffer;

   char *zout;
   char *zout_start;
   char *zout_end;
   int   z_expandable;

   stbi__zhuffman z_length, z_distance;
} stbi__zbuf;

stbi_inline static int stbi__zeof(stbi__zbuf *z)
{
   return (z->zbuffer >= z->zbuffer_end);
}

stbi_inline static stbi_uc stbi__zget8(stbi__zbuf *z)
{
   return stbi__zeof(z) ? 0 : *z->zbuffer++;
}

static void stbi__fill_bits(stbi__zbuf *z)
{
   do {
      if (z->code_buffer >= (1U << z->num_bits)) {
        z->zbuffer = z->zbuffer_end;  /* treat this as EOF so we fail. */
        return;
      }
      z->code_buffer |= (unsigned int) stbi__zget8(z) << z->num_bits;
      z->num_bits += 8;
   } while (z->num_bits <= 24);
}

stbi_inline static unsigned int stbi__zreceive(stbi__zbuf *z, int n)
{
   unsigned int k;
   if (z->num_bits < n) stbi__fill_bits(z);
   k = z->code_buffer & ((1 << n) - 1);
   z->code_buffer >>= n;
   z->num_bits -= n;
   return k;
}

static int stbi__zhuffman_decode_slowpath(stbi__zbuf *a, stbi__zhuffman *z)
{
   int b,s,k;
   // not resolved by fast table, so compute it the slow way
   // use jpeg approach, which requires MSbits at top
   k = stbi__bit_reverse(a->code_buffer, 16);
   for (s=STBI__ZFAST_BITS+1; ; ++s)
      if (k < z->maxcode[s])
         break;
   if (s >= 16) return -1; // invalid code!
   // code size is s, so:
   b = (k >> (16-s)) - z->firstcode[s] + z->firstsymbol[s];
   if (b >= STBI__ZNSYMS) return -1; // some data was corrupt somewhere!
   if (z->size[b] != s) return -1;  // was originally an assert, but report failure instead.
   a->code_buffer >>= s;
   a->num_bits -= s;
   return z->value[b];
}

stbi_inline static int stbi__zhuffman_decode(stbi__zbuf *a, stbi__zhuffman *z)
{
   int b,s;
   if (a->num_bits < 16) {
      if (stbi__zeof(a)) {
         if (!a->hit_zeof_once) {
            // This is the first time we hit eof, insert 16 extra padding btis
            // to allow us to keep going; if we actually consume any of them
            // though, that is invalid data. This is caught later.
            a->hit_zeof_once = 1;
            a->num_bits += 16; // add 16 implicit zero bits
         } else {
            // We already inserted our extra 16 padding bits and are again
            // out, this stream is actually prematurely terminated.
            return -1;
         }
      } else {
         stbi__fill_bits(a);
      }
   }
   b = z->fast[a->code_buffer & STBI__ZFAST_MASK];
   if (b) {
      s = b >> 9;
      a->code_buffer >>= s;
      a->num_bits -= s;
      return b & 511;
   }
   return stbi__zhuffman_decode_slowpath(a, z);
}

static int stbi__zexpand(stbi__zbuf *z, char *zout, int n)  // need to make room for n bytes
{
   char *q;
   unsigned int cur, limit, old_limit;
   z->zout = zout;
   if (!z->z_expandable) return stbi__err("output buffer limit","Corrupt PNG");
   cur   = (unsigned int) (z->zout - z->zout_start);
   limit = old_limit = (unsigned) (z->zout_end - z->zout_start);
   if (UINT_MAX - cur < (unsigned) n) return stbi__err("outofmem", "Out of memory");
   while (cur + n > limit) {
      if(limit > UINT_MAX / 2) return stbi__err("outofmem", "Out of memory");
      limit *= 2;
   }
   q = (char *) STBI_REALLOC_SIZED(z->zout_start, old_limit, limit);
   STBI_NOTUSED(old_limit);
   if (q == NULL) return stbi__err("outofmem", "Out of memory");
   z->zout_start = q;
   z->zout       = q + cur;
   z->zout_end   = q + limit;
   return 1;
}

static const int stbi__zlength_base[31] = {
   3,4,5,6,7,8,9,10,11,13,
   15,17,19,23,27,31,35,43,51,59,
   67,83,99,115,131,163,195,227,258,0,0 };

static const int stbi__zlength_extra[31]=
{ 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0,0,0 };

static const int stbi__zdist_base[32] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577,0,0};

static const int stbi__zdist_extra[32] =
{ 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int stbi__parse_huffman_block(stbi__zbuf *a)
{
   char *zout = a->zout;
   for(;;) {
      int z = stbi__zhuffman_decode(a, &a->z_length);
      if (z < 256) {
         if (z < 0) return stbi__err("bad huffman code","Corrupt PNG"); // error in huffman codes
         if (zout >= a->zout_end) {
            if (!stbi__zexpand(a, zout, 1)) return 0;
            zout = a->zout;
         }
         *zout++ = (char) z;
      } else {
         stbi_uc *p;
         int len,dist;
         if (z == 256) {
            a->zout = zout;
            if (a->hit_zeof_once && a->num_bits < 16) {
               // The first time we hit zeof, we inserted 16 extra zero bits into our bit
               // buffer so the decoder can just do its speculative decoding. But if we
               // actually consumed any of those bits (which is the case when num_bits < 16),
               // the stream actually read past the end so it is malformed.
               return stbi__err("unexpected end","Corrupt PNG");
            }
            return 1;
         }
         if (z >= 286) return stbi__err("bad huffman code","Corrupt PNG"); // per DEFLATE, length codes 286 and 287 must not appear in compressed data
         z -= 257;
         len = stbi__zlength_base[z];
         if (stbi__zlength_extra[z]) len += stbi__zreceive(a, stbi__zlength_extra[z]);
         z = stbi__zhuffman_decode(a, &a->z_distance);
         if (z < 0 || z >= 30) return stbi__err("bad huffman code","Corrupt PNG"); // per DEFLATE, distance codes 30 and 31 must not appear in compressed data
         dist = stbi__zdist_base[z];
         if (stbi__zdist_extra[z]) dist += stbi__zreceive(a, stbi__zdist_extra[z]);
         if (zout - a->zout_start < dist) return stbi__err("bad dist","Corrupt PNG");
         if (len > a->zout_end - zout) {
            if (!stbi__zexpand(a, zout, len)) return 0;
            zout = a->zout;
         }
         p = (stbi_uc *) (zout - dist);
         if (dist == 1) { // run of one byte; common in images.
            stbi_uc v = *p;
            if (len) { do *zout++ = v; while (--len); }
         } else {
            if (len) { do *zout++ = *p++; while (--len); }
         }
      }
   }
}

static int stbi__compute_huffman_codes(stbi__zbuf *a)
{
   static const stbi_uc length_dezigzag[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
   stbi__zhuffman z_codelength;
   stbi_uc lencodes[286+32+137];//padding for maximum single op
   stbi_uc codelength_sizes[19];
   int i,n;

   int hlit  = stbi__zreceive(a,5) + 257;
   int hdist = stbi__zreceive(a,5) + 1;
   int hclen = stbi__zreceive(a,4) + 4;
   int ntot  = hlit + hdist;

   memset(codelength_sizes, 0, sizeof(codelength_sizes));
   for (i=0; i < hclen; ++i) {
      int s = stbi__zreceive(a,3);
      codelength_sizes[length_dezigzag[i]] = (stbi_uc) s;
   }
   if (!stbi__zbuild_huffman(&z_codelength, codelength_sizes, 19)) return 0;

   n = 0;
   while (n < ntot) {
      int c = stbi__zhuffman_decode(a, &z_codelength);
      if (c < 0 || c >= 19) return stbi__err("bad codelengths", "Corrupt PNG");
      if (c < 16)
         lencodes[n++] = (stbi_uc) c;
      else {
         stbi_uc fill = 0;
         if (c == 16) {
            c = stbi__zreceive(a,2)+3;
            if (n == 0) return stbi__err("bad codelengths", "Corrupt PNG");
            fill = lencodes[n-1];
         } else if (c == 17) {
            c = stbi__zreceive(a,3)+3;
         } else if (c == 18) {
            c = stbi__zreceive(a,7)+11;
         } else {
            return stbi__err("bad codelengths", "Corrupt PNG");
         }
         if (ntot - n < c) return stbi__err("bad codelengths", "Corrupt PNG");
         memset(lencodes+n, fill, c);
         n += c;
      }
   }
   if (n != ntot) return stbi__err("bad codelengths","Corrupt PNG");
   if (!stbi__zbuild_huffman(&a->z_length, lencodes, hlit)) return 0;
   if (!stbi__zbuild_huffman(&a->z_distance, lencodes+hlit, hdist)) return 0;
   return 1;
}

static int stbi__parse_uncompressed_block(stbi__zbuf *a)
{
   stbi_uc header[4];
   int len,nlen,k;
   if (a->num_bits & 7)
      stbi__zreceive(a, a->num_bits & 7); // discard
   // drain the bit-packed data into header
   k = 0;
   while (a->num_bits > 0) {
      header[k++] = (stbi_uc) (a->code_buffer & 255); // suppress MSVC run-time check
      a->code_buffer >>= 8;
      a->num_bits -= 8;
   }
   if (a->num_bits < 0) return stbi__err("zlib corrupt","Corrupt PNG");
   // now fill header the normal way
   while (k < 4)
      header[k++] = stbi__zget8(a);
   len  = header[1] * 256 + header[0];
   nlen = header[3] * 256 + header[2];
   if (nlen != (len ^ 0xffff)) return stbi__err("zlib corrupt","Corrupt PNG");
   if (a->zbuffer + len > a->zbuffer_end) return stbi__err("read past buffer","Corrupt PNG");
   if (a->zout + len > a->zout_end)
      if (!stbi__zexpand(a, a->zout, len)) return 0;
   memcpy(a->zout, a->zbuffer, len);
   a->zbuffer += len;
   a->zout += len;
   return 1;
}

static int stbi__parse_zlib_header(stbi__zbuf *a)
{
   int cmf   = stbi__zget8(a);
   int cm    = cmf & 15;
   /* int cinfo = cmf >> 4; */
   int flg   = stbi__zget8(a);
   if (stbi__zeof(a)) return stbi__err("bad zlib header","Corrupt PNG"); // zlib spec
   if ((cmf*256+flg) % 31 != 0) return stbi__err("bad zlib header","Corrupt PNG"); // zlib spec
   if (flg & 32) return stbi__err("no preset dict","Corrupt PNG"); // preset dictionary not allowed in png
   if (cm != 8) return stbi__err("bad compression","Corrupt PNG"); // DEFLATE required for png
   // window = 1 << (8 + cinfo)... but who cares, we fully buffer output
   return 1;
}

static const stbi_uc stbi__zdefault_length[STBI__ZNSYMS] =
{
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, 7,7,7,7,7,7,7,7,8,8,8,8,8,8,8,8
};
static const stbi_uc stbi__zdefault_distance[32] =
{
   5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
};
/*
Init algorithm:
{
   int i;   // use <= to match clearly with spec
   for (i=0; i <= 143; ++i)     stbi__zdefault_length[i]   = 8;
   for (   ; i <= 255; ++i)     stbi__zdefault_length[i]   = 9;
   for (   ; i <= 279; ++i)     stbi__zdefault_length[i]   = 7;
   for (   ; i <= 287; ++i)     stbi__zdefault_length[i]   = 8;

   for (i=0; i <=  31; ++i)     stbi__zdefault_distance[i] = 5;
}
*/

static int stbi__parse_zlib(stbi__zbuf *a, int parse_header)
{
   int final, type;
   if (parse_header)
      if (!stbi__parse_zlib_header(a)) return 0;
   a->num_bits = 0;
   a->code_buffer = 0;
   a->hit_zeof_once = 0;
   do {
      final = stbi__zreceive(a,1);
      type = stbi__zreceive(a,2);
      if (type == 0) {
         if (!stbi__parse_uncompressed_block(a)) return 0;
      } else if (type == 3) {
         return 0;
      } else {
         if (type == 1) {
            // use fixed code lengths
            if (!stbi__zbuild_huffman(&a->z_length  , stbi__zdefault_length  , STBI__ZNSYMS)) return 0;
            if (!stbi__zbuild_huffman(&a->z_distance, stbi__zdefault_distance,  32)) return 0;
         } else {
            if (!stbi__compute_huffman_codes(a)) return 0;
         }
         if (!stbi__parse_huffman_block(a)) return 0;
      }
   } while (!final);
   return 1;
}

static int stbi__do_zlib(stbi__zbuf *a, char *obuf, int olen, int exp, int parse_header)
{
   a->zout_start = obuf;
   a->zout       = obuf;
   a->zout_end   = obuf + olen;
   a->z_expandable = exp;

   return stbi__parse_zlib(a, parse_header);
}

STBIDEF char *stbi_zlib_decode_malloc_guesssize(const char *buffer, int len, int initial_size, int *outlen)
{
   stbi__zbuf a;
   char *p = (char *) stbi__malloc(initial_size);
   if (p == NULL) return NULL;
   a.zbuffer = (stbi_uc *) buffer;
   a.zbuffer_end = (stbi_uc *) buffer + len;
   if (stbi__do_zlib(&a, p, initial_size, 1, 1)) {
      if (outlen) *outlen = (int) (a.zout - a.zout_start);
      return a.zout_start;
   } else {
      STBI_FREE(a.zout_start);
      return NULL;
   }
}

STBIDEF char *stbi_zlib_decode_malloc(char const *buffer, int len, int *outlen)
{
   return stbi_zlib_decode_malloc_guesssize(buffer, len, 16384, outlen);
}

STBIDEF char *stbi_zlib_decode_malloc_guesssize_headerflag(const char *buffer, int len, int initial_size, int *outlen, int parse_header)
{
   stbi__zbuf a;
   char *p = (char *) stbi__malloc(initial_size);
   if (p == NULL) return NULL;
   a.zbuffer = (stbi_uc *) buffer;
   a.zbuffer_end = (stbi_uc *) buffer + len;
   if (stbi__do_zlib(&a, p, initial_size, 1, parse_header)) {
      if (outlen) *outlen = (int) (a.zout - a.zout_start);
      return a.zout_start;
   } else {
      STBI_FREE(a.zout_start);
      return NULL;
   }
}

STBIDEF int stbi_zlib_decode_buffer(char *obuffer, int olen, char const *ibuffer, int ilen)
{
   stbi__zbuf a;
   a.zbuffer = (stbi_uc *) ibuffer;
   a.zbuffer_end = (stbi_uc *) ibuffer + ilen;
   if (stbi__do_zlib(&a, obuffer, olen, 0, 1))
      return (int) (a.zout - a.zout_start);
   else
      return -1;
}

STBIDEF char *stbi_zlib_decode_noheader_malloc(char const *buffer, int len, int *outlen)
{
   stbi__zbuf a;
   char *p = (char *) stbi__malloc(16384);
   if (p == NULL) return NULL;
   a.zbuffer = (stbi_uc *) buffer;
   a.zbuffer_end = (stbi_uc *) buffer+len;
   if (stbi__do_zlib(&a, p, 16384, 1, 0)) {
      if (outlen) *outlen = (int) (a.zout - a.zout_start);
      return a.zout_start;
   } else {
      STBI_FREE(a.zout_start);
      return NULL;
   }
}

STBIDEF int stbi_zlib_decode_noheader_buffer(char *obuffer, int olen, const char *ibuffer, int ilen)
{
   stbi__zbuf a;
   a.zbuffer = (stbi_uc *) ibuffer;
   a.zbuffer_end = (stbi_uc *) ibuffer + ilen;
   if (stbi__do_zlib(&a, obuffer, olen, 0, 0))
      return (int) (a.zout - a.zout_start);
   else
      return -1;
}
#endif

// public domain "baseline" PNG decoder   v0.10  Sean Barrett 2006-11-18
//    simple implementation
//      - only 8-bit samples
//      - no CRC checking
//      - allocates lots of intermediate memory
//        - avoids problem of streaming data between subsystems
//        - avoids explicit window management
//    performance
//      - uses stb_zlib, a PD zlib implementation with fast huffman decoding

#ifndef STBI_NO_PNG
typedef struct
{
   stbi__uint32 length;
   stbi__uint32 type;
} stbi__pngchunk;

static stbi__pngchunk stbi__get_chunk_header(stbi__context *s)
{
   stbi__pngchunk c;
   c.length = stbi__get32be(s);
   c.type   = stbi__get32be(s);
   return c;
}

static int stbi__check_png_header(stbi__context *s)
{
   static const stbi_uc png_sig[8] = { 137,80,78,71,13,10,26,10 };
   int i;
   for (i=0; i < 8; ++i)
      if (stbi__get8(s) != png_sig[i]) return stbi__err("bad png sig","Not a PNG");
   return 1;
}

typedef struct
{
   stbi__context *s;
   stbi_uc *idata, *expanded, *out;
   int depth;
} stbi__png;


enum {
   STBI__F_none=0,
   STBI__F_sub=1,
   STBI__F_up=2,
   STBI__F_avg=3,
   STBI__F_paeth=4,
   // synthetic filter used for first scanline to avoid needing a dummy row of 0s
   STBI__F_avg_first
};

static stbi_uc first_row_filter[5] =
{
   STBI__F_none,
   STBI__F_sub,
   STBI__F_none,
   STBI__F_avg_first,
   STBI__F_sub // Paeth with b=c=0 turns out to be equivalent to sub
};

static int stbi__paeth(int a, int b, int c)
{
   // This formulation looks very different from the reference in the PNG spec, but is
   // actually equivalent and has favorable data dependencies and admits straightforward
   // generation of branch-free code, which helps performance significantly.
   int thresh = c*3 - (a + b);
   int lo = a < b ? a : b;
   int hi = a < b ? b : a;
   int t0 = (hi <= thresh) ? lo : c;
   int t1 = (thresh <= lo) ? hi : t0;
   return t1;
}

static const stbi_uc stbi__depth_scale_table[9] = { 0, 0xff, 0x55, 0, 0x11, 0,0,0, 0x01 };

// adds an extra all-255 alpha channel
// dest == src is legal
// img_n must be 1 or 3
static void stbi__create_png_alpha_expand8(stbi_uc *dest, stbi_uc *src, stbi__uint32 x, int img_n)
{
   int i;
   // must process data backwards since we allow dest==src
   if (img_n == 1) {
      for (i=x-1; i >= 0; --i) {
         dest[i*2+1] = 255;
         dest[i*2+0] = src[i];
      }
   } else {
      STBI_ASSERT(img_n == 3);
      for (i=x-1; i >= 0; --i) {
         dest[i*4+3] = 255;
         dest[i*4+2] = src[i*3+2];
         dest[i*4+1] = src[i*3+1];
         dest[i*4+0] = src[i*3+0];
      }
   }
}

// create the png data from post-deflated data
static int stbi__create_png_image_raw(stbi__png *a, stbi_uc *raw, stbi__uint32 raw_len, int out_n, stbi__uint32 x, stbi__uint32 y, int depth, int color)
{
   int bytes = (depth == 16 ? 2 : 1);
   stbi__context *s = a->s;
   stbi__uint32 i,j,stride = x*out_n*bytes;
   stbi__uint32 img_len, img_width_bytes;
   stbi_uc *filter_buf;
   int all_ok = 1;
   int k;
   int img_n = s->img_n; // copy it into a local for later

   int output_bytes = out_n*bytes;
   int filter_bytes = img_n*bytes;
   int width = x;

   STBI_ASSERT(out_n == s->img_n || out_n == s->img_n+1);
   a->out = (stbi_uc *) stbi__malloc_mad3(x, y, output_bytes, 0); // extra bytes to write off the end into
   if (!a->out) return stbi__err("outofmem", "Out of memory");

   // note: error exits here don't need to clean up a->out individually,
   // stbi__do_png always does on error.
   if (!stbi__mad3sizes_valid(img_n, x, depth, 7)) return stbi__err("too large", "Corrupt PNG");
   img_width_bytes = (((img_n * x * depth) + 7) >> 3);
   if (!stbi__mad2sizes_valid(img_width_bytes, y, img_width_bytes)) return stbi__err("too large", "Corrupt PNG");
   img_len = (img_width_bytes + 1) * y;

   // we used to check for exact match between raw_len and img_len on non-interlaced PNGs,
   // but issue #276 reported a PNG in the wild that had extra data at the end (all zeros),
   // so just check for raw_len < img_len always.
   if (raw_len < img_len) return stbi__err("not enough pixels","Corrupt PNG");

   // Allocate two scan lines worth of filter workspace buffer.
   filter_buf = (stbi_uc *) stbi__malloc_mad2(img_width_bytes, 2, 0);
   if (!filter_buf) return stbi__err("outofmem", "Out of memory");

   // Filtering for low-bit-depth images
   if (depth < 8) {
      filter_bytes = 1;
      width = img_width_bytes;
   }

   for (j=0; j < y; ++j) {
      // cur/prior filter buffers alternate
      stbi_uc *cur = filter_buf + (j & 1)*img_width_bytes;
      stbi_uc *prior = filter_buf + (~j & 1)*img_width_bytes;
      stbi_uc *dest = a->out + stride*j;
      int nk = width * filter_bytes;
      int filter = *raw++;

      // check filter type
      if (filter > 4) {
         all_ok = stbi__err("invalid filter","Corrupt PNG");
         break;
      }

      // if first row, use special filter that doesn't sample previous row
      if (j == 0) filter = first_row_filter[filter];

      // perform actual filtering
      switch (filter) {
      case STBI__F_none:
         memcpy(cur, raw, nk);
         break;
      case STBI__F_sub:
         memcpy(cur, raw, filter_bytes);
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + cur[k-filter_bytes]);
         break;
      case STBI__F_up:
         for (k = 0; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + prior[k]);
         break;
      case STBI__F_avg:
         for (k = 0; k < filter_bytes; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + (prior[k]>>1));
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + ((prior[k] + cur[k-filter_bytes])>>1));
         break;
      case STBI__F_paeth:
         for (k = 0; k < filter_bytes; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + prior[k]); // prior[k] == stbi__paeth(0,prior[k],0)
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + stbi__paeth(cur[k-filter_bytes], prior[k], prior[k-filter_bytes]));
         break;
      case STBI__F_avg_first:
         memcpy(cur, raw, filter_bytes);
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + (cur[k-filter_bytes] >> 1));
         break;
      }

      raw += nk;

      // expand decoded bits in cur to dest, also adding an extra alpha channel if desired
      if (depth < 8) {
         stbi_uc scale = (color == 0) ? stbi__depth_scale_table[depth] : 1; // scale grayscale values to 0..255 range
         stbi_uc *in = cur;
         stbi_uc *out = dest;
         stbi_uc inb = 0;
         stbi__uint32 nsmp = x*img_n;

         // expand bits to bytes first
         if (depth == 4) {
            for (i=0; i < nsmp; ++i) {
               if ((i & 1) == 0) inb = *in++;
               *out++ = scale * (inb >> 4);
               inb <<= 4;
            }
         } else if (depth == 2) {
            for (i=0; i < nsmp; ++i) {
               if ((i & 3) == 0) inb = *in++;
               *out++ = scale * (inb >> 6);
               inb <<= 2;
            }
         } else {
            STBI_ASSERT(depth == 1);
            for (i=0; i < nsmp; ++i) {
               if ((i & 7) == 0) inb = *in++;
               *out++ = scale * (inb >> 7);
               inb <<= 1;
            }
         }

         // insert alpha=255 values if desired
         if (img_n != out_n)
            stbi__create_png_alpha_expand8(dest, dest, x, img_n);
      } else if (depth == 8) {
         if (img_n == out_n)
            memcpy(dest, cur, x*img_n);
         else
            stbi__create_png_alpha_expand8(dest, cur, x, img_n);
      } else if (depth == 16) {
         // convert the image data from big-endian to platform-native
         stbi__uint16 *dest16 = (stbi__uint16*)dest;
         stbi__uint32 nsmp = x*img_n;

         if (img_n == out_n) {
            for (i = 0; i < nsmp; ++i, ++dest16, cur += 2)
               *dest16 = (cur[0] << 8) | cur[1];
         } else {
            STBI_ASSERT(img_n+1 == out_n);
            if (img_n == 1) {
               for (i = 0; i < x; ++i, dest16 += 2, cur += 2) {
                  dest16[0] = (cur[0] << 8) | cur[1];
                  dest16[1] = 0xffff;
               }
            } else {
               STBI_ASSERT(img_n == 3);
               for (i = 0; i < x; ++i, dest16 += 4, cur += 6) {
                  dest16[0] = (cur[0] << 8) | cur[1];
                  dest16[1] = (cur[2] << 8) | cur[3];
                  dest16[2] = (cur[4] << 8) | cur[5];
                  dest16[3] = 0xffff;
               }
            }
         }
      }
   }

   STBI_FREE(filter_buf);
   if (!all_ok) return 0;

   return 1;
}

static int stbi__create_png_image(stbi__png *a, stbi_uc *image_data, stbi__uint32 image_data_len, int out_n, int depth, int color, int interlaced)
{
   int bytes = (depth == 16 ? 2 : 1);
   int out_bytes = out_n * bytes;
   stbi_uc *final;
   int p;
   if (!interlaced)
      return stbi__create_png_image_raw(a, image_data, image_data_len, out_n, a->s->img_x, a->s->img_y, depth, color);

   // de-interlacing
   final = (stbi_uc *) stbi__malloc_mad3(a->s->img_x, a->s->img_y, out_bytes, 0);
   if (!final) return stbi__err("outofmem", "Out of memory");
   for (p=0; p < 7; ++p) {
      int xorig[] = { 0,4,0,2,0,1,0 };
      int yorig[] = { 0,0,4,0,2,0,1 };
      int xspc[]  = { 8,8,4,4,2,2,1 };
      int yspc[]  = { 8,8,8,4,4,2,2 };
      int i,j,x,y;
      // pass1_x[4] = 0, pass1_x[5] = 1, pass1_x[12] = 1
      x = (a->s->img_x - xorig[p] + xspc[p]-1) / xspc[p];
      y = (a->s->img_y - yorig[p] + yspc[p]-1) / yspc[p];
      if (x && y) {
         stbi__uint32 img_len = ((((a->s->img_n * x * depth) + 7) >> 3) + 1) * y;
         if (!stbi__create_png_image_raw(a, image_data, image_data_len, out_n, x, y, depth, color)) {
            STBI_FREE(final);
            return 0;
         }
         for (j=0; j < y; ++j) {
            for (i=0; i < x; ++i) {
               int out_y = j*yspc[p]+yorig[p];
               int out_x = i*xspc[p]+xorig[p];
               memcpy(final + out_y*a->s->img_x*out_bytes + out_x*out_bytes,
                      a->out + (j*x+i)*out_bytes, out_bytes);
            }
         }
         STBI_FREE(a->out);
         image_data += img_len;
         image_data_len -= img_len;
      }
   }
   a->out = final;

   return 1;
}

static int stbi__compute_transparency(stbi__png *z, stbi_uc tc[3], int out_n)
{
   stbi__context *s = z->s;
   stbi__uint32 i, pixel_count = s->img_x * s->img_y;
   stbi_uc *p = z->out;

   // compute color-based transparency, assuming we've
   // already got 255 as the alpha value in the output
   STBI_ASSERT(out_n == 2 || out_n == 4);

   if (out_n == 2) {
      for (i=0; i < pixel_count; ++i) {
         p[1] = (p[0] == tc[0] ? 0 : 255);
         p += 2;
      }
   } else {
      for (i=0; i < pixel_count; ++i) {
         if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
            p[3] = 0;
         p += 4;
      }
   }
   return 1;
}

static int stbi__compute_transparency16(stbi__png *z, stbi__uint16 tc[3], int out_n)
{
   stbi__context *s = z->s;
   stbi__uint32 i, pixel_count = s->img_x * s->img_y;
   stbi__uint16 *p = (stbi__uint16*) z->out;

   // compute color-based transparency, assuming we've
   // already got 65535 as the alpha value in the output
   STBI_ASSERT(out_n == 2 || out_n == 4);

   if (out_n == 2) {
      for (i = 0; i < pixel_count; ++i) {
         p[1] = (p[0] == tc[0] ? 0 : 65535);
         p += 2;
      }
   } else {
      for (i = 0; i < pixel_count; ++i) {
         if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
            p[3] = 0;
         p += 4;
      }
   }
   return 1;
}

static int stbi__expand_png_palette(stbi__png *a, stbi_uc *palette, int len, int pal_img_n)
{
   stbi__uint32 i, pixel_count = a->s->img_x * a->s->img_y;
   stbi_uc *p, *temp_out, *orig = a->out;

   p = (stbi_uc *) stbi__malloc_mad2(pixel_count, pal_img_n, 0);
   if (p == NULL) return stbi__err("outofmem", "Out of memory");

   // between here and free(out) below, exitting would leak
   temp_out = p;

   if (pal_img_n == 3) {
      for (i=0; i < pixel_count; ++i) {
         int n = orig[i]*4;
         p[0] = palette[n  ];
         p[1] = palette[n+1];
         p[2] = palette[n+2];
         p += 3;
      }
   } else {
      for (i=0; i < pixel_count; ++i) {
         int n = orig[i]*4;
         p[0] = palette[n  ];
         p[1] = palette[n+1];
         p[2] = palette[n+2];
         p[3] = palette[n+3];
         p += 4;
      }
   }
   STBI_FREE(a->out);
   a->out = temp_out;

   STBI_NOTUSED(len);

   return 1;
}

static int stbi__unpremultiply_on_load_global = 0;
static int stbi__de_iphone_flag_global = 0;

STBIDEF void stbi_set_unpremultiply_on_load(int flag_true_if_should_unpremultiply)
{
   stbi__unpremultiply_on_load_global = flag_true_if_should_unpremultiply;
}

STBIDEF void stbi_convert_iphone_png_to_rgb(int flag_true_if_should_convert)
{
   stbi__de_iphone_flag_global = flag_true_if_should_convert;
}

#ifndef STBI_THREAD_LOCAL
#define stbi__unpremultiply_on_load  stbi__unpremultiply_on_load_global
#define stbi__de_iphone_flag  stbi__de_iphone_flag_global
#else
static STBI_THREAD_LOCAL int stbi__unpremultiply_on_load_local, stbi__unpremultiply_on_load_set;
static STBI_THREAD_LOCAL int stbi__de_iphone_flag_local, stbi__de_iphone_flag_set;

STBIDEF void stbi_set_unpremultiply_on_load_thread(int flag_true_if_should_unpremultiply)
{
   stbi__unpremultiply_on_load_local = flag_true_if_should_unpremultiply;
   stbi__unpremultiply_on_load_set = 1;
}

STBIDEF void stbi_convert_iphone_png_to_rgb_thread(int flag_true_if_should_convert)
{
   stbi__de_iphone_flag_local = flag_true_if_should_convert;
   stbi__de_iphone_flag_set = 1;
}

#define stbi__unpremultiply_on_load  (stbi__unpremultiply_on_load_set           \
                                       ? stbi__unpremultiply_on_load_local      \
                                       : stbi__unpremultiply_on_load_global)
#define stbi__de_iphone_flag  (stbi__de_iphone_flag_set                         \
                                ? stbi__de_iphone_flag_local                    \
                                : stbi__de_iphone_flag_global)
#endif // STBI_THREAD_LOCAL

static void stbi__de_iphone(stbi__png *z)
{
   stbi__context *s = z->s;
   stbi__uint32 i, pixel_count = s->img_x * s->img_y;
   stbi_uc *p = z->out;

   if (s->img_out_n == 3) {  // convert bgr to rgb
      for (i=0; i < pixel_count; ++i) {
         stbi_uc t = p[0];
         p[0] = p[2];
         p[2] = t;
         p += 3;
      }
   } else {
      STBI_ASSERT(s->img_out_n == 4);
      if (stbi__unpremultiply_on_load) {
         // convert bgr to rgb and unpremultiply
         for (i=0; i < pixel_count; ++i) {
            stbi_uc a = p[3];
            stbi_uc t = p[0];
            if (a) {
               stbi_uc half = a / 2;
               p[0] = (p[2] * 255 + half) / a;
               p[1] = (p[1] * 255 + half) / a;
               p[2] = ( t   * 255 + half) / a;
            } else {
               p[0] = p[2];
               p[2] = t;
            }
            p += 4;
         }
      } else {
         // convert bgr to rgb
         for (i=0; i < pixel_count; ++i) {
            stbi_uc t = p[0];
            p[0] = p[2];
            p[2] = t;
            p += 4;
         }
      }
   }
}

#define STBI__PNG_TYPE(a,b,c,d)  (((unsigned) (a) << 24) + ((unsigned) (b) << 16) + ((unsigned) (c) << 8) + (unsigned) (d))

static int stbi__parse_png_file(stbi__png *z, int scan, int req_comp)
{
   stbi_uc palette[1024], pal_img_n=0;
   stbi_uc has_trans=0, tc[3]={0};
   stbi__uint16 tc16[3];
   stbi__uint32 ioff=0, idata_limit=0, i, pal_len=0;
   int first=1,k,interlace=0, color=0, is_iphone=0;
   stbi__context *s = z->s;

   z->expanded = NULL;
   z->idata = NULL;
   z->out = NULL;

   if (!stbi__check_png_header(s)) return 0;

   if (scan == STBI__SCAN_type) return 1;

   for (;;) {
      stbi__pngchunk c = stbi__get_chunk_header(s);
      switch (c.type) {
         case STBI__PNG_TYPE('C','g','B','I'):
            is_iphone = 1;
            stbi__skip(s, c.length);
            break;
         case STBI__PNG_TYPE('I','H','D','R'): {
            int comp,filter;
            if (!first) return stbi__err("multiple IHDR","Corrupt PNG");
            first = 0;
            if (c.length != 13) return stbi__err("bad IHDR len","Corrupt PNG");
            s->img_x = stbi__get32be(s);
            s->img_y = stbi__get32be(s);
            if (s->img_y > STBI_MAX_DIMENSIONS) return stbi__err("too large","Very large image (corrupt?)");
            if (s->img_x > STBI_MAX_DIMENSIONS) return stbi__err("too large","Very large image (corrupt?)");
            z->depth = stbi__get8(s);  if (z->depth != 1 && z->depth != 2 && z->depth != 4 && z->depth != 8 && z->depth != 16)  return stbi__err("1/2/4/8/16-bit only","PNG not supported: 1/2/4/8/16-bit only");
            color = stbi__get8(s);  if (color > 6)         return stbi__err("bad ctype","Corrupt PNG");
            if (color == 3 && z->depth == 16)                  return stbi__err("bad ctype","Corrupt PNG");
            if (color == 3) pal_img_n = 3; else if (color & 1) return stbi__err("bad ctype","Corrupt PNG");
            comp  = stbi__get8(s);  if (comp) return stbi__err("bad comp method","Corrupt PNG");
            filter= stbi__get8(s);  if (filter) return stbi__err("bad filter method","Corrupt PNG");
            interlace = stbi__get8(s); if (interlace>1) return stbi__err("bad interlace method","Corrupt PNG");
            if (!s->img_x || !s->img_y) return stbi__err("0-pixel image","Corrupt PNG");
            if (!pal_img_n) {
               s->img_n = (color & 2 ? 3 : 1) + (color & 4 ? 1 : 0);
               if ((1 << 30) / s->img_x / s->img_n < s->img_y) return stbi__err("too large", "Image too large to decode");
            } else {
               // if paletted, then pal_n is our final components, and
               // img_n is # components to decompress/filter.
               s->img_n = 1;
               if ((1 << 30) / s->img_x / 4 < s->img_y) return stbi__err("too large","Corrupt PNG");
            }
            // even with SCAN_header, have to scan to see if we have a tRNS
            break;
         }

         case STBI__PNG_TYPE('P','L','T','E'):  {
            if (first) return stbi__err("first not IHDR", "Corrupt PNG");
            if (c.length > 256*3) return stbi__err("invalid PLTE","Corrupt PNG");
            pal_len = c.length / 3;
            if (pal_len * 3 != c.length) return stbi__err("invalid PLTE","Corrupt PNG");
            for (i=0; i < pal_len; ++i) {
               palette[i*4+0] = stbi__get8(s);
               palette[i*4+1] = stbi__get8(s);
               palette[i*4+2] = stbi__get8(s);
               palette[i*4+3] = 255;
            }
            break;
         }

         case STBI__PNG_TYPE('t','R','N','S'): {
            if (first) return stbi__err("first not IHDR", "Corrupt PNG");
            if (z->idata) return stbi__err("tRNS after IDAT","Corrupt PNG");
            if (pal_img_n) {
               if (scan == STBI__SCAN_header) { s->img_n = 4; return 1; }
               if (pal_len == 0) return stbi__err("tRNS before PLTE","Corrupt PNG");
               if (c.length > pal_len) return stbi__err("bad tRNS len","Corrupt PNG");
               pal_img_n = 4;
               for (i=0; i < c.length; ++i)
                  palette[i*4+3] = stbi__get8(s);
            } else {
               if (!(s->img_n & 1)) return stbi__err("tRNS with alpha","Corrupt PNG");
               if (c.length != (stbi__uint32) s->img_n*2) return stbi__err("bad tRNS len","Corrupt PNG");
               has_trans = 1;
               // non-paletted with tRNS = constant alpha. if header-scanning, we can stop now.
               if (scan == STBI__SCAN_header) { ++s->img_n; return 1; }
               if (z->depth == 16) {
                  for (k = 0; k < s->img_n && k < 3; ++k) // extra loop test to suppress false GCC warning
                     tc16[k] = (stbi__uint16)stbi__get16be(s); // copy the values as-is
               } else {
                  for (k = 0; k < s->img_n && k < 3; ++k)
                     tc[k] = (stbi_uc)(stbi__get16be(s) & 255) * stbi__depth_scale_table[z->depth]; // non 8-bit images will be larger
               }
            }
            break;
         }

         case STBI__PNG_TYPE('I','D','A','T'): {
            if (first) return stbi__err("first not IHDR", "Corrupt PNG");
            if (pal_img_n && !pal_len) return stbi__err("no PLTE","Corrupt PNG");
            if (scan == STBI__SCAN_header) {
               // header scan definitely stops at first IDAT
               if (pal_img_n)
                  s->img_n = pal_img_n;
               return 1;
            }
            if (c.length > (1u << 30)) return stbi__err("IDAT size limit", "IDAT section larger than 2^30 bytes");
            if ((int)(ioff + c.length) < (int)ioff) return 0;
            if (ioff + c.length > idata_limit) {
               stbi__uint32 idata_limit_old = idata_limit;
               stbi_uc *p;
               if (idata_limit == 0) idata_limit = c.length > 4096 ? c.length : 4096;
               while (ioff + c.length > idata_limit)
                  idata_limit *= 2;
               STBI_NOTUSED(idata_limit_old);
               p = (stbi_uc *) STBI_REALLOC_SIZED(z->idata, idata_limit_old, idata_limit); if (p == NULL) return stbi__err("outofmem", "Out of memory");
               z->idata = p;
            }
            if (!stbi__getn(s, z->idata+ioff,c.length)) return stbi__err("outofdata","Corrupt PNG");
            ioff += c.length;
            break;
         }

         case STBI__PNG_TYPE('I','E','N','D'): {
            stbi__uint32 raw_len, bpl;
            if (first) return stbi__err("first not IHDR", "Corrupt PNG");
            if (scan != STBI__SCAN_load) return 1;
            if (z->idata == NULL) return stbi__err("no IDAT","Corrupt PNG");
            // initial guess for decoded data size to avoid unnecessary reallocs
            bpl = (s->img_x * z->depth + 7) / 8; // bytes per line, per component
            raw_len = bpl * s->img_y * s->img_n /* pixels */ + s->img_y /* filter mode per row */;
            z->expanded = (stbi_uc *) stbi_zlib_decode_malloc_guesssize_headerflag((char *) z->idata, ioff, raw_len, (int *) &raw_len, !is_iphone);
            if (z->expanded == NULL) return 0; // zlib should set error
            STBI_FREE(z->idata); z->idata = NULL;
            if ((req_comp == s->img_n+1 && req_comp != 3 && !pal_img_n) || has_trans)
               s->img_out_n = s->img_n+1;
            else
               s->img_out_n = s->img_n;
            if (!stbi__create_png_image(z, z->expanded, raw_len, s->img_out_n, z->depth, color, interlace)) return 0;
            if (has_trans) {
               if (z->depth == 16) {
                  if (!stbi__compute_transparency16(z, tc16, s->img_out_n)) return 0;
               } else {
                  if (!stbi__compute_transparency(z, tc, s->img_out_n)) return 0;
               }
            }
            if (is_iphone && stbi__de_iphone_flag && s->img_out_n > 2)
               stbi__de_iphone(z);
            if (pal_img_n) {
               // pal_img_n == 3 or 4
               s->img_n = pal_img_n; // record the actual colors we had
               s->img_out_n = pal_img_n;
               if (req_comp >= 3) s->img_out_n = req_comp;
               if (!stbi__expand_png_palette(z, palette, pal_len, s->img_out_n))
                  return 0;
            } else if (has_trans) {
               // non-paletted image with tRNS -> source image has (constant) alpha
               ++s->img_n;
            }
            STBI_FREE(z->expanded); z->expanded = NULL;
            // end of PNG chunk, read and skip CRC
            stbi__get32be(s);
            return 1;
         }

         default:
            // if critical, fail
            if (first) return stbi__err("first not IHDR", "Corrupt PNG");
            if ((c.type & (1 << 29)) == 0) {
               #ifndef STBI_NO_FAILURE_STRINGS
               // not threadsafe
               static char invalid_chunk[] = "XXXX PNG chunk not known";
               invalid_chunk[0] = STBI__BYTECAST(c.type >> 24);
               invalid_chunk[1] = STBI__BYTECAST(c.type >> 16);
               invalid_chunk[2] = STBI__BYTECAST(c.type >>  8);
               invalid_chunk[3] = STBI__BYTECAST(c.type >>  0);
               #endif
               return stbi__err(invalid_chunk, "PNG not supported: unknown PNG chunk type");
            }
            stbi__skip(s, c.length);
            break;
      }
      // end of PNG chunk, read and skip CRC
      stbi__get32be(s);
   }
}

static void *stbi__do_png(stbi__png *p, int *x, int *y, int *n, int req_comp, stbi__result_info *ri)
{
   void *result=NULL;
   if (req_comp < 0 || req_comp > 4) return stbi__errpuc("bad req_comp", "Internal error");
   if (stbi__parse_png_file(p, STBI__SCAN_load, req_comp)) {
      if (p->depth <= 8)
         ri->bits_per_channel = 8;
      else if (p->depth == 16)
         ri->bits_per_channel = 16;
      else
         return stbi__errpuc("bad bits_per_channel", "PNG not supported: unsupported color depth");
      result = p->out;
      p->out = NULL;
      if (req_comp && req_comp != p->s->img_out_n) {
         if (ri->bits_per_channel == 8)
            result = stbi__convert_format((unsigned char *) result, p->s->img_out_n, req_comp, p->s->img_x, p->s->img_y);
         else
            result = stbi__convert_format16((stbi__uint16 *) result, p->s->img_out_n, req_comp, p->s->img_x, p->s->img_y);
         p->s->img_out_n = req_comp;
         if (result == NULL) return result;
      }
      *x = p->s->img_x;
      *y = p->s->img_y;
      if (n) *n = p->s->img_n;
   }
   STBI_FREE(p->out);      p->out      = NULL;
   STBI_FREE(p->expanded); p->expanded = NULL;
   STBI_FREE(p->idata);    p->idata    = NULL;

   return result;
}

static void *stbi__png_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri)
{
   stbi__png p;
   p.s = s;
   return stbi__do_png(&p, x,y,comp,req_comp, ri);
}

static int stbi__png_test(stbi__context *s)
{
   int r;
   r = stbi__check_png_header(s);
   stbi__rewind(s);
   return r;
}

static int stbi__png_info_raw(stbi__png *p, int *x, int *y, int *comp)
{
   if (!stbi__parse_png_file(p, STBI__SCAN_header, 0)) {
      stbi__rewind( p->s );
      return 0;
   }
   if (x) *x = p->s->img_x;
   if (y) *y = p->s->img_y;
   if (comp) *comp = p->s->img_n;
   return 1;
}

static int stbi__png_info(stbi__context *s, int *x, int *y, int *comp)
{
   stbi__png p;
   p.s = s;
   return stbi__png_info_raw(&p, x, y, comp);
}

static int stbi__png_is16(stbi__context *s)
{
   stbi__png p;
   p.s = s;
   if (!stbi__png_info_raw(&p, NULL, NULL, NULL))
	   return 0;
   if (p.depth != 16) {
      stbi__rewind(p.s);
      return 0;
   }
   return 1;
}
#endif

// Microsoft/Windows BMP image

#ifndef STBI_NO_BMP
static int stbi__bmp_test_raw(stbi__context *s)
{
   int r;
   int sz;
   if (stbi__get8(s) != 'B') return 0;
   if (stbi__get8(s) != 'M') return 0;
   stbi__get32le(s); // discard filesize
   stbi__get16le(s); // discard reserved
   stbi__get16le(s); // discard reserved
   stbi__get32le(s); // discard data offset
   sz = stbi__get32le(s);
   r = (sz == 12 || sz == 40 || sz == 56 || sz == 108 || sz == 124);
   return r;
}

static int stbi__bmp_test(stbi__context *s)
{
   int r = stbi__bmp_test_raw(s);
   stbi__rewind(s);
   return r;
}


// returns 0..31 for the highest set bit
static int stbi__high_bit(unsigned int z)
{
   int n=0;
   if (z == 0) return -1;
   if (z >= 0x10000) { n += 16; z >>= 16; }
   if (z >= 0x00100) { n +=  8; z >>=  8; }
   if (z >= 0x00010) { n +=  4; z >>=  4; }
   if (z >= 0x00004) { n +=  2; z >>=  2; }
   if (z >= 0x00002) { n +=  1;/* >>=  1;*/ }
   return n;
}

static int stbi__bitcount(unsigned int a)
{
   a = (a & 0x55555555) + ((a >>  1) & 0x55555555); // max 2
   a = (a & 0x33333333) + ((a >>  2) & 0x33333333); // max 4
   a = (a + (a >> 4)) & 0x0f0f0f0f; // max 8 per 4, now 8 bits
   a = (a + (a >> 8)); // max 16 per 8 bits
   a = (a + (a >> 16)); // max 32 per 8 bits
   return a & 0xff;
}

// extract an arbitrarily-aligned N-bit value (N=bits)
// from v, and then make it 8-bits long and fractionally
// extend it to full full range.
static int stbi__shiftsigned(unsigned int v, int shift, int bits)
{
   static unsigned int mul_table[9] = {
      0,
      0xff/*0b11111111*/, 0x55/*0b01010101*/, 0x49/*0b01001001*/, 0x11/*0b00010001*/,
      0x21/*0b00100001*/, 0x41/*0b01000001*/, 0x81/*0b10000001*/, 0x01/*0b00000001*/,
   };
   static unsigned int shift_table[9] = {
      0, 0,0,1,0,2,4,6,0,
   };
   if (shift < 0)
      v <<= -shift;
   else
      v >>= shift;
   STBI_ASSERT(v < 256);
   v >>= (8-bits);
   STBI_ASSERT(bits >= 0 && bits <= 8);
   return (int) ((unsigned) v * mul_table[bits]) >> shift_table[bits];
}

typedef struct
{
   int bpp, offset, hsz;
   unsigned int mr,mg,mb,ma, all_a;
   int extra_read;
} stbi__bmp_data;

static int stbi__bmp_set_mask_defaults(stbi__bmp_data *info, int compress)
{
   // BI_BITFIELDS specifies masks explicitly, don't override
   if (compress == 3)
      return 1;

   if (compress == 0) {
      if (info->bpp == 16) {
         info->mr = 31u << 10;
         info->mg = 31u <<  5;
         info->mb = 31u <<  0;
      } else if (info->bpp == 32) {
         info->mr = 0xffu << 16;
         info->mg = 0xffu <<  8;
         info->mb = 0xffu <<  0;
         info->ma = 0xffu << 24;
         info->all_a = 0; // if all_a is 0 at end, then we loaded alpha channel but it was all 0
      } else {
         // otherwise, use defaults, which is all-0
         info->mr = info->mg = info->mb = info->ma = 0;
      }
      return 1;
   }
   return 0; // error
}

static void *stbi__bmp_parse_header(stbi__context *s, stbi__bmp_data *info)
{
   int hsz;
   if (stbi__get8(s) != 'B' || stbi__get8(s) != 'M') return stbi__errpuc("not BMP", "Corrupt BMP");
   stbi__get32le(s); // discard filesize
   stbi__get16le(s); // discard reserved
   stbi__get16le(s); // discard reserved
   info->offset = stbi__get32le(s);
   info->hsz = hsz = stbi__get32le(s);
   info->mr = info->mg = info->mb = info->ma = 0;
   info->extra_read = 14;

   if (info->offset < 0) return stbi__errpuc("bad BMP", "bad BMP");

   if (hsz != 12 && hsz != 40 && hsz != 56 && hsz != 108 && hsz != 124) return stbi__errpuc("unknown BMP", "BMP type not supported: unknown");
   if (hsz == 12) {
      s->img_x = stbi__get16le(s);
      s->img_y = stbi__get16le(s);
   } else {
      s->img_x = stbi__get32le(s);
      s->img_y = stbi__get32le(s);
   }
   if (stbi__get16le(s) != 1) return stbi__errpuc("bad BMP", "bad BMP");
   info->bpp = stbi__get16le(s);
   if (hsz != 12) {
      int compress = stbi__get32le(s);
      if (compress == 1 || compress == 2) return stbi__errpuc("BMP RLE", "BMP type not supported: RLE");
      if (compress >= 4) return stbi__errpuc("BMP JPEG/PNG", "BMP type not supported: unsupported compression"); // this includes PNG/JPEG modes
      if (compress == 3 && info->bpp != 16 && info->bpp != 32) return stbi__errpuc("bad BMP", "bad BMP"); // bitfields requires 16 or 32 bits/pixel
      stbi__get32le(s); // discard sizeof
      stbi__get32le(s); // discard hres
      stbi__get32le(s); // discard vres
      stbi__get32le(s); // discard colorsused
      stbi__get32le(s); // discard max important
      if (hsz == 40 || hsz == 56) {
         if (hsz == 56) {
            stbi__get32le(s);
            stbi__get32le(s);
            stbi__get32le(s);
            stbi__get32le(s);
         }
         if (info->bpp == 16 || info->bpp == 32) {
            if (compress == 0) {
               stbi__bmp_set_mask_defaults(info, compress);
            } else if (compress == 3) {
               info->mr = stbi__get32le(s);
               info->mg = stbi__get32le(s);
               info->mb = stbi__get32le(s);
               info->extra_read += 12;
               // not documented, but generated by photoshop and handled by mspaint
               if (info->mr == info->mg && info->mg == info->mb) {
                  // ?!?!?
                  return stbi__errpuc("bad BMP", "bad BMP");
               }
            } else
               return stbi__errpuc("bad BMP", "bad BMP");
         }
      } else {
         // V4/V5 header
         int i;
         if (hsz != 108 && hsz != 124)
            return stbi__errpuc("bad BMP", "bad BMP");
         info->mr = stbi__get32le(s);
         info->mg = stbi__get32le(s);
         info->mb = stbi__get32le(s);
         info->ma = stbi__get32le(s);
         if (compress != 3) // override mr/mg/mb unless in BI_BITFIELDS mode, as per docs
            stbi__bmp_set_mask_defaults(info, compress);
         stbi__get32le(s); // discard color space
         for (i=0; i < 12; ++i)
            stbi__get32le(s); // discard color space parameters
         if (hsz == 124) {
            stbi__get32le(s); // discard rendering intent
            stbi__get32le(s); // discard offset of profile data
            stbi__get32le(s); // discard size of profile data
            stbi__get32le(s); // discard reserved
         }
      }
   }
   return (void *) 1;
}


static void *stbi__bmp_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri)
{
   stbi_uc *out;
   unsigned int mr=0,mg=0,mb=0,ma=0, all_a;
   stbi_uc pal[256][4];
   int psize=0,i,j,width;
   int flip_vertically, pad, target;
   stbi__bmp_data info;
   STBI_NOTUSED(ri);

   info.all_a = 255;
   if (stbi__bmp_parse_header(s, &info) == NULL)
      return NULL; // error code already set

   flip_vertically = ((int) s->img_y) > 0;
   s->img_y = abs((int) s->img_y);

   if (s->img_y > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");
   if (s->img_x > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");

   mr = info.mr;
   mg = info.mg;
   mb = info.mb;
   ma = info.ma;
   all_a = info.all_a;

   if (info.hsz == 12) {
      if (info.bpp < 24)
         psize = (info.offset - info.extra_read - 24) / 3;
   } else {
      if (info.bpp < 16)
         psize = (info.offset - info.extra_read - info.hsz) >> 2;
   }
   if (psize == 0) {
      // accept some number of extra bytes after the header, but if the offset points either to before
      // the header ends or implies a large amount of extra data, reject the file as malformed
      int bytes_read_so_far = s->callback_already_read + (int)(s->img_buffer - s->img_buffer_original);
      int header_limit = 1024; // max we actually read is below 256 bytes currently.
      int extra_data_limit = 256*4; // what ordinarily goes here is a palette; 256 entries*4 bytes is its max size.
      if (bytes_read_so_far <= 0 || bytes_read_so_far > header_limit) {
         return stbi__errpuc("bad header", "Corrupt BMP");
      }
      // we established that bytes_read_so_far is positive and sensible.
      // the first half of this test rejects offsets that are either too small positives, or
      // negative, and guarantees that info.offset >= bytes_read_so_far > 0. this in turn
      // ensures the number computed in the second half of the test can't overflow.
      if (info.offset < bytes_read_so_far || info.offset - bytes_read_so_far > extra_data_limit) {
         return stbi__errpuc("bad offset", "Corrupt BMP");
      } else {
         stbi__skip(s, info.offset - bytes_read_so_far);
      }
   }

   if (info.bpp == 24 && ma == 0xff000000)
      s->img_n = 3;
   else
      s->img_n = ma ? 4 : 3;
   if (req_comp && req_comp >= 3) // we can directly decode 3 or 4
      target = req_comp;
   else
      target = s->img_n; // if they want monochrome, we'll post-convert

   // sanity-check size
   if (!stbi__mad3sizes_valid(target, s->img_x, s->img_y, 0))
      return stbi__errpuc("too large", "Corrupt BMP");

   out = (stbi_uc *) stbi__malloc_mad3(target, s->img_x, s->img_y, 0);
   if (!out) return stbi__errpuc("outofmem", "Out of memory");
   if (info.bpp < 16) {
      int z=0;
      if (psize == 0 || psize > 256) { STBI_FREE(out); return stbi__errpuc("invalid", "Corrupt BMP"); }
      for (i=0; i < psize; ++i) {
         pal[i][2] = stbi__get8(s);
         pal[i][1] = stbi__get8(s);
         pal[i][0] = stbi__get8(s);
         if (info.hsz != 12) stbi__get8(s);
         pal[i][3] = 255;
      }
      stbi__skip(s, info.offset - info.extra_read - info.hsz - psize * (info.hsz == 12 ? 3 : 4));
      if (info.bpp == 1) width = (s->img_x + 7) >> 3;
      else if (info.bpp == 4) width = (s->img_x + 1) >> 1;
      else if (info.bpp == 8) width = s->img_x;
      else { STBI_FREE(out); return stbi__errpuc("bad bpp", "Corrupt BMP"); }
      pad = (-width)&3;
      if (info.bpp == 1) {
         for (j=0; j < (int) s->img_y; ++j) {
            int bit_offset = 7, v = stbi__get8(s);
            for (i=0; i < (int) s->img_x; ++i) {
               int color = (v>>bit_offset)&0x1;
               out[z++] = pal[color][0];
               out[z++] = pal[color][1];
               out[z++] = pal[color][2];
               if (target == 4) out[z++] = 255;
               if (i+1 == (int) s->img_x) break;
               if((--bit_offset) < 0) {
                  bit_offset = 7;
                  v = stbi__get8(s);
               }
            }
            stbi__skip(s, pad);
         }
      } else {
         for (j=0; j < (int) s->img_y; ++j) {
            for (i=0; i < (int) s->img_x; i += 2) {
               int v=stbi__get8(s),v2=0;
               if (info.bpp == 4) {
                  v2 = v & 15;
                  v >>= 4;
               }
               out[z++] = pal[v][0];
               out[z++] = pal[v][1];
               out[z++] = pal[v][2];
               if (target == 4) out[z++] = 255;
               if (i+1 == (int) s->img_x) break;
               v = (info.bpp == 8) ? stbi__get8(s) : v2;
               out[z++] = pal[v][0];
               out[z++] = pal[v][1];
               out[z++] = pal[v][2];
               if (target == 4) out[z++] = 255;
            }
            stbi__skip(s, pad);
         }
      }
   } else {
      int rshift=0,gshift=0,bshift=0,ashift=0,rcount=0,gcount=0,bcount=0,acount=0;
      int z = 0;
      int easy=0;
      stbi__skip(s, info.offset - info.extra_read - info.hsz);
      if (info.bpp == 24) width = 3 * s->img_x;
      else if (info.bpp == 16) width = 2*s->img_x;
      else /* bpp = 32 and pad = 0 */ width=0;
      pad = (-width) & 3;
      if (info.bpp == 24) {
         easy = 1;
      } else if (info.bpp == 32) {
         if (mb == 0xff && mg == 0xff00 && mr == 0x00ff0000 && ma == 0xff000000)
            easy = 2;
      }
      if (!easy) {
         if (!mr || !mg || !mb) { STBI_FREE(out); return stbi__errpuc("bad masks", "Corrupt BMP"); }
         // right shift amt to put high bit in position #7
         rshift = stbi__high_bit(mr)-7; rcount = stbi__bitcount(mr);
         gshift = stbi__high_bit(mg)-7; gcount = stbi__bitcount(mg);
         bshift = stbi__high_bit(mb)-7; bcount = stbi__bitcount(mb);
         ashift = stbi__high_bit(ma)-7; acount = stbi__bitcount(ma);
         if (rcount > 8 || gcount > 8 || bcount > 8 || acount > 8) { STBI_FREE(out); return stbi__errpuc("bad masks", "Corrupt BMP"); }
      }
      for (j=0; j < (int) s->img_y; ++j) {
         if (easy) {
            for (i=0; i < (int) s->img_x; ++i) {
               unsigned char a;
               out[z+2] = stbi__get8(s);
               out[z+1] = stbi__get8(s);
               out[z+0] = stbi__get8(s);
               z += 3;
               a = (easy == 2 ? stbi__get8(s) : 255);
               all_a |= a;
               if (target == 4) out[z++] = a;
            }
         } else {
            int bpp = info.bpp;
            for (i=0; i < (int) s->img_x; ++i) {
               stbi__uint32 v = (bpp == 16 ? (stbi__uint32) stbi__get16le(s) : stbi__get32le(s));
               unsigned int a;
               out[z++] = STBI__BYTECAST(stbi__shiftsigned(v & mr, rshift, rcount));
               out[z++] = STBI__BYTECAST(stbi__shiftsigned(v & mg, gshift, gcount));
               out[z++] = STBI__BYTECAST(stbi__shiftsigned(v & mb, bshift, bcount));
               a = (ma ? stbi__shiftsigned(v & ma, ashift, acount) : 255);
               all_a |= a;
               if (target == 4) out[z++] = STBI__BYTECAST(a);
            }
         }
         stbi__skip(s, pad);
      }
   }

   // if alpha channel is all 0s, replace with all 255s
   if (target == 4 && all_a == 0)
      for (i=4*s->img_x*s->img_y-1; i >= 0; i -= 4)
         out[i] = 255;

   if (flip_vertically) {
      stbi_uc t;
      for (j=0; j < (int) s->img_y>>1; ++j) {
         stbi_uc *p1 = out +      j     *s->img_x*target;
         stbi_uc *p2 = out + (s->img_y-1-j)*s->img_x*target;
         for (i=0; i < (int) s->img_x*target; ++i) {
            t = p1[i]; p1[i] = p2[i]; p2[i] = t;
         }
      }
   }

   if (req_comp && req_comp != target) {
      out = stbi__convert_format(out, target, req_comp, s->img_x, s->img_y);
      if (out == NULL) return out; // stbi__convert_format frees input on failure
   }

   *x = s->img_x;
   *y = s->img_y;
   if (comp) *comp = s->img_n;
   return out;
}
#endif

// Targa Truevision - TGA
// by Jonathan Dummer
#ifndef STBI_NO_TGA
// returns STBI_rgb or whatever, 0 on error
static int stbi__tga_get_comp(int bits_per_pixel, int is_grey, int* is_rgb16)
{
   // only RGB or RGBA (incl. 16bit) or grey allowed
   if (is_rgb16) *is_rgb16 = 0;
   switch(bits_per_pixel) {
      case 8:  return STBI_grey;
      case 16: if(is_grey) return STBI_grey_alpha;
               // fallthrough
      case 15: if(is_rgb16) *is_rgb16 = 1;
               return STBI_rgb;
      case 24: // fallthrough
      case 32: return bits_per_pixel/8;
      default: return 0;
   }
}

static int stbi__tga_info(stbi__context *s, int *x, int *y, int *comp)
{
    int tga_w, tga_h, tga_comp, tga_image_type, tga_bits_per_pixel, tga_colormap_bpp;
    int sz, tga_colormap_type;
    stbi__get8(s);                   // discard Offset
    tga_colormap_type = stbi__get8(s); // colormap type
    if( tga_colormap_type > 1 ) {
        stbi__rewind(s);
        return 0;      // only RGB or indexed allowed
    }
    tga_image_type = stbi__get8(s); // image type
    if ( tga_colormap_type == 1 ) { // colormapped (paletted) image
        if (tga_image_type != 1 && tga_image_type != 9) {
            stbi__rewind(s);
            return 0;
        }
        stbi__skip(s,4);       // skip index of first colormap entry and number of entries
        sz = stbi__get8(s);    //   check bits per palette color entry
        if ( (sz != 8) && (sz != 15) && (sz != 16) && (sz != 24) && (sz != 32) ) {
            stbi__rewind(s);
            return 0;
        }
        stbi__skip(s,4);       // skip image x and y origin
        tga_colormap_bpp = sz;
    } else { // "normal" image w/o colormap - only RGB or grey allowed, +/- RLE
        if ( (tga_image_type != 2) && (tga_image_type != 3) && (tga_image_type != 10) && (tga_image_type != 11) ) {
            stbi__rewind(s);
            return 0; // only RGB or grey allowed, +/- RLE
        }
        stbi__skip(s,9); // skip colormap specification and image x/y origin
        tga_colormap_bpp = 0;
    }
    tga_w = stbi__get16le(s);
    if( tga_w < 1 ) {
        stbi__rewind(s);
        return 0;   // test width
    }
    tga_h = stbi__get16le(s);
    if( tga_h < 1 ) {
        stbi__rewind(s);
        return 0;   // test height
    }
    tga_bits_per_pixel = stbi__get8(s); // bits per pixel
    stbi__get8(s); // ignore alpha bits
    if (tga_colormap_bpp != 0) {
        if((tga_bits_per_pixel != 8) && (tga_bits_per_pixel != 16)) {
            // when using a colormap, tga_bits_per_pixel is the size of the indexes
            // I don't think anything but 8 or 16bit indexes makes sense
            stbi__rewind(s);
            return 0;
        }
        tga_comp = stbi__tga_get_comp(tga_colormap_bpp, 0, NULL);
    } else {
        tga_comp = stbi__tga_get_comp(tga_bits_per_pixel, (tga_image_type == 3) || (tga_image_type == 11), NULL);
    }
    if(!tga_comp) {
      stbi__rewind(s);
      return 0;
    }
    if (x) *x = tga_w;
    if (y) *y = tga_h;
    if (comp) *comp = tga_comp;
    return 1;                   // seems to have passed everything
}

static int stbi__tga_test(stbi__context *s)
{
   int res = 0;
   int sz, tga_color_type;
   stbi__get8(s);      //   discard Offset
   tga_color_type = stbi__get8(s);   //   color type
   if ( tga_color_type > 1 ) goto errorEnd;   //   only RGB or indexed allowed
   sz = stbi__get8(s);   //   image type
   if ( tga_color_type == 1 ) { // colormapped (paletted) image
      if (sz != 1 && sz != 9) goto errorEnd; // colortype 1 demands image type 1 or 9
      stbi__skip(s,4);       // skip index of first colormap entry and number of entries
      sz = stbi__get8(s);    //   check bits per palette color entry
      if ( (sz != 8) && (sz != 15) && (sz != 16) && (sz != 24) && (sz != 32) ) goto errorEnd;
      stbi__skip(s,4);       // skip image x and y origin
   } else { // "normal" image w/o colormap
      if ( (sz != 2) && (sz != 3) && (sz != 10) && (sz != 11) ) goto errorEnd; // only RGB or grey allowed, +/- RLE
      stbi__skip(s,9); // skip colormap specification and image x/y origin
   }
   if ( stbi__get16le(s) < 1 ) goto errorEnd;      //   test width
   if ( stbi__get16le(s) < 1 ) goto errorEnd;      //   test height
   sz = stbi__get8(s);   //   bits per pixel
   if ( (tga_color_type == 1) && (sz != 8) && (sz != 16) ) goto errorEnd; // for colormapped images, bpp is size of an index
   if ( (sz != 8) && (sz != 15) && (sz != 16) && (sz != 24) && (sz != 32) ) goto errorEnd;

   res = 1; // if we got this far, everything's good and we can return 1 instead of 0

errorEnd:
   stbi__rewind(s);
   return res;
}

// read 16bit value and convert to 24bit RGB
static void stbi__tga_read_rgb16(stbi__context *s, stbi_uc* out)
{
   stbi__uint16 px = (stbi__uint16)stbi__get16le(s);
   stbi__uint16 fiveBitMask = 31;
   // we have 3 channels with 5bits each
   int r = (px >> 10) & fiveBitMask;
   int g = (px >> 5) & fiveBitMask;
   int b = px & fiveBitMask;
   // Note that this saves the data in RGB(A) order, so it doesn't need to be swapped later
   out[0] = (stbi_uc)((r * 255)/31);
   out[1] = (stbi_uc)((g * 255)/31);
   out[2] = (stbi_uc)((b * 255)/31);

   // some people claim that the most significant bit might be used for alpha
   // (possibly if an alpha-bit is set in the "image descriptor byte")
   // but that only made 16bit test images completely translucent..
   // so let's treat all 15 and 16bit TGAs as RGB with no alpha.
}

static void *stbi__tga_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri)
{
   //   read in the TGA header stuff
   int tga_offset = stbi__get8(s);
   int tga_indexed = stbi__get8(s);
   int tga_image_type = stbi__get8(s);
   int tga_is_RLE = 0;
   int tga_palette_start = stbi__get16le(s);
   int tga_palette_len = stbi__get16le(s);
   int tga_palette_bits = stbi__get8(s);
   int tga_x_origin = stbi__get16le(s);
   int tga_y_origin = stbi__get16le(s);
   int tga_width = stbi__get16le(s);
   int tga_height = stbi__get16le(s);
   int tga_bits_per_pixel = stbi__get8(s);
   int tga_comp, tga_rgb16=0;
   int tga_inverted = stbi__get8(s);
   // int tga_alpha_bits = tga_inverted & 15; // the 4 lowest bits - unused (useless?)
   //   image data
   unsigned char *tga_data;
   unsigned char *tga_palette = NULL;
   int i, j;
   unsigned char raw_data[4] = {0};
   int RLE_count = 0;
   int RLE_repeating = 0;
   int read_next_pixel = 1;
   STBI_NOTUSED(ri);
   STBI_NOTUSED(tga_x_origin); // @TODO
   STBI_NOTUSED(tga_y_origin); // @TODO

   if (tga_height > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");
   if (tga_width > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");

   //   do a tiny bit of precessing
   if ( tga_image_type >= 8 )
   {
      tga_image_type -= 8;
      tga_is_RLE = 1;
   }
   tga_inverted = 1 - ((tga_inverted >> 5) & 1);

   //   If I'm paletted, then I'll use the number of bits from the palette
   if ( tga_indexed ) tga_comp = stbi__tga_get_comp(tga_palette_bits, 0, &tga_rgb16);
   else tga_comp = stbi__tga_get_comp(tga_bits_per_pixel, (tga_image_type == 3), &tga_rgb16);

   if(!tga_comp) // shouldn't really happen, stbi__tga_test() should have ensured basic consistency
      return stbi__errpuc("bad format", "Can't find out TGA pixelformat");

   //   tga info
   *x = tga_width;
   *y = tga_height;
   if (comp) *comp = tga_comp;

   if (!stbi__mad3sizes_valid(tga_width, tga_height, tga_comp, 0))
      return stbi__errpuc("too large", "Corrupt TGA");

   tga_data = (unsigned char*)stbi__malloc_mad3(tga_width, tga_height, tga_comp, 0);
   if (!tga_data) return stbi__errpuc("outofmem", "Out of memory");

   // skip to the data's starting position (offset usually = 0)
   stbi__skip(s, tga_offset );

   if ( !tga_indexed && !tga_is_RLE && !tga_rgb16 ) {
      for (i=0; i < tga_height; ++i) {
         int row = tga_inverted ? tga_height -i - 1 : i;
         stbi_uc *tga_row = tga_data + row*tga_width*tga_comp;
         stbi__getn(s, tga_row, tga_width * tga_comp);
      }
   } else  {
      //   do I need to load a palette?
      if ( tga_indexed)
      {
         if (tga_palette_len == 0) {  /* you have to have at least one entry! */
            STBI_FREE(tga_data);
            return stbi__errpuc("bad palette", "Corrupt TGA");
         }

         //   any data to skip? (offset usually = 0)
         stbi__skip(s, tga_palette_start );
         //   load the palette
         tga_palette = (unsigned char*)stbi__malloc_mad2(tga_palette_len, tga_comp, 0);
         if (!tga_palette) {
            STBI_FREE(tga_data);
            return stbi__errpuc("outofmem", "Out of memory");
         }
         if (tga_rgb16) {
            stbi_uc *pal_entry = tga_palette;
            STBI_ASSERT(tga_comp == STBI_rgb);
            for (i=0; i < tga_palette_len; ++i) {
               stbi__tga_read_rgb16(s, pal_entry);
               pal_entry += tga_comp;
            }
         } else if (!stbi__getn(s, tga_palette, tga_palette_len * tga_comp)) {
               STBI_FREE(tga_data);
               STBI_FREE(tga_palette);
               return stbi__errpuc("bad palette", "Corrupt TGA");
         }
      }
      //   load the data
      for (i=0; i < tga_width * tga_height; ++i)
      {
         //   if I'm in RLE mode, do I need to get a RLE stbi__pngchunk?
         if ( tga_is_RLE )
         {
            if ( RLE_count == 0 )
            {
               //   yep, get the next byte as a RLE command
               int RLE_cmd = stbi__get8(s);
               RLE_count = 1 + (RLE_cmd & 127);
               RLE_repeating = RLE_cmd >> 7;
               read_next_pixel = 1;
            } else if ( !RLE_repeating )
            {
               read_next_pixel = 1;
            }
         } else
         {
            read_next_pixel = 1;
         }
         //   OK, if I need to read a pixel, do it now
         if ( read_next_pixel )
         {
            //   load however much data we did have
            if ( tga_indexed )
            {
               // read in index, then perform the lookup
               int pal_idx = (tga_bits_per_pixel == 8) ? stbi__get8(s) : stbi__get16le(s);
               if ( pal_idx >= tga_palette_len ) {
                  // invalid index
                  pal_idx = 0;
               }
               pal_idx *= tga_comp;
               for (j = 0; j < tga_comp; ++j) {
                  raw_data[j] = tga_palette[pal_idx+j];
               }
            } else if(tga_rgb16) {
               STBI_ASSERT(tga_comp == STBI_rgb);
               stbi__tga_read_rgb16(s, raw_data);
            } else {
               //   read in the data raw
               for (j = 0; j < tga_comp; ++j) {
                  raw_data[j] = stbi__get8(s);
               }
            }
            //   clear the reading flag for the next pixel
            read_next_pixel = 0;
         } // end of reading a pixel

         // copy data
         for (j = 0; j < tga_comp; ++j)
           tga_data[i*tga_comp+j] = raw_data[j];

         //   in case we're in RLE mode, keep counting down
         --RLE_count;
      }
      //   do I need to invert the image?
      if ( tga_inverted )
      {
         for (j = 0; j*2 < tga_height; ++j)
         {
            int index1 = j * tga_width * tga_comp;
            int index2 = (tga_height - 1 - j) * tga_width * tga_comp;
            for (i = tga_width * tga_comp; i > 0; --i)
            {
               unsigned char temp = tga_data[index1];
               tga_data[index1] = tga_data[index2];
               tga_data[index2] = temp;
               ++index1;
               ++index2;
            }
         }
      }
      //   clear my palette, if I had one
      if ( tga_palette != NULL )
      {
         STBI_FREE( tga_palette );
      }
   }

   // swap RGB - if the source data was RGB16, it already is in the right order
   if (tga_comp >= 3 && !tga_rgb16)
   {
      unsigned char* tga_pixel = tga_data;
      for (i=0; i < tga_width * tga_height; ++i)
      {
         unsigned char temp = tga_pixel[0];
         tga_pixel[0] = tga_pixel[2];
         tga_pixel[2] = temp;
         tga_pixel += tga_comp;
      }
   }

   // convert to target component count
   if (req_comp && req_comp != tga_comp)
      tga_data = stbi__convert_format(tga_data, tga_comp, req_comp, tga_width, tga_height);

   //   the things I do to get rid of an error message, and yet keep
   //   Microsoft's C compilers happy... [8^(
   tga_palette_start = tga_palette_len = tga_palette_bits =
         tga_x_origin = tga_y_origin = 0;
   STBI_NOTUSED(tga_palette_start);
   //   OK, done
   return tga_data;
}
#endif

// *************************************************************************************************
// Photoshop PSD loader -- PD by Thatcher Ulrich, integration by Nicolas Schulz, tweaked by STB

#ifndef STBI_NO_PSD
static int stbi__psd_test(stbi__context *s)
{
   int r = (stbi__get32be(s) == 0x38425053);
   stbi__rewind(s);
   return r;
}

static int stbi__psd_decode_rle(stbi__context *s, stbi_uc *p, int pixelCount)
{
   int count, nleft, len;

   count = 0;
   while ((nleft = pixelCount - count) > 0) {
      len = stbi__get8(s);
      if (len == 128) {
         // No-op.
      } else if (len < 128) {
         // Copy next len+1 bytes literally.
         len++;
         if (len > nleft) return 0; // corrupt data
         count += len;
         while (len) {
            *p = stbi__get8(s);
            p += 4;
            len--;
         }
      } else if (len > 128) {
         stbi_uc   val;
         // Next -len+1 bytes in the dest are replicated from next source byte.
         // (Interpret len as a negative 8-bit int.)
         len = 257 - len;
         if (len > nleft) return 0; // corrupt data
         val = stbi__get8(s);
         count += len;
         while (len) {
            *p = val;
            p += 4;
            len--;
         }
      }
   }

   return 1;
}

static void *stbi__psd_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri, int bpc)
{
   int pixelCount;
   int channelCount, compression;
   int channel, i;
   int bitdepth;
   int w,h;
   stbi_uc *out;
   STBI_NOTUSED(ri);

   // Check identifier
   if (stbi__get32be(s) != 0x38425053)   // "8BPS"
      return stbi__errpuc("not PSD", "Corrupt PSD image");

   // Check file type version.
   if (stbi__get16be(s) != 1)
      return stbi__errpuc("wrong version", "Unsupported version of PSD image");

   // Skip 6 reserved bytes.
   stbi__skip(s, 6 );

   // Read the number of channels (R, G, B, A, etc).
   channelCount = stbi__get16be(s);
   if (channelCount < 0 || channelCount > 16)
      return stbi__errpuc("wrong channel count", "Unsupported number of channels in PSD image");

   // Read the rows and columns of the image.
   h = stbi__get32be(s);
   w = stbi__get32be(s);

   if (h > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");
   if (w > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");

   // Make sure the depth is 8 bits.
   bitdepth = stbi__get16be(s);
   if (bitdepth != 8 && bitdepth != 16)
      return stbi__errpuc("unsupported bit depth", "PSD bit depth is not 8 or 16 bit");

   // Make sure the color mode is RGB.
   // Valid options are:
   //   0: Bitmap
   //   1: Grayscale
   //   2: Indexed color
   //   3: RGB color
   //   4: CMYK color
   //   7: Multichannel
   //   8: Duotone
   //   9: Lab color
   if (stbi__get16be(s) != 3)
      return stbi__errpuc("wrong color format", "PSD is not in RGB color format");

   // Skip the Mode Data.  (It's the palette for indexed color; other info for other modes.)
   stbi__skip(s,stbi__get32be(s) );

   // Skip the image resources.  (resolution, pen tool paths, etc)
   stbi__skip(s, stbi__get32be(s) );

   // Skip the reserved data.
   stbi__skip(s, stbi__get32be(s) );

   // Find out if the data is compressed.
   // Known values:
   //   0: no compression
   //   1: RLE compressed
   compression = stbi__get16be(s);
   if (compression > 1)
      return stbi__errpuc("bad compression", "PSD has an unknown compression format");

   // Check size
   if (!stbi__mad3sizes_valid(4, w, h, 0))
      return stbi__errpuc("too large", "Corrupt PSD");

   // Create the destination image.

   if (!compression && bitdepth == 16 && bpc == 16) {
      out = (stbi_uc *) stbi__malloc_mad3(8, w, h, 0);
      ri->bits_per_channel = 16;
   } else
      out = (stbi_uc *) stbi__malloc(4 * w*h);

   if (!out) return stbi__errpuc("outofmem", "Out of memory");
   pixelCount = w*h;

   // Initialize the data to zero.
   //memset( out, 0, pixelCount * 4 );

   // Finally, the image data.
   if (compression) {
      // RLE as used by .PSD and .TIFF
      // Loop until you get the number of unpacked bytes you are expecting:
      //     Read the next source byte into n.
      //     If n is between 0 and 127 inclusive, copy the next n+1 bytes literally.
      //     Else if n is between -127 and -1 inclusive, copy the next byte -n+1 times.
      //     Else if n is 128, noop.
      // Endloop

      // The RLE-compressed data is preceded by a 2-byte data count for each row in the data,
      // which we're going to just skip.
      stbi__skip(s, h * channelCount * 2 );

      // Read the RLE data by channel.
      for (channel = 0; channel < 4; channel++) {
         stbi_uc *p;

         p = out+channel;
         if (channel >= channelCount) {
            // Fill this channel with default data.
            for (i = 0; i < pixelCount; i++, p += 4)
               *p = (channel == 3 ? 255 : 0);
         } else {
            // Read the RLE data.
            if (!stbi__psd_decode_rle(s, p, pixelCount)) {
               STBI_FREE(out);
               return stbi__errpuc("corrupt", "bad RLE data");
            }
         }
      }

   } else {
      // We're at the raw image data.  It's each channel in order (Red, Green, Blue, Alpha, ...)
      // where each channel consists of an 8-bit (or 16-bit) value for each pixel in the image.

      // Read the data by channel.
      for (channel = 0; channel < 4; channel++) {
         if (channel >= channelCount) {
            // Fill this channel with default data.
            if (bitdepth == 16 && bpc == 16) {
               stbi__uint16 *q = ((stbi__uint16 *) out) + channel;
               stbi__uint16 val = channel == 3 ? 65535 : 0;
               for (i = 0; i < pixelCount; i++, q += 4)
                  *q = val;
            } else {
               stbi_uc *p = out+channel;
               stbi_uc val = channel == 3 ? 255 : 0;
               for (i = 0; i < pixelCount; i++, p += 4)
                  *p = val;
            }
         } else {
            if (ri->bits_per_channel == 16) {    // output bpc
               stbi__uint16 *q = ((stbi__uint16 *) out) + channel;
               for (i = 0; i < pixelCount; i++, q += 4)
                  *q = (stbi__uint16) stbi__get16be(s);
            } else {
               stbi_uc *p = out+channel;
               if (bitdepth == 16) {  // input bpc
                  for (i = 0; i < pixelCount; i++, p += 4)
                     *p = (stbi_uc) (stbi__get16be(s) >> 8);
               } else {
                  for (i = 0; i < pixelCount; i++, p += 4)
                     *p = stbi__get8(s);
               }
            }
         }
      }
   }

   // remove weird white matte from PSD
   if (channelCount >= 4) {
      if (ri->bits_per_channel == 16) {
         for (i=0; i < w*h; ++i) {
            stbi__uint16 *pixel = (stbi__uint16 *) out + 4*i;
            if (pixel[3] != 0 && pixel[3] != 65535) {
               float a = pixel[3] / 65535.0f;
               float ra = 1.0f / a;
               float inv_a = 65535.0f * (1 - ra);
               pixel[0] = (stbi__uint16) (pixel[0]*ra + inv_a);
               pixel[1] = (stbi__uint16) (pixel[1]*ra + inv_a);
               pixel[2] = (stbi__uint16) (pixel[2]*ra + inv_a);
            }
         }
      } else {
         for (i=0; i < w*h; ++i) {
            unsigned char *pixel = out + 4*i;
            if (pixel[3] != 0 && pixel[3] != 255) {
               float a = pixel[3] / 255.0f;
               float ra = 1.0f / a;
               float inv_a = 255.0f * (1 - ra);
               pixel[0] = (unsigned char) (pixel[0]*ra + inv_a);
               pixel[1] = (unsigned char) (pixel[1]*ra + inv_a);
               pixel[2] = (unsigned char) (pixel[2]*ra + inv_a);
            }
         }
      }
   }

   // convert to desired output format
   if (req_comp && req_comp != 4) {
      if (ri->bits_per_channel == 16)
         out = (stbi_uc *) stbi__convert_format16((stbi__uint16 *) out, 4, req_comp, w, h);
      else
         out = stbi__convert_format(out, 4, req_comp, w, h);
      if (out == NULL) return out; // stbi__convert_format frees input on failure
   }

   if (comp) *comp = 4;
   *y = h;
   *x = w;

   return out;
}
#endif

// *************************************************************************************************
// Softimage PIC loader
// by Tom Seddon
//
// See http://softimage.wiki.softimage.com/index.php/INFO:_PIC_file_format
// See http://ozviz.wasp.uwa.edu.au/~pbourke/dataformats/softimagepic/

#ifndef STBI_NO_PIC
static int stbi__pic_is4(stbi__context *s,const char *str)
{
   int i;
   for (i=0; i<4; ++i)
      if (stbi__get8(s) != (stbi_uc)str[i])
         return 0;

   return 1;
}

static int stbi__pic_test_core(stbi__context *s)
{
   int i;

   if (!stbi__pic_is4(s,"\x53\x80\xF6\x34"))
      return 0;

   for(i=0;i<84;++i)
      stbi__get8(s);

   if (!stbi__pic_is4(s,"PICT"))
      return 0;

   return 1;
}

typedef struct
{
   stbi_uc size,type,channel;
} stbi__pic_packet;

static stbi_uc *stbi__readval(stbi__context *s, int channel, stbi_uc *dest)
{
   int mask=0x80, i;

   for (i=0; i<4; ++i, mask>>=1) {
      if (channel & mask) {
         if (stbi__at_eof(s)) return stbi__errpuc("bad file","PIC file too short");
         dest[i]=stbi__get8(s);
      }
   }

   return dest;
}

static void stbi__copyval(int channel,stbi_uc *dest,const stbi_uc *src)
{
   int mask=0x80,i;

   for (i=0;i<4; ++i, mask>>=1)
      if (channel&mask)
         dest[i]=src[i];
}

static stbi_uc *stbi__pic_load_core(stbi__context *s,int width,int height,int *comp, stbi_uc *result)
{
   int act_comp=0,num_packets=0,y,chained;
   stbi__pic_packet packets[10];

   // this will (should...) cater for even some bizarre stuff like having data
    // for the same channel in multiple packets.
   do {
      stbi__pic_packet *packet;

      if (num_packets==sizeof(packets)/sizeof(packets[0]))
         return stbi__errpuc("bad format","too many packets");

      packet = &packets[num_packets++];

      chained = stbi__get8(s);
      packet->size    = stbi__get8(s);
      packet->type    = stbi__get8(s);
      packet->channel = stbi__get8(s);

      act_comp |= packet->channel;

      if (stbi__at_eof(s))          return stbi__errpuc("bad file","file too short (reading packets)");
      if (packet->size != 8)  return stbi__errpuc("bad format","packet isn't 8bpp");
   } while (chained);

   *comp = (act_comp & 0x10 ? 4 : 3); // has alpha channel?

   for(y=0; y<height; ++y) {
      int packet_idx;

      for(packet_idx=0; packet_idx < num_packets; ++packet_idx) {
         stbi__pic_packet *packet = &packets[packet_idx];
         stbi_uc *dest = result+y*width*4;

         switch (packet->type) {
            default:
               return stbi__errpuc("bad format","packet has bad compression type");

            case 0: {//uncompressed
               int x;

               for(x=0;x<width;++x, dest+=4)
                  if (!stbi__readval(s,packet->channel,dest))
                     return 0;
               break;
            }

            case 1://Pure RLE
               {
                  int left=width, i;

                  while (left>0) {
                     stbi_uc count,value[4];

                     count=stbi__get8(s);
                     if (stbi__at_eof(s))   return stbi__errpuc("bad file","file too short (pure read count)");

                     if (count > left)
                        count = (stbi_uc) left;

                     if (!stbi__readval(s,packet->channel,value))  return 0;

                     for(i=0; i<count; ++i,dest+=4)
                        stbi__copyval(packet->channel,dest,value);
                     left -= count;
                  }
               }
               break;

            case 2: {//Mixed RLE
               int left=width;
               while (left>0) {
                  int count = stbi__get8(s), i;
                  if (stbi__at_eof(s))  return stbi__errpuc("bad file","file too short (mixed read count)");

                  if (count >= 128) { // Repeated
                     stbi_uc value[4];

                     if (count==128)
                        count = stbi__get16be(s);
                     else
                        count -= 127;
                     if (count > left)
                        return stbi__errpuc("bad file","scanline overrun");

                     if (!stbi__readval(s,packet->channel,value))
                        return 0;

                     for(i=0;i<count;++i, dest += 4)
                        stbi__copyval(packet->channel,dest,value);
                  } else { // Raw
                     ++count;
                     if (count>left) return stbi__errpuc("bad file","scanline overrun");

                     for(i=0;i<count;++i, dest+=4)
                        if (!stbi__readval(s,packet->channel,dest))
                           return 0;
                  }
                  left-=count;
               }
               break;
            }
         }
      }
   }

   return result;
}

static void *stbi__pic_load(stbi__context *s,int *px,int *py,int *comp,int req_comp, stbi__result_info *ri)
{
   stbi_uc *result;
   int i, x,y, internal_comp;
   STBI_NOTUSED(ri);

   if (!comp) comp = &internal_comp;

   for (i=0; i<92; ++i)
      stbi__get8(s);

   x = stbi__get16be(s);
   y = stbi__get16be(s);

   if (y > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");
   if (x > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");

   if (stbi__at_eof(s))  return stbi__errpuc("bad file","file too short (pic header)");
   if (!stbi__mad3sizes_valid(x, y, 4, 0)) return stbi__errpuc("too large", "PIC image too large to decode");

   stbi__get32be(s); //skip `ratio'
   stbi__get16be(s); //skip `fields'
   stbi__get16be(s); //skip `pad'

   // intermediate buffer is RGBA
   result = (stbi_uc *) stbi__malloc_mad3(x, y, 4, 0);
   if (!result) return stbi__errpuc("outofmem", "Out of memory");
   memset(result, 0xff, x*y*4);

   if (!stbi__pic_load_core(s,x,y,comp, result)) {
      STBI_FREE(result);
      result=0;
   }
   *px = x;
   *py = y;
   if (req_comp == 0) req_comp = *comp;
   result=stbi__convert_format(result,4,req_comp,x,y);

   return result;
}

static int stbi__pic_test(stbi__context *s)
{
   int r = stbi__pic_test_core(s);
   stbi__rewind(s);
   return r;
}
#endif

// *************************************************************************************************
// GIF loader -- public domain by Jean-Marc Lienher -- simplified/shrunk by stb

#ifndef STBI_NO_GIF
typedef struct
{
   stbi__int16 prefix;
   stbi_uc first;
   stbi_uc suffix;
} stbi__gif_lzw;

typedef struct
{
   int w,h;
   stbi_uc *out;                 // output buffer (always 4 components)
   stbi_uc *background;          // The current "background" as far as a gif is concerned
   stbi_uc *history;
   int flags, bgindex, ratio, transparent, eflags;
   stbi_uc  pal[256][4];
   stbi_uc lpal[256][4];
   stbi__gif_lzw codes[8192];
   stbi_uc *color_table;
   int parse, step;
   int lflags;
   int start_x, start_y;
   int max_x, max_y;
   int cur_x, cur_y;
   int line_size;
   int delay;
} stbi__gif;

static int stbi__gif_test_raw(stbi__context *s)
{
   int sz;
   if (stbi__get8(s) != 'G' || stbi__get8(s) != 'I' || stbi__get8(s) != 'F' || stbi__get8(s) != '8') return 0;
   sz = stbi__get8(s);
   if (sz != '9' && sz != '7') return 0;
   if (stbi__get8(s) != 'a') return 0;
   return 1;
}

static int stbi__gif_test(stbi__context *s)
{
   int r = stbi__gif_test_raw(s);
   stbi__rewind(s);
   return r;
}

static void stbi__gif_parse_colortable(stbi__context *s, stbi_uc pal[256][4], int num_entries, int transp)
{
   int i;
   for (i=0; i < num_entries; ++i) {
      pal[i][2] = stbi__get8(s);
      pal[i][1] = stbi__get8(s);
      pal[i][0] = stbi__get8(s);
      pal[i][3] = transp == i ? 0 : 255;
   }
}

static int stbi__gif_header(stbi__context *s, stbi__gif *g, int *comp, int is_info)
{
   stbi_uc version;
   if (stbi__get8(s) != 'G' || stbi__get8(s) != 'I' || stbi__get8(s) != 'F' || stbi__get8(s) != '8')
      return stbi__err("not GIF", "Corrupt GIF");

   version = stbi__get8(s);
   if (version != '7' && version != '9')    return stbi__err("not GIF", "Corrupt GIF");
   if (stbi__get8(s) != 'a')                return stbi__err("not GIF", "Corrupt GIF");

   stbi__g_failure_reason = "";
   g->w = stbi__get16le(s);
   g->h = stbi__get16le(s);
   g->flags = stbi__get8(s);
   g->bgindex = stbi__get8(s);
   g->ratio = stbi__get8(s);
   g->transparent = -1;

   if (g->w > STBI_MAX_DIMENSIONS) return stbi__err("too large","Very large image (corrupt?)");
   if (g->h > STBI_MAX_DIMENSIONS) return stbi__err("too large","Very large image (corrupt?)");

   if (comp != 0) *comp = 4;  // can't actually tell whether it's 3 or 4 until we parse the comments

   if (is_info) return 1;

   if (g->flags & 0x80)
      stbi__gif_parse_colortable(s,g->pal, 2 << (g->flags & 7), -1);

   return 1;
}

static int stbi__gif_info_raw(stbi__context *s, int *x, int *y, int *comp)
{
   stbi__gif* g = (stbi__gif*) stbi__malloc(sizeof(stbi__gif));
   if (!g) return stbi__err("outofmem", "Out of memory");
   if (!stbi__gif_header(s, g, comp, 1)) {
      STBI_FREE(g);
      stbi__rewind( s );
      return 0;
   }
   if (x) *x = g->w;
   if (y) *y = g->h;
   STBI_FREE(g);
   return 1;
}

static void stbi__out_gif_code(stbi__gif *g, stbi__uint16 code)
{
   stbi_uc *p, *c;
   int idx;

   // recurse to decode the prefixes, since the linked-list is backwards,
   // and working backwards through an interleaved image would be nasty
   if (g->codes[code].prefix >= 0)
      stbi__out_gif_code(g, g->codes[code].prefix);

   if (g->cur_y >= g->max_y) return;

   idx = g->cur_x + g->cur_y;
   p = &g->out[idx];
   g->history[idx / 4] = 1;

   c = &g->color_table[g->codes[code].suffix * 4];
   if (c[3] > 128) { // don't render transparent pixels;
      p[0] = c[2];
      p[1] = c[1];
      p[2] = c[0];
      p[3] = c[3];
   }
   g->cur_x += 4;

   if (g->cur_x >= g->max_x) {
      g->cur_x = g->start_x;
      g->cur_y += g->step;

      while (g->cur_y >= g->max_y && g->parse > 0) {
         g->step = (1 << g->parse) * g->line_size;
         g->cur_y = g->start_y + (g->step >> 1);
         --g->parse;
      }
   }
}

static stbi_uc *stbi__process_gif_raster(stbi__context *s, stbi__gif *g)
{
   stbi_uc lzw_cs;
   stbi__int32 len, init_code;
   stbi__uint32 first;
   stbi__int32 codesize, codemask, avail, oldcode, bits, valid_bits, clear;
   stbi__gif_lzw *p;

   lzw_cs = stbi__get8(s);
   if (lzw_cs > 12) return NULL;
   clear = 1 << lzw_cs;
   first = 1;
   codesize = lzw_cs + 1;
   codemask = (1 << codesize) - 1;
   bits = 0;
   valid_bits = 0;
   for (init_code = 0; init_code < clear; init_code++) {
      g->codes[init_code].prefix = -1;
      g->codes[init_code].first = (stbi_uc) init_code;
      g->codes[init_code].suffix = (stbi_uc) init_code;
   }

   // support no starting clear code
   avail = clear+2;
   oldcode = -1;

   len = 0;
   for(;;) {
      if (valid_bits < codesize) {
         if (len == 0) {
            len = stbi__get8(s); // start new block
            if (len == 0)
               return g->out;
         }
         --len;
         bits |= (stbi__int32) stbi__get8(s) << valid_bits;
         valid_bits += 8;
      } else {
         stbi__int32 code = bits & codemask;
         bits >>= codesize;
         valid_bits -= codesize;
         // @OPTIMIZE: is there some way we can accelerate the non-clear path?
         if (code == clear) {  // clear code
            codesize = lzw_cs + 1;
            codemask = (1 << codesize) - 1;
            avail = clear + 2;
            oldcode = -1;
            first = 0;
         } else if (code == clear + 1) { // end of stream code
            stbi__skip(s, len);
            while ((len = stbi__get8(s)) > 0)
               stbi__skip(s,len);
            return g->out;
         } else if (code <= avail) {
            if (first) {
               return stbi__errpuc("no clear code", "Corrupt GIF");
            }

            if (oldcode >= 0) {
               p = &g->codes[avail++];
               if (avail > 8192) {
                  return stbi__errpuc("too many codes", "Corrupt GIF");
               }

               p->prefix = (stbi__int16) oldcode;
               p->first = g->codes[oldcode].first;
               p->suffix = (code == avail) ? p->first : g->codes[code].first;
            } else if (code == avail)
               return stbi__errpuc("illegal code in raster", "Corrupt GIF");

            stbi__out_gif_code(g, (stbi__uint16) code);

            if ((avail & codemask) == 0 && avail <= 0x0FFF) {
               codesize++;
               codemask = (1 << codesize) - 1;
            }

            oldcode = code;
         } else {
            return stbi__errpuc("illegal code in raster", "Corrupt GIF");
         }
      }
   }
}

// this function is designed to support animated gifs, although stb_image doesn't support it
// two back is the image from two frames ago, used for a very specific disposal format
static stbi_uc *stbi__gif_load_next(stbi__context *s, stbi__gif *g, int *comp, int req_comp, stbi_uc *two_back)
{
   int dispose;
   int first_frame;
   int pi;
   int pcount;
   STBI_NOTUSED(req_comp);

   // on first frame, any non-written pixels get the background colour (non-transparent)
   first_frame = 0;
   if (g->out == 0) {
      if (!stbi__gif_header(s, g, comp,0)) return 0; // stbi__g_failure_reason set by stbi__gif_header
      if (!stbi__mad3sizes_valid(4, g->w, g->h, 0))
         return stbi__errpuc("too large", "GIF image is too large");
      pcount = g->w * g->h;
      g->out = (stbi_uc *) stbi__malloc(4 * pcount);
      g->background = (stbi_uc *) stbi__malloc(4 * pcount);
      g->history = (stbi_uc *) stbi__malloc(pcount);
      if (!g->out || !g->background || !g->history)
         return stbi__errpuc("outofmem", "Out of memory");

      // image is treated as "transparent" at the start - ie, nothing overwrites the current background;
      // background colour is only used for pixels that are not rendered first frame, after that "background"
      // color refers to the color that was there the previous frame.
      memset(g->out, 0x00, 4 * pcount);
      memset(g->background, 0x00, 4 * pcount); // state of the background (starts transparent)
      memset(g->history, 0x00, pcount);        // pixels that were affected previous frame
      first_frame = 1;
   } else {
      // second frame - how do we dispose of the previous one?
      dispose = (g->eflags & 0x1C) >> 2;
      pcount = g->w * g->h;

      if ((dispose == 3) && (two_back == 0)) {
         dispose = 2; // if I don't have an image to revert back to, default to the old background
      }

      if (dispose == 3) { // use previous graphic
         for (pi = 0; pi < pcount; ++pi) {
            if (g->history[pi]) {
               memcpy( &g->out[pi * 4], &two_back[pi * 4], 4 );
            }
         }
      } else if (dispose == 2) {
         // restore what was changed last frame to background before that frame;
         for (pi = 0; pi < pcount; ++pi) {
            if (g->history[pi]) {
               memcpy( &g->out[pi * 4], &g->background[pi * 4], 4 );
            }
         }
      } else {
         // This is a non-disposal case eithe way, so just
         // leave the pixels as is, and they will become the new background
         // 1: do not dispose
         // 0:  not specified.
      }

      // background is what out is after the undoing of the previou frame;
      memcpy( g->background, g->out, 4 * g->w * g->h );
   }

   // clear my history;
   memset( g->history, 0x00, g->w * g->h );        // pixels that were affected previous frame

   for (;;) {
      int tag = stbi__get8(s);
      switch (tag) {
         case 0x2C: /* Image Descriptor */
         {
            stbi__int32 x, y, w, h;
            stbi_uc *o;

            x = stbi__get16le(s);
            y = stbi__get16le(s);
            w = stbi__get16le(s);
            h = stbi__get16le(s);
            if (((x + w) > (g->w)) || ((y + h) > (g->h)))
               return stbi__errpuc("bad Image Descriptor", "Corrupt GIF");

            g->line_size = g->w * 4;
            g->start_x = x * 4;
            g->start_y = y * g->line_size;
            g->max_x   = g->start_x + w * 4;
            g->max_y   = g->start_y + h * g->line_size;
            g->cur_x   = g->start_x;
            g->cur_y   = g->start_y;

            // if the width of the specified rectangle is 0, that means
            // we may not see *any* pixels or the image is malformed;
            // to make sure this is caught, move the current y down to
            // max_y (which is what out_gif_code checks).
            if (w == 0)
               g->cur_y = g->max_y;

            g->lflags = stbi__get8(s);

            if (g->lflags & 0x40) {
               g->step = 8 * g->line_size; // first interlaced spacing
               g->parse = 3;
            } else {
               g->step = g->line_size;
               g->parse = 0;
            }

            if (g->lflags & 0x80) {
               stbi__gif_parse_colortable(s,g->lpal, 2 << (g->lflags & 7), g->eflags & 0x01 ? g->transparent : -1);
               g->color_table = (stbi_uc *) g->lpal;
            } else if (g->flags & 0x80) {
               g->color_table = (stbi_uc *) g->pal;
            } else
               return stbi__errpuc("missing color table", "Corrupt GIF");

            o = stbi__process_gif_raster(s, g);
            if (!o) return NULL;

            // if this was the first frame,
            pcount = g->w * g->h;
            if (first_frame && (g->bgindex > 0)) {
               // if first frame, any pixel not drawn to gets the background color
               for (pi = 0; pi < pcount; ++pi) {
                  if (g->history[pi] == 0) {
                     g->pal[g->bgindex][3] = 255; // just in case it was made transparent, undo that; It will be reset next frame if need be;
                     memcpy( &g->out[pi * 4], &g->pal[g->bgindex], 4 );
                  }
               }
            }

            return o;
         }

         case 0x21: // Comment Extension.
         {
            int len;
            int ext = stbi__get8(s);
            if (ext == 0xF9) { // Graphic Control Extension.
               len = stbi__get8(s);
               if (len == 4) {
                  g->eflags = stbi__get8(s);
                  g->delay = 10 * stbi__get16le(s); // delay - 1/100th of a second, saving as 1/1000ths.

                  // unset old transparent
                  if (g->transparent >= 0) {
                     g->pal[g->transparent][3] = 255;
                  }
                  if (g->eflags & 0x01) {
                     g->transparent = stbi__get8(s);
                     if (g->transparent >= 0) {
                        g->pal[g->transparent][3] = 0;
                     }
                  } else {
                     // don't need transparent
                     stbi__skip(s, 1);
                     g->transparent = -1;
                  }
               } else {
                  stbi__skip(s, len);
                  break;
               }
            }
            while ((len = stbi__get8(s)) != 0) {
               stbi__skip(s, len);
            }
            break;
         }

         case 0x3B: // gif stream termination code
            return (stbi_uc *) s; // using '1' causes warning on some compilers

         default:
            return stbi__errpuc("unknown code", "Corrupt GIF");
      }
   }
}

static void *stbi__load_gif_main_outofmem(stbi__gif *g, stbi_uc *out, int **delays)
{
   STBI_FREE(g->out);
   STBI_FREE(g->history);
   STBI_FREE(g->background);

   if (out) STBI_FREE(out);
   if (delays && *delays) STBI_FREE(*delays);
   return stbi__errpuc("outofmem", "Out of memory");
}

static void *stbi__load_gif_main(stbi__context *s, int **delays, int *x, int *y, int *z, int *comp, int req_comp)
{
   if (stbi__gif_test(s)) {
      int layers = 0;
      stbi_uc *u = 0;
      stbi_uc *out = 0;
      stbi_uc *two_back = 0;
      stbi__gif g;
      int stride;
      int out_size = 0;
      int delays_size = 0;

      STBI_NOTUSED(out_size);
      STBI_NOTUSED(delays_size);

      memset(&g, 0, sizeof(g));
      if (delays) {
         *delays = 0;
      }

      do {
         u = stbi__gif_load_next(s, &g, comp, req_comp, two_back);
         if (u == (stbi_uc *) s) u = 0;  // end of animated gif marker

         if (u) {
            *x = g.w;
            *y = g.h;
            ++layers;
            stride = g.w * g.h * 4;

            if (out) {
               void *tmp = (stbi_uc*) STBI_REALLOC_SIZED( out, out_size, layers * stride );
               if (!tmp)
                  return stbi__load_gif_main_outofmem(&g, out, delays);
               else {
                   out = (stbi_uc*) tmp;
                   out_size = layers * stride;
               }

               if (delays) {
                  int *new_delays = (int*) STBI_REALLOC_SIZED( *delays, delays_size, sizeof(int) * layers );
                  if (!new_delays)
                     return stbi__load_gif_main_outofmem(&g, out, delays);
                  *delays = new_delays;
                  delays_size = layers * sizeof(int);
               }
            } else {
               out = (stbi_uc*)stbi__malloc( layers * stride );
               if (!out)
                  return stbi__load_gif_main_outofmem(&g, out, delays);
               out_size = layers * stride;
               if (delays) {
                  *delays = (int*) stbi__malloc( layers * sizeof(int) );
                  if (!*delays)
                     return stbi__load_gif_main_outofmem(&g, out, delays);
                  delays_size = layers * sizeof(int);
               }
            }
            memcpy( out + ((layers - 1) * stride), u, stride );
            if (layers >= 2) {
               two_back = out - 2 * stride;
            }

            if (delays) {
               (*delays)[layers - 1U] = g.delay;
            }
         }
      } while (u != 0);

      // free temp buffer;
      STBI_FREE(g.out);
      STBI_FREE(g.history);
      STBI_FREE(g.background);

      // do the final conversion after loading everything;
      if (req_comp && req_comp != 4)
         out = stbi__convert_format(out, 4, req_comp, layers * g.w, g.h);

      *z = layers;
      return out;
   } else {
      return stbi__errpuc("not GIF", "Image was not as a gif type.");
   }
}

static void *stbi__gif_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri)
{
   stbi_uc *u = 0;
   stbi__gif g;
   memset(&g, 0, sizeof(g));
   STBI_NOTUSED(ri);

   u = stbi__gif_load_next(s, &g, comp, req_comp, 0);
   if (u == (stbi_uc *) s) u = 0;  // end of animated gif marker
   if (u) {
      *x = g.w;
      *y = g.h;

      // moved conversion to after successful load so that the same
      // can be done for multiple frames.
      if (req_comp && req_comp != 4)
         u = stbi__convert_format(u, 4, req_comp, g.w, g.h);
   } else if (g.out) {
      // if there was an error and we allocated an image buffer, free it!
      STBI_FREE(g.out);
   }

   // free buffers needed for multiple frame loading;
   STBI_FREE(g.history);
   STBI_FREE(g.background);

   return u;
}

static int stbi__gif_info(stbi__context *s, int *x, int *y, int *comp)
{
   return stbi__gif_info_raw(s,x,y,comp);
}
#endif

// *************************************************************************************************
// Radiance RGBE HDR loader
// originally by Nicolas Schulz
#ifndef STBI_NO_HDR
static int stbi__hdr_test_core(stbi__context *s, const char *signature)
{
   int i;
   for (i=0; signature[i]; ++i)
      if (stbi__get8(s) != signature[i])
          return 0;
   stbi__rewind(s);
   return 1;
}

static int stbi__hdr_test(stbi__context* s)
{
   int r = stbi__hdr_test_core(s, "#?RADIANCE\n");
   stbi__rewind(s);
   if(!r) {
       r = stbi__hdr_test_core(s, "#?RGBE\n");
       stbi__rewind(s);
   }
   return r;
}

#define STBI__HDR_BUFLEN  1024
static char *stbi__hdr_gettoken(stbi__context *z, char *buffer)
{
   int len=0;
   char c = '\0';

   c = (char) stbi__get8(z);

   while (!stbi__at_eof(z) && c != '\n') {
      buffer[len++] = c;
      if (len == STBI__HDR_BUFLEN-1) {
         // flush to end of line
         while (!stbi__at_eof(z) && stbi__get8(z) != '\n')
            ;
         break;
      }
      c = (char) stbi__get8(z);
   }

   buffer[len] = 0;
   return buffer;
}

static void stbi__hdr_convert(float *output, stbi_uc *input, int req_comp)
{
   if ( input[3] != 0 ) {
      float f1;
      // Exponent
      f1 = (float) ldexp(1.0f, input[3] - (int)(128 + 8));
      if (req_comp <= 2)
         output[0] = (input[0] + input[1] + input[2]) * f1 / 3;
      else {
         output[0] = input[0] * f1;
         output[1] = input[1] * f1;
         output[2] = input[2] * f1;
      }
      if (req_comp == 2) output[1] = 1;
      if (req_comp == 4) output[3] = 1;
   } else {
      switch (req_comp) {
         case 4: output[3] = 1; /* fallthrough */
         case 3: output[0] = output[1] = output[2] = 0;
                 break;
         case 2: output[1] = 1; /* fallthrough */
         case 1: output[0] = 0;
                 break;
      }
   }
}

static float *stbi__hdr_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri)
{
   char buffer[STBI__HDR_BUFLEN];
   char *token;
   int valid = 0;
   int width, height;
   stbi_uc *scanline;
   float *hdr_data;
   int len;
   unsigned char count, value;
   int i, j, k, c1,c2, z;
   const char *headerToken;
   STBI_NOTUSED(ri);

   // Check identifier
   headerToken = stbi__hdr_gettoken(s,buffer);
   if (strcmp(headerToken, "#?RADIANCE") != 0 && strcmp(headerToken, "#?RGBE") != 0)
      return stbi__errpf("not HDR", "Corrupt HDR image");

   // Parse header
   for(;;) {
      token = stbi__hdr_gettoken(s,buffer);
      if (token[0] == 0) break;
      if (strcmp(token, "FORMAT=32-bit_rle_rgbe") == 0) valid = 1;
   }

   if (!valid)    return stbi__errpf("unsupported format", "Unsupported HDR format");

   // Parse width and height
   // can't use sscanf() if we're not using stdio!
   token = stbi__hdr_gettoken(s,buffer);
   if (strncmp(token, "-Y ", 3))  return stbi__errpf("unsupported data layout", "Unsupported HDR format");
   token += 3;
   height = (int) strtol(token, &token, 10);
   while (*token == ' ') ++token;
   if (strncmp(token, "+X ", 3))  return stbi__errpf("unsupported data layout", "Unsupported HDR format");
   token += 3;
   width = (int) strtol(token, NULL, 10);

   if (height > STBI_MAX_DIMENSIONS) return stbi__errpf("too large","Very large image (corrupt?)");
   if (width > STBI_MAX_DIMENSIONS) return stbi__errpf("too large","Very large image (corrupt?)");

   *x = width;
   *y = height;

   if (comp) *comp = 3;
   if (req_comp == 0) req_comp = 3;

   if (!stbi__mad4sizes_valid(width, height, req_comp, sizeof(float), 0))
      return stbi__errpf("too large", "HDR image is too large");

   // Read data
   hdr_data = (float *) stbi__malloc_mad4(width, height, req_comp, sizeof(float), 0);
   if (!hdr_data)
      return stbi__errpf("outofmem", "Out of memory");

   // Load image data
   // image data is stored as some number of sca
   if ( width < 8 || width >= 32768) {
      // Read flat data
      for (j=0; j < height; ++j) {
         for (i=0; i < width; ++i) {
            stbi_uc rgbe[4];
           main_decode_loop:
            stbi__getn(s, rgbe, 4);
            stbi__hdr_convert(hdr_data + j * width * req_comp + i * req_comp, rgbe, req_comp);
         }
      }
   } else {
      // Read RLE-encoded data
      scanline = NULL;

      for (j = 0; j < height; ++j) {
         c1 = stbi__get8(s);
         c2 = stbi__get8(s);
         len = stbi__get8(s);
         if (c1 != 2 || c2 != 2 || (len & 0x80)) {
            // not run-length encoded, so we have to actually use THIS data as a decoded
            // pixel (note this can't be a valid pixel--one of RGB must be >= 128)
            stbi_uc rgbe[4];
            rgbe[0] = (stbi_uc) c1;
            rgbe[1] = (stbi_uc) c2;
            rgbe[2] = (stbi_uc) len;
            rgbe[3] = (stbi_uc) stbi__get8(s);
            stbi__hdr_convert(hdr_data, rgbe, req_comp);
            i = 1;
            j = 0;
            STBI_FREE(scanline);
            goto main_decode_loop; // yes, this makes no sense
         }
         len <<= 8;
         len |= stbi__get8(s);
         if (len != width) { STBI_FREE(hdr_data); STBI_FREE(scanline); return stbi__errpf("invalid decoded scanline length", "corrupt HDR"); }
         if (scanline == NULL) {
            scanline = (stbi_uc *) stbi__malloc_mad2(width, 4, 0);
            if (!scanline) {
               STBI_FREE(hdr_data);
               return stbi__errpf("outofmem", "Out of memory");
            }
         }

         for (k = 0; k < 4; ++k) {
            int nleft;
            i = 0;
            while ((nleft = width - i) > 0) {
               count = stbi__get8(s);
               if (count > 128) {
                  // Run
                  value = stbi__get8(s);
                  count -= 128;
                  if ((count == 0) || (count > nleft)) { STBI_FREE(hdr_data); STBI_FREE(scanline); return stbi__errpf("corrupt", "bad RLE data in HDR"); }
                  for (z = 0; z < count; ++z)
                     scanline[i++ * 4 + k] = value;
               } else {
                  // Dump
                  if ((count == 0) || (count > nleft)) { STBI_FREE(hdr_data); STBI_FREE(scanline); return stbi__errpf("corrupt", "bad RLE data in HDR"); }
                  for (z = 0; z < count; ++z)
                     scanline[i++ * 4 + k] = stbi__get8(s);
               }
            }
         }
         for (i=0; i < width; ++i)
            stbi__hdr_convert(hdr_data+(j*width + i)*req_comp, scanline + i*4, req_comp);
      }
      if (scanline)
         STBI_FREE(scanline);
   }

   return hdr_data;
}

static int stbi__hdr_info(stbi__context *s, int *x, int *y, int *comp)
{
   char buffer[STBI__HDR_BUFLEN];
   char *token;
   int valid = 0;
   int dummy;

   if (!x) x = &dummy;
   if (!y) y = &dummy;
   if (!comp) comp = &dummy;

   if (stbi__hdr_test(s) == 0) {
       stbi__rewind( s );
       return 0;
   }

   for(;;) {
      token = stbi__hdr_gettoken(s,buffer);
      if (token[0] == 0) break;
      if (strcmp(token, "FORMAT=32-bit_rle_rgbe") == 0) valid = 1;
   }

   if (!valid) {
       stbi__rewind( s );
       return 0;
   }
   token = stbi__hdr_gettoken(s,buffer);
   if (strncmp(token, "-Y ", 3)) {
       stbi__rewind( s );
       return 0;
   }
   token += 3;
   *y = (int) strtol(token, &token, 10);
   while (*token == ' ') ++token;
   if (strncmp(token, "+X ", 3)) {
       stbi__rewind( s );
       return 0;
   }
   token += 3;
   *x = (int) strtol(token, NULL, 10);
   *comp = 3;
   return 1;
}
#endif // STBI_NO_HDR

#ifndef STBI_NO_BMP
static int stbi__bmp_info(stbi__context *s, int *x, int *y, int *comp)
{
   void *p;
   stbi__bmp_data info;

   info.all_a = 255;
   p = stbi__bmp_parse_header(s, &info);
   if (p == NULL) {
      stbi__rewind( s );
      return 0;
   }
   if (x) *x = s->img_x;
   if (y) *y = s->img_y;
   if (comp) {
      if (info.bpp == 24 && info.ma == 0xff000000)
         *comp = 3;
      else
         *comp = info.ma ? 4 : 3;
   }
   return 1;
}
#endif

#ifndef STBI_NO_PSD
static int stbi__psd_info(stbi__context *s, int *x, int *y, int *comp)
{
   int channelCount, dummy, depth;
   if (!x) x = &dummy;
   if (!y) y = &dummy;
   if (!comp) comp = &dummy;
   if (stbi__get32be(s) != 0x38425053) {
       stbi__rewind( s );
       return 0;
   }
   if (stbi__get16be(s) != 1) {
       stbi__rewind( s );
       return 0;
   }
   stbi__skip(s, 6);
   channelCount = stbi__get16be(s);
   if (channelCount < 0 || channelCount > 16) {
       stbi__rewind( s );
       return 0;
   }
   *y = stbi__get32be(s);
   *x = stbi__get32be(s);
   depth = stbi__get16be(s);
   if (depth != 8 && depth != 16) {
       stbi__rewind( s );
       return 0;
   }
   if (stbi__get16be(s) != 3) {
       stbi__rewind( s );
       return 0;
   }
   *comp = 4;
   return 1;
}

static int stbi__psd_is16(stbi__context *s)
{
   int channelCount, depth;
   if (stbi__get32be(s) != 0x38425053) {
       stbi__rewind( s );
       return 0;
   }
   if (stbi__get16be(s) != 1) {
       stbi__rewind( s );
       return 0;
   }
   stbi__skip(s, 6);
   channelCount = stbi__get16be(s);
   if (channelCount < 0 || channelCount > 16) {
       stbi__rewind( s );
       return 0;
   }
   STBI_NOTUSED(stbi__get32be(s));
   STBI_NOTUSED(stbi__get32be(s));
   depth = stbi__get16be(s);
   if (depth != 16) {
       stbi__rewind( s );
       return 0;
   }
   return 1;
}
#endif

#ifndef STBI_NO_PIC
static int stbi__pic_info(stbi__context *s, int *x, int *y, int *comp)
{
   int act_comp=0,num_packets=0,chained,dummy;
   stbi__pic_packet packets[10];

   if (!x) x = &dummy;
   if (!y) y = &dummy;
   if (!comp) comp = &dummy;

   if (!stbi__pic_is4(s,"\x53\x80\xF6\x34")) {
      stbi__rewind(s);
      return 0;
   }

   stbi__skip(s, 88);

   *x = stbi__get16be(s);
   *y = stbi__get16be(s);
   if (stbi__at_eof(s)) {
      stbi__rewind( s);
      return 0;
   }
   if ( (*x) != 0 && (1 << 28) / (*x) < (*y)) {
      stbi__rewind( s );
      return 0;
   }

   stbi__skip(s, 8);

   do {
      stbi__pic_packet *packet;

      if (num_packets==sizeof(packets)/sizeof(packets[0]))
         return 0;

      packet = &packets[num_packets++];
      chained = stbi__get8(s);
      packet->size    = stbi__get8(s);
      packet->type    = stbi__get8(s);
      packet->channel = stbi__get8(s);
      act_comp |= packet->channel;

      if (stbi__at_eof(s)) {
          stbi__rewind( s );
          return 0;
      }
      if (packet->size != 8) {
          stbi__rewind( s );
          return 0;
      }
   } while (chained);

   *comp = (act_comp & 0x10 ? 4 : 3);

   return 1;
}
#endif

// *************************************************************************************************
// Portable Gray Map and Portable Pixel Map loader
// by Ken Miller
//
// PGM: http://netpbm.sourceforge.net/doc/pgm.html
// PPM: http://netpbm.sourceforge.net/doc/ppm.html
//
// Known limitations:
//    Does not support comments in the header section
//    Does not support ASCII image data (formats P2 and P3)

#ifndef STBI_NO_PNM

static int      stbi__pnm_test(stbi__context *s)
{
   char p, t;
   p = (char) stbi__get8(s);
   t = (char) stbi__get8(s);
   if (p != 'P' || (t != '5' && t != '6')) {
       stbi__rewind( s );
       return 0;
   }
   return 1;
}

static void *stbi__pnm_load(stbi__context *s, int *x, int *y, int *comp, int req_comp, stbi__result_info *ri)
{
   stbi_uc *out;
   STBI_NOTUSED(ri);

   ri->bits_per_channel = stbi__pnm_info(s, (int *)&s->img_x, (int *)&s->img_y, (int *)&s->img_n);
   if (ri->bits_per_channel == 0)
      return 0;

   if (s->img_y > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");
   if (s->img_x > STBI_MAX_DIMENSIONS) return stbi__errpuc("too large","Very large image (corrupt?)");

   *x = s->img_x;
   *y = s->img_y;
   if (comp) *comp = s->img_n;

   if (!stbi__mad4sizes_valid(s->img_n, s->img_x, s->img_y, ri->bits_per_channel / 8, 0))
      return stbi__errpuc("too large", "PNM too large");

   out = (stbi_uc *) stbi__malloc_mad4(s->img_n, s->img_x, s->img_y, ri->bits_per_channel / 8, 0);
   if (!out) return stbi__errpuc("outofmem", "Out of memory");
   if (!stbi__getn(s, out, s->img_n * s->img_x * s->img_y * (ri->bits_per_channel / 8))) {
      STBI_FREE(out);
      return stbi__errpuc("bad PNM", "PNM file truncated");
   }

   if (req_comp && req_comp != s->img_n) {
      if (ri->bits_per_channel == 16) {
         out = (stbi_uc *) stbi__convert_format16((stbi__uint16 *) out, s->img_n, req_comp, s->img_x, s->img_y);
      } else {
         out = stbi__convert_format(out, s->img_n, req_comp, s->img_x, s->img_y);
      }
      if (out == NULL) return out; // stbi__convert_format frees input on failure
   }
   return out;
}

static int      stbi__pnm_isspace(char c)
{
   return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

static void     stbi__pnm_skip_whitespace(stbi__context *s, char *c)
{
   for (;;) {
      while (!stbi__at_eof(s) && stbi__pnm_isspace(*c))
         *c = (char) stbi__get8(s);

      if (stbi__at_eof(s) || *c != '#')
         break;

      while (!stbi__at_eof(s) && *c != '\n' && *c != '\r' )
         *c = (char) stbi__get8(s);
   }
}

static int      stbi__pnm_isdigit(char c)
{
   return c >= '0' && c <= '9';
}

static int      stbi__pnm_getinteger(stbi__context *s, char *c)
{
   int value = 0;

   while (!stbi__at_eof(s) && stbi__pnm_isdigit(*c)) {
      value = value*10 + (*c - '0');
      *c = (char) stbi__get8(s);
      if((value > 214748364) || (value == 214748364 && *c > '7'))
          return stbi__err("integer parse overflow", "Parsing an integer in the PPM header overflowed a 32-bit int");
   }

   return value;
}

static int      stbi__pnm_info(stbi__context *s, int *x, int *y, int *comp)
{
   int maxv, dummy;
   char c, p, t;

   if (!x) x = &dummy;
   if (!y) y = &dummy;
   if (!comp) comp = &dummy;

   stbi__rewind(s);

   // Get identifier
   p = (char) stbi__get8(s);
   t = (char) stbi__get8(s);
   if (p != 'P' || (t != '5' && t != '6')) {
       stbi__rewind(s);
       return 0;
   }

   *comp = (t == '6') ? 3 : 1;  // '5' is 1-component .pgm; '6' is 3-component .ppm

   c = (char) stbi__get8(s);
   stbi__pnm_skip_whitespace(s, &c);

   *x = stbi__pnm_getinteger(s, &c); // read width
   if(*x == 0)
       return stbi__err("invalid width", "PPM image header had zero or overflowing width");
   stbi__pnm_skip_whitespace(s, &c);

   *y = stbi__pnm_getinteger(s, &c); // read height
   if (*y == 0)
       return stbi__err("invalid width", "PPM image header had zero or overflowing width");
   stbi__pnm_skip_whitespace(s, &c);

   maxv = stbi__pnm_getinteger(s, &c);  // read max value
   if (maxv > 65535)
      return stbi__err("max value > 65535", "PPM image supports only 8-bit and 16-bit images");
   else if (maxv > 255)
      return 16;
   else
      return 8;
}

static int stbi__pnm_is16(stbi__context *s)
{
   if (stbi__pnm_info(s, NULL, NULL, NULL) == 16)
	   return 1;
   return 0;
}
#endif

static int stbi__info_main(stbi__context *s, int *x, int *y, int *comp)
{
   #ifndef STBI_NO_JPEG
   if (stbi__jpeg_info(s, x, y, comp)) return 1;
   #endif

   #ifndef STBI_NO_PNG
   if (stbi__png_info(s, x, y, comp))  return 1;
   #endif

   #ifndef STBI_NO_GIF
   if (stbi__gif_info(s, x, y, comp))  return 1;
   #endif

   #ifndef STBI_NO_BMP
   if (stbi__bmp_info(s, x, y, comp))  return 1;
   #endif

   #ifndef STBI_NO_PSD
   if (stbi__psd_info(s, x, y, comp))  return 1;
   #endif

   #ifndef STBI_NO_PIC
   if (stbi__pic_info(s, x, y, comp))  return 1;
   #endif

   #ifndef STBI_NO_PNM
   if (stbi__pnm_info(s, x, y, comp))  return 1;
   #endif

   #ifndef STBI_NO_HDR
   if (stbi__hdr_info(s, x, y, comp))  return 1;
   #endif

   // test tga last because it's a crappy test!
   #ifndef STBI_NO_TGA
   if (stbi__tga_info(s, x, y, comp))
       return 1;
   #endif
   return stbi__err("unknown image type", "Image not of any known type, or corrupt");
}

static int stbi__is_16_main(stbi__context *s)
{
   #ifndef STBI_NO_PNG
   if (stbi__png_is16(s))  return 1;
   #endif

   #ifndef STBI_NO_PSD
   if (stbi__psd_is16(s))  return 1;
   #endif

   #ifndef STBI_NO_PNM
   if (stbi__pnm_is16(s))  return 1;
   #endif
   return 0;
}

#ifndef STBI_NO_STDIO
STBIDEF int stbi_info(char const *filename, int *x, int *y, int *comp)
{
    FILE *f = stbi__fopen(filename, "rb");
    int result;
    if (!f) return stbi__err("can't fopen", "Unable to open file");
    result = stbi_info_from_file(f, x, y, comp);
    fclose(f);
    return result;
}

STBIDEF int stbi_info_from_file(FILE *f, int *x, int *y, int *comp)
{
   int r;
   stbi__context s;
   long pos = ftell(f);
   stbi__start_file(&s, f);
   r = stbi__info_main(&s,x,y,comp);
   fseek(f,pos,SEEK_SET);
   return r;
}

STBIDEF int stbi_is_16_bit(char const *filename)
{
    FILE *f = stbi__fopen(filename, "rb");
    int result;
    if (!f) return stbi__err("can't fopen", "Unable to open file");
    result = stbi_is_16_bit_from_file(f);
    fclose(f);
    return result;
}

STBIDEF int stbi_is_16_bit_from_file(FILE *f)
{
   int r;
   stbi__context s;
   long pos = ftell(f);
   stbi__start_file(&s, f);
   r = stbi__is_16_main(&s);
   fseek(f,pos,SEEK_SET);
   return r;
}
#endif // !STBI_NO_STDIO

STBIDEF int stbi_info_from_memory(stbi_uc const *buffer, int len, int *x, int *y, int *comp)
{
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__info_main(&s,x,y,comp);
}

STBIDEF int stbi_info_from_callbacks(stbi_io_callbacks const *c, void *user, int *x, int *y, int *comp)
{
   stbi__context s;
   stbi__start_callbacks(&s, (stbi_io_callbacks *) c, user);
   return stbi__info_main(&s,x,y,comp);
}

STBIDEF int stbi_is_16_bit_from_memory(stbi_uc const *buffer, int len)
{
   stbi__context s;
   stbi__start_mem(&s,buffer,len);
   return stbi__is_16_main(&s);
}

STBIDEF int stbi_is_16_bit_from_callbacks(stbi_io_callbacks const *c, void *user)
{
   stbi__context s;
   stbi__start_callbacks(&s, (stbi_io_callbacks *) c, user);
   return stbi__is_16_main(&s);
}

#endif // STB_IMAGE_IMPLEMENTATION

/*
   revision history:
      2.20  (2019-02-07) support utf8 filenames in Windows; fix warnings and platform ifdefs
      2.19  (2018-02-11) fix warning
      2.18  (2018-01-30) fix warnings
      2.17  (2018-01-29) change sbti__shiftsigned to avoid clang -O2 bug
                         1-bit BMP
                         *_is_16_bit api
                         avoid warnings
      2.16  (2017-07-23) all functions have 16-bit variants;
                         STBI_NO_STDIO works again;
                         compilation fixes;
                         fix rounding in unpremultiply;
                         optimize vertical flip;
                         disable raw_len validation;
                         documentation fixes
      2.15  (2017-03-18) fix png-1,2,4 bug; now all Imagenet JPGs decode;
                         warning fixes; disable run-time SSE detection on gcc;
                         uniform handling of optional "return" values;
                         thread-safe initialization of zlib tables
      2.14  (2017-03-03) remove deprecated STBI_JPEG_OLD; fixes for Imagenet JPGs
      2.13  (2016-11-29) add 16-bit API, only supported for PNG right now
      2.12  (2016-04-02) fix typo in 2.11 PSD fix that caused crashes
      2.11  (2016-04-02) allocate large structures on the stack
                         remove white matting for transparent PSD
                         fix reported channel count for PNG & BMP
                         re-enable SSE2 in non-gcc 64-bit
                         support RGB-formatted JPEG
                         read 16-bit PNGs (only as 8-bit)
      2.10  (2016-01-22) avoid warning introduced in 2.09 by STBI_REALLOC_SIZED
      2.09  (2016-01-16) allow comments in PNM files
                         16-bit-per-pixel TGA (not bit-per-component)
                         info() for TGA could break due to .hdr handling
                         info() for BMP to shares code instead of sloppy parse
                         can use STBI_REALLOC_SIZED if allocator doesn't support realloc
                         code cleanup
      2.08  (2015-09-13) fix to 2.07 cleanup, reading RGB PSD as RGBA
      2.07  (2015-09-13) fix compiler warnings
                         partial animated GIF support
                         limited 16-bpc PSD support
                         #ifdef unused functions
                         bug with < 92 byte PIC,PNM,HDR,TGA
      2.06  (2015-04-19) fix bug where PSD returns wrong '*comp' value
      2.05  (2015-04-19) fix bug in progressive JPEG handling, fix warning
      2.04  (2015-04-15) try to re-enable SIMD on MinGW 64-bit
      2.03  (2015-04-12) extra corruption checking (mmozeiko)
                         stbi_set_flip_vertically_on_load (nguillemot)
                         fix NEON support; fix mingw support
      2.02  (2015-01-19) fix incorrect assert, fix warning
      2.01  (2015-01-17) fix various warnings; suppress SIMD on gcc 32-bit without -msse2
      2.00b (2014-12-25) fix STBI_MALLOC in progressive JPEG
      2.00  (2014-12-25) optimize JPG, including x86 SSE2 & NEON SIMD (ryg)
                         progressive JPEG (stb)
                         PGM/PPM support (Ken Miller)
                         STBI_MALLOC,STBI_REALLOC,STBI_FREE
                         GIF bugfix -- seemingly never worked
                         STBI_NO_*, STBI_ONLY_*
      1.48  (2014-12-14) fix incorrectly-named assert()
      1.47  (2014-12-14) 1/2/4-bit PNG support, both direct and paletted (Omar Cornut & stb)
                         optimize PNG (ryg)
                         fix bug in interlaced PNG with user-specified channel count (stb)
      1.46  (2014-08-26)
              fix broken tRNS chunk (colorkey-style transparency) in non-paletted PNG
      1.45  (2014-08-16)
              fix MSVC-ARM internal compiler error by wrapping malloc
      1.44  (2014-08-07)
              various warning fixes from Ronny Chevalier
      1.43  (2014-07-15)
              fix MSVC-only compiler problem in code changed in 1.42
      1.42  (2014-07-09)
              don't define _CRT_SECURE_NO_WARNINGS (affects user code)
              fixes to stbi__cleanup_jpeg path
              added STBI_ASSERT to avoid requiring assert.h
      1.41  (2014-06-25)
              fix search&replace from 1.36 that messed up comments/error messages
      1.40  (2014-06-22)
              fix gcc struct-initialization warning
      1.39  (2014-06-15)
              fix to TGA optimization when req_comp != number of components in TGA;
              fix to GIF loading because BMP wasn't rewinding (whoops, no GIFs in my test suite)
              add support for BMP version 5 (more ignored fields)
      1.38  (2014-06-06)
              suppress MSVC warnings on integer casts truncating values
              fix accidental rename of 'skip' field of I/O
      1.37  (2014-06-04)
              remove duplicate typedef
      1.36  (2014-06-03)
              convert to header file single-file library
              if de-iphone isn't set, load iphone images color-swapped instead of returning NULL
      1.35  (2014-05-27)
              various warnings
              fix broken STBI_SIMD path
              fix bug where stbi_load_from_file no longer left file pointer in correct place
              fix broken non-easy path for 32-bit BMP (possibly never used)
              TGA optimization by Arseny Kapoulkine
      1.34  (unknown)
              use STBI_NOTUSED in stbi__resample_row_generic(), fix one more leak in tga failure case
      1.33  (2011-07-14)
              make stbi_is_hdr work in STBI_NO_HDR (as specified), minor compiler-friendly improvements
      1.32  (2011-07-13)
              support for "info" function for all supported filetypes (SpartanJ)
      1.31  (2011-06-20)
              a few more leak fixes, bug in PNG handling (SpartanJ)
      1.30  (2011-06-11)
              added ability to load files via callbacks to accomidate custom input streams (Ben Wenger)
              removed deprecated format-specific test/load functions
              removed support for installable file formats (stbi_loader) -- would have been broken for IO callbacks anyway
              error cases in bmp and tga give messages and don't leak (Raymond Barbiero, grisha)
              fix inefficiency in decoding 32-bit BMP (David Woo)
      1.29  (2010-08-16)
              various warning fixes from Aurelien Pocheville
      1.28  (2010-08-01)
              fix bug in GIF palette transparency (SpartanJ)
      1.27  (2010-08-01)
              cast-to-stbi_uc to fix warnings
      1.26  (2010-07-24)
              fix bug in file buffering for PNG reported by SpartanJ
      1.25  (2010-07-17)
              refix trans_data warning (Won Chun)
      1.24  (2010-07-12)
              perf improvements reading from files on platforms with lock-heavy fgetc()
              minor perf improvements for jpeg
              deprecated type-specific functions so we'll get feedback if they're needed
              attempt to fix trans_data warning (Won Chun)
      1.23    fixed bug in iPhone support
      1.22  (2010-07-10)
              removed image *writing* support
              stbi_info support from Jetro Lauha
              GIF support from Jean-Marc Lienher
              iPhone PNG-extensions from James Brown
              warning-fixes from Nicolas Schulz and Janez Zemva (i.stbi__err. Janez (U+017D)emva)
      1.21    fix use of 'stbi_uc' in header (reported by jon blow)
      1.20    added support for Softimage PIC, by Tom Seddon
      1.19    bug in interlaced PNG corruption check (found by ryg)
      1.18  (2008-08-02)
              fix a threading bug (local mutable static)
      1.17    support interlaced PNG
      1.16    major bugfix - stbi__convert_format converted one too many pixels
      1.15    initialize some fields for thread safety
      1.14    fix threadsafe conversion bug
              header-file-only version (#define STBI_HEADER_FILE_ONLY before including)
      1.13    threadsafe
      1.12    const qualifiers in the API
      1.11    Support installable IDCT, colorspace conversion routines
      1.10    Fixes for 64-bit (don't use "unsigned long")
              optimized upsampling by Fabian "ryg" Giesen
      1.09    Fix format-conversion for PSD code (bad global variables!)
      1.08    Thatcher Ulrich's PSD code integrated by Nicolas Schulz
      1.07    attempt to fix C++ warning/errors again
      1.06    attempt to fix C++ warning/errors again
      1.05    fix TGA loading to return correct *comp and use good luminance calc
      1.04    default float alpha is 1, not 255; use 'void *' for stbi_image_free
      1.03    bugfixes to STBI_NO_STDIO, STBI_NO_HDR
      1.02    support for (subset of) HDR files, float interface for preferred access to them
      1.01    fix bug: possible bug in handling right-side up bmps... not sure
              fix bug: the stbi__bmp_load() and stbi__tga_load() functions didn't work at all
      1.00    interface to zlib that skips zlib header
      0.99    correct handling of alpha in palette
      0.98    TGA loader by lonesock; dynamically add loaders (untested)
      0.97    jpeg errors on too large a file; also catch another malloc failure
      0.96    fix detection of invalid v value - particleman@mollyrocket forum
      0.95    during header scan, seek to markers in case of padding
      0.94    STBI_NO_STDIO to disable stdio usage; rename all #defines the same
      0.93    handle jpegtran output; verbose errors
      0.92    read 4,8,16,24,32-bit BMP files of several formats
      0.91    output 24-bit Windows 3.0 BMP files
      0.90    fix a few more warnings; bump version number to approach 1.0
      0.61    bugfixes due to Marc LeBlanc, Christopher Lloyd
      0.60    fix compiling as c++
      0.59    fix warnings: merge Dave Moore's -Wall fixes
      0.58    fix bug: zlib uncompressed mode len/nlen was wrong endian
      0.57    fix bug: jpg last huffman symbol before marker was >9 bits but less than 16 available
      0.56    fix bug: zlib uncompressed mode len vs. nlen
      0.55    fix bug: restart_interval not initialized to 0
      0.54    allow NULL for 'int *comp'
      0.53    fix bug in png 3->4; speedup png decoding
      0.52    png handles req_comp=3,4 directly; minor cleanup; jpeg comments
      0.51    obey req_comp requests, 1-component jpegs return as 1-component,
              on 'test' only check type, not whether we support this variant
      0.50  (2006-11-19)
              first released version
*/


/*
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
*/
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#ifdef TIMUI_UNDEF_STBI_NO_FAILURE_STRINGS
#undef STBI_NO_FAILURE_STRINGS
#undef TIMUI_UNDEF_STBI_NO_FAILURE_STRINGS
#endif
#ifdef TIMUI_UNDEF_STBI_NO_THREAD_LOCALS
#undef STBI_NO_THREAD_LOCALS
#undef TIMUI_UNDEF_STBI_NO_THREAD_LOCALS
#endif
#ifdef TIMUI_UNDEF_STBI_NO_HDR
#undef STBI_NO_HDR
#undef TIMUI_UNDEF_STBI_NO_HDR
#endif
#ifdef TIMUI_UNDEF_STBI_NO_LINEAR
#undef STBI_NO_LINEAR
#undef TIMUI_UNDEF_STBI_NO_LINEAR
#endif
#ifdef TIMUI_UNDEF_STBI_NO_STDIO
#undef STBI_NO_STDIO
#undef TIMUI_UNDEF_STBI_NO_STDIO
#endif
#ifdef TIMUI_UNDEF_STBI_ONLY_PNG
#undef STBI_ONLY_PNG
#undef TIMUI_UNDEF_STBI_ONLY_PNG
#endif
#ifdef TIMUI_UNDEF_STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_IMPLEMENTATION
#undef TIMUI_UNDEF_STB_IMAGE_IMPLEMENTATION
#endif
#ifdef TIMUI_UNDEF_STB_IMAGE_STATIC
#undef STB_IMAGE_STATIC
#undef TIMUI_UNDEF_STB_IMAGE_STATIC
#endif
#ifdef TIMUI_UNDEF_STBI_MAX_DIMENSIONS
#undef STBI_MAX_DIMENSIONS
#undef TIMUI_UNDEF_STBI_MAX_DIMENSIONS
#endif
#endif

/* Write all len bytes, looping past short writes. A real fd transport may
 * deliver fewer bytes than requested; without this a graphics chunk can split
 * across the header/payload/ST boundary and corrupt the image (G5 residual). */
static void image_write_all_(TimuiTransport *t, const void *data, size_t len){
    const unsigned char *p = (const unsigned char *)data;
    size_t off = 0;
    if(!t || !t->write) return;
    while(off < len){
        size_t chunk = len - off;
        int w;
        if(chunk > 4096) chunk = 4096;
        w = t->write(t, p + off, chunk);
        if(w <= 0) break;                /* error / would-block: best-effort, stop */
        off += ((size_t)w > chunk) ? chunk : (size_t)w;
    }
}

static int image_fmt_size_(char *buf, size_t v){
    char tmp[32];
    int n = 0, i;
    do {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while(v > 0);
    for(i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    return n;
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
    img->rgba = NULL;
    img->rgba_len = 0;
    img->kind = TIMUI_IMAGE_KIND_PNG;
    img->stride = 0;
    img->id = 0;                 /* assigned on first transmit (timui_images_flush_) */
    /* pixel size from the PNG IHDR (width @16, height @20, big-endian) so a
     * placement can be cropped to a cell sub-rect (smooth scroll clipping). */
    img->px_w = img->px_h = 0;
    if(size >= 24){
        const unsigned char *d = (const unsigned char *)data;
        uint32_t w = ((uint32_t)d[16] << 24) | ((uint32_t)d[17] << 16) | ((uint32_t)d[18] << 8) | d[19];
        uint32_t h = ((uint32_t)d[20] << 24) | ((uint32_t)d[21] << 16) | ((uint32_t)d[22] << 8) | d[23];
        img->px_w = (w <= (uint32_t)INT_MAX) ? (int)w : 0;
        img->px_h = (h <= (uint32_t)INT_MAX) ? (int)h : 0;
    }
    return img;
}

static int image_rgba_size_(int w, int h, int stride, size_t *out_row, size_t *out_total){
    size_t row;
    if(w <= 0 || h <= 0) return 0;
    if(w > INT_MAX / 4) return 0;
    row = (size_t)w * 4u;
    if(stride < (int)row) return 0;
    if((size_t)h > SIZE_MAX / row) return 0;
    if(out_row) *out_row = row;
    if(out_total) *out_total = row * (size_t)h;
    return 1;
}

static void image_copy_rows_(unsigned char *dst, const unsigned char *src,
                             int h, size_t row, int stride){
    int y;
    for(y = 0; y < h; y++)
        memcpy(dst + (size_t)y * row, src + (size_t)y * (size_t)stride, row);
}

#ifndef TIMUI_NO_IMAGES
static int image_png_preflight_(const TimuiImage *img, int *out_w, int *out_h){
    const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    const unsigned char *d;
    uint32_t w, h;
    uint64_t pixels;
    if(!img || !img->data || img->len < 24 || img->len > (size_t)INT_MAX) return 0;
    d = img->data;
    if(memcmp(d, sig, sizeof sig) != 0) return 0;
    w = ((uint32_t)d[16] << 24) | ((uint32_t)d[17] << 16) | ((uint32_t)d[18] << 8) | d[19];
    h = ((uint32_t)d[20] << 24) | ((uint32_t)d[21] << 16) | ((uint32_t)d[22] << 8) | d[23];
    if(w == 0 || h == 0) return 0;
    if(w > TIMUI_IMAGE_PNG_MAX_DIMENSION || h > TIMUI_IMAGE_PNG_MAX_DIMENSION) return 0;
    pixels = (uint64_t)w * (uint64_t)h;
    if(pixels > (uint64_t)TIMUI_IMAGE_PNG_MAX_PIXELS) return 0;
    if(w > (uint32_t)(INT_MAX / 4)) return 0;
    if(out_w) *out_w = (int)w;
    if(out_h) *out_h = (int)h;
    return 1;
}

static int image_decode_png_rgba_(TimuiImage *img){
    int want_w = 0, want_h = 0, w = 0, h = 0, comp = 0;
    size_t row = 0, total = 0;
    unsigned char *rgba;
    if(!img) return 0;
    if(img->rgba) return 1;
    if(img->kind != TIMUI_IMAGE_KIND_PNG) return 0;
    if(!image_png_preflight_(img, &want_w, &want_h)) return 0;
    rgba = stbi_load_from_memory(img->data, (int)img->len, &w, &h, &comp, 4);
    (void)comp;
    if(!rgba) return 0;
    if(w != want_w || h != want_h || !image_rgba_size_(w, h, w * 4, &row, &total)){
        stbi_image_free(rgba);
        return 0;
    }
    img->rgba = rgba;
    img->rgba_len = total;
    img->px_w = w;
    img->px_h = h;
    img->stride = (int)row;
    return 1;
}
#endif

TIMUI_API TimuiImage *timui_image_from_rgba(Timui *ui, const void *rgba, int w, int h, int stride){
    TimuiImage *img;
    TimuiAllocator al;
    size_t row = 0, total = 0;
    const unsigned char *src;
    (void)ui;
    if(!rgba || !image_rgba_size_(w, h, stride, &row, &total)) return NULL;
    al = timui_default_allocator();
    img = (TimuiImage *)al.alloc(al.userdata, sizeof(TimuiImage));
    if(!img) return NULL;
    img->data = (unsigned char *)al.alloc(al.userdata, total);
    if(!img->data){ al.free(al.userdata, img, sizeof *img); return NULL; }
    src = (const unsigned char *)rgba;
    image_copy_rows_(img->data, src, h, row, stride);
    img->len = total;
    img->rgba = img->data;
    img->rgba_len = total;
    img->id = 0;
    img->px_w = w;
    img->px_h = h;
    img->kind = TIMUI_IMAGE_KIND_RGBA;
    img->stride = (int)row;
    return img;
}
TIMUI_API TimuiImage *timui_image_from_png_rgba(Timui *ui, const void *png,
                                                size_t png_size,
                                                const void *rgba,
                                                int w, int h, int stride){
    TimuiImage *img;
    TimuiAllocator al;
    size_t row = 0, total = 0;
    const unsigned char *src;
    (void)ui;
    if(!png || png_size == 0 || !rgba || !image_rgba_size_(w, h, stride, &row, &total))
        return NULL;
    al = timui_default_allocator();
    img = (TimuiImage *)al.alloc(al.userdata, sizeof(TimuiImage));
    if(!img) return NULL;
    memset(img, 0, sizeof *img);
    img->data = (unsigned char *)al.alloc(al.userdata, png_size);
    if(!img->data){ al.free(al.userdata, img, sizeof *img); return NULL; }
    img->rgba = (unsigned char *)al.alloc(al.userdata, total);
    if(!img->rgba){
        al.free(al.userdata, img->data, png_size);
        al.free(al.userdata, img, sizeof *img);
        return NULL;
    }
    memcpy(img->data, png, png_size);
    src = (const unsigned char *)rgba;
    image_copy_rows_(img->rgba, src, h, row, stride);
    img->len = png_size;
    img->rgba_len = total;
    img->id = 0;
    img->px_w = w;
    img->px_h = h;
    img->kind = TIMUI_IMAGE_KIND_PNG_RGBA;
    img->stride = (int)row;
    return img;
}
TIMUI_API void timui_image_free(Timui *ui, TimuiImage *img){
    TimuiAllocator al;
    (void)ui;
    if(!img) return;
    al = timui_default_allocator();
    if(img->rgba && img->rgba != img->data){
#ifndef TIMUI_NO_IMAGES
        if(img->kind == TIMUI_IMAGE_KIND_PNG)
            stbi_image_free(img->rgba);
        else
#endif
            al.free(al.userdata, img->rgba, img->rgba_len);
    }
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
            image_write_all_(t, hdr, (size_t)hn);
            image_write_all_(t, buf + sent, chunk);
            image_write_all_(t, "\x1b\\", 2);
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
    image_write_all_(t, b, (size_t)n);
}
/* delete every visible placement (keeps image data: lowercase d=a). */
static void kitty_delete_all_placements(TimuiTransport *t){
    image_write_all_(t, "\x1b_Ga=d,d=a\x1b\\", 12);
}
static int image_cup_(TimuiTransport *t, int x, int y){
    char cup[32];
    int cn = 0;
    if(x < 0 || y < 0 || x == INT_MAX || y == INT_MAX) return 0;
    cup[cn++] = 0x1b; cup[cn++] = '[';
    cn += fmt_uint(cup + cn, (unsigned)(y + 1)); cup[cn++] = ';';
    cn += fmt_uint(cup + cn, (unsigned)(x + 1)); cup[cn++] = 'H';
    image_write_all_(t, cup, (size_t)cn);
    return 1;
}

static int iterm2_emit_(TimuiTransport *t, const TimuiImage *img, TimuiRect r){
    size_t b64cap, b64len;
    TimuiAllocator al = timui_default_allocator();
    char *buf;
    char hdr[160];
    int hn = 0;
    const char *p;
    if(!t || !t->write || !img || !img->data || img->len == 0 || r.w <= 0 || r.h <= 0) return 0;
    if(r.x < 0 || r.y < 0) return 0;
    if(img->len > (SIZE_MAX - 1) / 4) return 0;
    b64cap = ((img->len + 2) / 3) * 4 + 1;
    buf = (char *)al.alloc(al.userdata, b64cap);
    if(!buf) return 0;
    b64len = b64_encode(img->data, img->len, buf, b64cap - 1);
    if(b64len == 0 || b64len == (size_t)-1){
        al.free(al.userdata, buf, b64cap);
        return 0;
    }
    if(!image_cup_(t, r.x, r.y)){
        al.free(al.userdata, buf, b64cap);
        return 0;
    }
    hdr[hn++] = 0x1b; hdr[hn++] = ']';
    p = "1337;File=inline=1;size="; while(*p) hdr[hn++] = *p++;
    hn += image_fmt_size_(hdr + hn, img->len);
    p = ";width="; while(*p) hdr[hn++] = *p++;
    hn += fmt_uint(hdr + hn, (unsigned)r.w);
    p = ";height="; while(*p) hdr[hn++] = *p++;
    hn += fmt_uint(hdr + hn, (unsigned)r.h);
    p = ";preserveAspectRatio=0:"; while(*p) hdr[hn++] = *p++;
    image_write_all_(t, hdr, (size_t)hn);
    image_write_all_(t, buf, b64len);
    image_write_all_(t, "\x1b\\", 2);
    al.free(al.userdata, buf, b64cap);
    return 1;
}

#define SIXEL_MAX_COLORS 16

typedef struct {
    unsigned char r, g, b;
} SixelColor_;

typedef struct {
    int sx, sy, sw, sh;
} SixelCrop_;

typedef struct {
    SixelColor_ colors[SIXEL_MAX_COLORS];
    int count;
    int quantized;
} SixelPalette_;

static int image_rect_emit_valid_(TimuiRect r);
static int image_rect_contains_(TimuiRect outer, TimuiRect inner);

static int image_has_png_(const TimuiImage *img){
    return img && img->data && img->len > 0 &&
           (img->kind == TIMUI_IMAGE_KIND_PNG || img->kind == TIMUI_IMAGE_KIND_PNG_RGBA);
}

static const unsigned char *image_rgba_(const TimuiImage *img){
    if(!img) return NULL;
    return img->rgba ? img->rgba :
           ((img->kind == TIMUI_IMAGE_KIND_RGBA) ? img->data : NULL);
}

static size_t image_rgba_len_(const TimuiImage *img){
    if(!img) return 0;
    if(img->rgba) return img->rgba_len;
    return (img->kind == TIMUI_IMAGE_KIND_RGBA) ? img->len : 0;
}

static int sixel_is_rgba_(const TimuiImage *img){
    size_t row, stride, need;
    if(!img || !image_rgba_(img)) return 0;
    if(img->px_w <= 0 || img->px_h <= 0 || img->px_w > INT_MAX / 4) return 0;
    if(img->stride <= 0) return 0;
    row = (size_t)img->px_w * 4u;
    stride = (size_t)img->stride;
    if(stride < row) return 0;
    if((size_t)(img->px_h - 1) > (SIZE_MAX - row) / stride) return 0;
    need = (size_t)(img->px_h - 1) * stride + row;
    return image_rgba_len_(img) >= need;
}

static int sixel_palette_index_(const SixelColor_ *pal, int count,
                                unsigned char r, unsigned char g, unsigned char b){
    int i;
    for(i = 0; i < count; i++)
        if(pal[i].r == r && pal[i].g == g && pal[i].b == b) return i;
    return -1;
}

static const SixelColor_ sixel_quant16_[SIXEL_MAX_COLORS] = {
    {   0,   0,   0 }, { 128,   0,   0 }, {   0, 128,   0 }, { 128, 128,   0 },
    {   0,   0, 128 }, { 128,   0, 128 }, {   0, 128, 128 }, { 192, 192, 192 },
    { 128, 128, 128 }, { 255,   0,   0 }, {   0, 255,   0 }, { 255, 255,   0 },
    {   0,   0, 255 }, { 255,   0, 255 }, {   0, 255, 255 }, { 255, 255, 255 }
};

static unsigned sixel_color_dist_(SixelColor_ c,
                                  unsigned char r, unsigned char g, unsigned char b){
    int dr = (int)c.r - (int)r;
    int dg = (int)c.g - (int)g;
    int db = (int)c.b - (int)b;
    return (unsigned)(dr * dr + dg * dg + db * db);
}

static SixelColor_ sixel_quantize_color_(unsigned char r, unsigned char g, unsigned char b){
    int i, best = 0;
    unsigned best_dist = sixel_color_dist_(sixel_quant16_[0], r, g, b);
    for(i = 1; i < SIXEL_MAX_COLORS; i++){
        unsigned dist = sixel_color_dist_(sixel_quant16_[i], r, g, b);
        if(dist < best_dist){
            best = i;
            best_dist = dist;
        }
    }
    return sixel_quant16_[best];
}

static int sixel_palette_add_(SixelPalette_ *pal, SixelColor_ c){
    if(!pal) return 0;
    if(sixel_palette_index_(pal->colors, pal->count, c.r, c.g, c.b) >= 0) return 1;
    if(pal->count >= SIXEL_MAX_COLORS) return 0;
    pal->colors[pal->count++] = c;
    return 1;
}

static int sixel_crop_(const TimuiImage *img, TimuiRect full, TimuiRect visible, SixelCrop_ *out){
    int64_t fx0, fy0, fx1, fy1;
    int64_t sx0, sy0, sx1, sy1;
    if(!sixel_is_rgba_(img) || !out || !image_rect_contains_(full, visible))
        return 0;
    fx0 = (int64_t)visible.x - (int64_t)full.x;
    fy0 = (int64_t)visible.y - (int64_t)full.y;
    fx1 = fx0 + (int64_t)visible.w;
    fy1 = fy0 + (int64_t)visible.h;
    sx0 = fx0 * (int64_t)img->px_w / (int64_t)full.w;
    sy0 = fy0 * (int64_t)img->px_h / (int64_t)full.h;
    sx1 = (fx1 * (int64_t)img->px_w + (int64_t)full.w - 1) / (int64_t)full.w;
    sy1 = (fy1 * (int64_t)img->px_h + (int64_t)full.h - 1) / (int64_t)full.h;
    if(sx0 < 0) sx0 = 0;
    if(sy0 < 0) sy0 = 0;
    if(sx1 > img->px_w) sx1 = img->px_w;
    if(sy1 > img->px_h) sy1 = img->px_h;
    if(sx1 <= sx0 || sy1 <= sy0) return 0;
    out->sx = (int)sx0;
    out->sy = (int)sy0;
    out->sw = (int)(sx1 - sx0);
    out->sh = (int)(sy1 - sy0);
    return 1;
}

static int sixel_palette_crop_(const TimuiImage *img, SixelCrop_ crop, SixelPalette_ *pal){
    int x, y;
    if(!sixel_is_rgba_(img) || !pal) return 0;
    if(crop.sx < 0 || crop.sy < 0 || crop.sw <= 0 || crop.sh <= 0) return 0;
    if(crop.sx > img->px_w || crop.sy > img->px_h) return 0;
    if(crop.sw > img->px_w - crop.sx || crop.sh > img->px_h - crop.sy) return 0;
    memset(pal, 0, sizeof *pal);
    for(y = crop.sy; y < crop.sy + crop.sh; y++){
        const unsigned char *row = image_rgba_(img) + (size_t)y * (size_t)img->stride;
        for(x = crop.sx; x < crop.sx + crop.sw; x++){
            const unsigned char *px = row + (size_t)x * 4u;
            if(px[3] < 128) continue;
            if(sixel_palette_index_(pal->colors, pal->count, px[0], px[1], px[2]) >= 0)
                continue;
            if(pal->count >= SIXEL_MAX_COLORS) goto quantize;
            pal->colors[pal->count].r = px[0];
            pal->colors[pal->count].g = px[1];
            pal->colors[pal->count].b = px[2];
            pal->count++;
        }
    }
    return pal->count > 0;

quantize:
    memset(pal, 0, sizeof *pal);
    pal->quantized = 1;
    for(y = crop.sy; y < crop.sy + crop.sh; y++){
        const unsigned char *row = image_rgba_(img) + (size_t)y * (size_t)img->stride;
        for(x = crop.sx; x < crop.sx + crop.sw; x++){
            const unsigned char *px = row + (size_t)x * 4u;
            SixelColor_ q;
            if(px[3] < 128) continue;
            q = sixel_quantize_color_(px[0], px[1], px[2]);
            if(!sixel_palette_add_(pal, q)) return 0;
        }
    }
    return pal->count > 0;
}

static int sixel_palette_(const TimuiImage *img, SixelPalette_ *pal){
    SixelCrop_ crop;
    if(!sixel_is_rgba_(img)) return 0;
    crop.sx = 0;
    crop.sy = 0;
    crop.sw = img->px_w;
    crop.sh = img->px_h;
    return sixel_palette_crop_(img, crop, pal);
}

static int sixel_image_supported_(const TimuiImage *img){
    SixelPalette_ pal;
    return sixel_palette_(img, &pal);
}

static int sixel_image_supported_at_(const TimuiImage *img, TimuiRect full, TimuiRect visible){
    SixelPalette_ pal;
    SixelCrop_ crop;
    return sixel_crop_(img, full, visible, &crop) &&
           sixel_palette_crop_(img, crop, &pal);
}

static unsigned sixel_pct_(unsigned char v){
    return (unsigned)(((unsigned)v * 100u + 127u) / 255u);
}

static void sixel_emit_color_def_(TimuiTransport *t, int idx, SixelColor_ c){
    char b[64];
    int n = 0;
    const char *p;
    b[n++] = '#'; n += fmt_uint(b + n, (unsigned)(idx + 1));
    p = ";2;"; while(*p) b[n++] = *p++;
    n += fmt_uint(b + n, sixel_pct_(c.r)); b[n++] = ';';
    n += fmt_uint(b + n, sixel_pct_(c.g)); b[n++] = ';';
    n += fmt_uint(b + n, sixel_pct_(c.b));
    image_write_all_(t, b, (size_t)n);
}

static int sixel_palette_pixel_matches_(const SixelPalette_ *pal, int idx,
                                        const unsigned char *px){
    SixelColor_ c;
    if(!pal || !px || idx < 0 || idx >= pal->count || px[3] < 128) return 0;
    c = pal->quantized ? sixel_quantize_color_(px[0], px[1], px[2])
                       : (SixelColor_){ px[0], px[1], px[2] };
    return c.r == pal->colors[idx].r && c.g == pal->colors[idx].g &&
           c.b == pal->colors[idx].b;
}

static void sixel_target_size_(SixelCrop_ crop, TimuiRect r, int cell_px_w, int cell_px_h,
                               int *out_w, int *out_h){
    int w = crop.sw, h = crop.sh;
    if(cell_px_w > 0 && cell_px_h > 0 &&
       r.w > 0 && r.h > 0 &&
       r.w <= INT_MAX / cell_px_w && r.h <= INT_MAX / cell_px_h){
        w = r.w * cell_px_w;
        h = r.h * cell_px_h;
    }
    if(out_w) *out_w = w;
    if(out_h) *out_h = h;
}

static const unsigned char *sixel_sample_pixel_(const TimuiImage *img, SixelCrop_ crop,
                                                int x, int y, int out_w, int out_h){
    int sx, sy;
    if(!img || out_w <= 0 || out_h <= 0) return NULL;
    sx = crop.sx + (int)(((int64_t)x * (int64_t)crop.sw) / (int64_t)out_w);
    sy = crop.sy + (int)(((int64_t)y * (int64_t)crop.sh) / (int64_t)out_h);
    if(sx < crop.sx) sx = crop.sx;
    if(sy < crop.sy) sy = crop.sy;
    if(sx >= crop.sx + crop.sw) sx = crop.sx + crop.sw - 1;
    if(sy >= crop.sy + crop.sh) sy = crop.sy + crop.sh - 1;
    return image_rgba_(img) + (size_t)sy * (size_t)img->stride + (size_t)sx * 4u;
}

static int sixel_emit_(TimuiTransport *t, const TimuiImage *img, TimuiRect r, TimuiRect full,
                       int cell_px_w, int cell_px_h){
    SixelPalette_ pal;
    SixelCrop_ crop;
    int ci, x, band;
    int out_w, out_h;
    char b[64];
    int n;
    const char *p;
    if(!t || !t->write || !image_rect_emit_valid_(r)) return 0;
    if(!sixel_crop_(img, full, r, &crop)) return 0;
    if(!sixel_palette_crop_(img, crop, &pal)) return 0;
    sixel_target_size_(crop, r, cell_px_w, cell_px_h, &out_w, &out_h);
    if(out_w <= 0 || out_h <= 0) return 0;
    if(!image_cup_(t, r.x, r.y)) return 0;
    image_write_all_(t, "\x1bP0;1;0q", sizeof("\x1bP0;1;0q") - 1);
    n = 0;
    b[n++] = '"'; b[n++] = '1'; b[n++] = ';'; b[n++] = '1'; b[n++] = ';';
    n += fmt_uint(b + n, (unsigned)out_w); b[n++] = ';';
    n += fmt_uint(b + n, (unsigned)out_h);
    image_write_all_(t, b, (size_t)n);
    for(ci = 0; ci < pal.count; ci++) sixel_emit_color_def_(t, ci, pal.colors[ci]);
    for(band = 0; band < out_h; band += 6){
        for(ci = 0; ci < pal.count; ci++){
            n = 0;
            b[n++] = '#';
            n += fmt_uint(b + n, (unsigned)(ci + 1));
            image_write_all_(t, b, (size_t)n);
            for(x = 0; x < out_w; x++){
                int bit;
                unsigned bits = 0;
                for(bit = 0; bit < 6; bit++){
                    int y = band + bit;
                    const unsigned char *px;
                    if(y >= out_h) continue;
                    px = sixel_sample_pixel_(img, crop, x, y, out_w, out_h);
                    if(sixel_palette_pixel_matches_(&pal, ci, px))
                        bits |= (1u << bit);
                }
                b[0] = (char)(0x3f + bits);
                image_write_all_(t, b, 1);
            }
            p = (ci + 1 < pal.count) ? "$" : ((band + 6 < out_h) ? "-" : "");
            if(*p) image_write_all_(t, p, 1);
        }
    }
    image_write_all_(t, "\x1b\\", 2);
    return 1;
}

/* Transmit/place or emit every image recorded this frame, on top of the cell
 * diff. Kitty gets explicit placement lifecycle management; iTerm2 is a direct
 * inline image write with no placement ids or delete escape. */
void timui_images_flush_(Timui *ui){
    int i;
    int emitted = 0;
    TimuiImageProtocol protocol;
    if(!ui) return;
    protocol = timui_image_protocol(ui);
    if(ui->img_last_count > 0 && ui->img_last_protocol == TIMUI_IMAGE_PROTOCOL_KITTY)
        kitty_delete_all_placements(&ui->transport);
    if(protocol == TIMUI_IMAGE_PROTOCOL_KITTY){
        for(i = 0; i < ui->img_place_count; i++){
            TimuiImage *img = ui->img_place[i].img;
            TimuiRect r    = ui->img_place[i].rect;   /* visible sub-rect */
            TimuiRect full = ui->img_place[i].full;   /* uncropped rect   */
            int sx = 0, sy = 0, sw = 0, sh = 0;
            if(!image_has_png_(img)) continue;
            if(r.x < 0 || r.y < 0 || r.x == INT_MAX || r.y == INT_MAX) continue;
            /* If the visible rect is a vertical sub-slice of `full`, crop the source
             * pixels to match, so the image clips smoothly at a pane edge. */
            if(img->px_w > 0 && img->px_h > 0 && full.h > 0 && (r.y != full.y || r.h != full.h)){
                int64_t sy64 = ((int64_t)r.y - (int64_t)full.y) * (int64_t)img->px_h / (int64_t)full.h;
                int64_t sh64 = (int64_t)r.h * (int64_t)img->px_h / (int64_t)full.h;
                sx = 0;
                sw = img->px_w;
                if(sy64 < 0) sy64 = 0;
                if(sy64 > img->px_h) sy64 = img->px_h;
                if(sh64 < 1) sh64 = 1;
                if(sy64 + sh64 > img->px_h) sh64 = (int64_t)img->px_h - sy64;
                if(sh64 < 1) sh64 = 1;
                sy = (int)sy64;
                sh = (int)sh64;
            }
            if(img->id == 0){                                   /* transmit once, keyed by id */
                img->id = ++ui->next_image_id;
                kitty_transmit_(&ui->transport, img->id, img->data, img->len);
            }
            if(image_cup_(&ui->transport, r.x, r.y)){
                kitty_place_(&ui->transport, img->id, r.w, r.h, i + 1, sx, sy, sw, sh);
                emitted++;
            }
        }
    } else if(protocol == TIMUI_IMAGE_PROTOCOL_ITERM2){
        for(i = 0; i < ui->img_place_count; i++)
            emitted += iterm2_emit_(&ui->transport, ui->img_place[i].img, ui->img_place[i].rect);
    } else if(protocol == TIMUI_IMAGE_PROTOCOL_SIXEL){
        for(i = 0; i < ui->img_place_count; i++)
            emitted += sixel_emit_(&ui->transport, ui->img_place[i].img,
                                   ui->img_place[i].rect, ui->img_place[i].full,
                                   ui->cell_px_w, ui->cell_px_h);
    }
    ui->img_last_count = emitted;
    ui->img_last_protocol = emitted ? protocol : TIMUI_IMAGE_PROTOCOL_NONE;
}

static int image_rect_same_(TimuiRect a, TimuiRect b){
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static int image_rect_emit_valid_(TimuiRect r){
    return r.w > 0 && r.h > 0 && r.x >= 0 && r.y >= 0 && r.x != INT_MAX && r.y != INT_MAX;
}

static int image_rect_contains_(TimuiRect outer, TimuiRect inner){
    int64_t ox = outer.x, oy = outer.y, ow = outer.w, oh = outer.h;
    int64_t ix = inner.x, iy = inner.y, iw = inner.w, ih = inner.h;
    if(ow <= 0 || oh <= 0 || iw <= 0 || ih <= 0) return 0;
    return ix >= ox && iy >= oy && ix + iw <= ox + ow && iy + ih <= oy + oh;
}

static void image_placeholder_(Timui *ui, TimuiRect visible){
    if(!ui || visible.w <= 0 || visible.h <= 0) return;
    timui_draw_fill(&ui->curr, visible,
                    timui_widget_style_(ui, TIMUI_WIDGET_PANEL, TIMUI_SLOT_INPUT, 0));
    timui_draw_text(&ui->curr, visible.x, visible.y, TIMUI_STR_LIT("[img]"),
                    timui_widget_style_(ui, TIMUI_WIDGET_PANEL, TIMUI_SLOT_TEXT_DIM, 0));
}

/* Record an image placement (transmit + place happen on top of the cell diff in
 * timui_end, so the renderer can't clobber it). `visible` is where it's drawn;
 * `full` is the uncropped rect (== visible when not clipping). The caller
 * reserves the region (draws its own background, no text). */
static void image_record_(Timui *ui, TimuiImage *img, TimuiRect visible, TimuiRect full){
    TimuiImageProtocol protocol;
    if(!ui || visible.w <= 0 || visible.h <= 0) return;
    if(!image_rect_contains_(full, visible)){
        image_placeholder_(ui, visible);
        return;
    }
    protocol = timui_image_protocol(ui);
#ifndef TIMUI_NO_IMAGES
    if(protocol == TIMUI_IMAGE_PROTOCOL_SIXEL && img && img->kind == TIMUI_IMAGE_KIND_PNG)
        (void)image_decode_png_rgba_(img);
#endif
    if((protocol == TIMUI_IMAGE_PROTOCOL_KITTY && image_has_png_(img)) ||
       (protocol == TIMUI_IMAGE_PROTOCOL_ITERM2 && image_has_png_(img) && image_rect_same_(visible, full)) ||
       (protocol == TIMUI_IMAGE_PROTOCOL_SIXEL &&
        ((image_rect_same_(visible, full) && sixel_image_supported_(img)) ||
         (!image_rect_same_(visible, full) && sixel_image_supported_at_(img, full, visible))))){
        if(!image_rect_emit_valid_(visible)){
            image_placeholder_(ui, visible);
            return;
        }
        if(ui->img_place_count < (int)(sizeof(ui->img_place) / sizeof(ui->img_place[0]))){
            ui->img_place[ui->img_place_count].img  = img;
            ui->img_place[ui->img_place_count].rect = visible;
            ui->img_place[ui->img_place_count].full = full;
            ui->img_place_count++;
        }
    } else {
        image_placeholder_(ui, visible);
    }
}
TIMUI_API void timui_image_draw(TimuiFrame *f, TimuiImage *img, TimuiRect r){
    Timui *ui;
    TimuiRect active, visible;
    if(!f || !f->ui || !img || r.w <= 0 || r.h <= 0) return;
    ui = f->ui;
    active = ui->curr.has_clip ? ui->curr.clip : TIMUI_RECT(0, 0, ui->curr.w, ui->curr.h);
    visible = timui_intersect_rect_(r, active);
    if(visible.w <= 0 || visible.h <= 0) return;
    image_record_(ui, img, visible, r);
}
TIMUI_API void timui_image_draw_clipped(TimuiFrame *f, TimuiImage *img,
                                        TimuiRect full, TimuiRect visible){
    Timui *ui;
    TimuiRect active;
    if(!f || !f->ui || !img || visible.w <= 0 || visible.h <= 0) return;
    ui = f->ui;
    active = ui->curr.has_clip ? ui->curr.clip : TIMUI_RECT(0, 0, ui->curr.w, ui->curr.h);
    visible = timui_intersect_rect_(visible, active);
    if(visible.w <= 0 || visible.h <= 0) return;
    image_record_(ui, img, visible, full);
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
    timui_draw_fill(&ui->curr, r, timui_widget_style_(ui, TIMUI_WIDGET_MENU, TIMUI_SLOT_MENU, 0));
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
        TimuiStyle st = timui_widget_style_(ui, TIMUI_WIDGET_MENU,
            is_open ? TIMUI_SLOT_MENU_ACTIVE : TIMUI_SLOT_MENU,
            is_open ? TIMUI_STYLE_STATE_ACTIVE : 0);
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
        TimuiStyle st = timui_widget_style_(ui, TIMUI_WIDGET_MENU,
            ir.hovered ? TIMUI_SLOT_MENU_ACTIVE : TIMUI_SLOT_MENU,
            ir.hovered ? TIMUI_STYLE_STATE_HOVERED : 0);
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
/*
 * timui_layout.c — ratatui-style constraint layout solver.
 *
 * timui_split divides an area along an axis into contiguous child rects. Fixed
 * (LEN/PCT) sizes are allocated first; the leftover is shared across the
 * flexible children (FLEX/MIN/MAX) by weight, with the LAST flexible child
 * absorbing the rounding remainder so the children tile the area EXACTLY (no
 * gaps or overlaps). MIN/MAX lower/upper bounds are honoured by a small
 * freeze-and-redistribute loop (CSS-flexbox style): each round hands the
 * remaining space to the still-free flexible children, then freezes any child
 * that lands outside its bound at that bound and repeats — terminating in at
 * most one round per flexible child. All sizes are clamped non-negative, and
 * contiguous placement clamps to the area boundary so over-constrained inputs
 * never produce negative or overflowing rects.
 *
 * timui_grid composes two splits (rows then columns); timui_split_h/_v are thin
 * axis-fixing wrappers. Everything here is a pure function of its arguments.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */

/* Upper bound on children per split — sizes the stack scratch arrays. A TUI
 * never splits one area into this many pieces; larger n is rejected (returns 0)
 * rather than silently truncated. */
#define TIMUI_LAYOUT_MAX 128

/* Round a * num / den to the nearest integer (den > 0; a, num >= 0). */
static int timui_round_div_(long a, long num, long den){
    if(den <= 0) return 0;
    return (int)((a * num + den / 2) / den);
}

/* A flexible constraint shares in the leftover space (FLEX/MIN/MAX). */
static int timui_con_is_flex_(TimuiConstraintKind k){
    return k == TIMUI_CON_FLEX || k == TIMUI_CON_MIN || k == TIMUI_CON_MAX;
}

/* Weight of a flexible constraint: FLEX uses its value (clamped >= 0); MIN/MAX
 * are single-weight fill segments. */
static int timui_con_weight_(const TimuiConstraint *c){
    if(c->kind == TIMUI_CON_FLEX) return c->value > 0 ? c->value : 0;
    return 1;   /* MIN / MAX */
}

TIMUI_API int timui_split_ex(TimuiRect area, TimuiAxis axis, const TimuiConstraint *cons,
                             int n, TimuiLayoutOpts opts, TimuiRect *out){
    int  size[TIMUI_LAYOUT_MAX];
    char locked[TIMUI_LAYOUT_MAX];
    int  i, gap, margin;
    int  axis_start, axis_len, cross_start, cross_len;
    int  inner_start, inner_len, cross_inner_start, cross_inner_len;
    int  avail, fixed_sum, leftover, remaining, pos, end;

    if(!cons || !out || n <= 0 || n > TIMUI_LAYOUT_MAX) return 0;

    gap    = opts.gap    > 0 ? opts.gap    : 0;
    margin = opts.margin > 0 ? opts.margin : 0;

    /* Project the area onto (axis, cross): H splits width, V splits height. */
    if(axis == TIMUI_AXIS_V){
        axis_start = area.y; axis_len = area.h; cross_start = area.x; cross_len = area.w;
    } else {
        axis_start = area.x; axis_len = area.w; cross_start = area.y; cross_len = area.h;
    }

    /* Outer margin shrinks both axes by `margin` on each side. */
    inner_start       = axis_start + margin;
    inner_len         = axis_len - 2 * margin;  if(inner_len < 0)       inner_len = 0;
    cross_inner_start = cross_start + margin;
    cross_inner_len   = cross_len - 2 * margin; if(cross_inner_len < 0) cross_inner_len = 0;

    /* Space the children actually divide — the (n-1) gaps live between them. */
    avail = inner_len - gap * (n - 1); if(avail < 0) avail = 0;

    /* Pass 1: fixed (LEN, PCT) sizes; flexible children start at 0. */
    fixed_sum = 0;
    for(i = 0; i < n; i++){
        locked[i] = 0;
        if(cons[i].kind == TIMUI_CON_LEN){
            size[i] = cons[i].value > 0 ? cons[i].value : 0;
            fixed_sum += size[i];
        } else if(cons[i].kind == TIMUI_CON_PCT){
            int p = cons[i].value < 0 ? 0 : cons[i].value;
            size[i] = timui_round_div_(avail, p, 100);
            fixed_sum += size[i];
        } else {
            size[i] = 0;   /* flexible — resolved in pass 2 */
        }
    }

    leftover = avail - fixed_sum; if(leftover < 0) leftover = 0;

    /* Pass 2: distribute the leftover across flexible children by weight,
     * honouring MIN/MAX bounds via freeze-and-redistribute. */
    remaining = leftover;
    for(;;){
        int free_weight = 0, last_free = -1, assigned = 0, changed = 0;
        for(i = 0; i < n; i++)
            if(timui_con_is_flex_(cons[i].kind) && !locked[i]){
                free_weight += timui_con_weight_(&cons[i]);
                last_free = i;
            }
        if(last_free < 0) break;   /* no free flexible children left */

        /* Provisional shares — the last free child gets the exact remainder so
         * the free children always sum to `remaining` (no rounding gap). */
        for(i = 0; i < n; i++){
            if(!timui_con_is_flex_(cons[i].kind) || locked[i]) continue;
            if(i == last_free){
                size[i] = remaining - assigned;
            } else {
                size[i] = timui_round_div_(remaining, timui_con_weight_(&cons[i]), free_weight);
                assigned += size[i];
            }
            if(size[i] < 0) size[i] = 0;
        }

        /* Freeze any child whose provisional share violates its bound. */
        for(i = 0; i < n; i++){
            if(!timui_con_is_flex_(cons[i].kind) || locked[i]) continue;
            if(cons[i].kind == TIMUI_CON_MIN && size[i] < cons[i].value){
                size[i] = cons[i].value < 0 ? 0 : cons[i].value;
                locked[i] = 1; remaining -= size[i]; changed = 1;
            } else if(cons[i].kind == TIMUI_CON_MAX && cons[i].value >= 0 && size[i] > cons[i].value){
                size[i] = cons[i].value;
                locked[i] = 1; remaining -= size[i]; changed = 1;
            }
        }
        if(remaining < 0) remaining = 0;
        if(!changed) break;   /* all free shares within bounds — solved */
    }

    /* Pass 3: place children contiguously, clamping each so it never goes
     * negative or overflows the area (the over-constrained case). */
    pos = inner_start;
    end = inner_start + inner_len;
    for(i = 0; i < n; i++){
        int s = size[i], rem;
        if(i > 0) pos += gap;
        rem = end - pos; if(rem < 0) rem = 0;
        if(s > rem) s = rem;
        if(s < 0) s = 0;
        if(axis == TIMUI_AXIS_V){
            out[i].x = cross_inner_start; out[i].w = cross_inner_len;
            out[i].y = pos;               out[i].h = s;
        } else {
            out[i].x = pos;               out[i].w = s;
            out[i].y = cross_inner_start; out[i].h = cross_inner_len;
        }
        pos += s;
    }
    return n;
}

TIMUI_API int timui_split(TimuiRect area, TimuiAxis axis, const TimuiConstraint *cons, int n, TimuiRect *out){
    TimuiLayoutOpts o; o.gap = 0; o.margin = 0;
    return timui_split_ex(area, axis, cons, n, o, out);
}
TIMUI_API int timui_split_h(TimuiRect area, const TimuiConstraint *cons, int n, TimuiRect *out){
    return timui_split(area, TIMUI_AXIS_H, cons, n, out);
}
TIMUI_API int timui_split_v(TimuiRect area, const TimuiConstraint *cons, int n, TimuiRect *out){
    return timui_split(area, TIMUI_AXIS_V, cons, n, out);
}

TIMUI_API int timui_grid_ex(TimuiRect area, const TimuiConstraint *rows, int nr,
                            const TimuiConstraint *cols, int nc, TimuiLayoutOpts opts, TimuiRect *out){
    TimuiRect rowrects[TIMUI_LAYOUT_MAX];
    int r;
    if(!rows || !cols || !out || nr <= 0 || nc <= 0 || nr > TIMUI_LAYOUT_MAX) return 0;
    /* Rows carve `area` into vertical bands; each band is then split into cells
     * by the column constraints. Output is row-major: out[r*nc + c]. */
    if(timui_split_ex(area, TIMUI_AXIS_V, rows, nr, opts, rowrects) != nr) return 0;
    for(r = 0; r < nr; r++)
        if(timui_split_ex(rowrects[r], TIMUI_AXIS_H, cols, nc, opts, out + (size_t)r * nc) != nc) return 0;
    return nr * nc;
}
TIMUI_API int timui_grid(TimuiRect area, const TimuiConstraint *rows, int nr,
                         const TimuiConstraint *cols, int nc, TimuiRect *out){
    TimuiLayoutOpts o; o.gap = 0; o.margin = 0;
    return timui_grid_ex(area, rows, nr, cols, nc, o, out);
}

#undef TIMUI_LAYOUT_MAX
/*
 * timui_box.c — line-drawing box frame + RGB colour interpolation.
 *
 * timui_border strokes a 1-cell frame around a rect using one of four Unicode
 * line styles (SINGLE / ROUNDED / DOUBLE / THICK), embeds an optional title in
 * the top edge, and returns the inner content rect (r inset by the frame). The
 * inner rect is computed and returned even when the frame is too small to draw
 * or the frame pointer is NULL, so callers can always lay out inside it.
 *
 * timui_lerp_rgb linearly interpolates two packed 0xRRGGBB colours. Both are
 * side-effect-free apart from the cell writes timui_border makes into the
 * frame's buffer (via the public text primitive, which handles clipping and
 * wide-glyph continuation).
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */

/* Encode one already-validated codepoint and stamp it at (x,y) through the
 * public text primitive (timui_utf8_encode_ is the shared encoder from
 * timui_int.h). Keeps the box logic on the public drawing API rather than
 * reaching into the renderer's private put_glyph. */
static void timui_box_put_(TimuiCellBuffer *buf, int x, int y, uint32_t cp, TimuiStyle st){
    char tmp[4];
    TimuiStr s;
    s.ptr = tmp;
    s.len = (size_t)timui_utf8_encode_(cp, tmp);
    timui_draw_text(buf, x, y, s, st);
}

TIMUI_API TimuiRect timui_border(TimuiFrame *f, TimuiRect r, TimuiBorderStyle style,
                                 TimuiStr title, TimuiStyle st){
    TimuiCellBuffer *buf;
    TimuiRect inner;
    uint32_t hz, vt, tl, tr, bl, br;
    int i;

    /* Inner content rect: r inset by the 1-cell frame, clamped non-negative.
     * Always computed so it is valid even on the no-draw paths below. */
    inner.x = r.x + 1;
    inner.y = r.y + 1;
    inner.w = r.w - 2; if(inner.w < 0) inner.w = 0;
    inner.h = r.h - 2; if(inner.h < 0) inner.h = 0;

    if(!f) return inner;
    buf = timui_frame_buffer(f);
    if(!buf) return inner;

    /* Glyph set per style: horizontal, vertical, and the four corners. */
    switch(style){
        case TIMUI_BOX_ROUNDED: hz=0x2500; vt=0x2502; tl=0x256D; tr=0x256E; bl=0x2570; br=0x256F; break;
        case TIMUI_BOX_DOUBLE:  hz=0x2550; vt=0x2551; tl=0x2554; tr=0x2557; bl=0x255A; br=0x255D; break;
        case TIMUI_BOX_THICK:   hz=0x2501; vt=0x2503; tl=0x250F; tr=0x2513; bl=0x2517; br=0x251B; break;
        case TIMUI_BOX_SINGLE:
        default:                hz=0x2500; vt=0x2502; tl=0x250C; tr=0x2510; bl=0x2514; br=0x2518; break;
    }

    /* Need a 2x2 rect to stroke a frame with distinct corners; otherwise the
     * inner rect is still returned for layout. */
    if(r.w < 2 || r.h < 2) return inner;

    timui_box_put_(buf, r.x,           r.y,           tl, st);
    timui_box_put_(buf, r.x + r.w - 1, r.y,           tr, st);
    timui_box_put_(buf, r.x,           r.y + r.h - 1, bl, st);
    timui_box_put_(buf, r.x + r.w - 1, r.y + r.h - 1, br, st);
    for(i = 1; i < r.w - 1; i++){
        timui_box_put_(buf, r.x + i, r.y,           hz, st);
        timui_box_put_(buf, r.x + i, r.y + r.h - 1, hz, st);
    }
    for(i = 1; i < r.h - 1; i++){
        timui_box_put_(buf, r.x,           r.y + i, vt, st);
        timui_box_put_(buf, r.x + r.w - 1, r.y + i, vt, st);
    }

    /* Optional title in the top edge, one cell in. Clip to the interior span so
     * a long title truncates cleanly rather than spilling over the corners. */
    if(title.ptr && title.len && r.w > 2){
        timui_push_clip(f, TIMUI_RECT(r.x + 1, r.y, r.w - 2, 1));
        timui_draw_text(buf, r.x + 1, r.y, title, st);
        timui_pop_clip(f);
    }

    return inner;
}

TIMUI_API uint32_t timui_lerp_rgb(uint32_t a, uint32_t b, float t){
    float ar = (float)((a >> 16) & 0xFF), ag = (float)((a >> 8) & 0xFF), ab = (float)(a & 0xFF);
    float br = (float)((b >> 16) & 0xFF), bg = (float)((b >> 8) & 0xFF), bb = (float)(b & 0xFF);
    unsigned rr, rg, rb;
    if(t < 0.0f) t = 0.0f;
    if(t > 1.0f) t = 1.0f;
    /* Each interpolated channel stays within [0,255], so rounding a
     * non-negative value with +0.5 is correct in both directions. */
    rr = (unsigned)(ar + (br - ar) * t + 0.5f);
    rg = (unsigned)(ag + (bg - ag) * t + 0.5f);
    rb = (unsigned)(ab + (bb - ab) * t + 0.5f);
    return (rr << 16) | (rg << 8) | rb;
}
/* ---- chart / indicator widgets (W3) ----------------------------------- *
 * Pure UI over caller-supplied values — there is NO DSP here (spectra, FFTs,
 * metering all live off the UI thread; see examples/radio.c). This section
 * promotes the reusable *look and feel* from that example into first-class
 * widgets: the vertical gradient bar with a floating peak-hold cap, plus
 * compact sparklines, horizontal gauge/meter/progress bars, and a spinner.
 *
 * Everything routes through the existing drawing primitives (timui_draw_fill /
 * timui_draw_text) so clipping, wide-glyph handling, and the diff renderer all
 * apply unchanged. The pure helpers (lerp / bar_cells / peak_hold /
 * spinner_glyph) are side-effect-free and unit-tested without a frame.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd. */

/* ============================ pure helpers ============================== */

/* timui_lerp_rgb is the shared colour-interpolation helper defined in
 * src/timui_box.c (included just before this section); the barchart gradient
 * uses it. It ROUNDS each channel (nearest), so a midpoint may differ by 1/255
 * from the radio example's original truncating gradient — visually identical. */

/* fraction of `value` relative to `max` (or the value itself when max<=0),
 * clamped to [0,1]. Shared by bar_cells and the widgets. */
static float timui_frac_(float value, float max){
    float frac = (max > 0.0f) ? value / max : value;
    if(frac < 0.0f) frac = 0.0f;
    if(frac > 1.0f) frac = 1.0f;
    return frac;
}

/* Number of filled cells for `value` over a `size`-cell track: the clamped
 * fraction rounded to the nearest whole cell. value>max fills the whole track;
 * value<0 fills none; size<=0 yields 0. */
TIMUI_API int timui_bar_cells(float value, float max, int size){
    int n;
    if(size <= 0) return 0;
    n = (int)(timui_frac_(value, max) * (float)size + 0.5f);   /* round to nearest */
    if(n < 0) n = 0;
    if(n > size) n = size;
    return n;
}

/* Peak-hold envelope for one cap: rises INSTANTLY to a higher `value`, else
 * releases LINEARLY by `decay` per call, never falling below the live `value`
 * (and, since levels are non-negative, never below 0). Feeding it once per
 * frame makes a floating cap chase peaks up and ease back down. */
TIMUI_API float timui_peak_hold(float cap, float value, float decay){
    float c;
    if(value >= cap) return value;          /* instant attack */
    c = cap - decay;                        /* linear release */
    return c < value ? value : c;
}

/* Codepoint of the braille "dots" throbber frame for `tick`. Ten frames; a
 * negative tick wraps (so a monotonically increasing OR decreasing counter both
 * animate). Cycle: ⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏. */
TIMUI_API uint32_t timui_spinner_glyph(int tick){
    static const uint32_t FRAMES[10] = {
        0x280Bu, 0x2819u, 0x2839u, 0x2838u, 0x283Cu,
        0x2834u, 0x2826u, 0x2827u, 0x2807u, 0x280Fu
    };
    int i = tick % 10;
    if(i < 0) i += 10;
    return FRAMES[i];
}

/* ============================ draw helpers ============================== */

/* Fill a single cell's background with `col` (a space glyph whose fg==bg reads
 * as a solid block). The one place the widgets paint a coloured cell. */
static void timui_cell_bg_(TimuiCellBuffer *buf, int x, int y, uint32_t col){
    timui_draw_fill(buf, TIMUI_RECT(x, y, 1, 1), timui_style_make(col, col, 0));
}

/* Format an unsigned integer into `out` (no stdio in the library). Returns the
 * digit count. */
static int timui_fmt_uint_(char *out, unsigned v){
    char tmp[12];
    int n = 0, i;
    if(v == 0){ out[0] = '0'; return 1; }
    while(v){ tmp[n++] = (char)('0' + (int)(v % 10)); v /= 10; }
    for(i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    return n;
}

/* "NN%" for a 0..1 fraction. Returns the byte count. */
static int timui_fmt_pct_(char *out, float frac){
    int n;
    if(frac < 0.0f) frac = 0.0f;
    if(frac > 1.0f) frac = 1.0f;
    n = timui_fmt_uint_(out, (unsigned)(int)(frac * 100.0f + 0.5f));
    out[n++] = '%';
    return n;
}

/* "W.FF" for a level (clamped to [0, 9.99]). Returns the byte count. */
static int timui_fmt_frac2_(char *out, float v){
    int whole, frac2, n;
    if(v < 0.0f) v = 0.0f;
    if(v > 9.99f) v = 9.99f;
    whole = (int)v;
    frac2 = (int)((v - (float)whole) * 100.0f + 0.5f);
    if(frac2 > 99) frac2 = 99;
    n = timui_fmt_uint_(out, (unsigned)whole);
    out[n++] = '.';
    out[n++] = (char)('0' + frac2 / 10);
    out[n++] = (char)('0' + frac2 % 10);
    return n;
}

/* ============================== widgets ================================= */

TIMUI_API void timui_barchart(TimuiFrame *f, TimuiRect r, const float *vals, int n,
                              TimuiBarOpts opts, TimuiBarState *st){
    TimuiCellBuffer *buf;
    int gap = opts.gap > 0 ? opts.gap : 1;
    int barH, bw, x0, b, use_labels;
    if(!f || !vals || n <= 0 || r.w <= 0 || r.h <= 0) return;
    buf = timui_frame_buffer(f);
    if(n > TIMUI_BAR_MAX) n = TIMUI_BAR_MAX;             /* cap to the state array */
    use_labels = (opts.labels != NULL) && (r.h >= 2);
    barH = use_labels ? r.h - 1 : r.h;                  /* reserve a row for labels */
    if(barH < 1) barH = 1;
    bw = (r.w - (n - 1) * gap) / n;                     /* even split, gaps between */
    if(bw < 1){ bw = 1; gap = 0; }                      /* too tight: drop gaps */
    x0 = r.x + (r.w - (bw * n + gap * (n - 1))) / 2;    /* centre the group */
    if(x0 < r.x) x0 = r.x;
    if(st) st->n = n;
    for(b = 0; b < n; b++){
        int bx = x0 + b * (bw + gap), row;
        float frac = timui_frac_(vals[b], opts.max);
        int filled = timui_bar_cells(vals[b], opts.max, barH), capr = -1;
        /* peak-hold cap (caller-owned state): rises instantly, decays per call */
        if(st && opts.peak_decay > 0.0f){
            st->caps[b] = timui_peak_hold(st->caps[b], frac, opts.peak_decay);
            capr = (int)(st->caps[b] * (float)barH + 0.5f);
            if(capr > barH) capr = barH;
        }
        for(row = 0; row < barH; row++){                /* row 0 == bottom cell */
            int y = r.y + barH - 1 - row;
            float t = (float)row / (float)(barH > 1 ? barH - 1 : 1);
            uint32_t base = timui_lerp_rgb(opts.lo, opts.hi, t), col;
            if(row < filled)          col = base;                         /* fill */
            else if(row + 1 == capr)  col = timui_lerp_rgb(base, 0xFFFFFFu, opts.cap_light);
            else                      col = opts.track;                   /* empty */
            timui_draw_fill(buf, TIMUI_RECT(bx, y, bw, 1), timui_style_make(col, col, 0));
        }
        if(use_labels && opts.labels[b]){               /* centred label on the last row */
            TimuiStr lbl = timui_str_from_cstr(opts.labels[b]);
            int lx = bx + (bw - (int)lbl.len) / 2;
            if(lx < r.x) lx = bx;
            timui_draw_text(buf, lx, r.y + r.h - 1, lbl, timui_style_make(opts.hi, opts.track, 0));
        }
    }
}

TIMUI_API void timui_sparkline(TimuiFrame *f, TimuiRect r, const float *history, int n,
                               TimuiStyle style){
    TimuiCellBuffer *buf;
    int cols, off, i;
    if(!f || !history || n <= 0 || r.w <= 0 || r.h <= 0) return;
    buf = timui_frame_buffer(f);
    cols = n < r.w ? n : r.w;               /* at most r.w most-recent samples */
    off  = r.w - cols;                      /* right-align within the rect */
    for(i = 0; i < cols; i++){
        /* map to one of eight sub-cell levels; level 0 stays blank */
        int level = timui_bar_cells(history[n - cols + i], 1.0f, 8);
        uint32_t cp = level <= 0 ? (uint32_t)' ' : (0x2580u + (uint32_t)level);
        char bytes[4];
        TimuiStr s;
        s.ptr = bytes;
        s.len = (size_t)timui_utf8_encode_(cp, bytes);   /* Z6 shared encoder */
        timui_draw_text(buf, r.x + off + i, r.y, s, style);
    }
}

/* Shared horizontal fill for gauge/meter/progress: a `r.w`-cell track filled to
 * `frac` with style.fg over style.bg, an optional bright peak `cap` tick
 * (cap < 0 disables it), and an optional readout right-aligned in a reserved
 * field. Draws on the first row (r.y). */
static void timui_hbar_(TimuiFrame *f, TimuiRect r, float frac, float cap,
                        TimuiStyle style, const char *readout, int rn){
    TimuiCellBuffer *buf = timui_frame_buffer(f);
    int reserve = (readout && rn > 0) ? rn + 1 : 0;     /* field + one-column gap */
    int track_w = r.w - reserve, filled;
    if(track_w < 1){ track_w = r.w; reserve = 0; }      /* no room: drop the readout */
    filled = timui_bar_cells(frac, 1.0f, track_w);
    if(filled > 0)
        timui_draw_fill(buf, TIMUI_RECT(r.x, r.y, filled, 1),
                        timui_style_make(style.fg, style.fg, 0));
    if(track_w - filled > 0)
        timui_draw_fill(buf, TIMUI_RECT(r.x + filled, r.y, track_w - filled, 1),
                        timui_style_make(style.bg, style.bg, 0));
    if(cap >= 0.0f){                                     /* floating peak-hold tick */
        int capx = timui_bar_cells(cap, 1.0f, track_w);
        if(capx >= track_w) capx = track_w - 1;
        if(capx >= filled && capx >= 0)
            timui_cell_bg_(buf, r.x + capx, r.y, timui_lerp_rgb(style.fg, 0xFFFFFFu, 0.5f));
    }
    if(reserve){
        TimuiStr s;
        s.ptr = readout;
        s.len = (size_t)rn;
        timui_draw_text(buf, r.x + track_w + 1, r.y, s,
                        timui_style_make(style.fg, style.bg, style.attrs));
    }
}

TIMUI_API void timui_progress(TimuiFrame *f, TimuiRect r, float frac, TimuiStyle style){
    char out[8];
    if(!f || r.w <= 0 || r.h <= 0) return;
    timui_hbar_(f, r, frac, -1.0f, style, out, timui_fmt_pct_(out, frac));
}

TIMUI_API void timui_gauge(TimuiFrame *f, TimuiRect r, float frac, TimuiStyle style){
    char out[8];
    if(!f || r.w <= 0 || r.h <= 0) return;
    timui_hbar_(f, r, frac, -1.0f, style, out, timui_fmt_frac2_(out, frac));
}

TIMUI_API void timui_meter(TimuiFrame *f, TimuiRect r, float level, float cap, TimuiStyle style){
    char out[8];
    if(!f || r.w <= 0 || r.h <= 0) return;
    timui_hbar_(f, r, level, cap, style, out, timui_fmt_frac2_(out, level));
}

TIMUI_API void timui_spinner(TimuiFrame *f, int x, int y, int tick, TimuiStyle style){
    TimuiCellBuffer *buf;
    char bytes[4];
    TimuiStr s;
    if(!f) return;
    buf = timui_frame_buffer(f);
    s.ptr = bytes;
    s.len = (size_t)timui_utf8_encode_(timui_spinner_glyph(tick), bytes);
    timui_draw_text(buf, x, y, s, style);
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
/*
 * timui_syntax.c — table-driven syntax highlighter + read-only code viewer (W4).
 *
 * Promoted from the chat/sqlite examples' header-only highlighter into a first-
 * class library section. It scans a source span and emits, in source order, the
 * NON-default token spans (keywords, types, strings, comments, numbers, …);
 * whatever it does not emit is plain text (TIMUI_HL_TEXT) that the consumer
 * paints with the default colour. Keeping the token stream sparse lets a renderer
 * walk "gap, token, gap, token, …" trivially.
 *
 * Languages: "c", "sh"/"bash", "python"/"py", "sql" (case-insensitive
 * keywords/types, double-dash line comments, C-style block comments), and a
 * NULL/"" generic mode (strings, # and // line comments, block comments, numbers
 * — no keywords). An unknown language name falls back to generic.
 *
 * Design:
 *   - Table-driven: each language is a small TimuiHlLang descriptor (feature bits
 *     + keyword/type tables). One shared scan loop drives them all.
 *   - Bounded & pure: every scan helper advances by at least one byte (no
 *     infinite loops), reads only within [0,len), and treats bytes as unsigned so
 *     non-ASCII input can never be misclassified or overrun.
 *   - Whole-word keyword matches: a full identifier is scanned and matched exact,
 *     so `iffy` never matches `if`.
 *   - No allocation, no globals, no I/O. The tables are const (read-only, shared).
 *
 * The static helpers are prefixed timui_hl_ so this section coexists in one TU
 * with the examples' chat_highlight.h (which uses hl_* names) — chat.c and
 * sqlite_tui.c include both.
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
 */

/* ----------------------------------------------------------------------- */
/* Character predicates. Each takes an int already holding an unsigned-char  */
/* value (0..255) so behaviour is well-defined for non-ASCII bytes.          */
/* ----------------------------------------------------------------------- */

static int timui_hl_is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '\f' || c == '\v';
}
static int timui_hl_is_digit(int c) { return c >= '0' && c <= '9'; }
static int timui_hl_is_hexdigit(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}
static int timui_hl_is_ident_start(int c)
{
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static int timui_hl_is_ident(int c)
{
    return timui_hl_is_ident_start(c) || timui_hl_is_digit(c);
}

/* ASCII punctuation not otherwise consumed as a string/comment/number/ident.
 * NUL is excluded so strchr's terminator can't produce a false positive. */
static int timui_hl_is_punct(int c)
{
    return c != 0 &&
           strchr("+-*/%=<>!&|^~?:;,.()[]{}@$#`\\", c) != NULL;
}

/* ----------------------------------------------------------------------- */
/* Span scanners. Each returns the index one past the scanned span; on a     */
/* truncated/unterminated span it returns `len` (never reads past it).       */
/* ----------------------------------------------------------------------- */

/* A single-line quoted run starting at the opening quote s[i]. `esc` enables
 * backslash escaping (so \" does not close the string). A newline ends an
 * unterminated string; the closing quote is included when present. */
static int timui_hl_scan_quoted(const char *s, int len, int i, char quote, int esc)
{
    int j = i + 1;
    while (j < len) {
        char ch = s[j];
        if (esc && ch == '\\') { j += 2; continue; } /* skip the escaped byte */
        if (ch == quote) return j + 1;
        if (ch == '\n') return j;                    /* unterminated at EOL */
        j++;
    }
    return len;                                      /* unterminated at EOF */
}

/* A Python triple-quoted string starting at s[i] (s[i..i+2] are all `q`). */
static int timui_hl_scan_triple(const char *s, int len, int i, char q)
{
    int j = i + 3;
    while (j < len) {
        if (s[j] == '\\') { j += 2; continue; }
        if (s[j] == q && j + 2 < len && s[j + 1] == q && s[j + 2] == q)
            return j + 3;
        j++;
    }
    return len;                                      /* unterminated */
}

/* A C block comment starting at s[i] (s[i]=='/', s[i+1]=='*'). */
static int timui_hl_scan_block(const char *s, int len, int i)
{
    int j = i + 2;
    while (j + 1 < len) {
        if (s[j] == '*' && s[j + 1] == '/') return j + 2;
        j++;
    }
    return len;                                      /* unterminated */
}

/* A line comment: from s[i] up to (not including) the next newline. */
static int timui_hl_scan_line(const char *s, int len, int i)
{
    int j = i;
    while (j < len && s[j] != '\n') j++;
    return j;
}

/* A number: decimal / hex (0x…) / float (frac + e/E exponent) with integer
 * and float suffixes (u l f). Called only when s[i] begins a number. */
static int timui_hl_scan_number(const char *s, int len, int i)
{
    int j = i;
    if (s[j] == '0' && j + 1 < len && (s[j + 1] == 'x' || s[j + 1] == 'X')) {
        j += 2;
        while (j < len && timui_hl_is_hexdigit((unsigned char)s[j])) j++;
    } else {
        while (j < len && timui_hl_is_digit((unsigned char)s[j])) j++;
        if (j < len && s[j] == '.') {
            j++;
            while (j < len && timui_hl_is_digit((unsigned char)s[j])) j++;
        }
        if (j < len && (s[j] == 'e' || s[j] == 'E')) {
            int k = j + 1;
            if (k < len && (s[k] == '+' || s[k] == '-')) k++;
            if (k < len && timui_hl_is_digit((unsigned char)s[k])) {
                j = k + 1;
                while (j < len && timui_hl_is_digit((unsigned char)s[j])) j++;
            }
        }
    }
    while (j < len && s[j] != 0 && strchr("uUlLfF", s[j]) != NULL) j++;
    return j;
}

/* An identifier: [A-Za-z_][A-Za-z0-9_]* starting at s[i]. */
static int timui_hl_scan_ident(const char *s, int len, int i)
{
    int j = i;
    while (j < len && timui_hl_is_ident((unsigned char)s[j])) j++;
    return j;
}

/* A shell $-expansion at s[i]=='$': ${...}, $name, or a special param
 * ($#, $@, $*, $?, $!, $$, $-, $0..$9). Returns i+1 for a bare '$'. */
static int timui_hl_scan_dollar(const char *s, int len, int i)
{
    int j = i + 1;
    if (j >= len) return j;                          /* trailing '$' */
    if (s[j] == '{') {
        j++;
        while (j < len && s[j] != '}' && s[j] != '\n') j++;
        if (j < len && s[j] == '}') j++;             /* include '}' */
        return j;
    }
    if (timui_hl_is_ident_start((unsigned char)s[j])) {
        while (j < len && timui_hl_is_ident((unsigned char)s[j])) j++;
        return j;
    }
    if (s[j] != 0 && (strchr("#@*?!$-", s[j]) != NULL ||
                      timui_hl_is_digit((unsigned char)s[j])))
        return j + 1;
    return i + 1;                                    /* bare '$' */
}

/* A C preprocessor directive from the '#' at s[i] to end of line. Line
 * continuations (\<nl>) extend it; a string inside is skipped whole (so a //
 * inside it is not a comment); a real trailing line- or block-comment start
 * ends the directive so the comment itself stays highlighted as a comment. */
static int timui_hl_scan_preproc(const char *s, int len, int i)
{
    int j = i;
    while (j < len) {
        char ch = s[j];
        if (ch == '\n') return j;                    /* end of directive */
        if (ch == '\\' && j + 1 < len) { j += 2; continue; } /* continuation */
        if (ch == '"' || ch == '\'') { j = timui_hl_scan_quoted(s, len, j, ch, 1); continue; }
        if (ch == '/' && j + 1 < len && s[j + 1] == '/') return j; /* // */
        if (ch == '/' && j + 1 < len && s[j + 1] == '*') return j; /* block */
        j++;
    }
    return len;
}

/* ----------------------------------------------------------------------- */
/* Language descriptors (keyword/type tables are const; shared read-only).   */
/* ----------------------------------------------------------------------- */

static const char *const timui_hl_c_kw[] = {
    "auto", "break", "case", "const", "continue", "default", "do", "else",
    "enum", "extern", "for", "goto", "if", "inline", "register", "restrict",
    "return", "signed", "sizeof", "static", "struct", "switch", "typedef",
    "union", "unsigned", "void", "volatile", "while", "asm", "_Complex",
    "_Imaginary", "_Alignas", "_Alignof", "_Atomic", "_Generic", "_Noreturn",
    "_Static_assert", "_Thread_local", NULL
};
static const char *const timui_hl_c_ty[] = {
    "int", "char", "short", "long", "float", "double", "bool", "_Bool", NULL
};
static const char *const timui_hl_sh_kw[] = {
    "if", "then", "elif", "else", "fi", "for", "while", "until", "do", "done",
    "case", "esac", "in", "function", "select", "return", "local", "export",
    NULL
};
static const char *const timui_hl_py_kw[] = {
    "def", "class", "if", "elif", "else", "for", "while", "return", "import",
    "from", "as", "with", "try", "except", "finally", "lambda", "None", "True",
    "False", "and", "or", "not", "in", "is", "pass", "break", "continue",
    "global", "nonlocal", "yield", "raise", "assert", "del", "async", "await",
    NULL
};
/* SQL (matched case-insensitively via TimuiHlLang.nocase — stored lowercase).
 * Covers common DML/DDL + clause + operator keywords; SQLite-flavoured. */
static const char *const timui_hl_sql_kw[] = {
    "select", "from", "where", "insert", "into", "values", "update", "set",
    "delete", "create", "table", "index", "view", "trigger", "drop", "alter",
    "add", "rename", "column", "join", "inner", "left", "right", "outer",
    "full", "cross", "natural", "on", "using", "group", "by", "order",
    "having", "limit", "offset", "as", "distinct", "all", "union", "intersect",
    "except", "and", "or", "not", "null", "is", "in", "like", "glob", "regexp",
    "match", "between", "exists", "case", "when", "then", "else", "end",
    "pragma", "begin", "commit", "rollback", "savepoint", "release",
    "transaction", "if", "primary", "key", "foreign", "references", "unique",
    "check", "default", "autoincrement", "constraint", "collate", "asc",
    "desc", "with", "recursive", "replace", "conflict", "abort", "fail",
    "ignore", "vacuum", "analyze", "reindex", "attach", "detach", "explain",
    "cast", "returning", "without", "rowid", "temp", "temporary", "escape",
    "nulls", "first", "last", "over", "partition", "window", "filter",
    NULL
};
/* SQL type / affinity names (SQLite is affinity-based, so these are advisory). */
static const char *const timui_hl_sql_ty[] = {
    "integer", "int", "smallint", "bigint", "tinyint", "text", "varchar",
    "char", "nchar", "nvarchar", "clob", "blob", "real", "double", "float",
    "numeric", "decimal", "boolean", "bool", "date", "datetime", "timestamp",
    "time", NULL
};

/* Feature bits + tables for one language. */
typedef struct {
    const char *const *kw;    /* keyword table (NULL-terminated) or NULL */
    const char *const *ty;    /* type table (NULL-terminated) or NULL */
    unsigned line_hash  : 1;  /* '#' starts a line comment (after ws/BOL) */
    unsigned line_slash : 1;  /* '//' starts a line comment */
    unsigned line_dash  : 1;  /* '--' starts a line comment (SQL) */
    unsigned block      : 1;  /* C-style block comments */
    unsigned preproc    : 1;  /* '#' at BOL = whole-line preprocessor */
    unsigned triple     : 1;  /* triple-quoted strings */
    unsigned dollar     : 1;  /* $VAR / ${…} expansions */
    unsigned sq_char    : 1;  /* single quote is a C char literal */
    unsigned sq_escape  : 1;  /* backslash escapes inside single-quoted strings */
    unsigned t_heur     : 1;  /* identifiers ending in _t are types */
    unsigned nocase     : 1;  /* keyword/type matching is case-insensitive (SQL) */
} TimuiHlLang;

static TimuiHlLang timui_hl_lang_for(const char *lang)
{
    TimuiHlLang L;
    memset(&L, 0, sizeof L);

    if (lang != NULL && strcmp(lang, "c") == 0) {
        L.kw = timui_hl_c_kw; L.ty = timui_hl_c_ty;
        L.line_slash = 1; L.block = 1; L.preproc = 1;
        L.sq_char = 1; L.sq_escape = 1; L.t_heur = 1;
        return L;
    }
    if (lang != NULL && (strcmp(lang, "sh") == 0 || strcmp(lang, "bash") == 0)) {
        L.kw = timui_hl_sh_kw;
        L.line_hash = 1; L.dollar = 1; L.sq_escape = 0; /* sh '' is literal */
        return L;
    }
    if (lang != NULL && (strcmp(lang, "python") == 0 || strcmp(lang, "py") == 0)) {
        L.kw = timui_hl_py_kw;
        L.line_hash = 1; L.triple = 1; L.sq_escape = 1;
        return L;
    }
    if (lang != NULL && strcmp(lang, "sql") == 0) {
        L.kw = timui_hl_sql_kw; L.ty = timui_hl_sql_ty;
        L.line_dash = 1; L.block = 1; L.nocase = 1;
        /* SQL strings are single-quoted with '' doubling (no backslash escape);
         * double quotes are identifiers. Both scan as string spans here. */
        L.sq_escape = 0;
        return L;
    }
    /* generic (NULL / "" / unknown): both comment styles, both string quotes,
     * numbers, no keywords. */
    L.line_hash = 1; L.line_slash = 1; L.block = 1; L.sq_escape = 1;
    return L;
}

/* ASCII lowercase — for case-insensitive keyword matching (SQL). */
static int timui_hl_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* Whole-word membership test for the span code[off..off+n) against a
 * NULL-terminated table. `nocase` compares ASCII case-insensitively. */
static int timui_hl_in_list(const char *code, int off, int n,
                            const char *const *list, int nocase)
{
    int k;
    if (list == NULL) return 0;
    for (k = 0; list[k] != NULL; k++) {
        if ((int)strlen(list[k]) != n) continue;
        if (!nocase) {
            if (memcmp(code + off, list[k], (size_t)n) == 0) return 1;
        } else {
            int j, eq = 1;
            for (j = 0; j < n; j++)
                if (timui_hl_lower((unsigned char)code[off + j]) !=
                    timui_hl_lower((unsigned char)list[k][j])) { eq = 0; break; }
            if (eq) return 1;
        }
    }
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Public: highlighter.                                                      */
/* ----------------------------------------------------------------------- */

TIMUI_API int timui_highlight(const char *src, int len, const char *lang,
                              TimuiHlTok *out, int max)
{
    TimuiHlLang L;
    int n = 0, i = 0, at_bol = 1;

    if (src == NULL || len <= 0 || out == NULL || max <= 0) return 0;
    L = timui_hl_lang_for(lang);

    /* Each iteration handles the byte at `i` and pushes AT MOST one token.
     * We enter the loop only while n < max, so a push can never overflow. */
    while (i < len && n < max) {
        int c = (unsigned char)src[i];
        int start = i, end;
        int bol;
        TimuiHlClass cls;

        /* Whitespace and newlines are HL_TEXT gaps — never emitted. */
        if (c == '\n') { at_bol = 1; i++; continue; }
        if (timui_hl_is_space(c)) { i++; continue; }

        bol = at_bol;   /* is this the first non-space token on its line? */
        at_bol = 0;

        /* Comments first, so '/' and '#' cannot be seen as punctuation. */
        if (L.block && c == '/' && i + 1 < len && src[i + 1] == '*') {
            end = timui_hl_scan_block(src, len, i); cls = TIMUI_HL_COMMENT;
        } else if (L.line_slash && c == '/' && i + 1 < len && src[i + 1] == '/') {
            end = timui_hl_scan_line(src, len, i); cls = TIMUI_HL_COMMENT;
        } else if (L.line_dash && c == '-' && i + 1 < len && src[i + 1] == '-') {
            end = timui_hl_scan_line(src, len, i); cls = TIMUI_HL_COMMENT;
        } else if (L.preproc && c == '#' && bol) {
            end = timui_hl_scan_preproc(src, len, i); cls = TIMUI_HL_PREPROC;
        } else if (L.line_hash && c == '#' &&
                   (i == 0 || timui_hl_is_space((unsigned char)src[i - 1]))) {
            end = timui_hl_scan_line(src, len, i); cls = TIMUI_HL_COMMENT;
        }
        /* Strings and character literals. */
        else if (c == '"') {
            if (L.triple && i + 2 < len && src[i + 1] == '"' && src[i + 2] == '"')
                end = timui_hl_scan_triple(src, len, i, '"');
            else
                end = timui_hl_scan_quoted(src, len, i, '"', 1);
            cls = TIMUI_HL_STRING;
        } else if (c == '\'') {
            if (L.sq_char) {
                end = timui_hl_scan_quoted(src, len, i, '\'', 1); cls = TIMUI_HL_CHAR;
            } else if (L.triple && i + 2 < len &&
                       src[i + 1] == '\'' && src[i + 2] == '\'') {
                end = timui_hl_scan_triple(src, len, i, '\''); cls = TIMUI_HL_STRING;
            } else {
                end = timui_hl_scan_quoted(src, len, i, '\'', (int)L.sq_escape);
                cls = TIMUI_HL_STRING;
            }
        }
        /* Shell variable expansion. */
        else if (L.dollar && c == '$') {
            end = timui_hl_scan_dollar(src, len, i);
            cls = (end == start + 1) ? TIMUI_HL_PUNCT : TIMUI_HL_TYPE; /* bare '$' -> punct */
        }
        /* Numbers. */
        else if (timui_hl_is_digit(c) ||
                 (c == '.' && i + 1 < len &&
                  timui_hl_is_digit((unsigned char)src[i + 1]))) {
            end = timui_hl_scan_number(src, len, i); cls = TIMUI_HL_NUMBER;
        }
        /* Identifiers: keyword / type / *_t heuristic, else plain text. */
        else if (timui_hl_is_ident_start(c)) {
            int tl;
            end = timui_hl_scan_ident(src, len, i);
            tl = end - start;
            if (timui_hl_in_list(src, start, tl, L.kw, L.nocase)) cls = TIMUI_HL_KEYWORD;
            else if (timui_hl_in_list(src, start, tl, L.ty, L.nocase)) cls = TIMUI_HL_TYPE;
            else if (L.t_heur && tl > 2 &&
                     src[end - 2] == '_' && src[end - 1] == 't') cls = TIMUI_HL_TYPE;
            else { i = end; continue; }  /* plain identifier => HL_TEXT gap */
        }
        /* Punctuation. */
        else if (timui_hl_is_punct(c)) {
            end = i + 1; cls = TIMUI_HL_PUNCT;
        }
        /* Anything else (non-ASCII bytes, NUL, …) is text. */
        else { i++; continue; }

        out[n].off = start;
        out[n].len = end - start;
        out[n].cls = cls;
        n++;
        i = end;
    }
    return n;
}

/* ----------------------------------------------------------------------- */
/* Public: class -> default colour (a Night-Owl-ish palette over CODE_BG).   */
/* ----------------------------------------------------------------------- */

/* Read-only code viewer palette (matches the chat example's fenced blocks). */
#define TIMUI_CODE_BG     0x1B1E2Bu   /* subtle code-block background */
#define TIMUI_CODE_FG     0xD6DEEBu   /* default code text colour     */
#define TIMUI_CODE_GUTTER 0x5C6478u   /* muted line-number gutter     */

TIMUI_API uint32_t timui_hl_color(TimuiHlClass cls)
{
    switch (cls) {
        case TIMUI_HL_KEYWORD: return 0xC792EAu;                        /* purple */
        case TIMUI_HL_TYPE:    return 0x82AAFFu;                        /* blue   */
        case TIMUI_HL_STRING:  /* fall through: string + char share green */
        case TIMUI_HL_CHAR:    return 0xC3E88Du;                        /* green  */
        case TIMUI_HL_COMMENT: return 0x7A88A0u;                        /* muted  */
        case TIMUI_HL_NUMBER:  return 0xF78C6Cu;                        /* orange */
        case TIMUI_HL_PREPROC: return 0xFFCB6Bu;                        /* yellow */
        case TIMUI_HL_PUNCT:   return 0x89DDFFu;                        /* cyan   */
        case TIMUI_HL_TEXT:    /* fall through */
        default:               return TIMUI_CODE_FG;
    }
}

/* ----------------------------------------------------------------------- */
/* Public: read-only code viewer.                                            */
/* ----------------------------------------------------------------------- */

/* Clamp a top-line scroll offset to [0, max(0, nlines - visible)]. Pure. */
TIMUI_API int timui_code_scroll_clamp(int scroll, int nlines, int visible)
{
    int maxscroll;
    if (nlines < 0) nlines = 0;
    if (visible < 0) visible = 0;
    maxscroll = nlines - visible;
    if (maxscroll < 0) maxscroll = 0;
    if (scroll < 0) scroll = 0;
    if (scroll > maxscroll) scroll = maxscroll;
    return scroll;
}

/* Count the '\n'-separated lines in src[0..len): 1 + the number of newlines
 * (an empty buffer still has one, empty, line). */
static int timui_hl_count_lines(const char *src, int len)
{
    int i, n = 1;
    for (i = 0; i < len; i++) if (src[i] == '\n') n++;
    return n;
}

/* Decimal digit count of a positive line number (>=1 => at least 1). */
static int timui_hl_digits(int n)
{
    int d = 1;
    if (n < 1) return 1;
    while (n >= 10) { n /= 10; d++; }
    return d;
}

/* Render the 1-based line number `ln` right-aligned into `out` (which must hold
 * w+1 bytes), left-padded with spaces and NUL-terminated. */
static void timui_hl_fmt_lineno(int ln, int w, char *out)
{
    int i = w;
    out[w] = '\0';
    while (i > 0) { i--; out[i] = ' '; }
    i = w - 1;
    if (ln < 1) ln = 1;
    while (ln > 0 && i >= 0) { out[i--] = (char)('0' + ln % 10); ln /= 10; }
}

/* Draw one already-isolated source line at row `y`, columns [x0, x1), with a
 * per-token syntax colour over CODE_BG. Not wrapped — clips at x1 (and the
 * caller's pushed clip guards the buffer edges). */
static void timui_hl_draw_line(TimuiFrame *f, int x0, int y, int x1,
                               const char *line, int llen, const char *lang)
{
    TimuiHlTok toks[256];
    int nt = timui_highlight(line, llen, lang, toks, 256);
    int ti = 0, col = x0, i = 0;
    while (i < llen && col < x1) {
        TimuiHlClass cls = TIMUI_HL_TEXT;
        TimuiStr ch;
        uint32_t cp = 0;
        int adv, w;
        /* Advance past tokens that end at/before this byte, then adopt the one
         * covering it (gaps stay TIMUI_HL_TEXT). */
        while (ti < nt && i >= toks[ti].off + toks[ti].len) ti++;
        if (ti < nt && i >= toks[ti].off) cls = toks[ti].cls;
        adv = timui_utf8_decode(line + i, (size_t)(llen - i), &cp);
        if (adv <= 0) adv = 1;
        ch.ptr = line + i; ch.len = (size_t)adv;
        timui_label(f, col, y, ch, timui_style_make(timui_hl_color(cls), TIMUI_CODE_BG, 0));
        w = timui_utf8_width(cp);
        col += w > 0 ? w : 0;
        i += adv;
    }
}

TIMUI_API void timui_code(TimuiFrame *f, TimuiRect r, const char *src, int len,
                          const char *lang, int *scroll)
{
    TimuiCellBuffer *buf;
    int nlines, top, digits, gutter, codex, row, lo;

    if (!f || !src || r.w <= 0 || r.h <= 0) return;
    if (len < 0) len = 0;
    buf = timui_frame_buffer(f);
    if (!buf) return;

    nlines = timui_hl_count_lines(src, len);

    /* Clamp the scroll offset (in place) so neither end overscrolls. */
    top = scroll ? timui_code_scroll_clamp(*scroll, nlines, r.h) : 0;
    if (scroll) *scroll = top;

    /* A line-number gutter "<num> " when the rect is wide enough to leave room
     * for at least one column of code; otherwise draw code flush-left. */
    digits = timui_hl_digits(nlines);
    gutter = digits + 1;                 /* digits + a single-space separator */
    codex  = (r.w > gutter + 1) ? r.x + gutter : r.x;

    /* Subtle code background across the whole rect, then draw on top of it. */
    timui_draw_fill(buf, r, timui_style_make(TIMUI_CODE_FG, TIMUI_CODE_BG, 0));
    timui_push_clip(f, r);               /* guard glyphs against the rect edges */

    /* Walk src to the first visible line, then render r.h rows. */
    lo = 0;
    { int skipped = 0;
      while (skipped < top && lo < len) { if (src[lo] == '\n') skipped++; lo++; } }

    for (row = 0; row < r.h; row++) {
        int lineno = top + row + 1;      /* 1-based */
        int y = r.y + row, hi;
        if (top + row >= nlines) break;  /* past the last line -> just background */

        /* This line spans [lo, hi); hi is the next '\n' or end of buffer. */
        hi = lo;
        while (hi < len && src[hi] != '\n') hi++;

        if (codex != r.x) {              /* draw the gutter number (right-aligned) */
            char num[24];
            int nw = digits < (int)sizeof num - 1 ? digits : (int)sizeof num - 1;
            timui_hl_fmt_lineno(lineno, nw, num);
            timui_label(f, r.x, y, timui_str_from_cstr(num),
                        timui_style_make(TIMUI_CODE_GUTTER, TIMUI_CODE_BG, 0));
        }
        timui_hl_draw_line(f, codex, y, r.x + r.w, src + lo, hi - lo, lang);

        lo = (hi < len) ? hi + 1 : hi;   /* step past the newline to the next line */
    }

    timui_pop_clip(f);
}
#endif /* TIMUI_IMPLEMENTATION */
