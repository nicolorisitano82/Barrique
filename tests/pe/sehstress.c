/* Barrique: handled exceptions under load. Each worker fills every vector
 * register with a known pattern (v0-v31 on ARM64, xmm0-xmm15 on x86-64), takes
 * an exception (an undefined instruction or a read of address 0), lets a
 * vectored handler skip the instruction, and checks that every vector register
 * came back intact. Many workers do this at once while a sampler thread keeps
 * suspending them and reading their context, as Firefox's profiler does.
 *
 *   sehstress.exe [seconds] [workers] [sample]
 *
 * Prints counters and "sehstress: ok" or "sehstress: FAILED"; exit code 0 or 1.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__aarch64__)
#define NB_VREGS 32
#define UNDEF_LEN 4
#define NULLREAD_LEN 4
#elif defined(__x86_64__)
#define NB_VREGS 16
#define UNDEF_LEN 2      /* ud2 */
#define NULLREAD_LEN 3   /* mov (%rax),%rax */
#endif

static volatile LONG stop;
static LONG errors;
static LONG64 exceptions, trials;

static void __attribute__((noinline)) trial( const unsigned char *in, unsigned char *out, int null_read )
{
#if defined(__aarch64__)
    __asm__ volatile(
        "mov x9, %0\n\t"
        "ld1 {v0.16b, v1.16b, v2.16b, v3.16b}, [x9], #64\n\t"
        "ld1 {v4.16b, v5.16b, v6.16b, v7.16b}, [x9], #64\n\t"
        "ld1 {v8.16b, v9.16b, v10.16b, v11.16b}, [x9], #64\n\t"
        "ld1 {v12.16b, v13.16b, v14.16b, v15.16b}, [x9], #64\n\t"
        "ld1 {v16.16b, v17.16b, v18.16b, v19.16b}, [x9], #64\n\t"
        "ld1 {v20.16b, v21.16b, v22.16b, v23.16b}, [x9], #64\n\t"
        "ld1 {v24.16b, v25.16b, v26.16b, v27.16b}, [x9], #64\n\t"
        "ld1 {v28.16b, v29.16b, v30.16b, v31.16b}, [x9], #64\n\t"
        "cbnz %w2, 1f\n\t"
        "udf #0\n\t"
        "b 2f\n"
        "1:\tmov x10, #0\n\t"
        "ldr x10, [x10]\n"
        "2:\tmov x9, %1\n\t"
        "st1 {v0.16b, v1.16b, v2.16b, v3.16b}, [x9], #64\n\t"
        "st1 {v4.16b, v5.16b, v6.16b, v7.16b}, [x9], #64\n\t"
        "st1 {v8.16b, v9.16b, v10.16b, v11.16b}, [x9], #64\n\t"
        "st1 {v12.16b, v13.16b, v14.16b, v15.16b}, [x9], #64\n\t"
        "st1 {v16.16b, v17.16b, v18.16b, v19.16b}, [x9], #64\n\t"
        "st1 {v20.16b, v21.16b, v22.16b, v23.16b}, [x9], #64\n\t"
        "st1 {v24.16b, v25.16b, v26.16b, v27.16b}, [x9], #64\n\t"
        "st1 {v28.16b, v29.16b, v30.16b, v31.16b}, [x9], #64\n\t"
        :: "r"(in), "r"(out), "r"(null_read)
        : "x9", "x10", "memory",
          "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
          "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31" );
#elif defined(__x86_64__)
    __asm__ volatile(
#define L(n) "movdqu " #n "*16(%0), %%xmm" #n "\n\t"
        L(0) L(1) L(2) L(3) L(4) L(5) L(6) L(7) L(8) L(9) L(10) L(11) L(12) L(13) L(14) L(15)
#undef L
        "test %2, %2\n\t"
        "jnz 1f\n\t"
        "ud2\n\t"
        "jmp 2f\n"
        "1:\txor %%eax, %%eax\n\t"
        "mov (%%rax), %%rax\n"
        "2:\n\t"
#define S(n) "movdqu %%xmm" #n ", " #n "*16(%1)\n\t"
        S(0) S(1) S(2) S(3) S(4) S(5) S(6) S(7) S(8) S(9) S(10) S(11) S(12) S(13) S(14) S(15)
#undef S
        :: "r"(in), "r"(out), "r"(null_read)
        : "rax", "memory", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
          "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15" );
#endif
}

static LONG CALLBACK handler( EXCEPTION_POINTERS *ep )
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;

    if (code == EXCEPTION_ILLEGAL_INSTRUCTION)
    {
#if defined(__aarch64__)
        ep->ContextRecord->Pc += UNDEF_LEN;
#else
        ep->ContextRecord->Rip += UNDEF_LEN;
#endif
    }
    else if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->ExceptionInformation[1] == 0)
    {
#if defined(__aarch64__)
        ep->ContextRecord->Pc += NULLREAD_LEN;
#else
        ep->ContextRecord->Rip += NULLREAD_LEN;
#endif
    }
    else return EXCEPTION_CONTINUE_SEARCH;
    InterlockedIncrement64( &exceptions );
    return EXCEPTION_CONTINUE_EXECUTION;
}

static DWORD WINAPI worker( void *arg )
{
    unsigned char in[NB_VREGS * 16], out[NB_VREGS * 16];
    unsigned int seed = 1234 + (unsigned int)(ULONG_PTR)arg, i, n = 0;

    while (!stop)
    {
        for (i = 0; i < sizeof(in); i++)
        {
            seed = seed * 1103515245 + 12345;
            in[i] = seed >> 16;
        }
        memset( out, 0, sizeof(out) );
        trial( in, out, n++ & 1 );
        InterlockedIncrement64( &trials );
        if (memcmp( in, out, sizeof(in) ))
        {
            for (i = 0; i < sizeof(in) && in[i] == out[i]; i++) ;
            if (InterlockedIncrement( &errors ) <= 10)
                printf( "vector register %u changed across the exception (%s)\n", i / 16, (n & 1) ? "undefined" : "null read" );
        }
    }
    return 0;
}

int main( int argc, char **argv )
{
    unsigned int seconds = argc > 1 ? atoi( argv[1] ) : 10;
    int nb = argc > 2 ? atoi( argv[2] ) : 8, sample = argc > 3 ? atoi( argv[3] ) : 1, i;
    HANDLE threads[64];
    LONG64 samples = 0;
    DWORD start;

    if (nb > 64) nb = 64;
    AddVectoredExceptionHandler( 1, handler );
    for (i = 0; i < nb; i++) threads[i] = CreateThread( NULL, 0, worker, (void *)(ULONG_PTR)i, 0, NULL );
    start = GetTickCount();
    srand( 3 );
    while (GetTickCount() - start < seconds * 1000)
    {
        if (sample)
        {
            HANDLE t = threads[rand() % nb];
            CONTEXT ctx;
            if (SuspendThread( t ) != (DWORD)-1)
            {
                ctx.ContextFlags = CONTEXT_FULL | CONTEXT_FLOATING_POINT;
                GetThreadContext( t, &ctx );
                ResumeThread( t );
                samples++;
            }
            if (rand() % 8 == 0) Sleep( 0 );
        }
        else Sleep( 10 );
    }
    stop = 1;
    WaitForMultipleObjects( nb, threads, TRUE, 10000 );
    printf( "%lld trials, %lld exceptions, %lld samples, %ld errors\n", trials, exceptions, samples, errors );
    printf( "sehstress: %s\n", errors ? "FAILED" : "ok" );
    return errors ? 1 : 0;
}
