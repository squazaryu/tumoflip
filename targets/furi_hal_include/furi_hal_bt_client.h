/**
 * @file furi_hal_bt_client.h
 * BLE observer and explicitly-authorized GATT client API.
 *
 * The client API is intentionally bounded and is available only when the
 * full STM32WB BLE stack is installed.  The normal firmware continues to
 * use the light peripheral stack; callers must handle a false return value
 * and keep the existing BLE workflow.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FURI_HAL_BT_SCAN_DATA_MAX 31U
#define FURI_HAL_BT_SCAN_NAME_MAX 32U
#define FURI_HAL_BT_GATT_DATA_MAX 244U

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    uint8_t data_len;
    uint8_t data[FURI_HAL_BT_SCAN_DATA_MAX];
    char name[FURI_HAL_BT_SCAN_NAME_MAX];
} FuriHalBtScanResult;

typedef void (*FuriHalBtScanResultCallback)(const FuriHalBtScanResult* result, void* context);

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
} FuriHalBtPeer;

typedef enum {
    FuriHalBtGattEventConnected,
    FuriHalBtGattEventDisconnected,
    FuriHalBtGattEventService,
    FuriHalBtGattEventCharacteristic,
    FuriHalBtGattEventRead,
    FuriHalBtGattEventNotification,
    FuriHalBtGattEventProcedureComplete,
    FuriHalBtGattEventError,
} FuriHalBtGattEventType;

typedef struct {
    FuriHalBtGattEventType type;
    uint16_t connection_handle;
    uint16_t start_handle;
    uint16_t end_handle;
    uint16_t attribute_handle;
    uint8_t properties;
    uint8_t uuid_len;
    uint8_t uuid[16];
    uint16_t data_len;
    uint8_t data[FURI_HAL_BT_GATT_DATA_MAX];
    uint8_t status;
} FuriHalBtGattEvent;

typedef void (*FuriHalBtGattEventCallback)(const FuriHalBtGattEvent* event, void* context);

/** Start a bounded passive scan.  The callback must not block. */
bool furi_hal_bt_scan_start(
    uint32_t duration_ms,
    FuriHalBtScanResultCallback callback,
    void* context);

/** Stop a scan started by furi_hal_bt_scan_start(). */
bool furi_hal_bt_scan_stop(void);

/** Return whether a passive scan is currently active. */
bool furi_hal_bt_scan_is_active(void);

/** Connect to one explicitly selected peer.  Full stack only. */
bool furi_hal_bt_gatt_connect(
    const FuriHalBtPeer* peer,
    FuriHalBtGattEventCallback callback,
    void* context);

/** Disconnect the current GATT client connection. */
bool furi_hal_bt_gatt_disconnect(void);

/** Start discovery of all primary services on the selected peer. */
bool furi_hal_bt_gatt_discover_services(void);

/** Start discovery of characteristics in one previously reported service. */
bool furi_hal_bt_gatt_discover_characteristics(uint16_t start_handle, uint16_t end_handle);

/** Read one explicitly selected characteristic value handle. */
bool furi_hal_bt_gatt_read(uint16_t attribute_handle);

/**
 * Write one characteristic value after an explicit user confirmation.
 * Writes are always acknowledged by the peer; unacknowledged writes are
 * deliberately not exposed by this API.
 */
bool furi_hal_bt_gatt_write(
    uint16_t attribute_handle,
    const uint8_t* data,
    size_t data_len,
    bool user_confirmed);

/** Enable or disable notifications on a previously discovered CCCD. */
bool furi_hal_bt_gatt_set_notifications(
    uint16_t cccd_handle,
    bool enabled,
    bool user_confirmed);

#ifdef __cplusplus
}
#endif
