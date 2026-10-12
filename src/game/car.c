#include "car.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "track.h"

#define HALF_PI 1.5707963267948966
#define PI 3.14159265358979323846
#define VERT_REST 5.0 /* the original rests the car 5 units above the ground */

int ign_first_car_id(const IgnLevel *lv) {    for (int i = 0; i < lv->cars.nobj; i++)
        if (lv->cars.objs[i].type % 10 == 0) return (int)(lv->cars.objs[i].type / 10);
    return -1;
}

void ign_car_progress(IgnCar *car, const IgnTrack *t) {
    if (car->ground_object < 0 || car->ground_object >= t->nobj || t->count < 4) return;
    int s = t->section[car->ground_object];
    if (s == IGN_NO_SECTION) return;
    car->section = s;
    int a = abs(s), q = t->count / 4;
    if (car->phase == 3 && a == t->count - 1) {
        car->lap++;
        car->phase = 0;
    } else if (car->phase == 0 && a >= q && a < 2 * q) {
        car->phase = 1;
    } else if (car->phase == 1 && a >= 2 * q && a < 3 * q) {
        car->phase = 2;
    } else if (car->phase == 2 && a >= 3 * q) {
        car->phase = 3;
    }
}

/* Ground under the four wheels: slope (when grounded), surface types and mean height (up positive). */
static void sample_ground(IgnCar *car, const IgnLevel *lv, int types[4]) {
    IgnBody *b = &car->body;
    double fx = cos(b->heading), fz = sin(b->heading);
    double sx = cos(b->heading + HALF_PI), sz = sin(b->heading + HALF_PI);
    float cx = (float)(b->px * IGN_WORLD_PER_METRE), cz = (float)(b->pz * IGN_WORLD_PER_METRE);
    float hint = (float)-car->height;
    const float along[4] = {car->front, car->front, car->rear, car->rear};
    const float side[4] = {car->half_width, -car->half_width, car->half_width, -car->half_width};
    float h[4];
    int found[4], nfound = 0;
    for (int w = 0; w < 4; w++) {
        float wx = cx + (float)(fx * along[w] + sx * side[w]);
        float wz = cz + (float)(fz * along[w] + sz * side[w]);
        IgnGroundHit hit;
        found[w] = ign_ground_at(lv, wx, wz, hint, &hit) == 0;
        if (found[w]) {
            h[w] = hit.y;
            types[w] = hit.surface_type;
            nfound++;
            if (w == 0) car->ground_object = hit.object;
        } else {
            types[w] = 0;
        }
    }
    /* car_update 0x303xx: 1-3 wheels far from the previous ground (or on type >= 0x50) follow it instead. */
    double limit = fabs(car->kmh) * 0.5 + 100.0;
    int outlier[4], nout = 0;
    for (int w = 0; w < 4; w++) {
        outlier[w] = !found[w] || types[w] >= 0x50 ||
                     fabs(trunc(-h[w] - car->ground)) > limit;
        nout += outlier[w];
    }
    if (nout > 0 && nout < 4) {
        for (int w = 0; w < 4; w++)
            if (outlier[w]) h[w] = (float)-(car->ground - car->fall_speed);
    } else {
        /* All four moved together: accept them; wheels without ground take the others' mean. */
        float sum = 0;
        for (int w = 0; w < 4; w++) sum += found[w] ? h[w] : 0;
        float fallback = nfound ? sum / nfound : (float)-car->ground;
        for (int w = 0; w < 4; w++)
            if (!found[w]) h[w] = fallback;
    }
    if (!car->airborne) {
        float base = car->front - car->rear;
        float width = 2 * car->half_width;
        /* y points down: a lower front (larger y) gives positive pitch, i.e. downhill acceleration. */
        b->pitch = atan2((h[0] + h[1]) * 0.5 - (h[2] + h[3]) * 0.5, base);
        b->roll = atan2((h[0] + h[2]) * 0.5 - (h[1] + h[3]) * 0.5, width);
    }
    car->ground = -(h[0] + h[1] + h[2] + h[3]) * 0.25;
    car->x = cx;
    car->z = cz;
    car->heading = (float)(HALF_PI - b->heading);
}

/* sub_0002e234: gravity, ground following, jumps and landing. */
static void vertical_step(IgnCar *car) {
    double drop = car->ground_prev - car->ground;
    if (fabs(car->kmh) < 50.0 && fabs(drop) > 4.0) drop = 0;
    if (fabs(car->kmh * 0.05) < drop) drop = 0; /* ground falls away faster than we follow: jump */
    /* Deviation: the same limit upwards; layer switches otherwise become huge launches. */
    if (drop < -fabs(car->kmh * 0.05) - 1.0) drop = 0;
    car->fall_speed += 0.2;
    car->height -= car->fall_speed;
    double rest = car->ground + VERT_REST;
    if (car->height < rest) {
        car->height = rest;
        if (!car->airborne) {
            car->fall_speed = drop;
        } else {
            car->fall_speed = car->fall_speed > 8.0 ? -car->fall_speed * 0.2 : car->fall_speed * 0.5;
            if (car->fall_speed < -15.0) car->fall_speed = -15.0;
            car->airborne = 0;
        }
    }
    if (car->ground + 30.0 < car->height) car->airborne = 1;
}

/* sub_00048a14 places body and wheels at car+8: axle 5 units above the ground, so the tyres sink into it. */
static void place(IgnCar *car) {
    car->y = (float)-car->height;
}

int ign_cars_collide(IgnCar *const *cars, int n) {
    int hits = 0;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            IgnCar *a = cars[i], *b = cars[j];
            if (fabsf(a->x - b->x) >= 200 || fabsf(a->z - b->z) >= 200 || fabsf(a->y - b->y) >= 70)
                continue;
            hits += ign_body_collide_pair(&a->body, &b->body);
        }
    return hits;
}

int ign_car_init(IgnCar *car, const IgnLevel *lv, int id, float x, float z, float heading) {
    memset(car, 0, sizeof *car);
    car->id = id;
    car->body_stretch = 1.0f;
    car->spec = ((id % 12) + 12) % 12;
    int body = -1;
    for (int i = 0; i < lv->cars.nobj && car->nparts < IGN_MAX_CAR_PARTS; i++) {
        const IgnObject *o = &lv->cars.objs[i];
        if ((int)(o->type / 10) != id) continue;
        car->parts[car->nparts++] = i;
        if (o->type % 10 == 0) body = i;
    }
    if (body < 0) return -1;
    memcpy(car->origin, lv->cars.objs[body].pos, sizeof car->origin);

    /* cars_init 0x265f6: body lifted so its lowest vertex is y=0; wheels keep only their x/z offset. */
    const IgnObject *bo0 = &lv->cars.objs[body];
    float body_low = -INFINITY;
    for (int v = 0; v < bo0->nv; v++)
        if (bo0->verts[v * 3 + 1] > body_low) body_low = (float)bo0->verts[v * 3 + 1];
    for (int p = 0; p < car->nparts; p++) {
        const IgnObject *o = &lv->cars.objs[car->parts[p]];
        float dx = (float)(o->pos[0] - car->origin[0]), dz = (float)(o->pos[2] - car->origin[2]);
        if (o->type % 10) {
            car->part_off[p][0] = dx;
            car->part_off[p][1] = 0;
            car->part_off[p][2] = dz;
            if (dx > car->front) car->front = dx;
            if (dx < car->rear) car->rear = dx;
            if (fabsf(dz) > car->half_width) car->half_width = fabsf(dz);
        } else {
            car->part_off[p][0] = car->part_off[p][2] = 0;
            car->part_off[p][1] = -body_low;
        }
    }
    if (car->front <= car->rear) car->front = 1, car->rear = -1;
    if (car->half_width <= 0) car->half_width = 1;

    for (int g = 0; g < 3; g++) ign_torque_curve(ign_car_gears[car->spec][g], car->torque[g]);
    car->gear = 1;

    /* sub_00026458 body setup. */
    IgnBody *b = &car->body;
    b->mass = ign_car_mass[car->spec];
    b->inertia = 8000.0;
    car->turbo_charge = 100.0;
    b->restitution = 0.6;
    b->radius = 4.0;
    b->a = fabs(car->front) / IGN_WORLD_PER_METRE * 1.5;
    b->b = fabs(car->rear) / IGN_WORLD_PER_METRE * 2.0;
    b->grip_f = b->grip_r = 4.2;
    b->px = x / IGN_WORLD_PER_METRE;
    b->pz = z / IGN_WORLD_PER_METRE;
    b->heading = HALF_PI - heading;

    /* Box corners from the body mesh bounds (front-left, front-right, rear-right, rear-left). */
    const IgnObject *bo = &lv->cars.objs[body];
    double minx = INFINITY, maxx = -INFINITY, maxz = 0;
    for (int v = 0; v < bo->nv; v++) {
        double vx = bo->verts[v * 3], vz = fabs((double)bo->verts[v * 3 + 2]);
        if (vx < minx) minx = vx;
        if (vx > maxx) maxx = vx;
        if (vz > maxz) maxz = vz;
    }
    const double cxs[4] = {maxx, maxx, minx, minx}, czs[4] = {maxz, -maxz, -maxz, maxz};
    for (int i = 0; i < 4; i++) {
        b->corner_x[i] = cxs[i] / IGN_WORLD_PER_METRE;
        b->corner_z[i] = czs[i] / IGN_WORLD_PER_METRE;
    }

    IgnGroundHit hit;
    car->height = ign_ground_at(lv, x, z, 0.0f, &hit) == 0 ? -hit.y + VERT_REST : VERT_REST;
    int types[4];
    sample_ground(car, lv, types);
    car->height = car->ground + VERT_REST;
    car->ground_prev = car->ground;
    place(car);
    return 0;
}

/* sub_000500f0: smoothed steering limited by speed and surface grip. */
static void steer(IgnCar *car, int input, double grip_avg) {
    IgnBody *b = &car->body;
    int s = car->spec;
    car->steer_visual += (10.0 * 0.01 * input * 4.0 - car->steer_visual) * 0.1;
    double limit = 0.3;
    if (fabs(car->kmh) < ign_car_steer_speed[s])
        limit = (ign_car_steer_speed[s] - car->kmh) / ign_car_steer_div[s] + 0.3;
    double target = 5.0 / grip_avg * (input * 0.01) * limit;
    double rate = ign_car_steer_rate[s];
    if (target == 0) rate *= 0.5;
    b->steer += (target - b->steer) / rate;
}

/* sub_0004db90: torque curve * throttle, engine braking when released. */
static void throttle(IgnCar *car, int input) {
    IgnBody *b = &car->body;
    if (input == 0) {
        b->drive = -(b->v_fwd * 500.0);
        return;
    }
    int idx = (int)fabs(trunc(b->v_fwd * 3.6));
    if (idx > IGN_TORQUE_POINTS - 1) idx = IGN_TORQUE_POINTS - 1;
    double target = car->torque[car->gear][idx] * 1000.0 * (input * 0.01);
    b->drive += (target - b->drive) * 0.2;
}

/* sub_0004e2d8: burns 2.5 charge per tick while active, otherwise recharges. */
static void turbo(IgnCar *car) {
    IgnBody *b = &car->body;
    if (!car->turbo_mode) {
        if (car->turbo_charge < 100.0)
            car->turbo_charge += ign_car_turbo_rate[car->spec];
        else
            car->turbo_charge = 100.0;
        return;
    }
    car->turbo_charge -= 2.5;
    if (car->turbo_charge <= 0) {
        car->turbo_charge = 0;
        car->turbo_mode = 0;
    }
    float low = (float)((20.0 - b->v_fwd) * 10000.0);
    if (low < 0) low = 0;
    if (car->turbo_mode == 1) {
        b->drive += ign_car_turbo_force[car->spec] * car->turbo_charge + low;
    } else {
        b->drive += car->turbo_charge * 20000.0 + low;
        b->vx += cos(b->heading) * car->turbo_charge * 0.01;
        b->vz += sin(b->heading) * car->turbo_charge * 0.01;
    }
}

/* car_input 0x4d8eb: a full charge starts the turbo; otherwise the request is refused (beep). */
static void request_turbo(IgnCar *car) {
    if (car->turbo_charge >= 100.0 && !car->turbo_mode) {
        car->turbo_mode = 1;
        car->turbo_wait = 0;
    } else if (!car->turbo_mode && car->turbo_wait <= 0 && car->turbo_charge > 20.0) {
        car->turbo_wait = 30;
    }
}

/* sub_0002e6f8 + sub_00040ec8: body lean from per-tick acceleration, smoothed towards ground tilt. */
static void body_pose(IgnCar *car, double dx, double dz) {
    const IgnBody *b = &car->body;
    double ax = dx - car->step_x, az = dz - car->step_z;
    car->step_x = dx;
    car->step_z = dz;
    if (ax > 0.5) ax = 0.5;
    if (ax < -0.5) ax = -0.5;
    if (az > 0.5) az = 0.5;
    if (az < -0.5) az = -0.5;
    double ch = cos(b->heading), sh = sin(b->heading);
    double lat = -sh * ax + ch * az, lon = ch * ax + sh * az;
    car->lean_roll = (car->lean_roll - 3.0 * lat) * 0.97;
    car->lean_pitch = (car->lean_pitch - 3.0 * lon) * 0.97;
    if (car->lean_roll > 35) car->lean_roll = 35;
    if (car->lean_roll < -35) car->lean_roll = -35;
    if (car->lean_pitch > 20) car->lean_pitch = 20;
    if (car->lean_pitch < -20) car->lean_pitch = -20;
    double target_roll = b->roll + car->lean_roll * (PI / 180.0);
    double target_pitch = b->pitch + car->lean_pitch * (PI / 180.0);
    car->body_roll += (target_roll - car->body_roll) * 0.25;
    car->body_pitch += (target_pitch - car->body_pitch) * 0.25;
}

void ign_car_tick(IgnCar *car, const IgnLevel *lv, const IgnInput *in) {
    IgnBody *b = &car->body;
    double px0 = b->px, pz0 = b->pz;
    int types[4];
    sample_ground(car, lv, types);

    double grip_sum = 0;
    int barrier = 0;
    for (int w = 0; w < 4; w++) {
        grip_sum += ign_surface_grip[types[w] >= 0 && types[w] < 30 ? types[w] : 0];
        barrier |= types[w] == 0x50 || types[w] == 0x51; /* car+0x57c */
    }
    double grip_avg = grip_sum * 0.25; /* car+0x148 */
    car->surface_type = types[0];
    b->grip_f = grip_avg * ign_car_grip[car->spec];
    b->grip_r = grip_avg * ign_car_grip[car->spec]; /* x difficulty factor 0xc5470 (1.0 on easy) */

    /* Gearbox: brake selects reverse (gear 0, negated throttle); otherwise the stronger forward curve. */
    int idx = (int)fabs(trunc(b->v_fwd * 3.6));
    if (idx > IGN_TORQUE_POINTS - 1) idx = IGN_TORQUE_POINTS - 1;
    int throttle_in = 0;
    if (in->brake) {
        car->gear = 0;
        throttle_in = -100;
    } else if (in->accel) {
        car->gear = car->torque[2][idx] > car->torque[1][idx] ? 2 : 1;
        throttle_in = 100;
    }
    throttle(car, throttle_in);
    turbo(car);
    /* Positive body steering turns towards +z from +x, i.e. left as seen from above. */
    steer(car, (in->left ? 100 : 0) - (in->right ? 100 : 0), grip_avg);
    if (in->turbo) request_turbo(car);
    if (car->turbo_wait > 0) car->turbo_wait--;

    if (barrier) {
        enum { MAX_WALLS = 512 };
        static IgnWall walls[MAX_WALLS];
        static double wm[MAX_WALLS][4];
        int n = ign_collect_walls(lv, car->x, (float)-car->ground, car->z, walls, MAX_WALLS);
        for (int i = 0; i < n; i++) {
            wm[i][0] = walls[i].x0 / IGN_WORLD_PER_METRE;
            wm[i][1] = walls[i].z0 / IGN_WORLD_PER_METRE;
            wm[i][2] = walls[i].x1 / IGN_WORLD_PER_METRE;
            wm[i][3] = walls[i].z1 / IGN_WORLD_PER_METRE;
        }
        car->wall_hit = ign_body_collide(b, (const double(*)[4])wm, n);
    } else {
        car->wall_hit = 0;
    }
    ign_body_step(b, car->airborne, grip_avg > 0.4 ? 1.0 : 4.0);
    car->kmh = (float)fabs(b->v_fwd * 3.6);

    car->ground_prev = car->ground;
    sample_ground(car, lv, types);
    vertical_step(car);
    place(car);
    body_pose(car, (b->px - px0) * IGN_WORLD_PER_METRE, (b->pz - pz0) * IGN_WORLD_PER_METRE);
}
