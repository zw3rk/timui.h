# Threading — timui.h

`timui.h` is **single-writer**: exactly one thread — the UI thread — renders and
owns `Timui`/`TimuiFrame`. Background work posts messages in; the UI thread owns
the terminal.

## Thread-safety contract

| Function class | Safety |
|---|---|
| `timui_post`, `timui_post_result` (the MPSC queue) | **thread-safe** — any thread |
| `timui_*` widget / drawing / style / frame APIs | **UI-thread only** |
| `timui_begin`, `timui_begin_result`, `timui_end`, `timui_poll_event`, `timui_recv`, `timui_emit` | UI-thread only |
| `timui_close` | UI-thread only |

Hard rule: **only the UI thread writes to the terminal.**

## Worker model

```
worker thread(s)
    │  timui_post_result(ui, type, data, size)   // copies bytes into the MPSC queue
    ▼
thread-safe message queue
    │  (drained on the UI thread)
    ▼
UI thread: update model → render frame → terminal write
```

Workers never touch `TimuiFrame` or the terminal; they communicate by posting
messages, which the UI thread applies to its model between frames.

## Message ownership and ordering

`timui_post_result` copies `size` bytes before returning, so the caller may
reuse or free the source buffer immediately. `size == 0` may use `data == NULL`;
non-zero messages with `data == NULL` return `TIMUI_ERR_INVALID_ARGUMENT`.

The queue is FIFO for successful posts. With multiple producers, "first" means
the order in which producers acquire the queue mutex. The queue has no fixed
message-count cap; each message allocates one node. Overflow-sized messages and
invalid arguments fail before allocation, allocation failure returns
`TIMUI_ERR_OUT_OF_MEMORY`, and the source-compatible `timui_post` returns
`false` for any non-OK result.

`timui_emit_result` is the UI-thread/frame-scoped equivalent for code that wants
to enqueue a message while rendering a frame. It uses the same queue and payload
contract but is not a worker-thread escape hatch.

`timui_begin_result` does not block indefinitely. On a real POSIX terminal it
polls input briefly before reading to avoid a hot spin; no available bytes still
returns a renderable frame. EOF on the POSIX input fd, or `-2` from a custom
transport, returns `TIMUI_ERR_EOF`; runtime read failures return
`TIMUI_ERR_IO`; `timui_quit` makes future begins return `TIMUI_ERR_CLOSED`.
Posting does not currently wake a blocked poll; latency is bounded by the UI
loop and the 16 ms poll window.

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
