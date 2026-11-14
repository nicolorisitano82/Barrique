/*
 * FP/SIMD registers must survive syscalls and callbacks: keep double values
 * live across SwitchToThread() and SendMessage() to a window whose procedure
 * does FP math itself, and compare with the same computation without them.
 */
#include <windows.h>
#include <stdio.h>
#include <math.h>

static volatile double sink;

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_USER)
    {
        /* clobber FP registers inside the callback */
        double a = (double)wp, b = 0;
        int i;
        for (i = 0; i < 64; i++) b += sqrt( a + i ) * 1.5;
        sink = b;
        return (LRESULT)(b > 0);
    }
    return DefWindowProcA( hwnd, msg, wp, lp );
}

static double compute( HWND hwnd, int with_calls )
{
    double x = 1.0, y = 0.5, z = 0.25, w = 2.0;
    int i;
    for (i = 0; i < 20000; i++)
    {
        x = x * 1.000001 + y * 0.5;
        y = sqrt( x ) + z * 0.001;
        z = z * 0.999 + w * 1e-6;
        w = w + x * 1e-9 - y * 1e-10;
        if (with_calls)
        {
            if (i % 3 == 0) SwitchToThread();
            if (i % 5 == 0) SendMessageA( hwnd, WM_USER, i, 0 );
            if (i % 7 == 0) Sleep( 0 );
        }
    }
    return x + y * 3 + z * 7 + w * 11;
}

int main(void)
{
    WNDCLASSA cls = { 0 };
    HWND hwnd;
    double ref, got;

    cls.lpfnWndProc = wndproc;
    cls.hInstance = GetModuleHandleA( NULL );
    cls.lpszClassName = "fpsys";
    RegisterClassA( &cls );
    hwnd = CreateWindowA( "fpsys", "fpsys", 0, 0, 0, 10, 10, HWND_MESSAGE, NULL, cls.hInstance, NULL );
    if (!hwnd)
    {
        printf( "CreateWindow failed %lu\n", GetLastError() );
        return 1;
    }
    ref = compute( hwnd, 0 );
    got = compute( hwnd, 1 );
    printf( "reference %.17g\nwith calls %.17g\n", ref, got );
    printf( "RESULT: %s\n", ref == got ? "ok" : "FAILED" );
    DestroyWindow( hwnd );
    return ref != got;
}
