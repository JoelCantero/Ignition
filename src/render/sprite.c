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

void ign_sprite_draw_scaled(IgnFrame *fb, const IgnClip *clip, const IgnSprite *s, int x, int y, float scale,
                            const uint8_t *lut) {
    if (!s->pixels || !(scale > 0.0f)) return;
    /* sub_00062184: the scale becomes a 16.16 matrix (scale * 65536.0f, truncated). */
    int32_t m = (int32_t)(scale * 65536.0f);
    if (m <= 0) return;
    int dw = (int)(((int64_t)s->w * m) >> 16), dh = (int)(((int64_t)s->h * m) >> 16);
    IgnClip c;
    clip_to_frame(fb, clip, &c);
    for (int dy = 0; dy < dh; dy++) {
        int py = y + dy;
        if (py < c.y0 || py >= c.y1) continue;
        int sy = (int)(((int64_t)dy << 16) / m);
        if (sy >= s->h) continue;
        for (int dx = 0; dx < dw; dx++) {
            int px = x + dx;
            if (px < c.x0 || px >= c.x1) continue;
            int sx = (int)(((int64_t)dx << 16) / m);
            if (sx >= s->w) continue;
            put(fb, px, py, s->pixels[(size_t)sy * s->stride + sx], lut);
        }
    }
}

void ign_fill_triangle(IgnFrame *fb, const IgnClip *clip, const int xy[6], uint8_t color) {
    IgnClip c;
    clip_to_frame(fb, clip, &c);
    int ymin = xy[1], ymax = xy[1];
    for (int i = 1; i < 3; i++) {
        if (xy[i * 2 + 1] < ymin) ymin = xy[i * 2 + 1];
        if (xy[i * 2 + 1] > ymax) ymax = xy[i * 2 + 1];
    }
    if (ymin < c.y0) ymin = c.y0;
    if (ymax > c.y1 - 1) ymax = c.y1 - 1;
    /* Span rule: pixel centres inside the triangle, upper edges included (the exact rule of 0x729f3 is not
     * reproduced; a degenerate triangle still draws its top edge row). */
    for (int y = ymin; y <= ymax; y++) {
        double cy = y + 0.5, lo = INFINITY, hi = -INFINITY;
        for (int e = 0; e < 3; e++) {
            double x0 = xy[e * 2], y0 = xy[e * 2 + 1], x1 = xy[(e + 1) % 3 * 2], y1 = xy[(e + 1) % 3 * 2 + 1];
            if (y0 == y1) {
                if (fabs(cy - y0) <= 0.5) {
                    if (fmin(x0, x1) < lo) lo = fmin(x0, x1);
                    if (fmax(x0, x1) > hi) hi = fmax(x0, x1);
                }
                continue;
            }
            if ((cy < y0 && cy < y1) || (cy > y0 && cy > y1)) continue;
            double x = x0 + (cy - y0) * (x1 - x0) / (y1 - y0);
            if (x < lo) lo = x;
            if (x > hi) hi = x;
        }
        if (lo > hi) continue;
        int xa = (int)ceil(lo - 0.5), xb = (int)ceil(hi - 0.5) - 1;
        if (xb < xa) xb = xa;
        if (xa < c.x0) xa = c.x0;
        if (xb > c.x1 - 1) xb = c.x1 - 1;
        for (int x = xa; x <= xb; x++) fb->color[y * fb->w + x] = color;
    }
}
