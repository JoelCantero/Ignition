#include "skid.h"

#include <math.h>
#include <string.h>

#define HALF_PI 1.5707963267948966

/* 0xc34e0: skid mark palette index per surface type 0..39; -1 leaves no mark and no dust. */
static const int SKID_COLOR[10] = {38, 92, 92, 38, 42, 38, -1, 42, -1, 0};

static int skid_color(int type) {
    return type < 0 || type >= 0x28 ? -1 : SKID_COLOR[type % 10];
}

void ign_skids_init(IgnSkids *s) { memset(s, 0, sizeof *s); }

/* sub_0003731c 0x374c1: one effect of type 1 per tyre (SMOKE.TEX, table 0xc4110). */
static void add_puff(IgnFx *fx, float x, float height, float z, float focus_x, float focus_z) {
    double speed = ign_fx_rand(fx) * 0.4 + 0.2;
    int32_t t[IGN_FX_WORDS] = {IGN_FX_SPRITE, (int32_t)(x * 1024.0f), (int32_t)(height * 1024.0f),
                               (int32_t)(z * 1024.0f)};
    t[4] = (int32_t)(ign_fx_rand(fx) * 2048.0);
    t[5] = 0x400;
    t[6] = (int32_t)(ign_fx_rand(fx) * 2048.0);
    t[7] = 0x200;
    t[9] = 4;
    t[11] = (int32_t)(speed * 1024.0);
    t[12] = (int32_t)((ign_fx_rand(fx) * 100.0 + 50.0) * 1024.0);
    t[13] = (int32_t)((ign_fx_rand(fx) * 4.0 + 4.0) * 1024.0);
    t[14] = 0xc4110;
    t[22] = (int32_t)(8.0 / speed);
    ign_fx_add(fx, t, focus_x, focus_z);
}

void ign_skids_car(IgnSkids *s, IgnFx *effects, IgnCar *car, float focus_x, float focus_z) {
    double h = car->body.heading;
    float fx = (float)cos(h), fz = (float)sin(h);
    float px = (float)cos(h + HALF_PI), pz = (float)sin(h + HALF_PI);
    /* Axle 0 = rear (car+0x48/0x50, colour from car+0x158), 1 = front (car+0x68/0x70, car+0x150). */
    const float along[2] = {car->rear, car->front};
    const int wheel[2] = {2, 0};
    for (int a = 0; a < 2; a++) {
        int t0 = car->wheel_type[wheel[a]], t1 = car->wheel_type[wheel[a] + 1];
        int color = skid_color(t0);
        if (!car->skidding || color < 0) {
            car->mark_valid[a] = 0;
            continue;
        }
        float ax = car->x + fx * along[a], az = car->z + fz * along[a];
        float y = (float)-car->ground;
        float edge[2][2][2];
        for (int k = 0; k < 2; k++) {
            float side = k ? -car->half_width : car->half_width;
            float cx = ax + px * side, cz = az + pz * side;
            edge[k][0][0] = cx + px * car->tyre_half, edge[k][0][1] = cz + pz * car->tyre_half;
            edge[k][1][0] = cx - px * car->tyre_half, edge[k][1][1] = cz - pz * car->tyre_half;
        }
        /* 0x38425: the first skidding frame (car+0x298/+0x29c clear) only starts the sound and records the edges. */
        if (!car->mark_valid[a]) {
            memcpy(car->mark_prev[a], edge, sizeof edge);
            car->mark_at[a][0] = ax, car->mark_at[a][1] = az;
            car->mark_valid[a] = 1;
            continue;
        }
        /* 0x3744e: dust once per 1/36 s frame, not on surfaces 5, 15 and 25, at both tyres (heading +- pi/2). */
        if (!(s->tick & 1) && t0 % 10 != 5 && t1 % 10 != 5) {
            float hy = (float)car->height;
            add_puff(effects, ax + px * car->half_width, hy, az + pz * car->half_width, focus_x, focus_z);
            add_puff(effects, ax - px * car->half_width, hy, az - pz * car->half_width, focus_x, focus_z);
        }
        /* 0x378d9: a new mark every 30 units of travel. */
        float dx = ax - car->mark_at[a][0], dz = az - car->mark_at[a][1];
        if (sqrtf(dx * dx + dz * dz) <= 30.0f) continue;
        IgnSkidMark *m = &s->marks[s->next];
        for (int k = 0; k < 2; k++) {
            m->x[k][0] = car->mark_prev[a][k][0][0], m->z[k][0] = car->mark_prev[a][k][0][1];
            m->x[k][1] = car->mark_prev[a][k][1][0], m->z[k][1] = car->mark_prev[a][k][1][1];
            m->x[k][2] = edge[k][1][0], m->z[k][2] = edge[k][1][1];
            m->x[k][3] = edge[k][0][0], m->z[k][3] = edge[k][0][1];
        }
        m->y = y;
        m->color = (uint8_t)color;
        s->next = (s->next + 1) % IGN_MAX_SKIDS;
        if (s->count < IGN_MAX_SKIDS) s->count++;
        memcpy(car->mark_prev[a], edge, sizeof edge);
        car->mark_at[a][0] = ax, car->mark_at[a][1] = az;
    }
}

void ign_skids_step(IgnSkids *s) { s->tick++; }
