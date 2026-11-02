/* CPU support: dispatcher, ALU helpers that are too large to inline. */
#include "recomp.h"
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

M68K m68k;
uint8_t *RAM;
uint32_t a5_expected;

static RecompFn *dispatch;      /* indexed by (pc - recomp_code_lo) / 2 */
static uint32_t dispatch_n;

void cpu_init(void) {
    dispatch_n = (recomp_code_hi - recomp_code_lo) / 2 + 1;
    dispatch = (RecompFn *)calloc(dispatch_n, sizeof(RecompFn));
    for (int i = 0; i < recomp_num_entries; i++) {
        uint32_t a = recomp_entries[i].addr;
        dispatch[(a - recomp_code_lo) / 2] = recomp_entries[i].fn;
    }
}

const char *func_name(uint32_t pc) {
    /* nearest MacsBug name at or below pc */
    const char *best = "?";
    uint32_t ba = 0;
    for (int i = 0; i < recomp_num_names; i++) {
        if (recomp_names[i].addr <= pc && recomp_names[i].addr >= ba) {
            ba = recomp_names[i].addr;
            best = recomp_names[i].name;
        }
    }
    return best;
}

/* Resolve pc to a host function; follows "jmp abs.l" stubs in the A5 jump
   table that live in RAM. */
static RecompFn lookup(uint32_t *ppc) {
    uint32_t pc = *ppc;
    for (int hops = 0; hops < 4; hops++) {
        if (pc >= recomp_code_lo && pc < recomp_code_hi && !(pc & 1)) {
            RecompFn f = dispatch[(pc - recomp_code_lo) / 2];
            if (f) { *ppc = pc; return f; }
            return NULL;
        }
        if (RW(pc) == 0x4EF9) {          /* jmp abs.l (jump table stub): count it as executed */
            pc = RL(pc + 2);
            m68k.cyc++;
            continue;
        }
        break;
    }
    *ppc = pc;
    return NULL;
}

void fatal(const char *fmt, ...);

uint32_t recomp_bad_entry(uint32_t pc, uint32_t func) {
    fatal("bad entry %06x into function %06x (%s)", pc, func, func_name(func));
    return 0;
}

uint32_t interp_run(uint32_t pc);

uint32_t m68k_call(uint32_t target) {
    extern int g_interp_all;
    uint32_t pc = target;
    if (g_interp_all) return interp_run(pc);
    RecompFn f = lookup(&pc);
    if (!f) {
        if (pc >= recomp_code_lo && pc < recomp_code_hi)
            fatal("call to unrecompiled code address %06x", pc);
        return interp_run(pc);
    }
    return f(pc);
}

void a5_check(uint32_t addr) {
    fatal("A5 changed (%08x, expected %08x) at %06x", A(5), a5_expected, addr);
}

uint32_t m68k_exception(int vec, uint32_t addr) {
    fatal("CPU exception %d at %06x (%s)", vec, addr, func_name(addr));
    return 0;
}

uint32_t m68k_trap(int n, uint32_t func, uint32_t next) {
    fatal("unhandled trap #%d func %x at %06x", n, func, next);
    return next;
}

/* Run a 68k subroutine to completion from host code, as a system-state
   extension calling into the program would: d0/d1/a6 and the stack
   arguments are set, everything else is restored afterwards.  Returns d0.
   Signals are not delivered and the scheduler does not run meanwhile. */
#define CALL_SENTINEL 0xFFFFFFF0u
int g_in_sync_call;

uint32_t cpu_call_sync(uint32_t target, uint32_t a6, uint32_t d0, uint32_t d1, const uint32_t *args, int nargs) {
    extern int g_interp_all;
    M68K saved = m68k;
    A(7) = (A(7) - 0x200) & ~3u;          /* stay clear of the interrupted code's stack */
    for (int i = nargs - 1; i >= 0; i--) { A(7) -= 4; WL(A(7), args[i]); }
    A(7) -= 4; WL(A(7), CALL_SENTINEL);
    A(6) = a6; D(0) = d0; D(1) = d1;
    m68k.cyc_limit = INT64_MAX;
    g_in_sync_call++;
    uint32_t pc = target;
    while (pc != CALL_SENTINEL) {
        RecompFn f = g_interp_all ? NULL : lookup(&pc);
        if (f) pc = f(pc);
        else if (!g_interp_all && pc >= recomp_code_lo && pc < recomp_code_hi)
            fatal("synchronous call reached unrecompiled code address %06x", pc);
        else pc = interp_run(pc);
        m68k.yield = 0;
    }
    g_in_sync_call--;
    uint32_t r = D(0);
    int64_t cyc = m68k.cyc;
    m68k = saved;
    m68k.cyc = cyc;
    return r;
}

/* ---------------------------------------------------------------- run loop */
int g_run_depth;    /* nested run loops: 2 while a forked child runs */

void cpu_run_until(volatile int *flag) {
    extern int g_interp_all;
    g_run_depth++;
    while (!*flag) {
        uint32_t pc = m68k.pc;
        RecompFn f = g_interp_all ? NULL : lookup(&pc);
        if (f) m68k.pc = f(pc);
        else if (!g_interp_all && pc >= recomp_code_lo && pc < recomp_code_hi)
            fatal("jump to unrecompiled code address %06x", pc);
        else m68k.pc = interp_run(pc);
        /* the scheduler runs only where code yielded (backward branches), so
           translated and interpreted execution see events at the same points */
        if (m68k.yield) {
            m68k.yield = 0;
            sched_service();
        }
    }
    g_run_depth--;
}

void cpu_run(void) {
    static volatile int never;
    cpu_run_until(&never);
}
