#ifndef IGN_SPRITE_H
#define IGN_SPRITE_H

#include <stdint.h>

#include "../game/assets.h"
#include "raster.h"

/* geputget.c sprite (sub_000771e0): a w x h window into 8-bit pixels with a row stride. The HUD creates every
 * sprite with a zero hotspot (+0x18/+0x1c), so its position is the top-left corner. */
typedef struct {
    const uint8_t *pixels;
    int w, h, stride;
} IgnSprite;

/* Clip rectangle of sub_00055958 (x0, y0 inclusive, x1, y1 exclusive). */
typedef struct {
    int x0, y0, x1, y1;
} IgnClip;

/* Window (x, y, w, h) of a PIC, with the PIC width as stride. Returns 0, or -1 (and an empty sprite) when the
 * rectangle does not fit the picture. */
int ign_sprite_from_pic(IgnSprite *s, const IgnPic *pic, int x, int y, int w, int h);

/* sub_00062184 without a matrix: pixel (x, y) is the top-left corner. Every texel goes through the blend table of
 * [0x257e48] (sub_000791c9: dst = lut[src * 256 + dst]); with a NULL table index 0 is transparent and the rest
 * opaque. */
void ign_sprite_draw(IgnFrame *fb, const IgnClip *clip, const IgnSprite *s, int x, int y, const uint8_t *lut);
/* sub_00062184 with {scale, angle = 0}: nearest-neighbour scale about the top-left corner (¿?). */
void ign_sprite_draw_scaled(IgnFrame *fb, const IgnClip *clip, const IgnSprite *s, int x, int y, float scale,
                            const uint8_t *lut);
/* Raster primitive 0x0f (0x729f3): flat triangle in the given colour, clipped to `clip`. */
void ign_fill_triangle(IgnFrame *fb, const IgnClip *clip, const int xy[6], uint8_t color);

#endif
