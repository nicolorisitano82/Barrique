/* Barrique: shared memory coherence between processes (each one its own VM
 * under the Hypervisor.framework backend), the way Firefox moves IPC data and
 * graphics surfaces: a pagefile-backed section shared by a parent and a child.
 *
 *  - slots in the section go back and forth: one side fills a slot with a
 *    pattern for its generation and publishes it, the other side checks the
 *    whole slot, then refills it for the next generation;
 *  - both sides keep mapping and unmapping extra views of the same section
 *    (at new addresses) and reading/writing through them, so pages of one
 *    section live at several addresses that come and go.
 *
 *   shmstress.exe [seconds]
 *
 * Prints counters and "shmstress: ok" or "shmstress: FAILED"; exit code 0 or 1.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOT      0x8000
#define NB_SLOTS  32
#define SECTION   (SLOT * NB_SLOTS + 0x1000)

struct header
{
    volatile LONG stop;
    volatile LONG state[NB_SLOTS];  /* even: parent's turn, odd: child's turn */
    volatile LONG errors;
    volatile LONG checks;
};

static HANDLE section;
static BYTE *base;
static int is_child;

static void fill( BYTE *p, LONG gen, int slot )
{
    DWORD *d = (DWORD *)p, v = (DWORD)gen * 0x9e3779b1u ^ (DWORD)slot * 0x85ebca6bu;
    size_t i;
    for (i = 0; i < SLOT / 4; i++) d[i] = v ^ (DWORD)i;
}

static BOOL check( const BYTE *p, LONG gen, int slot, size_t *bad, DWORD *got, DWORD *want )
{
    const DWORD *d = (const DWORD *)p;
    DWORD v = (DWORD)gen * 0x9e3779b1u ^ (DWORD)slot * 0x85ebca6bu;
    size_t i;
    for (i = 0; i < SLOT / 4; i++)
        if (d[i] != (v ^ (DWORD)i)) { *bad = i * 4; *got = d[i]; *want = v ^ (DWORD)i; return FALSE; }
    return TRUE;
}

/* slots whose turn is ours: check what the other side wrote, write the next generation */
static DWORD WINAPI slot_thread( void *arg )
{
    struct header *h = (struct header *)base;
    int first = (int)(ULONG_PTR)arg, s;

    while (!h->stop)
    {
        for (s = first; s < NB_SLOTS; s += 2)
        {
            LONG st = h->state[s];
            BYTE *p = base + 0x1000 + SLOT * s;
            size_t bad;
            DWORD got, want;

            if ((st & 1) != is_child) continue;
            MemoryBarrier();
            if (st && !check( p, st, s, &bad, &got, &want ))
            {
                if (InterlockedIncrement( &h->errors ) <= 10)
                    printf( "%s: slot %d gen %ld offset %#zx: got %#lx expected %#lx\n",
                            is_child ? "child" : "parent", s, st, bad, got, want );
            }
            InterlockedIncrement( &h->checks );
            fill( p, st + 1, s );
            MemoryBarrier();
            InterlockedExchange( &h->state[s], st + 1 );
        }
    }
    return 0;
}

/* views that come and go: map the section again somewhere else, touch it, unmap */
static DWORD WINAPI view_thread( void *arg )
{
    struct header *h = (struct header *)base;
    unsigned int seed = 77 + is_child;

    while (!h->stop)
    {
        BYTE *view = MapViewOfFile( section, FILE_MAP_ALL_ACCESS, 0, 0, SECTION );
        int s;
        if (!view) { printf( "MapViewOfFile failed %lu\n", GetLastError() ); break; }
        seed = seed * 1103515245 + 12345;
        s = (seed >> 8) % NB_SLOTS;
        /* read through the second view while the slot threads use the first one */
        (void)*(volatile DWORD *)(view + 0x1000 + SLOT * s + ((seed >> 4) & (SLOT - 4) & ~3));
        UnmapViewOfFile( view );
    }
    return 0;
}

int main( int argc, char **argv )
{
    struct header *h;
    HANDLE threads[8], child = NULL;
    int n = 0, i;
    unsigned int seconds = 10;

    if (argc == 3 && !strcmp( argv[1], "child" ))
    {
        is_child = 1;
        section = (HANDLE)(ULONG_PTR)strtoull( argv[2], NULL, 16 );
    }
    else
    {
        SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
        char cmd[MAX_PATH + 64], self[MAX_PATH];
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi;

        if (argc > 1) seconds = atoi( argv[1] );
        section = CreateFileMappingA( INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, SECTION, NULL );
        GetModuleFileNameA( NULL, self, sizeof(self) );
        snprintf( cmd, sizeof(cmd), "\"%s\" child %Ix", self, (ULONG_PTR)section );
        base = MapViewOfFile( section, FILE_MAP_ALL_ACCESS, 0, 0, SECTION );
        memset( base, 0, sizeof(struct header) );
        if (!CreateProcessA( NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi ))
        {
            printf( "CreateProcess failed %lu\n", GetLastError() );
            return 2;
        }
        child = pi.hProcess;
        CloseHandle( pi.hThread );
    }
    if (!base) base = MapViewOfFile( section, FILE_MAP_ALL_ACCESS, 0, 0, SECTION );
    h = (struct header *)base;

    for (i = 0; i < 2; i++) threads[n++] = CreateThread( NULL, 0, slot_thread, (void *)(ULONG_PTR)i, 0, NULL );
    threads[n++] = CreateThread( NULL, 0, view_thread, NULL, 0, NULL );

    if (is_child)
    {
        WaitForMultipleObjects( n, threads, TRUE, INFINITE );
        return 0;
    }
    Sleep( seconds * 1000 );
    h->stop = 1;
    WaitForMultipleObjects( n, threads, TRUE, 10000 );
    if (WaitForSingleObject( child, 10000 )) TerminateProcess( child, 1 );
    printf( "%ld slot checks, %ld errors\n", h->checks, h->errors );
    printf( "shmstress: %s\n", h->errors ? "FAILED" : "ok" );
    return h->errors ? 1 : 0;
}
