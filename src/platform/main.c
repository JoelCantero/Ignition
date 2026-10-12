#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../game/ai.h"
#include "../game/assets.h"
#include "../game/car.h"
#include "../game/race.h"
#include "../game/track.h"
#include "../render/raster.h"

#define DEFAULT_DATA_DIR "/Users/Shared/Epic Games/Ignition/ignition"
#define WINDOW_SCALE 3
#define SIM_DT ((float)IGN_PHYS_DT)
#define CHASE_DIST 420.0f
#define CHASE_HEIGHT 520.0f
#define CHASE_PITCH 0.85f

static IgnFrame frame;

static void to_rgb(const IgnFrame *fb, const IgnPalette *pal, uint32_t *out) {
    uint32_t lut[256];
    for (int i = 0; i < 256; i++)
        lut[i] = 0xFF000000u | pal->rgb[i][0] << 16 | pal->rgb[i][1] << 8 | pal->rgb[i][2];
    for (int i = 0; i < IGN_SCREEN_W * IGN_SCREEN_H; i++) out[i] = lut[fb->color[i]];
}

static int write_ppm(const char *path, const IgnFrame *fb, const IgnPalette *pal) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", IGN_SCREEN_W, IGN_SCREEN_H);
    for (int i = 0; i < IGN_SCREEN_W * IGN_SCREEN_H; i++) fwrite(pal->rgb[fb->color[i]], 1, 3, f);
    return fclose(f);
}

static void chase_camera(const IgnCar *car, IgnCamera *cam) {
    cam->yaw = car->heading;
    cam->pitch = CHASE_PITCH;
    cam->x = car->x - sinf(car->heading) * CHASE_DIST;
    cam->z = car->z - cosf(car->heading) * CHASE_DIST;
    cam->y = car->y - CHASE_HEIGHT;
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

/* Spawns rivals with the other CARS.PLC models in grid slots 1..want. */
static void spawn_rivals(const IgnLevel *lv, const IgnTrack *t, const IgnCar *player, int want) {
    for (int i = 0; i < lv->cars.nobj && nrivals < want && nrivals < MAX_RIVALS; i++) {
        const IgnObject *o = &lv->cars.objs[i];
        int id = (int)(o->type / 10);
        if (o->type % 10 || id == player->id) continue;
        float x, z, h;
        grid_pose(lv, t, nrivals + 1, want + 1, &x, &z, &h);
        if (ign_car_init(&rivals[nrivals], lv, id, x, z, h)) continue;
        ign_ai_init(&rival_ai[nrivals], nrivals);
        nrivals++;
    }
}

static int tick_rivals(const IgnLevel *lv, const IgnTrack *t, IgnCar *player) {
    IgnCar *all[MAX_RIVALS + 1] = {player};
    for (int i = 0; i < nrivals; i++) all[i + 1] = &rivals[i];
    ign_ai_set_cars(all, nrivals + 1);
    for (int i = 0; i < nrivals; i++) {
        IgnInput in;
        if (!rivals[i].wrecked) {
            ign_ai_drive(&rival_ai[i], &rivals[i], t, &in);
            ign_car_tick(&rivals[i], lv, &in);
            ign_car_progress(&rivals[i], t);
        }
        all[i + 1] = &rivals[i];
    }
    int hits = ign_cars_collide(all, nrivals + 1);
    ign_race_step(&race, all, nrivals + 1);
    return hits;
}

static void draw(const IgnLevel *lv, const IgnCar *car, const IgnCamera *cam) {
    ign_frame_clear(&frame);
    ign_render_track(&frame, lv, cam);
    for (int i = 0; i < nrivals; i++) ign_render_car(&frame, lv, &rivals[i], cam);
    ign_render_car(&frame, lv, car, cam);
}

int main(int argc, char **argv) {
    const char *data_dir = getenv("IGNITION_DATA");
    if (!data_dir) data_dir = DEFAULT_DATA_DIR;
    const char *level = "USA";
    const char *shot = NULL;
    float sim_seconds = 0.0f, sim_steer = 0.0f;
    int autopilot = 0, want_rivals = -1, sim_turbo = 0, race_mode = 0;
    IgnCamera cam = {0};
    int fixed_cam = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 1 < argc)
            shot = argv[++i];
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
        } else if (!strcmp(argv[i], "--sim") && i + 2 < argc) {
            sim_seconds = strtof(argv[++i], NULL);
            i++;
            autopilot = !strcmp(argv[i], "auto");
            sim_turbo = !strcmp(argv[i], "turbo");
            sim_steer = autopilot || sim_turbo ? 0 : strtof(argv[i], NULL);
        } else if (!strcmp(argv[i], "--difficulty") && i + 1 < argc) {
            ign_set_difficulty(atoi(argv[++i]));
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
    ign_ai_set_track(level);
    IgnCar car;
    int want = want_rivals >= 0 ? want_rivals : (shot ? 0 : 3);
    if (want > MAX_RIVALS) want = MAX_RIVALS;
    float sx, sz, sh;
    grid_pose(&lv, &track, 0, want + 1, &sx, &sz, &sh);
    if (ign_car_init(&car, &lv, ign_first_car_id(&lv), sx, sz, sh)) {
        fprintf(stderr, "no car models in CARS.PLC\n");
        return 1;
    }
    spawn_rivals(&lv, &track, &car, want);
    ign_race_init(&race, race_mode, nrivals + 1);
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
        IgnInput in = {.accel = sim_seconds > 0, .left = sim_steer < 0, .right = sim_steer > 0,
                       .turbo = sim_turbo};
        int hits = 0, air = 0, car_hits = 0;
        for (float t = 0; t < sim_seconds; t += SIM_DT) {
            if (autopilot) ign_ai_drive(&player_ai, &car, &track, &in);
            if (!car.wrecked) {
                ign_car_tick(&car, &lv, &in);
                ign_car_progress(&car, &track);
            }
            car_hits += tick_rivals(&lv, &track, &car);
            hits += car.wall_hit;
            air += car.airborne;
            if (getenv("IGN_TRACE"))
                fprintf(stderr, "t=%.3f x=%.0f z=%.0f h=%.1f g=%.1f v=%.2f air=%d type=0x%x drive=%.0f/%.0f turbo=%d/%.0f kmh=%.0f\n",
                        t, car.x, car.z, car.height, car.ground, car.fall_speed, car.airborne,
                        car.surface_type, car.body.drive, car.body.fmax_f, car.turbo_mode,
                        car.turbo_charge, car.kmh);
        }
        printf("car at %.0f %.0f %.0f heading %.2f %.0f km/h gear %d surface 0x%x wall hits %d "
               "airborne ticks %d section %d/%d phase %d lap %d\n",
               car.x, car.y, car.z, car.heading, car.kmh, car.gear, car.surface_type, hits, air,
               car.section, track.count, car.phase, car.lap);
        printf("car-car contacts %d\n", car_hits);
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
            printf("rival %d (car %d): section %d phase %d lap %d %.0f km/h\n", i, rivals[i].id,
                   rivals[i].section, rivals[i].phase, rivals[i].lap, rivals[i].kmh);
        if (!fixed_cam) chase_camera(&car, &cam);
        draw(&lv, &car, &cam);
        int rc = write_ppm(shot, &frame, &lv.palette);
        ign_free_level(&lv);
        return rc ? 1 : 0;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win;
    SDL_Renderer *ren;
    if (!SDL_CreateWindowAndRenderer("Ignition", IGN_SCREEN_W * WINDOW_SCALE,
                                     IGN_SCREEN_H * WINDOW_SCALE, SDL_WINDOW_RESIZABLE, &win,
                                     &ren)) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(ren, 1);
    SDL_SetRenderLogicalPresentation(ren, IGN_SCREEN_W, IGN_SCREEN_H,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, IGN_SCREEN_W, IGN_SCREEN_H);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    static uint32_t rgb[IGN_SCREEN_W * IGN_SCREEN_H];

    bool free_cam = false;
    char typed[16] = {0};
    SDL_StartTextInput(win);
    float accumulator = 0.0f;
    Uint64 last = SDL_GetTicks();
    for (bool running = true; running;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT ||
                (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE))
                running = false;
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_F1) free_cam = !free_cam;
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
            };
            for (accumulator += dt; accumulator >= SIM_DT; accumulator -= SIM_DT) {
                if (!car.wrecked) {
                    ign_car_tick(&car, &lv, &in);
                    ign_car_progress(&car, &track);
                }
                tick_rivals(&lv, &track, &car);
            }
            chase_camera(&car, &cam);
            char title[128];
            snprintf(title, sizeof title, "Ignition - lap %d - section %d/%d - %.0f km/h - turbo %.0f%%%s",
                     car.lap + 1, car.section, track.count, car.kmh, car.turbo_charge,
                     car.turbo_mode ? " ON" : "");
            SDL_SetWindowTitle(win, title);
        }

        draw(&lv, &car, &cam);
        to_rgb(&frame, &lv.palette, rgb);
        SDL_UpdateTexture(tex, NULL, rgb, IGN_SCREEN_W * 4);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    ign_free_level(&lv);
    return 0;
}
