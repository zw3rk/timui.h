---
type: Report
title: Phase 1.5 Platform Widgets Style Text Image Status
date: 2026-07-08
---

# Phase 1.5 Status

## Landing State

- Branch: `phase1-5-kitty-transmit`
- Latest implementation checkpoint: `61705f1`
  (`images: decode plain png for sixel`)
- Latest integrated local master checkpoint before this ASAN follow-up:
  `3ab8944` (`docs: align phase 1.5 evidence copy`)
- Previous local master checkpoint before this slice:
  `5392c72` (`smoke: add phase 1.5 operator harnesses`)
- Merge status: fast-forwarded into local
  `/Users/angerman/Projects/zw3rk/timui.h-master` at `ca74b88`; this
  evidence-copy update is a follow-up docs checkpoint
- Push status: not pushed
- Remote state before this slice: local `master` was ahead of `github/master`
  by 25 commits

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
  128 is transparent/background-preserving. Clipped Sixel draws crop source
  pixels, and Sixel draws scale to the requested cell rectangle when terminal
  cell-pixel geometry is known.
- `timui_image_from_png_rgba` copies original PNG bytes plus caller-supplied
  decoded RGBA rows. Kitty/iTerm2 transmit the PNG bytes; Sixel uses the RGBA
  sidecar for emission and clipping. The sidecar dimensions are expected to
  match the PNG and drive source cropping.
- Plain PNG images forced to Sixel now decode lazily through the bounded
  PNG-only `stb_image` path and then use the same RGBA Sixel encoder as raw
  RGBA and PNG+RGBA sidecars. Malformed/oversized PNGs, clipped iTerm2 draws,
  and unsupported protocol/data pairs intentionally render `[img]`.
- `TIMUI_NO_IMAGES` is now active as an API-preserving no-terminal-image mode:
  image constructors/free/draw APIs still compile, image caps are stripped even
  when forced on, protocol selectors return `TIMUI_IMAGE_PROTOCOL_NONE`, and
  draws render `[img]` without Kitty, iTerm2, or Sixel escapes.
- `make check` now includes `check-no-images`, and `make release-check`
  compiles the amalgamated release header both normally and with
  `TIMUI_NO_IMAGES`.
- Win32 ConPTY is implemented behind `_WIN32`, runtime-probed for
  `CreatePseudoConsole`/`ResizePseudoConsole`/`ClosePseudoConsole`, and covered
  by POSIX fallback/helper tests plus a MinGW compile seam in `make check`.
- Operator smoke harnesses now exist for the remaining live gates:
  `make smoke-image-live PROTO=auto|kitty|sixel|iterm2|none` renders a valid
  PNG, raw RGBA, and PNG+RGBA sidecar in a real terminal; `make
  smoke-conpty-win32` runs the ConPTY sentinel round-trip inside Windows
  Terminal. The matching compile/headless checks are deterministic, but they do
  not count as live evidence.
- `www/index.html` now includes a standalone `LICENSE` section, and
  `www/llms.txt` includes both the license URL and an explicit license section.
- `make check` now includes `check-www`, which verifies the website license
  section, `llms.txt` license URL, SPDX marker in `www/timui.h`, and that
  `www/LICENSE` matches the repository `LICENSE`.
- `www/timui.h` was refreshed by `nix develop -c make www`.

## Blockers And Pickup Points

- Windows ConPTY has compile evidence only. This session ran on macOS; do not
  claim supported Windows operation until a real Windows Terminal smoke run is
  captured and recorded.
- iTerm2 and Sixel have fake-transport wire tests, but no live terminal capture
  evidence yet. Use `make smoke-image-live PROTO=sixel` and `make
  smoke-image-live PROTO=iterm2` outside tmux/screen/zellij, then record the
  terminal, command, terminal version, and outcome before claiming evidence.
  Add `FRAMES=N` for bounded capture runs; omit it for an Escape-driven
  operator session.
- Plain PNG images forced to Sixel now use a bounded PNG-only `stb_image`
  decoder and the existing RGBA Sixel encoder. Caller-supplied PNG+RGBA sidecars
  still cover apps that already have decoded pixels and want to avoid lazy
  decode.
- AddressSanitizer did not complete locally: `nix develop -c make test-san
  SAN=address` hung in macOS ASAN runtime initialization before entering the
  test harness. A `sample` of the process showed `__asan::AsanInitInternal` /
  `__sanitizer::MemoryRangeIsAvailable`; the run was interrupted. A follow-up
  run on `ca74b88` was still silent after roughly 90 seconds and was also
  interrupted. A substituted system-clang attempt,
  `nix develop -c make test-san SAN=address CC=/usr/bin/clang`, also stayed
  silent for roughly 90 seconds and was interrupted, so ASAN remains weak
  evidence rather than accepted.

## Verification Already Run

- `nix develop -c make build` - passed on `ca74b88`.
- `nix develop -c make test` - passed on `ca74b88`, 307 tests, existing pty Esc
  sandbox skip.
- `nix develop -c make check check-layout check-grid check-tabs check-chat-text
  check-chat-highlight check-irc smoke-irc smoke-gallery check-image-smoke
  vt-test release-check check-vt-gif-all` - passed on `ca74b88`; this covered
  build, 307 tests, `check-no-images` (26 checks), both Win32 ConPTY compile
  seams, website license/link checks, layout/grid/tabs, chat text/highlight,
  IRC parser and smoke, gallery smoke, image smoke harness, vt-tests (315
  tests), release header compile, and all vt_gif renderer checks.
- `nix develop -c make test-san SAN=address` - rerun on `ca74b88`; produced no
  output for roughly 90 seconds and was interrupted. Do not count ASAN as green
  from this macOS runner.
- `nix develop -c make www check-www test-san SAN=undefined` - passed after the
  website evidence-copy update; refreshed `www/timui.h` and `www/LICENSE`,
  verified website license links, and passed UBSAN with 307 tests.
- `nix develop -c make test-san SAN=address CC=/usr/bin/clang` - tried as a
  system-clang substitution after the evidence-copy update; produced no output
  for roughly 90 seconds and was interrupted. This is diagnostic only, not
  accepted ASAN evidence.
- `/usr/bin/perl -e 'alarm shift; exec @ARGV' 3 nix develop -c make
  smoke-image-live-none FRAMES=1` - intentionally timed out before the live
  smoke target passed `FRAMES` through to `examples/image_smoke.c`.
- `/usr/bin/perl -e 'alarm shift; exec @ARGV' 20 nix develop -c make
  smoke-image-live-none FRAMES=1` - passed after wiring `FRAMES=N` into the
  target. Redirected-output checks for `smoke-image-live-sixel FRAMES=1` and
  `smoke-image-live-iterm2 FRAMES=1` also exited cleanly; the captured Sixel
  stream contained DCS starts and the captured iTerm2 stream contained OSC 1337
  markers. These are bounded target diagnostics, not live terminal evidence.
- `nix develop -c make test` - intentionally failed before the built-in
  PNG-to-Sixel decode slice: `test_sixel_plain_png_decodes_to_dcs` and
  `test_sixel_clipped_png_decodes_and_crops` rendered the placeholder instead
  of Sixel DCS.
- `nix develop -c make test` - passed after adding bounded plain-PNG Sixel
  decode, 307 tests, existing pty Esc sandbox skip when present.
- `nix develop -c make release-check` - passed after promoting `stb_image.h`
  into the amalgamated release header as the PNG-only decoder.
- `nix develop -c make www` - passed after the PNG decoder slice, refreshing
  `www/timui.h` and `www/LICENSE`.
- `nix develop -c make check-no-images` - passed after the PNG decoder slice;
  `TIMUI_NO_IMAGES` still strips image caps/escapes and keeps API stubs.
- `nix develop -c make check-image-smoke` - passed after the PNG decoder slice;
  this remains a headless harness sanity check, not terminal image evidence.
- `nix develop -c make check` - passed after the PNG decoder slice: build, 307
  tests, `check-no-images` (26 checks), both Win32 ConPTY compile seams, and
  website license/link checks.
- `nix develop -c make check-vt-gif-all` - passed after the PNG decoder slice;
  Pillow emitted the existing `Image.Image.getdata` deprecation warning.
- `nix develop -c make test-san SAN=undefined` - passed after the PNG decoder
  slice, 307 tests, existing pty Esc sandbox skip.
- `nix develop -c make check-www` - passed after the PNG decoder slice.
- `nix develop -c make test` - intentionally failed before the sidecar bounds
  fix: `test_sixel_rejects_short_strided_sidecar` emitted Sixel instead of the
  placeholder for a crafted short reported `rgba_len` with a large stride.
- `nix develop -c make test` - passed after the PNG+RGBA sidecar slice, 304
  tests, existing pty Esc sandbox skip.
- `nix develop -c make www` - passed after the PNG+RGBA sidecar slice,
  refreshing `www/timui.h` and `www/LICENSE`.
- `nix develop -c make release-check` - passed after the PNG+RGBA sidecar
  slice; the release header compiles standalone.
- `nix develop -c make check` - passed after the PNG+RGBA sidecar slice:
  build, 304 tests, `check-no-images` (26 checks), and the MinGW ConPTY compile
  seam.
- `nix develop -c make check-vt-gif-all` - passed after the PNG+RGBA sidecar
  slice; Pillow emitted a deprecation warning in `tools/vtg_probe.py`.
- `nix develop -c make test-san SAN=undefined` - passed after the PNG+RGBA
  sidecar slice, 304 tests, existing pty Esc sandbox skip.
- `nix develop -c make check-image-smoke` - passed after adding the live image
  smoke harness; this is a headless harness sanity check, not terminal image
  evidence.
- `nix develop -c make check-conpty-win32-smoke-compile` - passed after adding
  the Win32 ConPTY smoke runner; this is compile evidence, not live Windows
  evidence.
- `nix develop -c make check-www` - passed after strengthening the website and
  `llms.txt` license sections.
- `nix develop -c make check` - passed after adding `check-www`: build, 304
  tests, `check-no-images` (26 checks), both Win32 ConPTY compile seams, and
  website license/link checks.
- `nix develop -c make man` - passed after documenting `check-www`.
- `nix develop -c make test` - passed while verifying the smoke-harness slice,
  304 tests, existing pty Esc sandbox skip. A prior `make check` attempt hit
  the known pty hello sandbox flake at `tests/test_images_pty.c:1513`, then
  `make test` passed on rerun.
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
- `nix develop -c make check-no-images` - first failed before implementation
  (16/24 checks), then failed a mixed-cap regression check (1/26 checks), then
  passed after the cap-mask fix (26 checks).
- `nix develop -c make www` - passed after the `TIMUI_NO_IMAGES` header/docs
  changes, refreshing `www/timui.h` and `www/LICENSE`.
- `nix develop -c make release-check` - passed after the `TIMUI_NO_IMAGES`
  change; the release header compiles standalone in both normal and no-images
  modes.
- `nix develop -c make check` - passed after the `TIMUI_NO_IMAGES` change:
  build, 295 tests, `check-no-images` (26 checks), and the MinGW ConPTY compile
  seam.
- `nix develop -c make test-san SAN=undefined` - passed after the
  `TIMUI_NO_IMAGES` change, 295 tests, existing pty Esc sandbox skip.
- `nix develop -c make check-vt-gif-all` - passed after the `TIMUI_NO_IMAGES`
  change; Pillow emitted a deprecation warning in `tools/vtg_probe.py`.

## Next Safe Move

Use a Windows-capable worktree or CI worker to run a live Windows Terminal smoke
with `make smoke-conpty-win32` and record the command, Windows build, Windows
Terminal version, and output. Separately, run and record `make smoke-image-live
PROTO=sixel FRAMES=N` and `make smoke-image-live PROTO=iterm2 FRAMES=N` in
real terminals before promoting image protocol support from fake-transport wire
evidence to terminal evidence. The plain-PNG Sixel decoder dependency decision
is implemented and documented; remaining image work is evidence, not local
wire-format implementation.
