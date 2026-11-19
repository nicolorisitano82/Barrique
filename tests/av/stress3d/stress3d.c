/*
 * Barrique: stress3d, a 3D stress test of every graphics path Barrique has,
 * with metrics. One scene for all APIs: N textured cubes (default 4096) in a
 * grid, each with its own rotation and color computed by the CPU every frame,
 * and a 256x256 texture rewritten every frame.
 *
 *   gl      OpenGL fixed function (Apple GL through winemac), one draw per cube
 *   d3d9    Direct3D 9 fixed function (wined3d), one draw per cube
 *   d3d11   Direct3D 11, instancing (wined3d, or DXMT with BARRIQUE_D3D=dxmt)
 *   d3d12   Direct3D 12, instancing (vkd3d over MoltenVK)
 *   vulkan  Vulkan, instancing (MoltenVK)
 *
 *   stress3d.exe [api|all] [seconds] [instances] [--batch n] [--size WxH] [--json file]
 *
 * Instanced APIs draw `batch` cubes per call (default 16). Each frame uploads the
 * instance data (80 bytes per cube) and the texture (256 KB). Per API it prints a
 * line and, with --json, appends one JSON object per line to the file:
 *   ok, error, adapter, frames, fps, frame time avg/p50/p95/p99/max, time to the
 *   first frame, draws/s, Mtriangles/s, upload MB/s, CPU user/kernel seconds and
 *   use, peak working set, and the coverage of a frame read back (cubes drawn).
 * The exit code is the number of APIs that failed.
 */
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#define INITGUID
#define PSAPI_VERSION 2
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <GL/gl.h>
#include <d3d9.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <vulkan/vulkan.h>
#include "spirv.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define TEX_SIZE 256
#define FRAMES_IN_FLIGHT 2

static int width = 960, height = 600;
static double seconds = 8.0;
static unsigned int instances = 4096, batch = 16;

/* ------------------------------------------------------------------------ */
/* scene                                                                     */

struct vertex { float x, y, z, nx, ny, nz, u, v; };
struct instance { float m[16]; float color[4]; };   /* row-vector, row-major world matrix */

static struct vertex cube[36];
static struct instance *inst;
static BYTE *texels;
static unsigned int grid;

static void make_cube(void)
{
    static const float n[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
    static const float uv[6][2] = { {0,0}, {1,0}, {1,1}, {0,0}, {1,1}, {0,1} };
    int f, k, i = 0;

    for (f = 0; f < 6; f++)
    {
        float a[3], b[3], c[3] = { n[f][0] * 0.5f, n[f][1] * 0.5f, n[f][2] * 0.5f };
        int ax = f < 2 ? 1 : 0, bx = f < 2 ? 2 : (f < 4 ? 2 : 1);
        memset( a, 0, sizeof(a) ); memset( b, 0, sizeof(b) );
        a[ax] = 0.5f; b[bx] = 0.5f;
        for (k = 0; k < 6; k++, i++)
        {
            float su = uv[k][0] * 2 - 1, sv = uv[k][1] * 2 - 1;
            cube[i].x = c[0] + a[0] * su + b[0] * sv;
            cube[i].y = c[1] + a[1] * su + b[1] * sv;
            cube[i].z = c[2] + a[2] * su + b[2] * sv;
            cube[i].nx = n[f][0]; cube[i].ny = n[f][1]; cube[i].nz = n[f][2];
            cube[i].u = uv[k][0]; cube[i].v = uv[k][1];
        }
    }
}

static void mat_identity( float *m )
{
    memset( m, 0, 16 * sizeof(float) );
    m[0] = m[5] = m[10] = m[15] = 1;
}

static void mat_mul( float *out, const float *a, const float *b )   /* out = a * b (row vectors: a first) */
{
    float r[16];
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
    memcpy( out, r, sizeof(r) );
}

/* view-projection for a camera looking at the grid; zero_one: depth 0..1 (D3D, Vulkan), flip_y: Vulkan */
static void view_projection( float *m, int zero_one, int flip_y )
{
    float view[16], proj[16], dist = grid * 2.5f * 1.35f + 4, f = 1.0f / tanf( 0.5f * 1.0472f );
    float n = 1.0f, fa = dist * 3, aspect = (float)width / height;

    mat_identity( view );
    view[14] = dist;                                   /* camera at -dist on z, looking at +z */
    memset( proj, 0, sizeof(proj) );
    proj[0] = f / aspect;
    proj[5] = flip_y ? -f : f;
    if (zero_one) { proj[10] = fa / (fa - n); proj[14] = -n * fa / (fa - n); }
    else { proj[10] = (fa + n) / (fa - n); proj[14] = -2 * n * fa / (fa - n); }
    proj[11] = 1;
    mat_mul( m, view, proj );
}

/* the CPU part of every frame: instance matrices and colors */
static void update_instances( float t )
{
    unsigned int i;
    float half = (grid - 1) * 1.25f;

    for (i = 0; i < instances; i++)
    {
        struct instance *p = &inst[i];
        float a = t * 1.3f + i * 0.37f, b = t * 0.7f + i * 0.11f;
        float ca = cosf( a ), sa = sinf( a ), cb = cosf( b ), sb = sinf( b );
        unsigned int gx = i % grid, gy = (i / grid) % grid, gz = i / (grid * grid);

        /* rotation about y (a) then x (b), then translation */
        p->m[0] = ca;  p->m[1] = sa * sb;  p->m[2] = -sa * cb; p->m[3] = 0;
        p->m[4] = 0;   p->m[5] = cb;       p->m[6] = sb;       p->m[7] = 0;
        p->m[8] = sa;  p->m[9] = -ca * sb; p->m[10] = ca * cb; p->m[11] = 0;
        p->m[12] = gx * 2.5f - half; p->m[13] = gy * 2.5f - half; p->m[14] = gz * 2.5f - half; p->m[15] = 1;
        p->color[0] = 0.4f + 0.6f * (gx + 1) / grid;
        p->color[1] = 0.4f + 0.6f * (gy + 1) / grid;
        p->color[2] = 0.4f + 0.6f * (gz + 1) / grid;
        p->color[3] = 1;
    }
}

/* the texture: a checkerboard with a moving bright band, RGBA */
static void update_texture( unsigned int frame )
{
    unsigned int x, y, band = (frame * 3) % TEX_SIZE;
    for (y = 0; y < TEX_SIZE; y++)
    {
        BYTE *p = texels + y * TEX_SIZE * 4;
        for (x = 0; x < TEX_SIZE; x++, p += 4)
        {
            BYTE c = ((x >> 5) ^ (y >> 5)) & 1 ? 255 : 170;
            if ((x + y) % TEX_SIZE - band < 12u) c = 255;
            p[0] = p[1] = p[2] = c;
            p[3] = 255;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* window, timing, metrics                                                   */

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static HWND create_window( const char *api )
{
    static int registered;
    WNDCLASSW wc = {0};
    RECT r = { 0, 0, width, height };
    WCHAR title[64];

    if (!registered)
    {
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = wndproc;
        wc.hInstance = GetModuleHandleW( NULL );
        wc.hCursor = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
        wc.lpszClassName = L"stress3d";
        RegisterClassW( &wc );
        registered = 1;
    }
    AdjustWindowRect( &r, WS_OVERLAPPEDWINDOW, FALSE );
    swprintf( title, ARRAY_SIZE(title), L"stress3d - %hs", api );
    return CreateWindowW( L"stress3d", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40,
                          r.right - r.left, r.bottom - r.top, NULL, NULL, GetModuleHandleW( NULL ), NULL );
}

static void pump(void)
{
    MSG msg;
    while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
}

static double now(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;
    if (!freq.QuadPart) QueryPerformanceFrequency( &freq );
    QueryPerformanceCounter( &t );
    return (double)t.QuadPart / freq.QuadPart;
}

static double filetime_s( FILETIME ft )
{
    return (((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 1e7;
}

struct result
{
    const char *api;
    int ok;
    char error[200];
    char adapter[200];
    unsigned int frames, draws_per_frame;
    double first_ms, fps, avg_ms, p50_ms, p95_ms, p99_ms, max_ms;
    double draws_per_s, mtris_per_s, upload_mb_s;
    double cpu_user, cpu_kernel, cpu_use, peak_ws_mb, coverage;
};

/* pixels drawn on the background (any channel over 40, background is 13): needs >= 2% */
static void check_pixels( struct result *res, const BYTE *pixels, unsigned int pitch )
{
    unsigned int lit = 0, x, y;

    for (y = 0; y < (unsigned int)height; y++)
    {
        const BYTE *p = pixels + y * pitch;
        for (x = 0; x < (unsigned int)width; x++, p += 4)
            if (p[0] > 40 || p[1] > 40 || p[2] > 40) lit++;
    }
    res->coverage = 100.0 * lit / ((double)width * height);
    res->ok = res->coverage >= 2.0;
    if (!res->ok) snprintf( res->error, sizeof(res->error), "check failed: %.2f%% of the frame drawn", res->coverage );
}

static int cmp_double( const void *a, const void *b )
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

/* frame loop: draw(ctx, t, frame) updates the scene data it needs and presents one frame */
static void run_loop( struct result *res, void (*draw)( void *ctx, float t, unsigned int frame ), void *ctx,
                      double start, unsigned int draws_per_frame )
{
    FILETIME c, e, k0, u0, k1, u1;
    PROCESS_MEMORY_COUNTERS pmc = { sizeof(pmc) };
    double t0, t, last, *times = NULL, sum = 0;
    unsigned int frames = 0, max_frames = 0;

    draw( ctx, 0, 0 );
    res->first_ms = (now() - start) * 1000;
    GetProcessTimes( GetCurrentProcess(), &c, &e, &k0, &u0 );
    t0 = last = now();
    do
    {
        pump();
        draw( ctx, (float)(now() - t0), frames + 1 );
        t = now();
        if (frames == max_frames)
        {
            max_frames = max_frames ? max_frames * 2 : 4096;
            times = realloc( times, max_frames * sizeof(*times) );
        }
        times[frames++] = (t - last) * 1000;
        sum += (t - last) * 1000;
        last = t;
    } while (t - t0 < seconds);
    GetProcessTimes( GetCurrentProcess(), &c, &e, &k1, &u1 );
    K32GetProcessMemoryInfo( GetCurrentProcess(), &pmc, sizeof(pmc) );

    res->frames = frames;
    res->fps = frames / (t - t0);
    res->draws_per_frame = draws_per_frame;
    qsort( times, frames, sizeof(*times), cmp_double );
    res->avg_ms = sum / frames;
    res->p50_ms = times[frames / 2];
    res->p95_ms = times[(unsigned int)(frames * 0.95)];
    res->p99_ms = times[(unsigned int)(frames * 0.99)];
    res->max_ms = times[frames - 1];
    res->draws_per_s = res->fps * draws_per_frame;
    res->mtris_per_s = res->fps * instances * 12 / 1e6;
    res->upload_mb_s = res->fps * (instances * sizeof(struct instance) + TEX_SIZE * TEX_SIZE * 4) / (1024.0 * 1024.0);
    res->cpu_user = filetime_s( u1 ) - filetime_s( u0 );
    res->cpu_kernel = filetime_s( k1 ) - filetime_s( k0 );
    res->cpu_use = (res->cpu_user + res->cpu_kernel) / (t - t0);
    res->peak_ws_mb = pmc.PeakWorkingSetSize / (1024.0 * 1024.0);
    free( times );
}

/* ------------------------------------------------------------------------ */
/* OpenGL: fixed function, one glDrawArrays per cube                          */

#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
typedef void (APIENTRY *PFNGLGENBUFFERS)( GLsizei, GLuint * );
typedef void (APIENTRY *PFNGLBINDBUFFER)( GLenum, GLuint );
typedef void (APIENTRY *PFNGLBUFFERDATA)( GLenum, ptrdiff_t, const void *, GLenum );
typedef void (APIENTRY *PFNGLDELETEBUFFERS)( GLsizei, const GLuint * );
typedef BOOL (WINAPI *PFNWGLSWAPINTERVALEXT)( int );

struct gl_ctx { HDC dc; GLuint tex; };

static void gl_render( struct gl_ctx *g, float t, unsigned int frame )
{
    float vp[16], m[16];
    unsigned int i;

    update_instances( t );
    update_texture( frame );
    glBindTexture( GL_TEXTURE_2D, g->tex );
    glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, TEX_SIZE, TEX_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, texels );
    glClearColor( 0.05f, 0.05f, 0.05f, 1 );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    view_projection( vp, 0, 0 );
    glMatrixMode( GL_PROJECTION );
    glLoadMatrixf( vp );
    glMatrixMode( GL_MODELVIEW );
    for (i = 0; i < instances; i++)
    {
        memcpy( m, inst[i].m, sizeof(m) );
        glLoadMatrixf( m );   /* row-vector row-major == GL column-major */
        glColor4fv( inst[i].color );
        glDrawArrays( GL_TRIANGLES, 0, 36 );
    }
}

static void gl_draw( void *ctx, float t, unsigned int frame )
{
    struct gl_ctx *g = ctx;
    gl_render( g, t, frame );
    SwapBuffers( g->dc );
}

static void run_gl( struct result *res )
{
    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
                                  PFD_TYPE_RGBA, 32, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 24 };
    PFNGLGENBUFFERS pglGenBuffers;
    PFNGLBINDBUFFER pglBindBuffer;
    PFNGLBUFFERDATA pglBufferData;
    PFNGLDELETEBUFFERS pglDeleteBuffers;
    PFNWGLSWAPINTERVALEXT pwglSwapIntervalEXT;
    float light_pos[4] = { 0.408248f, 0.408248f, -0.816497f, 0 }, ambient[4] = { 0.25f, 0.25f, 0.25f, 1 };
    double start = now();
    struct gl_ctx g = {0};
    HGLRC rc = NULL;
    GLuint vbo = 0;
    BYTE *pixels;
    HWND hwnd;
    int format;

    hwnd = create_window( "gl" );
    g.dc = GetDC( hwnd );
    if (!(format = ChoosePixelFormat( g.dc, &pfd )) || !SetPixelFormat( g.dc, format, &pfd ) ||
        !(rc = wglCreateContext( g.dc )) || !wglMakeCurrent( g.dc, rc ))
    {
        snprintf( res->error, sizeof(res->error), "GL context: error %lu", GetLastError() );
        goto done;
    }
    snprintf( res->adapter, sizeof(res->adapter), "%s, GL %s", (const char *)glGetString( GL_RENDERER ),
              (const char *)glGetString( GL_VERSION ) );
    pglGenBuffers = (void *)wglGetProcAddress( "glGenBuffers" );
    pglBindBuffer = (void *)wglGetProcAddress( "glBindBuffer" );
    pglBufferData = (void *)wglGetProcAddress( "glBufferData" );
    pglDeleteBuffers = (void *)wglGetProcAddress( "glDeleteBuffers" );
    if (!pglGenBuffers || !pglBindBuffer || !pglBufferData)
    {
        snprintf( res->error, sizeof(res->error), "no vertex buffer objects" );
        goto done;
    }
    if ((pwglSwapIntervalEXT = (void *)wglGetProcAddress( "wglSwapIntervalEXT" ))) pwglSwapIntervalEXT( 0 );

    pglGenBuffers( 1, &vbo );
    pglBindBuffer( GL_ARRAY_BUFFER, vbo );
    pglBufferData( GL_ARRAY_BUFFER, sizeof(cube), cube, GL_STATIC_DRAW );
    glEnableClientState( GL_VERTEX_ARRAY );
    glEnableClientState( GL_NORMAL_ARRAY );
    glEnableClientState( GL_TEXTURE_COORD_ARRAY );
    glVertexPointer( 3, GL_FLOAT, sizeof(struct vertex), (void *)0 );
    glNormalPointer( GL_FLOAT, sizeof(struct vertex), (void *)12 );
    glTexCoordPointer( 2, GL_FLOAT, sizeof(struct vertex), (void *)24 );
    glGenTextures( 1, &g.tex );
    glBindTexture( GL_TEXTURE_2D, g.tex );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, TEX_SIZE, TEX_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels );
    glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
    glEnable( GL_TEXTURE_2D );
    glEnable( GL_DEPTH_TEST );
    glEnable( GL_LIGHTING );
    glEnable( GL_LIGHT0 );
    glEnable( GL_COLOR_MATERIAL );
    glColorMaterial( GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE );
    glLightModelfv( GL_LIGHT_MODEL_AMBIENT, ambient );
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();
    glLightfv( GL_LIGHT0, GL_POSITION, light_pos );
    glViewport( 0, 0, width, height );

    gl_render( &g, 0, 0 );
    glFinish();
    pixels = malloc( width * height * 4 );
    glReadBuffer( GL_BACK );
    glPixelStorei( GL_PACK_ALIGNMENT, 1 );
    glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels );
    check_pixels( res, pixels, width * 4 );
    free( pixels );
    if (glGetError() != GL_NO_ERROR && res->ok)
    {
        res->ok = 0;
        snprintf( res->error, sizeof(res->error), "GL error after the check frame" );
    }
    run_loop( res, gl_draw, &g, start, instances );

done:
    if (g.tex) glDeleteTextures( 1, &g.tex );
    if (vbo && pglDeleteBuffers) pglDeleteBuffers( 1, &vbo );
    if (rc) { wglMakeCurrent( NULL, NULL ); wglDeleteContext( rc ); }
    ReleaseDC( hwnd, g.dc );
    DestroyWindow( hwnd );
}

/* ------------------------------------------------------------------------ */
/* Direct3D 9: fixed function, one DrawPrimitive per cube                     */

struct d3d9_ctx { IDirect3DDevice9 *device; IDirect3DVertexBuffer9 *vb; IDirect3DTexture9 *tex; };

static void d3d9_render( struct d3d9_ctx *d, float t, unsigned int frame )
{
    D3DLOCKED_RECT lr;
    D3DMATERIAL9 mat = {0};
    float vp[16];
    unsigned int i, y;

    update_instances( t );
    update_texture( frame );
    if (SUCCEEDED(IDirect3DTexture9_LockRect( d->tex, 0, &lr, NULL, D3DLOCK_DISCARD )))
    {
        for (y = 0; y < TEX_SIZE; y++)   /* RGBA to BGRA is the same for grey texels */
            memcpy( (BYTE *)lr.pBits + y * lr.Pitch, texels + y * TEX_SIZE * 4, TEX_SIZE * 4 );
        IDirect3DTexture9_UnlockRect( d->tex, 0 );
    }
    IDirect3DDevice9_Clear( d->device, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB( 13, 13, 13 ), 1, 0 );
    IDirect3DDevice9_BeginScene( d->device );
    view_projection( vp, 1, 0 );
    IDirect3DDevice9_SetTransform( d->device, D3DTS_PROJECTION, (D3DMATRIX *)vp );
    for (i = 0; i < instances; i++)
    {
        IDirect3DDevice9_SetTransform( d->device, D3DTS_WORLD, (D3DMATRIX *)inst[i].m );
        mat.Diffuse.r = mat.Ambient.r = inst[i].color[0];
        mat.Diffuse.g = mat.Ambient.g = inst[i].color[1];
        mat.Diffuse.b = mat.Ambient.b = inst[i].color[2];
        mat.Diffuse.a = mat.Ambient.a = 1;
        IDirect3DDevice9_SetMaterial( d->device, &mat );
        IDirect3DDevice9_DrawPrimitive( d->device, D3DPT_TRIANGLELIST, 0, 12 );
    }
    IDirect3DDevice9_EndScene( d->device );
}

static void d3d9_draw( void *ctx, float t, unsigned int frame )
{
    struct d3d9_ctx *d = ctx;
    d3d9_render( d, t, frame );
    IDirect3DDevice9_Present( d->device, NULL, NULL, NULL, NULL );
}

static void run_d3d9( struct result *res )
{
    D3DPRESENT_PARAMETERS pp = {0};
    D3DADAPTER_IDENTIFIER9 id;
    D3DLIGHT9 light = {0};
    IDirect3DSurface9 *back = NULL, *sys = NULL;
    struct d3d9_ctx d = {0};
    double start = now();
    IDirect3D9 *d3d;
    D3DLOCKED_RECT lr;
    float ident[16];
    void *data;
    HRESULT hr;
    HWND hwnd;

    hwnd = create_window( "d3d9" );
    if (!(d3d = Direct3DCreate9( D3D_SDK_VERSION )))
    {
        snprintf( res->error, sizeof(res->error), "Direct3DCreate9 failed" );
        DestroyWindow( hwnd );
        return;
    }
    if (SUCCEEDED(IDirect3D9_GetAdapterIdentifier( d3d, D3DADAPTER_DEFAULT, 0, &id )))
        snprintf( res->adapter, sizeof(res->adapter), "%s", id.Description );
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = width;
    pp.BackBufferHeight = height;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24X8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    pp.hDeviceWindow = hwnd;
    if (FAILED(hr = IDirect3D9_CreateDevice( d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                             D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &d.device )))
    {
        snprintf( res->error, sizeof(res->error), "CreateDevice: %#lx", hr );
        goto done;
    }
    if (FAILED(hr = IDirect3DDevice9_CreateVertexBuffer( d.device, sizeof(cube), D3DUSAGE_WRITEONLY,
                                                        D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1, D3DPOOL_DEFAULT, &d.vb, NULL )) ||
        FAILED(hr = IDirect3DVertexBuffer9_Lock( d.vb, 0, 0, &data, 0 )))
    {
        snprintf( res->error, sizeof(res->error), "vertex buffer: %#lx", hr );
        goto done;
    }
    memcpy( data, cube, sizeof(cube) );
    IDirect3DVertexBuffer9_Unlock( d.vb );
    if (FAILED(hr = IDirect3DDevice9_CreateTexture( d.device, TEX_SIZE, TEX_SIZE, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                                                    D3DPOOL_DEFAULT, &d.tex, NULL )))
    {
        snprintf( res->error, sizeof(res->error), "texture: %#lx", hr );
        goto done;
    }
    IDirect3DDevice9_SetStreamSource( d.device, 0, d.vb, 0, sizeof(struct vertex) );
    IDirect3DDevice9_SetFVF( d.device, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1 );
    IDirect3DDevice9_SetTexture( d.device, 0, (IDirect3DBaseTexture9 *)d.tex );
    IDirect3DDevice9_SetTextureStageState( d.device, 0, D3DTSS_COLOROP, D3DTOP_MODULATE );
    IDirect3DDevice9_SetTextureStageState( d.device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
    IDirect3DDevice9_SetTextureStageState( d.device, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
    IDirect3DDevice9_SetSamplerState( d.device, 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR );
    IDirect3DDevice9_SetSamplerState( d.device, 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR );
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = light.Diffuse.a = 1;
    light.Direction.x = -0.408248f; light.Direction.y = -0.408248f; light.Direction.z = 0.816497f;
    IDirect3DDevice9_SetLight( d.device, 0, &light );
    IDirect3DDevice9_LightEnable( d.device, 0, TRUE );
    IDirect3DDevice9_SetRenderState( d.device, D3DRS_LIGHTING, TRUE );
    IDirect3DDevice9_SetRenderState( d.device, D3DRS_AMBIENT, D3DCOLOR_COLORVALUE( 0.25f, 0.25f, 0.25f, 1 ) );
    IDirect3DDevice9_SetRenderState( d.device, D3DRS_CULLMODE, D3DCULL_NONE );
    IDirect3DDevice9_SetRenderState( d.device, D3DRS_ZENABLE, D3DZB_TRUE );
    mat_identity( ident );
    IDirect3DDevice9_SetTransform( d.device, D3DTS_VIEW, (D3DMATRIX *)ident );

    d3d9_render( &d, 0, 0 );
    if (FAILED(hr = IDirect3DDevice9_GetBackBuffer( d.device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back )) ||
        FAILED(hr = IDirect3DDevice9_CreateOffscreenPlainSurface( d.device, width, height, D3DFMT_X8R8G8B8,
                                                                   D3DPOOL_SYSTEMMEM, &sys, NULL )) ||
        FAILED(hr = IDirect3DDevice9_GetRenderTargetData( d.device, back, sys )) ||
        FAILED(hr = IDirect3DSurface9_LockRect( sys, &lr, NULL, D3DLOCK_READONLY )))
    {
        snprintf( res->error, sizeof(res->error), "read back: %#lx", hr );
        goto done;
    }
    check_pixels( res, lr.pBits, lr.Pitch );
    IDirect3DSurface9_UnlockRect( sys );
    run_loop( res, d3d9_draw, &d, start, instances );

done:
    if (sys) IDirect3DSurface9_Release( sys );
    if (back) IDirect3DSurface9_Release( back );
    if (d.tex) IDirect3DTexture9_Release( d.tex );
    if (d.vb) IDirect3DVertexBuffer9_Release( d.vb );
    if (d.device) IDirect3DDevice9_Release( d.device );
    IDirect3D9_Release( d3d );
    DestroyWindow( hwnd );
}

/* ------------------------------------------------------------------------ */
/* HLSL for Direct3D 11 and 12: instancing                                    */

static const char shader_source[] =
    "cbuffer c : register(b0) { row_major float4x4 vp; };\n"
    "Texture2D tex : register(t0);\n"
    "SamplerState smp : register(s0);\n"
    "struct vs_in { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0;\n"
    "               float4 m0 : WORLD0; float4 m1 : WORLD1; float4 m2 : WORLD2; float4 m3 : WORLD3; float4 col : COLOR; };\n"
    "struct vs_out { float4 pos : SV_Position; float4 col : COLOR; float2 uv : TEXCOORD0; };\n"
    "vs_out vs( vs_in i )\n"
    "{\n"
    "    vs_out o;\n"
    "    float4x4 m = float4x4( i.m0, i.m1, i.m2, i.m3 );\n"
    "    float3 wn = normalize( mul( float4( i.n, 0 ), m ).xyz );\n"
    "    float d = max( dot( wn, float3( 0.408248, 0.408248, -0.816497 ) ), 0 );\n"
    "    o.pos = mul( mul( float4( i.p, 1 ), m ), vp );\n"
    "    o.col = float4( i.col.rgb * ( 0.25 + 0.75 * d ), 1 );\n"
    "    o.uv = i.uv;\n"
    "    return o;\n"
    "}\n"
    "float4 ps( vs_out i ) : SV_Target { return i.col * tex.Sample( smp, i.uv ); }\n";

typedef HRESULT (WINAPI *PFND3DCOMPILE)( const void *, SIZE_T, const char *, const D3D_SHADER_MACRO *, ID3DInclude *,
                                         const char *, const char *, UINT, UINT, ID3DBlob **, ID3DBlob ** );

static HRESULT compile( const char *entry, const char *target, ID3DBlob **blob, char *error, size_t size )
{
    static PFND3DCOMPILE pD3DCompile;
    ID3DBlob *messages = NULL;
    HRESULT hr;

    if (!pD3DCompile)
    {
        HMODULE module = LoadLibraryA( "d3dcompiler_47.dll" );
        if (!module || !(pD3DCompile = (void *)GetProcAddress( module, "D3DCompile" )))
        {
            snprintf( error, size, "d3dcompiler_47.dll not available" );
            return E_FAIL;
        }
    }
    if (FAILED(hr = pD3DCompile( shader_source, sizeof(shader_source) - 1, "stress3d", NULL, NULL, entry, target, 0, 0,
                                 blob, &messages )))
        snprintf( error, size, "D3DCompile %s: %#lx %s", entry, hr,
                  messages ? (const char *)ID3D10Blob_GetBufferPointer( messages ) : "" );
    if (messages) ID3D10Blob_Release( messages );
    return hr;
}

#define LAYOUT_ELEMENTS(cls_vertex, cls_instance) \
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, cls_vertex, 0 }, \
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, cls_vertex, 0 }, \
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, cls_vertex, 0 }, \
    { "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, cls_instance, 1 }, \
    { "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, cls_instance, 1 }, \
    { "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, cls_instance, 1 }, \
    { "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, cls_instance, 1 }, \
    { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, cls_instance, 1 }

static void adapter_name( IDXGIAdapter *adapter, char *out, size_t size, const char *suffix )
{
    DXGI_ADAPTER_DESC desc;
    if (adapter && SUCCEEDED(IDXGIAdapter_GetDesc( adapter, &desc )))
        snprintf( out, size, "%ls%s", desc.Description, suffix );
}

static unsigned int batches(void) { return (instances + batch - 1) / batch; }

/* ------------------------------------------------------------------------ */
/* Direct3D 11                                                               */

struct d3d11_ctx
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    IDXGISwapChain *swapchain;
    ID3D11RenderTargetView *rtv;
    ID3D11DepthStencilView *dsv;
    ID3D11Buffer *cb, *ib;
    ID3D11Texture2D *tex;
};

static void d3d11_render( struct d3d11_ctx *d, float t, unsigned int frame )
{
    static const float clear[4] = { 0.05f, 0.05f, 0.05f, 1 };
    D3D11_MAPPED_SUBRESOURCE map;
    float vp[16];
    unsigned int b;

    update_instances( t );
    update_texture( frame );
    if (SUCCEEDED(ID3D11DeviceContext_Map( d->context, (ID3D11Resource *)d->ib, 0, D3D11_MAP_WRITE_DISCARD, 0, &map )))
    {
        memcpy( map.pData, inst, instances * sizeof(*inst) );
        ID3D11DeviceContext_Unmap( d->context, (ID3D11Resource *)d->ib, 0 );
    }
    ID3D11DeviceContext_UpdateSubresource( d->context, (ID3D11Resource *)d->tex, 0, NULL, texels, TEX_SIZE * 4, 0 );
    view_projection( vp, 1, 0 );
    ID3D11DeviceContext_UpdateSubresource( d->context, (ID3D11Resource *)d->cb, 0, NULL, vp, 0, 0 );
    ID3D11DeviceContext_ClearRenderTargetView( d->context, d->rtv, clear );
    ID3D11DeviceContext_ClearDepthStencilView( d->context, d->dsv, D3D11_CLEAR_DEPTH, 1.0f, 0 );
    for (b = 0; b < batches(); b++)
    {
        unsigned int first = b * batch, count = min( batch, instances - first );
        ID3D11DeviceContext_DrawInstanced( d->context, 36, count, 0, first );
    }
}

static void d3d11_draw( void *ctx, float t, unsigned int frame )
{
    struct d3d11_ctx *d = ctx;
    d3d11_render( d, t, frame );
    IDXGISwapChain_Present( d->swapchain, 0, 0 );
}

static void run_d3d11( struct result *res )
{
    static const D3D11_INPUT_ELEMENT_DESC layout[] =
        { LAYOUT_ELEMENTS( D3D11_INPUT_PER_VERTEX_DATA, D3D11_INPUT_PER_INSTANCE_DATA ) };
    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                                D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3 };
    DXGI_SWAP_CHAIN_DESC sd = {0};
    D3D11_TEXTURE2D_DESC td = {0};
    D3D11_BUFFER_DESC bd = {0};
    D3D11_SUBRESOURCE_DATA init = {0};
    D3D11_VIEWPORT vpt = { 0, 0, (float)width, (float)height, 0, 1 };
    D3D11_RASTERIZER_DESC rd = {0};
    D3D11_SAMPLER_DESC smd = {0};
    D3D11_MAPPED_SUBRESOURCE map;
    ID3D11Texture2D *back = NULL, *depth = NULL, *staging = NULL;
    ID3D11Buffer *vb = NULL;
    ID3D11VertexShader *vs = NULL;
    ID3D11PixelShader *ps = NULL;
    ID3D11InputLayout *il = NULL;
    ID3D11RasterizerState *rs = NULL;
    ID3D11SamplerState *smp = NULL;
    ID3D11ShaderResourceView *srv = NULL;
    ID3DBlob *vs_blob = NULL, *ps_blob = NULL;
    IDXGIDevice *dxgi_device = NULL;
    IDXGIAdapter *adapter = NULL;
    struct d3d11_ctx d = {0};
    D3D_FEATURE_LEVEL level;
    ID3D11Buffer *vbs[2];
    UINT strides[2] = { sizeof(struct vertex), sizeof(struct instance) }, offsets[2] = { 0, 0 };
    /* DXMT has no 32-bit build: 32-bit programs always get wined3d */
    const char *backend = sizeof(void *) == 4 ? "wined3d" : getenv( "BARRIQUE_D3D" );
    char suffix[64];
    double start = now();
    HRESULT hr;
    HWND hwnd;

    hwnd = create_window( "d3d11" );
    sd.BufferCount = 2;
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    if (FAILED(hr = D3D11CreateDeviceAndSwapChain( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, levels, ARRAY_SIZE(levels),
                                                   D3D11_SDK_VERSION, &sd, &d.swapchain, &d.device, &level, &d.context )))
    {
        snprintf( res->error, sizeof(res->error), "D3D11CreateDeviceAndSwapChain: %#lx", hr );
        goto done;
    }
    snprintf( suffix, sizeof(suffix), ", level %s, %s",
              level == D3D_FEATURE_LEVEL_11_1 ? "11_1" : level == D3D_FEATURE_LEVEL_11_0 ? "11_0" :
              level == D3D_FEATURE_LEVEL_10_1 ? "10_1" : level == D3D_FEATURE_LEVEL_10_0 ? "10_0" : "9_3",
              backend && *backend ? backend : "wined3d" );
    if (SUCCEEDED(ID3D11Device_QueryInterface( d.device, &IID_IDXGIDevice, (void **)&dxgi_device )) &&
        SUCCEEDED(IDXGIDevice_GetAdapter( dxgi_device, &adapter )))
        adapter_name( adapter, res->adapter, sizeof(res->adapter), suffix );

    /* shader model for the level: instancing needs at least 9_3 */
    {
        const char *vs_target = "vs_4_0_level_9_3", *ps_target = "ps_4_0_level_9_3";
        if (level >= D3D_FEATURE_LEVEL_11_0) { vs_target = "vs_5_0"; ps_target = "ps_5_0"; }
        else if (level >= D3D_FEATURE_LEVEL_10_0) { vs_target = "vs_4_0"; ps_target = "ps_4_0"; }
        if (FAILED(compile( "vs", vs_target, &vs_blob, res->error, sizeof(res->error) )) ||
            FAILED(compile( "ps", ps_target, &ps_blob, res->error, sizeof(res->error) )))
            goto done;
    }
    if (FAILED(hr = ID3D11Device_CreateVertexShader( d.device, ID3D10Blob_GetBufferPointer( vs_blob ),
                                                     ID3D10Blob_GetBufferSize( vs_blob ), NULL, &vs )) ||
        FAILED(hr = ID3D11Device_CreatePixelShader( d.device, ID3D10Blob_GetBufferPointer( ps_blob ),
                                                    ID3D10Blob_GetBufferSize( ps_blob ), NULL, &ps )) ||
        FAILED(hr = ID3D11Device_CreateInputLayout( d.device, layout, ARRAY_SIZE(layout),
                                                    ID3D10Blob_GetBufferPointer( vs_blob ),
                                                    ID3D10Blob_GetBufferSize( vs_blob ), &il )))
    {
        snprintf( res->error, sizeof(res->error), "shaders/input layout: %#lx", hr );
        goto done;
    }

    bd.ByteWidth = sizeof(cube);
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    init.pSysMem = cube;
    if (FAILED(hr = ID3D11Device_CreateBuffer( d.device, &bd, &init, &vb ))) goto buffer_error;
    bd.ByteWidth = instances * sizeof(struct instance);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(hr = ID3D11Device_CreateBuffer( d.device, &bd, NULL, &d.ib ))) goto buffer_error;
    bd.ByteWidth = 64;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.CPUAccessFlags = 0;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(hr = ID3D11Device_CreateBuffer( d.device, &bd, NULL, &d.cb ))) goto buffer_error;

    td.Width = td.Height = TEX_SIZE;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(hr = ID3D11Device_CreateTexture2D( d.device, &td, NULL, &d.tex )) ||
        FAILED(hr = ID3D11Device_CreateShaderResourceView( d.device, (ID3D11Resource *)d.tex, NULL, &srv )))
        goto buffer_error;
    smd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    smd.AddressU = smd.AddressV = smd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    smd.MaxLOD = D3D11_FLOAT32_MAX;
    ID3D11Device_CreateSamplerState( d.device, &smd, &smp );

    IDXGISwapChain_GetBuffer( d.swapchain, 0, &IID_ID3D11Texture2D, (void **)&back );
    ID3D11Device_CreateRenderTargetView( d.device, (ID3D11Resource *)back, NULL, &d.rtv );
    td.Width = width;
    td.Height = height;
    td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(hr = ID3D11Device_CreateTexture2D( d.device, &td, NULL, &depth )) ||
        FAILED(hr = ID3D11Device_CreateDepthStencilView( d.device, (ID3D11Resource *)depth, NULL, &d.dsv )))
        goto buffer_error;
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ID3D11Device_CreateRasterizerState( d.device, &rd, &rs );

    vbs[0] = vb; vbs[1] = d.ib;
    ID3D11DeviceContext_OMSetRenderTargets( d.context, 1, &d.rtv, d.dsv );
    ID3D11DeviceContext_RSSetViewports( d.context, 1, &vpt );
    ID3D11DeviceContext_RSSetState( d.context, rs );
    ID3D11DeviceContext_IASetInputLayout( d.context, il );
    ID3D11DeviceContext_IASetPrimitiveTopology( d.context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    ID3D11DeviceContext_IASetVertexBuffers( d.context, 0, 2, vbs, strides, offsets );
    ID3D11DeviceContext_VSSetShader( d.context, vs, NULL, 0 );
    ID3D11DeviceContext_VSSetConstantBuffers( d.context, 0, 1, &d.cb );
    ID3D11DeviceContext_PSSetShader( d.context, ps, NULL, 0 );
    ID3D11DeviceContext_PSSetShaderResources( d.context, 0, 1, &srv );
    ID3D11DeviceContext_PSSetSamplers( d.context, 0, 1, &smp );

    d3d11_render( &d, 0, 0 );
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(hr = ID3D11Device_CreateTexture2D( d.device, &td, NULL, &staging ))) goto buffer_error;
    ID3D11DeviceContext_CopyResource( d.context, (ID3D11Resource *)staging, (ID3D11Resource *)back );
    if (FAILED(hr = ID3D11DeviceContext_Map( d.context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map )))
    {
        snprintf( res->error, sizeof(res->error), "read back: %#lx", hr );
        goto done;
    }
    check_pixels( res, map.pData, map.RowPitch );
    ID3D11DeviceContext_Unmap( d.context, (ID3D11Resource *)staging, 0 );
    run_loop( res, d3d11_draw, &d, start, batches() );
    goto done;

buffer_error:
    snprintf( res->error, sizeof(res->error), "resource creation: %#lx", hr );
done:
    if (staging) ID3D11Texture2D_Release( staging );
    if (smp) ID3D11SamplerState_Release( smp );
    if (srv) ID3D11ShaderResourceView_Release( srv );
    if (d.tex) ID3D11Texture2D_Release( d.tex );
    if (rs) ID3D11RasterizerState_Release( rs );
    if (d.dsv) ID3D11DepthStencilView_Release( d.dsv );
    if (depth) ID3D11Texture2D_Release( depth );
    if (d.rtv) ID3D11RenderTargetView_Release( d.rtv );
    if (back) ID3D11Texture2D_Release( back );
    if (d.cb) ID3D11Buffer_Release( d.cb );
    if (d.ib) ID3D11Buffer_Release( d.ib );
    if (vb) ID3D11Buffer_Release( vb );
    if (il) ID3D11InputLayout_Release( il );
    if (ps) ID3D11PixelShader_Release( ps );
    if (vs) ID3D11VertexShader_Release( vs );
    if (ps_blob) ID3D10Blob_Release( ps_blob );
    if (vs_blob) ID3D10Blob_Release( vs_blob );
    if (adapter) IDXGIAdapter_Release( adapter );
    if (dxgi_device) IDXGIDevice_Release( dxgi_device );
    if (d.context) ID3D11DeviceContext_Release( d.context );
    if (d.swapchain) IDXGISwapChain_Release( d.swapchain );
    if (d.device) ID3D11Device_Release( d.device );
    DestroyWindow( hwnd );
}

/* ------------------------------------------------------------------------ */
/* Direct3D 12                                                               */

struct d3d12_ctx
{
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *allocator[FRAMES_IN_FLIGHT];
    ID3D12GraphicsCommandList *list;
    IDXGISwapChain3 *swapchain;
    ID3D12Resource *targets[FRAMES_IN_FLIGHT], *depth, *vb, *ib[FRAMES_IN_FLIGHT], *upload[FRAMES_IN_FLIGHT], *tex;
    void *ib_map[FRAMES_IN_FLIGHT], *upload_map[FRAMES_IN_FLIGHT];
    ID3D12DescriptorHeap *rtv_heap, *dsv_heap, *srv_heap;
    UINT rtv_size;
    ID3D12RootSignature *root;
    ID3D12PipelineState *pso;
    ID3D12Fence *fence;
    UINT64 fence_value, frame_fence[FRAMES_IN_FLIGHT];
    HANDLE event;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT tex_fp;
};

static void d3d12_wait( struct d3d12_ctx *d, UINT64 value )
{
    if (ID3D12Fence_GetCompletedValue( d->fence ) >= value) return;
    ID3D12Fence_SetEventOnCompletion( d->fence, value, d->event );
    WaitForSingleObject( d->event, INFINITE );
}

static void transition( ID3D12GraphicsCommandList *list, ID3D12Resource *res, D3D12_RESOURCE_STATES from,
                        D3D12_RESOURCE_STATES to )
{
    D3D12_RESOURCE_BARRIER b = {0};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    ID3D12GraphicsCommandList_ResourceBarrier( list, 1, &b );
}

/* record one frame into the list for back buffer i, using upload slot s (RENDER_TARGET on exit) */
static void d3d12_record( struct d3d12_ctx *d, UINT i, UINT s, float t, unsigned int frame )
{
    static const float clear[4] = { 0.05f, 0.05f, 0.05f, 1 };
    D3D12_VIEWPORT vpt = { 0, 0, (float)width, (float)height, 0, 1 };
    D3D12_RECT scissor = { 0, 0, width, height };
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart( d->rtv_heap );
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart( d->dsv_heap );
    D3D12_VERTEX_BUFFER_VIEW vbv[2];
    D3D12_TEXTURE_COPY_LOCATION src = {0}, dst = {0};
    float vp[16];
    unsigned int b, y;

    update_instances( t );
    update_texture( frame );
    memcpy( d->ib_map[s], inst, instances * sizeof(*inst) );
    for (y = 0; y < TEX_SIZE; y++)
        memcpy( (BYTE *)d->upload_map[s] + y * d->tex_fp.Footprint.RowPitch, texels + y * TEX_SIZE * 4, TEX_SIZE * 4 );

    transition( d->list, d->tex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST );
    src.pResource = d->upload[s];
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = d->tex_fp;
    dst.pResource = d->tex;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ID3D12GraphicsCommandList_CopyTextureRegion( d->list, &dst, 0, 0, 0, &src, NULL );
    transition( d->list, d->tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );

    rtv.ptr += i * d->rtv_size;
    transition( d->list, d->targets[i], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET );
    ID3D12GraphicsCommandList_ClearRenderTargetView( d->list, rtv, clear, 0, NULL );
    ID3D12GraphicsCommandList_ClearDepthStencilView( d->list, dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL );
    ID3D12GraphicsCommandList_OMSetRenderTargets( d->list, 1, &rtv, FALSE, &dsv );
    ID3D12GraphicsCommandList_SetGraphicsRootSignature( d->list, d->root );
    ID3D12GraphicsCommandList_SetDescriptorHeaps( d->list, 1, &d->srv_heap );
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable( d->list, 1,
            ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart( d->srv_heap ) );
    view_projection( vp, 1, 0 );
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants( d->list, 0, 16, vp, 0 );
    ID3D12GraphicsCommandList_RSSetViewports( d->list, 1, &vpt );
    ID3D12GraphicsCommandList_RSSetScissorRects( d->list, 1, &scissor );
    ID3D12GraphicsCommandList_IASetPrimitiveTopology( d->list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    vbv[0].BufferLocation = ID3D12Resource_GetGPUVirtualAddress( d->vb );
    vbv[0].SizeInBytes = sizeof(cube);
    vbv[0].StrideInBytes = sizeof(struct vertex);
    vbv[1].BufferLocation = ID3D12Resource_GetGPUVirtualAddress( d->ib[s] );
    vbv[1].SizeInBytes = instances * sizeof(struct instance);
    vbv[1].StrideInBytes = sizeof(struct instance);
    ID3D12GraphicsCommandList_IASetVertexBuffers( d->list, 0, 2, vbv );
    for (b = 0; b < batches(); b++)
    {
        unsigned int first = b * batch, count = min( batch, instances - first );
        ID3D12GraphicsCommandList_DrawInstanced( d->list, 36, count, 0, first );
    }
}

static void d3d12_draw( void *ctx, float t, unsigned int frame )
{
    struct d3d12_ctx *d = ctx;
    UINT i = IDXGISwapChain3_GetCurrentBackBufferIndex( d->swapchain ), s = frame % FRAMES_IN_FLIGHT;

    d3d12_wait( d, d->frame_fence[s] );
    ID3D12CommandAllocator_Reset( d->allocator[s] );
    ID3D12GraphicsCommandList_Reset( d->list, d->allocator[s], d->pso );
    d3d12_record( d, i, s, t, frame );
    transition( d->list, d->targets[i], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT );
    ID3D12GraphicsCommandList_Close( d->list );
    ID3D12CommandQueue_ExecuteCommandLists( d->queue, 1, (ID3D12CommandList **)&d->list );
    IDXGISwapChain3_Present( d->swapchain, 0, 0 );
    ID3D12CommandQueue_Signal( d->queue, d->fence, ++d->fence_value );
    d->frame_fence[s] = d->fence_value;
}

static HRESULT d3d12_buffer( ID3D12Device *device, D3D12_HEAP_TYPE type, UINT64 size, D3D12_RESOURCE_STATES state,
                             ID3D12Resource **res, void **map )
{
    D3D12_HEAP_PROPERTIES heap = {0};
    D3D12_RESOURCE_DESC rd = {0};
    D3D12_RANGE range = { 0, 0 };
    HRESULT hr;

    heap.Type = type;
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = size;
    rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(hr = ID3D12Device_CreateCommittedResource( device, &heap, D3D12_HEAP_FLAG_NONE, &rd, state, NULL,
                                                          &IID_ID3D12Resource, (void **)res ))) return hr;
    if (map && (FAILED(hr = ID3D12Resource_Map( *res, 0, &range, map )) || !*map)) return FAILED(hr) ? hr : E_FAIL;
    return S_OK;
}

static void run_d3d12( struct result *res )
{
    static const D3D12_INPUT_ELEMENT_DESC layout[] =
        { LAYOUT_ELEMENTS( D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA ) };
    D3D12_COMMAND_QUEUE_DESC qd = {0};
    D3D12_DESCRIPTOR_HEAP_DESC hd = {0};
    D3D12_HEAP_PROPERTIES heap = {0};
    D3D12_RESOURCE_DESC rdesc = {0};
    D3D12_CLEAR_VALUE clear = {0};
    D3D12_ROOT_PARAMETER params[2] = {{0}};
    D3D12_DESCRIPTOR_RANGE srv_range = {0};
    D3D12_STATIC_SAMPLER_DESC sampler = {0};
    D3D12_ROOT_SIGNATURE_DESC rsd = {0};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {0};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    D3D12_TEXTURE_COPY_LOCATION src = {0}, dst = {0};
    D3D12_RANGE range = { 0, 0 };
    DXGI_SWAP_CHAIN_DESC1 sd = {0};
    IDXGIFactory4 *factory = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGISwapChain1 *sc1 = NULL;
    ID3DBlob *vs_blob = NULL, *ps_blob = NULL, *sig = NULL, *sig_error = NULL;
    ID3D12Resource *readback = NULL;
    struct d3d12_ctx d = {0};
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    UINT64 total, tex_total;
    double start = now();
    void *data;
    HRESULT hr;
    HWND hwnd;
    UINT i;

    hwnd = create_window( "d3d12" );
    if (FAILED(hr = CreateDXGIFactory1( &IID_IDXGIFactory4, (void **)&factory )) ||
        FAILED(hr = D3D12CreateDevice( NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&d.device )))
    {
        snprintf( res->error, sizeof(res->error), "D3D12 device: %#lx", hr );
        goto done;
    }
    if (SUCCEEDED(IDXGIFactory4_EnumAdapters( factory, 0, &adapter )))
        adapter_name( adapter, res->adapter, sizeof(res->adapter), ", vkd3d" );

    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12Device_CreateCommandQueue( d.device, &qd, &IID_ID3D12CommandQueue, (void **)&d.queue );
    for (i = 0; i < FRAMES_IN_FLIGHT; i++)
        ID3D12Device_CreateCommandAllocator( d.device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator,
                                             (void **)&d.allocator[i] );
    ID3D12Device_CreateFence( d.device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&d.fence );
    d.event = CreateEventW( NULL, FALSE, FALSE, NULL );

    sd.Width = width;
    sd.Height = height;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = FRAMES_IN_FLIGHT;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(hr = IDXGIFactory4_CreateSwapChainForHwnd( factory, (IUnknown *)d.queue, hwnd, &sd, NULL, NULL, &sc1 )) ||
        FAILED(hr = IDXGISwapChain1_QueryInterface( sc1, &IID_IDXGISwapChain3, (void **)&d.swapchain )))
    {
        snprintf( res->error, sizeof(res->error), "swap chain: %#lx", hr );
        goto done;
    }

    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = FRAMES_IN_FLIGHT;
    ID3D12Device_CreateDescriptorHeap( d.device, &hd, &IID_ID3D12DescriptorHeap, (void **)&d.rtv_heap );
    d.rtv_size = ID3D12Device_GetDescriptorHandleIncrementSize( d.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
    h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart( d.rtv_heap );
    for (i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        IDXGISwapChain3_GetBuffer( d.swapchain, i, &IID_ID3D12Resource, (void **)&d.targets[i] );
        ID3D12Device_CreateRenderTargetView( d.device, d.targets[i], NULL, h );
        h.ptr += d.rtv_size;
    }
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hd.NumDescriptors = 1;
    ID3D12Device_CreateDescriptorHeap( d.device, &hd, &IID_ID3D12DescriptorHeap, (void **)&d.dsv_heap );
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12Device_CreateDescriptorHeap( d.device, &hd, &IID_ID3D12DescriptorHeap, (void **)&d.srv_heap );

    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    rdesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rdesc.Width = width;
    rdesc.Height = height;
    rdesc.DepthOrArraySize = rdesc.MipLevels = 1;
    rdesc.Format = DXGI_FORMAT_D32_FLOAT;
    rdesc.SampleDesc.Count = 1;
    rdesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;
    if (FAILED(hr = ID3D12Device_CreateCommittedResource( d.device, &heap, D3D12_HEAP_FLAG_NONE, &rdesc,
                                                          D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                                                          &IID_ID3D12Resource, (void **)&d.depth ))) goto resource_error;
    ID3D12Device_CreateDepthStencilView( d.device, d.depth, NULL,
                                         ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart( d.dsv_heap ) );

    rdesc.Width = rdesc.Height = TEX_SIZE;
    rdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rdesc.Flags = 0;
    if (FAILED(hr = ID3D12Device_CreateCommittedResource( d.device, &heap, D3D12_HEAP_FLAG_NONE, &rdesc,
                                                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL,
                                                          &IID_ID3D12Resource, (void **)&d.tex ))) goto resource_error;
    ID3D12Device_GetCopyableFootprints( d.device, &rdesc, 0, 1, 0, &d.tex_fp, NULL, NULL, &tex_total );
    ID3D12Device_CreateShaderResourceView( d.device, d.tex, NULL,
                                           ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart( d.srv_heap ) );

    /* upload heaps: mapped memory (VK_EXT_external_memory_host under HVF) */
    if (FAILED(hr = d3d12_buffer( d.device, D3D12_HEAP_TYPE_UPLOAD, sizeof(cube), D3D12_RESOURCE_STATE_GENERIC_READ,
                                  &d.vb, &data ))) goto resource_error;
    memcpy( data, cube, sizeof(cube) );
    for (i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        if (FAILED(hr = d3d12_buffer( d.device, D3D12_HEAP_TYPE_UPLOAD, instances * sizeof(struct instance),
                                      D3D12_RESOURCE_STATE_GENERIC_READ, &d.ib[i], &d.ib_map[i] ))) goto resource_error;
        if (FAILED(hr = d3d12_buffer( d.device, D3D12_HEAP_TYPE_UPLOAD, tex_total, D3D12_RESOURCE_STATE_GENERIC_READ,
                                      &d.upload[i], &d.upload_map[i] ))) goto resource_error;
    }

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = 16;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srv_range.NumDescriptors = 1;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srv_range;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rsd.NumParameters = 2;
    rsd.pParameters = params;
    rsd.NumStaticSamplers = 1;
    rsd.pStaticSamplers = &sampler;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    if (FAILED(hr = D3D12SerializeRootSignature( &rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &sig_error )) ||
        FAILED(hr = ID3D12Device_CreateRootSignature( d.device, 0, ID3D10Blob_GetBufferPointer( sig ),
                                                      ID3D10Blob_GetBufferSize( sig ), &IID_ID3D12RootSignature,
                                                      (void **)&d.root )))
    {
        snprintf( res->error, sizeof(res->error), "root signature: %#lx", hr );
        goto done;
    }
    if (FAILED(compile( "vs", "vs_5_0", &vs_blob, res->error, sizeof(res->error) )) ||
        FAILED(compile( "ps", "ps_5_0", &ps_blob, res->error, sizeof(res->error) )))
        goto done;
    pd.pRootSignature = d.root;
    pd.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer( vs_blob );
    pd.VS.BytecodeLength = ID3D10Blob_GetBufferSize( vs_blob );
    pd.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer( ps_blob );
    pd.PS.BytecodeLength = ID3D10Blob_GetBufferSize( ps_blob );
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = ~0u;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.DepthStencilState.DepthEnable = TRUE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pd.InputLayout.pInputElementDescs = layout;
    pd.InputLayout.NumElements = ARRAY_SIZE(layout);
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pd.SampleDesc.Count = 1;
    if (FAILED(hr = ID3D12Device_CreateGraphicsPipelineState( d.device, &pd, &IID_ID3D12PipelineState, (void **)&d.pso )) ||
        FAILED(hr = ID3D12Device_CreateCommandList( d.device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, d.allocator[0], d.pso,
                                                    &IID_ID3D12GraphicsCommandList, (void **)&d.list )))
    {
        snprintf( res->error, sizeof(res->error), "pipeline state: %#lx", hr );
        goto done;
    }

    /* check frame into back buffer i, copied to a readback buffer */
    memset( &rdesc, 0, sizeof(rdesc) );
    rdesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rdesc.Width = width;
    rdesc.Height = height;
    rdesc.DepthOrArraySize = rdesc.MipLevels = 1;
    rdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rdesc.SampleDesc.Count = 1;
    ID3D12Device_GetCopyableFootprints( d.device, &rdesc, 0, 1, 0, &fp, NULL, NULL, &total );
    if (FAILED(hr = d3d12_buffer( d.device, D3D12_HEAP_TYPE_READBACK, total, D3D12_RESOURCE_STATE_COPY_DEST,
                                  &readback, NULL ))) goto resource_error;
    i = IDXGISwapChain3_GetCurrentBackBufferIndex( d.swapchain );
    d3d12_record( &d, i, 0, 0, 0 );
    transition( d.list, d.targets[i], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE );
    src.pResource = d.targets[i];
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = readback;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = fp;
    ID3D12GraphicsCommandList_CopyTextureRegion( d.list, &dst, 0, 0, 0, &src, NULL );
    transition( d.list, d.targets[i], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT );
    ID3D12GraphicsCommandList_Close( d.list );
    ID3D12CommandQueue_ExecuteCommandLists( d.queue, 1, (ID3D12CommandList **)&d.list );
    ID3D12CommandQueue_Signal( d.queue, d.fence, ++d.fence_value );
    d3d12_wait( &d, d.fence_value );
    range.End = total;
    if (FAILED(hr = ID3D12Resource_Map( readback, 0, &range, &data )) || !data)
    {
        snprintf( res->error, sizeof(res->error), "read back: %#lx", hr );
        goto done;
    }
    check_pixels( res, (BYTE *)data + fp.Offset, fp.Footprint.RowPitch );
    range.End = 0;
    ID3D12Resource_Unmap( readback, 0, &range );

    run_loop( res, d3d12_draw, &d, start, batches() );
    ID3D12CommandQueue_Signal( d.queue, d.fence, ++d.fence_value );
    d3d12_wait( &d, d.fence_value );
    goto done;

resource_error:
    snprintf( res->error, sizeof(res->error), "resource creation: %#lx", hr );
done:
    if (d.queue && d.fence) { ID3D12CommandQueue_Signal( d.queue, d.fence, ++d.fence_value ); d3d12_wait( &d, d.fence_value ); }
    if (readback) ID3D12Resource_Release( readback );
    if (d.list) ID3D12GraphicsCommandList_Release( d.list );
    if (d.pso) ID3D12PipelineState_Release( d.pso );
    if (d.root) ID3D12RootSignature_Release( d.root );
    if (sig) ID3D10Blob_Release( sig );
    if (sig_error) ID3D10Blob_Release( sig_error );
    if (ps_blob) ID3D10Blob_Release( ps_blob );
    if (vs_blob) ID3D10Blob_Release( vs_blob );
    for (i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        if (d.ib[i]) ID3D12Resource_Release( d.ib[i] );
        if (d.upload[i]) ID3D12Resource_Release( d.upload[i] );
        if (d.targets[i]) ID3D12Resource_Release( d.targets[i] );
        if (d.allocator[i]) ID3D12CommandAllocator_Release( d.allocator[i] );
    }
    if (d.tex) ID3D12Resource_Release( d.tex );
    if (d.vb) ID3D12Resource_Release( d.vb );
    if (d.depth) ID3D12Resource_Release( d.depth );
    if (d.srv_heap) ID3D12DescriptorHeap_Release( d.srv_heap );
    if (d.dsv_heap) ID3D12DescriptorHeap_Release( d.dsv_heap );
    if (d.rtv_heap) ID3D12DescriptorHeap_Release( d.rtv_heap );
    if (d.swapchain) IDXGISwapChain3_Release( d.swapchain );
    if (sc1) IDXGISwapChain1_Release( sc1 );
    if (d.fence) ID3D12Fence_Release( d.fence );
    if (d.event) CloseHandle( d.event );
    if (d.device) ID3D12Device_Release( d.device );
    if (adapter) IDXGIAdapter_Release( adapter );
    if (factory) IDXGIFactory4_Release( factory );
    DestroyWindow( hwnd );
}

/* ------------------------------------------------------------------------ */
/* Vulkan (MoltenVK)                                                         */

#define VK_FUNCS \
    X(vkCreateInstance)
#define VK_INSTANCE_FUNCS \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
    X(vkCreateWin32SurfaceKHR) X(vkDestroySurfaceKHR) X(vkCreateDevice) X(vkGetDeviceProcAddr)
#define VK_DEVICE_FUNCS \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) \
    X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR) X(vkQueuePresentKHR) X(vkQueueSubmit) \
    X(vkDeviceWaitIdle) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateImage) X(vkDestroyImage) \
    X(vkGetImageMemoryRequirements) X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkBindImageMemory) X(vkBindBufferMemory) X(vkMapMemory) \
    X(vkCreateRenderPass) X(vkDestroyRenderPass) X(vkCreateFramebuffer) X(vkDestroyFramebuffer) \
    X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
    X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) X(vkCreateDescriptorSetLayout) \
    X(vkDestroyDescriptorSetLayout) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) \
    X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) X(vkCreateSampler) X(vkDestroySampler) \
    X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) \
    X(vkEndCommandBuffer) X(vkResetCommandBuffer) X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) \
    X(vkCmdBindPipeline) X(vkCmdBindVertexBuffers) X(vkCmdBindDescriptorSets) X(vkCmdPushConstants) \
    X(vkCmdDraw) X(vkCmdPipelineBarrier) X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) \
    X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences) X(vkCreateSemaphore) \
    X(vkDestroySemaphore)

#define X(f) static PFN_##f p##f;
X(vkGetInstanceProcAddr) VK_FUNCS VK_INSTANCE_FUNCS VK_DEVICE_FUNCS
#undef X

struct vk_buffer { VkBuffer buf; VkDeviceMemory mem; void *map; };

struct vk_ctx
{
    VkInstance instance;
    VkPhysicalDevice phys;
    VkPhysicalDeviceMemoryProperties memprops;
    VkSurfaceKHR surface;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    VkSwapchainKHR swapchain;
    VkFormat format;
    uint32_t image_count;
    VkImage images[8];
    VkImageView views[8];
    VkFramebuffer framebuffers[8];
    VkImage depth, tex;
    VkDeviceMemory depth_mem, tex_mem;
    VkImageView depth_view, tex_view;
    VkSampler sampler;
    VkRenderPass pass;
    VkDescriptorSetLayout set_layout;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkCommandPool cmd_pool;
    VkCommandBuffer cmd[FRAMES_IN_FLIGHT];
    VkFence fence[FRAMES_IN_FLIGHT];
    VkSemaphore acquired[FRAMES_IN_FLIGHT], rendered[FRAMES_IN_FLIGHT];
    struct vk_buffer vb, ib[FRAMES_IN_FLIGHT], upload[FRAMES_IN_FLIGHT], readback;
    int tex_initialized;
};

static uint32_t vk_memory_type( struct vk_ctx *v, uint32_t bits, VkMemoryPropertyFlags flags )
{
    uint32_t i;
    for (i = 0; i < v->memprops.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (v->memprops.memoryTypes[i].propertyFlags & flags) == flags) return i;
    return ~0u;
}

static VkResult vk_create_buffer( struct vk_ctx *v, VkDeviceSize size, VkBufferUsageFlags usage, struct vk_buffer *b )
{
    VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkMemoryRequirements req;
    VkResult vr;

    bi.size = size;
    bi.usage = usage;
    if ((vr = pvkCreateBuffer( v->device, &bi, NULL, &b->buf ))) return vr;
    pvkGetBufferMemoryRequirements( v->device, b->buf, &req );
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = vk_memory_type( v, req.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
    if ((vr = pvkAllocateMemory( v->device, &ai, NULL, &b->mem ))) return vr;
    if ((vr = pvkBindBufferMemory( v->device, b->buf, b->mem, 0 ))) return vr;
    return pvkMapMemory( v->device, b->mem, 0, VK_WHOLE_SIZE, 0, &b->map );
}

static void vk_destroy_buffer( struct vk_ctx *v, struct vk_buffer *b )
{
    if (b->buf) pvkDestroyBuffer( v->device, b->buf, NULL );
    if (b->mem) pvkFreeMemory( v->device, b->mem, NULL );
}

static VkResult vk_create_image( struct vk_ctx *v, uint32_t w, uint32_t h, VkFormat format, VkImageUsageFlags usage,
                                 VkImageAspectFlags aspect, VkImage *image, VkDeviceMemory *mem, VkImageView *view )
{
    VkImageCreateInfo ii = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkMemoryRequirements req;
    VkResult vr;

    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent.width = w;
    ii.extent.height = h;
    ii.extent.depth = 1;
    ii.mipLevels = ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    if ((vr = pvkCreateImage( v->device, &ii, NULL, image ))) return vr;
    pvkGetImageMemoryRequirements( v->device, *image, &req );
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = vk_memory_type( v, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
    if ((vr = pvkAllocateMemory( v->device, &ai, NULL, mem ))) return vr;
    if ((vr = pvkBindImageMemory( v->device, *image, *mem, 0 ))) return vr;
    vi.image = *image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange.aspectMask = aspect;
    vi.subresourceRange.levelCount = vi.subresourceRange.layerCount = 1;
    return pvkCreateImageView( v->device, &vi, NULL, view );
}

static void vk_barrier( VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                        VkAccessFlags src_access, VkAccessFlags dst_access,
                        VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage )
{
    VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.srcAccessMask = src_access;
    b.dstAccessMask = dst_access;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = b.subresourceRange.layerCount = 1;
    pvkCmdPipelineBarrier( cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &b );
}

/* record frame slot s into image i; with readback, copy the image to v->readback.buf */
static void vk_record( struct vk_ctx *v, uint32_t s, uint32_t i, float t, unsigned int frame, int readback )
{
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VkRenderPassBeginInfo rp = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    VkClearValue clears[2];
    VkBufferImageCopy copy = {0};
    VkBuffer bufs[2];
    VkDeviceSize offs[2] = { 0, 0 };
    VkCommandBuffer cmd = v->cmd[s];
    float vp[16];
    unsigned int b;

    update_instances( t );
    update_texture( frame );
    memcpy( v->ib[s].map, inst, instances * sizeof(*inst) );
    memcpy( v->upload[s].map, texels, TEX_SIZE * TEX_SIZE * 4 );

    pvkResetCommandBuffer( cmd, 0 );
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    pvkBeginCommandBuffer( cmd, &bi );
    vk_barrier( cmd, v->tex, v->tex_initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT );
    v->tex_initialized = 1;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = copy.imageExtent.height = TEX_SIZE;
    copy.imageExtent.depth = 1;
    pvkCmdCopyBufferToImage( cmd, v->upload[s].buf, v->tex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy );
    vk_barrier( cmd, v->tex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT );

    clears[0].color.float32[0] = clears[0].color.float32[1] = clears[0].color.float32[2] = 0.05f;
    clears[0].color.float32[3] = 1;
    clears[1].depthStencil.depth = 1;
    clears[1].depthStencil.stencil = 0;
    rp.renderPass = v->pass;
    rp.framebuffer = v->framebuffers[i];
    rp.renderArea.extent.width = width;
    rp.renderArea.extent.height = height;
    rp.clearValueCount = 2;
    rp.pClearValues = clears;
    pvkCmdBeginRenderPass( cmd, &rp, VK_SUBPASS_CONTENTS_INLINE );
    pvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, v->pipeline );
    pvkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, v->layout, 0, 1, &v->set, 0, NULL );
    view_projection( vp, 1, 1 );
    pvkCmdPushConstants( cmd, v->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vp), vp );
    bufs[0] = v->vb.buf;
    bufs[1] = v->ib[s].buf;
    pvkCmdBindVertexBuffers( cmd, 0, 2, bufs, offs );
    for (b = 0; b < batches(); b++)
    {
        unsigned int first = b * batch, count = min( batch, instances - first );
        pvkCmdDraw( cmd, 36, count, 0, first );
    }
    pvkCmdEndRenderPass( cmd );
    if (readback)
    {
        vk_barrier( cmd, v->images[i], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT );
        copy.imageExtent.width = width;
        copy.imageExtent.height = height;
        pvkCmdCopyImageToBuffer( cmd, v->images[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, v->readback.buf, 1, &copy );
        vk_barrier( cmd, v->images[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_ACCESS_TRANSFER_READ_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT );
    }
    pvkEndCommandBuffer( cmd );
}

/* one frame; returns the image presented (readback: also waits for it) */
static VkResult vk_frame( struct vk_ctx *v, float t, unsigned int frame, int readback )
{
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    uint32_t s = frame % FRAMES_IN_FLIGHT, i;
    VkResult vr;

    pvkWaitForFences( v->device, 1, &v->fence[s], VK_TRUE, UINT64_MAX );
    if ((vr = pvkAcquireNextImageKHR( v->device, v->swapchain, UINT64_MAX, v->acquired[s], VK_NULL_HANDLE, &i )) < 0)
        return vr;
    pvkResetFences( v->device, 1, &v->fence[s] );
    vk_record( v, s, i, t, frame, readback );
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &v->acquired[s];
    si.pWaitDstStageMask = &stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &v->cmd[s];
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &v->rendered[s];
    if ((vr = pvkQueueSubmit( v->queue, 1, &si, v->fence[s] ))) return vr;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &v->rendered[s];
    pi.swapchainCount = 1;
    pi.pSwapchains = &v->swapchain;
    pi.pImageIndices = &i;
    vr = pvkQueuePresentKHR( v->queue, &pi );
    if (readback) pvkWaitForFences( v->device, 1, &v->fence[s], VK_TRUE, UINT64_MAX );
    return vr < 0 ? vr : VK_SUCCESS;
}

static void vk_draw( void *ctx, float t, unsigned int frame )
{
    vk_frame( ctx, t, frame, 0 );
}

static VkResult vk_setup( struct vk_ctx *v, HWND hwnd, struct result *res )
{
    static const char *inst_exts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
    static const char *dev_exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    static const float priority = 1;
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    VkWin32SurfaceCreateInfoKHR sci = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    VkSwapchainCreateInfoKHR swci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    VkAttachmentDescription att[2] = {{0}};
    VkAttachmentReference color_ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depth_ref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = {0};
    VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    VkFramebufferCreateInfo fci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    VkSamplerCreateInfo smci = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    VkDescriptorSetLayoutBinding binding = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT };
    VkDescriptorSetLayoutCreateInfo dslci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
    VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    VkDescriptorImageInfo dii;
    VkWriteDescriptorSet wds = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    VkPushConstantRange pcr = { VK_SHADER_STAGE_VERTEX_BIT, 0, 64 };
    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkShaderModuleCreateInfo smi = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    VkPipelineShaderStageCreateInfo stages[2] = {{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO },
                                                 { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO }};
    VkVertexInputBindingDescription vbind[2] = { { 0, sizeof(struct vertex), VK_VERTEX_INPUT_RATE_VERTEX },
                                                 { 1, sizeof(struct instance), VK_VERTEX_INPUT_RATE_INSTANCE } };
    VkVertexInputAttributeDescription vattr[8] = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 }, { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12 },
        { 2, 0, VK_FORMAT_R32G32_SFLOAT, 24 }, { 3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
        { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16 }, { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32 },
        { 6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48 }, { 7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 64 } };
    VkPipelineVertexInputStateCreateInfo vis = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ias = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    VkViewport viewport = { 0, 0, (float)width, (float)height, 0, 1 };
    VkRect2D scissor = { { 0, 0 }, { width, height } };
    VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    VkPipelineColorBlendAttachmentState cba = {0};
    VkPipelineColorBlendStateCreateInfo cbs = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    VkGraphicsPipelineCreateInfo gpci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    VkFenceCreateInfo fnci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkSemaphoreCreateInfo semci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkSurfaceCapabilitiesKHR caps;
    VkSurfaceFormatKHR formats[32];
    VkPresentModeKHR modes[8], mode = VK_PRESENT_MODE_FIFO_KHR;
    VkQueueFamilyProperties families[16];
    VkPhysicalDeviceProperties props;
    VkPhysicalDevice physs[8];
    uint32_t count, i;
    HMODULE vulkan;
    VkResult vr;

#define CHECK(call) do { if ((vr = (call)) < 0) { snprintf( res->error, sizeof(res->error), "%s: %d", #call, vr ); return vr; } } while (0)
    if (!(vulkan = LoadLibraryA( "vulkan-1.dll" )) ||
        !(pvkGetInstanceProcAddr = (void *)GetProcAddress( vulkan, "vkGetInstanceProcAddr" )))
    {
        snprintf( res->error, sizeof(res->error), "vulkan-1.dll not available" );
        return VK_ERROR_INITIALIZATION_FAILED;
    }
#define X(f) p##f = (PFN_##f)pvkGetInstanceProcAddr( NULL, #f );
    VK_FUNCS
#undef X
    app.pApplicationName = "stress3d";
    app.apiVersion = VK_API_VERSION_1_1;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = ARRAY_SIZE(inst_exts);
    ici.ppEnabledExtensionNames = inst_exts;
    CHECK( pvkCreateInstance( &ici, NULL, &v->instance ) );
#define X(f) p##f = (PFN_##f)pvkGetInstanceProcAddr( v->instance, #f );
    VK_INSTANCE_FUNCS
#undef X
    count = ARRAY_SIZE(physs);
    CHECK( pvkEnumeratePhysicalDevices( v->instance, &count, physs ) );
    if (!count) { snprintf( res->error, sizeof(res->error), "no Vulkan device" ); return VK_ERROR_INITIALIZATION_FAILED; }
    v->phys = physs[0];
    pvkGetPhysicalDeviceProperties( v->phys, &props );
    snprintf( res->adapter, sizeof(res->adapter), "%s, Vulkan %u.%u.%u", props.deviceName, VK_VERSION_MAJOR( props.apiVersion ),
              VK_VERSION_MINOR( props.apiVersion ), VK_VERSION_PATCH( props.apiVersion ) );
    pvkGetPhysicalDeviceMemoryProperties( v->phys, &v->memprops );
    sci.hinstance = GetModuleHandleW( NULL );
    sci.hwnd = hwnd;
    CHECK( pvkCreateWin32SurfaceKHR( v->instance, &sci, NULL, &v->surface ) );
    count = ARRAY_SIZE(families);
    pvkGetPhysicalDeviceQueueFamilyProperties( v->phys, &count, families );
    for (v->family = 0; v->family < count; v->family++)
    {
        VkBool32 present = VK_FALSE;
        pvkGetPhysicalDeviceSurfaceSupportKHR( v->phys, v->family, v->surface, &present );
        if ((families[v->family].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) break;
    }
    if (v->family == count) { snprintf( res->error, sizeof(res->error), "no graphics+present queue" ); return VK_ERROR_INITIALIZATION_FAILED; }
    qci.queueFamilyIndex = v->family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = ARRAY_SIZE(dev_exts);
    dci.ppEnabledExtensionNames = dev_exts;
    CHECK( pvkCreateDevice( v->phys, &dci, NULL, &v->device ) );
#define X(f) p##f = (PFN_##f)pvkGetDeviceProcAddr( v->device, #f );
    VK_DEVICE_FUNCS
#undef X
    pvkGetDeviceQueue( v->device, v->family, 0, &v->queue );

    /* swapchain: BGRA or RGBA 8-bit, immediate present if there is one */
    CHECK( pvkGetPhysicalDeviceSurfaceCapabilitiesKHR( v->phys, v->surface, &caps ) );
    if (caps.currentExtent.width != ~0u && caps.currentExtent.width &&
        ((int)caps.currentExtent.width != width || (int)caps.currentExtent.height != height))
    {
        /* the surface decides (Retina scaling): render and check at its size */
        width = caps.currentExtent.width;
        height = caps.currentExtent.height;
        viewport.width = (float)width;
        viewport.height = (float)height;
        scissor.extent = caps.currentExtent;
    }
    count = ARRAY_SIZE(formats);
    pvkGetPhysicalDeviceSurfaceFormatsKHR( v->phys, v->surface, &count, formats );
    v->format = formats[0].format;
    for (i = 0; i < count; i++)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM || formats[i].format == VK_FORMAT_R8G8B8A8_UNORM)
        { v->format = formats[i].format; break; }
    count = ARRAY_SIZE(modes);
    pvkGetPhysicalDeviceSurfacePresentModesKHR( v->phys, v->surface, &count, modes );
    for (i = 0; i < count; i++) if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) mode = modes[i];
    swci.surface = v->surface;
    swci.minImageCount = max( caps.minImageCount, 2 );
    if (caps.maxImageCount && swci.minImageCount > caps.maxImageCount) swci.minImageCount = caps.maxImageCount;
    swci.imageFormat = v->format;
    swci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swci.imageExtent.width = width;
    swci.imageExtent.height = height;
    swci.imageArrayLayers = 1;
    swci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    swci.preTransform = caps.currentTransform;
    swci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swci.presentMode = mode;
    swci.clipped = VK_TRUE;
    CHECK( pvkCreateSwapchainKHR( v->device, &swci, NULL, &v->swapchain ) );
    v->image_count = ARRAY_SIZE(v->images);
    CHECK( pvkGetSwapchainImagesKHR( v->device, v->swapchain, &v->image_count, v->images ) );

    att[0].format = v->format;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    att[1] = att[0];
    att[1].format = VK_FORMAT_D32_SFLOAT;
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &color_ref;
    sub.pDepthStencilAttachment = &depth_ref;
    rpci.attachmentCount = 2;
    rpci.pAttachments = att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    CHECK( pvkCreateRenderPass( v->device, &rpci, NULL, &v->pass ) );

    CHECK( vk_create_image( v, width, height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                            VK_IMAGE_ASPECT_DEPTH_BIT, &v->depth, &v->depth_mem, &v->depth_view ) );
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = v->format;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = vi.subresourceRange.layerCount = 1;
    for (i = 0; i < v->image_count; i++)
    {
        VkImageView views[2];
        vi.image = v->images[i];
        CHECK( pvkCreateImageView( v->device, &vi, NULL, &v->views[i] ) );
        views[0] = v->views[i];
        views[1] = v->depth_view;
        fci.renderPass = v->pass;
        fci.attachmentCount = 2;
        fci.pAttachments = views;
        fci.width = width;
        fci.height = height;
        fci.layers = 1;
        CHECK( pvkCreateFramebuffer( v->device, &fci, NULL, &v->framebuffers[i] ) );
    }

    CHECK( vk_create_image( v, TEX_SIZE, TEX_SIZE, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                            &v->tex, &v->tex_mem, &v->tex_view ) );
    smci.magFilter = smci.minFilter = VK_FILTER_LINEAR;
    smci.addressModeU = smci.addressModeV = smci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    smci.maxLod = 1;
    CHECK( pvkCreateSampler( v->device, &smci, NULL, &v->sampler ) );
    dslci.bindingCount = 1;
    dslci.pBindings = &binding;
    CHECK( pvkCreateDescriptorSetLayout( v->device, &dslci, NULL, &v->set_layout ) );
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &pool_size;
    CHECK( pvkCreateDescriptorPool( v->device, &dpci, NULL, &v->pool ) );
    dsai.descriptorPool = v->pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &v->set_layout;
    CHECK( pvkAllocateDescriptorSets( v->device, &dsai, &v->set ) );
    dii.sampler = v->sampler;
    dii.imageView = v->tex_view;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    wds.dstSet = v->set;
    wds.descriptorCount = 1;
    wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wds.pImageInfo = &dii;
    pvkUpdateDescriptorSets( v->device, 1, &wds, 0, NULL );
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &v->set_layout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    CHECK( pvkCreatePipelineLayout( v->device, &plci, NULL, &v->layout ) );

    smi.codeSize = sizeof(stress_vert_spv);
    smi.pCode = stress_vert_spv;
    CHECK( pvkCreateShaderModule( v->device, &smi, NULL, &vs ) );
    smi.codeSize = sizeof(stress_frag_spv);
    smi.pCode = stress_frag_spv;
    CHECK( pvkCreateShaderModule( v->device, &smi, NULL, &fs ) );
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";
    vis.vertexBindingDescriptionCount = 2;
    vis.pVertexBindingDescriptions = vbind;
    vis.vertexAttributeDescriptionCount = 8;
    vis.pVertexAttributeDescriptions = vattr;
    ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    vps.viewportCount = vps.scissorCount = 1;
    vps.pViewports = &viewport;
    vps.pScissors = &scissor;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    ds.depthTestEnable = ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    cba.colorWriteMask = 0xf;
    cbs.attachmentCount = 1;
    cbs.pAttachments = &cba;
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vis;
    gpci.pInputAssemblyState = &ias;
    gpci.pViewportState = &vps;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pDepthStencilState = &ds;
    gpci.pColorBlendState = &cbs;
    gpci.layout = v->layout;
    gpci.renderPass = v->pass;
    vr = pvkCreateGraphicsPipelines( v->device, VK_NULL_HANDLE, 1, &gpci, NULL, &v->pipeline );
    pvkDestroyShaderModule( v->device, vs, NULL );
    pvkDestroyShaderModule( v->device, fs, NULL );
    if (vr < 0) { snprintf( res->error, sizeof(res->error), "vkCreateGraphicsPipelines: %d", vr ); return vr; }

    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = v->family;
    CHECK( pvkCreateCommandPool( v->device, &cpci, NULL, &v->cmd_pool ) );
    cbai.commandPool = v->cmd_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = FRAMES_IN_FLIGHT;
    CHECK( pvkAllocateCommandBuffers( v->device, &cbai, v->cmd ) );
    fnci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    CHECK( vk_create_buffer( v, sizeof(cube), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &v->vb ) );
    memcpy( v->vb.map, cube, sizeof(cube) );
    for (i = 0; i < FRAMES_IN_FLIGHT; i++)
    {
        CHECK( pvkCreateFence( v->device, &fnci, NULL, &v->fence[i] ) );
        CHECK( pvkCreateSemaphore( v->device, &semci, NULL, &v->acquired[i] ) );
        CHECK( pvkCreateSemaphore( v->device, &semci, NULL, &v->rendered[i] ) );
        CHECK( vk_create_buffer( v, instances * sizeof(struct instance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &v->ib[i] ) );
        CHECK( vk_create_buffer( v, TEX_SIZE * TEX_SIZE * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &v->upload[i] ) );
    }
    CHECK( vk_create_buffer( v, (VkDeviceSize)width * height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &v->readback ) );
#undef CHECK
    return VK_SUCCESS;
}

static void vk_teardown( struct vk_ctx *v )
{
    uint32_t i;

    if (v->device)
    {
        pvkDeviceWaitIdle( v->device );
        for (i = 0; i < FRAMES_IN_FLIGHT; i++)
        {
            if (v->fence[i]) pvkDestroyFence( v->device, v->fence[i], NULL );
            if (v->acquired[i]) pvkDestroySemaphore( v->device, v->acquired[i], NULL );
            if (v->rendered[i]) pvkDestroySemaphore( v->device, v->rendered[i], NULL );
            vk_destroy_buffer( v, &v->ib[i] );
            vk_destroy_buffer( v, &v->upload[i] );
        }
        vk_destroy_buffer( v, &v->vb );
        vk_destroy_buffer( v, &v->readback );
        if (v->cmd_pool) pvkDestroyCommandPool( v->device, v->cmd_pool, NULL );
        if (v->pipeline) pvkDestroyPipeline( v->device, v->pipeline, NULL );
        if (v->layout) pvkDestroyPipelineLayout( v->device, v->layout, NULL );
        if (v->pool) pvkDestroyDescriptorPool( v->device, v->pool, NULL );
        if (v->set_layout) pvkDestroyDescriptorSetLayout( v->device, v->set_layout, NULL );
        if (v->sampler) pvkDestroySampler( v->device, v->sampler, NULL );
        if (v->tex_view) pvkDestroyImageView( v->device, v->tex_view, NULL );
        if (v->tex) pvkDestroyImage( v->device, v->tex, NULL );
        if (v->tex_mem) pvkFreeMemory( v->device, v->tex_mem, NULL );
        for (i = 0; i < v->image_count; i++)
        {
            if (v->framebuffers[i]) pvkDestroyFramebuffer( v->device, v->framebuffers[i], NULL );
            if (v->views[i]) pvkDestroyImageView( v->device, v->views[i], NULL );
        }
        if (v->depth_view) pvkDestroyImageView( v->device, v->depth_view, NULL );
        if (v->depth) pvkDestroyImage( v->device, v->depth, NULL );
        if (v->depth_mem) pvkFreeMemory( v->device, v->depth_mem, NULL );
        if (v->pass) pvkDestroyRenderPass( v->device, v->pass, NULL );
        if (v->swapchain) pvkDestroySwapchainKHR( v->device, v->swapchain, NULL );
        pvkDestroyDevice( v->device, NULL );
    }
    if (v->surface) pvkDestroySurfaceKHR( v->instance, v->surface, NULL );
    if (v->instance) pvkDestroyInstance( v->instance, NULL );
}

static void run_vulkan( struct result *res )
{
    struct vk_ctx v = {0};
    double start = now();
    VkResult vr;
    HWND hwnd;

    hwnd = create_window( "vulkan" );
    if (vk_setup( &v, hwnd, res ) == VK_SUCCESS)
    {
        if ((vr = vk_frame( &v, 0, 0, 1 )) != VK_SUCCESS)
            snprintf( res->error, sizeof(res->error), "check frame: %d", vr );
        else
        {
            BYTE *p = v.readback.map;
            if (v.format == VK_FORMAT_B8G8R8A8_UNORM) { /* coverage check reads any channel: order does not matter */ }
            check_pixels( res, p, width * 4 );
            run_loop( res, vk_draw, &v, start, batches() );
        }
    }
    vk_teardown( &v );
    DestroyWindow( hwnd );
}

/* ------------------------------------------------------------------------ */

static const struct api
{
    const char *name;
    void (*run)( struct result *res );
}
apis[] =
{
    { "gl", run_gl },
    { "d3d9", run_d3d9 },
    { "d3d11", run_d3d11 },
    { "d3d12", run_d3d12 },
    { "vulkan", run_vulkan },
};

static void json_string( FILE *f, const char *s )
{
    fputc( '"', f );
    for (; *s; s++)
    {
        if (*s == '"' || *s == '\\') fprintf( f, "\\%c", *s );
        else if ((unsigned char)*s < 0x20) fprintf( f, "\\u%04x", *s );
        else fputc( *s, f );
    }
    fputc( '"', f );
}

static void write_json( FILE *f, const struct result *r )
{
    fprintf( f, "{\"api\": " ); json_string( f, r->api );
    fprintf( f, ", \"arch\": \"%s\", \"ok\": %s, \"error\": ",
#if defined(__aarch64__) || defined(__arm64ec__)
             "arm64",
#elif defined(__x86_64__)
             "x86_64",
#else
             "i386",
#endif
             r->ok ? "true" : "false" );
    json_string( f, r->error );
    fprintf( f, ", \"backend\": " );
    if (!strcmp( r->api, "d3d11" ))
    {
        const char *b = sizeof(void *) == 4 ? "wined3d" : getenv( "BARRIQUE_D3D" );
        json_string( f, b && *b ? b : "wined3d" );
    }
    else json_string( f, !strcmp( r->api, "d3d9" ) ? "wined3d" : !strcmp( r->api, "d3d12" ) ? "vkd3d" :
                         !strcmp( r->api, "vulkan" ) ? "moltenvk" : "apple-gl" );
    fprintf( f, ", \"adapter\": " ); json_string( f, r->adapter );
    fprintf( f, ", \"width\": %d, \"height\": %d, \"instances\": %u, \"draws_per_frame\": %u, \"seconds\": %.1f, "
                "\"frames\": %u, \"fps\": %.2f, \"frame_ms\": {\"avg\": %.3f, \"p50\": %.3f, \"p95\": %.3f, "
                "\"p99\": %.3f, \"max\": %.3f}, \"first_frame_ms\": %.1f, \"draws_per_s\": %.0f, "
                "\"mtris_per_s\": %.2f, \"upload_mb_s\": %.1f, \"cpu_user_s\": %.2f, \"cpu_kernel_s\": %.2f, "
                "\"cpu_use\": %.2f, \"peak_ws_mb\": %.1f, \"coverage_pct\": %.2f}\n",
             width, height, instances, r->draws_per_frame, seconds, r->frames, r->fps, r->avg_ms, r->p50_ms,
             r->p95_ms, r->p99_ms, r->max_ms, r->first_ms, r->draws_per_s, r->mtris_per_s, r->upload_mb_s,
             r->cpu_user, r->cpu_kernel, r->cpu_use, r->peak_ws_mb, r->coverage );
}

int main( int argc, char **argv )
{
    const char *which = "all", *json_path = NULL;
    struct result results[ARRAY_SIZE(apis)] = {{0}};
    unsigned int i, ran = 0, ok = 0, pos = 0;
    FILE *json = NULL;
    int a;

    for (a = 1; a < argc; a++)
    {
        if (!strcmp( argv[a], "--json" ) && a + 1 < argc) json_path = argv[++a];
        else if (!strcmp( argv[a], "--batch" ) && a + 1 < argc) batch = atoi( argv[++a] );
        else if (!strcmp( argv[a], "--size" ) && a + 1 < argc) sscanf( argv[++a], "%dx%d", &width, &height );
        else if (pos == 0) { which = argv[a]; pos++; }
        else if (pos == 1) { seconds = atof( argv[a] ); pos++; }
        else if (pos == 2) { instances = atoi( argv[a] ); pos++; }
    }
    if (seconds <= 0 || width < 64 || height < 64 || !instances || !batch)
    {
        printf( "usage: stress3d.exe [gl|d3d9|d3d11|d3d12|vulkan|all] [seconds] [instances] [--batch n] [--size WxH] [--json file]\n" );
        return 255;
    }
    if (json_path && !(json = fopen( json_path, "a" )))
    {
        printf( "cannot open %s\n", json_path );
        return 255;
    }
    SetProcessDPIAware();
    for (grid = 1; grid * grid * grid < instances; grid++) ;
    inst = calloc( instances, sizeof(*inst) );
    texels = malloc( TEX_SIZE * TEX_SIZE * 4 );
    make_cube();
    update_texture( 0 );
    printf( "stress3d: %u cubes (%u triangles), batch %u, texture %ux%u, %dx%d, %.1f s per API, %s build\n",
            instances, instances * 12, batch, TEX_SIZE, TEX_SIZE, width, height, seconds,
            sizeof(void *) == 8 ? "64-bit" : "32-bit" );
    fflush( stdout );

    for (i = 0; i < ARRAY_SIZE(apis); i++)
    {
        struct result *res = &results[i];
        if (strcmp( which, "all" ) && strcmp( which, apis[i].name )) continue;
        res->api = apis[i].name;
        apis[i].run( res );
        ran++;
        if (res->ok) ok++;
        if (res->frames)
            printf( "%-6s %s fps %8.1f  ms avg %6.2f p99 %6.2f  draws/s %9.0f  Mtri/s %7.1f  MB/s %7.1f  "
                    "cpu %.2f  ws %4.0f MB  cover %4.1f%%  first %5.0f ms  %s%s%s\n",
                    res->api, res->ok ? "ok  " : "FAIL", res->fps, res->avg_ms, res->p99_ms, res->draws_per_s,
                    res->mtris_per_s, res->upload_mb_s, res->cpu_use, res->peak_ws_mb, res->coverage, res->first_ms,
                    res->adapter, res->ok ? "" : "  ", res->ok ? "" : res->error );
        else
            printf( "%-6s FAIL %s  %s\n", res->api, res->error, res->adapter );
        fflush( stdout );
        if (json) { write_json( json, res ); fflush( json ); }
    }
    if (json) fclose( json );
    if (!ran)
    {
        printf( "unknown API %s\n", which );
        return 255;
    }
    printf( "RESULT stress3d %s %u/%u\n", ok == ran ? "ok" : "FAILED", ok, ran );
    return ran - ok;
}
