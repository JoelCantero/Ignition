#ifndef IGN_SOUND_H
#define IGN_SOUND_H

#include "../game/assets.h"
#include "../game/car.h"
#include "../game/fx.h"
#include "../game/race.h"
#include "../game/weather.h"

/* Opens the audio device and starts the looping sounds of sub_0002a4ec / sub_0002489c / sub_00026154. */
int ign_sound_start(const char *data_dir, const char *level, const IgnLevel *lv, IgnCar *const *cars, int ncars,
                    const IgnWeather *w);
void ign_sound_stop(void);
/* Once per physics tick: wall and landing knocks of cars_update_all / car_vertical. */
void ign_sound_tick(IgnCar *const *cars, int ncars, IgnFx *fx);
/* Once per 1/36 s frame: engines (sub_00050824), rolling (sub_00050464), skids (sub_0003731c), ambience
 * (sub_000360f0), weather loops, countdown, gears and turbo, then the one-shots queued in `fx`. */
void ign_sound_frame(IgnCar *const *cars, int ncars, int view, const IgnRace *race, double finish_clock,
                     const IgnWeather *w, IgnFx *fx);

#endif
