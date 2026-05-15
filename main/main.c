#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "spi_flash_mmap.h"
#include "system.h"
#include "lvgl_init.h"

/**
 * @brief LVGL rendering loop — pinned to Core 0 (fix J).
 *
 * All RF/hardware tasks (RSSI, diversity, beep, ELRS) run on Core 1.
 * Separating LVGL onto Core 0 prevents any hardware blocking call from
 * stalling the display, and gives the UI its own dedicated CPU core.
 */
static void lvgl_loop_task(void *param)
{
    (void)param;
    lvgl_init();
    while (1) {
        lv_task_handler();
        vTaskDelay(system_lvgl_sleep_ms / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    system_init();

    // Pin the LVGL rendering task to Core 0; hardware tasks stay on Core 1.
    // Stack 8 KB covers all LVGL draw operations observed in profiling.
    xTaskCreatePinnedToCore(lvgl_loop_task, "lvgl", 8192, NULL, 5, NULL, 0);

    // app_main is no longer needed — delete this task to free its stack.
    vTaskDelete(NULL);
}
