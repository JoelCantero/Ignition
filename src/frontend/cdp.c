#include "cdp.h"

#include <string.h>

#define CDP_FRAMES 0x310

static unsigned rd16(const uint8_t *p) { return (unsigned)(p[0] | p[1] << 8); }

int ign_cdp_open(IgnCdp *p, const uint8_t *data, size_t size, uint8_t *dst) {
    memset(p, 0, sizeof *p);
    if (!data || size < CDP_FRAMES || memcmp(data, "CDP\0", 4) || rd16(data + 4) != 100) return 0;
    p->data = data;
    p->size = size;
    p->dst = dst;
    p->frames = (int)rd16(data + 6);
    p->loop = (int)rd16(data + 8);
    p->w = (int)rd16(data + 0xa);
    p->h = (int)rd16(data + 0xc);
    p->pos = CDP_FRAMES;
    p->open = 1;
    return 1;
}

const uint8_t *ign_cdp_palette(const IgnCdp *p) { return p->data ? p->data + 0x10 : NULL; }

int ign_cdp_next(IgnCdp *p) {
    if (!p->open) return -1;
    size_t n = (size_t)p->w * p->h, o = 0;
    const uint8_t *d = p->data;
#define NEED(k)                                                                                                         \
    do {                                                                                                               \
        if (p->pos + (k) > p->size) return p->open = 0, -1;                                                            \
    } while (0)
#define PIX(v)                                                                                                         \
    do {                                                                                                               \
        if (o < n) p->dst[o] = (v);                                                                                    \
        o++;                                                                                                           \
    } while (0)
    for (;;) {
        NEED(1);
        unsigned op = d[p->pos++];
        if (op <= 0xf5) PIX((uint8_t)op);
        else if (op == 0xf6) {
            NEED(1);
            PIX(d[p->pos++]);
        } else if (op <= 0xfa) o += op - 0xf7 + 2;
        else if (op == 0xfb) {
            NEED(1);
            o += d[p->pos++];
        } else if (op == 0xfc) {
            NEED(2);
            o += rd16(d + p->pos);
            p->pos += 2;
        } else if (op == 0xfd) {
            NEED(2);
            unsigned k = d[p->pos] ? d[p->pos] : 256;
            uint8_t v = d[p->pos + 1];
            p->pos += 2;
            while (k--) PIX(v);
        } else if (op == 0xfe) { /* 16-bit run: the loop at 0xc588b rewrites the one byte it read */
            NEED(3);
            unsigned k = rd16(d + p->pos) ? rd16(d + p->pos) : 65536;
            uint8_t v = d[p->pos + 2];
            p->pos += 3;
            while (k--) PIX(v);
        } else
            break; /* 0xff: end of frame */
    }
#undef NEED
#undef PIX
    if (++p->cur == p->frames) {
        p->pos = CDP_FRAMES;
        p->cur = 0;
        return p->loop == 1 ? 1 : 0;
    }
    return 1;
}
