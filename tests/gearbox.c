#include <stdio.h>
#include <string.h>

#include "game/car.h"
#include "game/physics.h"
#include "game/race.h"

static int check(int condition, const char *message) {
    if (condition) return 0;
    fprintf(stderr, "FAIL %s\n", message);
    return 1;
}

static int respawn_tests(void) {
    int failed = 0;
#define RESET_CHECK(condition) failed |= check((condition), "respawn: " #condition)
    IgnCar car = {0}, expected;
    IgnCar *cars[] = {&car};
    IgnLevel empty = {0};
    IgnTrack track = {0};
    car.gear = car.forward_gear = 2;
    car.gear_ticks = 31;
    car.gear_delay = 30;
    car.revs = 31.5;
    car.slip = 2.5;
    car.jump_mode = car.landed = 1;
    car.skid_timer = 7;
    car.spark_timer = 13;
    car.turbo_charge = 37.25;
    car.turbo_mode = 2;
    car.turbo_wait = 29;
    car.turbo_key = 1;
    car.turbo_frames = 17;
    car.out = 1;
    car.race_elapsed = 10.25;
    car.lap_elapsed = 4.25;
    car.lap_origin = 9.5;
    memcpy(&expected, &car, sizeof car);
    ign_car_init_turbo(&car);
    expected.turbo_charge = 100;
    expected.turbo_mode = expected.turbo_wait = expected.turbo_frames = 0;
    RESET_CHECK(memcmp(&car, &expected, sizeof car) == 0);
    car.turbo_wait = 29;
    car.turbo_frames = 17;
    for (int i = 0; i < 2; i++) {
        car.turbo_charge = 37.25;
        car.turbo_mode = 2;
        memcpy(&expected, &car, sizeof car);
        expected.turbo_charge = 100;
        expected.turbo_mode = 0;
        ign_car_wreck_turbo(&car);
        RESET_CHECK(memcmp(&car, &expected, sizeof car) == 0);
    }
    car.turbo_mode = 2;
    memcpy(&expected, &car, sizeof car);
    expected.gear = expected.forward_gear = 1;
    expected.gear_ticks = 0;
    expected.revs = expected.slip = 0;
    expected.jump_mode = expected.landed = expected.skid_timer = expected.spark_timer = 0;
    ign_car_respawn_controls(&car);
    RESET_CHECK(memcmp(&car, &expected, sizeof car) == 0);
    RESET_CHECK(ign_car_turbo_input(&car, 1, 10.25) == 1);
    RESET_CHECK(ign_car_turbo_input(&car, 0, 10.25) == 0);
    car.turbo_key = 0;
    RESET_CHECK(ign_car_turbo_input(&car, 1, 10.25) == 1);
    car.turbo_mode = 0;
    RESET_CHECK(ign_car_turbo_input(&car, 1, 0) == 2);
    car.turbo_key = 1;
    RESET_CHECK(ign_car_turbo_input(&car, 1, 0) == 1);
    memcpy(&expected, &car, sizeof car);
    RESET_CHECK(ign_car_respawn(&car, &empty, 10, 20, 0, 250) == -1);
    RESET_CHECK(memcmp(&car, &expected, sizeof car) == 0);
    car.out = 0;
    car.wrecked = 1;
    car.section = IGN_NO_SECTION;
    for (int kind = 1; kind <= 3; kind++)
        for (int i = 0; i < 2; i++) {
            car.wreck_kind = kind;
            car.turbo_charge = 37.25;
            car.turbo_mode = 2;
            ign_race_wrecks(cars, 1, &empty, &track);
            RESET_CHECK(car.turbo_charge == 100 && car.turbo_mode == 0);
            RESET_CHECK(car.turbo_wait == 29 && car.turbo_key == 1 && car.turbo_frames == 17);
            RESET_CHECK(car.race_elapsed == 10.25 && car.lap_elapsed == 4.25 && car.lap_origin == 9.5);
        }
    car.out = 1;
    car.turbo_charge = 37.25;
    car.turbo_mode = 2;
    memcpy(&expected, &car, sizeof car);
    expected.turbo_charge = 100;
    expected.turbo_mode = 0;
    ign_race_wrecks(cars, 1, &empty, &track);
    RESET_CHECK(memcmp(&car, &expected, sizeof car) == 0);
    car.wrecked = 0;
    car.turbo_mode = 2;
    car.turbo_charge = 37.25;
    memcpy(&expected, &car, sizeof car);
    ign_race_wrecks(cars, 1, &empty, &track);
    RESET_CHECK(memcmp(&car, &expected, sizeof car) == 0);

    int32_t verts[] = {-1000, 0, -1000, 1000, 0, -1000, 0, 0, 1000};
    IgnFace face = {.v = {0, 1, 2}};
    IgnObject object = {.nv = 3, .nf = 1, .verts = verts, .faces = &face};
    uint8_t cells[12] = {0}, list[4] = {0};
    /* One descending SRF span references face offset 2 + 3 * nv of the triangle. */
    uint8_t spans[24] = {0, 100, 0, 0, 232, 103, 0, 0, 0, 0, 1, 0,
                         0, 0, 255, 255, 48, 248, 11, 0, 0, 0, 0, 0};
    int engine_to_plc[] = {0};
    cells[8] = 1;
    IgnLevel level = {.track = {.nobj = 1, .objs = &object},
                      .surface = {.data = cells, .cell_w = 65536, .cell_h = 65536, .gx = 1, .gz = 1,
                                  .nspan = 1, .na = 1, .cells = cells, .spans = spans, .list_a = list,
                                  .list_b = list + 4, .engine_to_plc = engine_to_plc, .nengine = 1}};
    car.gear = car.forward_gear = 2;
    car.gear_ticks = 31;
    car.turbo_mode = 2;
    RESET_CHECK(ign_car_respawn(&car, &level, 10, 20, 0, 250) == 0);
    RESET_CHECK(car.gear == 1 && car.forward_gear == 1 && car.gear_ticks == 0 && car.gear_delay == 30);
    RESET_CHECK(car.turbo_mode == 2 && car.turbo_wait == 29 && car.turbo_key == 1 && car.turbo_frames == 17);
    RESET_CHECK(car.out == 1 && car.race_elapsed == 10.25 && car.lap_elapsed == 4.25 && car.lap_origin == 9.5);
    int16_t curves[3][IGN_TORQUE_POINTS] = {{0}};
    for (int i = 0; i < IGN_TORQUE_POINTS; i++) curves[2][i] = 1;
    for (int i = 1; i <= 30; i++) {
        ign_car_gearbox(&car, curves);
        RESET_CHECK(car.gear == 1 && car.gear_ticks == i);
    }
    ign_car_gearbox(&car, curves);
    RESET_CHECK(car.gear == 2 && car.gear_ticks == 31);
    ign_car_respawn_controls(&car);
    ign_car_shift(&car, 1);
    RESET_CHECK(car.gear == 2 && car.gear_ticks == 0);
#undef RESET_CHECK
    return failed;
}

static int lap_tests(void) {
    int failed = 0;
#define LAP_CHECK(condition) failed |= check((condition), "laps: " #condition)
    IgnRace race;
    ign_race_init(&race, 0, 2);
    race.elapsed = 100.5;
    IgnCar car = {.lap = 2, .phase = 3, .lap_section = 19, .section = 19,
                  .lap_elapsed = 20.25, .race_elapsed = 99.5, .position = 4};
    int sections[] = {19, 0};
    IgnTrack track = {.count = 20, .nobj = 2, .section = sections};
    car.ground_object = 0;
    ign_race_progress(&race, &car, &track);
    LAP_CHECK(car.lap == 2 && !car.out && race.finished == 0);
    car.ground_object = 1;
    ign_race_progress(&race, &car, &track);
    LAP_CHECK(car.lap == 3 && car.out && car.phase == 0 && race.finished == 1);
    LAP_CHECK(car.finish_position == 4 && car.place == 1);
    LAP_CHECK(car.last_lap == 20.25 && car.lap_times[2] == 20.25 && car.lap_origin == 100.5);
    LAP_CHECK(car.race_elapsed == 99.5 && car.lap_elapsed == 20.25 && race.best_lap == 20.25);
    IgnCar saved = car;
    ign_race_progress(&race, &car, &track);
    LAP_CHECK(!memcmp(&car, &saved, sizeof car) && race.finished == 1);
    ign_race_car_clock(&car, 0, 101);
    LAP_CHECK(car.race_elapsed == 99.5 && car.lap_elapsed == 20.25);
    IgnCar rival = {.phase = 3, .lap_section = -19, .lap_elapsed = 19.5, .ai = 1};
    LAP_CHECK(ign_race_progress_sample(&race, &rival, 20, 0, 0) == 5);
    LAP_CHECK(race.best_lap == 19.5 && !rival.out && rival.lap_times[0] == 19.5);
    rival.phase = 3; rival.lap_section = 19;
    LAP_CHECK(ign_race_progress_sample(&race, &rival, 20, 0, 0) == 1);
    LAP_CHECK(rival.lap == 2 && rival.lap_times[1] == 19.5);
    ign_race_car_clock(&rival, 0, 101);
    LAP_CHECK(rival.lap_elapsed == 0.5);
    rival.phase = 3; rival.lap_section = 19; rival.progress_lock = 1;
    LAP_CHECK(ign_race_progress_sample(&race, &rival, 20, 0, 0) == 0 && rival.lap == 2);
    rival.progress_lock = 0; rival.phase = 2;
    LAP_CHECK(ign_race_progress_sample(&race, &rival, 23, 10000, 22) == 0 && rival.phase == 2);
    LAP_CHECK(ign_race_progress_sample(&race, &rival, 23, 10000, -19) == 0 && rival.phase == 3);
    LAP_CHECK(rival.lap_section == 0);
    ign_race_reset_car_clock(&car);
    LAP_CHECK(car.last_lap == 0 && car.lap_times[2] == 0 && car.lap_origin == 100.5);
    IgnCar *cars[] = {&rival};
    rival.lap = 3; rival.out = 0;
    ign_race_step(&race, cars, 1);
    LAP_CHECK(!rival.out && race.finished == 1);
    race.mode = IGN_MODE_ELIMINATION;
    rival.lap = 2; rival.phase = 3; rival.lap_section = 19;
    LAP_CHECK(ign_race_progress_sample(&race, &rival, 20, 0, 0) == 5 && !rival.out);
    rival.wrecked = 1; rival.ground_object = 0;
    saved = rival;
    ign_race_progress(&race, &rival, &track);
    LAP_CHECK(!memcmp(&rival, &saved, sizeof rival));
#undef LAP_CHECK
    return failed;
}

int main(void) {
    int failed = respawn_tests() | lap_tests();
    IgnCar car;
    IgnLevel level;
    memset(&car, 0, sizeof car);
    memset(&level, 0, sizeof level);
    car.spec = 0;
    car.gear = car.forward_gear = 1;
    car.turbo_charge = 100.0;
    car.body.mass = 1000.0;
    car.body.inertia = 8000.0;
    car.body.restitution = 0.6;
    car.airborne = 1;
    car.gear_delay = 30;
    for (int i = 0; i < IGN_TORQUE_POINTS; i++) {
        car.torque[1][i] = 10;
        car.torque[2][i] = 20;
    }
    ign_set_race_started(1);
    ign_set_difficulty(0);

    IgnInput input = {.accel = true, .manual_gear = true, .gear_shift = 1};
    ign_car_tick(&car, &level, &input);
    failed |= check(car.gear == 2 && car.forward_gear == 2, "upshift selects second gear");
    failed |= check(car.body.drive == 2000.0, "throttle uses first gear before upshift, not second");

    input = (IgnInput){.manual_gear = true};
    ign_car_tick(&car, &level, &input);
    failed |= check(car.gear == 2, "selected gear persists without throttle");

    input.brake = true;
    ign_car_tick(&car, &level, &input);
    failed |= check(car.gear == 2, "manual braking does not select reverse");

    input = (IgnInput){.manual_gear = true, .gear_shift = -1};
    ign_car_tick(&car, &level, &input);
    failed |= check(car.gear == 1 && car.forward_gear == 1, "downshift selects first gear");

    input.gear_shift = -1;
    ign_car_tick(&car, &level, &input);
    failed |= check(car.gear == 0 && car.forward_gear == 1, "manual downshift selects reverse");
    ign_car_tick(&car, &level, &input);
    failed |= check(car.gear == 0, "downshift is bounded at reverse");

    input.gear_shift = 1;
    ign_car_tick(&car, &level, &input);
    input.gear_shift = 1;
    ign_car_tick(&car, &level, &input);
    failed |= check(car.gear == 2 && car.forward_gear == 2, "upshift is bounded at second gear");

    input = (IgnInput){.gear_shift = -1};
    ign_car_tick(&car, &level, &input);
    failed |= check(car.gear == 2, "automatic mode ignores manual shift requests");

    car.gear = 1;
    car.gear_ticks = 0;
    car.slip = 0;
    car.body.v_fwd = 0;
    for (int i = 0; i < 30; i++) ign_car_gearbox(&car, car.torque);
    failed |= check(car.gear == 1 && car.gear_ticks == 30, "counter equal to delay does not change gear");
    ign_car_gearbox(&car, car.torque);
    failed |= check(car.gear == 2 && car.gear_ticks == 31, "counter greater than delay commits gear");
    ign_car_gearbox(&car, car.torque);
    failed |= check(car.gear_ticks == 0, "agreeing target clears counter on next call");

    car.torque[2][0] = car.torque[1][0];
    car.gear_delay = 0;
    car.gear = 1;
    ign_car_gearbox(&car, car.torque);
    failed |= check(car.gear == 2, "equal signed torque chooses second in forward angle interval");

    car.gear = 1;
    car.slip = 0;
    car.body.v_fwd = 0;
    ign_car_input_direction(&car, 0, 100, 1);
    failed |= check(car.gear == 0 && car.body.sliding == 0, "automatic brake at rest selects reverse");
    failed |= check(ign_car_input_throttle(&car, 100, 25, 1) == -25, "automatic reverse uses brake pedal");
    failed |= check(ign_car_input_throttle(&car, 100, 25, 0) == -100, "manual reverse uses accelerator");
    ign_car_input_direction(&car, 100, 100, 1);
    failed |= check(car.gear == 1, "both pedals: accelerator wins forward selection after reverse");

    car.gear = 1;
    car.gear_ticks = 0;
    car.gear_delay = 0;
    car.body.v_fwd = car.body.vx = 0;
    car.body.vz = 0;
    car.body.drive = 0;
    car.body.heading = 0;
    car.turbo_mode = 0;
    car.spec = 9;
    ign_set_difficulty(3);
    input = (IgnInput){.accel = true};
    ign_car_tick(&car, &level, &input);
    failed |= check(car.body.drive == 2000.0 && car.gear == 1,
                    "automatic selection uses shared model-zero curves, not local torque or difficulty");

    car.gear = 1;
    car.gear_ticks = 0;
    car.body.v_fwd = car.body.vx = 100.0 / 3.6;
    car.body.vz = 0;
    car.body.drive = 0;
    car.body.heading = 0;
    ign_car_tick(&car, &level, &input);
    failed |= check(car.body.drive == 2000.0 && car.gear == 2 && car.gear_ticks == 1,
                    "automatic upshift applies force with the previous gear before selecting second");

    car.race_elapsed = 1.0;
    car.ai = car.out = 0;
    car.turbo_mode = car.turbo_key = car.turbo_started = 0;
    car.turbo_charge = 100;
    car.turbo_wait = 7;
    input = (IgnInput){.turbo = true};
    ign_car_tick(&car, &level, &input);
    failed |= check(car.turbo_mode == 1 && car.turbo_charge == 100 && car.turbo_wait == 0 &&
                    car.turbo_started == 1, "turbo request starts after this tick's charge arithmetic");
    car.turbo_started = 0; /* The production effects caller consumes this event. */
    ign_car_tick(&car, &level, &input);
    failed |= check(car.turbo_mode == 1 && car.turbo_charge == 97.5 && car.turbo_started == 0,
                    "held turbo consumes charge on the next tick without restarting effects");

    car.turbo_charge = 2.5;
    car.turbo_wait = 7;
    ign_car_tick(&car, &level, &input);
    failed |= check(car.turbo_mode == 0 && car.turbo_charge == 0 && car.turbo_wait == 6,
                    "exhaustion precedes held-request arbitration");
    input.turbo = false;
    ign_car_tick(&car, &level, &input);
    failed |= check(car.turbo_wait == 6, "released turbo does not decrement refusal countdown");

    ign_set_race_started(0);
    car.turbo_charge = 50;
    car.turbo_wait = car.turbo_key = 0;
    input.turbo = true;
    ign_car_tick(&car, &level, &input);
    failed |= check(car.turbo_mode == 0 && car.turbo_wait == 29 && car.turbo_key == 1,
                    "pre-start press reaches refusal arbitration, not start-boost conversion");
    input.turbo = false;
    ign_car_tick(&car, &level, &input);
    failed |= check(car.turbo_wait == 29, "pre-start release preserves refusal countdown");

    car.ai = 1;
    car.turbo_wait = 0;
    input.turbo = true;
    ign_car_tick(&car, &level, &input);
    failed |= check(car.turbo_mode == 0 && car.turbo_wait == 0, "computer refusals do not arm human beep timer");

    car.out = 1;
    car.turbo_charge = 0;
    car.turbo_wait = 12;
    failed |= check(ign_car_turbo_request(&car, 2) == IGN_TURBO_REQUEST_FORCED &&
                    car.turbo_mode == 2 && car.turbo_wait == 0,
                    "explicit forced request ignores charge, finish and start gates");

    car.ai = car.out = 0;
    car.turbo_mode = 1;
    car.turbo_key = car.turbo_started = 0;
    car.turbo_charge = 2.5;
    car.race_elapsed = 0.0;
    ign_set_race_started(1);
    ign_car_tick(&car, &level, &input);
    failed |= check(car.turbo_mode == 0 && car.turbo_key == 1 && car.turbo_charge == 0 &&
                    car.turbo_started == 0, "new press samples active mode before exhaustion, not forced boost");

    car.turbo_mode = car.turbo_key = 0;
    car.turbo_charge = 50;
    ign_set_race_started(0);
    ign_car_tick(&car, &level, &input);
    failed |= check(car.turbo_mode == 2 && car.turbo_key == 2 && car.turbo_started == 1,
                    "initial human press with elapsed time below 0.25 forces boost even before start");
    car.turbo_started = 0;
    ign_car_tick(&car, &level, &input);
    failed |= check(car.turbo_mode == 2 && car.turbo_key == 1 && car.turbo_started == 0,
                    "held key becomes request one without another initial-press event");
    input.turbo = false;
    ign_car_tick(&car, &level, &input);
    failed |= check(car.turbo_key == 0, "release commits zero to turbo latch");
    car.turbo_mode = 0;
    input.turbo = true;
    ign_car_tick(&car, &level, &input);
    failed |= check(car.turbo_mode == 2 && car.turbo_key == 2, "repress after release can force boost");

    car.turbo_mode = car.turbo_key = 0;
    car.turbo_charge = 50;
    car.race_elapsed = 0.25;
    ign_car_tick(&car, &level, &input);
    failed |= check(car.turbo_mode == 0 && car.turbo_key == 1, "elapsed time equal to 0.25 is not start boost");
    car.ai = 1;
    car.turbo_mode = car.turbo_key = car.turbo_wait = 0;
    car.race_elapsed = 0.0;
    ign_car_tick(&car, &level, &input);
    failed |= check(car.turbo_mode == 0 && car.turbo_key == 1 && car.turbo_wait == 0,
                    "AI request is not converted by the human initial-press helper");

    car.ai = 0;
    car.out = 1;
    car.turbo_key = car.turbo_mode = 0;
    failed |= check(ign_car_turbo_input(&car, 1, -1) == 2 && car.turbo_key == 0,
                    "generation ignores finish and negative elapsed time, leaving latch commit to caller");
    failed |= check(ign_car_turbo_input(&car, 2, 0) == 0, "only sampled key value one counts as pressed");
    car.wrecked = 1;
    failed |= check(ign_car_turbo_input(&car, 1, 0) == 0, "aggregated wreck flags block generation");
    IgnRace race;
    ign_race_init(&race, 0, 2);
    for (int i = 0; i < 400; i++) ign_race_clock(&race);
    failed |= check(!ign_race_started(&race) && race.elapsed == 5.555 &&
                    race.clock == 5555 * 0.036, "virtual integer milliseconds remain before start at tick 400");
    ign_race_clock(&race);
    failed |= check(ign_race_started(&race) && race.elapsed == 5.569 && race.origin_ms == 5569,
                    "normal start resets origin, retaining the previously sampled global time");
    ign_race_clock(&race);
    failed |= check(race.elapsed == 0.014 && race.clock == -1, "next sample uses the new origin");
    car.out = car.wrecked = car.lap = car.turbo_key = car.turbo_mode = 0;
    car.lap_origin = 0;
    ign_race_car_clock(&car, 0, ign_race_elapsed_ms(249, 0));
    failed |= check(ign_car_turbo_input(&car, 1, car.race_elapsed) == 2, "produced 249 ms permits start turbo");
    ign_race_car_clock(&car, 0, ign_race_elapsed_ms(250, 0));
    failed |= check(ign_car_turbo_input(&car, 1, car.race_elapsed) == 1, "produced 250 ms closes start turbo");
    car.wrecked = 1;
    ign_race_car_clock(&car, 0, 2);
    failed |= check(car.race_elapsed == 2, "wreck does not freeze car time");
    car.out = 1;
    ign_race_car_clock(&car, 3, 3);
    failed |= check(car.race_elapsed == 2, "eliminated car freezes time");
    car.out = 0;
    car.lap = 3;
    ign_race_car_clock(&car, 0, 4);
    failed |= check(car.race_elapsed == 2, "third lap freezes time outside elimination");
    ign_race_car_clock(&car, 3, 5);
    failed |= check(car.race_elapsed == 5, "third lap keeps time in elimination");
    car.lap_elapsed = 3;
    car.lap_origin = 2;
    car.turbo_key = 2;
    car.turbo_mode = 1;
    ign_race_reset_car_clock(&car);
    failed |= check(car.race_elapsed == 0 && car.lap_elapsed == 3 && car.lap_origin == 2 &&
                    car.turbo_key == 2 && car.turbo_mode == 1,
                    "start reset preserves lap clock, lap origin and turbo state");
    IgnCar other = car;
    other.lap = 0;
    other.lap_origin = 1;
    ign_race_car_clock(&other, 0, 6);
    failed |= check(other.race_elapsed == 6 && other.lap_elapsed == 5 &&
                    car.race_elapsed == 0 && car.lap_elapsed == 3 && car.lap_origin == 2,
                    "car clock fields are independent");
    ign_race_init(&race, 0, 1);
    ign_race_skip_countdown(&race);
    ign_race_clock(&race);
    failed |= check(race.elapsed == 13 * 0.001 && ign_race_started(&race), "headless skip has a fresh virtual origin");
    ign_set_race_started(1);
    return failed ? 1 : 0;
}
