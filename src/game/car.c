#include "car.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "track.h"

#define HALF_PI 1.5707963267948966
#define PI 3.14159265358979323846
/* Rear grip factor per difficulty level, [0xc5470] indexed by [0x1fedd8] (car_update 0x30811). */
static const double difficulty_grip[4] = {1.0, 1.15, 1.3, 1.3};
static int difficulty;

int ign_get_difficulty(void) { return difficulty; }
void ign_set_difficulty(int level) { difficulty = level < 0 ? 0 : level > 3 ? 3 : level; }

static int race_started = 1;
static double level_clock;

void ign_set_race_started(int started) { race_started = started; }
void ign_set_level_clock(double frames) { level_clock = frames; }

/* Table index 11 is not a CARS.PLC model: it is the profile computer cars use for steering, turbo force and revs. */
#define COMPUTER_PROFILE 11

/* sub_00022d08: models of computer cars by difficulty (0xc5520) and their torque bonus (0xc55c0 by 0xc57d0[model]). */
static const int COMPUTER_MODELS[4][10] = {
    {1, 3, 5, 0, 2, 6, 9, 4, 7, 8}, {6, 1, 4, 8, 7, 3, 9, 0, 5, 2}, {9, 6, 1, 3, 5, 4, 7, 0, 8, 2},
    {9, 6, 1, 3, 5, 4, 7, 0, 8, 2}};
static const int BONUS_INDEX[12] = {8, 3, 10, 5, 4, 6, 2, 7, 9, 1, 0, 0};
/* 0xc5670, indexed by difficulty and 0xc57d0[model]. */
static const double AI_REAR_GRIP[4][11] = {
    {1.4, 1.37, 1.4, 1.4, 1.33, 1.37, 1.33, 1.29, 1.29, 1.24, 1.24},
    {1.55, 1.5, 1.55, 1.55, 1.45, 1.5, 1.45, 1.4, 1.4, 1.3, 1.3},
    {1.7, 1.65, 1.7, 1.7, 1.6, 1.65, 1.6, 1.55, 1.55, 1.45, 1.45},
    {1.7, 1.65, 1.7, 1.7, 1.6, 1.65, 1.6, 1.55, 1.55, 1.45, 1.45},
};
static const int TORQUE_BONUS[4][11] = {{0, -8, -3, -6, -9, -6, -4, -7, -13, -7, -16},
                                        {0, -1, 3, 1, -3, -2, 5, -3, -5, 0, -5},
                                        {0, 6, 8, 7, 7, 6, 8, 5, 2, 7, 0},
                                        {0, 9, 10, 9, 10, 8, 12, 7, 7, 10, 4}};

int ign_computer_model(int k, const int *humans, int nhumans, int *spare) {
    const int *row = COMPUTER_MODELS[difficulty];
    int clash = 0;
    for (int i = 0; i < nhumans; i++) clash |= row[k % 10] == humans[i];
    if (!clash) return row[k % 10];
    int e = *spare;
    for (int i = 0; i < nhumans && e < 9; i++)
        if (row[e] == humans[i]) e++, i = -1;
    *spare = e + 1;
    return row[e];
}

void ign_car_set_computer(IgnCar *car) {
    car->ai = 1;
    car->torque_bonus = TORQUE_BONUS[difficulty][BONUS_INDEX[car->spec]];
}

#define VERT_REST 5.0 /* the original rests the car 5 units above the ground */

int ign_first_car_id(const IgnLevel *lv) {    for (int i = 0; i < lv->cars.nobj; i++)
        if (lv->cars.objs[i].type % 10 == 0) return (int)(lv->cars.objs[i].type / 10);
    return -1;
}

void ign_car_progress_phase(IgnCar *car, int count) {
    int a = abs(car->section), q = count / 4;
    /* car_update 0x2fc1c..0x2fd63: one phase transition per sample, including the upper bound. */
    if (car->phase == 0 && a >= q && a < 2 * q) {
        car->phase = 1;
    } else if (car->phase == 1 && a >= 2 * q && a < 3 * q) {
        car->phase = 2;
    } else if (car->phase == 2 && a >= 3 * q && a < 4 * q) {
        car->phase = 3;
    }
}

/* car_update 0x2eea4: one ground probe per body corner (car+0x2fc/+0x31c) scaled by car+0x5cc = 1 + km/h / 220,
 * 50 above the car. With a wheel on a type above 0x13 (car+0x5fc) the probes are repeated at half that distance
 * (0x2fe45). Returns that flag; heights are y down. */
static int probe_corners(IgnCar *car, const IgnLevel *lv, int types[4], double h[4], int found[4], double *scale) {
    const IgnBody *b = &car->body;
    double fx = cos(b->heading), fz = sin(b->heading);
    double cx = b->px * IGN_WORLD_PER_METRE, cz = b->pz * IGN_WORLD_PER_METRE;
    float hint = (float)-car->height;
    int special = 0;
    *scale = 1.0 + car->kmh / 220.0;
    for (int pass = 0; pass < 2; pass++) {
        double s = pass ? *scale * 0.5 : *scale;
        for (int w = 0; w < 4; w++) {
            double ax = car->corner_x[w] * s, az = car->corner_z[w] * s;
            IgnGroundHit hit;
            found[w] = ign_ground_at(lv, (float)(cx + fx * ax - fz * az), (float)(cz + fz * ax + fx * az), hint,
                                     &hit) == 0;
            types[w] = found[w] ? hit.surface_type : -1;
            h[w] = found[w] ? hit.y : -car->ground;
            if (found[w]) car->ground_object = hit.object;
            special |= types[w] > 0x13;
        }
        if (!special) break;
    }
    return special;
}

/* 0x309a7: slope angle asin(clamp(rise / run, +-0.95)) moving at most 0.1 rad per tick; the roll uses 0.6 of it. */
static double slope_angle(double rise, double run, double k, double prev, int limit) {
    double a = run > 0 ? rise / run : 0;
    a = a > 0.95 ? 0.95 : a < -0.95 ? -0.95 : a;
    a = asin(a * k);
    if (limit && a - prev > 0.1) a = prev + 0.1;
    if (limit && a - prev < -0.1) a = prev - 0.1;
    return a;
}

/* Ground under the four body corners: slope (when grounded), surface types (-1 = none), each corner's ground height
 * (up) and the mean height (up positive). `limit` applies the per-tick slope limit. Returns car+0x5fc. */
static int sample_ground(IgnCar *car, const IgnLevel *lv, int types[4], double wheel_up[4], int limit) {
    IgnBody *b = &car->body;
    double h[4], scale;
    int found[4];
    int special = probe_corners(car, lv, types, h, found, &scale);
    /* 0x2f9f8: walls of types 0x51/0x5c/0x6f (when they block) do not count in the mean height (normal probes). */
    double sum = 0;
    int count = 0;
    for (int w = 0; w < 4; w++) {
        wheel_up[w] = found[w] ? -h[w] : 0;
        int above = -h[w] + 20.0 <= car->height;
        int t = types[w];
        int skip = !special && ((t == 0x51 || t == 0x5c) && !above);
        skip |= !special && t == 0x6f && !above && !car->jump_mode;
        if (!skip) sum -= h[w], count++;
    }
    /* car_update 0x30265: 1-3 corners far from the previous ground (or on type >= 0x50) follow it instead. */
    double limit_h = fabs(car->kmh) * 0.5 + 100.0;
    int nout = 0, outlier[4];
    for (int w = 0; w < 4; w++) {
        outlier[w] = fabs(trunc(-h[w] - car->ground)) > limit_h || types[w] >= 0x50;
        nout += outlier[w];
    }
    if (nout > 0 && nout < 4) {
        sum = 0;
        for (int w = 0; w < 4; w++) {
            if (outlier[w]) h[w] = car->fall_speed - car->ground;
            sum -= h[w];
        }
    }
    if (!car->airborne) {
        double run_x = fabs(car->corner_x[0] - car->corner_x[3]) * scale / (special + 1);
        double run_z = fabs(car->corner_z[0] - car->corner_z[1]) * scale / (special + 1);
        /* y points down: a lower front (larger y) gives positive pitch, i.e. downhill acceleration. */
        b->pitch = slope_angle((h[2] + h[3]) * 0.5 - (h[0] + h[1]) * 0.5, run_x, 1.0, b->pitch, limit);
        b->roll = slope_angle((h[1] + h[2]) * 0.5 - (h[0] + h[3]) * 0.5, run_z, 0.6, b->roll, limit);
    } else if (limit && b->pitch < PI / 4) {
        b->pitch += 0.01; /* 0x30d98: car+0x130, the nose drops in the air */
    }
    double ground = count > 0 ? sum / count : -h[0];
    car->level_change = nout == 4;
    car->ground_prev = car->ground;
    car->ground = ground;
    /* 0x30c27: switching between plain surfaces (<= 0x13) and the rest restarts the ground step. */
    if (special != car->special_ground) car->ground_prev = car->ground;
    car->special_ground = special;
    car->x = (float)(b->px * IGN_WORLD_PER_METRE);
    car->z = (float)(b->pz * IGN_WORLD_PER_METRE);
    car->heading = (float)(HALF_PI - b->heading);
    return special;
}

/* car_update 0x2f856-0x2fa9b, per wheel: surfaces 0x50-0x8d raise the wall flag car+0x57c, some only while the car
 * is not 20 units above that wheel's ground; 0x0a-0x13 enter jump mode (car+0x274), 0x00-0x09 leave it. */
static int wall_trigger(IgnCar *car, const int types[4], const double wheel_up[4]) {
    int walls = 0;
    for (int w = 0; w < 4; w++) {
        int t = types[w];
        int above = wheel_up[w] + 20.0 <= car->height;
        if (t < 0x50) {
            if (t >= 0x0a && t < 0x14) car->jump_mode = 1;
            else if (t >= 0 && t < 0x0a && !above) car->jump_mode = 0;
            continue;
        }
        switch (t) {
        case 0x50: case 0x5a: case 0x5b:
            walls = 1;
            break;
        case 0x51: case 0x5c: case 0x5d:
            walls |= !above;
            break;
        case 0x6f:
            walls |= !above && !car->jump_mode;
            break;
        case 0x6e: case 0x78: case 0x79: case 0x82: case 0x83: case 0x8c: case 0x8d:
            walls |= !car->jump_mode;
            break;
        }
    }
    return walls;
}

/* sub_0002e234: gravity, ground following, jumps and landing. Returns 1 on landing; `raw_drop` is the ground step
 * the original tests (car_vertical [esp+8]), before the port's upward limit. */
int ign_car_vertical_step(IgnCar *car, double *raw_drop) {
    double drop = car->ground_prev - car->ground;
    if (fabs(car->kmh) < 50.0 && fabs(drop) > 4.0) drop = 0;
    if (fabs(car->kmh * 0.05) < drop) drop = 0; /* ground falls away faster than we follow: jump */
    *raw_drop = drop;
    /* Deviation: when every corner is an outlier (a level change: a bank of type >= 0x50 or a gap in the port's
     * ground lookup, CARIB sections 1-4) a steep rise is climbed, not followed with its speed, which would launch the
     * car hundreds of units up. */
    if (car->level_change && drop < -(fabs(car->kmh * 0.05) + 1.0)) drop = 0;
    car->fall_speed += 0.2;
    car->height -= car->fall_speed;
    double rest = car->ground + VERT_REST;
    int landed = 0;
    if (car->height < rest) {
        car->height = rest;
        if (!car->airborne) {
            car->fall_speed = drop;
        } else {
            car->fall_speed = car->fall_speed > 8.0 ? -car->fall_speed * 0.2 : car->fall_speed * 0.5;
            if (car->fall_speed < -15.0) car->fall_speed = -15.0;
            car->airborne = 0;
            landed = 1;
        }
    }
    return landed;
}

void ign_car_wreck(IgnCar *car, int kind) {
    if (car->wrecked) return;
    car->wrecked = 1;
    car->wrecks++;
    car->wreck_kind = kind;
    car->wreck_frame = 0;
    car->wreck_slid = 0;
    /* sub_0003a788 frame 0: start pose of the glide, airborne, full turbo. */
    car->wreck_from[0] = car->x;
    car->wreck_from[1] = car->height;
    car->wreck_from[2] = car->z;
    car->wreck_from[3] = car->body.heading;
    if (kind == 2) {
        car->wreck_at[0] = car->x, car->wreck_at[2] = car->z;
    } else {
        car->wreck_at[0] = car->x + cos(car->body.heading) * car->rear;
        car->wreck_at[2] = car->z + sin(car->body.heading) * car->rear;
    }
    car->wreck_at[1] = car->height;
    car->airborne = 1;
    ign_car_wreck_turbo(car);
    car->mark_valid[0] = car->mark_valid[1] = 0;
}

static void wreck_why(IgnCar *car, int kind, const char *why) {
    if (!car->wrecked && getenv("IGN_TRACE"))
        fprintf(stderr, "wreck %d (%s) car %d at %.0f %.0f h=%.0f g=%.0f type=0x%x section %d\n", kind, why, car->id,
                car->x, car->z, car->height, car->ground, car->wheel_type[0], car->section);
    ign_car_wreck(car, kind);
}

/* car_vertical 0x2e397: landing on 0x64, on a jump-only surface in jump mode or on a step up of more than 35 explodes
 * the car; landing on 0x65 or on the other jump-only surfaces sinks it. car+0x170 is the front-left wheel. */
static void landing_check(IgnCar *car, double drop) {
    int t = car->wheel_type[0], jump = car->jump_mode;
    if (t == 0x64 || (jump && ((t >= 0x6e && t <= 0x82) || (t >= 0x14 && t < 0x1e))))
        wreck_why(car, 1, "landing surface");
    else if (drop < -35.0)
        wreck_why(car, 1, "landing step");
    else if (t == 0x65 || (jump && ((t >= 0x82 && t <= 0x96) || (t >= 0x1e && t < 0x28))))
        wreck_why(car, 2, "landing sink");
}

/* sub_00048a14 places body and wheels at car+8: axle 5 units above the ground, so the tyres sink into it. */
static void place(IgnCar *car) {
    car->y = (float)-car->height;
}

int ign_cars_collide(IgnCar *const *cars, int n, IgnCarHit *hits, int max_hits, int *nhits) {
    int count = 0, nh = 0;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            IgnCar *a = cars[i], *b = cars[j];
            if (fabsf(a->x - b->x) >= 200 || fabsf(a->z - b->z) >= 200 || fabsf(a->y - b->y) >= 70)
                continue;
            if (!ign_body_collide_pair(&a->body, &b->body)) continue;
            count++;
            if (hits && nh < max_hits)
                hits[nh++] = (IgnCarHit){i, j, a->body.contact_x * IGN_WORLD_PER_METRE,
                                         a->body.contact_z * IGN_WORLD_PER_METRE,
                                         (a->body.impact + b->body.impact) * 0.5};
        }
    if (nhits) *nhits = nh;
    return count;
}

/* cars_update_all 0x2da32: rear axle car+0x5b0 behind and front axle car+0x5ac ahead of the car along car+0xf8. The
 * integer offsets come from per-model tables of cars_init (0x27594) that are not decoded; the port truncates the
 * wheel offsets of the CARS.PLC model. */
static void axle_points(const IgnCar *car, double out[2][2]) {
    double x = car->body.px * IGN_WORLD_PER_METRE, z = car->body.pz * IGN_WORLD_PER_METRE;
    double c = cos(car->body.heading), s = sin(car->body.heading);
    double rear = trunc(-car->rear), front = trunc(car->front);
    out[0][0] = x - c * rear, out[0][1] = z - s * rear;
    out[1][0] = x + front * c, out[1][1] = z + front * s;
}

static void axle_reset(IgnCar *car) {
    axle_points(car, car->axle);
    memset(car->axle_step, 0, sizeof car->axle_step);
}

/* 0x2da9d: the axle points' steps of this tick. */
static void axle_update(IgnCar *car) {
    double now[2][2];
    axle_points(car, now);
    for (int a = 0; a < 2; a++)
        for (int k = 0; k < 2; k++) car->axle_step[a][k] = now[a][k] - car->axle[a][k], car->axle[a][k] = now[a][k];
}

void ign_car_frame_velocity(IgnCar *car) {
    car->contact_v[0] = (car->axle_step[0][0] + car->axle_step[1][0]) * 0.5;
    car->contact_v[1] = (car->axle_step[0][1] + car->axle_step[1][1]) * 0.5;
}

void ign_car_contact_box(const IgnCar *car, int engine, float out[4][2]) {
    double c = cos(car->body.heading), s = sin(car->body.heading);
    double x = car->body.px * IGN_WORLD_PER_METRE, z = car->body.pz * IGN_WORLD_PER_METRE;
    if (engine) x += IGN_SRF_ORIGIN, z += IGN_SRF_ORIGIN;
    for (int i = 0; i < 4; i++) {
        double cx = car->corner_x[i], cz = car->corner_z[i];
        out[i][0] = (float)(cx * c + x - cz * s);
        out[i][1] = (float)(cz * c + z + cx * s);
    }
}

/* Sign of an x87 comparison against 0 (NaN would give -2 in the original). */
static int sign0(double v) {
    return v > 0 ? 1 : v < 0 ? -1 : 0;
}

int ign_contact_polygons(const float (*a)[2], int na, const float (*b)[2], int nb, float *cx, float *cz,
                         float *theta) {
    float hx[8], hz[8];
    int n = 0;
    for (int i = 0; i < na; i++) {
        const float *p0 = a[i], *p1 = a[i + 1 < na ? i + 1 : 0];
        float ex = p0[0] - p1[0], ez = p0[1] - p1[1];
        for (int j = 0; j < nb; j++) {
            const float *q0 = b[j], *q1 = b[j + 1 < nb ? j + 1 : 0];
            /* 0x34aa0: q0 and q1 on the same side of edge p (signs of float32 products). */
            float px1 = p0[0] - q1[0], pz1 = p0[1] - q1[1];
            int s0 = sign0(((double)p0[1] - q0[1]) * ex - ((double)p0[0] - q0[0]) * ez);
            int s1 = sign0((double)ex * pz1 - (double)px1 * ez);
            if (s0 == s1) continue;
            /* 0x34c3c: p0 and p1 on the same side of edge q. */
            float fx = q0[0] - q1[0], fz = q0[1] - q1[1];
            float qx1 = q0[0] - p1[0], qz1 = q0[1] - p1[1];
            int s2 = sign0((double)fx * ((double)q0[1] - p0[1]) - ((double)q0[0] - p0[0]) * fz);
            int s3 = sign0((double)fx * qz1 - (double)qx1 * fz);
            if (s2 == s3) continue;
            /* 0x34db1: crossing point (float32 denominators; parallel edges are skipped). */
            float exfz = ex * fz, ezfx = ez * fx;
            float den = exfz - ezfx;
            if (den == 0) continue;
            float ix = (float)((((double)p0[1] - q0[1]) * ((double)ex * fx) - (double)fx * ((double)p0[0] * ez) +
                                (double)ex * ((double)q0[0] * fz)) / den);
            den = ezfx - exfz;
            if (den == 0) continue;
            float iz = (float)((((double)p0[0] - q0[0]) * ((double)ez * fz) - (double)fz * ((double)p0[1] * ex) +
                                (double)ez * ((double)q0[1] * fx)) / den);
            if (n >= 8) return 0;
            hx[n] = ix, hz[n] = iz, n++;
        }
    }
    if (n < 1) return 0;
    double sx = (float)0, sz = (float)0;
    for (int k = 0; k < n; k++) sx += hx[k], sz += hz[k];
    double inv = 1.0 / n;
    *cx = (float)((float)sx * inv);
    *cz = (float)(inv * (float)sz);
    /* 0x34fd1: the farthest pair, the first strict maximum; the unrounded distance is compared with the float32
     * maximum kept so far (fst/fcomp). */
    float best = 0;
    int bi = 0, bj = 0;
    for (int i = 0; i <= n - 2; i++)
        for (int j = i; j <= n - 1; j++) {
            double d = pow((double)hx[i] - hx[j], 2.0) + pow((double)hz[i] - hz[j], 2.0);
            if (d > best) best = (float)d, bi = i, bj = j;
        }
    *theta = (float)atan2((double)hz[bi] - hz[bj], (double)hx[bi] - hx[bj]);
    return 1;
}

int ign_contact_impulse(IgnContactBody *a, IgnContactBody *b, float cx, float cz, float theta) {
    /* 0x346d8: lever arm angle/length of A and its side of the contact line. */
    double dxa = (double)cx - a->x, dza = (double)cz - a->z;
    float aa = (float)atan2(dza, dxa);
    float ra = (float)sqrt(dxa * dxa + dza * dza);
    double t = theta;
    int side = sign0((float)(dza * cos(t) - dxa * sin(t)));
    double dxb = (double)cx - b->x, dzb = (double)cz - b->z;
    float ab = (float)atan2(dzb, dxb);
    float rb = (float)sqrt(dxb * dxb + dzb * dzb);
    /* 0x34775: contact point velocities and their relative normal speed; separating bodies are left alone. */
    float uax = (float)(a->vx - sin((double)aa) * (ra * a->w)), uaz = (float)(cos((double)aa) * (ra * a->w) + a->vz);
    float ubx = (float)(b->vx - sin((double)ab) * (rb * b->w)), ubz = (float)(cos((double)ab) * (rb * b->w) + b->vz);
    float un = (float)(((double)uaz - ubz) * cos(t) - ((double)uax - ubx) * sin(t));
    if (sign0(un) != side) return 0;
    double st = sin(t), ct = cos(t);
    float j0 = (float)(((double)ubx * st - (double)ubz * ct) - ((double)uax * st - (double)uaz * ct));
    int32_t imp = (int32_t)trunc(j0);
    a->impact = b->impact = (float)(imp < 0 ? -(int64_t)imp : imp);
    /* 0x348a5: k = cos(theta - a) * r * m / I, impulses split by mass with -(1 + 0.5). */
    float ka = (float)(cos((double)theta - aa) * ra * a->mass / a->inertia);
    float kb = (float)(cos((double)theta - ab) * rb * b->mass / b->inertia);
    double inv = 1.0 / ((double)a->mass + b->mass);
    double j0e = (double)j0 * -1.5;
    double ja0 = b->mass * inv * j0e;
    float jb0 = (float)(inv * a->mass * j0e);
    double ca = sin((double)aa) * st + cos((double)aa) * ct;
    float ja = (float)(ja0 / (ca * ((double)ra * ka) + 1.0));
    double cb = sin((double)ab) * st + cos((double)ab) * ct;
    double jb = jb0 / (cb * ((double)kb * rb) + 1.0);
    a->w += (double)ja * ka;
    a->vx -= (double)ja * st;
    a->vz += (double)ja * ct;
    b->w -= kb * jb;
    b->vx += jb * st;
    b->vz -= jb * ct;
    return 1;
}

int ign_car_init(IgnCar *car, const IgnLevel *lv, int id, float x, float z, float heading) {
    memset(car, 0, sizeof *car);
    car->id = id;
    car->body_stretch = 1.0f;
    car->grip_avg = 0.4;
    car->spec = ((id % 12) + 12) % 12;
    int body = -1;
    for (int i = 0; i < lv->cars.nobj && car->nparts < IGN_MAX_CAR_PARTS; i++) {
        const IgnObject *o = &lv->cars.objs[i];
        if ((int)(o->type / 10) != id) continue;
        car->parts[car->nparts++] = i;
        if (o->type % 10 == 0) body = i;
    }
    if (body < 0) return -1;
    memcpy(car->origin, lv->cars.objs[body].pos, sizeof car->origin);

    /* cars_init 0x265f6: body lifted so its lowest vertex is y=0; wheels keep only their x/z offset. */
    const IgnObject *bo0 = &lv->cars.objs[body];
    float body_low = -INFINITY;
    for (int v = 0; v < bo0->nv; v++) {
        if (bo0->verts[v * 3 + 1] > body_low) body_low = (float)bo0->verts[v * 3 + 1];
        if (bo0->verts[v * 3] < car->body_min_x) car->body_min_x = (float)bo0->verts[v * 3];
    }
    for (int p = 0; p < car->nparts; p++) {
        const IgnObject *o = &lv->cars.objs[car->parts[p]];
        float dx = (float)(o->pos[0] - car->origin[0]), dz = (float)(o->pos[2] - car->origin[2]);
        if (o->type % 10) {
            car->part_off[p][0] = dx;
            car->part_off[p][1] = 0;
            car->part_off[p][2] = dz;
            if (dx > car->front) car->front = dx;
            if (dx < car->rear) car->rear = dx;
            if (fabsf(dz) > car->half_width) car->half_width = fabsf(dz);
            for (int v = 0; v < o->nv; v++)
                if (fabsf((float)o->verts[v * 3 + 2]) > car->tyre_half) car->tyre_half = fabsf((float)o->verts[v * 3 + 2]);
        } else {
            car->part_off[p][0] = car->part_off[p][2] = 0;
            car->part_off[p][1] = -body_low;
        }
    }
    if (car->front <= car->rear) car->front = 1, car->rear = -1;
    if (car->half_width <= 0) car->half_width = 1;

    for (int g = 0; g < 3; g++) ign_torque_curve(ign_car_gears[car->spec][g], car->torque[g]);
    car->gear = car->forward_gear = 1;
    car->gear_ticks = 0;
    car->gear_delay = 30;

    /* sub_00026458 body setup. */
    IgnBody *b = &car->body;
    b->mass = ign_car_mass[car->spec];
    b->inertia = 8000.0;
    ign_car_init_turbo(car);
    b->restitution = 0.6;
    b->radius = 4.0;
    b->a = fabs(car->front) / IGN_WORLD_PER_METRE * 1.5;
    b->b = fabs(car->rear) / IGN_WORLD_PER_METRE * 2.0;
    b->grip_f = b->grip_r = 4.2;
    b->px = x / IGN_WORLD_PER_METRE;
    b->pz = z / IGN_WORLD_PER_METRE;
    b->heading = HALF_PI - heading;

    /* Box corners from the body mesh bounds (front-left, front-right, rear-right, rear-left). */
    const IgnObject *bo = &lv->cars.objs[body];
    double minx = INFINITY, maxx = -INFINITY, maxz = 0;
    for (int v = 0; v < bo->nv; v++) {
        double vx = bo->verts[v * 3], vz = fabs((double)bo->verts[v * 3 + 2]);
        if (vx < minx) minx = vx;
        if (vx > maxx) maxx = vx;
        if (vz > maxz) maxz = vz;
    }
    const double cxs[4] = {maxx, maxx, minx, minx}, czs[4] = {maxz, -maxz, -maxz, maxz};
    for (int i = 0; i < 4; i++) {
        b->corner_x[i] = cxs[i] / IGN_WORLD_PER_METRE;
        b->corner_z[i] = czs[i] / IGN_WORLD_PER_METRE;
    }
    /* cars_init 0x27a2a: the body mesh bounds x1.1 give the ground probes (car+0x2fc/+0x31c). */
    double minz = INFINITY, maxz2 = -INFINITY;
    for (int v = 0; v < bo->nv; v++) {
        double vz = bo->verts[v * 3 + 2];
        if (vz < minz) minz = vz;
        if (vz > maxz2) maxz2 = vz;
    }
    const double px[4] = {minx, minx, maxx, maxx}, pz[4] = {minz, maxz2, maxz2, minz};
    for (int i = 0; i < 4; i++) car->corner_x[i] = (float)(px[i] * 1.1), car->corner_z[i] = (float)(pz[i] * 1.1);

    IgnGroundHit hit;
    car->height = ign_ground_at(lv, x, z, 0.0f, &hit) == 0 ? -hit.y + VERT_REST : VERT_REST;
    int types[4];
    double wheel_up[4];
    sample_ground(car, lv, types, wheel_up, 0);
    car->height = car->ground + VERT_REST;
    car->ground_prev = car->ground;
    car->wall_x = car->wall_z = INFINITY;
    /* cars_init 0x273b3: the car as a car_ground_contact body. */
    car->contact_mass = 10.0f;
    car->contact_inertia = 4000.0f;
    axle_reset(car);
    place(car);
    return 0;
}

void ign_car_set_pose(IgnCar *car, const IgnLevel *lv, float x, float z, double heading, double height) {
    (void)lv;
    IgnBody *b = &car->body;
    b->px = x / IGN_WORLD_PER_METRE;
    b->pz = z / IGN_WORLD_PER_METRE;
    b->heading = heading;
    car->x = x;
    car->z = z;
    car->heading = (float)(HALF_PI - heading);
    car->height = height;
    axle_reset(car);
    place(car);
}

void ign_car_init_turbo(IgnCar *car) {
    /* cars_init 0x27451..0x27497; unlike a wreck, initialization clears the wait and effect counter. */
    car->turbo_charge = 100.0;
    car->turbo_mode = car->turbo_wait = car->turbo_frames = 0;
}

void ign_car_wreck_turbo(IgnCar *car) {
    /* 0x3a7f5..0x3a812, repeated on every explosion-effect update. */
    car->turbo_charge = 100.0;
    car->turbo_mode = 0;
}

void ign_car_respawn_controls(IgnCar *car) {
    /* respawn_place 0x41de5..0x41e93 / 0x41ee5..0x41f0d: turbo and clocks are preserved. */
    car->gear = car->forward_gear = 1;
    car->gear_ticks = 0;
    car->revs = car->slip = 0;
    car->jump_mode = car->landed = car->skid_timer = car->spark_timer = 0;
}

int ign_car_respawn(IgnCar *car, const IgnLevel *lv, float x, float z, double heading, double height) {
    /* sub_00041430 0x418ec: one probe at the respawn point; without ground, a second one 20 units towards -x, which
     * also moves the car there. */
    IgnGroundHit hit;
    if (ign_ground_at(lv, x, z, (float)-height, &hit) != 0) {
        x -= 20.0f;
        if (ign_ground_at(lv, x, z, (float)-height, &hit) != 0) return -1;
    }
    ign_car_set_pose(car, lv, x, z, heading, height);
    IgnBody *b = &car->body;
    /* 0x41c1e-0x420b4: still, level and airborne above that ground in first gear; body +0x30/+0x78-+0x9c/+0xac/+0xd4
     * cleared. */
    b->vx = b->vz = b->v_fwd = b->v_lat = b->yaw_rate = b->steer = b->drive = 0;
    b->pitch = b->roll = 0;
    b->sliding = 0;
    car->step_x = car->step_z = 0;
    car->fall_speed = 0;
    car->airborne = 1;
    car->ground = car->ground_prev = -hit.y;
    car->ground_object = hit.object;
    for (int w = 0; w < 4; w++) car->wheel_type[w] = hit.surface_type;
    car->body_roll = car->body_pitch = 0;
    car->lean_roll = car->lean_pitch = 0;
    ign_car_respawn_controls(car);
    car->kmh = 0;
    /* sub_0003a788 0x3b5e8: the wreck ends with a full turbo. */
    car->wrecked = 0;
    car->wreck_kind = 0;
    car->turbo_charge = 100.0;
    car->offroad = 0;
    car->wall_x = car->wall_z = INFINITY;
    return 0;
}

/* car_throttle 0x4dfbe: reuses the final torque index; human and computer airborne/finish rules differ. */
void ign_car_update_revs(IgnCar *car, int input, int torque_index) {
    double ratio = torque_index / (double)ign_car_gears[car->ai ? COMPUTER_PROFILE : car->spec][car->gear][4];
    if (ratio <= 1.5) {
        if (car->ai) {
            if (car->airborne && input > 80) car->revs += (input - car->revs) * (1.0 / 6.0);
            else car->revs = ratio * 66.0;
        } else {
            if (car->airborne || !race_started) car->revs += (abs(input) - car->revs) * (1.0 / 6.0);
            else car->revs = ratio * 66.0;
        }
    }
    if (car->skidding && car->kmh < 60.0) car->revs += (100.0 - car->revs) * (1.0 / 6.0);
    if (!car->ai && car->out) car->revs += -car->revs * (1.0 / 6.0);
}

/* sub_000500f0: smoothed steering limited by speed and surface grip; computer cars use profile 11's limit and, like
 * analog input, set the wheel angle directly. */
void ign_car_steer(IgnCar *car, int input, double grip_avg, int analog) {
    IgnBody *b = &car->body;
    int s = car->spec, p = car->ai ? COMPUTER_PROFILE : s;
    car->steer_visual += (10.0 * 0.01 * input * 4.0 - car->steer_visual) * 0.1;
    double limit = 0.3;
    if (fabs(car->kmh) < ign_car_steer_speed[p])
        limit = (ign_car_steer_speed[p] - car->kmh) / ign_car_steer_div[p] + 0.3;
    double target = 5.0 / grip_avg * (input * 0.01) * limit;
    if (car->ai || analog) {
        b->steer = target;
        return;
    }
    double rate = ign_car_steer_rate[s];
    if (target == 0) rate *= 0.5;
    b->steer += (target - b->steer) / rate;
}

double ign_car_update_grip(IgnCar *car, const int types[4]) {
    /* car_update 0x3070a: one invalid wheel skips the entire update. */
    for (int w = 0; w < 4; w++)
        if (types[w] < 0 || types[w] >= 30) return car->grip_avg;
    double sum = 0;
    for (int w = 0; w < 4; w++)
        sum += ign_surface_grip[types[w]];
    double avg = sum * 0.25;
    car->grip_avg = avg;
    if (car->ai) {
        car->body.grip_f = avg * ign_car_grip[COMPUTER_PROFILE];
        car->body.grip_r = car->body.grip_f * AI_REAR_GRIP[difficulty][BONUS_INDEX[car->spec]];
    } else {
        car->body.grip_f = avg * ign_car_grip[car->spec];
        car->body.grip_r = car->body.grip_f * difficulty_grip[difficulty];
    }
    return avg;
}

static int speed_index(double velocity) {
    double kmh = velocity * 3.6;
    int speed = (int)kmh;
    /* x87 truncates the extended product, not a binary64 product rounded to an integer. */
    if (speed && kmh == speed) {
        double residual = fma(velocity, 3.6, -kmh);
        double half_ulp = ldexp(1.0, ilogb(fabs(kmh)) - 64);
        if (speed > 0 && residual < -half_ulp) speed--;
        if (speed < 0 && residual > half_ulp) speed++;
    }

    return abs(speed);
}

int ign_car_input_throttle(const IgnCar *car, int accel, int brake, int automatic) {
    return car->gear > 0 ? accel : -(automatic ? brake : accel);
}

static int forward_motion_angle(double slip) {
    return (slip > -1.5707963267949 && slip < 1.5707963267949) || slip > 4.7123889803847;
}

void ign_car_input_direction(IgnCar *car, int accel, int brake, int automatic) {
    if (!automatic) {
        ign_car_brake(car, brake);
        return;
    }
    ign_car_brake(car, 0);
    if (forward_motion_angle(car->slip) && car->body.v_fwd > 2.0)
        ign_car_brake(car, brake);
    else if (brake > 5)
        car->gear = 0;
    if (accel > 50 && car->gear == 0) car->gear = 1;
    if (car->gear > 0) car->forward_gear = car->gear;
}

void ign_car_gearbox(IgnCar *car, const int16_t curves[3][IGN_TORQUE_POINTS]) {
    int idx = speed_index(car->body.v_fwd);
    if (idx >= IGN_TORQUE_POINTS) idx = IGN_TORQUE_POINTS - 1;
    int target = curves[1][idx] > curves[2][idx] || !forward_motion_angle(car->slip) ? 1 : 2;
    car->gear_ticks = target != car->gear ? car->gear_ticks + 1 : 0;
    if (car->gear_ticks > car->gear_delay) car->gear = target;
    if (car->gear > 0) car->forward_gear = car->gear;
}

void ign_car_shift(IgnCar *car, int shift) {
    if (shift > 0 && car->gear < 2) car->gear++;
    if (shift < 0 && car->gear > 0) car->gear--;
    if (car->gear > 0) car->forward_gear = car->gear;
}

/* sub_0004db90: torque curve (speed index divided by 0xc5450[difficulty]) plus the creep, catch-up and computer
 * bonuses, x throttle; reverse selected while rolling forward brakes with -25000; released, engine braking. */
int ign_car_throttle(IgnCar *car, int input) {
    static const double SPEED_DIV[4] = {1.0, 1.15, 1.3, 1.3};
    IgnBody *b = &car->body;
    int magnitude = speed_index(b->v_fwd);
    double divisor = SPEED_DIV[difficulty], quotient = magnitude / divisor;
    int idx = (int)quotient;
    if (idx && quotient == idx &&
        fma(-idx, divisor, magnitude) < -ldexp(1.0, ilogb(quotient) - 64) * divisor)
        idx--;
    if (idx > IGN_TORQUE_POINTS - 1) idx = IGN_TORQUE_POINTS - 1;
    /* 0x4dd05: wheelspin against the motion gives no torque. */
    if (car->slip > 1.5707963267949 && car->slip < 4.7123889803847 && car->kmh > 2.0 && input > 0)
        car->skid_timer = 10, idx = 0;
    int torque = car->torque[car->gear][idx];
    /* 0x4dd5e: with the curve at <= 3 the creep grows by one every 8 x creep + 15 ticks; above it, it halves. */
    if (torque <= 3) {
        if (++car->creep_ticks >= car->creep * 8 + 15) car->creep_ticks = 0, car->creep++;
    } else if (car->creep > 0) {
        car->creep /= 2;
    }
    /* 0x4ddb7 ([0xc0b50] = 1): every 182 frames the human gets 1.5 per place behind the leader. */
    if (!car->ai) {
        if (car->catchup_clock + 182.0 < level_clock)
            car->catchup = (car->position - 1) * 1.5, car->catchup_clock = level_clock;
    } else {
        car->catchup = 0;
    }
    double target = b->v_fwd > 0 && car->gear == 0
                        ? -25000.0
                        : (torque + car->creep + car->catchup + car->torque_bonus) * 1000.0 * (input * 0.01);
    /* 0x4df0e: before the start the drive force keeps its value. */
    if (race_started) b->drive += (target - b->drive) * 0.2;
    if (input == 0) b->drive = -(b->v_fwd * 500.0);
    if (b->saturated && !car->airborne) car->skid_timer = 30;
    return idx;
}

void ign_car_brake(IgnCar *car, int input) {
    if (input > 80 && car->body.v_fwd > 2.0) car->skid_timer = 10;
    /* 0x4e233 / 0x4e254: >50 locks the wheels; the nonnegative start clock also locks them. */
    car->body.sliding = input > 50 || !race_started;
}

/* sub_0004e2d8: burns 2.5 charge per tick while active; otherwise recharges (it may pass 100 for one tick). */
void ign_car_turbo(IgnCar *car) {
    IgnBody *b = &car->body;
    if (!car->turbo_mode) {
        if (car->turbo_charge < 100.0)
            car->turbo_charge += ign_car_turbo_rate[car->spec];
        else if (car->turbo_charge > 100.0)
            car->turbo_charge = 100.0;
        return;
    }
    car->turbo_charge -= 2.5;
    if (car->turbo_charge <= 0) {
        car->turbo_charge = 0;
        car->turbo_mode = 0;
        car->turbo_frames = 0;
        return;
    }
    float low = (float)((20.0 - b->v_fwd) * 10000.0);
    if (low < 0) low = 0;
    if (car->turbo_mode == 1) {
        b->drive += ign_car_turbo_force[car->ai ? COMPUTER_PROFILE : car->spec] * car->turbo_charge + low;
    } else if (car->turbo_mode == 2) {
        b->drive += car->turbo_charge * 20000.0 + low;
        b->vx += cos(b->heading) * car->turbo_charge * 0.01;
        b->vz += sin(b->heading) * car->turbo_charge * 0.01;
    }
}

int ign_car_turbo_input(const IgnCar *car, int key, double elapsed) {
    if (key != 1 || car->wrecked) return 0;
    return elapsed < 0.25 && !car->turbo_key && !car->turbo_mode ? 2 : 1;
}

IgnTurboRequestEvent ign_car_turbo_request(IgnCar *car, int request) {
    IgnTurboRequestEvent event = IGN_TURBO_REQUEST_NONE;
    if (request == 2) {
        car->turbo_wait = 0;
        car->turbo_mode = 2;
        car->turbo_started = 1;
        return IGN_TURBO_REQUEST_FORCED;
    }
    if (request != 1) return event;
    if (car->turbo_charge >= 100.0 && !car->turbo_mode && race_started && !car->out) {
        car->turbo_mode = 1;
        car->turbo_wait = 0;
        car->turbo_started = 1;
        event = IGN_TURBO_REQUEST_START;
    } else if (!car->turbo_mode && car->turbo_wait <= 0 && car->turbo_charge > 20.0 && !car->ai) {
        car->turbo_wait = 30;
        event = IGN_TURBO_REQUEST_REFUSED;
    }
    if (car->turbo_wait > 0) car->turbo_wait--;
    return event;
}

/* sub_0002e6f8 + sub_00040ec8: body lean from per-tick acceleration, smoothed towards ground tilt. */
static void body_pose(IgnCar *car, double dx, double dz) {
    const IgnBody *b = &car->body;
    double ax = dx - car->step_x, az = dz - car->step_z;
    car->step_x = dx;
    car->step_z = dz;
    if (ax > 0.5) ax = 0.5;
    if (ax < -0.5) ax = -0.5;
    if (az > 0.5) az = 0.5;
    if (az < -0.5) az = -0.5;
    double ch = cos(b->heading), sh = sin(b->heading);
    double lat = -sh * ax + ch * az, lon = ch * ax + sh * az;
    car->lean_roll = (car->lean_roll - 3.0 * lat) * 0.97;
    car->lean_pitch = (car->lean_pitch - 3.0 * lon) * 0.97;
    if (car->lean_roll > 35) car->lean_roll = 35;
    if (car->lean_roll < -35) car->lean_roll = -35;
    if (car->lean_pitch > 20) car->lean_pitch = 20;
    if (car->lean_pitch < -20) car->lean_pitch = -20;
    double target_roll = b->roll + car->lean_roll * (PI / 180.0);
    double target_pitch = b->pitch + car->lean_pitch * (PI / 180.0);
    car->body_roll += (target_roll - car->body_roll) * 0.25;
    car->body_pitch += (target_pitch - car->body_pitch) * 0.25;
}

void ign_car_auto_gearbox(IgnCar *car) {
    /* 0x502ee/0x502fe use model zero's shared curves, not this car's model or difficulty. */
    static int16_t curves[3][IGN_TORQUE_POINTS];
    static int curves_ready;
    if (!curves_ready) {
        for (int g = 1; g < 3; g++) ign_torque_curve(ign_car_gears[0][g], curves[g]);
        curves_ready = 1;
    }
    ign_car_gearbox(car, curves);
}

void ign_car_apply_input(IgnCar *car, const IgnInput *in, int turbo_request) {
    int accel = in->analog ? in->a_accel : in->accel ? 100 : 0;
    int brake = in->analog ? in->a_brake : in->brake ? 100 : 0;
    int automatic = !in->manual_gear;
    int throttle_in = ign_car_input_throttle(car, accel, brake, automatic);
    int torque_index = ign_car_throttle(car, throttle_in);
    ign_car_update_revs(car, throttle_in, torque_index);
    ign_car_turbo(car);
    ign_car_input_direction(car, accel, brake, automatic);
    ign_car_steer(car, in->analog ? in->a_steer : (in->left ? 100 : 0) - (in->right ? 100 : 0),
                  car->grip_avg, in->analog);
    ign_car_turbo_request(car, turbo_request);
}

void ign_car_tick(IgnCar *car, const IgnLevel *lv, const IgnInput *in) {
    IgnBody *b = &car->body;
    double px0 = b->px, pz0 = b->pz;
    car->landed = 0;
    car->wall_impact = 0;
    int types[4];
    double wheel_up[4];
    sample_ground(car, lv, types, wheel_up, 1);

    /* car_update 0x2f01f: near a special object (PLC type 0x12c-0x15d, list 0x1fef58, built at 0x255cb) the ground
     * under the car centre is probed; type 0x130 (AUSTRIA) starts wreck 3 (sub_0003db4c). The other types drive
     * moving objects that the port does not have. */
    for (int i = 0; i < lv->track.nobj; i++) {
        const IgnObject *o = &lv->track.objs[i];
        if (o->type < 0x12c || o->type >= 0x15e || fabsf(car->x - (float)o->pos[0]) >= 200.0f ||
            fabsf(car->z - (float)o->pos[2]) >= 200.0f)
            continue;
        IgnGroundHit hit;
        if (ign_ground_at(lv, car->x, car->z, (float)-car->height, &hit) != 0 ||
            lv->track.objs[hit.object].type < 0x12c)
            continue;
        if (lv->track.objs[hit.object].type == 0x130) {
            wreck_why(car, 3, "object 0x130");
            return;
        }
        break;
    }

    /* car_update 0x2fd63: no ground under any wheel wrecks the car; 0x2fd9a: neither do more than 5 grounded ticks
     * with every wheel on a type above 0x28. A wheel without ground keeps its last type (0x30265). */
    int none = 1, rough = 1;
    for (int w = 0; w < 4; w++) {
        if (types[w] >= 0) car->wheel_type[w] = types[w], none = 0;
        if (types[w] <= 0x28) rough = 0;
    }
    car->offroad = rough && !car->airborne ? car->offroad + 1 : 0;
    /* Without an SRF (unit tests) every lookup fails, so there is nothing to judge. */
    if (lv->surface.data && (none || car->offroad > 5)) {
        wreck_why(car, 1, none ? "no ground" : "off road");
        return;
    }

    /* cars_update_all 0x2dbb2: car+0x90 = angle of the velocity relative to the heading. */
    double vel = b->vx != 0 || b->vz != 0 ? atan2(b->vz, b->vx) : 0;
    car->slip = fmod(vel - b->heading, 2 * PI);
    if (car->slip < 0) car->slip += 2 * PI;

    int barrier = wall_trigger(car, types, wheel_up);
    double grip_avg = ign_car_update_grip(car, types); /* car+0x148 */
    car->surface_type = types[0] < 0 ? 0 : types[0];

    if (in) {
        int request = car->ai ? (in->turbo && !car->wrecked) :
                               ign_car_turbo_input(car, in->turbo, car->race_elapsed);
        ign_car_apply_input(car, in, request);
        if (in->manual_gear) ign_car_shift(car, in->gear_shift);
        else if (car->gear != 0) ign_car_auto_gearbox(car);
        car->turbo_key = request;
    }

    if (barrier) {
        /* cars_update_all 0x2d77a: the wall list (car+0x9b0) is rebuilt only after moving 150 units in x or z. */
        if (fabs(car->x - car->wall_x) > 150.0 || fabs(car->z - car->wall_z) > 150.0) {
            IgnWall walls[IGN_MAX_WALLS];
            car->nwalls = ign_collect_walls(lv, car->x, (float)car->height, car->z, car->jump_mode, walls,
                                            IGN_MAX_WALLS);
            for (int i = 0; i < car->nwalls; i++) {
                car->walls[i][0] = walls[i].x0 / IGN_WORLD_PER_METRE;
                car->walls[i][1] = walls[i].z0 / IGN_WORLD_PER_METRE;
                car->walls[i][2] = walls[i].x1 / IGN_WORLD_PER_METRE;
                car->walls[i][3] = walls[i].z1 / IGN_WORLD_PER_METRE;
            }
            car->wall_x = car->x;
            car->wall_z = car->z;
        }
        car->wall_hit = ign_body_collide(b, (const double(*)[4])car->walls, car->nwalls);
    } else {
        car->wall_hit = 0;
    }
    car->wall_impact = car->wall_hit ? b->impact : 0;
    ign_body_step(b, car->airborne, grip_avg > 0.4 ? 1.0 : 4.0);
    car->kmh = (float)fabs(b->v_fwd * 3.6);
    car->x = (float)(b->px * IGN_WORLD_PER_METRE);
    car->z = (float)(b->pz * IGN_WORLD_PER_METRE);
    car->heading = (float)(HALF_PI - b->heading);
    axle_update(car);

    /* cars_update_all 0x2d229: car_vertical runs after body_step with the ground car_update found before it. */
    double drop;
    int landed = ign_car_vertical_step(car, &drop);
    if (car->ground + 30.0 < car->height) car->airborne = 1;
    if (landed) car->landed = 1, landing_check(car, drop);
    place(car);
    body_pose(car, (b->px - px0) * IGN_WORLD_PER_METRE, (b->pz - pz0) * IGN_WORLD_PER_METRE);

    /* sub_0003731c: skid effects while sliding more than 22.5 degrees above 8 km/h, or while the timer runs. */
    double s = sin(car->slip);
    car->skidding = !car->airborne && !car->wrecked &&
                    ((fabs(s) > sin(PI / 8) && car->kmh > 8.0) || car->skid_timer > 0);
    if (car->skid_timer > 0) car->skid_timer--; /* cars_update_all 0x2daf8 */
}
