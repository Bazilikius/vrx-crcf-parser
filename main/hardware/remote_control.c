#include "crsf.h"
#include "rx5808.h"
#include "rx5808_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "REMOTE_CTRL";

extern uint32_t rx5808_div_setup[];

static void remote_control_task(void *pvParameters) {
    uint16_t channels[CRSF_MAX_CHANNELS];
    int last_v_pos = -1;
    int last_b_pos = -1;

    while (1) {
        if (crsf_get_channels(channels)) {
            int ch_v_idx = rx5808_div_setup[rx5808_div_config_crsf_ch_video] - 1;
            int ch_b_idx = rx5808_div_setup[rx5808_div_config_crsf_ch_band] - 1;

            if (ch_v_idx >= 0 && ch_v_idx < 16) {
                // CRSF channel range 172 - 1811, center 992
                // Divide into 8 segments for 8 channels
                int v_pos = (channels[ch_v_idx] - 172) * 8 / (1811 - 172);
                if (v_pos < 0) v_pos = 0;
                if (v_pos > 7) v_pos = 7;

                if (v_pos != last_v_pos) {
                    uint8_t current_ch = Rx5808_Get_Channel();
                    uint8_t current_band = current_ch / 8;
                    Rx5808_Set_Channel(current_band * 8 + v_pos);
                    RX5808_Set_Freq(RX5808_Get_Current_Freq());
                    last_v_pos = v_pos;
                    ESP_LOGI(TAG, "Remote Video Channel Switch: %d", v_pos + 1);
                }
            }

            if (ch_b_idx >= 0 && ch_b_idx < 16) {
                // Divide into 6 segments for 6 bands (A, B, E, F, R, L)
                int b_pos = (channels[ch_b_idx] - 172) * 6 / (1811 - 172);
                if (b_pos < 0) b_pos = 0;
                if (b_pos > 5) b_pos = 5;

                if (b_pos != last_b_pos) {
                    uint8_t current_ch = Rx5808_Get_Channel();
                    uint8_t current_v_ch = current_ch % 8;
                    Rx5808_Set_Channel(b_pos * 8 + current_v_ch);
                    RX5808_Set_Freq(RX5808_Get_Current_Freq());
                    last_b_pos = b_pos;
                    ESP_LOGI(TAG, "Remote Band Switch: %d", b_pos);
                }
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void remote_control_init(void) {
    xTaskCreate(remote_control_task, "remote_ctrl_task", 4096, NULL, 5, NULL);
}
