#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "game/assets.h"

static void put(uint8_t *p, uint32_t value) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (i * 8));
}

static int runner(void) {
    int descending;
    while (scanf("%d", &descending) == 1) {
        int32_t x, z, fields[4];
        int dz;
        if ((descending != 0 && descending != 1) || scanf("%" SCNd32 "%" SCNd32, &x, &z) != 2) goto invalid;
        for (int k = 0; k < 4; k++)
            if (scanf("%" SCNd32, &fields[k]) != 1) goto invalid;
        if (scanf("%d", &dz) != 1 || dz < INT16_MIN || dz > INT16_MAX) goto invalid;
        uint8_t span[24] = {0};
        for (int k = 0; k < 4; k++) put(span + k * 4, (uint32_t)fields[k]);
        span[16] = (uint8_t)dz;
        span[17] = (uint8_t)((uint16_t)dz >> 8);
        printf("%d\n", ign_surface_span_contains(span, descending, x, z));
    }
    if (feof(stdin)) return 0;
invalid:
    fprintf(stderr, "surface_spans: invalid input\n");
    return 1;
}

static int unit(void) {
    uint8_t span[24] = {0};
    put(span + 12, 65536);
    span[16] = 100;
    if (!ign_surface_span_contains(span, 0, 50, 50) ||
        !ign_surface_span_contains(span, 0, 0, 0) ||
        !ign_surface_span_contains(span, 0, 100, 100) ||
        ign_surface_span_contains(span, 0, 51, 50) ||
        ign_surface_span_contains(span, 0, 0, -1)) goto fail;
    put(span + 8, 131072);
    put(span + 12, 131072);
    span[16] = 0xFF;
    span[17] = 0x7F;
    /* 32767 * 131072 wraps to -131072; a 64-bit intermediate would incorrectly yield 65534. */
    if (!ign_surface_span_contains(span, 0, -2, 32767) ||
        ign_surface_span_contains(span, 0, 65534, 32767)) goto fail;
    return 0;
fail:
    fprintf(stderr, "FAIL original SRF span bounds or wrapping arithmetic\n");
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 1) return unit();
    if (argc == 2 && !strcmp(argv[1], "--runner")) return runner();
    fprintf(stderr, "surface_spans: expected --runner or no arguments\n");
    return 1;
}
