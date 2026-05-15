#ifndef __RX5808_H
#define __RX5808_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/hwvers.h"


typedef enum
{
     rx5808_receiver0=0,
	 rx5808_receiver1,
	 rx5808_receiver_count,
}rx5808_receive;

extern const char Rx5808_ChxMap[7];
extern const uint16_t Rx5808_Freq[7][8];
extern volatile int8_t channel_count;
extern volatile int8_t Chx_count;
extern uint16_t adc_converted_value[3];
void RX5808_RSSI_ADC_Init(void);
void RX5808_Init(void);
void RX5808_Pause(void);
void RX5808_Resume(void);
void Soft_SPI_Send_One_Bit(uint8_t bit);
void Send_Register_Data(uint8_t addr, uint32_t data);
void RX5808_Set_Freq(uint16_t Fre);
void Rx5808_Set_Channel(uint8_t ch);
void RX5808_Set_RSSI_Ad_Min0(uint16_t value);
void RX5808_Set_RSSI_Ad_Max0(uint16_t value);
void RX5808_Set_RSSI_Ad_Min1(uint16_t value);
void RX5808_Set_RSSI_Ad_Max1(uint16_t value);
void RX5808_Set_OSD_Format(uint16_t value);
void RX5808_Set_Language(uint16_t value);
void RX5808_Set_Signal_Source(uint16_t value);
// ELRS Backpack functions removed - use ELRS_Backpack API directly
void RX5808_Set_LED_Brightness(uint16_t value);
void RX5808_Set_CPU_Freq(uint16_t value);
void RX5808_Set_GUI_Update_Rate(uint16_t value);
uint16_t Rx5808_Get_Channel(void);
uint16_t RX5808_Get_RSSI_Ad_Min0(void);
uint16_t RX5808_Get_RSSI_Ad_Max0(void);
uint16_t RX5808_Get_RSSI_Ad_Min1(void);
uint16_t RX5808_Get_RSSI_Ad_Max1(void);
uint16_t RX5808_Get_OSD_Format(void);
uint16_t RX5808_Get_Language(void);
uint16_t RX5808_Get_Signal_Source(void);
// ELRS Backpack functions removed - use ELRS_Backpack API directly
uint16_t RX5808_Get_LED_Brightness(void);
uint16_t RX5808_Get_CPU_Freq(void);
uint16_t RX5808_Get_GUI_Update_Rate(void);
uint16_t rx5808_get_l_band_grid_config(void);
bool RX5808_Calib_RSSI(uint16_t min0,uint16_t max0,uint16_t min1,uint16_t max1);
float Rx5808_Calculate_RSSI_Precentage(uint16_t value, uint16_t min, uint16_t max);
float Rx5808_Get_Precentage0(void);
float Rx5808_Get_Precentage1(void);
float Get_Battery_Voltage(void);
void DMA2_Stream0_IRQHandler(void);

// ExpressLRS Backpack Detection
void RX5808_Check_Backpack_Activity(void);
void RX5808_Set_Backpack_Detected(bool detected);
bool RX5808_Is_Backpack_Detected(void);
uint16_t RX5808_Get_Expected_Frequency(void);
void RX5808_Clear_Backpack_Detection(void);

// Band X (User Favorites) Management
void RX5808_Init_Band_X(void);
void RX5808_Set_Band_X_Freq(uint8_t channel, uint16_t freq);
uint16_t RX5808_Get_Band_X_Freq(uint8_t channel);
void RX5808_Save_Band_X_To_NVS(void);
void RX5808_Load_Band_X_From_NVS(void);
bool RX5808_Is_Band_X(void);
uint16_t RX5808_Get_Current_Freq(void);

// ADC shared access (oneshot unit, also used by lvgl keypad driver)
int RX5808_ADC_Read_Raw(int channel);

#endif
