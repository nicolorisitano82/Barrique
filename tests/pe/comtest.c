/* Barrique: talk to a serial device through a COM port (USB serial adapters on
 * macOS become COMn, see mountmgr). Default: a Digital Logic uFR reader (FTDI),
 * GET_READER_TYPE (0x10) and GET_FIRMWARE_VERSION (0x29) at 1 Mbps.
 *
 *   comtest.exe [COMn] [baud]     prints the bytes sent and received
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static void dump( const char *what, const BYTE *buf, DWORD len )
{
    DWORD i;
    printf( "%s %lu:", what, len );
    for (i = 0; i < len; i++) printf( " %02x", buf[i] );
    printf( "\n" );
}

static int ufr_cmd( HANDLE port, BYTE cmd )
{
    BYTE req[7] = { 0x55, cmd, 0xaa, 0, 0, 0, 0 }, reply[64];
    DWORD done, total = 0, i;

    for (i = 0; i < 6; i++) req[6] ^= req[i];
    req[6] += 7;
    PurgeComm( port, PURGE_RXCLEAR | PURGE_TXCLEAR );
    if (!WriteFile( port, req, sizeof(req), &done, NULL ) || done != sizeof(req))
    {
        printf( "WriteFile failed %lu\n", GetLastError() );
        return 0;
    }
    dump( "sent", req, done );
    while (total < sizeof(reply) && ReadFile( port, reply + total, sizeof(reply) - total, &done, NULL ) && done)
        total += done;
    dump( "got ", reply, total );
    return total >= 7 && reply[0] == 0xde && reply[1] == cmd && reply[2] == 0xed;
}

int main( int argc, char **argv )
{
    char name[32];
    DCB dcb = { sizeof(dcb) };
    COMMTIMEOUTS to = { 0, 0, 300, 0, 300 };
    HANDLE port;
    int ok;

    snprintf( name, sizeof(name), "\\\\.\\%s", argc > 1 ? argv[1] : "COM1" );
    port = CreateFileA( name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL );
    if (port == INVALID_HANDLE_VALUE)
    {
        printf( "comtest: cannot open %s: %lu\n", name, GetLastError() );
        return 1;
    }
    if (!GetCommState( port, &dcb )) printf( "GetCommState failed %lu\n", GetLastError() );
    dcb.BaudRate = argc > 2 ? atoi( argv[2] ) : 1000000;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fOutxCtsFlow = dcb.fOutxDsrFlow = dcb.fOutX = dcb.fInX = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    if (!SetCommState( port, &dcb )) printf( "SetCommState failed %lu\n", GetLastError() );
    SetCommTimeouts( port, &to );
    Sleep( 100 );

    ok = ufr_cmd( port, 0x10 );
    ok = ufr_cmd( port, 0x29 ) && ok;
    printf( "comtest: %s\n", ok ? "reader answered" : "no valid answer" );
    CloseHandle( port );
    return !ok;
}
