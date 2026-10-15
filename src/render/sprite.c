#include "sprite.h"

#include <math.h>
#include <string.h>

int ign_sprite_from_pic(IgnSprite *s, const IgnPic *pic, int x, int y, int w, int h) {
    memset(s, 0, sizeof *s);
    if (!pic->pixels || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > pic->width || y + h > pic->height)
        return -1;
    s->pixels = pic->pixels + (size_t)y * pic->width + x;
    s->w = w;
    s->h = h;
    s->stride = pic->width;
    return 0;
}

static void clip_to_frame(const IgnFrame *fb, const IgnClip *clip, IgnClip *out) {
    *out = clip ? *clip : (IgnClip){0, 0, fb->w, fb->h};
    if (out->x0 < 0) out->x0 = 0;
    if (out->y0 < 0) out->y0 = 0;
    if (out->x1 > fb->w) out->x1 = fb->w;
    if (out->y1 > fb->h) out->y1 = fb->h;
}

static inline void put(IgnFrame *fb, int x, int y, uint8_t src, const uint8_t *lut) {
    uint8_t *dst = &fb->color[y * fb->w + x];
    if (lut)
        *dst = lut[src << 8 | *dst];
    else if (src)
        *dst = src;
}

void ign_sprite_draw(IgnFrame *fb, const IgnClip *clip, const IgnSprite *s, int x, int y, const uint8_t *lut) {
    if (!s->pixels) return;
    IgnClip c;
    clip_to_frame(fb, clip, &c);
    /* sub_000791c9: rows and columns outside the clip window are skipped, the rest go through the table. */
    for (int sy = 0; sy < s->h; sy++) {
        int py = y + sy;
        if (py < c.y0 || py >= c.y1) continue;
        const uint8_t *row = s->pixels + (size_t)sy * s->stride;
        for (int sx = 0; sx < s->w; sx++) {
            int px = x + sx;
            if (px < c.x0 || px >= c.x1) continue;
            put(fb, px, py, row[sx], lut);
        }
    }
}

/* ---- Matrix sprites: sub_00082950 -> two affine texture-mapped triangles (sub_00078304 / sub_00078ac0). ----
 * Positions are 8.8 pixels, texture coordinates 8.8 texels of a 256 x 256 page. Names follow the original globals. */

#define TEX_EDGE_MAX 700 /* 0x20d8-byte edge buffers of sub_000782b0 */

typedef struct {
    int32_t start, count;
    int32_t e[TEX_EDGE_MAX][3]; /* V, U, x per row */
} TexEdges;

typedef struct {
    int32_t x0, y0, x1, y1, u0, v0, u1, v1; /* 0x268ab8 / 0x268ad8 */
} TexEdge;

typedef struct {
    IgnFrame *fb;
    const uint8_t *page, *lut;
    int32_t top, bottom, left, right; /* [0x257ea4], [0x257ea8], [0x257eac], [0x257eb0] (8.8) */
    int32_t clip_x0, clip_y0, clip_y1; /* [0x257e98], [0x257ea0], [0x257e9c] */
    int32_t dudx, dvdx;                /* 0xe6cf0, 0xe6cf4 */
    int32_t dither_u[8], dither_v[8];  /* 0xe6d00, 0xe6d20 */
    uint32_t packed_step;              /* 0xe6e70 */
    uint8_t step_int;                  /* 0xe6e6c */
    int32_t du, dv, dx, acc_u, acc_v, acc_x; /* 0xe6df8, 0xe6dfc, 0xe6e0c, 0xe6df0, 0xe6df4, 0xe6e08 */
    int split;                         /* 0xe6e14 */
    int32_t saved[4];                  /* 0xe6e18..0xe6e24 */
    TexEdges a, b, c;                  /* [0x268ab4], [0x268a98], [0x268a9c] */
} TexCtx;

static int32_t hi32(int64_t v) {
    return (int32_t)(v >> 32);
}

/* imul; mov ax, dx; ror/rol eax, 16: bits 16..47 of the product. */
static int32_t mul16(int32_t a, int32_t b) {
    return (int32_t)(uint32_t)((uint64_t)((int64_t)a * b) >> 16);
}

/* "mov edx, eax; sar edx, 31; shl eax, 16; idiv": the sign of the unshifted value above the shifted low word. */
static int32_t div_shift16(int32_t d, int32_t by) {
    int64_t n = (int64_t)((uint64_t)(uint32_t)(d >> 31) << 32 | (uint32_t)((uint32_t)d << 16));
    return (int32_t)(n / by);
}

/* sub_00090a80: texture gradients across x from the long edge's point at the middle vertex's row. */
static void tex_gradients(TexCtx *t, const int32_t v[12]) {
    int32_t d40 = v[3] - v[1], d44 = v[5] - v[1];
    if (d44 == 0) d44 = 1;
    uint32_t k = d44 == d40 ? 0x7fffffffu : (uint32_t)(((uint64_t)(uint32_t)d40 << 32) / (uint32_t)d44) >> 1;
    int32_t d4c = v[2] - v[0];
    int32_t d50 = hi32((int64_t)(int32_t)((uint32_t)(v[4] - v[0]) * 2u) * (int32_t)k);
    int32_t d54 = hi32((int64_t)(int32_t)((uint32_t)(v[10] - v[6]) * 2u) * (int32_t)k);
    int32_t d58 = hi32((int64_t)(int32_t)((uint32_t)(v[11] - v[7]) * 2u) * (int32_t)k);
    int32_t w = d50 - d4c;
    if (w >= -2 && w <= 2) w = (int32_t)((uint32_t)((w >> 2) | 1) << 2);
    t->dudx = (int32_t)((int64_t)(d54 - v[8] + v[6]) * 65536 / w);
    t->dvdx = (int32_t)((int64_t)(d58 - v[9] + v[7]) * 65536 / w);
    int32_t su = -t->dudx >> 3, sv = -t->dvdx >> 3;
    for (int i = 0; i < 8; i++) t->dither_u[i] = (int32_t)((uint32_t)su * (uint32_t)i), t->dither_v[i] = (int32_t)((uint32_t)sv * (uint32_t)i);
    /* sub_0008283c: span step packed as u fraction (16 bits) over v 8.8; the u integer byte apart. */
    uint32_t nu = (uint32_t)-t->dudx, nv = (uint32_t)-t->dvdx;
    t->step_int = (uint8_t)(nu >> 16);
    t->packed_step = nu << 16 | ((nv >> 8) & 0xffff);
}

/* sub_000914d0: edge set-up with the sub-row prestep (256 - y & 0xff). */
static void tex_edge_setup(TexCtx *t, const TexEdge *e) {
    int32_t dy = e->y1 - e->y0;
    if ((uint32_t)dy <= 0x40) {
        int32_t adx = e->x1 - e->x0;
        if (adx <= 0) adx = -adx;
        if ((uint32_t)adx <= 0x10000) dy = 0x41;
        else if ((uint32_t)dy <= 3) dy = 3;
    }
    t->dv = div_shift16(e->v1 - e->v0, dy);
    t->du = div_shift16(e->u1 - e->u0, dy);
    t->dx = (int32_t)((int64_t)(e->x1 - e->x0) * 65536 / dy);
    int32_t pre = 0x100 - (e->y0 & 0xff);
    t->acc_u = (int32_t)((uint32_t)e->u0 << 8) + ((int32_t)((uint32_t)pre * (uint32_t)t->du) >> 8);
    t->acc_v = (int32_t)((uint32_t)e->v0 << 8) + ((int32_t)((uint32_t)pre * (uint32_t)t->dv) >> 8);
    t->acc_x = (int32_t)((uint32_t)e->x0 << 8) + (int32_t)((uint32_t)pre * (uint32_t)(t->dx >> 8));
}

/* sub_000911e0 (integer u, v: left ends) and sub_00091220 (8.8 u, v with the x-fraction dither: right ends). */
static void tex_walk(TexCtx *t, int32_t (*out)[3], int n, int dither) {
    for (int i = 0; i < n; i++) {
        if (dither) {
            int k = (int)((uint32_t)t->acc_x >> 13 & 7);
            out[i][1] = (t->dither_u[k] + t->acc_u) >> 8;
            out[i][0] = (t->dither_v[k] + t->acc_v) >> 8;
        } else {
            out[i][1] = t->acc_u >> 16;
            out[i][0] = t->acc_v >> 16;
        }
        out[i][2] = t->acc_x >> 16;
        t->acc_u += t->du, t->acc_v += t->dv, t->acc_x += t->dx;
    }
}

static int clamp_rows(int n) {
    return n < 0 ? 0 : n > TEX_EDGE_MAX ? TEX_EDGE_MAX : n;
}

/* sub_00090c40 / sub_00090c00. */
static void tex_edge(TexCtx *t, TexEdges *buf, const TexEdge *e, int dither) {
    buf->start = e->y0 >> 8;
    buf->count = (e->y1 >> 8) - buf->start;
    if (!buf->count) return;
    tex_edge_setup(t, e);
    tex_walk(t, buf->e, clamp_rows(buf->count), dither);
}

/* hi32((a - b) * 2 * t) of sub_00091270. */
static int32_t lerp_hi(int32_t a, int32_t b, uint32_t t) {
    return hi32((int64_t)(int32_t)((uint32_t)(a - b) * 2u) * (int32_t)t);
}

/* sub_00091270: clips an edge to the window; a part beyond the right side is kept for a vertical piece at x = right
 * ([0xe6e14] = 1, end point saved in 0xe6e18). */
static void tex_clip_edge(TexCtx *t, TexEdge *e) {
    if (t->top > e->y0) {
        uint32_t k = (uint32_t)(((uint64_t)(uint32_t)(t->top - e->y0) << 32) / (uint32_t)(e->y1 - e->y0)) >> 1;
        e->v0 += lerp_hi(e->v1, e->v0, k);
        e->u0 += lerp_hi(e->u1, e->u0, k);
        e->x0 += lerp_hi(e->x1, e->x0, k);
        e->y0 = t->top;
    }
    if (t->bottom < e->y1) {
        uint32_t k = (uint32_t)(((uint64_t)(uint32_t)(e->y1 - t->bottom) << 32) / (uint32_t)(e->y1 - e->y0)) >> 1;
        e->v1 -= lerp_hi(e->v1, e->v0, k);
        e->u1 -= lerp_hi(e->u1, e->u0, k);
        e->x1 -= lerp_hi(e->x1, e->x0, k);
        e->y1 = t->bottom;
    }
    t->split = 0;
    if (t->right < e->x0) {
        if (t->right > e->x1) {
            t->split = 1;
            t->saved[0] = e->x1, t->saved[1] = e->y1, t->saved[2] = e->u1, t->saved[3] = e->v1;
            uint32_t k = (uint32_t)(((uint64_t)(uint32_t)(t->right - e->x1) << 32) / (uint32_t)(e->x0 - e->x1)) >> 1;
            e->u1 -= lerp_hi(e->u1, e->u0, k);
            e->v1 -= lerp_hi(e->v1, e->v0, k);
            e->y1 -= lerp_hi(e->y1, e->y0, k);
            e->x1 = t->right;
        } else {
            int32_t d = e->x1 - t->right;
            e->u1 -= mul16(d, t->dudx);
            e->v1 -= mul16(d, t->dvdx);
            e->x1 = t->right;
        }
        int32_t d = e->x0 - t->right;
        e->u0 -= mul16(d, t->dudx);
        e->v0 -= mul16(d, t->dvdx);
        e->x0 = t->right;
    }
    if (t->right < e->x1) {
        t->split = 1;
        t->saved[0] = e->x1, t->saved[1] = e->y1, t->saved[2] = e->u1, t->saved[3] = e->v1;
        uint32_t span = (uint32_t)(e->x1 - e->x0), over = (uint32_t)(e->x1 - t->right);
        uint32_t k = (over == span ? 0xffffffffu : (uint32_t)(((uint64_t)over << 32) / span)) >> 1;
        e->u1 -= lerp_hi(e->u1, e->u0, k);
        e->v1 -= lerp_hi(e->v1, e->v0, k);
        e->y1 -= lerp_hi(e->y1, e->y0, k);
        e->x1 = t->right;
    }
}

/* sub_000910c0 / sub_00090fa0: clipped edge, possibly in two pieces. */
static void tex_edge_clipped(TexCtx *t, TexEdges *buf, TexEdge e, int dither) {
    if (!(t->top < e.y1)) {
        buf->count = 0, buf->start = t->clip_y0;
        return;
    }
    if (!(t->bottom > e.y0)) {
        buf->count = 0, buf->start = t->clip_y1;
        return;
    }
    tex_clip_edge(t, &e);
    int at = 0;
    buf->start = e.y0 >> 8;
    buf->count = (e.y1 >> 8) - buf->start;
    if (buf->count) {
        tex_edge_setup(t, &e);
        at = clamp_rows(buf->count);
        tex_walk(t, buf->e, at, dither);
    }
    if (t->split != 1) return;
    e.x0 = e.x1, e.y0 = e.y1, e.u0 = e.u1, e.v0 = e.v1;
    e.x1 = t->saved[0], e.y1 = t->saved[1], e.u1 = t->saved[2], e.v1 = t->saved[3];
    if (t->right < e.x1) { /* sub_00091489 */
        int32_t d = e.x1 - t->right;
        e.u1 -= mul16(d, t->dudx);
        e.v1 -= mul16(d, t->dvdx);
        e.x1 = t->right;
    }
    int n = (e.y1 >> 8) - (e.y0 >> 8);
    if (!n) return;
    buf->count += n;
    tex_edge_setup(t, &e);
    if (at + n > TEX_EDGE_MAX) n = TEX_EDGE_MAX - at;
    if (n > 0) tex_walk(t, buf->e + at, n, dither);
}

/* sub_00082862 (with the left end texel) and sub_000828b7 (clipped on the left, `left` NULL): pixels from right to
 * left, stepping the packed u/v before each texel; dst = lut[texel * 256 + dst]. */
static void tex_span(TexCtx *t, int row, int x, int count, const int32_t *left, const int32_t *right) {
    uint8_t *line = t->fb->color + (size_t)row * t->fb->w;
    uint32_t c = (uint32_t)right[1] << 24;
    c += (uint32_t)right[0] & 0xffff;
    uint8_t u = (uint8_t)(right[1] >> 8), v;
    uint8_t first = left ? t->page[((uint32_t)left[0] & 0xff) << 8 | ((uint32_t)left[1] & 0xff)] : 0;
    int i = count - 1;
    for (; i >= 0; i--) {
        uint32_t old = c;
        c += t->packed_step;
        v = (uint8_t)(c >> 8);
        u = (uint8_t)(u + t->step_int + (c < old));
        uint8_t texel = t->page[(uint32_t)v << 8 | u];
        if (x + i < 0 || x + i >= t->fb->w) continue; /* only broken input leaves the window */
        uint8_t *dst = line + x + i;
        *dst = t->lut[(uint32_t)texel << 8 | *dst];
    }
    if (left && i == -1 && x - 1 >= 0 && x - 1 < t->fb->w) {
        uint8_t *dst = line + x - 1;
        *dst = t->lut[(uint32_t)first << 8 | *dst];
    }
}

/* One row pair: `l` gives the left end, `r` (dithered) the start of the walk. */
static void tex_row(TexCtx *t, int clipped, int row, const int32_t *l, const int32_t *r) {
    if (row < 0 || row >= t->fb->h) return; /* the original writes outside the frame only on broken input */
    if (clipped && t->clip_x0 > l[2])
        tex_span(t, row, t->clip_x0, r[2] - t->clip_x0, NULL, r);
    else
        tex_span(t, row, l[2] + 1, r[2] - l[2] - 1, l, r);
}

/* sub_00079174: x step per y of an edge (8.8), with the flat-edge sentinels. */
static int32_t tex_slope(int32_t xa, int32_t ya, int32_t xb, int32_t yb) {
    if (ya == yb) return xa > xb ? (int32_t)0x80010000u : 0x7fff0000;
    return (int32_t)(int64_t)((int32_t)((uint32_t)(xa - xb) << 8)) / (ya - yb);
}

/* sub_00078304: v = {x0, y0, x1, y1, x2, y2, u0, v0, u1, v1, u2, v2}. */
static void tex_triangle(TexCtx *t, const int32_t in[12]) {
    int32_t v[12];
    memcpy(v, in, sizeof v);
    int32_t xmax = v[0] > v[2] ? v[0] : v[2], xmin = v[0] > v[2] ? v[2] : v[0];
    if (v[4] > xmax) xmax = v[4];
    else if (v[4] < xmin) xmin = v[4];
#define SWAP_VERTEX(a, b)                                                                                          \
    do {                                                                                                           \
        int32_t s_;                                                                                                \
        s_ = v[2 * a], v[2 * a] = v[2 * b], v[2 * b] = s_;                                                         \
        s_ = v[2 * a + 1], v[2 * a + 1] = v[2 * b + 1], v[2 * b + 1] = s_;                                         \
        s_ = v[6 + 2 * a], v[6 + 2 * a] = v[6 + 2 * b], v[6 + 2 * b] = s_;                                         \
        s_ = v[7 + 2 * a], v[7 + 2 * a] = v[7 + 2 * b], v[7 + 2 * b] = s_;                                         \
    } while (0)
    if (v[1] > v[5]) SWAP_VERTEX(0, 2);
    if (v[3] < v[1]) SWAP_VERTEX(0, 1);
    else if (v[3] > v[5]) SWAP_VERTEX(1, 2);
#undef SWAP_VERTEX
    int clipped = v[1] < t->top || v[5] > t->bottom || xmax > t->right || xmin < t->left;
    if (clipped && (v[5] < t->top || v[1] > t->bottom || xmin > t->right || xmax < t->left)) return; /* 0x78ac0 */
    tex_gradients(t, v);
    TexEdge e01 = {v[0], v[1], v[2], v[3], v[6], v[7], v[8], v[9]};
    TexEdge e02 = {v[0], v[1], v[4], v[5], v[6], v[7], v[10], v[11]};
    TexEdge e12 = {v[2], v[3], v[4], v[5], v[8], v[9], v[10], v[11]};
    int long_right = tex_slope(v[0], v[1], v[2], v[3]) < tex_slope(v[0], v[1], v[4], v[5]);
    /* The left edges walk integer texels, the right ones the dithered 8.8 start of each span. */
    if (clipped) {
        tex_edge_clipped(t, &t->a, e01, !long_right);
        tex_edge_clipped(t, &t->b, e02, long_right);
        tex_edge_clipped(t, &t->c, e12, !long_right);
    } else {
        tex_edge(t, &t->a, &e01, !long_right);
        tex_edge(t, &t->b, &e02, long_right);
        tex_edge(t, &t->c, &e12, !long_right);
    }
    int row = long_right ? t->a.start : t->b.start;
    int n1 = clamp_rows(t->a.count), n2 = clamp_rows(t->c.count);
    for (int k = 0; k < n1; k++, row++) {
        if (k >= TEX_EDGE_MAX) break;
        if (long_right) tex_row(t, clipped, row, t->a.e[k], t->b.e[k]);
        else tex_row(t, clipped, row, t->b.e[k], t->a.e[k]);
    }
    for (int j = 0; j < n2 && n1 + j < TEX_EDGE_MAX; j++, row++) {
        if (long_right) tex_row(t, clipped, row, t->c.e[j], t->b.e[n1 + j]);
        else tex_row(t, clipped, row, t->b.e[n1 + j], t->c.e[j]);
    }
}

void ign_sprite_draw_matrix(IgnFrame *fb, const IgnClip *clip, const uint8_t page[65536], int u0, int v0, int w,
                            int h, const int32_t m[4], int32_t x, int32_t y, const uint8_t *lut) {
    static TexCtx t;
    IgnClip c;
    clip_to_frame(fb, clip, &c);
    t.fb = fb, t.page = page, t.lut = lut;
    /* sub_00062040: the 8.8 window, x1 and y1 exclusive. */
    t.left = c.x0 << 8, t.top = c.y0 << 8, t.right = c.x1 << 8, t.bottom = c.y1 << 8;
    t.clip_x0 = c.x0, t.clip_y0 = c.y0, t.clip_y1 = c.y1;
    /* sub_00082950 with a zero hotspot: corners pos, pos + width, pos + height (texture extents minus 1/16). */
    int32_t uu0 = u0 << 8, vv0 = v0 << 8, uu1 = ((u0 + w) << 8) - 0x10, vv1 = ((v0 + h) << 8) - 0x10;
    int32_t wx = mul16(uu1 - uu0, m[0]), wy = mul16(uu1 - uu0, m[1]);
    int32_t hx = mul16(vv1 - vv0, m[2]), hy = mul16(vv1 - vv0, m[3]);
    int32_t p0x = x, p0y = y, p1x = wx + x, p1y = wy + y, p2x = hx + x, p2y = hy + y;
    int32_t p3x = hx + wx + x, p3y = hy + wy + y;
    const int32_t t1[12] = {p0x, p0y, p1x, p1y, p2x, p2y, uu0, vv0, uu1, vv0, uu0, vv1};
    const int32_t t2[12] = {p1x, p1y, p2x, p2y, p3x, p3y, uu1, vv0, uu0, vv1, uu1, vv1};
    tex_triangle(&t, t1);
    tex_triangle(&t, t2);
}

void ign_sprite_draw_scaled(IgnFrame *fb, const IgnClip *clip, const IgnSprite *s, int x, int y, float scale,
                            const uint8_t *lut) {
    if (!s->pixels || s->w > 256 || s->h > 256) return;
    /* geputget copies the sprite into a 256 x 256 texture page (0x68e4c); the port puts it at the page origin. */
    static uint8_t page[65536];
    memset(page, 0, sizeof page);
    for (int r = 0; r < s->h; r++) memcpy(page + r * 256, s->pixels + (size_t)r * s->stride, (size_t)s->w);
    static uint8_t opaque[65536];
    if (!lut) {
        for (int a = 0; a < 256; a++)
            for (int b = 0; b < 256; b++) opaque[a << 8 | b] = a ? (uint8_t)a : (uint8_t)b;
        lut = opaque;
    }
    /* sub_00062184: S = trunc(scale * 65536.0f); the matrix is {S cos a, S sin a, S sin a, S cos a}, angle 0. */
    int32_t S = (int32_t)(scale * 65536.0f);
    const int32_t m[4] = {S, 0, 0, S};
    ign_sprite_draw_matrix(fb, clip, page, 0, 0, s->w, s->h, m, x << 8, y << 8, lut);
}

/* 0x60dc4: reciprocal table at 0xc9168, entry n = 65536 / (n + 1) - 1, indexed by a span height. */
static int32_t recip(int n) {
    return (int32_t)(65536u / (uint32_t)(n + 1)) - 1;
}

typedef struct {
    uint32_t x;  /* 16.16 edge position: integer in the high word, used as a signed 16-bit value */
    int32_t step;
} Edge;

static inline int edge_x(uint32_t v) {
    return (int16_t)(v >> 16);
}

/* Slope of an edge of height dy: (dx + 1) * recip(dy) moving right, (dx - 1) * recip(dy) moving left. */
static int32_t slope(int dir, int dx, int dy) {
    return (int32_t)((uint32_t)(dx + dir) * (uint32_t)recip(dy));
}

/* Edge set-up of 0x729f3. Each site tests the direction with its own sign rule (`zero_right`: dx == 0 counts as
 * moving right). Left edges moving right start at x.0000; moving left at x.ffff with one step already applied.
 * Right edges moving right start at x.0000 plus one step; moving left at x.ffff. */
static Edge left_edge(int x, int dx, int dy, int zero_right) {
    int right = dx > 0 || (zero_right && dx == 0);
    Edge e = {(uint32_t)x << 16, slope(right ? 1 : -1, dx, dy)};
    if (!right) e.x = (e.x | 0xffff) + (uint32_t)e.step;
    return e;
}

static Edge right_edge(int x, int dx, int dy, int zero_right) {
    int right = dx > 0 || (zero_right && dx == 0);
    Edge e = {(uint32_t)x << 16, slope(right ? 1 : -1, dx, dy)};
    if (right) e.x += (uint32_t)e.step;
    else e.x |= 0xffff;
    return e;
}

static void span(IgnFrame *fb, const IgnClip *c, int y, int l, int r, uint8_t color) {
    if (l < c->x0) {
        if (r < c->x0) return;
        l = c->x0;
    }
    if (r > c->x1 - 1) {
        if (l > c->x1 - 1) return;
        r = c->x1 - 1;
    }
    for (int x = l; x <= r; x++) fb->color[y * fb->w + x] = color; /* both ends included */
}

/* Rows y .. y + count. `pre_step` advances the edges before each row (second half of a split triangle). */
static void run(IgnFrame *fb, const IgnClip *c, int y, int count, Edge *l, Edge *r, int pre_step, uint8_t color) {
    if (y < c->y0) {
        int skip = c->y0 - y;
        count -= skip;
        if (count < 0) return;
        l->x += (uint32_t)(l->step * skip);
        r->x += (uint32_t)(r->step * skip);
        y = c->y0;
    }
    if (y + count > c->y1 - 1) {
        count -= y + count - (c->y1 - 1);
        if (count < 0) return;
    }
    for (; count >= 0; count--, y++) {
        if (pre_step) l->x += (uint32_t)l->step, r->x += (uint32_t)r->step;
        span(fb, c, y, edge_x(l->x), edge_x(r->x), color);
        if (!pre_step) l->x += (uint32_t)l->step, r->x += (uint32_t)r->step;
    }
}

void ign_fill_triangle(IgnFrame *fb, const IgnClip *clip, const int xy[6], uint8_t color, const uint8_t *lut) {
    IgnClip c;
    clip_to_frame(fb, clip, &c);
    /* 0x72d44: the colour goes through the state's shade table ([0xc484c], the level PAN in the HUD) with the
     * primitive's +0x2d byte, 0 here. */
    if (lut) color = lut[color << 8];
    int v[3][2];
    for (int i = 0; i < 3; i++) {
        v[i][0] = xy[i * 2] < -32000 ? -32000 : xy[i * 2] > 32000 ? 32000 : xy[i * 2];
        v[i][1] = xy[i * 2 + 1];
    }
    /* 0x729f3: a = top, b = middle, d = bottom, with the original's exchanges (strict comparisons). */
    int a, b, d;
    if (v[1][1] >= v[0][1]) {
        if (v[1][1] < v[2][1]) a = 0, b = 1, d = 2;
        else if (v[0][1] < v[2][1]) a = 0, b = 2, d = 1;
        else a = 2, b = 0, d = 1;
    } else {
        if (v[2][1] > v[0][1]) a = 1, b = 0, d = 2;
        else if (v[2][1] > v[1][1]) a = 1, b = 2, d = 0;
        else a = 2, b = 1, d = 0;
    }
    int xa = v[a][0], ya = v[a][1], xb = v[b][0], yb = v[b][1], xd = v[d][0], yd = v[d][1];
    if (yd - ya > 15000) return;
    if (ya == yd) {
        /* 0x7300d: one row from the leftmost to the rightmost vertex. */
        int l = xa < xb ? xa : xb, r = xa < xb ? xb : xa;
        if (xd > r) r = xd;
        else if (xd <= l) l = xd;
        Edge el = {(uint32_t)l << 16, 0}, er = {(uint32_t)r << 16, 0};
        run(fb, &c, ya, 0, &el, &er, 0, color);
        return;
    }
    if (ya == yb) {
        /* 0x73046: flat top, both edges down to the bottom vertex. */
        int l = xa <= xb ? xa : xb, r = xa <= xb ? xb : xa, h = yd - ya;
        Edge er = right_edge(r, xd - r, h, 0), el = left_edge(l, xd - l, h, 1);
        run(fb, &c, ya, h, &el, &er, 0, color);
        return;
    }
    if (yb == yd) {
        /* 0x730ef: flat bottom, both edges from the top vertex. */
        int l = xb <= xd ? xb : xd, r = xb <= xd ? xd : xb, h = yd - ya;
        Edge er = right_edge(xa, r - xa, h, 0), el = left_edge(xa, l - xa, h, 1);
        run(fb, &c, ya, h, &el, &er, 0, color);
        return;
    }
    /* 0x72afe: split at the middle vertex; the long edge a-d crosses its row at mid. */
    int h1 = yb - ya, h2 = yd - yb, h = yd - ya;
    int mid = (int)(((int32_t)((uint32_t)(xd - xa) * (uint32_t)recip(h) * (uint32_t)h1) + 0x8000) >> 16) + xa;
    Edge l1, r1, l2, r2;
    if (mid < xb) {
        l1 = left_edge(xa, xd - xa, h, 0);
        l2 = l1;
        l2.x = ((uint32_t)mid << 16) | (xd - xa > 0 ? 0 : 0xffff);
        if (xd - xa <= 0) l2.x += (uint32_t)l1.step;
        r1 = right_edge(xa, xb - xa, h1, 1);
        r2 = right_edge(xb, xd - xb, h2, 0);
    } else {
        r1 = right_edge(xa, xd - xa, h, 1);
        r2 = r1;
        r2.x = ((uint32_t)mid << 16) | (xd - xa >= 0 ? 0 : 0xffff);
        if (xd - xa >= 0) r2.x += (uint32_t)r1.step;
        l1 = left_edge(xa, xb - xa, h1, 0);
        l2 = left_edge(xb, xd - xb, h2, 1);
    }
    run(fb, &c, ya, h1, &l1, &r1, 0, color);
    run(fb, &c, yb + 1, h2 - 1, &l2, &r2, 1, color);
}
