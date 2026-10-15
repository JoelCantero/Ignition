#ifndef IGN_GFX2D_H
#define IGN_GFX2D_H

/* 2D drawing of the frontend over the engine's sprite and font code (render/sprite.c, render/font.c): the current
 * draw target (0x61f8c) and colour table [0x257e48], sprites with a hot spot (geputget.c 0x771e0 / 0x62184), the LUT
 * rectangle blend 0x51818 and the MenuBkg.dat highlight boxes (0x541d4..0x54864). */

#include <stddef.h>
#include <stdint.h>

#include "../render/font.h"
#include "../render/sprite.h"

/* sub_00061f8c: the target frame (its width is the pitch) with the clip rectangle (0, 0, w, h). */
void ign_gfx_set_target(IgnFrame *fb);
IgnFrame *ign_gfx_target(void);
/* [0x257e48]: sprite and glyph pixels are written as dst = lut[src << 8 | dst]. */
void ign_gfx_set_lut(const uint8_t *lut);
const uint8_t *ign_gfx_lut(void);
/* [0x1fec0c], built by game_init 0x215d0: lut[s << 8 | d] = s (opaque copy, index 0 included). */
const uint8_t *ign_gfx_identity_lut(void);

/* A sprite of sub_00055970: a copy of the pixel rectangle and its hot spot (8.8, +0x18/+0x1c of the descriptor). */
typedef struct {
    IgnSprite s;
    int hotx, hoty;
    uint8_t *px;
} IgnMenuSprite;

int ign_menu_sprite_set(IgnMenuSprite *s, const uint8_t *src, int w, int h, int pitch, int hotx, int hoty);
void ign_menu_sprite_free(IgnMenuSprite *s);
/* sub_00055968 (0x62184): hot spot at (x88, y88), 24.8 fixed point. `xform` = {scale, angle} or NULL; the frontend
 * only scales. Unscaled: 0x791c9 at (pos - hot) >> 8. Scaled: 0x82950, whose first corner is pos + M * (-hot). */
void ign_menu_sprite_put(const IgnMenuSprite *s, int x88, int y88, const float *xform);

/* Text in the current target and colour table (sub_00061959 / sub_000616db). */
void ign_gfx_text(const IgnFont *f, const char *s, int x, int y);
int ign_gfx_text_width(const IgnFont *f, const char *s);

/* sub_00051818: dst[(dy + y - y0) * dp + dx + x - x0] = lut[src[y * sp + x] << 8 | dst[...]], x in [x0, x1), y in
 * [y0, y1). Rows and columns outside either buffer are skipped (the original wrote past them). */
void ign_blend_rect(const uint8_t *src, int x0, int y0, int x1, int y1, uint8_t *dst, int dx, int dy,
                    const uint8_t *lut, int sp, int dp, int buf_w, int buf_h);

/* MenuBkg.dat boxes. */
int ign_menubox_init(const char *path);             /* sub_000541d4 */
void ign_menubox_free(void);
void ign_menubox_clear(void);                       /* sub_00054278 */
void ign_menubox_add(int x, int row, int w, int yoff, int style); /* sub_000542c4 */
void ign_menubox_draw(uint8_t *buf, int pitch, int buf_w, int buf_h); /* sub_0005430c */

#endif
