#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/ai.h"
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

static int oracle(void) {
    IgnAiRouteRecord record = {0}, next = {0};
    while (scanf("%" SCNd32, &record.point[0]) == 1) {
        for (int i = 1; i < 6; i++)
            if (scanf("%" SCNd32, &record.point[i]) != 1) return 2;
        for (int i = 0; i < 6; i++)
            if (scanf("%" SCNd32, &next.point[i]) != 1) return 2;
        ign_track_ai_geometry(&record, &next);
        for (int i = 0; i < 8; i++) {
            const float *value = i < 3 ? &record.heading[i] :
                                 i < 6 ? &record.slope[i - 3] : &record.across[i - 6];
            uint32_t bits;
            memcpy(&bits, value, sizeof bits);
            printf("%" PRIu32 " ", bits);
        }
        printf("%" PRId32 " %" PRId32 " %" PRId32 " %" PRId32 "\n",
               record.inset[0], record.inset[1], record.inset[2], record.inset[3]);
    }
    return ferror(stdin) ? 2 : 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--geometry-oracle") == 0) return oracle();
    if (argc == 2 && strcmp(argv[1], "--travel-oracle") == 0) {
        double heading, motion;
        while (scanf("%lf %lf", &heading, &motion) == 2)
            printf("%.17g\n", ign_ai_travel(heading, motion));
        return ferror(stdin) ? 2 : 0;
    }
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
    set_vertex_record(tri + 4, 1);
    set_vertex_record(tri + 4 + 500, 2);

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

    failed |= check(ign_track_ai_records(&track, &level, 1) == 0, "build TRI AI records");
    failed |= check(track.ai_record_count && track.ai_record_count[1][0] == 2,
                    "TRI type 1 creates two records on its own route");
    failed |= check(track.ai_record_count && track.ai_record_count[1][1] == 0,
                    "a main split suppresses the alternate route");
    if (track.ai_records && track.ai_record_count) {
        const IgnAiRouteRecord *left_first = &track.ai_records[1][0][0];
        const IgnAiRouteRecord *left_second = &track.ai_records[1][0][1];
        failed |= check(left_first->point[0] == 25665 && left_first->point[1] == 127 &&
                            left_first->point[2] == 25789 && left_first->point[3] == 25715 &&
                            left_first->point[4] == 227 && left_first->point[5] == 25939,
                        "type 1 midpoint-to-right record uses world x/z origin");
        failed |= check(left_second->point[0] == 25615 && left_second->point[1] == 27 &&
                            left_second->point[2] == 25639 && left_second->point[3] == 25665 &&
                            left_second->point[4] == 127 && left_second->point[5] == 25789,
                        "type 1 left-to-midpoint record preserves y without world offset");
        failed |= check(left_first->object == 0 && left_first->plc == 0,
                        "section objects already index PLC, independently of engine mapping");
        failed |= check(ign_track_ai_record(&track, 1, 0) == left_first &&
                            ign_track_ai_record(&track, 1, 1) == left_second,
                        "both canonical slots belong to the split main gate");
        failed |= check(left_first->geometry_valid && left_second->geometry_valid,
                        "both slots have original float32 geometry");
    }
    set_vertex_record(tri + 4, 0);
    failed |= check(ign_track_ai_records(&track, &level, 1) == 0, "rebuild without a main split");
    if (track.ai_record_count && track.ai_records) {
        const IgnAiRouteRecord *branch = ign_track_ai_record(&track, 1, 1);
        failed |= check(track.ai_record_count[1][0] == 1 && track.ai_record_count[1][1] == 1 &&
                            branch && branch->point[0] == 25607 && branch->point[3] == 25707 &&
                            branch->tri_type == 2,
                        "branch-B TRI type 2 remains one complete gate");
    }
    set_vertex_record(tri + 4, 2);
    failed |= check(ign_track_ai_records(&track, &level, 1) == 0, "rebuild a type-2 main split");
    if (track.ai_record_count && track.ai_records) {
        const IgnAiRouteRecord *first = ign_track_ai_record(&track, 1, 0);
        const IgnAiRouteRecord *second = ign_track_ai_record(&track, 1, 1);
        failed |= check(first && second && first->point[0] == 25615 &&
                            first->point[3] == 25665 && second->point[0] == 25665 &&
                            second->point[3] == 25715 && track.ai_record_count[1][1] == 0,
                        "type 2 produces left-to-middle then middle-to-right");
    }
    tri[4 + 0x17] = 0;
    failed |= check(ign_track_ai_records(&track, &level, 1) == 0,
                    "rebuild a zero-width original gate");
    const IgnAiRouteRecord *zero_width = ign_track_ai_record(&track, 1, 0);
    failed |= check(zero_width && zero_width->valid && zero_width->geometry_valid &&
                        zero_width->point[0] == zero_width->point[3],
                    "coincident TRI indices remain valid as in load_tri");

    ign_free_track(&track);
    return failed;
}
