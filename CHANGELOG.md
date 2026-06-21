# Changelog

All notable changes to **timui.h** are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/); the project uses pre-1.0
semver (a MINOR bump signals a breaking API change, PATCH a fix).

## [Unreleased]

### Added
- **Five larger example applications** (`make run-<name>`): `editor` (text_area
  cursor editing), `file_manager` (MC-style dual-pane browser), `todo`
  (functional model/view/update), `procmon` (live `ps` table), `chat`
  (thread-safe `timui_post` from a worker thread).
- **App-level input accessors** (surfaced by building those demos):
  `timui_set_focus(f, id)` / `timui_focus(f)` — programmatic focus; and
  `timui_char_pressed(f, ch)` / `timui_text_input(f)` — read typed characters
  (digits/space/letters arrive as text, not `TimuiKey` events). These remove the
  need for apps to reach into `struct Timui`.

## [0.2.0] — 2026-07-05

First tagged release. A working immediate-mode C99 TUI — terminal backend,
incremental input parser, truecolour diff renderer, and a themed widget set —
hardened over seven review rounds (ASAN/UBSAN clean, 182 unit tests).

### Added
- **In-line cursor editing** for text inputs. `timui_text_area` and the new
  `timui_input_field` support Left/Right (by codepoint), Home/End (line bounds),
  Backspace/Delete (before/at the cursor), and mid-string insertion.
- **`timui_input_field`** + `TimuiInputState { char *text; size_t cap; size_t
  cursor; int scroll_x; }` — a single-line editor with horizontal scroll that
  keeps the cursor visible. `timui_input_line_buf` remains the append-only form.
- **Focused-input cursor visualization** — the hardware terminal cursor is
  placed at the edit position of the focused input and hidden when focus leaves.
- `timui_events_dropped()` (queue-overflow count) and a colour-model sentinel so
  pure black `0x000000` is distinct from `TIMUI_COLOR_DEFAULT` (ADR 0001).

### Changed (breaking)
- **`timui_ui_resize` now returns `TimuiResult`** (was `void`) so a resize-time
  out-of-memory is signalable.
- **`timui_tree` / `timui_table` / `timui_command_palette` are now controlled**
  with value-returning forms (`TimuiTreeResult` / `TimuiTableResult` /
  `TimuiCmdPaletteResult`) plus `*_mut` write-back twins, mirroring
  `timui_listbox`. The pointer-mutating forms are the `*_mut` variants; the
  plain forms take state by value and never touch caller memory.
- **The menu bar is now controlled via a caller-owned `TimuiMenuBar`** threaded
  through `menu_bar_begin` / `menu_begin` / `menu_item` / `menu_bar_end`. Its
  `open` field persists across frames and is observable; six hidden fields were
  removed from the `Timui` struct.
- `timui_id_stack_push` returns `TimuiResult` so an OOM can't silently corrupt
  the id hierarchy.

### Fixed
- **Input parser**: the UTF-8 decoder now rejects overlong / surrogate /
  above-max sequences (substituting U+FFFD), closing a NUL/invalid-UTF-8
  injection into app buffers; an ESC mid-CSI/SS3 aborts and restarts the
  sequence (ECMA-48) instead of leaking its tail as text; a CSI `:`
  sub-parameter (Kitty event-type / alternate-key reports) is discarded rather
  than desyncing the parser.
- **Rendering**: `draw_box` / `draw_hline` / `draw_vline` clamp extreme geometry
  to the buffer, removing signed-overflow UB and an unbounded loop.
- Terminal restoration on SIGTERM/SIGHUP/SIGQUIT; numerous adversarial-input and
  lifecycle-OOM edge cases hardened (see `docs/gaps.md`).

### Internal
- Single shared UTF-8 encoder and unified glyph-emit path; extracted shared
  list-widget scaffolding; stopped implementation-only macros/typedefs leaking
  into the consumer translation unit; honest public-header documentation.
- Coverage expanded to error/OOM paths (injectable allocator), the message API,
  drawing primitives, and the termios failure branch.

[0.2.0]: https://github.com/
