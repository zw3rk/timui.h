% TIMUI(1) | timui.h 0.2.0
% Moritz Angermann
% 2026-07-07

<!--
SPDX-License-Identifier: Apache-2.0
Copyright 2026 Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.
-->


# NAME

timui.h — single-header C99 immediate-mode TUI for modern terminals

# SYNOPSIS

**nix develop -c make** \[*TARGET*] \[*VAR=VALUE* ...]

**nix develop -c make** *run-NAME* | *rec-NAME* | *drive-NAME*

In C, as a single-header drop-in:

    #define TIMUI_IMPLEMENTATION
    #include "timui.h"

# DESCRIPTION

**timui.h** is a single-header, C99, immediate-mode terminal-UI library for
modern terminals (Ghostty, kitty, WezTerm, Alacritty, foot, Rio, …) with a
safe ANSI fallback for everything else. It has a DOS / Midnight-Commander
aesthetic and is functional / immutable by default, with `_mut` convenience
wrappers. It does **not** use ncurses; a C99 compiler and libc are the only
hard dependencies.

The library is delivered as `include/timui.h`. Define `TIMUI_IMPLEMENTATION`
in exactly one translation unit to emit the implementation; every other unit
just includes the header for declarations. A flat, amalgamated release header
can be regenerated into `release/timui.h` (see the **amalgamate** target).

All runtime state lives in a `Timui` handle and per-frame state in a
`TimuiFrame`; there is no hidden global mutable state. The app redraws every
frame — `timui_begin` then declare widgets then `timui_end` — and a truecolour diff
renderer emits only the cells that changed (identical frames emit nothing).
Input is parsed by an incremental state machine that handles legacy CSI, the
Kitty keyboard protocol, SGR mouse, bracketed paste, and focus events, and is
safe across partial sequences. Worker threads post messages to the UI thread
through a thread-safe MPSC queue (`timui_post`); the UI thread is the single
writer.

The project's sole interface is the self-documenting `Makefile`, which is meant
to be driven through the nix dev shell: `nix develop -c make <target>`. Running
`make` with no target (or `make help`) prints the coloured target menu. Do not
invoke the C toolchain directly outside `nix develop`.

Feature macros recognised by the header include `TIMUI_IMPLEMENTATION`,
`TIMUI_NO_STDIO`, `TIMUI_NO_THREADS`, `TIMUI_NO_IMAGES`, `TIMUI_NO_UTF8_TABLES`,
and `TIMUI_API`.

# TARGETS

The following `make` targets are the primary interface. Each is invoked as
`nix develop -c make <target>`.

## Building and testing

**help**
: Print the coloured, self-documenting target menu (the default goal).

**build**
: Compile every example under `examples/` in single-header mode into `build/`.

**test**
: Compile and run the unit test suite (`build/test_unit`).

**check**
: The build-plus-test gate: runs **build** then **test**.

**test-san** \[*SAN=address*]
: Compile and run the unit tests under a sanitizer, e.g.
  `make test-san SAN=address` or `SAN=undefined`.

**vt-test**
: Run the unit tests together with the libvterm round-trip (Tier A) tests.
  Requires libvterm, which in nixpkgs is Linux-only; on macOS the target
  reports the missing dependency and the core targets still work.

**goldens**
: Regenerate the Tier B golden cell-buffer snapshots under `tests/golden/*.txt`.
  Review the resulting `git diff` when rendering intentionally changes.

## Running examples

**run**
: Build and run `examples/hello` (press Esc to quit).

**run-*NAME***
: Build and run one example by name, e.g. `make run-editor`,
  `make run-chat`, `make run-todo`, `make run-procmon`,
  `make run-file_manager`, `make run-counter`, `make run-form`,
  `make run-mini_commander`.

**rec-*NAME***
: Record a real interactive session of an example to
  `recordings/NAME.cast` with asciinema (you drive it; quit with F10/Esc).

**drive-*NAME***
: Drive an example headless in a pty, feeding scripted keystrokes from
  `recordings/NAME.in` (a raw byte file; missing means none), capturing the output
  stream to `recordings/NAME.raw`, and rendering the final screen to
  `recordings/NAME.txt`.

**accept**
: Headless acceptance smoke test: drives `editor` with the checked-in
  `tests/drive/editor.in` and asserts the typed text renders. Timing-dependent,
  so it is deliberately outside **check**.

## Release header

**amalgamate**
: Regenerate the flat, single-file release header into `release/timui.h`.

**release-check**
: Regenerate the release header and verify it compiles standalone.

## Recording, GIF/PNG capture (vt_gif)

**gif-chat-demo**
: Fully headless: autoplay the `chat --demo` script and rasterize it — text,
  colour, and composited Kitty images — into `recordings/chat-demo.gif`.

**webp-chat-demo**
: Also produce `recordings/chat-demo.mp4` and `recordings/chat-demo.webp`,
  both far smaller than the GIF.

**check-vt-gif-all**
: Run every `vt_gif` renderer check (smoke, extended glyphs, style, CJK,
  colour emoji, output controls, golden PNG). Individual checks
  (**check-vt-gif**, **check-vt-gif-cjk**, **check-vt-gif-emoji**, …) are also
  available.

**check-chat-text**, **check-chat-highlight**
: Standalone unit tests for the `chat` example's text/word-wrap helpers and
  its C99 syntax highlighter.

**gen-font-ttf**, **gen-emoji**, **gen-cjk**
: Regenerate the bundled vendored faces used by `vt_gif`
  (`tools/vendor/vt_font_ttf.h`, `emoji_atlas.h`, `vt_font_cjk.h`) via nix.

## Documentation

**man**
: Render this page to a roff man page at `build/timui.1` with pandoc.

**install-man** \[*PREFIX=/usr/local*] \[*DESTDIR=*]
: Install `build/timui.1` to `$(DESTDIR)$(PREFIX)/share/man/man1/timui.1`.

## Housekeeping

**fmt**
: Format the C sources with clang-format if it is available.

**clean**
: Remove the `build/` and `release/` directories.

# EXAMPLES

Print the target menu:

    nix develop -c make

Build the examples and run the full unit test suite:

    nix develop -c make check

Build and run the Midnight-Commander-style dual-pane file browser:

    nix develop -c make run-file_manager

Run the unit tests under the AddressSanitizer:

    nix develop -c make test-san SAN=address

Render this man page and read it locally:

    nix develop -c make man
    man -l build/timui.1

Install the man page under a staging root:

    nix develop -c make install-man DESTDIR=/tmp/stage PREFIX=/usr

Produce an animated GIF of the chat demo, Kitty images and all, with no
external screen recorder:

    nix develop -c make gif-chat-demo

# FILES

*include/timui.h*
: The single-header library (declarations plus, under
  `TIMUI_IMPLEMENTATION`, the implementation).

*release/timui.h*
: The generated flat release header (see **amalgamate**).

*Makefile*
: The self-documenting build interface; the sole supported entry point.

*flake.nix*
: The nix dev shell providing the toolchain (clang, gnumake, pkg-config,
  asciinema, and libvterm on Linux).

*examples/*
: Example applications — `hello`, `counter`, `form`, `mini_commander`,
  `editor`, `file_manager`, `todo`, `procmon`, `chat`.

*tests/golden/*
: Golden cell-buffer snapshots regenerated by **goldens**.

*docs/*
: Project documentation — `PRD.md`, `DESIGN.md`, `API.md`, `THREADING.md`,
  `DECISIONS.md`, and `visual-tests.md`.

*build/timui.1*
: This man page, rendered to roff by **man**.

# SEE ALSO

**make**(1), **pandoc**(1), **asciinema**(1), **cc**(1)

Project documentation: `README.md`, `USAGE.md`, and the `docs/` directory
(`docs/DESIGN.md`, `docs/API.md`, `docs/THREADING.md`, `docs/PRD.md`,
`docs/visual-tests.md`).
