#ifndef IGN_PHYSICS_H
#define IGN_PHYSICS_H

#include <stdint.h>

/* Port of the original rigid-body car model (sub_0001e090), SI units: metres, seconds, kg. */
#define IGN_PHYS_DT (1.0 / 72.0)
#define IGN_WORLD_PER_METRE 21.76
#define IGN_TORQUE_POINTS 200

typedef struct {
    double mass;        /* +0x00 */
    double inertia;     /* +0x08 */
    double restitution; /* +0x10: 0.6 */
    double a, b;        /* +0x18/+0x20: axle distances from the centre of mass */
    double heading;     /* +0x28: radians, forward = (cos, sin) in x/z */
    double yaw_rate;    /* +0x30 */
    double px, pz;      /* +0x68/+0x70 */
    double vx, vz;      /* +0x78/+0x80 */
    double v_fwd;       /* +0x98 */
    double v_lat;       /* +0xa0 */
    int saturated;      /* +0xa8: drive force clipped by front grip */
    double steer;       /* +0xac: front wheel angle */
    double pitch, roll; /* +0xb4/+0xbc */
    double grip_f, grip_r; /* +0xc4/+0xcc */
    int sliding;        /* +0xd4 */
    double drive;       /* +0xd8: longitudinal force */
    double fmax_f, fmax_r; /* +0xe0/+0xe8 */
    double corner_x[4], corner_z[4]; /* +0xf0/+0x110: body-local box corners, metres */
    double radius;      /* +0x130: 4 m bounding circle */
    double impact;      /* +0x1b8: last wall impact speed */
} IgnBody;

/* sub_0001ebb4 + sub_0001e638: box vs box impulse between two cars; returns 1 on contact. */
int ign_body_collide_pair(IgnBody *a, IgnBody *b);

/* sub_0001f708 + sub_0001f3fc: box vs wall segments (metres); returns 1 if an impulse was applied. */
int ign_body_collide(IgnBody *b, const double (*walls)[4], int nwalls);

/* One integration step; slope_k is 1.0 on grippy surfaces and 4.0 on slippery ones. */
void ign_body_step(IgnBody *b, int airborne, double slope_k);

/* sub_00053e70: Hermite torque curve through 4 points, start slope 1.2, end slope 0. */
void ign_torque_curve(const int32_t p[8], int16_t out[IGN_TORQUE_POINTS]);

/* Original per-car tables (index 0..11). */
extern const int32_t ign_car_mass[12];
extern const int32_t ign_car_gears[12][3][8];
extern const double ign_car_steer_speed[12];
extern const double ign_car_steer_div[12];
extern const int32_t ign_car_steer_rate[12];
extern const double ign_car_grip[12];
extern const int32_t ign_car_turbo_force[12];
extern const double ign_car_turbo_rate[12];
extern const double ign_surface_grip[30];

#endif
