/**
 * @file diversity.h
 * @brief Advanced diversity switching algorithm based on Starfish specification
 * @version 1.8.0
 *
 * v1.8.0 additions:
 *   - Point 1: glitch-free switch (de-assert both lines before asserting new)
 *   - Point 2: time-normalised slope (ADC units/s) so threshold is rate-invariant
 *   - Point 7: per-receiver software AGC (slow EMA baseline, ±15% correction)
 *   - Point 9: outcome-weighted hysteresis (+8 score bonus for 5 s when a
 *              switch is confirmed beneficial 200 ms after the event)
 *   - Point 8: interference rejection via micro-frequency offset (±1 MHz trial
 *              when active receiver RSSI is low and declining; reverts after 30 s)
 *
 * Implements intelligent diversity switching with:
 * - RSSI normalization and calibration
 * - Variance-based stability scoring
 * - Dwell time, hysteresis, and cooldown
 * - Mode profiles (Race, Freestyle, Long Range)
 * - Switch event counters and telemetry
 */

#ifndef __DIVERSITY_H
#define __DIVERSITY_H

#include <stdint.h>
#include <stdbool.h>

// Configuration
#define DIVERSITY_SAMPLE_WINDOW_MS 200 // Rolling window for variance calculation
#define DIVERSITY_SAMPLE_RATE_HZ 250   // RSSI sampling rate (4ms per sample)
#define DIVERSITY_MAX_SAMPLES 50       // Max samples in rolling window (200ms @ 250Hz)

/** @brief Diversity mode profiles */
typedef enum {
    DIVERSITY_MODE_RACE = 0,      // Aggressive switching, minimal lag
    DIVERSITY_MODE_FREESTYLE,      // Balanced switching, smooth video
    DIVERSITY_MODE_LONG_RANGE,     // Stable switching, range optimization
    DIVERSITY_MODE_COUNT
} diversity_mode_t;

/** @brief Active receiver selection */
typedef enum {
    DIVERSITY_RX_A = 0,
    DIVERSITY_RX_B,
} diversity_rx_t;

/** @brief Frequency-shift FSM states for interference rejection (point 8) */
typedef enum {
    FREQ_SHIFT_IDLE = 0,       // No shift active; normal operation
    FREQ_SHIFT_EVAL_PLUS,      // Tried +1 MHz — collecting evidence
    FREQ_SHIFT_EVAL_MINUS,     // Tried -1 MHz — collecting evidence
    FREQ_SHIFT_HOLD,           // Shift accepted; holding for FREQ_SHIFT_HOLD_MS
} freq_shift_state_t;

/** @brief Receiver health status flags */
typedef struct {
    bool stuck_high;               // RSSI stuck at max value
    bool stuck_low;                // RSSI stuck at min value
    bool no_variance;              // No RSSI changes detected
    bool disabled;                 // Receiver manually disabled
} diversity_health_t;

/** @brief Mode-specific parameters */
typedef struct {
    uint32_t dwell_ms;             // Minimum time before allowing next switch
    uint32_t cooldown_ms;          // Extended dwell after a switch
    uint8_t hysteresis_pct;        // Required score advantage to switch (0-100)
    float weight_rssi;             // RSSI weight in combined score (0.0-1.0)
    float weight_stability;        // Stability weight in combined score (0.0-1.0)
    int16_t slope_threshold;       // Preemptive switch slope threshold (negative)
    const char* name;              // Mode display name
} diversity_mode_params_t;

/** @brief RSSI calibration data per receiver */
typedef struct {
    uint16_t floor_raw;            // Noise floor (min RSSI)
    uint16_t peak_raw;             // Signal peak (max RSSI)
    bool calibrated;               // Calibration completed flag
} rssi_calibration_t;

/** @brief Per-receiver runtime state */
typedef struct {
    // Raw and normalized RSSI
    uint16_t rssi_raw;             // Current raw ADC value
    uint8_t rssi_norm;             // Normalized RSSI (0-100)

    // Rolling window for statistics
    uint16_t rssi_samples[DIVERSITY_MAX_SAMPLES];
    uint8_t sample_index;
    uint8_t sample_count;

    // Statistics
    uint16_t rssi_mean;            // Mean RSSI over window
    uint16_t rssi_variance;        // Variance over window
    int16_t rssi_slope;            // Rate of change (delta/time)

    // Scores
    uint8_t stability_score;       // Stability metric (0-100)
    uint8_t combined_score;        // Final weighted score (0-100)

    // Software AGC — per-receiver long-term baseline (point 7)
    float    agc_baseline;         // Slow EMA of rssi_norm (~10 s convergence @100 Hz)
    uint16_t agc_sample_count;     // Stable samples accumulated; correction active after 100
    int16_t  rssi_agc;             // AGC-corrected RSSI (0-100), used inside scoring

    // Health
    diversity_health_t health;
    uint32_t health_check_ms;      // Time of last health check
} diversity_rx_state_t;

/** @brief Complete diversity system state */
typedef struct {
    // Active configuration
    diversity_mode_t mode;
    diversity_rx_t active_rx;

    // Per-receiver state
    diversity_rx_state_t rx_a;
    diversity_rx_state_t rx_b;

    // Calibration
    rssi_calibration_t cal_a;
    rssi_calibration_t cal_b;

    // Switching logic state
    uint32_t last_switch_ms;       // Time of last switch
    uint32_t switch_count;         // Total switches since boot
    uint32_t switch_count_minute;  // Switches in last 60 seconds
    uint32_t time_stable_ms;       // Time since last switch
    uint32_t longest_stable_ms;    // Longest stable period
    bool in_cooldown;              // Currently in cooldown period

    // Outcome-weighted hysteresis (point 9)
    uint32_t       outcome_check_ms;       // When to evaluate the last switch (0 = none pending)
    diversity_rx_t outcome_new_rx;         // Receiver we just switched TO
    uint8_t        outcome_rssi_at_switch; // rssi_norm of new RX right at switch moment
    float          rx_a_pref_bonus;        // Temporary score bonus for RX A (0-10)
    float          rx_b_pref_bonus;        // Temporary score bonus for RX B (0-10)
    uint32_t       rx_a_bonus_expires_ms;  // Expiry timestamp for RX A bonus
    uint32_t       rx_b_bonus_expires_ms;  // Expiry timestamp for RX B bonus

    // Adaptive sampling (v1.7.1)
    uint32_t last_sample_ms;       // Last RSSI sampling time
    bool adaptive_high_rate;       // true=100Hz, false=20Hz
    float switches_per_second;     // Recent switching rate

    // Point 8: interference rejection via micro-frequency offset
    freq_shift_state_t freq_shift_state;       // FSM state
    uint16_t           freq_shift_nominal;     // Nominal channel freq at trigger time (MHz)
    int8_t             freq_shift_offset;      // Active offset: 0, +1, or -1 MHz
    uint32_t           freq_shift_eval_end_ms; // End of evidence-collection window
    uint32_t           freq_shift_hold_end_ms; // When the accepted hold expires
    uint32_t           freq_shift_cooldown_ms; // Don't re-trigger until this timestamp
    uint8_t            freq_shift_baseline;    // rssi_norm of active RX at trigger time

    // Point E: low-signal audio alert state
    uint32_t low_signal_beep_ms;   // Timestamp of last low-signal beep (ms)

    // Telemetry
    int8_t rssi_delta;             // RSSI_A - RSSI_B (signed)
    uint8_t switches_per_min;      // Recent switch rate

} diversity_state_t;

// Global mode parameters (defined in diversity.c)
extern const diversity_mode_params_t diversity_mode_params[DIVERSITY_MODE_COUNT];

// Core API functions
void diversity_init(void);
void diversity_set_mode(diversity_mode_t mode);
void diversity_update(void); // Call at DIVERSITY_SAMPLE_RATE_HZ
diversity_rx_t diversity_get_active_rx(void);
diversity_state_t* diversity_get_state(void);

// RSSI calibration — incremental (non-blocking) API
#define DIVERSITY_CALIB_SAMPLES 50          // 50 × 50ms = 2.5 s per phase on a single channel
void diversity_calibrate_floor_sample(diversity_rx_t rx, uint16_t* buf, int index);
bool diversity_calibrate_floor_finish(uint16_t* buf, int count, uint16_t* result);
void diversity_calibrate_peak_sample(diversity_rx_t rx, uint16_t* buf, int index);
bool diversity_calibrate_peak_finish(uint16_t* buf, int count, uint16_t* result);
bool diversity_calibrate_save(void);
void diversity_calibrate_load(void);

// Telemetry / stats
uint32_t diversity_get_switch_count(void);
uint32_t diversity_get_switches_per_minute(void);
uint32_t diversity_get_time_stable_ms(void);
void diversity_reset_stats(void);

// Internal functions (exposed for testing)
uint8_t diversity_normalize_rssi(uint16_t raw, rssi_calibration_t* cal);
void diversity_calculate_scores(diversity_rx_state_t* rx, const diversity_mode_params_t* params);
bool diversity_should_switch(diversity_state_t* state, const diversity_mode_params_t* params, uint32_t now);
void diversity_check_receiver_health(diversity_rx_state_t* rx, uint32_t now);

#endif // __DIVERSITY_H
