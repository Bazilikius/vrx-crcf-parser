#include "page_drone_finder.h"
#include "page_menu.h"
#include "rx5808.h"
#include "beep.h"
#include "lvgl_stl.h"
#include "lv_port_indev.h"

LV_FONT_DECLARE(lv_font_chinese_12);

static lv_obj_t* finder_container = NULL;
static lv_obj_t* rssi_bar_a;
static lv_obj_t* rssi_bar_b;
static lv_obj_t* rssi_label_a;
static lv_obj_t* rssi_label_b;
static lv_obj_t* peak_label_a;
static lv_obj_t* peak_label_b;
static lv_obj_t* exit_button;
static lv_group_t* finder_group;
static lv_timer_t* update_timer;

// Peak tracking
static uint16_t peak_rssi_a = 0;
static uint16_t peak_rssi_b = 0;
static uint32_t last_beep_time = 0;

static void page_drone_finder_exit(void);
static void update_drone_finder(lv_timer_t* timer);

static void event_handler(lv_event_t* event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_KEY) {
        lv_key_t key = lv_indev_get_key(lv_indev_get_act());
        if (key == LV_KEY_LEFT || key == LV_KEY_ENTER) {
            beep_turn_on();
            page_drone_finder_exit();
            lv_fun_param_delayed(page_menu_create, 500, item_drone_finder);
        }
        else if (key == LV_KEY_RIGHT) {
            // Reset peak values
            beep_turn_on();
            peak_rssi_a = 0;
            peak_rssi_b = 0;
            lv_label_set_text(peak_label_a, "Peak: ---");
            lv_label_set_text(peak_label_b, "Peak: ---");
        }
    }
}

static void page_drone_finder_exit(void)
{
    if (update_timer) {
        lv_timer_del(update_timer);
        update_timer = NULL;
    }
    if (finder_group) {
        lv_group_del(finder_group);
        finder_group = NULL;
    }
    if (finder_container) {
        lv_obj_del_delayed(finder_container, 500);
        finder_container = NULL;
    }
}

void page_drone_finder_create(void)
{
    // Reset peaks
    peak_rssi_a = 0;
    peak_rssi_b = 0;
    last_beep_time = 0;

    finder_container = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(finder_container);
    lv_obj_set_style_bg_color(finder_container, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(finder_container, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_size(finder_container, 160, 80);
    lv_obj_set_pos(finder_container, 0, 0);

    // RX A Section (top half)
    lv_obj_t* label_a_title = lv_label_create(finder_container);
    lv_obj_set_style_text_font(label_a_title, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_a_title, lv_color_make(0, 255, 255), LV_STATE_DEFAULT);
    lv_obj_set_pos(label_a_title, 5, 2);
    lv_label_set_text(label_a_title, "RX A:");

    rssi_bar_a = lv_bar_create(finder_container);
    lv_obj_set_size(rssi_bar_a, 100, 12);
    lv_obj_set_pos(rssi_bar_a, 35, 2);
    lv_bar_set_range(rssi_bar_a, 0, 100);
    lv_obj_set_style_bg_color(rssi_bar_a, lv_color_make(50, 50, 50), LV_PART_MAIN);
    lv_obj_set_style_bg_color(rssi_bar_a, lv_color_make(0, 255, 0), LV_PART_INDICATOR);

    rssi_label_a = lv_label_create(finder_container);
    lv_obj_set_style_text_font(rssi_label_a, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(rssi_label_a, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_pos(rssi_label_a, 140, 2);
    lv_label_set_text(rssi_label_a, "0");

    peak_label_a = lv_label_create(finder_container);
    lv_obj_set_style_text_font(peak_label_a, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(peak_label_a, lv_color_make(255, 150, 0), LV_STATE_DEFAULT);
    lv_obj_set_pos(peak_label_a, 5, 16);
    lv_label_set_text(peak_label_a, "Peak: ---");

    // RX B Section (bottom half)
    lv_obj_t* label_b_title = lv_label_create(finder_container);
    lv_obj_set_style_text_font(label_b_title, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_b_title, lv_color_make(255, 0, 255), LV_STATE_DEFAULT);
    lv_obj_set_pos(label_b_title, 5, 30);
    lv_label_set_text(label_b_title, "RX B:");

    rssi_bar_b = lv_bar_create(finder_container);
    lv_obj_set_size(rssi_bar_b, 100, 12);
    lv_obj_set_pos(rssi_bar_b, 35, 30);
    lv_bar_set_range(rssi_bar_b, 0, 100);
    lv_obj_set_style_bg_color(rssi_bar_b, lv_color_make(50, 50, 50), LV_PART_MAIN);
    lv_obj_set_style_bg_color(rssi_bar_b, lv_color_make(0, 255, 0), LV_PART_INDICATOR);

    rssi_label_b = lv_label_create(finder_container);
    lv_obj_set_style_text_font(rssi_label_b, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(rssi_label_b, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_pos(rssi_label_b, 140, 30);
    lv_label_set_text(rssi_label_b, "0");

    peak_label_b = lv_label_create(finder_container);
    lv_obj_set_style_text_font(peak_label_b, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(peak_label_b, lv_color_make(255, 150, 0), LV_STATE_DEFAULT);
    lv_obj_set_pos(peak_label_b, 5, 44);
    lv_label_set_text(peak_label_b, "Peak: ---");

    // Exit button
    exit_button = lv_label_create(finder_container);
    lv_obj_set_style_text_font(exit_button, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(exit_button, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(exit_button, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(exit_button, lv_color_make(100, 100, 100), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(exit_button, lv_color_make(255, 100, 0), LV_STATE_FOCUSED);
    lv_obj_set_style_radius(exit_button, 3, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(exit_button, 2, LV_STATE_DEFAULT);
    lv_obj_set_pos(exit_button, 100, 58);

    if (RX5808_Get_Language() == 0) {
        lv_label_set_text(exit_button, "EXIT (LEFT)");
    } else {
        lv_obj_set_style_text_font(exit_button, &lv_font_chinese_12, LV_STATE_DEFAULT);
        lv_label_set_text(exit_button, "退出");
    }

    // Help text
    lv_obj_t* help_label = lv_label_create(finder_container);
    lv_obj_set_style_text_font(help_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(help_label, lv_color_make(150, 150, 150), LV_STATE_DEFAULT);
    lv_obj_set_pos(help_label, 5, 58);
    lv_label_set_text(help_label, "RIGHT:Reset");

    // Input group
    finder_group = lv_group_create();
    lv_indev_set_group(indev_keypad, finder_group);
    lv_obj_add_event_cb(exit_button, event_handler, LV_EVENT_KEY, NULL);
    lv_group_add_obj(finder_group, exit_button);
    lv_group_set_editing(finder_group, true);

    // Start update timer (100ms for responsive audio feedback)
    update_timer = lv_timer_create(update_drone_finder, 100, NULL);
}

static void update_drone_finder(lv_timer_t* timer)
{
    // Get current RSSI percentages (0-100)
    int rssi_a = (int)Rx5808_Get_Precentage0();
    int rssi_b = (int)Rx5808_Get_Precentage1();

    // Update bars
    lv_bar_set_value(rssi_bar_a, rssi_a, LV_ANIM_OFF);
    lv_bar_set_value(rssi_bar_b, rssi_b, LV_ANIM_OFF);

    // Update labels
    lv_label_set_text_fmt(rssi_label_a, "%d", rssi_a);
    lv_label_set_text_fmt(rssi_label_b, "%d", rssi_b);

    // Update peaks
    if (rssi_a > peak_rssi_a) {
        peak_rssi_a = rssi_a;
        lv_label_set_text_fmt(peak_label_a, "Peak: %d", peak_rssi_a);
    }
    if (rssi_b > peak_rssi_b) {
        peak_rssi_b = rssi_b;
        lv_label_set_text_fmt(peak_label_b, "Peak: %d", peak_rssi_b);
    }

    // Update bar colors based on signal strength
    if (rssi_a >= 70) {
        lv_obj_set_style_bg_color(rssi_bar_a, lv_color_make(0, 255, 0), LV_PART_INDICATOR);  // Green - strong
    } else if (rssi_a >= 40) {
        lv_obj_set_style_bg_color(rssi_bar_a, lv_color_make(255, 255, 0), LV_PART_INDICATOR);  // Yellow - medium
    } else {
        lv_obj_set_style_bg_color(rssi_bar_a, lv_color_make(255, 100, 0), LV_PART_INDICATOR);  // Orange - weak
    }

    if (rssi_b >= 70) {
        lv_obj_set_style_bg_color(rssi_bar_b, lv_color_make(0, 255, 0), LV_PART_INDICATOR);
    } else if (rssi_b >= 40) {
        lv_obj_set_style_bg_color(rssi_bar_b, lv_color_make(255, 255, 0), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color(rssi_bar_b, lv_color_make(255, 100, 0), LV_PART_INDICATOR);
    }

    // Audio feedback - use strongest receiver
    int max_rssi = (rssi_a > rssi_b) ? rssi_a : rssi_b;
    uint32_t current_time = lv_tick_get();
    uint32_t beep_interval;

    // Beep interval based on signal strength (faster = stronger signal)
    if (max_rssi >= 80) {
        beep_interval = 100;  // Very fast beeps - very close!
    } else if (max_rssi >= 60) {
        beep_interval = 250;  // Fast beeps - close
    } else if (max_rssi >= 40) {
        beep_interval = 500;  // Medium beeps - moderate distance
    } else if (max_rssi >= 20) {
        beep_interval = 1000;  // Slow beeps - far
    } else {
        beep_interval = 2000;  // Very slow beeps - very far or no signal
    }

    // Trigger beep if interval elapsed
    if (current_time - last_beep_time >= beep_interval) {
        beep_turn_on();
        last_beep_time = current_time;
    }
}
