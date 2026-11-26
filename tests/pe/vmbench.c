/* Barrique: cost of VirtualAlloc/touch/VirtualFree and VirtualProtect cycles,
 * the pattern of a game loading a new area (Unreal frees and commits big blocks) */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static double now(void)
{
    LARGE_INTEGER c, f;
    QueryPerformanceCounter( &c );
    QueryPerformanceFrequency( &f );
    return (double)c.QuadPart / f.QuadPart;
}

int main(int argc, char **argv)
{
    const SIZE_T size = 64 << 20, page = 4096;
    double t;
    DWORD old;
    int i;
    SIZE_T off;
    char *p;

    int loops = argc > 1 ? atoi( argv[1] ) : 20;

    t = now();
    for (i = 0; i < loops; i++)
    {
        p = VirtualAlloc( NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
        for (off = 0; off < size; off += page) p[off] = 1;
        VirtualFree( p, 0, MEM_RELEASE );
    }
    printf( "alloc+touch+free 64MB: %.1f ms\n", (now() - t) * 1000 / loops );
    if (argc > 1) return 0;

    p = VirtualAlloc( NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
    t = now();
    for (i = 0; i < 20; i++)
    {
        for (off = 0; off < size; off += page) p[off] = 1;
        VirtualFree( p, size, MEM_DECOMMIT );
        VirtualAlloc( p, size, MEM_COMMIT, PAGE_READWRITE );
    }
    printf( "touch+decommit+commit 64MB: %.1f ms\n", (now() - t) * 1000 / 20 );

    t = now();
    for (i = 0; i < 20; i++)
    {
        for (off = 0; off < size; off += page) p[off] = 1;
        VirtualProtect( p, size, PAGE_READONLY, &old );
        VirtualProtect( p, size, PAGE_READWRITE, &old );
    }
    printf( "touch+protect 64MB: %.1f ms\n", (now() - t) * 1000 / 20 );

    t = now();
    for (i = 0; i < 20000; i++)
    {
        char *q = VirtualAlloc( NULL, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
        q[0] = 1; q[32768] = 1;
        VirtualFree( q, 0, MEM_RELEASE );
    }
    printf( "small alloc+touch+free 64KB: %.1f us\n", (now() - t) * 1e6 / 20000 );
    return 0;
}
