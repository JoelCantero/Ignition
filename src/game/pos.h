#ifndef IGN_POS_H
#define IGN_POS_H

#include "assets.h"

int ign_pos_parse(const uint8_t *data, size_t size, int nobj, IgnPos *out);
int ign_pos_load(const char *path, int nobj, IgnPos *out);
void ign_pos_free(IgnPos *pos);
/* pos_apply_frame 0x412e8: advances each animated PLC object's pose by one frame. */
void ign_pos_step(IgnPos *pos, IgnModel *model);

#endif
