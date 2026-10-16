#include <stdio.h>
#include <string.h>

#include "game/palette.h"

static int read_palette(IgnPalette *p) {
    for (int c = 0; c < 256; c++)
        for (int k = 0; k < 3; k++) {
            unsigned v;
            if (scanf("%2x", &v) != 1) return -1;
            p->rgb[c][k] = (uint8_t)v;
        }
    return 0;
}

static int read_rgb(uint8_t rgb[3]) {
    for (int k = 0; k < 3; k++) {
        unsigned v;
        if (scanf("%u", &v) != 1 || v > 255) return -1;
        rgb[k] = (uint8_t)v;
    }
    return 0;
}

static void print_ints(const int32_t *values, int count) {
    for (int i = 0; i < count; i++) printf("%s%d", i ? " " : "", (int)values[i]);
}

static int runner(void) {
    char op[2];
    while (scanf("%1s", op) == 1) {
        IgnColorSearch search;
        memset(&search, 0xa5, sizeof search);
        ign_color_squares_init(&search);
        if (op[0] == 'S') {
            print_ints(search.squares, 599);
        } else if (op[0] == 'K') {
            IgnPalette p;
            if (read_palette(&p)) return 1;
            ign_color_buckets_build(&search, &p);
            print_ints(search.buckets, 1024);
            printf(" ");
            print_ints(search.red_fraction, 256);
            printf(" ");
            print_ints(search.green_fraction, 256);
        } else if (op[0] == 'C' || op[0] == 'A' || op[0] == 'L') {
            uint8_t a[3], b[3];
            int32_t rgb[3];
            int t = 0;
            if ((op[0] == 'L' && scanf("%d", &t) != 1) || read_rgb(a)) return 1;
            if (op[0] == 'C') {
                printf("%d", ign_rgb_avg3(a));
            } else {
                if (read_rgb(b)) return 1;
                if (op[0] == 'A') ign_rgb_avg(a, b, rgb);
                else ign_rgb_lerp(a, b, t, rgb);
                print_ints(rgb, 3);
            }
        } else if (op[0] == 'N') {
            IgnPalette p;
            int fast;
            int32_t rgb[3];
            if (scanf("%d", &fast) != 1 || read_palette(&p) ||
                scanf("%d %d %d", &rgb[0], &rgb[1], &rgb[2]) != 3)
                return 1;
            ign_color_buckets_build(&search, &p);
            int c = fast ? ign_nearest_color_fast(&search, rgb, &p) : ign_nearest_color(&search, rgb, &p);
            if (c < 0) return 1;
            printf("%d", c);
        } else if (op[0] == 'M' || op[0] == 'T' || op[0] == 'O') {
            uint8_t buffer[65536 + 32];
            memset(buffer, 0xa5, sizeof buffer);
            int count = 65536;
            if (op[0] == 'M') {
                IgnPalette a, b, p;
                int fast, na, nb;
                if (scanf("%d %d %d", &fast, &na, &nb) != 3 || na < 0 || na > 256 || nb < 0 || nb > 256 ||
                    read_palette(&a) || read_palette(&b) || read_palette(&p) ||
                    ign_palette_table_avg(&a, na, &b, nb, &p, fast, buffer + 16))
                    return 1;
                count = na * nb;
            } else if (op[0] == 'T') {
                IgnPalette p;
                int t;
                if (scanf("%d", &t) != 1 || read_palette(&p) || ign_palette_table_lerp(&p, t, buffer + 16))
                    return 1;
            } else {
                ign_palette_table_overlay(buffer + 16);
            }
            for (int i = 0; i < 16; i++)
                if (buffer[i] != 0xa5) return 1;
            for (int i = 16 + count; i < (int)sizeof buffer; i++)
                if (buffer[i] != 0xa5) return 1;
            if (!count) printf("-");
            for (int i = 0; i < count; i++) printf("%02x", buffer[16 + i]);
        } else {
            fprintf(stderr, "Unknown color-table operation: %s\n", op);
            return 1;
        }
        printf("\n");
    }
    if (ferror(stdin)) {
        fprintf(stderr, "Failed reading color-table cases\n");
        return 1;
    }
    return 0;
}

static int self_test(void) {
    IgnPalette p;
    for (int i = 0; i < 256; i++)
        for (int k = 0; k < 3; k++) p.rgb[i][k] = (uint8_t)i;
    IgnColorSearch search;
    ign_color_squares_init(&search);
    ign_color_buckets_build(&search, &p);
    int32_t color[3] = {32, 32, 32};
    if (ign_nearest_color(&search, color, &p) != 32 ||
        ign_nearest_color_fast(&search, color, &p) != 33)
        return 1;
    static uint8_t table[65536];
    ign_palette_table_overlay(table);
    for (int i = 0; i < 256; i++)
        for (int j = 0; j < 256; j++)
            if (table[i * 256 + j] != (i ? i : j)) return 1;
    memset(table, 0xa5, sizeof table);
    if (ign_palette_table_avg(&p, -1, &p, 256, &p, 0, table) != -1 ||
        ign_palette_table_lerp(&p, 257, table) != -1)
        return 1;
    for (int i = 0; i < 65536; i++)
        if (table[i] != 0xa5) return 1;
    color[0] = 256;
    if (ign_nearest_color(&search, color, &p) != -1 ||
        ign_nearest_color_fast(&search, color, &p) != -1)
        return 1;
    memset(&p, 255, sizeof p);
    ign_color_buckets_build(&search, &p);
    color[0] = color[1] = color[2] = 255;
    if (ign_nearest_color_fast(&search, color, &p) != -1) return 1;
    puts("Color-table kernels passed");
    return 0;
}

int main(int argc, char **argv) {
    int result = argc == 2 && strcmp(argv[1], "--runner") == 0 ? runner() : self_test();
    if (result) fprintf(stderr, "Color-table test failed\n");
    return result;
}
