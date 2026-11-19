/* Barrique: memory below 4GB in a 64-bit process.
 *
 * The host cannot map below 4GB, so such memory lives in a window at
 * 0xe000000000 + address that the VM also shows at the Windows address. A program
 * that asks for an address below 4GB must get that address back, use it, and pass
 * it to the system (here ReadFile/WriteFile).
 *
 *   lowalloc.exe       prints the checks and "lowalloc: ok" or "lowalloc: FAILED"
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int bad;

static void check( int ok, const char *what, const void *got )
{
    printf( "%-40s %s (%p)\n", what, ok ? "ok" : "FAILED", got );
    if (!ok) bad = 1;
}

int main( void )
{
    void *want = (void *)0x30000000, *view_want = (void *)0x40000000;
    char path[MAX_PATH], dir[MAX_PATH];
    MEMORY_BASIC_INFORMATION mbi;
    HANDLE file, map;
    DWORD done;
    char *p, *view;
    int i;

    p = VirtualAlloc( want, 0x20000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
    check( p == want, "VirtualAlloc at 0x30000000", p );
    if (!p) return 1;
    for (i = 0; i < 0x20000; i++) p[i] = (char)i;
    check( p[0x1234] == (char)0x34, "write and read it back", p + 0x1234 );

    check( VirtualQuery( p + 0x1000, &mbi, sizeof(mbi) ) == sizeof(mbi) &&
           mbi.BaseAddress == p + 0x1000 && mbi.AllocationBase == p, "VirtualQuery", mbi.AllocationBase );

    /* the system reads from and writes to it */
    GetTempPathA( sizeof(dir), dir );
    GetTempFileNameA( dir, "low", 0, path );
    file = CreateFileA( path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_FLAG_DELETE_ON_CLOSE, NULL );
    check( WriteFile( file, p, 0x10000, &done, NULL ) && done == 0x10000, "WriteFile from it", p );
    memset( p, 0, 0x10000 );
    SetFilePointer( file, 0, NULL, FILE_BEGIN );
    check( ReadFile( file, p, 0x10000, &done, NULL ) && done == 0x10000 && p[0x4321] == (char)0x21,
           "ReadFile into it", p + 0x4321 );
    CloseHandle( file );

    map = CreateFileMappingA( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x10000, NULL );
    view = MapViewOfFileEx( map, FILE_MAP_ALL_ACCESS, 0, 0, 0, view_want );
    check( view == view_want, "MapViewOfFileEx at 0x40000000", view );
    if (view)
    {
        strcpy( view, "hello" );
        check( !strcmp( view, "hello" ), "use the view", view );
        check( UnmapViewOfFile( view ), "UnmapViewOfFile", view );
    }
    CloseHandle( map );

    check( VirtualFree( p, 0, MEM_RELEASE ), "VirtualFree", p );
    /* without an address the result may be anywhere, but must be usable */
    p = VirtualAlloc( NULL, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
    p[0] = 1;
    check( p != NULL, "VirtualAlloc anywhere", p );

    printf( "lowalloc: %s\n", bad ? "FAILED" : "ok" );
    return bad;
}
