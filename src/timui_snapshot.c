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
