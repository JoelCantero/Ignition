#include "game/physics.h"

#include <math.h>
#include <stdio.h>

#define BODY_FIELDS(X) \
    X(mass) X(inertia) X(restitution) X(a) X(b) X(heading) X(yaw_rate) \
    X(px) X(pz) X(vx) X(vz) X(v_fwd) X(v_lat) X(steer) X(pitch) X(roll) \
    X(grip_f) X(grip_r) X(drive) X(fmax_f) X(fmax_r) X(radius) X(impact) \
    X(contact_x) X(contact_z) \
    X(corner_x[0]) X(corner_x[1]) X(corner_x[2]) X(corner_x[3]) \
    X(corner_z[0]) X(corner_z[1]) X(corner_z[2]) X(corner_z[3])

int main(void) {
    int ticks, airborne, saturated, sliding, count;
    double slope_k;
    while ((count = scanf("%d %d %lf %d %d", &ticks, &airborne, &slope_k,
                         &saturated, &sliding)) != EOF) {
        if (count != 5 || ticks < 1 || ticks > 10000 || !isfinite(slope_k)) {
            fprintf(stderr, "Invalid body-step test header\n");
            return 1;
        }
        IgnBody body = {0};
#define READ_FIELD(name) \
        if (scanf("%lf", &body.name) != 1 || !isfinite(body.name)) { \
            fprintf(stderr, "Invalid body-step field: %s\n", #name); \
            return 1; \
        }
        BODY_FIELDS(READ_FIELD)
#undef READ_FIELD
        if (body.mass <= 0 || body.inertia <= 0 || body.a + body.b <= 0) {
            fprintf(stderr, "Invalid body-step mass, inertia or wheelbase\n");
            return 1;
        }
        body.saturated = saturated;
        body.sliding = sliding;
        for (int tick = 0; tick < ticks; ++tick) {
            ign_body_step(&body, airborne, slope_k);
            printf("%d %d", body.saturated, body.sliding);
#define PRINT_FIELD(name) printf(" %.17g", body.name);
            BODY_FIELDS(PRINT_FIELD)
#undef PRINT_FIELD
            putchar('\n');
        }
    }
    if (ferror(stdin) || fflush(stdout) == EOF) {
        fprintf(stderr, "Body-step test I/O failure\n");
        return 1;
    }
    return 0;
}
