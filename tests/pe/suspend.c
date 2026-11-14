/*
 * Suspend a thread that spins in pure user code (no syscalls), read and
 * change its context. Under WINE_HVF this can only work through the
 * deferred-signal path: the thread's registers live in the vCPU.
 */
#include <windows.h>

/* x19 counts loop iterations; the loop ends when x20 becomes non-zero,
 * and the thread exits with x20 as its exit code */
extern DWORD WINAPI spin( void *arg );
extern char spin_loop[], spin_loop_end[];
__asm__( ".globl spin\n"
         "spin:\n\t"
         "stp x19, x20, [sp, #-16]!\n\t"
         "mov x19, #0\n\t"
         "mov x20, #0\n"
         ".globl spin_loop\n"
         "spin_loop:\n\t"
         "add x19, x19, #1\n\t"
         "cbz x20, spin_loop\n"
         ".globl spin_loop_end\n"
         "spin_loop_end:\n\t"
         "mov x0, x20\n\t"
         "ldp x19, x20, [sp], #16\n\t"
         "ret" );

static HANDLE out;

static void print( const char *s )
{
    DWORD len = 0, written;
    while (s[len]) len++;
    WriteFile( out, s, len, &written, NULL );
}

static void print_num( const char *label, ULONG64 n )
{
    char buf[32], *p = buf + sizeof(buf) - 1;
    *p = 0;
    do { *--p = '0' + n % 10; n /= 10; } while (n);
    print( label );
    print( p );
    print( "\r\n" );
}

void mainCRTStartup(void)
{
    enum { ROUNDS = 2000 };
    CONTEXT ctx;
    HANDLE thread;
    DWORD i, code = 0;
    DWORD in_loop = 0, increasing = 0, decreasing = 0, errors = 0;
    ULONG64 last = 0;

    out = GetStdHandle( STD_OUTPUT_HANDLE );
    thread = CreateThread( NULL, 0, spin, NULL, 0, NULL );
    Sleep( 50 );

    for (i = 0; i < ROUNDS; i++)
    {
        if (SuspendThread( thread ) == (DWORD)-1) { errors++; continue; }
        ctx.ContextFlags = CONTEXT_FULL;
        if (!GetThreadContext( thread, &ctx )) errors++;
        else
        {
            if (ctx.Pc >= (ULONG64)spin_loop && ctx.Pc <= (ULONG64)spin_loop_end) in_loop++;
            if (ctx.X19 > last) increasing++;
            if (ctx.X19 < last) decreasing++;   /* the state must never go back */
            last = ctx.X19;
        }
        ResumeThread( thread );
        if (!(i % 100)) Sleep( 1 );
    }

    /* now stop the loop by changing x20 from outside */
    SuspendThread( thread );
    ctx.ContextFlags = CONTEXT_FULL;
    GetThreadContext( thread, &ctx );
    ctx.X20 = 0x1234;
    if (!SetThreadContext( thread, &ctx )) errors++;
    ResumeThread( thread );
    if (WaitForSingleObject( thread, 5000 )) print( "thread did not exit after SetThreadContext\r\n" );
    GetExitCodeThread( thread, &code );

    print_num( "rounds:          ", ROUNDS );
    print_num( "pc in spin loop: ", in_loop );
    print_num( "x19 increasing:  ", increasing );
    print_num( "x19 decreasing:  ", decreasing );
    print_num( "api errors:      ", errors );
    print_num( "last x19:        ", last );
    print_num( "exit code:       ", code );
    print( in_loop == ROUNDS && increasing > ROUNDS / 2 && !decreasing && !errors && code == 0x1234
           ? "RESULT: ok\r\n" : "RESULT: FAILED\r\n" );
    ExitProcess( 0 );
}
