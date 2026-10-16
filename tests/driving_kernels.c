#include "game/car.h"
#include "game/ghost.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int self_test(void) {
    int failed = 0;
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s\n", #expr); failed++; } } while (0)
    CHECK(ign_physics_sign(NAN) == 1);
    CHECK(ign_control_sign(NAN) == -2);
    CHECK(ign_integer_sign(-2147483647 - 1) == -1);
    CHECK(signbit(ign_wrap_positive(-0.0, 7.0)));
    CHECK(ign_wrap_positive(-0x1p-100, 7.0) == 7.0);
    CHECK(ign_angle_normalize(-0x1p-100) == 0.0);
    IgnCar car = {.ground = 10, .height = 15, .airborne = 1};
    ign_car_grounded_probe(&car);
    CHECK(car.airborne == 1 && car.landed == 0);
    car.height = nextafter(15, 0);
    ign_car_grounded_probe(&car);
    CHECK(car.airborne == 0 && car.landed == 1);
    car.body.px = 3;
    car.body.vx = 72;
    ign_body_move_planar(&car.body, 1);
    CHECK(car.body.px == 3);
    ign_body_move_planar(&car.body, 0);
    CHECK(car.body.px == 4);
    car.body_pitch = 3.13;
    car.body.pitch = -3.13;
    ign_car_body_sway(&car);
    CHECK(car.body_pitch > 3.13 && car.body_pitch < 3.15);
    car.launch = 1;
    car.body.roll = 0.75;
    car.lean_roll = 20;
    ign_car_body_sway(&car);
    CHECK(fabs(car.body_roll - 0.75) < 1e-14);
    static IgnGhost ghost;
    car.wrecked = 1;
    car.wreck_kind = 2;
    car.height = 100;
    ign_ghost_record(&ghost, &car, 100);
    CHECK(ghost.s[0].y == 100);
    car.wreck_kind = 1;
    ign_ghost_record(&ghost, &car, 100);
    CHECK(ghost.s[1].y == 5100);
#undef CHECK
    if (!failed) puts("driving kernels: OK");
    return failed != 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--self-test")) return self_test();
    char mode;
    while (scanf(" %c", &mode) == 1) {
        IgnCar car = {0};
        double value, period;
        int integer;
        if (mode == 'S') {
            if (scanf("%lf %d", &value, &integer) != 2) goto invalid;
            printf("%d %.17g %d\n", ign_physics_sign(value), ign_control_sign(value), ign_integer_sign(integer));
        } else if (mode == 'W') {
            if (scanf("%lf %lf", &value, &period) != 2 || !isfinite(value) ||
                !isfinite(period) || period <= 0 || fabs(value / period) > 10000) goto invalid;
            printf("%.17g\n", ign_wrap_positive(value, period));
        } else if (mode == 'G') {
            if (scanf("%lf %lf %d %d", &car.ground, &car.height, &car.airborne, &car.landed) != 4) goto invalid;
            ign_car_grounded_probe(&car);
            printf("%d %d\n", car.airborne, car.landed);
        } else if (mode == 'M') {
            if (scanf("%lf %lf %lf %lf %d", &car.body.px, &car.body.pz,
                      &car.body.vx, &car.body.vz, &integer) != 5) goto invalid;
            ign_body_move_planar(&car.body, integer);
            printf("%.17g %.17g\n", car.body.px, car.body.pz);
        } else if (mode == 'L') {
            double dx, dz;
            if (scanf("%lf %lf %lf %lf %lf %lf %lf", &dx, &dz, &car.step_x, &car.step_z,
                      &car.drive_heading, &car.lean_roll, &car.lean_pitch) != 7) goto invalid;
            ign_car_body_lean(&car, dx, dz);
            printf("%.17g %.17g\n", car.lean_roll, car.lean_pitch);
        } else if (mode == 'B') {
            if (scanf("%lf %lf %lf %lf %lf %lf %d", &car.body.roll, &car.body.pitch,
                      &car.lean_roll, &car.lean_pitch, &car.body_roll, &car.body_pitch,
                      &car.launch) != 7) goto invalid;
            ign_car_body_sway(&car);
            printf("%.17g %.17g\n", car.body_roll, car.body_pitch);
        } else if (mode == 'R') {
            int sections, count;
            if (scanf("%f %lf %f %lf %lf %lf %d %d %d %d %d",
                      &car.x, &car.height, &car.z, &car.body.roll, &car.body.heading, &car.body.pitch,
                      &car.wreck_kind, &car.section, &car.lap, &car.phase, &sections) != 11 ||
                scanf("%d", &count) != 1 || count < 0 || count >= IGN_GHOST_SAMPLES) goto invalid;
            static IgnGhost ghost;
            memset(&ghost, 0, sizeof ghost);
            ghost.count = count;
            ign_ghost_record(&ghost, &car, sections);
            const IgnGhostSample *s = &ghost.s[count];
            printf("%d %d %d %d %d %d %d %d %d\n", s->x, s->y, s->z, s->roll, s->heading,
                   s->pitch, s->progress, ghost.count, ghost.overflow);
        } else goto invalid;
    }
    if (!ferror(stdin)) return 0;
invalid:
    fprintf(stderr, "Invalid driving kernel fixture\n");
    return 1;
}
