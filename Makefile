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

HEADER    := $(INCDIR)/timui.h
EXAMPLES  := $(patsubst $(EXADIR)/%.c,$(BLDDIR)/%,$(wildcard $(EXADIR)/*.c))
TEST_SRCS := $(SRCDIR)/timui_core.c $(TSTDIR)/test_main.c $(TSTDIR)/test_rect.c $(TSTDIR)/test_result.c $(TSTDIR)/test_arena.c $(TSTDIR)/test_strings.c $(TSTDIR)/test_id_stack.c $(TSTDIR)/test_msgq.c $(TSTDIR)/test_mpsc.c $(TSTDIR)/test_transport.c $(TSTDIR)/test_screen.c $(TSTDIR)/test_input.c $(TSTDIR)/test_mouse.c $(TSTDIR)/test_termios.c $(TSTDIR)/test_size.c $(TSTDIR)/test_caps.c $(TSTDIR)/test_kitty.c $(TSTDIR)/test_sync.c
TEST_BIN  := $(BLDDIR)/test_unit

ifeq ($(NO_COLOR),)
C_RESET := \033[0m
C_BOLD  := \033[1m
C_CYAN  := \033[36m
C_GREEN := \033[32m
C_YELL  := \033[33m
endif

.PHONY: help build test run amalgamate fmt check clean

help: ## Show this help
	@printf "$(C_BOLD)timui.h$(C_RESET) — single-header C99 immediate-mode TUI\n\n"
	@printf "$(C_CYAN)usage: nix develop -c make <target>$(C_RESET)\n\n"
	@printf "  $(C_BOLD)targets$(C_RESET)\n"
	@awk 'BEGIN {FS = ":.*##"} /^[a-zA-Z_-]+:.*##/ { printf "  $(C_GREEN)%-14s$(C_RESET) %s\n", $$1, $$2 }' $(MAKEFILE_LIST)

build: $(EXAMPLES) ## Build all examples (single-header mode)
	@printf "$(C_GREEN)✓ build complete$(C_RESET)\n"

$(BLDDIR)/%: $(EXADIR)/%.c $(HEADER)
	@mkdir -p $(@D)
	@printf "$(C_CYAN)build$(C_RESET) $<\n"
	@$(CC) $(CFLAGS) -I$(INCDIR) $< -o $@

test: $(TEST_BIN) ## Compile and run the unit tests
	@printf "$(C_YELL)▶ running tests$(C_RESET)\n"
	@./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRCS) $(HEADER)
	@mkdir -p $(@D)
	@printf "$(C_CYAN)build$(C_RESET) tests\n"
	@$(CC) $(TESTCFLAGS) -I$(INCDIR) $(TEST_SRCS) -o $@

run: build ## Build and run the hello example
	@./$(BLDDIR)/hello

amalgamate: $(BLDDIR)/amalgamate $(HEADER) ## Regenerate the release single-header into release/
	@mkdir -p $(RELDIR)
	@./$(BLDDIR)/amalgamate $(RELDIR)/timui.h $(HEADER)
	@printf "$(C_GREEN)✓ wrote $(RELDIR)/timui.h$(C_RESET)\n"

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
