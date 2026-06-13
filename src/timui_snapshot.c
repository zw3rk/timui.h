/* ---- snapshot testing (v0.2) ------------------------------------------ *
 * Render a cell-buffer row to an ASCII string for golden comparison. */
TIMUI_API void timui_snapshot_render(const TimuiCellBuffer *buf, int row, char *out, size_t cap){
    int x;
    size_t j = 0;
    if(!buf || !out || cap == 0 || row < 0 || row >= buf->h){ if(out && cap) out[0] = '\0'; return; }
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
