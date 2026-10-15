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
/* Where the level's TEX is, to read it whole (the model keeps whole pages only). Call before rendering a level. */
void ign_render_set_level_dir(const char *data_dir, const char *name);
/* Static and POS-animated level objects through the Lisa 2 engine (render_scene 0x56410) and rasterizer. */
void ign_render_track(IgnFrame *fb, const IgnLevel *lv, const IgnCamera *cam);
/* Primitive list of the last ign_render_track (struct LisaFrame of lisa.h), for verification tools. */
const struct LisaFrame *ign_render_last_frame(void);
/* Places the car's engine objects (body, wheels, shadow) for this frame. */
void ign_render_car(IgnFrame *fb, const IgnLevel *lv, const IgnCar *car, const IgnCamera *cam);
/* render_scene and the rasterizer: draws everything given since the last flush, far to near. */
void ign_render_flush(IgnFrame *fb);
/* Skid marks of sub_0003731c; call after the track and before the cars. */
void ign_render_skids(IgnFrame *fb, const IgnLevel *lv, const IgnSkids *s, const IgnCamera *cam);
/* Effects of the 0x1d5f10 slots: debris and splash polygons, then the sprites far to near. Call last. */
void ign_render_fx(IgnFrame *fb, const IgnLevel *lv, const IgnFx *fx, IgnCar *const *cars, int ncars,
                   const IgnCamera *cam);
/* Rain lines or snow sprites of the weather tiles (sub_000403c8). */
void ign_render_weather(IgnFrame *fb, const IgnLevel *lv, const IgnWeather *w, const IgnCamera *cam);

#endif
