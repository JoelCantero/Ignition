#include "pfm.h"

#include <stdlib.h>
#include <string.h>

/* pfm_load 0x549b0 (+0x54b44, 0x54bf0, 0x54cd0), pfm_build_frame 0x54df8, raster_draw_list 0x60e3d and the handlers of
 * table 0xd7bd0 for the PFM types: 7 sprite (0x704a0), 11 line (0x7072f), 15 flat (0x729f3), 17 textured (0x73c58),
 * 18 textured through a shade table (0x744c8) and 19 flat through a shade table (0x74d04), with the edge and gradient
 * code at 0x90000..0x90a7c and the span loops 0x824b4 / 0x825b8. Checked pixel for pixel against the original code
 * run in an emulator: all 209 frames of TEST2.PFM and random single primitives of every type. Arithmetic is 32-bit
 * two's complement as in the original, coordinates are 24.8 fixed point. */

#define MAX_ROWS 1024

typedef struct {
    int32_t uv[6]; /* 8.8 texels */
    int page;      /* index into pages, clamped to 0..30 */
} TexDef;

typedef struct {
    int32_t ax, ay, u0, v0, u1, v1; /* << 8 */
    int page, shade;
} Sprite;

typedef struct {
    size_t li00, li01; /* data offsets */
    int nverts;
} Frame;

struct IgnPfm {
    const uint8_t *data;
    size_t size;
    const uint8_t *pages[32];
    int npages;
    const uint8_t *shades[16];
    int nshades;
    Frame *frames;
    int nframes;
    TexDef *texdefs;
    int ntexdefs;
    Sprite *sprites;
    int nsprites;
    int32_t *verts; /* x, y pairs of the frame being drawn */
    int maxverts;
    IgnPfmWorkspace workspace;
    /* raster state of 0x60e3d */
    uint8_t *fb;
    int pitch, minx, miny, maxx, maxy;
    int32_t miny8, minx8, r8, b8;
    const uint8_t *lut;
    int32_t recip[0x3a9a]; /* 0xc9168 */
};

static int32_t S32(int64_t x) { return (int32_t)(uint32_t)(uint64_t)x; }
static int32_t HI32(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 32); }
static int32_t MID32(int32_t a, int32_t b) { return S32(((int64_t)a * b) >> 16); }
static uint32_t FRAC(int32_t n, int32_t d) {
    uint32_t den = (uint32_t)d;
    return den ? (uint32_t)((((uint64_t)(uint32_t)n) << 32) / den) : 0xffffffffu;
}
static int32_t RATIO(int32_t n, int32_t d) { return (int32_t)(FRAC(n, d) >> 1); }
static int32_t IDIV(int64_t n, int32_t d) { return d ? S32(n / d) : 0; }
static int32_t SAR(int32_t x, int n) { return x >> n; }
static int16_t rds16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static uint16_t rdu16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static int32_t rd32(const uint8_t *p) {
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

void ign_pfm_workspace_include(IgnPfmWorkspace *workspace, const uint8_t header[8]) {
    uint32_t n = (uint32_t)rd32(header + 4);
    int32_t *maximum;
    int32_t candidate;
    if (!memcmp(header, "li00", 4)) {
        maximum = &workspace->primitives;
        candidate = S32((uint64_t)n * 6);
    } else if (!memcmp(header, "li01", 4)) {
        maximum = &workspace->vertices;
        candidate = S32((uint64_t)n * 2);
    } else {
        return;
    }
    if (candidate > *maximum) *maximum = candidate;
}

IgnPfmWorkspace ign_pfm_workspace(const IgnPfm *p) {
    return p ? p->workspace : (IgnPfmWorkspace){0, 0};
}

IgnPfm *ign_pfm_load(const uint8_t *data, size_t size, uint8_t *const *pages, const uint8_t *const *shades) {
    if (!data) return NULL;
    IgnPfm *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    p->data = data;
    p->size = size;
    for (p->npages = 0; p->npages < 31 && pages[p->npages]; p->npages++) p->pages[p->npages] = pages[p->npages];
    for (p->nshades = 0; p->nshades < 16 && shades[p->nshades]; p->nshades++) p->shades[p->nshades] = shades[p->nshades];
    for (int k = 0; k < 0x3a9a; k++) p->recip[k] = 0x10000 / (k + 1) - 1;
    size_t o = 0, vblk = 0;
    int vcount = 0, cap = 0;
    while (o + 8 <= size && memcmp(data + o, "lixx", 4)) {
        int32_t n = rd32(data + o + 4);
        size_t d = o + 8;
        if (n < 0 || d + (size_t)n > size) break;
        ign_pfm_workspace_include(&p->workspace, data + o);
        if (!memcmp(data + o, "li01", 4)) {
            vblk = d;
            vcount = n / 4;
            if (vcount > p->maxverts) p->maxverts = vcount;
        } else if (!memcmp(data + o, "li00", 4)) {
            if (p->nframes == cap) {
                cap = cap ? cap * 2 : 256;
                Frame *f = realloc(p->frames, sizeof *f * (size_t)cap);
                if (!f) goto fail;
                p->frames = f;
            }
            p->frames[p->nframes++] = (Frame){d, vblk, vcount};
        } else if (!memcmp(data + o, "li02", 4)) {
            free(p->texdefs);
            p->ntexdefs = n / 16;
            p->texdefs = calloc((size_t)p->ntexdefs + 1, sizeof *p->texdefs);
            if (!p->texdefs) goto fail;
            for (int i = 0; i < p->ntexdefs; i++) {
                const uint8_t *r = data + d + 16 * i;
                for (int k = 0; k < 6; k++) p->texdefs[i].uv[k] = rdu16(r + 2 * k);
                int32_t pg = rd32(r + 12);
                p->texdefs[i].page = pg < 0 || pg > 30 ? 0 : pg;
            }
        } else if (!memcmp(data + o, "li03", 4)) {
            free(p->sprites);
            p->nsprites = n / 16;
            p->sprites = calloc((size_t)p->nsprites + 1, sizeof *p->sprites);
            if (!p->sprites) goto fail;
            for (int i = 0; i < p->nsprites; i++) {
                const uint8_t *r = data + d + 16 * i;
                Sprite *s = &p->sprites[i];
                s->ax = (int32_t)rds16(r) * 256;
                s->ay = (int32_t)rds16(r + 2) * 256;
                s->u0 = rdu16(r + 4) << 8;
                s->v0 = rdu16(r + 6) << 8;
                s->u1 = rdu16(r + 8) << 8;
                s->v1 = rdu16(r + 10) << 8;
                s->page = rdu16(r + 12);
                s->shade = rdu16(r + 14);
            }
        }
        o = d + (size_t)n;
    }
    p->verts = calloc((size_t)p->maxverts * 2 + 2, sizeof *p->verts);
    if (!p->verts) goto fail;
    return p;
fail:
    ign_pfm_free(p);
    return NULL;
}

void ign_pfm_free(IgnPfm *p) {
    if (!p) return;
    free(p->frames);
    free(p->texdefs);
    free(p->sprites);
    free(p->verts);
    free(p);
}

int ign_pfm_frames(const IgnPfm *p) { return p ? p->nframes : 0; }

static const uint8_t *page_of(const IgnPfm *p, int i) { return i >= 0 && i < p->npages ? p->pages[i] : NULL; }
static const uint8_t *shade_of(const IgnPfm *p, int i) { return i >= 0 && i < p->nshades ? p->shades[i] : NULL; }

/* ---- types 17 / 18: affine textured triangle ---------------------------------------------------------------- */

typedef struct {
    int32_t w, dsdx, dtdx, ks[8], kt[8];
    uint32_t e; /* 0xe6c28 */
    uint32_t dl; /* 0xe6c24 */
} Grad;

typedef struct {
    int32_t x, y, u, v;
} Pt;

typedef struct {
    int n;
    int32_t t[MAX_ROWS], s[MAX_ROWS], x[MAX_ROWS];
} EdgeUv;

typedef struct {
    int n;
    int32_t x[MAX_ROWS];
} EdgeX;

/* 0x90000 */
static void gradients(Grad *g, const Pt *t, const Pt *m, const Pt *b) {
    int32_t dm = S32((int64_t)m->y - t->y), db = S32((int64_t)b->y - t->y);
    int32_t r = db == dm ? 0x7fffffff : RATIO(dm, db);
    g->w = S32((int64_t)HI32(S32(2 * (int64_t)S32((int64_t)b->x - t->x)), r) - S32((int64_t)m->x - t->x));
    int32_t tw0 = HI32(S32(2 * (int64_t)S32((int64_t)b->v - t->v)), r);
    int32_t sw = S32((int64_t)HI32(S32(2 * (int64_t)S32((int64_t)b->u - t->u)), r) - m->u + t->u);
    int32_t tw = S32((int64_t)tw0 - m->v + t->v);
    int32_t a = (g->w >> 31) ^ g->w;
    if (a > 0x10000) {
        g->dsdx = IDIV((int64_t)sw * 65536, g->w);
        g->dtdx = IDIV((int64_t)tw * 65536, g->w);
    } else {
        int32_t rc = a > 3 ? IDIV((int64_t)1 << 32, g->w) : g->w >= 0 ? 0x7fffffff : INT32_MIN;
        g->dsdx = MID32(sw, rc);
        g->dtdx = MID32(tw, rc);
    }
    int32_t nds = S32(-(int64_t)g->dsdx), ndt = S32(-(int64_t)g->dtdx);
    for (int k = 0; k < 8; k++) {
        g->ks[k] = S32((int64_t)k * (nds >> 3));
        g->kt[k] = S32((int64_t)k * (ndt >> 3));
    }
    g->e = ((uint32_t)nds & 0xffff) << 16 | (((uint32_t)ndt >> 8) & 0xffff);
    g->dl = ((uint32_t)nds >> 16) & 0xff;
}

static void slopes(int32_t dy, const int32_t *d, int32_t *out, int n) {
    if ((uint32_t)dy <= 0x10000) {
        int32_t rc = (uint32_t)dy <= 2 ? 0x7fffffff : S32((int64_t)(((uint64_t)1 << 32) / (uint32_t)dy));
        for (int i = 0; i < n; i++) out[i] = MID32(d[i], rc);
    } else
        for (int i = 0; i < n; i++) out[i] = IDIV((int64_t)d[i] * 65536, dy);
}

static void fill_uv(EdgeUv *e, int n, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t s0, int32_t t0,
                    int32_t s1, int32_t t1, const Grad *g) {
    int32_t d[3] = {S32((int64_t)t1 - t0), S32((int64_t)s1 - s0), S32((int64_t)x1 - x0)}, sl[3];
    slopes(S32((int64_t)y1 - y0), d, sl, 3);
    int32_t dtdy = sl[0], dsdy = sl[1], dxdy = sl[2];
    int32_t f = 0x100 - (y0 & 0xff);
    int32_t s = S32((int64_t)s0 * 256 + SAR(S32((int64_t)f * dsdy), 8));
    int32_t t = S32((int64_t)t0 * 256 + SAR(S32((int64_t)f * dtdy), 8));
    int32_t x = S32((int64_t)f * SAR(dxdy, 8) + (int64_t)x0 * 256);
    for (int i = 0; i < n && e->n < MAX_ROWS; i++) {
        int k = (x >> 13) & 7;
        e->t[e->n] = S32((int64_t)t + g->kt[k]);
        e->s[e->n] = S32((int64_t)s + g->ks[k]);
        e->x[e->n] = SAR(x, 16);
        e->n++;
        s = S32((int64_t)s + dsdy);
        x = S32((int64_t)x + dxdy);
        t = S32((int64_t)t + dtdy);
    }
}

static void fill_x(EdgeX *e, int n, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int clamp, int minx) {
    int32_t d = S32((int64_t)x1 - x0), dxdy;
    slopes(S32((int64_t)y1 - y0), &d, &dxdy, 1);
    int32_t f = 0x100 - (y0 & 0xff);
    int32_t x = S32((int64_t)f * SAR(dxdy, 8) + (int64_t)x0 * 256);
    for (int i = 0; i < n && e->n < MAX_ROWS; i++) {
        int32_t xi = SAR(x, 16);
        if (clamp && xi < minx) xi = minx;
        e->x[e->n++] = xi;
        x = S32((int64_t)x + dxdy);
    }
}

/* 0x90200 */
static void edge_uv(EdgeUv *e, const Pt *a, const Pt *b, const Grad *g) {
    e->n = 0;
    int n = SAR(b->y, 8) - SAR(a->y, 8);
    if (n) fill_uv(e, n, a->x, a->y, b->x, b->y, a->u, a->v, b->u, b->v, g);
}

/* 0x90240 */
static int edge_x(EdgeX *e, const Pt *a, const Pt *b) {
    e->n = 0;
    int y0 = SAR(a->y, 8), n = SAR(b->y, 8) - y0;
    if (n) fill_x(e, n, a->x, a->y, b->x, b->y, 0, 0);
    return y0;
}

/* 0x90270 (+0x90530 / 0x9074b): clipped against the top, bottom and right edges, with UV correction. */
static void edge_uv_clip(const IgnPfm *p, EdgeUv *e, const Pt *a, const Pt *b, const Grad *g) {
    int32_t x0 = a->x, y0 = a->y, s0 = a->u, t0 = a->v, x1 = b->x, y1 = b->y, s1 = b->u, t1 = b->v;
    e->n = 0;
    if (!(p->miny8 < y1) || !(p->b8 > y0)) return;
    int32_t R = p->r8;
    if (p->miny8 > y0) {
        int32_t r = RATIO(p->miny8 - y0, y1 - y0);
        t0 = S32((int64_t)t0 + HI32(S32(2 * (int64_t)S32((int64_t)t1 - t0)), r));
        s0 = S32((int64_t)s0 + HI32(S32(2 * (int64_t)S32((int64_t)s1 - s0)), r));
        x0 = S32((int64_t)x0 + HI32(S32(2 * (int64_t)S32((int64_t)x1 - x0)), r));
        y0 = p->miny8;
    }
    if (p->b8 < y1) {
        int32_t r = RATIO(y1 - p->b8, y1 - y0);
        t1 = S32((int64_t)t1 - HI32(S32(2 * (int64_t)S32((int64_t)t1 - t0)), r));
        s1 = S32((int64_t)s1 - HI32(S32(2 * (int64_t)S32((int64_t)s1 - s0)), r));
        x1 = S32((int64_t)x1 - HI32(S32(2 * (int64_t)S32((int64_t)x1 - x0)), r));
        y1 = p->b8;
    }
    int split = 0;
    Pt sp = {0};
    if (R < x0) {
        if (!(R > x1)) {
            int32_t c = S32((int64_t)x1 - R);
            s1 = S32((int64_t)s1 - MID32(c, g->dsdx));
            t1 = S32((int64_t)t1 - MID32(c, g->dtdx));
            x1 = R;
        } else {
            split = 1;
            sp = (Pt){x1, y1, s1, t1};
            int32_t r = RATIO(R - x1, x0 - x1);
            s1 = S32((int64_t)s1 - HI32(S32(2 * (int64_t)S32((int64_t)s1 - s0)), r));
            t1 = S32((int64_t)t1 - HI32(S32(2 * (int64_t)S32((int64_t)t1 - t0)), r));
            y1 = S32((int64_t)y1 - HI32(S32(2 * (int64_t)S32((int64_t)y1 - y0)), r));
            x1 = R;
        }
        int32_t c = S32((int64_t)x0 - R);
        s0 = S32((int64_t)s0 - MID32(c, g->dsdx));
        t0 = S32((int64_t)t0 - MID32(c, g->dtdx));
        x0 = R;
    }
    if (R < x1) {
        split = 1;
        sp = (Pt){x1, y1, s1, t1};
        int32_t ee = S32((int64_t)x1 - R), c = S32((int64_t)x1 - x0);
        uint32_t q = ee == c ? 0xffffffffu : FRAC(ee, c);
        int32_t r = (int32_t)(q >> 1);
        s1 = S32((int64_t)s1 - HI32(S32(2 * (int64_t)S32((int64_t)s1 - s0)), r));
        t1 = S32((int64_t)t1 - HI32(S32(2 * (int64_t)S32((int64_t)t1 - t0)), r));
        y1 = S32((int64_t)y1 - HI32(S32(2 * (int64_t)S32((int64_t)y1 - y0)), r));
        x1 = R;
    }
    int n = SAR(y1, 8) - SAR(y0, 8);
    if (n) fill_uv(e, n, x0, y0, x1, y1, s0, t0, s1, t1, g);
    if (split) {
        x0 = x1, y0 = y1, s0 = s1, t0 = t1;
        x1 = sp.x, y1 = sp.y, s1 = sp.u, t1 = sp.v;
        if (R < x1) {
            int32_t c = S32((int64_t)x1 - R);
            s1 = S32((int64_t)s1 - MID32(c, g->dsdx));
            t1 = S32((int64_t)t1 - MID32(c, g->dtdx));
            x1 = R;
        }
        n = SAR(y1, 8) - SAR(y0, 8);
        if (n) fill_uv(e, n, x0, y0, x1, y1, s0, t0, s1, t1, g);
    }
}

/* 0x90380 (+0x908a0 / 0x909da) */
static int edge_x_clip(const IgnPfm *p, EdgeX *e, const Pt *a, const Pt *b) {
    int32_t x0 = a->x, y0 = a->y, x1 = b->x, y1 = b->y;
    e->n = 0;
    if (!(p->miny8 < y1)) return p->miny;
    if (!(p->b8 > y0)) return p->maxy + 1;
    int32_t R = p->r8;
    if (p->miny8 > y0) {
        int32_t r = RATIO(p->miny8 - y0, y1 - y0);
        x0 = S32((int64_t)x0 + HI32(S32(2 * (int64_t)S32((int64_t)x1 - x0)), r));
        y0 = p->miny8;
    }
    if (p->b8 < y1) {
        int32_t r = RATIO(y1 - p->b8, y1 - y0);
        x1 = S32((int64_t)x1 - HI32(S32(2 * (int64_t)S32((int64_t)x1 - x0)), r));
        y1 = p->b8;
    }
    int split = 0;
    int32_t sx = 0, sy = 0;
    if (R < x0) {
        if (!(R > x1))
            x1 = R;
        else {
            split = 1, sx = x1, sy = y1;
            int32_t r = RATIO(R - x1, x0 - x1);
            y1 = S32((int64_t)y1 - HI32(S32(2 * (int64_t)S32((int64_t)y1 - y0)), r));
            x1 = R;
        }
        x0 = R;
    }
    if (R < x1) {
        split = 1, sx = x1, sy = y1;
        int32_t ee = S32((int64_t)x1 - R), c = S32((int64_t)x1 - x0);
        uint32_t q = ee == c ? 0xffffffffu : FRAC(ee, c);
        int32_t r = (int32_t)(q >> 1);
        y1 = S32((int64_t)y1 - HI32(S32(2 * (int64_t)S32((int64_t)y1 - y0)), r));
        x1 = R;
    }
    int ystart = SAR(y0, 8);
    int n = SAR(y1, 8) - ystart;
    if (n) fill_x(e, n, x0, y0, x1, y1, 1, p->minx);
    if (split) {
        x0 = x1, y0 = y1, x1 = sx, y1 = sy;
        if (R < x1) x1 = R;
        n = SAR(y1, 8) - SAR(y0, 8);
        if (n) fill_x(e, n, x0, y0, x1, y1, 1, p->minx);
    }
    return ystart;
}

/* 0x824b4 (no shade) / 0x825b8: spans [left, right) drawn right to left; u and v step in a packed 32-bit value
 * (u fraction << 16 | v 8.8) with the u integer byte apart, the carry out of v leaking into u as in the original. */
static void spans(IgnPfm *p, int y0, const int32_t *a, int na, const EdgeUv *b, int bofs, int n, const uint8_t *page,
                  const uint8_t *shade, const Grad *g) {
    for (int i = 0; i < n; i++) {
        if (i >= na || bofs + i >= b->n) break;
        int32_t xl = a[i], tq = b->t[bofs + i], sq = b->s[bofs + i], xr = b->x[bofs + i];
        if (xr - xl <= 0) continue;
        int row = y0 + i;
        if (row < 0 || row > p->maxy) continue;
        uint8_t *dst = p->fb + (size_t)row * p->pitch;
        uint32_t c = ((uint32_t)sq & 0xffff) << 16 | (((uint32_t)tq >> 8) & 0xffff);
        uint32_t bl = ((uint32_t)sq >> 16) & 0xff;
        for (int32_t x = xr - 1; x >= xl; x--) {
            uint8_t tex = page[((c >> 8) & 0xff) << 8 | bl];
            if (x >= 0 && x < p->pitch) dst[x] = shade ? shade[tex << 8 | dst[x]] : tex;
            uint64_t sum = (uint64_t)c + g->e;
            c = (uint32_t)sum;
            bl = (bl + g->dl + (uint32_t)(sum >> 32)) & 0xff;
        }
    }
}

static void textri(IgnPfm *p, const int32_t v[6], const int32_t T[6], const uint8_t *page, const uint8_t *shade) {
    if (!page) return;
    int32_t X[6] = {0, v[0], 0, v[2], 0, v[4]}, Y[6] = {0, v[1], 0, v[3], 0, v[5]};
    int32_t U[6] = {0, T[0], 0, T[2], 0, T[4]}, V[6] = {0, T[1], 0, T[3], 0, T[5]};
    int32_t mx, mn;
    if (v[0] > v[2]) mx = v[0], mn = v[2];
    else mx = v[2], mn = v[0];
    if (v[4] > mx) mx = v[4];
    else if (v[4] < mn) mn = v[4];
    int top = 1, mid = 3, bot = 5;
    if (v[1] > v[5]) bot = 1, top = 5;
    if (v[3] < Y[top]) mid = top, top = 3;
    else if (v[3] > Y[bot]) mid = bot, bot = 3;
    int clipped = (S32((int64_t)Y[top] - p->miny8) | S32((int64_t)mn - p->minx8) | S32((int64_t)p->b8 - Y[bot]) |
                   S32((int64_t)p->r8 - mx)) < 0;
    if (clipped && (S32((int64_t)Y[bot] - p->miny8) | S32((int64_t)mx - p->minx8) | S32((int64_t)p->b8 - Y[top]) |
                    S32((int64_t)p->r8 - mn)) < 0)
        return;
    Pt tp = {X[top], Y[top], U[top], V[top]}, md = {X[mid], Y[mid], U[mid], V[mid]}, bt = {X[bot], Y[bot], U[bot], V[bot]};
    Grad g;
    gradients(&g, &tp, &md, &bt);
    static EdgeX a1, a2;
    static EdgeUv b1, b2;
    if (g.w > 0) {
        int y0 = clipped ? edge_x_clip(p, &a1, &tp, &md) : edge_x(&a1, &tp, &md);
        if (clipped) edge_uv_clip(p, &b1, &tp, &bt, &g);
        else edge_uv(&b1, &tp, &bt, &g);
        spans(p, y0, a1.x, a1.n, &b1, 0, a1.n, page, shade, &g);
        if (clipped) edge_x_clip(p, &a2, &md, &bt);
        else edge_x(&a2, &md, &bt);
        spans(p, y0 + a1.n, a2.x, a2.n, &b1, a1.n, a2.n, page, shade, &g);
    } else {
        if (clipped) edge_uv_clip(p, &b1, &tp, &md, &g);
        else edge_uv(&b1, &tp, &md, &g);
        int y0 = clipped ? edge_x_clip(p, &a1, &tp, &bt) : edge_x(&a1, &tp, &bt);
        spans(p, y0, a1.x, a1.n, &b1, 0, b1.n, page, shade, &g);
        if (clipped) edge_uv_clip(p, &b2, &md, &bt, &g);
        else edge_uv(&b2, &md, &bt, &g);
        int off = b1.n < a1.n ? b1.n : a1.n;
        spans(p, y0 + b1.n, a1.x + off, a1.n - off, &b2, 0, b2.n, page, shade, &g);
    }
}

/* ---- type 7: scaled sprite as two type-18 triangles (0x704a0) ----------------------------------------------- */

static void sprite(IgnPfm *p, int idx, int32_t x, int32_t y, const int32_t m[4]) {
    if (idx < 0 || idx >= p->nsprites) return;
    const Sprite *s = &p->sprites[idx];
    int32_t p0x = S32((int64_t)MID32(S32(-(int64_t)s->ax), m[0]) + MID32(S32(-(int64_t)s->ay), m[2]));
    int32_t p0y = S32((int64_t)MID32(S32(-(int64_t)s->ax), m[1]) + MID32(S32(-(int64_t)s->ay), m[3]));
    int32_t w = S32((int64_t)s->u1 - s->u0), h = S32((int64_t)s->v1 - s->v0);
    int32_t ax = MID32(m[0], w), ay = MID32(m[1], w), bx = MID32(m[2], h), by = MID32(m[3], h);
    int32_t P0x = S32((int64_t)p0x + x), P0y = S32((int64_t)p0y + y);
    int32_t P1x = S32((int64_t)p0x + ax + x), P1y = S32((int64_t)p0y + ay + y);
    int32_t P2x = S32((int64_t)p0x + bx + x), P2y = S32((int64_t)p0y + by + y);
    int32_t P3x = S32((int64_t)p0x + ax + bx + x), P3y = S32((int64_t)p0y + ay + by + y);
    const uint8_t *page = page_of(p, s->page), *shade = shade_of(p, s->shade);
    if (!shade) return;
    int32_t va[6] = {P0x, P0y, P1x, P1y, P2x, P2y}, ta[6] = {s->u0, s->v0, s->u1, s->v0, s->u0, s->v1};
    int32_t vb[6] = {P1x, P1y, P2x, P2y, P3x, P3y}, tb[6] = {s->u1, s->v0, s->u0, s->v1, s->u1, s->v1};
    textri(p, va, ta, page, shade);
    textri(p, vb, tb, page, shade);
}

/* ---- type 19: flat triangle through a shade table, dst = shade[colour << 8 | dst] (0x74d04) ---------------- */

static void sort3(int32_t P[3][2]) {
    int32_t *b = P[0], *c = P[1], *d = P[2], t[2];
#define SWAP(a, z) (t[0] = a[0], t[1] = a[1], a[0] = z[0], a[1] = z[1], z[0] = t[0], z[1] = t[1])
    if (c[1] < b[1]) {
        SWAP(b, c);
        int32_t ey = d[1];
        if (!(ey > c[1])) {
            SWAP(c, d);
            if (!(ey > b[1])) SWAP(b, c);
        }
    } else if (!(c[1] < d[1])) {
        SWAP(c, d);
        if (!(b[1] < c[1])) SWAP(b, c);
    }
#undef SWAP
}

static void shade_slope(const int32_t a[2], const int32_t e[2], int32_t *sl, int32_t *x) {
    int32_t dy = S32((int64_t)e[1] - a[1]), dx = S32((int64_t)e[0] - a[0]);
    if ((uint32_t)dy <= 0x40) {
        uint32_t adx = (uint32_t)(dx < 0 ? -(int64_t)dx : dx);
        if (adx <= 0x10000) dy = 0x41;
        else if ((uint32_t)dy <= 3) dy = 3;
    }
    *sl = IDIV((int64_t)dx * 65536, dy);
    *x = S32((int64_t)a[0] * 256 + (int64_t)(0x100 - (a[1] & 0xff)) * SAR(*sl, 8));
}

static void shade_rows(IgnPfm *p, int row, int cnt, int32_t xl, int32_t xr, int32_t dl, int32_t dr,
                       const uint8_t *shade, int base) {
    if (row < p->miny) {
        int k = p->miny - row;
        cnt -= k;
        if (cnt <= 0) return;
        xl = S32((int64_t)xl + (int64_t)k * dl);
        xr = S32((int64_t)xr + (int64_t)k * dr);
        row = p->miny;
    }
    int lim = p->maxy + 1 - row;
    if (lim < cnt) {
        if (row > p->maxy + 1 || lim == 0) return;
        cnt = lim;
    }
    for (; cnt; cnt--, row++) {
        int32_t L = SAR(xl, 16), R = SAR(xr, 16);
        int ok = 1;
        if (L < p->minx) {
            if (R < p->minx) ok = 0;
            else L = p->minx;
        }
        if (ok && R > p->maxx + 1) {
            if (L > p->maxx + 1) ok = 0;
            else R = p->maxx + 1;
        }
        if (ok && R - L > 0 && row >= 0 && row <= p->maxy) {
            uint8_t *d = p->fb + (size_t)row * p->pitch;
            for (int32_t x = L; x < R; x++) d[x] = shade[base | d[x]];
        }
        xl = S32((int64_t)xl + dl);
        xr = S32((int64_t)xr + dr);
    }
}

static void shadetri(IgnPfm *p, const int32_t v[6], int col, const uint8_t *shade) {
    if (!shade) return;
    int32_t P[3][2] = {{v[0], v[1]}, {v[2], v[3]}, {v[4], v[5]}};
    sort3(P);
    const int32_t *top = P[0], *mid = P[1], *bot = P[2];
    const int32_t LIM = 0x7d0000;
    for (int i = 0; i < 3; i++)
        if (P[i][0] < -LIM || P[i][0] > LIM) return;
    if (bot[1] > LIM) return;
    int base = col << 8;
    int r0 = SAR(top[1], 8), n1 = SAR(mid[1], 8) - r0;
    int32_t dl, xl, dr, xr, t;
    if (n1) {
        shade_slope(top, mid, &dl, &xl);
        shade_slope(top, bot, &dr, &xr);
        if (dl > dr) t = dl, dl = dr, dr = t, t = xl, xl = xr, xr = t;
        shade_rows(p, r0, n1, xl, xr, dl, dr, shade, base);
    }
    int r1 = SAR(mid[1], 8), n2 = SAR(bot[1], 8) - r1;
    if (!n2) return;
    shade_slope(top, bot, &dl, &xl);
    shade_slope(mid, bot, &dr, &xr);
    xl = S32((int64_t)xl + (int64_t)(SAR(mid[1], 8) - SAR(top[1], 8)) * dl);
    if (dl < dr) t = dl, dl = dr, dr = t, t = xl, xl = xr, xr = t;
    shade_rows(p, r1, n2, xl, xr, dl, dr, shade, base);
}

/* ---- type 15: flat triangle, colour = MENU.TAB[colour << 8] (0x729f3), whole-pixel vertices --------------- */

static int32_t FX(int32_t i, int32_t frac) { return (int32_t)(((uint32_t)i << 16) | (uint32_t)frac); }

static void flat_run(IgnPfm *p, int row, int cnt, int32_t L, int32_t R, int32_t dl, int32_t dr, int step_first,
                     uint8_t col) {
    if (row < p->miny) {
        int k = p->miny - row;
        cnt -= k;
        if (cnt < 0) return;
        R = S32((int64_t)R + (int64_t)dr * k);
        L = S32((int64_t)L + (int64_t)dl * k);
        row = p->miny;
    }
    if (row + cnt > p->maxy) {
        cnt -= row + cnt - p->maxy;
        if (cnt < 0) return;
    }
    for (;;) {
        if (step_first) {
            R = S32((int64_t)R + dr);
            L = S32((int64_t)L + dl);
        }
        int li = ((uint32_t)L >> 16) & 0xffff, ri = ((uint32_t)R >> 16) & 0xffff;
        int li16 = (int16_t)li, ri16 = (int16_t)ri;
        int draw = 1, lo = li, hi = ri;
        if (li16 < p->minx) {
            if (ri16 < p->minx) draw = 0;
            else {
                lo = p->minx;
                if (ri16 > p->maxx) hi = p->maxx;
            }
        } else if (ri16 > p->maxx) {
            if (li16 > p->maxx) draw = 0;
            else hi = p->maxx;
        }
        if (draw) {
            int n = (hi - lo) & 0xffff;
            if (!(n & 0x8000) && row >= 0 && row <= p->maxy)
                for (int x = lo; x <= lo + n; x++)
                    if (x >= 0 && x < p->pitch) p->fb[(size_t)row * p->pitch + x] = col;
        }
        if (!step_first) {
            R = S32((int64_t)R + dr);
            L = S32((int64_t)L + dl);
        }
        row++;
        if (--cnt < 0) break;
    }
}

static void flat(IgnPfm *p, const int32_t v[6], int colour) {
    int32_t P[3][2] = {{v[0], v[1]}, {v[2], v[3]}, {v[4], v[5]}};
    sort3(P);
    int32_t T[2] = {SAR(P[0][0], 8), SAR(P[0][1], 8)}, M[2] = {SAR(P[1][0], 8), SAR(P[1][1], 8)},
            B[2] = {SAR(P[2][0], 8), SAR(P[2][1], 8)};
    if (B[1] - T[1] > 15000) return;
    int32_t *q[3] = {T, M, B};
    for (int i = 0; i < 3; i++) {
        if (q[i][0] < -32000) q[i][0] = -32000;
        else if (q[i][0] > 32000) q[i][0] = 32000;
    }
    uint8_t col = p->lut[colour << 8];
    const int32_t *Rc = p->recip;
#define RC(k) ((k) >= 0 && (k) < 0x3a9a ? Rc[k] : 0)
    if (T[1] == B[1]) {
        const int32_t *l = T, *r = M, *o = B, *tt;
        int32_t right;
        if (l[0] > r[0]) tt = l, l = r, r = tt;
        if (o[0] > r[0]) right = o[0];
        else {
            if (!(o[0] > l[0])) l = o;
            right = r[0];
        }
        flat_run(p, T[1], 0, FX(l[0], 0), FX(right, 0), 0, 0, 0, col);
        return;
    }
    int32_t dl, dr, L0, R0, dx, rc;
    if (T[1] == M[1]) {
        const int32_t *l = T, *r = M, *tt;
        if (l[0] > r[0]) tt = l, l = r, r = tt;
        int cnt = B[1] - l[1];
        rc = RC(cnt);
        dx = B[0] - r[0];
        if (dx > 0) dr = S32((int64_t)(dx + 1) * rc), R0 = S32((int64_t)FX(r[0], 0) + dr);
        else dr = S32((int64_t)(dx - 1) * rc), R0 = FX(r[0], 0xffff);
        dx = B[0] - l[0];
        if (dx < 0) dl = S32((int64_t)(dx - 1) * rc), L0 = S32((int64_t)FX(l[0], 0xffff) + dl);
        else dl = S32((int64_t)(dx + 1) * rc), L0 = FX(l[0], 0);
        flat_run(p, l[1], cnt, L0, R0, dl, dr, 0, col);
        return;
    }
    if (M[1] == B[1]) {
        const int32_t *l = M, *r = B, *tt;
        if (l[0] > r[0]) tt = l, l = r, r = tt;
        int cnt = r[1] - T[1];
        rc = RC(cnt);
        dx = r[0] - T[0];
        if (dx > 0) dr = S32((int64_t)(dx + 1) * rc), R0 = S32((int64_t)FX(T[0], 0) + dr);
        else dr = S32((int64_t)(dx - 1) * rc), R0 = FX(T[0], 0xffff);
        dx = l[0] - T[0];
        if (dx < 0) dl = S32((int64_t)(dx - 1) * rc), L0 = S32((int64_t)FX(T[0], 0xffff) + dl);
        else dl = S32((int64_t)(dx + 1) * rc), L0 = FX(T[0], 0);
        flat_run(p, T[1], cnt, L0, R0, dl, dr, 0, col);
        return;
    }
    int c1 = M[1] - T[1], c2 = B[1] - M[1];
    int32_t rl = RC(B[1] - T[1]);
    int32_t xm = S32((int64_t)S32((int64_t)S32((int64_t)(B[0] - T[0]) * rl) * c1) + 0x8000) >> 16;
    xm = S32((int64_t)xm + T[0]);
    int32_t L1, L2, R1, R2, dl1, dl2, dr1, dr2;
    if (xm < M[0]) {
        dx = B[0] - T[0];
        if (dx > 0) dl = S32((int64_t)(dx + 1) * rl), L1 = FX(T[0], 0), L2 = FX(xm, 0);
        else {
            dl = S32((int64_t)(dx - 1) * rl);
            L1 = S32((int64_t)FX(T[0], 0xffff) + dl);
            L2 = S32((int64_t)FX(xm, 0xffff) + dl);
        }
        int32_t r1 = RC(c1);
        dx = M[0] - T[0];
        if (dx < 0) dr1 = S32((int64_t)(dx - 1) * r1), R1 = FX(T[0], 0xffff);
        else dr1 = S32((int64_t)(dx + 1) * r1), R1 = S32((int64_t)FX(T[0], 0) + dr1);
        int32_t r2 = RC(c2);
        dx = B[0] - M[0];
        if (dx > 0) dr2 = S32((int64_t)(dx + 1) * r2), R2 = S32((int64_t)FX(M[0], 0) + dr2);
        else dr2 = S32((int64_t)(dx - 1) * r2), R2 = FX(M[0], 0xffff);
        dl1 = dl2 = dl;
    } else {
        dx = B[0] - T[0];
        if (dx < 0) dr = S32((int64_t)(dx - 1) * rl), R1 = FX(T[0], 0xffff), R2 = FX(xm, 0xffff);
        else {
            dr = S32((int64_t)(dx + 1) * rl);
            R1 = S32((int64_t)FX(T[0], 0) + dr);
            R2 = S32((int64_t)FX(xm, 0) + dr);
        }
        dr1 = dr2 = dr;
        int32_t r1 = RC(c1);
        dx = M[0] - T[0];
        if (dx > 0) dl1 = S32((int64_t)(dx + 1) * r1), L1 = FX(T[0], 0);
        else dl1 = S32((int64_t)(dx - 1) * r1), L1 = S32((int64_t)FX(T[0], 0xffff) + dl1);
        int32_t r2 = RC(c2);
        dx = B[0] - M[0];
        if (dx < 0) dl2 = S32((int64_t)(dx - 1) * r2), L2 = S32((int64_t)FX(M[0], 0xffff) + dl2);
        else dl2 = S32((int64_t)(dx + 1) * r2), L2 = FX(M[0], 0);
    }
#undef RC
    flat_run(p, T[1], c1, L1, R1, dl1, dr1, 0, col);
    flat_run(p, M[1] + 1, c2 - 1, L2, R2, dl2, dr2, 1, col);
}

/* ---- type 11: line, colour = MENU.TAB[colour << 8] (0x7072f), run-slice from an accumulator of 0x7fff ------ */

static void line(IgnPfm *p, const int32_t v[4], int colour) {
    int32_t x0 = SAR(v[0], 8), y0 = SAR(v[1], 8), x1 = SAR(v[2], 8), y1 = SAR(v[3], 8), t;
    uint8_t col = p->lut[colour << 8];
    if (x0 > x1) t = x0, x0 = x1, x1 = t, t = y0, y0 = y1, y1 = t;
    int mnx = p->minx, mny = p->miny, mxx = p->maxx, mxy = p->maxy;
    if (x0 < mnx) {
        if (x1 < mnx) return;
        int32_t dy = S32((int64_t)y1 - y0);
        y0 = S32((int64_t)y0 + dy - HI32(S32(2 * (int64_t)dy), RATIO(x1 - mnx + 1, x1 - x0 + 1)));
        x0 = mnx;
    }
    if (x1 > mxx) {
        if (x0 > mxx) return;
        y1 = S32((int64_t)y0 + HI32(S32(2 * (int64_t)S32((int64_t)y1 - y0)), RATIO(mxx - x0 + 1, x1 - x0 + 1)));
        x1 = mxx;
    }
    if (y0 <= y1) {
        if (y1 > mxy) {
            if (y0 > mxy) return;
            x1 = S32((int64_t)x0 + HI32(S32(2 * (int64_t)S32((int64_t)x1 - x0)), RATIO(mxy - y0 + 1, y1 - y0 + 1)));
            y1 = mxy;
        }
        if (y0 < mny) {
            if (y1 < mny) return;
            x0 = S32((int64_t)x1 - HI32(S32(2 * (int64_t)S32((int64_t)x1 - x0)), RATIO(y1 - mny + 1, y1 - y0 + 1)));
            y0 = mny;
        }
    } else {
        if (y0 > mxy) {
            if (y1 > mxy) return;
            x0 = S32((int64_t)x1 - HI32(S32(2 * (int64_t)S32((int64_t)x1 - x0)), RATIO(mxy - y1 + 1, y0 - y1 + 1)));
            y0 = mxy;
        }
        if (y1 < mny) {
            if (y0 < mny) return;
            x1 = S32((int64_t)x0 + HI32(S32(2 * (int64_t)S32((int64_t)x1 - x0)), RATIO(y0 - mny + 1, y0 - y1 + 1)));
            y1 = mny;
        }
    }
    int64_t pos = (int64_t)y0 * p->pitch + x0, total = (int64_t)p->pitch * (p->maxy + 1);
    int dxp = (x1 - x0 + 1) & 0xffff, dy = y1 - y0;
    int ystep = dy >= 0 ? p->pitch : -p->pitch;
    int dyp = ((dy >= 0 ? dy : -dy) + 1) & 0xffff;
    if (!dxp || !dyp) return;
    uint32_t cx = 0x7fff;
#define PLOT() (pos >= 0 && pos < total ? (void)(p->fb[pos] = col) : (void)0)
    if (dxp <= dyp) {
        int q = dyp / dxp;
        uint32_t frac = (uint32_t)((dyp % dxp) << 16) / (uint32_t)dxp;
        for (int i = 0; i < dxp; i++) {
            cx += frac;
            int n = q + (int)(cx >> 16);
            cx &= 0xffff;
            for (int k = 0; k < n; k++) PLOT(), pos += ystep;
            pos += 1;
        }
    } else {
        int q = dxp / dyp;
        uint32_t frac = (uint32_t)((dxp % dyp) << 16) / (uint32_t)dyp;
        for (int i = 0; i < dyp; i++) {
            cx += frac;
            int n = q + (int)(cx >> 16);
            cx &= 0xffff;
            for (int k = 0; k < n; k++) PLOT(), pos += 1;
            pos += ystep;
        }
    }
#undef PLOT
}

/* ---- pfm_build_frame 0x54df8 + raster_draw_list 0x60e3d ------------------------------------------------------ */

static const int REC_SIZE[20] = {[7] = 8, [11] = 5, [13] = 7, [15] = 7, [17] = 8, [18] = 9, [19] = 8};

int ign_pfm_draw(IgnPfm *p, int f, uint8_t *dst, int w, int h, const uint8_t *lut) {
    if (!p || f < 0 || f >= p->nframes) return -1;
    const Frame *fr = &p->frames[f];
    const uint8_t *d = p->data;
    /* An unknown primitive makes pfm_build_frame return NULL: nothing of the frame is drawn. */
    size_t o = fr->li00;
    while (o < p->size && d[o] != 0xff) {
        if (o + 2 > p->size) return -1;
        int t = d[o], c = d[o + 1];
        if (t >= 20 || !REC_SIZE[t]) return -1;
        o += 2 + (size_t)c * REC_SIZE[t];
    }
    if (o >= p->size) return -1;
    for (int i = 0; i < fr->nverts; i++) {
        p->verts[2 * i] = rds16(d + fr->li01 + 4 * i) * 16;
        p->verts[2 * i + 1] = rds16(d + fr->li01 + 4 * i + 2) * 16;
    }
    p->fb = dst;
    p->pitch = w;
    p->minx = p->miny = 0;
    p->maxx = w - 1;
    p->maxy = h - 1;
    p->miny8 = 0;
    p->minx8 = 0;
    p->r8 = w << 8;
    p->b8 = h << 8;
    p->lut = lut;
#define VX(i) ((i) >= 0 && (i) < fr->nverts ? p->verts[2 * (i)] : 0)
#define VY(i) ((i) >= 0 && (i) < fr->nverts ? p->verts[2 * (i) + 1] : 0)
    for (o = fr->li00; d[o] != 0xff;) {
        int t = d[o], c = d[o + 1];
        o += 2;
        for (int k = 0; k < c; k++, o += REC_SIZE[t]) {
            const uint8_t *r = d + o;
            if (t == 7) {
                int32_t m[4] = {rdu16(r + 2) << 8, 0, 0, rdu16(r + 4) << 8};
                int vi = rds16(r + 6);
                sprite(p, rds16(r), VX(vi), VY(vi), m);
            } else if (t == 11) {
                int a = rds16(r), b = rds16(r + 2);
                int32_t v[4] = {VX(a), VY(a), VX(b), VY(b)};
                line(p, v, r[4]);
            } else if (t == 15 || t == 17 || t == 18 || t == 19) {
                int a = rds16(r), b = rds16(r + 2), cc = rds16(r + 4);
                int32_t v[6] = {VX(a), VY(a), VX(b), VY(b), VX(cc), VY(cc)};
                if (t == 15) flat(p, v, r[6]);
                else if (t == 19) shadetri(p, v, r[6], shade_of(p, r[7]));
                else {
                    int td = rds16(r + 6);
                    if (td < 0 || td >= p->ntexdefs) continue;
                    const TexDef *T = &p->texdefs[td];
                    textri(p, v, T->uv, page_of(p, T->page), t == 18 ? shade_of(p, r[8]) : NULL);
                }
            }
        }
    }
#undef VX
#undef VY
    return 0;
}
