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
    r->xmax1 = x1 + 1, r->ymax1 = y1 + 1;
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
static void tex_spans(const IgnRaster *r, const TexState *s, const uint8_t *tex, const uint8_t *lut,
                      const int32_t *left, const UvRow *right, int rows, int y) {
    for (; rows > 0; rows--, left++, right++, y++) {
        int32_t n = right->x - *left;
        if (n <= 0) continue;
        uint8_t *dst = r->fb + (size_t)y * r->stride + *left;
        uint32_t ecx = ((uint32_t)right->u << 16) | (((uint32_t)right->v >> 8) & 0xffff);
        uint8_t bl = (uint8_t)((uint32_t)right->u >> 16), bh = (uint8_t)(ecx >> 8);
        for (int32_t k = 0; k < n; k++) {
            uint8_t t = tex[(bh << 8) | bl];
            dst[n - 1 - k] = lut ? lut[(t << 8) | dst[n - 1 - k]] : t; /* sub_000825b8 blends through 0xe6cbc */
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

void ign_prim_tex(IgnRaster *r, const int32_t xy[6], const int32_t uv[6], const uint8_t *tex, const uint8_t *lut) {
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
        tex_spans(r, &s, tex, lut, xe.x, ue.row, n1, y);
        if (inside) x_edge(&s, &xe, mid_bot);
        else x_edge_clipped(r, &s, &xe, mid_bot);
        tex_spans(r, &s, tex, lut, xe.x, ue.row + n1, xe.n, y + n1);
    } else {
        if (inside) uv_edge(&s, &ue, top_mid), x_edge(&s, &xe, top_bot);
        else uv_edge_clipped(r, &s, &ue, top_mid), x_edge_clipped(r, &s, &xe, top_bot);
        int y = xe.y, n1 = ue.n;
        tex_spans(r, &s, tex, lut, xe.x, ue.row, n1, y);
        if (inside) uv_edge(&s, &ue, mid_bot);
        else uv_edge_clipped(r, &s, &ue, mid_bot);
        tex_spans(r, &s, tex, lut, xe.x + n1, ue.row, ue.n, y + n1);
    }
}

/* ---- perspective textured triangle, primitives 0x14 and 0x15 (sub_0007586c) ----
 * The setup (sub_00075090, sub_000755ec) runs with the default x87 control word (approximated here with double
 * arithmetic); the row and span code (sub_0007616c on) switches the FPU to 24-bit precision, emulated exactly:
 * every arithmetic result is the exact value rounded once to float. fistp rounds to nearest even. */

#include <math.h>

/* Round p + e (e the error of p, |e| <= ulp(p) / 2) once to float. */
static double round24(double p, double e) {
    float f = (float)p;
    if (e == 0 || (double)f == p) return f;
    float g = nextafterf(f, p > (double)f ? INFINITY : -INFINITY);
    if (p != ((double)f + (double)g) * 0.5) return f;
    return (e > 0) == ((double)g > (double)f) ? g : f;
}
/* Precision control: 24-bit while set. The blended span (0x76df4) restores the saved control word, so the rows
 * after a drawn 0x15 span run in extended precision (approximated with double) until the next half. */
static int fpu_single;
static double s_mul(double a, double b) {
    double p = a * b;
    return fpu_single ? round24(p, fma(a, b, -p)) : p;
}
static double s_add(double a, double b) {
    double s = a + b, bb = s - a;
    return fpu_single ? round24(s, (a - (s - bb)) + (b - bb)) : s;
}
static double s_div(double a, double b) {
    double q = a / b, r = fma(-q, b, a);
    return fpu_single ? round24(q, (r > 0) == (b > 0) ? (r == 0 ? 0 : 1) : -1) : q;
}
static int32_t s_int(double v) { return (int32_t)lrint(v); }
/* bits of the float v + 1.5 * 2^23 (the integer trick of the span code) */
static uint32_t s_magic(double v) {
    float f = (float)s_add(v, 12582912.0);
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

typedef struct {
    int32_t x[3], y[3], l[3], u[3], v[3]; /* top, middle, bottom (0x260c58...) */
    int long_right;                       /* 0xe6650 */
    /* long edge (top -> bottom) */
    float slope_l, k_l, c_l, len_l;       /* 0xe6620, 0xe662c, 0xe6628, 0xe65f4 */
    int32_t du_l, dv_l, dl_l;             /* 0xe6638, 0xe663c, 0xe6648 */
    /* short edges */
    float slope_tm, k_tm, c_tm, len_tm;   /* 0xe6624, 0xe6634, 0xe6630, 0xe6600 */
    float slope_mb, k_mb, c_mb, len_mb;   /* 0x260c44, 0x260c3c, 0x260c4c, 0x260c20 */
    int32_t du_tm, dv_tm, dl_tm, du_mb, dv_mb, dl_mb;
} PerspState;

/* sub_00075440: vertices by y (first minimum and first maximum win); the middle one by elimination. */
static int persp_sort(PerspState *s, const int32_t xy[6], const int32_t light[3], const int32_t uv[6]) {
    int lo = 0, hi = 0;
    for (int k = 1; k < 3; k++) {
        if (xy[k * 2 + 1] < xy[lo * 2 + 1]) lo = k;
        if (xy[k * 2 + 1] > xy[hi * 2 + 1]) hi = k;
    }
    int mid = lo == hi ? -1 : 3 - lo - hi;
    if (mid < 0) return -1; /* all on one row: the original reads before the record; nothing is drawn */
    const int order[3] = {lo, mid, hi};
    for (int i = 0; i < 3; i++) {
        int k = order[i];
        s->x[i] = xy[k * 2], s->y[i] = xy[k * 2 + 1], s->l[i] = light[k], s->u[i] = uv[k * 2], s->v[i] = uv[k * 2 + 1];
    }
    return 0;
}

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static int32_t longest(int32_t dx, int32_t dy) { return iabs(dx) > iabs(dy) ? iabs(dx) : iabs(dy); }

/* sub_00075090: perspective terms; the weight along an edge of length n (in 8.8 rows) is t / (t * k + c) with
 * t = row offset * n / dy, k = -(l1 - l0) / l0, c = l1 / l0 * n. */
static void persp_setup(PerspState *s) {
    double inv_top = 1.0 / s->l[0];
    int32_t len_tb = longest(s->x[2] - s->x[0], s->y[2] - s->y[0]);
    int32_t len_tm = longest(s->x[1] - s->x[0], s->y[1] - s->y[0]);
    int32_t len_mb = longest(s->x[2] - s->x[1], s->y[2] - s->y[1]);
    s->du_l = s->u[2] - s->u[0], s->dv_l = s->v[2] - s->v[0], s->dl_l = s->l[2] - s->l[0];
    s->du_tm = s->u[1] - s->u[0], s->dv_tm = s->v[1] - s->v[0], s->dl_tm = s->l[1] - s->l[0];
    s->du_mb = s->u[2] - s->u[1], s->dv_mb = s->v[2] - s->v[1], s->dl_mb = s->l[2] - s->l[1];
    double inv_mid = 1.0 / s->l[1];
    s->c_l = (float)(s->l[2] * inv_top * (double)len_tb);
    s->c_tm = (float)(s->l[1] * inv_top * len_tm);
    s->k_l = (float)(-s->dl_l * inv_top);
    s->k_tm = (float)(-s->dl_tm * inv_top);
    s->c_mb = (float)(s->l[2] * inv_mid * len_mb);
    s->k_mb = (float)(-s->dl_mb * inv_mid);
    s->slope_l = s->slope_tm = s->slope_mb = 0;
    s->len_l = s->len_tm = s->len_mb = 0;
    if (s->y[2] - s->y[0]) {
        double inv = 1.0 / (s->y[2] - s->y[0]);
        s->slope_l = (float)((s->x[2] - s->x[0]) * inv);
        s->len_l = (float)(inv * (double)len_tb);
    }
    if (s->y[1] - s->y[0]) {
        double inv = 1.0 / (s->y[1] - s->y[0]);
        s->slope_tm = (float)((s->x[1] - s->x[0]) * inv);
        s->len_tm = (float)(inv * len_tm);
    }
    if (s->y[2] - s->y[1]) {
        double inv = 1.0 / (s->y[2] - s->y[1]);
        s->slope_mb = (float)((s->x[2] - s->x[1]) * inv);
        s->len_mb = (float)(inv * len_mb);
    }
    s->long_right = !(s->slope_l < s->slope_tm);
    if (s->y[1] - s->y[0] == 0) s->long_right = s->x[0] > s->x[1];
}

typedef struct {
    int32_t x0, y0, l0, u0, v0;          /* short edge start (0xe6604...) */
    float slope, k, c;                    /* short edge */
    int32_t du, dv, dl;
    float len;                            /* 0xe6600 */
    int32_t pos_l, pos_s, limit;          /* 0xe65ec, 0xe65f8, 0xe65e8 */
    float t_l, t_s;                       /* 0xe65f0, 0xe65fc */
} PerspHalf;

static float inv_count[17]; /* 0xe66d8: 1 / n in 24-bit precision (sub_000765c3) */

/* sub_00076a94 0x76d52 / 0x76e03: the blended remainder and short spans step a plain packed u << 16 | v and read
 * u and v from the same state. */
static void blend_tail(uint8_t *px, int n, uint32_t start, int32_t nu, int32_t nv, const uint8_t *tex,
                       const uint8_t *lut) {
    uint32_t step = ((uint32_t)nu << 16) | ((uint32_t)nv & 0xffff), e = start;
    for (int k = 0; k < n; k++, e += step) px[-k] = lut[(unsigned)tex[((e >> 8) & 0xff) << 8 | (e >> 24)] << 8 | px[-k]];
}

/* Span of sub_00076658 (or the 0xe6658 blend of sub_00076a94): from the right end leftwards; perspective-correct
 * points every 16 pixels, an affine packed DDA in between. */
static void persp_span(uint8_t *dst, int32_t count, int32_t u0, int32_t v0, int32_t ur, int32_t vr,
                       int32_t du, int32_t dv, float a_coef, float b_coef, int32_t frac, int32_t length,
                       const uint8_t *tex, const uint8_t *lut) {
    uint8_t *px = dst; /* rightmost pixel */
#define PUT(p, t) (*(p) = lut ? lut[((unsigned)(t) << 8) | *(p)] : (t))
    uint32_t start = ((uint32_t)u0 << 16) | ((uint32_t)v0 & 0xffff); /* 0xe667c */
    int32_t tu, tv;    /* target u, v (32-bit, the low 16 bits matter) */
    int rest;
    if (count > 16) {
        double sc = s_add((double)frac, 4096.0);
        double w = s_div(sc, s_add(s_mul(sc, a_coef), b_coef));
        int chunks = (count - 1) >> 4;
        rest = ((count - 1) & 15) + 1;
        tu = s_int(s_mul(w, du)) + ur;
        tv = s_int(s_mul(w, dv)) + vr;
        int32_t tail = length - (chunks * 4096 + frac);
        for (int left = chunks - 1; left >= 0; left--) {
            sc = s_add(sc, left > 0 ? 4096.0 : (double)tail);
            /* chunk from `start` to (tu, tv): steps of (target - start) >> 4 packed u << 16 | v */
            uint32_t target = ((uint32_t)tu << 16) | ((uint32_t)tv & 0xffff);
            int32_t su = (tu - (int32_t)(start >> 16)) >> 4, sv = ((int32_t)((uint32_t)tv & 0xffff) - (int32_t)(start & 0xffff)) >> 4;
            uint32_t step = ((uint32_t)su << 16) | ((uint32_t)sv & 0xffff);
            uint32_t e = (start & 0xffff0000u) | (((start & 0xffff) - (step & 0xffff)) & 0xffff);
            for (int k = 0; k < 16; k++) {
                uint32_t cur = e;
                e += step;
                unsigned t = tex[((e >> 8) & 0xff) << 8 | (cur >> 24)];
                PUT(px - k, t);
            }
            px -= 16;
            w = s_div(sc, s_add(s_mul(sc, a_coef), b_coef));
            tu = (int32_t)s_magic(s_mul(w, du)) + ur;
            tv = (int32_t)s_magic(s_mul(w, dv)) + vr;
            start = target;
        }
        /* 0x7689e: the remainder, 1..16 pixels, with steps (target - start) / n. */
        int32_t ru = (int32_t)((uint32_t)tu & 0xffff) - (int32_t)(start >> 16);
        int32_t rv = (int32_t)((uint32_t)tv & 0xffff) - (int32_t)(start & 0xffff);
        if (lut) {
            blend_tail(px, rest, start, s_int(s_mul(ru, inv_count[rest])), s_int(s_mul(rv, inv_count[rest])), tex, lut);
            return;
        }
        uint32_t bu = s_magic(s_mul(ru, inv_count[rest])), bv = s_magic(s_mul(rv, inv_count[rest]));
        uint8_t ustep = (uint8_t)(bu >> 8);
        uint32_t step = (bu << 24) | (bv & 0xffff);
        uint32_t e = (start << 8);
        e = (e & 0xffff0000u) | (start & 0xffff);
        uint8_t uint = (uint8_t)(start >> 24), vint = (uint8_t)(start >> 8);
        for (int k = 0; k < rest; k++) {
            PUT(px - k, tex[(vint << 8) | uint]);
            uint32_t s2 = e + step;
            unsigned c = s2 < e;
            e = s2;
            uint = (uint8_t)(uint + ustep + c);
            vint = (uint8_t)(e >> 8);
        }
        return;
    }
    /* 0x76953: short span, one perspective point at the left end. */
    double w = s_div((double)length, s_add(s_mul((double)length, a_coef), b_coef));
    int32_t ru = s_int(s_mul(du, w)) + ur - u0, rv = s_int(s_mul(dv, w)) + vr - v0;
    int32_t nu = s_int(s_mul(ru, inv_count[count])), nv = s_int(s_mul(rv, inv_count[count]));
    if (lut) {
        blend_tail(px, count, start, nu, nv, tex, lut);
        return;
    }
    uint8_t ustep = (uint8_t)((uint32_t)nu >> 8);
    uint32_t step = ((uint32_t)nu << 24) | ((uint32_t)nv & 0xffff);
    uint32_t e = ((start << 8) & 0xff000000u) | (start & 0xffff);
    uint8_t uint = (uint8_t)(start >> 24), vint = (uint8_t)(start >> 8);
    for (int k = 0; k < count; k++) {
        PUT(px - k, tex[(vint << 8) | uint]);
        uint32_t s2 = e + step;
        unsigned c = s2 < e;
        e = s2;
        uint = (uint8_t)(uint + ustep + c);
        vint = (uint8_t)(e >> 8);
    }
#undef PUT
}

/* sub_0007616c: one half of the triangle, row by row. */
static void persp_rows(const IgnRaster *r, const PerspState *s, PerspHalf *h, const uint8_t *tex, const uint8_t *lut) {
    if (h->pos_s > h->limit) return;
    fpu_single = 1;
    int row = (int)((uint32_t)(h->pos_l + s->y[0]) >> 8) - 1;
    for (;; row++) {
        double xl = s_add(s_mul(h->pos_l, s->slope_l), s->x[0]), xs = s_add(s_mul(h->pos_s, h->slope), h->x0);
        int32_t x_long = s_int(xl), x_short = s_int(xs);
        double w = s_div(h->t_l, s_add(s_mul(h->t_l, s->k_l), s->c_l));
        int skip, fpu_single_after = fpu_single;
        if (!s->long_right) skip = x_long >= r->xmax8 || r->xmin8 > x_short;
        else skip = x_long < r->xmin8 || r->xmax8 <= x_short;
        if (!skip && (x_long >> 8) - (x_short >> 8) == 0) skip = 1;
        if (!skip) {
            int32_t u_long = s_int(s_add(s_mul(s->du_l, w), s->u[0]));
            int32_t v_long = s_int(s_add(s_mul(s->dv_l, w), s->v[0]));
            int32_t l_long = s_int(s_add(s_mul(s->dl_l, w), s->l[0]));
            double ws = s_div(h->t_s, s_add(s_mul(h->t_s, h->k), h->c));
            int32_t u_short = s_int(s_add(s_mul(h->du, ws), h->u0));
            int32_t v_short = s_int(s_add(s_mul(h->dv, ws), h->v0));
            int32_t l_short = s_int(s_add(s_mul(h->dl, ws), h->l0));
            /* 0x7639d: right end first */
            int32_t xr = x_long, xlft = x_short, ur = u_long, vr = v_long, lr = l_long, ul = u_short, vl = v_short,
                    ll = l_short;
            if (!s->long_right) {
                xr = x_short, xlft = x_long, ur = u_short, vr = v_short, lr = l_short;
                ul = u_long, vl = v_long, ll = l_long;
            }
            int32_t width = xr - xlft;
            double q = s_div(ll, lr);
            int32_t du = ul - ur, dv = vl - vr;
            int32_t xl2 = xlft;
            if (xl2 <= r->xmin8) xl2 = r->xmin8 + 0x10;
            int32_t length = xr - ((xl2 & ~0xff) + 0x100);
            float b_coef = (float)s_mul(q, width);
            float a_coef = (float)s_add(1.0, -q);
            int32_t frac = xr & 0xff, xr2 = xr;
            if (xr >= r->xmax8) {
                frac = xr - (r->xmax8 & ~0xff);
                xr2 = r->xmax8;
            }
            double w0 = s_div(frac, s_add(s_mul(frac, a_coef), b_coef));
            int32_t count = (int32_t)((uint32_t)xr2 >> 8) - (int32_t)((uint32_t)xl2 >> 8);
            if (count > 0) {
                int32_t xpix = (xr2 & ~0xff) - 0x100;
                int32_t u0 = s_int(s_add(s_mul(du, w0), ur)), v0 = s_int(s_add(s_mul(dv, w0), vr));
                if (!(xpix > r->xmax8) && count <= r->stride) {
                    uint8_t *dst = r->fb + (size_t)row * r->stride + (uint32_t)xpix / 256;
                    if (lut) fpu_single_after = 0;
                    persp_span(dst, count, u0, v0, ur, vr, du, dv, a_coef, b_coef, frac, length, tex, lut);
                }
            }
        }
        fpu_single = fpu_single_after;
        /* 0x76347 */
        h->pos_l += 0x100, h->pos_s += 0x100;
        h->t_l = (float)s_mul(h->pos_l, s->len_l);
        h->t_s = (float)s_mul(h->pos_s, h->len);
        if (h->pos_s > h->limit) return;
    }
}

void ign_prim_persp(IgnRaster *r, const int32_t xy[6], const int32_t light[3], const int32_t uv[6],
                    const uint8_t *tex, const uint8_t *lut) {
    if (!inv_count[1])
        for (int n = 1; n <= 16; n++) inv_count[n] = (float)(1.0 / n);
    PerspState s;
    if (persp_sort(&s, xy, light, uv)) return;
    persp_setup(&s);
    /* sub_000755ec */
    int32_t skip_top = r->ymin8 - s.y[0], skip_bot = s.y[2] - r->ymax8;
    if (skip_top < 0) skip_top = 0;
    if (skip_bot < 0) skip_bot = 0;
    int32_t h_tm = s.y[1] - s.y[0], h_mb = s.y[2] - s.y[1];
    int rows_tm = (s.y[1] >> 8) - (s.y[0] >> 8), rows_mb = (s.y[2] >> 8) - ((s.y[1] - 1) >> 8);
    PerspHalf h;
    if (skip_top < h_tm && rows_tm) {
        int32_t lim = h_tm;
        if (skip_bot > h_mb) lim = lim - skip_bot + h_mb;
        h.pos_s = h.pos_l = skip_top ? skip_top + 0x100 : 0x100 - (s.y[0] & 0xff);
        h.t_s = (float)s_mul(h.pos_s, s.len_tm);
        h.t_l = (float)s_mul(h.pos_l, s.len_l);
        h.x0 = s.x[0], h.y0 = s.y[0], h.l0 = s.l[0], h.u0 = s.u[0], h.v0 = s.v[0];
        h.slope = s.slope_tm, h.k = s.k_tm, h.c = s.c_tm, h.len = s.len_tm;
        h.du = s.du_tm, h.dv = s.dv_tm, h.dl = s.dl_tm;
        h.limit = lim - (skip_bot < h_mb ? 1 : 0);
        persp_rows(r, &s, &h, tex, lut);
        h_tm = lim;
    }
    if (skip_bot <= h_mb && rows_mb) {
        int32_t ps = 0x100 - (s.y[1] & 0xff);
        if (ps == 0x100) ps = 0;
        int32_t pl = h_tm + ps;
        if (skip_top > h_tm) {
            ps = skip_top - h_tm + 0x100;
            pl = skip_top + 0x100;
        }
        h.pos_s = ps, h.pos_l = pl;
        h.t_l = (float)s_mul(pl, s.len_l);
        h.t_s = (float)s_mul(ps, s.len_mb);
        h.limit = h_mb - skip_bot;
        h.x0 = s.x[1], h.y0 = s.y[1], h.l0 = s.l[1], h.u0 = s.u[1], h.v0 = s.v[1];
        h.slope = s.slope_mb, h.k = s.k_mb, h.c = s.c_mb, h.len = s.len_mb;
        h.du = s.du_mb, h.dv = s.dv_mb, h.dl = s.dl_mb;
        persp_rows(r, &s, &h, tex, lut);
    }
}

/* ---- flat blend through one table row, primitive 0x13 (sub_00074d04) ---- */

typedef struct {
    int32_t left, right, dleft, dright; /* 0xe6570, 0xe6574, 0xe6578, 0xe657c: 16.16 */
    int32_t row, rows;                  /* 0xe6568, 0xe656c */
} ShadeEdges;

/* sub_00074fc0 / sub_00075026: dx / dy in 16.16 with dy forced to 0x41 (or 3) for very short edges, prestepped
 * to the next row as in sub_00090a00. */
static void shade_edge(const int32_t *from, const int32_t *to, int32_t *x, int32_t *dx) {
    uint32_t dy = (uint32_t)(to[1] - from[1]);
    int32_t ddx = to[0] - from[0];
    if (dy <= 0x40) {
        uint32_t adx = (uint32_t)(ddx > 0 ? ddx : -ddx);
        if (adx <= 0x10000) dy = 0x41;
        else if (dy <= 3) dy = 3;
    }
    *dx = div16(ddx, (int32_t)dy);
    int32_t f = 0x100 - (from[1] & 0xff);
    *x = (int32_t)((uint32_t)from[0] << 8) + f * (*dx >> 8);
}

/* sub_00074ec8 */
static int32_t shade_rows(const IgnRaster *r, ShadeEdges *e, const uint8_t *row_lut) {
    int32_t a = e->left, b = e->right, y = e->row;
    if (y < r->ymin) {
        int32_t n = r->ymin - y;
        e->rows -= n;
        if (e->rows <= 0) return a;
        a += n * e->dleft;
        b += n * e->dright;
        y = r->ymin;
    }
    int32_t room = r->ymax1 - y;
    if (room < e->rows) {
        if (y > r->ymax1 || room == 0) return a;
        e->rows = room;
    }
    for (; e->rows > 0; e->rows--, y++) {
        int32_t xr = b >> 16, xl = a >> 16;
        a += e->dleft, b += e->dright;
        if (xl < r->xmin) {
            if (xr < r->xmin) continue;
            xl = r->xmin;
        }
        if (xr > r->xmax1) {
            if (xl > r->xmax1) continue;
            xr = r->xmax1;
        }
        uint8_t *p = r->fb + (size_t)y * r->stride;
        for (int32_t x = xl; x < xr; x++) p[x] = row_lut[p[x]];
    }
    return a;
}

void ign_prim_shade(IgnRaster *r, const int32_t xy[6], const uint8_t *row_lut) {
    /* 0x74d11: order by y with the original's exchanges */
    const int32_t *v0 = xy, *v1 = xy + 2, *v2 = xy + 4, *t;
    if (v1[1] >= v0[1]) {
        if (!(v1[1] < v2[1])) {
            t = v2, v2 = v1, v1 = t;
            if (!(v0[1] < v1[1])) t = v1, v1 = v0, v0 = t;
        }
    } else {
        t = v1, v1 = v0, v0 = t;
        if (!(v2[1] > v1[1])) {
            t = v2, v2 = v1, v1 = t;
            if (!(v1[1] > v0[1])) t = v1, v1 = v0, v0 = t;
        }
    }
    const int32_t lim = 0x7d0000;
    if (v0[0] < -lim || v0[0] > lim || v1[0] < -lim || v1[0] > lim || v2[0] < -lim || v2[0] > lim || v2[1] > lim)
        return;
    ShadeEdges e;
    e.row = v0[1] >> 8;
    e.rows = (v1[1] >> 8) - e.row;
    if (e.rows) {
        shade_edge(v0, v1, &e.left, &e.dleft);
        shade_edge(v0, v2, &e.right, &e.dright);
        if (e.dleft > e.dright) {
            int32_t s = e.dleft;
            e.dleft = e.dright, e.dright = s;
            s = e.left, e.left = e.right, e.right = s;
        }
        shade_rows(r, &e, row_lut);
    }
    e.row = v1[1] >> 8;
    e.rows = (v2[1] >> 8) - e.row;
    if (!e.rows) return;
    shade_edge(v0, v2, &e.left, &e.dleft);
    shade_edge(v1, v2, &e.right, &e.dright);
    e.left += ((v1[1] >> 8) - (v0[1] >> 8)) * e.dleft;
    if (e.dleft < e.dright) {
        int32_t s = e.dleft;
        e.dleft = e.dright, e.dright = s;
        s = e.left, e.left = e.right, e.right = s;
    }
    shade_rows(r, &e, row_lut);
}
