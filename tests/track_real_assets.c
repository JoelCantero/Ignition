#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>
#include <string.h>

#include "game/ghost.h"
#include "game/track.h"

static int check(int condition, const char *track, const char *message) {
    if (condition) return 0;
    fprintf(stderr, "FAIL %s: %s\n", track, message);
    return 1;
}

static void print_target_record(const IgnAiRouteRecord *record) {
    uint32_t bits;
    memcpy(&bits, &record->heading[1], sizeof bits);
    for (int k = 0; k < 6; k++) printf("%" PRId32 " ", record->point[k]);
    for (int k = 0; k < 4; k++) printf("%" PRId32 " ", record->inset[k]);
    printf("%" PRIu32 " %d ", bits, record->surface);
}

/* Inputs for tests/ai_target_original.py: slot-0 records at the clamped lookahead indices of
 * 0x1b1a5..0x1b1f3 and car positions around the current gate (SRF coordinates). */
static void print_target_fixtures(const IgnTrack *track) {
    static const int offsets[6] = {1, 2, 3, 4, 6, 8};
    for (int s = 0; s < track->count; s++) {
        const IgnAiRouteRecord *current = ign_track_ai_record(track, s, 0), *ahead[6];
        int complete = current && current->valid && current->geometry_valid;
        for (int k = 0; k < 6 && complete; k++) {
            int sk = s + offsets[k];
            if (sk >= track->count) sk -= track->count;
            if (sk >= track->count - 3) sk = track->count - 1;
            ahead[k] = ign_track_ai_record(track, sk, 0);
            complete = ahead[k] && ahead[k]->valid && ahead[k]->geometry_valid;
        }
        if (!complete) continue;
        const int32_t *p = current->point, *in = current->inset;
        double cx = p[0] + (p[3] - p[0]) * 0.5, cz = p[2] + (p[5] - p[2]) * 0.5;
        double cars[4][2] = {{cx + 0.375, cz - 0.625}, {in[0] + 0.25, in[1] + 0.5},
                             {in[2] - 0.75, in[3] + 0.125},
                             {p[3] + (p[3] - p[0]) * 0.25, p[5] + (p[5] - p[2]) * 0.25}};
        for (int c = 0; c < 4; c++) {
            printf("%.17g %.17g %.17g ", c & 1 ? 0.0 : 5.0, cars[c][0], cars[c][1]);
            print_target_record(current);
            for (int k = 0; k < 6; k++) print_target_record(ahead[k]);
            printf("\n");
        }
    }
}

static int validate_track(const char *data_dir, const char *name, int oracle) {
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
    if (ign_track_ai_records(&track, &level, ign_ghost_track_id(name))) {
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
                failed |= check(path == 0 || count <= 1, name, "branch B is never split");
                failed |= check(path == 0 || track.ai_record_count[s][0] < 2 || count == 0,
                                name, "a main split suppresses branch B");
                for (int r = 0; r < count; r++) {
                    const IgnAiRouteRecord *record = &track.ai_records[s][path][r];
                    failed |= check(record->valid && record->object >= 0 &&
                                        record->object < track.nobj &&
                                        record->plc == record->object,
                                    name, "TRI route record preserves section-to-PLC identity");
                    if (!record->geometry_valid) {
                        int ns = -1, slot = -1;
                        int requested = path == 0 && count > 1 ? r : path;
                        int found = ign_track_find_section(&track, s + 1, requested, 1,
                                                          ign_ghost_track_id(name), &ns, &slot, NULL);
                        fprintf(stderr, "FAIL %s: geometry s=%d slot=%d successor=%d/%d found=%d counts=%d/%d\n",
                                name, s, requested, ns, slot, found,
                                ns >= 0 ? track.ai_record_count[ns][0] : -1,
                                ns >= 0 ? track.ai_record_count[ns][1] : -1);
                        failed = 1;
                    }
                    for (int k = 0; k < 3; k++)
                        failed |= check(isfinite(record->heading[k]) && isfinite(record->slope[k]),
                                        name, "TRI longitudinal angles are finite");
                    for (int k = 0; k < 2; k++)
                        failed |= check(isfinite(record->across[k]), name,
                                        "TRI transverse angles are finite");
                    if (oracle == 1 && record->geometry_valid) {
                        int ns, slot, requested = path == 0 && count > 1 ? r : path;
                        if (!ign_track_find_section(&track, s + 1, requested, 1,
                                                    ign_ghost_track_id(name), &ns, &slot, NULL)) {
                            failed |= check(0, name, "oracle successor resolution failed");
                            continue;
                        }
                        const IgnAiRouteRecord *next = ign_track_ai_record(&track, ns, slot);
                        if (!next || !next->valid) {
                            failed |= check(0, name, "oracle successor record missing");
                            continue;
                        }
                        for (int k = 0; k < 6; k++) printf("%" PRId32 " ", record->point[k]);
                        for (int k = 0; k < 6; k++) printf("%" PRId32 " ", next->point[k]);
                        for (int k = 0; k < 8; k++) {
                            const float *value = k < 3 ? &record->heading[k] :
                                k < 6 ? &record->slope[k - 3] : &record->across[k - 6];
                            uint32_t bits;
                            memcpy(&bits, value, sizeof bits);
                            printf("%" PRIu32 " ", bits);
                        }
                        printf("%" PRId32 " %" PRId32 " %" PRId32 " %" PRId32 "\n",
                               record->inset[0], record->inset[1], record->inset[2], record->inset[3]);
                    }
                }
            }
        }
        failed |= check(main_objects == track.count && main_gaps == 0, name,
                        "every main-route section retains its ordered PLC object, including repeated terminal entries");
        failed |= check(branch_objects > 0 && records > 0, name,
                        "track has branch-B and TRI records");
        if (oracle == 2) print_target_fixtures(&track);
        if (!oracle) printf("%-8s objects=%d sections=%d main=%d main_gaps=%d first_gap=%d branch_b=%d tri_records=%d\n",
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
    int oracle = argc != 3 ? 0 : strcmp(argv[2], "--geometry-oracle") == 0 ? 1 :
                 strcmp(argv[2], "--target-fixtures") == 0 ? 2 : -1;
    if (argc != 2 && oracle <= 0) {
        fprintf(stderr, "usage: %s GAME_DATA_DIRECTORY [--geometry-oracle|--target-fixtures]\n", argv[0]);
        return 2;
    }

    int failed = 0;
    for (size_t i = 0; i < sizeof tracks / sizeof tracks[0]; i++)
        failed |= validate_track(argv[1], tracks[i], oracle);
    return failed != 0;
}
