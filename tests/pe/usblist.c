/* Barrique: list the USB devices Windows programs see (wineusb.sys over libusb):
 * every present device under the USB enumerator, with its hardware ID, description
 * and the service (driver) bound to it, if any.
 *
 *   usblist.exe       prints one line per device and "usblist: N devices"
 */
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>

static void prop( HDEVINFO set, SP_DEVINFO_DATA *dev, DWORD what, char *buf, DWORD size )
{
    buf[0] = 0;
    if (!SetupDiGetDeviceRegistryPropertyA( set, dev, what, NULL, (BYTE *)buf, size, NULL )) buf[0] = 0;
}

int main( void )
{
    HDEVINFO set = SetupDiGetClassDevsA( NULL, "USB", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT );
    SP_DEVINFO_DATA dev = { sizeof(dev) };
    char hwid[512], desc[256], service[128];
    DWORD i;

    if (set == INVALID_HANDLE_VALUE)
    {
        printf( "SetupDiGetClassDevs failed %lu\n", GetLastError() );
        return 1;
    }
    for (i = 0; SetupDiEnumDeviceInfo( set, i, &dev ); i++)
    {
        prop( set, &dev, SPDRP_HARDWAREID, hwid, sizeof(hwid) );
        prop( set, &dev, SPDRP_DEVICEDESC, desc, sizeof(desc) );
        prop( set, &dev, SPDRP_SERVICE, service, sizeof(service) );
        printf( "%-40s %-30s driver %s\n", hwid, desc, service[0] ? service : "(none)" );
    }
    printf( "usblist: %lu devices\n", i );
    SetupDiDestroyDeviceInfoList( set );
    return 0;
}
