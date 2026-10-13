#include "weather.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define ENGINE_ORIGIN 25600

void ign_weather_init(IgnWeather *w, const char *level) {
    memset(w, 0, sizeof *w);
    /* sub_00026154: track 0 rains, track 4 snows, track 1 only has the wind sound. */
    if (!strcmp(level, "CANADA")) w->kind = IGN_WEATHER_RAIN;
    else if (!strcmp(level, "AUSTRIA")) w->kind = IGN_WEATHER_SNOW;
    else if (!strcmp(level, "USA")) w->kind = IGN_WEATHER_WIND;
}

void ign_weather_zone(IgnWeather *w, const IgnCar *player) {
    int s = player->section, a = abs(s);
    switch (w->kind) {
    case IGN_WEATHER_RAIN: w->active = s > 0x2d && s < 0x78; break;
    case IGN_WEATHER_WIND: w->active = a > 0 && a < 0x1e; break;
    case IGN_WEATHER_SNOW: w->active = a > 0x1c && a < 0x64; break;
    default: break;
    }
}

static void tint(IgnFx *fx, int r, int g, int b, double frac) {
    fx->tint[0] = r, fx->tint[1] = g, fx->tint[2] = b;
    fx->tint_frac = frac;
}

/* 0x40516: one more drop pair (rain: lines (x, -y)-(x+4, -y+60) and 800 higher; snow: two flakes 800 apart). */
static void grow(IgnWeather *w, IgnFx *fx) {
    int pairs = w->nfaces / 2;
    if (w->kind == IGN_WEATHER_RAIN) tint(fx, 0x22, 0x22, 0x31, pairs * 0.02 * 0.5);
    int x = (int)(ign_fx_rand(fx) * 1000.0), y = (int)(ign_fx_rand(fx) * 800.0), z = (int)(ign_fx_rand(fx) * 1000.0);
    int32_t(*v)[3] = w->v + w->nverts;
    if (w->kind == IGN_WEATHER_RAIN) {
        const int32_t r[4][3] = {{x, -y, z}, {x + 4, -y + 0x3c, z}, {x, -y - 0x320, z}, {x + 4, -y - 0x2e4, z}};
        memcpy(v, r, sizeof r);
        w->nverts += 4;
    } else {
        const int32_t s[2][3] = {{x, -y, z}, {x, -y - 0x320, z}};
        memcpy(v, s, sizeof s);
        w->nverts += 2;
    }
    w->nfaces += 2;
}

static void shrink(IgnWeather *w, IgnFx *fx) {
    w->nverts -= w->kind == IGN_WEATHER_RAIN ? 4 : 2;
    w->nfaces -= 2;
    if (w->kind == IGN_WEATHER_RAIN) tint(fx, 0x22, 0x22, 0x31, w->nfaces / 2 * 0.02 * 0.5);
}

void ign_weather_frame(IgnWeather *w, IgnFx *fx, const IgnCar *view, int view_index, const IgnTrack *t,
                       const IgnLevel *lv, double level_clock, double finish_clock, float focus_x, float focus_z) {
    (void)lv;
    if (w->kind != IGN_WEATHER_RAIN && w->kind != IGN_WEATHER_SNOW) {
        /* 0x40e37: the wind fades in and out with the zone. */
        if (w->kind == IGN_WEATHER_WIND) w->volume = w->active ? (w->volume < 0x7530 ? w->volume + 0x9c4 : w->volume)
                                                               : (w->volume > 0x9c4 ? w->volume - 0x9c4 : 0);
        return;
    }
    /* 0x403e5: two seconds after the player's finish the weather stops. */
    if (finish_clock >= 120.0) {
        w->nverts = w->nfaces = 0;
        w->volume = 0;
        return;
    }
    int racing = finish_clock < 0;
    if (w->mark + 2 < level_clock) {
        if (w->active && racing && w->nfaces < 0x64) grow(w, fx);
        else if (!(w->active && racing) && w->nfaces > 0) shrink(w, fx);
    }

    if (view->step_x != 0 || view->step_z != 0) w->motion = atan2(view->step_z, view->step_x);
    if (w->kind == IGN_WEATHER_RAIN && w->nverts > 0) {
        /* 0x40911: rain falls 25 and drifts 5 per frame. */
        w->wind += 5;
        w->fall -= 0x19;
        if ((int)(ign_fx_rand(fx) * 100.0) == 0 && racing) /* 0x40937: level sound 1 at the rear axle */
            ign_fx_sound(fx, 1, 0, 1, 0x10000, 0, view->x + (float)(cos(view->body.heading) * view->rear),
                         view->z + (float)(sin(view->body.heading) * view->rear));
    } else if (w->kind == IGN_WEATHER_SNOW && w->nverts > 0) {
        /* 0x409ca: snow sways sin(phase) * 20 and falls 5; the upper flake of each pair trails behind the car. */
        w->phase += 0.11781;
        if (w->phase > 2 * PI) w->phase += -2 * PI;
        w->wind = (int)(sin(w->phase) * 20.0);
        w->fall -= 5;
        double s = view->kmh * (1.0 / 3.0) + 3.0;
        int dx = (int)(-cos(w->motion) * s), dz = (int)(-sin(w->motion) * s);
        for (int k = 0; k + 1 < w->nverts; k += 2) {
            w->v[k + 1][0] = w->v[k][0] + dx;
            w->v[k + 1][2] = w->v[k][2] + dz;
        }
    }
    if (w->fall < -0x320) w->wind = 0, w->fall += 0x320;

    /* 0x40b26: 3 x 3 tiles of 1000 around the viewed car's 1000-unit cell (engine coordinates), 200 below it. */
    int ex = (int)((view->x + ENGINE_ORIGIN) * 0.001) * 1000, ez = (int)((view->z + ENGINE_ORIGIN) * 0.001) * 1000;
    w->tile_x = ex - 1000 + w->wind - ENGINE_ORIGIN;
    w->tile_y = (int32_t)(view->height - 200.0) + w->fall;
    w->tile_z = ez - 1000 - ENGINE_ORIGIN;

    if (w->mark + 2 < level_clock) w->mark = trunc(level_clock);
    if (w->kind == IGN_WEATHER_RAIN)
        w->volume = w->active ? (w->volume < 0x7530 ? w->volume + 0x9c4 : w->volume)
                              : (w->volume > 0x9c4 ? w->volume - 0x9c4 : 0);

    /* 0x40c4f: in the rain, 1 in 200 frames a bolt hits a road object up to 3 sections ahead. */
    if (w->kind == IGN_WEATHER_RAIN && w->active && (int)(ign_fx_rand(fx) * 200.0) == 0) {
        int ahead = (int)(ign_fx_rand(fx) * 4.0), s = abs(view->section) + ahead;
        if (view->section == IGN_NO_SECTION || s >= t->count) s = ahead;
        int o = ign_track_section_object(t, s);
        if (o < 0 || !lv) return;
        const IgnObject *obj = &lv->track.objs[o];
        int32_t x = (int32_t)((ign_fx_rand(fx) * 200.0 + obj->pos[0] - 100.0) * 1024.0);
        int32_t z = (int32_t)((ign_fx_rand(fx) * 200.0 + obj->pos[2] - 100.0) * 1024.0);
        int32_t b[IGN_FX_WORDS] = {IGN_FX_LIGHTNING, x, -obj->pos[1] * 1024, z, view_index, 0, 6};
        ign_fx_add(fx, b, focus_x, focus_z);
        ign_fx_sound(fx, 1, 0, 2, 0x10000, 0, view->x + (float)(cos(view->body.heading) * view->rear),
                     view->z + (float)(sin(view->body.heading) * view->rear));
    }
}
