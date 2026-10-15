#ifndef IGN_CDP_H
#define IGN_CDP_H

#include <stddef.h>
#include <stdint.h>

/* CDP player (sub_000552fc / sub_00055384, decoder at 0xc5800). Frame opcodes: 00-f5 pixel, f6 b escaped pixel,
 * f7-fa skip 2-5, fb n8 skip, fc n16 skip, fd n8 v run (n 0 = 256), fe n16 v run (n 0 = 65536), ff end of frame.
 * Header: "CDP\0", u16 100, u16 frames (+6), u16 loop (+8),
 * u16 w (+0xa), u16 h (+0xc), u16 ? (+0xe), 768-byte 8-bit palette (+0x10), frame stream (+0x310). */
typedef struct {
    const uint8_t *data;
    size_t size;
    uint8_t *dst; /* linear, pitch = w */
    int frames, loop, w, h;
    int cur; /* +0x14 */
    int open;
    size_t pos;
} IgnCdp;

int ign_cdp_open(IgnCdp *p, const uint8_t *data, size_t size, uint8_t *dst);
const uint8_t *ign_cdp_palette(const IgnCdp *p);
/* Decodes the next frame: 1 more frames, 0 clip finished (rewound unless looping), -1 not open or corrupt. */
int ign_cdp_next(IgnCdp *p);

#endif
