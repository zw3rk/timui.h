---
type: Report
title: Hosted Visual Probe Follow-up
date: 2026-07-09
---

# Hosted Visual Probe Follow-up

## Landing State

- Branch: `runner-capture-research`
- Follow-up branch: `windows-visual-rca`
- Latest inspected hosted-run commit: `c744e2a`
  (`ci: preapprove iTerm2 inline display`)
- Remote push: `github windows-visual-rca` pushed through `c744e2a`
- GitHub workflow: `Hosted visual probes`
- Latest run inspected: `28985469159`
- Run URL: `https://github.com/zw3rk/timui.h/actions/runs/28985469159`

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
- Hosted macOS iTerm2 now has accepted timui OSC 1337 visual evidence. Run
  `28985469159` produced `iterm2-window-12s.png`, visibly showing the
  `timui image smoke` UI in iTerm2 with `active: iterm2 (forced)` and both
  PNG-backed image tiles rendered. The raw RGBA tile remains `[img]`, which is
  expected for this iTerm2 path.
- The Windows POSIX image-smoke build now succeeds under MSYS2 after passing the
  setup action's `msys2-location` output into the script and overriding
  `POSIX_CFLAGS` for MSYS C99 feature visibility.
- The workflow remains best-effort/manual. Sanity screenshots prove capture
  mechanics only; protocol screenshots still need manual inspection.

## Rejected / Inconclusive State

- Earlier hosted iTerm2 OS-level image evidence remains rejected. Those older
  full-screen `screencapture` artifacts showed macOS TCC prompts or first-launch
  prompts instead of the timui iTerm2 smoke.
- macOS direct full-screen capture can still trigger a real user-consent prompt
  for direct screen/audio capture. The accepted path is the targeted iTerm2
  window capture from run `28985469159`, not the full-screen diagnostic when it
  contains a TCC overlay.
- The iTerm2 Python API connection path is useful as a text predicate, but not
  as screenshot evidence on current hosted images. Run `28985469159` connected
  to iTerm2, `iterm2-api-session.json` recorded `"screen_text_matched": true`,
  and the upstream API overlay exposed `Session.async_screenshot()`, but
  Homebrew iTerm2 3.6.11 rejected that RPC as too old for Python API session
  screenshots. Use `iterm2-window-*.png` for accepted native visual evidence
  unless the hosted cask gains screenshot support.
- Run `28984631494` proved the upstream iTerm2 Python API overlay works, but
  iTerm2 did not reach the API server because the just-installed Homebrew cask
  hit macOS first-launch/Gatekeeper confirmation:
  `iterm2-screen-12s.png` and `iterm2-screen-24s.png` show the
  `"iTerm" is an app downloaded from the Internet` prompt. This was fixed by
  removing `com.apple.quarantine`, registering the app, installing PyObjC for
  the AppKit prelaunch path, and retaining a direct executable launch fallback.
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
  - `artifacts/gh-runs/28984631494/`
  - `artifacts/gh-runs/28985212786/`
  - `artifacts/gh-runs/28985469159/`
- `/artifacts/` is gitignored scratch.
- Run `28985469159` macOS iTerm2:
  - `image-smoke-build.status`: `0`
  - `iterm2-first-launch.status`: `0`
  - `iterm2-defaults-allow-inline-display.status`: `0`
  - `iterm2-open.status`: `0`
  - `iterm2-api-session.json`: `"screen_text_matched": true`
  - `iterm2-api-capture.status`: `1`, because iTerm2 3.6.11 is too old for the
    Python API screenshot RPC.
  - `iterm2-window-12s.png`: accepted native iTerm2 visual evidence; visible
    PNG-backed `plain png` and `png+rgba sidecar` tiles.
- Run `28985212786` macOS iTerm2:
  - First-launch Gatekeeper prompt was cleared and iTerm2 launched.
  - `iterm2-window-12s.png` showed the next blocker:
    `Allow Terminal-Initiated Display?`.
  - This was fixed by pre-seeding `NoSyncSuppressDownloadConfirmation` and its
    saved selection before iTerm2 launch.
- Run `28984631494` macOS iTerm2:
  - `iterm2-api-overlay.status`: `0`
  - `iterm2-open.status`: `124`
  - `iterm2-api-capture.status`: `1`
  - `iterm2-api-prelaunch.txt`: `ModuleNotFoundError("No module named 'AppKit'")`
  - `iterm2-screen-12s.png`: visible first-launch/Gatekeeper prompt for iTerm,
    not accepted protocol evidence.
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
  `28982372688`, `28982641529`, `28984631494`, `28985212786`, `28985469159`

## Next Safe Move

- For future iTerm2 evidence: prefer `iterm2-window-12s.png` or
  `iterm2-region-12s.png` from `Hosted visual probes`. The accepted baseline is
  run `28985469159` at `c744e2a`. Treat `iterm2-api-session.json` as a useful
  text predicate, but not screenshot evidence while Homebrew iTerm2 3.6.11
  reports the screenshot RPC unsupported.
- For ConPTY: debug `tools/conpty_smoke_win32.c` on an interactive Windows host
  with live byte-stream logging before treating hosted ConPTY as an acceptance
  gate.
