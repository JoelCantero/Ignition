/* Collision objects, special objects and POS geometry on the seven real tracks (needs the game data). */
#include "game/assets.h"
#include "game/fx.h"
#include "game/pos.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "%s:%d: ", __FILE__, __LINE__);                    \
            fprintf(stderr, __VA_ARGS__);                                      \
            fputc('\n', stderr);                                               \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int animated(const IgnLevel *lv, int obj) {
    int32_t off;
    memcpy(&off, lv->pos.data + (size_t)obj * 4, 4);
    return off != -1;
}

/* Centre of an object's first face that is not edge-on, engine coordinates; -1 if none. */
static int face_centre(const IgnObject *o, int32_t c[3]) {
    for (int f = 0; f < o->nf; f++) {
        int32_t p[3][3];
        for (int k = 0; k < 3; k++) ign_object_point(o, o->faces[f].v[k], p[k]);
        long long area = (long long)(p[1][0] - p[0][0]) * (p[2][2] - p[0][2]) -
                         (long long)(p[2][0] - p[0][0]) * (p[1][2] - p[0][2]);
        if (llabs(area) < 64) continue;
        for (int a = 0; a < 3; a++) c[a] = (p[0][a] + p[1][a] + p[2][a]) / 3;
        return 0;
    }
    return -1;
}

int main(int argc, char **argv) {
    const char *data = argc > 1 ? argv[1] : "/Users/Shared/Epic Games/Ignition/ignition";
    static const char *const TRACKS[7] = {"USA", "CANADA", "AUSTRIA", "BRAZIL", "CARIB", "ICELAND", "JAPAN"};
    static const int COLL[7] = {17, 22, 23, 0, 24, 9, 53};
    static IgnCollObjs colls;
    static IgnFx fx;
    for (int t = 0; t < 7; t++) {
        static IgnLevel lv;
        if (ign_load_level(data, TRACKS[t], &lv)) {
            fprintf(stderr, "cannot load %s from %s\n", TRACKS[t], data);
            return 2;
        }
        /* World creation 0x24f22: every collision object has a known kind. */
        int n = ign_coll_init(&colls, &lv.track);
        CHECK(n == COLL[t], "%s: %d collision objects, expected %d", TRACKS[t], n, COLL[t]);

        /* No POS-animated object (nor collision object) has SRF spans: the ground query never sees them, only
         * sub_0003383c and car_ground_contact do. */
        int spans = 0;
        for (int r = 0; r < lv.surface.nspan; r++) {
            uint32_t ob = rd32(lv.surface.spans + (size_t)r * 24 + 20);
            if (ob % 42 || ob / 42 >= (uint32_t)lv.surface.nengine) continue;
            int p = lv.surface.engine_to_plc[ob / 42];
            if (p >= 0 && (animated(&lv, p) || (lv.track.objs[p].type >= 0x96 && lv.track.objs[p].type < 0xc8)))
                spans++;
        }
        CHECK(spans == 0, "%s: %d SRF spans on moving objects", TRACKS[t], spans);

        /* Dropped from 100 above, each collision object lands and rests at most 15 above the ground's plane. */
        ign_fx_init(&fx);
        int rested = 0, airborne = 0;
        for (int i = 0; i < colls.n; i++) {
            IgnCollObj *o = &colls.o[i];
            o->flying = 1;
            o->pos[1] += 100;
            o->vel[0] = 1.5;
        }
        for (int frame = 0; frame < 600; frame++) ign_coll_frame(&colls, &lv, NULL, 0, &fx, 0, 0);
        for (int i = 0; i < colls.n; i++) {
            const IgnCollObj *o = &colls.o[i];
            CHECK(isfinite(o->pos[0]) && isfinite(o->pos[1]) && isfinite(o->pos[2]), "%s: object %d not finite",
                  TRACKS[t], o->obj);
            if (o->flying) {
                airborne++;
                continue;
            }
            IgnGroundHit hit;
            if (ign_ground_query(&lv, (int32_t)trunc(o->pos[0]), (int32_t)trunc(o->pos[1]) + 150,
                                 (int32_t)trunc(o->pos[2]), &hit) == 0) {
                double ground = ign_coll_ground_up(&hit, o->pos[0], o->pos[2]);
                /* The last frame may still move it in x/z: on a slope the plane can end a little above it. */
                CHECK(o->pos[1] > ground - 20.0 && o->pos[1] <= ground + 15.0 + 1e-9,
                      "%s: object %d rests at %.1f, ground %.1f", TRACKS[t], o->obj, o->pos[1], ground);
                rested++;
            }
            /* The engine pose follows the record. */
            const IgnObject *e = &lv.track.objs[o->obj];
            CHECK(e->pos[0] == (int32_t)trunc(o->pos[0]) - IGN_SRF_ORIGIN && e->pos[1] == -(int32_t)trunc(o->pos[1]),
                  "%s: object %d pose not moved", TRACKS[t], o->obj);
        }
        CHECK(rested == colls.n, "%s: %d of %d objects rest on the ground (%d still flying)", TRACKS[t], rested,
              colls.n, airborne);

        /* sub_0003383c finds each special object with faces under one of its faces' centres, before and after
         * 200 POS frames move it. */
        int specials = 0, found = 0;
        for (int step = 0; step < 2; step++) {
            for (int i = 0; i < lv.track.nobj; i++) {
                const IgnObject *o = &lv.track.objs[i];
                int type = (int)(o->type & 0xfff);
                int32_t c[3];
                if (type < 0x12c || type >= 0x15e || face_centre(o, c)) continue;
                specials++;
                int q = ign_special_query(&lv, c[0], -o->pos[1], c[2], 0, 0);
                found += q >= 0x12c && q < 0x15e;
            }
            for (int f = 0; f < 200; f++) ign_pos_step(&lv.pos, &lv.track);
        }
        CHECK(found == specials, "%s: %d of %d special-object faces found", TRACKS[t], found, specials);
        /* Texture animations (0xfa): every frame's faces move tile by tile and are back after one full cycle. */
        static IgnTexAnims ta;
        int nanim = ign_texanim_init(&ta, &lv.track, 1), animated_faces = 0, cycled = 1;
        CHECK(nanim >= 0, "%s: texture animations failed", TRACKS[t]);
        for (int a = 0; a < ta.n; a++) {
            const IgnTexAnim *an = &ta.a[a];
            int nf = 0;
            for (int k = 0; k < an->n; k++)
                for (IgnFace **f = an->faces[k]; *f; f++) nf++;
            animated_faces += nf;
            IgnFace **saved = malloc(sizeof *saved * (size_t)(nf + 1));
            IgnFace *copy = malloc(sizeof *copy * (size_t)(nf + 1));
            int m = 0;
            for (int k = 0; k < an->n; k++)
                for (IgnFace **f = an->faces[k]; *f; f++) saved[m] = *f, copy[m++] = **f;
            IgnTexAnims one = ta;
            one.a = &ta.a[a], one.n = 1;
            /* Frames may repeat a tile (it then holds for two steps), so look for motion over the whole cycle. */
            int moved = 0;
            for (int s2 = 0; s2 < an->n; s2++) {
                ign_texanim_step(&one);
                for (int i = 0; i < m && s2 + 1 < an->n; i++) moved += memcmp(saved[i], &copy[i], sizeof copy[i]) != 0;
            }
            CHECK(an->n == 1 || moved > 0, "%s: animation %d did not move", TRACKS[t], a);
            for (int i = 0; i < m; i++) cycled &= !memcmp(saved[i], &copy[i], sizeof copy[i]);
            free(saved), free(copy);
        }
        CHECK(cycled, "%s: texture animation faces not back after a cycle", TRACKS[t]);
        ign_texanim_free(&ta);
        printf("%-8s collision objects %2d (rested %2d), special faces %2d/%2d, texture animations %d (%d faces)\n",
               TRACKS[t], n, rested, found, specials, nanim, animated_faces);
        ign_free_level(&lv);
    }
    if (failures) fprintf(stderr, "%d failures\n", failures);
    return failures != 0;
}
