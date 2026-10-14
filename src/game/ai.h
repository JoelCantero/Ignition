#ifndef IGN_AI_H
#define IGN_AI_H

#include "car.h"
#include "fx.h"
#include "track.h"

/* Driver parameters: state offsets +0x00..+0x48 of the 0x7c-byte AI state, copied by load_tri from the
 * 0x48-byte record at 0xbfb28 (0.9, 0.8, 0.4, 0.6, 0.75, 0.5, 5, 0.6, 0.6); +0x00 is scaled by the
 * difficulty (x1, x0.7, x0.4, x0.4). */
typedef struct {
    double s00, s08, s10, s18, s20, s28, s38, s40, s48;
} IgnAiParams;

/* AI state of sub_0001aea0 (0x7c bytes in the original). */
typedef struct {
    IgnAiParams p;
    double side;   /* +0x58: left/right distance ratio to the road edges (negative = right half) */
    double steer;  /* +0x74 */
    int stuck;     /* +0x60 */
    int age;       /* ticks since the start; the stuck counter waits for the standing start */
    int accel;     /* +0x54 */
    int section;   /* +0x68 */
    int route_choice;   /* +0x64: selected route */
    int route_selector; /* +0x66: use route_choice instead of side ratio */
    double route_random; /* +0x6c: last rand() value, scaled by 1/32768 */
} IgnAi;

void ign_ai_init(IgnAi *ai, int index);
/* Track id of [0x1fef10] by level name (0 CANADA, 1 USA, 2 CARIB, 3 BRAZIL, 4 AUSTRIA, 5 ICELAND, 6 JAPAN). */
void ign_ai_set_track(const char *level);
/* Cars seen by the nearest-rival search of the AI. */
void ign_ai_set_cars(IgnCar *const *cars, int n);
/* sub_0001aea0 / t_ai_lanes 0xbf9a0: update route state for a matching section. */
void ign_ai_update_route_choice(IgnAi *ai, int section, IgnFx *fx);
/* sub_0001aea0: steering, throttle/brake and turbo as a car_input call. */
void ign_ai_drive(IgnAi *ai, const IgnCar *car, const IgnTrack *t, IgnFx *fx, IgnInput *in);

#endif
