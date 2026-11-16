/*
 * Vendored, unmodified, from the vex project's own interpreter/vex.c (same author/project as
 * this one) - the real simulator tests/run_tests.sh's own check_vex() and examples/vex/test.py
 * run every compiled vex program under. Vendored rather than built against that sibling repo
 * directly so CI needs no extra clone/checkout step and stays pinned to one known-matching
 * interpreter version instead of whatever that repo's own `interpreter/vex.c` happens to be on
 * a given day.
 *
 * vex.c - reference interpreter for the Vex virtual machine (spec v0.4).
 * See docs/SPEC.md for the machine definition.
 *
 * Usage: vex [-t] [-d] [-s steps] program.rom
 *   -t        trace every instruction to stderr
 *   -d        dump the final data and return stacks to stderr
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
#define DS_SIZE   1024
#define RS_SIZE   512

/* 18 (0x12) was GTH ("a > b" is "b < a" with no opcode of its own - see
 * docs/SPEC.md section 7), now unassigned; PUSH..STS keep their numbers
 * rather than shifting down to fill the hole. */
enum {
    OP_RET, OP_LIT, OP_LD, OP_ST, OP_DEI, OP_DEO, OP_JMP, OP_JCN, OP_CAL,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_AND, OP_ORA, OP_XOR,
    OP_EQU, OP_LTH,
    OP_PUSH = 19, OP_POP, OP_LDS, OP_STS,
    OP_COUNT
};

static const char *const OPNAME[OP_COUNT] = {
    "RET", "LIT", "LD", "ST", "DEI", "DEO", "JMP", "JCN", "CAL",
    "ADD", "SUB", "MUL", "DIV", "AND", "ORA", "XOR",
    "EQU", "LTH", "", "PUSH", "POP", "LDS", "STS"
};

typedef struct {
    uint8_t *b;
    int sp;          /* number of bytes currently on the stack */
    int cap;
    const char *name;
} Stack;

typedef struct {
    uint8_t mem[MEM_SIZE];
    uint8_t dbuf[DS_SIZE];
    uint8_t rbuf[RS_SIZE];
    Stack ds, rs;
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

/* ---- stacks: big-endian, byte granular ---- */

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

static void jump(VM *m, uint32_t operand, int rel)
{
    m->pc = rel ? (uint32_t)(m->pc + (int32_t)operand) : operand;
}

static void step(VM *m)
{
    m->ipc = m->pc;
    uint8_t ins = (uint8_t)mrd(m, m->pc, 1);
    m->pc++;

    int reserved = ins >> 6;
    int r = (ins >> 5) & 1;
    int op = ins & 31;
    if (reserved != 0)
        fault(m, "reserved bits set in opcode 0x%02x", ins);
    if (op >= OP_COUNT)
        fault(m, "unknown opcode 0x%02x", ins);

    uint32_t a, b, res;
    uint32_t addr;

    switch (op) {
    case OP_RET:
        if (m->rs.sp == 0) {
            m->halted = 1;
            m->exit_code = 0;
        } else {
            m->pc = spop(m, &m->rs, 4);
        }
        break;
    case OP_LIT:
        a = mrd(m, m->pc, 4);
        m->pc = (uint32_t)(m->pc + 4);
        spush(m, &m->ds, a, 4);
        break;
    case OP_LD:
        addr = spop(m, &m->ds, 4);
        if (r)
            addr = (uint32_t)(m->pc + (int32_t)addr);
        spush(m, &m->ds, mrd(m, addr, 4), 4);
        break;
    case OP_ST:
        addr = spop(m, &m->ds, 4);
        if (r)
            addr = (uint32_t)(m->pc + (int32_t)addr);
        a = spop(m, &m->ds, 4);
        mwr(m, addr, a, 4);
        break;
    case OP_DEI: {
        uint8_t port = (uint8_t)spop(m, &m->ds, 4);
        spush(m, &m->ds, dev_in(port), 4);
        break;
    }
    case OP_DEO: {
        uint8_t port = (uint8_t)spop(m, &m->ds, 4);
        uint8_t v = (uint8_t)spop(m, &m->ds, 4);
        dev_out(m, port, v);
        break;
    }
    case OP_JMP:
        addr = spop(m, &m->ds, 4);
        jump(m, addr, r);
        break;
    case OP_JCN:
        addr = spop(m, &m->ds, 4);
        a = spop(m, &m->ds, 4);
        if (a)
            jump(m, addr, r);
        break;
    case OP_CAL:
        addr = spop(m, &m->ds, 4);
        spush(m, &m->rs, m->pc, 4);
        jump(m, addr, r);
        break;

    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
    case OP_AND: case OP_ORA: case OP_XOR:
        b = spop(m, &m->ds, 4);
        a = spop(m, &m->ds, 4);
        switch (op) {
        case OP_ADD: res = a + b; break;
        case OP_SUB: res = a - b; break;
        case OP_MUL: res = a * b; break;
        case OP_DIV: res = b ? a / b : 0; break;
        case OP_AND: res = a & b; break;
        case OP_ORA: res = a | b; break;
        default:     res = a ^ b; break;
        }
        spush(m, &m->ds, res, 4);
        break;

    case OP_EQU: case OP_LTH:
        b = spop(m, &m->ds, 4);
        a = spop(m, &m->ds, 4);
        res = (op == OP_EQU) ? (a == b) : (a < b);
        spush(m, &m->ds, res, 4);
        break;

    case OP_PUSH:
        if (m->ds.sp < 4)
            fault(m, "data stack underflow");
        if (m->rs.sp + 4 > m->rs.cap)
            fault(m, "return stack overflow");
        memcpy(m->rs.b + m->rs.sp, m->ds.b + m->ds.sp - 4, 4);
        m->ds.sp -= 4;
        m->rs.sp += 4;
        break;
    case OP_POP:
        if (m->rs.sp < 4)
            fault(m, "return stack underflow");
        if (m->ds.sp + 4 > m->ds.cap)
            fault(m, "data stack overflow");
        memcpy(m->ds.b + m->ds.sp, m->rs.b + m->rs.sp - 4, 4);
        m->rs.sp -= 4;
        m->ds.sp += 4;
        break;

    case OP_LDS: {
        uint32_t off = spop(m, &m->ds, 4) * 4;  /* cells -> bytes */
        Stack *s = r ? &m->rs : &m->ds;
        uint8_t tmp[4];
        if (off + 4 > (uint32_t)s->sp)
            fault(m, "LDS offset %u beyond %s stack (depth %d cells)", off / 4, s->name, s->sp / 4);
        memcpy(tmp, s->b + s->sp - off - 4, 4);
        if (m->ds.sp + 4 > m->ds.cap)
            fault(m, "data stack overflow");
        memcpy(m->ds.b + m->ds.sp, tmp, 4);
        m->ds.sp += 4;
        break;
    }
    case OP_STS: {
        uint32_t off = spop(m, &m->ds, 4) * 4;  /* cells -> bytes */
        Stack *s;
        uint8_t tmp[4];
        if (m->ds.sp < 4)
            fault(m, "data stack underflow");
        memcpy(tmp, m->ds.b + m->ds.sp - 4, 4);
        m->ds.sp -= 4;
        s = r ? &m->rs : &m->ds;
        if (off + 4 > (uint32_t)s->sp)
            fault(m, "STS offset %u beyond %s stack (depth %d cells)", off / 4, s->name, s->sp / 4);
        memcpy(s->b + s->sp - off - 4, tmp, 4);
        break;
    }
    default:
        fault(m, "unknown opcode 0x%02x", ins);
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
    uint8_t ins = m->mem[pc];
    int reserved = ins >> 6, r = (ins >> 5) & 1, op = ins & 31;
    char text[48];

    if (reserved == 0 && op < OP_COUNT && OPNAME[op][0] != '\0') {
        int len = snprintf(text, sizeof text, "%s%s", OPNAME[op], r ? ".r" : "");
        if (op == OP_LIT && (uint64_t)pc + 1 + 4 <= MEM_SIZE) {
            len += snprintf(text + len, sizeof text - (size_t)len, " 0x");
            for (int i = 0; i < 4; i++)
                len += snprintf(text + len, sizeof text - (size_t)len, "%02x", m->mem[pc + 1 + i]);
        }
    } else {
        snprintf(text, sizeof text, "??? 0x%02x", ins);
    }
    fprintf(stderr, "%08x  %-18s ds=", pc, text);
    print_stack(stderr, &m->ds);
    fprintf(stderr, "  rs=");
    print_stack(stderr, &m->rs);
    fputc('\n', stderr);
}

static void dump(const VM *m)
{
    fprintf(stderr, "ds: ");
    print_stack(stderr, &m->ds);
    fprintf(stderr, "\nrs: ");
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

    vm.ds = (Stack){ vm.dbuf, 0, DS_SIZE, "data" };
    vm.rs = (Stack){ vm.rbuf, 0, RS_SIZE, "return" };
    vm.pc = LOAD_ADDR;

    int status = run(&vm, limit, trace);
    if (do_dump)
        dump(&vm);
    return status;
}
