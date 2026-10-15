/* Track-only frame of the port renderer for comparisons with tests/render_original.py.
 * usage: render_shot DATA LEVEL WxH X Y Z YAW PITCH OUT.raw [PRIMS.txt]
 * Camera as ./ignition --cam; writes 8-bit indices and, if given, the sorted primitive list.
 * IGN_ROT=k rotates every k-th PLC object by angles derived from its index (see tests/render_original.py). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "render/lisa.h"
#include "render/raster.h"

static IgnFrame frame;
static LisaFrame lf;

int main(int argc, char **argv) {
    if (argc != 10 && argc != 11) {
        fprintf(stderr, "usage: %s DATA LEVEL WxH X Y Z YAW PITCH OUT.raw [PRIMS.txt]\n", argv[0]);
        return 2;
    }
    static IgnLevel lv;
    if (ign_load_level(argv[1], argv[2], &lv)) return 1;
    ign_render_set_level_dir(argv[1], argv[2]);
    int w = 0, h = 0;
    if (sscanf(argv[3], "%dx%d", &w, &h) != 2) return 2;
    ign_frame_init(&frame, w, h);
    IgnCamera cam = {strtof(argv[4], NULL), strtof(argv[5], NULL), strtof(argv[6], NULL), strtof(argv[7], NULL),
                     strtof(argv[8], NULL)};
    const char *rot = getenv("IGN_ROT");
    if (rot && atoi(rot) > 0)
        for (int i = 0; i < lv.track.nobj; i += atoi(rot)) {
            lv.track.objs[i].rotation[0] = (i * 37) % 3600 - 100;
            lv.track.objs[i].rotation[1] = (i * 53) % 3600;
            lv.track.objs[i].rotation[2] = (i * 71) % 3700;
        }
    ign_frame_clear(&frame);
    ign_render_track(&frame, &lv, &cam);
    FILE *f = fopen(argv[9], "wb");
    if (!f || fwrite(frame.color, 1, (size_t)frame.w * frame.h, f) != (size_t)frame.w * frame.h) return 1;
    fclose(f);
    if (argc == 11) {
        const LisaFrame *built = ign_render_last_frame();
        FILE *t = fopen(argv[10], "w");
        if (!t || !built) return 1;
        for (int i = 0; i < built->norder; i++) {
            const LisaPrim *p = &built->prims[built->order[i]];
            fprintf(t, "%x", p->type);
            for (int k = 0; k < 6; k++) fprintf(t, " %d", p->xy[k]);
            if (p->type == 0x14 || p->type == 0x15) fprintf(t, " L %d %d %d", p->light[0], p->light[1], p->light[2]);
            fprintf(t, " P %d T %d\n", (int)((p->tex - lv.track.tex) >> 16),
                    !p->lut ? 0 : p->lut == lv.shd ? 2 : 1);
        }
        fclose(t);
        if (getenv("IGN_VIS"))
            for (int i = 0; i < built->nvis; i++) {
                fprintf(stderr, "vis %d base %d\n", built->vis[i], built->vbase[i]);
                int end = i + 1 < built->nvis ? built->vbase[i + 1] : built->nvert;
                for (int k = built->vbase[i]; k < end; k++)
                    fprintf(stderr, "  v%d %d %d %d\n", k - built->vbase[i], built->verts[k].x, built->verts[k].y,
                            built->verts[k].z);
            }
    }
    ign_free_level(&lv);
    return 0;
}
