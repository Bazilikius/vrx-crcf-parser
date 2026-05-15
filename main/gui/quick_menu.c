/**
 * @file quick_menu.c
 * @brief Quick Action Menu Implementation
 *
 * Popup menu for fast access to common features from main page.
 * Activated by long-hold LEFT button when locked (1 second).
 *
 * Design:
 * - Centered popup with semi-transparent background
 * - 6 menu items (icons optional)
 * - Smooth animation (slide up 200ms)
 * - Consistent navigation pattern
 * - Audio feedback on selection
 *
 * @version 1.8.0
 * @date 2026-02-20
 */

// Suppress warnings for LVGL animation callbacks
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"

#include "quick_menu.h"
#include "beep.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Menu styling constants — sized for 160×80 display
#define MENU_WIDTH          130
#define MENU_RADIUS         6
#define MENU_PADDING        3
#define ITEM_HEIGHT         16
#define ITEM_GAP            1
#define ANIM_TIME           150  // ms

// Colors
#define COLOR_ITEM_NORMAL   lv_color_make(60, 60, 60)
#define COLOR_ITEM_SELECTED lv_color_make(30, 120, 220)
#define COLOR_TEXT          lv_color_white()

/**
 * @brief Highlight a rendered menu item button as selected
 */
static void highlight_item(quick_menu_t* menu, uint8_t idx) {
    if (!menu || idx >= menu->item_count || !menu->items[idx]) return;
    lv_obj_set_style_bg_color(menu->items[idx], COLOR_ITEM_SELECTED, 0);
}

/**
 * @brief Remove selection highlight from a rendered menu item button
 */
static void unhighlight_item(quick_menu_t* menu, uint8_t idx) {
    if (!menu || idx >= menu->item_count || !menu->items[idx]) return;
    lv_obj_set_style_bg_color(menu->items[idx], COLOR_ITEM_NORMAL, 0);
}

// Static menu items definition
// locked_visible: show when menu opened in locked (text-only) mode
static const quick_menu_item_t menu_items[QUICK_ACTION_COUNT] = {
    {NULL, NULL, QUICK_ACTION_NONE, false, false},          // Placeholder
    {"Quick Scan",  LV_SYMBOL_REFRESH,  QUICK_ACTION_SCAN,         true, true},
    {"Spectrum",    LV_SYMBOL_IMAGE,    QUICK_ACTION_SPECTRUM,      true, true},
    {"Calibration", LV_SYMBOL_SETTINGS, QUICK_ACTION_CALIBRATION,   true, false},
    {"Band X",      LV_SYMBOL_EDIT,     QUICK_ACTION_BAND_X,        true, true},
    {"Settings",    LV_SYMBOL_LIST,     QUICK_ACTION_SETTINGS,      true, false},
    {"Exit",        NULL,               QUICK_ACTION_EXIT,          true, true}
};

// Static menu context (single instance)
static quick_menu_t g_menu = {0};
static bool g_menu_initialized = false;

/**
 * @brief Event handler for menu list
 */
static void menu_event_handler(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    quick_menu_t* menu = (quick_menu_t*)lv_event_get_user_data(e);

    if (!menu || !menu->active) return;

    if (code == LV_EVENT_CLICKED) {
        // Get rendered item index (0-based) stored as user data
        lv_obj_t* btn = lv_event_get_target(e);
        uint32_t idx = (uint32_t)(uintptr_t)lv_obj_get_user_data(btn);

        if (idx < menu->item_count) {
            quick_action_t action = menu->item_actions[idx];
            void (*on_action)(quick_action_t) = menu->on_action;

            // Audio feedback
            beep_turn_on();

            // Hide menu (clears menu state including on_action)
            quick_menu_hide(menu);

            // Trigger callback
            if (on_action) {
                on_action(action);
            }
        }
    }
}

/**
 * @brief Create menu popup UI
 *
 * @param menu   Quick menu context
 * @param parent Parent LVGL object
 * @param show_icons  true = full menu with icons; false = locked mode (text-only, fewer items)
 */
static bool create_menu_ui(quick_menu_t* menu, lv_obj_t* parent, bool show_icons) {
    if (!menu || !parent) return false;

    // --- Count and collect items to render ---
    menu->item_count = 0;
    for (uint8_t i = 1; i < QUICK_ACTION_COUNT; i++) {
        if (!menu_items[i].enabled) continue;
        if (!show_icons && !menu_items[i].locked_visible) continue;
        menu->item_actions[menu->item_count] = menu_items[i].action;
        menu->items[menu->item_count] = NULL;
        menu->item_count++;
    }
    if (menu->item_count == 0) return false;

    // --- Compute exact menu height to fit all items (160x80 display) ---
    // inner height = item_count * ITEM_HEIGHT + (item_count-1) * ITEM_GAP
    lv_coord_t inner_h = (lv_coord_t)(menu->item_count * ITEM_HEIGHT +
                                       (menu->item_count - 1) * ITEM_GAP);
    lv_coord_t menu_h  = (lv_coord_t)(MENU_PADDING * 2 + inner_h);

    // Modal background
    lv_obj_t* bg = lv_obj_create(parent);
    lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_40, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    // Menu container — flex column, no inner list wrapper needed
    lv_obj_t* menu_obj = lv_obj_create(bg);
    lv_obj_set_size(menu_obj, MENU_WIDTH, menu_h);
    lv_obj_align(menu_obj, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(menu_obj, MENU_RADIUS, 0);
    lv_obj_set_style_pad_all(menu_obj, MENU_PADDING, 0);
    lv_obj_set_style_pad_row(menu_obj, ITEM_GAP, 0);
    lv_obj_set_flex_flow(menu_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(menu_obj, LV_OBJ_FLAG_SCROLLABLE);

    // Create item buttons directly inside menu_obj
    uint8_t render_idx = 0;
    for (uint8_t i = 1; i < QUICK_ACTION_COUNT; i++) {
        if (!menu_items[i].enabled) continue;
        if (!show_icons && !menu_items[i].locked_visible) continue;

        lv_obj_t* btn = lv_btn_create(menu_obj);
        lv_obj_set_size(btn, LV_PCT(100), ITEM_HEIGHT);
        lv_obj_set_style_bg_color(btn, COLOR_ITEM_NORMAL, 0);
        lv_obj_set_style_radius(btn, 3, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_add_event_cb(btn, menu_event_handler, LV_EVENT_CLICKED, menu);
        lv_obj_set_user_data(btn, (void*)(uintptr_t)render_idx);

        lv_obj_t* label = lv_label_create(btn);
        char text[64];
        if (show_icons && menu_items[i].icon) {
            snprintf(text, sizeof(text), "%s %s", menu_items[i].icon, menu_items[i].label);
        } else {
            snprintf(text, sizeof(text), "%s", menu_items[i].label);
        }
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(label, COLOR_TEXT, 0);
        lv_obj_center(label);

        menu->items[render_idx] = btn;
        render_idx++;
    }

    // Highlight first item
    menu->selected_index = 0;
    highlight_item(menu, 0);

    // Slide-up animation
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, menu_obj);
    lv_anim_set_time(&a, ANIM_TIME);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_values(&a, lv_obj_get_y(menu_obj) + 20, lv_obj_get_y(menu_obj));
    lv_anim_start(&a);

    menu->menu_obj = bg;
    menu->list = menu_obj;   // list pointer kept for compatibility
    menu->parent = parent;
    menu->active = true;

    return true;
}

// Public API implementation

quick_menu_t* quick_menu_init(void) {
    if (g_menu_initialized) {
        return &g_menu;
    }

    memset(&g_menu, 0, sizeof(quick_menu_t));
    g_menu_initialized = true;

    return &g_menu;
}

bool quick_menu_show(quick_menu_t* menu, lv_obj_t* parent, void (*on_action)(quick_action_t), bool show_icons) {
    if (!menu || !parent) return false;

    // Don't show if already active
    if (menu->active) return false;

    menu->on_action = on_action;

    // Create UI
    if (!create_menu_ui(menu, parent, show_icons)) {
        return false;
    }

    // Audio feedback
    beep_turn_on();

    return true;
}

void quick_menu_hide(quick_menu_t* menu) {
    if (!menu || !menu->active) return;

    // Delete menu object (includes all children)
    if (menu->menu_obj) {
        lv_obj_del(menu->menu_obj);
        menu->menu_obj = NULL;
    }

    menu->list = NULL;
    menu->parent = NULL;
    menu->active = false;
    menu->selected_index = 0;
    menu->item_count = 0;
    menu->on_action = NULL;
    // Clear button pointers (objects are deleted with menu_obj)
    for (uint8_t i = 0; i < QUICK_ACTION_COUNT; i++) {
        menu->items[i] = NULL;
    }
}

bool quick_menu_is_active(quick_menu_t* menu) {
    return menu ? menu->active : false;
}

bool quick_menu_handle_key(quick_menu_t* menu, uint32_t key) {
    if (!menu || !menu->active || menu->item_count == 0) return false;

    switch (key) {
        case LV_KEY_UP:
            if (menu->selected_index > 0) {
                unhighlight_item(menu, menu->selected_index);
                menu->selected_index--;
                highlight_item(menu, menu->selected_index);
                beep_turn_on();
            }
            return true;

        case LV_KEY_DOWN:
            if (menu->selected_index < menu->item_count - 1) {
                unhighlight_item(menu, menu->selected_index);
                menu->selected_index++;
                highlight_item(menu, menu->selected_index);
                beep_turn_on();
            }
            return true;

        case LV_KEY_ENTER: {
            // Select current item
            uint8_t idx = menu->selected_index;
            if (idx < menu->item_count) {
                quick_action_t action = menu->item_actions[idx];
                void (*on_action)(quick_action_t) = menu->on_action;
                beep_turn_on();
                quick_menu_hide(menu);
                if (on_action) {
                    on_action(action);
                }
            }
            return true;
        }

        case LV_KEY_LEFT:
        case LV_KEY_ESC:
            // Cancel/close menu
            beep_turn_on();
            quick_menu_hide(menu);
            return true;

        default:
            return false;
    }
}

quick_action_t quick_menu_get_selected(quick_menu_t* menu) {
    if (!menu || !menu->active || menu->selected_index >= menu->item_count) {
        return QUICK_ACTION_NONE;
    }
    return menu->item_actions[menu->selected_index];
}

void quick_menu_set_item_enabled(quick_menu_t* menu, quick_action_t action, bool enabled) {
    // Note: This modifies static data - not thread-safe
    // For runtime enable/disable, would need dynamic array
    if (action > 0 && action < QUICK_ACTION_COUNT) {
        // Cannot modify const data directly
        // In production, would need mutable item array
    }
}

void quick_menu_cleanup(quick_menu_t* menu) {
    if (!menu) return;

    quick_menu_hide(menu);
    memset(menu, 0, sizeof(quick_menu_t));

    if (menu == &g_menu) {
        g_menu_initialized = false;
    }
}

const char* quick_menu_action_name(quick_action_t action) {
    if (action <= 0 || action >= QUICK_ACTION_COUNT) {
        return "NONE";
    }
    return menu_items[action].label;
}

// Restore diagnostic settings
#pragma GCC diagnostic pop
