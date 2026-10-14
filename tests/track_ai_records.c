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

static void set_vertex_record(uint8_t *block, uint8_t tri_type) {
    block[1] = 0;
    block[2] = 0;
    block[0x17] = 1;
    block[0x18] = 0;
    block[0x6d] = tri_type;
}

int main(void) {
    int failed = 0;
    int32_t vertices[] = {
        10, 20, 30,
        110, 220, 330,
    };
    IgnObject objects[] = {
        {.type = 3, .pos = {5, 7, 9}, .nv = 2, .verts = vertices},
        {.type = 4, .pos = {-3, 11, -13}, .nv = 2, .verts = vertices},
    };
    int engine_to_plc[] = {1, 0};
    uint8_t tri[4 + 2 * 500] = {0};
    uint32_t tri_objects = 2;
    memcpy(tri, &tri_objects, sizeof tri_objects);
    set_vertex_record(tri + 4, 2);
    set_vertex_record(tri + 4 + 500, 1);

    IgnLevel level = {
        .track = {.nobj = 2, .objs = objects},
        .surface = {.engine_to_plc = engine_to_plc, .nengine = 2},
        .tri = tri,
        .tri_size = sizeof tri,
    };
    IgnTrack track = {.nobj = 2, .count = 2};
    track.section_obj = malloc(2 * sizeof *track.section_obj);
    track.section = calloc(2, sizeof *track.section);
    if (!track.section_obj || !track.section) {
        fprintf(stderr, "FAIL allocating AI record fixture\n");
        ign_free_track(&track);
        return 1;
    }
    track.section_obj[0] = 0;
    track.section_obj[1] = 0;
    track.section[0] = 0;
    track.section[1] = -1;

    failed |= check(ign_track_ai_records(&track, &level) == 0, "build TRI AI records");
    failed |= check(track.ai_record_count && track.ai_record_count[1][0] == 2,
                    "TRI type 1 creates two records on its own route");
    failed |= check(track.ai_record_count && track.ai_record_count[1][1] == 2,
                    "TRI type 2 creates two records on the alternate route");
    if (track.ai_records && track.ai_record_count) {
        const IgnAiRouteRecord *left_first = &track.ai_records[1][0][0];
        const IgnAiRouteRecord *left_second = &track.ai_records[1][0][1];
        const IgnAiRouteRecord *right_first = &track.ai_records[1][1][0];
        const IgnAiRouteRecord *right_second = &track.ai_records[1][1][1];
        failed |= check(left_first->point[0] == 25665 && left_first->point[1] == 127 &&
                            left_first->point[2] == 25789 && left_first->point[3] == 25715 &&
                            left_first->point[4] == 227 && left_first->point[5] == 25939,
                        "type 1 midpoint-to-right record uses world x/z origin");
        failed |= check(left_second->point[0] == 25615 && left_second->point[1] == 27 &&
                            left_second->point[2] == 25639 && left_second->point[3] == 25665 &&
                            left_second->point[4] == 127 && left_second->point[5] == 25789,
                        "type 1 left-to-midpoint record preserves y without world offset");
        failed |= check(right_first->point[0] == 25607 && right_first->point[1] == 31 &&
                            right_first->point[2] == 25617 && right_first->point[3] == 25657 &&
                            right_first->point[4] == 131 && right_first->point[5] == 25767,
                        "type 2 left-to-midpoint record uses mapped PLC block");
        failed |= check(right_second->point[0] == 25657 && right_second->point[1] == 131 &&
                            right_second->point[2] == 25767 && right_second->point[3] == 25707 &&
                            right_second->point[4] == 231 && right_second->point[5] == 25917,
                        "type 2 midpoint-to-right record is retained independently");
        failed |= check(left_first->object == 0 && left_first->plc == 1 &&
                            right_first->object == 1 && right_first->plc == 0,
                        "route records preserve engine-to-PLC identity");
    }

    ign_free_track(&track);
    return failed;
}
