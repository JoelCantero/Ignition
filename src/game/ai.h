#ifndef IGN_AI_H
#define IGN_AI_H

#include "car.h"
#include "fx.h"
#include "race.h"
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
    int accel;     /* +0x54 */
    int section;   /* +0x68 */
    int route_choice;   /* +0x64: selected route */
    int route_selector; /* +0x66: use route_choice instead of side ratio */
    double route_random; /* +0x6c: last rand() value, scaled by 1/32768 */
    IgnInput input; /* [0x1dade0 + index*16]: last car_input arguments, replayed with eax=0x1f02 */
    int turbo_request; /* Resolved fourth argument, including human start request 2. */
} IgnAi;

void ign_ai_init(IgnAi *ai, int index);
/* Track id of [0x1fef10] by level name (0 CANADA, 1 USA, 2 CARIB, 3 BRAZIL, 4 AUSTRIA, 5 ICELAND, 6 JAPAN). */
void ign_ai_set_track(const char *level);
/* Cars seen by the nearest-rival search of the AI. */
void ign_ai_set_cars(IgnCar *const *cars, int n);
/* sub_0001aea0 / t_ai_lanes 0xbf9a0: update route state for a matching section. */
void ign_ai_update_route_choice(IgnAi *ai, int section, IgnFx *fx);
/* 0x1c630..0x1c777: one decision, finite road-angle error, total car count (including inactive cars). */
void ign_ai_stuck_step(IgnAi *ai, const IgnCar *car, double road_error, int ncars);
/* 0x1be03..0x1bf60: original road/velocity heading blend, including angular wrap. */
double ign_ai_travel(double heading, double motion);
/* 0x1b1b0..0x1b1f3: section of lookahead k (0..5) at s + {1,2,3,4,6,8}, wrapped once; a result in the
 * last three sections becomes count - 1. */
int ign_ai_lookahead_section(int section, int k, int count);
/* Outputs of 0x1b4a2..0x1b823 (stack slots of sub_0001aea0). */
typedef struct {
    int index;    /* esi: accepted lookahead record 0..5 */
    int offset;   /* [esp+0x130]: section offset {1,2,3,4,6,8}[index] */
    int sign;     /* [esp+0x144]: 0, or -1/+1 from the last blocking record */
    double x, z;  /* [esp+0xe8]/[esp+0xe0]: integer centre of the last tested record */
    double aim;   /* [esp+0xf0]: atan2(z - car z, x - car x) */
    double first; /* [esp+0x40]: wrapped centre-heading change to the first lookahead record */
    double grip;  /* [esp+0x80]: max(0, (8 - t_surface_grip[current TRI byte]) * 0.125) */
    double curve; /* [esp+0xa8] at 0x1b823, before a8 = 1 + (grip + 0.8) * curve */
} IgnAiTarget;
/* 0x1b4a2..0x1b823: target clipping by the inset points and curvature of six lookahead records. */
void ign_ai_target(const IgnAiRouteRecord *current, const IgnAiRouteRecord *const ahead[6],
                   double s38, double car_x, double car_z, IgnAiTarget *out);
/* sub_0001aea0: steering, throttle/brake and turbo as a car_input call. */
void ign_ai_drive(IgnAi *ai, const IgnCar *car, const IgnTrack *t, IgnFx *fx, IgnInput *in);
/* Call after physics/collisions; no RNG or control mutation outside an eligible control turn. */
IgnAiControlAction ign_ai_control_tick(IgnAi *ai, IgnCar *car, const IgnTrack *t, IgnFx *fx,
                                       const IgnRace *race, int index);

#endif
