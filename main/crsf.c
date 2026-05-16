#include "crsf.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "CRSF";

#define CRSF_BAUDRATE 420000
#define UART_PORT UART_NUM_2
#define BUF_SIZE 1024

static crsf_data_t crsf_data;

static const uint8_t crsf_crc8tab[256] = {
    0x00, 0xD5, 0x7F, 0xAA, 0xFE, 0x2B, 0x81, 0x54, 0x29, 0xFC, 0x56, 0x83, 0xD7, 0x02, 0xA8, 0x7D,
    0x52, 0x87, 0x2D, 0xF8, 0xAC, 0x79, 0xD3, 0x06, 0x7B, 0xAE, 0x04, 0xD1, 0x85, 0x50, 0xFA, 0x2F,
    0xA4, 0x71, 0xDB, 0x0E, 0x5A, 0x8F, 0x25, 0xF0, 0x8D, 0x58, 0xF2, 0x27, 0x73, 0xA6, 0x0C, 0xD9,
    0xF6, 0x23, 0x89, 0x5C, 0x08, 0xDD, 0x77, 0xA2, 0xDF, 0x0A, 0xA0, 0x75, 0x21, 0xF4, 0x5E, 0x8B,
    0x9D, 0x48, 0xE2, 0x37, 0x63, 0xB6, 0x1C, 0xC9, 0xB4, 0x61, 0xCB, 0x1E, 0x4A, 0x9F, 0x35, 0xE0,
    0xCF, 0x1A, 0xB0, 0x65, 0x31, 0xE4, 0x4E, 0x9B, 0xE6, 0x33, 0x99, 0x4C, 0x18, 0xCD, 0x67, 0xB2,
    0x39, 0xEC, 0x46, 0x93, 0xC7, 0x12, 0xB8, 0x6D, 0x10, 0xC5, 0x6F, 0xBA, 0xEE, 0x3B, 0x91, 0x44,
    0x6B, 0xBE, 0x14, 0xC1, 0x95, 0x40, 0xEA, 0x3F, 0x42, 0x97, 0x3D, 0xE8, 0xBC, 0x69, 0xC3, 0x16,
    0xEF, 0x3A, 0x90, 0x45, 0x11, 0xC4, 0x6E, 0xBB, 0xC6, 0x13, 0xB9, 0x6C, 0x38, 0xED, 0x47, 0x92,
    0xBD, 0x68, 0xC2, 0x17, 0x43, 0x96, 0x3C, 0xE9, 0x94, 0x41, 0xEB, 0x3E, 0x6A, 0xBF, 0x15, 0xC0,
    0x4B, 0x9E, 0x34, 0xE1, 0xB5, 0x60, 0xCA, 0x1F, 0x62, 0xB7, 0x1D, 0xC8, 0x9C, 0x49, 0xE3, 0x36,
    0x19, 0xCC, 0x66, 0xB3, 0xE7, 0x32, 0x98, 0x4D, 0x30, 0xE5, 0x4F, 0x9A, 0xCE, 0x1B, 0xB1, 0x64,
    0x72, 0xA7, 0x0D, 0xD8, 0x8C, 0x59, 0xF3, 0x26, 0x5B, 0x8E, 0x24, 0xF1, 0xA5, 0x70, 0xDA, 0x0F,
    0x20, 0xF5, 0x5F, 0x8A, 0xDE, 0x0B, 0xA1, 0x74, 0x09, 0xDC, 0x76, 0xA3, 0xF7, 0x22, 0x88, 0x5D,
    0xD6, 0x03, 0xA9, 0x7C, 0x28, 0xFD, 0x57, 0x82, 0xFF, 0x2A, 0x80, 0x55, 0x01, 0xD4, 0x7E, 0xAB,
    0x84, 0x51, 0xFB, 0x2E, 0x7A, 0xAF, 0x05, 0xD0, 0xAD, 0x78, 0xD2, 0x07, 0x53, 0x86, 0x2C, 0xF9
};

static uint8_t crsf_crc8(const uint8_t *ptr, uint8_t len) {
    uint8_t crc = 0;
    while (len--) crc = crsf_crc8tab[crc ^ *ptr++];
    return crc;
}

static void crsf_parse_channels(const uint8_t *payload) {
    crsf_data.channels[0]  = (uint16_t)((payload[0]       | payload[1] << 8)                          & 0x07FF);
    crsf_data.channels[1]  = (uint16_t)((payload[1] >> 3  | payload[2] << 5)                          & 0x07FF);
    crsf_data.channels[2]  = (uint16_t)((payload[2] >> 6  | payload[3] << 2 | payload[4] << 10)       & 0x07FF);
    crsf_data.channels[3]  = (uint16_t)((payload[4] >> 1  | payload[5] << 7)                          & 0x07FF);
    crsf_data.channels[4]  = (uint16_t)((payload[5] >> 4  | payload[6] << 4)                          & 0x07FF);
    crsf_data.channels[5]  = (uint16_t)((payload[6] >> 7  | payload[7] << 1 | payload[8] << 9)        & 0x07FF);
    crsf_data.channels[6]  = (uint16_t)((payload[8] >> 2  | payload[9] << 6)                          & 0x07FF);
    crsf_data.channels[7]  = (uint16_t)((payload[9] >> 5  | payload[10] << 3)                         & 0x07FF);
    crsf_data.channels[8]  = (uint16_t)((payload[11]      | payload[12] << 8)                         & 0x07FF);
    crsf_data.channels[9]  = (uint16_t)((payload[12] >> 3 | payload[13] << 5)                         & 0x07FF);
    crsf_data.channels[10] = (uint16_t)((payload[13] >> 6 | payload[14] << 2 | payload[15] << 10)      & 0x07FF);
    crsf_data.channels[11] = (uint16_t)((payload[15] >> 1 | payload[16] << 7)                         & 0x07FF);
    crsf_data.channels[12] = (uint16_t)((payload[16] >> 4 | payload[17] << 4)                         & 0x07FF);
    crsf_data.channels[13] = (uint16_t)((payload[17] >> 7 | payload[18] << 1 | payload[19] << 9)       & 0x07FF);
    crsf_data.channels[14] = (uint16_t)((payload[19] >> 2 | payload[20] << 6)                         & 0x07FF);
    crsf_data.channels[15] = (uint16_t)((payload[20] >> 5 | payload[21] << 3)                         & 0x07FF);
    crsf_data.updated = true;
}

static void crsf_task(void *pvParameters) {
    uint8_t *data = (uint8_t *) malloc(BUF_SIZE);
    while (1) {
        int len = uart_read_bytes(UART_PORT, data, BUF_SIZE, 20 / portTICK_PERIOD_MS);
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                if (data[i] == 0xC8 && i + 1 < len) {
                    uint8_t frame_len = data[i+1];
                    if (frame_len >= 2 && frame_len <= 62 && i + frame_len + 1 < len) {
                        if (crsf_crc8(&data[i+2], frame_len - 1) == data[i + frame_len + 1]) {
                            if (data[i+2] == 0x16) crsf_parse_channels(&data[i+3]);
                            i += frame_len + 1;
                        }
                    }
                }
            }
        }
    }
}

void crsf_init(int rx_pin) {
    const uart_config_t uart_config = {
        .baud_rate = CRSF_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_driver_install(UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_PORT, &uart_config);
    uart_set_pin(UART_PORT, UART_PIN_NO_CHANGE, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    xTaskCreate(crsf_task, "crsf_task", 4096, NULL, 10, NULL);
}

bool crsf_get_channels(uint16_t *channels) {
    if (crsf_data.updated) {
        memcpy(channels, crsf_data.channels, sizeof(uint16_t) * CRSF_MAX_CHANNELS);
        crsf_data.updated = false;
        return true;
    }
    return false;
}
