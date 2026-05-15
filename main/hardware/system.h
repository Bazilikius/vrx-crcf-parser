#ifndef __MODULE_H
#define __MODULE_H

#include <stdint.h>
#include <stdbool.h>



// LED_Init() removed — use led_init() from led.h instead
void USART_init(uint32_t bound);
void system_init(void);
void system_apply_cpu_freq(uint16_t freq_setting);

// LVGL refresh rate control (20ms normal, 50ms when display locked/idle)
extern volatile uint8_t system_lvgl_sleep_ms;
void system_set_lvgl_idle(bool idle);

// CPU power context control
void system_set_cpu_context_idle(bool idle);

#endif
