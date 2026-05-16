#ifndef __ELRS_BACKPACK_EMUL_H
#define __ELRS_BACKPACK_EMUL_H

#include <stdint.h>

void backpack_emul_init(const uint8_t uid[6]);
void backpack_emul_send_vtx_config(uint8_t channel_index);

#endif
