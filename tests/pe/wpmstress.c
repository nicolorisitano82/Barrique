/* Barrique: WriteProcessMemory / ReadProcessMemory coherence with a child
 * process that runs in its own VM under the Hypervisor.framework backend.
 *
 * On macOS the wineserver writes into another process with mach_vm_write,
 * which for whole pages may hand the target new physical pages instead of
 * copying into the old ones. The child's VM must then see the new data, not a
 * stale page: a parent fills a buffer of the child with the generation number,
 * the child (Windows code, so in the VM) waits for it and checks every word,
 * then writes the complement back for the parent to read.
 *
 * Sizes and offsets vary: whole 64K, 16K host page, 4K, unaligned.
 *
 *   wpmstress.exe [generations]
 *
 * Prints counters and "wpmstress: ok" or "wpmstress: FAILED"; exit code 0 or 1.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_SIZE  0x20000
#define CTRL_WORDS 16   /* word 0 generation, 1 offset, 2 size (bytes) */

struct layout { DWORD offset, size; };
static const struct layout layouts[] =
{
    { 0x10000, 0x10000 },   /* whole 64K, page aligned */
    { 0x4000,  0x4000 },    /* one 16K host page */
    { 0x8000,  0x1000 },    /* one 4K guest page */
    { 0x10010, 0x3ff0 },    /* unaligned start */
    { 0x0,     0x20000 },   /* everything */
};
#define NLAYOUTS (sizeof(layouts) / sizeof(layouts[0]))

static int child( void )
{
    HANDLE ready = OpenEventA( EVENT_ALL_ACCESS, FALSE, "wpmstress_ready" );
    HANDLE go = OpenEventA( EVENT_ALL_ACCESS, FALSE, "wpmstress_go" );
    HANDLE ack = OpenEventA( EVENT_ALL_ACCESS, FALSE, "wpmstress_ack" );
    volatile DWORD *ctrl;
    DWORD *buf;

    HANDLE info = OpenFileMappingA( FILE_MAP_ALL_ACCESS, FALSE, "wpmstress_info" );
    DWORD_PTR *addrs = info ? MapViewOfFile( info, FILE_MAP_ALL_ACCESS, 0, 0, 0 ) : NULL;

    /* any address: below 4GB Barrique hands out a different (aliased) one */
    buf = VirtualAlloc( NULL, BUF_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
    if (!buf || !addrs || !ready || !go || !ack)
    {
        printf( "child: buffer %p events %p %p %p, error %lu\n", buf, ready, go, ack, GetLastError() );
        fflush( stdout );
        return 2;
    }
    ctrl = VirtualAlloc( NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
    memset( buf, 0xee, BUF_SIZE );          /* touch everything: mapped in the VM */
    /* tell the parent where the buffer and the control block are */
    addrs[0] = (DWORD_PTR)buf;
    addrs[1] = (DWORD_PTR)ctrl;
    SetEvent( ready );

    for (;;)
    {
        DWORD gen, off, size, i, bad = 0, first = 0;
        const DWORD *p;

        WaitForSingleObject( go, INFINITE );
        gen = ctrl[0]; off = ctrl[1]; size = ctrl[2];
        if (!gen) break;
        p = (const DWORD *)((char *)buf + off);
        for (i = 0; i < size / 4; i++)
            if (((volatile const DWORD *)p)[i] != gen) { if (!bad++) first = i; }
        ctrl[3] = bad;
        ctrl[4] = first;
        ctrl[5] = bad ? p[first] : 0;
        /* write back the complement for the parent to read */
        for (i = 0; i < size / 4; i++) ((DWORD *)p)[i] = ~gen;
        SetEvent( ack );
    }
    return 0;
}

int main( int argc, char **argv )
{
    int gens = argc > 1 ? atoi( argv[1] ) : 2000;
    char cmd[MAX_PATH + 32], self[MAX_PATH];
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    HANDLE ready, go, ack, info;
    DWORD_PTR ctrl_addr, *addrs;
    char *target;
    DWORD *src, *back;
    int g, stale = 0, readbad = 0, apierr = 0;
    SIZE_T done;

    if (argc > 1 && !strcmp( argv[1], "child" )) return child();

    info = CreateFileMappingA( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, "wpmstress_info" );
    addrs = MapViewOfFile( info, FILE_MAP_ALL_ACCESS, 0, 0, 0 );
    ready = CreateEventA( NULL, FALSE, FALSE, "wpmstress_ready" );
    go = CreateEventA( NULL, FALSE, FALSE, "wpmstress_go" );
    ack = CreateEventA( NULL, FALSE, FALSE, "wpmstress_ack" );
    GetModuleFileNameA( NULL, self, sizeof(self) );
    snprintf( cmd, sizeof(cmd), "\"%s\" child", self );
    if (!CreateProcessA( self, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ))
    {
        printf( "CreateProcess failed %lu\n", GetLastError() );
        return 1;
    }
    if (WaitForSingleObject( ready, 10000 ))
    {
        printf( "child not ready\n" );
        TerminateProcess( pi.hProcess, 1 );
        return 1;
    }
    target = (char *)addrs[0];
    ctrl_addr = addrs[1];

    src = VirtualAlloc( NULL, BUF_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );   /* page aligned */
    back = VirtualAlloc( NULL, BUF_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );

    for (g = 1; g <= gens; g++)
    {
        const struct layout *l = &layouts[g % NLAYOUTS];
        DWORD ctrl[6] = { g, l->offset, l->size }, i;

        for (i = 0; i < l->size / 4; i++) src[i] = g;
        if (!WriteProcessMemory( pi.hProcess, target + l->offset, src, l->size, &done ) ||
            done != l->size) apierr++;
        if (!WriteProcessMemory( pi.hProcess, (void *)ctrl_addr, ctrl, 3 * sizeof(DWORD), &done )) apierr++;
        SetEvent( go );
        if (WaitForSingleObject( ack, 10000 ))
        {
            printf( "gen %d: child did not answer\n", g );
            apierr++;
            break;
        }
        if (!ReadProcessMemory( pi.hProcess, (void *)ctrl_addr, ctrl, sizeof(ctrl), &done )) apierr++;
        if (ctrl[3])
        {
            if (stale++ < 10)
                printf( "gen %d off %#lx size %#lx: child saw %lu stale words, first word %lu = %#lx\n",
                        g, l->offset, l->size, ctrl[3], ctrl[4], ctrl[5] );
        }
        if (!ReadProcessMemory( pi.hProcess, target + l->offset, back, l->size, &done )) apierr++;
        for (i = 0; i < l->size / 4; i++)
            if (back[i] != ~(DWORD)g)
            {
                if (readbad++ < 10)
                    printf( "gen %d off %#lx size %#lx: parent read word %lu = %#lx, expected %#lx\n",
                            g, l->offset, l->size, i, back[i], ~(DWORD)g );
                break;
            }
    }

    {
        DWORD stop[3] = { 0 };
        WriteProcessMemory( pi.hProcess, (void *)ctrl_addr, stop, sizeof(stop), &done );
        SetEvent( go );
        WaitForSingleObject( pi.hProcess, 10000 );
    }
    printf( "generations %d, child saw stale data %d, parent read stale data %d, api errors %d\n",
            g - 1, stale, readbad, apierr );
    printf( "wpmstress: %s\n", stale || readbad || apierr ? "FAILED" : "ok" );
    return stale || readbad || apierr ? 1 : 0;
}
