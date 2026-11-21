/* Minimal WebView2 host without WebView2Loader: loads the runtime's
 * EmbeddedBrowserWebView.dll directly, creates an environment and a
 * controller, navigates to a string and runs a script. Prints each step.
 *   wv2test.exe [runtime dir] [seconds]   (default: newest installed runtime, 60 s)
 */
#define COBJMACROS
#include <windows.h>
#include <stdio.h>

static DWORD t0;
static HWND hwnd;
static FILE *logf;
static void say(const char *fmt, ...)
{
    va_list ap;
    char buf[512];
    va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    printf("[%6lu ms] %s\n", GetTickCount() - t0, buf); fflush(stdout);
    if (logf) { fprintf(logf, "[%6lu ms] %s\n", GetTickCount() - t0, buf); fflush(logf); }
}

/* a generic callback object: Invoke(this, HRESULT, void *arg) */
typedef struct cb cb;
typedef HRESULT (*cb_fn)(cb *self, HRESULT hr, void *arg, void *arg2);
struct cb_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(cb *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(cb *);
    ULONG (STDMETHODCALLTYPE *Release)(cb *);
    HRESULT (STDMETHODCALLTYPE *Invoke)(cb *, void *, void *);
};
struct cb { const struct cb_vtbl *vtbl; LONG ref; cb_fn fn; };
static HRESULT STDMETHODCALLTYPE cb_qi(cb *This, REFIID riid, void **out) { *out = This; This->ref++; return S_OK; }
static ULONG STDMETHODCALLTYPE cb_addref(cb *This) { return ++This->ref; }
static ULONG STDMETHODCALLTYPE cb_release(cb *This) { return --This->ref; }
static HRESULT STDMETHODCALLTYPE cb_invoke(cb *This, void *a, void *b) { return This->fn(This, (HRESULT)(LONG_PTR)a, a, b); }
static const struct cb_vtbl cb_vtbl = { cb_qi, cb_addref, cb_release, cb_invoke };

typedef HRESULT (STDMETHODCALLTYPE *vfn)(void *, ...);
#define VCALL(obj, idx, ...) (((vfn *)*(void **)(obj))[idx])((obj), ##__VA_ARGS__)

static void *controller, *webview;
typedef HRESULT (STDMETHODCALLTYPE *put_bounds_fn)(void *, RECT);
static HRESULT put_bounds(void *c, RECT rc) { return ((put_bounds_fn *)*(void **)c)[6](c, rc); }
static int done;

static HRESULT script_done(cb *self, HRESULT hr, void *a, void *b)
{
    say("ExecuteScript done hr %#lx result %ls", (ULONG)(LONG_PTR)a, b ? (WCHAR *)b : L"(null)");
    done = 1;
    return S_OK;
}
static cb script_cb = { &cb_vtbl, 1, script_done };

static HRESULT nav_completed(cb *self, HRESULT hr, void *sender, void *args)
{
    BOOL ok = FALSE;
    VCALL(args, 3, &ok); /* get_IsSuccess */
    say("NavigationCompleted success %d", ok);
    say("ExecuteScript hr %#lx", VCALL(webview, 29, L"document.title + ' ' + (1+1)", &script_cb));
    return S_OK;
}
static cb nav_cb = { &cb_vtbl, 1, nav_completed };

static HRESULT process_failed(cb *self, HRESULT hr, void *sender, void *args)
{
    int kind = -1;
    VCALL(args, 3, &kind);
    say("ProcessFailed kind %d", kind);
    return S_OK;
}
static cb pf_cb = { &cb_vtbl, 1, process_failed };

static HRESULT controller_created(cb *self, HRESULT hr, void *a, void *b)
{
    RECT rc;
    INT64 tok;
    say("controller created hr %#lx controller %p", (ULONG)(LONG_PTR)a, b);
    if (!b) { done = 1; return S_OK; }
    controller = b; IUnknown_AddRef((IUnknown *)controller);
    GetClientRect(hwnd, &rc);
    say("put_Bounds hr %#lx", put_bounds(controller, rc));
    say("put_IsVisible hr %#lx", VCALL(controller, 4, TRUE));
    say("get_CoreWebView2 hr %#lx", VCALL(controller, 25, &webview));
    say("add_NavigationCompleted hr %#lx", VCALL(webview, 15, &nav_cb, &tok));
    say("add_ProcessFailed hr %#lx", VCALL(webview, 25, &pf_cb, &tok));
    say("NavigateToString hr %#lx", VCALL(webview, 6,
            L"<html><head><title>wv2test</title></head><body style='background:#c33;color:white;font:40px sans-serif'>WebView2 OK</body></html>"));
    return S_OK;
}
static cb controller_cb = { &cb_vtbl, 1, controller_created };

static HRESULT env_created(cb *self, HRESULT hr, void *a, void *b)
{
    say("environment created hr %#lx env %p", (ULONG)(LONG_PTR)a, b);
    if (!b) { done = 1; return S_OK; }
    say("CreateCoreWebView2Controller hr %#lx", VCALL(b, 3, hwnd, &controller_cb));
    return S_OK;
}
static cb env_cb = { &cb_vtbl, 1, env_created };

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_SIZE && controller)
    {
        RECT rc; GetClientRect(h, &rc); put_bounds(controller, rc);
    }
    if (msg == WM_DESTROY) { done = 1; return 0; }
    return DefWindowProcW(h, msg, wp, lp);
}

typedef HRESULT (STDMETHODCALLTYPE *create_env_fn)(BOOL, int, const WCHAR *, IUnknown *, void *);

int main(int argc, char **argv)
{
    WCHAR dll[MAX_PATH], dir[MAX_PATH] = L"C:\\Program Files (x86)\\Microsoft\\EdgeWebView\\Application\\";
    WIN32_FIND_DATAW fd;
    HANDLE find;
    HMODULE mod;
    create_env_fn create;
    int seconds = argc > 2 ? atoi(argv[2]) : 60;
    const WCHAR *arch;
    WNDCLASSW wc = {0};
    MSG msg;
    HRESULT hr;

    t0 = GetTickCount();
    logf = fopen("C:\\wv2test.log", "w");
#if defined(__aarch64__) || defined(__arm64ec__)
    arch = L"arm64";
#elif defined(__x86_64__)
    arch = L"x64";
#else
    arch = L"x86";
#endif
    if (argc > 1) MultiByteToWideChar(CP_ACP, 0, argv[1], -1, dir, MAX_PATH);
    else
    {
        WCHAR pat[MAX_PATH], best[64] = L"";
        swprintf(pat, MAX_PATH, L"%s*.*.*.*", dir);
        if ((find = FindFirstFileW(pat, &fd)) != INVALID_HANDLE_VALUE)
        {
            do if (fd.cFileName[0] >= L'0' && fd.cFileName[0] <= L'9' && wcscmp(fd.cFileName, best) > 0) wcscpy(best, fd.cFileName); while (FindNextFileW(find, &fd));
            FindClose(find);
        }
        wcscat(dir, best);
    }
    swprintf(dll, MAX_PATH, L"%s\\EBWebView\\%s\\EmbeddedBrowserWebView.dll", dir, arch);
    say("loading %ls", dll);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (!(mod = LoadLibraryW(dll))) { say("LoadLibrary failed %lu", GetLastError()); return 1; }
    if (!(create = (create_env_fn)GetProcAddress(mod, "CreateWebViewEnvironmentWithOptionsInternal")))
    { say("no CreateWebViewEnvironmentWithOptionsInternal"); return 1; }

    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"wv2test";
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    RegisterClassW(&wc);
    hwnd = CreateWindowW(L"wv2test", L"wv2test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            100, 100, 800, 600, NULL, NULL, wc.hInstance, NULL);
    say("window %p", hwnd);

    hr = create(TRUE, 0, L"C:\\wv2test-data", NULL, &env_cb);
    say("CreateWebViewEnvironmentWithOptionsInternal hr %#lx", hr);

    while (!done && GetTickCount() - t0 < seconds * 1000u)
    {
        if (MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLINPUT) == WAIT_OBJECT_0)
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    say(done ? "finished" : "timeout"); if (getenv("WV2_HOLD")) { DWORD end = GetTickCount() + atoi(getenv("WV2_HOLD")) * 1000; while (GetTickCount() < end) { if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg); else Sleep(10); } }
    if (done) { DWORD end = GetTickCount() + 3000; while (GetTickCount() < end) { if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg); else Sleep(10); } }
    return 0;
}
