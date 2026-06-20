#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_SVC_APP_BRIDGE_FRAME_SIZE_MAX  (244)
#define BLE_SVC_APP_BRIDGE_APP_ID_LEN_MAX  (32)
#define BLE_SVC_APP_BRIDGE_COMMAND_LEN_MAX (32)
#define BLE_SVC_APP_BRIDGE_PAYLOAD_LEN_MAX (172)
#define BLE_SVC_APP_BRIDGE_HEADER_LEN      (8)

#define BLE_SVC_APP_BRIDGE_V2_HEADER_LEN      (16)
#define BLE_SVC_APP_BRIDGE_V2_PAYLOAD_LEN_MAX (160)

#define BLE_SVC_APP_BRIDGE_V2_FLAG_ACK_REQUESTED (1U << 0)
#define BLE_SVC_APP_BRIDGE_V2_FLAG_RESPONSE      (1U << 1)
#define BLE_SVC_APP_BRIDGE_V2_FLAG_ERROR         (1U << 2)

typedef enum {
    AppBridgeServiceEventTypeCommandReceived,
} AppBridgeServiceEventType;

typedef struct {
    const uint8_t* buffer;
    uint16_t size;
} AppBridgeServiceData;

typedef struct {
    AppBridgeServiceEventType event;
    AppBridgeServiceData data;
} AppBridgeServiceEvent;

typedef void (*AppBridgeServiceEventCallback)(AppBridgeServiceEvent event, void* context);

typedef struct BleServiceAppBridge BleServiceAppBridge;

BleServiceAppBridge* ble_svc_app_bridge_start(void);

void ble_svc_app_bridge_stop(BleServiceAppBridge* service);

void ble_svc_app_bridge_set_callback(
    BleServiceAppBridge* service,
    AppBridgeServiceEventCallback callback,
    void* context);

bool ble_svc_app_bridge_send(
    BleServiceAppBridge* service,
    const char* app_id,
    const char* command,
    const uint8_t* payload,
    uint16_t payload_len);

bool ble_svc_app_bridge_send_v2(
    BleServiceAppBridge* service,
    const char* app_id,
    const char* command,
    uint32_t request_id,
    uint8_t flags,
    uint8_t chunk_index,
    uint8_t chunk_count,
    const uint8_t* payload,
    uint16_t payload_len);

#ifdef __cplusplus
}
#endif
