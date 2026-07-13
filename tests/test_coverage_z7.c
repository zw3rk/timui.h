/*
 * test_coverage_z7.c — round-7 (Z-series) coverage: error/OOM paths and
 * previously untested public entry points.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "test.h"
#include "timui.h"

#include <string.h>
#include <stdlib.h>

/* ---- net-byte counting allocator -------------------------------------- *
 * Fails the `fail_at`-th alloc/realloc call (1-based; 0 = never) and tracks
 * live bytes so a leak in a rollback branch shows up as `live != 0`. A failed
 * realloc returns NULL WITHOUT touching the old block (the W11 preserve-on-
 * failure contract). Each block carries an 8-byte size header. */
typedef struct { int calls; int fail_at; long live; } CountAlloc;

static void *ca_alloc(void *ud, size_t n){
    CountAlloc *c = (CountAlloc *)ud;
    size_t *p;
    if(++c->calls == c->fail_at) return NULL;
    p = (size_t *)malloc(sizeof(size_t) + n);
    if(!p) return NULL;
    *p = n; c->live += (long)n;
    return p + 1;
}
static void ca_free(void *ud, void *p, size_t n){
    CountAlloc *c = (CountAlloc *)ud; (void)n;
    if(p){ size_t *q = (size_t *)p - 1; c->live -= (long)*q; free(q); }
}
static void *ca_realloc(void *ud, void *p, size_t on, size_t nn){
    CountAlloc *c = (CountAlloc *)ud; (void)on;
    if(++c->calls == c->fail_at) return NULL;         /* old block preserved */
    if(!p){
        size_t *q = (size_t *)malloc(sizeof(size_t) + nn);
        if(!q) return NULL;
        *q = nn; c->live += (long)nn; return q + 1;
    }
    { size_t *q = (size_t *)p - 1, old = *q, *nq;
      nq = (size_t *)realloc(q, sizeof(size_t) + nn);
      if(!nq) return NULL;                            /* original q still valid */
      *nq = nn; c->live += (long)nn - (long)old; return nq + 1; }
}
static TimuiAllocator counting_allocator(CountAlloc *c){
    TimuiAllocator a;
    a.userdata = c; a.alloc = ca_alloc; a.realloc = ca_realloc; a.free = ca_free;
    return a;
}

/* Z15: every partial-init rollback branch in timui_setup must free everything
 * it allocated. Driving each branch (fail_at 1..N) under the counting allocator
 * makes a leak/double-free visible (live != 0) even without ASAN — and, in CI,
 * ASAN now actually sees these branches (it can't flag a branch no test enters). */
TIMUI_TEST(test_open_oom_cleanup){
    int fail_at, saw_fail = 0, saw_ok = 0;
    TimuiAllocator def = timui_default_allocator();
    for(fail_at = 1; fail_at <= 8; fail_at++){
        CountAlloc ca = {0, 0, 0};
        TimuiAllocator al;
        TimuiFakeTransport fake;
        TimuiTransport t;
        Timui *ui = NULL;
        TimuiResult r;
        ca.fail_at = fail_at;
        al = counting_allocator(&ca);
        timui_fake_init(&fake, &def);                 /* fake uses its own allocator */
        t = timui_fake_transport(&fake);
        r = timui_open_for_test(&ui, t, 30, 10, &al); /* only this uses the counter */
        if(r == TIMUI_OK){
            saw_ok++;
            TIMUI_CHECK(ui != NULL);
            timui_close(ui);
        } else {
            saw_fail++;
            TIMUI_CHECK(ui == NULL);                  /* *out cleared on failure */
        }
        TIMUI_CHECK(ca.live == 0);                    /* no rollback branch leaks */
        timui_fake_destroy(&fake);
    }
    TIMUI_CHECK(saw_fail >= 3 && saw_ok >= 1);        /* loop exercised both sides */
}

/* Z16: the functional-runner message API (emit/post/recv/frame_quit). */
TIMUI_TEST(test_message_api_roundtrip){
    TimuiAllocator def = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    uint32_t type = 0;
    char buf[16];
    size_t sz;
    timui_fake_init(&fake, &def);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 5, &def);

    TIMUI_CHECK(timui_post_result(ui, 7, "hi", 2) == TIMUI_OK);
    sz = sizeof buf;
    TIMUI_CHECK(timui_recv(ui, &type, buf, &sz));
    TIMUI_CHECK(type == 7 && sz == 2 && memcmp(buf, "hi", 2) == 0);

    TIMUI_CHECK(timui_post(ui, 7, "hi", 2));          /* thread-safe post */
    sz = sizeof buf;
    TIMUI_CHECK(timui_recv(ui, &type, buf, &sz));
    TIMUI_CHECK(type == 7 && sz == 2 && memcmp(buf, "hi", 2) == 0);

    timui_begin(ui, &f);                              /* emit during a frame */
    TIMUI_CHECK(timui_emit_result(f, 9, "x", 1) == TIMUI_OK);
    timui_end(f);
    sz = sizeof buf;
    TIMUI_CHECK(timui_recv(ui, &type, buf, &sz) && type == 9 && sz == 1 && buf[0] == 'x');

    timui_begin(ui, &f);
    TIMUI_CHECK(timui_emit(f, 9, "x", 1));
    timui_end(f);
    sz = sizeof buf;
    TIMUI_CHECK(timui_recv(ui, &type, buf, &sz) && type == 9 && sz == 1 && buf[0] == 'x');

    sz = sizeof buf;
    TIMUI_CHECK(!timui_recv(ui, &type, buf, &sz));    /* empty queue -> false */

    timui_begin(ui, &f);                              /* frame_quit -> should_quit */
    timui_frame_quit(f);
    timui_end(f);
    TIMUI_CHECK(timui_should_quit(ui));

    TIMUI_CHECK(!timui_post(NULL, 1, "a", 1));        /* NULL guards */
    TIMUI_CHECK(!timui_emit(NULL, 1, "a", 1));
    TIMUI_CHECK(!timui_recv(NULL, &type, buf, &sz));
    TIMUI_CHECK(timui_post_result(NULL, 1, "a", 1) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(timui_post_result(ui, 1, NULL, 1) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(timui_emit_result(NULL, 1, "a", 1) == TIMUI_ERR_INVALID_ARGUMENT);
    TIMUI_CHECK(timui_emit_result(f, 1, NULL, 1) == TIMUI_ERR_INVALID_ARGUMENT);
    timui_frame_quit(NULL);                           /* no crash */

    timui_close(ui);
    timui_fake_destroy(&fake);
}

TIMUI_TEST(test_message_api_reports_oom){
    TimuiAllocator def = timui_default_allocator();
    CountAlloc ca = {0, 0, 0};
    TimuiAllocator al = counting_allocator(&ca);
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    int fail_next;

    timui_fake_init(&fake, &def);
    t = timui_fake_transport(&fake);
    TIMUI_CHECK(timui_open_for_test(&ui, t, 20, 5, &al) == TIMUI_OK);

    fail_next = ca.calls + 1;
    ca.fail_at = fail_next;
    TIMUI_CHECK(timui_post_result(ui, 1, "x", 1) == TIMUI_ERR_OUT_OF_MEMORY);
    ca.fail_at = 0;

    timui_close(ui);
    TIMUI_CHECK(ca.live == 0);
    timui_fake_destroy(&fake);
}

/* Z18: the layout-helper siblings test_rect.c doesn't cover. */
TIMUI_TEST(test_layout_siblings){
    TimuiRect r, cut, a, b, p;
    r = TIMUI_RECT(0, 0, 10, 10); cut = timui_cut_left(&r, 3);
    TIMUI_CHECK(cut.x == 0 && cut.w == 3 && r.x == 3 && r.w == 7);
    r = TIMUI_RECT(0, 0, 10, 10); cut = timui_cut_right(&r, 3);
    TIMUI_CHECK(cut.x == 7 && cut.w == 3 && r.x == 0 && r.w == 7);
    p = timui_pad(TIMUI_RECT(0, 0, 10, 10), 1, 2, 3, 4);
    TIMUI_CHECK(p.x == 1 && p.y == 2 && p.w == 6 && p.h == 4);   /* w=10-1-3, h=10-2-4 */
    timui_split_rows(TIMUI_RECT(0, 0, 10, 100), 0.25f, &a, &b);
    TIMUI_CHECK(a.h == 25 && b.y == 25 && b.h == 75);
    /* over-cut clamps to the remaining extent, never negative */
    r = TIMUI_RECT(0, 0, 10, 10); cut = timui_cut_left(&r, 999);
    TIMUI_CHECK(cut.w == 10 && r.w == 0 && r.x == 10);
    /* ratio clamps to [0,1] */
    timui_split_rows(TIMUI_RECT(0, 0, 10, 40), 2.0f, &a, &b);
    TIMUI_CHECK(a.h == 40 && b.h == 0);
}

/* Z19: the frame-level hyperlink label wrapper. */
TIMUI_TEST(test_label_hyperlink_wrapper){
    TimuiAllocator def = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    TimuiCell *cell;
    TimuiStyle st = timui_style_make(0xffffff, TIMUI_COLOR_DEFAULT, 0);
    timui_fake_init(&fake, &def);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 3, &def);

    timui_begin(ui, &f);
    timui_label_hyperlink(f, 1, 0, TIMUI_STR_LIT("go"), "https://x", st);
    buf = timui_frame_buffer(f);
    cell = timui_cells_get(buf, 1, 0);
    TIMUI_CHECK(cell && cell->codepoint == 'g' && cell->hyperlink_id != 0);
    TIMUI_CHECK(buf->link_count >= 1);
    TIMUI_CHECK(strcmp(buf->links[cell->hyperlink_id - 1].uri, "https://x") == 0);
    timui_end(f);

    timui_begin(ui, &f);                              /* uri == NULL -> plain, no link */
    timui_label_hyperlink(f, 1, 0, TIMUI_STR_LIT("no"), NULL, st);
    cell = timui_cells_get(timui_frame_buffer(f), 1, 0);
    TIMUI_CHECK(cell && cell->codepoint == 'n' && cell->hyperlink_id == 0);
    timui_end(f);

    timui_close(ui);
    timui_fake_destroy(&fake);
}

/* Z20: the function-bar widget. */
TIMUI_TEST(test_function_bar_widget){
    TimuiAllocator def = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;
    timui_fake_init(&fake, &def);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 3, &def);

    timui_begin(ui, &f);
    timui_function_bar(f, TIMUI_RECT(0, 0, 10, 1), TIMUI_STR_LIT("F1 Help"));
    buf = timui_frame_buffer(f);
    TIMUI_CHECK(timui_cells_get(buf, 0, 0)->codepoint == 'F');
    TIMUI_CHECK(timui_cells_get(buf, 1, 0)->codepoint == '1');
    timui_end(f);
    timui_function_bar(NULL, TIMUI_RECT(0, 0, 10, 1), TIMUI_STR_LIT("x"));  /* no crash */

    timui_close(ui);
    timui_fake_destroy(&fake);
}

TIMUI_TEST(test_function_bar_clipped){
    TimuiAllocator def = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    TimuiFrame *f = NULL;
    TimuiCellBuffer *buf;

    timui_fake_init(&fake, &def);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 10, 3, &def);

    timui_begin(ui, &f);
    buf = timui_frame_buffer(f);
    timui_function_bar(f, TIMUI_RECT(1, 1, 4, 1), TIMUI_STR_LIT("abcdefgh"));
    TIMUI_CHECK(timui_cells_get(buf, 5, 1)->codepoint == 0);
    timui_end(f);

    timui_close(ui);
    timui_fake_destroy(&fake);
}

/* Z21: the interact tab_order grow-OOM branch (V24 covered only the success
 * path). A failed grow must drop the widget from the Tab cycle without writing
 * past the (unallocated) tab_order — never crash, never tab_count > tab_cap. */
TIMUI_TEST(test_interact_tab_grow_oom){
    CountAlloc ca = {0, 0, 0};
    TimuiAllocator al = counting_allocator(&ca);
    TimuiInteract ia;
    int i;
    timui_interact_init(&ia, &al);
    ca.fail_at = 1;                                   /* fail the first tab_order grow */
    timui_interact_begin(&ia);
    for(i = 0; i < 20; i++)
        (void)timui_interact_button(&ia, (TimuiId)(i + 1), TIMUI_RECT(0, i, 5, 1));
    timui_interact_end(&ia);
    TIMUI_CHECK(ia.tab_count <= ia.tab_cap);          /* no write past the buffer */
    timui_interact_destroy(&ia);                      /* safe after a failed grow */
    TIMUI_CHECK(ca.live == 0);
}

/* Z22: timui_hyperlink_set NULL / grow-OOM / URI-truncation branches. */
TIMUI_TEST(test_hyperlink_set_edges){
    TimuiAllocator def = timui_default_allocator();
    TimuiCellBuffer b;
    int i;
    timui_cells_init(&b, 4, 1, &def);
    TIMUI_CHECK(timui_hyperlink_set(NULL, "x") == 0);
    TIMUI_CHECK(timui_hyperlink_set(&b, NULL) == 0);
    for(i = 1; i <= 9; i++)                           /* grows past the initial cap of 8 */
        TIMUI_CHECK(timui_hyperlink_set(&b, "u") == (uint32_t)i);
    {   /* a 299-char URI is stored NUL-terminated at 255 (uri[256]) */
        char big[300];
        uint32_t id;
        memset(big, 'a', sizeof big - 1); big[sizeof big - 1] = '\0';
        id = timui_hyperlink_set(&b, big);
        TIMUI_CHECK(id != 0 && strlen(b.links[id - 1].uri) == 255);
    }
    timui_cells_destroy(&b);

    {   /* grow-OOM: the 9th link's realloc fails -> id 0, count unchanged */
        CountAlloc ca = {0, 0, 0};
        TimuiAllocator al = counting_allocator(&ca);
        TimuiCellBuffer fb;
        timui_cells_init(&fb, 4, 1, &al);
        for(i = 1; i <= 8; i++) TIMUI_CHECK(timui_hyperlink_set(&fb, "u") == (uint32_t)i);
        ca.fail_at = ca.calls + 1;                    /* next alloc (the grow) fails */
        TIMUI_CHECK(timui_hyperlink_set(&fb, "u") == 0);
        TIMUI_CHECK(fb.link_count == 8);
        timui_cells_destroy(&fb);
        TIMUI_CHECK(ca.live == 0);
    }
}

/* Z23: timui_run's negative guards (the positive path opens a real tty). */
TIMUI_TEST(test_run_negative_guards){
    TimuiConfig cfg;
    TimuiApp app;
    timui_config_init(&cfg);
    memset(&app, 0, sizeof app);
    TIMUI_CHECK(timui_run(NULL, &app) == 1);
    TIMUI_CHECK(timui_run(&cfg, NULL) == 1);
    app.view = NULL;                                  /* view is required */
    TIMUI_CHECK(timui_run(&cfg, &app) == 1);
}

typedef struct {
    TimuiFakeTransport *fake;
    size_t out_len_at_update;
    size_t msg_size;
    unsigned char first;
    unsigned char last;
    int updates;
} AppFrameProbe;

static void app_frame_view(TimuiFrame *f, void *model){
    AppFrameProbe *p = (AppFrameProbe *)model;
    (void)p;
    timui_label(f, 0, 0, TIMUI_STR_LIT("X"), timui_style_make(0xFFFFFF, 0x000000, 0));
    timui_emit(f, 11, "z", 1);
}

static void app_frame_large_view(TimuiFrame *f, void *model){
    AppFrameProbe *p = (AppFrameProbe *)model;
    unsigned char payload[5000];
    size_t i;
    for(i = 0; i < sizeof payload; i++) payload[i] = (unsigned char)(i & 0xffu);
    timui_label(f, 0, 0, TIMUI_STR_LIT("L"), timui_style_make(0xFFFFFF, 0x000000, 0));
    timui_emit(f, 22, payload, sizeof payload);
    (void)p;
}

static void app_frame_update(void *model, uint32_t type, const void *msg, size_t msg_size){
    AppFrameProbe *p = (AppFrameProbe *)model;
    TimuiStr out = timui_fake_output(p->fake);
    p->updates++;
    p->out_len_at_update = out.len;
    p->msg_size = msg_size;
    if(msg_size > 0 && msg){
        const unsigned char *b = (const unsigned char *)msg;
        p->first = b[0];
        p->last = b[msg_size - 1];
    }
    (void)type;
}

TIMUI_TEST(test_app_frame_updates_after_end){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    AppFrameProbe probe;
    TimuiApp app;
    memset(&probe, 0, sizeof probe);
    memset(&app, 0, sizeof app);
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 5, &al);
    probe.fake = &fake;
    app.model = &probe;
    app.view = app_frame_view;
    app.update = app_frame_update;
    TIMUI_CHECK(timui_app_frame(ui, &app) == 1);
    TIMUI_CHECK(probe.updates == 1);
    TIMUI_CHECK(probe.out_len_at_update > 0);
    TIMUI_CHECK(probe.msg_size == 1 && probe.first == 'z' && probe.last == 'z');
    timui_close(ui);
    timui_fake_destroy(&fake);
}

TIMUI_TEST(test_app_frame_delivers_large_messages){
    TimuiAllocator al = timui_default_allocator();
    TimuiFakeTransport fake;
    TimuiTransport t;
    Timui *ui = NULL;
    AppFrameProbe probe;
    TimuiApp app;
    memset(&probe, 0, sizeof probe);
    memset(&app, 0, sizeof app);
    timui_fake_init(&fake, &al);
    t = timui_fake_transport(&fake);
    timui_open_for_test(&ui, t, 20, 5, &al);
    probe.fake = &fake;
    app.model = &probe;
    app.view = app_frame_large_view;
    app.update = app_frame_update;
    TIMUI_CHECK(timui_app_frame(ui, &app) == 1);
    TIMUI_CHECK(probe.updates == 1);
    TIMUI_CHECK(probe.msg_size == 5000);
    TIMUI_CHECK(probe.first == 0);
    TIMUI_CHECK(probe.last == (unsigned char)((5000 - 1) & 0xff));
    timui_close(ui);
    timui_fake_destroy(&fake);
}

/* Z24: the trivial getters str_len / now_ms. */
TIMUI_TEST(test_getters){
    uint64_t a, b;
    TIMUI_CHECK(timui_str_len(TIMUI_STR_LIT("hi")) == 2);
    TIMUI_CHECK(timui_str_len(TIMUI_STR_LIT("")) == 0);
    a = timui_now_ms();
    b = timui_now_ms();
    TIMUI_CHECK(b >= a);                              /* monotonic non-decreasing */
}
