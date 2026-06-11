#include "app_bridge_service.h"
#include "app_common.h"
#include <ble/ble.h>
#include <furi_ble/event_dispatcher.h>
#include <furi_ble/gatt.h>

#include <furi.h>

#include "app_bridge_service_uuid.inc"

#define TAG "BtAppBridgeSvc"

typedef enum {
    AppBridgeSvcGattCharacteristicEvents = 0,
    AppBridgeSvcGattCharacteristicCommands,
    AppBridgeSvcGattCharacteristicCount,
} AppBridgeSvcGattCharacteristicId;

static const uint8_t empty_frame[BLE_SVC_APP_BRIDGE_FRAME_SIZE_MAX] = {0};

static const BleGattCharacteristicParams ble_svc_app_bridge_chars
    [AppBridgeSvcGattCharacteristicCount] = {
        [AppBridgeSvcGattCharacteristicEvents] =
            {.name = "App events",
             .data_prop_type = FlipperGattCharacteristicDataFixed,
             .data.fixed.ptr = empty_frame,
             .data.fixed.length = BLE_SVC_APP_BRIDGE_FRAME_SIZE_MAX,
             .uuid.Char_UUID_128 = BLE_SVC_APP_BRIDGE_EVENTS_CHAR_UUID,
             .uuid_type = UUID_TYPE_128,
             .char_properties = CHAR_PROP_READ | CHAR_PROP_NOTIFY,
             .security_permissions = ATTR_PERMISSION_AUTHEN_READ,
             .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
             .is_variable = CHAR_VALUE_LEN_VARIABLE},
        [AppBridgeSvcGattCharacteristicCommands] =
            {.name = "App commands",
             .data_prop_type = FlipperGattCharacteristicDataFixed,
             .data.fixed.ptr = empty_frame,
             .data.fixed.length = BLE_SVC_APP_BRIDGE_FRAME_SIZE_MAX,
             .uuid.Char_UUID_128 = BLE_SVC_APP_BRIDGE_COMMANDS_CHAR_UUID,
             .uuid_type = UUID_TYPE_128,
             .char_properties = CHAR_PROP_READ | CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RESP,
             .security_permissions = ATTR_PERMISSION_AUTHEN_READ | ATTR_PERMISSION_AUTHEN_WRITE,
             .gatt_evt_mask = GATT_NOTIFY_ATTRIBUTE_WRITE,
             .is_variable = CHAR_VALUE_LEN_VARIABLE}};

struct BleServiceAppBridge {
    uint16_t svc_handle;
    BleGattCharacteristicInstance chars[AppBridgeSvcGattCharacteristicCount];
    GapSvcEventHandler* event_handler;
    AppBridgeServiceEventCallback callback;
    void* context;
};

static BleEventAckStatus ble_svc_app_bridge_event_handler(void* event, void* context) {
    BleServiceAppBridge* service = context;
    BleEventAckStatus ret = BleEventNotAck;
    hci_event_pckt* event_pckt = (hci_event_pckt*)(((hci_uart_pckt*)event)->data);
    evt_blecore_aci* blecore_evt = (evt_blecore_aci*)event_pckt->data;

    if(event_pckt->evt == HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) {
        if(blecore_evt->ecode == ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE) {
            aci_gatt_attribute_modified_event_rp0* attribute_modified =
                (aci_gatt_attribute_modified_event_rp0*)blecore_evt->data;
            if(attribute_modified->Attr_Handle ==
               service->chars[AppBridgeSvcGattCharacteristicCommands].handle + 1) {
                FURI_LOG_D(TAG, "Received %d command bytes", attribute_modified->Attr_Data_Length);
                if(service->callback) {
                    const AppBridgeServiceEvent app_event = {
                        .event = AppBridgeServiceEventTypeCommandReceived,
                        .data = {
                            .buffer = attribute_modified->Attr_Data,
                            .size = attribute_modified->Attr_Data_Length,
                        }};
                    service->callback(app_event, service->context);
                }
                ret = BleEventAckFlowEnable;
            }
        }
    }

    return ret;
}

BleServiceAppBridge* ble_svc_app_bridge_start(void) {
    BleServiceAppBridge* service = malloc(sizeof(BleServiceAppBridge));
    service->callback = NULL;
    service->context = NULL;
    service->event_handler =
        ble_event_dispatcher_register_svc_handler(ble_svc_app_bridge_event_handler, service);

    if(!ble_gatt_service_add(UUID_TYPE_128, &service_uuid, PRIMARY_SERVICE, 8, &service->svc_handle)) {
        ble_event_dispatcher_unregister_svc_handler(service->event_handler);
        free(service);
        return NULL;
    }

    for(uint8_t i = 0; i < AppBridgeSvcGattCharacteristicCount; i++) {
        ble_gatt_characteristic_init(
            service->svc_handle, &ble_svc_app_bridge_chars[i], &service->chars[i]);
    }

    return service;
}

void ble_svc_app_bridge_stop(BleServiceAppBridge* service) {
    if(!service) {
        return;
    }

    ble_event_dispatcher_unregister_svc_handler(service->event_handler);
    for(uint8_t i = 0; i < AppBridgeSvcGattCharacteristicCount; i++) {
        ble_gatt_characteristic_delete(service->svc_handle, &service->chars[i]);
    }
    ble_gatt_service_delete(service->svc_handle);
    free(service);
}

void ble_svc_app_bridge_set_callback(
    BleServiceAppBridge* service,
    AppBridgeServiceEventCallback callback,
    void* context) {
    furi_check(service);
    service->callback = callback;
    service->context = context;
}

static bool ble_svc_app_bridge_encode_frame(
    uint8_t* frame,
    uint16_t* frame_len,
    const char* app_id,
    const char* command,
    const uint8_t* payload,
    uint16_t payload_len) {
    furi_check(frame);
    furi_check(frame_len);
    furi_check(app_id);
    furi_check(command);

    const size_t app_id_len = strlen(app_id);
    const size_t command_len = strlen(command);
    if((app_id_len == 0) || (command_len == 0) ||
       (app_id_len > BLE_SVC_APP_BRIDGE_APP_ID_LEN_MAX) ||
       (command_len > BLE_SVC_APP_BRIDGE_COMMAND_LEN_MAX) ||
       (payload_len > BLE_SVC_APP_BRIDGE_PAYLOAD_LEN_MAX)) {
        return false;
    }

    const size_t total_len = BLE_SVC_APP_BRIDGE_HEADER_LEN + app_id_len + command_len + payload_len;
    if(total_len > BLE_SVC_APP_BRIDGE_FRAME_SIZE_MAX) {
        return false;
    }

    frame[0] = 'F';
    frame[1] = 'A';
    frame[2] = 'B';
    frame[3] = '1';
    frame[4] = app_id_len;
    frame[5] = command_len;
    frame[6] = payload_len & 0xFF;
    frame[7] = payload_len >> 8;
    memcpy(&frame[BLE_SVC_APP_BRIDGE_HEADER_LEN], app_id, app_id_len);
    memcpy(&frame[BLE_SVC_APP_BRIDGE_HEADER_LEN + app_id_len], command, command_len);
    if(payload_len) {
        furi_check(payload);
        memcpy(&frame[BLE_SVC_APP_BRIDGE_HEADER_LEN + app_id_len + command_len], payload, payload_len);
    }

    *frame_len = total_len;
    return true;
}

bool ble_svc_app_bridge_send(
    BleServiceAppBridge* service,
    const char* app_id,
    const char* command,
    const uint8_t* payload,
    uint16_t payload_len) {
    furi_check(service);

    uint8_t frame[BLE_SVC_APP_BRIDGE_FRAME_SIZE_MAX];
    uint16_t frame_len = 0;
    if(!ble_svc_app_bridge_encode_frame(frame, &frame_len, app_id, command, payload, payload_len)) {
        FURI_LOG_W(TAG, "Invalid app bridge frame");
        return false;
    }

    const tBleStatus result = aci_gatt_update_char_value(
        service->svc_handle,
        service->chars[AppBridgeSvcGattCharacteristicEvents].handle,
        0,
        frame_len,
        frame);

    if(result != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "Failed updating app bridge events characteristic: %d", result);
    }

    return result == BLE_STATUS_SUCCESS;
}
