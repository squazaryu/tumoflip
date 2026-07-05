#include <bt/bt_service/bt_i.h>
#include <furi.h>
#include <furi_hal_info.h>
#include <furi_hal_power.h>
#include <furi_hal_version.h>
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
#define TUMOFLIP_RUNTIME_SESSION_OWNER_MAX 24U
#define TUMOFLIP_RUNTIME_PACKAGE_STATE_PATH EXT_PATH(".tumoflip/package-state.txt")
#define TUMOFLIP_RUNTIME_CAPABILITIES \
    "runtime=1;fab=2;session=3;status=2;trace=1;twin=1;pkg=1;radio=2;sd=1;" \
    "feat=pkg,radio,trace,twin"

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
    FuriPubSubSubscription* subscription;
    TumoflipRuntimeSession session;
    TumoflipRuntimeTraceEvent trace[TUMOFLIP_RUNTIME_TRACE_DEPTH];
    uint8_t trace_head;
    uint8_t trace_count;
} TumoflipRuntime;

static void tumoflip_runtime_trace_add(
    TumoflipRuntime* runtime,
    char code,
    const char* command,
    bool error);

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

static void tumoflip_runtime_trace_add(
    TumoflipRuntime* runtime,
    char code,
    const char* command,
    bool error) {
    TumoflipRuntimeTraceEvent* trace = &runtime->trace[runtime->trace_head];
    trace->code = code;
    trace->command = (command && command[0]) ? command[0] : '-';
    trace->error = error;

    runtime->trace_head = (runtime->trace_head + 1U) % TUMOFLIP_RUNTIME_TRACE_DEPTH;
    if(runtime->trace_count < TUMOFLIP_RUNTIME_TRACE_DEPTH) {
        runtime->trace_count++;
    }
}

static void tumoflip_runtime_make_trace_payload(
    TumoflipRuntime* runtime,
    char* payload,
    size_t size) {
    int written = snprintf(
        payload,
        size,
        "schema=1;depth=%u;count=%u",
        TUMOFLIP_RUNTIME_TRACE_DEPTH,
        runtime->trace_count);
    if(written < 0) return;
    size_t used = (size_t)written;
    if(used >= size) return;

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
}

static void
    tumoflip_runtime_make_status_payload(TumoflipRuntime* runtime, char* payload, size_t size) {
    const Version* version = furi_hal_version_get_firmware_version();
    uint16_t api_major = 0;
    uint16_t api_minor = 0;
    furi_hal_info_get_api_version(&api_major, &api_minor);
    const uint8_t dirty = version_get_dirty_flag(version) ? 1U : 0U;
    const uint8_t target = version_get_target(version);
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
        0U,
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

    SubGhzRadioBrokerStatusV2 radio_status;
    subghz_radio_broker_get_status_v2(runtime->radio_broker, &radio_status);

    snprintf(
        payload,
        size,
        "schema=1;fw=%.8s;cm=%.8s;dy=%hhu;sd=%hhu;pkg=%hhu;bat=%u;rf=%hhu;"
        "ro=%.4s;sid=%08lX;bo=%.8s",
        tumoflip_runtime_str_or_unknown(version_get_version(version)),
        tumoflip_runtime_str_or_unknown(version_get_githash(version)),
        dirty,
        sd_ready,
        package_state,
        furi_hal_power_get_pct(),
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
