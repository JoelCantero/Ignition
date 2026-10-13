#include "sound.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"

#define MAX_CARS 16
#define MAX_AMBIENT 64

/* 0xc0bfc: CARS folder of each model. */
static const char *const MODEL[11] = {"COOPER", "PORSCHE", "JEEP",  "COP",   "MUSTANG", "SCHOOL",
                                      "VAN",    "VW",      "TRUCK", "DODGE", "NASCAR"};

static struct {
    int on;
    uint8_t engine[MAX_CARS][4][200]; /* ENGINE.INF -> car+0x658/+0x720/+0x7e8/+0x8b0 */
    int engine_ch[MAX_CARS][2];       /* car+0x650/+0x654 */
    int roll_ch[MAX_CARS][2];         /* car+0x978/+0x97c */
    int skid_ch[MAX_CARS], skid_class[MAX_CARS], skid_on[MAX_CARS]; /* car+0x99c, car+0x168, car+0x298 */
    int gear[MAX_CARS], turbo[MAX_CARS];
    double charge;
    int weather_ch;
    int ambient_ch[MAX_AMBIENT], nambient;
    float ambient_x[MAX_AMBIENT], ambient_z[MAX_AMBIENT];
    int beep;      /* [0x1fedfc] */
    int austria;   /* [0x1fef10] == 4 */
    float lx, lz;  /* listener: the viewed car */
} S;

static int clampv(double v) { return v > 0x10000 ? 0x10000 : v < 0 ? 0 : (int)v; }

/* sub_00049a8c: the volume falls linearly to 0 at 1500 of 0.8 x the distance to the viewed car. */
static double atten(float x, float z) {
    double dx = fabs(x - S.lx) * 0.8, dz = fabs(z - S.lz) * 0.8;
    double a = (1500.0 - sqrt(dx * dx + dz * dz)) * (1.0 / 1500.0);
    return a < 0 ? 0 : a;
}

static void play_at(int group, int sub, int idx, int loops, int vol, int freq, float x, float z) {
    ign_audio_play(group, sub, idx, loops, (int)(vol * atten(x, z)), freq);
}

static float rear_x(const IgnCar *c) { return c->x + (float)(cos(c->body.heading) * c->rear); }
static float rear_z(const IgnCar *c) { return c->z + (float)(sin(c->body.heading) * c->rear); }

int ign_sound_start(const char *data_dir, const char *level, const IgnLevel *lv, IgnCar *const *cars, int ncars,
                    const IgnWeather *w) {
    memset(&S, 0, sizeof S);
    if (ncars > MAX_CARS) ncars = MAX_CARS;
    const char *models[MAX_CARS];
    for (int i = 0; i < ncars; i++) {
        models[i] = MODEL[cars[i]->spec % 11];
        char path[1024];
        snprintf(path, sizeof path, "%s/CARS/%s/SOUND/ENGINE.INF", data_dir, models[i]);
        FILE *f = fopen(path, "rb");
        if (f) {
            if (fread(S.engine[i], 1, sizeof S.engine[i], f) != sizeof S.engine[i]) memset(S.engine[i], 0, 800);
            fclose(f);
        }
    }
    if (ign_audio_open(data_dir, level, models, ncars)) return -1;
    S.on = 1;
    S.austria = !strcmp(level, "AUSTRIA");
    /* sub_0002a4ec: two engine loops of the car's bank at 5000 Hz, two silent rolling loops. */
    for (int i = 0; i < ncars; i++) {
        for (int k = 0; k < 2; k++) {
            S.engine_ch[i][k] = ign_audio_play(IGN_SND_CAR, i, k, -1, 0x10000, 0x1388);
            S.roll_ch[i][k] = ign_audio_play(IGN_SND_GENERAL, IGN_SND_ROLL, k, -1, 0, 0x1388);
        }
        S.skid_class[i] = -1;
        S.gear[i] = cars[i]->gear;
    }
    /* sub_0002489c 0x2525e: PLC types 0x190-0x1f3 loop level sound type - 0x190 at 11000 Hz; the first 0x165 object
     * plays sound 0. */
    int first165 = -1;
    for (int o = 0; o < lv->track.nobj; o++)
        if (lv->track.objs[o].type == 0x165 && first165 < 0) first165 = o;
    for (int o = 0; o < lv->track.nobj && S.nambient < MAX_AMBIENT; o++) {
        uint32_t t = lv->track.objs[o].type;
        if ((t < 0x190 || t >= 0x1f3) && o != first165) continue;
        S.ambient_ch[S.nambient] = ign_audio_play(IGN_SND_LEVEL, 0, o == first165 ? 0 : (int)t - 0x190, -1, 0, 0x2af8);
        S.ambient_x[S.nambient] = (float)lv->track.objs[o].pos[0];
        S.ambient_z[S.nambient] = (float)lv->track.objs[o].pos[2];
        S.nambient++;
    }
    /* sub_00026154: the rain (sound 0) or, in the USA, sound 2 loops silently until the zone fades it in. */
    if (w->kind == IGN_WEATHER_RAIN) S.weather_ch = ign_audio_play(IGN_SND_LEVEL, 0, 0, -1, 0, 0);
    else if (w->kind == IGN_WEATHER_WIND) S.weather_ch = ign_audio_play(IGN_SND_LEVEL, 0, 2, -1, 0, 0);
    S.charge = 100.0;
    return 0;
}

void ign_sound_stop(void) {
    if (S.on) ign_audio_close();
    S.on = 0;
}

void ign_sound_tick(IgnCar *const *cars, int ncars, IgnFx *fx) {
    for (int i = 0; i < ncars; i++) {
        IgnCar *c = cars[i];
        /* cars_update_all 0x2d882: a wall impact above 5 knocks with (impact - 5) x 2500. */
        if (c->wall_impact > 5.0)
            ign_fx_sound(fx, 0, IGN_SND_COLL, 3, clampv((c->wall_impact - 5.0) * 2500.0), 22000, c->x, c->z);
        /* car_vertical 0x2e57b and cars_update_all 0x2d0e1 (car+0x278): two thuds on landing. */
        if (c->landed) {
            ign_fx_sound(fx, 0, IGN_SND_COLL, 3, clampv(fabs(c->fall_speed) * 64.0 + 4096.0), 22000, c->x, c->z);
            ign_fx_sound(fx, 0, IGN_SND_COLL, 9, clampv(fabs(c->fall_speed * 8192.0) + 4096.0), 22000, c->x, c->z);
        }
        c->wall_impact = 0;
        c->landed = 0;
    }
}

void ign_sound_frame(IgnCar *const *cars, int ncars, int view, const IgnRace *race, double finish_clock,
                     const IgnWeather *w, IgnFx *fx) {
    if (!S.on) {
        fx->nsounds = 0;
        return;
    }
    if (ncars > MAX_CARS) ncars = MAX_CARS;
    S.lx = cars[view]->x, S.lz = cars[view]->z;
    int silent = finish_clock >= 120.0;

    for (int i = 0; i < ncars; i++) {
        const IgnCar *c = cars[i];
        double a = atten(c->x, c->z);
        /* sub_00050824: ENGINE.INF entry 2 x revs picks two rates (x600 + 20000 Hz) and volumes (x650). */
        if (!c->wrecked && !silent) {
            int idx = (int)c->revs * 2;
            idx = idx > 199 ? 199 : idx < 1 ? 1 : idx;
            for (int k = 0; k < 2; k++)
                ign_audio_set(S.engine_ch[i][k], clampv(S.engine[i][1 + 2 * k][idx - 1] * a * 650.0),
                              S.engine[i][2 * k][idx] * 600 + 20000);
        } else {
            for (int k = 0; k < 2; k++) ign_audio_set(S.engine_ch[i][k], 0, -1);
        }
        /* sub_00050464: rolling on surface (type mod 10, 9 -> 4) of the rear wheel; the distance factor is truncated
         * to an integer, so only the viewed car rolls audibly. Only classes 0 and 1 have a channel. */
        static const int ROLL[10] = {16384, 24576, 0, 24576, 0, 0, 0, 16384, 0, 0};
        int cls = c->wheel_type[2] % 10;
        if (cls == 9) cls = 4;
        if (cls < 0) cls = 0;
        int loud = (int)((1500.0 - hypot(trunc(fabs(c->x - S.lx)), trunc(fabs(c->z - S.lz)))) / 1500.0);
        if (loud < 0 || c->wrecked) loud = 0;
        for (int k = 0; k < 2; k++)
            ign_audio_set(S.roll_ch[i][k], k == cls ? clampv(loud * ROLL[cls] * c->kmh * 0.01) : 0,
                          (int)(c->kmh * 100.0 + ROLL[cls]));
        /* sub_0003731c: the skid loop of surface (type mod 9), started at kmh x 100 + rate and then kept at
         * kmh x 25 + rate; 0xc35a0 volumes, 0xc3578 rates. */
        static const int SKID_RATE[10] = {10000, 12000, 0, 0, 12000, 0, 0, 10000, 0, 0};
        static const int SKID_VOL[10] = {65536, 32768, 0, 0, 32768, 0, 0, 65536, 0, 0};
        if (c->skidding && !c->wrecked) {
            int sc = (c->wheel_type[2] < 0 ? 0 : c->wheel_type[2]) % 9;
            if (sc == S.skid_class[i] && S.skid_on[i]) {
                ign_audio_set(S.skid_ch[i], -1, (int)(c->kmh * 25.0 + SKID_RATE[sc]));
            } else {
                ign_audio_set(S.skid_ch[i], -1, 0);
                S.skid_ch[i] = ign_audio_play(IGN_SND_GENERAL, IGN_SND_SKID, sc, -1,
                                              (int)(SKID_VOL[sc] * atten(c->x, c->z)),
                                              (int)(c->kmh * 100.0 + SKID_RATE[sc]));
                S.skid_class[i] = sc;
            }
            S.skid_on[i] = 1;
        } else if (S.skid_on[i]) {
            ign_audio_set(S.skid_ch[i], -1, 0);
            S.skid_on[i] = 0;
        }
        /* car_gearbox 0x50400 / car_input 0x4d7c2: a gear change clicks (DIV 02_GEAR). */
        if (c->gear != S.gear[i] && !c->wrecked)
            play_at(0, IGN_SND_DIV, 2, 0, 0xd000, 22000, rear_x(c), rear_z(c));
        S.gear[i] = c->gear;
        /* car_input 0x4d9bc and sub_0004e688: the turbo whoosh, the model's own cry (BOOST 4 + model) and the
         * siren of the police car (model 3, played twice). */
        if (c->turbo_mode && !S.turbo[i]) {
            play_at(0, IGN_SND_BOOST, 0, 0, 0xa000, 22000, rear_x(c), rear_z(c));
            play_at(0, IGN_SND_BOOST, 4 + c->spec % 11, 0, 0x10000, 22000, rear_x(c), rear_z(c));
            if (c->spec == 3) play_at(0, IGN_SND_BOOST, 3, 1, 0xe000, 0, c->x, c->z);
        }
        S.turbo[i] = c->turbo_mode;
    }
    /* car_turbo 0x4e5b4: the player's turbo is full again (BOOST 02_OK). */
    if (cars[0]->turbo_charge >= 100.0 && S.charge < 100.0)
        play_at(0, IGN_SND_BOOST, 2, 0, 0x10000, 22000, rear_x(cars[0]), rear_z(cars[0]));
    S.charge = cars[0]->turbo_charge;

    /* sub_000360f0: each emitter at 0.75 x (1500 - distance) / 1500, the first two of AUSTRIA at full scale. */
    for (int k = 0; k < S.nambient; k++) {
        double dx = trunc(fabs(S.ambient_x[k] - S.lx)), dz = trunc(fabs(S.ambient_z[k] - S.lz));
        double a = (1500.0 - sqrt(dx * dx + dz * dz)) * (1.0 / 1500.0);
        if (a < 0 || silent) a = 0;
        ign_audio_set(S.ambient_ch[k], (int)(a * (S.austria && k < 2 ? 1.0 : 0.75) * 65536.0), -1);
    }
    if (S.weather_ch) ign_audio_set(S.weather_ch, w->volume, -1);

    /* sub_0004a320 0x4b7a6: two red beeps (clock 100 and 150) and the green one at the start. */
    const IgnCar *vc = cars[view];
    if (race->clock >= 100.0 && race->clock < 150.0 && S.beep == 0)
        play_at(0, IGN_SND_DIV, 0, 0, 0x10000, 22000, rear_x(vc), rear_z(vc)), S.beep = 1;
    else if (race->clock >= 150.0 && race->clock < 200.0 && S.beep == 1)
        play_at(0, IGN_SND_DIV, 0, 0, 0x10000, 22000, rear_x(vc), rear_z(vc)), S.beep = 2;
    else if (race->clock == -1.0 && race->total < 250.0 && S.beep == 2)
        play_at(0, IGN_SND_DIV, 1, 0, 0x10000, 22000, rear_x(vc), rear_z(vc)), S.beep = 3;

    for (int k = 0; k < fx->nsounds; k++) {
        const IgnSoundEvent *e = &fx->sounds[k];
        play_at(e->group, e->sub, e->idx, 0, e->vol, e->freq, e->x, e->z);
    }
    fx->nsounds = 0;
}
