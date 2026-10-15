#ifndef IGN_LISA_H
#define IGN_LISA_H

/* Port of the "Lisa 2 Development System" renderer of MAINDOS.EXE: the scene engine (0x54000-0x60000) builds a
 * painter's list of primitives in depth buckets and the rasterizer (sub_00060e3d) draws it back to front. */

#include <stdint.h>

#define IGN_RASTER_ROWS 1024

/* Clip state of sub_00060e3d (0xc87cc..0xc87f8) and the target. */
typedef struct {
    uint8_t *fb;
    int stride;
    int xmin, ymin, ymax1;              /* 0xc87cc, 0xc87d0, 0xc87d8 */
    int32_t xmin8, ymin8, xmax8, ymax8; /* 0xc87ec..0xc87f8: 8.8, far edges exclusive */
} IgnRaster;

void ign_raster_clip(IgnRaster *r, int x0, int y0, int x1, int y1);

/* Primitive 0x11 (sub_00073c58): affine textured triangle; xy and uv in 8.8, tex = one 256x256 page. */
void ign_prim_tex(IgnRaster *r, const int32_t xy[6], const int32_t uv[6], const uint8_t *tex);

#endif
