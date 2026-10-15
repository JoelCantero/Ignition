#ifndef IGN_PFM_H
#define IGN_PFM_H

/* "Script Player" vector animation of the menu background: DEFAULT2.PSQ + TEST2.PFM (pfm_load 0x549b0,
 * pfm_build_frame 0x54df8, rasterised by 0x60e3d). */

#include <stddef.h>
#include <stdint.h>

typedef struct IgnPfm IgnPfm;

/* `pages`: 64 KB texture pages (NULL-terminated, at most 31); `shades`: 256x256 tables (NULL-terminated). */
IgnPfm *ign_pfm_load(const uint8_t *data, size_t size, uint8_t *const *pages, const uint8_t *const *shades);
void ign_pfm_free(IgnPfm *p);
int ign_pfm_frames(const IgnPfm *p);
/* Builds frame `frame` and rasterises it into dst (pitch = w), clipped to (0, 0, w - 1, h - 1); `lut` is the raster
 * state colour table (MENU.TAB). Returns 0, or -1 for an unknown primitive. */
int ign_pfm_draw(IgnPfm *p, int frame, uint8_t *dst, int w, int h, const uint8_t *lut);

#endif
