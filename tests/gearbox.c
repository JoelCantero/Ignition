#include <stdio.h>
#include <string.h>

#include "game/car.h"

static int check(int condition, const char *message) {
    if (condition) return 0;
    fprintf(stderr, "FAIL %s\n", message);
    return 1;
}

int main(void) {
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

    IgnInput input = {.accel = true, .manual_gear = true, .gear_shift = 1};
    ign_car_tick(&car, &level, &input);
    int failed = check(car.gear == 2 && car.forward_gear == 2, "upshift selects second gear");

    input = (IgnInput){.manual_gear = true};
    ign_car_tick(&car, &level, &input);
    failed |= check(car.gear == 2, "selected gear persists without throttle");

    input.brake = true;
    ign_car_tick(&car, &level, &input);
    failed |= check(car.gear == 0 && car.forward_gear == 2, "braking selects reverse and retains forward gear");

    input = (IgnInput){.manual_gear = true, .gear_shift = -1};
    ign_car_tick(&car, &level, &input);
    failed |= check(car.gear == 1 && car.forward_gear == 1, "downshift selects first gear after braking");

    input.gear_shift = -1;
    ign_car_tick(&car, &level, &input);
    failed |= check(car.gear == 1 && car.forward_gear == 1, "downshift is bounded at first gear");

    input.gear_shift = 1;
    ign_car_tick(&car, &level, &input);
    input.gear_shift = 1;
    ign_car_tick(&car, &level, &input);
    failed |= check(car.gear == 2 && car.forward_gear == 2, "upshift is bounded at second gear");

    input = (IgnInput){.accel = true, .gear_shift = -1};
    ign_car_tick(&car, &level, &input);
    failed |= check(car.gear == 1 && car.forward_gear == 1, "automatic mode ignores manual shift requests");
    return failed ? 1 : 0;
}
