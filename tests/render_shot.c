/* Track-only frame of the port renderer for comparisons with tests/render_original.py.
 * usage: render_shot DATA LEVEL WxH X Y Z YAW PITCH OUT.raw  (camera as ./ignition --cam; writes 8-bit indices) */
#include <stdio.h>
#include <stdlib.h>

#include "render/raster.h"

static IgnFrame frame;

int main(int argc, char **argv) {
    if (argc != 10) {
        fprintf(stderr, "usage: %s DATA LEVEL WxH X Y Z YAW PITCH OUT.raw\n", argv[0]);
        return 2;
    }
    static IgnLevel lv;
    if (ign_load_level(argv[1], argv[2], &lv)) return 1;
    int w = 0, h = 0;
    if (sscanf(argv[3], "%dx%d", &w, &h) != 2) return 2;
    ign_frame_init(&frame, w, h);
    IgnCamera cam = {strtof(argv[4], NULL), strtof(argv[5], NULL), strtof(argv[6], NULL), strtof(argv[7], NULL),
                     strtof(argv[8], NULL)};
    ign_frame_clear(&frame);
    ign_render_track(&frame, &lv, &cam);
    FILE *f = fopen(argv[9], "wb");
    if (!f || fwrite(frame.color, 1, (size_t)frame.w * frame.h, f) != (size_t)frame.w * frame.h) return 1;
    fclose(f);
    ign_free_level(&lv);
    return 0;
}
