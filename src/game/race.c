#include "race.h"
#include <float.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PI 3.14159265358979323846

/* 0xc0678: ease-in-out curve read by sub_000413fc (index = f * 200, value * 0.005). */
static const unsigned char EASE[201] = {
    0,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   3,   3,   3,   3,   3,   4,   4,
    4,   5,   5,   5,   6,   6,   6,   6,   7,   7,   7,   8,   8,   9,   9,   9,   10,  10,  11,  11,  11,
    12,  12,  13,  13,  14,  14,  14,  15,  16,  16,  17,  17,  18,  19,  19,  20,  21,  21,  22,  23,  24,
    25,  26,  27,  28,  29,  29,  30,  31,  32,  34,  35,  37,  38,  40,  42,  44,  45,  47,  48,  50,  52,
    53,  55,  58,  60,  61,  63,  65,  67,  69,  70,  73,  76,  79,  80,  82,  84,  87,  89,  90,  93,  95,
    98,  99,  101, 103, 106, 108, 109, 112, 115, 118, 119, 122, 125, 128, 129, 131, 133, 135, 137, 138, 140,
    143, 145, 146, 148, 151, 153, 154, 156, 158, 160, 161, 162, 163, 164, 165, 166, 167, 169, 170, 171, 172,
    174, 175, 176, 177, 177, 178, 179, 180, 181, 181, 182, 183, 184, 184, 185, 185, 186, 187, 187, 188, 188,
    189, 190, 190, 190, 191, 191, 192, 192, 192, 193, 194, 194, 194, 195, 195, 195, 196, 196, 196, 196, 197,
    197, 197, 198, 198, 198, 199, 199, 199, 199, 199, 199, 199,
};

/* sub_0003a788 0x3b004: two sections ahead of car+0x364 (section 0 past the end), on branch B its object. The point
 * is the mean vertex of the object's faces of type < 0x28, 250 above them; the heading is the section angle - pi/2.
 * Returns the object, -1 if there is none. */
static int respawn_point(const IgnLevel *lv, const IgnTrack *t, int section, double out[4]) {
    int s = section == IGN_NO_SECTION ? 0 : abs(section) + 2;
    if (s >= t->count) s = 0;
    int sec = section < 0 && s > 0 ? -s : s;
    int o = ign_track_section_object(t, sec);
    if (o < 0) o = ign_track_section_object(t, sec = s);
    double ang;
    if (o < 0 || !ign_track_section_angle(t, lv, sec, &ang)) return -1;
    const IgnObject *obj = &lv->track.objs[o];
    double sx = 0, sy = 0, sz = 0;
    int n = 0;
    for (int f = 0; f < obj->nf; f++) {
        if ((obj->faces[f].flags >> 16) >= 0x28) continue;
        for (int k = 0; k < 3; k++) {
            const int32_t *v = obj->verts + obj->faces[f].v[k] * 3;
            sx += v[0] + obj->pos[0];
            sy += -(v[1] + obj->pos[1]);
            sz += v[2] + obj->pos[2];
            n++;
        }
    }
    if (n) {
        out[0] = trunc(sx / n);
        out[1] = trunc(sy / n) + 250.0;
        out[2] = trunc(sz / n);
    } else {
        out[0] = obj->pos[0];
        out[1] = -obj->pos[1] + 250.0;
        out[2] = obj->pos[2];
    }
    out[3] = ang - PI / 2;
    if (out[3] < 0) out[3] += 2 * PI;
    return o;
}

/* sub_00041430 puts the car at the respawn point and sets its section (car+0x364/+0x368) to that object's, so a car
 * that wrecks again respawns two sections further on. Deviation: where the port's ground lookup has a gap (CARIB
 * sections 1-4) the following sections are tried. */
static void respawn(IgnCar *c, const IgnLevel *lv, const IgnTrack *t, int obj, const double to[4]) {
    double at[4] = {to[0], to[1], to[2], to[3]};
    int s = c->section == IGN_NO_SECTION ? 0 : c->section;
    for (int k = 0; k < 8; k++) {
        if (obj >= 0 && ign_car_respawn(c, lv, (float)at[0], (float)at[2], at[3], at[1]) == 0) {
            if (t->section[obj] != IGN_NO_SECTION) c->section = t->section[obj];
            if (getenv("IGN_TRACE"))
                fprintf(stderr, "respawn car %d section %d at %.0f %.0f h=%.0f heading %.2f\n", c->id, c->section,
                        at[0], at[2], at[1], at[3]);
            return;
        }
        s += s < 0 ? -1 : 1;
        obj = respawn_point(lv, t, s, at);
    }
}

/* sub_0003db4c frames 0-30: the car slides min(30 - f, 10) along +x and +z (with its saved pose), climbs 4 for ten
 * frames and then drops 10 per frame while tumbling 0.2 / 0.25 rad per frame (car+0x108/+0x110). */
static void slide_off(IgnCar *c, const IgnLevel *lv) {
    while (c->wreck_slid <= 30 && c->wreck_slid <= (int)c->wreck_frame) {
        int f = c->wreck_slid++;
        int d = 30 - f > 10 ? 10 : 30 - f;
        c->wreck_from[0] += d;
        c->wreck_from[2] += d;
        double roll = c->body_roll + 0.2, pitch = c->body_pitch + 0.25;
        ign_car_set_pose(c, lv, c->x + (float)d, c->z + (float)d, c->body.heading, c->height + (f < 10 ? 4.0 : -10.0));
        c->body_roll = roll, c->body_pitch = pitch;
    }
}

void ign_race_wrecks(IgnCar *const *cars, int n, const IgnLevel *lv, const IgnTrack *t) {
    for (int i = 0; i < n; i++) {
        IgnCar *c = cars[i];
        if (!c->wrecked) continue;
        ign_car_wreck_turbo(c);
        double to[4];
        int obj = respawn_point(lv, t, c->section, to);
        if (obj < 0) continue;
        if (c->wreck_frame < IGN_WRECK_FRAMES) c->wreck_frame += IGN_PHYS_DT * 36.0;
        if (c->wreck_kind == 3) slide_off(c, lv);
        double rem = IGN_WRECK_FRAMES - c->wreck_frame;
        if (rem < 40.0) {
            /* 0x3b39d: glide over the last 40 frames, eased in x/z, linear in height and heading. */
            double f = (40.0 - rem) * 0.025;
            if (f > 1.0) f = 1.0;
            double e = EASE[(int)(f * 200.0)] * 0.005;
            const double *from = c->wreck_from;
            double h = to[3];
            if (fabs(h - 2 * PI - from[3]) < fabs(h - from[3])) h -= 2 * PI;
            h = from[3] + (h - from[3]) * f;
            ign_car_set_pose(c, lv, (float)(from[0] + (to[0] - from[0]) * e), (float)(from[2] + (to[2] - from[2]) * e),
                             h, from[1] + (to[1] - from[1]) * f);
        }
        if (c->wreck_frame < IGN_WRECK_FRAMES) continue;
        /* 0x3b53b: wait while another running car is within 120 units of this one on every axis; 0x3b5d2: cars that
         * are out (eliminated or finished) wait for good. */
        int blocked = c->out;
        for (int j = 0; j < n; j++)
            if (j != i && !cars[j]->wrecked && fabs(cars[j]->x - c->x) < 120.0 &&
                fabs(cars[j]->height - c->height) < 120.0 && fabs(cars[j]->z - c->z) < 120.0)
                blocked = 1;
        if (!blocked) respawn(c, lv, t, obj, to);
    }
}

void ign_race_init(IgnRace *r, int mode, int ncars) {
    r->mode = mode;
    r->tick = 0;
    r->finished = 0;
    r->remaining = ncars;
    r->clock = 0;
    r->total = 0;
    r->timer_ticks = r->origin_ms = 0;
    r->elapsed = 0;
    r->best_lap = DBL_MAX;
    r->ai_parity = r->ai_car = 0;
}

IgnAiControlAction ign_race_ai_action(const IgnRace *r, const IgnCar *car, int index, int computer) {
    if (car->wrecked || r->ai_parity != 1 || (!computer && car->progress_lock != 1))
        return IGN_AI_CONTROLS_NONE;
    if (index == r->ai_car && (r->clock < 0 || r->clock > IGN_RACE_AI_WAKE))
        return IGN_AI_CONTROLS_DRIVE;
    return IGN_AI_CONTROLS_REPLAY;
}

void ign_race_ai_step(IgnRace *r, int ncars) {
    /* 0x2dd99..0x2ddba: advance before toggling, even when the selected car cannot drive. */
    if (r->ai_parity == 0 && ++r->ai_car >= ncars) r->ai_car = 0;
    r->ai_parity ^= 1;
}

void ign_race_skip_countdown(IgnRace *r) {
    r->clock = -1.0;
    r->total = 250.0;
    r->origin_ms = r->timer_ticks * 1000 / 72;
    r->elapsed = 0;
}

static int32_t signed_timer(uint32_t value) {
    int64_t signed_value = value;
    if (value > INT32_MAX) signed_value -= INT64_C(4294967296);
    return (int32_t)signed_value;
}

static int32_t timer_difference(int32_t now, int32_t origin) {
    return signed_timer((uint32_t)now - (uint32_t)origin);
}

double ign_race_elapsed_ms(int32_t now, int32_t origin) {
    return timer_difference(now, origin) * 0.001;
}

double ign_race_clock_sample(double previous, int32_t now, int32_t origin) {
    if (previous <= -1.0) return previous;
    double clock = timer_difference(now, origin) * 0.036;
    return clock >= 200.0 ? -1.0 : clock;
}

void ign_race_car_clock(IgnCar *car, int mode, double elapsed) {
    if (car->out || (mode != IGN_MODE_ELIMINATION && car->lap >= IGN_RACE_LAPS)) return;
    car->race_elapsed = elapsed;
    car->lap_elapsed = elapsed - car->lap_origin;
}

void ign_race_reset_car_clock(IgnCar *car) {
    car->race_elapsed = 0;
    car->last_lap = 0;
    for (int i = 0; i < IGN_RACE_LAPS; i++) car->lap_times[i] = 0;
}

void ign_race_clock(IgnRace *r) {
    double step = IGN_PHYS_DT * 36.0;
    r->total += step;
    int was_started = ign_race_started(r);
    int64_t now = ++r->timer_ticks * 1000 / 72;
    int32_t sampled_ms = signed_timer((uint32_t)now);
    int32_t origin_ms = signed_timer((uint32_t)r->origin_ms);
    r->elapsed = ign_race_elapsed_ms(sampled_ms, origin_ms);
    /* Virtual 72 Hz source, not the DOS interrupt timer; sample after the shared physics tick. */
    r->clock = ign_race_clock_sample(r->clock, sampled_ms, origin_ms);
    if (!was_started && ign_race_started(r)) r->origin_ms = now;
}

int ign_race_started(const IgnRace *r) { return r->clock < 0; }

int ign_race_countdown(const IgnRace *r) {
    if (r->clock >= 150.0) return 1;
    if (r->clock >= 100.0) return 2;
    if (r->clock >= 50.0) return 3;
    if (ign_race_started(r) && r->total < 250.0) return 0;
    return -1;
}

static void finish(IgnRace *r, IgnCar *c) {
    c->out = 1;
    c->finish_position = c->position;
    c->finish_tick = r->tick;
    c->place = ++r->finished;
}

int ign_race_progress_sample(IgnRace *r, IgnCar *car, int count, int lap_probe, int section_sample) {
    int event = 0;
    /* 0x2f2be..0x2f436: the crossing uses the previous lap probe, before updating either section. */
    if (car->phase == 3 && lap_probe == 0 && abs(car->lap_section) == count - 1 &&
        car->progress_lock == 0) {
        car->lap++;
        car->phase = 0;
        event = 1;
        if (car->lap >= IGN_RACE_LAPS && r->mode != IGN_MODE_ELIMINATION) {
            finish(r, car);
            event |= 2;
        }
        car->last_lap = car->lap_elapsed;
        if (car->lap < 4) car->lap_times[car->lap - 1] = car->lap_elapsed;
        car->lap_origin = r->elapsed;
        if (car->last_lap < r->best_lap) {
            r->best_lap = car->last_lap;
            event |= 4;
        }
    }
    if (lap_probe != IGN_NO_SECTION) car->lap_section = lap_probe;
    if (section_sample != IGN_NO_SECTION) car->section = section_sample;
    ign_car_progress_phase(car, count);
    return event;
}

void ign_race_progress(IgnRace *r, IgnCar *car, const IgnTrack *t) {
    if (car->out || car->wrecked || t->count <= 0) return;
    int sample = IGN_NO_SECTION;
    if (car->ground_object >= 0 && car->ground_object < t->nobj)
        sample = t->section[car->ground_object];
    ign_race_progress_sample(r, car, t->count, sample, sample);
}

int ign_race_step(IgnRace *r, IgnCar *const *cars, int n) {
    r->tick++;

    if (r->mode == IGN_MODE_ELIMINATION) {
        /* sub_0002de30: with exactly one racing car a lap behind the leader, it is out. */
        int max_lap = 0, behind = 0, last = -1;
        for (int i = 0; i < n; i++)
            if (!cars[i]->out && cars[i]->lap > max_lap) max_lap = cars[i]->lap;
        for (int i = 0; i < n; i++)
            if (!cars[i]->out && cars[i]->lap < max_lap) behind++, last = i;
        if (behind == 1 && cars[last]->blow == 0.0) {
            IgnCar *c = cars[last];
            c->wrecked = 1;
            c->blow = 1.0;
            finish(r, c);
            if (--r->remaining == 1)
                for (int i = 0; i < n; i++)
                    if (!cars[i]->out) finish(r, cars[i]);
        }
    }
    int racing = 0;
    for (int i = 0; i < n; i++) racing += !cars[i]->out;
    return racing;
}

int ign_race_position(const IgnCar *const *cars, int n, int index) {
    const IgnCar *me = cars[index];
    int pos = 1;
    for (int i = 0; i < n; i++) {
        if (i == index) continue;
        const IgnCar *o = cars[i];
        int ahead;
        /* Finishers rank first, eliminated cars last (the earliest eliminated is the lowest). */
        int ro = o->wrecked ? -1 : o->out, rm = me->wrecked ? -1 : me->out;
        if (ro != rm) ahead = ro > rm;
        else if (o->out) ahead = o->wrecked ? o->place > me->place : o->place < me->place;
        else if (o->lap != me->lap) ahead = o->lap > me->lap;
        else if (o->phase != me->phase) ahead = o->phase > me->phase;
        else ahead = abs(o->section) > abs(me->section);
        pos += ahead;
    }
    return pos;
}
