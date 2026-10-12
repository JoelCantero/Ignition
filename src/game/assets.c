#include "assets.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COL_SIZE 0x308
#define PIC_HEADER 0x40
#define PIC_MAGIC 0x95
#define CHUNK_HEADER 6

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

uint8_t *ign_read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *buf = NULL;
    long len;
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        buf = malloc(len ? (size_t)len : 1);
        if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) {
            free(buf);
            buf = NULL;
        }
        if (buf) *size = (size_t)len;
    }
    fclose(f);
    return buf;
}

int ign_parse_col(const uint8_t *data, size_t size, IgnPalette *out) {
    if (size < COL_SIZE || rd32(data) != COL_SIZE) return -1;
    const uint8_t *rgb = data + 8;
    int max = 0;
    for (int i = 0; i < 768; i++)
        if (rgb[i] > max) max = rgb[i];
    /* VGA DAC palettes are 6-bit; some files already store 8-bit values. */
    int shift = max < 64 ? 2 : 0;
    for (int i = 0; i < 256; i++)
        for (int c = 0; c < 3; c++)
            out->rgb[i][c] = (uint8_t)(rgb[i * 3 + c] << shift);
    return 0;
}

int ign_load_pic(const char *path, IgnPic *out) {
    size_t size;
    uint8_t *data = ign_read_file(path, &size);
    if (!data) return -1;

    int rc = -1;
    if (size < PIC_HEADER + COL_SIZE + CHUNK_HEADER || data[5] != PIC_MAGIC) goto done;
    int w = rd16(data + 6), h = rd16(data + 8);
    if (ign_parse_col(data + PIC_HEADER, size - PIC_HEADER, &out->palette)) goto done;

    const uint8_t *chunk = data + PIC_HEADER + COL_SIZE;
    size_t npix = (size_t)w * h;
    if (rd32(chunk) != npix + CHUNK_HEADER) goto done;
    if ((size_t)(chunk - data) + CHUNK_HEADER + npix > size) goto done;

    out->pixels = malloc(npix ? npix : 1);
    if (!out->pixels) goto done;
    memcpy(out->pixels, chunk + CHUNK_HEADER, npix);
    out->width = w;
    out->height = h;
    rc = 0;
done:
    free(data);
    return rc;
}

void ign_free_pic(IgnPic *pic) {
    free(pic->pixels);
    pic->pixels = NULL;
}

static uint8_t *read_or_warn(const char *path, size_t *size) {
    uint8_t *d = ign_read_file(path, size);
    if (!d) fprintf(stderr, "cannot read %s\n", path);
    return d;
}

int ign_load_model(const char *plc_path, const char *msh_path, const char *tex_path,
                   IgnModel *out) {
    memset(out, 0, sizeof *out);
    size_t tex_size, plc_size, msh_size;
    out->tex = read_or_warn(tex_path, &tex_size);
    out->plc = read_or_warn(plc_path, &plc_size);
    out->msh = read_or_warn(msh_path, &msh_size);
    int rc = -1;
    if (!out->tex || !out->plc || !out->msh || tex_size < 65536 || plc_size < 4) goto done;
    /* CARS.TEX carries 64 trailing bytes; only whole pages are used. */
    out->tex_h = (int)(tex_size / 65536) * 256;

    uint32_t n = rd32(out->plc);
    if (plc_size < 4 + (size_t)n * 20) goto done;
    out->objs = calloc(n ? n : 1, sizeof *out->objs);
    if (!out->objs) goto done;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *r = out->plc + 4 + i * 20;
        size_t off = (size_t)rd32(r) * 4;
        if (off + 8 > msh_size) goto done;
        IgnObject *o = &out->objs[i];
        o->type = rd32(r + 4);
        for (int k = 0; k < 3; k++) o->pos[k] = (int32_t)rd32(r + 8 + k * 4);
        const int32_t *m = (const int32_t *)(out->msh + off);
        o->nv = m[0];
        o->nf = m[1];
        if (o->nv < 0 || o->nf < 0 ||
            off + 8 + (size_t)o->nv * 12 + (size_t)o->nf * sizeof(IgnFace) > msh_size)
            goto done;
        o->verts = m + 2;
        o->faces = (const IgnFace *)(m + 2 + o->nv * 3);
        for (int f = 0; f < o->nf; f++)
            for (int k = 0; k < 3; k++)
                if (o->faces[f].v[k] < 0 || o->faces[f].v[k] >= o->nv) goto done;
    }
    out->nobj = (int)n;
    rc = 0;
done:
    if (rc) ign_free_model(out);
    return rc;
}

void ign_free_model(IgnModel *m) {
    free(m->tex);
    free(m->plc);
    free(m->msh);
    free(m->objs);
    memset(m, 0, sizeof *m);
}

/* The engine registers PLC objects in order but skips some decorations, so SRF object indices
 * drift from PLC indices. Rebuild the mapping: each engine object takes the next PLC object whose
 * mesh can hold every face offset SRF cites for it. */
static int map_engine_objects(IgnLevel *lv) {
    IgnSurface *s = &lv->surface;
    int maxe = -1;
    for (int r = 0; r < s->nspan; r++) {
        uint32_t ob = rd32(s->spans + (size_t)r * 24 + 20);
        if (ob % 42 == 0 && (int)(ob / 42) > maxe) maxe = (int)(ob / 42);
    }
    s->nengine = maxe + 1;
    s->engine_to_plc = malloc(sizeof(int) * (size_t)(s->nengine ? s->nengine : 1));
    uint32_t *lo = calloc((size_t)s->nengine + 1, sizeof *lo), *hi = calloc((size_t)s->nengine + 1, sizeof *hi);
    unsigned char *used = calloc((size_t)s->nengine + 1, 1);
    if (!s->engine_to_plc || !lo || !hi || !used) {
        free(lo), free(hi), free(used);
        return -1;
    }
    for (int e = 0; e < s->nengine; e++) lo[e] = UINT32_MAX;
    /* Track the min/max offsets and whether all offsets share one residue mod 11. */
    uint32_t *res = calloc((size_t)s->nengine + 1, sizeof *res);
    if (!res) {
        free(lo), free(hi), free(used);
        return -1;
    }
    for (int r = 0; r < s->nspan; r++) {
        const uint8_t *sp = s->spans + (size_t)r * 24;
        uint32_t fo = rd16(sp + 18), ob = rd32(sp + 20);
        if (ob % 42) continue;
        int e = (int)(ob / 42);
        if (!used[e]) res[e] = fo % 11;
        else if (res[e] != fo % 11) res[e] = UINT32_MAX;
        used[e] = 1;
        if (fo < lo[e]) lo[e] = fo;
        if (fo > hi[e]) hi[e] = fo;
    }
    int p = 0;
    for (int e = 0; e < s->nengine; e++) {
        s->engine_to_plc[e] = -1;
        if (!used[e]) continue;
        for (; p < lv->track.nobj; p++) {
            const IgnObject *o = &lv->track.objs[p];
            uint32_t first = 2 + (uint32_t)o->nv * 3;
            uint32_t last = first + ((uint32_t)o->nf - 1) * 11;
            if (o->nf > 0 && res[e] == first % 11 && lo[e] >= first && hi[e] <= last) break;
        }
        if (p < lv->track.nobj) s->engine_to_plc[e] = p++;
    }
    free(lo), free(hi), free(used), free(res);
    return 0;
}

int ign_load_level(const char *data_dir, const char *name, IgnLevel *out) {
    memset(out, 0, sizeof *out);
    char col[1024], pic[1024], plc[1024], msh[1024], tex[1024];
    snprintf(col, sizeof col, "%s/LEVELS/%s/%s.COL", data_dir, name, name);
    snprintf(pic, sizeof pic, "%s/LEVELS/%s/%s.PIC", data_dir, name, name);
    snprintf(plc, sizeof plc, "%s/LEVELS/%s/%s.PLC", data_dir, name, name);
    snprintf(msh, sizeof msh, "%s/LEVELS/%s/%s.MSH", data_dir, name, name);
    snprintf(tex, sizeof tex, "%s/LEVELS/%s/%s.TEX", data_dir, name, name);

    size_t col_size;
    uint8_t *col_data = read_or_warn(col, &col_size);
    int rc = -1;
    if (!col_data || ign_parse_col(col_data, col_size, &out->palette)) goto done;
    if (ign_load_pic(pic, &out->backdrop)) goto done;
    if (ign_load_model(plc, msh, tex, &out->track)) goto done;

    snprintf(plc, sizeof plc, "%s/CARS/CARS.PLC", data_dir);
    snprintf(msh, sizeof msh, "%s/CARS/CARS.MSH", data_dir);
    snprintf(tex, sizeof tex, "%s/CARS/CARS.TEX", data_dir);
    if (ign_load_model(plc, msh, tex, &out->cars)) goto done;

    snprintf(plc, sizeof plc, "%s/LEVELS/%s/%s.SRF", data_dir, name, name);
    IgnSurface *s = &out->surface;
    s->data = read_or_warn(plc, &s->size);
    if (!s->data || s->size < 0x24) goto done;
    s->cell_h = (int)rd32(s->data + 8);
    s->cell_w = (int)rd32(s->data + 12);
    s->gx = (int)rd32(s->data + 16);
    s->gz = (int)rd32(s->data + 20);
    s->nspan = (int)rd32(s->data + 24);
    s->na = (int)rd32(s->data + 28);
    s->nb = (int)rd32(s->data + 32);
    if (s->cell_w <= 0 || s->cell_h <= 0 || s->gx <= 0 || s->gz <= 0 || s->nspan < 0 ||
        s->na < 0 || s->nb < 0)
        goto done;
    size_t need = 0x24 + (size_t)s->gx * s->gz * 12 + (size_t)s->nspan * 24 +
                  ((size_t)s->na + s->nb) * 4;
    if (need != s->size) goto done;
    s->cells = s->data + 0x24;
    s->spans = s->cells + (size_t)s->gx * s->gz * 12;
    s->list_a = s->spans + (size_t)s->nspan * 24;
    s->list_b = s->list_a + (size_t)s->na * 4;
    if (map_engine_objects(out)) goto done;

    snprintf(plc, sizeof plc, "%s/LEVELS/%s/%s.TRI", data_dir, name, name);
    out->tri = read_or_warn(plc, &out->tri_size);
    if (!out->tri || out->tri_size < 4) goto done;
    rc = 0;
done:
    free(col_data);
    if (rc) ign_free_level(out);
    return rc;
}

void ign_free_level(IgnLevel *lv) {
    ign_free_pic(&lv->backdrop);
    ign_free_model(&lv->track);
    ign_free_model(&lv->cars);
    free(lv->surface.data);
    free(lv->surface.engine_to_plc);
    free(lv->tri);
    memset(lv, 0, sizeof *lv);
}

/* Midpoint of the two 22-byte edge records {u8, u16 vertex, i32 x, y, z, 8 bytes} of a TRI block. */
static int tri_midpoint(const IgnLevel *lv, int block, float *x, float *z) {
    if (block >= lv->track.nobj || 4 + (size_t)(block + 1) * 500 > lv->tri_size) return -1;
    const uint8_t *b = lv->tri + 4 + (size_t)block * 500;
    int32_t x0 = (int32_t)rd32(b + 3), z0 = (int32_t)rd32(b + 11);
    int32_t x1 = (int32_t)rd32(b + 25), z1 = (int32_t)rd32(b + 33);
    if (!x0 && !z0) return -1;
    *x = (x0 + x1) * 0.5f - IGN_SRF_ORIGIN;
    *z = (z0 + z1) * 0.5f - IGN_SRF_ORIGIN;
    return 0;
}

int ign_level_start(const IgnLevel *lv, float *x, float *z, float *heading) {
    float x1, z1;
    if (!lv->tri || tri_midpoint(lv, 0, x, z) || tri_midpoint(lv, 1, &x1, &z1)) return -1;
    *heading = atan2f(x1 - *x, z1 - *z);
    return 0;
}

/* Plane height of a face at world (x, z); returns 0 on success. */
static int face_height(const IgnObject *o, const IgnFace *f, float x, float z, float *y) {
    float p[3][3];
    for (int k = 0; k < 3; k++)
        for (int c = 0; c < 3; c++) p[k][c] = (float)(o->verts[f->v[k] * 3 + c] + o->pos[c]);
    float ux = p[1][0] - p[0][0], uy = p[1][1] - p[0][1], uz = p[1][2] - p[0][2];
    float vx = p[2][0] - p[0][0], vy = p[2][1] - p[0][1], vz = p[2][2] - p[0][2];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    float len = sqrtf(nx * nx + ny * ny + nz * nz);
    /* Deviation: near-vertical faces extrapolate to absurd heights, so they are not ground. */
    if (len == 0 || fabsf(ny) < 0.17f * len) return -1;
    *y = p[0][1] - (nx * (x - p[0][0]) + nz * (z - p[0][2])) / ny;
    return 0;
}

static int wall_type(int t) {
    return t >= 0x28 && t <= 0x4f;
}

/* Mean vertex height of a face, up positive (sub_00020bbc). */
static float face_avg_up(const IgnObject *o, const IgnFace *f) {
    int32_t s = 0;
    for (int k = 0; k < 3; k++) s += o->verts[f->v[k] * 3 + 1];
    return -(float)(s / 3) - (float)o->pos[1];
}

static void check_list(const IgnLevel *lv, const uint8_t *list, int off, int n, int descending,
                       int32_t sx, int32_t sz, float x, float z, float query_up, IgnGroundHit *best,
                       float *best_d, int *best_type) {
    const IgnSurface *s = &lv->surface;
    for (int k = 0; k < n; k++) {
        size_t at = (size_t)off + (size_t)k * 4;
        if (off < 0 || at + 4 > (size_t)(s->list_b + (size_t)s->nb * 4 - list)) return;
        uint32_t span_off = rd32(list + at);
        if (span_off % 24 || span_off / 24 >= (uint32_t)s->nspan) continue;
        const uint8_t *sp = s->spans + span_off;
        int32_t x0 = (int32_t)rd32(sp), z0 = (int32_t)rd32(sp + 4);
        int32_t sl = (int32_t)rd32(sp + 8), sr = (int32_t)rd32(sp + 12);
        int32_t dz = (int16_t)rd16(sp + 16);
        uint32_t fo = rd16(sp + 18), ob = rd32(sp + 20);
        if (descending ? (sz > z0 || z0 + dz > sz) : (sz < z0 || z0 + dz < sz)) continue;
        int64_t t = (int64_t)sz - z0;
        if (x0 + (int32_t)((t * sl) >> 16) > sx || x0 + (int32_t)((t * sr) >> 16) < sx) continue;

        /* Object references are offsets into the engine's 42-byte object pool. */
        if (ob % 42 || ob / 42 >= (uint32_t)s->nengine || s->engine_to_plc[ob / 42] < 0) continue;
        int plc = s->engine_to_plc[ob / 42];
        const IgnObject *o = &lv->track.objs[plc];
        int64_t rel = (int64_t)fo - 2 - (int64_t)o->nv * 3;
        if (rel < 0 || rel % 11 || rel / 11 >= o->nf) continue;
        const IgnFace *f = &o->faces[rel / 11];
        float y;
        if (face_height(o, f, x, z, &y)) continue;
        /* Faces below the probe cost their distance, faces above its square; walls only as fallback. */
        float d = face_avg_up(o, f) - query_up;
        float metric = d < 0 ? -d : d * d;
        int type = (int)(f->flags >> 16);
        if ((metric < *best_d && !wall_type(type)) ||
            (wall_type(*best_type) && !(wall_type(type) && metric >= *best_d))) {
            *best_d = metric;
            *best_type = type;
            best->y = y;
            best->surface_type = type;
            best->object = plc;
            best->face = (int)(rel / 11);
        }
    }
}

int ign_ground_at(const IgnLevel *lv, float x, float z, float y_hint, IgnGroundHit *hit) {
    const IgnSurface *s = &lv->surface;
    if (!s->data) return -1;
    int32_t sx = (int32_t)floorf(x) + IGN_SRF_ORIGIN, sz = (int32_t)floorf(z) + IGN_SRF_ORIGIN;
    if (sx < 0 || sz < 0 || sx / s->cell_w >= s->gx || sz / s->cell_h >= s->gz) return -1;
    const uint8_t *c = s->cells + ((size_t)(sz / s->cell_h) * s->gx + sx / s->cell_w) * 12;
    uint32_t count = rd32(c + 8);
    float best_d = 1e9f, query_up = -y_hint + 50.0f;
    int best_type = 0x4f;
    hit->object = -1;
    check_list(lv, s->list_b, (int32_t)rd32(c), (int)(count >> 16), 0, sx, sz, x, z, query_up, hit,
               &best_d, &best_type);
    check_list(lv, s->list_a, (int32_t)rd32(c + 4), (int)(count & 0xFFFF), 1, sx, sz, x, z,
               query_up, hit, &best_d, &best_type);
    return hit->object < 0 ? -1 : 0;
}

#define WALL_RADIUS 600
#define WALL_HEIGHT 350 /* 0x15e for most track styles */
#define WALL_SHARED_EPS 16

static int drivable(uint32_t type) {
    return type < 0x28 || type == 0x64 || type == 0x65;
}

int ign_collect_walls(const IgnLevel *lv, float x, float y, float z, IgnWall *out, int max) {
    enum { MAX_EDGES = 2048 };
    static IgnWall edges[MAX_EDGES];
    static uint8_t shared[MAX_EDGES];
    int n = 0;
    for (int oi = 0; oi < lv->track.nobj; oi++) {
        const IgnObject *o = &lv->track.objs[oi];
        if (fabsf(x - (float)o->pos[0]) >= WALL_RADIUS || fabsf(y - (float)o->pos[1]) >= WALL_HEIGHT ||
            fabsf(z - (float)o->pos[2]) >= WALL_RADIUS)
            continue;
        for (int fi = 0; fi < o->nf; fi++) {
            const IgnFace *f = &o->faces[fi];
            if (!drivable(f->flags >> 16)) continue;
            for (int k = 0; k < 3 && n < MAX_EDGES; k++) {
                const int32_t *a = o->verts + f->v[k] * 3, *b = o->verts + f->v[(k + 1) % 3] * 3;
                edges[n] = (IgnWall){a[0] + o->pos[0], a[2] + o->pos[2], b[0] + o->pos[0],
                                     b[2] + o->pos[2]};
                shared[n++] = 0;
            }
        }
    }
    /* An edge used in reverse by another face is interior, not a wall. */
    for (int i = 0; i < n; i++) {
        if (shared[i]) continue;
        for (int j = i + 1; j < n; j++) {
            if (fabs(edges[i].x0 - edges[j].x1) < WALL_SHARED_EPS &&
                fabs(edges[i].z0 - edges[j].z1) < WALL_SHARED_EPS &&
                fabs(edges[i].x1 - edges[j].x0) < WALL_SHARED_EPS &&
                fabs(edges[i].z1 - edges[j].z0) < WALL_SHARED_EPS) {
                shared[i] = shared[j] = 1;
                break;
            }
        }
    }
    int m = 0;
    for (int i = 0; i < n && m < max; i++)
        if (!shared[i]) out[m++] = edges[i];
    return m;
}
