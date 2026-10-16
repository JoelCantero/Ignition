#ifndef IGN_RACE_UI_H
#define IGN_RACE_UI_H

#include "../game/race.h"
#include "../game/save.h"
#include "../render/hud.h"

typedef enum {
    IGN_RACE_UI_PLAY, IGN_RACE_UI_PAUSE, IGN_RACE_UI_QUIT, IGN_RACE_UI_RESTART,
    IGN_RACE_UI_RESULTS, IGN_RACE_UI_TRACK_SCORE, IGN_RACE_UI_TOTAL_SCORE, IGN_RACE_UI_PODIUM
} IgnRaceUiPage;

enum { IGN_FLOW_STAY, IGN_FLOW_MENU, IGN_FLOW_RESTART, IGN_FLOW_ADVANCE };
enum { IGN_UI_ESCAPE, IGN_UI_ENTER, IGN_UI_UP, IGN_UI_DOWN, IGN_UI_YES, IGN_UI_NO, IGN_UI_LEFT, IGN_UI_RIGHT };

typedef struct {
    IgnRaceUiPage page, return_page;
    int selection, action, language, mode, ncars, round, klass;
    int qualified, complete, order[8], points[8], total[8], standings[8];
    int committed, retries;
    unsigned notifications;
    IgnFont normal, selected, small;
    IgnPic trophy;
} IgnRaceUi;

/* Native layout of 0x4395c/0x43e48/0x43fe0/0x47ae0; resources are shared across all pages. */
int ign_race_ui_init(IgnRaceUi *ui, const char *data_dir, const IgnBtz *btz, int mode, int ncars,
                     const int previous_points[8]);
void ign_race_ui_destroy(IgnRaceUi *ui);
void ign_race_ui_update(IgnRaceUi *ui, const IgnCar *const *cars);
/* Returns an IGN_FLOW_* action; music_delta is -1/+1 only on the CD TRACK line. */
int ign_race_ui_key(IgnRaceUi *ui, int key, int *music_delta);
void ign_race_ui_draw(IgnRaceUi *ui, IgnFrame *frame, const IgnHud *hud, const IgnCar *const *cars,
                      const IgnBtz *btz, int music_track);
/* 0x21448 notification deduplication; status 100 host quit, 101 restart, 102 participant quit. */
int ign_race_ui_notify(IgnRaceUi *ui, IgnHud *hud, int status, int car, const char *name, IgnCar *participant);
int ign_race_points(int place);
/* Stable descending order, preserving the initial car order on equal scores (0x44f73). */
void ign_race_score_order(const int *points, int n, int *order);

typedef struct {
    double previous, elapsed;
    int32_t origin;
    int fps;
    int64_t fps_sum;
    uint64_t frames;
} IgnFrameTime;

/* sub_0002b734: delta in 1/36 s, >=1 commits the sample; cap 10.8, pause shifts the race origin. */
double ign_frame_time_sample(IgnFrameTime *time, int32_t now, int paused);
/* BIOS int 1Ah replacement, modulo the original daily tick count. */
uint32_t ign_bios_ticks(uint64_t milliseconds);

#endif
