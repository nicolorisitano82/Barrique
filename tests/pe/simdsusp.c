/* Barrique: do SIMD and general registers survive thread suspension?
 * Worker threads run a deterministic computation that keeps 16 vector
 * accumulators live in registers (SSE2 on x86-64, NEON on ARM64) and compare
 * every result with the expected one, while another thread keeps suspending
 * them, reading (and writing back) their context and resuming them, the way
 * Firefox samples its threads (background hang monitor, profiler).
 *
 *   simdsusp.exe [seconds] [workers] [setcontext] [sleepms]
 *
 * sleepms >= 0: workers also sleep between rounds. With many workers this makes
 * the HVF backend release and recreate vCPUs (a blocking thread gives its vCPU
 * back once half of them are in use), so the state that must survive that is
 * checked too: each worker uses its own FP rounding mode, and its TEB.
 *
 * Prints counters and "simdsusp: ok" or "simdsusp: FAILED"; exit code 0 or 1.
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fenv.h>

#if defined(__x86_64__)
#include <emmintrin.h>
typedef __m128i vec;
#define VSET(x)    _mm_set1_epi32( x )
#define VMUL(a,b)  _mm_add_epi32( _mm_slli_epi32( a, 5 ), _mm_add_epi32( a, b ) )  /* a*33 + b */
#define VXOR(a,b)  _mm_xor_si128( a, b )
#define VSTORE(p,v) _mm_storeu_si128( (__m128i *)(p), v )
#elif defined(__aarch64__)
#include <arm_neon.h>
typedef uint32x4_t vec;
#define VSET(x)    vdupq_n_u32( x )
#define VMUL(a,b)  vaddq_u32( vmulq_n_u32( a, 33 ), b )
#define VXOR(a,b)  veorq_u32( a, b )
#define VSTORE(p,v) vst1q_u32( (uint32_t *)(p), v )
#endif

#define ROUNDS 20000

static volatile LONG stop;
static LONG errors, results, suspends;
static unsigned int expected[4];

/* 16 accumulators that stay in vector registers through the whole loop */
static void __attribute__((noinline)) compute( unsigned int seed, unsigned int out[4] )
{
    vec a0 = VSET( seed + 0 ),  a1 = VSET( seed + 1 ),  a2 = VSET( seed + 2 ),  a3 = VSET( seed + 3 );
    vec a4 = VSET( seed + 4 ),  a5 = VSET( seed + 5 ),  a6 = VSET( seed + 6 ),  a7 = VSET( seed + 7 );
    vec a8 = VSET( seed + 8 ),  a9 = VSET( seed + 9 ),  a10 = VSET( seed + 10 ), a11 = VSET( seed + 11 );
    vec a12 = VSET( seed + 12 ), a13 = VSET( seed + 13 ), a14 = VSET( seed + 14 ), a15 = VSET( seed + 15 );
    unsigned int r[4];
    int i;

    for (i = 0; i < ROUNDS; i++)
    {
        a0 = VMUL( a0, a1 );   a1 = VMUL( a1, a2 );   a2 = VMUL( a2, a3 );   a3 = VMUL( a3, a4 );
        a4 = VMUL( a4, a5 );   a5 = VMUL( a5, a6 );   a6 = VMUL( a6, a7 );   a7 = VMUL( a7, a8 );
        a8 = VMUL( a8, a9 );   a9 = VMUL( a9, a10 );  a10 = VMUL( a10, a11 ); a11 = VMUL( a11, a12 );
        a12 = VMUL( a12, a13 ); a13 = VMUL( a13, a14 ); a14 = VMUL( a14, a15 ); a15 = VMUL( a15, a0 );
        __asm__ volatile( "" ::: "memory" );
    }
    a0 = VXOR( VXOR( VXOR( a0, a1 ), VXOR( a2, a3 ) ), VXOR( VXOR( a4, a5 ), VXOR( a6, a7 ) ) );
    a8 = VXOR( VXOR( VXOR( a8, a9 ), VXOR( a10, a11 ) ), VXOR( VXOR( a12, a13 ), VXOR( a14, a15 ) ) );
    VSTORE( r, VXOR( a0, a8 ) );
    memcpy( out, r, sizeof(r) );
}

/* general registers too: a long dependent chain kept in locals */
static unsigned long long __attribute__((noinline)) gpr_chain( unsigned long long x )
{
    unsigned long long a = x, b = x ^ 0x9e3779b97f4a7c15ull, c = x * 3, d = x + 7;
    int i;
    for (i = 0; i < ROUNDS * 4; i++)
    {
        a += b; b ^= c; c = (c << 7) | (c >> 57); d += a ^ c;
        __asm__ volatile( "" ::: "memory" );
    }
    return a ^ b ^ c ^ d;
}

static unsigned long long expected_gpr;
static int sleep_ms = -1;
static double expected_div[4];
static const int modes[4] = { FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO };

static double __attribute__((noinline)) fp_div( volatile double a, volatile double b ) { return a / b; }

static DWORD WINAPI worker( void *arg )
{
    unsigned int r[4];
    int mode = (int)(ULONG_PTR)arg % 4;
    DWORD tid = GetCurrentThreadId();

    fesetround( modes[mode] );
    while (!stop)
    {
        if (sleep_ms >= 0)
        {
            Sleep( sleep_ms );
            if (fegetround() != modes[mode] && InterlockedIncrement( &errors ) <= 10)
                printf( "rounding mode lost: %#x expected %#x\n", fegetround(), modes[mode] );
            if (fp_div( 1.0, 3.0 ) != expected_div[mode] && InterlockedIncrement( &errors ) <= 10)
                printf( "FP result wrong for rounding mode %d\n", mode );
            if ((DWORD)*(ULONG_PTR *)((char *)NtCurrentTeb() + 0x48) != tid && InterlockedIncrement( &errors ) <= 10)
                printf( "TEB of another thread\n" );
        }
        compute( 1234, r );
        if (memcmp( r, expected, sizeof(r) ))
        {
            if (InterlockedIncrement( &errors ) <= 10)
                printf( "SIMD result wrong: %08x %08x %08x %08x\n", r[0], r[1], r[2], r[3] );
        }
        if (gpr_chain( 42 ) != expected_gpr && InterlockedIncrement( &errors ) <= 10)
            printf( "GPR result wrong\n" );
        InterlockedIncrement( &results );
    }
    return 0;
}

int main( int argc, char **argv )
{
    unsigned int seconds = argc > 1 ? atoi( argv[1] ) : 10;
    int nb = argc > 2 ? atoi( argv[2] ) : 4, i, set_context = argc > 3 && atoi( argv[3] );
    HANDLE threads[128];
    unsigned int check[4];
    DWORD start;

    compute( 1234, expected );
    compute( 1234, check );
    if (memcmp( expected, check, sizeof(check) )) printf( "not deterministic?\n" );
    expected_gpr = gpr_chain( 42 );
    if (argc > 4) sleep_ms = atoi( argv[4] );
    for (i = 0; i < 4; i++)
    {
        fesetround( modes[i] );
        expected_div[i] = fp_div( 1.0, 3.0 );
    }
    fesetround( FE_TONEAREST );
    if (expected_div[1] == expected_div[2]) printf( "rounding modes not distinguishable?\n" );
    if (nb > 128) nb = 128;
    for (i = 0; i < nb; i++) threads[i] = CreateThread( NULL, 0, worker, (void *)(ULONG_PTR)i, 0, NULL );

    start = GetTickCount();
    srand( 99 );
    while (GetTickCount() - start < seconds * 1000)
    {
        HANDLE t = threads[rand() % nb];
        CONTEXT ctx;

        if (SuspendThread( t ) == (DWORD)-1) continue;
        ctx.ContextFlags = CONTEXT_FULL | CONTEXT_FLOATING_POINT;
        if (GetThreadContext( t, &ctx ) && set_context) SetThreadContext( t, &ctx );
        ResumeThread( t );
        suspends++;
        if (rand() % 4 == 0) Sleep( 0 );
    }
    stop = 1;
    for (i = 0; i < nb; i += 64) WaitForMultipleObjects( min( 64, nb - i ), threads + i, TRUE, 10000 );
    printf( "%ld results checked, %ld suspensions, %ld errors\n", results, suspends, errors );
    printf( "simdsusp: %s\n", errors ? "FAILED" : "ok" );
    return errors ? 1 : 0;
}
