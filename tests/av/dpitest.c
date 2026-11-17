/*
 * Barrique phase 5 step 1b: how programs that are not DPI-aware look under
 * Retina (winemac RetinaMode=y, LogPixels 192).
 *
 * One window per DPI awareness context (unaware, system aware, per-monitor
 * v2), created on the same thread with SetThreadDpiAwarenessContext. Each
 * draws a 300x150 logical frame, a 10pt line of text in a font sized for its
 * own DPI, and a 1-pixel checker. The program prints what each window sees:
 *   dpi=<GetDpiForWindow> caps=<LOGPIXELSX> screen=<SM_CXSCREEN>x<SM_CYSCREEN>
 *   client=<w>x<h> phys=<w>x<h> text=<width of the line>
 * and keeps the windows on screen for <seconds> (default 6) so the host can
 * take a screenshot, then saves each window with PrintWindow to
 * <prefix>-<mode>.bmp when a prefix is given.
 *
 *   dpitest.exe [seconds] [bmp prefix]
 */
#define _WIN32_WINNT 0x0a00
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const struct mode
{
    const char *name;
    DPI_AWARENESS_CONTEXT context;
}
modes[] =
{
    { "unaware", DPI_AWARENESS_CONTEXT_UNAWARE },
    { "system", DPI_AWARENESS_CONTEXT_SYSTEM_AWARE },
    { "permonitorv2", DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 },
};

static const WCHAR text[] = L"The quick brown fox 0123 \x6f22\x5b57 \x3042";

static HFONT font_for(HWND hwnd)
{
    LOGFONTW lf = {0};
    lf.lfHeight = -MulDiv(10, GetDpiForWindow(hwnd), 72);
    lf.lfQuality = CLEARTYPE_QUALITY;
    lstrcpyW(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

static int text_width(HWND hwnd)
{
    HDC dc = GetDC(hwnd);
    HFONT font = font_for(hwnd), old = SelectObject(dc, font);
    SIZE size = {0};
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
    SelectObject(dc, old);
    DeleteObject(font);
    ReleaseDC(hwnd, dc);
    return size.cx;
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        UINT dpi = GetDpiForWindow(hwnd);
        int s = MulDiv(1, dpi, 96), x, y;
        HFONT font = font_for(hwnd), old;
        WCHAR title[64];
        RECT r;

        GetClientRect(hwnd, &r);
        FillRect(dc, &r, GetStockObject(WHITE_BRUSH));
        /* 300x150 at 96 DPI, scaled for the window's own DPI */
        r.left = 10 * s; r.top = 10 * s;
        r.right = r.left + 300 * dpi / 96; r.bottom = r.top + 150 * dpi / 96;
        FrameRect(dc, &r, GetStockObject(BLACK_BRUSH));
        old = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        GetWindowTextW(hwnd, title, ARRAY_SIZE(title));
        TextOutW(dc, r.left + 8 * s, r.top + 8 * s, title, lstrlenW(title));
        TextOutW(dc, r.left + 8 * s, r.top + 40 * s, text, lstrlenW(text));
        SelectObject(dc, old);
        DeleteObject(font);
        /* 1-pixel checker: blurred if the window is scaled */
        for (y = 0; y < 32; y++)
            for (x = 0; x < 64; x++)
                if ((x + y) & 1) SetPixel(dc, r.left + 8 * s + x, r.top + 80 * s + y, RGB(0, 0, 0));
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_TIMER)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void save_bmp(HWND hwnd, const char *path)
{
    RECT r;
    HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
    BITMAPINFO bi = {{sizeof(bi.bmiHeader)}};
    BITMAPFILEHEADER fh = {0};
    void *bits;
    HBITMAP bmp, old;
    FILE *f;
    int w, h;

    GetWindowRect(hwnd, &r);
    w = r.right - r.left; h = r.bottom - r.top;
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    old = SelectObject(dc, bmp);
    PrintWindow(hwnd, dc, 0);
    if ((f = fopen(path, "wb")))
    {
        bi.bmiHeader.biHeight = h;   /* bottom-up for viewers: flip rows */
        fh.bfType = 0x4d42;
        fh.bfOffBits = sizeof(fh) + sizeof(bi.bmiHeader);
        fh.bfSize = fh.bfOffBits + w * h * 4;
        fwrite(&fh, sizeof(fh), 1, f);
        fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, f);
        for (int y = h - 1; y >= 0; y--) fwrite((char *)bits + y * w * 4, w * 4, 1, f);
        fclose(f);
    }
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
}

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 6;
    const char *prefix = argc > 2 ? argv[2] : NULL;
    WNDCLASSW wc = {0};
    HWND hwnd[ARRAY_SIZE(modes)];
    MSG msg;
    int i;

    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    wc.lpszClassName = L"dpitest";
    RegisterClassW(&wc);

    printf("process awareness %d\n", (int)GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext()));
    for (i = 0; i < ARRAY_SIZE(modes); i++)
    {
        DPI_AWARENESS_CONTEXT prev = SetThreadDpiAwarenessContext(modes[i].context);
        WCHAR title[64];
        UINT dpi;
        RECT r = {0, 0, 340, 200};

        swprintf(title, ARRAY_SIZE(title), L"%hs", modes[i].name);
        /* 340x200 client at 96 DPI; the aware ones scale it themselves */
        dpi = GetDpiForSystem();
        if (i) { r.right = r.right * dpi / 96; r.bottom = r.bottom * dpi / 96; }
        AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, i ? dpi : 96);
        hwnd[i] = CreateWindowW(L"dpitest", title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
        SetThreadDpiAwarenessContext(prev);
    }
    /* place them side by side in physical pixels */
    for (i = 0; i < ARRAY_SIZE(modes); i++)
    {
        DPI_AWARENESS_CONTEXT prev = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        RECT r;
        GetWindowRect(hwnd[i], &r);
        SetWindowPos(hwnd[i], NULL, 40 + i * 760, 80, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SetThreadDpiAwarenessContext(prev);
        ShowWindow(hwnd[i], SW_SHOWNORMAL);
        UpdateWindow(hwnd[i]);
    }
    for (i = 0; i < ARRAY_SIZE(modes); i++)
    {
        DPI_AWARENESS_CONTEXT prev = SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(hwnd[i]));
        RECT client, phys;
        HDC dc = GetDC(hwnd[i]);

        GetClientRect(hwnd[i], &client);
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        GetClientRect(hwnd[i], &phys);
        SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(hwnd[i]));
        printf("%s dpi=%u caps=%d screen=%dx%d client=%ldx%ld phys=%ldx%ld text=%d\n", modes[i].name,
               GetDpiForWindow(hwnd[i]), GetDeviceCaps(dc, LOGPIXELSX),
               GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
               client.right, client.bottom, phys.right, phys.bottom, text_width(hwnd[i]));
        ReleaseDC(hwnd[i], dc);
        SetThreadDpiAwarenessContext(prev);
    }
    fflush(stdout);

    SetTimer(hwnd[0], 1, seconds * 1000, NULL);
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (prefix)
        for (i = 0; i < ARRAY_SIZE(modes); i++)
        {
            char path[MAX_PATH];
            DPI_AWARENESS_CONTEXT prev = SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(hwnd[i]));
            snprintf(path, sizeof(path), "%s-%s.bmp", prefix, modes[i].name);
            save_bmp(hwnd[i], path);
            SetThreadDpiAwarenessContext(prev);
        }
    printf("RESULT done\n");
    return 0;
}
