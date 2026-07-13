---
type: Report
title: Windows ConPTY Live Smoke RCA
date: 2026-07-13
---

# Windows ConPTY Live Smoke RCA

## Landing State

- Branch: `conpty-evidence-manifest-2026-07-13`
- Base commit: `ae58a8aea26fd1cde394642a6cdb3d842b666aed`
  (`docs: stamp conpty rca handoff`)
- RCA commit: `8098b11a58e6a5538ecc8590d0e748ab49517aec`
- Evidence manifest commit: `4105026d23c3a5e705334b7e5af30a9815ca3eb3`
- Evidence verifier commit: `72d6abd82fdc313badd9008ed84fba29fde0fe6b`
- Merge/push status: not merged, not pushed at the time this handoff was
  written.
- Downloaded hosted artifact: `artifacts/gh-runs/28982641529/` (gitignored
  scratch, fetched with `gh run download 28982641529 --repo zw3rk/timui.h
  --name hosted-visual-windows-terminal`).

## Read First

- `tools/conpty_smoke_win32.c`
- `docs/runbooks/phase1-5-live-evidence.md`
- `docs/research/conpty/REVIEW.md`
- `docs/handoff/2026-07-09-hosted-visual-probe.md`

## Accepted State

- Windows ConPTY backend implementation remains compile-covered only. Do not
  claim supported Windows operation yet.
- Hosted run `28982641529` is useful RCA evidence, not accepted ConPTY
  evidence. `conpty-smoke.stdout` contains the initial `cmd.exe` banner and
  prompt, while `conpty-smoke.stderr` says:
  `conpty smoke: sentinel not observed; captured 166 bytes`.
- That artifact proves the smoke reached a live child shell and read initial
  ConPTY output. It does not prove that input written to the ConPTY was consumed
  by the shell.

## Change Made

- `tools/conpty_smoke_win32.c` now writes only `echo TIMUI_CONPTY_SMOKE` first,
  using LF newlines to match Microsoft's ConPTY input example. It writes `exit`
  only after the sentinel read attempt.
- If LF does not produce the sentinel, the smoke closes that session and retries
  the previous CRLF form in a fresh ConPTY session.
- Short writes are treated as failures. Failure output now includes attempts,
  last write count, and a bounded escaped excerpt of captured bytes.
- `make check-conpty-smoke-tool` compiles a portable helper test for the smoke
  script selection, exact-write helper, and sentinel matching. Both
  `make check` and `make check-conpty` run it.
- `tools/ci/hosted_visual_windows.ps1` now emits
  `conpty-acceptance.json`, `conpty-smoke.command.txt`, and
  `conpty-smoke.meta.txt` so hosted Windows runs have a machine-readable
  acceptance predicate in addition to stdout/stderr.
- `make verify-conpty-evidence ARTIFACT_DIR=... COMMIT=...` validates a
  downloaded hosted Windows artifact against that predicate. The fixture test
  rejects missing/malformed manifests, wrong commits, false acceptance flags,
  nonzero status files, missing PASS tokens, evidence.md mismatch, and unsafe
  manifest paths. It now also rejects compile-only commands and artifacts that
  do not record Windows host metadata.

## Verification Already Run

- Red: `nix develop -c make check-conpty-smoke-tool` failed before
  `smoke_script_for_attempt` existed.
- Red: `nix develop -c make check-conpty-smoke-tool` failed again after the
  test was tightened to require split echo/exit scripts and exact-write
  behavior.
- Green: `nix develop -c make check-conpty-smoke-tool`.
- Green: `nix develop -c make check-conpty-win32-smoke-compile`.
- Green: `nix develop -c make check-hosted-visual-windows`.
- Green: `nix develop -c make check-conpty-evidence-artifacts`.
- Green: `nix develop -c make check-conpty`.
- Green: `nix develop -c make man`.
- Green: `nix develop -c make check`.

## Blocker

- No accepted real Windows ConPTY smoke result has been captured for this
  branch. The next run must contain
  `PASS conpty smoke: observed TIMUI_CONPTY_SMOKE` in `conpty-smoke.stdout`
  from the same commit being accepted.

## Next Safe Move

- Push or otherwise run this branch on a Windows host only when the operator is
  ready to collect evidence. Trigger `Hosted visual probes` or run
  `nix develop -c make smoke-conpty-win32 CONPTY_WIN_CC=cc` on an interactive
  Windows setup. For hosted runs, run
  `nix develop -c make verify-conpty-evidence ARTIFACT_DIR=... COMMIT=...`
  first. If it fails, inspect `conpty-smoke.stdout`, `conpty-smoke.stderr`, and
  `conpty-smoke.status`; the escaped excerpt should show whether the shell saw
  neither command, only echo, only exit, or additional prompt/error output.
