/* Draws Lisa 2 primitive records with the port rasterizer, for tests/render_original.py --prims comparisons.
 * usage: lisa_prims [--each N] TEX PRIMS OUT.raw  (--each: one cleared frame per N records, concatenated)
 * PRIMS: int32 width, height, clip x0, y0, x1, y1, then records of 32 int32: [0] type, [1..23] the words of the
 * original record (pointer words unused), [24..29] uv (8.8), [30] TEX page, [31] blend table: 1 the keyed table
 * of 0x24c004 (a ? a : b), 2 the level SHD next to TEX. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/assets.h"
#include "render/lisa.h"

int main(int argc, char **argv) {
    int each = argc == 6 && !strcmp(argv[1], "--each") ? atoi(argv[2]) : 0;
    if (each) argv += 2, argc -= 2;
    if (argc != 4) {
        fprintf(stderr, "usage: %s [--each] TEX PRIMS OUT.raw\n", argv[0]);
        return 2;
    }
    size_t tex_size, size;
    uint8_t *tex = ign_read_file(argv[1], &tex_size);
    int32_t *p = (int32_t *)ign_read_file(argv[2], &size);
    if (!tex || !p || size < 24) return 1;
    int w = p[0], h = p[1];
    uint8_t *fb = calloc((size_t)w * h, 1);
    static uint8_t keyed[65536];
    for (int a = 0; a < 256; a++)
        for (int b = 0; b < 256; b++) keyed[a * 256 + b] = (uint8_t)(a ? a : b);
    char shd_path[1024];
    snprintf(shd_path, sizeof shd_path, "%s", argv[1]);
    size_t len = strlen(shd_path);
    if (len > 3) memcpy(shd_path + len - 3, "SHD", 3);
    size_t shd_size;
    uint8_t *shd = ign_read_file(shd_path, &shd_size);
    if (len > 3) memcpy(shd_path + len - 3, "TAB", 3);
    uint8_t *tab = ign_read_file(shd_path, &shd_size);
    IgnRaster r = {.fb = fb, .stride = w};
    ign_raster_clip(&r, p[2], p[3], p[4], p[5]);
    int n = (int)((size / 4 - 6) / 32);
    FILE *f = fopen(argv[3], "wb");
    if (!f) return 1;
    for (int i = 0; i < n; i++) {
        const int32_t *rec = p + 6 + i * 32;
        const uint8_t *page = tex + ((size_t)rec[30] << 16) % tex_size;
        const uint8_t *lut = rec[31] == 1 ? keyed : rec[31] == 2 ? shd : NULL;
        switch (rec[0]) {
        case 0x11:
        case 0x12: ign_prim_tex(&r, rec + 1, rec + 24, page, rec[0] == 0x12 ? lut : NULL); break;
        case 1: ign_prim_point(&r, rec[1], rec[2], tab, rec[4], (rec[5] >> 8) & 0xff); break;
        case 2: ign_prim_point(&r, rec[1], rec[2], shd, rec[3], -1); break;
        case 0xf: ign_prim_flat(&r, (const int32_t[6]){rec[1], rec[2], rec[4], rec[5], rec[7], rec[8]},
                                tab[(rec[10] & 0xff) << 8 | ((rec[11] >> 8) & 0xff)]); break;
        case 0x13: ign_prim_shade(&r, rec + 1, shd + ((size_t)(rec[7] & 0xff) << 8)); break;
        case 0x14:
        case 0x15: {
            const int32_t xy[6] = {rec[2], rec[3], rec[5], rec[6], rec[8], rec[9]}, l[3] = {rec[4], rec[7], rec[10]};
            ign_prim_persp(&r, xy, l, rec + 24, page, rec[0] == 0x15 ? lut : NULL);
            break;
        }
        default: fprintf(stderr, "primitive 0x%x not supported\n", rec[0]); return 1;
        }
        if (each && (i + 1) % each == 0) {
            if (fwrite(fb, 1, (size_t)w * h, f) != (size_t)w * h) return 1;
            memset(fb, 0, (size_t)w * h);
        }
    }
    if (!each && fwrite(fb, 1, (size_t)w * h, f) != (size_t)w * h) return 1;
    fclose(f);
    return 0;
}
