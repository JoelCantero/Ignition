#include "pos.h"

#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int32_t rdi32(const uint8_t *p) { return (int32_t)rd32(p); }

static uint8_t *animation_at(IgnPos *pos, int object) {
    int32_t offset = rdi32(pos->data + (size_t)object * 4);
    if (offset == -1) return NULL;
    return pos->data + (size_t)object * 4 + (size_t)offset * 4;
}

void ign_pos_free(IgnPos *pos) {
    free(pos->data);
    memset(pos, 0, sizeof *pos);
}

int ign_pos_parse(const uint8_t *data, size_t size, int nobj, IgnPos *out) {
    if (!data || !out || nobj < 0 || (size_t)nobj > size / 4) return -1;
    memset(out, 0, sizeof *out);
    uint8_t *copy = malloc(size ? size : 1);
    if (!copy) return -1;
    memcpy(copy, data, size);
    IgnPos parsed = {.data = copy, .size = size, .nobj = nobj};

    for (int i = 0; i < nobj; i++) {
        int32_t offset = rdi32(copy + (size_t)i * 4);
        if (offset == -1) continue;
        if (offset < 0) goto fail;
        int64_t record = (int64_t)i * 4 + (int64_t)offset * 4;
        if (record < 0 || (uint64_t)record + 8 > size) goto fail;
        int32_t nframes = rdi32(copy + record);
        int32_t current = rdi32(copy + record + 4);
        if (nframes <= 0 || current < 0 || current >= nframes ||
            (uint64_t)record + 8 + (uint64_t)nframes * 24 > size)
            goto fail;

        int32_t first[3];
        for (int k = 0; k < 3; k++) first[k] = rdi32(copy + record + 8 + (size_t)k * 4);
        for (int32_t frame = 0; frame + 1 < nframes; frame++) {
            uint8_t *here = copy + record + 8 + (size_t)frame * 24;
            const uint8_t *next = here + 24;
            for (int k = 0; k < 3; k++)
                wr32(here + (size_t)k * 4, rd32(here + (size_t)k * 4) - rd32(next + (size_t)k * 4));
        }
        uint8_t *last = copy + record + 8 + (size_t)(nframes - 1) * 24;
        for (int k = 0; k < 3; k++) wr32(last + (size_t)k * 4, rd32(last + (size_t)k * 4) - (uint32_t)first[k]);
    }
    *out = parsed;
    return 0;

fail:
    free(copy);
    return -1;
}

int ign_pos_load(const char *path, int nobj, IgnPos *out) {
    size_t size = 0;
    uint8_t *data = ign_read_file(path, &size);
    if (!data) return -1;
    int rc = ign_pos_parse(data, size, nobj, out);
    free(data);
    return rc;
}

void ign_pos_step(IgnPos *pos, IgnModel *model) {
    if (!pos || !pos->data || !model || !model->objs || model->nobj != pos->nobj) return;
    for (int i = 0; i < pos->nobj; i++) {
        uint8_t *animation = animation_at(pos, i);
        if (!animation) continue;
        int32_t nframes = rdi32(animation);
        int32_t current = rdi32(animation + 4);
        if (current == nframes) current = 0;
        if (current < 0 || current >= nframes) continue;
        uint8_t *frame = animation + 8 + (size_t)current * 24;
        IgnObject *o = &model->objs[i];
        for (int k = 0; k < 3; k++)
            o->pos[k] = (int32_t)((uint32_t)o->pos[k] - rd32(frame + (size_t)k * 4));
        for (int k = 0; k < 3; k++) {
            int32_t angle = rdi32(frame + 12 + (size_t)k * 4);
            o->rotation[k] = angle >= 3600 ? angle - 3600 : angle;
        }
        current++;
        if (current == nframes) current = 0;
        wr32(animation + 4, (uint32_t)current);
    }
}
