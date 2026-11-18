/* Barrique: memory coherence stress test for the Hypervisor.framework backend.
 * Several threads, each on its own vCPU, share memory that keeps changing:
 *
 *  decommit  blocks are committed, filled, handed to another thread (atomics
 *            only, no syscall) that checks the contents, then decommitted and
 *            committed again: a recommitted page must read as zero, and
 *            nobody may see data of an earlier generation;
 *  protect   writer threads store to a page in a tight loop while another
 *            thread makes it read-only with VirtualProtect: once the call has
 *            returned no store may land any more (FEX relies on this to see
 *            changes to x86 code, so does any JIT); the writers' faults are
 *            handled and retried once the page is writable again.
 *
 *   memstress.exe [seconds] [threads]
 *
 * Prints counters and "memstress: ok" or "memstress: FAILED"; exit code 0 or 1.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define BLOCK      0x10000
#define NB_BLOCKS  64
#define PAGE       0x1000

static volatile LONG stop;
static LONG errors;

/* ---- decommit/recommit ---- */

struct block
{
    volatile LONG owner;        /* -1 free, else the thread that holds it */
    volatile LONG stage;        /* 0 empty, 1 filled by owner, waiting for a checker */
    volatile LONG gen;
    volatile LONG filler;
    BYTE *mem;
};

static struct block blocks[NB_BLOCKS];
static LONG commits, checks, handoffs;

static void fail( const char *what, int id, LONG gen, size_t off, unsigned int got, unsigned int expect )
{
    if (InterlockedIncrement( &errors ) <= 20)
        printf( "%s: block %d gen %ld offset %#zx: got %#x expected %#x\n", what, id, gen, off, got, expect );
}

static DWORD WINAPI decommit_thread( void *arg )
{
    LONG me = (LONG)(ULONG_PTR)arg;
    unsigned int seed = 1234 + me;

    while (!stop)
    {
        int id;
        struct block *b;
        size_t i;

        seed = seed * 1103515245 + 12345;
        id = (seed >> 8) % NB_BLOCKS;
        b = &blocks[id];
        if (InterlockedCompareExchange( &b->owner, me, -1 ) != -1) continue;

        if (b->stage == 1 && b->filler != me)
        {
            /* filled by another thread: check it, as a reader on another vCPU */
            DWORD *p = (DWORD *)b->mem;
            DWORD expect = (DWORD)(b->gen * 0x9e3779b1u) ^ (DWORD)b->filler;
            for (i = 0; i < BLOCK / 4; i += 61)
                if (p[i] != (expect ^ (DWORD)i)) { fail( "handoff", id, b->gen, i * 4, p[i], expect ^ (DWORD)i ); break; }
            InterlockedIncrement( &checks );
            InterlockedIncrement( &handoffs );
            /* give it back to the system, partly: 4K pages inside the 16K host pages */
            if (seed & 0x100)
                VirtualFree( b->mem, BLOCK, MEM_DECOMMIT );
            else
                for (i = 0; i < BLOCK; i += PAGE * ((seed >> 12) % 3 + 1))
                    VirtualFree( b->mem + i, PAGE, MEM_DECOMMIT );
            VirtualFree( b->mem, BLOCK, MEM_DECOMMIT );
            b->stage = 0;
        }
        else if (b->stage == 0)
        {
            DWORD *p;
            DWORD expect;
            if (!VirtualAlloc( b->mem, BLOCK, MEM_COMMIT, PAGE_READWRITE ))
            {
                fail( "commit", id, b->gen, 0, GetLastError(), 0 );
                b->owner = -1;
                continue;
            }
            InterlockedIncrement( &commits );
            p = (DWORD *)b->mem;
            for (i = 0; i < BLOCK / 4; i += 61)
                if (p[i]) { fail( "not zero after recommit", id, b->gen, i * 4, p[i], 0 ); break; }
            b->gen++;
            b->filler = me;
            expect = (DWORD)(b->gen * 0x9e3779b1u) ^ (DWORD)me;
            for (i = 0; i < BLOCK / 4; i++) p[i] = expect ^ (DWORD)i;
            MemoryBarrier();
            b->stage = 1;
        }
        InterlockedExchange( &b->owner, -1 );
    }
    return 0;
}

/* ---- protect race ---- */

static BYTE *prot_page;
static volatile LONG page_writable = 1;
static LONG stores, faults, protects, late_stores;

static LONG CALLBACK fault_handler( EXCEPTION_POINTERS *ep )
{
    EXCEPTION_RECORD *rec = ep->ExceptionRecord;

    if (rec->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || rec->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    if ((BYTE *)rec->ExceptionInformation[1] < prot_page || (BYTE *)rec->ExceptionInformation[1] >= prot_page + PAGE)
        return EXCEPTION_CONTINUE_SEARCH;
    InterlockedIncrement( &faults );
    while (!page_writable && !stop) YieldProcessor();
    return EXCEPTION_CONTINUE_EXECUTION;
}

static DWORD WINAPI writer_thread( void *arg )
{
    volatile LONG *slot = (volatile LONG *)(prot_page + 64 * (ULONG_PTR)arg);
    LONG n = 0;

    while (!stop)
    {
        *slot = ++n;
        if (!(n & 0xffff)) InterlockedAdd( &stores, 0x10000 );
    }
    return 0;
}

static DWORD WINAPI protect_thread( void *arg )
{
    int nb_writers = (int)(ULONG_PTR)arg;
    DWORD old;
    int i;

    while (!stop)
    {
        LONG before[64], after[64];

        page_writable = 0;
        if (!VirtualProtect( prot_page, PAGE, PAGE_READONLY, &old )) fail( "VirtualProtect", 0, 0, 0, GetLastError(), 0 );
        InterlockedIncrement( &protects );
        /* read-only now: the writers fault and wait, the values must stay put */
        for (i = 0; i < nb_writers; i++) before[i] = *(volatile LONG *)(prot_page + 64 * i);
        Sleep( 2 );
        for (i = 0; i < nb_writers; i++) after[i] = *(volatile LONG *)(prot_page + 64 * i);
        for (i = 0; i < nb_writers; i++)
            if (before[i] != after[i])
            {
                InterlockedIncrement( &late_stores );
                fail( "store after VirtualProtect(PAGE_READONLY) returned", i, protects, 64 * i, after[i], before[i] );
            }
        VirtualProtect( prot_page, PAGE, PAGE_READWRITE, &old );
        page_writable = 1;
        Sleep( 1 );
    }
    return 0;
}

int main( int argc, char **argv )
{
    unsigned int seconds = argc > 1 ? atoi( argv[1] ) : 10;
    int nb_threads = argc > 2 ? atoi( argv[2] ) : 6;
    HANDLE threads[64];
    BYTE *base;
    int i, n = 0, nb_writers = 3;

    base = VirtualAlloc( NULL, BLOCK * NB_BLOCKS, MEM_RESERVE, PAGE_NOACCESS );
    for (i = 0; i < NB_BLOCKS; i++)
    {
        blocks[i].owner = -1;
        blocks[i].mem = base + BLOCK * i;
    }
    prot_page = VirtualAlloc( NULL, PAGE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
    AddVectoredExceptionHandler( 1, fault_handler );

    for (i = 0; i < nb_threads && n < 60; i++)
        threads[n++] = CreateThread( NULL, 0, decommit_thread, (void *)(ULONG_PTR)i, 0, NULL );
    for (i = 0; i < nb_writers; i++)
        threads[n++] = CreateThread( NULL, 0, writer_thread, (void *)(ULONG_PTR)i, 0, NULL );
    threads[n++] = CreateThread( NULL, 0, protect_thread, (void *)(ULONG_PTR)nb_writers, 0, NULL );

    Sleep( seconds * 1000 );
    stop = 1;
    page_writable = 1;
    WaitForMultipleObjects( n, threads, TRUE, 10000 );

    printf( "decommit: %ld commits, %ld handoffs checked\n", commits, checks );
    printf( "protect: %ld protects, %ld faults, ~%ld stores, %ld late stores\n", protects, faults, stores, late_stores );
    printf( "errors %ld\n", errors );
    printf( "memstress: %s\n", errors ? "FAILED" : "ok" );
    return errors ? 1 : 0;
}
