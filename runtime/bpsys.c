/* HLE of cdi_bpsys, the Balboa system extension that players carry in ROM.

   Balboa programs link it into the video driver (SS_SLink $5A, module name
   "cdi_bpsys") and talk to it with SetStat functions $42xx.  The extension
   owns the pointer: every few fields it moves the control panel cursor,
   hit-tests the program's hotspot trees and reports hotspot events by
   calling back into the program (its packet allocator and
   dispatch_function_sys) with the program's a6.  The program-side library
   is linked into the game, so the structures below were read from it:

   video environment (VE, 0x6C bytes)
     +10.w video path     +48.l control panel    +52.l program a6
     +56.l -> dispatcher busy flag (word)        +64.l dispatch_function_sys
   control panel (CP, 0x7A bytes)
     +0.b  flags: $80 halted, $20 cursor hidden, $04 hotspot tree installed
     +1.b  flags: $40 re-evaluate, $20 update pending (ve_cp_wait), $04 cursor changed
     +3.b  flags: $10 idle function armed
     +4.l  displayed cursor   +20.w/+22.w position   +26..+38.l limits x0,y0,x1,y1
     +46.l hotspot tree list  +50.l packet allocator +62.l update period (ticks)
     +66.l idle fn  +70.l wake fn  +74.l idle arg  +78.l idle periods
     +86.l override cursor    +90.l default cursor
   hotspot tree node: +0.w flags ($8000 disabled) +12.l root hotspot
     +16.l hotspot under the cursor  +20.l next node
   hotspot (30 bytes): +0.w flags ($8000 locked, low byte = index)
     +2.l cursor  +6.w x0,y0,x1,y1  +18.l next sibling  +22.l first child
     +26.l callbacks: 10-byte entries (mask.w, function.l, argument.l),
           mask bit 14 marks the last entry
   packet (28 bytes): +0.b in use  +2.w event  +4.l argument  +8.w x
     +10.w y  +24.l hotspot
   events: $0004/$0008 button 1/2 pressed, $0100 enter, $0200 leave
   cursor: +0.w flags ($1000 cycling) +2.l first frame +6.l last frame
     +10.l current frame +16.w/+18.w hit point (high resolution units)
     frame (10 bytes): pattern.l (16x16, 1 bpp)  color.l (GC_Col)  fields.w

   Positions are in high resolution screen units.  An NTSC program (the CSD
   says LI="525") uses 768x480 directly; a PAL one adds 40 to its hotspot
   rectangles, so its coordinates start 40 lines above an NTSC picture. */
#include "recomp.h"
#include "runtime.h"
#include <string.h>

#define EV_BUTTON1 0x0004
#define EV_BUTTON2 0x0008
#define EV_ENTER   0x0100
#define EV_LEAVE   0x0200
#define NTSC_TOP   0       /* first visible line in pointer coordinates (NTSC CSD) */

uint32_t cpu_call_sync(uint32_t target, uint32_t a6, uint32_t d0, uint32_t d1, const uint32_t *args, int nargs);
int os9_sig_masked(void);
void video_hle_cursor(int on, int x, int y, uint32_t ptn, int h, uint32_t color);

static uint32_t cur_ve;          /* current video environment */
static int linked;

/* events waiting until the program can take them */
typedef struct { uint32_t fn, arg, hs; uint16_t event; int16_t x, y; int packet; } Pending;
static Pending pend[32];
static int npend;

static int64_t idle_ticks;       /* time without pointer activity */
static int asleep;
static int ob1, ob2;
static int frame_left;           /* fields left on the current cursor frame */
static uint32_t frame_cursor;

int bpsys_setstat(uint32_t code, uint32_t fn) {
    switch (fn) {
    case 0x4200: /* version: high word of d0 */
        D(0) = 0x20000000u;
        return 0;
    case 0x4204: case 0x4206: /* install the cursor / pointer handlers */
        linked = 1;
        return 0;
    case 0x420C: case 0x4214: /* VE to the front / make it current: a0 = VE */
        if (cur_ve != A(0)) LOG("bpsys: video environment %06x is current", A(0));
        cur_ve = A(0);
        return 0;
    case 0x4210: /* remove VE */
        if (cur_ve == A(0)) cur_ve = 0;
        return 0;
    }
    LOG("bpsys: SetStat %x function %x not implemented", code, fn);
    return 0;
}

static int contains(uint32_t hs, int x, int y) {
    int16_t x0 = (int16_t)RW(hs + 6), y0 = (int16_t)RW(hs + 8);
    int16_t x1 = (int16_t)RW(hs + 10), y1 = (int16_t)RW(hs + 12);
    return x >= x0 && x < x1 && y >= y0 && y < y1;
}

/* deepest unlocked hotspot containing (x, y) in a sibling list */
static uint32_t hit(uint32_t hs, int x, int y, int depth) {
    for (int guard = 0; hs && guard < 256; guard++, hs = RL(hs + 18)) {
        if (RW(hs) & 0x8000) continue;
        if (!contains(hs, x, y)) continue;
        uint32_t child = RL(hs + 22);
        uint32_t deeper = (child && depth < 16) ? hit(child, x, y, depth + 1) : 0;
        return deeper ? deeper : hs;
    }
    return 0;
}

static void queue_event(uint32_t hs, uint16_t event, int x, int y) {
    uint32_t cb = RL(hs + 26);
    for (int k = 0; cb && k < 16; k++, cb += 10) {
        uint16_t mask = RW(cb);
        if ((mask & event & 0x3FFF) && npend < 32) {
            Pending *p = &pend[npend++];
            p->fn = RL(cb + 2); p->arg = RL(cb + 6); p->hs = hs;
            p->event = event; p->x = (int16_t)x; p->y = (int16_t)y; p->packet = 1;
        }
        if (mask & 0x4000) break;
    }
}

static void queue_call(uint32_t fn, uint32_t arg) {
    if (!fn || npend >= 32) return;
    Pending *p = &pend[npend++];
    memset(p, 0, sizeof *p);
    p->fn = fn; p->arg = arg;
}

/* hand queued events to the program's dispatcher */
static void deliver(uint32_t ve) {
    uint32_t cp = RL(ve + 48), a6 = RL(ve + 52), dispatch = RL(ve + 64);
    uint32_t busy = RL(ve + 56);
    if (!npend || !dispatch) return;
    if ((busy && RW(busy)) || os9_sig_masked()) return;   /* the queue is being worked on */
    int n = 0;
    for (; n < npend; n++) {
        Pending *p = &pend[n];
        if (!p->packet) {
            uint32_t args[2] = { p->arg, 0 };
            cpu_call_sync(dispatch, a6, 0, p->fn, args, 2);
            continue;
        }
        uint32_t pkt = cpu_call_sync(RL(cp + 50), a6, cp, 0, NULL, 0);
        if (!pkt) { LOG("bpsys: no free packet"); continue; }
        WW(pkt + 2, p->event);
        WL(pkt + 4, p->arg);
        WW(pkt + 8, (uint16_t)p->x);
        WW(pkt + 10, (uint16_t)p->y);
        WL(pkt + 24, p->hs);
        uint32_t args[2] = { pkt, 1 };       /* 1: the dispatcher frees the packet */
        if (cpu_call_sync(dispatch, a6, p->event, p->fn, args, 2)) {
            WB(pkt, RB(pkt) & ~1u);          /* queue full: drop */
            LOG("bpsys: dispatch queue full");
        }
        TRACE("bpsys: event %04x hotspot %06x (#%d) -> %06x", p->event, p->hs, RW(p->hs) & 0xFF, p->fn);
    }
    npend = 0;
}

static void update_cursor(uint32_t cp, int x, int y) {
    uint32_t cur = RL(cp + 4);
    int hidden = RB(cp) & 0x20;
    if (!cur || hidden) { video_hle_cursor(0, 0, 0, 0, 0, 0); return; }
    uint32_t first = RL(cur + 2), last = RL(cur + 6), frame = RL(cur + 10);
    if (!frame) frame = first;
    if (!last) last = first;
    if (cur != frame_cursor) { frame_cursor = cur; frame_left = RW(frame + 8); }
    if ((RW(cur) & 0x1000) && last != first && --frame_left <= 0) {   /* cycling through frames */
        frame = (frame >= last || frame < first) ? first : frame + 10;
        WL(cur + 10, frame);
        frame_left = RW(frame + 8) ? RW(frame + 8) : 1;
    }
    int hx = (int16_t)RW(cur + 16), hy = (int16_t)RW(cur + 18);
    int h = (int16_t)RW(cur + 22);
    video_hle_cursor(1, x - hx, y - NTSC_TOP - hy, RL(frame), h > 0 && h <= 32 ? h : 16, RL(frame + 4));
}

void bpsys_field(void) {
    uint32_t ve = cur_ve;
    if (!ve) return;
    uint32_t cp = RL(ve + 48);
    if (!cp) return;
    int x = (int16_t)RW(cp + 20), y = (int16_t)RW(cp + 22);
    int moved = input.dx || input.dy || input.abs;
    int pressed1 = input.b1 && !ob1, pressed2 = input.b2 && !ob2;
    int active = moved || pressed1 || pressed2 || input.b1 || input.b2;
    ob1 = input.b1; ob2 = input.b2;
    uint8_t f0 = RB(cp), f1 = RB(cp + 1);

    if (!(f0 & 0x80)) {
        /* move within the panel's limits */
        if (input.abs) { x = input.x; y = input.y + NTSC_TOP; }
        x += input.dx; y += input.dy;
        int x0 = (int)RL(cp + 26), y0 = (int)RL(cp + 30), x1 = (int)RL(cp + 34), y1 = (int)RL(cp + 38);
        if (x1 > x0) { if (x < x0) x = x0; if (x > x1) x = x1; }
        if (y1 > y0) { if (y < y0) y = y0; if (y > y1) y = y1; }
        WW(cp + 20, (uint16_t)x); WW(cp + 22, (uint16_t)y);

        /* hotspots under the cursor; none while the cursor is hidden: the game
           hides it during scene changes and relies on that (a click on the
           map while it is still loading would otherwise leave it stuck) */
        for (uint32_t node = (f0 & 0x20) ? 0 : RL(cp + 46), guard = 0; node && guard < 16; node = RL(node + 20), guard++) {
            if (RW(node) & 0x8000) continue;
            uint32_t old = RL(node + 16);
            uint32_t now = hit(RL(node + 12), x, y, 0);
            if (now != old) {
                WL(node + 16, now);
                if (old) queue_event(old, EV_LEAVE, x, y);
                if (now) queue_event(now, EV_ENTER, x, y);
            }
            if (now && pressed1) queue_event(now, EV_BUTTON1, x, y);
            if (now && pressed2) queue_event(now, EV_BUTTON2, x, y);
            /* the cursor of the hotspot under the pointer, unless overridden */
            uint32_t want = RL(cp + 86);
            if (!want) want = (now && RL(now + 2)) ? RL(now + 2) : RL(cp + 90);
            if (want) WL(cp + 4, want);
        }

        /* idle ("sleep") and wake functions */
        int64_t period = (int64_t)RL(cp + 78) * (int64_t)(RL(cp + 62) ? RL(cp + 62) : 1);
        if (active) {
            idle_ticks = 0;
            if (asleep) { asleep = 0; queue_call(RL(cp + 70), RL(cp + 74)); }
        } else if (RB(cp + 3) & 0x10) {
            idle_ticks += 100 / FIELD_RATE + 1;         /* OS-9 ticks are 1/100 s */
            if (period > 0 && idle_ticks >= period) {
                idle_ticks = 0;
                asleep = 1;
                WB(cp + 3, RB(cp + 3) & ~0x10u);       /* the idle function re-arms itself */
                queue_call(RL(cp + 66), RL(cp + 74));
            }
        }
        WB(cp + 1, f1 & ~0x64u);                       /* update done (ve_cp_wait), changes seen */
    }
    update_cursor(cp, x, y);
    deliver(ve);
}

/* ---------------------------------------------------------------- snapshots */
#include "state.h"

void bpsys_state(St *s) {
    ST(s, cur_ve); ST(s, linked); ST(s, pend); ST(s, npend);
    ST(s, idle_ticks); ST(s, asleep); ST(s, ob1); ST(s, ob2); ST(s, frame_left); ST(s, frame_cursor);
}
