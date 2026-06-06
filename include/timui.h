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

/* ---- Strings ----------------------------------------------------------- */
TIMUI_API size_t timui_str_len(TimuiStr s);
TIMUI_API int    timui_str_empty(TimuiStr s);
TIMUI_API int    timui_str_eq(TimuiStr a, TimuiStr b);

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

#include <string.h>

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

/* ---- strings ----------------------------------------------------------- */
TIMUI_API size_t timui_str_len(TimuiStr s){ return s.len; }
TIMUI_API int    timui_str_empty(TimuiStr s){ return s.len == 0; }
TIMUI_API int    timui_str_eq(TimuiStr a, TimuiStr b){
    if(a.len != b.len) return 0;
    if(a.len == 0) return 1;
    return memcmp(a.ptr, b.ptr, a.len) == 0;
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
