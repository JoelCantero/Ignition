#ifndef IGN_RACE_H
#define IGN_RACE_H

#include "car.h"
#include "track.h"

#define IGN_RACE_LAPS 3        /* car_update 0x2f32b: a car finishes on reaching lap 3 */
#define IGN_MODE_ELIMINATION 3 /* [0x1feefc] == 3: the last car of every lap is eliminated */
#define IGN_RACE_MAX_CARS 8

typedef struct {
    int model, computer, automatic, input_device, joystick;
    unsigned char keys[8];
    char name[16];
    int gear_delay;
} IgnRaceParticipant;

/* sub_0002293c: decoded packed IGN_DOS.BTZ race settings, not a replacement save image. */
typedef struct {
    int mode, track, round, difficulty, ncars, players_minus_one, human_count, ghost;
    int music_track, music_volume, effects_volume, effects_enabled;
    int value_107, value_10b, value_197, value_15b, value_10f, value_113, value_117, value_014;
    int grid[IGN_RACE_MAX_CARS];
    IgnRaceParticipant participants[IGN_RACE_MAX_CARS];
} IgnRaceConfig;

/* Returns -1 for an unsafe/truncated image; leaves *out unchanged. random01 is needed only for music -1. */
int ign_race_config_decode(IgnRaceConfig *out, const unsigned char *image, size_t size,
                           int cd_tracks, double (*random01)(void));
int ign_race_config_load(IgnRaceConfig *out, const char *path, int cd_tracks, double (*random01)(void));

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
    int players_minus_one;
    double finish_clock; /* [0x1fb4d0]: -1 until the human race-end transition */
    int human_finish[IGN_RACE_MAX_CARS]; /* [0x1fed9c]: -1 until end transition */
    int last_eliminated; /* port event: index this step, -1 if none; caller owns sound/HUD/effects */
    int tv_camera, camera_hold; /* [0x1fee80], [0xc0b38] when a human is eliminated */
    int elimination_limit, elimination_count; /* [0x1fede0], [0x1fed7c] */
    int section_count, grid[IGN_RACE_MAX_CARS];
    int rank_bias[IGN_RACE_MAX_CARS]; /* car+0x608: supplied by the car/grid adapter */
    int ghost_enabled, ghost_progress, ghost_position; /* ghost progress is the signed GST high word */
    int points[IGN_RACE_MAX_CARS]; /* [0x1aebb0], retained between championship rounds */
    int result_order[IGN_RACE_MAX_CARS], result_aux[IGN_RACE_MAX_CARS];
    int hud_slots[2][10]; /* [0x1fb398], [0x1fb3c0], reset to 100 */
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
/* sub_0002a97c management reset; preserves points except championship round zero.
 * Audio, weather, rendering scratch and the time-trial allocation remain owned by their subsystems. */
void ign_race_reset(IgnRace *r, int mode, int ncars, int round);
void ign_race_configure(IgnRace *r, const IgnRaceConfig *config, int section_count);
/* Full race-time reset 0x2a410: retains the best lap when n == 0; does not reset sampled elapsed. */
void ign_race_reset_times(IgnRace *r, IgnCar *const *cars, int n, int32_t now, double saved_best_lap);
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
/* sub_0004c2d8: stable signed-score ranking, finish-tie repair, startup grid and ghost override.
 * Set section_count/grid/rank_bias on r before calling. Finish-tie repair affects the NEXT score pass. */
void ign_race_rank(IgnRace *r, IgnCar *const *cars, int n);
int32_t ign_race_rank_score(const IgnRace *r, const IgnCar *car, int n, int bias);
/* Compatibility accessor for the last ign_race_rank result; never re-sorts individual cars. */
int ign_race_position(const IgnCar *const *cars, int n, int index);
/* sub_0003a788: wrecked cars glide to two sections ahead and are put back on the road. Once per physics tick. */
void ign_race_wrecks(IgnCar *const *cars, int n, const IgnLevel *lv, const IgnTrack *t);

#endif
