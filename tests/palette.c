#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game/palette.h"

static int check(int condition, const char *message) {
    if (condition) return 0;
    fprintf(stderr, "FAIL %s\n", message);
    return 1;
}

static void fill(IgnPalette *p) {
    for (int c = 0; c < 256; c++)
        for (int k = 0; k < 3; k++) p->rgb[c][k] = (uint8_t)(c * 3 + k * 85);
}

static int read_palette(IgnPalette *p) {
    for (int i = 0; i < 768; i++) {
        unsigned v;
        if (scanf("%2x", &v) != 1) return 1;
        p->rgb[i / 3][i % 3] = (uint8_t)v;
    }
    return 0;
}

/* Runner for tests/palette_original.py; one case per line, the result printed as 1536 hex digits.
 *   B frac-bits(hex) r g b keep-flag palette(1536 hex) [256 keep entries]   palette_blend
 *   L t a(1536 hex) b(1536 hex)                                               palette_lerp */
static int runner(void) {
    char op[2];
    while (scanf("%1s", op) == 1) {
        IgnPalette in, other, out;
        if (op[0] == 'B') {
            unsigned long long bits;
            int r, g, b, has_keep;
            if (scanf("%llx %d %d %d %d", &bits, &r, &g, &b, &has_keep) != 5 || read_palette(&in)) {
                fprintf(stderr, "Invalid blend case\n");
                return 1;
            }
            double frac;
            memcpy(&frac, &bits, sizeof frac);
            int32_t keep[256];
            for (int c = 0; has_keep && c < 256; c++) {
                if (scanf("%d", &keep[c]) != 1) {
                    fprintf(stderr, "Invalid keep entry\n");
                    return 1;
                }
            }
            ign_palette_blend(&in, r, g, b, frac, has_keep ? keep : NULL, &out);
        } else if (op[0] == 'L') {
            int t;
            if (scanf("%d", &t) != 1 || read_palette(&in) || read_palette(&other)) {
                fprintf(stderr, "Invalid lerp case\n");
                return 1;
            }
            ign_palette_lerp(&in, &other, t, &out);
        } else {
            fprintf(stderr, "Unknown palette op %s\n", op);
            return 1;
        }
        for (int i = 0; i < 768; i++) printf("%02x", out.rgb[i / 3][i % 3]);
        printf("\n");
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--runner") == 0) return runner();
    int fails = 0;
    IgnPalette in, out;
    fill(&in);

    ign_palette_blend(&in, 0xff, 0xff, 0xff, 0.0, NULL, &out);
    fails += check(memcmp(&in, &out, sizeof in) == 0, "frac 0 leaves the palette unchanged");

    ign_palette_blend(&in, 0x22, 0x22, 0x31, 1.0, NULL, &out);
    fails += check(out.rgb[7][0] == 0x22 && out.rgb[200][1] == 0x22 && out.rgb[90][2] == 0x31,
                   "frac 1 reaches the target");

    /* chop toward zero for both signs: 11 -> 0 by 0.5 steps -5 (-5.5 chopped), 0 -> 11 steps +5. */
    memset(&in, 0, sizeof in);
    in.rgb[1][0] = 11;
    ign_palette_blend(&in, 0, 11, 0, 0.5, NULL, &out);
    fails += check(out.rgb[1][0] == 6 && out.rgb[1][1] == 5, "step is chopped toward zero");

    /* CW 0x127f (53-bit): 3 * (1.0 / 3) is 0.99999999999999994 exactly but rounds to 1.0 before the chop. */
    in.rgb[2][0] = 3;
    ign_palette_blend(&in, 0, 3, 0, 1.0 / 3.0, NULL, &out);
    fails += check(out.rgb[2][0] == 2 && out.rgb[2][1] == 1, "product is rounded to a double before the chop");

    /* the sum keeps its low byte: 0 + 300 -> 44. */
    ign_palette_blend(&in, 300, 0, 0, 1.0, NULL, &out);
    fails += check(out.rgb[0][0] == 44, "target beyond a byte wraps");

    /* integer indefinite 0x80000000 adds nothing to the low byte. */
    ign_palette_blend(&in, 0xff, 0xff, 0xff, NAN, NULL, &out);
    fails += check(out.rgb[1][0] == 11 && out.rgb[2][0] == 3, "NaN fraction leaves colours unchanged");

    fill(&in);
    int32_t keep[256] = {0};
    keep[5] = 1;
    keep[6] = -1;
    ign_palette_blend(&in, 0, 0, 0, 1.0, keep, &out);
    fails += check(memcmp(out.rgb[5], in.rgb[5], 3) == 0 && memcmp(out.rgb[6], in.rgb[6], 3) == 0,
                   "non-zero keep entries are left alone");
    fails += check(out.rgb[4][0] == 0 && out.rgb[7][2] == 0, "zero keep entries are blended");

    IgnPalette same = in;
    ign_palette_blend(&same, 0x10, 0x20, 0x30, 0.25, NULL, &same);
    ign_palette_blend(&in, 0x10, 0x20, 0x30, 0.25, NULL, &out);
    fails += check(memcmp(&same, &out, sizeof out) == 0, "in and out may alias");

    IgnPalette a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    a.rgb[0][0] = 10, b.rgb[0][0] = 20;  /* +10 * 128 / 256 = 5 */
    a.rgb[0][1] = 20, b.rgb[0][1] = 9;   /* -11 * 128 / 256 = -5.5 -> -5 */
    a.rgb[0][2] = 0, b.rgb[0][2] = 255;
    ign_palette_lerp(&a, &b, 128, &out);
    fails += check(out.rgb[0][0] == 15 && out.rgb[0][1] == 15 && out.rgb[0][2] == 127,
                   "lerp divides by 256 toward zero");
    ign_palette_lerp(&a, &b, 0, &out);
    fails += check(memcmp(&a, &out, sizeof out) == 0, "lerp t 0 gives a");
    ign_palette_lerp(&a, &b, 255, &out);
    fails += check(out.rgb[0][2] == 254, "lerp t 255 stops short of b");
    ign_palette_lerp(&a, &b, 256, &out);
    fails += check(memcmp(&b, &out, sizeof out) == 0, "lerp t >= 256 copies b");
    ign_palette_lerp(&a, &b, -256, &out); /* 10 + (10 * -256) / 256 = 0 */
    fails += check(out.rgb[0][0] == 0 && out.rgb[0][1] == 31, "negative t extrapolates away from b");

    if (!fails) printf("palette: all checks passed\n");
    return fails != 0;
}
