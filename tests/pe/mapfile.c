/*
 * Shared writable file mapping test: write through a view, check the file
 * sees the data after FlushViewOfFile and after UnmapViewOfFile, and that a
 * second view of the same section sees the flushed data.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check( int ok, const char *what )
{
    printf( "%s: %s\n", ok ? "ok  " : "FAIL", what );
    if (!ok) failures++;
}

static void read_file( const char *name, char *buf, DWORD size, DWORD offset )
{
    HANDLE f = CreateFileA( name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL );
    DWORD got = 0;

    memset( buf, 0, size );
    SetFilePointer( f, offset, NULL, FILE_BEGIN );
    ReadFile( f, buf, size, &got, NULL );
    CloseHandle( f );
}

int main(void)
{
    static const char name[] = "mapfile.tmp";
    char buf[64], *view, *view2;
    HANDLE file, mapping;
    DWORD written, size;

    file = CreateFileA( name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, CREATE_ALWAYS, 0, NULL );
    check( file != INVALID_HANDLE_VALUE, "create file" );
    WriteFile( file, "original data", 13, &written, NULL );
    SetFilePointer( file, 0x6000, NULL, FILE_BEGIN );  /* 24K: spans several pages */
    SetEndOfFile( file );

    mapping = CreateFileMappingA( file, NULL, PAGE_READWRITE, 0, 0, NULL );
    check( mapping != NULL, "CreateFileMapping read/write" );
    view = MapViewOfFile( mapping, FILE_MAP_WRITE, 0, 0, 0 );
    check( view != NULL, "MapViewOfFile write" );
    if (!view)
    {
        printf( "  error %lu\n", GetLastError() );
        return 1;
    }
    check( !memcmp( view, "original data", 13 ), "view shows the file contents" );

    memcpy( view, "changed", 7 );
    memcpy( view + 0x5000, "far page", 8 );
    check( FlushViewOfFile( view, 0 ), "FlushViewOfFile" );
    read_file( name, buf, 13, 0 );
    check( !memcmp( buf, "changed" "l data", 13 ), "file has the flushed first page" );
    read_file( name, buf, 8, 0x5000 );
    check( !memcmp( buf, "far page", 8 ), "file has the flushed sixth page" );

    view2 = MapViewOfFile( mapping, FILE_MAP_READ, 0, 0, 0 );
    check( view2 && !memcmp( view2, "changed", 7 ), "second view sees flushed data" );
    if (view2) UnmapViewOfFile( view2 );

    memcpy( view + 0x1000, "unmapped", 8 );
    check( UnmapViewOfFile( view ), "UnmapViewOfFile" );
    read_file( name, buf, 8, 0x1000 );
    check( !memcmp( buf, "unmapped", 8 ), "file has data written before unmap" );

    size = GetFileSize( file, NULL );
    check( size == 0x6000, "file size unchanged" );
    CloseHandle( mapping );
    CloseHandle( file );
    DeleteFileA( name );

    printf( "RESULT: %s\n", failures ? "FAILED" : "ok" );
    return failures;
}
