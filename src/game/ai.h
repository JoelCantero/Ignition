#ifndef IGN_AI_H
#define IGN_AI_H

#include "car.h"
#include "track.h"

/* Rival driver parameters: one 0x48-byte record per car at 0xbfb28 (copied into the 0x7c-byte AI state
 * by load_tri); every car uses {0.8, 0.4, 0.6, 0.75, 0.5, 5, 0.6, 0.6, 0.9}, matching CARS/TEST.AIS
 * (acc, carebed, caresight, careother, maintain, aggr, sight, curveinflu, slopeinflu). */
typedef struct {
    double acc, carebed, caresight, careother, maintain, aggr, sight, curveinflu, slopeinflu;
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
} IgnAi;

void ign_ai_init(IgnAi *ai, int index);
/* Track id of [0x1fef10] by level name (0 CANADA, 1 USA, 2 CARIB, 3 BRAZIL, 4 AUSTRIA, 5 ICELAND, 6 JAPAN). */
void ign_ai_set_track(const char *level);
/* Cars seen by the nearest-rival search of the AI. */
void ign_ai_set_cars(IgnCar *const *cars, int n);
/* sub_0001aea0: steering, throttle/brake and turbo as a car_input call. */
void ign_ai_drive(IgnAi *ai, const IgnCar *car, const IgnTrack *t, IgnInput *in);

#endif
