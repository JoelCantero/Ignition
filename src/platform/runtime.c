#include "runtime.h"

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
