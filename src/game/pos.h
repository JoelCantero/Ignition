#ifndef IGN_POS_H
#define IGN_POS_H

#include "assets.h"
#include "car.h"
#include "fx.h"

int ign_pos_parse(const uint8_t *data, size_t size, int nobj, IgnPos *out);
int ign_pos_load(const char *path, int nobj, IgnPos *out);
void ign_pos_free(IgnPos *pos);
/* pos_apply_frame 0x412e8: advances each animated PLC object's pose by one frame. */
void ign_pos_step(IgnPos *pos, IgnModel *model);

/* Collision objects (world creation 0x24f22, list [0x1fefc0] of 0x98-byte records, count [0x1feeb4]): PLC types
 * 0x96-0xc7 that a car knocks flying. Positions in engine coordinates (x/z + IGN_SRF_ORIGIN, height up). */
#define IGN_MAX_COLL_OBJS 128

typedef struct {
    int kind;          /* +0x00: PLC type - 0x96 (0, 2, 3 or 4 = flashing light) */
    int obj;           /* +0x04: PLC object, also its engine move record */
    int flying;        /* +0x08 */
    double pos[3];     /* +0x0c/+0x14/+0x1c */
    double vel[3];     /* +0x24/+0x2c/+0x34: units per frame, y up */
    double ang[3];     /* +0x3c/+0x44/+0x4c: tenths of a degree */
    double spin[3];    /* +0x54/+0x5c/+0x64 */
    double cvx, cvz, cw;                 /* +0x6c/+0x74/+0x7c: sub_000346cc velocity and yaw rate */
    float mass, inertia, impact;         /* +0x84/+0x88/+0x8c */
    float frame;       /* +0x90: flashing phase */
    int period;        /* +0x94 */
} IgnCollObj;

typedef struct {
    IgnCollObj o[IGN_MAX_COLL_OBJS];
    int n;
    IgnModel *track; /* engine move records: the objects' render pose */
} IgnCollObjs;

/* World creation 0x24f22-0x251fa (every detail option on). Returns the count, or -1 on an unknown kind (the original
 * stops with "Ingen info om kollisions objektet!!"). */
int ign_coll_init(IgnCollObjs *c, IgnModel *track);
/* car_ground_contact 0x350f0, once per frame from collisions_pass: resting objects against every car that is not
 * wrecked, flying objects fall, tumble and bounce on the ground; kind 4 flashes (LIGHT.TEX). */
void ign_coll_frame(IgnCollObjs *c, const IgnLevel *lv, IgnCar *const *cars, int ncars, IgnFx *fx, float focus_x,
                    float focus_z);
/* sub_00035f38: reflects v (+0x18) on the plane of normal n (+0) when it moves against it, through two rotations. */
void ign_coll_reflect(const double n[3], double v[3]);
/* 0x35967: height (up) of the query's plane at engine (x, z). */
double ign_coll_ground_up(const IgnGroundHit *hit, double x, double z);

#endif
