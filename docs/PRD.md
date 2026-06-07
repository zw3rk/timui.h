# `timui.h` — Product Requirements Document and Technical Specification

**Version:** Draft v0.1  
**Date:** 2026-07-02  
**Project name:** `timui.h`  
**Expanded name:** Terminal Immediate Mode UI  
**Tagline:** Single-header C99 immediate-mode TUI for Ghostty, kitty, and modern terminals.

---

## Table of contents

1. Product brief
2. Motivation and rationale
3. Product requirements
4. Design principles
5. Public API specification
6. Events and messages
7. Functional app model
8. Thread safety and immutability
9. Terminal capability model
10. Rendering architecture
11. Styling and themes
12. Example: Mini Commander skeleton
13. Repository layout
14. Dependencies
15. Implementation roadmap for an agent/swarm
16. Parallelization map
17. MVP definition of done
18. Risks and mitigations
19. Open design questions
20. Product positioning
21. References

---

# 1. Product brief

## 1.1 Name

```text
timui.h
```

Pronunciation: “tim-you-eye” or “timmy.”

Expanded meaning:

```text
Terminal Immediate Mode UI
```

Possible taglines:

```text
timui.h — immediate-mode terminal UI for modern emulators
```

```text
timui.h — single-header C99 immediate-mode TUI for Ghostty, kitty, and modern terminals
```

## 1.2 Product thesis

C should have a small, pleasant, modern TUI framework that can recreate the feel of:

```text
QuickBasic
Turbo Pascal
Turbo Vision
Norton Commander
Midnight Commander
Borland-style dialogs
DOS-era blue/gray text interfaces
```

but target modern terminal emulators instead of pretending every terminal is a lowest-common-denominator VT100.

Modern terminals support capabilities that make this much more attractive than it used to be:

```text
better keyboard protocols
synchronized output
truecolor
mouse tracking
bracketed paste
hyperlinks
inline graphics in some terminals
```

So `timui.h` should not be “ncurses with a nicer API.” It should be:

```text
a modern terminal UI runtime with an immediate-mode C API
```

## 1.3 Primary users

Primary users:

```text
C programmers
systems programmers
tool authors
single-binary CLI authors
embedded-ish Linux tool authors
people who miss DOS/Borland-style TUIs
people who dislike ncurses APIs
people who want no Rust/Go/Node/Python runtime
```

Secondary users:

```text
C++ users who want a C ABI
Zig/Odin/Nim/D/Rust FFI users
developers building terminal dashboards
developers building file managers, debuggers, database browsers, git tools, log viewers, agent UIs
```

## 1.4 Core promise

A user should be able to build a polished two-pane file-manager skeleton in under 500 lines of C.

The API should make this kind of program feel natural:

```c
while (timui_begin(ui, &f)) {
    TimuiRect root = timui_root(f);

    TimuiRect menu = timui_cut_top(&root, 1);
    TimuiRect keys = timui_cut_bottom(&root, 1);
    TimuiRect cmd  = timui_cut_bottom(&root, 1);

    timui_menu_bar_begin(f, menu);
    /* menus... */
    timui_menu_bar_end(f);

    timui_input_line_buf(f, TIMUI_ID("cmd"), cmd, command, sizeof command);

    TimuiRect left, right;
    timui_split_cols(root, 0.5f, &left, &right);

    timui_panel_begin(f, TIMUI_ID("left"), left, TIMUI_STR_LIT("Left"), TIMUI_BORDER_DOUBLE);
    timui_listbox_mut(f, TIMUI_ID("left-files"), timui_panel_body(f),
                      &left_state, file_count, file_label, left_files);
    timui_panel_end(f);

    timui_panel_begin(f, TIMUI_ID("right"), right, TIMUI_STR_LIT("Right"), TIMUI_BORDER_DOUBLE);
    timui_listbox_mut(f, TIMUI_ID("right-files"), timui_panel_body(f),
                      &right_state, file_count, file_label, right_files);
    timui_panel_end(f);

    timui_function_bar(f, keys,
        TIMUI_STR_LIT("1 Help  2 Menu  3 View  4 Edit  5 Copy  6 Move  7 Mkdir  8 Delete  10 Quit"));

    timui_end(f);
}
```

---

# 2. Motivation and rationale

## 2.1 The gap

C already has terminal rendering libraries and widget libraries, but most fall into one of these categories:

```text
too low-level: raw ANSI, termios, termbox-style cell APIs
too old-feeling: curses/ncurses mental model
too retained/object-oriented: fake C++ in C
too big: advanced renderer but not a simple app framework
too language-specific: Rust/Go/JS/Python frameworks
```

`termbox2` is a useful reference point: it is a slim alternative to ncurses, has a tighter API, no dependencies beyond libc, a single-header organization, stricter escape parsing, optional 32-bit color, extended grapheme-cluster support, and a test suite. But it intentionally lives at the terminal I/O/cell layer. `timui.h` should operate one layer higher:

```text
layout
focus
widgets
style
frame lifecycle
app structure
```

## 2.2 Why immediate mode?

A retained widget tree in C quickly becomes unpleasant:

```c
Widget *w = button_new(...);
widget_set_callback(w, ...);
container_add(parent, w);
```

That style creates hard questions in C:

```text
Who owns widgets?
Who frees children?
How do callbacks capture state?
How do custom widgets inherit behavior?
How does async work avoid use-after-free?
How do we avoid fake virtual classes?
```

Immediate mode avoids most of that. The user writes the UI every frame:

```c
if (timui_button(f, TIMUI_ID("save"), r, TIMUI_STR_LIT("Save")).clicked) {
    save();
}
```

The framework internally remembers only the unavoidable interaction state:

```text
focus
hot item
active item
scroll offsets
text cursor
selection
menu stack
modal stack
previous frame buffer
terminal capabilities
```

That is much more C-friendly.

## 2.3 Why functional/immutable by default?

Traditional immediate-mode C APIs often mutate caller state directly:

```c
timui_checkbox_mut(f, id, r, TIMUI_STR_LIT("Enabled"), &enabled);
```

That is convenient, but it makes complex apps harder to reason about.

`timui.h` should support that style, but the **default documented style** should be functional/controlled:

```c
TimuiBoolEdit edit = timui_checkbox(f, id, r, TIMUI_STR_LIT("Enabled"), model->enabled);

if (edit.changed) {
    AppMsg msg = { .type = APP_SET_ENABLED, .enabled = edit.value };
    timui_emit(f, APP_SET_ENABLED, &msg, sizeof msg);
}
```

Then the app updates its model through an explicit update function:

```c
static AppModel app_update(AppModel model, AppMsg msg) {
    switch (msg.type) {
        case APP_SET_ENABLED:
            model.enabled = msg.enabled;
            return model;

        default:
            return model;
    }
}
```

This gives C a simple version of:

```text
model → view → message → update → new model
```

The framework internals can be mutable for performance, but application data should be immutable by convention and `const` by API.

---

# 3. Product requirements

## 3.1 Must-have MVP requirements

`timui.h` v0.1 must provide:

```text
single-header usage mode
pure C implementation
C99 baseline
no ncurses dependency
no required external runtime
POSIX terminal backend
raw mode setup/restore
alternate screen support
UTF-8 input/output
truecolor with fallback
16-color/256-color fallback
cell-buffer renderer
diff-based frame presentation
synchronized output support where safe
SGR mouse support
bracketed paste support
resize events
keyboard events
Kitty keyboard protocol support where available
rect-split layout helpers
style/theme system
DOS-blue theme
DOS-gray theme
ASCII border fallback
Unicode border glyphs
panel widget
button widget
checkbox widget
radio widget
input-line widget
listbox widget
menu-bar widget
modal/message-box widget
status/function-key bar widget
thread-safe message posting
functional controlled-widget API
mutable convenience wrappers
examples
tests
```

## 3.2 Should-have v0.2 requirements

```text
OSC 8 hyperlinks
focus-in/focus-out events
clipboard integration via optional OSC 52
Kitty graphics protocol image placement
scroll views
table widget
tree widget
text-area widget
command palette widget
theme light/dark adaptation
configurable keymaps
snapshot testing harness
pty integration tests
Windows ConPTY backend
```

Kitty-style graphics support would make image preview panes possible for file managers, markdown viewers, package browsers, git tools, database tools, and agent UIs. This should be designed for, but not required in v0.1.

## 3.3 Non-goals

`timui.h` should not try to be:

```text
a GUI toolkit
a browser renderer
a CSS engine
a React clone
a full terminal emulator
a replacement for shell readline
a complete text editor engine in v0.1
a curses compatibility layer
a terminal multiplexer
a dependency-heavy framework
```

It should also not promise perfect behavior in every historical terminal. The main target is:

```text
modern terminal first
safe fallback second
ancient terminal compatibility third
```

---

# 4. Design principles

## 4.1 Single-header first, split-build optional

Primary usage:

```c
#define TIMUI_IMPLEMENTATION
#include "timui.h"
```

Optional split build:

```text
timui.h
timui.c
```

Optional internal split during development:

```text
src/timui_core.c
src/timui_term.c
src/timui_render.c
src/timui_widgets.c
```

Releases should be able to produce a single amalgamated header.

## 4.2 No hidden global mutable state

Bad:

```c
static TimuiGlobalState g_timui;
```

Good:

```c
Timui *ui = NULL;
timui_open(&cfg, &ui);
```

Rules:

```text
All runtime state lives in Timui.
All frame state lives in TimuiFrame.
All app state belongs to the caller.
No process-global singleton.
No global allocator.
No global theme.
No global terminal mode state except unavoidable OS process terminal state, tracked per Timui.
```

## 4.3 Single writer, many producers

The terminal should be written from one thread only.

Thread safety should mean:

```text
background threads can post messages safely
background threads can request wakeup safely
the UI thread owns rendering
the UI thread owns TimuiFrame
the UI thread owns terminal writes
```

The safe threading model is:

```text
worker thread(s)
    ↓ timui_post()
thread-safe message queue
    ↓
UI thread
    ↓ update model
    ↓ render frame
    ↓ terminal write
```

## 4.4 Functional by default, mutable convenience available

Default APIs should not mutate app data:

```c
TimuiBoolEdit timui_checkbox(
    TimuiFrame *f,
    TimuiId id,
    TimuiRect r,
    TimuiStr label,
    bool value
);
```

Convenience APIs may mutate:

```c
bool timui_checkbox_mut(
    TimuiFrame *f,
    TimuiId id,
    TimuiRect r,
    TimuiStr label,
    bool *value
);
```

Naming rule:

```text
No suffix       controlled/functional
_mut suffix     mutates caller-owned data
```

## 4.5 Boring C

Use:

```text
C99
stdint.h
stdbool.h
stddef.h
string.h
errno.h
```

Avoid requiring:

```text
C++
exceptions
RTTI
compiler-specific extensions
libuv
ncurses
OpenSSL
JSON libraries
image decoders
package managers
```

Optional features may use:

```text
pthreads
C11 atomics
termios
poll/select
ioctl(TIOCGWINSZ)
Win32 console APIs later
```

## 4.6 Capability tiers

`timui.h` should support capability tiers.

```c
typedef enum {
    TIMUI_PROFILE_SAFE = 0,
    TIMUI_PROFILE_MODERN,
    TIMUI_PROFILE_KITTY_FAMILY,
    TIMUI_PROFILE_AUTO
} TimuiProfile;
```

Meaning:

```text
SAFE:
    alternate screen optional
    ANSI cursor movement
    16 colors
    ASCII fallback
    legacy keyboard parsing

MODERN:
    UTF-8
    truecolor
    SGR mouse
    bracketed paste
    focus events
    synchronized output
    OSC 8 hyperlinks

KITTY_FAMILY:
    Kitty keyboard protocol
    Kitty graphics protocol
    richer key modifiers
    repeat/release events where available
    image placement where available

AUTO:
    detect and select the best safe profile
```

---

# 5. Public API specification

## 5.1 Header structure

```c
#ifndef TIMUI_H
#define TIMUI_H

#ifdef __cplusplus
extern "C" {
#endif

/* public declarations */

#ifdef __cplusplus
}
#endif

#endif /* TIMUI_H */

#ifdef TIMUI_IMPLEMENTATION
/* implementation */
#endif
```

Feature macros:

```c
#define TIMUI_IMPLEMENTATION      /* include implementation */
#define TIMUI_NO_STDIO            /* avoid stdio helpers */
#define TIMUI_NO_THREADS          /* disable thread-safe post/task API */
#define TIMUI_NO_IMAGES           /* omit image protocol helpers */
#define TIMUI_NO_UTF8_TABLES      /* ASCII-only/minimal Unicode build */
#define TIMUI_STATIC              /* static function definitions if desired */
#define TIMUI_API                 /* override symbol visibility */
```

Version macros:

```c
#define TIMUI_VERSION_MAJOR 0
#define TIMUI_VERSION_MINOR 1
#define TIMUI_VERSION_PATCH 0
#define TIMUI_VERSION_STRING "0.1.0"
```

## 5.2 Basic types

```c
typedef struct Timui Timui;
typedef struct TimuiFrame TimuiFrame;

typedef uint64_t TimuiId;

typedef struct {
    const char *ptr;
    size_t len;
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
    void *userdata;
    void *(*alloc)(void *userdata, size_t size);
    void *(*realloc)(void *userdata, void *ptr, size_t old_size, size_t new_size);
    void  (*free)(void *userdata, void *ptr, size_t size);
} TimuiAllocator;
```

Convenience macros:

```c
#define TIMUI_STR_LIT(s) ((TimuiStr){ (s), sizeof(s) - 1 })
#define TIMUI_RECT(x,y,w,h) ((TimuiRect){ (x), (y), (w), (h) })
#define TIMUI_ID(s) timui_id_from_cstr((s))
```

## 5.3 Configuration

```c
typedef enum {
    TIMUI_THEME_DOS_BLUE = 0,
    TIMUI_THEME_DOS_GRAY,
    TIMUI_THEME_MODERN_DARK,
    TIMUI_THEME_MONO
} TimuiBuiltinTheme;

typedef enum {
    TIMUI_PROFILE_SAFE = 0,
    TIMUI_PROFILE_MODERN,
    TIMUI_PROFILE_KITTY_FAMILY,
    TIMUI_PROFILE_AUTO
} TimuiProfile;

typedef enum {
    TIMUI_FLAG_ALT_SCREEN       = 1u << 0,
    TIMUI_FLAG_MOUSE            = 1u << 1,
    TIMUI_FLAG_BRACKETED_PASTE  = 1u << 2,
    TIMUI_FLAG_TRUECOLOR        = 1u << 3,
    TIMUI_FLAG_SYNC_OUTPUT      = 1u << 4,
    TIMUI_FLAG_KITTY_KEYBOARD   = 1u << 5,
    TIMUI_FLAG_FOCUS_EVENTS     = 1u << 6,
    TIMUI_FLAG_HIDE_CURSOR      = 1u << 7,
    TIMUI_FLAG_RESTORE_ON_EXIT  = 1u << 8
} TimuiFlags;

typedef struct {
    const char *title;

    int input_fd;
    int output_fd;

    TimuiProfile profile;
    uint32_t flags;

    TimuiBuiltinTheme theme;

    TimuiAllocator allocator;

    size_t frame_arena_bytes;
    size_t persistent_state_bytes;
    size_t message_queue_bytes;

    void *userdata;
} TimuiConfig;
```

Open/close:

```c
typedef enum {
    TIMUI_OK = 0,
    TIMUI_ERR_INVALID_ARGUMENT,
    TIMUI_ERR_OUT_OF_MEMORY,
    TIMUI_ERR_NOT_A_TTY,
    TIMUI_ERR_OS,
    TIMUI_ERR_UNSUPPORTED,
    TIMUI_ERR_PROTOCOL
} TimuiResult;

TimuiResult timui_open(const TimuiConfig *cfg, Timui **out_ui);
void        timui_close(Timui *ui);

const char *timui_error_string(TimuiResult result);
```

## 5.4 Frame lifecycle

```c
bool timui_begin(Timui *ui, TimuiFrame **out_frame);
void timui_end(TimuiFrame *frame);

void timui_quit(Timui *ui);
bool timui_should_quit(const Timui *ui);

TimuiRect timui_root(const TimuiFrame *frame);
int       timui_width(const TimuiFrame *frame);
int       timui_height(const TimuiFrame *frame);
```

Frame rules:

```text
Only one active frame may exist per Timui.
TimuiFrame is valid only between timui_begin and timui_end.
Widget return data is valid until timui_end unless documented otherwise.
TimuiFrame APIs are UI-thread-only.
```

## 5.5 IDs

```c
TimuiId timui_id_from_bytes(const void *data, size_t len);
TimuiId timui_id_from_cstr(const char *str);

void timui_push_id(TimuiFrame *f, TimuiId id);
void timui_push_id_cstr(TimuiFrame *f, const char *str);
void timui_pop_id(TimuiFrame *f);
```

Rules:

```text
IDs must be stable across frames.
String IDs are hashed with the current ID stack.
Use visible labels for text and hidden IDs for identity when needed.
```

Example:

```c
timui_button(f, TIMUI_ID("save-button"), r, TIMUI_STR_LIT("Save"));
```

## 5.6 Layout

Rect-split layout is the base API.

```c
TimuiRect timui_cut_top(TimuiRect *r, int h);
TimuiRect timui_cut_bottom(TimuiRect *r, int h);
TimuiRect timui_cut_left(TimuiRect *r, int w);
TimuiRect timui_cut_right(TimuiRect *r, int w);

TimuiRect timui_inset(TimuiRect r, int n);
TimuiRect timui_pad(TimuiRect r, int l, int t, int rr, int b);

void timui_split_cols(TimuiRect r, float ratio, TimuiRect *a, TimuiRect *b);
void timui_split_rows(TimuiRect r, float ratio, TimuiRect *a, TimuiRect *b);
```

Optional stack layout:

```c
typedef struct TimuiLayout TimuiLayout;

void      timui_layout_begin_col(TimuiFrame *f, TimuiRect r, int gap);
void      timui_layout_begin_row(TimuiFrame *f, TimuiRect r, int gap);
TimuiRect timui_layout_next(TimuiFrame *f, int size);
TimuiRect timui_layout_flex(TimuiFrame *f);
void      timui_layout_end(TimuiFrame *f);
```

## 5.7 Drawing primitives

```c
void timui_fill(TimuiFrame *f, TimuiRect r, TimuiStyle style);

void timui_text(
    TimuiFrame *f,
    int x,
    int y,
    TimuiStr text,
    TimuiStyle style
);

void timui_text_clipped(
    TimuiFrame *f,
    TimuiRect r,
    TimuiStr text,
    TimuiStyle style
);

void timui_box(
    TimuiFrame *f,
    TimuiRect r,
    TimuiStr title,
    uint32_t border_flags,
    TimuiStyle style
);

void timui_hline(TimuiFrame *f, int x, int y, int w, TimuiStyle style);
void timui_vline(TimuiFrame *f, int x, int y, int h, TimuiStyle style);
```

Border flags:

```c
typedef enum {
    TIMUI_BORDER_NONE   = 0,
    TIMUI_BORDER_SINGLE = 1u << 0,
    TIMUI_BORDER_DOUBLE = 1u << 1,
    TIMUI_BORDER_ROUND  = 1u << 2,
    TIMUI_BORDER_ASCII  = 1u << 3,
    TIMUI_BORDER_SHADOW = 1u << 4
} TimuiBorderFlags;
```

## 5.8 Widgets: controlled default API

### Button

```c
typedef struct {
    bool clicked;
    bool pressed;
    bool hovered;
    bool focused;
} TimuiButtonResult;

TimuiButtonResult timui_button(
    TimuiFrame *f,
    TimuiId id,
    TimuiRect r,
    TimuiStr label
);
```

### Checkbox

```c
typedef struct {
    bool changed;
    bool value;
    bool hovered;
    bool focused;
} TimuiBoolEdit;

TimuiBoolEdit timui_checkbox(
    TimuiFrame *f,
    TimuiId id,
    TimuiRect r,
    TimuiStr label,
    bool value
);

bool timui_checkbox_mut(
    TimuiFrame *f,
    TimuiId id,
    TimuiRect r,
    TimuiStr label,
    bool *value
);
```

### Radio

```c
TimuiBoolEdit timui_radio(
    TimuiFrame *f,
    TimuiId id,
    TimuiRect r,
    TimuiStr label,
    bool selected
);
```

### Input line

Functional version:

```c
typedef enum {
    TIMUI_TEXT_NONE = 0,
    TIMUI_TEXT_INSERT,
    TIMUI_TEXT_DELETE_RANGE,
    TIMUI_TEXT_REPLACE_RANGE,
    TIMUI_TEXT_SUBMIT
} TimuiTextEditKind;

typedef struct {
    TimuiTextEditKind kind;
    size_t start;
    size_t end;
    TimuiStr insert;
} TimuiTextEdit;

typedef struct {
    bool changed;
    bool submitted;
    bool focused;
    TimuiTextEdit edit;
} TimuiInputResult;

typedef struct {
    size_t cursor;
    size_t selection_start;
    size_t selection_end;
    int scroll_x;
} TimuiInputState;

TimuiInputResult timui_input_line(
    TimuiFrame *f,
    TimuiId id,
    TimuiRect r,
    TimuiStr value,
    TimuiInputState state
);
```

Mutable convenience version:

```c
bool timui_input_line_buf(
    TimuiFrame *f,
    TimuiId id,
    TimuiRect r,
    char *buf,
    size_t cap
);
```

### Listbox

Controlled version:

```c
typedef struct {
    int selected;
    int scroll;
} TimuiListState;

typedef const char *(*TimuiLabelFn)(void *userdata, int index);

typedef struct {
    bool state_changed;
    bool activated;
    bool focused;
    TimuiListState state;
    int selected;
} TimuiListResult;

TimuiListResult timui_listbox(
    TimuiFrame *f,
    TimuiId id,
    TimuiRect r,
    TimuiListState state,
    int count,
    TimuiLabelFn label,
    void *userdata
);
```

Mutable convenience version:

```c
TimuiListResult timui_listbox_mut(
    TimuiFrame *f,
    TimuiId id,
    TimuiRect r,
    TimuiListState *state,
    int count,
    TimuiLabelFn label,
    void *userdata
);
```

### Panels

```c
bool      timui_panel_begin(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr title, uint32_t flags);
TimuiRect timui_panel_body(TimuiFrame *f);
void      timui_panel_end(TimuiFrame *f);
```

### Menus

```c
void timui_menu_bar_begin(TimuiFrame *f, TimuiRect r);
bool timui_menu_begin(TimuiFrame *f, TimuiId id, TimuiStr label);
bool timui_menu_item(TimuiFrame *f, TimuiId id, TimuiStr label, TimuiStr shortcut);
void timui_menu_end(TimuiFrame *f);
void timui_menu_bar_end(TimuiFrame *f);
```

### Modal/message box

```c
typedef enum {
    TIMUI_DIALOG_NONE = 0,
    TIMUI_DIALOG_OK,
    TIMUI_DIALOG_CANCEL,
    TIMUI_DIALOG_YES,
    TIMUI_DIALOG_NO
} TimuiDialogResult;

bool timui_modal_begin(TimuiFrame *f, TimuiId id, TimuiRect r, TimuiStr title);
void timui_modal_end(TimuiFrame *f);

TimuiDialogResult timui_message_box(
    TimuiFrame *f,
    TimuiId id,
    TimuiStr title,
    TimuiStr message,
    const TimuiStr *buttons,
    int button_count
);
```

### Function-key bar

```c
void timui_function_bar(
    TimuiFrame *f,
    TimuiRect r,
    TimuiStr text
);
```

---

# 6. Events and messages

## 6.1 Raw events

```c
typedef enum {
    TIMUI_EVENT_NONE = 0,
    TIMUI_EVENT_KEY,
    TIMUI_EVENT_TEXT,
    TIMUI_EVENT_MOUSE,
    TIMUI_EVENT_PASTE,
    TIMUI_EVENT_RESIZE,
    TIMUI_EVENT_FOCUS,
    TIMUI_EVENT_TIMER,
    TIMUI_EVENT_USER
} TimuiEventKind;
```

Keys:

```c
typedef enum {
    TIMUI_KEY_UNKNOWN = 0,

    TIMUI_KEY_ESCAPE,
    TIMUI_KEY_ENTER,
    TIMUI_KEY_TAB,
    TIMUI_KEY_BACKSPACE,
    TIMUI_KEY_DELETE,
    TIMUI_KEY_INSERT,

    TIMUI_KEY_UP,
    TIMUI_KEY_DOWN,
    TIMUI_KEY_LEFT,
    TIMUI_KEY_RIGHT,
    TIMUI_KEY_HOME,
    TIMUI_KEY_END,
    TIMUI_KEY_PAGE_UP,
    TIMUI_KEY_PAGE_DOWN,

    TIMUI_KEY_F1,
    TIMUI_KEY_F2,
    TIMUI_KEY_F3,
    TIMUI_KEY_F4,
    TIMUI_KEY_F5,
    TIMUI_KEY_F6,
    TIMUI_KEY_F7,
    TIMUI_KEY_F8,
    TIMUI_KEY_F9,
    TIMUI_KEY_F10,
    TIMUI_KEY_F11,
    TIMUI_KEY_F12
} TimuiKey;
```

Modifiers:

```c
typedef enum {
    TIMUI_MOD_NONE  = 0,
    TIMUI_MOD_SHIFT = 1u << 0,
    TIMUI_MOD_ALT   = 1u << 1,
    TIMUI_MOD_CTRL  = 1u << 2,
    TIMUI_MOD_SUPER = 1u << 3,
    TIMUI_MOD_HYPER = 1u << 4,
    TIMUI_MOD_META  = 1u << 5
} TimuiMods;
```

Key action:

```c
typedef enum {
    TIMUI_KEY_PRESS = 0,
    TIMUI_KEY_REPEAT,
    TIMUI_KEY_RELEASE
} TimuiKeyAction;
```

Event struct:

```c
typedef struct {
    TimuiEventKind kind;

    union {
        struct {
            TimuiKey key;
            uint32_t codepoint;
            uint32_t mods;
            TimuiKeyAction action;
        } key;

        struct {
            TimuiStr text;
        } text;

        struct {
            int x;
            int y;
            int button;
            int wheel_x;
            int wheel_y;
            uint32_t mods;
            bool pressed;
            bool released;
            bool motion;
        } mouse;

        struct {
            TimuiStr text;
        } paste;

        struct {
            int width;
            int height;
        } resize;

        struct {
            bool focused;
        } focus;

        struct {
            uint32_t type;
            const void *data;
            size_t size;
        } user;
    } as;
} TimuiEvent;
```

Polling events directly should be possible:

```c
bool timui_poll_event(Timui *ui, TimuiEvent *out_event);
```

Normal apps should usually use widgets and app messages rather than raw events.

## 6.2 App messages

Functional apps need message passing.

```c
typedef struct {
    uint32_t type;
    const void *data;
    size_t size;
} TimuiMsgView;

bool timui_emit(TimuiFrame *f, uint32_t type, const void *data, size_t size);
bool timui_recv(Timui *ui, uint32_t *out_type, void *out_buf, size_t *inout_size);
```

Thread-safe posting:

```c
bool timui_post(Timui *ui, uint32_t type, const void *data, size_t size);
void timui_wakeup(Timui *ui);
```

Rules:

```text
timui_emit is UI-thread-only and used during view construction.
timui_post is thread-safe when TIMUI_NO_THREADS is not defined.
timui_post copies message bytes into an internal queue.
timui_recv is UI-thread-only.
Large payloads should be sent by handle/path/index, not copied directly.
```

---

# 7. Functional app model

`timui.h` should not force a framework runner, but it should include a small optional app-loop helper.

## 7.1 Manual loop

```c
typedef enum {
    APP_INC = 1,
    APP_DEC,
    APP_QUIT
} AppMsgType;

typedef struct {
    AppMsgType type;
} AppMsg;

typedef struct {
    int count;
} AppModel;

static AppModel update(AppModel m, AppMsg msg) {
    switch (msg.type) {
        case APP_INC:  m.count++; break;
        case APP_DEC:  m.count--; break;
        case APP_QUIT: break;
    }
    return m;
}

static void view(TimuiFrame *f, const AppModel *m) {
    TimuiRect root = timui_root(f);
    TimuiRect top = timui_cut_top(&root, 3);

    char buf[64];
    snprintf(buf, sizeof buf, "Count: %d", m->count);

    timui_text(f, top.x + 2, top.y + 1,
               (TimuiStr){ buf, strlen(buf) },
               timui_style(f, TIMUI_SLOT_TEXT));

    TimuiRect row = timui_cut_top(&root, 3);
    TimuiRect dec = timui_cut_left(&row, 12);
    TimuiRect inc = timui_cut_left(&row, 12);

    if (timui_button(f, TIMUI_ID("dec"), dec, TIMUI_STR_LIT("-")).clicked) {
        AppMsg msg = { APP_DEC };
        timui_emit(f, APP_DEC, &msg, sizeof msg);
    }

    if (timui_button(f, TIMUI_ID("inc"), inc, TIMUI_STR_LIT("+")).clicked) {
        AppMsg msg = { APP_INC };
        timui_emit(f, APP_INC, &msg, sizeof msg);
    }
}

int main(void) {
    Timui *ui = NULL;

    TimuiConfig cfg = {
        .title = "Counter",
        .input_fd = 0,
        .output_fd = 1,
        .profile = TIMUI_PROFILE_AUTO,
        .flags = TIMUI_FLAG_ALT_SCREEN |
                 TIMUI_FLAG_MOUSE |
                 TIMUI_FLAG_BRACKETED_PASTE |
                 TIMUI_FLAG_TRUECOLOR |
                 TIMUI_FLAG_SYNC_OUTPUT |
                 TIMUI_FLAG_KITTY_KEYBOARD |
                 TIMUI_FLAG_RESTORE_ON_EXIT,
        .theme = TIMUI_THEME_DOS_BLUE
    };

    if (timui_open(&cfg, &ui) != TIMUI_OK) {
        return 1;
    }

    AppModel model = {0};

    while (!timui_should_quit(ui)) {
        uint32_t type;
        AppMsg msg;
        size_t size = sizeof msg;

        while (timui_recv(ui, &type, &msg, &size)) {
            model = update(model, msg);
            size = sizeof msg;
        }

        TimuiFrame *f = NULL;
        if (!timui_begin(ui, &f)) {
            break;
        }

        view(f, &model);

        if (timui_key_pressed(f, TIMUI_KEY_ESCAPE, TIMUI_MOD_NONE)) {
            timui_quit(ui);
        }

        timui_end(f);
    }

    timui_close(ui);
    return 0;
}
```

## 7.2 Optional app runner

Optional helper:

```c
typedef void (*TimuiViewFn)(TimuiFrame *f, const void *model);
typedef void (*TimuiUpdateFn)(void *model, uint32_t msg_type, const void *msg, size_t msg_size);

typedef struct {
    void *model;
    TimuiViewFn view;
    TimuiUpdateFn update;
} TimuiApp;

int timui_run(const TimuiConfig *cfg, TimuiApp *app);
```

This runner may still mutate `model` in place because C cannot enforce true immutability ergonomically. The default examples should still treat `view` as:

```c
view(frame, const_model)
```

and treat `update` as the only place where model changes happen.

---

# 8. Thread safety and immutability specification

## 8.1 Thread-safety contract

Thread-safe functions:

```c
bool timui_post(Timui *ui, uint32_t type, const void *data, size_t size);
void timui_wakeup(Timui *ui);
```

Conditionally thread-safe functions:

```c
TimuiCaps timui_caps_snapshot(Timui *ui);
```

UI-thread-only functions:

```text
timui_begin
timui_end
all widget functions
all drawing functions
all style functions
timui_poll_event
timui_recv
timui_close
```

Hard rule:

```text
Only the UI thread may write to the terminal.
```

## 8.2 Internal mutability

Internally mutable:

```text
terminal mode state
input parser state
current frame buffer
previous frame buffer
style stack
layout stack
focus state
hot/active IDs
modal stack
menu state
persistent widget state
message queues
```

Externally immutable:

```text
TimuiStr data is read-only.
Themes are const after creation.
Capability snapshots are immutable values.
View functions receive const app model pointers.
Controlled widgets return proposed changes instead of mutating app data.
Worker messages are copied into queues.
```

## 8.3 Multiple `Timui` instances

Allowed:

```text
Multiple Timui instances may exist if they use independent file descriptors.
```

Not guaranteed:

```text
Multiple Timui instances writing to the same terminal concurrently.
```

## 8.4 Worker model

Example:

```c
typedef struct {
    Timui *ui;
    const char *path;
} ScanTask;

static void *scan_thread(void *arg) {
    ScanTask *task = arg;

    /* perform filesystem scan */

    AppMsg msg = {
        .type = APP_SCAN_DONE,
        .count = count
    };

    timui_post(task->ui, APP_SCAN_DONE, &msg, sizeof msg);
    return NULL;
}
```

The UI thread receives:

```c
while (timui_recv(ui, &type, &msg, &size)) {
    model = update(model, msg);
}
```

---

# 9. Terminal capability model

## 9.1 Capabilities

```c
typedef enum {
    TIMUI_CAP_ALT_SCREEN       = 1u << 0,
    TIMUI_CAP_TRUECOLOR        = 1u << 1,
    TIMUI_CAP_256_COLOR        = 1u << 2,
    TIMUI_CAP_SGR_MOUSE        = 1u << 3,
    TIMUI_CAP_BRACKETED_PASTE  = 1u << 4,
    TIMUI_CAP_FOCUS_EVENTS     = 1u << 5,
    TIMUI_CAP_SYNC_OUTPUT      = 1u << 6,
    TIMUI_CAP_KITTY_KEYBOARD   = 1u << 7,
    TIMUI_CAP_OSC8_HYPERLINKS  = 1u << 8,
    TIMUI_CAP_KITTY_GRAPHICS   = 1u << 9,
    TIMUI_CAP_UNICODE_CORE     = 1u << 10
} TimuiCapFlags;

typedef struct {
    uint32_t flags;
    int colors;
    int width;
    int height;

    char term[64];
    char term_program[64];
    char term_program_version[64];
} TimuiCaps;

TimuiCaps timui_caps(const Timui *ui);
bool      timui_has_cap(const Timui *ui, TimuiCapFlags cap);
```

## 9.2 Detection policy

Detection should be conservative.

Inputs:

```text
TERM
TERM_PROGRAM
COLORTERM
terminfo when available, but not required
active terminal queries with short timeout
known terminal allowlist
known multiplexer deny/limit list
```

Rules:

```text
Do not block startup for long capability probes.
Do not enable risky protocols unless configured or detected.
Allow users to force-enable or force-disable capabilities.
Treat tmux/screen/zellij as capability reducers unless explicit passthrough is known.
```

## 9.3 Synchronized output

Use synchronized output only when:

```text
TIMUI_FLAG_SYNC_OUTPUT requested
terminal capability says supported
not inside a known problematic multiplexer, unless forced
```

Renderer behavior:

```text
begin frame output:
    ESC [ ? 2026 h

emit diff:
    cursor moves
    SGR changes
    text cells
    clears
    hyperlinks
    image placeholders

end frame output:
    ESC [ ? 2026 l
    flush
```

Fallback behavior:

```text
hide cursor
emit diff
show cursor
flush
```

---

# 10. Rendering architecture

## 10.1 Layers

```text
Public API
    ↓
Widgets/layout/style
    ↓
Frame builder
    ↓
Cell buffer + overlays
    ↓
Diff renderer
    ↓
ANSI/protocol writer
    ↓
terminal
```

## 10.2 Cell representation

```c
typedef struct {
    uint32_t codepoint;
    uint32_t cluster_index;

    uint32_t fg;
    uint32_t bg;
    uint32_t attrs;

    uint16_t width;
    uint16_t flags;

    uint32_t hyperlink_id;
    uint32_t image_id;
} TimuiCell;
```

Flags:

```c
typedef enum {
    TIMUI_CELL_EMPTY        = 0,
    TIMUI_CELL_CONTINUATION = 1u << 0,
    TIMUI_CELL_DIRTY        = 1u << 1,
    TIMUI_CELL_WIDE         = 1u << 2,
    TIMUI_CELL_IMAGE        = 1u << 3,
    TIMUI_CELL_LINK         = 1u << 4
} TimuiCellFlags;
```

## 10.3 Buffers

Each `Timui` owns:

```text
current cell buffer
previous cell buffer
scratch output buffer
frame arena
persistent widget state map
overlay list
```

Frame lifecycle:

```text
timui_begin:
    poll terminal events
    process resize
    reset current frame buffer
    reset frame arena
    reset layout/style stacks
    begin widget traversal

widget calls:
    update interaction state
    draw into current cell buffer
    emit app messages if requested

timui_end:
    resolve cursor
    resolve overlays
    diff current vs previous
    emit terminal update
    swap current/previous
```

## 10.4 Diff algorithm v0.1

MVP algorithm:

```text
For each row:
    scan cells left to right
    find changed runs
    move cursor to start of run
    emit SGR only when style changes
    emit cell text
    clear rest of line when cheaper
```

Later optimizations:

```text
damage regions
line hashing
run-length encoding
style cache
cursor-motion cost model
alternate diff strategies for slow SSH links
```

## 10.5 Unicode policy

v0.1:

```text
UTF-8 decode
ASCII support
Unicode box drawing support
basic wcwidth-compatible width table
combining mark handling for common cases
safe replacement character for invalid UTF-8
ASCII fallback mode
```

v0.2:

```text
generated Unicode width tables
grapheme cluster table
emoji width policy
CJK ambiguous-width configuration
optional external unicode backend
```

Build options:

```c
#define TIMUI_UNICODE_MINIMAL
#define TIMUI_UNICODE_GENERATED_TABLES
#define TIMUI_UNICODE_EXTERNAL
```

## 10.6 Images

Not v0.1 core. Design for v0.2.

```c
typedef struct TimuiImage TimuiImage;

TimuiResult timui_image_from_png_bytes(
    Timui *ui,
    const void *data,
    size_t size,
    TimuiImage **out_image
);

void timui_image_free(Timui *ui, TimuiImage *image);

void timui_image(
    TimuiFrame *f,
    TimuiImage *image,
    TimuiRect r,
    uint32_t flags
);
```

Rules:

```text
Do not require PNG decoding for v0.2.
Accept already-encoded PNG bytes.
Transmit to terminal when supported.
Fallback to text placeholder.
Cache terminal image IDs.
Delete placements on frame teardown or when invalidated.
```

---

# 11. Styling and themes

## 11.1 Style attributes

```c
typedef enum {
    TIMUI_ATTR_NONE      = 0,
    TIMUI_ATTR_BOLD      = 1u << 0,
    TIMUI_ATTR_DIM       = 1u << 1,
    TIMUI_ATTR_ITALIC    = 1u << 2,
    TIMUI_ATTR_UNDERLINE = 1u << 3,
    TIMUI_ATTR_REVERSE   = 1u << 4,
    TIMUI_ATTR_BLINK     = 1u << 5,
    TIMUI_ATTR_STRIKE    = 1u << 6
} TimuiAttrs;
```

## 11.2 Theme slots

```c
typedef enum {
    TIMUI_SLOT_TEXT = 0,
    TIMUI_SLOT_TEXT_DIM,
    TIMUI_SLOT_PANEL,
    TIMUI_SLOT_PANEL_TITLE,
    TIMUI_SLOT_BORDER,
    TIMUI_SLOT_BUTTON,
    TIMUI_SLOT_BUTTON_HOVERED,
    TIMUI_SLOT_BUTTON_FOCUSED,
    TIMUI_SLOT_BUTTON_ACTIVE,
    TIMUI_SLOT_INPUT,
    TIMUI_SLOT_INPUT_FOCUSED,
    TIMUI_SLOT_SELECTION,
    TIMUI_SLOT_MENU,
    TIMUI_SLOT_MENU_ACTIVE,
    TIMUI_SLOT_STATUS,
    TIMUI_SLOT_ERROR,
    TIMUI_SLOT_WARNING,
    TIMUI_SLOT_SUCCESS,
    TIMUI_SLOT_COUNT
} TimuiStyleSlot;

typedef struct {
    TimuiStyle slots[TIMUI_SLOT_COUNT];
} TimuiTheme;
```

APIs:

```c
void       timui_set_theme(Timui *ui, const TimuiTheme *theme);
TimuiStyle timui_style(const TimuiFrame *f, TimuiStyleSlot slot);

void timui_push_style(TimuiFrame *f, TimuiStyleSlot slot, TimuiStyle style);
void timui_pop_style(TimuiFrame *f);
```

---

# 12. Example: Mini Commander skeleton

```c
#define TIMUI_IMPLEMENTATION
#include "timui.h"

#include <string.h>

typedef struct {
    const char **items;
    int count;
} FileList;

typedef struct {
    TimuiListState left;
    TimuiListState right;
    char command[256];
} Model;

static const char *label_fn(void *userdata, int index) {
    FileList *list = (FileList *)userdata;
    return list->items[index];
}

int main(void) {
    const char *left_items[] = {
        "..", "src", "include", "README.md", "main.c", "build"
    };

    const char *right_items[] = {
        "..", "bin", "lib", "share", "notes.txt"
    };

    FileList left_files = { left_items, 6 };
    FileList right_files = { right_items, 5 };

    Model model = {0};

    Timui *ui = NULL;

    TimuiConfig cfg = {
        .title = "Mini Commander",
        .input_fd = 0,
        .output_fd = 1,
        .profile = TIMUI_PROFILE_AUTO,
        .flags = TIMUI_FLAG_ALT_SCREEN |
                 TIMUI_FLAG_MOUSE |
                 TIMUI_FLAG_BRACKETED_PASTE |
                 TIMUI_FLAG_TRUECOLOR |
                 TIMUI_FLAG_SYNC_OUTPUT |
                 TIMUI_FLAG_KITTY_KEYBOARD |
                 TIMUI_FLAG_RESTORE_ON_EXIT,
        .theme = TIMUI_THEME_DOS_BLUE
    };

    if (timui_open(&cfg, &ui) != TIMUI_OK) {
        return 1;
    }

    while (!timui_should_quit(ui)) {
        TimuiFrame *f = NULL;

        if (!timui_begin(ui, &f)) {
            break;
        }

        TimuiRect root = timui_root(f);

        TimuiRect menu = timui_cut_top(&root, 1);
        TimuiRect keys = timui_cut_bottom(&root, 1);
        TimuiRect cmd  = timui_cut_bottom(&root, 1);

        timui_menu_bar_begin(f, menu);

        if (timui_menu_begin(f, TIMUI_ID("file-menu"), TIMUI_STR_LIT("File"))) {
            if (timui_menu_item(f, TIMUI_ID("copy"), TIMUI_STR_LIT("Copy"), TIMUI_STR_LIT("F5"))) {
                /* copy */
            }

            if (timui_menu_item(f, TIMUI_ID("delete"), TIMUI_STR_LIT("Delete"), TIMUI_STR_LIT("F8"))) {
                /* delete */
            }

            timui_menu_end(f);
        }

        timui_menu_bar_end(f);

        timui_input_line_buf(f, TIMUI_ID("command"), cmd,
                             model.command, sizeof model.command);

        TimuiRect left_rect, right_rect;
        timui_split_cols(root, 0.5f, &left_rect, &right_rect);

        timui_panel_begin(f, TIMUI_ID("left-panel"), left_rect,
                          TIMUI_STR_LIT("/home/me/project"),
                          TIMUI_BORDER_DOUBLE);

        TimuiListResult left_result =
            timui_listbox_mut(f,
                              TIMUI_ID("left-list"),
                              timui_panel_body(f),
                              &model.left,
                              left_files.count,
                              label_fn,
                              &left_files);

        timui_panel_end(f);

        timui_panel_begin(f, TIMUI_ID("right-panel"), right_rect,
                          TIMUI_STR_LIT("/tmp"),
                          TIMUI_BORDER_DOUBLE);

        TimuiListResult right_result =
            timui_listbox_mut(f,
                              TIMUI_ID("right-list"),
                              timui_panel_body(f),
                              &model.right,
                              right_files.count,
                              label_fn,
                              &right_files);

        timui_panel_end(f);

        if (left_result.activated) {
            /* open left_files.items[left_result.selected] */
        }

        if (right_result.activated) {
            /* open right_files.items[right_result.selected] */
        }

        timui_function_bar(f, keys,
            TIMUI_STR_LIT("1 Help  2 Menu  3 View  4 Edit  5 Copy  6 Move  7 Mkdir  8 Delete  10 Quit"));

        if (timui_key_pressed(f, TIMUI_KEY_ESCAPE, TIMUI_MOD_NONE) ||
            timui_key_pressed(f, TIMUI_KEY_F10, TIMUI_MOD_NONE)) {
            timui_quit(ui);
        }

        timui_end(f);
    }

    timui_close(ui);
    return 0;
}
```

---

# 13. Repository layout

Recommended development repo:

```text
timui/
  include/
    timui.h                 generated amalgamated header
  src/
    timui_core.h
    timui_core.c
    timui_term_posix.c
    timui_term_win32.c
    timui_input.c
    timui_render.c
    timui_widgets.c
    timui_unicode.c
    timui_thread.c
  tools/
    amalgamate.c
    gen_unicode_tables.c
  examples/
    hello.c
    counter.c
    form.c
    mini_commander.c
    keyboard_inspector.c
    mouse_demo.c
  tests/
    test_id.c
    test_rect.c
    test_utf8.c
    test_input_parser.c
    test_render_diff.c
    test_widgets.c
    test_message_queue.c
  docs/
    README.md
    API.md
    DESIGN.md
    TERMINAL_PROTOCOLS.md
    THREADING.md
  fuzz/
    fuzz_input_parser.c
  build/
```

Release artifact:

```text
timui.h
examples/
README.md
LICENSE
```

---

# 14. Dependencies

## 14.1 Required default dependencies

```text
C99 compiler
libc
```

For POSIX terminal backend:

```text
termios
unistd
fcntl
poll or select
ioctl(TIOCGWINSZ)
signal handling
```

For thread-safe posting on POSIX:

```text
pthread, unless TIMUI_NO_THREADS is defined
```

## 14.2 Optional dependencies

```text
none required by default
```

Optional future integrations:

```text
termbox2 backend adapter
utf8proc backend
stb-style image helpers
CMake build
Meson build
pkg-config file
```

`termbox2` should be treated as an inspiration or optional backend, not as a required foundation. Its appeal is clear: it is single-header, has no dependencies beyond libc, supports a slim terminal I/O model, and already does useful parsing/color work. But `timui.h` needs direct control over higher-level capabilities such as synchronized output, Kitty keyboard negotiation, overlays, widgets, and functional app messages.

---

# 15. Implementation roadmap for an agent/swarm

The following is structured for parallel implementation. Each task has:

```text
ID
Owner type
Dependencies
Deliverable
Acceptance criteria
Parallelization notes
```

## Phase 0 — Product and scaffolding

### T0.1 — Lock project decisions ✅

Dependencies: none.

Deliverable:

```text
docs/DECISIONS.md
```

Must decide:

```text
license: MIT or 0BSD recommended
C baseline: C99
thread baseline: optional pthreads
single-header release strategy
public naming conventions
error handling policy
style conventions
```

Acceptance criteria:

```text
DECISIONS.md exists
all public symbol prefixes are timui_
all macros use TIMUI_
all structs use Timui
```

Parallelization: blocks public API work.

---

### T0.2 — Repository skeleton ✅

Dependencies: T0.1.

Deliverable:

```text
repo structure
basic build scripts
empty timui.h
empty examples
empty tests
```

Acceptance criteria:

```text
cc -std=c99 examples/hello.c compiles
make test runs placeholder tests
CI stub exists
```

Parallelization: enables all teams.

---

### T0.3 — Amalgamation tool ✅

Dependencies: T0.2.

Deliverable:

```text
tools/amalgamate.c or tools/amalgamate.py
generated include/timui.h
```

Acceptance criteria:

```text
single generated timui.h compiles with TIMUI_IMPLEMENTATION
source split and generated header expose same API
```

Parallelization: can run alongside core work after file layout is stable.

---

## Phase 1 — Core foundations

### T1.1 — Result/error system ✅

Dependencies: T0.2.

Deliverable:

```text
TimuiResult
timui_error_string
internal error macros
```

Acceptance criteria:

```text
every public fallible API returns TimuiResult or bool with retrievable error
tests cover stringification
```

Parallelization: independent.

---

### T1.2 — Allocator and arena ✅

Dependencies: T0.2, T1.1.

Deliverable:

```text
TimuiAllocator
internal permanent allocator
frame arena
scratch arena
```

Acceptance criteria:

```text
custom allocator test passes
frame arena resets each frame
ASAN clean
```

Parallelization: blocks many tasks.

---

### T1.3 — Strings ✅

Dependencies: T1.2.

Deliverable:

```text
TimuiStr
string helpers
bounded copy helpers
format helpers if stdio enabled
```

Acceptance criteria:

```text
no public API requires null-terminated strings except _cstr helpers
tests cover literal, slice, empty, invalid UTF-8 pass-through
```

Parallelization: can proceed with layout/IDs.

---

### T1.4 — Rect and layout primitives ✅

Dependencies: T0.2.

Deliverable:

```text
TimuiRect
cut/split/inset/pad helpers
```

Acceptance criteria:

```text
unit tests for negative/zero dimensions
no overflow on common int ranges
```

Parallelization: independent; widgets depend on it.

---

### T1.5 — ID hashing and ID stack ✅

Dependencies: T1.2, T1.3.

Deliverable:

```text
TimuiId
hash functions
push/pop ID stack
debug collision support
```

Acceptance criteria:

```text
stable IDs across frames
nested IDs compose correctly
tests cover stack behavior
```

Parallelization: blocks focus/widgets.

---

### T1.6 — Message queue ✅

Dependencies: T1.2.

Deliverable:

```text
internal message queue
timui_emit
timui_recv
```

Acceptance criteria:

```text
messages are copied
queue handles full condition predictably
tests cover variable message sizes
```

Parallelization: blocks functional runner.

---

### T1.7 — Thread-safe MPSC queue ✅

Dependencies: T1.6.

Deliverable:

```text
timui_post
timui_wakeup
optional pthread backend
TIMUI_NO_THREADS mode
```

Acceptance criteria:

```text
multiple worker threads can post messages
TSAN clean where available
TIMUI_NO_THREADS build compiles
```

Parallelization: can proceed while rendering/widgets happen.

---

## Phase 2 — Terminal backend

### T2.1 — Terminal transport abstraction ✅

Dependencies: T1.1, T1.2.

Deliverable:

```text
TimuiTransport
read/write/flush abstraction
fake transport for tests
```

Acceptance criteria:

```text
all renderer/input tests can run without real terminal
fake transport captures output bytes
```

Parallelization: blocks parser and renderer tests.

---

### T2.2 — POSIX raw mode ✅

Dependencies: T2.1.

Deliverable:

```text
termios setup/restore
nonblocking input config
atexit restoration hook
```

Acceptance criteria:

```text
terminal restored after normal close
double close safe
failure during open restores previous state
```

Parallelization: backend-specific.

---

### T2.3 — Screen mode setup/teardown ✅

Dependencies: T2.1, T2.2.

Deliverable:

```text
alternate screen
cursor hide/show
bracketed paste enable/disable
mouse enable/disable
focus enable/disable
title set
```

Acceptance criteria:

```text
fake transport output matches expected setup/teardown sequences
teardown reverses setup in correct order
```

Parallelization: can run with T2.4.

---

### T2.4 — Terminal size ✅

Dependencies: T2.2.

Deliverable:

```text
initial size query
SIGWINCH or polling resize detection
TimuiEvent resize production
```

Acceptance criteria:

```text
resize event updates root rect
zero-size handled safely
```

Parallelization: blocks robust frame begin.

---

### T2.5 — Capability detection ✅

Dependencies: T2.1, T2.3.

Deliverable:

```text
TimuiCaps
env detection
safe query mechanism
force enable/disable options
multiplexer detection
```

Acceptance criteria:

```text
caps are deterministic under fake env
timeouts are bounded
unknown terminal falls back safely
```

Parallelization: blocks sync output and kitty keyboard negotiation.

---

### T2.6 — Legacy input parser ✅

Dependencies: T1.3, T2.1.

Deliverable:

```text
byte stream parser
ASCII text
UTF-8 text
basic ESC/CSI key sequences
arrow keys
function keys
```

Acceptance criteria:

```text
golden tests for common xterm sequences
partial sequences handled
invalid bytes do not crash
```

Parallelization: can run with renderer.

---

### T2.7 — Mouse, paste, focus parser ✅

Dependencies: T2.6.

Deliverable:

```text
SGR mouse parser
wheel events
drag/motion events
bracketed paste parser
focus in/out parser
```

Acceptance criteria:

```text
golden tests for mouse press/release/move/wheel
paste data delivered as paste event
large paste is bounded/chunked safely
```

Parallelization: blocks input widgets.

---

### T2.8 — Kitty keyboard protocol ✅

Dependencies: T2.5, T2.6.

Deliverable:

```text
enable/disable protocol
parse enhanced key events
modifiers
repeat/release where available
associated text where available
fallback behavior
```

Acceptance criteria:

```text
golden tests from kitty protocol examples
Esc vs Alt/Esc ambiguity improved when protocol active
Ctrl/Shift combinations represented distinctly when terminal reports them
```

Parallelization: independent after parser/caps.

---

### T2.9 — Synchronized output ✅

Dependencies: T2.5, T3.4.

Deliverable:

```text
renderer wrapping support for DEC 2026 sync output
fallback cursor-hide strategy
```

Acceptance criteria:

```text
sync wrappers emitted only when enabled and supported
no sync wrappers in known unsafe fallback mode
fake transport golden tests pass
```

Parallelization: depends on renderer output.

---

## Phase 3 — Rendering

### T3.1 — Cell buffer ✅

Dependencies: T1.2, T1.4.

Deliverable:

```text
TimuiCell
current/previous buffers
resize handling
clear/fill operations
```

Acceptance criteria:

```text
resize preserves no invalid pointers
clear/fill unit tests pass
ASAN clean
```

Parallelization: blocks drawing/diff.

---

### T3.2 — UTF-8 decode and width ✅

Dependencies: T1.3.

Deliverable:

```text
UTF-8 decoder
minimal width function
replacement handling
box drawing support
```

Acceptance criteria:

```text
valid UTF-8 decodes
invalid UTF-8 safe
ASCII width correct
box glyph width correct
basic CJK/wide tests documented even if incomplete
```

Parallelization: can run with T3.1.

---

### T3.3 — Drawing primitives ✅

Dependencies: T3.1, T3.2.

Deliverable:

```text
text
text clipped
fill
box
hline/vline
border glyph sets
```

Acceptance criteria:

```text
cell-buffer snapshot tests pass
clipping safe
wide cells do not corrupt adjacent cells
```

Parallelization: blocks widgets.

---

### T3.4 — Diff renderer ✅

Dependencies: T2.1, T3.1, T3.3.

Deliverable:

```text
current-vs-previous diff
cursor motion emission
SGR emission
flush
```

Acceptance criteria:

```text
golden tests for simple frames
unchanged frame emits minimal/no output
style changes correct
clear-to-end optimization tested
```

Parallelization: blocks full frame lifecycle.

---

### T3.5 — Cursor rendering ✅

Dependencies: T3.4.

Deliverable:

```text
logical cursor position
show/hide policy
cursor style options
```

Acceptance criteria:

```text
input widget can place cursor
cursor hidden when requested
teardown restores cursor
```

Parallelization: needed for input widgets.

---

### T3.6 — Hyperlink overlay

Dependencies: T3.4.

Deliverable v0.2:

```text
OSC 8 hyperlink range support
cell hyperlink IDs
fallback plain text
```

Acceptance criteria:

```text
golden output tests
links closed correctly
```

Parallelization: v0.2; not MVP.

---

### T3.7 — Image overlay

Dependencies: T2.5, T3.4, T3.6 optional.

Deliverable v0.2:

```text
Kitty graphics transport
image ID cache
placement lifecycle
fallback placeholder
```

Acceptance criteria:

```text
fake transport verifies transmit/place/delete sequences
image unsupported fallback is clean
```

Parallelization: v0.2; can be assigned separately once caps and renderer exist.

---

## Phase 4 — Frame and interaction core

### T4.1 — Frame lifecycle ✅

Dependencies: T1.2, T2.4, T2.6, T3.4.

Deliverable:

```text
timui_begin
timui_end
event ingestion
arena reset
buffer reset
diff presentation
```

Acceptance criteria:

```text
hello example runs
resize updates root rect
multiple frames stable
```

Parallelization: central integration task.

---

### T4.2 — Focus/hot/active state ✅

Dependencies: T1.5, T2.6, T2.7, T4.1.

Deliverable:

```text
hot ID
active ID
focus ID
tab order
mouse hit testing
keyboard activation
```

Acceptance criteria:

```text
Tab cycles focus
Enter/Space activate focused button
mouse click activates correct widget
focus stable across frames
```

Parallelization: blocks widgets.

---

### T4.3 — Style/theme stack ✅

Dependencies: T3.3, T4.1.

Deliverable:

```text
builtin themes
style lookup
push/pop style overrides
```

Acceptance criteria:

```text
theme examples render
push/pop nesting correct
```

Parallelization: can run with focus work.

---

### T4.4 — Clip stack

Dependencies: T3.3, T4.1.

Deliverable:

```text
push clip rect
pop clip rect
drawing clipped to active rect
```

Acceptance criteria:

```text
nested clipping tests pass
listbox cannot draw outside panel
```

Parallelization: blocks scroll/list widgets.

---

### T4.5 — Modal/menu stack

Dependencies: T4.2.

Deliverable:

```text
modal focus trapping
menu open/close state
outside-click close
escape close
```

Acceptance criteria:

```text
modal blocks background widgets
menus close predictably
keyboard navigation works
```

Parallelization: blocks menus/modals.

---

## Phase 5 — Widgets

### T5.1 — Label, separator, panel ✅

Dependencies: T3.3, T4.3.

Deliverable:

```text
timui_text helpers
separator
panel begin/end/body
```

Acceptance criteria:

```text
panel demo renders DOS theme
body rect correct
```

Parallelization: first widget milestone.

---

### T5.2 — Button ✅

Dependencies: T4.2, T5.1.

Deliverable:

```text
timui_button
hover/focus/active rendering
keyboard activation
mouse activation
```

Acceptance criteria:

```text
button tests cover mouse and keyboard
counter example works
```

Parallelization: enables many examples.

---

### T5.3 — Checkbox and radio ✅

Dependencies: T5.2.

Deliverable:

```text
controlled checkbox/radio
_mut wrappers
```

Acceptance criteria:

```text
functional form example works
mutable form example works
```

Parallelization: independent after button.

---

### T5.4 — Input line ✅

Dependencies: T2.7, T3.2, T3.5, T4.2.

Deliverable:

```text
controlled input-line edit ops
mutable buffer wrapper
cursor movement
delete/backspace
home/end
paste
submit
```

Acceptance criteria:

```text
typing text updates buffer in _mut wrapper
controlled API returns correct edit ops
cursor placement correct
paste works
invalid UTF-8 safe
```

Parallelization: complex; assign dedicated owner.

---

### T5.5 — Listbox ✅

Dependencies: T4.2, T4.4, T5.1.

Deliverable:

```text
controlled listbox
_mut wrapper
scrolling
selection
activation
mouse wheel
```

Acceptance criteria:

```text
keyboard up/down works
mouse click selects
Enter activates
scroll offset maintained
```

Parallelization: dedicated owner.

---

### T5.6 — Status/function bar ✅

Dependencies: T5.1.

Deliverable:

```text
timui_status_bar
timui_function_bar
```

Acceptance criteria:

```text
mini_commander function-key row matches expected snapshot
```

Parallelization: simple; can be early.

---

### T5.7 — Menu bar and menus

Dependencies: T4.5, T5.2, T5.5.

Deliverable:

```text
menu bar
menu popup
menu items
shortcut display
keyboard navigation
mouse navigation
```

Acceptance criteria:

```text
Alt/menu key opens menu where possible
arrow navigation works
Esc closes
click outside closes
```

Parallelization: after modal/menu stack.

---

### T5.8 — Message box/modal

Dependencies: T4.5, T5.2.

Deliverable:

```text
message box
button row
modal centering helper
```

Acceptance criteria:

```text
modal traps focus
buttons return result
Esc maps to cancel
```

Parallelization: after modal stack and button.

---

## Phase 6 — Functional/app runner

### T6.1 — Controlled widget examples

Dependencies: T5.2, T5.3, T5.4, T5.5.

Deliverable:

```text
examples/counter_functional.c
examples/form_functional.c
docs/FUNCTIONAL_STYLE.md
```

Acceptance criteria:

```text
examples do not pass mutable pointers to widgets except local rendering state
model updated only in update function
```

Parallelization: docs/examples team.

---

### T6.2 — App runner

Dependencies: T1.6, T4.1, T6.1.

Deliverable:

```text
timui_run
TimuiApp
optional update/view runner
```

Acceptance criteria:

```text
counter works with runner
manual loop remains supported
```

Parallelization: optional, not blocking MVP if manual loop is good.

---

### T6.3 — Worker example

Dependencies: T1.7, T6.1.

Deliverable:

```text
examples/async_scan.c
docs/THREADING.md
```

Acceptance criteria:

```text
worker thread posts message
UI wakes and updates model
TSAN clean where available
```

Parallelization: can run after queue.

---

## Phase 7 — Examples and demos

### T7.1 — Hello ✅

Dependencies: T4.1, T5.1.

Deliverable:

```text
examples/hello.c
```

Acceptance criteria:

```text
under 80 LOC
compiles single-header
```

---

### T7.2 — Counter ✅

Dependencies: T5.2, T6.1.

Deliverable:

```text
examples/counter.c
```

Acceptance criteria:

```text
shows controlled functional style
under 150 LOC
```

---

### T7.3 — Form demo

Dependencies: T5.3, T5.4, T5.8.

Deliverable:

```text
examples/form.c
```

Acceptance criteria:

```text
checkbox
radio
input
submit modal
```

---

### T7.4 — Mini Commander

Dependencies: T5.4, T5.5, T5.6, T5.7, T5.8.

Deliverable:

```text
examples/mini_commander.c
```

Acceptance criteria:

```text
two panes
menu bar
function bar
command input
listbox focus switching
F10 exits
under 500 LOC target
```

---

### T7.5 — Keyboard inspector

Dependencies: T2.8.

Deliverable:

```text
examples/keyboard_inspector.c
```

Acceptance criteria:

```text
shows key/mod/action/text
useful for comparing Ghostty/kitty/fallback behavior
```

---

### T7.6 — Protocol capability inspector

Dependencies: T2.5.

Deliverable:

```text
examples/caps.c
```

Acceptance criteria:

```text
prints detected capabilities
shows why each capability is enabled/disabled
```

---

## Phase 8 — Testing and quality

### T8.1 — Unit test harness

Dependencies: T0.2.

Deliverable:

```text
minimal C test harness
assert macros
test runner
```

Acceptance criteria:

```text
make test runs all tests
nonzero exit on failure
```

---

### T8.2 — Input parser golden tests

Dependencies: T2.6, T2.7, T2.8.

Deliverable:

```text
tests/test_input_parser.c
golden sequence fixtures
```

Acceptance criteria:

```text
legacy keys
mouse
paste
kitty keyboard
partial reads
invalid sequences
```

---

### T8.3 — Renderer snapshot tests

Dependencies: T3.4.

Deliverable:

```text
cell-buffer snapshots
ANSI output golden tests
```

Acceptance criteria:

```text
stable deterministic output
diff output minimal enough for MVP
```

---

### T8.4 — Widget interaction tests

Dependencies: T4.2, T5.x.

Deliverable:

```text
fake event injection
widget state tests
```

Acceptance criteria:

```text
button click
keyboard focus
list scroll
input typing
modal trapping
```

---

### T8.5 — Fuzz input parser

Dependencies: T2.6.

Deliverable:

```text
fuzz/fuzz_input_parser.c
```

Acceptance criteria:

```text
no crash on arbitrary byte streams
bounded memory growth
timeout-safe
```

---

### T8.6 — Sanitizers

Dependencies: broad.

Deliverable:

```text
ASAN build
UBSAN build
TSAN build when threads enabled
```

Acceptance criteria:

```text
core tests pass under sanitizers
```

---

## Phase 9 — Documentation

### T9.1 — README

Dependencies: T7.1, T7.2.

Deliverable:

```text
README.md
```

Must include:

```text
what timui.h is
what it is not
hello example
build instructions
terminal support
screenshots/gifs later
```

---

### T9.2 — API reference

Dependencies: public API stabilization.

Deliverable:

```text
docs/API.md
```

Acceptance criteria:

```text
every public function documented
ownership/lifetime/threading documented
```

---

### T9.3 — Design document

Dependencies: architecture stabilized.

Deliverable:

```text
docs/DESIGN.md
```

Must cover:

```text
immediate mode
functional style
frame lifecycle
renderer
input parser
capabilities
state model
```

---

### T9.4 — Threading guide

Dependencies: T1.7, T6.3.

Deliverable:

```text
docs/THREADING.md
```

Must state clearly:

```text
UI thread owns rendering
worker threads post messages only
which APIs are thread-safe
which APIs are UI-thread-only
```

---

### T9.5 — Terminal protocol guide

Dependencies: T2.5, T2.8, T2.9.

Deliverable:

```text
docs/TERMINAL_PROTOCOLS.md
```

Must cover:

```text
legacy keyboard fallback
Kitty keyboard
SGR mouse
bracketed paste
synchronized output
OSC 8 future
Kitty graphics future
multiplexer behavior
```

---

# 16. Parallelization map

## Team A — Core/API

Can start immediately after T0.2:

```text
T1.1 result/error
T1.2 allocator/arena
T1.3 strings
T1.4 rect/layout
T1.5 IDs
T1.6 message queue
```

Blocks:

```text
almost everything
```

## Team B — Terminal backend

Starts after T2.1 transport:

```text
T2.2 raw mode
T2.3 screen modes
T2.4 terminal size
T2.5 caps
T2.6 parser
T2.7 mouse/paste/focus
T2.8 kitty keyboard
T2.9 sync output
```

Blocks:

```text
frame lifecycle
widgets that need input
smooth rendering
```

## Team C — Renderer

Starts after T1.2/T1.4:

```text
T3.1 cell buffer
T3.2 UTF-8/width
T3.3 drawing
T3.4 diff
T3.5 cursor
```

Blocks:

```text
frame lifecycle
visible widgets
```

## Team D — Interaction/widgets

Starts after T4.1/T4.2:

```text
T5.1 panel/label
T5.2 button
T5.3 checkbox/radio
T5.4 input
T5.5 listbox
T5.6 function bar
T5.7 menus
T5.8 modals
```

Blocks:

```text
examples
PR screenshots
user validation
```

## Team E — Functional/threading

Starts after T1.6/T1.7 and early widgets:

```text
T6.1 controlled examples
T6.2 app runner
T6.3 worker example
docs/THREADING.md
```

Blocks:

```text
immutable/functional positioning
async app story
```

## Team F — Testing/docs/examples

Can start early with stubs:

```text
T8.x tests
T9.x docs
T7.x examples
```

Must track API changes.

---

# 17. MVP definition of done

v0.1 is done when:

```text
A single timui.h can be copied into a C project.
hello.c compiles with cc -std=c99.
counter.c demonstrates functional controlled style.
form.c demonstrates input, checkbox, radio, modal.
mini_commander.c demonstrates the target DOS/MC aesthetic.
No ncurses dependency exists.
POSIX backend works in Ghostty and kitty.
Fallback works in xterm-256color-like terminals.
Synchronized output is used when detected/enabled.
Kitty keyboard protocol is parsed when enabled.
Mouse, paste, resize, focus basics work.
ASAN/UBSAN test runs are clean.
Parser fuzz target does not crash on arbitrary bytes.
Thread-safe timui_post works with a worker example.
README explains philosophy and limitations.
```

---

# 18. Risks and mitigations

## Risk: Unicode width is harder than expected

Mitigation:

```text
ship minimal Unicode in v0.1
make ASCII fallback excellent
document limitations
add generated tables in v0.2
avoid promising perfect emoji/CJK behavior initially
```

## Risk: terminal capability detection is unreliable

Mitigation:

```text
conservative defaults
explicit force flags
capability inspector example
short query timeouts
known terminal/multiplexer policy table
```

## Risk: “threadsafe” is misunderstood

Mitigation:

```text
document single-renderer-thread model everywhere
name APIs clearly
only timui_post/timui_wakeup are cross-thread
TSAN tests
```

## Risk: functional C API becomes too verbose

Mitigation:

```text
provide controlled APIs as default
provide _mut wrappers for quick scripts
keep examples for both styles
allow gradual adoption
```

## Risk: single-header becomes too large

Mitigation:

```text
amalgamated release
split internal source
feature macros
optional images/unicode tables
```

## Risk: trying to support too many terminals

Mitigation:

```text
target Ghostty/kitty-class terminals first
safe fallback second
do not chase every historical terminal quirk
document support matrix
```

---

# 19. Open design questions

## 19.1 Should `TimuiFrame` be explicit?

Option A:

```c
timui_button(f, id, r, label);
```

Option B:

```c
timui_button(ui, id, r, label);
```

Recommendation: use explicit `TimuiFrame *`.

Reason:

```text
clear lifetime
clear UI-thread boundary
better testability
better functional mental model
```

## 19.2 Should controlled widgets return new state by value?

Example:

```c
TimuiListResult r = timui_listbox(f, id, r, old_state, count, label, data);
new_state = r.state;
```

Recommendation: yes.

Reason:

```text
supports immutable app model
small structs are cheap
_mut wrappers can cover convenience use
```

## 19.3 Should images be v0.1?

Recommendation: no.

Reason:

```text
image support is differentiating but not required for the DOS/MC MVP
it complicates protocol lifecycle
it can ship as v0.2 without disrupting core API
```

## 19.4 Should termbox2 be used internally?

Recommendation: not as the default core.

Reason:

```text
timui.h needs direct ownership of capability detection, synchronized output, Kitty keyboard, overlays, and widget state
termbox2 remains useful as inspiration or optional backend
```

---

# 20. Product positioning

The README should say:

```text
timui.h is a single-header C99 immediate-mode TUI library for modern terminal emulators.

It is designed for DOS-style and Midnight Commander-style interfaces:
panels, menus, dialogs, listboxes, inputs, function-key bars, and keyboard-driven workflows.

It does not depend on ncurses.

It targets Ghostty, kitty, WezTerm, Alacritty, foot, Rio, and similar modern terminals, while degrading to safe ANSI behavior where possible.

The default style is functional:
your app owns immutable state, your view describes widgets, widgets emit messages, and your update function produces the next state.

For small tools, mutable convenience APIs are available.
```

That positioning is the heart of the project. The implementation can evolve, but the identity should stay sharp:

```text
pure C
single-header
immediate-mode
modern-terminal-first
DOS/MC aesthetic
functional by default
thread-safe message passing
no ncurses
```

---

# 21. References

These are useful references for implementation research and protocol details:

- Kitty keyboard protocol: <https://sw.kovidgoyal.net/kitty/keyboard-protocol/>
- Kitty graphics protocol: <https://sw.kovidgoyal.net/kitty/graphics-protocol/>
- Ghostty feature documentation: <https://ghostty.org/docs/features>
- Ghostty synchronized output guidance: <https://ghostty.org/docs/help/synchronized-output>
- termbox2 repository: <https://github.com/termbox/termbox2>
- Notcurses repository: <https://github.com/dankamongmen/notcurses>
- Modern Turbo Vision repository: <https://github.com/magiblot/tvision>
- Midnight Commander developer documentation: <https://github.com/MidnightCommander/mc/blob/master/doc/HACKING>

