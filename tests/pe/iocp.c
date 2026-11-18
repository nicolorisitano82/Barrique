/* Barrique: I/O completion port stress test in the pattern of Chromium's IPC
 * (ipc/chromium ChannelWin + MessagePumpForIO, used by Firefox):
 *
 *  - pairs of overlapped message-mode named pipes, both ends on one completion
 *    port, the completion key is the channel;
 *  - one IO thread keeps a read pending on each server end and writes from the
 *    client end; every operation, synchronous success included, must produce
 *    exactly one completion packet (no FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
 *  - channels are closed the Chromium way: CancelIo on both ends, then the IO
 *    thread waits until the packets of that channel's pending operations have
 *    arrived, then closes the handles and frees the channel;
 *  - a packet for an operation that already completed, or that belongs to a
 *    closed channel, is what makes Firefox call a freed IPC handler.
 *
 *   iocp.exe [seconds] [channels] [procs]
 *
 * procs=1: the client end of each pipe lives in a child process (iocp.exe child
 * <pipe>) that writes at irregular intervals, as Firefox's content and GPU
 * processes do; channels are closed with a read pending (CancelIo) or by
 * terminating the child (broken pipe), both with packets still to come.
 *
 * Prints counters and "iocp: ok" or "iocp: FAILED"; exit code 0 or 1.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_LIVE 4096

struct channel;

struct op
{
    OVERLAPPED ov;          /* first: the packet's overlapped pointer is the op */
    struct channel *chan;
    int is_read;
    char buf[512];
};

struct channel
{
    HANDLE server, client, child;
    unsigned int id;
    int read_pending, write_pending;
    int closing;
};

static struct op *live[MAX_LIVE];
static unsigned int nb_live;
static HANDLE port;
static unsigned int next_channel_id;

static LONG ops_issued, ops_sync, ops_completed, ops_aborted, ops_failed;
static int use_procs;
static char self_path[MAX_PATH];
static LONG ops_broken, children_killed;
static LONG stray_packets, wrong_key, late_packets, missing, channels_closed;

static void add_live( struct op *op )
{
    if (nb_live < MAX_LIVE) live[nb_live++] = op;
}

static BOOL remove_live( struct op *op )
{
    unsigned int i;
    for (i = 0; i < nb_live; i++)
        if (live[i] == op)
        {
            live[i] = live[--nb_live];
            return TRUE;
        }
    return FALSE;
}

static BOOL issue( struct channel *chan, int is_read )
{
    struct op *op = calloc( 1, sizeof(*op) );
    DWORD size = 0;
    BOOL ok;

    op->chan = chan;
    op->is_read = is_read;
    if (is_read)
        ok = ReadFile( chan->server, op->buf, sizeof(op->buf), NULL, &op->ov );
    else
    {
        if (use_procs) { free( op ); return TRUE; }
        size = 64 + rand() % 400;
        memset( op->buf, 'a' + chan->id % 26, size );
        ok = WriteFile( chan->client, op->buf, size, NULL, &op->ov );
    }
    if (!ok && GetLastError() != ERROR_IO_PENDING)
    {
        InterlockedIncrement( &ops_failed );
        free( op );
        return FALSE;
    }
    if (ok) InterlockedIncrement( &ops_sync );
    InterlockedIncrement( &ops_issued );
    if (is_read) chan->read_pending = 1;
    else chan->write_pending = 1;
    add_live( op );
    return TRUE;
}

static struct channel *open_channel(void)
{
    struct channel *chan = calloc( 1, sizeof(*chan) );
    WCHAR name[64];
    OVERLAPPED ov = {0};
    DWORD mode = PIPE_READMODE_MESSAGE;

    chan->id = next_channel_id++;
    swprintf( name, 64, L"\\\\.\\pipe\\barrique-iocp-%u-%u", GetCurrentProcessId(), chan->id );
    chan->server = CreateNamedPipeW( name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                     PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE, 1, 4096, 4096, 5000, NULL );
    if (chan->server == INVALID_HANDLE_VALUE)
    {
        printf( "CreateNamedPipe failed %lu\n", GetLastError() );
        exit( 2 );
    }
    ov.hEvent = CreateEventW( NULL, TRUE, FALSE, NULL );
    ConnectNamedPipe( chan->server, &ov );
    if (use_procs)
    {
        char cmd[MAX_PATH + 128];
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        snprintf( cmd, sizeof(cmd), "\"%s\" child %ls %u", self_path, name, (unsigned int)rand() );
        if (!CreateProcessA( NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ))
        {
            printf( "CreateProcess failed %lu\n", GetLastError() );
            exit( 2 );
        }
        CloseHandle( pi.hThread );
        chan->child = pi.hProcess;
        chan->client = INVALID_HANDLE_VALUE;
        if (WaitForSingleObject( ov.hEvent, 10000 )) printf( "child did not connect\n" );
    }
    else
    {
        chan->client = CreateFileW( name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL );
        if (chan->client == INVALID_HANDLE_VALUE)
        {
            printf( "CreateFile failed %lu\n", GetLastError() );
            exit( 2 );
        }
        WaitForSingleObject( ov.hEvent, 5000 );
        SetNamedPipeHandleState( chan->client, &mode, NULL, NULL );
        CreateIoCompletionPort( chan->client, port, (ULONG_PTR)chan, 1 );
    }
    CloseHandle( ov.hEvent );
    CreateIoCompletionPort( chan->server, port, (ULONG_PTR)chan, 1 );
    issue( chan, TRUE );
    issue( chan, FALSE );
    return chan;
}

/* the client end in its own process: write messages at irregular intervals */
static int child_main( const char *name, unsigned int seed )
{
    HANDLE pipe;
    char buf[512];
    DWORD written, mode = PIPE_READMODE_MESSAGE;
    int i;

    srand( seed );
    pipe = CreateFileA( name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL );
    if (pipe == INVALID_HANDLE_VALUE) return 3;
    SetNamedPipeHandleState( pipe, &mode, NULL, NULL );
    for (i = 0; ; i++)
    {
        DWORD size = 64 + rand() % 400;
        memset( buf, 'a' + i % 26, size );
        if (!WriteFile( pipe, buf, size, &written, NULL )) break;
        if (rand() % 4 == 0) Sleep( rand() % 3 );
        /* now and then go quiet, so the parent's read stays pending */
        if (rand() % 50 == 0) Sleep( 20 + rand() % 80 );
    }
    return 0;
}

/* one packet: account for its operation, re-issue it unless the channel is closing */
static void handle_packet( struct channel **chans, unsigned int nb_chans, ULONG_PTR key, OVERLAPPED *ov, BOOL ok )
{
    struct op *op = (struct op *)ov;
    struct channel *chan;
    unsigned int i;

    if (!remove_live( op ))
    {
        /* not an operation in flight: duplicate packet, or one for a freed channel */
        InterlockedIncrement( &stray_packets );
        printf( "stray packet: key %p overlapped %p ok %d error %lu\n", (void *)key, ov, ok, ok ? 0 : GetLastError() );
        return;
    }
    chan = op->chan;
    if ((ULONG_PTR)chan != key)
    {
        InterlockedIncrement( &wrong_key );
        printf( "wrong key: key %p channel %p\n", (void *)key, chan );
    }
    for (i = 0; i < nb_chans; i++) if (chans[i] == chan) break;
    if (i == nb_chans && !chan->closing)
    {
        InterlockedIncrement( &late_packets );
        printf( "packet for a closed channel %u\n", chan->id );
    }
    InterlockedIncrement( &ops_completed );
    if (!ok && GetLastError() == ERROR_OPERATION_ABORTED) InterlockedIncrement( &ops_aborted );
    if (!ok && GetLastError() == ERROR_BROKEN_PIPE) InterlockedIncrement( &ops_broken );
    if (op->is_read) chan->read_pending = 0;
    else chan->write_pending = 0;
    if (!chan->closing && ok) issue( chan, op->is_read );
    free( op );
}

/* Chromium's ChannelWin::Close(): cancel, wait for this channel's pending packets, free */
static void close_channel( struct channel **chans, unsigned int nb_chans, struct channel *chan )
{
    DWORD start = GetTickCount();

    chan->closing = 1;
    if (chan->child && rand() % 2)
    {
        TerminateProcess( chan->child, 1 );
        InterlockedIncrement( &children_killed );
    }
    CancelIo( chan->server );
    if (chan->client != INVALID_HANDLE_VALUE) CancelIo( chan->client );
    while (chan->read_pending || chan->write_pending)
    {
        DWORD bytes;
        ULONG_PTR key;
        OVERLAPPED *ov;
        BOOL ok = GetQueuedCompletionStatus( port, &bytes, &key, &ov, 1000 );
        if (!ov)
        {
            if (GetTickCount() - start > 5000)
            {
                printf( "channel %u: no packet for its pending %s after 5 s\n", chan->id,
                        chan->read_pending ? "read" : "write" );
                InterlockedIncrement( &missing );
                break;
            }
            continue;
        }
        handle_packet( chans, nb_chans, key, ov, ok );
    }
    CloseHandle( chan->server );
    if (chan->client != INVALID_HANDLE_VALUE) CloseHandle( chan->client );
    if (chan->child)
    {
        TerminateProcess( chan->child, 0 );
        WaitForSingleObject( chan->child, 5000 );
        CloseHandle( chan->child );
    }
    InterlockedIncrement( &channels_closed );
    /* poison like Firefox's allocator, then free: a later packet must not reach it */
    memset( chan, 0xe5, sizeof(*chan) );
    free( chan );
}

int main( int argc, char **argv )
{
    unsigned int seconds;

    if (argc == 4 && !strcmp( argv[1], "child" )) return child_main( argv[2], atoi( argv[3] ) );
    seconds = argc > 1 ? atoi( argv[1] ) : 10;
    unsigned int nb_chans = argc > 2 ? atoi( argv[2] ) : 8;
    struct channel *chans[64];
    DWORD start, bytes;
    ULONG_PTR key;
    OVERLAPPED *ov;
    unsigned int i, loops = 0;
    BOOL ok;

    if (nb_chans > 64) nb_chans = 64;
    use_procs = argc > 3 && atoi( argv[3] );
    GetModuleFileNameA( NULL, self_path, sizeof(self_path) );
    srand( 1234 );
    port = CreateIoCompletionPort( INVALID_HANDLE_VALUE, NULL, 0, 1 );
    for (i = 0; i < nb_chans; i++) chans[i] = open_channel();

    start = GetTickCount();
    while (GetTickCount() - start < seconds * 1000)
    {
        ok = GetQueuedCompletionStatus( port, &bytes, &key, &ov, 100 );
        if (ov) handle_packet( chans, nb_chans, key, ov, ok );
        if (++loops % (use_procs ? 2000 : 200) == 0)
        {
            /* replace a random channel, as when a Firefox process goes away */
            unsigned int victim = rand() % nb_chans;
            struct channel *old = chans[victim];
            chans[victim] = open_channel();
            close_channel( chans, nb_chans, old );
        }
    }
    for (i = 0; i < nb_chans; i++)
    {
        struct channel *chan = chans[i];
        chans[i] = NULL;
        close_channel( chans, nb_chans, chan );
    }
    /* nothing may arrive any more */
    while (GetQueuedCompletionStatus( port, &bytes, &key, &ov, 1000 ) || ov)
        handle_packet( chans, 0, key, ov, FALSE );
    if (nb_live) { printf( "%u operations never completed\n", nb_live ); missing += nb_live; }

    printf( "issued %ld (sync %ld), completed %ld (aborted %ld, broken pipe %ld), failed %ld, channels closed %ld (children killed %ld)\n",
            ops_issued, ops_sync, ops_completed, ops_aborted, ops_broken, ops_failed, channels_closed, children_killed );
    printf( "stray %ld, wrong key %ld, late %ld, missing %ld\n", stray_packets, wrong_key, late_packets, missing );
    ok = !stray_packets && !wrong_key && !late_packets && !missing;
    printf( "iocp: %s\n", ok ? "ok" : "FAILED" );
    return ok ? 0 : 1;
}
