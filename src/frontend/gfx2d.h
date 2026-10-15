#ifndef IGN_GFX2D_H
#define IGN_GFX2D_H

/* 2D layer of the Lisa 2 engine as used by the frontend: draw target (0x61f8c), colour table [0x257e48], sprites
 * (geputget.c 0x771e0 / 0x62184 / 0x791c9), LFT fonts (0x61220, 0x616db, 0x61959), the LUT rectangle blend 0x51818,
 * the palette lerp 0x51a9c and the MenuBkg.dat highlight boxes (0x541d4..0x54864). */

#include <stddef.h>
#include <stdint.h>

/* sub_00061f8c: 8-bit destination with the clip rectangle (0, 0, w, h). */
void ign_gfx_set_target(uint8_t *buf, int pitch, int w, int h);
/* [0x257e48]: every sprite and glyph pixel is written as dst = lut[src << 8 | dst]. */
void ign_gfx_set_lut(const uint8_t *lut);
const uint8_t *ign_gfx_lut(void);
/* [0x1fec0c], built by game_init 0x215d0: lut[s << 8 | d] = s (opaque copy, index 0 included). */
const uint8_t *ign_gfx_identity_lut(void);

typedef struct {
    int w, h;
    int hotx, hoty; /* 8.8 fixed point */
    uint8_t *px;    /* w * h, owned */
} IgnSprite;

/* sub_00055970 (0x771e0): (re)creates `s` from a pixel rectangle; hot spot in 8.8. */
int ign_sprite_set(IgnSprite *s, const uint8_t *src, int w, int h, int pitch, int hotx, int hoty);
void ign_sprite_free(IgnSprite *s);
/* sub_00055968 (0x62184): draws with its hot spot at (x88, y88) (24.8 fixed point). `xform` = {scale, angle} or NULL;
 * the frontend only scales (angle 0). */
void ign_sprite_put(const IgnSprite *s, int x88, int y88, const float *xform);

/* LFT font slot (0x24c214 + i * 0x63e). */
typedef struct {
    int loaded;
    int align;        /* +0x04: 0 left, 1 centre, 2 right */
    int disabled;     /* +0x08 */
    int proportional; /* +0x0c */
    int spacing;      /* +0x10 */
    int cellw, h;     /* +0x1a, +0x1c */
    uint8_t present[256];
    int16_t width[256];
    IgnSprite glyph[256];
} IgnFont;

int ign_font_load(IgnFont *f, const char *path);
void ign_font_free(IgnFont *f);
/* sub_000616db */
int ign_font_width(const IgnFont *f, const char *s);
/* sub_00061959: y is the top of the glyphs. */
void ign_font_draw(const IgnFont *f, const char *s, int x, int y);

/* sub_00051818: dst[(dy + y - y0) * dp + dx + x - x0] = lut[src[y * sp + x] << 8 | dst[...]], x in [x0, x1), y in
 * [y0, y1). Rows and columns outside either buffer are skipped (the original wrote past them). */
void ign_blend_rect(const uint8_t *src, int x0, int y0, int x1, int y1, uint8_t *dst, int dx, int dy,
                    const uint8_t *lut, int sp, int dp, int buf_w, int buf_h);
/* sub_00051a9c: out = a + trunc((b - a) * t / 256) for t < 256, b otherwise; 768 bytes. */
void ign_palette_lerp(const uint8_t *a, const uint8_t *b, uint8_t *out, int t);

/* MenuBkg.dat boxes. */
int ign_menubox_init(const char *path);             /* sub_000541d4 */
void ign_menubox_free(void);
void ign_menubox_clear(void);                       /* sub_00054278 */
void ign_menubox_add(int x, int row, int w, int yoff, int style); /* sub_000542c4 */
void ign_menubox_draw(uint8_t *buf, int pitch, int buf_w, int buf_h); /* sub_0005430c */

#endif
