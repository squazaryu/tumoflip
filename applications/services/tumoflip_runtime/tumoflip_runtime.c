#include <bt/bt_service/bt_i.h>
#include <furi.h>
#include <furi_hal_info.h>
#include <furi_hal_version.h>
#include <storage/storage.h>
#include <subghz_radio_broker/subghz_radio_broker.h>
#include <string.h>

#define TAG "TumoflipRuntime"

#define TUMOFLIP_RUNTIME_APP_ID            "runtime"
#define TUMOFLIP_RUNTIME_QUEUE_DEPTH       8U
#define TUMOFLIP_RUNTIME_REASSEMBLY_MAX    512U
#define TUMOFLIP_RUNTIME_STATUS_MAX        160U
#define TUMOFLIP_RUNTIME_SESSION_OWNER_MAX 24U
#define TUMOFLIP_RUNTIME_PACKAGE_STATE_PATH EXT_PATH(".tumoflip/package-state.txt")
#define TUMOFLIP_RUNTIME_CAPABILITIES \
    "runtime=1;fab=2;session=3;status=2;packages=1;radio=2;sd=1;" \
    "features=transfer_activity,pkg_state,radio_v2"

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
    uint32_t session_id;
    char owner[TUMOFLIP_RUNTIME_SESSION_OWNER_MAX + 1];
} TumoflipRuntimeSession;

typedef struct {
    Bt* bt;
    Storage* storage;
    SubGhzRadioBroker* radio_broker;
    FuriMessageQueue* queue;
    FuriPubSubSubscription* subscription;
    TumoflipRuntimeAssembly assembly;
    TumoflipRuntimeSession session;
    bool transfer_active;
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

static void tumoflip_runtime_transfer_activity(TumoflipRuntime* runtime, bool active) {
    runtime->transfer_active = active;
    BtMessage message = {
        .lock = api_lock_alloc_locked(),
        .type = BtMessageTypeTransferActivity,
        .data.transfer_active = active,
    };
    furi_check(
        furi_message_queue_put(runtime->bt->message_queue, &message, FuriWaitForever) ==
        FuriStatusOk);
    api_lock_wait_unlock_and_free(message.lock);
}

static void tumoflip_runtime_assembly_reset(TumoflipRuntimeAssembly* assembly) {
    memset(assembly, 0, sizeof(TumoflipRuntimeAssembly));
}

static const char* tumoflip_runtime_str_or_unknown(const char* value) {
    return value ? value : "unknown";
}

static void
    tumoflip_runtime_make_status_payload(TumoflipRuntime* runtime, char* payload, size_t size) {
    const Version* version = furi_hal_version_get_firmware_version();
    uint16_t api_major = 0;
    uint16_t api_minor = 0;
    furi_hal_info_get_api_version(&api_major, &api_minor);
    const uint8_t dirty = version_get_dirty_flag(version) ? 1U : 0U;
    const uint8_t target = version_get_target(version);
    const uint8_t transfer_active = runtime->transfer_active ? 1U : 0U;
    const uint8_t sd_ready = (storage_sd_status(runtime->storage) == FSE_OK) ? 1U : 0U;
    const uint8_t package_state =
        (sd_ready && storage_file_exists(runtime->storage, TUMOFLIP_RUNTIME_PACKAGE_STATE_PATH)) ?
            1U :
            0U;

    SubGhzRadioBrokerStatusV2 radio_status;
    subghz_radio_broker_get_status_v2(runtime->radio_broker, &radio_status);

    snprintf(
        payload,
        size,
        "schema=2;fw=%.8s;commit=%.8s;dirty=%hhu;origin=%.4s;api=%hu.%hu;target=%hhu;"
        "transfer=%hhu;sd=%hhu;pkg=%hhu;sid=%08lX;bo=%.8s;radio=%hhu;owner=%.4s",
        tumoflip_runtime_str_or_unknown(version_get_version(version)),
        tumoflip_runtime_str_or_unknown(version_get_githash(version)),
        dirty,
        tumoflip_runtime_str_or_unknown(version_get_firmware_origin(version)),
        api_major,
        api_minor,
        target,
        transfer_active,
        sd_ready,
        package_state,
        (unsigned long)runtime->session.session_id,
        runtime->session.owner,
        (uint8_t)radio_status.state,
        radio_status.base.owner);
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
    } else if(strcmp(runtime->assembly.command, "status") == 0) {
        char payload[TUMOFLIP_RUNTIME_STATUS_MAX];
        tumoflip_runtime_make_status_payload(runtime, payload, sizeof(payload));
        tumoflip_runtime_reply(runtime, event->request_id, "status", payload, false);
    } else if(strcmp(runtime->assembly.command, "hello") == 0) {
        const size_t owner_prefix_len = 6U;
        const size_t payload_len = runtime->assembly.payload_len;
        size_t owner_len = 0U;
        bool owner_valid = (payload_len > owner_prefix_len) &&
                           (memcmp(runtime->assembly.payload, "owner=", owner_prefix_len) == 0);
        if(owner_valid) {
            owner_len = payload_len - owner_prefix_len;
            owner_valid = owner_len <= TUMOFLIP_RUNTIME_SESSION_OWNER_MAX;
        }

        if(!owner_valid) {
            tumoflip_runtime_reply(runtime, event->request_id, "error", "invalid_owner", true);
        } else {
            runtime->session.session_id = event->request_id;
            memcpy(runtime->session.owner, &runtime->assembly.payload[owner_prefix_len], owner_len);
            runtime->session.owner[owner_len] = '\0';

            char response[48];
            snprintf(
                response,
                sizeof(response),
                "sid=%08lX",
                (unsigned long)runtime->session.session_id);
            tumoflip_runtime_reply(runtime, event->request_id, "hello", response, false);
        }
    } else if(strcmp(runtime->assembly.command, "transfer_begin") == 0) {
        tumoflip_runtime_transfer_activity(runtime, true);
        tumoflip_runtime_reply(runtime, event->request_id, "transfer_begin", "ok", false);
    } else if(strcmp(runtime->assembly.command, "transfer_progress") == 0) {
        tumoflip_runtime_transfer_activity(runtime, true);
        tumoflip_runtime_reply(runtime, event->request_id, "transfer_progress", "ok", false);
    } else if(strcmp(runtime->assembly.command, "transfer_end") == 0) {
        tumoflip_runtime_transfer_activity(runtime, false);
        tumoflip_runtime_reply(runtime, event->request_id, "transfer_end", "ok", false);
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
    runtime->storage = furi_record_open(RECORD_STORAGE);
    runtime->radio_broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
    runtime->subscription = furi_pubsub_subscribe(
        bt_app_bridge_get_pubsub(runtime->bt), tumoflip_runtime_bridge_callback, runtime);

    TumoflipRuntimeMessage message;
    while(true) {
        if(furi_message_queue_get(runtime->queue, &message, FuriWaitForever) == FuriStatusOk) {
            tumoflip_runtime_handle_request(runtime, &message.event);
        }
    }

    return 0;
}
