---
type: Report
title: Hosted Visual Probe Follow-up
date: 2026-07-09
---

# Hosted Visual Probe Follow-up

## Landing State

- Branch: `runner-capture-research`
- Follow-up branch: `windows-visual-rca`
- Latest follow-up commit: `d683b5b`
  (`examples: keep Sixel smoke visible without cell pixels`)
- Remote push: `github windows-visual-rca` pushed through `d683b5b`
- GitHub workflow: `Hosted visual probes`
- Latest run inspected: `28982641529`
- Run URL: `https://github.com/zw3rk/timui.h/actions/runs/28982641529`

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
- Hosted Windows Terminal renders Sixel on GitHub-hosted Windows. RCA run
  `28982372688` rendered a direct 180x72 Sixel control in
  `windows-terminal-direct-sixel-12s.png`.
- Hosted Windows Terminal now has accepted timui Sixel visual evidence. Run
  `28982641529` produced `windows-terminal-sixel-12s.png` with all three
  `image_smoke` tiles visibly rendered; `timui-sixel-dcs-metrics.txt` records
  three `64x24` Sixel rasters.
- The Windows POSIX image-smoke build now succeeds under MSYS2 after passing the
  setup action's `msys2-location` output into the script and overriding
  `POSIX_CFLAGS` for MSYS C99 feature visibility.
- The workflow remains best-effort/manual. Sanity screenshots prove capture
  mechanics only; protocol screenshots still need manual inspection.

## Rejected / Inconclusive State

- Hosted iTerm2 image evidence is still rejected. iTerm2 installs, but
  `osascript.status` is `1`; screenshots show a macOS TCC prompt for `bash` to
  access screen/audio instead of the timui iTerm2 smoke.
- macOS iTerm2 privacy/TCC RCA: on hosted `macos-15`, the prompt is real user
  consent for direct screen/audio capture. It is not solved by `sudo`, Homebrew,
  or TCC.db sqlite edits; Apple PPPC does not provide a supported silent allow
  path for this hosted runner case. Prefer Terminal.app hosted screenshots.
- Historical Windows Sixel rejection was fixture-size, not renderer failure.
  Run `28982372688` showed direct Sixel rendered, while timui emitted three
  `4x4` rasters because the smoke fixture was 4x4 and MSYS/Windows Terminal did
  not report cell pixel geometry.
- Hosted Windows ConPTY smoke is still rejected. `conpty-smoke.status` is `2`;
  the runner compiles and starts `cmd.exe`, but the sentinel
  `TIMUI_CONPTY_SMOKE` is not observed.

## Artifact Results

- Local downloaded artifacts are under:
  - `artifacts/gh-runs/28980999976/`
  - `artifacts/gh-runs/28981256095/`
  - `artifacts/gh-runs/28981426012/`
  - `artifacts/gh-runs/28982372688/`
  - `artifacts/gh-runs/28982641529/`
- `/artifacts/` is gitignored scratch.
- Run `28982641529` Windows:
  - `image-smoke-build.status`: `0`
  - `conpty-smoke.status`: `2`
  - `direct-sixel-dcs-metrics.txt`: `raster=180x72`
  - `timui-sixel-dcs-metrics.txt`: three `raster=64x24`
  - `windows-terminal-direct-sixel-12s.png`: visible red direct Sixel block
  - `windows-terminal-sixel-12s.png`: visible timui image smoke UI and three
    rendered image tiles
- Run `28982372688` Windows RCA:
  - `direct-sixel-dcs-metrics.txt`: `raster=180x72`
  - `timui-sixel-dcs-metrics.txt`: three `raster=4x4`
  - Direct Sixel rendered; timui tiles were tiny because the smoke fixture was
    tiny.
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
- `nix develop -c make check-image-smoke` - failed after adding the visible
  Sixel predicate, then passed after the 64x24 smoke fixture change.
- GitHub hosted workflow runs: `28980999976`, `28981256095`, `28981426012`,
  `28982372688`, `28982641529`

## Next Safe Move

- Integrate `windows-visual-rca` into `master` after review/verification.
- For accepted iTerm2 evidence: avoid hosted iTerm2 unless a future macOS/TCC
  change makes the prompt disappear. Use Terminal.app hosted screenshots for
  GUI mechanics and deterministic protocol-byte evidence for iTerm2.
- For ConPTY: debug `tools/conpty_smoke_win32.c` on an interactive Windows host
  with live byte-stream logging before treating hosted ConPTY as an acceptance
  gate.
