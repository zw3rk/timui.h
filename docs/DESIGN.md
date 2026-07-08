# DESIGN — timui.h

Architecture of the timui.h single-header C99 immediate-mode TUI.

## Layering

```
Public widget API (immediate-mode)
        ↓
Frame lifecycle + interaction + themes
        ↓
Cell buffer + diff renderer + drawing primitives
        ↓
Input parser + terminal transport
        ↓
POSIX backend (termios, ioctl, fd transport)
```

Each layer depends only downward. All runtime state lives in `Timui` and
per-frame state in `TimuiFrame`; there is **no hidden global mutable state**.

## Immediate mode

The app redraws every frame: `timui_begin` → declare widgets (which draw into
the current cell buffer and report interaction) → `timui_end` (diff-render to
the terminal and swap buffers). Widgets carry no identity of their own; the
framework remembers only the unavoidable interaction state (hot/active/focus,
scroll, pending text) keyed by caller-supplied `TimuiId`s that are stable
across frames.

## Functional / controlled style

Controlled widgets (`timui_button`, `timui_checkbox`, `timui_listbox`, …) take
the current value and return a result describing the user's intent
(clicked/changed/…); the app's model is updated only in the caller. `_mut`
wrappers (`timui_checkbox_mut`, `timui_input_line_buf`) are convenience for
quick tools. App state is immutable by convention; framework internals are
mutable only behind documented contracts.

Text editing follows the same rule. `timui_text_area_ex` returns a
`TimuiTextAreaResult` with the next state and intent bits; `timui_text_area_mut`
writes that state back for compact tools; the legacy `timui_text_area` wrapper
keeps editor-style Enter-as-newline behavior.

## Frame lifecycle

`timui_open` wires the fd transport, enters raw mode + alternate screen,
detects capabilities (TERM / TERM_PROGRAM / COLORTERM), sizes the cell
buffers, and inits the renderer / input parser / message queue / id stack /
interaction state. `timui_begin` ingests available input (non-blocking), drains
parsed events into the interaction state, clears the frame buffer, and resets
the id stack. `timui_end` diffs the previous vs current buffer through the
transport and swaps them.

## Rendering

A `TimuiCellBuffer` (w×h grid of `TimuiCell`) holds the frame. The diff
renderer walks prev vs curr and emits **only changed cells**: cursor
positioning (skipped when adjacent to the last write), truecolour SGR (only
when the style changed), and the UTF-8 glyph. Identical frames emit nothing.
Integer formatting is hand-rolled (no stdio), so the renderer is
`TIMUI_NO_STDIO`-safe.

## Input

An incremental state machine (ground / esc / csi / ss3 / utf8) parses the byte
stream into `TimuiEvent`s (key / text / mouse / paste / focus): legacy CSI
sequences, the Kitty keyboard protocol (CSI `<code>;<mods>u`), SGR mouse,
bracketed paste, and focus events. Invalid bytes become U+FFFD rather than
crashing; partial sequences complete across feeds. Modifier decoding (Shift /
Ctrl / Alt / …) is applied to ~ finals, letter finals, and CSI-u.

## Capabilities & fallback

Detection is conservative and deterministic from environment hints: modern
terminals (Ghostty / kitty / WezTerm / Alacritty / foot / Rio) get the VT
features they are known to support, while multiplexers (tmux / screen /
zellij) drop protocol-sensitive keyboard, sync, and image caps unless
passthrough is known. Image selection is protocol-neutral at the draw site:
Kitty is preferred, then Sixel, then iTerm2, and `TIMUI_NO_IMAGES` strips image
caps entirely while preserving placeholder rendering. Unknown terminals fall
back to a safe 16-colour minimum. Force masks override the result.

## Threading

Single writer: only the UI thread renders and owns `TimuiFrame`. Worker
threads post messages through the thread-safe MPSC queue (`timui_post`); the
UI thread drains them (`timui_recv`/the message queue). `TIMUI_NO_THREADS`
drops pthread entirely (single-threaded build).
