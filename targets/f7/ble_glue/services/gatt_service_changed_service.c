#include "gatt_service_changed_service_i.h"

#include "app_common.h"
#include <ble/ble.h>
#include <furi.h>
#include <furi_ble/event_dispatcher.h>

#define TAG "BtGattChangedSvc"

// aci_gatt_init() creates the Generic Attribute service and Service Changed
// characteristic before GAP and application services. The STM32WB stack exposes
// fixed handles for that built-in range.
#define BLE_SVC_GATT_SERVICE_HANDLE                    (0x0001U)
#define BLE_SVC_GATT_SERVICE_CHANGED_CHAR_HANDLE       (0x0002U)
#define BLE_SVC_GATT_SERVICE_CHANGED_CCCD_HANDLE       (0x0004U)
#define BLE_SVC_GATT_SERVICE_CHANGED_INDICATE_ENABLED  (0x0002U)
#define BLE_SVC_GATT_SERVICE_CHANGED_RANGE_START       (0x0001U)
#define BLE_SVC_GATT_SERVICE_CHANGED_RANGE_END         (0xFFFFU)

struct BleServiceGattServiceChanged {
    GapSvcEventHandler* event_handler;
    bool pending;
    bool cccd_enabled;
    bool indication_in_flight;
};

static bool ble_svc_gatt_service_changed_read_cccd(void) {
    uint8_t value[sizeof(uint16_t)] = {0};
    uint16_t length = 0;
    uint16_t value_length = 0;
    const tBleStatus result = aci_gatt_read_handle_value(
        BLE_SVC_GATT_SERVICE_CHANGED_CCCD_HANDLE, 0, sizeof(value), &length, &value_length, value);
    if((result != BLE_STATUS_SUCCESS) || (value_length < sizeof(uint16_t))) {
        return false;
    }

    const uint16_t client_config = value[0] | (value[1] << 8);
    return client_config & BLE_SVC_GATT_SERVICE_CHANGED_INDICATE_ENABLED;
}

static bool ble_svc_gatt_service_changed_indicate(BleServiceGattServiceChanged* service) {
    if(!service || !service->pending || !service->cccd_enabled || service->indication_in_flight) {
        return false;
    }

    const uint8_t changed_range[] = {
        BLE_SVC_GATT_SERVICE_CHANGED_RANGE_START & 0xFF,
        BLE_SVC_GATT_SERVICE_CHANGED_RANGE_START >> 8,
        BLE_SVC_GATT_SERVICE_CHANGED_RANGE_END & 0xFF,
        BLE_SVC_GATT_SERVICE_CHANGED_RANGE_END >> 8,
    };

    const tBleStatus result = aci_gatt_update_char_value(
        BLE_SVC_GATT_SERVICE_HANDLE,
        BLE_SVC_GATT_SERVICE_CHANGED_CHAR_HANDLE,
        0,
        sizeof(changed_range),
        changed_range);

    if(result == BLE_STATUS_SUCCESS) {
        service->pending = false;
        service->indication_in_flight = true;
        FURI_LOG_D(TAG, "Service Changed indication queued");
        return true;
    }

    if((result != BLE_STATUS_BUSY) && (result != BLE_STATUS_INSUFFICIENT_RESOURCES)) {
        FURI_LOG_D(TAG, "Service Changed indication deferred: %d", result);
    }

    return false;
}

static bool ble_svc_gatt_service_changed_cccd_enabled(
    const aci_gatt_attribute_modified_event_rp0* attribute_modified,
    bool* cccd_enabled) {
    if(attribute_modified->Attr_Handle != BLE_SVC_GATT_SERVICE_CHANGED_CCCD_HANDLE) {
        return false;
    }

    if(attribute_modified->Attr_Data_Length < sizeof(uint16_t)) {
        *cccd_enabled = false;
        return false;
    }

    const uint16_t client_config =
        attribute_modified->Attr_Data[0] | (attribute_modified->Attr_Data[1] << 8);
    *cccd_enabled = client_config & BLE_SVC_GATT_SERVICE_CHANGED_INDICATE_ENABLED;
    return true;
}

static BleEventAckStatus
    ble_svc_gatt_service_changed_event_handler(void* event, void* context) {
    BleServiceGattServiceChanged* service = context;
    BleEventAckStatus ret = BleEventNotAck;
    hci_event_pckt* event_pckt = (hci_event_pckt*)(((hci_uart_pckt*)event)->data);

    if(event_pckt->evt == HCI_DISCONNECTION_COMPLETE_EVT_CODE) {
        service->cccd_enabled = false;
        service->indication_in_flight = false;
        service->pending = true;
    } else if(event_pckt->evt == HCI_LE_META_EVT_CODE) {
        evt_le_meta_event* meta_evt = (evt_le_meta_event*)event_pckt->data;
        if(meta_evt->subevent == HCI_LE_CONNECTION_COMPLETE_SUBEVT_CODE) {
            service->cccd_enabled = ble_svc_gatt_service_changed_read_cccd();
            ble_svc_gatt_service_changed_indicate(service);
        }
    } else if(event_pckt->evt == HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) {
        evt_blecore_aci* blecore_evt = (evt_blecore_aci*)event_pckt->data;

        if(blecore_evt->ecode == ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE) {
            aci_gatt_attribute_modified_event_rp0* attribute_modified =
                (aci_gatt_attribute_modified_event_rp0*)blecore_evt->data;

            bool cccd_enabled = false;
            if(ble_svc_gatt_service_changed_cccd_enabled(attribute_modified, &cccd_enabled)) {
                service->cccd_enabled = cccd_enabled;
                ble_svc_gatt_service_changed_indicate(service);
                ret = BleEventAckFlowEnable;
            }
        } else if(blecore_evt->ecode == ACI_GAP_PAIRING_COMPLETE_VSEVT_CODE) {
            service->cccd_enabled =
                service->cccd_enabled || ble_svc_gatt_service_changed_read_cccd();
            ble_svc_gatt_service_changed_indicate(service);
        } else if(blecore_evt->ecode == ACI_GATT_TX_POOL_AVAILABLE_VSEVT_CODE) {
            ble_svc_gatt_service_changed_indicate(service);
        } else if(
            (blecore_evt->ecode == ACI_GATT_SERVER_CONFIRMATION_VSEVT_CODE) &&
            service->indication_in_flight) {
            service->indication_in_flight = false;
            ret = BleEventAckFlowEnable;
        }
    }

    return ret;
}

BleServiceGattServiceChanged* ble_svc_gatt_service_changed_start(void) {
    BleServiceGattServiceChanged* service = malloc(sizeof(BleServiceGattServiceChanged));
    service->pending = true;
    service->cccd_enabled = false;
    service->indication_in_flight = false;
    service->event_handler = ble_event_dispatcher_register_svc_handler(
        ble_svc_gatt_service_changed_event_handler, service);

    return service;
}

void ble_svc_gatt_service_changed_stop(BleServiceGattServiceChanged* service) {
    if(!service) {
        return;
    }

    ble_event_dispatcher_unregister_svc_handler(service->event_handler);
    free(service);
}

void ble_svc_gatt_service_changed_mark_dirty(BleServiceGattServiceChanged* service) {
    if(!service) {
        return;
    }

    service->pending = true;
    ble_svc_gatt_service_changed_indicate(service);
}
