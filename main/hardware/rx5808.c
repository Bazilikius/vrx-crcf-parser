#include "rx5808.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "sys/unistd.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "hwvers.h"
#include "led.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "rx5808_config.h"

static const char *TAG = "RX5808";

extern uint32_t rx5808_div_setup[];

static SemaphoreHandle_t spi_mutex = NULL;
static SemaphoreHandle_t adc_mutex = NULL;

static spi_device_handle_t rx5808_spi = NULL;
static bool rx5808_spi_initialized = false;


#define Synthesizer_Register_A 				              0x00
#define Synthesizer_Register_B 				              0x01
#define Synthesizer_Register_C 				              0x02
#define Synthesizer_Register_D 				              0x03
#define VCO_Switch_Cap_Control_Register 		        0x04
#define DFC_Control_Register 				                0x05
#define _6M_Audio_Demodulator_Control_Register 			0x06
#define _6M5_Audio_Demodulator_Control_Register 	  0x07
#define Receiver_control_Register_1 				        0x08
#define Receiver_control_Register_2 				        0x09
#define Power_Down_Control_Register                 0x0A
#define State_Register                              0x0F

#define RSSI_FILTER_SIZE 4
#define RX5808_FREQ_SETTLING_TIME_MS 50

#define BACKPACK_DETECTION_ENABLED 1
#define BACKPACK_CHECK_INTERVAL_MS 500

bool RX5808_Shutdown = false;
uint16_t adc_converted_value[3]={1024,1024,1024};

static uint16_t rssi0_filter_buffer[RSSI_FILTER_SIZE] = {0};
static uint16_t rssi1_filter_buffer[RSSI_FILTER_SIZE] = {0};
static uint8_t rssi0_filter_index = 0;
static uint8_t rssi1_filter_index = 0;

static uint16_t Band_X_Custom_Freq[8] = {5740,5760,5780,5800,5820,5840,5860,5880};
static bool Band_X_Loaded = false;

#define NVS_NAMESPACE_BANDX "band_x"
#define NVS_KEY_BANDX_FREQS "x_freqs"
#define NVS_KEY_BANDX_CH1 "x_ch1"
#define NVS_KEY_BANDX_CH2 "x_ch2"
#define NVS_KEY_BANDX_CH3 "x_ch3"
#define NVS_KEY_BANDX_CH4 "x_ch4"
#define NVS_KEY_BANDX_CH5 "x_ch5"
#define NVS_KEY_BANDX_CH6 "x_ch6"
#define NVS_KEY_BANDX_CH7 "x_ch7"
#define NVS_KEY_BANDX_CH8 "x_ch8"

static bool backpack_detected = false;
static uint16_t expected_frequency = 5800;
static uint32_t last_freq_set_time_ms = 0;
static uint32_t backpack_detected_time_ms = 0;

volatile int8_t channel_count = 0;
volatile int8_t Chx_count = 0;
volatile uint8_t Rx5808_channel;
volatile uint16_t Rx5808_RSSI_Ad_Min0=0;
volatile uint16_t Rx5808_RSSI_Ad_Max0=4095;
volatile uint16_t Rx5808_RSSI_Ad_Min1=0;
volatile uint16_t Rx5808_RSSI_Ad_Max1=4095;
volatile uint16_t Rx5808_OSD_Format=0;
volatile uint16_t Rx5808_Language=1;
volatile uint16_t Rx5808_Signal_Source=0;
volatile uint16_t Rx5808_LED_Brightness=100;
volatile uint16_t Rx5808_CPU_Freq=3;
volatile uint16_t Rx5808_GUI_Update_Rate=1;

const char Rx5808_ChxMap[7] = {'A', 'B', 'E', 'F', 'R', 'L', 'X'};
const uint16_t Rx5808_Freq[7][8]=
{
	{5865,5845,5825,5805,5785,5765,5745,5725},	    //A
    {5733,5752,5771,5790,5809,5828,5847,5866},		//B
    {5705,5685,5665,5645,5885,5905,5925,5945},		//E
    {5740,5760,5780,5800,5820,5840,5860,5880},		//F
    {5658,5695,5732,5769,5806,5843,5880,5917},		//R
    {5362,5399,5436,5473,5510,5547,5584,5621},		//L (Standard)
    {5658,5695,5732,5769,5806,5843,5880,5917}		//X
};

const uint16_t Rx5808_L_Band_Grid2[8] = {5333, 5373, 5413, 5453, 5493, 5533, 5573, 5613};

static adc_oneshot_unit_handle_t adc1_handle = NULL;

void RX5808_RSSI_ADC_Init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc1_handle));

    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, RX5808_RSSI0_CHAN, &ch_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, RX5808_RSSI1_CHAN, &ch_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, VBAT_ADC_CHAN,     &ch_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, KEY_ADC_CHAN,      &ch_cfg));
}

int RX5808_ADC_Read_Raw(int channel)
{
    int raw = 0;
    if (adc_mutex != NULL) xSemaphoreTake(adc_mutex, portMAX_DELAY);
    adc_oneshot_read(adc1_handle, (adc_channel_t)channel, &raw);
    if (adc_mutex != NULL) xSemaphoreGive(adc_mutex);
    return raw;
}

static void RX5808_Init_Hardware_SPI(void) {
    esp_err_t ret;
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = RX5808_MOSI,
        .sclk_io_num = RX5808_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4,
    };
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1*1000*1000,
        .mode = 0,
        .spics_io_num = RX5808_CS,
        .queue_size = 4,
        .pre_cb = NULL,
        .post_cb = NULL,
        .flags = SPI_DEVICE_BIT_LSBFIRST,
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
    };
    ret = spi_bus_initialize(VSPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize VSPI bus: %s", esp_err_to_name(ret));
        return;
    }
    ret = spi_bus_add_device(VSPI_HOST, &devcfg, &rx5808_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add RX5808 to SPI bus: %s", esp_err_to_name(ret));
        return;
    }
    rx5808_spi_initialized = true;
}

void RX5808_Init()
{
    spi_mutex = xSemaphoreCreateMutex();
    adc_mutex = xSemaphoreCreateMutex();
    RX5808_Init_Hardware_SPI();
	gpio_set_direction(RX5808_SWITCH0, GPIO_MODE_OUTPUT);
	gpio_reset_pin(RX5808_SWITCH1);
	gpio_set_direction(RX5808_SWITCH1, GPIO_MODE_OUTPUT);
	gpio_set_level(RX5808_SWITCH0, 1);
	gpio_set_level(RX5808_SWITCH1, 0);
	Send_Register_Data(Synthesizer_Register_A,0x00008);
	Send_Register_Data(Power_Down_Control_Register,0x10DF3);
	RX5808_Init_Band_X();
	RX5808_Set_Freq(RX5808_Get_Current_Freq());
	RX5808_RSSI_ADC_Init();
	xTaskCreatePinnedToCore( (TaskFunction_t)DMA2_Stream0_IRQHandler, "rx5808_rssi", 1536, NULL, 5, NULL, 1 );
}

void RX5808_Pause() {
	RX5808_Shutdown = true;
	gpio_set_level(RX5808_SWITCH0, 0);
	gpio_set_level(RX5808_SWITCH1, 0);
}
void RX5808_Resume() {
	RX5808_Shutdown = false;
	gpio_set_level(RX5808_SWITCH0, 1);
	gpio_set_level(RX5808_SWITCH1, 0);
}
void Soft_SPI_Send_One_Bit(uint8_t bit)
{
	gpio_set_level(RX5808_SCLK, 0);
	usleep(20);
    gpio_set_level(RX5808_MOSI, ((bit&0x01)==1));
	usleep(30);
	gpio_set_level(RX5808_SCLK, 1);
	usleep(20);
}

void Send_Register_Data(uint8_t addr,uint32_t data)
{
    if (spi_mutex != NULL) xSemaphoreTake(spi_mutex, portMAX_DELAY);
    if (rx5808_spi_initialized && rx5808_spi != NULL) {
      uint32_t spi_data = 0;
      spi_data |= (addr & 0x0F);
      spi_data |= (1 << 4);
      spi_data |= (data & 0xFFFFF) << 5;
      spi_transaction_t trans = { .length = 25, .tx_buffer = &spi_data };
      spi_device_queue_trans(rx5808_spi, &trans, portMAX_DELAY);
      spi_transaction_t *rtrans;
      spi_device_get_trans_result(rx5808_spi, &rtrans, portMAX_DELAY);
    } else {
      gpio_set_level(RX5808_CS, 0);
	  for(uint8_t i=0;i<4;i++) Soft_SPI_Send_One_Bit(((addr>>i)&0x01));
      Soft_SPI_Send_One_Bit(1);
	  for(uint8_t i=0;i<20;i++) Soft_SPI_Send_One_Bit(((data>>i)&0x01));
	  gpio_set_level(RX5808_CS, 1);
	  gpio_set_level(RX5808_SCLK, 0);
	  gpio_set_level(RX5808_MOSI, 0);
    }
    if (spi_mutex != NULL) xSemaphoreGive(spi_mutex);
}

void RX5808_Set_Freq(uint16_t Fre)
{
#if BACKPACK_DETECTION_ENABLED
	if (backpack_detected) return;
#endif
	uint16_t F_LO=(Fre-479)>>1;
	uint16_t N=F_LO/32; uint16_t A=F_LO%32;
	Send_Register_Data(Synthesizer_Register_B,N<<7|A);
	expected_frequency = Fre;
	last_freq_set_time_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
	vTaskDelay(RX5808_FREQ_SETTLING_TIME_MS / portTICK_PERIOD_MS);
}

void Rx5808_Set_Channel(uint8_t ch)
{
	if(ch>47) return ;
    Rx5808_channel=ch;
	Chx_count=Rx5808_channel/8;
	channel_count=Rx5808_channel%8;
}

void RX5808_Set_RSSI_Ad_Min0(uint16_t value) { Rx5808_RSSI_Ad_Min0=value; }
void RX5808_Set_RSSI_Ad_Max0(uint16_t value) { Rx5808_RSSI_Ad_Max0=value; }
void RX5808_Set_RSSI_Ad_Min1(uint16_t value) { Rx5808_RSSI_Ad_Min1=value; }
void RX5808_Set_RSSI_Ad_Max1(uint16_t value) { Rx5808_RSSI_Ad_Max1=value; }
void RX5808_Set_OSD_Format(uint16_t value) { Rx5808_OSD_Format = value; }
void RX5808_Set_Language(uint16_t value) { Rx5808_Language = value; }
void RX5808_Set_Signal_Source(uint16_t value) { Rx5808_Signal_Source = value; }
void RX5808_Set_LED_Brightness(uint16_t value) { if (value > 100) value = 100; Rx5808_LED_Brightness = value; led_set_brightness((uint8_t)value); }
void RX5808_Set_CPU_Freq(uint16_t value) { if (value > 3) value = 3; Rx5808_CPU_Freq = value; }
void RX5808_Set_GUI_Update_Rate(uint16_t value) { if (value > 5) value = 1; Rx5808_GUI_Update_Rate = value; }

uint16_t Rx5808_Get_Channel() { return Rx5808_channel; }
uint16_t RX5808_Get_RSSI_Ad_Min0() { return Rx5808_RSSI_Ad_Min0; }
uint16_t RX5808_Get_RSSI_Ad_Max0() { return Rx5808_RSSI_Ad_Max0; }
uint16_t RX5808_Get_RSSI_Ad_Min1() { return Rx5808_RSSI_Ad_Min1; }
uint16_t RX5808_Get_RSSI_Ad_Max1() { return Rx5808_RSSI_Ad_Max1; }
uint16_t RX5808_Get_OSD_Format() { return Rx5808_OSD_Format; }
uint16_t RX5808_Get_Language() { return Rx5808_Language; }
uint16_t RX5808_Get_Signal_Source() { return Rx5808_Signal_Source; }
uint16_t RX5808_Get_LED_Brightness() { return Rx5808_LED_Brightness; }
uint16_t RX5808_Get_CPU_Freq() { return Rx5808_CPU_Freq; }
uint16_t RX5808_Get_GUI_Update_Rate() { return Rx5808_GUI_Update_Rate; }

bool RX5808_Calib_RSSI(uint16_t min0,uint16_t max0,uint16_t min1,uint16_t max1)
{
	if((min0+RX5808_CALIB_RSSI_ADC_VALUE_THRESHOULD<=max0)&&(min1+RX5808_CALIB_RSSI_ADC_VALUE_THRESHOULD<=max1)) return true;
	return false;
}

float Rx5808_Calculate_RSSI_Precentage(uint16_t value, uint16_t min, uint16_t max)
{
  if(max<=min) return 0;
  float precent=((float)((value - min)*100)) / (float)(max - min);
  if(precent>=99.0f) precent=99.0f;
  if(precent<=0) precent=0;
  return precent;
}

static uint16_t filter_rssi(uint16_t new_value, uint16_t *buffer, uint8_t *index) {
    buffer[*index] = new_value;
    *index = (*index + 1) % RSSI_FILTER_SIZE;
    uint32_t sum = 0;
    for(int i = 0; i < RSSI_FILTER_SIZE; i++) sum += buffer[i];
    return sum / RSSI_FILTER_SIZE;
}

float Rx5808_Get_Precentage0() {
    uint16_t filtered_value = filter_rssi(adc_converted_value[0], rssi0_filter_buffer, &rssi0_filter_index);
    return Rx5808_Calculate_RSSI_Precentage(filtered_value, Rx5808_RSSI_Ad_Min0, Rx5808_RSSI_Ad_Max0);
}

float Rx5808_Get_Precentage1() {
    uint16_t filtered_value = filter_rssi(adc_converted_value[1], rssi1_filter_buffer, &rssi1_filter_index);
    return Rx5808_Calculate_RSSI_Precentage(filtered_value, Rx5808_RSSI_Ad_Min1, Rx5808_RSSI_Ad_Max1);
}

float Get_Battery_Voltage() { return (float)adc_converted_value[2]/4095*6.8; }

void DMA2_Stream0_IRQHandler(void)
{
	while(1) {
        uint32_t sum0=0,sum1=0;
        int _adc_raw;
        if (adc_mutex != NULL) xSemaphoreTake(adc_mutex, portMAX_DELAY);
        for(int i=0;i<16;i++) {
            adc_oneshot_read(adc1_handle, RX5808_RSSI0_CHAN, &_adc_raw); sum0 += _adc_raw;
            adc_oneshot_read(adc1_handle, RX5808_RSSI1_CHAN, &_adc_raw); sum1 += _adc_raw;
        }
        adc_converted_value[0] = sum0 >> 4;
        adc_converted_value[1] = sum1 >> 4;
        adc_oneshot_read(adc1_handle, VBAT_ADC_CHAN, &_adc_raw);
        adc_converted_value[2] = (uint16_t)_adc_raw;
        if (adc_mutex != NULL) xSemaphoreGive(adc_mutex);
		int sig_src = Rx5808_Signal_Source;
		if(RX5808_Shutdown) sig_src = 3;
		if(sig_src==1) { gpio_set_level(RX5808_SWITCH1, 1); gpio_set_level(RX5808_SWITCH0, 0); }
		else if(sig_src==2) { gpio_set_level(RX5808_SWITCH0, 1); gpio_set_level(RX5808_SWITCH1, 0); }
		else if(sig_src==3) { gpio_set_level(RX5808_SWITCH0, 0); gpio_set_level(RX5808_SWITCH1, 0); }
		vTaskDelay(25 / portTICK_PERIOD_MS);
	}
}

void RX5808_Check_Backpack_Activity(void) { }
void RX5808_Set_Backpack_Detected(bool detected) { backpack_detected = detected; }
bool RX5808_Is_Backpack_Detected(void) { return backpack_detected; }
uint16_t RX5808_Get_Expected_Frequency(void) { return expected_frequency; }
void RX5808_Clear_Backpack_Detection(void) { backpack_detected = false; }

void RX5808_Init_Band_X(void) {
	if (!Band_X_Loaded) {
		esp_err_t ret = nvs_flash_init();
		if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
			nvs_flash_erase(); nvs_flash_init();
		}
		RX5808_Load_Band_X_From_NVS();
		Band_X_Loaded = true;
	}
}

void RX5808_Set_Band_X_Freq(uint8_t channel, uint16_t freq) { if (channel < 8) Band_X_Custom_Freq[channel] = freq; }
uint16_t RX5808_Get_Band_X_Freq(uint8_t channel) { return (channel < 8) ? Band_X_Custom_Freq[channel] : 5800; }

void RX5808_Save_Band_X_To_NVS(void) {
	nvs_handle_t nvs_handle;
	if (nvs_open(NVS_NAMESPACE_BANDX, NVS_READWRITE, &nvs_handle) == ESP_OK) {
		nvs_set_blob(nvs_handle, NVS_KEY_BANDX_FREQS, Band_X_Custom_Freq, sizeof(Band_X_Custom_Freq));
		nvs_commit(nvs_handle); nvs_close(nvs_handle);
	}
}

void RX5808_Load_Band_X_From_NVS(void) {
	nvs_handle_t nvs_handle;
	if (nvs_open(NVS_NAMESPACE_BANDX, NVS_READWRITE, &nvs_handle) == ESP_OK) {
		size_t blob_size = sizeof(Band_X_Custom_Freq);
		nvs_get_blob(nvs_handle, NVS_KEY_BANDX_FREQS, Band_X_Custom_Freq, &blob_size);
		nvs_close(nvs_handle);
	}
}

bool RX5808_Is_Band_X(void) { return (Chx_count == 6); }

uint16_t RX5808_Get_Current_Freq(void)
{
	if (RX5808_Is_Band_X()) return RX5808_Get_Band_X_Freq(channel_count);
    if (Chx_count == 5 && rx5808_div_setup[rx5808_div_config_l_band_grid_type] == 1) {
        return Rx5808_L_Band_Grid2[channel_count];
    }
	if (Chx_count < 6 && channel_count < 8) return Rx5808_Freq[Chx_count][channel_count];
	return 5800;
}
