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
wrappers. It does **not** use ncurses; a C99 compiler plus POSIX libc are
required. The default thread-safe post queue also links pthreads; define
`TIMUI_NO_THREADS` for a single-threaded build without pthread.

The public drop-in library is delivered as an amalgamated `timui.h`: download
`https://timui.dev/timui.h`, use `www/timui.h` from the website build, or
regenerate `release/timui.h` with the **amalgamate** target. The repo's
`include/timui.h` is the development header used by the split source tree.
Define `TIMUI_IMPLEMENTATION` in exactly one translation unit to emit the
implementation; every other unit just includes the header for declarations.

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
`TIMUI_NO_THREADS`, `TIMUI_NO_IMAGES`, and `TIMUI_API`. `TIMUI_NO_IMAGES`
keeps the image API but disables terminal image protocol caps and escapes.
`TIMUI_NO_STDIO` and `TIMUI_NO_UTF8_TABLES` are currently reserved
compatibility no-ops.

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
: The build-plus-test gate: runs **build**, **test**, no-image API coverage,
  Win32 ConPTY compile seams, hosted evidence-script checks, and static website
  license/link checks.

**test-san** \[*SAN=address*]
: Compile and run the unit tests under a sanitizer, e.g.
  `make test-san SAN=address` or `SAN=undefined`.

**vt-test**
: Run the unit tests together with the libvterm round-trip (Tier A) tests.
  Requires the neovim/Paul Evans libvterm API, provided by the flake as
  `libvterm-neovim`.

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

## Operator smokes

These targets exist to collect live evidence. They are intentionally outside
**check**; headless pty captures prove byte streams and final cell text, not
that a real terminal consumed an image protocol or that Windows ConPTY passed on
a Windows host. The canonical evidence procedure and recording template live in
`docs/runbooks/phase1-5-live-evidence.md`.

**check-image-smoke**
: Headless sanity check for the image smoke harness. It forces protocol `none`,
  `sixel`, and `iterm2`, drives `image_smoke` through a pty, and asserts the
  placeholder path, non-trivial Sixel rasters, and iTerm2 PNG-backed-only
  behavior.

**smoke-image-live** \[*PROTO=auto|kitty|sixel|iterm2|none*] \[*FRAMES=N*]
: Run `examples/image_smoke.c` in the current terminal. It draws a plain PNG,
  raw RGBA image, and PNG+RGBA sidecar so an operator can verify the selected
  terminal image protocol. In forced iTerm2 mode, the raw RGBA-only slot is
  labeled unsupported because this implementation emits OSC 1337 only for
  PNG-backed images. `FRAMES=N` exits after N drawn frames for bounded capture
  runs; without it, Escape exits. Convenience aliases are
  **smoke-image-live-auto**, **smoke-image-live-kitty**,
  **smoke-image-live-sixel**, **smoke-image-live-iterm2**, and
  **smoke-image-live-none**.

**check-conpty-win32-smoke-compile**
: Cross-compile the Win32 ConPTY smoke runner when MinGW is available. This is
  compile evidence only.

**check-conpty-smoke-tool**
: Compile and run the portable helper tests for the Win32 ConPTY smoke runner:
  script line-ending selection, sentinel matching, and helper behavior that can
  be checked without a Windows host.

**check-hosted-visual-windows**
: Verify the hosted Windows visual probe script still emits the ConPTY
  acceptance manifest (`conpty-acceptance.json`) and its command/meta sidecars.

**check-conpty-evidence-artifacts**
: Run synthetic positive and negative fixture tests for the hosted ConPTY
  evidence verifier. This proves the verifier rejects wrong commits, malformed
  JSON, false acceptance flags, nonzero status, missing PASS tokens, and unsafe
  manifest paths, plus compile-only commands or non-Windows metadata.

**verify-conpty-evidence**
: Validate a downloaded hosted Windows ConPTY artifact directory:
  `make verify-conpty-evidence ARTIFACT_DIR=... COMMIT=...`. The target checks
  `conpty-acceptance.json` and its stdout/stderr/status/meta/evidence sidecars
  against one expected commit, the real smoke target, and Windows host metadata.
  It does not create Windows evidence; it only accepts or rejects an artifact
  already collected from a Windows host.

**smoke-conpty-win32**
: Run the Win32 ConPTY smoke runner on Windows. It opens the default shell via
  `timui_conpty_open`, writes an echo sentinel through the transport, reads it
  back, resizes once, and closes twice. A non-Windows skip is not Windows
  evidence.

## Release header

**amalgamate**
: Regenerate the flat, single-file release header into `release/timui.h`.

**release-check**
: Regenerate the release header and verify it compiles standalone.

**www**
: Regenerate the amalgamated release header and refresh `www/timui.h` plus
  `www/LICENSE` for the static website.

**check-www**
: Verify the static website and `llms.txt` expose the Apache-2.0 license link,
  and that `www/LICENSE` matches the repository `LICENSE`.

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
: The repository development header for the split source tree.

*release/timui.h*
: The generated flat release header (see **amalgamate**). Public consumers
  should use this shape.

*Makefile*
: The self-documenting build interface; the sole supported entry point.

*flake.nix*
: The nix dev shell providing the toolchain (clang, gnumake, pkg-config,
  asciinema, and libvterm-neovim).

*examples/*
: Example applications — `hello`, `counter`, `form`, `mini_commander`,
  `editor`, `file_manager`, `todo`, `procmon`, `chat`.

*tests/golden/*
: Golden cell-buffer snapshots regenerated by **goldens**.

*docs/*
: Project documentation — current references include `API.md`, `DESIGN.md`,
  `THREADING.md`, `visual-tests.md`, and `decisions/`; historical planning
  notes also live here.

*build/timui.1*
: This man page, rendered to roff by **man**.

# SEE ALSO

**make**(1), **pandoc**(1), **asciinema**(1), **cc**(1)

Project documentation: `README.md`, `USAGE.md`, and the `docs/` directory
(`docs/API.md`, `docs/DESIGN.md`, `docs/THREADING.md`, `docs/visual-tests.md`).
