# timui.h

> Single-header **C99** immediate-mode TUI for modern terminals (Ghostty, kitty, …) with a safe ANSI fallback. DOS / Midnight-Commander aesthetic. Functional / immutable by default, with `_mut` convenience wrappers. No ncurses.

**Status:** Phase 0 scaffold. The foundational types and the pure layout/id/string helpers are implemented and unit-tested; the terminal, rendering, and widget stack land in later phases (see the [docs/PRD.md](docs/PRD.md) roadmap).

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
