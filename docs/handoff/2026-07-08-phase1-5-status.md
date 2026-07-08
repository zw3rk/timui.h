---
type: Report
title: Phase 1.5 Platform Widgets Style Text Image Status
date: 2026-07-08
---

# Phase 1.5 Status

## Landing State

- Branch: `phase1-5-impl`
- Local master checkpoint: `a775e86` (`www: add license section`)
- Merge status: `phase1-5-impl` fast-forwarded into local
  `/Users/angerman/Projects/zw3rk/timui.h-master`
- Push status: not pushed
- Remote state: local `master` is ahead of `github/master` by 15 commits

## Read First

- `docs/goals/phase1_5-platform-widgets-style-text-image.goal.txt`
- `docs/backlog.md`
- `docs/gaps.md`
- `docs/API.md`
- `docs/TERMINAL_PROTOCOLS.md`
- `docs/research/image-protocols/REVIEW.md`
- `docs/research/conpty/REVIEW.md`

## Accepted State

- Submit-capable textarea, combobox/autocomplete, toast notifications,
  resizable split panes, stylesheet parser/application, and grapheme-aware text
  editing/truncation are implemented and tested.
- Image protocol selection is protocol-neutral at the public draw site.
- Kitty graphics remain the rich PNG path, including clipped image draws.
- iTerm2 inline images emit OSC 1337 for unclipped PNG draws.
- Sixel emits DCS graphics for raw RGBA images with up to 16 opaque exact
  colours; alpha below 128 is transparent/background-preserving. Clipped
  raw-RGBA Sixel draws crop and emit cropped DCS payloads.
- PNG images forced to Sixel, over-palette Sixel images, PNG/non-raw clipped
  Sixel draws, and clipped iTerm2 draws intentionally render `[img]`.
- Win32 ConPTY is implemented behind `_WIN32`, runtime-probed for
  `CreatePseudoConsole`/`ResizePseudoConsole`/`ClosePseudoConsole`, and covered
  by POSIX fallback/helper tests plus a MinGW compile seam in `make check`.
- `www/index.html` now includes a standalone `LICENSE` section, and
  `www/llms.txt` includes both the license URL and an explicit license section.
- `www/timui.h` was refreshed by `nix develop -c make www`.

## Blockers And Pickup Points

- Windows ConPTY has compile evidence only. This session ran on macOS; do not
  claim supported Windows operation until a real Windows Terminal smoke run is
  captured and recorded.
- iTerm2 and Sixel have fake-transport wire tests, but no live terminal capture
  evidence yet. Do not claim terminal evidence until captured.
- Sixel parity remains open: PNG-to-Sixel decode, palette quantization, scaling
  to cell geometry, and non-raw clipped Sixel draws.
- AddressSanitizer did not complete locally: `nix develop -c make test-san
  SAN=address` hung in macOS ASAN runtime initialization before entering the
  test harness. A `sample` of the process showed `__asan::AsanInitInternal` /
  `__sanitizer::MemoryRangeIsAvailable`; the run was interrupted.

## Verification Already Run

- `nix develop -c make test` - passed, 290 tests, existing pty Esc sandbox skip.
- `nix develop -c make check-conpty` - passed, including POSIX fallback/helper
  tests and the isolated MinGW Win32 ConPTY compile seam.
- `nix develop -c make check` - passed after the raw-RGBA Sixel clipping change,
  including build, 290 tests,
  and the MinGW ConPTY compile seam.
- `nix develop -c make check-vt-gif-all` - passed after the image emission change.
- `nix develop -c make release-check` - passed after Sixel/header changes.
- `nix develop -c make test-san SAN=undefined` - passed, 290 tests, existing pty
  Esc sandbox skip.
- `nix flake check` - passed for the current system; Nix reported incompatible
  non-current systems omitted unless `--all-systems` is used.
- Earlier in the same image sweep:
  `nix develop -c make build check-layout check-grid check-tabs
  check-chat-text check-chat-highlight release-check check-vt-gif-all
  smoke-gallery check-irc smoke-irc` - passed.
- `nix develop -c make vt-test` - passed, 287 tests, existing pty Esc sandbox
  skip.

## Next Safe Move

Use a Windows-capable worktree or CI worker to run a live Windows Terminal smoke
against `timui_conpty_open`, transport read/write, resize, and close. Separately,
capture live iTerm2 and Sixel terminal evidence before promoting image protocol
support from fake-transport wire evidence to terminal evidence.
