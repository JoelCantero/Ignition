#ifndef IGN_PALETTE_H
#define IGN_PALETTE_H

#include <stdint.h>

#include "assets.h"

/* palette_blend (sub_00051508): each colour of `in` moved `frac` of the way toward (r, g, b), written to `out`
 * (the original copies into its static buffer 0x207afe and returns it). Per channel the step is
 * chop(frac * (target - c)) on the x87 and the sum keeps its low byte. A non-NULL `keep` holds one int32 per
 * colour; a non-zero entry leaves that colour unchanged. Every known caller (fx_lightning, weather_update,
 * race_setup_video) passes NULL. `in` and `out` may alias. */
void ign_palette_blend(const IgnPalette *in, int r, int g, int b, double frac, const int32_t *keep,
                       IgnPalette *out);

/* palette_lerp (sub_00051a9c, eax=a, edx=b, ebx=out, ecx=t): t >= 256 copies `b`; otherwise each byte is
 * a + (b - a) * t / 256, the product wrapping at 32 bits and the division truncating toward zero, low byte kept.
 * sub_00012bb8 (menu) crossfades with it. `out` may alias `a` or `b`. */
void ign_palette_lerp(const IgnPalette *a, const IgnPalette *b, int t, IgnPalette *out);

#endif
