#ifndef IGN_CAR_H
#define IGN_CAR_H

#include <stdbool.h>

#include "assets.h"
#include "physics.h"

#define IGN_MAX_CAR_PARTS 8
#define IGN_MAX_WALLS 500 /* car+0x9b0..0x4830: 32 bytes per wall */

typedef struct {
    bool accel, brake, left, right, turbo;
    /* car_input (0x4d5f4) analog form used by the AI: pedals 0..100, steer -100..100 (+ = left). */
    bool analog;
    int a_accel, a_brake, a_steer;
    bool manual_gear;
    int gear_shift;                  /* one-shot request: -1 down, +1 up */
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
    /* car+0x2fc/+0x31c: body box corners x1.1, (min x, min z), (min x, max z), (max x, max z), (max x, min z). */
    float corner_x[4], corner_z[4];
    int16_t torque[3][IGN_TORQUE_POINTS];
    int gear;                      /* 0 reverse, 1-2 forward */
    int forward_gear;              /* port trace/UX: last forward gear, not an original selection input */
    int gear_ticks, gear_delay;    /* ctrl+0x10/+0x14: calls choosing a different gear, delay */
    double steer_visual;           /* car+0xb0 */
    IgnBody body;
    float x, y, z, heading;        /* render pose; heading 0 = +z */
    float kmh;
    int surface_type;
    int wall_hit;
    int jump_mode;                 /* car+0x274: set on surfaces 0x0a-0x13, widens the wall search */
    double walls[IGN_MAX_WALLS][4]; /* car+0x9b0: cached wall segments x0, z0, x1, z1 in metres */
    int nwalls;                    /* car+0x4830 */
    float wall_x, wall_z;          /* car+0x4834/+0x483c: position of the last wall search */
    /* Vertical state in world units, up positive (car+0x08/+0x20/+0x120/+0x128/+0x270). */
    double height, fall_speed, ground, ground_prev;
    double grip_avg;               /* car+0x148: last valid four-wheel surface average */
    int airborne;
    int ground_object;             /* track object under the last probe with ground */
    int section, phase, lap;       /* car+0x364 / +0x378 / +0x374 */
    int lap_section;               /* car+0x370: last non-sentinel lap probe */
    int progress_lock;             /* car+0x604: crossing gate; producer not reconstructed */
    double race_elapsed, lap_elapsed, lap_origin; /* car+0x3a4/+0x3ac/+0x3d4, seconds */
    double last_lap, lap_times[3]; /* car+0x3b4, +0x3bc/+0x3c4/+0x3cc */
    double turbo_charge;           /* car+0x534: 0..100, starts full */
    int turbo_mode;                /* car+0x53c: 0 off, 1 button, 2 forced */
    int turbo_wait;                /* car+0x548: decremented only on request 1 */
    int turbo_key;                 /* car+0x530: last tick's turbo request (0, 1 held, 2 start boost) */
    int turbo_started;             /* the turbo started this tick (sub_0004e688 effects), cleared by the caller */
    /* Visual pose, model frame (x forward, y down, z = physics side): radians unless noted. */
    double lean_roll, lean_pitch;  /* car+0xb8/+0xc0: degrees, from acceleration (sub_0002e6f8) */
    double body_roll, body_pitch;  /* car+0x584/+0x58c: smoothed body tilt (sub_00040ec8) */
    int out;                       /* car+0x528: finished or eliminated */
    int wrecked;                   /* aggregate of car+0x354/+0x358/+0x35c wreck flags */
    double blow;                   /* car+0x60c: 0 until eliminated, then 1.0 */
    int finish_position;           /* car+0x3a0: snapshot of position (+0x39c) */
    int finish_tick, place;        /* port tick statistic and arrival counter */
    double step_x, step_z;         /* car+0x18/+0x28: last tick displacement, world units */
    /* Wrecks (car_vertical 0x2e3a7, car_update 0x2fd63) and the respawn of sub_0003a788. */
    int wreck_kind;                /* 1 exploded (car+0x354), 2 sunk (car+0x358), 3 slid off (car+0x35c) */
    int wreck_slid;                /* kind 3: next slide frame of sub_0003db4c (0..30) */
    int wrecks;                    /* port statistic */
    double wreck_frame;            /* effect frame, 1/36 s; 80 frames, the last 40 glide to the respawn point */
    double wreck_from[4];          /* car+0x384/+0x38c/+0x394/+0x37c: x, height, z, physics heading */
    double wreck_at[3];            /* effect origin: rear axle point (explosion) or car position (sink), y up */
    float body_min_x;              /* car+0x5b8: rearmost body vertex, model x (turbo exhaust) */
    int turbo_frames;              /* car+0x54c: frames of turbo effects */
    int wheel_type[4];             /* car+0x150..: last surface found under each wheel */
    int offroad;                   /* car+0x5c8: ticks with all wheels on types > 0x28 */
    int special_ground;            /* car+0x5fc: some wheel on a type > 0x13 */
    int ai;                        /* [0x1fefd0]+4 == 1: computer car (steering, turbo force and revs of profile 11) */
    int torque_bonus;              /* car+0x524: computer cars, by model and difficulty */
    int creep, creep_ticks;        /* car+0x51c/+0x520: torque added while the curve gives <= 3 */
    double catchup, catchup_clock; /* car+0x594/+0x59c: human torque bonus by place, renewed every 182 frames */
    int position;                  /* car+0x39c: race position (1 = leader), set by the caller */
    int level_change;              /* every corner was an outlier (car_update 0x30265 keeps them) */
    int spark_timer;               /* car+0x33c: frames of wall sparks left */
    int spark_types[4];            /* car+0x5e8: wheel types when the wall sparks started */
    double revs;                   /* car+0x288: engine revs 0..~100, picks the ENGINE.INF entries */
    double wall_impact;            /* body +0x1b8 after this tick's wall pass, 0 without a wall hit */
    int landed;                    /* car+0x278: set by a landing this tick */
    /* Skid state (sub_0003731c). */
    double slip;                   /* car+0x90: velocity angle minus heading, [0, 2pi) */
    int skid_timer;                /* car+0x2f8: wheelspin, saturated grip or hard braking */
    int skidding;
    float tyre_half;               /* half width of a tyre track, world units */
    int mark_valid[2];             /* per axle (0 rear car+0x48, 1 front car+0x68) */
    float mark_at[2][2];           /* car+0x2c4/+0x2c8: axle point of the last mark */
    float mark_prev[2][2][2][2];   /* [axle][tyre][edge] x, z of the last mark's leading edge */
    /* cars_update_all 0x2da4a: rear (car+0x48/+0x50) and front (+0x68/+0x70) axle points and their last tick steps
     * (+0x58/+0x60, +0x78/+0x80), world units. */
    double axle[2][2], axle_step[2][2];
    int axle_front, axle_rear;     /* car+0x5ac/+0x5b0: |x| of wheels 1 and 3 from the body in CARS.PLC (0x267be) */
    /* car_ground_contact operand: car+0x640/+0x648 = mean axle step at the frame start (sub_00032d8c); mass 10,
     * inertia 4000 (cars_init 0x273b3) and the impact of the last object hit (car+0x634/+0x638/+0x63c). */
    double contact_v[2];
    float contact_mass, contact_inertia, contact_impact;
    int launch;                    /* car+0x554: ticks of the somersault started by a 0x12d object, 0 = none */
    /* sub_00031858: flattened by a car landing on it or by a 0x12f object; ticks since (car+0x558), 0 = normal. The
     * body mesh is scaled x1.2 / y0.2 / z1.6 (the port's renderer only takes the height, body_stretch). */
    int flat;
    int flat_sound;                /* event for the caller: 6 flattened, 7 restored (COLL samples) */
    float flat_saved_stretch;
} IgnCar;

#define IGN_WRECK_FRAMES 80.0

/* sub_0003a788 / sub_0003c208 hide the car at frame 0, sub_0003db4c at frame 30. */
static inline int ign_car_visible(const IgnCar *car) {
    return !car->wrecked || (car->wreck_kind == 3 && car->wreck_frame < 30.0);
}

/* Difficulty level 0..3 ([0x1fedd8]): scales the rear grip of every car by 1, 1.15, 1.3, 1.3. */
void ign_set_difficulty(int level);
/* Original race clock is nonnegative before the start, then -1 while racing. */
void ign_set_race_started(int started);
/* [0x1fb540]: seconds since the start, for the start boost. */
/* [0x1fb4d8]: 1/36 s frames since the level started (catch-up bonus). */
void ign_set_level_clock(double frames);
/* sub_00022d08: model of the k-th computer car for the difficulty (0xc5520); a model a human drives is replaced by
 * the next spare of the list, from index `*spare` (start at 5). */
int ign_computer_model(int k, const int *humans, int nhumans, int *spare);
/* sub_00022d08: marks a car as computer-driven, with its torque bonus (car+0x524). */
void ign_car_set_computer(IgnCar *car);
int ign_get_difficulty(void);
/* Returns 0 if the car id exists in CARS.PLC. */
int ign_car_init(IgnCar *car, const IgnLevel *lv, int id, float x, float z, float heading);
/* Advances one original physics tick (1/72 s). */
void ign_car_tick(IgnCar *car, const IgnLevel *lv, const IgnInput *in);
/* NULL input in ign_car_tick advances physics without dispatching controls.
 * Scheduled AI applies input after the all-car physics/collision pass.
 * turbo_request is the resolved car_input argument (0/1/2), not a sampled human key. */
void ign_car_apply_input(IgnCar *car, const IgnInput *in, int turbo_request);
/* Original shared forward curves; caller owns automatic/reverse/progress-lock eligibility. */
void ign_car_auto_gearbox(IgnCar *car);
/* Isolated production kernels: steering input -100..100, positive mean surface grip; valid car spec 0..11. */
void ign_car_steer(IgnCar *car, int input, double grip_avg, int analog);
/* Retains mean/front/rear grip if any wheel type is outside 0..29; mean starts at 0.4. */
double ign_car_update_grip(IgnCar *car, const int types[4]);
/* Drive arithmetic (0x4db90..0x4dfbe), signed throttle -100..100; gear 0..2, spec 0..11.
 * Returns the final torque index, including wheelspin, for the rev kernel. */
int ign_car_throttle(IgnCar *car, int input);
/* Revs (0x4dfbe..0x4e1a7), throttle -100..100, final torque index 0..199, gear 0..2, spec 0..11. */
void ign_car_update_revs(IgnCar *car, int input, int torque_index);
/* car_input 0x4d655: route raw pedal values using the current gear, before changing it. */
int ign_car_input_throttle(const IgnCar *car, int accel, int brake, int automatic);
/* car_input 0x4d6e2..0x4d8d8: brake and automatic forward/reverse arbitration. */
void ign_car_input_direction(IgnCar *car, int accel, int brake, int automatic);
/* car_gearbox 0x502c4: supplied original shared forward torque curves, no audio. */
void ign_car_gearbox(IgnCar *car, const int16_t curves[3][IGN_TORQUE_POINTS]);
/* human_input: one-shot shifts, after dispatch, bounded at reverse and second. */
void ign_car_shift(IgnCar *car, int shift);
/* Brake arithmetic (0x4e1bc), brake 0..100. Finish/input arbitration is handled by the caller. */
void ign_car_brake(IgnCar *car, int input);
/* Turbo arithmetic (0x4e3a7..0x4e656); caller handles requests, sounds and effects. */
void ign_car_turbo(IgnCar *car);
/* human_input 0x2c940..0x2c9d4: sampled key result, elapsed car time, before turbo arithmetic.
 * Caller commits the returned request to turbo_key after dispatch (0x2cc9b..0x2ccb5). */
int ign_car_turbo_input(const IgnCar *car, int key, double elapsed);
typedef enum {
    IGN_TURBO_REQUEST_NONE,
    IGN_TURBO_REQUEST_START,
    IGN_TURBO_REQUEST_REFUSED,
    IGN_TURBO_REQUEST_FORCED
} IgnTurboRequestEvent;
/* car_input 0x4d8e1..0x4db89: request 0/1/2, excluding external sound/effect callees. */
IgnTurboRequestEvent ign_car_turbo_request(IgnCar *car, int request);
/* Vertical arithmetic only; caller handles landing effects and the car_update airborne threshold. */
int ign_car_vertical_step(IgnCar *car, double *raw_drop);
/* Starts a wreck (kind 1 explosion, 2 sinking): the car is hidden and no longer driven. */
void ign_car_wreck(IgnCar *car, int kind);
/* cars_init turbo fields; preserves the request latch. */
void ign_car_init_turbo(IgnCar *car);
/* Wreck-effect updates refill turbo and disable its mode, preserving wait/latch/effect counter. */
void ign_car_wreck_turbo(IgnCar *car);
/* respawn_place control reset, without geometry, wreck flags, turbo or clocks. */
void ign_car_respawn_controls(IgnCar *car);
/* Moves a wrecked car (glide of sub_0003a788). */
void ign_car_set_pose(IgnCar *car, const IgnLevel *lv, float x, float z, double heading, double height);
/* sub_00041430: puts a wrecked car back at a respawn point, still and airborne. Returns -1 (car untouched) if the
 * port's ground lookup finds nothing there. */
int ign_car_respawn(IgnCar *car, const IgnLevel *lv, float x, float z, double heading, double height);
int ign_first_car_id(const IgnLevel *lv);

/* Pair pass of cars_update_all (0x2d2f8): |dx|,|dz| < 200, |dy| < 70, then sub_0001ebb4. Each contact goes to `hits`
 * (contact point in world units, mean impact of both bodies). Returns the contacts. */
typedef struct {
    int a, b;
    double x, z, impact;
} IgnCarHit;
int ign_cars_collide(IgnCar *const *cars, int n, IgnCarHit *hits, int max_hits, int *nhits);

/* sub_00031858: flattens the car (mesh, COLL 6 event). */
void ign_car_flatten(IgnCar *car);
/* car_update 0x31316 (per tick, before the effects): a flattened car counts up; from 200 ticks, if no other car is within
 * 30 in x and z, it gets its mesh back, pops up 6 (when grounded) and raises COLL 7; else it waits at 200. */
void ign_car_flat_tick(IgnCar *car, IgnCar *const *cars, int n, int index);

/* sub_00032d8c, once per frame before the cars move: car+0x640/+0x648 = mean of the last axle steps. */
void ign_car_frame_velocity(IgnCar *car);

/* sub_000346cc operand (0x34 bytes): +0 x, +8 z, +0x10 vx, +0x18 vz, +0x20 yaw rate (doubles), +0x28 mass,
 * +0x2c inertia, +0x30 impact (floats). Positions in world units, velocities in units per tick. */
typedef struct {
    double x, z, vx, vz, w;
    float mass, inertia, impact;
} IgnContactBody;

/* sub_000349e8: crossings of the edges of two polygons (float x, z pairs). With 1-8 crossings stores their mean
 * (0x1fee9c/0x1feea8) and the angle from the second to the first of the two farthest apart (0x1fee98), returns 1;
 * otherwise 0 (more than 8 crossings counts as none). */
int ign_contact_polygons(const float (*a)[2], int na, const float (*b)[2], int nb, float *cx, float *cz,
                         float *theta);
/* sub_000346cc: impulse between two bodies touching at (cx, cz) along the contact line `theta`, restitution 0.5
 * (-1.5 = -(1 + e)). Both impacts become |trunc(normal speed)|. Returns 0 if they are separating. */
int ign_contact_impulse(IgnContactBody *a, IgnContactBody *b, float cx, float cz, float theta);
/* car_ground_contact 0x351c9: the car's body box (car+0x2fc/+0x31c) around the car, as floats (0x207aa0). Engine
 * coordinates (x/z + IGN_SRF_ORIGIN) when `engine`, as the original. */
void ign_car_contact_box(const IgnCar *car, int engine, float out[4][2]);

struct IgnTrack;
/* Lap progress from the section under the car (car_update 0x2f2cc / 0x2fc1c). */
/* 0x2fc1c: phase-only decision on car.section, not a lap crossing. */
void ign_car_progress_phase(IgnCar *car, int count);

#endif
