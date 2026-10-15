#include "game/ai.h"

#include <math.h>
#include <stdio.h>

static int check(int ok, const char *message) {
    if (!ok) fprintf(stderr, "FAIL: %s\n", message);
    return !ok;
}

static int test_geometry_consumers(void) {
    int failed = 0;
    IgnAi ai;
    IgnFx fx;
    IgnInput input;
    IgnCar car = {0};
    int objects[4] = {0, 1, 2, 3};
    unsigned char has_b[4] = {1, 0, 1, 0};
    float gates[4][4] = {{0}};
    uint8_t counts[4][2] = {{2, 0}, {1, 0}, {2, 0}, {1, 0}};
    IgnAiRouteRecord records[4][2][2] = {0};
    IgnTrack track = {.count = 4, .section_obj = objects, .has_b = has_b, .gate = gates,
                      .ai_record_count = counts, .ai_records = records};
    const double half_pi = 1.5707963267948966;
    for (int s = 0; s < 4; s++) {
        for (int slot = 0; slot < counts[s][0]; slot++) {
            IgnAiRouteRecord *record = &records[s][0][slot];
            record->valid = record->geometry_valid = 1;
            record->tri_type = counts[s][0] == 2 ? 2 : 0;
            record->point[0] = 25600 + (counts[s][0] == 2 ? slot * 50 : 0);
            record->point[3] = 25600 + (counts[s][0] == 2 ? (slot + 1) * 50 : 100);
            record->point[2] = record->point[5] = 25600 + s * 100;
            record->heading[1] = 0;
        }
    }
    ign_ai_set_track("CANADA");
    IgnCar *cars[] = {&car};
    ign_ai_set_cars(cars, 1);
    car.x = 40;
    car.body.v_fwd = 10;
    double expected_side = -(60 + hypot(60, 100)) / (40 + hypot(40, 100));
    for (int slot = 0; slot < 2; slot++) {
        ign_ai_init(&ai, 0);
        ign_fx_init(&fx);
        ai.route_selector = 1;
        ai.route_choice = slot;
        ign_ai_drive(&ai, &car, &track, &fx, &input);
        failed |= check(fabs(ai.side - expected_side) < 1e-12,
                        "split side ratio uses the whole gate for either slot");
        failed |= check(ai.stuck == 0, "road error consumes stored float32 heading, not gate direction");
    }
    records[1][0][0].valid = 0;
    car.section = 1;
    car.z = 200;
    ign_ai_init(&ai, 0);
    ign_fx_init(&fx);
    ign_ai_drive(&ai, &car, &track, &fx, &input);
    failed |= check(fabs(ai.side + 1.5) < 1e-12,
                    "whole gate is reconstructed from the resolved split section");
    records[1][0][0].valid = 1;
    car.z = 100;
    ign_ai_init(&ai, 0);
    ign_fx_init(&fx);
    ai.route_selector = ai.route_choice = 1;
    ign_ai_drive(&ai, &car, &track, &fx, &input);
    failed |= check(fabs(ai.side - expected_side) < 1e-12,
                    "missing split slot resolves to the next normal gate");

    for (int s = 0; s < 4; s++) {
        counts[s][0] = 1;
        has_b[s] = 0;
        records[s][0][0].tri_type = 0;
        records[s][0][0].point[0] = -2;
        records[s][0][0].point[3] = 5;
        records[s][0][0].point[2] = records[s][0][0].point[5] = 25600 + 20 * s;
        records[s][0][0].heading[1] = (float)half_pi;
    }
    records[1][0][0].heading[1] = (float)(half_pi + 0.1);
    records[2][0][0].heading[1] = (float)(half_pi - 0.1);
    car.section = 0;
    car.x = -25599;
    car.z = 0;
    car.body.v_fwd = 0;
    car.body.heading = half_pi;
    car.step_z = 1;
    ign_ai_init(&ai, 0);
    ign_fx_init(&fx);
    ign_ai_drive(&ai, &car, &track, &fx, &input);
    failed |= check(input.analog && input.a_steer == 0,
                    "odd signed endpoints target the truncated integer midpoint");
    return failed;
}

int main(void) {
    int failed = test_geometry_consumers();
    IgnAi ai;
    IgnFx fx, expected_fx;

    ign_ai_set_track("CANADA");
    ign_ai_init(&ai, 0);
    ign_fx_init(&fx);
    ign_ai_update_route_choice(&ai, 10, &fx);
    failed |= check(ai.route_selector == 0 && ai.route_choice == 1, "section 10 threshold selects route 1");
    failed |= check(ai.route_random == 16838.0 / 32768.0, "rand() uses the original 15-bit scale");

    ign_ai_set_track("AUSTRIA");
    ign_ai_init(&ai, 0);
    ign_fx_init(&fx);
    ign_ai_update_route_choice(&ai, 5, &fx);
    failed |= check(ai.route_selector == 0 && ai.route_choice == 1, "100-percent threshold forces route 1");

    ign_ai_set_track("JAPAN");
    ign_ai_init(&ai, 0);
    ign_fx_init(&fx);
    ign_ai_update_route_choice(&ai, 190, &fx);
    failed |= check(ai.route_selector == 1 && ai.route_choice == 0, "zero threshold forces route 0 and retains selector");

    ign_ai_set_track("CANADA");
    ign_ai_init(&ai, 0);
    ign_fx_init(&fx);
    ign_fx_init(&expected_fx);
    ign_fx_rand15(&expected_fx);
    unsigned second = ign_fx_rand15(&expected_fx);
    ign_ai_update_route_choice(&ai, 2, &fx);
    failed |= check(fx.seed == expected_fx.seed, "duplicate section rows each consume one random value");
    failed |= check(ai.route_random == (double)second / 32768.0, "duplicate section rows retain the last random value");

    unsigned seed = fx.seed;
    ign_ai_update_route_choice(&ai, 3, &fx);
    failed |= check(fx.seed == seed, "unmatched section does not consume random state");
    return failed;
}
