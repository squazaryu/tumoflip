#include <bt/bt_service/bt.h>
#include <furi.h>
#include <subghz_radio_broker/subghz_radio_broker.h>
#include <string.h>

#define TAG "TumoflipRuntime"

#define TUMOFLIP_RUNTIME_APP_ID         "runtime"
#define TUMOFLIP_RUNTIME_QUEUE_DEPTH    8U
#define TUMOFLIP_RUNTIME_REASSEMBLY_MAX 512U
#define TUMOFLIP_RUNTIME_CAPABILITIES                                            \
    "runtime=1;fab=2;packages=2;legacy=1;payload=160;chunks=255;reassembly=512;" \
    "features=request_id,chunking,error,capabilities,radio_status"

typedef struct {
    BtAppBridgeEvent event;
} TumoflipRuntimeMessage;

typedef struct {
    bool active;
    uint32_t request_id;
    uint8_t next_chunk;
    uint8_t chunk_count;
    char command[BT_APP_BRIDGE_COMMAND_LEN_MAX + 1];
    uint8_t payload[TUMOFLIP_RUNTIME_REASSEMBLY_MAX];
    size_t payload_len;
} TumoflipRuntimeAssembly;

typedef struct {
    Bt* bt;
    SubGhzRadioBroker* radio_broker;
    FuriMessageQueue* queue;
    FuriPubSubSubscription* subscription;
    TumoflipRuntimeAssembly assembly;
} TumoflipRuntime;

static void tumoflip_runtime_bridge_callback(const void* message, void* context) {
    TumoflipRuntime* runtime = context;
    const BtAppBridgeEvent* event = message;

    if((event->protocol_version != 2) || (strcmp(event->app_id, TUMOFLIP_RUNTIME_APP_ID) != 0) ||
       (event->flags & BtAppBridgeFlagResponse)) {
        return;
    }

    const TumoflipRuntimeMessage runtime_message = {.event = *event};
    if(furi_message_queue_put(runtime->queue, &runtime_message, 0) != FuriStatusOk) {
        FURI_LOG_W(TAG, "Dropping bridge request: queue is full");
    }
}

static void tumoflip_runtime_reply(
    TumoflipRuntime* runtime,
    uint32_t request_id,
    const char* command,
    const char* payload,
    bool error) {
    const uint8_t flags = BtAppBridgeFlagResponse | (error ? BtAppBridgeFlagError : 0);
    if(!bt_app_bridge_send_text_v2(
           runtime->bt, TUMOFLIP_RUNTIME_APP_ID, command, request_id, flags, payload)) {
        FURI_LOG_W(TAG, "Failed to send %s response", command);
    }
}

static void tumoflip_runtime_assembly_reset(TumoflipRuntimeAssembly* assembly) {
    memset(assembly, 0, sizeof(TumoflipRuntimeAssembly));
}

static bool
    tumoflip_runtime_assembly_append(TumoflipRuntime* runtime, const BtAppBridgeEvent* event) {
    TumoflipRuntimeAssembly* assembly = &runtime->assembly;

    if(event->chunk_index == 0) {
        tumoflip_runtime_assembly_reset(assembly);
        assembly->active = true;
        assembly->request_id = event->request_id;
        assembly->chunk_count = event->chunk_count;
        strlcpy(assembly->command, event->command, sizeof(assembly->command));
    }

    if(!assembly->active || (assembly->request_id != event->request_id) ||
       (assembly->chunk_count != event->chunk_count) ||
       (assembly->next_chunk != event->chunk_index) ||
       (strcmp(assembly->command, event->command) != 0)) {
        tumoflip_runtime_reply(runtime, event->request_id, "error", "invalid_chunk_order", true);
        tumoflip_runtime_assembly_reset(assembly);
        return false;
    }

    if((assembly->payload_len + event->payload_len) > TUMOFLIP_RUNTIME_REASSEMBLY_MAX) {
        tumoflip_runtime_reply(runtime, event->request_id, "error", "payload_too_large", true);
        tumoflip_runtime_assembly_reset(assembly);
        return false;
    }

    memcpy(&assembly->payload[assembly->payload_len], event->payload, event->payload_len);
    assembly->payload_len += event->payload_len;
    assembly->next_chunk++;
    return assembly->next_chunk == assembly->chunk_count;
}

static void
    tumoflip_runtime_handle_request(TumoflipRuntime* runtime, const BtAppBridgeEvent* event) {
    if(!tumoflip_runtime_assembly_append(runtime, event)) return;

    if(strcmp(runtime->assembly.command, "ping") == 0) {
        tumoflip_runtime_reply(runtime, event->request_id, "pong", "ok", false);
    } else if(strcmp(runtime->assembly.command, "capabilities") == 0) {
        tumoflip_runtime_reply(
            runtime, event->request_id, "capabilities", TUMOFLIP_RUNTIME_CAPABILITIES, false);
    } else if(strcmp(runtime->assembly.command, "radio_status") == 0) {
        SubGhzRadioBrokerStatus status;
        subghz_radio_broker_get_status(runtime->radio_broker, &status);
        const char* device = "internal";
        if(status.selected_device == SubGhzRadioBrokerDeviceExternalCC1101) {
            device = "external";
        } else if(status.selected_device == SubGhzRadioBrokerDeviceDual) {
            device = "dual";
        }
        char payload[128];
        snprintf(
            payload,
            sizeof(payload),
            "busy=%u;external_power=%u;device=%s;owner=%s",
            status.busy,
            status.external_powered,
            device,
            status.owner);
        tumoflip_runtime_reply(runtime, event->request_id, "radio_status", payload, false);
    } else {
        tumoflip_runtime_reply(runtime, event->request_id, "error", "unsupported_command", true);
    }

    tumoflip_runtime_assembly_reset(&runtime->assembly);
}

int32_t tumoflip_runtime_srv(void* context) {
    UNUSED(context);

    TumoflipRuntime* runtime = malloc(sizeof(TumoflipRuntime));
    memset(runtime, 0, sizeof(TumoflipRuntime));
    runtime->queue =
        furi_message_queue_alloc(TUMOFLIP_RUNTIME_QUEUE_DEPTH, sizeof(TumoflipRuntimeMessage));
    runtime->bt = furi_record_open(RECORD_BT);
    runtime->radio_broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
    runtime->subscription = furi_pubsub_subscribe(
        bt_app_bridge_get_pubsub(runtime->bt), tumoflip_runtime_bridge_callback, runtime);

    FURI_LOG_I(TAG, "Runtime ready");
    TumoflipRuntimeMessage message;
    while(true) {
        if(furi_message_queue_get(runtime->queue, &message, FuriWaitForever) == FuriStatusOk) {
            tumoflip_runtime_handle_request(runtime, &message.event);
        }
    }

    return 0;
}
