/* Barrique: print the process command line (GetCommandLineW), to check what
 * ntdll adds to it, e.g. --in-process-gpu for CEF browser processes. */
#include <windows.h>
#include <stdio.h>

int main(void)
{
    printf("cmdline: %ls\n", GetCommandLineW());
    return 0;
}
