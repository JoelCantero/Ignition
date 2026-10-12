#include "ai.h"

#include <math.h>
#include <stdlib.h>

#define PI 3.14159265358979

/* Approximation: the original sub_0001aea0 (~1900 instructions) is not ported yet. This driver
 * uses the same inputs (TRI road edges per section, the 0xbfb30 parameters, car_input-style
 * digital controls) but its control law is our own. */

void ign_ai_init(IgnAi *ai, int index) {
    static const IgnAiParams base = {0.8, 0.4, 0.6, 0.75, 0.5, 5.0, 0.6, 0.6, 0.9};
    ai->p = base;
    ai->lane = 0.3f + 0.4f * (float)((index * 37) % 11) / 10.0f;
    ai->stuck = 0;
    ai->reverse = 0;
}

static void gate_point(const IgnTrack *t, int s, int branch_b, float lane, float *x, float *z) {
    s %= t->count;
    if (s < 0) s += t->count;
    const float *g = branch_b && t->has_b[s] ? t->gate_b[s] : t->gate[s];
    *x = g[0] + (g[2] - g[0]) * lane;
    *z = g[1] + (g[3] - g[1]) * lane;
}

static float wrap(float a) {
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}

void ign_ai_drive(IgnAi *ai, const IgnCar *car, const IgnTrack *t, IgnInput *in) {
    *in = (IgnInput){0};
    if (!t->gate || t->count < 2) return;
    int s = car->section == IGN_NO_SECTION ? 0 : abs(car->section);
    int bb = car->section != IGN_NO_SECTION && car->section < 0;
    int ahead = (int)ai->p.aggr + (int)(fabsf(car->kmh) / 40.0f);

    float tx, tz;
    gate_point(t, s + 2, bb, ai->lane, &tx, &tz);
    float want = atan2f(tx - car->x, tz - car->z);
    float err = wrap(want - car->heading);

    /* Curvature over the sight distance: heading change between consecutive gate midpoints. */
    float turn = 0;
    float px, pz, qx, qz;
    gate_point(t, s + 1, bb, 0.5f, &px, &pz);
    float prev = 0;
    for (int k = 2; k <= ahead + 1; k++) {
        gate_point(t, s + k, bb, 0.5f, &qx, &qz);
        float h = atan2f(qx - px, qz - pz);
        if (k > 2) turn += fabsf(wrap(h - prev));
        prev = h;
        px = qx;
        pz = qz;
    }
    float target_kmh = 260.0f * (float)ai->p.acc * (1.0f - (float)ai->p.curveinflu * fminf(turn / (float)(PI / 2), 1.0f));
    if (target_kmh < 60.0f) target_kmh = 60.0f;

    /* Recovery: reverse with opposite lock after moving less than 150 units in 1.5 s. */
    if (ai->reverse > 0) {
        ai->reverse--;
        in->brake = 1;
        in->left = err < 0;
        in->right = err > 0;
        return;
    }
    if (++ai->stuck >= 108) {
        if (hypotf(car->x - ai->sx, car->z - ai->sz) < 150.0f) ai->reverse = 72;
        ai->stuck = 0;
        ai->sx = car->x;
        ai->sz = car->z;
    }

    in->left = err < -0.03f;
    in->right = err > 0.03f;
    if (fabsf(err) > 1.2f && car->kmh > 40.0f)
        in->brake = 1;
    else if (car->kmh > target_kmh + 15.0f)
        in->brake = 1;
    else if (car->kmh < target_kmh)
        in->accel = 1;
    /* Original AI (0x1ca15) only boosts on a full charge, on the ground, with little steering. */
    in->turbo = in->accel && car->turbo_charge >= 100.0 && !car->airborne && fabsf(err) < 0.1f &&
                turn < 0.35f;
}
