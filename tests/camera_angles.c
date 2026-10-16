#include <stdio.h>
#include <string.h>

#include "render/lisa.h"

static int runner(void) {
    LisaView view;
    while (1) {
        memset(&view, 0, sizeof view);
        int count = scanf("%lf %lf %lf %d %d %d %d %lf", &view.x, &view.y, &view.z,
                          &view.target[0], &view.target[1], &view.target[2], &view.target_roll, &view.yaw);
        if (count == EOF && !ferror(stdin)) return 0;
        if (count != 8) {
            fprintf(stderr, "Invalid look-at camera case\n");
            return 1;
        }
        lisa_camera_angles(&view);
        printf("%.17g %.17g %.17g\n", view.pitch, view.yaw, view.roll);
    }
}

static int self_test(void) {
    static LisaFrame frame;
    LisaScene scene = {0};
    LisaView view = {.look_at = 1, .target = {0, 100, 0}, .target_roll = 72, .yaw = 123.5};
    lisa_build(&scene, &view, &frame);
    if (view.pitch != 2700 || view.yaw != 123.5 || view.roll != 72) return 1;
    view.look_at = 0;
    view.pitch = 17;
    lisa_build(&scene, &view, &frame);
    if (view.pitch != 17 || view.yaw != 123.5 || view.roll != 72) return 1;
    view.look_at = 1;
    view.target[0] = 1;
    view.target[1] = 0;
    lisa_build(&scene, &view, &frame);
    if (view.pitch != 0 || view.yaw != 900 || view.roll != 72) return 1;
    static const int32_t mesh[] = {1, 1, 0, 0, 0, 1, 0, 0};
    if (lisa_scene_init(&scene, 51200, 51200, 1)) return 1;
    int id = lisa_create(&scene, mesh, NULL, 0, 0, 0);
    if (id < 0) {
        lisa_scene_free(&scene);
        return 1;
    }
    int16_t angles[3] = {0};
    lisa_place(&scene, id, 25600, 0, 29600, angles);
    view = (LisaView){.x = 25600, .z = 25600, .yaw = 900, .zoom = 1,
                      .fx = 224, .fy = 224, .near = 25, .cx = 160, .cy = 100,
                      .layers = LISA_LAYER_OBJECTS, .reach = 25, .look_at = 1, .target = {25600, 0, 29600}};
    lisa_view_clip(&view, 0, 0, 319, 199);
    lisa_build(&scene, &view, &frame);
    int visible = frame.nvis == 1 && view.yaw == 0;
    view.look_at = 0;
    view.yaw = 900;
    lisa_build(&scene, &view, &frame);
    int hidden = frame.nvis == 0 && view.yaw == 900;
    lisa_scene_free(&scene);
    if (!visible || !hidden) return 1;
    puts("Look-at camera and render-scene flag passed");
    return 0;
}

int main(int argc, char **argv) {
    int result = argc == 2 && strcmp(argv[1], "--runner") == 0 ? runner() : self_test();
    if (result) fprintf(stderr, "Look-at camera test failed\n");
    return result;
}
