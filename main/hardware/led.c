#include "led.h"
#include "hwvers.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "LED";

// Board LED is active-low on GPIO21: 0=ON, 1=OFF
#define LED_ACTIVE_LOW 1

// LED state
static led_pattern_t current_pattern = LED_PATTERN_OFF;
static led_pattern_t saved_pattern = LED_PATTERN_OFF;
static uint8_t signal_strength_rssi = 0;
static uint8_t led_brightness_percent = 100;
static bool double_blink_triggered = false;
static SemaphoreHandle_t led_mutex = NULL;

// FreeRTOS task handle
static TaskHandle_t led_task_handle = NULL;

static inline void led_write(bool on)
{
#if LED_ACTIVE_LOW
    gpio_set_level(LED0_PIN, on ? 0 : 1);
#else
    gpio_set_level(LED0_PIN, on ? 1 : 0);
#endif
}

static void led_apply_brightness(uint8_t brightness, uint32_t tick)
{
    uint8_t scaled = (uint8_t)((brightness * led_brightness_percent) / 100);
    if (scaled == 0) {
        led_write(false);
    } else if (scaled >= 100) {
        led_write(true);
    } else {
        uint8_t period_ms = 20;
        uint8_t on_time = (uint8_t)((scaled * period_ms) / 100);
        led_write((tick % period_ms) < on_time);
    }
}

/**
 * @brief Calculate LED brightness for breathing pattern
 *
 * Uses a precomputed 100-entry LUT instead of sinf() to avoid FPU use.
 * The LUT maps (tick % 2000) / 20 -> brightness 0-100 following a full
 * sine wave: starts at 50, peaks at 100 (step 25), troughs at 0 (step 75).
 *
 * @param tick Current tick value (advances by 20 each iteration)
 * @return Brightness (0-100)
 */
static const uint8_t breathing_lut[100] = {
    /* 0-9  */  50, 53, 56, 59, 62, 65, 68, 71, 74, 77,
    /* 10-19 */ 79, 82, 84, 86, 89, 90, 92, 94, 95, 96,
    /* 20-29 */ 98, 98, 99,100,100,100,100,100, 99, 98,
    /* 30-39 */ 98, 96, 95, 94, 92, 90, 89, 86, 84, 82,
    /* 40-49 */ 79, 77, 74, 71, 68, 65, 62, 59, 56, 53,
    /* 50-59 */ 50, 47, 44, 41, 38, 35, 32, 29, 26, 23,
    /* 60-69 */ 21, 18, 16, 14, 11, 10,  8,  6,  5,  4,
    /* 70-79 */  2,  2,  1,  0,  0,  0,  0,  0,  1,  2,
    /* 80-89 */  2,  4,  5,  6,  8, 10, 11, 14, 16, 18,
    /* 90-99 */ 21, 23, 26, 29, 32, 35, 38, 41, 44, 47
};
static uint8_t calculate_breathing_brightness(uint32_t tick)
{
    return breathing_lut[(tick % 2000) / 20];
}

/**
 * @brief Calculate LED brightness for heartbeat pattern
 *
 * @param tick Current tick value
 * @return Brightness (0-100), or 0 for off periods
 */
static uint8_t calculate_heartbeat_brightness(uint32_t tick)
{
    // Heartbeat cycle: 2 seconds
    // Two quick pulses, then pause
    uint32_t cycle_pos = tick % 2000;

    if (cycle_pos < 100) {
        // First pulse (0-100ms)
        return 100;
    } else if (cycle_pos < 200) {
        // First fade (100-200ms)
        return 100 - ((cycle_pos - 100) * 100 / 100);
    } else if (cycle_pos < 350) {
        // Short pause (200-350ms)
        return 0;
    } else if (cycle_pos < 450) {
        // Second pulse (350-450ms)
        return 100;
    } else if (cycle_pos < 550) {
        // Second fade (450-550ms)
        return 100 - ((cycle_pos - 450) * 100 / 100);
    } else {
        // Long pause (550-2000ms)
        return 0;
    }
}

/**
 * @brief LED control task
 *
 * Runs continuously, updates LED based on current pattern.
 *
 * @param param Unused
 */
static void led_task(void *param)
{
    uint32_t tick = 0;
    uint8_t brightness = 0;
    bool led_state = false;
    uint8_t double_blink_count = 0;

    while (1) {
        // Check for double blink trigger
        if (xSemaphoreTake(led_mutex, 0) == pdTRUE) {
            if (double_blink_triggered) {
                double_blink_count = 4; // 2 blinks = 4 transitions (on-off-on-off)
                double_blink_triggered = false;
            }
            xSemaphoreGive(led_mutex);
        }

        // Handle double blink (overrides other patterns)
        if (double_blink_count > 0) {
            led_state = !led_state;
            led_apply_brightness(led_state ? 100 : 0, tick);
            double_blink_count--;
            if (double_blink_count == 0) {
                // Restore the pattern that was active before the blink
                current_pattern = saved_pattern;
            }
            vTaskDelay(100 / portTICK_PERIOD_MS);
            tick += 100;
            continue;
        }

        // Handle normal patterns
        switch (current_pattern) {
            case LED_PATTERN_OFF:
                led_apply_brightness(0, tick);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                tick += 100;
                break;

            case LED_PATTERN_SOLID:
                led_apply_brightness(100, tick);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                tick += 100;
                break;

            case LED_PATTERN_HEARTBEAT:
                brightness = calculate_heartbeat_brightness(tick);
                led_apply_brightness(brightness, tick);
                vTaskDelay(10 / portTICK_PERIOD_MS);
                tick += 10;
                break;

            case LED_PATTERN_FAST_BLINK:
                // 5 Hz = 200ms period = 100ms on, 100ms off
                led_state = (tick % 200) < 100;
                led_apply_brightness(led_state ? 100 : 0, tick);
                vTaskDelay(10 / portTICK_PERIOD_MS);
                tick += 10;
                break;

            case LED_PATTERN_BREATHING:
                brightness = calculate_breathing_brightness(tick);
                led_apply_brightness(brightness, tick);
                // pdMS_TO_TICKS() rounds UP so 20ms is at least 1 tick
                // regardless of tick rate; 20ms gives 100 steps per
                // 2-second breathing cycle — smooth enough.
                vTaskDelay(pdMS_TO_TICKS(20));
                tick += 20;
                break;

            case LED_PATTERN_SIGNAL_STRENGTH:
                // LED brightness proportional to signal strength
                led_apply_brightness(signal_strength_rssi, tick);
                vTaskDelay(pdMS_TO_TICKS(20));
                tick += 20;
                break;

            case LED_PATTERN_DOUBLE_BLINK:
                // Handled above
                vTaskDelay(10 / portTICK_PERIOD_MS);
                break;

            default:
                led_apply_brightness(0, tick);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                tick += 100;
                break;
        }
    }
}

void led_init(void)
{
    ESP_LOGI(TAG, "Initializing LED module on GPIO %d", LED0_PIN);

    // Configure GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED0_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Start with LED off
    led_write(false);

    // Create mutex
    led_mutex = xSemaphoreCreateMutex();
    if (led_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create LED mutex");
        return;
    }

    // Create LED task
    xTaskCreatePinnedToCore(
        led_task,
        "led_task",
        2048,
        NULL,
        5,  // Priority 5 (below GUI, above idle)
        &led_task_handle,
        1   // Core 1 (RF tasks)
    );

    if (led_task_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create LED task");
        return;
    }

    ESP_LOGI(TAG, "LED module initialized successfully");
}

void led_set_pattern(led_pattern_t pattern)
{
    if (led_mutex != NULL) {
        xSemaphoreTake(led_mutex, portMAX_DELAY);
        current_pattern = pattern;
        ESP_LOGD(TAG, "LED pattern changed to: %d", pattern);
        xSemaphoreGive(led_mutex);
    } else {
        current_pattern = pattern;
    }
}

led_pattern_t led_get_pattern(void)
{
    led_pattern_t pattern;
    if (led_mutex != NULL) {
        xSemaphoreTake(led_mutex, portMAX_DELAY);
        pattern = current_pattern;
        xSemaphoreGive(led_mutex);
    } else {
        pattern = current_pattern;
    }
    return pattern;
}

void led_set_signal_strength(uint8_t rssi_percent)
{
    if (rssi_percent > 100) {
        rssi_percent = 100;
    }

    if (led_mutex != NULL) {
        xSemaphoreTake(led_mutex, portMAX_DELAY);
        signal_strength_rssi = rssi_percent;
        xSemaphoreGive(led_mutex);
    } else {
        signal_strength_rssi = rssi_percent;
    }
}

void led_set_brightness(uint8_t brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = 100;
    }

    if (led_mutex != NULL) {
        xSemaphoreTake(led_mutex, portMAX_DELAY);
        led_brightness_percent = brightness_percent;
        xSemaphoreGive(led_mutex);
    } else {
        led_brightness_percent = brightness_percent;
    }
}

void led_trigger_double_blink(void)
{
    if (led_mutex != NULL) {
        xSemaphoreTake(led_mutex, portMAX_DELAY);
        saved_pattern = current_pattern;
        double_blink_triggered = true;
        ESP_LOGD(TAG, "Double blink triggered");
        xSemaphoreGive(led_mutex);
    }
}
