/* Barrique: static TLS of DLLs loaded at run time (like Gecko's xul.dll): each
 * DLL and each thread must get its own storage, with the initial values.
 *   tlstest.exe <dll1> <dll2>     two copies of tlsdll.dll under different names
 */
#include <windows.h>
#include <stdio.h>
typedef ULONG_PTR *(*getter)(void);
static _Thread_local ULONG_PTR exe_tls = 0x7777;
static getter a1, b1, a2, b2;
static int bad;

static void check( int ok, const char *what )
{
    printf( "%-48s %s\n", what, ok ? "ok" : "FAILED" );
    if (!ok) bad = 1;
}

static void run( const char *who )
{
    char buf[80];
    ULONG_PTR *p1 = a1(), *p2 = a2(), *q1 = b1(), *q2 = b2();
    snprintf( buf, sizeof(buf), "%s: initial values", who );
    check( *p1 == 0x1111 && *p2 == 0x1111 && q1[3] == 4 && q2[3] == 4 && exe_tls == 0x7777, buf );
    snprintf( buf, sizeof(buf), "%s: distinct storage", who );
    check( p1 != p2 && q1 != q2 && p1 != &exe_tls && p2 != &exe_tls, buf );
    *p1 = 0xa1; *p2 = 0xa2; exe_tls = 0xe0; q1[0] = 0xb1; q2[0] = 0xb2;
    snprintf( buf, sizeof(buf), "%s: writes stay apart", who );
    check( *a1() == 0xa1 && *a2() == 0xa2 && exe_tls == 0xe0 && b1()[0] == 0xb1 && b2()[0] == 0xb2, buf );
}

static DWORD WINAPI thread( void *arg ) { run( "new thread" ); return 0; }

int main( int argc, char **argv )
{
    HMODULE m1, m2;
    HANDLE t;
    if (argc < 3) return 2;
    m1 = LoadLibraryA( argv[1] ); m2 = LoadLibraryA( argv[2] );
    if (!m1 || !m2) { printf( "load failed %lu\n", GetLastError() ); return 2; }
    a1 = (getter)GetProcAddress( m1, "get_a" ); b1 = (getter)GetProcAddress( m1, "get_b" );
    a2 = (getter)GetProcAddress( m2, "get_a" ); b2 = (getter)GetProcAddress( m2, "get_b" );
    run( "main thread" );
    t = CreateThread( NULL, 0, thread, NULL, 0, NULL );
    WaitForSingleObject( t, INFINITE );
    printf( "tlstest: %s\n", bad ? "FAILED" : "ok" );
    return bad;
}
