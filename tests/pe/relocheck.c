/* Barrique: check that every DIR64 relocation of a DLL loaded away from its
 * preferred base holds the file value plus the load delta.
 *
 *   relocheck.exe <dll path>
 *
 * The DLL is loaded with DONT_RESOLVE_DLL_REFERENCES (no DllMain, nothing else
 * runs), then the relocation table is read from the file. Prints the number of
 * relocations, the first wrong ones and "relocheck: ok" or "relocheck: FAILED".
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int main( int argc, char **argv )
{
    HANDLE file;
    DWORD size, done;
    unsigned char *data, *mod;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_SECTION_HEADER *sec;
    IMAGE_BASE_RELOCATION *rel, *end;
    ULONGLONG delta, count = 0, wrong = 0, highlow = 0;
    int i;

    if (argc < 2) { printf( "usage: relocheck <dll>\n" ); return 2; }
    file = CreateFileA( argv[1], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    if (file == INVALID_HANDLE_VALUE) { printf( "open failed %lu\n", GetLastError() ); return 2; }
    size = GetFileSize( file, NULL );
    data = malloc( size );
    ReadFile( file, data, size, &done, NULL );
    CloseHandle( file );

    mod = (unsigned char *)LoadLibraryExA( argv[1], NULL, DONT_RESOLVE_DLL_REFERENCES );
    if (!mod) { printf( "load failed %lu\n", GetLastError() ); return 2; }

    nt = (IMAGE_NT_HEADERS64 *)(data + ((IMAGE_DOS_HEADER *)data)->e_lfanew);
    delta = (ULONGLONG)mod - nt->OptionalHeader.ImageBase;
    printf( "loaded at %p, preferred %#llx, delta %#llx\n", mod, nt->OptionalHeader.ImageBase, delta );
    sec = IMAGE_FIRST_SECTION( nt );

#define RVA2FILE(rva) ({ ULONG r_ = (rva); unsigned char *p_ = NULL; \
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++) \
        if (r_ >= sec[i].VirtualAddress && r_ < sec[i].VirtualAddress + sec[i].SizeOfRawData) \
            p_ = data + sec[i].PointerToRawData + r_ - sec[i].VirtualAddress; \
    p_; })

    rel = (IMAGE_BASE_RELOCATION *)RVA2FILE( nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress );
    end = (IMAGE_BASE_RELOCATION *)((char *)rel + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size);
    while (rel < end && rel->SizeOfBlock)
    {
        USHORT *r = (USHORT *)(rel + 1);
        int n = (rel->SizeOfBlock - sizeof(*rel)) / 2, k;
        for (k = 0; k < n; k++)
        {
            ULONG rva = rel->VirtualAddress + (r[k] & 0xfff);
            unsigned char *f;
            if (r[k] >> 12 == IMAGE_REL_BASED_HIGHLOW) { highlow++; continue; }
            if (r[k] >> 12 != IMAGE_REL_BASED_DIR64) continue;
            count++;
            if (!(f = RVA2FILE( rva ))) continue;   /* in uninitialized data */
            if (*(ULONGLONG *)(mod + rva) != *(ULONGLONG *)f + delta)
            {
                if (wrong++ < 20)
                    printf( "rva %#lx: memory %#llx, expected %#llx (file %#llx)\n", rva,
                            *(ULONGLONG *)(mod + rva), *(ULONGLONG *)f + delta, *(ULONGLONG *)f );
            }
        }
        rel = (IMAGE_BASE_RELOCATION *)((char *)rel + rel->SizeOfBlock);
    }
    printf( "dir64 %llu, highlow %llu, wrong %llu\n", count, highlow, wrong );
    printf( "relocheck: %s\n", wrong ? "FAILED" : "ok" );
    return !!wrong;
}
