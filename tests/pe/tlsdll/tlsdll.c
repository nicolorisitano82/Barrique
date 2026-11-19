/* Barrique: a DLL with static TLS (_Thread_local), loaded at run time by tlstest.c */
#include <windows.h>
static _Thread_local ULONG_PTR tls_a = 0x1111;
static _Thread_local ULONG_PTR tls_b[4] = { 1, 2, 3, 4 };
__declspec(dllexport) ULONG_PTR *get_a(void) { return &tls_a; }
__declspec(dllexport) ULONG_PTR *get_b(void) { return tls_b; }
BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, void *reserved ) { return TRUE; }
