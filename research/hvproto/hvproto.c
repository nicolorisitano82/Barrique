/*
 * hvproto: run Windows-style arm64 user code inside a Hypervisor.framework VM.
 *
 * Proves, on a macOS arm64 host:
 *   - guest gets its own address space: pages below 4GB (0x10000, 0x7ffe0000)
 *   - 4K stage-1 granule (Windows page size), independent of host 16K pages
 *   - x18 belongs to the guest (Windows TEB register)
 *   - syscalls trap EL0 -> guest EL1 -> host, and their cost
 *   - Apple TSO memory ordering toggled per-vCPU via ACTLR_EL1
 */
#include <Hypervisor/Hypervisor.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

static const char *coherence_file;

#define CHECK(x) do { hv_return_t _r = (x); if (_r != HV_SUCCESS) { \
    fprintf(stderr, "%s:%d %s failed: 0x%x\n", __FILE__, __LINE__, #x, _r); exit(1); } } while (0)

/* guest physical memory */
#define IPA_BASE  0x40000000ULL
#define MEM_SIZE  (64ULL << 20)
#define PAGE      0x1000ULL

/* guest virtual layout (Windows-like low addresses) */
#define CODE_VA   0x10000ULL      /* lowest allocatable address on Windows */
#define STACK_VA  0x100000ULL     /* 64K per vCPU */
#define SHARED_VA 0x200000ULL     /* litmus test data */
#define TEB_VA    0x7ff00000ULL
#define KUSD_VA   0x7ffe0000ULL   /* KUSER_SHARED_DATA */

/* stage-1 descriptor bits, 4K granule */
#define D_VALID   (1ULL << 0)
#define D_TABLE   (1ULL << 1)     /* also "page" at level 3 */
#define D_ATTR(i) ((uint64_t)(i) << 2)
#define D_AP_EL1RW   (0ULL << 6)
#define D_AP_RW      (1ULL << 6)
#define D_AP_RO      (3ULL << 6)
#define D_SH_INNER (3ULL << 8)
#define D_AF      (1ULL << 10)
#define D_PXN     (1ULL << 53)
#define D_UXN     (1ULL << 54)

#define ACTLR_APPLE_TSO (1ULL << 1)

extern const char guest_blob_start[], guest_blob_end[], guest_vectors[],
                  guest_user_main[], guest_mp_writer[], guest_mp_reader[], guest_mem_test[],
                  guest_exit_stub[], guest_coherence[];

static uint8_t *mem;           /* host view of guest RAM */
static uint64_t next_free = IPA_BASE;
static uint64_t ttbr0;

static void *host_ptr(uint64_t ipa) { return mem + (ipa - IPA_BASE); }

static uint64_t alloc_page(void)
{
    uint64_t ipa = next_free;
    next_free += PAGE;
    if (next_free > IPA_BASE + MEM_SIZE) { fprintf(stderr, "out of guest memory\n"); exit(1); }
    return ipa;   /* already zero: anonymous mmap */
}

#define PTE_ADDR_MASK 0x0000fffffffff000ULL

static uint64_t *pte_for(uint64_t va)
{
    uint64_t table = ttbr0;
    for (int level = 0; level < 3; level++)
    {
        uint64_t *t = host_ptr(table);
        unsigned idx = (va >> (39 - 9 * level)) & 0x1ff;
        if (!(t[idx] & D_VALID)) t[idx] = alloc_page() | D_TABLE | D_VALID;
        table = t[idx] & PTE_ADDR_MASK;
    }
    return (uint64_t *)host_ptr(table) + ((va >> 12) & 0x1ff);
}

static void map_page(uint64_t va, uint64_t ipa, uint64_t flags)
{
    *pte_for(va) = ipa | flags | D_AF | D_SH_INNER | D_ATTR(0) | D_TABLE | D_VALID;
}

/* ------------------------------------------------------------------------
 * Shared address space between guest and host.
 *
 *  guest VA >= 4GB : host VA == guest VA. Pointers are valid on both sides.
 *  guest VA <  4GB : the host can't map there (__PAGEZERO is 4GB on arm64),
 *                    so the backing memory lives at guest VA + LOW_ALIAS.
 *
 * Two arenas are mapped into the VM once at startup, so an allocation only
 * edits guest page tables (no hv_vm_map per allocation):
 *   low arena : guest VA [0, 4GB)        host LOW_ALIAS + va       IPA LOW_IPA + va
 *   high arena: guest VA == host VA      host arena + off          IPA ARENA_IPA + off
 * Both are untouched anonymous memory: no physical cost until used.
 * Allocation granularity is 64K, like Windows.
 */
#define LOW_LIMIT  0x100000000ULL
#define LOW_ALIAS  0x600000000000ULL    /* 96TB; 0x10_0000_0000..0x70_0000_0000 is taken by macOS */
#define LOW_IPA    0x1000000000ULL      /* IPA space is 40 bits on this machine */
#define ARENA_SIZE (64ULL << 30)
#define ARENA_IPA  0x2000000000ULL
#define CHUNK      0x10000ULL

static uint8_t *arena;
static uint64_t bm_low[LOW_LIMIT / CHUNK / 64];
static uint64_t bm_high[ARENA_SIZE / CHUNK / 64];
static uint64_t high_hint;

struct region { uint64_t va, size, ipa; };
static struct region regions[256];

static inline void *g2h(uint64_t va) { return (void *)(va < LOW_LIMIT ? va + LOW_ALIAS : va); }
static inline int in_arena(uint64_t va) { return va >= (uint64_t)arena && va < (uint64_t)arena + ARENA_SIZE; }

/* bumped whenever a valid PTE is changed or removed; vCPUs flush their TLB when it moves */
static _Atomic uint64_t tlb_generation;

static void *map_arena(void *want, uint64_t size, uint64_t ipa)
{
    void *p = mmap(want, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED || (want && p != want)) { fprintf(stderr, "cannot reserve arena at %p (got %p)\n", want, p); exit(1); }
    CHECK(hv_vm_map(p, ipa, size, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC));
    return p;
}

static void setup_arenas(void)
{
    map_arena((void *)LOW_ALIAS, LOW_LIMIT, LOW_IPA);
    arena = map_arena(NULL, ARENA_SIZE, ARENA_IPA);
    if ((uint64_t)arena < LOW_LIMIT) { fprintf(stderr, "arena below 4GB?\n"); exit(1); }
}

static int bm_free(const uint64_t *bm, uint64_t first, uint64_t n)
{
    for (uint64_t i = first; i < first + n; i++) if (bm[i / 64] & (1ULL << (i % 64))) return 0;
    return 1;
}

static void bm_set(uint64_t *bm, uint64_t first, uint64_t n, int on)
{
    for (uint64_t i = first; i < first + n; i++)
        if (on) bm[i / 64] |= 1ULL << (i % 64); else bm[i / 64] &= ~(1ULL << (i % 64));
}

/* next-fit search in the high arena */
static int64_t bm_find(const uint64_t *bm, uint64_t total, uint64_t n, uint64_t *hint)
{
    for (uint64_t pass = 0, i = *hint; pass < total; )
    {
        if (i + n > total) { pass += total - i; i = 0; continue; }
        if (!(bm[i / 64] & (1ULL << (i % 64))) && bm_free(bm, i, n)) { *hint = i + n; return i; }
        i++; pass++;
    }
    return -1;
}

static uint64_t guest_alloc(uint64_t va, uint64_t size, uint64_t flags)
{
    size = (size + CHUNK - 1) & ~(CHUNK - 1);
    uint64_t n = size / CHUNK, ipa;
    if (va & (CHUNK - 1)) return 0;

    if (!va)
    {
        int64_t idx = bm_find(bm_high, ARENA_SIZE / CHUNK, n, &high_hint);
        if (idx < 0) return 0;
        bm_set(bm_high, idx, n, 1);
        va = (uint64_t)arena + idx * CHUNK;
        ipa = ARENA_IPA + idx * CHUNK;
    }
    else if (va < LOW_LIMIT)
    {
        if (va + size > LOW_LIMIT || !bm_free(bm_low, va / CHUNK, n)) return 0;
        bm_set(bm_low, va / CHUNK, n, 1);
        ipa = LOW_IPA + va;
    }
    else if (in_arena(va) && in_arena(va + size - 1))
    {
        uint64_t idx = (va - (uint64_t)arena) / CHUNK;
        if (!bm_free(bm_high, idx, n)) return 0;
        bm_set(bm_high, idx, n, 1);
        ipa = ARENA_IPA + idx * CHUNK;
    }
    else return 0;   /* would collide with host memory: STATUS_CONFLICTING_ADDRESSES */

    struct region *r = NULL;
    for (unsigned i = 0; i < 256; i++) if (!regions[i].size) { r = &regions[i]; break; }
    if (!r) return 0;
    for (uint64_t off = 0; off < size; off += PAGE) map_page(va + off, ipa + off, flags);
    *r = (struct region){ va, size, ipa };
    return va;
}

static struct region *find_region(uint64_t va)
{
    for (unsigned i = 0; i < 256; i++)
        if (regions[i].size && va >= regions[i].va && va < regions[i].va + regions[i].size)
            return &regions[i];
    return NULL;
}

static int guest_free(uint64_t va)
{
    struct region *r = find_region(va);
    if (!r || r->va != va) return -1;
    for (uint64_t off = 0; off < r->size; off += PAGE) *pte_for(va + off) = 0;
    tlb_generation++;
    /* drop the pages so the next allocation sees zeroes (Windows semantics) */
    if (madvise(g2h(va), r->size, MADV_ZERO)) perror("madvise");
    if (va < LOW_LIMIT) bm_set(bm_low, va / CHUNK, r->size / CHUNK, 0);
    else bm_set(bm_high, (va - (uint64_t)arena) / CHUNK, r->size / CHUNK, 0);
    r->size = 0;
    return 0;
}

static int guest_protect(uint64_t va, uint64_t size, uint64_t flags)
{
    struct region *r = find_region(va);
    if (!r) return -1;
    for (uint64_t off = 0; off < size; off += PAGE)
        map_page(va + off, r->ipa + (va + off - r->va), flags);
    tlb_generation++;
    return 0;
}

#define PROT_GUEST_RO (D_AP_RO | D_UXN | D_PXN)
#define PROT_GUEST_RW (D_AP_RW | D_UXN | D_PXN)

static uint64_t code_off(const char *sym) { return CODE_VA + (sym - guest_blob_start); }

static double ns_per_tick;

/* ------------------------------------------------------------------------ */

struct vcpu_job
{
    const char *name;
    uint64_t pc, x0, x1, sp, lr;
    int tso;
    uint64_t result, result2;
    uint64_t marks[8];
    int nmarks;
    int verbose;
};

static void setup_vcpu(hv_vcpu_t vcpu, struct vcpu_job *job)
{
    /* MMU: 4K granule, 48-bit VA, TTBR0 only */
    CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_MAIR_EL1, 0xff));   /* attr0 normal WB */
    CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TCR_EL1,
          16 | (1ULL << 8) | (1ULL << 10) | (3ULL << 12) /* T0SZ, WB, inner shareable */
          | (0ULL << 14)                                  /* TG0 = 4K */
          | (1ULL << 23)                                  /* EPD1: no TTBR1 walks */
          | (2ULL << 32)));                               /* IPS = 40 bits */
    CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TTBR0_EL1, ttbr0));
    CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SCTLR_EL1,
          0x30d00800ULL | (1 << 0) | (1 << 2) | (1 << 12)  /* RES1, M, C, I */
          | (1 << 14) | (1 << 15) | (1 << 16) | (1 << 18) | (1 << 26))); /* DZE UCT nTWI nTWE UCI */
    CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_CPACR_EL1, 3ULL << 20)); /* FP/SIMD at EL0/EL1 */
    CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_VBAR_EL1, code_off(guest_vectors)));
    CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL0, job->sp));
    CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL1, job->sp - 0x8000));

    uint64_t actlr;
    CHECK(hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_ACTLR_EL1, &actlr));
    actlr = job->tso ? (actlr | ACTLR_APPLE_TSO) : (actlr & ~ACTLR_APPLE_TSO);
    CHECK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_ACTLR_EL1, actlr));
    CHECK(hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_ACTLR_EL1, &actlr));
    if (job->verbose) printf("[%s] ACTLR_EL1 = 0x%llx (TSO %s)\n", job->name, actlr,
                             (actlr & ACTLR_APPLE_TSO) ? "on" : "off");
    if (!!(actlr & ACTLR_APPLE_TSO) != !!job->tso)
        printf("[%s] warning: TSO bit did not stick\n", job->name);

    CHECK(hv_vcpu_set_reg(vcpu, HV_REG_X0, job->x0));
    CHECK(hv_vcpu_set_reg(vcpu, HV_REG_X1, job->x1));
    CHECK(hv_vcpu_set_reg(vcpu, HV_REG_X18, TEB_VA));
    CHECK(hv_vcpu_set_reg(vcpu, HV_REG_LR, job->lr ? job->lr : code_off(guest_exit_stub)));
    CHECK(hv_vcpu_set_reg(vcpu, HV_REG_PC, job->pc));
    CHECK(hv_vcpu_set_reg(vcpu, HV_REG_CPSR, 0x3c0 | 0x0)); /* EL0t, DAIF masked */
}

static const char *tag_name(uint64_t tag)
{
    switch (tag)
    {
    case 1: return "KUSER_SHARED_DATA @0x7ffe0000";
    case 2: return "[x18] (TEB magic)";
    case 3: return "x18 after VM exit";
    case 4: return "first word @0x10000";
    case 5: return "byte after RO fault + resume";
    case 6: return "low realloc'd byte (want 0)";
    case 7: return "high realloc'd byte (want 0)";
    case 8: return "before remap (want 0x77)";
    case 9: return "after file MAP_FIXED (want MZ..)";
    case 10: return "after anon MAP_FIXED + host write";
    default: return "value";
    }
}

static void *vcpu_thread(void *arg)
{
    struct vcpu_job *job = arg;
    hv_vcpu_t vcpu;
    hv_vcpu_exit_t *exit_info;

    CHECK(hv_vcpu_create(&vcpu, &exit_info, NULL));
    setup_vcpu(vcpu, job);

    uint64_t seen_gen = tlb_generation;
    int at_hvc_return = 0;
    for (;;)
    {
        if (at_hvc_return && seen_gen != tlb_generation)
        {
            uint64_t pc;
            hv_vcpu_get_reg(vcpu, HV_REG_PC, &pc);
            hv_vcpu_set_reg(vcpu, HV_REG_PC, pc + 4);   /* take the TLB flush path */
            seen_gen = tlb_generation;
        }
        at_hvc_return = 0;
        CHECK(hv_vcpu_run(vcpu));
        if (exit_info->reason != HV_EXIT_REASON_EXCEPTION)
        {
            if (exit_info->reason == HV_EXIT_REASON_VTIMER_ACTIVATED) continue;
            fprintf(stderr, "[%s] unexpected exit reason %d\n", job->name, exit_info->reason);
            break;
        }
        uint64_t syndrome = exit_info->exception.syndrome;
        unsigned ec = syndrome >> 26, imm = syndrome & 0xffff;
        if (ec != 0x16)
        {
            uint64_t pc; hv_vcpu_get_reg(vcpu, HV_REG_PC, &pc);
            fprintf(stderr, "[%s] unexpected EL2 exception ec=0x%x syndrome=0x%llx pc=0x%llx va=0x%llx\n",
                    job->name, ec, syndrome, pc, exit_info->exception.virtual_address);
            break;
        }
        at_hvc_return = 1;
        if (imm != 0)
        {
            uint64_t esr, far, elr;
            hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_ESR_EL1, &esr);
            hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_FAR_EL1, &far);
            hv_vcpu_get_sys_reg(vcpu, HV_SYS_REG_ELR_EL1, &elr);
            /* data abort, permission fault, on a page we own: like a Windows
             * write-watch / guard page. Make it writable and retry. */
            if (imm == 0xEF && (esr >> 26) == 0x24 && ((esr & 0x3c) == 0x0c) && find_region(far))
            {
                printf("[%s] write fault at 0x%llx (pc 0x%llx): host makes page RW, resumes\n",
                       job->name, far, elr);
                guest_protect(far & ~(PAGE - 1), PAGE, PROT_GUEST_RW);
                continue;
            }
            fprintf(stderr, "[%s] guest exception vector 0x%x: ESR_EL1=0x%llx FAR=0x%llx ELR=0x%llx\n",
                    job->name, imm, esr, far, elr);
            break;
        }

        uint64_t nr, x0, x1;
        hv_vcpu_get_reg(vcpu, HV_REG_X8, &nr);
        hv_vcpu_get_reg(vcpu, HV_REG_X0, &x0);
        hv_vcpu_get_reg(vcpu, HV_REG_X1, &x1);
        if (nr == 0) continue;                  /* null syscall */
        if (nr == 1) { printf("[%s] %-32s = 0x%llx\n", job->name, tag_name(x1), x0); continue; }
        if (nr == 3) { if (job->nmarks < 8) job->marks[job->nmarks++] = mach_absolute_time(); continue; }
        if (nr == 2) { job->result = x0; job->result2 = x1; break; }
        if (nr == 4)
        {
            uint64_t va = guest_alloc(x0, x1, PROT_GUEST_RW);
            if (!job->nmarks)   /* quiet once the benchmark loop starts */
                printf("[%s] alloc(0x%llx, 0x%llx) -> guest 0x%llx, host %p\n",
                       job->name, x0, x1, va, va ? g2h(va) : NULL);
            hv_vcpu_set_reg(vcpu, HV_REG_X0, va);
            continue;
        }
        if (nr == 9 || nr == 10)
        {
            void *want = g2h(x0), *p;
            if (nr == 9)
            {
                int fd = open(coherence_file, O_RDONLY);
                p = mmap(want, 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd, 0);
                close(fd);
            }
            else
            {
                p = mmap(want, 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
                if (p == want) *(volatile uint64_t *)p = 0x1122334455667788ULL;
            }
            if (p != want) perror("mmap MAP_FIXED");
            if (getenv("REMAP"))
            {
                struct region *r = find_region(x0);
                uint64_t ipa = r->ipa + (x0 - r->va);
                uint64_t t0 = mach_absolute_time();
                CHECK(hv_vm_unmap(ipa, 0x10000));
                CHECK(hv_vm_map(want, ipa, 0x10000, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC));
                printf("[%s] partial stage-2 remap: %.1f us\n", job->name, (mach_absolute_time() - t0) * ns_per_tick / 1000);
            }
            continue;
        }
        if (nr == 8) { printf("[%s] %-32s = 0x%llx\n", job->name, (char *)g2h(x0), x1); continue; }
        if (nr == 5) { printf("[%s] host reads %p: \"%s\"\n", job->name, g2h(x0), (char *)g2h(x0)); continue; }
        if (nr == 6) { hv_vcpu_set_reg(vcpu, HV_REG_X0, guest_free(x0)); continue; }
        if (nr == 7)
        {
            uint64_t x2; hv_vcpu_get_reg(vcpu, HV_REG_X2, &x2);
            hv_vcpu_set_reg(vcpu, HV_REG_X0, guest_protect(x0, x1, x2 == 1 ? PROT_GUEST_RO : PROT_GUEST_RW));
            continue;
        }
        fprintf(stderr, "[%s] unknown syscall %llu\n", job->name, nr);
        break;
    }
    hv_vcpu_destroy(vcpu);
    return NULL;
}

static int run_pe(const char *path);

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    ns_per_tick = (double)tb.numer / tb.denom;

    hv_vm_config_t config = hv_vm_config_create();
    uint32_t max_ipa;
    CHECK(hv_vm_config_get_max_ipa_size(&max_ipa));
    hv_ipa_granule_t gran;
    if (hv_vm_config_set_ipa_granule(config, HV_IPA_GRANULE_4KB) == HV_SUCCESS)
        printf("stage-2 IPA granule: 4KB\n");
    else if (hv_vm_config_get_default_ipa_granule(&gran) == HV_SUCCESS)
        printf("stage-2 IPA granule: default (%s)\n", gran == HV_IPA_GRANULE_4KB ? "4KB" : "16KB");
    CHECK(hv_vm_config_set_ipa_size(config, max_ipa));   /* default is 36 bits */
    printf("IPA bits: %u\n", max_ipa);
    CHECK(hv_vm_create(config));
    setup_arenas();

    mem = mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mem == MAP_FAILED) { perror("mmap"); return 1; }
    CHECK(hv_vm_map(mem, IPA_BASE, MEM_SIZE, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC));

    ttbr0 = alloc_page();

    /* code: read-only, executable at EL0 and EL1 */
    size_t code_size = (guest_blob_end - guest_blob_start + PAGE - 1) & ~(PAGE - 1);
    if (!guest_alloc(CODE_VA, code_size, D_AP_RO)) { fprintf(stderr, "code alloc failed\n"); return 1; }
    memcpy(g2h(CODE_VA), guest_blob_start, guest_blob_end - guest_blob_start);

    if (!guest_alloc(STACK_VA, 0x20000, PROT_GUEST_RW) || !guest_alloc(SHARED_VA, CHUNK, PROT_GUEST_RW) ||
        !guest_alloc(TEB_VA, CHUNK, PROT_GUEST_RW) || !guest_alloc(KUSD_VA, CHUNK, PROT_GUEST_RO))
    { fprintf(stderr, "static alloc failed\n"); return 1; }
    *(uint64_t *)g2h(KUSD_VA) = 0x4b55534552534844ULL;  /* "DHSRESUK" */
    *(uint64_t *)g2h(TEB_VA)  = 0x2142455400000000ULL | 0xbeef; /* "TEB!" */

    if (argc > 2 && !strcmp(argv[1], "--coherence"))
    {
        coherence_file = argv[2];
        struct vcpu_job cj = { .name = "coh", .pc = code_off(guest_coherence), .sp = STACK_VA + 0x10000 };
        pthread_t cth;
        pthread_create(&cth, NULL, vcpu_thread, &cj);
        pthread_join(cth, NULL);
        return 0;
    }
    if (argc > 1) return run_pe(argv[1]);

    printf("\n== Test: Windows address layout, x18, syscalls ==\n");
    struct vcpu_job main_job = { .name = "main", .pc = code_off(guest_user_main),
                                 .sp = STACK_VA + 0x10000, .tso = 0, .verbose = 1 };
    pthread_t th;
    pthread_create(&th, NULL, vcpu_thread, &main_job);
    pthread_join(th, NULL);
    if (main_job.nmarks == 3)
    {
        double exit_ns = (main_job.marks[1] - main_job.marks[0]) * ns_per_tick / 65536;
        double fast_ns = (main_job.marks[2] - main_job.marks[1]) * ns_per_tick / (16.0 * 1024 * 1024);
        printf("syscall via VM exit to host : %8.1f ns\n", exit_ns);
        printf("syscall inside guest kernel : %8.1f ns\n", fast_ns);
    }

    printf("\n== Test: shared guest/host address space ==\n");
    struct vcpu_job mem_job = { .name = "mem", .pc = code_off(guest_mem_test),
                                .sp = STACK_VA + 0x10000, .tso = 0 };
    pthread_create(&th, NULL, vcpu_thread, &mem_job);
    pthread_join(th, NULL);
    if (mem_job.nmarks == 2)
        printf("alloc+touch+free 64K cycle   : %8.1f us\n",
               (mem_job.marks[1] - mem_job.marks[0]) * ns_per_tick / 2000 / 1000);

    printf("\n== Test: message-passing litmus (2 vCPUs) ==\n");
    uint64_t iterations = 20000000;
    for (int tso = 0; tso <= 1; tso++)
    {
        memset(g2h(SHARED_VA), 0, PAGE);
        struct vcpu_job w = { .name = "writer", .pc = code_off(guest_mp_writer), .x0 = SHARED_VA,
                              .x1 = iterations, .sp = STACK_VA + 0x10000, .tso = tso, .verbose = 1 };
        struct vcpu_job r = { .name = "reader", .pc = code_off(guest_mp_reader), .x0 = SHARED_VA,
                              .x1 = iterations, .sp = STACK_VA + 0x20000, .tso = tso, .verbose = 1 };
        pthread_t tw, tr;
        uint64_t t0 = mach_absolute_time();
        pthread_create(&tw, NULL, vcpu_thread, &w);
        pthread_create(&tr, NULL, vcpu_thread, &r);
        pthread_join(tw, NULL);
        pthread_join(tr, NULL);
        double ms = (mach_absolute_time() - t0) * ns_per_tick / 1e6;
        printf("TSO %-3s: %llu / %llu reader samples saw data < flag (%.2f%%), writer %llu stores, %.0f ms\n\n",
               tso ? "on" : "off", r.result, r.result2, 100.0 * r.result / (r.result2 ? r.result2 : 1),
               iterations, ms);
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * PE loader (host side). Maps PE32+ ARM64 images into the guest address
 * space, applies base relocations, resolves imports by name.
 */
struct pe_file_hdr { uint16_t Machine, NumberOfSections; uint32_t TimeDateStamp, PointerToSymbolTable, NumberOfSymbols;
                     uint16_t SizeOfOptionalHeader, Characteristics; };
struct pe_data_dir { uint32_t va, size; };
struct pe_opt_hdr  { uint16_t Magic; uint8_t MajorLinker, MinorLinker;
                     uint32_t SizeOfCode, SizeOfInitializedData, SizeOfUninitializedData, AddressOfEntryPoint, BaseOfCode;
                     uint64_t ImageBase; uint32_t SectionAlignment, FileAlignment;
                     uint16_t MajorOS, MinorOS, MajorImage, MinorImage, MajorSubsys, MinorSubsys;
                     uint32_t Win32Version, SizeOfImage, SizeOfHeaders, CheckSum;
                     uint16_t Subsystem, DllCharacteristics;
                     uint64_t StackReserve, StackCommit, HeapReserve, HeapCommit;
                     uint32_t LoaderFlags, NumberOfRvaAndSizes; struct pe_data_dir dir[16]; };
struct pe_nt_hdr   { uint32_t Signature; struct pe_file_hdr fh; struct pe_opt_hdr oh; };
struct pe_section  { char Name[8]; uint32_t VirtualSize, VirtualAddress, SizeOfRawData, PointerToRawData,
                     PointerToRelocations, PointerToLinenumbers; uint16_t NumberOfRelocations, NumberOfLinenumbers;
                     uint32_t Characteristics; };
struct pe_import   { uint32_t OriginalFirstThunk, TimeDateStamp, ForwarderChain, Name, FirstThunk; };
struct pe_export   { uint32_t Characteristics, TimeDateStamp; uint16_t Major, Minor;
                     uint32_t Name, Base, NumberOfFunctions, NumberOfNames, AddressOfFunctions, AddressOfNames,
                          AddressOfNameOrdinals; };

#define PE_DIR_EXPORT 0
#define PE_DIR_IMPORT 1
#define PE_DIR_RELOC  5
#define SCN_EXEC  0x20000000u
#define SCN_WRITE 0x80000000u

struct module { char name[64]; uint64_t base; struct pe_nt_hdr *nt; };
static struct module modules[16];
static int nmodules;
static char pe_dir[1024];

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *size = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(*size);
    if (fread(buf, 1, *size, f) != *size) { free(buf); buf = NULL; }
    fclose(f);
    return buf;
}

static uint64_t find_export(struct module *mod, const char *name)
{
    struct pe_data_dir d = mod->nt->oh.dir[PE_DIR_EXPORT];
    if (!d.size) return 0;
    uint8_t *img = g2h(mod->base);
    struct pe_export *e = (struct pe_export *)(img + d.va);
    uint32_t *names = (uint32_t *)(img + e->AddressOfNames);
    uint16_t *ords = (uint16_t *)(img + e->AddressOfNameOrdinals);
    uint32_t *funcs = (uint32_t *)(img + e->AddressOfFunctions);
    for (uint32_t i = 0; i < e->NumberOfNames; i++)
        if (!strcmp((char *)img + names[i], name)) return mod->base + funcs[ords[i]];
    return 0;
}

static struct module *load_pe(const char *name)
{
    for (int i = 0; i < nmodules; i++) if (!strcasecmp(modules[i].name, name)) return &modules[i];
    if (nmodules == 16) return NULL;

    char path[1200];
    snprintf(path, sizeof(path), "%s/%s", pe_dir, name);
    size_t fsize;
    uint8_t *buf = read_file(path, &fsize);
    if (!buf) { fprintf(stderr, "loader: cannot read %s\n", path); return NULL; }

    uint32_t lfanew = *(uint32_t *)(buf + 0x3c);
    struct pe_nt_hdr *nt = (struct pe_nt_hdr *)(buf + lfanew);
    if (buf[0] != 'M' || buf[1] != 'Z' || nt->Signature != 0x4550 || nt->fh.Machine != 0xaa64 || nt->oh.Magic != 0x20b)
    { fprintf(stderr, "loader: %s is not a PE32+ ARM64 image\n", name); free(buf); return NULL; }

    uint64_t pref = nt->oh.ImageBase, size = nt->oh.SizeOfImage;
    uint64_t base = guest_alloc(pref, size, PROT_GUEST_RW);
    if (!base)
    {
        if (nt->fh.Characteristics & 1) { fprintf(stderr, "loader: %s: relocations stripped\n", name); free(buf); return NULL; }
        base = guest_alloc(0, size, PROT_GUEST_RW);
        if (!base) { free(buf); return NULL; }
    }
    uint8_t *img = g2h(base);

    memcpy(img, buf, nt->oh.SizeOfHeaders);
    struct pe_section *sec = (struct pe_section *)((uint8_t *)&nt->oh + nt->fh.SizeOfOptionalHeader);
    for (int i = 0; i < nt->fh.NumberOfSections; i++)
    {
        uint32_t n = sec[i].SizeOfRawData;
        if (sec[i].VirtualSize && n > sec[i].VirtualSize) n = sec[i].VirtualSize;
        memcpy(img + sec[i].VirtualAddress, buf + sec[i].PointerToRawData, n);
    }
    free(buf);
    nt = (struct pe_nt_hdr *)(img + lfanew);
    sec = (struct pe_section *)((uint8_t *)&nt->oh + nt->fh.SizeOfOptionalHeader);

    struct module *mod = &modules[nmodules++];
    snprintf(mod->name, sizeof(mod->name), "%s", name);
    mod->base = base;
    mod->nt = nt;

    /* base relocations */
    int64_t delta = base - pref;
    unsigned nrelocs = 0;
    struct pe_data_dir rd = nt->oh.dir[PE_DIR_RELOC];
    if (delta && rd.size)
    {
        for (uint8_t *p = img + rd.va, *end = p + rd.size; p < end; )
        {
            uint32_t page = ((uint32_t *)p)[0], bsize = ((uint32_t *)p)[1];
            uint16_t *ent = (uint16_t *)(p + 8);
            for (unsigned i = 0; i < (bsize - 8) / 2; i++)
            {
                unsigned type = ent[i] >> 12, off = ent[i] & 0xfff;
                if (type == 10) { *(uint64_t *)(img + page + off) += delta; nrelocs++; }   /* DIR64 */
                else if (type != 0) fprintf(stderr, "loader: %s: unsupported reloc type %u\n", name, type);
            }
            p += bsize;
        }
    }
    printf("[loader] %-10s preferred 0x%09llx -> mapped 0x%09llx host %p (%u relocs)\n",
           name, pref, base, img, nrelocs);

    /* imports */
    struct pe_data_dir id = nt->oh.dir[PE_DIR_IMPORT];
    for (struct pe_import *imp = id.size ? (struct pe_import *)(img + id.va) : NULL; imp && imp->Name; imp++)
    {
        const char *dll = (char *)img + imp->Name;
        struct module *dep = load_pe(dll);
        if (!dep) return NULL;
        uint64_t *lookup = (uint64_t *)(img + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        uint64_t *iat = (uint64_t *)(img + imp->FirstThunk);
        for (int i = 0; lookup[i]; i++)
        {
            if (lookup[i] >> 63) { fprintf(stderr, "loader: import by ordinal not supported\n"); return NULL; }
            const char *fn = (char *)img + (lookup[i] & 0x7fffffff) + 2;
            uint64_t addr = find_export(dep, fn);
            if (!addr) { fprintf(stderr, "loader: %s: %s!%s not found\n", name, dll, fn); return NULL; }
            iat[i] = addr;
            printf("[loader]   %s: %s!%s -> 0x%llx\n", name, dll, fn, addr);
        }
    }

    /* page protections, 4K granular like Windows */
    guest_protect(base, nt->oh.SizeOfHeaders, PROT_GUEST_RO);
    for (int i = 0; i < nt->fh.NumberOfSections; i++)
    {
        uint64_t flags = (sec[i].Characteristics & SCN_EXEC)  ? (D_AP_RO | D_PXN) :
                         (sec[i].Characteristics & SCN_WRITE) ? PROT_GUEST_RW : PROT_GUEST_RO;
        uint64_t vsize = (sec[i].VirtualSize + PAGE - 1) & ~(PAGE - 1);
        guest_protect(base + sec[i].VirtualAddress, vsize, flags);
    }
    return mod;
}

static int run_pe(const char *path)
{
    const char *slash = strrchr(path, '/');
    snprintf(pe_dir, sizeof(pe_dir), "%.*s", slash ? (int)(slash - path) : 1, slash ? path : ".");
    struct module *exe = load_pe(slash ? slash + 1 : path);
    if (!exe) return 1;

    struct vcpu_job job = { .name = "pe", .pc = exe->base + exe->nt->oh.AddressOfEntryPoint,
                            .sp = STACK_VA + 0x10000 };
    printf("[loader] entry point 0x%llx, x18 = TEB 0x%llx\n\n", job.pc, TEB_VA);
    pthread_t th;
    pthread_create(&th, NULL, vcpu_thread, &job);
    pthread_join(th, NULL);
    printf("\n[loader] %s exited with code %lld\n", exe->name, (long long)job.result);
    return (int)job.result;
}
