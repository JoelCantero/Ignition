#include "runtime.h"

#include <errno.h>
#include <string.h>

/* sub_0006476e: the default Watcom stream; simulation and HUD retain private streams. */
uint32_t *ign_random_state(void) {
    static uint32_t state = 1;
    return &state;
}

/* sub_00064798 */
void ign_random_seed(uint32_t *state, uint32_t seed) {
    *state = seed;
}

/* sub_00064774: unsigned 32-bit wraparound, independent of the host libc. */
unsigned ign_random15(uint32_t *state) {
    *state = *state * UINT32_C(1103515245) + UINT32_C(12345);
    return (*state >> 16) & 0x7fff;
}

/* sub_00051714: all results are exactly representable as both float and double. */
double ign_random14(uint32_t *state) {
    return (ign_random15(state) & 0x3fff) * (1.0 / 16384.0);
}

/* sub_00065367: little-endian dwords, including unaligned destinations. */
unsigned char *ign_fill_dwords(void *dest, uint32_t pattern, size_t count) {
    unsigned char *p = dest;
    const unsigned char bytes[4] = {
        (unsigned char)pattern, (unsigned char)(pattern >> 8),
        (unsigned char)(pattern >> 16), (unsigned char)(pattern >> 24),
    };
    for (size_t i = 0; i < count; i++, p += 4) memcpy(p, bytes, sizeof bytes);
    return p;
}

/* sub_00065330: preserve both the DL/DH/DL tail and the cursor before that tail. */
unsigned char *ign_fill_pattern(void *dest, uint32_t pattern, size_t count) {
    unsigned char *p = dest;
    while (count && ((uintptr_t)p & 3)) {
        *p++ = (unsigned char)pattern;
        pattern = (pattern >> 8) | (pattern << 24);
        count--;
    }
    p = ign_fill_dwords(p, pattern, count / 4);
    size_t tail = count & 3;
    if (tail) p[0] = (unsigned char)pattern;
    if (tail > 1) p[1] = (unsigned char)(pattern >> 8);
    if (tail > 2) p[2] = (unsigned char)pattern;
    return p;
}

/* sub_000700d0: the original digit table at 0x700ab uses lowercase letters. */
char *ign_u32toa(uint32_t value, char *out, unsigned base) {
    if (base < 2 || base > 36) {
        errno = EINVAL;
        return NULL;
    }
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char reverse[32];
    size_t n = 0;
    do {
        reverse[n++] = digits[value % base];
        value /= base;
    } while (value);
    for (size_t i = 0; i < n; i++) out[i] = reverse[n - i - 1];
    out[n] = '\0';
    return out;
}

/* sub_0007011a: only decimal negatives get a sign; unsigned subtraction handles INT32_MIN. */
char *ign_i32toa(int32_t value, char *out, unsigned base) {
    if (base == 10 && value < 0) {
        out[0] = '-';
        ign_u32toa(UINT32_C(0) - (uint32_t)value, out + 1, base);
        return out;
    }
    return ign_u32toa((uint32_t)value, out, base);
}
