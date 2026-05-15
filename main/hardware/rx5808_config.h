#ifndef __rx5808_config_H
#define __rx5808_config_H


#include <stdint.h>

//STM32F4 СFlASH16K дʱϳʹFLASH
#define RX5808_CONFIGT_FLASH_EEPROM  0     //0 EEPROM  | 1 FLASH

#define  STATUS_ON   1
#define  STATUS_OFF  0


#define  START_ANIMATION_DEFAULT  STATUS_ON
#define  BEEP_DEFAULT             STATUS_OFF
#define  BACKLIGHT_DEFAULT        100
#define  LED_BRIGHTNESS_DEFAULT   100
#define  FAN_SPEED_DEFAULT        100
#define  CHANNEL_DEFAULT          0
#define  RSSI0_MIN_DEFAULT        0
#define  RSSI0_MAX_DEFAULT        4095
#define  RSSI1_MIN_DEFAULT        0
#define  RSSI1_MAX_DEFAULT        4095
#define  OSD_FORMAT_DEFAULT       0
#define  LANGUAGE_DEFAULT         0               //English
#define  SIGNAL_SOURCE_DEFAULT    0               //auto
// ELRS_BACKPACK removed - binding managed through ELRS_Backpack API in page_setup.c
#define  CPU_FREQ_DEFAULT         3               //AUTO (0=80MHz, 1=160MHz, 2=240MHz, 3=AUTO)
#define  GUI_UPDATE_RATE_DEFAULT  1               //70ms = 14Hz (0=100ms/10Hz, 1=70ms/14Hz, 2=50ms/20Hz, 3=40ms/25Hz, 4=20ms/50Hz, 5=10ms/100Hz)

#define  SETUP_ID_DEFAULT         0x1236


typedef enum
{
		rx5808_div_config_start_animation=0,
		rx5808_div_config_beep,
		rx5808_div_config_backlight,
		rx5808_div_config_led_brightness,
		rx5808_div_config_fan_speed,
		rx5808_div_config_channel,
		rx5808_div_config_rssi_adc_value_min0,
		rx5808_div_config_rssi_adc_value_max0,
		rx5808_div_config_rssi_adc_value_min1,
		rx5808_div_config_rssi_adc_value_max1,
        rx5808_div_config_osd_format,
		rx5808_div_config_language_set,
		rx5808_div_config_signal_source,
		// rx5808_div_config_elrs_backpack removed - binding managed through ELRS_Backpack API
		rx5808_div_config_cpu_freq,
		rx5808_div_config_gui_update_rate,
		rx5808_div_config_crsf_ch_video,
		rx5808_div_config_crsf_ch_band,
		rx5808_div_config_l_band_grid_type,
		rx5808_div_config_setup_id,
		rx5808_div_config_setup_count,
}rx5808_div_setup_enum;

#if RX5808_CONFIGT_FLASH_EEPROM==1
extern uint32_t  rx5808_div_setup[];
#else
extern uint16_t  rx5808_div_setup[];
#endif

void rx5808_div_setup_load(void);
void rx5808_div_setup_upload(uint8_t index);
void rx5808_div_setup_upload_start(uint8_t index);

uint16_t crsf_get_ch_video_config(void);
uint16_t crsf_get_ch_band_config(void);
uint16_t rx5808_get_l_band_grid_config(void);

#ifdef __cplusplus
 /*extern "C"*/
#endif




#endif
