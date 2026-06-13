/* ---- Windows ConPTY backend (v0.2) ------------------------------------ *
 * On Windows, ConPTY (CreatePseudoConsole) provides the equivalent of POSIX
 * forkpty. This is a minimal #ifdef _WIN32 skeleton; the real implementation
 * wraps CreatePseudoConsole + overlapped I/O in a TimuiTransport. On non-
 * Windows platforms it returns TIMUI_ERR_UNSUPPORTED. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct { HANDLE hPC; HANDLE hPipeIn; HANDLE hPipeOut; } ConptyCtx;

static int conpty_write(TimuiTransport *t, const void *d, size_t n){
    ConptyCtx *c = (ConptyCtx *)t->ctx;
    DWORD written = 0;
    WriteFile(c->hPipeIn, d, (DWORD)n, &written, NULL);
    return (int)written;
}
static int conpty_read(TimuiTransport *t, void *b, size_t cap){
    ConptyCtx *c = (ConptyCtx *)t->ctx;
    DWORD got = 0;
    ReadFile(c->hPipeOut, b, (DWORD)cap, &got, NULL);
    return (int)got;
}
static int conpty_flush(TimuiTransport *t){ (void)t; return 0; }
static void conpty_close(TimuiTransport *t){ (void)t; }

TIMUI_API TimuiResult timui_conpty_open(TimuiTransport *out_transport, int *out_pid){
    /* TODO: CreatePipe x2, CreatePseudoConsole, CreateProcess, wrap handles.
     * The skeleton above provides the transport vtable; the full impl needs
     * STARTUPINFO + STARTUPINFOEX + PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE. */
    (void)out_transport; (void)out_pid;
    return TIMUI_ERR_UNSUPPORTED;
}
TIMUI_API void timui_conpty_close(TimuiTransport *transport, int pid){
    /* TODO: ClosePseudoConsole, CloseHandle, TerminateProcess. */
    (void)transport; (void)pid;
}
#else
TIMUI_API TimuiResult timui_conpty_open(TimuiTransport *out_transport, int *out_pid){
    (void)out_transport; (void)out_pid;
    return TIMUI_ERR_UNSUPPORTED;
}
TIMUI_API void timui_conpty_close(TimuiTransport *transport, int pid){
    (void)transport; (void)pid;
}
#endif
