#ifndef IGN_GHOST_H
#define IGN_GHOST_H

#include <stdint.h>

#include "car.h"

/* GHOSTS\<name>.GST: reader 0x2adf8, recorder 0x51204 (one sample per 1/72 s tick), writer 0x2cd12. */
#define IGN_GHOST_SAMPLES 21600
#define IGN_GHOST_FILE_SIZE 0x49d4c /* 10-byte header + samples * 14 + 2 zero bytes */

typedef struct {
    int16_t x, y, z;    /* mesh x/z (engine x/z - 25600); y = height, up positive, +5000 only when exploded */
    int16_t roll;       /* car+0x108 ground roll, [0, 2pi) * 1024 */
    int16_t heading;    /* car+0xf8, original convention (body heading, forward = cos/sin in x/z) */
    int16_t pitch;      /* car+0x110 ground pitch */
    int16_t progress;   /* lap * (sections + 10) + |section| */
} IgnGhostSample;

typedef struct {
    int16_t car, track; /* car model; track id 0..6 (ign_ghost_track_id) */
    int16_t time_cs;    /* whole 3-lap race in centiseconds */
    char name[4];       /* 3 characters + NUL */
    IgnGhostSample s[IGN_GHOST_SAMPLES];
    int count;          /* recorded samples (load: all) */
    int overflow;       /* [0x1fee00]: more than 300 s recorded, the writer refuses */
} IgnGhost;

/* Track id of a LEVELS directory name, or -1. */
int ign_ghost_track_id(const char *level);
/* "MOOSEJAW", "GOLDRUSH", ... for a track id, or NULL. */
const char *ign_ghost_basename(int track);

/* Return 0 on success; load requires the exact file size, as read_file_into does. */
int ign_ghost_load(const char *path, IgnGhost *g);
int ign_ghost_save(const char *path, const IgnGhost *g);

void ign_ghost_begin(IgnGhost *g, int car, int track, const char *name);
void ign_ghost_record(IgnGhost *g, const IgnCar *car, int sections);
/* Sets the race time and pads the tail with the last sample (words 0-5 only, as 0x2cd12 does). */
void ign_ghost_finish(IgnGhost *g, double race_seconds);

#define IGN_MODE_TIME_TRIAL 2 /* [0x1feefc] == 2 */

/* Time trial ([0x1feefc] == 2) with the ghost option ([0x1fee4c] == 1). */
#define IGN_GHOST_NO_RECORD 3000000 /* 0x2ae5a: [0x1fee04] when GHOSTS\<name>.GST cannot be read */

typedef enum {
    IGN_TRIAL_NONE,     /* no readable ghost: nothing to play, any finish beats IGN_GHOST_NO_RECORD */
    IGN_TRIAL_GHOST,    /* ghost of this track loaded for playback */
    IGN_TRIAL_MISMATCH  /* ghost of another track: no playback and [0x1fee00] = 1, so nothing is saved */
} IgnTrialGhost;

/* sub_0002adf8 0x2ae16..0x2af0b: loads the ghost of `track`, sets *record_cs ([0x1fee04]) and, on a track
 * mismatch, marks `rec` as unsaveable as the original does through [0x1fee00]. */
IgnTrialGhost ign_ghost_trial_load(const char *path, int track, IgnGhost *g, IgnGhost *rec, int *record_cs);
/* sub_0002ccc8 0x2ccd7..0x2cd06: save only if the recording fits and record_cs * 0.01 > race time. */
int ign_ghost_trial_beats(const IgnGhost *rec, int record_cs, double race_seconds);

/* Playback (0x50e94): puts a car initialised with ign_car_init in the pose of sample `tick` (clamped). */
void ign_ghost_pose(const IgnGhost *g, int tick, IgnCar *car);

#endif
