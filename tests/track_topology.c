#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/track.h"
#include "render/lisa.h"

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
                 check(track.section_next_obj[1] == 3,
                       "route neighbor selection uses integer-recentered PLC origins");
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
                 check(track.section[3] == -3 && track.section_b[3] == -3,
                       "branch B overwrites the shared fork's signed object mapping") |
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

static int test_recenter_consumers(void) {
    int32_t mesh[22] = {3, 1, 2000, 600, 2000, 2100, 600, 2000, 2000, 600, 2100};
    IgnFace *face = (IgnFace *)(mesh + 11);
    face->v[1] = 1;
    face->v[2] = 2;
    uint8_t plc[44] = {0};
    IgnObject objects[2];
    set_object(&objects[0], 2, 0, -1000, 0);
    objects[0].nv = 3;
    objects[0].nf = 1;
    objects[0].verts = mesh + 2;
    objects[0].faces = face;
    set_object(&objects[1], 300, 100, -200, 300);
    objects[1].rotation[1] = 900;
    IgnLevel level = {.track = {.nobj = 2, .objs = objects, .msh = (uint8_t *)mesh, .plc = plc},
                      .wall_height = 350};
    int32_t points[3][3];
    float vertices[3][3];
    for (int v = 0; v < 3; v++) {
        ign_object_point(objects, v, points[v]);
        ign_object_vertex(objects, v, vertices[v]);
    }
    IgnWall walls[8];
    int failed = check(ign_collect_walls(&level, 2033, 400, 2033, 0, walls, 8) == 0,
                       "unrecentered origin excludes the distant wall fixture");
    failed |= check(!ign_track_recenter_model(&level.track), "mutable model recentres before placement");
    failed |= check(objects[0].pos[0] == 2033 && objects[0].pos[1] == 400 &&
                        objects[0].pos[2] == 2033,
                    "object origin uses integer means and original recentered Y sign");
    int32_t raw[3];
    memcpy(raw, plc + 12, sizeof raw);
    failed |= check(raw[0] == 2033 && raw[1] == -400 && raw[2] == 2033,
                    "raw PLC and object representation retain opposite Y signs");
    failed |= check(objects[1].pos[0] == 100 && objects[1].pos[1] == -200 &&
                        objects[1].pos[2] == 300 && objects[1].rotation[1] == 900,
                    "class 300 prepass has no net position or angle mutation");
    for (int v = 0; v < 3; v++) {
        int32_t point[3];
        float vertex[3];
        ign_object_point(objects, v, point);
        ign_object_vertex(objects, v, vertex);
        failed |= check(!memcmp(points[v], point, sizeof point) &&
                            !memcmp(vertices[v], vertex, sizeof vertex),
                        "collision and rendered world vertices are preserved");
    }
    int32_t saved[22];
    memcpy(saved, mesh, sizeof saved);
    failed |= check(!ign_track_recenter_model(&level.track) &&
                        !memcmp(saved, mesh, sizeof saved), "normalization runs only once");
    failed |= check(ign_collect_walls(&level, 2033, 400, 2033, 0, walls, 8) == 3,
                    "wall broad phase uses the recentered origin and original Y addition");
    failed |= check(ign_collect_walls(&level, 2033, -400, 2033, 0, walls, 8) == 0,
                    "wall broad phase rejects the opposite height");
    if (ign_engine_place(&level.track)) return 1;
    int cell = ((2033 + IGN_SRF_ORIGIN) / 256) * 201;
    failed |= check(level.track.cell[0] == cell,
                    "physics engine grid is populated from recentered origins");
    failed |= check(!ign_track_recenter_model(&level.track),
                    "already-normalized models remain idempotent after placement");
    IgnModel late = level.track;
    late.track_recentered = 0;
    failed |= check(ign_track_recenter_model(&late) == -1 &&
                        !memcmp(saved, mesh, sizeof saved),
                    "uninitialized normalization refuses an already-placed model");
    free(level.track.cell);
    free(level.track.stamp);

    LisaScene scene;
    if (lisa_scene_init(&scene, 51200, 51200, 2)) return 1;
    int id = lisa_create(&scene, mesh, NULL, 0, 2, 0);
    int16_t angles[3] = {0};
    lisa_place(&scene, id, objects[0].pos[0] + IGN_SRF_ORIGIN, -objects[0].pos[1],
               objects[0].pos[2] + IGN_SRF_ORIGIN, angles);
    failed |= check(scene.objs[id].radius == 803 && scene.grid[cell] == id,
                    "renderer radius and grid use compensated mesh and recentered origin");
    LisaView view = {.x = 27633, .y = -400, .z = 27333, .near = 1,
                     .fy = 100, .cy = 100, .reach = 3, .layers = LISA_LAYER_OBJECTS};
    lisa_view_clip(&view, 0, 0, 319, 199);
    static LisaFrame frame;
    lisa_build(&scene, &view, &frame);
    failed |= check(frame.nvis == 1 && frame.vis[0] == id,
                    "renderer cell collection and sphere culling retain the recentered object");
    lisa_scene_free(&scene);
    return failed;
}

int main(void) {
    return test_origin_distance() | test_parallel_branch_route() |
           test_branch_b_merge_continuation() | test_recenter_consumers();
}
