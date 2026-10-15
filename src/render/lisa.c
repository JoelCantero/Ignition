/* Lisa 2 scene engine (MAINDOS.EXE 0x54000-0x60000): objects in a grid of 256-unit cells, render_scene
 * (sub_00056410) and the face handlers that turn each visible face into a primitive in one of 6000 depth buckets.
 * Engine coordinates: x/z = world + 25600, y up; angles in tenths of a degree. */
#include "lisa.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "lisa_cone.inc"

#define CELL_SHIFT 8
#define CONE_TABLE 45 /* CONE[45 ...] = 0xc5e40 */

/* `sar` with the sbb trick of the original: division by 2^n rounding toward zero. */
static int32_t div_pow2(int32_t v, int n) { return (v + (v < 0 ? (1 << n) - 1 : 0)) >> n; }
/* x87 `fld; call __CHP; fistp`: truncation toward zero. */
static int32_t chop(double v) { return (int32_t)v; }
/* Wrapping 32-bit integer product and sums, as imul/add. */
static int32_t mul(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
static int32_t add3(int32_t a, int32_t b, int32_t c) { return (int32_t)((uint32_t)a + (uint32_t)b + (uint32_t)c); }

static uint8_t keyed_table[65536];

int lisa_scene_init(LisaScene *s, int width, int depth, int max_objects) {
    memset(s, 0, sizeof *s);
    if (!keyed_table[1])
        for (int a = 0; a < 256; a++)
            for (int b = 0; b < 256; b++) keyed_table[a * 256 + b] = (uint8_t)(a ? a : b);
    s->keyed = keyed_table;
    s->gw = div_pow2(width + 0xff, CELL_SHIFT); /* engine_alloc_objects (0x569ec) */
    s->gh = div_pow2(depth + 0xff, CELL_SHIFT);
    s->grid = malloc(sizeof *s->grid * (size_t)s->gw * s->gh);
    s->objs = calloc((size_t)max_objects, sizeof *s->objs);
    if (!s->grid || !s->objs) return -1;
    s->cap = max_objects;
    for (int i = 0; i < s->gw * s->gh; i++) s->grid[i] = -1;
    return 0;
}

void lisa_scene_free(LisaScene *s) {
    free(s->grid);
    free(s->objs);
    memset(s, 0, sizeof *s);
}

/* LI_CREATEOBJECT (sub_00056b28): the radius of -1 is computed as the largest vertex distance, tested only for
 * vertices whose |x| + |y| + |z| exceeds the radius so far. */
int lisa_create(LisaScene *s, const int32_t *mesh, const uint8_t *tex, int tex_pages, int klass, int zone) {
    if (s->nobj >= s->cap) return -1;
    LisaObject *o = &s->objs[s->nobj];
    memset(o, 0, sizeof *o);
    o->mesh = mesh, o->tex = tex, o->tex_pages = tex_pages;
    o->klass = (int16_t)klass, o->zone = (int16_t)zone, o->next = -1, o->alive = 1;
    int32_t r = -1000;
    const int32_t *v = mesh + 2;
    for (int i = 0; i < mesh[0]; i++, v += 3)
        if (abs(v[0]) + abs(v[1]) + abs(v[2]) > r) {
            int32_t d = chop(sqrt((double)v[0] * v[0] + (double)v[1] * v[1] + (double)v[2] * v[2]));
            if (r < d) r = d;
        }
    o->radius = (int16_t)r;
    return s->nobj++;
}

static int *cell_of(LisaScene *s, int32_t x, int32_t z) {
    return &s->grid[div_pow2(z, CELL_SHIFT) * s->gw + div_pow2(x, CELL_SHIFT)];
}

static void unlink_object(LisaScene *s, int id) {
    LisaObject *o = &s->objs[id];
    int *head = cell_of(s, o->x, o->z);
    if (*head == id) {
        *head = o->next;
        return;
    }
    for (int p = *head; p >= 0; p = s->objs[p].next)
        if (s->objs[p].next == id) {
            s->objs[p].next = o->next;
            return;
        }
}

/* LI_PLACEOBJECT (sub_00056c74): position, angles and insertion at the head of the cell's list. */
void lisa_place(LisaScene *s, int id, int32_t x, int32_t y, int32_t z, const int16_t ang[3]) {
    LisaObject *o = &s->objs[id];
    if (o->placed) return;
    o->x = x, o->y = y, o->z = z;
    memcpy(o->ang, ang, sizeof o->ang);
    int *head = cell_of(s, x, z);
    o->next = *head;
    *head = id;
    o->placed = 1;
}

/* LI_MOVEOBJECT (sub_00056d24): a placed object is moved to the head of its new cell only when the cell changes. */
void lisa_move(LisaScene *s, int id, int32_t x, int32_t y, int32_t z, const int16_t ang[3]) {
    LisaObject *o = &s->objs[id];
    int32_t ox = o->x, oz = o->z;
    o->x = x, o->y = y, o->z = z;
    memcpy(o->ang, ang, sizeof o->ang);
    if (!o->placed) return;
    if (div_pow2(ox, CELL_SHIFT) == div_pow2(x, CELL_SHIFT) && div_pow2(oz, CELL_SHIFT) == div_pow2(z, CELL_SHIFT))
        return;
    o->x = ox, o->z = oz;
    unlink_object(s, id);
    o->x = x, o->z = z;
    int *head = cell_of(s, x, z);
    o->next = *head;
    *head = id;
}

/* LI_REMOVEOBJECT (sub_00056f8c). */
void lisa_remove(LisaScene *s, int id) {
    LisaObject *o = &s->objs[id];
    if (!o->placed) return;
    unlink_object(s, id);
    o->placed = 0;
}

/* render_scene 0x56410: the clip rectangle in 8.8 (0xc87ec..0xc87f8). */
void lisa_view_clip(LisaView *v, int x0, int y0, int x1, int y1) {
    v->clip[0] = x0, v->clip[1] = y0, v->clip[2] = x1, v->clip[3] = y1;
    v->xmin8 = x0 << 8, v->ymin8 = y0 << 8, v->xmax8 = (x1 + 1) << 8, v->ymax8 = (y1 + 1) << 8;
}

/* ---- render_scene ---- */

typedef struct {
    const LisaScene *scene;
    const LisaView *v;
    LisaFrame *f;
} Ctx;

static void add_visible(Ctx *c, int id) {
    if (c->f->nvis < LISA_MAX_VISIBLE) c->f->vis[c->f->nvis++] = id;
}

/* One cell of sub_00058ebc: a dead head object hides the whole cell; with a zone, objects of another zone are
 * skipped (zone 0 shows everything). */
static void visit_cell(Ctx *c, int head, int zone) {
    const LisaObject *objs = c->scene->objs;
    if (head < 0 || !objs[head].alive) return;
    for (int id = head; id >= 0; id = objs[id].next) {
        const LisaObject *o = &objs[id];
        if (!o->alive) continue;
        if (zone && o->zone && o->zone != zone) continue;
        add_visible(c, id);
    }
}

static int grid_at(const LisaScene *s, long cell) {
    return cell >= 0 && cell < (long)s->gw * s->gh ? s->grid[cell] : -1;
}

/* sub_0005b7a8: camera rows scaled by 16384 for the sphere test. */
static void cull_matrix(const LisaView *v, int32_t m[9]) {
    const double k = 0.001745329251994333;
    double a = -v->pitch * k, b = -v->yaw * k, c = -v->roll * k;
    double sa = sin(a), ca = cos(a), sb = sin(b), cb = cos(b), sc = sin(c), cc = cos(c);
    const double K = 16384.0;
    double t = sc * sa;
    m[0] = chop((cc * cb - t * sb) * K);
    m[1] = chop(-sc * ca * K);
    m[2] = chop((t * cb + cc * sb) * K);
    double u = cc * sa;
    m[3] = chop((sc * cb + u * sb) * K);
    m[4] = chop(cc * ca * K);
    m[5] = chop((sc * sb - u * cb) * K);
    m[6] = chop(-sb * ca * K);
    m[7] = chop(sa * K);
    m[8] = chop(cb * ca * K);
}

/* sub_00058ebc: objects of the cells around the camera, then a bounding-sphere test. With reach (config +0xa4)
 * a reach x reach square shifted a third of it along the view; otherwise the cone pattern of 0xc5d8c moved by the
 * 0xc5e40 offset of the view's 10-degree yaw and pitch steps. */
static void collect(Ctx *c) {
    const LisaView *v = c->v;
    const LisaScene *s = c->scene;
    int zone = v->zone;
    c->f->nvis = 0;
    if (v->reach) {
        int third = v->reach / 3, half = v->reach / 2;
        double yaw = v->yaw * 0.001745329251994333;
        int32_t ox = chop(sin(yaw) * third), oz = chop(cos(yaw) * third);
        long p = (long)(div_pow2(chop(v->z), CELL_SHIFT) - half + oz) * s->gw +
                 (div_pow2(chop(v->x), CELL_SHIFT) - half) + ox;
        for (int row = 0; row < v->reach; row++, p += s->gw - v->reach)
            for (int col = 0; col < v->reach; col++, p++) visit_cell(c, grid_at(s, p), zone);
    } else {
        int32_t yq = chop(v->yaw * 0.01), pq = chop(0.01 * v->pitch);
        long idx = CONE_TABLE + 72L * pq + 2L * yq;
        if (idx < 0) idx = 0;
        if (idx > (long)(sizeof CONE / sizeof CONE[0]) - 2) idx = (long)(sizeof CONE / sizeof CONE[0]) - 2;
        long p = (long)CONE[1] * s->gw + CONE[2] + (long)div_pow2(chop(v->z), CELL_SHIFT) * s->gw +
                 div_pow2(chop(v->x), CELL_SHIFT) + CONE[idx] + (long)CONE[idx + 1] * s->gw;
        for (int k = 3; CONE[k] != -5000; k += 2) {
            for (int i = 0; i < CONE[k]; i++) visit_cell(c, grid_at(s, ++p), zone);
            p += s->gw + CONE[k + 1];
        }
    }
    /* 0x59469: sphere against the view, depth halved by the >> 15 of 16384-scaled rows and moved by the near
     * distance. Only the vertical extent rejects (the horizontal test lets everything through). */
    int32_t m[9];
    cull_matrix(v, m);
    int n = 0;
    for (int i = 0; i < c->f->nvis; i++) {
        const LisaObject *o = &s->objs[c->f->vis[i]];
        int32_t dx = chop(o->x - v->x), dy = chop(o->y - v->y), dz = chop(o->z - v->z);
        int32_t z = (add3(mul(dx, m[6]), mul(dy, m[7]), mul(dz, m[8])) >> 15) + v->near;
        if (o->radius + z < 0) continue;
        if (z < v->near) z = v->near;
        int32_t y = add3(mul(dz, m[5]), mul(dy, m[4]), mul(dx, m[3])) >> 15;
        int32_t sy = v->cy + mul(-v->fy, y) / z;
        int32_t r = mul(v->fy, o->radius) / z + 2;
        if (r + sy <= v->clip[1] || sy - r >= v->clip[3] + 1) continue;
        c->f->vis[n++] = c->f->vis[i];
    }
    c->f->nvis = n;
}

/* sub_0005a2c8 (focal <= 300): float rows rounded to float32 then truncated; x mirrored (-focal), y by -focal,
 * depth x 2^18 so that (row . d) >> 16 is 4 z. The camera's fraction (x 4096) is taken out per row. */
static void projection(const LisaView *v, int32_t m[9]) {
    const double k = 0.001745329251994333;
    float sa = (float)sin(-v->pitch * k), ca = (float)cos(-v->pitch * k);
    float sb = (float)sin(-v->yaw * k), cb = (float)cos(-v->yaw * k);
    float sc = (float)sin(-v->roll * k), cc = (float)cos(-v->roll * k);
    double sx = -v->fx * 16384.0 * 0.015625 * 4.0 * v->zoom;
    float sy = (float)(-v->fy * 16384.0 * 0.015625 * 4.0);
    double t = (double)sc * sa;
    float u = (float)((double)cc * sa);
    float M[9] = {
        (float)(((double)cc * cb - t * sb) * sx),
        (float)(-(double)sc * ca * sx),
        (float)((t * cb + (double)cc * sb) * sx),
        (float)(((double)cc * sa * sb + (double)sc * cb) * sy),
        (float)((double)cc * ca * sy),
        (float)(((double)sc * sb - (double)u * cb) * sy),
        (float)(-(double)sb * ca * 16384.0 * 16.0),
        (float)((double)sa * 16384.0 * 16.0),
        (float)((double)ca * cb * 16384.0 * 16.0),
    };
    for (int i = 0; i < 9; i++) m[i] = chop(M[i]);
}

static void transform(Ctx *c) {
    const LisaView *v = c->v;
    LisaFrame *f = c->f;
    int32_t m[9];
    projection(v, m);
    int32_t near4 = v->near << 2, cx8 = v->cx << 8, cy8 = v->cy << 8;
    int32_t ix = chop(v->x), iy = chop(v->y), iz = chop(v->z);
    int32_t fx4 = chop((v->x - ix) * 4096.0), fy4 = chop((v->y - iy) * 4096.0), fz4 = chop((v->z - iz) * 4096.0);
    int32_t off0 = div_pow2(add3(mul(fx4, m[0]), mul(fy4, m[1]), mul(fz4, m[2])), 12);
    int32_t off1 = div_pow2(add3(mul(fx4, m[3]), mul(fy4, m[4]), mul(fz4, m[5])), 12);
    int32_t off2 = div_pow2(add3(mul(fx4, m[6]), mul(fy4, m[7]), mul(fz4, m[8])), 12);
    f->nvert = 0;
    for (int i = 0; i < f->nvis; i++) {
        const LisaObject *o = &c->scene->objs[f->vis[i]];
        f->vbase[i] = f->nvert;
        int32_t dx = o->x - ix, dy = o->y - iy, dz = o->z - iz;
        const int32_t *p = o->mesh + 2;
        int nv = o->mesh[0];
        if (f->nvert + nv > LISA_MAX_VERTS) nv = 0;
        for (int k = 0; k < nv; k++, p += 3) {
            int32_t X = dx + p[0], Y = dy - p[1], Z = dz + p[2];
            int32_t xn = (int32_t)((uint32_t)add3(mul(X, m[0]), mul(Y, m[1]), mul(Z, m[2])) - (uint32_t)off0);
            int32_t yn = (int32_t)((uint32_t)add3(mul(X, m[3]), mul(Y, m[4]), mul(Z, m[5])) - (uint32_t)off1);
            int32_t zn = (int32_t)((uint32_t)add3(mul(X, m[6]), mul(Y, m[7]), mul(Z, m[8])) - (uint32_t)off2);
            int32_t z = near4 + (zn >> 16);
            if (z < near4) z = near4;
            LisaVert *out = &f->verts[f->nvert++];
            out->x = xn / z + cx8;
            out->y = yn / z + cy8;
            out->z = z;
        }
    }
}

/* ---- face handlers (table 0xc86c4) ---- */

static LisaPrim *new_prim(LisaFrame *f) { return f->nprim < LISA_MAX_PRIMS ? &f->prims[f->nprim] : NULL; }

/* Bucket insertion of the handlers: objects of class >= 100 move 92 buckets nearer (84 for class 210). */
static void bucket(LisaFrame *f, const LisaObject *o, int32_t key) {
    if (o->klass >= 100) {
        key -= o->klass == 210 ? 84 : 92;
        if (key < 0) key = 0;
    }
    if (key >= LISA_BUCKETS) key = LISA_BUCKETS - 1;
    f->next[f->nprim] = f->head[key];
    f->head[key] = f->nprim++;
}

static const uint8_t *face_page(const LisaObject *o, uint32_t word) {
    int page = (int)(word >> 16);
    if (page >= o->tex_pages) page = o->tex_pages - 1;
    return o->tex + ((size_t)page << 16);
}

/* sub_0005c144 (face type 17, no MIP levels): trivial reject against the clip box, back faces culled on the
 * 1/16-pixel cross product, faces with z0 + z1 + z2 <= 600 (4 z) dropped. Near faces (bucket <= 720) of road
 * classes (< 100, 200, 300-302) become primitive 0x14 with the vertex depth as light, the spread around the mean
 * faded out between buckets 480 and 720; the rest primitive 0x11. */
static void face_tex(Ctx *c, const LisaObject *o, const LisaVert *vb, const int32_t *face) {
    const LisaView *v = c->v;
    LisaFrame *f = c->f;
    const LisaVert *a = &vb[face[1]], *b = &vb[face[2]], *d = &vb[face[3]];
    if (((a->y - v->ymin8) | (a->x - v->xmin8) | (v->ymax8 - a->y) | (v->xmax8 - a->x)) < 0) {
        int32_t hx = a->x > b->x ? a->x : b->x, lx = a->x < b->x ? a->x : b->x;
        int32_t hy = a->y > b->y ? a->y : b->y, ly = a->y < b->y ? a->y : b->y;
        if (hx <= d->x) hx = d->x;
        if (lx >= d->x) lx = d->x;
        if (hy <= d->y) hy = d->y;
        if (ly >= d->y) ly = d->y;
        if (hx < v->xmin8 || lx > v->xmax8 || hy < v->ymin8 || ly > v->ymax8) return;
    }
    int32_t cross = ((d->y >> 4) - (b->y >> 4)) * ((a->x >> 4) - (b->x >> 4)) -
                    ((a->y >> 4) - (b->y >> 4)) * ((d->x >> 4) - (b->x >> 4));
    if (v->zoom <= 0) cross = ~cross;
    int32_t zsum = a->z + b->z + d->z;
    if (zsum <= 600 || cross <= 0) return;
    LisaPrim *p = new_prim(f);
    if (!p) return;
    int32_t key = zsum >> 4;
    const uint8_t *tex = face_page(o, (uint32_t)face[10]);
    int k = o->klass;
    if (key > 720 || !(k < 100 || k == 200 || (k >= 300 && k <= 302))) {
        p->type = 0x11;
        p->xy[0] = a->x, p->xy[1] = a->y, p->xy[2] = b->x, p->xy[3] = b->y, p->xy[4] = d->x, p->xy[5] = d->y;
    } else {
        p->type = 0x14;
        p->xy[0] = a->x, p->xy[1] = a->y, p->xy[2] = b->x, p->xy[3] = b->y, p->xy[4] = d->x, p->xy[5] = d->y;
        if (key <= 480) {
            p->light[0] = a->z, p->light[1] = b->z, p->light[2] = d->z;
        } else {
            int32_t mean = zsum / 3;
            float fade = (float)((720 - key) * (double)0.004166666883975267f);
            p->light[0] = mean + chop((double)(a->z - mean) * fade);
            p->light[1] = mean + chop((double)(b->z - mean) * fade);
            p->light[2] = mean + chop((double)(d->z - mean) * fade);
        }
    }
    memcpy(p->uv, face + 4, sizeof p->uv);
    p->tex = tex;
    bucket(f, o, key);
}

/* sub_0005d718 (face types 18, 19, 22, 23 through 0x5d6d0..0x5d704): blended faces, keyed by the table of
 * 0x24c004 (18, 22) or the level SHD (19, 23); 22 and 23 sort 80 buckets nearer. Rejected only when their box is
 * off the clip area. Near road faces become primitive 0x15 (unless +0x58 is clear or the surface is 0x4d), the
 * rest primitive 0x12. */
static void face_blend(Ctx *c, const LisaObject *o, const LisaVert *vb, const int32_t *face, const uint8_t *lut,
                       int bias) {
    const LisaView *v = c->v;
    LisaFrame *f = c->f;
    const LisaVert *a = &vb[face[1]], *b = &vb[face[2]], *d = &vb[face[3]];
    int32_t hx = a->x > b->x ? a->x : b->x, lx = a->x < b->x ? a->x : b->x;
    int32_t hy = a->y > b->y ? a->y : b->y, ly = a->y < b->y ? a->y : b->y;
    if (hx <= d->x) hx = d->x;
    if (lx >= d->x) lx = d->x;
    if (hy <= d->y) hy = d->y;
    if (ly >= d->y) ly = d->y;
    if (hx < v->xmin8 || lx > v->xmax8 || hy < v->ymin8 || ly > v->ymax8) return;
    int32_t cross = ((d->y >> 4) - (b->y >> 4)) * ((a->x >> 4) - (b->x >> 4)) -
                    ((a->y >> 4) - (b->y >> 4)) * ((d->x >> 4) - (b->x >> 4));
    if (v->zoom <= 0) cross = ~cross;
    int32_t zsum = a->z + b->z + d->z;
    if (zsum <= 600 || cross <= 0) return;
    LisaPrim *p = new_prim(f);
    if (!p) return;
    int32_t key = zsum >> 4;
    int k = o->klass;
    int road = k < 100 || k == 200 || (k >= 300 && k <= 302);
    p->xy[0] = a->x, p->xy[1] = a->y, p->xy[2] = b->x, p->xy[3] = b->y, p->xy[4] = d->x, p->xy[5] = d->y;
    if (key > 720 || !road || !v->persp || (face[0] >> 16) == 0x4d) {
        p->type = 0x12;
    } else {
        p->type = 0x15;
        if (key <= 480) {
            p->light[0] = a->z, p->light[1] = b->z, p->light[2] = d->z;
        } else {
            /* unlike type 17 the fade factor stays in the FPU unrounded */
            int32_t mean = zsum / 3;
            double fade = (720 - key) * (double)0.004166666883975267f;
            p->light[0] = mean + chop((a->z - mean) * fade);
            p->light[1] = mean + chop((b->z - mean) * fade);
            p->light[2] = mean + chop((d->z - mean) * fade);
        }
    }
    memcpy(p->uv, face + 4, sizeof p->uv);
    p->tex = face_page(o, (uint32_t)face[10]);
    p->lut = lut;
    key -= bias;
    if (o->klass >= 100) key -= 92;
    if (key < 0) key = 0;
    if (key >= LISA_BUCKETS) key = LISA_BUCKETS - 1;
    f->next[f->nprim] = f->head[key];
    f->head[key] = f->nprim++;
}

/* Bytes taken by each face type in the mesh (handlers advance 0x24c028 by these). */
static int face_words(int type) {
    switch (type) {
    case 1: case 2: return 3;
    case 12: return 4;
    case 20: return 5;
    default: return 11;
    }
}

/* sub_0005cb48 */
static void faces(Ctx *c) {
    LisaFrame *f = c->f;
    for (int i = 0; i < f->nvis; i++) {
        const LisaObject *o = &c->scene->objs[f->vis[i]];
        const LisaVert *vb = &f->verts[f->vbase[i]];
        const int32_t *face = o->mesh + 2 + o->mesh[0] * 3;
        for (int n = 0; n < o->mesh[1]; n++) {
            int type = face[0] & 0xff;
            switch (type) {
            case 17: face_tex(c, o, vb, face); break;
            case 18: face_blend(c, o, vb, face, c->scene->keyed, 0); break;
            case 19: face_blend(c, o, vb, face, c->scene->shade, 0); break;
            case 22: face_blend(c, o, vb, face, c->scene->keyed, 80); break;
            case 23: face_blend(c, o, vb, face, c->scene->shade, 80); break;
            default: break;
            }
            face += face_words(type);
        }
    }
}

void lisa_build(const LisaScene *scene, const LisaView *view, LisaFrame *f) {
    Ctx c = {scene, view, f};
    f->nprim = 0;
    for (int i = 0; i < LISA_BUCKETS; i++) f->head[i] = -1;
    if (view->layers & LISA_LAYER_OBJECTS) collect(&c);
    else f->nvis = 0;
    if (view->layers & LISA_LAYER_TRANSFORM) transform(&c);
    if (view->layers & LISA_LAYER_FACES) faces(&c);
    /* sub_0005b9c4: buckets far to near, each list from its last insertion. */
    f->norder = 0;
    if (view->layers & LISA_LAYER_SORT)
        for (int k = LISA_BUCKETS - 1; k >= 0; k--)
            for (int p = f->head[k]; p >= 0; p = f->next[p]) f->order[f->norder++] = p;
}

void lisa_draw(const LisaFrame *f, IgnRaster *r) {
    for (int i = 0; i < f->norder; i++) {
        const LisaPrim *p = &f->prims[f->order[i]];
        switch (p->type) {
        case 0x11: ign_prim_tex(r, p->xy, p->uv, p->tex, NULL); break;
        case 0x12: ign_prim_tex(r, p->xy, p->uv, p->tex, p->lut); break;
        case 0x14: ign_prim_persp(r, p->xy, p->light, p->uv, p->tex, NULL); break;
        case 0x15: ign_prim_persp(r, p->xy, p->light, p->uv, p->tex, p->lut); break;
        default: break;
        }
    }
}
