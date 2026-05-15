#ifndef __BEEP_H
#define __BEEP_H

#include <stdint.h>
#include "hardware/hwvers.h"

void PWM_Enable(void);
void PWM_Disable(void);
void Beep_Init(void);
void beep_on_off(uint8_t on_off);
void beep_set_enable_disable(uint8_t en);
uint16_t beep_get_status(void);

// -- Pattern API --
/** Single 100 ms click (UI feedback, backwards-compatible). */
void beep_turn_on(void);
/** Two short clicks: diversity antenna-switch event. */
void beep_play_double(void);
/** Three rapid clicks: success / bind confirmation. */
void beep_play_triple(void);
/** One long 450 ms tone: error / low-signal warning. */
void beep_play_long(void);
/** Custom pattern: on_ms on, off_ms silence, repeated count times. */
void beep_play_pattern(uint16_t on_ms, uint16_t off_ms, uint8_t count);
/** Stub for API compatibility — active buzzer has fixed frequency. */
void beep_set_tone(uint16_t tone);

#endif
