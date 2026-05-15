#include "rx5808_config.h"
#include "lcd.h"
#include "beep.h"
#include "fan.h"
#include "rx5808.h"
#include "page_start.h"
#include "24cxx.h"
#include "hwvers.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// Define these here as they are needed for the function pointer array
uint16_t crsf_get_ch_video_config(void) { return rx5808_div_setup[rx5808_div_config_crsf_ch_video]; }
uint16_t crsf_get_ch_band_config(void) { return rx5808_div_setup[rx5808_div_config_crsf_ch_band]; }
uint16_t rx5808_get_l_band_grid_config(void) { return rx5808_div_setup[rx5808_div_config_l_band_grid_type]; }


#if RX5808_CONFIGT_FLASH_EEPROM==1
uint32_t  rx5808_div_setup[rx5808_div_config_setup_count];
#else
uint16_t  rx5808_div_setup[rx5808_div_config_setup_count];
#endif

static QueueHandle_t setup_upload_queue;

void rx5808_setup_upload(void *param)
{
	while(1)
	{
		uint8_t index=0;
		xQueueReceive(setup_upload_queue,&index,portMAX_DELAY);
		rx5808_div_setup_upload_start(index);
		vTaskDelay(10/portTICK_PERIOD_MS);
	}
}

void rx5808_div_setup_load()
{
	#if RX5808_CONFIGT_FLASH_EEPROM==1
	STMF4_FLASH_Read_Word(rx5808_div_setup,rx5808_div_config_setup_count);
	#else
	eeprom_24cxx_read_half_word_len(0,rx5808_div_setup,rx5808_div_config_setup_count);
	#endif
	if(rx5808_div_setup[rx5808_div_config_setup_id]!=SETUP_ID_DEFAULT)
	{
		rx5808_div_setup[rx5808_div_config_start_animation]=START_ANIMATION_DEFAULT;
		rx5808_div_setup[rx5808_div_config_beep]=BEEP_DEFAULT;
		rx5808_div_setup[rx5808_div_config_backlight]=BACKLIGHT_DEFAULT;
		rx5808_div_setup[rx5808_div_config_led_brightness]=LED_BRIGHTNESS_DEFAULT;
		rx5808_div_setup[rx5808_div_config_fan_speed]=FAN_SPEED_DEFAULT;
		rx5808_div_setup[rx5808_div_config_channel]=CHANNEL_DEFAULT;
		rx5808_div_setup[rx5808_div_config_rssi_adc_value_min0]=RSSI0_MIN_DEFAULT;
		rx5808_div_setup[rx5808_div_config_rssi_adc_value_max0]=RSSI0_MAX_DEFAULT;
		rx5808_div_setup[rx5808_div_config_rssi_adc_value_min1]=RSSI1_MIN_DEFAULT;
		rx5808_div_setup[rx5808_div_config_rssi_adc_value_max1]=RSSI1_MAX_DEFAULT;
		rx5808_div_setup[rx5808_div_config_osd_format]=OSD_FORMAT_DEFAULT;
		rx5808_div_setup[rx5808_div_config_language_set]=LANGUAGE_DEFAULT;
		rx5808_div_setup[rx5808_div_config_signal_source]=SIGNAL_SOURCE_DEFAULT;
		rx5808_div_setup[rx5808_div_config_cpu_freq]=CPU_FREQ_DEFAULT;
		rx5808_div_setup[rx5808_div_config_gui_update_rate]=GUI_UPDATE_RATE_DEFAULT;
		rx5808_div_setup[rx5808_div_config_crsf_ch_video]=12;
		rx5808_div_setup[rx5808_div_config_crsf_ch_band]=11;
		rx5808_div_setup[rx5808_div_config_l_band_grid_type]=0;
		rx5808_div_setup[rx5808_div_config_setup_id]=SETUP_ID_DEFAULT;
		#if RX5808_CONFIGT_FLASH_EEPROM==1
		STMF4_FLASH_Write_Word(rx5808_div_setup,rx5808_div_config_setup_count);
	  #else
	  eeprom_24cxx_write_half_word_len(0,rx5808_div_setup,rx5808_div_config_setup_count);
	  #endif
	}

		page_set_animation_en(rx5808_div_setup[rx5808_div_config_start_animation]);
		beep_set_enable_disable(rx5808_div_setup[rx5808_div_config_beep]);
		LCD_SET_BLK(rx5808_div_setup[rx5808_div_config_backlight]);
		fan_set_speed(rx5808_div_setup[rx5808_div_config_fan_speed]);
		Rx5808_Set_Channel(rx5808_div_setup[rx5808_div_config_channel]);
		RX5808_Set_RSSI_Ad_Min0(rx5808_div_setup[rx5808_div_config_rssi_adc_value_min0]);
		RX5808_Set_RSSI_Ad_Max0(rx5808_div_setup[rx5808_div_config_rssi_adc_value_max0]);
		RX5808_Set_RSSI_Ad_Min1(rx5808_div_setup[rx5808_div_config_rssi_adc_value_min1]);
		RX5808_Set_RSSI_Ad_Max1(rx5808_div_setup[rx5808_div_config_rssi_adc_value_max1]);
		RX5808_Set_OSD_Format(rx5808_div_setup[rx5808_div_config_osd_format]);
		RX5808_Set_Language(rx5808_div_setup[rx5808_div_config_language_set]);
		if (rx5808_div_setup[rx5808_div_config_signal_source] > 3)
			rx5808_div_setup[rx5808_div_config_signal_source] = SIGNAL_SOURCE_DEFAULT;
		RX5808_Set_Signal_Source(rx5808_div_setup[rx5808_div_config_signal_source]);
		RX5808_Set_LED_Brightness(rx5808_div_setup[rx5808_div_config_led_brightness]);
		RX5808_Set_CPU_Freq(rx5808_div_setup[rx5808_div_config_cpu_freq]);
		RX5808_Set_GUI_Update_Rate(rx5808_div_setup[rx5808_div_config_gui_update_rate]);

		setup_upload_queue=xQueueCreate(rx5808_div_config_setup_count-1,sizeof(char));
		if( setup_upload_queue == NULL ) {
            return;
        }
		xTaskCreatePinnedToCore( (TaskFunction_t)rx5808_setup_upload,
	                          "upload_task",
							  1024,
							  NULL,
							   2,
							    NULL,
								1 );
}

void rx5808_div_setup_upload(uint8_t index)
{
   xQueueSend(setup_upload_queue,&index,pdMS_TO_TICKS(50));
}

uint16_t (*set_fun_arr[rx5808_div_config_setup_count-1])()={
	page_get_animation_en,\
	beep_get_status,\
	LCD_GET_BLK,\
	fan_get_speed,\
	Rx5808_Get_Channel,\
	RX5808_Get_RSSI_Ad_Min0,\
	RX5808_Get_RSSI_Ad_Max0,\
	RX5808_Get_RSSI_Ad_Min1,\
	RX5808_Get_RSSI_Ad_Max1,\
	RX5808_Get_OSD_Format,\
	RX5808_Get_Language,\
	RX5808_Get_Signal_Source,\
	RX5808_Get_LED_Brightness,\
	RX5808_Get_CPU_Freq,\
	RX5808_Get_GUI_Update_Rate,\
	crsf_get_ch_video_config,\
	crsf_get_ch_band_config,\
	rx5808_get_l_band_grid_config
};

void rx5808_div_setup_upload_start(uint8_t index)
{
    if(index>=rx5808_div_config_setup_count-1)
	return;
		rx5808_div_setup[index]=set_fun_arr[index]();

	#if RX5808_CONFIGT_FLASH_EEPROM==1
		STMF4_FLASH_Write_Word(rx5808_div_setup,rx5808_div_config_setup_count);
	#else
	  eeprom_24cxx_write_half_word_len(0+2*index,rx5808_div_setup+index,1);
	#endif

}
