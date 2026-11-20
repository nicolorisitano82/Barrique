/* Barrique: show what the threads of a process wait on in RtlWaitOnAddress (locks,
 * condition variables, WaitOnAddress), without changing them. Needs an ntdll that
 * stores the wait in the TEB (ReservedForDebuggerInstrumentation[0..3]: address,
 * size, compared value, caller), a temporary debug build. In RtlWaitOnAddress
 * (dlls/ntdll/sync.c), around NtWaitForAlertByThreadId:
 *
 *     NtCurrentTeb()->ReservedForDebuggerInstrumentation[0] = (void *)addr;
 *     NtCurrentTeb()->ReservedForDebuggerInstrumentation[1] = (void *)(ULONG_PTR)size;
 *     NtCurrentTeb()->ReservedForDebuggerInstrumentation[2] = (void *)*(const ULONG64 *)cmp;
 *     NtCurrentTeb()->ReservedForDebuggerInstrumentation[3] = __builtin_return_address(0);
 *     ret = NtWaitForAlertByThreadId( NULL, timeout );
 *     NtCurrentTeb()->ReservedForDebuggerInstrumentation[0] = NULL;
 *
 *   waitpeek.exe <pid|name> [...]      pids in decimal or 0x hex, or an exe name:
 *                                      every process with that name
 *
 * With the TEMP code in msync_wait_objs (dlls/ntdll/unix/msync.c) it also shows the
 * msync waits: objects, their shared-memory value, type, refcount and waiter count.
 *
 * For each waiting thread: address, size, value it expected, value now, caller.
 * "CHANGED" means the value is no longer the one the thread waits for: if that
 * stays, the wake-up was lost.
 */
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    NTSTATUS ExitStatus;
    void *TebBaseAddress;
    CLIENT_ID ClientId;
    ULONG_PTR AffinityMask;
    LONG Priority, BasePriority;
} THREAD_BASIC;

#define TEB_WAIT_OFFSET 0x110   /* ReservedForDebuggerInstrumentation, 64-bit TEB */

static void peek( DWORD pid )
{
    HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD, 0 );
    HANDLE process = OpenProcess( PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid );
    THREADENTRY32 te = { sizeof(te) };
    int threads = 0, waiting = 0;

    if (!process) { printf( "pid %04lx: OpenProcess %lu\n", pid, GetLastError() ); return; }
    for (BOOL ok = Thread32First( snap, &te ); ok; ok = Thread32Next( snap, &te ))
    {
        HANDLE thread;
        THREAD_BASIC tb;
        void *wait[16];
        ULONG64 now = 0;
        SIZE_T got;
        WCHAR *desc = NULL;

        if (te.th32OwnerProcessID != pid) continue;
        threads++;
        if (!(thread = OpenThread( THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID ))) continue;
        if (NtQueryInformationThread( thread, ThreadBasicInformation, &tb, sizeof(tb), NULL ) ||
            !ReadProcessMemory( process, (char *)tb.TebBaseAddress + TEB_WAIT_OFFSET, wait, sizeof(wait), &got ))
        {
            printf( "pid %04lx tid %04lx: cannot read TEB\n", pid, te.th32ThreadID );
            CloseHandle( thread );
            continue;
        }
        if (FAILED( GetThreadDescription( thread, &desc ) )) desc = NULL;
        if (wait[0])
        {
            ULONG_PTR size = (ULONG_PTR)wait[1];
            ULONG64 cmp = (ULONG64)wait[2], mask = size >= 8 ? ~0ull : (1ull << (size * 8)) - 1;
            BYTE around[16] = {0};
            ReadProcessMemory( process, wait[0], &now, size, &got );
            ReadProcessMemory( process, (void *)((ULONG_PTR)wait[0] & ~7), around, sizeof(around), &got );
            waiting++;
            printf( "pid %04lx tid %04lx %-28ls wait %p size %Iu expect %#llx now %#llx%s caller %p mem",
                    pid, te.th32ThreadID, desc ? desc : L"", wait[0], size, cmp & mask, now & mask,
                    (cmp & mask) != (now & mask) ? " CHANGED" : "", wait[3] );
            for (int i = 0; i < 16; i += 4) printf( " %08lx", *(DWORD *)(around + i) );
            printf( "\n" );
        }
        if (((ULONG64)wait[4] >> 48) == 0x4d53)   /* msync wait, see the TEMP code in msync.c */
        {
            ULONG64 info = (ULONG64)wait[4];
            unsigned int count = info & 0xff;
            int tidmap = -1;

            waiting++;
            ReadProcessMemory( process, wait[15], &tidmap, sizeof(tidmap), &got );
            printf( "pid %04lx tid %04lx %-28ls msync %s%s count %u alert %u tidmap %d:",
                    pid, te.th32ThreadID, desc ? desc : L"", (info >> 8) & 1 ? "any" : "all",
                    info & 0x8000 ? " timeout" : "", count, (unsigned int)(info >> 16) & 0xffffffff, tidmap );
            for (unsigned int i = 0; i < count && i < 7; i++)
            {
                DWORD obj[4] = {0};
                ReadProcessMemory( process, wait[8 + i], obj, sizeof(obj), &got );
                printf( " [%s%u v=%ld type=%u refs=%u multi=%ld]", i < 2 ? "idx " : "",
                        i == 0 ? (unsigned int)(ULONG_PTR)wait[5] : i == 1 ? (unsigned int)(ULONG_PTR)wait[6] : 0,
                        (LONG)obj[0], (unsigned int)(obj[2] & 0xffff), (unsigned int)(obj[2] >> 16), (LONG)obj[3] );
            }
            if (wait[7])
            {
                DWORD obj[4] = {0};
                ReadProcessMemory( process, wait[7], obj, sizeof(obj), &got );
                printf( " alert[v=%ld multi=%ld]", (LONG)obj[0], (LONG)obj[3] );
            }
            printf( "\n" );
        }
        if (desc) LocalFree( desc );
        CloseHandle( thread );
    }
    printf( "pid %04lx: %d threads, %d in RtlWaitOnAddress\n", pid, threads, waiting );
    CloseHandle( process );
    CloseHandle( snap );
}

int main( int argc, char **argv )
{
    if (argc < 2) { printf( "usage: waitpeek <pid>...\n" ); return 1; }
    for (int i = 1; i < argc; i++)
    {
        char *end;
        DWORD pid = strtoul( argv[i], &end, 0 );
        if (!*end) { peek( pid ); continue; }
        HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
        PROCESSENTRY32 pe = { sizeof(pe) };
        for (BOOL ok = Process32First( snap, &pe ); ok; ok = Process32Next( snap, &pe ))
            if (!_stricmp( pe.szExeFile, argv[i] )) peek( pe.th32ProcessID );
        CloseHandle( snap );
    }
    return 0;
}
