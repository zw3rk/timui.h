# Hosted runner terminal capture review

type: Report
title: Hosted runner terminal capture review
date: 2026-07-09

## Question

Can GitHub-hosted macOS and Windows runners produce screenshots or recordings
usable as timui.h terminal visual evidence?

## Sources

- GitHub hosted runner model:
  <https://docs.github.com/en/actions/concepts/runners/github-hosted-runners>
- GitHub runner images:
  <https://github.com/actions/runner-images>
- macOS 15 image manifest:
  <https://raw.githubusercontent.com/actions/runner-images/main/images/macos/macos-15-Readme.md>
- macOS runner TCC setup:
  <https://raw.githubusercontent.com/actions/runner-images/main/images/macos/scripts/build/configure-tccdb-macos.sh>
- macOS hosted screenshot/TCC regressions:
  <https://github.com/actions/runner-images/issues/8951>
- Apple Terminal automation:
  <https://support.apple.com/guide/terminal/automate-tasks-using-applescript-and-terminal-trml1003/mac>
- Apple PPPC / ScreenCapture privacy controls:
  <https://support.apple.com/guide/deployment/privacy-preferences-policy-control-payload-dep38df53c2a/web>
- iTerm2 Python API:
  <https://iterm2.com/python-api/>
- iTerm2 Python API security / no-auth marker:
  <https://iterm2.com/python-api-auth.html>
- iTerm2 `Session.async_screenshot` source:
  <https://github.com/gnachman/iTerm2/blob/master/api/library/python/iterm2/iterm2/session.py>
- Windows Terminal command line:
  <https://learn.microsoft.com/en-us/windows/terminal/command-line-arguments>
- Windows self-hosted GUI runner caveat:
  <https://github.com/orgs/community/discussions/67003>
- Desktop screenshot action, useful as an existence proof for hosted capture:
  <https://github.com/marketplace/actions/create-a-desktop-screenshot>

## Findings

- Hosted runners are ephemeral VMs. GitHub documents the VM model and points to
  runner-image manifests, but does not make a product-level promise that native
  GUI screenshots are stable acceptance evidence.
- Hosted macOS is the strongest candidate for best-effort GUI capture. The
  runner images pre-seed Screen Recording/TCC grants for shell/runner paths and
  Terminal.app. Prefer opening an executable `.command` file with
  `open -a Terminal`, then capture with `screencapture`.
- Hosted iTerm2 OS-level capture is weaker than Terminal.app. iTerm2 is not
  part of the standard macOS image, and a just-installed copy does not inherit
  Terminal.app's seeded TCC grants. On `macos-15`, direct `screencapture`
  attempts produced a privacy prompt attributed to `bash` requesting direct
  screen/audio access. Apple PPPC/TCC does not provide a supported silent allow
  path for this direct-capture case on GitHub-hosted macOS.
- iTerm2's Python API is a separate promising path. Current upstream source
  exposes `Session.async_screenshot()`, which asks iTerm2 for a PNG of the
  session's visible screen rather than asking macOS for global screen capture.
  PyPI `iterm2` 2.20 does not yet expose this method, so the hosted probe
  installs PyPI for dependencies and overlays the sparse upstream Python package
  directory, recording the upstream commit in `iterm2-api-source-commit.txt`.
  The API is disabled by default and external scripts normally need
  authentication; the documented root-owned
  `~/Library/Application Support/iTerm2/disable-automation-auth` marker lets CI
  avoid an AppleScript auth prompt after `EnableAPIServer` is set. Treat this as
  accepted iTerm2 visual evidence only after the PNG artifact is manually
  inspected and visibly shows the `image_smoke` iTerm2 inline-image tiles.
- Run `28984631494` narrowed the next iTerm2 blocker to first launch, not
  desktop availability: the runner had a logged-in GUI session and
  `screencapture` produced desktop PNGs, but the visible iTerm2 artifact was the
  macOS `"downloaded from the Internet"` confirmation for the Homebrew cask.
  The next probe removes `com.apple.quarantine`, registers the bundle with
  LaunchServices, installs PyObjC for AppKit launch diagnostics, and falls back
  to direct executable launch before retrying the iTerm2 API screenshot.
- Hosted Windows can run screen-capture code and can render Windows Terminal UI
  in the active `runneradmin` console session. Run `28982641529` proved
  Windows Terminal Sixel rendering on hosted Windows Server 2025:
  `windows-terminal-direct-sixel-12s.png` visibly rendered a 180x72 direct
  Sixel control, and `windows-terminal-sixel-12s.png` visibly rendered all
  three timui image-smoke Sixel tiles.
- The earlier hosted Windows Sixel failure was not a Windows Terminal renderer
  failure. RCA run `28982372688` showed the direct 180x72 Sixel control rendered
  correctly while timui emitted three `4x4` Sixel rasters. The root cause was
  the smoke harness's tiny 4x4 source fixture combined with MSYS/Windows
  Terminal reporting no cell pixel geometry; the fixed smoke harness now emits
  visible 64x24 source-pixel rasters when cell pixels are unavailable.
- Raw terminal streams, DCS/OSC marker counts, asciinema/ttyrec captures, and
  launch logs are diagnostics. Accepted evidence needs either deterministic
  replay/predicate checks or a manually inspected screenshot/video that visibly
  shows the claimed terminal result.

## Reconciled plan

- Keep `.github/workflows/hosted-visual-probe.yml` manual and best-effort.
- Add screenshot sanity artifacts before protocol claims:
  - macOS: `terminal-sanity.png` must show `TIMUI_HOSTED_SCREENSHOT_SANITY`.
  - Windows: `cmd-sanity.png` must show `TIMUI_HOSTED_SCREENSHOT_SANITY`.
- macOS protocol evidence:
  - use `macos-15`;
  - prefer Terminal.app for hosted OS-level GUI screenshots;
  - first try iTerm2's Python API `Session.async_screenshot()` path because it
    avoids macOS direct screen capture;
  - treat direct `screencapture` iTerm2 artifacts as diagnostics unless a future
    run shows no TCC prompt and visibly shows the live iTerm2 payload.
- Windows hosted evidence:
  - record `query user`, `qwinsta`, process lists, and full virtual-screen PNGs;
  - count Windows Terminal Sixel evidence when the screenshot visibly shows the
    live payload, the session diagnostics show an active console, and raw DCS
    metrics confirm non-trivial image rasters.
- Required CI acceptance should stay deterministic:
  - cell-buffer/golden tests;
  - libvterm replay where applicable;
  - parsed raw-byte predicates for image protocols;
  - decoded Sixel/PNG structural checks where possible;
  - real native terminal screenshots/videos only as manual release evidence.

## Open work

- Add protocol-level Sixel decode predicates so hosted Windows can still prove
  Sixel payload correctness without depending on visible desktop capture.
- Hosted iTerm2 direct OS capture remains weaker than Terminal.app and may
  still hit macOS privacy/TCC after first launch succeeds. The next safe
  experiment is the de-quarantined Python API session PNG path; if that fails,
  use Terminal.app hosted screenshots for capture mechanics and deterministic
  protocol-byte evidence, or use a persistent Mac with pre-approved permissions
  if native iTerm2 pixels become a release requirement.
