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
- Hosted iTerm2 is weaker than Terminal.app. iTerm2 is not part of the standard
  macOS image, and a just-installed copy does not inherit the same confidence as
  Terminal.app. Count iTerm2 only when the screenshot visibly shows the live
  timui iTerm2 payload.
- Hosted Windows can run screen-capture code, but it is not a reliable required
  acceptance gate for visible desktop UI. Service/session isolation, absent or
  non-foreground windows, black desktops, and Windows Terminal packaging all
  remain likely failure modes.
- Windows Terminal Sixel visual proof is better handled on a self-hosted Windows
  runner launched in an autologon interactive session. Hosted Windows remains
  useful for deterministic ConPTY/protocol-byte tests and diagnostic artifacts.
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
  - record TCC/WindowServer diagnostics;
  - count only an `iterm2-screen-*.png` that visibly shows the iTerm2 live smoke.
- Windows hosted evidence:
  - record `query user`, `qwinsta`, process lists, and full virtual-screen PNGs;
  - treat Windows Terminal screenshots as supplemental diagnostics unless they
    visibly show the live payload and the session diagnostics support an
    interactive desktop.
- Required CI acceptance should stay deterministic:
  - cell-buffer/golden tests;
  - libvterm replay where applicable;
  - parsed raw-byte predicates for image protocols;
  - decoded Sixel/PNG structural checks where possible;
  - real native terminal screenshots/videos only as manual release evidence.

## Open work

- Add a self-hosted Windows visual runbook if Windows Terminal pixel evidence
  becomes a release blocker.
- Add protocol-level Sixel decode predicates so hosted Windows can still prove
  Sixel payload correctness without depending on visible desktop capture.
