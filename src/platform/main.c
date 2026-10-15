#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../game/ai.h"
#include "../game/assets.h"
#include "../game/car.h"
#include "../game/ghost.h"
#include "../game/pos.h"
#include "../game/race.h"
#include "../game/save.h"
#include "../game/track.h"
#include "../render/camera.h"
#include "../render/hud.h"
#include "../render/raster.h"
#include "../frontend/frontend.h"
#include "../frontend/settings.h"
#include "audio.h"
#include "music.h"
#include "sound.h"

#define DEFAULT_DATA_DIR "/Users/Shared/Epic Games/Ignition/ignition"
#define WINDOW_W 1280
#define SIM_DT ((float)IGN_PHYS_DT)

static IgnFrame frame;
static IgnHud hud;

static void to_rgb(const IgnFrame *fb, const IgnPalette *pal, uint32_t *out) {
    uint32_t lut[256];
    for (int i = 0; i < 256; i++)
        lut[i] = 0xFF000000u | pal->rgb[i][0] << 16 | pal->rgb[i][1] << 8 | pal->rgb[i][2];
    for (int i = 0; i < fb->w * fb->h; i++) out[i] = lut[fb->color[i]];
}

static int write_ppm(const char *path, const IgnFrame *fb, const IgnPalette *pal) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", fb->w, fb->h);
    for (int i = 0; i < fb->w * fb->h; i++) fwrite(pal->rgb[fb->color[i]], 1, 3, f);
    return fclose(f);
}

static void free_camera(IgnCamera *cam, const bool *keys, float dt) {
    float speed = (keys[SDL_SCANCODE_LSHIFT] ? 4000.0f : 1000.0f) * dt;
    float turn = 1.5f * dt;
    float fx = sinf(cam->yaw), fz = cosf(cam->yaw);
    if (keys[SDL_SCANCODE_LEFT]) cam->yaw -= turn;
    if (keys[SDL_SCANCODE_RIGHT]) cam->yaw += turn;
    if (keys[SDL_SCANCODE_UP]) cam->pitch -= turn;
    if (keys[SDL_SCANCODE_DOWN]) cam->pitch += turn;
    if (keys[SDL_SCANCODE_W]) cam->x += fx * speed, cam->z += fz * speed;
    if (keys[SDL_SCANCODE_S]) cam->x -= fx * speed, cam->z -= fz * speed;
    if (keys[SDL_SCANCODE_D]) cam->x += fz * speed, cam->z -= fx * speed;
    if (keys[SDL_SCANCODE_A]) cam->x -= fz * speed, cam->z += fx * speed;
    if (keys[SDL_SCANCODE_Q]) cam->y -= speed;
    if (keys[SDL_SCANCODE_E]) cam->y += speed;
}

#define MAX_RIVALS 7

static IgnCar rivals[MAX_RIVALS];
static IgnAi rival_ai[MAX_RIVALS];
static int nrivals;
static IgnRace race;
/* Loaded with --ghost; replayed one sample per physics tick, without collisions. */
static IgnGhost ghost;
static IgnCar ghost_car;
static int ghost_on, ghost_tick;
/* Per-car cameras [0x1fef9c] (0 = player). */
static IgnChaseCam cams[MAX_RIVALS + 1];
static int track_id;
/* Post-race view of sub_000420cc: [0x1fb4d0] frames since the player finished, camera [0x1fee80] (0-2 fixed,
 * 3 = following a car) and the followed car [0xc0b34] with its switch delay [0xc0b38]. */
static double finish_clock;
/* Records, IGN_DOS.BTZ and time-trial ghosts in the user folder: always in the window; headless with --time-trial
 * or IGN_SAVE_DIR. */
static IgnSession session;
static int session_on, session_saved;
static int tv_index = 3, spectated, spectate_wait;
static IgnSkids skids;
static IgnFx fx;
static const IgnTrack *hud_track;
/* The free camera (F1) shows the debug camera overlay of sub_00049fdc. */
static bool hud_debug;
static IgnCar *player_car;
static IgnCollObjs colls;
static IgnTexAnims texanims;
static IgnMessage messages[IGN_MESSAGES];
static IgnWeather weather;

/* Start grid of sub_00026458: PLC origin of the type-0 object facing the type-1 object; slot i is
 * (i/2)*180 + 200 units back and 60 units to the side by parity (a lone car: 200 back, centred). */
static void grid_pose(const IgnLevel *lv, const IgnTrack *t, int slot, int ncars, float *x, float *z,
                      float *heading) {
    const int32_t *a = lv->track.objs[t->start_obj].pos, *b = lv->track.objs[t->dir_obj].pos;
    *heading = atan2f((float)(b[0] - a[0]), (float)(b[2] - a[2]));
    float fx = sinf(*heading), fz = cosf(*heading);
    float back = ncars > 1 ? -(float)(slot / 2) * 180.0f - 200.0f : -200.0f;
    float side = ncars > 1 ? (slot % 2 ? -60.0f : 60.0f) : 0.0f;
    *x = (float)a[0] + back * fx - side * fz;
    *z = (float)a[2] + back * fz + side * fx;
}

/* sub_00022d08: computer cars in grid slots 1..want with the models of 0xc5520 for the difficulty. */
static void spawn_rivals(const IgnLevel *lv, const IgnTrack *t, const IgnCar *player, int want) {
    int spare = 5;
    for (int k = 0; nrivals < want && nrivals < MAX_RIVALS && k < 10; k++) {
        int id = ign_computer_model(k, &player->id, 1, &spare);
        float x, z, h;
        grid_pose(lv, t, nrivals + 1, want + 1, &x, &z, &h);
        if (ign_car_init(&rivals[nrivals], lv, id, x, z, h)) continue;
        ign_car_set_computer(&rivals[nrivals]);
        ign_ai_init(&rival_ai[nrivals], nrivals);
        /* 0x22eeb: the computer models' AI parameter +0x28 = 0.19 x difficulty + 0.4. */
        rival_ai[nrivals].p.s28 = ign_get_difficulty() * 0.19 + 0.4;
        nrivals++;
    }
}

/* sub_00043590: the leading car still racing (the last one in elimination), switched to after 20 frames.
 * Returns -1 when every car has finished. */
static int spectate(IgnCar *const *all, int n) {
    int best = -1, best_pos = 0;
    for (int i = 0; i < n; i++) {
        if (all[i]->out) continue;
        int pos = ign_race_position((const IgnCar *const *)all, n, i);
        if (best < 0 || (race.mode == IGN_MODE_ELIMINATION ? pos > best_pos : pos < best_pos)) best = i, best_pos = pos;
    }
    if (best < 0) return -1;
    if (best == spectated) spectate_wait = 0;
    else if (++spectate_wait > 20) spectated = best;
    return spectated;
}

/* car_update 0x2f01f / 0x30dbb: the special objects (0x12c-0x15d) under a car, before its tick. Track 5 is ICELAND. */
static void special_tick(IgnCar *c, const IgnLevel *lv, const IgnTrack *t) {
    if (c->wrecked) return;
    /* cars_update_all 0x2d685: [0x24bff0] = layer (PLC type bits 12-15) of the car's section object, until 120
     * frames after the player's finish. */
    int layer = 0, obj = ign_track_section_object(t, c->section);
    if (obj >= 0 && obj < lv->track.nobj && !(player_car->out && finish_clock >= 120.0))
        layer = (int)((lv->track.objs[obj].type >> 12) & 0xf);
    int wreck, special = ign_special_probe(lv, &fx, c, track_id == 5, layer, &wreck);
    ign_special_apply(c, special, wreck, &fx, c->x, c->z);
    /* car_update 0x31316: the flattened state of sub_00031858. */
    IgnCar *all[MAX_RIVALS + 1];
    int n = 0, index = 0;
    all[n++] = player_car;
    for (int i = 0; i < nrivals; i++) all[n++] = &rivals[i];
    for (int i = 0; i < n; i++)
        if (all[i] == c) index = i;
    ign_car_flat_tick(c, all, n, index);
    ign_car_flat_effects(c, &fx, c->x, c->z);
}

/* human_input 0x2c8f5: a flattened car steers the other way. */
static IgnInput human_steer(const IgnCar *c, IgnInput in) {
    if (c->flat > 0) {
        bool left = in.left;
        in.left = in.right, in.right = left;
        in.a_steer = -in.a_steer;
    }
    return in;
}

/* Persistence hooks once per physics tick: ghost sample, "LAP RECORD" (0x2f43a) and the end of the race
 * (records of render_main 0x45938, ghost and BTZ of sub_0002ccc8). */
static void session_hooks(IgnCar *const *all, int n, const IgnTrack *t, double best_lap, int was_out) {
    const IgnCar *player = all[0];
    int language = ign_btz_i32(&session.btz, IGN_BTZ_LANGUAGE);
    if (ign_race_started(&race)) ign_session_tick(&session, player, t->count);
    if (race.best_lap < best_lap && player->last_lap == race.best_lap)
        printf("%s %.2f\n", ign_lap_record_text(language), player->last_lap);
    if (player->out && !was_out) {
        session_saved = ign_session_finish(&session, player);
        if (session_saved < 0)
            fprintf(stderr, "cannot save records or ghost in %s\n", session.dir);
        else if (session_saved & IGN_SAVED_TRACK_RECORD)
            printf("%s %.2f\n", ign_track_record_text(language), player->race_elapsed);
        if (session_saved >= 0 && session.time_trial)
            printf("time trial: %.2f s against %.2f s, ghost %s\n", player->race_elapsed, session.record_cs / 100.0,
                   session_saved & IGN_SAVED_GHOST ? session.ghost_path : "not saved");
    }
    /* sub_0002ccc8 once every car is out: championship round and grid, grid shuffle, IGN_DOS.BTZ. */
    if (race.finished >= n && !session.ended) {
        int place[MAX_RIVALS + 1];
        for (int i = 0; i < n; i++) place[i] = all[i]->place - 1;
        if (ign_session_race_end(&session, race.mode, place, n) < 0)
            fprintf(stderr, "cannot save %s in %s\n", IGN_BTZ_NAME, session.dir);
    }
}

/* SDL_GetPrefPath, or IGN_SAVE_DIR (tests); never the game folder. */
static int session_setup(const char *data_dir, const char *level, int car_model, int time_trial) {
    const char *dir = getenv("IGN_SAVE_DIR");
    char *pref = dir ? NULL : SDL_GetPrefPath("UDS", "Ignition");
    int rc = dir || pref ? ign_session_begin(&session, dir ? dir : pref, data_dir, level, car_model, time_trial,
                                             &ghost)
                         : -1;
    SDL_free(pref);
    if (rc) {
        fprintf(stderr, "no user folder for records: %s\n", SDL_GetError());
        return -1;
    }
    session_on = 1;
    race.best_lap = ign_session_best_lap(&session);
    if (session.time_trial) {
        static const char *const STATE[] = {"no ghost", "ghost", "ghost of another track (not saved)"};
        printf("time trial: %s, record %.2f s, saving in %s\n", STATE[session.ghost_state], session.record_cs / 100.0,
               session.dir);
    }
    return 0;
}

static int tick_rivals(const IgnLevel *lv, const IgnTrack *t, IgnCar *player, IgnAi *player_ai, int autopilot) {
    ghost_tick++;
    int started = ign_race_started(&race);
    int was_out = player->out;
    double best_lap = race.best_lap;
    ign_race_progress(&race, player, t);
    /* car_update 0x2f43a: the player's lap record, 80 frames blinking every 8 (English). */
    if (race.best_lap < best_lap) ign_message_add(messages, "LAP RECORD", 80, 8, -1);
    IgnCar *all[MAX_RIVALS + 1] = {player};
    for (int i = 0; i < nrivals; i++) all[i + 1] = &rivals[i];
    ign_ai_set_cars(all, nrivals + 1);
    for (int i = 0; i < nrivals; i++) {
        special_tick(&rivals[i], lv, t);
        if (!rivals[i].wrecked) {
            ign_car_tick(&rivals[i], lv, NULL);
            ign_race_progress(&race, &rivals[i], t);
        }
        all[i + 1] = &rivals[i];
    }
    IgnCarHit impacts[MAX_RIVALS * (MAX_RIVALS + 1) / 2 + 1];
    int nimpacts = 0;
    int hits = ign_cars_collide(all, nrivals + 1, impacts, (int)(sizeof impacts / sizeof impacts[0]), &nimpacts);
    /* cars_update_all 0x2dcbc: control phase follows physics and pair collisions for all cars. */
    if (autopilot || player->progress_lock == 1)
        ign_ai_control_tick(player_ai, player, t, &fx, &race, 0);
    for (int i = 0; i < nrivals; i++)
        ign_ai_control_tick(&rival_ai[i], &rivals[i], t, &fx, &race, i + 1);
    ign_race_ai_step(&race, nrivals + 1);
    ign_race_wrecks(all, nrivals + 1, lv, t);
    float focus_x = all[spectated]->x, focus_z = all[spectated]->z;
    /* sub_0003f37c runs in the frame pass, where car+0x278 is still 1 only after a landing in the frame's last tick. */
    if (skids.tick % 2 == 0)
        for (int i = 0; i <= nrivals; i++)
            if (!all[i]->wrecked && all[i]->landed) ign_fx_landing_dust(&fx, all[i], focus_x, focus_z);
    ign_sound_tick(all, nrivals + 1, &fx);
    for (int i = 0; i < nimpacts; i++) {
        const IgnCarHit *h = &impacts[i];
        /* cars_update_all 0x2d50f: sub_0002ea44 above a mean impact of 15, then COLL 03_DUNS at the first car. */
        if (h->impact > 15) {
            ign_fx_impact(&fx, all[h->a], all[h->b], h->x, h->z, focus_x, focus_z);
            int v = (int)(h->impact * 30.0);
            ign_fx_sound(&fx, 0, 2, 0, v > 0x10000 ? 0x10000 : v, 22000, (float)h->x, (float)h->z);
        }
        int v = (int)fabs(h->impact * 800.0);
        ign_fx_sound(&fx, 0, 2, 3, v > 0x10000 ? 0x10000 : v, 22000, all[h->a]->x, all[h->a]->z);
    }
    for (int i = 0; i <= nrivals; i++) ign_skids_car(&skids, &fx, all[i], focus_x, focus_z);
    /* car_input 0x4d949 / 0x4daf2: sub_0004e688 as soon as a turbo starts. */
    for (int i = 0; i <= nrivals; i++)
        if (all[i]->turbo_started) ign_fx_turbo_start(&fx, all[i], focus_x, focus_z), all[i]->turbo_started = 0;
    /* Once per 1/36 s frame: the wreck controllers (sub_0003a788 / sub_0003c208), the turbo exhaust of car_turbo
     * (sub_0004e97c) and collisions_pass (0x3fd04), which moves every effect. */
    if (skids.tick % 2 == 0) {
        for (int i = 0; i <= nrivals; i++) {
            if (all[i]->wrecked && !all[i]->out)
                ign_fx_wreck(&fx, all[i], i, (int)all[i]->wreck_frame, focus_x, focus_z);
            else if (!all[i]->wrecked && all[i]->turbo_mode > 0)
                ign_fx_turbo(&fx, all[i], focus_x, focus_z);
            /* cars_body_sway (sub_0003ea00) and sub_00020d60 (sub_0003ee70) for every car still driving. */
            if (!all[i]->wrecked) {
                ign_fx_wall_sparks(&fx, all[i], i == 0, focus_x, focus_z);
                ign_fx_wheel_sparks(&fx, all[i], focus_x, focus_z);
            }
        }
        /* sub_000400f4 (sub_00020d60) and sub_000403c8 (end of cars_body_sway). */
        ign_weather_zone(&weather, player);
        ign_weather_frame(&weather, &fx, all[spectated], spectated, t, lv, race.total,
                          player->out ? finish_clock : -1.0, focus_x, focus_z);
        ign_fx_emitters(&fx, lv, focus_x, focus_z);
        ign_fx_frame(&fx, all, nrivals + 1, focus_x, focus_z);
        /* collisions_pass 0x3fdce..0x3feda after the effect handlers. */
        ign_texanim_frame(&texanims);
        ign_coll_frame(&colls, lv, all, nrivals + 1, &fx, focus_x, focus_z);
        if (race.mode == IGN_MODE_ELIMINATION && race.clock < 0)
            for (int i = 0; i <= nrivals; i++)
                if (!all[i]->wrecked && !all[i]->out &&
                    ign_race_position((const IgnCar *const *)all, nrivals + 1, i) == race.remaining)
                    ign_fx_elimination_smoke(&fx, all[i], focus_x, focus_z);
        ign_messages_age(messages);
        ign_sound_frame(all, nrivals + 1, spectated, &race, player->out ? finish_clock : -1.0, &weather, &fx);
        /* sub_00032d8c at the start of the next frame: the last tick's axle steps. */
        for (int i = 0; i <= nrivals; i++) ign_car_frame_velocity(all[i]);
    }
    ign_skids_step(&skids);
    if (started) {
        double blown[MAX_RIVALS + 1];
        for (int i = 0; i <= nrivals; i++) blown[i] = all[i]->blow;
        ign_race_step(&race, all, nrivals + 1);
        /* sub_0002de30 0x2e100: "%s IS OUT" for 80 frames; the port has no driver names. */
        for (int i = 0; i <= nrivals; i++)
            if (blown[i] == 0.0 && all[i]->blow != 0.0) {
                char text[100];
                if (i == 0) snprintf(text, sizeof text, "PLAYER IS OUT");
                else snprintf(text, sizeof text, "CPU %d IS OUT", i);
                ign_message_add(messages, text, 80, 8, -1);
            }
    }
    /* car_update 0x2f37f: the player's finish restarts [0x1fb4d0] and selects the car-following camera. */
    if (player->out && !was_out) finish_clock = 0, tv_index = 3, spectate_wait = 100;
    if (session_on) session_hooks(all, nrivals + 1, t, best_lap, was_out);
    /* sub_00047f94 runs once per rendered frame for every car; the original renders at most 36 frames per second. */
    if (ghost_tick % 2 == 0) {
        for (int i = 0; i <= nrivals; i++) ign_chase_update(&cams[i], all[i], lv, t, started);
        if (player->out && finish_clock < 120.0) finish_clock += 1.0;
        if (player->out && finish_clock >= 120.0 && tv_index == 3 && spectate(all, nrivals + 1) < 0)
            tv_index = 0, spectated = 0;
    }
    /* Advanced last so the player (ticked before this function) and the rivals see the same clock. */
    ign_race_clock(&race);
    for (int i = 0; i <= nrivals; i++) {
        if (!started && ign_race_started(&race)) ign_race_reset_car_clock(all[i]);
        ign_race_car_clock(all[i], race.mode, race.elapsed);
    }
    ign_set_race_started(ign_race_started(&race));
    ign_set_level_clock(race.total);
    for (int i = 0; i <= nrivals; i++) all[i]->position = ign_race_position((const IgnCar *const *)all, nrivals + 1, i);
    return hits;
}

/* sub_000420cc: chase camera (ahead of the car looking back while `rear`), and 120 frames after the player has
 * finished the fixed cameras of 0xc464c or the camera of the leading car. */
static void race_view(const IgnCar *player, int rear, IgnCamera *cam) {
    if (player->out && finish_clock >= 120.0) {
        if (tv_index < 3 && ign_fixed_camera(track_id, tv_index, race.total, cam) == 0) return;
        if (tv_index == 3 && spectated > 0 && spectated <= nrivals) {
            ign_chase_view(&cams[spectated], &rivals[spectated - 1], 1, cam);
            return;
        }
        if (tv_index == 3) {
            ign_chase_view(&cams[0], player, 1, cam);
            return;
        }
    }
    ign_chase_view(&cams[0], player, rear, cam);
}

/* Race data read by the HUD (sub_0004a320); all[0] is the player. */
static IgnHudState hud_state(const IgnLevel *lv, const IgnCar *const *all) {
    return (IgnHudState){.cars = all, .ncars = nrivals + 1, .level = lv, .track = hud_track, .mode = race.mode,
                         .remaining = race.remaining, .difficulty = ign_get_difficulty(), .clock = race.clock,
                         .total = race.total, .best_lap = race.best_lap,
                         .shake = cams[0].shake, .track_id = track_id};
}

/* Per-frame HUD counters, advanced from the race clock so headless runs see the same frames. */
static void hud_tick(const IgnLevel *lv, const IgnCar *car) {
    const IgnCar *all[MAX_RIVALS + 1] = {car};
    for (int i = 0; i < nrivals; i++) all[i + 1] = &rivals[i];
    const IgnHudState hs = hud_state(lv, all);
    ign_hud_update(&hud, &hs);
}

static void draw(const IgnLevel *lv, const IgnCar *car, const IgnCamera *cam) {
    ign_frame_clear(&frame);
    ign_render_track(&frame, lv, cam);
    ign_render_skids(&frame, lv, &skids, cam);
    /* sub_0003a788 hides a wrecked car (LI_HIDEOBJECT) until it is put back; sub_0003db4c after its slide. */
    for (int i = 0; i < nrivals; i++)
        if (ign_car_visible(&rivals[i])) ign_render_car(&frame, lv, &rivals[i], cam);
    if (ghost_on) {
        ign_ghost_pose(&ghost, race.tick > 0 ? race.tick - 1 : 0, &ghost_car);
        ign_render_car(&frame, lv, &ghost_car, cam);
    }
    if (ign_car_visible(car)) ign_render_car(&frame, lv, car, cam);
    IgnCar *all[MAX_RIVALS + 1] = {(IgnCar *)car};
    for (int i = 0; i < nrivals; i++) all[i + 1] = &rivals[i];
    ign_render_fx(&frame, lv, &fx, all, nrivals + 1, cam);
    ign_render_weather(&frame, lv, &weather, cam);
    const IgnHudState hs = hud_state(lv, (const IgnCar *const *)all);
    ign_hud_update(&hud, &hs);
    ign_hud_draw(&hud, &frame, &hs);
    if (hud_debug) ign_hud_draw_debug(&hud, &frame, cam->x, cam->y, cam->z, cam->yaw, cam->pitch);
}

static void trace_car(int tick, const IgnCar *car, const IgnInput *in) {
    fprintf(stderr, "t=%.17g tick=%d trace_schema=5", tick * IGN_PHYS_DT, tick);
#define CAR_FIELD(field) fprintf(stderr, " " #field "=%.17g", (double)car->field)
#define BODY_FIELD(field) fprintf(stderr, " b_" #field "=%.17g", (double)car->body.field)
#define INPUT_FIELD(field) fprintf(stderr, " in_" #field "=%d", (int)in->field)
    INPUT_FIELD(accel); INPUT_FIELD(brake); INPUT_FIELD(left); INPUT_FIELD(right); INPUT_FIELD(turbo);
    INPUT_FIELD(analog); INPUT_FIELD(a_accel); INPUT_FIELD(a_brake); INPUT_FIELD(a_steer);
    INPUT_FIELD(manual_gear); INPUT_FIELD(gear_shift);
    CAR_FIELD(id); CAR_FIELD(spec); CAR_FIELD(x); CAR_FIELD(y); CAR_FIELD(z); CAR_FIELD(heading);
    CAR_FIELD(kmh); CAR_FIELD(height); CAR_FIELD(ground); CAR_FIELD(ground_prev); CAR_FIELD(fall_speed);
    CAR_FIELD(grip_avg);
    CAR_FIELD(airborne); CAR_FIELD(ground_object); CAR_FIELD(surface_type); CAR_FIELD(special_ground);
    CAR_FIELD(level_change); CAR_FIELD(jump_mode); CAR_FIELD(wall_hit); CAR_FIELD(wall_impact); CAR_FIELD(nwalls);
    CAR_FIELD(gear); CAR_FIELD(forward_gear); CAR_FIELD(steer_visual); CAR_FIELD(revs);
    CAR_FIELD(gear_ticks); CAR_FIELD(gear_delay);
    CAR_FIELD(turbo_charge); CAR_FIELD(turbo_mode); CAR_FIELD(turbo_wait); CAR_FIELD(turbo_key);
    CAR_FIELD(race_elapsed); CAR_FIELD(lap_elapsed); CAR_FIELD(lap_origin);
    CAR_FIELD(last_lap); CAR_FIELD(lap_section); CAR_FIELD(progress_lock); CAR_FIELD(finish_position);
    fprintf(stderr, " race_best_lap=%.17g", race.best_lap);
    for (int i = 0; i < IGN_RACE_LAPS; i++)
        fprintf(stderr, " lap_time%d=%.17g", i, car->lap_times[i]);
    CAR_FIELD(creep); CAR_FIELD(creep_ticks); CAR_FIELD(catchup); CAR_FIELD(catchup_clock);
    CAR_FIELD(section); CAR_FIELD(phase); CAR_FIELD(lap); CAR_FIELD(position);
    CAR_FIELD(out); CAR_FIELD(wrecked); CAR_FIELD(wrecks); CAR_FIELD(wreck_kind); CAR_FIELD(wreck_frame);
    CAR_FIELD(landed); CAR_FIELD(offroad); CAR_FIELD(step_x); CAR_FIELD(step_z);
    BODY_FIELD(mass); BODY_FIELD(inertia); BODY_FIELD(restitution); BODY_FIELD(a); BODY_FIELD(b);
    BODY_FIELD(heading); BODY_FIELD(yaw_rate); BODY_FIELD(px); BODY_FIELD(pz);
    BODY_FIELD(vx); BODY_FIELD(vz); BODY_FIELD(v_fwd); BODY_FIELD(v_lat); BODY_FIELD(saturated);
    BODY_FIELD(steer); BODY_FIELD(pitch); BODY_FIELD(roll); BODY_FIELD(grip_f); BODY_FIELD(grip_r);
    BODY_FIELD(sliding); BODY_FIELD(drive); BODY_FIELD(fmax_f); BODY_FIELD(fmax_r);
    BODY_FIELD(radius); BODY_FIELD(impact); BODY_FIELD(contact_x); BODY_FIELD(contact_z);
    for (int i = 0; i < 4; i++)
        fprintf(stderr, " wheel_type%d=%d b_corner_x%d=%.17g b_corner_z%d=%.17g",
                i, car->wheel_type[i], i, car->body.corner_x[i], i, car->body.corner_z[i]);
#undef CAR_FIELD
#undef BODY_FIELD
#undef INPUT_FIELD
    fputc('\n', stderr);
}

/* A window shared with the frontend (menu mode); run_game creates its own otherwise. */
static SDL_Window *shared_win;
static SDL_Renderer *shared_ren;
static bool quit_requested;

static int run_game(int argc, char **argv) {
    nrivals = 0;
    memset(&race, 0, sizeof race);
    ghost_on = ghost_tick = 0;
    finish_clock = 0;
    tv_index = 3;
    spectated = spectate_wait = 0;
    session_on = session_saved = 0;
    const char *data_dir = getenv("IGNITION_DATA");
    if (!data_dir) data_dir = DEFAULT_DATA_DIR;
    const char *level = "USA";
    const char *shot = NULL;
    float sim_seconds = 0.0f, sim_steer = 0.0f;
    int autopilot = 0, want_rivals = -1, sim_turbo = 0, race_mode = 0, countdown = 0, rear_view = 0, car_id = -1;
    int sim_ticks = -1;
    bool manual_start = false;
    IgnCamera cam = {0};
    int fixed_cam = 0;
    const char *ghost_out = NULL, *ghost_in = NULL;
    /* [0x1ff253] music volume 0..10 and [0x1ff24f] track setting (0 level, -1 random, n fixed). */
    int music_on = 1, music_volume = 10, music_mode = IGN_MUSIC_BY_LEVEL;
    ign_frame_init(&frame, 640, 480);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 1 < argc)
            shot = argv[++i];
        else if (!strcmp(argv[i], "--res") && i + 1 < argc) {
            int w = 0, h = 0;
            if (sscanf(argv[++i], "%dx%d", &w, &h) != 2) {
                fprintf(stderr, "--res expects WxH, e.g. 640x480\n");
                return 1;
            }
            ign_frame_init(&frame, w, h);
        } else if (!strcmp(argv[i], "--persp")) {
            frame.perspective = 1;
        } else if (!strcmp(argv[i], "--countdown")) {
            countdown = 1;
        } else if (!strcmp(argv[i], "--no-music")) {
            music_on = 0;
        } else if (!strcmp(argv[i], "--music-volume") && i + 1 < argc) {
            music_volume = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--music-track") && i + 1 < argc) {
            i++;
            music_mode = !strcmp(argv[i], "random") ? IGN_MUSIC_RANDOM : atoi(argv[i]);
        } else if (!strcmp(argv[i], "--rear")) {
            rear_view = 1;
        } else if (!strcmp(argv[i], "--record-ghost") && i + 1 < argc)
            ghost_out = argv[++i];
        else if (!strcmp(argv[i], "--ghost") && i + 1 < argc)
            ghost_in = argv[++i];
        else if (!strcmp(argv[i], "--probe") && i + 4 < argc) {
            const char *lvl = argv[++i];
            float px = strtof(argv[++i], NULL), pz = strtof(argv[++i], NULL);
            float up = strtof(argv[++i], NULL);
            IgnLevel pl;
            if (ign_load_level(data_dir, lvl, &pl)) return 1;
            for (float dx = -40; dx <= 40; dx += 10) {
                IgnGroundHit hit;
                if (ign_ground_at(&pl, px + dx, pz, -up, &hit) == 0)
                    printf("x=%.0f ground_up=%.1f type=0x%x obj=%d face=%d\n", px + dx, -hit.y,
                           hit.surface_type, hit.object, hit.face);
                else
                    printf("x=%.0f none\n", px + dx);
            }
            ign_free_level(&pl);
            return 0;
        } else if ((!strcmp(argv[i], "--sim") || !strcmp(argv[i], "--sim-ticks")) && i + 2 < argc) {
            int exact_ticks = !strcmp(argv[i], "--sim-ticks");
            if (exact_ticks) {
                char *end;
                errno = 0;
                long n = strtol(argv[++i], &end, 10);
                if (errno || end == argv[i] || *end || n < 0 || n > INT_MAX) {
                    fprintf(stderr, "--sim-ticks expects an integer from 0 to %d\n", INT_MAX);
                    return 1;
                }
                sim_ticks = (int)n;
                sim_seconds = (float)(sim_ticks * IGN_PHYS_DT);
            } else {
                sim_seconds = strtof(argv[++i], NULL);
                sim_ticks = -1;
            }
            i++;
            autopilot = !strcmp(argv[i], "auto");
            sim_turbo = !strcmp(argv[i], "turbo");
            sim_steer = autopilot || sim_turbo ? 0 : strtof(argv[i], NULL);
        } else if (!strcmp(argv[i], "--sim-ticks")) {
            fprintf(stderr, "--sim-ticks requires N and DIR\n");
            return 1;
        } else if (!strcmp(argv[i], "--difficulty") && i + 1 < argc) {
            ign_set_difficulty(atoi(argv[++i]));
        } else if (!strcmp(argv[i], "--car") && i + 1 < argc) {
            car_id = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--manual")) {
            manual_start = true;
        } else if (!strcmp(argv[i], "--time-trial")) {
            race_mode = IGN_MODE_TIME_TRIAL;
        } else if (!strcmp(argv[i], "--elimination")) {
            race_mode = IGN_MODE_ELIMINATION;
        } else if (!strcmp(argv[i], "--rivals") && i + 1 < argc) {
            want_rivals = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--model") && i + 6 < argc && shot) {
            int id = atoi(argv[++i]);
            float yaw = strtof(argv[++i], NULL), steer = strtof(argv[++i], NULL);
            float roll = strtof(argv[++i], NULL), pitch = strtof(argv[++i], NULL);
            const char *lvl = argv[++i];
            IgnLevel ml;
            IgnCar mc;
            if (ign_load_level(data_dir, lvl, &ml) || ign_car_init(&mc, &ml, id, 0, 0, yaw)) return 1;
            if (getenv("IGN_BANARNE")) mc.body_stretch = 3.0f;
            mc.x = mc.z = 0;
            mc.y = 0;
            mc.height = mc.ground = 0;
            if (getenv("IGN_LIFT")) mc.height = atof(getenv("IGN_LIFT")), mc.y = (float)-mc.height;
            mc.steer_visual = steer;
            mc.body_roll = mc.body.roll = roll;
            mc.body_pitch = mc.body.pitch = pitch;
            IgnCamera mcam = {.x = 0, .y = -38, .z = -105, .yaw = 0, .pitch = 0.3f};
            int bg = 0, best = -1;
            for (int c = 1; c < 256; c++) {
                int l = ml.palette.rgb[c][0] + ml.palette.rgb[c][1] + ml.palette.rgb[c][2];
                if (l > best) best = l, bg = c;
            }
            memset(frame.color, bg, sizeof frame.color);
            memset(frame.inv_z, 0, sizeof frame.inv_z);
            ign_render_car(&frame, &ml, &mc, &mcam);
            return write_ppm(shot, &frame, &ml.palette) ? 1 : 0;
        } else if (!strcmp(argv[i], "--cam") && i + 5 < argc) {
            cam.x = strtof(argv[++i], NULL);
            cam.y = strtof(argv[++i], NULL);
            cam.z = strtof(argv[++i], NULL);
            cam.yaw = strtof(argv[++i], NULL);
            cam.pitch = strtof(argv[++i], NULL);
            fixed_cam = 1;
        } else
            level = argv[i];
    }

    if (race_mode == IGN_MODE_TIME_TRIAL && ghost_in) {
        fprintf(stderr, "--time-trial loads its own ghost; use --ghost without it\n");
        return 1;
    }
    if (sim_ticks >= 0 && !shot) {
        fprintf(stderr, "--sim-ticks requires --shot\n");
        return 1;
    }
    IgnLevel lv;
    if (ign_load_level(data_dir, level, &lv)) {
        fprintf(stderr, "cannot load level %s from %s\n", level, data_dir);
        return 1;
    }

    /* Start on the class-0 object facing the class-1 object, as sub_00028f08 walks the track. */
    IgnTrack track;
    if (ign_build_track(&lv.track, &track)) {
        fprintf(stderr, "cannot build track sections\n");
        return 1;
    }
    ign_track_gates(&track, &lv);
    if (ign_track_ai_records(&track, &lv, ign_ghost_track_id(level)))
        fprintf(stderr, "warning: cannot build TRI AI records; using track-gate fallback\n");
    ign_ai_set_track(level);
    IgnCar car;
    int want = race_mode == IGN_MODE_TIME_TRIAL ? 0 : want_rivals >= 0 ? want_rivals : (shot ? 0 : 3);
    if (want > MAX_RIVALS) want = MAX_RIVALS;
    float sx, sz, sh;
    grid_pose(&lv, &track, 0, want + 1, &sx, &sz, &sh);
    player_car = &car;
    if (ign_car_init(&car, &lv, car_id >= 0 ? car_id : ign_first_car_id(&lv), sx, sz, sh)) {
        fprintf(stderr, "no car models in CARS.PLC\n");
        return 1;
    }
    spawn_rivals(&lv, &track, &car, want);
    /* BANARNE typed in the menu (0x160c4 toggles 0x1fb2f0[0]): 3x tall bodies from the start. */
    if (ign_frontend_cheat(0)) {
        car.body_stretch = 3.0f;
        for (int r = 0; r < nrivals; r++) rivals[r].body_stretch = 3.0f;
    }
    ign_skids_init(&skids);
    ign_fx_init(&fx);
    ign_fx_emitters_init(&fx, &lv);
    track_id = ign_ghost_track_id(level);
    if (ign_texanim_init(&texanims, &lv.track, ign_texanim_period(track_id)) < 0) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    if (ign_coll_init(&colls, &lv.track) < 0) {
        fprintf(stderr, "Ingen info om kollisions objektet!!\n");
        return 1;
    }
    ign_weather_init(&weather, level);
    ign_chase_init(&cams[0], &car);
    for (int i = 0; i < nrivals; i++) ign_chase_init(&cams[i + 1], &rivals[i]);
    track_id = ign_ghost_track_id(level);
    if (ghost_in) {
        if (ign_ghost_load(ghost_in, &ghost)) {
            fprintf(stderr, "cannot read ghost %s\n", ghost_in);
            return 1;
        }
        if (ghost.track != track_id)
            fprintf(stderr, "ghost track id %d, level %s is %d\n", ghost.track, level, track_id);
        if (ign_car_init(&ghost_car, &lv, ghost.car, sx, sz, sh) &&
            ign_car_init(&ghost_car, &lv, car.id, sx, sz, sh))
            return 1;
        ghost_on = 1;
    }
    hud_track = &track;
    if (ign_hud_init(&hud, data_dir, level, frame.w, frame.h)) {
        fprintf(stderr, "cannot load HUD panel or fonts from %s\n", data_dir);
        ign_free_level(&lv);
        return 1;
    }
    ign_race_init(&race, race_mode, nrivals + 1);
    /* Headless runs keep the user folder untouched unless --time-trial or an explicit IGN_SAVE_DIR. */
    if ((race_mode == IGN_MODE_TIME_TRIAL || !shot || getenv("IGN_SAVE_DIR")) &&
        !session_setup(data_dir, level, car.id, race_mode == IGN_MODE_TIME_TRIAL) &&
        session.ghost_state == IGN_TRIAL_GHOST && !ign_car_init(&ghost_car, &lv, ghost.car, sx, sz, sh))
        ghost_on = 1;
    if (session_on) {
        /* The HUD's TRACK RECORD and languages come from the session's IGN_DOS.BTZ ([0x1ff32b], [0x1ff6eb]). */
        IgnTrackRecords rec;
        if (track_id >= 0 && track_id < 7 && ign_btz_records(&session.btz, track_id, &rec) == 0)
            hud.track_record[track_id] = rec.race[0];
        int language = ign_btz_i32(&session.btz, IGN_BTZ_LANGUAGE);
        if (!getenv("IGN_LANG") && language >= 0 && language < 6) hud.language = language;
    }
    /* Headless runs start at once unless --countdown; the game window always counts down. */
    if (shot && !countdown) ign_race_skip_countdown(&race);
    ign_set_race_started(ign_race_started(&race));
    IgnAi player_ai;
    ign_ai_init(&player_ai, MAX_RIVALS);
    if (getenv("IGN_TRACE")) {
        fprintf(stderr, "track: %d sections\n", track.count);
        double worst = 0;
        int worst_s = -1;
        for (int s = 1; s < track.count; s++) {
            int a_i = -1, b_i = -1;
            for (int i = 0; i < track.nobj; i++) {
                if (track.section[i] == s - 1) a_i = i;
                if (track.section[i] == s) b_i = i;
            }
            if (a_i < 0 || b_i < 0) continue;
            double d = hypot(track.center[a_i][0] - track.center[b_i][0],
                             track.center[a_i][2] - track.center[b_i][2]);
            if (d > worst) worst = d, worst_s = s;
        }
        fprintf(stderr, "largest gap between consecutive sections: %.0f at %d\n", worst, worst_s);
        int narrow = 0, wide = 0;
        double sum = 0;
        for (int s = 0; s < track.count; s++) {
            double w = hypot(track.gate[s][2] - track.gate[s][0], track.gate[s][3] - track.gate[s][1]);
            sum += w;
            narrow += w < 50;
            wide += w > 1500;
        }
        fprintf(stderr, "gates: mean width %.0f, %d narrow (<50), %d wide (>1500)\n",
                sum / track.count, narrow, wide);
        if (getenv("IGN_TRACK_DUMP")) {
            FILE *f = fopen(getenv("IGN_TRACK_DUMP"), "w");
            for (int i = 0; f && i < track.nobj; i++)
                fprintf(f, "%d %d %.0f %.0f\n", track.section[i], track.klass[i],
                        track.center[i][0], track.center[i][2]);
            if (f) fclose(f);
        }
    }

    if (shot) {
        const char *trace = getenv("IGN_TRACE");
        int full_trace = trace && !strcmp(trace, "full"), trace_tick = 0;
        IgnInput in = {.accel = sim_seconds > 0, .left = sim_steer < 0, .right = sim_steer > 0,
                       .turbo = sim_turbo};
        int hits = 0, air = 0, car_hits = 0;
        static IgnGhost rec;
        int ticks = 0;
        int pos_ticks = 0;
        double dev_sum = 0, dev_max = 0;
        if (ghost_out) ign_ghost_begin(&rec, car.id, track_id, "PL1");
        if (full_trace) {
            IgnInput initial = {0};
            trace_car(0, &car, &initial);
        }
        float t = 0;
        for (int step = 0; sim_ticks >= 0 ? step < sim_ticks : t < sim_seconds; step++, t += SIM_DT) {
            special_tick(&car, &lv, &track);
            if (!car.wrecked) {
                IgnInput steered = human_steer(&car, in);
                if (!autopilot && car.progress_lock != 1) player_ai.input = steered;
                ign_car_tick(&car, &lv, autopilot || car.progress_lock == 1 ? NULL : &steered);
                if (!autopilot && car.progress_lock != 1) player_ai.turbo_request = car.turbo_key;
            }
            car_hits += tick_rivals(&lv, &track, &car, &player_ai, autopilot);
            if (autopilot) in = player_ai.input;
            if (++pos_ticks == 2) {
                ign_pos_step(&lv.pos, &lv.track);
                pos_ticks = 0;
            }
            hud_tick(&lv, &car);
            if (full_trace) trace_car(++trace_tick, &car, &in);
            if (!ign_race_started(&race)) continue;
            if (ghost_out && !car.out) ign_ghost_record(&rec, &car, track.count);
            if (ghost_in && ticks < IGN_GHOST_SAMPLES) {
                double d = hypot(car.x - ghost.s[ticks].x, car.z - ghost.s[ticks].z);
                dev_sum += d;
                if (d > dev_max) dev_max = d;
            }
            ticks++;
            hits += car.wall_hit;
            air += car.airborne;
            if (trace && !full_trace)
                fprintf(stderr, "t=%.3f x=%.0f z=%.0f h=%.1f g=%.1f v=%.2f air=%d type=0x%x drive=%.0f/%.0f turbo=%d/%.0f kmh=%.0f\n",
                        t, car.x, car.z, car.height, car.ground, car.fall_speed, car.airborne,
                        car.surface_type, car.body.drive, car.body.fmax_f, car.turbo_mode,
                        car.turbo_charge, car.kmh);
        }
        printf("car at %.0f %.0f %.0f heading %.2f %.0f km/h gear %d surface 0x%x wall hits %d "
               "airborne ticks %d section %d/%d phase %d lap %d\n",
               car.x, car.y, car.z, car.heading, car.kmh, car.gear, car.surface_type, hits, air,
               car.section, track.count, car.phase, car.lap);
        printf("car-car contacts %d, wrecks %d\n", car_hits, car.wrecks);
        if (ghost_in) {
            int n = ticks < IGN_GHOST_SAMPLES ? ticks : IGN_GHOST_SAMPLES;
            printf("ghost %s: car %d track %d %.2f s, name %.3s; deviation over %d ticks mean %.0f max %.0f\n",
                   ghost_in, ghost.car, ghost.track, ghost.time_cs / 100.0, ghost.name, n, n ? dev_sum / n : 0.0,
                   dev_max);
        }
        if (session_on && session.time_trial && !session.finished) printf("time trial: not finished\n");
        if (ghost_out) {
            ign_ghost_finish(&rec, car.race_elapsed);
            if (ign_ghost_save(ghost_out, &rec)) {
                fprintf(stderr, "cannot write ghost %s%s\n", ghost_out, rec.overflow ? " (over 300 s)" : "");
                ign_hud_destroy(&hud);
                ign_free_level(&lv);
                return 1;
            }
            printf("ghost written: %d samples, %.2f s\n", rec.count, rec.time_cs / 100.0);
        }
        {
            const IgnCar *all[MAX_RIVALS + 1] = {&car};
            for (int i = 0; i < nrivals; i++) all[i + 1] = &rivals[i];
            printf("race: %d/%d finished or out, position %d\n", race.finished, nrivals + 1,
                   ign_race_position(all, nrivals + 1, 0));
            for (int i = 0; i <= nrivals; i++)
                if (all[i]->out)
                    printf("  car %d: place %d at tick %d%s\n", i, all[i]->place, all[i]->finish_tick,
                           all[i]->wrecked ? " (eliminated)" : "");
        }
        for (int i = 0; i < nrivals; i++)
            printf("rival %d (car %d): section %d phase %d lap %d %.0f km/h wrecks %d\n", i, rivals[i].id,
                   rivals[i].section, rivals[i].phase, rivals[i].lap, rivals[i].kmh, rivals[i].wrecks);
        if (!fixed_cam) race_view(&car, rear_view, &cam);
        draw(&lv, &car, &cam);
        IgnPalette shown;
        ign_fx_palette(&fx, &lv.palette, &shown);
        int rc = write_ppm(shot, &frame, &shown);
        ign_hud_destroy(&hud);
        ign_free_level(&lv);
        return rc ? 1 : 0;
    }

    SDL_Window *win = shared_win;
    SDL_Renderer *ren = shared_ren;
    bool own_window = !win;
    if (own_window && !SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        ign_hud_destroy(&hud);
        ign_free_level(&lv);
        return 1;
    }
    if (own_window && !SDL_CreateWindowAndRenderer("Ignition", WINDOW_W,
                                                   WINDOW_W * 3 / 4, SDL_WINDOW_RESIZABLE, &win,
                                                   &ren)) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        ign_hud_destroy(&hud);
        ign_free_level(&lv);
        return 1;
    }
    SDL_SetRenderVSync(ren, 1);
    /* The original modes are all shown at 4:3, so 320x200 gets tall pixels as on a CRT. */
    SDL_SetRenderLogicalPresentation(ren, frame.w, frame.w * 3 / 4, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, frame.w, frame.h);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    static uint32_t rgb[IGN_MAX_W * IGN_MAX_H];
    {
        IgnCar *all[MAX_RIVALS + 1] = {&car};
        for (int i = 0; i < nrivals; i++) all[i + 1] = &rivals[i];
        if (ign_sound_start(data_dir, level, &lv, all, nrivals + 1, &weather))
            fprintf(stderr, "audio: %s\n", SDL_GetError());
        else if (music_on)
            ign_music_start(data_dir, level, music_mode, music_volume);
    }

    bool free_cam = false;
    bool manual_gear = manual_start;
    int pending_gear_shift = 0;
    char typed[16] = {0};
    SDL_StartTextInput(win);
    float accumulator = 0.0f;
    int pos_ticks = 0;
    Uint64 last = SDL_GetTicks();
    for (bool running = true; running;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) quit_requested = true;
            if (ev.type == SDL_EVENT_QUIT ||
                (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE))
                running = false;
            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                if (ev.key.key == SDLK_F1) free_cam = !free_cam, hud_debug = free_cam;
                else if (ev.key.key == SDLK_M) manual_gear = !manual_gear;
                else if (!free_cam && ev.key.key == SDLK_Q) pending_gear_shift = -1;
                else if (!free_cam && ev.key.key == SDLK_E) pending_gear_shift = 1;
                /* sub_0002b9e4 0x2c017: TAB (scancode 0x0f) steps the post-race camera 0, 1, 2, 3. */
                else if (ev.key.key == SDLK_TAB) tv_index = (tv_index + 1) % 4;
                /* sub_0002b9e4 0x2c1cb/0x2c29a: left/right on the pause menu's music line; F5/F6 here (deviation). */
                else if (ev.key.key == SDLK_F5) ign_music_step(-1);
                else if (ev.key.key == SDLK_F6) ign_music_step(1);
            }
            /* Cheat words are typed during play (0x160c4); BANARNE toggles 3x tall bodies. */
            if (ev.type == SDL_EVENT_TEXT_INPUT) {
                for (const char *c = ev.text.text; *c; c++) {
                    memmove(typed, typed + 1, sizeof typed - 2);
                    typed[sizeof typed - 2] = (char)SDL_toupper((unsigned char)*c);
                }
                size_t len = strlen(typed);
                if (len >= 7 && !strcmp(typed + len - 7, "BANARNE")) {
                    float s = car.body_stretch == 1.0f ? 3.0f : 1.0f;
                    car.body_stretch = s;
                    for (int r = 0; r < nrivals; r++) rivals[r].body_stretch = s;
                    memset(typed, 0, sizeof typed);
                }
            }
        }
        Uint64 now = SDL_GetTicks();
        float dt = fminf((now - last) / 1000.0f, 0.25f);
        last = now;
        const bool *keys = SDL_GetKeyboardState(NULL);

        if (free_cam) {
            free_camera(&cam, keys, dt);
        } else {
            IgnInput in = {
                .accel = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W],
                .brake = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S],
                .left = keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A],
                .right = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D],
                .turbo = keys[SDL_SCANCODE_SPACE],
                .manual_gear = manual_gear,
            };
            for (accumulator += dt; accumulator >= SIM_DT; accumulator -= SIM_DT) {
                IgnInput tick_input = human_steer(&car, in);
                tick_input.gear_shift = pending_gear_shift;
                pending_gear_shift = 0;
                special_tick(&car, &lv, &track);
                if (!car.wrecked) {
                    if (car.progress_lock != 1) player_ai.input = tick_input;
                    ign_car_tick(&car, &lv, car.progress_lock == 1 ? NULL : &tick_input);
                    if (car.progress_lock != 1) player_ai.turbo_request = car.turbo_key;
                }
                tick_rivals(&lv, &track, &car, &player_ai, 0);
                if (++pos_ticks == 2) {
                    ign_pos_step(&lv.pos, &lv.track);
                    pos_ticks = 0;
                }
            }
            /* human_input 0x2c9df: the rear view (car+0x5a4) lasts while its key is held. */
            race_view(&car, keys[SDL_SCANCODE_R], &cam);
            static const char *const COUNT[] = {" - GO!", " - 1", " - 2", " - 3"};
            int count = ign_race_countdown(&race);
            char title[160];
            snprintf(title, sizeof title, "Ignition - lap %d - section %d/%d - %.0f km/h - gear %d %s - turbo %.0f%%%s%s",
                     car.lap + 1, car.section, track.count, car.kmh, car.gear,
                     manual_gear ? "manual" : "auto", car.turbo_charge,
                     car.turbo_mode ? " ON" : "", count >= 0 ? COUNT[count] : "");
            SDL_SetWindowTitle(win, title);
        }

        ign_music_poll(now / 1000.0, car.out ? car.finish_position : 0);
        draw(&lv, &car, &cam);
        IgnPalette shown;
        ign_fx_palette(&fx, &lv.palette, &shown);
        to_rgb(&frame, &shown, rgb);
        SDL_UpdateTexture(tex, NULL, rgb, frame.w * 4);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }

    ign_music_stop();
    ign_sound_stop();
    SDL_DestroyTexture(tex);
    SDL_StopTextInput(win);
    if (own_window) {
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
    }
    ign_hud_destroy(&hud);
    ign_free_level(&lv);
    return 0;
}

/* ---- frontend (menu mode) --------------------------------------------------------------------------------------- */

static SDL_Texture *menu_tex;
static uint8_t menu_palette[768];

static int host_timer(void) { return (int)SDL_GetTicks(); }
static void host_palette(const uint8_t rgb[768]) { memcpy(menu_palette, rgb, 768); }

static void host_present(const uint8_t *buf) {
    static uint32_t rgb[IGN_FRONTEND_W * IGN_FRONTEND_H];
    for (int i = 0; i < IGN_FRONTEND_W * IGN_FRONTEND_H; i++) {
        const uint8_t *c = menu_palette + buf[i] * 3;
        rgb[i] = 0xFF000000u | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
    }
    SDL_UpdateTexture(menu_tex, NULL, rgb, IGN_FRONTEND_W * 4);
    SDL_RenderClear(shared_ren);
    SDL_RenderTexture(shared_ren, menu_tex, NULL, NULL);
    SDL_RenderPresent(shared_ren);
}

static const char *menu_data_dir;
static int host_sound_init(void) { return ign_audio_open_menu(menu_data_dir) == 0 ? 1 : 0; }
/* The menu's CD (MSCDEX in the original) is the GOG rip: Track02-08.ogg, i.e. an 8-track disc whose track 1 is data. */
static int host_cd_tracks(void) {
    char path[1200];
    snprintf(path, sizeof path, "%s/MUSIC/Track07.ogg", menu_data_dir);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return IGN_MUSIC_LAST;
}
/* Track 1 is the data track: MSCDEX refuses to play it, so the menu just tries again 2 s later, silently. */
static void host_cd_play(int track) {
    if (track >= 2) ign_music_play(menu_data_dir, track, 0);
}
static int host_sound_play(int sample, int vol, int rate) {
    int h = ign_audio_play(IGN_SND_MENU, 0, sample, 0, vol, rate);
    return h ? h : -1;
}
static void host_sound_stop_voice(int handle) {
    if (handle > 0) ign_audio_set(handle, 0, 0);
}
static void host_sound_free(int sample) { ign_audio_free(IGN_SND_MENU, 0, sample); }

/* SDL scancode -> DOS set-1 scancode as delivered by the keyboard ISR 0x55e04 (0xE0-prefixed keys + 0x80). */
static int dos_scancode(SDL_Scancode sc) {
    static const struct { SDL_Scancode sdl; uint8_t dos; } map[] = {
        {SDL_SCANCODE_ESCAPE, 0x01}, {SDL_SCANCODE_1, 0x02}, {SDL_SCANCODE_2, 0x03}, {SDL_SCANCODE_3, 0x04},
        {SDL_SCANCODE_4, 0x05}, {SDL_SCANCODE_5, 0x06}, {SDL_SCANCODE_6, 0x07}, {SDL_SCANCODE_7, 0x08},
        {SDL_SCANCODE_8, 0x09}, {SDL_SCANCODE_9, 0x0a}, {SDL_SCANCODE_0, 0x0b}, {SDL_SCANCODE_MINUS, 0x0c},
        {SDL_SCANCODE_EQUALS, 0x0d}, {SDL_SCANCODE_BACKSPACE, 0x0e}, {SDL_SCANCODE_TAB, 0x0f},
        {SDL_SCANCODE_Q, 0x10}, {SDL_SCANCODE_W, 0x11}, {SDL_SCANCODE_E, 0x12}, {SDL_SCANCODE_R, 0x13},
        {SDL_SCANCODE_T, 0x14}, {SDL_SCANCODE_Y, 0x15}, {SDL_SCANCODE_U, 0x16}, {SDL_SCANCODE_I, 0x17},
        {SDL_SCANCODE_O, 0x18}, {SDL_SCANCODE_P, 0x19}, {SDL_SCANCODE_LEFTBRACKET, 0x1a},
        {SDL_SCANCODE_RIGHTBRACKET, 0x1b}, {SDL_SCANCODE_RETURN, 0x1c}, {SDL_SCANCODE_LCTRL, 0x1d},
        {SDL_SCANCODE_A, 0x1e}, {SDL_SCANCODE_S, 0x1f}, {SDL_SCANCODE_D, 0x20}, {SDL_SCANCODE_F, 0x21},
        {SDL_SCANCODE_G, 0x22}, {SDL_SCANCODE_H, 0x23}, {SDL_SCANCODE_J, 0x24}, {SDL_SCANCODE_K, 0x25},
        {SDL_SCANCODE_L, 0x26}, {SDL_SCANCODE_SEMICOLON, 0x27}, {SDL_SCANCODE_APOSTROPHE, 0x28},
        {SDL_SCANCODE_GRAVE, 0x29}, {SDL_SCANCODE_LSHIFT, 0x2a}, {SDL_SCANCODE_BACKSLASH, 0x2b},
        {SDL_SCANCODE_Z, 0x2c}, {SDL_SCANCODE_X, 0x2d}, {SDL_SCANCODE_C, 0x2e}, {SDL_SCANCODE_V, 0x2f},
        {SDL_SCANCODE_B, 0x30}, {SDL_SCANCODE_N, 0x31}, {SDL_SCANCODE_M, 0x32}, {SDL_SCANCODE_COMMA, 0x33},
        {SDL_SCANCODE_PERIOD, 0x34}, {SDL_SCANCODE_SLASH, 0x35}, {SDL_SCANCODE_RSHIFT, 0x36},
        {SDL_SCANCODE_KP_MULTIPLY, 0x37}, {SDL_SCANCODE_LALT, 0x38}, {SDL_SCANCODE_SPACE, 0x39},
        {SDL_SCANCODE_CAPSLOCK, 0x3a}, {SDL_SCANCODE_F1, 0x3b}, {SDL_SCANCODE_F2, 0x3c}, {SDL_SCANCODE_F3, 0x3d},
        {SDL_SCANCODE_F4, 0x3e}, {SDL_SCANCODE_F5, 0x3f}, {SDL_SCANCODE_F6, 0x40}, {SDL_SCANCODE_F7, 0x41},
        {SDL_SCANCODE_F8, 0x42}, {SDL_SCANCODE_F9, 0x43}, {SDL_SCANCODE_F10, 0x44},
        {SDL_SCANCODE_NUMLOCKCLEAR, 0x45}, {SDL_SCANCODE_SCROLLLOCK, 0x46}, {SDL_SCANCODE_KP_7, 0x47},
        {SDL_SCANCODE_KP_8, 0x48}, {SDL_SCANCODE_KP_9, 0x49}, {SDL_SCANCODE_KP_MINUS, 0x4a},
        {SDL_SCANCODE_KP_4, 0x4b}, {SDL_SCANCODE_KP_5, 0x4c}, {SDL_SCANCODE_KP_6, 0x4d},
        {SDL_SCANCODE_KP_PLUS, 0x4e}, {SDL_SCANCODE_KP_1, 0x4f}, {SDL_SCANCODE_KP_2, 0x50},
        {SDL_SCANCODE_KP_3, 0x51}, {SDL_SCANCODE_KP_0, 0x52}, {SDL_SCANCODE_KP_PERIOD, 0x53},
        {SDL_SCANCODE_F11, 0x57}, {SDL_SCANCODE_F12, 0x58}, {SDL_SCANCODE_KP_ENTER, 0x9c},
        {SDL_SCANCODE_RCTRL, 0x9d}, {SDL_SCANCODE_KP_DIVIDE, 0xb5}, {SDL_SCANCODE_RALT, 0xb8},
        {SDL_SCANCODE_HOME, 0xc7}, {SDL_SCANCODE_UP, 0xc8}, {SDL_SCANCODE_PAGEUP, 0xc9},
        {SDL_SCANCODE_LEFT, 0xcb}, {SDL_SCANCODE_RIGHT, 0xcd}, {SDL_SCANCODE_END, 0xcf},
        {SDL_SCANCODE_DOWN, 0xd0}, {SDL_SCANCODE_PAGEDOWN, 0xd1}, {SDL_SCANCODE_INSERT, 0xd2},
        {SDL_SCANCODE_DELETE, 0xd3},
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (map[i].sdl == sc) return map[i].dos;
    return 0;
}

/* IGN_KEYS="ms:Key,ms:Key,...": scripted key presses (SDL key names, or "quit"), each `ms` after the previous one,
 * pushed as SDL events so the menu and the race windows react as to the keyboard. For automated end-to-end runs. */
static char *scripted_keys;
static Uint32 SDLCALL push_scripted_key(void *user, SDL_TimerID id, Uint32 interval) {
    (void)user;
    (void)id;
    (void)interval;
    char *tok = scripted_keys, *colon = tok ? strchr(tok, ':') : NULL;
    if (!colon) return 0;
    char *end = strchr(colon, ',');
    if (end) *end = 0;
    SDL_Event ev = {0};
    if (!strcmp(colon + 1, "quit"))
        ev.type = SDL_EVENT_QUIT;
    else {
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.scancode = SDL_GetScancodeFromName(colon + 1);
        ev.key.key = SDL_GetKeyFromScancode(ev.key.scancode, SDL_KMOD_NONE, false);
        ev.key.down = true;
    }
    SDL_PushEvent(&ev);
    if (ev.type == SDL_EVENT_KEY_DOWN) {
        SDL_Event up = ev;
        up.type = SDL_EVENT_KEY_UP;
        up.key.down = false;
        SDL_PushEvent(&up);
    }
    scripted_keys = end ? end + 1 : NULL;
    return scripted_keys ? (Uint32)atoi(scripted_keys) : 0;
}

static void start_scripted_keys(void) {
    const char *env = getenv("IGN_KEYS");
    if (!env || !*env || !(scripted_keys = SDL_strdup(env))) return;
    if (!SDL_InitSubSystem(SDL_INIT_EVENTS)) return;
    SDL_AddTimer((Uint32)atoi(scripted_keys), push_scripted_key, NULL);
}

/* Menu, then the race it configures, then back to the menu (sub_00020d60). */
static int run_frontend(const char *data_dir) {
    menu_data_dir = data_dir;
    /* The same user folder as the race's records (session_setup): IGN_DOS.BTZ and GHOSTS live there; the game
     * folder's IGN_DOS.BTZ is only read when the user folder has none. */
    const char *env = getenv("IGN_SAVE_DIR");
    char *pref = env ? NULL : SDL_GetPrefPath("UDS", "Ignition");
    char save_dir[1024];
    snprintf(save_dir, sizeof save_dir, "%s", env ? env : pref ? pref : "");
    SDL_free(pref);
    if (!save_dir[0]) {
        fprintf(stderr, "no user folder for the settings: %s\n", SDL_GetError());
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (!SDL_CreateWindowAndRenderer("Ignition", WINDOW_W, WINDOW_W * 3 / 4, SDL_WINDOW_RESIZABLE, &shared_win,
                                     &shared_ren)) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(shared_ren, 1);
    start_scripted_keys();
    IgnFrontendHost host = {
        .timer_ms = host_timer,
        .set_palette = host_palette,
        .present = host_present,
        .sound_init = host_sound_init,
        .sound_play = host_sound_play,
        .sound_stop_voice = host_sound_stop_voice,
        .sound_silence = ign_audio_silence,
        .sound_free = host_sound_free,
        .sound_volume = ign_audio_set_master,
        .sound_shutdown = ign_audio_close,
        .cd_tracks = host_cd_tracks,
        .cd_play = host_cd_play,
        .cd_stop = ign_music_stop,
        .cd_volume = ign_music_set_volume,
        .cd_busy = ign_music_playing,
        .save_dir = save_dir,
    };
    if (ign_frontend_init(data_dir, &host)) {
        fprintf(stderr, "cannot load the frontend data from %s\n", data_dir);
        return 1;
    }
    int rc = 0;
    while (!quit_requested) {
        menu_tex = SDL_CreateTexture(shared_ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                     IGN_FRONTEND_W, IGN_FRONTEND_H);
        SDL_SetTextureScaleMode(menu_tex, SDL_SCALEMODE_NEAREST);
        /* 320x200 shown at 4:3, as on a CRT. */
        SDL_SetRenderLogicalPresentation(shared_ren, IGN_FRONTEND_W, IGN_FRONTEND_W * 3 / 4,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
        memset(g_key_latch, 0, sizeof g_key_latch);
        ign_frontend_load();
        int result = IGN_FRONTEND_STAY;
        while (result == IGN_FRONTEND_STAY) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_EVENT_QUIT) quit_requested = true;
                if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) ign_frontend_key_down(dos_scancode(ev.key.scancode));
            }
            if (quit_requested) break;
            result = ign_frontend_frame();
        }
        ign_frontend_shutdown();
        SDL_DestroyTexture(menu_tex);
        menu_tex = NULL;
        if (result != IGN_FRONTEND_RACE) break;

        /* The settings block (ign_dos.btz) drives the race: track, player car, field size, difficulty, gearbox. */
        char car[16], rivals[16], difficulty[16], volume[16], cd_track[16];
        snprintf(car, sizeof car, "%d", (int)ign_cfg_get(CFG_CAR_SLOT));
        int racers = (int)ign_cfg_get(CFG_RACERS);
        snprintf(rivals, sizeof rivals, "%d", racers > 1 ? racers - 1 : 0);
        snprintf(difficulty, sizeof difficulty, "%d", (int)ign_cfg_get(CFG_DIFFICULTY));
        char *argv[24];
        int argc = 0;
        argv[argc++] = "ignition";
        argv[argc++] = "--car";
        argv[argc++] = car;
        argv[argc++] = "--rivals";
        argv[argc++] = rivals;
        argv[argc++] = "--difficulty";
        argv[argc++] = difficulty;
        if (ign_cfg_get(CFG_GEARBOX) == 0) argv[argc++] = "--manual";
        /* GAME TYPE: 2 TIME TRIAL (ghost and records of the session), 3 PURSUE MODE (elimination). */
        if (ign_cfg_get(CFG_GAME_MODE) == 2) argv[argc++] = "--time-trial";
        else if (ign_cfg_get(CFG_GAME_MODE) == 3) argv[argc++] = "--elimination";
        /* SOUND OPTIONS: CD VOLUME 0..10 and CD TRACK (-1 random, 0 the level's track, n). */
        snprintf(volume, sizeof volume, "%d", (int)ign_cfg_get(CFG_MUSIC_VOL));
        argv[argc++] = "--music-volume";
        argv[argc++] = volume;
        argv[argc++] = "--music-track";
        if (ign_cfg_get(CFG_CD_TRACK) == -1) argv[argc++] = "random";
        else {
            snprintf(cd_track, sizeof cd_track, "%d", (int)ign_cfg_get(CFG_CD_TRACK));
            argv[argc++] = cd_track;
        }
        argv[argc++] = (char *)ign_frontend_level((int)ign_cfg_get(CFG_TRACK));
        argv[argc] = NULL;
        printf("menu: race on %s, car %s, %s rivals\n", argv[argc - 1], car, rivals);
        rc = run_game(argc, argv);
        if (rc) break;
        if (!quit_requested) printf("menu: back from the race\n");
    }
    ign_frontend_destroy();
    SDL_DestroyRenderer(shared_ren);
    SDL_DestroyWindow(shared_win);
    SDL_Quit();
    return rc;
}

int main(int argc, char **argv) {
    int menu = argc == 1;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--menu")) menu = 1;
    if (!menu) return run_game(argc, argv);
    const char *data_dir = getenv("IGNITION_DATA");
    return run_frontend(data_dir ? data_dir : DEFAULT_DATA_DIR);
}
