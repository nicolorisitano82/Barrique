/* Barrique: x86-64 calling convention across calls into Wine's ARM64EC DLLs.
 * Before each call the nonvolatile registers (rbx, rbp, rsi, rdi, r12-r15,
 * xmm6-xmm15, all 128 bits) and the caller's stack above the 32-byte home area
 * get known values; after the call they must be unchanged.
 * Built for x86_64 only (runs through ARM64EC and FEX).
 *   abicheck.exe      prints each function and "abicheck: ok" or "abicheck: FAILED"
 */
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>

struct regs
{
    ULONG64 gpr[8];        /* rbx rbp rsi rdi r12 r13 r14 r15 */
    ULONG64 xmm[10][2];    /* xmm6-xmm15 */
    ULONG64 stack[8];      /* caller frame above the home area */
};

/* call fn(a, b, c, d) with the registers and stack from *in, store what is
 * left after the call in *out */
extern void call_checked( void *fn, ULONG64 a, ULONG64 b, ULONG64 c, ULONG64 d,
                          const struct regs *in, struct regs *out );
__asm__(
    ".globl call_checked\n"
    "call_checked:\n\t"
    /* save the real nonvolatile registers */
    "pushq %rbx\n\tpushq %rbp\n\tpushq %rsi\n\tpushq %rdi\n\t"
    "pushq %r12\n\tpushq %r13\n\tpushq %r14\n\tpushq %r15\n\t"
    "subq $0x108, %rsp\n\t"              /* 0xa0 xmm save + 0x20 home + 0x40 stack canaries + pad */
    "movdqu %xmm6, 0x60(%rsp)\n\tmovdqu %xmm7, 0x70(%rsp)\n\tmovdqu %xmm8, 0x80(%rsp)\n\t"
    "movdqu %xmm9, 0x90(%rsp)\n\tmovdqu %xmm10, 0xa0(%rsp)\n\tmovdqu %xmm11, 0xb0(%rsp)\n\t"
    "movdqu %xmm12, 0xc0(%rsp)\n\tmovdqu %xmm13, 0xd0(%rsp)\n\tmovdqu %xmm14, 0xe0(%rsp)\n\t"
    "movdqu %xmm15, 0xf0(%rsp)\n\t"
    /* args: rcx fn, rdx a, r8 b, r9 c, stack: d, in, out (above 8 pushes + 0x108 + ret) */
    "movq %rcx, %rax\n\t"
    "movq 0x148+0x28(%rsp), %r10\n\t"    /* d */
    "movq 0x148+0x30(%rsp), %r11\n\t"    /* in */
    "movq %r11, 0x100(%rsp)\n\t"         /* keep in (pad slot) */
    /* stack canaries at 0x20..0x5f */
    "movq 0xe0(%r11), %rcx\n\tmovq %rcx, 0x20(%rsp)\n\t"
    "movq 0xe8(%r11), %rcx\n\tmovq %rcx, 0x28(%rsp)\n\t"
    "movq 0xf0(%r11), %rcx\n\tmovq %rcx, 0x30(%rsp)\n\t"
    "movq 0xf8(%r11), %rcx\n\tmovq %rcx, 0x38(%rsp)\n\t"
    "movq 0x100(%r11), %rcx\n\tmovq %rcx, 0x40(%rsp)\n\t"
    "movq 0x108(%r11), %rcx\n\tmovq %rcx, 0x48(%rsp)\n\t"
    "movq 0x110(%r11), %rcx\n\tmovq %rcx, 0x50(%rsp)\n\t"
    "movq 0x118(%r11), %rcx\n\tmovq %rcx, 0x58(%rsp)\n\t"
    /* test values in the nonvolatile registers */
    "movdqu 0x40(%r11), %xmm6\n\tmovdqu 0x50(%r11), %xmm7\n\tmovdqu 0x60(%r11), %xmm8\n\t"
    "movdqu 0x70(%r11), %xmm9\n\tmovdqu 0x80(%r11), %xmm10\n\t"
    "movdqu 0x40(%r11), %xmm11\n\tmovdqu 0x50(%r11), %xmm12\n\tmovdqu 0x60(%r11), %xmm13\n\t"
    "movdqu 0x70(%r11), %xmm14\n\tmovdqu 0x80(%r11), %xmm15\n\t"
    "movq 0x00(%r11), %rbx\n\tmovq 0x08(%r11), %rbp\n\tmovq 0x10(%r11), %rsi\n\tmovq 0x18(%r11), %rdi\n\t"
    "movq 0x20(%r11), %r12\n\tmovq 0x28(%r11), %r13\n\tmovq 0x30(%r11), %r14\n\tmovq 0x38(%r11), %r15\n\t"
    "movq %rdx, %rcx\n\tmovq %r8, %rdx\n\tmovq %r9, %r8\n\tmovq %r10, %r9\n\t"
    "callq *%rax\n\t"
    /* store what is left */
    "movq 0x148+0x38(%rsp), %r11\n\t"    /* out */
    "movq %rbx, 0x00(%r11)\n\tmovq %rbp, 0x08(%r11)\n\tmovq %rsi, 0x10(%r11)\n\tmovq %rdi, 0x18(%r11)\n\t"
    "movq %r12, 0x20(%r11)\n\tmovq %r13, 0x28(%r11)\n\tmovq %r14, 0x30(%r11)\n\tmovq %r15, 0x38(%r11)\n\t"
    "movdqu %xmm6, 0x40(%r11)\n\tmovdqu %xmm7, 0x50(%r11)\n\tmovdqu %xmm8, 0x60(%r11)\n\t"
    "movdqu %xmm9, 0x70(%r11)\n\tmovdqu %xmm10, 0x80(%r11)\n\tmovdqu %xmm11, 0x90(%r11)\n\t"
    "movdqu %xmm12, 0xa0(%r11)\n\tmovdqu %xmm13, 0xb0(%r11)\n\tmovdqu %xmm14, 0xc0(%r11)\n\t"
    "movdqu %xmm15, 0xd0(%r11)\n\t"
    "movq 0x20(%rsp), %rcx\n\tmovq %rcx, 0xe0(%r11)\n\t"
    "movq 0x28(%rsp), %rcx\n\tmovq %rcx, 0xe8(%r11)\n\t"
    "movq 0x30(%rsp), %rcx\n\tmovq %rcx, 0xf0(%r11)\n\t"
    "movq 0x38(%rsp), %rcx\n\tmovq %rcx, 0xf8(%r11)\n\t"
    "movq 0x40(%rsp), %rcx\n\tmovq %rcx, 0x100(%r11)\n\t"
    "movq 0x48(%rsp), %rcx\n\tmovq %rcx, 0x108(%r11)\n\t"
    "movq 0x50(%rsp), %rcx\n\tmovq %rcx, 0x110(%r11)\n\t"
    "movq 0x58(%rsp), %rcx\n\tmovq %rcx, 0x118(%r11)\n\t"
    /* restore the real registers */
    "movdqu 0x60(%rsp), %xmm6\n\tmovdqu 0x70(%rsp), %xmm7\n\tmovdqu 0x80(%rsp), %xmm8\n\t"
    "movdqu 0x90(%rsp), %xmm9\n\tmovdqu 0xa0(%rsp), %xmm10\n\tmovdqu 0xb0(%rsp), %xmm11\n\t"
    "movdqu 0xc0(%rsp), %xmm12\n\tmovdqu 0xd0(%rsp), %xmm13\n\tmovdqu 0xe0(%rsp), %xmm14\n\t"
    "movdqu 0xf0(%rsp), %xmm15\n\t"
    "addq $0x108, %rsp\n\t"
    "popq %r15\n\tpopq %r14\n\tpopq %r13\n\tpopq %r12\n\t"
    "popq %rdi\n\tpopq %rsi\n\tpopq %rbp\n\tpopq %rbx\n\t"
    "ret\n" );


/* qsort comparator that trashes every x86-64 volatile register, as compiled
 * code may: rax rcx rdx r8-r11, xmm0-xmm5, flags */
extern int trash_cmp( const void *a, const void *b );
__asm__(
    ".globl trash_cmp\n"
    "trash_cmp:\n\t"
    "movl (%rcx), %eax\n\tsubl (%rdx), %eax\n\t"
    "movq $-1, %r8\n\tmovq $-2, %r9\n\tmovq $-3, %r10\n\tmovq $-4, %r11\n\t"
    "pcmpeqd %xmm0, %xmm0\n\tpcmpeqd %xmm1, %xmm1\n\tpcmpeqd %xmm2, %xmm2\n\t"
    "pcmpeqd %xmm3, %xmm3\n\tpcmpeqd %xmm4, %xmm4\n\tpcmpeqd %xmm5, %xmm5\n\t"
    "movq $-5, %rcx\n\tmovq $-6, %rdx\n\t"
    "ret\n" );

static int bad;
static CRITICAL_SECTION cs;
static volatile LONG holder_ready;
static DWORD WINAPI holder( void *arg )
{
    EnterCriticalSection( &cs );
    InterlockedExchange( &holder_ready, 1 );
    Sleep( 200 );
    LeaveCriticalSection( &cs );
    return 0;
}

static void test( const char *name, void *fn, ULONG64 a, ULONG64 b, ULONG64 c, ULONG64 d )
{
    struct regs in, out;
    ULONG64 expect[36];
    int i, wrong = 0;

    for (i = 0; i < 8; i++) in.gpr[i] = 0x1111111111111111ull * (i + 1) ^ 0xa5a5000000000000ull;
    for (i = 0; i < 10; i++) { in.xmm[i][0] = 0x0102030405060708ull + i; in.xmm[i][1] = 0xf0e0d0c0b0a09080ull - i; }
    for (i = 0; i < 8; i++) in.stack[i] = 0x5a5a5a5a00000000ull + i;
    memset( &out, 0, sizeof(out) );
    call_checked( fn, a, b, c, d, &in, &out );

    /* the asm loads xmm11-15 from the same slots as xmm6-10 */
    memcpy( expect, in.gpr, 8 * 8 );
    for (i = 0; i < 10; i++) { expect[8 + 2 * i] = in.xmm[i % 5][0]; expect[9 + 2 * i] = in.xmm[i % 5][1]; }
    /* layout of out: gpr 8, xmm 20 qwords, stack 8 */
    {
        ULONG64 *o = (ULONG64 *)&out;
        static const char *names[] = { "rbx", "rbp", "rsi", "rdi", "r12", "r13", "r14", "r15" };
        for (i = 0; i < 8; i++) if (o[i] != expect[i]) { printf( "  %s: %s %#llx\n", name, names[i], o[i] ); wrong++; }
        for (i = 0; i < 20; i++)
            if (o[8 + i] != expect[8 + i]) { printf( "  %s: xmm%d.%s %#llx\n", name, 6 + i / 2, i & 1 ? "hi" : "lo", o[8 + i] ); wrong++; }
        for (i = 0; i < 8; i++)
            if (o[28 + i] != in.stack[i]) { printf( "  %s: caller stack +%#x %#llx\n", name, 0x20 + 8 * i, o[28 + i] ); wrong++; }
    }
    printf( "%-28s %s\n", name, wrong ? "FAILED" : "ok" );
    if (wrong) bad = 1;
}

int main( void )
{
    MEMORYSTATUSEX ms = { sizeof(ms) };
    GUID guid;
    char buf[64];
    HANDLE event = CreateEventA( NULL, TRUE, TRUE, NULL );
    HMODULE k32 = GetModuleHandleA( "kernel32.dll" ), nt = GetModuleHandleA( "ntdll.dll" );
    HMODULE ole = LoadLibraryA( "ole32.dll" ), ucrt = LoadLibraryA( "ucrtbase.dll" );
    void *heap = GetProcessHeap();
    SYSTEM_INFO si;

    setvbuf( stdout, NULL, _IONBF, 0 );

    test( "GetCurrentThreadId", GetProcAddress( k32, "GetCurrentThreadId" ), 0, 0, 0, 0 );
    test( "GlobalMemoryStatusEx", GetProcAddress( k32, "GlobalMemoryStatusEx" ), (ULONG64)&ms, 0, 0, 0 );
    test( "GetSystemInfo", GetProcAddress( k32, "GetSystemInfo" ), (ULONG64)&si, 0, 0, 0 );
    test( "CoCreateGuid", GetProcAddress( ole, "CoCreateGuid" ), (ULONG64)&guid, 0, 0, 0 );
    test( "Sleep(0)", GetProcAddress( k32, "Sleep" ), 0, 0, 0, 0 );
    test( "WaitForSingleObject", GetProcAddress( k32, "WaitForSingleObject" ), (ULONG64)event, 100, 0, 0 );
    test( "RtlAllocateHeap", GetProcAddress( nt, "RtlAllocateHeap" ), (ULONG64)heap, 0, 64, 0 );
    test( "ucrtbase.malloc", GetProcAddress( ucrt, "malloc" ), 100, 0, 0, 0 );
    test( "ucrtbase.strlen", GetProcAddress( ucrt, "strlen" ), (ULONG64)"hello", 0, 0, 0 );
    test( "ucrtbase.strtol", GetProcAddress( ucrt, "strtol" ), (ULONG64)"12345", 0, 10, 0 );
    test( "NtQuerySystemInformation", GetProcAddress( nt, "NtQuerySystemInformation" ), 0, (ULONG64)buf, 64, 0 );
    test( "QueryPerformanceCounter", GetProcAddress( k32, "QueryPerformanceCounter" ), (ULONG64)buf, 0, 0, 0 );
    {
        static int v[64];
        int i, sorted = 1;
        for (i = 0; i < 64; i++) v[i] = (i * 37) % 64;
        test( "ucrtbase.qsort (x64 callback)", GetProcAddress( ucrt, "qsort" ), (ULONG64)v, 64, sizeof(int), (ULONG64)trash_cmp );
        for (i = 1; i < 64; i++) if (v[i - 1] > v[i]) sorted = 0;
        printf( "%-28s %s\n", "  qsort result", sorted ? "ok" : "FAILED" );
        if (!sorted) bad = 1;
    }
    InitializeCriticalSection( &cs );
    test( "EnterCriticalSection", GetProcAddress( k32, "EnterCriticalSection" ), (ULONG64)&cs, 0, 0, 0 );
    test( "LeaveCriticalSection", GetProcAddress( k32, "LeaveCriticalSection" ), (ULONG64)&cs, 0, 0, 0 );
    {
        DWORD idx = TlsAlloc();
        TlsSetValue( idx, (void *)0x1234 );
        test( "TlsGetValue", GetProcAddress( k32, "TlsGetValue" ), idx, 0, 0, 0 );
    }
    {
        HANDLE t = CreateThread( NULL, 0, holder, NULL, 0, NULL );
        while (!holder_ready) Sleep( 1 );
        test( "EnterCriticalSection (contended)", GetProcAddress( k32, "EnterCriticalSection" ), (ULONG64)&cs, 0, 0, 0 );
        test( "LeaveCriticalSection (waiter)", GetProcAddress( k32, "LeaveCriticalSection" ), (ULONG64)&cs, 0, 0, 0 );
        WaitForSingleObject( t, INFINITE );
    }
    test( "GetModuleHandleW", GetProcAddress( k32, "GetModuleHandleW" ), 0, 0, 0, 0 );
    printf( "abicheck: %s\n", bad ? "FAILED" : "ok" );
    return bad;
}
