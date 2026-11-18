/* Barrique: cost of Windows synchronization primitives (msync study, step 1).
 * Each test reports nanoseconds per operation; the ones marked "server" go
 * through wineserver in Wine 11 on macOS (no in-process sync backend there),
 * the others stay in the process (futex-like or user-space).
 *
 *   syncbench.exe [iterations]
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static LARGE_INTEGER freq;
static int iters = 20000;

static double now_ns(void)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter( &c );
    return (double)c.QuadPart * 1e9 / freq.QuadPart;
}

static void report( const char *name, const char *kind, double ns, int ops )
{
    printf( "%-40s %-8s %10.0f ns/op\n", name, kind, ns / ops );
}

/* event ping-pong between two threads: two waits and two sets per round trip */
static HANDLE ev_a, ev_b;
static DWORD WINAPI pong_events( void *arg )
{
    int i;
    for (i = 0; i < iters; i++)
    {
        WaitForSingleObject( ev_a, INFINITE );
        SetEvent( ev_b );
    }
    return 0;
}

static CRITICAL_SECTION cs;
static volatile LONG cs_turn;
static DWORD WINAPI pong_cs( void *arg )
{
    int i;
    for (i = 0; i < iters; i++)
    {
        for (;;)
        {
            EnterCriticalSection( &cs );
            if (cs_turn == 1) { cs_turn = 0; LeaveCriticalSection( &cs ); break; }
            LeaveCriticalSection( &cs );
        }
    }
    return 0;
}

static volatile LONG addr_val;
static DWORD WINAPI pong_address( void *arg )
{
    int i;
    LONG one = 1;
    for (i = 0; i < iters; i++)
    {
        while (addr_val != 1) WaitOnAddress( &addr_val, &(LONG){0}, sizeof(LONG), INFINITE );
        addr_val = 2;
        WakeByAddressSingle( (void *)&addr_val );
        (void)one;
    }
    return 0;
}

int main( int argc, char **argv )
{
    HANDLE thread, mutex, sem, evs[4];
    SRWLOCK srw = SRWLOCK_INIT;
    double t;
    int i;

    if (argc > 1) iters = atoi( argv[1] );
    QueryPerformanceFrequency( &freq );

    mutex = CreateMutexW( NULL, FALSE, NULL );
    t = now_ns();
    for (i = 0; i < iters; i++) { WaitForSingleObject( mutex, INFINITE ); ReleaseMutex( mutex ); }
    report( "mutex wait+release, uncontended", "server", now_ns() - t, iters );

    sem = CreateSemaphoreW( NULL, 0, 1, NULL );
    t = now_ns();
    for (i = 0; i < iters; i++) { ReleaseSemaphore( sem, 1, NULL ); WaitForSingleObject( sem, INFINITE ); }
    report( "semaphore release+wait, uncontended", "server", now_ns() - t, iters );

    for (i = 0; i < 4; i++) evs[i] = CreateEventW( NULL, FALSE, FALSE, NULL );
    t = now_ns();
    for (i = 0; i < iters; i++) { SetEvent( evs[i & 3] ); WaitForMultipleObjects( 4, evs, FALSE, INFINITE ); }
    report( "SetEvent + WaitForMultipleObjects(4)", "server", now_ns() - t, iters );

    t = now_ns();
    for (i = 0; i < iters; i++) WaitForSingleObject( evs[0], 0 );
    report( "WaitForSingleObject, timeout 0", "server", now_ns() - t, iters );

    ev_a = CreateEventW( NULL, FALSE, FALSE, NULL );
    ev_b = CreateEventW( NULL, FALSE, FALSE, NULL );
    thread = CreateThread( NULL, 0, pong_events, NULL, 0, NULL );
    t = now_ns();
    for (i = 0; i < iters; i++) { SetEvent( ev_a ); WaitForSingleObject( ev_b, INFINITE ); }
    report( "event ping-pong (round trip)", "server", now_ns() - t, iters );
    WaitForSingleObject( thread, INFINITE );

    InitializeCriticalSection( &cs );
    t = now_ns();
    for (i = 0; i < iters; i++) { EnterCriticalSection( &cs ); LeaveCriticalSection( &cs ); }
    report( "critical section, uncontended", "process", now_ns() - t, iters );

    t = now_ns();
    for (i = 0; i < iters; i++) { AcquireSRWLockExclusive( &srw ); ReleaseSRWLockExclusive( &srw ); }
    report( "SRW lock, uncontended", "process", now_ns() - t, iters );

    cs_turn = 0;
    thread = CreateThread( NULL, 0, pong_cs, NULL, 0, NULL );
    t = now_ns();
    for (i = 0; i < iters; i++)
    {
        for (;;)
        {
            EnterCriticalSection( &cs );
            if (cs_turn == 0) { cs_turn = 1; LeaveCriticalSection( &cs ); break; }
            LeaveCriticalSection( &cs );
        }
    }
    WaitForSingleObject( thread, INFINITE );
    report( "critical section ping-pong (round trip)", "process", now_ns() - t, iters );

    addr_val = 0;
    thread = CreateThread( NULL, 0, pong_address, NULL, 0, NULL );
    t = now_ns();
    for (i = 0; i < iters; i++)
    {
        addr_val = 1;
        WakeByAddressSingle( (void *)&addr_val );
        while (addr_val != 2) WaitOnAddress( &addr_val, &(LONG){1}, sizeof(LONG), INFINITE );
        addr_val = 0;
    }
    WaitForSingleObject( thread, INFINITE );
    report( "WaitOnAddress ping-pong (round trip)", "process", now_ns() - t, iters );
    return 0;
}
