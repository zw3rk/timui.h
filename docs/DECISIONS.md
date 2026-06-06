# DECISIONS — timui.h

Locked project decisions (PRD task T0.1). Date: 2026-07-02.

## License
- **Apache-2.0** (SPDX `Apache-2.0`). Overrides the PRD's MIT/0BSD suggestion; matches the project-wide default.
- Copyright: `Moritz Angermann <moritz@zw3rk.com>, zw3rk pte. ltd.`
- Every source file carries the SPDX identifier + copyright header.

## Language & baseline
- **C99** (`-std=c99`). No C++ / C11 required.
- Warning gate: `-Wall -Wextra -Wpedantic` clean.

## Dependencies
- Hard: a C99 compiler + libc.
- POSIX backend (Phase 2): termios / unistd / fcntl / poll-or-select / ioctl(TIOCGWINSZ) / signals.
- Threading: optional **pthreads**; a `TIMUI_NO_THREADS` build must still compile and link.

## Naming
- functions: `timui_*`
- macros / enumerator prefix: `TIMUI_*`
- structs / typedefs: `Timui*`
- opaque handles: `Timui`, `TimuiFrame`.

## Error handling
- Fallible public APIs return **`TimuiResult`**; stringify via `timui_error_string()`.
- Simple widget predicates return `bool`.

## Architecture (Phase 0)
- **Single-header, stb-style.** `include/timui.h` is the canonical source of truth; `#define TIMUI_IMPLEMENTATION` includes the implementation in exactly one TU.
- Split build via `src/timui_core.c`. `tools/amalgamate.c` emits a release header from one or more parts (multi-part split deferred).
- **No hidden global mutable state.** Runtime state lives in `Timui`, frame state in `TimuiFrame`, all app state is caller-owned. Functional / immutable-by-default; `_mut` wrappers for convenience.
- Single-writer (UI thread) / many-producer (worker `timui_post`) threading.

## Entry point
- The **Makefile** is the sole interface, driven through `nix develop -c make`. No direct `cc`/`clang` invocations outside the nix shell.

## Deferred (later phases)
- Full public API surface (events, widgets, themes) — Phase 1+.
- Terminal backend (raw mode, caps, input parsing, sync output) — Phase 2.
- Rendering (cell buffer, diff, cursor) — Phase 3.
- Interaction core + widgets — Phase 4/5.
- Generated Unicode width/grapheme tables, OSC 8, Kitty graphics, Windows ConPTY.
