#include <stdio.h>
#include <stdlib.h>

#include "game/track.h"

static int check(int condition, const char *track, const char *message) {
    if (condition) return 0;
    fprintf(stderr, "FAIL %s: %s\n", track, message);
    return 1;
}

static int validate_track(const char *data_dir, const char *name) {
    IgnLevel level = {0};
    IgnTrack track = {0};
    int failed = 0;
    if (ign_load_level(data_dir, name, &level)) {
        fprintf(stderr, "FAIL %s: cannot load original track assets\n", name);
        return 1;
    }
    if (ign_build_track(&level.track, &track)) {
        fprintf(stderr, "FAIL %s: cannot build route topology\n", name);
        ign_free_level(&level);
        return 1;
    }

    ign_track_gates(&track, &level);
    if (ign_track_ai_records(&track, &level)) {
        fprintf(stderr, "FAIL %s: cannot build TRI route records\n", name);
        ign_free_track(&track);
        ign_free_level(&level);
        return 1;
    }

    int main_objects = 0, main_gaps = 0, first_main_gap = -1, branch_objects = 0, records = 0;
    failed |= check(track.start_obj >= 0 && track.dir_obj >= 0, name,
                    "start and direction PLC objects are present");
    failed |= check(track.count > 0 && track.count <= track.nobj, name,
                    "section count is within the PLC object count");
    failed |= check(track.section_obj && track.section_obj_b && track.section_next_obj &&
                        track.section_next_obj_b && track.has_b && track.ai_records &&
                        track.ai_record_count,
                    name, "route and TRI tables are allocated");

    if (!failed) {
        failed |= check(track.section_obj[0] == track.start_obj &&
                            track.section_next_obj[0] == track.dir_obj,
                        name, "section zero starts at the start PLC and points to its direction object");
        for (int s = 0; s < track.count; s++) {
            int main_obj = track.section_obj[s];
            int branch_obj = track.section_obj_b[s];
            if (main_obj >= 0) {
                main_objects++;
                int mapped_section = main_obj < track.nobj ? track.section[main_obj] : -1;
                if (main_obj >= track.nobj ||
                    (mapped_section != s &&
                     !(mapped_section > s && mapped_section < track.count &&
                       track.section_obj[mapped_section] == main_obj))) {
                    fprintf(stderr, "FAIL %s: main section %d -> object %d -> mapped section %d\n",
                            name, s, main_obj, mapped_section);
                    failed = 1;
                }
            } else {
                if (first_main_gap < 0) first_main_gap = s;
                main_gaps++;
            }
            failed |= check(track.section_next_obj[s] == -1 ||
                                (track.section_next_obj[s] >= 0 &&
                                 track.section_next_obj[s] < track.nobj),
                            name, "main route successor is a valid PLC object");
            failed |= check(track.section_next_obj_b[s] == -1 ||
                                (track.section_next_obj_b[s] >= 0 &&
                                 track.section_next_obj_b[s] < track.nobj),
                            name, "branch-B successor is a valid PLC object");
            if (branch_obj >= 0) {
                branch_objects++;
                int mapped_branch = branch_obj < track.nobj ? track.section_b[branch_obj] : 0;
                failed |= check(branch_obj < track.nobj &&
                                    (mapped_branch == -s ||
                                     (mapped_branch < -s && -mapped_branch < track.count &&
                                      track.section_obj_b[-mapped_branch] == branch_obj)),
                                name, "branch-B section maps back to its PLC object");
                failed |= check(track.has_b[s] != 0, name,
                                "branch-B object has a corresponding route gate");
            }
            for (int path = 0; path < IGN_AI_ROUTE_PATHS; path++) {
                int count = track.ai_record_count[s][path];
                failed |= check(count <= IGN_AI_ROUTE_RECORDS, name,
                                "TRI record count stays within the route capacity");
                records += count;
                for (int r = 0; r < count; r++) {
                    const IgnAiRouteRecord *record = &track.ai_records[s][path][r];
                    failed |= check(record->valid && record->object >= 0 &&
                                        record->object < track.nobj &&
                                        record->object < level.surface.nengine &&
                                        level.surface.engine_to_plc &&
                                        record->plc == level.surface.engine_to_plc[record->object],
                                    name, "TRI route record preserves engine-to-PLC identity");
                }
            }
        }
        failed |= check(main_objects == track.count && main_gaps == 0, name,
                        "every main-route section retains its ordered PLC object, including repeated terminal entries");
        failed |= check(branch_objects > 0 && records > 0, name,
                        "track has branch-B and TRI records");
        printf("%-8s objects=%d sections=%d main=%d main_gaps=%d first_gap=%d branch_b=%d tri_records=%d\n",
               name, track.nobj, track.count, main_objects, main_gaps, first_main_gap,
               branch_objects, records);
    }

    ign_free_track(&track);
    ign_free_level(&level);
    return failed;
}

int main(int argc, char **argv) {
    static const char *const tracks[] = {
        "USA", "CANADA", "AUSTRIA", "BRAZIL", "CARIB", "ICELAND", "JAPAN",
    };
    if (argc != 2) {
        fprintf(stderr, "usage: %s GAME_DATA_DIRECTORY\n", argv[0]);
        return 2;
    }

    int failed = 0;
    for (size_t i = 0; i < sizeof tracks / sizeof tracks[0]; i++)
        failed |= validate_track(argv[1], tracks[i]);
    return failed != 0;
}
