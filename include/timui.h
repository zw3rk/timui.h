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
#define TIMUI_API_VERSION \
    ((TIMUI_VERSION_MAJOR << 16) | (TIMUI_VERSION_MINOR << 8) | TIMUI_VERSION_PATCH)

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
    size_t            struct_size;
    uint32_t          api_version;
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
#define TIMUI_CONFIG_INIT    ((TimuiConfig){ sizeof(TimuiConfig), TIMUI_API_VERSION, NULL, 0, 1, \
                                             TIMUI_PROFILE_AUTO, TIMUI_FLAG_RESTORE_ON_EXIT, \
                                             TIMUI_THEME_MODERN_DARK, {0}, 0, 0, 0, NULL })

/* ---- Lifecycle (POSIX terminal backend; Win32 ConPTY transport) -------- */
TIMUI_API void        timui_config_init(TimuiConfig *cfg);
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
/* Reset cached terminal state after an external terminal mode/style reset.
 * Does not force cell repaint by itself. */
TIMUI_API void             timui_invalidate(Timui *ui);
/* Force the next timui_end to repaint every cell. Use after subprocess output,
 * terminal reset, suspend/resume, or any external write that may have changed
 * screen contents behind timui's diff renderer. */
TIMUI_API void             timui_full_redraw(Timui *ui);
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

/* Run one functional app frame on an already-open Timui. Useful when the caller
 * owns the outer loop; returns 1 when a frame was rendered, 0 on invalid args,
 * a quit state, or begin failure. Posted messages are delivered to update()
 * after timui_end(), so terminal/image flushes see the model used by view(). */
TIMUI_API int  timui_app_frame(Timui *ui, TimuiApp *app);
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

#ifndef TIMUI_IMAGE_MAX_DIMENSION
#define TIMUI_IMAGE_MAX_DIMENSION 4096
#endif
#ifndef TIMUI_IMAGE_MAX_PIXELS
#define TIMUI_IMAGE_MAX_PIXELS 16777216u
#endif
#ifndef TIMUI_IMAGE_PNG_MAX_BYTES
#define TIMUI_IMAGE_PNG_MAX_BYTES 16777216u
#endif
#ifndef TIMUI_IMAGE_PNG_MAX_DIMENSION
#define TIMUI_IMAGE_PNG_MAX_DIMENSION TIMUI_IMAGE_MAX_DIMENSION
#endif
#ifndef TIMUI_IMAGE_PNG_MAX_PIXELS
#define TIMUI_IMAGE_PNG_MAX_PIXELS TIMUI_IMAGE_MAX_PIXELS
#endif
#ifndef TIMUI_IMAGE_PLACEMENT_CAP
#define TIMUI_IMAGE_PLACEMENT_CAP 8
#endif

TIMUI_API TimuiImage *timui_image_from_png(Timui *ui, const void *data, size_t size);
TIMUI_API TimuiImage *timui_image_from_rgba(Timui *ui, const void *rgba,
                                            int w, int h, int stride);
/* Original PNG bytes plus caller-supplied decoded RGBA pixels. This lets apps
 * avoid the lazy PNG decode path while preserving PNG passthrough for
 * Kitty/iTerm2 and giving Sixel exact pixels for emission and clipping. The
 * supplied RGBA dimensions must match the PNG and drive source cropping. */
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
#include "../src/timui_int.h"
#include "../src/timui_core.c"
#include "../src/timui_render.c"
#include "../src/timui_stylesheet.c"
#include "../src/timui_grapheme.c"
#include "../src/timui_term.c"
#include "../src/timui_input.c"
#include "../src/timui_widgets.c"
#include "../src/timui_tabs.c"
#include "../src/timui_clip.c"
#include "../src/timui_scroll.c"
#include "../src/timui_clipboard.c"
#include "../src/timui_keymap.c"
#include "../src/timui_table.c"
#include "../src/timui_tree.c"
#include "../src/timui_cmdpal.c"
#include "../src/timui_combobox.c"
#include "../src/timui_toast.c"
#include "../src/timui_splitpane.c"
#include "../src/timui_snapshot.c"
#include "../src/timui_textarea.c"
#include "../src/timui_conpty.c"
#include "../src/timui_images.c"
#include "../src/timui_menus.c"
#include "../src/timui_layout.c"
#include "../src/timui_box.c"
#include "../src/timui_chart.c"
#include "../src/timui_app.c"
#include "../src/timui_syntax.c"
#endif /* TIMUI_IMPLEMENTATION */
