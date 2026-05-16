#ifndef __CONFIG_H
#define __CONFIG_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t uid[6];
    uint8_t ch_v;
    uint8_t ch_b;
    uint8_t l_grid;
} config_t;

extern config_t device_config;

void config_load(void);
void config_save(void);

#endif
