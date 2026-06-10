# API reference — timui.h

Public surface of the timui.h single-header C99 immediate-mode TUI. Include
`timui.h` for the declarations; define `TIMUI_IMPLEMENTATION` in exactly one
translation unit before including it (or link the dev build's `src/timui.c`).

## Conventions

- **Naming:** functions `timui_*`; macros / enumerator prefix `TIMUI_*`; structs
  `Timui*`. Opaque handles: `Timui`, `TimuiFrame`.
- **Ownership:** `TimuiStr` is a non-owning `{ptr, len}` view (valid for the
  call). Strings returned by the library (e.g. `timui_error_string`) are static.
- **Threading:** only `timui_post` / `timui_wakeup` are thread-safe; everything
  else is UI-thread-only. See [THREADING.md](THREADING.md).
- **Errors:** fallible APIs return `TimuiResult` (`TIMUI_OK` on success);
  stringify with `timui_error_string`.

## Lifecycle

```c
TimuiResult timui_open(const TimuiConfig *cfg, Timui **out_ui);
void        timui_close(Timui *ui);
bool        timui_begin(Timui *ui, TimuiFrame **out_frame);   /* UI-thread */
void        timui_end(TimuiFrame *frame);
void        timui_quit(Timui *ui);
bool        timui_should_quit(const Timui *ui);
TimuiRect   timui_root(const TimuiFrame *frame);
int         timui_width/height(const TimuiFrame *frame);
void        timui_ui_resize(Timui *ui, int w, int h);
TimuiCellBuffer *timui_frame_buffer(TimuiFrame *frame);
```

`timui_open` enters raw mode + alternate screen (if a tty), detects
capabilities, and sizes the buffers. `timui_begin` ingests input, clears the
frame, and resets the id stack; `timui_end` diff-renders and swaps. A `TimuiFrame`
is valid only between `begin` and `end`.

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
bool              timui_input_line_buf(..., char *buf, size_t cap);   /* true on submit */
TimuiListResult   timui_listbox(...);    TimuiListResult timui_listbox_mut(..., TimuiListState *);
TimuiRect         timui_panel_begin(...);  void timui_panel_end(...);
void              timui_label(...);  void timui_function_bar(...);
int               timui_message_box(...);  /* clicked button index, or -1 */
```

Results carry `clicked` / `changed` / `hovered` / `focused` / `pressed` as
appropriate. Persistent widget state (list selection, input cursor) is
caller-owned (`TimuiListState`, the `char *buf`).

## Styling & themes

```c
TimuiStyle timui_style_make(uint32_t fg, uint32_t bg, uint32_t attrs);
TimuiTheme timui_theme_builtin(TimuiBuiltinTheme);   /* DOS_BLUE/GRAY, MODERN_DARK, MONO */
TimuiStyle timui_theme_style(const TimuiTheme *, TimuiStyleSlot);
```

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
bool timui_post(Timui *, uint32_t type, const void *data, size_t size);  /* any thread */
void timui_wakeup(Timui *);
```

## Feature macros

`TIMUI_IMPLEMENTATION`, `TIMUI_NO_STDIO`, `TIMUI_NO_THREADS`, `TIMUI_NO_IMAGES`,
`TIMUI_NO_UTF8_TABLES`, `TIMUI_API`. Single-header users link `-pthread` (or
define `TIMUI_NO_THREADS`).

---
This is a concise reference; see [DESIGN.md](DESIGN.md) for architecture and
the header comments for full per-function detail.
