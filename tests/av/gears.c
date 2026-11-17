/*
 * Barrique: gears, a small graphics benchmark and compatibility check in the
 * spirit of glxgears. The three gears (own geometry, same layout as glxgears)
 * are drawn with each API in its own window:
 *
 *   gl     OpenGL 1.5 fixed function, vertex buffer object (on macOS: Apple's
 *          GL through winemac and wined3d-free opengl32)
 *   d3d9   Direct3D 9 fixed function (wined3d)
 *   d3d11  Direct3D 11 with HLSL shaders (wined3d)
 *   d3d12  Direct3D 12 with HLSL shaders (vkd3d over Vulkan / MoltenVK)
 *
 *   gears.exe [api|all] [seconds] [width height]     (default: all 5 640 480)
 *
 * For each API it prints the adapter or renderer, the time to the first
 * frame, the frames per second with vsync off, and a compatibility check: a
 * frame at a fixed angle is read back and must show the red, green and blue
 * gears (each at least 0.3% of the pixels) on the background. The blue gear
 * sits more than 64 KB into the OpenGL vertex buffer, so a pointer offset
 * that a WoW64 layer mistakes for an address shows up as a missing gear.
 *
 *   gl     ok   fps 1234.5  frames 6172  first 85 ms  red 4.1% green 2.0% blue 2.2%  renderer ...
 *   RESULT gears ok 4/4
 *
 * The exit code is the number of APIs that failed.
 */
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#define INITGUID  /* define the IIDs here: llvm-mingw has no import library with the D3D12 ones */
#define _USE_MATH_DEFINES
#include <windows.h>
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

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static int width = 640, height = 480;
static double seconds = 5.0;

/* ------------------------------------------------------------------------ */
/* geometry                                                                  */

struct vertex { float x, y, z, nx, ny, nz; };

static struct vertex *verts;
static unsigned int vert_count, vert_max;

static void add_vertex( float x, float y, float z, float nx, float ny, float nz )
{
    if (vert_count == vert_max)
    {
        vert_max = vert_max ? vert_max * 2 : 4096;
        verts = realloc( verts, vert_max * sizeof(*verts) );
    }
    verts[vert_count++] = (struct vertex){ x, y, z, nx, ny, nz };
}

/* two triangles a b c, a c d with one normal */
static void add_quad( const float *a, const float *b, const float *c, const float *d, float nx, float ny, float nz )
{
    add_vertex( a[0], a[1], a[2], nx, ny, nz );
    add_vertex( b[0], b[1], b[2], nx, ny, nz );
    add_vertex( c[0], c[1], c[2], nx, ny, nz );
    add_vertex( a[0], a[1], a[2], nx, ny, nz );
    add_vertex( c[0], c[1], c[2], nx, ny, nz );
    add_vertex( d[0], d[1], d[2], nx, ny, nz );
}

static void polar( float *p, float r, float a, float z )
{
    p[0] = r * cosf( a );
    p[1] = r * sinf( a );
    p[2] = z;
}

/* a gear in the z = 0 plane centred on the origin, as triangles */
static void make_gear( float inner, float outer, float thickness, int teeth, float depth )
{
    float r0 = inner, r1 = outer - depth / 2, r2 = outer + depth / 2, hz = thickness / 2;
    float da = 2 * (float)M_PI / teeth / 4;
    int i, k;

    for (i = 0; i < teeth; i++)
    {
        float a = i * 2 * (float)M_PI / teeth, t[5], outline[5][2];
        float p[4][3], q[4][3];

        for (k = 0; k < 5; k++) t[k] = a + k * da;

        for (k = 0; k < 4; k++)  /* front and back rings, inner cylinder */
        {
            polar( p[0], r0, t[k], hz ); polar( p[1], r1, t[k], hz );
            polar( p[2], r1, t[k + 1], hz ); polar( p[3], r0, t[k + 1], hz );
            add_quad( p[0], p[1], p[2], p[3], 0, 0, 1 );
            polar( q[0], r0, t[k], -hz ); polar( q[1], r0, t[k + 1], -hz );
            polar( q[2], r1, t[k + 1], -hz ); polar( q[3], r1, t[k], -hz );
            add_quad( q[0], q[1], q[2], q[3], 0, 0, -1 );
            polar( p[0], r0, t[k], hz ); polar( p[1], r0, t[k + 1], hz );
            polar( p[2], r0, t[k + 1], -hz ); polar( p[3], r0, t[k], -hz );
            add_quad( p[0], p[1], p[2], p[3], -cosf( t[k] + da / 2 ), -sinf( t[k] + da / 2 ), 0 );
        }

        /* tooth faces */
        polar( p[0], r1, t[0], hz ); polar( p[1], r2, t[1], hz );
        polar( p[2], r2, t[2], hz ); polar( p[3], r1, t[3], hz );
        add_quad( p[0], p[1], p[2], p[3], 0, 0, 1 );
        polar( q[0], r1, t[0], -hz ); polar( q[1], r1, t[3], -hz );
        polar( q[2], r2, t[2], -hz ); polar( q[3], r2, t[1], -hz );
        add_quad( q[0], q[1], q[2], q[3], 0, 0, -1 );

        /* outward faces along the tooth outline */
        outline[0][0] = r1; outline[0][1] = t[0];
        outline[1][0] = r2; outline[1][1] = t[1];
        outline[2][0] = r2; outline[2][1] = t[2];
        outline[3][0] = r1; outline[3][1] = t[3];
        outline[4][0] = r1; outline[4][1] = t[4];
        for (k = 0; k < 4; k++)
        {
            float ex, ey, len;
            polar( p[0], outline[k][0], outline[k][1], hz );
            polar( p[1], outline[k + 1][0], outline[k + 1][1], hz );
            polar( p[2], outline[k + 1][0], outline[k + 1][1], -hz );
            polar( p[3], outline[k][0], outline[k][1], -hz );
            ex = p[1][0] - p[0][0];
            ey = p[1][1] - p[0][1];
            len = sqrtf( ex * ex + ey * ey );
            if (len < 1e-6f) continue;
            add_quad( p[0], p[1], p[2], p[3], ey / len, -ex / len, 0 );
        }
    }
}

static const struct gear_desc
{
    float inner, outer, thickness, depth, x, y, speed, phase;
    int teeth;
    float color[4];
}
gear_descs[3] =
{
    { 1.0f, 4.0f, 1.0f, 0.7f, -3.0f, -2.0f,  1.0f,   0.0f, 20, { 0.8f, 0.1f, 0.0f, 1.0f } },
    { 0.5f, 2.0f, 2.0f, 0.7f,  3.1f, -2.0f, -2.0f,  -9.0f, 10, { 0.0f, 0.8f, 0.2f, 1.0f } },
    { 1.3f, 2.0f, 0.5f, 0.7f, -3.1f,  4.2f, -2.0f, -25.0f, 10, { 0.2f, 0.2f, 1.0f, 1.0f } },
};

static unsigned int gear_first[3], gear_count[3];

static void make_gears(void)
{
    int i;
    for (i = 0; i < 3; i++)
    {
        gear_first[i] = vert_count;
        make_gear( gear_descs[i].inner, gear_descs[i].outer, gear_descs[i].thickness,
                   gear_descs[i].teeth, gear_descs[i].depth );
        gear_count[i] = vert_count - gear_first[i];
    }
}

/* ------------------------------------------------------------------------ */
/* matrices: float[16], element [r * 4 + c] of the row-vector form (v * M),  */
/* which is also the column-major array OpenGL expects                       */

static void mat_identity( float *m )
{
    memset( m, 0, 16 * sizeof(float) );
    m[0] = m[5] = m[10] = m[15] = 1;
}

static void mat_mul( float *out, const float *a, const float *b )  /* out = a * b */
{
    float r[16];
    int i, j, k;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
        {
            r[i * 4 + j] = 0;
            for (k = 0; k < 4; k++) r[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
        }
    memcpy( out, r, sizeof(r) );
}

static void mat_rotate( float *m, float deg, int axis )
{
    float r = deg * (float)M_PI / 180, c = cosf( r ), s = sinf( r );
    int a = (axis + 1) % 3, b = (axis + 2) % 3;
    mat_identity( m );
    m[a * 4 + a] = c; m[a * 4 + b] = s;
    m[b * 4 + a] = -s; m[b * 4 + b] = c;
}

static void mat_translate( float *m, float x, float y, float z )
{
    mat_identity( m );
    m[12] = x; m[13] = y; m[14] = z;
}

/* frustum, eye looking down -z; zero_one: depth 0..1 (Direct3D) or -1..1 (GL) */
static void mat_frustum( float *m, float l, float r, float b, float t, float n, float f, int zero_one )
{
    memset( m, 0, 16 * sizeof(float) );
    m[0] = 2 * n / (r - l);
    m[5] = 2 * n / (t - b);
    m[8] = (r + l) / (r - l);
    m[9] = (t + b) / (t - b);
    m[11] = -1;
    if (zero_one)
    {
        m[10] = f / (n - f);
        m[14] = n * f / (n - f);
    }
    else
    {
        m[10] = (f + n) / (n - f);
        m[14] = 2 * f * n / (n - f);
    }
}

static void projection( float *m, int zero_one )
{
    float h = (float)height / width;
    mat_frustum( m, -1, 1, -h, h, 5, 60, zero_one );
}

/* model-view of gear i at animation angle a (degrees) */
static void modelview( float *m, int i, float a )
{
    float t[16];
    mat_rotate( m, a * gear_descs[i].speed + gear_descs[i].phase, 2 );
    mat_translate( t, gear_descs[i].x, gear_descs[i].y, 0 ); mat_mul( m, m, t );
    mat_rotate( t, 30, 1 ); mat_mul( m, m, t );
    mat_rotate( t, 20, 0 ); mat_mul( m, m, t );
    mat_translate( t, 0, 0, -40 ); mat_mul( m, m, t );
}

static const float light_dir[3] = { 0.408248f, 0.408248f, 0.816497f };  /* normalize(5, 5, 10) */
static const float clear_color[4] = { 0.05f, 0.05f, 0.05f, 1.0f };
#define CHECK_ANGLE 15.0f

/* ------------------------------------------------------------------------ */
/* window, timing, check                                                     */

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
        wc.lpszClassName = L"gears";
        RegisterClassW( &wc );
        registered = 1;
    }
    AdjustWindowRect( &r, WS_OVERLAPPEDWINDOW, FALSE );
    swprintf( title, ARRAY_SIZE(title), L"gears - %hs", api );
    return CreateWindowW( L"gears", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, 60, 60,
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

struct result
{
    const char *api;
    int ok;
    char error[160];
    char adapter[160];
    double first_ms, fps;
    unsigned int frames;
    float red, green, blue;
};

/* pixels: rows of width * 4 bytes, rgba or bgra */
static void check_pixels( struct result *res, const BYTE *pixels, unsigned int pitch, int bgra )
{
    unsigned int red = 0, green = 0, blue = 0, x, y, total = width * height;

    for (y = 0; y < (unsigned int)height; y++)
    {
        const BYTE *p = pixels + y * pitch;
        for (x = 0; x < (unsigned int)width; x++, p += 4)
        {
            int r = bgra ? p[2] : p[0], g = p[1], b = bgra ? p[0] : p[2];
            if (r > 60 && r > g + 30 && r > b + 30) red++;
            else if (g > 60 && g > r + 30 && g > b + 15) green++;
            else if (b > 60 && b > r + 30 && b > g + 30) blue++;
        }
    }
    res->red = 100.0f * red / total;
    res->green = 100.0f * green / total;
    res->blue = 100.0f * blue / total;
    if (res->red < 0.3f || res->green < 0.3f || res->blue < 0.3f)
    {
        res->ok = 0;
        snprintf( res->error, sizeof(res->error), "check failed: a gear is missing or wrong" );
    }
}

/* run the frame loop: draw(angle) renders and presents one frame */
static void run_loop( struct result *res, void (*draw)( void *ctx, float angle ), void *ctx, double start )
{
    double t0, t;
    unsigned int frames = 0;
    float angle = CHECK_ANGLE;

    draw( ctx, angle );
    res->first_ms = (now() - start) * 1000;
    t0 = now();
    do
    {
        pump();
        angle += 2.0f;
        draw( ctx, angle );
        frames++;
        t = now();
    } while (t - t0 < seconds);
    res->frames = frames;
    res->fps = frames / (t - t0);
}

/* ------------------------------------------------------------------------ */
/* OpenGL                                                                    */

#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
typedef void (WINAPI *PFNGLGENBUFFERS)( GLsizei, GLuint * );
typedef void (WINAPI *PFNGLBINDBUFFER)( GLenum, GLuint );
typedef void (WINAPI *PFNGLBUFFERDATA)( GLenum, ptrdiff_t, const void *, GLenum );
typedef void (WINAPI *PFNGLDELETEBUFFERS)( GLsizei, const GLuint * );
typedef BOOL (WINAPI *PFNWGLSWAPINTERVALEXT)( int );

struct gl_ctx { HDC dc; };

static void gl_draw_gears( float angle )
{
    float proj[16], mv[16];
    int i;

    glClearColor( clear_color[0], clear_color[1], clear_color[2], clear_color[3] );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    projection( proj, 0 );
    glMatrixMode( GL_PROJECTION );
    glLoadMatrixf( proj );
    glMatrixMode( GL_MODELVIEW );
    for (i = 0; i < 3; i++)
    {
        modelview( mv, i, angle );
        glLoadMatrixf( mv );
        glMaterialfv( GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, gear_descs[i].color );
        /* the vertex arrays point at each gear's own offset in the buffer object */
        glVertexPointer( 3, GL_FLOAT, sizeof(struct vertex), (void *)(ULONG_PTR)(gear_first[i] * sizeof(struct vertex)) );
        glNormalPointer( GL_FLOAT, sizeof(struct vertex), (void *)(ULONG_PTR)(gear_first[i] * sizeof(struct vertex) + 12) );
        glDrawArrays( GL_TRIANGLES, 0, gear_count[i] );
    }
}

static void gl_draw( void *ctx, float angle )
{
    struct gl_ctx *gl = ctx;
    gl_draw_gears( angle );
    SwapBuffers( gl->dc );
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
    float light_pos[4] = { light_dir[0], light_dir[1], light_dir[2], 0 }, ambient[4] = { 0.2f, 0.2f, 0.2f, 1 };
    double start = now();
    struct gl_ctx gl;
    HGLRC rc = NULL;
    GLuint vbo = 0;
    BYTE *pixels;
    HWND hwnd;
    int format;

    hwnd = create_window( "gl" );
    gl.dc = GetDC( hwnd );
    if (!(format = ChoosePixelFormat( gl.dc, &pfd )) || !SetPixelFormat( gl.dc, format, &pfd ))
    {
        snprintf( res->error, sizeof(res->error), "pixel format: error %lu", GetLastError() );
        goto done;
    }
    if (!(rc = wglCreateContext( gl.dc )) || !wglMakeCurrent( gl.dc, rc ))
    {
        snprintf( res->error, sizeof(res->error), "wglCreateContext/MakeCurrent: error %lu", GetLastError() );
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
        snprintf( res->error, sizeof(res->error), "no vertex buffer objects (GL 1.5)" );
        goto done;
    }
    if ((pwglSwapIntervalEXT = (void *)wglGetProcAddress( "wglSwapIntervalEXT" ))) pwglSwapIntervalEXT( 0 );

    pglGenBuffers( 1, &vbo );
    pglBindBuffer( GL_ARRAY_BUFFER, vbo );
    pglBufferData( GL_ARRAY_BUFFER, vert_count * sizeof(struct vertex), verts, GL_STATIC_DRAW );
    glEnableClientState( GL_VERTEX_ARRAY );
    glEnableClientState( GL_NORMAL_ARRAY );
    glEnable( GL_DEPTH_TEST );
    glEnable( GL_LIGHTING );
    glEnable( GL_LIGHT0 );
    glEnable( GL_NORMALIZE );
    glShadeModel( GL_FLAT );
    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();
    glLightfv( GL_LIGHT0, GL_POSITION, light_pos );
    glLightModelfv( GL_LIGHT_MODEL_AMBIENT, ambient );
    glViewport( 0, 0, width, height );

    /* check frame, read back from the back buffer before it is presented */
    gl_draw_gears( CHECK_ANGLE );
    glFinish();
    pixels = malloc( width * height * 4 );
    glReadBuffer( GL_BACK );
    glPixelStorei( GL_PACK_ALIGNMENT, 1 );
    glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels );
    res->ok = 1;
    check_pixels( res, pixels, width * 4, 0 );
    free( pixels );
    if (glGetError() != GL_NO_ERROR && res->ok)
    {
        res->ok = 0;
        snprintf( res->error, sizeof(res->error), "GL error after the check frame" );
    }

    run_loop( res, gl_draw, &gl, start );

done:
    if (vbo && pglDeleteBuffers) pglDeleteBuffers( 1, &vbo );
    if (rc) { wglMakeCurrent( NULL, NULL ); wglDeleteContext( rc ); }
    ReleaseDC( hwnd, gl.dc );
    DestroyWindow( hwnd );
}

/* ------------------------------------------------------------------------ */
/* Direct3D 9                                                                */

struct d3d9_ctx { IDirect3DDevice9 *device; IDirect3DVertexBuffer9 *vb; };

static void d3d9_draw_gears( struct d3d9_ctx *d, float angle )
{
    D3DMATERIAL9 mat = {{0}};
    float proj[16], mv[16];
    int i;

    IDirect3DDevice9_Clear( d->device, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                            D3DCOLOR_COLORVALUE( clear_color[0], clear_color[1], clear_color[2], 1 ), 1.0f, 0 );
    IDirect3DDevice9_BeginScene( d->device );
    projection( proj, 1 );
    IDirect3DDevice9_SetTransform( d->device, D3DTS_PROJECTION, (D3DMATRIX *)proj );
    for (i = 0; i < 3; i++)
    {
        modelview( mv, i, angle );
        IDirect3DDevice9_SetTransform( d->device, D3DTS_WORLD, (D3DMATRIX *)mv );
        memcpy( &mat.Diffuse, gear_descs[i].color, sizeof(mat.Diffuse) );
        memcpy( &mat.Ambient, gear_descs[i].color, sizeof(mat.Ambient) );
        IDirect3DDevice9_SetMaterial( d->device, &mat );
        IDirect3DDevice9_DrawPrimitive( d->device, D3DPT_TRIANGLELIST, gear_first[i], gear_count[i] / 3 );
    }
    IDirect3DDevice9_EndScene( d->device );
}

static void d3d9_draw( void *ctx, float angle )
{
    struct d3d9_ctx *d = ctx;
    d3d9_draw_gears( d, angle );
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
    float id_matrix[16];
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

    if (FAILED(hr = IDirect3DDevice9_CreateVertexBuffer( d.device, vert_count * sizeof(struct vertex), D3DUSAGE_WRITEONLY,
                                                        D3DFVF_XYZ | D3DFVF_NORMAL, D3DPOOL_DEFAULT, &d.vb, NULL )) ||
        FAILED(hr = IDirect3DVertexBuffer9_Lock( d.vb, 0, 0, &data, 0 )))
    {
        snprintf( res->error, sizeof(res->error), "vertex buffer: %#lx", hr );
        goto done;
    }
    memcpy( data, verts, vert_count * sizeof(struct vertex) );
    IDirect3DVertexBuffer9_Unlock( d.vb );
    IDirect3DDevice9_SetStreamSource( d.device, 0, d.vb, 0, sizeof(struct vertex) );
    IDirect3DDevice9_SetFVF( d.device, D3DFVF_XYZ | D3DFVF_NORMAL );

    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = light.Diffuse.a = 1;
    light.Direction.x = -light_dir[0]; light.Direction.y = -light_dir[1]; light.Direction.z = -light_dir[2];
    IDirect3DDevice9_SetLight( d.device, 0, &light );
    IDirect3DDevice9_LightEnable( d.device, 0, TRUE );
    IDirect3DDevice9_SetRenderState( d.device, D3DRS_LIGHTING, TRUE );
    IDirect3DDevice9_SetRenderState( d.device, D3DRS_AMBIENT, D3DCOLOR_COLORVALUE( 0.2f, 0.2f, 0.2f, 1 ) );
    IDirect3DDevice9_SetRenderState( d.device, D3DRS_NORMALIZENORMALS, TRUE );
    IDirect3DDevice9_SetRenderState( d.device, D3DRS_CULLMODE, D3DCULL_NONE );
    IDirect3DDevice9_SetRenderState( d.device, D3DRS_SHADEMODE, D3DSHADE_FLAT );
    IDirect3DDevice9_SetRenderState( d.device, D3DRS_ZENABLE, D3DZB_TRUE );
    mat_identity( id_matrix );
    IDirect3DDevice9_SetTransform( d.device, D3DTS_VIEW, (D3DMATRIX *)id_matrix );

    /* check frame */
    d3d9_draw_gears( &d, CHECK_ANGLE );
    if (FAILED(hr = IDirect3DDevice9_GetBackBuffer( d.device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back )) ||
        FAILED(hr = IDirect3DDevice9_CreateOffscreenPlainSurface( d.device, width, height, D3DFMT_X8R8G8B8,
                                                                   D3DPOOL_SYSTEMMEM, &sys, NULL )) ||
        FAILED(hr = IDirect3DDevice9_GetRenderTargetData( d.device, back, sys )) ||
        FAILED(hr = IDirect3DSurface9_LockRect( sys, &lr, NULL, D3DLOCK_READONLY )))
    {
        snprintf( res->error, sizeof(res->error), "read back: %#lx", hr );
        goto done;
    }
    res->ok = 1;
    check_pixels( res, lr.pBits, lr.Pitch, 1 );
    IDirect3DSurface9_UnlockRect( sys );

    run_loop( res, d3d9_draw, &d, start );

done:
    if (sys) IDirect3DSurface9_Release( sys );
    if (back) IDirect3DSurface9_Release( back );
    if (d.vb) IDirect3DVertexBuffer9_Release( d.vb );
    if (d.device) IDirect3DDevice9_Release( d.device );
    IDirect3D9_Release( d3d );
    DestroyWindow( hwnd );
}

/* ------------------------------------------------------------------------ */
/* shaders for Direct3D 11 and 12 (Shader Model 5.0)                         */

static const char shader_source[] =
    "cbuffer c : register(b0) { row_major float4x4 mv; row_major float4x4 proj; float4 color; };\n"
    "struct vs_out { float4 pos : SV_Position; float4 col : COLOR; };\n"
    "vs_out vs( float3 p : POSITION, float3 n : NORMAL )\n"
    "{\n"
    "    vs_out o;\n"
    "    float3 en = normalize( mul( float4( n, 0 ), mv ).xyz );\n"
    "    float d = max( dot( en, float3( 0.408248, 0.408248, 0.816497 ) ), 0 );\n"
    "    o.pos = mul( mul( float4( p, 1 ), mv ), proj );\n"
    "    o.col = float4( color.rgb * ( 0.2 + d ), 1 );\n"
    "    return o;\n"
    "}\n"
    "float4 ps( vs_out i ) : SV_Target { return i.col; }\n";

struct constants { float mv[16], proj[16], color[4]; };

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
    if (FAILED(hr = pD3DCompile( shader_source, sizeof(shader_source) - 1, "gears", NULL, NULL, entry, target, 0, 0,
                                 blob, &messages )))
        snprintf( error, size, "D3DCompile %s: %#lx %s", entry, hr,
                  messages ? (const char *)ID3D10Blob_GetBufferPointer( messages ) : "" );
    if (messages) ID3D10Blob_Release( messages );
    return hr;
}

static const D3D11_INPUT_ELEMENT_DESC d3d11_layout[] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

static void adapter_name( IDXGIAdapter *adapter, char *out, size_t size, const char *suffix )
{
    DXGI_ADAPTER_DESC desc;
    if (adapter && SUCCEEDED(IDXGIAdapter_GetDesc( adapter, &desc )))
        snprintf( out, size, "%ls%s", desc.Description, suffix );
}

/* ------------------------------------------------------------------------ */
/* Direct3D 11                                                               */

struct d3d11_ctx
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    IDXGISwapChain *swapchain;
    ID3D11RenderTargetView *rtv;
    ID3D11DepthStencilView *dsv;
    ID3D11Buffer *cb;
};

static void d3d11_draw_gears( struct d3d11_ctx *d, float angle )
{
    struct constants c;
    int i;

    ID3D11DeviceContext_ClearRenderTargetView( d->context, d->rtv, clear_color );
    ID3D11DeviceContext_ClearDepthStencilView( d->context, d->dsv, D3D11_CLEAR_DEPTH, 1.0f, 0 );
    projection( c.proj, 1 );
    for (i = 0; i < 3; i++)
    {
        modelview( c.mv, i, angle );
        memcpy( c.color, gear_descs[i].color, sizeof(c.color) );
        ID3D11DeviceContext_UpdateSubresource( d->context, (ID3D11Resource *)d->cb, 0, NULL, &c, 0, 0 );
        ID3D11DeviceContext_Draw( d->context, gear_count[i], gear_first[i] );
    }
}

static void d3d11_draw( void *ctx, float angle )
{
    struct d3d11_ctx *d = ctx;
    d3d11_draw_gears( d, angle );
    IDXGISwapChain_Present( d->swapchain, 0, 0 );
}

static void run_d3d11( struct result *res )
{
    static const char *level_names[] = { "9_1", "9_2", "9_3", "10_0", "10_1", "11_0", "11_1" };
    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                                D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_2,
                                                D3D_FEATURE_LEVEL_9_1 };
    DXGI_SWAP_CHAIN_DESC sd = {0};
    D3D11_TEXTURE2D_DESC td = {0};
    D3D11_BUFFER_DESC bd = {0};
    D3D11_SUBRESOURCE_DATA init = {0};
    D3D11_VIEWPORT vp = { 0, 0, (float)width, (float)height, 0, 1 };
    D3D11_RASTERIZER_DESC rd = {0};
    D3D11_MAPPED_SUBRESOURCE map;
    ID3D11Texture2D *back = NULL, *depth = NULL, *staging = NULL;
    ID3D11Buffer *vb = NULL;
    ID3D11VertexShader *vs = NULL;
    ID3D11PixelShader *ps = NULL;
    ID3D11InputLayout *layout = NULL;
    ID3D11RasterizerState *rs = NULL;
    ID3DBlob *vs_blob = NULL, *ps_blob = NULL;
    IDXGIDevice *dxgi_device = NULL;
    IDXGIAdapter *adapter = NULL;
    struct d3d11_ctx d = {0};
    D3D_FEATURE_LEVEL level;
    UINT stride = sizeof(struct vertex), offset = 0;
    double start = now();
    char suffix[32];
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
    snprintf( suffix, sizeof(suffix), ", level %s",
              level_names[level == D3D_FEATURE_LEVEL_9_1 ? 0 : level == D3D_FEATURE_LEVEL_9_2 ? 1 :
                          level == D3D_FEATURE_LEVEL_9_3 ? 2 : level == D3D_FEATURE_LEVEL_10_0 ? 3 :
                          level == D3D_FEATURE_LEVEL_10_1 ? 4 : level == D3D_FEATURE_LEVEL_11_0 ? 5 : 6] );
    if (SUCCEEDED(ID3D11Device_QueryInterface( d.device, &IID_IDXGIDevice, (void **)&dxgi_device )) &&
        SUCCEEDED(IDXGIDevice_GetAdapter( dxgi_device, &adapter )))
        adapter_name( adapter, res->adapter, sizeof(res->adapter), suffix );

    /* below 11_0 there is no shader model 5: compile for the level */
    {
        const char *vs_target = "vs_4_0_level_9_1", *ps_target = "ps_4_0_level_9_1";
        if (level >= D3D_FEATURE_LEVEL_11_0) { vs_target = "vs_5_0"; ps_target = "ps_5_0"; }
        else if (level >= D3D_FEATURE_LEVEL_10_0) { vs_target = "vs_4_0"; ps_target = "ps_4_0"; }
        else if (level == D3D_FEATURE_LEVEL_9_3) { vs_target = "vs_4_0_level_9_3"; ps_target = "ps_4_0_level_9_3"; }
        if (FAILED(compile( "vs", vs_target, &vs_blob, res->error, sizeof(res->error) )) ||
            FAILED(compile( "ps", ps_target, &ps_blob, res->error, sizeof(res->error) )))
            goto done;
    }
    if (FAILED(hr = ID3D11Device_CreateVertexShader( d.device, ID3D10Blob_GetBufferPointer( vs_blob ),
                                                     ID3D10Blob_GetBufferSize( vs_blob ), NULL, &vs )))
    {
        snprintf( res->error, sizeof(res->error), "CreateVertexShader: %#lx", hr );
        goto done;
    }
    if (FAILED(hr = ID3D11Device_CreatePixelShader( d.device, ID3D10Blob_GetBufferPointer( ps_blob ),
                                                    ID3D10Blob_GetBufferSize( ps_blob ), NULL, &ps )))
    {
        snprintf( res->error, sizeof(res->error), "CreatePixelShader: %#lx", hr );
        goto done;
    }
    if (FAILED(hr = ID3D11Device_CreateInputLayout( d.device, d3d11_layout, ARRAY_SIZE(d3d11_layout),
                                                    ID3D10Blob_GetBufferPointer( vs_blob ),
                                                    ID3D10Blob_GetBufferSize( vs_blob ), &layout )))
    {
        snprintf( res->error, sizeof(res->error), "CreateInputLayout: %#lx", hr );
        goto done;
    }

    bd.ByteWidth = vert_count * sizeof(struct vertex);
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    init.pSysMem = verts;
    if (FAILED(hr = ID3D11Device_CreateBuffer( d.device, &bd, &init, &vb )))
    {
        snprintf( res->error, sizeof(res->error), "vertex buffer: %#lx", hr );
        goto done;
    }
    bd.ByteWidth = sizeof(struct constants);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(hr = ID3D11Device_CreateBuffer( d.device, &bd, NULL, &d.cb )))
    {
        snprintf( res->error, sizeof(res->error), "constant buffer: %#lx", hr );
        goto done;
    }

    IDXGISwapChain_GetBuffer( d.swapchain, 0, &IID_ID3D11Texture2D, (void **)&back );
    ID3D11Device_CreateRenderTargetView( d.device, (ID3D11Resource *)back, NULL, &d.rtv );
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(hr = ID3D11Device_CreateTexture2D( d.device, &td, NULL, &depth )) ||
        FAILED(hr = ID3D11Device_CreateDepthStencilView( d.device, (ID3D11Resource *)depth, NULL, &d.dsv )))
    {
        snprintf( res->error, sizeof(res->error), "depth buffer: %#lx", hr );
        goto done;
    }
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ID3D11Device_CreateRasterizerState( d.device, &rd, &rs );

    ID3D11DeviceContext_OMSetRenderTargets( d.context, 1, &d.rtv, d.dsv );
    ID3D11DeviceContext_RSSetViewports( d.context, 1, &vp );
    ID3D11DeviceContext_RSSetState( d.context, rs );
    ID3D11DeviceContext_IASetInputLayout( d.context, layout );
    ID3D11DeviceContext_IASetPrimitiveTopology( d.context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    ID3D11DeviceContext_IASetVertexBuffers( d.context, 0, 1, &vb, &stride, &offset );
    ID3D11DeviceContext_VSSetShader( d.context, vs, NULL, 0 );
    ID3D11DeviceContext_VSSetConstantBuffers( d.context, 0, 1, &d.cb );
    ID3D11DeviceContext_PSSetShader( d.context, ps, NULL, 0 );

    /* check frame */
    d3d11_draw_gears( &d, CHECK_ANGLE );
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(hr = ID3D11Device_CreateTexture2D( d.device, &td, NULL, &staging )))
    {
        snprintf( res->error, sizeof(res->error), "staging texture: %#lx", hr );
        goto done;
    }
    ID3D11DeviceContext_CopyResource( d.context, (ID3D11Resource *)staging, (ID3D11Resource *)back );
    if (FAILED(hr = ID3D11DeviceContext_Map( d.context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map )))
    {
        snprintf( res->error, sizeof(res->error), "read back: %#lx", hr );
        goto done;
    }
    res->ok = 1;
    check_pixels( res, map.pData, map.RowPitch, 0 );
    ID3D11DeviceContext_Unmap( d.context, (ID3D11Resource *)staging, 0 );

    run_loop( res, d3d11_draw, &d, start );

done:
    if (staging) ID3D11Texture2D_Release( staging );
    if (rs) ID3D11RasterizerState_Release( rs );
    if (d.dsv) ID3D11DepthStencilView_Release( d.dsv );
    if (depth) ID3D11Texture2D_Release( depth );
    if (d.rtv) ID3D11RenderTargetView_Release( d.rtv );
    if (back) ID3D11Texture2D_Release( back );
    if (d.cb) ID3D11Buffer_Release( d.cb );
    if (vb) ID3D11Buffer_Release( vb );
    if (layout) ID3D11InputLayout_Release( layout );
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

#define D3D12_FRAMES 2

struct d3d12_ctx
{
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    ID3D12CommandAllocator *allocator[D3D12_FRAMES];
    ID3D12GraphicsCommandList *list;
    IDXGISwapChain3 *swapchain;
    ID3D12Resource *targets[D3D12_FRAMES], *depth, *vb;
    ID3D12DescriptorHeap *rtv_heap, *dsv_heap;
    UINT rtv_size;
    ID3D12RootSignature *root;
    ID3D12PipelineState *pso;
    ID3D12Fence *fence;
    UINT64 fence_value, frame_fence[D3D12_FRAMES];
    HANDLE event;
    D3D12_VERTEX_BUFFER_VIEW vbv;
};

static void d3d12_wait( struct d3d12_ctx *d, UINT64 value )
{
    if (ID3D12Fence_GetCompletedValue( d->fence ) >= value) return;
    ID3D12Fence_SetEventOnCompletion( d->fence, value, d->event );
    WaitForSingleObject( d->event, INFINITE );
}

static D3D12_CPU_DESCRIPTOR_HANDLE heap_start( ID3D12DescriptorHeap *heap )
{
    return ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart( heap );
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

/* record the gears into the list for back buffer i (state RENDER_TARGET on exit) */
static void d3d12_record( struct d3d12_ctx *d, UINT i, float angle )
{
    D3D12_VIEWPORT vp = { 0, 0, (float)width, (float)height, 0, 1 };
    D3D12_RECT scissor = { 0, 0, width, height };
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = heap_start( d->rtv_heap ), dsv = heap_start( d->dsv_heap );
    struct constants c;
    int g;

    rtv.ptr += i * d->rtv_size;
    transition( d->list, d->targets[i], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET );
    ID3D12GraphicsCommandList_ClearRenderTargetView( d->list, rtv, clear_color, 0, NULL );
    ID3D12GraphicsCommandList_ClearDepthStencilView( d->list, dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL );
    ID3D12GraphicsCommandList_OMSetRenderTargets( d->list, 1, &rtv, FALSE, &dsv );
    ID3D12GraphicsCommandList_SetGraphicsRootSignature( d->list, d->root );
    ID3D12GraphicsCommandList_RSSetViewports( d->list, 1, &vp );
    ID3D12GraphicsCommandList_RSSetScissorRects( d->list, 1, &scissor );
    ID3D12GraphicsCommandList_IASetPrimitiveTopology( d->list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    ID3D12GraphicsCommandList_IASetVertexBuffers( d->list, 0, 1, &d->vbv );
    projection( c.proj, 1 );
    for (g = 0; g < 3; g++)
    {
        modelview( c.mv, g, angle );
        memcpy( c.color, gear_descs[g].color, sizeof(c.color) );
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants( d->list, 0, sizeof(c) / 4, &c, 0 );
        ID3D12GraphicsCommandList_DrawInstanced( d->list, gear_count[g], 1, gear_first[g], 0 );
    }
}

static void d3d12_draw( void *ctx, float angle )
{
    struct d3d12_ctx *d = ctx;
    UINT i = IDXGISwapChain3_GetCurrentBackBufferIndex( d->swapchain );

    d3d12_wait( d, d->frame_fence[i] );
    ID3D12CommandAllocator_Reset( d->allocator[i] );
    ID3D12GraphicsCommandList_Reset( d->list, d->allocator[i], d->pso );
    d3d12_record( d, i, angle );
    transition( d->list, d->targets[i], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT );
    ID3D12GraphicsCommandList_Close( d->list );
    ID3D12CommandQueue_ExecuteCommandLists( d->queue, 1, (ID3D12CommandList **)&d->list );
    IDXGISwapChain3_Present( d->swapchain, 0, 0 );
    ID3D12CommandQueue_Signal( d->queue, d->fence, ++d->fence_value );
    d->frame_fence[i] = d->fence_value;
}

static void run_d3d12( struct result *res )
{
    static const D3D12_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    D3D12_COMMAND_QUEUE_DESC qd = {0};
    D3D12_DESCRIPTOR_HEAP_DESC hd = {0};
    D3D12_HEAP_PROPERTIES heap = {0};
    D3D12_RESOURCE_DESC rdesc = {0};
    D3D12_CLEAR_VALUE clear = {0};
    D3D12_ROOT_PARAMETER param = {0};
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
    UINT64 total;
    double start = now();
    void *data;
    HRESULT hr;
    HWND hwnd;
    UINT i;

    hwnd = create_window( "d3d12" );
    if (FAILED(hr = CreateDXGIFactory1( &IID_IDXGIFactory4, (void **)&factory )))
    {
        snprintf( res->error, sizeof(res->error), "CreateDXGIFactory1: %#lx", hr );
        goto done;
    }
    if (FAILED(hr = D3D12CreateDevice( NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&d.device )))
    {
        snprintf( res->error, sizeof(res->error), "D3D12CreateDevice: %#lx", hr );
        goto done;
    }
    if (SUCCEEDED(IDXGIFactory4_EnumAdapters( factory, 0, &adapter )))
        adapter_name( adapter, res->adapter, sizeof(res->adapter), "" );

    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12Device_CreateCommandQueue( d.device, &qd, &IID_ID3D12CommandQueue, (void **)&d.queue );
    for (i = 0; i < D3D12_FRAMES; i++)
        ID3D12Device_CreateCommandAllocator( d.device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator,
                                             (void **)&d.allocator[i] );
    ID3D12Device_CreateFence( d.device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&d.fence );
    d.event = CreateEventW( NULL, FALSE, FALSE, NULL );

    sd.Width = width;
    sd.Height = height;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = D3D12_FRAMES;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(hr = IDXGIFactory4_CreateSwapChainForHwnd( factory, (IUnknown *)d.queue, hwnd, &sd, NULL, NULL, &sc1 )) ||
        FAILED(hr = IDXGISwapChain1_QueryInterface( sc1, &IID_IDXGISwapChain3, (void **)&d.swapchain )))
    {
        snprintf( res->error, sizeof(res->error), "swap chain: %#lx", hr );
        goto done;
    }

    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = D3D12_FRAMES;
    ID3D12Device_CreateDescriptorHeap( d.device, &hd, &IID_ID3D12DescriptorHeap, (void **)&d.rtv_heap );
    d.rtv_size = ID3D12Device_GetDescriptorHandleIncrementSize( d.device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
    h = heap_start( d.rtv_heap );
    for (i = 0; i < D3D12_FRAMES; i++)
    {
        IDXGISwapChain3_GetBuffer( d.swapchain, i, &IID_ID3D12Resource, (void **)&d.targets[i] );
        ID3D12Device_CreateRenderTargetView( d.device, d.targets[i], NULL, h );
        h.ptr += d.rtv_size;
    }

    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hd.NumDescriptors = 1;
    ID3D12Device_CreateDescriptorHeap( d.device, &hd, &IID_ID3D12DescriptorHeap, (void **)&d.dsv_heap );
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    rdesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rdesc.Width = width;
    rdesc.Height = height;
    rdesc.DepthOrArraySize = 1;
    rdesc.MipLevels = 1;
    rdesc.Format = DXGI_FORMAT_D32_FLOAT;
    rdesc.SampleDesc.Count = 1;
    rdesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;
    if (FAILED(hr = ID3D12Device_CreateCommittedResource( d.device, &heap, D3D12_HEAP_FLAG_NONE, &rdesc,
                                                          D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                                                          &IID_ID3D12Resource, (void **)&d.depth )))
    {
        snprintf( res->error, sizeof(res->error), "depth buffer: %#lx", hr );
        goto done;
    }
    ID3D12Device_CreateDepthStencilView( d.device, d.depth, NULL, heap_start( d.dsv_heap ) );

    /* vertex buffer in an upload heap: mapped memory (VK_EXT_external_memory_host under HVF) */
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    memset( &rdesc, 0, sizeof(rdesc) );
    rdesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rdesc.Width = vert_count * sizeof(struct vertex);
    rdesc.Height = rdesc.DepthOrArraySize = rdesc.MipLevels = 1;
    rdesc.SampleDesc.Count = 1;
    rdesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(hr = ID3D12Device_CreateCommittedResource( d.device, &heap, D3D12_HEAP_FLAG_NONE, &rdesc,
                                                          D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                                                          &IID_ID3D12Resource, (void **)&d.vb )) ||
        FAILED(hr = ID3D12Resource_Map( d.vb, 0, &range, &data )) || !data)
    {
        snprintf( res->error, sizeof(res->error), "vertex buffer: %#lx", hr );
        goto done;
    }
    memcpy( data, verts, vert_count * sizeof(struct vertex) );
    ID3D12Resource_Unmap( d.vb, 0, NULL );
    d.vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress( d.vb );
    d.vbv.SizeInBytes = vert_count * sizeof(struct vertex);
    d.vbv.StrideInBytes = sizeof(struct vertex);

    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param.Constants.Num32BitValues = sizeof(struct constants) / 4;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rsd.NumParameters = 1;
    rsd.pParameters = &param;
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

    /* check frame: draw into back buffer 0 and copy it to a readback buffer */
    memset( &rdesc, 0, sizeof(rdesc) );
    rdesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rdesc.Width = width;
    rdesc.Height = height;
    rdesc.DepthOrArraySize = 1;
    rdesc.MipLevels = 1;
    rdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rdesc.SampleDesc.Count = 1;
    ID3D12Device_GetCopyableFootprints( d.device, &rdesc, 0, 1, 0, &fp, NULL, NULL, &total );
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    memset( &rdesc, 0, sizeof(rdesc) );
    rdesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rdesc.Width = total;
    rdesc.Height = rdesc.DepthOrArraySize = rdesc.MipLevels = 1;
    rdesc.SampleDesc.Count = 1;
    rdesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(hr = ID3D12Device_CreateCommittedResource( d.device, &heap, D3D12_HEAP_FLAG_NONE, &rdesc,
                                                          D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                                                          &IID_ID3D12Resource, (void **)&readback )))
    {
        snprintf( res->error, sizeof(res->error), "readback buffer: %#lx", hr );
        goto done;
    }
    i = IDXGISwapChain3_GetCurrentBackBufferIndex( d.swapchain );
    d3d12_record( &d, i, CHECK_ANGLE );
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
    res->ok = 1;
    check_pixels( res, (BYTE *)data + fp.Offset, fp.Footprint.RowPitch, 0 );
    range.End = 0;
    ID3D12Resource_Unmap( readback, 0, &range );

    run_loop( res, d3d12_draw, &d, start );
    ID3D12CommandQueue_Signal( d.queue, d.fence, ++d.fence_value );
    d3d12_wait( &d, d.fence_value );

done:
    if (readback) ID3D12Resource_Release( readback );
    if (d.list) ID3D12GraphicsCommandList_Release( d.list );
    if (d.pso) ID3D12PipelineState_Release( d.pso );
    if (d.root) ID3D12RootSignature_Release( d.root );
    if (sig) ID3D10Blob_Release( sig );
    if (sig_error) ID3D10Blob_Release( sig_error );
    if (ps_blob) ID3D10Blob_Release( ps_blob );
    if (vs_blob) ID3D10Blob_Release( vs_blob );
    if (d.vb) ID3D12Resource_Release( d.vb );
    if (d.depth) ID3D12Resource_Release( d.depth );
    if (d.dsv_heap) ID3D12DescriptorHeap_Release( d.dsv_heap );
    for (i = 0; i < D3D12_FRAMES; i++) if (d.targets[i]) ID3D12Resource_Release( d.targets[i] );
    if (d.rtv_heap) ID3D12DescriptorHeap_Release( d.rtv_heap );
    if (d.swapchain) IDXGISwapChain3_Release( d.swapchain );
    if (sc1) IDXGISwapChain1_Release( sc1 );
    if (d.fence) ID3D12Fence_Release( d.fence );
    if (d.event) CloseHandle( d.event );
    for (i = 0; i < D3D12_FRAMES; i++) if (d.allocator[i]) ID3D12CommandAllocator_Release( d.allocator[i] );
    if (d.queue) ID3D12CommandQueue_Release( d.queue );
    if (d.device) ID3D12Device_Release( d.device );
    if (adapter) IDXGIAdapter_Release( adapter );
    if (factory) IDXGIFactory4_Release( factory );
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
};

int main( int argc, char **argv )
{
    const char *which = argc > 1 ? argv[1] : "all";
    struct result results[ARRAY_SIZE(apis)] = {{0}};
    unsigned int i, ran = 0, ok = 0;

    if (argc > 2) seconds = atof( argv[2] );
    if (argc > 4) { width = atoi( argv[3] ); height = atoi( argv[4] ); }
    if (seconds <= 0 || width < 64 || height < 64)
    {
        printf( "usage: gears.exe [gl|d3d9|d3d11|d3d12|all] [seconds] [width height]\n" );
        return 255;
    }
    SetProcessDPIAware();
    make_gears();
    printf( "gears: %u vertices (%u, %u, %u), %dx%d, %.1f s per API, %s build\n", vert_count,
            gear_count[0], gear_count[1], gear_count[2], width, height, seconds,
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
        if (res->ok)
            printf( "%-6s ok   fps %8.1f  frames %6u  first %5.0f ms  red %.1f%% green %.1f%% blue %.1f%%  %s\n",
                    res->api, res->fps, res->frames, res->first_ms, res->red, res->green, res->blue, res->adapter );
        else if (res->frames)
            printf( "%-6s FAIL fps %8.1f  frames %6u  %s  red %.1f%% green %.1f%% blue %.1f%%  %s\n",
                    res->api, res->fps, res->frames, res->error, res->red, res->green, res->blue, res->adapter );
        else
            printf( "%-6s FAIL %s  %s\n", res->api, res->error, res->adapter );
        fflush( stdout );
    }
    if (!ran)
    {
        printf( "unknown API %s\n", which );
        return 255;
    }
    printf( "RESULT gears %s %u/%u\n", ok == ran ? "ok" : "FAILED", ok, ran );
    return ran - ok;
}
