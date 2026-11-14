/*
 * More live threads than the VM has vCPUs (64): 100 threads wait on an event,
 * then all work, sleep and work again before exiting.
 */
#include <windows.h>
#include <stdio.h>

#define NTHREADS 100

static HANDLE start;
static volatile LONG started, finished;

static DWORD WINAPI worker( void *arg )
{
    volatile unsigned int x = 0;
    int i, round;

    InterlockedIncrement( &started );
    WaitForSingleObject( start, INFINITE );
    for (round = 0; round < 5; round++)
    {
        for (i = 0; i < 100000; i++) x += i;
        Sleep( 10 );
    }
    InterlockedIncrement( &finished );
    return (DWORD)(ULONG_PTR)arg;
}

int main(void)
{
    HANDLE threads[NTHREADS];
    DWORD code, ok_codes = 0, t0;
    int i;

    start = CreateEventA( NULL, TRUE, FALSE, NULL );
    for (i = 0; i < NTHREADS; i++)
    {
        threads[i] = CreateThread( NULL, 0, worker, (void *)(ULONG_PTR)(i + 1000), 0, NULL );
        if (!threads[i])
        {
            printf( "CreateThread %d failed: %lu\n", i, GetLastError() );
            return 1;
        }
    }
    t0 = GetTickCount();
    while (started < NTHREADS && GetTickCount() - t0 < 20000) Sleep( 10 );
    printf( "threads started and waiting: %ld\n", started );
    SetEvent( start );

    for (i = 0; i < NTHREADS; i++)
    {
        if (WaitForSingleObject( threads[i], 60000 )) printf( "thread %d did not finish\n", i );
        else if (GetExitCodeThread( threads[i], &code ) && code == (DWORD)(i + 1000)) ok_codes++;
        CloseHandle( threads[i] );
    }
    printf( "threads finished: %ld, right exit codes: %lu, time %lu ms\n",
            finished, ok_codes, GetTickCount() - t0 );
    printf( "RESULT: %s\n", started == NTHREADS && finished == NTHREADS && ok_codes == NTHREADS ? "ok" : "FAILED" );
    return 0;
}
