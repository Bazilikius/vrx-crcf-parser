#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "crsf.h"
#include "elrs_backpack_emul.h"
#include "web_server.h"
#include "config.h"

static const char *TAG = "MAIN";

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    config_load();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // Set custom MAC for STA interface (ELRS UID)
    uint8_t sta_mac[6];
    memcpy(sta_mac, device_config.uid, 6);
    sta_mac[0] &= 0xFE; // Clear multicast bit
    ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_STA, sta_mac));

    web_server_init(); // Configures AP and starts WiFi

    // Now that WiFi is started, init ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());
    backpack_emul_init(device_config.uid); // Adds peer

    crsf_init(16); // GPIO 16 RX

    ESP_LOGI(TAG, "Bridge started. WiFi SSID: Backpack-Emul. CRSF on GPIO 16.");

    uint16_t channels[CRSF_MAX_CHANNELS];
    int last_band = -1;
    int last_channel = -1;

    while (1) {
        if (crsf_get_channels(channels)) {
            // S2 - Video Channel (8 segments)
            int ch_idx = device_config.ch_v - 1;
            int current_ch = -1;
            if (ch_idx >= 0 && ch_idx < 16) {
                current_ch = (channels[ch_idx] - 172) * 8 / (1811 - 172 + 1);
                if (current_ch < 0) current_ch = 0;
                if (current_ch > 7) current_ch = 7;
            }

            // S3 - Band (6 segments: A, B, E, F, R, L)
            int band_idx = device_config.ch_b - 1;
            int current_band = -1;
            if (band_idx >= 0 && band_idx < 16) {
                current_band = (channels[band_idx] - 172) * 6 / (1811 - 172 + 1);
                if (current_band < 0) current_band = 0;
                if (current_band > 5) current_band = 5;
            }

            if (current_ch != -1 && current_band != -1) {
                if (current_ch != last_channel || current_band != last_band) {
                    last_channel = current_ch;
                    last_band = current_band;

                    uint8_t msp_idx;
                    if (current_band == 5 && device_config.l_grid == 1) {
                        // If L-Band Grid 2 is selected, we map it to Band X (index 6)
                        // VRX must have Band X configured to Grid 2 frequencies
                        msp_idx = 6 * 8 + current_ch;
                        ESP_LOGI(TAG, "Switching to L-Band Grid 2 (mapped to Band X index %d)", msp_idx);
                    } else {
                        msp_idx = current_band * 8 + current_ch;
                        ESP_LOGI(TAG, "Switching to Band %d Channel %d (index %d)", current_band, current_ch + 1, msp_idx);
                    }
                    backpack_emul_send_vtx_config(msp_idx);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
