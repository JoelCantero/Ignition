#ifndef IGN_RUNTIME_H
#define IGN_RUNTIME_H

#include <stdint.h>
#include <stddef.h>

uint32_t *ign_random_state(void);
void ign_random_seed(uint32_t *state, uint32_t seed);
unsigned ign_random15(uint32_t *state);
double ign_random14(uint32_t *state);

/* Pattern fills return the original EAX cursor, not necessarily the end of the buffer. */
unsigned char *ign_fill_dwords(void *dest, uint32_t pattern, size_t count);
unsigned char *ign_fill_pattern(void *dest, uint32_t pattern, size_t count);

/* out needs 33 bytes. Bases 2..36 are supported; invalid bases return NULL with errno=EINVAL. */
char *ign_u32toa(uint32_t value, char *out, unsigned base);
char *ign_i32toa(int32_t value, char *out, unsigned base);

#endif
