# Threading — timui.h

`timui.h` is **single-writer**: exactly one thread — the UI thread — renders and
owns `Timui`/`TimuiFrame`. Background work posts messages in; the UI thread owns
the terminal.

## Thread-safety contract

| Function class | Safety |
|---|---|
| `timui_post` (the MPSC queue) | **thread-safe** — any thread |
| `timui_*` widget / drawing / style / frame APIs | **UI-thread only** |
| `timui_begin`, `timui_end`, `timui_poll_event`, `timui_recv` | UI-thread only |
| `timui_close` | UI-thread only |

Hard rule: **only the UI thread writes to the terminal.**

## Worker model

```
worker thread(s)
    │  timui_post(ui, type, data, size)   // copies bytes into the MPSC queue
    ▼
thread-safe message queue
    │  (drained on the UI thread)
    ▼
UI thread: update model → render frame → terminal write
```

Workers never touch `TimuiFrame` or the terminal; they communicate by posting
messages, which the UI thread applies to its model between frames.

## Shutdown ordering (W14)

**All producers MUST be joined (or otherwise guaranteed stopped) before
`timui_close`.** `timui_close` destroys the MPSC queue a worker posts to; a
`timui_post` that races with `close` (a worker between the `if(!q)` check and
the lock) locks/frees a destroyed mutex — UB. The UI thread owns the lifecycle:
signal workers to stop, join them, then `timui_close`. `examples/async_scan.c`
follows this (`pthread_join` before `timui_close`).

## `TIMUI_NO_THREADS`

Define `TIMUI_NO_THREADS` for a single-threaded build: the MPSC queue drops its
pthread mutex (unlocked), and the program links without `-pthread`. The API is
unchanged; `timui_post` is simply no longer safe to call concurrently.

## Verification

The MPSC queue is exercised under ThreadSanitizer (a 4-producer × 100-message
stress, TSAN-clean) — see `tests/test_mpsc.c`.
