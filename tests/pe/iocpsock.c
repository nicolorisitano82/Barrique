/* Barrique: TCP over loopback with overlapped sockets and an I/O completion port,
 * the way Steam's web UI transport and Chromium's IPC work: a server thread
 * accepts connections with AcceptEx and echoes with overlapped WSARecv/WSASend
 * completed on an IOCP by worker threads; client threads send small messages and
 * wait for each echo. A lost completion shows up as a client that waits forever.
 *
 *   iocpsock.exe [clients] [messages per client]     default 4 x 20000
 *
 * Prints the round trips and "iocpsock: ok", or the stuck clients and
 * "iocpsock: FAILED" (exit code 1).
 */
#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define MSG_SIZE 64
#define MAX_CLIENTS 64

struct conn
{
    OVERLAPPED ov;
    SOCKET s;
    int op;               /* 0 accept, 1 recv, 2 send */
    char buf[2 * (sizeof(struct sockaddr_in) + 16) + MSG_SIZE];
    WSABUF wb;
};

static HANDLE port;
static SOCKET listener;
static LPFN_ACCEPTEX pAcceptEx;
static volatile LONG done_clients, progress[MAX_CLIENTS];
static int clients = 4, messages = 20000;
static unsigned short port_number;

static void post_accept(void)
{
    struct conn *c = calloc( 1, sizeof(*c) );
    DWORD bytes;
    c->s = WSASocketW( AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED );
    c->op = 0;
    if (!pAcceptEx( listener, c->s, c->buf, 0, sizeof(struct sockaddr_in) + 16, sizeof(struct sockaddr_in) + 16,
                    &bytes, &c->ov ) && WSAGetLastError() != ERROR_IO_PENDING)
        printf( "AcceptEx failed %d\n", WSAGetLastError() );
}

static void post_recv( struct conn *c )
{
    DWORD flags = 0;
    c->op = 1;
    c->wb.buf = c->buf;
    c->wb.len = MSG_SIZE;
    memset( &c->ov, 0, sizeof(c->ov) );
    if (WSARecv( c->s, &c->wb, 1, NULL, &flags, &c->ov, NULL ) && WSAGetLastError() != WSA_IO_PENDING)
        c->op = -1;
}

static DWORD WINAPI worker( void *arg )
{
    for (;;)
    {
        DWORD bytes;
        ULONG_PTR key;
        OVERLAPPED *ov;
        struct conn *c;

        if (!GetQueuedCompletionStatus( port, &bytes, &key, &ov, INFINITE ) && !ov) break;
        if (!ov) break;   /* quit */
        c = CONTAINING_RECORD( ov, struct conn, ov );
        switch (c->op)
        {
        case 0:   /* accepted: attach to the port and start reading; accept the next one */
            setsockopt( c->s, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)&listener, sizeof(listener) );
            CreateIoCompletionPort( (HANDLE)c->s, port, 0, 0 );
            post_accept();
            post_recv( c );
            break;
        case 1:   /* received: echo it */
            if (!bytes) { closesocket( c->s ); free( c ); break; }
            c->op = 2;
            c->wb.len = bytes;
            memset( &c->ov, 0, sizeof(c->ov) );
            if (WSASend( c->s, &c->wb, 1, NULL, 0, &c->ov, NULL ) && WSAGetLastError() != WSA_IO_PENDING)
                c->op = -1;
            break;
        case 2:   /* sent: read the next one */
            post_recv( c );
            break;
        }
    }
    return 0;
}

static DWORD WINAPI client( void *arg )
{
    int id = (int)(INT_PTR)arg, i;
    struct sockaddr_in sa = { AF_INET };
    SOCKET s = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    char msg[MSG_SIZE], back[MSG_SIZE];
    BOOL nodelay = TRUE;

    sa.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
    sa.sin_port = port_number;
    if (connect( s, (struct sockaddr *)&sa, sizeof(sa) )) { printf( "client %d: connect %d\n", id, WSAGetLastError() ); return 1; }
    setsockopt( s, IPPROTO_TCP, TCP_NODELAY, (char *)&nodelay, sizeof(nodelay) );
    for (i = 0; i < messages; i++)
    {
        int got = 0, r;
        memset( msg, 'a' + (i % 26), sizeof(msg) );
        msg[0] = (char)id;
        if (send( s, msg, sizeof(msg), 0 ) != sizeof(msg)) { printf( "client %d: send %d\n", id, WSAGetLastError() ); break; }
        while (got < MSG_SIZE && (r = recv( s, back + got, MSG_SIZE - got, 0 )) > 0) got += r;
        if (got != MSG_SIZE || memcmp( msg, back, MSG_SIZE )) { printf( "client %d: bad echo at %d\n", id, i ); break; }
        InterlockedIncrement( &progress[id] );
    }
    closesocket( s );
    InterlockedIncrement( &done_clients );
    return 0;
}

int main( int argc, char **argv )
{
    WSADATA wsa;
    GUID guid = WSAID_ACCEPTEX;
    struct sockaddr_in sa = { AF_INET };
    int len = sizeof(sa), i, stuck = 0, workers = 4;
    DWORD bytes, start;
    LONG last[MAX_CLIENTS] = {0};

    if (argc > 1) clients = min( atoi( argv[1] ), MAX_CLIENTS );
    if (argc > 2) messages = atoi( argv[2] );
    WSAStartup( MAKEWORD( 2, 2 ), &wsa );
    listener = WSASocketW( AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED );
    sa.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
    bind( listener, (struct sockaddr *)&sa, sizeof(sa) );
    getsockname( listener, (struct sockaddr *)&sa, &len );
    port_number = sa.sin_port;
    listen( listener, SOMAXCONN );
    WSAIoctl( listener, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &pAcceptEx, sizeof(pAcceptEx),
              &bytes, NULL, NULL );
    port = CreateIoCompletionPort( INVALID_HANDLE_VALUE, NULL, 0, 0 );
    CreateIoCompletionPort( (HANDLE)listener, port, 0, 0 );
    for (i = 0; i < workers; i++) CreateThread( NULL, 0, worker, NULL, 0, NULL );
    post_accept();
    for (i = 0; i < clients; i++) CreateThread( NULL, 0, client, (void *)(INT_PTR)i, 0, NULL );

    /* watchdog: a client that makes no progress for 5 s is stuck */
    start = GetTickCount();
    while (done_clients < clients)
    {
        int moving = 0;
        Sleep( 5000 );
        for (i = 0; i < clients; i++)
        {
            if (progress[i] != last[i] || progress[i] == messages) moving++;
            last[i] = progress[i];
        }
        if (moving < clients && done_clients < clients)
        {
            for (i = 0; i < clients; i++)
                if (progress[i] < messages) { printf( "client %d stuck after %ld round trips\n", i, progress[i] ); stuck++; }
            break;
        }
    }
    {
        LONG total = 0;
        for (i = 0; i < clients; i++) total += progress[i];
        printf( "%ld round trips in %.1f s\n", total, (GetTickCount() - start) / 1000.0 );
    }
    printf( "iocpsock: %s\n", stuck ? "FAILED" : "ok" );
    fflush( stdout );
    ExitProcess( stuck ? 1 : 0 );
}
