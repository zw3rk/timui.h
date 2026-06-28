# Plan: lift `vt_gif` v1 limits — full glyphs, colour emoji, output controls

*Status: Phases 1–3 IMPLEMENTED (system-font paths). Date: 2026-07-06.*
*Builds on `docs/research/kitty-gif-renderer.md` (Path A, implemented).*

> **Done:** P1 stb_truetype text face + `--cell-h`/`--scale` (any glyph the font
> has, antialiased). P2 outline fallback chain + CJK via `--system-fonts` (macOS
> Hiragino/AppleSDGothicNeo/…) + colour emoji via `--system-emoji` (Apple Color
> Emoji `sbix`) — with a two-pass renderer fixing wide-glyph clipping. P3
> `--width`/`--bit-depth`/`--frames-dir`. Checks: `make check-vt-gif-all`.
> **Remaining:** bundled **Twemoji** + **Unifont** (reproducible defaults, no
> flags / Linux-CI); the P4 golden-PNG test. See the Rollout note.

## Current state & the two limits

`tools/vt_gif.c` renders a capture to PNG/GIF **with Kitty images**. v1 limits:

1. **Glyph coverage** — text is drawn from a baked 8×16 bitmap (`tools/vendor/
   vt_font.h`, ASCII + a box-drawing subset). Anything outside it — accented
   Latin, Greek/Cyrillic, many symbols, and **all emoji** — renders blank.
2. **No output size/quality controls** — the cell is fixed 8×16, so output is
   always `cols*8 × rows*16`; GIF is the only format; no quality/size knob.

## Decisions (chosen defaults — adjustable)

| # | Decision | Chosen | Alt |
|---|----------|--------|-----|
| 1 | Text font engine | **`stb_truetype` + bundled (subset) DejaVu Sans Mono** | expand the baked bitmap |
| 2 | **Font fallback chain** | **ordered faces: text → CJK → emoji → tofu** (first face with the glyph wins) | single font |
| 3 | **CJK** (required) | **bundled fallback face (GNU Unifont, OFL, full-BMP); `--system-fonts` → OS CJK (macOS PingFang)** | none (CJK blank) |
| 4 | Emoji | **Twemoji (bundled, default); `--system-emoji` → OS colour emoji (macOS Apple Color Emoji, `sbix`)** | skip/monochrome |
| 5 | Output formats | **`--frames-dir` → PNG seq for ffmpeg MP4/WebP** + `--width`/`--scale`/`--bit-depth` | GIF-only + knobs |

**Font architecture — a fallback chain of "faces".** Each face resolves a
codepoint to a glyph bitmap: a `stb_truetype` outline face (grayscale, blended in
fg over bg) or a colour bitmap face (emoji, composited as RGBA). For each cell,
try faces in order until one has the glyph; else draw a tofu box. Default faces
(all bundled → reproducible/CI-safe): **DejaVu Sans Mono** (Latin/Greek/Cyrillic/
box/symbols) → **Unifont** (CJK + rest of BMP) → **Twemoji** (colour emoji).
`--system-fonts` / `--system-emoji` swap the CJK / emoji faces for the OS fonts
(native look, full coverage) by reading the font files directly (no platform
framework dependency): macOS **PingFang.ttc** (CJK outlines via stb_truetype) and
**Apple Color Emoji.ttc** (`sbix` PNG strikes).

## Phase 1 — text glyphs + arbitrary scale (`stb_truetype`)

Runtime glyph rasterization; also delivers the scale knob (glyphs render at any
pixel size). Lifts limit 1 for text.

- **Vendor** `tools/vendor/stb_truetype.h` (+ `stb_image_resize2.h` for Phase 3).
- **Bundle the font**: subset DejaVu Sans Mono (`fonttools subset` → the ranges
  we use: Latin-1, Latin Ext-A, Greek, Cyrillic, punctuation, arrows, box
  drawing, block elements, geometric shapes, misc symbols), `xxd -i` →
  `tools/vendor/vt_font_ttf.h`. Target < 100 KB. `make gen-font-ttf`.
- **Rasterizer** in `vt_gif.c`:
  - `stbtt_InitFont(&font, ttf)`; `scale = stbtt_ScaleForPixelHeight(&font, cellh)`.
  - `cellw`/`cellh` become runtime vars (default 8/16; set by `--cell-h`/`--scale`).
  - Glyph cache: `codepoint → {cov[], w, h, xoff, yoff, advance}` via
    `stbtt_GetCodepointBitmap`; blit positioned on the baseline, fg-over-bg.
  - Retire `glyph()`/`vt_font.h` (keep `gen_font.py` for reference or delete).
- **Verify**: render `é ─ ↑ Ω © ►` → non-blank; visual diff vs v1.
- *Files*: `tools/vt_gif.c`, `tools/vendor/vt_font_ttf.h` (new), `Makefile`.
- *Risk*: TTF size → mitigate by subsetting. *~1–1.5 d.*

## Phase 2 — fallback faces: CJK + colour emoji

Phase 1 leaves the face-chain hook; this adds two fallback faces.

**CJK face (required).** Bundle **GNU Unifont** (OFL; 16-px full-BMP bitmap) as
`tools/vendor/vt_font_cjk.h` — pack the CJK-relevant ranges (CJK Unified
Ideographs, Hangul, Kana, CJK Symbols/Punctuation, Fullwidth forms) to keep it
~1 MB, or the whole BMP. Rasterize the bitmap glyph scaled to the cell; wide
(width-2) codepoints span 2 columns. `--system-fonts` instead loads the OS CJK
outline font (macOS `/System/Library/Fonts/PingFang.ttc` via `stb_truetype`) for
antialiased native CJK, falling back to Unifont if absent.
*Acceptance*: `日本語 中文 한국어` render legibly, 2 columns per ideograph, correctly
positioned relative to surrounding Latin.

**Colour emoji face.**
Colour glyphs a normal font can't draw; composite them like tiny inline images.

- **Curate** a Twemoji subset (the demo/chat set — 👋🎉📷🖼✅ … — plus common
  ones, ~40–60). Embed the **PNG bytes** (not decoded) in a generated
  `tools/vendor/emoji_atlas.h` (`{codepoint, png[], len}`), built by
  `tools/gen_emoji.py` from a Twemoji checkout. ~150 KB; decoded lazily by the
  `stb_image` + decode-cache infra already in `vt_gif`. (CC-BY 4.0 → `NOTICE`.)
- **`--system-emoji`** (macOS): read `/System/Library/Fonts/Apple Color Emoji.ttc`,
  locate the `sbix` table, and pull the PNG strike for the codepoint's glyph
  (cmap → glyphID via `stb_truetype`; largest-ppem strike) → the native Apple
  artwork. Missing font/table → fall back to the bundled Twemoji face + a stderr
  note. Linux (Noto Color Emoji) is a follow-up.
- **Detect** emoji codepoints (0x1F300–0x1FAFF, 0x2600–0x27BF, …); in
  `render_frame`, skip the font glyph and composite the emoji RGBA across the
  2-cell slot (`cp_width` already returns 2 for the astral range).
- **v1 scope**: single-codepoint emoji only. ZWJ sequences / skin-tone modifiers
  → follow-up.
- **Verify**: render 👋 → assert colour (non-gray) pixels present.
- *Files*: `tools/vt_gif.c`, `tools/vendor/emoji_atlas.h` + `NOTICE` (CC-BY 4.0),
  `tools/gen_emoji.py` (new). *~1 d.*

## Phase 3 — output size / quality / formats

Lifts limit 2.

- `--cell-h N` / `--scale F` — native size (from Phase 1).
- `--width N` — post-render downscale each frame (`stb_image_resize2`, aspect
  preserved) before encoding — exact target widths, smaller GIFs.
- `--bit-depth N` — forwarded to `msf_gif_frame` (quality ↔ size).
- `--frames-dir DIR` — write `frame_%05d.png` (instead of/with the GIF) so the
  user can `ffmpeg -framerate FPS -i frame_%05d.png … out.mp4` (or animated
  WebP) — **much** smaller than GIF, and `vt_gif` stays ffmpeg-free. Print the
  suggested command.
- `Makefile`: size defaults on `gif-chat-demo`; optional `mp4-chat-demo`.
- *Files*: `tools/vt_gif.c`, `Makefile`. *~0.5–1 d.*

## Phase 4 — tests + docs (TDD gate)

- Extend `make check-vt-gif`: **golden-PNG hash** (fixed synthetic capture →
  PNG → sha256 vs a committed golden; `make gen-golden-vtgif` to refresh);
  **glyph coverage** (render `é ─ ↑ Ω ©`, assert those cells non-blank via a
  pixel probe); **emoji colour** (render 👋, assert a non-gray pixel);
  **`--width`** dimension check; malformed-input no-crash (kept).
  - *Determinism*: `stb_truetype` is pure C (no platform font APIs), so the
    golden hash should be stable cross-platform — **verify** on first run; if it
    drifts, assert structural properties (dimensions + sampled pixels) instead
    of an exact hash.
- Update `docs/research/kitty-gif-renderer.md` (limits lifted), `USAGE.md`,
  `CHANGELOG`; document `gen-font-ttf` / `gen-emoji`. *~0.5–1 d.*

## Out of scope (noted follow-ups)

- **COLRv1 / animated / ZWJ emoji, skin-tone modifiers** — single-codepoint
  colour emoji only for now (Twemoji static / one `sbix` strike).
- **Linux system fonts/emoji** — `--system-fonts` / `--system-emoji` target the
  macOS font paths first; Linux (Noto CJK / Noto Color Emoji, `CBDT`/`COLR`) is a
  follow-up and falls back to the bundled faces meanwhile.

## Rollout

Land phase-by-phase (each independently useful): P1 (biggest visual win + scale)
→ P3 (cheap, high value) → P2 (emoji) → P4 folds tests in as each lands.
**Total ≈ 3–5 focused days.**
