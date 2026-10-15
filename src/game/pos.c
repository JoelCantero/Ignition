#include "pos.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int32_t rdi32(const uint8_t *p) { return (int32_t)rd32(p); }

static uint8_t *animation_at(IgnPos *pos, int object) {
    int32_t offset = rdi32(pos->data + (size_t)object * 4);
    if (offset == -1) return NULL;
    return pos->data + (size_t)object * 4 + (size_t)offset * 4;
}

void ign_pos_free(IgnPos *pos) {
    free(pos->data);
    memset(pos, 0, sizeof *pos);
}

int ign_pos_parse(const uint8_t *data, size_t size, int nobj, IgnPos *out) {
    if (!data || !out || nobj < 0 || (size_t)nobj > size / 4) return -1;
    memset(out, 0, sizeof *out);
    uint8_t *copy = malloc(size ? size : 1);
    if (!copy) return -1;
    memcpy(copy, data, size);
    IgnPos parsed = {.data = copy, .size = size, .nobj = nobj};

    for (int i = 0; i < nobj; i++) {
        int32_t offset = rdi32(copy + (size_t)i * 4);
        if (offset == -1) continue;
        if (offset < 0) goto fail;
        int64_t record = (int64_t)i * 4 + (int64_t)offset * 4;
        if (record < 0 || (uint64_t)record + 8 > size) goto fail;
        int32_t nframes = rdi32(copy + record);
        int32_t current = rdi32(copy + record + 4);
        if (nframes <= 0 || current < 0 || current >= nframes ||
            (uint64_t)record + 8 + (uint64_t)nframes * 24 > size)
            goto fail;

        int32_t first[3];
        for (int k = 0; k < 3; k++) first[k] = rdi32(copy + record + 8 + (size_t)k * 4);
        for (int32_t frame = 0; frame + 1 < nframes; frame++) {
            uint8_t *here = copy + record + 8 + (size_t)frame * 24;
            const uint8_t *next = here + 24;
            for (int k = 0; k < 3; k++)
                wr32(here + (size_t)k * 4, rd32(here + (size_t)k * 4) - rd32(next + (size_t)k * 4));
        }
        uint8_t *last = copy + record + 8 + (size_t)(nframes - 1) * 24;
        for (int k = 0; k < 3; k++) wr32(last + (size_t)k * 4, rd32(last + (size_t)k * 4) - (uint32_t)first[k]);
    }
    *out = parsed;
    return 0;

fail:
    free(copy);
    return -1;
}

int ign_pos_load(const char *path, int nobj, IgnPos *out) {
    size_t size = 0;
    uint8_t *data = ign_read_file(path, &size);
    if (!data) return -1;
    int rc = ign_pos_parse(data, size, nobj, out);
    free(data);
    return rc;
}

void ign_pos_step(IgnPos *pos, IgnModel *model) {
    if (!pos || !pos->data || !model || !model->objs || model->nobj != pos->nobj) return;
    for (int i = 0; i < pos->nobj; i++) {
        uint8_t *animation = animation_at(pos, i);
        if (!animation) continue;
        int32_t nframes = rdi32(animation);
        int32_t current = rdi32(animation + 4);
        if (current == nframes) current = 0;
        if (current < 0 || current >= nframes) continue;
        uint8_t *frame = animation + 8 + (size_t)current * 24;
        IgnObject *o = &model->objs[i];
        for (int k = 0; k < 3; k++)
            o->pos[k] = (int32_t)((uint32_t)o->pos[k] - rd32(frame + (size_t)k * 4));
        for (int k = 0; k < 3; k++) {
            int32_t angle = rdi32(frame + 12 + (size_t)k * 4);
            o->rotation[k] = angle >= 3600 ? angle - 3600 : angle;
        }
        current++;
        if (current == nframes) current = 0;
        wr32(animation + 4, (uint32_t)current);
    }
}

int ign_coll_init(IgnCollObjs *c, IgnModel *track) {
    memset(c, 0, sizeof *c);
    c->track = track;
    int phase = 0;
    for (int i = 0; i < track->nobj && c->n < IGN_MAX_COLL_OBJS; i++) {
        const IgnObject *src = &track->objs[i];
        if (src->type < 0x96 || src->type >= 0xc8) continue;
        IgnCollObj *o = &c->o[c->n++];
        o->kind = (int)src->type - 0x96;
        o->obj = i;
        o->pos[0] = src->pos[0] + IGN_SRF_ORIGIN;
        o->pos[1] = -src->pos[1];
        o->pos[2] = src->pos[2] + IGN_SRF_ORIGIN;
        /* 0x25105: mass and inertia by type; the lights share a phase counter stepping 4 frames per object. */
        switch (src->type) {
        case 0x96: case 0x9a:
            o->mass = 0.3f, o->inertia = 100.0f, o->period = 30;
            o->frame = (float)phase;
            phase += 4;
            if (phase >= o->period) phase = 0;
            break;
        case 0x98: o->mass = 25.0f, o->inertia = 8000.0f; break;
        case 0x99: o->mass = 2.0f, o->inertia = 1500.0f; break;
        default: return -1;
        }
    }
    return c->n;
}

void ign_coll_reflect(const double n[3], double v[3]) {
    /* Rotate about y so that the normal lies in x/y, then in x/y so that it is +x. */
    double a1 = atan2(n[2], n[0]);
    double s1 = sin(-a1), c1 = cos(-a1);
    double a2 = atan2(n[1], n[0] * c1 - n[2] * s1);
    double vx = v[0] * c1 - v[2] * s1, vz = v[0] * s1 + v[2] * c1;
    double s2 = sin(-a2), c2 = cos(-a2);
    double rx = vx * c2 - v[1] * s2, ry = vx * s2 + v[1] * c2;
    if (rx > 0) return;
    rx = -rx;
    double s = sin(a2), c = cos(a2);
    double x = rx * c - ry * s;
    v[1] = rx * s + c * ry;
    s = sin(a1), c = cos(a1);
    v[0] = x * c - vz * s;
    v[2] = x * s + vz * c;
}

double ign_coll_ground_up(const IgnGroundHit *hit, double x, double z) {
    const int32_t *p0 = hit->p[0], *p1 = hit->p[1];
    double a = hit->normal[0], b = hit->normal[1], c = hit->normal[2];
    double e1x = (double)p1[0] - p0[0], e1y = (double)p1[1] - p0[1], e1z = (double)p1[2] - p0[2];
    double dx = x - p0[0], dz = z - p0[2];
    double t = (-a * dx + a * e1x - c * dz + c * e1z + b * e1y) / b;
    return -(p0[1] + t);
}

/* 0x356b8: one frame of a flying object; returns 1 if it is still flying. */
static int coll_fly(IgnCollObj *o, const IgnLevel *lv) {
    o->vel[0] *= 0.99;
    o->vel[1] += -0.4;
    o->vel[2] = 0.99 * o->vel[2];
    for (int k = 0; k < 3; k++) {
        o->ang[k] += o->spin[k];
        if (0 > o->ang[k]) o->ang[k] += 3600.0;
        if (o->ang[k] > 3599.0) o->ang[k] += -3600.0;
    }
    for (int k = 0; k < 3; k++) o->spin[k] *= 0.98;
    for (int k = 0; k < 3; k++) o->pos[k] += o->vel[k];
    if (0 > o->pos[0]) o->pos[0] = 0;
    if (o->pos[0] > 51200.0) o->pos[0] = 51200.0;
    if (o->pos[1] < -10000.0) o->pos[1] = -10000.0;
    if (o->pos[1] > 10000.0) o->pos[1] = 10000.0;
    if (0 > o->pos[2]) o->pos[2] = 0;
    if (o->pos[2] > 51200.0) o->pos[2] = 51200.0;

    IgnGroundHit hit;
    if (ign_ground_query(lv, (int32_t)trunc(o->pos[0]), (int32_t)trunc(o->pos[1]) + 150, (int32_t)trunc(o->pos[2]),
                         &hit))
        return 1;
    double ground = ign_coll_ground_up(&hit, o->pos[0], o->pos[2]);
    if (!(ground + 15.0 > o->pos[1])) return 1;
    double speed = sqrt(pow(o->vel[0], 2.0) + pow(o->vel[1], 2.0) + pow(o->vel[2], 2.0));
    if (!(speed > 2.0)) {
        /* 0x35c29: too slow to bounce, it rests where it is. */
        memset(o->vel, 0, sizeof o->vel);
        memset(o->spin, 0, sizeof o->spin);
        return 0;
    }
    /* 0x35a6b: bounce on the face, (p0 - p1) x (p2 - p1) with y flipped up. */
    const int32_t *p0 = hit.p[0], *p1 = hit.p[1], *p2 = hit.p[2];
    double ax = p0[0] - p1[0], ay = p0[1] - p1[1], az = p0[2] - p1[2];
    double bx = p2[0] - p1[0], by = p2[1] - p1[1], bz = p2[2] - p1[2];
    double n[3] = {ay * bz - az * by, ax * bz - az * bx, ax * by - ay * bx};
    ign_coll_reflect(n, o->vel);
    o->vel[0] *= 0.9;
    o->vel[1] *= 0.6;
    o->vel[2] = 0.9 * o->vel[2];
    if (o->vel[1] > 20.0) o->vel[1] = 20.0;
    o->pos[1] = ground + 15.0;
    for (int k = 0; k < 3; k++) o->spin[k] = speed * 5.0;
    return 1;
}

/* 0x3514e: a resting object against one car. */
static void coll_touch(IgnCollObj *o, IgnCar *car, IgnFx *fx) {
    if (!(fabs(car->height - o->pos[1]) < 50.0) || car->wrecked) return;
    double cx = car->body.px * IGN_WORLD_PER_METRE + IGN_SRF_ORIGIN, cz = car->body.pz * IGN_WORLD_PER_METRE + IGN_SRF_ORIGIN;
    if (!(fabs(cx - o->pos[0]) < 150.0) || !(fabs(cz - o->pos[2]) < 150.0)) return;
    float box[4][2], obj[4][2];
    ign_car_contact_box(car, 1, box);
    obj[0][0] = (float)(o->pos[0] + -30.0), obj[0][1] = (float)(o->pos[2] + -30.0);
    obj[1][0] = (float)(o->pos[0] + 30.0), obj[1][1] = obj[0][1];
    obj[2][0] = obj[1][0], obj[2][1] = (float)(o->pos[2] + 30.0);
    obj[3][0] = obj[0][0], obj[3][1] = obj[2][1];
    float px, pz, theta;
    if (!ign_contact_polygons((const float(*)[2])box, 4, (const float(*)[2])obj, 4, &px, &pz, &theta)) return;
    IgnContactBody a = {cx, cz, car->contact_v[0], car->contact_v[1], car->body.yaw_rate, car->contact_mass,
                        car->contact_inertia, car->contact_impact};
    IgnContactBody b = {o->pos[0], o->pos[2], o->cvx, o->cvz, o->cw, o->mass, o->inertia, o->impact};
    if (!ign_contact_impulse(&a, &b, px, pz, theta)) return;
    /* 0x354bd: the car keeps its velocity (only position, yaw rate and the contact floats are copied back). */
    car->body.yaw_rate = a.w;
    car->contact_mass = a.mass, car->contact_inertia = a.inertia, car->contact_impact = a.impact;
    o->cvx = b.vx, o->cvz = b.vz, o->cw = b.w;
    o->mass = b.mass, o->inertia = b.inertia, o->impact = b.impact;
    o->vel[0] = o->cvx;
    o->vel[1] = ign_fx_rand(fx) * 2.0 + 2.0;
    o->vel[2] = o->cvz;
    for (int k = 0; k < 3; k++) o->spin[k] = (double)o->impact * 5.0;
    o->flying = 1;
    /* 0x35602: COLL sample 1 above an impact of 10. */
    if (car->contact_impact > 10.0f) {
        int32_t vol = (int32_t)trunc((fabs((double)car->contact_impact) + -10.0) * 3000.0);
        if (vol > 0x10000) vol = 0x10000;
        ign_fx_sound(fx, 0, 2, 1, vol, 22000, (float)(trunc(cx) - IGN_SRF_ORIGIN), (float)(trunc(cz) - IGN_SRF_ORIGIN));
    }
}

void ign_coll_frame(IgnCollObjs *c, const IgnLevel *lv, IgnCar *const *cars, int ncars, IgnFx *fx, float focus_x,
                    float focus_z) {
    for (int k = 0; k < c->n; k++) {
        IgnCollObj *o = &c->o[k];
        if (!o->flying) {
            for (int i = 0; i < ncars; i++) coll_touch(o, cars[i], fx);
        } else {
            o->flying = coll_fly(o, lv);
            /* 0x35c84: LI_MOVEOBJECT with the truncated pose. */
            IgnObject *e = &c->track->objs[o->obj];
            e->pos[0] = (int32_t)trunc(o->pos[0]) - IGN_SRF_ORIGIN;
            e->pos[1] = -(int32_t)trunc(o->pos[1]);
            e->pos[2] = (int32_t)trunc(o->pos[2]) - IGN_SRF_ORIGIN;
            for (int a = 0; a < 3; a++) e->rotation[a] = (int32_t)trunc(o->ang[a]);
        }
        if (o->kind != 4) continue;
        /* 0x35d7a: the light shows during the first fifth of its period (LIGHT.TEX frames 0, 2, 4), 2 frames each. */
        int32_t frame = (int32_t)trunc(o->frame);
        if (o->period / 5 > frame) {
            int32_t anim = frame == 0 ? 0 : 0x20;
            if (o->period / 5 - 1 == frame) anim = 0x40;
            int32_t t[IGN_FX_WORDS] = {IGN_FX_SPRITE};
            t[1] = (int32_t)trunc(o->pos[0] * 1024.0) - IGN_SRF_ORIGIN * 1024;
            t[2] = (int32_t)trunc((o->pos[1] + 20.0) * 1024.0);
            t[3] = (int32_t)trunc(1024.0 * o->pos[2]) - IGN_SRF_ORIGIN * 1024;
            t[7] = 0x400, t[9] = 3, t[12] = 0x64000, t[14] = 0xc4010 + anim, t[22] = 2;
            ign_fx_add(fx, t, focus_x, focus_z);
        }
        o->frame += 1.0f;
        if (!((float)o->period > o->frame)) o->frame = 0;
    }
}
