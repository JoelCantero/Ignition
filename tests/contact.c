/* Collision objects and car contact (car_ground_contact 0x350f0): unit checks, and with --runner a line protocol for
 * tests/contact_original.py, which replays the same cases on the unchanged LE bytes. */
#include "game/assets.h"
#include "game/car.h"
#include "game/fx.h"
#include "game/pos.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)

static int near(double a, double b, double eps) {
    return fabs(a - b) <= eps;
}

static void square(float out[4][2], float x, float z, float h) {
    out[0][0] = x - h, out[0][1] = z - h;
    out[1][0] = x + h, out[1][1] = z - h;
    out[2][0] = x + h, out[2][1] = z + h;
    out[3][0] = x - h, out[3][1] = z + h;
}

static void test_polygons(void) {
    float a[4][2], b[4][2], cx, cz, t;
    square(a, 0, 0, 10);
    square(b, 100, 0, 10);
    CHECK(!ign_contact_polygons((const float(*)[2])a, 4, (const float(*)[2])b, 4, &cx, &cz, &t));
    /* Overlapping on the right: the right edge of a crosses the top and bottom of b at x = 10. */
    /* a's right edge crosses b's bottom at (10, -7), a's top crosses b's left at (5, 10). */
    square(b, 15, 3, 10);
    CHECK(ign_contact_polygons((const float(*)[2])a, 4, (const float(*)[2])b, 4, &cx, &cz, &t));
    CHECK(near(cx, 7.5, 1e-5) && near(cz, 1.5, 1e-5));
    CHECK(near(t, atan2(-17.0, 5.0), 1e-6));
    /* A square and the same square turned 45 degrees cross at 8 points around the centre. */
    float r[4][2] = {{0, -13}, {13, 0}, {0, 13}, {-13, 0}};
    CHECK(ign_contact_polygons((const float(*)[2])a, 4, (const float(*)[2])r, 4, &cx, &cz, &t));
    CHECK(near(cx, 0, 1e-4) && near(cz, 0, 1e-4));
    /* Nine crossings count as no contact (the original keeps at most 8). */
    float comb[10][2], bar[4][2] = {{-1, 1}, {100, 1}, {100, 2}, {-1, 2}};
    for (int i = 0; i < 5; i++) {
        comb[2 * i][0] = (float)(i * 20), comb[2 * i][1] = 0;
        comb[2 * i + 1][0] = (float)(i * 20 + 10), comb[2 * i + 1][1] = 5;
    }
    CHECK(!ign_contact_polygons((const float(*)[2])comb, 10, (const float(*)[2])bar, 4, &cx, &cz, &t));
}

static void test_impulse(void) {
    /* A heavy car moving +x hits a light object ahead of it head on. */
    IgnContactBody car = {0, 0, 6, 0, 0, 10, 4000, 0}, obj = {40, 0, 0, 0, 0, 0.3f, 100, 0};
    CHECK(ign_contact_impulse(&car, &obj, 20, 0, (float)(M_PI / 2)));
    CHECK(car.impact == 6 && obj.impact == 6);
    CHECK(obj.vx > 6 && car.vx < 6 && car.vx > 5);
    CHECK(near(car.mass * (car.vx - 6) + obj.mass * obj.vx, 0, 1e-4)); /* momentum along the normal */
    CHECK(near(car.w, 0, 1e-6) && near(obj.vz, 0, 1e-6));
    /* Separating: nothing happens. */
    IgnContactBody c2 = {0, 0, -6, 0, 0, 10, 4000, 0}, o2 = {40, 0, 0, 0, 0, 0.3f, 100, 0};
    CHECK(!ign_contact_impulse(&c2, &o2, 20, 0, (float)(M_PI / 2)));
    CHECK(c2.vx == -6 && o2.vx == 0 && o2.impact == 0);
}

static void test_reflect(void) {
    double n[3] = {0, 3, 0}, v[3] = {1, -5, 2};
    ign_coll_reflect(n, v);
    CHECK(near(v[0], 1, 1e-12) && near(v[1], 5, 1e-12) && near(v[2], 2, 1e-12));
    double away[3] = {1, 5, 2};
    ign_coll_reflect(n, away);
    CHECK(away[0] == 1 && away[1] == 5 && away[2] == 2);
    double slope[3] = {-1, 1, 0}, w[3] = {0, -4, 0};
    ign_coll_reflect(slope, w);
    CHECK(near(w[0], -4, 1e-12) && near(w[1], 0, 1e-12));
}

/* A flat 1000 x 1000 face at height 100 (y down = -100) as the ground query returns it. */
static IgnGroundHit flat_hit(int up) {
    IgnGroundHit h = {0};
    int32_t p[3][3] = {{26000, -100, 26000}, {26000, -100, 27000}, {27000, -100, 26000}};
    if (!up) memcpy(p[1], (int32_t[3]){27000, -100, 26000}, sizeof p[1]), memcpy(p[2], (int32_t[3]){26000, -100, 27000}, sizeof p[2]);
    memcpy(h.p, p, sizeof p);
    int32_t ax = p[0][0] - p[1][0], ay = p[0][1] - p[1][1], az = p[0][2] - p[1][2];
    int32_t bx = p[2][0] - p[1][0], by = p[2][1] - p[1][1], bz = p[2][2] - p[1][2];
    h.normal[0] = ay * bz - by * az, h.normal[1] = az * bx - bz * ax, h.normal[2] = ax * by - bx * ay;
    return h;
}

static void test_flight(void) {
    IgnGroundHit h = flat_hit(1);
    CHECK(near(ign_coll_ground_up(&h, 26300, 26400), 100, 1e-9));
    IgnCollObj o = {.flying = 1, .pos = {26500, 140, 26500}, .vel = {3, 2, 0}, .ang = {3599, 0, 10}, .spin = {5, -20, 0}};
    ign_coll_fly_step(&o);
    CHECK(near(o.vel[0], 2.97, 1e-12) && near(o.vel[1], 1.6, 1e-12));
    CHECK(near(o.ang[0], 3604 - 3600, 1e-9) && near(o.ang[1], 3580, 1e-9));
    CHECK(near(o.spin[1], -19.6, 1e-12) && near(o.pos[1], 141.6, 1e-9));
    ign_coll_bounce(&o, &h); /* 41.6 above the ground: still in the air */
    CHECK(o.flying && near(o.pos[1], 141.6, 1e-9));
    int frames = 0, bounces = 0;
    double vy = o.vel[1];
    while (o.flying && frames < 1000) {
        ign_coll_fly_step(&o);
        ign_coll_bounce(&o, &h);
        if (o.vel[1] > 0 && vy <= 0) bounces++;
        vy = o.vel[1];
        frames++;
        CHECK(o.pos[1] >= 100 - 15);
    }
    CHECK(!o.flying && bounces >= 1 && frames < 1000);
    CHECK(o.vel[0] == 0 && o.vel[1] == 0 && o.spin[0] == 0);
    CHECK(o.pos[1] < 116 && o.pos[1] > 85);
    /* The clamp keeps objects inside the engine world. */
    IgnCollObj far = {.flying = 1, .pos = {10, 0, 51199}, .vel = {-20, -20000, 5}};
    ign_coll_fly_step(&far);
    CHECK(far.pos[0] == 0 && far.pos[1] == -10000 && far.pos[2] == 51200);
    /* A face wound the other way (normal down) does not reflect: the object is held 15 above it until it stops. */
    IgnGroundHit down = flat_hit(0);
    IgnCollObj d = {.flying = 1, .pos = {26500, 110, 26500}, .vel = {1, -6, 0}};
    ign_coll_fly_step(&d);
    ign_coll_bounce(&d, &down);
    CHECK(d.flying && d.vel[1] < 0 && d.pos[1] == 115);
}

/* A track model with one object of each kind, a car driving into the cone of kind 0. */
static void test_frame(void) {
    static const int32_t verts[3] = {0, 0, 0};
    IgnObject objs[5] = {
        {.type = 7, .verts = verts},     {.type = 0x96, .pos = {1000, -20, 2000}, .verts = verts},
        {.type = 0x98, .verts = verts},  {.type = 0x99, .verts = verts},
        {.type = 0x9a, .pos = {-500, 0, 300}, .verts = verts},
    };
    IgnModel track = {.nobj = 5, .objs = objs};
    IgnCollObjs c;
    CHECK(ign_coll_init(&c, &track) == 4);
    CHECK(c.o[0].obj == 1 && c.o[0].kind == 0 && c.o[0].pos[0] == 26600 && c.o[0].pos[1] == 20);
    CHECK(c.o[0].mass == 0.3f && c.o[0].period == 30 && c.o[0].frame == 0);
    CHECK(c.o[1].kind == 2 && c.o[1].mass == 25.0f && c.o[1].inertia == 8000.0f);
    CHECK(c.o[2].kind == 3 && c.o[2].mass == 2.0f && c.o[2].inertia == 1500.0f);
    CHECK(c.o[3].kind == 4 && c.o[3].frame == 4);
    IgnObject bad = {.type = 0x97, .verts = verts};
    IgnModel bad_track = {.nobj = 1, .objs = &bad};
    IgnCollObjs bc;
    CHECK(ign_coll_init(&bc, &bad_track) < 0);

    IgnCar car = {0};
    car.contact_mass = 10, car.contact_inertia = 4000;
    for (int i = 0; i < 4; i++) {
        car.corner_x[i] = i < 2 ? -40.0f : 40.0f;
        car.corner_z[i] = i == 0 || i == 3 ? -20.0f : 20.0f;
    }
    car.body.px = 940 / IGN_WORLD_PER_METRE, car.body.pz = 2000 / IGN_WORLD_PER_METRE;
    car.height = 25;
    car.contact_v[0] = 12;
    IgnCar *cars[1] = {&car};
    IgnLevel lv = {0};
    IgnFx fx;
    ign_fx_init(&fx);
    ign_coll_frame(&c, &lv, cars, 1, &fx, 0, 0);
    CHECK(c.o[0].flying && c.o[0].vel[0] > 17 && c.o[0].vel[1] >= 2 && c.o[0].vel[1] <= 4);
    CHECK(c.o[0].impact == 12 && car.contact_impact == 12 && c.o[0].spin[0] == 60);
    CHECK(fx.nsounds == 1 && fx.sounds[0].sub == 2 && fx.sounds[0].idx == 1 && fx.sounds[0].vol == 6000);
    CHECK(!c.o[1].flying && !c.o[2].flying);
    /* Without a surface the object keeps falling; its engine pose follows. */
    double y = c.o[0].pos[1];
    ign_coll_frame(&c, &lv, cars, 1, &fx, 0, 0);
    CHECK(c.o[0].pos[1] > y && objs[1].pos[0] == (int32_t)trunc(c.o[0].pos[0]) - IGN_SRF_ORIGIN);
    CHECK(objs[1].pos[1] == -(int32_t)trunc(c.o[0].pos[1]));
    /* The light flashed at phases 4 and 5 of 30 (LIGHT.TEX tables 0xc4030, then 0xc4050 for the last of the first
     * fifth), 2 frames each, size 400; phase 6 shows nothing. */
    int sprites = 0, tables = 0;
    for (int i = 0; i < IGN_FX_SLOTS; i++)
        if (fx.r[i][0] == IGN_FX_SPRITE) {
            sprites++;
            tables |= fx.r[i][14] == 0xc4030 ? 1 : fx.r[i][14] == 0xc4050 ? 2 : 4;
            CHECK(fx.r[i][22] == 2 && fx.r[i][12] == 0x64000 && fx.r[i][9] == 3 && fx.r[i][1] == -500 * 1024 &&
                  fx.r[i][2] == 20 * 1024 && fx.r[i][3] == 300 * 1024);
        }
    CHECK(sprites == 2 && tables == 3);
    ign_coll_frame(&c, &lv, cars, 1, &fx, 0, 0);
    CHECK(c.o[3].frame == 7);
    sprites = 0;
    for (int i = 0; i < IGN_FX_SLOTS; i++) sprites += fx.r[i][0] == IGN_FX_SPRITE;
    CHECK(sprites == 2);
    /* A wrecked car or one 50 units above does not touch. */
    ign_coll_init(&c, &track);
    car.wrecked = 1;
    ign_coll_frame(&c, &lv, cars, 1, &fx, 0, 0);
    CHECK(!c.o[0].flying);
    car.wrecked = 0, car.height = 70;
    ign_coll_frame(&c, &lv, cars, 1, &fx, 0, 0);
    CHECK(!c.o[0].flying);
}

/* One special object: a 200 x 200 square (two faces) centred on its position, mesh y = 0. */
static const int32_t SQUARE_V[12] = {-100, 0, -100, 100, 0, -100, 100, 0, 100, -100, 0, 100};
static const IgnFace SQUARE_F[2] = {{.v = {0, 1, 2}}, {.v = {0, 2, 3}}};

static IgnObject special_obj(uint32_t type, int32_t x, int32_t up, int32_t z) {
    return (IgnObject){.type = type, .pos = {x, -up, z}, .nv = 4, .nf = 2, .verts = SQUARE_V, .faces = SQUARE_F};
}

static void test_special(void) {
    const int32_t O = IGN_SRF_ORIGIN;
    IgnObject objs[3] = {special_obj(0x12e, 1000, 50, 1000), special_obj(0x1ffff, 0, 0, 0),
                         special_obj(0x130, 1150, 500, 1000)};
    IgnLevel lv = {.track = {.nobj = 3, .objs = objs}};
    /* Over the first square, in height: found; outside the square or 190 away in height: not. */
    CHECK(ign_special_query(&lv, O + 1000, 60, O + 990, 0, 0) == 0x12e);
    CHECK(ign_special_query(&lv, O + 1000, 60, O + 1101, 0, 0) == -1);
    CHECK(ign_special_query(&lv, O + 1000, 240, O + 1000, 0, 0) == -1);
    CHECK(ign_special_query(&lv, O + 1000, -129, O + 1000, 0, 0) == 0x12e);
    /* Over both squares: the later object wins, even 450 above it once the first one passed the height gate. */
    CHECK(ign_special_query(&lv, O + 1080, 60, O + 1000, 0, 0) == 0x130);
    /* Without the first object's gate, the second is too high. */
    objs[0].type = 0x11ff; /* class 0x1ff: not special */
    CHECK(ign_special_query(&lv, O + 1080, 60, O + 1000, 0, 0) == -1);
    objs[0].type = 0x12e;
    /* A translated (POS) object is found where it is now. */
    objs[0].pos[0] = 3000;
    CHECK(ign_special_query(&lv, O + 1000, 60, O + 990, 0, 0) == -1);
    CHECK(ign_special_query(&lv, O + 3000, 60, O + 990, 0, 0) == 0x12e);
    objs[0].pos[0] = 1000;

    IgnFx fx;
    ign_fx_init(&fx);
    IgnCar car = {0};
    car.body.px = 1000 / IGN_WORLD_PER_METRE, car.body.pz = 990 / IGN_WORLD_PER_METRE;
    car.height = 60, car.ground = 50;
    int wreck;
    CHECK(ign_special_probe(&lv, &fx, &car, 0, 0, &wreck) == IGN_SPECIAL_PUSH && !wreck);
    ign_special_apply(&car, IGN_SPECIAL_PUSH, 0, &fx, 0, 0);
    CHECK(car.airborne && car.fall_speed <= -4.5 && car.fall_speed > -7.5 && car.body.yaw_rate <= -2.5);
    CHECK(fx.nsounds == 1 && fx.sounds[0].group == 1 && fx.sounds[0].idx == 8 && fx.sounds[0].vol == 0x10000);
    /* Airborne cars are not pushed again, and the sample waits 15 car updates. */
    double v = car.fall_speed;
    ign_special_apply(&car, IGN_SPECIAL_PUSH, 0, &fx, 0, 0);
    CHECK(car.fall_speed == v);
    car.airborne = 0;
    for (int i = 0; i < 13; i++) ign_special_apply(&car, IGN_SPECIAL_NONE, 0, &fx, 0, 0);
    ign_special_apply(&car, IGN_SPECIAL_PUSH, 0, &fx, 0, 0);
    CHECK(fx.nsounds == 2 && car.fall_speed == -10.0);

    /* 0x130 under the car: slide wreck; 0x12c: explosion. */
    car = (IgnCar){0};
    car.body.px = 1200 / IGN_WORLD_PER_METRE, car.body.pz = 1000 / IGN_WORLD_PER_METRE;
    car.height = 450;
    CHECK(ign_special_probe(&lv, &fx, &car, 0, 0, &wreck) == IGN_SPECIAL_NONE && wreck == 3);
    objs[2].type = 0x12c;
    CHECK(ign_special_probe(&lv, &fx, &car, 0, 0, &wreck) == IGN_SPECIAL_NONE && wreck == 1);
    ign_special_apply(&car, IGN_SPECIAL_NONE, wreck, &fx, 0, 0);
    CHECK(car.wrecked && car.wreck_kind == 1);
    /* ICELAND: a 0x12c object explodes a car within 30 of its origin only, without the face query. */
    car = (IgnCar){0};
    car.body.px = 1200 / IGN_WORLD_PER_METRE, car.body.pz = 1000 / IGN_WORLD_PER_METRE;
    car.height = 450;
    CHECK(ign_special_probe(&lv, &fx, &car, 1, 0, &wreck) == IGN_SPECIAL_NONE && wreck == 0);
    car.body.px = 1170 / IGN_WORLD_PER_METRE, car.height = 480;
    CHECK(ign_special_probe(&lv, &fx, &car, 1, 0, &wreck) == IGN_SPECIAL_NONE && wreck == 1);
    /* More than 200 from every special object: nothing is queried. */
    car.body.px = 1400 / IGN_WORLD_PER_METRE, car.height = 60;
    objs[0].pos[0] = 1300;
    CHECK(ign_special_probe(&lv, &fx, &car, 0, 0, &wreck) == IGN_SPECIAL_NONE && wreck == 0);

    /* Geyser 0x131: a launch only while an emitter within 80 erupts (phase > 0). */
    IgnObject g[1] = {special_obj(0x131, 0, 0, 0)};
    IgnLevel gl = {.track = {.nobj = 1, .objs = g}};
    ign_fx_init(&fx);
    ign_fx_emitters_init(&fx, &gl);
    car = (IgnCar){0};
    car.body.px = 50 / IGN_WORLD_PER_METRE, car.height = 10;
    CHECK(ign_special_probe(&gl, &fx, &car, 0, 0, &wreck) == IGN_SPECIAL_NONE);
    fx.emit[0].phase = 1;
    CHECK(ign_special_probe(&gl, &fx, &car, 0, 0, &wreck) == IGN_SPECIAL_LAUNCH);
    ign_special_apply(&car, IGN_SPECIAL_LAUNCH, 0, &fx, 0, 0);
    CHECK(car.launch == 1 && car.fall_speed == -8.0);
    int pieces = 0;
    for (int i = 0; i < IGN_FX_SLOTS; i++) pieces += fx.r[i][0] == IGN_FX_PIECE && fx.r[i][19] == 30 && fx.r[i][8] == -460;
    CHECK(pieces == 1);
    ign_special_apply(&car, IGN_SPECIAL_LAUNCH, 0, &fx, 0, 0);
    CHECK(car.fall_speed == -8.0); /* once per somersault */
}

static void test_flatten(void) {
    IgnCar a = {0}, b = {0};
    a.body_stretch = b.body_stretch = 1.0f;
    a.x = 100, a.z = 100, b.x = 120, b.z = 90, b.y = -40;
    a.fall_speed = -1, b.fall_speed = 3; /* b falls onto a */
    IgnCar *cars[2] = {&a, &b};
    ign_cars_collide(cars, 2, NULL, 0, NULL);
    CHECK(a.flat == 1 && !b.flat && a.body_stretch == 0.2f && a.flat_sound == 6);
    /* Already flattened: nobody else is. */
    ign_cars_collide(cars, 2, NULL, 0, NULL);
    CHECK(!b.flat);
    IgnFx fx;
    ign_fx_init(&fx);
    ign_car_flat_effects(&a, &fx, 0, 0);
    CHECK(fx.nsounds == 1 && fx.sounds[0].sub == 2 && fx.sounds[0].idx == 6 && a.flat_sound == 0);
    /* 200 ticks later it waits while b is within 30, then gets up (popping 6 up) with COLL 7. */
    for (int i = 0; i < 198; i++) ign_car_flat_tick(&a, cars, 2, 0);
    CHECK(a.flat == 199);
    ign_car_flat_tick(&a, cars, 2, 0);
    CHECK(a.flat == 200);
    b.x = 200;
    ign_car_flat_tick(&a, cars, 2, 0);
    CHECK(a.flat == 0 && a.body_stretch == 1.0f && a.fall_speed == -7 && a.flat_sound == 7);
    /* 0x12f: flattened by the object. */
    IgnCar c = {0};
    c.body_stretch = 3.0f;
    ign_special_apply(&c, IGN_SPECIAL_MESH, 0, &fx, 0, 0);
    CHECK(c.flat == 1 && fabsf(c.body_stretch - 0.6f) < 1e-6f);
}

/* sub_00058c1c: cells around the point in the original order, each from its last insertion, and the layer filter. */
static void test_engine_view(void) {
    static const int32_t V[3] = {0, 0, 0};
    const int32_t O = IGN_SRF_ORIGIN;
    IgnObject objs[5] = {
        {.type = 0x12c, .pos = {0, 0, 0}, .verts = V},        {.type = 7, .pos = {10, 0, 10}, .verts = V},
        {.type = 0x112c, .pos = {20, 0, 20}, .verts = V},     {.type = 0x212c, .pos = {300, 0, 0}, .verts = V},
        {.type = 0x12c, .pos = {9000, 0, 0}, .verts = V},
    };
    IgnModel m = {.nobj = 5, .objs = objs};
    CHECK(ign_engine_place(&m) == 0);
    int out[8];
    /* Same cell: the class < 100 object was placed first, so it is last in the chain. */
    int n = ign_engine_view(&m, O + 50, O + 50, 0, out, 8);
    CHECK(n == 4 && out[0] == 2 && out[1] == 0 && out[2] == 1 && out[3] == 3);
    /* Moving object 0 away and back puts it at the head. */
    objs[0].pos[0] = 600;
    ign_engine_moved(&m, 0);
    objs[0].pos[0] = 0;
    ign_engine_moved(&m, 0);
    n = ign_engine_view(&m, O + 50, O + 50, 0, out, 8);
    CHECK(n == 4 && out[0] == 0 && out[1] == 2);
    /* Layer 1: layer-2 objects drop out, layer 0 and 1 stay. */
    n = ign_engine_view(&m, O + 50, O + 50, 1, out, 8);
    CHECK(n == 3 && out[2] == 1);
    free(m.cell), free(m.stamp);
}

static int read_obj(IgnCollObj *o) {
    return scanf("%d %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf", &o->flying, &o->pos[0], &o->pos[1], &o->pos[2],
                 &o->vel[0], &o->vel[1], &o->vel[2], &o->ang[0], &o->ang[1], &o->ang[2], &o->spin[0], &o->spin[1],
                 &o->spin[2]) == 13;
}

static void print_obj(const IgnCollObj *o) {
    printf("%d", o->flying);
    for (int k = 0; k < 3; k++) printf(" %.17g", o->pos[k]);
    for (int k = 0; k < 3; k++) printf(" %.17g", o->vel[k]);
    for (int k = 0; k < 3; k++) printf(" %.17g", o->ang[k]);
    for (int k = 0; k < 3; k++) printf(" %.17g", o->spin[k]);
    printf("\n");
}

/* Line protocol: P na nb (x z)*  |  I bodyA bodyB cx cz theta  |  R n[3] v[3]  |  F obj  |  B obj hit  |
 * Q x y z jump n (type x y z nv nf verts faces)*  |  G probe state x y z jump n (same, faces with flags). */
static int runner(void) {
    char mode;
    while (scanf(" %c", &mode) == 1) {
        if (mode == 'P') {
            int na, nb;
            float a[16][2], b[16][2], cx = 0, cz = 0, t = 0;
            if (scanf("%d %d", &na, &nb) != 2 || na < 1 || na > 16 || nb < 1 || nb > 16) return 1;
            for (int i = 0; i < na; i++)
                if (scanf("%f %f", &a[i][0], &a[i][1]) != 2) return 1;
            for (int i = 0; i < nb; i++)
                if (scanf("%f %f", &b[i][0], &b[i][1]) != 2) return 1;
            int r = ign_contact_polygons((const float(*)[2])a, na, (const float(*)[2])b, nb, &cx, &cz, &t);
            printf("%d %.17g %.17g %.17g\n", r, cx, cz, t);
        } else if (mode == 'I') {
            IgnContactBody b[2];
            float cx, cz, t;
            for (int i = 0; i < 2; i++)
                if (scanf("%lf %lf %lf %lf %lf %f %f %f", &b[i].x, &b[i].z, &b[i].vx, &b[i].vz, &b[i].w, &b[i].mass,
                          &b[i].inertia, &b[i].impact) != 8)
                    return 1;
            if (scanf("%f %f %f", &cx, &cz, &t) != 3) return 1;
            int r = ign_contact_impulse(&b[0], &b[1], cx, cz, t);
            printf("%d", r);
            for (int i = 0; i < 2; i++)
                printf(" %.17g %.17g %.17g %.17g", b[i].vx, b[i].vz, b[i].w, b[i].impact);
            printf("\n");
        } else if (mode == 'R') {
            double n[3], v[3];
            if (scanf("%lf %lf %lf %lf %lf %lf", &n[0], &n[1], &n[2], &v[0], &v[1], &v[2]) != 6) return 1;
            ign_coll_reflect(n, v);
            printf("%.17g %.17g %.17g\n", v[0], v[1], v[2]);
        } else if (mode == 'F' || mode == 'B') {
            IgnCollObj o = {0};
            if (!read_obj(&o)) return 1;
            if (mode == 'F') {
                ign_coll_fly_step(&o);
            } else {
                IgnGroundHit h = {0};
                for (int k = 0; k < 3; k++)
                    if (scanf("%d", &h.normal[k]) != 1) return 1;
                for (int k = 0; k < 9; k++)
                    if (scanf("%d", &h.p[k / 3][k % 3]) != 1) return 1;
                ign_coll_bounce(&o, &h);
            }
            print_obj(&o);
        } else if (mode == 'Q' || mode == 'G') {
            int32_t x, y, z, probe = 0, state = -1;
            int jump, n;
            if (mode == 'G' && scanf("%d %d", &probe, &state) != 2) return 1;
            if (scanf("%d %d %d %d %d", &x, &y, &z, &jump, &n) != 5 || n < 0 || n > 16) return 1;
            static IgnObject objs[16];
            static int32_t verts[16][16 * 3];
            static IgnFace faces[16][16];
            for (int i = 0; i < n; i++) {
                IgnObject *o = &objs[i];
                int32_t ox, oy, oz;
                if (scanf("%u %d %d %d %d %d", &o->type, &ox, &oy, &oz, &o->nv, &o->nf) != 6 || o->nv < 1 ||
                    o->nv > 16 || o->nf < 0 || o->nf > 16)
                    return 1;
                o->pos[0] = ox - IGN_SRF_ORIGIN, o->pos[1] = -oy, o->pos[2] = oz - IGN_SRF_ORIGIN;
                for (int k = 0; k < o->nv * 3; k++)
                    if (scanf("%d", &verts[i][k]) != 1) return 1;
                for (int f = 0; f < o->nf; f++) {
                    faces[i][f] = (IgnFace){0};
                    if (mode == 'G' && scanf("%u", &faces[i][f].flags) != 1) return 1;
                    if (scanf("%d %d %d", &faces[i][f].v[0], &faces[i][f].v[1], &faces[i][f].v[2]) != 3) return 1;
                }
                o->verts = verts[i], o->faces = faces[i];
            }
            IgnLevel lv = {.track = {.nobj = n, .objs = objs}};
            if (mode == 'Q') {
                printf("%d\n", ign_special_query(&lv, x, y, z, jump, 0));
            } else {
                int list[16];
                for (int i = 0; i < n; i++) list[i] = i;
                IgnGroundHit h;
                ign_ground_list_state(state);
                int r = ign_ground_list_query_in(&lv, list, n, x, y, z, jump, probe, &h);
                printf("%d", r == 0 ? h.surface_type : -1);
                if (r == 0 && (objs[h.object].type & 0xfff) < 100) {
                    for (int k = 0; k < 3; k++) printf(" %d", h.normal[k]);
                    for (int k = 0; k < 9; k++) printf(" %d", h.p[k / 3][k % 3]);
                }
                printf("\n");
            }
        } else if (mode == 'V') {
            int32_t x, z;
            int layer, n;
            if (scanf("%d %d %d %d", &x, &z, &layer, &n) != 4 || n < 0 || n > 512) return 1;
            static IgnObject objs[512];
            static const int32_t vz[3] = {0, 0, 0};
            for (int i = 0; i < n; i++) {
                objs[i] = (IgnObject){.verts = vz};
                int32_t ox, oz;
                if (scanf("%u %d %d", &objs[i].type, &ox, &oz) != 3) return 1;
                objs[i].pos[0] = ox - IGN_SRF_ORIGIN, objs[i].pos[2] = oz - IGN_SRF_ORIGIN;
            }
            IgnModel m = {.nobj = n, .objs = objs};
            if (ign_engine_place(&m)) return 1;
            int out[512];
            int k = ign_engine_view(&m, x, z, layer, out, 512);
            printf("%d", k);
            for (int i = 0; i < k; i++) printf(" %d", out[i]);
            printf("\n");
            free(m.cell), free(m.stamp);
        } else {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--runner")) return runner();
    test_polygons();
    test_impulse();
    test_reflect();
    test_flight();
    test_frame();
    test_special();
    test_flatten();
    test_engine_view();
    if (failures) fprintf(stderr, "%d failures\n", failures);
    else printf("contact: all checks passed\n");
    return failures != 0;
}
