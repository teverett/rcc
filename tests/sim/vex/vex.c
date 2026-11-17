/*
 * Vendored, unmodified, from the vex project's own interpreter/vex.c (same author/project as
 * this one) - the real simulator tests/run_tests.sh's own check_vex() and examples/vex/
 * Makefile's own `test` target run every compiled vex program under. Vendored rather than
 * built against that sibling repo directly so CI needs no extra clone/checkout step and stays
 * pinned to one known-matching interpreter version instead of whatever that repo's own
 * `interpreter/vex.c` happens to be on a given day.
 *
 * vex.c - reference interpreter for the Vex virtual machine (spec v1.1).
 * See docs/SPEC.md for the machine definition.
 *
 * Usage: vex [-t] [-d] [-s steps] program.rom
 *   -t        trace every instruction to stderr
 *   -d        dump the final registers and return stack to stderr
 *   -s N      stop with status 3 after N instructions (0 = unlimited)
 *
 * The ROM is loaded at 0x0100 and execution starts there.
 * Exit status: 0 normal halt, N from "DEO 0x00" (value & 0xFF),
 *              1 usage or I/O error, 2 machine fault, 3 step limit.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#define MEM_SIZE  UINT64_C(0x100000000)
#define LOAD_ADDR 0x0100
#define RS_SIZE   512
#define NREGS     8

enum {
    OP_RET, OP_LDI, OP_MOV, OP_LD, OP_ST, OP_DEI, OP_DEO, OP_JMP, OP_JCN, OP_CAL,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_AND, OP_ORA, OP_XOR,
    OP_CEQ, OP_CLT, OP_NOP,
    OP_COUNT
};

/* total instruction length in bytes, including the opcode byte itself */
static const uint8_t OPLEN[OP_COUNT] = {
    1, 6, 3, 3, 3, 3, 3, 2, 3, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1
};

static const char *const OPNAME[OP_COUNT] = {
    "RET", "LDI", "MOV", "LD", "ST", "DEI", "DEO", "JMP", "JCN", "CAL",
    "ADD", "SUB", "MUL", "DIV", "AND", "ORA", "XOR", "CEQ", "CLT", "NOP"
};

typedef struct {
    uint8_t *b;
    int sp;          /* number of bytes currently on the stack */
    int cap;
    const char *name;
} Stack;

typedef struct {
    uint8_t  mem[MEM_SIZE];
    uint8_t  rbuf[RS_SIZE];
    Stack    rs;
    uint32_t regs[NREGS];
    uint32_t pc;     /* address of the next instruction */
    uint32_t ipc;    /* address of the instruction being executed */
    int halted;
    int exit_code;
    long steps;
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

/* ---- the return stack: big-endian, byte granular ---- */

static uint32_t spop(VM *m, Stack *s, int n)
{
    uint32_t v = 0;
    if (s->sp < n)
        fault(m, "%s stack underflow", s->name);
    for (int i = 0; i < n; i++)
        v = (v << 8) | s->b[s->sp - n + i];
    s->sp -= n;
    return v;
}

static void spush(VM *m, Stack *s, uint32_t v, int n)
{
    if (s->sp + n > s->cap)
        fault(m, "%s stack overflow", s->name);
    for (int i = n - 1; i >= 0; i--) {
        s->b[s->sp + i] = (uint8_t)(v & 0xFF);
        v >>= 8;
    }
    s->sp += n;
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

/* A register operand (docs/SPEC.md section 2): bits 2-0 select R0-R7,
 * bits 7-3 are reserved and must be 0. */
static uint8_t fetch_reg(VM *m)
{
    uint8_t b = fetch_byte(m);
    if (b & 0xF8)
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
    case OP_RET:
        if (m->rs.sp == 0) {
            m->halted = 1;
            m->exit_code = 0;
        } else {
            m->pc = spop(m, &m->rs, 4);
        }
        break;
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
        spush(m, &m->rs, m->pc, 4);
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

    default:
        fault(m, "unknown opcode 0x%02x", op);
    }
}

/* ---- diagnostics ---- */

static void print_stack(FILE *f, const Stack *s)
{
    for (int i = 0; i < s->sp; i++)
        fprintf(f, "%02x", s->b[i]);
}

static void trace_line(const VM *m)
{
    uint32_t pc = m->pc;
    uint8_t op = m->mem[pc];
    char text[64];

    if (op < OP_COUNT) {
        int len = snprintf(text, sizeof text, "%s", OPNAME[op]);
        uint8_t instrlen = OPLEN[op];
        if (op == OP_LDI) {
            uint8_t rd = m->mem[pc + 1] & 7;
            uint32_t imm = ((uint32_t)m->mem[pc + 2] << 24) | ((uint32_t)m->mem[pc + 3] << 16)
                         | ((uint32_t)m->mem[pc + 4] << 8)  | m->mem[pc + 5];
            len += snprintf(text + len, sizeof text - (size_t)len, " R%u,0x%08x", rd, imm);
        } else {
            for (int i = 1; i < instrlen; i++)
                len += snprintf(text + len, sizeof text - (size_t)len, " R%u", m->mem[pc + i] & 7);
        }
    } else {
        snprintf(text, sizeof text, "??? 0x%02x", op);
    }
    fprintf(stderr, "%08x  %-24s regs=", pc, text);
    for (int i = 0; i < NREGS; i++)
        fprintf(stderr, "%08x ", m->regs[i]);
    fprintf(stderr, "rs=");
    print_stack(stderr, &m->rs);
    fputc('\n', stderr);
}

static void dump(const VM *m)
{
    fprintf(stderr, "regs:");
    for (int i = 0; i < NREGS; i++)
        fprintf(stderr, " %08x", m->regs[i]);
    fputc('\n', stderr);
    fprintf(stderr, "rs: ");
    print_stack(stderr, &m->rs);
    fputc('\n', stderr);
}

static int run(VM *m, long limit, int trace)
{
    if (setjmp(m->trap)) {
        fflush(stdout);
        fprintf(stderr, "vex: fault at 0x%08x: %s\n", m->ipc, m->err);
        return 2;
    }
    while (!m->halted) {
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
    int trace = 0, do_dump = 0;
    long limit = 0;
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
        } else if (argv[i][0] == '-' && argv[i][1]) {
            path = NULL;
            break;
        } else {
            path = argv[i];
        }
    }
    if (!path) {
        fprintf(stderr, "usage: %s [-t] [-d] [-s steps] program.rom\n", argv[0]);
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

    vm.rs = (Stack){ vm.rbuf, 0, RS_SIZE, "return" };
    vm.pc = LOAD_ADDR;

    int status = run(&vm, limit, trace);
    if (do_dump)
        dump(&vm);
    return status;
}
