# timui.h

> Single-header **C99** immediate-mode TUI for modern terminals (Ghostty, kitty, …) with a safe ANSI fallback. DOS / Midnight-Commander aesthetic. Functional / immutable by default, with `_mut` convenience wrappers. No ncurses.

**Status:** v0.2.0 — a working immediate-mode TUI, not a scaffold. The unit
suite is green; CI also runs ASAN/UBSAN/TSAN, golden staleness checks, and the
Tier A libvterm round-trip gate. See [CHANGELOG.md](CHANGELOG.md):

- **Terminal backend:** POSIX raw mode, screen-mode setup/teardown, terminal-size
  query, capability detection (modern-terminal allowlist + multiplexer reduction),
  synchronized output (DEC 2026).
- **Input:** legacy + Kitty keyboard protocol, SGR mouse (button/wheel/motion),
  bracketed paste, focus events — incremental, partial-sequence-safe.
- **Rendering:** UTF-8 decode + minimal width, drawing primitives, truecolour
  **diff renderer** (unchanged frames emit nothing), cursor placement.
- **Interaction + widgets:** focus/hot/active, tab cycling, themed styles
  (DOS-blue/gray/modern-dark/mono), button, checkbox, radio, input-line +
  **`input_field`/`text_area` with in-line cursor editing**, listbox, tree,
  table, command palette, panel/label, function bar, menu bar, message box —
  controlled + `_mut`.
- **Examples:** `hello`, `counter` (functional), `form`, `mini_commander`,
  `editor`, `file_manager`, `todo`, `procmon`, `chat`, `gallery`, `irc`,
  `radio`, and `sqlite_tui`. Run one with `make run-<name>`.

See [USAGE.md](USAGE.md), [docs/API.md](docs/API.md), and
[docs/visual-tests.md](docs/visual-tests.md). Historical planning notes live
under `docs/`. The website build lives in `www/` and publishes the downloadable
amalgamated header as `www/timui.h`.

## Build & test

Everything goes through the nix dev shell + Makefile:

    nix develop -c make          # menu
    nix develop -c make check    # build examples + run tests
    nix develop -c make run      # build & run examples/hello
    nix develop -c make release VERSION=0.2.0

A C99 compiler plus POSIX libc are required. The default thread-safe post queue
also links pthreads; define `TIMUI_NO_THREADS` for a single-threaded build
without pthread.

## Single-header drop-in

Download the release header from <https://timui.dev/timui.h> or use
`www/timui.h` after `make www`. The repo's `include/timui.h` is the development
header used by the split source tree.

    #define TIMUI_IMPLEMENTATION
    #include "timui.h"
    TimuiConfig cfg = TIMUI_CONFIG_INIT;

Define `TIMUI_IMPLEMENTATION` in exactly one translation unit. See
[USAGE.md](USAGE.md) and [docs/API.md](docs/API.md).

## License

Apache-2.0. Copyright Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
