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

/* 0x10d34 / 0x11004: caller-owned replacement for the original global square, brightness-bucket and
 * chromaticity tables. Initialise before searching; rebuild buckets when the searched palette changes. */
typedef struct {
    int32_t squares[599]; /* differences -299..299, zero at 299 */
    int32_t buckets[1025];
    int32_t red_fraction[256], green_fraction[256];
} IgnColorSearch;

void ign_color_squares_init(IgnColorSearch *search);
void ign_color_buckets_build(IgnColorSearch *search, const IgnPalette *palette);
int ign_rgb_avg3(const uint8_t rgb[3]);
void ign_rgb_avg(const uint8_t a[3], const uint8_t b[3], int32_t out[3]);
void ign_rgb_lerp(const uint8_t a[3], const uint8_t b[3], int32_t t, int32_t out[3]);

/* RGB components 0..255. Equal distances retain the first index. The fast search is deliberately the
 * original heuristic, not necessarily the exact nearest colour; it falls back when its error exceeds 2048. */
int ign_nearest_color(const IgnColorSearch *search, const int32_t rgb[3], const IgnPalette *palette);
int ign_nearest_color_fast(const IgnColorSearch *search, const int32_t rgb[3], const IgnPalette *palette);

/* 0x10e04 / 0x10ea4: out[a_index * b_count + b_index], not the other way around.
 * Counts are 0..256; empty tables do not write. The output must not overlap the palettes. */
int ign_palette_table_avg(const IgnPalette *a, int a_count, const IgnPalette *b, int b_count,
                          const IgnPalette *palette, int fast, uint8_t *out);
/* 0x10f4c: a full 256x256 table; t is 0..256. */
int ign_palette_table_lerp(const IgnPalette *palette, int t, uint8_t out[65536]);
/* 0x11494: colour zero preserves the background, all other rows overwrite it. */
void ign_palette_table_overlay(uint8_t out[65536]);

#endif
