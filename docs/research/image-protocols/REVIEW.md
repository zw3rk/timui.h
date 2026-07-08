---
type: Report
title: Image Protocols Phase 1.5 Review
date: 2026-07-08
---

# Image Protocols Phase 1.5 Review

## Read First

- `docs/goals/phase1_5-platform-widgets-style-text-image.goal.txt`
- `docs/TERMINAL_PROTOCOLS.md`
- `src/timui_images.c`
- `tools/vendor/NOTICE`

## Sources

- iTerm2 Inline Images Protocol: <https://iterm2.com/documentation-images.html>
- DEC Sixel graphics reference, VT330/VT340 chapter 14: <https://vt100.net/docs/vt3xx-gp/chapter14.html>
- DEC PPL2 Sixel graphics reference: <https://vt100.net/mirror/mds-199909/cd3/printer/pplv2pmb.pdf>
- libsixel repository: <https://github.com/saitoha/libsixel>
- stb repository/license notes: <https://github.com/nothings/stb>

## Pass A - iTerm2

iTerm2 inline images are direct file-byte transport. For PNG bytes already held
by `TimuiImage`, the emitter can move to the target cell with CUP and send:

```text
ESC ] 1337 ; File=inline=1;size=<bytes>;width=<cols>;height=<rows>;preserveAspectRatio=0:<base64_png> ST
```

Bare numeric `width` and `height` are character-cell units in the iTerm2 spec.
`preserveAspectRatio=0` matches Kitty placement semantics more closely because
Kitty currently scales exactly to `c=<cols>,r=<rows>`. The primary `File=`
sequence is enough for the first pass because timui strips image protocols
under multiplexers unless an application forces them; multipart/tmux behavior
can remain future work.

iTerm2 has no documented placement id or source-crop fields in the `File`
sequence. Therefore stale Kitty placements must be cleared when switching from
Kitty to iTerm2, but iTerm2 frames must not later trigger Kitty delete escapes.
`timui_image_draw_clipped` should fall back to the placeholder for iTerm2 until
timui can crop and re-encode PNGs.

## Pass B - Sixel

Sixel is not PNG-on-wire. It is a DCS/ST envelope containing six-pixel-high
bitmap columns, optional raster attributes, color register definitions, and
optional run-length encoding:

```text
ESC P 0;1;0 q
"1;1;<pixel_width>;<pixel_height>
#<n>;2;<r_pct>;<g_pct>;<b_pct>
#<n><sixel-data>[- ...]
ESC \
```

Each printable sixel byte is `0x3f + bits`; bit 0 is the top pixel in a
six-pixel band. RGB color values are percentages in `0..100`, not byte values.
Alpha is not represented directly. A first implementation should either
threshold alpha to transparent with background-preserving zero bits (`P2=1`) or
pre-composite RGBA over a known background before palette selection.

The first Sixel slices deliberately avoided PNG decode in the library path:
`timui_image_from_png` copied bytes and read IHDR dimensions only, while
`tools/vt_gif.c` decoded PNG via vendored `stb_image.h` as dev tooling. That
kept the release header small while raw-RGBA and PNG+RGBA sidecar emission were
designed and tested.

## Decision

Implement iTerm2 first as the direct-PNG emitter. It requires no public API
change and no new dependency.

Implemented state: `src/timui_images.c` now emits iTerm2 inline images for
unclipped PNG draws via OSC 1337. It clears stale Kitty placements when
switching from Kitty to iTerm2, does not emit Kitty delete escapes after an
iTerm2 frame, rejects oversized base64 expansions before writing partial
escapes, and falls back to `[img]` for clipped iTerm2 draws.

Keep Sixel as the next image emitter slice. The smallest honest implementation
should avoid promoting `stb_image` into the public library for now: add a
raw-RGBA image constructor, copy caller-owned pixel rows with strict
dimension/stride/overflow validation, and emit Sixel only for that raw pixel
kind. At that intermediate slice, plain PNG images forced to Sixel continued to
draw `[img]`; the final local implementation state below supersedes this after
the bounded PNG decoder decision.

Implemented state: `timui_image_from_rgba` copies rows into tightly packed RGBA
storage and the Sixel emitter handles raw RGBA images with up to 16 opaque exact
colours, deterministic 16-colour terminal-palette quantization beyond that cap,
cropped Sixel draws via `timui_image_draw_clipped`, and nearest-neighbor scaling
to the requested cell rectangle when `TIOCGWINSZ` reports terminal pixel
geometry. Alpha below 128 is transparent/background-preserving.

Follow-up state: `timui_image_from_png_rgba` copies original PNG bytes plus
caller-supplied decoded RGBA rows. Kitty and iTerm2 continue to transmit the PNG
bytes; Sixel uses the RGBA sidecar for emission and clipping. The supplied RGBA
dimensions are expected to match the PNG and drive source cropping.

Final local implementation state: `stb_image.h` is now promoted into the
library/release header as a PNG-only, no-stdio, bounded decoder. Plain PNG
images forced to Sixel are lazily decoded into the same RGBA source path used by
raw-RGBA and PNG+RGBA sidecars; malformed or oversized PNGs keep the `[img]`
fallback and emit no partial DCS payload. `tools/vendor/NOTICE` records this
library use. Real-terminal capture evidence remains open.

Operator smoke state: `examples/image_smoke.c` is a small live-terminal harness
with a valid embedded PNG, matching RGBA pixels, and PNG+RGBA sidecar. Run
`make smoke-image-live PROTO=auto|kitty|sixel|iterm2|none FRAMES=N` (or the
convenience aliases) outside multiplexers to collect bounded visual evidence;
omit `FRAMES` for an Escape-driven operator session. `make
check-image-smoke` only proves the harness renders the placeholder path through
a headless pty; it is not terminal image protocol evidence.

Do not claim live Sixel support from fake-transport tests alone. The decoder and
wire emitter are unit-tested, but terminal evidence still requires
`make smoke-image-live PROTO=sixel` in a real Sixel-capable terminal.
