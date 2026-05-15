/**
 * @file quick_menu.h
 * @brief Quick Action Menu - Fast access to common features
 *
 * Provides a popup menu accessible via long-hold LEFT button when locked.
 * Solves UX pain point: Long workflows (7+ steps to scan → 2 steps)
 *
 * Menu Items:
 * - Quick Scan: Start channel scan immediately
 * - Spectrum: Open spectrum analyzer
 * - Calibration: Open calibration menu
 * - Band X: Open Band X frequency editor
 * - Settings: Open setup page
 * - Back to Menu: Return to main menu
 *
 * Navigation:
 * - UP/DOWN: Navigate menu items
 * - OK/ENTER: Select item
 * - LEFT: Close menu (cancel)
 *
 * @version 1.8.0
 * @date 2026-02-20
 */

#ifndef QUICK_MENU_H
#define QUICK_MENU_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Quick menu action types
 */
typedef enum {
    QUICK_ACTION_NONE = 0,
    QUICK_ACTION_SCAN,          // Start quick scan
    QUICK_ACTION_SPECTRUM,      // Open spectrum analyzer
    QUICK_ACTION_CALIBRATION,   // Open calibration
    QUICK_ACTION_BAND_X,        // Open Band X editor
    QUICK_ACTION_SETTINGS,      // Open setup page
    QUICK_ACTION_EXIT,          // Close quick menu, stay on main page
    QUICK_ACTION_COUNT
} quick_action_t;

/**
 * @brief Quick menu item definition
 */
typedef struct {
    const char* label;          // Display text
    const char* icon;           // Icon symbol (optional)
    quick_action_t action;      // Action to perform
    bool enabled;               // Item enabled state
    bool locked_visible;        // Show in locked (text-only, fewer) mode
} quick_menu_item_t;

/**
 * @brief Quick menu configuration
 */
typedef struct {
    lv_obj_t* parent;           // Parent object (main page)
    lv_obj_t* menu_obj;         // Menu popup object
    lv_obj_t* list;             // List of menu items
    lv_group_t* group;          // Input group
    uint8_t selected_index;     // 0-based index within rendered items
    uint8_t item_count;         // Number of rendered items
    lv_obj_t* items[QUICK_ACTION_COUNT];          // Rendered button pointers
    quick_action_t item_actions[QUICK_ACTION_COUNT]; // Actions for rendered items
    bool active;                // Menu is currently shown

    // Callback for action selection
    void (*on_action)(quick_action_t action);
} quick_menu_t;

/**
 * @brief Initialize quick menu module
 *
 * @return Pointer to quick menu context, NULL on failure
 */
quick_menu_t* quick_menu_init(void);

/**
 * @brief Show quick menu popup
 *
 * @param menu Quick menu context
 * @param parent Parent object (main page container)
 * @param on_action Callback when action is selected
 * @param show_icons true to include icons, false for text-only
 * @return true if menu was shown, false on error
 */
bool quick_menu_show(quick_menu_t* menu, lv_obj_t* parent, void (*on_action)(quick_action_t), bool show_icons);

/**
 * @brief Hide/close quick menu
 *
 * @param menu Quick menu context
 */
void quick_menu_hide(quick_menu_t* menu);

/**
 * @brief Check if quick menu is currently active
 *
 * @param menu Quick menu context
 * @return true if menu is shown, false otherwise
 */
bool quick_menu_is_active(quick_menu_t* menu);

/**
 * @brief Handle key event for quick menu navigation
 *
 * @param menu Quick menu context
 * @param key Key code (LV_KEY_UP, LV_KEY_DOWN, LV_KEY_ENTER, LV_KEY_LEFT)
 * @return true if key was handled, false otherwise
 */
bool quick_menu_handle_key(quick_menu_t* menu, uint32_t key);

/**
 * @brief Get currently selected action
 *
 * @param menu Quick menu context
 * @return Current action, or QUICK_ACTION_NONE if invalid
 */
quick_action_t quick_menu_get_selected(quick_menu_t* menu);

/**
 * @brief Set menu item enabled state
 *
 * @param menu Quick menu context
 * @param action Action to enable/disable
 * @param enabled true to enable, false to disable
 */
void quick_menu_set_item_enabled(quick_menu_t* menu, quick_action_t action, bool enabled);

/**
 * @brief Cleanup quick menu resources
 *
 * @param menu Quick menu context
 */
void quick_menu_cleanup(quick_menu_t* menu);

/**
 * @brief Get action name string (for debugging)
 *
 * @param action Action type
 * @return String representation of action
 */
const char* quick_menu_action_name(quick_action_t action);

#ifdef __cplusplus
}
#endif

#endif // QUICK_MENU_H
