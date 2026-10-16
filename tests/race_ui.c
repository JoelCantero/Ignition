#include "frontend/race_ui.h"
#include "frontend/gfx2d.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static uint64_t hash_bytes(const uint8_t *data, size_t size) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < size; i++) hash = (hash ^ data[i]) * UINT64_C(1099511628211);
    return hash;
}

static void check(int condition, const char *name) {
    if (!condition) fprintf(stderr, "FAIL: %s\n", name), failures++;
}

static void keys(void) {
    IgnRaceUi ui = {.ncars = 4, .mode = 1};
    int music;
    check(ign_race_ui_key(&ui, IGN_UI_ESCAPE, &music) == IGN_FLOW_STAY && ui.page == IGN_RACE_UI_PAUSE,
          "escape pauses rather than leaving the race");
    ign_race_ui_key(&ui, IGN_UI_UP, &music);
    check(ui.selection == 3, "pause wraps upwards");
    ign_race_ui_key(&ui, IGN_UI_LEFT, &music);
    check(music == -1, "CD line steps music");
    ign_race_ui_key(&ui, IGN_UI_RIGHT, &music);
    check(music == 1, "CD line steps music forwards");
    ign_race_ui_key(&ui, IGN_UI_DOWN, &music);
    ign_race_ui_key(&ui, IGN_UI_DOWN, &music);
    ign_race_ui_key(&ui, IGN_UI_ENTER, &music);
    check(ui.page == IGN_RACE_UI_RESTART, "restart asks for confirmation");
    ign_race_ui_key(&ui, IGN_UI_NO, &music);
    check(ui.page == IGN_RACE_UI_PAUSE, "no cancels confirmation");
    ign_race_ui_key(&ui, IGN_UI_DOWN, &music);
    ign_race_ui_key(&ui, IGN_UI_ENTER, &music);
    check(ui.page == IGN_RACE_UI_QUIT, "quit asks for confirmation");
    check(ign_race_ui_key(&ui, IGN_UI_YES, &music) == IGN_FLOW_MENU, "yes returns to the menu");
    ui = (IgnRaceUi){.page = IGN_RACE_UI_RESULTS, .mode = 1};
    check(ign_race_ui_key(&ui, IGN_UI_YES, &music) == IGN_FLOW_RESTART, "single race can replay");
    ui = (IgnRaceUi){.page = IGN_RACE_UI_RESTART, .mode = 0, .retries = 0};
    check(ign_race_ui_key(&ui, IGN_UI_YES, &music) == IGN_FLOW_STAY, "championship retry limit");
}

static void results(void) {
    IgnCar a = {.out = 1, .place = 2}, b = {.out = 1, .place = 1}, c = {.place = 0};
    const IgnCar *cars[] = {&a, &b, &c};
    IgnRaceUi ui = {.ncars = 3, .mode = 0, .total = {10, 7, 5}, .round = 1, .retries = 3};
    ign_race_ui_update(&ui, cars);
    check(ui.page == IGN_RACE_UI_RESULTS && !ui.committed, "human finish opens results before AI finish");
    check(ui.order[0] == 1 && ui.order[1] == 0 && ui.order[2] == 2, "finish order precedes unplaced AI");
    check(ui.points[0] == 7 && ui.points[1] == 10 && ui.points[2] == 0, "original points");
    check(ui.standings[0] == 0 && ui.standings[1] == 1, "tied totals preserve participant order");
    c.out = 1, c.place = 3;
    ign_race_ui_update(&ui, cars);
    check(ui.committed && ui.qualified && ui.total[0] == 17 && ui.total[1] == 17 && ui.total[2] == 10,
          "final results commit points exactly once");
    ign_race_ui_update(&ui, cars);
    check(ui.total[0] == 17, "redraw/update cannot award points twice");
    int music;
    ign_race_ui_key(&ui, IGN_UI_ENTER, &music);
    check(ui.page == IGN_RACE_UI_TRACK_SCORE, "results to track score");
    ign_race_ui_key(&ui, IGN_UI_ENTER, &music);
    check(ui.page == IGN_RACE_UI_TOTAL_SCORE, "track score to total score");
    ign_race_ui_key(&ui, IGN_UI_ENTER, &music);
    check(ui.page == IGN_RACE_UI_PODIUM, "total score to podium");
    check(ign_race_ui_key(&ui, IGN_UI_ENTER, &music) == IGN_FLOW_ADVANCE, "qualified advances");
    ui = (IgnRaceUi){.ncars = 3, .mode = 0, .round = 4, .klass = 0};
    ign_race_ui_update(&ui, cars);
    check(ui.complete, "last championship round");
    a.place = 4;
    ui = (IgnRaceUi){.ncars = 3, .mode = 0};
    ign_race_ui_update(&ui, cars);
    check(!ui.qualified, "fourth place does not qualify");
    int p[] = {5, 10, 10, -1}, order[4];
    ign_race_score_order(p, 4, order);
    check(order[0] == 1 && order[1] == 2 && order[2] == 0 && order[3] == 3, "stable total score sort");
    check(ign_race_points(0) == 0 && ign_race_points(7) == 0 && ign_race_points(6) == 1, "points bounds");
    check(ign_champ_track(0, 4) == 4 && ign_champ_track(3, 6) == 3, "original track sequence");
}

static void notifications(void) {
    IgnRaceUi ui = {.ncars = 3, .language = 3};
    IgnHud hud = {0};
    IgnCar car = {.progress_lock = 1};
    check(ign_race_ui_notify(&ui, &hud, 100, 0, NULL, NULL) == 1 &&
          ign_race_ui_notify(&ui, &hud, 100, 0, NULL, NULL) == 0, "host quit deduplication");
    check(!strcmp(hud.msg[0].text, "EL ANFITRION HA ABANDONADO EL JUEGO"), "original Spanish host message");
    check(ign_race_ui_notify(&ui, &hud, 101, 0, NULL, NULL) == 1, "host restart uses independent latch");
    check(ign_race_ui_notify(&ui, &hud, 102, 2, "CPU", &car) == 1 && car.out && !car.progress_lock,
          "participant quit marks car out and clears progress lock");
    check(hud.msg[2].duration == 80 && hud.msg[2].period == 8 && hud.msg[2].viewport == -1,
          "notification lifetime blink and viewport");
    check(ign_race_ui_notify(&ui, &hud, 102, 2, "CPU", &car) == 0, "participant notification deduplication");
}

static void timing(void) {
    IgnFrameTime time = {0};
    check(ign_frame_time_sample(&time, 10, 0) == 0.36 && time.frames == 0, "sub-frame sample not committed");
    double delta = ign_frame_time_sample(&time, 28, 0);
    check(fabs(delta - 1.008) < 1e-14 && time.frames == 1 && time.fps == 35 && time.elapsed == 0.028,
          "committed 36Hz frame sample");
    ign_frame_time_sample(&time, 56, 1);
    check(time.origin == 28 && time.elapsed == 0.028, "pause advances clock origin not elapsed");
    check(ign_frame_time_sample(&time, 1000, 0) == 10.8 && time.frames == 3, "long frame cap");
    check(ign_bios_ticks(86400000) == 0 && ign_bios_ticks(43200000) == 786520, "daily BIOS tick rollover");
}

static void assets(const char *dir) {
    static IgnFrame fb;
    IgnBtz btz;
    ign_btz_default(&btz);
    IgnCar car[8] = {0};
    const IgnCar *cars[8];
    for (int i = 0; i < 8; i++) {
        car[i].out = 1;
        car[i].place = i + 1;
        car[i].race_elapsed = 123.45 + i;
        cars[i] = &car[i];
    }
    for (int language = 0; language < 6; language++) {
        ign_btz_set_i32(&btz, IGN_BTZ_LANGUAGE, language);
        IgnRaceUi ui;
        if (ign_race_ui_init(&ui, dir, &btz, 0, 8, NULL)) {
            check(0, "real race UI assets");
            return;
        }
        ign_race_ui_update(&ui, cars);
        IgnHud hud = {0};
        for (int resolution = 0; resolution < 3; resolution++) {
            ign_frame_init(&fb, resolution == 0 ? 320 : resolution == 1 ? 640 : 800,
                           resolution == 0 ? 200 : resolution == 1 ? 480 : 600);
            for (int page = IGN_RACE_UI_PAUSE; page <= IGN_RACE_UI_PODIUM; page++) {
                memset(fb.color, 0, sizeof fb.color);
                ui.page = page;
                ign_race_ui_draw(&ui, &fb, &hud, cars, &btz, 7);
                int pixels = 0;
                for (int i = 0; i < fb.w * fb.h; i++) pixels += fb.color[i] != 0;
                check(pixels > 10, "localised UI page renders visible glyphs in every resolution");
            }
        }
        ign_race_ui_destroy(&ui);
    }
    static const char *const levels[] = {"USA", "CANADA", "AUSTRIA", "BRAZIL", "CARIB", "ICELAND", "JAPAN"};
    char path[1200];
    snprintf(path, sizeof path, "%s/MENUBKG.DAT", dir);
    if (ign_menubox_init(path)) {
        check(0, "menu box resources for loading isolation");
        return;
    }
    ign_menubox_add(12, 0, 70, 0, 0);
    ign_menubox_add(12, 1, 90, 0, 1);
    static uint8_t menu_before[320 * 200], menu_after[320 * 200];
    memset(menu_before, 73, sizeof menu_before);
    ign_menubox_draw(menu_before, 320, 320, 200);
    for (int i = 0; i < 7; i++) {
        IgnRaceLoading loading;
        if (ign_race_loading_init(&loading, dir, levels[i], i % 6)) {
            check(0, "real loading assets for all seven tracks");
            continue;
        }
        ign_frame_init(&fb, 320, 200);
        check(ign_race_loading_draw(&loading, &fb, 0) == 0 &&
              ign_race_loading_draw(&loading, &fb, 100) == 0, "loading endpoints");
        check(ign_race_loading_draw(&loading, &fb, 101) == -1, "reject invalid progress");
        check(ign_race_loading_draw(&loading, &fb, -1) == -1, "reject negative progress");
        uint64_t before = hash_bytes(loading.frame->color, 320 * 200);
        check(ign_race_loading_draw(&loading, &fb, 101) == -1 &&
              before == hash_bytes(loading.frame->color, 320 * 200), "invalid progress preserves the screen");
        uint8_t guarded[16 * 16 + 32];
        memset(guarded, 0xad, sizeof guarded);
        IgnMenuBox edge = {-8, 20, 0, 0, 1, NULL};
        ign_menubox_draw_list(guarded + 16, 16, 16, 16, loading.boxes, &edge);
        int intact = 1;
        for (int g = 0; g < 16; g++)
            if (guarded[g] != 0xad || guarded[16 * 16 + 16 + g] != 0xad) intact = 0;
        check(intact, "linear-pitch box overhang never writes outside the caller buffer");
        for (int resolution = 0; resolution < 3; resolution++) {
            ign_frame_init(&fb, resolution == 0 ? 320 : resolution == 1 ? 640 : 800,
                           resolution == 0 ? 200 : resolution == 1 ? 480 : 600);
            check(ign_race_loading_draw(&loading, &fb, 33) == 0, "loading scales to each supported resolution");
            int equal = 1;
            for (int y = 0; y < fb.h; y++)
                for (int x = 0; x < fb.w; x++)
                    if (fb.color[y * fb.w + x] != loading.frame->color[(y * 200 / fb.h) * 320 + x * 320 / fb.w])
                        equal = 0;
            check(equal, "loading scales the completed indexed frame, not individual UI elements");
        }
        ign_race_loading_destroy(&loading);
        ign_race_loading_destroy(&loading);
        memset(menu_after, 73, sizeof menu_after);
        ign_menubox_draw(menu_after, 320, 320, 200);
        check(!memcmp(menu_before, menu_after, sizeof menu_before), "loading preserves live menu box ownership and clipping");
    }
    ign_menubox_free();
    IgnRaceLoading missing;
    check(ign_race_loading_init(&missing, "/nonexistent/ignition-test-data", "USA", 0) == -1 &&
          !missing.frame && !missing.overlay && !missing.boxes && !missing.pan && !missing.font.data &&
          !missing.blue.data && !missing.system.pixels, "loading failure releases all partial owners");
    ign_race_loading_destroy(&missing);
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--loading")) {
        static IgnFrame frame;
        static const char *const levels[] = {"CANADA", "USA", "CARIB", "BRAZIL", "AUSTRIA", "ICELAND", "JAPAN"};
        ign_frame_init(&frame, 320, 200);
        for (int language = 0; language < 6; language++)
            for (int track = 0; track < 7; track++) {
                IgnRaceLoading loading;
                if (ign_race_loading_init(&loading, argv[2], levels[track], language)) return 1;
                printf("%d %d %016llx %016llx", language, track,
                       (unsigned long long)hash_bytes(loading.frame->color, 320 * 200),
                       (unsigned long long)hash_bytes(loading.overlay, 320 * 480));
                for (int p = 0; p <= 101; p++) {
                    if (ign_race_loading_draw(&loading, &frame, p <= 100 ? p : 0)) return 1;
                    printf(" %016llx", (unsigned long long)hash_bytes(frame.color, 320 * 200));
                }
                putchar('\n');
                ign_race_loading_destroy(&loading);
            }
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--qualified")) {
        int place0, place1, players;
        while (scanf("%d %d %d", &place0, &place1, &players) == 3)
            printf("%d\n", ign_champ_qualified(place0, place1, players));
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--frame")) {
        int32_t now, origin;
        int pause;
        double previous;
        while (scanf("%lf %d %d %d", &previous, &origin, &now, &pause) == 4) {
            IgnFrameTime time = {.previous = previous, .origin = origin};
            double delta = ign_frame_time_sample(&time, now, pause);
            printf("%.17g %.17g %d %d %llu\n", delta, time.elapsed, time.origin, time.fps,
                   (unsigned long long)time.frames);
        }
        return 0;
    }
    keys();
    results();
    notifications();
    timing();
    if (argc > 1) assets(argv[1]);
    printf("race UI: %d failures\n", failures);
    return failures ? 1 : 0;
}
