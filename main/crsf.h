#ifndef __CRSF_H
#define __CRSF_H

#include <stdint.h>
#include <stdbool.h>

#define CRSF_MAX_CHANNELS 16

typedef struct {
    uint16_t channels[CRSF_MAX_CHANNELS];
    bool updated;
} crsf_data_t;

void crsf_init(int rx_pin);
bool crsf_get_channels(uint16_t *channels);

#endif
