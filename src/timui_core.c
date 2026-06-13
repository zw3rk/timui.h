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
static void ui_event_cb(void *ctx, const TimuiEvent *ev){
    Timui *ui = (Timui *)ctx;
    if(ui->event_count < (int)(sizeof(ui->events) / sizeof(ui->events[0])))
        ui->events[ui->event_count++] = *ev;
    else
        ui->events_dropped++;
}
static int fd_write(TimuiTransport *t, const void *d, size_t n){
    TimuiFdCtx *c = (TimuiFdCtx *)t->ctx;
    ssize_t w = write(c->write_fd, d, n);
    return w < 0 ? -1 : (int)w;
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
    timui_interact_init(&ui->ia);
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
    timui_caps_detect(&ui->caps, NULL, NULL, NULL);
    r = timui_setup(ui, w, h);
    if(r != TIMUI_OK){ alloc->free(alloc->userdata, ui, sizeof *ui); return r; }
    *out_ui = ui;
    return TIMUI_OK;
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
        if(flags >= 0) (void)fcntl(cfg->input_fd, F_SETFL, flags | O_NONBLOCK);  /* nonblocking input */
        if(timui_termios_enter(&ui->termios, cfg->input_fd) == TIMUI_OK) ui->termios_active = 1;
        timui_screen_enter(&ui->transport, &ui->screen, cfg->flags, timui_str_from_cstr(cfg->title));
        ui->screen_active = 1;
    }
    r = timui_setup(ui, w, h);
    if(r != TIMUI_OK){
        if(ui->screen_active) timui_screen_exit(&ui->transport, &ui->screen);
        if(ui->termios_active){ timui_termios_restore(&ui->termios); timui_termios_destroy(&ui->termios); }
        al.free(al.userdata, ui, sizeof *ui);
        return r;
    }
    *out_ui = ui;
    return TIMUI_OK;
}
TIMUI_API void timui_close(Timui *ui){
    TimuiAllocator al;
    if(!ui) return;
    if(ui->screen_active) timui_screen_exit(&ui->transport, &ui->screen);
    if(ui->termios_active){ timui_termios_restore(&ui->termios); timui_termios_destroy(&ui->termios); }
    if(ui->have_buffers){ timui_cells_destroy(&ui->curr); timui_cells_destroy(&ui->prev); }
    if(ui->have_postq) timui_mpsc_destroy(&ui->postq);
    if(ui->have_ids) timui_id_stack_destroy(&ui->ids);
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
            poll(&pfd, 1, 16);   /* block up to 16ms (~60fps) */
        }
        n = ui->transport.read(&ui->transport, buf, sizeof buf);
        if(n > 0){
            timui_input_set_now(&ui->input, timui_now_ms());
            timui_input_feed(&ui->input, buf, (size_t)n, ui_event_cb, ui);
        }
        timui_input_flush_esc(&ui->input, timui_now_ms(), ui_event_cb, ui);
    }
    /* drain parsed events: mouse -> hit-testing; tab/enter -> interaction;
     * printable text + cursor keys -> the focused input's accumulator. */
    ui->text_in_len = 0;
    ui->key_in = 0;
    ui->key_pressed = TIMUI_KEY_UNKNOWN;
    {
        TimuiEvent ev;
        while(timui_poll_event(ui, &ev)){
            if(ev.kind == TIMUI_EVENT_MOUSE){
                timui_interact_set_mouse(&ui->ia, ev.as.mouse.x - 1, ev.as.mouse.y - 1, ev.as.mouse.pressed);
            } else if(ev.kind == TIMUI_EVENT_KEY){
                ui->key_pressed = ev.as.key.key;   /* app-level key detection */
                ui->key_mods = ev.as.key.mods;
                if(ev.as.key.key == TIMUI_KEY_TAB) timui_interact_set_keys(&ui->ia, 1, 0);
                else if(ev.as.key.key == TIMUI_KEY_ENTER) timui_interact_set_keys(&ui->ia, 0, 1);
                else if(ev.as.key.key == TIMUI_KEY_BACKSPACE) ui->key_in |= TIMUI_KEYIN_BACKSPACE;
                else if(ev.as.key.key == TIMUI_KEY_LEFT) ui->key_in |= TIMUI_KEYIN_LEFT;
                else if(ev.as.key.key == TIMUI_KEY_RIGHT) ui->key_in |= TIMUI_KEYIN_RIGHT;
                else if(ev.as.key.key == TIMUI_KEY_UP) ui->key_in |= TIMUI_KEYIN_UP;
                else if(ev.as.key.key == TIMUI_KEY_DOWN) ui->key_in |= TIMUI_KEYIN_DOWN;
            } else if(ev.kind == TIMUI_EVENT_TEXT){
                /* UTF-8 encode the codepoint into text_in (supports international input) */
                uint32_t cp = ev.as.text.codepoint;
                char enc[4]; int enclen = 0;
                if(cp < 0x80){ enc[0] = (char)cp; enclen = 1; }
                else if(cp < 0x800){ enc[0] = (char)(0xC0 | (cp >> 6)); enc[1] = (char)(0x80 | (cp & 0x3F)); enclen = 2; }
                else if(cp < 0x10000){ enc[0] = (char)(0xE0 | (cp >> 12)); enc[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); enc[2] = (char)(0x80 | (cp & 0x3F)); enclen = 3; }
                else { enc[0] = (char)(0xF0 | (cp >> 18)); enc[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); enc[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); enc[3] = (char)(0x80 | (cp & 0x3F)); enclen = 4; }
                if(enclen > 0 && ui->text_in_len + enclen <= (int)sizeof(ui->text_in)){
                    int ei;
                    for(ei = 0; ei < enclen; ei++) ui->text_in[ui->text_in_len++] = enc[ei];
                }
            }
        }
    }
    timui_interact_begin(&ui->ia);
    ui->curr.has_clip = 0;            /* fresh clip stack each frame */
    ui->clip_count = 0;
    timui_cells_clear(&ui->curr);
    ui->ids.count = 0;                  /* fresh id stack for this frame */
    ui->frame.ui = ui;
    *out_frame = &ui->frame;
    return true;
}
TIMUI_API void timui_end(TimuiFrame *frame){
    Timui *ui;
    TimuiCellBuffer tmp;
    if(!frame || !frame->ui) return;
    ui = frame->ui;
    timui_interact_end(&ui->ia);
    timui_render_diff(&ui->transport, &ui->prev, &ui->curr, &ui->renderer);
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
TIMUI_API void timui_ui_resize(Timui *ui, int w, int h){
    TimuiResult r;
    if(!ui || w <= 0 || h <= 0) return;
    r = timui_cells_resize(&ui->curr, w, h, &ui->alloc);
    if(r != TIMUI_OK) return;
    r = timui_cells_resize(&ui->prev, w, h, &ui->alloc);
    if(r != TIMUI_OK) return;
    ui->w = w;
    ui->h = h;
    timui_renderer_reset(&ui->renderer);   /* cursor/SGR tracking invalidated */
}
TIMUI_API int timui_poll_event(Timui *ui, TimuiEvent *out_event){
    int i;
    if(!ui || !out_event || ui->event_count == 0) return 0;
    *out_event = ui->events[0];
    ui->event_count--;
    for(i = 0; i < ui->event_count; i++) ui->events[i] = ui->events[i + 1];
    return 1;
}
TIMUI_API void timui_quit(Timui *ui){ if(ui) ui->should_quit = 1; }
TIMUI_API bool timui_should_quit(const Timui *ui){ return ui ? (bool)ui->should_quit : false; }
TIMUI_API int timui_key_pressed(TimuiFrame *f, TimuiKey key){
    return (f && f->ui && f->ui->key_pressed == key);
}
TIMUI_API int timui_key_pressed_mods(TimuiFrame *f, TimuiKey key, uint32_t mods){
    return (f && f->ui && f->ui->key_pressed == key &&
            (f->ui->key_mods & mods) == mods);
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
/* Note: on OOM during geometric grow, the push is silently dropped (void return).
 * The caller cannot detect this. If this matters, use a sufficiently large
 * initial capacity via timui_id_stack_init. See docs/gaps.md G6. */
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
    size_t need;
    unsigned char *p;
    if(!q || size > SIZE_MAX - hdr) return 0;   /* overflow guard */
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

