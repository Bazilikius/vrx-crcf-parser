/**
 * @file lv_font_cache.h
 * @brief LVGL font caching system for PSRAM
 * @version 1.7.1
 */

#ifndef LV_FONT_CACHE_H
#define LV_FONT_CACHE_H

#include "lvgl/lvgl.h"

/**
 * @brief Initialize font caching system
 * Call this early in initialization, before creating any LVGL objects
 */
void lv_font_cache_init(void);

/**
 * @brief Get cached version of lv_font_chinese_12
 * @return Pointer to cached font (or original if not cached)
 */
const lv_font_t* lv_font_get_chinese_12(void);

/**
 * @brief Get cached version of lv_font_chinese_16
 * @return Pointer to cached font (or original if not cached)
 */
const lv_font_t* lv_font_get_chinese_16(void);

/**
 * @brief Print cache statistics
 */
void lv_font_cache_stats(void);

/**
 * @brief Cleanup font cache
 */
void lv_font_cache_deinit(void);

#endif // LV_FONT_CACHE_H
