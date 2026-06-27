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
