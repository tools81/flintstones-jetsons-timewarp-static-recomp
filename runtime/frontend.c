/* Front end: the frame loop, presentation and save states.

   The emulation runs one field (1/60 s) at a time and stops between fields,
   where the whole machine can be snapshotted (state.c).  The original
   edition shows every field as the CD-i did.  The HD edition adds:

   - run-ahead: after each real field the machine is snapshotted, run R
     fields further with the same input, that field is shown, and the
     snapshot is restored.  The game's own reaction time to input is hidden.
   - sprite interpolation: the game animates at 10-15 pictures a second.
     While a picture is held, the emulation looks further ahead (from the
     same snapshot) for the next picture, and motion.c moves the parts that
     moved along their path, so motion is shown at 60 fields a second.
   - Cartoon HD / Cartoon HD+ upscaling (cartoon.c).
   - quicksave: F2 save, F4 load, F3 next of 10 slots. */
#include "recomp.h"
#include "runtime.h"
#include "state.h"
#include "cartoon.h"
#include "motion.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 768
#define H 240
#define NPIX (W * H)
#define LOOKAHEAD_MAX 12     /* fields searched for the next picture */
#define LOOKAHEAD_STEP 5     /* fields searched per frame */

int g_spec;
extern int g_run_depth;
extern int g_max_frames;

static volatile int field_done;
static int in_loop;

/* HD settings (saves/hd_settings.ini, command line, F5/F6/F8) */
static int runahead = 2;
static int interp_on = 1;
static int upscaler = CARTOON_HDPLUS;
static int slot = 1;

/* ---------------------------------------------------------------- OSD */
static char osd_text[96];
static int osd_frames;

static void osd(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(osd_text, sizeof osd_text, fmt, ap);
    va_end(ap);
    osd_frames = 150;
    LOG("%s", osd_text);
}

/* 5x7 font: ' ' .. 'Z' (lower case is shown as upper case) */
static const uint8_t font5x7[][7] = {
    {0,0,0,0,0,0,0},{4,4,4,4,4,0,4},{10,10,0,0,0,0,0},{10,31,10,10,31,10,0},{4,15,20,14,5,30,4},
    {24,25,2,4,8,19,3},{12,18,20,8,21,18,13},{4,4,0,0,0,0,0},{2,4,8,8,8,4,2},{8,4,2,2,2,4,8},
    {0,4,21,14,21,4,0},{0,4,4,31,4,4,0},{0,0,0,0,12,4,8},{0,0,0,31,0,0,0},{0,0,0,0,0,12,12},
    {0,1,2,4,8,16,0},{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{31,2,4,2,1,17,14},
    {2,6,10,18,31,2,2},{31,16,30,1,1,17,14},{6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
    {14,17,17,15,1,2,12},{0,12,12,0,12,12,0},{0,12,12,0,12,4,8},{2,4,8,16,8,4,2},{0,0,31,0,31,0,0},
    {8,4,2,1,2,4,8},{14,17,1,2,4,0,4},{14,17,1,13,21,21,14},{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14},{28,18,17,17,17,18,28},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{7,2,2,2,2,18,12},{17,18,20,24,20,18,17},
    {16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,17,25,21,19,17,17},{14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},
    {31,4,4,4,4,4,4},{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},
    {17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
};

static void osd_draw(uint32_t *img, int w, int h) {
    if (osd_frames <= 0) return;
    osd_frames--;
    int scale = w / 384 > 1 ? w / 384 : 1;
    int x0 = 8 * scale, y0 = h - 16 * scale;
    for (int pass = 0; pass < 2; pass++) {          /* shadow, then text */
        uint32_t col = pass ? 0xFFFFE070u : 0xFF000000u;
        int off = pass ? 0 : scale;
        int x = x0;
        for (const char *p = osd_text; *p; p++, x += 6 * scale) {
            int c = *p;
            if (c >= 'a' && c <= 'z') c -= 32;
            if (c < ' ' || c > 'Z') c = ' ';
            const uint8_t *g = font5x7[c - ' '];
            for (int yy = 0; yy < 7; yy++)
                for (int xx = 0; xx < 5; xx++) {
                    if (!(g[yy] & (16 >> xx))) continue;
                    for (int sy = 0; sy < scale; sy++)
                        for (int sx = 0; sx < scale; sx++) {
                            int px = x + xx * scale + sx + off, py = y0 + yy * scale + sy + off;
                            if (px >= 0 && px < w && py >= 0 && py < h) img[py * w + px] = col;
                        }
                }
        }
    }
}

/* ---------------------------------------------------------------- settings */
static const char *upscaler_name(int u) {
    return u == CARTOON_HDPLUS ? "Cartoon HD+" : u == CARTOON_HD ? "Cartoon HD" : "off (original pixels)";
}

static void settings_path(char *p, size_t n) { snprintf(p, n, "%s/hd_settings.ini", g_data_dir); }

static void settings_load(void) {
    char p[600];
    settings_path(p, sizeof p);
    FILE *f = fopen(p, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        int v;
        if (sscanf(line, "runahead=%d", &v) == 1 && v >= 0 && v <= 4) runahead = v;
        if (sscanf(line, "interpolation=%d", &v) == 1) interp_on = v != 0;
        if (sscanf(line, "upscaler=%d", &v) == 1 && v >= 0 && v < CARTOON_MODES) upscaler = v;
        if (sscanf(line, "slot=%d", &v) == 1 && v >= 1 && v <= 10) slot = v;
    }
    fclose(f);
}

static void settings_save(void) {
    char p[600];
    settings_path(p, sizeof p);
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "runahead=%d\ninterpolation=%d\nupscaler=%d\nslot=%d\n", runahead, interp_on, upscaler, slot);
    fclose(f);
}

static int cli_runahead = -1, cli_interp = -1, cli_upscaler = -1;

/* --runahead N, --interpolation on|off, --upscale hdplus|hd|off */
int frontend_option(const char *opt, const char *val) {
    if (!strcmp(opt, "--runahead") && val) { cli_runahead = atoi(val); return 1; }
    if (!strcmp(opt, "--interpolation") && val) { cli_interp = strcmp(val, "off") != 0; return 1; }
    if (!strcmp(opt, "--upscale") && val) {
        cli_upscaler = !strcmp(val, "hd") ? CARTOON_HD : !strcmp(val, "off") ? CARTOON_OFF : CARTOON_HDPLUS;
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- frames */
static void run_field(void) {
    field_done = 0;
    cpu_run_until(&field_done);
}

static uint32_t *work;          /* picture being prepared for presentation */
static double frame_t0;         /* start of the frame's work */
static int64_t t_real;          /* real fields run */
/* work per HD frame (emulation, interpolation, upscaling), for the log */
static double ft_sum, ft_max;
static long ft_n, ft_over;
static double ph[6], ph_worst[6];  /* real, run-ahead, lookahead, motion, upscale, save/load */
static int la_fields, la_worst;
static long over_by[6], over_la;

/* show a 768x240 picture: the cursor goes on top, then the HD upscaler */
static void present(const uint32_t *pic, const VideoCursor *c) {
    if (!g_edition_hd) {
        host_present(pic, W, H);
        return;
    }
    if (pic != work) memcpy(work, pic, NPIX * 4);
    video_cursor_draw(work, c);
    int ow, oh;
    double qu = host_time();
    uint32_t *img = cartoon_upscale(work, W, H, upscaler, &ow, &oh);
    osd_draw(img, ow, oh);
    ph[4] = host_time() - qu;
    if (in_loop && frame_t0 > 0) {
        double dt = host_time() - frame_t0;
        ft_sum += dt; ft_n++;
        if (dt > ft_max) { ft_max = dt; memcpy(ph_worst, ph, sizeof ph); la_worst = la_fields; }
        if (dt > 1.0 / 60) {
            ft_over++;
            int big = 0;
            for (int i = 1; i < 6; i++) if (ph[i] > ph[big]) big = i;
            over_by[big]++;
            if (la_fields >= 8) over_la++;
        }
        frame_t0 = 0;
    }
    host_present(img, ow, oh);
}

/* fields shown as they come while a forked child (the Philips logo) runs */
void frontend_field_done(void) {
    if (g_run_depth == 1 && in_loop) { field_done = 1; return; }
    if (g_spec) return;
    present(video_fb, &g_cursor);
    host_poll();
}

/* ---------------------------------------------------------------- interpolation */
static St snap;
static uint32_t *pic_p, *pic_n; /* picture shown since tp, and the next one (tn) */
static int64_t tp = -1, tn = -1;
static int n_valid;             /* pic_n/tn known */
static int64_t quiet_until = -1;/* looked ahead: no change before this field */
static int n_b1, n_b2;          /* buttons when the lookahead ran */
static int pair_ready;          /* motion.c has prepared pic_p -> pic_n */
static St la_snap;              /* where the search for the next picture stopped */
static int64_t la_t;            /* ... its field */
static int la_valid;

static void interp_reset(void) {
    tp = tn = quiet_until = -1;
    n_valid = 0;
    la_valid = 0;
    pair_ready = 0;
}

/* one HD frame: returns 0 if it could not snapshot (then nothing was run) */
static int hd_frame(void) {
    if (!os9_can_snapshot()) return 0;
    int R = runahead;
    /* the real field: heard, not shown unless no run-ahead */
    double q = host_time(), q2;
    memset(ph, 0, sizeof ph);
    la_fields = 0;
    g_render_skip = R > 0;
    run_field();
    t_real++;
    q2 = host_time(); ph[0] = q2 - q; q = q2;
    if (state_save(&snap)) { g_render_skip = 0; return 1; }   /* cannot happen: checked above */
    q2 = host_time(); ph[5] += q2 - q; q = q2;
    g_spec = 1;
    for (int i = 1; i <= R; i++) {
        g_render_skip = i < R;
        run_field();
    }
    q2 = host_time(); ph[1] = q2 - q; q = q2;
    g_render_skip = 0;
    int64_t t_disp = t_real + R;
    VideoCursor cur = g_cursor;
    if (tp < 0 || memcmp(video_fb, pic_p, NPIX * 4)) {         /* a new picture */
        memcpy(pic_p, video_fb, NPIX * 4);
        tp = t_disp;
        n_valid = 0;
        pair_ready = 0;
        quiet_until = -1;
        la_valid = 0;
    }
    if (interp_on) {
        int changed_input = input.b1 != n_b1 || input.b2 != n_b2;      /* input changed the future */
        int need = (!(n_valid && tn > t_disp) && t_disp >= quiet_until) || (n_valid && changed_input);
        if (need) {
            /* the search goes on from where the last frame left it, a few
               fields per frame, so no frame takes much longer than another */
            if (n_valid || changed_input) la_valid = 0;
            n_valid = 0;
            pair_ready = 0;
            if (la_valid && la_t > t_disp) {
                if (state_load(&la_snap)) la_valid = 0;
            } else la_valid = 0;
            if (!la_valid) { la_t = t_disp; n_b1 = input.b1; n_b2 = input.b2; }
            for (int k = 0; k < LOOKAHEAD_STEP && la_t - t_disp < LOOKAHEAD_MAX; k++) {
                run_field();
                la_t++;
                la_fields++;
                if (memcmp(video_fb, pic_p, NPIX * 4)) {
                    memcpy(pic_n, video_fb, NPIX * 4);
                    tn = la_t;
                    n_valid = 1;
                    break;
                }
            }
            la_valid = 0;
            if (!n_valid) {
                if (la_t - t_disp >= LOOKAHEAD_MAX) quiet_until = la_t;
                else la_valid = state_save(&la_snap) == 0;
            }
        }
    }
    q2 = host_time(); ph[2] = q2 - q; q = q2;
    g_spec = 0;
    if (state_load(&snap)) fatal("run-ahead: snapshot restore failed (error %d at %u of %u)", snap.err, (unsigned)snap.pos, (unsigned)snap.n);
    const uint32_t *show = pic_p;
    if (interp_on && n_valid && tn - tp > 1 && t_disp > tp) {
        q2 = host_time(); ph[5] += q2 - q; q = q2;
        if (!pair_ready) {
            extern long g_interp_moving;
            long before = g_interp_moving;
            motion_prepare(pic_p, pic_n);
            pair_ready = 1;
            if (g_interp_moving != before && getenv("TW_INTERP_LOG"))
            {
                extern unsigned long long host_frames(void);
                LOG("interpolating motion: fields %lld..%lld (shown at %lld, frame %llu)", (long long)tp, (long long)tn,
                    (long long)t_disp, host_frames() + 1);
            }
        }
        float alpha = (float)(t_disp - tp) / (float)(tn - tp);
        motion_render(work, pic_p, pic_n, alpha);
        show = work;
        q2 = host_time(); ph[3] = q2 - q; q = q2;
    }
    present(show, &cur);
    return 1;
}

/* ---------------------------------------------------------------- hotkeys */
static void hotkeys(void) {
    int k;
    while ((k = host_take_hotkey()) != 0) {
        if (!g_edition_hd) continue;
        char when[32];
        switch (k) {
        case HOTKEY_SAVE:
            if (!os9_can_snapshot()) { osd("Quicksave is not available during the Philips logo"); break; }
            if (state_save_slot(slot)) osd("Slot %d: save failed", slot);
            else osd("Saved to slot %d", slot);
            break;
        case HOTKEY_LOAD:
            if (!state_slot_exists(slot, NULL, 0)) { osd("Slot %d is empty", slot); break; }
            if (state_load_slot(slot)) osd("Slot %d: cannot load (not a Timewarp save of this version)", slot);
            else { interp_reset(); osd("Loaded slot %d", slot); }
            break;
        case HOTKEY_SLOT:
            slot = slot % 10 + 1;
            if (state_slot_exists(slot, when, sizeof when)) osd("Slot %d  (%s)", slot, when);
            else osd("Slot %d  (empty)", slot);
            settings_save();
            break;
        case HOTKEY_RUNAHEAD:
            runahead = (runahead + 1) % 4;
            interp_reset();
            osd("Run-ahead: %d field%s", runahead, runahead == 1 ? "" : "s");
            settings_save();
            break;
        case HOTKEY_INTERP:
            interp_on = !interp_on;
            interp_reset();
            osd("Sprite interpolation: %s", interp_on ? "on (60 fps)" : "off");
            settings_save();
            break;
        case HOTKEY_UPSCALER:
            upscaler = (upscaler + CARTOON_MODES - 1) % CARTOON_MODES;
            osd("Upscaler: %s", upscaler_name(upscaler));
            settings_save();
            break;
        }
    }
}

static void frame_stats(void) {
    extern long g_interp_pairs, g_interp_moving;
    if (ft_n) LOG("HD frame work: average %.2f ms, longest %.2f ms, %ld of %ld frames over 16.7 ms",
                  ft_sum / ft_n * 1e3, ft_max * 1e3, ft_over, ft_n);
    if (g_interp_pairs) LOG("sprite interpolation: %ld of %ld picture changes were motion and were interpolated",
                            g_interp_moving, g_interp_pairs);
    if (ft_n) LOG("  longest frame: real %.1f, run-ahead %.1f, lookahead %.1f (%d fields), motion %.1f, upscale %.1f, snapshots %.1f ms",
                  ph_worst[0] * 1e3, ph_worst[1] * 1e3, ph_worst[2] * 1e3, la_worst, ph_worst[3] * 1e3, ph_worst[4] * 1e3, ph_worst[5] * 1e3);
    if (ft_over) LOG("  over budget, largest part: real %ld, run-ahead %ld, lookahead %ld (8+ fields: %ld), motion %ld, upscale %ld, snapshots %ld",
                     over_by[0], over_by[1], over_by[2], over_la, over_by[3], over_by[4], over_by[5]);
}

/* TW_BENCH_UPSCALE=picture.bmp (a 768x240 frame dump): time the upscalers */
static void bench_upscale(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return;
    static uint8_t b[54 + NPIX * 4];
    size_t n = fread(b, 1, sizeof b, f);
    fclose(f);
    if (n < sizeof b) return;
    uint32_t *pic = (uint32_t *)calloc(NPIX, 4);
    memcpy(pic, b + 54, NPIX * 4);
    cartoon_init();
    for (int m = CARTOON_HD; m <= CARTOON_HDPLUS; m++) {
        int ow, oh;
        double t = host_time();
        for (int i = 0; i < 50; i++) cartoon_upscale(pic, W, H, m, &ow, &oh);
        LOG("upscale %s: %.2f ms", upscaler_name(m), (host_time() - t) / 50 * 1e3);
    }
    exit(0);
}

void frontend_run(void) {
    if (getenv("TW_BENCH_UPSCALE")) bench_upscale(getenv("TW_BENCH_UPSCALE"));
    if (g_edition_hd) {
        settings_load();
        if (cli_runahead >= 0 && cli_runahead <= 8) runahead = cli_runahead;
        if (cli_interp >= 0) interp_on = cli_interp;
        if (cli_upscaler >= 0) upscaler = cli_upscaler;
        g_cursor_external = 1;
        work = (uint32_t *)calloc(NPIX, 4);
        pic_p = (uint32_t *)calloc(NPIX, 4);
        pic_n = (uint32_t *)calloc(NPIX, 4);
        cartoon_init();
        LOG("HD edition: upscaler %s, run-ahead %d, sprite interpolation %s",
            upscaler_name(upscaler), runahead, interp_on ? "on" : "off");
    }
    in_loop = 1;
    if (g_edition_hd) atexit(frame_stats);
    for (;;) {
        hotkeys();
        double t0 = g_edition_hd ? host_time() : 0;
        frame_t0 = t0;
        if (!(g_edition_hd && (runahead > 0 || interp_on) && hd_frame())) {
            g_render_skip = 0;
            run_field();
            if (g_edition_hd) { t_real++; interp_reset(); }
            present(video_fb, &g_cursor);
        }
        host_poll();
    }
}
