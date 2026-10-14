#include "game/car.h"
#include "game/race.h"

#include <math.h>
#include <stdio.h>

int main(void) {
    char mode;
    int count;
    while ((count = scanf(" %c", &mode)) != EOF) {
        IgnCar car = {0};
        if (mode == 'C') {
            int ticks, now, origin, delta, race_mode, key;
            double clock;
            if (scanf("%d %d %d %d %lf %d %d %d %d %lf %lf %lf %d %d %d",
                      &ticks, &now, &origin, &delta, &clock, &race_mode, &car.lap,
                      &car.out, &car.wrecked, &car.race_elapsed, &car.lap_elapsed,
                      &car.lap_origin, &key, &car.turbo_key, &car.turbo_mode) != 15 ||
                ticks < 1 || ticks > 10000 || delta < 0 || delta > 10000 ||
                !isfinite(clock) || race_mode < 0 || race_mode > 3 || car.lap < 0 ||
                car.lap > 4 || (car.out != 0 && car.out != 1) ||
                (car.wrecked != 0 && car.wrecked != 1) || !isfinite(car.race_elapsed) ||
                !isfinite(car.lap_elapsed) || !isfinite(car.lap_origin) ||
                key < 0 || key > 2 || car.turbo_key < 0 || car.turbo_key > 2 ||
                car.turbo_mode < 0 || car.turbo_mode > 2) {
                fprintf(stderr, "Invalid race clock fixture\n");
                return 1;
            }
            for (int i = 0; i < ticks; i++) {
                double elapsed = ign_race_elapsed_ms(now, origin);
                double previous = clock;
                clock = ign_race_clock_sample(clock, now, origin);
                if (previous > -1 && clock == -1) {
                    ign_race_reset_car_clock(&car);
                    origin = now;
                }
                ign_race_car_clock(&car, race_mode, elapsed);
                int request = ign_car_turbo_input(&car, key, car.race_elapsed);
                car.turbo_key = request;
                printf("%.17g %.17g %.17g %.17g %.17g %d %d %d\n", elapsed, clock,
                       car.race_elapsed, car.lap_elapsed, car.lap_origin, origin, request, car.turbo_key);
                uint32_t next = (uint32_t)now + (uint32_t)delta;
                int64_t signed_next = next;
                if (next > INT32_MAX) signed_next -= INT64_C(4294967296);
                now = (int32_t)signed_next;
            }
        } else if (mode == 'P') {
            int ticks, key, exploded, sunk, slid, pattern;
            double elapsed;
            if (scanf("%d %d %d %d %d %d %d %lf %d", &ticks, &key, &car.turbo_key,
                      &car.turbo_mode, &exploded, &sunk, &slid, &elapsed, &pattern) != 9 ||
                ticks < 1 || ticks > 10000 || key < 0 || key > 2 ||
                car.turbo_key < 0 || car.turbo_key > 2 || car.turbo_mode < 0 || car.turbo_mode > 2 ||
                (exploded != 0 && exploded != 1) || (sunk != 0 && sunk != 1) ||
                (slid != 0 && slid != 1) || !isfinite(elapsed) || pattern < 0 || pattern > 2) {
                fprintf(stderr, "Invalid turbo input fixture\n");
                return 1;
            }
            car.wrecked = exploded || sunk || slid;
            for (int i = 0; i < ticks; i++) {
                int sampled = pattern == 1 ? (i % 2 ? key : 0) :
                              pattern == 2 ? i % 3 : key;
                int request = ign_car_turbo_input(&car, sampled, elapsed);
                car.turbo_key = request;
                printf("%d %d\n", request, car.turbo_key);
            }
        } else if (mode == 'J') {
            int ticks, request, pattern;
            double clock;
            if (scanf("%d %d %d %lf %d %d %d %lf %d", &ticks, &request, &car.turbo_mode,
                      &car.turbo_charge, &car.turbo_wait, &car.ai, &car.out, &clock, &pattern) != 9 ||
                ticks < 1 || ticks > 10000 || request < 0 || request > 2 ||
                car.turbo_mode < 0 || car.turbo_mode > 2 || !isfinite(car.turbo_charge) ||
                car.turbo_charge < 0 || car.turbo_charge > 1000 ||
                car.turbo_wait < -10000 || car.turbo_wait > 10000 ||
                (car.ai != 0 && car.ai != 1) || (car.out != 0 && car.out != 1) ||
                !isfinite(clock) || pattern < 0 || pattern > 2) {
                fprintf(stderr, "Invalid turbo request fixture\n");
                return 1;
            }
            ign_set_race_started(clock < 0);
            for (int i = 0; i < ticks; i++) {
                int input = pattern == 1 ? (i % 2 ? request : 0) :
                            pattern == 2 ? i % 3 : request;
                car.turbo_started = 0;
                IgnTurboRequestEvent event = ign_car_turbo_request(&car, input);
                printf("%d %d %d\n", event, car.turbo_mode, car.turbo_wait);
            }
        } else if (mode == 'D' || mode == 'A') {
            int accel, brake, automatic, started;
            if (scanf("%d %d %d %d %d %lf %lf %d %d", &car.gear, &accel, &brake, &automatic,
                      &started, &car.slip, &car.body.v_fwd, &car.body.sliding, &car.skid_timer) != 9 ||
                car.gear < 0 || car.gear > 2 || accel < 0 || accel > 100 || brake < 0 || brake > 100 ||
                (automatic != 0 && automatic != 1) || (started != 0 && started != 1) ||
                !isfinite(car.slip) || !isfinite(car.body.v_fwd) || fabs(car.body.v_fwd) > 10000 ||
                (car.body.sliding != 0 && car.body.sliding != 1) || car.skid_timer < 0) {
                fprintf(stderr, "Invalid input fixture\n");
                return 1;
            }
            ign_set_race_started(started);
            if (mode == 'D')
                printf("%d\n", ign_car_input_throttle(&car, accel, brake, automatic));
            else {
                ign_car_input_direction(&car, accel, brake, automatic);
                printf("%d %d %d\n", car.gear, car.body.sliding, car.skid_timer);
            }
        } else if (mode == 'K') {
            int ticks, pattern;
            if (scanf("%d %d %d %d %lf %lf %d", &ticks, &car.gear, &car.gear_ticks, &car.gear_delay,
                      &car.body.v_fwd, &car.slip, &pattern) != 7 ||
                ticks < 1 || ticks > 10000 || car.gear < 0 || car.gear > 2 ||
                car.gear_ticks < 0 || car.gear_ticks > 10000 || car.gear_delay < 0 || car.gear_delay > 10000 ||
                !isfinite(car.slip) || !isfinite(car.body.v_fwd) || fabs(car.body.v_fwd) > 10000 ||
                pattern < -1 || pattern > 3) {
                fprintf(stderr, "Invalid gearbox fixture\n");
                return 1;
            }
            for (int i = 0; i < IGN_TORQUE_POINTS; i++) {
                car.torque[1][i] = i - 100;
                car.torque[2][i] = (pattern == 0 ? i : pattern == 1 ? 199 - i : 0) - 100;
                if (pattern == 2) car.torque[2][i] = 99 - i;
                if (pattern == 3) {
                    car.torque[1][i] = i % 2 ? INT16_MIN : INT16_MAX;
                    car.torque[2][i] = i % 2 ? INT16_MAX : INT16_MIN;
                }
            }
            double speed = car.body.v_fwd, slip = car.slip;
            for (int i = 0; i < ticks; i++) {
                if (pattern == 2) {
                    car.body.v_fwd = i % 3 == 0 ? 0 : i % 3 == 1 ? speed : -speed;
                    car.slip = i % 4 == 0 ? slip : 3.14159265358979323846;
                }
                ign_car_gearbox(&car, car.torque);
                printf("%d %d\n", car.gear, car.gear_ticks);
            }
        } else if (mode == 'H') {
            int shift;
            if (scanf("%d %d", &car.gear, &shift) != 2 || car.gear < 0 || car.gear > 2 ||
                shift < -1 || shift > 1 || shift == 0) {
                fprintf(stderr, "Invalid manual shift fixture\n");
                return 1;
            }
            ign_car_shift(&car, shift);
            printf("%d\n", car.gear);
        } else if (mode == 'S') {
            int ticks, input, analog;
            double grip;
            if (scanf("%d %d %d %d %d %f %lf %lf %lf", &ticks, &car.spec, &car.ai, &analog,
                      &input, &car.kmh, &grip, &car.steer_visual, &car.body.steer) != 9 ||
                ticks < 1 || ticks > 10000 || car.spec < 0 || car.spec > 11 ||
                (car.ai != 0 && car.ai != 1) || (analog != 0 && analog != 1) ||
                input < -100 || input > 100 || !isfinite(car.kmh) || !isfinite(grip) || grip <= 0 ||
                !isfinite(car.steer_visual) || !isfinite(car.body.steer)) {
                fprintf(stderr, "Invalid steering fixture\n");
                return 1;
            }
            for (int i = 0; i < ticks; i++) {
                ign_car_steer(&car, input, grip, analog);
                printf("%.17g %.17g\n", car.steer_visual, car.body.steer);
            }
        } else if (mode == 'G') {
            int level, ticks, types[4];
            if (scanf("%d %d %d %d", &car.spec, &car.ai, &level, &ticks) != 4 ||
                car.spec < 0 || car.spec > 11 || (car.ai != 0 && car.ai != 1) ||
                level < 0 || level > 3 || ticks < 1 || ticks > 10000) {
                fprintf(stderr, "Invalid grip fixture\n");
                return 1;
            }
            car.body.grip_f = 7;
            car.body.grip_r = 8;
            car.grip_avg = 0.4;
            ign_set_difficulty(level);
            for (int i = 0; i < ticks; i++) {
                if (scanf("%d %d %d %d", &types[0], &types[1], &types[2], &types[3]) != 4) {
                    fprintf(stderr, "Invalid grip surface fixture\n");
                    return 1;
                }
                double avg = ign_car_update_grip(&car, types);
                printf("%.17g %.17g %.17g\n", avg, car.body.grip_f, car.body.grip_r);
            }
        } else if (mode == 'V') {
            int ticks;
            if (scanf("%d %f %lf %lf %lf %lf %d %d", &ticks, &car.kmh, &car.height, &car.ground,
                      &car.ground_prev, &car.fall_speed, &car.airborne, &car.level_change) != 8 ||
                ticks < 1 || ticks > 10000 || !isfinite(car.kmh) || !isfinite(car.height) ||
                !isfinite(car.ground) || !isfinite(car.ground_prev) || !isfinite(car.fall_speed) ||
                (car.airborne != 0 && car.airborne != 1) ||
                (car.level_change != 0 && car.level_change != 1)) {
                fprintf(stderr, "Invalid vertical fixture\n");
                return 1;
            }
            for (int i = 0; i < ticks; i++) {
                double drop;
                int landed = ign_car_vertical_step(&car, &drop);
                printf("%.17g %.17g %.17g %d %d\n", car.height, car.fall_speed, drop, car.airborne, landed);
            }
        } else if (mode == 'R') {
            int ticks, input, index, started;
            if (scanf("%d %d %d %d %d %d %d %d %d %f %lf %d", &ticks, &car.spec, &car.ai,
                      &car.gear, &input, &index, &started, &car.airborne, &car.skidding,
                      &car.kmh, &car.revs, &car.out) != 12 ||
                ticks < 1 || ticks > 10000 || car.spec < 0 || car.spec > 11 ||
                (car.ai != 0 && car.ai != 1) || car.gear < 0 || car.gear > 2 ||
                input < -100 || input > 100 || index < 0 || index > 199 ||
                (started != 0 && started != 1) || (car.airborne != 0 && car.airborne != 1) ||
                (car.skidding != 0 && car.skidding != 1) || !isfinite(car.kmh) || !isfinite(car.revs) ||
                (car.out != 0 && car.out != 1)) {
                fprintf(stderr, "Invalid rev fixture\n");
                return 1;
            }
            ign_set_race_started(started);
            for (int i = 0; i < ticks; i++) {
                ign_car_update_revs(&car, input, index);
                printf("%.17g\n", car.revs);
            }
        } else if (mode == 'T' || mode == 'Q') {
            int ticks, level;
            if (scanf("%d %d %d %d %d %d %d %lf %lf %lf %d %d %d %lf %f",
                      &ticks, &car.spec, &car.ai, &level, &car.gear, &car.creep, &car.creep_ticks,
                      &car.catchup, &car.catchup_clock, &car.body.drive, &car.torque_bonus,
                      &car.position, &car.skid_timer, &car.slip, &car.kmh) != 15 ||
                ticks < 1 || ticks > 10000 || car.spec < 0 || car.spec > 11 ||
                (car.ai != 0 && car.ai != 1) || level < 0 || level > 3 || car.gear < 0 || car.gear > 2 ||
                car.creep < 0 || car.creep > 1000 || car.creep_ticks < 0 || car.creep_ticks > 10000 ||
                car.torque_bonus < -1000 || car.torque_bonus > 1000 || car.position < 1 || car.position > 8 ||
                !isfinite(car.catchup) || !isfinite(car.catchup_clock) || !isfinite(car.body.drive) ||
                !isfinite(car.slip) || !isfinite(car.kmh)) {
                fprintf(stderr, "Invalid throttle fixture\n");
                return 1;
            }
            if (mode == 'Q' && (scanf("%lf %d", &car.revs, &car.skidding) != 2 ||
                               !isfinite(car.revs) || (car.skidding != 0 && car.skidding != 1))) {
                fprintf(stderr, "Invalid throttle/rev fixture\n");
                return 1;
            }
            ign_set_difficulty(level);
            for (int g = 0; g < 3; g++)
                for (int i = 0; i < IGN_TORQUE_POINTS; i++) car.torque[g][i] = (int16_t)(i - 5 + g);
            for (int i = 0; i < ticks; i++) {
                int input, started;
                double clock;
                if (scanf("%d %lf %lf %d %d %d", &input, &car.body.v_fwd, &clock, &started,
                          &car.airborne, &car.body.saturated) != 6 ||
                    input < -100 || input > 100 || !isfinite(car.body.v_fwd) ||
                    fabs(car.body.v_fwd) > 10000 || !isfinite(clock) ||
                    (started != 0 && started != 1) || (car.airborne != 0 && car.airborne != 1) ||
                    (car.body.saturated != 0 && car.body.saturated != 1)) {
                    fprintf(stderr, "Invalid throttle step\n");
                    return 1;
                }
                ign_set_race_started(started);
                ign_set_level_clock(clock);
                int index = ign_car_throttle(&car, input);
                if (mode == 'Q') ign_car_update_revs(&car, input, index);
                printf("%.17g %d %d %.17g %.17g %d", car.body.drive, car.creep, car.creep_ticks,
                       car.catchup, car.catchup_clock, car.skid_timer);
                if (mode == 'Q') printf(" %.17g %d", car.revs, index);
                putchar('\n');
            }
        } else if (mode == 'B') {
            int ticks, input, started;
            if (scanf("%d %d %d %lf %d %d", &ticks, &input, &started, &car.body.v_fwd,
                      &car.body.sliding, &car.skid_timer) != 6 ||
                ticks < 1 || ticks > 10000 || input < 0 || input > 100 ||
                (started != 0 && started != 1) || !isfinite(car.body.v_fwd) ||
                (car.body.sliding != 0 && car.body.sliding != 1)) {
                fprintf(stderr, "Invalid brake fixture\n");
                return 1;
            }
            ign_set_race_started(started);
            for (int i = 0; i < ticks; i++) {
                ign_car_brake(&car, input);
                printf("%d %d\n", car.body.sliding, car.skid_timer);
            }
        } else if (mode == 'U') {
            int ticks;
            if (scanf("%d %d %d %d %lf %d %lf %lf %lf %lf %lf", &ticks, &car.spec, &car.ai,
                      &car.turbo_mode, &car.turbo_charge, &car.turbo_frames, &car.body.v_fwd,
                      &car.body.drive, &car.body.vx, &car.body.vz, &car.body.heading) != 11 ||
                ticks < 1 || ticks > 10000 || car.spec < 0 || car.spec > 11 ||
                (car.ai != 0 && car.ai != 1) || car.turbo_mode < 0 || car.turbo_mode > 2 ||
                !isfinite(car.turbo_charge) || car.turbo_charge < 0 || car.turbo_charge > 101 ||
                !isfinite(car.body.v_fwd) || !isfinite(car.body.drive) || !isfinite(car.body.vx) ||
                !isfinite(car.body.vz) || !isfinite(car.body.heading) || fabs(car.body.heading) > 100 ||
                car.turbo_frames < 0) {
                fprintf(stderr, "Invalid turbo fixture\n");
                return 1;
            }
            for (int i = 0; i < ticks; i++) {
                ign_car_turbo(&car);
                printf("%.17g %d %d %.17g %.17g %.17g\n", car.turbo_charge, car.turbo_mode,
                       car.turbo_frames, car.body.drive, car.body.vx, car.body.vz);
            }
        } else {
            fprintf(stderr, "Unknown car kernel fixture: %c\n", mode);
            return 1;
        }
    }
    if (ferror(stdin) || fflush(stdout) == EOF) {
        fprintf(stderr, "Car kernel fixture I/O failure\n");
        return 1;
    }
    return 0;
}
