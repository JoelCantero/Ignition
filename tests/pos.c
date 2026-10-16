#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/pos.h"

static int check(int condition, const char *message) {
    if (condition) return 0;
    fprintf(stderr, "FAIL %s\n", message);
    return 1;
}

static void put32(uint8_t *data, size_t offset, int32_t value) {
    uint32_t v = (uint32_t)value;
    data[offset] = (uint8_t)v;
    data[offset + 1] = (uint8_t)(v >> 8);
    data[offset + 2] = (uint8_t)(v >> 16);
    data[offset + 3] = (uint8_t)(v >> 24);
}

static int32_t get32(const uint8_t *data, size_t offset) {
    uint32_t v = (uint32_t)data[offset] | (uint32_t)data[offset + 1] << 8 |
                 (uint32_t)data[offset + 2] << 16 | (uint32_t)data[offset + 3] << 24;
    return (int32_t)v;
}

static int runner(void) {
    int count;
    while (scanf("%d", &count) == 1) {
        size_t size;
        if (scanf("%zu", &size) != 1 || count < 0 || count > 65536 || size > 0x1000000) goto invalid;
        uint8_t *data = malloc(size ? size : 1);
        if (!data) goto invalid;
        for (size_t i = 0; i < size; i++) {
            unsigned value;
            if (scanf("%2x", &value) != 1) {
                free(data);
                goto invalid;
            }
            data[i] = (uint8_t)value;
        }
        IgnPos pos;
        int rc = ign_pos_parse(data, size, count, &pos);
        free(data);
        if (rc) goto invalid;
        for (size_t i = 0; i < pos.size; i++) printf("%02x", pos.data[i]);
        printf("\n");
        ign_pos_free(&pos);
    }
    if (feof(stdin)) return 0;
invalid:
    fprintf(stderr, "pos: invalid case or allocation failure\n");
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--runner")) return runner();
    if (argc != 1) {
        fprintf(stderr, "pos: expected --runner or no arguments\n");
        return 1;
    }
    uint8_t bytes[64] = {0};
    put32(bytes, 0, 2); /* Entry 0 points two dwords forward to its animation. */
    put32(bytes, 4, -1);
    put32(bytes, 8, 2);
    put32(bytes, 12, 0);
    const int32_t frames[2][6] = {
        {100, 200, 300, 3600, 900, 3610},
        {90, 180, 270, 1800, 2700, 0},
    };
    for (int f = 0; f < 2; f++)
        for (int k = 0; k < 6; k++) put32(bytes, 16 + (size_t)f * 24 + (size_t)k * 4, frames[f][k]);

    IgnPos pos;
    int failed = check(ign_pos_parse(bytes, sizeof bytes, 2, &pos) == 0, "parse valid animation");
    if (failed) return 1;
    failed |= check(get32(pos.data, 16) == 10 && get32(pos.data, 20) == 20 && get32(pos.data, 24) == 30,
                   "normalize positions to next-frame deltas");
    failed |= check(get32(pos.data, 40) == -10 && get32(pos.data, 44) == -20 && get32(pos.data, 48) == -30,
                   "normalize final frame to first-frame cycle");

    IgnObject objects[2] = {0};
    objects[0].pos[0] = 1000;
    objects[0].pos[1] = 2000;
    objects[0].pos[2] = 3000;
    IgnModel model = {.nobj = 2, .objs = objects};
    ign_pos_step(&pos, &model);
    failed |= check(objects[0].pos[0] == 990 && objects[0].pos[1] == 1980 && objects[0].pos[2] == 2970,
                   "apply first position delta");
    failed |= check(objects[0].rotation[0] == 0 && objects[0].rotation[1] == 900 &&
                       objects[0].rotation[2] == 10,
                   "copy angles and wrap angles at 3600");
    failed |= check(get32(pos.data, 12) == 1, "advance frame counter");

    ign_pos_step(&pos, &model);
    failed |= check(objects[0].pos[0] == 1000 && objects[0].pos[1] == 2000 && objects[0].pos[2] == 3000,
                   "apply second position delta");
    failed |= check(get32(pos.data, 12) == 0, "wrap animation counter at frame count");
    ign_pos_step(&pos, &model);
    failed |= check(objects[0].pos[0] == 990 && get32(pos.data, 12) == 1, "wrap animation frame counter");
    ign_pos_free(&pos);

    uint8_t malformed[64];
    memcpy(malformed, bytes, sizeof bytes);
    put32(malformed, 0, -2);
    failed |= check(ign_pos_parse(malformed, sizeof malformed, 2, &pos) != 0, "reject invalid negative offset");
    memcpy(malformed, bytes, sizeof bytes);
    put32(malformed, 8, 3);
    failed |= check(ign_pos_parse(malformed, sizeof malformed, 2, &pos) != 0, "reject truncated frame data");
    failed |= check(ign_pos_parse(bytes, sizeof bytes, 17, &pos) != 0, "reject truncated offset table");
    return failed ? 1 : 0;
}
