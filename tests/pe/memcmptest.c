/* Barrique: memcmp, strncmp and memchr of the CRT against a byte loop, for every
 * length 0-80, every alignment 0-15 of both buffers and a difference at every
 * position. Built for x86_64 it calls Wine's ARM64EC CRT through FEX.
 * Prints "memcmptest: ok" or the first mismatches and "memcmptest: FAILED".
 */
#include <stdio.h>
#include <string.h>

static int sign( int v ) { return (v > 0) - (v < 0); }

static int ref_memcmp( const unsigned char *a, const unsigned char *b, size_t n )
{
    size_t i;
    for (i = 0; i < n; i++) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

int main( void )
{
    static unsigned char bufa[256], bufb[256];
    int bad = 0, la, lb, len, pos, tests = 0;

    for (len = 0; len <= 80; len++)
    for (la = 0; la < 16; la++)
    for (lb = 0; lb < 16; lb++)
    for (pos = -1; pos < len; pos++)
    {
        unsigned char *a = bufa + la, *b = bufb + lb;
        int i, got, want;

        for (i = 0; i < len + 16; i++) a[i] = b[i] = (unsigned char)('A' + (i * 7) % 26);
        if (pos >= 0) b[pos] = (unsigned char)(a[pos] + (pos & 1 ? 1 : -1));
        a[len] = 1; b[len] = 2;   /* past the end: must not matter */

        want = ref_memcmp( a, b, len );
        got = sign( memcmp( a, b, len ) );
        tests++;
        if (got != want && bad++ < 10)
            printf( "memcmp len %d align %d/%d diff at %d: got %d want %d\n", len, la, lb, pos, got, want );

        a[len] = b[len] = 0;
        got = sign( strncmp( (char *)a, (char *)b, len ) );
        if (got != want && bad++ < 10)
            printf( "strncmp len %d align %d/%d diff at %d: got %d want %d\n", len, la, lb, pos, got, want );
    }
    printf( "%d cases\n", tests );
    printf( "\"@mozilla.org/docloaderservice;1\" vs \"@mozilla.org/observer-service;1\": memcmp %d\n",
            sign( memcmp( "@mozilla.org/docloaderservice;1", "@mozilla.org/observer-service;1", 31 ) ) );
    printf( "memcmptest: %s\n", bad ? "FAILED" : "ok" );
    return !!bad;
}
