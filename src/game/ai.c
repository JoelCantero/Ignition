#include "ai.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "physics.h"

#define PI 3.14159265358979323846

/* Port of sub_0001aea0 (see docs/formats.md, "IA original"). Deviations: the per-section records of
 * load_tri are rebuilt on the fly from the gate edges (angles +0x2e), the lane/branch choice tables
 * at 0xbf9a0 are not used (the port's progress code already picks the branch from the car position),
 * and the throttle algebra of 0x1b9a2-0x1bae2 is approximated. */

static int track_id = 1;
static IgnCar *all_cars[16];
static int n_cars;

void ign_ai_set_track(const char *level) {
    static const char *names[7] = {"CANADA", "USA", "CARIB", "BRAZIL", "AUSTRIA", "ICELAND", "JAPAN"};
    for (int i = 0; i < 7; i++)
        if (level && !strcmp(level, names[i])) track_id = i;
}

void ign_ai_set_cars(IgnCar *const *cars, int n) {
    n_cars = n > 16 ? 16 : n;
    memcpy(all_cars, cars, sizeof(IgnCar *) * (size_t)n_cars);
}

void ign_ai_init(IgnAi *ai, int index) {
    static const IgnAiParams base = {0.8, 0.4, 0.6, 0.75, 0.5, 5.0, 0.6, 0.6, 0.9};
    (void)index;
    memset(ai, 0, sizeof *ai);
    ai->p = base;
}

/* Curve speed limits {section & ~1, km/h} of [0xbfa48 + idx*32]; 500 = no limit. */
typedef struct {
    short key, speed;
} Limit;
static const Limit limits[7][8] = {
    {{-84, 500}, {-118, 500}},
    {{104, 500}, {140, 110}, {142, 90}, {144, 90}, {146, 90}, {148, 110}},
    {{78, 500}},
    {{-180, 500}, {174, 500}, {90, 110}, {92, 80}, {94, 80}},
    {{48, 120}, {50, 90}, {-52, 90}, {-54, 130}, {-78, 500}, {-186, 500}, {100, 110}, {106, 110}},
    {{100, 500}},
    {{154, 100}, {192, 500}, {114, 100}, {116, 110}},
};
static const int track_idx[7] = {1, 2, 3, 6, 4, 0, 5};
static const int look_off[6] = {1, 2, 3, 4, 6, 8};

static double wrap(double a) {
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

typedef struct {
    double lx, lz, rx, rz;
} Edge;

static Edge edge_at(const IgnTrack *t, int s, int bb) {
    s %= t->count;
    if (s < 0) s += t->count;
    const float *g = bb && t->has_b[s] ? t->gate_b[s] : t->gate[s];
    return (Edge){g[0], g[1], g[2], g[3]};
}

static void mid_of(Edge e, double *x, double *z) {
    *x = (e.lx + e.rx) * 0.5;
    *z = (e.lz + e.rz) * 0.5;
}

/* Record angle +0x2e: direction of the road centre line from section s to the next one. */
static double road_angle(const IgnTrack *t, int s, int bb) {
    double ax, az, bx, bz;
    mid_of(edge_at(t, s, bb), &ax, &az);
    mid_of(edge_at(t, s + 1, bb), &bx, &bz);
    return atan2(bz - az, bx - ax);
}

void ign_ai_drive(IgnAi *ai, const IgnCar *car, const IgnTrack *t, IgnInput *in) {
    *in = (IgnInput){0};
    in->analog = true;
    if (!t->gate || t->count < 4 || car->section == IGN_NO_SECTION) return;
    const IgnBody *b = &car->body;
    const IgnAiParams *p = &ai->p;
    int diff = ign_get_difficulty();
    int s = abs(car->section);
    int bb = car->section < 0;
    int idx = track_idx[track_id];
    double cx = car->x, cz = car->z;

    /* +0x58: distance to the left edges over distance to the right edges, folded to |ratio| >= 1. */
    Edge e0 = edge_at(t, s, bb), e1 = edge_at(t, s + 1, bb);
    double dl = hypot(cx - e0.lx, cz - e0.lz) + hypot(cx - e1.lx, cz - e1.lz);
    double dr = hypot(cx - e0.rx, cz - e0.rz) + hypot(cx - e1.rx, cz - e1.rz);
    double ratio = dr > 0 ? dl / dr : 1;
    ai->side = ratio < 1 && ratio > 0 ? -1 / ratio : ratio;

    /* Lookahead records: sections s + {1,2,3,4,6,8}, stopping where the turn direction flips. */
    double cur_ang = road_angle(t, s, bb);
    double sum = 0, first = 0;
    int used = 0, sign = 0;
    double lx = 0, lz = 0;
    for (int k = 0; k < 6; k++) {
        int sk = s + look_off[k];
        double d = wrap(road_angle(t, sk, bb) - cur_ang);
        int sg = d > 0.02 ? 1 : d < -0.02 ? -1 : 0;
        if (sg && !sign) sign = sg;
        if (sg && sign && sg != sign) break;
        if (k == 0) first = d;
        sum += fabs(d);
        used++;
        mid_of(edge_at(t, sk, bb), &lx, &lz);
    }
    if (!used) {
        used = 1;
        mid_of(edge_at(t, s + 1, bb), &lx, &lz);
    }
    double grip = clampd((8 - ign_surface_grip[car->surface_type >= 0 && car->surface_type < 30 ? car->surface_type : 0]) * 0.125, 0, 100);
    double curve = 1 + (grip + 0.3) * sum * (p->curveinflu * 0.4 + 0.6) * 1.6 / used;

    /* Curve speed override of the current section (last matching entry wins). */
    int key = (bb ? -s : s) & ~1, override = -1;
    for (int i = 0; i < 8; i++)
        if (limits[idx][i].speed && limits[idx][i].key == key) override = limits[idx][i].speed;

    /* Throttle target (+0x70) and brake level. */
    double kmh = car->kmh;
    double cap = p->acc * 60 + 120;
    double margin = clampd((cap - kmh) / cap, 0, 100) * p->acc * 1.5 + 1;
    double tgt = ((1 + p->acc) * 0.5 + 0.3) * margin / ((curve - 1) * 0.4 + 1);
    double accel = clampd(tgt * 100, 0, 100);
    int brake = 0;
    if (override != -1 && override != 500 && kmh > override)
        brake = (int)clampd((kmh - override) * 0.5, 0, 50);
    else {
        /* Port heuristic (not in the original): brake for the total turn seen over the lookahead. */
        double lim = fmax(70.0, 240.0 / (1 + 1.2 * sum * (grip + 0.3) * (p->curveinflu * 0.4 + 0.6) * 1.6));
        if (kmh > lim) brake = (int)clampd((kmh - lim) * 0.5, 0, 50);
    }

    /* Steering: aim at the lookahead mid-point from the blended travel direction. */
    double h = b->heading;
    double dirt = (car->step_x != 0 || car->step_z != 0) ? atan2(car->step_z, car->step_x) : 0;
    double travel = h + 0.15 * wrap(dirt - h);
    if (fabs(h - dirt) > 0.7854) travel = h;
    double aim = atan2(lz - cz, lx - cx);
    double err = wrap(aim - travel);
    double road_err = wrap(cur_ang - travel);
    double dir = err * 75 * ((curve - 1) * 0.25 + 1);
    dir *= diff == 1 ? 0.9 : diff == 2 ? 0.8 : diff == 3 ? 0.75 : 1;
    dir = clampd(dir, -100, 100);

    /* Nearest rival ahead: steer away from it. */
    int best = -1;
    double bestd = 100000;
    for (int i = 0; i < n_cars; i++) {
        if (all_cars[i] == car) continue;
        double d = hypot(all_cars[i]->x - cx, all_cars[i]->z - cz);
        if (d < bestd) bestd = d, best = i;
    }
    if (best >= 0) {
        double rel = wrap(atan2(all_cars[best]->z - cz, all_cars[best]->x - cx) - travel);
        double r = p->careother * 800;
        if (fabs(rel) < 0.3 && r > bestd) {
            double push = clampd(-(rel < 0 ? -1 : 1) * 100 * (1 - bestd / r) - rel * 40, -100, 100);
            dir += (push - dir) / curve;
        }
    }
    /* Stay off the edge when the road bends away and the car is already hugging it. */
    if (sign * first < 0 && fabs(road_err) < 0.3 && curve < 2 && used > 2 && fabs(ai->side) > 1.5) {
        double tgt_dir = clampd((curve - 1) * 50, 0, 50) * (ai->side < 0 ? 1 : -1);
        dir += (tgt_dir - dir) / (ai->side < 0 ? 2.5 : fabs(ai->side) * 2.5);
    }
    /* Collision spin: invert the steering while bumping another car. */
    ai->steer = dir = clampd(dir, -100, 100);

    /* Stuck detection (+0x60 counts ticks; the original adds the car count once per round). */
    /* The original resets the counter while the car faces more than 1.4 rad away from the road; the port
     * also counts those ticks so a car pointing at a wall still recovers. */
    if (ai->age < 144) ai->age++;
    if (ai->age >= 144 && (fabs(b->v_fwd) < 6.944 || car->airborne))
        ai->stuck++;
    else
        ai->stuck = 0;
    if (ai->stuck >= 151.2) ai->stuck = 0;
    int reverse = ai->stuck > 43.2 && ai->stuck < 97.2;
    if (ai->stuck > 97.2 && ai->stuck < 108) ai->steer = dir = 0;
    if (car->airborne) ai->steer = dir = 0;

    if (reverse) {
        in->a_brake = 50;
        in->a_steer = (int)-clampd(dir, -70, 70);
        ai->accel = 0;
        ai->section = s;
        return;
    }
    /* Turbo (0x1ca15): full charge, on the ground, no braking, gentle curve and steering. */
    int turbo = car->turbo_charge >= 100.0 && !car->airborne && !brake &&
                p->aggr * 0.13 + 1.07 > curve && used > 3 && fabs(dir) < p->aggr * 5 + 3;
    in->a_steer = (int)dir;
    in->turbo = turbo;
    if (brake)
        in->a_brake = brake;
    else
        in->a_accel = (int)accel;
    ai->accel = (int)accel;
    ai->section = s;
}
