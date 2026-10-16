#include "race.h"
#include "save.h"
#include <float.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846

/* 0xc45b4 and 0xc3fd8; track IDs are GST IDs, not menu display order. */
static const int CHAMP_TRACKS[4][7] = {
    {0, 1, 2, 3, 4, 0, 0}, {3, 2, 0, 4, 1, 5, 0},
    {4, 3, 5, 1, 0, 2, 6}, {5, 0, 4, 6, 2, 1, 3}
};
static const int TRACK_MUSIC[7] = {2, 3, 4, 5, 6, 6, 7};

int ign_race_config_decode(IgnRaceConfig *out, const unsigned char *image, size_t size,
                           int cd_tracks, double (*random01)(void)) {
    if (!out || !image || size != IGN_BTZ_SIZE) return -1;
    IgnBtz b;
    memcpy(b.raw, image, sizeof b.raw);
    IgnRaceConfig c = {0};
#define CFG(off) ign_btz_i32(&b, (off))
    c.mode = CFG(IGN_BTZ_MODE);
    c.difficulty = CFG(IGN_BTZ_CLASS);
    c.round = CFG(IGN_BTZ_TRACK);
    c.ncars = CFG(IGN_BTZ_NCARS);
    c.players_minus_one = CFG(IGN_BTZ_PLAYERS);
    /* Original trusts these indices; reject them rather than reading outside fixed tables. */
    if (c.mode < 0 || c.mode > 3 || c.difficulty < 0 || c.difficulty > 3 ||
        c.round < 0 || c.round >= 7 || c.ncars < 1 || c.ncars > IGN_RACE_MAX_CARS ||
        c.players_minus_one < 0 || c.players_minus_one > 1 ||
        c.players_minus_one >= c.ncars) return -1;
    c.track = c.mode == 0 ? CHAMP_TRACKS[c.difficulty][c.round] : c.round;
    c.human_count = c.players_minus_one + 1;
    c.ghost = CFG(IGN_BTZ_GHOST);
    if (c.ghost == 1 && c.mode != 2) c.ghost = 0;
    c.value_107 = CFG(0x107);
    c.value_10b = CFG(0x10b);
    c.value_197 = CFG(0x197);
    c.value_15b = CFG(0x15b);
    c.value_10f = CFG(0x10f);
    c.value_113 = CFG(0x113);
    c.value_117 = CFG(0x117);
    c.value_014 = c.mode == 0 ? 1 : CFG(0x014);
    c.music_track = CFG(0x0f7);
    if (c.music_track == 0) {
        c.music_track = TRACK_MUSIC[c.track];
        if (c.music_track > cd_tracks) c.music_track = cd_tracks;
    } else if (c.music_track == -1) {
        if (!random01) return -1;
        double random = random01();
        double track = random * cd_tracks + 1.0;
        if (!isfinite(track) || track < INT32_MIN || track > INT32_MAX) return -1;
        c.music_track = (int)track;
    }
    int music = CFG(0x0fb), effects = CFG(0x0ff);
    if (music > INT32_MAX / 10 || music < INT32_MIN / 10 ||
        effects > INT32_MAX / 10 || effects < INT32_MIN / 10) return -1;
    c.music_volume = music * 10;
    c.effects_volume = effects * 10;
    c.effects_enabled = c.effects_volume != 0;
    int noncomputer = 0;
    for (int i = 0; i < c.ncars; i++) {
        IgnRaceParticipant *p = &c.participants[i];
        c.grid[i] = CFG(IGN_BTZ_GRID + i * 4);
        p->model = CFG(IGN_BTZ_CAR + i * 4);
        p->automatic = CFG(0x048 + i * 4);
        p->computer = CFG(IGN_BTZ_HUMAN + i * 4);
        p->gear_delay = 30;
        ign_btz_name(&b, i, p->name);
        if (i == 0 || (i == 1 && c.players_minus_one == 1)) {
            memcpy(p->keys, b.raw + 0x17f + i * 8, sizeof p->keys);
            if (c.difficulty >= 3) {
                unsigned char key = p->keys[0];
                p->keys[0] = p->keys[1];
                p->keys[1] = key;
            }
            p->input_device = CFG(0x123 + i * 4);
            p->joystick = CFG(0x12b + i * 4);
        }
        noncomputer += p->computer != 1;
    }
    if (noncomputer > 1) c.ghost = 0;
#undef CFG
    *out = c;
    return 0;
}

int ign_race_config_load(IgnRaceConfig *out, const char *path, int cd_tracks, double (*random01)(void)) {
    IgnBtz b;
    if (ign_btz_load(path, &b)) return -1;
    return ign_race_config_decode(out, b.raw, sizeof b.raw, cd_tracks, random01);
}

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
        if (obj >= 0 && ign_car_respawn(c, lv, (float)at[0], (float)at[2], at[3], at[1],
                                        (int)((lv->track.objs[obj].type >> 12) & 0xf)) == 0) {
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
    memset(r, 0, sizeof *r);
    ign_race_reset(r, mode, ncars, 0);
    r->best_lap = DBL_MAX;
    for (int i = 0; i < IGN_RACE_MAX_CARS; i++) r->grid[i] = i;
}

void ign_race_reset(IgnRace *r, int mode, int ncars, int round) {
    if (mode == 0 && round == 0)
        for (int i = 0; i < ncars && i < IGN_RACE_MAX_CARS; i++) r->points[i] = 0;
    r->mode = mode;
    r->tick = r->finished = r->ai_parity = r->ai_car = 0;
    r->total = 0;
    memset(r->result_order, 0, sizeof r->result_order);
    memset(r->result_aux, 0, sizeof r->result_aux);
    r->remaining = ncars;
    r->finish_clock = -1;
    r->last_eliminated = -1;
    r->elimination_limit = mode == IGN_MODE_ELIMINATION ? ncars - 1 : 0;
    r->elimination_count = 0;
    for (int i = 0; i < IGN_RACE_MAX_CARS; i++) {
        r->human_finish[i] = -1;
    }
    for (int i = 0; i < 10; i++) r->hud_slots[0][i] = r->hud_slots[1][i] = 100;
}

void ign_race_configure(IgnRace *r, const IgnRaceConfig *config, int section_count) {
    ign_race_reset(r, config->mode, config->ncars, config->round);
    r->players_minus_one = config->players_minus_one;
    r->section_count = section_count;
    r->ghost_enabled = config->ghost;
    memcpy(r->grid, config->grid, sizeof r->grid);
}

void ign_race_reset_times(IgnRace *r, IgnCar *const *cars, int n, int32_t now, double saved_best_lap) {
    for (int i = 0; i < n; i++) ign_race_reset_car_clock(cars[i]);
    if (n > 0) r->best_lap = saved_best_lap;
    r->origin_ms = now;
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
    r->last_eliminated = -1;

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
            c->wreck_kind = 1;
            c->blow = 1.0;
            finish(r, c);
            r->last_eliminated = last;
            if (cars[0]->out == 1 && (r->players_minus_one == 0 || (n > 1 && cars[1]->out == 1)) &&
                r->finish_clock < 0) {
                r->finish_clock = 0;
                r->human_finish[0] = r->human_finish[1] = 0;
            }
            if (--r->remaining == 1) {
                for (int i = 0; i < n; i++)
                    if (!cars[i]->out) {
                        cars[i]->out = 1;
                        cars[i]->finish_position = cars[i]->position;
                        cars[i]->finish_tick = r->tick;
                        cars[i]->place = n;
                        if (r->finish_clock < 0) {
                            r->finish_clock = 0;
                            r->human_finish[i] = 0;
                        }
                        r->finished = n;
                    }
            }
            if (last == 0 || (last == 1 && r->players_minus_one == 1)) {
                r->finish_clock = 0;
                r->tv_camera = 3;
                r->camera_hold = 100;
                r->human_finish[last] = 0;
            }
        }
    }
    int racing = 0;
    for (int i = 0; i < n; i++) racing += !cars[i]->out;
    return racing;
}

int32_t ign_race_rank_score(const IgnRace *r, const IgnCar *car, int n, int bias) {
    uint32_t span = (uint32_t)r->section_count + 10;
    uint32_t progress = (uint32_t)car->lap * span;
    if (!car->out) {
        uint32_t section = car->section < 0 ? 0u - (uint32_t)car->section : (uint32_t)car->section;
        if (signed_timer(section) < r->section_count / 4 || car->phase != 0) progress += section;
    } else if (r->mode == IGN_MODE_ELIMINATION) {
        progress -= (uint32_t)car->finish_position * 10;
    } else {
        progress += ((uint32_t)n - (uint32_t)car->finish_position) * 10;
    }
    uint32_t score = progress * 30 + (uint32_t)bias;
    if (car->out && r->mode != IGN_MODE_ELIMINATION && !(r->total >= 250.0)) score -= 11;
    return signed_timer(score);
}

void ign_race_rank(IgnRace *r, IgnCar *const *cars, int n) {
    if (n <= 0 || n > IGN_RACE_MAX_CARS) return;
    int order[IGN_RACE_MAX_CARS];
    int32_t score[IGN_RACE_MAX_CARS];
    for (int i = 0; i < n; i++) {
        order[i] = i;
        score[i] = ign_race_rank_score(r, cars[i], n, r->rank_bias[i]);
    }
    for (int end = n - 1; end > 0; end--)
        for (int i = 1; i <= end; i++)
            if (score[i] > score[i - 1]) {
                int32_t s = score[i]; score[i] = score[i - 1]; score[i - 1] = s;
                int o = order[i]; order[i] = order[i - 1]; order[i - 1] = o;
            }
    for (int i = 0; i < n; i++) cars[order[i]]->position = i + 1;
    /* 0x4c4c2..0x4c671: a duplicate FINISH place sorts every car by elapsed time,
     * overwriting even running cars' finish snapshots, without recomputing positions this call. */
    for (int i = 0; i < n; i++)
        if (cars[i]->out == 1)
            for (int j = i + 1; j < n; j++)
                if (cars[j]->out == 1 && cars[i]->finish_position == cars[j]->finish_position) {
                    for (int k = 0; k < n; k++) order[k] = k;
                    for (int end = n - 1; end > 0; end--)
                        for (int k = 0; k < end; k++)
                            if (cars[order[k]]->race_elapsed > cars[order[k + 1]]->race_elapsed) {
                                int o = order[k]; order[k] = order[k + 1]; order[k + 1] = o;
                            }
                    for (int k = 0; k < n; k++) cars[order[k]]->finish_position = k + 1;
                }
    if (!(r->total >= 250.0) && cars[0]->out == 0)
        for (int i = 0; i < n; i++) cars[i]->position = r->grid[i] + 1;
    if (r->ghost_enabled == 1 && cars[0]->out == 0) {
        if (r->ghost_progress > score[0]) {
            r->ghost_position = 1;
            cars[0]->position = 2;
        } else {
            r->ghost_position = 2;
            cars[0]->position = 1;
        }
    }
}

int ign_race_position(const IgnCar *const *cars, int n, int index) {
    return index >= 0 && index < n ? cars[index]->position : 0;
}
