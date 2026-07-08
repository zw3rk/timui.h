# Phase 1.5 Live Evidence Runbook

Phase 1.5 implementation and wire-format tests are local. These remaining
checks require a real terminal or Windows host. This runbook defines what counts
as evidence, what does not, and the artifact text to record afterward.

## General Rules

- Run from a clean checkout at the commit being accepted.
- Use the project interface: `nix develop -c make <target>`.
- Record the exact command, commit, host OS, terminal app, terminal version, and
  whether a multiplexer was present.
- Record the shell and any compiler override used for Windows.
- Capture stdout/stderr where applicable. For image protocol evidence, include a
  screenshot or screen recording; raw escape streams are diagnostic only.
- Do not count headless pty runs, redirected escape streams, or fake transports
  as live terminal evidence.
- Prefer `FRAMES=2` for bounded image captures; omit `FRAMES` for a manual
  Escape-driven session.
- Put the final evidence note in `docs/handoff/2026-07-08-phase1-5-status.md`
  or a dated follow-up handoff, with screenshot/video paths if captured.

## Sixel

Prerequisites:

- A Sixel-capable terminal, outside tmux/screen/zellij unless deliberately
  testing a multiplexer.
- A visible terminal window large enough for the three image tiles.

Command:

```sh
nix develop -c make smoke-image-live PROTO=sixel FRAMES=2
```

Accept if all are true:

- The command exits with status 0.
- The screen says `active: sixel (forced)`.
- `plain png`, `raw rgba`, and `png+rgba sidecar` each show a visible image,
  not `[img]`.
- No terminal corruption remains after exit.

Reject or mark inconclusive if:

- The run is redirected to a file instead of viewed in a terminal.
- The terminal shows placeholders for the image tiles.
- The run happens under a multiplexer without a deliberately documented
  passthrough test.

## iTerm2

Prerequisites:

- iTerm2 or another terminal that explicitly supports OSC 1337 inline images.
- A visible terminal window large enough for the three image tiles.

Command:

```sh
nix develop -c make smoke-image-live PROTO=iterm2 FRAMES=2
```

Accept if all are true:

- The command exits with status 0.
- The screen says `active: iterm2 (forced)`.
- `plain png` and `png+rgba sidecar` show visible images.
- `raw rgba` may render `[img]`; iTerm2 inline images are PNG-backed in this
  implementation.
- No terminal corruption remains after exit.

Reject or mark inconclusive if:

- The PNG-backed tiles are placeholders.
- The run is a captured escape stream without visual confirmation.

## Windows ConPTY

Prerequisites:

- Windows Terminal on Windows.
- A Windows-capable build environment that can produce
  `build/conpty_smoke_win32.exe` from the Makefile target, or an equivalent
  already-built executable from the same commit.

Command:

```sh
nix develop -c make smoke-conpty-win32
```

If the local Windows environment uses a native GCC/clang rather than the MinGW
cross name, run with an explicit compiler override, for example:

```sh
nix develop -c make smoke-conpty-win32 CONPTY_WIN_CC=cc
```

Accept if all are true:

- The command exits with status 0.
- The output includes `PASS conpty smoke: observed TIMUI_CONPTY_SMOKE`.
- The note records Windows build, Windows Terminal version, compiler, command,
  commit, and whether this was run from PowerShell, cmd, MSYS, or another shell.

Reject or mark inconclusive if:

- The target prints the non-Windows skip.
- Only `check-conpty-win32-compile` or `check-conpty-win32-smoke-compile` ran.
- The smoke executable was built from a different commit.

## Evidence Template

```md
## Phase 1.5 Live Evidence - YYYY-MM-DD

- Commit:
- Worktree:
- Date:
- Host OS / build:
- Terminal app / version:
- Shell:
- Multiplexer:
- Command:
- Exit status:
- Visible result:
- stdout/stderr artifact path:
- Screenshot/video/artifact path:
- Accepted:
- Notes:
```
