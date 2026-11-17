/*
 * Phase 5 step 4, point 1: what happens to host pages when Metal uses them.
 *
 * Read-only queries, like safetest.c: no Hypervisor.framework VM is created
 * and nothing is passed to hv_vm_map. Every page is classified with the same
 * test hvf.c runs before hv_vm_map (page_type_ok + page_is_usable, without
 * the copy-on-write fix-ups), plus the fields that test does not look at
 * (user tag, wired count, object id), so a change Metal makes is visible even
 * when the verdict stays ACCEPT.
 *
 * Cases:
 *   A  import:  anonymous memory -> newBufferWithBytesNoCopy, before / after
 *               creating the buffer / after the GPU wrote it / after release
 *   B  Metal's own shared MTLBuffer (what the GPU memory looks like)
 *   C  IOSurface locked for CPU access
 *   D  copy cost (strategy A): memcpy anon -> MTLBuffer contents, and a GPU
 *      blit from the imported buffer into a private buffer, in MB/s
 *
 *   gpumem [MB for the copy test, default 64]
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/mach_vm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define HOST_PAGE 0x4000ULL

struct page_state
{
    int mapped;
    vm_region_extended_info_data_t ext;
    vm_region_submap_info_data_64_t sub;
    vm_page_info_basic_data_t pi;
};

static void query( void *p, struct page_state *s )
{
    mach_vm_address_t page = (mach_vm_address_t)p & ~(HOST_PAGE - 1), addr = page;
    mach_vm_size_t size = 0;
    mach_msg_type_number_t count = VM_REGION_EXTENDED_INFO_COUNT;
    mach_port_t obj = MACH_PORT_NULL;
    natural_t depth = 0;

    memset( s, 0, sizeof(*s) );
    if (mach_vm_region( mach_task_self(), &addr, &size, VM_REGION_EXTENDED_INFO,
                        (vm_region_info_t)&s->ext, &count, &obj ) != KERN_SUCCESS ||
        addr > page || addr + size < page + HOST_PAGE)
        return;
    s->mapped = 1;
    addr = page;
    count = VM_REGION_SUBMAP_INFO_COUNT_64;
    mach_vm_region_recurse( mach_task_self(), &addr, &size, &depth, (vm_region_recurse_info_t)&s->sub, &count );
    count = VM_PAGE_INFO_BASIC_COUNT;
    mach_vm_page_info( mach_task_self(), page, VM_PAGE_INFO_BASIC, (vm_page_info_t)&s->pi, &count );
}

/* same verdict as hvf.c host_page_is_safe() without the copy-on-write fix-ups
 * (with the GPU checks added after the first run: tags 88/100, shared + wired) */
static int safe( const struct page_state *s )
{
    const vm_region_extended_info_data_t *info = &s->ext;
    const vm_page_info_basic_data_t *pi = &s->pi;

    if (!s->mapped) return 0;
    if (info->external_pager || (info->protection & VM_PROT_EXECUTE) || !(info->protection & VM_PROT_READ))
        return 0;
    if (info->user_tag == VM_MEMORY_IOACCELERATOR || info->user_tag == VM_MEMORY_IOSURFACE)
        return 0;
    if ((pi->disposition & VM_PAGE_QUERY_PAGE_PRESENT) &&
        (pi->disposition & (VM_PAGE_QUERY_PAGE_EXTERNAL | VM_PAGE_QUERY_PAGE_CS_VALIDATED |
                            VM_PAGE_QUERY_PAGE_CS_TAINTED | VM_PAGE_QUERY_PAGE_CS_NX)))
        return 0;
    if ((pi->disposition & VM_PAGE_QUERY_PAGE_WIRED) &&
        info->share_mode != SM_PRIVATE && info->share_mode != SM_PRIVATE_ALIASED)
        return 0;
    switch (info->share_mode)
    {
    case SM_PRIVATE: case SM_EMPTY: case SM_PRIVATE_ALIASED:
    case SM_SHARED: case SM_TRUESHARED: case SM_SHARED_ALIASED:
        return 1;
    case SM_COW:
        return (pi->disposition & VM_PAGE_QUERY_PAGE_PRESENT) && pi->depth == 0;
    default:
        return 0;
    }
}

static void show( const char *name, void *p, struct page_state *s )
{
    query( p, s );
    if (!s->mapped) { printf( "%-44s REFUSE (not mapped)\n", name ); return; }
    printf( "%-44s %s pager %u prot %x/%x share %u tag %u wired %u ref %u shadow %u obj %#x | "
            "depth %d disp %#x\n", name, safe( s ) ? "ACCEPT" : "REFUSE",
            s->ext.external_pager, s->ext.protection, s->sub.max_protection, s->ext.share_mode,
            s->ext.user_tag, s->sub.user_wired_count, s->ext.ref_count, s->ext.shadow_depth,
            s->sub.object_id, s->pi.depth, s->pi.disposition );
}

/* report every field that differs from the reference state */
static void diff( const char *what, const struct page_state *a, const struct page_state *b )
{
    int n = 0;
#define CMP(f) if (a->f != b->f) n++, printf( "  %s: %s %#llx -> %#llx\n", what, #f, \
                                              (unsigned long long)a->f, (unsigned long long)b->f )
    CMP(mapped); CMP(ext.external_pager); CMP(ext.protection); CMP(ext.share_mode); CMP(ext.user_tag);
    CMP(ext.shadow_depth); CMP(sub.max_protection); CMP(sub.user_wired_count); CMP(sub.object_id);
    CMP(pi.depth); CMP(pi.disposition);
#undef CMP
    if (!n) printf( "  %s: unchanged\n", what );
}

static double seconds( uint64_t t )
{
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info( &tb );
    return (double)t * tb.numer / tb.denom / 1e9;
}

int main( int argc, char **argv )
{
    size_t copy_mb = argc > 1 ? strtoul( argv[1], NULL, 0 ) : 64;
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    id<MTLCommandQueue> queue;
    struct page_state ref, s;

    if (!dev) { printf( "RESULT: FAILED no Metal device\n" ); return 1; }
    queue = [dev newCommandQueue];
    printf( "device: %s, unified memory %d\n", [[dev name] UTF8String], (int)[dev hasUnifiedMemory] );

    /* A: import anonymous memory, the way hvf.c memory would be */
    @autoreleasepool
    {
        size_t len = 16 * HOST_PAGE;
        char *mem = mmap( NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
        char *probe = mem + 4 * HOST_PAGE;
        id<MTLBuffer> buf;
        id<MTLCommandBuffer> cb;
        id<MTLBlitCommandEncoder> blit;

        minherit( mem, len, VM_INHERIT_NONE );   /* as hvf_mmap does */
        memset( mem, 0x5a, len );
        printf( "\nA: anonymous memory imported with newBufferWithBytesNoCopy\n" );
        show( "A0 anon, written (reference)", probe, &ref );

        buf = [dev newBufferWithBytesNoCopy:mem length:len options:MTLResourceStorageModeShared deallocator:nil];
        if (!buf) printf( "  newBufferWithBytesNoCopy failed\n" );
        show( "A1 after newBufferWithBytesNoCopy", probe, &s );
        diff( "A1", &ref, &s );

        cb = [queue commandBuffer];
        blit = [cb blitCommandEncoder];
        [blit fillBuffer:buf range:NSMakeRange( 0, len ) value:0xa5];
        [blit endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        show( "A2 after a GPU fill", probe, &s );
        diff( "A2", &ref, &s );
        printf( "  GPU write visible to the CPU: %s\n", (unsigned char)probe[0] == 0xa5 ? "yes" : "NO" );

        probe[0] = 1;
        show( "A3 CPU write after GPU use", probe, &s );
        diff( "A3", &ref, &s );

        buf = nil; cb = nil; blit = nil;
    }
    /* A4: the same, checked after the buffer is released */
    @autoreleasepool
    {
        size_t len = 16 * HOST_PAGE;
        char *mem = mmap( NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
        char *probe = mem + 4 * HOST_PAGE;

        minherit( mem, len, VM_INHERIT_NONE );
        memset( mem, 0x5a, len );
        query( probe, &ref );
        @autoreleasepool
        {
            id<MTLBuffer> buf = [dev newBufferWithBytesNoCopy:mem length:len
                                                      options:MTLResourceStorageModeShared deallocator:nil];
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit fillBuffer:buf range:NSMakeRange( 0, len ) value:0xa5];
            [blit endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
        }
        show( "A4 after the buffer is released", probe, &s );
        diff( "A4", &ref, &s );
    }

    /* E: what VirtualLock does (mlock of private memory): must stay accepted */
    {
        char *p = mmap( NULL, 4 * HOST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
        memset( p, 1, 4 * HOST_PAGE );
        printf( "\nE: VirtualLock\n" );
        if (mlock( p, 4 * HOST_PAGE )) printf( "  mlock failed\n" );
        show( "E1 anon private, mlock (expect ACCEPT)", p + HOST_PAGE, &s );
        munlock( p, 4 * HOST_PAGE );
    }

    /* B: memory Metal allocates itself */
    @autoreleasepool
    {
        id<MTLBuffer> buf = [dev newBufferWithLength:16 * HOST_PAGE options:MTLResourceStorageModeShared];
        char *p = [buf contents];
        printf( "\nB: Metal's own buffers\n" );
        p[0] = 1;
        show( "B1 MTLBuffer shared, contents", p + 4 * HOST_PAGE, &s );
        buf = [dev newBufferWithLength:64 << 20 options:MTLResourceStorageModeShared];
        p = [buf contents];
        memset( p, 1, 64 << 20 );
        show( "B2 MTLBuffer shared 64 MB, middle", p + (32 << 20), &s );
    }

    /* C: IOSurface */
    @autoreleasepool
    {
        NSDictionary *props = @{ (id)kIOSurfaceWidth: @1024, (id)kIOSurfaceHeight: @1024,
                                 (id)kIOSurfaceBytesPerElement: @4, (id)kIOSurfacePixelFormat: @((uint32_t)'BGRA') };
        IOSurfaceRef surf = IOSurfaceCreate( (__bridge CFDictionaryRef)props );
        printf( "\nC: IOSurface\n" );
        if (surf)
        {
            char *p;
            IOSurfaceLock( surf, 0, NULL );
            p = IOSurfaceGetBaseAddress( surf );
            p[0] = 1;
            show( "C1 IOSurface locked, base", p, &s );
            show( "C2 IOSurface locked, middle", p + (2 << 20), &s );
            IOSurfaceUnlock( surf, 0, NULL );
            CFRelease( surf );
        }
        else printf( "  IOSurfaceCreate failed\n" );
    }

    /* D: copy cost */
    @autoreleasepool
    {
        size_t len = copy_mb << 20;
        char *src = mmap( NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
        id<MTLBuffer> dst = [dev newBufferWithLength:len options:MTLResourceStorageModeShared];
        id<MTLBuffer> priv = [dev newBufferWithLength:len options:MTLResourceStorageModePrivate];
        id<MTLBuffer> imported;
        double best = 0, t;
        int i;

        printf( "\nD: copy cost, %zu MB\n", copy_mb );
        memset( src, 3, len );
        memset( [dst contents], 0, len );
        for (i = 0; i < 5; i++)
        {
            uint64_t t0 = mach_absolute_time();
            memcpy( [dst contents], src, len );
            t = seconds( mach_absolute_time() - t0 );
            if (!best || t < best) best = t;
        }
        printf( "  D1 memcpy anon -> shared MTLBuffer: %.0f MB/s (best of 5)\n", copy_mb / best );

        imported = [dev newBufferWithBytesNoCopy:src length:len options:MTLResourceStorageModeShared deallocator:nil];
        best = 0;
        for (i = 0; i < 5; i++)
        {
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            uint64_t t0;
            [blit copyFromBuffer:imported sourceOffset:0 toBuffer:priv destinationOffset:0 size:len];
            [blit endEncoding];
            t0 = mach_absolute_time();
            [cb commit];
            [cb waitUntilCompleted];
            t = seconds( mach_absolute_time() - t0 );
            if (!best || t < best) best = t;
        }
        printf( "  D2 GPU blit imported -> private: %.0f MB/s (best of 5)\n", copy_mb / best );
        show( "D3 source page after the copies", src + len / 2, &s );
    }

    printf( "\nRESULT: done\n" );
    return 0;
}
