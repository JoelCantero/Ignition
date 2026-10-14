#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/track.h"

static int check(int condition, const char *message) {
    if (condition) return 0;
    fprintf(stderr, "FAIL %s\n", message);
    return 1;
}

int main(void) {
    int failed = 0;
    int32_t vertices0[] = {10, 0, 20, 11, 0, 21, 12, 0, 22, 13, 0, 23};
    int32_t vertices1[] = {100, 0, 200, 101, 0, 201, 102, 0, 202, 103, 0, 203};
    IgnObject objects[] = {
        {.nv = 4, .verts = vertices0},
        {.nv = 4, .verts = vertices1},
    };
    int engine_to_plc[] = {1, 0};
    uint8_t tri[4 + 2 * 500] = {0};
    uint32_t tri_objects = 2;
    memcpy(tri, &tri_objects, sizeof tri_objects);
    tri[4 + 1] = 0;
    tri[4 + 2] = 0;
    tri[4 + 0x17] = 1;
    tri[4 + 0x18] = 0;
    tri[4 + 500 + 1] = 2;
    tri[4 + 500 + 2] = 0;
    tri[4 + 500 + 0x17] = 3;
    tri[4 + 500 + 0x18] = 0;

    IgnLevel level = {
        .track = {.nobj = 2, .objs = objects},
        .surface = {.engine_to_plc = engine_to_plc, .nengine = 2},
        .tri = tri,
        .tri_size = sizeof tri,
    };
    IgnTrack track = {.nobj = 2, .count = 2};
    track.section = calloc(2, sizeof *track.section);
    track.center = calloc(2, sizeof *track.center);
    if (!track.section || !track.center) {
        fprintf(stderr, "FAIL allocating track-gate fixture\n");
        ign_free_track(&track);
        return 1;
    }
    track.section[0] = 0;
    track.section[1] = 1;
    track.center[0][0] = 50;
    track.center[0][2] = 60;
    track.center[1][0] = 70;
    track.center[1][2] = 80;

    ign_track_gates(&track, &level);
    failed |= check(track.gate && track.gate[0][0] == 12 && track.gate[0][1] == 22 &&
                        track.gate[0][2] == 13 && track.gate[0][3] == 23,
                    "engine object 0 uses its mapped PLC block");
    failed |= check(track.gate && track.gate[1][0] == 100 && track.gate[1][1] == 200 &&
                        track.gate[1][2] == 101 && track.gate[1][3] == 201,
                    "engine object 1 uses its mapped PLC block");
    ign_free_track(&track);
    return failed;
}
