/* Microsecond time on a 68k Palm, from the timer that makes the system tick.
 *
 * Palm OS counts ticks (100 a second) with a Dragonball timer in restart
 * mode: its counter (TCN) runs from 0 to the compare value (TCMP) once per
 * tick. Reading the tick count and TCN together gives time to a fraction of
 * a tick. The timer used differs by chip (one on the EZ, two on the 328 and
 * VZ), so clk_start() looks for the one whose counter wraps whenever the tick
 * count moves, and falls back to whole ticks if none does (or on ARM).
 *
 * Registers of one timer, from its base: TCTL +0, TPRER +2, TCMP +4, TCR +6,
 * TCN +8, TSTAT +10 (MC68328, MC68EZ328 and MC68VZ328 manuals). */
#include <PalmOS.h>

#include "h2h.h"

#define REG16(addr) (*(volatile UInt16 *)(addr))
#define TCTL_TEN   0x0001   /* timer enabled */
#define TCTL_FRR   0x0100   /* free-running: does not restart at compare */
#define TSTAT_COMP 0x0001   /* compare reached, interrupt not yet taken */

#define CPU_MASK 0xFFFF0000UL
#define CPU_328  0x00010000UL
#define CPU_EZ   0x00020000UL
#define CPU_VZ   0x00030000UL
#define CPU_SZ   0x00040000UL

static UInt32 gBase;       /* tick count at clk_start */
static UInt32 gUsPerTick;
static UInt32 gLast;
static UInt32 gTimer;      /* base address of the timer in use, 0 for ticks */
static UInt16 gCounts;     /* counter steps per tick */
static Char gDesc[24];

/* Watches one timer for three tick changes: in restart mode its counter
 * must fall back each time the tick count moves and climb in between. */
static Boolean TimerMakesTicks(UInt32 base)
{
    UInt16 ctl = REG16(base), cmp = REG16(base + 4), c, prev;
    UInt32 t, lastTick, started = TimGetTicks();
    UInt16 wraps = 0, rises = 0, samples = 0;

    if (!(ctl & TCTL_TEN) || (ctl & TCTL_FRR) || cmp < 16)
        return false;
    lastTick = TimGetTicks();
    prev = REG16(base + 8);
    while (wraps < 3) {
        if (TimGetTicks() - started > 10)
            return false;
        t = TimGetTicks();
        c = REG16(base + 8);
        if (c > cmp)
            return false;
        samples++;
        if (t != lastTick) {
            if (c >= prev && prev > cmp / 4)
                return false;   /* the tick moved but this counter did not wrap */
            wraps++;
            lastTick = t;
        } else if (c > prev) {
            rises++;
        }
        prev = c;
    }
    return rises > 8 && samples > 16;
}

void clk_start(void)
{
    static const UInt32 timers328[] = {0xFFFFF600UL, 0xFFFFF60CUL};
    static const UInt32 timersVZ[] = {0xFFFFF600UL, 0xFFFFF610UL};
    static const UInt32 timersEZ[] = {0xFFFFF600UL};
    const UInt32 *list = NULL;
    UInt16 n = 0, i, found = 0;
    UInt32 cpu = 0, res;

    gUsPerTick = 1000000UL / SysTicksPerSecond();
    gTimer = 0;
    gCounts = 1;
    if (FtrGet(sysFtrCreator, sysFtrNumProcessorID, &cpu) == errNone) {
        switch (cpu & CPU_MASK) {
        case CPU_328: list = timers328; n = 2; break;
        case CPU_EZ: list = timersEZ; n = 1; break;
        case CPU_VZ:
        case CPU_SZ: list = timersVZ; n = 2; break;
        }
    }
    for (i = 0; i < n && !gTimer; i++)
        if (TimerMakesTicks(list[i])) {
            gTimer = list[i];
            gCounts = REG16(gTimer + 4) + 1;
            found = i + 1;
        }
    if (gTimer) {
        res = gUsPerTick / gCounts;
        StrPrintF(gDesc, "TMR%u %lu.%luus", found, res, (gUsPerTick * 10 / gCounts) % 10);
    } else {
        StrPrintF(gDesc, "ticks %lums", gUsPerTick / 1000);
    }
    gBase = TimGetTicks();
    gLast = 0;
}

UInt32 clk_us(void)
{
    UInt32 t, t2, us;
    UInt16 c, s;

    if (!gTimer) {
        us = (TimGetTicks() - gBase) * gUsPerTick;
    } else {
        do {
            t = TimGetTicks();
            c = REG16(gTimer + 8);
            s = REG16(gTimer + 10);
            t2 = TimGetTicks();
        } while (t != t2);
        /* The counter wrapped but the tick interrupt has not run yet. */
        if ((s & TSTAT_COMP) && c < gCounts / 2)
            t++;
        us = (t - gBase) * gUsPerTick + (UInt32)c * gUsPerTick / gCounts;
    }
    if ((Int32)(us - gLast) < 0)
        us = gLast;
    gLast = us;
    return us;
}

UInt32 clk_resolution_ns(void)
{
    return gTimer ? gUsPerTick * 1000 / gCounts : gUsPerTick * 1000;
}

const Char *clk_describe(void)
{
    return gDesc;
}
