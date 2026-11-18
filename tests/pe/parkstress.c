/* Barrique: thread parking the way Rust's std does it on Windows (WaitOnAddress /
 * WakeByAddressSingle, used by std::thread::park, Mutex, Condvar and the mpsc
 * channels of Firefox's WebRender), while a sampler thread keeps suspending the
 * threads and reading their context, as Firefox's profiler and hang monitor do.
 *
 * Producers push numbered messages into per-pair slots; a consumer that finds
 * its slot empty parks on it until the producer stores and wakes it. Every
 * message carries a checksum and its sequence number: lost, duplicated, torn
 * or out-of-order messages are errors, and so is a consumer that never wakes.
 *
 *   parkstress.exe [seconds] [pairs] [sample]
 *
 * Prints counters and "parkstress: ok" or "parkstress: FAILED"; exit code 0 or 1.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_PAIRS 32

struct msg
{
    ULONG64 seq;
    ULONG64 check;
    ULONG64 payload[6];
};

struct pair
{
    volatile LONG state;          /* 0 empty, 1 full; the address parked on */
    struct msg * volatile slot;
    ULONG64 sent, received;
    HANDLE threads[2];
} __attribute__((aligned(64)));

static struct pair pairs[MAX_PAIRS];
static volatile LONG stop;
static LONG errors;
static LONG64 parks, wakes;

static ULONG64 checksum( const struct msg *m )
{
    ULONG64 c = m->seq * 0x9e3779b97f4a7c15ull;
    int i;
    for (i = 0; i < 6; i++) c = (c ^ m->payload[i]) * 0x100000001b3ull;
    return c;
}

static void fail( const char *what, int pair, ULONG64 a, ULONG64 b )
{
    if (InterlockedIncrement( &errors ) <= 20) printf( "pair %d: %s (%llu, %llu)\n", pair, what, a, b );
}

static DWORD WINAPI producer( void *arg )
{
    int id = (int)(ULONG_PTR)arg;
    struct pair *p = &pairs[id];
    LONG full = 1;

    while (!stop)
    {
        struct msg *m = malloc( sizeof(*m) );
        int i;

        m->seq = ++p->sent;
        for (i = 0; i < 6; i++) m->payload[i] = m->seq * (i + 3) ^ (ULONG64)id << 48;
        m->check = checksum( m );
        /* wait until the consumer took the previous one */
        while (p->state && !stop) WaitOnAddress( &p->state, &full, sizeof(full), 50 );
        if (stop) { free( m ); break; }
        p->slot = m;
        InterlockedExchange( &p->state, 1 );
        WakeByAddressSingle( (void *)&p->state );
        InterlockedIncrement64( &wakes );
    }
    return 0;
}

static DWORD WINAPI consumer( void *arg )
{
    int id = (int)(ULONG_PTR)arg;
    struct pair *p = &pairs[id];
    LONG empty = 0;
    DWORD last_progress = GetTickCount();

    while (!stop)
    {
        struct msg *m;

        if (!p->state)
        {
            InterlockedIncrement64( &parks );
            WaitOnAddress( &p->state, &empty, sizeof(empty), 1000 );
            if (!p->state)
            {
                if (GetTickCount() - last_progress > 5000 && !stop)
                {
                    fail( "no message for 5 s (lost wake-up?)", id, p->received, p->sent );
                    last_progress = GetTickCount();
                }
                continue;
            }
        }
        MemoryBarrier();
        m = p->slot;
        p->slot = NULL;
        if (!m) { fail( "full slot with no message", id, p->received, 0 ); InterlockedExchange( &p->state, 0 ); continue; }
        if (m->check != checksum( m )) fail( "torn message", id, m->seq, m->check );
        if (m->seq != p->received + 1) fail( "out of order", id, m->seq, p->received + 1 );
        p->received = m->seq;
        memset( m, 0xe5, sizeof(*m) );
        free( m );
        last_progress = GetTickCount();
        InterlockedExchange( &p->state, 0 );
        WakeByAddressSingle( (void *)&p->state );
    }
    return 0;
}

int main( int argc, char **argv )
{
    unsigned int seconds = argc > 1 ? atoi( argv[1] ) : 10;
    int nb = argc > 2 ? atoi( argv[2] ) : 8, sample = argc > 3 ? atoi( argv[3] ) : 1, i;
    LONG64 samples = 0;
    ULONG64 total = 0;
    DWORD start;

    if (nb > MAX_PAIRS) nb = MAX_PAIRS;
    for (i = 0; i < nb; i++)
    {
        pairs[i].threads[0] = CreateThread( NULL, 0, producer, (void *)(ULONG_PTR)i, 0, NULL );
        pairs[i].threads[1] = CreateThread( NULL, 0, consumer, (void *)(ULONG_PTR)i, 0, NULL );
    }
    start = GetTickCount();
    srand( 7 );
    while (GetTickCount() - start < seconds * 1000)
    {
        if (sample)
        {
            HANDLE t = pairs[rand() % nb].threads[rand() % 2];
            CONTEXT ctx;
            if (SuspendThread( t ) != (DWORD)-1)
            {
                ctx.ContextFlags = CONTEXT_FULL;
                GetThreadContext( t, &ctx );
                ResumeThread( t );
                samples++;
            }
            if (rand() % 8 == 0) Sleep( 1 );
        }
        else Sleep( 10 );
    }
    stop = 1;
    for (i = 0; i < nb; i++)
    {
        WakeByAddressAll( (void *)&pairs[i].state );
        WaitForMultipleObjects( 2, pairs[i].threads, TRUE, 10000 );
        total += pairs[i].received;
    }
    printf( "%llu messages, %lld parks, %lld wakes, %lld samples, %ld errors\n", total, parks, wakes, samples, errors );
    printf( "parkstress: %s\n", errors ? "FAILED" : "ok" );
    return errors ? 1 : 0;
}
