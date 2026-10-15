/* Rasterizer of the Lisa 2 primitive list (sub_00060e3d and the hand-written routines it dispatches to through
 * the table 0xd7bd0). Integer arithmetic follows the original instruction by instruction so the frames match
 * pixel for pixel; comments name the original routine of each step. */
#include "lisa.h"

#include <string.h>

/* High 32 bits of a signed 32 x 32 product (imul; result in edx). */
static int32_t hi32(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 32); }
/* Bits 16..47 of a signed product: `imul; shl edx, 16; shr eax, 16; or eax, edx`. */
static int32_t mid32(int32_t a, int32_t b) { return (int32_t)(uint32_t)((uint64_t)((int64_t)a * b) >> 16); }
/* `xor eax, eax; div ecx` with edx = n: (n << 32) / d, unsigned. Callers guarantee n < d. */
static uint32_t frac32(uint32_t n, uint32_t d) { return (uint32_t)(((uint64_t)n << 32) / d); }
/* `mov edx, eax; sar edx, 16; shl eax, 16; idiv ecx`. */
static int32_t div16(int32_t n, int32_t d) { return (int32_t)(((int64_t)n * 65536) / d); }

void ign_raster_clip(IgnRaster *r, int x0, int y0, int x1, int y1) {
    /* sub_00060e3d 0x60e5d: clip rectangle in pixels and in 8.8 (the far edges are exclusive). */
    r->xmin = x0, r->ymin = y0;
    r->ymax1 = y1 + 1;
    r->xmin8 = x0 << 8, r->ymin8 = y0 << 8;
    r->xmax8 = (x1 + 1) << 8, r->ymax8 = (y1 + 1) << 8;
}

/* ---- textured affine triangle, primitive 0x11 (sub_00073c58) ---- */

typedef struct {
    int32_t dudx, dvdx, width;   /* 0xe6c30, 0xe6c34, 0xe6c40 */
    int32_t tu[8], tv[8];        /* 0xe6c44 / 0xe6c64: k * (-d/dx >> 3), the subpixel x prestep */
    uint8_t step_u;              /* 0xe6c24: integer byte of -du/dx */
    uint32_t step;               /* 0xe6c28: frac(-du/dx) << 16 | -dv/dx 8.8 */
    /* edge state */
    int32_t eu, ev, ex, dudy, dvdy, dxdy; /* 0xe6c88, 0xe6c8c, 0xe6c9c, 0xe6c90, 0xe6c94, 0xe6ca0 */
    int split;                   /* 0xe6ca4 */
    int32_t sx, sy, su, sv;      /* 0xe6ca8..0xe6cb4: end point saved by the x clip */
} TexState;

typedef struct {
    int32_t v, u, x;
} UvRow;

/* sub_00090000: gradients of the triangle, vertices sorted top/middle/bottom; t = {x, y} x 3, uv = {u, v} x 3. */
static void tex_gradients(TexState *s, const int32_t *t, const int32_t *uv) {
    uint32_t dm = (uint32_t)(t[3] - t[1]), db = (uint32_t)(t[5] - t[1]);
    int32_t ratio = db == dm ? 0x7fffffff : (int32_t)(frac32(dm, db) >> 1);
    int32_t width = hi32((t[4] - t[0]) * 2, ratio) - (t[2] - t[0]);
    int32_t dv_long = hi32((uv[5] - uv[1]) * 2, ratio);
    int32_t du_span = hi32((uv[4] - uv[0]) * 2, ratio) - uv[2] + uv[0];
    int32_t dv_span = dv_long - uv[3] + uv[1];
    s->width = width;
    int32_t mag = (width >> 31) ^ width;
    if (mag > 0x10000) {
        s->dudx = div16(du_span, width);
        s->dvdx = div16(dv_span, width);
    } else {
        int32_t recip;
        if (mag > 3) recip = (int32_t)(0x100000000LL / width);
        else recip = width >= 0 ? 0x7fffffff : (int32_t)0x80000000;
        s->dudx = mid32(du_span, recip);
        s->dvdx = mid32(dv_span, recip);
    }
    int32_t nu = -s->dudx, nv = -s->dvdx;
    s->tu[0] = s->tv[0] = 0;
    for (int k = 1; k < 8; k++) s->tu[k] = s->tu[k - 1] + (nu >> 3), s->tv[k] = s->tv[k - 1] + (nv >> 3);
    s->step_u = (uint8_t)((uint32_t)nu >> 16);
    s->step = ((uint32_t)nu << 16) | (((uint32_t)nv >> 8) & 0xffff);
}

/* sub_00090790: slopes and the prestep to the first scanline (y rounded up, a whole row when y is exact). */
static void uv_edge_setup(TexState *s, const int32_t *e) {
    uint32_t dy = (uint32_t)(e[3] - e[1]);
    if (dy <= 0x10000) {
        int32_t recip = dy <= 2 ? 0x7fffffff : (int32_t)(0x100000000ULL / dy);
        s->dvdy = mid32(e[7] - e[5], recip);
        s->dudy = mid32(e[6] - e[4], recip);
        s->dxdy = mid32(e[2] - e[0], recip);
    } else {
        s->dvdy = div16(e[7] - e[5], (int32_t)dy);
        s->dudy = div16(e[6] - e[4], (int32_t)dy);
        s->dxdy = div16(e[2] - e[0], (int32_t)dy);
    }
    int32_t f = 0x100 - (e[1] & 0xff);
    s->eu = (int32_t)((uint32_t)e[4] << 8) + ((f * s->dudy) >> 8);
    s->ev = (int32_t)((uint32_t)e[5] << 8) + ((f * s->dvdy) >> 8);
    s->ex = (int32_t)((uint32_t)e[0] << 8) + f * (s->dxdy >> 8);
}

/* sub_00090a00: x-only edge. */
static void x_edge_setup(TexState *s, const int32_t *e) {
    uint32_t dy = (uint32_t)(e[3] - e[1]);
    if (dy <= 0x10000) {
        int32_t recip = dy <= 2 ? 0x7fffffff : (int32_t)(0x100000000ULL / dy);
        s->dxdy = mid32(e[2] - e[0], recip);
    } else {
        s->dxdy = div16(e[2] - e[0], (int32_t)dy);
    }
    int32_t f = 0x100 - (e[1] & 0xff);
    s->ex = (int32_t)((uint32_t)e[0] << 8) + f * (s->dxdy >> 8);
}

/* sub_000904e0: one entry per scanline, uv corrected by the top 3 bits of the x fraction. */
static UvRow *uv_edge_rows(TexState *s, UvRow *out, int n) {
    int32_t u = s->eu, v = s->ev, x = s->ex;
    for (; n > 0; n--, out++) {
        int k = (int)(((uint32_t)x >> 13) & 7);
        out->u = u + s->tu[k];
        out->v = v + s->tv[k];
        out->x = x >> 16;
        u += s->dudy, x += s->dxdy, v += s->dvdy;
    }
    return out;
}

/* sub_00090470 / sub_000904a0 (clamped to the left clip column): x of each scanline, 48-bit accumulation. */
static int32_t *x_edge_rows(const TexState *s, int32_t *out, int n, const int32_t *xmin) {
    int64_t x = (int64_t)s->ex * 65536;
    for (; n > 0; n--, out++) {
        int32_t xi = (int32_t)(x >> 32);
        *out = xmin && xi - *xmin < 0 ? *xmin : xi;
        x += (int64_t)s->dxdy * 65536;
    }
    return out;
}

/* sub_000824b4: spans from the plain (left) edge to the uv (right) edge, drawn right to left; pixel x_r - 1 - k
 * takes the texel of step k. The packed DDA lets the v carry leak into the u fraction, as the original does. */
static void tex_spans(const IgnRaster *r, const TexState *s, const uint8_t *tex, const int32_t *left,
                      const UvRow *right, int rows, int y) {
    for (; rows > 0; rows--, left++, right++, y++) {
        int32_t n = right->x - *left;
        if (n <= 0) continue;
        uint8_t *dst = r->fb + (size_t)y * r->stride + *left;
        uint32_t ecx = ((uint32_t)right->u << 16) | (((uint32_t)right->v >> 8) & 0xffff);
        uint8_t bl = (uint8_t)((uint32_t)right->u >> 16), bh = (uint8_t)(ecx >> 8);
        for (int32_t k = 0; k < n; k++) {
            dst[n - 1 - k] = tex[(bh << 8) | bl];
            uint32_t sum = ecx + s->step;
            unsigned carry = sum < ecx;
            ecx = sum;
            bh = (uint8_t)(ecx >> 8);
            bl = (uint8_t)(bl + s->step_u + carry);
        }
    }
}

/* sub_00090530: clip a uv edge {x0, y0, x1, y1, u0, v0, u1, v1} to the rows and to the right column. */
static void uv_edge_clip(const IgnRaster *r, TexState *s, int32_t *e) {
    if (r->ymin8 > e[1]) {
        int32_t t = (int32_t)(frac32((uint32_t)(r->ymin8 - e[1]), (uint32_t)(e[3] - e[1])) >> 1);
        e[5] += hi32((e[7] - e[5]) * 2, t);
        e[4] += hi32((e[6] - e[4]) * 2, t);
        e[0] += hi32((e[2] - e[0]) * 2, t);
        e[1] = r->ymin8;
    }
    if (r->ymax8 < e[3]) {
        int32_t t = (int32_t)(frac32((uint32_t)(e[3] - r->ymax8), (uint32_t)(e[3] - e[1])) >> 1);
        e[7] -= hi32((e[7] - e[5]) * 2, t);
        e[6] -= hi32((e[6] - e[4]) * 2, t);
        e[2] -= hi32((e[2] - e[0]) * 2, t);
        e[3] = r->ymax8;
    }
    s->split = 0;
    if (r->xmax8 < e[0]) {
        if (r->xmax8 > e[2]) {
            s->split = 1;
            s->sx = e[2], s->sy = e[3], s->su = e[6], s->sv = e[7];
            int32_t t = (int32_t)(frac32((uint32_t)(r->xmax8 - e[2]), (uint32_t)(e[0] - e[2])) >> 1);
            e[6] -= hi32((e[6] - e[4]) * 2, t);
            e[7] -= hi32((e[7] - e[5]) * 2, t);
            e[3] -= hi32((e[3] - e[1]) * 2, t);
            e[2] = r->xmax8;
        } else {
            int32_t c = e[2] - r->xmax8;
            e[6] -= mid32(c, s->dudx);
            e[7] -= mid32(c, s->dvdx);
            e[2] = r->xmax8;
        }
        int32_t c = e[0] - r->xmax8;
        e[4] -= mid32(c, s->dudx);
        e[5] -= mid32(c, s->dvdx);
        e[0] = r->xmax8;
    }
    if (r->xmax8 < e[2]) {
        s->split = 1;
        s->sx = e[2], s->sy = e[3], s->su = e[6], s->sv = e[7];
        uint32_t num = (uint32_t)(e[2] - r->xmax8), den = (uint32_t)(e[2] - e[0]);
        int32_t t = (int32_t)((num == den ? 0xffffffffu : frac32(num, den)) >> 1);
        e[6] -= hi32((e[6] - e[4]) * 2, t);
        e[7] -= hi32((e[7] - e[5]) * 2, t);
        e[3] -= hi32((e[3] - e[1]) * 2, t);
        e[2] = r->xmax8;
    }
}

/* sub_000908a0: the same for an x-only edge {x0, y0, x1, y1}. */
static void x_edge_clip(const IgnRaster *r, TexState *s, int32_t *e) {
    if (r->ymin8 > e[1]) {
        int32_t t = (int32_t)(frac32((uint32_t)(r->ymin8 - e[1]), (uint32_t)(e[3] - e[1])) >> 1);
        e[0] += hi32((e[2] - e[0]) * 2, t);
        e[1] = r->ymin8;
    }
    if (r->ymax8 < e[3]) {
        int32_t t = (int32_t)(frac32((uint32_t)(e[3] - r->ymax8), (uint32_t)(e[3] - e[1])) >> 1);
        e[2] -= hi32((e[2] - e[0]) * 2, t);
        e[3] = r->ymax8;
    }
    s->split = 0;
    if (r->xmax8 < e[0]) {
        if (r->xmax8 > e[2]) {
            s->split = 1;
            s->sx = e[2], s->sy = e[3];
            int32_t t = (int32_t)(frac32((uint32_t)(r->xmax8 - e[2]), (uint32_t)(e[0] - e[2])) >> 1);
            e[3] -= hi32((e[3] - e[1]) * 2, t);
        }
        e[2] = r->xmax8;
        e[0] = r->xmax8;
    }
    if (r->xmax8 < e[2]) {
        s->split = 1;
        s->sx = e[2], s->sy = e[3];
        uint32_t num = (uint32_t)(e[2] - r->xmax8), den = (uint32_t)(e[2] - e[0]);
        int32_t t = (int32_t)((num == den ? 0xffffffffu : frac32(num, den)) >> 1);
        e[3] -= hi32((e[3] - e[1]) * 2, t);
        e[2] = r->xmax8;
    }
}

/* Scanline buffers of 0x260be8 (x) and 0x260bec (uv); y of the first row comes first, then the count. */
typedef struct {
    int32_t y, n;
    int32_t x[IGN_RASTER_ROWS];
} XEdge;
typedef struct {
    int32_t n;
    UvRow row[IGN_RASTER_ROWS];
} UvEdge;

/* sub_00090240 / sub_00090200: unclipped edges. */
static void x_edge(TexState *s, XEdge *out, const int32_t *e) {
    out->y = e[1] >> 8;
    out->n = (e[3] >> 8) - out->y;
    if (out->n) {
        x_edge_setup(s, e);
        x_edge_rows(s, out->x, out->n, NULL);
    }
}

static void uv_edge(TexState *s, UvEdge *out, const int32_t *e) {
    out->n = (e[3] >> 8) - (e[1] >> 8);
    if (out->n) {
        uv_edge_setup(s, e);
        uv_edge_rows(s, out->row, out->n);
    }
}

/* sub_00090380: clipped x edge; rows left of the clip column are clamped to it. */
static void x_edge_clipped(const IgnRaster *r, TexState *s, XEdge *out, int32_t *e) {
    if (!(r->ymin8 < e[3])) {
        out->n = 0, out->y = r->ymin;
        return;
    }
    if (!(r->ymax8 > e[1])) {
        out->n = 0, out->y = r->ymax1;
        return;
    }
    x_edge_clip(r, s, e);
    out->y = e[1] >> 8;
    out->n = (e[3] >> 8) - out->y;
    int32_t *next = out->x;
    if (out->n) {
        x_edge_setup(s, e);
        next = x_edge_rows(s, out->x, out->n, &r->xmin);
    }
    if (s->split) {
        e[0] = e[2], e[1] = e[3];
        e[2] = s->sx, e[3] = s->sy;
        if (r->xmax8 < e[2]) e[2] = r->xmax8; /* sub_000909da */
        int32_t n = (e[3] >> 8) - (e[1] >> 8);
        if (n) {
            out->n += n;
            x_edge_setup(s, e);
            x_edge_rows(s, next, n, &r->xmin);
        }
    }
}

/* sub_00090270: clipped uv edge; the part right of the clip column becomes a vertical run on it. */
static void uv_edge_clipped(const IgnRaster *r, TexState *s, UvEdge *out, int32_t *e) {
    if (!(r->ymin8 < e[3]) || !(r->ymax8 > e[1])) {
        out->n = 0;
        return;
    }
    uv_edge_clip(r, s, e);
    out->n = (e[3] >> 8) - (e[1] >> 8);
    UvRow *next = out->row;
    if (out->n) {
        uv_edge_setup(s, e);
        next = uv_edge_rows(s, out->row, out->n);
    }
    if (s->split) {
        e[0] = e[2], e[1] = e[3], e[4] = e[6], e[5] = e[7];
        e[2] = s->sx, e[3] = s->sy, e[6] = s->su, e[7] = s->sv;
        if (r->xmax8 < e[2]) { /* sub_0009074b */
            int32_t c = e[2] - r->xmax8;
            e[6] -= mid32(c, s->dudx);
            e[7] -= mid32(c, s->dvdx);
            e[2] = r->xmax8;
        }
        int32_t n = (e[3] >> 8) - (e[1] >> 8);
        if (n) {
            out->n += n;
            uv_edge_setup(s, e);
            uv_edge_rows(s, next, n);
        }
    }
}

void ign_prim_tex(IgnRaster *r, const int32_t xy[6], const int32_t uv[6], const uint8_t *tex) {
    static XEdge xe;
    static UvEdge ue;
    TexState s;
    /* 0x73c60: x extent; then vertices 0 and 2 ordered by y (0 first on ties) and vertex 1 placed. */
    int top = 0, mid = 1, bot = 2;
    int32_t xmax = xy[0] > xy[2] ? xy[0] : xy[2], xmin = xy[0] > xy[2] ? xy[2] : xy[0];
    if (xy[4] > xmax) xmax = xy[4];
    else if (xy[4] < xmin) xmin = xy[4];
    if (xy[1] > xy[5]) top = 2, bot = 0;
    if (xy[3] < xy[top * 2 + 1]) mid = top, top = 1;
    else if (xy[3] > xy[bot * 2 + 1]) mid = bot, bot = 1;
    const int32_t t[6] = {xy[top * 2], xy[top * 2 + 1], xy[mid * 2], xy[mid * 2 + 1], xy[bot * 2], xy[bot * 2 + 1]};
    const int32_t tuv[6] = {uv[top * 2], uv[top * 2 + 1], uv[mid * 2], uv[mid * 2 + 1], uv[bot * 2], uv[bot * 2 + 1]};
    int inside = ((t[1] - r->ymin8) | (xmin - r->xmin8) | (r->ymax8 - t[5]) | (r->xmax8 - xmax)) >= 0;
    if (!inside && ((t[5] - r->ymin8) | (xmax - r->xmin8) | (r->ymax8 - t[1]) | (r->xmax8 - xmin)) < 0) return;
    tex_gradients(&s, t, tuv);
    int32_t top_mid[8] = {t[0], t[1], t[2], t[3], tuv[0], tuv[1], tuv[2], tuv[3]};
    int32_t mid_bot[8] = {t[2], t[3], t[4], t[5], tuv[2], tuv[3], tuv[4], tuv[5]};
    int32_t top_bot[8] = {t[0], t[1], t[4], t[5], tuv[0], tuv[1], tuv[4], tuv[5]};
    if (s.width > 0) {
        /* Long edge on the right: it carries the uv; the left side is top-middle then middle-bottom. */
        if (inside) x_edge(&s, &xe, top_mid), uv_edge(&s, &ue, top_bot);
        else x_edge_clipped(r, &s, &xe, top_mid), uv_edge_clipped(r, &s, &ue, top_bot);
        int y = xe.y, n1 = xe.n;
        tex_spans(r, &s, tex, xe.x, ue.row, n1, y);
        if (inside) x_edge(&s, &xe, mid_bot);
        else x_edge_clipped(r, &s, &xe, mid_bot);
        tex_spans(r, &s, tex, xe.x, ue.row + n1, xe.n, y + n1);
    } else {
        if (inside) uv_edge(&s, &ue, top_mid), x_edge(&s, &xe, top_bot);
        else uv_edge_clipped(r, &s, &ue, top_mid), x_edge_clipped(r, &s, &xe, top_bot);
        int y = xe.y, n1 = ue.n;
        tex_spans(r, &s, tex, xe.x, ue.row, n1, y);
        if (inside) uv_edge(&s, &ue, mid_bot);
        else uv_edge_clipped(r, &s, &ue, mid_bot);
        tex_spans(r, &s, tex, xe.x + n1, ue.row, ue.n, y + n1);
    }
}
