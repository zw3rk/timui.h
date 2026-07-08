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

static int timui_clamp_i64_to_int_(int64_t v){
    if(v < (int64_t)INT_MIN) return INT_MIN;
    if(v > (int64_t)INT_MAX) return INT_MAX;
    return (int)v;
}

/* ---- rect layout (clamps to non-negative; never overflows the parent) -- */
TIMUI_API TimuiRect timui_cut_top(TimuiRect *r, int h){
    TimuiRect out = {0, 0, 0, 0};
    if(!r) return out;
    if(h < 0) h = 0;
    if(h > r->h) h = r->h;
    out.x = r->x; out.y = r->y; out.w = r->w; out.h = h;
    r->y = timui_clamp_i64_to_int_((int64_t)r->y + (int64_t)h);
    r->h -= h;
    return out;
}
TIMUI_API TimuiRect timui_cut_bottom(TimuiRect *r, int h){
    TimuiRect out = {0, 0, 0, 0};
    if(!r) return out;
    if(h < 0) h = 0;
    if(h > r->h) h = r->h;
    r->h -= h;
    out.x = r->x;
    out.y = timui_clamp_i64_to_int_((int64_t)r->y + (int64_t)r->h);
    out.w = r->w;
    out.h = h;
    return out;
}
TIMUI_API TimuiRect timui_cut_left(TimuiRect *r, int w){
    TimuiRect out = {0, 0, 0, 0};
    if(!r) return out;
    if(w < 0) w = 0;
    if(w > r->w) w = r->w;
    out.x = r->x; out.y = r->y; out.w = w; out.h = r->h;
    r->x = timui_clamp_i64_to_int_((int64_t)r->x + (int64_t)w);
    r->w -= w;
    return out;
}
TIMUI_API TimuiRect timui_cut_right(TimuiRect *r, int w){
    TimuiRect out = {0, 0, 0, 0};
    if(!r) return out;
    if(w < 0) w = 0;
    if(w > r->w) w = r->w;
    r->w -= w;
    out.x = timui_clamp_i64_to_int_((int64_t)r->x + (int64_t)r->w);
    out.y = r->y;
    out.w = w;
    out.h = r->h;
    return out;
}
TIMUI_API TimuiRect timui_inset(TimuiRect r, int n){
    int64_t shrink;
    if(n < 0) n = 0;
    shrink = (int64_t)n * 2;
    r.x = timui_clamp_i64_to_int_((int64_t)r.x + (int64_t)n);
    r.y = timui_clamp_i64_to_int_((int64_t)r.y + (int64_t)n);
    r.w = ((int64_t)r.w > shrink) ? (int)((int64_t)r.w - shrink) : 0;
    r.h = ((int64_t)r.h > shrink) ? (int)((int64_t)r.h - shrink) : 0;
    return r;
}
TIMUI_API TimuiRect timui_pad(TimuiRect r, int l, int t, int rr, int b){
    int64_t sw, sh;
    if(l < 0) l = 0; if(t < 0) t = 0; if(rr < 0) rr = 0; if(b < 0) b = 0;
    sw = (int64_t)l + (int64_t)rr;
    sh = (int64_t)t + (int64_t)b;
    r.x = timui_clamp_i64_to_int_((int64_t)r.x + (int64_t)l);
    r.y = timui_clamp_i64_to_int_((int64_t)r.y + (int64_t)t);
    r.w = ((int64_t)r.w > sw) ? (int)((int64_t)r.w - sw) : 0;
    r.h = ((int64_t)r.h > sh) ? (int)((int64_t)r.h - sh) : 0;
    return r;
}
TIMUI_API void timui_split_cols(TimuiRect r, float ratio, TimuiRect *a, TimuiRect *b){
    int aw;
    if(!(ratio >= 0.0f)) ratio = 0.0f;
    if(ratio > 1.0f) ratio = 1.0f;
    aw = (int)(r.w * ratio);
    if(a){ a->x = r.x;      a->y = r.y; a->w = aw;       a->h = r.h; }
    if(b){ b->x = timui_clamp_i64_to_int_((int64_t)r.x + (int64_t)aw); b->y = r.y; b->w = r.w - aw; b->h = r.h; }
}
TIMUI_API void timui_split_rows(TimuiRect r, float ratio, TimuiRect *a, TimuiRect *b){
    int ah;
    if(!(ratio >= 0.0f)) ratio = 0.0f;
    if(ratio > 1.0f) ratio = 1.0f;
    ah = (int)(r.h * ratio);
    if(a){ a->x = r.x; a->y = r.y;      a->w = r.w; a->h = ah; }
    if(b){ b->x = r.x; b->y = timui_clamp_i64_to_int_((int64_t)r.y + (int64_t)ah); b->w = r.w; b->h = r.h - ah; }
}

/* Z10: undo the section's implementation-only macros so they can't leak into
 * the consumer's translation unit in the amalgamated single header. */
#undef TIMUI_ID_ROOT
#undef TIMUI_MPSC_LOCK
#undef TIMUI_MPSC_UNLOCK
