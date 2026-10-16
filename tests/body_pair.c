#include "game/car.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FIELDS(X) \
    X(mass) X(inertia) X(restitution) X(heading) X(yaw_rate) \
    X(px) X(pz) X(vx) X(vz) X(radius) X(impact) X(contact_x) X(contact_z) \
    X(corner_x[0]) X(corner_x[1]) X(corner_x[2]) X(corner_x[3]) \
    X(corner_z[0]) X(corner_z[1]) X(corner_z[2]) X(corner_z[3])

static int read_body(IgnBody *body) {
    memset(body, 0, sizeof(*body));
#define READ(name) if (scanf("%lf", &body->name) != 1 || !isfinite(body->name)) return -1;
    FIELDS(READ)
#undef READ
    return body->mass > 0 && body->inertia > 0 && body->radius >= 0 ? 0 : -1;
}

static void print_body(const IgnBody *body) {
#define PRINT(name) printf(" %.17g", body->name);
    FIELDS(PRINT)
#undef PRINT
}

static int check(int ok, const char *name) {
    if (!ok) fprintf(stderr, "body_pair: %s\n", name);
    return !ok;
}

static int self_test(void) {
    IgnBody a = {.mass = 500, .inertia = 1000, .restitution = 0.6, .radius = 4,
                 .corner_x = {-1, -1, 1, 1}, .corner_z = {-2, 2, 2, -2}};
    IgnBody b = a;
    int failed = 0;
    IgnBodyCorners corners;
    a.px = 10;
    a.pz = -20;
    ign_body_corners(&a, 2, &corners);
    failed |= check(corners.x[0] == 9 && corners.z[0] == -22 &&
                    corners.scaled_x[0] == 8 && corners.scaled_z[0] == -24, "scaled corner offsets");
    a.px = a.pz = 0;
    b.px = b.pz = 1;
    failed |= check(ign_body_collide_pair(&a, &b) == 1, "stationary overlap is a contact without impulse");
    failed |= check(a.impact == 0 && b.impact == 0 && a.contact_x == 0.5 && a.contact_z == 0.5 &&
                    b.contact_x == 0.5 && b.contact_z == 0.5, "contact midpoint without impulse");
    b.px = 100;
    failed |= check(ign_body_collide_pair(&a, &b) == 0 && a.contact_x == 0 && a.contact_z == 0 &&
                    b.contact_x == 0 && b.contact_z == 0, "rejected pair clears stale contact");
    a.vz = -3;
    a.impact = b.impact = 17;
    failed |= check(ign_body_pair_impulse(&a, &b, 0, 1, 0) == 0 &&
                    a.impact == 17 && b.impact == 17, "separating impulse preserves body state");
    a.vz = 3;
    b = a;
    b.pz = 2;
    b.vz = 0;
    failed |= check(ign_body_pair_impulse(&a, &b, 0, 1, 0) == 1 &&
                    a.impact == 3 && b.impact == 3 &&
                    fabs(a.vz - 0.96) < 1e-14 && fabs(b.vz - 2.04) < 1e-14 &&
                    a.pz == -0.25 && b.pz == 2.25, "approaching bodies exchange impulse and separate");
    a.px = a.pz = a.vx = a.vz = a.yaw_rate = 0;
    b = a;
    b.px = 1;
    b.pz = 2.5;
    IgnCar first = {.body = a}, second = {.body = b, .x = 21.76f, .z = 54.4f};
    IgnCar *cars[2] = {&first, &second};
    IgnCarHit hit;
    int nhits = -1;
    failed |= check(ign_cars_collide(cars, 2, &hit, 1, &nhits) == 1 && nhits == 1 &&
                    hit.a == 0 && hit.b == 1 && hit.impact == 0 &&
                    fabs(hit.x - 10.88) < 1e-12 && fabs(hit.z - 27.2) < 1e-12,
                    "car pair pass reports zero-impulse geometric contact");
    failed |= check(ign_cars_collide(cars, 2, NULL, 0, &nhits) == 1 && nhits == 0,
                    "contact count is independent of event storage capacity");
    return failed;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0) return self_test();
    if (argc != 1) {
        fprintf(stderr, "body_pair: expected no arguments or --self-test\n");
        return 1;
    }
    char mode;
    while (scanf(" %c", &mode) == 1) {
        IgnBody a, b;
        double cx, cz, theta, scale;
        if (mode == 'C') {
            if (scanf("%lf", &scale) != 1 || !isfinite(scale) || read_body(&a) < 0) goto invalid;
            IgnBodyCorners corners;
            ign_body_corners(&a, scale, &corners);
            for (int i = 0; i < 4; i++) printf(" %.17g %.17g", corners.x[i], corners.z[i]);
            for (int i = 0; i < 4; i++) printf(" %.17g %.17g", corners.scaled_x[i], corners.scaled_z[i]);
        } else if (mode == 'I' || mode == 'B') {
            if (mode == 'I' && (scanf("%lf %lf %lf", &cx, &cz, &theta) != 3 ||
                               !isfinite(cx) || !isfinite(cz) || !isfinite(theta))) goto invalid;
            if (read_body(&a) < 0 || read_body(&b) < 0) goto invalid;
            int hit = mode == 'I' ? ign_body_pair_impulse(&a, &b, cx, cz, theta) :
                                   ign_body_collide_pair(&a, &b);
            printf("%d", hit);
            print_body(&a);
            print_body(&b);
        } else goto invalid;
        putchar('\n');
    }
    if (ferror(stdin) || fflush(stdout) == EOF) {
        fprintf(stderr, "body_pair: I/O failure\n");
        return 1;
    }
    return 0;
invalid:
    fprintf(stderr, "body_pair: invalid input\n");
    return 1;
}
