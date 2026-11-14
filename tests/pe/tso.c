/*
 * x86 memory ordering under the emulator.
 *
 * 1. Message passing: one thread writes data then a flag, the other reads the
 *    flag then the data. x86 (TSO) never lets the reader see the flag without
 *    the data; count how often it happens.
 * 2. Time a loop of plain loads and stores while a second thread exists (the
 *    emulator only pays for ordering once the process has more threads).
 */
#include <windows.h>
#include <stdio.h>

#define ROUNDS 2000000

static volatile LONG data, flag, round_no, stop;
static volatile LONG seen_round;
static LONG violations;

static DWORD WINAPI writer( void *arg )
{
    LONG r;
    for (r = 1; r <= ROUNDS; r++)
    {
        while (seen_round != r - 1 && !stop) YieldProcessor();
        data = r;           /* plain stores, ordered on x86 */
        flag = r;
    }
    return 0;
}

static DWORD WINAPI reader( void *arg )
{
    LONG r, f, d;
    for (r = 1; r <= ROUNDS; r++)
    {
        while ((f = flag) != r) YieldProcessor();
        d = data;           /* plain loads, ordered on x86 */
        if (d != r) violations++;
        seen_round = r;
    }
    return 0;
}

static DWORD WINAPI idle( void *arg )
{
    while (!stop) Sleep( 10 );
    return 0;
}

static unsigned int buffer[1 << 16];

int main(void)
{
    HANDLE t[3];
    DWORD start, mp_ms, loop_ms;
    unsigned int i, j, sum = 0;

    start = GetTickCount();
    t[0] = CreateThread( NULL, 0, writer, NULL, 0, NULL );
    t[1] = CreateThread( NULL, 0, reader, NULL, 0, NULL );
    WaitForMultipleObjects( 2, t, TRUE, INFINITE );
    mp_ms = GetTickCount() - start;
    printf( "message passing: %u rounds, %ld violations, %lu ms\n", ROUNDS, violations, mp_ms );

    t[2] = CreateThread( NULL, 0, idle, NULL, 0, NULL );
    start = GetTickCount();
    for (j = 0; j < 400; j++)
        for (i = 0; i < ARRAYSIZE(buffer); i++)
        {
            buffer[i] = buffer[(i * 7) & (ARRAYSIZE(buffer) - 1)] + i + j;
            sum += buffer[i];
        }
    loop_ms = GetTickCount() - start;
    stop = 1;
    WaitForSingleObject( t[2], INFINITE );
    printf( "load/store loop: %lu ms (sum %u)\n", loop_ms, sum );
    printf( "RESULT: %s\n", violations ? "FAILED" : "ok" );
    return 0;
}
