/**
 * @file lv_port_indev_templ.c
 *
 */

 /*Copy this file as "lv_port_indev.c" and set this value to "1" to enable content*/
#if 1

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_indev.h"
#include "../../lvgl.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

//static void touchpad_init(void);
//static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
//static bool touchpad_is_pressed(void);
//static void touchpad_get_xy(lv_coord_t * x, lv_coord_t * y);

//static void mouse_init(void);
//static void mouse_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
//static bool mouse_is_pressed(void);
//static void mouse_get_xy(lv_coord_t * x, lv_coord_t * y);

static void keypad_init(void);
static void keypad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
static uint32_t keypad_get_key(void);

//static void encoder_init(void);
//static void encoder_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
//static void encoder_handler(void);

//static void button_init(void);
//static void button_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
//static int8_t button_get_pressed_id(void);
//static bool button_is_pressed(uint8_t id);

/**********************
 *  STATIC VARIABLES
 **********************/
//lv_indev_t * indev_touchpad;
//lv_indev_t * indev_mouse;
lv_indev_t * indev_keypad;
//lv_indev_t * indev_encoder;
//lv_indev_t * indev_button;

//static int32_t encoder_diff;
//static lv_indev_state_t encoder_state;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_indev_init(void)
{
    /**
     * Here you will find example implementation of input devices supported by LittelvGL:
     *  - Touchpad
     *  - Mouse (with cursor support)
     *  - Keypad (supports GUI usage only with key)
     *  - Encoder (supports GUI usage only with: left, right, push)
     *  - Button (external buttons to press points on the screen)
     *
     *  The `..._read()` function are only examples.
     *  You should shape them according to your hardware
     */

    static lv_indev_drv_t indev_drv;

    /*------------------
     * Touchpad
     * -----------------*/

    /*Initialize your touchpad if you have*/
//    touchpad_init();

//    /*Register a touchpad input device*/
//    lv_indev_drv_init(&indev_drv);
//    indev_drv.type = LV_INDEV_TYPE_POINTER;
//    indev_drv.read_cb = touchpad_read;
//    indev_touchpad = lv_indev_drv_register(&indev_drv);

    /*------------------
     * Mouse
     * -----------------*/

    /*Initialize your touchpad if you have*/
//    mouse_init();

//    /*Register a mouse input device*/
//    lv_indev_drv_init(&indev_drv);
//    indev_drv.type = LV_INDEV_TYPE_POINTER;
//    indev_drv.read_cb = mouse_read;
//    indev_mouse = lv_indev_drv_register(&indev_drv);

//    /*Set cursor. For simplicity set a HOME symbol now.*/
//    lv_obj_t * mouse_cursor = lv_img_create(lv_scr_act());
//    lv_img_set_src(mouse_cursor, LV_SYMBOL_HOME);
//    lv_indev_set_cursor(indev_mouse, mouse_cursor);

    /*------------------
     * Keypad
     * -----------------*/

    /*Initialize your keypad or keyboard if you have*/
    keypad_init();

    /*Register a keypad input device*/
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv.read_cb = keypad_read;
    indev_keypad = lv_indev_drv_register(&indev_drv);

    /*Later you should create group(s) with `lv_group_t * group = lv_group_create()`,
     *add objects to the group with `lv_group_add_obj(group, obj)`
     *and assign this input device to group to navigate in it:
     *`lv_indev_set_group(indev_keypad, group);`*/

    /*------------------
     * Encoder
     * -----------------*/

    /*Initialize your encoder if you have*/
//    encoder_init();

//    /*Register a encoder input device*/
//    lv_indev_drv_init(&indev_drv);
//    indev_drv.type = LV_INDEV_TYPE_ENCODER;
//    indev_drv.read_cb = encoder_read;
//    indev_encoder = lv_indev_drv_register(&indev_drv);

    /*Later you should create group(s) with `lv_group_t * group = lv_group_create()`,
     *add objects to the group with `lv_group_add_obj(group, obj)`
     *and assign this input device to group to navigate in it:
     *`lv_indev_set_group(indev_encoder, group);`*/

    /*------------------
     * Button
     * -----------------*/

    /*Initialize your button if you have*/
//    button_init();

//    /*Register a button input device*/
//    lv_indev_drv_init(&indev_drv);
//    indev_drv.type = LV_INDEV_TYPE_BUTTON;
//    indev_drv.read_cb = button_read;
//    indev_button = lv_indev_drv_register(&indev_drv);

//    /*Assign buttons to points on the screen*/
//    static const lv_point_t btn_points[2] = {
//            {10, 10},   /*Button 0 -> x:10; y:10*/
//            {40, 100},  /*Button 1 -> x:40; y:100*/
//    };
//    lv_indev_set_button_points(indev_button, btn_points);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*------------------
 * Touchpad
 * -----------------*/

/*Initialize your touchpad*/
//static void touchpad_init(void)
//{
//    /*Your code comes here*/
//}

///*Will be called by the library to read the touchpad*/
//static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
//{
//    static lv_coord_t last_x = 0;
//    static lv_coord_t last_y = 0;

//    /*Save the pressed coordinates and the state*/
//    if(touchpad_is_pressed()) {
//        touchpad_get_xy(&last_x, &last_y);
//        data->state = LV_INDEV_STATE_PR;
//    } else {
//        data->state = LV_INDEV_STATE_REL;
//    }

//    /*Set the last pressed coordinates*/
//    data->point.x = last_x;
//    data->point.y = last_y;
//}

///*Return true is the touchpad is pressed*/
//static bool touchpad_is_pressed(void)
//{
//    /*Your code comes here*/

//    return false;
//}

///*Get the x and y coordinates if the touchpad is pressed*/
//static void touchpad_get_xy(lv_coord_t * x, lv_coord_t * y)
//{
//    /*Your code comes here*/

//    (*x) = 0;
//    (*y) = 0;
//}

/*------------------
 * Mouse
 * -----------------*/

/*Initialize your mouse*/
//static void mouse_init(void)
//{
//    /*Your code comes here*/
//}

///*Will be called by the library to read the mouse*/
//static void mouse_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
//{
//    /*Get the current x and y coordinates*/
//    mouse_get_xy(&data->point.x, &data->point.y);

//    /*Get whether the mouse button is pressed or released*/
//    if(mouse_is_pressed()) {
//        data->state = LV_INDEV_STATE_PR;
//    } else {
//        data->state = LV_INDEV_STATE_REL;
//    }
//}

///*Return true is the mouse button is pressed*/
//static bool mouse_is_pressed(void)
//{
//    /*Your code comes here*/

//    return false;
//}

///*Get the x and y coordinates if the mouse is pressed*/
//static void mouse_get_xy(lv_coord_t * x, lv_coord_t * y)
//{
//    /*Your code comes here*/

//    (*x) = 0;
//    (*y) = 0;
//}

/*------------------
 * Keypad
 * -----------------*/
// #define ADC_EXAMPLE_CALI_SCHEME     ESP_ADC_CAL_VAL_EFUSE_VREF

// KEY_ADC_CHAN is defined in hardware/hwvers.h (included via lv_port_indev.h)

// //ADC Attenuation
// #define ADC_EXAMPLE_ATTEN           ADC_ATTEN_DB_11

#include "driver/gpio.h"
#include "hardware/rx5808.h"   /* shared ADC1 oneshot handle */
#include "esp_log.h"

//static esp_adc_cal_characteristics_t adc1_chars;
/*Initialize your keypad*/
static void keypad_init(void)
{
//     gpio_set_direction(9, GPIO_MODE_INPUT);
//     gpio_pullup_en(9);
//    gpio_set_direction(0, GPIO_MODE_INPUT);
//     gpio_pullup_en(0);
    // esp_err_t ret;
    // ret = esp_adc_cal_check_efuse(ADC_EXAMPLE_CALI_SCHEME);
    // if (ret == ESP_OK) {
    //     esp_adc_cal_characterize(ADC_UNIT_1, ADC_EXAMPLE_ATTEN, ADC_WIDTH_BIT_DEFAULT, 0, &adc1_chars);
    // } else {
    //     printf("adc calib failed!\n");
    // }
    // adc_set_clk_div(1);
    // adc1_config_width(ADC_WIDTH_BIT_12);
    // adc1_config_channel_atten(KEY_ADC_CHAN, ADC_EXAMPLE_ATTEN);
    /*Your code comes here*/
}

/*Will be called by the library to read the mouse*/
static void keypad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    static uint32_t last_key = 0;

    /*Get the current x and y coordinates*/
    //mouse_get_xy(&data->point.x, &data->point.y);

    /*Get whether the a key is pressed and save the pressed key*/
    /* Release debounce: require 3 consecutive zero ADC reads before releasing.
     * This bridges ADC noise gaps on narrow-window keys (e.g. RIGHT at 2800-3000)
     * that would otherwise cause the LVGL keypad FSM to reset on every noisy tick. */
    static uint32_t zero_streak = 0;  // uint32_t prevents overflow (uint8_t wraps at 255 → phantom key)

    uint32_t act_key = keypad_get_key();
    if(act_key != 0) {
        zero_streak = 0;
        data->state = LV_INDEV_STATE_PR;
        last_key = act_key;
    } else {
        zero_streak++;
        if(zero_streak >= 3) {
            data->state = LV_INDEV_STATE_REL;
        } else {
            /* ADC glitch — keep last key pressed to maintain LVGL long-press state */
            data->state = (last_key != 0) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
        }
    }

    data->key = last_key;
}
/*Get the currently being pressed key.  0 if no key is pressed*/
static uint32_t keypad_get_key(void)
{
    int key_raw = RX5808_ADC_Read_Raw(KEY_ADC_CHAN);
    if(key_raw > 10 && key_raw < 4090) {  // suppress true idle (4095) and floating noise
        // DEBUG removed: printf("KEY_ADC_V: %d\n", key_raw);  // was flooding UART on every keypress
    }
    if(key_raw>3000&&key_raw<3500)
        return LV_KEY_UP;
    if(key_raw<2650&&key_raw>2400)   // DOWN center ~2457, dead zone 2650-2700
        return LV_KEY_DOWN;
    if(key_raw<500)
        return LV_KEY_LEFT;
    if(key_raw<3000&&key_raw>2700)   // RIGHT center ~2800, well clear of DOWN
        return LV_KEY_RIGHT;
    if(key_raw<2200&&key_raw>1750)
        return LV_KEY_ENTER;
    return 0;
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif