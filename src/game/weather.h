#ifndef IGN_WEATHER_H
#define IGN_WEATHER_H

#include <stdint.h>

#include "assets.h"
#include "car.h"
#include "fx.h"
#include "track.h"

/* Weather of sub_00026154 ([0x1fee8c]): 1 rain (CANADA), 2 snow (AUSTRIA), 3 wind sound only (USA). One camera. */
enum { IGN_WEATHER_NONE = 0, IGN_WEATHER_RAIN = 1, IGN_WEATHER_SNOW = 2, IGN_WEATHER_WIND = 3 };

#define IGN_WEATHER_VERTS 204

typedef struct {
    int kind;
    int active;                         /* [0x1fb4f0]: the player is in the weather zone (sub_000400f4) */
    int nverts, nfaces;                 /* the weather mesh (0x9c40-byte buffer [0x1fef28]) */
    int32_t v[IGN_WEATHER_VERTS][3];    /* mesh vertices, y down, in a 1000 x 1000 tile */
    double phase;                       /* [0x1fb4f8]: snow sway */
    int fall, wind;                     /* [0x1fb508]/[0x1fb510]: tile offset in height and x */
    double mark;                        /* [0x1fedec]: level clock of the last density change */
    double motion;                      /* car+0x100 of the viewed car: angle of its last step */
    int32_t tile_x, tile_y, tile_z;     /* origin of the first of the 3 x 3 tiles (world, height up) */
    int volume;                         /* looped rain / wind sound volume, 0..30000 */
} IgnWeather;

void ign_weather_init(IgnWeather *w, const char *level);
/* sub_000400f4: weather zones by the player's section. */
void ign_weather_zone(IgnWeather *w, const IgnCar *player);
/* sub_000403c8, once per 1/36 s frame: density, drift, tiles around `view` and lightning. `level_clock` is
 * [0x1fb4d8]; `finish_clock` < 0 while the player races ([0x1fb4d0]). */
void ign_weather_frame(IgnWeather *w, IgnFx *fx, const IgnCar *view, int view_index, const IgnTrack *t,
                       const IgnLevel *lv, double level_clock, double finish_clock, float focus_x, float focus_z);

#endif
