#ifndef IGN_SKID_H
#define IGN_SKID_H

#include "car.h"
#include "fx.h"

#define IGN_MAX_SKIDS 700 /* [0x1feeb8] wraps at 0x2bc */

/* One skid mark object of sub_0003731c: a quad per tyre from the previous mark to the current axle position. */
typedef struct {
    float x[2][4], z[2][4], y;
    uint8_t color;
} IgnSkidMark;

typedef struct {
    IgnSkidMark marks[IGN_MAX_SKIDS];
    int next, count;
    int tick;
} IgnSkids;

void ign_skids_init(IgnSkids *s);
/* sub_0003731c for one car, once per physics tick after ign_car_tick. Dust puffs are effects of type 1 in `fx`
 * (only created within 3500 units of the followed car (fx, fz), as sub_0003fee8 does). */
void ign_skids_car(IgnSkids *s, IgnFx *fx, IgnCar *car, float focus_x, float focus_z);
/* Counts physics ticks: dust is emitted once per 1/36 s frame. */
void ign_skids_step(IgnSkids *s);

#endif
