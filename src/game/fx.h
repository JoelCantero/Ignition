#ifndef IGN_FX_H
#define IGN_FX_H

#include <stdint.h>

#include "car.h"

/* Effect slots of the original (0x1d5f10): 200 records of 0x64 bytes, kept as 25 dwords so the handlers can be
 * ported with their own offsets and 22.10 fixed point. Updated once per 1/36 s frame by collisions_pass. */
#define IGN_FX_SLOTS 200
#define IGN_FX_WORDS 25

enum {
    IGN_FX_SPRITE = 1,  /* sub_00039d1c: drifting sprite, optional offset in a car's frame */
    IGN_FX_PLOT = 2,    /* sub_0003a2b4: single point (spark) */
    IGN_FX_BLAST = 4,   /* sub_00039fe8: sprite with float damping */
    IGN_FX_PIECE = 5,   /* sub_0003b744: tumbling debris triangle pair that trails smoke */
    IGN_FX_LIGHTNING = 6, /* sub_0003bdac: bolt mesh 0xbfe90 with a palette flash */
    IGN_FX_SUPERPLOT = 8, /* sub_0003a498: four rotating points (sparks) */
    IGN_FX_SPLASH = 9,  /* sub_0003d808: animated textured quad */
    IGN_FX_SPARKLE = 11 /* sub_0003e6ec: sprite orbiting a car */
};

/* A sub_00049a8c request: bank group/sub, sample, volume 0..0x10000, rate (0 = the sample's), source x/z. */
typedef struct {
    int group, sub, idx, vol, freq;
    float x, z;
} IgnSoundEvent;

#define IGN_FX_MAX_SOUNDS 32
#define IGN_FX_MAX_EMITTERS 64

/* A smoke source of the track: PLC object, its type, frame counter and geyser phase (lists 0x1fefac / 0x1fef58). */
typedef struct {
    int obj, type, count, phase;
} IgnEmitter;

typedef struct {
    int32_t r[IGN_FX_SLOTS][IGN_FX_WORDS];
    int32_t piece[IGN_FX_SLOTS][9]; /* type 5 mesh vertices (0x1aebd8) */
    uint32_t seed;
    int wreck_frame_done[16];      /* last wreck frame handled per car */
    int wreck_seen[16];            /* IgnCar.wrecks when it was handled */
    int tint[3];                   /* sub_00051508: the palette is drawn tint_frac of the way to tint[] */
    double tint_frac;
    IgnSoundEvent sounds[IGN_FX_MAX_SOUNDS]; /* one-shot sounds for the platform to play */
    int nsounds;
    IgnEmitter emit[IGN_FX_MAX_EMITTERS];
    int nemit;
} IgnFx;

void ign_fx_sound(IgnFx *fx, int group, int sub, int idx, int vol, int freq, float x, float z);

void ign_fx_init(IgnFx *fx);
/* sub_00051714: rand() & 0x3fff scaled to [0, 1). */
double ign_fx_rand(IgnFx *fx);
/* sub_00064774: rand() & 0x7fff; shares the game's random state. */
unsigned ign_fx_rand15(IgnFx *fx);
/* sub_0003fee8: copies the template into the first free slot; types 1, 2 and 8 only within 3500 of (fx_x, fx_z).
 * When full, replaces the lowest-priority slot (0xc47f0) if it is below the new type. Returns the slot or -1. */
int ign_fx_add(IgnFx *fx, const int32_t tpl[IGN_FX_WORDS], float focus_x, float focus_z);
/* collisions_pass 0x3fd04: one frame of every effect handler. */
void ign_fx_frame(IgnFx *fx, IgnCar *const *cars, int ncars, float focus_x, float focus_z);
/* sub_0003f37c: four SMOKE.TEX puffs at the wheels when a car lands (car+0x278 == 1) on a dusty surface. */
void ign_fx_landing_dust(IgnFx *fx, const IgnCar *car, float focus_x, float focus_z);
/* World creation 0x24e28 / 0x255cb: the track's smoke sources (PLC types 0x12e, 0x130, 0x131, 0x15e-0x167). */
void ign_fx_emitters_init(IgnFx *fx, const IgnLevel *lv);
/* collisions_pass 0x3fe4e: sub_0003636c (chimney and vent smoke) and sub_00036ce0 (0x12e smoke and embers, 0x131
 * geysers), with every detail option on. */
void ign_fx_emitters(IgnFx *fx, const IgnLevel *lv, float focus_x, float focus_z);
/* sub_0003a788 (kind 1) / sub_0003c208 (kind 2): effects of wreck frame `frame` of car `index`. */
void ign_fx_wreck(IgnFx *fx, const IgnCar *car, int index, int frame, float focus_x, float focus_z);
/* sub_0004e97c: turbo exhaust of one frame. */
void ign_fx_turbo(IgnFx *fx, IgnCar *car, float focus_x, float focus_z);
/* sub_0004e688: ten nitro puffs when a turbo starts. */
void ign_fx_turbo_start(IgnFx *fx, const IgnCar *car, float focus_x, float focus_z);
/* sub_0003ea00: sparks of a wheel scraping a wall (surface 0x5a), two per frame for a human driver. */
void ign_fx_wall_sparks(IgnFx *fx, IgnCar *car, int human, float focus_x, float focus_z);
/* sub_0003ee70: sparks of the wheels on surfaces 6, 0x10 and 0x1a. */
void ign_fx_wheel_sparks(IgnFx *fx, const IgnCar *car, float focus_x, float focus_z);
/* sub_0002ea44: six debris pieces at the contact point (cx, cz) of two colliding cars. */
void ign_fx_impact(IgnFx *fx, const IgnCar *a, const IgnCar *b, double cx, double cz, float focus_x,
                   float focus_z);

/* Entry `frame` of animation table `table` (u0, v0, u1, v1 texels), clamped to the known entries. */
const int *ign_fx_uv(int32_t table, int frame);
/* Palette `in` blended toward the current tint (sub_00051508). */
void ign_fx_palette(const IgnFx *fx, const IgnPalette *in, IgnPalette *out);

#endif
