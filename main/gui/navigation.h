#ifndef __NAVIGATION_H
#define __NAVIGATION_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "lvgl/lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/*********************
 *      DEFINES
 *********************/
#define NAV_LONG_PRESS_MS           1000    // 1 second for long press
#define NAV_DOUBLE_CLICK_MS         500     // 500ms window for double click
#define NAV_BEEP_DURATION_MS        50      // Beep duration

/*********************
 *      TYPEDEFS
 *********************/

/**
 * Navigation context - tracks button press timing
 */
typedef struct {
    uint32_t press_start_time;       // When button was pressed
    uint32_t last_release_time;      // When button was last released
    lv_key_t pressed_key;            // Which key is currently pressed
    bool is_long_press;              // Long press detected
    bool is_double_click;            // Double click detected
} nav_context_t;

/**
 * Navigation callbacks
 */
typedef void (*nav_key_callback_t)(lv_key_t key, bool is_long_press);
typedef void (*nav_back_callback_t)(void);

/**
 * Standard navigation result
 */
typedef enum {
    NAV_RESULT_NONE = 0,
    NAV_RESULT_SHORT_PRESS,
    NAV_RESULT_LONG_PRESS,
    NAV_RESULT_DOUBLE_CLICK,
    NAV_RESULT_HANDLED
} nav_result_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Initialize navigation module
 * @param lock_state_ptr Pointer to global lock flag
 */
void navigation_init(bool *lock_state_ptr);

/**
 * Reset navigation context (call when entering new page)
 */
void navigation_reset_context(void);

/**
 * Handle key press event
 * @param key The key that was pressed
 * @return Navigation result (short/long/double)
 */
nav_result_t navigation_handle_key_press(lv_key_t key);

/**
 * Handle key release event
 * @param key The key that was released
 * @return Navigation result
 */
nav_result_t navigation_handle_key_release(lv_key_t key);

/**
 * Check if a key is currently being held
 * @param key The key to check
 * @return True if key is held for long press duration
 */
bool navigation_is_long_press(lv_key_t key);

/**
 * Check if last action was double click
 * @return True if double click detected
 */
bool navigation_is_double_click(void);

/**
 * Play standard beep feedback
 */
void navigation_beep_feedback(void);

/**
 * Play lock beep pattern (2 beeps)
 */
void navigation_beep_lock(void);

/**
 * Play unlock beep pattern (1 beep)
 */
void navigation_beep_unlock(void);

/**
 * Toggle lock state (main page only)
 * @return New lock state
 */
bool navigation_toggle_lock(void);

/**
 * Check if system is locked
 * @return True if locked
 */
bool navigation_is_locked(void);

/**
 * Set lock state
 * @param locked New lock state
 */
void navigation_set_lock(bool locked);

/**
 * Standard key handler for menu navigation
 * Handles UP/DOWN/LEFT/RIGHT/ENTER consistently
 * @param event LVGL event
 * @param group LVGL group for focus
 * @param back_callback Callback for LEFT/back action
 */
void navigation_handle_menu_keys(lv_event_t* event, lv_group_t* group, nav_back_callback_t back_callback);

/**
 * Get elapsed time since key press started
 * @return Time in milliseconds
 */
uint32_t navigation_get_press_duration(void);

/**
 * Check if in locked mode and handle feedback
 * @return True if locked (caller should return early)
 */
bool navigation_check_locked_feedback(void);

/**********************
 *      MACROS
 **********************/

// Helper macro for standard menu event handling
#define NAV_HANDLE_MENU_EVENT(event, group, back_func) \
    navigation_handle_menu_keys(event, group, back_func)

// Helper macro to check lock and beep
#define NAV_CHECK_LOCKED() \
    if (navigation_check_locked_feedback()) return

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*__NAVIGATION_H*/
