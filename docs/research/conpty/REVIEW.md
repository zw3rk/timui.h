---
type: Report
title: Windows ConPTY Phase 1.5 Review
date: 2026-07-08
---

# Windows ConPTY Phase 1.5 Review

## Sources

- Microsoft Learn, Creating a Pseudoconsole session:
  <https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session>
- Microsoft Learn, CreatePseudoConsole:
  <https://learn.microsoft.com/en-us/windows/console/createpseudoconsole>
- Microsoft Learn, ClosePseudoConsole:
  <https://learn.microsoft.com/en-us/windows/console/closepseudoconsole>
- Microsoft Learn, ResizePseudoConsole:
  <https://learn.microsoft.com/en-us/windows/console/resizepseudoconsole>
- Microsoft Learn, CreatePipe:
  <https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-createpipe>
- Microsoft Learn, InitializeProcThreadAttributeList:
  <https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-initializeprocthreadattributelist>
- Microsoft Learn, UpdateProcThreadAttribute:
  <https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute>
- Microsoft Learn, STARTUPINFOEX:
  <https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-startupinfoexw>
- Microsoft Learn, CreateProcessW:
  <https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw>
- Microsoft Learn, GetModuleHandleW, LoadLibraryW, and GetProcAddress:
  <https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandlew>
  <https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryw>
  <https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getprocaddress>

## Decision

Implement ConPTY as a backend transport, not as a graphics abstraction. The
public `timui_conpty_open` surface has no command argument, so Phase 1.5 opens
the user's default shell (`COMSPEC`, falling back to `cmd.exe`) and returns a
`TimuiTransport` over the ConPTY pipes. A future pre-1.0 API can add an explicit
command/argv form if applications need to launch a specific child.

The backend follows the Microsoft pseudoconsole setup pattern: create two
anonymous pipes, pass the ConPTY-owned ends to `CreatePseudoConsole`, attach the
`HPCON` to a `STARTUPINFOEXW` attribute list with
`PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE`, then launch the shell with
`EXTENDED_STARTUPINFO_PRESENT`.

ConPTY symbols are resolved dynamically from `kernel32.dll`. If the host
Windows build does not expose `CreatePseudoConsole`, `ResizePseudoConsole`, and
`ClosePseudoConsole`, `timui_conpty_open` returns `TIMUI_ERR_UNSUPPORTED`
instead of creating a binary that fails to load.

## Implementation Notes

- `HPCON` is closed with `ClosePseudoConsole`, not `CloseHandle`.
- Parent pipe handles kept by the transport are the write side of ConPTY input
  and the read side of ConPTY output.
- `conpty_read` uses `PeekNamedPipe` before `ReadFile` so the frame loop does
  not block indefinitely when no child output is available.
- `conpty_write` chunks `size_t` payloads into bounded `DWORD` writes and also
  caps the total per transport call to the `int` return type.
- `timui_conpty_resize` validates dimensions within the positive `COORD` range
  before calling `ResizePseudoConsole`.
- Close is idempotent: close the local pipe handles, close the pseudoconsole,
  wait briefly for the child, terminate only if still active, close process
  handles, free context, and clear the transport.
- The ConPTY transport does not mutate host console VT modes. It owns pipes and
  the child process; a future Windows terminal backend can own real console mode
  management without stale restores between multiple transports.

## Evidence And Limits

- POSIX fallback/helper tests run in the main unit suite.
- `make check-conpty-win32-compile` cross-compiles an isolated ConPTY TU with
  MinGW when the dev shell provides `x86_64-w64-mingw32-gcc`; the target is part
  of `make check`.
- This is compile evidence, not live Windows evidence. Do not claim supported
  Windows operation until a real Windows Terminal smoke run is captured.
