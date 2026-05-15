#include "elrs_backpack.h"
#include "elrs_msp.h"
#include "elrs_config.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "rx5808.h"
#include <string.h>

static const char *TAG = "ELRS_BP";

// ESP-NOW Configuration
#define ESPNOW_WIFI_CHANNEL 1
#define ESPNOW_QUEUE_SIZE 20
#define ESPNOW_MAX_DATA_LEN 250

// Binding Configuration
#define BINDING_TIMEOUT_MS 30000  // 30 seconds default

// ESP-NOW Message Structure
typedef struct {
    uint8_t mac_addr[6];
    uint8_t data[ESPNOW_MAX_DATA_LEN];
    int data_len;
} espnow_event_t;

// Static Variables
static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t elrs_uid[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static uint8_t tx_mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};  // TX's actual MAC address (sender)
static elrs_bind_state_t binding_state = ELRS_STATE_UNBOUND;
static QueueHandle_t espnow_queue = NULL;
static TaskHandle_t elrs_task_handle = NULL;
static msp_parser_t msp_parser;
static TimerHandle_t binding_timer = NULL;
static uint32_t binding_timeout_ms = 0;
static uint32_t binding_start_time = 0;
static SemaphoreHandle_t channel_mutex = NULL;  // Thread safety for channel changes
static uint8_t last_remote_channel = 0xFF;  // Track last remote channel for logging
static bool vtx_band_swap_enabled = false;  // VTX band swap for non-standard VTX tables

// Forward Declarations
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len);
static void elrs_backpack_task(void *param);
static bool reinit_espnow_with_uid(const uint8_t uid[6]);
static void process_msp_packet(msp_parser_t *parser);
static void handle_msp_set_vtx_config(const uint8_t *payload, uint16_t length);
static void handle_msp_elrs_bind(const uint8_t *payload, uint16_t length);
static void binding_timeout_callback(TimerHandle_t xTimer);
static void complete_binding(void);
static bool send_msp_packet(const uint8_t *dest_mac, uint16_t function, uint8_t type, const uint8_t *payload, uint16_t payload_size);

// MSP Parser Feed Byte Implementation
bool msp_parser_feed_byte(msp_parser_t *parser, uint8_t byte) {
    switch (parser->state) {
        case MSP_IDLE:
            if (byte == MSP_V2_HEADER_START) {
                parser->state = MSP_HEADER_START;
                parser->crc = 0;
            }
            break;

        case MSP_HEADER_START:
            if (byte == MSP_V2_HEADER_X) {
                parser->state = MSP_HEADER_X;
            } else {
                parser->state = MSP_IDLE;
            }
            break;

        case MSP_HEADER_X:
            if (byte == MSP_V2_FLAG_REQUEST || byte == MSP_V2_FLAG_RESPONSE || byte == MSP_V2_FLAG_ERROR) {
                // Direction byte is NOT included in CRC (header is not part of checksum)
                parser->state = MSP_HEADER_V2_FLAGS;  // Next: read flag byte
            } else {
                parser->state = MSP_IDLE;
            }
            break;

        case MSP_HEADER_V2_FLAGS:
            parser->flags = byte;  // Flag byte (0x00 typically)
            parser->crc = crc8_dvb_s2(parser->crc, byte);
            parser->state = MSP_HEADER_V2_FUNC_L;  // Next: read function low byte
            break;

        case MSP_HEADER_V2_FUNC_L:
            parser->function = byte;  // Function low byte
            parser->crc = crc8_dvb_s2(parser->crc, byte);
            parser->state = MSP_HEADER_V2_FUNC_H;
            break;

        case MSP_HEADER_V2_FUNC_H:
            parser->function |= (uint16_t)byte << 8;  // Function high byte
            parser->crc = crc8_dvb_s2(parser->crc, byte);
            parser->state = MSP_HEADER_V2_SIZE_L;
            break;

        case MSP_HEADER_V2_SIZE_L:
            parser->payload_size = byte;  // Size low byte
            parser->crc = crc8_dvb_s2(parser->crc, byte);
            parser->state = MSP_HEADER_V2_SIZE_H;
            break;

        case MSP_HEADER_V2_SIZE_H:
            parser->payload_size |= (uint16_t)byte << 8;  // Size high byte
            parser->crc = crc8_dvb_s2(parser->crc, byte);
            parser->payload_index = 0;

            if (parser->payload_size > 0) {
                parser->state = MSP_PAYLOAD_V2;
            } else {
                parser->state = MSP_CHECKSUM_V2;
            }
            break;

        case MSP_PAYLOAD_V2:
            if (parser->payload_index < sizeof(parser->payload)) {
                parser->payload[parser->payload_index++] = byte;
                parser->crc = crc8_dvb_s2(parser->crc, byte);
            }

            if (parser->payload_index >= parser->payload_size) {
                parser->state = MSP_CHECKSUM_V2;
            }
            break;

        case MSP_CHECKSUM_V2:
            // Verify CRC
            if (byte == parser->crc) {
                parser->state = MSP_IDLE;
                return true;  // Complete packet received
            } else {
                ESP_LOGW(TAG, "MSP CRC mismatch: expected 0x%02X, got 0x%02X", parser->crc, byte);
                parser->state = MSP_IDLE;
            }
            break;

        default:
            parser->state = MSP_IDLE;
            break;
    }

    return false;
}

// Send MSP Packet via ESP-NOW
static bool send_msp_packet(const uint8_t *dest_mac, uint16_t function, uint8_t type, const uint8_t *payload, uint16_t payload_size) {
    if (payload_size > 200) {
        ESP_LOGE(TAG, "MSP payload too large: %d bytes", payload_size);
        return false;
    }

    // Build MSP v2 packet: $X<type><flags><func_l><func_h><size_l><size_h><payload><crc>
    uint8_t buffer[250];
    buffer[0] = '$';
    buffer[1] = 'X';
    buffer[2] = type;  // '<' for command, '>' for response
    buffer[3] = 0x00;  // flags
    buffer[4] = function & 0xFF;  // function low byte
    buffer[5] = (function >> 8) & 0xFF;  // function high byte
    buffer[6] = payload_size & 0xFF;  // size low byte
    buffer[7] = (payload_size >> 8) & 0xFF;  // size high byte

    // Copy payload if any
    if (payload_size > 0 && payload != NULL) {
        memcpy(&buffer[8], payload, payload_size);
    }

    // Calculate CRC8-DVB-S2 (from byte 3 onwards: flags, function, size, payload)
    uint8_t crc = 0;
    for (int i = 3; i < 8 + payload_size; i++) {
        crc = crc8_dvb_s2(crc, buffer[i]);
    }
    buffer[8 + payload_size] = crc;

    // Send via ESP-NOW
    uint16_t total_len = 9 + payload_size;
    esp_err_t err = esp_now_send(dest_mac, buffer, total_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send MSP packet (func=0x%04X): %s", function, esp_err_to_name(err));
        return false;
    }

    ESP_LOGD(TAG, "Sent MSP: func=0x%04X", function);  // Keep debug quiet
    return true;
}

// Binding Timeout Callback
static void binding_timeout_callback(TimerHandle_t xTimer) {
    ESP_LOGW(TAG, "Binding timeout");

    // Transition to timeout state
    binding_state = ELRS_STATE_BIND_TIMEOUT;

    // Remove broadcast peer
    esp_now_del_peer(broadcast_mac);

    // Timer will be deleted by the binding cancel function
}

// Complete Binding Process
static void complete_binding(void) {
    ESP_LOGI(TAG, "Completing binding with UID: %02X:%02X:%02X:%02X:%02X:%02X",
             elrs_uid[0], elrs_uid[1], elrs_uid[2], elrs_uid[3], elrs_uid[4], elrs_uid[5]);

    // Save UID to NVS
    if (!elrs_config_save_uid(elrs_uid)) {
        ESP_LOGE(TAG, "Failed to save UID to NVS");
        binding_state = ELRS_STATE_UNBOUND;
        return;
    }

    // Save TX sender MAC to NVS
    if (!elrs_config_save_tx_mac(tx_mac)) {
        ESP_LOGW(TAG, "Failed to save TX MAC to NVS (non-fatal)");
    }

    // Remove broadcast peer
    esp_now_del_peer(broadcast_mac);

    // Stop binding timer
    if (binding_timer != NULL) {
        xTimerStop(binding_timer, 0);
        xTimerDelete(binding_timer, 0);
        binding_timer = NULL;
    }

    // Reinitialize ESP-NOW with new UID as MAC
    if (!reinit_espnow_with_uid(elrs_uid)) {
        ESP_LOGE(TAG, "Failed to reinitialize ESP-NOW with UID");
        binding_state = ELRS_STATE_UNBOUND;
        return;
    }

    // Add TX as peer using its ACTUAL sender MAC address
    // (TX has already sanitized its MAC, e.g., 50:45:... not 51:45:...)
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, tx_mac, 6);  // Use sender MAC from bind packet
    peer_info.channel = ESPNOW_WIFI_CHANNEL;
    peer_info.ifidx = WIFI_IF_STA;
    peer_info.encrypt = false;

    esp_err_t err = esp_now_add_peer(&peer_info);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(TAG, "Failed to add TX peer: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Added TX peer (sender MAC): %02X:%02X:%02X:%02X:%02X:%02X",
                 tx_mac[0], tx_mac[1], tx_mac[2],
                 tx_mac[3], tx_mac[4], tx_mac[5]);
    }

    // Transition to success state
    binding_state = ELRS_STATE_BIND_SUCCESS;
    ESP_LOGI(TAG, "Binding successful!");

    // After a brief moment, transition to BOUND state
    // (UI can show success message during this time)
    vTaskDelay(pdMS_TO_TICKS(1000));
    binding_state = ELRS_STATE_BOUND;
}

// Handle MSP SET_VTX_CONFIG Command (0x0059) - ACTUAL channel changes
static void handle_msp_set_vtx_config(const uint8_t *payload, uint16_t length) {
    ESP_LOGI(TAG, "==== MSP_SET_VTX_CONFIG (0x0059) ====");
    ESP_LOGI(TAG, "Size: %u bytes", length);

    // Print full payload for diagnosis
    if (length > 0) {
        char hex_str[128];
        int offset = 0;
        for (int i = 0; i < length && i < 20; i++) {
            offset += snprintf(hex_str + offset, sizeof(hex_str) - offset, "%02X ", payload[i]);
        }
        ESP_LOGI(TAG, "Payload: %s", hex_str);
    }

    // Need at least 1 byte for channel index
    if (length < 1) {
        ESP_LOGW(TAG, "Packet too short (need 1+ bytes)");
        return;
    }

    // Extract channel from byte[0] (ELRS backpack standard)
    uint8_t channel_index = payload[0];
    ESP_LOGI(TAG, "Channel from byte[0] = 0x%02X (%u decimal)", channel_index, channel_index);

    // VTX Band Swap: If enabled, swap R (32-39) ↔ L (40-47) to match non-standard VTX tables
    if (vtx_band_swap_enabled && channel_index >= 32 && channel_index <= 47) {
        uint8_t original_index = channel_index;

        if (channel_index >= 32 && channel_index <= 39) {
            // R (32-39) → L (40-47)
            channel_index = 40 + (channel_index - 32);
        } else if (channel_index >= 40 && channel_index <= 47) {
            // L (40-47) → R (32-39)
            channel_index = 32 + (channel_index - 40);
        }

        ESP_LOGI(TAG, "VTX Band Swap: %u → %u (R↔L remapped)", original_index, channel_index);
    }

    // Validate channel index (0-47 for standard bands)
    if (channel_index > 47) {
        ESP_LOGW(TAG, "Channel index out of range: %u (valid: 0-47)", channel_index);
        return;
    }

    // Thread-safe channel update with mutex protection
    if (channel_mutex != NULL && xSemaphoreTake(channel_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Check if channel actually changed
        if (last_remote_channel != channel_index) {
            // Calculate band/channel for display
            uint8_t band = channel_index / 8;
            uint8_t channel = channel_index % 8;

            // Look up actual frequency from band/channel table
            uint16_t frequency = Rx5808_Freq[band][channel];

            // CRITICAL: Tune RX5808 hardware to new frequency (includes 50ms PLL settling)
            RX5808_Set_Freq(frequency);

            // Update channel variables for GUI
            Rx5808_Set_Channel(channel_index);
            last_remote_channel = channel_index;

            ESP_LOGI(TAG, ">>> CHANNEL CHANGED: %c%u (index %u) → %u MHz <<<",
                     "ABEFRЛ"[band], channel + 1, channel_index, frequency);

            // GUI will automatically update on next polling cycle
        } else {
            // Channel unchanged - don't spam logs
            ESP_LOGD(TAG, "Channel unchanged: %u", channel_index);
        }

        xSemaphoreGive(channel_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire channel mutex for remote change");
    }
}

// Handle MSP ELRS_BIND Command
static void handle_msp_elrs_bind(const uint8_t *payload, uint16_t length) {
    ESP_LOGI(TAG, "handle_msp_elrs_bind called! length=%u, state=%d", length, binding_state);

    if (length < 6) {
        ESP_LOGW(TAG, "MSP_ELRS_BIND: payload too short (%u bytes)", length);
        return;
    }

    // If already bound, ignore (no keepalive ACKs like jp39)
    if (binding_state == ELRS_STATE_BOUND) {
        ESP_LOGI(TAG, "Received bind packet while bound - ignoring");
        return;
    }

    // Only process bind packets when in BINDING state
    if (binding_state != ELRS_STATE_BINDING) {
        ESP_LOGW(TAG, "Ignoring bind packet (not in BINDING state, current state: %d)", binding_state);
        return;
    }

    // Extract UID from payload (first 6 bytes)
    memcpy(elrs_uid, payload, 6);

    ESP_LOGI(TAG, "Received bind packet with UID: %02X:%02X:%02X:%02X:%02X:%02X",
             elrs_uid[0], elrs_uid[1], elrs_uid[2], elrs_uid[3], elrs_uid[4], elrs_uid[5]);
    ESP_LOGI(TAG, "TX sender MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             tx_mac[0], tx_mac[1], tx_mac[2],
             tx_mac[3], tx_mac[4], tx_mac[5]);

    // Complete binding process
    complete_binding();
}

// Process Complete MSP Packet
static void process_msp_packet(msp_parser_t *parser) {
    ESP_LOGD(TAG, "MSP func=0x%04X, size=%u", parser->function, parser->payload_size);

    switch (parser->function) {
        case MSP_ELRS_BACKPACK_CRSF_TLM:  // 0x0011 - Telemetry (GPS/battery/linkstats) - ignore
            ESP_LOGD(TAG, "Received CRSF telemetry (0x0011) - ignoring");
            break;

        case MSP_SET_VTX_CONFIG:  // 0x0059 - ACTUAL VTX channel commands
            handle_msp_set_vtx_config(parser->payload, parser->payload_size);
            break;

        case MSP_ELRS_BIND:
            ESP_LOGI(TAG, "Processing MSP_ELRS_BIND");
            handle_msp_elrs_bind(parser->payload, parser->payload_size);
            break;

        case 88:  // MSP_VTXTABLE_BAND
        case 90:  // MSP_VTXTABLE_POWERLEVEL
            ESP_LOGD(TAG, "VTX table command: 0x%04X (not implemented)", parser->function);
            break;

        default:
            ESP_LOGD(TAG, "Unknown MSP: 0x%04X", parser->function);
            break;
    }
}

// ESP-NOW Receive Callback (ISR context) - ESP-IDF v5.5 signature
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len) {
    if (data_len <= 0 || data_len > ESPNOW_MAX_DATA_LEN) {
        return;
    }

    // Log source MAC periodically to detect multiple senders (TX + drone)
    static uint32_t last_log = 0;
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now - last_log > 5000) {  // Log every 5 seconds
        ESP_LOGI(TAG, "Receiving from MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
                 recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);
        last_log = now;
    }

    // Queue message for processing in task context
    espnow_event_t evt;
    memcpy(evt.mac_addr, recv_info->src_addr, 6);
    memcpy(evt.data, data, data_len);
    evt.data_len = data_len;

    if (xQueueSend(espnow_queue, &evt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Queue full, packet dropped");
    }
}

// ELRS Backpack Task (processes ESP-NOW messages)
static void elrs_backpack_task(void *param) {
    espnow_event_t evt;
    TickType_t last_keepalive_time = 0;
    const TickType_t keepalive_interval = pdMS_TO_TICKS(5000);  // 5 seconds

    ESP_LOGI(TAG, "ELRS Backpack task started");

    while (1) {
        // Wait for messages with timeout to allow periodic keepalive
        if (xQueueReceive(espnow_queue, &evt, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // Feed bytes into MSP parser
            for (int i = 0; i < evt.data_len; i++) {
                if (msp_parser_feed_byte(&msp_parser, evt.data[i])) {
                    // Complete MSP packet received - detailed logging in handlers

                    // Store sender MAC for bind packets
                    if (msp_parser.function == MSP_ELRS_BIND) {
                        ESP_LOGI(TAG, "MSP BIND packet received");
                        memcpy(tx_mac, evt.mac_addr, 6);
                    }

                    process_msp_packet(&msp_parser);
                    msp_parser_init(&msp_parser);  // Reset for next packet
                }
            }
        }

        // Send periodic keepalive queries when bound
        TickType_t current_time = xTaskGetTickCount();
        if (binding_state == ELRS_STATE_BOUND &&
            (current_time - last_keepalive_time) >= keepalive_interval) {

            // Send keepalive query to keep connection alive
            if (tx_mac[0] != 0x00 || tx_mac[1] != 0x00) {  // Check if TX MAC is valid
                send_msp_packet(tx_mac, 0x0382, '<', NULL, 0);  // MSP_GET_BP_STATUS
            }
            last_keepalive_time = current_time;
        }
    }
}

// Reinitialize ESP-NOW with New UID as MAC Address
static bool reinit_espnow_with_uid(const uint8_t uid[6]) {
    ESP_LOGI(TAG, "Reinitializing ESP-NOW with UID as MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             uid[0], uid[1], uid[2], uid[3], uid[4], uid[5]);

    // Deinitialize ESP-NOW
    esp_now_deinit();

    // Stop WiFi
    esp_wifi_stop();

    // ESP32 requires unicast MAC (bit 0 must be 0)
    uint8_t our_mac[6];
    memcpy(our_mac, uid, 6);
    our_mac[0] &= 0xFE;  // Clear multicast bit

    if (our_mac[0] != uid[0]) {
        ESP_LOGI(TAG, "Using unicast MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 our_mac[0], our_mac[1], our_mac[2], our_mac[3], our_mac[4], our_mac[5]);
    }

    esp_err_t err = esp_wifi_set_mac(WIFI_IF_STA, our_mac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set MAC address: %s", esp_err_to_name(err));
        // Try to restart WiFi anyway
        esp_wifi_start();
        return false;
    }

    // Restart WiFi
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
        return false;
    }

    // Set WiFi channel after starting
    err = esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi channel: %s", esp_err_to_name(err));
        return false;
    }

    // Reinitialize ESP-NOW
    err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW: %s", esp_err_to_name(err));
        return false;
    }

    // Register receive callback
    err = esp_now_register_recv_cb(espnow_recv_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register receive callback: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "ESP-NOW reinitialized successfully with new MAC");
    return true;
}

// Initialize ELRS Backpack
bool ELRS_Backpack_Init(void) {
    ESP_LOGI(TAG, "Initializing ELRS Backpack (ESP-NOW)");

    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "Erasing NVS flash and reinitializing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi in STA mode
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Try to load UID from NVS and set MAC address before starting WiFi
    if (elrs_config_load_uid(elrs_uid)) {
        ESP_LOGI(TAG, "Loaded UID from NVS: %02X:%02X:%02X:%02X:%02X:%02X",
                 elrs_uid[0], elrs_uid[1], elrs_uid[2], elrs_uid[3], elrs_uid[4], elrs_uid[5]);

        // ESP32 requires unicast MAC (bit 0 must be 0)
        // If UID has multicast bit set, clear it for our MAC
        uint8_t our_mac[6];
        memcpy(our_mac, elrs_uid, 6);
        our_mac[0] &= 0xFE;  // Clear multicast bit (bit 0)

        if (our_mac[0] != elrs_uid[0]) {
            ESP_LOGI(TAG, "UID has multicast bit, using unicast MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                     our_mac[0], our_mac[1], our_mac[2], our_mac[3], our_mac[4], our_mac[5]);
        }

        ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_STA, our_mac));
        binding_state = ELRS_STATE_BOUND;
    } else {
        ESP_LOGI(TAG, "No UID found in NVS, starting in UNBOUND state");
        binding_state = ELRS_STATE_UNBOUND;
    }

    // Load VTX band swap setting (for non-standard VTX with R/L swapped)
    elrs_config_load_vtx_band_swap(&vtx_band_swap_enabled);
    ESP_LOGI(TAG, "VTX band mode: %s", vtx_band_swap_enabled ? "SWAPPED (R↔L)" : "STANDARD");

    // Start WiFi (must be called before setting channel)
    ESP_ERROR_CHECK(esp_wifi_start());
    // Item 3: reduce TX power to 10dBm (40) vs 20dBm default (80) — cuts WiFi heat
    esp_wifi_set_max_tx_power(40);

    // Set WiFi channel after starting
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_LOGI(TAG, "WiFi set to channel %d for ESP-NOW", ESPNOW_WIFI_CHANNEL);

    // Initialize ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());

    // Register receive callback
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    // Note: We do NOT add peers for receiving - ESP-NOW receives from ANY sender
    // Peers are only needed for sending encrypted packets
    // This allows us to receive broadcasts from both TX and drone VTX backpack
    ESP_LOGI(TAG, "ESP-NOW listening for broadcasts from any source");

    // If already bound, save TX MAC for future sending (keepalive queries)
    // If already bound, add TX peer for sending keepalive queries
    if (binding_state == ELRS_STATE_BOUND) {
        // Try to load TX sender MAC from NVS (saved during binding)
        if (elrs_config_load_tx_mac(tx_mac)) {
            // Add as peer for SENDING only (receiving works without peers)
            esp_now_peer_info_t peer_info = {};
            memcpy(peer_info.peer_addr, tx_mac, 6);
            peer_info.channel = ESPNOW_WIFI_CHANNEL;
            peer_info.ifidx = WIFI_IF_STA;
            peer_info.encrypt = false;

            esp_err_t err = esp_now_add_peer(&peer_info);
            if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
                ESP_LOGE(TAG, "Failed to add TX peer for sending: %s", esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "Added TX peer for sending queries: %02X:%02X:%02X:%02X:%02X:%02X",
                         tx_mac[0], tx_mac[1], tx_mac[2], tx_mac[3], tx_mac[4], tx_mac[5]);
            }
        } else {
            ESP_LOGW(TAG, "No TX MAC in NVS - keepalive queries disabled");
        }
    }

    // Create message queue
    espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(espnow_event_t));
    if (espnow_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create ESP-NOW queue");
        return false;
    }

    // Create mutex for thread-safe channel control
    channel_mutex = xSemaphoreCreateMutex();
    if (channel_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create channel mutex");
        return false;
    }

    // Initialize MSP parser
    msp_parser_init(&msp_parser);

    // Create background task on Core 1 for better load distribution (v1.7.1)
    xTaskCreatePinnedToCore(
        elrs_backpack_task,
        "elrs_backpack",
        4096,
        NULL,
        3,
        &elrs_task_handle,
        1  // Core 1 - offload network processing from UI core
    );

    ESP_LOGI(TAG, "ELRS Backpack initialized successfully (Core 1)");
    return true;
}

// Get Current Binding State
elrs_bind_state_t ELRS_Backpack_Get_State(void) {
    return binding_state;
}

// Start Binding Process
bool ELRS_Backpack_Start_Binding(uint32_t timeout_ms) {
    if (binding_state == ELRS_STATE_BINDING) {
        ESP_LOGW(TAG, "Already in BINDING state");
        return false;
    }

    ESP_LOGI(TAG, "Starting binding process (timeout: %lu ms)", timeout_ms);

    // Add broadcast MAC as peer
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, broadcast_mac, 6);
    peer_info.channel = ESPNOW_WIFI_CHANNEL;
    peer_info.ifidx = WIFI_IF_STA;
    peer_info.encrypt = false;

    esp_err_t err = esp_now_add_peer(&peer_info);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(TAG, "Failed to add broadcast peer: %s", esp_err_to_name(err));
        return false;
    }

    // Create and start binding timer (one-shot)
    binding_timeout_ms = timeout_ms;
    binding_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    binding_timer = xTimerCreate(
        "binding_timer",
        pdMS_TO_TICKS(timeout_ms),
        pdFALSE,  // One-shot timer
        NULL,
        binding_timeout_callback
    );

    if (binding_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create binding timer");
        esp_now_del_peer(broadcast_mac);
        return false;
    }

    xTimerStart(binding_timer, 0);

    // Transition to BINDING state
    binding_state = ELRS_STATE_BINDING;

    ESP_LOGI(TAG, "Binding process started, waiting for TX...");
    return true;
}

// Cancel Binding Process
void ELRS_Backpack_Cancel_Binding(void) {
    if (binding_state != ELRS_STATE_BINDING) {
        ESP_LOGW(TAG, "Not in BINDING state");
        return;
    }

    ESP_LOGI(TAG, "Cancelling binding process");

    // Stop and delete binding timer
    if (binding_timer != NULL) {
        xTimerStop(binding_timer, 0);
        xTimerDelete(binding_timer, 0);
        binding_timer = NULL;
    }

    // Remove broadcast peer
    esp_now_del_peer(broadcast_mac);

    // Revert to previous state
    if (elrs_config_has_uid()) {
        binding_state = ELRS_STATE_BOUND;
    } else {
        binding_state = ELRS_STATE_UNBOUND;
    }
}

// Check if Bound
bool ELRS_Backpack_Is_Bound(void) {
    return (binding_state == ELRS_STATE_BOUND || binding_state == ELRS_STATE_BIND_SUCCESS);
}

// Unbind from TX
void ELRS_Backpack_Unbind(void) {
    ESP_LOGI(TAG, "Unbinding from TX");

    // Clear UID from NVS
    elrs_config_clear_uid();

    // Clear local UID
    memset(elrs_uid, 0, 6);

    // Reinitialize with default MAC (all zeros will use default)
    uint8_t default_mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    // Deinitialize ESP-NOW
    esp_now_deinit();

    // Stop WiFi
    esp_wifi_stop();

    // Reset to default MAC (must be done while WiFi is stopped)
    esp_wifi_set_mac(WIFI_IF_STA, default_mac);

    // Restart WiFi
    esp_wifi_start();

    // Set WiFi channel after starting
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    // Reinitialize ESP-NOW
    esp_now_init();
    esp_now_register_recv_cb(espnow_recv_cb);

    // Transition to UNBOUND state
    binding_state = ELRS_STATE_UNBOUND;

    ESP_LOGI(TAG, "Successfully unbound");
}

// Get Current UID
void ELRS_Backpack_Get_UID(uint8_t uid[6]) {
    if (uid != NULL) {
        memcpy(uid, elrs_uid, 6);
    }
}

// Get Remaining Binding Timeout
uint32_t ELRS_Backpack_Get_Binding_Timeout_Remaining(void) {
    if (binding_state != ELRS_STATE_BINDING) {
        return 0;
    }

    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t elapsed = current_time - binding_start_time;

    if (elapsed >= binding_timeout_ms) {
        return 0;
    }

    return (binding_timeout_ms - elapsed) / 1000;  // Return in seconds
}

// Thread-Safe Channel Change for Local Control
bool ELRS_Backpack_Set_Channel_Safe(uint8_t channel) {
    // Validate channel range
    if (channel > 47) {
        ESP_LOGW(TAG, "Invalid channel %u (valid range: 0-47)", channel);
        return false;
    }

    // Acquire mutex for thread-safe operation
    if (channel_mutex == NULL) {
        ESP_LOGE(TAG, "Channel mutex not initialized");
        return false;
    }

    if (xSemaphoreTake(channel_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire channel mutex for local change");
        return false;
    }

    // Update channel
    Rx5808_Set_Channel(channel);
    ESP_LOGI(TAG, "Local channel changed to %u (Band %u, Ch %u)",
             channel, channel / 8, (channel % 8) + 1);

    // Release mutex
    xSemaphoreGive(channel_mutex);

    return true;
}

// Send VTX configuration packet via ESP-NOW
bool ELRS_Backpack_Send_VTX_Config(uint8_t channel_idx) {
    uint8_t payload[4];
    payload[0] = channel_idx;
    payload[1] = 0x00; // freq msb (not used for simple channel sets)
    payload[2] = 0x00; // power idx
    payload[3] = 0x00; // pit mode

    // Broadcast to everyone
    return send_msp_packet(broadcast_mac, MSP_SET_VTX_CONFIG, '<', payload, sizeof(payload));
}

// Get VTX Band Swap Setting
bool ELRS_Backpack_Get_VTX_Band_Swap(void) {
    return vtx_band_swap_enabled;
}

// Set VTX Band Swap Setting
bool ELRS_Backpack_Set_VTX_Band_Swap(bool swap_enabled) {
    vtx_band_swap_enabled = swap_enabled;

    // Save to NVS for persistence
    if (!elrs_config_save_vtx_band_swap(swap_enabled)) {
        ESP_LOGW(TAG, "Failed to save VTX band swap setting to NVS");
        return false;
    }

    ESP_LOGI(TAG, "VTX band swap %s", swap_enabled ? "ENABLED (R↔L)" : "DISABLED (standard)");
    return true;
}
