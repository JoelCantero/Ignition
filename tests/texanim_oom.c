#undef malloc
#undef calloc
#undef realloc
#undef free

#include <stdio.h>
#include <stdlib.h>

#include "game/pos.h"

static void *blocks[128];
static int calls, fail_at = -1, live;

static int slot(void *pointer) {
    for (int i = 0; i < 128; i++)
        if (blocks[i] == pointer) return i;
    fprintf(stderr, "FAIL untracked animation allocation\n");
    abort();
}

static void record(void *pointer) {
    if (!pointer) return;
    blocks[slot(NULL)] = pointer;
    live++;
}

void *ign_test_malloc(size_t size) {
    if (calls++ == fail_at) return NULL;
    void *pointer = malloc(size);
    record(pointer);
    return pointer;
}

void *ign_test_calloc(size_t count, size_t size) {
    if (calls++ == fail_at) return NULL;
    void *pointer = calloc(count, size);
    record(pointer);
    return pointer;
}

void *ign_test_realloc(void *old, size_t size) {
    if (calls++ == fail_at) return NULL;
    int had_old = old != NULL;
    int index = had_old ? slot(old) : slot(NULL);
    void *pointer = realloc(old, size);
    if (pointer) {
        if (!had_old) live++;
        blocks[index] = pointer;
    }
    return pointer;
}

void ign_test_free(void *pointer) {
    if (!pointer) return;
    blocks[slot(pointer)] = NULL;
    live--;
    free(pointer);
}

int main(void) {
    IgnFace frames[2] = {{.flags = 0x11}, {.flags = 0x11}};
    for (int k = 0; k < 3; k++) frames[1].uv[k * 2] = 0x8000;
    IgnObject objects[2] = {{.type = 0xFA, .nf = 2, .faces = frames},
                            {.type = 0xFA, .nf = 2, .faces = frames}};
    IgnModel model = {.nobj = 2, .objs = objects};
    IgnTexAnims animations;
    if (ign_texanim_init(&animations, &model, 1) != 2) return 1;
    int allocations = calls;
    ign_texanim_free(&animations);
    if (live) return 1;
    for (fail_at = 0; fail_at < allocations; fail_at++) {
        calls = 0;
        if (ign_texanim_init(&animations, &model, 1) >= 0 || live || animations.a ||
            animations.n || animations.period || animations.clock) {
            fprintf(stderr, "FAIL incomplete cleanup at animation allocation %d\n", fail_at);
            return 1;
        }
        ign_texanim_free(&animations);
        if (live) return 1;
    }
    printf("texanim_oom: %d allocation-failure paths cleared without leaks\n", allocations);
    return 0;
}
