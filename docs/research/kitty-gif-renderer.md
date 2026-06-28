# Research: headless terminal→GIF recorder that captures Kitty graphics

*Status: **Path A IMPLEMENTED** (`tools/vt_gif.c`). Date: 2026-07-06.*

> **Built.** `tools/vt_gif.c` renders a capture to pixels — font + SGR colour +
> composited Kitty images — and emits a PNG (final frame) or an animated GIF.
> `tools/pty_drive.c` gained a `--timing` sidecar for frame pacing. End-to-end:
> **`make gif-chat-demo`** → `recordings/chat-demo.gif`, fully headless. See
> **Usage** at the bottom. Path B stays the documented fidelity escape hatch.

## Problem

`examples/chat` draws real inline images with the **Kitty graphics protocol**.
These are *pixels the terminal itself rasterizes*, not part of the text cell
stream. So none of the usual terminal-recording tools capture them:

| Tool | Why it drops the images |
|------|-------------------------|
| asciinema | records the raw byte stream only; its player is text |
| `agg` (cast→GIF) | renders the cast's **text** cells only |
| VHS (charmbracelet) | drives xterm.js in headless Chromium; `@xterm/addon-image` speaks **SIXEL + iTerm IIP only, not Kitty** |
| kitty / wezterm | render Kitty graphics but need a GPU/display and have **no session→frames/video export** |

**Conclusion:** nothing off-the-shelf renders a Kitty-graphics session to a GIF
headlessly. The reliable *manual* path is to screen-record the real terminal
(Ghostty) — which is what `make rec-chat-demo` + the `--demo` autoplay support.
For a *headless/CI* pipeline, we would have to build it. Two paths exist.

## Path A — extend our own `tools/vt_render.c` (recommended)

We already own the hard half: `vt_render.c` parses the Kitty APC (`ESC _ G …`),
tracks per-id transmits (`a=t`, base64 PNG) and placements (`a=p`, `c`×`r`,
source-crop `x/y/w/h`). What's missing is **rasterization to pixels**. All the
pieces are public-domain single-header C99, matching the project's aesthetic:

- **PNG decode:** [`stb_image.h`](https://github.com/nothings/stb) — retain the
  `a=t` base64 chunks (today we only *count* them), decode → RGBA + w/h.
- **Font raster (v1):** an embedded fixed bitmap font (VGA/CP437 8×16, Cozette,
  or Spleen) as a header array — zero assets, deterministic, crisp. **Upgrade:**
  [`stb_truetype.h`](https://github.com/nothings/stb) + a bundled monospace TTF.
- **Composite:** dest rect `(cx·cell_w, cy·cell_h)` sized `c·cell_w × r·cell_h`;
  sample the decoded image over the crop `(x,y,w,h)` we already track.
- **Frame output:** [`stb_image_write.h`](https://github.com/nothings/stb) per
  frame → GIF via [`msf_gif.h`](https://github.com/notnullnotvoid/msf_gif)
  (single-header, keeps the tool self-contained) or shell out to `ffmpeg`.
- **Timing:** `tools/pty_drive.c` writes a sidecar timing log (byte-offset,
  timestamp, asciinema-style); `vt_render` replays incrementally and snapshots
  the RGBA buffer at frame boundaries (~10–20 fps).

**The non-obvious cost:** `vt_render`'s emulator currently **skips SGR** (colour
/ attributes) — it only models CUP + autowrap. A watchable chat GIF wants at
least fg/bg colour, so the cell model must carry colour and parse an SGR subset
(16 / 256 / truecolor). *That* — not the pixel-pushing — is the bulk of the work.

**Scope lever:** we emit the Kitty stream ourselves from `chat`, so the decoder
can be restricted to `f=100` PNG + direct placement and skip the ugly corners
(raw `f=24/32`, zlib `o=z`, Unicode-placeholder/virtual placements, animation).

**Effort ≈ 4–6 focused days** (base64+PNG ~0.5d · RGBA rasterizer + font + SGR
subset ~1.5d · crop/scale composite ~0.5d · frame stepping + timing ~0.5–1d ·
GIF out ~0.5d · Makefile/tests/docs ~1d). Deterministic, CI-able, no
display/GPU/Zig. Follows the TDD gate (golden-PNG hash + malformed-APC negatives).

## Path B — libghostty-vt as a headless VT engine (fidelity escape hatch)

Newly viable: as of **~April 2026** libghostty **exposes the Kitty graphics
protocol through its render state** (it did *not* a month earlier — the request
[ghostty#12111](https://github.com/ghostty-org/ghostty/issues/12111) had been
closed "not planned"). The new C API surface (`placement_iterator_set` with a
z-layer filter, `placement_viewport_pos`, `placement_source_rect`, a
caller-supplied `decode_png` hook) hands you image data + placement rects + crops
— see [Ghostling PR #13](https://github.com/ghostty-org/ghostling/pull/13)
(a full Kitty renderer in ~200 LOC) and the
[announcement](https://hachyderm.io/@mitchellh/116359670905583974).

Caveats: the render state is **state, not pixels** — you still write the
rasterizer (retarget Ghostling's ~200 LOC from Raylib to an offscreen RGBA
buffer). The C API is **explicitly unstable** ("breaking changes expected", no
versioned release), and building it pulls in **Zig 0.15.x** + CMake/Ninja.
Existing libghostty-vt tools (termscope, headless-terminal, evp) capture **text**
state; none renders Kitty graphics to pixels yet. **Effort ≈ 5–8 days + ongoing
API-churn maintenance.** Worth it *only if* we later need real terminal fidelity
(reflow, scrollback, full SGR, z-layered/animated graphics) we'd otherwise
reimplement — then terminal-emulation correctness stops being our problem.

## Recommendation

**Do Path A when we want a headless GIF pipeline** — it closes exactly the gap
(parse+placement already done → add pixels), reuses code we own, stays in-idiom
(single-header C99, nix, Makefile, deterministic), and lets us bound the Kitty +
SGR feature set because we control the emitter. Keep **Path B documented as the
fidelity escape hatch** so switching later is a deliberate, informed choice.

**Until then**, the shipped workflow is: `make rec-chat-demo` → the `--demo`
autoplay self-drives the chat while you screen-record the real Ghostty window
(Kap / QuickTime) → `ffmpeg -i cap.mov -vf 'fps=15,scale=900:-1:flags=lanczos'
chat.gif`. See `examples/chat.demo` for the script format.

## Usage (implemented)

```sh
# One-shot: autoplay the chat demo -> animated GIF with the inline images.
make gif-chat-demo                      # -> recordings/chat-demo.gif

# Manual pipeline (any timui app):
build/pty_drive --cols 90 --rows 22 --run-ms 30000 \
  --out cap.raw --timing cap.timing -- ./build/chat --demo examples/chat.demo < /dev/null
build/vt_gif --cols 90 --rows 22 --fps 12 --timing cap.timing --gif out.gif cap.raw

# Single still frame (final state) — handy for debugging the rasterizer:
build/vt_gif --cols 90 --rows 22 --png out.png cap.raw

# CJK + colour emoji from the OS fonts (macOS), at a chosen size:
build/vt_gif --cols 90 --rows 22 --cell-h 24 --system-fonts --system-emoji --gif out.gif cap.raw
build/vt_gif --cols 90 --rows 22 --width 720 --frames-dir frames cap.raw   # PNG seq -> ffmpeg

make check-vt-gif-all  # smoke · glyphs · CJK · emoji · output controls
make gen-font-ttf      # regenerate tools/vendor/vt_font_ttf.h (fonttools, via nix)
```

Implementation notes / limits (see `vt-gif-v2-plan.md` for the v2 detail):
- **Fonts (v2):** an ordered fallback chain — bundled **DejaVu Sans Mono** (subset
  TTF `tools/vendor/vt_font_ttf.h`, `make gen-font-ttf`) rasterized with
  **stb_truetype** → a CJK face → a colour-emoji face. `--system-fonts` chains the
  OS CJK fonts (macOS Hiragino / AppleSDGothicNeo / Arial Unicode); `--system-emoji`
  renders Apple Color Emoji from its `sbix` PNG strikes. Cell size is runtime
  (`--cell-h`/`--scale`), and `render_frame` is two-pass (backgrounds then glyphs)
  so a width-2 CJK/emoji glyph isn't clipped by the next cell's fill. CJK + colour
  emoji also render **reproducibly with no flags** from bundled faces — **Unifont**
  (a `.bdf` bitmap face, `tools/vendor/vt_font_cjk.h`, `make gen-cjk`) and
  **Twemoji** (`tools/vendor/emoji_atlas.h`, `make gen-emoji`); the OS fonts are
  preferred when the flag is set.
- **Output (v2):** `--width` (aspect-preserving downscale, stb_image_resize2),
  `--bit-depth` (msf_gif quantization), `--frames-dir` (PNG sequence → `ffmpeg`
  MP4/WebP, vt_gif stays ffmpeg-free).
- **SGR:** reset/bold/dim/underline/reverse + 16 / 256 / truecolour fg+bg.
- **Kitty:** `f=100` PNG transmit (chunked `m=1`), direct placement `a=p` with
  source-crop `x/y/w/h`, and `a=d` delete-all. Skips raw `f=24/32`, zlib `o=z`,
  Unicode-placeholder placements, and animation (we control the emitter).
- **Timing:** `pty_drive --timing` logs `<ms> <byte-offset>` per chunk; `vt_gif`
  replays incrementally, and `feed()` stops before a sequence split across a
  chunk boundary (returns bytes consumed) so the parser never desyncs.
- **Follow-ups:** Linux system fonts (Noto CJK/Emoji); ZWJ/skin-tone emoji; a
  larger bundled Twemoji set; trimming the DejaVu subset.

## Sources

- libghostty Kitty-graphics announcement — <https://hachyderm.io/@mitchellh/116359670905583974>
- Ghostling Kitty renderer (~200 LOC) — <https://github.com/ghostty-org/ghostling/pull/13>
- ghostty#12111 (prior "not planned") — <https://github.com/ghostty-org/ghostty/issues/12111>
- awesome-libghostty — <https://github.com/Uzaaft/awesome-libghostty>
- xterm image addon (SIXEL/IIP only) — <https://github.com/jerch/xterm-addon-image>
- stb single-headers — <https://github.com/nothings/stb> · msf_gif — <https://github.com/notnullnotvoid/msf_gif>
