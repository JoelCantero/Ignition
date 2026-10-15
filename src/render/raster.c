#include "raster.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NEAR_Z 8.0f
#define FAR_Z 30000.0f
#define MAX_POLY 8
/* CARS models face +x; rotate so heading 0 drives along +z. */
#define IGN_CAR_MODEL_YAW (-1.5707963f)

typedef struct {
    float x, y, z, u, v;
} Vert;

typedef struct {
    float sx, sy, iz, uz, vz, u, v;
} ScreenVert;

static float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static void project(const IgnFrame *fb, const Vert *p, ScreenVert *s) {
    float iz = 1.0f / p->z;
    s->sx = fb->w * 0.5f + p->x * fb->fx * iz;
    s->sy = fb->h * 0.5f + p->y * fb->fy * iz;
    s->iz = iz;
    s->uz = p->u * iz;
    s->vz = p->v * iz;
    s->u = p->u;
    s->v = p->v;
}

/* `lut` non-NULL: translucent, dst = lut[texel * 256 + dst] without depth write (primitive 19 with the SHD). */
static void draw_tri(IgnFrame *fb, const ScreenVert *a, const ScreenVert *b, const ScreenVert *c,
                     const uint8_t *page, int keyed, const uint8_t *lut) {
    float area = edge(a->sx, a->sy, b->sx, b->sy, c->sx, c->sy);
    if (fabsf(area) < 1e-6f) return;
    if (area < 0) {
        const ScreenVert *t = b;
        b = c;
        c = t;
        area = -area;
    }
    int x0 = (int)floorf(fminf(a->sx, fminf(b->sx, c->sx)));
    int x1 = (int)ceilf(fmaxf(a->sx, fmaxf(b->sx, c->sx)));
    int y0 = (int)floorf(fminf(a->sy, fminf(b->sy, c->sy)));
    int y1 = (int)ceilf(fmaxf(a->sy, fmaxf(b->sy, c->sy)));
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > fb->w - 1) x1 = fb->w - 1;
    if (y1 > fb->h - 1) y1 = fb->h - 1;
    float inv_area = 1.0f / area;

    for (int y = y0; y <= y1; y++) {
        float py = y + 0.5f;
        for (int x = x0; x <= x1; x++) {
            float px = x + 0.5f;
            float w0 = edge(b->sx, b->sy, c->sx, c->sy, px, py);
            float w1 = edge(c->sx, c->sy, a->sx, a->sy, px, py);
            float w2 = edge(a->sx, a->sy, b->sx, b->sy, px, py);
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            w0 *= inv_area;
            w1 *= inv_area;
            w2 *= inv_area;
            float iz = w0 * a->iz + w1 * b->iz + w2 * c->iz;
            int idx = y * fb->w + x;
            if (iz <= fb->inv_z[idx]) continue;
            float z = 1.0f / iz;
            int u, v;
            if (fb->perspective) {
                u = (int)((w0 * a->uz + w1 * b->uz + w2 * c->uz) * z);
                v = (int)((w0 * a->vz + w1 * b->vz + w2 * c->vz) * z);
            } else {
                u = (int)floorf(w0 * a->u + w1 * b->u + w2 * c->u);
                v = (int)floorf(w0 * a->v + w1 * b->v + w2 * c->v);
            }
            uint8_t texel = page[(v & 255) * 256 + (u & 255)];
            if (keyed && texel == 0) continue;
            if (lut) {
                fb->color[idx] = lut[texel * 256 + fb->color[idx]];
                continue;
            }
            fb->color[idx] = texel;
            fb->inv_z[idx] = iz;
        }
    }
}

static int clip_near(const Vert *in, int n, Vert *out) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        const Vert *p = &in[i], *q = &in[(i + 1) % n];
        int pin = p->z >= NEAR_Z, qin = q->z >= NEAR_Z;
        if (pin) out[m++] = *p;
        if (pin != qin) {
            float t = (NEAR_Z - p->z) / (q->z - p->z);
            out[m].x = p->x + (q->x - p->x) * t;
            out[m].y = p->y + (q->y - p->y) * t;
            out[m].z = NEAR_Z;
            out[m].u = p->u + (q->u - p->u) * t;
            out[m].v = p->v + (q->v - p->v) * t;
            m++;
        }
    }
    return m;
}

void ign_frame_init(IgnFrame *fb, int w, int h) {
    fb->w = w < 1 ? 1 : w > IGN_MAX_W ? IGN_MAX_W : w;
    fb->h = h < 1 ? 1 : h > IGN_MAX_H ? IGN_MAX_H : h;
    /* race_setup_video: 200/164 at 320x200 (tall pixels), 425 at 640x480, 550 at 800x600. */
    if (fb->w == 320 && fb->h == 200) fb->fx = 200, fb->fy = 164;
    else if (fb->w == 640 && fb->h == 480) fb->fx = fb->fy = 425;
    else if (fb->w == 800 && fb->h == 600) fb->fx = fb->fy = 550;
    else fb->fx = fb->w * (425.0f / 640.0f), fb->fy = fb->fx * fb->h / (fb->w * 0.75f);
}

void ign_frame_clear(IgnFrame *fb) {
    memset(fb->color, 0, (size_t)fb->w * fb->h);
    memset(fb->inv_z, 0, (size_t)fb->w * fb->h * sizeof fb->inv_z[0]);
}

/* World vertex = t + m * (obj.pos + shift + pre * v); pre (may be NULL) turns a part about its own origin. */
static void draw_object(IgnFrame *fb, const IgnModel *mdl, const IgnObject *o,
                        const IgnCamera *cam, const float m[9], const float t[3],
                        const float pre[9], const float shift[3]) {
    float cy = cosf(cam->yaw), sy = sinf(cam->yaw);
    float cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    int npages = mdl->tex_h / 256;
    float lx0 = (float)o->pos[0] + shift[0];
    float ly0 = (float)o->pos[1] + shift[1];
    float lz0 = (float)o->pos[2] + shift[2];

    for (int fi = 0; fi < o->nf; fi++) {
        const IgnFace *f = &o->faces[fi];
        Vert tri[3];
        int behind = 0, far = 0;
        for (int k = 0; k < 3; k++) {
            const int32_t *p = o->verts + f->v[k] * 3;
            float vx = (float)p[0], vy = (float)p[1], vz = (float)p[2];
            if (pre) {
                float rx = pre[0] * vx + pre[1] * vy + pre[2] * vz;
                float ry = pre[3] * vx + pre[4] * vy + pre[5] * vz;
                float rz = pre[6] * vx + pre[7] * vy + pre[8] * vz;
                vx = rx, vy = ry, vz = rz;
            }
            float lx = lx0 + vx, ly = ly0 + vy, lz = lz0 + vz;
            float wx = t[0] + m[0] * lx + m[1] * ly + m[2] * lz - cam->x;
            float wy = t[1] + m[3] * lx + m[4] * ly + m[5] * lz - cam->y;
            float wz = t[2] + m[6] * lx + m[7] * ly + m[8] * lz - cam->z;
            float x = wx * cy - wz * sy;
            float z = wx * sy + wz * cy;
            tri[k].x = x;
            tri[k].y = wy * cp - z * sp;
            tri[k].z = wy * sp + z * cp;
            tri[k].u = f->uv[k * 2] / 256.0f;
            tri[k].v = f->uv[k * 2 + 1] / 256.0f;
            behind += tri[k].z < NEAR_Z;
            far += tri[k].z > FAR_Z;
        }
        if (behind == 3 || far == 3) continue;

        int npg = (int)(f->extra >> 16);
        if (npg >= npages) npg = npages - 1;
        const uint8_t *page = mdl->tex + (size_t)npg * 256 * 256;
        /* Face modes 17 and 21 are opaque; the blended ones (18, 19, 22, 23) leave index 0 see-through. */
        unsigned mode = f->flags & 0xffff;
        int keyed = mode != 17 && mode != 21;

        Vert poly[MAX_POLY];
        int n = clip_near(tri, 3, poly);
        ScreenVert sv[MAX_POLY];
        for (int k = 0; k < n; k++) project(fb, &poly[k], &sv[k]);
        for (int k = 1; k + 1 < n; k++)
            draw_tri(fb, &sv[0], &sv[k], &sv[k + 1], page, keyed, NULL);
    }
}

void ign_render_track(IgnFrame *fb, const IgnLevel *lv, const IgnCamera *cam) {
    static const float zero[3] = {0, 0, 0};
    static const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int i = 0; i < lv->track.nobj; i++) {
        const IgnObject *o = &lv->track.objs[i];
        float rotation[9];
        ign_object_rotation_matrix(o, rotation);
        draw_object(fb, &lv->track, o, cam, id, zero, rotation, zero);
    }
}

void ign_render_object(IgnFrame *fb, const IgnModel *mdl, int obj, const float rotation[9], const IgnCamera *cam) {
    static const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    static const float zero[3] = {0, 0, 0};
    if (obj < 0 || obj >= mdl->nobj) return;
    const IgnObject *o = &mdl->objs[obj];
    const float shift[3] = {(float)-o->pos[0], (float)-o->pos[1], (float)-o->pos[2]};
    draw_object(fb, mdl, o, cam, id, zero, rotation, shift);
}

static void mat_mul(const float a[9], const float b[9], float out[9]) {
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            out[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
}

/* Model frame (x forward, y down, z left) to world: yaw, then pitch (+ = nose down), roll (+ = left side down). */
static void car_matrix(float heading, double pitch, double roll, float out[9]) {
    float yaw = heading + IGN_CAR_MODEL_YAW;
    float ry = cosf(yaw), rs = sinf(yaw);
    float cp = (float)cos(pitch), sp = (float)sin(pitch), cr = (float)cos(roll), sr = (float)sin(roll);
    const float y[9] = {ry, 0, rs, 0, 1, 0, -rs, 0, ry};
    const float p[9] = {cp, -sp, 0, sp, cp, 0, 0, 0, 1};
    const float r[9] = {1, 0, 0, 0, cr, sr, 0, -sr, cr};
    float yp[9];
    mat_mul(y, p, yp);
    mat_mul(yp, r, out);
}

/* Car shadows darken through SHD row 0x28, as the shadow primitive (0x13) of Ign_win.exe does. */
#define SHADOW_SHD_ROW 0x28

static void to_camera(const IgnCamera *cam, float wx, float wy, float wz, Vert *out) {
    float cy = cosf(cam->yaw), sy = sinf(cam->yaw);
    float cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    wx -= cam->x, wy -= cam->y, wz -= cam->z;
    float z = wx * sy + wz * cy;
    out->x = wx * cy - wz * sy;
    out->y = wy * cp - z * sp;
    out->z = wy * sp + z * cp;
    out->u = out->v = 0;
}

/* sub_00048a14 shadow: wheel rectangle (z x1.2) on the ground, offset diagonally by the height above it. */
static void draw_shadow(IgnFrame *fb, const IgnLevel *lv, const IgnCar *car, const IgnCamera *cam,
                        const float m[9]) {
    float qx[4], qz[4];
    int n = 0;
    static const int order[4] = {1, 2, 4, 3};
    for (int k = 0; k < 4; k++)
        for (int p = 0; p < car->nparts; p++)
            if ((int)(lv->cars.objs[car->parts[p]].type % 10) == order[k] && n < 4) {
                qx[n] = car->part_off[p][0];
                qz[n] = car->part_off[p][2] * 1.2f;
                n++;
            }
    if (n != 4) return;
    float d = (float)(car->height - car->ground);
    float ox = car->x + d, oy = (float)-car->ground, oz = car->z + d;
    Vert quad[4], poly[MAX_POLY];
    for (int k = 0; k < 4; k++)
        to_camera(cam, ox + m[0] * qx[k] + m[2] * qz[k], oy + m[3] * qx[k] + m[5] * qz[k],
                  oz + m[6] * qx[k] + m[8] * qz[k], &quad[k]);
    int np = clip_near(quad, 4, poly);
    if (np < 3) return;
    ScreenVert sv[MAX_POLY];
    for (int k = 0; k < np; k++) project(fb, &poly[k], &sv[k]);
    const uint8_t *shade = lv->shd + SHADOW_SHD_ROW * 256;
    for (int k = 1; k + 1 < np; k++) {
        const ScreenVert *a = &sv[0], *b = &sv[k], *c = &sv[k + 1];
        float area = edge(a->sx, a->sy, b->sx, b->sy, c->sx, c->sy);
        if (fabsf(area) < 1e-6f) continue;
        if (area < 0) {
            const ScreenVert *t = b;
            b = c;
            c = t;
            area = -area;
        }
        int x0 = (int)fmaxf(0, floorf(fminf(a->sx, fminf(b->sx, c->sx))));
        int x1 = (int)fminf(fb->w - 1, ceilf(fmaxf(a->sx, fmaxf(b->sx, c->sx))));
        int y0 = (int)fmaxf(0, floorf(fminf(a->sy, fminf(b->sy, c->sy))));
        int y1 = (int)fminf(fb->h - 1, ceilf(fmaxf(a->sy, fmaxf(b->sy, c->sy))));
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = edge(b->sx, b->sy, c->sx, c->sy, px, py) / area;
                float w1 = edge(c->sx, c->sy, a->sx, a->sy, px, py) / area;
                float w2 = edge(a->sx, a->sy, b->sx, b->sy, px, py) / area;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float iz = w0 * a->iz + w1 * b->iz + w2 * c->iz;
                int idx = y * fb->w + x;
                /* Lies on the road: accept small depth differences instead of z-fighting. */
                if (iz < fb->inv_z[idx] * 0.97f) continue;
                fb->color[idx] = shade[fb->color[idx]];
            }
    }
}

/* sub_00048a14: body and four wheels; wheels +1/+2 (front) turn by car+0xb0 degrees about their own axis. */
void ign_render_car(IgnFrame *fb, const IgnLevel *lv, const IgnCar *car, const IgnCamera *cam) {
    float t[3] = {car->x, car->y, car->z};
    float body[9], wheels[9];
    car_matrix(car->heading, car->body_pitch, car->body_roll, body);
    car_matrix(car->heading, car->body.pitch, car->body.roll, wheels);
    draw_shadow(fb, lv, car, cam, wheels);
    const float stretch[9] = {1, 0, 0, 0, car->body_stretch, 0, 0, 0, 1};
    float a = (float)(car->steer_visual * (3.14159265358979 / 180.0));
    float ca = cosf(a), sa = sinf(a);
    const float steer[9] = {ca, 0, -sa, 0, 1, 0, sa, 0, ca};
    for (int p = 0; p < car->nparts; p++) {
        const IgnObject *o = &lv->cars.objs[car->parts[p]];
        int part = (int)(o->type % 10);
        float shift[3] = {car->part_off[p][0] - (float)o->pos[0],
                          car->part_off[p][1] * (part ? 1.0f : car->body_stretch) - (float)o->pos[1],
                          car->part_off[p][2] - (float)o->pos[2]};
        draw_object(fb, &lv->cars, o, cam, part ? wheels : body, t,
                    part == 1 || part == 2 ? steer : part ? NULL : stretch, shift);
    }
}

/* Flat polygon lying on the road (accepts small depth differences, like the shadow), darkened through `shade`. */
static void fill_ground_poly(IgnFrame *fb, const IgnCamera *cam, const float *x, float y, const float *z, int n,
                             const uint8_t *shade) {
    Vert quad[MAX_POLY], poly[MAX_POLY];
    for (int k = 0; k < n; k++) to_camera(cam, x[k], y, z[k], &quad[k]);
    int np = clip_near(quad, n, poly);
    if (np < 3) return;
    ScreenVert sv[MAX_POLY];
    for (int k = 0; k < np; k++) project(fb, &poly[k], &sv[k]);
    for (int k = 1; k + 1 < np; k++) {
        const ScreenVert *a = &sv[0], *b = &sv[k], *c = &sv[k + 1];
        float area = edge(a->sx, a->sy, b->sx, b->sy, c->sx, c->sy);
        if (fabsf(area) < 1e-6f) continue;
        if (area < 0) {
            const ScreenVert *t = b;
            b = c;
            c = t;
            area = -area;
        }
        int x0 = (int)fmaxf(0, floorf(fminf(a->sx, fminf(b->sx, c->sx))));
        int x1 = (int)fminf(fb->w - 1, ceilf(fmaxf(a->sx, fmaxf(b->sx, c->sx))));
        int y0 = (int)fmaxf(0, floorf(fminf(a->sy, fminf(b->sy, c->sy))));
        int y1 = (int)fminf(fb->h - 1, ceilf(fmaxf(a->sy, fmaxf(b->sy, c->sy))));
        for (int yy = y0; yy <= y1; yy++)
            for (int xx = x0; xx <= x1; xx++) {
                float px = xx + 0.5f, py = yy + 0.5f;
                float w0 = edge(b->sx, b->sy, c->sx, c->sy, px, py) / area;
                float w1 = edge(c->sx, c->sy, a->sx, a->sy, px, py) / area;
                float w2 = edge(a->sx, a->sy, b->sx, b->sy, px, py) / area;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float iz = w0 * a->iz + w1 * b->iz + w2 * c->iz;
                int idx = yy * fb->w + xx;
                if (iz < fb->inv_z[idx] * 0.97f) continue;
                fb->color[idx] = shade[fb->color[idx]];
            }
    }
}

void ign_render_skids(IgnFrame *fb, const IgnLevel *lv, const IgnSkids *s, const IgnCamera *cam) {
    /* The mark face value (0xc34e0) is read as an SHD row: 38/42/92 darken the road, 0 leaves it as it is. */
    for (int i = 0; i < s->count; i++) {
        const IgnSkidMark *m = &s->marks[i];
        if (!m->color) continue;
        for (int k = 0; k < 2; k++) fill_ground_poly(fb, cam, m->x[k], m->y, m->z[k], 4, lv->shd + m->color * 256);
    }
}

#define FX_PI 3.14159265358979323846

/* Engine object angles (roll, heading, pitch in tenths of a degree, heading as car+0xf8) to a car_matrix. */
static void fx_matrix(double roll, double heading, double pitch, float m[9]) {
    const double k = FX_PI / 1800.0;
    car_matrix((float)(FX_PI / 2 - heading * k), pitch * k, roll * k, m);
}

/* World point (y down) of local vertex v of an object at fixed-point position p (y up). */
static void fx_point(const int32_t p[3], const float m[9], float vx, float vy, float vz, float out[3]) {
    out[0] = p[0] / 1024 + m[0] * vx + m[1] * vy + m[2] * vz;
    out[1] = -(float)(p[1] / 1024) + m[3] * vx + m[4] * vy + m[5] * vz;
    out[2] = p[2] / 1024 + m[6] * vx + m[7] * vy + m[8] * vz;
}

typedef struct {
    float z, sx, sy, size;
    const int *uv;
    const uint8_t *tex;
} FxSprite;

static int sprite_far_first(const void *a, const void *b) {
    float za = ((const FxSprite *)a)->z, zb = ((const FxSprite *)b)->z;
    return za < zb ? 1 : za > zb ? -1 : 0;
}

/* Point faces: type 1 (sub_0005ba5c) writes the shade ramp TAB[colour * 256 + light 0], type 2 (sub_0005ee50)
 * blends SHD[colour * 256 + dst]. One pixel, rejected at z <= 200, depth z as a triangle would get it. */
static void fx_plot(IgnFrame *fb, const IgnLevel *lv, const IgnCamera *cam, const float w[3], int mode,
                    int32_t colour) {
    Vert v;
    to_camera(cam, w[0], w[1], w[2], &v);
    if (v.z <= 200.0f) return;
    ScreenVert sv;
    project(fb, &v, &sv);
    int x = (int)floorf(sv.sx), y = (int)floorf(sv.sy);
    if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) return;
    int idx = y * fb->w + x, c = (colour / 1024) & 255;
    if (1.0f / v.z < fb->inv_z[idx]) return;
    if (mode == 1) fb->color[idx] = lv->shd[c * 256 + fb->color[idx]];
    else if (lv->tab) fb->color[idx] = lv->tab[c * 256];
}

static void queue_sprite(const IgnFrame *fb, const IgnCamera *cam, const float w[3], float size, const int *uv,
                         const uint8_t *tex, FxSprite *out, int *n) {
    Vert v;
    to_camera(cam, w[0], w[1], w[2], &v);
    if (v.z <= 200.0f || !tex) return;
    ScreenVert sv;
    project(fb, &v, &sv);
    out[*n] = (FxSprite){v.z, sv.sx, sv.sy, size, uv, tex};
    (*n)++;
}

/* Sprite emitter sub_0005dea0: an axis-aligned frame centred on the projected point, scaled by 4/z * size *
 * (W/320, H/200) per texel ([0x1feee8]/[0x1feeec]), blended through the level SHD ([0x24bfa8]):
 * dst = SHD[texel * 256 + dst], texel 0 leaves it unchanged. The original sorts it by z * 2.8 against
 * (z0 + z1 + z2) for triangles, so it is tested here as if 2.8/3 of its depth. */
static void draw_sprite(IgnFrame *fb, const IgnLevel *lv, const FxSprite *s) {
    const int *uv = s->uv;
    float iz = 3.0f / (2.8f * s->z);
    float mx = 4.0f / s->z * s->size * (fb->w / 320.0f), my = 4.0f / s->z * s->size * (fb->h / 200.0f);
    float hw = (uv[2] - uv[0]) * 0.5f * mx, hh = (uv[3] - uv[1]) * 0.5f * my;
    int x0 = (int)fmaxf(0, floorf(s->sx - hw)), x1 = (int)fminf(fb->w - 1, ceilf(s->sx + hw));
    int y0 = (int)fmaxf(0, floorf(s->sy - hh)), y1 = (int)fminf(fb->h - 1, ceilf(s->sy + hh));
    for (int y = y0; y <= y1; y++) {
        int tv = uv[1] + (int)((y + 0.5f - (s->sy - hh)) / my);
        if (tv < uv[1] || tv >= uv[3]) continue;
        for (int x = x0; x <= x1; x++) {
            int tu = uv[0] + (int)((x + 0.5f - (s->sx - hw)) / mx);
            if (tu < uv[0] || tu >= uv[2]) continue;
            uint8_t texel = s->tex[(tv & 255) * 256 + (tu & 255)];
            int idx = y * fb->w + x;
            if (!texel || iz < fb->inv_z[idx]) continue;
            fb->color[idx] = lv->shd[texel * 256 + fb->color[idx]];
        }
    }
}

/* Clipped, projected polygon of world points (y down) with texel uvs; `page` NULL fills with `color`; `lut`
 * blends the texels (see draw_tri). */
static void fx_poly(IgnFrame *fb, const IgnCamera *cam, const float (*w)[3], const float (*uv)[2], int n,
                    const uint8_t *page, uint8_t color, const uint8_t *lut) {
    static uint8_t solid[256 * 256];
    Vert in[4], poly[MAX_POLY];
    for (int k = 0; k < n; k++) {
        to_camera(cam, w[k][0], w[k][1], w[k][2], &in[k]);
        in[k].u = uv ? uv[k][0] : 0;
        in[k].v = uv ? uv[k][1] : 0;
    }
    int np = clip_near(in, n, poly);
    if (np < 3) return;
    ScreenVert sv[MAX_POLY];
    for (int k = 0; k < np; k++) project(fb, &poly[k], &sv[k]);
    if (!page) {
        solid[0] = color;
        page = solid;
    }
    for (int k = 1; k + 1 < np; k++) draw_tri(fb, &sv[0], &sv[k], &sv[k + 1], page, uv != NULL, lut);
}

void ign_render_fx(IgnFrame *fb, const IgnLevel *lv, const IgnFx *fx, IgnCar *const *cars, int ncars,
                   const IgnCamera *cam) {
    static FxSprite sprites[IGN_FX_SLOTS];
    int ns = 0;
    for (int i = 0; i < IGN_FX_SLOTS; i++) {
        const int32_t *r = fx->r[i];
        float m[9], w[4][3];
        switch (r[0]) {
        case IGN_FX_SPRITE: /* sub_00039d1c: vertex +0x3c in the frame of angles +0x48 (roll, heading, pitch) */
            fx_matrix(r[18], r[19], r[20], m);
            fx_point(r + 1, m, (float)r[15], (float)r[16], (float)r[17], w[0]);
            queue_sprite(fb, cam, w[0], (float)(r[12] / 1024), ign_fx_uv(r[14], r[10] / 1024),
                         lv->fx_tex[r[9] & 7], sprites, &ns);
            break;
        case IGN_FX_LIGHTNING: { /* sub_0003bdac: mesh 0xbfe90, 20 flat faces (0x0f) of colour 251 */
            static const int16_t V[13][3] = {{10, -920, 11}, {1, -187, 5},    {21, -212, 3},  {9, -172, 4},
                                             {-9, -160, 6},  {-14, -921, 11}, {10, -920, -8}, {1, -187, -3},
                                             {21, -212, -2}, {0, -1, 0},      {9, -172, -3},  {-9, -160, -5},
                                             {-14, -921, -8}};
            static const uint8_t F[20][3] = {{3, 9, 2},  {3, 2, 1},   {4, 3, 1},  {5, 4, 1},  {5, 1, 0},
                                             {0, 1, 7},  {0, 7, 6},   {1, 2, 8},  {1, 8, 7},  {2, 9, 8},
                                             {9, 3, 10}, {3, 4, 11},  {3, 11, 10}, {4, 5, 12}, {4, 12, 11},
                                             {8, 9, 10}, {7, 8, 10},  {7, 10, 11}, {7, 11, 12}, {6, 7, 12}};
            /* The two type-0x14 faces of the mesh (a ground quad of SHD row 0) leave the screen as it is. */
            fx_matrix(r[7], r[8], 0, m);
            for (int f = 0; f < 20; f++) {
                for (int k = 0; k < 3; k++) {
                    const int16_t *p = V[F[f][k]];
                    fx_point(r + 1, m, p[0], p[1], p[2], w[k]);
                }
                fx_poly(fb, cam, (const float (*)[3])w, NULL, 3, NULL, 251, NULL);
            }
            break;
        }
        case IGN_FX_PLOT: { /* sub_0003a2b4: one point of face type mode + 1 (mode clamped to 0..1) */
            static const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
            fx_point(r + 1, id, 0, 0, 0, w[0]);
            fx_plot(fb, lv, cam, w[0], r[11] == 1, r[9]);
            break;
        }
        case IGN_FX_SUPERPLOT: { /* sub_0003a498: points (+-10, 0, +-10) turned by angles +0x30 */
            static const float P[4][2] = {{10, 10}, {-10, 10}, {10, -10}, {-10, -10}};
            fx_matrix(r[12] / 1024.0, r[13] / 1024.0, r[14] / 1024.0, m);
            for (int k = 0; k < 4; k++) {
                fx_point(r + 1, m, P[k][0], 0, P[k][1], w[0]);
                fx_plot(fb, lv, cam, w[0], r[11] == 1, r[9]);
            }
            break;
        }
        case IGN_FX_BLAST: { /* sub_00039fe8 */
            static const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
            fx_point(r + 1, id, 0, 0, 0, w[0]);
            queue_sprite(fb, cam, w[0], (float)(r[12] / 1024), ign_fx_uv(r[14], r[10] / 1024),
                         lv->fx_tex[r[9] & 7], sprites, &ns);
            break;
        }
        case IGN_FX_SPARKLE: { /* sub_0003e6ec: DIVSPR (33, 84)-(66, 117) at scale 150, orbiting the car */
            static const int uv[4] = {33, 84, 66, 117};
            if (r[1] < 0 || r[1] >= ncars) break;
            const IgnCar *c = cars[r[1]];
            const int32_t at[3] = {(int32_t)(c->x * 1024.0f), (int32_t)(c->height * 1024.0), (int32_t)(c->z * 1024.0f)};
            int radius = 250 - 8 * r[8];
            if (radius < 50) radius = 50;
            fx_matrix(r[2] * 10.0 / 1024.0, r[3] * 10.0 / 1024.0, r[4] * 10.0 / 1024.0, m);
            fx_point(at, m, (float)radius, 0, 0, w[0]);
            queue_sprite(fb, cam, w[0], 150.0f, uv, lv->fx_tex[2], sprites, &ns);
            break;
        }
        case IGN_FX_PIECE: { /* sub_0003b744: two flat faces (0x0f) of colour +0x3c, both windings */
            const int32_t *v = fx->piece[i];
            fx_matrix(r[9] / 1024.0, r[10] / 1024.0, r[11] / 1024.0, m);
            for (int k = 0; k < 3; k++) fx_point(r + 1, m, (float)v[k * 3], (float)v[k * 3 + 1], (float)v[k * 3 + 2], w[k]);
            fx_poly(fb, cam, (const float (*)[3])w, NULL, 3, NULL, (uint8_t)r[15], NULL);
            break;
        }
        case IGN_FX_SPLASH: { /* sub_0003d808: quad (0, 1, 2) + (0, 2, 3) of mode 0x13 (primitive 19, sub_0005d6e0:
                                 textured through the SHD), frame of table +0x34 */
            static const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
            const int *e = ign_fx_uv(r[13], r[18] / 1024);
            const float uv[4][2] = {{(float)e[0], (float)e[3]}, {(float)e[0], (float)e[1]},
                                    {(float)e[2], (float)e[1]}, {(float)e[2], (float)e[3]}};
            for (int k = 0; k < 4; k++) fx_point(r + 15, id, (float)r[1 + k * 3], (float)r[2 + k * 3], (float)r[3 + k * 3], w[k]);
            if (lv->fx_tex[r[14] & 7]) fx_poly(fb, cam, (const float (*)[3])w, uv, 4, lv->fx_tex[r[14] & 7], 0, lv->shd);
            break;
        }
        default: break;
        }
    }
    qsort(sprites, (size_t)ns, sizeof sprites[0], sprite_far_first);
    for (int i = 0; i < ns; i++) draw_sprite(fb, lv, &sprites[i]);
}

/* Face type 0x0d (sub_0005bb20, primitive 13 at 0x71cc7): a one-pixel line whose colour runs 0x77 -> 0x7b and is
 * used as an SHD row, dst = SHD[row * 256 + dst]; rejected when z0 + z1 <= 400. */
static void rain_line(IgnFrame *fb, const IgnLevel *lv, const IgnCamera *cam, const float a[3], const float b[3]) {
    Vert va, vb;
    to_camera(cam, a[0], a[1], a[2], &va);
    to_camera(cam, b[0], b[1], b[2], &vb);
    if (va.z + vb.z <= 400.0f || va.z <= 1.0f || vb.z <= 1.0f) return;
    ScreenVert sa, sb;
    project(fb, &va, &sa);
    project(fb, &vb, &sb);
    float dx = sb.sx - sa.sx, dy = sb.sy - sa.sy;
    int n = (int)fmaxf(fabsf(dx), fabsf(dy)) + 1;
    if (n > 4 * (fb->w + fb->h)) return;
    for (int i = 0; i < n; i++) {
        float t = n > 1 ? (float)i / (float)(n - 1) : 0.0f;
        int x = (int)(sa.sx + dx * t), y = (int)(sa.sy + dy * t);
        if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) continue;
        int idx = y * fb->w + x;
        if (sa.iz + (sb.iz - sa.iz) * t < fb->inv_z[idx]) continue;
        int row = 0x77 + (int)(4.0f * t);
        fb->color[idx] = lv->shd[row * 256 + fb->color[idx]];
    }
}

void ign_render_weather(IgnFrame *fb, const IgnLevel *lv, const IgnWeather *w, const IgnCamera *cam) {
    static FxSprite flakes[9 * IGN_WEATHER_VERTS];
    int ns = 0;
    if (w->nverts <= 0 || !lv->shd) return;
    /* sub_000403c8 0x40b26: the same mesh in 3 x 3 tiles of 1000; mesh y down from the tile height. */
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++) {
            float ox = (float)(w->tile_x + col * 1000), oz = (float)(w->tile_z + row * 1000), oy = (float)w->tile_y;
            float p[IGN_WEATHER_VERTS][3];
            for (int k = 0; k < w->nverts; k++)
                p[k][0] = ox + w->v[k][0], p[k][1] = w->v[k][1] - oy, p[k][2] = oz + w->v[k][2];
            if (w->kind == IGN_WEATHER_RAIN) {
                for (int k = 0; k + 1 < w->nverts; k += 2) rain_line(fb, lv, cam, p[k], p[k + 1]);
            } else if (w->kind == IGN_WEATHER_SNOW && lv->fx_tex[2]) {
                /* Sprite faces (type 7) of u 0-31, v 117-148 (8.8) on the object's texture slot 2, DIVSPR; scale 100. */
                static const int uv[4] = {0, 117, 31, 148};
                for (int k = 0; k < w->nverts; k++) queue_sprite(fb, cam, p[k], 100.0f, uv, lv->fx_tex[2], flakes, &ns);
            }
        }
    qsort(flakes, (size_t)ns, sizeof flakes[0], sprite_far_first);
    for (int i = 0; i < ns; i++) draw_sprite(fb, lv, &flakes[i]);
}
