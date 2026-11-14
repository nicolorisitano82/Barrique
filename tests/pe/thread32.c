/*
 * Smallest multi-thread test for 32-bit x86: create a thread, wait for it,
 * check its exit code and that it really ran.
 */
#include <windows.h>

static HANDLE out;
static volatile LONG ran;

static void print( const char *s )
{
    DWORD len = 0, written;
    while (s[len]) len++;
    WriteFile( out, s, len, &written, NULL );
}

static void print_num( const char *label, unsigned int n )
{
    char buf[16], *p = buf + sizeof(buf) - 1;
    *p = 0;
    do { *--p = '0' + n % 10; n /= 10; } while (n);
    print( label );
    print( p );
    print( "\r\n" );
}

static DWORD WINAPI worker( void *arg )
{
    InterlockedIncrement( &ran );
    return (DWORD)(ULONG_PTR)arg + 1;
}

void __stdcall entry(void)
{
    HANDLE thread;
    DWORD wait, code = 0, tid = 0;

    out = GetStdHandle( STD_OUTPUT_HANDLE );
    print( "creating thread\r\n" );
    thread = CreateThread( NULL, 0, worker, (void *)41, 0, &tid );
    print_num( "thread handle ok: ", thread != NULL );
    wait = WaitForSingleObject( thread, 5000 );
    GetExitCodeThread( thread, &code );
    print_num( "wait result (0 = done, 258 = timeout): ", wait );
    print_num( "exit code (expect 42): ", code );
    print_num( "worker ran: ", ran );
    print( wait == 0 && code == 42 && ran == 1 ? "RESULT: ok\r\n" : "RESULT: FAILED\r\n" );
    ExitProcess( 0 );
}
