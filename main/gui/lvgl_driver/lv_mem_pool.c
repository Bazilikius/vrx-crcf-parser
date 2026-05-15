/**
 * @file lv_mem_pool.c
 * @brief LVGL custom memory allocator with pre-allocated pools
 * @version 1.7.1
 *
 * Implements memory pools for frequently allocated objects:
 * - Labels: 40 bytes each, 32 objects
 * - Buttons: 96 bytes each, 16 objects
 * - Containers: 80 bytes each, 16 objects
 *
 * Reduces malloc/free overhead and memory fragmentation
 */

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "lv_mem_pool";

// Pool configuration
#define LABEL_POOL_SIZE     32      // 32 label objects
#define LABEL_OBJECT_SIZE   40      // 40 bytes per label
#define BUTTON_POOL_SIZE    16      // 16 button objects
#define BUTTON_OBJECT_SIZE  96      // 96 bytes per button
#define CONTAINER_POOL_SIZE 16      // 16 container objects
#define CONTAINER_OBJECT_SIZE 80    // 80 bytes per container

// Pool memory blocks (statically allocated)
static uint8_t label_pool_memory[LABEL_POOL_SIZE * LABEL_OBJECT_SIZE];
static uint8_t button_pool_memory[BUTTON_POOL_SIZE * BUTTON_OBJECT_SIZE];
static uint8_t container_pool_memory[CONTAINER_POOL_SIZE * CONTAINER_OBJECT_SIZE];

// Free list bitmasks (1 = free, 0 = allocated)
static uint32_t label_pool_free = 0xFFFFFFFF;      // All 32 slots free
static uint32_t button_pool_free = 0x0000FFFF;     // 16 slots free
static uint32_t container_pool_free = 0x0000FFFF;  // 16 slots free

// Mutex for pool access protection
static SemaphoreHandle_t pool_mutex = NULL;

// Statistics
static uint32_t label_allocs = 0;
static uint32_t button_allocs = 0;
static uint32_t container_allocs = 0;
static uint32_t heap_allocs = 0;
static uint32_t failed_allocs = 0;

/**
 * @brief Initialize memory pools
 */
void lv_mem_pool_init(void) {
    pool_mutex = xSemaphoreCreateMutex();
    if (pool_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create pool mutex!");
        return;
    }

    memset(label_pool_memory, 0, sizeof(label_pool_memory));
    memset(button_pool_memory, 0, sizeof(button_pool_memory));
    memset(container_pool_memory, 0, sizeof(container_pool_memory));

    ESP_LOGI(TAG, "Memory pools initialized:");
    ESP_LOGI(TAG, "  Labels: %d objects x %d bytes = %d bytes",
             LABEL_POOL_SIZE, LABEL_OBJECT_SIZE, LABEL_POOL_SIZE * LABEL_OBJECT_SIZE);
    ESP_LOGI(TAG, "  Buttons: %d objects x %d bytes = %d bytes",
             BUTTON_POOL_SIZE, BUTTON_OBJECT_SIZE, BUTTON_POOL_SIZE * BUTTON_OBJECT_SIZE);
    ESP_LOGI(TAG, "  Containers: %d objects x %d bytes = %d bytes",
             CONTAINER_POOL_SIZE, CONTAINER_OBJECT_SIZE, CONTAINER_POOL_SIZE * CONTAINER_OBJECT_SIZE);
    ESP_LOGI(TAG, "  Total pool size: %d bytes",
             (LABEL_POOL_SIZE * LABEL_OBJECT_SIZE) +
             (BUTTON_POOL_SIZE * BUTTON_OBJECT_SIZE) +
             (CONTAINER_POOL_SIZE * CONTAINER_OBJECT_SIZE));
}

/**
 * @brief Allocate from label pool
 */
static void* pool_alloc_label(void) {
    for (uint8_t i = 0; i < LABEL_POOL_SIZE; i++) {
        if (label_pool_free & (1 << i)) {
            label_pool_free &= ~(1 << i);  // Mark as allocated
            label_allocs++;
            return &label_pool_memory[i * LABEL_OBJECT_SIZE];
        }
    }
    return NULL;  // Pool exhausted
}

/**
 * @brief Allocate from button pool
 */
static void* pool_alloc_button(void) {
    for (uint8_t i = 0; i < BUTTON_POOL_SIZE; i++) {
        if (button_pool_free & (1 << i)) {
            button_pool_free &= ~(1 << i);  // Mark as allocated
            button_allocs++;
            return &button_pool_memory[i * BUTTON_OBJECT_SIZE];
        }
    }
    return NULL;  // Pool exhausted
}

/**
 * @brief Allocate from container pool
 */
static void* pool_alloc_container(void) {
    for (uint8_t i = 0; i < CONTAINER_POOL_SIZE; i++) {
        if (container_pool_free & (1 << i)) {
            container_pool_free &= ~(1 << i);  // Mark as allocated
            container_allocs++;
            return &container_pool_memory[i * CONTAINER_OBJECT_SIZE];
        }
    }
    return NULL;  // Pool exhausted
}

/**
 * @brief Free from label pool
 */
static bool pool_free_label(void* ptr) {
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t base = (uintptr_t)label_pool_memory;
    if (addr >= base && addr < base + sizeof(label_pool_memory)) {
        uint32_t index = (addr - base) / LABEL_OBJECT_SIZE;
        if (index < LABEL_POOL_SIZE) {
            label_pool_free |= (1 << index);  // Mark as free
            return true;
        }
    }
    return false;
}

/**
 * @brief Free from button pool
 */
static bool pool_free_button(void* ptr) {
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t base = (uintptr_t)button_pool_memory;
    if (addr >= base && addr < base + sizeof(button_pool_memory)) {
        uint32_t index = (addr - base) / BUTTON_OBJECT_SIZE;
        if (index < BUTTON_POOL_SIZE) {
            button_pool_free |= (1 << index);  // Mark as free
            return true;
        }
    }
    return false;
}

/**
 * @brief Free from container pool
 */
static bool pool_free_container(void* ptr) {
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t base = (uintptr_t)container_pool_memory;
    if (addr >= base && addr < base + sizeof(container_pool_memory)) {
        uint32_t index = (addr - base) / CONTAINER_OBJECT_SIZE;
        if (index < CONTAINER_POOL_SIZE) {
            container_pool_free |= (1 << index);  // Mark as free
            return true;
        }
    }
    return false;
}

/**
 * @brief Custom allocator for LVGL
 */
void* lv_mem_pool_alloc(size_t size) {
    if (pool_mutex != NULL) {
        xSemaphoreTake(pool_mutex, portMAX_DELAY);
    }

    void* ptr = NULL;

    // Try to allocate from appropriate pool based on size
    if (size <= LABEL_OBJECT_SIZE) {
        ptr = pool_alloc_label();
    } else if (size <= CONTAINER_OBJECT_SIZE) {
        ptr = pool_alloc_container();
    } else if (size <= BUTTON_OBJECT_SIZE) {
        ptr = pool_alloc_button();
    }

    // Fallback to heap if pools exhausted or size too large
    if (ptr == NULL) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT);
        if (ptr != NULL) {
            heap_allocs++;
        } else {
            failed_allocs++;
            ESP_LOGW(TAG, "Allocation failed for %d bytes (heap allocs: %lu, failed: %lu)",
                     size, heap_allocs, failed_allocs);
        }
    }

    if (pool_mutex != NULL) {
        xSemaphoreGive(pool_mutex);
    }

    return ptr;
}

/**
 * @brief Custom free for LVGL
 */
void lv_mem_pool_free(void* ptr) {
    if (ptr == NULL) return;

    if (pool_mutex != NULL) {
        xSemaphoreTake(pool_mutex, portMAX_DELAY);
    }

    // Try to free from pools first
    if (!pool_free_label(ptr) &&
        !pool_free_button(ptr) &&
        !pool_free_container(ptr)) {
        // Not from pools, free from heap
        heap_caps_free(ptr);
    }

    if (pool_mutex != NULL) {
        xSemaphoreGive(pool_mutex);
    }
}

/**
 * @brief Custom realloc for LVGL
 */
void* lv_mem_pool_realloc(void* ptr, size_t new_size) {
    if (ptr == NULL) {
        return lv_mem_pool_alloc(new_size);
    }

    if (new_size == 0) {
        lv_mem_pool_free(ptr);
        return NULL;
    }

    // Check if ptr is from pools - if so, we can't realloc in place
    // Must allocate new and copy
    uintptr_t addr = (uintptr_t)ptr;
    bool is_from_pool = false;
    size_t old_size = 0;

    if (addr >= (uintptr_t)label_pool_memory &&
        addr < (uintptr_t)label_pool_memory + sizeof(label_pool_memory)) {
        is_from_pool = true;
        old_size = LABEL_OBJECT_SIZE;
    } else if (addr >= (uintptr_t)button_pool_memory &&
               addr < (uintptr_t)button_pool_memory + sizeof(button_pool_memory)) {
        is_from_pool = true;
        old_size = BUTTON_OBJECT_SIZE;
    } else if (addr >= (uintptr_t)container_pool_memory &&
               addr < (uintptr_t)container_pool_memory + sizeof(container_pool_memory)) {
        is_from_pool = true;
        old_size = CONTAINER_OBJECT_SIZE;
    }

    if (is_from_pool) {
        // Allocate new memory
        void* new_ptr = lv_mem_pool_alloc(new_size);
        if (new_ptr != NULL) {
            // Copy old data
            memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
            // Free old memory
            lv_mem_pool_free(ptr);
        }
        return new_ptr;
    } else {
        // Heap allocation - use standard realloc
        void* new_ptr = heap_caps_realloc(ptr, new_size, MALLOC_CAP_8BIT);
        if (new_ptr == NULL && new_size > 0) {
            failed_allocs++;
        }
        return new_ptr;
    }
}

/**
 * @brief Get pool statistics
 */
void lv_mem_pool_stats(void) {
    if (pool_mutex != NULL) {
        xSemaphoreTake(pool_mutex, portMAX_DELAY);
    }

    uint32_t label_used = LABEL_POOL_SIZE - __builtin_popcount(label_pool_free);
    uint32_t button_used = BUTTON_POOL_SIZE - __builtin_popcount(button_pool_free);
    uint32_t container_used = CONTAINER_POOL_SIZE - __builtin_popcount(container_pool_free);

    ESP_LOGI(TAG, "Pool Statistics:");
    ESP_LOGI(TAG, "  Labels: %lu/%d used (%d%% utilization, %lu total allocs)",
             label_used, LABEL_POOL_SIZE, (int)((label_used * 100) / LABEL_POOL_SIZE), label_allocs);
    ESP_LOGI(TAG, "  Buttons: %lu/%d used (%d%% utilization, %lu total allocs)",
             button_used, BUTTON_POOL_SIZE, (int)((button_used * 100) / BUTTON_POOL_SIZE), button_allocs);
    ESP_LOGI(TAG, "  Containers: %lu/%d used (%d%% utilization, %lu total allocs)",
             container_used, CONTAINER_POOL_SIZE, (int)((container_used * 100) / CONTAINER_POOL_SIZE), container_allocs);
    ESP_LOGI(TAG, "  Heap fallback allocs: %lu", heap_allocs);
    ESP_LOGI(TAG, "  Failed allocations: %lu", failed_allocs);

    if (pool_mutex != NULL) {
        xSemaphoreGive(pool_mutex);
    }
}
