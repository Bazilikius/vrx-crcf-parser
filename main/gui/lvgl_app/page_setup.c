#include "page_setup.h"
#include "page_menu.h"
#include "lcd.h"
#include "fan.h"
#include "beep.h"
#include "page_start.h"
#include "rx5808_config.h"
#include "rx5808.h"
#include "lvgl_stl.h"
#include "lv_port_disp.h"
#include "diversity.h"
#include "system.h"
#include "lv_anim_helpers.h"

#ifdef ELRS_BACKPACK_ENABLE
#include "elrs_backpack.h"
#endif

LV_FONT_DECLARE(lv_font_chinese_12);

#define page_setup_anim_enter  lv_anim_path_bounce
#define page_setup_anim_leave  lv_anim_path_bounce

#define LABEL_FOCUSE_COLOR    lv_color_make(255, 100, 0)
#define LABEL_DEFAULT_COLOR   lv_color_make(255, 255, 255)
#define BAR_COLOR             lv_color_make(255, 168, 0)
#define SWITCH_COLOR          lv_color_make(255, 0, 128)

static lv_obj_t* menu_setup_contain = NULL;
static lv_obj_t* back_light_label;
static lv_obj_t* led_strength_label;
static lv_obj_t* fan_speed_label;
static lv_obj_t* boot_animation_label;
static lv_obj_t* beep_label;
static lv_obj_t* osd_format_label;
static lv_obj_t* language_label;
static lv_obj_t* signal_source_label;
static lv_obj_t* diversity_mode_label;
static lv_obj_t* cpu_freq_label;
static lv_obj_t* gui_update_rate_label;
#ifdef ELRS_BACKPACK_ENABLE
static lv_obj_t* elrs_bind_button_label;  // "ELRS BP" title
static lv_obj_t* elrs_bind_button;        // Button with "BIND"/"UNBIND"/"CANCEL" label
static lv_obj_t* elrs_status_value;       // "Bound"/"Unbound"/"Binding..." status text
static lv_timer_t* elrs_status_timer = NULL;
static elrs_bind_state_t last_elrs_state = ELRS_STATE_UNBOUND;
static lv_obj_t* vtx_band_swap_label;     // "VTX Bands" title
static lv_obj_t* vtx_band_swap_switch;    // ON/OFF switch for Râ†”L band swapping
#endif
static lv_obj_t* restore_defaults_label;
static lv_obj_t* exit_label;
static lv_obj_t* back_light_bar;
static lv_obj_t* led_strength_bar;
static lv_obj_t* fan_speed_bar;
static lv_obj_t* boot_animation_switch;
static lv_obj_t* beep_switch;
static lv_obj_t* osd_format_setup_label;
static lv_obj_t* language_setup_label;
static lv_obj_t* signal_source_setup_label;
static lv_obj_t* diversity_mode_setup_label;
static lv_obj_t* cpu_freq_setup_label;
static lv_obj_t* gui_update_rate_setup_label;

const char language_label_text[][24] = { "English","??" };
const char osd_format_label_text[][5] = { "PAL","NTSC" };
const char signal_source_label_text[][6] = { "Auto","Recv1","Recv2","None" };
const char signal_source_label_chinese_text[][24] = { "??","???1","???2","??"};
const char diversity_mode_label_text[][6] = { "RACE","FREE","L-R" };
const char diversity_mode_label_chinese_text[][24] = { "??","??","??"};
const char cpu_freq_label_text[][8] = { "80MHz","160MHz","240MHz","AUTO" };
const char gui_update_rate_label_text[][7] = { "10Hz","14Hz","20Hz","25Hz","50Hz","100Hz" };
const char gui_update_rate_label_chinese_text[][24] = { "10??","14??","20??","25??","50??","100??" };

static uint32_t language_selid = 65532;
static uint32_t signal_source_selid = 65532;
static uint32_t osd_format_selid = 65532;
static uint32_t diversity_mode_selid = 65532;
static uint32_t cpu_freq_selid = 65532;
static uint32_t gui_update_rate_selid = 65532;

static lv_group_t* setup_group;

// ELRS: No longer a simple bool - binding managed through ELRS_Backpack API
static lv_style_t style_setup_item;
static lv_style_t style_label;

static uint8_t setup_back_light;
static uint8_t setup_led_strength;
static int8_t setup_fan_speed;
static bool  boot_animation_state;
static bool  beep_state;

static void page_setup_exit(void);
static void page_setup_style_deinit(void);
static void page_setup_set_language(uint16_t language);

#ifdef ELRS_BACKPACK_ENABLE
// ELRS Status Update Timer Callback (500ms interval)
static void elrs_status_update(lv_timer_t* timer) {
    elrs_bind_state_t state = ELRS_Backpack_Get_State();

    // Update UI based on state changes
    if (state != last_elrs_state) {
        switch (state) {
            case ELRS_STATE_UNBOUND:
                lv_label_set_text(elrs_bind_button, "BIND");
                lv_label_set_text(elrs_status_value, "Unbound");
                break;

            case ELRS_STATE_BOUND:
                lv_label_set_text(elrs_bind_button, "UNBIND");
                lv_label_set_text(elrs_status_value, "Bound");
                break;

            case ELRS_STATE_BINDING:
                lv_label_set_text(elrs_bind_button, "CANCEL");
                // Countdown handled below
                break;

            case ELRS_STATE_BIND_SUCCESS:
                lv_label_set_text(elrs_status_value, "Success!");
                break;

            case ELRS_STATE_BIND_TIMEOUT:
                lv_label_set_text(elrs_status_value, "Timeout");
                break;
        }
        last_elrs_state = state;
    }

    // Show countdown during binding
    if (state == ELRS_STATE_BINDING) {
        uint32_t remaining = ELRS_Backpack_Get_Binding_Timeout_Remaining();
        lv_label_set_text_fmt(elrs_status_value, "%lus", remaining);
    }
}
#endif


static void setup_event_callback(lv_event_t* event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t* obj = lv_event_get_target(event);
    if (code == LV_EVENT_KEY)
    {
        //beep_on_off(1);
        //lv_fun_param_delayed(beep_on_off, 100, 0);
        beep_turn_on();
        lv_key_t key_status = lv_indev_get_key(lv_indev_get_act());
        if (key_status == LV_KEY_ENTER) {
            if (obj == exit_label) {
                page_set_animation_en(lv_obj_has_state(boot_animation_switch, LV_STATE_CHECKED));
                beep_set_enable_disable(lv_obj_has_state(beep_switch, LV_STATE_CHECKED));
                // ELRS Backpack removed from save/load - binding persisted automatically in NVS
                //LCD_SET_BLK(setup_back_light);
                rx5808_div_setup_upload(rx5808_div_config_start_animation);
                rx5808_div_setup_upload(rx5808_div_config_beep);
                rx5808_div_setup_upload(rx5808_div_config_backlight);
                rx5808_div_setup_upload(rx5808_div_config_led_brightness);
                rx5808_div_setup_upload(rx5808_div_config_fan_speed);
                rx5808_div_setup_upload(rx5808_div_config_osd_format);
                rx5808_div_setup_upload(rx5808_div_config_language_set);
                rx5808_div_setup_upload(rx5808_div_config_signal_source);
                // ELRS backpack binding no longer saved to config - persisted in NVS automatically
                rx5808_div_setup_upload(rx5808_div_config_cpu_freq);
                rx5808_div_setup_upload(rx5808_div_config_gui_update_rate);
                // Apply CPU frequency immediately
                system_apply_cpu_freq(RX5808_Get_CPU_Freq());
                // Save diversity mode to NVS
                diversity_calibrate_save();
                page_setup_exit();
            }
            else if (obj == boot_animation_label)
            {
                if (lv_obj_has_state(boot_animation_switch, LV_STATE_CHECKED) == true)
                    lv_obj_clear_state(boot_animation_switch, LV_STATE_CHECKED);
                else
                    lv_obj_add_state(boot_animation_switch, LV_STATE_CHECKED);
            }
            else if (obj == beep_label)
            {
                if (lv_obj_has_state(beep_switch, LV_STATE_CHECKED) == true)
                    lv_obj_clear_state(beep_switch, LV_STATE_CHECKED);
                else
                    lv_obj_add_state(beep_switch, LV_STATE_CHECKED);
            }
            #ifdef ELRS_BACKPACK_ENABLE
            else if (obj == elrs_bind_button_label || obj == elrs_bind_button)
            {
                // Handle ELRS binding button press
                elrs_bind_state_t state = ELRS_Backpack_Get_State();
                if (state == ELRS_STATE_BINDING) {
                    // Cancel ongoing binding
                    ELRS_Backpack_Cancel_Binding();
                } else if (state == ELRS_STATE_BOUND || state == ELRS_STATE_BIND_SUCCESS) {
                    // Unbind from TX
                    ELRS_Backpack_Unbind();
                } else {
                    // Start binding process (30 second timeout)
                    ELRS_Backpack_Start_Binding(30000);
                }
            }
            else if (obj == vtx_band_swap_label)
            {
                // Toggle VTX band swap
                if (lv_obj_has_state(vtx_band_swap_switch, LV_STATE_CHECKED) == true) {
                    lv_obj_clear_state(vtx_band_swap_switch, LV_STATE_CHECKED);
                    ELRS_Backpack_Set_VTX_Band_Swap(false);
                } else {
                    lv_obj_add_state(vtx_band_swap_switch, LV_STATE_CHECKED);
                    ELRS_Backpack_Set_VTX_Band_Swap(true);
                }
            }
            #endif
            else if (obj == restore_defaults_label)
            {
                setup_led_strength = LED_BRIGHTNESS_DEFAULT;
                cpu_freq_selid = 65532 + CPU_FREQ_DEFAULT;
                gui_update_rate_selid = 65532 + GUI_UPDATE_RATE_DEFAULT;

                lv_bar_set_value(led_strength_bar, setup_led_strength, LV_ANIM_OFF);
                RX5808_Set_LED_Brightness(setup_led_strength);

                lv_label_set_text_fmt(cpu_freq_setup_label, (const char*)(&cpu_freq_label_text[cpu_freq_selid % 4]));
                RX5808_Set_CPU_Freq(cpu_freq_selid % 4);

                lv_label_set_text_fmt(gui_update_rate_setup_label, (const char*)(&gui_update_rate_label_text[gui_update_rate_selid % 6]));
                RX5808_Set_GUI_Update_Rate(gui_update_rate_selid % 6);
                page_setup_set_language(language_selid % 2);
            }
        }
        else if (key_status == LV_KEY_LEFT) {
            if (obj == back_light_label)
            {
                setup_back_light -= 5;
                if (setup_back_light < 10)
                    setup_back_light = 10;
                LCD_SET_BLK(setup_back_light);
                lv_bar_set_value(back_light_bar, setup_back_light, LV_ANIM_OFF);
                lv_event_stop_bubbling(event);  // Prevent group navigation
                return;
            }
            else if (obj == led_strength_label)
            {
                if (setup_led_strength >= 5)
                    setup_led_strength -= 5;
                else
                    setup_led_strength = 0;
                RX5808_Set_LED_Brightness(setup_led_strength);
                lv_bar_set_value(led_strength_bar, setup_led_strength, LV_ANIM_OFF);
                lv_event_stop_bubbling(event);  // Prevent group navigation
                return;
            }
            else if(obj == fan_speed_label)
            {
                setup_fan_speed-=5;
                if (setup_fan_speed < 0)
                    setup_fan_speed = 0;
                fan_set_speed(setup_fan_speed);
                lv_bar_set_value(fan_speed_bar, setup_fan_speed, LV_ANIM_OFF);
                lv_event_stop_bubbling(event);  // Prevent group navigation
                return;
            }
            else if (obj == boot_animation_label)
            {
                lv_obj_clear_state(boot_animation_switch, LV_STATE_CHECKED);
            }
            else if (obj == beep_label)
            {
                lv_obj_clear_state(beep_switch, LV_STATE_CHECKED);
            }
            #ifdef ELRS_BACKPACK_ENABLE
            else if (obj == vtx_band_swap_label)
            {
                lv_obj_clear_state(vtx_band_swap_switch, LV_STATE_CHECKED);
                ELRS_Backpack_Set_VTX_Band_Swap(false);
            }
            #endif
            else if(obj == osd_format_label)
            {
                --osd_format_selid;
                lv_label_set_text_fmt(osd_format_setup_label, (const char*)(&osd_format_label_text[osd_format_selid % 2]));
                RX5808_Set_OSD_Format(osd_format_selid % 2);
            }
            else if (obj == language_label)
            {
                --language_selid;
                lv_label_set_text_fmt(language_setup_label, (const char*)(&language_label_text[language_selid % 2]));
                RX5808_Set_Language(language_selid % 2);
                page_setup_set_language(language_selid % 2);
            }
            else if (obj == signal_source_label)
            {
                --signal_source_selid;
                lv_label_set_text_fmt(signal_source_setup_label, (const char*)(&signal_source_label_text[signal_source_selid % 4]));
                RX5808_Set_Signal_Source(signal_source_selid % 4);
                page_setup_set_language(language_selid % 2);

            }
            else if (obj == diversity_mode_label)
            {
                --diversity_mode_selid;
                uint8_t mode_idx = diversity_mode_selid % 3;
                lv_label_set_text_fmt(diversity_mode_setup_label, (const char*)(&diversity_mode_label_text[mode_idx]));
                diversity_set_mode((diversity_mode_t)mode_idx);
                page_setup_set_language(language_selid % 2);
            }
            else if (obj == cpu_freq_label)
            {
                --cpu_freq_selid;
                lv_label_set_text_fmt(cpu_freq_setup_label, (const char*)(&cpu_freq_label_text[cpu_freq_selid % 4]));
                RX5808_Set_CPU_Freq(cpu_freq_selid % 4);
            }
            else if (obj == gui_update_rate_label)
            {
                --gui_update_rate_selid;
                lv_label_set_text_fmt(gui_update_rate_setup_label, (const char*)(&gui_update_rate_label_text[gui_update_rate_selid % 6]));
                RX5808_Set_GUI_Update_Rate(gui_update_rate_selid % 6);
                page_setup_set_language(language_selid % 2);
            }
            else if (obj == exit_label)
            {

            }
        }
        else if (key_status == LV_KEY_RIGHT) {
            if (obj == back_light_label)
            {
                setup_back_light += 5;
                if (setup_back_light > 100)
                    setup_back_light = 100;
                LCD_SET_BLK(setup_back_light);
                lv_bar_set_value(back_light_bar, setup_back_light, LV_ANIM_OFF);
                lv_event_stop_bubbling(event);  // Prevent group navigation
                return;
            }
            else if (obj == led_strength_label)
            {
                setup_led_strength += 5;
                if (setup_led_strength > 100)
                    setup_led_strength = 100;
                RX5808_Set_LED_Brightness(setup_led_strength);
                lv_bar_set_value(led_strength_bar, setup_led_strength, LV_ANIM_OFF);
                lv_event_stop_bubbling(event);  // Prevent group navigation
                return;
            }
             else if(obj == fan_speed_label)
            {
                setup_fan_speed+=5;
                if (setup_fan_speed > 100)
                    setup_fan_speed = 100;
               fan_set_speed(setup_fan_speed);
                lv_bar_set_value(fan_speed_bar, setup_fan_speed, LV_ANIM_OFF);
                lv_event_stop_bubbling(event);  // Prevent group navigation
                return;
            }
            else if (obj == boot_animation_label)
            {
                lv_obj_add_state(boot_animation_switch, LV_STATE_CHECKED);
            }
            else if (obj == beep_label)
            {
                lv_obj_add_state(beep_switch, LV_STATE_CHECKED);
            }
            #ifdef ELRS_BACKPACK_ENABLE
            else if (obj == vtx_band_swap_label)
            {
                lv_obj_add_state(vtx_band_swap_switch, LV_STATE_CHECKED);
                ELRS_Backpack_Set_VTX_Band_Swap(true);
            }
            #endif
            else if(obj == osd_format_label)
            {
                ++osd_format_selid;
                lv_label_set_text_fmt(osd_format_setup_label, (const char*)(&osd_format_label_text[osd_format_selid % 2]));
                RX5808_Set_OSD_Format(osd_format_selid % 2);
            }
            else if (obj == language_label)
            {
                ++language_selid;
                lv_label_set_text_fmt(language_setup_label, (const char*)(&language_label_text[language_selid % 2]));
                RX5808_Set_Language(language_selid % 2);
                page_setup_set_language(language_selid % 2);
            }
            else if (obj == signal_source_label)
            {
                ++signal_source_selid;
                lv_label_set_text_fmt(signal_source_setup_label, (const char*)(&signal_source_label_text[signal_source_selid % 4]));
                RX5808_Set_Signal_Source(signal_source_selid % 4);
                page_setup_set_language(language_selid % 2);

            }
            else if (obj == diversity_mode_label)
            {
                ++diversity_mode_selid;
                uint8_t mode_idx = diversity_mode_selid % 3;
                lv_label_set_text_fmt(diversity_mode_setup_label, (const char*)(&diversity_mode_label_text[mode_idx]));
                diversity_set_mode((diversity_mode_t)mode_idx);
                page_setup_set_language(language_selid % 2);
            }
            else if (obj == cpu_freq_label)
            {
                ++cpu_freq_selid;
                lv_label_set_text_fmt(cpu_freq_setup_label, (const char*)(&cpu_freq_label_text[cpu_freq_selid % 4]));
                RX5808_Set_CPU_Freq(cpu_freq_selid % 4);
            }
            else if (obj == gui_update_rate_label)
            {
                ++gui_update_rate_selid;
                lv_label_set_text_fmt(gui_update_rate_setup_label, (const char*)(&gui_update_rate_label_text[gui_update_rate_selid % 6]));
                RX5808_Set_GUI_Update_Rate(gui_update_rate_selid % 6);
                page_setup_set_language(language_selid % 2);
            }
            else if (obj == exit_label)
            {

            }
        }
        else if (key_status == LV_KEY_UP) {
            lv_group_focus_prev(setup_group);
        }
        else if (key_status == LV_KEY_DOWN) {
            lv_group_focus_next(setup_group);
        }
    }
}

static void page_setup_style_init()
{
    lv_style_init(&style_label);
    lv_style_set_bg_color(&style_label, LABEL_DEFAULT_COLOR);
    lv_style_set_bg_opa(&style_label, LV_OPA_COVER);
    lv_style_set_text_color(&style_label, lv_color_black());
    lv_style_set_border_width(&style_label, 2);
    lv_style_set_text_font(&style_label, &lv_font_montserrat_12);
    lv_style_set_text_opa(&style_label, LV_OPA_COVER);
    lv_style_set_radius(&style_label, 4);
    lv_style_set_x(&style_label, 100);

    lv_style_init(&style_setup_item);
    lv_style_set_bg_color(&style_setup_item, lv_color_make(0, 0, 0));
    lv_style_set_bg_opa(&style_setup_item, (lv_opa_t)LV_OPA_COVER);
    lv_style_set_text_color(&style_setup_item, lv_color_make(255, 128, 255));
    lv_style_set_border_color(&style_setup_item, lv_color_make(255, 0, 100));
    lv_style_set_border_width(&style_setup_item, 2);
    lv_style_set_radius(&style_setup_item, 4);
    lv_style_set_border_opa(&style_setup_item, (lv_opa_t)LV_OPA_COVER);
    lv_style_set_text_align(&style_setup_item, LV_TEXT_ALIGN_CENTER);
    lv_style_set_text_font(&style_setup_item, &lv_font_chinese_12);


}


static void page_setup_style_deinit()
{
    lv_style_reset(&style_label);
    lv_style_reset(&style_setup_item);
}

static void group_obj_scroll(lv_group_t* g)
{
    lv_obj_t* icon = lv_group_get_focused(g);
    lv_coord_t y = lv_obj_get_y(icon);
    lv_obj_scroll_to_y(lv_obj_get_parent(icon), y, LV_ANIM_ON);
}

static void page_setup_set_language(uint16_t language)
{
    if (language == 0)
    {
        lv_obj_set_style_text_font(back_light_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(led_strength_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(fan_speed_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(boot_animation_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(beep_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        #ifdef ELRS_BACKPACK_ENABLE
        lv_obj_set_style_text_font(elrs_bind_button_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(vtx_band_swap_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        #endif
        lv_obj_set_style_text_font(osd_format_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(language_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(signal_source_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(diversity_mode_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(cpu_freq_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(gui_update_rate_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(restore_defaults_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(exit_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
        lv_label_set_text_fmt(back_light_label, "BckLight");
        lv_label_set_text_fmt(led_strength_label, "LED");
        lv_label_set_text_fmt(fan_speed_label, "Fan");
        lv_label_set_text_fmt(boot_animation_label, "Boot ANM");
        lv_label_set_text_fmt(beep_label, "Beep");
        #ifdef ELRS_BACKPACK_ENABLE
        lv_label_set_text_fmt(elrs_bind_button_label, "ELRS BP");
        lv_label_set_text_fmt(vtx_band_swap_label, "Swap R/L");
        #endif
        lv_label_set_text_fmt(osd_format_label, "OSD");
        lv_label_set_text_fmt(language_label, "Language");
        lv_label_set_text_fmt(signal_source_label, "Signal");
        lv_label_set_text_fmt(diversity_mode_label, "Div Mode");
        lv_label_set_text_fmt(cpu_freq_label, "CPU Freq");
        lv_label_set_text_fmt(gui_update_rate_label, "GUI Rate");
        lv_label_set_text_fmt(restore_defaults_label, "Defaults");
        lv_label_set_text_fmt(exit_label, "Save&Exit");
        lv_label_set_text_fmt(osd_format_setup_label, (const char*)(&osd_format_label_text[osd_format_selid % 2]));
        lv_label_set_text_fmt(language_setup_label, (const char*)(&language_label_text[language_selid % 2]));
        lv_label_set_text_fmt(signal_source_setup_label, (const char*)(&signal_source_label_text[signal_source_selid % 4]));
        lv_label_set_text_fmt(diversity_mode_setup_label, (const char*)(&diversity_mode_label_text[diversity_mode_selid % 3]));
        lv_label_set_text_fmt(cpu_freq_setup_label, (const char*)(&cpu_freq_label_text[cpu_freq_selid % 4]));
        lv_label_set_text_fmt(gui_update_rate_setup_label, (const char*)(&gui_update_rate_label_text[gui_update_rate_selid % 6]));
    }
    else
    {
        lv_obj_set_style_text_font(back_light_label, &lv_font_chinese_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(led_strength_label, &lv_font_chinese_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(fan_speed_label, &lv_font_chinese_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(boot_animation_label, &lv_font_chinese_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(beep_label, &lv_font_chinese_12, LV_STATE_DEFAULT);
        #ifdef ELRS_BACKPACK_ENABLE
        lv_obj_set_style_text_font(elrs_bind_button_label, &lv_font_chinese_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(vtx_band_swap_label, &lv_font_chinese_12, LV_STATE_DEFAULT);
        #endif
        lv_obj_set_style_text_font(osd_format_label, &lv_font_chinese_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(language_label, &lv_font_chinese_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(signal_source_label, &lv_font_chinese_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(diversity_mode_label, &lv_font_chinese_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(cpu_freq_label, &lv_font_chinese_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(gui_update_rate_label, &lv_font_chinese_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(restore_defaults_label, &lv_font_chinese_12, LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(exit_label, &lv_font_chinese_12, LV_STATE_DEFAULT);
        lv_label_set_text_fmt(back_light_label, "屏幕背光 ");
        lv_label_set_text_fmt(led_strength_label, "LED?? ");
        lv_label_set_text_fmt(fan_speed_label, "???? ");
        lv_label_set_text_fmt(boot_animation_label, "???? ");
        lv_label_set_text_fmt(beep_label, "??? ");
        #ifdef ELRS_BACKPACK_ENABLE
        lv_label_set_text_fmt(elrs_bind_button_label, "ELRS??");
        lv_label_set_text_fmt(vtx_band_swap_label, "Swap R/L");
        #endif
        lv_label_set_text_fmt(osd_format_label, "OSD制式");
        lv_label_set_text_fmt(language_label, "???? ");
        lv_label_set_text_fmt(signal_source_label, "输出信号源 ");
        lv_label_set_text_fmt(diversity_mode_label, "分集模式 ");
        lv_label_set_text_fmt(cpu_freq_label, "CPU??");
        lv_label_set_text_fmt(gui_update_rate_label, "????");
        lv_label_set_text_fmt(restore_defaults_label, "恢复默认值");
        lv_label_set_text_fmt(exit_label, "保存并退出 ");
        lv_label_set_text_fmt(osd_format_setup_label, (const char*)(&osd_format_label_text[osd_format_selid % 2]));
        lv_label_set_text_fmt(language_setup_label, (const char*)(&language_label_text[language_selid % 2]));
        lv_label_set_text_fmt(signal_source_setup_label, (const char*)(&signal_source_label_chinese_text[signal_source_selid % 4]));
        lv_label_set_text_fmt(diversity_mode_setup_label, (const char*)(&diversity_mode_label_chinese_text[diversity_mode_selid % 3]));
        lv_label_set_text_fmt(cpu_freq_setup_label, (const char*)(&cpu_freq_label_text[cpu_freq_selid % 4]));
        lv_label_set_text_fmt(gui_update_rate_setup_label, (const char*)(&gui_update_rate_label_chinese_text[gui_update_rate_selid % 6]));
    }
}



void page_setup_create()
{
    language_selid = 65532 + RX5808_Get_Language();
    signal_source_selid = 65532 + RX5808_Get_Signal_Source();
    osd_format_selid = 65532 + RX5808_Get_OSD_Format();
    cpu_freq_selid = 65532 + RX5808_Get_CPU_Freq();
    gui_update_rate_selid = 65532 + RX5808_Get_GUI_Update_Rate();
    page_setup_style_init();

    menu_setup_contain = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(menu_setup_contain);
    lv_obj_set_style_bg_color(menu_setup_contain, lv_color_make(0, 0, 0), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(menu_setup_contain, (lv_opa_t)LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_size(menu_setup_contain, 160, 80);
    lv_obj_set_pos(menu_setup_contain, 0, 0);
    lv_obj_set_scrollbar_mode(menu_setup_contain, LV_SCROLLBAR_MODE_OFF);  // Hide scrollbar but allow scrolling
    lv_obj_set_scroll_dir(menu_setup_contain, LV_DIR_VER);  // Vertical scrolling only


    back_light_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(back_light_label, &style_label, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(back_light_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    //lv_label_set_text_fmt(back_light_label, "BackLight");
    lv_obj_set_pos(back_light_label, 0, 2);
    lv_obj_set_size(back_light_label, 75, 20);
    lv_label_set_long_mode(back_light_label, LV_LABEL_LONG_WRAP);

    back_light_bar = lv_bar_create(menu_setup_contain);
    lv_obj_remove_style(back_light_bar, NULL, LV_PART_KNOB);
    lv_obj_set_size(back_light_bar, 50, 14);
    lv_obj_set_style_bg_color(back_light_bar, BAR_COLOR, LV_PART_INDICATOR);
    lv_obj_set_pos(back_light_bar, 110, 5);
    setup_back_light = LCD_GET_BLK();
    lv_bar_set_value(back_light_bar, setup_back_light, LV_ANIM_ON);
    lv_obj_set_style_anim_time(back_light_bar, 200, LV_STATE_DEFAULT);

    led_strength_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(led_strength_label, &style_label, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(led_strength_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    lv_obj_set_pos(led_strength_label, 0, 21);
    lv_obj_set_size(led_strength_label, 75, 20);
    lv_label_set_long_mode(led_strength_label, LV_LABEL_LONG_WRAP);

    led_strength_bar = lv_bar_create(menu_setup_contain);
    lv_obj_remove_style(led_strength_bar, NULL, LV_PART_KNOB);
    lv_obj_set_size(led_strength_bar, 50, 14);
    lv_obj_set_style_bg_color(led_strength_bar, BAR_COLOR, LV_PART_INDICATOR);
    lv_obj_set_pos(led_strength_bar, 110, 24);
    setup_led_strength = RX5808_Get_LED_Brightness();
    lv_bar_set_value(led_strength_bar, setup_led_strength, LV_ANIM_ON);
    lv_obj_set_style_anim_time(led_strength_bar, 200, LV_STATE_DEFAULT);

    fan_speed_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(fan_speed_label, &style_label, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(fan_speed_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    //lv_label_set_text_fmt(fan_speed_label, "BackLight");
    lv_obj_set_pos(fan_speed_label, 0, 40);
    lv_obj_set_size(fan_speed_label, 75, 20);
    lv_label_set_long_mode(fan_speed_label, LV_LABEL_LONG_WRAP);

    fan_speed_bar = lv_bar_create(menu_setup_contain);
    lv_obj_remove_style(fan_speed_bar, NULL, LV_PART_KNOB);
    lv_obj_set_size(fan_speed_bar, 50, 14);
    lv_obj_set_style_bg_color(fan_speed_bar, BAR_COLOR, LV_PART_INDICATOR);
    lv_obj_set_pos(fan_speed_bar, 110, 43);
    setup_fan_speed = fan_get_speed();
    lv_bar_set_value(fan_speed_bar, setup_fan_speed, LV_ANIM_ON);
    lv_obj_set_style_anim_time(fan_speed_bar, 200, LV_STATE_DEFAULT);

    boot_animation_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(boot_animation_label, &style_label, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(boot_animation_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    //lv_label_set_text_fmt(boot_animation_label, "Boot Logo");
    lv_obj_set_size(boot_animation_label, 75, 20);
    lv_obj_set_pos(boot_animation_label, 0, 59);
    lv_label_set_long_mode(boot_animation_label, LV_LABEL_LONG_WRAP);

    boot_animation_switch = lv_switch_create(menu_setup_contain);
    lv_obj_set_style_border_opa(boot_animation_switch, 0, LV_STATE_DEFAULT);
    lv_obj_set_size(boot_animation_switch, 50, 14);
    lv_obj_set_pos(boot_animation_switch, 110, 62);
    lv_obj_set_style_bg_color(boot_animation_switch, SWITCH_COLOR, LV_PART_INDICATOR | LV_STATE_CHECKED);
    boot_animation_state = page_get_animation_en();
    if (boot_animation_state == true)
        lv_obj_add_state(boot_animation_switch, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(boot_animation_switch, LV_STATE_CHECKED);

    beep_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(beep_label, &style_label, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(beep_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    //lv_label_set_text_fmt(beep_label, "Beep");
    lv_obj_set_pos(beep_label, 0, 78);
    lv_obj_set_size(beep_label, 75, 20);
    lv_label_set_long_mode(beep_label, LV_LABEL_LONG_WRAP);

    beep_switch = lv_switch_create(menu_setup_contain);
    lv_obj_set_style_border_opa(beep_switch, 0, LV_STATE_DEFAULT);
    lv_obj_set_size(beep_switch, 50, 14);
    lv_obj_set_pos(beep_switch, 110, 81);
    lv_obj_set_style_bg_color(beep_switch, SWITCH_COLOR, LV_PART_INDICATOR | LV_STATE_CHECKED);
    beep_state = beep_get_status();
    if (beep_state == true)
        lv_obj_add_state(beep_switch, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(beep_switch, LV_STATE_CHECKED);


    #ifdef ELRS_BACKPACK_ENABLE
    // ELRS Backpack Binding UI (replaces simple ON/OFF toggle)
    elrs_bind_button_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(elrs_bind_button_label, &style_label, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(elrs_bind_button_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    lv_obj_set_pos(elrs_bind_button_label, 0, 97);
    lv_obj_set_size(elrs_bind_button_label, 75, 20);
    lv_label_set_long_mode(elrs_bind_button_label, LV_LABEL_LONG_WRAP);

    // Binding button (shows BIND/UNBIND/CANCEL depending on state)
    elrs_bind_button = lv_label_create(menu_setup_contain);
    lv_obj_add_style(elrs_bind_button, &style_setup_item, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(elrs_bind_button, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    lv_obj_set_pos(elrs_bind_button, 75, 97);
    lv_obj_set_size(elrs_bind_button, 35, 18);
    lv_label_set_text(elrs_bind_button, "BIND");  // Initial label
    lv_label_set_long_mode(elrs_bind_button, LV_LABEL_LONG_CLIP);

    // Status value label (shows Bound/Unbound/countdown)
    elrs_status_value = lv_label_create(menu_setup_contain);
    lv_obj_add_style(elrs_status_value, &style_setup_item, LV_STATE_DEFAULT);
    lv_obj_set_pos(elrs_status_value, 110, 97);
    lv_obj_set_size(elrs_status_value, 50, 18);
    lv_label_set_text(elrs_status_value, "---");  // Initial status
    lv_label_set_long_mode(elrs_status_value, LV_LABEL_LONG_CLIP);

    // Initialize based on current binding state
    elrs_bind_state_t initial_state = ELRS_Backpack_Get_State();
    last_elrs_state = initial_state;
    if (initial_state == ELRS_STATE_BOUND) {
        lv_label_set_text(elrs_bind_button, "UNBIND");
        lv_label_set_text(elrs_status_value, "Bound");
    } else {
        lv_label_set_text(elrs_bind_button, "BIND");
        lv_label_set_text(elrs_status_value, "Unbound");
    }

    // VTX Band Swap Toggle (for non-standard VTX with R/L swapped)
    vtx_band_swap_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(vtx_band_swap_label, &style_label, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(vtx_band_swap_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    lv_label_set_text(vtx_band_swap_label, "Swap R/L");
    lv_obj_set_pos(vtx_band_swap_label, 0, 116);
    lv_obj_set_size(vtx_band_swap_label, 75, 20);
    lv_label_set_long_mode(vtx_band_swap_label, LV_LABEL_LONG_WRAP);

    vtx_band_swap_switch = lv_switch_create(menu_setup_contain);
    lv_obj_set_size(vtx_band_swap_switch, 50, 14);
    lv_obj_set_pos(vtx_band_swap_switch, 110, 119);
    lv_obj_set_style_bg_color(vtx_band_swap_switch, SWITCH_COLOR, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (ELRS_Backpack_Get_VTX_Band_Swap()) {
        lv_obj_add_state(vtx_band_swap_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(vtx_band_swap_switch, LV_STATE_CHECKED);
    }
    #endif

    osd_format_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(osd_format_label, &style_label, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(osd_format_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    lv_obj_set_pos(osd_format_label, 0, 135);
    lv_obj_set_size(osd_format_label, 75, 20);
    lv_label_set_long_mode(osd_format_label, LV_LABEL_LONG_WRAP);

    osd_format_setup_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(osd_format_setup_label, &style_setup_item, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(osd_format_setup_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    lv_obj_set_pos(osd_format_setup_label, 110, 135);
    lv_obj_set_size(osd_format_setup_label, 50, 18);
    lv_label_set_long_mode(osd_format_setup_label, LV_LABEL_LONG_WRAP);

    language_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(language_label, &style_label, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(language_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    //lv_label_set_text_fmt(language_label, "Language");
    lv_obj_set_pos(language_label, 0, 154);
    lv_obj_set_size(language_label, 75, 20);
    lv_label_set_long_mode(language_label, LV_LABEL_LONG_WRAP);


    language_setup_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(language_setup_label, &style_setup_item, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(language_setup_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    //lv_label_set_text_fmt(language_setup_label, (const char*)(&language_label_text[language_selid % 2]));
    lv_obj_set_pos(language_setup_label, 110, 154);
    lv_obj_set_size(language_setup_label, 50, 18);
    lv_label_set_long_mode(language_setup_label, LV_LABEL_LONG_WRAP);

    signal_source_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(signal_source_label, &style_label, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(signal_source_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    //lv_label_set_text_fmt(signal_source_label, "Signal");
    lv_obj_set_pos(signal_source_label, 0, 173);
    lv_obj_set_size(signal_source_label, 75, 20);
    lv_label_set_long_mode(signal_source_label, LV_LABEL_LONG_WRAP);

    signal_source_setup_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(signal_source_setup_label, &style_setup_item, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(signal_source_setup_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    //lv_label_set_text_fmt(signal_source_setup_label, (const char*)(&signal_source_label_text[signal_source_selid % 4]));
    lv_obj_set_pos(signal_source_setup_label, 110, 173);
    lv_obj_set_size(signal_source_setup_label, 50, 18);
    lv_label_set_long_mode(signal_source_setup_label, LV_LABEL_LONG_WRAP);

    // Diversity mode selector
    diversity_mode_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(diversity_mode_label, &style_label, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(diversity_mode_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    lv_obj_set_pos(diversity_mode_label, 0, 192);
    lv_obj_set_size(diversity_mode_label, 75, 20);
    lv_label_set_long_mode(diversity_mode_label, LV_LABEL_LONG_WRAP);

    diversity_mode_setup_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(diversity_mode_setup_label, &style_setup_item, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(diversity_mode_setup_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    lv_obj_set_pos(diversity_mode_setup_label, 110, 192);
    lv_obj_set_size(diversity_mode_setup_label, 50, 18);
    lv_label_set_long_mode(diversity_mode_setup_label, LV_LABEL_LONG_WRAP);
    // Initialize to current diversity mode
    diversity_state_t* div_state = diversity_get_state();
    diversity_mode_selid = div_state->mode;
    lv_label_set_text_fmt(diversity_mode_setup_label, (const char*)(&diversity_mode_label_text[diversity_mode_selid % 3]));

    // CPU Frequency selector
    cpu_freq_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(cpu_freq_label, &style_label, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cpu_freq_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    lv_obj_set_pos(cpu_freq_label, 0, 211);
    lv_obj_set_size(cpu_freq_label, 75, 20);
    lv_label_set_long_mode(cpu_freq_label, LV_LABEL_LONG_WRAP);

    cpu_freq_setup_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(cpu_freq_setup_label, &style_setup_item, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cpu_freq_setup_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    lv_obj_set_pos(cpu_freq_setup_label, 110, 211);
    lv_obj_set_size(cpu_freq_setup_label, 50, 18);
    lv_label_set_long_mode(cpu_freq_setup_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(cpu_freq_setup_label, (const char*)(&cpu_freq_label_text[cpu_freq_selid % 4]));

    // GUI/Diversity Update Rate selector
    gui_update_rate_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(gui_update_rate_label, &style_label, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(gui_update_rate_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    lv_obj_set_pos(gui_update_rate_label, 0, 230);
    lv_obj_set_size(gui_update_rate_label, 75, 20);
    lv_label_set_long_mode(gui_update_rate_label, LV_LABEL_LONG_WRAP);

    gui_update_rate_setup_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(gui_update_rate_setup_label, &style_setup_item, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(gui_update_rate_setup_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    lv_obj_set_pos(gui_update_rate_setup_label, 110, 230);
    lv_obj_set_size(gui_update_rate_setup_label, 50, 18);
    lv_label_set_long_mode(gui_update_rate_setup_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(gui_update_rate_setup_label, (const char*)(&gui_update_rate_label_text[gui_update_rate_selid % 6]));
    restore_defaults_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(restore_defaults_label, &style_label, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(restore_defaults_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    lv_obj_set_pos(restore_defaults_label, 0, 249);
    lv_obj_set_size(restore_defaults_label, 75, 20);
    lv_label_set_long_mode(restore_defaults_label, LV_LABEL_LONG_WRAP);
        exit_label = lv_label_create(menu_setup_contain);
    lv_obj_add_style(exit_label, &style_label, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(exit_label, LABEL_FOCUSE_COLOR, LV_STATE_FOCUSED);
    //lv_label_set_text_fmt(exit_label, "SAVE&EXIT");
    lv_obj_set_pos(exit_label, 0, 268);
    lv_obj_set_size(exit_label, 75, 20);
    lv_label_set_long_mode(exit_label, LV_LABEL_LONG_WRAP);

    page_setup_set_language(RX5808_Get_Language());

    setup_group = lv_group_create();
    lv_indev_set_group(indev_keypad, setup_group);
    lv_obj_add_event_cb(boot_animation_label, setup_event_callback, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(back_light_label, setup_event_callback, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(led_strength_label, setup_event_callback, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(fan_speed_label, setup_event_callback, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(beep_label, setup_event_callback, LV_EVENT_KEY, NULL);
    #ifdef ELRS_BACKPACK_ENABLE
    lv_obj_add_event_cb(elrs_bind_button_label, setup_event_callback, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(elrs_bind_button, setup_event_callback, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(vtx_band_swap_label, setup_event_callback, LV_EVENT_KEY, NULL);
    #endif
    lv_obj_add_event_cb(osd_format_label, setup_event_callback, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(language_label, setup_event_callback, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(signal_source_label, setup_event_callback, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(diversity_mode_label, setup_event_callback, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(cpu_freq_label, setup_event_callback, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(gui_update_rate_label, setup_event_callback, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(restore_defaults_label, setup_event_callback, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(exit_label, setup_event_callback, LV_EVENT_KEY, NULL);

    lv_group_add_obj(setup_group, back_light_label);
    lv_group_add_obj(setup_group, led_strength_label);
    lv_group_add_obj(setup_group, fan_speed_label);
    lv_group_add_obj(setup_group, boot_animation_label);
    lv_group_add_obj(setup_group, beep_label);
    #ifdef ELRS_BACKPACK_ENABLE
    lv_group_add_obj(setup_group, elrs_bind_button_label);
    lv_group_add_obj(setup_group, vtx_band_swap_label);
    #endif
    lv_group_add_obj(setup_group, osd_format_label);
    lv_group_add_obj(setup_group, language_label);
    lv_group_add_obj(setup_group, signal_source_label);
    lv_group_add_obj(setup_group, diversity_mode_label);
    lv_group_add_obj(setup_group, cpu_freq_label);
    lv_group_add_obj(setup_group, gui_update_rate_label);
    lv_group_add_obj(setup_group, restore_defaults_label);
    lv_group_add_obj(setup_group, exit_label);
    lv_group_set_editing(setup_group, true);

    lv_group_set_focus_cb(setup_group, group_obj_scroll);

    lv_group_set_wrap(setup_group, true);



    lv_amin_start(back_light_label, -100, 0, 1, 150, 0, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(led_strength_label, -100, 0, 1, 150, 50, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(fan_speed_label, -100, 0, 1, 150, 100, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(boot_animation_label, -100, 0, 1, 150, 150, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(beep_label, -100, 0, 1, 150, 200, anim_set_x_cb, page_setup_anim_enter);
    #ifdef ELRS_BACKPACK_ENABLE
    lv_amin_start(elrs_bind_button_label, -100, 0, 1, 150, 200, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(vtx_band_swap_label, -100, 0, 1, 150, 250, anim_set_x_cb, page_setup_anim_enter);
    #endif
    lv_amin_start(osd_format_label, -100, 0, 1, 150, 300, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(language_label, -100, 0, 1, 150, 350, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(signal_source_label, -100, 0, 1, 150, 400, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(diversity_mode_label, -100, 0, 1, 150, 450, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(cpu_freq_label, -100, 0, 1, 150, 500, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(gui_update_rate_label, -100, 0, 1, 150, 550, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(restore_defaults_label, -100, 0, 1, 150, 600, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(exit_label, -100, 0, 1, 150, 650, anim_set_x_cb, page_setup_anim_enter);

    lv_amin_start(back_light_bar, 160, 110, 1, 150, 0, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(led_strength_bar, 160, 110, 1, 150, 50, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(fan_speed_bar, 160, 110, 1, 150, 100, anim_set_x_cb, page_setup_anim_enter);
    #ifdef ELRS_BACKPACK_ENABLE
    lv_amin_start(elrs_bind_button, 160, 75, 1, 150, 200, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(elrs_status_value, 160, 110, 1, 150, 200, anim_set_x_cb, page_setup_anim_enter);
    #endif
    lv_amin_start(beep_switch, 160, 110, 1, 150, 200, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(osd_format_setup_label, 160, 110, 1, 150, 300, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(language_setup_label, 160, 110, 1, 150, 350, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(signal_source_setup_label, 160, 110, 1, 150, 400, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(diversity_mode_setup_label, 160, 110, 1, 150, 450, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(cpu_freq_setup_label, 160, 110, 1, 150, 500, anim_set_x_cb, page_setup_anim_enter);
    #ifdef ELRS_BACKPACK_ENABLE
    // Create timer for ELRS status updates (500ms interval)
    elrs_status_timer = lv_timer_create(elrs_status_update, 500, NULL);
    last_elrs_state = ELRS_Backpack_Get_State();
    #endif
}


static void page_setup_exit()
{
    lv_amin_start(back_light_label, lv_obj_get_x(back_light_label), -100, 1, 200, 0, anim_set_x_cb, page_setup_anim_leave);
    lv_amin_start(led_strength_label, lv_obj_get_x(led_strength_label), -100, 1, 200, 50, anim_set_x_cb, page_setup_anim_leave);
    lv_amin_start(fan_speed_label, lv_obj_get_x(fan_speed_label), -100, 1, 200, 100, anim_set_x_cb, page_setup_anim_leave);
    lv_amin_start(boot_animation_label, lv_obj_get_x(boot_animation_label), -100, 1, 200, 150, anim_set_x_cb, page_setup_anim_leave);
    lv_amin_start(beep_label, lv_obj_get_x(beep_label), -100, 1, 200, 200, anim_set_x_cb, page_setup_anim_leave);
    #ifdef ELRS_BACKPACK_ENABLE
    lv_amin_start(elrs_bind_button_label, lv_obj_get_x(elrs_bind_button_label), -100, 1, 200, 200, anim_set_x_cb, page_setup_anim_leave);
    lv_amin_start(vtx_band_swap_label, lv_obj_get_x(vtx_band_swap_label), -100, 1, 200, 250, anim_set_x_cb, page_setup_anim_leave);
    #endif
    lv_amin_start(osd_format_label, lv_obj_get_x(osd_format_label), -100, 1, 200, 300, anim_set_x_cb, page_setup_anim_leave);
    lv_amin_start(language_label, lv_obj_get_x(language_label), -100, 1, 200, 350, anim_set_x_cb, page_setup_anim_leave);
    lv_amin_start(signal_source_label, lv_obj_get_x(signal_source_label), -100, 1, 200, 400, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(diversity_mode_label, lv_obj_get_x(diversity_mode_label), -100, 1, 200, 450, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(cpu_freq_label, lv_obj_get_x(cpu_freq_label), -100, 1, 200, 500, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(restore_defaults_label, lv_obj_get_x(restore_defaults_label), -100, 1, 200, 550, anim_set_x_cb, page_setup_anim_leave);
    lv_amin_start(exit_label, lv_obj_get_x(exit_label), -100, 1, 200, 600, anim_set_x_cb, page_setup_anim_enter);

    lv_amin_start(back_light_bar, lv_obj_get_x(back_light_bar), 160, 1, 200, 0, anim_set_x_cb, page_setup_anim_leave);
    lv_amin_start(led_strength_bar, lv_obj_get_x(led_strength_bar), 160, 1, 200, 50, anim_set_x_cb, page_setup_anim_leave);
    lv_amin_start(fan_speed_bar, lv_obj_get_x(fan_speed_bar), 160, 1, 200, 100, anim_set_x_cb, page_setup_anim_leave);
    lv_amin_start(boot_animation_switch, lv_obj_get_x(boot_animation_switch), 160, 1, 200, 150, anim_set_x_cb, page_setup_anim_leave);
    lv_amin_start(beep_switch, lv_obj_get_x(beep_switch), 160, 1, 200, 200, anim_set_x_cb, page_setup_anim_leave);
    #ifdef ELRS_BACKPACK_ENABLE
    lv_amin_start(elrs_bind_button, lv_obj_get_x(elrs_bind_button), 160, 1, 200, 200, anim_set_x_cb, page_setup_anim_leave);
    lv_amin_start(elrs_status_value, lv_obj_get_x(elrs_status_value), 160, 1, 200, 200, anim_set_x_cb, page_setup_anim_leave);
    #endif
    lv_amin_start(osd_format_setup_label, lv_obj_get_x(osd_format_setup_label), 160, 1, 200, 300, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(language_setup_label, lv_obj_get_x(language_setup_label), 160, 1, 200, 350, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(signal_source_setup_label, lv_obj_get_x(signal_source_setup_label), 160, 1, 200, 400, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(diversity_mode_setup_label, lv_obj_get_x(diversity_mode_setup_label), 160, 1, 200, 450, anim_set_x_cb, page_setup_anim_enter);
    lv_amin_start(cpu_freq_setup_label, lv_obj_get_x(cpu_freq_setup_label), 160, 1, 200, 450, anim_set_x_cb, page_setup_anim_enter);

    #ifdef ELRS_BACKPACK_ENABLE
    // Stop and delete ELRS status timer on exit
    if (elrs_status_timer != NULL) {
        lv_timer_del(elrs_status_timer);
        elrs_status_timer = NULL;
    }
    #endif

    lv_fun_delayed(page_setup_style_deinit, 500);
    lv_group_del(setup_group);
    lv_obj_del_delayed(menu_setup_contain, 500);
    lv_fun_param_delayed(page_menu_create, 500, item_setup);
}
