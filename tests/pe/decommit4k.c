/* Barrique: a Windows page (4K) decommitted inside a host page (16K on Apple
 * Silicon) must read as zero when it is committed again, as on Windows, and the
 * other pages of that host page must keep their data. mozjemalloc (Firefox)
 * decommits single pages and counts on getting zeros back.
 *
 *   decommit4k.exe      prints each check and "decommit4k: ok" or "decommit4k: FAILED"
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check( BOOL ok, const char *what )
{
    printf( "%-48s %s\n", what, ok ? "ok" : "FAILED" );
    if (!ok) failures++;
}

static BOOL all( const BYTE *p, size_t size, BYTE value )
{
    for (size_t i = 0; i < size; i++) if (p[i] != value) return FALSE;
    return TRUE;
}

int main( void )
{
    BYTE *p = VirtualAlloc( NULL, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );

    memset( p, 0xab, 0x10000 );

    /* one page inside a host page */
    VirtualFree( p + 0x1000, 0x1000, MEM_DECOMMIT );
    VirtualAlloc( p + 0x1000, 0x1000, MEM_COMMIT, PAGE_READWRITE );
    check( all( p + 0x1000, 0x1000, 0 ), "one page recommitted reads zero" );
    check( all( p, 0x1000, 0xab ) && all( p + 0x2000, 0x2000, 0xab ), "its neighbours keep their data" );
    memset( p + 0x1000, 0x5a, 0x1000 );
    check( all( p + 0x1000, 0x1000, 0x5a ), "recommitted page is writable" );

    /* two pages, committed again read-only */
    VirtualFree( p + 0x1000, 0x2000, MEM_DECOMMIT );
    VirtualAlloc( p + 0x1000, 0x2000, MEM_COMMIT, PAGE_READONLY );
    check( all( p + 0x1000, 0x2000, 0 ), "two pages recommitted read-only read zero" );
    check( p[0] == 0xab && p[0x3000] == 0xab, "their neighbours keep their data" );

    /* a range across a host page boundary, no whole host page in it */
    VirtualFree( p + 0x3000, 0x2000, MEM_DECOMMIT );
    VirtualAlloc( p + 0x3000, 0x2000, MEM_COMMIT, PAGE_READWRITE );
    check( all( p + 0x3000, 0x2000, 0 ), "range across a host page boundary reads zero" );
    check( all( p + 0x5000, 0x3000, 0xab ), "the next host page keeps the rest" );

    /* a whole host page and pieces around it */
    VirtualFree( p + 0x7000, 0x6000, MEM_DECOMMIT );
    VirtualAlloc( p + 0x7000, 0x6000, MEM_COMMIT, PAGE_READWRITE );
    check( all( p + 0x7000, 0x6000, 0 ), "whole host page plus pieces reads zero" );
    check( all( p + 0xd000, 0x3000, 0xab ), "pages after it keep their data" );

    printf( "decommit4k: %s\n", failures ? "FAILED" : "ok" );
    return failures ? 1 : 0;
}
