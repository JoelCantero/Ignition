#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/pos.h"

static int face_id(const IgnObject *meshes, int count, const IgnFace *face) {
    int id = 0;
    for (int m = 0; m < count; m++)
        for (int f = 0; f < meshes[m].nf; f++, id++)
            if (&meshes[m].faces[f] == face) return id;
    return -1;
}

static int runner(void) {
    int steps, nm, no;
    while (scanf("%d", &steps) == 1) {
        if (scanf("%d%d", &nm, &no) != 2 || steps < 0 || steps > 1000 ||
            nm < 0 || nm > 4096 || no < 0 || no > 4096) {
            fprintf(stderr, "texanim: invalid case header\n");
            return 1;
        }
        IgnObject *meshes = calloc((size_t)nm + 1, sizeof *meshes);
        IgnObject *objects = calloc((size_t)no + 1, sizeof *objects);
        IgnTexAnims animations = {0};
        int rc = 1, total = 0;
        if (!meshes || !objects) goto cleanup;
        for (int m = 0; m < nm; m++) {
            int nf;
            if (scanf("%d", &nf) != 1 || nf < 0 || nf > 65536 - total) goto cleanup;
            total += nf;
            IgnFace *faces = calloc((size_t)nf + 1, sizeof *faces);
            if (!faces) goto cleanup;
            meshes[m].nf = nf;
            meshes[m].faces = faces;
            for (int f = 0; f < nf; f++) {
                if (scanf("%" SCNu32, &faces[f].flags) != 1) goto cleanup;
                for (int k = 0; k < 3; k++)
                    if (scanf("%" SCNd32, &faces[f].v[k]) != 1) goto cleanup;
                for (int k = 0; k < 6; k++)
                    if (scanf("%" SCNd32, &faces[f].uv[k]) != 1) goto cleanup;
                if (scanf("%" SCNu32, &faces[f].extra) != 1) goto cleanup;
            }
        }
        for (int o = 0; o < no; o++) {
            uint32_t type;
            int mesh;
            if (scanf("%" SCNu32 "%d", &type, &mesh) != 2 || mesh < 0 || mesh >= nm) goto cleanup;
            objects[o] = meshes[mesh];
            objects[o].type = type;
        }
        IgnModel model = {.nobj = no, .objs = objects};
        if (ign_texanim_init(&animations, &model, 1) < 0) goto cleanup;
        for (int step = 0; step < steps; step++) ign_texanim_step(&animations);
        printf("{\"animations\":[");
        for (int i = 0; i < animations.n; i++) {
            const IgnTexAnim *a = &animations.a[i];
            printf("%s{\"n\":%d,\"cur\":%d,\"prev\":%d,\"tiles\":[", i ? "," : "", a->n, a->cur, a->prev);
            for (int k = 0; k < a->n; k++)
                printf("%s[%" PRId32 ",%" PRId32 ",%" PRIu32 "]", k ? "," : "", a->uv[k][0], a->uv[k][1], a->page[k]);
            printf("],\"targets\":[");
            for (int k = 0; k < a->n; k++) {
                printf("%s[", k ? "," : "");
                for (int j = 0; a->faces[k][j]; j++) {
                    int id = face_id(meshes, nm, a->faces[k][j]);
                    if (id < 0) goto cleanup;
                    printf("%s%d", j ? "," : "", id);
                }
                printf("]");
            }
            printf("]}");
        }
        printf("],\"faces\":[");
        int id = 0;
        for (int m = 0; m < nm; m++)
            for (int f = 0; f < meshes[m].nf; f++, id++) {
                const IgnFace *face = &meshes[m].faces[f];
                printf("%s[%" PRIu32, id ? "," : "", face->flags);
                for (int k = 0; k < 3; k++) printf(",%" PRId32, face->v[k]);
                for (int k = 0; k < 6; k++) printf(",%" PRId32, face->uv[k]);
                printf(",%" PRIu32 "]", face->extra);
            }
        ign_texanim_free(&animations);
        if (animations.a || animations.n || animations.clock || animations.period) goto cleanup;
        ign_texanim_free(&animations);
        printf("],\"cleared\":true}\n");
        rc = 0;
cleanup:
        ign_texanim_free(&animations);
        if (meshes)
            for (int m = 0; m < nm; m++) free((void *)meshes[m].faces);
        free(meshes);
        free(objects);
        if (rc) {
            fprintf(stderr, "texanim: invalid input or allocation failure\n");
            return rc;
        }
    }
    if (!feof(stdin)) {
        fprintf(stderr, "texanim: invalid input\n");
        return 1;
    }
    return 0;
}

static int check(int condition, const char *message) {
    if (condition) return 0;
    fprintf(stderr, "FAIL %s\n", message);
    return 1;
}

static int unit(void) {
    IgnFace marker[2] = {{.flags = 0x11}, {.flags = 0x11}};
    IgnFace shared[3] = {{.flags = 0x11}, {.flags = 0x14}, {.flags = 0x11}};
    for (int k = 0; k < 3; k++) {
        marker[1].uv[k * 2] = 0x8000;
        shared[0].uv[k * 2] = shared[0].uv[k * 2 + 1] = 100;
    }
    IgnObject objects[3] = {{.type = 0xFA, .nf = 2, .faces = marker},
                            {.nf = 3, .faces = shared}, {.nf = 3, .faces = shared}};
    IgnModel model = {.nobj = 3, .objs = objects};
    IgnTexAnims animations;
    int failed = check(ign_texanim_init(&animations, &model, 2) == 1, "construct one animation");
    if (failed) return 1;
    failed |= check(animations.a[0].n == 2 && animations.a[0].cur == 1 && animations.a[0].prev == 0,
                    "initialize frame count and cursors");
    int targets = 0;
    while (animations.a[0].faces[0][targets]) targets++;
    failed |= check(targets == 3, "retain duplicate pointers for shared meshes, stop at first nontexture");
    ign_texanim_frame(&animations);
    failed |= check(shared[0].uv[0] == 100, "wait for the animation period");
    ign_texanim_frame(&animations);
    failed |= check(shared[0].uv[0] == 100 + 0x10000, "animate each shared-mesh occurrence");
    ign_texanim_step(&animations);
    failed |= check(shared[0].uv[0] == 100 && shared[2].uv[0] == 0, "cycle tiles without traversing past a nontexture");
    ign_texanim_free(&animations);
    failed |= check(!animations.a && !animations.n && !animations.period && !animations.clock,
                    "clear all owned state");
    ign_texanim_free(&animations);

    objects[0].nf = 1;
    model.nobj = 1;
    for (int k = 0; k < 3; k++) marker[0].uv[k * 2] = INT32_MAX & ~0x3FFF;
    failed |= check(ign_texanim_init(&animations, &model, 1) == 1, "construct signed-boundary tile");
    if (animations.n)
        failed |= check(!animations.a[0].faces[0][0], "wrap the upper bound without signed C overflow");
    ign_texanim_free(&animations);
    model.nobj = 0;
    failed |= check(ign_texanim_init(&animations, &model, 1) == 0, "accept an empty scene");
    ign_texanim_free(&animations);
    return failed ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc == 1) return unit();
    if (argc == 2 && strcmp(argv[1], "--runner") == 0) return runner();
    fprintf(stderr, "texanim: expected --runner or no arguments\n");
    return 1;
}
