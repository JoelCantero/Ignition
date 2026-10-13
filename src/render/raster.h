#ifndef IGN_RASTER_H
#define IGN_RASTER_H

#include <stdint.h>

#include "../game/assets.h"
#include "../game/car.h"
#include "../game/fx.h"
#include "../game/skid.h"
#include "../game/weather.h"

/* Video modes of the original: 320x200 and 640x480 (VESA) in DOS, also 800x600 in Ign_win.exe. */
#define IGN_MAX_W 800
#define IGN_MAX_H 600

typedef struct {
    int w, h;
    float fx, fy;    /* projection scale (race_setup_video 0x492fc: camera +0x58/+0x5c) */
    int perspective; /* PERSP. POLY option; off by default, the original maps textures affinely */
    uint8_t color[IGN_MAX_W * IGN_MAX_H];
    float inv_z[IGN_MAX_W * IGN_MAX_H];
} IgnFrame;

typedef struct {
    float x, y, z; /* world position, y down */
    float yaw, pitch; /* radians */
} IgnCamera;

/* Sets the resolution (clamped to IGN_MAX_W x IGN_MAX_H); every mode is shown at 4:3. */
void ign_frame_init(IgnFrame *fb, int w, int h);
void ign_frame_clear(IgnFrame *fb);
void ign_render_track(IgnFrame *fb, const IgnLevel *lv, const IgnCamera *cam);
void ign_render_car(IgnFrame *fb, const IgnLevel *lv, const IgnCar *car, const IgnCamera *cam);
/* Skid marks of sub_0003731c; call after the track and before the cars. */
void ign_render_skids(IgnFrame *fb, const IgnLevel *lv, const IgnSkids *s, const IgnCamera *cam);
/* Effects of the 0x1d5f10 slots: debris and splash polygons, then the sprites far to near. Call last. */
void ign_render_fx(IgnFrame *fb, const IgnLevel *lv, const IgnFx *fx, IgnCar *const *cars, int ncars,
                   const IgnCamera *cam);
/* Rain lines or snow sprites of the weather tiles (sub_000403c8). */
void ign_render_weather(IgnFrame *fb, const IgnLevel *lv, const IgnWeather *w, const IgnCamera *cam);

#endif
