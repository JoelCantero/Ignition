#include "platform/runtime.h"
#include "game/fx.h"
#include "render/raster.h"

#include <errno.h>
#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "runtime:%d: %s\n", __LINE__, #x); return 1; } } while (0)

static int native_contracts(void) {
    _Alignas(32) unsigned char fill[128];
    memset(fill, 0xa5, sizeof fill);
    CHECK(ign_fill_dwords(fill + 1, UINT32_C(0x12345678), 2) == fill + 9);
    CHECK(!memcmp(fill + 1, "\x78\x56\x34\x12\x78\x56\x34\x12", 8));
    CHECK(fill[0] == 0xa5 && fill[9] == 0xa5);
    CHECK(ign_fill_pattern(fill + 32, UINT32_C(0x12345678), 3) == fill + 32);
    CHECK(!memcmp(fill + 32, "\x78\x56\x78", 3));
    CHECK(ign_fill_pattern(fill + 65, UINT32_C(0x12345678), 6) == fill + 68);
    CHECK(!memcmp(fill + 65, "\x78\x56\x34\x12\x78\x12", 6));
    CHECK(ign_fill_pattern(fill, 0, 0) == fill && ign_fill_dwords(fill, 0, 0) == fill);
    IgnFrame *frame = malloc(sizeof *frame);
    CHECK(frame != NULL);
    memset(frame->color, 0xa5, 22);
    memset(frame->inv_z, 0xa5, 22 * sizeof frame->inv_z[0]);
    ign_frame_init(frame, 7, 3);
    ign_frame_clear(frame);
    for (int i = 0; i < 21; i++) CHECK(frame->color[i] == 0 && frame->inv_z[i] == 0);
    CHECK(frame->color[21] == 0xa5 && *(unsigned char *)&frame->inv_z[21] == 0xa5);
    free(frame);
    char integer[33];
    CHECK(ign_u32toa(UINT32_MAX, integer, 2) == integer && strlen(integer) == 32);
    CHECK(ign_u32toa(UINT32_MAX, integer, 36) == integer && !strcmp(integer, "1z141z3"));
    CHECK(ign_i32toa(INT32_MIN, integer, 10) == integer && !strcmp(integer, "-2147483648"));
    CHECK(ign_i32toa(-1, integer, 16) == integer && !strcmp(integer, "ffffffff"));
    for (unsigned base = 0; base <= 37; base++) {
        if (base >= 2 && base <= 36) continue;
        memset(integer, 0x5a, sizeof integer);
        errno = 0;
        CHECK(ign_i32toa(-1, integer, base) == NULL && errno == EINVAL);
        for (size_t i = 0; i < sizeof integer; i++) CHECK(integer[i] == 0x5a);
    }
    uint32_t state = 0;
    ign_random_seed(&state, 1);
    const unsigned sequence[] = {16838, 5758, 10113, 17515, 31051};
    for (size_t i = 0; i < sizeof sequence / sizeof sequence[0]; i++)
        CHECK(ign_random15(&state) == sequence[i]);
    uint32_t *shared = ign_random_state();
    CHECK(shared == ign_random_state() && *shared == 1);
    ign_random_seed(shared, UINT32_MAX);
    CHECK(*ign_random_state() == UINT32_MAX);
    ign_random_seed(shared, 1);
    IgnFx *fx = malloc(sizeof *fx);
    CHECK(fx != NULL);
    ign_fx_init(fx);
    ign_random_seed(&state, 1);
    for (int i = 0; i < 32768; i++) {
        double value = ign_random14(&state);
        CHECK(value >= 0 && value < 1 && value == (double)(float)value);
        CHECK(value == ign_fx_rand(fx) && state == fx->seed);
        CHECK(ign_random15(&state) == ign_fx_rand15(fx) && state == fx->seed);
    }
    free(fx);
    char text[64] = "Ignition";
    CHECK(strcat(text, "/DOS") == text);
    CHECK(strlen(text) == 12 && strcmp(text, "Ignition/DOS") == 0);
    CHECK(strcmp("\x80", "\x7f") > 0 && strcmp("", "a") < 0);
    CHECK(snprintf(text, sizeof text, "%s %d %08x %.2f", "car", -42, 0x123u, 1.25) == 21);
    CHECK(strcmp(text, "car -42 00000123 1.25") == 0);
    for (int c = 0; c <= 255; c++)
        CHECK(tolower(c) == (c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c));
    errno = 0;
    CHECK(fopen("", "rb") == NULL && errno != 0);
    int *error = &errno;
    *error = EDOM;
    CHECK(errno == EDOM);
    CHECK(fabs(asin(0.5) - 0.5235987755982989) < 1e-15);
    CHECK(fabs(atan(1.0) - 0.7853981633974483) < 1e-15);
    CHECK(fabs(atan2(1.0, -1.0) - 2.356194490192345) < 1e-15);
    CHECK(fmod(-7.5, 2.0) == -1.5 && signbit(fmod(-0.0, 2.0)));
    CHECK(pow(-2.0, 3.0) == -8 && pow(2.0, -3.0) == 0.125);
    unsigned char *zero = calloc(257, 1);
    CHECK(zero != NULL);
    for (int i = 0; i < 257; i++) CHECK(zero[i] == 0);
    memset(zero + 1, 0xa5, 255);
    CHECK(zero[0] == 0 && zero[256] == 0);
    for (int i = 1; i < 256; i++) CHECK(zero[i] == 0xa5);
    free(zero);
    free(NULL);
    FILE *f = tmpfile();
    CHECK(f != NULL);
    const unsigned char bytes[] = {0, 10, 13, 26, 128, 255, 42};
    CHECK(fwrite(bytes, 2, 3, f) == 3);
    CHECK(fwrite(bytes + 6, 1, 1, f) == 1);
    CHECK(ftell(f) == 7);
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    unsigned char readback[8] = {0};
    CHECK(fread(readback, 2, 4, f) == 3);
    CHECK(memcmp(bytes, readback, 7) == 0 && feof(f) && !ferror(f));
    CHECK(fread(readback, 0, 4, f) == 0);
    CHECK(fseek(f, -1, SEEK_END) == 0 && !feof(f));
    CHECK(ftell(f) == 6 && fgetc(f) == 42);
    CHECK(fseek(f, -2, SEEK_CUR) == 0 && ftell(f) == 5);
    CHECK(fclose(f) == 0);
    f = tmpfile();
    CHECK(f != NULL);
    CHECK(fprintf(f, "%s %d", "car", -42) == 7);
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    CHECK(fread(text, 1, sizeof text, f) == 7);
    CHECK(memcmp(text, "car -42", 7) == 0 && fclose(f) == 0);
    puts("runtime native contracts: OK");
    return 0;
}

static int decode(const char *hex, char *out, size_t capacity) {
    if (!strcmp(hex, "-")) { out[0] = 0; return 0; }
    size_t n = strlen(hex);
    if (n % 2 || n / 2 >= capacity) return -1;
    for (size_t i = 0; i < n / 2; i++) {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1 || !v) return -1;
        out[i] = (char)v;
    }
    out[n / 2] = 0;
    return 0;
}

static int oracle_runner(void) {
    char line[2048];
    while (fgets(line, sizeof line, stdin)) {
        char op, kind;
        if (sscanf(line, " %c", &op) != 1) return 2;
        if (op == 'B') {
            unsigned offset, count;
            uint32_t pattern;
            _Alignas(32) unsigned char buffer[2112];
            if (sscanf(line, " B %c %u %u %" SCNu32, &kind, &offset, &count, &pattern) != 4 ||
                offset > 31 || count > 512 || (kind != 'D' && kind != 'P' && kind != 'M')) return 2;
            memset(buffer, 0xa5, sizeof buffer);
            unsigned char *dest = buffer + 32 + offset, *end;
            size_t length = kind == 'D' ? (size_t)count * 4 : count;
            if (kind == 'D') end = ign_fill_dwords(dest, pattern, count);
            else if (kind == 'P') end = ign_fill_pattern(dest, pattern, count);
            else end = memset(dest, (int)(pattern & 255), count);
            for (size_t i = 0; i < 32 + offset; i++) if (buffer[i] != 0xa5) return 2;
            for (size_t i = 32 + offset + length; i < sizeof buffer; i++)
                if (buffer[i] != 0xa5) return 2;
            printf("%td ", end - dest);
            for (size_t i = 0; i < length; i++) printf("%02x", dest[i]);
            puts(length ? "" : "-");
        } else if (op == 'I') {
            uint32_t value;
            unsigned base;
            _Alignas(32) unsigned char buffer[96];
            if (sscanf(line, " I %c %" SCNu32 " %u", &kind, &value, &base) != 3 ||
                (kind != 'U' && kind != 'S') || base < 2 || base > 36) return 2;
            memset(buffer, 0xa5, sizeof buffer);
            char *dest = (char *)buffer + 32, *end;
            int32_t signed_value;
            memcpy(&signed_value, &value, sizeof value);
            end = kind == 'U' ? ign_u32toa(value, dest, base) : ign_i32toa(signed_value, dest, base);
            if (end != dest) return 2;
            for (size_t i = 0; i < 32; i++) if (buffer[i] != 0xa5) return 2;
            for (size_t i = 32 + strlen(dest) + 1; i < sizeof buffer; i++)
                if (buffer[i] != 0xa5) return 2;
            puts(dest);
        } else if (op == 'R') {
            uint32_t seed;
            unsigned count;
            if (sscanf(line, " R %" SCNu32 " %u %c", &seed, &count, &kind) != 3 ||
                count > 65536 || (kind != 'I' && kind != 'F')) return 2;
            uint32_t state;
            ign_random_seed(&state, seed);
            for (unsigned i = 0; i < count; i++) {
                if (kind == 'I') {
                    unsigned value = ign_random15(&state);
                    printf("%u %" PRIu32 "\n", value, state);
                } else {
                    double value = ign_random14(&state);
                    printf("%a %" PRIu32 "\n", value, state);
                }
            }
        } else if (op == 'M') {
            double a, b, value;
            if (sscanf(line, " M %c %lf %lf", &kind, &a, &b) != 3) return 2;
            switch (kind) {
            case 'S': value = asin(a); break;
            case 'A': value = atan(a); break;
            case 'T': value = atan2(a, b); break;
            case 'F': value = fmod(a, b); break;
            default: return 2;
            }
            printf("%a\n", value);
        } else if (op == 'S') {
            char ha[513], hb[513], a[513], b[257];
            if (sscanf(line, " S %c %512s %512s", &kind, ha, hb) != 3 ||
                decode(ha, a, 257) || decode(hb, b, sizeof b)) return 2;
            if (kind == 'L') printf("%zu\n", strlen(a));
            else if (kind == 'C') {
                int value = strcmp(a, b);
                printf("%d\n", (value > 0) - (value < 0));
            } else if (kind == 'A') {
                strcat(a, b);
                for (size_t i = 0; a[i]; i++) printf("%02x", (unsigned char)a[i]);
                puts(*a ? "" : "-");
            } else return 2;
        } else return 2;
    }
    return ferror(stdin) ? 2 : 0;
}

int main(int argc, char **argv) {
    if (argc == 1) return native_contracts();
    if (argc == 2 && !strcmp(argv[1], "--oracle")) {
        int result = oracle_runner();
        if (result) fputs("Invalid runtime oracle input or I/O error\n", stderr);
        return result;
    }
    fputs("Usage: runtime_test [--oracle]\n", stderr);
    return 2;
}
