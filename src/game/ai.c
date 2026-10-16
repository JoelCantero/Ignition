#include "ai.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "physics.h"

static int track_id = 1;
static IgnCar *all_cars[16];
static int n_cars;
static unsigned reported_slope_tracks;
static unsigned reported_gate_tracks;

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
    static const IgnAiParams base = {0.9, 0.8, 0.4, 0.6, 0.75, 0.5, 5.0, 0.6, 0};
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
        /* 0x1b0bf..0x1b0f7 uses the unpermuted track id for the selector only. */
        ai->route_selector = lane_choices[track_id][i].selector;
        ai->route_random = (double)ign_fx_rand15(fx) * (1.0 / 32768.0);
        if (choices[i].chance == 0)
            ai->route_choice = 0;
        else if (choices[i].chance == 100)
            ai->route_choice = 1;
        else
            ai->route_choice =
                (ai->p.s20 * 0.7 + 0.3) * ai->route_random * choices[i].chance * 0.01 > 0.125;
        break;
    }
}

static double wrap(double a) {
    while (a > 3.1415926535898) a -= 6.2831853071796;
    while (a < -3.1415926535898) a += 6.2831853071796;
    return a;
}

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

static int reverse_zone(const IgnAi *ai, int section) {
    int s = abs(section);
    return (track_id == 1 && s > 0x54 && s < 0x5f && ai->side < 0) ||
           (track_id == 4 && section > 0x3d && section < 0x41) ||
           (track_id == 5 && s >= 0x92 && s <= 0x94);
}

static void stuck_step(IgnAi *ai, const IgnCar *car, int section, double road_error, int ncars) {
    if (fabs(road_error) > 1.4 || (car->body.v_fwd < 6.944444444444445 && !car->airborne) ||
        (ai->stuck > 43.2 && reverse_zone(ai, section)))
        ai->stuck += ncars;
    else
        ai->stuck = 0;
    if (ai->stuck >= 151.2) ai->stuck = 0;
}

void ign_ai_stuck_step(IgnAi *ai, const IgnCar *car, double road_error, int ncars) {
    stuck_step(ai, car, car->section, road_error, ncars);
}

typedef struct {
    double lx, lz, rx, rz;
} Edge;

/* 0x1b29c..0x1b3ce: restore both edges of a split gate, without resolving its section again. */
static Edge full_edge(const IgnTrack *t, int s, const IgnAiRouteRecord *record) {
    Edge edge = {record->point[0], record->point[2], record->point[3], record->point[5]};
    const IgnAiRouteRecord *first = ign_track_ai_record(t, s, 0);
    const IgnAiRouteRecord *second = ign_track_ai_record(t, s, 1);
    if (record->tri_type == 1 || record->tri_type == 2) {
        const IgnAiRouteRecord *left = record->tri_type == 1 ? second : first;
        const IgnAiRouteRecord *right = record->tri_type == 1 ? first : second;
        if (left && right && left->valid && right->valid) {
            edge.lx = left->point[0], edge.lz = left->point[2];
            edge.rx = right->point[3], edge.rz = right->point[5];
        } else if (!(reported_gate_tracks & (1u << track_id))) {
            /* As with the slope, do not reconstruct an edge from an invalid one-byte record. */
            fprintf(stderr, "AI: keeping selected TRI gate on track %d (invalid split endpoint)\n", track_id);
            reported_gate_tracks |= 1u << track_id;
        }
    }
    return edge;
}

/* One-pass wrap (e.g. 0x1b4b3..0x1b4e5): the -pi test first, then +pi. */
static double original_wrap(double angle) {
    if (angle < -3.1415926535898) angle += 6.2831853071796;
    if (angle > 3.1415926535898) angle -= 6.2831853071796;
    return angle;
}

double ign_ai_travel(double heading, double motion) {
    /* Control word 0x127f rounds every x87 sum to binary64 before the comparisons. */
    double travel = original_wrap(heading + 0.15 * original_wrap(motion - heading));
    double separation = fabs(heading - motion);
    if (separation > 3.1415926535898) separation = 6.2831853071796 - separation;
    return separation > 0.78539816339745 ? heading : travel;
}

int ign_ai_lookahead_section(int section, int k, int count) {
    int s = section + look_off[k];
    if (s >= count) s -= count;
    if (s >= count - 3) s = count - 1;
    return s;
}

/* 0x1b50d..0x1b536: 32-bit a + (b - a) / 2, truncating towards zero. */
static int32_t record_mid(int32_t a, int32_t b) {
    int32_t half = (int32_t)((uint32_t)b - (uint32_t)a) / 2;
    return (int32_t)((uint32_t)a + (uint32_t)half);
}

/* t_surface_grip (0xc33f0, 30 doubles) indexed by the signed TRI byte. The seven tracks also index
 * read-only words outside the table: -21, 79, 90 and 97 (raw little-endian bit patterns below). */
static double record_grip(int index) {
    static const struct {
        int index;
        uint64_t bits;
    } outside[] = {{-21, 0}, {79, 0x000000390000000dull}, {90, 0x000000160000000dull}, {97, 54}};
    if (index >= 0 && index < 30) return ign_surface_grip[index];
    for (size_t i = 0; i < sizeof outside / sizeof outside[0]; i++) {
        if (outside[i].index != index) continue;
        double value;
        memcpy(&value, &outside[i].bits, sizeof value);
        return value;
    }
    return 0; /* Deviation: other out-of-table bytes are not reached by the shipped TRI files. */
}

void ign_ai_target(const IgnAiRouteRecord *current, const IgnAiRouteRecord *const ahead[6],
                   double s38, double car_x, double car_z, IgnAiTarget *out) {
    /* 0x1b4a2..0x1b4e9: centre-heading change to the first lookahead record. */
    out->first = original_wrap((double)ahead[0]->heading[1] - (double)current->heading[1]);

    /* 0x1b4f1..0x1b6f6: from the farthest record inwards, aim at its integer centre and accept it when,
     * for every record up to it, the centre is seen from the two inset points on opposite sides of
     * the aim. A blocking record keeps the side of its left inset; the last tested centre is kept. */
    out->index = 0;
    out->sign = 0;
    for (int k = 5; k > 0; k--) {
        const IgnAiRouteRecord *target = ahead[k];
        out->x = record_mid(target->point[0], target->point[3]);
        out->z = record_mid(target->point[2], target->point[5]);
        out->aim = atan2(out->z - car_z, out->x - car_x);
        int j = 0;
        for (; j <= k; j++) {
            const int32_t *inset = ahead[j]->inset;
            double left = original_wrap(atan2(out->z - inset[1], out->x - inset[0]) - out->aim);
            double right = original_wrap(atan2(out->z - inset[3], out->x - inset[2]) - out->aim);
            if (!(0 > left * right)) {
                out->sign = 0 > left ? -1 : 1;
                break;
            }
        }
        if (j > k) {
            out->index = k;
            break;
        }
    }
    out->offset = look_off[out->index];

    /* 0x1b721..0x1b75d: grip term of the current record's TRI byte, clamped at zero. */
    out->grip = (8.0 - record_grip(current->surface)) * 0.125;
    if (0 > out->grip) out->grip = 0;

    /* 0x1b764..0x1b821: squared centre-heading changes between min(index + 1, 5) record pairs. */
    int pairs = out->index + 1 > 5 ? 5 : out->index + 1;
    out->curve = 0;
    for (int j = 0; j < pairs; j++) {
        double turn = fabs((double)ahead[j]->heading[1] - (double)ahead[j + 1]->heading[1]);
        if (turn > 3.1415926535898) turn = fabs(turn + -6.2831853071796);
        double weight = (s38 * 0.4 + 0.6) * 1.6;
        out->curve = turn * (weight * turn) / pairs + out->curve;
    }
}

void ign_ai_drive(IgnAi *ai, const IgnCar *car, const IgnTrack *t, IgnFx *fx, IgnInput *in) {
    *in = (IgnInput){0};
    in->analog = true;
    if (!t->ai_records || t->count < 4) {
        fprintf(stderr, "AI: route records are not initialized\n");
        return;
    }
    const IgnBody *b = &car->body;
    IgnAiParams p = ai->p;
    int diff = ign_get_difficulty();
    p.s00 *= diff == 1 ? 0.7 : diff >= 2 ? 0.4 : 1.0; /* load_tri 0x1ceef / 0x1cf06 */
    int section = car->section;
    if (section >= t->count)
        section = track_id == 0 && ai->section > 0x50 && ai->section < 0x5e ? 0x5e : ai->section;
    int s = abs(section);
    int slot = section < 0;
    if (s >= t->count) {
        fprintf(stderr, "AI: section %d is outside the route (%d sections)\n", section, t->count);
        return;
    }
    int idx = track_idx[track_id];
    double cx = (double)car->x + IGN_SRF_ORIGIN, cz = (double)car->z + IGN_SRF_ORIGIN;
    const IgnAiRouteRecord *second = ign_track_ai_record(t, s, 1);
    if (second && second->valid && (second->tri_type == 1 || second->tri_type == 2)) {
        if (ai->route_selector || t->section_obj[s] != t->section_obj_b[s])
            slot = ai->route_choice;
        else
            slot = second->tri_type == 1 ? ai->side < 0 : ai->side > 0;
    }
    /* Route decisions precede the new side ratio and affect the next control turn's slot. */
    ign_ai_update_route_choice(ai, s, fx);
    const IgnAiRouteRecord *current = ign_track_ai_record(t, s, slot);
    if (!current || !current->valid) {
        fprintf(stderr, "AI: current section %d slot %d has no valid TRI record\n", s, slot);
        return;
    }
    const IgnAiRouteRecord *ahead[6];
    for (int k = 0; k < 6; k++) {
        int sk = ign_ai_lookahead_section(s, k, t->count);
        int resolved, branch, object;
        int path = second ? slot : ai->route_choice;
        if (!ign_track_find_section(t, sk, path, 1, track_id, &resolved, &branch, &object)) {
            fprintf(stderr, "AI: no valid lookahead from section %d slot %d\n", sk, path);
            return;
        }
        ahead[k] = ign_track_ai_record(t, resolved, branch);
    }
    Edge e0 = full_edge(t, s, current);
    Edge e1 = full_edge(t, (s + 1) % t->count, ahead[0]);
    double dx = e0.lx - cx, dz = e0.lz - cz;
    double dl = sqrt(dz * dz + dx * dx);
    dx = e1.lx - cx, dz = e1.lz - cz;
    dl += sqrt(dz * dz + dx * dx);
    dx = e0.rx - cx, dz = e0.rz - cz;
    double dr = sqrt(dz * dz + dx * dx);
    dx = e1.rx - cx, dz = e1.rz - cz;
    dr += sqrt(dz * dz + dx * dx);
    double ratio = dl / dr;
    ai->side = ratio < 1 ? -1 / ratio : ratio;
    IgnAiTarget target;
    ign_ai_target(current, ahead, p.s38, cx, cz, &target);
    int off = target.offset;
    /* 0x1b823..0x1b86b */
    double a8 = 1 + (target.grip + 0.8) * target.curve;

    /* Curve speed override of the current section (last matching entry wins). */
    int key = section & ~1, override = -1;
    for (int i = 0; i < 8; i++)
        if (limits[idx][i].speed && limits[idx][i].key == key) override = limits[idx][i].speed;

    /* Throttle (0x1b9a2-0x1bad1): T is the pedal fraction, scaled by 100 at the end. */
    double kmh = car->kmh;
    int slope_section = (s + off) % t->count;
    const IgnAiRouteRecord *slope_record = ign_track_ai_record(t, slope_section, slot);
    if (!slope_record) slope_record = ign_track_ai_record(t, slope_section, 0);
    if (!slope_record || !slope_record->valid) {
        /* Native deviation: the original reads beyond a one-byte invalid TRI placeholder here.
         * Resolve a valid successor rather than using undefined heap bytes as a height. */
        int resolved, branch, object;
        if (!ign_track_find_section(t, slope_section, slot, 1, track_id,
                                    &resolved, &branch, &object)) {
            fprintf(stderr, "AI: no valid slope record from section %d slot %d\n", slope_section, slot);
            return;
        }
        slope_record = ign_track_ai_record(t, resolved, branch);
        if (!(reported_slope_tracks & (1u << track_id))) {
            fprintf(stderr, "AI: resolving invalid TRI slope record on track %d (original heap overread)\n",
                    track_id);
            reported_slope_tracks |= 1u << track_id;
        }
    }
    double slope = ((current->point[1] - slope_record->point[1]) * (p.s40 * 1.1)) / (off * 40.0);
    slope += 1;
    if (slope < 1) slope = 1;
    if (slope > 1) slope = sqrt(slope);
    double T = (((1 + p.s00) * 0.5 + 0.3) * slope) /
               (p.s10 * 6 / (3.0 * target.index + 6) + 1);
    T /= (a8 - 1) * 0.4 + 1;
    double cap = p.s18 * 60 + 120;
    double x88 = fmax(0, cap - kmh) / cap * (p.s18 * 1.5) + 1;
    T = (T * x88 + (x88 - 1) * 0.25) * 100;
    if (T > 100) T = 100;
    int brake = 0, force_turbo = 0;
    if (override == 500) {
        T = 100;
        force_turbo = diff < 1;
    } else if (override != -1 && override * 0.27777777777777773 < b->v_fwd) {
        brake = (int)clampd((kmh - override) * 50 * 0.01, 0, 50);
    }
    double A = fmin(T * 1.5, 100);

    /* Steering (0x1c07a...): aim at the lookahead mid-point from the blended travel direction. */
    double h = car->drive_heading;
    double dirt = (car->step_x != 0 || car->step_z != 0) ? atan2(car->step_z, car->step_x) : 0;
    double travel = ign_ai_travel(h, dirt);
    double c0 = original_wrap(target.aim - travel);
    double dir = ((a8 - 1) * 0.25 + 1) * (c0 * 75);
    if (car->spec == 5 || car->spec == 8) dir *= 1.2;
    dir *= diff == 1 ? 0.9 : diff == 2 ? 0.8 : diff == 3 ? 0.75 : 1;
    dir = clampd(dir, -100, 100);

    /* Nearest rival (0x1c1b0-0x1c3d0): steer around it when it is almost straight ahead. */
    int best = -1;
    double bestd = 100000;
    for (int i = 0; i < n_cars; i++) {
        if (all_cars[i] == car) continue;
        double dx = (double)car->x - all_cars[i]->x, dz = (double)car->z - all_cars[i]->z;
        double d = sqrt(dz * dz + dx * dx);
        if (d < bestd) bestd = d, best = i;
    }
    if (best >= 0) {
        const IgnCar *other = all_cars[best];
        double rel = wrap(atan2(other->axle[0][1] - car->axle[1][1],
                               other->axle[0][0] - car->axle[1][0]) - travel + 1.5707963267949);
        double r = p.s28 * 800;
        if (rel > 1.2707963267949 && rel < 1.8707963267949 && r > bestd) {
            rel = original_wrap(rel - 1.5707963267949);
            double v = rel < 0 ? -1.5707963267949 : 1.5707963267949;
            double push = -(v - 3 * rel) * 40;
            push = (r - bestd) / r * push;
            push = clampd(push, -100, 100);
            if ((other->ai_side < -1.9 && push < 0) || (other->ai_side > 1.9 && push > 0))
                push = -push;
            dir += (push - dir) / a8;
        }
    }

    /* Hug the inside edge of the next bend (0x1c40d-0x1c5be), from the last blocking side. */
    if (target.index > 2 && a8 < 2 && fabs(target.first) < 0.3) {
        if (target.sign == -1) {
            double tg = fmin(50, (a8 - 1) * 50);
            double dv = ai->side >= 0 ? ai->side * 2.5 : 2.5;
            dir += (tg - dir) / dv;
        } else if (target.sign == 1) {
            double tg = fmax(-50, (a8 - 1) * -50);
            double dv = ai->side > 0 ? 2.5 : ai->side * -2.5; /* 0x1c57a..0x1c5a4 */
            dir += (tg - dir) / dv;
        }
    }
    if (car->flat && 32.4 - diff * 9.0 > car->flat) dir = -dir;
    ai->steer = dir;

    /* 0x1bfc1: TRI road angle minus blended travel, not the lookahead aiming angle. */
    stuck_step(ai, car, section, original_wrap(current->heading[1] - travel), n_cars);
    int force_back = reverse_zone(ai, section);
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
        ai->accel = (int)T;
        ai->section = s;
        return;
    }
    if (ai->stuck > 97.19999999999997 && ai->stuck < 108) ai->steer = 0;
    if (car->airborne) ai->steer = 0;

    /* Turbo (0x1ca15): full charge, on the ground, no braking; a hard steering input in a gentle
     * bend, or an unlimited-speed section on the easiest difficulty. */
    int want = (p.s20 * 0.13 + 1.07 > a8 && off > 3 && fabs(ai->steer) < p.s20 * 5 + 3) || force_turbo;
    int sec = section;
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
        car->ai_side = ai->side;
        ai->input.manual_gear = manual;
        ai->turbo_request = ai->input.turbo;
    }
    ign_car_apply_input(car, &ai->input, ai->turbo_request);
    return action;
}
