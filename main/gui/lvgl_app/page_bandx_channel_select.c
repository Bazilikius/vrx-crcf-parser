#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"

#include "page_bandx_channel_select.h"
#include "page_main.h"
#include "page_spectrum.h"
#include "rx5808.h"
#include "lvgl_stl.h"
#include "beep.h"
#include <stdio.h>

LV_FONT_DECLARE(lv_font_chinese_12);

// External declarations
extern lv_indev_t* indev_keypad;

// UI objects
static lv_obj_t* page_contain;
static lv_obj_t* title_label;
static lv_obj_t* instruction_label;
static lv_obj_t* channel_labels[8];
static lv_group_t* channel_group;

// State
static uint8_t selected_channel = 0;  // 0-7 for channels 1-8
static bool exit_pending = false;
static uint32_t enter_press_start = 0;
static bool enter_is_pressed = false;

// Forward declarations
static void channel_select_event_handler(lv_event_t* event);
static void channel_select_exit_callback(lv_anim_t* anim);
static void update_channel_display(void);

// Update channel display to show selection
static void update_channel_display(void)
{
    for (int i = 0; i < 8; i++) {
        uint16_t freq = RX5808_Get_Band_X_Freq(i);
        if (i == selected_channel) {
            lv_obj_set_style_text_color(channel_labels[i], lv_color_hex(0x00FF00), 0);
            lv_label_set_text_fmt(channel_labels[i], ">CH%d %d", i + 1, freq);
        } else {
            lv_obj_set_style_text_color(channel_labels[i], lv_color_hex(0x808080), 0);
            lv_label_set_text_fmt(channel_labels[i], " CH%d %d", i + 1, freq);
        }
    }
}

// Event handler
static void channel_select_event_handler(lv_event_t* event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (exit_pending) return;

    uint32_t key = lv_indev_get_key(lv_indev_get_act());

    // Handle ENTER key press and release for long press detection
    if (key == LV_KEY_ENTER) {
        if (code == LV_EVENT_PRESSED) {
            enter_press_start = lv_tick_get();
            enter_is_pressed = true;
            return;
        } else if (code == LV_EVENT_RELEASED) {
            // Only process release if we saw a press first
            if (enter_press_start == 0) {
                return;
            }

            uint32_t press_duration = lv_tick_get() - enter_press_start;
            enter_is_pressed = false;
            enter_press_start = 0;  // Reset for next press

            // Long press (>500ms) - exit to main page
            if (press_duration >= 500) {
                beep_turn_on();
                page_bandx_channel_select_exit();
                return;
            }

            // Short press - open spectrum for selected channel
            beep_turn_on();

            // Clean up this page before navigating
            if (channel_group) {
                lv_group_remove_all_objs(channel_group);
                lv_group_del(channel_group);
                channel_group = NULL;
            }
            if (page_contain) {
                lv_obj_del(page_contain);
                page_contain = NULL;
            }
            // Open spectrum analyzer for selected channel
            page_spectrum_create(true, selected_channel);
            return;
        }
    }

    if (code != LV_EVENT_KEY) return;

    switch (key) {
        case LV_KEY_ESC:
        case LV_KEY_BACKSPACE:
            beep_turn_on();
            page_bandx_channel_select_exit();
            break;

        case LV_KEY_UP:
            beep_turn_on();
            if (selected_channel > 0) {
                selected_channel--;
                update_channel_display();
            }
            break;

        case LV_KEY_DOWN:
            beep_turn_on();
            if (selected_channel < 7) {
                selected_channel++;
                update_channel_display();
            }
            break;

        case LV_KEY_LEFT:
            beep_turn_on();
            if (selected_channel >= 4) {
                selected_channel -= 4;
                update_channel_display();
            }
            break;

        case LV_KEY_RIGHT:
            beep_turn_on();
            if (selected_channel < 4) {
                selected_channel += 4;
                update_channel_display();
            }
            break;
    }
}

void page_bandx_channel_select_exit(void)
{
    if (exit_pending) return;

    exit_pending = true;
    beep_turn_on();

    // Remove from group and delete it
    if (channel_group) {
        lv_group_remove_all_objs(channel_group);
        lv_group_del(channel_group);
        channel_group = NULL;
    }

    // Exit animation
    lv_anim_t anim_out;
    lv_anim_init(&anim_out);
    lv_anim_set_var(&anim_out, page_contain);
    lv_anim_set_values(&anim_out, 0, -80);
    lv_anim_set_time(&anim_out, 300);
    lv_anim_set_exec_cb(&anim_out, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_path_cb(&anim_out, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&anim_out, channel_select_exit_callback);
    lv_anim_start(&anim_out);
}

static void channel_select_exit_callback(lv_anim_t* anim)
{
    lv_obj_del_async(page_contain);
    page_contain = NULL;
    page_main_create();
}

void page_bandx_channel_select_create(void)
{
    exit_pending = false;
    selected_channel = 0;
    enter_is_pressed = false;

    // Clean up any existing page/group
    if (channel_group) {
        lv_group_remove_all_objs(channel_group);
        lv_group_del(channel_group);
        channel_group = NULL;
    }
    if (page_contain) {
        lv_obj_del(page_contain);
        page_contain = NULL;
    }

    // Create container
    page_contain = lv_obj_create(lv_scr_act());
    lv_obj_set_size(page_contain, 160, 80);
    lv_obj_set_pos(page_contain, 0, 80);  // Start off-screen
    lv_obj_set_style_bg_color(page_contain, lv_color_black(), 0);
    lv_obj_set_style_border_width(page_contain, 0, 0);
    lv_obj_set_style_pad_all(page_contain, 2, 0);
    lv_obj_clear_flag(page_contain, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    title_label = lv_label_create(page_contain);
    lv_label_set_text(title_label, "Band X Edit");
    lv_obj_set_style_text_font(title_label, &lv_font_chinese_12, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x00E0E0), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 0);

    // Instructions
    instruction_label = lv_label_create(page_contain);
    lv_label_set_text(instruction_label, "OK=Spectrum  Hold=Exit");
    lv_obj_set_style_text_font(instruction_label, &lv_font_chinese_12, 0);
    lv_obj_set_style_text_color(instruction_label, lv_color_hex(0xFFFF00), 0);
    lv_obj_align(instruction_label, LV_ALIGN_TOP_MID, 0, 14);

    // Create 2 columns of 4 channels each — single merged label per slot
    // Col 0: x=2 (CH1-CH4), Col 1: x=82 (CH5-CH8), each 76px wide
    // Format: ">CH1 5705" / " CH1 5705" — no MHz suffix saves space
    for (int i = 0; i < 8; i++) {
        int col = i / 4;  // 0 or 1
        int row = i % 4;  // 0, 1, 2, 3

        channel_labels[i] = lv_label_create(page_contain);
        lv_obj_set_style_text_font(channel_labels[i], &lv_font_chinese_12, 0);
        lv_obj_set_pos(channel_labels[i], 2 + col * 80, 28 + row * 13);
        lv_obj_set_size(channel_labels[i], 76, 13);
        lv_label_set_long_mode(channel_labels[i], LV_LABEL_LONG_CLIP);
    }

    // Update initial display
    update_channel_display();

    // Create input group - add main container to group
    channel_group = lv_group_create();
    lv_group_add_obj(channel_group, page_contain);
    lv_indev_set_group(indev_keypad, channel_group);
    lv_obj_add_event_cb(page_contain, channel_select_event_handler, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(page_contain, channel_select_event_handler, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(page_contain, channel_select_event_handler, LV_EVENT_RELEASED, NULL);

    // Entry animation
    lv_anim_t anim_in;
    lv_anim_init(&anim_in);
    lv_anim_set_var(&anim_in, page_contain);
    lv_anim_set_values(&anim_in, 80, 0);
    lv_anim_set_time(&anim_in, 300);
    lv_anim_set_exec_cb(&anim_in, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_path_cb(&anim_in, lv_anim_path_ease_out);
    lv_anim_start(&anim_in);
}

#pragma GCC diagnostic pop
