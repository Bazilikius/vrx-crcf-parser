#ifndef __RX5808_DIV_HW_VERS_H
#define __RX5808_DIV_HW_VERS_H

// Software Version v1.8.0
// Changes in v1.8.0:
//   - UX: Unified navigation system (consistent controls across all pages)
//   - UX: Quick action menu (long-hold RIGHT for instant access)
//   - UX: Main page band/channel controls (UP/DOWN=band, LEFT/RIGHT=channel)
//   - UX: Smart channel scanning (auto-switch to strongest signal)
//   - UX: LED status feedback (meaningful patterns for every state)
//   - UX: Flattened menu structure (7 items, no nesting)
//   - Fix: Calibration noise floor detection (median + MAD filtering)
//   - Improve: Band X editor (auto-repeat, visual validation)
//   - Feature: AUTO CPU mode (dynamic 80/160/240 MHz)
//   - Defaults: 160 MHz CPU, 14 Hz GUI rate
//
// Changes in v1.7.1:
//   - Performance (#1): Complete diversity hardware switching (GPIO antenna control)
//   - Performance (#2): DMA-accelerated SPI (VSPI peripheral, 64% faster, non-blocking)
//   - Performance (#3): Adaptive RSSI sampling (100Hz active, 20Hz stable, 15-20% CPU savings)
//   - Performance (#4): Memory pools for LVGL objects (4KB pools, 60-70% fragmentation reduction)
//   - Performance (#5): Dual-core utilization (Core 1: RF & network, 38% Core 0 reduction)
//   - Performance (#6): PSRAM-ready font caching (future-ready for hardware upgrades)
//   - Fixed: TODO placeholders replaced with functional hardware calls
//
// Changes in v1.7.0:
//   - ELRS Wireless VRX Backpack: Control channel switching from ELRS TX via ESP-NOW
//   - VTX Band Swap Feature: Configurable R↔L band remapping for non-standard VTX firmware
//   - NVS Persistence: Band swap and ELRS settings survive power cycles
//   - Fixed RX5808 hardware tuning on remote channel commands
//   - Improved RSSI accuracy after channel changes
//   - Added 50ms PLL settling time for stable tuning
//
// Changes in v1.5.1:
//   - Advanced diversity algorithm with dwell, hysteresis, cooldown
//   - Variance-based stability scoring for better switching decisions
//   - Mode profiles: Race, Freestyle, Long Range
//   - Enhanced telemetry overlay (active RX, switch counters, RSSI delta)
//   - Interactive RSSI calibration wizard
//   - Switch event counters and stability metrics
//
// Changes in v1.5.0:
//   - ExpressLRS backpack integration (CRSF protocol, GPIO16/17 UART)
//   - Performance optimizations (RSSI filtering, 3x faster channel switching)
//   - Fixed Band E frequency mapping for VTX compatibility
//   - D0WDQ6_VER hardware variant support (v1.2 hardware)
#define RX5808_VERSION_MAJOR  1
#define RX5808_VERSION_MINOR  8
#define RX5808_VERSION_PATCH  1

//#define ST7735S
#define D0WDQ6_VER   // ENABLE this for v1.2 hardware
#define SPI_LOW_SPEED

// ELRS Backpack Support (ESP-NOW wireless implementation)
// Comment out to disable ELRS functionality
#define ELRS_BACKPACK_ENABLE


#define SPI_HOST_USER    HSPI_HOST

#define SPI_NUM_MISO 12
#define SPI_NUM_MOSI 13
#define SPI_NUM_CLK  14
#define SPI_NUM_CS   15

#define PIN_NUM_DC   2
#define PIN_NUM_RST  27
#define PIN_NUM_BCKL 26


#define io_i2c_gpio_sda  32
#define io_i2c_gpio_scl  33
#define i2c_fre  400*1000
#define i2c_port  I2C_NUM_0



// ADC channel constants use the new adc_channel_t enum (esp_adc/adc_oneshot.h).
// The legacy ADC1_CHANNEL_x names belong to driver/adc.h which is deprecated
// in ESP-IDF v5.x; the replacement API drops the unit prefix.
#include "esp_adc/adc_oneshot.h"
#define RX5808_RSSI0_CHAN  ADC_CHANNEL_0
#define RX5808_RSSI1_CHAN  ADC_CHANNEL_3
#ifndef D0WDQ6_VER
#define VBAT_ADC_CHAN      ADC_CHANNEL_1
#define KEY_ADC_CHAN       ADC_CHANNEL_2
#else
#define VBAT_ADC_CHAN      ADC_CHANNEL_7
#define KEY_ADC_CHAN       ADC_CHANNEL_6
#endif

#define RX5808_SCLK       18
#define RX5808_MOSI       23
#define RX5808_CS         5

#define RX5808_SWITCH0    4
#define RX5808_SWITCH1    12

#define RX5808_TOGGLE_DEAD_ZONE 1
#define RX5808_CALIB_RSSI_ADC_VALUE_THRESHOULD 100


#define Beep_Is_Src 1
#define RMT_TX_GPIO 19
#define LED0_PIN 21
#define FAN_IO_NUM 0
#define Beep_Pin_Num  22



#endif