/* Barrique: CRT functions that x86-64 code calls in Wine's ARM64EC CRT, with
 * x86-64 callbacks or struct returns: qsort, bsearch, div, ldiv, setjmp/longjmp,
 * memcmp, strstr. Built for x86_64 (UCRT) it runs through ARM64EC and FEX.
 * Prints each check and "crtcallbacks: ok" or "crtcallbacks: FAILED".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

static int bad, calls;

static void check( int ok, const char *what )
{
    printf( "%-36s %s\n", what, ok ? "ok" : "FAILED" );
    if (!ok) bad = 1;
}

static int cmp_int( const void *a, const void *b )
{
    int x = *(const int *)a, y = *(const int *)b;
    calls++;
    return x < y ? -1 : x > y;
}

static int cmp_str( const void *a, const void *b )
{
    calls++;
    return strcmp( *(const char * const *)a, *(const char * const *)b );
}

struct entry { const char *key; int value; };
static int cmp_entry( const void *a, const void *b )
{
    calls++;
    return strcmp( ((const struct entry *)a)->key, ((const struct entry *)b)->key );
}

static jmp_buf jb;
static void jump( int v ) { longjmp( jb, v ); }

int main( void )
{
    int v[1000], i, sorted = 1, *found, missing = 0;
    const char *words[] = { "pear", "apple", "fig", "kiwi", "banana", "cherry", "date", "grape" };
    struct entry table[200], key, *e;
    char names[200][24];
    volatile int stage = 0;
    div_t d; ldiv_t ld;

    for (i = 0; i < 1000; i++) v[i] = (i * 7919) % 1000;
    qsort( v, 1000, sizeof(int), cmp_int );
    for (i = 1; i < 1000; i++) if (v[i - 1] > v[i]) sorted = 0;
    check( sorted && calls > 1000, "qsort ints" );

    qsort( words, 8, sizeof(words[0]), cmp_str );
    check( !strcmp( words[0], "apple" ) && !strcmp( words[7], "pear" ), "qsort strings" );

    for (i = 0; i < 1000; i++)
    {
        int k = i;
        found = bsearch( &k, v, 1000, sizeof(int), cmp_int );
        if (!found || *found != i) missing++;
    }
    check( !missing, "bsearch ints" );

    /* a contract-id style table: sort by string, look every key up */
    for (i = 0; i < 200; i++)
    {
        snprintf( names[i], sizeof(names[i]), "@mozilla.org/x%03d;1", (i * 37) % 200 );
        table[i].key = names[i];
        table[i].value = (i * 37) % 200;
    }
    qsort( table, 200, sizeof(table[0]), cmp_entry );
    missing = 0;
    for (i = 0; i < 200; i++)
    {
        char buf[24];
        snprintf( buf, sizeof(buf), "@mozilla.org/x%03d;1", i );
        key.key = buf;
        e = bsearch( &key, table, 200, sizeof(table[0]), cmp_entry );
        if (!e || e->value != i) missing++;
    }
    check( !missing, "qsort + bsearch string table" );

    d = div( -17, 5 );
    check( d.quot == -3 && d.rem == -2, "div" );
    ld = ldiv( 1234567L, 1000L );
    check( ld.quot == 1234 && ld.rem == 567, "ldiv" );

    check( memcmp( "abc", "abd", 3 ) < 0 && memcmp( "abd", "abc", 3 ) > 0 && !memcmp( "x", "x", 1 ), "memcmp" );
    check( strstr( "docloaderservice", "loader" ) == (char *)"docloaderservice" + 3 ||
           !strcmp( strstr( "docloaderservice", "loader" ), "loaderservice" ), "strstr" );

    if (!setjmp( jb ))
    {
        stage = 1;
        jump( 42 );
        stage = 2;
    }
    check( stage == 1, "setjmp/longjmp" );

    printf( "crtcallbacks: %s\n", bad ? "FAILED" : "ok" );
    return bad;
}
