#include "palette.h"

#include <math.h>

/* 0x5154f..0x5155d: fild (target - c), fmul frac, sub_00055f9c (frndint with RC = chop), fistp. The Watcom
 * runtime (init routine 0x56294, XI table 0xe7070) loads CW 0x127f from [0xe6be0]: 53-bit precision, so the product
 * is rounded to a double before the chop, as in C. A NaN or out-of-range result stores the integer indefinite
 * 0x80000000 (exceptions masked). */
static int32_t chop_step(int32_t d, double frac) {
    double t = trunc(d * frac);
    if (!isfinite(t) || t < (double)INT32_MIN || t > (double)INT32_MAX) return INT32_MIN;
    return (int32_t)t;
}

void ign_palette_blend(const IgnPalette *in, int r, int g, int b, double frac, const int32_t *keep,
                       IgnPalette *out) {
    const int target[3] = {r, g, b};
    IgnPalette src = *in; /* 0x51514: the original works on a copy of the 0x300 bytes */
    for (int c = 0; c < 256; c++) {
        for (int k = 0; k < 3; k++) {
            uint8_t v = src.rgb[c][k];
            /* 0x51533: a NULL `keep` or a zero entry blends this colour. */
            if (!keep || !keep[c]) {
                int32_t d = (int32_t)((uint32_t)target[k] - v);
                v = (uint8_t)((uint32_t)v + (uint32_t)chop_step(d, frac));
            }
            out->rgb[c][k] = v;
        }
    }
}
