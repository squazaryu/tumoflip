#include "gap.h"

#include "app_common.h"
#include <core/mutex.h>
#include "furi_ble/event_dispatcher.h"
#include <ble/ble.h>

#include <furi_hal.h>
#include <furi_hal_bt.h>
#include <furi.h>
#include <stdint.h>

/* Full-stack GATT client support is available for engineering builds. The
 * default release profile keeps the existing light-stack image within the
 * STM32WB C2 boundary; callers receive a deterministic unsupported result. */
#ifndef TUMOFLIP_ROADMAP_FULL
#define TUMOFLIP_ROADMAP_FULL 0
#endif
#define TUMOFLIP_BLE_GATT_CLIENT TUMOFLIP_ROADMAP_FULL
#define TUMOFLIP_BLE_SCAN TUMOFLIP_ROADMAP_FULL

#define TAG "BleGap"

#define FAST_ADV_TIMEOUT    30000
#define INITIAL_ADV_TIMEOUT 60000

#define GAP_SCAN_MIN_DURATION_MS 1000U
#define GAP_SCAN_MAX_DURATION_MS 30000U
#define GAP_SCAN_INTERVAL        0x0060U
#define GAP_SCAN_WINDOW          0x0030U
#define GAP_CONNECT_INTERVAL     0x0060U
#define GAP_CONNECT_WINDOW       0x0030U
#define GAP_CONNECT_INTERVAL_MIN 0x0006U
#define GAP_CONNECT_INTERVAL_MAX 0x000CU
#define GAP_CONNECT_TIMEOUT      0x00C8U

#define GAP_INTERVAL_TO_MS(x) (uint16_t)((x) * 1.25)

typedef struct {
    uint16_t gap_svc_handle;
    uint16_t dev_name_char_handle;
    uint16_t appearance_char_handle;
    uint16_t connection_handle;
    uint8_t adv_svc_uuid_len;
    uint8_t adv_svc_uuid[20];
    uint8_t mfg_data_len;
    uint8_t mfg_data[23];
    char* adv_name;
} GapSvc;

typedef struct {
    GapSvc service;
    GapConfig* config;
    GapConnectionParams connection_params;
    GapState state;
    FuriMutex* state_mutex;
    GapEventCallback on_event_cb;
    void* context;
    FuriTimer* advertise_timer;
    FuriThread* thread;
    FuriMessageQueue* command_queue;
    bool enable_adv;
    bool is_secure;
    uint8_t negotiation_round;
    GapSvcEventHandler* client_event_handler;
#if TUMOFLIP_BLE_SCAN
    FuriTimer* scan_timer;
    bool scan_start_pending;
    bool scan_active;
    bool scan_restore_adv;
    uint32_t scan_duration_ms;
    FuriHalBtScanResultCallback scan_callback;
    void* scan_context;
#endif
#if TUMOFLIP_BLE_GATT_CLIENT
    bool gatt_connect_pending;
    bool gatt_client_connected;
    bool gatt_restore_adv;
    uint16_t gatt_connection_handle;
    FuriHalBtPeer gatt_peer;
    FuriHalBtGattEventCallback gatt_callback;
    void* gatt_context;
    uint16_t gatt_discovery_start_handle;
    uint16_t gatt_discovery_end_handle;
    uint8_t gatt_write_len;
    uint16_t gatt_descriptor_handle;
    bool gatt_notifications_enabled;
    /* One serialized attribute operation is kept at a time.  The handle is
       shared by read and write requests; naming it generically avoids making
       read completions look like they refer to a write-only field. */
    uint16_t gatt_attribute_handle;
    uint8_t gatt_write_data[FURI_HAL_BT_GATT_DATA_MAX];
#endif
} Gap;

typedef enum {
    GapCommandAdvFast,
    GapCommandAdvLowPower,
    GapCommandAdvStop,
#if TUMOFLIP_BLE_SCAN
    GapCommandScanStart,
    GapCommandScanStop,
#endif
#if TUMOFLIP_BLE_GATT_CLIENT
    GapCommandGattConnect,
    GapCommandGattDisconnect,
    GapCommandGattDiscoverServices,
    GapCommandGattDiscoverCharacteristics,
    GapCommandGattRead,
    GapCommandGattWrite,
    GapCommandGattNotifications,
#endif
    GapCommandKillThread,
} GapCommand;

static Gap* gap = NULL;

static void gap_advertise_start(GapState new_state);
static int32_t gap_app(void* context);
#if TUMOFLIP_BLE_SCAN
static void gap_scan_stop_locked(bool restore_advertising);
#endif
#if TUMOFLIP_BLE_GATT_CLIENT
static void gap_gatt_disconnect_locked(void);
#endif

static bool gap_client_stack_available(void) {
    return furi_hal_bt_get_radio_stack() == FuriHalBtStackFull;
}

#if TUMOFLIP_BLE_GATT_CLIENT
static void gap_emit_gatt_event(const FuriHalBtGattEvent* event) {
    if(gap->gatt_callback && event) {
        gap->gatt_callback(event, gap->gatt_context);
    }
}

static void gap_emit_gatt_error(uint8_t status) {
    FuriHalBtGattEvent event = {
        .type = FuriHalBtGattEventError,
        .connection_handle = gap->gatt_connection_handle,
        .status = status,
    };
    gap_emit_gatt_event(&event);
}
#endif

#if TUMOFLIP_BLE_SCAN
static uint8_t gap_copy_advertising_name(
    const uint8_t* data,
    uint8_t data_len,
    char* name,
    size_t name_size) {
    if(!name || name_size == 0U) return 0U;
    name[0] = '\0';

    uint8_t offset = 0U;
    while(offset < data_len) {
        const uint8_t field_len = data[offset];
        if(field_len == 0U) break;
        if((uint16_t)offset + 1U + field_len > data_len) break;
        const uint8_t field_type = data[offset + 1U];
        if((field_type == 0x08U || field_type == 0x09U) && field_len > 1U) {
            size_t copy_len = field_len - 1U;
            if(copy_len >= name_size) copy_len = name_size - 1U;
            memcpy(name, &data[offset + 2U], copy_len);
            name[copy_len] = '\0';
            return (uint8_t)copy_len;
        }
        offset = (uint8_t)(offset + 1U + field_len);
    }
    return 0U;
}

static void gap_handle_advertising_reports(const hci_event_pckt* event_pckt) {
    if(!gap->scan_active || !gap->scan_callback || event_pckt->plen < 3U) return;

    const uint8_t* data = event_pckt->data;
    size_t remaining = event_pckt->plen;
    if(remaining < 2U || data[0] != HCI_LE_ADVERTISING_REPORT_SUBEVT_CODE) return;
    data++;
    remaining--;

    const uint8_t report_count = data[0];
    data++;
    remaining--;

    for(uint8_t report_index = 0U; report_index < report_count; report_index++) {
        // Event layout: type (1), address type (1), address (6), data len (1),
        // data (N), RSSI (1).  Do not trust any length supplied by the peer.
        if(remaining < 10U) break;
        const uint8_t data_len = data[8];
        if((size_t)data_len + 10U > remaining || data_len > FURI_HAL_BT_SCAN_DATA_MAX) break;

        FuriHalBtScanResult result = {0};
        result.address_type = data[1];
        memcpy(result.address, &data[2], sizeof(result.address));
        result.data_len = data_len;
        if(data_len) memcpy(result.data, &data[9], data_len);
        result.rssi = (int8_t)data[9U + data_len];
        gap_copy_advertising_name(
            result.data, result.data_len, result.name, sizeof(result.name));
        gap->scan_callback(&result, gap->scan_context);

        data += 10U + data_len;
        remaining -= 10U + data_len;
    }
}
#endif

#if TUMOFLIP_BLE_GATT_CLIENT
static void gap_handle_gatt_event(const hci_event_pckt* event_pckt) {
    if(!gap->gatt_client_connected || event_pckt->plen < 3U) return;

    const evt_blecore_aci* blue_evt = (const evt_blecore_aci*)event_pckt->data;
    const uint8_t* data = blue_evt->data;
    const size_t data_len = event_pckt->plen - 2U;

    if(blue_evt->ecode == ACI_GATT_NOTIFICATION_VSEVT_CODE ||
       blue_evt->ecode == ACI_GATT_INDICATION_VSEVT_CODE) {
        if(data_len < 5U) return;
        const aci_gatt_notification_event_rp0* notification =
            (const aci_gatt_notification_event_rp0*)data;
        const size_t value_len = MIN(
            (size_t)notification->Attribute_Value_Length,
            MIN(sizeof(notification->Attribute_Value), FURI_HAL_BT_GATT_DATA_MAX));
        if(5U + value_len > data_len) return;

        FuriHalBtGattEvent event = {
            .type = blue_evt->ecode == ACI_GATT_NOTIFICATION_VSEVT_CODE
                        ? FuriHalBtGattEventNotification
                        : FuriHalBtGattEventNotification,
            .connection_handle = notification->Connection_Handle,
            .attribute_handle = notification->Attribute_Handle,
            .data_len = value_len,
        };
        if(value_len) memcpy(event.data, notification->Attribute_Value, value_len);
        gap_emit_gatt_event(&event);
    } else if(
        blue_evt->ecode == ACI_ATT_READ_RESP_VSEVT_CODE ||
        blue_evt->ecode == ACI_ATT_READ_BLOB_RESP_VSEVT_CODE) {
        if(data_len < 3U) return;
        const aci_att_read_resp_event_rp0* read = (const aci_att_read_resp_event_rp0*)data;
        const size_t value_len = MIN(
            (size_t)read->Event_Data_Length,
            MIN(sizeof(read->Attribute_Value), FURI_HAL_BT_GATT_DATA_MAX));
        if(3U + value_len > data_len) return;

        FuriHalBtGattEvent event = {
            .type = FuriHalBtGattEventRead,
            .connection_handle = read->Connection_Handle,
            .attribute_handle = gap->gatt_attribute_handle,
            .data_len = value_len,
        };
        if(value_len) memcpy(event.data, read->Attribute_Value, value_len);
        gap_emit_gatt_event(&event);
    } else if(blue_evt->ecode == ACI_ATT_READ_BY_GROUP_TYPE_RESP_VSEVT_CODE) {
        if(data_len < 4U) return;
        const aci_att_read_by_group_type_resp_event_rp0* response =
            (const aci_att_read_by_group_type_resp_event_rp0*)data;
        const uint8_t tuple_len = response->Attribute_Data_Length;
        const uint8_t list_len = response->Data_Length;
        if(tuple_len < 6U || list_len > data_len - 4U || (list_len % tuple_len) != 0U) return;

        const uint8_t* cursor = response->Attribute_Data_List;
        for(uint8_t offset = 0U; offset < list_len; offset = (uint8_t)(offset + tuple_len)) {
            FuriHalBtGattEvent event = {
                .type = FuriHalBtGattEventService,
                .connection_handle = response->Connection_Handle,
                .start_handle = cursor[0] | ((uint16_t)cursor[1] << 8),
                .end_handle = cursor[2] | ((uint16_t)cursor[3] << 8),
                .uuid_len = (uint8_t)MIN((size_t)(tuple_len - 4U), sizeof(event.uuid)),
            };
            memcpy(event.uuid, &cursor[4], event.uuid_len);
            gap_emit_gatt_event(&event);
            cursor += tuple_len;
        }
    } else if(blue_evt->ecode == ACI_ATT_READ_BY_TYPE_RESP_VSEVT_CODE) {
        if(data_len < 4U) return;
        const aci_att_read_by_type_resp_event_rp0* response =
            (const aci_att_read_by_type_resp_event_rp0*)data;
        const uint8_t tuple_len = response->Handle_Value_Pair_Length;
        const uint8_t list_len = response->Data_Length;
        if(tuple_len < 7U || list_len > data_len - 4U || (list_len % tuple_len) != 0U) return;

        const uint8_t* cursor = response->Handle_Value_Pair_Data;
        for(uint8_t offset = 0U; offset < list_len; offset = (uint8_t)(offset + tuple_len)) {
            FuriHalBtGattEvent event = {
                .type = FuriHalBtGattEventCharacteristic,
                .connection_handle = response->Connection_Handle,
                .attribute_handle = cursor[0] | ((uint16_t)cursor[1] << 8),
                .properties = cursor[2],
                .data_len = 0U,
                .uuid_len = (uint8_t)MIN((size_t)(tuple_len - 5U), sizeof(event.uuid)),
            };
            event.start_handle = cursor[3] | ((uint16_t)cursor[4] << 8);
            memcpy(event.uuid, &cursor[5], event.uuid_len);
            gap_emit_gatt_event(&event);
            cursor += tuple_len;
        }
    } else if(blue_evt->ecode == ACI_GATT_PROC_COMPLETE_VSEVT_CODE) {
        if(data_len < 3U) return;
        const aci_gatt_proc_complete_event_rp0* complete =
            (const aci_gatt_proc_complete_event_rp0*)data;
        FuriHalBtGattEvent event = {
            .type = FuriHalBtGattEventProcedureComplete,
            .connection_handle = complete->Connection_Handle,
            .status = complete->Error_Code,
        };
        gap_emit_gatt_event(&event);
    } else if(blue_evt->ecode == ACI_GATT_ERROR_RESP_VSEVT_CODE) {
        if(data_len < 6U) return;
        const aci_gatt_error_resp_event_rp0* error =
            (const aci_gatt_error_resp_event_rp0*)data;
        FuriHalBtGattEvent event = {
            .type = FuriHalBtGattEventError,
            .connection_handle = error->Connection_Handle,
            .attribute_handle = error->Attribute_Handle,
            .status = error->Error_Code,
        };
        gap_emit_gatt_event(&event);
    }
}
#endif

static BleEventAckStatus gap_client_event_handler(void* event, void* context) {
    UNUSED(context);
    if(!gap || !event) return BleEventNotAck;

    const hci_uart_pckt* uart_packet = (const hci_uart_pckt*)event;
    if(!uart_packet) return BleEventNotAck;
    const hci_event_pckt* event_pckt = (const hci_event_pckt*)uart_packet->data;
    if(event_pckt->plen == 0U) return BleEventNotAck;
#if TUMOFLIP_BLE_SCAN
    if(event_pckt->evt == HCI_LE_META_EVT_CODE) {
        gap_handle_advertising_reports(event_pckt);
    }
#endif
#if TUMOFLIP_BLE_GATT_CLIENT
    else if(event_pckt->evt == HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) {
        gap_handle_gatt_event(event_pckt);
    }
#endif
    return BleEventNotAck;
}

static void gap_verify_connection_parameters(Gap* gap) {
    furi_check(gap);

    FURI_LOG_I(
        TAG,
        "Connection parameters: Connection Interval: %d (%d ms), Slave Latency: %d, Supervision Timeout: %d",
        gap->connection_params.conn_interval,
        GAP_INTERVAL_TO_MS(gap->connection_params.conn_interval),
        gap->connection_params.slave_latency,
        gap->connection_params.supervisor_timeout);

    // Send connection parameters request update if necessary
    GapConnectionParamsRequest* params = &gap->config->conn_param;

    // Desired max connection interval depends on how many negotiation rounds we had in the past
    // In the first negotiation round we want connection interval to be minimum
    // If platform disagree then we request wider range
    uint16_t connection_interval_max = gap->negotiation_round ? params->conn_int_max :
                                                                params->conn_int_min;

    // We do care about lower connection interval bound a lot: if it's lower than 30ms 2nd core will not allow us to use flash controller
    bool negotiation_failed = params->conn_int_min > gap->connection_params.conn_interval;

    // We don't care about upper bound till connection become secure
    if(gap->is_secure) {
        negotiation_failed |= connection_interval_max < gap->connection_params.conn_interval;
    }

    if(negotiation_failed) {
        FURI_LOG_W(
            TAG,
            "Connection interval doesn't suite us. Trying to negotiate, round %u",
            gap->negotiation_round + 1);
        if(aci_l2cap_connection_parameter_update_req(
               gap->service.connection_handle,
               params->conn_int_min,
               connection_interval_max,
               gap->connection_params.slave_latency,
               gap->connection_params.supervisor_timeout)) {
            FURI_LOG_E(TAG, "Failed to request connection parameters update");
            // The other side is not in the mood
            // But we are open to try it again
            gap->negotiation_round = 0;
        } else {
            gap->negotiation_round++;
        }
    } else {
        FURI_LOG_I(
            TAG,
            "Connection interval suits us. Spent %u rounds to negotiate",
            gap->negotiation_round);
        // Looks like the other side is open to negotiation
        gap->negotiation_round = 0;
    }
}

BleEventFlowStatus ble_event_app_notification(void* pckt) {
    hci_event_pckt* event_pckt;
    evt_le_meta_event* meta_evt;
    evt_blecore_aci* blue_evt;
    hci_le_phy_update_complete_event_rp0* evt_le_phy_update_complete;
    uint8_t tx_phy;
    uint8_t rx_phy;
    tBleStatus ret = BLE_STATUS_INVALID_PARAMS;

    event_pckt = (hci_event_pckt*)((hci_uart_pckt*)pckt)->data;

    furi_check(gap);
    furi_check(furi_mutex_acquire(gap->state_mutex, FuriWaitForever) == FuriStatusOk);

    switch(event_pckt->evt) {
    case HCI_DISCONNECTION_COMPLETE_EVT_CODE: {
        hci_disconnection_complete_event_rp0* disconnection_complete_event =
            (hci_disconnection_complete_event_rp0*)event_pckt->data;
#if TUMOFLIP_BLE_GATT_CLIENT
        const bool was_gatt_client =
            gap->gatt_client_connected || gap->gatt_connect_pending ||
            (gap->gatt_connection_handle == disconnection_complete_event->Connection_Handle);
#endif
        if(disconnection_complete_event->Connection_Handle == gap->service.connection_handle) {
            gap->service.connection_handle = 0;
            gap->state = GapStateIdle;
            FURI_LOG_I(
                TAG, "Disconnect from client. Reason: %02X", disconnection_complete_event->Reason);
        }
        gap->is_secure = false;
        gap->negotiation_round = 0;
#if TUMOFLIP_BLE_GATT_CLIENT
        if(was_gatt_client) {
            gap->gatt_client_connected = false;
            gap->gatt_connect_pending = false;
            gap->gatt_descriptor_handle = 0U;
            gap->gatt_notifications_enabled = false;
            FuriHalBtGattEvent gatt_event = {
                .type = FuriHalBtGattEventDisconnected,
                .connection_handle = disconnection_complete_event->Connection_Handle,
                .status = disconnection_complete_event->Reason,
            };
            gap_emit_gatt_event(&gatt_event);
        }
#endif
        // Enterprise sleep
        furi_delay_us(666 + 666);
#if TUMOFLIP_BLE_GATT_CLIENT
        if(gap->enable_adv && gap->gatt_restore_adv && !gap->scan_active) {
            // Restart advertising
            gap_advertise_start(GapStateAdvFast);
        }
        gap->gatt_restore_adv = false;
#endif
        GapEvent event = {.type = GapEventTypeDisconnected};
        gap->on_event_cb(event, gap->context);
    } break;

    case HCI_LE_META_EVT_CODE:
        meta_evt = (evt_le_meta_event*)event_pckt->data;
        switch(meta_evt->subevent) {
        case HCI_LE_CONNECTION_UPDATE_COMPLETE_SUBEVT_CODE: {
            hci_le_connection_update_complete_event_rp0* event =
                (hci_le_connection_update_complete_event_rp0*)meta_evt->data;
            gap->connection_params.conn_interval = event->Conn_Interval;
            gap->connection_params.slave_latency = event->Conn_Latency;
            gap->connection_params.supervisor_timeout = event->Supervision_Timeout;
            FURI_LOG_I(TAG, "Connection parameters event complete");
            gap_verify_connection_parameters(gap);

            break;
        }

        case HCI_LE_PHY_UPDATE_COMPLETE_SUBEVT_CODE:
            evt_le_phy_update_complete = (hci_le_phy_update_complete_event_rp0*)meta_evt->data;
            if(evt_le_phy_update_complete->Status) {
                FURI_LOG_E(
                    TAG, "Update PHY failed, status %d", evt_le_phy_update_complete->Status);
            } else {
                FURI_LOG_I(TAG, "Update PHY succeed");
            }
            ret = hci_le_read_phy(gap->service.connection_handle, &tx_phy, &rx_phy);
            if(ret) {
                FURI_LOG_E(TAG, "Read PHY failed, status: %d", ret);
            } else {
                FURI_LOG_I(TAG, "PHY Params TX = %d, RX = %d ", tx_phy, rx_phy);
            }
            break;

        case HCI_LE_CONNECTION_COMPLETE_SUBEVT_CODE: {
            hci_le_connection_complete_event_rp0* event =
                (hci_le_connection_complete_event_rp0*)meta_evt->data;
            if(event->Status != BLE_STATUS_SUCCESS) {
#if TUMOFLIP_BLE_GATT_CLIENT
                if(gap->gatt_connect_pending) {
                    gap_emit_gatt_error(event->Status);
                    gap->gatt_connect_pending = false;
                    gap->state = GapStateIdle;
                    if(gap->enable_adv && gap->gatt_restore_adv) {
                        gap_advertise_start(GapStateAdvFast);
                    }
                    gap->gatt_restore_adv = false;
                }
#endif
                break;
            }
            gap->connection_params.conn_interval = event->Conn_Interval;
            gap->connection_params.slave_latency = event->Conn_Latency;
            gap->connection_params.supervisor_timeout = event->Supervision_Timeout;

            // Stop advertising as connection completed
            furi_timer_stop(gap->advertise_timer);

            // Update connection status and handle
            gap->state = GapStateConnected;
            gap->service.connection_handle = event->Connection_Handle;

#if TUMOFLIP_BLE_GATT_CLIENT
            if(gap->gatt_connect_pending && event->Role == 0U) {
                gap->gatt_connect_pending = false;
                gap->gatt_client_connected = true;
                gap->gatt_connection_handle = event->Connection_Handle;
                gap->gatt_descriptor_handle = 0U;
                gap->gatt_notifications_enabled = false;
                FuriHalBtGattEvent gatt_event = {
                    .type = FuriHalBtGattEventConnected,
                    .connection_handle = event->Connection_Handle,
                };
                gap_emit_gatt_event(&gatt_event);
            }
#endif

            gap_verify_connection_parameters(gap);

            if((event->Role != 0U) && (gap->config->pairing_method != GapPairingNone)) {
                // Start pairing by sending security request
                aci_gap_slave_security_req(event->Connection_Handle);
            }
        } break;

        default:
            break;
        }
        break;

    case HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE:
        blue_evt = (evt_blecore_aci*)event_pckt->data;
        switch(blue_evt->ecode) {
            aci_gap_pairing_complete_event_rp0* pairing_complete;

        case ACI_GAP_LIMITED_DISCOVERABLE_VSEVT_CODE:
            FURI_LOG_I(TAG, "Limited discoverable event");
            break;

        case ACI_GAP_PASS_KEY_REQ_VSEVT_CODE: {
            // Generate random PIN code
            uint32_t pin = rand() % 999999; //-V1064
            aci_gap_pass_key_resp(gap->service.connection_handle, pin);
            if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagLock)) {
                FURI_LOG_I(TAG, "Pass key request event. Pin: ******");
            } else {
                FURI_LOG_I(TAG, "Pass key request event. Pin: %06ld", pin);
            }
            GapEvent event = {.type = GapEventTypePinCodeShow, .data.pin_code = pin};
            gap->on_event_cb(event, gap->context);
        } break;

        case ACI_ATT_EXCHANGE_MTU_RESP_VSEVT_CODE: {
            aci_att_exchange_mtu_resp_event_rp0* pr = (void*)blue_evt->data;
            FURI_LOG_I(TAG, "Rx MTU size: %d", pr->Server_RX_MTU);
            // Set maximum packet size given header size is 3 bytes
            GapEvent event = {
                .type = GapEventTypeUpdateMTU, .data.max_packet_size = pr->Server_RX_MTU - 3};
            gap->on_event_cb(event, gap->context);
        } break;

        case ACI_GAP_AUTHORIZATION_REQ_VSEVT_CODE:
            FURI_LOG_D(TAG, "Authorization request event");
            break;

        case ACI_GAP_SLAVE_SECURITY_INITIATED_VSEVT_CODE:
            FURI_LOG_D(TAG, "Slave security initiated");
            gap->is_secure = true;
            break;

        case ACI_GAP_BOND_LOST_VSEVT_CODE:
            FURI_LOG_D(TAG, "Bond lost event. Start rebonding");
            aci_gap_allow_rebond(gap->service.connection_handle);
            break;

        case ACI_GAP_ADDR_NOT_RESOLVED_VSEVT_CODE:
            FURI_LOG_D(TAG, "Address not resolved event");
            break;

        case ACI_GAP_KEYPRESS_NOTIFICATION_VSEVT_CODE:
            FURI_LOG_D(TAG, "Key press notification event");
            break;

        case ACI_GAP_NUMERIC_COMPARISON_VALUE_VSEVT_CODE: {
            uint32_t pin =
                ((aci_gap_numeric_comparison_value_event_rp0*)(blue_evt->data))->Numeric_Value;
            FURI_LOG_I(TAG, "Verify numeric comparison: %06lu", pin);
            GapEvent event = {.type = GapEventTypePinCodeVerify, .data.pin_code = pin};
            bool result = gap->on_event_cb(event, gap->context);
            aci_gap_numeric_comparison_value_confirm_yesno(gap->service.connection_handle, result);
            break;
        }

        case ACI_GAP_PAIRING_COMPLETE_VSEVT_CODE:
            pairing_complete = (aci_gap_pairing_complete_event_rp0*)blue_evt->data;
            if(pairing_complete->Status) {
                FURI_LOG_E(
                    TAG,
                    "Pairing failed with status: %d. Terminating connection",
                    pairing_complete->Status);
                aci_gap_terminate(gap->service.connection_handle, 5);
            } else {
                FURI_LOG_I(TAG, "Pairing complete");
                GapEvent event = {.type = GapEventTypeConnected};
                gap->on_event_cb(event, gap->context); //-V595
            }
            break;

        case ACI_L2CAP_CONNECTION_UPDATE_RESP_VSEVT_CODE:
            FURI_LOG_D(TAG, "Procedure complete event");
            break;

        case ACI_L2CAP_CONNECTION_UPDATE_REQ_VSEVT_CODE: {
            uint16_t result =
                ((aci_l2cap_connection_update_resp_event_rp0*)(blue_evt->data))->Result;
            if(result == 0) {
                FURI_LOG_D(TAG, "Connection parameters accepted");
            } else if(result == 1) {
                FURI_LOG_D(TAG, "Connection parameters denied");
            }
            break;
        }
        }
    default:
        break;
    }

    furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);

    return BleEventFlowEnable;
}

static void set_advertisment_service_uid(uint8_t* uid, uint8_t uid_len) {
    if(uid_len == 2) {
        gap->service.adv_svc_uuid[0] = AD_TYPE_16_BIT_SERV_UUID;
    } else if(uid_len == 4) {
        gap->service.adv_svc_uuid[0] = AD_TYPE_32_BIT_SERV_UUID;
    } else if(uid_len == 16) {
        gap->service.adv_svc_uuid[0] = AD_TYPE_128_BIT_SERV_UUID_CMPLT_LIST;
    }
    memcpy(&gap->service.adv_svc_uuid[gap->service.adv_svc_uuid_len], uid, uid_len);
    gap->service.adv_svc_uuid_len += uid_len;
}

static void set_manufacturer_data(uint8_t* mfg_data, uint8_t mfg_data_len) {
    furi_check(mfg_data_len <= sizeof(gap->service.mfg_data) - 2);
    gap->service.mfg_data[0] = mfg_data_len + 1;
    gap->service.mfg_data[1] = AD_TYPE_MANUFACTURER_SPECIFIC_DATA;
    memcpy(&gap->service.mfg_data[gap->service.mfg_data_len], mfg_data, mfg_data_len);
    gap->service.mfg_data_len += mfg_data_len;
}

static void gap_init_svc(Gap* gap, const GapRootSecurityKeys* root_keys) {
    furi_check(root_keys);

    tBleStatus status;
    uint32_t srd_bd_addr[2];

    // Configure mac address
    aci_hal_write_config_data(
        CONFIG_DATA_PUBADDR_OFFSET, CONFIG_DATA_PUBADDR_LEN, gap->config->mac_address);

    /* Static random Address
     * The two upper bits shall be set to 1
     * The lowest 32bits is read from the UDN to differentiate between devices
     * The RNG may be used to provide a random number on each power on
     */
    srd_bd_addr[1] = 0x0000ED6E;
    srd_bd_addr[0] = LL_FLASH_GetUDN();
    aci_hal_write_config_data(
        CONFIG_DATA_RANDOM_ADDRESS_OFFSET, CONFIG_DATA_RANDOM_ADDRESS_LEN, (uint8_t*)srd_bd_addr);
    // Set Identity root key used to derive LTK and CSRK
    aci_hal_write_config_data(CONFIG_DATA_IR_OFFSET, CONFIG_DATA_IR_LEN, root_keys->irk);
    // Set Encryption root key used to derive LTK and CSRK
    aci_hal_write_config_data(CONFIG_DATA_ER_OFFSET, CONFIG_DATA_ER_LEN, root_keys->erk);
    // Set TX Power to 0 dBm
    aci_hal_set_tx_power_level(1, 0x19);
    // Initialize GATT interface
    aci_gatt_init();
    // Initialize GAP interface
    // Skip first symbol AD_TYPE_COMPLETE_LOCAL_NAME
    char* name = gap->service.adv_name + 1;
    uint8_t gap_role = GAP_PERIPHERAL_ROLE;
    if(gap_client_stack_available()) {
        gap_role |= GAP_CENTRAL_ROLE | GAP_OBSERVER_ROLE;
    }
    aci_gap_init(
        gap_role,
        0,
        strlen(name),
        &gap->service.gap_svc_handle,
        &gap->service.dev_name_char_handle,
        &gap->service.appearance_char_handle);

    // Set GAP characteristics
    status = aci_gatt_update_char_value(
        gap->service.gap_svc_handle,
        gap->service.dev_name_char_handle,
        0,
        strlen(name),
        (uint8_t*)name);
    if(status) {
        FURI_LOG_E(TAG, "Failed updating name characteristic: %d", status);
    }

    uint8_t gap_appearence_char_uuid[2] = {
        gap->config->appearance_char & 0xff, gap->config->appearance_char >> 8};
    status = aci_gatt_update_char_value(
        gap->service.gap_svc_handle,
        gap->service.appearance_char_handle,
        0,
        2,
        gap_appearence_char_uuid);
    if(status) {
        FURI_LOG_E(TAG, "Failed updating appearence characteristic: %d", status);
    }
    // Set default PHY
    hci_le_set_default_phy(ALL_PHYS_PREFERENCE, TX_2M_PREFERRED, RX_2M_PREFERRED);
    // Set I/O capability
    uint8_t auth_req_mitm_mode = MITM_PROTECTION_REQUIRED;
    uint8_t auth_req_use_fixed_pin = USE_FIXED_PIN_FOR_PAIRING_FORBIDDEN;
    bool keypress_supported = false;
    if(gap->config->pairing_method == GapPairingPinCodeShow) {
        aci_gap_set_io_capability(IO_CAP_DISPLAY_ONLY);
    } else if(gap->config->pairing_method == GapPairingPinCodeVerifyYesNo) {
        aci_gap_set_io_capability(IO_CAP_DISPLAY_YES_NO);
        keypress_supported = true;
    } else if(gap->config->pairing_method == GapPairingNone) {
        // "Just works" pairing method (iOS accepts it, it seems Android and Linux don't)
        auth_req_mitm_mode = MITM_PROTECTION_NOT_REQUIRED;
        auth_req_use_fixed_pin = USE_FIXED_PIN_FOR_PAIRING_ALLOWED;
        // If "just works" isn't supported, we want the numeric comparaison method
        aci_gap_set_io_capability(IO_CAP_DISPLAY_YES_NO);
        keypress_supported = true;
    }
    // Setup  authentication
    aci_gap_set_authentication_requirement(
        gap->config->bonding_mode,
        auth_req_mitm_mode,
        CFG_SC_SUPPORT,
        keypress_supported,
        CFG_ENCRYPTION_KEY_SIZE_MIN,
        CFG_ENCRYPTION_KEY_SIZE_MAX,
        auth_req_use_fixed_pin,
        0,
        CFG_IDENTITY_ADDRESS);
    // Configure whitelist
    aci_gap_configure_whitelist();
}

static void gap_advertise_start(GapState new_state) {
    tBleStatus status;
    uint16_t min_interval;
    uint16_t max_interval;

    FURI_LOG_D(TAG, "Start: %d", new_state);

    if(new_state == GapStateAdvFast) {
        min_interval = 0x80; // 80 ms
        max_interval = 0xa0; // 100 ms
    } else {
        min_interval = 0x0640; // 1 s
        max_interval = 0x0fa0; // 2.5 s
    }
    // Stop advertising timer
    furi_timer_stop(gap->advertise_timer);

    if((new_state == GapStateAdvLowPower) &&
       ((gap->state == GapStateAdvFast) || (gap->state == GapStateAdvLowPower))) {
        // Stop advertising
        status = aci_gap_set_non_discoverable();
        if(status) {
            FURI_LOG_E(TAG, "set_non_discoverable failed %d", status);
        } else {
            FURI_LOG_D(TAG, "set_non_discoverable success");
        }
    }

    if(gap->service.mfg_data_len > 0) {
        hci_le_set_scan_response_data(gap->service.mfg_data_len, gap->service.mfg_data);
    }

    // Configure advertising
    status = aci_gap_set_discoverable(
        ADV_IND,
        min_interval,
        max_interval,
        CFG_IDENTITY_ADDRESS,
        0,
        strlen(gap->service.adv_name),
        (uint8_t*)gap->service.adv_name,
        gap->service.adv_svc_uuid_len,
        gap->service.adv_svc_uuid,
        0,
        0);
    if(status) {
        FURI_LOG_E(TAG, "set_discoverable failed %d", status);
    }
    gap->state = new_state;
    GapEvent event = {.type = GapEventTypeStartAdvertising};
    gap->on_event_cb(event, gap->context);
    furi_timer_start(gap->advertise_timer, INITIAL_ADV_TIMEOUT);
}

static void gap_advertise_stop(void) {
    FURI_LOG_D(TAG, "Stop");
    tBleStatus ret;
#if TUMOFLIP_BLE_SCAN
    if(gap->scan_active || gap->scan_start_pending) {
        gap_scan_stop_locked(false);
    }
#endif
    if(gap->state > GapStateIdle) {
#if TUMOFLIP_BLE_GATT_CLIENT
        if((gap->state == GapStateConnected && gap->gatt_client_connected) ||
           gap->state == GapStateConnecting || gap->gatt_connect_pending) {
            gap_gatt_disconnect_locked();
        } else if(gap->state == GapStateConnected) {
            // Terminate connection
            ret = aci_gap_terminate(gap->service.connection_handle, 0x13);
            if(ret != BLE_STATUS_SUCCESS) {
                FURI_LOG_E(TAG, "terminate failed %d", ret);
            } else {
                FURI_LOG_D(TAG, "terminate success");
            }
        }
#else
        if(gap->state == GapStateConnected) {
            ret = aci_gap_terminate(gap->service.connection_handle, 0x13);
            if(ret != BLE_STATUS_SUCCESS) {
                FURI_LOG_E(TAG, "terminate failed %d", ret);
            } else {
                FURI_LOG_D(TAG, "terminate success");
            }
        }
#endif
        // Stop advertising
        furi_timer_stop(gap->advertise_timer);
        if((gap->state == GapStateAdvFast) || (gap->state == GapStateAdvLowPower) ||
           (gap->state == GapStateStartingAdv)) {
            ret = aci_gap_set_non_discoverable();
            if(ret != BLE_STATUS_SUCCESS) {
                FURI_LOG_E(TAG, "set_non_discoverable failed %d", ret);
            } else {
                FURI_LOG_D(TAG, "set_non_discoverable success");
            }
        }
#if TUMOFLIP_BLE_GATT_CLIENT
        if(!gap->gatt_client_connected) gap->state = GapStateIdle;
#else
        gap->state = GapStateIdle;
#endif
    }
    GapEvent event = {.type = GapEventTypeStopAdvertising};
    gap->on_event_cb(event, gap->context);
}

#if TUMOFLIP_BLE_SCAN
static void gap_scan_timer_callback(void* context) {
    UNUSED(context);
    if(!gap || !gap->command_queue) return;
    GapCommand command = GapCommandScanStop;
    furi_message_queue_put(gap->command_queue, &command, 0);
}

static void gap_scan_start_locked(void) {
    furi_check(gap->scan_start_pending);

    // A peripheral advertisement and an observer procedure cannot run at
    // the same time.  Keep the user's advertising preference and restore it
    // once the bounded scan finishes.
    if((gap->state == GapStateAdvFast) || (gap->state == GapStateAdvLowPower) ||
       (gap->state == GapStateStartingAdv)) {
        furi_timer_stop(gap->advertise_timer);
        tBleStatus advertising_status = aci_gap_set_non_discoverable();
        if(advertising_status != BLE_STATUS_SUCCESS) {
            FURI_LOG_W(TAG, "Failed to pause advertising for scan: %d", advertising_status);
        }
        gap->state = GapStateIdle;
    }

    const tBleStatus status = aci_gap_start_observation_proc(
        GAP_SCAN_INTERVAL,
        GAP_SCAN_WINDOW,
        HCI_SCAN_TYPE_PASSIVE,
        CFG_IDENTITY_ADDRESS,
        1U,
        0U);
    gap->scan_start_pending = false;
    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_W(TAG, "Failed to start passive scan: %d", status);
        gap->scan_active = false;
        gap->scan_callback = NULL;
        gap->scan_context = NULL;
        gap->state = GapStateIdle;
        if(gap->scan_restore_adv && gap->enable_adv) {
            gap_advertise_start(GapStateAdvFast);
        }
        gap->scan_restore_adv = false;
        return;
    }

    gap->scan_active = true;
    gap->state = GapStateScanning;
    furi_timer_start(gap->scan_timer, gap->scan_duration_ms);
    FURI_LOG_I(TAG, "Passive scan started for %lu ms", (unsigned long)gap->scan_duration_ms);
}

static void gap_scan_stop_locked(bool restore_advertising) {
    if(!gap->scan_active && !gap->scan_start_pending) return;

    if(gap->scan_active) {
        const tBleStatus status = aci_gap_terminate_gap_proc(GAP_OBSERVATION_PROC);
        if(status != BLE_STATUS_SUCCESS) {
            FURI_LOG_W(TAG, "Failed to stop passive scan: %d", status);
        }
    }
    furi_timer_stop(gap->scan_timer);
    gap->scan_active = false;
    gap->scan_start_pending = false;
    gap->state = GapStateIdle;
    gap->scan_callback = NULL;
    gap->scan_context = NULL;

    if(restore_advertising && gap->enable_adv) {
        gap_advertise_start(GapStateAdvFast);
    }
    gap->scan_restore_adv = false;
    FURI_LOG_I(TAG, "Passive scan stopped");
}
#endif

#if TUMOFLIP_BLE_GATT_CLIENT
static void gap_gatt_connect_locked(void) {
    furi_check(gap->gatt_connect_pending);

    if(gap->state == GapStateAdvFast || gap->state == GapStateAdvLowPower ||
       gap->state == GapStateStartingAdv) {
        furi_timer_stop(gap->advertise_timer);
        const tBleStatus advertising_status = aci_gap_set_non_discoverable();
        if(advertising_status != BLE_STATUS_SUCCESS) {
            FURI_LOG_W(TAG, "Failed to pause advertising for GATT client: %d", advertising_status);
        }
        gap->state = GapStateIdle;
    }

    const tBleStatus status = aci_gap_create_connection(
        GAP_CONNECT_INTERVAL,
        GAP_CONNECT_WINDOW,
        gap->gatt_peer.address_type,
        gap->gatt_peer.address,
        CFG_IDENTITY_ADDRESS,
        GAP_CONNECT_INTERVAL_MIN,
        GAP_CONNECT_INTERVAL_MAX,
        0U,
        GAP_CONNECT_TIMEOUT,
        0U,
        0U);
    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_W(TAG, "Failed to start GATT connection: %d", status);
        gap_emit_gatt_error(status);
        gap->gatt_connect_pending = false;
        gap->state = GapStateIdle;
        if(gap->gatt_restore_adv && gap->enable_adv) {
            gap_advertise_start(GapStateAdvFast);
        }
        gap->gatt_restore_adv = false;
        return;
    }

    gap->state = GapStateConnecting;
    FURI_LOG_I(TAG, "GATT connection procedure started");
}

static void gap_gatt_disconnect_locked(void) {
    if(gap->gatt_client_connected) {
        const tBleStatus status = aci_gap_terminate(gap->gatt_connection_handle, 0x13U);
        if(status != BLE_STATUS_SUCCESS) {
            FURI_LOG_W(TAG, "Failed to terminate GATT connection: %d", status);
        }
    } else if(gap->gatt_connect_pending || gap->state == GapStateConnecting) {
        const tBleStatus status =
            aci_gap_terminate_gap_proc(GAP_DIRECT_CONNECTION_ESTABLISHMENT_PROC);
        if(status != BLE_STATUS_SUCCESS) {
            FURI_LOG_W(TAG, "Failed to cancel GATT connection: %d", status);
        }
        gap->gatt_connect_pending = false;
        gap->state = GapStateIdle;
        if(gap->gatt_restore_adv && gap->enable_adv) {
            gap_advertise_start(GapStateAdvFast);
        }
        gap->gatt_restore_adv = false;
    }
}

static void gap_gatt_discover_services_locked(void) {
    if(!gap->gatt_client_connected) return;
    const tBleStatus status = aci_gatt_disc_all_primary_services(gap->gatt_connection_handle);
    if(status != BLE_STATUS_SUCCESS) {
        gap_emit_gatt_error(status);
    }
}

static void gap_gatt_discover_characteristics_locked(void) {
    if(!gap->gatt_client_connected) return;
    const tBleStatus status = aci_gatt_disc_all_char_of_service(
        gap->gatt_connection_handle,
        gap->gatt_discovery_start_handle,
        gap->gatt_discovery_end_handle);
    if(status != BLE_STATUS_SUCCESS) {
        gap_emit_gatt_error(status);
    }
}

static void gap_gatt_read_locked(void) {
    if(!gap->gatt_client_connected || gap->gatt_attribute_handle == 0U) return;
    const tBleStatus status =
        aci_gatt_read_char_value(gap->gatt_connection_handle, gap->gatt_attribute_handle);
    if(status != BLE_STATUS_SUCCESS) {
        gap_emit_gatt_error(status);
    }
}

static void gap_gatt_write_locked(void) {
    if(!gap->gatt_client_connected || gap->gatt_attribute_handle == 0U ||
       gap->gatt_write_len == 0U) {
        return;
    }
    const tBleStatus status = aci_gatt_write_char_value(
        gap->gatt_connection_handle,
        gap->gatt_attribute_handle,
        gap->gatt_write_len,
        gap->gatt_write_data);
    if(status != BLE_STATUS_SUCCESS) {
        gap_emit_gatt_error(status);
    }
    memset(gap->gatt_write_data, 0, sizeof(gap->gatt_write_data));
    gap->gatt_write_len = 0U;
}

static void gap_gatt_notifications_locked(void) {
    if(!gap->gatt_client_connected || gap->gatt_descriptor_handle == 0U) return;
    const uint8_t value[2] = {
        gap->gatt_notifications_enabled ? 0x01U : 0x00U,
        0x00U,
    };
    const tBleStatus status = aci_gatt_write_char_desc(
        gap->gatt_connection_handle,
        gap->gatt_descriptor_handle,
        sizeof(value),
        value);
    if(status != BLE_STATUS_SUCCESS) {
        gap_emit_gatt_error(status);
    }
}
#endif

void gap_start_advertising(void) {
    furi_check(furi_mutex_acquire(gap->state_mutex, FuriWaitForever) == FuriStatusOk);
    if(gap->state == GapStateIdle) {
        gap->state = GapStateStartingAdv;
        FURI_LOG_I(TAG, "Start advertising");
        gap->enable_adv = true;
        GapCommand command = GapCommandAdvFast;
        furi_check(furi_message_queue_put(gap->command_queue, &command, 0) == FuriStatusOk);
    }
    furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
}

void gap_stop_advertising(void) {
    furi_check(furi_mutex_acquire(gap->state_mutex, FuriWaitForever) == FuriStatusOk);
    if(gap->state > GapStateIdle) {
        FURI_LOG_I(TAG, "Stop advertising");
        gap->enable_adv = false;
        GapCommand command = GapCommandAdvStop;
        furi_check(furi_message_queue_put(gap->command_queue, &command, 0) == FuriStatusOk);
    }
    furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
}

static void gap_advertise_timer_callback(void* context) {
    UNUSED(context);
    GapCommand command = GapCommandAdvLowPower;
    furi_check(furi_message_queue_put(gap->command_queue, &command, 0) == FuriStatusOk);
}

bool gap_init(
    GapConfig* config,
    const GapRootSecurityKeys* root_keys,
    GapEventCallback on_event_cb,
    void* context) {
    if(!ble_glue_is_radio_stack_ready()) {
        return false;
    }

    furi_check(gap == NULL);

    gap = malloc(sizeof(Gap));
    memset(gap, 0, sizeof(Gap));
    gap->config = config;
    // Create advertising timer
    gap->advertise_timer = furi_timer_alloc(gap_advertise_timer_callback, FuriTimerTypeOnce, NULL);
#if TUMOFLIP_BLE_SCAN
    gap->scan_timer = furi_timer_alloc(gap_scan_timer_callback, FuriTimerTypeOnce, NULL);
#endif
    // Initialization of GATT & GAP layer
    gap->service.adv_name = config->adv_name;
    gap_init_svc(gap, root_keys);
    ble_event_dispatcher_init();
    // Initialization of the GAP state
    gap->state_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    gap->state = GapStateIdle;
    gap->service.connection_handle = 0xFFFF;
#if TUMOFLIP_BLE_GATT_CLIENT
    gap->gatt_connection_handle = 0xFFFF;
#endif
    gap->enable_adv = true;

    // Command queue allocation
    gap->command_queue = furi_message_queue_alloc(8, sizeof(GapCommand));

    gap->client_event_handler =
        ble_event_dispatcher_register_svc_handler(gap_client_event_handler, gap);

    // Thread configuration
    gap->thread = furi_thread_alloc_ex("BleGapDriver", 1024, gap_app, gap);
    furi_thread_start(gap->thread);

    // Set initial state
    gap->is_secure = false;
    gap->negotiation_round = 0;

    if(gap->config->mfg_data_len > 0) {
        // Offset by 2 for length + AD_TYPE_MANUFACTURER_SPECIFIC_DATA
        gap->service.mfg_data_len = 2;
        set_manufacturer_data(gap->config->mfg_data, gap->config->mfg_data_len);
    }

    gap->service.adv_svc_uuid_len = 1;
    if(gap->config->adv_service.UUID_Type == UUID_TYPE_16) {
        uint8_t adv_service_uid[2];
        adv_service_uid[0] = gap->config->adv_service.Service_UUID_16 & 0xff;
        adv_service_uid[1] = gap->config->adv_service.Service_UUID_16 >> 8;
        set_advertisment_service_uid(adv_service_uid, sizeof(adv_service_uid));
    } else if(gap->config->adv_service.UUID_Type == UUID_TYPE_128) {
        set_advertisment_service_uid(
            gap->config->adv_service.Service_UUID_128,
            sizeof(gap->config->adv_service.Service_UUID_128));
    } else {
        furi_crash("Invalid UUID type");
    }

    // Set callback
    gap->on_event_cb = on_event_cb;
    gap->context = context;

    return true;
}

GapState gap_get_state(void) {
    GapState state;
    if(gap) {
        furi_check(furi_mutex_acquire(gap->state_mutex, FuriWaitForever) == FuriStatusOk);
        state = gap->state;
        furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
    } else {
        state = GapStateUninitialized;
    }
    return state;
}

void gap_thread_stop(void) {
    if(gap) {
        furi_check(furi_mutex_acquire(gap->state_mutex, FuriWaitForever) == FuriStatusOk);
        gap->enable_adv = false;
        GapCommand command = GapCommandKillThread;
        furi_message_queue_put(gap->command_queue, &command, FuriWaitForever);
        furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
        furi_thread_join(gap->thread);
        furi_thread_free(gap->thread);
        gap->thread = NULL;
        // Free resources
        furi_mutex_free(gap->state_mutex);
        gap->state_mutex = NULL;
        furi_message_queue_free(gap->command_queue);
        gap->command_queue = NULL;
        furi_timer_free(gap->advertise_timer);
        gap->advertise_timer = NULL;
#if TUMOFLIP_BLE_SCAN
        furi_timer_free(gap->scan_timer);
        gap->scan_timer = NULL;
#endif

        if(gap->client_event_handler) {
            ble_event_dispatcher_unregister_svc_handler(gap->client_event_handler);
            gap->client_event_handler = NULL;
        }
        ble_event_dispatcher_reset();
        free(gap);
        gap = NULL;
    }
}

static int32_t gap_app(void* context) {
    UNUSED(context);
    GapCommand command;
    while(1) {
        FuriStatus status = furi_message_queue_get(gap->command_queue, &command, FuriWaitForever);
        if(status != FuriStatusOk) {
            FURI_LOG_E(TAG, "Message queue get error: %d", status);
            continue;
        }
        furi_check(furi_mutex_acquire(gap->state_mutex, FuriWaitForever) == FuriStatusOk);
        if(command == GapCommandKillThread) {
#if TUMOFLIP_BLE_SCAN
            gap_scan_stop_locked(false);
#endif
#if TUMOFLIP_BLE_GATT_CLIENT
            gap_gatt_disconnect_locked();
#endif
            break;
        }
        if(command == GapCommandAdvFast) {
            gap_advertise_start(GapStateAdvFast);
        } else if(command == GapCommandAdvLowPower) {
            gap_advertise_start(GapStateAdvLowPower);
        } else if(command == GapCommandAdvStop) {
            gap_advertise_stop();
#if TUMOFLIP_BLE_SCAN
        } else if(command == GapCommandScanStart) {
            gap_scan_start_locked();
        } else if(command == GapCommandScanStop) {
            gap_scan_stop_locked(true);
#endif
        }
#if TUMOFLIP_BLE_GATT_CLIENT
        else if(command == GapCommandGattConnect) {
            gap_gatt_connect_locked();
        } else if(command == GapCommandGattDisconnect) {
            gap_gatt_disconnect_locked();
        } else if(command == GapCommandGattDiscoverServices) {
            gap_gatt_discover_services_locked();
        } else if(command == GapCommandGattDiscoverCharacteristics) {
            gap_gatt_discover_characteristics_locked();
        } else if(command == GapCommandGattRead) {
            gap_gatt_read_locked();
        } else if(command == GapCommandGattWrite) {
            gap_gatt_write_locked();
        } else if(command == GapCommandGattNotifications) {
            gap_gatt_notifications_locked();
        }
#endif
        furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
    }

    return 0;
}

void gap_emit_ble_beacon_status_event(bool active) {
    GapEvent event = {.type = active ? GapEventTypeBeaconStart : GapEventTypeBeaconStop};
    gap->on_event_cb(event, gap->context);
    FURI_LOG_I(TAG, "Beacon status event: %d", active);
}

#if TUMOFLIP_BLE_SCAN
bool furi_hal_bt_scan_start(
    uint32_t duration_ms,
    FuriHalBtScanResultCallback callback,
    void* context) {
    if(!gap || !callback || !gap_client_stack_available()) return false;
    if(duration_ms < GAP_SCAN_MIN_DURATION_MS) duration_ms = GAP_SCAN_MIN_DURATION_MS;
    if(duration_ms > GAP_SCAN_MAX_DURATION_MS) duration_ms = GAP_SCAN_MAX_DURATION_MS;

    furi_check(furi_mutex_acquire(gap->state_mutex, FuriWaitForever) == FuriStatusOk);
#if TUMOFLIP_BLE_GATT_CLIENT
    const bool can_start =
        !gap->scan_active && !gap->scan_start_pending && !gap->gatt_client_connected &&
        !gap->gatt_connect_pending && gap->state != GapStateConnecting &&
        gap->state != GapStateConnected;
#else
    const bool can_start =
        !gap->scan_active && !gap->scan_start_pending && gap->state != GapStateConnected;
#endif
    bool queued = false;
    if(can_start) {
        gap->scan_restore_adv =
            gap->state == GapStateAdvFast || gap->state == GapStateAdvLowPower ||
            gap->state == GapStateStartingAdv;
        gap->scan_duration_ms = duration_ms;
        gap->scan_callback = callback;
        gap->scan_context = context;
        gap->scan_start_pending = true;
        GapCommand command = GapCommandScanStart;
        if(furi_message_queue_put(gap->command_queue, &command, 0) != FuriStatusOk) {
            gap->scan_start_pending = false;
            gap->scan_restore_adv = false;
            gap->scan_callback = NULL;
            gap->scan_context = NULL;
        } else {
            queued = true;
        }
    }
    furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
    return queued;
}

bool furi_hal_bt_scan_stop(void) {
    if(!gap) return false;
    furi_check(furi_mutex_acquire(gap->state_mutex, FuriWaitForever) == FuriStatusOk);
    const bool was_active = gap->scan_active || gap->scan_start_pending;
    if(was_active) {
        GapCommand command = GapCommandScanStop;
        if(furi_message_queue_put(gap->command_queue, &command, 0) != FuriStatusOk) {
            furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
            return false;
        }
    }
    furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
    return was_active;
}

bool furi_hal_bt_scan_is_active(void) {
    if(!gap) return false;
    furi_check(furi_mutex_acquire(gap->state_mutex, FuriWaitForever) == FuriStatusOk);
    const bool active = gap->scan_active || gap->scan_start_pending;
    furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
    return active;
}
#else
bool furi_hal_bt_scan_start(
    uint32_t duration_ms,
    FuriHalBtScanResultCallback callback,
    void* context) {
    UNUSED(duration_ms);
    UNUSED(callback);
    UNUSED(context);
    return false;
}

bool furi_hal_bt_scan_stop(void) {
    return false;
}

bool furi_hal_bt_scan_is_active(void) {
    return false;
}
#endif

#if TUMOFLIP_BLE_GATT_CLIENT
bool furi_hal_bt_gatt_connect(
    const FuriHalBtPeer* peer,
    FuriHalBtGattEventCallback callback,
    void* context) {
    if(!gap || !peer || !callback || !gap_client_stack_available()) return false;

    furi_check(furi_mutex_acquire(gap->state_mutex, FuriWaitForever) == FuriStatusOk);
    const bool can_start =
        !gap->scan_active && !gap->scan_start_pending && !gap->gatt_client_connected &&
        !gap->gatt_connect_pending && gap->state != GapStateConnected &&
        gap->state != GapStateConnecting;
    bool queued = false;
    if(can_start) {
        gap->gatt_peer = *peer;
        gap->gatt_callback = callback;
        gap->gatt_context = context;
        gap->gatt_connection_handle = 0xFFFFU;
        gap->gatt_restore_adv =
            gap->state == GapStateAdvFast || gap->state == GapStateAdvLowPower ||
            gap->state == GapStateStartingAdv;
        gap->gatt_connect_pending = true;
        GapCommand command = GapCommandGattConnect;
        if(furi_message_queue_put(gap->command_queue, &command, 0) != FuriStatusOk) {
            gap->gatt_connect_pending = false;
            gap->gatt_restore_adv = false;
            gap->gatt_callback = NULL;
            gap->gatt_context = NULL;
        } else {
            queued = true;
        }
    }
    furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
    return queued;
}

bool furi_hal_bt_gatt_disconnect(void) {
    if(!gap) return false;
    furi_check(furi_mutex_acquire(gap->state_mutex, FuriWaitForever) == FuriStatusOk);
    const bool active = gap->gatt_client_connected || gap->gatt_connect_pending;
    if(active) {
        GapCommand command = GapCommandGattDisconnect;
        if(furi_message_queue_put(gap->command_queue, &command, 0) != FuriStatusOk) {
            furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
            return false;
        }
    }
    furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
    return active;
}

bool furi_hal_bt_gatt_discover_services(void) {
    if(!gap) return false;
    furi_check(furi_mutex_acquire(gap->state_mutex, FuriWaitForever) == FuriStatusOk);
    const bool active = gap->gatt_client_connected;
    if(active) {
        GapCommand command = GapCommandGattDiscoverServices;
        if(furi_message_queue_put(gap->command_queue, &command, 0) != FuriStatusOk) {
            furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
            return false;
        }
    }
    furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
    return active;
}

bool furi_hal_bt_gatt_discover_characteristics(uint16_t start_handle, uint16_t end_handle) {
    if(!gap || start_handle == 0U || end_handle < start_handle) return false;
    furi_check(furi_mutex_acquire(gap->state_mutex, FuriWaitForever) == FuriStatusOk);
    const bool active = gap->gatt_client_connected;
    if(active) {
        gap->gatt_discovery_start_handle = start_handle;
        gap->gatt_discovery_end_handle = end_handle;
        GapCommand command = GapCommandGattDiscoverCharacteristics;
        if(furi_message_queue_put(gap->command_queue, &command, 0) != FuriStatusOk) {
            furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
            return false;
        }
    }
    furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
    return active;
}

bool furi_hal_bt_gatt_read(uint16_t attribute_handle) {
    if(!gap || attribute_handle == 0U) return false;
    furi_check(furi_mutex_acquire(gap->state_mutex, FuriWaitForever) == FuriStatusOk);
    const bool active = gap->gatt_client_connected;
    if(active) {
        gap->gatt_attribute_handle = attribute_handle;
        GapCommand command = GapCommandGattRead;
        if(furi_message_queue_put(gap->command_queue, &command, 0) != FuriStatusOk) {
            furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
            return false;
        }
    }
    furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
    return active;
}

bool furi_hal_bt_gatt_write(
    uint16_t attribute_handle,
    const uint8_t* data,
    size_t data_len,
    bool user_confirmed) {
    if(!gap || !data || attribute_handle == 0U || data_len == 0U ||
       data_len > FURI_HAL_BT_GATT_DATA_MAX || !user_confirmed) {
        return false;
    }

    furi_check(furi_mutex_acquire(gap->state_mutex, FuriWaitForever) == FuriStatusOk);
    const bool active = gap->gatt_client_connected;
    if(active) {
        gap->gatt_attribute_handle = attribute_handle;
        gap->gatt_write_len = (uint8_t)data_len;
        memcpy(gap->gatt_write_data, data, data_len);
        GapCommand command = GapCommandGattWrite;
        if(furi_message_queue_put(gap->command_queue, &command, 0) != FuriStatusOk) {
            memset(gap->gatt_write_data, 0, sizeof(gap->gatt_write_data));
            gap->gatt_write_len = 0U;
            furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
            return false;
        }
    }
    furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
    return active;
}

bool furi_hal_bt_gatt_set_notifications(
    uint16_t cccd_handle,
    bool enabled,
    bool user_confirmed) {
    if(!gap || cccd_handle == 0U || !user_confirmed) return false;

    furi_check(furi_mutex_acquire(gap->state_mutex, FuriWaitForever) == FuriStatusOk);
    const bool active = gap->gatt_client_connected;
    if(active) {
        gap->gatt_descriptor_handle = cccd_handle;
        gap->gatt_notifications_enabled = enabled;
        GapCommand command = GapCommandGattNotifications;
        if(furi_message_queue_put(gap->command_queue, &command, 0) != FuriStatusOk) {
            gap->gatt_descriptor_handle = 0U;
            gap->gatt_notifications_enabled = false;
            furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
            return false;
        }
    }
    furi_check(furi_mutex_release(gap->state_mutex) == FuriStatusOk);
    return active;
}
#else
bool furi_hal_bt_gatt_connect(
    const FuriHalBtPeer* peer,
    FuriHalBtGattEventCallback callback,
    void* context) {
    UNUSED(peer);
    UNUSED(callback);
    UNUSED(context);
    return false;
}

bool furi_hal_bt_gatt_disconnect(void) {
    return false;
}

bool furi_hal_bt_gatt_discover_services(void) {
    return false;
}

bool furi_hal_bt_gatt_discover_characteristics(uint16_t start_handle, uint16_t end_handle) {
    UNUSED(start_handle);
    UNUSED(end_handle);
    return false;
}

bool furi_hal_bt_gatt_read(uint16_t attribute_handle) {
    UNUSED(attribute_handle);
    return false;
}

bool furi_hal_bt_gatt_write(
    uint16_t attribute_handle,
    const uint8_t* data,
    size_t data_len,
    bool user_confirmed) {
    UNUSED(attribute_handle);
    UNUSED(data);
    UNUSED(data_len);
    UNUSED(user_confirmed);
    return false;
}

bool furi_hal_bt_gatt_set_notifications(
    uint16_t cccd_handle,
    bool enabled,
    bool user_confirmed) {
    UNUSED(cccd_handle);
    UNUSED(enabled);
    UNUSED(user_confirmed);
    return false;
}
#endif
