#include "pos.h"

#include <math.h>
#include <stdio.h>
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

    /* 0x41244: preserve angles/cursors and convert positions to cyclic, wrapping 32-bit deltas. */
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
        ign_engine_moved(model, i);
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
        /* split_plc_types 0x247b0 runs first: the world sees the class, type & 0xfff. */
        uint32_t klass = src->type & 0xfff;
        if (klass < 0x96 || klass >= 0xc8) continue;
        IgnCollObj *o = &c->o[c->n++];
        o->kind = (int)klass - 0x96;
        o->obj = i;
        o->pos[0] = src->pos[0] + IGN_SRF_ORIGIN;
        o->pos[1] = -src->pos[1];
        o->pos[2] = src->pos[2] + IGN_SRF_ORIGIN;
        /* 0x25105: mass and inertia by type; the lights share a phase counter stepping 4 frames per object. */
        switch (klass) {
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
    return ign_ground_plane_up(hit, x, z);
}

void ign_coll_fly_step(IgnCollObj *o) {
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
}

void ign_coll_bounce(IgnCollObj *o, const IgnGroundHit *hit) {
    double ground = ign_coll_ground_up(hit, o->pos[0], o->pos[2]);
    if (!(ground + 15.0 > o->pos[1])) return;
    double speed = sqrt(pow(o->vel[0], 2.0) + pow(o->vel[1], 2.0) + pow(o->vel[2], 2.0));
    if (!(speed > 2.0)) {
        /* 0x35c29: too slow to bounce, it rests where it is. */
        memset(o->vel, 0, sizeof o->vel);
        memset(o->spin, 0, sizeof o->spin);
        o->flying = 0;
        return;
    }
    /* 0x35a6b: bounce on the face, (p0 - p1) x (p2 - p1) with y flipped up. */
    const int32_t *p0 = hit->p[0], *p1 = hit->p[1], *p2 = hit->p[2];
    double ax = (double)p0[0] - p1[0], ay = (double)p0[1] - p1[1], az = (double)p0[2] - p1[2];
    double bx = (double)p2[0] - p1[0], by = (double)p2[1] - p1[1], bz = (double)p2[2] - p1[2];
    double n[3] = {ay * bz - az * by, ax * bz - az * bx, ax * by - ay * bx};
    ign_coll_reflect(n, o->vel);
    o->vel[0] *= 0.9;
    o->vel[1] *= 0.6;
    o->vel[2] = 0.9 * o->vel[2];
    if (o->vel[1] > 20.0) o->vel[1] = 20.0;
    o->pos[1] = ground + 15.0;
    for (int k = 0; k < 3; k++) o->spin[k] = speed * 5.0;
}

/* 0x356b8: one frame of a flying object. */
static void coll_fly(IgnCollObj *o, const IgnLevel *lv) {
    ign_coll_fly_step(o);
    IgnGroundHit hit;
    if (ign_ground_query(lv, (int32_t)trunc(o->pos[0]), (int32_t)trunc(o->pos[1]) + 150, (int32_t)trunc(o->pos[2]),
                         &hit) == 0)
        ign_coll_bounce(o, &hit);
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
            coll_fly(o, lv);
            /* 0x35c84: LI_MOVEOBJECT with the truncated pose. */
            IgnObject *e = &c->track->objs[o->obj];
            e->pos[0] = (int32_t)trunc(o->pos[0]) - IGN_SRF_ORIGIN;
            e->pos[1] = -(int32_t)trunc(o->pos[1]);
            e->pos[2] = (int32_t)trunc(o->pos[2]) - IGN_SRF_ORIGIN;
            for (int a = 0; a < 3; a++) e->rotation[a] = (int32_t)trunc(o->ang[a]);
            ign_engine_moved(c->track, o->obj);
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

/* 0x33aa8: crossing-number test of (px, pz) against a face's x/z (integer, as the original). */
static int face_covers(const int32_t *v0, const int32_t *v1, const int32_t *v2, int32_t px, int32_t pz) {
    const int32_t *v[3] = {v0, v1, v2};
    int inside = 0;
    for (int i = 0, j = 2; i < 3; j = i++) {
        int32_t zi = v[i][2], zj = v[j][2];
        if (!((pz >= zi && pz < zj) || (pz >= zj && pz < zi))) continue;
        int32_t t = (int32_t)((uint32_t)(v[j][0] - v[i][0]) * (uint32_t)(pz - zi)) / (zj - zi) + v[i][0];
        if (t > px) inside = !inside;
    }
    return inside;
}

int ign_special_query(const IgnLevel *lv, int32_t x, int32_t y, int32_t z, int jump, int layer) {
    int32_t reach = jump ? 900 : 600;
    int near_height = 0, found = -1;
    /* The engine's object list after sub_00058c1c: the view's cells around the car; without the grid (synthetic
     * levels) every object in PLC order. */
    static int list[8192];
    int n = lv->track.cell ? ign_engine_view(&lv->track, x, z, layer, list, 8192) : lv->track.nobj;
    for (int k = 0; k < n; k++) {
        int i = lv->track.cell ? list[k] : k;
        const IgnObject *o = &lv->track.objs[i];
        int type = (int)(o->type & 0xfff); /* word +0x1e: the class */
        if (type < 0x12c || type >= 0x15e) continue;
        int32_t dx = x - (o->pos[0] + IGN_SRF_ORIGIN), dy = y + o->pos[1], dz = z - (o->pos[2] + IGN_SRF_ORIGIN);
        if (abs(dy) < 190) near_height = 1; /* 0x339d0: never cleared for the following objects */
        if (abs(dx) >= reach || abs(dz) >= reach || !near_height) continue;
        for (int f = 0; f < o->nf; f++) {
            const int32_t *vs = o->verts;
            const IgnFace *face = &o->faces[f];
            if (face_covers(vs + face->v[0] * 3, vs + face->v[1] * 3, vs + face->v[2] * 3, dx, dz)) {
                found = type;
                break;
            }
        }
    }
    return found;
}

static int erupting_geyser(const IgnLevel *lv, const IgnFx *fx, const IgnCar *car) {
    /* 0x2f202: any list entry within 80 in x and z whose phase (+0xc, the geyser of sub_00036ce0) runs. */
    for (int i = 0; i < fx->nemit; i++) {
        const IgnObject *o = &lv->track.objs[fx->emit[i].obj];
        if (fabs((double)o->pos[0] - car->body.px * IGN_WORLD_PER_METRE) < 80.0 &&
            fabs((double)o->pos[2] - car->body.pz * IGN_WORLD_PER_METRE) < 80.0 && fx->emit[i].phase > 0)
            return 1;
    }
    return 0;
}

int ign_special_probe(const IgnLevel *lv, const IgnFx *fx, const IgnCar *car, int iceland, int layer, int *wreck) {
    *wreck = 0;
    double cx = car->body.px * IGN_WORLD_PER_METRE, cz = car->body.pz * IGN_WORLD_PER_METRE;
    int type = -1, launch = 0;
    /* 0x2ef9d: while a lightning bolt lives ([0x1fede4], set after its first frame by sub_0003bdac), sub_00032dc8
     * under the car; a 0x12d answer launches it. */
    int lightning = 0;
    for (int i = 0; i < IGN_FX_SLOTS; i++) lightning |= fx->r[i][0] == IGN_FX_LIGHTNING && fx->r[i][5] >= 1;
    if (lightning && lv->track.cell) {
        IgnGroundHit hit;
        if (ign_ground_list_query(lv, (int32_t)trunc(cx + IGN_SRF_ORIGIN), (int32_t)trunc(car->height),
                                  (int32_t)trunc(cz + IGN_SRF_ORIGIN), car->jump_mode, layer, 50, &hit) == 0 &&
            hit.surface_type == 0x12d)
            launch = 1;
    }
    for (int i = 0; i < lv->track.nobj; i++) {
        const IgnObject *o = &lv->track.objs[i];
        uint32_t klass = o->type & 0xfff;
        if (klass < 0x12c || klass >= 0x15e) continue;
        double dx = cx - o->pos[0], dz = cz - o->pos[2];
        if (!(fabs(dx) < 200.0) || !(fabs(dz) < 200.0)) continue;
        if (iceland && klass == 0x12c) {
            /* 0x2f0cd: the 0x12c objects of ICELAND explode a car within 30. */
            if (fabs(dx) < 30.0 && fabs(car->height + o->pos[1]) < 30.0 && fabs(dz) < 30.0) *wreck = 1;
            continue;
        }
        /* 0x2f124: the query does not depend on the entry, so one call answers for all of them. */
        type = ign_special_query(lv, (int32_t)trunc(cx + IGN_SRF_ORIGIN), (int32_t)trunc(car->height),
                                 (int32_t)trunc(cz + IGN_SRF_ORIGIN), car->jump_mode, layer);
        if (type < 0x12c && iceland) continue;
        break;
    }
    switch (type) {
    case 0x12c: *wreck = 1; return launch ? IGN_SPECIAL_LAUNCH : IGN_SPECIAL_NONE;
    case 0x12d: return IGN_SPECIAL_LAUNCH;
    case 0x131: return launch || erupting_geyser(lv, fx, car) ? IGN_SPECIAL_LAUNCH : IGN_SPECIAL_NONE;
    case 0x12e: return IGN_SPECIAL_PUSH;
    case 0x12f: return IGN_SPECIAL_MESH;
    case 0x130: *wreck = 3; return launch ? IGN_SPECIAL_LAUNCH : IGN_SPECIAL_NONE;
    default: return launch ? IGN_SPECIAL_LAUNCH : IGN_SPECIAL_NONE;
    }
}

static int push_wait; /* [0x1fee3c] */

void ign_special_apply(IgnCar *car, int special, int wreck, IgnFx *fx, float focus_x, float focus_z) {
    if (wreck) ign_car_wreck(car, wreck); /* 0x30dbb: explosion (car+0x354) or slide (car+0x35c) */
    if (special == IGN_SPECIAL_LAUNCH && !car->airborne && !car->launch) {
        /* 0x30f66: 8 upwards (at most 10), the somersault starts, and a piece flies off. */
        car->fall_speed += -8.0;
        car->launch = 1;
        if (car->fall_speed < -10.0) car->fall_speed = -10.0;
        int32_t p[IGN_FX_WORDS] = {IGN_FX_PIECE};
        p[1] = (int32_t)trunc(car->body.px * IGN_WORLD_PER_METRE * 1024.0);
        p[2] = (int32_t)trunc(car->height * 1024.0);
        p[3] = (int32_t)trunc(car->body.pz * IGN_WORLD_PER_METRE * 1024.0);
        p[4] = (int32_t)trunc((ign_fx_rand(fx) * 4.0 + -2.0) * 1024.0);
        p[5] = (int32_t)trunc((ign_fx_rand(fx) * 3.0 + 3.0) * 1024.0);
        p[6] = (int32_t)trunc((ign_fx_rand(fx) * 4.0 + -2.0) * 1024.0);
        p[7] = 0x400, p[8] = -460;
        for (int a = 0; a < 3; a++) p[12 + a] = (int32_t)trunc((ign_fx_rand(fx) * 100.0 + -50.0) * 1024.0);
        p[16] = 1, p[19] = 30;
        ign_fx_add(fx, p, focus_x, focus_z);
    } else if (special == IGN_SPECIAL_PUSH && !car->airborne && fabs(car->height - car->ground) < 50.0) {
        /* 0x3113e: thrown up 4.5-7.5 and spun 2.5-4 rad/s clockwise, with GENERAL sample 8. */
        car->fall_speed = car->fall_speed - (ign_fx_rand(fx) * 3.0 + 4.5);
        car->body.yaw_rate = car->body.yaw_rate - (ign_fx_rand(fx) * 1.5 + 2.5);
        car->airborne = 1;
        if (car->fall_speed < -10.0) car->fall_speed = -10.0;
        if (push_wait == 0) {
            ign_fx_sound(fx, 1, 0, 8, 0x10000, 22000, (float)trunc(car->body.px * IGN_WORLD_PER_METRE),
                         (float)trunc(car->body.pz * IGN_WORLD_PER_METRE));
            push_wait = 15;
        }
    }
    else if (special == IGN_SPECIAL_MESH && !car->flat)
        ign_car_flatten(car); /* 0x31229: car+0x558/+0x55c/+0x560 and sub_00031858 */
    if (push_wait > 0) push_wait--;
}

void ign_car_flat_effects(IgnCar *car, IgnFx *fx, float focus_x, float focus_z) {
    int was_flat = car->flat > 0 && car->flat_sound != 6;
    if (car->flat_sound) {
        ign_fx_sound(fx, 0, 2, car->flat_sound, 0x10000, 22000, (float)trunc(car->x), (float)trunc(car->z));
        car->flat_sound = 0;
    }
    /* 0x31687: while flattened, one tick in six a puff of SMOKE around the car. */
    if (!was_flat || (int)trunc(ign_fx_rand(fx) * 6.0) != 0) return;
    int32_t t[IGN_FX_WORDS] = {IGN_FX_SPRITE};
    t[1] = (int32_t)trunc((ign_fx_rand(fx) * 50.0 + car->x + -25.0) * 1024.0);
    t[2] = (int32_t)trunc((car->height + 10.0) * 1024.0);
    t[3] = (int32_t)trunc((ign_fx_rand(fx) * 50.0 + car->z + -25.0) * 1024.0);
    t[4] = (int32_t)trunc((ign_fx_rand(fx) + -0.5) * 1024.0);
    t[5] = (int32_t)trunc((ign_fx_rand(fx) + 1.0) * 1024.0);
    t[6] = (int32_t)trunc((ign_fx_rand(fx) + -0.5) * 1024.0);
    t[7] = 0x400, t[9] = 5, t[10] = 0xc00, t[11] = 0x66;
    t[12] = (int32_t)trunc((ign_fx_rand(fx) * 150.0 + 150.0) * 1024.0);
    t[13] = (int32_t)trunc((ign_fx_rand(fx) * 3.0 + 3.0) * 1024.0);
    t[14] = 0xc4110, t[22] = 50;
    ign_fx_add(fx, t, focus_x, focus_z);
}

int ign_texanim_period(int track) {
    static const int PERIOD[7] = {6, 20, 2, 4, 12, 9, 12};
    return track >= 0 && track < 7 ? PERIOD[track] : 12;
}

static int textured(const IgnFace *f) {
    int kind = (int)(f->flags & 0xff);
    return kind >= 0x11 && kind != 0x14;
}

/* 0x10cf8 / 0x10c5c: signed inclusive bounds, including the original 32-bit bound additions. */
static int uv_in_tile(uint32_t page, int32_t u, int32_t v, uint32_t target,
                      int32_t u0, int32_t v0, int32_t u1, int32_t v1) {
    return page == target && u >= u0 && u <= u1 && v >= v0 && v <= v1;
}

static int in_tile(const IgnFace *f, uint32_t page, int32_t u, int32_t v) {
    int32_t u1 = (int32_t)((uint32_t)u + 0x4000u), v1 = (int32_t)((uint32_t)v + 0x4000u);
    for (int k = 0; k < 3; k++)
        if (!uv_in_tile(f->extra, f->uv[2 * k], f->uv[2 * k + 1], page, u, v, u1, v1))
            return 0;
    return 1;
}

/* 0x1098c: only the initial run of textured faces contributes animation frames. */
static int texanim_count_frames(const IgnObject *object) {
    int n = 0;
    while (n < object->nf && textured(&object->faces[n])) n++;
    return n;
}

/* 0x10bc8 / 0x10b30: the same traversal counts first, then collects into the allocated list. */
static size_t texanim_collect(IgnModel *track, uint32_t page, int32_t u, int32_t v, IgnFace **out) {
    size_t n = 0;
    for (int o = 0; o < track->nobj; o++) {
        const IgnObject *object = &track->objs[o];
        for (int f = 0; f < object->nf && textured(&object->faces[f]); f++) {
            if (!in_tile(&object->faces[f], page, u, v)) continue;
            if (out) out[n] = (IgnFace *)&object->faces[f];
            n++;
        }
    }
    return n;
}

int ign_texanim_init(IgnTexAnims *t, IgnModel *track, int period) {
    memset(t, 0, sizeof *t);
    t->period = period;
    for (int i = 0; i < track->nobj; i++) {
        const IgnObject *src = &track->objs[i];
        if ((src->type & 0xfff) != 0xfa) continue; /* after split_plc_types: the class */
        int n = texanim_count_frames(src);
        if (n == 0) continue; /* 0x106d0: nothing kept */
        IgnTexAnim *na = realloc(t->a, sizeof *t->a * (size_t)(t->n + 1));
        if (!na) goto fail;
        t->a = na;
        IgnTexAnim *a = &t->a[t->n++];
        *a = (IgnTexAnim){.n = n, .cur = 1, .prev = 0};
        a->uv = calloc((size_t)n, sizeof *a->uv);
        a->page = calloc((size_t)n, sizeof *a->page);
        a->faces = calloc((size_t)n, sizeof *a->faces);
        if (!a->uv || !a->page || !a->faces) goto fail;
        for (int k = 0; k < n; k++) {
            const IgnFace *f = &src->faces[k];
            a->uv[k][0] = (int32_t)((uint32_t)f->uv[0] & 0xffffc000u);
            a->uv[k][1] = (int32_t)((uint32_t)f->uv[1] & 0xffffc000u);
            a->page[k] = f->extra & 0xffff0000u;
        }
        for (int k = 0; k < n; k++) {
            /* 0x106e9: a frame repeating an earlier tile gets no faces. */
            int repeat = 0;
            for (int j = 0; j < k; j++)
                repeat |= a->page[j] == a->page[k] && a->uv[j][0] == a->uv[k][0] && a->uv[j][1] == a->uv[k][1];
            size_t count = repeat ? 0 : texanim_collect(track, a->page[k], a->uv[k][0], a->uv[k][1], NULL);
            a->faces[k] = calloc(count + 1, sizeof **a->faces);
            if (!a->faces[k]) goto fail;
            if (!repeat) texanim_collect(track, a->page[k], a->uv[k][0], a->uv[k][1], a->faces[k]);
        }
    }
    return t->n;

fail:
    fprintf(stderr, "texanim: cannot allocate animation data\n");
    ign_texanim_free(t);
    return -1;
}

/* 0x10390 / 0x1044c: identical game teardown; also release the original's leaked face-list container. */
void ign_texanim_free(IgnTexAnims *t) {
    for (int i = 0; i < t->n; i++) {
        if (t->a[i].faces)
            for (int k = 0; k < t->a[i].n; k++) free(t->a[i].faces[k]);
        free(t->a[i].faces), free(t->a[i].uv), free(t->a[i].page);
    }
    free(t->a);
    memset(t, 0, sizeof *t);
}

void ign_texanim_step(IgnTexAnims *t) {
    for (int i = 0; i < t->n; i++) {
        IgnTexAnim *a = &t->a[i];
        for (int k = 0; k < a->n; k++) {
            int from = (a->prev + k) % a->n, to = (a->cur + k) % a->n;
            uint32_t dpage = a->page[from] - a->page[to];
            int32_t du = (int32_t)((uint32_t)a->uv[from][0] - (uint32_t)a->uv[to][0]);
            int32_t dv = (int32_t)((uint32_t)a->uv[from][1] - (uint32_t)a->uv[to][1]);
            for (IgnFace **f = a->faces[k]; f && *f; f++) {
                (*f)->extra -= dpage;
                for (int c = 0; c < 3; c++) {
                    (*f)->uv[2 * c] = (int32_t)((uint32_t)(*f)->uv[2 * c] - (uint32_t)du);
                    (*f)->uv[2 * c + 1] = (int32_t)((uint32_t)(*f)->uv[2 * c + 1] - (uint32_t)dv);
                }
            }
        }
        a->prev = a->cur;
        a->cur++;
    }
}

void ign_texanim_frame(IgnTexAnims *t) {
    t->clock += 1.0f;
    if ((float)t->period > t->clock || t->period <= 0) return;
    ign_texanim_step(t);
    while (t->clock >= (float)t->period) t->clock -= (float)t->period;
}

int ign_message_add(IgnMessage m[IGN_MESSAGES], const char *text, int duration, int blink, int player) {
    for (int i = 0; i < IGN_MESSAGES; i++) {
        if (m[i].active) continue;
        m[i].active = 1;
        snprintf(m[i].text, sizeof m[i].text, "%s", text);
        m[i].age = 0;
        m[i].blink = blink, m[i].player = player, m[i].duration = duration;
        return i;
    }
    return -1;
}

void ign_messages_age(IgnMessage m[IGN_MESSAGES]) {
    for (int i = 0; i < IGN_MESSAGES; i++)
        if (m[i].active == 1 && ++m[i].age > m[i].duration) m[i].active = 0;
}

int ign_message_visible(const IgnMessage *m) {
    return m->active == 1 && !((double)m->blink < fmod((double)m->age, (double)(m->blink * 2)));
}

void ign_fx_elimination_smoke(IgnFx *fx, const IgnCar *car, float focus_x, float focus_z) {
    int32_t pos[3] = {(int32_t)trunc(car->x * 1024.0), (int32_t)trunc(car->height * 1024.0),
                      (int32_t)trunc(car->z * 1024.0)};
    /* car+0x108/+0xf8/+0x110 x 180 x (1/pi) x 10, in that order. */
    int32_t ang[3] = {(int32_t)trunc(car->body.roll * 180.0 * 0.31830988618379 * 10.0),
                      (int32_t)trunc(car->body.heading * 180.0 * 0.31830988618379 * 10.0),
                      (int32_t)trunc(car->body.pitch * 180.0 * 0.31830988618379 * 10.0)};
    int rear = (int)car->body_min_x;
    for (int k = 0; k < 3; k++) {
        /* 0x4fd08: two EXSMOKE puffs; 0x4feec: then a DARKSMOK one when trunc(rand * 2) == 0. */
        if (k == 2 && (int)trunc(ign_fx_rand(fx) * 2.0) != 0) break;
        int dark = k == 2;
        int32_t t[IGN_FX_WORDS] = {IGN_FX_SPRITE, pos[0], pos[1], pos[2]};
        t[4] = (int32_t)trunc(ign_fx_rand(fx) * 1024.0 + -512.0 + car->step_x * 0.5 * 1024.0);
        t[5] = 0x400;
        t[6] = (int32_t)trunc(ign_fx_rand(fx) * 1024.0 + -512.0 + car->step_z * 0.5 * 1024.0);
        t[7] = 0x400;
        t[9] = dark ? 5 : 7;
        t[11] = dark ? 0x133 : 0x200;
        t[12] = dark ? 0x25800 : 0x3e800;
        t[13] = dark ? 0x1000 : -0x1800;
        t[14] = dark ? 0xc4230 : 0xc43c0;
        t[15] = (int32_t)trunc(ign_fx_rand(fx) * 10.0 + rear);
        t[17] = (int32_t)trunc(dark ? ign_fx_rand(fx) * 50.0 + -25.0 : ign_fx_rand(fx) * 30.0 + -15.0);
        t[18] = ang[0], t[19] = ang[1], t[20] = ang[2];
        t[22] = dark ? 25 : 14;
        ign_fx_add(fx, t, focus_x, focus_z);
    }
}
