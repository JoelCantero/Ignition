#ifndef IGN_ASSETS_H
#define IGN_ASSETS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t rgb[256][3];
} IgnPalette;

typedef struct {
    int width;
    int height;
    IgnPalette palette;
    uint8_t *pixels;
} IgnPic;

/* Reads a whole file; returns NULL on failure. Caller frees. */
uint8_t *ign_read_file(const char *path, size_t *size);

/* Parses a 0x308-byte COL palette block. Returns 0 on success. */
int ign_parse_col(const uint8_t *data, size_t size, IgnPalette *out);

int ign_load_pic(const char *path, IgnPic *out);
void ign_free_pic(IgnPic *pic);

/* MSH face: 44 bytes on disk. uv are 8.8 fixed-point texels inside the 256x256 page extra >> 16. */
typedef struct {
    uint32_t flags;
    int32_t v[3];
    int32_t uv[6];
    uint32_t extra;
} IgnFace;

typedef struct {
    uint32_t type;
    int32_t pos[3];
    int32_t rotation[3]; /* POS rotation in tenths of a degree */
    int nv, nf;
    const int32_t *verts; /* nv * {x, y, z}; y points down */
    const IgnFace *faces;
} IgnObject;

typedef struct {
    uint8_t *tex; /* 256 x tex_h indices, 256x256 pages */
    int tex_h;
    int nobj;
    IgnObject *objs;
    uint8_t *plc, *msh;
} IgnModel;

typedef struct {
    uint8_t *data;
    size_t size;
    int nobj;
} IgnPos;

typedef struct {
    uint8_t *data;
    size_t size;
    int cell_w, cell_h, gx, gz;
    int nspan, na, nb;
    const uint8_t *cells, *spans, *list_a, *list_b;
    int *engine_to_plc; /* engine object index -> PLC index, -1 if unknown */
    int nengine;
} IgnSurface;

/* SRF coordinates are world x/z + this offset. */
#define IGN_SRF_ORIGIN 25600

typedef struct {
    IgnPalette palette;
    IgnPic backdrop;
    IgnModel track;
    IgnModel cars; /* CARS: object type = car * 10 + part (0 body, 1-4 wheels) */
    IgnPos pos;
    IgnSurface surface;
    uint8_t *tri; /* u32 n + 500-byte block per PLC object */
    size_t tri_size;
    uint8_t *shd; /* 64 KB SHD: shd[row * 256 + colour] darkens colour by row */
    uint8_t *tab; /* 64 KB TAB: shade ramp tab[colour * 256 + light] (shaded points) */
    uint8_t *fx_tex[8]; /* effect texture slots 2-7 (0x1fe8e0): DIVSPR, LIGHT, SMOKE, DARKSMOK, BOOM, EXSMOKE */
    int wall_height; /* collect_walls vertical reach for this track */
} IgnLevel;

int ign_load_model(const char *plc_path, const char *msh_path, const char *tex_path,
                   IgnModel *out);
void ign_free_model(IgnModel *m);

/* data_dir: game root; name: e.g. "USA". Returns 0 on success. */
int ign_load_level(const char *data_dir, const char *name, IgnLevel *out);
void ign_free_level(IgnLevel *lv);

/* Rotates one mesh-space vertex using its current POS angles. */
void ign_object_rotation_matrix(const IgnObject *o, float out[9]);
void ign_object_vertex(const IgnObject *o, int vertex, float out[3]);

/* The engine's ground query and collisions see an object's mesh vertices plus its current (POS or collision object)
 * position: the rotation only reaches the renderer. Engine coordinates: x/z + IGN_SRF_ORIGIN, y down. */
void ign_object_point(const IgnObject *o, int vertex, int32_t out[3]);

typedef struct {
    float y;          /* ground height (y down) */
    int surface_type; /* face flags >> 16 */
    int object, face;
    /* sub_00020814 result 0xebad8: +4 normal (p0 - p1) x (p2 - p1), +0x10/+0x1c/+0x28 the face points (engine). */
    int32_t normal[3];
    int32_t p[3][3];
} IgnGroundHit;

/* sub_00020814: ground face under world (x, z) for a car whose height (y down) is y_hint, probed 50 above it.
 * Returns 0 if found. */
int ign_ground_at(const IgnLevel *lv, float x, float z, float y_hint, IgnGroundHit *hit);
/* sub_00020814 with its own integer arguments: engine x/z (world + IGN_SRF_ORIGIN) and the probe height (up). */
int ign_ground_query(const IgnLevel *lv, int32_t sx, int32_t probe_up, int32_t sz, IgnGroundHit *hit);

/* Start pose from the first two TRI road sections. Returns 0 on success. */
int ign_level_start(const IgnLevel *lv, float *x, float *z, float *heading);

typedef struct {
    double x0, z0, x1, z1; /* world units */
} IgnWall;

/* sub_00033ee8: unshared edges of the static objects' drivable faces near (x, height up, z); jump mode (car+0x274)
 * widens the reach and adds faces 0x6e-0x83. Returns the count. */
int ign_collect_walls(const IgnLevel *lv, float x, float y, float z, int jump, IgnWall *out, int max);

#endif
