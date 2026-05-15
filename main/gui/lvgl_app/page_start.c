#include "page_start.h"
#include "page_main.h"
#include "lvgl_stl.h"
#include "hwvers.h"
#include "lv_anim_helpers.h"
#include "hardware/rx5808.h"   /* RX5808_ADC_Read_Raw — key skip poll */

//ä¿¡å·è´¨é‡èƒŒå…‰äº®åº¦å¼€æœºåŠ¨ç”»èœ‚é¸£å™¨æ‰“å…³é—­è¯­è¨€ä¸­æ–‡è¾“å‡ºæºä¿å­˜å¹¶é€€å‡ºä¾›ç”µåŽ‹ç•Œé¢ç‰ˆæœ¬æºåè®®å›ºä»¶æ‰«æä¸­å›¾ä¼ å§‹æ ¡å‡†æˆåŠŸå¤±è´¥å¯è®¾ç½®æœ€å¤§åŠŸçŽ‡è¿‡å°è¯·é‡è¯•å±å¹•ç³»ç»Ÿè‡ªæŽ¥æ”¶é¢‘è°±é“ç»“æžœå·²å®‰è£…å¤©çº¿é£Žæ‰‡è½¬é€Ÿåˆ¶å¼ï¼Œ
//æŽ¥æ”¶æœºè®¾ç½®å…³äºŽåˆ†é›†æ‰«æä¸­ç»“æŸæ¨¡æ‹Ÿ 0x21,0x2d,0x2e

LV_FONT_DECLARE(lv_font_start);

lv_obj_t* page_start_contain;
volatile uint8_t start_animation = true;

// Timer handle for the key-press skip poll; NULL when not active.
static lv_timer_t* skip_timer = NULL;
// Guard: prevents page_start_exit() from running twice when both the
// lv_fun_delayed animation AND the skip-timer fire close together.
static bool exit_done = false;

static void page_start_exit(void);

// ---------------------------------------------------------------------------
// Skip-on-keypress poll — runs at 100 ms while the boot screen is visible.
// Any readable key ADC value (i.e. not idle / floating) skips the animation.
// ---------------------------------------------------------------------------
static void start_skip_check_cb(lv_timer_t* timer)
{
    (void)timer;
    int raw = RX5808_ADC_Read_Raw(KEY_ADC_CHAN);
    // ADC idle = ~4095 (pull-up, no key); floating noise < 10.
    // Any key press pulls it into 0–3500 range.
    if (raw > 10 && raw < 3600) {
        page_start_exit();  // guard inside prevents double-exit
    }
}

void page_start_create()
{
    exit_done = false;  // reset guard for this boot cycle

    if (start_animation == true)
    {
        page_start_contain = lv_obj_create(lv_scr_act());
        lv_obj_remove_style_all(page_start_contain);
        lv_obj_set_style_bg_color(page_start_contain, lv_color_make(0, 0, 0), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(page_start_contain, (lv_opa_t)LV_OPA_COVER, LV_STATE_DEFAULT);
        lv_obj_set_size(page_start_contain, 160, 80);
        lv_obj_set_pos(page_start_contain, 0, 0);


        lv_obj_t* start_info_label = lv_label_create(page_start_contain);
        lv_label_set_long_mode(start_info_label, LV_LABEL_LONG_WRAP);

        lv_obj_align(start_info_label, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_text_font(start_info_label, &lv_font_start, LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(start_info_label, lv_color_make(255, 255, 255), LV_STATE_DEFAULT);
        lv_label_set_recolor(start_info_label, true);
        lv_label_set_text(start_info_label, "RX5808");

        lv_obj_t* start_info_bar = lv_bar_create(page_start_contain);
        lv_obj_remove_style(start_info_bar, NULL, LV_PART_KNOB);
        lv_obj_set_size(start_info_bar, 100, 5);
        lv_obj_set_style_bg_color(start_info_bar, lv_color_make(255, 140, 0), LV_PART_INDICATOR);
        lv_obj_align(start_info_bar, LV_ALIGN_TOP_MID, 0, 53);
        lv_bar_set_value(start_info_bar, 0, LV_ANIM_OFF);

        // Add version label below the loading bar
        lv_obj_t* version_label = lv_label_create(page_start_contain);
        lv_obj_align(version_label, LV_ALIGN_TOP_MID, 0, 63);
        lv_obj_set_style_text_color(version_label, lv_color_make(128, 128, 128), LV_STATE_DEFAULT);
        lv_label_set_text_fmt(version_label, "v%d.%d.%d",
            RX5808_VERSION_MAJOR, RX5808_VERSION_MINOR, RX5808_VERSION_PATCH);

        lv_amin_start(start_info_label, -50, 20, 1, 800, 0, anim_set_y_cb, lv_anim_path_linear);
        lv_amin_start(start_info_bar, 0, 100, 1, 500, 800, anim_opa_cb, lv_anim_path_linear);
        lv_amin_start(start_info_bar, 0, 100, 1, 1500, 1000, anim_bar_value_cb, lv_anim_path_linear);

        // Auto-exit after 3 s (matches end of bar-fill animation at ~2.5 s).
        lv_fun_delayed(page_start_exit, 3000);

        // Allow any key press to skip the boot screen immediately.
        // The timer is deleted inside page_start_exit() so it never fires
        // after the screen has already been torn down.
        skip_timer = lv_timer_create(start_skip_check_cb, 100, NULL);
    }
    else
        page_main_create();

}

static void page_start_exit()
{
    // Guard: lv_fun_delayed and the skip-timer can both fire; only the
    // first call actually tears down the screen.
    if (exit_done) return;
    exit_done = true;

    // Stop the key-poll timer — no longer needed once we've exited.
    if (skip_timer != NULL) {
        lv_timer_del(skip_timer);
        skip_timer = NULL;
    }

    lv_obj_del(page_start_contain);
    page_main_create();
}


void page_set_animation_en(uint8_t en)
{
    if (en)
        start_animation = true;
    else
        start_animation = false;
}

uint16_t page_get_animation_en()
{
    return start_animation;
}
