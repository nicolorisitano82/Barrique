/* Barrique: cost of first touches of new memory (VM page faults under HVF), as a
 * game loading a level: commit a large block, then touch every 4K page once,
 * in order and with a stride, and time it.
 *
 *   faultbench.exe [MB]      default 512
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static double now_ms(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency( &f );
    QueryPerformanceCounter( &c );
    return c.QuadPart * 1000.0 / f.QuadPart;
}

static double touch( size_t size, size_t stride )
{
    volatile char *p = VirtualAlloc( NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
    size_t start, off;
    double t0, t;

    if (!p) { printf( "VirtualAlloc failed %lu\n", GetLastError() ); exit( 1 ); }
    t0 = now_ms();
    for (start = 0; start < stride; start += 4096)
        for (off = start; off < size; off += stride) p[off] = 1;
    t = now_ms() - t0;
    VirtualFree( (void *)p, 0, MEM_RELEASE );
    return t;
}

int main( int argc, char **argv )
{
    size_t mb = argc > 1 ? atoi( argv[1] ) : 512, size = mb << 20;
    double seq = touch( size, 4096 ), strided = touch( size, 1 << 20 );

    printf( "faultbench: %zu MB, sequential %.1f ms (%.2f us/page), 1MB stride %.1f ms (%.2f us/page)\n",
            mb, seq, seq * 1000 / (size / 4096), strided, strided * 1000 / (size / 4096) );
    return 0;
}
