/* ---- Windows ConPTY backend (v0.2) ------------------------------------ *
 * On Windows, ConPTY (CreatePseudoConsole) provides a VT byte-stream transport
 * to a child console application. Non-Windows builds keep returning
 * TIMUI_ERR_UNSUPPORTED. */

#define TIMUI_CONPTY_COORD_MAX 32767
#define TIMUI_CONPTY_IO_CHUNK_MAX ((size_t)(1024u * 1024u))

TIMUI_API size_t timui_conpty_io_chunk_for_test(size_t remaining){
    size_t max = TIMUI_CONPTY_IO_CHUNK_MAX;
    if(max > (size_t)INT_MAX) max = (size_t)INT_MAX;
    if(remaining < max) return remaining;
    return max;
}

TIMUI_API int timui_conpty_size_valid_for_test(int cols, int rows){
    return cols > 0 && rows > 0 &&
           cols <= TIMUI_CONPTY_COORD_MAX && rows <= TIMUI_CONPTY_COORD_MAX;
}

static void conpty_zero_transport_(TimuiTransport *t){
    if(!t) return;
    t->write = NULL;
    t->read = NULL;
    t->flush = NULL;
    t->close = NULL;
    t->ctx = NULL;
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

typedef HRESULT (WINAPI *TimuiCreatePseudoConsoleFn)(COORD, HANDLE, HANDLE, DWORD, HPCON *);
typedef HRESULT (WINAPI *TimuiResizePseudoConsoleFn)(HPCON, COORD);
typedef VOID    (WINAPI *TimuiClosePseudoConsoleFn)(HPCON);

typedef struct {
    TimuiCreatePseudoConsoleFn create_pseudo_console;
    TimuiResizePseudoConsoleFn resize_pseudo_console;
    TimuiClosePseudoConsoleFn  close_pseudo_console;
} TimuiConptyApi;

typedef struct {
    HPCON  hpc;
    HANDLE hPipeIn;       /* host writes here -> ConPTY input */
    HANDLE hPipeOut;      /* host reads here  <- ConPTY output */
    HANDLE hProcess;
    HANDLE hThread;
    DWORD  pid;
    TimuiResizePseudoConsoleFn resize_pseudo_console;
    TimuiClosePseudoConsoleFn  close_pseudo_console;
} TimuiConptyCtx;

static int conpty_load_api_(TimuiConptyApi *api){
    HMODULE kernel32;
    union { FARPROC p; TimuiCreatePseudoConsoleFn f; } create_pc;
    union { FARPROC p; TimuiResizePseudoConsoleFn f; } resize_pc;
    union { FARPROC p; TimuiClosePseudoConsoleFn f; } close_pc;
    if(!api) return 0;
    memset(api, 0, sizeof *api);
    kernel32 = GetModuleHandleW(L"kernel32.dll");
    if(!kernel32) kernel32 = LoadLibraryW(L"kernel32.dll");
    if(!kernel32) return 0;
    create_pc.p = GetProcAddress(kernel32, "CreatePseudoConsole");
    resize_pc.p = GetProcAddress(kernel32, "ResizePseudoConsole");
    close_pc.p = GetProcAddress(kernel32, "ClosePseudoConsole");
    api->create_pseudo_console = create_pc.f;
    api->resize_pseudo_console = resize_pc.f;
    api->close_pseudo_console = close_pc.f;
    return api->create_pseudo_console &&
           api->resize_pseudo_console &&
           api->close_pseudo_console;
}

static void conpty_close_handle_(HANDLE *h){
    if(h && *h && *h != INVALID_HANDLE_VALUE){
        CloseHandle(*h);
        *h = NULL;
    }
}

static void conpty_cleanup_ctx_(TimuiConptyCtx *c){
    DWORD exit_code = 0;
    if(!c) return;
    conpty_close_handle_(&c->hPipeIn);
    conpty_close_handle_(&c->hPipeOut);
    if(c->hpc){
        c->close_pseudo_console(c->hpc);
        c->hpc = NULL;
    }
    if(c->hProcess){
        if(WaitForSingleObject(c->hProcess, 1000) == WAIT_TIMEOUT &&
           GetExitCodeProcess(c->hProcess, &exit_code) && exit_code == STILL_ACTIVE){
            (void)TerminateProcess(c->hProcess, 1);
            (void)WaitForSingleObject(c->hProcess, 1000);
        }
    }
    conpty_close_handle_(&c->hThread);
    conpty_close_handle_(&c->hProcess);
    free(c);
}

static int conpty_write(TimuiTransport *t, const void *d, size_t n){
    TimuiConptyCtx *c = t ? (TimuiConptyCtx *)t->ctx : NULL;
    const unsigned char *p = (const unsigned char *)d;
    size_t limit, off = 0;
    if(!c || !c->hPipeIn || (!d && n > 0)) return -1;
    limit = n < (size_t)INT_MAX ? n : (size_t)INT_MAX;
    while(off < limit){
        size_t chunk = timui_conpty_io_chunk_for_test(limit - off);
        DWORD written = 0;
        if(chunk == 0) break;
        if(!WriteFile(c->hPipeIn, p + off, (DWORD)chunk, &written, NULL))
            break;
        if(written == 0) break;
        off += (size_t)written;
    }
    return (off == 0 && n > 0) ? -1 : (int)off;
}

static int conpty_read(TimuiTransport *t, void *b, size_t cap){
    TimuiConptyCtx *c = t ? (TimuiConptyCtx *)t->ctx : NULL;
    DWORD avail = 0, got = 0;
    size_t chunk;
    if(!c || !c->hPipeOut || !b || cap == 0) return 0;
    if(!PeekNamedPipe(c->hPipeOut, NULL, 0, NULL, &avail, NULL)) return -1;
    if(avail == 0) return 0;
    chunk = timui_conpty_io_chunk_for_test(cap);
    if(chunk > (size_t)avail) chunk = (size_t)avail;
    if(chunk == 0) return 0;
    if(!ReadFile(c->hPipeOut, b, (DWORD)chunk, &got, NULL)) return -1;
    return (int)got;
}

static int conpty_flush(TimuiTransport *t){ (void)t; return 0; }
static void conpty_close_transport(TimuiTransport *t){
    if(!t || !t->ctx) return;
    conpty_cleanup_ctx_((TimuiConptyCtx *)t->ctx);
    conpty_zero_transport_(t);
}

static void conpty_default_command_(wchar_t *buf, DWORD cap){
    DWORD n;
    static const wchar_t fallback[] = L"cmd.exe";
    if(!buf || cap == 0) return;
    buf[0] = 0;
    n = GetEnvironmentVariableW(L"COMSPEC", buf, cap);
    if(n == 0 || n >= cap){
        DWORD i;
        for(i = 0; i + 1 < cap && fallback[i]; i++) buf[i] = fallback[i];
        buf[i] = 0;
    }
}

TIMUI_API TimuiResult timui_conpty_open(TimuiTransport *out_transport, int *out_pid){
    TimuiConptyCtx *ctx = NULL;
    HANDLE in_read = NULL, in_write = NULL, out_read = NULL, out_write = NULL;
    SIZE_T attr_bytes = 0;
    STARTUPINFOEXW si;
    PROCESS_INFORMATION pi;
    COORD size;
    wchar_t cmd[32768];
    TimuiConptyApi api;
    TimuiResult result = TIMUI_ERR_OS;
    int attr_ready = 0;

    if(!out_transport || !out_pid) return TIMUI_ERR_INVALID_ARGUMENT;
    conpty_zero_transport_(out_transport);
    *out_pid = -1;
    if(!conpty_load_api_(&api)) return TIMUI_ERR_UNSUPPORTED;

    ctx = (TimuiConptyCtx *)calloc(1, sizeof *ctx);
    if(!ctx) return TIMUI_ERR_OUT_OF_MEMORY;
    ctx->resize_pseudo_console = api.resize_pseudo_console;
    ctx->close_pseudo_console = api.close_pseudo_console;
    if(!CreatePipe(&in_read, &in_write, NULL, 0)) goto fail;
    if(!CreatePipe(&out_read, &out_write, NULL, 0)) goto fail;

    size.X = 80;
    size.Y = 24;
    if(FAILED(api.create_pseudo_console(size, in_read, out_write, 0, &ctx->hpc))) goto fail;

    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = NULL;
    si.StartupInfo.hStdOutput = NULL;
    si.StartupInfo.hStdError = NULL;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_bytes);
    if(attr_bytes == 0) goto fail;
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)calloc(1, attr_bytes);
    if(!si.lpAttributeList){ result = TIMUI_ERR_OUT_OF_MEMORY; goto fail; }
    if(!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_bytes)) goto fail_attr;
    attr_ready = 1;
    if(!UpdateProcThreadAttribute(si.lpAttributeList, 0,
                                  PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                  ctx->hpc, sizeof ctx->hpc, NULL, NULL)) goto fail_attr;

    conpty_default_command_(cmd, (DWORD)(sizeof cmd / sizeof cmd[0]));
    if(!cmd[0]) goto fail_attr;
    if(!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT,
                       NULL, NULL, &si.StartupInfo, &pi)) goto fail_attr;
    conpty_close_handle_(&in_read);
    conpty_close_handle_(&out_write);
    DeleteProcThreadAttributeList(si.lpAttributeList);
    free(si.lpAttributeList);
    si.lpAttributeList = NULL;

    if(pi.dwProcessId > (DWORD)INT_MAX){
        ctx->hProcess = pi.hProcess;
        ctx->hThread = pi.hThread;
        goto fail;
    }
    ctx->hPipeIn = in_write;
    ctx->hPipeOut = out_read;
    in_write = NULL;
    out_read = NULL;
    ctx->hProcess = pi.hProcess;
    ctx->hThread = pi.hThread;
    ctx->pid = pi.dwProcessId;

    out_transport->write = conpty_write;
    out_transport->read = conpty_read;
    out_transport->flush = conpty_flush;
    out_transport->close = conpty_close_transport;
    out_transport->ctx = ctx;
    *out_pid = (int)ctx->pid;
    conpty_close_handle_(&in_read);
    conpty_close_handle_(&out_write);
    return TIMUI_OK;

fail_attr:
    if(attr_ready)
        DeleteProcThreadAttributeList(si.lpAttributeList);
    if(si.lpAttributeList)
        free(si.lpAttributeList);
fail:
    conpty_close_handle_(&in_read);
    conpty_close_handle_(&in_write);
    conpty_close_handle_(&out_read);
    conpty_close_handle_(&out_write);
    conpty_cleanup_ctx_(ctx);
    conpty_zero_transport_(out_transport);
    *out_pid = -1;
    return result;
}

TIMUI_API TimuiResult timui_conpty_resize(TimuiTransport *transport, int cols, int rows){
    TimuiConptyCtx *c;
    COORD size;
    if(!transport || !timui_conpty_size_valid_for_test(cols, rows))
        return TIMUI_ERR_INVALID_ARGUMENT;
    c = (TimuiConptyCtx *)transport->ctx;
    if(!c || !c->hpc || !c->resize_pseudo_console) return TIMUI_ERR_INVALID_ARGUMENT;
    size.X = (SHORT)cols;
    size.Y = (SHORT)rows;
    return SUCCEEDED(c->resize_pseudo_console(c->hpc, size)) ? TIMUI_OK : TIMUI_ERR_OS;
}

TIMUI_API void timui_conpty_close(TimuiTransport *transport, int pid){
    (void)pid;
    conpty_close_transport(transport);
}
#else
TIMUI_API TimuiResult timui_conpty_open(TimuiTransport *out_transport, int *out_pid){
    if(!out_transport || !out_pid) return TIMUI_ERR_INVALID_ARGUMENT;
    conpty_zero_transport_(out_transport);
    *out_pid = -1;
    return TIMUI_ERR_UNSUPPORTED;
}
TIMUI_API TimuiResult timui_conpty_resize(TimuiTransport *transport, int cols, int rows){
    if(!transport || !timui_conpty_size_valid_for_test(cols, rows))
        return TIMUI_ERR_INVALID_ARGUMENT;
    return TIMUI_ERR_UNSUPPORTED;
}
TIMUI_API void timui_conpty_close(TimuiTransport *transport, int pid){
    (void)pid;
    conpty_zero_transport_(transport);
}
#endif
