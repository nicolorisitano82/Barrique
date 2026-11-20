/* Barrique: mouse input the way games use it. Opens a window and, depending on the
 * mode, clips the cursor to it, hides it and puts it back in the middle after every
 * move (mouse look in first-person games), and counts what arrives:
 *   raw     WM_INPUT mouse deltas (RegisterRawInputDevices)
 *   move    WM_MOUSEMOVE distance from the centre (or from the last position)
 * Once a second it prints the totals, the cursor position and whether the cursor is
 * inside the clip rectangle, on stdout and in <exe>.txt (started from the Finder a
 * program has no console). Move the mouse (or post moves from the host) while it runs.
 *
 *   mouselook.exe [mode] [seconds]
 *     look    clip + hide + recentre (default)
 *     clip    clip only
 *     free    nothing, raw input only
 *   (or MOUSELOOK_MODE=<mode> in the environment)
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

static const char *mode = "look";
static HWND hwnd;
static POINT centre;
static LONG raw_dx, raw_dy, raw_count, move_dx, move_dy, move_count, recentre_count;
static POINT last_pos;
static BOOL have_last;
static FILE *log_file;

static void out( const char *fmt, ... )
{
    va_list args;
    va_start( args, fmt );
    vprintf( fmt, args );
    va_end( args );
    if (!log_file) return;
    va_start( args, fmt );
    vfprintf( log_file, fmt, args );
    va_end( args );
    fflush( log_file );
}

static void recentre(void)
{
    RECT rc;
    GetClientRect( hwnd, &rc );
    centre.x = (rc.left + rc.right) / 2;
    centre.y = (rc.top + rc.bottom) / 2;
    ClientToScreen( hwnd, &centre );
    if (!strcmp( mode, "look" )) SetCursorPos( centre.x, centre.y );
}

static void clip(void)
{
    RECT rc;
    if (!strcmp( mode, "free" )) return;
    GetClientRect( hwnd, &rc );
    MapWindowPoints( hwnd, NULL, (POINT *)&rc, 2 );
    ClipCursor( &rc );
}

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_INPUT:
    {
        RAWINPUT ri;
        UINT size = sizeof(ri);
        if (GetRawInputData( (HRAWINPUT)lp, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER) ) != (UINT)-1 &&
            ri.header.dwType == RIM_TYPEMOUSE)
        {
            if (ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) out( "raw: absolute event\n" );
            raw_dx += ri.data.mouse.lLastX;
            raw_dy += ri.data.mouse.lLastY;
            raw_count++;
        }
        break;
    }
    case WM_MOUSEMOVE:
    {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        ClientToScreen( hwnd, &pt );
        if (!strcmp( mode, "look" ))
        {
            if (pt.x != centre.x || pt.y != centre.y)
            {
                move_dx += pt.x - centre.x;
                move_dy += pt.y - centre.y;
                move_count++;
                recentre_count++;
                SetCursorPos( centre.x, centre.y );
            }
        }
        else
        {
            if (have_last) { move_dx += pt.x - last_pos.x; move_dy += pt.y - last_pos.y; }
            last_pos = pt;
            have_last = TRUE;
            move_count++;
        }
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) { clip(); recentre(); }
        break;
    case WM_DESTROY:
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcA( hwnd, msg, wp, lp );
}

int main( int argc, char **argv )
{
    WNDCLASSA wc = {0};
    RAWINPUTDEVICE rid = { 1, 2, 0, NULL };   /* generic desktop, mouse */
    DWORD start, last_report;
    int seconds = 20, tick = 0;
    MSG msg;

    if (getenv( "MOUSELOOK_MODE" )) mode = getenv( "MOUSELOOK_MODE" );   /* started from the Finder */
    if (argc > 1) mode = argv[1];
    if (argc > 2) seconds = atoi( argv[2] );
    setvbuf( stdout, NULL, _IONBF, 0 );
    {
        char path[MAX_PATH + 4];
        GetModuleFileNameA( NULL, path, MAX_PATH );
        strcat( path, ".txt" );
        log_file = fopen( path, "w" );
    }

    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA( NULL );
    wc.hCursor = LoadCursorA( NULL, (LPCSTR)IDC_ARROW );
    wc.lpszClassName = "mouselook";
    RegisterClassA( &wc );
    hwnd = CreateWindowA( "mouselook", "mouselook", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          200, 150, 800, 600, NULL, NULL, wc.hInstance, NULL );
    SetForegroundWindow( hwnd );
    rid.hwndTarget = hwnd;
    if (!RegisterRawInputDevices( &rid, 1, sizeof(rid) )) out( "RegisterRawInputDevices failed %lu\n", GetLastError() );
    if (!strcmp( mode, "look" )) ShowCursor( FALSE );
    clip();
    recentre();
    out( "mouselook: mode %s, centre %ld,%ld\n", mode, centre.x, centre.y );

    start = last_report = GetTickCount();
    while (GetTickCount() - start < seconds * 1000u)
    {
        MsgWaitForMultipleObjects( 0, NULL, FALSE, 50, QS_ALLINPUT );
        while (PeekMessageA( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            TranslateMessage( &msg );
            DispatchMessageA( &msg );
        }
        /* like games: put the clip back if it went away (Wine, like Windows, drops it
           when the foreground changes) */
        if (strcmp( mode, "free" ) && GetForegroundWindow() == hwnd)
        {
            RECT want, now;
            GetClientRect( hwnd, &want );
            MapWindowPoints( hwnd, NULL, (POINT *)&want, 2 );
            GetClipCursor( &now );
            if (!EqualRect( &want, &now )) { clip(); recentre(); }
        }
        if (GetTickCount() - last_report >= 1000)
        {
            POINT pos;
            RECT cliprc;
            CURSORINFO ci = { sizeof(ci) };
            GetCursorPos( &pos );
            GetClipCursor( &cliprc );
            GetCursorInfo( &ci );
            out( "t=%d raw %ld,%ld (%ld ev) move %ld,%ld (%ld ev) pos %ld,%ld %s cursor %s fg %s\n",
                    ++tick, raw_dx, raw_dy, raw_count, move_dx, move_dy, move_count, pos.x, pos.y,
                    PtInRect( &cliprc, pos ) ? "in-clip" : "OUT-OF-CLIP",
                    ci.flags & CURSOR_SHOWING ? "shown" : "hidden",
                    GetForegroundWindow() == hwnd ? "yes" : "no" );
            last_report = GetTickCount();
        }
    }
    ClipCursor( NULL );
    out( "mouselook: total raw %ld,%ld move %ld,%ld recentred %ld\n", raw_dx, raw_dy, move_dx, move_dy, recentre_count );
    DestroyWindow( hwnd );
    return 0;
}
