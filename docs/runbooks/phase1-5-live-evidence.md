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
- A visible terminal window large enough for the two PNG-backed image tiles and
  the raw-RGBA note.

Command:

```sh
nix develop -c make smoke-image-live PROTO=iterm2 FRAMES=2
```

Accept if all are true:

- The command exits with status 0.
- The screen says `active: iterm2 (forced)`.
- `plain png` and `png+rgba sidecar` show visible images.
- `raw rgba` shows the explicit unsupported note. iTerm2 inline images are
  PNG-backed in this implementation; raw RGBA-only images are not emitted as
  OSC 1337 image payloads.
- No terminal corruption remains after exit.

Reject or mark inconclusive if:

- The PNG-backed tiles are placeholders.
- The raw-RGBA slot is treated as image evidence rather than as the expected
  unsupported note.
- The run is a captured escape stream without visual confirmation.

Hosted runner probe:

- Trigger `Hosted visual probes` from GitHub Actions.
- Download the `hosted-visual-macos-iterm2` artifact.
- First inspect `terminal-sanity.png`. It should show the
  `TIMUI_HOSTED_SCREENSHOT_SANITY` Terminal.app window; if it does not, the
  hosted macOS screenshot path is inconclusive before iTerm2 is considered.
- Prefer `iterm2-window-12s.png`. Accept it if it visibly shows the iTerm2 live
  smoke with `active: iterm2 (forced)`, PNG-backed image tiles for `plain png`
  and `png+rgba sidecar`, and an unsupported note for `raw rgba`. Run
  `28986249841` is the accepted hosted macOS iTerm2 baseline for this cleaned-up
  smoke.
- `iterm2-region-12s.png` and `iterm2-screen-*.png` may also count only if they
  visibly show the same live iTerm2 payload without TCC or other permission
  prompts obscuring the evidence.
- Treat `iterm2-api-session.json` as a supporting text predicate when it records
  `"screen_text_matched": true`. On run `28986249841`, the Python API connected
  and matched the screen text, but Homebrew iTerm2 3.6.11 rejected
  `Session.async_screenshot()` as too old for the screenshot RPC. If a future
  run produces `iterm2-api-session.png`, accept it only if the JSON predicate is
  true and the PNG visibly shows the live smoke with PNG-backed image tiles.
- If the iTerm2 path fails before window capture, inspect
  `iterm2-first-launch.*`, `iterm2-open.*`, `iterm2-direct-launch.*`, and the
  full-display screenshots first. Run `28984631494` failed because the
  Homebrew-installed iTerm2 cask was stopped at macOS first-launch/Gatekeeper
  confirmation, so a screenshot showing that prompt is diagnostic, not protocol
  evidence. Run `28985212786` then exposed the iTerm2
  `Allow Terminal-Initiated Display?` prompt, fixed by pre-seeding
  `NoSyncSuppressDownloadConfirmation`.
- Treat `iterm2.typescript`, `osc1337-count.txt`, text dumps, window lists, and
  the Terminal.app sanity screenshot as diagnostics, not iTerm2 protocol
  evidence.

## Windows ConPTY

Prerequisites:

- A Windows host with ConPTY support. Windows Terminal is useful for hosted GUI
  context and the Sixel visual probe, but the ConPTY smoke itself is a
  byte-stream predicate over `conpty-smoke.stdout`.
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
- The note records Windows build, compiler, command, commit, and whether this
  was run from PowerShell, cmd, MSYS, or another shell. Record Windows Terminal
  version too when the smoke is collected through the hosted visual workflow or
  an interactive Windows Terminal session.

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
- `conpty-acceptance.json` is the machine-readable summary for hosted runs. It
  must name the same commit, set `passTokenPresent: true`, set `accepted: true`,
  and reference `conpty-smoke.stdout`, `conpty-smoke.stderr`, and
  `conpty-smoke.status`. If it is absent, fall back to the stdout predicate
  above and record that the manifest was unavailable.
- `conpty-smoke.command.txt` and `conpty-smoke.meta.txt` are diagnostics for
  the exact hosted MSYS2 invocation and host/toolchain metadata.
- For current artifacts, validate the machine predicate before updating any
  docs:

  ```sh
  nix develop -c make verify-conpty-evidence ARTIFACT_DIR=artifacts/gh-runs/<run>/hosted-visual-windows-terminal COMMIT=<commit>
  ```

  The verifier requires `conpty-acceptance.json`, `evidence.md`,
  `conpty-smoke.stdout`, `conpty-smoke.stderr`, `conpty-smoke.status`,
  `conpty-smoke.command.txt`, and `conpty-smoke.meta.txt` to agree on the same
  commit, status 0, the exact ConPTY PASS token, a `smoke-conpty-win32`
  command, and `os_env=Windows_NT` host metadata.
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
