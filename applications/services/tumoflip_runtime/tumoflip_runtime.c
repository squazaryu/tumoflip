#include <bt/bt_service/bt_i.h>
#include <furi.h>
#include <furi_hal_info.h>
#include <furi_hal_power.h>
#include <furi_hal_version.h>
#include <tumoflip_runtime/tumoflip_runtime.h>
#include "tumofabric_core.h"
#include <storage/storage.h>
#include <subghz_radio_broker/subghz_radio_broker.h>
#include <stdio.h>
#include <string.h>

#define TUMOFLIP_RUNTIME_APP_ID            "runtime"
#define TUMOFLIP_RUNTIME_QUEUE_DEPTH       8U
#define TUMOFLIP_RUNTIME_STATUS_MAX        160U
#define TUMOFLIP_RUNTIME_TRACE_DEPTH       8U
#define TUMOFLIP_RUNTIME_TRACE_MAX         160U
#define TUMOFLIP_RUNTIME_TWIN_MAX          160U
#define TUMOFLIP_RUNTIME_FABRIC_MAX        160U
#define TUMOFLIP_RUNTIME_SESSION_OWNER_MAX 24U
#define TUMOFLIP_RUNTIME_PACKAGE_STATE_PATH EXT_PATH(".tumoflip/package-state.txt")
#define TUMOFLIP_RUNTIME_CAPABILITIES \
    "runtime=1;fab=2;session=3;status=2;trace=1;twin=1;pkg=1;radio=2;sd=1;" \
    "fabric=1;feat=pkg,radio,trace,twin,transfer,fabric"

typedef struct {
    BtAppBridgeEvent event;
} TumoflipRuntimeMessage;

typedef struct {
    uint32_t session_id;
    char owner[TUMOFLIP_RUNTIME_SESSION_OWNER_MAX + 1];
} TumoflipRuntimeSession;

typedef struct {
    char code;
    char command;
    bool error;
} TumoflipRuntimeTraceEvent;

typedef struct {
    Bt* bt;
    Storage* storage;
    SubGhzRadioBroker* radio_broker;
    FuriMessageQueue* queue;
    FuriMutex* trace_mutex;
    FuriMutex* fabric_mutex;
    FuriPubSubSubscription* subscription;
    TumoflipRuntimeApi api;
    TumoflipRuntimeSession session;
    TumoFabricState fabric;
    TumoflipRuntimeTraceEvent trace[TUMOFLIP_RUNTIME_TRACE_DEPTH];
    uint8_t trace_head;
    uint8_t trace_count;
    uint32_t trace_dropped;
    bool transfer_active;
} TumoflipRuntime;

static void tumoflip_runtime_trace_add(
    TumoflipRuntime* runtime,
    char code,
    const char* command,
    bool error);
static bool tumoflip_runtime_api_get_trace(
    TumoflipRuntimeApi* api,
    char* output,
    size_t output_size);
static bool tumoflip_runtime_api_get_fabric_state(
    TumoflipRuntimeApi* api,
    TumoflipRuntimeFabricSnapshot* snapshot);
static bool tumoflip_runtime_api_cancel_fabric(TumoflipRuntimeApi* api);
static bool tumoflip_runtime_api_open_local_fabric(TumoflipRuntimeApi* api);
static bool tumoflip_runtime_api_step_local_fabric(TumoflipRuntimeApi* api, int8_t delta);

static void tumoflip_runtime_bridge_callback(const void* message, void* context) {
    TumoflipRuntime* runtime = context;
    const BtAppBridgeEvent* event = message;

    if((event->protocol_version != 2) || (strcmp(event->app_id, TUMOFLIP_RUNTIME_APP_ID) != 0) ||
       (event->flags & BtAppBridgeFlagResponse)) {
        return;
    }

    const TumoflipRuntimeMessage runtime_message = {.event = *event};
    furi_message_queue_put(runtime->queue, &runtime_message, 0);
}

static void tumoflip_runtime_reply(
    TumoflipRuntime* runtime,
    uint32_t request_id,
    const char* command,
    const char* payload,
    bool error) {
    const uint8_t flags = BtAppBridgeFlagResponse | (error ? BtAppBridgeFlagError : 0);
    bt_app_bridge_send_text_v2(
        runtime->bt, TUMOFLIP_RUNTIME_APP_ID, command, request_id, flags, payload);
    tumoflip_runtime_trace_add(runtime, error ? 'e' : 't', command, error);
}

static const char* tumoflip_runtime_str_or_unknown(const char* value) {
    return value ? value : "unknown";
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

static void tumoflip_runtime_trace_add(
    TumoflipRuntime* runtime,
    char code,
    const char* command,
    bool error) {
    furi_check(furi_mutex_acquire(runtime->trace_mutex, FuriWaitForever) == FuriStatusOk);
    TumoflipRuntimeTraceEvent* trace = &runtime->trace[runtime->trace_head];
    trace->code = code;
    trace->command = (command && command[0]) ? command[0] : '-';
    trace->error = error;

    runtime->trace_head = (runtime->trace_head + 1U) % TUMOFLIP_RUNTIME_TRACE_DEPTH;
    if(runtime->trace_count < TUMOFLIP_RUNTIME_TRACE_DEPTH) {
        runtime->trace_count++;
    } else {
        runtime->trace_dropped++;
    }
    furi_check(furi_mutex_release(runtime->trace_mutex) == FuriStatusOk);
}

static void tumoflip_runtime_make_trace_payload(
    TumoflipRuntime* runtime,
    char* payload,
    size_t size) {
    furi_check(furi_mutex_acquire(runtime->trace_mutex, FuriWaitForever) == FuriStatusOk);
    int written = snprintf(
        payload,
        size,
        "schema=1;depth=%u;count=%u;drop=%lu",
        TUMOFLIP_RUNTIME_TRACE_DEPTH,
        runtime->trace_count,
        (unsigned long)runtime->trace_dropped);
    if(written < 0) goto out;
    size_t used = (size_t)written;
    if(used >= size) goto out;

    for(uint8_t offset = 0; offset < runtime->trace_count; offset++) {
        const uint8_t index =
            (runtime->trace_head + TUMOFLIP_RUNTIME_TRACE_DEPTH - runtime->trace_count + offset) %
            TUMOFLIP_RUNTIME_TRACE_DEPTH;
        const TumoflipRuntimeTraceEvent* trace = &runtime->trace[index];

        written = snprintf(
            &payload[used],
            size - used,
            "|%c,%c,%c",
            trace->code,
            trace->command,
            trace->error ? 'e' : 'o');

        if(written < 0) break;
        used += (size_t)written;
        if(used >= size) break;
    }

out:
    furi_check(furi_mutex_release(runtime->trace_mutex) == FuriStatusOk);
}

static bool tumoflip_runtime_api_get_trace(
    TumoflipRuntimeApi* api,
    char* output,
    size_t output_size) {
    furi_assert(api);
    furi_assert(output);
    TumoflipRuntime* runtime = api->context;
    if(!runtime || output_size == 0U) {
        return false;
    }

    tumoflip_runtime_make_trace_payload(runtime, output, output_size);
    output[output_size - 1U] = '\0';
    return true;
}

static bool tumoflip_runtime_api_get_fabric_state(
    TumoflipRuntimeApi* api,
    TumoflipRuntimeFabricSnapshot* snapshot) {
    furi_assert(api);
    furi_assert(snapshot);
    TumoflipRuntime* runtime = api->context;
    if(!runtime) return false;

    furi_check(furi_mutex_acquire(runtime->fabric_mutex, FuriWaitForever) == FuriStatusOk);
    snapshot->active = runtime->fabric.active;
    snapshot->session_id = runtime->fabric.session_id;
    snapshot->token = runtime->fabric.token;
    snapshot->last_sequence = runtime->fabric.last_sequence;
    snapshot->counter = runtime->fabric.counter;
    strlcpy(snapshot->owner, runtime->fabric.owner, sizeof(snapshot->owner));
    furi_check(furi_mutex_release(runtime->fabric_mutex) == FuriStatusOk);
    return true;
}

static bool tumoflip_runtime_api_cancel_fabric(TumoflipRuntimeApi* api) {
    furi_assert(api);
    TumoflipRuntime* runtime = api->context;
    if(!runtime) return false;

    furi_check(furi_mutex_acquire(runtime->fabric_mutex, FuriWaitForever) == FuriStatusOk);
    tumofabric_init(&runtime->fabric);
    furi_check(furi_mutex_release(runtime->fabric_mutex) == FuriStatusOk);
    tumoflip_runtime_trace_add(runtime, 's', "fabric_cancel", false);
    return true;
}

static bool tumoflip_runtime_api_open_local_fabric(TumoflipRuntimeApi* api) {
    furi_assert(api);
    TumoflipRuntime* runtime = api->context;
    if(!runtime) return false;

    furi_check(furi_mutex_acquire(runtime->fabric_mutex, FuriWaitForever) == FuriStatusOk);
    const bool opened = tumofabric_open_local(&runtime->fabric);
    furi_check(furi_mutex_release(runtime->fabric_mutex) == FuriStatusOk);
    return opened;
}

static bool tumoflip_runtime_api_step_local_fabric(TumoflipRuntimeApi* api, int8_t delta) {
    furi_assert(api);
    TumoflipRuntime* runtime = api->context;
    if(!runtime) return false;

    furi_check(furi_mutex_acquire(runtime->fabric_mutex, FuriWaitForever) == FuriStatusOk);
    const bool stepped = tumofabric_step_local(&runtime->fabric, delta);
    furi_check(furi_mutex_release(runtime->fabric_mutex) == FuriStatusOk);
    return stepped;
}

static void tumoflip_runtime_handle_fabric(
    TumoflipRuntime* runtime,
    const BtAppBridgeEvent* event) {
    char response[TUMOFLIP_RUNTIME_FABRIC_MAX];
    TumoFabricResult result = TumoFabricResultPayload;

    furi_check(furi_mutex_acquire(runtime->fabric_mutex, FuriWaitForever) == FuriStatusOk);
    if(strcmp(event->command, "fabric_open") == 0) {
        result = tumofabric_open(
            &runtime->fabric,
            event->request_id,
            event->payload,
            event->payload_len,
            response,
            sizeof(response));
    } else if(strcmp(event->command, "fabric_state") == 0) {
        result = tumofabric_get_state(
            &runtime->fabric,
            event->payload,
            event->payload_len,
            response,
            sizeof(response));
    } else if(strcmp(event->command, "fabric_step") == 0) {
        result = tumofabric_step(
            &runtime->fabric,
            event->payload,
            event->payload_len,
            response,
            sizeof(response));
    } else if(strcmp(event->command, "fabric_cancel") == 0) {
        result = tumofabric_cancel(
            &runtime->fabric,
            event->payload,
            event->payload_len,
            response,
            sizeof(response));
    }
    furi_check(furi_mutex_release(runtime->fabric_mutex) == FuriStatusOk);

    if(result == TumoFabricResultOk) {
        tumoflip_runtime_reply(runtime, event->request_id, event->command, response, false);
    } else {
        tumoflip_runtime_reply(
            runtime, event->request_id, "error", tumofabric_result_token(result), true);
    }
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

static void
    tumoflip_runtime_make_twin_payload(TumoflipRuntime* runtime, char* payload, size_t size) {
    const Version* version = furi_hal_version_get_firmware_version();
    const uint8_t dirty = version_get_dirty_flag(version) ? 1U : 0U;
    const uint8_t sd_ready = (storage_sd_status(runtime->storage) == FSE_OK) ? 1U : 0U;
    const uint8_t package_state =
        (sd_ready && storage_file_exists(runtime->storage, TUMOFLIP_RUNTIME_PACKAGE_STATE_PATH)) ?
            1U :
            0U;
    const uint8_t charging = furi_hal_power_is_charging() ? 1U : 0U;
    const uint8_t otg = furi_hal_power_is_otg_enabled() ? 1U : 0U;

    SubGhzRadioBrokerStatusV2 radio_status;
    subghz_radio_broker_get_status_v2(runtime->radio_broker, &radio_status);

    snprintf(
        payload,
        size,
        "schema=1;fw=%.8s;cm=%.8s;dy=%hhu;sd=%hhu;pkg=%hhu;bat=%u;chg=%hhu;otg=%hhu;"
        "heap=%lu;rf=%hhu;ro=%.4s;sid=%08lX;bo=%.8s",
        tumoflip_runtime_str_or_unknown(version_get_version(version)),
        tumoflip_runtime_str_or_unknown(version_get_githash(version)),
        dirty,
        sd_ready,
        package_state,
        furi_hal_power_get_pct(),
        charging,
        otg,
        (unsigned long)memmgr_heap_get_max_free_block(),
        (uint8_t)radio_status.state,
        radio_status.base.owner,
        (unsigned long)runtime->session.session_id,
        runtime->session.owner);
}

static void
    tumoflip_runtime_handle_request(TumoflipRuntime* runtime, const BtAppBridgeEvent* event) {
    const char* command = event->command;
    const uint8_t* payload = event->payload;
    const size_t payload_len = event->payload_len;

    if((event->chunk_index != 0U) || (event->chunk_count != 1U)) {
        tumoflip_runtime_trace_add(runtime, 'e', command, true);
        tumoflip_runtime_reply(runtime, event->request_id, "error", "chunk", true);
        return;
    }

    tumoflip_runtime_trace_add(runtime, 'r', command, false);

    if(strcmp(command, "ping") == 0) {
        tumoflip_runtime_reply(runtime, event->request_id, "pong", "ok", false);
    } else if(strcmp(command, "capabilities") == 0) {
        tumoflip_runtime_reply(
            runtime, event->request_id, "capabilities", TUMOFLIP_RUNTIME_CAPABILITIES, false);
    } else if(strcmp(command, "status") == 0) {
        char payload[TUMOFLIP_RUNTIME_STATUS_MAX];
        tumoflip_runtime_make_status_payload(runtime, payload, sizeof(payload));
        tumoflip_runtime_reply(runtime, event->request_id, "status", payload, false);
    } else if(strcmp(command, "trace") == 0) {
        char payload[TUMOFLIP_RUNTIME_TRACE_MAX];
        tumoflip_runtime_make_trace_payload(runtime, payload, sizeof(payload));
        tumoflip_runtime_reply(runtime, event->request_id, "trace", payload, false);
    } else if(strcmp(command, "twin") == 0) {
        char payload[TUMOFLIP_RUNTIME_TWIN_MAX];
        tumoflip_runtime_make_twin_payload(runtime, payload, sizeof(payload));
        tumoflip_runtime_reply(runtime, event->request_id, "twin", payload, false);
    } else if(strcmp(command, "fabric_caps") == 0) {
        tumoflip_runtime_reply(
            runtime,
            event->request_id,
            "fabric_caps",
            "schema=1;node=flipper;pkg=counter;ops=inc,dec;resume=1;persist=ram;trust=ble-bond",
            false);
    } else if(
        strcmp(command, "fabric_open") == 0 || strcmp(command, "fabric_state") == 0 ||
        strcmp(command, "fabric_step") == 0 || strcmp(command, "fabric_cancel") == 0) {
        tumoflip_runtime_handle_fabric(runtime, event);
    } else if(strcmp(command, "transfer_begin") == 0) {
        tumoflip_runtime_transfer_activity(runtime, true);
        tumoflip_runtime_reply(runtime, event->request_id, "transfer_begin", "ok", false);
    } else if(strcmp(command, "transfer_progress") == 0) {
        tumoflip_runtime_transfer_activity(runtime, true);
        tumoflip_runtime_reply(runtime, event->request_id, "transfer_progress", "ok", false);
    } else if(strcmp(command, "transfer_end") == 0) {
        tumoflip_runtime_transfer_activity(runtime, false);
        tumoflip_runtime_reply(runtime, event->request_id, "transfer_end", "ok", false);
    } else if(strcmp(command, "hello") == 0) {
        const size_t owner_prefix_len = 6U;
        size_t owner_len = 0U;
        bool owner_valid = (payload_len > owner_prefix_len) &&
                           (memcmp(payload, "owner=", owner_prefix_len) == 0);
        if(owner_valid) {
            owner_len = payload_len - owner_prefix_len;
            owner_valid = owner_len <= TUMOFLIP_RUNTIME_SESSION_OWNER_MAX;
        }

        if(!owner_valid) {
            tumoflip_runtime_trace_add(runtime, 'e', "hello", true);
            tumoflip_runtime_reply(runtime, event->request_id, "error", "owner", true);
        } else {
            runtime->session.session_id = event->request_id;
            memcpy(runtime->session.owner, &payload[owner_prefix_len], owner_len);
            runtime->session.owner[owner_len] = '\0';
            tumoflip_runtime_trace_add(runtime, 's', runtime->session.owner, false);

            char response[48];
            snprintf(
                response,
                sizeof(response),
                "sid=%08lX",
                (unsigned long)runtime->session.session_id);
            tumoflip_runtime_reply(runtime, event->request_id, "hello", response, false);
        }
    } else {
        tumoflip_runtime_trace_add(runtime, 'e', command, true);
        tumoflip_runtime_reply(runtime, event->request_id, "error", "badcmd", true);
    }
}

int32_t tumoflip_runtime_srv(void* context) {
    UNUSED(context);

    TumoflipRuntime* runtime = malloc(sizeof(TumoflipRuntime));
    memset(runtime, 0, sizeof(TumoflipRuntime));
    runtime->queue =
        furi_message_queue_alloc(TUMOFLIP_RUNTIME_QUEUE_DEPTH, sizeof(TumoflipRuntimeMessage));
    runtime->trace_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    runtime->fabric_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    runtime->api.get_trace = tumoflip_runtime_api_get_trace;
    runtime->api.get_fabric_state = tumoflip_runtime_api_get_fabric_state;
    runtime->api.cancel_fabric = tumoflip_runtime_api_cancel_fabric;
    runtime->api.open_local_fabric = tumoflip_runtime_api_open_local_fabric;
    runtime->api.step_local_fabric = tumoflip_runtime_api_step_local_fabric;
    runtime->api.context = runtime;
    tumofabric_init(&runtime->fabric);
    runtime->bt = furi_record_open(RECORD_BT);
    runtime->storage = furi_record_open(RECORD_STORAGE);
    runtime->radio_broker = furi_record_open(RECORD_SUBGHZ_RADIO_BROKER);
    furi_record_create(RECORD_TUMOFLIP_RUNTIME, &runtime->api);
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
