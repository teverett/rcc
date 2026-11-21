/*
 * Vendored, unmodified, from the vex project's own interpreter/vex.c (same author/project as
 * this one) - the real simulator tests/run_tests.sh's own check_vex() and examples/vex/
 * Makefile's own `test` target run every compiled vex program under. Vendored rather than
 * built against that sibling repo directly so CI needs no extra clone/checkout step and stays
 * pinned to one known-matching interpreter version instead of whatever that repo's own
 * `interpreter/vex.c` happens to be on a given day.
 *
 * vex.c - reference interpreter for the Vex virtual machine (spec v1.3).
 * See docs/SPEC.md for the machine definition.
 *
 * Usage: vex [-t] [-d] [-s steps] [-i] [-I period] [-n count] program.rom
 *   -t        trace every instruction to stderr
 *   -d        dump the final registers (including SP, R8) to stderr
 *   -s N      stop with status 3 after N instructions (0 = unlimited)
 *   -i        raise the maskable interrupt every IRQ_PERIOD_MS milliseconds
 *             of wall-clock time (off by default)
 *   -I N      raise the maskable interrupt every N instructions instead of
 *             on a wall-clock period - a test hook for exercising interrupt
 *             semantics deterministically (see tests/run_tests.py), not a
 *             real timer source; -i is what a real program should expect
 *   -n N      raise NMI once, immediately before instruction N executes
 *             (a test hook standing in for a real board's NMI pin - see
 *             docs/SPEC.md section 3, "Interrupts")
 *
 * HLT (docs/SPEC.md section 3) parks the machine - the interpreter actually
 * blocks the host thread, not a busy-wait - until an interrupt is pending,
 * then resumes at the instruction after HLT (or, if unmasked, the handler
 * vector). -n/-I are step-count test hooks and never advance while parked
 * (step count is frozen), so only -i's real wall-clock timer can wake a
 * HLT'd machine.
 *
 * The ROM is loaded at 0x0100 and execution starts there.
 * Exit status: 0 normal halt, N from "DEO 0x00" (value & 0xFF),
 *              1 usage or I/O error, 2 machine fault, 3 step limit.
 */
#if !defined(_WIN32)
/* clock_gettime(CLOCK_MONOTONIC, ...) is POSIX, not C99 - -std=c99
 * otherwise hides it under -pedantic. Must come before any system header
 * pulls in <time.h> or <features.h> and fixes the visibility it grants. */
#define _POSIX_C_SOURCE 200112L
#endif

#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

/* A monotonic wall-clock reading in milliseconds - unlike clock(), this
 * keeps advancing while the process is asleep (host_sleep_while_waiting()
 * below relies on that), which is the whole point of a real timer. */
#if defined(_WIN32)
static double now_ms(void) { return (double)GetTickCount64(); }
#else
static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}
#endif

#define MEM_SIZE  UINT64_C(0x100000000)
#define LOAD_ADDR 0x0100
#define NREGS     9
#define SP        8   /* R8: the stack pointer (docs/SPEC.md section 1) */

/* R8/SP starts here and grows downward through ordinary memory, exactly
 * like rcc's own R5/SP convention (VexCodegen.h's kVexStackTop,
 * 0x01000000) - a different, comfortably separated region so the two
 * never collide under normal use. mem is calloc'd (see main()), so the
 * word at SP_RESET is already zero without any extra setup - see
 * OP_RET's own comment for why that matters. */
#define SP_RESET 0x00800000

/* Interrupt vectors: fixed memory words below LOAD_ADDR (free memory, docs/SPEC.md
 * section 1), read once per interrupt delivery - see docs/SPEC.md section 3,
 * "Interrupts". A program that never enables interrupts never touches them. */
#define NMI_VECTOR 0x0000
#define IRQ_VECTOR 0x0004

/* Period of the maskable-interrupt timer (see -i). Fixed, like irq_period
 * used to be - see docs/SPEC.md section 3, "Interrupts". */
#define IRQ_PERIOD_MS 100

/* How often (in instructions) the timer's wall-clock deadline is actually
 * polled - must be a power of 2 (see the "& (IRQ_POLL_STRIDE - 1)" check
 * in run()). */
#define IRQ_POLL_STRIDE 1024

enum {
    OP_RET, OP_LDI, OP_MOV, OP_LD, OP_ST, OP_DEI, OP_DEO, OP_JMP, OP_JCN, OP_CAL,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_AND, OP_ORA, OP_XOR,
    OP_CEQ, OP_CLT, OP_NOP, OP_PSH, OP_POP, OP_IRET, OP_SEI, OP_CLI, OP_HLT,
    OP_COUNT
};

/* total instruction length in bytes, including the opcode byte itself */
static const uint8_t OPLEN[OP_COUNT] = {
    1, 6, 3, 3, 3, 3, 3, 2, 3, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1, 2, 2, 1, 1, 1, 1
};

static const char *const OPNAME[OP_COUNT] = {
    "RET", "LDI", "MOV", "LD", "ST", "DEI", "DEO", "JMP", "JCN", "CAL",
    "ADD", "SUB", "MUL", "DIV", "AND", "ORA", "XOR", "CEQ", "CLT", "NOP",
    "PSH", "POP", "IRET", "SEI", "CLI", "HLT"
};

typedef struct {
    uint8_t *mem;    /* MEM_SIZE bytes, heap-allocated - see main() */
    uint32_t regs[NREGS];   /* R0-R7 general-purpose, R8 (SP) the stack
                               pointer - an ordinary address into mem,
                               grows downward (see SP_RESET above) */
    uint32_t pc;     /* address of the next instruction */
    uint32_t ipc;    /* address of the instruction being executed */
    int halted;
    int waiting;     /* parked by HLT; resumes once any interrupt is pending - see run() */
    int exit_code;
    long steps;
    int ie;          /* maskable-interrupt enable, cleared by CLI/entry */
    int ie_pending;  /* SEI's one-instruction-delayed commit into ie - see run() */
    int nmi_pending;
    int irq_pending;
    int irq_enabled;    /* periodic timer IRQ on/off (see -i) */
    double next_irq_at_ms; /* now_ms() value at which the timer next fires */
    long irq_test_period; /* test hook: raise IRQ every N instructions; 0 = off (see -I) */
    long nmi_at;     /* raise NMI once, right before this step count; -1 = off (see -n) */
    jmp_buf trap;
    char err[160];
} VM;

static VM vm;

#if defined(__GNUC__)
__attribute__((noreturn, format(printf, 2, 3)))
#endif
static void fault(VM *m, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m->err, sizeof m->err, fmt, ap);
    va_end(ap);
    longjmp(m->trap, 1);
}

/* ---- memory ---- */

static uint32_t mrd(VM *m, uint32_t addr, int n)
{
    uint32_t v = 0;
    if ((uint64_t)addr + (unsigned)n > MEM_SIZE)
        fault(m, "memory read out of range at 0x%x", addr);
    for (int i = 0; i < n; i++)
        v = (v << 8) | m->mem[addr + i];
    return v;
}

static void mwr(VM *m, uint32_t addr, uint32_t v, int n)
{
    if ((uint64_t)addr + (unsigned)n > MEM_SIZE)
        fault(m, "memory write out of range at 0x%x", addr);
    for (int i = n - 1; i >= 0; i--) {
        m->mem[addr + i] = (uint8_t)(v & 0xFF);
        v >>= 8;
    }
}

/* ---- the stack: ordinary memory, addressed through R8/SP (docs/SPEC.md
 * section 1) - CAL/PSH/interrupt entry pre-decrement SP and write there;
 * RET/POP/IRET read at SP and post-increment, mirroring rcc's own R5/SP
 * convention (VexCodegen.h) exactly. There is no dedicated overflow/
 * underflow check: running SP below 0 or past other memory eventually
 * trips mrd()/mwr()'s own out-of-range fault, same as any other runaway
 * pointer - docs/SPEC.md section 7 discusses the tradeoff this accepts
 * in exchange for SP being an ordinary register, directly readable and
 * writable like R0-R7, rather than fixed, opaque hardware state. */
static void sp_push(VM *m, uint32_t v)
{
    m->regs[SP] -= 4;
    mwr(m, m->regs[SP], v, 4);
}

static uint32_t sp_pop(VM *m)
{
    uint32_t v = mrd(m, m->regs[SP], 4);
    m->regs[SP] += 4;
    return v;
}

/* ---- devices (see docs/SPEC.md, "Reference devices") ---- */

static uint8_t dev_in(uint8_t port)
{
    switch (port) {
    case 0x10: {                    /* console: next input byte, 0 at EOF */
        int c = getc(stdin);
        return c == EOF ? 0 : (uint8_t)c;
    }
    case 0x11: {                    /* console: 1 if no more input */
        int c = getc(stdin);
        if (c == EOF)
            return 1;
        ungetc(c, stdin);
        return 0;
    }
    default:
        return 0;
    }
}

static void dev_out(VM *m, uint8_t port, uint8_t v)
{
    switch (port) {
    case 0x00:                      /* system: halt with exit code */
        m->halted = 1;
        m->exit_code = v;
        break;
    case 0x18:
        putc(v, stdout);
        break;
    case 0x19:
        putc(v, stderr);
        break;
    default:
        break;
    }
}

/* ---- execution ---- */

/* Fetches one byte at pc and advances it - used for the opcode byte and
 * for every register operand byte. */
static uint8_t fetch_byte(VM *m)
{
    uint8_t b = (uint8_t)mrd(m, m->pc, 1);
    m->pc++;
    return b;
}

/* A register operand (docs/SPEC.md section 2): selects R0-R8 (R8 is SP);
 * any other value is reserved. */
static uint8_t fetch_reg(VM *m)
{
    uint8_t b = fetch_byte(m);
    if (b >= NREGS)
        fault(m, "reserved bits set in register operand 0x%02x", b);
    return b;
}

static void step(VM *m)
{
    m->ipc = m->pc;
    uint8_t op = fetch_byte(m);
    if (op >= OP_COUNT)
        fault(m, "unknown opcode 0x%02x", op);

    uint8_t rd, ra, rb;

    switch (op) {
    case OP_RET: {
        /* Peeks rather than unconditionally popping: a fully unwound call
         * chain finds zero at the word it would pop - mem is calloc'd and
         * nothing legitimate ever stores there (every CAL/PSH/interrupt-
         * entry writes strictly below SP before advancing it) - and RET
         * repurposes that as "halt normally" (a real return address of 0
         * would jump into the NMI vector anyway, never a valid
         * destination), leaving SP exactly where it was rather than
         * advancing past a slot nothing was ever really popped from. */
        uint32_t target = mrd(m, m->regs[SP], 4);
        if (target == 0) {
            m->halted = 1;
            m->exit_code = 0;
        } else {
            m->regs[SP] += 4;
            m->pc = target;
        }
        break;
    }
    case OP_LDI:
        rd = fetch_reg(m);
        m->regs[rd] = mrd(m, m->pc, 4);
        m->pc += 4;
        break;
    case OP_MOV:
        rd = fetch_reg(m); ra = fetch_reg(m);
        m->regs[rd] = m->regs[ra];
        break;
    case OP_LD:
        rd = fetch_reg(m); ra = fetch_reg(m);
        m->regs[rd] = mrd(m, m->regs[ra], 4);
        break;
    case OP_ST:
        ra = fetch_reg(m); rb = fetch_reg(m);
        mwr(m, m->regs[ra], m->regs[rb], 4);
        break;
    case OP_DEI:
        rd = fetch_reg(m); ra = fetch_reg(m);
        m->regs[rd] = dev_in((uint8_t)m->regs[ra]);
        break;
    case OP_DEO:
        ra = fetch_reg(m); rb = fetch_reg(m);
        dev_out(m, (uint8_t)m->regs[ra], (uint8_t)m->regs[rb]);
        break;
    case OP_JMP:
        ra = fetch_reg(m);
        m->pc = m->regs[ra];
        break;
    case OP_JCN:
        ra = fetch_reg(m); rb = fetch_reg(m);
        if (m->regs[rb])
            m->pc = m->regs[ra];
        break;
    case OP_CAL:
        ra = fetch_reg(m);
        sp_push(m, m->pc);
        m->pc = m->regs[ra];
        break;

    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
    case OP_AND: case OP_ORA: case OP_XOR: {
        rd = fetch_reg(m); ra = fetch_reg(m); rb = fetch_reg(m);
        uint32_t a = m->regs[ra], b = m->regs[rb], res;
        switch (op) {
        case OP_ADD: res = a + b; break;
        case OP_SUB: res = a - b; break;
        case OP_MUL: res = a * b; break;
        case OP_DIV: res = b ? a / b : 0; break;
        case OP_AND: res = a & b; break;
        case OP_ORA: res = a | b; break;
        default:     res = a ^ b; break;
        }
        m->regs[rd] = res;
        break;
    }

    case OP_CEQ: case OP_CLT:
        rd = fetch_reg(m); ra = fetch_reg(m); rb = fetch_reg(m);
        m->regs[rd] = (op == OP_CEQ) ? (m->regs[ra] == m->regs[rb])
                                      : (m->regs[ra] <  m->regs[rb]);
        break;

    case OP_NOP:
        break;

    case OP_PSH:
        ra = fetch_reg(m);
        sp_push(m, m->regs[ra]);
        break;
    case OP_POP:
        rd = fetch_reg(m);
        m->regs[rd] = sp_pop(m);
        break;
    case OP_IRET:
        m->pc = sp_pop(m);
        break;
    case OP_SEI:
        /* Takes effect after the *next* instruction, not immediately -
         * see run()'s commit-or-discard logic below. Lets a SEI;IRET
         * pair stay atomic even if an interrupt is already pending the
         * moment SEI runs (docs/SPEC.md section 3, "SEI's effect is
         * delayed by one instruction"). */
        m->ie_pending = 1;
        break;
    case OP_CLI:
        m->ie = 0;
        break;
    case OP_HLT:
        /* Parks the machine - run()'s loop stops calling step() and
         * blocks the host instead of busy-spinning, until some interrupt
         * is pending (docs/SPEC.md section 3, "HLT"). Woken on a merely
         * *pending* request, masked or not - see run() - so the
         * instruction right after HLT runs either way; whether that turns
         * into a handler call depends on ie, same as any other boundary. */
        m->waiting = 1;
        break;
    default:
        fault(m, "unknown opcode 0x%02x", op);
    }
}

/* Delivers a pending interrupt at an instruction boundary: pushes the
 * return address (the instruction about to execute), disables further
 * maskable interrupts, and jumps to the handler at `vector` - same shape
 * as CAL, except the target comes from memory instead of a register.
 * docs/SPEC.md section 3, "Interrupts". */
static void deliver_interrupt(VM *m, uint32_t vector)
{
    m->ipc = m->pc;
    sp_push(m, m->pc);
    m->ie = 0;
    m->pc = mrd(m, vector, 4);
}

/* ---- diagnostics ---- */

static void trace_line(const VM *m)
{
    uint32_t pc = m->pc;
    uint8_t op = m->mem[pc];
    char text[64];

    if (op < OP_COUNT) {
        int len = snprintf(text, sizeof text, "%s", OPNAME[op]);
        uint8_t instrlen = OPLEN[op];
        if (op == OP_LDI) {
            uint8_t rd = m->mem[pc + 1];
            uint32_t imm = ((uint32_t)m->mem[pc + 2] << 24) | ((uint32_t)m->mem[pc + 3] << 16)
                         | ((uint32_t)m->mem[pc + 4] << 8)  | m->mem[pc + 5];
            len += snprintf(text + len, sizeof text - (size_t)len, " R%u,0x%08x", rd, imm);
        } else {
            for (int i = 1; i < instrlen; i++)
                len += snprintf(text + len, sizeof text - (size_t)len, " R%u", m->mem[pc + i]);
        }
    } else {
        snprintf(text, sizeof text, "??? 0x%02x", op);
    }
    fprintf(stderr, "%08x  %-24s regs=", pc, text);
    for (int i = 0; i < NREGS; i++)
        fprintf(stderr, "%08x ", m->regs[i]);
    fputc('\n', stderr);
}

/* SP (R8) is just a register (docs/SPEC.md section 1/7), already covered
 * by the regs loop above - no separate line for it. */
static void dump(const VM *m)
{
    fprintf(stderr, "regs:");
    for (int i = 0; i < NREGS; i++)
        fprintf(stderr, " %08x", m->regs[i]);
    fputc('\n', stderr);
}

/* Blocks the host for a short while when HLT has parked the machine -
 * never the whole remaining wait in one call, so the caller can re-check
 * for a newly pending interrupt between sleeps. Sleeps until the real
 * timer's next deadline when one is armed (clamped to a small ceiling so
 * a very distant deadline still re-checks promptly); otherwise nothing
 * will ever wake the machine, so it just idles in fixed small steps
 * rather than hanging in one uninterruptible call. */
static void host_sleep_while_waiting(const VM *m)
{
    long ms = 20;
    if (m->irq_enabled) {
        double want_ms = m->next_irq_at_ms - now_ms();
        if (want_ms < 1.0)
            return;
        ms = want_ms < 20.0 ? (long)want_ms : 20;
    }
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

static int run(VM *m, long limit, int trace)
{
    if (setjmp(m->trap)) {
        fflush(stdout);
        fprintf(stderr, "vex: fault at 0x%08x: %s\n", m->ipc, m->err);
        return 2;
    }
    while (!m->halted) {
        if (m->nmi_at >= 0 && m->steps == m->nmi_at) {
            m->nmi_at = -1;        /* one-shot test hook, see -n */
            m->nmi_pending = 1;
        }
        /* now_ms() is cheap but not free, and calling it on every single
         * instruction would make it dominate interpretation cost for a
         * timer this coarse - sample it every IRQ_POLL_STRIDE steps instead.
         * That still resolves IRQ_PERIOD_MS to well under a millisecond in
         * practice, since a real program executes far more than that many
         * instructions per millisecond. While HLT has parked the machine,
         * m->steps is frozen - possibly on a value this stride never
         * revisits - so poll on every host-sleep wakeup instead; those are
         * already paced in host_sleep_while_waiting(), not per-instruction. */
        if (m->irq_enabled && (m->waiting || (m->steps & (IRQ_POLL_STRIDE - 1)) == 0)
            && now_ms() >= m->next_irq_at_ms) {
            m->irq_pending = 1;    /* latched: stays pending if ie is off */
            /* advance by one period rather than snapping to "now + period",
             * so a late delivery doesn't push later ones out and the
             * long-run average stays IRQ_PERIOD_MS even under jitter */
            m->next_irq_at_ms += IRQ_PERIOD_MS;
        }
        if (m->irq_test_period > 0 && m->steps > 0 && m->steps % m->irq_test_period == 0)
            m->irq_pending = 1;    /* -I: test hook, see top-of-file usage comment */

        if (m->waiting && (m->nmi_pending || m->irq_pending))
            m->waiting = 0;   /* HLT wakes on a merely pending request, masked or not */
        if (m->waiting) {
            host_sleep_while_waiting(m);
            continue;
        }

        if (m->nmi_pending) {
            m->nmi_pending = 0;
            m->ie_pending = 0;   /* this boundary is being preempted - discard, don't commit */
            deliver_interrupt(m, NMI_VECTOR);
        } else if (m->ie && m->irq_pending) {
            m->irq_pending = 0;
            m->ie_pending = 0;
            deliver_interrupt(m, IRQ_VECTOR);
        } else if (m->ie_pending) {
            /* No interrupt preempted the instruction SEI scheduled this
             * for - safe to commit now, one full instruction after SEI
             * itself ran. */
            m->ie = 1;
            m->ie_pending = 0;
        }

        if (limit && m->steps >= limit) {
            fflush(stdout);
            fprintf(stderr, "vex: step limit (%ld) reached at 0x%08x\n", limit, m->pc);
            return 3;
        }
        if (trace)
            trace_line(m);
        step(m);
        m->steps++;
    }
    fflush(stdout);
    return m->exit_code;
}

int main(int argc, char **argv)
{
    int trace = 0, do_dump = 0, irq_enabled = 0;
    long limit = 0, irq_test_period = 0, nmi_at = -1;
    const char *path = NULL;

#if defined(_WIN32)
    /* DEI/DEO move exact bytes; the CRT's default text mode would
     * silently rewrite '\n' to "\r\n" on the way out (and, for other
     * byte values, could misread input) if left alone. */
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-t")) {
            trace = 1;
        } else if (!strcmp(argv[i], "-d")) {
            do_dump = 1;
        } else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            limit = strtol(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "-i")) {
            irq_enabled = 1;
        } else if (!strcmp(argv[i], "-I") && i + 1 < argc) {
            irq_test_period = strtol(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "-n") && i + 1 < argc) {
            nmi_at = strtol(argv[++i], NULL, 0);
        } else if (argv[i][0] == '-' && argv[i][1]) {
            path = NULL;
            break;
        } else {
            path = argv[i];
        }
    }
    if (!path) {
        fprintf(stderr, "usage: %s [-t] [-d] [-s steps] [-i] [-I period] [-n count] program.rom\n", argv[0]);
        return 1;
    }

    /* A static/global MEM_SIZE array would put this 4GiB of mostly-unused
     * address space in the binary's own BSS, where some toolchains/loaders
     * (confirmed: GitHub Actions' macOS and Windows runners, not Linux or
     * local macOS) fail to zero-fill or commit it, crashing before main()
     * ever runs a single instruction. A heap allocation goes through the
     * OS's own virtual-memory calls instead, same as every other large
     * allocation a C program makes. */
    vm.mem = calloc(1, MEM_SIZE);
    if (!vm.mem) {
        fprintf(stderr, "vex: cannot allocate %llu bytes of memory\n", (unsigned long long)MEM_SIZE);
        return 1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return 1;
    }
    size_t cap = MEM_SIZE - LOAD_ADDR;
    size_t got = fread(vm.mem + LOAD_ADDR, 1, cap, f);
    int overflowed = got == cap && fgetc(f) != EOF;
    fclose(f);
    if (overflowed) {
        fprintf(stderr, "vex: %s: program too large (max %llu bytes)\n", path, (unsigned long long)cap);
        return 1;
    }

    vm.regs[SP] = SP_RESET;
    vm.pc = LOAD_ADDR;
    vm.irq_enabled = irq_enabled;
    vm.irq_test_period = irq_test_period;
    vm.nmi_at = nmi_at;
    if (irq_enabled)
        vm.next_irq_at_ms = now_ms() + IRQ_PERIOD_MS;

    int status = run(&vm, limit, trace);
    if (do_dump)
        dump(&vm);
    return status;
}
