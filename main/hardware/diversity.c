/**
 * @file diversity.c
 * @brief Advanced diversity switching algorithm implementation
 * @version 1.8.0
 */

#include "diversity.h"
#include "rx5808.h"
#include "hwvers.h"
#include "beep.h"
#include "led.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <string.h>

static const char* TAG = "diversity";

// Mode parameter definitions
const diversity_mode_params_t diversity_mode_params[DIVERSITY_MODE_COUNT] = {
    // RACE mode - aggressive, minimal lag
    {
        .dwell_ms = 80,
        .cooldown_ms = 150,
        .hysteresis_pct = 2,
        .weight_rssi = 0.85f,
        .weight_stability = 0.15f,
        // Point 2: now ADC units/second. -200/s ≈ full-range drop in ~20 s → very reactive
        .slope_threshold = -200,
        .name = "Race"
    },
    // FREESTYLE mode - balanced
    {
        .dwell_ms = 250,
        .cooldown_ms = 500,
        .hysteresis_pct = 4,
        .weight_rssi = 0.70f,
        .weight_stability = 0.30f,
        .slope_threshold = -100, // -100/s ≈ full drop in ~40 s
        .name = "Freestyle"
    },
    // LONG_RANGE mode - stable, minimize switching
    {
        .dwell_ms = 400,
        .cooldown_ms = 800,
        .hysteresis_pct = 6,
        .weight_rssi = 0.75f,
        .weight_stability = 0.25f,
        .slope_threshold = -50,  // -50/s ≈ full drop in ~80 s → only obvious trend
        .name = "Long Range"
    }
};

// Global diversity state
static diversity_state_t g_diversity_state = {0};
static bool g_diversity_initialized = false;

// NVS storage keys
#define NVS_NAMESPACE "diversity"
#define NVS_KEY_CAL_A_FLOOR "cal_a_floor"
#define NVS_KEY_CAL_A_PEAK "cal_a_peak"
#define NVS_KEY_CAL_B_FLOOR "cal_b_floor"
#define NVS_KEY_CAL_B_PEAK "cal_b_peak"
#define NVS_KEY_MODE "div_mode"

// Switch rate tracking (for switches per minute)
#define SWITCH_HISTORY_SIZE 60
static uint32_t g_switch_timestamps[SWITCH_HISTORY_SIZE] = {0};
static uint8_t g_switch_history_index = 0;

/**
 * @brief FreeRTOS task function that drives the diversity update loop (fix N).
 * Forward-declared here; defined after diversity_init().
 */
static void diversity_task_fn(void *param);

/**
 * @brief Initialize diversity system
 */
void diversity_init(void) {
    ESP_LOGI(TAG, "Initializing diversity system v1.8.0");

    memset(&g_diversity_state, 0, sizeof(diversity_state_t));

    // Set defaults
    g_diversity_state.mode      = DIVERSITY_MODE_FREESTYLE;
    g_diversity_state.active_rx = DIVERSITY_RX_A;

    // Point 7: AGC baseline starts at 50 (midpoint of 0-100 range) so both
    // receivers start with zero correction and converge naturally toward their
    // true long-term mean over the first ~10 s of stable flight.
    g_diversity_state.rx_a.agc_baseline = 50.0f;
    g_diversity_state.rx_b.agc_baseline = 50.0f;

    // Initialize calibration defaults (uncalibrated)
    g_diversity_state.cal_a.floor_raw  = 0;
    g_diversity_state.cal_a.peak_raw   = 4095;
    g_diversity_state.cal_a.calibrated = false;

    g_diversity_state.cal_b.floor_raw  = 0;
    g_diversity_state.cal_b.peak_raw   = 4095;
    g_diversity_state.cal_b.calibrated = false;

    // Load calibration from NVS
    diversity_calibrate_load();

    g_diversity_initialized = true;

    ESP_LOGI(TAG, "Diversity initialized - Mode: %s",
             diversity_mode_params[g_diversity_state.mode].name);

    // Spawn the diversity update task on Core 1 (fix N + J).
    //
    // Running diversity_update() in its own FreeRTOS task instead of inside
    // the LVGL timer callback ensures that RX5808_Set_Freq() (which blocks
    // ~50 ms during PLL settling) never stalls the LVGL rendering loop.
    // It also guarantees diversity runs regardless of which GUI page is open.
    //
    // The task calls diversity_update() in a tight 5 ms loop; the function
    // itself is internally rate-limited (10 ms at 100 Hz or 50 ms at 20 Hz),
    // so excess calls return immediately at negligible cost.
    xTaskCreatePinnedToCore(
        diversity_task_fn,  // forward-declared below
        "diversity",
        4096,
        NULL,
        6,      // priority 6 — above LVGL (5), keeps RF switching responsive
        NULL,
        1       // Core 1 — alongside the RSSI and beep tasks
    );
}

/**
 * @brief FreeRTOS task that drives the diversity update loop (fix N).
 *
 * Called at 200 Hz nominal; diversity_update() self-rate-limits to 100 Hz or
 * 20 Hz depending on flight activity, so extra calls simply return early.
 * Pinned to Core 1 so any blocking RX5808_Set_Freq() call never touches Core 0
 * (LVGL).
 */
static void diversity_task_fn(void *param)
{
    (void)param;
    while (1) {
        diversity_update();
        // Delay at least 1 full tick so IDLE1 gets CPU time.
        // pdMS_TO_TICKS(5) rounds to 0 on a 100 Hz tick config, making
        // vTaskDelay(0) a no-op that starves the idle task and triggers
        // the task watchdog.  vTaskDelay(1) always blocks for 1 tick
        // (~10 ms on 100 Hz), well within the 100 Hz / 20 Hz rate-limit
        // that diversity_update() enforces internally.
        vTaskDelay(1);
    }
}

/**
 * @brief Set diversity mode
 */
void diversity_set_mode(diversity_mode_t mode) {
    if (mode >= DIVERSITY_MODE_COUNT) {
        ESP_LOGW(TAG, "Invalid mode %d", mode);
        return;
    }

    g_diversity_state.mode = mode;

    // Save to NVS
    nvs_handle_t nvs_handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_u8(nvs_handle, NVS_KEY_MODE, (uint8_t)mode);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }

    ESP_LOGI(TAG, "Mode changed to: %s", diversity_mode_params[mode].name);
}

/**
 * @brief Normalize raw RSSI to 0-100 scale using calibration
 */
uint8_t diversity_normalize_rssi(uint16_t raw, rssi_calibration_t* cal) {
    // Use uncalibrated mode if calibration not set or invalid (floor >= peak)
    if (!cal->calibrated || cal->floor_raw >= cal->peak_raw) {
        // Fallback to simple 12-bit to 0-100 mapping
        return (uint8_t)((raw * 100) / 4095);
    }

    // Clamp to calibrated range
    if (raw <= cal->floor_raw) return 0;
    if (raw >= cal->peak_raw) return 100;

    // Linear interpolation
    uint32_t range = cal->peak_raw - cal->floor_raw;
    uint32_t value = raw - cal->floor_raw;

    return (uint8_t)((value * 100) / range);
}

/**
 * @brief Calculate statistics (mean, variance, slope) over rolling window
 *
 * @param rx          Per-receiver state to update
 * @param interval_ms Actual time elapsed since the previous sample (ms).
 *                    Used to express slope in ADC units/second so that the
 *                    threshold comparison in diversity_should_switch() is
 *                    rate-invariant (same meaning at 20 Hz and 100 Hz).
 */
static void diversity_calculate_statistics(diversity_rx_state_t* rx, uint32_t interval_ms) {
    if (rx->sample_count == 0) {
        return;
    }

    // Calculate mean
    uint32_t sum = 0;
    for (uint8_t i = 0; i < rx->sample_count; i++) {
        sum += rx->rssi_samples[i];
    }
    rx->rssi_mean = sum / rx->sample_count;

    // Calculate variance
    uint32_t variance_sum = 0;
    for (uint8_t i = 0; i < rx->sample_count; i++) {
        int32_t diff = (int32_t)rx->rssi_samples[i] - (int32_t)rx->rssi_mean;
        variance_sum += (diff * diff);
    }
    rx->rssi_variance = (uint16_t)(variance_sum / rx->sample_count);

    // Point 2: time-normalised slope in ADC units/second.
    // half_window_ms = number of samples in half the window × interval per sample.
    // slope = Δrssi_mean / elapsed_time → same numeric meaning regardless of
    // whether diversity_update() is running at 20 Hz or 100 Hz.
    if (rx->sample_count >= 10) {
        uint8_t half_window = rx->sample_count / 2;
        uint8_t old_index = (rx->sample_index + DIVERSITY_MAX_SAMPLES - half_window) % DIVERSITY_MAX_SAMPLES;

        uint32_t old_sum = 0;
        for (uint8_t i = 0; i < half_window; i++) {
            uint8_t idx = (old_index + i) % DIVERSITY_MAX_SAMPLES;
            old_sum += rx->rssi_samples[idx];
        }
        uint16_t old_mean = (uint16_t)(old_sum / half_window);

        uint32_t half_window_ms = (uint32_t)half_window * interval_ms;
        if (half_window_ms == 0) half_window_ms = 1; // guard against div/0 on first tick
        // Multiply delta by 1000 to convert ms → seconds
        rx->rssi_slope = (int16_t)(((int32_t)rx->rssi_mean - (int32_t)old_mean) * 1000
                                   / (int32_t)half_window_ms);
    } else {
        rx->rssi_slope = 0;
    }
}

/**
 * @brief Integer square-root (Newton's method).
 *        Replaces sqrtf() in the hot scoring path — no FPU division, no libm.
 *        Converges in ≤8 iterations for any uint32_t input.
 */
static uint32_t diversity_isqrt(uint32_t n) {
    if (n == 0) return 0;
    uint32_t x = n;
    uint32_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

/**
 * @brief Calculate stability score and combined score
 */
void diversity_calculate_scores(diversity_rx_state_t* rx, const diversity_mode_params_t* params) {
    // Stability score: 100 - penalties for variance and slope
    float k_variance = 0.2f;
    // Point 2: slope is now in ADC units/second (range ~0-2000).
    // k_slope scaled so max penalty stays similar to before (~75 pts).
    float k_slope = 0.038f;

    // Integer sqrt/abs replace sqrtf()/fabsf() — avoids libm on the hot path
    float variance_penalty = k_variance * (float)diversity_isqrt((uint32_t)rx->rssi_variance);
    int16_t slope_abs      = (rx->rssi_slope < 0) ? (int16_t)(-rx->rssi_slope) : rx->rssi_slope;
    float slope_penalty    = k_slope    * (float)slope_abs;

    int16_t stability = 100 - (int16_t)(variance_penalty + slope_penalty);
    if (stability < 0)   stability = 0;
    if (stability > 100) stability = 100;
    rx->stability_score = (uint8_t)stability;

    // Point 7: use AGC-corrected RSSI in scoring so hardware-offset receivers
    // are compared on a level field.  rssi_agc is populated in diversity_update()
    // before this function is called; it equals rssi_norm during AGC warmup.
    float rssi_for_score = (float)rx->rssi_agc;

    float score = (params->weight_rssi     * rssi_for_score) +
                  (params->weight_stability * (float)rx->stability_score);
    if (score > 100.0f) score = 100.0f;
    if (score <   0.0f) score =   0.0f;
    rx->combined_score = (uint8_t)score;
}

/**
 * @brief Check receiver health for stuck/failed conditions
 */
void diversity_check_receiver_health(diversity_rx_state_t* rx, uint32_t now) {
    // Check every 1 second
    if (now - rx->health_check_ms < 1000) {
        return;
    }
    rx->health_check_ms = now;

    // Reset flags initially
    rx->health.stuck_high = false;
    rx->health.stuck_low = false;
    rx->health.no_variance = false;

    // Stuck high: RSSI > 95 for >3s with zero variance
    if (rx->rssi_norm > 95 && rx->rssi_variance < 5) {
        rx->health.stuck_high = true;
        ESP_LOGW(TAG, "Receiver health: stuck high detected");
    }

    // Stuck low: RSSI < 5 for >3s
    if (rx->rssi_norm < 5) {
        rx->health.stuck_low = true;
        ESP_LOGW(TAG, "Receiver health: stuck low detected");
    }

    // No variance: variance near zero for >5s across multiple samples
    if (rx->sample_count > 20 && rx->rssi_variance < 2) {
        rx->health.no_variance = true;
        ESP_LOGW(TAG, "Receiver health: no variance detected");
    }
}

/**
 * @brief Determine if conditions are right to switch receivers
 */
bool diversity_should_switch(diversity_state_t* state, const diversity_mode_params_t* params, uint32_t now) {
    // Get pointers to active and other receiver
    diversity_rx_state_t* active = (state->active_rx == DIVERSITY_RX_A) ? &state->rx_a : &state->rx_b;
    diversity_rx_state_t* other = (state->active_rx == DIVERSITY_RX_A) ? &state->rx_b : &state->rx_a;

    // Don't switch if other receiver is unhealthy
    if (other->health.disabled || other->health.stuck_low || other->health.no_variance) {
        return false;
    }

    // If active receiver is unhealthy, switch immediately
    if (active->health.disabled || active->health.stuck_low) {
        ESP_LOGI(TAG, "Switching due to active receiver health issue");
        return true;
    }

    // Check dwell time
    uint32_t time_since_switch = now - state->last_switch_ms;
    uint32_t required_dwell = state->in_cooldown ? params->cooldown_ms : params->dwell_ms;

    if (time_since_switch < required_dwell) {
        return false; // Still in dwell/cooldown period
    }

    // Apply hysteresis
    uint8_t hysteresis = params->hysteresis_pct;
    if (state->in_cooldown) {
        hysteresis *= 2; // Double hysteresis during cooldown
    }

    // Switch only if other receiver is significantly better
    if (other->combined_score > (active->combined_score + hysteresis)) {
        ESP_LOGI(TAG, "Switch: other=%d active=%d (delta=%d, threshold=%d)",
                 other->combined_score, active->combined_score,
                 other->combined_score - active->combined_score, hysteresis);
        return true;
    }

    // Preemptive switching: if active receiver is dropping fast
    if (active->rssi_slope < params->slope_threshold &&
        other->combined_score > active->combined_score) {
        ESP_LOGI(TAG, "Preemptive switch: slope=%d (threshold=%d)",
                 active->rssi_slope, params->slope_threshold);
        return true;
    }

    return false;
}

/**
 * @brief Perform receiver switch
 *
 * Point 1: glitch-free GPIO sequence.
 * Old sequence (A→B): SWITCH0=1 (both=1!), then SWITCH1=0
 * New sequence:       both=0 first (~150 ns gap, invisible at 50 Hz field rate),
 *                     then assert the new selection.
 *
 * Marked IRAM_ATTR so the function always resides in IRAM and can be called
 * from a future ISR context without a cache-miss stall.
 */
static IRAM_ATTR void diversity_perform_switch(diversity_state_t* state) {
    uint32_t now = esp_timer_get_time() / 1000; // ms

    // Toggle active receiver
    state->active_rx = (state->active_rx == DIVERSITY_RX_A) ? DIVERSITY_RX_B : DIVERSITY_RX_A;

    // Update switch tracking
    state->last_switch_ms = now;
    state->switch_count++;
    state->time_stable_ms = 0;
    state->in_cooldown = true;

    // Record switch timestamp for rate calculation
    g_switch_timestamps[g_switch_history_index] = now;
    g_switch_history_index = (g_switch_history_index + 1) % SWITCH_HISTORY_SIZE;

    // Point 1: de-assert both lines first, then assert the chosen side.
    // This eliminates the brief "both active" glitch of the old direct assignment.
    gpio_set_level(RX5808_SWITCH0, 0);
    gpio_set_level(RX5808_SWITCH1, 0);
    if (state->active_rx == DIVERSITY_RX_A) {
        gpio_set_level(RX5808_SWITCH1, 1); // RX_A: SWITCH1=1, SWITCH0=0
    } else {
        gpio_set_level(RX5808_SWITCH0, 1); // RX_B: SWITCH0=1, SWITCH1=0
    }

    // Point 9: record state for outcome evaluation 200 ms from now
    const diversity_rx_state_t* new_rx = (state->active_rx == DIVERSITY_RX_A)
                                         ? &state->rx_a : &state->rx_b;
    state->outcome_check_ms       = now + 200;
    state->outcome_new_rx         = state->active_rx;
    state->outcome_rssi_at_switch = new_rx->rssi_norm;

    ESP_LOGI(TAG, "Switched to RX_%c (switches=%lu)",
             state->active_rx == DIVERSITY_RX_A ? 'A' : 'B',
             state->switch_count);
}

// ============================================================================
// Point 8: Interference rejection via micro-frequency offset
// When the active receiver RSSI is weak AND declining, the system trials a
// ±1 MHz shift on both chips.  If the shift improves RSSI by at least
// FREQ_SHIFT_IMPROVE_MIN within FREQ_SHIFT_EVAL_MS it is held for
// FREQ_SHIFT_HOLD_MS seconds, after which the nominal frequency is restored.
// Both directions are tried before giving up (cooldown FREQ_SHIFT_COOLDOWN_MS).
// RX5808_Set_Freq() programs both RX chips simultaneously and already blocks
// ~50 ms for PLL settling, so no extra delay is required here.
// ============================================================================
#define FREQ_SHIFT_TRIGGER_RSSI    22   // Trigger only when active rssi_norm < this
#define FREQ_SHIFT_TRIGGER_SLOPE (-80)  // Trigger only when rssi_slope < this (ADC-units/s)
#define FREQ_SHIFT_DWELL_MIN_MS   1500  // Must be on current receiver for this long before shifting
#define FREQ_SHIFT_EVAL_MS         300  // Evidence-collection window after the shift (ms)
#define FREQ_SHIFT_HOLD_MS       30000  // Hold a successful shift for 30 s
#define FREQ_SHIFT_COOLDOWN_MS   30000  // Minimum gap between shift attempts (ms)
#define FREQ_SHIFT_IMPROVE_MIN       5  // rssi_norm must improve by at least this to accept

/**
 * @brief Point 8 FSM: non-blocking micro-frequency-offset interference rejector.
 *
 * Must be called every diversity update cycle *after* statistics and scoring
 * have been refreshed.  The function may call RX5808_Set_Freq() which blocks
 * ~50 ms (PLL settle), mirroring the latency already accepted during normal
 * channel changes.
 *
 * Guard conditions prevent activation during calibration, switch cooldown,
 * or when the user has changed band/channel externally.
 */
static void diversity_freq_shift_update(diversity_state_t* state, uint32_t now)
{
    const diversity_rx_state_t* arx = (state->active_rx == DIVERSITY_RX_A)
                                      ? &state->rx_a : &state->rx_b;

    /* ----------------------------------------------------------------
     * External channel-change detection.
     * RX5808_Get_Current_Freq() reads from the band-table (the nominal
     * channel the user selected), never from expected_frequency.  So if
     * the user changed channel while we held a shift, the nominal will
     * differ from freq_shift_nominal.  Silently reset; RX5808_Set_Freq
     * was already called externally with the new nominal, so PLL is OK.
     * ---------------------------------------------------------------- */
    if (state->freq_shift_state != FREQ_SHIFT_IDLE) {
        uint16_t current_nominal = RX5808_Get_Current_Freq();
        if (current_nominal != state->freq_shift_nominal) {
            ESP_LOGI(TAG, "Point8: channel changed externally (%d→%d), resetting FSM",
                     state->freq_shift_nominal, current_nominal);
            state->freq_shift_state  = FREQ_SHIFT_IDLE;
            state->freq_shift_offset = 0;
            // No cooldown needed — this was a user-initiated change.
            return;
        }
    }

    switch (state->freq_shift_state) {

    /* -------------------------------------------------------------- */
    case FREQ_SHIFT_IDLE:
        // Gate conditions
        if (now < state->freq_shift_cooldown_ms)                   break;
        if (state->in_cooldown)                                     break;
        if (state->time_stable_ms < FREQ_SHIFT_DWELL_MIN_MS)       break;
        if (arx->rssi_norm  >= FREQ_SHIFT_TRIGGER_RSSI)            break;
        if (arx->rssi_slope >= FREQ_SHIFT_TRIGGER_SLOPE)           break;

        // Snapshot nominal and baseline RSSI
        state->freq_shift_nominal  = RX5808_Get_Current_Freq();
        state->freq_shift_baseline = arx->rssi_norm;
        state->freq_shift_offset   = +1;
        ESP_LOGI(TAG, "Point8: rssi=%d slope=%d → trying +1 MHz (%d→%d)",
                 arx->rssi_norm, arx->rssi_slope,
                 state->freq_shift_nominal, state->freq_shift_nominal + 1);
        RX5808_Set_Freq((uint16_t)(state->freq_shift_nominal + 1));
        state->freq_shift_eval_end_ms = now + FREQ_SHIFT_EVAL_MS;
        state->freq_shift_state       = FREQ_SHIFT_EVAL_PLUS;
        break;

    /* -------------------------------------------------------------- */
    case FREQ_SHIFT_EVAL_PLUS:
        if (now < state->freq_shift_eval_end_ms) break;

        if ((int16_t)arx->rssi_norm >=
            (int16_t)state->freq_shift_baseline + FREQ_SHIFT_IMPROVE_MIN) {
            // +1 MHz improved reception — hold it
            ESP_LOGI(TAG, "Point8: +1 MHz accepted (rssi %d→%d), holding %d s",
                     state->freq_shift_baseline, arx->rssi_norm,
                     FREQ_SHIFT_HOLD_MS / 1000);
            state->freq_shift_hold_end_ms = now + FREQ_SHIFT_HOLD_MS;
            state->freq_shift_state       = FREQ_SHIFT_HOLD;
        } else {
            // +1 didn't help — try -1 MHz
            // Hardware is currently at nominal+1; target is nominal-1 = (nominal+1) - 2
            state->freq_shift_offset = -1;
            ESP_LOGI(TAG, "Point8: +1 no help → trying -1 MHz (%d)",
                     state->freq_shift_nominal - 1);
            RX5808_Set_Freq((uint16_t)(state->freq_shift_nominal - 1));
            state->freq_shift_eval_end_ms = now + FREQ_SHIFT_EVAL_MS;
            state->freq_shift_state       = FREQ_SHIFT_EVAL_MINUS;
        }
        break;

    /* -------------------------------------------------------------- */
    case FREQ_SHIFT_EVAL_MINUS:
        if (now < state->freq_shift_eval_end_ms) break;

        if ((int16_t)arx->rssi_norm >=
            (int16_t)state->freq_shift_baseline + FREQ_SHIFT_IMPROVE_MIN) {
            // -1 MHz improved reception — hold it
            ESP_LOGI(TAG, "Point8: -1 MHz accepted (rssi %d→%d), holding %d s",
                     state->freq_shift_baseline, arx->rssi_norm,
                     FREQ_SHIFT_HOLD_MS / 1000);
            state->freq_shift_hold_end_ms = now + FREQ_SHIFT_HOLD_MS;
            state->freq_shift_state       = FREQ_SHIFT_HOLD;
        } else {
            // Neither ±1 MHz helped — revert to nominal and cool down
            ESP_LOGI(TAG, "Point8: neither offset helped → reverting to %d MHz",
                     state->freq_shift_nominal);
            RX5808_Set_Freq(state->freq_shift_nominal);
            state->freq_shift_offset      = 0;
            state->freq_shift_cooldown_ms = now + FREQ_SHIFT_COOLDOWN_MS;
            state->freq_shift_state       = FREQ_SHIFT_IDLE;
        }
        break;

    /* -------------------------------------------------------------- */
    case FREQ_SHIFT_HOLD:
        if (now >= state->freq_shift_hold_end_ms) {
            // Hold period expired — quietly revert to nominal
            ESP_LOGI(TAG, "Point8: hold expired → reverting to %d MHz",
                     state->freq_shift_nominal);
            RX5808_Set_Freq(state->freq_shift_nominal);
            state->freq_shift_offset      = 0;
            // Short cooldown after natural expiry — allows re-trial sooner
            state->freq_shift_cooldown_ms = now + (FREQ_SHIFT_COOLDOWN_MS / 3);
            state->freq_shift_state       = FREQ_SHIFT_IDLE;
        }
        break;

    default:
        state->freq_shift_state = FREQ_SHIFT_IDLE;
        break;
    }
}

/**
 * @brief Main diversity update function - call at DIVERSITY_SAMPLE_RATE_HZ
 * Implements adaptive sampling: 100Hz (10ms) during active switching, 20Hz (50ms) when stable
 */
void diversity_update(void) {
    if (!g_diversity_initialized) {
        return;
    }

    uint32_t now = esp_timer_get_time() / 1000; // ms
    diversity_state_t* state = &g_diversity_state;
    const diversity_mode_params_t* params = &diversity_mode_params[state->mode];

    // Calculate switches per second over last 5 seconds
    uint32_t window_start = now - 5000; // 5 second window
    uint32_t recent_switches = 0;
    for (uint8_t i = 0; i < SWITCH_HISTORY_SIZE; i++) {
        if (g_switch_timestamps[i] >= window_start && g_switch_timestamps[i] <= now) {
            recent_switches++;
        }
    }
    state->switches_per_second = recent_switches / 5.0f;

    // Adaptive sampling rate: switch between 20Hz (50ms) and 100Hz (10ms)
    // High rate (100Hz): switches_per_second >= 2 OR time_stable < 3s
    // Low rate (20Hz):  switches_per_second < 2 AND time_stable >= 3s
    uint32_t time_since_last_sample = now - state->last_sample_ms;

    if (state->switches_per_second >= 2.0f || state->time_stable_ms < 3000) {
        // High activity mode: 100Hz sampling (10ms intervals)
        state->adaptive_high_rate = true;
        if (time_since_last_sample < 10) {
            return; // Skip this call, not enough time elapsed
        }
    } else {
        // Low activity mode: 20Hz sampling (50ms intervals)
        state->adaptive_high_rate = false;
        if (time_since_last_sample < 50) {
            return; // Skip this call, not enough time elapsed
        }
    }

    // Update last sample timestamp
    state->last_sample_ms = now;

    // Sample raw RSSI from both receivers via ADC
    // adc_converted_value[] is populated by the ADC DMA handler in rx5808.c
    state->rx_a.rssi_raw = adc_converted_value[0];
    state->rx_b.rssi_raw = adc_converted_value[1];

    // Normalize RSSI
    state->rx_a.rssi_norm = diversity_normalize_rssi(state->rx_a.rssi_raw, &state->cal_a);
    state->rx_b.rssi_norm = diversity_normalize_rssi(state->rx_b.rssi_raw, &state->cal_b);

    // Keep LED signal-strength value in sync with the active receiver
    {
        uint8_t active_rssi = (uint8_t)((state->active_rx == DIVERSITY_RX_A)
                                         ? state->rx_a.rssi_norm
                                         : state->rx_b.rssi_norm);
        led_set_signal_strength(active_rssi);
    }

    // --- Point 7: software AGC ---
    // Only update the baseline during stable flight (after 2 s without a switch)
    // to avoid contaminating it with mid-manoeuvre swings.
    if (state->time_stable_ms > 2000) {
        // Slow EMA: α=0.001 → converges to true mean in ~1000 samples (~10 s @100 Hz)
        state->rx_a.agc_baseline = state->rx_a.agc_baseline * 0.999f
                                 + (float)state->rx_a.rssi_norm * 0.001f;
        state->rx_b.agc_baseline = state->rx_b.agc_baseline * 0.999f
                                 + (float)state->rx_b.rssi_norm * 0.001f;
        if (state->rx_a.agc_sample_count < 60000) state->rx_a.agc_sample_count++;
        if (state->rx_b.agc_sample_count < 60000) state->rx_b.agc_sample_count++;
    }
    // AGC-corrected RSSI: shift each receiver so its long-term mean sits at 50%.
    // Capped at ±15 to prevent over-correction on hardware with large offsets.
    // Correction activates after 100 stable samples (~warmup 1 s @100 Hz).
    if (state->rx_a.agc_sample_count >= 100) {
        int16_t corr_a = (int16_t)(50.0f - state->rx_a.agc_baseline);
        if (corr_a >  15) corr_a =  15;
        if (corr_a < -15) corr_a = -15;
        int16_t agc_a = (int16_t)state->rx_a.rssi_norm + corr_a;
        state->rx_a.rssi_agc = (agc_a < 0) ? 0 : (agc_a > 100) ? 100 : (int16_t)agc_a;
    } else {
        state->rx_a.rssi_agc = (int16_t)state->rx_a.rssi_norm;
    }
    if (state->rx_b.agc_sample_count >= 100) {
        int16_t corr_b = (int16_t)(50.0f - state->rx_b.agc_baseline);
        if (corr_b >  15) corr_b =  15;
        if (corr_b < -15) corr_b = -15;
        int16_t agc_b = (int16_t)state->rx_b.rssi_norm + corr_b;
        state->rx_b.rssi_agc = (agc_b < 0) ? 0 : (agc_b > 100) ? 100 : (int16_t)agc_b;
    } else {
        state->rx_b.rssi_agc = (int16_t)state->rx_b.rssi_norm;
    }

    // Store samples in rolling windows
    state->rx_a.rssi_samples[state->rx_a.sample_index] = state->rx_a.rssi_raw;
    state->rx_a.sample_index = (state->rx_a.sample_index + 1) % DIVERSITY_MAX_SAMPLES;
    if (state->rx_a.sample_count < DIVERSITY_MAX_SAMPLES) state->rx_a.sample_count++;

    state->rx_b.rssi_samples[state->rx_b.sample_index] = state->rx_b.rssi_raw;
    state->rx_b.sample_index = (state->rx_b.sample_index + 1) % DIVERSITY_MAX_SAMPLES;
    if (state->rx_b.sample_count < DIVERSITY_MAX_SAMPLES) state->rx_b.sample_count++;

    // Calculate statistics — pass actual interval so slope is time-normalised (point 2)
    diversity_calculate_statistics(&state->rx_a, time_since_last_sample);
    diversity_calculate_statistics(&state->rx_b, time_since_last_sample);

    // Calculate scores
    diversity_calculate_scores(&state->rx_a, params);
    diversity_calculate_scores(&state->rx_b, params);

    // --- Point 9: apply / expire preference bonuses ---
    // Outcome check: 200 ms after a switch see if the new receiver actually improved.
    if (state->outcome_check_ms != 0 && now >= state->outcome_check_ms) {
        const diversity_rx_state_t* new_rx = (state->outcome_new_rx == DIVERSITY_RX_A)
                                              ? &state->rx_a : &state->rx_b;
        // "Improved" = rssi_norm at least 5% higher than it was at the switch moment
        if ((int16_t)new_rx->rssi_norm > (int16_t)state->outcome_rssi_at_switch + 5) {
            if (state->outcome_new_rx == DIVERSITY_RX_A) {
                state->rx_a_pref_bonus        = 8.0f;
                state->rx_a_bonus_expires_ms  = now + 5000;
            } else {
                state->rx_b_pref_bonus        = 8.0f;
                state->rx_b_bonus_expires_ms  = now + 5000;
            }
            ESP_LOGI(TAG, "RX_%c confirmed good — preference bonus applied",
                     state->outcome_new_rx == DIVERSITY_RX_A ? 'A' : 'B');
        }
        state->outcome_check_ms = 0;
    }
    // Expire bonuses
    if (state->rx_a_bonus_expires_ms && now >= state->rx_a_bonus_expires_ms) {
        state->rx_a_pref_bonus = 0.0f; state->rx_a_bonus_expires_ms = 0;
    }
    if (state->rx_b_bonus_expires_ms && now >= state->rx_b_bonus_expires_ms) {
        state->rx_b_pref_bonus = 0.0f; state->rx_b_bonus_expires_ms = 0;
    }
    // Apply preference bonus to combined score (capped at 100)
    if (state->rx_a_pref_bonus > 0.0f) {
        uint16_t s = (uint16_t)state->rx_a.combined_score + (uint16_t)state->rx_a_pref_bonus;
        state->rx_a.combined_score = (s > 100) ? 100 : (uint8_t)s;
    }
    if (state->rx_b_pref_bonus > 0.0f) {
        uint16_t s = (uint16_t)state->rx_b.combined_score + (uint16_t)state->rx_b_pref_bonus;
        state->rx_b.combined_score = (s > 100) ? 100 : (uint8_t)s;
    }

    // Check receiver health (pass current timestamp to avoid extra syscalls)
    diversity_check_receiver_health(&state->rx_a, now);
    diversity_check_receiver_health(&state->rx_b, now);

    // Point 8: interference rejection via micro-frequency offset
    // Run after scores and outcome bonuses are finalised, before the switch decision,
    // so that a successful shift is reflected in the next cycle's scores.
    diversity_freq_shift_update(state, now);

    // Update telemetry
    state->rssi_delta = (int8_t)state->rx_a.rssi_norm - (int8_t)state->rx_b.rssi_norm;
    state->time_stable_ms = now - state->last_switch_ms;

    // Update longest stable time
    if (state->time_stable_ms > state->longest_stable_ms) {
        state->longest_stable_ms = state->time_stable_ms;
    }

    // Clear cooldown flag after cooldown period
    if (state->in_cooldown && (now - state->last_switch_ms) > params->cooldown_ms) {
        state->in_cooldown = false;
    }

    // Decide whether to switch
    if (diversity_should_switch(state, params, now)) {
        diversity_perform_switch(state);
        // Visual feedback: double blink on every antenna switch.
        // Called here (task context) rather than inside the IRAM_ATTR function
        // to keep the IRAM path free of non-ISR-safe FreeRTOS queue calls.
        led_trigger_double_blink();
    }

    // --- Point E: low-signal audio alert ---
    // Fires a long beep when BOTH receivers report weak signal (rssi_norm < 15),
    // indicating the aircraft is likely out of range on all antennas.
    // Rate-limited to once every 5 s so the cockpit isn't flooded with beeps.
    // Only activates after calibration is complete so boot noise doesn't trigger it.
#define LOW_SIGNAL_THRESHOLD   15   // rssi_norm below this on both RX = "weak"
#define LOW_SIGNAL_REPEAT_MS 5000  // minimum gap between consecutive alert beeps
    if (state->cal_a.calibrated && state->cal_b.calibrated) {
        if (state->rx_a.rssi_norm < LOW_SIGNAL_THRESHOLD &&
            state->rx_b.rssi_norm < LOW_SIGNAL_THRESHOLD) {
            if (now - state->low_signal_beep_ms >= LOW_SIGNAL_REPEAT_MS) {
                state->low_signal_beep_ms = now;
                beep_play_triple(); // three rapid clicks = range warning
            }
        }
    }
}

/**
 * @brief Get current active receiver
 */
diversity_rx_t diversity_get_active_rx(void) {
    return g_diversity_state.active_rx;
}

/**
 * @brief Get pointer to diversity state (for UI/telemetry)
 */
diversity_state_t* diversity_get_state(void) {
    return &g_diversity_state;
}

/**
 * @brief Get total switch count
 */
uint32_t diversity_get_switch_count(void) {
    return g_diversity_state.switch_count;
}

/**
 * @brief Calculate switches per minute over last 60 seconds
 */
uint32_t diversity_get_switches_per_minute(void) {
    uint32_t now = esp_timer_get_time() / 1000; // ms
    uint32_t count = 0;

    // Count switches in last 60 seconds
    for (int i = 0; i < SWITCH_HISTORY_SIZE; i++) {
        if (g_switch_timestamps[i] > 0 && (now - g_switch_timestamps[i]) < 60000) {
            count++;
        }
    }

    return count;
}

/**
 * @brief Get time stable in milliseconds
 */
uint32_t diversity_get_time_stable_ms(void) {
    return g_diversity_state.time_stable_ms;
}

/**
 * @brief Reset statistics
 */
void diversity_reset_stats(void) {
    g_diversity_state.switch_count = 0;
    g_diversity_state.longest_stable_ms = 0;
    memset(g_switch_timestamps, 0, sizeof(g_switch_timestamps));
    g_switch_history_index = 0;
    ESP_LOGI(TAG, "Statistics reset");
}

// ============================================================================
// RSSI Calibration Functions
// ============================================================================

/**
 * @brief Load calibration from NVS
 */
void diversity_calibrate_load(void) {
    nvs_handle_t nvs_handle;

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle) != ESP_OK) {
        ESP_LOGW(TAG, "No calibration found in NVS");
        return;
    }

    uint16_t value;

    // Load RX A calibration
    if (nvs_get_u16(nvs_handle, NVS_KEY_CAL_A_FLOOR, &value) == ESP_OK) {
        g_diversity_state.cal_a.floor_raw = value;
    }
    if (nvs_get_u16(nvs_handle, NVS_KEY_CAL_A_PEAK, &value) == ESP_OK) {
        g_diversity_state.cal_a.peak_raw = value;
    }

    // Load RX B calibration
    if (nvs_get_u16(nvs_handle, NVS_KEY_CAL_B_FLOOR, &value) == ESP_OK) {
        g_diversity_state.cal_b.floor_raw = value;
    }
    if (nvs_get_u16(nvs_handle, NVS_KEY_CAL_B_PEAK, &value) == ESP_OK) {
        g_diversity_state.cal_b.peak_raw = value;
    }

    // Validate calibration
    if (g_diversity_state.cal_a.peak_raw > g_diversity_state.cal_a.floor_raw + 50) {
        g_diversity_state.cal_a.calibrated = true;
        ESP_LOGI(TAG, "RX A calibration loaded: floor=%d peak=%d",
                 g_diversity_state.cal_a.floor_raw, g_diversity_state.cal_a.peak_raw);
    }

    if (g_diversity_state.cal_b.peak_raw > g_diversity_state.cal_b.floor_raw + 50) {
        g_diversity_state.cal_b.calibrated = true;
        ESP_LOGI(TAG, "RX B calibration loaded: floor=%d peak=%d",
                 g_diversity_state.cal_b.floor_raw, g_diversity_state.cal_b.peak_raw);
    }

    // F: load persisted diversity mode (Race / Freestyle / Long-Range).
    // diversity_set_mode() already saves on every change; we restore it here
    // so the user's preference survives a power cycle.
    uint8_t saved_mode = 0;
    if (nvs_get_u8(nvs_handle, NVS_KEY_MODE, &saved_mode) == ESP_OK
            && saved_mode < DIVERSITY_MODE_COUNT) {
        g_diversity_state.mode = (diversity_mode_t)saved_mode;
        ESP_LOGI(TAG, "Diversity mode loaded: %s",
                 diversity_mode_params[g_diversity_state.mode].name);
    }

    nvs_close(nvs_handle);
}

/**
 * @brief Add one floor sample (call once per timer tick, non-blocking)
 *
 * Collects ADC samples incrementally so the LVGL task never blocks.
 * Call diversity_calibrate_floor_finish() after DIVERSITY_CALIB_SAMPLES ticks.
 */
void diversity_calibrate_floor_sample(diversity_rx_t rx, uint16_t* buf, int index) {
    buf[index] = (rx == DIVERSITY_RX_A) ? adc_converted_value[0] : adc_converted_value[1];
}

/**
 * @brief Compute floor result from filled sample buffer
 *
 * Algorithm: median + 8% fixed margin (robust, no MAD complexity needed
 * given ESP32 ADC noise floor is already larger than MAD precision).
 */
bool diversity_calibrate_floor_finish(uint16_t* buf, int count, uint16_t* result) {
    if (count <= 0) return false;

    // Insertion sort (fine for <=50 samples)
    for (int i = 1; i < count; i++) {
        uint16_t key = buf[i];
        int j = i - 1;
        while (j >= 0 && buf[j] > key) { buf[j + 1] = buf[j]; j--; }
        buf[j + 1] = key;
    }

    uint16_t median = buf[count / 2];
    uint16_t margin = (median * 8) / 100;
    *result = median + margin;

    ESP_LOGI(TAG, "Floor finish: median=%d margin=%d result=%d", median, margin, *result);
    return true;
}

/**
 * @brief Add one peak sample (call once per timer tick, non-blocking)
 */
void diversity_calibrate_peak_sample(diversity_rx_t rx, uint16_t* buf, int index) {
    buf[index] = (rx == DIVERSITY_RX_A) ? adc_converted_value[0] : adc_converted_value[1];
}

/**
 * @brief Compute peak result from filled sample buffer
 *
 * Uses 90th percentile — less sensitive to VTX distance variation than 95th.
 */
bool diversity_calibrate_peak_finish(uint16_t* buf, int count, uint16_t* result) {
    if (count <= 0) return false;

    // Insertion sort
    for (int i = 1; i < count; i++) {
        uint16_t key = buf[i];
        int j = i - 1;
        while (j >= 0 && buf[j] > key) { buf[j + 1] = buf[j]; j--; }
        buf[j + 1] = key;
    }

    *result = buf[(count * 90) / 100];

    ESP_LOGI(TAG, "Peak finish: 90th-pct=%d", *result);
    return true;
}

/**
 * @brief Save calibration to NVS
 */
bool diversity_calibrate_save(void) {
    nvs_handle_t nvs_handle;

    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for calibration save");
        return false;
    }

    // Save RX A
    nvs_set_u16(nvs_handle, NVS_KEY_CAL_A_FLOOR, g_diversity_state.cal_a.floor_raw);
    nvs_set_u16(nvs_handle, NVS_KEY_CAL_A_PEAK, g_diversity_state.cal_a.peak_raw);

    // Save RX B
    nvs_set_u16(nvs_handle, NVS_KEY_CAL_B_FLOOR, g_diversity_state.cal_b.floor_raw);
    nvs_set_u16(nvs_handle, NVS_KEY_CAL_B_PEAK, g_diversity_state.cal_b.peak_raw);

    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Calibration saved to NVS");
    return true;
}
