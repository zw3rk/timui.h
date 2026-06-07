# timui.h

> Single-header **C99** immediate-mode TUI for modern terminals (Ghostty, kitty, …) with a safe ANSI fallback. DOS / Midnight-Commander aesthetic. Functional / immutable by default, with `_mut` convenience wrappers. No ncurses.

**Status:** v0.1 core is implemented and tested (87 unit tests green) — a working
immediate-mode TUI, not a scaffold:

- **Terminal backend:** POSIX raw mode, screen-mode setup/teardown, terminal-size
  query, capability detection (modern-terminal allowlist + multiplexer reduction),
  synchronized output (DEC 2026).
- **Input:** legacy + Kitty keyboard protocol, SGR mouse (button/wheel/motion),
  bracketed paste, focus events — incremental, partial-sequence-safe.
- **Rendering:** UTF-8 decode + minimal width, drawing primitives, truecolour
  **diff renderer** (unchanged frames emit nothing), cursor placement.
- **Interaction + widgets:** focus/hot/active, tab cycling, themed styles
  (DOS-blue/gray/modern-dark/mono), button, checkbox, radio, input-line,
  listbox, panel/label, function bar, message box — controlled + `_mut`.
- **Examples:** `hello`, `counter` (functional), `form`, `mini_commander`.

See [docs/PRD.md](docs/PRD.md) (roadmap, §15 progress) and [docs/DECISIONS.md](docs/DECISIONS.md).

## Build & test

Everything goes through the nix dev shell + Makefile:

    nix develop -c make          # menu
    nix develop -c make check    # build examples + run tests
    nix develop -c make run      # build & run examples/hello

A C99 compiler and libc are the only hard dependencies.

## Single-header drop-in

    #define TIMUI_IMPLEMENTATION
    #include "timui.h"

Define `TIMUI_IMPLEMENTATION` in exactly one translation unit. See [USAGE.md](USAGE.md), [docs/PRD.md](docs/PRD.md), and [docs/DECISIONS.md](docs/DECISIONS.md).

## License

Apache-2.0. Copyright Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
