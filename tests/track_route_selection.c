#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "game/track.h"

static int check(int condition, const char *message) {
    if (condition) return 0;
    fprintf(stderr, "FAIL %s\n", message);
    return 1;
}

static int init_track(IgnTrack *t, int count, int nobj) {
    t->count = count;
    t->nobj = nobj;
    t->section_obj = malloc((size_t)count * sizeof *t->section_obj);
    t->section = malloc((size_t)nobj * sizeof *t->section);
    t->has_b = calloc((size_t)count, sizeof *t->has_b);
    t->ai_records = calloc((size_t)count, sizeof *t->ai_records);
    t->ai_record_count = calloc((size_t)count, sizeof *t->ai_record_count);
    if (!t->section_obj || !t->section || !t->has_b || !t->ai_records || !t->ai_record_count)
        return 0;
    for (int s = 0; s < count; s++) {
        t->section_obj[s] = s;
        t->section[s] = s;
        t->ai_record_count[s][0] = 1;
        t->ai_records[s][0][0].valid = 1;
    }
    return 1;
}

static void add_branch_object(IgnTrack *t, int section, int object) {
    t->section[object] = -section;
    t->has_b[section] = 1;
}

int main(void) {
    int failed = 0, section = -1, branch = -1, object = -1;
    IgnTrack t = {0};
    if (!init_track(&t, 5, 7)) {
        fprintf(stderr, "FAIL allocating route-selection fixture\n");
        ign_free_track(&t);
        return 1;
    }
    add_branch_object(&t, 1, 5);
    t.ai_record_count[1][1] = 1;
    t.ai_records[1][1][0].valid = 1;

    failed |= check(ign_track_find_section(&t, 1, 1, 1, 1, &section, &branch, &object) &&
                        section == 1 && branch == 1 && object == 5,
                    "selects an available alternate branch");
    failed |= check(ign_track_find_section(&t, 3, 1, 1, 1, &section, &branch, &object) &&
                        section == 3 && branch == 0 && object == 3,
                    "falls back to the main route when branch B is absent");

    t.ai_records[2][0][0].valid = 0;
    failed |= check(ign_track_find_section(&t, 2, 0, 1, 1, &section, &branch, &object) &&
                        section == 2 && branch == 0,
                    "accepts an invalid record when the preceding section has no branch");
    t.has_b[1] = 1;
    t.ai_records[3][0][0].valid = 1;
    failed |= check(ign_track_find_section(&t, 2, 0, 1, 1, &section, &branch, &object) &&
                        section == 3 && branch == 0,
                    "advances past an invalid record at a branch transition");

    t.ai_records[4][0][0].valid = 0;
    t.has_b[3] = 1;
    failed |= check(ign_track_find_section(&t, 4, 0, 1, 1, &section, &branch, &object) &&
                        section == 0 && branch == 0,
                    "wraps once when advancing beyond the final section");
    ign_free_track(&t);

    IgnTrack canada = {0};
    if (!init_track(&canada, 175, 176)) {
        fprintf(stderr, "FAIL allocating Canada route-selection fixture\n");
        ign_free_track(&canada);
        return 1;
    }
    canada.ai_records[171][0][0].valid = 0;
    add_branch_object(&canada, 171, 175);
    failed |= check(ign_track_find_section(&canada, 171, 0, 1, 0, &section, &branch, &object) &&
                        section == 172,
                    "Canada skips invalid records in sections 0xab..0xad");
    failed |= check(ign_track_find_section(&canada, 171, 0, 1, 1, &section, &branch, &object) &&
                        section == 171,
                    "other tracks may accept the invalid record at a branch");
    ign_free_track(&canada);
    return failed;
}
