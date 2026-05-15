#ifndef __LED_H__
#define __LED_H__

#include <stdint.h>
#include <stdbool.h>

/**
 * @file led.h
 * @brief LED Status Feedback Module for RX5808-Div
 *
 * Provides visual feedback through LED patterns for different device states.
 * Part of v1.8.0 UX overhaul (Phase 7).
 */

/**
 * LED Pattern Types
 */
typedef enum {
    LED_PATTERN_OFF,              // LED off
    LED_PATTERN_SOLID,            // Solid on (locked state)
    LED_PATTERN_HEARTBEAT,        // Heartbeat pulse 1/2s (unlocked state)
    LED_PATTERN_FAST_BLINK,       // Fast blink 5 Hz (scanning)
    LED_PATTERN_BREATHING,        // Breathing pulse (calibrating)
    LED_PATTERN_SIGNAL_STRENGTH,  // Brightness based on RSSI
    LED_PATTERN_DOUBLE_BLINK      // Double blink (channel switch confirmation)
} led_pattern_t;

/**
 * @brief Initialize LED module
 *
 * Sets up GPIO pin, creates FreeRTOS task for LED control.
 * Called once during system initialization.
 */
void led_init(void);

/**
 * @brief Set LED pattern
 *
 * @param pattern LED pattern to display
 */
void led_set_pattern(led_pattern_t pattern);

/**
 * @brief Get current LED pattern
 *
 * @return Current LED pattern
 */
led_pattern_t led_get_pattern(void);

/**
 * @brief Set LED brightness for signal strength indicator
 *
 * @param rssi_percent RSSI percentage (0-100)
 */
void led_set_signal_strength(uint8_t rssi_percent);

/**
 * @brief Set global LED brightness scaling
 *
 * @param brightness_percent Brightness scale (0-100)
 */
void led_set_brightness(uint8_t brightness_percent);

/**
 * @brief Trigger double-blink confirmation
 *
 * Used after channel switch or significant events.
 * Returns to previous pattern after completion.
 */
void led_trigger_double_blink(void);

#endif // __LED_H__
