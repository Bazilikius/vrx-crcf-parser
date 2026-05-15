#ifndef __CRSF_H
#define __CRSF_H

#include <stdint.h>
#include <stdbool.h>

#define CRSF_MAX_CHANNELS 16
#define CRSF_FRAME_SIZE_MAX 64

// CRSF Frame types
#define CRSF_FRAMETYPE_RC_CHANNELS_PACKET 0x16

typedef struct {
    uint16_t channels[CRSF_MAX_CHANNELS];
    bool updated;
} crsf_data_t;

void crsf_init(int rx_pin);
bool crsf_get_channels(uint16_t *channels);
bool crsf_is_updated(void);

#endif
