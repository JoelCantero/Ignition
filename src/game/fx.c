#include "fx.h"

#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846

/* 0xc47f0: priority of each effect type when every slot is taken. */
static const int PRIORITY[12] = {0, 5, 2, 90, 20, 10, 10, 90, 2, 50, 90, 10};

/* Animation tables: u0, v0, u1, v1 per frame. */
static const int ANIM_C4110[8][4] = { /* skid dust, SMOKE.TEX */
    {0, 0, 64, 64}, {64, 0, 128, 64}, {128, 0, 192, 64}, {192, 0, 256, 64},
    {0, 64, 64, 128}, {64, 64, 128, 128}, {128, 64, 192, 128}, {192, 64, 256, 128},
};
static const int ANIM_C4230[8][4] = { /* smoke trails and DARKSMOK */
    {0, 0, 64, 64}, {64, 0, 128, 64}, {128, 0, 192, 64}, {192, 0, 256, 64},
    {0, 64, 64, 128}, {64, 64, 128, 128}, {128, 64, 192, 128}, {192, 64, 256, 128},
};
static const int ANIM_C43C0[12][4] = { /* EXSMOKE */
    {0, 0, 64, 64}, {64, 0, 128, 64}, {128, 0, 192, 64}, {192, 0, 256, 64},
    {0, 64, 64, 128}, {64, 64, 128, 128}, {128, 64, 192, 128}, {192, 64, 256, 128},
    {0, 128, 64, 192}, {64, 128, 128, 192}, {128, 128, 192, 192}, {192, 128, 256, 192},
};
static const int ANIM_C42C0[16][4] = { /* BOOM */
    {0, 0, 64, 64}, {64, 0, 128, 64}, {128, 0, 192, 64}, {192, 0, 256, 64},
    {0, 64, 64, 128}, {64, 64, 128, 128}, {128, 64, 192, 128}, {192, 64, 256, 128},
    {0, 128, 64, 192}, {64, 128, 128, 192}, {128, 128, 192, 192}, {192, 128, 256, 192},
    {0, 192, 64, 256}, {64, 192, 128, 256}, {128, 192, 192, 256}, {192, 192, 256, 256},
};
static const int ANIM_C4010[5][4] = { /* flashing lights of collision objects 0x9a, LIGHT.TEX (0x35e9b) */
    {0, 0, 64, 64}, {64, 0, 128, 64}, {128, 0, 192, 64}, {192, 0, 256, 64}, {0, 64, 64, 128},
};
static const int ANIM_C4060[12][4] = { /* water splash, LIGHT.TEX */
    {64, 64, 128, 128}, {128, 64, 192, 128}, {192, 64, 256, 128}, {0, 128, 64, 192},
    {64, 128, 128, 192}, {128, 128, 192, 192}, {192, 128, 256, 192}, {0, 192, 64, 256},
    {64, 192, 128, 256}, {128, 192, 192, 256}, {192, 192, 256, 256}, {0, 0, 64, 64},
};
static const int ANIM_C41A0[9][4] = { /* nitro smoke, SMOKE.TEX lower half */
    {0, 128, 64, 192}, {64, 128, 128, 192}, {128, 128, 192, 192}, {192, 128, 256, 192},
    {0, 192, 64, 256}, {64, 192, 128, 256}, {128, 192, 192, 256}, {192, 192, 256, 256}, {192, 192, 256, 256},
};
static const int ANIM_C44C0[4][4] = {{0, 0, 42, 42}, {42, 0, 84, 42}, {84, 0, 126, 42}, {126, 0, 168, 42}};
static const int ANIM_C4500[4][4] = {{0, 42, 42, 84}, {42, 42, 84, 84}, {84, 42, 126, 84}, {126, 42, 168, 84}};
static const int ANIM_C4550[1][4] = {{0, 148, 33, 181}}; /* turbo exhaust, DIVSPR */

static const struct {
    int32_t addr;
    const int (*frames)[4];
    int n;
} ANIMS[] = {
    {0xc4110, ANIM_C4110, 8}, {0xc4230, ANIM_C4230, 8}, {0xc43c0, ANIM_C43C0, 12}, {0xc42c0, ANIM_C42C0, 16},
    {0xc4060, ANIM_C4060, 12}, {0xc41a0, ANIM_C41A0, 9}, {0xc44c0, ANIM_C44C0, 4}, {0xc4500, ANIM_C4500, 4},
    {0xc4550, ANIM_C4550, 1}, {0xc4010, ANIM_C4010, 5}, {0xc4030, ANIM_C4010 + 2, 3}, {0xc4050, ANIM_C4010 + 4, 1},
};

/* Frame `f` of table `addr` (entries past the known ones clamp to the last). */
static const int *anim_frame(int32_t addr, int f) {
    for (size_t i = 0; i < sizeof ANIMS / sizeof ANIMS[0]; i++)
        if (ANIMS[i].addr == addr) {
            if (f < 0) f = 0;
            if (f >= ANIMS[i].n) f = ANIMS[i].n - 1;
            return ANIMS[i].frames[f];
        }
    return ANIM_C4110[0];
}

const int *ign_fx_uv(int32_t table, int frame) { return anim_frame(table, frame); }

void ign_fx_init(IgnFx *fx) {
    memset(fx, 0, sizeof *fx);
    fx->seed = 1;
    for (int i = 0; i < 16; i++) fx->wreck_frame_done[i] = -1;
}

static unsigned fx_rand_next(IgnFx *fx) {
    fx->seed = fx->seed * 1103515245u + 12345u;
    return fx->seed >> 16;
}

double ign_fx_rand(IgnFx *fx) {
    return (double)(fx_rand_next(fx) & 0x3fff) * (1.0 / 16384.0);
}

unsigned ign_fx_rand15(IgnFx *fx) {
    return fx_rand_next(fx) & 0x7fff;
}

int ign_fx_add(IgnFx *fx, const int32_t tpl[IGN_FX_WORDS], float focus_x, float focus_z) {
    int type = tpl[0];
    if ((type == 1 || type == 2 || type == 8) &&
        (fabsf(focus_x - (float)(tpl[1] / 1024)) >= 3500.0f || fabsf(focus_z - (float)(tpl[3] / 1024)) >= 3500.0f))
        return -1;
    int slot = -1;
    for (int i = 0; i < IGN_FX_SLOTS; i++)
        if (!fx->r[i][0]) {
            slot = i;
            break;
        }
    if (slot < 0) {
        int low = 100;
        for (int i = 0; i < IGN_FX_SLOTS; i++) {
            int p = PRIORITY[fx->r[i][0] % 12];
            if (p < low) low = p, slot = i;
        }
        if (slot < 0 || low >= PRIORITY[type % 12]) return -1;
    }
    memcpy(fx->r[slot], tpl, sizeof fx->r[slot]);
    return slot;
}

#define FIX(v) ((int32_t)((v) * 1024.0))

/* Type 1 template; position and velocity in 22.10 fixed point, angles in tenths of a degree. */
static int sprite(IgnFx *fx, const int32_t pos[3], int32_t vx, int32_t vy, int32_t vz, int32_t drag, int32_t grav,
                  int slot, int32_t frame, int32_t speed, int32_t size, int32_t growth, int32_t anim, int ox, int oy,
                  int oz, const int32_t ang[3], int life, float focus_x, float focus_z) {
    int32_t t[IGN_FX_WORDS] = {IGN_FX_SPRITE, pos[0], pos[1], pos[2], vx, vy, vz, drag, grav, slot, frame, speed,
                               size, growth, anim, ox, oy, oz};
    if (ang) t[18] = ang[0], t[19] = ang[1], t[20] = ang[2];
    t[22] = life;
    return ign_fx_add(fx, t, focus_x, focus_z);
}

void ign_fx_sound(IgnFx *fx, int group, int sub, int idx, int vol, int freq, float x, float z) {
    if (fx->nsounds < IGN_FX_MAX_SOUNDS) fx->sounds[fx->nsounds++] = (IgnSoundEvent){group, sub, idx, vol, freq, x, z};
}

/* sub_00039d1c after drawing: move, damp x/z, fall, animate, grow, expire. */
static void step_sprite(int32_t *r) {
    r[1] += r[4], r[2] += r[5], r[3] += r[6];
    r[4] = (int32_t)((int64_t)r[4] * r[7] / 1024);
    r[6] = (int32_t)((int64_t)r[6] * r[7] / 1024);
    r[5] += r[8];
    r[10] += r[11];
    r[12] += r[13];
    if (++r[21] >= r[22]) r[0] = 0;
}

/* sub_00039fe8: as type 1 with the damping in floating point; age +0x3c, life +0x40. */
static void step_blast(int32_t *r) {
    r[1] += r[4], r[2] += r[5], r[3] += r[6];
    r[4] = (int32_t)(r[4] * (r[7] * (1.0 / 1024.0)));
    r[6] = (int32_t)(r[6] * (r[7] * (1.0 / 1024.0)));
    r[5] += r[8];
    r[10] += r[11];
    r[12] += r[13];
    if (++r[15] >= r[16]) r[0] = 0;
}

/* sub_0003b744: debris; kind 0 trails smoke of texture slot +0x40 + 4, kind 1 EXSMOKE. */
static void step_piece(IgnFx *fx, int i, float focus_x, float focus_z) {
    int32_t *r = fx->r[i];
    if (r[18] == 0) {
        int32_t *v = fx->piece[i];
        v[0] = (int32_t)(ign_fx_rand(fx) * 5.0 + 8.0), v[1] = 0, v[2] = 0;
        v[3] = (int32_t)(ign_fx_rand(fx) * -5.0 - 4.0), v[4] = 0, v[5] = (int32_t)(ign_fx_rand(fx) * -5.0 - 4.0);
        v[6] = (int32_t)(ign_fx_rand(fx) * -5.0 - 4.0), v[7] = 0, v[8] = (int32_t)(ign_fx_rand(fx) * 5.0 + 4.0);
    }
    const int32_t pos[3] = {r[1], r[2], r[3]};
    if (r[17] == 0 && ign_fx_rand(fx) > 0.85)
        sprite(fx, pos, 0, (int32_t)(ign_fx_rand(fx) + 1.0), 0, 0x400, 0, r[16] + 4, 0, 0x199, 0xc800, 0x800,
               0xc4230, 0, 0, 0, NULL, 0x13, focus_x, focus_z);
    else if (r[17] == 1 && ign_fx_rand(fx) > 0.85 &&
             sprite(fx, pos, 0, FIX(ign_fx_rand(fx) + 1.0), 0, 0x400, 0, 7, 0, 0x100, 0xc800, 0x800, 0xc43c0, 0, 0, 0,
                    NULL, 0x1c, focus_x, focus_z) >= 0 &&
             ign_fx_rand(fx) * 24.0 < 2.0)
        ign_fx_sound(fx, 0, 2, 8, 0x4000, 0, (float)(pos[0] / 1024), (float)(pos[2] / 1024)); /* 0x3bba4: COLL 08_TINYE */
    r = fx->r[i];
    for (int k = 0; k < 3; k++) {
        r[9 + k] += r[12 + k];
        if (r[9 + k] >= 0x384000) r[9 + k] -= 0x384000;
        if (r[9 + k] < 0) r[9 + k] += 0x384000;
    }
    r[1] += r[4], r[2] += r[5], r[3] += r[6];
    r[4] = (int32_t)(r[4] * (r[7] * (1.0 / 1024.0)));
    r[6] = (int32_t)(r[6] * (r[7] * (1.0 / 1024.0)));
    r[5] += r[8];
    if (++r[18] >= r[19]) r[0] = 0;
}

/* sub_0003a2b4 after drawing: move, damp x/z, fall, fade the colour, expire (age +0x30, life +0x34). */
static void step_plot(int32_t *r) {
    double k = r[7] * (1.0 / 1024.0);
    r[1] += r[4], r[2] += r[5], r[3] += r[6];
    r[4] = (int32_t)(r[4] * k);
    r[6] = (int32_t)(r[6] * k);
    r[5] += r[8];
    r[9] += r[10];
    if (++r[12] >= r[13]) r[0] = 0;
}

/* sub_0003a498: as type 2 plus angles +0x30..+0x38 turning by +0x3c..+0x44; age +0x48, life +0x4c. */
static void step_superplot(int32_t *r) {
    double k = r[7] * (1.0 / 1024.0);
    r[1] += r[4], r[2] += r[5], r[3] += r[6];
    r[4] = (int32_t)(r[4] * k);
    r[6] = (int32_t)(r[6] * k);
    r[5] += r[8];
    r[9] += r[10];
    for (int a = 0; a < 3; a++) r[12 + a] += r[15 + a];
    if (++r[18] >= r[19]) r[0] = 0;
}

/* sub_0003bdac: at age 0 eight sparks and four DARKSMOK puffs; each frame the bolt turns at random (+0x10 rand*50,
 * +0x14 rand*3599) and, with one camera, the palette flashes white 0.4, 0.8, 0.4, back, then dark blue 0.5. */
static void step_lightning(IgnFx *fx, int i, float focus_x, float focus_z) {
    int32_t *r = fx->r[i];
    const int32_t pos[3] = {r[1], r[2], r[3]};
    if (r[5] == 0) {
        for (int k = 0; k < 8; k++) {
            int32_t s[IGN_FX_WORDS] = {IGN_FX_PLOT, pos[0], pos[1], pos[2]};
            s[4] = FIX(ign_fx_rand(fx) * 4.0 - 4.0);
            s[5] = FIX(ign_fx_rand(fx) * 3.0 + 3.0);
            s[6] = FIX(ign_fx_rand(fx) * 4.0 - 4.0);
            s[7] = 0x3e1, s[8] = -153, s[9] = FIX(15.0 - ign_fx_rand(fx) * 5.0), s[10] = -614, s[11] = 1, s[13] = 15;
            ign_fx_add(fx, s, focus_x, focus_z);
        }
        for (int k = 0; k < 4; k++)
            sprite(fx, pos, 0, FIX(ign_fx_rand(fx) * 3.0 + 1.0), 0, 0x400, 0, 5, 0, 0x99, 0x19000, 0x1000, 0xc4230, 0, 0,
                   0, NULL, 50, focus_x, focus_z);
        r = fx->r[i];
    }
    r[7] = (int32_t)(ign_fx_rand(fx) * 50.0);
    r[8] = (int32_t)(ign_fx_rand(fx) * 3599.0);
    static const double FLASH[6] = {0.4, 0.8, 0.4, 0.0, 0.5, 0.5};
    if (r[5] < 4) fx->tint[0] = fx->tint[1] = fx->tint[2] = 0xff;
    else fx->tint[0] = fx->tint[1] = 0x22, fx->tint[2] = 0x31;
    if (r[5] < 6) fx->tint_frac = FLASH[r[5]];
    if (++r[5] >= r[6]) r[0] = 0;
}

void ign_fx_palette(const IgnFx *fx, const IgnPalette *in, IgnPalette *out) {
    *out = *in;
    if (fx->tint_frac == 0) return;
    for (int c = 0; c < 256; c++)
        for (int k = 0; k < 3; k++)
            out->rgb[c][k] = (uint8_t)(in->rgb[c][k] + (int)((fx->tint[k] - in->rgb[c][k]) * fx->tint_frac));
}

/* sub_0003d808: frame advanced before drawing; age +0x50, life +0x54. */
static void step_splash(int32_t *r) {
    r[18] += r[19];
    if (++r[20] >= r[21]) r[0] = 0;
}

/* sub_0003e6ec: angles in degrees x1024 wrap at 360. */
static void step_sparkle(int32_t *r) {
    for (int k = 0; k < 3; k++) {
        r[2 + k] += r[5 + k];
        if (r[2 + k] > 0x59c00) r[2 + k] -= 0x5a000;
        if (r[2 + k] < 0) r[2 + k] += 0x5a000;
    }
    if (++r[8] >= r[9]) r[0] = 0;
}

void ign_fx_frame(IgnFx *fx, IgnCar *const *cars, int ncars, float focus_x, float focus_z) {
    (void)cars;
    (void)ncars;
    for (int i = 0; i < IGN_FX_SLOTS; i++) {
        int32_t *r = fx->r[i];
        switch (r[0]) {
        case IGN_FX_SPRITE: step_sprite(r); break;
        case IGN_FX_PLOT: step_plot(r); break;
        case IGN_FX_SUPERPLOT: step_superplot(r); break;
        case IGN_FX_BLAST: step_blast(r); break;
        case IGN_FX_PIECE: step_piece(fx, i, focus_x, focus_z); break;
        case IGN_FX_LIGHTNING: step_lightning(fx, i, focus_x, focus_z); break;
        case IGN_FX_SPLASH: step_splash(r); break;
        case IGN_FX_SPARKLE: step_sparkle(r); break;
        default: r[0] = 0; break;
        }
    }
}

/* 0x3c2a0: the twelve quads of the sinking splash, v0..v3 (mesh y down), at the car position. */
static const int16_t SPLASH[12][12] = {
    {-60, 0, -40, -60, -300, 0, 60, -300, 0, 60, 0, -40},   {60, 0, -40, 60, -300, 0, -60, -300, 0, -60, 0, -40},
    {-40, 0, 60, 0, -300, 60, 0, -300, -60, -40, 0, -60},   {-40, 0, -60, 0, -300, -60, 0, -300, 60, -40, 0, 60},
    {-60, 0, 40, -60, -300, 0, 60, -300, 0, 60, 0, 40},     {60, 0, 40, 60, -300, 0, -60, -300, 0, -60, 0, 40},
    {40, 0, 60, 0, -300, 60, 0, -300, -60, 40, 0, -60},     {40, 0, -60, 0, -300, -60, 0, -300, 60, 40, 0, 60},
    {-60, 10, 0, -60, 10, 60, 60, 10, 60, 60, 10, 0},       {60, 10, 0, 60, 10, 60, -60, 10, 60, -60, 10, 0},
    {-60, 10, -60, -60, 10, 0, 60, 10, 0, 60, 10, -60},     {60, 10, -60, 60, 10, 0, -60, 10, 0, -60, 10, -60},
};

static void wreck_frame(IgnFx *fx, const IgnCar *car, int index, int frame, float focus_x, float focus_z) {
    const int total = (int)IGN_WRECK_FRAMES;
    /* Effect origin: car_vertical / car_update pass the rear axle point (car+0x48/+0x50) for an explosion and the
     * car position for a sink. */
    const int32_t pos[3] = {FIX(car->wreck_at[0]), FIX(car->wreck_at[1]), FIX(car->wreck_at[2])};
    if (car->wreck_kind == 3) {
        /* sub_0003db4c: no effect of its own besides the sparkles. */
    } else if (car->wreck_kind == 2) {
        if (frame == 0) ign_fx_sound(fx, 0, 2, 5, 0x10000, 22000, (float)car->wreck_at[0], (float)car->wreck_at[2]);
        if (frame == 0)
            for (int q = 0; q < 12; q++) {
                int32_t t[IGN_FX_WORDS] = {IGN_FX_SPLASH};
                for (int k = 0; k < 12; k++) t[1 + k] = SPLASH[q][k];
                t[13] = 0xc4060, t[14] = 3;
                t[15] = pos[0], t[16] = pos[1], t[17] = pos[2];
                t[19] = 0x166, t[21] = 31;
                ign_fx_add(fx, t, focus_x, focus_z);
            }
    } else {
        if (frame == 0) {
            int32_t b[IGN_FX_WORDS] = {IGN_FX_BLAST, pos[0], pos[1], pos[2], 0, 0, 0, 0x400, 0, 6, 0, 0x266, 0x89800,
                                       0, 0xc42c0, 0, 0x1a};
            ign_fx_add(fx, b, focus_x, focus_z);
            ign_fx_sound(fx, 0, 2, 4, 0xe000, 22000, (float)car->wreck_at[0], (float)car->wreck_at[2]);
            for (int k = 0; k < 15; k++) {
                int32_t p[IGN_FX_WORDS] = {IGN_FX_PIECE, pos[0], pos[1], pos[2]};
                p[4] = FIX(ign_fx_rand(fx) * 12.0 - 6.0);
                p[5] = FIX(ign_fx_rand(fx) * 5.0 + 5.0);
                p[6] = FIX(ign_fx_rand(fx) * 12.0 - 6.0);
                p[7] = 0x400, p[8] = -358;
                for (int a = 0; a < 3; a++) p[12 + a] = FIX(ign_fx_rand(fx) * 100.0 - 50.0);
                p[15] = 0, p[16] = 1, p[17] = 1, p[19] = 60;
                ign_fx_add(fx, p, focus_x, focus_z);
            }
        }
        if ((frame & 1) && frame <= 20) {
            int32_t b[IGN_FX_WORDS] = {IGN_FX_BLAST, pos[0] + FIX(ign_fx_rand(fx) * 100.0 - 50.0),
                                       pos[1] + FIX(ign_fx_rand(fx) * 20.0), pos[2] + FIX(ign_fx_rand(fx) * 100.0 - 50.0),
                                       0, 0, 0, 0x400, 0, 6, 0, 0x266, FIX(ign_fx_rand(fx) * 300.0 + 250.0), 0, 0xc42c0,
                                       0, 0x1a};
            ign_fx_add(fx, b, focus_x, focus_z);
        }
        if (ign_fx_rand(fx) > frame * 0.0025 + 0.8) {
            /* 0x3ad3b: the +-20 jitter is added to the fixed-point position, so it is a fraction of a unit. */
            const int32_t at[3] = {(int32_t)(ign_fx_rand(fx) * 40.0 + pos[0] - 20.0), pos[1],
                                   (int32_t)(ign_fx_rand(fx) * 40.0 + pos[2] - 20.0)};
            int32_t vx = FIX(ign_fx_rand(fx) * 2.0 - 1.0), vy = FIX(ign_fx_rand(fx) * 4.0 + 2.0);
            int32_t vz = FIX(ign_fx_rand(fx) * 2.0 - 1.0);
            sprite(fx, at, vx, vy, vz, 0x400, 0, 5, 0, 0x66, 0x4b000, 0x800, 0xc4230, 0, 0, 0, NULL, 0x4f, focus_x,
                   focus_z);
        }
    }
    /* Both controllers: ten sparkles around the car 20 frames before it is put back. */
    if (frame == total - 20) {
        for (int k = 0; k < 10; k++) {
            int32_t s[IGN_FX_WORDS] = {IGN_FX_SPARKLE, index, 0, FIX(ign_fx_rand(fx) * 359.0),
                                       FIX(ign_fx_rand(fx) * 359.0), 0, 0, 0x2800, 0, 60};
            ign_fx_add(fx, s, focus_x, focus_z);
        }
        ign_fx_sound(fx, 0, 4, 3, 0xe000, 0, car->x, car->z); /* 0x3afec: DIV 03_START */
    }
}

void ign_fx_wreck(IgnFx *fx, const IgnCar *car, int index, int frame, float focus_x, float focus_z) {
    if (index < 0 || index >= 16) return;
    /* A new wreck restarts at frame 0; frames the caller stepped over (the clock advances first) are caught up. */
    if (fx->wreck_seen[index] != car->wrecks) fx->wreck_seen[index] = car->wrecks, fx->wreck_frame_done[index] = -1;
    while (fx->wreck_frame_done[index] < frame)
        wreck_frame(fx, car, index, ++fx->wreck_frame_done[index], focus_x, focus_z);
}

void ign_fx_turbo(IgnFx *fx, IgnCar *car, float focus_x, float focus_z) {
    const double tenths = 1800.0 / PI;
    const int32_t pos[3] = {FIX(car->x), FIX(car->height), FIX(car->z)};
    const int32_t ang[3] = {(int32_t)(car->body.roll * tenths), (int32_t)(car->body.heading * tenths),
                            (int32_t)(car->body.pitch * tenths)};
    int rear = (int)car->body_min_x;
    for (int k = 0; k < 2; k++) {
        int32_t vx = (int32_t)(car->step_x * 0.5 * 1024.0 + ign_fx_rand(fx) * 1024.0 - 512.0);
        int32_t vz = (int32_t)(car->step_z * 0.5 * 1024.0 + ign_fx_rand(fx) * 1024.0 - 512.0);
        int ox = (int)(rear + ign_fx_rand(fx) * 10.0), oz = (int)(ign_fx_rand(fx) * 30.0 - 15.0);
        sprite(fx, pos, vx, 0x400, vz, 0x400, 0, 2, 0, 0, 0x25800, 0x400, 0xc4550, ox, 0, oz, ang, 26, focus_x,
               focus_z);
    }
    car->turbo_frames++;
    if (car->turbo_mode == 2 && (car->turbo_frames & 0xa) == 0xa)
        for (int k = 0; k < 10; k++) {
            int32_t vx = FIX(ign_fx_rand(fx) * 4.0 - 2.0), vy = FIX(ign_fx_rand(fx));
            int32_t vz = FIX(ign_fx_rand(fx) * 4.0 - 2.0);
            int ox = (int)(rear + ign_fx_rand(fx) * 20.0), oz = (int)(ign_fx_rand(fx) * 60.0 - 30.0);
            sprite(fx, pos, vx, vy, vz, 0x400, 0, 4, 0, 0x99, 0x32000, 0x800, 0xc41a0, ox, 0, oz, ang, 50, focus_x,
                   focus_z);
        }
    if (car->spec == 3) {
        /* Twin exhaust flame, alternating sides every 8 frames. */
        int side = car->turbo_frames & 8;
        int f = (int)(fmod(car->turbo_frames, 8.0) * 0.5);
        sprite(fx, pos, 0, 0, 0, 0x400, 0, 2, FIX(f), 0, 0x4b000, 0, side ? 0xc4500 : 0xc44c0, 0, -25,
               side ? 12 : -12, ang, 2, focus_x, focus_z);
    } else if (car->spec == 8) {
        /* Exhaust stacks: four DARKSMOK puffs, each with probability turbo charge / 100. */
        static const int SIDE[4] = {22, -22, 22, -22};
        for (int k = 0; k < 4; k++) {
            if (!(car->turbo_charge > ign_fx_rand(fx) * 100.0)) continue;
            int32_t vx = (int32_t)(car->step_x * 0.9 * 1024.0 + (ign_fx_rand(fx) - 0.5) * 1024.0);
            int32_t vz = (int32_t)(car->step_z * 0.9 * 1024.0 + (ign_fx_rand(fx) - 0.5) * 1024.0);
            sprite(fx, pos, vx, 0x400, vz, 0x400, 0, 5, 0x1000, 0x3d, 0x19000, 0x1000, 0xc4230, -25, -50, SIDE[k],
                   ang, 50, focus_x, focus_z);
        }
    }
}

void ign_fx_turbo_start(IgnFx *fx, const IgnCar *car, float focus_x, float focus_z) {
    const int32_t pos[3] = {FIX(car->x), FIX(car->height), FIX(car->z)};
    /* The original stores these angles as radians x 1024 although the sprite reads tenths of a degree. */
    const int32_t ang[3] = {FIX(car->body.roll), FIX(car->body.heading), FIX(car->body.pitch)};
    for (int k = 0; k < 10; k++) {
        int32_t vx = FIX(ign_fx_rand(fx) * 4.0 - 2.0), vy = FIX(ign_fx_rand(fx));
        int32_t vz = FIX(ign_fx_rand(fx) * 4.0 - 2.0);
        int ox = (int)(car->body_min_x + ign_fx_rand(fx) * 20.0), oz = (int)(ign_fx_rand(fx) * 60.0 - 30.0);
        sprite(fx, pos, vx, vy, vz, 0x400, 0, 4, 0, 0x99, 0x32000, 0x800, 0xc41a0, ox, 0, oz, ang, 50, focus_x,
               focus_z);
    }
}

void ign_fx_wall_sparks(IgnFx *fx, IgnCar *car, int human, float focus_x, float focus_z) {
    int wall = 0;
    for (int w = 0; w < 4; w++) wall |= car->wheel_type[w] == 0x5a;
    if (wall && car->kmh > 100 && !car->airborne) {
        car->spark_timer = 6;
        memcpy(car->spark_types, car->wheel_type, sizeof car->spark_types);
    }
    if (car->spark_timer > 0) {
        /* car+0x2fc/+0x31c corners; the jitter moves the rear corners (0, 1) forward and the front ones back. The
         * last corner on 0x5a wins. */
        int jitter = (int)(ign_fx_rand(fx) * 20.0);
        double ch = cos(car->body.heading), sh = sin(car->body.heading), px = 0, pz = 0;
        for (int w = 0; w < 4; w++) {
            if (car->spark_types[w] != 0x5a) continue;
            double x = car->corner_x[w] + (w < 2 ? jitter : -jitter), z = car->corner_z[w];
            px = car->x + x * ch - z * sh;
            pz = car->z + z * ch + x * sh;
        }
        for (int k = 0, n = human ? 2 : 1; k < n; k++) {
            double rx = ign_fx_rand(fx) * 2.0 - 1.0, rz = ign_fx_rand(fx) * 2.0 - 1.0;
            int32_t t[IGN_FX_WORDS] = {IGN_FX_PLOT, FIX(px), FIX(car->height), FIX(pz),
                                       FIX(ch * car->kmh * 0.1 + rx), 0x4cc, FIX(sh * car->kmh * 0.1 + rz), 0x3e1,
                                       -153, FIX(15.0 - ign_fx_rand(fx) * 5.0), -614, 1, 0, 15};
            ign_fx_add(fx, t, focus_x, focus_z);
        }
    }
    if (car->spark_timer > 0) car->spark_timer--;
}

void ign_fx_wheel_sparks(IgnFx *fx, const IgnCar *car, float focus_x, float focus_z) {
    if (!(car->kmh > 5) || car->airborne) return;
    const float along[4] = {car->front, car->front, car->rear, car->rear};
    const float side[4] = {car->half_width, -car->half_width, car->half_width, -car->half_width};
    double ch = cos(car->body.heading), sh = sin(car->body.heading);
    for (int w = 0; w < 4; w++) {
        int t = car->wheel_type[w];
        if (t != 6 && t != 0x10 && t != 0x1a) continue;
        double px = car->x + along[w] * ch - side[w] * sh, pz = car->z + along[w] * sh + side[w] * ch;
        double up = 1.0 + car->kmh / 15.0;
        for (int k = 0; k < 2; k++) {
            double vx = car->step_x * 0.2857 + ign_fx_rand(fx) * 4.0 - 2.0;
            double vz = car->step_z * 0.2857 + ign_fx_rand(fx) * 4.0 - 2.0;
            int32_t s[IGN_FX_WORDS] = {IGN_FX_SUPERPLOT, FIX(px), FIX(car->height), FIX(pz), FIX(vx), FIX(up),
                                       FIX(vz), 0x3d7, -153, 0x1ec00, -32, 1};
            for (int a = 0; a < 3; a++) s[15 + a] = (int32_t)(ign_fx_rand(fx) * 100.0);
            s[19] = 60;
            ign_fx_add(fx, s, focus_x, focus_z);
        }
    }
}

void ign_fx_impact(IgnFx *fx, const IgnCar *a, const IgnCar *b, double cx, double cz, float focus_x,
                   float focus_z) {
    const int32_t pos[3] = {FIX(cx), FIX((a->height + b->height) * 0.5), FIX(cz)};
    double sx = (a->step_x + b->step_x) * 0.5, sz = (a->step_z + b->step_z) * 0.5;
    for (int k = 0; k < 6; k++) {
        int32_t p[IGN_FX_WORDS] = {IGN_FX_PIECE, pos[0], pos[1], pos[2]};
        p[4] = FIX(sx + ign_fx_rand(fx) * 6.0 - 3.0);
        p[5] = FIX(ign_fx_rand(fx) * 3.0 + 3.0);
        p[6] = FIX(sz + ign_fx_rand(fx) * 6.0 - 3.0);
        p[7] = 0x400, p[8] = -614;
        for (int n = 0; n < 3; n++) p[12 + n] = FIX(ign_fx_rand(fx) * 100.0 - 50.0);
        p[15] = 0, p[16] = 1, p[17] = 0, p[19] = 15;
        ign_fx_add(fx, p, focus_x, focus_z);
    }
}

void ign_fx_landing_dust(IgnFx *fx, const IgnCar *car, float focus_x, float focus_z) {
    /* 0xc3558: surfaces that raise dust (0x3f3a3 reads car+0x158, else car+0x15c, else type 0). */
    static const unsigned char DUSTY[40] = {0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0,
                                            0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    int t = car->wheel_type[2];
    if (t < 0 || t > 0x27) t = car->wheel_type[3];
    if (t < 0 || t > 0x27) t = 0;
    if (!DUSTY[t]) return;
    double h = car->body.heading, fx_ = cos(h), fz = sin(h), sx = cos(h + PI / 2), sz = sin(h + PI / 2);
    const float along[2] = {car->front, car->rear};
    for (int a = 0; a < 2; a++)
        for (int side = 1; side >= -1; side -= 2) {
            double w = side * car->half_width;
            const int32_t pos[3] = {FIX(car->x + fx_ * along[a] + sx * w), FIX(car->height),
                                    FIX(car->z + fz * along[a] + sz * w)};
            int32_t vx = FIX(ign_fx_rand(fx) * 2.0 - 1.0), vz = FIX(ign_fx_rand(fx) * 2.0 - 1.0);
            int32_t size = FIX(ign_fx_rand(fx) * 50.0 + 50.0), grow = FIX(ign_fx_rand(fx) * 2.0 + 2.0);
            sprite(fx, pos, vx, -0x400, vz, 0x400, 0, 4, 0, 0xcc, size, grow, 0xc4110, 0, 0, 0, NULL, 40, focus_x,
                   focus_z);
        }
}

void ign_fx_emitters_init(IgnFx *fx, const IgnLevel *lv) {
    fx->nemit = 0;
    for (int i = 0; i < lv->track.nobj && fx->nemit < IGN_FX_MAX_EMITTERS; i++) {
        uint32_t t = lv->track.objs[i].type;
        if (t == 0x12e || t == 0x130 || t == 0x131 || (t >= 0x15e && t < 0x168))
            fx->emit[fx->nemit++] = (IgnEmitter){i, (int)t, 0, 0};
    }
}

/* Engine object position (x, y up, z) truncated, plus an offset, in 22.10. */
static void emitter_pos(const IgnObject *o, double dx, double dy, double dz, int32_t out[3]) {
    out[0] = (int32_t)trunc(o->pos[0] + dx) * 1024;
    out[1] = (int32_t)trunc(-o->pos[1] + dy) * 1024;
    out[2] = (int32_t)trunc(o->pos[2] + dz) * 1024;
}

void ign_fx_emitters(IgnFx *fx, const IgnLevel *lv, float focus_x, float focus_z) {
    for (int i = 0; i < fx->nemit; i++) {
        IgnEmitter *e = &fx->emit[i];
        const IgnObject *o = &lv->track.objs[e->obj];
        int32_t pos[3];
        switch (e->type) {
        case 0x15e: case 0x15f: /* 0x363c9: SMOKE (0x15e) or DARKSMOK (0x15f) every 2-5 frames */
        case 0x164: case 0x130: { /* 0x36658: nitro smoke of SMOKE.TEX */
            double wait = ign_fx_rand(fx) * 3.0 + 2.0;
            if (wait >= ++e->count) break;
            int nitro = e->type == 0x164 || e->type == 0x130;
            emitter_pos(o, 0, 0, 0, pos);
            int32_t vx = FIX(ign_fx_rand(fx) - 0.5);
            int32_t vy = FIX(ign_fx_rand(fx) * 2.0 + (nitro ? 1.0 : 2.0));
            int32_t vz = FIX(ign_fx_rand(fx) - 0.5);
            int32_t size = FIX(ign_fx_rand(fx) * 100.0 + 100.0), grow = FIX(ign_fx_rand(fx) * 2.0 + 2.0);
            sprite(fx, pos, vx, vy, vz, 0x400, 0, e->type == 0x15f ? 5 : 4, 0, 0xa3, size, grow,
                   nitro ? 0xc41a0 : 0xc4110, 0, 0, 0, NULL, 50, focus_x, focus_z);
            e->count = 0;
            break;
        }
        case 0x161: { /* 0x36824: smoke blown towards -z, slowly falling, every 3-7 frames */
            double wait = ign_fx_rand(fx) * 4.0 + 3.0;
            if (wait >= ++e->count) break;
            emitter_pos(o, 0, 0, 0, pos);
            int32_t vx = FIX(ign_fx_rand(fx) * 2.0), vy = FIX(ign_fx_rand(fx) + 3.0);
            int32_t vz = FIX(ign_fx_rand(fx) * 2.0 - 9.0);
            int32_t size = FIX(ign_fx_rand(fx) * 100.0 + 100.0), grow = FIX(ign_fx_rand(fx) * 2.0 + 2.0);
            sprite(fx, pos, vx, vy, vz, 0x3f5, -102, 4, 0xc00, 0x33, size, grow, 0xc41a0, 0, 0, 0, NULL, 100, focus_x,
                   focus_z);
            e->count = 0;
            break;
        }
        case 0x166: case 0x167: { /* 0x369cc / 0x36b1c: slow, long-lived columns (mist, DARKSMOK) */
            int mist = e->type == 0x166;
            double wait = mist ? ign_fx_rand(fx) * 10.0 + 10.0 : ign_fx_rand(fx) * 15.0 + 15.0;
            if (wait >= ++e->count) break;
            double dx = mist ? ign_fx_rand(fx) * 40.0 : ign_fx_rand(fx) * 150.0 - 75.0;
            double dy = ign_fx_rand(fx) * 10.0;
            double dz = mist ? ign_fx_rand(fx) * 40.0 : ign_fx_rand(fx) * 150.0 - 75.0;
            pos[0] = FIX(o->pos[0] + dx), pos[1] = FIX(-o->pos[1] + dy), pos[2] = FIX(o->pos[2] + dz);
            int32_t vy = FIX(ign_fx_rand(fx) + (mist ? 2.0 : 1.0));
            int32_t size = FIX(ign_fx_rand(fx) * 150.0 + 150.0), grow = FIX(ign_fx_rand(fx) + 2.0);
            sprite(fx, pos, 0, vy, 0, 0x400, 0, mist ? 4 : 5, 0, mist ? 0x35 : 0x28, size, grow,
                   mist ? 0xc41a0 : 0xc4110, 0, 0, 0, NULL, 150, focus_x, focus_z);
            e->count = 0;
            break;
        }
        case 0x12e: { /* 0x36d0f: smoke when the counter passes 1 + rand, embers 1 frame in 10 */
            ++e->count;
            if (ign_fx_rand(fx) + 1.0 < e->count) {
                double dx = ign_fx_rand(fx) * 150.0 - 75.0, dz = ign_fx_rand(fx) * 150.0 - 75.0;
                emitter_pos(o, dx, 0, dz, pos);
                int32_t vy = FIX(ign_fx_rand(fx) * 0.5 + 0.5);
                int32_t size = FIX(ign_fx_rand(fx) * 150.0 + 150.0), grow = FIX(ign_fx_rand(fx) * 3.0 + 3.0);
                sprite(fx, pos, 0, vy, 0, 0x400, 0, 4, 0, 0x10f, size, grow, 0xc4110, 0, 0, 0, NULL, 30, focus_x,
                       focus_z);
                e->count = 0;
            }
            if ((int)(ign_fx_rand(fx) * 10.0) == 1) {
                double dx = ign_fx_rand(fx) * 100.0 - 50.0, dy = ign_fx_rand(fx) * 100.0 + 200.0;
                double dz = ign_fx_rand(fx) * 100.0 - 50.0;
                int32_t p[IGN_FX_WORDS] = {IGN_FX_PIECE};
                emitter_pos(o, dx, dy, dz, p + 1);
                p[4] = FIX(ign_fx_rand(fx) * 20.0 - 10.0);
                p[5] = FIX(ign_fx_rand(fx) * 5.0 + 5.0);
                p[6] = FIX(ign_fx_rand(fx) * 20.0 - 10.0);
                p[7] = 0x400, p[8] = -460;
                for (int n = 0; n < 3; n++) p[12 + n] = FIX(ign_fx_rand(fx) * 100.0 - 50.0);
                p[15] = (int32_t)(ign_fx_rand(fx) * 7.0 + 4.0), p[19] = 60;
                ign_fx_add(fx, p, focus_x, focus_z);
            }
            break;
        }
        case 0x131: /* 0x370c9: a geyser erupts 41 frames after the last one, 7 puffs over 15 frames */
            if (++e->count > 40 && e->phase == 0) e->phase = 1;
            if (e->phase <= 0) break;
            {
                double dx = ign_fx_rand(fx) * 50.0 - 25.0, dz = ign_fx_rand(fx) * 50.0 - 25.0;
                if (!(e->phase & 1)) {
                    emitter_pos(o, dx, 0, dz, pos);
                    int32_t vx = FIX(ign_fx_rand(fx) * 2.0 - 1.0);
                    ign_fx_rand(fx);
                    int32_t vz = FIX(ign_fx_rand(fx) * 2.0 - 1.0);
                    int32_t size = FIX(ign_fx_rand(fx) * 150.0 + 100.0), grow = FIX(ign_fx_rand(fx) * 3.0 + 3.0);
                    sprite(fx, pos, vx, 0x2800, vz, 0x400, 0, 4, 0, 0x18f, size, grow, 0xc41a0, 0, 0, 0, NULL, 20,
                           focus_x, focus_z);
                }
            }
            if (++e->phase > 15) e->phase = 0, e->count = 0;
            break;
        }
    }
}
