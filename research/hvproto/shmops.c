#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
int main(void)
{
    const char *name = "/hvproto_shm_ops";
    char c = 0, buf[8];
    struct stat st;
    int fd;
    shm_unlink( name );
    fd = shm_open( name, O_RDWR | O_CREAT | O_EXCL, 0600 );
    shm_unlink( name );
    printf( "ftruncate 0x4000:  %d (%s)\n", ftruncate( fd, 0x4000 ), strerror( errno ) );
    printf( "ftruncate 0x10000: %d (%s)\n", ftruncate( fd, 0x10000 ), strerror( errno ) );
    errno = 0; printf( "pwrite:            %zd (%s)\n", pwrite( fd, &c, 1, 0x8000 ), strerror( errno ) );
    errno = 0; printf( "pread:             %zd (%s)\n", pread( fd, buf, 8, 0 ), strerror( errno ) );
    fstat( fd, &st );
    printf( "fstat: mode %o (S_ISREG %d) size %lld nlink %d\n", st.st_mode, S_ISREG(st.st_mode), (long long)st.st_size, st.st_nlink );
    return 0;
}
