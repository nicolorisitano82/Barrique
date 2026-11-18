/* Barrique: game input check.
 *
 *   inputtest.exe check   non-interactive: raw input registration, ClipCursor
 *                         round trip, XInput and DirectInput availability, the
 *                         game controllers each API sees. Prints "input: ok"
 *                         or "input: FAILED".
 *   inputtest.exe         interactive window: click to capture the mouse the
 *                         way a game does (ClipCursor to the window, cursor
 *                         hidden, raw input), Esc to release. Shows the raw
 *                         mouse deltas (WM_INPUT), whether the cursor stays in
 *                         the clip rectangle, the last key, and the state of
 *                         up to four XInput pads, live.
 */
#define DIRECTINPUT_VERSION 0x0800
#define INITGUID
#include <windows.h>
#include <xinput.h>
#include <dinput.h>
#include <stdio.h>

static LONG64 raw_dx, raw_dy, raw_events, raw_hid_events, mouse_moves;
static int captured, escaped_clip;
static WCHAR last_key[64] = L"-";
static WCHAR controllers[1024];

static BOOL CALLBACK enum_cb( const DIDEVICEINSTANCEW *inst, void *ctx )
{
    WCHAR *list = ctx;
    if (wcslen( list ) < 900)
    {
        if (*list) wcscat( list, L", " );
        wcsncat( list, inst->tszProductName, 60 );
    }
    return DIENUM_CONTINUE;
}

static int directinput_controllers( WCHAR *list )
{
    IDirectInput8W *di;
    *list = 0;
    if (FAILED(DirectInput8Create( GetModuleHandleW( NULL ), DIRECTINPUT_VERSION, &IID_IDirectInput8W, (void **)&di, NULL )))
        return -1;
    IDirectInput8_EnumDevices( di, DI8DEVCLASS_GAMECTRL, enum_cb, list, DIEDFL_ATTACHEDONLY );
    IDirectInput8_Release( di );
    return 0;
}

static int xinput_pads(void)
{
    XINPUT_STATE state;
    int i, n = 0;
    for (i = 0; i < 4; i++) if (XInputGetState( i, &state ) == ERROR_SUCCESS) n++;
    return n;
}

static BOOL register_raw( HWND hwnd, DWORD flags )
{
    RAWINPUTDEVICE rid[3] = {
        { 0x01, 0x02, flags, hwnd },   /* mouse */
        { 0x01, 0x04, flags, hwnd },   /* joystick */
        { 0x01, 0x05, flags, hwnd },   /* gamepad */
    };
    return RegisterRawInputDevices( rid, 3, sizeof(rid[0]) );
}

static int check(void)
{
    WNDCLASSW wc = { 0 };
    HWND hwnd;
    RECT clip = { 100, 100, 300, 300 }, got;
    POINT pos;
    UINT count = 0;
    int bad = 0, pads, di;

    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = L"inputcheck";
    RegisterClassW( &wc );
    hwnd = CreateWindowW( L"inputcheck", L"inputcheck", WS_OVERLAPPEDWINDOW, 0, 0, 400, 400, 0, 0, 0, 0 );

    if (!register_raw( hwnd, 0 )) { printf( "RegisterRawInputDevices failed %lu\n", GetLastError() ); bad++; }
    else printf( "raw input: registered (mouse, joystick, gamepad)\n" );
    GetRawInputDeviceList( NULL, &count, sizeof(RAWINPUTDEVICELIST) );
    printf( "raw input devices: %u\n", count );

    if (!ClipCursor( &clip )) { printf( "ClipCursor failed %lu\n", GetLastError() ); bad++; }
    GetClipCursor( &got );
    printf( "ClipCursor: set (100,100)-(300,300), got (%ld,%ld)-(%ld,%ld)\n", got.left, got.top, got.right, got.bottom );
    if (memcmp( &got, &clip, sizeof(got) )) bad++;
    SetCursorPos( 10, 10 );
    GetCursorPos( &pos );
    printf( "SetCursorPos(10,10) while clipped: cursor at (%ld,%ld)\n", pos.x, pos.y );
    if (pos.x < clip.left || pos.y < clip.top) bad++;
    ClipCursor( NULL );

    pads = xinput_pads();
    printf( "XInput pads connected: %d\n", pads );
    di = directinput_controllers( controllers );
    if (di < 0) { printf( "DirectInput8Create failed\n" ); bad++; }
    else printf( "DirectInput game controllers: %ls\n", *controllers ? controllers : L"(none)" );

    DestroyWindow( hwnd );
    printf( "input: %s\n", bad ? "FAILED" : "ok" );
    return bad != 0;
}

static void capture( HWND hwnd, BOOL on )
{
    RECT rc;
    if (on)
    {
        GetClientRect( hwnd, &rc );
        MapWindowPoints( hwnd, NULL, (POINT *)&rc, 2 );
        ClipCursor( &rc );
        while (ShowCursor( FALSE ) >= 0) ;
        captured = 1;
    }
    else
    {
        ClipCursor( NULL );
        while (ShowCursor( TRUE ) < 0) ;
        captured = 0;
    }
}

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_INPUT:
    {
        RAWINPUT raw;
        UINT size = sizeof(raw);
        if (GetRawInputData( (HRAWINPUT)lp, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER) ) != (UINT)-1)
        {
            if (raw.header.dwType == RIM_TYPEMOUSE && !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
            {
                raw_dx += raw.data.mouse.lLastX;
                raw_dy += raw.data.mouse.lLastY;
                raw_events++;
            }
            else if (raw.header.dwType == RIM_TYPEHID) raw_hid_events++;
        }
        break;
    }
    case WM_MOUSEMOVE:
        mouse_moves++;
        if (captured)
        {
            POINT p;
            RECT rc;
            GetCursorPos( &p );
            GetClipCursor( &rc );
            if (!PtInRect( &rc, p )) escaped_clip++;
        }
        break;
    case WM_LBUTTONDOWN:
        if (!captured) capture( hwnd, TRUE );
        break;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && captured) capture( hwnd, FALSE );
        else GetKeyNameTextW( (LONG)lp, last_key, 64 );
        break;
    case WM_TIMER:
        InvalidateRect( hwnd, NULL, TRUE );
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint( hwnd, &ps );
        WCHAR line[256];
        XINPUT_STATE st;
        int i, y = 10;

#define LINE(...) do { swprintf( line, 256, __VA_ARGS__ ); TextOutW( hdc, 10, y, line, wcslen( line ) ); y += 20; } while (0)
        LINE( L"%ls", captured ? L"Mouse captured (Esc to release)" : L"Click to capture the mouse like a game" );
        LINE( L"Raw mouse: %lld events, total dx %lld, dy %lld", raw_events, raw_dx, raw_dy );
        LINE( L"WM_MOUSEMOVE: %lld, cursor outside the clip rectangle: %d times", mouse_moves, escaped_clip );
        LINE( L"Raw HID (joystick/gamepad) reports: %lld", raw_hid_events );
        LINE( L"Last key: %ls", last_key );
        for (i = 0; i < 4; i++)
        {
            if (XInputGetState( i, &st ) != ERROR_SUCCESS) { LINE( L"XInput pad %d: not connected", i ); continue; }
            LINE( L"XInput pad %d: buttons %04x  LX %6d LY %6d  RX %6d RY %6d  LT %3u RT %3u", i,
                 st.Gamepad.wButtons, st.Gamepad.sThumbLX, st.Gamepad.sThumbLY,
                 st.Gamepad.sThumbRX, st.Gamepad.sThumbRY, st.Gamepad.bLeftTrigger, st.Gamepad.bRightTrigger );
        }
        LINE( L"DirectInput controllers: %ls", *controllers ? controllers : L"(none)" );
#undef LINE
        EndPaint( hwnd, &ps );
        return 0;
    }
    case WM_DESTROY:
        capture( hwnd, FALSE );
        PostQuitMessage( 0 );
        break;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int main( int argc, char **argv )
{
    WNDCLASSW wc = { 0 };
    HWND hwnd;
    MSG msg;

    if (argc > 1 && !strcmp( argv[1], "check" )) return check();

    directinput_controllers( controllers );
    wc.lpfnWndProc = wndproc;
    wc.lpszClassName = L"inputtest";
    wc.hCursor = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW( &wc );
    hwnd = CreateWindowW( L"inputtest", L"Barrique input test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          CW_USEDEFAULT, CW_USEDEFAULT, 760, 320, 0, 0, 0, 0 );
    register_raw( hwnd, RIDEV_INPUTSINK );
    SetTimer( hwnd, 1, 50, NULL );
    while (GetMessageW( &msg, NULL, 0, 0 ))
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
    return 0;
}
