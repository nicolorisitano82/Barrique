/*
 * Minimal test: can a shm_open page (anonymous, truly shared between
 * processes) be mapped into a Hypervisor.framework VM?
 *
 * One 16K shm page at IPA 0x40000000, one code page at IPA 0x10000000.
 * The guest runs at EL1 with the MMU off: it reads the shm page, writes
 * to it and exits with hvc. A second process watches the same shm page.
 */
#include <Hypervisor/Hypervisor.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define HOST_PAGE 0x4000ULL
#define CODE_IPA  0x10000000ULL
#define SHM_IPA   0x40000000ULL

#define CHECK(x) do { hv_return_t _r = (x); if (_r) { fprintf(stderr, "%s failed: %#x\n", #x, _r); exit(1); } } while (0)

int main(void)
{
    static const uint32_t code[] =
    {
        0xf9400001,  /* ldr x1, [x0]       read what the host wrote */
        0xf9000402,  /* str x2, [x0, #8]   write for the host and the other process */
        0xd4000002,  /* hvc #0 */
    };
    const char *name = "/hvproto_shm_vm";
    hv_vcpu_t vcpu;
    hv_vcpu_exit_t *exit_info;
    uint64_t x1, *shm;
    char *codepage;
    int fd, pfd[2];
    pid_t pid;

    shm_unlink( name );
    fd = shm_open( name, O_RDWR | O_CREAT | O_EXCL, 0600 );
    shm_unlink( name );
    if (fd < 0 || ftruncate( fd, HOST_PAGE )) { perror( "shm" ); return 1; }
    shm = mmap( NULL, HOST_PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
    shm[0] = 0x1111222233334444ULL;

    /* second process: maps the same shm and waits for the guest's write */
    pipe( pfd );
    if (!(pid = fork()))
    {
        volatile uint64_t *q = mmap( NULL, HOST_PAGE, PROT_READ, MAP_SHARED, fd, 0 );
        char x;
        read( pfd[0], &x, 1 );
        printf( "other process sees guest write: %#llx\n", (unsigned long long)q[1] );
        _exit( q[1] == 0xabcdef0123456789ULL ? 0 : 1 );
    }

    codepage = mmap( NULL, HOST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0 );
    memcpy( codepage, code, sizeof(code) );
    mprotect( codepage, HOST_PAGE, PROT_READ );

    CHECK( hv_vm_create( NULL ) );
    CHECK( hv_vm_map( codepage, CODE_IPA, HOST_PAGE, HV_MEMORY_READ | HV_MEMORY_EXEC ) );
    printf( "mapping the shm page into the VM...\n" );
    fflush( stdout );
    CHECK( hv_vm_map( shm, SHM_IPA, HOST_PAGE, HV_MEMORY_READ | HV_MEMORY_WRITE ) );
    printf( "hv_vm_map ok\n" );
    fflush( stdout );

    CHECK( hv_vcpu_create( &vcpu, &exit_info, NULL ) );
    CHECK( hv_vcpu_set_reg( vcpu, HV_REG_PC, CODE_IPA ) );
    CHECK( hv_vcpu_set_reg( vcpu, HV_REG_CPSR, 0x3c5 ) );   /* EL1h, MMU off */
    CHECK( hv_vcpu_set_reg( vcpu, HV_REG_X0, SHM_IPA ) );
    CHECK( hv_vcpu_set_reg( vcpu, HV_REG_X2, 0xabcdef0123456789ULL ) );
    CHECK( hv_vcpu_run( vcpu ) );

    hv_vcpu_get_reg( vcpu, HV_REG_X1, &x1 );
    printf( "exit reason %u, syndrome %#llx\n", exit_info->reason, (unsigned long long)exit_info->exception.syndrome );
    printf( "guest read host value:  %#llx %s\n", (unsigned long long)x1, x1 == 0x1111222233334444ULL ? "ok" : "WRONG" );
    printf( "host sees guest write:  %#llx %s\n", (unsigned long long)shm[1], shm[1] == 0xabcdef0123456789ULL ? "ok" : "WRONG" );
    fflush( stdout );

    write( pfd[1], "x", 1 );
    waitpid( pid, &fd, 0 );
    printf( "other process: %s\n", WIFEXITED(fd) && !WEXITSTATUS(fd) ? "ok" : "WRONG" );

    hv_vcpu_destroy( vcpu );
    hv_vm_unmap( SHM_IPA, HOST_PAGE );
    hv_vm_unmap( CODE_IPA, HOST_PAGE );
    hv_vm_destroy();
    printf( "done\n" );
    return 0;
}
