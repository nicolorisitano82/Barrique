/*
 * Phase 5 step 4, point 3: map memory that Metal uses into a
 * Hypervisor.framework VM and touch it from the guest.
 *
 * DANGER: a page type the SPTM does not accept in a VM panics the kernel
 * ("[SPTM] VIOLATION_ILLEGAL_MAPPING_TYPE") and the Mac restarts. Run one case
 * per process, only with the user's OK and their work saved. The log line for
 * each step is flushed (and fsync'ed to gpumap.log) before the step, so after
 * a restart the log shows where it stopped.
 *
 *   gpumap <case>
 *     1  plain anonymous memory (control, what Barrique maps every day)
 *     2  anonymous memory imported with newBufferWithBytesNoCopy, GPU not used yet
 *     3  same, after the GPU wrote it (shared + wired)
 *     4  Metal's own shared MTLBuffer (tag VM_MEMORY_IOACCELERATOR)
 *     5  IOSurface (tag VM_MEMORY_IOSURFACE)
 *     6  Metal's own MTLBuffer released by Metal while still mapped in the VM
 *        (what a "window on Metal memory" in hvf.c would face when DXMT frees a
 *        buffer), then 256 MB of new Metal buffers to reuse the pages, and the
 *        guest touches the stale mapping again
 *
 * Guest: one vCPU at EL1 with the MMU off reads the target page, writes a
 * marker and returns with HVC. The host then checks the marker from the CPU
 * and, for Metal memory, from the GPU (blit into a private buffer and back).
 * Order on the way out: hv_vm_unmap and hv_vm_destroy before the Metal
 * objects are released.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#include <Hypervisor/Hypervisor.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define HOST_PAGE  0x4000ULL
#define TARGET_IPA 0x40000000ULL
#define CODE_IPA   0x40100000ULL
#define MARKER     0x4241525249515545ULL   /* "EUQIRRAB" */

static int logfd = -1;

static void step( const char *fmt, ... )
{
    char buf[512];
    va_list ap;
    int n;

    va_start( ap, fmt );
    n = vsnprintf( buf, sizeof(buf) - 1, fmt, ap );
    va_end( ap );
    buf[n++] = '\n';
    fwrite( buf, 1, n, stdout );
    fflush( stdout );
    if (logfd >= 0) { write( logfd, buf, n ); fsync( logfd ); }
}

#define CHECK(x) do { hv_return_t _r = (x); if (_r != HV_SUCCESS) { \
    step( "  %s failed: 0x%x", #x, _r ); goto done; } } while (0)

/* EL1, MMU off: x0 = target IPA, x2 = marker */
static const uint32_t guest_code[] =
{
    0xf9400001,   /* ldr x1, [x0]  */
    0xf9000002,   /* str x2, [x0]  */
    0xd5033f9f,   /* dsb sy        */
    0xd4000002,   /* hvc #0        */
};

static int gpu_reads_marker( id<MTLDevice> dev, id<MTLBuffer> buf )
{
    id<MTLCommandQueue> q = [dev newCommandQueue];
    id<MTLBuffer> priv = [dev newBufferWithLength:HOST_PAGE options:MTLResourceStorageModePrivate];
    id<MTLBuffer> back = [dev newBufferWithLength:HOST_PAGE options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [q commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];

    [blit copyFromBuffer:buf sourceOffset:0 toBuffer:priv destinationOffset:0 size:HOST_PAGE];
    [blit copyFromBuffer:priv sourceOffset:0 toBuffer:back destinationOffset:0 size:HOST_PAGE];
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    return *(uint64_t *)[back contents] == MARKER;
}

static void gpu_fill( id<MTLDevice> dev, id<MTLBuffer> buf, size_t len )
{
    id<MTLCommandQueue> q = [dev newCommandQueue];
    id<MTLCommandBuffer> cb = [q commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit fillBuffer:buf range:NSMakeRange( 0, len ) value:0xa5];
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
}

int main( int argc, char **argv )
{
    int which = argc > 1 ? atoi( argv[1] ) : 0;
    size_t len = 4 * HOST_PAGE;
    id<MTLDevice> dev = nil;
    id<MTLBuffer> buf = nil;
    IOSurfaceRef surf = NULL;
    char *target = NULL, *code;
    hv_vcpu_t vcpu = 0;
    hv_vcpu_exit_t *exit_info;
    int vm = 0, target_mapped = 0, code_mapped = 0, have_vcpu = 0, ok = 0;
    uint64_t x1 = 0;

    if (which < 1 || which > 6) { fprintf( stderr, "usage: gpumap <1-6>\n" ); return 2; }
    logfd = open( "gpumap.log", O_WRONLY | O_CREAT | O_APPEND, 0644 );
    step( "=== gpumap case %d", which );

    @autoreleasepool
    {
        if (which >= 2)
        {
            if (!(dev = MTLCreateSystemDefaultDevice())) { step( "  no Metal device" ); return 1; }
        }
        switch (which)
        {
        case 1:
        case 2:
        case 3:
            target = mmap( NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
            minherit( target, len, VM_INHERIT_NONE );
            memset( target, 0x5a, len );
            if (which >= 2)
                buf = [dev newBufferWithBytesNoCopy:target length:len
                                            options:MTLResourceStorageModeShared deallocator:nil];
            if (which == 3) gpu_fill( dev, buf, len );
            break;
        case 4:
        case 6:
            buf = [dev newBufferWithLength:len options:MTLResourceStorageModeShared];
            target = [buf contents];
            memset( target, 0x5a, len );
            break;
        case 5:
        {
            NSDictionary *props = @{ (id)kIOSurfaceWidth: @256, (id)kIOSurfaceHeight: @64,
                                     (id)kIOSurfaceBytesPerElement: @4,
                                     (id)kIOSurfacePixelFormat: @((uint32_t)'BGRA') };
            surf = IOSurfaceCreate( (__bridge CFDictionaryRef)props );
            IOSurfaceLock( surf, 0, NULL );
            target = IOSurfaceGetBaseAddress( surf );
            memset( target, 0x5a, HOST_PAGE );
            break;
        }
        }
        if (!target || ((uintptr_t)target & (HOST_PAGE - 1)) || (which >= 2 && which != 5 && !buf))
        {
            step( "  setup failed (target %p, buffer %p)", target, (__bridge void *)buf );
            return 1;
        }
        step( "  target %p, first qword %#llx", target, *(unsigned long long *)target );

        code = mmap( NULL, HOST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
        memcpy( code, guest_code, sizeof(guest_code) );

        step( "  hv_vm_create" );
        CHECK( hv_vm_create( NULL ) );
        vm = 1;
        step( "  hv_vm_map code page" );
        CHECK( hv_vm_map( code, CODE_IPA, HOST_PAGE, HV_MEMORY_READ | HV_MEMORY_EXEC ) );
        code_mapped = 1;
        step( "  hv_vm_map target page  <-- point of no return" );
        CHECK( hv_vm_map( target, TARGET_IPA, HOST_PAGE, HV_MEMORY_READ | HV_MEMORY_WRITE ) );
        target_mapped = 1;
        step( "  mapped" );

        CHECK( hv_vcpu_create( &vcpu, &exit_info, NULL ) );
        have_vcpu = 1;
        CHECK( hv_vcpu_set_reg( vcpu, HV_REG_PC, CODE_IPA ) );
        CHECK( hv_vcpu_set_reg( vcpu, HV_REG_CPSR, 0x3c5 ) );   /* EL1h, DAIF masked */
        CHECK( hv_vcpu_set_reg( vcpu, HV_REG_X0, TARGET_IPA ) );
        CHECK( hv_vcpu_set_reg( vcpu, HV_REG_X2, MARKER ) );
        CHECK( hv_vcpu_set_sys_reg( vcpu, HV_SYS_REG_SCTLR_EL1, 0x30d00800 ) );  /* MMU and caches off */
        step( "  guest read + write" );
        CHECK( hv_vcpu_run( vcpu ) );
        hv_vcpu_get_reg( vcpu, HV_REG_X1, &x1 );
        step( "  exit reason %u syndrome %#llx, guest read %#llx", exit_info->reason,
              (unsigned long long)exit_info->exception.syndrome, (unsigned long long)x1 );
        ok = exit_info->reason == HV_EXIT_REASON_EXCEPTION && (exit_info->exception.syndrome >> 26) == 0x16;
        step( "  host CPU sees marker: %s", *(uint64_t *)target == MARKER ? "yes" : "NO" );
        if (buf) step( "  GPU sees marker: %s", gpu_reads_marker( dev, buf ) ? "yes" : "NO" );

        if (which == 6)
        {
            NSMutableArray *others = [NSMutableArray array];
            int k;

            step( "  releasing the Metal buffer while it is mapped  <-- untested" );
            @autoreleasepool { buf = nil; }
            step( "  released; allocating 256 MB of new Metal buffers" );
            for (k = 0; k < 64; k++)
            {
                id<MTLBuffer> b = [dev newBufferWithLength:4 << 20 options:MTLResourceStorageModeShared];
                memset( [b contents], 0x77, 4 << 20 );
                [others addObject:b];
            }
            step( "  guest touches the stale mapping again" );
            CHECK( hv_vcpu_set_reg( vcpu, HV_REG_PC, CODE_IPA ) );
            CHECK( hv_vcpu_set_reg( vcpu, HV_REG_CPSR, 0x3c5 ) );
            CHECK( hv_vcpu_set_reg( vcpu, HV_REG_X0, TARGET_IPA ) );
            CHECK( hv_vcpu_run( vcpu ) );
            hv_vcpu_get_reg( vcpu, HV_REG_X1, &x1 );
            step( "  exit reason %u syndrome %#llx, guest read %#llx", exit_info->reason,
                  (unsigned long long)exit_info->exception.syndrome, (unsigned long long)x1 );
            ok = ok && exit_info->reason == HV_EXIT_REASON_EXCEPTION && (exit_info->exception.syndrome >> 26) == 0x16;
            others = nil;
        }

    done:
        if (have_vcpu) hv_vcpu_destroy( vcpu );
        if (target_mapped) { step( "  hv_vm_unmap target" ); hv_vm_unmap( TARGET_IPA, HOST_PAGE ); }
        if (code_mapped) hv_vm_unmap( CODE_IPA, HOST_PAGE );
        if (vm) { step( "  hv_vm_destroy" ); hv_vm_destroy(); }
        step( "  releasing Metal objects" );
        buf = nil;
        if (surf) { IOSurfaceUnlock( surf, 0, NULL ); CFRelease( surf ); }
    }
    step( "RESULT case %d: %s", which, ok ? "ok" : "FAILED" );
    return ok ? 0 : 1;
}
