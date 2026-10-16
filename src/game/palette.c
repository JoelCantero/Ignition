#include "palette.h"

#include <math.h>
#include <stdio.h>

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

void ign_palette_lerp(const IgnPalette *a, const IgnPalette *b, int t, IgnPalette *out) {
    for (int c = 0; c < 256; c++) {
        for (int k = 0; k < 3; k++) {
            uint8_t va = a->rgb[c][k], vb = b->rgb[c][k];
            if (t >= 0x100) { /* 0x51aaa: signed compare, so a negative t blends */
                out->rgb[c][k] = vb;
                continue;
            }

            /* 0x51ad1..0x51ae6: imul, then sar/shl/sbb/sar 8 rounds the quotient toward zero. */
            int32_t p = (int32_t)((uint32_t)(vb - va) * (uint32_t)t);
            out->rgb[c][k] = (uint8_t)((uint32_t)(p / 256) + va);
        }
    }
}

void ign_color_squares_init(IgnColorSearch *search) {
    for (int n = 0; n < 300; n++) {
        search->squares[299 + n] = n * n;
        search->squares[299 - n] = n * n;
    }
}

int ign_rgb_avg3(const uint8_t rgb[3]) {
    return (rgb[0] + rgb[1] + rgb[2]) / 3;
}

void ign_color_buckets_build(IgnColorSearch *search, const IgnPalette *palette) {
    for (int group = 0; group < 4; group++) {
        int n = 0;
        for (int c = 0; c < 256; c++) {
            int value = ign_rgb_avg3(palette->rgb[c]);
            if ((group == 0 && value < 96) || (group == 1 && value > 32 && value < 160) ||
                (group == 2 && value > 96 && value < 224) || (group == 3 && value > 160))
                search->buckets[group * 256 + n++] = c;
        }
        /* The DOS arrays have a 256-dword stride: a full list's terminator can be overwritten by the next
         * list. Preserve that layout instead of silently replacing the heuristic with independent lists. */
        search->buckets[group * 256 + n] = -1;
    }
    for (int c = 0; c < 256; c++) {
        const uint8_t *rgb = palette->rgb[c];
        int sum = rgb[0] + rgb[1] + rgb[2];
        search->red_fraction[c] = sum ? rgb[0] * 255 / sum : 85;
        search->green_fraction[c] = sum ? rgb[1] * 255 / sum : 85;
    }
}

void ign_rgb_avg(const uint8_t a[3], const uint8_t b[3], int32_t out[3]) {
    for (int k = 0; k < 3; k++) out[k] = (a[k] + b[k]) / 2;
}

void ign_rgb_lerp(const uint8_t a[3], const uint8_t b[3], int32_t t, int32_t out[3]) {
    for (int k = 0; k < 3; k++) {
        uint32_t p = a[k] * (256u - (uint32_t)t) + b[k] * (uint32_t)t;
        out[k] = (int32_t)p / 256;
    }
}

static int valid_rgb(const int32_t rgb[3]) {
    for (int k = 0; k < 3; k++)
        if (rgb[k] < 0 || rgb[k] > 255) {
            fprintf(stderr, "Palette search requires RGB components in 0..255\n");
            return 0;
        }
    return 1;
}

static int color_distance(const IgnColorSearch *search, const int32_t rgb[3], const uint8_t color[3],
                           int best) {
    int distance = 0;
    for (int k = 0; k < 3; k++) {
        distance += search->squares[299 + rgb[k] - color[k]];
        if (distance >= best) break;
    }
    return distance;
}

int ign_nearest_color(const IgnColorSearch *search, const int32_t rgb[3], const IgnPalette *palette) {
    if (!valid_rgb(rgb)) return -1;
    int best = 0x7f000000, index = 0;
    for (int c = 0; c < 256; c++) {
        int distance = color_distance(search, rgb, palette->rgb[c], best);
        if (distance < best) {
            best = distance;
            index = c;
        }
    }
    return index;
}

int ign_nearest_color_fast(const IgnColorSearch *search, const int32_t rgb[3], const IgnPalette *palette) {
    if (!valid_rgb(rgb)) return -1;
    int sum = rgb[0] + rgb[1] + rgb[2];
    int red = sum ? rgb[0] * 255 / sum : 85;
    int low = red < 20 ? 0 : red - 20, high = red + 20;
    int group = ((sum >> 1) + (sum >> 2)) >> 6;
    if (group > 3) group = 3;
    int best = 0x7f000000, index = 0, at = group * 256;
    while (at < 1024 && search->buckets[at] >= 0) {
        int c = search->buckets[at++];
        if (search->red_fraction[c] <= low || search->red_fraction[c] >= high) continue;
        int distance = color_distance(search, rgb, palette->rgb[c], best);
        if (distance < best) {
            best = distance;
            index = c;
        }
    }
    if (at == 1024) {
        /* With 256 bright colours the DOS terminator is overwritten by chromaticity data; its search
         * then runs past the tables. Reject that undefined DOS case rather than reading out of bounds. */
        fprintf(stderr, "Palette search brightness list has no terminator\n");
        return -1;
    }
    return best > 2048 ? ign_nearest_color(search, rgb, palette) : index;
}

int ign_palette_table_avg(const IgnPalette *a, int a_count, const IgnPalette *b, int b_count,
                          const IgnPalette *palette, int fast, uint8_t *out) {
    if (a_count < 0 || a_count > 256 || b_count < 0 || b_count > 256) {
        fprintf(stderr, "Palette table counts must be in 0..256\n");
        return -1;
    }
    IgnColorSearch search;
    ign_color_squares_init(&search);
    if (fast) ign_color_buckets_build(&search, palette);
    for (int j = 0; j < b_count; j++)
        for (int i = 0; i < a_count; i++) {
            int32_t rgb[3];
            ign_rgb_avg(a->rgb[i], b->rgb[j], rgb);
            int c = fast ? ign_nearest_color_fast(&search, rgb, palette)
                         : ign_nearest_color(&search, rgb, palette);
            if (c < 0) return -1;
            out[i * b_count + j] = (uint8_t)c;
        }
    return 0;
}

int ign_palette_table_lerp(const IgnPalette *palette, int t, uint8_t out[65536]) {
    if (t < 0 || t > 256) {
        fprintf(stderr, "Palette table interpolation weight must be in 0..256\n");
        return -1;
    }
    IgnColorSearch search;
    ign_color_squares_init(&search);
    for (int j = 0; j < 256; j++)
        for (int i = 0; i < 256; i++) {
            int32_t rgb[3];
            ign_rgb_lerp(palette->rgb[i], palette->rgb[j], t, rgb);
            out[i * 256 + j] = (uint8_t)ign_nearest_color(&search, rgb, palette);
        }
    return 0;
}

void ign_palette_table_overlay(uint8_t out[65536]) {
    for (int a = 0; a < 256; a++)
        for (int b = 0; b < 256; b++) out[a * 256 + b] = (uint8_t)(a ? a : b);
}
