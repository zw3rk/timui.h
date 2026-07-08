---
type: Report
title: Website product brief handoff
date: 2026-07-07
---

## Landing state

Branch `product-brief`, worktree
`/Users/angerman/Projects/zw3rk/timui.h-product-brief`, not pushed.

Primary artifact: `docs/website-product-brief.md`.

## Read first

- `docs/website-product-brief.md` - website positioning, copy bank, proof
  points, demo plan, and limitations.
- `README.md` - current public overview.
- `USAGE.md` - demo/media commands for the current bundled vt_gif font and
  emoji pipeline.
- `include/timui.h` - canonical public API and caveats.
- `Makefile` - demo, media, test, release, and visual-rendering targets.

## Accepted state

- `nix develop -c make test` passed on 2026-07-07 with
  `all tests passed (196)`.
- The product brief was written after direct review of the header, source,
  docs, examples, tests, Makefile, and CI.
- Two read-only subagents completed: examples/product review and
  build/test/release/docs maturity review.
- A third core-API subagent errored during compaction; its work was not used as
  accepted evidence.

## Caveats

- Do not claim Windows support. ConPTY is documented as unsupported.
- Do not claim full Unicode bidi/grapheme support. The chat demo has a useful
  scoped RTL approximation and CJK/emoji rendering support.
- Do not claim images work everywhere. Terminal Kitty graphics support is
  required for real inline images; the library provides fallback placeholders.
- Do not claim a website pipeline already exists. This branch only adds the
  product brief.

## Verification already run

```sh
nix develop -c make
nix develop -c make test
```

`make test` result: `all tests passed (196)`.

## Next safe move

Generate website media with `nix develop -c make webp-chat-demo`, then build a
first static site around real generated terminal assets, not placeholder
marketing art. Before public launch, update stale README/USAGE/backlog copy
called out in `docs/website-product-brief.md`.
