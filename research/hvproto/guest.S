/*
 * Guest code for hvproto. Copied verbatim into guest memory at CODE_VA.
 * Everything here must be position independent.
 *
 * Syscall convention (from EL0): x8 = number, x0..x7 = args, svc #imm
 *   svc #0 -> exits to host (VM exit via hvc from the EL1 vector)
 *   svc #1 -> handled entirely inside the guest EL1 vector (no VM exit)
 * Host syscalls: 0 = null, 1 = print x0 (x1 = tag), 2 = exit(x0), 3 = timestamp mark
 */
    .text
    .p2align 12
    .globl _guest_blob_start
_guest_blob_start:

/* ---------------- EL1 exception vectors (2KB aligned) ---------------- */
    .globl _guest_vectors
_guest_vectors:
    .irp idx, 0,1,2,3,4,5,6,7
    .p2align 7
    hvc #(0xE0 + \idx)          /* unexpected exception at EL1 */
    b .
    .endr

    /* 0x400: lower EL, AArch64, synchronous */
    .p2align 7
    msr tpidr_el1, x9
    mrs x9, esr_el1
    lsr x9, x9, #26
    cmp x9, #0x15               /* EC = SVC from AArch64 */
    b.ne 2f
    mrs x9, esr_el1
    and x9, x9, #0xffff         /* svc immediate */
    cmp x9, #1
    mrs x9, tpidr_el1           /* mrs doesn't touch flags */
    b.ne 1f
    eret                        /* fast path: in-guest syscall */
/* After each hvc the host resumes at the eret right below it, or 4 bytes
 * further when it changed the page tables, to go through the TLB flush. */
1:  hvc #0                      /* slow path: exit to host */
    eret
    tlbi vmalle1is
    dsb ish
    isb
    eret
2:  mrs x9, tpidr_el1
    hvc #0xEF                   /* fault from EL0 -> host may fix it and resume */
    eret
    tlbi vmalle1is
    dsb ish
    isb
    eret

    .irp idx, 9,10,11,12,13,14,15
    .p2align 7
    hvc #(0xE0 + \idx)
    b .
    .endr

/* ---------------- EL0 test program ---------------- */
    .p2align 4
    .globl _guest_user_main
_guest_user_main:
    /* Test 1: read KUSER_SHARED_DATA at its Windows address 0x7ffe0000 */
    movz x0, #0x7ffe, lsl #16
    ldr x0, [x0]
    mov x1, #1
    mov x8, #1
    svc #0

    /* Test 2: x18 = TEB pointer (Windows ARM64 ABI), set by host */
    ldr x0, [x18]
    mov x1, #2
    mov x8, #1
    svc #0
    mov x0, x18                 /* x18 must survive the VM exit round trip */
    mov x1, #3
    mov x8, #1
    svc #0

    /* Test 3: lowest Windows-style address: a page at 0x10000 (our own code) */
    movz x0, #0x1, lsl #16
    ldr w0, [x0]
    mov x1, #4
    mov x8, #1
    svc #0

    /* Bench A: N syscalls that exit to the host */
    mov x8, #3
    svc #0
    movz x19, #0x1, lsl #16     /* 65536 */
1:  mov x8, #0
    svc #0
    subs x19, x19, #1
    b.ne 1b
    mov x8, #3
    svc #0

    /* Bench B: N syscalls handled inside the guest kernel */
    movz x19, #0x100, lsl #16   /* 16M */
2:  mov x8, #0
    svc #1
    subs x19, x19, #1
    b.ne 2b
    mov x8, #3
    svc #0

    mov x0, #0
    mov x8, #2
    svc #0

/* ---------------- Message-passing litmus test (TSO check) ----------------
 * x0 = shared base, x1 = iterations
 * layout: data @ +0, flag @ +128, go @ +256 (separate cache lines)
 */
    .p2align 4
    .globl _guest_mp_writer
_guest_mp_writer:
1:  ldr x3, [x0, #256]          /* wait for reader to be running */
    cbz x3, 1b
    mov x2, #1
2:  str x2, [x0]                /* data = i */
    str x2, [x0, #128]          /* flag = i */
    add x2, x2, #1
    cmp x2, x1
    b.ls 2b
    mov x0, #0
    mov x8, #2
    svc #0

    .p2align 4
    .globl _guest_mp_reader
_guest_mp_reader:
    mov x3, #0                  /* violations */
    mov x7, #0                  /* samples */
    mov x6, #1
    str x6, [x0, #256]          /* go */
1:  ldr x4, [x0, #128]          /* flag */
    ldr x5, [x0]                /* data */
    add x7, x7, #1
    cmp x5, x4
    cinc x3, x3, lo             /* data < flag: stores or loads were reordered */
    cmp x4, x1
    b.lo 1b
    mov x0, x3
    mov x1, x7
    mov x8, #2
    svc #0

/* ---------------- Shared address space test ----------------
 * Host syscalls: 4 = alloc(va or 0, size) -> va, 5 = puts(ptr),
 *                6 = free(va), 7 = protect(va, size, 1=RO/2=RW)
 */
    .p2align 4
    .globl _guest_mem_test
_guest_mem_test:
    /* A: host picks the address (>= 4GB): guest VA == host VA */
    mov x0, #0
    mov x1, #0x10000
    mov x8, #4
    svc #0
    mov x20, x0
    adr x1, msg_high
    bl copy_str
    mov x0, x20
    mov x8, #5
    svc #0

    /* B: Windows-style low address 0x20000000, aliased on the host */
    movz x0, #0x2000, lsl #16
    mov x1, #0x10000
    mov x8, #4
    svc #0
    mov x21, x0
    adr x1, msg_low
    bl copy_str
    mov x0, x21
    mov x8, #5
    svc #0

    /* C: make the low page read-only, write to it: fault -> host -> resume */
    mov x0, x21
    mov x1, #0x1000
    mov x2, #1
    mov x8, #7
    svc #0
    mov w3, #0x42
    strb w3, [x21]
    ldrb w0, [x21]
    mov x1, #5
    mov x8, #1
    svc #0

    /* E: free + realloc at the same address must read back zero (low, then high) */
    movz x0, #0x3000, lsl #16
    mov x1, #0x10000
    mov x8, #4
    svc #0
    mov x22, x0
    mov w3, #0x55
    strb w3, [x22]
    mov x0, x22
    mov x8, #6
    svc #0
    mov x0, x22
    mov x1, #0x10000
    mov x8, #4
    svc #0
    ldrb w0, [x22]
    mov x1, #6
    mov x8, #1
    svc #0

    mov x0, #0
    mov x1, #0x10000
    mov x8, #4
    svc #0
    mov x22, x0
    mov w3, #0x55
    strb w3, [x22]
    mov x0, x22
    mov x8, #6
    svc #0
    mov x0, x22
    mov x1, #0x10000
    mov x8, #4
    svc #0
    ldrb w0, [x22]
    mov x1, #7
    mov x8, #1
    svc #0

    /* D: VirtualAlloc/VirtualFree-like cycles */
    mov x8, #3
    svc #0
    mov x19, #2000
1:  mov x0, #0
    mov x1, #0x10000
    mov x8, #4
    svc #0
    strb w19, [x0]
    mov x8, #6
    svc #0
    subs x19, x19, #1
    b.ne 1b
    mov x8, #3
    svc #0

    mov x0, #0
    mov x8, #2
    svc #0

copy_str:
    ldrb w2, [x1], #1
    strb w2, [x0], #1
    cbnz w2, copy_str
    ret

msg_high: .asciz "written by guest at a high address, read by host with no translation"
msg_low:  .asciz "written by guest at 0x20000000, read by host through the alias"

/* ---------------- Coherence test: host replaces pages under a live mapping ----------------
 * 9 = host maps a file MAP_FIXED over x0, 10 = host maps fresh anon MAP_FIXED over x0 and writes a pattern
 */
    .p2align 4
    .globl _guest_coherence
_guest_coherence:
    mov x0, #0
    mov x1, #0x10000
    mov x8, #4
    svc #0
    mov x20, x0
    mov w3, #0x77
    strb w3, [x20]              /* fault the original page into stage 2 */
    ldr x0, [x20]
    mov x1, #8
    mov x8, #1
    svc #0
    mov x0, x20
    mov x8, #9
    svc #0
    ldr x0, [x20]
    mov x1, #9
    mov x8, #1
    svc #0
    mov x0, x20
    mov x8, #10
    svc #0
    ldr x0, [x20]
    mov x1, #10
    mov x8, #1
    svc #0
    mov x0, #0
    mov x8, #2
    svc #0

/* Return address for guest entry points: their return value becomes the exit code */
    .p2align 2
    .globl _guest_exit_stub
_guest_exit_stub:
    mov x8, #2
    svc #0

    .globl _guest_blob_end
_guest_blob_end:
