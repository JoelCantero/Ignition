#ifndef IGN_RACE_H
#define IGN_RACE_H

#include "car.h"
#include "track.h"

#define IGN_RACE_LAPS 3        /* car_update 0x2f32b: a car finishes on reaching lap 3 */
#define IGN_MODE_ELIMINATION 3 /* [0x1feefc] == 3: the last car of every lap is eliminated */

typedef struct {
    int mode;      /* [0x1feefc] */
    int tick;      /* physics ticks since the start */
    int finished;  /* [0x1fee60]: cars that finished or were eliminated */
    int remaining; /* [0x1fed78]: cars still racing, minus eliminations */
    double clock;  /* [0x1fb4e8]: 0..200 in 1/36 s before the start, then -1 */
    double total;  /* [0x1fb4d8]: 1/36 s since the level started */
    int64_t timer_ticks, origin_ms; /* deterministic virtual millisecond source/origin */
    double elapsed; /* [0x1fb540]: sampled seconds since origin */
    double best_lap; /* [0x1fb390]; port starts a session record, not a saved track record */
    int ai_parity, ai_car; /* [0x1fed68], [0x1fed8c]: independent of the countdown and race tick */
} IgnRace;

#define IGN_RACE_AI_WAKE 170.0 /* cars_update_all 0x2dd3c: strictly greater than this value */

typedef enum {
    IGN_AI_CONTROLS_NONE,
    IGN_AI_CONTROLS_REPLAY,
    IGN_AI_CONTROLS_DRIVE
} IgnAiControlAction;

/* cars_update_all 0x2dcbc..0x2dd68; computer also selects the headless autopilot. */
IgnAiControlAction ign_race_ai_action(const IgnRace *r, const IgnCar *car, int index, int computer);
/* Once after every physics substep, including countdown/wrecks. ncars includes human/out cars. */
void ign_race_ai_step(IgnRace *r, int ncars);

void ign_race_init(IgnRace *r, int mode, int ncars);
/* Starts the race at once instead of after the countdown. */
void ign_race_skip_countdown(IgnRace *r);
/* sub_0002b850: advances the clocks by one physics tick; at 200 the race starts. */
void ign_race_clock(IgnRace *r);
/* Signed 32-bit timer subtraction and the original binary64 conversion constants. */
double ign_race_elapsed_ms(int32_t now, int32_t origin);
double ign_race_clock_sample(double previous, int32_t now, int32_t origin);
/* sub_0004c738: copy sampled time unless out or (outside elimination) lap >= 3. */
void ign_race_car_clock(IgnCar *car, int mode, double elapsed);
/* sub_0002a410 clears race_elapsed and recorded laps, not lap_elapsed, lap_origin or controls. */
void ign_race_reset_car_clock(IgnCar *car);
/* car_update crossing, time commit and phase decisions; positive count and nonnegative lap.
 * Samples may independently be IGN_NO_SECTION.
 * Return bits: 1 lap completed, 2 finished, 4 best lap improved. Caller owns out/wreck eligibility. */
int ign_race_progress_sample(IgnRace *r, IgnCar *car, int count, int lap_probe, int section_sample);
/* Port ground-object adapter: the two original probe families are not geometrically verified. */
void ign_race_progress(IgnRace *r, IgnCar *car, const IgnTrack *t);
int ign_race_started(const IgnRace *r);
/* sub_0004a320 0x4b717: 3, 2, 1 for clock 50/100/150..200, 0 (GO) until total 250, else -1. */
int ign_race_countdown(const IgnRace *r);
/* sub_0002de30 (elimination). Call once per physics tick after ign_race_progress.
 * Normal finishes are committed only at a valid crossing. Returns the number still racing. */
int ign_race_step(IgnRace *r, IgnCar *const *cars, int n);
/* Race position 1..n: finished cars by arrival, then by lap, phase and section. */
int ign_race_position(const IgnCar *const *cars, int n, int index);
/* sub_0003a788: wrecked cars glide to two sections ahead and are put back on the road. Once per physics tick. */
void ign_race_wrecks(IgnCar *const *cars, int n, const IgnLevel *lv, const IgnTrack *t);

#endif
