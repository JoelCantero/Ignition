#include "assets.h"
#include "pos.h"
#include "track.h"

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
        /* PLC y points up: the original builds world y as vertex.y - plc.y (0x20a2b, 0x28f77). */
        o->pos[1] = -o->pos[1];
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

static int32_t engine_cell(const IgnObject *o) {
    int32_t x = o->pos[0] + IGN_SRF_ORIGIN, z = o->pos[2] + IGN_SRF_ORIGIN;
    return (z / 256) * IGN_ENGINE_GRID + x / 256; /* sar with rounding toward zero */
}

int ign_engine_place(IgnModel *m) {
    free(m->cell), free(m->stamp);
    m->cell = malloc(sizeof *m->cell * (size_t)(m->nobj ? m->nobj : 1));
    m->stamp = malloc(sizeof *m->stamp * (size_t)(m->nobj ? m->nobj : 1));
    if (!m->cell || !m->stamp) return -1;
    m->clock = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < m->nobj; i++)
            if (((m->objs[i].type & 0xfff) < 100) == (pass == 0))
                m->cell[i] = engine_cell(&m->objs[i]), m->stamp[i] = ++m->clock;
    return 0;
}

void ign_engine_moved(IgnModel *m, int obj) {
    if (!m->cell) return;
    int32_t c = engine_cell(&m->objs[obj]);
    if (c != m->cell[obj]) m->cell[obj] = c, m->stamp[obj] = ++m->clock;
}

/* 0xc5d8c: +1/+2 row and column of the first row, then (cells, next row shift) pairs until -5000. With the view
 * (850 x 0.01, 0) the table 0xc5e40 adds one row. */
static const int16_t VIEW_ROWS[] = {
    8, -10, 12, -13, 14, -15, 16, -17, 18, -18, 18, -19, 20, -20, 19, -19, 19, -19, 19, -19, 19, -19, 19, -19, 19,
    -19, 19, -18, 17, -17, 16, -15, 14, -13, 12, -11, 10, -8, 2
};

int ign_engine_view(const IgnModel *m, int32_t x, int32_t z, int layer, int *out, int max) {
    if (!m->cell) return 0;
    int n = 0;
    int32_t at = (z / 256 - 10 + 1) * IGN_ENGINE_GRID + x / 256 - 4;
    size_t nrows = sizeof VIEW_ROWS / sizeof VIEW_ROWS[0];
    for (size_t r = 0; r < nrows; r += 2) {
        for (int k = 0; k < VIEW_ROWS[r]; k++) {
            at++;
            if (at < 0 || at >= IGN_ENGINE_GRID * IGN_ENGINE_GRID) continue;
            /* The cell's chain: most recent insertion first. */
            int first = n;
            for (int i = 0; i < m->nobj; i++) {
                if (m->cell[i] != at) continue;
                int l = (int)((m->objs[i].type >> 12) & 0xf);
                if (layer && l && l != layer) continue;
                if (n < max) out[n++] = i;
            }
            for (int a = first + 1; a < n; a++)
                for (int b = a; b > first && m->stamp[out[b]] > m->stamp[out[b - 1]]; b--) {
                    int t = out[b];
                    out[b] = out[b - 1], out[b - 1] = t;
                }
        }
        if (r + 1 < nrows) at += IGN_ENGINE_GRID + VIEW_ROWS[r + 1];
    }
    return n;
}

void ign_free_model(IgnModel *m) {
    free(m->cell);
    free(m->stamp);
    free(m->tex);
    free(m->plc);
    free(m->msh);
    free(m->objs);
    memset(m, 0, sizeof *m);
}

void ign_object_rotation_matrix(const IgnObject *o, float out[9]) {
    const float scale = 3.14159265358979323846f / 1800.0f;
    float ax = o->rotation[0] * scale, ay = o->rotation[1] * scale, az = o->rotation[2] * scale;
    float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay), cz = cosf(az), sz = sinf(az);
    /* The Euler order is not established from the engine; use Z(Y(X(v))) as a rendering approximation. */
    out[0] = cz * cy;
    out[1] = -sz * cx + cz * sy * sx;
    out[2] = sz * sx + cz * sy * cx;
    out[3] = sz * cy;
    out[4] = cz * cx + sz * sy * sx;
    out[5] = -cz * sx + sz * sy * cx;
    out[6] = -sy;
    out[7] = cy * sx;
    out[8] = cy * cx;
}

void ign_object_vertex(const IgnObject *o, int vertex, float out[3]) {
    float rotation[9];
    ign_object_rotation_matrix(o, rotation);
    const int32_t *v = o->verts + vertex * 3;
    for (int row = 0; row < 3; row++)
        out[row] = rotation[row * 3] * v[0] + rotation[row * 3 + 1] * v[1] +
                   rotation[row * 3 + 2] * v[2] + o->pos[row];
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

static int wall_height(const char *name);

int ign_load_level(const char *data_dir, const char *name, IgnLevel *out) {
    memset(out, 0, sizeof *out);
    char col[1024], pic[1024], plc[1024], msh[1024], tex[1024], pos[1024];
    snprintf(col, sizeof col, "%s/LEVELS/%s/%s.COL", data_dir, name, name);
    snprintf(pic, sizeof pic, "%s/LEVELS/%s/%s.PIC", data_dir, name, name);
    snprintf(plc, sizeof plc, "%s/LEVELS/%s/%s.PLC", data_dir, name, name);
    snprintf(msh, sizeof msh, "%s/LEVELS/%s/%s.MSH", data_dir, name, name);
    snprintf(tex, sizeof tex, "%s/LEVELS/%s/%s.TEX", data_dir, name, name);
    snprintf(pos, sizeof pos, "%s/LEVELS/%s/%s.POS", data_dir, name, name);

    size_t col_size;
    uint8_t *col_data = read_or_warn(col, &col_size);
    int rc = -1;
    if (!col_data || ign_parse_col(col_data, col_size, &out->palette)) goto done;
    if (ign_load_pic(pic, &out->backdrop)) goto done;
    if (ign_load_model(plc, msh, tex, &out->track)) goto done;
    if (ign_track_recenter_model(&out->track)) goto done;
    if (ign_engine_place(&out->track)) goto done;
    if (ign_pos_load(pos, out->track.nobj, &out->pos)) {
        fprintf(stderr, "invalid or unreadable POS animation file %s\n", pos);
        goto done;
    }

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
    out->wall_height = wall_height(name);

    snprintf(plc, sizeof plc, "%s/LEVELS/%s/%s.SHD", data_dir, name, name);
    size_t shd_size;
    out->shd = read_or_warn(plc, &shd_size);
    if (!out->shd || shd_size != 65536) goto done;

    snprintf(plc, sizeof plc, "%s/LEVELS/%s/%s.TAB", data_dir, name, name);
    size_t tab_size;
    out->tab = read_or_warn(plc, &tab_size);
    if (!out->tab || tab_size != 65536) goto done;

    /* Effect texture slots 2-7 (0x23c9c); DARKSMOK.TEX has only 128 rows, padded to a full page. */
    static const char *const FX_TEX[8] = {NULL, NULL, "DIVSPR", "LIGHT", "SMOKE", "DARKSMOK", "BOOM", "EXSMOKE"};
    for (int i = 2; i < 8; i++) {
        snprintf(plc, sizeof plc, "%s/GENERAL/%s.TEX", data_dir, FX_TEX[i]);
        size_t size;
        uint8_t *tex = read_or_warn(plc, &size);
        if (!tex || size > 65536) {
            free(tex);
            goto done;
        }
        out->fx_tex[i] = calloc(1, 65536);
        if (!out->fx_tex[i]) {
            free(tex);
            goto done;
        }
        memcpy(out->fx_tex[i], tex, size);
        free(tex);
    }
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
    ign_pos_free(&lv->pos);
    free(lv->surface.data);
    free(lv->surface.engine_to_plc);
    free(lv->tri);
    free(lv->shd);
    free(lv->tab);
    for (int i = 0; i < 8; i++) free(lv->fx_tex[i]);
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

void ign_object_point(const IgnObject *o, int vertex, int32_t out[3]) {
    const int32_t *v = o->verts + vertex * 3;
    out[0] = v[0] + o->pos[0] + IGN_SRF_ORIGIN;
    out[1] = v[1] + o->pos[1];
    out[2] = v[2] + o->pos[2] + IGN_SRF_ORIGIN;
}

/* Plane height of a face at world (x, z); returns 0 on success. */
static int face_height(const IgnObject *o, const IgnFace *f, float x, float z, float *y) {
    float p[3][3];
    for (int k = 0; k < 3; k++) {
        int32_t q[3];
        ign_object_point(o, f->v[k], q);
        p[k][0] = (float)(q[0] - IGN_SRF_ORIGIN), p[k][1] = (float)q[1], p[k][2] = (float)(q[2] - IGN_SRF_ORIGIN);
    }
    float ux = p[1][0] - p[0][0], uy = p[1][1] - p[0][1], uz = p[1][2] - p[0][2];
    float vx = p[2][0] - p[0][0], vy = p[2][1] - p[0][1], vz = p[2][2] - p[0][2];
    float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    float len = sqrtf(nx * nx + ny * ny + nz * nz);
    if (len == 0 || fabsf(ny) < 1e-4f * len) return -1;
    *y = p[0][1] - (nx * (x - p[0][0]) + nz * (z - p[0][2])) / ny;
    /* Inside the face's x/z projection (the SRF span) the plane height lies between its vertices; the clamp only
     * guards the rounding of spans on near-vertical faces (cliffs). */
    float lo = fminf(p[0][1], fminf(p[1][1], p[2][1])), hi = fmaxf(p[0][1], fmaxf(p[1][1], p[2][1]));
    *y = *y < lo ? lo : *y > hi ? hi : *y;
    return 0;
}

static int wall_type(int t) {
    return t >= 0x28 && t <= 0x4f;
}

/* face_avg_height 0x20bbc: integer mean of the vertices' heights (up) plus the object's current height. */
static int32_t face_avg_up(const IgnObject *o, const IgnFace *f) {
    int32_t sum = 0;
    for (int k = 0; k < 3; k++) sum -= o->verts[f->v[k] * 3 + 1];
    return sum / 3 - o->pos[1];
}

/* sub_00020814 0x20990-0x20b9c: the chosen face's normal (p0 - p1) x (p2 - p1) and points, engine coordinates. */
static void face_plane(const IgnObject *o, const IgnFace *f, IgnGroundHit *hit) {
    for (int k = 0; k < 3; k++) ign_object_point(o, f->v[k], hit->p[k]);
    const int32_t *u = hit->p[0], *v = hit->p[1], *w = hit->p[2];
    int32_t ax = u[0] - v[0], ay = u[1] - v[1], az = u[2] - v[2];
    int32_t bx = w[0] - v[0], by = w[1] - v[1], bz = w[2] - v[2];
    hit->normal[0] = (int32_t)((uint32_t)ay * (uint32_t)bz - (uint32_t)by * (uint32_t)az);
    hit->normal[1] = (int32_t)((uint32_t)az * (uint32_t)bx - (uint32_t)bz * (uint32_t)ax);
    hit->normal[2] = (int32_t)((uint32_t)ax * (uint32_t)by - (uint32_t)bx * (uint32_t)ay);
}

/* 0x20c18 / 0x20c81: imul discards the upper word before the arithmetic shift, not afterwards. */
int ign_surface_span_contains(const uint8_t span[24], int descending, int32_t x, int32_t z) {
    int32_t z0 = (int32_t)rd32(span + 4);
    int32_t end = (int32_t)((uint32_t)z0 + (uint32_t)(int32_t)(int16_t)rd16(span + 16));
    if (descending ? (z > z0 || end > z) : (z < z0 || end < z)) return 0;
    uint32_t delta = (uint32_t)z - (uint32_t)z0;
    int32_t edge[2];
    for (int k = 0; k < 2; k++) {
        uint32_t product = delta * rd32(span + 8 + k * 4);
        uint32_t shifted = product >> 16;
        if (product & 0x80000000u) shifted |= 0xFFFF0000u;
        edge[k] = (int32_t)(rd32(span) + shifted);
    }
    return x >= edge[0] && x <= edge[1];
}

static void check_list(const IgnLevel *lv, const uint8_t *list, int off, int n, int descending,
                       int32_t sx, int32_t sz, int32_t probe_up, IgnGroundHit *best, int32_t *best_d,
                       int *best_type, const IgnFace **best_face) {
    const IgnSurface *s = &lv->surface;
    for (int k = 0; k < n; k++) {
        size_t at = (size_t)off + (size_t)k * 4;
        if (off < 0 || at + 4 > (size_t)(s->list_b + (size_t)s->nb * 4 - list)) return;
        uint32_t span_off = rd32(list + at);
        if (span_off % 24 || span_off / 24 >= (uint32_t)s->nspan) continue;
        const uint8_t *sp = s->spans + span_off;
        uint32_t fo = rd16(sp + 18), ob = rd32(sp + 20);
        if (!ign_surface_span_contains(sp, descending, sx, sz)) continue;

        /* Object references are offsets into the engine's 42-byte object pool. */
        if (ob % 42 || ob / 42 >= (uint32_t)s->nengine || s->engine_to_plc[ob / 42] < 0) continue;
        int plc = s->engine_to_plc[ob / 42];
        const IgnObject *o = &lv->track.objs[plc];
        int64_t rel = (int64_t)fo - 2 - (int64_t)o->nv * 3;
        if (rel < 0 || rel % 11 || rel / 11 >= o->nf) continue;
        const IgnFace *f = &o->faces[rel / 11];
        /* 0x20907: faces below the probe cost their distance, faces above its square (imul, wrapping); walls only
         * win when nothing else does. */
        int32_t d = (int32_t)((uint32_t)face_avg_up(o, f) - (uint32_t)probe_up);
        int32_t metric = d < 0 ? -d : (int32_t)((uint32_t)d * (uint32_t)d);
        int type = (int)(f->flags >> 16);
        if ((metric < *best_d && !wall_type(type)) ||
            (wall_type(*best_type) && !(wall_type(type) && metric >= *best_d))) {
            *best_d = metric;
            *best_type = type;
            best->surface_type = type;
            best->object = plc;
            best->face = (int)(rel / 11);
            *best_face = f;
        }
    }
}

int ign_ground_query(const IgnLevel *lv, int32_t sx, int32_t probe_up, int32_t sz, IgnGroundHit *hit) {
    const IgnSurface *s = &lv->surface;
    hit->object = -1;
    if (!s->data || sx < 0 || sz < 0 || sx / s->cell_w >= s->gx || sz / s->cell_h >= s->gz) return -1;
    const uint8_t *c = s->cells + ((size_t)(sz / s->cell_h) * s->gx + sx / s->cell_w) * 12;
    uint32_t count = rd32(c + 8);
    int32_t best_d = 1000000000;
    int best_type = 0x4f;
    const IgnFace *face = NULL;
    check_list(lv, s->list_b, (int32_t)rd32(c), (int)(count >> 16), 0, sx, sz, probe_up, hit, &best_d, &best_type,
               &face);
    check_list(lv, s->list_a, (int32_t)rd32(c + 4), (int)(count & 0xFFFF), 1, sx, sz, probe_up, hit, &best_d,
               &best_type, &face);
    if (hit->object < 0) return -1;
    face_plane(&lv->track.objs[hit->object], face, hit);
    return 0;
}

int ign_ground_at(const IgnLevel *lv, float x, float z, float y_hint, IgnGroundHit *hit) {
    int32_t sx = (int32_t)floorf(x) + IGN_SRF_ORIGIN, sz = (int32_t)floorf(z) + IGN_SRF_ORIGIN;
    /* car_update 0x2ef41: the probe is trunc(height + 50). */
    if (ign_ground_query(lv, sx, (int32_t)trunc(-(double)y_hint + 50.0), sz, hit)) return -1;
    const IgnObject *o = &lv->track.objs[hit->object];
    return face_height(o, &o->faces[hit->face], x, z, &hit->y) ? (hit->object = -1, -1) : 0;
}

double ign_ground_plane_up(const IgnGroundHit *hit, double x, double z) {
    const int32_t *p0 = hit->p[0], *p1 = hit->p[1];
    double a = hit->normal[0], b = hit->normal[1], c = hit->normal[2];
    double e1x = (double)p1[0] - p0[0], e1y = (double)p1[1] - p0[1], e1z = (double)p1[2] - p0[2];
    double dx = x - p0[0], dz = z - p0[2];
    double t = (-a * dx + a * e1x - c * dz + c * e1z + b * e1y) / b;
    return -(p0[1] + t);
}

/* 0x32feb / 0x33aa8: crossing test of (px, pz) on the x/z of three vertices. */
static int covers(const int32_t *v0, const int32_t *v1, const int32_t *v2, int32_t px, int32_t pz) {
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

static int32_t last_list_type = -1; /* [0x1ff970] between calls */

void ign_ground_list_state(int32_t type) {
    last_list_type = type;
}

int ign_ground_list_query(const IgnLevel *lv, int32_t x, int32_t y, int32_t z, int jump, int layer, int32_t probe,
                          IgnGroundHit *hit) {
    static int list[8192];
    int n = ign_engine_view(&lv->track, x, z, layer, list, 8192);
    return ign_ground_list_query_in(lv, list, n, x, y, z, jump, probe, hit);
}

int ign_ground_list_query_in(const IgnLevel *lv, const int *list, int n, int32_t x, int32_t y, int32_t z, int jump,
                             int32_t probe, IgnGroundHit *hit) {
    enum { MAX_HITS = 8 };
    struct {
        int type, obj;
        int32_t o[3], v[3][3];
    } rec[MAX_HITS];
    int nrec = 0, special = 0;
    int32_t reach = jump ? 900 : 600;
    hit->object = -1;
    for (int k = 0; k < n; k++) {
        const IgnObject *o = &lv->track.objs[list[k]];
        int32_t ox = o->pos[0] + IGN_SRF_ORIGIN, oy = -o->pos[1], oz = o->pos[2] + IGN_SRF_ORIGIN;
        int32_t dx = x - ox, dy = y - oy, dz = z - oz;
        int klass = (int)(o->type & 0xfff);
        if (klass < 100) {
            if (abs(dx) >= reach || abs(dz) >= reach) continue;
            for (int f = 0; f < o->nf; f++) {
                const IgnFace *face = &o->faces[f];
                const int32_t *v0 = o->verts + face->v[0] * 3, *v1 = o->verts + face->v[1] * 3,
                              *v2 = o->verts + face->v[2] * 3;
                if (abs((v0[0] + v1[0] + v2[0]) / 3 - dx) >= 200 || abs((v0[2] + v1[2] + v2[2]) / 3 - dz) >= 200)
                    continue;
                if (!covers(v0, v1, v2, dx, dz)) continue;
                int type = (int)(face->flags >> 16);
                if (type >= 0x28 && type <= 0x4f) continue;
                /* 0x3324e: the original logs past 8 hits and writes on; the port keeps the first 8. */
                if (nrec == MAX_HITS) continue;
                rec[nrec].type = type, rec[nrec].obj = list[k];
                rec[nrec].o[0] = ox, rec[nrec].o[1] = oy, rec[nrec].o[2] = oz;
                for (int a = 0; a < 3; a++)
                    rec[nrec].v[0][a] = v0[a], rec[nrec].v[1][a] = v1[a], rec[nrec].v[2][a] = v2[a];
                nrec++;
            }
        } else if (klass >= 0x12c && klass < 0x15e && last_list_type >= 0 && last_list_type < 0x15e) {
            if (abs(dx) >= reach || abs(dz) >= reach || abs(dy) >= 190) continue;
            for (int f = 0; f < o->nf; f++) {
                const IgnFace *face = &o->faces[f];
                if (!covers(o->verts + face->v[0] * 3, o->verts + face->v[1] * 3, o->verts + face->v[2] * 3, dx,
                            dz))
                    continue;
                last_list_type = klass;
                hit->surface_type = klass, hit->object = list[k], hit->face = f;
                for (int c = 0; c < 3; c++) {
                    const int32_t *v = o->verts + face->v[c] * 3;
                    hit->p[c][0] = v[0] + ox, hit->p[c][1] = v[1] - oy, hit->p[c][2] = v[2] + oz;
                }
                hit->normal[0] = hit->normal[1] = hit->normal[2] = 0; /* 0x33463: not computed for objects */
                special = 1;
                break;
            }
        }
    }
    if (special) return 0;
    /* 0x335b6: the face nearest below the probe (first strict minimum, non-vertical). */
    int best = -1;
    int32_t best_d = 1000000;
    for (int r = 0; r < nrec; r++) {
        const int32_t(*v)[3] = rec[r].v;
        int32_t ax = v[0][0] - v[1][0], az = v[0][2] - v[1][2], bx = v[2][0] - v[1][0], bz = v[2][2] - v[1][2];
        int32_t d = ((v[0][1] - rec[r].o[1]) + (v[1][1] - rec[r].o[1]) + (v[2][1] - rec[r].o[1])) / 3 + y + probe;
        if (d < best_d && d >= 0 && (int32_t)((uint32_t)az * (uint32_t)bx - (uint32_t)bz * (uint32_t)ax) != 0)
            best_d = d, best = r;
    }
    if (best < 0) {
        last_list_type = -1;
        return -1;
    }
    const int32_t(*v)[3] = rec[best].v;
    int32_t ax = v[0][0] - v[1][0], ay = v[0][1] - v[1][1], az = v[0][2] - v[1][2];
    int32_t bx = v[2][0] - v[1][0], by = v[2][1] - v[1][1], bz = v[2][2] - v[1][2];
    hit->normal[0] = (int32_t)((uint32_t)bz * (uint32_t)ay - (uint32_t)az * (uint32_t)by);
    hit->normal[1] = (int32_t)((uint32_t)az * (uint32_t)bx - (uint32_t)ax * (uint32_t)bz);
    hit->normal[2] = (int32_t)((uint32_t)by * (uint32_t)ax - (uint32_t)ay * (uint32_t)bx);
    for (int c = 0; c < 3; c++)
        hit->p[c][0] = v[c][0] + rec[best].o[0], hit->p[c][1] = v[c][1] - rec[best].o[1],
        hit->p[c][2] = v[c][2] + rec[best].o[2];
    hit->surface_type = rec[best].type, hit->object = rec[best].obj, hit->face = -1;
    last_list_type = rec[best].type;
    hit->y = (float)-ign_ground_plane_up(hit, x, z);
    return 0;
}

#define WALL_RADIUS 600
#define WALL_RADIUS_JUMP 700
#define WALL_HEIGHT_JUMP 2500
#define WALL_SHARED_EPS 16

static int wall_face(uint32_t type, int jump) {
    return type < 0x28 || type == 0x64 || type == 0x65 || (jump && type >= 0x6e && type <= 0x83);
}

/* collect_walls 0x33f1b: vertical reach by track id. */
static int wall_height(const char *name) {
    static const struct {
        const char *name;
        int height;
    } t[] = {{"CANADA", 0x15e}, {"USA", 0x15e}, {"CARIB", 0xc8},  {"BRAZIL", 0x1c2},
             {"AUSTRIA", 0x15e}, {"ICELAND", 0x15e}, {"JAPAN", 0x7d}};
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        if (!strcmp(name, t[i].name)) return t[i].height;
    return 0x15e;
}

int ign_collect_walls(const IgnLevel *lv, float x, float y, float z, int jump, IgnWall *out, int max) {
    enum { MAX_EDGES = 2048 };
    static IgnWall edges[MAX_EDGES];
    static uint8_t shared[MAX_EDGES];
    int n = 0;
    int radius = jump ? WALL_RADIUS_JUMP : WALL_RADIUS;
    int height = jump ? WALL_HEIGHT_JUMP : lv->wall_height;
    int32_t ix = (int32_t)x, iy = (int32_t)y, iz = (int32_t)z;
    for (int oi = 0; oi < lv->track.nobj; oi++) {
        const IgnObject *o = &lv->track.objs[oi];
        /* 0x34027 adds the engine object's y (= raw recentered PLC y = -pos[1]) to car height. */
        if (abs(ix - o->pos[0]) >= radius || abs(iy - o->pos[1]) >= height || abs(iz - o->pos[2]) >= radius ||
            (o->type & 0xfff) >= 100)
            continue;
        for (int fi = 0; fi < o->nf; fi++) {
            const IgnFace *f = &o->faces[fi];
            if (!wall_face(f->flags >> 16, jump)) continue;
            for (int k = 0; k < 3 && n < MAX_EDGES; k++) {
                float a[3], b[3];
                ign_object_vertex(o, f->v[k], a);
                ign_object_vertex(o, f->v[(k + 1) % 3], b);
                edges[n] = (IgnWall){a[0], a[2], b[0], b[2]};
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
