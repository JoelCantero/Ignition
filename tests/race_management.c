#include "game/race.h"
#include "game/save.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static double random_value;
static double random01(void) { return random_value; }

static void print_config(const IgnRaceConfig *c) {
    printf("%d %d %d %d %d %d %d %d %d %d %d %d",
           c->mode, c->track, c->round, c->difficulty, c->ncars, c->players_minus_one,
           c->human_count, c->ghost, c->music_track, c->music_volume, c->effects_volume, c->effects_enabled);
    printf(" %d %d %d %d %d %d %d %d", c->value_107, c->value_10b, c->value_197,
           c->value_15b, c->value_10f, c->value_113, c->value_117, c->value_014);
    for (int i = 0; i < c->ncars; i++) {
        const IgnRaceParticipant *p = &c->participants[i];
        printf(" %d %d %d %d %d %d %d", c->grid[i], p->model, p->automatic,
               p->computer, p->gear_delay, p->input_device, p->joystick);
        for (int k = 0; k < 8; k++) printf(" %u", p->keys[k]);
        for (int k = 0; k < 15; k++) printf(" %u", (unsigned char)p->name[k]);
    }
    putchar('\n');
}

static void unit_tests(void) {
    IgnCar c[3] = {{0}};
    IgnCar *cars[] = {c, c + 1, c + 2};
    IgnRace r;
    ign_race_init(&r, 0, 3);
    r.total = 250;
    r.section_count = 100;
    c[0].section = 30;
    c[1].section = -10;
    c[2].section = 10;
    ign_race_rank(&r, cars, 3);
    assert(c[0].position == 3 && c[1].position == 1 && c[2].position == 2);
    c[0].phase = 1;
    ign_race_rank(&r, cars, 3);
    assert(c[0].position == 1);
    r.total = 249;
    r.grid[0] = 2; r.grid[1] = 1; r.grid[2] = 0;
    ign_race_rank(&r, cars, 3);
    assert(c[0].position == 3 && c[2].position == 1);
    r.total = 250;
    r.ghost_enabled = 1;
    r.ghost_progress = 400;
    ign_race_rank(&r, cars, 3);
    assert(r.ghost_position == 2 && c[0].position == 1);

    r.ghost_enabled = 0;
    for (int mode = 0; mode < 4; mode++) {
        r.mode = mode;
        ign_race_rank(&r, cars, 3);
        int position = c[0].position;
        c[0].wrecked = 1;
        c[0].wreck_kind = 3;
        c[0].blow = 1;
        ign_race_rank(&r, cars, 3);
        assert(c[0].position == position && c[0].out == 0 && r.finished == 0);
        c[0].wrecked = c[0].wreck_kind = 0;
        c[0].blow = 0;
    }

    for (int i = 0; i < 8; i++) r.points[i] = 10 + i;
    r.best_lap = 15; r.elapsed = 12; r.clock = 99; r.origin_ms = 123;
    ign_race_reset(&r, 0, 3, 0);
    assert(r.points[0] == 0 && r.points[2] == 0 && r.points[3] == 13);
    assert(r.best_lap == 15 && r.elapsed == 12 && r.clock == 99 && r.origin_ms == 123);
    for (int i = 0; i < 10; i++) assert(r.hud_slots[0][i] == 100 && r.hud_slots[1][i] == 100);
    r.points[0] = 50;
    ign_race_reset(&r, 0, 3, 1);
    assert(r.points[0] == 50 && r.finished == 0 && r.remaining == 3 && r.finish_clock == -1);

    memset(c, 0, sizeof c);
    ign_race_init(&r, 3, 2);
    c[0].position = 2; c[1].position = 1; c[1].lap = 1; c[0].blow = -0.0;
    assert(ign_race_step(&r, cars, 2) == 0);
    assert(r.remaining == 1 && r.finished == 2 && r.last_eliminated == 0);
    assert(c[0].blow == 1 && c[0].wreck_kind == 1 && c[1].finish_position == 1);
    assert(r.tv_camera == 3 && r.camera_hold == 100 && r.human_finish[0] == 0);
    assert(ign_race_position((const IgnCar *const *)cars, 2, 0) == 2);
    assert(ign_race_position((const IgnCar *const *)cars, 2, -1) == 0);

    IgnBtz b;
    ign_btz_default(&b);
    IgnRaceConfig config;
    assert(ign_race_config_decode(&config, b.raw, sizeof b.raw, 8, random01) == 0);
    IgnRaceConfig before = config;
    config.mode = 3; config.ncars = 3; config.players_minus_one = 1;
    config.ghost = 1; config.grid[0] = 2;
    ign_race_configure(&r, &config, 191);
    assert(r.mode == 3 && r.remaining == 3 && r.elimination_limit == 2);
    assert(r.section_count == 191 && r.grid[0] == 2 && r.players_minus_one == 1 && r.ghost_enabled == 1);
    config = before;
    assert(ign_race_config_decode(&config, b.raw, sizeof b.raw - 1, 8, random01) == -1);
    assert(!memcmp(&config, &before, sizeof config));
    ign_btz_set_i32(&b, IGN_BTZ_NCARS, 9);
    assert(ign_race_config_decode(&config, b.raw, sizeof b.raw, 8, random01) == -1);
    assert(!memcmp(&config, &before, sizeof config));
}

int main(int argc, char **argv) {
    if (argc == 1) { unit_tests(); return 0; }
    if (argc != 2 || strcmp(argv[1], "--oracle")) return 2;
    char command;
    while (scanf(" %c", &command) == 1) {
        IgnRace r;
        IgnCar c[8] = {{0}};
        IgnCar *cars[8];
        for (int i = 0; i < 8; i++) cars[i] = c + i;
        if (command == 'C') {
            unsigned char raw[IGN_BTZ_SIZE];
            int cd;
            if (scanf("%d %lf", &cd, &random_value) != 2) return 2;
            for (size_t i = 0; i < sizeof raw; i++) {
                unsigned byte;
                if (scanf("%2x", &byte) != 1) return 2;
                raw[i] = (unsigned char)byte;
            }
            IgnRaceConfig config;
            if (ign_race_config_decode(&config, raw, sizeof raw, cd, random01)) return 2;
            print_config(&config);
        } else if (command == 'Q') {
            int mode, n, round;
            if (scanf("%d %d %d", &mode, &n, &round) != 3 || n < 0 || n > 8) return 2;
            ign_race_init(&r, mode, n);
            r.best_lap = 15; r.elapsed = 12; r.clock = 99; r.origin_ms = 123;
            for (int i = 0; i < 8; i++) r.points[i] = 10 + i;
            ign_race_reset(&r, mode, n, round);
            printf("%d %d %.17g %.17g %d %d %d %d %.17g %.17g %.17g %lld",
                   r.finished, r.remaining, r.finish_clock, r.total, r.ai_parity, r.ai_car,
                   r.elimination_limit, r.elimination_count, r.best_lap, r.elapsed, r.clock,
                   (long long)r.origin_ms);
            for (int i = 0; i < 2; i++) printf(" %d", r.human_finish[i]);
            for (int i = 0; i < 8; i++) printf(" %d %d %d", r.points[i], r.result_order[i], r.result_aux[i]);
            for (int i = 0; i < 10; i++) printf(" %d %d", r.hud_slots[0][i], r.hud_slots[1][i]);
            putchar('\n');
        } else if (command == 'T') {
            int n, now;
            double saved;
            if (scanf("%d %d %lf", &n, &now, &saved) != 3 || n < 0 || n > 8) return 2;
            ign_race_init(&r, 0, n);
            r.best_lap = 15; r.elapsed = 12;
            for (int i = 0; i < n; i++) {
                c[i].race_elapsed = 1; c[i].last_lap = 2;
                c[i].lap_elapsed = 3; c[i].lap_origin = 4;
                for (int k = 0; k < 3; k++) c[i].lap_times[k] = 5 + k;
            }
            ign_race_reset_times(&r, cars, n, now, saved);
            printf("%.17g %.17g %lld", r.best_lap, r.elapsed, (long long)r.origin_ms);
            for (int i = 0; i < n; i++) {
                printf(" %.17g %.17g %.17g %.17g", c[i].race_elapsed, c[i].last_lap,
                       c[i].lap_elapsed, c[i].lap_origin);
                for (int k = 0; k < 3; k++) printf(" %.17g", c[i].lap_times[k]);
            }
            putchar('\n');
        } else if (command == 'R' || command == 'E') {
            int n, mode, section, players, ghost, ghost_progress, remaining, finished, camera, hold;
            double total, finish_clock;
            if (scanf("%d %d %d %lf %d %d %d %d %d %lf %d %d", &n, &mode, &section, &total,
                      &players, &ghost, &ghost_progress, &remaining, &finished, &finish_clock,
                      &camera, &hold) != 12 || n < 1 || n > 8) return 2;
            ign_race_init(&r, mode, n);
            r.section_count = section; r.total = total; r.players_minus_one = players;
            r.ghost_enabled = ghost; r.ghost_progress = ghost_progress;
            r.remaining = remaining; r.finished = finished; r.finish_clock = finish_clock;
            r.tv_camera = camera; r.camera_hold = hold; r.ghost_position = 7;
            for (int i = 0; i < n; i++)
                if (scanf("%d %d %d %d %d %d %d %lf %lf %d %d",
                          &c[i].lap, &c[i].section, &c[i].phase, &c[i].out, &c[i].finish_position,
                          &c[i].position, &r.rank_bias[i], &c[i].race_elapsed, &c[i].blow,
                          &c[i].wrecked, &r.grid[i]) != 11) return 2;
            if (command == 'R') {
                ign_race_rank(&r, cars, n);
                printf("%d", r.ghost_position);
                for (int i = 0; i < n; i++) printf(" %d %d", c[i].position, c[i].finish_position);
            } else {
                ign_race_step(&r, cars, n);
                printf("%d %d %.17g %d %d", r.remaining, r.finished, r.finish_clock, r.tv_camera, r.camera_hold);
                for (int i = 0; i < 2; i++) printf(" %d", r.human_finish[i]);
                for (int i = 0; i < n; i++) printf(" %d %d %.17g %d", c[i].out, c[i].finish_position,
                                                 c[i].blow, c[i].wrecked);
            }
            putchar('\n');
        } else return 2;
    }
    return ferror(stdin) ? 2 : 0;
}
