#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "CONFIG";
config_t device_config;

void config_load(void) {
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READWRITE, &handle) == ESP_OK) {
        size_t len = sizeof(device_config);
        if (nvs_get_blob(handle, "config", &device_config, &len) != ESP_OK) {
            memset(&device_config, 0, sizeof(device_config));
            device_config.ch_v = 12;
            device_config.ch_b = 11;
        }
        nvs_close(handle);
    }
}

void config_save(void) {
    nvs_handle_t handle;
    if (nvs_open("storage", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_blob(handle, "config", &device_config, sizeof(device_config));
        nvs_commit(handle);
        nvs_close(handle);
    }
}
