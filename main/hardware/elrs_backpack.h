#ifndef ELRS_BACKPACK_H
#define ELRS_BACKPACK_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief ELRS Binding State Machine
 */
typedef enum {
    ELRS_STATE_UNBOUND,        // No UID stored, not bound
    ELRS_STATE_BOUND,          // UID stored, normal operation
    ELRS_STATE_BINDING,        // Currently waiting for bind packet
    ELRS_STATE_BIND_SUCCESS,   // Just successfully received bind packet
    ELRS_STATE_BIND_TIMEOUT    // Binding timed out without receiving packet
} elrs_bind_state_t;

/**
 * @brief Initialize ELRS Backpack (ESP-NOW wireless implementation)
 * Initializes WiFi in STA mode, ESP-NOW protocol, loads UID from NVS if available,
 * and starts the background task for processing MSP packets.
 * @return true if initialization successful, false otherwise
 */
bool ELRS_Backpack_Init(void);

/**
 * @brief Get current binding state
 * @return Current binding state (UNBOUND, BOUND, BINDING, BIND_SUCCESS, BIND_TIMEOUT)
 */
elrs_bind_state_t ELRS_Backpack_Get_State(void);

/**
 * @brief Start binding process
 * Adds broadcast MAC as peer and waits for TX to send binding packet with UID.
 * Binding will timeout after specified duration if no packet received.
 * @param timeout_ms Timeout in milliseconds (typically 30000 for 30 seconds)
 * @return true if binding process started successfully, false otherwise
 */
bool ELRS_Backpack_Start_Binding(uint32_t timeout_ms);

/**
 * @brief Cancel ongoing binding process
 * Removes broadcast peer and returns to previous state (BOUND or UNBOUND)
 */
void ELRS_Backpack_Cancel_Binding(void);

/**
 * @brief Check if currently bound to a TX
 * @return true if bound (UID stored and valid), false otherwise
 */
bool ELRS_Backpack_Is_Bound(void);

/**
 * @brief Unbind from TX
 * Clears stored UID from NVS and reinitializes WiFi with default MAC.
 * Transitions to UNBOUND state.
 */
void ELRS_Backpack_Unbind(void);

/**
 * @brief Get current stored UID
 * @param uid Buffer to store 6-byte UID (must be at least 6 bytes)
 */
void ELRS_Backpack_Get_UID(uint8_t uid[6]);

/**
 * @brief Get remaining binding timeout in milliseconds
 * Only valid when state is ELRS_STATE_BINDING
 * @return Remaining timeout in milliseconds
 */
uint32_t ELRS_Backpack_Get_Binding_Timeout_Remaining(void);

/**
 * @brief Thread-safe channel change for local control
 * Uses mutex protection to prevent conflicts with remote MSP channel changes.
 * Immediately updates RX5808 hardware and GUI will reflect change on next refresh.
 * @param channel Absolute channel index (0-47)
 * @return true if channel changed successfully, false if invalid or mutex timeout
 * @note Automatically validates channel range (0-47) and acquires channel mutex
 */
bool ELRS_Backpack_Set_Channel_Safe(uint8_t channel);

/**
 * @brief Get VTX band swap setting
 * When enabled, swaps R (32-39) ↔ L (40-47) band indices to match non-standard VTX tables
 * where Raceband and Lowband frequencies are reversed.
 * Enable this if your VTX transmits Lowband frequencies when you select Raceband (and vice versa).
 * @return true if band swap enabled, false for standard band order
 */
bool ELRS_Backpack_Get_VTX_Band_Swap(void);

/**
 * @brief Set VTX band swap setting
 * Enable this if your VTX has R and L bands swapped in its frequency table.
 * Symptoms: Selecting R1 in VTX Admin shows video on L1, selecting L1 shows video on R1.
 * This remaps incoming channel indices so the receiver tunes to the correct frequency.
 * Setting is saved to NVS for persistence across reboots.
 * @param swap_enabled true to enable R↔L swapping, false for standard (A,B,E,F,R,L)
 * @return true if setting saved successfully, false on NVS error
 */
bool ELRS_Backpack_Set_VTX_Band_Swap(bool swap_enabled);

/**
 * @brief Send VTX configuration packet via ESP-NOW
 * Broadcasts the channel change to all listening backpacks using MSP_SET_VTX_CONFIG.
 * @param channel_idx Channel index (0-47)
 * @return true if packet sent successfully
 */
bool ELRS_Backpack_Send_VTX_Config(uint8_t channel_idx);

#endif // ELRS_BACKPACK_H
