#ifndef IGN_CAMERA_H
#define IGN_CAMERA_H

#include "../game/track.h"
#include "raster.h"

/* Per-car chase camera of the original ([0x1fef9c] + car * 0xc8), updated by sub_00047f94. */
typedef struct {
    double height;      /* +0x00: camera height (up) */
    double yaw;         /* +0x08: smoothed car heading, radians, physics convention */
    double rear_yaw;    /* +0x28: smoothed heading + pi for the rear view */
    double wreck;       /* +0x30: extra pitch (tenths of a degree) while wrecked */
    double tilt;        /* +0x48: pitch added to the base 42 degrees, tenths of a degree */
    double lift;        /* +0xc0: per-object height offset (PLC typeword byte 2) */
    int dist;           /* +0x54: twice the distance behind the car */
    double back[2];     /* +0x78/+0x88: x, z behind the car */
    double front[2];    /* +0x90/+0xa0: x, z ahead of the car (rear view) */
    double car_height;  /* +0x80: car+8 when last followed */
    double target_yaw;  /* +0xa8: car heading when last followed */
    double target_rear; /* +0xb8: heading + pi */
    double car_pitch;   /* +0xb0: car+0x110 when last followed */
    double shake;       /* +0x38: height jitter at the start of a wreck */
} IgnChaseCam;

/* sub_00027df8: 1400 units above the car, looking along its heading. */
void ign_chase_init(IgnChaseCam *c, const IgnCar *car);
/* One update of sub_00047f94; the original runs it once per rendered frame for every car. `racing` = race
 * clock [0x1fb4e8] < 0. Once the car has finished the position targets freeze. */
void ign_chase_update(IgnChaseCam *c, const IgnCar *car, const IgnLevel *lv, const IgnTrack *t, int racing);
/* Camera of sub_000420cc: behind the car (or ahead of it looking back while `rear`, car+0x5a4). */
void ign_chase_view(const IgnChaseCam *c, const IgnCar *car, int rear, IgnCamera *out);
/* sub_000420cc after the finish: fixed camera `index` (0..2) of table 0xc464c for `track` (0..6). `clock` is the
 * total race clock [0x1fb4d8] in 1/36 s units, used for the vertical bob. Returns -1 if there is no table entry. */
int ign_fixed_camera(int track, int index, double clock, IgnCamera *out);

#endif
