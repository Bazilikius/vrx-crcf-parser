/**
 * @file lv_mem_pool.h
 * @brief LVGL custom memory allocator with pre-allocated pools
 * @version 1.7.1
 */

#ifndef LV_MEM_POOL_H
#define LV_MEM_POOL_H

#include <stddef.h>

/**
 * @brief Initialize memory pools
 */
void lv_mem_pool_init(void);

/**
 * @brief Custom allocator for LVGL
 * @param size Size in bytes to allocate
 * @return Pointer to allocated memory or NULL on failure
 */
void* lv_mem_pool_alloc(size_t size);

/**
 * @brief Custom free for LVGL
 * @param ptr Pointer to memory to free
 */
void lv_mem_pool_free(void* ptr);

/**
 * @brief Custom realloc for LVGL
 * @param ptr Pointer to existing memory
 * @param new_size New size in bytes
 * @return Pointer to reallocated memory or NULL on failure
 */
void* lv_mem_pool_realloc(void* ptr, size_t new_size);

/**
 * @brief Print pool statistics
 */
void lv_mem_pool_stats(void);

#endif // LV_MEM_POOL_H
