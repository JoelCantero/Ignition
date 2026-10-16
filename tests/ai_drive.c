#include "game/ai.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    static const char *names[] = {"CANADA", "USA", "CARIB", "BRAZIL", "AUSTRIA", "ICELAND", "JAPAN"};
    int tid, difficulty, count, ncars;
    unsigned seed;
    while (scanf("%d %d %d %d %u", &tid, &difficulty, &count, &ncars, &seed) == 5) {
        if (tid < 0 || tid > 6 || count < 9 || count > 1000 || ncars < 1 || ncars > 16) return 2;
        IgnAi ai;
        IgnFx fx;
        ign_ai_init(&ai, 0);
        ign_fx_init(&fx);
        fx.seed = seed;
        if (scanf("%lf %lf %d %d %d %d %d %lf", &ai.side, &ai.steer, &ai.stuck, &ai.accel,
                  &ai.section, &ai.route_choice, &ai.route_selector, &ai.route_random) != 8)
            return 2;
        if (scanf("%lf %lf %lf %lf %lf %lf %lf %lf %d", &ai.p.s00, &ai.p.s08, &ai.p.s10,
                  &ai.p.s18, &ai.p.s20, &ai.p.s28, &ai.p.s38, &ai.p.s40, &ai.p.s48) != 9)
            return 2;
        IgnCar *cars[16] = {0};
        for (int i = 0; i < ncars; i++) {
            cars[i] = calloc(1, sizeof *cars[i]);
            if (!cars[i]) return 2;
            IgnCar *c = cars[i];
            if (scanf("%d %d %d %d %f %f %f %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
                      &c->section, &c->spec, &c->airborne, &c->flat, &c->x, &c->z, &c->kmh,
                      &c->body.v_fwd, &c->drive_heading, &c->step_x, &c->step_z, &c->turbo_charge,
                      &c->ai_side, &c->axle[0][0], &c->axle[0][1], &c->axle[1][0], &c->axle[1][1]) != 17)
                return 2;
        }
        IgnTrack t = {.count = count};
        t.section_obj = calloc((size_t)count, sizeof *t.section_obj);
        t.section_obj_b = calloc((size_t)count, sizeof *t.section_obj_b);
        t.ai_records = calloc((size_t)count, sizeof *t.ai_records);
        t.ai_record_count = calloc((size_t)count, sizeof *t.ai_record_count);
        if (!t.section_obj || !t.section_obj_b || !t.ai_records || !t.ai_record_count) return 2;
        for (int s = 0; s < count; s++) {
            int slots;
            if (scanf("%d %d %d", &t.section_obj[s], &t.section_obj_b[s], &slots) != 3) return 2;
            for (int k = 0; k < slots; k++) {
                int valid, type, surface;
                uint32_t bits;
                if (scanf("%d %d %d", &valid, &type, &surface) != 3) return 2;
                int first_type = t.ai_records[s][0][0].tri_type;
                int split = first_type == 1 || first_type == 2;
                IgnAiRouteRecord *r = &t.ai_records[s][k && !split][k && split];
                r->valid = valid;
                r->geometry_valid = 1;
                r->tri_type = type;
                r->surface = surface;
                r->object = r->plc = k && !split ? t.section_obj_b[s] : t.section_obj[s];
                for (int j = 0; j < 6; j++)
                    if (scanf("%d", &r->point[j]) != 1) return 2;
                for (int j = 0; j < 4; j++)
                    if (scanf("%d", &r->inset[j]) != 1) return 2;
                if (scanf("%u", &bits) != 1) return 2;
                memcpy(&r->heading[1], &bits, sizeof bits);
                t.ai_record_count[s][k && !split]++;
            }
        }
        ign_ai_set_track(names[tid]);
        ign_set_difficulty(difficulty);
        ign_ai_set_cars(cars, ncars);
        IgnInput in;
        ign_ai_drive(&ai, cars[0], &t, &fx, &in);
        printf("%d %d %d %d %d %d %d %d %d %u %a %a %a\n", in.a_accel, in.a_brake,
               in.a_steer, in.turbo, ai.stuck, ai.accel, ai.section, ai.route_choice,
               ai.route_selector, fx.seed, ai.side, ai.steer, ai.route_random);
        ign_free_track(&t);
        for (int i = 0; i < ncars; i++) free(cars[i]);
    }
    return ferror(stdin) ? 2 : 0;
}
