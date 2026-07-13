# API reference — timui.h

Public surface of the timui.h single-header C99 immediate-mode TUI. Include
`timui.h` for the declarations; define `TIMUI_IMPLEMENTATION` in exactly one
translation unit before including it (or link the dev build's `src/timui.c`).

## Conventions

- **Naming:** functions `timui_*`; macros / enumerator prefix `TIMUI_*`; structs
  `Timui*`. Opaque handles: `Timui`, `TimuiFrame`.
- **Ownership:** `TimuiStr` is a non-owning `{ptr, len}` view (valid for the
  call). Strings returned by the library (e.g. `timui_error_string`) are static.
- **Threading:** only `timui_post` is thread-safe; everything
  else is UI-thread-only. See [THREADING.md](THREADING.md).
- **Errors:** fallible APIs return `TimuiResult` (`TIMUI_OK` on success);
  stringify with `timui_error_string`.

## Lifecycle

```c
TimuiConfig cfg = TIMUI_CONFIG_INIT;
void        timui_config_init(TimuiConfig *cfg);
TimuiResult timui_open(const TimuiConfig *cfg, Timui **out_ui);
void        timui_close(Timui *ui);
TimuiResult timui_begin_result(Timui *ui, TimuiFrame **out_frame);
bool        timui_begin(Timui *ui, TimuiFrame **out_frame);   /* UI-thread */
void        timui_end(TimuiFrame *frame);
void        timui_quit(Timui *ui);
bool        timui_should_quit(const Timui *ui);
TimuiRect   timui_root(const TimuiFrame *frame);
int         timui_width/height(const TimuiFrame *frame);
TimuiResult timui_ui_resize(Timui *ui, int w, int h);
void        timui_invalidate(Timui *ui);
void        timui_full_redraw(Timui *ui);
TimuiCellBuffer *timui_frame_buffer(TimuiFrame *frame);
TimuiResult timui_term_size_pixels(int fd, int *out_w, int *out_h,
                                   int *out_px_w, int *out_px_h);
```

Initialize `TimuiConfig` with `TIMUI_CONFIG_INIT` or `timui_config_init(&cfg)`.
The initializer fills the ABI guards (`struct_size`, `api_version`), defaults
to file descriptors `0`/`1` (`STDIN_FILENO`/`STDOUT_FILENO`), `AUTO` profile,
the modern dark theme, and `TIMUI_FLAG_RESTORE_ON_EXIT`. `timui_open` rejects
config size/API-version mismatches so stale binaries fail before touching the
terminal.

The input/output descriptors are borrowed. timui never closes them. The POSIX
backend temporarily sets the input descriptor non-blocking and restores its
original flags on `timui_restore_terminal` / `timui_close`; raw mode is entered
only when the input descriptor is a tty, and screen-mode escapes are emitted
only when the output descriptor is a tty.

`timui_open` enters raw mode + screen modes requested by flags, detects
capabilities, and sizes the buffers. `timui_begin_result` ingests input, clears
the frame, and resets the id stack; `timui_begin` is the source-compatible bool
wrapper that returns true only for `TIMUI_OK`. A `TimuiFrame` is valid only
between a successful begin and exactly one `timui_end`.

`timui_begin_result` returns `TIMUI_ERR_INVALID_ARGUMENT` for bad handles,
`TIMUI_ERR_CLOSED` after `timui_quit`, `TIMUI_ERR_EOF` when the POSIX input fd
or a custom transport reports closed input, and `TIMUI_ERR_IO` for runtime
transport read failures. No pending bytes is not an error: real terminal input
is polled briefly and fake/ConPTY transports return a redrawable frame with no
new input. `timui_post` does not interrupt that poll; wakeup latency is bounded
by the next UI-loop iteration and the current 16 ms terminal-input poll.

Live terminal resize is explicit in v0.2: call `timui_term_size(output_fd, &w,
&h)` and then `timui_ui_resize(ui, w, h)` when the dimensions change.

The diff renderer assumes timui owns the terminal between frames. If an external
write only resets terminal state such as SGR, cursor position, or OSC 8 link
state, call `timui_invalidate(ui)` before the next frame. If a subprocess,
shell escape, suspend/resume, terminal reset, or diagnostic print may have
changed visible screen contents, call `timui_full_redraw(ui)`; the next frame
will repaint every cell instead of relying on the previous cell buffer.

### Windows ConPTY

```c
TimuiResult timui_conpty_open(TimuiTransport *out_transport, int *out_pid);
TimuiResult timui_conpty_resize(TimuiTransport *transport, int cols, int rows);
void        timui_conpty_close(TimuiTransport *transport, int pid);
```

On `_WIN32`, `timui_conpty_open` creates a ConPTY session around the default
shell (`COMSPEC`, falling back to `cmd.exe`) and returns a `TimuiTransport`
backed by ConPTY input/output pipes. The ConPTY entry points are resolved at
runtime from `kernel32.dll`; if they are unavailable, open returns
`TIMUI_ERR_UNSUPPORTED`. The transport read path polls pipe availability before
`ReadFile`, writes are chunked to a bounded `DWORD` size, and close is
idempotent. `timui_conpty_resize` validates character-cell dimensions before
calling `ResizePseudoConsole`.

On non-Windows builds, ConPTY APIs return `TIMUI_ERR_UNSUPPORTED` after
clearing output handles. The Win32 path is enforced by the MinGW compile seam in
`make check`; hosted Windows ConPTY smoke evidence is accepted in run
`29226547099` at commit `44bb495b1f7937357f117b42563b50ba08c08e08`.

## Layout

```c
TimuiRect timui_cut_top/bottom/left/right(TimuiRect *r, int n);
TimuiRect timui_inset(TimuiRect r, int n);
TimuiRect timui_pad(TimuiRect r, int l, int t, int rr, int b);
void      timui_split_cols/rows(TimuiRect r, float ratio, TimuiRect *a, TimuiRect *b);
```

## IDs & strings

```c
TimuiId timui_id_from_bytes(const void *data, size_t len);
TimuiId timui_id_from_cstr(const char *s);
TimuiStr timui_str_from_cstr(const char *s);
size_t   timui_str_copy(char *dst, size_t cap, TimuiStr src);   /* snprintf-style */
#define  TIMUI_ID(s)   timui_id_from_cstr(s)
#define  TIMUI_STR_LIT(s) ((TimuiStr){ (s), sizeof(s)-1 })
```

## Widgets (immediate-mode; call between begin/end)

Controlled (return intent; the caller mutates its model) and `_mut`
convenience wrappers:

```c
TimuiButtonResult timui_button(TimuiFrame *, TimuiId, TimuiRect, TimuiStr label);
TimuiBoolEdit     timui_checkbox(...);   bool timui_checkbox_mut(..., bool *value);
TimuiBoolEdit     timui_radio(...);
bool              timui_input_line_buf(..., char *buf, size_t cap);   /* append-only, true on submit */
bool              timui_input_field(..., TimuiInputState *st);        /* in-line cursor editing */
TimuiTextAreaResult timui_text_area_ex(..., TimuiTextAreaState state, uint32_t flags);
TimuiTextAreaResult timui_text_area_mut(..., TimuiTextAreaState *st, uint32_t flags);
void              timui_text_area(..., TimuiTextAreaState *st);       /* compatibility wrapper */
TimuiComboboxResult timui_combobox(..., const TimuiStr *options, int count, TimuiComboboxState state);
TimuiComboboxResult timui_combobox_mut(..., const TimuiStr *options, int count, TimuiComboboxState *state);
TimuiToastResult timui_toasts(..., const TimuiToast *toasts, int count, uint64_t now_ms);
TimuiSplitPaneResult timui_split_pane(..., TimuiAxis axis, TimuiSplitPaneState state);
TimuiSplitPaneResult timui_split_pane_mut(..., TimuiAxis axis, TimuiSplitPaneState *state);
TimuiListResult   timui_listbox(...);    TimuiListResult timui_listbox_mut(..., TimuiListState *);
TimuiTreeResult   timui_tree(...);       TimuiTreeResult timui_tree_mut(..., int *selected);
TimuiTableResult  timui_table(...);      TimuiTableResult timui_table_mut(..., TimuiTableState *);
TimuiRect         timui_panel_begin(...);  void timui_panel_end(...);
void              timui_label(...);  void timui_function_bar(...);
int               timui_message_box(...);  /* clicked button index, or -1 */
```

Results carry `clicked` / `changed` / `hovered` / `focused` / `pressed` as
appropriate. Persistent widget state (list selection, edit cursor) is
caller-owned: `TimuiListState`, `TimuiTreeResult.selected`, `TimuiTableState`,
and — for text editing — `TimuiInputState { char *text; size_t cap; size_t
cursor; int scroll_x; }` / `TimuiTextAreaState`.

`timui_combobox` is a one-focus autocomplete field. The first row edits the
caller-owned query buffer; remaining rows show the filtered popup while open.
Results report `activated` and `selected` as original option indices, with
`match_count` for empty/no-match handling.

`timui_toasts` renders a top-to-bottom stack of 3-row notification cards over
caller-owned data. Toasts carry title, message, severity, creation time, TTL,
and a caller-owned dismissed flag. The widget mutates nothing; it reports the
original array index clicked for dismissal and the count drawn inside the
supplied rectangle.

`timui_split_pane` lays out a resizable two-pane region over an existing
rectangle. `TIMUI_AXIS_H` returns left/divider/right panes; `TIMUI_AXIS_V`
returns top/divider/bottom panes. State is caller-owned (`ratio`, `min_first`,
`min_second`), and `_mut` writes back only while the divider is dragged.

## Capabilities & Images

```c
void timui_caps_detect(TimuiCaps *, const char *term,
                       const char *term_program, const char *colorterm);
int  timui_caps_has(const TimuiCaps *, TimuiCapFlags);
TimuiImageProtocol timui_caps_image_protocol(const TimuiCaps *);
const TimuiCaps *timui_caps(const Timui *);
TimuiImageProtocol timui_image_protocol(const Timui *);
void timui_force_cap(Timui *, TimuiCapFlags, int enable);
void timui_force_image_protocol(Timui *, TimuiImageProtocol);
TimuiImage *timui_image_from_png(Timui *, const void *data, size_t size);
TimuiImage *timui_image_from_rgba(Timui *, const void *rgba,
                                  int w, int h, int stride);
TimuiImage *timui_image_from_png_rgba(Timui *, const void *png, size_t png_size,
                                      const void *rgba, int w, int h, int stride);
void timui_image_draw(TimuiFrame *, TimuiImage *, TimuiRect);
void timui_image_draw_clipped(TimuiFrame *, TimuiImage *,
                              TimuiRect full, TimuiRect visible);
void timui_image_free(Timui *, TimuiImage *);
```

Capability detection is deterministic from `TERM`, `TERM_PROGRAM`, and
`COLORTERM`, with multiplexers conservatively stripping image protocols unless
the application explicitly forces them. Image protocol selection returns
`NONE`, `KITTY`, `SIXEL`, or `ITERM2`; when several image caps are present,
Kitty wins, then Sixel, then iTerm2.

The image API is protocol-neutral at the draw call. v0.2 emits Kitty graphics
and iTerm2 inline images from PNG bytes supplied to `timui_image_from_png` or
`timui_image_from_png_rgba`, and Sixel from raw RGBA pixels supplied to
`timui_image_from_rgba`, the RGBA sidecar supplied to
`timui_image_from_png_rgba`, or a lazily decoded plain PNG. The sidecar
constructor copies the original PNG bytes and caller-decoded RGBA rows so apps
that already decoded pixels can avoid the built-in PNG decoder and control the
exact Sixel source pixels. The Sixel path is intentionally narrow: PNG decode is
PNG-only, no-stdio, bounded by dimension/pixel limits, exact colours are
preserved up to 16 opaque colours, larger raw-RGBA palettes are quantized to a
deterministic 16-colour terminal palette, and alpha below 128 is transparent.
When the terminal reports total pixel dimensions via `TIOCGWINSZ`, Sixel draws
are nearest-neighbor scaled to the requested cell rectangle; otherwise they
preserve source pixel dimensions. `timui_image_draw_clipped` crops Kitty
placements and Sixel source pixels when pixels are available. Malformed or
oversized PNGs, clipped iTerm2 draws, and unsupported protocol/data pairs fall
back to `[img]`. `timui_force_image_protocol` is intended for tests and user
overrides; unknown enum values clear image caps and select `NONE`.

Image resource limits are public macros: `TIMUI_IMAGE_MAX_DIMENSION`,
`TIMUI_IMAGE_MAX_PIXELS`, `TIMUI_IMAGE_PNG_MAX_BYTES`,
`TIMUI_IMAGE_PNG_MAX_DIMENSION`, `TIMUI_IMAGE_PNG_MAX_PIXELS`, and
`TIMUI_IMAGE_PLACEMENT_CAP`. See [IMAGE_LIMITS.md](IMAGE_LIMITS.md) for the
ownership, cache, and protocol lifecycle contract.

Defining `TIMUI_NO_IMAGES` keeps this API available but disables terminal image
protocol emission. `timui_caps_image_protocol` / `timui_image_protocol` return
`NONE`, image caps are stripped even when forced on, `timui_force_image_protocol`
selects `NONE`, and image draws render the `[img]` placeholder.

### Text editing

`timui_input_field` (single line) and `timui_text_area` (multi-line) support
full in-line cursor editing when focused: Left/Right move by whole grapheme
clusters, Home/End jump to the line bounds, Backspace/Delete remove the cluster
before/at the cursor, and typing inserts mid-string. The focused field shows a
hardware cursor at the edit position; `input_field` scrolls horizontally to keep
it visible. `timui_input_line_buf` remains the append-only convenience with no
cursor, but its Backspace also deletes one whole cluster.

`timui_text_area_ex` and `timui_text_area_mut` return
`TimuiTextAreaResult { state, changed, submitted, focused }`. The default
`timui_text_area` wrapper preserves editor semantics: Enter inserts a newline.
Pass `TIMUI_TEXT_AREA_ENTER_SUBMITS` to opt into chat/IRC composer semantics:
plain Enter submits without inserting a newline, Shift+Enter inserts `\n` when
the terminal reports modifiers, and bracketed-paste newlines are preserved as
text rather than treated as submits.

## Styling & themes

```c
TimuiStyle timui_style_make(uint32_t fg, uint32_t bg, uint32_t attrs);
TimuiTheme timui_theme_builtin(TimuiBuiltinTheme);   /* DOS_BLUE/GRAY, MODERN_DARK, MONO */
TimuiStyle timui_theme_style(const TimuiTheme *, TimuiStyleSlot);
TimuiResult timui_stylesheet_parse(TimuiStylesheet *, const char *src, size_t len,
                                   const TimuiAllocator *);
TimuiResolvedStyle timui_stylesheet_resolve(const TimuiStylesheet *, TimuiStyleQuery);
void timui_stylesheet_free(TimuiStylesheet *);
void timui_set_stylesheet(Timui *, const TimuiStylesheet *);   /* borrowed; NULL clears */
```

The stylesheet parser is a small TCSS-inspired layer over existing styles. The
first grammar supports one selector per rule with widget kind, `#id`, `.class`,
and `:focused` / `:hovered` / `:active` / `:disabled` / `:selected` states.
Declarations cover `fg`, `bg`, `bold`, `dim`, `reverse`, `border`, `padding`,
`gap`, `gradient-lo`, and `gradient-hi`. Resolution is deterministic:
id-specific rules beat class/state rules, class/state beat kind, and source
order breaks ties per property.

`timui_set_stylesheet` attaches a parsed stylesheet by borrowing it for later
frames. Built-in widgets resolve their theme slot through the sheet using widget
kind and states such as `:hovered`, `:focused`, `:active`, and `:selected`.
Explicit style arguments, including `timui_label` and
`timui_input_field_styled`, remain hard overrides.

## UTF-8, Widths & Graphemes

```c
int    timui_utf8_decode(const char *s, size_t len, uint32_t *out_cp);
int    timui_utf8_width(uint32_t cp);
size_t timui_grapheme_next(const char *s, size_t len, size_t off);
size_t timui_grapheme_prev(const char *s, size_t len, size_t off);
int    timui_grapheme_width(const char *s, size_t len);
int    timui_display_width(const char *s);
int    timui_fit_cell(const char *s, int width, char *out, size_t cap, int *ellipsis);
```

The scalar UTF-8 helpers decode one codepoint and provide a minimal terminal
cell width. Grapheme helpers walk byte offsets across common extended clusters
that terminal UI code must not split: combining marks, variation selectors,
skin-tone modifiers, regional-indicator flags, CRLF, and ZWJ emoji sequences.
See [UNICODE.md](UNICODE.md) for the ownership boundary between timui and the
terminal/font stack.

`timui_display_width` and `timui_fit_cell` use those cluster helpers, so table
and grid truncation keep clusters intact before appending an ellipsis.

## Events & input (usually consumed by widgets)

```c
int timui_poll_event(Timui *, TimuiEvent *out);   /* UI-thread; begin drains most */
int timui_key_pressed(TimuiFrame *, TimuiKey);    /* last key this frame */
```

Raw events (`TimuiEvent` key/text/mouse/paste/focus) are produced by the
incremental input parser; `timui_begin` routes them into the interaction state.
Apps usually react through widget results + `timui_key_pressed`.

## Threads

```c
TimuiResult timui_post_result(Timui *, uint32_t type,
                              const void *data, size_t size);  /* any thread */
bool        timui_post(Timui *, uint32_t type,
                       const void *data, size_t size);         /* bool wrapper */
TimuiResult timui_emit_result(TimuiFrame *, uint32_t type,
                              const void *data, size_t size);  /* UI thread */
bool        timui_emit(TimuiFrame *, uint32_t type,
                       const void *data, size_t size);         /* bool wrapper */
```

`timui_post_result` and `timui_emit_result` copy the payload before returning.
The caller retains ownership of `data`. A non-zero `size` requires non-NULL
`data`; zero-size messages are allowed with `data == NULL`. Queue entries are
FIFO in successful post order; concurrent producers are ordered by mutex
acquisition. Allocation failure returns `TIMUI_ERR_OUT_OF_MEMORY`; bad
arguments return `TIMUI_ERR_INVALID_ARGUMENT`. Producers must stop before
`timui_close`; posting to a destroyed queue is outside the API contract.

## Feature macros

`TIMUI_IMPLEMENTATION`, `TIMUI_NO_THREADS`, `TIMUI_NO_IMAGES`, and `TIMUI_API`
are implemented. `TIMUI_NO_IMAGES` preserves the public image API while
disabling image protocol caps and escapes. `TIMUI_NO_STDIO` and
`TIMUI_NO_UTF8_TABLES` are reserved compatibility no-ops. Single-header users
link `-pthread` (or define `TIMUI_NO_THREADS`).

---
This is a concise reference; see [DESIGN.md](DESIGN.md) for architecture and
the header comments for full per-function detail.
