#include "ghost.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TWO_PI 6.283185307179586

/* Track ids of the level switch; the delete list at 0x1705c is in another order. */
static const char *const LEVELS[] = {"CANADA", "USA", "CARIB", "BRAZIL", "AUSTRIA", "ICELAND", "JAPAN"};
static const char *const BASENAMES[] = {"MOOSEJAW", "GOLDRUSH", "SNAKEISL", "LOSTRUIN",
                                        "YODELPEA", "CAPETHOR", "TOKYOBUL"};
#define NTRACKS 7
#define HEADER 10

int ign_ghost_track_id(const char *level) {
    for (int i = 0; i < NTRACKS; i++)
        if (!strcmp(level, LEVELS[i])) return i;
    return -1;
}

const char *ign_ghost_basename(int track) {
    return track >= 0 && track < NTRACKS ? BASENAMES[track] : NULL;
}

static int16_t rd16(const uint8_t *p) {
    return (int16_t)(p[0] | p[1] << 8);
}

static void wr16(uint8_t *p, int16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)((uint16_t)v >> 8);
}

int ign_ghost_load(const char *path, IgnGhost *g) {
    static uint8_t buf[IGN_GHOST_FILE_SIZE + 1];
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (n != IGN_GHOST_FILE_SIZE) return -1;
    memset(g, 0, sizeof *g);
    g->car = rd16(buf);
    g->track = rd16(buf + 2);
    g->time_cs = rd16(buf + 4);
    memcpy(g->name, buf + 6, 3);
    for (int i = 0; i < IGN_GHOST_SAMPLES; i++) {
        const uint8_t *r = buf + HEADER + i * 14;
        IgnGhostSample *s = &g->s[i];
        s->x = rd16(r), s->y = rd16(r + 2), s->z = rd16(r + 4);
        s->roll = rd16(r + 6), s->heading = rd16(r + 8), s->pitch = rd16(r + 10);
        s->progress = rd16(r + 12);
    }
    g->count = IGN_GHOST_SAMPLES;
    return 0;
}

int ign_ghost_save(const char *path, const IgnGhost *g) {
    static uint8_t buf[IGN_GHOST_FILE_SIZE];
    if (g->overflow) return -1;
    memset(buf, 0, sizeof buf);
    wr16(buf, g->car);
    wr16(buf + 2, g->track);
    wr16(buf + 4, g->time_cs);
    /* sprintf("%s") at +6; the samples overwrite anything past the NUL. */
    memcpy(buf + 6, g->name, strnlen(g->name, 3));
    for (int i = 0; i < IGN_GHOST_SAMPLES; i++) {
        uint8_t *r = buf + HEADER + i * 14;
        const IgnGhostSample *s = &g->s[i];
        wr16(r, s->x), wr16(r + 2, s->y), wr16(r + 4, s->z);
        wr16(r + 6, s->roll), wr16(r + 8, s->heading), wr16(r + 10, s->pitch);
        wr16(r + 12, s->progress);
    }
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(buf, 1, sizeof buf, f);
    return fclose(f) || n != sizeof buf ? -1 : 0;
}

void ign_ghost_begin(IgnGhost *g, int car, int track, const char *name) {
    memset(g, 0, sizeof *g);
    g->car = (int16_t)car;
    g->track = (int16_t)track;
    snprintf(g->name, sizeof g->name, "%s", name);
}

/* sub_00051938 + x1024: angle wrapped to [0, 2pi), truncated. */
static int16_t angle(double a) {
    a = fmod(a, TWO_PI);
    if (a < 0) a += TWO_PI;
    return (int16_t)(a * 1024.0);
}

void ign_ghost_record(IgnGhost *g, const IgnCar *car, int sections) {
    if (g->overflow) return;
    IgnGhostSample *s = &g->s[g->count];
    s->x = (int16_t)trunc(car->x);
    s->y = (int16_t)trunc(car->height + (car->wrecked ? 5000.0 : 0.0));
    s->z = (int16_t)trunc(car->z);
    s->roll = angle(car->body.roll);
    s->heading = angle(car->body.heading);
    s->pitch = angle(car->body.pitch);
    int section = abs(car->section);
    int progress = car->lap * (sections + 10) + section;
    /* Early in the lap (phase 0) the section term is dropped beyond the first quarter. */
    if (section >= sections / 4 && car->phase == 0) progress = car->lap * (sections + 10);
    s->progress = (int16_t)progress;
    if (++g->count >= IGN_GHOST_SAMPLES) g->count = 0, g->overflow = 1;
}

void ign_ghost_finish(IgnGhost *g, double race_seconds) {
    g->time_cs = (int16_t)trunc(race_seconds * 100.0);
    if (g->count <= 0) return;
    const IgnGhostSample *last = &g->s[g->count - 1];
    for (int i = g->count; i < IGN_GHOST_SAMPLES; i++) {
        int16_t progress = g->s[i].progress;
        g->s[i] = *last;
        g->s[i].progress = progress;
    }
}

void ign_ghost_pose(const IgnGhost *g, int tick, IgnCar *car) {
    if (g->count <= 0) return;
    if (tick < 0) tick = 0;
    if (tick >= g->count) tick = g->count - 1;
    const IgnGhostSample *s = &g->s[tick];
    car->x = s->x;
    car->z = s->z;
    car->height = car->ground = s->y;
    car->y = (float)-car->height;
    car->body.heading = s->heading / 1024.0;
    car->heading = (float)(TWO_PI / 4.0 - car->body.heading);
    car->body.roll = car->body_roll = s->roll / 1024.0;
    car->body.pitch = car->body_pitch = s->pitch / 1024.0;
    car->steer_visual = 0;
}

IgnTrialGhost ign_ghost_trial_load(const char *path, int track, IgnGhost *g, IgnGhost *rec, int *record_cs) {
    if (ign_ghost_load(path, g)) {
        *record_cs = IGN_GHOST_NO_RECORD;
        return IGN_TRIAL_NONE;
    }
    /* The record is taken from the header before the track is checked (0x2ae93). */
    *record_cs = g->time_cs;
    if (g->track != track) {
        rec->overflow = 1; /* 0x2b410 */
        return IGN_TRIAL_MISMATCH;
    }
    return IGN_TRIAL_GHOST;
}

int ign_ghost_trial_beats(const IgnGhost *rec, int record_cs, double race_seconds) {
    return !rec->overflow && record_cs * 0.01 > race_seconds;
}
