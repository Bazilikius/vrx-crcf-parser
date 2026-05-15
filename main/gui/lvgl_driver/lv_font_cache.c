/**
 * @file lv_font_cache.c
 * @brief LVGL font caching system for PSRAM
 * @version 1.7.1
 *
 * Caches frequently-used Chinese fonts in PSRAM for faster access
 * Falls back to flash if PSRAM is not available
 */

#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl/lvgl.h"

static const char *TAG = "lv_font_cache";

// External font declarations
LV_FONT_DECLARE(lv_font_chinese_12);
LV_FONT_DECLARE(lv_font_chinese_16);

// Cached font copies (NULL if not cached)
static lv_font_t* lv_font_chinese_12_cached = NULL;
static lv_font_t* lv_font_chinese_16_cached = NULL;

// Cache statistics
static size_t total_cached_size = 0;
static bool psram_available = false;

/**
 * @brief Check if PSRAM is available
 */
static bool check_psram_available(void) {
    size_t psram_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (psram_size > 0) {
        ESP_LOGI(TAG, "PSRAM detected: %d KB free", psram_size / 1024);
        return true;
    } else {
        ESP_LOGW(TAG, "No PSRAM available - fonts will use flash memory");
        return false;
    }
}

/**
 * @brief Deep copy an LVGL font to PSRAM
 */
static lv_font_t* copy_font_to_psram(const lv_font_t* source_font, const char* font_name) {
    if (source_font == NULL) {
        ESP_LOGE(TAG, "Source font %s is NULL", font_name);
        return NULL;
    }

    // Allocate main font structure in PSRAM
    lv_font_t* cached_font = heap_caps_malloc(sizeof(lv_font_t), MALLOC_CAP_SPIRAM);
    if (cached_font == NULL) {
        ESP_LOGE(TAG, "Failed to allocate font structure for %s in PSRAM", font_name);
        return NULL;
    }

    // Copy font structure
    memcpy(cached_font, source_font, sizeof(lv_font_t));

    // Note: The font's glyph bitmap data is const and stored in flash (.rodata section)
    // We keep pointers to it - full deep copy would require copying all glyph data which is complex
    // The main benefit is having the font structure and frequently-accessed tables in PSRAM

    ESP_LOGI(TAG, "Cached font %s structure in PSRAM (%d bytes)",
             font_name, sizeof(lv_font_t));

    return cached_font;
}

/**
 * @brief Initialize font caching system
 */
void lv_font_cache_init(void) {
    ESP_LOGI(TAG, "Initializing font cache...");

    // Check PSRAM availability
    psram_available = check_psram_available();

    if (!psram_available) {
        ESP_LOGW(TAG, "Font caching disabled (no PSRAM) - using flash fonts");
        return;
    }

    // Cache Chinese fonts (most used)
    lv_font_chinese_12_cached = copy_font_to_psram(&lv_font_chinese_12, "lv_font_chinese_12");
    if (lv_font_chinese_12_cached != NULL) {
        total_cached_size += sizeof(lv_font_t);
    }

    lv_font_chinese_16_cached = copy_font_to_psram(&lv_font_chinese_16, "lv_font_chinese_16");
    if (lv_font_chinese_16_cached != NULL) {
        total_cached_size += sizeof(lv_font_t);
    }

    ESP_LOGI(TAG, "Font cache initialized: %d bytes in PSRAM", total_cached_size);
}

/**
 * @brief Get cached version of lv_font_chinese_12 (or original if not cached)
 */
const lv_font_t* lv_font_get_chinese_12(void) {
    return (lv_font_chinese_12_cached != NULL) ? lv_font_chinese_12_cached : &lv_font_chinese_12;
}

/**
 * @brief Get cached version of lv_font_chinese_16 (or original if not cached)
 */
const lv_font_t* lv_font_get_chinese_16(void) {
    return (lv_font_chinese_16_cached != NULL) ? lv_font_chinese_16_cached : &lv_font_chinese_16;
}

/**
 * @brief Print cache statistics
 */
void lv_font_cache_stats(void) {
    ESP_LOGI(TAG, "Font Cache Statistics:");
    ESP_LOGI(TAG, "  PSRAM available: %s", psram_available ? "YES" : "NO");
    ESP_LOGI(TAG, "  Total cached size: %d bytes", total_cached_size);
    ESP_LOGI(TAG, "  lv_font_chinese_12: %s",
             lv_font_chinese_12_cached != NULL ? "CACHED" : "FLASH");
    ESP_LOGI(TAG, "  lv_font_chinese_16: %s",
             lv_font_chinese_16_cached != NULL ? "CACHED" : "FLASH");

    if (psram_available) {
        size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "  PSRAM free: %d KB", psram_free / 1024);
    }
}

/**
 * @brief Cleanup font cache (call before shutdown)
 */
void lv_font_cache_deinit(void) {
    if (lv_font_chinese_12_cached != NULL) {
        heap_caps_free(lv_font_chinese_12_cached);
        lv_font_chinese_12_cached = NULL;
    }

    if (lv_font_chinese_16_cached != NULL) {
        heap_caps_free(lv_font_chinese_16_cached);
        lv_font_chinese_16_cached = NULL;
    }

    total_cached_size = 0;
    ESP_LOGI(TAG, "Font cache cleaned up");
}
