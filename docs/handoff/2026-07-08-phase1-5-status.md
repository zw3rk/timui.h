---
type: Report
title: Phase 1.5 Platform Widgets Style Text Image Status
date: 2026-07-08
---

# Phase 1.5 Status

## Landing State

- Branch: `phase1-5-impl`
- Local master checkpoint: `4a3767d` (`images: add raw rgba sixel output`)
- Merge status: `phase1-5-impl` fast-forwarded into local
  `/Users/angerman/Projects/zw3rk/timui.h-master`
- Push status: not pushed
- Remote state: local `master` is ahead of `github/master`

## Read First

- `docs/goals/phase1_5-platform-widgets-style-text-image.goal.txt`
- `docs/backlog.md`
- `docs/gaps.md`
- `docs/API.md`
- `docs/TERMINAL_PROTOCOLS.md`
- `docs/research/image-protocols/REVIEW.md`

## Accepted State

- Submit-capable textarea, combobox/autocomplete, toast notifications,
  resizable split panes, stylesheet parser/application, and grapheme-aware text
  editing/truncation are implemented and tested.
- Image protocol selection is protocol-neutral at the public draw site.
- Kitty graphics remain the rich PNG path, including clipped image draws.
- iTerm2 inline images emit OSC 1337 for unclipped PNG draws.
- Sixel emits DCS graphics for raw RGBA images with up to 16 opaque exact
  colours; alpha below 128 is transparent/background-preserving.
- PNG images forced to Sixel, over-palette Sixel images, clipped Sixel draws,
  and clipped iTerm2 draws intentionally render `[img]`.
- `www/timui.h` was refreshed by `nix develop -c make www`.

## Blockers And Pickup Points

- Windows ConPTY remains unsupported at runtime. There is no current Makefile or
  flake target for mingw/Windows compile checks in this repo, and this session
  ran on macOS. Do not claim Windows support until a `_WIN32` build and a real
  Windows Terminal smoke run pass.
- iTerm2 and Sixel have fake-transport wire tests, but no live terminal capture
  evidence yet. Do not claim terminal evidence until captured.
- Sixel parity remains open: PNG-to-Sixel decode, palette quantization, scaling
  to cell geometry, and clipped Sixel draws.
- AddressSanitizer did not complete locally: `nix develop -c make test-san
  SAN=address` hung in macOS ASAN runtime initialization before entering the
  test harness. A `sample` of the process showed `__asan::AsanInitInternal` /
  `__sanitizer::MemoryRangeIsAvailable`; the run was interrupted.

## Verification Already Run

- `nix develop -c make test` - passed, 286 tests, existing pty Esc sandbox skip.
- `nix develop -c make build release-check check-vt-gif-all` - passed.
- `nix develop -c make test-san SAN=undefined` - passed, 286 tests, existing pty
  Esc sandbox skip.
- Earlier in the same image sweep:
  `nix develop -c make build check-layout check-grid check-tabs
  check-chat-text check-chat-highlight release-check check-vt-gif-all
  smoke-gallery check-irc smoke-irc` - passed.
- `nix develop -c make vt-test` - passed, 287 tests, existing pty Esc sandbox
  skip.

## Next Safe Move

Use a Windows-capable worktree or CI worker. First add compile-only seams/tests
for the `_WIN32` ConPTY implementation, then implement the handle lifecycle and
transport chunking, then record a real Windows Terminal smoke run before moving
the Windows backlog item out of "blocked".
