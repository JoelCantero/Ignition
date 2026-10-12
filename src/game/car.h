#ifndef IGN_CAR_H
#define IGN_CAR_H

#include <stdbool.h>

#include "assets.h"
#include "physics.h"

#define IGN_MAX_CAR_PARTS 8

typedef struct {
    bool accel, brake, left, right, turbo;
} IgnInput;

typedef struct {
    int id;                        /* CARS.PLC car number (type / 10) */
    int spec;                      /* index into the original per-car tables */
    int parts[IGN_MAX_CAR_PARTS];  /* object indices in IgnLevel.cars */
    int nparts;
    int32_t origin[3];             /* body position in CARS.PLC; parts are drawn relative to it */
    float part_off[IGN_MAX_CAR_PARTS][3]; /* part origin in the car frame (x/z from PLC, y per cars_init) */
    float body_stretch;            /* 3 with the BANARNE cheat ([0x1fb2f0]), else 1 */
    float front, rear, half_width; /* wheel offsets in world units (model +x = forward) */
    int16_t torque[3][IGN_TORQUE_POINTS];
    int gear;                      /* 0 reverse, 1-2 forward */
    double steer_visual;           /* car+0xb0 */
    IgnBody body;
    float x, y, z, heading;        /* render pose; heading 0 = +z */
    float kmh;
    int surface_type;
    int wall_hit;
    /* Vertical state in world units, up positive (car+0x08/+0x20/+0x120/+0x128/+0x270). */
    double height, fall_speed, ground, ground_prev;
    int airborne;
    int ground_object;             /* track object under the front-left wheel */
    int section, phase, lap;       /* car+0x370 / +0x378 / +0x374 */
    double turbo_charge;           /* car+0x534: 0..100, starts full */
    int turbo_mode;                /* car+0x53c: 0 off, 1 button, 2 forced */
    int turbo_wait;                /* car+0x548: ticks before another refused request */
    /* Visual pose, model frame (x forward, y down, z = physics side): radians unless noted. */
    double lean_roll, lean_pitch;  /* car+0xb8/+0xc0: degrees, from acceleration (sub_0002e6f8) */
    double body_roll, body_pitch;  /* car+0x584/+0x58c: smoothed body tilt (sub_00040ec8) */
    int out;                       /* car+0x528: finished or eliminated */
    int wrecked;                   /* car+0x354: exploded, no longer driven */
    double blow;                   /* car+0x60c: 0 until eliminated, then 1.0 */
    int finish_tick, place;        /* car+0x3a0 and the [0x1fee60] counter at the finish */
    double step_x, step_z;         /* car+0x18/+0x28: last tick displacement, world units */
} IgnCar;

/* Returns 0 if the car id exists in CARS.PLC. */
int ign_car_init(IgnCar *car, const IgnLevel *lv, int id, float x, float z, float heading);
/* Advances one original physics tick (1/72 s). */
void ign_car_tick(IgnCar *car, const IgnLevel *lv, const IgnInput *in);
int ign_first_car_id(const IgnLevel *lv);

/* Pair pass of cars_update_all (0x2d2f8): |dx|,|dz| < 200, |dy| < 70, then sub_0001ebb4. */
int ign_cars_collide(IgnCar *const *cars, int n);

struct IgnTrack;
/* Lap progress from the section under the car (car_update 0x2f2cc / 0x2fc1c). */
void ign_car_progress(IgnCar *car, const struct IgnTrack *t);

#endif
