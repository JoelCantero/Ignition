#include "game/ai.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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
    int objects[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    uint8_t counts[12][2] = {{2, 0}, {1, 0}, {2, 0}, {1, 0}};
    IgnAiRouteRecord records[12][2][2] = {0};
    IgnTrack track = {.count = 12, .section_obj = objects, .section_obj_b = objects,
                      .ai_record_count = counts, .ai_records = records};
    const double half_pi = 1.5707963267948966;
    for (int s = 0; s < 12; s++) {
        counts[s][0] = 2;
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
    counts[1][0] = 1;
    records[1][0][0].tri_type = 0;
    records[1][0][0].point[3] = 25700;
    car.section = IGN_NO_SECTION;
    car.z = 100;
    ign_ai_init(&ai, 0);
    ai.section = 1;
    ign_fx_init(&fx);
    ign_ai_drive(&ai, &car, &track, &fx, &input);
    failed |= check(fabs(ai.side - expected_side) < 1e-12,
                    "an unavailable progress section reuses the previous AI section");
    car.section = 1;
    car.z = 100;
    ign_ai_init(&ai, 0);
    ign_fx_init(&fx);
    ai.route_selector = ai.route_choice = 1;
    ign_ai_drive(&ai, &car, &track, &fx, &input);
    failed |= check(fabs(ai.side - expected_side) < 1e-12,
                    "a normal current section keeps its direct slot despite the route choice");

    for (int s = 0; s < 12; s++) {
        counts[s][0] = 1;
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
    ign_car_sync_heading(&car);
    car.step_z = 1;
    ign_ai_init(&ai, 0);
    ign_fx_init(&fx);
    ign_ai_drive(&ai, &car, &track, &fx, &input);
    failed |= check(input.analog && input.a_steer == 0,
                    "odd signed endpoints target the truncated integer midpoint");
    car.body.heading += 0.1;
    ign_car_sync_heading(&car);
    car.spec = 4;
    ign_ai_init(&ai, 0);
    ign_ai_drive(&ai, &car, &track, &fx, &input);
    double base_steer = ai.steer;
    car.body.heading += 6 * 6.2831853071796;
    ign_ai_init(&ai, 0);
    ign_ai_drive(&ai, &car, &track, &fx, &input);
    failed |= check(ai.steer == base_steer,
                    "AI consumes the separate car heading, not accumulated body revolutions");
    for (int model = 5; model <= 8; model += 3) {
        car.spec = model;
        ign_ai_init(&ai, 0);
        ign_ai_drive(&ai, &car, &track, &fx, &input);
        failed |= check(fabs(base_steer) > 1 && fabs(ai.steer - base_steer * 1.2) < 1e-12,
                        "model indices 5/8 use the original extra steering gain");
    }
    return failed;
}

static void straight_record(IgnAiRouteRecord *record, int z, int width, int8_t surface) {
    memset(record, 0, sizeof *record);
    record->point[0] = 0, record->point[2] = z;
    record->point[3] = width, record->point[5] = z;
    record->inset[0] = 18, record->inset[1] = z;
    record->inset[2] = width - 18, record->inset[3] = z;
    record->heading[1] = (float)1.5707963267948966;
    record->surface = surface;
    record->valid = record->geometry_valid = 1;
}

/* 0x1b7a6..0x1b821 written independently of ai.c for the expected values. */
static double curve_sum(const IgnAiRouteRecord *const ahead[6], double s38, int pairs) {
    double sum = 0;
    for (int j = 0; j < pairs; j++) {
        double turn = fabs((double)ahead[j]->heading[1] - (double)ahead[j + 1]->heading[1]);
        if (turn > 3.1415926535898) turn = fabs(turn + -6.2831853071796);
        double weight = (s38 * 0.4 + 0.6) * 1.6;
        sum = turn * (weight * turn) / pairs + sum;
    }
    return sum;
}

static int test_target_clipping(void) {
    int failed = 0;
    IgnAiRouteRecord records[7];
    const IgnAiRouteRecord *ahead[6];
    IgnAiTarget target;
    for (int r = 0; r < 7; r++) straight_record(&records[r], 100 * r, 101, 1);
    for (int k = 0; k < 6; k++) ahead[k] = &records[k + 1];

    ign_ai_target(&records[0], ahead, 5, 50.5, 0, &target);
    failed |= check(target.index == 5 && target.offset == 8 && target.sign == 0,
                    "a clear straight accepts the farthest record without a side");
    failed |= check(target.x == 50 && target.z == 600, "the target is the truncated integer centre");
    failed |= check(target.first == 0 && target.curve == 0, "a straight has no heading change");
    failed |= check(target.grip == (8 - 4.0) * 0.125, "grip uses t_surface_grip of the current TRI byte");

    /* Car beside the left inset line: every far centre is seen from both insets on one side. */
    ign_ai_target(&records[0], ahead, 5, -500, 0, &target);
    failed |= check(target.index == 0 && target.sign == 1 && target.offset == 1,
                    "blocked candidates fall back to the first record and keep the last blocking side");
    failed |= check(target.x == 50 && target.z == 200,
                    "with no accepted candidate the last tested centre (record 1) is kept");

    records[0].surface = 79;
    ign_ai_target(&records[0], ahead, 5, 50.5, 0, &target);
    failed |= check(target.grip == 1, "out-of-table TRI byte 79 reads a subnormal static word");
    records[0].surface = -21;
    ign_ai_target(&records[0], ahead, 5, 50.5, 0, &target);
    failed |= check(target.grip == 1, "signed TRI byte -21 reads a zero static word");

    records[1].heading[1] = (float)1.4;
    records[2].heading[1] = (float)1.6;
    ign_ai_target(&records[0], ahead, 0.5, 50.5, 0, &target);
    failed |= check(target.first == (double)(float)1.4 - (double)(float)1.5707963267948966,
                    "first heading change is relative to the current record");
    failed |= check(target.index == 5, "heading changes alone do not clip the target");
    failed |= check(target.curve == curve_sum(ahead, 0.5, 5),
                    "curvature sums squared changes over min(index + 1, 5) pairs");

    records[1].heading[1] = (float)3.1;
    records[2].heading[1] = (float)-3.1;
    ign_ai_target(&records[0], ahead, 0.5, 50.5, 0, &target);
    failed |= check(target.curve == curve_sum(ahead, 0.5, 5) && target.curve < 1,
                    "a change across +-pi is folded once before squaring (unfolded sum would be ~15)");

    failed |= check(ign_ai_lookahead_section(10, 0, 100) == 11, "first lookahead is s + 1");
    failed |= check(ign_ai_lookahead_section(89, 5, 100) == 99 &&
                        ign_ai_lookahead_section(94, 2, 100) == 99,
                    "count - 3 and later become count - 1");
    failed |= check(ign_ai_lookahead_section(93, 2, 100) == 96, "count - 4 is kept");
    failed |= check(ign_ai_lookahead_section(95, 4, 100) == 1 &&
                        ign_ai_lookahead_section(99, 0, 100) == 0,
                    "lookahead wraps once before the end clamp");
    return failed;
}

/* stdin: s38 car_x car_z, then current and six lookahead records as
 * point[6] inset[4] centre_heading_bits surface_byte; stdout: IgnAiTarget with exact doubles. */
static int target_oracle(void) {
    double s38, car_x, car_z;
    while (scanf("%lf %lf %lf", &s38, &car_x, &car_z) == 3) {
        IgnAiRouteRecord records[7] = {0};
        for (int r = 0; r < 7; r++) {
            int32_t *point = records[r].point, *inset = records[r].inset;
            uint32_t bits;
            int surface;
            if (scanf("%d %d %d %d %d %d %d %d %d %d %u %d", &point[0], &point[1], &point[2],
                      &point[3], &point[4], &point[5], &inset[0], &inset[1], &inset[2], &inset[3],
                      &bits, &surface) != 12)
                return 2;
            memcpy(&records[r].heading[1], &bits, sizeof bits);
            records[r].surface = (int8_t)surface;
        }
        const IgnAiRouteRecord *ahead[6];
        for (int k = 0; k < 6; k++) ahead[k] = &records[k + 1];
        IgnAiTarget target;
        ign_ai_target(&records[0], ahead, s38, car_x, car_z, &target);
        printf("%d %d %d %a %a %a %a %a %a\n", target.index, target.offset, target.sign, target.x,
               target.z, target.aim, target.first, target.grip, target.curve);
    }
    return ferror(stdin) ? 2 : 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--target-oracle") == 0) return target_oracle();
    int failed = test_geometry_consumers() | test_target_clipping();
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
    failed |= check(ai.route_selector == 0 && ai.route_choice == 0,
                    "zero chance uses the permuted row but selector uses the direct track row");

    ign_ai_set_track("CANADA");
    ign_ai_init(&ai, 0);
    ign_fx_init(&fx);
    ign_fx_init(&expected_fx);
    unsigned first = ign_fx_rand15(&expected_fx);
    ign_ai_update_route_choice(&ai, 2, &fx);
    failed |= check(fx.seed == expected_fx.seed, "duplicate section rows stop after the first match");
    failed |= check(ai.route_random == (double)first / 32768.0, "only the first matching row consumes RNG");

    unsigned seed = fx.seed;
    ign_ai_update_route_choice(&ai, 3, &fx);
    failed |= check(fx.seed == seed, "unmatched section does not consume random state");
    return failed;
}
