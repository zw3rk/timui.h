## timui.h — single-header C99 immediate-mode TUI.
## Sole entry point: nix develop -c make <target>

# ============================================================================
# 1. CONFIG — toolchain · directories · source sets · colours · subsystem build vars
# ============================================================================

.DEFAULT_GOAL := help

CC        ?= cc
UNAME_S   := $(shell uname -s)
POSIX_CFLAGS :=
ifeq ($(UNAME_S),Linux)
  POSIX_CFLAGS := -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700
endif
CFLAGS     ?= -std=c99 $(POSIX_CFLAGS) -Wall -Wextra -Wpedantic -O2 -pthread
TESTCFLAGS ?= -std=c99 $(POSIX_CFLAGS) -Wall -Wextra -Wpedantic -O0 -g -pthread
SAN        ?= address
CONPTY_WIN_CC ?= x86_64-w64-mingw32-gcc
CONPTY_WIN_CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -Werror -D_WIN32_WINNT=0x0A00

INCDIR   := include
SRCDIR   := src
BLDDIR   := build
EXADIR   := examples
TSTDIR   := tests
TOOLDIR  := tools
RELDIR   := release
RECDIR   := recordings
WWWDIR   := www

HEADER    := $(INCDIR)/timui.h
WWW_HEADER := $(WWWDIR)/timui.h
WWW_LICENSE := $(WWWDIR)/LICENSE
# The library is a unity build: src/timui.c #includes every src/timui_*.c
# section. Any section edit must rebuild the test binary, examples, and tools,
# so they all depend on the whole section set (not just src/timui.c).
LIB_SECTIONS := $(wildcard $(SRCDIR)/timui_*.c) $(SRCDIR)/timui_int.h \
                $(TOOLDIR)/vendor/stb_image.h
# examples/radio.c has extra vendored deps (minimp3/miniaudio/kissfft) + audio
# link flags, so it is built by a dedicated rule below — keep it out of the
# generic single-file example pattern.
EXAMPLES  := $(filter-out $(BLDDIR)/radio,$(patsubst $(EXADIR)/%.c,$(BLDDIR)/%,$(wildcard $(EXADIR)/*.c)))
TEST_SRCS := $(SRCDIR)/timui.c $(TSTDIR)/test_main.c $(TSTDIR)/test_rect.c $(TSTDIR)/test_result.c $(TSTDIR)/test_arena.c $(TSTDIR)/test_strings.c $(TSTDIR)/test_id_stack.c $(TSTDIR)/test_msgq.c $(TSTDIR)/test_mpsc.c $(TSTDIR)/test_transport.c $(TSTDIR)/test_screen.c $(TSTDIR)/test_input.c $(TSTDIR)/test_mouse.c $(TSTDIR)/test_termios.c $(TSTDIR)/test_size.c $(TSTDIR)/test_caps.c $(TSTDIR)/test_kitty.c $(TSTDIR)/test_sync.c $(TSTDIR)/test_cells.c $(TSTDIR)/test_utf8.c $(TSTDIR)/test_draw.c $(TSTDIR)/test_render.c $(TSTDIR)/test_cursor.c $(TSTDIR)/test_frame.c $(TSTDIR)/test_interact.c $(TSTDIR)/test_theme.c $(TSTDIR)/test_stylesheet.c $(TSTDIR)/test_button.c $(TSTDIR)/test_widgets.c $(TSTDIR)/test_input_widget.c $(TSTDIR)/test_listbox.c $(TSTDIR)/test_grid_widget.c $(TSTDIR)/test_dialog.c $(TSTDIR)/test_fuzz.c $(TSTDIR)/test_clip.c $(TSTDIR)/test_menus.c $(TSTDIR)/test_modal.c $(TSTDIR)/test_hyperlink.c $(TSTDIR)/test_esc_timeout.c $(TSTDIR)/test_scroll.c $(TSTDIR)/test_v02_batch.c $(TSTDIR)/test_v02_widgets.c $(TSTDIR)/test_v02_more.c $(TSTDIR)/test_images_pty.c $(TSTDIR)/test_review_critical.c $(TSTDIR)/test_snapshot.c $(TSTDIR)/test_coverage_z7.c $(TSTDIR)/test_render_stream.c $(TSTDIR)/test_async_scan.c
TEST_BIN  := $(BLDDIR)/test_unit
GOLDEN_BIN := $(BLDDIR)/gen_golden

# libvterm round-trip tests (Tier A) are opt-in. WITH_VTERM=1 resolves the
# neovim/Paul Evans libvterm API via pkg-config and compiles
# tests/test_vt_roundtrip.c into a SEPARATE binary (build/test_vt), so the core
# `make test` never depends on libvterm. The vterm tests are registered only
# when TIMUI_WITH_VTERM_TESTS is defined by this build path; ambient include
# paths must not change the core test binary.
VT_BIN    := $(BLDDIR)/test_vt
VT_CFLAGS :=
VT_LIBS   :=
VT_SRCS   :=
ifeq ($(WITH_VTERM),1)
  VT_CFLAGS := $(shell pkg-config --cflags vterm 2>/dev/null)
  VT_LIBS   := $(shell pkg-config --libs   vterm 2>/dev/null)
  ifeq ($(VT_LIBS),)
    VT_CFLAGS := $(shell pkg-config --cflags libvterm 2>/dev/null)
    VT_LIBS   := $(shell pkg-config --libs   libvterm 2>/dev/null)
  endif
  ifeq ($(VT_LIBS),)
    # Last resort for shells where the compiler wrapper exposes the vterm
    # include/lib paths but no vterm pkg-config module is visible. The older
    # nixpkgs libvterm package's public header also needs GLib + curses flags.
    VT_CFLAGS += $(shell pkg-config --cflags glib-2.0 ncursesw 2>/dev/null)
    VT_LIBS := -lvterm $(shell pkg-config --libs glib-2.0 ncursesw 2>/dev/null)
  endif
  VT_CFLAGS += -DTIMUI_WITH_VTERM_TESTS
  VT_SRCS := $(TSTDIR)/test_vt_roundtrip.c
endif

ifeq ($(NO_COLOR),)
C_RESET := \033[0m
C_BOLD  := \033[1m
C_CYAN  := \033[36m
C_GREEN := \033[32m
C_YELL  := \033[33m
endif

# ---- man page installation prefix (DESTDIR-aware, override on the CLI) ----- #
PREFIX  ?= /usr/local
MANDIR  := $(DESTDIR)$(PREFIX)/share/man/man1

# ---- optional: correct UAX #9 bidi via vendored SheenBidi (opt-in) --------- #
# The chat lays RTL out with an always-on cheap 2-level approximation
# (examples/chat_text.h). WITH_SHEENBIDI=1 additionally defines CHAT_SHEENBIDI so
# bidi_visual runs the FULL Unicode Bidirectional Algorithm (UAX #9) via the
# vendored SheenBidi (tools/vendor/SheenBidi, Apache-2.0), keeping the ARJOIN
# Arabic shaping as a pre-bidi step. SheenBidi is compiled as a SEPARATE object
# (its amalgamation Source/SheenBidi.c under -DSB_CONFIG_UNITY) and linked in only
# under the flag — chat.c stays a single TU. Default builds have ZERO dependency.
SHEENBIDI_DIR := $(TOOLDIR)/vendor/SheenBidi
SB_OBJ_FILE   := $(BLDDIR)/sheenbidi.o
SB_CFLAGS :=
SB_OBJ    :=
ifeq ($(WITH_SHEENBIDI),1)
  SB_CFLAGS := -DCHAT_SHEENBIDI -I$(SHEENBIDI_DIR)/Headers
  SB_OBJ    := $(SB_OBJ_FILE)
endif

# ---- Internet-radio example (examples/radio.c) --------------------------- #
# Vendored single-file deps: minimp3 (MP3 decode), miniaudio (playback),
# kissfft (real FFT). kissfft ships .c files, compiled alongside radio.c.
# Audio backends need OS link flags: CoreAudio/AudioToolbox/CoreFoundation on
# macOS; -ldl -lpthread on Linux. Third-party headers build under relaxed
# warnings (as vt_gif does); our timui + radio logic still builds under -Wall.
RADIO_KISS  := $(TOOLDIR)/vendor/kiss_fft.c $(TOOLDIR)/vendor/kiss_fftr.c
RADIO_CFLAGS := -std=c99 $(POSIX_CFLAGS) -O2 -pthread -Wall -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare
ifeq ($(UNAME_S),Darwin)
  RADIO_LDFLAGS := -framework CoreAudio -framework AudioToolbox -framework CoreFoundation -lm
else
  RADIO_LDFLAGS := -lm -ldl -lpthread
endif

# ---- SQLite TUI example (T4) ------------------------------------------- #
# The SQLite amalgamation is large; compile it ONCE into its own object and link
# it into the example (and the fixture helper). SQLITE_THREADSAFE=1 because timui
# is multi-threaded; the OMIT/DEFAULT feature flags keep the object lean.
# -Wall/-Wextra/-Wpedantic are dropped for the vendored C (not our code) and -w
# silences it. -lpthread everywhere; -ldl on Linux (elsewhere it lives in libc).
SQLITE_DIR  := $(TOOLDIR)/vendor/sqlite3
SQLITE_OBJ  := $(BLDDIR)/sqlite3.o
SQLITE_DEFS := -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION \
               -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_OMIT_DEPRECATED -DSQLITE_DQS=0
SQLITE_LIBS := -lpthread
ifeq ($(UNAME_S),Linux)
  SQLITE_LIBS += -ldl -lm
endif

# ============================================================================
# 2. BUILD RULES — help/build · example pattern rule · test & tool binaries · subsystem objects
# ============================================================================

.PHONY: help build test test-san run www check-www check-www-assets check-phase1-5-docs check-hosted-visual-windows check-conpty-evidence-artifacts verify-conpty-evidence amalgamate release-check fmt check clean goldens vt-test check-no-images check-conpty check-conpty-posix check-conpty-smoke-tool check-conpty-source-order check-conpty-win32-compile check-conpty-win32-smoke-compile check-chat-highlight check-chat-text man install-man check-chat-text-sheenbidi check-radio smoke-radio run-radio check-sqlite-tui run-sqlite-tui smoke-sqlite-tui check-grid check-layout check-tabs check-chart check-syntax run-gallery smoke-gallery check-image-smoke smoke-image-live smoke-image-live-auto smoke-image-live-kitty smoke-image-live-sixel smoke-image-live-iterm2 smoke-image-live-none smoke-conpty-win32 check-irc run-irc smoke-irc
.PHONY: accept check-vt-gif check-vt-gif-glyphs check-vt-gif-cjk check-vt-gif-emoji check-vt-gif-output check-vt-gif-golden gen-golden-vtgif check-vt-gif-style check-vt-gif-all
.PHONY: run-chat-demo rec-chat-demo gif-chat-demo webp-chat-demo gen-font-ttf gen-emoji gen-cjk

help: ## Show this help
	@printf "$(C_BOLD)timui.h$(C_RESET) — single-header C99 immediate-mode TUI\n"
	@printf "$(C_CYAN)usage: nix develop -c make <target>$(C_RESET)  $(C_YELL)(grouped by section)$(C_RESET)\n"
	@# Walk the file: a "# N. NAME — …" banner opens a group (printed lazily on its
	@# first documented target); each "target: ## desc" prints under it, name padded.
	@awk 'BEGIN{FS=":.*##"} \
	  /^# [0-9]+\. /{s=substr($$0,3);sub(/ *—.*/,"",s);sub(/^[0-9]+\. /,"",s);sec=s;shown=0;next} \
	  /^[a-zA-Z0-9_-]+:.*##/{if(!shown){printf "\n  $(C_BOLD)%s$(C_RESET)\n",sec;shown=1} \
	                         printf "    $(C_GREEN)%-24s$(C_RESET) %s\n",$$1,$$2}' $(MAKEFILE_LIST)
	@printf "\n  $(C_BOLD)WILDCARDS$(C_RESET)\n"
	@printf "    $(C_GREEN)%-24s$(C_RESET) %s\n" "run-<name>"   "run one example (editor procmon todo chat file_manager gallery image_smoke irc)"
	@printf "    $(C_GREEN)%-24s$(C_RESET) %s\n" "rec-<name>"   "record an interactive session -> recordings/<name>.cast"
	@printf "    $(C_GREEN)%-24s$(C_RESET) %s\n" "drive-<name>" "drive headless w/ recordings/<name>.in -> .raw + .txt"

build: $(EXAMPLES) $(BLDDIR)/radio ## Build all examples (single-header mode)
	@printf "$(C_GREEN)✓ build complete$(C_RESET)\n"

# SB_CFLAGS / SB_OBJ are EMPTY unless WITH_SHEENBIDI=1, so the default build is
# byte-for-byte unchanged (no SheenBidi dependency); under the flag the chat picks
# up -DCHAT_SHEENBIDI + the separately-compiled SheenBidi object.
$(BLDDIR)/%: $(EXADIR)/%.c $(HEADER) $(LIB_SECTIONS) $(SB_OBJ)
	@mkdir -p $(@D)
	@printf "$(C_CYAN)build$(C_RESET) $<\n"
	@$(CC) $(CFLAGS) $(SB_CFLAGS) -I$(INCDIR) $< $(SB_OBJ) -o $@

$(TEST_BIN): $(TEST_SRCS) $(HEADER) $(LIB_SECTIONS)
	@mkdir -p $(@D)
	@printf "$(C_CYAN)build$(C_RESET) tests\n"
	@$(CC) $(TESTCFLAGS) -I$(INCDIR) $(TEST_SRCS) -o $@

$(GOLDEN_BIN): $(TOOLDIR)/gen_golden.c $(HEADER) $(TSTDIR)/scenes.h $(LIB_SECTIONS)
	@mkdir -p $(@D)
	@printf "$(C_CYAN)build$(C_RESET) gen_golden\n"
	@$(CC) $(CFLAGS) -I$(INCDIR) $< -o $@

$(VT_BIN): $(TEST_SRCS) $(VT_SRCS) $(HEADER) $(LIB_SECTIONS)
	@mkdir -p $(@D)
	@printf "$(C_CYAN)build$(C_RESET) vt-tests\n"
	@$(CC) $(TESTCFLAGS) $(VT_CFLAGS) -I$(INCDIR) $(TEST_SRCS) $(VT_SRCS) $(VT_LIBS) -o $@

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

# SheenBidi amalgamation -> one object. Third-party C: relaxed warnings (as with
# the vt_gif vendored single-headers). -ISource resolves its <API/…>/<Core/…>
# unity includes; -IHeaders resolves the public <SheenBidi/…> umbrella.
$(SB_OBJ_FILE): $(SHEENBIDI_DIR)/Source/SheenBidi.c
	@mkdir -p $(@D)
	@printf "$(C_CYAN)build$(C_RESET) SheenBidi (amalgamation, UAX #9)\n"
	@$(CC) -std=c99 -O2 -DSB_CONFIG_UNITY -I$(SHEENBIDI_DIR)/Headers -I$(SHEENBIDI_DIR)/Source -c $< -o $@

# ---- Internet-radio: build, unit test, headless smoke -------------------- #
$(BLDDIR)/radio: $(EXADIR)/radio.c $(EXADIR)/radio_dsp.h $(RADIO_KISS) $(HEADER) $(LIB_SECTIONS) \
                 $(TOOLDIR)/vendor/minimp3.h $(TOOLDIR)/vendor/miniaudio.h $(TOOLDIR)/vendor/kiss_fftr.h
	@mkdir -p $(@D)
	@printf "$(C_CYAN)build$(C_RESET) $(EXADIR)/radio.c (+minimp3/miniaudio/kissfft)\n"
	@$(CC) $(RADIO_CFLAGS) -I$(INCDIR) -I$(EXADIR) -I$(TOOLDIR)/vendor \
	  $(EXADIR)/radio.c $(RADIO_KISS) $(RADIO_LDFLAGS) -o $@

$(SQLITE_OBJ): $(SQLITE_DIR)/sqlite3.c $(SQLITE_DIR)/sqlite3.h
	@mkdir -p $(@D)
	@printf "$(C_CYAN)build$(C_RESET) sqlite3 amalgamation (once, large)\n"
	@$(CC) -std=c99 -O2 -w $(SQLITE_DEFS) -c $< -o $@

# Explicit rule — overrides the generic $(BLDDIR)/% example rule so the example
# links sqlite3.o and sees the vendored sqlite3.h.
$(BLDDIR)/sqlite_tui: $(EXADIR)/sqlite_tui.c $(EXADIR)/sqlite_table.h $(EXADIR)/chat_highlight.h $(HEADER) $(LIB_SECTIONS) $(SQLITE_OBJ)
	@mkdir -p $(@D)
	@printf "$(C_CYAN)build$(C_RESET) $<\n"
	@$(CC) $(CFLAGS) -I$(INCDIR) -I$(EXADIR) -I$(SQLITE_DIR) $< $(SQLITE_OBJ) $(SQLITE_LIBS) -o $@

# Fixture builder for the headless smoke (vendored sqlite C API, no network/CLI).
$(BLDDIR)/sqlite_mkfixture: $(TOOLDIR)/sqlite_mkfixture.c $(SQLITE_DIR)/sqlite3.h $(SQLITE_OBJ)
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) -I$(SQLITE_DIR) $< $(SQLITE_OBJ) $(SQLITE_LIBS) -o $@

# ============================================================================
# 3. RUN — build and launch an example or app
# ============================================================================

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

run-radio: $(BLDDIR)/radio ## Build + run the internet-radio player (auto-connects FIP)
	@./$(BLDDIR)/radio --play

run-sqlite-tui: $(BLDDIR)/sqlite_tui ## Run the SQLite TUI (DB=path, default :memory:)
	@./$(BLDDIR)/sqlite_tui $(if $(DB),$(DB),:memory:)

# ---- Widget gallery (examples/gallery.c) --------------------------------- #
run-gallery: $(BLDDIR)/gallery ## Run the widget + layout gallery showcase
	@./$(BLDDIR)/gallery

# ---- IRC client (examples/irc.c) ----------------------------------------- #
# No args -> the offline --demo (canned transcript, no network). Pass HOST=… to
# connect live over plaintext TCP (best-effort): make run-irc HOST=irc.libera.chat
run-irc: $(BLDDIR)/irc ## Run the IRC client (HOST=… to connect; default offline demo)
	@./$(BLDDIR)/irc $(if $(HOST),--connect $(HOST)) $(if $(NICK),--nick $(NICK)) $(if $(CHAN),--channel $(CHAN))

# ============================================================================
# 4. REC / DRIVE — record or headlessly drive a session
# ============================================================================

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

rec-chat-demo: $(BLDDIR)/chat ## Screen-record hint, then autoplay the chat demo
	@printf "$(C_CYAN)Start a screen recorder$(C_RESET) (Kap / QuickTime) on this Ghostty window,\n"
	@printf "then press Enter to autoplay the demo (~26s). Turn the capture into a GIF with:\n"
	@printf "  $(C_YELL)ffmpeg -i cap.mov -vf 'fps=15,scale=900:-1:flags=lanczos' chat.gif$(C_RESET)\n"
	@read _ && ./$(BLDDIR)/chat --demo examples/chat.demo

# ============================================================================
# 5. CHECK — unit tests · goldens · acceptance · per-subsystem standalone checks
# ============================================================================

check: build test check-no-images check-conpty-smoke-tool check-conpty-source-order check-conpty-win32-compile check-conpty-win32-smoke-compile check-hosted-visual-windows check-conpty-evidence-artifacts check-www check-phase1-5-docs ## Build + test gate
	@printf "$(C_GREEN)✓ check passed$(C_RESET)\n"

test: $(TEST_BIN) ## Compile and run the unit tests
	@printf "$(C_YELL)▶ running tests$(C_RESET)\n"
	@./$(TEST_BIN)

test-san: ## Compile + run unit tests under a sanitizer: make test-san SAN=address
	@mkdir -p $(BLDDIR)
	@$(CC) -std=c99 $(POSIX_CFLAGS) -Wall -Wextra -Wpedantic -O1 -g -fsanitize=$(SAN) -I$(INCDIR) $(TEST_SRCS) -o $(BLDDIR)/test_san
	@./$(BLDDIR)/test_san

vt-test: build ## Compile + run unit tests WITH vterm round-trip tests (needs libvterm-neovim)
	@$(MAKE) $(VT_BIN) WITH_VTERM=1
	@printf "$(C_YELL)▶ running vt-tests$(C_RESET)\n"
	@./$(VT_BIN)

check-phase1-5-docs: ## Verify Phase 1.5 evidence docs agree
	@awk '{$$1=$$1; printf "%s ", $$0}' docs/runbooks/phase1-5-live-evidence.md | grep -Fq -- 'Run `28986249841` is the accepted hosted macOS iTerm2 baseline'
	@grep -Fq -- 'Hosted macOS iTerm2 now has accepted timui OSC 1337 visual evidence.' docs/handoff/2026-07-09-hosted-visual-probe.md
	@awk '{$$1=$$1; printf "%s ", $$0}' docs/backlog.md | grep -Fq -- '- [x] iTerm2 terminal evidence via hosted macOS iTerm2 run `28986249841`.'
	@! grep -Fq -- '- [ ] iTerm2 terminal evidence.' docs/backlog.md
	@awk '{$$1=$$1; printf "%s ", $$0}' docs/gaps.md | grep -Fq -- 'Image protocol live evidence is accepted for Windows Terminal Sixel and hosted macOS iTerm2.'
	@grep -Fq -- 'DONE: iTerm2 terminal evidence via hosted macOS iTerm2 run `28986249841`.' docs/goals/phase1_5-platform-widgets-style-text-image.goal.txt
	@awk '{$$1=$$1; printf "%s ", $$0}' docs/goals/phase1_5-platform-widgets-style-text-image.goal.txt | grep -Fq -- 'remaining Windows ConPTY live evidence gate'
	@awk '{$$1=$$1; printf "%s ", $$0}' docs/TERMINAL_PROTOCOLS.md | grep -Fq -- 'Hosted Windows Terminal Sixel evidence is accepted in run `28982641529`; hosted macOS iTerm2 evidence is accepted in run `28986249841`.'
	@grep -Fq -- 'make verify-conpty-evidence ARTIFACT_DIR=' docs/runbooks/phase1-5-live-evidence.md
	@printf "$(C_GREEN)✓ Phase 1.5 evidence docs$(C_RESET)\n"

check-hosted-visual-windows: tools/ci/hosted_visual_windows.ps1 ## Verify hosted Windows probe emits ConPTY acceptance artifacts
	@grep -Fq -- 'conpty-acceptance.json' tools/ci/hosted_visual_windows.ps1
	@grep -Fq -- 'conpty-smoke.command.txt' tools/ci/hosted_visual_windows.ps1
	@grep -Fq -- 'conpty-smoke.meta.txt' tools/ci/hosted_visual_windows.ps1
	@grep -Fq -- 'passTokenPresent' tools/ci/hosted_visual_windows.ps1
	@grep -Fq -- 'accepted' tools/ci/hosted_visual_windows.ps1
	@grep -Fq -- 'PASS conpty smoke: observed TIMUI_CONPTY_SMOKE' tools/ci/hosted_visual_windows.ps1
	@grep -Fq -- 'ConvertTo-Json' tools/ci/hosted_visual_windows.ps1
	@grep -Fq -- 'return (Invoke-Captured $$Name' tools/ci/hosted_visual_windows.ps1
	@printf "$(C_GREEN)✓ hosted Windows ConPTY evidence manifest$(C_RESET)\n"

check-conpty-evidence-artifacts: tests/test_conpty_evidence.py tools/verify_conpty_evidence.py ## Verify hosted ConPTY artifact acceptance predicates
	@TIMUI_TEST_COMMIT=$$(git rev-parse HEAD) python3 tests/test_conpty_evidence.py
	@printf "$(C_GREEN)✓ hosted ConPTY evidence artifact verifier$(C_RESET)\n"

verify-conpty-evidence: tools/verify_conpty_evidence.py ## Validate downloaded hosted ConPTY evidence (ARTIFACT_DIR=... [COMMIT=...])
	@[ -n "$(ARTIFACT_DIR)" ] || { printf "$(C_YELL)ARTIFACT_DIR is required$(C_RESET)\n"; exit 2; }
	@commit="$(COMMIT)"; \
	  if [ -z "$$commit" ]; then commit=$$(git rev-parse HEAD); fi; \
	  python3 tools/verify_conpty_evidence.py --artifact-dir "$(ARTIFACT_DIR)" --commit "$$commit"

check-no-images: $(TSTDIR)/test_no_images.c $(HEADER) $(LIB_SECTIONS) ## Test TIMUI_NO_IMAGES keeps API but disables terminal image escapes
	@mkdir -p $(BLDDIR)
	@printf "$(C_CYAN)build$(C_RESET) no-images test\n"
	@$(CC) $(CFLAGS) -DTIMUI_NO_IMAGES -I$(INCDIR) $(TSTDIR)/test_no_images.c -o $(BLDDIR)/test_no_images
	@./$(BLDDIR)/test_no_images \
	  && printf "$(C_GREEN)✓ TIMUI_NO_IMAGES$(C_RESET) standalone tests passed\n" \
	  || { printf "$(C_YELL)✗ TIMUI_NO_IMAGES$(C_RESET) tests failed\n"; exit 1; }

check-conpty-posix: $(TEST_BIN) ## Run POSIX ConPTY fallback/helper coverage
	@printf "$(C_YELL)▶ running ConPTY POSIX helper tests$(C_RESET)\n"
	@./$(TEST_BIN)

check-conpty-smoke-tool: $(TSTDIR)/test_conpty_smoke_tool.c $(TOOLDIR)/conpty_smoke_win32.c $(HEADER) $(LIB_SECTIONS) ## Test portable ConPTY smoke-tool helpers
	@mkdir -p $(BLDDIR)
	@printf "$(C_CYAN)build$(C_RESET) ConPTY smoke helper tests\n"
	@$(CC) $(CFLAGS) -I$(INCDIR) $(TSTDIR)/test_conpty_smoke_tool.c -o $(BLDDIR)/test_conpty_smoke_tool
	@./$(BLDDIR)/test_conpty_smoke_tool

check-conpty-source-order: $(TSTDIR)/test_conpty_source.py $(SRCDIR)/timui_conpty.c ## Verify ConPTY handle lifetime ordering
	@python3 $(TSTDIR)/test_conpty_source.py
	@printf "$(C_GREEN)✓ ConPTY source ordering$(C_RESET)\n"

check-conpty-win32-compile: ## Cross-compile the isolated Win32 ConPTY backend when mingw is available
	@mkdir -p $(BLDDIR)
	@if ! command -v $(CONPTY_WIN_CC) >/dev/null 2>&1; then \
	  printf "$(C_YELL)SKIP$(C_RESET) $(CONPTY_WIN_CC) not found; install the nix dev shell cross compiler\n"; \
	  exit 0; \
	fi
	@printf "$(C_CYAN)build$(C_RESET) isolated Win32 ConPTY compile seam\n"
	@$(CONPTY_WIN_CC) $(CONPTY_WIN_CFLAGS) -I$(INCDIR) -c $(TSTDIR)/test_conpty_win32_compile.c -o $(BLDDIR)/test_conpty_win32_compile.o
	@printf "$(C_GREEN)✓ Win32 ConPTY compile seam$(C_RESET)\n"

check-conpty-win32-smoke-compile: ## Cross-compile the operator Win32 ConPTY smoke runner when mingw is available
	@mkdir -p $(BLDDIR)
	@if ! command -v $(CONPTY_WIN_CC) >/dev/null 2>&1; then \
	  printf "$(C_YELL)SKIP$(C_RESET) $(CONPTY_WIN_CC) not found; install the nix dev shell cross compiler\n"; \
	  exit 0; \
	fi
	@printf "$(C_CYAN)build$(C_RESET) Win32 ConPTY operator smoke runner\n"
	@$(CONPTY_WIN_CC) $(CONPTY_WIN_CFLAGS) -I$(INCDIR) $(TOOLDIR)/conpty_smoke_win32.c -o $(BLDDIR)/conpty_smoke_win32.exe
	@printf "$(C_GREEN)✓ Win32 ConPTY operator smoke runner compiles$(C_RESET)\n"

check-conpty: check-conpty-posix check-conpty-smoke-tool check-conpty-source-order check-conpty-win32-compile ## Run ConPTY helper + optional Win32 compile checks

goldens: $(GOLDEN_BIN) ## Regenerate tests/golden/*.txt snapshots
	@mkdir -p tests/golden
	@./$(GOLDEN_BIN)
	@printf "$(C_GREEN)✓ goldens regenerated$(C_RESET)\n"

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

# Assert bold/italic render distinctly (the SGR attrs pick the DejaVu variant).
check-vt-gif-style: $(BLDDIR)/vt_gif ## Assert bold/italic use font variants
	@printf 'a' > $(BLDDIR)/_vtg_r.raw
	@printf '\033[1ma\033[0m' > $(BLDDIR)/_vtg_b.raw
	@printf '\033[3ma\033[0m' > $(BLDDIR)/_vtg_i.raw
	@./$(BLDDIR)/vt_gif --cols 2 --rows 1 --cell-h 32 --png $(BLDDIR)/_vtg_r.png $(BLDDIR)/_vtg_r.raw 2>/dev/null
	@./$(BLDDIR)/vt_gif --cols 2 --rows 1 --cell-h 32 --png $(BLDDIR)/_vtg_b.png $(BLDDIR)/_vtg_b.raw 2>/dev/null
	@./$(BLDDIR)/vt_gif --cols 2 --rows 1 --cell-h 32 --png $(BLDDIR)/_vtg_i.png $(BLDDIR)/_vtg_i.raw 2>/dev/null
	@if cmp -s $(BLDDIR)/_vtg_r.png $(BLDDIR)/_vtg_b.png || cmp -s $(BLDDIR)/_vtg_r.png $(BLDDIR)/_vtg_i.png; then \
	   printf "$(C_YELL)✗ vt_gif$(C_RESET) bold/italic == regular (variant not applied)\n"; exit 1; \
	 else printf "$(C_GREEN)✓ vt_gif$(C_RESET) bold/italic use font variants\n"; fi

# Run every vt_gif renderer check.
check-vt-gif-all: check-vt-gif check-vt-gif-glyphs check-vt-gif-style check-vt-gif-cjk check-vt-gif-emoji check-vt-gif-output check-vt-gif-golden ## All vt_gif renderer checks
	@printf "$(C_GREEN)✓ vt_gif: all renderer checks passed$(C_RESET)\n"

# Standalone test for examples/chat_highlight.h — the chat example's pure-C99
# syntax highlighter. Compiles the header-only tokenizer against its own test
# main (NO timui library, NO tests/test.h) under the full project CFLAGS, then
# runs it. Kept out of `make test` so the highlighter is exercisable on its own.
check-chat-highlight: $(TSTDIR)/test_chat_highlight.c $(EXADIR)/chat_highlight.h ## Test the chat syntax highlighter (standalone)
	@mkdir -p $(BLDDIR)
	@printf "$(C_CYAN)build$(C_RESET) chat_highlight test\n"
	@$(CC) $(CFLAGS) -I$(EXADIR) $(TSTDIR)/test_chat_highlight.c -o $(BLDDIR)/test_chat_highlight
	@./$(BLDDIR)/test_chat_highlight \
	  && printf "$(C_GREEN)✓ chat_highlight$(C_RESET) standalone tests passed\n" \
	  || { printf "$(C_YELL)✗ chat_highlight$(C_RESET) tests failed\n"; exit 1; }

# Standalone test for src/timui_syntax.c — the promoted library syntax
# highlighter (timui_highlight) + the code viewer (timui_code) and its pure
# helpers. Builds timui as a single TU (TIMUI_IMPLEMENTATION) so the test reaches
# both the pure lexer and the render path through the public API.
check-syntax: $(TSTDIR)/test_syntax.c $(HEADER) $(LIB_SECTIONS) ## Test the library syntax highlighter + code viewer (standalone)
	@mkdir -p $(BLDDIR)
	@printf "$(C_CYAN)build$(C_RESET) syntax test\n"
	@$(CC) $(CFLAGS) -I$(INCDIR) $(TSTDIR)/test_syntax.c -o $(BLDDIR)/test_syntax
	@./$(BLDDIR)/test_syntax \
	  && printf "$(C_GREEN)✓ syntax$(C_RESET) standalone tests passed\n" \
	  || { printf "$(C_YELL)✗ syntax$(C_RESET) tests failed\n"; exit 1; }

check-chat-text: $(TSTDIR)/test_chat_text.c $(EXADIR)/chat_text.h $(HEADER) $(LIB_SECTIONS) ## Test the chat text/wrap helpers (standalone)
	@mkdir -p $(BLDDIR)
	@printf "$(C_CYAN)build$(C_RESET) chat_text test\n"
	@$(CC) $(CFLAGS) -I$(INCDIR) -I$(EXADIR) $(TSTDIR)/test_chat_text.c -o $(BLDDIR)/test_chat_text
	@./$(BLDDIR)/test_chat_text \
	  && printf "$(C_GREEN)✓ chat_text$(C_RESET) standalone tests passed\n" \
	  || { printf "$(C_YELL)✗ chat_text$(C_RESET) tests failed\n"; exit 1; }

# Standalone test for the PURE data-grid math (column-fit + paging/scroll + tree
# flatten) that backs the enhanced table (timui_table_ex) and scrollable tree
# (timui_tree_scroll). Drives the TIMUI_API helpers directly — no frame, no TUI,
# deterministic + hand-computable. Kept out of `make test` so it is exercisable
# on its own (like check-chat-text / check-sqlite-tui).
check-grid: $(TSTDIR)/test_grid.c $(HEADER) $(LIB_SECTIONS) ## Test the data-grid math (column-fit + paging + tree flatten)
	@mkdir -p $(BLDDIR)
	@printf "$(C_CYAN)build$(C_RESET) grid test\n"
	@$(CC) $(CFLAGS) -I$(INCDIR) $(TSTDIR)/test_grid.c -o $(BLDDIR)/test_grid
	@./$(BLDDIR)/test_grid \
	  && printf "$(C_GREEN)✓ grid$(C_RESET) standalone tests passed\n" \
	  || { printf "$(C_YELL)✗ grid$(C_RESET) tests failed\n"; exit 1; }

# Standalone test for the constraint layout solver (src/timui_layout.c) and the
# box-frame / colour-lerp helpers (src/timui_box.c). Compiles timui as a single
# TU (TIMUI_IMPLEMENTATION) and drives the pure split/grid solver plus the
# frame-backed timui_border. Kept out of `make test` so it is exercisable alone.
check-layout: $(TSTDIR)/test_layout.c $(HEADER) $(LIB_SECTIONS) ## Test the layout solver + box helpers (standalone)
	@mkdir -p $(BLDDIR)
	@printf "$(C_CYAN)build$(C_RESET) layout test\n"
	@$(CC) $(CFLAGS) -I$(INCDIR) $(TSTDIR)/test_layout.c -o $(BLDDIR)/test_layout
	@./$(BLDDIR)/test_layout \
	  && printf "$(C_GREEN)✓ layout$(C_RESET) standalone tests passed\n" \
	  || { printf "$(C_YELL)✗ layout$(C_RESET) tests failed\n"; exit 1; }

# Standalone test for the tab-bar widget (src/timui_tabs.c, W2): the PURE
# layout/scroll/visibility geometry helpers on hand-computed vectors, plus the
# interactive widget driven through a fake transport (click + Left/Right). Built
# as a single TU under the full project CFLAGS. Kept out of `make test` so the
# widget is exercisable on its own (like the chat_text / sqlite_table checks).
check-tabs: $(TSTDIR)/test_tabs.c $(HEADER) $(LIB_SECTIONS) ## Test the tab-bar widget (standalone)
	@mkdir -p $(BLDDIR)
	@printf "$(C_CYAN)build$(C_RESET) tabs test\n"
	@$(CC) $(CFLAGS) -I$(INCDIR) $(TSTDIR)/test_tabs.c -o $(BLDDIR)/test_tabs
	@./$(BLDDIR)/test_tabs \
	  && printf "$(C_GREEN)✓ tabs$(C_RESET) standalone tests passed\n" \
	  || { printf "$(C_YELL)✗ tabs$(C_RESET) tests failed\n"; exit 1; }

# Same standalone test, but built WITH the full UAX #9 path (SheenBidi): defines
# CHAT_SHEENBIDI, adds the SheenBidi include path, and links the amalgamation
# object. Asserts the CORRECT visual order on hand-computed Hebrew/Arabic/mixed
# vectors. The default check-chat-text above stays untouched (approximation).
check-chat-text-sheenbidi: $(TSTDIR)/test_chat_text.c $(EXADIR)/chat_text.h $(HEADER) $(LIB_SECTIONS) $(SB_OBJ_FILE) ## Test chat_text WITH SheenBidi (full UAX #9 bidi)
	@mkdir -p $(BLDDIR)
	@printf "$(C_CYAN)build$(C_RESET) chat_text test (SheenBidi, UAX #9)\n"
	@$(CC) $(CFLAGS) -DCHAT_SHEENBIDI -I$(INCDIR) -I$(EXADIR) -I$(SHEENBIDI_DIR)/Headers $(TSTDIR)/test_chat_text.c $(SB_OBJ_FILE) -o $(BLDDIR)/test_chat_text_sb
	@./$(BLDDIR)/test_chat_text_sb \
	  && printf "$(C_GREEN)✓ chat_text+SheenBidi$(C_RESET) full UAX #9 tests passed\n" \
	  || { printf "$(C_YELL)✗ chat_text+SheenBidi$(C_RESET) tests failed\n"; exit 1; }

# Standalone unit test for the chart/indicator widgets (src/timui_chart.c): the
# pure helpers (colour lerp, filled-cell counting, peak-hold envelope, spinner
# frames) plus the widgets driven through a fake-transport test frame with
# hand-computed cell assertions. Built as a single TU (like check-chat-text).
check-chart: $(TSTDIR)/test_chart.c $(HEADER) $(LIB_SECTIONS) ## Test the chart/indicator widgets (standalone)
	@mkdir -p $(BLDDIR)
	@printf "$(C_CYAN)build$(C_RESET) chart test\n"
	@$(CC) $(CFLAGS) -I$(INCDIR) $(TSTDIR)/test_chart.c -lm -o $(BLDDIR)/test_chart
	@./$(BLDDIR)/test_chart \
	  && printf "$(C_GREEN)✓ chart$(C_RESET) standalone tests passed\n" \
	  || { printf "$(C_YELL)✗ chart$(C_RESET) tests failed\n"; exit 1; }

# Standalone unit test for the PURE DSP (examples/radio_dsp.h): a synthetic sine
# through the vendored real FFT must land in the right log band, and the peak-hold
# envelope must rise instantly + decay over N frames. Links kissfft; no audio.
check-radio: $(TSTDIR)/test_radio_dsp.c $(EXADIR)/radio_dsp.h $(RADIO_KISS) ## Test the radio DSP (FFT bands + peak-hold)
	@mkdir -p $(BLDDIR)
	@printf "$(C_CYAN)build$(C_RESET) radio_dsp test\n"
	@$(CC) -std=c99 -Wall -Wextra -O2 -I$(EXADIR) -I$(TOOLDIR)/vendor \
	  $(TSTDIR)/test_radio_dsp.c $(RADIO_KISS) -lm -o $(BLDDIR)/test_radio_dsp
	@./$(BLDDIR)/test_radio_dsp \
	  && printf "$(C_GREEN)✓ radio_dsp$(C_RESET) FFT-band + peak-hold tests passed\n" \
	  || { printf "$(C_YELL)✗ radio_dsp$(C_RESET) tests failed\n"; exit 1; }

check-sqlite-tui: $(TSTDIR)/test_sqlite_table.c $(EXADIR)/sqlite_table.h $(HEADER) $(LIB_SECTIONS) ## Test the pure table-layout helpers (standalone)
	@mkdir -p $(BLDDIR)
	@printf "$(C_CYAN)build$(C_RESET) sqlite_table test\n"
	@$(CC) $(CFLAGS) -I$(INCDIR) -I$(EXADIR) $(TSTDIR)/test_sqlite_table.c -o $(BLDDIR)/test_sqlite_table
	@./$(BLDDIR)/test_sqlite_table \
	  && printf "$(C_GREEN)✓ sqlite_table$(C_RESET) standalone tests passed\n" \
	  || { printf "$(C_YELL)✗ sqlite_table$(C_RESET) tests failed\n"; exit 1; }

# Standalone unit test for examples/irc_proto.h — the PURE, allocation-free
# RFC 1459/2812 message parser + command classifier. No timui library, no
# network: header-only parser driven on hand-computed positive + adversarial
# vectors. Kept out of `make test` so the protocol is exercisable on its own.
check-irc: $(TSTDIR)/test_irc.c $(EXADIR)/irc_proto.h ## Test the IRC message parser (standalone)
	@mkdir -p $(BLDDIR)
	@printf "$(C_CYAN)build$(C_RESET) irc_proto test\n"
	@$(CC) $(CFLAGS) -I$(EXADIR) $(TSTDIR)/test_irc.c -o $(BLDDIR)/test_irc
	@./$(BLDDIR)/test_irc \
	  && printf "$(C_GREEN)✓ irc_proto$(C_RESET) standalone tests passed\n" \
	  || { printf "$(C_YELL)✗ irc_proto$(C_RESET) tests failed\n"; exit 1; }

# ============================================================================
# 6. SMOKE — headless one-frame render smokes
# ============================================================================

# Headless smoke: drive the radio through a pty (no sound card, no network — the
# app runs with --no-audio --frames) and assert it renders its dashboard. Proves
# the binary launches + renders one frame with a dead audio device.
smoke-radio: $(BLDDIR)/radio $(BLDDIR)/pty_drive $(BLDDIR)/vt_render ## Headless radio smoke (renders a frame, no device)
	@mkdir -p $(RECDIR)
	@./$(BLDDIR)/pty_drive --cols 100 --rows 30 --run-ms 1200 --settle-ms 200 \
	   --out "$(RECDIR)/radio-smoke.raw" --delay-ms 4 -- ./$(BLDDIR)/radio --no-audio --frames 8 < /dev/null
	@./$(BLDDIR)/vt_render --cols 100 --rows 30 "$(RECDIR)/radio-smoke.raw" | grep -q 'Master mix spectrum' \
	  && printf "$(C_GREEN)✓ radio$(C_RESET) headless smoke rendered a frame (no audio device)\n" \
	  || { printf "$(C_YELL)✗ radio$(C_RESET) smoke: dashboard not rendered\n"; exit 1; }

smoke-sqlite-tui: $(BLDDIR)/sqlite_tui $(BLDDIR)/sqlite_mkfixture ## Headless smoke: temp db -> SELECT -> render 1 frame
	@rm -f $(BLDDIR)/_smoke.db $(BLDDIR)/_smoke.out $(BLDDIR)/_smoke.txt
	@./$(BLDDIR)/sqlite_mkfixture $(BLDDIR)/_smoke.db
	@./$(BLDDIR)/sqlite_tui $(BLDDIR)/_smoke.db \
	   --query "SELECT id,name,age FROM users ORDER BY id" --exit-after \
	   --cols 100 --rows 24 2>$(BLDDIR)/_smoke.out || { cat $(BLDDIR)/_smoke.out; exit 1; }
	@# reconstruct the rendered character stream from the cell grid (each snapshot
	@# cell is "CHAR|fg|bg|..", so per-cell text like "alice" is contiguous here).
	@awk -F'  *' '{s="";for(i=2;i<=NF;i++){n=split($$i,a,"|");c=(a[1]==""?" ":a[1]);s=s c} print s}' \
	   $(BLDDIR)/_smoke.out > $(BLDDIR)/_smoke.txt
	@grep -q 'rows=3 cols=3' $(BLDDIR)/_smoke.out \
	  && grep -q 'alice' $(BLDDIR)/_smoke.txt && grep -q 'carol' $(BLDDIR)/_smoke.txt \
	  && printf "$(C_GREEN)✓ sqlite_tui$(C_RESET) headless smoke: SELECT rendered 3 rows (alice/bob/carol)\n" \
	  || { printf "$(C_YELL)✗ sqlite_tui$(C_RESET) smoke failed\n"; cat $(BLDDIR)/_smoke.out; exit 1; }

smoke-gallery: $(BLDDIR)/gallery $(BLDDIR)/pty_drive $(BLDDIR)/vt_render ## Headless gallery smoke (renders a frame)
	@mkdir -p $(RECDIR)
	@./$(BLDDIR)/pty_drive --cols 100 --rows 30 --run-ms 900 --settle-ms 200 \
	   --out "$(RECDIR)/gallery-smoke.raw" -- ./$(BLDDIR)/gallery --frames 6 < /dev/null
	@./$(BLDDIR)/vt_render --cols 100 --rows 30 "$(RECDIR)/gallery-smoke.raw" | grep -q 'gallery' \
	  && printf "$(C_GREEN)✓ gallery$(C_RESET) headless smoke rendered a frame\n" \
	  || { printf "$(C_YELL)✗ gallery$(C_RESET) smoke: dashboard not rendered\n"; exit 1; }

check-image-smoke: $(BLDDIR)/image_smoke $(BLDDIR)/pty_drive $(BLDDIR)/vt_render ## Headless image protocol smoke harness sanity check
	@mkdir -p $(RECDIR)
	@./$(BLDDIR)/pty_drive --cols 96 --rows 28 --run-ms 1000 --settle-ms 200 \
	   --out "$(RECDIR)/image-smoke.raw" -- ./$(BLDDIR)/image_smoke --frames 4 --protocol none < /dev/null
	@out=$$(./$(BLDDIR)/vt_render --cols 96 --rows 28 "$(RECDIR)/image-smoke.raw"); \
	 echo "$$out" | grep -q 'image protocol smoke' && echo "$$out" | grep -q 'plain png' && echo "$$out" | grep -Fq '[img]' \
	  && printf "$(C_GREEN)✓ image_smoke$(C_RESET) headless harness rendered placeholders\n" \
	  || { printf "$(C_YELL)✗ image_smoke$(C_RESET) smoke: expected title/plain png/[img]\n"; echo "$$out"; exit 1; }
	@./$(BLDDIR)/pty_drive --cols 96 --rows 28 --run-ms 1000 --settle-ms 200 \
	   --out "$(RECDIR)/image-smoke-sixel.raw" -- ./$(BLDDIR)/image_smoke --frames 1 --protocol sixel < /dev/null
	@grep -Fq '"1;1;64;24' "$(RECDIR)/image-smoke-sixel.raw" \
	  && ! grep -Fq '"1;1;4;4' "$(RECDIR)/image-smoke-sixel.raw" \
	  && printf "$(C_GREEN)✓ image_smoke$(C_RESET) forced Sixel emits visible source-pixel rasters\n" \
	  || { printf "$(C_YELL)✗ image_smoke$(C_RESET) forced Sixel used tiny fixture rasters\n"; exit 1; }
	@./$(BLDDIR)/pty_drive --cols 96 --rows 28 --run-ms 1000 --settle-ms 200 \
	   --out "$(RECDIR)/image-smoke-iterm2.raw" -- ./$(BLDDIR)/image_smoke --frames 1 --protocol iterm2 < /dev/null
	@count=$$(grep -ao '1337;File=' "$(RECDIR)/image-smoke-iterm2.raw" | wc -l | tr -d ' '); \
	 out=$$(./$(BLDDIR)/vt_render --cols 96 --rows 28 "$(RECDIR)/image-smoke-iterm2.raw"); \
	 [ "$$count" = "2" ] && echo "$$out" | grep -q 'iTerm2 needs PNG' \
	  && printf "$(C_GREEN)✓ image_smoke$(C_RESET) forced iTerm2 skips raw RGBA-only payloads\n" \
	  || { printf "$(C_YELL)✗ image_smoke$(C_RESET) iTerm2 smoke: expected two OSC 1337 PNG-backed payloads and an unsupported-RGBA note\n"; printf 'count=%s\n%s\n' "$$count" "$$out"; exit 1; }

# Headless IRC smoke: run the client's OFFLINE --demo path (canned transcript,
# NO network) through a pty and assert the model+render pipeline works: the
# #timui channel tab, a rendered PRIVMSG line ("morning"), and a nick (alice)
# must all appear. Deterministic — the same irc_feed handler the socket uses.
smoke-irc: $(BLDDIR)/irc $(BLDDIR)/pty_drive $(BLDDIR)/vt_render ## Headless IRC smoke (canned transcript, no network)
	@mkdir -p $(RECDIR)
	@./$(BLDDIR)/pty_drive --cols 100 --rows 30 --run-ms 900 --settle-ms 200 \
	   --out "$(RECDIR)/irc-smoke.raw" -- ./$(BLDDIR)/irc --demo --frames 6 < /dev/null
	@out=$$(./$(BLDDIR)/vt_render --cols 100 --rows 30 "$(RECDIR)/irc-smoke.raw"); \
	 echo "$$out" | grep -q '#timui' && echo "$$out" | grep -q 'morning' && echo "$$out" | grep -q 'alice' \
	  && printf "$(C_GREEN)✓ irc$(C_RESET) headless smoke: #timui tab + PRIVMSG + nick rendered\n" \
	  || { printf "$(C_YELL)✗ irc$(C_RESET) smoke: expected #timui/morning/alice\n"; echo "$$out"; exit 1; }

# ============================================================================
# 7. OPERATOR SMOKE — live terminal / Windows evidence
# ============================================================================

smoke-image-live: $(BLDDIR)/image_smoke ## Live terminal image smoke (PROTO=...; optional FRAMES=N)
	@./$(BLDDIR)/image_smoke --protocol $(or $(PROTO),$(PROTOCOL),auto) $(if $(FRAMES),--frames $(FRAMES),)

smoke-image-live-auto: PROTO=auto
smoke-image-live-auto: smoke-image-live ## Live image smoke with detected protocol

smoke-image-live-kitty: PROTO=kitty
smoke-image-live-kitty: smoke-image-live ## Live image smoke forcing Kitty graphics

smoke-image-live-sixel: PROTO=sixel
smoke-image-live-sixel: smoke-image-live ## Live image smoke forcing Sixel

smoke-image-live-iterm2: PROTO=iterm2
smoke-image-live-iterm2: smoke-image-live ## Live image smoke forcing iTerm2 inline images

smoke-image-live-none: PROTO=none
smoke-image-live-none: smoke-image-live ## Live image smoke forcing text placeholders

smoke-conpty-win32: check-conpty-win32-smoke-compile ## Run the ConPTY smoke runner on Windows only
	@if [ "$${OS:-}" = "Windows_NT" ]; then \
	  ./$(BLDDIR)/conpty_smoke_win32.exe; \
	else \
	  printf "$(C_YELL)SKIP$(C_RESET) smoke-conpty-win32 must run on Windows\n"; \
	fi

# ============================================================================
# 8. GIF / RECORDING — render the chat demo to GIF / WebP / MP4
# ============================================================================

# Render the chat autoplay demo to an animated GIF *including* the Kitty images —
# fully headless (no screen recorder needed): drive with a timing sidecar, then
# rasterize each frame to pixels and encode the GIF.
gif-chat-demo: $(BLDDIR)/chat $(BLDDIR)/pty_drive $(BLDDIR)/vt_gif ## Headless: chat demo -> recordings/chat-demo.gif
	@mkdir -p $(RECDIR)
	@TERM=xterm-kitty ./$(BLDDIR)/pty_drive --cols 90 --rows 22 --settle-ms 1500 --run-ms 86000 \
	  --out $(RECDIR)/chat-demo.raw --timing $(RECDIR)/chat-demo.timing \
	  -- ./$(BLDDIR)/chat --demo examples/chat.demo < /dev/null
	@./$(BLDDIR)/vt_gif --cols 90 --rows 22 --fps 12 --system-fonts --system-emoji \
	  --outro 'https://timui.dev 👀' \
	  --timing $(RECDIR)/chat-demo.timing --gif $(RECDIR)/chat-demo.gif $(RECDIR)/chat-demo.raw
	@printf "$(C_CYAN)wrote$(C_RESET) $(RECDIR)/chat-demo.gif\n"

# Same demo as smaller MP4 + animated WebP (both far smaller than the GIF).
# MP4 via ffmpeg over vt_gif's --frames-dir PNG sequence (truecolour, tiny with
# H.264); WebP via gif2webp, which does inter-frame delta (ffmpeg's libwebp muxer
# does NOT, and balloons to ~15 MB). Runs gif-chat-demo first to get the capture.
webp-chat-demo: gif-chat-demo ## chat demo -> recordings/chat-demo.{webp,mp4} (smaller than GIF)
	@rm -rf $(RECDIR)/frames && mkdir -p $(RECDIR)/frames
	@./$(BLDDIR)/vt_gif --cols 90 --rows 22 --fps 12 --system-fonts --system-emoji \
	  --outro 'https://timui.dev 👀' --timing $(RECDIR)/chat-demo.timing \
	  --frames-dir $(RECDIR)/frames $(RECDIR)/chat-demo.raw
	@nix run nixpkgs#ffmpeg -- -y -framerate 12 -i $(RECDIR)/frames/frame_%05d.png \
	  -c:v libx264 -pix_fmt yuv420p -movflags +faststart $(RECDIR)/chat-demo.mp4 2>/dev/null
	@nix shell nixpkgs#libwebp -c gif2webp -q 65 -m 4 $(RECDIR)/chat-demo.gif -o $(RECDIR)/chat-demo.webp 2>/dev/null
	@printf "$(C_CYAN)wrote$(C_RESET) $(RECDIR)/chat-demo.{mp4,webp}\n"

# ============================================================================
# 9. ASSETS — regenerate vendored font / emoji / CJK headers
# ============================================================================

# Regenerate the subset TTF face header from DejaVu Sans Mono (via nix: fonttools).
gen-font-ttf: ## Regenerate tools/vendor/vt_font_ttf.h (subset DejaVu Sans Mono)
	@nix-shell -p 'python3.withPackages(ps: [ps.fonttools])' dejavu_fonts --run 'python3 tools/gen_font_ttf.py'

# Regenerate the bundled colour-emoji atlas from Twemoji (needs network, via nix).
gen-emoji: ## Regenerate tools/vendor/emoji_atlas.h (curated Twemoji PNGs)
	@nix-shell -p python3 --run 'python3 tools/gen_emoji.py'

# Regenerate the bundled CJK bitmap face from GNU Unifont's .bdf (via nix: unifont).
gen-cjk: ## Regenerate tools/vendor/vt_font_cjk.h (Unifont CJK bitmaps, deflated)
	@nix-shell -p python3 unifont --run 'python3 tools/gen_cjk.py'

# ============================================================================
# 10. PACKAGING / MISC — amalgamate · release-check · man page · fmt · clean
# ============================================================================

amalgamate: $(BLDDIR)/amalgamate $(HEADER) $(LIB_SECTIONS) ## Regenerate the flat release single-header into release/
	@mkdir -p $(RELDIR)
	@$(BLDDIR)/amalgamate $(HEADER) $(RELDIR)/timui.h
	@printf "$(C_GREEN)✓ wrote $(RELDIR)/timui.h$(C_RESET)\n"

www: amalgamate ## Refresh static website assets under www/
	@mkdir -p $(WWWDIR)
	@install -m 0644 $(RELDIR)/timui.h $(WWW_HEADER)
	@install -m 0644 LICENSE $(WWW_LICENSE)
	@$(MAKE) check-www-assets
	@printf "$(C_GREEN)✓ refreshed $(WWW_HEADER) and $(WWW_LICENSE)$(C_RESET)\n"

check-www: amalgamate check-www-assets ## Verify static website license, agent links, header freshness, and assets
	@grep -q '<h2>LICENSE</h2>' $(WWWDIR)/index.html
	@grep -q 'href="LICENSE"' $(WWWDIR)/index.html
	@grep -q 'Apache-2.0' $(WWWDIR)/index.html
	@grep -q '^## License$$' $(WWWDIR)/llms.txt
	@grep -q 'https://timui.dev/LICENSE' $(WWWDIR)/llms.txt
	@grep -q 'SPDX-License-Identifier: Apache-2.0' $(WWW_HEADER)
	@cmp -s $(RELDIR)/timui.h $(WWW_HEADER) || { printf "$(C_YELL)✗ www$(C_RESET) timui.h is stale; run make www\n"; exit 1; }
	@cmp -s LICENSE $(WWW_LICENSE)
	@printf "$(C_GREEN)✓ website license links$(C_RESET)\n"

check-www-assets: ## Verify local assets referenced by www/index.html exist
	@mkdir -p $(BLDDIR)
	@missing=0; refs="$(BLDDIR)/www-assets.refs"; \
	  sed -nE 's/.*(href|src)="([^"]+)".*/\2/p' $(WWWDIR)/index.html | sort -u > "$$refs"; \
	  while IFS= read -r ref; do \
	    case "$$ref" in ""|\#*|*:*|/*) continue;; esac; \
	    path=$${ref%%\#*}; path=$${path%%\?*}; \
	    test -z "$$path" && continue; \
	    if test ! -e "$(WWWDIR)/$$path"; then \
	      printf "$(C_YELL)✗ www$(C_RESET) missing local asset %s\n" "$$path"; \
	      missing=1; \
	    fi; \
	  done < "$$refs"; \
	  rm -f "$$refs"; \
	  if test "$$missing" -eq 0; then \
	    printf "$(C_GREEN)✓ www$(C_RESET) local asset links resolve\n"; \
	  else \
	    exit 1; \
	  fi

$(BLDDIR)/amalgamate: $(TOOLDIR)/amalgamate.c
	@mkdir -p $(@D)
	@$(CC) $(CFLAGS) $< -o $@

release-check: amalgamate ## Verify the amalgamated release header compiles standalone
	@printf "$(C_CYAN)build$(C_RESET) release self-test\n"
	@if grep -nE '#[[:space:]]*include[[:space:]]+"\\.\\./src/' $(RELDIR)/timui.h; then \
	  printf "$(C_YELL)✗ release header retained repo-relative src include$(C_RESET)\n"; exit 1; fi
	@tmp="$(BLDDIR)/release_selftest.d"; rm -rf "$$tmp"; mkdir -p "$$tmp"; \
	  install -m 0644 $(RELDIR)/timui.h "$$tmp/timui.h"; \
	  printf '#define TIMUI_IMPLEMENTATION\n#include "timui.h"\nint main(void){return 0;}\n' > "$$tmp/release_selftest.c"; \
	  $(CC) $(CFLAGS) -I"$$tmp" "$$tmp/release_selftest.c" -o "$$tmp/release_selftest"; \
	  printf '#define TIMUI_NO_IMAGES\n#define TIMUI_IMPLEMENTATION\n#include "timui.h"\nint main(void){return 0;}\n' > "$$tmp/release_selftest_no_images.c"; \
	  $(CC) $(CFLAGS) -I"$$tmp" "$$tmp/release_selftest_no_images.c" -o "$$tmp/release_selftest_no_images"
	@printf "$(C_GREEN)✓ release header compiles standalone$(C_RESET)\n"

# ---- man page ------------------------------------------------------------- #
# Render the pandoc-flavoured Markdown man page (docs/timui.1.md) to roff into
# build/, and install it DESTDIR-aware under $(PREFIX)/share/man/man1.
man: ## Render docs/timui.1.md -> build/timui.1 (roff, via pandoc)
	@mkdir -p $(BLDDIR)
	@printf "$(C_CYAN)pandoc$(C_RESET) docs/timui.1.md -> $(BLDDIR)/timui.1\n"
	@nix run nixpkgs#pandoc -- -s -t man docs/timui.1.md -o $(BLDDIR)/timui.1
	@printf "$(C_GREEN)✓ wrote $(BLDDIR)/timui.1$(C_RESET)\n"

install-man: man ## Install build/timui.1 to $(DESTDIR)$(PREFIX)/share/man/man1
	@install -d $(MANDIR)
	@install -m 644 $(BLDDIR)/timui.1 $(MANDIR)/timui.1
	@printf "$(C_GREEN)✓ installed$(C_RESET) $(MANDIR)/timui.1\n"

fmt: ## Format C sources if clang-format is available
	@if command -v clang-format >/dev/null 2>&1; then clang-format -i $(HEADER) $(SRCDIR)/*.c $(EXADIR)/*.c $(TSTDIR)/*.c $(TOOLDIR)/*.c; printf "$(C_GREEN)✓ formatted$(C_RESET)\n"; else printf "$(C_YELL)clang-format not found; skipping$(C_RESET)\n"; fi

clean: ## Remove build artifacts
	@rm -rf $(BLDDIR) $(RELDIR)
	@printf "$(C_GREEN)✓ clean$(C_RESET)\n"
