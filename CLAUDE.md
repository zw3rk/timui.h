# CLAUDE.md — timui.h

Project-specific working agreements. These sit on top of the global
`~/.claude/CLAUDE.md` (bash, code style, workflow, makefile, testing,
copyright/licensing, version control) and add the deeper engineering
discipline below. Where the two overlap, the stricter/more specific rule wins.

> **Entry point:** the self-documenting `Makefile` is the sole interface to the
> project — run `make` for the menu, and do all work through the nix dev shell:
> `nix develop -c make <target>`. Never invoke the language toolchain directly
> outside `nix develop`. (Per global rules: if no Makefile/flake exists yet,
> create them rather than bypassing the convention.)

---

## TDD loop — non-negotiable

1. **Red** — write a failing test that demands the behaviour.
2. **Green** — minimal code to pass.
3. **Refactor** — clean up under green.

No production line is written before a failing test exists that requires it.
Rhythm: keep the watch target (`make test-watch` or the project equivalent)
running while you implement.

## Test-before-implementation gate

Before any new component, write **both**:

- **Positive** tests — happy path, spec/conformant vectors.
- **Negative** tests — malformed input, truncated/corrupt frames, wrong-version
  negotiation, out-of-range values, adversarial input.

Only then implement. Prefer dedicated test source sets (unit / property /
integration) and keep them paired with the code they cover; surface gaps in
lint.

## Regression policy

Every bug ⇒ a failing test in a `regression/` namespace, named for the issue,
**before** the fix, with the issue linked. The failing-test commit precedes the
fix commit (visible in `git log`). The regression stays green forever after.

---

## Synthesize acceptance predicates — don't default to wall-clock

When a gate is framed as a multi-day or operational **live run** ("follow ≥N
boundaries, 0 false-rejects"), **synthesize its acceptance predicate offline
first**. Canonical / settled history can be replayed through the **production**
code path at disk speed over far more boundaries — yielding a reusable,
CI-able artifact instead of a one-off operational result. **Inject the
adversarial conditions the live path would produce** (duplicates, out-of-order
re-delivery, rollbacks, …) and assert the same predicate. Reserve a live run
only for the genuinely non-synthesizable residue (real-time plumbing). A
synthesized soak is a first-class gate; an unbounded wall-clock run is not.

## Synthetic ground truth — don't default to an external oracle

The same instinct applies to **correctness tests**, not just soaks: before
standing up an external oracle (a reference binary, a public API, a live
cross-check) to validate a computation, ask whether a **synthetic scenario where
we already know the answer** is good enough. Construct controlled inputs whose
expected output is **hand-computable** (or trivially derivable), drive them
through the **production** code path, and assert equality. It is deterministic,
CI-able, has no network/infra dependency, isolates the unit under test, and lets
you **inject the exact edge cases** a real-world divergence would come from — so
a bug surfaces as a local assertion failure, not a 0.01% delta you then have to
bisect. Reserve the external oracle for the genuinely non-synthesizable residue
(real byte formats, real consensus/history, a whole-system equivalence gate).

## "No upstream source" is usually a search-depth artifact

When you conclude a value can't be sourced authoritatively — and you're tempted
to keep hand-rolled or third-party data — that conclusion is almost always
**premature**. Before settling, exhaust three avenues:

1. **git HISTORY** of the source repos — the deployed value often lived in the
   config/data files at a *past* commit/tag even after `master` moved on.
2. **Alternative authoritative repos**, not just the obvious one — specs,
   playgrounds, and per-variant data files frequently hold what the primary
   repo dropped.
3. **The primary-vs-downstream chain** — "X-only" usually means "I didn't check
   what X is derived from"; a downstream re-server is not the origin.

Do not declare data unavailable until history + alternative repos + the
provenance chain are exhausted.

---

## Crypto policy — LOCKED

We **never implement a cryptographic primitive**. Any crypto (hashing,
signatures, VRF, KES, pairings, …) comes **only from vetted, audited
libraries**. A composition *over* a vetted primitive (e.g. a documented
multi-round/MMM scheme over a library's core sign/verify) is acceptable and is
*not* a primitive; a hand-rolled field tower, pairing, or hash is not. Any
change touching crypto must cite the spec + reference implementation in the
project's research docs.

## Commits

- **Commit proactively, *along the way* — do not wait to be asked.** When a unit
  of work is complete and verified (a doc, a fix, a green refactor), commit it
  before moving on. Leaving finished work uncommitted is the anti-pattern. Only
  **pushing** and **merging to `master`** stay gated on an explicit request.
- Frequent, single-purpose, meaningful — **and concise**. Multiple small commits
  beat one sprawling one; split by concern (e.g. feature docs vs a
  workflow/policy change go in separate commits).
- Subject: imperative, ≤72 chars, scoped — e.g. `cbor: decode indefinite arrays`.
- Body: the *why* + spec/reference. Keep it tight; no filler.
- **Banned:** `Co-authored-by`, `Signed-off-by`, "Generated by/with", any AI/tool
  trailer. Never `--force` push.

## Handoff documents

Create or update a handoff document whenever a workstream is high-context, spans
sessions, lands with known blockers, depends on operator evidence, or leaves a
clear next phase for another agent. Put handoffs under `docs/handoff/YYYY-MM-DD-<topic>.md`
and link to them from the final report, active plan, or runbook future agents
are likely to open first.

A handoff is a navigation and pickup note, not a new capability claim. Keep it
short and artifact-led:

- Front matter: `type: Report`, a concrete `title`, the exact `date`.
- Landing state: branch, run id, commit hash, merge/push status, master/branch
  parity if relevant.
- Read-first links: the reports, runbooks, artifacts, dashboards, and
  reconciliations that rehydrate the context.
- Accepted state: only artifact-backed facts that are safe to rely on.
- Blockers and pickup points: exact missing evidence, failing predicates,
  commands to resume, and what *not* to claim.
- Verification already run: commands and outcomes, including partial or
  substituted gates.
- Next safe move: the smallest non-speculative continuation step.

Use concrete dates, commit hashes, artifact paths, ids, and command lines. Mark
WARN/best-effort, blocked, inferred, diagnostic, and historical states
explicitly so a later agent cannot accidentally promote them into accepted
evidence.

## Integrating to master (worktree checkpoints)

**All work happens in a feature worktree — never directly on `master`.**
`master` is an *integration target only*: it holds the fast-forwarded master,
not work-in-progress. Start any task with its own worktree + branch:
`git worktree add ../<proj>-<topic> -b <topic> master`.

Keep `master` continuously current: at every milestone/checkpoint, integrate a
feature worktree into `master` by **rebase + fast-forward** — never a merge
commit, so master's history stays linear. Procedure:

1. Commit your work; the worktree must be clean.
2. Back up the tip: `git branch backup/<slug>` (a recovery point if the rebase
   goes wrong).
3. `git rebase master`. On conflicts that are not trivially mechanical, **stop
   and ask** — never guess a resolution.
4. **Re-verify after the rebase** — it replays onto master's newer code, so a
   clean *textual* rebase can still break *semantically*: re-run the touched
   module's checks.
5. Fast-forward master *in its own worktree* (`git -C <master-worktree> merge
   --ff-only <branch>`). `--ff-only` rewrites only the files your commits change,
   so unrelated uncommitted work there is left untouched; it blocks only on a
   real overlap with the files you're integrating.
6. Do **not** push unless asked (the remote is outward-facing); never `--force`.

## Tech-debt ritual

Run a periodic tech-debt sweep: dead-code removal, TODO/FIXME triage, dependency
audit, coverage gaps, file-size review (split files approaching ~1000 lines),
and a re-read of past decisions/ADRs. Record the new baseline commit count after
each sweep so the cadence is measurable, not aspirational.

## Living documentation & multi-agent research

All research, spec digests, and decisions live in the project's `docs/`
directory (research, decisions/ADRs, runbooks, reports). Non-trivial research
(a protocol, an algorithm, a subsystem's rules) gets ≥2 independent passes,
reconciled in a `docs/research/<topic>/REVIEW.md`, before coding. Code comments
link to `docs/`; `docs/` links to source + the upstream spec. Keep `docs/`'s
index current.

## Idiomatic code — functional style, study, don't transliterate

This project is written in **idiomatic code for its primary language — not** a
line-by-line port of any reference implementation. Where a spec or reference
node/binary exists, mirror its **byte formats and protocol semantics** exactly,
but its **code structure is not our template**: never transliterate another
language file-for-file or function-for-function, and don't drag another
language's idioms (typeclass dictionaries, monad stacks, manual tagged unions,
…). Reach for what the platform gives you. Defaults:

- **Immutable data first.** Prefer value types produced by `withX`/copy-style
  helpers over in-place mutation.
- **Sum/product types done the language-native way**, with exhaustive
  pattern-matching where available — never `instanceof`/tag-`int` chains.
- **Avoid mutable global/static state** unless there is a documented performance
  reason, and then confine and document it.
- **Avoid `null`/sentinel values.** Use the language's optional/absent type; accept
  nullables only at external boundaries (decoding, I/O) and convert immediately.
- **Side-effect-free methods where possible** — compute and return a value rather
  than mutating a parameter or internal field.
- **Thread safety through immutability** — prefer immutable data shared freely
  across threads over locks. When mutable state is unavoidable (caches, key
  evolution, mempools), confine it behind a well-documented synchronization
  contract and keep the mutable surface as small as possible.

Studying references closely is encouraged; copying their *structure* is not. If
a change starts to look like a 1:1 translation, stop and redesign it the native
way.

---

## Definition of Done (per change)

red→green→refactor · positive + negative tests · regression test if a bugfix ·
Apache-2.0 header · no banned trailers · `docs/` updated if behaviour/spec
changed · the project's verify/lint gate green.
