#include "game/ai.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *tracks[] = {"CANADA", "USA", "CARIB", "BRAZIL", "AUSTRIA", "ICELAND", "JAPAN"};

static int check(int ok, const char *message) {
    if (!ok) fprintf(stderr, "FAIL: %s\n", message);
    return !ok;
}

static IgnCar fixture_car(void) {
    IgnCar car = {0};
    car.gear = car.forward_gear = 1;
    car.grip_avg = 0.4;
    car.body.mass = 500;
    car.body.inertia = 1000;
    car.body.a = car.body.b = 1.0;
    car.body.grip_f = car.body.grip_r = 4.2;
    for (int g = 0; g < 3; g++) ign_torque_curve(ign_car_gears[0][g], car.torque[g]);
    return car;
}

static int test_rotation(void) {
    int failed = 0;
    for (int n = 1; n <= 8; n++) {
        IgnRace r;
        IgnCar car = {0};
        ign_race_init(&r, 0, n);
        failed |= check(r.ai_parity == 0 && r.ai_car == 0, "initial phase and selector are zero");
        ign_race_skip_countdown(&r);
        int drives[8] = {0}, replays[8] = {0};
        for (int tick = 0; tick < 2 * n * 4; tick++) {
            failed |= check(r.ai_parity == tick % 2 && r.ai_car == ((tick + 1) / 2) % n,
                            "advance on phase zero, then toggle");
            for (int i = 0; i < n; i++) {
                IgnAiControlAction a = ign_race_ai_action(&r, &car, i, 1);
                drives[i] += a == IGN_AI_CONTROLS_DRIVE;
                replays[i] += a == IGN_AI_CONTROLS_REPLAY;
                failed |= check((a == IGN_AI_CONTROLS_NONE) == (tick % 2 == 0), "36 Hz control phase");
            }
            ign_race_ai_step(&r, n);
        }
        for (int i = 0; i < n; i++)
            failed |= check(drives[i] == 4 && replays[i] == 4 * (n - 1),
                            "each slot gets one decision per N control turns");
        r.ai_parity = 1;
        car.wrecked = 1;
        failed |= check(ign_race_ai_action(&r, &car, r.ai_car, 1) == IGN_AI_CONTROLS_NONE,
                        "wrecked selected slot does not run controls");
        car.wrecked = 0;
        failed |= check(ign_race_ai_action(&r, &car, r.ai_car, 0) == IGN_AI_CONTROLS_NONE,
                        "human selected slot is not skipped in rotation");
        car.progress_lock = 1;
        failed |= check(ign_race_ai_action(&r, &car, r.ai_car, 0) == IGN_AI_CONTROLS_DRIVE,
                        "progress lock one enables human AI");
        car.progress_lock = 2;
        failed |= check(ign_race_ai_action(&r, &car, r.ai_car, 0) == IGN_AI_CONTROLS_NONE,
                        "other lock values do not enable human AI");
        car.out = 1;
        failed |= check(ign_race_ai_action(&r, &car, r.ai_car, 1) == IGN_AI_CONTROLS_DRIVE,
                        "out flag is not the unknown original +0x350 flag");
        r.remaining = 0;
        ign_race_ai_step(&r, n);
        ign_race_ai_step(&r, n);
        failed |= check(r.ai_car == 1 % n, "remaining/finish counts do not shrink rotation");
        int phase = r.ai_parity, selected = r.ai_car;
        ign_race_skip_countdown(&r);
        failed |= check(r.ai_parity == phase && r.ai_car == selected, "race start does not reset rotation");
    }
    const double clocks[] = {-1.0, nextafter(0, -INFINITY), 0, nextafter(170, -INFINITY),
                             170, nextafter(170, INFINITY), 200};
    IgnRace r = {.ai_parity = 1};
    IgnCar car = {0};
    for (unsigned i = 0; i < sizeof clocks / sizeof clocks[0]; i++) {
        r.clock = clocks[i];
        int drive = i < 2 || i > 4;
        failed |= check(ign_race_ai_action(&r, &car, 0, 1) ==
                        (drive ? IGN_AI_CONTROLS_DRIVE : IGN_AI_CONTROLS_REPLAY),
                        "strict clock boundaries, replay during countdown");
    }
    return failed;
}

static int test_controls(void) {
    int failed = 0;
    IgnAi ai;
    IgnFx fx;
    IgnRace r;
    IgnCar car = fixture_car(), expected;
    IgnCar *cars[] = {&car};
    ign_ai_init(&ai, 0);
    ign_fx_init(&fx);
    ign_ai_set_cars(cars, 1);
    ign_ai_set_track("CANADA");
    ign_race_init(&r, 0, 1);
    ign_set_race_started(1);
    float gates[4][4] = {{-10, 0, 10, 0}, {-10, 100, 10, 100},
                         {-10, 200, 10, 200}, {-10, 300, 10, 300}};
    IgnTrack track = {.count = 4, .gate = gates};
    car.section = 2;
    ai.input = (IgnInput){.analog = true, .a_accel = 31, .a_brake = 72, .a_steer = -63, .turbo = true};
    ai.turbo_request = 2;
    IgnInput cached = ai.input;
    IgnCar before = car;
    unsigned seed = fx.seed;
    failed |= check(ign_ai_control_tick(&ai, &car, &track, &fx, &r, 0) == IGN_AI_CONTROLS_NONE &&
                    memcmp(&car, &before, sizeof car) == 0 && fx.seed == seed, "phase zero is a pure no-op");

    r.ai_parity = 1; /* Selected car still replays at the exact countdown threshold. */
    r.clock = 170;
    expected = car;
    ign_car_auto_gearbox(&expected);
    ign_car_apply_input(&expected, &cached, 2);
    failed |= check(ign_ai_control_tick(&ai, &car, &track, &fx, &r, 0) == IGN_AI_CONTROLS_REPLAY &&
                    memcmp(&car, &expected, sizeof car) == 0 &&
                    memcmp(&ai.input, &cached, sizeof cached) == 0 && ai.turbo_request == 2 && fx.seed == seed,
                    "replay retains pedals/steering/turbo, applies once, and consumes no RNG");
    failed |= check(car.turbo_key == before.turbo_key,
                    "replay does not resample or commit the human turbo latch");
    r.clock = -1;
    car.body.v_fwd = 20;
    failed |= check(ign_ai_control_tick(&ai, &car, &track, &fx, &r, 0) == IGN_AI_CONTROLS_DRIVE &&
                    fx.seed != seed && ai.input.analog && !car.ai,
                    "fresh decision consumes route RNG without changing the human autopilot profile");
    cached = ai.input;
    seed = fx.seed;
    r.ai_car = 1;
    for (int i = 0; i < 8; i++)
        failed |= check(ign_ai_control_tick(&ai, &car, &track, &fx, &r, 0) == IGN_AI_CONTROLS_REPLAY &&
                        fx.seed == seed && memcmp(&ai.input, &cached, sizeof cached) == 0,
                        "repeated replays do not decide again");
    car.wrecked = 1;
    before = car;
    failed |= check(ign_ai_control_tick(&ai, &car, &track, &fx, &r, 0) == IGN_AI_CONTROLS_NONE &&
                    memcmp(&car, &before, sizeof car) == 0 && fx.seed == seed, "wreck retains cached controls");

    car = fixture_car();
    ai.input = (IgnInput){.analog = true, .a_accel = 90};
    ai.turbo_request = 0;
    car.gear = 0;
    car.progress_lock = 1;
    expected = car;
    ign_car_auto_gearbox(&expected);
    ign_car_apply_input(&expected, &ai.input, 0);
    ign_ai_control_tick(&ai, &car, &track, &fx, &r, 0);
    failed |= check(memcmp(&car, &expected, sizeof car) == 0, "locked reverse runs gearbox before input");

    ai.input.manual_gear = true;
    r.ai_car = 0;
    ign_ai_control_tick(&ai, &car, &track, &fx, &r, 0);
    failed |= check(ai.input.manual_gear, "AI decisions do not overwrite human gearbox configuration");

    car = fixture_car();
    car.gear = 1;
    ai.input = (IgnInput){.analog = true, .manual_gear = true, .gear_shift = 1};
    ai.turbo_request = 0;
    r.ai_car = 1;
    ign_ai_control_tick(&ai, &car, &track, &fx, &r, 0);
    failed |= check(car.gear == 1, "replay does not repeat one-shot manual shifts");

    car = fixture_car();
    car.body.drive = 1234;
    car.body.steer = 0.2;
    car.turbo_charge = 73;
    IgnLevel level = {0};
    double px = car.body.px;
    for (int i = 0; i < 72; i++) ign_car_tick(&car, &level, NULL);
    failed |= check(car.body.px != px && isfinite(car.body.px) &&
                    car.body.drive == 1234 && car.body.steer == 0.2 && car.turbo_charge == 73,
                    "72 physics-only steps move without applying controls/turbo");
    return failed;
}

/* Small strict text protocol for the independent LE-fragment oracle. */
static int oracle(void) {
    char mode;
    while (scanf(" %c", &mode) == 1) {
        IgnCar car = {0};
        IgnAi ai;
        IgnRace r;
        ign_ai_init(&ai, 0);
        if (mode == 'S') {
            int n, index, computer;
            if (scanf("%d%d%d%d%d%lf%d%d%d", &n, &r.ai_parity, &r.ai_car, &index, &computer,
                      &r.clock, &car.wrecked, &car.progress_lock, &car.out) != 9) return 2;
            int action = ign_race_ai_action(&r, &car, index, computer);
            ign_race_ai_step(&r, n);
            printf("%d %d %d\n", action, r.ai_parity, r.ai_car);
        } else if (mode == 'K') {
            int track, n;
            double error;
            if (scanf("%d%d%d%lf%lf%d%d%lf", &track, &n, &ai.stuck, &error, &car.body.v_fwd,
                      &car.airborne, &car.section, &ai.side) != 8 || track < 0 || track > 6) return 2;
            ign_ai_set_track(tracks[track]);
            ign_ai_stuck_step(&ai, &car, error, n);
            printf("%d\n", ai.stuck);
        } else {
            fprintf(stderr, "Unknown AI oracle mode: %c\n", mode);
            return 2;
        }
    }
    if (ferror(stdin)) return 2;
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--oracle")) return oracle();
    if (argc != 1) return 2;
    return test_rotation() | test_controls();
}
