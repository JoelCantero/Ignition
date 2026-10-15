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
/* 0x356b8-0x358ff: a flying object's damping, gravity, tumbling, motion and world clamp. */
void ign_coll_fly_step(IgnCollObj *o);
/* 0x35967-0x35c84: with the ground query's face under it, an object within 15 of the plane bounces, or comes to rest
 * when slower than 2. */
void ign_coll_bounce(IgnCollObj *o, const IgnGroundHit *hit);
/* sub_00035f38: reflects v (+0x18) on the plane of normal n (+0) when it moves against it, through two rotations. */
void ign_coll_reflect(const double n[3], double v[3]);
/* 0x35967: height (up) of the query's plane at engine (x, z). */
double ign_coll_ground_up(const IgnGroundHit *hit, double x, double z);

/* Special objects, PLC types 0x12c-0x15d (list 0x1fef58, world creation 0x255cb). car_update 0x30dbb-0x31303 acts on
 * [esp+0x288]: */
enum {
    IGN_SPECIAL_NONE,
    IGN_SPECIAL_LAUNCH, /* 0x12d, or 0x131 next to an erupting geyser: somersault (car+0x554) */
    IGN_SPECIAL_PUSH,   /* 0x12e: thrown up and spun */
    IGN_SPECIAL_MESH    /* 0x12f: sub_00031858 mesh swap, not ported */
};
/* sub_0003383c: integer arguments in engine coordinates (x/z + IGN_SRF_ORIGIN, height up). Among the objects of type
 * 0x12c-0x15d (low 16 bits) the last one with a face over (x, z) within 600 in x and z (900 in jump mode) once one of
 * them has been within 190 in height; returns its type or -1. Vertices are the mesh's plus the object's current
 * position (POS), without rotation. */
int ign_special_query(const IgnLevel *lv, int32_t x, int32_t y, int32_t z, int jump);
/* car_update 0x2f01f-0x2f2be (first corner probe): for each special object within 200 in x and z of the car, ICELAND's
 * 0x12c objects explode a car within 30 in x, height and z (*wreck = 1); otherwise sub_0003383c under the car decides:
 * 0x12c explosion, 0x130 slide (*wreck = 3), 0x12d/0x131/0x12e/0x12f as above. Returns IGN_SPECIAL_*. */
int ign_special_probe(const IgnLevel *lv, const IgnFx *fx, const IgnCar *car, int iceland, int *wreck);
/* car_update 0x30dbb-0x31303 for one car and tick (before ign_car_tick): wrecks, launch with debris, push with its
 * sample (one per 15 car updates, [0x1fee3c]). */
void ign_special_apply(IgnCar *car, int special, int wreck, IgnFx *fx, float focus_x, float focus_z);

#endif
