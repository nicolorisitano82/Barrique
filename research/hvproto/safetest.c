/*
 * Classify host pages with the same check hvf.c runs before hv_vm_map.
 * Read-only queries: no VM is created and nothing is mapped into one.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/wait.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

#define HOST_PAGE 0x4000ULL

static int host_page_is_safe( uint64_t page, char *why, size_t len )
{
    vm_region_extended_info_data_t info;
    mach_msg_type_number_t count = VM_REGION_EXTENDED_INFO_COUNT;
    mach_vm_address_t addr = page;
    mach_vm_size_t size = 0;
    mach_port_t object = MACH_PORT_NULL;
    kern_return_t kr;

    kr = mach_vm_region( mach_task_self(), &addr, &size, VM_REGION_EXTENDED_INFO,
                         (vm_region_info_t)&info, &count, &object );
    if (kr != KERN_SUCCESS || addr > page || addr + size < page + HOST_PAGE)
    {
        snprintf( why, len, "not mapped" );
        return 0;
    }
    snprintf( why, len, "pager %u prot %x share %u", info.external_pager, info.protection, info.share_mode );
    if (info.external_pager || (info.protection & VM_PROT_EXECUTE) || !(info.protection & VM_PROT_READ) ||
        (info.share_mode != SM_PRIVATE && info.share_mode != SM_EMPTY &&
         info.share_mode != SM_PRIVATE_ALIASED))
        return 0;
    return 1;
}

static void check( const char *name, void *p, int expect )
{
    char why[128];
    int ok = host_page_is_safe( (uint64_t)p & ~(HOST_PAGE - 1), why, sizeof(why) );
    printf( "%-38s %-8s (%s) %s\n", name, ok ? "ACCEPT" : "REFUSE", why, ok == expect ? "ok" : "<-- UNEXPECTED" );
}

int main( int argc, char **argv )
{
    const char *file = argc > 1 ? argv[1] : argv[0];
    int fd = open( file, O_RDONLY );
    char *p;

    p = mmap( NULL, HOST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
    check( "anon RW, untouched", p, 1 );
    p[0] = 1;
    check( "anon RW, written", p, 1 );
    mprotect( p, HOST_PAGE, PROT_READ );
    check( "anon RW -> RO", p, 1 );
    mprotect( p, HOST_PAGE, PROT_READ | PROT_EXEC );
    check( "anon written -> RX", p, 0 );

    p = mmap( NULL, HOST_PAGE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0 );
    check( "anon PROT_NONE", p, 0 );

    p = mmap( NULL, HOST_PAGE, PROT_READ, MAP_PRIVATE, fd, 0 );
    check( "file MAP_PRIVATE RO", p, 0 );
    p = mmap( NULL, HOST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0 );
    p[0] ^= 0;
    check( "file MAP_PRIVATE written (COW)", p, 0 );
    p = mmap( NULL, HOST_PAGE, PROT_READ, MAP_SHARED, fd, 0 );
    check( "file MAP_SHARED RO", p, 0 );

    p = mmap( NULL, HOST_PAGE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0 );
    p[0] = 1;
    check( "anon MAP_SHARED (single mapping)", p, 1 );

    /* what virtual.c does for images under HVF: pread into anonymous memory */
    p = mmap( NULL, 4 * HOST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
    pread( fd, p, 4 * HOST_PAGE, 0 );
    check( "pread into aligned anon", p, 1 );
    mprotect( p, 4 * HOST_PAGE, PROT_READ );
    check( "pread into aligned anon -> RO", p, 1 );
    {
        static char bounce[4 * HOST_PAGE + 16];
        char *q = mmap( NULL, 4 * HOST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
        pread( fd, bounce + 16, 4 * HOST_PAGE, 0 );
        memcpy( q, bounce + 16, 4 * HOST_PAGE );
        mprotect( q, 4 * HOST_PAGE, PROT_READ );
        check( "pread via bounce buffer + memcpy -> RO", q, 1 );
    }

    /* what Wine does to its views: one anonymous region, protections changed on part of it */
    p = mmap( NULL, 4 * HOST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
    memset( p, 1, 4 * HOST_PAGE );
    mprotect( p + HOST_PAGE, HOST_PAGE, PROT_READ );
    check( "anon region split by mprotect: RO part", p + HOST_PAGE, 1 );
    check( "anon region split by mprotect: RW part", p, 1 );

    /* what fork() (wineserver startup) does to existing private memory */
    {
        char *c = mmap( NULL, 2 * HOST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
        pid_t pid;
        memset( c, 1, 2 * HOST_PAGE );
        mprotect( c, 2 * HOST_PAGE, PROT_READ );
        if (!(pid = fork())) _exit( 0 );
        waitpid( pid, NULL, 0 );
        check( "anon RO after fork (COW)", c, 0 );
        mach_vm_protect( mach_task_self(), (mach_vm_address_t)c, HOST_PAGE, FALSE, VM_PROT_READ | VM_PROT_COPY );
        check( "  ... after VM_PROT_COPY (not enough)", c, 0 );
        check( "  ... other page untouched", c + HOST_PAGE, 0 );
        printf( "  data still intact: %s\n", c[0] == 1 && c[HOST_PAGE - 1] == 1 ? "yes" : "NO" );
    }

    /* same, but marked not inheritable first: what hvf_mmap would do */
    {
        char *c = mmap( NULL, 2 * HOST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
        pid_t pid;
        minherit( c, 2 * HOST_PAGE, VM_INHERIT_NONE );
        memset( c, 1, 2 * HOST_PAGE );
        mprotect( c, HOST_PAGE, PROT_READ );
        if (!(pid = fork())) _exit( 0 );
        waitpid( pid, NULL, 0 );
        check( "minherit NONE, RO part after fork", c, 1 );
        check( "minherit NONE, RW part after fork", c + HOST_PAGE, 1 );
        mprotect( c, 2 * HOST_PAGE, PROT_READ | PROT_WRITE );
        check( "  ... after later mprotect", c, 1 );
        printf( "  data still intact: %s\n", c[0] == 1 && c[2 * HOST_PAGE - 1] == 1 ? "yes" : "NO" );
    }

    /* large pread, like loading ntdll.dll */
    {
        size_t big = 4 << 20;
        char *c = mmap( NULL, big, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
        ssize_t n = pread( fd, c, big, 0 );
        printf( "  (read %zd bytes)\n", n );
        check( "large pread, first page", c, 1 );
        check( "large pread, middle page", c + (n / 2 & ~(HOST_PAGE - 1)), 1 );
        mprotect( c, big, PROT_READ );
        check( "large pread -> RO, middle page", c + (n / 2 & ~(HOST_PAGE - 1)), 1 );
    }

    /* large page-aligned memcpy/memmove between anonymous buffers */
    {
        size_t big = 1 << 20;
        char *src = mmap( NULL, big, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
        char *dst = mmap( NULL, big, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
        char *dst2 = mmap( NULL, big, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
        memset( src, 7, big );
        memcpy( dst, src, big );
        check( "large aligned memcpy, destination", dst + big / 2, 1 );
        check( "large aligned memcpy, source", src + big / 2, 1 );
        memmove( dst2, src, big );
        check( "large aligned memmove, destination", dst2 + big / 2, 1 );
        mach_vm_copy( mach_task_self(), (mach_vm_address_t)src, big, (mach_vm_address_t)dst2 );
        check( "mach_vm_copy, destination", dst2 + big / 2, 0 );
    }

    /* Wine on macOS: reserve with mach_vm_map, commit with mprotect, then fork */
    {
        int inh;
        for (inh = 0; inh < 2; inh++)
        {
            mach_vm_address_t a = 0;
            pid_t pid;
            mach_vm_map( mach_task_self(), &a, 4 * HOST_PAGE, 0, VM_FLAGS_ANYWHERE, MEMORY_OBJECT_NULL, 0, 0,
                         VM_PROT_NONE, VM_PROT_ALL, inh ? VM_INHERIT_NONE : VM_INHERIT_COPY );
            mprotect( (void *)a, 2 * HOST_PAGE, PROT_READ | PROT_WRITE );
            memset( (void *)a, 3, 2 * HOST_PAGE );
            mprotect( (void *)a, HOST_PAGE, PROT_READ );
            if (!(pid = fork())) _exit( 0 );
            waitpid( pid, NULL, 0 );
            check( inh ? "mach_vm_map INHERIT_NONE + fork, RO" : "mach_vm_map INHERIT_COPY + fork, RO",
                   (void *)a, inh ? 1 : 0 );
        }
    }

    /* Wine's image loading: reserved area, commit RW, header written, pread of the rest */
    {
        size_t big = 4 << 20;
        mach_vm_address_t a = 0;
        char *c;
        mach_vm_map( mach_task_self(), &a, big, 0, VM_FLAGS_ANYWHERE, MEMORY_OBJECT_NULL, 0, 0,
                     VM_PROT_NONE, VM_PROT_ALL, VM_INHERIT_NONE );
        c = (char *)a;
        mprotect( c, big, PROT_READ | PROT_WRITE );
        pread( fd, c, HOST_PAGE, 0 );            /* header */
        memset( c + 0x400, 0, HOST_PAGE - 0x400 );
        pread( fd, c + 0x10000, big - 0x10000, 0x10000 ); /* sections */
        mprotect( c + 0x10000, big - 0x10000, PROT_READ );
        check( "reserved+commit+pread, section page", c + 0x40000, 1 );
        check( "reserved+commit+pread, header page", c, 1 );
    }

    /* making a copy-on-write anonymous page private again */
    {
        size_t big = 4 * HOST_PAGE;
        char *src = mmap( NULL, big, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
        char *dst = mmap( NULL, big, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
        char *tmp = malloc( HOST_PAGE );
        memset( src, 9, big );
        mach_vm_copy( mach_task_self(), (mach_vm_address_t)src, big, (mach_vm_address_t)dst );
        mprotect( dst, big, PROT_READ );
        check( "COW page (via mach_vm_copy), RO", dst, 0 );

        /* (a) write the same data back through a temporary RW mapping */
        mprotect( dst, HOST_PAGE, PROT_READ | PROT_WRITE );
        __atomic_fetch_or( (volatile uint64_t *)dst, 0, __ATOMIC_SEQ_CST );
        mprotect( dst, HOST_PAGE, PROT_READ );
        check( "(a) after write-touch", dst, 0 );

        /* (b) replace with a fresh anonymous page holding the same data */
        memcpy( tmp, dst + HOST_PAGE, HOST_PAGE );
        mmap( dst + HOST_PAGE, HOST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0 );
        memcpy( dst + HOST_PAGE, tmp, HOST_PAGE );
        mprotect( dst + HOST_PAGE, HOST_PAGE, PROT_READ );
        check( "(b) after replace with fresh anon", dst + HOST_PAGE, 1 );
        printf( "  data intact: %s\n", dst[HOST_PAGE] == 9 && dst[2 * HOST_PAGE - 1] == 9 ? "yes" : "NO" );
        check( "  neighbour page still COW", dst + 2 * HOST_PAGE, 0 );
        free( tmp );
    }

    /* page-level view: where does each page physically live in the COW chain? */
    {
        size_t big = 4 * HOST_PAGE;
        char *src = mmap( NULL, big, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
        char *dst = mmap( NULL, big, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
        int i;
        memset( src, 5, big );
        mach_vm_copy( mach_task_self(), (mach_vm_address_t)src, big, (mach_vm_address_t)dst );
        __atomic_fetch_or( (volatile uint64_t *)(dst + HOST_PAGE), 0, __ATOMIC_SEQ_CST );  /* touch page 1 only */
        mprotect( dst, big, PROT_READ );
        for (i = 0; i < 4; i++)
        {
            vm_page_info_basic_data_t pi;
            mach_msg_type_number_t cnt = VM_PAGE_INFO_BASIC_COUNT;
            kern_return_t kr = mach_vm_page_info( mach_task_self(), (mach_vm_address_t)(dst + i * HOST_PAGE),
                                                  VM_PAGE_INFO_BASIC, (vm_page_info_t)&pi, &cnt );
            printf( "  page %d: kr %d depth %d disposition %#x ref %d   (expect depth %d)\n",
                    i, kr, pi.depth, pi.disposition, pi.ref_count, i == 1 ? 0 : 1 );
        }
        check( "region summary after touching one page", dst + HOST_PAGE, 0 );
    }

    /* truly shared with another process, then replaced by a private copy */
    {
        char *c = mmap( NULL, 2 * HOST_PAGE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0 );
        char tmp[HOST_PAGE];
        int pfd[2];
        pid_t pid;
        memset( c, 6, 2 * HOST_PAGE );
        pipe( pfd );
        if (!(pid = fork())) { char x; read( pfd[0], &x, 1 ); _exit( 0 ); }  /* child keeps the mapping */
        check( "anon MAP_SHARED with a live child", c, 0 );
        memcpy( tmp, c, HOST_PAGE );
        mmap( c, HOST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0 );
        minherit( c, HOST_PAGE, VM_INHERIT_NONE );
        memcpy( c, tmp, HOST_PAGE );
        check( "  ... replaced with private copy", c, 1 );
        printf( "  data intact: %s\n", c[0] == 6 && c[HOST_PAGE - 1] == 6 ? "yes" : "NO" );
        check( "  ... other page still shared", c + HOST_PAGE, 0 );
        write( pfd[1], "x", 1 );
        waitpid( pid, NULL, 0 );
    }

    check( "this program's code (main)", (void *)main, 0 );
    check( "unmapped address", (void *)0x7e0000000000ULL, 0 );
    close( fd );
    return 0;
}
