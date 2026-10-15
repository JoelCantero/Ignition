#ifndef IGN_LISA_H
#define IGN_LISA_H

/* Port of the "Lisa 2 Development System" renderer of MAINDOS.EXE: the scene engine (0x54000-0x60000) builds a
 * painter's list of primitives in depth buckets and the rasterizer (sub_00060e3d) draws it back to front. */

#include <stdint.h>

#define IGN_RASTER_ROWS 1024

/* Clip state of sub_00060e3d (0xc87cc..0xc87f8) and the target. */
typedef struct {
    uint8_t *fb;
    int stride;
    int xmin, ymin, xmax1, ymax1;       /* 0xc87cc, 0xc87d0, 0xc87d4, 0xc87d8 */
    int32_t xmin8, ymin8, xmax8, ymax8; /* 0xc87ec..0xc87f8: 8.8, far edges exclusive */
} IgnRaster;

void ign_raster_clip(IgnRaster *r, int x0, int y0, int x1, int y1);

/* Primitives 0x11 (sub_00073c58) and 0x12 (sub_000744c8): affine textured triangle; xy and uv in 8.8, tex = one
 * 256x256 page; lut non-NULL (0x12) blends dst = lut[texel * 256 + dst]. */
void ign_prim_tex(IgnRaster *r, const int32_t xy[6], const int32_t uv[6], const uint8_t *tex, const uint8_t *lut);

/* Primitives 0x14 / 0x15 (sub_0007586c): perspective-correct textured triangle, per-vertex depth `light` (4 z);
 * lut non-NULL (0x15) blends dst = lut[texel * 256 + dst]. */
void ign_prim_persp(IgnRaster *r, const int32_t xy[6], const int32_t light[3], const int32_t uv[6],
                    const uint8_t *tex, const uint8_t *lut);

/* Primitive 0x13 (sub_00074d04): flat triangle darkening dst through one table row (row_lut = SHD + row * 256). */
void ign_prim_shade(IgnRaster *r, const int32_t xy[6], const uint8_t *row_lut);

/* Primitives 1 (lut = TAB, colour * 256 + light) and 2 (light < 0: lut = SHD, colour * 256 + dst): one pixel. */
void ign_prim_point(IgnRaster *r, int32_t x, int32_t y, const uint8_t *lut, int colour, int light);
/* Primitive 0xf (sub_000729f3): flat triangle of palette index `colour` (the TAB entry), whole-pixel vertices. */
void ign_prim_flat(IgnRaster *r, const int32_t xy[6], uint8_t colour);

/* Primitive 7 (sub_000704a0): sprite quad; uv = {half u, half v, u0, v0, u1, v1} (8.8), s = 2x2 scale. */
void ign_prim_sprite(IgnRaster *r, int32_t x, int32_t y, const int32_t uv[6], const int32_t s[4], const uint8_t *tex,
                     const uint8_t *lut);

/* Primitive 0xd (sub_00071cc7): line from (x0, y0) to (x1, y1) (8.8), 8.8 colours c0..c1 used as table rows. */
void ign_prim_line(IgnRaster *r, const int32_t xy[4], int32_t c0, int32_t c1, const uint8_t *lut);

/* ---- scene engine (lisa.c) ---- */

#define LISA_BUCKETS 6000      /* 0x24c04c */
#define LISA_MAX_VISIBLE 1500  /* 0x24c01c */
#define LISA_MAX_VERTS 20000   /* 0x24c054 */
#define LISA_MAX_PRIMS 14000   /* 0x24c050 */

/* 42-byte engine object of the pool [0x24c058]. */
typedef struct {
    const int32_t *mesh;  /* +0x04: MSH object {nv, nf, nv x {x, y, z} (y down), faces} */
    const uint8_t *tex;   /* +0x08 texture slot: 256x256 pages; a face's page is its last word >> 16 */
    int tex_pages;
    int32_t x, y, z;      /* +0x0c */
    int16_t ang[3];       /* +0x18: tenths of a degree */
    int16_t klass;        /* +0x1e: PLC class (type & 0xfff) */
    int16_t radius;       /* +0x22 */
    int16_t zone;         /* +0x24: PLC type bits 12-15 */
    int next;             /* +0x26: next object of the cell */
    int alive;            /* +0x00 points to itself */
    int placed;
} LisaObject;

typedef struct {
    LisaObject *objs;
    int nobj, cap;
    int *grid; /* 0x24c048: first object of each 256-unit cell */
    int gw, gh;
    const uint8_t *keyed; /* 0x24c004: [a * 256 + b] = a ? a : b (sub_00011494) */
    const uint8_t *shade; /* 0x24bfa8: the level SHD */
    const uint8_t *tab;   /* [0xc484c]: the level TAB */
    const uint8_t *pan;   /* 0x24bfec: the level PAN, blend table of type-8 sprites */
} LisaScene;

int lisa_scene_init(LisaScene *s, int width, int depth, int max_objects);
void lisa_scene_free(LisaScene *s);
int lisa_create(LisaScene *s, const int32_t *mesh, const uint8_t *tex, int tex_pages, int klass, int zone);
void lisa_place(LisaScene *s, int id, int32_t x, int32_t y, int32_t z, const int16_t ang[3]);
void lisa_move(LisaScene *s, int id, int32_t x, int32_t y, int32_t z, const int16_t ang[3]);
void lisa_remove(LisaScene *s, int id);
void lisa_set_mesh(LisaScene *s, int id, const int32_t *mesh, int radius);

/* render_scene layers, the flags [0x24c05c]+0x3c/+0x40/+0x50/+0x54 (+0x38, the look-at, is the caller's). */
enum {
    LISA_LAYER_OBJECTS = 1,
    LISA_LAYER_TRANSFORM = 2,
    LISA_LAYER_FACES = 4,
    LISA_LAYER_SORT = 8,
    LISA_LAYER_ALL = 15,
};

/* Configuration block [0x24c05c]. */
typedef struct {
    double x, y, z;            /* +0x00 */
    double pitch, yaw, roll;   /* +0x18: tenths of a degree */
    double zoom;               /* +0x30 */
    int fx, fy, near;          /* +0x80 */
    int cx, cy;                /* +0x9c */
    int reach;                 /* +0xa4: square of cells instead of the view cone when non-zero */
    int persp;                 /* +0x58: near road faces perspective-correct (1 as set by sub_000564d8) */
    int zone;                  /* [0x24bff0] */
    int layers;
    int clip[4];               /* x0, y0, x1, y1 inclusive ([0xc4850]) */
    int32_t xmin8, ymin8, xmax8, ymax8;
} LisaView;

typedef struct {
    int32_t x, y, z; /* 8.8 screen, 4 (z + near) */
} LisaVert;

typedef struct {
    int type;
    int32_t xy[6];
    int32_t light[3];
    int32_t uv[6];
    int32_t scale[4];   /* sprites */
    const uint8_t *tex;
    const uint8_t *lut;
} LisaPrim;

typedef struct LisaFrame {
    int vis[LISA_MAX_VISIBLE], vbase[LISA_MAX_VISIBLE], nvis;
    LisaVert verts[LISA_MAX_VERTS];
    int nvert;
    LisaPrim prims[LISA_MAX_PRIMS];
    int next[LISA_MAX_PRIMS], head[LISA_BUCKETS];
    int nprim;
    int order[LISA_MAX_PRIMS], norder;
} LisaFrame;

void lisa_view_clip(LisaView *v, int x0, int y0, int x1, int y1);
/* render_scene: angles of rotated objects may be renormalized in place, as the original does. */
void lisa_build(LisaScene *scene, const LisaView *view, LisaFrame *f);
void lisa_draw(const LisaFrame *f, IgnRaster *r);

#endif
