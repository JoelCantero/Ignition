#ifndef IGN_RASTER_H
#define IGN_RASTER_H

#include <stdint.h>

#include "../game/assets.h"
#include "../game/car.h"

#define IGN_SCREEN_W 320
#define IGN_SCREEN_H 200

typedef struct {
    uint8_t color[IGN_SCREEN_W * IGN_SCREEN_H];
    float inv_z[IGN_SCREEN_W * IGN_SCREEN_H];
} IgnFrame;

typedef struct {
    float x, y, z; /* world position, y down */
    float yaw, pitch; /* radians */
} IgnCamera;

void ign_frame_clear(IgnFrame *fb);
void ign_render_track(IgnFrame *fb, const IgnLevel *lv, const IgnCamera *cam);
void ign_render_car(IgnFrame *fb, const IgnLevel *lv, const IgnCar *car, const IgnCamera *cam);

#endif
