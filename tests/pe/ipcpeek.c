/* Barrique: look at shared-memory IPC objects of other processes without changing
 * them. Lists the named objects of the session whose name contains a pattern
 * (default "IPCWrapper", Steam's IPC), and prints for each:
 *   section  the first 64 bytes as dwords (ring buffer header)
 *   event    signaled or not (NtQueryEvent: does not reset it)
 *   mutant   current count and abandoned state (NtQueryMutant)
 *
 *   ipcpeek.exe [pattern]
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef struct { UNICODE_STRING Name, TypeName; } DIRINFO;
typedef enum { EvtBasic = 0 } EVCLASS;
typedef struct { ULONG EventType; LONG EventState; } EVENT_BASIC;
typedef struct { LONG CurrentCount; BOOLEAN OwnedByCaller, AbandonedState; } MUTANT_BASIC;

NTSTATUS NTAPI NtOpenDirectoryObject( HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES * );
NTSTATUS NTAPI NtQueryDirectoryObject( HANDLE, void *, ULONG, BOOLEAN, BOOLEAN, ULONG *, ULONG * );
NTSTATUS NTAPI NtQueryEvent( HANDLE, int, void *, ULONG, ULONG * );
NTSTATUS NTAPI NtQueryMutant( HANDLE, int, void *, ULONG, ULONG * );

int main( int argc, char **argv )
{
    static BYTE buf[256 * 1024];
    WCHAR pattern[128];
    UNICODE_STRING dir_name;
    OBJECT_ATTRIBUTES attr;
    HANDLE dir;
    ULONG ctx = 0, ret;
    NTSTATUS status;
    int count = 0;

    swprintf( pattern, 128, L"%hs", argc > 1 ? argv[1] : "IPCWrapper" );
    RtlInitUnicodeString( &dir_name, L"\\Sessions\\1\\BaseNamedObjects" );
    InitializeObjectAttributes( &attr, &dir_name, 0, NULL, NULL );
    if ((status = NtOpenDirectoryObject( &dir, 0x3 /* QUERY | TRAVERSE */, &attr )))
    {
        printf( "NtOpenDirectoryObject %#lx\n", status );
        return 1;
    }
    for (;;)
    {
        DIRINFO *info;
        status = NtQueryDirectoryObject( dir, buf, sizeof(buf), FALSE, ctx == 0, &ctx, &ret );
        if (status && status != 0x105 /* STATUS_MORE_ENTRIES */) break;
        for (info = (DIRINFO *)buf; info->Name.Buffer; info++)
        {
            WCHAR name[256];
            char narrow[256];
            unsigned int n = min( info->Name.Length / 2, 255 );
            memcpy( name, info->Name.Buffer, n * 2 );
            name[n] = 0;
            if (!wcsstr( name, pattern )) continue;
            WideCharToMultiByte( CP_ACP, 0, name, -1, narrow, sizeof(narrow), NULL, NULL );
            count++;
            if (!wcsncmp( info->TypeName.Buffer, L"Section", 7 ))
            {
                HANDLE h = OpenFileMappingA( FILE_MAP_READ, FALSE, narrow );
                const DWORD *p = h ? MapViewOfFile( h, FILE_MAP_READ, 0, 0, 64 ) : NULL;
                printf( "section %-58s", narrow );
                if (p) { for (int i = 0; i < 8; i++) printf( " %08lx", p[i] ); UnmapViewOfFile( p ); }
                else printf( " (cannot map %lu)", GetLastError() );
                printf( "\n" );
                if (h) CloseHandle( h );
            }
            else if (!wcsncmp( info->TypeName.Buffer, L"Event", 5 ))
            {
                HANDLE h = OpenEventA( 0x0001 /* EVENT_QUERY_STATE */, FALSE, narrow );
                EVENT_BASIC eb = {0};
                if (h && !NtQueryEvent( h, 0, &eb, sizeof(eb), NULL ))
                    printf( "event   %-58s %s %s\n", narrow, eb.EventType ? "auto  " : "manual", eb.EventState ? "SIGNALED" : "-" );
                else printf( "event   %-58s (cannot query %lu)\n", narrow, GetLastError() );
                if (h) CloseHandle( h );
            }
            else if (!wcsncmp( info->TypeName.Buffer, L"Mutant", 6 ))
            {
                HANDLE h = OpenMutexA( 0x0001 /* MUTANT_QUERY_STATE */, FALSE, narrow );
                MUTANT_BASIC mb = {0};
                if (h && !NtQueryMutant( h, 0, &mb, sizeof(mb), NULL ))
                    printf( "mutex   %-58s count %ld%s\n", narrow, mb.CurrentCount, mb.AbandonedState ? " ABANDONED" : "" );
                else printf( "mutex   %-58s (cannot query %lu)\n", narrow, GetLastError() );
                if (h) CloseHandle( h );
            }
            else printf( "%-7.*ls %s\n", (int)(info->TypeName.Length / 2), info->TypeName.Buffer, narrow );
        }
        if (status != 0x105) break;
    }
    printf( "ipcpeek: %d objects\n", count );
    return 0;
}
