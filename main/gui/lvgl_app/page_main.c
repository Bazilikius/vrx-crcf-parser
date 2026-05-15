// Suppress warnings for LVGL animation callbacks
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"

#include "page_main.h"
#include "page_menu.h"
#include "page_spectrum.h"
#include "page_scan.h"
#include "page_scan_table.h"
#include "page_setup.h"
#include "page_diversity_calib.h"
#include "page_bandx_channel_select.h"
#include "rx5808.h"
#include "rx5808_config.h"
#include "lvgl_stl.h"
#include "lv_port_disp.h"
#include <stdlib.h>
#include <inttypes.h>
#include "system.h"
#include "beep.h"
#include "lv_port_disp.h"
#include "diversity.h"
#include "navigation.h"
#include "../quick_menu.h"
#include "led.h"

//LV_FONT_DECLARE(lv_font_chinese_16);
LV_FONT_DECLARE(lv_font_chinese_12);

#define page_main_anim_enter  lv_anim_path_bounce
#define page_main_anim_leave  lv_anim_path_bounce


LV_IMG_DECLARE(lock_img);
LV_FONT_DECLARE(lv_font_fre);

static lv_obj_t* main_contain;
static lv_obj_t* lock_btn;
static lv_obj_t* lv_channel_label;
static lv_obj_t* frequency_label_contain;
static lv_obj_t* frequency_label[fre_pre_cur_count][fre_label_count];

static lv_obj_t* rssi_bar0;
static lv_obj_t* rssi_label0;

static lv_obj_t* rssi_bar1;
static lv_obj_t* rssi_label1;
static lv_obj_t* lv_rsss0_label;
static lv_obj_t* lv_rsss1_label;

static lv_obj_t* rssi_chart;
static lv_chart_series_t* rssi_curve;

static lv_timer_t* page_main_update_timer;

// Diversity telemetry UI elements
static lv_obj_t* diversity_switch_label;  // Switch counter
static lv_obj_t* diversity_delta_label;   // RSSI delta
static lv_obj_t* diversity_mode_label;    // Current mode

// ExpressLRS backpack status indicator
static lv_obj_t* backpack_status_label;   // Shows "ELRS" when backpack detected

bool lock_flag = false;    //lock
static bool page_main_active = false;  // Track if page is active to prevent callbacks after exit
static bool band_x_freq_edit_mode = false;  // Track if in Band X frequency edit mode
static lv_timer_t* blink_timer = NULL;      // Timer for blinking frequency labels
static bool blink_visible = true;           // Blink state
static uint32_t last_click_time = 0;        // For double-click detection (ms)
#define DOUBLE_CLICK_TIME_MS 500            // Maximum time between clicks for double-click

// Band X auto-repeat feature
static uint32_t bandx_key_press_start = 0;  // When key was pressed
static bool bandx_key_is_held = false;      // Track if key is being held
static lv_key_t bandx_held_key = 0;          // Which key is being held
#define BANDX_AUTO_REPEAT_START_MS 500       // Start auto-repeat after 500ms
#define BANDX_FAST_ADJUST_START_MS 2000      // Fast adjust (10MHz) after 2s
#define BANDX_AUTO_REPEAT_INTERVAL_MS 100    // Repeat every 100ms

// Quick menu support
static quick_menu_t* quick_menu = NULL;

// Hold-LEFT tracking for locked quick menu (LV_EVENT_LONG_PRESSED never fires for directional keys)
static uint32_t hold_left_start_time = 0;
static bool hold_left_menu_opened = false;
#define HOLD_LEFT_MENU_MS 1500  // ms to hold LEFT while locked to open quick menu

// Deferred navigation: stores the action selected in the quick menu so that
// the one-shot lv_timer callback can launch the correct page after page_main
// has fully torn itself down (avoids blocking Core 0 with vTaskDelay).
static quick_action_t pending_nav_action = QUICK_ACTION_EXIT;

static void deferred_nav_timer_cb(lv_timer_t* tmr)
{
    lv_timer_del(tmr);  // one-shot: remove immediately after firing
    switch (pending_nav_action) {
        case QUICK_ACTION_SCAN:
            page_scan_table_create_from_main();
            break;
        case QUICK_ACTION_SPECTRUM:
            page_spectrum_create(false, 0);
            break;
        case QUICK_ACTION_CALIBRATION:
            page_diversity_calib_create();
            break;
        case QUICK_ACTION_BAND_X:
            page_bandx_channel_select_create();
            break;
        case QUICK_ACTION_SETTINGS:
            page_setup_create();
            break;
        default:
            break;
    }
}

static void page_main_exit(void);
static void page_main_group_create(void);
static void fre_label_update(uint8_t a, uint8_t b);
static void fre_label_update_band_x(uint16_t freq);
static void blink_timer_callback(lv_timer_t* timer);
static void start_band_x_freq_edit(void);
static void stop_band_x_freq_edit(void);
static uint32_t bandx_get_freq_color(uint16_t freq);
static void bandx_adjust_frequency(int8_t direction);
static void quick_menu_action_handler(quick_action_t action);
static void locked_adjust_channel(int8_t direction, bool is_band_x);
static void adjust_band(int8_t direction, bool is_band_x);

/**
 * @brief Handle quick menu action selection
 */
static void quick_menu_action_handler(quick_action_t action) {
    if (!page_main_active) return;

    // Exit just closes the quick menu – stay on main page
    if (action == QUICK_ACTION_EXIT) return;

    page_main_exit();

    // Defer page creation with a non-blocking 50 ms lv_timer so that
    // page_main_exit() fully tears down its LVGL objects before the next
    // page builds its own.  The old vTaskDelay(50) blocked Core 0's LVGL
    // task, stalling rendering and input for the entire delay period.
    pending_nav_action = action;
    lv_timer_create(deferred_nav_timer_cb, 50, NULL);
}

static void locked_adjust_channel(int8_t direction, bool is_band_x)
{
    if (direction < 0) {
        if (is_band_x) {
            // Band X: Change channel within Band X
            channel_count--;
            if (channel_count < 0)
                channel_count = 7;
            uint16_t freq = RX5808_Get_Band_X_Freq(channel_count);
            RX5808_Set_Freq(freq);
            Rx5808_Set_Channel(channel_count + Chx_count * 8);
            rx5808_div_setup_upload(rx5808_div_config_channel);
            fre_label_update_band_x(freq);
            lv_label_set_text_fmt(lv_channel_label, "X%d", channel_count + 1);
        } else {
            // Normal: Change channel
            channel_count--;
            if (channel_count < 0)
                channel_count = 7;
            RX5808_Set_Freq(RX5808_Get_Current_Freq());
            Rx5808_Set_Channel(channel_count + Chx_count * 8);
            rx5808_div_setup_upload(rx5808_div_config_channel);
            fre_label_update(Chx_count, channel_count);
            lv_label_set_text_fmt(lv_channel_label, "%c%d", Rx5808_ChxMap[Chx_count], channel_count + 1);
        }
    } else {
        if (is_band_x) {
            // Band X: Change channel within Band X
            channel_count++;
            if (channel_count > 7)
                channel_count = 0;
            uint16_t freq = RX5808_Get_Band_X_Freq(channel_count);
            RX5808_Set_Freq(freq);
            Rx5808_Set_Channel(channel_count + Chx_count * 8);
            rx5808_div_setup_upload(rx5808_div_config_channel);
            fre_label_update_band_x(freq);
            lv_label_set_text_fmt(lv_channel_label, "X%d", channel_count + 1);
        } else {
            // Normal: Change channel
            channel_count++;
            if (channel_count > 7)
                channel_count = 0;
            RX5808_Set_Freq(RX5808_Get_Current_Freq());
            Rx5808_Set_Channel(channel_count + Chx_count * 8);
            rx5808_div_setup_upload(rx5808_div_config_channel);
            fre_label_update(Chx_count, channel_count);
            lv_label_set_text_fmt(lv_channel_label, "%c%d", Rx5808_ChxMap[Chx_count], channel_count + 1);
        }
    }
}

static void adjust_band(int8_t direction, bool is_band_x)
{
    if (direction < 0) {
        Chx_count--;
        if (Chx_count < 0)
            Chx_count = 6;
    } else {
        Chx_count++;
        if (Chx_count > 6)
            Chx_count = 0;
    }

    RX5808_Set_Freq(RX5808_Get_Current_Freq());
    Rx5808_Set_Channel(channel_count + Chx_count * 8);
    rx5808_div_setup_upload(rx5808_div_config_channel);

    if (is_band_x || RX5808_Is_Band_X()) {
        fre_label_update_band_x(RX5808_Get_Band_X_Freq(channel_count));
        lv_label_set_text_fmt(lv_channel_label, "X%d", channel_count + 1);
    } else {
        fre_label_update(Chx_count, channel_count);
        lv_label_set_text_fmt(lv_channel_label, "%c%d", Rx5808_ChxMap[Chx_count], channel_count + 1);
    }
}

static void event_callback(lv_event_t* event)
{
    lv_event_code_t code = lv_event_get_code(event);
    bool is_band_x = RX5808_Is_Band_X();

    // Quick menu intercept: route all input to quick menu while it is active
    if (quick_menu && quick_menu_is_active(quick_menu)) {
        if (code == LV_EVENT_KEY) {
            lv_key_t key = lv_indev_get_key(lv_indev_get_act());
            // Swallow the LEFT key-repeats from the hold that opened the menu,
            // until the user physically releases and re-presses LEFT.
            if (key == LV_KEY_LEFT && hold_left_menu_opened) {
                return;  // still holding the trigger key – don't cancel yet
            }
            quick_menu_handle_key(quick_menu, key);
        } else if (code == LV_EVENT_RELEASED) {
            // User lifted the key that opened the menu – LEFT can now cancel normally
            hold_left_menu_opened = false;
            hold_left_start_time = 0;
        }
        return;
    }

    if (code == LV_EVENT_KEY) {
        lv_key_t key = lv_indev_get_key(lv_indev_get_act());

        // While locked (lock_flag=false=RED): only track hold-LEFT for quick menu
        if (!lock_flag) {
            if (key == LV_KEY_LEFT) {
                if (hold_left_start_time == 0) {
                    hold_left_start_time = lv_tick_get();
                } else if (!hold_left_menu_opened &&
                           lv_tick_elaps(hold_left_start_time) >= HOLD_LEFT_MENU_MS) {
                    hold_left_menu_opened = true;
                    if (quick_menu && !quick_menu_is_active(quick_menu)) {
                        beep_turn_on();
                        quick_menu_show(quick_menu, main_contain, quick_menu_action_handler, false);
                    }
                }
            } else {
                hold_left_start_time = 0;
                hold_left_menu_opened = false;
            }
            return;
        }

        // Unlocked: reset hold-left state, process navigation normally
        hold_left_start_time = 0;
        hold_left_menu_opened = false;

        if (key == LV_KEY_UP || key == LV_KEY_DOWN ||
            key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
            beep_turn_on();
        }

        // RIGHT/LEFT = channel, UP/DOWN = band (unlocked only)
        if (key == LV_KEY_RIGHT) {
            locked_adjust_channel(1, is_band_x);
        } else if (key == LV_KEY_LEFT) {
            locked_adjust_channel(-1, is_band_x);
        } else if (key == LV_KEY_UP) {
            if (is_band_x && band_x_freq_edit_mode)
                bandx_adjust_frequency(1);
            else
                adjust_band(-1, is_band_x);
        } else if (key == LV_KEY_DOWN) {
            if (is_band_x && band_x_freq_edit_mode)
                bandx_adjust_frequency(-1);
            else
                adjust_band(1, is_band_x);
        }
    }
    else if (code == LV_EVENT_PRESSED) {
        if (lock_flag) {
            lv_key_t key = lv_indev_get_key(lv_indev_get_act());
            if (is_band_x && band_x_freq_edit_mode &&
                (key == LV_KEY_UP || key == LV_KEY_DOWN)) {
                bandx_key_press_start = lv_tick_get();
                bandx_key_is_held = true;
                bandx_held_key = key;
            }
        }
    }
    else if (code == LV_EVENT_RELEASED) {
        // Always reset hold-left tracking on any key release
        hold_left_start_time = 0;
        hold_left_menu_opened = false;
        if (lock_flag) {
            lv_key_t key = lv_indev_get_key(lv_indev_get_act());
            if (is_band_x && band_x_freq_edit_mode &&
                (key == LV_KEY_UP || key == LV_KEY_DOWN)) {
                bandx_key_is_held = false;
                bandx_key_press_start = 0;
            }
        }
    }
    else if (code == LV_EVENT_CLICKED) {
        beep_turn_on();
        if (!lock_flag && is_band_x) {
            uint32_t now = lv_tick_get();
            bool is_double_click = (now - last_click_time) < DOUBLE_CLICK_TIME_MS;
            last_click_time = now;
            if (band_x_freq_edit_mode) {
                stop_band_x_freq_edit();
                RX5808_Save_Band_X_To_NVS();
                lv_obj_t* save_label = lv_label_create(main_contain);
                lv_label_set_text(save_label, "Saved!");
                lv_obj_set_style_text_color(save_label, lv_color_hex(0x00FF00), 0);
                lv_obj_align(save_label, LV_ALIGN_CENTER, 0, 0);
                lv_obj_del_delayed(save_label, 1000);
            } else if (is_double_click) {
                start_band_x_freq_edit();
            }
        } else if (lock_flag) {
            // Unlocked (grey): open menu
            page_main_exit();
            lv_fun_param_delayed(page_menu_create, 500, 0);
        }
    }
    else if (code == LV_EVENT_LONG_PRESSED) {
        lv_key_t key = lv_indev_get_key(lv_indev_get_act());

        // Directional keys never toggle lock (LONG_PRESSED only fires for ENTER on keypad indev)
        if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT ||
            key == LV_KEY_UP || key == LV_KEY_DOWN) {
            return;
        }

        // Center/ENTER long press: toggle lock/unlock
        // lock_flag=false=RED=LOCKED, lock_flag=true=GREY=UNLOCKED
        beep_turn_on();
        if (!lock_flag) {
            // Currently locked (red) → unlock (grey)
            lv_obj_set_style_bg_color(lock_btn, lv_color_make(160, 160, 160), LV_STATE_DEFAULT);
            lock_flag = true;
            led_set_pattern(LED_PATTERN_HEARTBEAT);
        } else {
            // Currently unlocked (grey) → lock (red)
            lv_obj_set_style_bg_color(lock_btn, lv_color_make(255, 0, 0), LV_STATE_DEFAULT);
            lock_flag = false;
            led_set_pattern(LED_PATTERN_SOLID);
        }
        video_composite_switch(lock_flag);
        video_composite_sync_switch(lock_flag);
        // Item 4: slow LVGL refresh + Item 2: lower CPU when locked
        system_set_lvgl_idle(!lock_flag);
        system_set_cpu_context_idle(!lock_flag);
    }
}

static void page_main_update(lv_timer_t* tmr)
{
    // NOTE: diversity_update() was moved to a dedicated FreeRTOS task
    // (diversity_task_fn in diversity.c, spawned from diversity_init) so that
    // RX5808_Set_Freq() — which blocks ~50 ms during PLL settling — never
    // stalls the LVGL rendering loop (fixes N + J).

    // Check for ExpressLRS backpack activity (safe task context)
    #if BACKPACK_DETECTION_ENABLED
    RX5808_Check_Backpack_Activity();
    #endif

    // Check for remote channel changes (e.g., from ELRS backpack)
    static uint8_t last_channel = 0xFF;  // Initialize to invalid value
    uint8_t current_channel = Rx5808_Get_Channel();

    if (current_channel != last_channel && current_channel <= 47) {
        // Channel changed remotely - update display
        last_channel = current_channel;
        uint8_t band = current_channel / 8;
        uint8_t chan = current_channel % 8;

        // Update global band/channel state
        Chx_count = band;
        channel_count = chan;

        // Update frequency display based on band type
        if (band == 6) {
            // Band X (custom frequencies)
            uint16_t freq = RX5808_Get_Band_X_Freq(chan);
            fre_label_update_band_x(freq);
            lv_label_set_text_fmt(lv_channel_label, "X%d", chan + 1);
        } else {
            // Standard bands (A/B/E/F/R/L)
            fre_label_update(band, chan);
            lv_label_set_text_fmt(lv_channel_label, "%c%d", Rx5808_ChxMap[band], chan + 1);
        }
    }

    int rssi0 = (int)Rx5808_Get_Precentage0();
    int rssi1 = (int)Rx5808_Get_Precentage1();

    // Update diversity telemetry overlay
    if (RX5808_Get_Signal_Source() == 0) // Diversity mode
    {
        // Update active receiver and get state
        diversity_rx_t active_rx = diversity_get_active_rx();
        diversity_state_t* state = diversity_get_state();

        // Update mode indicator
        const char* mode_str = "?";
        switch(state->mode) {
            case DIVERSITY_MODE_RACE: mode_str = "RACE"; break;
            case DIVERSITY_MODE_FREESTYLE: mode_str = "FREESTYLE"; break;
            case DIVERSITY_MODE_LONG_RANGE: mode_str = "LONGRANGE"; break;
            case DIVERSITY_MODE_COUNT: mode_str = "?"; break;
        }
        lv_label_set_text(diversity_mode_label, mode_str);

        // Highlight the bar that belongs to the active antenna.
        // Bar/label mapping (determined by the bar-fill code below):
        //   rssi_bar0 / lv_rsss0_label -> rssi1 = adc_converted_value[1] = RX_B
        //   rssi_bar1 / lv_rsss1_label -> rssi0 = adc_converted_value[0] = RX_A
        if (active_rx == DIVERSITY_RX_A) {
            // RX A active -> highlight rssi_bar1 / lv_rsss1_label
            lv_obj_set_style_text_font(lv_rsss1_label, &lv_font_montserrat_16, LV_STATE_DEFAULT);
            lv_obj_set_style_text_opa(lv_rsss1_label, LV_OPA_100, LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(lv_rsss1_label, lv_color_make(0, 255, 255), LV_STATE_DEFAULT);  // Bright cyan
            lv_obj_set_style_border_width(rssi_bar1, 2, LV_PART_MAIN);
            lv_obj_set_style_border_color(rssi_bar1, lv_color_make(0, 255, 0), LV_PART_MAIN);
            // RX B inactive - smaller and dimmer
            lv_obj_set_style_text_font(lv_rsss0_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
            lv_obj_set_style_text_opa(lv_rsss0_label, LV_OPA_60, LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(lv_rsss0_label, lv_color_make(200, 0, 200), LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(rssi_bar0, 0, LV_PART_MAIN);
        } else {
            // RX B active -> highlight rssi_bar0 / lv_rsss0_label
            lv_obj_set_style_text_font(lv_rsss0_label, &lv_font_montserrat_16, LV_STATE_DEFAULT);
            lv_obj_set_style_text_opa(lv_rsss0_label, LV_OPA_100, LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(lv_rsss0_label, lv_color_make(255, 0, 255), LV_STATE_DEFAULT);  // Bright magenta
            lv_obj_set_style_border_width(rssi_bar0, 2, LV_PART_MAIN);
            lv_obj_set_style_border_color(rssi_bar0, lv_color_make(0, 255, 0), LV_PART_MAIN);
            // RX A inactive - smaller and dimmer
            lv_obj_set_style_text_font(lv_rsss1_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
            lv_obj_set_style_text_opa(lv_rsss1_label, LV_OPA_60, LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(lv_rsss1_label, lv_color_make(0, 0, 255), LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(rssi_bar1, 0, LV_PART_MAIN);
        }
    }

    if (RX5808_Get_Signal_Source() == 0)
    {
        lv_bar_set_value(rssi_bar0, rssi1, LV_ANIM_ON);
        lv_label_set_text_fmt(rssi_label0, "%d", rssi1);
        lv_bar_set_value(rssi_bar1, rssi0, LV_ANIM_ON);
        lv_label_set_text_fmt(rssi_label1, "%d", rssi0);

        // Dynamic RSSI bar colors based on signal strength
        lv_color_t color0, color1;

        // rssi_bar0 shows RX_B (rssi1) color
        if (rssi1 >= 70) {
            color0 = lv_color_make(0, 255, 0);  // Green - excellent
        } else if (rssi1 >= 40) {
            color0 = lv_color_make(255, 255, 0);  // Yellow - good
        } else {
            color0 = lv_color_make(255, 0, 0);  // Red - poor
        }
        lv_obj_set_style_bg_color(rssi_bar0, color0, LV_PART_INDICATOR);

        // rssi_bar1 shows RX_A (rssi0) color
        if (rssi0 >= 70) {
            color1 = lv_color_make(0, 255, 0);  // Green - excellent
        } else if (rssi0 >= 40) {
            color1 = lv_color_make(255, 255, 0);  // Yellow - good
        } else {
            color1 = lv_color_make(255, 0, 0);  // Red - poor
        }
        lv_obj_set_style_bg_color(rssi_bar1, color1, LV_PART_INDICATOR);
    }
    else if (RX5808_Get_Signal_Source() == 1)
    {
        lv_chart_set_next_value(rssi_chart, rssi_curve, rssi1);
        lv_label_set_text_fmt(rssi_label0, "%d", rssi1);
    }
    else
    {
        lv_chart_set_next_value(rssi_chart, rssi_curve, rssi0);
        lv_label_set_text_fmt(rssi_label0, "%d", rssi0);
    }

    // Update ExpressLRS backpack status indicator
    #if BACKPACK_DETECTION_ENABLED
    if (RX5808_Is_Backpack_Detected()) {
        lv_label_set_text(backpack_status_label, "ELRS");
        lv_obj_set_style_text_color(backpack_status_label, lv_color_make(255, 180, 0), LV_STATE_DEFAULT);  // Orange warning
    } else {
        lv_label_set_text(backpack_status_label, "");  // Hide when not detected
    }
    #endif

}


static void fre_pre_label_update()
{
    // Safety check: ensure page is still active
    if (!page_main_active) {
        return;
    }

    for (int i = 0; i < fre_label_count; i++)
    {
        if (frequency_label[fre_pre][i] && frequency_label[fre_cur][i]) {
            lv_label_set_text_fmt(frequency_label[fre_pre][i], "%c", lv_label_get_text(frequency_label[fre_cur][i])[0]);
        }
    }
}

static void fre_label_update(uint8_t a, uint8_t b)
{
    for (int i = 0; i < fre_label_count; i++)
    {
        lv_label_set_text_fmt(frequency_label[fre_cur][i], "%d", (int)((Rx5808_Freq[a][b] / lv_pow(10, fre_label_count - i - 1)) % 10));
        if (lv_label_get_text(frequency_label[fre_pre][i])[0] != lv_label_get_text(frequency_label[fre_cur][i])[0])
        {
            lv_amin_start(frequency_label[fre_cur][i], lv_pow(-1, (i % 2)) * 50, 0, 1, 200, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, lv_anim_path_ease_in);
            lv_amin_start(frequency_label[fre_pre][i], 0, lv_pow(-1, (i % 2)) * -50, 1, 200, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, lv_anim_path_ease_in);
        }
    }
    lv_fun_delayed(fre_pre_label_update, 200);
}

static void fre_label_update_band_x(uint16_t freq)
{
    // Update frequency display for Band X with direct frequency value
    // Stop any ongoing animations to prevent display glitches during rapid changes
    for (int i = 0; i < fre_label_count; i++)
    {
        lv_anim_del(frequency_label[fre_cur][i], (lv_anim_exec_xcb_t)lv_obj_set_y);
        lv_anim_del(frequency_label[fre_pre][i], (lv_anim_exec_xcb_t)lv_obj_set_y);

        // Update current label
        lv_label_set_text_fmt(frequency_label[fre_cur][i], "%d", (int)((freq / lv_pow(10, fre_label_count - i - 1)) % 10));

        // Ensure current label is at correct position
        lv_obj_set_y(frequency_label[fre_cur][i], 0);

        if (lv_label_get_text(frequency_label[fre_pre][i])[0] != lv_label_get_text(frequency_label[fre_cur][i])[0])
        {
            lv_amin_start(frequency_label[fre_cur][i], lv_pow(-1, (i % 2)) * 50, 0, 1, 200, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, lv_anim_path_ease_in);
            lv_amin_start(frequency_label[fre_pre][i], 0, lv_pow(-1, (i % 2)) * -50, 1, 200, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, lv_anim_path_ease_in);
        }
    }
    lv_fun_delayed(fre_pre_label_update, 200);
}

static void blink_timer_callback(lv_timer_t* timer)
{
    if (!band_x_freq_edit_mode) return;

    // Handle auto-repeat if key is held
    if (bandx_key_is_held && bandx_key_press_start > 0) {
        uint32_t hold_duration = lv_tick_get() - bandx_key_press_start;

        // Start auto-repeat after 500ms hold
        if (hold_duration >= BANDX_AUTO_REPEAT_START_MS) {
            // Trigger frequency adjustment
            int8_t direction = (bandx_held_key == LV_KEY_UP) ? 1 : -1;
            bandx_adjust_frequency(direction);
        }
    }

    blink_visible = !blink_visible;

    // Toggle visibility of frequency labels with asymmetric timing
    for (uint8_t i = 0; i < fre_label_count; i++) {
        if (blink_visible) {
            lv_obj_clear_flag(frequency_label[fre_cur][i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(frequency_label[fre_cur][i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Asymmetric blink: visible for 600ms, hidden for 150ms
    if (blink_visible) {
        lv_timer_set_period(blink_timer, 600);  // Stay visible longer
    } else {
        lv_timer_set_period(blink_timer, 150);  // Brief off period
    }
}

static void start_band_x_freq_edit(void)
{
    band_x_freq_edit_mode = true;
    blink_visible = true;

    // Create blink timer (100ms period for both blink and auto-repeat)
    if (blink_timer == NULL) {
        blink_timer = lv_timer_create(blink_timer_callback, 100, NULL);
    }

    // Show initial frequency in green (standard range assumed)
    uint16_t freq = RX5808_Get_Band_X_Freq(channel_count);
    uint32_t color = bandx_get_freq_color(freq);
    for (uint8_t i = 0; i < fre_label_count; i++) {
        lv_obj_set_style_text_color(frequency_label[fre_cur][i], lv_color_hex(color), LV_STATE_DEFAULT);
    }
}

static void stop_band_x_freq_edit(void)
{
    band_x_freq_edit_mode = false;
    bandx_key_is_held = false;
    bandx_key_press_start = 0;

    // Delete blink timer
    if (blink_timer != NULL) {
        lv_timer_del(blink_timer);
        blink_timer = NULL;
    }

    // Ensure frequency labels are visible and reset color to white
    blink_visible = true;
    for (uint8_t i = 0; i < fre_label_count; i++) {
        lv_obj_clear_flag(frequency_label[fre_cur][i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(frequency_label[fre_cur][i], lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    }
}

/**
 * @brief Get validation color for Band X frequency
 * @param freq Frequency in MHz
 * @return Color hex value (green  for valid standard range, amber for extended, red for out of range)
 */
static uint32_t bandx_get_freq_color(uint16_t freq)
{
    if (freq >= 5645 && freq <= 5945) {
        // Standard FPV racing range - green
        return 0x00FF00;
    } else if (freq >= 5300 && freq < 5645) {
        // Extended low range - amber
        return 0xFFA500;
    } else if (freq > 5945 && freq <= 5950) {
        // Extended high range - amber
        return 0xFFA500;
    } else {
        // Out of range - red
        return 0xFF0000;
    }
}

/**
 * @brief Adjust Band X frequency with auto-repeat and fast adjust
 * @param direction 1 for increase, -1 for decrease
 */
static void bandx_adjust_frequency(int8_t direction)
{
    uint16_t freq = RX5808_Get_Band_X_Freq(channel_count);
    uint32_t hold_duration = lv_tick_get() - bandx_key_press_start;

    // Determine step size based on hold duration
    uint16_t step = 1;  // Default: 1 MHz
    if (hold_duration >= BANDX_FAST_ADJUST_START_MS) {
        step = 10;  // Fast adjust: 10 MHz after 2s
    }

    // Adjust frequency
    if (direction > 0) {
        freq += step;
        if (freq > 5950) freq = 5950;
    } else {
        freq -= step;
        if (freq < 5300) freq = 5300;
    }

    // Apply frequency
    RX5808_Set_Band_X_Freq(channel_count, freq);
    RX5808_Set_Freq(freq);
    blink_visible = true;  // Show frequency during adjustment
    fre_label_update_band_x(freq);

    // Update color based on range validation
    uint32_t color = bandx_get_freq_color(freq);
    for (uint8_t i = 0; i < fre_label_count; i++) {
        lv_obj_set_style_text_color(frequency_label[fre_cur][i], lv_color_hex(color), LV_STATE_DEFAULT);
    }
}

void page_main_rssi_quality_create(uint16_t type)
{
    if (type == 0)
    {
        lv_rsss0_label = lv_label_create(main_contain);
        lv_label_set_long_mode(lv_rsss0_label, LV_LABEL_LONG_WRAP);
        lv_obj_align(lv_rsss0_label, LV_ALIGN_TOP_LEFT, 0, 50);
        //lv_obj_set_style_text_font(lv_rsss0_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(lv_rsss0_label, lv_color_make(0, 0, 255), LV_STATE_DEFAULT);
        lv_label_set_recolor(lv_rsss0_label, true);
        //lv_label_set_text(lv_rsss0_label, "RSSI1:");

        lv_rsss1_label = lv_label_create(main_contain);
        lv_label_set_long_mode(lv_rsss1_label, LV_LABEL_LONG_WRAP);
        lv_obj_align(lv_rsss1_label, LV_ALIGN_TOP_LEFT, 0, 65);
        //lv_obj_set_style_text_font(lv_rsss1_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(lv_rsss1_label, lv_color_make(200, 0, 200), LV_STATE_DEFAULT);
        lv_label_set_recolor(lv_rsss1_label, true);
        //lv_label_set_text(lv_rsss1_label, "RSSI2:");

        lv_obj_set_style_text_font(lv_rsss0_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(lv_rsss1_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_label_set_text(lv_rsss0_label, "A:");
        lv_label_set_text(lv_rsss1_label, "B:");
        rssi_bar0 = lv_bar_create(main_contain);
        lv_obj_remove_style(rssi_bar0, NULL, LV_PART_KNOB);
        lv_obj_set_size(rssi_bar0, 100, 12);
        // 二值化OSD优化
        lv_obj_set_style_bg_color(rssi_bar0, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_border_color(rssi_bar0, lv_color_make(50, 50, 80), LV_PART_MAIN);
        lv_obj_set_style_border_width(rssi_bar0, 1, LV_PART_MAIN);
        // 二值化OSD优化 end
        lv_obj_set_style_bg_color(rssi_bar0, lv_color_make(0, 0, 200), LV_PART_INDICATOR);
        lv_obj_set_pos(rssi_bar0, 40, 50);
        lv_bar_set_value(rssi_bar0, Rx5808_Get_Precentage1(), LV_ANIM_ON);
        lv_obj_set_style_anim_time(rssi_bar0, 200, LV_STATE_DEFAULT);

        rssi_label0 = lv_label_create(main_contain);
        lv_obj_align_to(rssi_label0, rssi_bar0, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
        lv_obj_set_style_bg_opa(rssi_label0, (lv_opa_t)LV_OPA_COVER, LV_STATE_DEFAULT);
        lv_obj_set_style_radius(rssi_label0, 4, LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(rssi_label0, lv_color_black(), LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(rssi_label0, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(rssi_label0, lv_color_white(), LV_STATE_DEFAULT);
        lv_label_set_recolor(rssi_label0, true);
        lv_label_set_text_fmt(rssi_label0, "%d", (int)Rx5808_Get_Precentage1());

        rssi_bar1 = lv_bar_create(main_contain);
        lv_obj_remove_style(rssi_bar1, NULL, LV_PART_KNOB);
        lv_obj_set_size(rssi_bar1, 100, 12);
        // 二值化OSD优化
        lv_obj_set_style_bg_color(rssi_bar1, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_border_width(rssi_bar1, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(rssi_bar1, lv_color_make(50, 50, 80), LV_PART_MAIN);
        // 二值化OSD优化 end
        lv_obj_set_style_bg_color(rssi_bar1, lv_color_make(200, 0, 200), LV_PART_INDICATOR);
        lv_obj_set_pos(rssi_bar1, 40, 65);
        lv_bar_set_value(rssi_bar1, Rx5808_Get_Precentage0(), LV_ANIM_ON);
        lv_obj_set_style_anim_time(rssi_bar1, 200, LV_STATE_DEFAULT);

        rssi_label1 = lv_label_create(main_contain);
        lv_obj_align_to(rssi_label1, rssi_bar1, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
        lv_obj_set_style_bg_opa(rssi_label1, (lv_opa_t)LV_OPA_COVER, LV_STATE_DEFAULT);
        lv_obj_set_style_radius(rssi_label1, 4, LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(rssi_label1, lv_color_black(), LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(rssi_label1, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(rssi_label1, lv_color_white(), LV_STATE_DEFAULT);
        lv_label_set_recolor(rssi_label1, true);
        lv_label_set_text_fmt(rssi_label1, "%d", (int)Rx5808_Get_Precentage0());

        lv_amin_start(lv_rsss0_label, 80, 50, 1, 800, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_enter);
        lv_amin_start(rssi_label0, 80, 50, 1, 800, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_enter);
        lv_amin_start(rssi_bar0, 80, 51, 1, 800, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_enter);
        lv_amin_start(lv_rsss1_label, 95, 65, 1, 800, 200, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_enter);
        lv_amin_start(rssi_label1, 95, 65, 1, 800, 200, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_enter);
        lv_amin_start(rssi_bar1, 95, 66, 1, 800, 200, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_enter);
    }
    else
    {
        lv_rsss0_label = lv_label_create(main_contain);
        lv_label_set_long_mode(lv_rsss0_label, LV_LABEL_LONG_WRAP);
        lv_obj_align(lv_rsss0_label, LV_ALIGN_TOP_LEFT, 3, 50);
        //lv_obj_set_style_text_font(lv_rsss0_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(lv_rsss0_label, lv_color_make(0, 0, 255), LV_STATE_DEFAULT);
        lv_label_set_recolor(lv_rsss0_label, true);
        //lv_label_set_text(lv_rsss0_label, "RSSI1:");

        if (RX5808_Get_Language() == 0)
        {
            lv_obj_set_style_text_font(lv_rsss0_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
            lv_label_set_text(lv_rsss0_label, "RSSI:");
        }
        else
        {
            lv_obj_set_style_text_font(lv_rsss0_label, &lv_font_chinese_12, LV_STATE_DEFAULT);
            lv_label_set_text(lv_rsss0_label, "信号:");

        }

        rssi_label0 = lv_label_create(main_contain);
        lv_obj_set_style_bg_opa(rssi_label0, (lv_opa_t)LV_OPA_COVER, LV_STATE_DEFAULT);
        lv_obj_set_style_radius(rssi_label0, 4, LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(rssi_label0, lv_color_make(255, 255, 255), LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(rssi_label0, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(rssi_label0, lv_color_make(0, 0, 0), LV_STATE_DEFAULT);
        lv_label_set_recolor(rssi_label0, true);
        lv_obj_set_size(rssi_label0,25,16);
        lv_obj_set_style_text_align(rssi_label0,LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
        lv_obj_align(rssi_label0, LV_ALIGN_TOP_LEFT, 5, 65);
        if(type == 1)
        lv_label_set_text_fmt(rssi_label0, "%d", (int)Rx5808_Get_Precentage1());
        else
        lv_label_set_text_fmt(rssi_label0, "%d", (int)Rx5808_Get_Precentage0());


        rssi_chart = lv_chart_create(main_contain);
        lv_obj_remove_style(rssi_chart, NULL, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(rssi_chart, (lv_opa_t)LV_OPA_TRANSP, LV_STATE_DEFAULT);
        lv_obj_set_size(rssi_chart, 120, 28);
        lv_obj_set_pos(rssi_chart, 35, 51);
        lv_chart_set_type(rssi_chart, LV_CHART_TYPE_LINE);   /*Show lines and points too*/
        lv_obj_set_style_text_color(rssi_chart, lv_color_white(), LV_STATE_DEFAULT);
        lv_chart_set_range(rssi_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
        lv_chart_set_range(rssi_chart, LV_CHART_AXIS_PRIMARY_X, 0, 8);

        lv_obj_set_style_border_color(rssi_chart, lv_color_make(0, 0, 0), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(rssi_chart, lv_color_make(100, 100, 100), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(rssi_chart, 100, LV_STATE_DEFAULT);
        //lv_obj_set_style_text_color(rssi_chart, lv_color_make(0, 0, 255), LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(rssi_chart, 0, LV_STATE_DEFAULT);
        //lv_obj_set_style_text_color_filtered(rssi_chart, lv_color_make(0, 0, 255), LV_STATE_DEFAULT);
        //lv_obj_set_style_text_font(rssi_chart, &lv_font_montserrat_8,LV_STATE_DEFAULT);

        lv_obj_set_style_size(rssi_chart, 0, LV_PART_INDICATOR);
        //lv_chart_set_div_line_count(rssi_chart, 5, 8);
        //lv_chart_set_range(rssi_chart, LV_CHART_AXIS_SECONDARY_Y, -50, 100);
        lv_chart_set_point_count(rssi_chart, 48);
        //lv_chart_set_axis_tick(rssi_chart, LV_CHART_AXIS_PRIMARY_X, 5, 3, 6, 1, true, 40);
        //lv_chart_set_axis_tick(rssi_chart, LV_CHART_AXIS_PRIMARY_Y, 5, 3, 5, 2, true, 50);
        rssi_curve = lv_chart_add_series(rssi_chart, lv_palette_main(LV_PALETTE_PINK), LV_CHART_AXIS_PRIMARY_Y);

        lv_amin_start(lv_rsss0_label, 80, 48, 1, 800, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_enter);
        lv_amin_start(rssi_label0, 80, 62, 1, 800, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_enter);
        lv_amin_start(rssi_chart, 80, 50, 1, 800, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_enter);
    }
}

void page_main_create()
{
    page_main_active = true;  // Mark page as active

    // Initialize LED module (Phase 7: LED Status Feedback)
    static bool led_initialized = false;
    if (!led_initialized) {
        led_init();
        led_initialized = true;
    }

    // Set initial LED pattern based on lock state
    // lock_flag=false=RED=LOCKED, lock_flag=true=GREY=UNLOCKED
    if (!lock_flag) {
        led_set_pattern(LED_PATTERN_SOLID);      // Locked: Solid LED
    } else {
        led_set_pattern(LED_PATTERN_HEARTBEAT);  // Unlocked: Heartbeat
    }
    // Apply initial power context based on lock state
    system_set_lvgl_idle(!lock_flag);
    system_set_cpu_context_idle(!lock_flag);

    // Initialize OSD state based on lock state
    // lock_flag=true (UNLOCKED) → OSD ON, lock_flag=false (LOCKED) → OSD OFF
    video_composite_switch(lock_flag);
    video_composite_sync_switch(lock_flag);

    // Initialize navigation module with lock state
    navigation_init(&lock_flag);

    // Initialize quick menu
    quick_menu = quick_menu_init();

    main_contain = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(main_contain);
    lv_obj_set_style_bg_color(main_contain, lv_color_make(0, 0, 0), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(main_contain, (lv_opa_t)LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_size(main_contain, 160, 80);
    lv_obj_set_pos(main_contain, 0, 0);


    frequency_label_contain = lv_obj_create(main_contain);
    lv_obj_remove_style_all(frequency_label_contain);
    lv_obj_set_style_bg_color(frequency_label_contain, lv_color_make(0, 0, 0), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(frequency_label_contain, (lv_opa_t)LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_size(frequency_label_contain, 130, 50);
    lv_obj_set_pos(frequency_label_contain, 30, 15);


    for (int i = 0; i < fre_label_count; i++)
    {
        frequency_label[fre_pre][i] = lv_label_create(frequency_label_contain);
        lv_label_set_long_mode(frequency_label[fre_pre][i], LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(frequency_label[fre_pre][i], 32 * i, -50);
        lv_obj_set_style_text_font(frequency_label[fre_pre][i], &lv_font_fre, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(frequency_label[fre_pre][i], lv_color_make(255, 200, 50), LV_STATE_DEFAULT);
        lv_label_set_text_fmt(frequency_label[fre_pre][i], "%d", (int)((Rx5808_Freq[Chx_count][channel_count] / lv_pow(10, fre_label_count - i - 1)) % 10));
        lv_label_set_long_mode(frequency_label[fre_pre][i], LV_LABEL_LONG_WRAP);
    }

    for (int i = 0; i < fre_label_count; i++)
    {
        frequency_label[fre_cur][i] = lv_label_create(frequency_label_contain);
        lv_label_set_long_mode(frequency_label[fre_cur][i], LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(frequency_label[fre_cur][i], 32 * i, 0);
        lv_obj_set_style_text_font(frequency_label[fre_cur][i], &lv_font_fre, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(frequency_label[fre_cur][i], lv_color_make(255, 200, 50), LV_STATE_DEFAULT);
        lv_label_set_text_fmt(frequency_label[fre_cur][i], "%d", (int)((Rx5808_Freq[Chx_count][channel_count] / lv_pow(10, fre_label_count - i - 1)) % 10));
        lv_label_set_long_mode(frequency_label[fre_cur][i], LV_LABEL_LONG_WRAP);
    }

    lv_channel_label = lv_label_create(main_contain);
    lv_label_set_long_mode(lv_channel_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(lv_channel_label, LV_ALIGN_TOP_LEFT, 5, 2);
    lv_obj_set_style_text_font(lv_channel_label, &lv_font_montserrat_16, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lv_channel_label, lv_color_make(0, 157, 255), LV_STATE_DEFAULT);
    lv_label_set_recolor(lv_channel_label, true);
    lv_label_set_text_fmt(lv_channel_label, "%c%d", Rx5808_ChxMap[Chx_count], channel_count + 1);


    page_main_rssi_quality_create(RX5808_Get_Signal_Source());

    lock_btn = lv_imgbtn_create(main_contain);
    lv_obj_remove_style(lock_btn, NULL, LV_PART_ANY);
    lv_obj_set_size(lock_btn, 20, 24);
    lv_obj_set_pos(lock_btn, 5, 23);
    lv_obj_set_style_bg_opa(lock_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_width(lock_btn, 20, LV_STATE_PRESSED);
    lv_obj_set_style_height(lock_btn, 24, LV_STATE_PRESSED);
    if (lock_flag == false)
        lv_obj_set_style_bg_color(lock_btn, lv_color_hex(0xff0000), LV_STATE_DEFAULT);
    else
        lv_obj_set_style_bg_color(lock_btn, lv_color_hex(0x999999), LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(lock_btn, lv_color_make(0, 0, 0), LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(lock_btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(lock_btn, 4, 0);
    lv_imgbtn_set_src(lock_btn, LV_IMGBTN_STATE_RELEASED, &lock_img, NULL, NULL);
    lv_imgbtn_set_src(lock_btn, LV_IMGBTN_STATE_PRESSED, &lock_img, NULL, NULL);

    // Create diversity telemetry overlay (only in diversity mode)
    if (RX5808_Get_Signal_Source() == 0)
    {
        // Mode indicator (top right, green)
        diversity_mode_label = lv_label_create(main_contain);
        lv_obj_set_style_text_font(diversity_mode_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(diversity_mode_label, lv_color_make(0, 255, 0), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(diversity_mode_label, LV_OPA_COVER, LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(diversity_mode_label, lv_color_black(), LV_STATE_DEFAULT);
        lv_obj_set_style_radius(diversity_mode_label, 3, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(diversity_mode_label, 0, LV_STATE_DEFAULT);
        lv_obj_set_pos(diversity_mode_label, 82, 0);
        lv_label_set_text(diversity_mode_label, "RACE");

        // All other labels removed to prevent overlay
        diversity_switch_label = NULL;
        diversity_delta_label = NULL;
    }

    // Create ExpressLRS backpack status indicator (bottom right corner, small)
    backpack_status_label = lv_label_create(main_contain);
    lv_obj_set_style_text_font(backpack_status_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(backpack_status_label, lv_color_make(255, 180, 0), LV_STATE_DEFAULT);  // Orange warning color
    lv_obj_set_style_bg_opa(backpack_status_label, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(backpack_status_label, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(backpack_status_label, 2, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(backpack_status_label, 1, LV_STATE_DEFAULT);
    lv_obj_set_pos(backpack_status_label, 120, 68);  // Bottom right corner
    lv_label_set_text(backpack_status_label, "");  // Start empty


    if(RX5808_Get_Signal_Source()==0)
    page_main_update_timer = lv_timer_create(page_main_update, 500, NULL);
    else {
    // Use configurable GUI update rate (0=100ms, 1=70ms, 2=50ms, 3=40ms, 4=20ms, 5=10ms)
    const uint16_t update_rates_ms[] = {100, 70, 50, 40, 20, 10};
    uint16_t rate_idx = RX5808_Get_GUI_Update_Rate();
    if (rate_idx > 5) rate_idx = 1; // Default to 70ms if invalid
    uint16_t update_period_ms = update_rates_ms[rate_idx];
    page_main_update_timer = lv_timer_create(page_main_update, update_period_ms, NULL);
    }


    lv_amin_start(lv_channel_label, -48, 2, 1, 800, 200, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_enter);
    lv_amin_start(lock_btn, -27, 23, 1, 800, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_enter);
    lv_amin_start(frequency_label[fre_cur][0], -50, 0, 1, 800, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_enter);
    lv_amin_start(frequency_label[fre_cur][1], -50, 0, 1, 800, 200, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_enter);
    lv_amin_start(frequency_label[fre_cur][2], -50, 0, 1, 800, 400, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_enter);
    lv_amin_start(frequency_label[fre_cur][3], -50, 0, 1, 800, 600, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_enter);

    lv_fun_delayed(page_main_group_create, 500);

}

static void page_main_group_create()
{
    lv_obj_add_event_cb(lock_btn, event_callback, LV_EVENT_ALL, NULL);
    lv_group_t* group = lv_group_create();
    lv_indev_set_group(indev_keypad, group);
    lv_group_add_obj(group, lock_btn);
    lv_group_set_editing(group, false);

}

static void page_main_exit()
{
    page_main_active = false;  // Mark page as inactive to prevent callbacks
    system_set_lvgl_idle(false);
    system_set_cpu_context_idle(false);

    // Clean up Band X frequency edit mode if active
    if (band_x_freq_edit_mode) {
        stop_band_x_freq_edit();
    }

    lv_obj_remove_event_cb(lock_btn, event_callback);
    lv_timer_del(page_main_update_timer);

    lv_amin_start(lv_channel_label, lv_obj_get_y(lv_channel_label), -48, 1, 500, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_leave);
    lv_amin_start(lock_btn, lv_obj_get_y(lock_btn), -27, 1, 800, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_leave);
    lv_amin_start(frequency_label[fre_cur][0], lv_obj_get_y(frequency_label[fre_cur][0]), -50, 1, 500, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_leave);
    lv_amin_start(frequency_label[fre_cur][1], lv_obj_get_y(frequency_label[fre_cur][1]), -50, 1, 500, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_leave);
    lv_amin_start(frequency_label[fre_cur][2], lv_obj_get_y(frequency_label[fre_cur][2]), -50, 1, 500, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_leave);
    lv_amin_start(frequency_label[fre_cur][3], lv_obj_get_y(frequency_label[fre_cur][3]), -50, 1, 500, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_leave);

    if (RX5808_Get_Signal_Source() == 0)
    {
     lv_amin_start(lv_rsss0_label, lv_obj_get_y(lv_rsss0_label), 80, 1, 500, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_leave);
     lv_amin_start(rssi_label0, lv_obj_get_y(rssi_label0), 80, 1, 500, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_leave);
     lv_amin_start(rssi_bar0, lv_obj_get_y(rssi_bar0), 80, 1, 500, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_leave);
     lv_amin_start(lv_rsss1_label, lv_obj_get_y(lv_rsss1_label), 95, 1, 500, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_leave);
     lv_amin_start(rssi_label1, lv_obj_get_y(rssi_label1), 95, 1, 500, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_leave);
     lv_amin_start(rssi_bar1, lv_obj_get_y(rssi_bar1), 95, 1, 500, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_leave);
     // Animate diversity telemetry out
     lv_amin_start(diversity_mode_label, lv_obj_get_y(diversity_mode_label), -30, 1, 500, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_leave);
    }
    else
    {
        lv_amin_start(lv_rsss0_label, lv_obj_get_y(lv_rsss0_label), 80,  1, 500, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_enter);
        lv_amin_start(rssi_label0, lv_obj_get_y(rssi_label0),80,  1, 500, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_enter);
        lv_amin_start(rssi_chart, lv_obj_get_y(rssi_chart),80,  1, 500, 0, (lv_anim_exec_xcb_t)lv_obj_set_y, page_main_anim_enter);
    }
    lv_obj_del_delayed(main_contain, 500);
}
