#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <core/pubsub.h>
#include <furi_ble/profile_interface.h>
#include <core/common_defines.h>
#include <services/serial_service.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORD_BT "bt"

typedef struct Bt Bt;

typedef enum {
    BtStatusUnavailable,
    BtStatusOff,
    BtStatusAdvertising,
    BtStatusConnected,
} BtStatus;

typedef struct {
    uint8_t rssi;
    uint32_t since;
} BtRssi;

typedef void (*BtStatusChangedCallback)(BtStatus status, void* context);

#define BT_APP_BRIDGE_APP_ID_LEN_MAX     32
#define BT_APP_BRIDGE_COMMAND_LEN_MAX    32
#define BT_APP_BRIDGE_PAYLOAD_LEN_MAX    172
#define BT_APP_BRIDGE_V2_PAYLOAD_LEN_MAX 160

typedef enum {
    BtAppBridgeFlagAckRequested = 1U << 0,
    BtAppBridgeFlagResponse = 1U << 1,
    BtAppBridgeFlagError = 1U << 2,
} BtAppBridgeFlag;

#define BT_APP_BRIDGE_FLAGS_MASK \
    (BtAppBridgeFlagAckRequested | BtAppBridgeFlagResponse | BtAppBridgeFlagError)

typedef struct {
    char app_id[BT_APP_BRIDGE_APP_ID_LEN_MAX + 1];
    char command[BT_APP_BRIDGE_COMMAND_LEN_MAX + 1];
    uint8_t payload[BT_APP_BRIDGE_PAYLOAD_LEN_MAX];
    uint16_t payload_len;
    uint32_t request_id;
    uint8_t protocol_version;
    uint8_t flags;
    uint8_t chunk_index;
    uint8_t chunk_count;
} BtAppBridgeEvent;

/** Change BLE Profile
 * @note Call of this function leads to 2nd core restart
 *
 * @param bt                 Bt instance
 * @param profile_template   Profile template to change to
 * @param params             Profile parameters. Can be NULL
 *
 * @return          true on success
 */
FURI_WARN_UNUSED FuriHalBleProfileBase* bt_profile_start(
    Bt* bt,
    const FuriHalBleProfileTemplate* profile_template,
    FuriHalBleProfileParams params);

/** Stop current BLE Profile and restore default profile
 * @note Call of this function leads to 2nd core restart
 *
 * @param bt        Bt instance
 *
 * @return          true on success
 */
bool bt_profile_restore_default(Bt* bt);

/** Disconnect from Central
 *
 * @param bt        Bt instance
 */
void bt_disconnect(Bt* bt);

/** Set callback for Bluetooth status change notification
 *
 * @param bt        Bt instance
 * @param callback  BtStatusChangedCallback instance
 * @param context   pointer to context
 */
void bt_set_status_changed_callback(Bt* bt, BtStatusChangedCallback callback, void* context);

/** Send a generic application event over BLE App Bridge
 *
 * The active BLE profile must be the default serial profile. The packet is
 * exposed to a paired central through the Flipper App Bridge GATT service.
 *
 * @param bt            Bt instance
 * @param app_id        stable app id, ASCII, up to 32 bytes
 * @param command       command name, ASCII, up to 32 bytes
 * @param payload       optional payload
 * @param payload_len   payload length, up to 172 bytes
 *
 * @return true on success
 */
bool bt_app_bridge_send(
    Bt* bt,
    const char* app_id,
    const char* command,
    const uint8_t* payload,
    uint16_t payload_len);

/** Send a null-terminated text payload over BLE App Bridge */
bool bt_app_bridge_send_text(Bt* bt, const char* app_id, const char* command, const char* payload);

/** Send an App Bridge v2 frame with request and chunk metadata. */
bool bt_app_bridge_send_v2(
    Bt* bt,
    const char* app_id,
    const char* command,
    uint32_t request_id,
    uint8_t flags,
    uint8_t chunk_index,
    uint8_t chunk_count,
    const uint8_t* payload,
    uint16_t payload_len);

/** Send a single-frame App Bridge v2 UTF-8 response. */
bool bt_app_bridge_send_text_v2(
    Bt* bt,
    const char* app_id,
    const char* command,
    uint32_t request_id,
    uint8_t flags,
    const char* payload);

/** Subscribe to commands received from BLE App Bridge central */
FuriPubSub* bt_app_bridge_get_pubsub(Bt* bt);

/** Forget bonded devices
 * @note Leads to wipe ble key storage and deleting bt.keys
 *
 * @param bt        Bt instance
 */
void bt_forget_bonded_devices(Bt* bt);

/** Set keys storage file path
 *
 * @param bt                    Bt instance
 * @param keys_storage_path     Path to file with saved keys
 */
void bt_keys_storage_set_storage_path(Bt* bt, const char* keys_storage_path);

/** Set default keys storage file path
 *
 * @param bt                    Bt instance
 */
void bt_keys_storage_set_default_path(Bt* bt);

#ifdef __cplusplus
}
#endif
