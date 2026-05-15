/**
 * @file navigation.c
 * @brief Unified navigation system for consistent user interaction
 *
 * Provides standardized key handling, long-press detection, double-click
 * detection, lock management, and audio feedback across all pages.
 */

/*********************
 *      INCLUDES
 *********************/
#include "navigation.h"
#include "beep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/*********************
 *      DEFINES
 *********************/
#define TAG "NAV"

/**********************
 *  STATIC PROTOTYPES
 **********************/
static uint32_t get_tick_ms(void);

/**********************
 *  STATIC VARIABLES
 **********************/
static nav_context_t nav_ctx = {0};
static bool *lock_flag_ptr = NULL;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void navigation_init(bool *lock_state_ptr)
{
    lock_flag_ptr = lock_state_ptr;
    memset(&nav_ctx, 0, sizeof(nav_context_t));
}

void navigation_reset_context(void)
{
    memset(&nav_ctx, 0, sizeof(nav_context_t));
}

nav_result_t navigation_handle_key_press(lv_key_t key)
{
    uint32_t now = get_tick_ms();

    nav_ctx.pressed_key = key;
    nav_ctx.press_start_time = now;
    nav_ctx.is_long_press = false;

    // Check for double click
    if ((now - nav_ctx.last_release_time) < NAV_DOUBLE_CLICK_MS) {
        nav_ctx.is_double_click = true;
        return NAV_RESULT_DOUBLE_CLICK;
    }

    nav_ctx.is_double_click = false;
    return NAV_RESULT_NONE;
}

nav_result_t navigation_handle_key_release(lv_key_t key)
{
    uint32_t now = get_tick_ms();
    uint32_t duration = now - nav_ctx.press_start_time;

    nav_ctx.last_release_time = now;

    // Determine if it was a long press
    if (duration >= NAV_LONG_PRESS_MS) {
        nav_ctx.is_long_press = true;
        return NAV_RESULT_LONG_PRESS;
    }

    return NAV_RESULT_SHORT_PRESS;
}

bool navigation_is_long_press(lv_key_t key)
{
    if (nav_ctx.pressed_key != key) {
        return false;
    }

    uint32_t duration = get_tick_ms() - nav_ctx.press_start_time;
    return (duration >= NAV_LONG_PRESS_MS);
}

bool navigation_is_double_click(void)
{
    return nav_ctx.is_double_click;
}

void navigation_beep_feedback(void)
{
    beep_turn_on();
}

void navigation_beep_lock(void)
{
    // 2 short beeps for lock
    beep_turn_on();
    vTaskDelay(pdMS_TO_TICKS(100));
    beep_turn_on();
}

void navigation_beep_unlock(void)
{
    // 1 beep for unlock
    beep_turn_on();
}

bool navigation_toggle_lock(void)
{
    if (lock_flag_ptr == NULL) {
        return false;
    }

    *lock_flag_ptr = !(*lock_flag_ptr);

    if (*lock_flag_ptr) {
        navigation_beep_lock();
    } else {
        navigation_beep_unlock();
    }

    return *lock_flag_ptr;
}

bool navigation_is_locked(void)
{
    if (lock_flag_ptr == NULL) {
        return false;
    }
    return *lock_flag_ptr;
}

void navigation_set_lock(bool locked)
{
    if (lock_flag_ptr != NULL) {
        *lock_flag_ptr = locked;
    }
}

void navigation_handle_menu_keys(lv_event_t* event, lv_group_t* group, nav_back_callback_t back_callback)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code != LV_EVENT_KEY) {
        return;
    }

    lv_key_t key = lv_indev_get_key(lv_indev_get_act());

    // Always provide beep feedback
    navigation_beep_feedback();

    switch(key) {
        case LV_KEY_UP:
            if (group != NULL) {
                lv_group_focus_prev(group);
            }
            break;

        case LV_KEY_DOWN:
            if (group != NULL) {
                lv_group_focus_next(group);
            }
            break;

        case LV_KEY_LEFT:
            if (back_callback != NULL) {
                back_callback();
            }
            break;

        case LV_KEY_RIGHT:
            // Default: no action (can be overridden by page)
            break;

        case LV_KEY_ENTER:
            // Enter key handling done by caller
            break;

        default:
            break;
    }
}

uint32_t navigation_get_press_duration(void)
{
    return get_tick_ms() - nav_ctx.press_start_time;
}

bool navigation_check_locked_feedback(void)
{
    if (navigation_is_locked()) {
        navigation_beep_feedback();
        return true;
    }
    return false;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static uint32_t get_tick_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
