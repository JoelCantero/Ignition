#include "raster.h"

#include "lisa.h"
#include "../platform/runtime.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    ign_fill_pattern(fb->color, 0, (size_t)fb->w * fb->h);
    ign_fill_pattern(fb->inv_z, 0, (size_t)fb->w * fb->h * sizeof fb->inv_z[0]);
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
typedef struct {
    int id, type;
    int32_t mesh[2 + 13 * 3 + 20 * 5 + 8];
} EngineFx;

static EngineFx engine_fx[IGN_FX_SLOTS];
static int engine_fx_ready;
static struct {
    int ids[9];
    int32_t *mesh;
    int ready;
} engine_weather;
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
    if (!engine_fx_ready) {
        for (int i = 0; i < IGN_FX_SLOTS; i++) engine_fx[i].id = -1;
        for (int k = 0; k < 9; k++) engine_weather.ids[k] = -1;
        engine_weather.ready = engine_fx_ready = 1;
    }
    if (scene_level) {
        for (int i = 0; i < n_engine_cars; i++) {
            free(engine_cars[i].mesh_body);
            for (int w = 0; w < 4; w++) free(engine_cars[i].mesh_wheel[w]);
        }
        n_engine_cars = 0;
        n_engine_skids = 0;
        for (int i = 0; i < IGN_FX_SLOTS; i++) engine_fx[i].id = -1;
        for (int k = 0; k < 9; k++) engine_weather.ids[k] = -1;
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

/* [0x24bff0]: zone of the viewed car's section object (race_frame 0x430b6), 0 after 120 post-race frames.
 * Config +0xa4 (race_frame 0x42130, sub_0002489c 0x25ff1): a square of cells instead of the view cone in some
 * sections (CARIB always 25). */
static int view_zone, view_reach;

void ign_render_set_view(const IgnLevel *lv, int track_id, int section, int zone_object) {
    view_zone = zone_object >= 0 && zone_object < lv->track.nobj ? (int)((lv->track.objs[zone_object].type >> 12) & 0xf)
                                                                  : 0;
    int s = section, r = 0;
    switch (track_id) {
    case 0: r = s > 0x24 && s < 0x2e ? 30 : s > 0x7c && s < 0x8c ? 35 : 0; break; /* CANADA */
    case 1: r = s > -0x7c && s < -0x71 ? 30 : 0; break;                           /* USA */
    case 2: r = 25; break;                                                         /* CARIB */
    case 3: r = s > 0 && s < 0x1e ? 25 : s > 0xbb && s < 0xc5 ? 30 : 0; break;    /* BRAZIL */
    case 4: r = s > 6 && s < 0xe ? 35 : 0; break;                                  /* AUSTRIA */
    case 5: r = s > 0x1b && s < 0x46 ? 30 : 0; break;                              /* ICELAND */
    default: r = 0; break;                                                         /* JAPAN */
    }
    view_reach = r;
}

/* race_frame 0x42c92 / race_setup_video: config block of a full-screen view. */
static void engine_view(const IgnFrame *fb, const IgnCamera *cam, LisaView *v) {
    const double tenths = 1800.0 / 3.14159265358979323846;
    memset(v, 0, sizeof *v);
    v->x = cam->x + (double)IGN_SRF_ORIGIN, v->y = -(double)cam->y, v->z = cam->z + (double)IGN_SRF_ORIGIN;
    v->pitch = cam->pitch * tenths, v->yaw = cam->yaw * tenths;
    /* sub_0002489c 0x248c9: difficulty 3 mirrors the view (+0x30 = -1); +0x58 is the PERSP. POLY option */
    v->zoom = ign_get_difficulty() >= 3 ? -1.0 : 1.0;
    v->fx = (int)fb->fx, v->fy = (int)fb->fy;
    v->near = fb->w == 320 && fb->h == 200 ? 25 : 10;
    v->cx = fb->w / 2, v->cy = fb->h / 2;
    v->layers = LISA_LAYER_ALL;
    v->zone = view_zone;
    v->reach = view_reach;
    v->persp = fb->perspective;
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
/* `sar` with the sbb trick: division by 2^n rounding toward zero */
static int32_t div_pow2_raster(int32_t v, int n) { return (v + (v < 0 ? (1 << n) - 1 : 0)) >> n; }
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

/* ---- effects: every slot of 0x1d5f10 owns an engine object ([0x1fefa8] + slot * 32) and a mesh buffer
 * (0x1aebd0 + slot * 0x320) that its handler fills before LI_SETMESH / LI_MOVEOBJECT; class 200 unless noted.
 * Effect positions are world x 1024 in the port and engine x 1024 in the original. ---- */


/* `sar` by 10 with the sbb rounding, on engine coordinates */
static int32_t fx_engine(int32_t world1024) { return div_pow2_raster(world1024 + IGN_SRF_ORIGIN * 1024, 10); }

static void fx_object(EngineFx *e, const uint8_t *tex, int klass, int radius, int32_t x, int32_t y, int32_t z,
                      const int16_t ang[3]) {
    if (e->id < 0) {
        e->id = lisa_create(&scene, e->mesh, tex, 1, klass, 0);
        if (e->id < 0) return;
    }
    LisaObject *o = &scene.objs[e->id];
    o->tex = tex, o->tex_pages = 1, o->klass = (int16_t)klass;
    lisa_set_mesh(&scene, e->id, e->mesh, radius);
    if (!o->placed) lisa_place(&scene, e->id, x, y, z, ang);
    else lisa_move(&scene, e->id, x, y, z, ang);
    o->alive = 1;
}

/* face type 7: {7, vertex, u0, v0, u1, v1, page, size x, size y} */
static void fx_sprite_face(int32_t *f, const int *uv, int32_t sx, int32_t sy) {
    f[0] = 7, f[1] = 0;
    f[2] = uv[0] << 8, f[3] = uv[1] << 8, f[4] = uv[2] << 8, f[5] = uv[3] << 8;
    f[6] = 0, f[7] = sx, f[8] = sy;
}

void ign_render_fx(IgnFrame *fb, const IgnLevel *lv, const IgnFx *fx, IgnCar *const *cars, int ncars,
                   const IgnCamera *cam) {
    if (build_scene(lv)) return;
    begin_frame(lv, cam);
    /* sub_00049724: sprite scales 65535 * W / 320 and 65535 * H / 200 */
    int32_t sw = chop_d(fb->w * 65535.0 * 0.003125), sh = chop_d(fb->h * 65535.0 * 0.005);
    static const int16_t zero[3] = {0, 0, 0};
    for (int i = 0; i < IGN_FX_SLOTS; i++) {
        const int32_t *r = fx->r[i];
        EngineFx *e = &engine_fx[i];
        int32_t *m = e->mesh;
        if (!r[0]) {
            if (e->id >= 0 && scene.objs[e->id].placed) lisa_remove(&scene, e->id); /* LI_REMOVEOBJECT */
            continue;
        }
        int32_t px = fx_engine(r[1]), py = div_pow2_raster(r[2], 10), pz = fx_engine(r[3]);
        switch (r[0]) {
        case IGN_FX_SPRITE: /* sub_00039d1c: vertex +0x3c, angles +0x48 */
        case IGN_FX_BLAST: { /* sub_00039fe8: at the origin */
            m[0] = 1, m[1] = 1;
            int sprite = r[0] == IGN_FX_SPRITE;
            m[2] = sprite ? r[15] : 0, m[3] = sprite ? r[16] : 0, m[4] = sprite ? r[17] : 0;
            int32_t size = div_pow2_raster(r[12], 5);
            fx_sprite_face(m + 5, ign_fx_uv(r[14], div_pow2_raster(r[10], 10)), size * div_pow2_raster(sw, 5),
                           size * div_pow2_raster(sh, 5));
            const int16_t ang[3] = {(int16_t)r[18], (int16_t)r[19], (int16_t)r[20]};
            fx_object(e, lv->fx_tex[r[9] & 7], 200, 20, px, py, pz, sprite ? ang : zero);
            break;
        }
        case IGN_FX_PLOT: { /* sub_0003a2b4: one point of type mode + 1 */
            int mode = r[11] < 0 || r[11] > 1 ? 0 : r[11];
            m[0] = 1, m[1] = 1, m[2] = m[3] = m[4] = 0;
            m[5] = mode + 1, m[6] = 0, m[7] = div_pow2_raster(r[9], 10);
            fx_object(e, NULL, 200, 0, px, py, pz, zero);
            break;
        }
        case IGN_FX_SUPERPLOT: { /* sub_0003a498: four points (+-10, 0, +-10), angles +0x30 */
            static const int32_t P[4][2] = {{10, 10}, {-10, 10}, {10, -10}, {-10, -10}};
            int mode = r[11] < 0 || r[11] > 1 ? 0 : r[11];
            m[0] = 4, m[1] = 4;
            for (int k = 0; k < 4; k++) {
                m[2 + k * 3] = P[k][0], m[3 + k * 3] = 0, m[4 + k * 3] = P[k][1];
                m[14 + k * 3] = mode + 1, m[15 + k * 3] = k, m[16 + k * 3] = div_pow2_raster(r[9], 10);
            }
            const int16_t ang[3] = {(int16_t)div_pow2_raster(r[12], 10), (int16_t)div_pow2_raster(r[13], 10),
                                    (int16_t)div_pow2_raster(r[14], 10)};
            fx_object(e, NULL, 200, 0, px, py, pz, ang);
            break;
        }
        case IGN_FX_PIECE: { /* sub_0003b744: a triangle as two flat faces (both windings) of colour +0x3c */
            const int32_t *v = fx->piece[i];
            m[0] = 3, m[1] = 2;
            memcpy(m + 2, v, 9 * sizeof *v);
            const int32_t faces[2][5] = {{15, 0, 1, 2, r[15]}, {15, 0, 2, 1, r[15]}};
            memcpy(m + 11, faces, sizeof faces);
            const int16_t ang[3] = {(int16_t)div_pow2_raster(r[9], 10), (int16_t)div_pow2_raster(r[10], 10),
                                    (int16_t)div_pow2_raster(r[11], 10)};
            fx_object(e, NULL, 200, 0, px, py, pz, ang);
            break;
        }
        case IGN_FX_LIGHTNING: { /* sub_0003bdac: mesh 0xbfe90, 20 flat faces of colour 251, class 301, radius 100 */
            static const int16_t V[13][3] = {{10, -920, 11}, {1, -187, 5},    {21, -212, 3},  {9, -172, 4},
                                             {-9, -160, 6},  {-14, -921, 11}, {10, -920, -8}, {1, -187, -3},
                                             {21, -212, -2}, {0, -1, 0},      {9, -172, -3},  {-9, -160, -5},
                                             {-14, -921, -8}};
            static const uint8_t F[20][3] = {{3, 9, 2},  {3, 2, 1},   {4, 3, 1},  {5, 4, 1},  {5, 1, 0},
                                             {0, 1, 7},  {0, 7, 6},   {1, 2, 8},  {1, 8, 7},  {2, 9, 8},
                                             {9, 3, 10}, {3, 4, 11},  {3, 11, 10}, {4, 5, 12}, {4, 12, 11},
                                             {8, 9, 10}, {7, 8, 10},  {7, 10, 11}, {7, 11, 12}, {6, 7, 12}};
            m[0] = 13, m[1] = 20;
            for (int k = 0; k < 13; k++) m[2 + k * 3] = V[k][0], m[3 + k * 3] = V[k][1], m[4 + k * 3] = V[k][2];
            for (int f = 0; f < 20; f++) {
                int32_t *w = m + 2 + 13 * 3 + f * 5;
                w[0] = 15, w[1] = F[f][0], w[2] = F[f][1], w[3] = F[f][2], w[4] = 251;
            }
            const int16_t ang[3] = {(int16_t)r[7], (int16_t)r[8], 0};
            fx_object(e, NULL, 301, 100, px, py, pz, ang);
            break;
        }
        case IGN_FX_SPLASH: { /* sub_0003d808: quad (0, 1, 2) + (0, 2, 3) of type 19, frame table +0x34 */
            const int *uv = ign_fx_uv(r[13], div_pow2_raster(r[18], 10));
            m[0] = 4, m[1] = 2;
            for (int k = 0; k < 4; k++) m[2 + k * 3] = r[1 + k * 3], m[3 + k * 3] = r[2 + k * 3], m[4 + k * 3] = r[3 + k * 3];
            const int32_t t[4][2] = {{uv[0], uv[3]}, {uv[0], uv[1]}, {uv[2], uv[1]}, {uv[2], uv[3]}};
            const int idx[2][3] = {{0, 1, 2}, {0, 2, 3}};
            for (int f = 0; f < 2; f++) {
                int32_t *w = m + 14 + f * 11;
                w[0] = 19;
                for (int k = 0; k < 3; k++) {
                    w[1 + k] = idx[f][k];
                    w[4 + k * 2] = t[idx[f][k]][0] << 8, w[5 + k * 2] = t[idx[f][k]][1] << 8;
                }
                w[10] = 0;
            }
            fx_object(e, lv->fx_tex[r[14] & 7], 200, 20, fx_engine(r[15]), div_pow2_raster(r[16], 10), fx_engine(r[17]),
                      zero);
            break;
        }
        case IGN_FX_SPARKLE: { /* sub_0003e6ec: DIVSPR (33, 84)-(66, 117) of size 150, orbiting the car */
            static const int uv[4] = {33, 84, 66, 117};
            if (r[1] < 0 || r[1] >= ncars) break;
            const IgnCar *c = cars[r[1]];
            int radius = 250 - 8 * r[8];
            if (radius < 50) radius = 50;
            m[0] = 1, m[1] = 1, m[2] = radius, m[3] = 0, m[4] = 0;
            fx_sprite_face(m + 5, uv, 150 * sw, 150 * sh);
            const int16_t ang[3] = {(int16_t)div_pow2_raster(r[2] * 10, 10), (int16_t)div_pow2_raster(r[3] * 10, 10),
                                    (int16_t)div_pow2_raster(r[4] * 10, 10)};
            fx_object(e, lv->fx_tex[2], 200, 20, chop_d(c->x + (double)IGN_SRF_ORIGIN), chop_d(c->height),
                      chop_d(c->z + (double)IGN_SRF_ORIGIN), ang);
            break;
        }
        default: break;
        }
    }
}

/* ---- weather: sub_0002489c creates one class-200 object (texture slot 2, radius 1000) with the weather mesh and
 * sub_000403c8 moves it over the tiles around the camera; the port keeps one tile and draws it in 3 x 3 objects.
 * Rain: lines of type 13 from SHD row 0x77 to 0x7b; snow: DIVSPR sprites (0, 117)-(31, 148) of size 100. ---- */


void ign_render_weather(IgnFrame *fb, const IgnLevel *lv, const IgnWeather *w, const IgnCamera *cam) {
    if (build_scene(lv)) return;
    begin_frame(lv, cam);
    int lines = w->kind == IGN_WEATHER_RAIN, snow = w->kind == IGN_WEATHER_SNOW && lv->fx_tex[2];
    if (w->nverts <= 0 || (!lines && !snow)) {
        for (int k = 0; k < 9; k++)
            if (engine_weather.ids[k] >= 0) scene.objs[engine_weather.ids[k]].alive = 0;
        return;
    }
    if (!engine_weather.mesh) engine_weather.mesh = malloc(sizeof(int32_t) * (2 + IGN_WEATHER_VERTS * (3 + 9)));
    int32_t *m = engine_weather.mesh;
    if (!m) return;
    int32_t sw = chop_d(fb->w * 65535.0 * 0.003125), sh = chop_d(fb->h * 65535.0 * 0.005);
    m[0] = w->nverts;
    for (int k = 0; k < w->nverts; k++) m[2 + k * 3] = w->v[k][0], m[3 + k * 3] = w->v[k][1], m[4 + k * 3] = w->v[k][2];
    int32_t *f = m + 2 + w->nverts * 3;
    int nf = 0;
    if (lines) {
        for (int k = 0; k + 1 < w->nverts; k += 2, f += 6, nf++)
            f[0] = 13, f[1] = k, f[2] = k + 1, f[3] = 0x7700, f[4] = 0x7b00, f[5] = 0;
    } else {
        static const int uv[4] = {0, 117, 31, 148};
        for (int k = 0; k < w->nverts; k++, f += 9, nf++) {
            fx_sprite_face(f, uv, 100 * sw, 100 * sh);
            f[1] = k;
        }
    }
    m[1] = nf;
    static const int16_t zero[3] = {0, 0, 0};
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++) {
            int *id = &engine_weather.ids[row * 3 + col];
            int32_t x = w->tile_x + col * 1000 + IGN_SRF_ORIGIN, y = w->tile_y, z = w->tile_z + row * 1000 + IGN_SRF_ORIGIN;
            if (*id < 0) {
                *id = lisa_create(&scene, m, lv->fx_tex[2], 1, 200, 0);
                if (*id < 0) continue;
                lisa_set_mesh(&scene, *id, m, 1000);
                lisa_place(&scene, *id, x, y, z, zero);
            } else {
                lisa_set_mesh(&scene, *id, m, 1000);
                lisa_move(&scene, *id, x, y, z, zero);
            }
            scene.objs[*id].alive = 1;
        }
}
