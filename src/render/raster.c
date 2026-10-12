#include "raster.h"

#include <math.h>
#include <string.h>

#define NEAR_Z 8.0f
#define FAR_Z 30000.0f
#define FOCAL 160.0f /* 90 degree horizontal FOV at 320 px */
#define MAX_POLY 8
/* CARS models face +x; rotate so heading 0 drives along +z. */
#define IGN_CAR_MODEL_YAW (-1.5707963f)

typedef struct {
    float x, y, z, u, v;
} Vert;

typedef struct {
    float sx, sy, iz, uz, vz;
} ScreenVert;

static float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static void draw_tri(IgnFrame *fb, const ScreenVert *a, const ScreenVert *b, const ScreenVert *c,
                     const uint8_t *page) {
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
    if (x1 > IGN_SCREEN_W - 1) x1 = IGN_SCREEN_W - 1;
    if (y1 > IGN_SCREEN_H - 1) y1 = IGN_SCREEN_H - 1;
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
            int idx = y * IGN_SCREEN_W + x;
            if (iz <= fb->inv_z[idx]) continue;
            float z = 1.0f / iz;
            int u = (int)((w0 * a->uz + w1 * b->uz + w2 * c->uz) * z);
            int v = (int)((w0 * a->vz + w1 * b->vz + w2 * c->vz) * z);
            uint8_t texel = page[(v & 255) * 256 + (u & 255)];
            /* Index 0 is the transparent colour key. */
            if (texel == 0) continue;
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

void ign_frame_clear(IgnFrame *fb) {
    memset(fb->color, 0, sizeof fb->color);
    memset(fb->inv_z, 0, sizeof fb->inv_z);
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

        Vert poly[MAX_POLY];
        int n = clip_near(tri, 3, poly);
        ScreenVert sv[MAX_POLY];
        for (int k = 0; k < n; k++) {
            float iz = 1.0f / poly[k].z;
            sv[k].sx = IGN_SCREEN_W / 2.0f + poly[k].x * FOCAL * iz;
            sv[k].sy = IGN_SCREEN_H / 2.0f + poly[k].y * FOCAL * iz;
            sv[k].iz = iz;
            sv[k].uz = poly[k].u * iz;
            sv[k].vz = poly[k].v * iz;
        }
        for (int k = 1; k + 1 < n; k++)
            draw_tri(fb, &sv[0], &sv[k], &sv[k + 1], page);
    }
}

void ign_render_track(IgnFrame *fb, const IgnLevel *lv, const IgnCamera *cam) {
    static const float zero[3] = {0, 0, 0};
    static const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int i = 0; i < lv->track.nobj; i++)
        draw_object(fb, &lv->track, &lv->track.objs[i], cam, id, zero, NULL, zero);
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

/* Palette index of each colour at half brightness (stand-in for the original TAB shading). */
static const uint8_t *shade_table(const IgnPalette *pal) {
    static uint8_t table[256];
    static const IgnPalette *cached;
    if (cached == pal) return table;
    for (int i = 0; i < 256; i++) {
        int r = pal->rgb[i][0] / 2, g = pal->rgb[i][1] / 2, b = pal->rgb[i][2] / 2, best = 1 << 30;
        for (int j = 1; j < 256; j++) {
            int dr = pal->rgb[j][0] - r, dg = pal->rgb[j][1] - g, db = pal->rgb[j][2] - b;
            int d = dr * dr + dg * dg + db * db;
            if (d < best) best = d, table[i] = (uint8_t)j;
        }
    }
    cached = pal;
    return table;
}

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
    for (int k = 0; k < np; k++) {
        float iz = 1.0f / poly[k].z;
        sv[k].sx = IGN_SCREEN_W / 2.0f + poly[k].x * FOCAL * iz;
        sv[k].sy = IGN_SCREEN_H / 2.0f + poly[k].y * FOCAL * iz;
        sv[k].iz = iz;
    }
    const uint8_t *shade = shade_table(&lv->palette);
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
        int x1 = (int)fminf(IGN_SCREEN_W - 1, ceilf(fmaxf(a->sx, fmaxf(b->sx, c->sx))));
        int y0 = (int)fmaxf(0, floorf(fminf(a->sy, fminf(b->sy, c->sy))));
        int y1 = (int)fminf(IGN_SCREEN_H - 1, ceilf(fmaxf(a->sy, fmaxf(b->sy, c->sy))));
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = edge(b->sx, b->sy, c->sx, c->sy, px, py) / area;
                float w1 = edge(c->sx, c->sy, a->sx, a->sy, px, py) / area;
                float w2 = edge(a->sx, a->sy, b->sx, b->sy, px, py) / area;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float iz = w0 * a->iz + w1 * b->iz + w2 * c->iz;
                int idx = y * IGN_SCREEN_W + x;
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
