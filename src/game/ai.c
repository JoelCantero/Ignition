#include "ai.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "physics.h"

#define PI 3.14159265358979323846

/* Port of sub_0001aea0 (see docs/formats.md, "IA original"). The lane table and split TRI records are
 * applied to the reconstructed section graph; exact build_sections/ai_find_section topology remains
 * approximate. The slope term, collision-overlap spin, control-type 5/8 gain and avoidance sign flip
 * from the rival's own side ratio are also omitted. */

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
    static const IgnAiParams base = {0.9, 0.8, 0.4, 0.6, 0.75, 0.5, 5.0, 0.6, 0.6};
    (void)index;
    memset(ai, 0, sizeof *ai);
    ai->p = base;
    ai->input.analog = true;
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

typedef struct {
    short section, chance, selector;
} LaneChoice;

static const LaneChoice lane_choices[7][4] = {
    {{50, 50, 0}, {90, 50, 0}, {2, 50, 0}, {2, 50, 0}},
    {{10, 60, 0}, {130, 40, 0}, {2, 50, 0}, {2, 50, 0}},
    {{5, 50, 0}, {90, 50, 0}, {2, 50, 0}, {2, 50, 0}},
    {{50, 40, 0}, {2, 50, 0}, {2, 50, 0}, {2, 50, 0}},
    {{5, 100, 0}, {33, 50, 0}, {110, 50, 0}, {2, 50, 0}},
    {{10, 60, 0}, {190, 0, 1}, {2, 50, 0}, {2, 50, 0}},
    {{15, 35, 0}, {55, 45, 0}, {2, 50, 0}, {2, 50, 0}},
};

void ign_ai_update_route_choice(IgnAi *ai, int section, IgnFx *fx) {
    const LaneChoice *choices = lane_choices[track_idx[track_id]];
    for (int i = 0; i < 4; i++) {
        if (choices[i].section != section) continue;
        ai->route_selector = choices[i].selector;
        ai->route_random = (double)ign_fx_rand15(fx) * (1.0 / 32768.0);
        if (choices[i].chance == 0)
            ai->route_choice = 0;
        else if (choices[i].chance == 100)
            ai->route_choice = 1;
        else
            ai->route_choice =
                (ai->p.s20 * 0.7 + 0.3) * ai->route_random * choices[i].chance * 0.01 > 0.125;
    }
}

static double wrap(double a) {
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

static int reverse_zone(const IgnAi *ai, int section) {
    int s = abs(section);
    return (track_id == 1 && s > 0x54 && s < 0x5f && ai->side < 0) ||
           (track_id == 4 && section > 0x3d && section < 0x41) ||
           (track_id == 5 && s >= 0x92 && s <= 0x94);
}

void ign_ai_stuck_step(IgnAi *ai, const IgnCar *car, double road_error, int ncars) {
    if (fabs(road_error) > 1.4 || (car->body.v_fwd < 6.944444444444445 && !car->airborne) ||
        (ai->stuck > 43.2 && reverse_zone(ai, car->section)))
        ai->stuck += ncars;
    else
        ai->stuck = 0;
    if (ai->stuck >= 151.2) ai->stuck = 0;
}

typedef struct {
    double lx, lz, rx, rz;
} Edge;

static int section_index(const IgnTrack *t, int s) {
    s %= t->count;
    if (s < 0) s += t->count;
    return s;
}

static Edge edge_at(const IgnTrack *t, int s, int path, int lane) {
    int resolved_section = s, record_path = path, object = -1;
    if (!ign_track_find_section(t, s, path, 1, track_id,
                                &resolved_section, &record_path, &object))
        resolved_section = section_index(t, s), record_path = 0;
    (void)object;
    if (t->ai_records && t->ai_record_count) {
        int count = t->ai_record_count[resolved_section][record_path];
        int record_index = count > 1 && lane ? 1 : 0;
        if (count > record_index) {
            const IgnAiRouteRecord *record =
                &t->ai_records[resolved_section][record_path][record_index];
            if (record->valid)
                return (Edge){record->point[0] - IGN_SRF_ORIGIN, record->point[2] - IGN_SRF_ORIGIN,
                              record->point[3] - IGN_SRF_ORIGIN, record->point[5] - IGN_SRF_ORIGIN};
        }
    }
    const float *g = record_path ? t->gate_b[resolved_section] : t->gate[resolved_section];
    return (Edge){g[0], g[1], g[2], g[3]};
}

static int selected_path(const IgnTrack *t, const IgnAi *ai, int s, int active_branch) {
    s = section_index(t, s);
    if (!t->has_b || !t->has_b[s]) return 0;
    if (active_branch) return 1;
    return ai->route_selector ? ai->route_choice : ai->side < 0;
}

static Edge selected_edge(const IgnTrack *t, const IgnAi *ai, int s, int active_branch) {
    int path = selected_path(t, ai, s, active_branch);
    return edge_at(t, s, path, ai->route_choice);
}

static void mid_of(Edge e, double *x, double *z) {
    *x = (e.lx + e.rx) * 0.5;
    *z = (e.lz + e.rz) * 0.5;
}

/* Record angle +0x2e: direction of the road centre line from section s to the next one. */
static double road_angle(const IgnTrack *t, const IgnAi *ai, int s, int bb) {
    double ax, az, bx, bz;
    mid_of(selected_edge(t, ai, s, bb), &ax, &az);
    mid_of(selected_edge(t, ai, s + 1, bb), &bx, &bz);
    return atan2(bz - az, bx - ax);
}

void ign_ai_drive(IgnAi *ai, const IgnCar *car, const IgnTrack *t, IgnFx *fx, IgnInput *in) {
    *in = (IgnInput){0};
    in->analog = true;
    if (!t->gate || t->count < 4 || car->section == IGN_NO_SECTION) return;
    const IgnBody *b = &car->body;
    IgnAiParams p = ai->p;
    int diff = ign_get_difficulty();
    p.s00 *= diff == 1 ? 0.7 : diff >= 2 ? 0.4 : 1.0; /* load_tri 0x1ceef / 0x1cf06 */
    int s = abs(car->section);
    int bb = car->section < 0;
    int idx = track_idx[track_id];
    double cx = car->x, cz = car->z;

    /* +0x58: distance to the left edges over distance to the right edges, folded to |ratio| >= 1. */
    Edge e0 = edge_at(t, s, bb, ai->route_choice), e1 = edge_at(t, s + 1, bb, ai->route_choice);
    double dl = hypot(cx - e0.lx, cz - e0.lz) + hypot(cx - e1.lx, cz - e1.lz);
    double dr = hypot(cx - e0.rx, cz - e0.rz) + hypot(cx - e1.rx, cz - e1.rz);
    double ratio = dr > 0 ? dl / dr : 1;
    ai->side = ratio < 1 && ratio > 0 ? -1 / ratio : ratio;
    ign_ai_update_route_choice(ai, s, fx);

    /* Lookahead records: sections s + {1,2,3,4,6,8}, stopping where the turn direction flips. */
    double cur_ang = road_angle(t, ai, s, bb);
    double sum = 0, first = 0;
    int used = 0, sign = 0;
    double lx = 0, lz = 0;
    for (int k = 0; k < 6; k++) {
        int sk = s + look_off[k];
        double d = wrap(road_angle(t, ai, sk, bb) - cur_ang);
        int sg = d > 0.02 ? 1 : d < -0.02 ? -1 : 0;
        if (sg && !sign) sign = sg;
        if (sg && sign && sg != sign) break;
        if (k == 0) first = d;
        sum += fabs(d);
        used++;
        mid_of(selected_edge(t, ai, sk, bb), &lx, &lz);
    }
    if (!used) {
        used = 1;
        mid_of(selected_edge(t, ai, s + 1, bb), &lx, &lz);
    }
    int off = look_off[used - 1];
    double grip = fmax(0, (8 - ign_surface_grip[car->surface_type >= 0 && car->surface_type < 30 ? car->surface_type : 0]) * 0.125);
    /* 0x1b7b0-0x1b84a */
    double a8 = 1 + (grip + 0.8) * sum * ((p.s38 * 0.4 + 0.6) * 1.6) / used;

    /* Curve speed override of the current section (last matching entry wins). */
    int key = (bb ? -s : s) & ~1, override = -1;
    for (int i = 0; i < 8; i++)
        if (limits[idx][i].speed && limits[idx][i].key == key) override = limits[idx][i].speed;

    /* Throttle (0x1b9a2-0x1bad1): T is the pedal fraction, scaled by 100 at the end. */
    double kmh = car->kmh;
    double T = ((1 + p.s00) * 0.5 + 0.3) / (p.s10 * 6 / (3.0 * used + 6) + 1);
    T /= (a8 - 1) * 0.4 + 1;
    double cap = p.s18 * 60 + 120;
    double x88 = fmax(0, cap - kmh) / cap * (p.s18 * 1.5) + 1;
    T = (T * x88 + (x88 - 1) * 0.25) * 100;
    if (T > 100) T = 100;
    int brake = 0, force_turbo = 0;
    if (override == 500) {
        T = 100;
        force_turbo = diff < 1;
    } else if (override != -1 && override * 0.2777 < b->v_fwd) {
        brake = (int)clampd((kmh - override) * 50 * 0.01, 0, 50);
    }
    double A = fmin(T * 1.5, 100);

    /* Steering (0x1c07a...): aim at the lookahead mid-point from the blended travel direction. */
    double h = b->heading;
    double dirt = (car->step_x != 0 || car->step_z != 0) ? atan2(car->step_z, car->step_x) : h;
    double travel = h + 0.15 * wrap(dirt - h);
    if (fabs(h - dirt) > 0.7854) travel = h;
    double aim = atan2(lz - cz, lx - cx);
    double c0 = wrap(aim - travel);
    double e40 = first;
    double dir = c0 * 75 * ((a8 - 1) * 0.25 + 1);
    dir *= diff == 1 ? 0.9 : diff == 2 ? 0.8 : diff == 3 ? 0.75 : 1;
    dir = clampd(dir, -100, 100);

    /* Nearest rival (0x1c1b0-0x1c3d0): steer around it when it is almost straight ahead. */
    int best = -1;
    double bestd = 1e9;
    for (int i = 0; i < n_cars; i++) {
        if (all_cars[i] == car) continue;
        double d = hypot(all_cars[i]->x - cx, all_cars[i]->z - cz);
        if (d < bestd) bestd = d, best = i;
    }
    if (best >= 0) {
        double rel = wrap(atan2(all_cars[best]->z - cz, all_cars[best]->x - cx) - travel + 1.5708);
        double r = p.s28 * 800;
        if (rel > 1.2708 && rel < 1.8708 && r > bestd) {
            rel = wrap(rel - 1.5708);
            double v = rel < 0 ? -PI / 2 : PI / 2;
            double push = -(v - 3 * rel) * 40;
            push = r / (r - bestd) * push;
            push = clampd(push, -100, 100);
            dir += (push - dir) / a8;
        }
    }

    /* Hug the inside edge of the next bend (0x1c3e0-0x1c5be). */
    if (off > 2 && a8 < 2 && fabs(e40) < 0.3) {
        if (sign == -1) {
            double tg = fmin(50, (a8 - 1) * 50);
            double dv = ai->side >= 0 ? ai->side * 2.5 : 2.5;
            dir += (tg - dir) / dv;
        } else if (sign == 1) {
            double tg = fmax(-50, (a8 - 1) * -50);
            double dv = ai->side <= 0 ? 2.5 : ai->side * -2.5;
            dir += (tg - dir) / dv;
        }
    }
    ai->steer = dir;

    /* 0x1bfc1: TRI road angle minus blended travel, not the lookahead aiming angle. */
    ign_ai_stuck_step(ai, car, wrap(cur_ang - travel), n_cars);
    int force_back = reverse_zone(ai, car->section);
    int reverse = ai->stuck > 43.2 && ai->stuck < 97.19999999999997;
    if (reverse) {
        if (ai->stuck < 53.99999999999999)
            ai->steer = 0;
        else if (force_back)
            ai->steer = -100;
        else if (ai->steer < 0 && ai->steer > -70)
            ai->steer = -70;
        else if (ai->steer >= 0 && ai->steer < 70)
            ai->steer = 70;
        in->a_brake = (int)A;
        in->a_steer = (int)-ai->steer;
        ai->accel = 0;
        ai->section = s;
        return;
    }
    if (ai->stuck > 97.19999999999997 && ai->stuck < 108) ai->steer = 0;
    if (car->airborne) ai->steer = 0;

    /* Turbo (0x1ca15): full charge, on the ground, no braking; a hard steering input in a gentle
     * bend, or an unlimited-speed section on the easiest difficulty. */
    int want = (p.s20 * 0.13 + 1.07 > a8 && off > 3 && p.s20 * 5 + 3 < fabs(ai->steer)) || force_turbo;
    int sec = bb ? -s : s;
    int zone = 1;
    if (track_id == 4 && s > 0x28 && s < 0x36) zone = 0;
    if (track_id == 0 && ((sec < -156 && sec > -162) || (sec < 102 && sec > 90))) zone = 0;
    in->turbo = car->turbo_charge == 100.0 && !car->airborne && !brake && want && zone;
    in->a_steer = (int)ai->steer;
    if (brake)
        in->a_brake = brake;
    else
        in->a_accel = (int)T;
    ai->accel = (int)T;
    ai->section = s;
}

IgnAiControlAction ign_ai_control_tick(IgnAi *ai, IgnCar *car, const IgnTrack *t, IgnFx *fx,
                                       const IgnRace *race, int index) {
    IgnAiControlAction action = ign_race_ai_action(race, car, index, 1);
    if (action == IGN_AI_CONTROLS_NONE) return action;
    /* 0x2dcea..0x2dd1b: automatic gearbox precedes both fresh and replayed input. */
    if ((!ai->input.manual_gear && car->gear != 0) || car->progress_lock == 1)
        ign_car_auto_gearbox(car);
    if (action == IGN_AI_CONTROLS_DRIVE) {
        int manual = ai->input.manual_gear;
        ign_ai_drive(ai, car, t, fx, &ai->input);
        ai->input.manual_gear = manual;
        ai->turbo_request = ai->input.turbo;
    }
    ign_car_apply_input(car, &ai->input, ai->turbo_request);
    return action;
}
