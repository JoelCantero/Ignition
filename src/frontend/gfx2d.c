#include "gfx2d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../game/assets.h"

static struct {
    uint8_t *buf;
    int pitch, w, h; /* clip rectangle (0, 0, w, h) */
    const uint8_t *lut;
} g_target;

void ign_gfx_set_target(uint8_t *buf, int pitch, int w, int h) {
    g_target.buf = buf;
    g_target.pitch = pitch;
    g_target.w = w;
    g_target.h = h;
}

void ign_gfx_set_lut(const uint8_t *lut) { g_target.lut = lut; }
const uint8_t *ign_gfx_lut(void) { return g_target.lut; }

const uint8_t *ign_gfx_identity_lut(void) {
    static uint8_t lut[65536];
    static int built;
    if (!built) {
        for (int i = 0; i < 65536; i++) lut[i] = (uint8_t)(i >> 8);
        built = 1;
    }
    return lut;
}

int ign_sprite_set(IgnSprite *s, const uint8_t *src, int w, int h, int pitch, int hotx, int hoty) {
    uint8_t *px = malloc(w > 0 && h > 0 ? (size_t)w * h : 1);
    if (!px) return -1;
    for (int y = 0; y < h; y++) memcpy(px + (size_t)y * w, src + (size_t)y * pitch, (size_t)w);
    free(s->px);
    s->px = px;
    s->w = w;
    s->h = h;
    s->hotx = hotx;
    s->hoty = hoty;
    return 0;
}

void ign_sprite_free(IgnSprite *s) {
    free(s->px);
    memset(s, 0, sizeof *s);
}

void ign_sprite_put(const IgnSprite *s, int x88, int y88, const float *xform) {
    const uint8_t *lut = g_target.lut;
    if (!s->px || !g_target.buf || !lut) return;
    if (!xform) {
        /* 0x791c9: top-left = (pos - hot) >> 8, clipped to the target, dst = lut[src << 8 | dst]. */
        int x0 = (x88 - s->hotx) >> 8, y0 = (y88 - s->hoty) >> 8;
        for (int y = 0; y < s->h; y++) {
            int ty = y0 + y;
            if (ty < 0 || ty >= g_target.h) continue;
            uint8_t *d = g_target.buf + (size_t)ty * g_target.pitch;
            const uint8_t *p = s->px + (size_t)y * s->w;
            for (int x = 0; x < s->w; x++) {
                int tx = x0 + x;
                if (tx >= 0 && tx < g_target.w) d[tx] = lut[p[x] << 8 | d[tx]];
            }
        }
        return;
    }
    /* 0x82950 rotozoom with the 16.16 matrix (scale * 65536 * cos, sin) about the hot spot. The frontend only uses
     * angle 0; texels are taken at the nearest sample. */
    double scale = xform[0], angle = xform[1];
    if (scale <= 0.0) return;
    double c = cos(angle) / scale, sn = sin(angle) / scale;
    double hx = s->hotx / 256.0, hy = s->hoty / 256.0, px = x88 / 256.0, py = y88 / 256.0;
    double r = (s->w + s->h) * scale + 2;
    int bx0 = (int)floor(px - r), bx1 = (int)ceil(px + r), by0 = (int)floor(py - r), by1 = (int)ceil(py + r);
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 > g_target.w) bx1 = g_target.w;
    if (by1 > g_target.h) by1 = g_target.h;
    for (int y = by0; y < by1; y++) {
        uint8_t *d = g_target.buf + (size_t)y * g_target.pitch;
        for (int x = bx0; x < bx1; x++) {
            double dx = x + 0.5 - px, dy = y + 0.5 - py;
            int u = (int)floor(hx + dx * c + dy * sn), v = (int)floor(hy - dx * sn + dy * c);
            if (u < 0 || v < 0 || u >= s->w || v >= s->h) continue;
            d[x] = lut[s->px[(size_t)v * s->w + u] << 8 | d[x]];
        }
    }
}

/* ---- LFT fonts (0x61220) ------------------------------------------------------------------------------------- */

#define LFT_GLYPHS 224
#define LFT_BITMAPS 0x84c

static unsigned rd16(const uint8_t *p) { return (unsigned)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int ign_font_load(IgnFont *f, const char *path) {
    memset(f, 0, sizeof *f);
    size_t size;
    uint8_t *d = ign_read_file(path, &size);
    if (!d) return -1;
    if (size < LFT_BITMAPS || memcmp(d, "LFT\0", 4) || rd16(d + 4) != 100) {
        free(d);
        return -1;
    }
    f->cellw = (int16_t)rd16(d + 8);
    f->h = (int16_t)rd16(d + 10);
    for (int i = 0; i < LFT_GLYPHS; i++) {
        uint32_t off = rd32(d + 12 + i * 4);
        int w = (int16_t)rd16(d + 0x68c + i * 2);
        f->width[i + 0x20] = (int16_t)w;
        if (off == UINT32_MAX || w <= 0 || f->h <= 0) continue;
        if ((size_t)off > size - LFT_BITMAPS || (size_t)w * f->h > size - LFT_BITMAPS - off) continue;
        if (ign_sprite_set(&f->glyph[i + 0x20], d + LFT_BITMAPS + off, w, f->h, w, 0, 0)) continue;
        f->present[i + 0x20] = 1;
    }
    free(d);
    /* fontInit 0x61220 leaves spacing 1; load_system_gfx 0x21860 then sets proportional = 1, spacing = 1. */
    f->loaded = 1;
    f->proportional = 1;
    f->spacing = 1;
    return 0;
}

void ign_font_free(IgnFont *f) {
    for (int i = 0; i < 256; i++) ign_sprite_free(&f->glyph[i]);
    memset(f, 0, sizeof *f);
}

/* Space advance of proportional fonts: trunc(cellw * 256 * 0.35 * (1 / 256)) (__CHP). */
static int space_advance(const IgnFont *f) { return (int)((double)(f->cellw << 8) * 0.35 * 0.00390625); }

int ign_font_width(const IgnFont *f, const char *s) {
    if (!f || !f->loaded || f->disabled) return 0;
    int x = 0;
    for (const unsigned char *c = (const unsigned char *)s; *c; c++) {
        if (f->present[*c]) x += (f->proportional ? f->width[*c] : f->cellw) + f->spacing;
        else if (*c == ' ') x += f->proportional ? space_advance(f) : f->cellw + f->spacing;
    }
    return x;
}

void ign_font_draw(const IgnFont *f, const char *s, int x, int y) {
    if (!f || !f->loaded || f->disabled) return;
    if (f->align) {
        int w = ign_font_width(f, s);
        x -= f->align == 1 ? w / 2 : w;
    }
    for (const unsigned char *c = (const unsigned char *)s; *c; c++) {
        if (f->present[*c]) {
            if (f->proportional) {
                ign_sprite_put(&f->glyph[*c], x << 8, y << 8, NULL);
                x += f->width[*c] + f->spacing;
            } else {
                int off = f->width[*c] != f->cellw ? (f->cellw - f->width[*c]) / 2 : 0;
                ign_sprite_put(&f->glyph[*c], (x + off) << 8, y << 8, NULL);
                x += f->cellw + f->spacing;
            }
        } else if (*c == ' ')
            x += f->proportional ? space_advance(f) : f->cellw + f->spacing;
    }
}

void ign_blend_rect(const uint8_t *src, int x0, int y0, int x1, int y1, uint8_t *dst, int dx, int dy,
                    const uint8_t *lut, int sp, int dp, int buf_w, int buf_h) {
    for (int y = y0; y < y1; y++) {
        int ty = dy + y - y0;
        if (y < 0 || y >= buf_h || ty < 0 || ty >= buf_h) continue;
        for (int x = x0; x < x1; x++) {
            int tx = dx + x - x0;
            if (x < 0 || x >= buf_w || tx < 0 || tx >= buf_w) continue;
            uint8_t *d = dst + (size_t)ty * dp + tx;
            *d = lut[src[(size_t)y * sp + x] << 8 | *d];
        }
    }
}

void ign_palette_lerp(const uint8_t *a, const uint8_t *b, uint8_t *out, int t) {
    if (t >= 256) {
        memcpy(out, b, 768);
        return;
    }
    for (int i = 0; i < 768; i++) out[i] = (uint8_t)(a[i] + ((b[i] - a[i]) * t) / 256);
}

/* ---- MenuBkg.dat boxes (0x541d4..0x54864) ---------------------------------------------------------------------- */

#define MENUBKG_SIZE 0x9358

typedef struct Box {
    int x0, x1, row, yoff, style;
    struct Box *next;
} Box;

static uint8_t *g_bkg;       /* [0x20876c] */
static uint8_t g_remap[256]; /* 0x208660 */
static Box *g_boxes;         /* 0xc57fc */
static Box **g_box_tail = &g_boxes;
static struct {
    uint8_t *buf;
    int pitch, w, h;
    int style, upper_style, lower_style; /* 0x208760, 0x208774, 0x208770 */
} g_bx;

int ign_menubox_init(const char *path) {
    /* Highlight remap 0x541c0: identity except c1->f5 c3->bd c4->be d5->b4 d6->bb fd->bc c0->bd c2->f4. */
    static const uint8_t pairs[] = {0xc1, 0xf5, 0xc3, 0xbd, 0xc4, 0xbe, 0xd5, 0xb4,
                                    0xd6, 0xbb, 0xfd, 0xbc, 0xc0, 0xbd, 0xc2, 0xf4};
    g_boxes = NULL;
    g_box_tail = &g_boxes;
    for (int i = 0; i < 256; i++) g_remap[i] = (uint8_t)i;
    for (size_t i = 0; i < sizeof pairs; i += 2) g_remap[pairs[i]] = pairs[i + 1];
    size_t size;
    free(g_bkg);
    g_bkg = ign_read_file(path, &size);
    if (g_bkg && size < MENUBKG_SIZE) {
        free(g_bkg);
        g_bkg = NULL;
    }
    return g_bkg ? 0 : -1;
}

void ign_menubox_free(void) {
    ign_menubox_clear();
    free(g_bkg);
    g_bkg = NULL;
}

void ign_menubox_clear(void) {
    while (g_boxes) {
        Box *n = g_boxes->next;
        free(g_boxes);
        g_boxes = n;
    }
    g_box_tail = &g_boxes;
}

void ign_menubox_add(int x, int row, int w, int yoff, int style) {
    Box *b = calloc(1, sizeof *b);
    if (!b) return;
    b->x0 = x;
    b->x1 = x + w;
    b->row = row;
    b->yoff = yoff;
    b->style = style;
    *g_box_tail = b;
    g_box_tail = &b->next;
}

static void put(int x, int y, uint8_t c) {
    if (x >= 0 && x < g_bx.w && y >= 0 && y < g_bx.h) g_bx.buf[(size_t)y * g_bx.pitch + x] = c;
}

/* sub_00054838: one fill scanline, remapped in the highlight style. */
static void hline(int x, int y, int len, uint8_t c) {
    if (g_bx.style == 1) c = g_remap[c];
    for (int i = 0; i < len; i++) put(x + i, y, c);
}

/* sub_00054864: a 19-row tile stored bottom-up; rows 0-9 take the upper box's style, 10-18 the own style; 0 is
 * transparent. */
static void tile(int off, int w, int x, int y) {
    const uint8_t *src = g_bkg + off + 18 * w;
    for (int r = 0; r < 19; r++, src -= w) {
        int remap = (r < 10 ? g_bx.upper_style : g_bx.lower_style) == 1;
        for (int i = 0; i < w; i++)
            if (src[i]) put(x + i, y + r, remap ? g_remap[src[i]] : src[i]);
    }
}

/* sub_00054330 */
static void draw_box(const Box *b) {
    int left_up = 0, right_up = 0, left_down = 0, right_down = 0;
    g_bx.style = b->style;
    for (int i = 0; i < 24; i++) hline(b->x0 + 13, b->row * 19 + i + b->yoff + 8, b->x1 - b->x0, g_bkg[0x9340 + i]);
    int top = b->row * 19 + b->yoff;
    for (const Box *o = g_boxes; o; o = o->next) {
        if (o == b) continue;
        int dl = o->x0 - b->x0, dr = o->x1 - b->x1;
        if (b->row - 1 == o->row) {
            g_bx.upper_style = o->style;
            g_bx.lower_style = g_bx.style;
            if (dl >= -13 && dl <= 13 && !left_up) {
                if (dl < 0) tile(0x2600 + (-dl - 1) * 0x260, 32, b->x0 - 19, top);
                else tile(0x4c0 + dl * 0x260, 32, b->x0 + dl - 19, top);
                left_up = 1;
            }
            if (dl < -13 && b->x0 - o->x1 < -2) {
                tile(0x8e80, 16, b->x0, top);
                left_up = 1;
            }
            if (dl > 13 && o->x0 - b->x1 < -2) tile(0x90e0, 16, b->x0 + dl, top);
            if (dr >= -15 && dr <= 15 && !right_up) {
                if (dr < 0) tile(0x6ae0 + (-dr - 1) * 0x260, 32, b->x1 + dr + 13, top);
                else tile(0x44e0 + dr * 0x260, 32, b->x1 + 13, top);
                right_up = 1;
            }
            if (dr > 15 && b->x1 - o->x0 > -1) {
                tile(0x8fb0, 16, b->x1 + 13, top);
                right_up = 1;
            }
            if (dr < -15 && o->x1 - b->x0 > -1) tile(0x9210, 16, b->x1 + 13 + dr, top);
        }
        if (b->row + 1 == o->row) {
            if (dl >= -13 && dl <= 13 && !left_down) left_down = 1;
            if (dr >= -15 && dr <= 15 && !right_up) right_down = 1;
        }
    }
    g_bx.upper_style = g_bx.lower_style = g_bx.style;
    if (!left_up) tile(0x000, 16, b->x0 - 3, top);
    if (!left_down) tile(0x260, 16, b->x0 - 3, top + 19);
    if (!right_up) tile(0x130, 16, b->x1 + 13, top);
    if (!right_down) tile(0x390, 16, b->x1 + 13, top + 19);
}

void ign_menubox_draw(uint8_t *buf, int pitch, int buf_w, int buf_h) {
    if (!g_bkg) return;
    g_bx.buf = buf;
    g_bx.pitch = pitch;
    g_bx.w = buf_w;
    g_bx.h = buf_h;
    for (const Box *b = g_boxes; b; b = b->next) draw_box(b);
}
