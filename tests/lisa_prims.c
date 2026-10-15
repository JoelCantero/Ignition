/* Draws Lisa 2 primitive records with the port rasterizer, for tests/render_original.py --prims comparisons.
 * usage: lisa_prims [--each] TEX PRIMS OUT.raw  (--each: one cleared frame per record, concatenated)
 * PRIMS: int32 width, height, clip x0, y0, x1, y1, then records of 32 int32: [0] type, [1..23] the words of the
 * original record (pointer words unused), [24..29] uv (8.8), [30] TEX page, [31] blend table id. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/assets.h"
#include "render/lisa.h"

int main(int argc, char **argv) {
    int each = argc == 5 && !strcmp(argv[1], "--each");
    argv += each, argc -= each;
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
    IgnRaster r = {.fb = fb, .stride = w};
    ign_raster_clip(&r, p[2], p[3], p[4], p[5]);
    int n = (int)((size / 4 - 6) / 32);
    FILE *f = fopen(argv[3], "wb");
    if (!f) return 1;
    for (int i = 0; i < n; i++) {
        const int32_t *rec = p + 6 + i * 32;
        const uint8_t *page = tex + ((size_t)rec[30] << 16) % tex_size;
        switch (rec[0]) {
        case 0x11: ign_prim_tex(&r, rec + 1, rec + 24, page); break;
        case 0x14: {
            const int32_t xy[6] = {rec[2], rec[3], rec[5], rec[6], rec[8], rec[9]}, l[3] = {rec[4], rec[7], rec[10]};
            ign_prim_persp(&r, xy, l, rec + 24, page, NULL);
            break;
        }
        default: fprintf(stderr, "primitive 0x%x not supported\n", rec[0]); return 1;
        }
        if (each) {
            if (fwrite(fb, 1, (size_t)w * h, f) != (size_t)w * h) return 1;
            memset(fb, 0, (size_t)w * h);
        }
    }
    if (!each && fwrite(fb, 1, (size_t)w * h, f) != (size_t)w * h) return 1;
    fclose(f);
    return 0;
}
