/*
 * Smallest CreateProcess test for 32-bit x86: start "cmd.exe /c exit 7",
 * wait for it and check the exit code.
 */
#include <windows.h>

static HANDLE out;

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

static BOOL run( char *cmdline, DWORD expect )
{
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    DWORD code = 0, wait = 0;
    BOOL ok;

    print( "running: " );
    print( cmdline );
    print( "\r\n" );
    ok = CreateProcessA( NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi );
    print_num( "  CreateProcess ok: ", ok );
    if (!ok)
    {
        print_num( "  GetLastError: ", GetLastError() );
        return FALSE;
    }
    wait = WaitForSingleObject( pi.hProcess, 20000 );
    GetExitCodeProcess( pi.hProcess, &code );
    print_num( "  wait result (0 = done): ", wait );
    print_num( "  exit code: ", code );
    return wait == 0 && code == expect;
}

void __stdcall entry(void)
{
    char cmd[] = "cmd.exe /c exit 7";
    char self[MAX_PATH + 16], *p = GetCommandLineA();
    DWORD len;
    BOOL ok1, ok2;

    out = GetStdHandle( STD_OUTPUT_HANDLE );
    /* child mode: "proc32.exe child" exits with 9 */
    while (*p) p++;
    if (p - GetCommandLineA() > 6 && !lstrcmpA( p - 5, "child" )) ExitProcess( 9 );

    ok1 = run( cmd, 7 );
    self[0] = '"';
    len = GetModuleFileNameA( NULL, self + 1, MAX_PATH );
    lstrcpyA( self + 1 + len, "\" child" );
    ok2 = run( self, 9 );
    print( ok1 && ok2 ? "RESULT: ok\r\n" : "RESULT: FAILED\r\n" );
    ExitProcess( 0 );
}
