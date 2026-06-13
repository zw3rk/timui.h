/* ---- Windows ConPTY backend (v0.2 stub) ------------------------------- *
 * On Windows, ConPTY (CreatePseudoConsole) provides the equivalent of POSIX
 * forkpty. This is a stub returning TIMUI_ERR_UNSUPPORTED on all platforms;
 * the real implementation (#ifdef _WIN32, kernel32 CreatePseudoConsole +
 overlapped I/O) is deferred until a Windows build target is available. */
TIMUI_API TimuiResult timui_conpty_open(TimuiTransport *out_transport, int *out_pid){
    (void)out_transport;
    (void)out_pid;
    return TIMUI_ERR_UNSUPPORTED;
}
TIMUI_API void timui_conpty_close(TimuiTransport *transport, int pid){
    (void)transport;
    (void)pid;
}
