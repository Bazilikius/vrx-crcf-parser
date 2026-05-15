#include "page_diversity_calib.h"
#include "page_menu.h"
#include "page_main.h"
#include "diversity.h"
#include "rx5808.h"
#include "lvgl_stl.h"
#include "beep.h"
#include "led.h"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

LV_FONT_DECLARE(lv_font_chinese_12);

//  Constants
// Single-channel sampling: DIVERSITY_CALIB_SAMPLES ticks × 50ms = 2.5 s per phase.
// The user tunes to the VTX channel before starting (instructed on the intro screen).
#define CALIB_SAMPLES      DIVERSITY_CALIB_SAMPLES  // 50 samples per phase
#define TIMER_INTERVAL_MS  50

// Warning thresholds (tuned for ESP32 ADC + RX5808 hardware reality)
#define FLOOR_HIGH_THRESHOLD    1000   // above this = noisy environment warning
#define RANGE_SMALL_THRESHOLD   300    // below this = range too small warning
#define ASYM_THRESHOLD_PCT      50     // >50% difference = asymmetric warning

//  State machine
typedef enum {
    STATE_INTRO,            // Screen 0: pre-calibration instructions
    STATE_FLOOR_SETUP,      // Screen 1: "Remove antennas, press OK"
    STATE_FLOOR_SAMPLING,   // Screen 2: timer collects samples, auto-advances
    STATE_PEAK_SETUP,       // Screen 3: "Power VTX 10cm, press OK"
    STATE_PEAK_SAMPLING,    // Screen 4: timer collects samples, auto-advances
    STATE_SUMMARY,          // Screen 5: results + Save / Discard
} calib_state_t;

//  Static UI objects
static lv_obj_t*   calib_contain;
static lv_obj_t*   title_label;
static lv_obj_t*   instruction_label;
static lv_obj_t*   rssi_a_label;
static lv_obj_t*   rssi_b_label;
static lv_obj_t*   warning_label;
static lv_obj_t*   progress_bar;
static lv_obj_t*   save_btn;
static lv_obj_t*   discard_btn;
static lv_timer_t* calib_timer;
static lv_group_t* calib_group;

//  Static state
static calib_state_t current_state = STATE_INTRO;
static int           sample_index  = 0;  // 0..CALIB_SAMPLES-1
static uint16_t      buf_a[CALIB_SAMPLES];
static uint16_t      buf_b[CALIB_SAMPLES];
static uint16_t      floor_a = 0, floor_b = 0;
static uint16_t      peak_a  = 0, peak_b  = 0;
static bool          initializing = false;

extern uint16_t adc_converted_value[3];

//  Forward declarations
static void update_ui(void);
static void page_diversity_calib_exit(void);
static void page_diversity_calib_event(lv_event_t* event);
static void calib_timer_cb(lv_timer_t* tmr);

//  UI update
static void update_ui(void)
{
    bool en = (RX5808_Get_Language() == 0);

    lv_obj_add_flag(save_btn,      LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(discard_btn,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(warning_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(rssi_a_label,  LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(rssi_b_label,  LV_OBJ_FLAG_HIDDEN);
    // Reset geometry to defaults in case a previous state overrode them
    lv_obj_set_size(instruction_label, 156, 42);
    lv_obj_set_pos(save_btn, 100, 57);

    switch (current_state) {

        case STATE_INTRO:
            lv_label_set_text(title_label,
                en ? "Before Calibrating" : "校准前准备");
            lv_label_set_text(instruction_label,
                en ? "1. Set VRX to your VTX channel\n2. Keep VTX OFF\nPress OK to start."
                   : "1. 将VRX调至VTX频道\n2. 关闭VTX\n准备好后按OK");
            lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
            lv_label_set_text(save_btn, en ? "OK" : "确定");
            lv_obj_clear_flag(save_btn, LV_OBJ_FLAG_HIDDEN);
            lv_group_focus_obj(save_btn);
            break;

        case STATE_FLOOR_SETUP:
            lv_label_set_text(title_label,
                en ? "Floor Calibration" : "底噪校准");
            lv_label_set_text(instruction_label,
                en ? "Remove BOTH antennas\n(top A & bottom B).\nPress OK when ready."
                   : "移除两个天线\n(顶部A和底部B)\n准备好后按OK");
            lv_obj_clear_flag(rssi_a_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(rssi_b_label, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(progress_bar, 0, LV_ANIM_ON);
            lv_label_set_text(save_btn, en ? "OK" : "确定");
            lv_obj_clear_flag(save_btn, LV_OBJ_FLAG_HIDDEN);
            lv_group_focus_obj(save_btn);
            break;

        case STATE_FLOOR_SAMPLING:
            lv_label_set_text(title_label,
                en ? "Sampling Floor..." : "采样底噪...");
            lv_label_set_text(instruction_label,
                en ? "Keep antennas removed.\nDo not touch device.\nHold to cancel."
                   : "保持天线移除\n请勿触碰设备\n长按取消");
            lv_obj_clear_flag(rssi_a_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(rssi_b_label, LV_OBJ_FLAG_HIDDEN);
            break;

        case STATE_PEAK_SETUP:
            lv_label_set_text(title_label,
                en ? "Peak Calibration" : "峰值校准");
            lv_label_set_text(instruction_label,
                en ? "Connect BOTH antennas.\nVTX ON, ~10cm away.\nPress OK when ready."
                   : "连接两个天线\nVTX开启同频道，10cm处\n准备好后按OK");
            lv_obj_clear_flag(rssi_a_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(rssi_b_label, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(progress_bar, 50, LV_ANIM_ON);
            lv_label_set_text(save_btn, en ? "OK" : "确定");
            lv_obj_clear_flag(save_btn, LV_OBJ_FLAG_HIDDEN);
            lv_group_focus_obj(save_btn);
            break;

        case STATE_PEAK_SAMPLING:
            lv_label_set_text(title_label,
                en ? "Sampling Peak..." : "采样峰值...");
            lv_label_set_text(instruction_label,
                en ? "Keep VTX in place.\nDo not touch device.\nHold to cancel."
                   : "保持VTX位置\n请勿触碰设备\n长按取消");
            lv_obj_clear_flag(rssi_a_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(rssi_b_label, LV_OBJ_FLAG_HIDDEN);
            break;

        case STATE_SUMMARY: {
            lv_label_set_text(title_label, en ? "Results" : "结果");

            int range_a = (int)peak_a - (int)floor_a;
            int range_b = (int)peak_b - (int)floor_b;

            lv_label_set_text_fmt(instruction_label,
                "A: %d->%d  R:%d\nB: %d->%d  R:%d",
                floor_a, peak_a, range_a,
                floor_b, peak_b, range_b);

            // Advisory warnings (non-blocking)
            const char* warn = NULL;
            if (floor_a > FLOOR_HIGH_THRESHOLD || floor_b > FLOOR_HIGH_THRESHOLD) {
                warn = en ? "Warn: High floor noise!" : "警告:底噪过高!";
            } else if (range_a < RANGE_SMALL_THRESHOLD || range_b < RANGE_SMALL_THRESHOLD) {
                warn = en ? "Warn: Range too small!" : "警告:范围太小!";
            } else if (range_a > 0 && range_b > 0 &&
                       abs(range_a - range_b) * 100 / range_a > ASYM_THRESHOLD_PCT) {
                warn = en ? "Warn: Asymmetric RX!" : "警告:接收器不对称!";
            }

            if (warn) {
                lv_label_set_text(warning_label, warn);
                lv_obj_set_style_text_color(warning_label, lv_color_make(255, 200, 0), 0);
                lv_obj_clear_flag(warning_label, LV_OBJ_FLAG_HIDDEN);
            }

            lv_bar_set_value(progress_bar, 100, LV_ANIM_ON);
            lv_label_set_text(save_btn,    en ? "Save"    : "保存");
            lv_label_set_text(discard_btn, en ? "Discard" : "放弃");
            lv_obj_clear_flag(save_btn,    LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(discard_btn, LV_OBJ_FLAG_HIDDEN);
            lv_group_focus_obj(save_btn);
            break;
        }
    }

    if (current_state == STATE_FLOOR_SAMPLING || current_state == STATE_PEAK_SAMPLING)
        led_set_pattern(LED_PATTERN_BREATHING);
    else
        led_set_pattern(lock_flag ? LED_PATTERN_SOLID : LED_PATTERN_HEARTBEAT);
}

//  Timer callback: incremental sampling + live RSSI refresh
static void calib_timer_cb(lv_timer_t* tmr)
{
    (void)tmr;

    if (initializing) {
        static int init_ticks = 0;
        if (++init_ticks >= 2) { initializing = false; init_ticks = 0; }
        return;
    }

    // Live RSSI refresh
    if (current_state != STATE_SUMMARY) {
        lv_label_set_text_fmt(rssi_a_label, "A:%4d", adc_converted_value[0]);
        lv_label_set_text_fmt(rssi_b_label, "B:%4d", adc_converted_value[1]);
    }

    // --- Floor sampling: 50 × 50ms on the user's current channel (VTX OFF) ---
    if (current_state == STATE_FLOOR_SAMPLING) {
        buf_a[sample_index] = adc_converted_value[0];
        buf_b[sample_index] = adc_converted_value[1];
        sample_index++;
        lv_bar_set_value(progress_bar, (sample_index * 50) / CALIB_SAMPLES, LV_ANIM_OFF);
        if (sample_index >= CALIB_SAMPLES) {
            diversity_calibrate_floor_finish(buf_a, CALIB_SAMPLES, &floor_a);
            diversity_calibrate_floor_finish(buf_b, CALIB_SAMPLES, &floor_b);
            diversity_get_state()->cal_a.floor_raw = floor_a;
            diversity_get_state()->cal_b.floor_raw = floor_b;
            sample_index  = 0;
            current_state = STATE_PEAK_SETUP;
            update_ui();
        }
        return;
    }

    // --- Peak sampling: 50 × 50ms on the same channel (VTX ON, ~10cm) ---
    if (current_state == STATE_PEAK_SAMPLING) {
        buf_a[sample_index] = adc_converted_value[0];
        buf_b[sample_index] = adc_converted_value[1];
        sample_index++;
        lv_bar_set_value(progress_bar, 50 + (sample_index * 50) / CALIB_SAMPLES, LV_ANIM_OFF);
        if (sample_index >= CALIB_SAMPLES) {
            diversity_calibrate_peak_finish(buf_a, CALIB_SAMPLES, &peak_a);
            diversity_calibrate_peak_finish(buf_b, CALIB_SAMPLES, &peak_b);
            diversity_get_state()->cal_a.peak_raw = peak_a;
            diversity_get_state()->cal_b.peak_raw = peak_b;
            sample_index  = 0;
            current_state = STATE_SUMMARY;
            update_ui();
        }
        return;
    }
}

//  Event handler
static void page_diversity_calib_event(lv_event_t* event)
{
    if (initializing) return;

    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t*       obj  = lv_event_get_target(event);

    if (code == LV_EVENT_LONG_PRESSED) {
        beep_turn_on();
        page_diversity_calib_exit();
        lv_fun_param_delayed(page_menu_create, 500, 0);
        return;
    }

    if (code != LV_EVENT_KEY) return;

    // Bug 2 fix: use lv_event_get_key() not lv_indev_get_key() — the indev
    // function returns a stale cached value and can produce phantom key events.
    lv_key_t key = lv_event_get_key(event);

    // LEFT/RIGHT navigate between Save and Discard on summary screen
    if (current_state == STATE_SUMMARY) {
        if (key == LV_KEY_LEFT || key == LV_KEY_UP) {
            lv_group_focus_obj(discard_btn);
            return;
        }
        if (key == LV_KEY_RIGHT || key == LV_KEY_DOWN) {
            lv_group_focus_obj(save_btn);
            return;
        }
    }

    if (key != LV_KEY_ENTER) return;

    beep_turn_on();

    if (current_state == STATE_SUMMARY) {
        if (obj == save_btn) {
            diversity_get_state()->cal_a.calibrated = true;
            diversity_get_state()->cal_b.calibrated = true;
            diversity_calibrate_save();
        }
        page_diversity_calib_exit();
        lv_fun_param_delayed(page_menu_create, 500, 0);
        return;
    }

    if (current_state == STATE_INTRO) {
        current_state = STATE_FLOOR_SETUP;
        update_ui();
        return;
    }

    if (current_state == STATE_FLOOR_SETUP) {
        sample_index  = 0;
        current_state = STATE_FLOOR_SAMPLING;
        update_ui();
        return;
    }

    if (current_state == STATE_PEAK_SETUP) {
        sample_index  = 0;
        current_state = STATE_PEAK_SAMPLING;
        update_ui();
        return;
    }
}

//  Exit / cleanup
static void page_diversity_calib_exit(void)
{
    if (calib_timer) { lv_timer_del(calib_timer); calib_timer = NULL; }
    if (calib_group) { lv_group_del(calib_group); calib_group = NULL; }
    lv_obj_del_delayed(calib_contain, 500);
}

//  Create
void page_diversity_calib_create(void)
{
    current_state = STATE_INTRO;
    sample_index  = 0;
    floor_a = floor_b = peak_a = peak_b = 0;
    initializing  = true;

    //  Container
    calib_contain = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(calib_contain);
    lv_obj_set_style_bg_color(calib_contain, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(calib_contain, LV_OPA_COVER, 0);
    lv_obj_set_size(calib_contain, 160, 80);
    lv_obj_set_pos(calib_contain, 0, 0);

    //  Title
    title_label = lv_label_create(calib_contain);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title_label, lv_color_make(255, 200, 0), 0);
    lv_obj_set_pos(title_label, 2, 1);
    lv_obj_set_size(title_label, 156, 13);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(title_label, "");

    //  Instruction
    instruction_label = lv_label_create(calib_contain);
    lv_obj_set_style_text_font(instruction_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(instruction_label, lv_color_make(255, 255, 255), 0);
    lv_obj_set_pos(instruction_label, 2, 14);
    lv_obj_set_size(instruction_label, 156, 42);  // 3 lines × ~14px = 42px
    lv_label_set_long_mode(instruction_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(instruction_label, "");

    //  Live RSSI — row at y=57: instruction ends at y=55 (h=42), progress bar at y=74
    rssi_a_label = lv_label_create(calib_contain);
    lv_obj_set_style_text_font(rssi_a_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(rssi_a_label, lv_color_make(0, 255, 255), 0);
    lv_obj_set_pos(rssi_a_label, 2, 57);
    lv_obj_set_size(rssi_a_label, 46, 13);
    lv_label_set_long_mode(rssi_a_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(rssi_a_label, "A:   0");
    lv_obj_add_flag(rssi_a_label, LV_OBJ_FLAG_HIDDEN);

    rssi_b_label = lv_label_create(calib_contain);
    lv_obj_set_style_text_font(rssi_b_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(rssi_b_label, lv_color_make(255, 100, 255), 0);
    lv_obj_set_pos(rssi_b_label, 50, 57);   // x=50..95, OK button at x=100 — no overlap
    lv_obj_set_size(rssi_b_label, 46, 13);
    lv_label_set_long_mode(rssi_b_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(rssi_b_label, "B:   0");
    lv_obj_add_flag(rssi_b_label, LV_OBJ_FLAG_HIDDEN);

    //  Warning (summary only) — sits in the lower part of instruction area
    warning_label = lv_label_create(calib_contain);
    lv_obj_set_style_text_font(warning_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(warning_label, lv_color_make(255, 200, 0), 0);
    lv_obj_set_pos(warning_label, 2, 44);
    lv_obj_set_size(warning_label, 156, 13);
    lv_label_set_long_mode(warning_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(warning_label, "");
    lv_obj_add_flag(warning_label, LV_OBJ_FLAG_HIDDEN);

    //  Progress bar
    progress_bar = lv_bar_create(calib_contain);
    lv_obj_remove_style(progress_bar, NULL, LV_PART_KNOB);
    lv_obj_set_size(progress_bar, 156, 5);
    lv_obj_set_pos(progress_bar, 2, 74);
    lv_obj_set_style_bg_color(progress_bar, lv_color_make(30, 30, 30), LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar, lv_color_make(0, 200, 255), LV_PART_INDICATOR);
    lv_obj_set_style_radius(progress_bar, 2, LV_PART_MAIN);
    lv_bar_set_range(progress_bar, 0, 100);
    lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);

    //  Save / OK button
    save_btn = lv_label_create(calib_contain);
    lv_obj_set_style_text_font(save_btn, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(save_btn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(save_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(save_btn, lv_color_make(0, 160, 0), 0);
    lv_obj_set_style_bg_color(save_btn, lv_color_make(0, 230, 0), LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(save_btn, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_text_color(save_btn, lv_color_make(255, 255, 255), LV_STATE_FOCUSED);
    lv_obj_set_style_radius(save_btn, 3, 0);
    lv_obj_set_pos(save_btn, 100, 57);  // x=100..155; rssi_b ends at x=95 — no overlap
    lv_obj_set_size(save_btn, 56, 14);
    lv_label_set_text(save_btn, "OK");

    //  Discard button
    discard_btn = lv_label_create(calib_contain);
    lv_obj_set_style_text_font(discard_btn, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(discard_btn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(discard_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(discard_btn, lv_color_make(100, 100, 100), 0);
    lv_obj_set_style_bg_color(discard_btn, lv_color_make(180, 180, 180), LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(discard_btn, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_text_color(discard_btn, lv_color_make(255, 255, 255), LV_STATE_FOCUSED);
    lv_obj_set_style_radius(discard_btn, 3, 0);
    lv_obj_set_pos(discard_btn, 4, 57);
    lv_obj_set_size(discard_btn, 56, 14);
    lv_label_set_text(discard_btn, "Discard");
    lv_obj_add_flag(discard_btn, LV_OBJ_FLAG_HIDDEN);

    //  Input group
    calib_group = lv_group_create();
    lv_group_set_wrap(calib_group, true);
    lv_indev_set_group(indev_keypad, calib_group);
    lv_group_add_obj(calib_group, discard_btn);
    lv_group_add_obj(calib_group, save_btn);
    lv_group_set_editing(calib_group, false);

    lv_obj_add_event_cb(save_btn,    page_diversity_calib_event, LV_EVENT_KEY,          NULL);
    lv_obj_add_event_cb(discard_btn, page_diversity_calib_event, LV_EVENT_KEY,          NULL);
    lv_obj_add_event_cb(save_btn,    page_diversity_calib_event, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(discard_btn, page_diversity_calib_event, LV_EVENT_LONG_PRESSED, NULL);

    //  Timer (50ms: drives sampling + live RSSI refresh)
    calib_timer = lv_timer_create(calib_timer_cb, TIMER_INTERVAL_MS, NULL);

    update_ui();
}
