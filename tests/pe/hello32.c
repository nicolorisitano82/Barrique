/*
 * 32-bit x86 test program: prints, does some integer and floating point work,
 * and checks the results, so a wrong translation shows up as FAILED.
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

static unsigned int fib( unsigned int n ) { return n < 2 ? n : fib( n - 1 ) + fib( n - 2 ); }

void __stdcall entry(void)
{
    volatile double x = 1.0;
    unsigned int f, i, eax, ebx, ecx, edx;
    unsigned int vendor[4];

    out = GetStdHandle( STD_OUTPUT_HANDLE );
    print( "hello from 32-bit x86 code\r\n" );
    print_num( "pointer size: ", sizeof(void *) );

    __asm__ volatile( "cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0) );
    vendor[0] = ebx;
    vendor[1] = edx;
    vendor[2] = ecx;
    vendor[3] = 0;
    print( "cpuid vendor: " );
    print( (const char *)vendor );
    print( "\r\n" );

    f = fib( 25 );
    print_num( "fib(25): ", f );
    for (i = 0; i < 10; i++) x = x * 1.5;
    print_num( "1.5^10 * 1000: ", (unsigned int)(x * 1000) );
    print( f == 75025 && (unsigned int)(x * 1000) == 57665 ? "RESULT: ok\r\n" : "RESULT: FAILED\r\n" );
    ExitProcess( 0 );
}
