#include "camera.h"

#include <math.h>
#include <stdlib.h>

#define PI 3.14159265358979
#define BASE_HEIGHT 400.0 /* +0x50 */
#define BASE_PITCH 420.0  /* +0x40, tenths of a degree */

static double wrap_pi(double a) {
    if (a > PI) a -= 2 * PI;
    if (a < -PI) a += 2 * PI;
    return a;
}

static double wrap_2pi(double a) {
    if (a >= 2 * PI) a -= 2 * PI;
    if (a <= 0) a += 2 * PI;
    return a;
}

/* Smooths `a` towards `target` by 1/8 of the wrapped difference (0x4821e, 0x4835a). */
static double follow_yaw(double a, double target) {
    return wrap_2pi(a - wrap_pi(a - target) * 0.125);
}

static void set_targets(IgnChaseCam *c, const IgnCar *car) {
    double half = c->dist * 0.5;
    c->back[0] = car->x - cos(c->yaw) * half;
    c->back[1] = car->z - sin(c->yaw) * half;
    c->front[0] = car->x + cos(c->yaw) * half;
    c->front[1] = car->z + sin(c->yaw) * half;
    c->car_height = car->height;
    /* car+0xf8 stays in [0, 2pi) in the original; the port's body heading is unbounded. */
    double h = fmod(car->body.heading, 2 * PI);
    if (h < 0) h += 2 * PI;
    c->target_yaw = h;
    c->target_rear = h + PI;
    c->car_pitch = car->body.pitch;
}

void ign_chase_init(IgnChaseCam *c, const IgnCar *car) {
    *c = (IgnChaseCam){0};
    c->height = car->height + 1400.0;
    c->yaw = car->body.heading;
    /* +0x28 is not set by sub_00027df8; start it facing backwards. */
    c->rear_yaw = wrap_2pi(car->body.heading + PI);
    c->tilt = 450.0;
    c->dist = 400;
    set_targets(c, car);
}

void ign_chase_update(IgnChaseCam *c, const IgnCar *car, const IgnLevel *lv, const IgnTrack *t, int racing) {
    /* 0x47ff6: the targets stop following the car once it has finished (car+0x528). */
    if (!car->out) set_targets(c, car);

    if (car->wrecked) c->wreck += (150.0 - c->wreck) / 30.0;
    else c->wreck *= 0.98;
    /* sub_0003a788 0x3aec1: for the first 20 frames of a wreck the camera height shakes (+0x38). */
    if (car->wrecked && car->wreck_kind != 3 && car->wreck_frame <= 20.0) c->shake = (20.0 - (int)car->wreck_frame) * (rand() / (double)RAND_MAX) * 25.0;

    c->yaw = follow_yaw(c->yaw, c->target_yaw);
    c->rear_yaw = follow_yaw(c->rear_yaw, c->target_rear);

    /* 0x48486: 0 while the camera looks along the section, 100 when it looks across it. */
    int s = car->section == IGN_NO_SECTION ? 0 : car->section;
    double across = 0, ang;
    if (!car->out && ign_track_section_angle(t, lv, s, &ang)) across = (cos((c->yaw - ang - PI / 2) * 2.0 + PI) + 1.0) * 50.0;

    /* 0x48755: PLC typeword byte 2 of the section object: 1-10 raise, 11-20 lower the camera by 50 each. */
    int o0 = ign_track_section_object(t, s);
    double lift_target = 0;
    if (o0 >= 0) {
        int v = (int)((lv->track.objs[o0].type >> 16) & 0xff);
        if (v > 0 && v < 11) lift_target = -v * 50.0;
        else if (v > 10 && v <= 20) lift_target = (v - 10) * 50.0;
    }
    double rate = car->kmh * 10.0 / 30.0; /* car+0x118 */
    if (c->lift < lift_target) c->lift = fmin(c->lift + rate, lift_target);
    else if (c->lift > lift_target) c->lift = fmax(c->lift - rate, lift_target);

    double div = racing ? 10.0 : 25.0; /* 0x47fab: 25 until the race clock [0x1fb4e8] goes negative */
    double ground_pitch = car->wrecked ? 0 : c->car_pitch * 3600.0 * 0.031830988618379;
    c->tilt += ((c->height - c->car_height - BASE_HEIGHT) * 0.5 + across * 3.0 + ground_pitch - c->tilt) / div;
    c->height += (BASE_HEIGHT - (c->height - c->car_height) + across * 2.0 - c->lift) / div;
    if (c->height < c->car_height + 200.0) c->height = c->car_height + 200.0;
    c->dist = (int)(c->dist + (400.0 - across * 2.0 - c->dist) * 0.1);
}

void ign_chase_view(const IgnChaseCam *c, const IgnCar *car, int rear, IgnCamera *out) {
    /* The original renders right after the update; while following, rebuild the stored point from the live car so
     * the port's faster frame rate does not show the car moving between camera updates. */
    double half = c->dist * 0.5, x, z;
    if (car->out) {
        x = rear ? c->front[0] : c->back[0];
        z = rear ? c->front[1] : c->back[1];
    } else {
        double k = rear ? half : -half;
        x = car->x + cos(c->yaw) * k;
        z = car->z + sin(c->yaw) * k;
    }
    out->x = (float)x;
    out->z = (float)z;
    out->y = (float)-(c->height + c->shake);
    out->yaw = (float)(PI / 2 - (rear ? c->rear_yaw : c->yaw));
    out->pitch = (float)((BASE_PITCH + c->wreck + c->tilt) * 0.1 * PI / 180.0);
}

/* 0xc464c: x, height, z (engine coordinates), pitch and engine yaw in radians; 3 cameras per track. */
static const float FIXED_CAMS[7][3][5] = {
    {{27271, 1380, 24168, 1.00f, 1.71f}, {25496, 685, 29226, 0.74f, 4.67f}, {20893, 930, 21161, 0.86f, 0.46f}},
    {{25763, 2415, 25466, 1.09f, 3.95f}, {25467, 580, 28550, 0.64f, 4.52f}, {27824, 930, 23659, 0.81f, 5.87f}},
    {{29574, -396, 24628, 1.10f, 0.81f}, {30184, -397, 21494, 0.95f, 5.84f}, {23627, -745, 29066, 0.82f, 4.90f}},
    {{19066, 895, 25362, 0.95f, 2.42f}, {25317, 1035, 21688, 1.06f, 4.88f}, {27566, -220, 32713, 0.76f, 3.17f}},
    {{25187, 245, 24708, 0.94f, 0.27f}, {29237, 155, 23979, 0.91f, 5.11f}, {23877, 1265, 20036, 1.03f, 2.35f}},
    {{26833, 290, 27697, 0.78f, 0.28f}, {21149, 660, 26885, 0.87f, 0.41f}, {19892, 190, 24149, 0.75f, 5.04f}},
    {{26307, 510, 28597, 0.88f, 4.71f}, {29128, 160, 25507, 0.65f, 0.74f}, {21943, 475, 24885, 0.84f, 6.12f}},
};

int ign_fixed_camera(int track, int index, double clock, IgnCamera *out) {
    if (track < 0 || track >= 7 || index < 0 || index >= 3) return -1;
    const float *e = FIXED_CAMS[track][index];
    out->x = e[0] - IGN_SRF_ORIGIN;
    out->z = e[2] - IGN_SRF_ORIGIN;
    out->y = (float)-(e[1] + sin(clock * 0.05) * 8.0); /* 0x429af */
    /* Both angles are radians scaled to tenths of a degree, so they map directly to the port's radians. */
    out->pitch = e[3];
    out->yaw = e[4];
    return 0;
}
