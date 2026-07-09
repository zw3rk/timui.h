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
        if(ev.as.mouse.motion){
            ev.as.mouse.pressed = 0;
            ev.as.mouse.released = 0;
        } else if(btn == 3){
            ev.as.mouse.pressed = 0;
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
            if(c == '<'){ p->csi_mouse = 1; p->mcount = 0; p->mparam[0] = p->mparam[1] = p->mparam[2] = -1; break; }
            if(c == '?' || c == '>' || c == '='){ break; }              /* private marker */
            /* Z4: ':' opens a sub-parameter (Kitty event-type / alternate-key
             * reports). timui does not use sub-parameters, so ignore their
             * digits until the next ';' or final byte — but stay in CSI state
             * so the base key is not dropped and the tail is not leaked. */
            if(c == ':'){ p->sub_param = 1; break; }
            if(c >= '0' && c <= '9'){
                if(p->sub_param){ break; }                              /* discard sub-parameter digits */
                if(p->csi_mouse){
                    if(p->mcount < 3 && p->mparam[p->mcount] < 99999){
                        if(p->mparam[p->mcount] < 0) p->mparam[p->mcount] = c - '0';
                        else p->mparam[p->mcount] = p->mparam[p->mcount] * 10 + (c - '0');
                    }
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
                    if((c == 'M' || c == 'm') &&
                       p->mcount == 2 && p->mparam[0] >= 0 &&
                       p->mparam[1] > 0 && p->mparam[2] > 0){
                        emit_mouse(cb, ctx, p->mparam, c);
                        count++;
                    }
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
            if(c >= 0x80) i--;     /* non-ASCII may be a UTF-8 lead; do not drop it */
            break;
        case 3: /* SS3 (ESC O X) */
            /* Z3: an ESC here aborts the truncated SS3 and restarts a fresh
             * escape rather than being swallowed as a bogus final byte. */
            if(c == 0x1b){ p->state = 1; p->esc_since_ms = p->now_ms; break; }
            {
                TimuiKey k = ss3_final(c);
                if(k != TIMUI_KEY_UNKNOWN){ emit_key(cb, ctx, k, 0, 0); count++; }
                p->state = 0;
                if(k == TIMUI_KEY_UNKNOWN && c >= 0x80) i--;
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
