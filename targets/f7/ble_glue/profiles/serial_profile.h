#pragma once

#include <furi_ble/profile_interface.h>

#include <services/serial_service.h>
#include <services/app_bridge_service.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_PROFILE_SERIAL_PACKET_SIZE_MAX BLE_SVC_SERIAL_DATA_LEN_MAX

typedef enum {
    FuriHalBtSerialRpcStatusNotActive,
    FuriHalBtSerialRpcStatusActive,
} FuriHalBtSerialRpcStatus;

/** Serial service callback type */
typedef SerialServiceEventCallback FuriHalBtSerialCallback;

/** App bridge service callback type */
typedef AppBridgeServiceEventCallback FuriHalBtAppBridgeCallback;

/** Serial profile descriptor */
extern const FuriHalBleProfileTemplate* const ble_profile_serial;

/** Send data through BLE
 *
 * @param profile       Profile instance
 * @param data          data buffer
 * @param size          data buffer size
 *
 * @return      true on success
 */
bool ble_profile_serial_tx(FuriHalBleProfileBase* profile, uint8_t* data, uint16_t size);

/** Send generic app event through BLE App Bridge service
 *
 * @param profile       Profile instance
 * @param app_id        Stable application id, ASCII, up to 32 bytes
 * @param command       Command name, ASCII, up to 32 bytes
 * @param payload       optional payload
 * @param payload_len   payload length, up to 172 bytes
 *
 * @return      true on success
 */
bool ble_profile_serial_app_bridge_tx(
    FuriHalBleProfileBase* profile,
    const char* app_id,
    const char* command,
    const uint8_t* payload,
    uint16_t payload_len);

/** Set App Bridge service events callback
 *
 * @param profile       Profile instance
 * @param callback      FuriHalBtAppBridgeCallback instance
 * @param context       pointer to context
 */
void ble_profile_serial_app_bridge_set_callback(
    FuriHalBleProfileBase* profile,
    FuriHalBtAppBridgeCallback callback,
    void* context);

/** Set BLE RPC status
 *
 * @param profile       Profile instance
 * @param active        true if RPC is active
 */
void ble_profile_serial_set_rpc_active(FuriHalBleProfileBase* profile, bool active);

/** Notify that application buffer is empty
 * @param profile       Profile instance
 */
void ble_profile_serial_notify_buffer_is_empty(FuriHalBleProfileBase* profile);

/** Set Serial service events callback
 *
 * @param profile       Profile instance
 * @param buffer_size   Applicaition buffer size
 * @param calback       FuriHalBtSerialCallback instance
 * @param context       pointer to context
 */
void ble_profile_serial_set_event_callback(
    FuriHalBleProfileBase* profile,
    uint16_t buff_size,
    FuriHalBtSerialCallback callback,
    void* context);

#ifdef __cplusplus
}
#endif
