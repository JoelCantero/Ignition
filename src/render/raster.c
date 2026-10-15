#include "raster.h"

#include "lisa.h"

#include <math.h>
#include <stdio.h>
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

/* ---- Lisa 2 engine scene of the level (sub_0002489c) ---- */

static LisaScene scene;
static const IgnLevel *scene_level;
static char level_tex_path[1024];
static uint8_t *level_tex; /* the TEX on whole 64 KB pages, as the loader allocates it (filelength + 0xffff) */
static int level_tex_pages;

void ign_render_set_level_dir(const char *data_dir, const char *name) {
    snprintf(level_tex_path, sizeof level_tex_path, "%s/LEVELS/%s/%s.TEX", data_dir, name, name);
}

/* JAPAN's TEX ends 3/4 into its 16th page and ICELAND's a quarter into its 17th; faces use those partial pages. */
static void load_level_texture(const IgnModel *m) {
    free(level_tex);
    level_tex = NULL;
    size_t size = 0;
    uint8_t *data = level_tex_path[0] ? ign_read_file(level_tex_path, &size) : NULL;
    if (data && size >= (size_t)m->tex_h * 256) {
        level_tex_pages = (int)((size + 0xffff) >> 16);
        level_tex = calloc((size_t)level_tex_pages << 16, 1);
        if (level_tex) memcpy(level_tex, data, size);
    }
    free(data);
    if (!level_tex) level_tex_pages = m->tex_h / 256;
}
#define MAX_ENGINE_CARS 16

typedef struct {
    const IgnCar *car;
    int body, wheel[4], shadow;
    int32_t *mesh_body, *mesh_wheel[4], mesh_shadow[2 + 4 * 3 + 2 * 5];
    const int32_t *src_wheel[4];
    int32_t off_wheel[4][3];
    int seen;
} EngineCar;

static EngineCar engine_cars[MAX_ENGINE_CARS];
static int n_engine_cars;

static int n_engine_skids;
static int *scene_ids; /* PLC index -> engine object */
static LisaFrame lisa_frame;

static void engine_pos(const IgnObject *o, int32_t *x, int32_t *y, int32_t *z, int16_t ang[3]) {
    *x = o->pos[0] + IGN_SRF_ORIGIN, *y = -o->pos[1], *z = o->pos[2] + IGN_SRF_ORIGIN;
    for (int k = 0; k < 3; k++) ang[k] = (int16_t)o->rotation[k];
}

/* The loader creates the PLC objects of class < 100 first, then the rest, every one with texture slot 0, the
 * class as priority (+0x1e), the computed radius and the zone of type bits 12-15; all detail options on. */
static int build_scene(const IgnLevel *lv) {
    if (scene_level == lv) return 0;
    if (scene_level) {
        for (int i = 0; i < n_engine_cars; i++) {
            free(engine_cars[i].mesh_body);
            for (int w = 0; w < 4; w++) free(engine_cars[i].mesh_wheel[w]);
        }
        n_engine_cars = 0;
        n_engine_skids = 0;
        lisa_scene_free(&scene), free(scene_ids), scene_level = NULL;
    }
    const IgnModel *m = &lv->track;
    if (lisa_scene_init(&scene, 0xc800, 0xc800, m->nobj + 2048)) return -1;
    scene.shade = lv->shd;
    load_level_texture(m);
    const uint8_t *tex = level_tex ? level_tex : m->tex;
    scene_ids = malloc(sizeof *scene_ids * (size_t)(m->nobj ? m->nobj : 1));
    if (!scene_ids) return -1;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < m->nobj; i++) {
            const IgnObject *o = &m->objs[i];
            int klass = (int)(o->type & 0xfff);
            if ((klass < 100) != (pass == 0)) continue;
            int id = lisa_create(&scene, o->verts - 2, tex, level_tex_pages, klass, (int)((o->type >> 12) & 0xf));
            scene_ids[i] = id;
            int32_t x, y, z;
            int16_t ang[3];
            engine_pos(o, &x, &y, &z, ang);
            if (id >= 0) lisa_place(&scene, id, x, y, z, ang);
        }
    scene_level = lv;
    return 0;
}

/* POS animation moves objects with LI_MOVEOBJECT. */
static void sync_scene(const IgnLevel *lv) {
    for (int i = 0; i < lv->track.nobj; i++) {
        int id = scene_ids[i];
        if (id < 0) continue;
        const LisaObject *e = &scene.objs[id];
        int32_t x, y, z;
        int16_t ang[3];
        engine_pos(&lv->track.objs[i], &x, &y, &z, ang);
        if (x != e->x || y != e->y || z != e->z || ang[0] != e->ang[0] || ang[1] != e->ang[1] || ang[2] != e->ang[2])
            lisa_move(&scene, id, x, y, z, ang);
    }
}

/* race_frame 0x42c92 / race_setup_video: config block of a full-screen view. */
static void engine_view(const IgnFrame *fb, const IgnCamera *cam, LisaView *v) {
    const double tenths = 1800.0 / 3.14159265358979323846;
    memset(v, 0, sizeof *v);
    v->x = cam->x + (double)IGN_SRF_ORIGIN, v->y = -(double)cam->y, v->z = cam->z + (double)IGN_SRF_ORIGIN;
    v->pitch = cam->pitch * tenths, v->yaw = cam->yaw * tenths, v->zoom = 1.0;
    v->fx = (int)fb->fx, v->fy = (int)fb->fy;
    v->near = fb->w == 320 && fb->h == 200 ? 25 : 10;
    v->cx = fb->w / 2, v->cy = fb->h / 2;
    v->layers = LISA_LAYER_ALL;
    v->persp = 1;
    lisa_view_clip(v, 0, 0, fb->w - 1, fb->h - 1);
}

const LisaFrame *ign_render_last_frame(void) { return &lisa_frame; }

/* ---- the frame: everything goes into one painter's list, drawn by ign_render_flush ---- */

static struct {
    int active, track;
    const IgnLevel *lv;
    IgnCamera cam;
} pending;

static void begin_frame(const IgnLevel *lv, const IgnCamera *cam) {
    if (pending.active && pending.lv == lv) return;
    pending.active = 1, pending.track = 0, pending.lv = lv, pending.cam = *cam;
}

void ign_render_track(IgnFrame *fb, const IgnLevel *lv, const IgnCamera *cam) {
    (void)fb;
    if (build_scene(lv)) return;
    sync_scene(lv);
    begin_frame(lv, cam);
    pending.track = 1;
}

/* ---- cars: sub_0002489c creates per car a body (class 100 + car), four wheels (class 210) and a shadow
 * (class 200, two type-20 faces of SHD row 0x28); car_place_objects (sub_00048a14) moves them every frame. ---- */


static int32_t chop_d(double v) { return (int32_t)v; }
/* car_place_objects: radians to tenths, `fld; fmul 180; fmul 1/pi; fmul 10; __CHP; fistp`. */
static int16_t tenths(double rad) { return (int16_t)chop_d(rad * 180.0 * 0.31830988618379 * 10.0); }

static int32_t *copy_mesh(const IgnObject *o) {
    size_t words = 2 + (size_t)o->nv * 3 + (size_t)o->nf * 11;
    int32_t *m = malloc(words * sizeof *m);
    if (m) memcpy(m, o->verts - 2, words * sizeof *m);
    return m;
}

static EngineCar *engine_car(const IgnLevel *lv, const IgnCar *car) {
    for (int i = 0; i < n_engine_cars; i++)
        if (engine_cars[i].car == car) return &engine_cars[i];
    if (n_engine_cars >= MAX_ENGINE_CARS) return NULL;
    EngineCar *e = &engine_cars[n_engine_cars];
    memset(e, 0, sizeof *e);
    e->car = car;
    e->body = e->shadow = -1;
    for (int w = 0; w < 4; w++) e->wheel[w] = -1;
    const IgnModel *cm = &lv->cars;
    static const int16_t zero[3] = {0, 0, 0};
    int32_t px = chop_d(car->x + (double)IGN_SRF_ORIGIN), py = chop_d(car->height), pz = chop_d(car->z + (double)IGN_SRF_ORIGIN);
    for (int p = 0; p < car->nparts; p++) {
        const IgnObject *o = &cm->objs[car->parts[p]];
        int part = (int)(o->type % 10);
        int32_t *m = copy_mesh(o);
        if (!m) continue;
        /* cars_init: part meshes carry their offset in the car frame; the body is stretched by BANARNE */
        int32_t *v = m + 2;
        for (int k = 0; k < m[0]; k++, v += 3) {
            if (part == 0) {
                v[0] = chop_d(car->part_off[p][0] + v[0]);
                v[1] = chop_d(car->part_off[p][1] * car->body_stretch + v[1] * car->body_stretch);
                v[2] = chop_d(car->part_off[p][2] + v[2]);
            } else {
                v[0] = chop_d(car->part_off[p][0] + v[0]);
                v[1] = chop_d(car->part_off[p][1] + v[1]);
                v[2] = chop_d(car->part_off[p][2] + v[2]);
            }
        }
        if (part == 0) {
            e->mesh_body = m;
            e->body = lisa_create(&scene, m, cm->tex, cm->tex_h / 256, 100 + n_engine_cars, 0);
            if (e->body >= 0) lisa_place(&scene, e->body, px, py, pz, zero);
        } else if (part <= 4) {
            int w = part - 1;
            e->mesh_wheel[w] = m;
            e->src_wheel[w] = o->verts;
            for (int k = 0; k < 3; k++) e->off_wheel[w][k] = chop_d(car->part_off[p][k]);
        }
    }
    for (int w = 0; w < 4; w++)
        if (e->mesh_wheel[w]) {
            e->wheel[w] = lisa_create(&scene, e->mesh_wheel[w], cm->tex, cm->tex_h / 256, 210, 0);
            if (e->wheel[w] >= 0) lisa_place(&scene, e->wheel[w], px, py, pz, zero);
        }
    /* 0x27ca9: four vertices on the ground, faces (0, 1, 3) and (0, 3, 2) */
    int32_t *sm = e->mesh_shadow;
    memset(sm, 0, sizeof e->mesh_shadow);
    sm[0] = 4, sm[1] = 2;
    const int32_t faces[2][5] = {{20, 0, 1, 3, 0x28}, {20, 0, 3, 2, 0x28}};
    memcpy(sm + 14, faces, sizeof faces);
    e->shadow = lisa_create(&scene, sm, NULL, 0, 200, 0);
    if (e->shadow >= 0) lisa_place(&scene, e->shadow, px, py, pz, zero);
    n_engine_cars++;
    return e;
}

/* car_place_objects (sub_00048a14) */
static void place_car(EngineCar *e) {
    const IgnCar *car = e->car;
    double cx = car->x + (double)IGN_SRF_ORIGIN, cz = car->z + (double)IGN_SRF_ORIGIN;
    int32_t px = chop_d(cx), py = chop_d(car->height), pz = chop_d(cz);
    int16_t heading = tenths(car->body.heading);
    const int16_t body_ang[3] = {tenths(car->body_roll), heading, tenths(car->body_pitch)};
    if (e->body >= 0) lisa_move(&scene, e->body, px, py, pz, body_ang);
    /* the front wheels (0, 1) are turned in their mesh copies by car+0xb0 degrees */
    double a = car->steer_visual * 3.1415926535898 * 0.005555555555555555, ca = cos(a), sa = sin(a);
    for (int w = 0; w < 2; w++) {
        if (!e->mesh_wheel[w]) continue;
        const int32_t *src = e->src_wheel[w];
        int32_t *dst = e->mesh_wheel[w] + 2;
        for (int k = 0; k < e->mesh_wheel[w][0]; k++, src += 3, dst += 3) {
            dst[0] = chop_d(src[0] * ca - src[2] * sa + e->off_wheel[w][0]);
            dst[2] = chop_d(src[0] * sa + src[2] * ca + e->off_wheel[w][2]);
        }
    }
    const int16_t wheel_ang[3] = {tenths(car->body.roll), heading, tenths(car->body.pitch)};
    for (int w = 0; w < 4; w++)
        if (e->wheel[w] >= 0) lisa_move(&scene, e->wheel[w], px, py, pz, wheel_ang);
    /* 0x48faa: shadow offset by the height above the ground (light at 45 degrees), sent far away when out of
     * range; corners from car+0x2fc/+0x31c with z x 1.2 */
    double lift = car->height - car->ground, sx = cx + lift, sz = cz + lift;
    if (0 > sx || sx > 50000.0) sx = 28000.0;
    if (car->ground < -2500.0 || car->ground > 2500.0) sx = 0;
    if (0 > sz || sz > 50000.0) sz = 28000.0;
    static const int corner[4] = {2, 3, 1, 0};
    for (int k = 0; k < 4; k++) {
        e->mesh_shadow[2 + k * 3] = chop_d(car->corner_x[corner[k]]);
        e->mesh_shadow[2 + k * 3 + 2] = chop_d(1.2 * car->corner_z[corner[k]]);
    }
    if (e->shadow >= 0) lisa_move(&scene, e->shadow, chop_d(sx), chop_d(car->ground), chop_d(sz), wheel_ang);
}

void ign_render_car(IgnFrame *fb, const IgnLevel *lv, const IgnCar *car, const IgnCamera *cam) {
    (void)fb;
    if (build_scene(lv)) return;
    begin_frame(lv, cam);
    EngineCar *e = engine_car(lv, car);
    if (!e) return;
    place_car(e);
    e->seen = 1;
}

static void set_alive(int id, int alive) {
    if (id >= 0) scene.objs[id].alive = alive;
}

void ign_render_flush(IgnFrame *fb) {
    if (!pending.active) return;
    pending.active = 0;
    /* objects not drawn this frame are hidden (LI_HIDEOBJECT); without a track pass only cars show */
    for (int i = 0; i < n_engine_cars; i++) {
        EngineCar *e = &engine_cars[i];
        set_alive(e->body, e->seen), set_alive(e->shadow, e->seen);
        for (int w = 0; w < 4; w++) set_alive(e->wheel[w], e->seen);
        e->seen = 0;
    }
    const IgnModel *m = &pending.lv->track;
    for (int i = 0; i < m->nobj; i++) set_alive(scene_ids[i], pending.track);
    LisaView v;
    engine_view(fb, &pending.cam, &v);
    lisa_build(&scene, &v, &lisa_frame);
    IgnRaster r = {.fb = fb->color, .stride = fb->w};
    ign_raster_clip(&r, 0, 0, fb->w - 1, fb->h - 1);
    lisa_draw(&lisa_frame, &r);
}

/* ---- effects not yet in the engine: drawn after the painter's list with the old depth test ---- */

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

/* ---- skid marks: sub_0003731c keeps a ring of 700 engine objects (class 200, radius 20, texture slot 0), each
 * an 8-vertex mesh of two tyre quads as type-20 faces (0, 1, 2), (2, 1, 3) and (4, 5, 6), (6, 5, 7) of the SHD row
 * of the surface (0xc34e0), placed at the axle point. The port keeps the corners as floats; the original stores
 * them as integers relative to the truncated axle point, rebuilt here from the current edge. ---- */

typedef struct {
    IgnSkidMark src;
    int id;
    int32_t mesh[2 + 8 * 3 + 4 * 5];
} EngineSkid;

static EngineSkid engine_skids[IGN_MAX_SKIDS];

static void place_skid(EngineSkid *e, const IgnSkidMark *m) {
    e->src = *m;
    double ax = 0, az = 0;
    for (int k = 0; k < 2; k++) ax += m->x[k][2] + m->x[k][3], az += m->z[k][2] + m->z[k][3];
    int32_t ox = chop_d(ax / 4 + IGN_SRF_ORIGIN), oz = chop_d(az / 4 + IGN_SRF_ORIGIN), oy = chop_d(-m->y);
    int32_t *mesh = e->mesh;
    mesh[0] = 8, mesh[1] = 4;
    /* per tyre: previous outer, previous inner, current outer, current inner */
    static const int corner[4] = {1, 0, 2, 3};
    for (int k = 0; k < 2; k++)
        for (int c = 0; c < 4; c++) {
            int32_t *v = mesh + 2 + (k * 4 + c) * 3;
            v[0] = chop_d(m->x[k][corner[c]] + IGN_SRF_ORIGIN) - ox;
            v[1] = 0;
            v[2] = chop_d(m->z[k][corner[c]] + IGN_SRF_ORIGIN) - oz;
        }
    const int32_t faces[4][5] = {{20, 0, 1, 2, m->color}, {20, 2, 1, 3, m->color}, {20, 4, 5, 6, m->color},
                                 {20, 6, 5, 7, m->color}};
    memcpy(mesh + 2 + 8 * 3, faces, sizeof faces);
    static const int16_t zero[3] = {0, 0, 0};
    if (e->id < 0) {
        e->id = lisa_create(&scene, mesh, NULL, 0, 200, 0);
        if (e->id < 0) return;
        lisa_set_mesh(&scene, e->id, mesh, 20);
        lisa_place(&scene, e->id, ox, oy, oz, zero);
    } else {
        lisa_set_mesh(&scene, e->id, mesh, 20);
        lisa_move(&scene, e->id, ox, oy, oz, zero);
    }
}

void ign_render_skids(IgnFrame *fb, const IgnLevel *lv, const IgnSkids *s, const IgnCamera *cam) {
    (void)fb;
    if (build_scene(lv)) return;
    begin_frame(lv, cam);
    for (int i = 0; i < s->count && i < IGN_MAX_SKIDS; i++) {
        EngineSkid *e = &engine_skids[i];
        if (i >= n_engine_skids) e->id = -1, n_engine_skids = i + 1;
        else if (!memcmp(&e->src, &s->marks[i], sizeof e->src)) continue;
        place_skid(e, &s->marks[i]);
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
