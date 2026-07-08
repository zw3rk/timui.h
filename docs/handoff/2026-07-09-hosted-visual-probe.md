---
type: Report
title: Hosted Visual Probe Follow-up
date: 2026-07-09
---

# Hosted Visual Probe Follow-up

## Landing State

- Branch: `runner-capture-research`
- Integrated local master: `8cb8b99`
  (`ci: expose POSIX APIs in hosted image smoke`)
- Remote push: `github master` pushed through `8cb8b99`
- GitHub workflow: `Hosted visual probes`
- Latest run inspected: `28981426012`
- Run URL: `https://github.com/zw3rk/timui.h/actions/runs/28981426012`

## Read First

- `.github/workflows/hosted-visual-probe.yml`
- `tools/ci/hosted_visual_macos.sh`
- `tools/ci/hosted_visual_windows.ps1`
- `docs/runbooks/phase1-5-live-evidence.md`
- `docs/research/hosted-runner-capture/REVIEW.md`

## Accepted State

- Hosted macOS screenshot capture is possible. Run `28980999976` produced
  `terminal-sanity.png`, visibly showing Terminal.app with
  `TIMUI_HOSTED_SCREENSHOT_SANITY`.
- Hosted Windows screenshot capture is possible. Runs `28980999976` and
  `28981426012` produced `cmd-sanity.png`, visibly showing the sanity console in
  an active `runneradmin` console session.
- Hosted Windows Terminal can be launched and captured. Run `28981426012`
  produced `windows-terminal-sixel-12s.png` and `windows-terminal-sixel-24s.png`
  showing the `timui image smoke` UI inside Windows Terminal with
  `active: sixel (forced)`.
- The Windows POSIX image-smoke build now succeeds under MSYS2 after passing the
  setup action's `msys2-location` output into the script and overriding
  `POSIX_CFLAGS` for MSYS C99 feature visibility.
- The workflow remains best-effort/manual. Sanity screenshots prove capture
  mechanics only; protocol screenshots still need manual inspection.

## Rejected / Inconclusive State

- Hosted iTerm2 image evidence is still rejected. iTerm2 installs, but
  `osascript.status` is `1`; screenshots show a macOS TCC prompt for `bash` to
  access screen/audio instead of the timui iTerm2 smoke.
- Hosted Windows Sixel image evidence is still rejected. The Windows Terminal
  screenshots show the timui UI and forced Sixel mode, but the three image tiles
  are not visibly rendered as image tiles.
- Hosted Windows ConPTY smoke is still rejected. `conpty-smoke.status` is `2`;
  the runner compiles and starts `cmd.exe`, but the sentinel
  `TIMUI_CONPTY_SMOKE` is not observed.

## Artifact Results

- Local downloaded artifacts are under:
  - `artifacts/gh-runs/28980999976/`
  - `artifacts/gh-runs/28981256095/`
  - `artifacts/gh-runs/28981426012/`
- `/artifacts/` is gitignored scratch.
- Run `28981426012` Windows:
  - `image-smoke-build.status`: `0`
  - `conpty-smoke.status`: `2`
  - `sixel-dcs-count.txt`: `3`
  - `cmd-sanity.png`: visible sanity console
  - `windows-terminal-sixel-12s.png`: visible timui image smoke UI, no accepted
    image tiles
- Run `28981426012` macOS:
  - `image-smoke-build.status`: `0`
  - `open-terminal-sanity.status`: `0`
  - `terminal-sanity-screencapture.status`: `0`
  - `osascript.status`: `1`
  - `iterm2-screen-*.png`: TCC prompt, not timui iTerm2 smoke

## Verification Already Run

- `bash -n tools/ci/hosted_visual_macos.sh`
- `nix shell nixpkgs#powershell -c pwsh ... Parser.ParseFile(...)`
- `nix shell nixpkgs#actionlint -c actionlint .github/workflows/hosted-visual-probe.yml`
- `git diff --check`
- `nix develop -c make check-www`
- `nix develop -c make check-conpty-win32-smoke-compile`
- GitHub hosted workflow runs: `28980999976`, `28981256095`, `28981426012`

## Next Safe Move

- For accepted iTerm2 evidence: use a self-hosted/manual macOS GUI session with
  iTerm2 already installed and screen/automation permissions pre-granted, or
  avoid iTerm2 and keep hosted macOS for Terminal.app screenshot mechanics only.
- For accepted Windows Sixel evidence: inspect Windows Terminal's Sixel support
  on the hosted Windows Server 2025 image, including settings/version/feature
  flags; otherwise move pixel proof to a self-hosted Windows GUI runner.
- For ConPTY: debug `tools/conpty_smoke_win32.c` on an interactive Windows host
  with live byte-stream logging before treating hosted ConPTY as an acceptance
  gate.
