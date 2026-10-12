#ifndef IGN_RACE_H
#define IGN_RACE_H

#include "car.h"

#define IGN_RACE_LAPS 3        /* car_update 0x2f32b: a car finishes on reaching lap 3 */
#define IGN_MODE_ELIMINATION 3 /* [0x1feefc] == 3: the last car of every lap is eliminated */

typedef struct {
    int mode;      /* [0x1feefc] */
    int tick;      /* physics ticks since the start */
    int finished;  /* [0x1fee60]: cars that finished or were eliminated */
    int remaining; /* [0x1fed78]: cars still racing, minus eliminations */
} IgnRace;

void ign_race_init(IgnRace *r, int mode, int ncars);
/* Finish test of car_update (0x2f319) and sub_0002de30 (elimination). Call once per physics tick
 * after ign_car_progress. Returns the number of cars still racing. */
int ign_race_step(IgnRace *r, IgnCar *const *cars, int n);
/* Race position 1..n: finished cars by arrival, then by lap, phase and section. */
int ign_race_position(const IgnCar *const *cars, int n, int index);

#endif
