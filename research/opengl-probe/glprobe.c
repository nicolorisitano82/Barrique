/* Barrique: OpenGL 4.6 on Metal, probe (research/opengl46-metal.md, step 1-2).
 * Native macOS, outside Wine: Mesa's Zink (GL on Vulkan) over KosmicKrisp
 * (Vulkan on Metal), through EGL without a window (surfaceless). Asks for the
 * highest core context from 4.6 down and prints what Zink gives.
 *
 *   VK_ICD_FILENAMES=<kosmickrisp icd json> glprobe
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>
#include <string.h>

#define GL_VENDOR                   0x1F00
#define GL_RENDERER                 0x1F01
#define GL_VERSION                  0x1F02
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
typedef const unsigned char *(*get_string_func)( unsigned int );

int main(void)
{
    static const int versions[][2] = { {4,6}, {4,5}, {4,3}, {4,1}, {4,0}, {3,3}, {3,2}, {3,1}, {3,0} };
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_display;
    EGLDisplay dpy;
    EGLContext ctx = EGL_NO_CONTEXT;
    EGLConfig config = NULL;
    EGLint major, minor, n = 0;
    get_string_func gl_get_string;
    unsigned int i;

    get_display = (void *)eglGetProcAddress( "eglGetPlatformDisplayEXT" );
    if (!get_display) { printf( "no eglGetPlatformDisplayEXT\n" ); return 1; }
    dpy = get_display( EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL );
    if (!eglInitialize( dpy, &major, &minor )) { printf( "eglInitialize failed %#x\n", eglGetError() ); return 1; }
    printf( "EGL %d.%d, %s\n", major, minor, eglQueryString( dpy, EGL_VENDOR ) );
    if (!eglBindAPI( EGL_OPENGL_API )) { printf( "eglBindAPI(GL) failed %#x\n", eglGetError() ); return 1; }
    {
        static const EGLint attribs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
        eglChooseConfig( dpy, attribs, &config, 1, &n );
    }

    for (i = 0; i < sizeof(versions) / sizeof(versions[0]); i++)
    {
        const EGLint attribs[] = {
            EGL_CONTEXT_MAJOR_VERSION, versions[i][0], EGL_CONTEXT_MINOR_VERSION, versions[i][1],
            EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE };
        ctx = eglCreateContext( dpy, n ? config : EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attribs );
        if (ctx != EGL_NO_CONTEXT)
        {
            printf( "highest core context created: %d.%d\n", versions[i][0], versions[i][1] );
            break;
        }
        printf( "core %d.%d: no (%#x)\n", versions[i][0], versions[i][1], eglGetError() );
    }
    if (ctx == EGL_NO_CONTEXT) { printf( "no core context at all\n" ); return 1; }
    if (!eglMakeCurrent( dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx )) { printf( "eglMakeCurrent failed %#x\n", eglGetError() ); return 1; }
    gl_get_string = (get_string_func)eglGetProcAddress( "glGetString" );
    printf( "GL_VENDOR   %s\n", gl_get_string( GL_VENDOR ) );
    printf( "GL_RENDERER %s\n", gl_get_string( GL_RENDERER ) );
    printf( "GL_VERSION  %s\n", gl_get_string( GL_VERSION ) );
    printf( "GLSL        %s\n", gl_get_string( GL_SHADING_LANGUAGE_VERSION ) );
    return 0;
}
