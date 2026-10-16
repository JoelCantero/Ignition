#include "gfx2d.h"

#include <stdlib.h>
#include <string.h>

#include "../game/assets.h"

static struct {
    IgnFrame *fb;
    IgnClip clip;
    const uint8_t *lut;
} g_target;

void ign_gfx_set_target(IgnFrame *fb) {
    g_target.fb = fb;
    g_target.clip = (IgnClip){0, 0, fb->w, fb->h};
}

IgnFrame *ign_gfx_target(void) { return g_target.fb; }
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

int ign_menu_sprite_set(IgnMenuSprite *s, const uint8_t *src, int w, int h, int pitch, int hotx, int hoty) {
    uint8_t *px = malloc(w > 0 && h > 0 ? (size_t)w * h : 1);
    if (!px) return -1;
    for (int y = 0; y < h; y++) memcpy(px + (size_t)y * w, src + (size_t)y * pitch, (size_t)w);
    free(s->px);
    s->px = px;
    s->s = (IgnSprite){px, w, h, w};
    s->hotx = hotx;
    s->hoty = hoty;
    return 0;
}

void ign_menu_sprite_free(IgnMenuSprite *s) {
    free(s->px);
    memset(s, 0, sizeof *s);
}

static int32_t mul16(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }

void ign_menu_sprite_put(const IgnMenuSprite *s, int x88, int y88, const float *xform) {
    if (!s->px || !g_target.fb || !g_target.lut) return;
    if (!xform) {
        ign_sprite_draw(g_target.fb, &g_target.clip, &s->s, (x88 - s->hotx) >> 8, (y88 - s->hoty) >> 8, g_target.lut);
        return;
    }
    if (s->s.w > 256 || s->s.h > 256) return;
    /* geputget copies the sprite into a 256 x 256 page; 0x62184 builds {S cos a, S sin a, S sin a, S cos a}. */
    static uint8_t page[65536];
    memset(page, 0, sizeof page);
    for (int r = 0; r < s->s.h; r++) memcpy(page + r * 256, s->px + (size_t)r * s->s.w, (size_t)s->s.w);
    int32_t S = (int32_t)(xform[0] * 65536.0f);
    const int32_t m[4] = {S, 0, 0, S};
    int32_t x = x88 + mul16(-s->hotx, m[0]) + mul16(-s->hoty, m[2]);
    int32_t y = y88 + mul16(-s->hotx, m[1]) + mul16(-s->hoty, m[3]);
    ign_sprite_draw_matrix(g_target.fb, &g_target.clip, page, 0, 0, s->s.w, s->s.h, m, x, y, g_target.lut);
}

void ign_gfx_text(const IgnFont *f, const char *s, int x, int y) {
    if (g_target.fb) ign_font_draw(g_target.fb, &g_target.clip, f, s, x, y, g_target.lut);
}

int ign_gfx_text_width(const IgnFont *f, const char *s) { return ign_font_text_width(f, s); }

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

/* ---- MenuBkg.dat boxes (0x541d4..0x54864) ---------------------------------------------------------------------- */

#define MENUBKG_SIZE 0x9358

typedef IgnMenuBox Box;

static uint8_t *g_bkg;       /* [0x20876c] */
static Box *g_boxes;         /* 0xc57fc */
static Box **g_box_tail = &g_boxes;
static struct {
    uint8_t *buf;
    const uint8_t *bkg;
    int pitch, w, h;
    int linear;
    int style, upper_style, lower_style; /* 0x208760, 0x208774, 0x208770 */
} g_bx;

int ign_menubox_init(const char *path) {
    ign_menubox_clear();
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
    if (g_bx.linear) {
        /* DOS tiles address a linear buffer: a right-edge overhang lands on the next row. */
        ptrdiff_t at = (ptrdiff_t)y * g_bx.pitch + x;
        if (at >= 0 && at < (ptrdiff_t)g_bx.pitch * g_bx.h) g_bx.buf[at] = c;
    } else if (x >= 0 && x < g_bx.w && y >= 0 && y < g_bx.h)
        g_bx.buf[(size_t)y * g_bx.pitch + x] = c;
}

/* 0x541c0: the eight non-identity entries of the highlight remap. */
static uint8_t highlight(uint8_t c) {
    switch (c) {
    case 0xc1: return 0xf5;
    case 0xc3: case 0xc0: return 0xbd;
    case 0xc4: return 0xbe;
    case 0xd5: return 0xb4;
    case 0xd6: return 0xbb;
    case 0xfd: return 0xbc;
    case 0xc2: return 0xf4;
    default: return c;
    }
}

/* sub_00054838: one fill scanline, remapped in the highlight style. */
static void hline(int x, int y, int len, uint8_t c) {
    if (g_bx.style == 1) c = highlight(c);
    for (int i = 0; i < len; i++) put(x + i, y, c);
}

/* sub_00054864: a 19-row tile stored bottom-up; rows 0-9 take the upper box's style, 10-18 the own style; 0 is
 * transparent. */
static void tile(int off, int w, int x, int y) {
    const uint8_t *src = g_bx.bkg + off + 18 * w;
    for (int r = 0; r < 19; r++, src -= w) {
        int remap = (r < 10 ? g_bx.upper_style : g_bx.lower_style) == 1;
        for (int i = 0; i < w; i++)
            if (src[i]) put(x + i, y + r, remap ? highlight(src[i]) : src[i]);
    }
}

/* sub_00054330 */
static void draw_box(const Box *b, const Box *boxes) {
    int left_up = 0, right_up = 0, left_down = 0, right_down = 0;
    g_bx.style = b->style;
    for (int i = 0; i < 24; i++) hline(b->x0 + 13, b->row * 19 + i + b->yoff + 8, b->x1 - b->x0, g_bx.bkg[0x9340 + i]);
    int top = b->row * 19 + b->yoff;
    for (const Box *o = boxes; o; o = o->next) {
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

static void draw_list(uint8_t *buf, int pitch, int buf_w, int buf_h, const uint8_t *tiles,
                      const IgnMenuBox *boxes, int linear) {
    g_bx.buf = buf;
    g_bx.bkg = tiles;
    g_bx.pitch = pitch;
    g_bx.w = buf_w;
    g_bx.h = buf_h;
    g_bx.linear = linear;
    for (const Box *b = boxes; b; b = b->next) draw_box(b, boxes);
}

void ign_menubox_draw(uint8_t *buf, int pitch, int buf_w, int buf_h) {
    if (g_bkg) draw_list(buf, pitch, buf_w, buf_h, g_bkg, g_boxes, 0);
}

void ign_menubox_draw_list(uint8_t *buf, int pitch, int buf_w, int buf_h, const uint8_t *tiles,
                           const IgnMenuBox *boxes) {
    draw_list(buf, pitch, buf_w, buf_h, tiles, boxes, 1);
}
