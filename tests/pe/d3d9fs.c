/* Barrique: D3D9 fullscreen at the desktop resolution (Retina: pixels), like Source
 * games. The frame is red with a 200-pixel square in each corner: top-left blue,
 * top-right green, bottom-left yellow, bottom-right white. A screenshot of the
 * display shows whether the whole back buffer reaches the screen at the right
 * scale (Portal drew its menu off screen, 2x too big).
 *
 *   d3d9fs.exe [seconds] [windowed [width height]]     default 6 seconds, fullscreen
 */
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_DESTROY) PostQuitMessage( 0 );
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static void fill( IDirect3DDevice9 *dev, int x, int y, int w, int h, D3DCOLOR color )
{
    D3DRECT r = { x, y, x + w, y + h };
    IDirect3DDevice9_Clear( dev, 1, &r, D3DCLEAR_TARGET, color, 1.0f, 0 );
}

int main( int argc, char **argv )
{
    int seconds = argc > 1 ? atoi( argv[1] ) : 6, windowed = argc > 2;
    WNDCLASSW wc = { 0, wndproc, 0, 0, GetModuleHandleW( NULL ), NULL, LoadCursorW( NULL, (LPCWSTR)IDC_ARROW ), NULL, NULL, L"d3d9fs" };
    D3DPRESENT_PARAMETERS pp = { 0 };
    IDirect3DDevice9 *dev;
    D3DDISPLAYMODE mode;
    IDirect3D9 *d3d;
    HRESULT hr;
    HWND hwnd;
    DWORD end;
    MSG msg;
    int w, h, s = 200;

    RegisterClassW( &wc );
    d3d = Direct3DCreate9( D3D_SDK_VERSION );
    IDirect3D9_GetAdapterDisplayMode( d3d, D3DADAPTER_DEFAULT, &mode );
    w = argc > 4 ? atoi( argv[3] ) : mode.Width;
    h = argc > 4 ? atoi( argv[4] ) : mode.Height;
    printf( "desktop %ux%u, screen metrics %dx%d\n", mode.Width, mode.Height,
            GetSystemMetrics( SM_CXSCREEN ), GetSystemMetrics( SM_CYSCREEN ) );

    {
        RECT rc = { 0, 0, w, h };
        DWORD style = windowed ? WS_OVERLAPPEDWINDOW : WS_POPUP;
        AdjustWindowRect( &rc, style, FALSE );
        hwnd = CreateWindowW( L"d3d9fs", L"d3d9fs", style, windowed ? 100 : 0, windowed ? 100 : 0,
                              rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, wc.hInstance, NULL );
    }
    ShowWindow( hwnd, SW_SHOW );

    pp.BackBufferWidth = w;
    pp.BackBufferHeight = h;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = windowed;
    pp.FullScreen_RefreshRateInHz = windowed ? 0 : mode.RefreshRate;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    hr = IDirect3D9_CreateDevice( d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                  D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev );
    printf( "CreateDevice %dx%d %s: %#lx\n", w, h, windowed ? "windowed" : "fullscreen", hr );
    if (FAILED(hr)) return 1;
    {
        RECT rc;
        GetClientRect( hwnd, &rc );
        printf( "client %ldx%ld\n", rc.right, rc.bottom );
    }

    end = GetTickCount() + seconds * 1000;
    while (GetTickCount() < end)
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
        IDirect3DDevice9_Clear( dev, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB( 200, 0, 0 ), 1.0f, 0 );
        fill( dev, 0, 0, s, s, D3DCOLOR_XRGB( 0, 0, 255 ) );
        fill( dev, w - s, 0, s, s, D3DCOLOR_XRGB( 0, 255, 0 ) );
        fill( dev, 0, h - s, s, s, D3DCOLOR_XRGB( 255, 255, 0 ) );
        fill( dev, w - s, h - s, s, s, D3DCOLOR_XRGB( 255, 255, 255 ) );
        IDirect3DDevice9_Present( dev, NULL, NULL, NULL, NULL );
    }
    IDirect3DDevice9_Release( dev );
    IDirect3D9_Release( d3d );
    DestroyWindow( hwnd );
    printf( "d3d9fs: done\n" );
    return 0;
}
