/*
 * Barrique benchmark: the same C code built natively for macOS (reference)
 * and as Windows ARM64, x86-64 and i386 programs. Each test prints its time
 * in ms and a checksum that must match the native one.
 *
 *   int     integer arithmetic (LCG, multiplies, shifts, divisions)
 *   fp      double precision math (sqrt, fused-style multiply-adds)
 *   mem     memory bandwidth (strided reads and writes over 32MB)
 *   atomic  atomic increments on one thread (lock-prefixed on x86)
 *   order   two threads passing a message back and forth (memory ordering)
 *   syscall yield to the scheduler (one VM exit per call under Barrique)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
typedef HANDLE thread_t;
static double now_ms(void)
{
    LARGE_INTEGER c, f;
    QueryPerformanceCounter( &c );
    QueryPerformanceFrequency( &f );
    return c.QuadPart * 1000.0 / f.QuadPart;
}
static void start_thread( thread_t *t, DWORD (WINAPI *fn)(void *), void *arg ) { *t = CreateThread( NULL, 0, fn, arg, 0, NULL ); }
static void join_thread( thread_t t ) { WaitForSingleObject( t, INFINITE ); CloseHandle( t ); }
static void do_yield(void) { SwitchToThread(); }
#define THREAD_FN(name) static DWORD WINAPI name( void *arg )
#define THREAD_RET return 0
static long atomic_inc( volatile long *p ) { return InterlockedIncrement( p ); }
#else
#include <pthread.h>
#include <sched.h>
#include <time.h>
typedef pthread_t thread_t;
static double now_ms(void)
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
static void start_thread( thread_t *t, void *(*fn)(void *), void *arg ) { pthread_create( t, NULL, fn, arg ); }
static void join_thread( thread_t t ) { pthread_join( t, NULL ); }
static void do_yield(void) { sched_yield(); }
#define THREAD_FN(name) static void *name( void *arg )
#define THREAD_RET return NULL
static long atomic_inc( volatile long *p ) { return __atomic_add_fetch( p, 1, __ATOMIC_SEQ_CST ); }
#endif

static unsigned int bench_int(void)
{
    unsigned int x = 12345, acc = 0, i;
    for (i = 0; i < 200000000; i++)
    {
        x = x * 1103515245u + 12345u;
        acc += (x >> 7) ^ (x << 3);
        if (!(i & 63)) acc += x / ((i & 0xff) + 1);
    }
    return acc;
}

static unsigned int bench_fp(void)
{
    double a = 1.0, b = 0.5, s = 0;
    int i;
    for (i = 0; i < 50000000; i++)
    {
        a = a * 1.0000001 + b * 0.25;
        b = sqrt( a ) * 0.5 + 1e-9 * i;
        s += a * 1e-12 - b * 1e-9;
    }
    return (unsigned int)(long long)(s * 1000.0);
}

static unsigned int bench_mem(void)
{
    size_t n = 32 << 20, i;
    unsigned char *buf = malloc( n );
    unsigned int acc = 0;
    int pass;
    for (i = 0; i < n; i++) buf[i] = (unsigned char)i;
    for (pass = 0; pass < 20; pass++)
    {
        unsigned int *w = (unsigned int *)buf;
        for (i = 0; i < n / 4; i += 4)
        {
            acc += w[i] ^ w[i + 1];
            w[i + 2] = acc;
        }
        memcpy( buf, buf + n / 2, n / 2 );
    }
    free( buf );
    return acc;
}

static volatile long counter;
static unsigned int bench_atomic(void)
{
    int i;
    counter = 0;
    for (i = 0; i < 50000000; i++) atomic_inc( &counter );
    return (unsigned int)counter;
}

#define ORDER_ROUNDS 2000000
static volatile long ping, pong, msg;
static long msg_sum;
THREAD_FN(order_peer)
{
    long r;
    for (r = 1; r <= ORDER_ROUNDS; r++)
    {
        while (ping != r) ;
        msg_sum += msg;
        pong = r;
    }
    THREAD_RET;
}
static unsigned int bench_order(void)
{
    thread_t t;
    long r;
    ping = pong = 0; msg_sum = 0;
    start_thread( &t, order_peer, NULL );
    for (r = 1; r <= ORDER_ROUNDS; r++)
    {
        msg = r;
        ping = r;
        while (pong != r) ;
    }
    join_thread( t );
    return (unsigned int)msg_sum;
}

static unsigned int bench_syscall(void)
{
    int i;
    for (i = 0; i < 1000000; i++) do_yield();
    return i;
}

static const struct { const char *name; unsigned int (*fn)(void); } tests[] =
{
    { "int", bench_int }, { "fp", bench_fp }, { "mem", bench_mem },
    { "atomic", bench_atomic }, { "order", bench_order }, { "syscall", bench_syscall },
};

int main( int argc, char **argv )
{
    unsigned int i;
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
        double t0, ms;
        unsigned int sum;
        if (argc > 1 && strcmp( argv[1], tests[i].name )) continue;
        t0 = now_ms();
        sum = tests[i].fn();
        ms = now_ms() - t0;
        printf( "%-8s %9.1f ms  checksum %08x\n", tests[i].name, ms, sum );
        fflush( stdout );
    }
    return 0;
}
