#ifndef IGN_AI_H
#define IGN_AI_H

#include "car.h"
#include "track.h"

/* Rival driver parameters. The original keeps one 0x48-byte record per car at 0xbfb30 (copied into
 * the 0x7c-byte AI state by load_tri); every car uses {0.8, 0.4, 0.6, 0.75, 0.5, 5, 0.6, 0.6, 0.9},
 * matching the fields of CARS/TEST.AIS (acc, carebed, caresight, careother, maintain, aggr, sight,
 * curveinflu, slopeinflu). */
typedef struct {
    double acc, carebed, caresight, careother, maintain, aggr, sight, curveinflu, slopeinflu;
} IgnAiParams;

typedef struct {
    IgnAiParams p;
    float lane;    /* lateral target across the section gate: 0 = left edge, 1 = right edge */
    int stuck;     /* ticks since the last progress check */
    float sx, sz;  /* position at the last progress check */
    int reverse;   /* remaining recovery ticks */
} IgnAi;

void ign_ai_init(IgnAi *ai, int index);
/* Approximation of sub_0001aea0: follow the TRI gates `sight` sections ahead, slow down for curves. */
void ign_ai_drive(IgnAi *ai, const IgnCar *car, const IgnTrack *t, IgnInput *in);

#endif
