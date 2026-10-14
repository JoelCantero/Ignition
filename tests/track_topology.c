#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game/track.h"

static int check(int condition, const char *message) {
    if (condition) return 0;
    fprintf(stderr, "FAIL %s\n", message);
    return 1;
}

static void set_object(IgnObject *object, int type, int x, int y, int z) {
    memset(object, 0, sizeof *object);
    object->type = (uint32_t)type;
    object->pos[0] = x;
    object->pos[1] = y;
    object->pos[2] = z;
}

static int test_origin_distance(void) {
    IgnFace faces[2] = {0};
    int32_t verts[18] = {
        90, 0, 0, 90, 0, 0, 90, 0, 0,
        -99, 0, 0, -99, 0, 0, -99, 0, 0,
    };
    IgnObject objects[4];
    set_object(&objects[0], 0, 0, 0, 0);
    set_object(&objects[1], 1, 0, 0, 0);
    set_object(&objects[2], 2, 10, 0, 0);
    set_object(&objects[3], 9, 100, 0, 0);
    for (int i = 2; i <= 3; i++) {
        objects[i].nv = 3;
        objects[i].nf = 1;
        objects[i].verts = verts + (i - 2) * 9;
        objects[i].faces = &faces[i - 2];
    }
    IgnModel model = {.nobj = 4, .objs = objects};
    IgnTrack track = {0};
    if (ign_build_track(&model, &track)) return 1;
    int failed = check(track.center[2][0] > track.center[3][0],
                       "fixture centroids prefer object 3 over object 2") |
                 check(track.section_next_obj[1] == 2,
                       "route neighbor selection uses PLC origins, not centroids");
    ign_free_track(&track);
    return failed;
}

static int test_parallel_branch_route(void) {
    IgnObject objects[10];
    set_object(&objects[0], 0, 0, 0, 0);
    set_object(&objects[1], 1, 0, 0, 1);
    set_object(&objects[2], 2, 0, 0, 2);
    set_object(&objects[3], 3, 0, 0, 3);
    set_object(&objects[4], 4, 10, 0, 3);
    set_object(&objects[5], 4, 20, 0, 3);
    set_object(&objects[6], 5, 0, 0, 13);
    set_object(&objects[7], 5, 0, 0, 23);
    set_object(&objects[8], 6, 0, 0, 33);
    set_object(&objects[9], 2, 0, 0, 43);
    IgnModel model = {.nobj = 10, .objs = objects};
    IgnTrack track = {0};
    if (ign_build_track(&model, &track)) return 1;
    IgnLevel level = {.track = model};
    ign_track_gates(&track, &level);

    int failed = check(track.count == 8, "route count includes main and branch-A sections only") |
                 check(track.section[3] == 3 && track.section_b[3] == -3,
                       "shared fork remains on the main route and is also mapped to branch B") |
                 check(track.section_obj[3] == 3 && track.section_obj_b[3] == 3,
                       "fork object is addressable from both route fields") |
                 check(track.section_obj_b[4] == 6 && track.section_obj_b[5] == 7,
                       "branch-B-only objects map to their negative sections") |
                 check(track.section_next_obj[5] == 8 && track.section_next_obj_b[5] == 8,
                       "both branches retain the exact merge object as their next object");

    double angle = 0.0;
    double expected = -atan2((double)objects[7].pos[0] - objects[8].pos[0],
                             (double)objects[7].pos[2] - objects[8].pos[2]);
    if (expected < 0) expected += 2.0 * 3.14159265358979323846;
    failed |= check(ign_track_section_angle(&track, &level, -5, &angle) &&
                        fabs(angle - expected) < 1e-12,
                    "branch-B angle uses its selected merge rather than a nearest-merge guess");
    ign_free_track(&track);
    return failed;
}

static int test_branch_b_merge_continuation(void) {
    IgnObject objects[12];
    set_object(&objects[0], 0, 0, 0, 0);
    set_object(&objects[1], 1, 0, 0, 1);
    set_object(&objects[2], 2, 0, 0, 2);
    set_object(&objects[3], 3, 0, 0, 3);
    set_object(&objects[4], 4, 10, 0, 3);
    set_object(&objects[5], 4, 20, 0, 3);
    set_object(&objects[6], 5, 0, 0, 13);
    set_object(&objects[7], 5, 0, 0, 23);
    set_object(&objects[8], 6, 21, 0, 3);
    set_object(&objects[9], 6, 0, 0, 24);
    set_object(&objects[10], 2, 0, 0, 34);
    set_object(&objects[11], 2, 0, 0, 44);
    IgnModel model = {.nobj = 12, .objs = objects};
    IgnTrack track = {0};
    if (ign_build_track(&model, &track)) return 1;
    IgnLevel level = {.track = model};
    ign_track_gates(&track, &level);

    int failed = check(track.section_next_obj[5] == 8,
                       "branch A retains its own merge object") |
                 check(track.section_next_obj_b[5] == 9,
                       "branch B retains its different merge object") |
                 check(track.section_obj[6] == 9 && track.section_next_obj[6] == 10,
                       "main route continues from branch B's merge");
    ign_free_track(&track);
    return failed;
}

int main(void) {
    return test_origin_distance() | test_parallel_branch_route() |
           test_branch_b_merge_continuation();
}
