/* Barrique: a window plus a thread that never calls the wineserver (it sleeps in a
 * loop), so the process outlives its wineserver, like a program whose threads wait
 * in msync. Used to check what happens when the server goes away and Cocoa then
 * calls into Wine on the main thread (app activation): the process must end without
 * a macOS crash report.
 *   winhold.exe [seconds]      default 60
 */
#include <windows.h>

static DWORD WINAPI sleeper( void *arg )
{
    for (;;) Sleep( 100 );
    return 0;
}

int main( int argc, char **argv )
{
    DWORD end = GetTickCount() + (argc > 1 ? atoi( argv[1] ) : 60) * 1000;
    HWND hwnd = CreateWindowA( "STATIC", "winhold", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 300, 200,
                               NULL, NULL, NULL, NULL );
    MSG msg;

    CreateThread( NULL, 0, sleeper, NULL, 0, NULL );
    while (GetTickCount() < end)
    {
        while (PeekMessageA( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
        MsgWaitForMultipleObjects( 0, NULL, FALSE, 100, QS_ALLINPUT );
    }
    DestroyWindow( hwnd );
    return 0;
}
