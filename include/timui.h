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

#endif /* TIMUI_IMPLEMENTATION */
