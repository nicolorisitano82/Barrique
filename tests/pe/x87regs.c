/* Barrique: x87 transcendental instructions must not change other registers.
 * Under FEX in ARM64EC mode with full x87 precision (the default), these
 * instructions changed r12 of the x86-64 program: Wine Gecko x64 lost a pointer
 * kept in r12 across log() (fyl2x) and crashed (2026-11-19). Barrique turns on
 * FEX's X87ReducedPrecision; this test shows whether a configuration is affected.
 * Built for x86_64 only.
 *   x87regs.exe       prints each instruction and "x87regs: ok" or "x87regs: FAILED"
 */
#include <stdio.h>
#include <string.h>

typedef unsigned long long u64;

/* run one x87 sequence (selected by index) with known values in every general
 * register but rsp, return them in out[] */
extern void run_x87( int which, u64 *out );
__asm__(
    ".globl run_x87\n"
    "run_x87:\n\t"
    "pushq %rbx\n\tpushq %rbp\n\tpushq %rsi\n\tpushq %rdi\n\t"
    "pushq %r12\n\tpushq %r13\n\tpushq %r14\n\tpushq %r15\n\t"
    "pushq %rdx\n\t"                              /* out */
    "subq $0x18, %rsp\n\t"
    "movl %ecx, %eax\n\t"
    "movabsq $0x1111111111111111, %rbx\n\t"
    "movabsq $0x2222222222222222, %rbp\n\t"
    "movabsq $0x3333333333333333, %rsi\n\t"
    "movabsq $0x4444444444444444, %rdi\n\t"
    "movabsq $0x5555555555555555, %r8\n\t"
    "movabsq $0x6666666666666666, %r9\n\t"
    "movabsq $0x7777777777777777, %r10\n\t"
    "movabsq $0x8888888888888888, %r11\n\t"
    "movabsq $0x9999999999999999, %r12\n\t"
    "movabsq $0xaaaaaaaaaaaaaaaa, %r13\n\t"
    "movabsq $0xbbbbbbbbbbbbbbbb, %r14\n\t"
    "movabsq $0xcccccccccccccccc, %r15\n\t"
    "fld1\n\tfldl2e\n\t"                          /* st1 = 1, st0 = log2(e) */
    "cmpl $0, %eax\n\tjne 1f\n\tfyl2x\n\tjmp 9f\n"
    "1:\tcmpl $1, %eax\n\tjne 2f\n\tfyl2xp1\n\tjmp 9f\n"
    "2:\tcmpl $2, %eax\n\tjne 3f\n\tfstp %st(1)\n\tfld1\n\tfsubrp\n\tf2xm1\n\tjmp 9f\n"
    "3:\tcmpl $3, %eax\n\tjne 4f\n\tfsin\n\tjmp 9f\n"
    "4:\tcmpl $4, %eax\n\tjne 5f\n\tfcos\n\tjmp 9f\n"
    "5:\tcmpl $5, %eax\n\tjne 6f\n\tfptan\n\tfstp %st(0)\n\tjmp 9f\n"
    "6:\tcmpl $6, %eax\n\tjne 7f\n\tfpatan\n\tjmp 9f\n"
    "7:\tcmpl $7, %eax\n\tjne 8f\n\tfprem\n\tjmp 9f\n"
    "8:\tfsqrt\n"
    "9:\tfstpt (%rsp)\n\tfninit\n\t"
    "movq 0x18(%rsp), %rax\n\t"                   /* out */
    "movq %rbx, 0x00(%rax)\n\tmovq %rbp, 0x08(%rax)\n\tmovq %rsi, 0x10(%rax)\n\tmovq %rdi, 0x18(%rax)\n\t"
    "movq %r8, 0x20(%rax)\n\tmovq %r9, 0x28(%rax)\n\tmovq %r10, 0x30(%rax)\n\tmovq %r11, 0x38(%rax)\n\t"
    "movq %r12, 0x40(%rax)\n\tmovq %r13, 0x48(%rax)\n\tmovq %r14, 0x50(%rax)\n\tmovq %r15, 0x58(%rax)\n\t"
    "addq $0x18, %rsp\n\tpopq %rdx\n\t"
    "popq %r15\n\tpopq %r14\n\tpopq %r13\n\tpopq %r12\n\t"
    "popq %rdi\n\tpopq %rsi\n\tpopq %rbp\n\tpopq %rbx\n\t"
    "ret\n" );

int main( void )
{
    static const char *names[] = { "fyl2x", "fyl2xp1", "f2xm1", "fsin", "fcos", "fptan", "fpatan", "fprem", "fsqrt" };
    static const char *regs[] = { "rbx", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" };
    static const u64 want[] = { 0x1111111111111111ull, 0x2222222222222222ull, 0x3333333333333333ull,
        0x4444444444444444ull, 0x5555555555555555ull, 0x6666666666666666ull, 0x7777777777777777ull,
        0x8888888888888888ull, 0x9999999999999999ull, 0xaaaaaaaaaaaaaaaaull, 0xbbbbbbbbbbbbbbbbull,
        0xccccccccccccccccull };
    int bad = 0, i, r;

    for (i = 0; i < 9; i++)
    {
        u64 out[12];
        int wrong = 0;
        memset( out, 0, sizeof(out) );
        run_x87( i, out );
        for (r = 0; r < 12; r++)
            if (out[r] != want[r]) { printf( "  %s: %s = %#llx\n", names[i], regs[r], out[r] ); wrong++; }
        printf( "%-10s %s\n", names[i], wrong ? "FAILED" : "ok" );
        if (wrong) bad = 1;
    }
    printf( "x87regs: %s\n", bad ? "FAILED" : "ok" );
    return bad;
}
