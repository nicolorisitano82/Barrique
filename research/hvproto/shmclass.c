/*
 * Classify a shm_open page the way hvf.c would. Read-only queries, no VM.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

#define HOST_PAGE 0x4000ULL

static void show( const char *name, void *p )
{
    vm_region_extended_info_data_t info;
    vm_page_info_basic_data_t pi = { 0 };
    mach_msg_type_number_t count = VM_REGION_EXTENDED_INFO_COUNT;
    mach_vm_address_t addr = (mach_vm_address_t)p;
    mach_vm_size_t size = 0;
    mach_port_t obj = MACH_PORT_NULL;

    mach_vm_region( mach_task_self(), &addr, &size, VM_REGION_EXTENDED_INFO, (vm_region_info_t)&info, &count, &obj );
    count = VM_PAGE_INFO_BASIC_COUNT;
    mach_vm_page_info( mach_task_self(), (mach_vm_address_t)p, VM_PAGE_INFO_BASIC, (vm_page_info_t)&pi, &count );
    printf( "%-34s pager %u prot %x share %u shadow %u | page depth %d disposition %#x\n",
            name, info.external_pager, info.protection, info.share_mode, info.shadow_depth, pi.depth, pi.disposition );
}

int main(void)
{
    const char *name = "/hvproto_shm_test";
    int fd;
    char *p;
    pid_t pid;
    int pfd[2];

    shm_unlink( name );
    fd = shm_open( name, O_RDWR | O_CREAT | O_EXCL, 0600 );
    if (fd < 0) { perror( "shm_open" ); return 1; }
    shm_unlink( name );
    if (ftruncate( fd, HOST_PAGE )) { perror( "ftruncate" ); return 1; }
    p = mmap( NULL, HOST_PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
    show( "shm, untouched", p );
    memset( p, 0x5a, HOST_PAGE );
    show( "shm, written", p );

    pipe( pfd );
    if (!(pid = fork()))
    {
        char x, *q = mmap( NULL, HOST_PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
        q[1] = 0x33;
        read( pfd[0], &x, 1 );
        _exit( 0 );
    }
    usleep( 200000 );
    show( "shm, mapped by a second process", p );
    printf( "second process write visible: %s\n", p[1] == 0x33 ? "yes" : "no" );
    write( pfd[1], "x", 1 );
    waitpid( pid, NULL, 0 );
    return 0;
}
