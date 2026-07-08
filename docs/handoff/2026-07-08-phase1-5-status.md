---
type: Report
title: Phase 1.5 Platform Widgets Style Text Image Status
date: 2026-07-08
---

# Phase 1.5 Status

## Landing State

- Branch: `phase1-5-impl`
- Local master checkpoint before this slice: `3a28af1` (`images: scale raw rgba sixel output`)
- Merge status: `phase1-5-impl` fast-forwarded into local
  `/Users/angerman/Projects/zw3rk/timui.h-master`
- Push status: not pushed
- Remote state before this slice: local `master` is ahead of `github/master` by
  19 commits

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
- Image implementation source is now the protocol-neutral `src/timui_images.c`
  module; Kitty-specific names remain only for Kitty protocol helpers/tests.
- Kitty graphics remain the rich PNG path, including clipped image draws.
- iTerm2 inline images emit OSC 1337 for unclipped PNG draws.
- Sixel emits DCS graphics for raw RGBA images with up to 16 opaque exact
  colours and deterministic 16-colour quantization beyond that cap; alpha below
  128 is transparent/background-preserving. Clipped raw-RGBA Sixel draws crop
  source pixels, and raw-RGBA Sixel draws scale to the requested cell rectangle
  when terminal cell-pixel geometry is known.
- PNG images forced to Sixel, PNG/non-raw clipped Sixel draws, and clipped
  iTerm2 draws intentionally render `[img]`.
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
- Sixel parity remains open: PNG-to-Sixel decode and non-raw clipped Sixel
  draws.
- AddressSanitizer did not complete locally: `nix develop -c make test-san
  SAN=address` hung in macOS ASAN runtime initialization before entering the
  test harness. A `sample` of the process showed `__asan::AsanInitInternal` /
  `__sanitizer::MemoryRangeIsAvailable`; the run was interrupted.

## Verification Already Run

- `nix develop -c make test` - passed, 295 tests, existing pty Esc sandbox skip.
- `nix develop -c make check-conpty` - passed, including POSIX fallback/helper
  tests and the isolated MinGW Win32 ConPTY compile seam.
- `nix develop -c make check` - passed after the raw-RGBA Sixel scaling change,
  including build, 295 tests, and the MinGW ConPTY compile seam.
- `nix develop -c make check-vt-gif-all` - passed after the raw-RGBA Sixel
  scaling change.
- `nix develop -c make release-check` - passed after the raw-RGBA Sixel scaling
  header changes.
- `nix develop -c make test-san SAN=undefined` - passed after the raw-RGBA Sixel
  scaling change, 295 tests, existing pty Esc sandbox skip.
- `nix flake check` - passed for the current system; Nix reported incompatible
  non-current systems omitted unless `--all-systems` is used.
- Earlier in the same image sweep:
  `nix develop -c make build check-layout check-grid check-tabs
  check-chat-text check-chat-highlight release-check check-vt-gif-all
  smoke-gallery check-irc smoke-irc` - passed.
- `nix develop -c make vt-test` - passed after the raw-RGBA Sixel scaling
  change, 303 tests, existing pty Esc sandbox skip.
- `nix develop -c make test` - passed after the protocol-neutral image source
  rename, 295 tests, existing pty Esc sandbox skip.
- `nix develop -c make release-check` - passed after the protocol-neutral image
  source rename.
- `nix develop -c make check` - passed after the protocol-neutral image source
  rename, including build, 295 tests, and the MinGW ConPTY compile seam.

## Next Safe Move

Use a Windows-capable worktree or CI worker to run a live Windows Terminal smoke
against `timui_conpty_open`, transport read/write, resize, and close. Separately,
capture live iTerm2 and Sixel terminal evidence before promoting image protocol
support from fake-transport wire evidence to terminal evidence. For Sixel parity,
PNG-to-Sixel remains blocked on a public-library PNG decode dependency decision;
`tools/vendor` currently contains dev-tooling-only `stb` use, not a release
header dependency.
