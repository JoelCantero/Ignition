#ifndef IGN_HUD_H
#define IGN_HUD_H

#include "../game/assets.h"
#include "../game/race.h"
#include "raster.h"

typedef struct {
    uint8_t *data;
    size_t size;
    uint16_t height;
    uint32_t offset[224];
    uint16_t width[224];
} IgnHudFont;

typedef struct {
    IgnPic panel;
    uint8_t palette_map[256];
    IgnHudFont yellow;
    IgnHudFont speed;
    IgnHudFont pos;
    double lap_start;
    double last_lap;
    int observed_lap;
    int race_started;
} IgnHud;

int ign_hud_init(IgnHud *hud, const char *data_dir, const IgnPalette *palette);
void ign_hud_destroy(IgnHud *hud);
void ign_hud_draw(IgnHud *hud, IgnFrame *frame, const IgnCar *car, const IgnRace *race, int car_count);

#endif
