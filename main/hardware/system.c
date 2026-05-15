#include <stdint.h>
#include "driver/gpio.h"
#include "24cxx.h"
#include "beep.h"
#include "lcd.h"
#include "rx5808.h"
#include "ws2812.h"
#include "fan.h"
#include "rx5808_config.h"
#include "system.h"
#include "esp_timer.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "hwvers.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "diversity.h"
#include "led.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_clk_tree.h"

#ifdef ELRS_BACKPACK_ENABLE
#include "elrs_backpack.h"
#endif
#include "crsf.h"
#include "web_server.h"
#include "remote_control.h"

static const char *TAG = "SYSTEM";

// LVGL task delay: 20ms (50Hz) active, 50ms (20Hz) when locked/idle
volatile uint8_t system_lvgl_sleep_ms = 20;


// led_flash_task and LED_Init() removed (G): they wrote to LED0_PIN directly,
// racing with led.c's pattern engine which owns the same GPIO.
// led_init() in led.c is the sole owner of LED0_PIN at runtime.

// Apply CPU frequency based on user setting
void system_apply_cpu_freq(uint16_t freq_setting)
{
	uint32_t freq_mhz;
	bool auto_mode = false;

    // Map setting to actual frequency
    switch (freq_setting) {
        case 0:  freq_mhz = 80;  break;
        case 1:  freq_mhz = 160; break;
        case 2:  freq_mhz = 240; break;
		case 3:
		default:
			auto_mode = true;
			freq_mhz = 240;
			break;
    }

	ESP_LOGI(TAG, "Applying CPU frequency mode: %s", auto_mode ? "AUTO" : "FIXED");

    #ifdef CONFIG_PM_ENABLE
    // Use power management to set frequency dynamically.
    // Light sleep is enabled in AUTO mode: the main loop and LVGL timer both
    // call vTaskDelay() which satisfies the tickless-idle precondition, so the
    // core can sleep between LVGL frames and RSSI samples for meaningful power
    // and heat reduction.  Fixed-frequency modes keep it off to avoid wakeup
    // latency affecting time-critical GPIO or SPI operations.
    // Requires CONFIG_PM_ENABLE=y and CONFIG_FREERTOS_USE_TICKLESS_IDLE=1.
    esp_pm_config_esp32_t pm_config = {
		.max_freq_mhz = freq_mhz,
		.min_freq_mhz = auto_mode ? 80 : freq_mhz,
        .light_sleep_enable = auto_mode
    };

    esp_err_t ret = esp_pm_configure(&pm_config);
    if (ret == ESP_OK) {
		if (auto_mode) {
			ESP_LOGI(TAG, "CPU frequency set to AUTO range: 80-%lu MHz", freq_mhz);
		} else {
			ESP_LOGI(TAG, "CPU frequency fixed at %lu MHz", freq_mhz);
		}
    } else {
        ESP_LOGW(TAG, "Failed to set CPU frequency via PM (error %d), trying direct method", ret);
    }
    #else
    // Power management not enabled, log current frequency
    uint32_t current_freq = 0;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &current_freq);
	ESP_LOGI(TAG, "PM not enabled. Current CPU frequency: %lu MHz (requested %lu MHz, mode: %s)",
			 current_freq / 1000000, freq_mhz, auto_mode ? "AUTO" : "FIXED");
    ESP_LOGI(TAG, "To enable dynamic frequency scaling, set CONFIG_PM_ENABLE=y in sdkconfig");
    #endif
}

esp_timer_handle_t esp_timer_tick = 0;
void timer_periodic_cb(void *arg) {
   lv_tick_inc(1);
}

esp_timer_create_args_t periodic_arg = { .callback =
		&timer_periodic_cb,
		.arg = NULL,
		.name = "LVGL_TICK_TIMER"
};


void timer_init()
{
      esp_err_t err;
      err = esp_timer_create(&periodic_arg, &esp_timer_tick);
	  ESP_ERROR_CHECK(err);
	  err = esp_timer_start_periodic(esp_timer_tick, 1 * 1000);
      ESP_ERROR_CHECK(err);
      //printf("Timer_Init! OK\n");

}

void system_init(void)
{
	// Display firmware version banner
	printf("\n");
	printf("╔══════════════════════════════════════════════════════╗\n");
	printf("║  RX5808 Diversity Receiver Firmware v%d.%d.%d         ║\n",
	         RX5808_VERSION_MAJOR, RX5808_VERSION_MINOR, RX5808_VERSION_PATCH);
	printf("║  - ExpressLRS Backpack Integration                  ║\n");
	printf("║  - Performance Optimizations (RSSI Filter, 3x Speed) ║\n");
	printf("║  - Hardware v1.2 Support (D0WDQ6_VER)                ║\n");
	printf("╚══════════════════════════════════════════════════════╝\n");
	printf("\n");

	LCD_Init();
	printf("lcd init success!\n");
	fan_Init();
	printf("fan init success!\n");
	thermal_control_init();
	eeprom_24cxx_init();
	printf("24cxx init success!\n");
	rx5808_div_setup_load();
	printf("setup load success!\n");

	// Apply CPU frequency from settings
	system_apply_cpu_freq(RX5808_Get_CPU_Freq());

	led_init();
	printf("led init success!\n");
	Beep_Init();
	printf("beep init success!\n");
    timer_init();
	printf("timer init success!\n");
    RX5808_Init();
	printf("RX5808 init success!\n");

	// Initialize diversity algorithm
	diversity_init();
	printf("Diversity algorithm initialized!\n");

	//ws2812_init();
	//printf("ws2812 init success!\n");

	#ifdef ELRS_BACKPACK_ENABLE
	// Initialize ELRS Backpack (ESP-NOW wireless implementation)
	if (ELRS_Backpack_Init()) {
		printf("ELRS Backpack initialized (ESP-NOW)!\n");
	} else {
		printf("ELRS Backpack initialization failed!\n");
	}
	#endif

    // Initialize CRSF listener on UART2 (GPIO 16 RX)
    crsf_init(16);

    // Initialize Web Configuration Server (WiFi AP)
    web_server_init();

    // Initialize Remote Channel Control logic
    remote_control_init();

	//while(1);

    //create_cpu_stack_monitor_task();

}


// --- LVGL idle rate control ---

void system_set_lvgl_idle(bool idle)
{
    system_lvgl_sleep_ms = idle ? 50 : 20;
    ESP_LOGD(TAG, "LVGL sleep: %dms", system_lvgl_sleep_ms);
}

// --- CPU power context control ---

void system_set_cpu_context_idle(bool idle)
{
#ifdef CONFIG_PM_ENABLE
    if (idle) {
        // Locked/idle: cap at 80 MHz; enable light sleep so the core can
        // actually power-gate while waiting for the next LVGL vTaskDelay.
        esp_pm_config_esp32_t pm = {
            .max_freq_mhz      = 80,
            .min_freq_mhz      = 80,
            .light_sleep_enable = true
        };
        esp_pm_configure(&pm);
        ESP_LOGD(TAG, "CPU context: IDLE (80 MHz + light sleep)");
    } else {
        // Active: restore user-configured frequency
        system_apply_cpu_freq(RX5808_Get_CPU_Freq());
        ESP_LOGD(TAG, "CPU context: ACTIVE (user freq)");
    }
#endif
}
