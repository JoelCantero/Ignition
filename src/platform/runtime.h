#ifndef IGN_RUNTIME_H
#define IGN_RUNTIME_H

#include <stdint.h>

uint32_t *ign_random_state(void);
void ign_random_seed(uint32_t *state, uint32_t seed);
unsigned ign_random15(uint32_t *state);
double ign_random14(uint32_t *state);

#endif
