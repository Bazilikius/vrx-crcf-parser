#include "fan.h"
#include "driver/ledc.h"
#include "hwvers.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdint.h>

static const char* TAG_FAN = "FAN";

// The original ESP32 chip exposes die temperature via the ROM function
// temprature_sens_read() (note: intentional Espressif typo).  It returns
// a raw uint8_t value in degrees Fahrenheit; convert with (F-32)/1.8.
// The newer driver/temperature_sensor.h HAL is NOT available on ESP32.
extern uint8_t temprature_sens_read(void);

// User-configured floor speed (saved to EEPROM, returned by fan_get_speed)
volatile uint8_t fan_speed = 100;

// Actual PWM duty currently applied (thermal may push above user floor)
static volatile uint8_t fan_speed_actual = 100;

// Last die temperature reading (degrees C)
static volatile float thermal_temp_c = 25.0f;

// Thermal fan curve thresholds (die temp runs ~10-15 C above ambient)
#define THERMAL_TEMP_IDLE   48.0f
#define THERMAL_TEMP_FULL   68.0f
#define THERMAL_TEMP_WARN   73.0f
#define THERMAL_FAN_MIN_PCT 35


void fan_Init(void)
{
   ledc_timer_config_t ledc_timer_ls = {
			.duty_resolution = LEDC_TIMER_13_BIT, // 设置分辨率,最大为2^13-1
			.freq_hz = 8000,                      // PWM信号频率
			.speed_mode = LEDC_LOW_SPEED_MODE,    // 定时器模式（“高速”或“低速”）
			.timer_num = LEDC_TIMER_1,            // 设置定时器源（0-3）
			.clk_cfg = LEDC_AUTO_CLK,             // 配置LEDC时钟源（这里是自动选择）
		};
		// 初始化ledc的定时器配置
		ledc_timer_config(&ledc_timer_ls);
		/*
		* 通过选择为 LEDC 控制器的每个通道准备单独的配置：
		* - 控制器的通道号
		* - 输出占空比，初始设置为 0
		* - LEDC 连接到的 GPIO 编号
		* - 速度模式，高或低
		* - 为LEDC通道指定定时器
		*   注意: 如果不同通道使用一个定时器，那么这些通道的频率和占空比分辨率将相同
		*/
	    gpio_reset_pin(FAN_IO_NUM);
		ledc_channel_config_t ledc_channel = {
			.channel    = LEDC_CHANNEL_0,
			.duty       = 0,
			.gpio_num   = FAN_IO_NUM,
			.speed_mode = LEDC_LOW_SPEED_MODE,
			.hpoint     = 0,
			.timer_sel  = LEDC_TIMER_1
		};
		// 初始化ledc的通道
		ledc_channel_config(&ledc_channel);
}

void fan_set_speed(uint8_t duty)
{
    if (duty > 100) duty = 100;
    fan_speed = duty;  // update user floor (persisted to EEPROM)
    // Apply immediately only when not already running hotter from thermal
    if (duty >= fan_speed_actual) {
        fan_speed_actual = duty;
        int fan_duty = (int)(duty * 8191 / 100);  // 13-bit full scale = 8191
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, fan_duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
    // If thermal is currently higher, leave it; next 5-s poll re-evaluates
}

uint16_t fan_get_speed(void)
{
    return (uint16_t)fan_speed;  // return user floor for EEPROM save
}

// --- Thermal control ---

static void fan_set_pwm(uint8_t duty)
{
    if (duty > 100) duty = 100;
    fan_speed_actual = duty;
    // Integer LEDC duty: 13-bit full scale = 8191; avoids float multiply on every fan update.
    int fan_duty = (int)(duty * 8191 / 100);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, fan_duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static uint8_t thermal_curve(float temp_c)
{
    if (temp_c < THERMAL_TEMP_IDLE) return THERMAL_FAN_MIN_PCT;
    if (temp_c >= THERMAL_TEMP_FULL) return 100;
    float ratio = (temp_c - THERMAL_TEMP_IDLE) / (THERMAL_TEMP_FULL - THERMAL_TEMP_IDLE);
    return (uint8_t)(THERMAL_FAN_MIN_PCT + ratio * (100.0f - THERMAL_FAN_MIN_PCT));
}

static void thermal_task(void* param)
{
    while (1) {
        // ROM function returns Fahrenheit; convert to Celsius
        float temp_c = ((float)temprature_sens_read() - 32.0f) / 1.8f;
        thermal_temp_c = temp_c;

        uint8_t thermal = thermal_curve(temp_c);
        uint8_t target  = (thermal > fan_speed) ? thermal : fan_speed;

        if (target != fan_speed_actual) {
            ESP_LOGD(TAG_FAN, "Thermal: %.1fC -> fan %d%%", temp_c, target);
            fan_set_pwm(target);
        }

        if (temp_c >= THERMAL_TEMP_WARN) {
            ESP_LOGW(TAG_FAN, "HIGH TEMP %.1fC - fan at 100%%", temp_c);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void thermal_control_init(void)
{
    // No driver install needed: the ESP32 ROM exposes temprature_sens_read()
    // directly without any peripheral initialisation.
    // Pin to Core 1 alongside the other hardware tasks (RSSI, diversity, beep).
    // xTaskCreate() would leave the task on whichever core first schedules it;
    // pinning ensures it never migrates to Core 0 and competes with LVGL.
    xTaskCreatePinnedToCore(thermal_task, "thermal", 2048, NULL, 1, NULL, 1);
    ESP_LOGI(TAG_FAN, "Thermal fan control started");
}

float thermal_get_temp_c(void)
{
    return thermal_temp_c;
}