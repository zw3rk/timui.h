# Changelog

All notable changes to **timui.h** are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/); the project uses pre-1.0
semver (a MINOR bump signals a breaking API change, PATCH a fix).

## [Unreleased]

### Fixed
- **Release header validation**: amalgamation now fails hard if an inlined source
  section cannot be read, and `release-check` compiles a copied header from an
  isolated directory so repo-relative `../src/...` includes cannot slip through.
- **Paste event ordering**: bracketed paste is now queued in input order with
  surrounding text and keys, so `x <paste>P</paste> y` reaches apps as `xPy`,
  and paste-then-Enter submits the pasted text rather than an empty segment.
- **Thread-safe custom allocators for `timui_post`**: MPSC node allocation and
  free are serialized under the queue mutex, preserving the public "post from
  worker threads" contract even when the configured allocator is not reentrant.
- **Failed-init cleanup hardening**: id-stack capacity overflow, termios enter
  failures, and cell-buffer init overflow now leave their destination objects in
  a destroy-safe empty state.
- **OSC 8 hyperlink closure**: the renderer now closes any open hyperlink at the
  end of a diff frame so later cursor/sync/shell bytes are not accidentally part
  of the last link.
- **Cursor restoration**: screen exit always emits cursor-show, because focused
  input widgets can hide the cursor during a session even when startup did not
  request `TIMUI_FLAG_HIDE_CURSOR`.
- **Terminal lifecycle hardening**: `timui_open` now fails cleanly when raw-mode
  setup fails, restores the caller's original input-fd flags on close, restores
  previous signal handlers instead of resetting them to defaults, and closes any
  trace fd opened during a failed setup.
- **Transactional UI resize**: `timui_ui_resize` now allocates replacement
  buffers before touching the live buffers, so an out-of-memory error cannot
  leave `curr`, `prev`, and `ui->w/h` with divergent dimensions.
- **Allocator ownership during cell-buffer resize**: an existing buffer keeps
  the allocator that originally owns its storage when resized, instead of
  switching to the allocator passed to the resize call.
- **MPSC payload validation**: `timui_post` rejects `NULL` payloads when
  `size > 0`, preventing uninitialized queue payload bytes from being copied.
- **Multiple inline images collapsed to one**: every Kitty placement used `p=1`,
  so messages sharing an image (same id) each replaced the previous placement —
  only one showed. Each on-screen slot now gets a distinct placement id, and last
  frame's placements are cleared (atomic under sync) before re-placing so
  scrolled-away/shuffled images don't linger.
- **Drag-drop path truncated to 16 chars**: the parser emits one event per typed
  character (a terminal inserts a dropped path as plain text), but the event
  queue held only 16 — so a 73-char path lost all but `/Users/angerman/`. The
  queue now holds a whole read (512 > the 256-byte read buffer). Not a terminal
  setting; diagnosed with `TIMUI_TRACE`.
- **Bracketed paste never reached inputs**: `timui_begin` dropped `PASTE` events
  (drain handled MOUSE/KEY/TEXT only), so a real paste or a Finder drag-drop of a
  file path didn't land in the focused input. Now the frame accumulates a paste's
  content (it can span several reads → several events) and appends it to the
  focused input; `chat` renders a dropped `.png` path as an inline image. Added
  `TIMUI_TRACE=<file>` to trace raw input (drag-drop / paste debugging).
- **Kitty graphics under tmux**: caps now ALWAYS strip `TIMUI_CAP_KITTY_GRAPHICS`
  under a multiplexer (tmux/screen/zellij) — passthrough needs explicit tmux
  config we can't assume, and dropped graphics APC leaves a grey placeholder +
  stray cursor moves. New public `timui_caps(ui)` accessor. `examples/chat` uses
  it to draw a real inline image only when graphics work, else a one-line badge;
  its badge emoji is now U+1F4F7 📷 (emoji-presentation, width 2 everywhere)
  rather than the text-default U+1F5BC 🖼 whose ambiguous width caused artifacts.
- **Kitty graphics never worked**: `timui_image_draw` emitted `ESC G`, but the
  protocol requires the APC `ESC _ G` — no terminal recognised the image. It also
  transmitted mid-frame (clobbered by the cell diff) with no placement/sizing.
  Now: correct APC; transmit once (keyed by `TimuiImage.id`) + place each frame
  (stable `p=1`); placements flush AFTER the cell diff (composed on top), CUP'd
  to the rect and scaled to its cell size. Enables real inline images.
- **`input_field` submit batching**: several Enters in one frame (a paste, or
  input faster than the frame rate) merged into a single submission with the
  post-Enter text appended (`"a\rb\r"` → one `"ab"`). `timui_begin` now records
  each Enter's position in the text stream and `input_field` submits one segment
  per frame, deferring the tail — `"a"` then `"b"`. Single-bool API unchanged.
- **Dropped render bytes (screen garbling)**: `fd_write` did a single `write()`
  and ignored a short count. The output fd shares its file description with the
  O_NONBLOCK input fd, so under output pressure (heavy render + fast typing) the
  tty buffer fills and `write()` returns EAGAIN / a partial count, silently
  dropping the rest of the frame. It now loops (EINTR / EAGAIN-poll / partial)
  until every byte is written.
- **Auto-wrap desync**: `screen_enter` now disables DECAWM (`\x1b[?7l`, restored
  on exit) so a glyph written to the last column can't wrap the cursor or scroll
  the screen behind the diff renderer's back — the remaining right-edge
  corruption in fast-updating apps. Standard for cell-based TUIs.
- `examples/chat`: focuses the input on the first frame (no Tab needed).
- `examples/file_manager`: the file viewer is now a real read-only scrollable
  view (was an editable text_area that swallowed Space/keys, took focus on click,
  and wouldn't page-scroll).
- **Frame tearing**: `timui_end` now wraps each frame in synchronized output
  (DEC 2026, `\x1b[?2026h`/`l`) when the terminal advertises it, so a partial
  update never reaches the screen. Without it, a fast-updating app (chat,
  file_manager) showed half-drawn frames that read as interleaved corruption.
  Added a byte-stream verifier (a minimal VT model replaying the diff stream)
  that confirmed the renderer itself is correct.
- `examples/file_manager`: Enter now views files (was: broke the pane by scanning
  a file as a directory); F10 quits from anywhere including the viewer.
- **Diff-renderer / cursor desync**: a focused `input_field`/`text_area` emits
  its hardware cursor after `render_diff`, which moved the physical cursor off
  the renderer's tracked position; the next frame could then skip a cursor-
  positioning CUP and draw a cell at the wrong place — visible as garbled output
  in `chat` and `file_manager`'s file viewer. The renderer is now resynced after
  the cursor moves. (The grid/golden tests can't catch this — they compare cell
  content, not the emitted byte stream — so a deterministic byte-stream
  regression was added.)

### Added
- **`timui_image_draw_clipped`**: draw only the part of an image that lands in a
  visible sub-rect (Kitty source-crop from the PNG's `IHDR` pixel size), so an
  image clips smoothly at a pane edge. `examples/chat` uses it with LINE-based
  scrolling so tall image messages scroll and clip smoothly; clicking an image
  opens it fullscreen (aspect-fit) with its alt caption below.
- **`examples/chat --demo <script>`**: self-driving autoplay (a scripted
  timeline of `msg`/`say`/`img`/`scroll`/`open`/`close`/`wait`/`quit`) so a
  screen recording of the real terminal — the only way to capture the Kitty
  images — is hands-off and reproducible. `make run-chat-demo` / `rec-chat-demo`.
  See `docs/research/kitty-gif-renderer.md` for a headless GIF-pipeline plan.
- **App input accessors**: `timui_mouse_wheel(f)` (per-frame wheel delta),
  `timui_mouse_clicked(f,&x,&y)` (a click + its cell), and `timui_hyperlink_at(
  f,x,y)` (the OSC 8 URL under a cell) — so apps can scroll on the wheel and open
  a clicked link even when mouse reporting intercepts it.
- **`timui_input_field_styled`**: an `input_field` drawn with a caller style
  (blend into a panel) rather than the theme's input box.
- **`examples/chat`**: Up/Down message history; Shift+↑↓ / wheel / PgUp/PgDn
  scrolling; click a link to open it in the browser; composer blended into the
  panel with a ❯ accent.
- **Emacs / readline line-editing keys** in `input_field`: Ctrl-A/E (start/end),
  Ctrl-B/F (back/forward), Ctrl-D (delete), Ctrl-K/U (kill to end/start of line),
  Ctrl-W (kill previous word). Navigation keys reach `text_area` too.
- **`examples/chat` enrichments**: scrollback (1024-line ring; Up/Down + PgUp/PgDn
  with a header indicator), per-line HH:MM:SS timestamps, a markdown subset in
  messages (`*bold*`, `_italic_`, `` `code` ``), http(s):// URLs as OSC 8
  hyperlinks, and emoji/wide-glyph rendering — all over the existing public API
  (`timui_utf8_width`/`_decode`, `timui_label_hyperlink`, text attributes).
  Markdown images: remote `![alt](http…)` render as clickable `🖼 alt` badges
  (OSC 8); LOCAL `![alt](path.png)` render as real inline Kitty-graphics images
  (variable-height transcript, PNG cached per path). Claude-code-style composer:
  a ❯ prompt with a ─── rule, a dim (non-status) hint line, a "jump to bottom"
  affordance while scrolled (Ctrl+End / Enter), and a 1 KB message cap.
- **Recording & headless-driving tooling**: `make rec-<name>` (asciinema),
  `make drive-<name>` (scripted-input pty capture), `make accept` (acceptance
  smoke); `tools/pty_drive.c` + `tools/vt_render.c`.
- **`tools/vt_gif` — headless terminal→PNG/GIF renderer *with* Kitty images**
  (what asciinema/agg/VHS can't do). Replays a capture through a VT model that
  now carries SGR colour + attributes, base64-decodes the transmitted Kitty
  PNGs, and composites them (source-crop aware) onto a font-rasterized cell grid.
  `pty_drive --timing` adds a frame-pacing sidecar; `make gif-chat-demo` renders
  the autoplay demo to `recordings/chat-demo.gif`. Vendored single-headers (stb,
  msf_gif). `make check-vt-gif` smoke test. See `docs/research/kitty-gif-renderer.md`.
- **`vt_gif` v2 — full glyphs, CJK, colour emoji, output controls**: text now
  rasterizes via **stb_truetype** (bundled DejaVu Sans Mono subset → any glyph the
  font has, antialiased, at a runtime `--cell-h`/`--scale` size) through an ordered
  font **fallback chain**. **CJK** renders flag-free from a bundled **Unifont**
  bitmap face (`make gen-cjk`), or nicer antialiased CJK with `--system-fonts`
  (macOS Hiragino/AppleSDGothicNeo → Japanese/Chinese/Korean). **Colour emoji**
  render flag-free from a bundled **Twemoji** atlas (`make gen-emoji`), or native
  with `--system-emoji` (Apple Color Emoji `sbix`). Output: `--width` (aspect
  downscale), `--bit-depth`, `--frames-dir` (PNG sequence → `ffmpeg` MP4/WebP).
  Fixed a wide-glyph clipping bug (two-pass render: backgrounds then glyphs). A
  deterministic golden-PNG regression + `make check-vt-gif-all` (8 checks); plan in
  `docs/research/vt-gif-v2-plan.md`.
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
