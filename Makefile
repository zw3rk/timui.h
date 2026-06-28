## timui.h — single-header C99 immediate-mode TUI.
## Sole entry point: nix develop -c make <target>

.DEFAULT_GOAL := help

CC         ?= cc
CFLAGS     ?= -std=c99 -Wall -Wextra -Wpedantic -O2 -pthread
TESTCFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -O0 -g -pthread

INCDIR   := include
SRCDIR   := src
BLDDIR   := build
EXADIR   := examples
TSTDIR   := tests
TOOLDIR  := tools
RELDIR   := release
RECDIR   := recordings

HEADER    := $(INCDIR)/timui.h
# The library is a unity build: src/timui.c #includes every src/timui_*.c
# section. Any section edit must rebuild the test binary, examples, and tools,
# so they all depend on the whole section set (not just src/timui.c).
LIB_SECTIONS := $(wildcard $(SRCDIR)/timui_*.c) $(SRCDIR)/timui_int.h
EXAMPLES  := $(patsubst $(EXADIR)/%.c,$(BLDDIR)/%,$(wildcard $(EXADIR)/*.c))
TEST_SRCS := $(SRCDIR)/timui.c $(TSTDIR)/test_main.c $(TSTDIR)/test_rect.c $(TSTDIR)/test_result.c $(TSTDIR)/test_arena.c $(TSTDIR)/test_strings.c $(TSTDIR)/test_id_stack.c $(TSTDIR)/test_msgq.c $(TSTDIR)/test_mpsc.c $(TSTDIR)/test_transport.c $(TSTDIR)/test_screen.c $(TSTDIR)/test_input.c $(TSTDIR)/test_mouse.c $(TSTDIR)/test_termios.c $(TSTDIR)/test_size.c $(TSTDIR)/test_caps.c $(TSTDIR)/test_kitty.c $(TSTDIR)/test_sync.c $(TSTDIR)/test_cells.c $(TSTDIR)/test_utf8.c $(TSTDIR)/test_draw.c $(TSTDIR)/test_render.c $(TSTDIR)/test_cursor.c $(TSTDIR)/test_frame.c $(TSTDIR)/test_interact.c $(TSTDIR)/test_theme.c $(TSTDIR)/test_button.c $(TSTDIR)/test_widgets.c $(TSTDIR)/test_input_widget.c $(TSTDIR)/test_listbox.c $(TSTDIR)/test_dialog.c $(TSTDIR)/test_fuzz.c $(TSTDIR)/test_clip.c $(TSTDIR)/test_menus.c $(TSTDIR)/test_modal.c $(TSTDIR)/test_hyperlink.c $(TSTDIR)/test_esc_timeout.c $(TSTDIR)/test_scroll.c $(TSTDIR)/test_v02_batch.c $(TSTDIR)/test_v02_widgets.c $(TSTDIR)/test_v02_more.c $(TSTDIR)/test_kitty_pty.c $(TSTDIR)/test_review_critical.c $(TSTDIR)/test_snapshot.c $(TSTDIR)/test_coverage_z7.c $(TSTDIR)/test_render_stream.c
TEST_BIN  := $(BLDDIR)/test_unit
GOLDEN_BIN := $(BLDDIR)/gen_golden

# libvterm round-trip tests (Tier A) are opt-in. WITH_VTERM=1 resolves libvterm
# via pkg-config and compiles tests/test_vt_roundtrip.c into a SEPARATE binary
# (build/test_vt), so the core `make test` never depends on libvterm. The vterm
# tests are registered in test_main.c under #if __has_include(<vterm.h>), which
# is true only when the libvterm include path is present.
VT_BIN    := $(BLDDIR)/test_vt
VT_CFLAGS :=
VT_LIBS   :=
VT_SRCS   :=
ifeq ($(WITH_VTERM),1)
  VT_CFLAGS := $(shell pkg-config --cflags libvterm 2>/dev/null)
  VT_LIBS   := $(shell pkg-config --libs   libvterm 2>/dev/null)
  ifneq ($(VT_LIBS),)
    VT_SRCS := $(TSTDIR)/test_vt_roundtrip.c
  else
    $(warning libvterm not found via pkg-config; round-trip tests disabled. \
Run inside `nix develop`, or install libvterm, then: make vt-test WITH_VTERM=1)
  endif
endif

ifeq ($(NO_COLOR),)
C_RESET := \033[0m
C_BOLD  := \033[1m
C_CYAN  := \033[36m
C_GREEN := \033[32m
C_YELL  := \033[33m
endif

.PHONY: help build test test-san run amalgamate release-check fmt check clean goldens vt-test

help: ## Show this help
	@printf "$(C_BOLD)timui.h$(C_RESET) — single-header C99 immediate-mode TUI\n\n"
	@printf "$(C_CYAN)usage: nix develop -c make <target>$(C_RESET)\n\n"
	@printf "  $(C_BOLD)targets$(C_RESET)\n"
	@awk 'BEGIN {FS = ":.*##"} /^[a-zA-Z_-]+:.*##/ { printf "  $(C_GREEN)%-14s$(C_RESET) %s\n", $$1, $$2 }' $(MAKEFILE_LIST)
	@printf "  $(C_GREEN)%-14s$(C_RESET) %s\n" "run-<name>" "run one example: editor procmon todo chat file_manager"
	@printf "  $(C_GREEN)%-14s$(C_RESET) %s\n" "rec-<name>" "record an interactive session -> recordings/<name>.cast"
	@printf "  $(C_GREEN)%-14s$(C_RESET) %s\n" "drive-<name>" "drive headless w/ recordings/<name>.in -> .raw + .txt"

build: $(EXAMPLES) ## Build all examples (single-header mode)
	@printf "$(C_GREEN)✓ build complete$(C_RESET)\n"

$(BLDDIR)/%: $(EXADIR)/%.c $(HEADER) $(LIB_SECTIONS)
	@mkdir -p $(@D)
	@printf "$(C_CYAN)build$(C_RESET) $<\n"
	@$(CC) $(CFLAGS) -I$(INCDIR) $< -o $@

test: $(TEST_BIN) ## Compile and run the unit tests
	@printf "$(C_YELL)▶ running tests$(C_RESET)\n"
	@./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRCS) $(HEADER) $(LIB_SECTIONS)
	@mkdir -p $(@D)
	@printf "$(C_CYAN)build$(C_RESET) tests\n"
	@$(CC) $(TESTCFLAGS) -I$(INCDIR) $(TEST_SRCS) -o $@

run: build ## Build and run the hello example
	@./$(BLDDIR)/hello

# Run one example by name (builds just that target first), e.g. `make run-editor`.
# Demos: editor procmon todo chat file_manager (plus hello counter form mini_commander).
run-%: $(BLDDIR)/%
	@./$(BLDDIR)/$*

# Autoplay the chat demo (self-driving via examples/chat.demo). Explicit targets
# override the run-% pattern. Kitty-graphics images are terminal PIXELS, so
# asciinema/agg/VHS can't capture them — screen-record this window instead.
run-chat-demo: $(BLDDIR)/chat ## Autoplay the chat demo script (self-driving)
	@./$(BLDDIR)/chat --demo examples/chat.demo

rec-chat-demo: $(BLDDIR)/chat ## Screen-record hint, then autoplay the chat demo
	@printf "$(C_CYAN)Start a screen recorder$(C_RESET) (Kap / QuickTime) on this Ghostty window,\n"
	@printf "then press Enter to autoplay the demo (~26s). Turn the capture into a GIF with:\n"
	@printf "  $(C_YELL)ffmpeg -i cap.mov -vf 'fps=15,scale=900:-1:flags=lanczos' chat.gif$(C_RESET)\n"
	@read _ && ./$(BLDDIR)/chat --demo examples/chat.demo

# ---- recording / headless driving --------------------------------------- #
$(BLDDIR)/pty_drive: $(TOOLDIR)/pty_drive.c
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) $< -o $@
$(BLDDIR)/vt_render: $(TOOLDIR)/vt_render.c
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) $< -o $@
# vt_gif rasterizes a capture to pixels (PNG/GIF) INCLUDING Kitty images. Uses
# vendored single-headers (stb, msf_gif) — relaxed warnings for that third-party
# code; our own logic still builds under -Wall.
$(BLDDIR)/vt_gif: $(TOOLDIR)/vt_gif.c $(TOOLDIR)/vendor/vt_font_ttf.h $(TOOLDIR)/vendor/emoji_atlas.h $(TOOLDIR)/vendor/vt_font_cjk.h
	@mkdir -p $(@D)
	@$(CC) -std=c99 -O2 -Wall -Wno-unused-function $(TOOLDIR)/vt_gif.c -o $@ -lm

# Regenerate the subset TTF face header from DejaVu Sans Mono (via nix: fonttools).
gen-font-ttf: ## Regenerate tools/vendor/vt_font_ttf.h (subset DejaVu Sans Mono)
	@nix-shell -p 'python3.withPackages(ps: [ps.fonttools])' dejavu_fonts --run 'python3 tools/gen_font_ttf.py'

# Regenerate the bundled colour-emoji atlas from Twemoji (needs network, via nix).
gen-emoji: ## Regenerate tools/vendor/emoji_atlas.h (curated Twemoji PNGs)
	@nix-shell -p python3 --run 'python3 tools/gen_emoji.py'

# Regenerate the bundled CJK bitmap face from GNU Unifont's .bdf (via nix: unifont).
gen-cjk: ## Regenerate tools/vendor/vt_font_cjk.h (Unifont CJK bitmaps, deflated)
	@nix-shell -p python3 unifont --run 'python3 tools/gen_cjk.py'

# Render the chat autoplay demo to an animated GIF *including* the Kitty images —
# fully headless (no screen recorder needed): drive with a timing sidecar, then
# rasterize each frame to pixels and encode the GIF.
gif-chat-demo: $(BLDDIR)/chat $(BLDDIR)/pty_drive $(BLDDIR)/vt_gif ## Headless: chat demo -> recordings/chat-demo.gif
	@mkdir -p $(RECDIR)
	@TERM=xterm-kitty ./$(BLDDIR)/pty_drive --cols 90 --rows 22 --settle-ms 1500 --run-ms 30000 \
	  --out $(RECDIR)/chat-demo.raw --timing $(RECDIR)/chat-demo.timing \
	  -- ./$(BLDDIR)/chat --demo examples/chat.demo < /dev/null
	@./$(BLDDIR)/vt_gif --cols 90 --rows 22 --fps 12 --system-fonts --system-emoji \
	  --timing $(RECDIR)/chat-demo.timing --gif $(RECDIR)/chat-demo.gif $(RECDIR)/chat-demo.raw
	@printf "$(C_CYAN)wrote$(C_RESET) $(RECDIR)/chat-demo.gif\n"

# Smoke-test the pixel renderer: a synthetic capture (text + an SGR colour +
# a malformed APC that must not crash the decoder) renders to a PNG of the
# expected pixel dimensions (cols*8 x rows*16).
check-vt-gif: $(BLDDIR)/vt_gif ## Smoke-test vt_gif (synthetic capture -> PNG)
	@printf 'hi \033[38;2;255;0;0mred\033[0m \033_Gi=9,a=t,f=100;Z2FyYmFnZQ==\033\134\033_Gi=9,a=p,c=2,r=1\033\134' > $(BLDDIR)/_vtg.raw
	@./$(BLDDIR)/vt_gif --cols 20 --rows 2 --png $(BLDDIR)/_vtg.png $(BLDDIR)/_vtg.raw >/dev/null 2>&1 \
	  || { printf "$(C_YELL)vt_gif crashed$(C_RESET)\n"; exit 1; }
	@file $(BLDDIR)/_vtg.png | grep -q '160 x 32' \
	  && printf "$(C_GREEN)✓ vt_gif$(C_RESET) renders 20x2 -> 160x32 PNG (malformed APC survived)\n" \
	  || { printf "$(C_YELL)✗ vt_gif: unexpected PNG$(C_RESET)\n"; exit 1; }

# Assert glyphs OUTSIDE the old ASCII+box baked set render (accented Latin, Greek,
# symbols) — blank with the v1 bitmap font, filled once the TTF face lands.
check-vt-gif-glyphs: $(BLDDIR)/vt_gif ## Assert extended glyphs (é Ω © …) render, not blank
	@printf 'éΩ©àüÄßµ' > $(BLDDIR)/_vtg_glyph.raw
	@./$(BLDDIR)/vt_gif --cols 8 --rows 1 --png $(BLDDIR)/_vtg_glyph.png $(BLDDIR)/_vtg_glyph.raw >/dev/null 2>&1
	@nix-shell -p 'python3.withPackages(ps:[ps.pillow])' --run 'python3 tools/vtg_probe.py $(BLDDIR)/_vtg_glyph.png nonbg 20' \
	  && printf "$(C_GREEN)✓ vt_gif$(C_RESET) extended glyphs render\n" \
	  || { printf "$(C_YELL)✗ vt_gif$(C_RESET) extended glyphs blank (expected until the TTF face)\n"; exit 1; }
	@./$(BLDDIR)/vt_gif --cols 8 --rows 1 --cell-h 24 --png $(BLDDIR)/_vtg_big.png $(BLDDIR)/_vtg_glyph.raw >/dev/null 2>&1
	@file $(BLDDIR)/_vtg_big.png | grep -q 'x 24' \
	  && printf "$(C_GREEN)✓ vt_gif$(C_RESET) --cell-h scales output\n" \
	  || { printf "$(C_YELL)✗ vt_gif$(C_RESET) --cell-h ignored\n"; exit 1; }

# Assert CJK (Han + Hangul + Kana) renders from the BUNDLED Unifont bitmap face
# (no flags → reproducible everywhere; --system-fonts adds nicer antialiased CJK).
# Also exercises the wide-glyph two-pass renderer (a width-2 glyph must not be
# clipped by the next cell's bg).
check-vt-gif-cjk: $(BLDDIR)/vt_gif ## Assert CJK renders (bundled Unifont)
	@printf '日本語中文한국어' > $(BLDDIR)/_vtg_cjk.raw
	@./$(BLDDIR)/vt_gif --cols 12 --rows 1 --cell-h 20 --png $(BLDDIR)/_vtg_cjk.png $(BLDDIR)/_vtg_cjk.raw 2>/dev/null
	@nix-shell -p 'python3.withPackages(ps:[ps.pillow])' --run 'python3 tools/vtg_probe.py $(BLDDIR)/_vtg_cjk.png nonbg 60' >/dev/null 2>&1 \
	  && printf "$(C_GREEN)✓ vt_gif$(C_RESET) CJK renders (bundled Unifont)\n" \
	  || { printf "$(C_YELL)✗ vt_gif$(C_RESET) bundled CJK blank\n"; exit 1; }

# Assert colour emoji render from the BUNDLED Twemoji atlas (no flags, so this is
# reproducible everywhere): a chromatic (non-gray) region must appear.
check-vt-gif-emoji: $(BLDDIR)/vt_gif ## Assert colour emoji render (bundled Twemoji)
	@printf '👋🎉🚀' > $(BLDDIR)/_vtg_emoji.raw
	@./$(BLDDIR)/vt_gif --cols 8 --rows 1 --cell-h 20 --png $(BLDDIR)/_vtg_emoji.png $(BLDDIR)/_vtg_emoji.raw 2>/dev/null
	@nix-shell -p 'python3.withPackages(ps:[ps.pillow])' --run 'python3 tools/vtg_probe.py $(BLDDIR)/_vtg_emoji.png colour 0 0 80 20' >/dev/null 2>&1 \
	  && printf "$(C_GREEN)✓ vt_gif$(C_RESET) colour emoji render (bundled Twemoji)\n" \
	  || { printf "$(C_YELL)✗ vt_gif$(C_RESET) bundled emoji blank\n"; exit 1; }

# Assert the output controls: --width downscales to an exact pixel width, and
# --frames-dir emits a PNG sequence (for ffmpeg -> MP4/WebP).
check-vt-gif-output: $(BLDDIR)/vt_gif ## Assert --width / --frames-dir output controls
	@printf 'hello world' > $(BLDDIR)/_vtg_o.raw
	@./$(BLDDIR)/vt_gif --cols 20 --rows 2 --width 200 --png $(BLDDIR)/_vtg_o.png $(BLDDIR)/_vtg_o.raw >/dev/null 2>&1
	@file $(BLDDIR)/_vtg_o.png | grep -q '200 x' \
	  && printf "$(C_GREEN)✓ vt_gif$(C_RESET) --width downscales to 200px\n" \
	  || { printf "$(C_YELL)✗ vt_gif$(C_RESET) --width failed\n"; exit 1; }
	@rm -rf $(BLDDIR)/_vtg_frames && mkdir -p $(BLDDIR)/_vtg_frames
	@./$(BLDDIR)/vt_gif --cols 20 --rows 2 --frames-dir $(BLDDIR)/_vtg_frames $(BLDDIR)/_vtg_o.raw >/dev/null 2>&1
	@test "$$(ls $(BLDDIR)/_vtg_frames/*.png 2>/dev/null | wc -l | tr -d ' ')" -ge 1 \
	  && printf "$(C_GREEN)✓ vt_gif$(C_RESET) --frames-dir writes a PNG sequence\n" \
	  || { printf "$(C_YELL)✗ vt_gif$(C_RESET) --frames-dir failed\n"; exit 1; }

# Golden-PNG regression: a fixed synthetic capture (text · bold · truecolour · CJK
# · emoji, all from the BUNDLED faces) must render byte-for-byte identical.
# Deterministic — stb_truetype is pure C and the fonts are vendored. Refresh the
# golden with `make gen-golden-vtgif` when the render intentionally changes.
GOLDEN_VTG := tests/golden/vt_gif_sample.png
VTG_GOLDEN_CAP = printf 'Hi \033[1mbold\033[0m \033[38;2;255;140;0m中文\033[0m 👋'
check-vt-gif-golden: $(BLDDIR)/vt_gif ## Compare a fixed render to the golden PNG
	@$(VTG_GOLDEN_CAP) > $(BLDDIR)/_vtg_g.raw
	@./$(BLDDIR)/vt_gif --cols 16 --rows 1 --cell-h 16 --png $(BLDDIR)/_vtg_g.png $(BLDDIR)/_vtg_g.raw 2>/dev/null
	@cmp -s $(BLDDIR)/_vtg_g.png $(GOLDEN_VTG) \
	  && printf "$(C_GREEN)✓ vt_gif$(C_RESET) golden PNG matches\n" \
	  || { printf "$(C_YELL)✗ vt_gif$(C_RESET) golden mismatch (make gen-golden-vtgif if intended)\n"; exit 1; }
gen-golden-vtgif: $(BLDDIR)/vt_gif ## Refresh tests/golden/vt_gif_sample.png
	@mkdir -p tests/golden
	@$(VTG_GOLDEN_CAP) > $(BLDDIR)/_vtg_g.raw
	@./$(BLDDIR)/vt_gif --cols 16 --rows 1 --cell-h 16 --png $(GOLDEN_VTG) $(BLDDIR)/_vtg_g.raw
	@printf "$(C_CYAN)refreshed$(C_RESET) $(GOLDEN_VTG)\n"

# Run every vt_gif renderer check (smoke · glyphs · CJK · emoji · output · golden).
check-vt-gif-all: check-vt-gif check-vt-gif-glyphs check-vt-gif-cjk check-vt-gif-emoji check-vt-gif-output check-vt-gif-golden ## All vt_gif renderer checks
	@printf "$(C_GREEN)✓ vt_gif: all renderer checks passed$(C_RESET)\n"

# Record a REAL interactive session (you type) to recordings/<name>.cast — the
# raw byte stream, viewable with `asciinema play` and analysable by the verifier.
rec-%: $(BLDDIR)/%
	@mkdir -p $(RECDIR)
	@command -v asciinema >/dev/null 2>&1 || { printf "$(C_YELL)asciinema not found — run inside 'nix develop'$(C_RESET)\n"; exit 1; }
	@printf "$(C_CYAN)recording$(C_RESET) $(RECDIR)/$*.cast — quit the app (F10/ESC) to stop\n"
	@asciinema rec --overwrite -c "./$(BLDDIR)/$*" "$(RECDIR)/$*.cast"

# Drive <name> HEADLESS: feed scripted keystrokes from recordings/<name>.in (a
# raw byte file; missing => none) through a pty, capture the output stream to
# recordings/<name>.raw, and render the final screen to recordings/<name>.txt.
drive-%: $(BLDDIR)/% $(BLDDIR)/pty_drive $(BLDDIR)/vt_render
	@mkdir -p $(RECDIR)
	@in="$(RECDIR)/$*.in"; [ -f "$$in" ] || in=/dev/null; \
	 printf "$(C_CYAN)driving$(C_RESET) $* headless (input: $$in)\n"; \
	 ./$(BLDDIR)/pty_drive --cols 100 --rows 30 --out "$(RECDIR)/$*.raw" --delay-ms 4 -- ./$(BLDDIR)/$* < "$$in"; \
	 ./$(BLDDIR)/vt_render --cols 100 --rows 30 "$(RECDIR)/$*.raw" > "$(RECDIR)/$*.txt"; \
	 printf "$(C_GREEN)✓ $(RECDIR)/$*.raw + $(RECDIR)/$*.txt$(C_RESET)\n"

# Headless acceptance smoke: drive an app with a CHECKED-IN input script
# (tests/drive/<name>.in) and assert the rendered screen. End-to-end proof that a
# real app binary handles input + renders correctly. Timing-dependent, so it is
# deliberately OUTSIDE `make check` (which stays deterministic).
accept: $(BLDDIR)/editor $(BLDDIR)/pty_drive $(BLDDIR)/vt_render ## Headless acceptance smoke (scripted input -> assert render)
	@mkdir -p $(RECDIR)
	@printf "$(C_BOLD)Headless acceptance$(C_RESET) (scripted input through a pty)\n"
	@./$(BLDDIR)/pty_drive --cols 100 --rows 30 --run-ms 2500 --settle-ms 300 \
	   --out "$(RECDIR)/accept-editor.raw" --delay-ms 4 -- ./$(BLDDIR)/editor < tests/drive/editor.in
	@./$(BLDDIR)/vt_render --cols 100 --rows 30 "$(RECDIR)/accept-editor.raw" | grep -q 'timui acceptance ok' \
	  && printf "  $(C_GREEN)✓ editor: typed text rendered$(C_RESET)\n" \
	  || { printf "  $(C_RED)✗ editor: typed text missing from render$(C_RESET)\n"; exit 1; }

test-san: ## Compile + run unit tests under a sanitizer: make test-san SAN=address
	@mkdir -p $(BLDDIR)
	@$(CC) -std=c99 -Wall -Wextra -Wpedantic -O1 -g -fsanitize=$(SAN) -I$(INCDIR) $(TEST_SRCS) -o $(BLDDIR)/test_san
	@./$(BLDDIR)/test_san

goldens: $(GOLDEN_BIN) ## Regenerate tests/golden/*.txt snapshots
	@mkdir -p tests/golden
	@./$(GOLDEN_BIN)
	@printf "$(C_GREEN)✓ goldens regenerated$(C_RESET)\n"

$(GOLDEN_BIN): $(TOOLDIR)/gen_golden.c $(HEADER) $(TSTDIR)/scenes.h $(LIB_SECTIONS)
	@mkdir -p $(@D)
	@printf "$(C_CYAN)build$(C_RESET) gen_golden\n"
	@$(CC) $(CFLAGS) -I$(INCDIR) $< -o $@

vt-test: build ## Compile + run unit tests WITH libvterm round-trip tests (needs libvterm)
	@$(MAKE) $(VT_BIN) WITH_VTERM=1
	@printf "$(C_YELL)▶ running vt-tests$(C_RESET)\n"
	@./$(VT_BIN)

$(VT_BIN): $(TEST_SRCS) $(VT_SRCS) $(HEADER) $(LIB_SECTIONS)
	@mkdir -p $(@D)
	@printf "$(C_CYAN)build$(C_RESET) vt-tests\n"
	@$(CC) $(TESTCFLAGS) $(VT_CFLAGS) -I$(INCDIR) $(TEST_SRCS) $(VT_SRCS) $(VT_LIBS) -o $@

amalgamate: $(BLDDIR)/amalgamate $(HEADER) $(LIB_SECTIONS) ## Regenerate the flat release single-header into release/
	@mkdir -p $(RELDIR)
	@./$(BLDDIR)/amalgamate $(HEADER) $(RELDIR)/timui.h
	@printf "$(C_GREEN)✓ wrote $(RELDIR)/timui.h$(C_RESET)\n"

release-check: amalgamate ## Verify the amalgamated release header compiles standalone
	@printf "$(C_CYAN)build$(C_RESET) release self-test\n"
	@printf '#define TIMUI_IMPLEMENTATION\n#include "../$(RELDIR)/timui.h"\nint main(void){return 0;}\n' > $(BLDDIR)/release_selftest.c
	@$(CC) $(CFLAGS) $(BLDDIR)/release_selftest.c -o $(BLDDIR)/release_selftest
	@printf "$(C_GREEN)✓ release header compiles standalone$(C_RESET)\n"

$(BLDDIR)/amalgamate: $(TOOLDIR)/amalgamate.c
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) $< -o $@

fmt: ## Format C sources if clang-format is available
	@if command -v clang-format >/dev/null 2>&1; then clang-format -i $(HEADER) $(SRCDIR)/*.c $(EXADIR)/*.c $(TSTDIR)/*.c $(TOOLDIR)/*.c; printf "$(C_GREEN)✓ formatted$(C_RESET)\n"; else printf "$(C_YELL)clang-format not found; skipping$(C_RESET)\n"; fi

check: build test ## Build + test gate
	@printf "$(C_GREEN)✓ check passed$(C_RESET)\n"

clean: ## Remove build artifacts
	@rm -rf $(BLDDIR) $(RELDIR)
	@printf "$(C_GREEN)✓ clean$(C_RESET)\n"
