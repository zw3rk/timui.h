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
- The manual GitHub Actions workflow
  `.github/workflows/hosted-visual-probe.yml` may be used as a best-effort
  hosted runner probe. Its screenshots count only after manual inspection shows
  the expected visible terminal result. Screenshot-sanity artifacts such as
  `terminal-sanity.png` or `cmd-sanity.png` prove capture mechanics only; they
  are not image-protocol evidence. Raw escape streams, DCS/OSC marker counts,
  process lists, session diagnostics, and launch logs are diagnostics only.
  iTerm2's own Python API screenshot artifact is a native-terminal visual
  artifact, but still requires manual inspection before acceptance.
- Hosted Windows GUI screenshots count when the artifact clearly shows the
  expected terminal payload and records an active interactive session. Run
  `28982641529` is accepted Sixel evidence. If a future Windows GUI predicate
  cannot be satisfied on hosted runners, use a self-hosted Windows runner
  launched from an autologon interactive session, not a runner service.
- The hosted Windows job uses native MSYS2 `make` rather than `nix develop`
  because the flake currently declares Linux and Darwin systems only. That is a
  documented CI exception for this evidence probe, not a project-wide toolchain
  bypass.
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

Hosted runner probe:

- Trigger `Hosted visual probes` from GitHub Actions.
- Download the `hosted-visual-macos-iterm2` artifact.
- First inspect `terminal-sanity.png`. It should show the
  `TIMUI_HOSTED_SCREENSHOT_SANITY` Terminal.app window; if it does not, the
  hosted macOS screenshot path is inconclusive before iTerm2 is considered.
- Prefer `iterm2-api-session.png`. Accept it only if
  `iterm2-api-overlay.status` and `iterm2-api-capture.status` are `0`,
  `iterm2-api-source-commit.txt` is present, `iterm2-api-session.json` records
  `"screen_text_matched": true`, and the PNG visibly shows the iTerm2 live smoke
  with PNG-backed image tiles. This path uses iTerm2's own session screenshot
  API, not macOS global screen capture.
- If the iTerm2 API path fails before connection, inspect
  `iterm2-first-launch.*`, `iterm2-open.*`, `iterm2-direct-launch.*`, and the
  full-display screenshots first. Run `28984631494` failed because the
  Homebrew-installed iTerm2 cask was stopped at macOS first-launch/Gatekeeper
  confirmation, so a screenshot showing that prompt is diagnostic, not protocol
  evidence.
- Direct macOS screenshots such as `iterm2-screen-*.png`,
  `iterm2-window-*.png`, and `iterm2-region-*.png` may also count only if they
  visibly show the iTerm2 live smoke with PNG-backed image tiles and no TCC
  prompt. Treat `iterm2.typescript`, `osc1337-count.txt`, text dumps, window
  lists, and the Terminal.app sanity screenshot as diagnostics, not iTerm2
  protocol evidence.

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

Hosted runner probe:

- Trigger `Hosted visual probes` from GitHub Actions.
- Download the `hosted-visual-windows-terminal` artifact.
- First inspect `cmd-sanity.png`. It should show the
  `TIMUI_HOSTED_SCREENSHOT_SANITY` console window; if it does not, the hosted
  Windows screenshot path is inconclusive before Windows Terminal is considered.
- `conpty-smoke.stdout` may count for the ConPTY smoke if it contains
  `PASS conpty smoke: observed TIMUI_CONPTY_SMOKE` and `evidence.md` records
  the same commit.
- `windows-terminal-sixel-*.png` is supplemental Sixel evidence only if it
  visibly shows the Windows Terminal live smoke with image tiles and the
  session diagnostics show an interactive desktop. Run `28982641529` is the
  first accepted hosted Windows example: the direct Sixel control rendered,
  `timui-sixel-dcs-metrics.txt` reported three `64x24` rasters, and
  `windows-terminal-sixel-12s.png` visibly showed all three timui image tiles.
  Treat `sixel.typescript`, DCS counts/metrics, process lists, and the cmd
  sanity screenshot as diagnostics that support, but do not replace, visual
  terminal evidence.

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
