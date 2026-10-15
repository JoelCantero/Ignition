#include "save.h"

#include <errno.h>
#include <float.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#define make_dir(p) mkdir(p, 0755)
#endif

int ign_btz_load(const char *path, IgnBtz *b) {
    uint8_t buf[IGN_BTZ_SIZE + 1];
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (n != IGN_BTZ_SIZE) return -1;
    memcpy(b->raw, buf, IGN_BTZ_SIZE);
    return 0;
}

/* sub_00061078: fopen "wb", one fwrite of the whole block. The port writes a temporary file first. */
int ign_btz_save(const char *path, const IgnBtz *b) {
    char tmp[1024];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return -1;
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    size_t n = fwrite(b->raw, 1, IGN_BTZ_SIZE, f);
    if (fclose(f) || n != IGN_BTZ_SIZE) {
        remove(tmp);
        return -1;
    }
    remove(path); /* rename does not replace an existing file on Windows */
    return rename(tmp, path) ? -1 : 0;
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}

/* binary64 little-endian, as stored by fstp qword. */
static double rdf64(const uint8_t *p) {
    uint64_t u = rd32(p) | (uint64_t)rd32(p + 4) << 32;
    double d;
    memcpy(&d, &u, sizeof d);
    return d;
}

static void wrf64(uint8_t *p, double d) {
    uint64_t u;
    memcpy(&u, &d, sizeof u);
    wr32(p, (uint32_t)u);
    wr32(p + 4, (uint32_t)(u >> 32));
}

int32_t ign_btz_i32(const IgnBtz *b, size_t off) {
    return off + 4 <= IGN_BTZ_SIZE ? (int32_t)rd32(b->raw + off) : 0;
}

void ign_btz_set_i32(IgnBtz *b, size_t off, int32_t v) {
    if (off + 4 <= IGN_BTZ_SIZE) wr32(b->raw + off, (uint32_t)v);
}

void ign_btz_name(const IgnBtz *b, int car, char out[IGN_BTZ_NAME_LEN + 1]) {
    out[0] = 0;
    if (car < 0 || car >= IGN_BTZ_MAX_CARS) return;
    memcpy(out, b->raw + IGN_BTZ_NAMES + car * IGN_BTZ_NAME_LEN, IGN_BTZ_NAME_LEN);
    out[IGN_BTZ_NAME_LEN] = 0;
}

int ign_btz_records(const IgnBtz *b, int track, IgnTrackRecords *r) {
    if (track < 0 || track >= IGN_BTZ_TRACKS) return -1;
    const uint8_t *p = b->raw + IGN_BTZ_RECORDS + track * IGN_RECORD_STRIDE;
    for (int i = 0; i < IGN_RECORD_SLOTS; i++) {
        memcpy(r->race_name[i], p + i * 4, 4);
        r->race[i] = rdf64(p + 0x14 + i * 8);
        memcpy(r->lap_name[i], p + 0x3c + i * 4, 4);
        r->lap[i] = rdf64(p + 0x50 + i * 8);
    }
    return 0;
}

int ign_btz_set_records(IgnBtz *b, int track, const IgnTrackRecords *r) {
    if (track < 0 || track >= IGN_BTZ_TRACKS) return -1;
    uint8_t *p = b->raw + IGN_BTZ_RECORDS + track * IGN_RECORD_STRIDE;
    for (int i = 0; i < IGN_RECORD_SLOTS; i++) {
        memcpy(p + i * 4, r->race_name[i], 4);
        wrf64(p + 0x14 + i * 8, r->race[i]);
        memcpy(p + 0x3c + i * 4, r->lap_name[i], 4);
        wrf64(p + 0x50 + i * 8, r->lap[i]);
    }
    return 0;
}

void ign_btz_default(IgnBtz *b) {
    memset(b, 0, sizeof *b);
    ign_btz_set_i32(b, 0x000, 30); /* as in the shipped file; meaning unknown */
    ign_btz_set_i32(b, IGN_BTZ_NCARS, 1);
    memcpy(b->raw + IGN_BTZ_NAMES, "PL1", 4);
    IgnTrackRecords r;
    for (int i = 0; i < IGN_RECORD_SLOTS; i++) {
        memcpy(r.race_name[i], "---", 4);
        memcpy(r.lap_name[i], "---", 4);
        r.race[i] = r.lap[i] = 999.99;
    }
    for (int t = 0; t < IGN_BTZ_TRACKS; t++) ign_btz_set_records(b, t, &r);
}

/* sprintf("%s") of the participant name into a 4-byte slot. A longer name would run into the next slot in the
 * original; the port keeps the first 3 characters. */
static void set_name(char slot[4], const char *name) {
    size_t n = strnlen(name, 3);
    memset(slot, 0, 4);
    memcpy(slot, name, n);
}

static int insert(char names[IGN_RECORD_SLOTS][4], double *times, const char *name, double seconds) {
    const int last = IGN_RECORD_SLOTS - 1;
    if (!(seconds < times[last])) return -1; /* fcomp; jae: NaN and ties keep the table */
    times[last] = seconds;
    set_name(names[last], name);
    int slot = last;
    /* One pass from slot 4 to 1, swapping while the lower slot is strictly faster. */
    for (int k = last; k > 0; k--) {
        if (!(times[k] < times[k - 1])) continue;
        double t = times[k];
        times[k] = times[k - 1], times[k - 1] = t;
        char tmp[4];
        memcpy(tmp, names[k], 4);
        memcpy(names[k], names[k - 1], 4);
        memcpy(names[k - 1], tmp, 4);
        if (slot == k) slot = k - 1;
    }
    return slot;
}

int ign_records_insert_race(IgnTrackRecords *r, const char *name, double seconds) {
    return insert(r->race_name, r->race, name, seconds);
}

int ign_records_insert_lap(IgnTrackRecords *r, const char *name, double seconds) {
    return insert(r->lap_name, r->lap, name, seconds);
}

void ign_btz_shuffle_grid(IgnBtz *b, double (*random01)(void)) {
    int n = ign_btz_i32(b, IGN_BTZ_NCARS);
    if (ign_btz_i32(b, IGN_BTZ_MODE) == 0 || n <= 1 || n > 8) return;
    for (int k = 0; k < n; k++) {
        int j = (int)(random01() * n); /* first draw -> [esp+4], second -> [esp] */
        int i = (int)(random01() * n);
        int32_t a = ign_btz_i32(b, IGN_BTZ_GRID + i * 4), c = ign_btz_i32(b, IGN_BTZ_GRID + j * 4);
        ign_btz_set_i32(b, IGN_BTZ_GRID + j * 4, a);
        ign_btz_set_i32(b, IGN_BTZ_GRID + i * 4, c);
    }
}

const char *ign_lap_record_text(int language) {
    static const char *const T[] = {"LAP RECORD", "RUNDENREKORD", "RECORD SUL GIRO",
                                    "RECORD DE VUELTA", "VARV REKORD", "RECORD DU TOUR"};
    return language >= 0 && language < 6 ? T[language] : T[0];
}

const char *ign_track_record_text(int language) {
    static const char *const T[] = {"TRACK RECORD", "STRECHENREKORD", "RECORD DELLA PISTA",
                                    "RECORD DE PISTA", "BAN REKORD", "RECORD DU CIRCUIT"};
    return language >= 0 && language < 6 ? T[language] : T[0];
}

int ign_save_path(char *out, size_t size, const char *dir, const char *name) {
    int n = snprintf(out, size, "%s%s", dir, name);
    return n < 0 || (size_t)n >= size ? -1 : 0;
}

int ign_save_make_ghost_dir(const char *dir) {
    char p[1024];
    if (ign_save_path(p, sizeof p, dir, "GHOSTS")) return -1;
    if (make_dir(p) && errno != EEXIST) return -1;
    return 0;
}

int ign_records_commit(IgnBtz *b, int track, const char *name, const IgnCar *car) {
    IgnTrackRecords r;
    if (!car->out || car->wrecked || ign_btz_records(b, track, &r)) return 0;
    int bits = 0, slot = ign_records_insert_race(&r, name, car->race_elapsed);
    if (slot >= 0) bits |= IGN_SAVED_RACE_TABLE | (slot == 0 ? IGN_SAVED_TRACK_RECORD : 0);
    for (int i = 0; i < 3; i++) {
        if (car->lap_times[i] <= 0) continue;
        slot = ign_records_insert_lap(&r, name, car->lap_times[i]);
        if (slot >= 0) bits |= IGN_SAVED_LAP_TABLE | (slot == 0 ? IGN_SAVED_LAP_RECORD : 0);
    }
    ign_btz_set_records(b, track, &r);
    return bits;
}

int ign_session_begin(IgnSession *s, const char *dir, const char *data_dir, const char *level, int car_model,
                      int time_trial, IgnGhost *play) {
    memset(s, 0, sizeof *s);
    size_t n = strlen(dir);
    if (n + 2 > sizeof s->dir) return -1;
    memcpy(s->dir, dir, n + 1);
    if (n && s->dir[n - 1] != '/' && s->dir[n - 1] != '\\') s->dir[n] = '/', s->dir[n + 1] = 0;
    char path[1100];
    if (!ign_save_path(path, sizeof path, s->dir, IGN_BTZ_NAME) && !ign_btz_load(path, &s->btz)) {
        s->btz_source = 0;
    } else if (snprintf(path, sizeof path, "%s/%s", data_dir, IGN_BTZ_NAME) < (int)sizeof path &&
               !ign_btz_load(path, &s->btz)) {
        s->btz_source = 1;
    } else {
        ign_btz_default(&s->btz);
        s->btz_source = 2;
    }
    s->track = ign_ghost_track_id(level);
    s->time_trial = time_trial && s->track >= 0;
    if (!s->time_trial) return 0;
    char name[IGN_BTZ_NAME_LEN + 1];
    ign_btz_name(&s->btz, 0, name);
    ign_ghost_begin(&s->rec, car_model, s->track, name);
    snprintf(s->ghost_path, sizeof s->ghost_path, "%sGHOSTS/%s.GST", s->dir, ign_ghost_basename(s->track));
    s->ghost_state = ign_ghost_trial_load(s->ghost_path, s->track, play, &s->rec, &s->record_cs);
    if (s->ghost_state == IGN_TRIAL_NONE &&
        snprintf(path, sizeof path, "%s/GHOSTS/%s.GST", data_dir, ign_ghost_basename(s->track)) < (int)sizeof path)
        s->ghost_state = ign_ghost_trial_load(path, s->track, play, &s->rec, &s->record_cs);
    return 0;
}

double ign_session_best_lap(const IgnSession *s) {
    IgnTrackRecords r;
    return ign_btz_records(&s->btz, s->track, &r) ? DBL_MAX : r.lap[0];
}

void ign_session_tick(IgnSession *s, const IgnCar *player, int sections) {
    if (s->time_trial && !player->out && !s->finished) ign_ghost_record(&s->rec, player, sections);
}

int ign_session_finish(IgnSession *s, const IgnCar *player) {
    if (s->finished || !player->out) return 0;
    s->finished = 1;
    int bits = 0, failed = 0;
    if (s->time_trial && ign_ghost_trial_beats(&s->rec, s->record_cs, player->race_elapsed)) {
        ign_ghost_finish(&s->rec, player->race_elapsed);
        if (ign_save_make_ghost_dir(s->dir) || ign_ghost_save(s->ghost_path, &s->rec))
            failed = 1;
        else
            bits |= IGN_SAVED_GHOST;
    }
    if (s->track >= 0) {
        char name[IGN_BTZ_NAME_LEN + 1];
        ign_btz_name(&s->btz, 0, name);
        bits |= ign_records_commit(&s->btz, s->track, name, player);
    }
    char path[1100];
    if (ign_save_path(path, sizeof path, s->dir, IGN_BTZ_NAME) || ign_btz_save(path, &s->btz))
        failed = 1;
    else
        bits |= IGN_SAVED_BTZ;
    return failed ? -1 : bits;
}
